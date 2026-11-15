#!/usr/bin/env bash
# Modes: debug (default), release, test, test-neon, test-wasm, test-avx512, fuzz, bench, codegen.
set -e
cd "$(dirname "$0")"

MODE="${1:-debug}"

COMMON_FLAGS=(
    -std=c99
    -Wall
    -Wextra
    -pedantic
    -fshort-enums
)

DEBUG_FLAGS=(
    -O0
    -g
    -fsanitize=address
    -fsanitize=undefined
)

RELEASE_FLAGS=(
    -O3
    -Werror
    -march=native
)

# Regenerate usfm.h tables when the generator or its data changed.
run_codegen() {
    mkdir -p build
    if [[ ! -f build/codegen.stamp \
          || codegen/codegen.c -nt build/codegen.stamp \
          || codegen/markers.h -nt build/codegen.stamp ]]; then
        (cd codegen && ./build.sh && ./codegen --overwrite)
        touch build/codegen.stamp
    fi
}

# Unpack the WEB corpus once (and again whenever the archive changes).
unpack_corpus() {
    mkdir -p build/corpus
    if [[ ! -f build/corpus/WEB.stamp || tests/corpus/WEB.tar.gz -nt build/corpus/WEB.stamp ]]; then
        rm -rf build/corpus/WEB
        tar -xzf tests/corpus/WEB.tar.gz -C build/corpus
        touch build/corpus/WEB.stamp
    fi
}

# Every path must produce the same AST checksum on WEB.
check_checksums() {
    local distinct
    distinct=$(cut -d' ' -f2 build/checksums.txt | sort -u | wc -l)
    if [[ "$distinct" -ne 1 ]]; then
        echo "checksums DIFFER across paths:" >&2
        cat build/checksums.txt >&2
        exit 1
    fi
    echo "checksums: identical across $(wc -l < build/checksums.txt) paths"
}

case "$MODE" in
    debug)
        run_codegen
        gcc "${COMMON_FLAGS[@]}" "${DEBUG_FLAGS[@]}" main.c -o usfm
        ;;
    release)
        run_codegen
        clang "${COMMON_FLAGS[@]}" "${RELEASE_FLAGS[@]}" main.c -o usfm
        ;;
    test)
        run_codegen
        mkdir -p build
        unpack_corpus
        g++ -std=c++11 -Wall -Wextra -fsyntax-only tests/cpp_check.cpp
        rm -f build/checksums.txt
        # Every native SIMD path: unit tests and the WEB corpus test.
        PATHS=("scalar:-DUSFM_SIMD_NONE" "sse2:")
        if grep -q ' avx2' /proc/cpuinfo; then PATHS+=("avx2:-mavx2"); else echo "avx2: skipped (CPU lacks AVX2)"; fi
        for entry in "${PATHS[@]}"; do
            name="${entry%%:*}"
            flags="${entry#*:}"
            # shellcheck disable=SC2086
            gcc "${COMMON_FLAGS[@]}" "${DEBUG_FLAGS[@]}" $flags tests/run_tests.c -o "build/run_tests_$name"
            if ! "./build/run_tests_$name" 2> "build/run_tests_$name.log"; then
                cat "build/run_tests_$name.log" >&2
                exit 1
            fi
            echo "$name: $(tail -n 1 "build/run_tests_$name.log")"
            # shellcheck disable=SC2086
            gcc "${COMMON_FLAGS[@]}" "${DEBUG_FLAGS[@]}" $flags tests/corpus_test.c -o "build/corpus_test_$name"
            "./build/corpus_test_$name" build/corpus/WEB | tee "build/corpus_$name.log"
            echo "$name $(grep 'AST checksum' "build/corpus_$name.log" | cut -d' ' -f3)" >> build/checksums.txt
        done
        # Consumers use the default enum ABI: run the unit tests once more
        # without -fshort-enums.
        gcc -std=c99 -Wall -Wextra -pedantic "${DEBUG_FLAGS[@]}" tests/run_tests.c \
            -o build/run_tests_default_enums
        if ! ./build/run_tests_default_enums 2> build/run_tests_default_enums.log; then
            cat build/run_tests_default_enums.log >&2
            exit 1
        fi
        echo "default enums: $(tail -n 1 build/run_tests_default_enums.log)"
        gcc "${COMMON_FLAGS[@]}" "${DEBUG_FLAGS[@]}" -DUSFM_FUZZ_REPLAY tests/fuzz_ast.c -o build/fuzz_replay
        ./build/fuzz_replay tests/fuzz/seeds/*
        # Paths this machine can't run: make sure they at least compile.
        gcc "${COMMON_FLAGS[@]}" -O1 -mavx512bw -c tests/run_tests.c -o /dev/null && echo "avx512bw: compiles"
        if command -v aarch64-linux-gnu-gcc > /dev/null; then
            aarch64-linux-gnu-gcc "${COMMON_FLAGS[@]}" -O1 -c tests/run_tests.c -o /dev/null && echo "neon: compiles"
        fi
        if command -v zig > /dev/null; then
            zig cc -target wasm32-wasi -msimd128 -std=c99 -O1 -c tests/run_tests.c -o build/wasm_check.o && echo "wasm-simd128: compiles"
        fi
        check_checksums
        ;;
    test-neon)
        run_codegen
        unpack_corpus
        if ! command -v qemu-aarch64 > /dev/null; then
            echo "test-neon: skipped (install qemu-user)"
            exit 0
        fi
        if command -v aarch64-linux-gnu-gcc > /dev/null; then
            ARM_CC=(aarch64-linux-gnu-gcc -static)
        elif command -v zig > /dev/null; then
            ARM_CC=(zig cc -target aarch64-linux-musl)
        else
            echo "test-neon: skipped (install gcc-aarch64-linux-gnu or zig)"
            exit 0
        fi
        "${ARM_CC[@]}" "${COMMON_FLAGS[@]}" -O1 -g tests/run_tests.c -o build/run_tests_neon
        "${ARM_CC[@]}" "${COMMON_FLAGS[@]}" -O1 -g tests/corpus_test.c -o build/corpus_test_neon
        gcc "${COMMON_FLAGS[@]}" -O1 -DUSFM_SIMD_NONE tests/corpus_test.c -o build/corpus_test_ref
        if ! qemu-aarch64 ./build/run_tests_neon 2> build/run_tests_neon.log; then
            cat build/run_tests_neon.log >&2
            exit 1
        fi
        grep 'SIMD path' build/run_tests_neon.log
        echo "neon: $(tail -n 1 build/run_tests_neon.log)"
        rm -f build/checksums.txt
        ./build/corpus_test_ref build/corpus/WEB > build/corpus_ref.log
        echo "scalar $(grep 'AST checksum' build/corpus_ref.log | cut -d' ' -f3)" >> build/checksums.txt
        qemu-aarch64 ./build/corpus_test_neon build/corpus/WEB | tee build/corpus_neon.log
        echo "neon $(grep 'AST checksum' build/corpus_neon.log | cut -d' ' -f3)" >> build/checksums.txt
        check_checksums
        ;;
    test-wasm)
        run_codegen
        unpack_corpus
        if ! command -v zig > /dev/null || ! command -v node > /dev/null; then
            echo "test-wasm: skipped (needs zig and node)"
            exit 0
        fi
        WASM_FLAGS=(-target wasm32-wasi -msimd128 -std=c99 -O2 -Wl,-z,stack-size=8388608)
        zig cc "${WASM_FLAGS[@]}" tests/run_tests.c -o build/run_tests_wasm.wasm
        zig cc "${WASM_FLAGS[@]}" tests/corpus_test.c -o build/corpus_test_wasm.wasm
        gcc "${COMMON_FLAGS[@]}" -O1 -DUSFM_SIMD_NONE tests/corpus_test.c -o build/corpus_test_ref
        if ! node --no-warnings tests/wasi_run.mjs build/run_tests_wasm.wasm 2> build/run_tests_wasm.log; then
            cat build/run_tests_wasm.log >&2
            exit 1
        fi
        grep 'SIMD path' build/run_tests_wasm.log
        echo "wasm: $(tail -n 1 build/run_tests_wasm.log)"
        rm -f build/checksums.txt
        ./build/corpus_test_ref build/corpus/WEB > build/corpus_ref.log
        echo "scalar $(grep 'AST checksum' build/corpus_ref.log | cut -d' ' -f3)" >> build/checksums.txt
        node --no-warnings tests/wasi_run.mjs build/corpus_test_wasm.wasm "$PWD/build/corpus/WEB" | tee build/corpus_wasm.log
        echo "wasm $(grep 'AST checksum' build/corpus_wasm.log | cut -d' ' -f3)" >> build/checksums.txt
        check_checksums
        ;;
    fuzz)
        run_codegen
        mkdir -p build/fuzz/corpus
        clang -std=c99 -g -O1 -fsanitize=fuzzer,address,undefined tests/fuzz_ast.c -o build/fuzz_ast
        echo "Run: ./build/fuzz_ast -dict=tests/fuzz/usfm.dict -max_total_time=1800 build/fuzz/corpus tests/fuzz/seeds"
        ;;
    codegen)
        rm -f build/codegen.stamp
        run_codegen
        ;;
    *)
        echo "Usage: $0 [debug|release|test|test-neon|test-wasm|test-avx512|fuzz|bench|codegen]"
        exit 1
        ;;
esac

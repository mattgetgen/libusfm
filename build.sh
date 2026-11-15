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
        # The ABI must not depend on how a compiler sizes enums: no field,
        # parameter or return value in usfm.h may have an enum type (store
        # enum values in fixed-width integers instead).
        if grep -nE '(^|[(,;]|USFM_API)[[:space:]]*(const[[:space:]]+)?enum[[:space:]]+[A-Za-z_][A-Za-z0-9_]*[[:space:]]*\**[[:space:]]*[A-Za-z_][A-Za-z0-9_]*[[:space:]]*[;,)(=[]' usfm.h; then
            echo "usfm.h: enum type used for storage (see above)" >&2
            exit 1
        fi
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
        gcc "${COMMON_FLAGS[@]}" "${DEBUG_FLAGS[@]}" main.c -o build/usfm_cli
        tests/cli_test.sh build/usfm_cli build/corpus/WEB
        if command -v zig > /dev/null; then
            zig cc -target x86_64-windows-gnu -std=c99 -Wall -Wextra -pedantic -O1 \
                -c main.c -o build/usfm_windows.o && echo "usfm tool: windows compiles"
        fi
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

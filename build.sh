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

# Unpack every tests/corpus/*.tar.gz into build/corpus (again whenever
# its archive changes) and list the corpus names in CORPORA.
CORPORA=()
unpack_corpus() {
    local archive name
    mkdir -p build/corpus
    CORPORA=()
    for archive in tests/corpus/*.tar.gz; do
        name=$(basename "$archive" .tar.gz)
        if [[ ! -f "build/corpus/$name.stamp" || "$archive" -nt "build/corpus/$name.stamp" ]]; then
            rm -rf "build/corpus/$name"
            tar -xzf "$archive" -C build/corpus
            touch "build/corpus/$name.stamp"
        fi
        CORPORA+=("$name")
    done
}

# run_corpus PATH CORPUS COMMAND...: runs one corpus test, prints a one-line
# summary (the full log on failure) and records the AST checksum.
run_corpus() {
    local path="$1" corpus="$2" log="build/corpus_$1_$2.log" checksum
    shift 2
    if ! "$@" > "$log" 2>&1; then
        cat "$log" >&2
        echo "$path $corpus: corpus test FAILED" >&2
        exit 1
    fi
    checksum=$(grep '^AST checksum' "$log" | cut -d' ' -f3)
    echo "$path $corpus: $(grep '^Scanner differences' "$log"), $(grep '^Classifier differences' "$log"), $(grep '^Fast-path differences' "$log"), $(grep '^Diagnostics' "$log"), checksum $checksum, $(tail -n 1 "$log")"
    echo "$path $corpus $checksum" >> build/checksums.txt
}

# Every path must produce the same AST checksum on each corpus.
check_checksums() {
    local corpus distinct paths
    for corpus in "${CORPORA[@]}"; do
        distinct=$(awk -v c="$corpus" '$2 == c { print $3 }' build/checksums.txt | sort -u | wc -l)
        if [[ "$distinct" -ne 1 ]]; then
            echo "checksums DIFFER across paths for $corpus:" >&2
            cat build/checksums.txt >&2
            exit 1
        fi
    done
    paths=$(cut -d' ' -f1 build/checksums.txt | sort -u | wc -l)
    echo "checksums: identical across $paths paths for ${#CORPORA[@]} corpora"
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
        # Every native SIMD path: unit tests and every corpus.
        PATHS=("scalar:-DUSFM_SIMD_NONE" "sse2:" "ssse3:-mssse3")
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
            for corpus in "${CORPORA[@]}"; do
                run_corpus "$name" "$corpus" "./build/corpus_test_$name" "build/corpus/$corpus" tests/corpus/expected.txt
            done
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
        gcc "${COMMON_FLAGS[@]}" "${DEBUG_FLAGS[@]}" -mssse3 -DUSFM_FUZZ_REPLAY tests/fuzz_ast.c -o build/fuzz_replay_ssse3
        ./build/fuzz_replay_ssse3 tests/fuzz/seeds/*
        # Paths this machine can't run: make sure they at least compile.
        gcc "${COMMON_FLAGS[@]}" -O1 -mavx512bw -c tests/run_tests.c -o /dev/null && echo "avx512bw: compiles"
        if command -v aarch64-linux-gnu-gcc > /dev/null; then
            aarch64-linux-gnu-gcc "${COMMON_FLAGS[@]}" -O1 -c tests/run_tests.c -o /dev/null && echo "neon: compiles"
        fi
        if command -v zig > /dev/null; then
            zig cc -target wasm32-wasi -msimd128 -std=c99 -O1 -D_WASI_EMULATED_PROCESS_CLOCKS -c tests/run_tests.c -o build/wasm_check.o && echo "wasm-simd128: compiles"
        fi
        gcc "${COMMON_FLAGS[@]}" "${DEBUG_FLAGS[@]}" main.c -o build/usfm_cli
        tests/cli_test.sh build/usfm_cli build/corpus/WEB
        if command -v zig > /dev/null; then
            zig cc -target x86_64-windows-gnu -std=c99 -Wall -Wextra -pedantic -O1 \
                -c main.c -o build/usfm_windows.o && echo "usfm tool: windows compiles"
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
        grep -E 'SIMD path|UTF-8 validator' build/run_tests_neon.log
        echo "neon: $(tail -n 1 build/run_tests_neon.log)"
        rm -f build/checksums.txt
        for corpus in "${CORPORA[@]}"; do
            run_corpus scalar "$corpus" ./build/corpus_test_ref "build/corpus/$corpus" tests/corpus/expected.txt
            run_corpus neon "$corpus" qemu-aarch64 ./build/corpus_test_neon "build/corpus/$corpus" tests/corpus/expected.txt
        done
        check_checksums
        ;;
    test-wasm)
        run_codegen
        unpack_corpus
        if ! command -v zig > /dev/null || ! command -v node > /dev/null; then
            echo "test-wasm: skipped (needs zig and node)"
            exit 0
        fi
        # clock() needs WASI's emulated process clocks (tests time two lookahead checks).
        WASM_FLAGS=(-target wasm32-wasi -msimd128 -std=c99 -O2 -D_WASI_EMULATED_PROCESS_CLOCKS -Wl,-z,stack-size=8388608)
        zig cc "${WASM_FLAGS[@]}" tests/run_tests.c -lwasi-emulated-process-clocks -o build/run_tests_wasm.wasm
        zig cc "${WASM_FLAGS[@]}" tests/corpus_test.c -lwasi-emulated-process-clocks -o build/corpus_test_wasm.wasm
        gcc "${COMMON_FLAGS[@]}" -O1 -DUSFM_SIMD_NONE tests/corpus_test.c -o build/corpus_test_ref
        if ! node --no-warnings tests/wasi_run.mjs build/run_tests_wasm.wasm 2> build/run_tests_wasm.log; then
            cat build/run_tests_wasm.log >&2
            exit 1
        fi
        grep -E 'SIMD path|UTF-8 validator' build/run_tests_wasm.log
        echo "wasm: $(tail -n 1 build/run_tests_wasm.log)"
        rm -f build/checksums.txt
        for corpus in "${CORPORA[@]}"; do
            run_corpus scalar "$corpus" ./build/corpus_test_ref "build/corpus/$corpus" tests/corpus/expected.txt
            run_corpus wasm "$corpus" node --no-warnings tests/wasi_run.mjs build/corpus_test_wasm.wasm \
                "$PWD/build/corpus/$corpus" "$PWD/tests/corpus/expected.txt"
        done
        check_checksums
        ;;
    test-avx512)
        run_codegen
        unpack_corpus
        if [[ -z "${USFM_SDE:-}" || ! -x "${USFM_SDE}" ]]; then
            echo "test-avx512: skipped (download Intel SDE and set USFM_SDE to its sde64 executable)"
            exit 0
        fi
        gcc "${COMMON_FLAGS[@]}" -O1 -g -mavx512bw tests/run_tests.c -o build/run_tests_avx512
        gcc "${COMMON_FLAGS[@]}" -O1 -g -mavx512bw tests/corpus_test.c -o build/corpus_test_avx512
        gcc "${COMMON_FLAGS[@]}" -O1 -DUSFM_SIMD_NONE tests/corpus_test.c -o build/corpus_test_ref
        if ! "$USFM_SDE" -spr -- ./build/run_tests_avx512 2> build/run_tests_avx512.log; then
            cat build/run_tests_avx512.log >&2
            exit 1
        fi
        grep -E 'SIMD path|UTF-8 validator' build/run_tests_avx512.log
        echo "avx512bw: $(tail -n 1 build/run_tests_avx512.log)"
        rm -f build/checksums.txt
        for corpus in "${CORPORA[@]}"; do
            run_corpus scalar "$corpus" ./build/corpus_test_ref "build/corpus/$corpus" tests/corpus/expected.txt
            run_corpus avx512bw "$corpus" "$USFM_SDE" -spr -- ./build/corpus_test_avx512 "build/corpus/$corpus" tests/corpus/expected.txt
        done
        check_checksums
        ;;
    fuzz)
        run_codegen
        mkdir -p build/fuzz/corpus
        clang -std=c99 -g -O1 -mavx2 -fsanitize=fuzzer,address,undefined tests/fuzz_ast.c -o build/fuzz_ast
        echo "Run: ./build/fuzz_ast -dict=tests/fuzz/usfm.dict -max_total_time=1800 build/fuzz/corpus tests/fuzz/seeds"
        ;;
    bench)
        run_codegen
        unpack_corpus
        shift
        DIRS=("$@")
        if [[ ${#DIRS[@]} -eq 0 ]]; then
            for corpus in "${CORPORA[@]}"; do DIRS+=("build/corpus/$corpus"); done
        fi
        VARIANTS=("scalar:-DUSFM_SIMD_NONE" "sse2:-mno-avx2 -mno-avx512f" "avx2:-mno-avx512f")
        if grep -q ' avx512bw' /proc/cpuinfo; then VARIANTS+=("avx512bw:"); fi
        PIN=()
        if [[ -n "${USFM_BENCH_CPU:-}" ]]; then PIN=(taskset -c "$USFM_BENCH_CPU"); fi
        for entry in "${VARIANTS[@]}"; do
            name="${entry%%:*}"
            flags="${entry#*:}"
            # shellcheck disable=SC2086
            clang -std=c99 -O3 -march=native $flags tests/bench.c -o "build/bench_$name"
        done
        for d in "${DIRS[@]}"; do
            for entry in "${VARIANTS[@]}"; do
                "${PIN[@]}" "./build/bench_${entry%%:*}" -n "${USFM_BENCH_PASSES:-5}" "$d"
            done
        done
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

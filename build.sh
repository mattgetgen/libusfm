#!/usr/bin/env bash
# Modes: debug (default), release, test, fuzz, codegen.
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
        gcc "${COMMON_FLAGS[@]}" "${DEBUG_FLAGS[@]}" tests/run_tests.c -o build/run_tests
        g++ -std=c++11 -Wall -Wextra -fsyntax-only tests/cpp_check.cpp
        ./build/run_tests
        # The ABI must not depend on how a compiler sizes enums: no field,
        # parameter or return value in usfm.h may have an enum type (store
        # enum values in fixed-width integers instead).
        if grep -nE '(^|[(,;]|USFM_API)[[:space:]]*(const[[:space:]]+)?enum[[:space:]]+[A-Za-z_][A-Za-z0-9_]*[[:space:]]*\**[[:space:]]*[A-Za-z_][A-Za-z0-9_]*[[:space:]]*[;,)(=[]' usfm.h; then
            echo "usfm.h: enum type used for storage (see above)" >&2
            exit 1
        fi
        unpack_corpus
        gcc "${COMMON_FLAGS[@]}" "${DEBUG_FLAGS[@]}" tests/corpus_test.c -o build/corpus_test
        ./build/corpus_test build/corpus/WEB
        gcc "${COMMON_FLAGS[@]}" "${DEBUG_FLAGS[@]}" -DUSFM_FUZZ_REPLAY tests/fuzz_ast.c -o build/fuzz_replay
        ./build/fuzz_replay tests/fuzz/seeds/*
        gcc "${COMMON_FLAGS[@]}" "${DEBUG_FLAGS[@]}" main.c -o build/usfm_cli
        tests/cli_test.sh build/usfm_cli build/corpus/WEB
        if command -v zig > /dev/null; then
            zig cc -target x86_64-windows-gnu -std=c99 -Wall -Wextra -pedantic -O1 \
                -c main.c -o build/usfm_windows.o && echo "usfm tool: windows compiles"
        fi
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
        echo "Usage: $0 [debug|release|test|fuzz|codegen]"
        exit 1
        ;;
esac

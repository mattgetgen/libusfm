# libusfm 0.1.1 SIMD Scanner Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the scanner's text and attributes loops use a SIMD 64-byte block classifier, chosen at compile time, with identical results on every path.

**Architecture:**
- `usfm__classify_block` turns 64 input bytes into five 64-bit masks: `\`, `|`, `/`, `~` and CR/LF/TAB. One implementation exists per instruction set.
- The scanner caches the masks of its current block and finds the next stop byte with count-trailing-zeros. A new block is classified only when scanning crosses into it.
- v0.1's byte-loop scanner remains, both as the `USFM_SIMD_NONE` path and as the reference that every SIMD path is compared against, token by token.

**Tech Stack:**
- C99, plus compiler intrinsics: SSE2, AVX2, AVX-512BW, NEON, wasm simd128.
- gcc and clang, with ASan and UBSan.
- `aarch64-linux-gnu-gcc` and `qemu-aarch64` for NEON.
- `zig cc` and Node's WASI for wasm.
- Intel SDE for AVX-512.
- bash.

**Spec:** `docs/superpowers/specs/2026-11-15-simd-scanner-design.md`. Read it first.

---

## Conventions for every task

- Work on branch `feat/simd` in `~/repos/libusfm`, and run every command from the repo root.
- Commit messages end with a blank line and then `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- `usfm.h` rules:
  - `/* */` comments only. Codegen treats any line that starts with `//` as a marker.
  - Internal names start with `usfm__` or `USFM__`.
  - Power of 10: no recursion; every loop bounded by the input length or a constant, and say which in a comment where it isn't obvious; no allocation.
  - The header must stay valid C99 and C++.
- Never hard-code paths to corpora other than `build/corpus/WEB`: benchmark inputs come from arguments.
- "Run tests" means `./build.sh test`. After Task 3 it runs every native path and ends with `checksums: identical across N paths`.

## File structure

| File | Change |
| --- | --- |
| `usfm.h` | Version 0.1.1. A new section `/* ===== SIMD block classifier ===== */` before the Scanner section, containing path selection, bit helpers, `USFM_BlockMasks` and one classifier per path. The Scanner section gains a cached block, `usfm__text_end_blocks` / `usfm__attr_end_blocks`, the extracted byte loops `usfm__text_end_scalar` / `usfm__attr_end_scalar`, and `usfm__scan_next_scalar` (test builds only). Header docs for `USFM_SIMD_NONE`. |
| `tests/scan_compare.h` | New: runs both scanners in lockstep and reports the first difference. Used by the unit, corpus and fuzz tests. |
| `tests/test_simd.c` | New: classifier and scanner differential tests. |
| `tests/run_tests.c` | Defines `USFM_TEST_SCAN_REFERENCE` and runs `test_simd.c`. |
| `tests/corpus_test.c` | Two-scanner comparison over WEB, the SIMD path name, and an AST checksum. |
| `tests/fuzz_ast.c` | Two-scanner comparison on every input. |
| `tests/wasi_run.mjs` | New: runs a WASI `.wasm` under Node. |
| `tests/bench.c` | New: whole-corpus benchmark. Takes directories as arguments, reports wall time and perf counters. |
| `build.sh` | `test` runs scalar, SSE2 and AVX2 plus compile-only checks. New modes: `test-neon`, `test-wasm`, `test-avx512` and `bench`. `fuzz` builds AVX2. |
| `docs/release-notes/0.1.1.md` | New: release notes with the measured results. |

---

### Task 1: Path selection and the block classifier

**Files:**
- Modify: `usfm.h` (version macros; a new section before `/* ===== Scanner ===== */`)
- Create: `tests/test_simd.c`
- Modify: `tests/run_tests.c`

- [ ] **Step 1: Write the failing classifier test, `tests/test_simd.c`**

```c
/* SIMD tests: the block classifier against the scalar reference, and
 * (Task 2) the block scanner against the byte-loop scanner. */

static uint32_t simd_rng = 12345u;

static uint32_t simd_rand(void)
{
    simd_rng = simd_rng * 1103515245u + 12345u;
    return simd_rng >> 8;
}

static void test_simd_path_name(void)
{
    fprintf(stderr, "    SIMD path: %s\n", USFM__SIMD_PATH);
    CHECK(USFM__SIMD_PATH[0] != '\0');
}

#if USFM__USE_BLOCKS
static int simd_masks_equal(const USFM_BlockMasks *a, const USFM_BlockMasks *b)
{
    return a->backslash == b->backslash && a->pipe == b->pipe && a->slash == b->slash &&
           a->nbsp == b->nbsp && a->loose_ws == b->loose_ws;
}

static void test_classifier_matches_scalar(void)
{
    static const char special[] = "\\|/~\r\n\t";
    char block[64];
    USFM_BlockMasks a, b;
    uint32_t mismatches = 0, n, k;
    for (n = 0; n < 256; n++) {             /* every byte value, whole block */
        memset(block, (int)n, sizeof block);
        usfm__classify_block(block, &a);
        usfm__classify_block_scalar(block, &b);
        if (!simd_masks_equal(&a, &b)) mismatches++;
    }
    for (n = 0; n < 20000; n++) {           /* random, a quarter special bytes */
        for (k = 0; k < 64; k++) {
            uint32_t r = simd_rand();
            block[k] = (r & 3u) == 0 ? special[(r >> 2) % 7u] : (char)(r >> 4);
        }
        usfm__classify_block(block, &a);
        usfm__classify_block_scalar(block, &b);
        if (!simd_masks_equal(&a, &b)) mismatches++;
    }
    CHECK_EQ(mismatches, 0);
}

static void test_bit_helpers(void)
{
    CHECK_EQ(usfm__ctz64(1u), 0);
    CHECK_EQ(usfm__ctz64((uint64_t)1 << 63), 63);
    CHECK_EQ(usfm__ctz64(0x50u), 4);
    CHECK_EQ(usfm__popcount64(0), 0);
    CHECK_EQ(usfm__popcount64(~(uint64_t)0), 64);
    CHECK_EQ(usfm__bit_range(0, 64), ~(uint64_t)0);
    CHECK_EQ(usfm__bit_range(3, 5), 0x18u);
    CHECK_EQ(usfm__bit_range(64, 64), 0);
    CHECK_EQ(usfm__bit_range(0, 0), 0);
}
#endif

static void run_simd_tests(void)
{
    RUN(test_simd_path_name);
#if USFM__USE_BLOCKS
    RUN(test_bit_helpers);
    RUN(test_classifier_matches_scalar);
#endif
}
```

In `tests/run_tests.c`, add `#define USFM_TEST_SCAN_REFERENCE` on the line before `#define USFM_IMPLEMENTATION`. Add `#include "test_simd.c"` after `test_parse_alloc.c`, and in `main`, before the final summary:

```c
    fprintf(stderr, "simd\n");
    run_simd_tests();
```

- [ ] **Step 2: Run to verify it fails**

Run: `mkdir -p build && gcc -std=c99 -Wall -Wextra -pedantic -fshort-enums -O0 -g -fsanitize=address,undefined tests/run_tests.c -o build/run_tests`
Expected: compile errors because `USFM__SIMD_PATH` and `USFM__USE_BLOCKS` are undeclared.

- [ ] **Step 3: Bump the version**

In `usfm.h`, change `#define USFM_VERSION_PATCH 0` to `#define USFM_VERSION_PATCH 1` and `#define USFM_VERSION "0.1.0-dev"` to `#define USFM_VERSION "0.1.1-dev"`.

- [ ] **Step 4: Add the classifier section**

Insert immediately before the line `/* ===== Scanner ===== */`:

```c
/* ===== SIMD block classifier ===== */

/* The path is chosen at compile time from the compiler's target macros;
 * there is no runtime CPU detection. USFM__USE_BLOCKS is 1 when a SIMD
 * classifier exists; otherwise the scanner keeps its byte loops. */
#if defined(USFM_SIMD_NONE)
#  define USFM__SIMD_PATH "scalar (USFM_SIMD_NONE)"
#  define USFM__USE_BLOCKS 0
#elif defined(__AVX512BW__)
#  include <immintrin.h>
#  define USFM__SIMD_PATH "avx512bw"
#  define USFM__SIMD_AVX512 1
#  define USFM__USE_BLOCKS 1
#elif defined(__AVX2__)
#  include <immintrin.h>
#  define USFM__SIMD_PATH "avx2"
#  define USFM__SIMD_AVX2 1
#  define USFM__USE_BLOCKS 1
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#  include <emmintrin.h>
#  define USFM__SIMD_PATH "sse2"
#  define USFM__SIMD_SSE2 1
#  define USFM__USE_BLOCKS 1
#elif defined(__aarch64__) && defined(__ARM_NEON)
#  include <arm_neon.h>
#  define USFM__SIMD_PATH "neon"
#  define USFM__SIMD_NEON 1
#  define USFM__USE_BLOCKS 1
#elif defined(__wasm_simd128__)
#  include <wasm_simd128.h>
#  define USFM__SIMD_PATH "wasm-simd128"
#  define USFM__SIMD_WASM 1
#  define USFM__USE_BLOCKS 1
#else
#  define USFM__SIMD_PATH "scalar (no SIMD for this target)"
#  define USFM__USE_BLOCKS 0
#endif

/* Bit i of each mask describes byte i of a 64-byte block. */
typedef struct USFM_BlockMasks {
    uint64_t backslash;                 /* '\\' */
    uint64_t pipe;                      /* '|' */
    uint64_t slash;                     /* '/' */
    uint64_t nbsp;                      /* '~' (flag only) */
    uint64_t loose_ws;                  /* CR, LF, TAB (flag only) */
} USFM_BlockMasks;

#if defined(USFM_TEST_SCAN_REFERENCE)
/* Reference classifier: every SIMD classifier is tested against it. */
static void usfm__classify_block_scalar(const char *p, USFM_BlockMasks *m)
{
    uint32_t i;
    memset(m, 0, sizeof *m);
    for (i = 0; i < 64u; i++) {
        uint64_t bit = (uint64_t)1 << i;
        switch (p[i]) {
        case '\\': m->backslash |= bit; break;
        case '|': m->pipe |= bit; break;
        case '/': m->slash |= bit; break;
        case '~': m->nbsp |= bit; break;
        case '\r': case '\n': case '\t': m->loose_ws |= bit; break;
        default: break;
        }
    }
}
#endif

#if USFM__USE_BLOCKS
#if defined(__GNUC__) || defined(__clang__)
static uint32_t usfm__popcount64(uint64_t x) { return (uint32_t)__builtin_popcountll(x); }
static uint32_t usfm__ctz64(uint64_t x) { return (uint32_t)__builtin_ctzll(x); }
#else
static uint32_t usfm__popcount64(uint64_t x)
{
    x = x - ((x >> 1) & 0x5555555555555555ull);
    x = (x & 0x3333333333333333ull) + ((x >> 2) & 0x3333333333333333ull);
    x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0Full;
    return (uint32_t)((x * 0x0101010101010101ull) >> 56);
}
/* x must not be 0. */
static uint32_t usfm__ctz64(uint64_t x) { return usfm__popcount64((x & (0 - x)) - 1u); }
#endif

/* Bits [from, to) of a block, 0 <= from <= to <= 64. */
static uint64_t usfm__bit_range(uint32_t from, uint32_t to)
{
    uint64_t hi = to >= 64u ? ~(uint64_t)0 : (((uint64_t)1 << to) - 1u);
    uint64_t lo = from >= 64u ? ~(uint64_t)0 : (((uint64_t)1 << from) - 1u);
    return hi & ~lo;
}

#if defined(USFM__SIMD_AVX512)
static void usfm__classify_block(const char *p, USFM_BlockMasks *m)
{
    __m512i x = _mm512_loadu_si512((const void *)p);
    m->backslash = (uint64_t)_mm512_cmpeq_epi8_mask(x, _mm512_set1_epi8('\\'));
    m->pipe = (uint64_t)_mm512_cmpeq_epi8_mask(x, _mm512_set1_epi8('|'));
    m->slash = (uint64_t)_mm512_cmpeq_epi8_mask(x, _mm512_set1_epi8('/'));
    m->nbsp = (uint64_t)_mm512_cmpeq_epi8_mask(x, _mm512_set1_epi8('~'));
    m->loose_ws = (uint64_t)(_mm512_cmpeq_epi8_mask(x, _mm512_set1_epi8('\r')) |
                             _mm512_cmpeq_epi8_mask(x, _mm512_set1_epi8('\n')) |
                             _mm512_cmpeq_epi8_mask(x, _mm512_set1_epi8('\t')));
}
#elif defined(USFM__SIMD_AVX2)
static void usfm__classify_block(const char *p, USFM_BlockMasks *m)
{
    const __m256i bs = _mm256_set1_epi8('\\'), pi = _mm256_set1_epi8('|');
    const __m256i sl = _mm256_set1_epi8('/'), tl = _mm256_set1_epi8('~');
    const __m256i cr = _mm256_set1_epi8('\r'), lf = _mm256_set1_epi8('\n');
    const __m256i tab = _mm256_set1_epi8('\t');
    uint32_t k;
    memset(m, 0, sizeof *m);
    for (k = 0; k < 2u; k++) {
        __m256i x = _mm256_loadu_si256((const __m256i *)(const void *)(p + 32u * k));
        uint32_t shift = 32u * k;
        __m256i ws = _mm256_or_si256(_mm256_or_si256(_mm256_cmpeq_epi8(x, cr),
                                                     _mm256_cmpeq_epi8(x, lf)),
                                     _mm256_cmpeq_epi8(x, tab));
        m->backslash |= (uint64_t)(uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(x, bs)) << shift;
        m->pipe |= (uint64_t)(uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(x, pi)) << shift;
        m->slash |= (uint64_t)(uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(x, sl)) << shift;
        m->nbsp |= (uint64_t)(uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(x, tl)) << shift;
        m->loose_ws |= (uint64_t)(uint32_t)_mm256_movemask_epi8(ws) << shift;
    }
}
#elif defined(USFM__SIMD_SSE2)
static void usfm__classify_block(const char *p, USFM_BlockMasks *m)
{
    const __m128i bs = _mm_set1_epi8('\\'), pi = _mm_set1_epi8('|');
    const __m128i sl = _mm_set1_epi8('/'), tl = _mm_set1_epi8('~');
    const __m128i cr = _mm_set1_epi8('\r'), lf = _mm_set1_epi8('\n');
    const __m128i tab = _mm_set1_epi8('\t');
    uint32_t k;
    memset(m, 0, sizeof *m);
    for (k = 0; k < 4u; k++) {
        __m128i x = _mm_loadu_si128((const __m128i *)(const void *)(p + 16u * k));
        uint32_t shift = 16u * k;
        __m128i ws = _mm_or_si128(_mm_or_si128(_mm_cmpeq_epi8(x, cr), _mm_cmpeq_epi8(x, lf)),
                                  _mm_cmpeq_epi8(x, tab));
        m->backslash |= (uint64_t)(uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(x, bs)) << shift;
        m->pipe |= (uint64_t)(uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(x, pi)) << shift;
        m->slash |= (uint64_t)(uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(x, sl)) << shift;
        m->nbsp |= (uint64_t)(uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(x, tl)) << shift;
        m->loose_ws |= (uint64_t)(uint32_t)_mm_movemask_epi8(ws) << shift;
    }
}
#elif defined(USFM__SIMD_NEON)
/* NEON has no movemask: weight each lane by its bit and add the halves. */
static uint64_t usfm__neon_bits(uint8x16_t eq)
{
    static const uint8_t weights[16] = { 1, 2, 4, 8, 16, 32, 64, 128,
                                         1, 2, 4, 8, 16, 32, 64, 128 };
    uint8x16_t b = vandq_u8(eq, vld1q_u8(weights));
    return (uint64_t)vaddv_u8(vget_low_u8(b)) | ((uint64_t)vaddv_u8(vget_high_u8(b)) << 8);
}

static void usfm__classify_block(const char *p, USFM_BlockMasks *m)
{
    uint32_t k;
    memset(m, 0, sizeof *m);
    for (k = 0; k < 4u; k++) {
        uint8x16_t x = vld1q_u8((const uint8_t *)p + 16u * k);
        uint32_t shift = 16u * k;
        uint8x16_t ws = vorrq_u8(vorrq_u8(vceqq_u8(x, vdupq_n_u8('\r')), vceqq_u8(x, vdupq_n_u8('\n'))),
                                 vceqq_u8(x, vdupq_n_u8('\t')));
        m->backslash |= usfm__neon_bits(vceqq_u8(x, vdupq_n_u8('\\'))) << shift;
        m->pipe |= usfm__neon_bits(vceqq_u8(x, vdupq_n_u8('|'))) << shift;
        m->slash |= usfm__neon_bits(vceqq_u8(x, vdupq_n_u8('/'))) << shift;
        m->nbsp |= usfm__neon_bits(vceqq_u8(x, vdupq_n_u8('~'))) << shift;
        m->loose_ws |= usfm__neon_bits(ws) << shift;
    }
}
#elif defined(USFM__SIMD_WASM)
static void usfm__classify_block(const char *p, USFM_BlockMasks *m)
{
    uint32_t k;
    memset(m, 0, sizeof *m);
    for (k = 0; k < 4u; k++) {
        v128_t x = wasm_v128_load(p + 16u * k);
        uint32_t shift = 16u * k;
        v128_t ws = wasm_v128_or(wasm_v128_or(wasm_i8x16_eq(x, wasm_i8x16_splat('\r')),
                                              wasm_i8x16_eq(x, wasm_i8x16_splat('\n'))),
                                 wasm_i8x16_eq(x, wasm_i8x16_splat('\t')));
        m->backslash |= (uint64_t)wasm_i8x16_bitmask(wasm_i8x16_eq(x, wasm_i8x16_splat('\\'))) << shift;
        m->pipe |= (uint64_t)wasm_i8x16_bitmask(wasm_i8x16_eq(x, wasm_i8x16_splat('|'))) << shift;
        m->slash |= (uint64_t)wasm_i8x16_bitmask(wasm_i8x16_eq(x, wasm_i8x16_splat('/'))) << shift;
        m->nbsp |= (uint64_t)wasm_i8x16_bitmask(wasm_i8x16_eq(x, wasm_i8x16_splat('~'))) << shift;
        m->loose_ws |= (uint64_t)wasm_i8x16_bitmask(ws) << shift;
    }
}
#endif
#endif /* USFM__USE_BLOCKS */
```

- [ ] **Step 5: Run the tests on every native path**

```bash
for f in "-DUSFM_SIMD_NONE" "" "-mavx2"; do
  gcc -std=c99 -Wall -Wextra -pedantic -fshort-enums -O0 -g -fsanitize=address,undefined $f tests/run_tests.c -o build/run_tests_x && ./build/run_tests_x 2>&1 | grep -E 'SIMD path|^ok|FAILED'
done
gcc -std=c99 -Wall -Wextra -pedantic -mavx512bw -O1 -c tests/run_tests.c -o /dev/null && echo avx512-compiles
aarch64-linux-gnu-gcc -std=c99 -Wall -Wextra -pedantic -O1 -c tests/run_tests.c -o /dev/null && echo neon-compiles
zig cc -target wasm32-wasi -msimd128 -std=c99 -Wall -Wextra -O1 -c tests/run_tests.c -o /dev/null && echo wasm-compiles
```

Expected:
- three runs, printing `SIMD path: scalar (USFM_SIMD_NONE)`, `sse2` and `avx2`, each with `ok: N checks`;
- `avx512-compiles`, `neon-compiles` and `wasm-compiles`.

Then run `./build.sh test` (still the old single-path mode); it should pass.

- [ ] **Step 6: Commit**

```bash
git add usfm.h tests/test_simd.c tests/run_tests.c
git commit -m "feat(simd): compile-time path selection and 64-byte block classifiers

SSE2, AVX2, AVX-512BW, NEON and wasm simd128 classifiers, each tested
against a scalar reference. Version 0.1.1-dev.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: The block scanner and the two-scanner comparison

**Files:**
- Modify: `usfm.h` (Scanner section)
- Create: `tests/scan_compare.h`
- Modify: `tests/test_simd.c`, `tests/run_tests.c`

- [ ] **Step 1: Write `tests/scan_compare.h`**

```c
#ifndef USFM_SCAN_COMPARE_H
#define USFM_SCAN_COMPARE_H
/* Runs the block scanner (usfm__scan_next) and the byte-loop reference
 * (usfm__scan_next_scalar) in lockstep over the same input and returns
 * 0 if every token and scanner state agree, or 1 at the first
 * difference (described on stderr if `report` is set).
 *
 * pipe_mode: 0 = '|' never starts attributes, 1 = always, any other
 * value seeds a generator that picks the mode per token (the same for
 * both scanners), the way the parser switches it.
 * Needs USFM_TEST_SCAN_REFERENCE defined before including usfm.h. */
#include <stdio.h>

static int scan_compare(const char *input, uint32_t length, uint32_t pipe_mode, int report)
{
    USFM_Scanner a, b;
    USFM_ScanToken ta, tb;
    uint32_t seed = pipe_mode, index = 0;
    usfm__scan_init(&a, input, length);
    usfm__scan_init(&b, input, length);
    /* Bounded: each successful call consumes at least one byte. */
    for (;;) {
        int ra, rb;
        uint8_t pipe;
        if (pipe_mode <= 1u) {
            pipe = (uint8_t)pipe_mode;
        } else {
            seed = seed * 1103515245u + 12345u;
            pipe = (uint8_t)(seed >> 31);
        }
        a.pipe_is_attr = pipe;
        b.pipe_is_attr = pipe;
        ra = usfm__scan_next(&a, &ta);
        rb = usfm__scan_next_scalar(&b, &tb);
        if (ra != rb || a.position != b.position || a.expect_arg != b.expect_arg ||
            (ra && (ta.tok.span.offset != tb.tok.span.offset ||
                    ta.tok.span.length != tb.tok.span.length ||
                    ta.tok.kind != tb.tok.kind || ta.tok.marker != tb.tok.marker ||
                    ta.tok.level_flags != tb.tok.level_flags || ta.row != tb.row ||
                    ta.name_offset != tb.name_offset || ta.name_length != tb.name_length))) {
            if (report) {
                fprintf(stderr,
                        "scanner difference at token %u (pipe %u): blocks kind %u span %u+%u "
                        "flags 0x%02x pos %u | scalar kind %u span %u+%u flags 0x%02x pos %u\n",
                        index, (unsigned)pipe, ra ? ta.tok.kind : 0u, ra ? ta.tok.span.offset : 0u,
                        ra ? ta.tok.span.length : 0u, ra ? ta.tok.level_flags : 0u, a.position,
                        rb ? tb.tok.kind : 0u, rb ? tb.tok.span.offset : 0u,
                        rb ? tb.tok.span.length : 0u, rb ? tb.tok.level_flags : 0u, b.position);
            }
            return 1;
        }
        if (!ra) return 0;
        index++;
    }
}

#endif
```

- [ ] **Step 2: Add the failing scanner tests to `tests/test_simd.c`**

Insert before `static void run_simd_tests(void)`:

```c
static void test_scanner_block_boundaries(void)
{
    static const char *const snippets[] = {
        "\\\\", "\\/", "\\|", "\\~", "\\\"", "\\=", "//", "/", "|", "~", "\r\n", "\r", "\n",
        "\t", "\\w ", "\\w*", "\\*", "\\+nd ", "\\v 1 ", "|lemma=\"a\\\"b\"\\w*", "\\", "~~//\\\\|"
    };
    char buf[256];
    uint32_t bad = 0, si, off, pipe;
    for (si = 0; si < sizeof snippets / sizeof snippets[0]; si++) {
        uint32_t sl = (uint32_t)strlen(snippets[si]);
        for (off = 0; off < 140u; off++) {
            uint32_t len = off + sl + 40u, k;
            for (k = 0; k < len; k++) buf[k] = (char)('a' + k % 23u);
            memcpy(buf + off, snippets[si], sl);
            for (pipe = 0; pipe < 3u; pipe++) {
                bad += (uint32_t)scan_compare(buf, off + sl, pipe == 2u ? 77u + off : pipe, bad == 0);
                bad += (uint32_t)scan_compare(buf, len, pipe == 2u ? 91u + off : pipe, bad == 0);
            }
        }
    }
    CHECK_EQ(bad, 0);
}

static void test_scanner_every_length(void)
{
    static const char unit[] =
        "\\id GEN x\n\\c 1\n\\p \\v 1 a\\/b~c\td \\w In|strong=\"H1\\=x\"\\w* e//f "
        "\\f + \\ft n\\f* \\qt-s |who=\"P\"\\*g\r\nh\\\\i|j \\zz k\\zz* \\+nd l\\+nd*\n";
    char rich[512];
    uint32_t bad = 0, len, pipe, used = 0;
    while (used + sizeof unit - 1u < sizeof rich) {     /* bounded by sizeof rich */
        memcpy(rich + used, unit, sizeof unit - 1u);
        used += (uint32_t)(sizeof unit - 1u);
    }
    for (len = 0; len <= used; len++) {
        for (pipe = 0; pipe < 3u; pipe++) {
            bad += (uint32_t)scan_compare(rich, len, pipe == 2u ? 5u + len : pipe, bad == 0);
        }
    }
    CHECK_EQ(bad, 0);
}

static void test_scanner_random(void)
{
    static const char alphabet[] = "\\\\\\|/~\r\n\t \"=*+-abcwvpq1";
    char buf[320];
    uint32_t bad = 0, n;
    for (n = 0; n < 20000u; n++) {
        uint32_t len = simd_rand() % 300u, k;
        for (k = 0; k < len; k++) buf[k] = alphabet[simd_rand() % (sizeof alphabet - 1u)];
        bad += (uint32_t)scan_compare(buf, len, 2u + n, bad == 0);
    }
    CHECK_EQ(bad, 0);
}
```

Add these to `run_simd_tests` after `RUN(test_simd_path_name);`, outside the `#if`, so they also run in the scalar build:

```c
    RUN(test_scanner_block_boundaries);
    RUN(test_scanner_every_length);
    RUN(test_scanner_random);
```

In `tests/run_tests.c`, add `#include "scan_compare.h"` after `#include "helpers.h"`.

- [ ] **Step 3: Run to verify it fails**

Run: `gcc -std=c99 -Wall -Wextra -pedantic -fshort-enums -O0 -g -fsanitize=address,undefined tests/run_tests.c -o build/run_tests`
Expected: errors because `usfm__scan_next_scalar` is undeclared.

- [ ] **Step 4: Restructure the scanner**

In the Scanner section of `usfm.h`:

**4a.** Give `USFM_Scanner` a cached block. Add two fields after `reserved`, and the constant after the struct:

```c
    uint32_t    block_base;             /* offset of the cached block, or
                                           USFM__NO_BLOCK */
    USFM_BlockMasks block;              /* masks of the cached block */
} USFM_Scanner;

#define USFM__NO_BLOCK 0xFFFFFFFFu
```

In `usfm__scan_init`, after `s->length = length;`, add `s->block_base = USFM__NO_BLOCK;`.

**4b.** Extract the two byte loops into functions. Add them before `usfm__scan_next`. The bodies are v0.1's loops, unchanged:

```c
/* End of a text run starting at `start` (byte loop); sets *flags. */
static uint32_t usfm__text_end_scalar(const USFM_Scanner *s, uint32_t start, uint8_t *flags)
{
    const char *in = s->input;
    uint32_t len = s->length, i = start;
    uint8_t f = 0;
    while (i < len) {
        uint8_t b = (uint8_t)in[i];
        if (!(USFM_ByteClass[b] & USFM_BC_STOP)) {
            i++;
            continue;
        }
        if (b == '\\') {
            /* \" is accepted as an escape in text too, as in attributes:
             * deliberate and harmless, since \" can't start a marker. */
            if (!usfm__is_escape(in, len, i)) break;
            f |= USFM_TF_HAS_ESCAPE;
            i += 2;
            continue;
        }
        if (b == '|') {
            if (s->pipe_is_attr) break;
            i++;
            continue;
        }
        if (b == '/') {
            if (i + 1 < len && in[i + 1] == '/') break;
            i++;
            continue;
        }
        if (b == '~') f |= USFM_TF_HAS_NBSP;
        else f |= USFM_TF_HAS_LOOSE_WS;          /* CR, LF, TAB */
        i++;
    }
    *flags = f;
    return i > len ? len : i;
}

/* End of an attributes run whose first byte after the '|' is at `i`
 * (byte loop): the next backslash that does not start an escape. */
static uint32_t usfm__attr_end_scalar(const USFM_Scanner *s, uint32_t i)
{
    const char *in = s->input;
    uint32_t len = s->length;
    while (i < len) {
        if (in[i] == '\\') {
            /* \= is an escape here only (field guide section 1). */
            if (!usfm__is_escape(in, len, i) && !(i + 1 < len && in[i + 1] == '=')) break;
            i += 2;
            continue;
        }
        i++;
    }
    return i > len ? len : i;
}
```

**4c.** Add the block versions after them:

```c
#if USFM__USE_BLOCKS
/* Makes the 64-byte block at `base` (a multiple of 64, below the input
 * length) the cached block. A last, partial block is classified from a
 * zero-padded copy, so nothing past the input is ever read; zero bytes
 * are not special, so the padding sets no bits. */
static void usfm__load_block(USFM_Scanner *s, uint32_t base)
{
    if (s->block_base == base) return;
    if (s->length - base >= 64u) {
        usfm__classify_block(s->input + base, &s->block);
    } else {
        char tail[64];
        memset(tail, 0, sizeof tail);
        memcpy(tail, s->input + base, s->length - base);
        usfm__classify_block(tail, &s->block);
    }
    s->block_base = base;
}

/* Adds the flag-only bits of the cached block in [from, to) (input
 * offsets inside the cached block) to the running results. */
static void usfm__flag_span(const USFM_Scanner *s, uint32_t from, uint32_t to,
                            uint64_t *nbsp, uint64_t *loose)
{
    uint64_t r;
    if (to <= from) return;
    r = usfm__bit_range(from - s->block_base, to - s->block_base);
    *nbsp |= s->block.nbsp & r;
    *loose |= s->block.loose_ws & r;
}

/* End of a text run starting at `start`, from cached block masks. Gives
 * the same end and flags as usfm__text_end_scalar: escaped bytes are
 * skipped (so an escaped '~' sets no NBSP flag), and only the stop
 * candidates ('\', '/', and '|' where attributes are allowed) are
 * inspected one by one. Bounded: each pass ends the run or advances i. */
static uint32_t usfm__text_end_blocks(USFM_Scanner *s, uint32_t start, uint8_t *flags)
{
    const char *in = s->input;
    uint32_t len = s->length, i = start;
    uint64_t nbsp = 0, loose = 0;
    uint8_t f = 0;
    while (i < len) {
        uint32_t base = i & ~63u, block_end = base + 64u, j;
        uint64_t stops;
        usfm__load_block(s, base);
        stops = s->block.backslash | s->block.slash | (s->pipe_is_attr ? s->block.pipe : 0u);
        stops &= usfm__bit_range(i - base, 64u);
        if (stops == 0) {                   /* no stop in the rest of this block */
            usfm__flag_span(s, i, block_end < len ? block_end : len, &nbsp, &loose);
            i = block_end;
            continue;
        }
        j = base + usfm__ctz64(stops);
        usfm__flag_span(s, i, j, &nbsp, &loose);
        if (in[j] == '\\') {
            if (!usfm__is_escape(in, len, j)) {
                i = j;
                break;
            }
            f |= USFM_TF_HAS_ESCAPE;
            i = j + 2u;                     /* skip the escaped byte too */
        } else if (in[j] == '|') {
            i = j;
            break;
        } else {                            /* '/' */
            if (j + 1u < len && in[j + 1u] == '/') {
                i = j;
                break;
            }
            i = j + 1u;
        }
    }
    if (nbsp != 0) f |= USFM_TF_HAS_NBSP;
    if (loose != 0) f |= USFM_TF_HAS_LOOSE_WS;
    *flags = f;
    return i > len ? len : i;
}

/* End of an attributes run from cached block masks: the next backslash
 * that starts neither an escape nor "\=". Bounded like the text run. */
static uint32_t usfm__attr_end_blocks(USFM_Scanner *s, uint32_t i)
{
    const char *in = s->input;
    uint32_t len = s->length;
    while (i < len) {
        uint32_t base = i & ~63u, j;
        uint64_t stops;
        usfm__load_block(s, base);
        stops = s->block.backslash & usfm__bit_range(i - base, 64u);
        if (stops == 0) {
            i = base + 64u;
            continue;
        }
        j = base + usfm__ctz64(stops);
        if (!usfm__is_escape(in, len, j) && !(j + 1u < len && in[j + 1u] == '=')) return j;
        i = j + 2u;
    }
    return len;
}
#endif /* USFM__USE_BLOCKS */
```

**4d.** Replace the whole `usfm__scan_next` function with a shared body and two entry points:

```c
/* Returns 1 and fills *t, or 0 at end of input. Every call that
 * returns 1 consumes at least one byte, so a caller's loop is bounded
 * by the input length. use_blocks selects the cached-block loops (when
 * compiled in) or the byte loops; results are identical either way. */
static int usfm__scan_next_with(USFM_Scanner *s, USFM_ScanToken *t, int use_blocks)
{
    const char *in = s->input;
    uint32_t len = s->length;
    uint32_t start, i;
    uint8_t flags = 0;
#if !USFM__USE_BLOCKS
    (void)use_blocks;
#endif
    if (s->expect_arg != USFM_ARG_NONE) {
        uint8_t mode = s->expect_arg;
        s->expect_arg = USFM_ARG_NONE;
        if (usfm__scan_argument(s, t, mode)) return 1;
    }
    start = s->position;
    if (start >= len) return 0;
    if (in[start] == '\\' && !usfm__is_escape(in, len, start)) return usfm__scan_marker(s, t);
    if (in[start] == '|' && s->pipe_is_attr) {
        uint32_t end;
#if USFM__USE_BLOCKS
        i = use_blocks ? usfm__attr_end_blocks(s, start + 1u) : usfm__attr_end_scalar(s, start + 1u);
#else
        i = usfm__attr_end_scalar(s, start + 1u);
#endif
        end = i;
        while (end > start + 1 &&
               (USFM_ByteClass[(uint8_t)in[end - 1]] & (USFM_BC_SPACE | USFM_BC_NEWLINE))) end--;
        usfm__tok(t, USFM_TOK_ATTRIBUTES, start, end);
        s->position = i;
        return 1;
    }
    if (in[start] == '/' && start + 1 < len && in[start + 1] == '/') {
        usfm__tok(t, USFM_TOK_OPTBREAK, start, start + 2);
        s->position = start + 2;
        return 1;
    }
#if USFM__USE_BLOCKS
    i = use_blocks ? usfm__text_end_blocks(s, start, &flags) : usfm__text_end_scalar(s, start, &flags);
#else
    i = usfm__text_end_scalar(s, start, &flags);
#endif
    USFM_ASSERT(i > start);
    usfm__tok(t, USFM_TOK_TEXT, start, i);
    t->tok.level_flags = flags;
    s->position = i;
    return 1;
}

static int usfm__scan_next(USFM_Scanner *s, USFM_ScanToken *t)
{
    return usfm__scan_next_with(s, t, USFM__USE_BLOCKS);
}

#if defined(USFM_TEST_SCAN_REFERENCE)
/* v0.1's byte-loop scanner: the reference the block scanner is tested
 * against. Test builds only. */
static int usfm__scan_next_scalar(USFM_Scanner *s, USFM_ScanToken *t)
{
    return usfm__scan_next_with(s, t, 0);
}
#endif
```

- [ ] **Step 5: Run the tests on every native path**

```bash
for f in "-DUSFM_SIMD_NONE" "" "-mavx2"; do
  gcc -std=c99 -Wall -Wextra -pedantic -fshort-enums -O0 -g -fsanitize=address,undefined $f tests/run_tests.c -o build/run_tests_x && ./build/run_tests_x 2>&1 | grep -E 'SIMD path|scanner difference|^ok|FAILED'
done
./build.sh test 2>&1 | tail -6
./build.sh release && ./usfm
```

Expected:
- three `ok: N checks` lines and no `scanner difference`;
- `./build.sh test` still passes, with unchanged corpus counts and `Diagnostics: 0`;
- the release build compiles with no warnings under `-Werror` and runs the benchmark.

A `scanner difference` line gives the token index and both scanners' views. Fix `usfm__text_end_blocks` or `usfm__attr_end_blocks`, never the reference.

- [ ] **Step 6: Commit**

```bash
git add usfm.h tests/scan_compare.h tests/test_simd.c tests/run_tests.c
git commit -m "feat(simd): scanner text and attribute runs on cached block masks

The v0.1 byte loops stay as the USFM_SIMD_NONE path and as the test
reference; both scanners are compared token by token.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: All native paths in `build.sh test`, corpus differential and AST checksum

**Files:**
- Modify: `tests/corpus_test.c`
- Modify: `build.sh`

- [ ] **Step 1: Add the comparison and checksum to `tests/corpus_test.c`**

Add `#define USFM_TEST_SCAN_REFERENCE` on the line before `#define USFM_IMPLEMENTATION`, and `#include "scan_compare.h"` after `#include "check.h"`. Then add before `int main`:

```c
/* FNV-1a over every field of the trees, tables and diagnostics (field
 * by field, so struct padding never enters the hash). */
static uint64_t fnv(uint64_t h, uint64_t v, uint32_t bytes)
{
    uint32_t k;
    for (k = 0; k < bytes; k++) {
        h ^= (v >> (8u * k)) & 0xFFu;
        h *= 0x100000001b3ull;
    }
    return h;
}

static uint64_t checksum_ast(uint64_t h, const USFM_Ast *ast)
{
    uint32_t count, i;
    const USFM_Node *nodes = USFM_Ast_GetNodes(ast, &count);
    const USFM_ChapterEntry *chapters;
    const USFM_VerseEntry *verses;
    const USFM_Diagnostic *diags;
    for (i = 0; i < count; i++) {
        h = fnv(h, nodes[i].parent, 4);
        h = fnv(h, nodes[i].end, 4);
        h = fnv(h, nodes[i].span.offset, 4);
        h = fnv(h, nodes[i].span.length, 4);
        h = fnv(h, nodes[i].marker, 2);
        h = fnv(h, nodes[i].kind, 1);
        h = fnv(h, nodes[i].level_flags, 1);
    }
    chapters = USFM_Ast_GetChapters(ast, &count);
    for (i = 0; i < count; i++) {
        h = fnv(h, chapters[i].number, 2);
        h = fnv(h, chapters[i].node, 4);
        h = fnv(h, chapters[i].end_node, 4);
        h = fnv(h, chapters[i].first_verse, 4);
        h = fnv(h, chapters[i].verse_count, 4);
    }
    verses = USFM_Ast_GetVerses(ast, &count);
    for (i = 0; i < count; i++) {
        h = fnv(h, verses[i].ref.chapter, 2);
        h = fnv(h, verses[i].ref.verse_first, 2);
        h = fnv(h, verses[i].ref.verse_last, 2);
        h = fnv(h, verses[i].ref.segment_first, 1);
        h = fnv(h, verses[i].ref.segment_last, 1);
        h = fnv(h, verses[i].node, 4);
        h = fnv(h, verses[i].end_node, 4);
    }
    diags = USFM_Ast_GetDiagnostics(ast, &count);
    for (i = 0; i < count; i++) {
        h = fnv(h, diags[i].span.offset, 4);
        h = fnv(h, diags[i].span.length, 4);
        h = fnv(h, diags[i].code, 2);
        h = fnv(h, diags[i].marker, 2);
    }
    return h;
}
```

In `main`:
- Declare `uint64_t checksum = 0xcbf29ce484222325ull;` and `unsigned long long scan_diffs = 0;`.
- Also accept `.sfm` files: change the name test to `(len > 5 && strcmp(e->d_name + len - 5, ".usfm") == 0) || (len > 4 && strcmp(e->d_name + len - 4, ".SFM") == 0)`.
- After `data = read_file(...)` succeeds, before parsing, add:

```c
        scan_diffs += (unsigned long long)(scan_compare(data, length, 0u, scan_diffs == 0) +
                                           scan_compare(data, length, 1u, scan_diffs == 0) +
                                           scan_compare(data, length, 7u + (uint32_t)i, scan_diffs == 0));
```

- In the successful-parse branch, add `checksum = checksum_ast(checksum, ast);`.
- Before the `if (memcmp(&total, &expected, ...))` check, add:

```c
    printf("SIMD path: %s\n", USFM__SIMD_PATH);
    printf("Scanner differences: %llu\n", scan_diffs);
    printf("AST checksum: %016llx\n", (unsigned long long)checksum);
    if (scan_diffs != 0) failures++;
```

- [ ] **Step 2: Rewrite the `test)` case of `build.sh`**

Replace the whole `test)` case with:

```bash
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
            zig cc -target wasm32-wasi -msimd128 -std=c99 -O1 -c tests/run_tests.c -o /dev/null && echo "wasm-simd128: compiles"
        fi
        check_checksums
        ;;
```

Add this function after `unpack_corpus`. Later modes reuse it:

```bash
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
```

Update the usage line and the top comment to list the modes `debug|release|test|test-neon|test-wasm|test-avx512|fuzz|bench|codegen`. The modes from later tasks don't exist yet; that's fine.

- [ ] **Step 3: Run it**

Run: `./build.sh test 2>&1 | grep -E '^(scalar|sse2|avx2|default enums|avx512bw|neon|wasm|checksums|SIMD path|Scanner differences|AST checksum|corpus|fuzz replay)'`

Expected:
- `scalar:`, `sse2:` and `avx2:` each followed by `ok: N checks`;
- three `Scanner differences: 0` lines and three identical `AST checksum` lines;
- three `corpus: ok`, then `default enums: ok`, `fuzz replay: 15 inputs ok`, the three `compiles` lines, and `checksums: identical across 3 paths`.

- [ ] **Step 4: Commit**

```bash
git add tests/corpus_test.c build.sh
git commit -m "test(simd): run scalar, SSE2 and AVX2 paths; corpus scanner comparison and AST checksum

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: `build.sh test-neon`

**Files:**
- Modify: `build.sh`

- [ ] **Step 1: Add the mode** (before `fuzz)`)

```bash
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
```

- [ ] **Step 2: Run it**

Run: `./build.sh test-neon`

Expected:
- `SIMD path: neon`;
- `neon: ok: N checks`;
- the NEON corpus run reports `Scanner differences: 0` and `corpus: ok`;
- `checksums: identical across 2 paths`.

The timing tests in `tests/test_chars.c` have a 2-second budget. If they fail under QEMU, report it; don't loosen them without asking.

- [ ] **Step 3: Commit**

```bash
git add build.sh
git commit -m "test(simd): NEON path under qemu-aarch64

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: `build.sh test-wasm`

**Files:**
- Create: `tests/wasi_run.mjs`
- Modify: `build.sh`

- [ ] **Step 1: Write `tests/wasi_run.mjs`**

```js
// Runs a WASI (preview1) program under Node: node wasi_run.mjs prog.wasm [args...]
// The host root is preopened as "/", so pass absolute paths.
import { readFileSync } from 'node:fs';
import { WASI } from 'node:wasi';
import { argv, exit } from 'node:process';

const [, , wasmPath, ...args] = argv;
const wasi = new WASI({
  version: 'preview1',
  args: [wasmPath, ...args],
  env: {},
  preopens: { '/': '/' },
  returnOnExit: true,
});
const { instance } = await WebAssembly.instantiate(readFileSync(wasmPath), wasi.getImportObject());
exit(wasi.start(instance));
```

- [ ] **Step 2: Add the mode** (before `fuzz)`)

```bash
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
```

The corpus test writes its per-book CSV to the relative path `build/corpus/libusfm-web.csv`. WASI has no current directory, so that `fopen` returns NULL and the test simply skips the CSV. It already checks `if (csv)`.

- [ ] **Step 3: Run it**

Run: `./build.sh test-wasm`

Expected:
- `SIMD path: wasm-simd128`;
- `wasm: ok: N checks`;
- the corpus reports `Scanner differences: 0` and `corpus: ok`;
- `checksums: identical across 2 paths`.

If `opendir` on the absolute path fails under Node's WASI, report the error text. Don't change how corpus_test reads directories without checking first.

- [ ] **Step 4: Commit**

```bash
git add tests/wasi_run.mjs build.sh
git commit -m "test(simd): wasm simd128 path via zig cc and Node WASI

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: `build.sh test-avx512` (Intel SDE)

**Files:**
- Modify: `build.sh`

- [ ] **Step 1: Add the mode** (before `fuzz)`)

```bash
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
        grep 'SIMD path' build/run_tests_avx512.log
        echo "avx512bw: $(tail -n 1 build/run_tests_avx512.log)"
        rm -f build/checksums.txt
        ./build/corpus_test_ref build/corpus/WEB > build/corpus_ref.log
        echo "scalar $(grep 'AST checksum' build/corpus_ref.log | cut -d' ' -f3)" >> build/checksums.txt
        "$USFM_SDE" -spr -- ./build/corpus_test_avx512 build/corpus/WEB | tee build/corpus_avx512.log
        echo "avx512bw $(grep 'AST checksum' build/corpus_avx512.log | cut -d' ' -f3)" >> build/checksums.txt
        check_checksums
        ;;
```

`-spr` makes SDE emulate a Sapphire Rapids CPU, which has AVX-512BW. These builds use no sanitizers, because ASan and SDE's instrumentation don't mix reliably. The native builds cover memory errors.

- [ ] **Step 2: Run it**

Run: `./build.sh test-avx512`

Expected:
- **Without `USFM_SDE`:** `test-avx512: skipped (...)`, exit 0.
- **With SDE:** `SIMD path: avx512bw`, `avx512bw: ok: N checks`, `Scanner differences: 0` and `checksums: identical across 2 paths`.

SDE isn't installed yet, so run the skip case now. If `USFM_SDE` happens to be set, run the full case and report it. Running under SDE takes minutes, not seconds.

- [ ] **Step 3: Commit**

```bash
git add build.sh
git commit -m "test(simd): AVX-512BW path under Intel SDE (USFM_SDE)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Fuzzing with the two-scanner comparison

**Files:**
- Modify: `tests/fuzz_ast.c`, `build.sh`

- [ ] **Step 1: Compare scanners on every fuzz input**

In `tests/fuzz_ast.c`, add `#define USFM_TEST_SCAN_REFERENCE` before `#define USFM_IMPLEMENTATION`, and `#include "scan_compare.h"` after `#include "check.h"`. In `LLVMFuzzerTestOneInput`, right after the `if (size < 2 ...) return 0;` line, add:

```c
    {
        const char *text = (const char *)data + 2;
        uint32_t text_length = (uint32_t)(size - 2);
        if (scan_compare(text, text_length, 0u, 1) || scan_compare(text, text_length, 1u, 1) ||
            scan_compare(text, text_length, 2u + data[1], 1)) {
            abort();
        }
    }
```

- [ ] **Step 2: Build the fuzzer for AVX2**

In `build.sh`'s `fuzz)` case, change the clang line to:

```bash
        clang -std=c99 -g -O1 -mavx2 -fsanitize=fuzzer,address,undefined tests/fuzz_ast.c -o build/fuzz_ast
```

- [ ] **Step 3: Check the replay, then run the fuzzer for 30 minutes**

```bash
./build.sh test 2>&1 | grep -E 'fuzz replay|checksums'
./build.sh fuzz
./build/fuzz_ast -dict=tests/fuzz/usfm.dict -max_total_time=1800 -print_final_stats=1 -artifact_prefix=build/fuzz/ build/fuzz/corpus tests/fuzz/seeds 2>&1 | tail -5
ls build/fuzz | grep -E 'crash|leak|timeout|oom' || echo "no findings"
```

Run the 30-minute fuzz in the background (it takes longer than a foreground command's time limit), and wait for it to finish.

Expected:
- `fuzz replay: 15 inputs ok`;
- `Done N runs in 1801 second(s)`;
- `no findings`.

For any finding:
1. Copy it to `tests/fuzz/seeds/regress-<name>.usfm`.
2. Use superpowers:systematic-debugging to find the cause.
3. Add a unit test, fix, and rerun until a clean 30-minute run.

- [ ] **Step 4: Commit**

```bash
git add tests/fuzz_ast.c build.sh tests/fuzz/seeds
git commit -m "test(simd): fuzz compares block and byte-loop scanners on every input (AVX2 build)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Benchmark, header docs and release notes

**Files:**
- Create: `tests/bench.c`, `docs/release-notes/0.1.1.md`
- Modify: `build.sh`, `usfm.h`

- [ ] **Step 1: Write `tests/bench.c`**

```c
/* Whole-corpus benchmark. Usage: bench [-n passes] DIR...
 * Reads every .usfm / .sfm file of each DIR into memory, then times
 * whole-directory passes of USFM_Ast_Parse + USFM_Ast_Free and reports
 * the fastest pass (1 warmup pass first). On Linux it also reports
 * user-space instructions, cycles and L1D read misses for that pass
 * (perf_event_open; shown as n/a when unavailable). Pin the process to
 * one CPU (taskset) for stable numbers on hybrid CPUs. */
#define _GNU_SOURCE 1
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <stdint.h>
#if defined(__linux__)
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#define USFM_IMPLEMENTATION
#include "../usfm.h"

#define BENCH_MAX_FILES 1024
#define BENCH_COUNTERS 3

typedef struct Counters {
    int      fd[BENCH_COUNTERS];
    uint64_t value[BENCH_COUNTERS];
} Counters;

static void counters_open(Counters *c)
{
    int k;
    for (k = 0; k < BENCH_COUNTERS; k++) c->fd[k] = -1;
#if defined(__linux__)
    for (k = 0; k < BENCH_COUNTERS; k++) {
        struct perf_event_attr attr;
        memset(&attr, 0, sizeof attr);
        attr.size = sizeof attr;
        attr.disabled = 1;
        attr.exclude_kernel = 1;
        attr.exclude_hv = 1;
        if (k == 0) { attr.type = PERF_TYPE_HARDWARE; attr.config = PERF_COUNT_HW_INSTRUCTIONS; }
        if (k == 1) { attr.type = PERF_TYPE_HARDWARE; attr.config = PERF_COUNT_HW_CPU_CYCLES; }
        if (k == 2) {
            attr.type = PERF_TYPE_HW_CACHE;
            attr.config = PERF_COUNT_HW_CACHE_L1D | (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                          (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);
        }
        c->fd[k] = (int)syscall(SYS_perf_event_open, &attr, 0, -1, -1, 0);
    }
#endif
}

static void counters_start(Counters *c)
{
    int k;
    for (k = 0; k < BENCH_COUNTERS; k++) {
        c->value[k] = 0;
#if defined(__linux__)
        if (c->fd[k] >= 0) {
            ioctl(c->fd[k], PERF_EVENT_IOC_RESET, 0);
            ioctl(c->fd[k], PERF_EVENT_IOC_ENABLE, 0);
        }
#endif
    }
}

static void counters_stop(Counters *c)
{
    int k;
    for (k = 0; k < BENCH_COUNTERS; k++) {
#if defined(__linux__)
        if (c->fd[k] >= 0) {
            ioctl(c->fd[k], PERF_EVENT_IOC_DISABLE, 0);
            if (read(c->fd[k], &c->value[k], sizeof c->value[k]) != (ssize_t)sizeof c->value[k]) c->fd[k] = -1;
        }
#endif
    }
}

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e3 + (double)t.tv_nsec / 1e6;
}

static int is_usfm_name(const char *name)
{
    size_t l = strlen(name);
    return (l > 5 && strcasecmp(name + l - 5, ".usfm") == 0) ||
           (l > 4 && strcasecmp(name + l - 4, ".sfm") == 0);
}

static void bench_dir(const char *dir, int passes)
{
    static char *data[BENCH_MAX_FILES];
    static uint32_t len[BENCH_MAX_FILES];
    size_t n = 0, bytes = 0, i;
    double best = 1e300;
    uint64_t best_counters[BENCH_COUNTERS] = { 0, 0, 0 };
    Counters c;
    DIR *d = opendir(dir);
    struct dirent *e;
    int pass, k;
    if (d == NULL) {
        printf("%s: cannot open\n", dir);
        return;
    }
    while ((e = readdir(d)) != NULL && n < BENCH_MAX_FILES) {
        char path[4096];
        FILE *f;
        long size;
        if (!is_usfm_name(e->d_name)) continue;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        f = fopen(path, "rb");
        if (f == NULL) continue;
        fseek(f, 0, SEEK_END);
        size = ftell(f);
        fseek(f, 0, SEEK_SET);
        data[n] = (char *)malloc(size > 0 ? (size_t)size : 1u);
        if (data[n] == NULL || (size > 0 && fread(data[n], 1, (size_t)size, f) != (size_t)size)) {
            fclose(f);
            continue;
        }
        fclose(f);
        len[n] = (uint32_t)size;
        bytes += (size_t)size;
        n++;
    }
    closedir(d);
    counters_open(&c);
    for (pass = -1; pass < passes; pass++) {        /* pass -1 is the warmup */
        double t0, ms;
        counters_start(&c);
        t0 = now_ms();
        for (i = 0; i < n; i++) {
            USFM_Err err;
            USFM_Ast_Free(USFM_Ast_Parse(data[i], len[i], NULL, &err));
        }
        ms = now_ms() - t0;
        counters_stop(&c);
        if (pass >= 0 && ms < best) {
            best = ms;
            for (k = 0; k < BENCH_COUNTERS; k++) best_counters[k] = c.value[k];
        }
    }
    printf("%-26s %-12s %4zu files %8.2f MiB  %9.2f ms  %7.1f MiB/s", dir, USFM__SIMD_PATH, n,
           (double)bytes / 1048576.0, best, (double)bytes / 1048576.0 / (best / 1e3));
    if (c.fd[0] >= 0) printf("  %8.1fM instr", (double)best_counters[0] / 1e6); else printf("  instr n/a");
    if (c.fd[1] >= 0) printf("  %8.1fM cycles", (double)best_counters[1] / 1e6); else printf("  cycles n/a");
    if (c.fd[2] >= 0) printf("  %7.1fk L1D miss", (double)best_counters[2] / 1e3); else printf("  L1D n/a");
    printf("\n");
    for (i = 0; i < n; i++) free(data[i]);
}

int main(int argc, char **argv)
{
    int passes = 5, a = 1;
    if (argc > 2 && strcmp(argv[1], "-n") == 0) {
        passes = atoi(argv[2]);
        a = 3;
    }
    if (a >= argc) {
        fprintf(stderr, "usage: %s [-n passes] DIR...\n", argv[0]);
        return 1;
    }
    for (; a < argc; a++) bench_dir(argv[a], passes > 0 ? passes : 1);
    return 0;
}
```

- [ ] **Step 2: Add the `bench` mode to `build.sh`** (before `codegen)`)

```bash
    bench)
        run_codegen
        unpack_corpus
        shift
        DIRS=("$@")
        if [[ ${#DIRS[@]} -eq 0 ]]; then DIRS=(build/corpus/WEB); fi
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
```

Every variant uses `-march=native` and differs only in which SIMD path the header picks, so the comparison isolates the scanner. `scalar` is v0.1's scanner.

- [ ] **Step 3: Run the benchmarks**

```bash
USFM_BENCH_CPU=2 ./build.sh bench
USFM_BENCH_CPU=6 ./build.sh bench
```

On this i7-1355U, CPU 2 is a P-core and CPU 6 an E-core. Run `lscpu -e` if unsure, and pick one of each.

The controller (not the plan) may pass extra local corpus directories as arguments, for example `./build.sh bench build/corpus/WEB "<path to HPUX>"`. Never write those paths into a committed file.

Expected:
- one line per variant per directory;
- every SIMD variant at least as fast as `scalar` on every directory, which is the release bar.

If a SIMD variant is slower anywhere, stop and report the numbers.

- [ ] **Step 4: Document `USFM_SIMD_NONE` in the header**

In `usfm.h`'s compile-time configuration comment, after the `USFM_NO_LIBC` lines, add:

```c
/* SIMD: the scanner finds text and attribute boundaries 64 bytes at a
 * time. The instruction set is chosen at compile time from the
 * compiler's target macros: AVX-512BW (-mavx512bw), AVX2 (-mavx2), SSE2
 * (always on x86-64), NEON (ARM64), wasm simd128 (-msimd128); anything
 * else uses byte loops. There is no runtime CPU detection, so build with
 * the flags for the machines you target (e.g. -march=native). Define
 * USFM_SIMD_NONE to force the byte loops. Every path produces identical
 * results, and none reads past the end of the input. */
```

- [ ] **Step 5: Write `docs/release-notes/0.1.1.md`**

Fill in the tables with the measured numbers from Step 3, one row per variant and corpus. Use only basenames for local corpora (e.g. "HPUX"), never paths.

```markdown
# libusfm 0.1.1

A speed-only release: the scanner finds text and attribute boundaries
64 bytes at a time with SIMD. The public API, the AST and every parse
result are unchanged.

## What changed

- New internal block classifier with SSE2, AVX2, AVX-512BW, NEON and
  wasm simd128 versions, chosen at compile time (see the SIMD comment in
  `usfm.h`). No runtime CPU detection.
- New configuration macro `USFM_SIMD_NONE` forces the v0.1 byte loops.
- Nothing is read past the end of the input on any path.

## Verification

- The block scanner and the v0.1 byte-loop scanner are compared token
  by token in the unit tests, over the whole WEB corpus, and on every
  fuzz input.
- The WEB AST checksum is identical on scalar, SSE2, AVX2, NEON
  (qemu-aarch64) and wasm simd128 (Node WASI), and on AVX-512BW under
  Intel SDE when run.
- A 30-minute libFuzzer run on the AVX2 build found nothing.

## Performance

Whole-corpus parse (parse + free), fastest of 5 passes, clang -O3
-march=native, Intel i7-1355U.

| Corpus | Core | scalar (v0.1) | sse2 | avx2 |
| --- | --- | ---: | ---: | ---: |
| WEB | P-core | (measured) | (measured) | (measured) |
| WEB | E-core | (measured) | (measured) | (measured) |
```

Then replace every `(measured)` with real values, formatted as `MiB/s (ms)`, and add rows for any local corpora the controller benchmarked. No `(measured)` may remain when you commit.

- [ ] **Step 6: Run the tests and a release build, then commit**

```bash
./build.sh test 2>&1 | tail -3
./build.sh release && ./usfm
git add tests/bench.c build.sh usfm.h docs/release-notes/0.1.1.md
git commit -m "feat(simd): whole-corpus benchmark mode, SIMD header docs, 0.1.1 release notes

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9: Final verification

- [ ] **Step 1: Run every mode from a clean build directory**

```bash
rm -rf build
./build.sh test
./build.sh test-neon
./build.sh test-wasm
./build.sh test-avx512
./build.sh release
./build.sh debug
git status --short
```

Expected:
- `test`: `checksums: identical across 3 paths`;
- `test-neon` and `test-wasm`: `identical across 2 paths`;
- `test-avx512`: either the skip message or `identical across 2 paths`;
- `release` and `debug`: both build;
- `git status`: clean.

- [ ] **Step 2: Check the spec against the result**

Read `docs/superpowers/specs/2026-11-15-simd-scanner-design.md`. For each "Done when" item and each scope bullet, name the test or file that covers it. Report any gap, and say whether AVX-512 was run under SDE or only compiled.

- [ ] **Step 3: Report to the user**

Summarize:
- the paths tested;
- the checksums;
- the fuzz result;
- the benchmark table.

Don't push, merge or change the PR without being asked.

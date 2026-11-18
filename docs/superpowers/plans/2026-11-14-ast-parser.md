# libusfm v0.1 AST Parser Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a single-header C99 USFM parser (`usfm.h`) whose public API parses USFM text into a preorder AST, with chapter/verse tables and iterators. It is done when it reproduces the WEB corpus counts exactly and survives a 30-minute fuzz run.

**Architecture:** One pass, with no token array:

- An internal scanner (no allocation) hands one token at a time to a parser loop.
- The loop keeps a fixed stack of 64 open containers. For each token it closes whatever must close, then opens or appends.
- Nodes go into one growing preorder array (`USFM_Node`, 20 bytes).
- Marker names are looked up through a perfect hash. `codegen/codegen.c` generates the hash into `usfm.h` from the hand-written table in `codegen/markers.h`.

**Tech Stack:**

- C99 (the header also compiles as C++)
- gcc 11 for debug and test builds, with ASan and UBSan
- clang 14 for release builds and libFuzzer
- bash (`build.sh`)
- No third-party dependencies

**Spec:** `docs/superpowers/specs/2026-11-14-ast-parser-design.md`. Background on USFM is in `docs/usfm-field-guide.md`. The source of the public API names is `usfm_claude_api.h`.

---

## Conventions for every task

- Work on branch `feat/ast-parser` in `~/repos/libusfm`. Run every command from the repo root.
- Commit messages end with a blank line followed by:
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`
- Code style follows the existing code:
  - Allman braces in codegen.
  - K&R braces are fine in `usfm.h`.
  - 4-space indent.
  - `/* */` comments only in `usfm.h`. Codegen treats any line that starts with `//` as a potential codegen marker.
- Internal names in `usfm.h` use the prefix `usfm__` (two underscores) or `USFM_` for internal types.
- Power of 10 rules:
  - No recursion.
  - Every loop is bounded by the input length or a constant. Say which in a comment where it isn't obvious.
  - No allocation except through `USFM_Alc`.
- "Run tests" always means `./build.sh test`. Expected output ends with `ok: N checks` and exit code 0.

## File structure

| File | Responsibility |
| --- | --- |
| `usfm.h` | The library. Public API first, then the implementation behind `USFM_IMPLEMENTATION`. The implementation is in sections in this order: internal types and hash (always compiled); generated tables; Allocator; Errors and diagnostics; Marker lookup; Scanner; AST handle; Parser; Lookups; Iterators. |
| `codegen/markers.h` | The hand-written marker table, one row per key. **This is the data the user asked to live in codegen.** |
| `codegen/codegen.c` | Generates the byte-class table and the marker table plus perfect hash into `usfm.h`'s `// USFM_CODEGEN` blocks. |
| `codegen/build.sh` | Builds codegen. |
| `build.sh` | Modes: `debug`, `release`, `test`, `fuzz`, `codegen`. |
| `main.c` | The benchmark harness (cycles/byte on one file). |
| `tests/test.h` | Minimal test framework (`CHECK`, `CHECK_EQ`, `CHECK_STR`). |
| `tests/check.h` | `check_ast()`, the invariant checker. Shared by the unit, corpus and fuzz tests. |
| `tests/dump.h` | Text dumps of tokens, trees and diagnostics. |
| `tests/helpers.h` | `CHECK_TREE` / `CHECK_DIAGS` parse-and-compare helpers. |
| `tests/run_tests.c` | A unity build: defines `USFM_IMPLEMENTATION` and includes every `tests/test_*.c`. |
| `tests/test_*.c` | Unit tests, one file per area. |
| `tests/cpp_check.cpp` | Proves that `usfm.h` compiles as C++. |
| `tests/corpus_test.c` | Parses the WEB corpus and checks the totals. |
| `tests/corpus/WEB.tar.gz` | The 83 WEB `.usfm` files plus `copr.htm`. |
| `tests/corpus/paratextdata-web.csv` | ParatextData's per-book counts, for diffing. |
| `tests/fuzz_ast.c` | The libFuzzer harness. Built with `-DUSFM_FUZZ_REPLAY`, it is a replay tool. |
| `tests/fuzz/usfm.dict`, `tests/fuzz/seeds/*` | The fuzz dictionary and seed corpus. |

---

### Task 0: Spec amendments

These are decisions made while planning, recorded in the spec before any code is written.

**Files:**
- Modify: `docs/superpowers/specs/2026-11-14-ast-parser-design.md`

- [ ] **Step 1: Replace the "Changes to the drafted API" section's ARGUMENT/ATTRIBUTES paragraph**

Replace the paragraph that begins `When present, they are the first children of their owner node` with:

```markdown
  An ARGUMENT, when present, is the first child of its owner. ATTRIBUTES
  appear where they were written: the last child of a character span or
  figure (`\w word|lemma="x"\w*`), right after the ARGUMENT of a `\periph`,
  and the only child of a milestone.
```

- [ ] **Step 2: Add two flags and a diagnostic to the same section**

Replace the bullet about `USFM_NF_IMPLIED` with:

```markdown
- PARA nodes carry two node flags in the high bits of `level_flags`.
  `USFM_TF_NESTED` and the milestone flags never apply to paragraphs, so
  these bits are free on PARA nodes. The API already uses the same trick for
  the TEXT flags.
  - `USFM_NF_IMPLIED = 0x10`: the parser inserted this paragraph.
  - `USFM_NF_HEADING = 0x20`: a heading paragraph, such as `\s`, `\ms`, `\mt`
    or `\r`. The verse iterator skips these.
- `enum USFM_DiagnosticCode` gains `USFM_DIAGNOSTIC_TOO_DEEP`, emitted when
  a marker would push the stack past `USFM_STACK_DEPTH`.
```

- [ ] **Step 3: Pin down text flags and whitespace in the Scanner section**

Replace the bullet `A newline inside a paragraph, or a run of spaces, sets USFM_TF_HAS_LOOSE_WS.` with:

```markdown
  - A CR, LF or TAB inside the text sets `USFM_TF_HAS_LOOSE_WS`. Runs of
    spaces are not detected: doing so would put the space byte on the hot
    path.
```

Then append this bullet to the end of the Scanner section's list:

```markdown
- **Trailing whitespace.** Before a paragraph-level marker, a `\v`, or the
  end of input, the parser trims trailing whitespace from the last TEXT
  node of the open container, and removes the node if nothing is left.
  Whitespace-only text at block level (directly under BOOK, SIDEBAR,
  PERIPHERAL or TABLE) is dropped.
```

- [ ] **Step 4: Record the exact counting rule in the Goal section**

Replace the paragraph that begins `The counts come from` with:

```markdown
The counts come from `docs/usfm-field-guide.md` §12. They were produced by
`~/repos/usfm/ParatextData/CountingSink.cs`, which counts ParatextData
parser events. Counting the markers in the WEB files checks the arithmetic:

- **Character spans:** every `\w`, `\+w`, `\wj`, `\fr`, `\ft`, `\fqa`,
  `\xt`, `\xo`, `\k`, `\+wh`, `\qs`, `\bk`, `\fl`, `\+bk` and `\fq` sums to
  exactly 685,197. So note parts count as character spans.
- **Paragraphs:** every paragraph marker, including `\h`, `\toc1-3`, `\ide`
  and `\mt#`, sums to 34,754. That is one more than 34,753, because
  ParatextData reports `\cp` (1 occurrence) as part of the chapter event,
  not as a paragraph.

libusfm's counter therefore counts:

- chapters: CHAPTER nodes;
- verses: VERSE nodes;
- character spans: CHAR nodes;
- notes: NOTE nodes;
- paragraphs: PARA nodes without `USFM_NF_IMPLIED` and whose marker is not
  `\cp`, plus HEADER nodes whose marker is not `\id`.

Per-book chapter, verse and paragraph counts from ParatextData are kept in
`tests/corpus/paratextdata-web.csv` for diffing.
```

- [ ] **Step 5: Fix the fuzzing and codegen bullets in "Testing and build"**

Replace the bullet `The fuzzer also runs the 2.x and 3.1 nesting cases from the unit tests and checks them against the closing rules.` with:

```markdown
  - The seed corpus includes the 2.x and 3.1 nesting cases from the unit
    tests, so the fuzzer mutates real closing-rule inputs. The fuzzer
    checks invariants, not tree shapes; the unit tests check shapes.
```

Replace `Codegen runs whenever its table changes.` with:

```markdown
`build.sh` reruns codegen whenever `codegen/codegen.c` or
`codegen/markers.h` is newer than `build/codegen.stamp`.
```

- [ ] **Step 6: Commit**

```bash
git add docs/superpowers/specs/2026-11-14-ast-parser-design.md
git commit -m "docs: spec amendments from implementation planning

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 1: Header skeleton, build script, test runner

**Files:**
- Modify: `usfm.h` (full rewrite after the license comment)
- Modify: `main.c` (temporary stub)
- Modify: `build.sh`
- Modify: `.gitignore`
- Create: `tests/test.h`, `tests/run_tests.c`, `tests/test_basics.c`, `tests/cpp_check.cpp`

- [ ] **Step 1: Write the test framework `tests/test.h`**

```c
#ifndef USFM_TEST_H
#define USFM_TEST_H
/* Minimal test framework: no dependencies, no allocation. */
#include <stdio.h>
#include <string.h>

static int usfm_test_failures = 0;
static int usfm_test_checks = 0;

#define CHECK(cond) do { \
    usfm_test_checks++; \
    if (!(cond)) { \
        usfm_test_failures++; \
        fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

#define CHECK_EQ(actual, expected) do { \
    unsigned long long check_a_ = (unsigned long long)(actual); \
    unsigned long long check_e_ = (unsigned long long)(expected); \
    usfm_test_checks++; \
    if (check_a_ != check_e_) { \
        usfm_test_failures++; \
        fprintf(stderr, "%s:%d: %s == %llu, expected %llu\n", \
                __FILE__, __LINE__, #actual, check_a_, check_e_); \
    } \
} while (0)

#define CHECK_STR(actual, expected) do { \
    const char *check_a_ = (actual); \
    const char *check_e_ = (expected); \
    usfm_test_checks++; \
    if (strcmp(check_a_, check_e_) != 0) { \
        usfm_test_failures++; \
        fprintf(stderr, "%s:%d: %s\n--- expected\n%s\n--- actual\n%s\n", \
                __FILE__, __LINE__, #actual, check_e_, check_a_); \
    } \
} while (0)

#define RUN(test) do { fprintf(stderr, "  %s\n", #test); test(); } while (0)

#endif
```

- [ ] **Step 2: Write the first test `tests/test_basics.c`**

```c
/* Struct sizes are part of the ABI; the header also asserts them at
 * compile time. */
static void test_struct_sizes(void)
{
    CHECK_EQ(sizeof(USFM_Span), 8);
    CHECK_EQ(sizeof(USFM_Token), 12);
    CHECK_EQ(sizeof(USFM_Node), 20);
    CHECK_EQ(sizeof(USFM_Diagnostic), 12);
    CHECK_EQ(sizeof(USFM_ChapterEntry), 20);
    CHECK_EQ(sizeof(USFM_VerseEntry), 16);
}

static void test_marker_hash_uses_length(void)
{
    /* "pp" and "p" share their first byte; the length must separate them. */
    CHECK(usfm__marker_hash("p", 1) != usfm__marker_hash("pp", 2));
    CHECK(usfm__marker_hash("add", 3) == usfm__marker_hash("add", 3));
}

static void run_basics_tests(void)
{
    RUN(test_struct_sizes);
    RUN(test_marker_hash_uses_length);
}
```

- [ ] **Step 3: Write the runner `tests/run_tests.c`**

```c
/* Unity build of every unit test. Internal (static) functions of usfm.h
 * are visible here because this translation unit defines
 * USFM_IMPLEMENTATION. */
#define USFM_IMPLEMENTATION
#include "../usfm.h"
#include "test.h"

#include "test_basics.c"

int main(void)
{
    fprintf(stderr, "basics\n");
    run_basics_tests();
    if (usfm_test_failures != 0) {
        fprintf(stderr, "FAILED: %d of %d checks\n", usfm_test_failures, usfm_test_checks);
        return 1;
    }
    fprintf(stderr, "ok: %d checks\n", usfm_test_checks);
    return 0;
}
```

- [ ] **Step 4: Write the C++ compile check `tests/cpp_check.cpp`**

```cpp
// Compiled with g++ -fsyntax-only: proves usfm.h is valid C++.
#define USFM_IMPLEMENTATION
#include "../usfm.h"

int main()
{
    USFM_Err err;
    USFM_Ast *ast = USFM_Ast_Parse("\\id GEN\n", 8, nullptr, &err);
    USFM_Ast_Free(ast);
    return 0;
}
```

- [ ] **Step 5: Rewrite `build.sh`**

```bash
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
```

(`markers.h` does not exist until Task 2. `-nt` against a missing file is false, so this is harmless.)

- [ ] **Step 6: Update `.gitignore`**

Append:

```
# build outputs
build/
```

- [ ] **Step 7: Replace `main.c` with a temporary stub**

The old `main.c` calls the old `USFM_Tokenize`, which this task removes. Task 14 writes the real benchmark.

```c
#include <stdio.h>
#define USFM_IMPLEMENTATION
#include "usfm.h"

int main(void)
{
    printf("libusfm %s\n", USFM_VERSION);
    return 0;
}
```

- [ ] **Step 8: Run tests to verify they fail**

Run: `./build.sh test`
Expected: compile errors such as `unknown type name 'USFM_Node'` and `implicit declaration of function 'usfm__marker_hash'`. The old header doesn't have the new API.

- [ ] **Step 9: Rewrite `usfm.h`**

Keep the opening license comment block exactly as it is: everything from `/* usmf - alpha - USFM Parser` to its closing ` */`. Delete everything after it, including the old token enums, `USFM_Tokenize`, the old `USFM_MarkerType` enum, both old codegen blocks and the old implementation. Then append the following:

```c

#ifndef USFM_H
#define USFM_H

#define USFM_VERSION_MAJOR 0
#define USFM_VERSION_MINOR 1
#define USFM_VERSION_PATCH 0
#define USFM_VERSION "0.1.0-dev"

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------
 * Overview
 *
 * Single-header USFM parser. C99, compiles as C++.
 * In exactly one translation unit:
 *     #define USFM_IMPLEMENTATION
 *     #include "usfm.h"
 *
 *     USFM_Err err;
 *     USFM_Ast *ast = USFM_Ast_Parse(src, len, NULL, &err);
 *     if (!ast) { ... err.code, err.msg, err.pos ... }
 *     ...
 *     USFM_Ast_Free(ast);
 *
 * Ground rules (NASA Power of 10 + TigerStyle):
 *   - All allocation goes through a pluggable allocator (USFM_Alc).
 *   - No recursion. The parser drives an explicit stack of
 *     USFM_STACK_DEPTH open containers.
 *   - Every loop is bounded by the input length or a constant.
 *   - Trees use u32 indices, not pointers. Sizes are u32.
 *   - Zero-copy: all text is a (offset, length) span into the input.
 *   - Malformed USFM is never an error: it produces diagnostics.
 *
 * Failure semantics
 *   NULL                          -> failed, nothing to free (see err)
 *   non-NULL, err.code == OK      -> complete
 *   non-NULL, err.code == NOMEM   -> tree complete, some diagnostics
 *                                    were dropped
 * ------------------------------------------------------------------- */

/* ---------------------------------------------------------------------
 * Compile-time configuration
 * ------------------------------------------------------------------- */
#ifndef USFM_STACK_DEPTH
#define USFM_STACK_DEPTH 64u            /* max open containers */
#endif
#ifndef USFM_INPUT_MAX
#define USFM_INPUT_MAX 0x7FFFFFFFu      /* 2 GiB - 1 */
#endif
#ifndef USFM_ASSERT
#include <assert.h>
#define USFM_ASSERT(x) assert(x)
#endif
/* Define USFM_NO_LIBC to drop the default malloc-based allocator. */

#define USFM_NIL 0xFFFFFFFFu            /* "no node" index */

#ifndef USFM_API
#  if defined(USFM_SHARED) && defined(_WIN32)
#    ifdef USFM_BUILD
#      define USFM_API __declspec(dllexport)
#    else
#      define USFM_API __declspec(dllimport)
#    endif
#  elif defined(USFM_SHARED) && defined(__GNUC__)
#    define USFM_API __attribute__((visibility("default")))
#  else
#    define USFM_API
#  endif
#endif

/* ---------------------------------------------------------------------
 * Basic types
 * ------------------------------------------------------------------- */
typedef struct USFM_Span {
    uint32_t offset;                    /* byte offset into input */
    uint32_t length;                    /* byte length */
} USFM_Span;

typedef enum USFM_Status {
    USFM_OK = 0,
    USFM_ERR_CAPACITY,                  /* caller's output buffer too small */
    USFM_ERR_DEPTH,                     /* reserved */
    USFM_ERR_INPUT_TOO_LARGE,
    USFM_ERR_INVALID_ARG,
    USFM_ERR_NOMEM,                     /* allocator returned NULL */
    USFM_ERR_NO_ALLOCATOR               /* NULL allocator + USFM_NO_LIBC */
} USFM_Status;

/* Filled by every function that can fail (pass NULL to ignore). */
typedef struct USFM_Err {
    const char *msg;                    /* static string, never NULL */
    uint32_t    code;                   /* USFM_Status */
    uint32_t    pos;                    /* byte offset reached in input */
} USFM_Err;

enum USFM_Version {
    USFM_VERSION_AUTO = 0,              /* read \usfm, else assume 3.1 */
    USFM_VERSION_1,
    USFM_VERSION_2,
    USFM_VERSION_3_0,
    USFM_VERSION_3_1
};

/* Marker ids. Values are part of the ABI: never renumber or reuse one;
 * append new markers before USFM_M_CUSTOM_BASE. Numbered markers share
 * a base id and carry the number as a level: \q2 -> (USFM_M_Q, 2). */
typedef uint16_t USFM_Marker;
enum {
    USFM_M_NONE = 0,                    /* not a marker */
    USFM_M_UNKNOWN = 1,                 /* name available via span */

    /* Identification */
    USFM_M_ID = 2, USFM_M_USFM = 3,

    /* Chapters and verses */
    USFM_M_C = 4, USFM_M_CA = 5, USFM_M_CP = 6,
    USFM_M_V = 7, USFM_M_VA = 8, USFM_M_VP = 9,

    /* Paragraphs */
    USFM_M_B = 10, USFM_M_CD = 11, USFM_M_CL = 12, USFM_M_CLS = 13,
    USFM_M_D = 14, USFM_M_H = 15, USFM_M_IB = 16, USFM_M_IDE = 17,
    USFM_M_IE = 18, USFM_M_IEX = 19, USFM_M_ILI = 20, USFM_M_IM = 21,
    USFM_M_IMI = 22, USFM_M_IMQ = 23, USFM_M_IMT = 24, USFM_M_IMTE = 25,
    USFM_M_IO = 26, USFM_M_IOT = 27, USFM_M_IP = 28, USFM_M_IPC = 29,
    USFM_M_IPI = 30, USFM_M_IPQ = 31, USFM_M_IPR = 32, USFM_M_IQ = 33,
    USFM_M_IS = 34, USFM_M_LF = 35, USFM_M_LH = 36, USFM_M_LI = 37,
    USFM_M_LIM = 38, USFM_M_LIT = 39, USFM_M_M = 40, USFM_M_MI = 41,
    USFM_M_MR = 42, USFM_M_MS = 43, USFM_M_MT = 44, USFM_M_MTE = 45,
    USFM_M_NB = 46, USFM_M_P = 47, USFM_M_PB = 48, USFM_M_PC = 49,
    USFM_M_PI = 50, USFM_M_PM = 51, USFM_M_PMC = 52, USFM_M_PMO = 53,
    USFM_M_PMR = 54, USFM_M_PO = 55, USFM_M_PR = 56, USFM_M_Q = 57,
    USFM_M_QA = 58, USFM_M_QC = 59, USFM_M_QD = 60, USFM_M_QM = 61,
    USFM_M_QR = 62, USFM_M_R = 63, USFM_M_REM = 64, USFM_M_S = 65,
    USFM_M_SD = 66, USFM_M_SP = 67, USFM_M_SR = 68, USFM_M_STS = 69,
    USFM_M_TOC = 70, USFM_M_TOCA = 71, USFM_M_TR = 72,

    /* Character styles */
    USFM_M_ADD = 73, USFM_M_BD = 74, USFM_M_BDIT = 75, USFM_M_BK = 76,
    USFM_M_DC = 77, USFM_M_EM = 78, USFM_M_FM = 79, USFM_M_FV = 80,
    USFM_M_IOR = 81, USFM_M_IQT = 82, USFM_M_IT = 83, USFM_M_JMP = 84,
    USFM_M_K = 85, USFM_M_LIK = 86, USFM_M_LITL = 87, USFM_M_LIV = 88,
    USFM_M_ND = 89, USFM_M_NO = 90, USFM_M_ORD = 91, USFM_M_PN = 92,
    USFM_M_PNG = 93, USFM_M_QAC = 94, USFM_M_QS = 95, USFM_M_QT = 96,
    USFM_M_RB = 97, USFM_M_REF = 98, USFM_M_RQ = 99, USFM_M_SC = 100,
    USFM_M_SIG = 101, USFM_M_SLS = 102, USFM_M_SUP = 103, USFM_M_TL = 104,
    USFM_M_W = 105, USFM_M_WA = 106, USFM_M_WG = 107, USFM_M_WH = 108,
    USFM_M_WJ = 109,

    /* Notes */
    USFM_M_EF = 110, USFM_M_EFE = 111, USFM_M_EX = 112, USFM_M_F = 113,
    USFM_M_FE = 114, USFM_M_X = 115,

    /* Note content */
    USFM_M_FK = 116, USFM_M_FL = 117, USFM_M_FP = 118, USFM_M_FQ = 119,
    USFM_M_FQA = 120, USFM_M_FR = 121, USFM_M_FT = 122, USFM_M_FW = 123,
    USFM_M_XK = 124, USFM_M_XNT = 125, USFM_M_XO = 126, USFM_M_XOP = 127,
    USFM_M_XOT = 128, USFM_M_XQ = 129, USFM_M_XT = 130, USFM_M_XTA = 131,

    /* Table cells */
    USFM_M_TC = 132, USFM_M_TCC = 133, USFM_M_TCR = 134, USFM_M_TH = 135,
    USFM_M_THC = 136, USFM_M_THR = 137,

    /* Milestones */
    USFM_M_TS = 138,

    /* Figures, peripherals, sidebars, categories */
    USFM_M_CAT = 139, USFM_M_ESB = 140, USFM_M_ESBE = 141, USFM_M_FIG = 142,
    USFM_M_PERIPH = 143,

    /* Deprecated (still found in older files) */
    USFM_M_ADDPN = 144, USFM_M_FDC = 145, USFM_M_FS = 146, USFM_M_PH = 147,
    USFM_M_PHI = 148, USFM_M_PRO = 149, USFM_M_XDC = 150,

    /* Obsolete (removed before 2.1) */
    USFM_M_CONC = 151, USFM_M_COV = 152, USFM_M_GLO = 153, USFM_M_IDX = 154,
    USFM_M_INTRO = 155, USFM_M_MAPS = 156, USFM_M_PREF = 157, USFM_M_PS = 158,
    USFM_M_PSI = 159, USFM_M_PUB = 160, USFM_M_PUBINFO = 161,
    USFM_M_SPINE = 162, USFM_M_WR = 163,

    /* Stylesheet-only (defined in usfm.sty, not the spec) */
    USFM_M_EFM = 164, USFM_M_NDX = 165, USFM_M_XTSEE = 166,
    USFM_M_XTSEEALSO = 167,

    /* Tool-specific */
    USFM_M_ERQ = 168, USFM_M_ERQE = 169, USFM_M_MSE = 170,
    USFM_M_RESTORE = 171, USFM_M_SE = 172,

    /* Drafts (3.1.2 / 3.2 development, not yet released) */
    USFM_M_ILIT = 173, USFM_M_LIST = 174, USFM_M_PL = 175, USFM_M_T = 176,
    USFM_M_TA = 177, USFM_M_TABLE = 178, USFM_M_VID = 179, USFM_M_WL = 180,

    USFM_M_CUSTOM_BASE = 0x8000         /* + stylesheet style index */
};

enum USFM_MarkerClass {
    USFM_CLASS_NONE = 0,
    USFM_CLASS_PARAGRAPH, USFM_CLASS_CHARACTER, USFM_CLASS_NOTE,
    USFM_CLASS_MILESTONE, USFM_CLASS_HEADER, USFM_CLASS_TABLE
};

typedef struct USFM_Stylesheet USFM_Stylesheet;    /* opaque; later milestone */

typedef struct USFM_Config {
    uint8_t version;                    /* enum USFM_Version */
    uint8_t reserved[3];
    const USFM_Stylesheet *custom;      /* ignored in v0.1 */
} USFM_Config;

/* ---------------------------------------------------------------------
 * Diagnostics: problems found in the input. Parsing always continues.
 * ------------------------------------------------------------------- */
enum USFM_DiagnosticSeverity {
    USFM_SEVERITY_NONE = 0,
    USFM_SEVERITY_ERROR,                /* input is invalid USFM */
    USFM_SEVERITY_WARNING,              /* valid but suspicious */
    USFM_SEVERITY_INFO
};

enum USFM_DiagnosticCode {
    USFM_DIAGNOSTIC_NONE = 0,
    USFM_DIAGNOSTIC_UNKNOWN_MARKER = 1,
    USFM_DIAGNOSTIC_UNCLOSED_MARKER = 2,     /* \w without \w* */
    USFM_DIAGNOSTIC_UNEXPECTED_CLOSE = 3,    /* \w* with no open \w */
    USFM_DIAGNOSTIC_INVALID_NESTING = 4,     /* e.g. a cell outside a row */
    USFM_DIAGNOSTIC_INVALID_NUMBER = 5,      /* \v abc, \c 0, \v 5-3 */
    USFM_DIAGNOSTIC_DUPLICATE_NUMBER = 6,
    USFM_DIAGNOSTIC_VERSE_BEFORE_CHAPTER = 7,
    USFM_DIAGNOSTIC_INVALID_BOOK_CODE = 8,   /* \id gen */
    USFM_DIAGNOSTIC_MISSING_CALLER = 9,      /* \f with no caller */
    USFM_DIAGNOSTIC_UNTERMINATED_QUOTE = 10,
    USFM_DIAGNOSTIC_UNKNOWN_ATTRIBUTE = 11,
    USFM_DIAGNOSTIC_MISSING_ATTRIBUTE = 12,
    USFM_DIAGNOSTIC_INVALID_ESCAPE = 13,
    USFM_DIAGNOSTIC_MILESTONE_MISMATCH = 14,
    USFM_DIAGNOSTIC_UNSUPPORTED_VERSION = 15,
    USFM_DIAGNOSTIC_DUPLICATE_STYLE = 16,
    USFM_DIAGNOSTIC_IMPLIED_PARAGRAPH = 17,  /* \v with no paragraph open */
    USFM_DIAGNOSTIC_TOO_DEEP = 18,           /* > USFM_STACK_DEPTH open */
    USFM_DIAGNOSTIC_CODE_COUNT = 19
};

typedef struct USFM_Diagnostic {
    USFM_Span   span;                   /* bytes at fault; length 0 for
                                           "missing" at offset */
    uint16_t    code;                   /* enum USFM_DiagnosticCode */
    USFM_Marker marker;                 /* context marker, or NONE */
} USFM_Diagnostic;

USFM_API uint8_t     USFM_DiagnosticCode_GetSeverity(uint16_t code);
USFM_API const char *USFM_DiagnosticCode_GetMessage(uint16_t code);

/* ---------------------------------------------------------------------
 * Allocator (modeled on yyjson_alc)
 *
 * Every size the library requests is a multiple of 8, and every
 * returned pointer must be 8-byte aligned. Frees are SIZED: dealloc and
 * resize get the exact size last requested. resize returning NULL must
 * leave the old block valid. The library never passes NULL to dealloc.
 * ------------------------------------------------------------------- */
typedef struct USFM_Alc {
    void *(*alloc)(void *ctx, size_t size);
    void *(*resize)(void *ctx, void *ptr, size_t old_size, size_t new_size);
    void  (*dealloc)(void *ctx, void *ptr, size_t size);
    void *ctx;
} USFM_Alc;

/* Releases a block the library allocated and handed out. Pass the
 * logical size; the library applies its rounding. NULL ptr: no-op.
 * alc NULL = the default allocator. */
USFM_API void USFM_Alc_Free(const USFM_Alc *alc, void *ptr, size_t size);

/* ---------------------------------------------------------------------
 * Tokens (internal in v0.1; the type is public because it shares its
 * level_flags packing with USFM_Node)
 * ------------------------------------------------------------------- */
enum USFM_TokenKind {
    USFM_TOK_TEXT = 1,
    USFM_TOK_WHITESPACE,                /* reserved for the CST */
    USFM_TOK_NEWLINE,                   /* reserved for the CST */
    USFM_TOK_MARKER,                    /* \p  \v  \w  \+w  \qt-s */
    USFM_TOK_MARKER_END,                /* \w*  \+w*  \* */
    USFM_TOK_ARGUMENT,                  /* "1-2a" after \v, "GEN" after
                                           \id, "+" caller after \f */
    USFM_TOK_ATTRIBUTES,                /* "|lemma="x" ..." */
    USFM_TOK_OPTBREAK                   /* "//" */
};

/* level_flags: low 4 bits = number (\q2 -> 2), high 4 bits = flags.
 * Marker flags: */
enum {
    USFM_TF_NESTED = 0x10,              /* \+w */
    USFM_TF_MILESTONE_START = 0x20,     /* \qt-s */
    USFM_TF_MILESTONE_END = 0x40,       /* \qt-e */
    USFM_TF_SELF_CLOSED = 0x80          /* milestone ended with \* */
};
/* Text flags (TEXT tokens and TEXT nodes only): */
enum {
    USFM_TF_HAS_ESCAPE = 0x10,          /* contains \\ \/ \| \~ \" */
    USFM_TF_HAS_NBSP = 0x20,            /* contains ~ */
    USFM_TF_HAS_LOOSE_WS = 0x40,        /* contains CR, LF or TAB */
    USFM_TEXT_FLAGS_COOK = 0x70
};
/* Node flags (PARA nodes only): */
enum {
    USFM_NF_IMPLIED = 0x10,             /* inserted by the parser */
    USFM_NF_HEADING = 0x20              /* \s \ms \mt \r ... */
};

#define USFM_LEVEL(level_flags) ((uint8_t)((level_flags) & 0x0Fu))
#define USFM_FLAGS(level_flags) ((uint8_t)((level_flags) & 0xF0u))

typedef struct USFM_Token {
    USFM_Span   span;                   /* bytes in the input */
    USFM_Marker marker;                 /* USFM_M_NONE for non-markers */
    uint8_t     kind;                   /* enum USFM_TokenKind */
    uint8_t     level_flags;
} USFM_Token;

/* ---------------------------------------------------------------------
 * AST (semantic, USX/USJ-shaped)
 *
 * Nodes are stored in preorder. A node's descendants are the
 * contiguous range (i, end). So:
 *     first child  = i + 1            (if i + 1 < node.end)
 *     next sibling = nodes[child].end (if < parent.end)
 *     skip subtree = node.end
 *
 * Paragraph markers are containers that close implicitly at the next
 * paragraph-level marker. Chapters and verses are milestones (as in
 * USX 3); each has its number as an ARGUMENT child. An ARGUMENT is
 * always a node's first child; ATTRIBUTES appear where they were
 * written (last child of a char span, after the ARGUMENT of \periph).
 * ------------------------------------------------------------------- */
typedef struct USFM_Node {
    uint32_t    parent;                 /* USFM_NIL for the root */
    uint32_t    end;                    /* one past last descendant */
    USFM_Span   span;                   /* source bytes covered */
    USFM_Marker marker;
    uint8_t     kind;                   /* enum USFM_AstKind */
    uint8_t     level_flags;            /* see the flag enums above */
} USFM_Node;

USFM_API uint8_t USFM_Node_GetLevel(const USFM_Node *node);
USFM_API uint8_t USFM_Node_GetFlags(const USFM_Node *node);
USFM_API int     USFM_Node_HasFlags(const USFM_Node *node, uint32_t flags);

enum USFM_AstKind {
    USFM_AST_BOOK = 1, USFM_AST_HEADER,     /* \id, \h, \toc ... */
    USFM_AST_CHAPTER, USFM_AST_VERSE,       /* milestones */
    USFM_AST_PARA, USFM_AST_CHAR, USFM_AST_NOTE,
    USFM_AST_MILESTONE, USFM_AST_FIGURE,
    USFM_AST_TABLE, USFM_AST_ROW, USFM_AST_CELL,
    USFM_AST_SIDEBAR, USFM_AST_PERIPHERAL,
    USFM_AST_TEXT, USFM_AST_OPTBREAK,
    USFM_AST_ARGUMENT,                      /* \v 3-5a, \f +, \id GEN */
    USFM_AST_ATTRIBUTES                     /* |lemma="grace", raw */
};

typedef struct USFM_Ast USFM_Ast;       /* opaque */

enum {
    USFM_PARSE_DEFAULT = 0x00,
    USFM_PARSE_COPY_INPUT = 0x01        /* handle owns a copy of input */
};

typedef struct USFM_VerseRef {
    uint16_t chapter;
    uint16_t verse_first;               /* \v 3-5a -> 3 */
    uint16_t verse_last;                /*         -> 5 */
    uint8_t  segment_first;             /* 'a' -> 1, none -> 0 */
    uint8_t  segment_last;
} USFM_VerseRef;

typedef struct USFM_VerseEntry {
    USFM_VerseRef ref;
    uint32_t node;                      /* the VERSE node */
    uint32_t end_node;                  /* next VERSE/CHAPTER, or end */
} USFM_VerseEntry;

/* One per \c in document order. A chapter's verses are
 * verses[first_verse .. first_verse + verse_count). Verses before the
 * first \c belong to no chapter. */
typedef struct USFM_ChapterEntry {
    uint16_t number;                    /* \c 3 -> 3; 0 if invalid */
    uint16_t reserved;
    uint32_t node;                      /* the CHAPTER node */
    uint32_t end_node;                  /* next CHAPTER, or end */
    uint32_t first_verse;
    uint32_t verse_count;
} USFM_ChapterEntry;

USFM_API USFM_Ast *USFM_Ast_Parse(const char *input, uint32_t input_length,
                                  const USFM_Config *config, USFM_Err *err);
USFM_API USFM_Ast *USFM_Ast_ParseOpts(const char *input, uint32_t input_length,
                                      const USFM_Config *config, uint32_t flags,
                                      const USFM_Alc *alc, USFM_Err *err);
USFM_API void      USFM_Ast_Free(USFM_Ast *ast);             /* NULL: no-op */

USFM_API const char *USFM_Ast_GetInput(const USFM_Ast *ast, uint32_t *length);
USFM_API const USFM_Diagnostic *USFM_Ast_GetDiagnostics(const USFM_Ast *ast,
                                                        uint32_t *count);
USFM_API uint32_t USFM_Ast_GetDroppedDiagnosticCount(const USFM_Ast *ast);
USFM_API const USFM_Node *USFM_Ast_GetNodes(const USFM_Ast *ast, uint32_t *count);
USFM_API const USFM_ChapterEntry *USFM_Ast_GetChapters(const USFM_Ast *ast,
                                                       uint32_t *count);
USFM_API const USFM_VerseEntry *USFM_Ast_GetVerses(const USFM_Ast *ast,
                                                   uint32_t *count);
/* Highest chapter number present (not the number of chapters). */
USFM_API uint32_t    USFM_Ast_GetMaxChapterNumber(const USFM_Ast *ast);
/* "GEN"; NUL-terminated, never NULL ("" if there is no \id). */
USFM_API const char *USFM_Ast_GetBookCode(const USFM_Ast *ast);
/* Resolved enum USFM_Version: never AUTO after parsing. */
USFM_API uint8_t     USFM_Ast_GetVersion(const USFM_Ast *ast);

/* Return the node index, or USFM_NIL when absent (*out untouched). A
 * repeated chapter number finds the first occurrence. FindVerse finds
 * the entry whose range contains `verse` (\v 3-5 contains 4). */
USFM_API uint32_t USFM_Ast_FindChapter(const USFM_Ast *ast, uint16_t chapter,
                                       USFM_ChapterEntry *out);
USFM_API uint32_t USFM_Ast_FindVerse(const USFM_Ast *ast, uint16_t chapter,
                                     uint16_t verse, USFM_VerseEntry *out);

/* ---------------------------------------------------------------------
 * Iterators (no callbacks, no allocation)
 * ------------------------------------------------------------------- */
enum {
    USFM_VI_INCLUDE_NOTES = 0x01,       /* descend into NOTE subtrees */
    USFM_VI_INCLUDE_HEADINGS = 0x02,    /* descend into heading paras */
    USFM_VI_REPORT_BREAKS = 0x04,       /* yield USFM_VITEM_PARA_BREAK */
    USFM_VI_REPORT_NOTES = 0x08         /* yield USFM_VITEM_NOTE */
};

enum USFM_VerseItemKind {
    USFM_VITEM_TEXT = 1,                /* text: raw TEXT span */
    USFM_VITEM_PARA_BREAK,              /* node: the new PARA */
    USFM_VITEM_NOTE,                    /* node: the NOTE */
    USFM_VITEM_OPTBREAK                 /* "//" */
};

typedef struct USFM_VerseItem {
    uint8_t   kind;                     /* enum USFM_VerseItemKind */
    uint8_t   in_note;                  /* 1 if inside a NOTE subtree */
    uint16_t  reserved;
    uint32_t  node;                     /* node yielded */
    uint32_t  para;                     /* enclosing PARA, or USFM_NIL */
    uint32_t  style;                    /* innermost CHAR, or USFM_NIL */
    USFM_Span text;                     /* raw span (TEXT, OPTBREAK) */
} USFM_VerseItem;

typedef struct USFM_VerseIter {
    const USFM_Ast *ast;
    uint32_t        position;           /* private */
    uint32_t        end;                /* private */
    uint32_t        flags;
} USFM_VerseIter;

/* Yields the verse's content in reading order, crossing paragraphs.
 * Defaults: TEXT only; skips notes and heading paragraphs. */
USFM_API void USFM_VerseIter_Init(USFM_VerseIter *it, const USFM_Ast *ast,
                                  const USFM_VerseEntry *entry, uint32_t flags);
USFM_API int  USFM_VerseIter_Next(USFM_VerseIter *it, USFM_VerseItem *item);

typedef struct USFM_ChapterIter {
    const USFM_Ast *ast;
    uint32_t        position;           /* private */
    uint32_t        end;                /* private */
} USFM_ChapterIter;

USFM_API void USFM_ChapterIter_Init(USFM_ChapterIter *it, const USFM_Ast *ast);
USFM_API int  USFM_ChapterIter_Next(USFM_ChapterIter *it,
                                    USFM_ChapterEntry *chapter);

typedef struct USFM_ChapterVerseIter {
    const USFM_Ast *ast;
    uint32_t        position;           /* private */
    uint32_t        end;                /* private */
} USFM_ChapterVerseIter;

USFM_API void USFM_ChapterVerseIter_Init(USFM_ChapterVerseIter *it,
                                         const USFM_Ast *ast,
                                         const USFM_ChapterEntry *chapter);
USFM_API int  USFM_ChapterVerseIter_Next(USFM_ChapterVerseIter *it,
                                         USFM_VerseEntry *verse);

/* ---------------------------------------------------------------------
 * ABI guarantees. C99 has no _Static_assert; a negative array size
 * fails the build instead.
 * ------------------------------------------------------------------- */
#define USFM_STATIC_ASSERT(name, cond) \
    typedef char usfm_static_assert_##name[(cond) ? 1 : -1]
USFM_STATIC_ASSERT(span_size,       sizeof(USFM_Span) == 8);
USFM_STATIC_ASSERT(token_size,      sizeof(USFM_Token) == 12);
USFM_STATIC_ASSERT(node_size,       sizeof(USFM_Node) == 20);
USFM_STATIC_ASSERT(diagnostic_size, sizeof(USFM_Diagnostic) == 12);
USFM_STATIC_ASSERT(chapter_size,    sizeof(USFM_ChapterEntry) == 20);
USFM_STATIC_ASSERT(verse_size,      sizeof(USFM_VerseEntry) == 16);

#ifdef __cplusplus
}
#endif
#endif /* USFM_H */

#ifdef USFM_IMPLEMENTATION
#ifndef USFM_IMPLEMENTATION_INCLUDED
#define USFM_IMPLEMENTATION_INCLUDED
/* ---------------------------------------------------------------------
 * Internal: NOT public API.
 *
 * This first part is compiled even under USFM_CODEGEN_BOOTSTRAP,
 * because codegen/codegen.c needs it to generate the tables below.
 * ------------------------------------------------------------------- */
#include <string.h>
#ifndef USFM_NO_LIBC
#include <stdlib.h>
#endif

/* What a marker does to the tree. Decided per row of the marker table,
 * not per marker id: \k is CHAR but \k1 is PARA. */
enum USFM_Role {
    USFM_ROLE_PARA = 1,                 /* \p \q \s: paragraph container */
    USFM_ROLE_HEADER,                   /* \ide \h \toc \usfm \sts */
    USFM_ROLE_ID,                       /* \id */
    USFM_ROLE_CHAPTER,                  /* \c */
    USFM_ROLE_VERSE,                    /* \v */
    USFM_ROLE_CHAR,                     /* \nd \w \add, closed by \X* */
    USFM_ROLE_NOTE,                     /* \f \x \fe \ef \ex \efe */
    USFM_ROLE_NOTE_PART,                /* \fr \ft \xo: closed implicitly */
    USFM_ROLE_ROW,                      /* \tr */
    USFM_ROLE_CELL,                     /* \tc# \th# ... */
    USFM_ROLE_MILESTONE,                /* \ts; also any -s/-e marker */
    USFM_ROLE_SIDEBAR,                  /* \esb */
    USFM_ROLE_SIDEBAR_END,              /* \esbe */
    USFM_ROLE_PERIPH,                   /* \periph */
    USFM_ROLE_FIGURE                    /* \fig */
};

/* Marker properties (USFM_MarkerRow.props). */
enum {
    USFM_MP_ARG_WORD = 0x01,            /* next word is an ARGUMENT */
    USFM_MP_ARG_LINE = 0x02,            /* rest of line up to | is one */
    USFM_MP_HEADING = 0x04              /* heading paragraph */
};

typedef struct USFM_MarkerRow {
    const char *name;
    uint8_t     name_length;
    uint8_t     role;                   /* enum USFM_Role */
    uint8_t     level;                  /* fixed level of a full-name
                                           key such as k1, else 0 */
    uint8_t     props;                  /* USFM_MP_* */
    USFM_Marker marker;
} USFM_MarkerRow;

#define USFM_ROW_NONE 0xFFFFu

/* Byte classes (USFM_ByteClass, generated). */
enum {
    USFM_BC_STOP = 0x01,                /* ends a fast text run:
                                           \ | / ~ CR LF TAB */
    USFM_BC_NAME = 0x02,                /* marker name: A-Z a-z 0-9 - */
    USFM_BC_ALPHA = 0x04,
    USFM_BC_DIGIT = 0x08,
    USFM_BC_SPACE = 0x10,               /* space, tab */
    USFM_BC_NEWLINE = 0x20,             /* CR, LF */
    USFM_BC_ESCAPE = 0x40               /* may follow \ as an escape:
                                           \ / | ~ " */
};

/* Perfect-hash input: first two and last two bytes plus the length.
 * Bytes past the end of a short name are never read. */
static uint32_t usfm__marker_hash(const char *name, uint32_t length)
{
    const uint8_t *s = (const uint8_t *)name;
    uint32_t b0, b1, b2, b3;
    USFM_ASSERT(name != NULL && length > 0);
    b0 = s[0];
    b1 = length > 1 ? s[1] : 0u;
    b2 = length > 2 ? s[length - 2] : 0u;
    b3 = length > 1 ? s[length - 1] : 0u;
    return (b0 | (b1 << 8) | (b2 << 16) | (b3 << 24)) ^ (length * 0x9E3779B1u);
}

/* Multiply-shift reduction: any slot_count, no division. */
static uint32_t usfm__marker_slot(uint32_t hash, uint32_t seed, uint32_t slot_count)
{
    uint32_t mixed = hash * seed;       /* wraps mod 2^32 on purpose */
    return (uint32_t)(((uint64_t)mixed * slot_count) >> 32);
}

#ifndef USFM_CODEGEN_BOOTSTRAP
/* ---------------------------------------------------------------------
 * Generated tables (codegen/codegen.c; do not edit by hand)
 * ------------------------------------------------------------------- */
// USFM_CODEGEN character_class start
// USFM_CODEGEN character_class end

// USFM_CODEGEN marker_table start
// USFM_CODEGEN marker_table end

/* ===== Allocator ===== */

/* ===== Errors and diagnostics ===== */

/* ===== Marker lookup ===== */

/* ===== Scanner ===== */

/* ===== AST handle ===== */

/* ===== Parser ===== */

/* ===== Lookups ===== */

/* ===== Iterators ===== */

#endif /* USFM_CODEGEN_BOOTSTRAP */
#endif /* USFM_IMPLEMENTATION_INCLUDED */
#endif /* USFM_IMPLEMENTATION */
```

The four `// USFM_CODEGEN` lines are the only lines in the file that start with `//`. Codegen requires each one to start at column 0 and end with `\n`.

- [ ] **Step 10: Run the unit tests (the C++ check comes later)**

Run: `gcc -std=c99 -Wall -Wextra -pedantic -fshort-enums -O0 -g -fsanitize=address,undefined tests/run_tests.c -o build/run_tests && ./build/run_tests`

(Run `mkdir -p build` first.)

Expected:
- `ok: 8 checks`.
- Warnings that `usfm__marker_slot` is defined but not used are fine. It is used from Task 2 on.

Don't run the full `./build.sh test` yet:
- The C++ check calls `USFM_Ast_Parse`, which doesn't exist until Task 5. `-fsyntax-only` only needs declarations, so it would pass, but there's no reason to run it before Task 5.
- `run_codegen` needs the codegen changes from Task 2.

- [ ] **Step 11: Commit**

```bash
git add usfm.h main.c build.sh .gitignore tests/test.h tests/run_tests.c tests/test_basics.c tests/cpp_check.cpp
git commit -m "feat: public AST API skeleton and test harness

Replace the old tokenizer header with the v0.1 public API from
usfm_claude_api.h (AST subset), the internal role/row/hash definitions
and empty codegen blocks. Add a dependency-free test runner and a
C++ compile check.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task 2: Codegen — byte classes, marker table, perfect hash

**Files:**
- Create: `codegen/markers.h`
- Modify: `codegen/codegen.c`
- Modify: `codegen/build.sh` (`-O0` → `-O2`, because the seed search is CPU-bound)
- Modify: `usfm.h` (Marker lookup section; the generated blocks are filled in by running codegen)
- Create: `tests/test_markers.c`
- Modify: `tests/run_tests.c`

- [ ] **Step 1: Write the failing test `tests/test_markers.c`**

```c
static void test_every_row_finds_itself(void)
{
    size_t count = sizeof USFM_MarkerRows / sizeof USFM_MarkerRows[0];
    for (size_t i = 0; i < count; i++) {
        const USFM_MarkerRow *row = &USFM_MarkerRows[i];
        CHECK_EQ(row->name_length, strlen(row->name));
        CHECK_EQ(usfm__marker_find(row->name, row->name_length), i);
    }
}

static void test_unknown_names_miss(void)
{
    CHECK_EQ(usfm__marker_find("zzz", 3), USFM_ROW_NONE);
    CHECK_EQ(usfm__marker_find("q2", 2), USFM_ROW_NONE);      /* levels are not keys */
    CHECK_EQ(usfm__marker_find("qt-s", 4), USFM_ROW_NONE);    /* suffixes are not keys */
    CHECK_EQ(usfm__marker_find("P", 1), USFM_ROW_NONE);       /* case-sensitive */
    CHECK_EQ(usfm__marker_find("", 0), USFM_ROW_NONE);
    CHECK_EQ(usfm__marker_find("xtSeeAlsoX", 10), USFM_ROW_NONE);
}

static void test_name_max_is_longest_key(void)
{
    size_t count = sizeof USFM_MarkerRows / sizeof USFM_MarkerRows[0];
    size_t longest = 0;
    for (size_t i = 0; i < count; i++) {
        if (USFM_MarkerRows[i].name_length > longest) longest = USFM_MarkerRows[i].name_length;
    }
    CHECK_EQ(USFM_MARKER_NAME_MAX, longest);
}

static void test_slots_are_collision_free(void)
{
    size_t count = sizeof USFM_MarkerRows / sizeof USFM_MarkerRows[0];
    size_t used = 0;
    CHECK(USFM_MARKER_SLOT_COUNT >= count);
    for (size_t s = 0; s < USFM_MARKER_SLOT_COUNT; s++) {
        if (USFM_MarkerSlots[s] != 0) used++;
    }
    CHECK_EQ(used, count);
}

static void test_k1_is_a_paragraph(void)
{
    uint32_t k = usfm__marker_find("k", 1);
    uint32_t k1 = usfm__marker_find("k1", 2);
    CHECK(k != USFM_ROW_NONE && k1 != USFM_ROW_NONE);
    CHECK_EQ(USFM_MarkerRows[k].role, USFM_ROLE_CHAR);
    CHECK_EQ(USFM_MarkerRows[k1].role, USFM_ROLE_PARA);
    CHECK_EQ(USFM_MarkerRows[k1].marker, USFM_M_K);
    CHECK_EQ(USFM_MarkerRows[k1].level, 1);
}

static void test_byte_classes(void)
{
    CHECK(USFM_ByteClass['\\'] & USFM_BC_STOP);
    CHECK(USFM_ByteClass['\\'] & USFM_BC_ESCAPE);
    CHECK(!(USFM_ByteClass['a'] & USFM_BC_STOP));
    CHECK(USFM_ByteClass['-'] & USFM_BC_NAME);
    CHECK(!(USFM_ByteClass['-'] & USFM_BC_ALPHA));
    CHECK(USFM_ByteClass['7'] & USFM_BC_DIGIT);
    CHECK(USFM_ByteClass['\t'] & USFM_BC_SPACE);
    CHECK(USFM_ByteClass['\r'] & USFM_BC_NEWLINE);
    CHECK(!(USFM_ByteClass[' '] & USFM_BC_STOP));
}

static void run_markers_tests(void)
{
    RUN(test_every_row_finds_itself);
    RUN(test_unknown_names_miss);
    RUN(test_name_max_is_longest_key);
    RUN(test_slots_are_collision_free);
    RUN(test_k1_is_a_paragraph);
    RUN(test_byte_classes);
}
```

In `tests/run_tests.c`, add `#include "test_markers.c"` after `#include "test_basics.c"`. In `main`, after `run_basics_tests();`, add:

```c
    fprintf(stderr, "markers\n");
    run_markers_tests();
```

- [ ] **Step 2: Run it to verify it fails**

Run: `mkdir -p build && gcc -std=c99 -Wall -Wextra -pedantic -fshort-enums -O0 -g -fsanitize=address,undefined tests/run_tests.c -o build/run_tests`
Expected: compile errors: `USFM_MarkerRows` undeclared, `usfm__marker_find` undeclared.

- [ ] **Step 3: Write the marker table `codegen/markers.h`**

```c
#ifndef CODEGEN_MARKERS_H
#define CODEGEN_MARKERS_H
/* The standard USFM marker table: one row per hash key.
 *
 * Keys are base names (q, tc, qt), plus the few full names whose
 * meaning differs from their base: k1/k2 are paragraphs while k is a
 * character style, and the tool-specific s1e..s4e / ms2e / ms3e put the
 * number before a trailing letter. The scanner parses levels, -s/-e,
 * + and * separately, so they are never keys.
 *
 * Roles and properties follow docs/usfm-field-guide.md (sections 2 and
 * 5); marker ids are the USFM_M_* values in usfm.h.
 *
 * TODO(sty): generate this table by parsing the official usfm.sty of
 * every supported version (2.4, 2.5, 3.0, 3.0.11, 3.1) instead of
 * maintaining it by hand. The generated output in usfm.h keeps the same
 * shape, so the parser does not change.
 *
 * Each row must start with four spaces and `{ "` -- the fuzz dictionary
 * is generated from these lines (see build.sh). */

typedef struct CodeGen_MarkerDef {
    const char *name;
    const char *marker;                 /* USFM_M_* */
    const char *role;                   /* USFM_ROLE_* */
    unsigned    level;                  /* fixed level of a full-name key */
    const char *props;                  /* USFM_MP_* expression or "0" */
} CodeGen_MarkerDef;

#define M_(id) "USFM_M_" #id
#define R_PARA "USFM_ROLE_PARA"
#define R_HEADER "USFM_ROLE_HEADER"
#define R_ID "USFM_ROLE_ID"
#define R_CHAPTER "USFM_ROLE_CHAPTER"
#define R_VERSE "USFM_ROLE_VERSE"
#define R_CHAR "USFM_ROLE_CHAR"
#define R_NOTE "USFM_ROLE_NOTE"
#define R_PART "USFM_ROLE_NOTE_PART"
#define R_ROW "USFM_ROLE_ROW"
#define R_CELL "USFM_ROLE_CELL"
#define R_MILESTONE "USFM_ROLE_MILESTONE"
#define R_SIDEBAR "USFM_ROLE_SIDEBAR"
#define R_SIDEBAR_END "USFM_ROLE_SIDEBAR_END"
#define R_PERIPH "USFM_ROLE_PERIPH"
#define R_FIGURE "USFM_ROLE_FIGURE"
#define P_NONE "0"
#define P_ARGW "USFM_MP_ARG_WORD"
#define P_ARGL "USFM_MP_ARG_LINE"
#define P_HEAD "USFM_MP_HEADING"

static const CodeGen_MarkerDef CodeGen_Markers[] = {
    /* Identification and book headers */
    { "id", M_(ID), R_ID, 0, P_ARGW },
    { "usfm", M_(USFM), R_HEADER, 0, P_ARGW },
    { "ide", M_(IDE), R_HEADER, 0, P_NONE },
    { "h", M_(H), R_HEADER, 0, P_NONE },
    { "toc", M_(TOC), R_HEADER, 0, P_NONE },
    { "toca", M_(TOCA), R_HEADER, 0, P_NONE },
    { "sts", M_(STS), R_HEADER, 0, P_NONE },
    /* Chapters and verses */
    { "c", M_(C), R_CHAPTER, 0, P_ARGW },
    { "ca", M_(CA), R_CHAR, 0, P_NONE },
    { "cp", M_(CP), R_PARA, 0, P_NONE },
    { "v", M_(V), R_VERSE, 0, P_ARGW },
    { "va", M_(VA), R_CHAR, 0, P_NONE },
    { "vp", M_(VP), R_CHAR, 0, P_NONE },
    /* Paragraphs */
    { "b", M_(B), R_PARA, 0, P_NONE },
    { "cd", M_(CD), R_PARA, 0, P_HEAD },
    { "cl", M_(CL), R_PARA, 0, P_HEAD },
    { "cls", M_(CLS), R_PARA, 0, P_NONE },
    { "d", M_(D), R_PARA, 0, P_NONE },
    { "ib", M_(IB), R_PARA, 0, P_NONE },
    { "ie", M_(IE), R_PARA, 0, P_NONE },
    { "iex", M_(IEX), R_PARA, 0, P_NONE },
    { "ili", M_(ILI), R_PARA, 0, P_NONE },
    { "im", M_(IM), R_PARA, 0, P_NONE },
    { "imi", M_(IMI), R_PARA, 0, P_NONE },
    { "imq", M_(IMQ), R_PARA, 0, P_NONE },
    { "imt", M_(IMT), R_PARA, 0, P_HEAD },
    { "imte", M_(IMTE), R_PARA, 0, P_HEAD },
    { "io", M_(IO), R_PARA, 0, P_NONE },
    { "iot", M_(IOT), R_PARA, 0, P_HEAD },
    { "ip", M_(IP), R_PARA, 0, P_NONE },
    { "ipc", M_(IPC), R_PARA, 0, P_NONE },
    { "ipi", M_(IPI), R_PARA, 0, P_NONE },
    { "ipq", M_(IPQ), R_PARA, 0, P_NONE },
    { "ipr", M_(IPR), R_PARA, 0, P_NONE },
    { "iq", M_(IQ), R_PARA, 0, P_NONE },
    { "is", M_(IS), R_PARA, 0, P_HEAD },
    { "k1", M_(K), R_PARA, 1, P_NONE },
    { "k2", M_(K), R_PARA, 2, P_NONE },
    { "lf", M_(LF), R_PARA, 0, P_NONE },
    { "lh", M_(LH), R_PARA, 0, P_NONE },
    { "li", M_(LI), R_PARA, 0, P_NONE },
    { "lim", M_(LIM), R_PARA, 0, P_NONE },
    { "lit", M_(LIT), R_PARA, 0, P_NONE },
    { "m", M_(M), R_PARA, 0, P_NONE },
    { "mi", M_(MI), R_PARA, 0, P_NONE },
    { "mr", M_(MR), R_PARA, 0, P_HEAD },
    { "ms", M_(MS), R_PARA, 0, P_HEAD },
    { "mt", M_(MT), R_PARA, 0, P_HEAD },
    { "mte", M_(MTE), R_PARA, 0, P_HEAD },
    { "nb", M_(NB), R_PARA, 0, P_NONE },
    { "p", M_(P), R_PARA, 0, P_NONE },
    { "pb", M_(PB), R_PARA, 0, P_NONE },
    { "pc", M_(PC), R_PARA, 0, P_NONE },
    { "pi", M_(PI), R_PARA, 0, P_NONE },
    { "pm", M_(PM), R_PARA, 0, P_NONE },
    { "pmc", M_(PMC), R_PARA, 0, P_NONE },
    { "pmo", M_(PMO), R_PARA, 0, P_NONE },
    { "pmr", M_(PMR), R_PARA, 0, P_NONE },
    { "po", M_(PO), R_PARA, 0, P_NONE },
    { "pr", M_(PR), R_PARA, 0, P_NONE },
    { "q", M_(Q), R_PARA, 0, P_NONE },
    { "qa", M_(QA), R_PARA, 0, P_HEAD },
    { "qc", M_(QC), R_PARA, 0, P_NONE },
    { "qd", M_(QD), R_PARA, 0, P_NONE },
    { "qm", M_(QM), R_PARA, 0, P_NONE },
    { "qr", M_(QR), R_PARA, 0, P_NONE },
    { "r", M_(R), R_PARA, 0, P_HEAD },
    { "rem", M_(REM), R_PARA, 0, P_NONE },
    { "s", M_(S), R_PARA, 0, P_HEAD },
    { "sd", M_(SD), R_PARA, 0, P_HEAD },
    { "sp", M_(SP), R_PARA, 0, P_HEAD },
    { "sr", M_(SR), R_PARA, 0, P_HEAD },
    { "tr", M_(TR), R_ROW, 0, P_NONE },
    /* Character styles */
    { "add", M_(ADD), R_CHAR, 0, P_NONE },
    { "bd", M_(BD), R_CHAR, 0, P_NONE },
    { "bdit", M_(BDIT), R_CHAR, 0, P_NONE },
    { "bk", M_(BK), R_CHAR, 0, P_NONE },
    { "dc", M_(DC), R_CHAR, 0, P_NONE },
    { "em", M_(EM), R_CHAR, 0, P_NONE },
    { "fm", M_(FM), R_CHAR, 0, P_NONE },
    { "fv", M_(FV), R_CHAR, 0, P_NONE },
    { "ior", M_(IOR), R_CHAR, 0, P_NONE },
    { "iqt", M_(IQT), R_CHAR, 0, P_NONE },
    { "it", M_(IT), R_CHAR, 0, P_NONE },
    { "jmp", M_(JMP), R_CHAR, 0, P_NONE },
    { "k", M_(K), R_CHAR, 0, P_NONE },
    { "lik", M_(LIK), R_CHAR, 0, P_NONE },
    { "litl", M_(LITL), R_CHAR, 0, P_NONE },
    { "liv", M_(LIV), R_CHAR, 0, P_NONE },
    { "nd", M_(ND), R_CHAR, 0, P_NONE },
    { "no", M_(NO), R_CHAR, 0, P_NONE },
    { "ord", M_(ORD), R_CHAR, 0, P_NONE },
    { "pn", M_(PN), R_CHAR, 0, P_NONE },
    { "png", M_(PNG), R_CHAR, 0, P_NONE },
    { "qac", M_(QAC), R_CHAR, 0, P_NONE },
    { "qs", M_(QS), R_CHAR, 0, P_NONE },
    { "qt", M_(QT), R_CHAR, 0, P_NONE },
    { "rb", M_(RB), R_CHAR, 0, P_NONE },
    { "ref", M_(REF), R_CHAR, 0, P_NONE },
    { "rq", M_(RQ), R_CHAR, 0, P_NONE },
    { "sc", M_(SC), R_CHAR, 0, P_NONE },
    { "sig", M_(SIG), R_CHAR, 0, P_NONE },
    { "sls", M_(SLS), R_CHAR, 0, P_NONE },
    { "sup", M_(SUP), R_CHAR, 0, P_NONE },
    { "tl", M_(TL), R_CHAR, 0, P_NONE },
    { "w", M_(W), R_CHAR, 0, P_NONE },
    { "wa", M_(WA), R_CHAR, 0, P_NONE },
    { "wg", M_(WG), R_CHAR, 0, P_NONE },
    { "wh", M_(WH), R_CHAR, 0, P_NONE },
    { "wj", M_(WJ), R_CHAR, 0, P_NONE },
    /* Notes */
    { "ef", M_(EF), R_NOTE, 0, P_ARGW },
    { "efe", M_(EFE), R_NOTE, 0, P_ARGW },
    { "ex", M_(EX), R_NOTE, 0, P_ARGW },
    { "f", M_(F), R_NOTE, 0, P_ARGW },
    { "fe", M_(FE), R_NOTE, 0, P_ARGW },
    { "x", M_(X), R_NOTE, 0, P_ARGW },
    /* Note content */
    { "fk", M_(FK), R_PART, 0, P_NONE },
    { "fl", M_(FL), R_PART, 0, P_NONE },
    { "fp", M_(FP), R_PART, 0, P_NONE },
    { "fq", M_(FQ), R_PART, 0, P_NONE },
    { "fqa", M_(FQA), R_PART, 0, P_NONE },
    { "fr", M_(FR), R_PART, 0, P_NONE },
    { "ft", M_(FT), R_PART, 0, P_NONE },
    { "fw", M_(FW), R_PART, 0, P_NONE },
    { "xk", M_(XK), R_PART, 0, P_NONE },
    { "xnt", M_(XNT), R_PART, 0, P_NONE },
    { "xo", M_(XO), R_PART, 0, P_NONE },
    { "xop", M_(XOP), R_PART, 0, P_NONE },
    { "xot", M_(XOT), R_PART, 0, P_NONE },
    { "xq", M_(XQ), R_PART, 0, P_NONE },
    { "xt", M_(XT), R_PART, 0, P_NONE },
    { "xta", M_(XTA), R_PART, 0, P_NONE },
    /* Table cells */
    { "tc", M_(TC), R_CELL, 0, P_NONE },
    { "tcc", M_(TCC), R_CELL, 0, P_NONE },
    { "tcr", M_(TCR), R_CELL, 0, P_NONE },
    { "th", M_(TH), R_CELL, 0, P_NONE },
    { "thc", M_(THC), R_CELL, 0, P_NONE },
    { "thr", M_(THR), R_CELL, 0, P_NONE },
    /* Milestones */
    { "ts", M_(TS), R_MILESTONE, 0, P_NONE },
    /* Figures, peripherals, sidebars, categories */
    { "cat", M_(CAT), R_CHAR, 0, P_NONE },
    { "esb", M_(ESB), R_SIDEBAR, 0, P_NONE },
    { "esbe", M_(ESBE), R_SIDEBAR_END, 0, P_NONE },
    { "fig", M_(FIG), R_FIGURE, 0, P_NONE },
    { "periph", M_(PERIPH), R_PERIPH, 0, P_ARGL },
    /* Deprecated */
    { "addpn", M_(ADDPN), R_CHAR, 0, P_NONE },
    { "fdc", M_(FDC), R_PART, 0, P_NONE },
    { "fs", M_(FS), R_PART, 0, P_NONE },
    { "ph", M_(PH), R_PARA, 0, P_NONE },
    { "phi", M_(PHI), R_PARA, 0, P_NONE },
    { "pro", M_(PRO), R_CHAR, 0, P_NONE },
    { "xdc", M_(XDC), R_PART, 0, P_NONE },
    /* Obsolete */
    { "conc", M_(CONC), R_PARA, 0, P_NONE },
    { "cov", M_(COV), R_PARA, 0, P_NONE },
    { "glo", M_(GLO), R_PARA, 0, P_NONE },
    { "idx", M_(IDX), R_PARA, 0, P_NONE },
    { "intro", M_(INTRO), R_PARA, 0, P_NONE },
    { "maps", M_(MAPS), R_PARA, 0, P_NONE },
    { "pref", M_(PREF), R_PARA, 0, P_NONE },
    { "ps", M_(PS), R_PARA, 0, P_NONE },
    { "psi", M_(PSI), R_PARA, 0, P_NONE },
    { "pub", M_(PUB), R_PARA, 0, P_NONE },
    { "pubinfo", M_(PUBINFO), R_PARA, 0, P_NONE },
    { "spine", M_(SPINE), R_PARA, 0, P_NONE },
    { "wr", M_(WR), R_CHAR, 0, P_NONE },
    /* Stylesheet-only */
    { "efm", M_(EFM), R_PART, 0, P_NONE },
    { "ndx", M_(NDX), R_CHAR, 0, P_NONE },
    { "xtSee", M_(XTSEE), R_PART, 0, P_NONE },
    { "xtSeeAlso", M_(XTSEEALSO), R_PART, 0, P_NONE },
    /* Tool-specific */
    { "erq", M_(ERQ), R_PARA, 0, P_NONE },
    { "erqe", M_(ERQE), R_PARA, 0, P_NONE },
    { "mse", M_(MSE), R_PARA, 0, P_HEAD },
    { "ms2e", M_(MSE), R_PARA, 2, P_HEAD },
    { "ms3e", M_(MSE), R_PARA, 3, P_HEAD },
    { "restore", M_(RESTORE), R_PARA, 0, P_NONE },
    { "s1e", M_(SE), R_PARA, 1, P_HEAD },
    { "s2e", M_(SE), R_PARA, 2, P_HEAD },
    { "s3e", M_(SE), R_PARA, 3, P_HEAD },
    { "s4e", M_(SE), R_PARA, 4, P_HEAD },
    /* Drafts (3.1.2 / 3.2) */
    { "ilit", M_(ILIT), R_PARA, 0, P_NONE },
    { "list", M_(LIST), R_MILESTONE, 0, P_NONE },
    { "pl", M_(PL), R_PARA, 0, P_NONE },
    { "t", M_(T), R_MILESTONE, 0, P_NONE },
    { "ta", M_(TA), R_CHAR, 0, P_NONE },
    { "table", M_(TABLE), R_MILESTONE, 0, P_NONE },
    { "vid", M_(VID), R_MILESTONE, 0, P_NONE },
    { "wl", M_(WL), R_CHAR, 0, P_NONE },
};

#endif
```

- [ ] **Step 4: Rework `codegen/codegen.c`**

Make these changes in this order.

**4a. Replace the includes at the top.** Replace from line 1 through `#include <unistd.h>` with:

```c
#define USFM_IMPLEMENTATION
#define USFM_CODEGEN_BOOTSTRAP
#define _POSIX_C_SOURCE 200809L
#include "../usfm.h"
#include "markers.h"

#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
```

`USFM_CODEGEN_BOOTSTRAP` compiles only the part of `usfm.h` before the generated blocks: the role enum, the byte-class bits and the hash functions. So a stale or empty generated block can never break the build that regenerates it.

Delete `#define CODEGEN_MARKER_TEXT_CAPACITY 200`.

**4b. Add two helpers after `CodeGen_WriteBuffer_Append`:**

```c
void CodeGen_WriteBuffer_Appendf(CodeGen_WriteBuffer *buffer, const char *format, ...)
{
    char line[512];
    va_list args;
    va_start(args, format);
    int length = vsnprintf(line, sizeof line, format, args);
    va_end(args);
    assert(length >= 0 && (size_t)length < sizeof line);
    CodeGen_WriteBuffer_Append(buffer, line, (size_t)length);
}

/* Writes `content` (with a fresh generation comment) unless its hash
 * matches the block already in the file. Frees content. Returns true
 * when the existing block is unchanged and must be copied as-is. */
bool CodeGen_Block_Finish(CodeGen_WriteBuffer *buffer, CodeGen_WriteBuffer *content,
    uint64_t current_hash, const char *name)
{
    uint64_t hash = CodeGen_ContentHash_Calculate(content->buffer, 0, content->length);
    bool unchanged = hash == current_hash;
    if (!unchanged)
    {
        printf("Writing new version of %s\n", name);
        CodeGen_GenerationComments_Serialize(buffer, hash);
        CodeGen_WriteBuffer_Append(buffer, content->buffer, content->length);
    }
    free(content->buffer);
    return unchanged;
}

CodeGen_WriteBuffer CodeGen_WriteBuffer_Temp(size_t capacity)
{
    CodeGen_WriteBuffer temp = {
        .buffer = malloc(capacity),
        .length = 0,
        .capacity = capacity,
    };
    assert(temp.buffer != NULL);
    return temp;
}
```

`CodeGen_Block_Finish` calls `CodeGen_ContentHash_Calculate` and `CodeGen_GenerationComments_Serialize`, which are defined further down. Add forward declarations right after the `CodeGen_WriteBuffer` typedef:

```c
uint64_t CodeGen_ContentHash_Calculate(char *input, size_t start, size_t end);
void CodeGen_GenerationComments_Serialize(CodeGen_WriteBuffer *buffer, uint64_t content_hash);
```

**4c. Delete these functions entirely:**
- `CodeGen_USFM_CharacterClass_Serialize`
- `CodeGen_USFM_MarkerType_Parse`
- `CodeGen_USFM_MarkerText_Serialize`

**4d. Add the byte-class generator and serializer** where `CodeGen_USFM_CharacterClass_Serialize` was:

```c
void CodeGen_ByteClass_Generate(uint8_t classes[256])
{
    memset(classes, 0, 256);
    const char *stops = "\\|/~\r\n\t";
    for (size_t i = 0; stops[i] != '\0'; i++)
    {
        classes[(uint8_t)stops[i]] |= USFM_BC_STOP;
    }
    for (int c = 'A'; c <= 'Z'; c++)
    {
        classes[c] |= USFM_BC_NAME | USFM_BC_ALPHA;
    }
    for (int c = 'a'; c <= 'z'; c++)
    {
        classes[c] |= USFM_BC_NAME | USFM_BC_ALPHA;
    }
    for (int c = '0'; c <= '9'; c++)
    {
        classes[c] |= USFM_BC_NAME | USFM_BC_DIGIT;
    }
    classes['-'] |= USFM_BC_NAME;
    classes[' '] |= USFM_BC_SPACE;
    classes['\t'] |= USFM_BC_SPACE;
    classes['\r'] |= USFM_BC_NEWLINE;
    classes['\n'] |= USFM_BC_NEWLINE;
    const char *escapes = "\\/|~\"";
    for (size_t i = 0; escapes[i] != '\0'; i++)
    {
        classes[(uint8_t)escapes[i]] |= USFM_BC_ESCAPE;
    }
}

bool CodeGen_ByteClass_Serialize(const uint8_t classes[256], CodeGen_WriteBuffer *buffer,
    uint64_t current_hash)
{
    CodeGen_WriteBuffer temp = CodeGen_WriteBuffer_Temp(buffer->capacity);
    CodeGen_WriteBuffer_Appendf(&temp, "static const uint8_t USFM_ByteClass[256] = {\n");
    for (size_t i = 0; i < 256; i += 16)
    {
        CodeGen_WriteBuffer_Appendf(&temp, "   ");
        for (size_t j = i; j < i + 16; j++)
        {
            CodeGen_WriteBuffer_Appendf(&temp, " 0x%02x,", classes[j]);
        }
        CodeGen_WriteBuffer_Appendf(&temp, "\n");
    }
    CodeGen_WriteBuffer_Appendf(&temp, "};\n");
    return CodeGen_Block_Finish(buffer, &temp, current_hash, "character_class");
}
```

The table is written as a full 256-entry list, not with `[i] = x` designators. Array designators are not valid C++, and `usfm.h` must compile as C++.

**4e. Add the perfect-hash search and the marker-table serializer** after the byte-class functions:

```c
/* Seeds tried per table size, and the largest size tried (as a multiple
 * of the key count). Both bound the search so codegen always ends. */
#define CODEGEN_HASH_SEED_LIMIT 65536u
#define CODEGEN_HASH_SIZE_FACTOR 64u

typedef struct {
    uint32_t seed;
    uint32_t slot_count;
    size_t name_max;
    uint16_t *slots;        /* slot -> row index + 1; 0 = empty */
} CodeGen_PerfectHash;

/* Finds the smallest slot count (and a seed) that puts every key in
 * its own slot. Prints an error and returns false if the bounds are hit
 * or two keys have identical hash input. */
bool CodeGen_PerfectHash_Search(const CodeGen_MarkerDef *defs, size_t count,
    CodeGen_PerfectHash *out)
{
    assert(count > 0 && count < 0xFFFF);
    uint32_t *hashes = malloc(count * sizeof *hashes);
    assert(hashes != NULL);
    size_t name_max = 0;
    for (size_t i = 0; i < count; i++)
    {
        size_t length = strlen(defs[i].name);
        if (length == 0 || length > 255)
        {
            printf("CodeGen Error: marker name \"%s\" must be 1..255 bytes\n", defs[i].name);
            free(hashes);
            return false;
        }
        hashes[i] = usfm__marker_hash(defs[i].name, (uint32_t)length);
        if (length > name_max)
        {
            name_max = length;
        }
        for (size_t j = 0; j < i; j++)
        {
            if (strcmp(defs[i].name, defs[j].name) == 0)
            {
                printf("CodeGen Error: marker \"%s\" is listed twice\n", defs[i].name);
                free(hashes);
                return false;
            }
            if (hashes[i] == hashes[j])
            {
                printf("CodeGen Error: \"%s\" and \"%s\" have identical hash input; "
                    "extend usfm__marker_hash\n", defs[i].name, defs[j].name);
                free(hashes);
                return false;
            }
        }
    }

    uint32_t max_size = (uint32_t)count * CODEGEN_HASH_SIZE_FACTOR;
    uint32_t *stamp = calloc(max_size, sizeof *stamp);
    assert(stamp != NULL);
    uint32_t generation = 0;
    for (uint32_t size = (uint32_t)count; size <= max_size; size++)
    {
        for (uint32_t k = 0; k < CODEGEN_HASH_SEED_LIMIT; k++)
        {
            uint32_t seed = (k + 1u) * 0x9E3779B1u;
            generation++;
            bool collision = false;
            for (size_t i = 0; i < count; i++)
            {
                uint32_t slot = usfm__marker_slot(hashes[i], seed, size);
                if (stamp[slot] == generation)
                {
                    collision = true;
                    break;
                }
                stamp[slot] = generation;
            }
            if (!collision)
            {
                out->seed = seed;
                out->slot_count = size;
                out->name_max = name_max;
                out->slots = calloc(size, sizeof *out->slots);
                assert(out->slots != NULL);
                for (size_t i = 0; i < count; i++)
                {
                    out->slots[usfm__marker_slot(hashes[i], seed, size)] = (uint16_t)(i + 1);
                }
                printf("Perfect hash: %zu keys in %u slots (seed 0x%08x)\n", count, size, seed);
                free(stamp);
                free(hashes);
                return true;
            }
        }
    }
    printf("CodeGen Error: no collision-free hash for %zu markers with up to %u slots "
        "and %u seeds per size\n", count, max_size, CODEGEN_HASH_SEED_LIMIT);
    free(stamp);
    free(hashes);
    return false;
}

bool CodeGen_MarkerTable_Serialize(CodeGen_WriteBuffer *buffer, uint64_t current_hash)
{
    size_t count = sizeof CodeGen_Markers / sizeof CodeGen_Markers[0];
    CodeGen_PerfectHash hash = {0};
    if (!CodeGen_PerfectHash_Search(CodeGen_Markers, count, &hash))
    {
        exit(1);
    }
    CodeGen_WriteBuffer temp = CodeGen_WriteBuffer_Temp(buffer->capacity);
    CodeGen_WriteBuffer_Appendf(&temp, "#define USFM_MARKER_NAME_MAX %zuu\n", hash.name_max);
    CodeGen_WriteBuffer_Appendf(&temp, "#define USFM_MARKER_HASH_SEED 0x%08xu\n", hash.seed);
    CodeGen_WriteBuffer_Appendf(&temp, "#define USFM_MARKER_SLOT_COUNT %uu\n", hash.slot_count);
    CodeGen_WriteBuffer_Appendf(&temp, "static const USFM_MarkerRow USFM_MarkerRows[%zu] = {\n", count);
    for (size_t i = 0; i < count; i++)
    {
        const CodeGen_MarkerDef *def = &CodeGen_Markers[i];
        CodeGen_WriteBuffer_Appendf(&temp, "    { \"%s\", %zu, %s, %u, %s, %s },\n",
            def->name, strlen(def->name), def->role, def->level, def->props, def->marker);
    }
    CodeGen_WriteBuffer_Appendf(&temp, "};\n");
    CodeGen_WriteBuffer_Appendf(&temp, "static const %s USFM_MarkerSlots[%u] = {\n",
        count < 255 ? "uint8_t" : "uint16_t", hash.slot_count);
    for (uint32_t i = 0; i < hash.slot_count; i += 16)
    {
        CodeGen_WriteBuffer_Appendf(&temp, "   ");
        for (uint32_t j = i; j < i + 16 && j < hash.slot_count; j++)
        {
            CodeGen_WriteBuffer_Appendf(&temp, " %u,", hash.slots[j]);
        }
        CodeGen_WriteBuffer_Appendf(&temp, "\n");
    }
    CodeGen_WriteBuffer_Appendf(&temp, "};\n");
    free(hash.slots);
    return CodeGen_Block_Finish(buffer, &temp, current_hash, "marker_table");
}
```

**4f. Update `main`.**

Replace its first two lines:

```c
    uint8_t character_class[256] = {0};
    USFM_CharacterClass_Generate((uint8_t *)&character_class);
```

with:

```c
    uint8_t byte_class[256];
    CodeGen_ByteClass_Generate(byte_class);
```

Replace the block from `const char *mt = "marker_type";` through the end of the loop that calls `CodeGen_CommentBlock_GetHash` with:

```c
    for (size_t i = 0; i < block_length; i++)
    {
        if (!blocks[i].is_data)
        {
            CodeGen_CommentBlock_GetHash(readbuf, &blocks[i]);
        }
    }
```

Inside the `for (size_t i = 0; i < block_length; i++)` write loop, replace the whole `else { const char *cc = ...; ... }` branch with:

```c
        else
        {
            const char *cc = "character_class";
            const char *mt = "marker_table";
            bool unchanged = true;
            if (block.name_length == strlen(cc) && memcmp(block.name, cc, strlen(cc)) == 0)
            {
                unchanged = CodeGen_ByteClass_Serialize(byte_class, &writebuf, block.content_hash);
            }
            else if (block.name_length == strlen(mt) && memcmp(block.name, mt, strlen(mt)) == 0)
            {
                unchanged = CodeGen_MarkerTable_Serialize(&writebuf, block.content_hash);
            }
            else
            {
                printf("CodeGen Error: unknown codegen block \"%.*s\"\n",
                    (int)block.name_length, block.name);
                exit(1);
            }
            if (unchanged)
            {
                should_copy_content = true;
            }
            else
            {
                content_changed = true;
            }
        }
```

Delete the cleanup loop at the end that frees `marker_texts` and `free(marker_texts);`.

**4g. Update `codegen/build.sh`.** Change `-O0` to `-O2`. The seed search runs about 10⁹ probes; `-O0` with sanitizers makes that take minutes.

- [ ] **Step 5: Add the lookup to `usfm.h`**

Under `/* ===== Marker lookup ===== */`, insert:

```c
/* Returns the USFM_MarkerRows index of an exact key, or USFM_ROW_NONE. */
static uint32_t usfm__marker_find(const char *name, uint32_t length)
{
    uint32_t slot, index;
    const USFM_MarkerRow *row;
    if (length == 0 || length > USFM_MARKER_NAME_MAX) return USFM_ROW_NONE;
    slot = usfm__marker_slot(usfm__marker_hash(name, length),
                             USFM_MARKER_HASH_SEED, USFM_MARKER_SLOT_COUNT);
    index = USFM_MarkerSlots[slot];
    if (index == 0) return USFM_ROW_NONE;
    row = &USFM_MarkerRows[index - 1];
    if (row->name_length != length || memcmp(row->name, name, length) != 0) {
        return USFM_ROW_NONE;
    }
    return index - 1;
}
```

- [ ] **Step 6: Run codegen and the tests**

Run: `./build.sh codegen`
Expected:
- `Perfect hash: 186 keys in N slots (seed 0x...)`, with N somewhere around 1,200–1,800.
- `Writing new version of character_class` and `Writing new version of marker_table`.

Run: `mkdir -p build && gcc -std=c99 -Wall -Wextra -pedantic -fshort-enums -O0 -g -fsanitize=address,undefined tests/run_tests.c -o build/run_tests && ./build/run_tests`
Expected: `ok: N checks`, where N is about 400. That's two checks per row plus the rest.

Then run `./build.sh codegen` again.
Expected: `No content changed!`, meaning the output is deterministic.

- [ ] **Step 7: Commit**

```bash
git add codegen/markers.h codegen/codegen.c codegen/build.sh usfm.h tests/test_markers.c tests/run_tests.c
git commit -m "feat(codegen): marker table with minimal perfect hash

codegen/markers.h holds the hand-written marker table (TODO: generate
from usfm.sty). codegen searches for the smallest collision-free slot
count, bounded by a seed limit and a size limit, and fails loudly if
none is found. Also generates the byte-class table as a C++-safe list.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Allocator, errors and diagnostic metadata

**Files:**
- Modify: `usfm.h` (Allocator section; Errors and diagnostics section)
- Create: `tests/test_alloc.c`
- Modify: `tests/run_tests.c`

- [ ] **Step 1: Write the failing test `tests/test_alloc.c`**

```c
/* A counting allocator that checks the allocator contract and can be
 * told to fail after a number of successful calls. Shared by later
 * tests. */
typedef struct TestAlc {
    size_t   live_bytes;
    uint32_t calls_left;                /* UINT32_MAX = never fail */
    uint32_t bad_sizes;                 /* sizes that were not multiples of 8 */
} TestAlc;

static void *test_alloc(void *ctx, size_t size)
{
    TestAlc *t = (TestAlc *)ctx;
    void *p;
    if (size % 8 != 0) t->bad_sizes++;
    if (t->calls_left == 0) return NULL;
    if (t->calls_left != UINT32_MAX) t->calls_left--;
    p = malloc(size);
    if (p) t->live_bytes += size;
    return p;
}

static void *test_resize(void *ctx, void *ptr, size_t old_size, size_t new_size)
{
    TestAlc *t = (TestAlc *)ctx;
    void *p;
    if (old_size % 8 != 0 || new_size % 8 != 0) t->bad_sizes++;
    if (t->calls_left == 0) return NULL;
    if (t->calls_left != UINT32_MAX) t->calls_left--;
    p = realloc(ptr, new_size);
    if (p) t->live_bytes = t->live_bytes - old_size + new_size;
    return p;
}

static void test_dealloc(void *ctx, void *ptr, size_t size)
{
    TestAlc *t = (TestAlc *)ctx;
    if (size % 8 != 0) t->bad_sizes++;
    t->live_bytes -= size;
    free(ptr);
}

static USFM_Alc test_alc(TestAlc *state, uint32_t calls_left)
{
    USFM_Alc alc;
    memset(state, 0, sizeof *state);
    state->calls_left = calls_left;
    alc.alloc = test_alloc;
    alc.resize = test_resize;
    alc.dealloc = test_dealloc;
    alc.ctx = state;
    return alc;
}

static void test_round8(void)
{
    CHECK_EQ(usfm__round8(0), 8);
    CHECK_EQ(usfm__round8(1), 8);
    CHECK_EQ(usfm__round8(8), 8);
    CHECK_EQ(usfm__round8(20), 24);
}

static void test_grow_doubles_and_keeps_old_block_on_failure(void)
{
    TestAlc state;
    USFM_Alc alc = test_alc(&state, UINT32_MAX);
    uint32_t capacity = 0;
    void *block = usfm__grow(&alc, NULL, &capacity, 20, 1);
    CHECK(block != NULL);
    CHECK_EQ(capacity, 8);
    block = usfm__grow(&alc, block, &capacity, 20, 9);
    CHECK_EQ(capacity, 16);
    state.calls_left = 0;
    CHECK(usfm__grow(&alc, block, &capacity, 20, 17) == NULL);
    CHECK_EQ(capacity, 16);                     /* unchanged on failure */
    usfm__free(&alc, block, (size_t)capacity * 20);
    CHECK_EQ(state.live_bytes, 0);
    CHECK_EQ(state.bad_sizes, 0);
}

static void test_alc_free_null_is_noop(void)
{
    USFM_Alc_Free(NULL, NULL, 10);
    CHECK(1);
}

static void test_diagnostic_metadata(void)
{
    for (uint16_t code = 1; code < USFM_DIAGNOSTIC_CODE_COUNT; code++) {
        CHECK(USFM_DiagnosticCode_GetMessage(code)[0] != '\0');
        CHECK(USFM_DiagnosticCode_GetSeverity(code) != USFM_SEVERITY_NONE);
    }
    CHECK_EQ(USFM_DiagnosticCode_GetSeverity(USFM_DIAGNOSTIC_IMPLIED_PARAGRAPH), USFM_SEVERITY_WARNING);
    CHECK_EQ(USFM_DiagnosticCode_GetSeverity(USFM_DIAGNOSTIC_UNCLOSED_MARKER), USFM_SEVERITY_ERROR);
    CHECK_STR(USFM_DiagnosticCode_GetMessage(999), "unknown diagnostic");
    CHECK_EQ(USFM_DiagnosticCode_GetSeverity(999), USFM_SEVERITY_NONE);
}

static void run_alloc_tests(void)
{
    RUN(test_round8);
    RUN(test_grow_doubles_and_keeps_old_block_on_failure);
    RUN(test_alc_free_null_is_noop);
    RUN(test_diagnostic_metadata);
}
```

In `tests/run_tests.c`, add `#include <stdlib.h>` near the top (the test allocator uses `malloc`). Add `#include "test_alloc.c"` after `test_markers.c`, and in `main`:

```c
    fprintf(stderr, "alloc\n");
    run_alloc_tests();
```

- [ ] **Step 2: Run to verify it fails**

Run: `gcc -std=c99 -Wall -Wextra -pedantic -fshort-enums -O0 -g -fsanitize=address,undefined tests/run_tests.c -o build/run_tests`
Expected: errors: `usfm__round8`, `usfm__grow`, `usfm__free` undeclared.

- [ ] **Step 3: Implement the Allocator section**

Under `/* ===== Allocator ===== */`:

```c
/* Every request is rounded to a multiple of 8 (allocator contract). */
static size_t usfm__round8(size_t size)
{
    if (size == 0) size = 1;
    return (size + 7u) & ~(size_t)7u;
}

#ifndef USFM_NO_LIBC
static void *usfm__libc_alloc(void *ctx, size_t size)
{
    (void)ctx;
    return malloc(size);
}
static void *usfm__libc_resize(void *ctx, void *ptr, size_t old_size, size_t new_size)
{
    (void)ctx;
    (void)old_size;
    return realloc(ptr, new_size);
}
static void usfm__libc_dealloc(void *ctx, void *ptr, size_t size)
{
    (void)ctx;
    (void)size;
    free(ptr);
}
static const USFM_Alc usfm__libc_alc = {
    usfm__libc_alloc, usfm__libc_resize, usfm__libc_dealloc, NULL
};
#endif

/* The allocator to use, or NULL when none was given and libc is off. */
static const USFM_Alc *usfm__alc_or_default(const USFM_Alc *alc)
{
    if (alc != NULL) return alc;
#ifndef USFM_NO_LIBC
    return &usfm__libc_alc;
#else
    return NULL;
#endif
}

static void *usfm__alloc(const USFM_Alc *alc, size_t size)
{
    return alc->alloc(alc->ctx, usfm__round8(size));
}

static void usfm__free(const USFM_Alc *alc, void *ptr, size_t size)
{
    if (ptr != NULL) alc->dealloc(alc->ctx, ptr, usfm__round8(size));
}

/* Makes room for `needed` elements of elem_size, doubling *capacity.
 * Returns the (possibly moved) array, or NULL on failure; on failure
 * the old block stays valid and *capacity is unchanged. */
static void *usfm__grow(const USFM_Alc *alc, void *array, uint32_t *capacity,
                        uint32_t elem_size, uint32_t needed)
{
    uint64_t new_capacity;
    void *block;
    uint32_t i;
    if (needed <= *capacity) return array;
    new_capacity = *capacity ? (uint64_t)*capacity * 2u : 8u;
    /* Bounded: each pass doubles a 64-bit value toward a 32-bit target. */
    for (i = 0; i < 32 && new_capacity < needed; i++) new_capacity *= 2u;
    if (new_capacity < needed || new_capacity * elem_size > 0xFFFFFFFFull) return NULL;
    if (array == NULL) {
        block = usfm__alloc(alc, (size_t)(new_capacity * elem_size));
    } else {
        block = alc->resize(alc->ctx, array,
                            usfm__round8((size_t)*capacity * elem_size),
                            usfm__round8((size_t)(new_capacity * elem_size)));
    }
    if (block == NULL) return NULL;
    *capacity = (uint32_t)new_capacity;
    return block;
}

USFM_API void USFM_Alc_Free(const USFM_Alc *alc, void *ptr, size_t size)
{
    const USFM_Alc *a = usfm__alc_or_default(alc);
    if (ptr == NULL || a == NULL) return;
    usfm__free(a, ptr, size);
}
```

- [ ] **Step 4: Implement the Errors and diagnostics section**

Under `/* ===== Errors and diagnostics ===== */`:

```c
static const char *usfm__status_message(uint32_t code)
{
    switch (code) {
    case USFM_OK: return "ok";
    case USFM_ERR_CAPACITY: return "output buffer too small";
    case USFM_ERR_DEPTH: return "nesting too deep";
    case USFM_ERR_INPUT_TOO_LARGE: return "input larger than USFM_INPUT_MAX";
    case USFM_ERR_INVALID_ARG: return "invalid argument";
    case USFM_ERR_NOMEM: return "out of memory";
    case USFM_ERR_NO_ALLOCATOR: return "no allocator (USFM_NO_LIBC)";
    default: return "unknown error";
    }
}

static void usfm__set_err(USFM_Err *err, uint32_t code, uint32_t pos)
{
    if (err == NULL) return;
    err->code = code;
    err->pos = pos;
    err->msg = usfm__status_message(code);
}

static const uint8_t usfm__diag_severity[USFM_DIAGNOSTIC_CODE_COUNT] = {
    USFM_SEVERITY_NONE,                 /* NONE */
    USFM_SEVERITY_WARNING,              /* UNKNOWN_MARKER */
    USFM_SEVERITY_ERROR,                /* UNCLOSED_MARKER */
    USFM_SEVERITY_ERROR,                /* UNEXPECTED_CLOSE */
    USFM_SEVERITY_ERROR,                /* INVALID_NESTING */
    USFM_SEVERITY_ERROR,                /* INVALID_NUMBER */
    USFM_SEVERITY_ERROR,                /* DUPLICATE_NUMBER */
    USFM_SEVERITY_ERROR,                /* VERSE_BEFORE_CHAPTER */
    USFM_SEVERITY_ERROR,                /* INVALID_BOOK_CODE */
    USFM_SEVERITY_ERROR,                /* MISSING_CALLER */
    USFM_SEVERITY_ERROR,                /* UNTERMINATED_QUOTE */
    USFM_SEVERITY_WARNING,              /* UNKNOWN_ATTRIBUTE */
    USFM_SEVERITY_ERROR,                /* MISSING_ATTRIBUTE */
    USFM_SEVERITY_ERROR,                /* INVALID_ESCAPE */
    USFM_SEVERITY_ERROR,                /* MILESTONE_MISMATCH */
    USFM_SEVERITY_WARNING,              /* UNSUPPORTED_VERSION */
    USFM_SEVERITY_INFO,                 /* DUPLICATE_STYLE */
    USFM_SEVERITY_WARNING,              /* IMPLIED_PARAGRAPH */
    USFM_SEVERITY_ERROR                 /* TOO_DEEP */
};

static const char *const usfm__diag_message[USFM_DIAGNOSTIC_CODE_COUNT] = {
    "",
    "unknown marker",
    "unclosed marker",
    "end marker with no open marker",
    "invalid nesting",
    "invalid number",
    "duplicate number",
    "verse before the first chapter",
    "invalid book code",
    "missing note caller",
    "unterminated attribute quote",
    "unknown attribute",
    "missing required attribute",
    "invalid escape",
    "milestone start and end do not match",
    "unsupported USFM version",
    "style defined more than once",
    "implied paragraph",
    "nesting deeper than USFM_STACK_DEPTH"
};

USFM_API uint8_t USFM_DiagnosticCode_GetSeverity(uint16_t code)
{
    return code < USFM_DIAGNOSTIC_CODE_COUNT ? usfm__diag_severity[code]
                                             : (uint8_t)USFM_SEVERITY_NONE;
}

USFM_API const char *USFM_DiagnosticCode_GetMessage(uint16_t code)
{
    return code < USFM_DIAGNOSTIC_CODE_COUNT ? usfm__diag_message[code]
                                             : "unknown diagnostic";
}
```

- [ ] **Step 5: Run the tests**

Run: `gcc -std=c99 -Wall -Wextra -pedantic -fshort-enums -O0 -g -fsanitize=address,undefined tests/run_tests.c -o build/run_tests && ./build/run_tests`
Expected: `ok: N checks` with no failures. Warnings about unused static functions (`usfm__set_err`, `usfm__alc_or_default`) are expected until Task 5.

- [ ] **Step 6: Commit**

```bash
git add usfm.h tests/test_alloc.c tests/run_tests.c
git commit -m "feat: allocator plumbing and diagnostic metadata

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Scanner

**Files:**
- Modify: `usfm.h` (Marker lookup section: `usfm__classify`; Scanner section)
- Create: `tests/dump.h` (token dump now; the tree dump is added in Task 5)
- Create: `tests/test_scanner.c`
- Modify: `tests/run_tests.c`

The scanner returns one token per call and never allocates. Token dump notation used in the tests:

| Notation | Meaning |
| --- | --- |
| `T"…"` | TEXT, optionally followed by `/` and flag letters: `e` escape, `n` `~`, `w` CR/LF/TAB |
| `M:q2` | MARKER, written as marker text: `+nd`, `qt1-s`, `k1`, `?zfoo` for unknown |
| `E:nd` | MARKER_END. `E:*` is a bare `\*` |
| `A"…"` | ARGUMENT |
| `@"…"` | ATTRIBUTES |
| `//` | OPTBREAK |

- [ ] **Step 1: Write `tests/dump.h` (token part)**

```c
#ifndef USFM_DUMP_H
#define USFM_DUMP_H
/* Text dumps of tokens, trees and diagnostics for the tests. Include
 * after usfm.h in a translation unit that defines USFM_IMPLEMENTATION. */
#include <stdio.h>
#include <string.h>

typedef struct DumpOut {
    char  *buf;
    size_t cap;
    size_t len;
} DumpOut;

static void dump_putc(DumpOut *o, char c)
{
    if (o->len + 1 < o->cap) o->buf[o->len++] = c;
    o->buf[o->len] = '\0';
}

static void dump_puts(DumpOut *o, const char *s)
{
    while (*s) dump_putc(o, *s++);
}

static void dump_quoted(DumpOut *o, const char *input, USFM_Span span)
{
    dump_putc(o, '"');
    for (uint32_t i = 0; i < span.length; i++) {
        char c = input[span.offset + i];
        if (c == '\n') dump_puts(o, "\\n");
        else if (c == '\r') dump_puts(o, "\\r");
        else if (c == '\t') dump_puts(o, "\\t");
        else if (c == '"') dump_puts(o, "\\\"");
        else dump_putc(o, c);
    }
    dump_putc(o, '"');
}

/* "q2", "k1", "s1e", "+nd", "qt1-s"; "?zfoo" for an unknown marker
 * (its name is read from the input after the backslash). marker_flags
 * says whether the high bits of level_flags are marker flags (they are
 * paragraph flags on PARA nodes). */
static void dump_marker(DumpOut *o, const char *input, uint32_t input_length,
                        uint32_t backslash, USFM_Marker marker,
                        uint8_t level_flags, int marker_flags)
{
    uint8_t level = USFM_LEVEL(level_flags);
    uint8_t flags = marker_flags ? USFM_FLAGS(level_flags) : 0;
    if (flags & USFM_TF_NESTED) dump_putc(o, '+');
    if (marker == USFM_M_UNKNOWN) {
        uint32_t i = backslash + 1;
        dump_putc(o, '?');
        if (i < input_length && input[i] == '+') i++;
        while (i < input_length && (USFM_ByteClass[(uint8_t)input[i]] & USFM_BC_NAME)) {
            dump_putc(o, input[i++]);
        }
    } else {
        const USFM_MarkerRow *base = NULL, *exact = NULL;
        size_t rows = sizeof USFM_MarkerRows / sizeof USFM_MarkerRows[0];
        for (size_t r = 0; r < rows; r++) {
            if (USFM_MarkerRows[r].marker != marker) continue;
            if (USFM_MarkerRows[r].level == 0 && base == NULL) base = &USFM_MarkerRows[r];
            if (level != 0 && USFM_MarkerRows[r].level == level && exact == NULL) {
                exact = &USFM_MarkerRows[r];
            }
        }
        if (exact != NULL) {
            dump_puts(o, exact->name);
        } else if (base != NULL) {
            dump_puts(o, base->name);
            if (level != 0) {
                char number[8];
                snprintf(number, sizeof number, "%u", (unsigned)level);
                dump_puts(o, number);
            }
        } else {
            dump_putc(o, '?');
        }
    }
    if (flags & USFM_TF_MILESTONE_START) dump_puts(o, "-s");
    if (flags & USFM_TF_MILESTONE_END) dump_puts(o, "-e");
}

/* Scans `input` to the end and writes one space-separated token list. */
static void dump_tokens(const char *input, int pipe_is_attr, char *out, size_t cap)
{
    DumpOut o;
    USFM_Scanner s;
    USFM_ScanToken t;
    uint32_t length = (uint32_t)strlen(input);
    int first = 1;
    o.buf = out;
    o.cap = cap;
    o.len = 0;
    out[0] = '\0';
    usfm__scan_init(&s, input, length);
    s.pipe_is_attr = (uint8_t)pipe_is_attr;
    while (usfm__scan_next(&s, &t)) {
        if (!first) dump_putc(&o, ' ');
        first = 0;
        switch (t.tok.kind) {
        case USFM_TOK_TEXT:
            dump_putc(&o, 'T');
            dump_quoted(&o, input, t.tok.span);
            if (USFM_FLAGS(t.tok.level_flags) != 0) {
                dump_putc(&o, '/');
                if (t.tok.level_flags & USFM_TF_HAS_ESCAPE) dump_putc(&o, 'e');
                if (t.tok.level_flags & USFM_TF_HAS_NBSP) dump_putc(&o, 'n');
                if (t.tok.level_flags & USFM_TF_HAS_LOOSE_WS) dump_putc(&o, 'w');
            }
            break;
        case USFM_TOK_MARKER:
            dump_puts(&o, "M:");
            dump_marker(&o, input, length, t.tok.span.offset, t.tok.marker, t.tok.level_flags, 1);
            break;
        case USFM_TOK_MARKER_END:
            dump_puts(&o, "E:");
            if (t.name_length == 0) dump_putc(&o, '*');
            else dump_marker(&o, input, length, t.tok.span.offset, t.tok.marker, t.tok.level_flags, 1);
            break;
        case USFM_TOK_ARGUMENT:
            dump_putc(&o, 'A');
            dump_quoted(&o, input, t.tok.span);
            break;
        case USFM_TOK_ATTRIBUTES:
            dump_putc(&o, '@');
            dump_quoted(&o, input, t.tok.span);
            break;
        case USFM_TOK_OPTBREAK:
            dump_puts(&o, "//");
            break;
        default:
            dump_puts(&o, "<?>");
            break;
        }
    }
}

#endif
```

- [ ] **Step 2: Write the failing tests `tests/test_scanner.c`**

```c
static char scan_buf[4096];

static const char *scan(const char *input, int pipe_is_attr)
{
    dump_tokens(input, pipe_is_attr, scan_buf, sizeof scan_buf);
    return scan_buf;
}

static void test_scan_id_line(void)
{
    CHECK_STR(scan("\\id GEN Test\n", 0), "M:id A\"GEN\" T\"Test\\n\"/w");
}

static void test_scan_levels(void)
{
    CHECK_STR(scan("\\q2 text", 0), "M:q2 T\"text\"");
    CHECK_STR(scan("\\k1 x", 0), "M:k1 T\"x\"");
    CHECK_STR(scan("\\s1e x", 0), "M:s1e T\"x\"");
    CHECK_STR(scan("\\tc1-2 x", 0), "M:tc1 T\"x\"");
}

static void test_scan_nested_and_closers(void)
{
    CHECK_STR(scan("\\+nd Lord\\+nd*", 0), "M:+nd T\"Lord\" E:+nd");
    CHECK_STR(scan("\\nd x\\nd* y", 0), "M:nd T\"x\" E:nd T\" y\"");
}

static void test_scan_milestone_with_attributes(void)
{
    CHECK_STR(scan("\\qt1-s |who=\"Pilate\"\\*", 1),
              "M:qt1-s @\"|who=\\\"Pilate\\\"\" E:*");
    CHECK_STR(scan("\\ts\\*", 0), "M:ts E:*");
}

static void test_scan_escapes_stay_in_text(void)
{
    CHECK_STR(scan("a\\/\\/b \\\\ c\\|d", 1), "T\"a\\/\\/b \\\\ c\\|d\"/e");
}

static void test_scan_optbreak_and_nbsp(void)
{
    CHECK_STR(scan("a//b~c", 0), "T\"a\" // T\"b~c\"/n");
    CHECK_STR(scan("a/b", 0), "T\"a/b\"");
}

static void test_scan_pipe_is_text_unless_attr(void)
{
    CHECK_STR(scan("a|b", 0), "T\"a|b\"");
    CHECK_STR(scan("a|b", 1), "T\"a\" @\"|b\"");
}

static void test_scan_unknown_and_lone_backslash(void)
{
    CHECK_STR(scan("\\zfoo x", 0), "M:?zfoo T\"x\"");
    CHECK_STR(scan("a\\", 0), "T\"a\" M:?");
}

static void test_scan_arguments(void)
{
    CHECK_STR(scan("\\v 3-5a text", 0), "M:v A\"3-5a\" T\"text\"");
    CHECK_STR(scan("\\c 1\n\\p", 0), "M:c A\"1\" M:p");
    CHECK_STR(scan("\\v\n\\p", 0), "M:v M:p");      /* missing argument */
    CHECK_STR(scan("\\f + \\fr 1.1 \\ft x\\f*", 0),
              "M:f A\"+\" M:fr T\"1.1 \" M:ft T\"x\" E:f");
}

static void test_scan_periph_argument_line(void)
{
    CHECK_STR(scan("\\periph Title Page|id=\"title\"\n\\p x", 1),
              "M:periph A\"Title Page\" @\"|id=\\\"title\\\"\" M:p T\"x\"");
}

static void test_scan_crlf_and_bom(void)
{
    CHECK_STR(scan("\\p\r\ntext", 0), "M:p T\"text\"");
    CHECK_STR(scan("\xEF\xBB\xBF\\id GEN", 0), "M:id A\"GEN\"");
}

static void run_scanner_tests(void)
{
    RUN(test_scan_id_line);
    RUN(test_scan_levels);
    RUN(test_scan_nested_and_closers);
    RUN(test_scan_milestone_with_attributes);
    RUN(test_scan_escapes_stay_in_text);
    RUN(test_scan_optbreak_and_nbsp);
    RUN(test_scan_pipe_is_text_unless_attr);
    RUN(test_scan_unknown_and_lone_backslash);
    RUN(test_scan_arguments);
    RUN(test_scan_periph_argument_line);
    RUN(test_scan_crlf_and_bom);
}
```

In `tests/run_tests.c`, add `#include "dump.h"` after `#include "test.h"`. Add `#include "test_scanner.c"` after `test_alloc.c`, and in `main`:

```c
    fprintf(stderr, "scanner\n");
    run_scanner_tests();
```

- [ ] **Step 3: Run to verify it fails**

Run: `gcc -std=c99 -Wall -Wextra -pedantic -fshort-enums -O0 -g -fsanitize=address,undefined tests/run_tests.c -o build/run_tests`
Expected: errors: unknown type `USFM_Scanner`, `USFM_ScanToken`; `usfm__scan_init` undeclared.

- [ ] **Step 4: Implement the scanner types and `usfm__classify`**

Under `/* ===== Marker lookup ===== */`, after `usfm__marker_find`, add:

```c
/* A token plus what the parser needs about its marker. */
typedef struct USFM_ScanToken {
    USFM_Token tok;
    uint16_t   row;                     /* USFM_MarkerRows index, or
                                           USFM_ROW_NONE */
    uint16_t   reserved;
    uint32_t   name_offset;             /* marker name in the input */
    uint32_t   name_length;             /* 0 for "\*" and a lone "\" */
} USFM_ScanToken;

/* Sets t->row, t->tok.marker and the level / milestone bits of
 * t->tok.level_flags from a marker name. Tries the full name first
 * (k1, s1e), then base letters + level digits + optional -s / -e /
 * -<digits> (qt1-s, tc1-2). Anything else is USFM_M_UNKNOWN. */
static void usfm__classify(const char *name, uint32_t length, USFM_ScanToken *t)
{
    uint32_t row = usfm__marker_find(name, length);
    uint32_t i = 0, base, level = 0;
    uint8_t flags = 0;
    t->row = USFM_ROW_NONE;
    t->tok.marker = USFM_M_UNKNOWN;
    if (row != USFM_ROW_NONE) {
        t->row = (uint16_t)row;
        t->tok.marker = USFM_MarkerRows[row].marker;
        t->tok.level_flags |= USFM_MarkerRows[row].level;
        return;
    }
    while (i < length && (USFM_ByteClass[(uint8_t)name[i]] & USFM_BC_ALPHA)) i++;
    base = i;
    if (base == 0) return;
    while (i < length && (USFM_ByteClass[(uint8_t)name[i]] & USFM_BC_DIGIT)) {
        if (level < 100) level = level * 10u + (uint32_t)(name[i] - '0');
        i++;
    }
    if (i < length && name[i] == '-') {
        if (i + 2 == length && name[i + 1] == 's') {
            flags = USFM_TF_MILESTONE_START;
            i = length;
        } else if (i + 2 == length && name[i + 1] == 'e') {
            flags = USFM_TF_MILESTONE_END;
            i = length;
        } else if (level > 0) {         /* column span: \tc1-2 */
            uint32_t j = i + 1;
            while (j < length && (USFM_ByteClass[(uint8_t)name[j]] & USFM_BC_DIGIT)) j++;
            if (j > i + 1 && j == length) i = length;
        }
    }
    if (i != length) return;
    row = usfm__marker_find(name, base);
    if (row == USFM_ROW_NONE) return;
    t->row = (uint16_t)row;
    t->tok.marker = USFM_MarkerRows[row].marker;
    t->tok.level_flags |= (uint8_t)((level > 15u ? 15u : level) | flags);
}
```

- [ ] **Step 5: Implement the Scanner section**

Under `/* ===== Scanner ===== */`:

```c
enum { USFM_ARG_NONE = 0, USFM_ARG_WORD, USFM_ARG_LINE };

typedef struct USFM_Scanner {
    const char *input;
    uint32_t    length;
    uint32_t    position;
    uint8_t     expect_arg;             /* set after a marker that takes one */
    uint8_t     pipe_is_attr;           /* set by the parser before each call */
    uint16_t    reserved;
} USFM_Scanner;

static void usfm__scan_init(USFM_Scanner *s, const char *input, uint32_t length)
{
    memset(s, 0, sizeof *s);
    s->input = input;
    s->length = length;
    if (length >= 3 && (uint8_t)input[0] == 0xEF && (uint8_t)input[1] == 0xBB &&
        (uint8_t)input[2] == 0xBF) {
        s->position = 3;                /* UTF-8 BOM */
    }
}

static USFM_Span usfm__span(uint32_t offset, uint32_t length)
{
    USFM_Span s;
    s.offset = offset;
    s.length = length;
    return s;
}

static void usfm__tok(USFM_ScanToken *t, uint8_t kind, uint32_t start, uint32_t end)
{
    memset(t, 0, sizeof *t);
    t->tok.span = usfm__span(start, end - start);
    t->tok.kind = kind;
    t->row = USFM_ROW_NONE;
}

/* Consumes the one whitespace character that ends an opening marker or
 * an argument: space, tab, LF, CR or CRLF. */
static uint32_t usfm__skip_one_ws(const char *in, uint32_t len, uint32_t i)
{
    if (i >= len) return i;
    if (in[i] == ' ' || in[i] == '\t' || in[i] == '\n') return i + 1;
    if (in[i] == '\r') return (i + 1 < len && in[i + 1] == '\n') ? i + 2 : i + 1;
    return i;
}

/* in[i] is a backslash: is it an escape (\\ \/ \| \~ \") rather than a
 * marker? */
static int usfm__is_escape(const char *in, uint32_t len, uint32_t i)
{
    return i + 1 < len && (USFM_ByteClass[(uint8_t)in[i + 1]] & USFM_BC_ESCAPE) != 0;
}

static int usfm__scan_marker(USFM_Scanner *s, USFM_ScanToken *t)
{
    const char *in = s->input;
    uint32_t len = s->length;
    uint32_t start = s->position;
    uint32_t i = start + 1, name_start, name_length;
    uint8_t kind = USFM_TOK_MARKER, nested = 0;
    if (i < len && in[i] == '*') {      /* "\*" closes a milestone */
        usfm__tok(t, USFM_TOK_MARKER_END, start, i + 1);
        t->tok.level_flags = USFM_TF_SELF_CLOSED;
        s->position = i + 1;
        return 1;
    }
    if (i < len && in[i] == '+') {
        nested = 1;
        i++;
    }
    name_start = i;
    while (i < len && (USFM_ByteClass[(uint8_t)in[i]] & USFM_BC_NAME)) i++;
    name_length = i - name_start;
    if (name_length > 0 && i < len && in[i] == '*') {
        kind = USFM_TOK_MARKER_END;
        i++;
    } else {
        i = usfm__skip_one_ws(in, len, i);
    }
    usfm__tok(t, kind, start, i);
    t->name_offset = name_start;
    t->name_length = name_length;
    t->tok.level_flags = nested ? USFM_TF_NESTED : 0;
    usfm__classify(in + name_start, name_length, t);
    if (kind == USFM_TOK_MARKER && t->row != USFM_ROW_NONE &&
        !(t->tok.level_flags & (USFM_TF_MILESTONE_START | USFM_TF_MILESTONE_END))) {
        uint8_t props = USFM_MarkerRows[t->row].props;
        if (props & USFM_MP_ARG_WORD) s->expect_arg = USFM_ARG_WORD;
        else if (props & USFM_MP_ARG_LINE) s->expect_arg = USFM_ARG_LINE;
    }
    s->position = i;
    return 1;
}

/* Scans the argument a marker asked for. Returns 0 (consuming nothing)
 * if there is none. */
static int usfm__scan_argument(USFM_Scanner *s, USFM_ScanToken *t, uint8_t mode)
{
    const char *in = s->input;
    uint32_t len = s->length;
    uint32_t i = s->position, start, end;
    while (i < len && (in[i] == ' ' || in[i] == '\t')) i++;
    start = i;
    if (mode == USFM_ARG_WORD) {
        while (i < len && in[i] != '\\' &&
               !(USFM_ByteClass[(uint8_t)in[i]] & (USFM_BC_SPACE | USFM_BC_NEWLINE))) i++;
    } else {
        while (i < len && in[i] != '\\' && in[i] != '|' &&
               !(USFM_ByteClass[(uint8_t)in[i]] & USFM_BC_NEWLINE)) i++;
    }
    end = i;
    if (mode == USFM_ARG_LINE) {
        while (end > start && (in[end - 1] == ' ' || in[end - 1] == '\t')) end--;
    }
    if (end == start) return 0;
    usfm__tok(t, USFM_TOK_ARGUMENT, start, end);
    s->position = mode == USFM_ARG_WORD ? usfm__skip_one_ws(in, len, i) : i;
    return 1;
}

/* Returns 1 and fills *t, or 0 at end of input. Every call that
 * returns 1 consumes at least one byte, so a caller's loop is bounded
 * by the input length. */
static int usfm__scan_next(USFM_Scanner *s, USFM_ScanToken *t)
{
    const char *in = s->input;
    uint32_t len = s->length;
    uint32_t start, i;
    uint8_t flags = 0;
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
        i = start + 1;
        /* Attributes run to the next marker; escapes don't end them. */
        while (i < len) {
            if (in[i] == '\\') {
                if (!usfm__is_escape(in, len, i)) break;
                i += 2;
                continue;
            }
            i++;
        }
        if (i > len) i = len;
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
    /* Text: skip quickly over bytes that are not STOP bytes. */
    i = start;
    while (i < len) {
        uint8_t b = (uint8_t)in[i];
        if (!(USFM_ByteClass[b] & USFM_BC_STOP)) {
            i++;
            continue;
        }
        if (b == '\\') {
            if (!usfm__is_escape(in, len, i)) break;
            flags |= USFM_TF_HAS_ESCAPE;
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
        if (b == '~') flags |= USFM_TF_HAS_NBSP;
        else flags |= USFM_TF_HAS_LOOSE_WS;      /* CR, LF, TAB */
        i++;
    }
    if (i > len) i = len;
    USFM_ASSERT(i > start);
    usfm__tok(t, USFM_TOK_TEXT, start, i);
    t->tok.level_flags = flags;
    s->position = i;
    return 1;
}
```

- [ ] **Step 6: Run the tests**

Run: `gcc -std=c99 -Wall -Wextra -pedantic -fshort-enums -O0 -g -fsanitize=address,undefined tests/run_tests.c -o build/run_tests && ./build/run_tests`
Expected: every scanner check passes, and the summary is `ok: N checks`.

If a scanner expectation fails, the test prints `expected` and `actual`. Fix the scanner, not the expectation, unless the expectation contradicts the table in Step 1.

- [ ] **Step 7: Commit**

```bash
git add usfm.h tests/dump.h tests/test_scanner.c tests/run_tests.c
git commit -m "feat: zero-allocation scanner with marker classification

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: AST handle and parser core (paragraphs, chapters, verses, text)

This task builds the parser loop and the paragraph-level rules. Character spans, notes, tables and the rest arrive in Tasks 6–9. Until then, those markers fall through to a `default` case that keeps them as inline MILESTONE leaves, so the code always compiles and nothing is dropped.

**Files:**
- Modify: `usfm.h` (AST handle section; Parser section; Lookups section, accessors only)
- Create: `tests/check.h`, `tests/helpers.h`, `tests/test_parser.c`
- Modify: `tests/dump.h` (tree and diagnostic dumps)
- Modify: `tests/run_tests.c`

Tree dump notation: one node per line, indented two spaces per level. It shows the `KIND`, then the marker text or the quoted span, then the flag words `implied`, `heading` (PARA) or `closed` (MILESTONE).

- [ ] **Step 1: Write the invariant checker `tests/check.h`**

```c
#ifndef USFM_CHECK_H
#define USFM_CHECK_H
/* check_ast: verifies every structural invariant of a parsed AST.
 * Returns 0 if all hold; otherwise prints the first violation to stderr
 * and returns 1. Used by the unit, corpus and fuzz tests. */
#include <stdio.h>

#define CHECK_AST_FAIL(...) do { \
    fprintf(stderr, "invariant violated: "); \
    fprintf(stderr, __VA_ARGS__); \
    fputc('\n', stderr); \
    return 1; \
} while (0)

static int check_ast(const USFM_Ast *ast)
{
    uint32_t count, input_length, chapter_count, verse_count, diag_count, i;
    const USFM_Node *nodes = USFM_Ast_GetNodes(ast, &count);
    const USFM_ChapterEntry *chapters = USFM_Ast_GetChapters(ast, &chapter_count);
    const USFM_VerseEntry *verses = USFM_Ast_GetVerses(ast, &verse_count);
    const USFM_Diagnostic *diags = USFM_Ast_GetDiagnostics(ast, &diag_count);
    USFM_Ast_GetInput(ast, &input_length);

    if (count == 0) CHECK_AST_FAIL("no nodes");
    if (nodes[0].kind != USFM_AST_BOOK || nodes[0].parent != USFM_NIL || nodes[0].end != count)
        CHECK_AST_FAIL("node 0 is not a BOOK root covering every node");
    for (i = 0; i < count; i++) {
        const USFM_Node *n = &nodes[i];
        uint8_t k = n->kind;
        if ((uint64_t)n->span.offset + n->span.length > input_length)
            CHECK_AST_FAIL("node %u span %u+%u outside input of %u", i,
                           n->span.offset, n->span.length, input_length);
        if (n->end <= i || n->end > count) CHECK_AST_FAIL("node %u end %u", i, n->end);
        if (k < USFM_AST_BOOK || k > USFM_AST_ATTRIBUTES) CHECK_AST_FAIL("node %u kind %u", i, k);
        if ((k == USFM_AST_TEXT || k == USFM_AST_OPTBREAK || k == USFM_AST_ARGUMENT ||
             k == USFM_AST_ATTRIBUTES) && n->end != i + 1)
            CHECK_AST_FAIL("leaf node %u has children", i);
        if (i > 0) {
            uint32_t p = n->parent;
            if (p >= i) CHECK_AST_FAIL("node %u parent %u is not before it", i, p);
            if (i >= nodes[p].end || n->end > nodes[p].end)
                CHECK_AST_FAIL("node %u not inside parent %u", i, p);
        }
    }
    for (i = 0; i < chapter_count; i++) {
        const USFM_ChapterEntry *c = &chapters[i];
        if (c->node >= count || nodes[c->node].kind != USFM_AST_CHAPTER)
            CHECK_AST_FAIL("chapter %u node %u is not a CHAPTER", i, c->node);
        if (c->end_node <= c->node || c->end_node > count)
            CHECK_AST_FAIL("chapter %u end_node %u", i, c->end_node);
        if ((uint64_t)c->first_verse + c->verse_count > verse_count)
            CHECK_AST_FAIL("chapter %u verses out of range", i);
    }
    for (i = 0; i < verse_count; i++) {
        const USFM_VerseEntry *v = &verses[i];
        if (v->node >= count || nodes[v->node].kind != USFM_AST_VERSE)
            CHECK_AST_FAIL("verse %u node %u is not a VERSE", i, v->node);
        if (v->end_node <= v->node || v->end_node > count)
            CHECK_AST_FAIL("verse %u end_node %u", i, v->end_node);
        if (i > 0 && v->node <= verses[i - 1].node)
            CHECK_AST_FAIL("verse %u out of document order", i);
    }
    for (i = 0; i < diag_count; i++) {
        if ((uint64_t)diags[i].span.offset + diags[i].span.length > input_length)
            CHECK_AST_FAIL("diagnostic %u span outside input", i);
        if (diags[i].code == 0 || diags[i].code >= USFM_DIAGNOSTIC_CODE_COUNT)
            CHECK_AST_FAIL("diagnostic %u code %u", i, diags[i].code);
    }
    return 0;
}

#endif
```

- [ ] **Step 2: Add tree and diagnostic dumps to `tests/dump.h`**

Insert before the final `#endif`:

```c
static const char *const dump_kind_names[] = {
    "?", "BOOK", "HEADER", "CHAPTER", "VERSE", "PARA", "CHAR", "NOTE",
    "MILESTONE", "FIGURE", "TABLE", "ROW", "CELL", "SIDEBAR", "PERIPHERAL",
    "TEXT", "OPTBREAK", "ARGUMENT", "ATTRIBUTES"
};

static const char *const dump_diag_names[USFM_DIAGNOSTIC_CODE_COUNT] = {
    "NONE", "UNKNOWN_MARKER", "UNCLOSED_MARKER", "UNEXPECTED_CLOSE",
    "INVALID_NESTING", "INVALID_NUMBER", "DUPLICATE_NUMBER",
    "VERSE_BEFORE_CHAPTER", "INVALID_BOOK_CODE", "MISSING_CALLER",
    "UNTERMINATED_QUOTE", "UNKNOWN_ATTRIBUTE", "MISSING_ATTRIBUTE",
    "INVALID_ESCAPE", "MILESTONE_MISMATCH", "UNSUPPORTED_VERSION",
    "DUPLICATE_STYLE", "IMPLIED_PARAGRAPH", "TOO_DEEP"
};

static void dump_tree(const USFM_Ast *ast, char *out, size_t cap)
{
    DumpOut o;
    uint32_t count, input_length, depth = 0;
    uint32_t ends[USFM_STACK_DEPTH + 8];
    const USFM_Node *nodes = USFM_Ast_GetNodes(ast, &count);
    const char *input = USFM_Ast_GetInput(ast, &input_length);
    o.buf = out;
    o.cap = cap;
    o.len = 0;
    out[0] = '\0';
    for (uint32_t i = 0; i < count; i++) {
        const USFM_Node *n = &nodes[i];
        while (depth > 0 && i >= ends[depth - 1]) depth--;
        for (uint32_t d = 0; d < depth; d++) dump_puts(&o, "  ");
        dump_puts(&o, n->kind <= USFM_AST_ATTRIBUTES ? dump_kind_names[n->kind] : "?");
        if (n->kind == USFM_AST_TEXT || n->kind == USFM_AST_ARGUMENT ||
            n->kind == USFM_AST_ATTRIBUTES) {
            dump_putc(&o, ' ');
            dump_quoted(&o, input, n->span);
        } else if (n->marker != USFM_M_NONE) {
            dump_putc(&o, ' ');
            dump_marker(&o, input, input_length, n->span.offset, n->marker, n->level_flags,
                        n->kind == USFM_AST_CHAR || n->kind == USFM_AST_MILESTONE);
        }
        if (n->kind == USFM_AST_PARA && (n->level_flags & USFM_NF_IMPLIED)) dump_puts(&o, " implied");
        if (n->kind == USFM_AST_PARA && (n->level_flags & USFM_NF_HEADING)) dump_puts(&o, " heading");
        if (n->kind == USFM_AST_MILESTONE && (n->level_flags & USFM_TF_SELF_CLOSED)) dump_puts(&o, " closed");
        dump_putc(&o, '\n');
        if (depth < sizeof ends / sizeof ends[0]) ends[depth++] = n->end;
    }
}

static void dump_diags(const USFM_Ast *ast, char *out, size_t cap)
{
    DumpOut o;
    uint32_t count;
    const USFM_Diagnostic *diags = USFM_Ast_GetDiagnostics(ast, &count);
    o.buf = out;
    o.cap = cap;
    o.len = 0;
    out[0] = '\0';
    for (uint32_t i = 0; i < count; i++) {
        char number[16];
        dump_puts(&o, diags[i].code < USFM_DIAGNOSTIC_CODE_COUNT ? dump_diag_names[diags[i].code] : "?");
        snprintf(number, sizeof number, " %u\n", (unsigned)diags[i].span.offset);
        dump_puts(&o, number);
    }
}
```

- [ ] **Step 3: Write `tests/helpers.h`**

```c
#ifndef USFM_HELPERS_H
#define USFM_HELPERS_H
/* Parse-and-compare helpers. Every parse is also run through
 * check_ast(), so each tree test is an invariant test too. */

static char helper_buf[16384];

static USFM_Ast *parse_with_version(const char *usfm, uint8_t version)
{
    USFM_Config config;
    USFM_Err err;
    memset(&config, 0, sizeof config);
    config.version = version;
    return USFM_Ast_Parse(usfm, (uint32_t)strlen(usfm), &config, &err);
}

static void check_tree_at(const char *file, int line, const char *usfm, const char *expected)
{
    USFM_Ast *ast = parse_with_version(usfm, USFM_VERSION_AUTO);
    usfm_test_checks++;
    if (ast == NULL) {
        usfm_test_failures++;
        fprintf(stderr, "%s:%d: parse returned NULL\n", file, line);
        return;
    }
    if (check_ast(ast) != 0) {
        usfm_test_failures++;
        fprintf(stderr, "%s:%d: invariants failed for: %s\n", file, line, usfm);
    }
    dump_tree(ast, helper_buf, sizeof helper_buf);
    if (strcmp(helper_buf, expected) != 0) {
        usfm_test_failures++;
        fprintf(stderr, "%s:%d: tree mismatch for: %s\n--- expected\n%s--- actual\n%s",
                file, line, usfm, expected, helper_buf);
    }
    USFM_Ast_Free(ast);
}

static void check_diags_at(const char *file, int line, const char *usfm, uint8_t version,
                           const char *expected)
{
    USFM_Ast *ast = parse_with_version(usfm, version);
    usfm_test_checks++;
    if (ast == NULL) {
        usfm_test_failures++;
        fprintf(stderr, "%s:%d: parse returned NULL\n", file, line);
        return;
    }
    dump_diags(ast, helper_buf, sizeof helper_buf);
    if (strcmp(helper_buf, expected) != 0) {
        usfm_test_failures++;
        fprintf(stderr, "%s:%d: diagnostics mismatch for: %s\n--- expected\n%s--- actual\n%s",
                file, line, usfm, expected, helper_buf);
    }
    USFM_Ast_Free(ast);
}

#define CHECK_TREE(usfm, expected) check_tree_at(__FILE__, __LINE__, (usfm), (expected))
#define CHECK_DIAGS(usfm, version, expected) \
    check_diags_at(__FILE__, __LINE__, (usfm), (version), (expected))

#endif
```

- [ ] **Step 4: Write the failing tests `tests/test_parser.c`**

```c
static void test_parse_empty_input(void)
{
    CHECK_TREE("", "BOOK\n");
    CHECK_TREE("\n\n", "BOOK\n");
}

static void test_parse_basic_book(void)
{
    CHECK_TREE("\\id GEN Test\n\\c 1\n\\p\n\\v 1 In the beginning.\n\\v 2 Next.\n",
        "BOOK\n"
        "  HEADER id\n"
        "    ARGUMENT \"GEN\"\n"
        "    TEXT \"Test\"\n"
        "  CHAPTER c\n"
        "    ARGUMENT \"1\"\n"
        "  PARA p\n"
        "    VERSE v\n"
        "      ARGUMENT \"1\"\n"
        "    TEXT \"In the beginning.\"\n"
        "    VERSE v\n"
        "      ARGUMENT \"2\"\n"
        "    TEXT \"Next.\"\n");
}

static void test_parse_poetry_and_headings(void)
{
    CHECK_TREE("\\c 1\n\\s1 Title\n\\q1 \\v 1 Line one\n\\q2 line two\n",
        "BOOK\n"
        "  CHAPTER c\n"
        "    ARGUMENT \"1\"\n"
        "  PARA s1 heading\n"
        "    TEXT \"Title\"\n"
        "  PARA q1\n"
        "    VERSE v\n"
        "      ARGUMENT \"1\"\n"
        "    TEXT \"Line one\"\n"
        "  PARA q2\n"
        "    TEXT \"line two\"\n");
}

static void test_parse_implied_paragraph(void)
{
    CHECK_TREE("\\c 1\n\\v 1 Text\n",
        "BOOK\n"
        "  CHAPTER c\n"
        "    ARGUMENT \"1\"\n"
        "  PARA p implied\n"
        "    VERSE v\n"
        "      ARGUMENT \"1\"\n"
        "    TEXT \"Text\"\n");
    CHECK_DIAGS("\\c 1\n\\v 1 Text\n", USFM_VERSION_AUTO, "IMPLIED_PARAGRAPH 5\n");
    CHECK_TREE("\\c 1\nstray\n",
        "BOOK\n"
        "  CHAPTER c\n"
        "    ARGUMENT \"1\"\n"
        "  PARA p implied\n"
        "    TEXT \"stray\"\n");
}

static void test_parse_trailing_whitespace(void)
{
    /* "\c 1  " (trailing spaces, as in WEB) leaves only blank text at
     * block level, which is dropped. */
    CHECK_TREE("\\c 1  \n\\p a  \n\\p b",
        "BOOK\n"
        "  CHAPTER c\n"
        "    ARGUMENT \"1\"\n"
        "  PARA p\n"
        "    TEXT \"a\"\n"
        "  PARA p\n"
        "    TEXT \"b\"\n");
}

static void test_parse_headers(void)
{
    CHECK_TREE("\\id GEN\n\\h Genesis\n\\toc1 The First Book\n\\mt1 Genesis\n",
        "BOOK\n"
        "  HEADER id\n"
        "    ARGUMENT \"GEN\"\n"
        "  HEADER h\n"
        "    TEXT \"Genesis\"\n"
        "  HEADER toc1\n"
        "    TEXT \"The First Book\"\n"
        "  PARA mt1 heading\n"
        "    TEXT \"Genesis\"\n");
}

static void test_parse_number_and_code_diagnostics(void)
{
    CHECK_DIAGS("\\id gen\n", USFM_VERSION_AUTO, "INVALID_BOOK_CODE 4\n");
    CHECK_DIAGS("\\c x\n", USFM_VERSION_AUTO, "INVALID_NUMBER 3\n");
    CHECK_DIAGS("\\c 0\n", USFM_VERSION_AUTO, "INVALID_NUMBER 3\n");
    CHECK_DIAGS("\\c 1\n\\p \\v 5-3 x", USFM_VERSION_AUTO, "INVALID_NUMBER 11\n");
    CHECK_DIAGS("\\p \\v 1 x", USFM_VERSION_AUTO, "VERSE_BEFORE_CHAPTER 3\n");
    CHECK_DIAGS("\\c 1\n\\p \\v\n", USFM_VERSION_AUTO, "INVALID_NUMBER 11\n");
}

static void test_parse_book_code_and_tables(void)
{
    USFM_Ast *ast = parse_with_version("\\id GEN x\n\\c 1\n\\p \\v 1 a \\v 2 b\n\\c 2\n\\p \\v 1 c",
                                       USFM_VERSION_AUTO);
    uint32_t chapter_count, verse_count;
    const USFM_ChapterEntry *chapters;
    const USFM_VerseEntry *verses;
    CHECK(ast != NULL);
    if (ast == NULL) return;
    CHECK_STR(USFM_Ast_GetBookCode(ast), "GEN");
    CHECK_EQ(USFM_Ast_GetMaxChapterNumber(ast), 2);
    CHECK_EQ(USFM_Ast_GetVersion(ast), USFM_VERSION_3_1);
    chapters = USFM_Ast_GetChapters(ast, &chapter_count);
    verses = USFM_Ast_GetVerses(ast, &verse_count);
    CHECK_EQ(chapter_count, 2);
    CHECK_EQ(verse_count, 3);
    CHECK_EQ(chapters[0].number, 1);
    CHECK_EQ(chapters[0].first_verse, 0);
    CHECK_EQ(chapters[0].verse_count, 2);
    CHECK_EQ(chapters[0].end_node, chapters[1].node);
    CHECK_EQ(chapters[1].first_verse, 2);
    CHECK_EQ(chapters[1].verse_count, 1);
    CHECK_EQ(verses[0].end_node, verses[1].node);
    CHECK_EQ(verses[1].end_node, chapters[1].node);
    CHECK_EQ(verses[2].ref.chapter, 2);
    CHECK_EQ(check_ast(ast), 0);
    USFM_Ast_Free(ast);
}

static void test_parse_verse_ranges(void)
{
    USFM_Ast *ast = parse_with_version("\\c 3\n\\p \\v 3-5a x", USFM_VERSION_AUTO);
    uint32_t verse_count;
    const USFM_VerseEntry *verses;
    CHECK(ast != NULL);
    if (ast == NULL) return;
    verses = USFM_Ast_GetVerses(ast, &verse_count);
    CHECK_EQ(verse_count, 1);
    CHECK_EQ(verses[0].ref.chapter, 3);
    CHECK_EQ(verses[0].ref.verse_first, 3);
    CHECK_EQ(verses[0].ref.verse_last, 5);
    CHECK_EQ(verses[0].ref.segment_first, 0);
    CHECK_EQ(verses[0].ref.segment_last, 1);
    USFM_Ast_Free(ast);
}

static void test_parse_bad_arguments(void)
{
    USFM_Err err;
    USFM_Config config;
    memset(&config, 0, sizeof config);
    CHECK(USFM_Ast_Parse(NULL, 5, NULL, &err) == NULL);
    CHECK_EQ(err.code, USFM_ERR_INVALID_ARG);
    config.version = 9;
    CHECK(USFM_Ast_Parse("\\p", 2, &config, &err) == NULL);
    CHECK_EQ(err.code, USFM_ERR_INVALID_ARG);
    CHECK(USFM_Ast_Parse("\\p", USFM_INPUT_MAX + 1u, NULL, &err) == NULL);
    CHECK_EQ(err.code, USFM_ERR_INPUT_TOO_LARGE);
}

static void run_parser_tests(void)
{
    RUN(test_parse_empty_input);
    RUN(test_parse_basic_book);
    RUN(test_parse_poetry_and_headings);
    RUN(test_parse_implied_paragraph);
    RUN(test_parse_trailing_whitespace);
    RUN(test_parse_headers);
    RUN(test_parse_number_and_code_diagnostics);
    RUN(test_parse_book_code_and_tables);
    RUN(test_parse_verse_ranges);
    RUN(test_parse_bad_arguments);
}
```

Offsets in the diagnostics tests:
- `\c 1\n\p \v 5-3 x`: `\c 1\n` is bytes 0–4, `\p ` is 5–7, `\v ` is 8–10, so the argument `5-3` starts at 11.
- For the missing argument (`\v\n`), the diagnostic sits at the end of the `\v` token (offset 11), with length 0.

In `tests/run_tests.c`, add after `#include "dump.h"`:

```c
#include "check.h"
#include "helpers.h"
```

Add `#include "test_parser.c"` after `test_scanner.c`, and in `main`:

```c
    fprintf(stderr, "parser\n");
    run_parser_tests();
```

- [ ] **Step 5: Run to verify it fails**

Run: `gcc -std=c99 -Wall -Wextra -pedantic -fshort-enums -O0 -g -fsanitize=address,undefined tests/run_tests.c -o build/run_tests`
Expected: link errors: undefined reference to `USFM_Ast_Parse`, `USFM_Ast_GetNodes` and the other functions.

- [ ] **Step 6: Implement the AST handle section**

Under `/* ===== AST handle ===== */`:

```c
struct USFM_Ast {
    USFM_Alc           alc;                 /* copy of the caller's allocator */
    const char        *input;
    char              *owned_input;         /* USFM_PARSE_COPY_INPUT, else NULL */
    uint32_t           input_length;
    uint8_t            version;             /* resolved enum USFM_Version */
    uint8_t            chapters_sorted;     /* strictly increasing numbers */
    uint8_t            verses_sorted;       /* strictly increasing per chapter */
    uint8_t            reserved;
    USFM_Node         *nodes;
    uint32_t           node_count, node_capacity;
    USFM_ChapterEntry *chapters;
    uint32_t           chapter_count, chapter_capacity;
    USFM_VerseEntry   *verses;
    uint32_t           verse_count, verse_capacity;
    USFM_Diagnostic   *diagnostics;
    uint32_t           diagnostic_count, diagnostic_capacity;
    uint32_t           dropped_diagnostics;
    uint32_t           first_dropped_offset;
    uint32_t           max_chapter;
    char               book_code[4];        /* NUL-terminated */
};

USFM_API void USFM_Ast_Free(USFM_Ast *ast)
{
    USFM_Alc alc;
    if (ast == NULL) return;
    alc = ast->alc;
    usfm__free(&alc, ast->nodes, (size_t)ast->node_capacity * sizeof(USFM_Node));
    usfm__free(&alc, ast->chapters, (size_t)ast->chapter_capacity * sizeof(USFM_ChapterEntry));
    usfm__free(&alc, ast->verses, (size_t)ast->verse_capacity * sizeof(USFM_VerseEntry));
    usfm__free(&alc, ast->diagnostics,
               (size_t)ast->diagnostic_capacity * sizeof(USFM_Diagnostic));
    usfm__free(&alc, ast->owned_input, ast->input_length);
    usfm__free(&alc, ast, sizeof(USFM_Ast));
}
```

- [ ] **Step 7: Implement the parser core**

Under `/* ===== Parser ===== */`:

```c
enum { USFM_FRAME_NOTE_PART = 0x01 };

typedef struct USFM_Frame {
    uint32_t node;
    uint8_t  kind;                      /* USFM_AST_* of the node */
    uint8_t  flags;                     /* USFM_FRAME_* */
    uint16_t reserved;
} USFM_Frame;

typedef struct USFM_Parser {
    USFM_Ast      *ast;
    USFM_Scanner   scan;
    USFM_ScanToken peeked;
    uint8_t        has_peeked;
    uint8_t        failed;              /* a tree array could not grow */
    uint8_t        version;             /* resolved; \usfm may change it */
    uint8_t        version_auto;        /* config said AUTO */
    uint32_t       depth;
    uint32_t       last_end;            /* end of the last token placed */
    uint32_t       lookahead_node;      /* cache for usfm__closer_ahead */
    uint32_t       lookahead_until;
    USFM_Frame     stack[USFM_STACK_DEPTH];
} USFM_Parser;

static uint8_t usfm__top_kind(const USFM_Parser *p)
{
    return p->stack[p->depth - 1].kind;
}

static int usfm__is_block(uint8_t kind)
{
    return kind == USFM_AST_BOOK || kind == USFM_AST_SIDEBAR || kind == USFM_AST_PERIPHERAL;
}

static int usfm__is_blank(const char *in, USFM_Span s)
{
    uint32_t i;
    for (i = 0; i < s.length; i++) {
        char c = in[s.offset + i];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return 0;
    }
    return 1;
}

static int usfm__next(USFM_Parser *p, USFM_ScanToken *t)
{
    uint8_t top;
    if (p->has_peeked) {
        *t = p->peeked;
        p->has_peeked = 0;
        return 1;
    }
    top = usfm__top_kind(p);
    p->scan.pipe_is_attr = (uint8_t)(top == USFM_AST_CHAR || top == USFM_AST_MILESTONE ||
                                     top == USFM_AST_FIGURE || top == USFM_AST_PERIPHERAL);
    return usfm__scan_next(&p->scan, t);
}

static void usfm__push_back(USFM_Parser *p, const USFM_ScanToken *t)
{
    p->peeked = *t;
    p->has_peeked = 1;
}

/* Diagnostics never fail the parse: if the array can't grow, the
 * diagnostic is counted as dropped. */
static void usfm__diag(USFM_Parser *p, uint16_t code, USFM_Span span, USFM_Marker marker)
{
    USFM_Ast *ast = p->ast;
    USFM_Diagnostic *d;
    if (ast->diagnostic_count == ast->diagnostic_capacity) {
        void *grown = usfm__grow(&ast->alc, ast->diagnostics, &ast->diagnostic_capacity,
                                 sizeof(USFM_Diagnostic), ast->diagnostic_count + 1);
        if (grown == NULL) {
            if (ast->dropped_diagnostics == 0) ast->first_dropped_offset = span.offset;
            ast->dropped_diagnostics++;
            return;
        }
        ast->diagnostics = (USFM_Diagnostic *)grown;
    }
    d = &ast->diagnostics[ast->diagnostic_count++];
    d->span = span;
    d->code = code;
    d->marker = marker;
}

/* Appends a node as a child of the top frame. Returns its index, or
 * USFM_NIL (and sets p->failed) if the node array can't grow. */
static uint32_t usfm__append(USFM_Parser *p, uint8_t kind, USFM_Span span,
                             USFM_Marker marker, uint8_t level_flags)
{
    USFM_Ast *ast = p->ast;
    USFM_Node *n;
    uint32_t index, span_end;
    if (p->failed) return USFM_NIL;
    if (ast->node_count == ast->node_capacity) {
        void *grown = usfm__grow(&ast->alc, ast->nodes, &ast->node_capacity,
                                 sizeof(USFM_Node), ast->node_count + 1);
        if (grown == NULL) {
            p->failed = 1;
            return USFM_NIL;
        }
        ast->nodes = (USFM_Node *)grown;
    }
    index = ast->node_count++;
    n = &ast->nodes[index];
    n->parent = p->depth > 0 ? p->stack[p->depth - 1].node : USFM_NIL;
    n->end = index + 1;
    n->span = span;
    n->marker = marker;
    n->kind = kind;
    n->level_flags = level_flags;
    span_end = span.offset + span.length;
    if (span_end > p->last_end) p->last_end = span_end;
    return index;
}

/* Appends a container and pushes it. Returns USFM_NIL if the stack is
 * full (diagnostic added) or memory ran out (p->failed). */
static uint32_t usfm__open(USFM_Parser *p, uint8_t kind, USFM_Span span, USFM_Marker marker,
                           uint8_t level_flags, uint8_t frame_flags)
{
    uint32_t node;
    if (p->depth >= USFM_STACK_DEPTH) {
        usfm__diag(p, USFM_DIAGNOSTIC_TOO_DEEP, span, marker);
        return USFM_NIL;
    }
    node = usfm__append(p, kind, span, marker, level_flags);
    if (node == USFM_NIL) return USFM_NIL;
    p->stack[p->depth].node = node;
    p->stack[p->depth].kind = kind;
    p->stack[p->depth].flags = frame_flags;
    p->stack[p->depth].reserved = 0;
    p->depth++;
    return node;
}

/* Opens a container for a marker token. If the stack is full, the
 * marker is kept as plain text instead. */
static uint32_t usfm__open_tok(USFM_Parser *p, uint8_t kind, const USFM_ScanToken *t,
                               uint8_t level_flags, uint8_t frame_flags)
{
    uint32_t node = usfm__open(p, kind, t->tok.span, t->tok.marker, level_flags, frame_flags);
    if (node == USFM_NIL && !p->failed) {
        usfm__append(p, USFM_AST_TEXT, t->tok.span, USFM_M_NONE, 0);
    }
    return node;
}

/* Pops the top frame. Its span grows to the last byte placed inside it
 * (or to an end marker the caller just accounted for in last_end). */
static void usfm__close_top(USFM_Parser *p)
{
    USFM_Frame *f;
    USFM_Node *n;
    USFM_ASSERT(p->depth > 1);
    f = &p->stack[--p->depth];
    n = &p->ast->nodes[f->node];
    n->end = p->ast->node_count;
    if (p->last_end > n->span.offset + n->span.length) {
        n->span.length = p->last_end - n->span.offset;
    }
}

/* Closes the top frame without an end marker, reporting it when the
 * version requires an explicit close. */
static void usfm__close_implicit(USFM_Parser *p)
{
    const USFM_Frame *f = &p->stack[p->depth - 1];
    const USFM_Node *n = &p->ast->nodes[f->node];
    int report = f->kind == USFM_AST_NOTE || f->kind == USFM_AST_FIGURE ||
                 (f->kind == USFM_AST_CHAR && !(f->flags & USFM_FRAME_NOTE_PART) &&
                  p->version >= USFM_VERSION_3_1);
    if (report) usfm__diag(p, USFM_DIAGNOSTIC_UNCLOSED_MARKER, n->span, n->marker);
    usfm__close_top(p);
}

/* Bounded by the stack depth. */
static void usfm__close_to_depth(USFM_Parser *p, uint32_t depth)
{
    while (p->depth > depth && p->depth > 1) usfm__close_implicit(p);
}

/* Closes everything above the nearest BOOK / SIDEBAR / PERIPHERAL
 * (and above a TABLE too, if keep_table). */
static void usfm__close_to_block(USFM_Parser *p, int keep_table)
{
    while (p->depth > 1) {
        uint8_t k = usfm__top_kind(p);
        if (usfm__is_block(k) || (keep_table && k == USFM_AST_TABLE)) break;
        usfm__close_implicit(p);
    }
}

/* Depth (index + 1) of the nearest open frame of `kind`, or 0. */
static uint32_t usfm__find_kind(const USFM_Parser *p, uint8_t kind)
{
    uint32_t d;
    for (d = p->depth; d > 0; d--) {
        if (p->stack[d - 1].kind == kind) return d;
    }
    return 0;
}

/* Trims trailing whitespace from the last TEXT node if it belongs to the
 * open top container, removing the node if nothing is left. Called
 * before paragraph-level markers, \v and the end of input. */
static void usfm__trim_last_text(USFM_Parser *p)
{
    USFM_Ast *ast = p->ast;
    USFM_Node *n;
    if (ast->node_count == 0) return;
    n = &ast->nodes[ast->node_count - 1];
    if (n->kind != USFM_AST_TEXT || n->parent != p->stack[p->depth - 1].node) return;
    while (n->span.length > 0) {
        char c = ast->input[n->span.offset + n->span.length - 1];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        n->span.length--;
    }
    if (n->span.length == 0) ast->node_count--;
}

/* Inline content with no paragraph open gets an implied \p. */
static void usfm__ensure_inline(USFM_Parser *p, uint32_t offset)
{
    uint8_t k = usfm__top_kind(p);
    USFM_Span at;
    if (!usfm__is_block(k) && k != USFM_AST_TABLE) return;
    at = usfm__span(offset, 0);
    usfm__diag(p, USFM_DIAGNOSTIC_IMPLIED_PARAGRAPH, at, USFM_M_P);
    usfm__open(p, USFM_AST_PARA, at, USFM_M_P, USFM_NF_IMPLIED, 0);
}

/* Takes the ARGUMENT token the scanner produced after a marker, as a
 * leaf child of the top frame. Returns 0 if there was none (the next
 * token is pushed back; *arg is a zero-length span at `missing_at`). */
static int usfm__take_argument(USFM_Parser *p, USFM_Span *arg, uint32_t missing_at)
{
    USFM_ScanToken t;
    if (usfm__next(p, &t)) {
        if (t.tok.kind == USFM_TOK_ARGUMENT) {
            usfm__append(p, USFM_AST_ARGUMENT, t.tok.span, USFM_M_NONE, 0);
            *arg = t.tok.span;
            return 1;
        }
        usfm__push_back(p, &t);
    }
    *arg = usfm__span(missing_at, 0);
    return 0;
}

static uint32_t usfm__tok_end(const USFM_ScanToken *t)
{
    return t->tok.span.offset + t->tok.span.length;
}

/* Reads decimal digits at in[*i]; fails on no digits or > 65535. */
static int usfm__read_uint(const char *in, uint32_t *i, uint32_t end, uint32_t *value)
{
    uint32_t start = *i, v = 0;
    while (*i < end && in[*i] >= '0' && in[*i] <= '9') {
        if (v <= 65535u) v = v * 10u + (uint32_t)(in[*i] - '0');
        (*i)++;
    }
    *value = v;
    return *i > start && v <= 65535u;
}

/* "3", "3a", "3-5", "3a-5b". Returns 0 if malformed; *ref keeps what
 * was read. */
static int usfm__parse_verse(const char *in, USFM_Span s, USFM_VerseRef *ref)
{
    uint32_t i = s.offset, end = s.offset + s.length, v;
    if (!usfm__read_uint(in, &i, end, &v)) return 0;
    ref->verse_first = ref->verse_last = (uint16_t)v;
    if (i < end && in[i] >= 'a' && in[i] <= 'z') {
        ref->segment_first = ref->segment_last = (uint8_t)(in[i] - 'a' + 1);
        i++;
    }
    if (i < end && in[i] == '-') {
        i++;
        if (!usfm__read_uint(in, &i, end, &v)) return 0;
        ref->verse_last = (uint16_t)v;
        ref->segment_last = 0;
        if (i < end && in[i] >= 'a' && in[i] <= 'z') {
            ref->segment_last = (uint8_t)(in[i] - 'a' + 1);
            i++;
        }
        if (ref->verse_last < ref->verse_first) return 0;
    }
    return i == end;
}

/* Three bytes, A-Z or 0-9, at least one letter (USFM 3.1.1). */
static int usfm__valid_book_code(const char *in, USFM_Span s)
{
    uint32_t i;
    int letters = 0;
    if (s.length != 3) return 0;
    for (i = 0; i < 3; i++) {
        char c = in[s.offset + i];
        if (c >= 'A' && c <= 'Z') letters++;
        else if (c < '0' || c > '9') return 0;
    }
    return letters > 0;
}

/* The most recent verse ends where the next verse or chapter starts. */
static void usfm__end_open_verse(USFM_Parser *p, uint32_t node)
{
    USFM_Ast *ast = p->ast;
    if (ast->verse_count > 0 && ast->verses[ast->verse_count - 1].end_node == USFM_NIL) {
        ast->verses[ast->verse_count - 1].end_node = node;
    }
}

static void usfm__add_chapter(USFM_Parser *p, uint32_t node, uint32_t number)
{
    USFM_Ast *ast = p->ast;
    USFM_ChapterEntry *e;
    void *grown;
    usfm__end_open_verse(p, node);
    if (ast->chapter_count > 0) {
        USFM_ChapterEntry *prev = &ast->chapters[ast->chapter_count - 1];
        prev->end_node = node;
        if (number <= prev->number) ast->chapters_sorted = 0;
    }
    grown = usfm__grow(&ast->alc, ast->chapters, &ast->chapter_capacity,
                       sizeof(USFM_ChapterEntry), ast->chapter_count + 1);
    if (grown == NULL) {
        p->failed = 1;
        return;
    }
    ast->chapters = (USFM_ChapterEntry *)grown;
    e = &ast->chapters[ast->chapter_count++];
    memset(e, 0, sizeof *e);
    e->number = (uint16_t)number;
    e->node = node;
    e->end_node = USFM_NIL;
    e->first_verse = ast->verse_count;
    if (number > ast->max_chapter) ast->max_chapter = number;
}

static void usfm__add_verse(USFM_Parser *p, uint32_t node, const USFM_VerseRef *ref)
{
    USFM_Ast *ast = p->ast;
    USFM_VerseEntry *e;
    void *grown;
    usfm__end_open_verse(p, node);
    grown = usfm__grow(&ast->alc, ast->verses, &ast->verse_capacity,
                       sizeof(USFM_VerseEntry), ast->verse_count + 1);
    if (grown == NULL) {
        p->failed = 1;
        return;
    }
    ast->verses = (USFM_VerseEntry *)grown;
    e = &ast->verses[ast->verse_count++];
    e->ref = *ref;
    e->node = node;
    e->end_node = USFM_NIL;
    if (ast->chapter_count > 0) {
        USFM_ChapterEntry *chapter = &ast->chapters[ast->chapter_count - 1];
        if (chapter->verse_count > 0 &&
            ast->verses[ast->verse_count - 2].ref.verse_first >= ref->verse_first) {
            ast->verses_sorted = 0;
        }
        chapter->verse_count++;
    }
}

static void usfm__set_version(USFM_Parser *p, USFM_Span arg)
{
    const char *s = p->ast->input + arg.offset;
    uint32_t i = 0, major = 0, minor = 0;
    uint8_t v;
    if (!p->version_auto) return;
    while (i < arg.length && s[i] >= '0' && s[i] <= '9' && major < 100) major = major * 10u + (uint32_t)(s[i++] - '0');
    if (i < arg.length && s[i] == '.') {
        i++;
        while (i < arg.length && s[i] >= '0' && s[i] <= '9' && minor < 100) minor = minor * 10u + (uint32_t)(s[i++] - '0');
    }
    if (major == 1) v = USFM_VERSION_1;
    else if (major == 2) v = USFM_VERSION_2;
    else if (major == 3 && minor == 0) v = USFM_VERSION_3_0;
    else if (major == 3) v = USFM_VERSION_3_1;
    else {
        usfm__diag(p, USFM_DIAGNOSTIC_UNSUPPORTED_VERSION, arg, USFM_M_USFM);
        v = USFM_VERSION_3_1;
    }
    p->version = v;
}

static void usfm__on_text(USFM_Parser *p, const USFM_ScanToken *t)
{
    uint8_t k = usfm__top_kind(p);
    if ((usfm__is_block(k) || k == USFM_AST_TABLE || k == USFM_AST_MILESTONE) &&
        usfm__is_blank(p->ast->input, t->tok.span)) {
        return;                         /* layout whitespace between blocks */
    }
    usfm__ensure_inline(p, t->tok.span.offset);
    usfm__append(p, USFM_AST_TEXT, t->tok.span, USFM_M_NONE,
                 (uint8_t)(t->tok.level_flags & USFM_TEXT_FLAGS_COOK));
}

static void usfm__on_optbreak(USFM_Parser *p, const USFM_ScanToken *t)
{
    usfm__ensure_inline(p, t->tok.span.offset);
    usfm__append(p, USFM_AST_OPTBREAK, t->tok.span, USFM_M_NONE, 0);
}

static void usfm__on_para(USFM_Parser *p, const USFM_ScanToken *t, uint8_t kind)
{
    const USFM_MarkerRow *row = &USFM_MarkerRows[t->row];
    uint8_t level_flags = USFM_LEVEL(t->tok.level_flags);
    uint32_t node;
    USFM_Span arg;
    usfm__trim_last_text(p);
    usfm__close_to_block(p, 0);
    if (kind == USFM_AST_PARA && (row->props & USFM_MP_HEADING)) level_flags |= USFM_NF_HEADING;
    node = usfm__open_tok(p, kind, t, level_flags, 0);
    if (node == USFM_NIL || !(row->props & USFM_MP_ARG_WORD)) return;
    if (usfm__take_argument(p, &arg, usfm__tok_end(t)) && t->tok.marker == USFM_M_USFM) {
        usfm__set_version(p, arg);
    }
}

static void usfm__on_id(USFM_Parser *p, const USFM_ScanToken *t)
{
    USFM_Ast *ast = p->ast;
    USFM_Span arg;
    int has;
    usfm__trim_last_text(p);
    usfm__close_to_depth(p, 1);
    if (usfm__open_tok(p, USFM_AST_HEADER, t, 0, 0) == USFM_NIL) return;
    has = usfm__take_argument(p, &arg, usfm__tok_end(t));
    if (!has || !usfm__valid_book_code(ast->input, arg)) {
        usfm__diag(p, USFM_DIAGNOSTIC_INVALID_BOOK_CODE, arg, USFM_M_ID);
    }
    if (has && arg.length == 3 && ast->book_code[0] == '\0') {
        memcpy(ast->book_code, ast->input + arg.offset, 3);
        ast->book_code[3] = '\0';
    }
}

static void usfm__on_chapter(USFM_Parser *p, const USFM_ScanToken *t)
{
    USFM_Span arg;
    uint32_t node, number = 0, i;
    int valid;
    usfm__trim_last_text(p);
    usfm__close_to_depth(p, 1);
    node = usfm__open_tok(p, USFM_AST_CHAPTER, t, 0, 0);
    if (node == USFM_NIL) return;
    valid = usfm__take_argument(p, &arg, usfm__tok_end(t));
    usfm__close_top(p);
    i = arg.offset;
    valid = valid && usfm__read_uint(p->ast->input, &i, arg.offset + arg.length, &number) &&
            i == arg.offset + arg.length && number > 0;
    if (!valid) {
        usfm__diag(p, USFM_DIAGNOSTIC_INVALID_NUMBER, arg, USFM_M_C);
        number = 0;
    }
    usfm__add_chapter(p, node, number);
}

static void usfm__on_verse(USFM_Parser *p, const USFM_ScanToken *t)
{
    USFM_Ast *ast = p->ast;
    USFM_VerseRef ref;
    USFM_Span arg;
    uint32_t node, note;
    uint8_t k;
    usfm__trim_last_text(p);
    /* A verse can't be inside a note: the note was left open. */
    note = usfm__find_kind(p, USFM_AST_NOTE);
    if (note != 0) usfm__close_to_depth(p, note - 1);
    /* Character spans end at a verse. Bounded by the stack depth. */
    while (p->depth > 1) {
        k = usfm__top_kind(p);
        if (k != USFM_AST_CHAR && k != USFM_AST_MILESTONE && k != USFM_AST_FIGURE) break;
        usfm__close_implicit(p);
    }
    /* A verse inside a heading closes the heading. */
    k = usfm__top_kind(p);
    if (p->depth > 1 &&
        (k == USFM_AST_HEADER ||
         (k == USFM_AST_PARA && (ast->nodes[p->stack[p->depth - 1].node].level_flags & USFM_NF_HEADING)))) {
        usfm__close_implicit(p);
    }
    usfm__ensure_inline(p, t->tok.span.offset);
    node = usfm__open_tok(p, USFM_AST_VERSE, t, 0, 0);
    if (node == USFM_NIL) return;
    memset(&ref, 0, sizeof ref);
    if (!usfm__take_argument(p, &arg, usfm__tok_end(t)) ||
        !usfm__parse_verse(ast->input, arg, &ref)) {
        usfm__diag(p, USFM_DIAGNOSTIC_INVALID_NUMBER, arg, USFM_M_V);
    }
    usfm__close_top(p);
    if (ast->chapter_count == 0) {
        usfm__diag(p, USFM_DIAGNOSTIC_VERSE_BEFORE_CHAPTER, t->tok.span, USFM_M_V);
    } else {
        ref.chapter = ast->chapters[ast->chapter_count - 1].number;
    }
    usfm__add_verse(p, node, &ref);
}

static void usfm__on_close(USFM_Parser *p, const USFM_ScanToken *t)
{
    /* Replaced in Task 6. */
    usfm__diag(p, USFM_DIAGNOSTIC_UNEXPECTED_CLOSE, t->tok.span, t->tok.marker);
}

static void usfm__on_marker(USFM_Parser *p, USFM_ScanToken *t)
{
    uint8_t role = t->row == USFM_ROW_NONE ? 0 : USFM_MarkerRows[t->row].role;
    switch (role) {
    case USFM_ROLE_PARA: usfm__on_para(p, t, USFM_AST_PARA); break;
    case USFM_ROLE_HEADER: usfm__on_para(p, t, USFM_AST_HEADER); break;
    case USFM_ROLE_ID: usfm__on_id(p, t); break;
    case USFM_ROLE_CHAPTER: usfm__on_chapter(p, t); break;
    case USFM_ROLE_VERSE: usfm__on_verse(p, t); break;
    default:
        /* Roles handled by later sections; keep the marker as a leaf. */
        usfm__append(p, USFM_AST_MILESTONE, t->tok.span, t->tok.marker, 0);
        break;
    }
}

/* The main loop. Bounded: every token consumes at least one input byte,
 * and a pushed-back token is consumed by the next iteration. */
static void usfm__parse(USFM_Parser *p)
{
    USFM_ScanToken t;
    while (!p->failed && usfm__next(p, &t)) {
        switch (t.tok.kind) {
        case USFM_TOK_TEXT: usfm__on_text(p, &t); break;
        case USFM_TOK_ARGUMENT: usfm__on_text(p, &t); break;   /* unclaimed */
        case USFM_TOK_ATTRIBUTES: usfm__on_text(p, &t); break; /* Task 6 */
        case USFM_TOK_OPTBREAK: usfm__on_optbreak(p, &t); break;
        case USFM_TOK_MARKER_END: usfm__on_close(p, &t); break;
        case USFM_TOK_MARKER: usfm__on_marker(p, &t); break;
        default: break;
        }
    }
}

static void usfm__finish(USFM_Parser *p)
{
    USFM_Ast *ast = p->ast;
    usfm__trim_last_text(p);
    while (p->depth > 1) usfm__close_implicit(p);
    ast->nodes[0].end = ast->node_count;
    if (ast->chapter_count > 0 && ast->chapters[ast->chapter_count - 1].end_node == USFM_NIL) {
        ast->chapters[ast->chapter_count - 1].end_node = ast->node_count;
    }
    if (ast->verse_count > 0 && ast->verses[ast->verse_count - 1].end_node == USFM_NIL) {
        ast->verses[ast->verse_count - 1].end_node = ast->node_count;
    }
}

USFM_API USFM_Ast *USFM_Ast_ParseOpts(const char *input, uint32_t input_length,
                                      const USFM_Config *config, uint32_t flags,
                                      const USFM_Alc *alc_in, USFM_Err *err)
{
    const USFM_Alc *alc = usfm__alc_or_default(alc_in);
    USFM_Ast *ast;
    USFM_Parser p;
    const char *text = input != NULL ? input : "";
    uint8_t version = config != NULL ? config->version : (uint8_t)USFM_VERSION_AUTO;
    void *grown;

    usfm__set_err(err, USFM_OK, 0);
    if (alc == NULL) {
        usfm__set_err(err, USFM_ERR_NO_ALLOCATOR, 0);
        return NULL;
    }
    if ((input == NULL && input_length > 0) || version > USFM_VERSION_3_1) {
        usfm__set_err(err, USFM_ERR_INVALID_ARG, 0);
        return NULL;
    }
    if (input_length > USFM_INPUT_MAX) {
        usfm__set_err(err, USFM_ERR_INPUT_TOO_LARGE, 0);
        return NULL;
    }
    ast = (USFM_Ast *)usfm__alloc(alc, sizeof(USFM_Ast));
    if (ast == NULL) {
        usfm__set_err(err, USFM_ERR_NOMEM, 0);
        return NULL;
    }
    memset(ast, 0, sizeof *ast);
    ast->alc = *alc;
    ast->chapters_sorted = 1;
    ast->verses_sorted = 1;
    if ((flags & USFM_PARSE_COPY_INPUT) && input_length > 0) {
        ast->owned_input = (char *)usfm__alloc(alc, input_length);
        if (ast->owned_input == NULL) {
            USFM_Ast_Free(ast);
            usfm__set_err(err, USFM_ERR_NOMEM, 0);
            return NULL;
        }
        memcpy(ast->owned_input, input, input_length);
        text = ast->owned_input;
    }
    ast->input = text;
    ast->input_length = input_length;

    /* Initial node capacity from the input length (WEB averages about
     * one node per 12 bytes); grown x2 as needed. */
    grown = usfm__grow(alc, NULL, &ast->node_capacity, sizeof(USFM_Node), input_length / 8u + 16u);
    if (grown == NULL) {
        USFM_Ast_Free(ast);
        usfm__set_err(err, USFM_ERR_NOMEM, 0);
        return NULL;
    }
    ast->nodes = (USFM_Node *)grown;

    memset(&p, 0, sizeof p);
    p.ast = ast;
    p.version_auto = version == USFM_VERSION_AUTO;
    p.version = p.version_auto ? (uint8_t)USFM_VERSION_3_1 : version;
    p.lookahead_node = USFM_NIL;
    usfm__scan_init(&p.scan, text, input_length);
    usfm__open(&p, USFM_AST_BOOK, usfm__span(0, input_length), USFM_M_NONE, 0, 0);
    usfm__parse(&p);
    if (!p.failed) usfm__finish(&p);
    if (p.failed) {
        uint32_t pos = p.scan.position;
        USFM_Ast_Free(ast);
        usfm__set_err(err, USFM_ERR_NOMEM, pos);
        return NULL;
    }
    ast->version = p.version;
    if (ast->dropped_diagnostics > 0) {
        usfm__set_err(err, USFM_ERR_NOMEM, ast->first_dropped_offset);
    }
    return ast;
}

USFM_API USFM_Ast *USFM_Ast_Parse(const char *input, uint32_t input_length,
                                  const USFM_Config *config, USFM_Err *err)
{
    return USFM_Ast_ParseOpts(input, input_length, config, USFM_PARSE_DEFAULT, NULL, err);
}
```

The BOOK node is opened with depth 0, so its parent is `USFM_NIL`. `usfm__open` cannot fail on depth there. If the node array can't be allocated, `p.failed` is set and the parse returns NULL.

- [ ] **Step 8: Implement the accessors**

Under `/* ===== Lookups ===== */`:

```c
USFM_API uint8_t USFM_Node_GetLevel(const USFM_Node *node) { return USFM_LEVEL(node->level_flags); }
USFM_API uint8_t USFM_Node_GetFlags(const USFM_Node *node) { return USFM_FLAGS(node->level_flags); }
USFM_API int USFM_Node_HasFlags(const USFM_Node *node, uint32_t flags)
{
    return (USFM_FLAGS(node->level_flags) & flags) == flags;
}

USFM_API const char *USFM_Ast_GetInput(const USFM_Ast *ast, uint32_t *length)
{
    if (length) *length = ast ? ast->input_length : 0;
    return ast ? ast->input : NULL;
}

USFM_API const USFM_Diagnostic *USFM_Ast_GetDiagnostics(const USFM_Ast *ast, uint32_t *count)
{
    if (count) *count = ast ? ast->diagnostic_count : 0;
    return ast ? ast->diagnostics : NULL;
}

USFM_API uint32_t USFM_Ast_GetDroppedDiagnosticCount(const USFM_Ast *ast)
{
    return ast ? ast->dropped_diagnostics : 0;
}

USFM_API const USFM_Node *USFM_Ast_GetNodes(const USFM_Ast *ast, uint32_t *count)
{
    if (count) *count = ast ? ast->node_count : 0;
    return ast ? ast->nodes : NULL;
}

USFM_API const USFM_ChapterEntry *USFM_Ast_GetChapters(const USFM_Ast *ast, uint32_t *count)
{
    if (count) *count = ast ? ast->chapter_count : 0;
    return ast ? ast->chapters : NULL;
}

USFM_API const USFM_VerseEntry *USFM_Ast_GetVerses(const USFM_Ast *ast, uint32_t *count)
{
    if (count) *count = ast ? ast->verse_count : 0;
    return ast ? ast->verses : NULL;
}

USFM_API uint32_t USFM_Ast_GetMaxChapterNumber(const USFM_Ast *ast)
{
    return ast ? ast->max_chapter : 0;
}

USFM_API const char *USFM_Ast_GetBookCode(const USFM_Ast *ast)
{
    return ast ? ast->book_code : "";
}

USFM_API uint8_t USFM_Ast_GetVersion(const USFM_Ast *ast)
{
    return ast ? ast->version : (uint8_t)USFM_VERSION_AUTO;
}
```

- [ ] **Step 9: Run the tests**

Run: `./build.sh test`

This is the first run of the full mode. It includes the C++ syntax check, which has all its declarations now.
Expected: `ok: N checks`. The C++ check prints nothing on success.

If a tree test fails, compare the printed `expected` and `actual`. A difference in trailing whitespace usually means `usfm__trim_last_text` wasn't called on that path.

- [ ] **Step 10: Commit**

```bash
git add usfm.h tests/check.h tests/dump.h tests/helpers.h tests/test_parser.c tests/run_tests.c
git commit -m "feat: AST parser core with chapter and verse tables

One loop over scanner tokens with a fixed 64-deep stack: paragraph-level
markers close to the nearest block, \\c and \\v become milestones with
ARGUMENT children and table entries, implied \\p, whitespace trimming.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Character spans and end markers

**Files:**
- Modify: `usfm.h` (Parser section)
- Create: `tests/test_chars.c`
- Modify: `tests/run_tests.c`

- [ ] **Step 1: Write the failing tests `tests/test_chars.c`**

```c
static void test_char_explicit_close(void)
{
    CHECK_TREE("\\p \\nd LORD\\nd* said",
        "BOOK\n"
        "  PARA p\n"
        "    CHAR nd\n"
        "      TEXT \"LORD\"\n"
        "    TEXT \" said\"\n");
}

static void test_char_plus_nesting(void)
{
    CHECK_TREE("\\p \\add an \\+nd Lord\\+nd* addition\\add*",
        "BOOK\n"
        "  PARA p\n"
        "    CHAR add\n"
        "      TEXT \"an \"\n"
        "      CHAR +nd\n"
        "        TEXT \"Lord\"\n"
        "      TEXT \" addition\"\n");
}

static void test_char_plain_nesting_when_outer_closes_later(void)
{
    /* 3.1 style: \nd nests because \add* appears later. */
    CHECK_TREE("\\p \\add a \\nd b\\nd* c\\add*",
        "BOOK\n"
        "  PARA p\n"
        "    CHAR add\n"
        "      TEXT \"a \"\n"
        "      CHAR nd\n"
        "        TEXT \"b\"\n"
        "      TEXT \" c\"\n");
    CHECK_DIAGS("\\p \\add a \\nd b\\nd* c\\add*", USFM_VERSION_AUTO, "");
}

static void test_char_plain_marker_closes_outer_without_closer(void)
{
    /* 2.x style: no \add* ahead, so \nd closes \add. */
    CHECK_TREE("\\p \\add a \\nd b\\nd* c",
        "BOOK\n"
        "  PARA p\n"
        "    CHAR add\n"
        "      TEXT \"a \"\n"
        "    CHAR nd\n"
        "      TEXT \"b\"\n"
        "    TEXT \" c\"\n");
    CHECK_DIAGS("\\p \\add a \\nd b\\nd* c", USFM_VERSION_AUTO, "UNCLOSED_MARKER 3\n");
    CHECK_DIAGS("\\p \\add a \\nd b\\nd* c", USFM_VERSION_2, "");
}

static void test_char_outer_close_closes_inner(void)
{
    CHECK_TREE("\\p \\add a \\+nd b\\add* c",
        "BOOK\n"
        "  PARA p\n"
        "    CHAR add\n"
        "      TEXT \"a \"\n"
        "      CHAR +nd\n"
        "        TEXT \"b\"\n"
        "    TEXT \" c\"\n");
    CHECK_DIAGS("\\p \\add a \\+nd b\\add* c", USFM_VERSION_AUTO, "UNCLOSED_MARKER 10\n");
    CHECK_DIAGS("\\p \\add a \\+nd b\\add* c", USFM_VERSION_2, "");
}

static void test_char_stray_end_marker(void)
{
    CHECK_TREE("\\p a\\nd* b",
        "BOOK\n"
        "  PARA p\n"
        "    TEXT \"a\"\n"
        "    TEXT \" b\"\n");
    CHECK_DIAGS("\\p a\\nd* b", USFM_VERSION_AUTO, "UNEXPECTED_CLOSE 4\n");
}

static void test_char_closed_by_verse(void)
{
    CHECK_TREE("\\c 1\n\\p \\wj a \\v 2 b\\wj*",
        "BOOK\n"
        "  CHAPTER c\n"
        "    ARGUMENT \"1\"\n"
        "  PARA p\n"
        "    CHAR wj\n"
        "      TEXT \"a\"\n"
        "    VERSE v\n"
        "      ARGUMENT \"2\"\n"
        "    TEXT \"b\"\n");
    CHECK_DIAGS("\\c 1\n\\p \\wj a \\v 2 b\\wj*", USFM_VERSION_AUTO,
                "UNCLOSED_MARKER 8\nUNEXPECTED_CLOSE 20\n");
}

static void test_char_attributes(void)
{
    CHECK_TREE("\\p \\w gracious|lemma=\"grace\"\\w*",
        "BOOK\n"
        "  PARA p\n"
        "    CHAR w\n"
        "      TEXT \"gracious\"\n"
        "      ATTRIBUTES \"|lemma=\\\"grace\\\"\"\n");
}

static void test_char_lookahead_cache(void)
{
    /* Two plain \w inside \wj: both nest; the second reuses the cached
     * lookahead result. */
    CHECK_TREE("\\p \\wj \\w a\\w* \\w b\\w*\\wj*",
        "BOOK\n"
        "  PARA p\n"
        "    CHAR wj\n"
        "      CHAR w\n"
        "        TEXT \"a\"\n"
        "      TEXT \" \"\n"
        "      CHAR w\n"
        "        TEXT \"b\"\n");
}

static void run_chars_tests(void)
{
    RUN(test_char_explicit_close);
    RUN(test_char_plus_nesting);
    RUN(test_char_plain_nesting_when_outer_closes_later);
    RUN(test_char_plain_marker_closes_outer_without_closer);
    RUN(test_char_outer_close_closes_inner);
    RUN(test_char_stray_end_marker);
    RUN(test_char_closed_by_verse);
    RUN(test_char_attributes);
    RUN(test_char_lookahead_cache);
}
```

In `tests/run_tests.c`, add `#include "test_chars.c"` after `test_parser.c`, and in `main`:

```c
    fprintf(stderr, "chars\n");
    run_chars_tests();
```

- [ ] **Step 2: Run to verify it fails**

Run: `./build.sh test`
Expected: FAIL. The char markers are kept as MILESTONE leaves and `\nd*` reports UNEXPECTED_CLOSE, so the tree mismatches print.

- [ ] **Step 3: Implement character spans**

In the Parser section, insert these functions before `usfm__on_close`:

```c
/* Name bytes of a marker node: after "\" and an optional "+". */
static void usfm__node_name(const USFM_Parser *p, uint32_t node, uint32_t *offset,
                            uint32_t *length)
{
    const char *in = p->ast->input;
    uint32_t len = p->ast->input_length;
    uint32_t i = p->ast->nodes[node].span.offset + 1, start;
    if (i < len && in[i] == '+') i++;
    start = i;
    while (i < len && (USFM_ByteClass[(uint8_t)in[i]] & USFM_BC_NAME)) i++;
    *offset = start;
    *length = i - start;
}

/* Does end marker `closer` close a span of `marker`? Unknown markers
 * match by name. */
static int usfm__closer_matches(const USFM_Parser *p, const USFM_ScanToken *closer,
                                USFM_Marker marker, uint32_t name_offset, uint32_t name_length)
{
    if (closer->name_length == 0) return 0;
    if (marker != USFM_M_UNKNOWN) return closer->tok.marker == marker;
    return closer->tok.marker == USFM_M_UNKNOWN && closer->name_length == name_length &&
           memcmp(p->ast->input + closer->name_offset, p->ast->input + name_offset,
                  name_length) == 0;
}

static int usfm__frame_matches(const USFM_Parser *p, const USFM_Frame *f,
                               const USFM_ScanToken *closer)
{
    const USFM_Node *n = &p->ast->nodes[f->node];
    uint32_t offset = 0, length = 0;
    if (n->marker == USFM_M_UNKNOWN) usfm__node_name(p, f->node, &offset, &length);
    return usfm__closer_matches(p, closer, n->marker, offset, length);
}

/* Does the end marker for (marker, name) appear before the span must
 * end anyway: at a paragraph-level marker, a verse, a cell, or (inside
 * a note) the next note part or the note's end? Scans a copy of the
 * scanner, so it consumes nothing. Bounded by the input length; the
 * cache makes repeated questions about one open span O(1). */
static int usfm__closer_ahead(USFM_Parser *p, USFM_Marker marker, uint32_t name_offset,
                              uint32_t name_length, uint32_t cache_node)
{
    USFM_Scanner s;
    USFM_ScanToken t;
    int in_note = usfm__find_kind(p, USFM_AST_NOTE) != 0;
    if (cache_node != USFM_NIL && p->lookahead_node == cache_node &&
        p->scan.position < p->lookahead_until) {
        return 1;
    }
    s = p->scan;
    s.pipe_is_attr = 0;
    while (usfm__scan_next(&s, &t)) {
        uint8_t role;
        if (t.tok.kind == USFM_TOK_MARKER_END) {
            if (usfm__closer_matches(p, &t, marker, name_offset, name_length)) {
                if (cache_node != USFM_NIL) {
                    p->lookahead_node = cache_node;
                    p->lookahead_until = t.tok.span.offset;
                }
                return 1;
            }
            if (in_note && t.row != USFM_ROW_NONE &&
                USFM_MarkerRows[t.row].role == USFM_ROLE_NOTE) {
                return 0;
            }
            continue;
        }
        if (t.tok.kind != USFM_TOK_MARKER || t.row == USFM_ROW_NONE) continue;
        role = USFM_MarkerRows[t.row].role;
        if (role == USFM_ROLE_CHAR || role == USFM_ROLE_NOTE || role == USFM_ROLE_FIGURE ||
            role == USFM_ROLE_MILESTONE) {
            continue;
        }
        if (role == USFM_ROLE_NOTE_PART && !in_note) continue;
        return 0;
    }
    return 0;
}

static void usfm__on_char(USFM_Parser *p, const USFM_ScanToken *t)
{
    const USFM_Frame *top;
    usfm__ensure_inline(p, t->tok.span.offset);
    top = &p->stack[p->depth - 1];
    if (top->kind == USFM_AST_CHAR && !(top->flags & USFM_FRAME_NOTE_PART) &&
        !(t->tok.level_flags & USFM_TF_NESTED)) {
        const USFM_Node *outer = &p->ast->nodes[top->node];
        uint32_t offset = 0, length = 0;
        if (outer->marker == USFM_M_UNKNOWN) usfm__node_name(p, top->node, &offset, &length);
        if (!usfm__closer_ahead(p, outer->marker, offset, length, top->node)) {
            /* 2.x semantics: a plain character marker closes every open
             * span (but not note parts). Bounded by the stack depth. */
            while (p->depth > 1) {
                const USFM_Frame *f = &p->stack[p->depth - 1];
                if (f->kind != USFM_AST_CHAR || (f->flags & USFM_FRAME_NOTE_PART)) break;
                usfm__close_implicit(p);
            }
        }
    }
    usfm__open_tok(p, USFM_AST_CHAR, t, t->tok.level_flags, 0);
}

static void usfm__on_attributes(USFM_Parser *p, const USFM_ScanToken *t)
{
    uint8_t k = usfm__top_kind(p);
    if (k == USFM_AST_CHAR || k == USFM_AST_MILESTONE || k == USFM_AST_FIGURE ||
        k == USFM_AST_PERIPHERAL) {
        usfm__append(p, USFM_AST_ATTRIBUTES, t->tok.span, USFM_M_NONE, 0);
    } else {
        usfm__on_text(p, t);
    }
}
```

Replace the placeholder `usfm__on_close` with:

```c
static void usfm__on_close(USFM_Parser *p, const USFM_ScanToken *t)
{
    uint32_t d, end = usfm__tok_end(t);
    if (t->name_length == 0) {          /* "\*" ends a milestone */
        if (usfm__top_kind(p) == USFM_AST_MILESTONE) {
            p->ast->nodes[p->stack[p->depth - 1].node].level_flags |= USFM_TF_SELF_CLOSED;
            if (end > p->last_end) p->last_end = end;
            usfm__close_top(p);
        } else {
            usfm__diag(p, USFM_DIAGNOSTIC_UNEXPECTED_CLOSE, t->tok.span, USFM_M_NONE);
        }
        return;
    }
    /* Search open spans in the current paragraph or note. Bounded by
     * the stack depth. */
    for (d = p->depth; d > 1; d--) {
        const USFM_Frame *f = &p->stack[d - 1];
        if (f->kind != USFM_AST_CHAR && f->kind != USFM_AST_NOTE && f->kind != USFM_AST_FIGURE) break;
        if (usfm__frame_matches(p, f, t)) {
            while (p->depth > d) usfm__close_implicit(p);
            if (end > p->last_end) p->last_end = end;
            usfm__close_top(p);
            return;
        }
        if (f->kind == USFM_AST_NOTE) break;
    }
    /* \fr* after \fr already closed: explicit close of a note part. */
    if (t->row != USFM_ROW_NONE && USFM_MarkerRows[t->row].role == USFM_ROLE_NOTE_PART) return;
    usfm__diag(p, USFM_DIAGNOSTIC_UNEXPECTED_CLOSE, t->tok.span, t->tok.marker);
}
```

In `usfm__on_marker`, add a case before `default`:

```c
    case USFM_ROLE_CHAR: usfm__on_char(p, t); break;
```

In `usfm__parse`, change the ATTRIBUTES case to:

```c
        case USFM_TOK_ATTRIBUTES: usfm__on_attributes(p, &t); break;
```

- [ ] **Step 4: Run the tests**

Run: `./build.sh test`
Expected: `ok: N checks`.

- [ ] **Step 5: Commit**

```bash
git add usfm.h tests/test_chars.c tests/run_tests.c
git commit -m "feat: character spans with version-independent closing rules

Plain character markers nest when the outer end marker appears before
the paragraph ends (bounded, cached lookahead), otherwise close the open
spans as in USFM 2.x. End markers close everything above the matching
span; stray end markers are diagnosed and dropped.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Notes

**Files:**
- Modify: `usfm.h` (Parser section)
- Create: `tests/test_notes.c`
- Modify: `tests/run_tests.c`

- [ ] **Step 1: Write the failing tests `tests/test_notes.c`**

```c
static void test_note_parts_close_each_other(void)
{
    CHECK_TREE("\\p a\\f + \\fr 1.1 \\ft note\\f* b",
        "BOOK\n"
        "  PARA p\n"
        "    TEXT \"a\"\n"
        "    NOTE f\n"
        "      ARGUMENT \"+\"\n"
        "      CHAR fr\n"
        "        TEXT \"1.1 \"\n"
        "      CHAR ft\n"
        "        TEXT \"note\"\n"
        "    TEXT \" b\"\n");
    CHECK_DIAGS("\\p a\\f + \\fr 1.1 \\ft note\\f* b", USFM_VERSION_AUTO, "");
}

static void test_note_missing_caller(void)
{
    CHECK_TREE("\\p \\f \\ft x\\f*",
        "BOOK\n"
        "  PARA p\n"
        "    NOTE f\n"
        "      CHAR ft\n"
        "        TEXT \"x\"\n");
    CHECK_DIAGS("\\p \\f \\ft x\\f*", USFM_VERSION_AUTO, "MISSING_CALLER 6\n");
}

static void test_note_unclosed_at_paragraph(void)
{
    CHECK_TREE("\\p a\\f + \\ft x\n\\p b",
        "BOOK\n"
        "  PARA p\n"
        "    TEXT \"a\"\n"
        "    NOTE f\n"
        "      ARGUMENT \"+\"\n"
        "      CHAR ft\n"
        "        TEXT \"x\"\n"
        "  PARA p\n"
        "    TEXT \"b\"\n");
    CHECK_DIAGS("\\p a\\f + \\ft x\n\\p b", USFM_VERSION_AUTO, "UNCLOSED_MARKER 4\n");
}

static void test_note_directly_after_chapter(void)
{
    CHECK_TREE("\\c 1\n\\f + \\ft x\\f*\n\\p y",
        "BOOK\n"
        "  CHAPTER c\n"
        "    ARGUMENT \"1\"\n"
        "  NOTE f\n"
        "    ARGUMENT \"+\"\n"
        "    CHAR ft\n"
        "      TEXT \"x\"\n"
        "  PARA p\n"
        "    TEXT \"y\"\n");
}

static void test_note_closed_by_verse(void)
{
    CHECK_DIAGS("\\c 1\n\\p \\f + \\ft a \\v 2 b\\f*", USFM_VERSION_AUTO,
                "UNCLOSED_MARKER 8\nUNEXPECTED_CLOSE 25\n");
}

static void test_note_part_explicitly_closed(void)
{
    CHECK_TREE("\\p \\f + \\fr 1.1\\fr* \\ft x\\f*",
        "BOOK\n"
        "  PARA p\n"
        "    NOTE f\n"
        "      ARGUMENT \"+\"\n"
        "      CHAR fr\n"
        "        TEXT \"1.1\"\n"
        "      TEXT \" \"\n"
        "      CHAR ft\n"
        "        TEXT \"x\"\n");
    /* A second \ft* after the part closed is ignored silently. */
    CHECK_DIAGS("\\p \\f + \\ft x\\ft*\\ft*\\f*", USFM_VERSION_AUTO, "");
}

static void test_note_inside_char_span(void)
{
    CHECK_TREE("\\p \\w a\\f + \\ft n\\f*\\w*",
        "BOOK\n"
        "  PARA p\n"
        "    CHAR w\n"
        "      TEXT \"a\"\n"
        "      NOTE f\n"
        "        ARGUMENT \"+\"\n"
        "        CHAR ft\n"
        "          TEXT \"n\"\n");
}

static void test_note_char_inside_part(void)
{
    CHECK_TREE("\\p \\f + \\ft a \\nd b\\nd*\\f*",
        "BOOK\n"
        "  PARA p\n"
        "    NOTE f\n"
        "      ARGUMENT \"+\"\n"
        "      CHAR ft\n"
        "        TEXT \"a \"\n"
        "        CHAR nd\n"
        "          TEXT \"b\"\n");
}

static void test_note_part_outside_note_is_char(void)
{
    CHECK_TREE("\\p \\xt Mk 1.1\\xt*",
        "BOOK\n"
        "  PARA p\n"
        "    CHAR xt\n"
        "      TEXT \"Mk 1.1\"\n");
}

static void test_cross_reference(void)
{
    CHECK_TREE("\\p a\\x - \\xo 1.1: \\xt Mk 1.1\\x*",
        "BOOK\n"
        "  PARA p\n"
        "    TEXT \"a\"\n"
        "    NOTE x\n"
        "      ARGUMENT \"-\"\n"
        "      CHAR xo\n"
        "        TEXT \"1.1: \"\n"
        "      CHAR xt\n"
        "        TEXT \"Mk 1.1\"\n");
}

static void run_notes_tests(void)
{
    RUN(test_note_parts_close_each_other);
    RUN(test_note_missing_caller);
    RUN(test_note_unclosed_at_paragraph);
    RUN(test_note_directly_after_chapter);
    RUN(test_note_closed_by_verse);
    RUN(test_note_part_explicitly_closed);
    RUN(test_note_inside_char_span);
    RUN(test_note_char_inside_part);
    RUN(test_note_part_outside_note_is_char);
    RUN(test_cross_reference);
}
```

Offsets:
- `test_note_closed_by_verse`: `\c 1\n` is 0–4, `\p ` is 5–7 and the note `\f` is at 8. Then `+ `, `\ft `, `a `, `\v `, `2 `, `b`, so `\f*` is at 25.
- `test_note_missing_caller`: the `\f ` token ends at 6.

In `tests/run_tests.c`, add `#include "test_notes.c"` after `test_chars.c`, and in `main`:

```c
    fprintf(stderr, "notes\n");
    run_notes_tests();
```

- [ ] **Step 2: Run to verify it fails**

Run: `./build.sh test`
Expected: FAIL. Notes are still MILESTONE leaves.

- [ ] **Step 3: Implement notes**

Insert after `usfm__on_attributes`:

```c
static void usfm__on_note(USFM_Parser *p, const USFM_ScanToken *t)
{
    USFM_Span arg;
    uint32_t note = usfm__find_kind(p, USFM_AST_NOTE);
    if (note != 0) usfm__close_to_depth(p, note - 1);   /* notes can't nest */
    /* No implied paragraph: USFM 3.1 allows notes directly after \c. */
    if (usfm__open_tok(p, USFM_AST_NOTE, t, USFM_LEVEL(t->tok.level_flags), 0) == USFM_NIL) return;
    if (!usfm__take_argument(p, &arg, usfm__tok_end(t))) {
        usfm__diag(p, USFM_DIAGNOSTIC_MISSING_CALLER, arg, t->tok.marker);
    }
}

static void usfm__on_note_part(USFM_Parser *p, const USFM_ScanToken *t)
{
    uint32_t note = usfm__find_kind(p, USFM_AST_NOTE);
    if (note == 0) {                    /* \xt outside a note (3.1) */
        usfm__on_char(p, t);
        return;
    }
    /* Close the previous part and every span opened inside it. */
    usfm__close_to_depth(p, note);
    usfm__open_tok(p, USFM_AST_CHAR, t, USFM_LEVEL(t->tok.level_flags), USFM_FRAME_NOTE_PART);
}
```

In `usfm__on_marker`, add before `default`:

```c
    case USFM_ROLE_NOTE: usfm__on_note(p, t); break;
    case USFM_ROLE_NOTE_PART: usfm__on_note_part(p, t); break;
```

- [ ] **Step 4: Run the tests**

Run: `./build.sh test`
Expected: `ok: N checks`.

- [ ] **Step 5: Commit**

```bash
git add usfm.h tests/test_notes.c tests/run_tests.c
git commit -m "feat: footnotes and cross references

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Tables, milestones, unknown markers, sidebars, peripherals, figures

**Files:**
- Modify: `usfm.h` (Parser section)
- Create: `tests/test_structure.c`
- Modify: `tests/run_tests.c`

- [ ] **Step 1: Write the failing tests `tests/test_structure.c`**

```c
static void test_table(void)
{
    CHECK_TREE("\\tr \\th1 A\\th2 B\n\\tr \\tc1 1\\tc2 2\n\\p after",
        "BOOK\n"
        "  TABLE\n"
        "    ROW tr\n"
        "      CELL th1\n"
        "        TEXT \"A\"\n"
        "      CELL th2\n"
        "        TEXT \"B\"\n"
        "    ROW tr\n"
        "      CELL tc1\n"
        "        TEXT \"1\"\n"
        "      CELL tc2\n"
        "        TEXT \"2\"\n"
        "  PARA p\n"
        "    TEXT \"after\"\n");
}

static void test_cell_outside_table(void)
{
    CHECK_TREE("\\c 1\n\\tc1 x",
        "BOOK\n"
        "  CHAPTER c\n"
        "    ARGUMENT \"1\"\n"
        "  TABLE\n"
        "    ROW tr\n"
        "      CELL tc1\n"
        "        TEXT \"x\"\n");
    CHECK_DIAGS("\\c 1\n\\tc1 x", USFM_VERSION_AUTO, "INVALID_NESTING 5\n");
}

static void test_milestones(void)
{
    CHECK_TREE("\\p a\\qt-s |who=\"Pilate\"\\*b\\qt-e\\*",
        "BOOK\n"
        "  PARA p\n"
        "    TEXT \"a\"\n"
        "    MILESTONE qt-s closed\n"
        "      ATTRIBUTES \"|who=\\\"Pilate\\\"\"\n"
        "    TEXT \"b\"\n"
        "    MILESTONE qt-e closed\n");
    /* \ts without \* is still a milestone; the next token ends it. */
    CHECK_TREE("\\p a\\ts b",
        "BOOK\n"
        "  PARA p\n"
        "    TEXT \"a\"\n"
        "    MILESTONE ts\n"
        "    TEXT \"b\"\n");
}

static void test_unknown_markers(void)
{
    CHECK_TREE("\\zp line start\n\\p a \\zx b \\zc c\\zc* \\zm\\*",
        "BOOK\n"
        "  PARA ?zp\n"
        "    TEXT \"line start\"\n"
        "  PARA p\n"
        "    TEXT \"a \"\n"
        "    MILESTONE ?zx\n"
        "    TEXT \"b \"\n"
        "    CHAR ?zc\n"
        "      TEXT \"c\"\n"
        "    TEXT \" \"\n"
        "    MILESTONE ?zm closed\n");
    CHECK_DIAGS("\\zp line start\n\\p a \\zx b \\zc c\\zc* \\zm\\*", USFM_VERSION_AUTO, "");
    CHECK_DIAGS("\\p \\xyz a", USFM_VERSION_AUTO, "UNKNOWN_MARKER 3\n");
}

static void test_sidebar(void)
{
    CHECK_TREE("\\esb\n\\p in\n\\esbe\n\\p out",
        "BOOK\n"
        "  SIDEBAR esb\n"
        "    PARA p\n"
        "      TEXT \"in\"\n"
        "  PARA p\n"
        "    TEXT \"out\"\n");
    CHECK_DIAGS("\\p a\\esbe", USFM_VERSION_AUTO, "UNEXPECTED_CLOSE 4\n");
}

static void test_peripheral(void)
{
    CHECK_TREE("\\id FRT\n\\periph Title Page|id=\"title\"\n\\p x",
        "BOOK\n"
        "  HEADER id\n"
        "    ARGUMENT \"FRT\"\n"
        "  PERIPHERAL periph\n"
        "    ARGUMENT \"Title Page\"\n"
        "    ATTRIBUTES \"|id=\\\"title\\\"\"\n"
        "    PARA p\n"
        "      TEXT \"x\"\n");
}

static void test_figure(void)
{
    CHECK_TREE("\\p \\fig At once|src=\"a.jpg\" size=\"span\"\\fig*",
        "BOOK\n"
        "  PARA p\n"
        "    FIGURE fig\n"
        "      TEXT \"At once\"\n"
        "      ATTRIBUTES \"|src=\\\"a.jpg\\\" size=\\\"span\\\"\"\n");
}

static void run_structure_tests(void)
{
    RUN(test_table);
    RUN(test_cell_outside_table);
    RUN(test_milestones);
    RUN(test_unknown_markers);
    RUN(test_sidebar);
    RUN(test_peripheral);
    RUN(test_figure);
}
```

In `tests/run_tests.c`, add `#include "test_structure.c"` after `test_notes.c`, and in `main`:

```c
    fprintf(stderr, "structure\n");
    run_structure_tests();
```

- [ ] **Step 2: Run to verify it fails**

Run: `./build.sh test`
Expected: FAIL with tree mismatches for every new test.

- [ ] **Step 3: Implement the remaining roles**

Insert after `usfm__on_note_part`:

```c
static void usfm__on_milestone(USFM_Parser *p, const USFM_ScanToken *t)
{
    /* Open until "\*" or any other token arrives (see usfm__parse).
     * Milestones may sit anywhere, so there is no implied paragraph. */
    usfm__open_tok(p, USFM_AST_MILESTONE, t, t->tok.level_flags, 0);
}

static void usfm__on_row(USFM_Parser *p, const USFM_ScanToken *t)
{
    usfm__trim_last_text(p);
    usfm__close_to_block(p, 1);
    if (usfm__top_kind(p) != USFM_AST_TABLE) {
        usfm__open(p, USFM_AST_TABLE, usfm__span(t->tok.span.offset, 0), USFM_M_NONE, 0, 0);
    }
    usfm__open_tok(p, USFM_AST_ROW, t, USFM_LEVEL(t->tok.level_flags), 0);
}

static void usfm__on_cell(USFM_Parser *p, const USFM_ScanToken *t)
{
    uint32_t row;
    usfm__trim_last_text(p);
    row = usfm__find_kind(p, USFM_AST_ROW);
    if (row == 0) {
        USFM_Span at = usfm__span(t->tok.span.offset, 0);
        usfm__diag(p, USFM_DIAGNOSTIC_INVALID_NESTING, t->tok.span, t->tok.marker);
        usfm__close_to_block(p, 1);
        if (usfm__top_kind(p) != USFM_AST_TABLE) {
            usfm__open(p, USFM_AST_TABLE, at, USFM_M_NONE, 0, 0);
        }
        usfm__open(p, USFM_AST_ROW, at, USFM_M_TR, 0, 0);
    } else {
        usfm__close_to_depth(p, row);   /* the previous cell */
    }
    usfm__open_tok(p, USFM_AST_CELL, t, USFM_LEVEL(t->tok.level_flags), 0);
}

static void usfm__on_sidebar(USFM_Parser *p, const USFM_ScanToken *t)
{
    uint32_t open = usfm__find_kind(p, USFM_AST_SIDEBAR);
    usfm__trim_last_text(p);
    if (open != 0) usfm__close_to_depth(p, open - 1);   /* sidebars can't nest */
    usfm__close_to_block(p, 0);
    usfm__open_tok(p, USFM_AST_SIDEBAR, t, 0, 0);
}

static void usfm__on_sidebar_end(USFM_Parser *p, const USFM_ScanToken *t)
{
    uint32_t open = usfm__find_kind(p, USFM_AST_SIDEBAR);
    uint32_t end = usfm__tok_end(t);
    usfm__trim_last_text(p);
    if (open == 0) {
        usfm__diag(p, USFM_DIAGNOSTIC_UNEXPECTED_CLOSE, t->tok.span, t->tok.marker);
        return;
    }
    usfm__close_to_depth(p, open);
    if (end > p->last_end) p->last_end = end;
    usfm__close_top(p);
}

static void usfm__on_periph(USFM_Parser *p, const USFM_ScanToken *t)
{
    USFM_Span arg;
    usfm__trim_last_text(p);
    usfm__close_to_depth(p, 1);
    if (usfm__open_tok(p, USFM_AST_PERIPHERAL, t, 0, 0) == USFM_NIL) return;
    usfm__take_argument(p, &arg, usfm__tok_end(t));
}

static void usfm__on_figure(USFM_Parser *p, const USFM_ScanToken *t)
{
    usfm__open_tok(p, USFM_AST_FIGURE, t, 0, 0);
}

/* Is offset the first non-blank byte of its line? Scans back over
 * spaces and tabs only, so the total work over a parse stays linear. */
static int usfm__at_line_start(const char *in, uint32_t offset)
{
    while (offset > 0 && (in[offset - 1] == ' ' || in[offset - 1] == '\t')) offset--;
    return offset == 0 || in[offset - 1] == '\n' || in[offset - 1] == '\r';
}

/* Guide section 2: followed by "\*" or "|" -> milestone; at line start
 * -> paragraph; a matching \name* ahead -> character span; otherwise an
 * inline milestone leaf. The text after it is never dropped. */
static void usfm__on_unknown(USFM_Parser *p, const USFM_ScanToken *t)
{
    const char *in = p->ast->input;
    uint32_t len = p->ast->input_length, after = usfm__tok_end(t);
    if (t->name_length == 0 || in[t->name_offset] != 'z') {
        usfm__diag(p, USFM_DIAGNOSTIC_UNKNOWN_MARKER, t->tok.span, USFM_M_UNKNOWN);
    }
    if (after < len &&
        (in[after] == '|' || (in[after] == '\\' && after + 1 < len && in[after + 1] == '*'))) {
        usfm__on_milestone(p, t);
        return;
    }
    if (usfm__at_line_start(in, t->tok.span.offset)) {
        usfm__trim_last_text(p);
        usfm__close_to_block(p, 0);
        usfm__open_tok(p, USFM_AST_PARA, t, 0, 0);
        return;
    }
    if (t->name_length > 0 &&
        usfm__closer_ahead(p, USFM_M_UNKNOWN, t->name_offset, t->name_length, USFM_NIL)) {
        usfm__on_char(p, t);
        return;
    }
    usfm__append(p, USFM_AST_MILESTONE, t->tok.span, USFM_M_UNKNOWN, 0);
}
```

Replace `usfm__on_marker` with the full dispatch:

```c
static void usfm__on_marker(USFM_Parser *p, USFM_ScanToken *t)
{
    if (USFM_FLAGS(t->tok.level_flags) & (USFM_TF_MILESTONE_START | USFM_TF_MILESTONE_END)) {
        usfm__on_milestone(p, t);
        return;
    }
    if (t->row == USFM_ROW_NONE) {
        usfm__on_unknown(p, t);
        return;
    }
    switch (USFM_MarkerRows[t->row].role) {
    case USFM_ROLE_PARA: usfm__on_para(p, t, USFM_AST_PARA); break;
    case USFM_ROLE_HEADER: usfm__on_para(p, t, USFM_AST_HEADER); break;
    case USFM_ROLE_ID: usfm__on_id(p, t); break;
    case USFM_ROLE_CHAPTER: usfm__on_chapter(p, t); break;
    case USFM_ROLE_VERSE: usfm__on_verse(p, t); break;
    case USFM_ROLE_CHAR: usfm__on_char(p, t); break;
    case USFM_ROLE_NOTE: usfm__on_note(p, t); break;
    case USFM_ROLE_NOTE_PART: usfm__on_note_part(p, t); break;
    case USFM_ROLE_ROW: usfm__on_row(p, t); break;
    case USFM_ROLE_CELL: usfm__on_cell(p, t); break;
    case USFM_ROLE_MILESTONE: usfm__on_milestone(p, t); break;
    case USFM_ROLE_SIDEBAR: usfm__on_sidebar(p, t); break;
    case USFM_ROLE_SIDEBAR_END: usfm__on_sidebar_end(p, t); break;
    case USFM_ROLE_PERIPH: usfm__on_periph(p, t); break;
    case USFM_ROLE_FIGURE: usfm__on_figure(p, t); break;
    default: usfm__on_unknown(p, t); break;
    }
}
```

In `usfm__parse`, close a pending milestone before any token that can't belong to it. Replace the loop body with:

```c
    while (!p->failed && usfm__next(p, &t)) {
        if (usfm__top_kind(p) == USFM_AST_MILESTONE) {
            int keep = t.tok.kind == USFM_TOK_ATTRIBUTES ||
                       (t.tok.kind == USFM_TOK_MARKER_END && t.name_length == 0) ||
                       (t.tok.kind == USFM_TOK_TEXT && usfm__is_blank(p->ast->input, t.tok.span));
            if (!keep) usfm__close_top(p);
        }
        switch (t.tok.kind) {
        case USFM_TOK_TEXT: usfm__on_text(p, &t); break;
        case USFM_TOK_ARGUMENT: usfm__on_text(p, &t); break;   /* unclaimed */
        case USFM_TOK_ATTRIBUTES: usfm__on_attributes(p, &t); break;
        case USFM_TOK_OPTBREAK: usfm__on_optbreak(p, &t); break;
        case USFM_TOK_MARKER_END: usfm__on_close(p, &t); break;
        case USFM_TOK_MARKER: usfm__on_marker(p, &t); break;
        default: break;
        }
    }
```

- [ ] **Step 4: Run the tests**

Run: `./build.sh test`
Expected: `ok: N checks`.

- [ ] **Step 5: Commit**

```bash
git add usfm.h tests/test_structure.c tests/run_tests.c
git commit -m "feat: tables, milestones, sidebars, peripherals, figures, unknown markers

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9: Versions and depth limits

**Files:**
- Create: `tests/test_versions.c`
- Modify: `tests/run_tests.c`
- (Modify `usfm.h` only if a test fails. Everything tested here was implemented in Tasks 5–8; this task pins it down.)

- [ ] **Step 1: Write the tests `tests/test_versions.c`**

```c
static void test_usfm_line_selects_version(void)
{
    const char *v24 = "\\id GEN\n\\usfm 2.4\n\\p \\add a \\nd b";
    const char *v31 = "\\id GEN\n\\usfm 3.1\n\\p \\add a \\nd b";
    USFM_Ast *ast = parse_with_version(v24, USFM_VERSION_AUTO);
    CHECK(ast != NULL);
    if (ast != NULL) {
        CHECK_EQ(USFM_Ast_GetVersion(ast), USFM_VERSION_2);
        USFM_Ast_Free(ast);
    }
    CHECK_DIAGS(v24, USFM_VERSION_AUTO, "");
    CHECK_DIAGS(v31, USFM_VERSION_AUTO, "UNCLOSED_MARKER 21\nUNCLOSED_MARKER 28\n");
    /* An explicit config version wins over \usfm. */
    CHECK_DIAGS(v24, USFM_VERSION_3_1, "UNCLOSED_MARKER 21\nUNCLOSED_MARKER 28\n");
}

static void test_unsupported_version(void)
{
    USFM_Ast *ast = parse_with_version("\\usfm 4.0\n", USFM_VERSION_AUTO);
    CHECK(ast != NULL);
    if (ast != NULL) {
        CHECK_EQ(USFM_Ast_GetVersion(ast), USFM_VERSION_3_1);
        USFM_Ast_Free(ast);
    }
    CHECK_DIAGS("\\usfm 4.0\n", USFM_VERSION_AUTO, "UNSUPPORTED_VERSION 6\n");
}

static void test_depth_limit(void)
{
    /* BOOK + PARA + 62 nested spans fill the 64-deep stack; the
     * remaining 8 markers become text with a TOO_DEEP diagnostic. */
    char input[1024];
    size_t used = 0;
    uint32_t count, too_deep = 0;
    const USFM_Diagnostic *diags;
    USFM_Ast *ast;
    used += (size_t)snprintf(input + used, sizeof input - used, "\\p ");
    for (int i = 0; i < 70; i++) {
        used += (size_t)snprintf(input + used, sizeof input - used, "\\+nd x");
    }
    ast = parse_with_version(input, USFM_VERSION_AUTO);
    CHECK(ast != NULL);
    if (ast == NULL) return;
    CHECK_EQ(check_ast(ast), 0);
    diags = USFM_Ast_GetDiagnostics(ast, &count);
    for (uint32_t i = 0; i < count; i++) {
        if (diags[i].code == USFM_DIAGNOSTIC_TOO_DEEP) too_deep++;
    }
    CHECK_EQ(too_deep, 8);
    USFM_Ast_Free(ast);
}

static void test_verse_closes_heading(void)
{
    CHECK_TREE("\\c 1\n\\s1 Head \\v 1 x",
        "BOOK\n"
        "  CHAPTER c\n"
        "    ARGUMENT \"1\"\n"
        "  PARA s1 heading\n"
        "    TEXT \"Head\"\n"
        "  PARA p implied\n"
        "    VERSE v\n"
        "      ARGUMENT \"1\"\n"
        "    TEXT \"x\"\n");
}

static void run_versions_tests(void)
{
    RUN(test_usfm_line_selects_version);
    RUN(test_unsupported_version);
    RUN(test_depth_limit);
    RUN(test_verse_closes_heading);
}
```

The byte offsets come from these lines: `\id GEN\n` is 0–7, `\usfm 2.4\n` is 8–17, `\p ` is 18–20, `\add ` is 21–25, `a ` is 26–27 and `\nd ` starts at 28.

In `tests/run_tests.c`, add `#include "test_versions.c"` after `test_structure.c`, and in `main`:

```c
    fprintf(stderr, "versions\n");
    run_versions_tests();
```

- [ ] **Step 2: Run the tests**

Run: `./build.sh test`
Expected: `ok: N checks`. If one fails, the printed expected and actual output says which rule disagrees. Fix `usfm.h`, not the test, unless the test contradicts the spec's closing-rules table.

- [ ] **Step 3: Commit**

```bash
git add tests/test_versions.c tests/run_tests.c usfm.h
git commit -m "test: version selection, unsupported versions and the depth limit

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 10: Chapter/verse lookups and iterators

**Files:**
- Modify: `usfm.h` (Lookups section; Iterators section)
- Create: `tests/test_iter.c`
- Modify: `tests/run_tests.c`

- [ ] **Step 1: Write the failing tests `tests/test_iter.c`**

```c
static const char *iter_doc =
    "\\c 1\n\\p\n\\v 1 a\n\\v 2 b \\f + \\ft n\\f* c\n\\s1 H\n\\p\n\\v 3 d\n"
    "\\c 2\n\\p\n\\v 1 e\n";

/* Concatenates the TEXT items of verse (chapter, verse) under flags. */
static void verse_text(const USFM_Ast *ast, uint16_t chapter, uint16_t verse,
                       uint32_t flags, char *out, size_t cap)
{
    USFM_VerseEntry entry;
    USFM_VerseIter it;
    USFM_VerseItem item;
    uint32_t input_length;
    const char *input = USFM_Ast_GetInput(ast, &input_length);
    size_t len = 0;
    out[0] = '\0';
    if (USFM_Ast_FindVerse(ast, chapter, verse, &entry) == USFM_NIL) return;
    USFM_VerseIter_Init(&it, ast, &entry, flags);
    while (USFM_VerseIter_Next(&it, &item)) {
        if (item.kind != USFM_VITEM_TEXT) continue;
        if (len + item.text.length + 1 > cap) break;
        memcpy(out + len, input + item.text.offset, item.text.length);
        len += item.text.length;
        out[len] = '\0';
    }
}

static void test_find_chapter_and_verse(void)
{
    USFM_Ast *ast = parse_with_version(iter_doc, USFM_VERSION_AUTO);
    USFM_ChapterEntry chapter;
    USFM_VerseEntry verse;
    uint32_t count;
    const USFM_Node *nodes = USFM_Ast_GetNodes(ast, &count);
    CHECK(USFM_Ast_FindChapter(ast, 2, &chapter) != USFM_NIL);
    CHECK_EQ(chapter.number, 2);
    CHECK_EQ(nodes[chapter.node].kind, USFM_AST_CHAPTER);
    CHECK_EQ(USFM_Ast_FindChapter(ast, 3, &chapter), USFM_NIL);
    CHECK(USFM_Ast_FindVerse(ast, 1, 2, &verse) != USFM_NIL);
    CHECK_EQ(verse.ref.verse_first, 2);
    CHECK_EQ(USFM_Ast_FindVerse(ast, 1, 9, &verse), USFM_NIL);
    CHECK_EQ(USFM_Ast_FindVerse(ast, 5, 1, &verse), USFM_NIL);
    USFM_Ast_Free(ast);
}

static void test_find_verse_in_range(void)
{
    USFM_Ast *ast = parse_with_version("\\c 1\n\\p \\v 1 a \\v 2-4 b \\v 5 c", USFM_VERSION_AUTO);
    USFM_VerseEntry verse;
    CHECK(USFM_Ast_FindVerse(ast, 1, 3, &verse) != USFM_NIL);
    CHECK_EQ(verse.ref.verse_first, 2);
    CHECK_EQ(verse.ref.verse_last, 4);
    CHECK(USFM_Ast_FindVerse(ast, 1, 5, &verse) != USFM_NIL);
    CHECK_EQ(verse.ref.verse_first, 5);
    USFM_Ast_Free(ast);
}

static void test_find_unsorted_and_repeated_chapters(void)
{
    USFM_Ast *ast = parse_with_version("\\c 2\n\\c 1\n\\c 1\n", USFM_VERSION_AUTO);
    USFM_ChapterEntry chapter;
    uint32_t count;
    const USFM_ChapterEntry *chapters = USFM_Ast_GetChapters(ast, &count);
    CHECK_EQ(count, 3);
    CHECK_EQ(USFM_Ast_FindChapter(ast, 1, &chapter), chapters[1].node);
    CHECK_EQ(USFM_Ast_FindChapter(ast, 2, &chapter), chapters[0].node);
    USFM_Ast_Free(ast);
}

static void test_verse_iterator_flags(void)
{
    USFM_Ast *ast = parse_with_version(iter_doc, USFM_VERSION_AUTO);
    char text[256];
    verse_text(ast, 1, 1, 0, text, sizeof text);
    CHECK_STR(text, "a");
    verse_text(ast, 1, 2, 0, text, sizeof text);
    CHECK_STR(text, "b  c");
    verse_text(ast, 1, 2, USFM_VI_INCLUDE_NOTES, text, sizeof text);
    CHECK_STR(text, "b n c");
    verse_text(ast, 1, 2, USFM_VI_INCLUDE_HEADINGS, text, sizeof text);
    CHECK_STR(text, "b  cH");
    verse_text(ast, 1, 3, 0, text, sizeof text);
    CHECK_STR(text, "d");
    verse_text(ast, 2, 1, 0, text, sizeof text);
    CHECK_STR(text, "e");
    USFM_Ast_Free(ast);
}

static void test_verse_iterator_items(void)
{
    USFM_Ast *ast = parse_with_version(iter_doc, USFM_VERSION_AUTO);
    USFM_VerseEntry entry;
    USFM_VerseIter it;
    USFM_VerseItem item;
    uint32_t count, texts = 0, notes = 0, breaks = 0;
    const USFM_Node *nodes = USFM_Ast_GetNodes(ast, &count);
    USFM_Ast_FindVerse(ast, 1, 2, &entry);
    USFM_VerseIter_Init(&it, ast, &entry,
                        USFM_VI_REPORT_NOTES | USFM_VI_REPORT_BREAKS | USFM_VI_INCLUDE_NOTES);
    while (USFM_VerseIter_Next(&it, &item)) {
        if (item.kind == USFM_VITEM_TEXT) {
            texts++;
            CHECK(item.para != USFM_NIL);
            CHECK_EQ(nodes[item.para].kind, USFM_AST_PARA);
            if (item.in_note) {
                CHECK(item.style != USFM_NIL);
                CHECK_EQ(nodes[item.style].marker, USFM_M_FT);
            }
        } else if (item.kind == USFM_VITEM_NOTE) {
            notes++;
            CHECK_EQ(nodes[item.node].kind, USFM_AST_NOTE);
        } else if (item.kind == USFM_VITEM_PARA_BREAK) {
            breaks++;
        }
    }
    CHECK_EQ(texts, 3);         /* "b ", "n", " c" */
    CHECK_EQ(notes, 1);
    CHECK_EQ(breaks, 1);        /* the \p before verse 3; \s1 is skipped */
    USFM_Ast_Free(ast);
}

static void test_chapter_iterators(void)
{
    USFM_Ast *ast = parse_with_version(iter_doc, USFM_VERSION_AUTO);
    USFM_ChapterIter chapters;
    USFM_ChapterEntry chapter;
    uint32_t chapter_count = 0, verse_counts[2] = {0, 0};
    USFM_ChapterIter_Init(&chapters, ast);
    while (USFM_ChapterIter_Next(&chapters, &chapter)) {
        USFM_ChapterVerseIter verses;
        USFM_VerseEntry verse;
        USFM_ChapterVerseIter_Init(&verses, ast, &chapter);
        while (USFM_ChapterVerseIter_Next(&verses, &verse)) {
            CHECK_EQ(verse.ref.chapter, chapter.number);
            if (chapter_count < 2) verse_counts[chapter_count]++;
        }
        chapter_count++;
    }
    CHECK_EQ(chapter_count, 2);
    CHECK_EQ(verse_counts[0], 3);
    CHECK_EQ(verse_counts[1], 1);
    USFM_Ast_Free(ast);
}

static void run_iter_tests(void)
{
    RUN(test_find_chapter_and_verse);
    RUN(test_find_verse_in_range);
    RUN(test_find_unsorted_and_repeated_chapters);
    RUN(test_verse_iterator_flags);
    RUN(test_verse_iterator_items);
    RUN(test_chapter_iterators);
}
```

Why verse 2 reads `"b  c"`: the text before the note keeps its trailing space (`"b "`, since trimming happens only before paragraph-level markers, `\v` and the end of input). The note is skipped, and the text after it starts with a space (`" c"`).

In `tests/run_tests.c`, add `#include "test_iter.c"` after `test_versions.c`, and in `main`:

```c
    fprintf(stderr, "iterators\n");
    run_iter_tests();
```

- [ ] **Step 2: Run to verify it fails**

Run: `./build.sh test`
Expected: link errors: undefined `USFM_Ast_FindVerse`, `USFM_VerseIter_Init` and the other functions.

- [ ] **Step 3: Implement lookups**

Append to the Lookups section:

```c
static uint32_t usfm__find_chapter_index(const USFM_Ast *ast, uint16_t number)
{
    uint32_t lo = 0, hi = ast->chapter_count, i;
    if (ast->chapters_sorted) {
        /* Bounded: the range halves each pass. */
        while (lo < hi) {
            uint32_t mid = lo + (hi - lo) / 2u;
            if (ast->chapters[mid].number < number) lo = mid + 1;
            else hi = mid;
        }
        return (lo < ast->chapter_count && ast->chapters[lo].number == number) ? lo : USFM_NIL;
    }
    for (i = 0; i < ast->chapter_count; i++) {
        if (ast->chapters[i].number == number) return i;
    }
    return USFM_NIL;
}

USFM_API uint32_t USFM_Ast_FindChapter(const USFM_Ast *ast, uint16_t chapter,
                                       USFM_ChapterEntry *out)
{
    uint32_t index;
    if (ast == NULL) return USFM_NIL;
    index = usfm__find_chapter_index(ast, chapter);
    if (index == USFM_NIL) return USFM_NIL;
    if (out) *out = ast->chapters[index];
    return ast->chapters[index].node;
}

USFM_API uint32_t USFM_Ast_FindVerse(const USFM_Ast *ast, uint16_t chapter, uint16_t verse,
                                     USFM_VerseEntry *out)
{
    uint32_t ci, first, end, i, found = USFM_NIL;
    if (ast == NULL) return USFM_NIL;
    ci = usfm__find_chapter_index(ast, chapter);
    if (ci == USFM_NIL) return USFM_NIL;
    first = ast->chapters[ci].first_verse;
    end = first + ast->chapters[ci].verse_count;
    if (ast->verses_sorted) {
        /* Last entry whose first verse is <= verse, then check its range. */
        uint32_t lo = first, hi = end;
        while (lo < hi) {
            uint32_t mid = lo + (hi - lo) / 2u;
            if (ast->verses[mid].ref.verse_first <= verse) lo = mid + 1;
            else hi = mid;
        }
        if (lo > first && ast->verses[lo - 1].ref.verse_last >= verse) found = lo - 1;
    } else {
        for (i = first; i < end; i++) {
            if (ast->verses[i].ref.verse_first <= verse && verse <= ast->verses[i].ref.verse_last) {
                found = i;
                break;
            }
        }
    }
    if (found == USFM_NIL) return USFM_NIL;
    if (out) *out = ast->verses[found];
    return ast->verses[found].node;
}
```

- [ ] **Step 4: Implement iterators**

Under `/* ===== Iterators ===== */`:

```c
USFM_API void USFM_ChapterIter_Init(USFM_ChapterIter *it, const USFM_Ast *ast)
{
    it->ast = ast;
    it->position = 0;
    it->end = ast ? ast->chapter_count : 0;
}

USFM_API int USFM_ChapterIter_Next(USFM_ChapterIter *it, USFM_ChapterEntry *chapter)
{
    if (it->ast == NULL || it->position >= it->end) return 0;
    *chapter = it->ast->chapters[it->position++];
    return 1;
}

USFM_API void USFM_ChapterVerseIter_Init(USFM_ChapterVerseIter *it, const USFM_Ast *ast,
                                         const USFM_ChapterEntry *chapter)
{
    it->ast = ast;
    it->position = 0;
    it->end = 0;
    if (ast == NULL || chapter == NULL) return;
    it->position = chapter->first_verse;
    it->end = chapter->first_verse + chapter->verse_count;
    if (it->end > ast->verse_count) it->end = ast->verse_count;
}

USFM_API int USFM_ChapterVerseIter_Next(USFM_ChapterVerseIter *it, USFM_VerseEntry *verse)
{
    if (it->ast == NULL || it->position >= it->end) return 0;
    *verse = it->ast->verses[it->position++];
    return 1;
}

USFM_API void USFM_VerseIter_Init(USFM_VerseIter *it, const USFM_Ast *ast,
                                  const USFM_VerseEntry *entry, uint32_t flags)
{
    it->ast = ast;
    it->flags = flags;
    it->position = 0;
    it->end = 0;
    if (ast == NULL || entry == NULL || entry->node >= ast->node_count) return;
    it->position = ast->nodes[entry->node].end;   /* skip VERSE + ARGUMENT */
    it->end = entry->end_node <= ast->node_count ? entry->end_node : ast->node_count;
}

static void usfm__verse_item(const USFM_Ast *ast, uint32_t node, uint8_t kind,
                             USFM_VerseItem *item)
{
    const USFM_Node *nodes = ast->nodes;
    uint32_t a = nodes[node].parent, steps;
    memset(item, 0, sizeof *item);
    item->kind = kind;
    item->node = node;
    item->para = kind == USFM_VITEM_PARA_BREAK ? node : USFM_NIL;
    item->style = USFM_NIL;
    item->text = (kind == USFM_VITEM_TEXT || kind == USFM_VITEM_OPTBREAK)
                     ? nodes[node].span : usfm__span(nodes[node].span.offset, 0);
    /* Bounded: a node has at most USFM_STACK_DEPTH ancestors. */
    for (steps = 0; a != USFM_NIL && steps <= USFM_STACK_DEPTH; steps++) {
        uint8_t k = nodes[a].kind;
        if (k == USFM_AST_CHAR && item->style == USFM_NIL) item->style = a;
        if (k == USFM_AST_NOTE) item->in_note = 1;
        if (k == USFM_AST_PARA && item->para == USFM_NIL) item->para = a;
        a = nodes[a].parent;
    }
}

USFM_API int USFM_VerseIter_Next(USFM_VerseIter *it, USFM_VerseItem *item)
{
    const USFM_Ast *ast = it->ast;
    if (ast == NULL) return 0;
    /* Bounded: position strictly increases and never passes end. */
    while (it->position < it->end) {
        uint32_t i = it->position;
        const USFM_Node *n = &ast->nodes[i];
        uint32_t skip_to = n->end < it->end ? n->end : it->end;
        switch (n->kind) {
        case USFM_AST_TEXT:
            it->position = i + 1;
            usfm__verse_item(ast, i, USFM_VITEM_TEXT, item);
            return 1;
        case USFM_AST_OPTBREAK:
            it->position = i + 1;
            usfm__verse_item(ast, i, USFM_VITEM_OPTBREAK, item);
            return 1;
        case USFM_AST_NOTE:
            it->position = (it->flags & USFM_VI_INCLUDE_NOTES) ? i + 1 : skip_to;
            if (it->flags & USFM_VI_REPORT_NOTES) {
                usfm__verse_item(ast, i, USFM_VITEM_NOTE, item);
                return 1;
            }
            break;
        case USFM_AST_PARA:
            if ((n->level_flags & USFM_NF_HEADING) && !(it->flags & USFM_VI_INCLUDE_HEADINGS)) {
                it->position = skip_to;
                break;
            }
            it->position = i + 1;
            if (it->flags & USFM_VI_REPORT_BREAKS) {
                usfm__verse_item(ast, i, USFM_VITEM_PARA_BREAK, item);
                return 1;
            }
            break;
        default:                        /* containers: step in; leaves:
                                           ARGUMENT / ATTRIBUTES skipped */
            it->position = i + 1;
            break;
        }
    }
    return 0;
}
```

- [ ] **Step 5: Run the tests**

Run: `./build.sh test`
Expected: `ok: N checks`.

- [ ] **Step 6: Commit**

```bash
git add usfm.h tests/test_iter.c tests/run_tests.c
git commit -m "feat: chapter/verse lookups and allocation-free iterators

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 11: Allocator behaviour of the parser

**Files:**
- Create: `tests/test_parse_alloc.c`
- Modify: `tests/run_tests.c`
- (Modify `usfm.h` only if a test exposes a leak or a wrong size.)

- [ ] **Step 1: Write the tests `tests/test_parse_alloc.c`**

These tests reuse `TestAlc` and `test_alc` from `tests/test_alloc.c`.

```c
static const char *alloc_doc =
    "\\id GEN Test\n\\c 1\n\\s1 Head\n\\p \\v 1 a \\w b|strong=\"H1\"\\w* \\f + \\ft n\\f*\n"
    "\\q1 \\v 2 c \\nd d\\nd*\n\\c 2\n\\p \\v 1 e \\xyz f\n";

static void test_parse_with_custom_allocator(void)
{
    TestAlc state;
    USFM_Alc alc = test_alc(&state, UINT32_MAX);
    USFM_Err err;
    USFM_Ast *ast = USFM_Ast_ParseOpts(alloc_doc, (uint32_t)strlen(alloc_doc), NULL,
                                       USFM_PARSE_DEFAULT, &alc, &err);
    CHECK(ast != NULL);
    CHECK_EQ(err.code, USFM_OK);
    CHECK(state.live_bytes > 0);
    USFM_Ast_Free(ast);
    CHECK_EQ(state.live_bytes, 0);
    CHECK_EQ(state.bad_sizes, 0);
}

static void test_parse_fails_cleanly_at_every_allocation(void)
{
    /* Fail after N successful allocator calls, for every N until a
     * parse succeeds. Each failure must free everything. */
    int succeeded = 0;
    for (uint32_t n = 0; n < 64 && !succeeded; n++) {
        TestAlc state;
        USFM_Alc alc = test_alc(&state, n);
        USFM_Err err;
        USFM_Ast *ast = USFM_Ast_ParseOpts(alloc_doc, (uint32_t)strlen(alloc_doc), NULL,
                                           USFM_PARSE_COPY_INPUT, &alc, &err);
        if (ast == NULL) {
            CHECK_EQ(err.code, USFM_ERR_NOMEM);
            CHECK_EQ(state.live_bytes, 0);
        } else {
            succeeded = 1;
            CHECK_EQ(check_ast(ast), 0);
            USFM_Ast_Free(ast);
            CHECK_EQ(state.live_bytes, 0);
        }
        CHECK_EQ(state.bad_sizes, 0);
    }
    CHECK(succeeded);
}

static void test_dropped_diagnostics_keep_the_tree(void)
{
    /* Calls: 1 = the handle, 2 = the node array, 3 = the diagnostic
     * array. Failing the third drops the diagnostic but keeps the tree. */
    TestAlc state;
    USFM_Alc alc = test_alc(&state, 2);
    USFM_Err err;
    USFM_Ast *ast = USFM_Ast_ParseOpts("\\p a\\nd* b", 10, NULL, USFM_PARSE_DEFAULT, &alc, &err);
    CHECK(ast != NULL);
    if (ast == NULL) return;
    CHECK_EQ(err.code, USFM_ERR_NOMEM);
    CHECK_EQ(err.pos, 4);
    CHECK_EQ(USFM_Ast_GetDroppedDiagnosticCount(ast), 1);
    CHECK_EQ(check_ast(ast), 0);
    USFM_Ast_Free(ast);
    CHECK_EQ(state.live_bytes, 0);
}

static void test_copy_input_is_independent(void)
{
    char buffer[] = "\\p hello";
    USFM_Err err;
    USFM_Ast *ast = USFM_Ast_ParseOpts(buffer, (uint32_t)strlen(buffer), NULL,
                                       USFM_PARSE_COPY_INPUT, NULL, &err);
    CHECK(ast != NULL);
    if (ast == NULL) return;
    memset(buffer, 'X', strlen(buffer));
    dump_tree(ast, helper_buf, sizeof helper_buf);
    CHECK_STR(helper_buf, "BOOK\n  PARA p\n    TEXT \"hello\"\n");
    USFM_Ast_Free(ast);
}

static void run_parse_alloc_tests(void)
{
    RUN(test_parse_with_custom_allocator);
    RUN(test_parse_fails_cleanly_at_every_allocation);
    RUN(test_dropped_diagnostics_keep_the_tree);
    RUN(test_copy_input_is_independent);
}
```

In `tests/run_tests.c`, add `#include "test_parse_alloc.c"` after `test_iter.c`, and in `main`:

```c
    fprintf(stderr, "parse alloc\n");
    run_parse_alloc_tests();
```

- [ ] **Step 2: Run the tests**

Run: `./build.sh test`
Expected:
- `ok: N checks`.
- ASan reports no leaks. A leak report at exit counts as a failure even if every check passes.

- [ ] **Step 3: Commit**

```bash
git add tests/test_parse_alloc.c tests/run_tests.c usfm.h
git commit -m "test: allocation failure at every call frees everything

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 12: WEB corpus test

**Files:**
- Create: `tests/corpus/WEB.tar.gz`, `tests/corpus/paratextdata-web.csv`
- Create: `tests/corpus_test.c`
- Modify: `build.sh`

- [ ] **Step 1: Archive the corpus and copy ParatextData's per-book counts**

```bash
mkdir -p tests/corpus
tar -czf tests/corpus/WEB.tar.gz -C ~/repos/usfm WEB/copr.htm $(cd ~/repos/usfm && ls WEB/*.usfm)
cp ~/repos/usfm/ParatextData/docs/results/2026-08-20-web-corpus.csv tests/corpus/paratextdata-web.csv
ls -la tests/corpus
tar -tzf tests/corpus/WEB.tar.gz | wc -l
```

Expected:
- `WEB.tar.gz` is about 5–6 MB.
- `tar -tzf` lists 84 entries (83 `.usfm` files plus `copr.htm`).

The CSV header must be `path,book,bytes,tokens,tokenize_ms,parse_ms,total_ms,chapters,verses,paragraphs,status`.

- [ ] **Step 2: Write `tests/corpus_test.c`**

```c
/* Parses every .usfm file in a directory and checks the totals that
 * ParatextData, usfmtc and usfm3 agree on for the WEB corpus. Also
 * writes per-book counts to build/corpus/libusfm-web.csv for diffing
 * against tests/corpus/paratextdata-web.csv. */
#define _DEFAULT_SOURCE 1
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define USFM_IMPLEMENTATION
#include "../usfm.h"
#include "check.h"

typedef struct Counts {
    unsigned long long books, chapters, verses, paragraphs, chars, notes, milestones;
} Counts;

/* Counts what ParatextData's CountingSink counts (see the spec). */
static void count_ast(const USFM_Ast *ast, Counts *c)
{
    uint32_t count, i;
    const USFM_Node *nodes = USFM_Ast_GetNodes(ast, &count);
    c->books++;
    for (i = 0; i < count; i++) {
        const USFM_Node *n = &nodes[i];
        switch (n->kind) {
        case USFM_AST_CHAPTER: c->chapters++; break;
        case USFM_AST_VERSE: c->verses++; break;
        case USFM_AST_PARA:
            if (!(n->level_flags & USFM_NF_IMPLIED) && n->marker != USFM_M_CP) c->paragraphs++;
            break;
        case USFM_AST_HEADER:
            if (n->marker != USFM_M_ID) c->paragraphs++;
            break;
        case USFM_AST_CHAR: c->chars++; break;
        case USFM_AST_NOTE: c->notes++; break;
        case USFM_AST_MILESTONE: c->milestones++; break;
        default: break;
        }
    }
}

static char *read_file(const char *path, uint32_t *length)
{
    FILE *f = fopen(path, "rb");
    char *data;
    long size;
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = (char *)malloc(size > 0 ? (size_t)size : 1);
    if (data != NULL && size > 0 && fread(data, 1, (size_t)size, f) != (size_t)size) {
        free(data);
        data = NULL;
    }
    fclose(f);
    *length = (uint32_t)size;
    return data;
}

static int compare_names(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/corpus/WEB";
    const Counts expected = { 83, 1402, 38058, 34753, 685197, 2218, 0 };
    Counts total;
    char *names[256];
    size_t name_count = 0, i;
    int failures = 0;
    DIR *d = opendir(dir);
    FILE *csv = fopen("build/corpus/libusfm-web.csv", "w");
    struct dirent *e;
    memset(&total, 0, sizeof total);
    if (d == NULL) {
        fprintf(stderr, "cannot open %s\n", dir);
        return 1;
    }
    while ((e = readdir(d)) != NULL && name_count < 256) {
        size_t len = strlen(e->d_name);
        if (len > 5 && strcmp(e->d_name + len - 5, ".usfm") == 0) {
            names[name_count++] = strdup(e->d_name);
        }
    }
    closedir(d);
    qsort(names, name_count, sizeof names[0], compare_names);
    for (i = 0; i < name_count; i++) {
        char path[1024];
        uint32_t length;
        char *data;
        USFM_Err err;
        USFM_Ast *ast;
        Counts book;
        snprintf(path, sizeof path, "%s/%s", dir, names[i]);
        data = read_file(path, &length);
        if (data == NULL) {
            fprintf(stderr, "cannot read %s\n", path);
            failures++;
            continue;
        }
        ast = USFM_Ast_Parse(data, length, NULL, &err);
        if (ast == NULL || err.code != USFM_OK) {
            fprintf(stderr, "%s: parse failed: %s\n", names[i], err.msg);
            failures++;
        } else {
            if (check_ast(ast) != 0) {
                fprintf(stderr, "%s: invariants failed\n", names[i]);
                failures++;
            }
            memset(&book, 0, sizeof book);
            count_ast(ast, &book);
            total.books += book.books;
            total.chapters += book.chapters;
            total.verses += book.verses;
            total.paragraphs += book.paragraphs;
            total.chars += book.chars;
            total.notes += book.notes;
            total.milestones += book.milestones;
            if (csv) {
                fprintf(csv, "%s,%llu,%llu,%llu\n", USFM_Ast_GetBookCode(ast),
                        book.chapters, book.verses, book.paragraphs);
            }
        }
        USFM_Ast_Free(ast);
        free(data);
        free(names[i]);
    }
    if (csv) fclose(csv);
    printf("Counted:    %llu books, %llu chapters, %llu verses, %llu paragraphs\n"
           "            %llu char styles, %llu notes, %llu milestones\n",
           total.books, total.chapters, total.verses, total.paragraphs,
           total.chars, total.notes, total.milestones);
    if (memcmp(&total, &expected, sizeof total) != 0) {
        printf("Expected:   %llu books, %llu chapters, %llu verses, %llu paragraphs\n"
               "            %llu char styles, %llu notes, %llu milestones\n",
               expected.books, expected.chapters, expected.verses, expected.paragraphs,
               expected.chars, expected.notes, expected.milestones);
        failures++;
    }
    if (failures != 0) {
        printf("corpus: FAILED\n");
        return 1;
    }
    printf("corpus: ok\n");
    return 0;
}
```

- [ ] **Step 3: Add the corpus to `build.sh test`**

Add this function after `run_codegen`:

```bash
# Unpack the WEB corpus once (and again whenever the archive changes).
unpack_corpus() {
    mkdir -p build/corpus
    if [[ ! -f build/corpus/WEB.stamp || tests/corpus/WEB.tar.gz -nt build/corpus/WEB.stamp ]]; then
        rm -rf build/corpus/WEB
        tar -xzf tests/corpus/WEB.tar.gz -C build/corpus
        touch build/corpus/WEB.stamp
    fi
}
```

At the end of the `test)` case, after `./build/run_tests`, add:

```bash
        unpack_corpus
        gcc "${COMMON_FLAGS[@]}" "${DEBUG_FLAGS[@]}" tests/corpus_test.c -o build/corpus_test
        ./build/corpus_test build/corpus/WEB
```

- [ ] **Step 4: Run it**

Run: `./build.sh test`
Expected output ends with:

```
Counted:    83 books, 1402 chapters, 38058 verses, 34753 paragraphs
            685197 char styles, 2218 notes, 0 milestones
corpus: ok
```

- [ ] **Step 5: If a count differs, find the books that differ**

```bash
awk -F, 'NR>1 {print $2","$8","$9","$10}' tests/corpus/paratextdata-web.csv | sort > build/corpus/expected.csv
sort build/corpus/libusfm-web.csv | diff build/corpus/expected.csv -
```

Each differing line names a book and its chapters, verses and paragraphs on both sides. ParatextData's CSV has no per-book character or note counts. For those, compare the marker tallies with grep (the method the spec's Goal section describes):

```bash
grep -o '\\[a-zA-Z0-9+-]*\*\?' build/corpus/WEB/<FILE>.usfm | sort | uniq -c | sort -rn
```

Then read the book's USFM around the first chapter where the counts differ.

**Use superpowers:systematic-debugging for this step.** Find the closing rule that disagrees and fix the rule in `usfm.h`. Add a unit test for the exact construct, in `tests/test_chars.c` or wherever it fits. Never special-case a book. If a rule in the spec turns out to be wrong (for example "`\v` closes character spans"), update the spec's closing-rules table in the same commit and say so in the commit message.

- [ ] **Step 6: Commit**

```bash
git add tests/corpus/WEB.tar.gz tests/corpus/paratextdata-web.csv tests/corpus_test.c build.sh
git commit -m "test: WEB corpus counts match ParatextData, usfmtc and usfm3

World English Bible (public domain; the name is a trademark of
eBible.org), archived with its copyright page.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 13: Fuzzing

**Files:**
- Create: `tests/fuzz_ast.c`, `tests/fuzz/usfm.dict`, `tests/fuzz/seeds/*`
- Modify: `build.sh`

Prerequisite for running libFuzzer (not for the replay build): `sudo apt install libstdc++-12-dev`. Clang 14 can't link `-fsanitize=fuzzer` on this machine until that is installed. Ask the user to install it, and don't install it yourself.

- [ ] **Step 1: Write the harness `tests/fuzz_ast.c`**

```c
/* libFuzzer harness. Input layout: byte 0 picks the USFM version
 * (% 5), byte 1 picks flags: bit 0 = COPY_INPUT, bit 7 = fail the
 * allocator after (byte1 & 0x3F) calls. The rest is the USFM text.
 *
 * Every parse must either return NULL with USFM_ERR_NOMEM (and have
 * freed everything), or return a tree whose invariants hold. Any
 * crash, sanitizer report, leak or broken invariant is a finding.
 *
 * Built with -DUSFM_FUZZ_REPLAY, it gets a main() that runs each file
 * given on the command line, so seeds and crash files replay under gcc. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define USFM_IMPLEMENTATION
#include "../usfm.h"
#include "check.h"

typedef struct FuzzAlc {
    size_t   live_bytes;
    uint32_t calls_left;
} FuzzAlc;

static void *fuzz_alloc(void *ctx, size_t size)
{
    FuzzAlc *f = (FuzzAlc *)ctx;
    void *p;
    if (size % 8 != 0) abort();
    if (f->calls_left == 0) return NULL;
    if (f->calls_left != UINT32_MAX) f->calls_left--;
    p = malloc(size);
    if (p) f->live_bytes += size;
    return p;
}

static void *fuzz_resize(void *ctx, void *ptr, size_t old_size, size_t new_size)
{
    FuzzAlc *f = (FuzzAlc *)ctx;
    void *p;
    if (old_size % 8 != 0 || new_size % 8 != 0) abort();
    if (f->calls_left == 0) return NULL;
    if (f->calls_left != UINT32_MAX) f->calls_left--;
    p = realloc(ptr, new_size);
    if (p) f->live_bytes = f->live_bytes - old_size + new_size;
    return p;
}

static void fuzz_dealloc(void *ctx, void *ptr, size_t size)
{
    FuzzAlc *f = (FuzzAlc *)ctx;
    if (size % 8 != 0 || size > f->live_bytes) abort();
    f->live_bytes -= size;
    free(ptr);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    USFM_Config config;
    FuzzAlc state;
    USFM_Alc alc;
    USFM_Err err;
    USFM_Ast *ast;
    uint32_t flags;
    if (size < 2 || size - 2 > USFM_INPUT_MAX) return 0;
    memset(&config, 0, sizeof config);
    config.version = (uint8_t)(data[0] % 5);
    flags = (data[1] & 0x01) ? USFM_PARSE_COPY_INPUT : USFM_PARSE_DEFAULT;
    state.live_bytes = 0;
    state.calls_left = (data[1] & 0x80) ? (uint32_t)(data[1] & 0x3F) : UINT32_MAX;
    alc.alloc = fuzz_alloc;
    alc.resize = fuzz_resize;
    alc.dealloc = fuzz_dealloc;
    alc.ctx = &state;

    ast = USFM_Ast_ParseOpts((const char *)data + 2, (uint32_t)(size - 2), &config, flags,
                             &alc, &err);
    if (ast == NULL) {
        if (err.code != USFM_ERR_NOMEM || state.live_bytes != 0) abort();
        return 0;
    }
    if (err.code != USFM_OK && err.code != USFM_ERR_NOMEM) abort();
    if (check_ast(ast) != 0) abort();
    {
        /* Exercise the iterators on every verse. */
        USFM_ChapterIter chapters;
        USFM_ChapterEntry chapter;
        uint32_t verse_count, i;
        const USFM_VerseEntry *verses = USFM_Ast_GetVerses(ast, &verse_count);
        for (i = 0; i < verse_count; i++) {
            USFM_VerseIter it;
            USFM_VerseItem item;
            USFM_VerseIter_Init(&it, ast, &verses[i],
                                USFM_VI_INCLUDE_NOTES | USFM_VI_REPORT_BREAKS |
                                USFM_VI_REPORT_NOTES | USFM_VI_INCLUDE_HEADINGS);
            while (USFM_VerseIter_Next(&it, &item)) {
                if (item.node >= ast->node_count) abort();
            }
            USFM_Ast_FindVerse(ast, verses[i].ref.chapter, verses[i].ref.verse_first, NULL);
        }
        USFM_ChapterIter_Init(&chapters, ast);
        while (USFM_ChapterIter_Next(&chapters, &chapter)) {
            USFM_Ast_FindChapter(ast, chapter.number, NULL);
        }
    }
    USFM_Ast_Free(ast);
    if (state.live_bytes != 0) abort();
    return 0;
}

#ifdef USFM_FUZZ_REPLAY
int main(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb");
        uint8_t *data;
        long size;
        if (f == NULL) {
            fprintf(stderr, "cannot open %s\n", argv[i]);
            return 1;
        }
        fseek(f, 0, SEEK_END);
        size = ftell(f);
        fseek(f, 0, SEEK_SET);
        data = (uint8_t *)malloc(size > 0 ? (size_t)size : 1);
        if (data == NULL || (size > 0 && fread(data, 1, (size_t)size, f) != (size_t)size)) {
            fprintf(stderr, "cannot read %s\n", argv[i]);
            return 1;
        }
        fclose(f);
        LLVMFuzzerTestOneInput(data, (size_t)size);
        free(data);
    }
    printf("fuzz replay: %d inputs ok\n", argc - 1);
    return 0;
}
#endif
```

(`ast->node_count` is visible because the harness defines `USFM_IMPLEMENTATION`.)

- [ ] **Step 2: Generate the dictionary**

```bash
mkdir -p tests/fuzz
awk -F'"' '/^    \{ "/ { printf "\"\\\\%s \"\n\"\\\\%s*\"\n\"\\\\+%s \"\n", $2, $2, $2 }' codegen/markers.h > tests/fuzz/usfm.dict
cat >> tests/fuzz/usfm.dict <<'EOF'
"\\*"
"|"
"//"
"~"
"\\\\"
"\\/"
"\\|"
"\\~"
"\\\""
"-s"
"-e"
"=\""
"\x0a"
EOF
wc -l tests/fuzz/usfm.dict
head -3 tests/fuzz/usfm.dict
```

Expected:
- 571 lines: 186 markers × 3 forms, plus 13 extras.
- The first line is `"\\id "`.

- [ ] **Step 3: Create the seed corpus**

Every seed starts with the two config bytes `AA`. `'A' % 5 = 0` selects AUTO. `'A' = 0x41` sets COPY_INPUT and leaves allocator failure off.

```bash
mkdir -p tests/fuzz/seeds
s() { printf 'AA%s' "$2" > "tests/fuzz/seeds/$1.usfm"; }
s basic        '\id GEN Test\n\c 1\n\p\n\v 1 In the beginning.\n\v 2 Next.\n'
s nesting31    '\p \add a \nd b\nd* c\add*'
s nesting2x    '\p \add a \nd b\nd* c'
s plusnest     '\p \add an \+nd Lord\+nd* addition\add*'
s notes        '\p a\f + \fr 1.1 \ft note \fq q\fq*\f* b\x - \xo 1.1: \xt Mk 1.1\x*'
s table        '\tr \th1 A\th2 B\n\tr \tc1 1\tc2-3 2\n\p after'
s milestones   '\p a\qt-s |who="Pilate"\*b\qt-e\* \ts\* \zaln-s |x-strong="H1"\*w\zaln-e\*'
s unknown      '\zp line\n\p a \zx b \zc c\zc* \xyz d'
s periph       '\id FRT\n\periph Title Page|id="title"\n\p x\n\esb\n\p in\n\esbe\n'
s figure       '\p \fig At once|src="a.jpg" size="span"\fig* \fig DESC|a.tif|span|||Cap|1.18\fig*'
s escapes      '\p a\/\/b \\ c\|d ~ e//f \w x|lemma="a\"b"\w*'
s versions     '\id GEN\n\usfm 2.4\n\p \add a \nd b'
for f in 02-GENeng-web 20-PSAeng-web 70-MATeng-web; do
  { printf 'AA'; head -c 4096 "build/corpus/WEB/$f.usfm"; } > "tests/fuzz/seeds/web-$f.usfm"
done
ls tests/fuzz/seeds | wc -l
```

(`printf '%s'` writes the backslashes literally. The `\n` inside the seed strings stays as the two characters `\` `n`, which is fine for fuzz seeds. If a WEB file name differs, use `ls build/corpus/WEB` to pick three that exist.)

Expected: 15 seed files.

- [ ] **Step 4: Add the replay to `build.sh test` and add the `fuzz` mode**

At the end of the `test)` case, after the corpus test, add:

```bash
        gcc "${COMMON_FLAGS[@]}" "${DEBUG_FLAGS[@]}" -DUSFM_FUZZ_REPLAY tests/fuzz_ast.c -o build/fuzz_replay
        ./build/fuzz_replay tests/fuzz/seeds/*
```

Add a `fuzz)` case before `codegen)`:

```bash
    fuzz)
        run_codegen
        mkdir -p build/fuzz/corpus
        clang -std=c99 -g -O1 -fsanitize=fuzzer,address,undefined tests/fuzz_ast.c -o build/fuzz_ast
        echo "Run: ./build/fuzz_ast -dict=tests/fuzz/usfm.dict -max_total_time=1800 build/fuzz/corpus tests/fuzz/seeds"
        ;;
```

- [ ] **Step 5: Run the replay**

Run: `./build.sh test`
Expected: the output ends with `fuzz replay: 15 inputs ok`.

- [ ] **Step 6: Run the fuzzer (requires libstdc++-12-dev)**

```bash
./build.sh fuzz
./build/fuzz_ast -dict=tests/fuzz/usfm.dict -max_total_time=1800 build/fuzz/corpus tests/fuzz/seeds
```

Expected: the run lasts 30 minutes and ends with `Done N runs in 1800 second(s)`, with no `crash-*`, `leak-*` or `timeout-*` files.

For any finding:
1. Copy it to `tests/fuzz/seeds/regress-<short-name>.usfm`.
2. Use superpowers:systematic-debugging to find the cause.
3. Fix it and add a unit test.
4. Re-run until a clean 30-minute run.

If libstdc++-12-dev is not installed yet, stop here, tell the user, and continue with Task 14. The 30-minute run stays open as the last v0.1 criterion.

- [ ] **Step 7: Commit**

```bash
git add tests/fuzz_ast.c tests/fuzz build.sh
git commit -m "test: libFuzzer harness with allocator-failure injection

The same harness builds as a replay tool under gcc so the seed corpus
runs in ./build.sh test.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 14: Benchmark harness and release build

**Files:**
- Modify: `main.c`

- [ ] **Step 1: Write the benchmark `main.c`**

```c
/* Benchmark: parse one file many times, report the fastest run.
 * Usage: ./usfm [file] [iterations] */
#define _DEFAULT_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <x86intrin.h>

#define USFM_IMPLEMENTATION
#include "usfm.h"

typedef struct {
    long cycles;
    struct timespec time;
} Timer;

static void getTime(Timer *timer)
{
    timer->cycles = (long)__rdtsc();
    if (clock_gettime(CLOCK_MONOTONIC, &timer->time) != 0)
    {
        exit(1);
    }
}

static void printTimeData(Timer start, Timer end, size_t size)
{
    long cycles = end.cycles - start.cycles;
    long nanoseconds = ((end.time.tv_sec - start.time.tv_sec) * (long)1e9) +
        (end.time.tv_nsec - start.time.tv_nsec);
    printf("%zu bytes, %ld cycles, %ld ns\n", size, cycles, nanoseconds);
    printf("%.2f cycles/byte, %.2f bytes/cycle, %.2f ns/byte, %.1f MiB/s\n",
        (double)cycles / (double)size, (double)size / (double)cycles,
        (double)nanoseconds / (double)size,
        ((double)size / (1024.0 * 1024.0)) / ((double)nanoseconds / 1e9));
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "build/corpus/WEB/25-JEReng-web.usfm";
    unsigned iterations = argc > 2 ? (unsigned)strtoul(argv[2], NULL, 10) : 100u;
    FILE *f = fopen(path, "rb");
    if (f == NULL)
    {
        printf("Error: cannot open %s (run ./build.sh test once to unpack the corpus)\n", path);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *input = malloc(size > 0 ? (size_t)size : 1);
    if (input == NULL || (size > 0 && fread(input, 1, (size_t)size, f) != (size_t)size))
    {
        printf("Error: cannot read %s\n", path);
        return 1;
    }
    fclose(f);

    Timer best_start = {0}, best_end = {0};
    long best = -1;
    uint32_t nodes = 0, verses = 0, diagnostics = 0;
    for (unsigned i = 0; i < iterations; i++)
    {
        Timer start, end;
        USFM_Err err;
        getTime(&start);
        USFM_Ast *ast = USFM_Ast_Parse(input, (uint32_t)size, NULL, &err);
        getTime(&end);
        if (ast == NULL)
        {
            printf("Error: parse failed: %s\n", err.msg);
            return 1;
        }
        USFM_Ast_GetNodes(ast, &nodes);
        USFM_Ast_GetVerses(ast, &verses);
        USFM_Ast_GetDiagnostics(ast, &diagnostics);
        USFM_Ast_Free(ast);
        long cycles = end.cycles - start.cycles;
        if (best < 0 || cycles < best)
        {
            best = cycles;
            best_start = start;
            best_end = end;
        }
    }
    printf("%s: %u nodes, %u verses, %u diagnostics\n", path, nodes, verses, diagnostics);
    printf("fastest of %u parses (parse + tree build, excluding free):\n", iterations);
    printTimeData(best_start, best_end, (size_t)size);
    free(input);
    return 0;
}
```

- [ ] **Step 2: Build release and run it**

```bash
./build.sh release && ./usfm
```

Expected:
- The build has no warnings. Release uses `-Werror`, so an unused static function in `usfm.h` fails the build; remove or use it.
- The run prints a node count, verse count, diagnostic count and timing for JER.
- Record the cycles/byte figure in the commit message. It's the baseline for later SIMD work.

- [ ] **Step 3: Build debug to check the gcc build too**

Run: `./build.sh debug && ./usfm build/corpus/WEB/32-OBAeng-web.usfm 3`
Expected: the run completes with no sanitizer output.

- [ ] **Step 4: Commit**

```bash
git add main.c
git commit -m "feat: benchmark harness for USFM_Ast_Parse

Baseline on JER (1 MB): <N> cycles/byte, <M> MiB/s (clang -O3).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 15: Final verification

- [ ] **Step 1: Run every check from a clean build directory**

```bash
rm -rf build
./build.sh test
./build.sh release
./build.sh debug
git status --short
```

Expected:
- Unit tests report `ok: N checks`.
- The corpus test reports `corpus: ok`.
- The fuzz replay reports `fuzz replay: 15 inputs ok`.
- Release and debug both build.
- `git status` shows nothing uncommitted except ignored build outputs.

- [ ] **Step 2: Check the spec against the result**

Read `docs/superpowers/specs/2026-11-14-ast-parser-design.md` top to bottom. For each "Done when" item and each scope bullet, name the test or file that covers it. Report any gap to the user instead of silently fixing scope.

- [ ] **Step 3: Report to the user**

Summarize:
- the corpus counts;
- the unit check count;
- the benchmark baseline;
- whether the 30-minute fuzz run has happened, or is still blocked on `libstdc++-12-dev`;
- any spec amendments made along the way.

Don't push or open a PR unless asked.


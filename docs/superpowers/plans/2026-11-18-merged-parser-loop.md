# Character-Span Fast Path (Merged Parser Loop, Step A) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement step A of
`docs/superpowers/specs/2026-11-18-merged-parser-loop-design.md`: right
after a character span opens, read its text, attributes, own closer and
following text directly. The result must stay byte-identical to the
generic loop, which remains the test reference.

**Architecture:**
- **Shared scanner helpers.** The scanner's attribute and text branches
  move into two shared, always-inlined helpers. `usfm__scan_next_with`
  and the fast path both call them, so every token boundary is identical
  by construction.
- **The fast path.** `usfm__char_body_fast` runs from `usfm__on_char`
  after a successful open. A matching closer that repeats the opening
  marker's bytes is compared directly. Anything else is handed back to
  the main loop, a scanned marker through the existing push-back slot.
- **The reference.** A parser field `fast` (0 only in tests) and a
  static `usfm__parse_opts_impl(…, fast)` make the generic loop the
  reference for a differential check in the unit, corpus and fuzz tests.

**Tech stack:** C99, the unity test build, corpus tests on every SIMD
path, libFuzzer.

**Branch:** `feat/simd`, in `~/repos/libusfm`. Never push.

**Status:** prototyped and verified on the local branch `wip/fastpath`
(commit `d8aabef`, worktree `/tmp/fastwt`, off `feat/simd` at
`86dd228`). On that prototype:
- **Tests:** `./build.sh test`, `test-neon`, `test-wasm` and `release`
  pass. Every corpus file on every path shows `Fast-path differences:
  0`, with checksums unchanged.
- **Fuzzing:** a 2-minute libFuzzer run found nothing.
- **Mutation tests:** five mutants were tried. Three are caught:
  - the dropped invalid-UTF-8 flag;
  - the dropped `last_end` update;
  - `pipe_is_attr` forced to 1.

  "Continue after the closer into a figure" was caught once a figure case
  was added. The fifth, dropping the trim bookkeeping, is equivalent: it
  only disables an O(1) shortcut whose fallback gives the same result.
- **Speed** (CPU 2, fastest of 27, best of 3 rounds), in cycles:

  | Corpus | AVX2 | Scalar |
  | --- | ---: | ---: |
  | WEB-STRONGS | **−21.4%** | **−14.7%** |
  | GRC-BYZ | **−20.6%** | **−14.9%** |
  | CMN-CUV | −3.8% | −5.4% |
  | WEB, HBO-WLC, ARB-VD | 0 to +1.5% | 0 to +0.3% |

  This passes the spec's criteria.
- **A finding along the way:** without forced inlining, the scanner
  refactor alone cost 2–3% on plain text in the scalar build. Task 1's
  `USFM__INLINE` exists for that.

Each task takes its code from `wip/fastpath` with
`git checkout -p wip/fastpath -- <file>`. The task names the hunks. Every
task ends with `./build.sh test` passing.

---

### Task 1: Shared scanner helpers

**Files:** `usfm.h`

- [ ] **Step 1:** From `wip/fastpath`, take the scanner hunks:
  - the `USFM__INLINE` macro (always-inline: GCC/Clang attribute, MSVC
    `__forceinline`, and `static inline` otherwise), with its comment;
  - `usfm__scan_attributes(s, t, use_blocks)` and
    `usfm__scan_text(s, t, use_blocks)`, both `USFM__INLINE`, placed
    before `usfm__scan_next_with`;
  - `usfm__scan_next_with` calling them in place of its former inline
    attribute and text branches.
- [ ] **Step 2:** Run `./build.sh test`.

  Expected: exit 0, with checksums identical across 4 paths and the
  same values as before:

  | Corpus | Checksum |
  | --- | --- |
  | WEB | `36b590d2a81e528b` |
  | WEB-STRONGS | `53a3a8b91eea5749` |
  | GRC-BYZ | `97a54f039cae4738` |
  | HBO-WLC | `a42faf844d23fe27` |

  Scanner differences must be 0.
- [ ] **Step 3: Measure** that the scalar build did not get slower.
  Build `tests/bench.c` with `-DUSFM_SIMD_NONE` from this commit and from
  `feat/simd`. Run `taskset -c 2 … -n 27` on WEB, HBO-WLC and ARB-VD, 3
  rounds, and compare minimum cycles.

  Expected: within ±0.5%. Without `USFM__INLINE` it was +2–3%.
- [ ] **Step 4:** Commit:
  `refactor: share the scanner's text and attribute branches (always inlined)`.

### Task 2: The reference switch

**Files:** `usfm.h`

- [ ] **Step 1:** From `wip/fastpath`, take these hunks:
  - **The parser field:** `uint8_t fast;` in `USFM_Parser`, with
    `reserved[2]` becoming `reserved[1]`.
  - **The split:** `USFM_Ast_ParseOpts`'s body becomes
    `static USFM_Ast *usfm__parse_opts_impl(…, int fast)`, which sets
    `p.fast = (uint8_t)(fast != 0);` right before `p.check_utf8 = …`.
  - **The public entry:** `USFM_Ast_ParseOpts` becomes a wrapper that
    calls `usfm__parse_opts_impl(…, 1)`.
- [ ] **Step 2:** Run `./build.sh test`. Expected: passes, with no
  behaviour change (no fast path yet).
- [ ] **Step 3:** Commit: `refactor: internal parse entry point with a fast-path switch`.

### Task 3: The differential check, before the fast path exists

**Files:**
- Modify: `tests/check.h`, `tests/helpers.h`, `tests/corpus_test.c`,
  `tests/fuzz_ast.c`, `build.sh`
- Create: `tests/test_fastpath.c`
- Modify: `tests/run_tests.c`

- [ ] **Step 1: The check helpers.** From `wip/fastpath`, take
  `check_same_parse(a, ea, b, eb, label)` and
  `check_fast_path(input, length, config, flags, label)` into
  `tests/check.h`, before the final `#endif`. `check_same_parse`
  compares:
  - result and error code;
  - every node byte for byte, reporting the first difference;
  - diagnostics, including the dropped count;
  - the chapter and verse tables;
  - the version and book code.
- [ ] **Step 2: Unit tests.** Take `check_fast_equal_at` into
  `tests/helpers.h`. It is called at the start of `check_tree_at` and
  `check_diags_at`.
- [ ] **Step 3: The corpus test.** Take its changes: a `fast_diffs`
  counter, a `check_fast_path` call per file before the parse, a
  `Fast-path differences: N` line, and a failure when N is not 0. Also
  take `build.sh` `run_corpus` printing that line.
- [ ] **Step 4: The fuzzer.** Take its changes:
  - **Same failure schedule:** after the first parse, a reference parse
    with `fast = 0`, using a second `FuzzAlc` with the same `calls_left`,
    and `check_same_parse` on the pair.
  - **Validation off:** in the no-injection branch, `check_fast_path` with
    `flags | USFM_PARSE_NO_UTF8_CHECK`.
- [ ] **Step 5: The targeted tests.** Take `tests/test_fastpath.c` with
  its `CHECK_FAST` cases:
  - different, bare and plus-less closers;
  - attributes without a closer;
  - `//` and escapes inside a span;
  - a span running to the end of input, with and without attributes;
  - invalid UTF-8;
  - a blank run before `\p`;
  - a closer directly followed by an opener;
  - a pipe after the closer;
  - spans in a note, a cell, a milestone context and a figure (with
    attributes after the closer), and a header line;
  - CRLF;
  - implicit closing;
  - spans next to verses;
  - an implied paragraph;
  - empty spans;
  - unknown markers;
  - the depth limit (70 nested `\+w`).

  Include it from `tests/run_tests.c` and call `run_fastpath_tests()`
  after `run_simd_tests()`.
- [ ] **Step 6:** Run `./build.sh test`. Expected: passes, with
  `Fast-path differences: 0` everywhere. Both parses are still the
  generic loop, so this proves the harness has no false positives.
- [ ] **Step 7:** Commit: `test: compare every parse with the fast path on and off`.

### Task 4: The fast path

**Files:** `usfm.h`

- [ ] **Step 1:** From `wip/fastpath`, take `usfm__char_body_fast(p,
  open_offset, open_length)`, with its comment, before `usfm__on_char`,
  and the hook at the end of `usfm__on_char`:
  ```c
      if (usfm__open_tok(p, USFM_AST_CHAR, t, t->tok.level_flags, 0) != USFM_NIL && p->fast &&
          !p->has_peeked) {
          /* The opening marker's bytes: "\\", an optional "+", the name. */
          usfm__char_body_fast(p, t->tok.span.offset,
                               t->name_offset + t->name_length - t->tok.span.offset);
      }
  ```
  How the function behaves (read it against the code):
  - **Loop:** each pass consumes input or returns.
  - **A marker:**
    - after the closer, return;
    - a closer that repeats the opening marker's bytes followed by `*`:
      update `last_end` as `usfm__on_close` does, then `close_top`;
    - any other marker: `usfm__scan_marker`, then a match through
      `usfm__frame_matches` closes it the same way, and anything else
      goes back through `usfm__push_back`.
  - **After a close:** continue only if the top frame is PARA, CELL,
    NOTE or CHAR.
  - **`pipe_is_attr`** is `top == CHAR`.
  - **`|`:** `usfm__scan_attributes`, the UTF-8 check, then append
    ATTRIBUTES with the invalid bit.
  - **`//`:** return.
  - **Otherwise:** `usfm__scan_text`, the UTF-8 check, append TEXT with
    the flags `COOK | INVALID_UTF8`, and set
    `text_node`/`text_offset`/`text_first_loose`.
- [ ] **Step 2:** Run `./build.sh test`, `./build.sh test-neon`,
  `./build.sh test-wasm` and `./build.sh release`.

  Expected: all exit 0, with `Fast-path differences: 0` and the same
  checksums as in Task 1.
- [ ] **Step 3: Mutation sanity.** Make each change, rebuild the unit
  tests (`gcc -std=c99 -w -O1 tests/run_tests.c`), and confirm they FAIL:
  - remove `| USFM_TF_INVALID_UTF8` from the TEXT flags;
  - remove the `last_end` update in the byte-compare closer branch;
  - set `s->pipe_is_attr = 1`;
  - remove the PARA/CELL/NOTE/CHAR check after a close.

  Restore after each.
- [ ] **Step 4: Fuzz.** Run `./build.sh fuzz`, then 10 minutes from a
  copy of `build/fuzz_ast` with the dictionary and seeds. Expected: no
  findings.
- [ ] **Step 5:** Commit: `perf: character-span fast path (step A of the merged parser loop)`.
  Put the cycle numbers from Task 5 in the commit body if they are
  available; otherwise amend after Task 5.

### Task 5: Measure and record

**Files:** `docs/superpowers/specs/2026-11-18-merged-parser-loop-design.md`

- [ ] **Step 1: Measure.** Build `tests/bench.c` from `feat/simd` before
  Task 1 (`86dd228`) and after Task 4, for AVX2
  (`-march=native -mno-avx512f`) and scalar (`-DUSFM_SIMD_NONE`). Run
  `taskset -c 2 … -n 27` on all six corpora, 3 rounds, keeping the
  minimum cycles and instructions.

  Expected: close to the prototype's figures. The criteria are ≥ 10% on
  WEB-STRONGS and GRC-BYZ on both builds, and no other corpus more than
  2% slower.
- [ ] **Step 2: Record.** Add a "Result of step A" section to the spec:
  the table, the `USFM__INLINE` finding, the equivalent mutant, and the
  B decision rule applied.

  With the prototype's numbers, AVX2 is −21% (between 10% and 25%), so
  the rule says to look at the new profile before B. Add one callgrind
  summary of where the remaining WEB-STRONGS instructions go
  (`valgrind --tool=callgrind` on `bench -n 1`).
- [ ] **Step 3:** Commit: `docs: step A results (fast path)`.
- [ ] **Step 4: Clean up:** `git worktree remove /tmp/fastwt` and
  `git branch -D wip/fastpath`.

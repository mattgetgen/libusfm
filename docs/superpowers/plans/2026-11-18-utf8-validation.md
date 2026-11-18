# UTF-8 Validation (0.1.0, scalar) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the 0.1.0 part of
`docs/superpowers/specs/2026-11-18-utf8-validation-design.md` on
`feat/ast-parser`:
- validate TEXT, ARGUMENT and ATTRIBUTES runs (on by default);
- one `INVALID_UTF8` diagnostic per bad run, and the
  `USFM_TF_INVALID_UTF8` flag on its node;
- reject UTF-16 and UTF-32 input;
- an opt-out flag.

**Architecture:**
- **Where validation happens:** in `usfm__next`, the single function
  through which the parser receives scanned tokens. A token that is
  pushed back and read again is not checked twice, and the look-ahead
  scanner never reaches `usfm__next`, so it never produces diagnostics.
- **Two-step check:** a branchless shift-DFA (`USFM_Utf8Dfa`, generated
  by codegen) answers "is this run valid?". It skips pure-ASCII 8-byte
  words. Only for a run that fails does the branchy decoder
  `usfm__utf8_check` locate the first bad sequence and its length.
- **The node flag** is copied from the token wherever TEXT, ARGUMENT and
  ATTRIBUTES nodes are created, and kept when a text node is trimmed.

**Tech stack:** C99, the existing unity test build
(`tests/run_tests.c`), codegen, libFuzzer.

**Status:** the whole change was prototyped and verified on the local
branch `wip/utf8` (commit `100976b`, worktree `/tmp/utf8wt`, off
`feat/ast-parser` at `c23e23f`):
- `./build.sh test` passes: 447,390 unit checks, the corpus,
  fuzz replay of 17 seeds, and 41 of 41 CLI checks;
- `./build.sh release` (clang -O3 -Werror) builds;
- the CLI compiles for Windows;
- a 5-minute libFuzzer run (1.47M inputs) found nothing.

The tasks below land it on `feat/ast-parser` as reviewable commits. The
code for each task is the matching part of `wip/utf8`; use `git
checkout wip/utf8 -- <paths>` or apply hunks with `git checkout -p
wip/utf8 -- usfm.h`. Every task ends with the full suite passing.

---

### Task 1: Spec update for the DFA

**Files:** Modify `docs/superpowers/specs/2026-11-18-utf8-validation-design.md`

- [ ] **Step 1:** In "0.1.0: the scalar validator", replace the
  description with the two-step design:
  - **`usfm__utf8_valid(s, n)`:** a shift-DFA. A state is a bit offset
    (a multiple of 6) into a 64-bit table entry per byte:
    `state = (USFM_Utf8Dfa[b] >> state) & 63`. There are 9 states; 0
    accepts and 6 is the absorbing error state. The table is generated
    by `codegen` (block `utf8_dfa`). It works on 8-byte words: a
    pure-ASCII word in the accepting state is skipped, otherwise its 8
    bytes go through the DFA without branches.
  - **`usfm__utf8_check(s, n, &bad)`:** the branchy RFC 3629 decoder.
    It runs only on runs the DFA rejects, and is the authority for the
    diagnostic's offset and length.
- [ ] **Step 2:** Add measured costs from the prototype (release build,
  CPU 2, `./usfm -b 27`, MiB/s):

  | Corpus | Before | Validation off | Branchy only | DFA + branchy |
  | --- | ---: | ---: | ---: | ---: |
  | WEB-STRONGS | 408 | 391 | 329 | 350 (−14%) |
  | WEB | 677 | 669 | 573 | 601 (−11%) |
  | HBO-WLC | 922 | 913 | 478 | 660 (−28%) |
  | CMN-CUV | 522 | 510 | 362 | 452 (−13%) |
  | ARB-VD | 908 | 898 | 457 | 645 (−29%) |
  | GRC-BYZ | 477 | 461 | 342 | 406 (−15%) |

  Standalone validators on the whole input, before the 8-byte DFA
  loop: the branchy decoder and a per-byte DFA both ran at about 1 GB/s
  on Hebrew and Arabic. The 8-byte DFA reaches about 2 GB/s on
  non-ASCII text. SIMD in 0.1.1 is expected to bring the cost close to
  zero.
- [ ] **Step 3:** Commit: `docs: UTF-8 spec settles on a shift-DFA plus a branchy locator`.

### Task 2: API, codes and the DFA table

**Files:**
- Modify: `usfm.h` (status enum, diagnostic enum and tables, the
  text-flag enum, parse flags, a new codegen block)
- Modify: `codegen/codegen.c`

- [ ] **Step 1:** From `wip/utf8`, take these `usfm.h` hunks:
  - `USFM_ERR_UNSUPPORTED_ENCODING` and its status message;
  - `USFM_DIAGNOSTIC_INVALID_UTF8 = 19` (`CODE_COUNT` becomes 20), with
    severity ERROR and the message "invalid UTF-8";
  - `USFM_TF_INVALID_UTF8 = 0x80` and the changed comment above the
    text flags;
  - `USFM_PARSE_NO_UTF8_CHECK = 0x02`;
  - the empty codegen block markers `// USFM_CODEGEN utf8_dfa
    start/end`, after `character_class`.
- [ ] **Step 2:** From `wip/utf8`, take the `codegen/codegen.c`
  changes:
  - `CodeGen_Utf8Dfa_Generate`;
  - `CodeGen_Utf8Dfa_Serialize`;
  - the `utf8_dfa` dispatch in `main`.
- [ ] **Step 3:** Run `./build.sh codegen`.

  Expected: `Writing new version of utf8_dfa`, and `usfm.h` gains
  `static const uint64_t USFM_Utf8Dfa[256]`.
- [ ] **Step 4:** Update `tests/dump.h`:
  - add `"INVALID_UTF8"` to `dump_diag_names`;
  - make `dump_tree` print ` badutf8` on flagged TEXT, ARGUMENT and
    ATTRIBUTES nodes.
- [ ] **Step 5:** Run `./build.sh test`.

  Expected: everything passes. The DFA table is unused so far, and
  unused `static const` tables don't warn.
- [ ] **Step 6:** Commit: `feat: UTF-8 validation API: error code, diagnostic, flag, parse flag, DFA table`.

### Task 3: Validators, with tests first

**Files:**
- Create: `tests/test_utf8.c` (only the three validator tests for now)
- Modify: `tests/check.h` (`check_utf8_prefix_ok`, `check_utf8_first_bad`)
- Modify: `tests/run_tests.c`
- Modify: `usfm.h` (`usfm__utf8_valid`, `usfm__utf8_check`)

- [ ] **Step 1:** Add the reference validator to `tests/check.h`, taken
  from `wip/utf8`. It checks each prefix by asking whether some
  completion decodes to an allowed scalar value: no overlong form, no
  surrogate, at most U+10FFFF. It shares no code with the library.
- [ ] **Step 2:** Create `tests/test_utf8.c` with
  `check_validator_matches`, `test_utf8_validator_table`,
  `test_utf8_validator_edges` (every 1- to 4-byte string over 24 edge
  bytes) and `test_utf8_validator_random` (100,000 strings), from
  `wip/utf8`. Its `run_utf8_tests` calls these three only. Include it
  from `tests/run_tests.c` and call it.
- [ ] **Step 3:** Run `./build.sh test`.

  Expected: compile errors, because `usfm__utf8_check` and
  `usfm__utf8_valid` are not defined yet.
- [ ] **Step 4:** Add `usfm__utf8_valid` and `usfm__utf8_check` to
  `usfm.h`, before `usfm__next`, from `wip/utf8`.
- [ ] **Step 5:** Run `./build.sh test`.

  Expected: passes. The validator tests add about 447,000 checks.
- [ ] **Step 6:** Commit: `feat: scalar UTF-8 validators (shift-DFA filter, branchy locator) with reference tests`.

### Task 4: Validate runs in the parser

**Files:**
- Modify: `usfm.h` (the `check_utf8` parser field, `usfm__check_utf8`
  and its forward declaration of `usfm__diag`, the hook in `usfm__next`,
  the flag copy in `usfm__take_argument`, `usfm__on_attributes`,
  `usfm__on_text` and `usfm__trim_last_text`, and `p.check_utf8` in
  `USFM_Ast_ParseOpts`)
- Modify: `tests/test_utf8.c`, `tests/check.h`

- [ ] **Step 1:** Add the parse tests from `wip/utf8`:
  `test_utf8_diagnostics`, `test_utf8_opt_out` and
  `test_utf8_legacy_book`. Add the `check_ast` changes from
  `tests/check.h`: compare text flags under `USFM_TEXT_FLAGS_COOK`, and
  require a flagged node's bytes to be invalid. Also add the
  `check_utf8_flags` and `check_utf8_coverage` helpers.
- [ ] **Step 2:** Run `./build.sh test`.

  Expected: the new tests fail. There are no diagnostics or flags yet.
- [ ] **Step 3:** Take the parser hunks listed under **Files** from
  `wip/utf8`. The trim change matters: `usfm__trim_last_text`
  recomputes a node's flags after removing trailing whitespace, and
  must keep `USFM_TF_INVALID_UTF8`. Without it the flag is silently
  lost on every text that ends a line.
- [ ] **Step 4:** Run `./build.sh test`.

  Expected: passes, and the corpus reports `Diagnostics: 0`.
- [ ] **Step 5:** Commit: `feat: validate TEXT, ARGUMENT and ATTRIBUTES runs as UTF-8 (one diagnostic per run)`.

### Task 5: Reject UTF-16 and UTF-32

**Files:**
- Modify: `usfm.h` (`usfm__wide_bom`, and the check in
  `USFM_Ast_ParseOpts` after the size check)
- Modify: `tests/test_utf8.c` (`test_utf8_byte_order_marks`)

- [ ] **Step 1:** Add `test_utf8_byte_order_marks` from `wip/utf8`. It
  covers all four marks, with and without `USFM_PARSE_NO_UTF8_CHECK`,
  and checks that a UTF-8 byte-order mark is still skipped.
- [ ] **Step 2:** Run `./build.sh test`. Expected: it fails, because the
  parse returns a tree.
- [ ] **Step 3:** Take the `usfm__wide_bom` hunk and the
  `USFM_ERR_UNSUPPORTED_ENCODING` early return in `USFM_Ast_ParseOpts`
  from `wip/utf8`.
- [ ] **Step 4:** Run `./build.sh test`. Expected: passes.
- [ ] **Step 5:** Commit: `feat: reject UTF-16 and UTF-32 input (byte-order mark) with USFM_ERR_UNSUPPORTED_ENCODING`.

### Task 6: Fuzzer, corpus test and CLI

**Files:**
- Modify: `tests/fuzz_ast.c`, `tests/corpus_test.c`, `main.c`
- Create: `tests/fuzz/seeds/legacy-cp1252.usfm`, `tests/fuzz/seeds/utf16le-bom.usfm`

- [ ] **Step 1:** Take the `tests/fuzz_ast.c` changes from `wip/utf8`.
  - **NULL results:** a NULL result is allowed only for NOMEM, or for
    `UNSUPPORTED_ENCODING` on an input that starts with a wide BOM.
  - **New invariants:** coverage (every high byte inside a validated
    run), and flags agreeing with the reference.
  - **Validation must change nothing else:** a second parse with
    `USFM_PARSE_NO_UTF8_CHECK` must give the same nodes and the same
    other diagnostics. Mask only bit `0x80` on TEXT, ARGUMENT and
    ATTRIBUTES nodes; on milestones that bit is `USFM_TF_SELF_CLOSED`.
- [ ] **Step 2:** Take the `tests/corpus_test.c` change: every corpus
  file must pass `check_utf8_coverage` and `check_utf8_flags`.
- [ ] **Step 3:** Take the `main.c` change: the tree view shows
  `invalid-utf8`, after the whole `if`/`else if` flag chain.
- [ ] **Step 4:** Add the two seeds from `wip/utf8`.
- [ ] **Step 5:** Run `./build.sh test` and `./build.sh release`.

  Expected: all pass, with `fuzz replay: 17 inputs ok` and `cli: 41
  passed`.
- [ ] **Step 6:** Run `./build.sh fuzz`, then
  `./build/fuzz_ast -dict=tests/fuzz/usfm.dict -max_total_time=1800 build/fuzz/corpus tests/fuzz/seeds`.
  Run it from a copy of the binary, because a rebuild during the run
  crashes it.

  Expected: no findings in 30 minutes.
- [ ] **Step 7:** Commit: `test: fuzz, corpus and CLI coverage for UTF-8 validation`.

### Task 7: Clean up and push

- [ ] **Step 1:** Check whether validation-off is really 1–4% slower
  than before. Measure `./usfm -b 27` from the old `main.c`, and from a
  copy that passes `USFM_PARSE_NO_UTF8_CHECK`, alternating three times
  per corpus.
  - If it's real, look at the extra `check_utf8` test in
    `usfm__next`, and at the inlining of the scanner call.
  - Report either way.
- [ ] **Step 2:** Delete the worktree and branch:
  `git worktree remove /tmp/utf8wt && git branch -D wip/utf8`.
- [ ] **Step 3:** Push `feat/ast-parser`, after the user approves.

### Notes for the 0.1.1 rebase (not this plan)

- **Trimming:** feat/simd's `usfm__trim_last_text` uses `first_loose`
  instead of recomputing the flags. It must still keep
  `USFM_TF_INVALID_UTF8`; check when rebasing.
- **The SIMD half** (Keiser–Lemire masks in the block classifier, with
  SSSE3, AVX2, AVX-512, NEON and wasm versions) gets its own plan. It
  then needs the benchmarks rerun, with validation on and off.

# Chunked UTF-8 Validation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** implement
`docs/superpowers/specs/2026-11-19-chunked-utf8-validation-design.md`.
Validate the input in chunks just ahead of the scanner instead of in one
pass before parsing, with byte-identical output.

**Architecture:** two parser fields, `utf8_valid_to` and
`utf8_invalid_seen`. A one-compare gate at the existing `check_utf8` call
sites, and a chunk loop in the out-of-line slow path. Test-only hooks
provide a chunk-size override and an always-per-run reference mode.

**Tech stack:** C99, the unity test build, corpus tests on every SIMD path,
libFuzzer.

**Branch:** `feat/simd`, in `~/repos/libusfm`. Never push.

Expected corpus checksums, unchanged throughout:

| Corpus | Checksum |
| --- | --- |
| WEB | `36b590d2a81e528b` |
| WEB-STRONGS | `53a3a8b91eea5749` |
| GRC-BYZ | `97a54f039cae4738` |
| HBO-WLC | `a42faf844d23fe27` |
| ARB-VD | `ce24d8c69445f7e2` |
| CMN-CUV | `549da8be55a53a1b` |

---

### Task 1: The reference harness, before the change

**Files:** `usfm.h` (test-only hooks), `tests/check.h`, `tests/helpers.h`,
`tests/corpus_test.c`, `tests/fuzz_ast.c`, `build.sh`, new
`tests/test_utf8_chunks.c`, `tests/run_tests.c`

- [ ] **Step 1: Test-only hooks** in `usfm.h`, under
  `USFM_TEST_SCAN_REFERENCE`:
  - a chunk-size override (0 means the default);
  - a reference mode that forces the per-run check on every TEXT,
    ARGUMENT and ATTRIBUTES token and skips any whole-input or chunk
    validation.

  Follow the existing `usfm__parse_opts_impl(…, fast)` pattern. Extend
  that internal entry, or add a small test-only parameter struct. Do not
  add globals unless the tests already use that pattern; check first.
- [ ] **Step 2: A helper** `check_utf8_chunks(input, len, config, flags,
  label)`. It parses in reference mode and at chunk sizes {1, 2, 3, 4, 7,
  64, 65536}, and compares each parse with the reference using the
  existing `check_same_parse`.
  - Call it from the unit-test helpers (as `check_fast_equal_at` is
    called).
  - Call it from the corpus test, with a `UTF-8 chunk differences: N` line
    that must be 0. Use the default size and 61 bytes, not all seven
    sizes, to keep the corpus test fast.
  - Call it from the fuzzer, with a chunk size derived from one input byte.
  - Show the new line in build.sh's `run_corpus`.
- [ ] **Step 3: `tests/test_utf8_chunks.c`** with the spec's unit cases.
  Build inputs with invalid or multi-byte sequences at offsets 61–68 and
  run them at chunk size 64, so a boundary falls inside them.
- [ ] **Step 4:** Run `./build.sh test`. Expected: passes, with 0 chunk
  differences. Today's code ignores the chunk size, so this proves the
  harness has no false positives. Then temporarily corrupt the reference
  mode, e.g. drop the INVALID flag, and confirm the harness fails. Restore.
- [ ] **Step 5:** Commit: `test: compare UTF-8 validation against an always-per-run reference`.

### Task 2: Chunked validation

**Files:** `usfm.h`

- [ ] **Step 1:** Implement the spec's "The change", points 1–6.
  - Remove the whole-input pre-pass from `usfm__parse_opts_impl`.
  - Keep `usfm__check_utf8` `USFM__NOINLINE`. The gate is an inline
    compare at the call sites; keep the hot path small.
  - Make sure the SIMD validators accept any start offset and length, and
    treat the end of the chunk as the end of the input.
- [ ] **Step 2:** Run `./build.sh test`, `test-neon`, `test-wasm`,
  `test-avx512` (SDE needs `vm.max_map_count` low; if it is high, skip and
  report) and `release`.

  Expected: all exit 0, with no warnings, `Fast-path differences: 0` and
  `UTF-8 chunk differences: 0` everywhere, and unchanged checksums.
- [ ] **Step 3: Mutation checks** (the spec's list). Each must fail,
  except a mutant that is equivalent, with the reason given.
- [ ] **Step 4: Fuzz** for 10 minutes: `setarch -R`, `-fork=4`,
  `taskset -c 0-3`, the dictionary, a scratch corpus under /tmp.
- [ ] **Step 5:** Commit: `perf: validate UTF-8 in chunks just ahead of the scanner`.

### Task 3: Chunk size, measurement, record

- [ ] **Step 1:** Measure chunk sizes 16, 32, 64 and 256 KiB on AVX2:
  - ALB900a, WEB-STRONGS, WEB, HBO-WLC;
  - CPU 2, `-n 27` (`-n 9` for ALB900a), 3 rounds.

  Pick the default and change it if it isn't 64 KiB.
- [ ] **Step 2:** Do the full measurement against `624bf3d` (before Task
  1):
  - AVX2 and scalar, plus an SSSE3 spot check;
  - the six corpora plus ALB900a and 01DFM (in `build/corpus`, local
    only);
  - cycles, instructions and wall ms.

  Also measure validation-off builds to show how much of the validation
  cost remains.
- [ ] **Step 3:** Add a "Result (date)" section to the spec, with the
  table, the keep criteria applied, the mutation results and the fuzz
  stats.
- [ ] **Step 4:** Commit: `docs: chunked UTF-8 validation results`.

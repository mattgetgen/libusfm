# Step B.1 (open character markers inside the fast path) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** implement `docs/superpowers/specs/2026-11-19-step-b1-char-open-design.md`.
When the fast path reaches a character-role opener, it opens the span
itself and keeps reading. Every other marker it has scanned is handed over
through the push-back slot. The result must stay byte-identical to the
generic loop.

**Architecture:**
- **The split.** `usfm__on_char` splits into `usfm__char_open`, which
  opens the span, and the existing fast body.
- **Openers in the fast body.** `usfm__char_body_fast` calls
  `usfm__char_open` for openers and keeps the current opener bytes in
  locals.

**Tech stack:** C99, the unity test build, corpus tests on every SIMD path,
libFuzzer.

**Branch:** `feat/simd`, in `~/repos/libusfm`. Never push.

---

### Task 1: Split `usfm__on_char`

**Files:** `usfm.h`

- [ ] **Step 1:** Move the body of `usfm__on_char`, everything before the
  fast-path hook, into:
  ```c
  /* Opens a character span for marker token t, as the main loop does:
   * an implied paragraph if needed, then (for a plain marker) the
   * implicit close of outer spans whose end marker isn't ahead. Returns
   * the new node, or USFM_NIL if the stack is full (the marker becomes
   * text) or memory ran out. */
  static uint32_t usfm__char_open(USFM_Parser *p, const USFM_ScanToken *t)
  ```
  It ends with `return usfm__open_tok(p, USFM_AST_CHAR, t, t->tok.level_flags, 0);`.
- [ ] **Step 2:** Make `usfm__on_char` call it:
  ```c
  static void usfm__on_char(USFM_Parser *p, const USFM_ScanToken *t)
  {
      if (usfm__char_open(p, t) != USFM_NIL && p->fast && !p->has_peeked) {
          /* The opening marker's bytes: "\\", an optional "+", the name. */
          usfm__char_body_fast(p, t->tok.span.offset,
                               t->name_offset + t->name_length - t->tok.span.offset);
      }
  }
  ```
  `usfm__char_open` must be defined before `usfm__char_body_fast`, which
  calls it in Task 3. Move it, and whatever it needs, above
  `usfm__char_body_fast` if necessary. Do not add prototypes for static
  functions unless the file already does that.
- [ ] **Step 3:** Run `./build.sh test`.

  Expected: exit 0, with `Fast-path differences: 0` everywhere and these
  checksums unchanged:

  | Corpus | Checksum |
  | --- | --- |
  | WEB | `36b590d2a81e528b` |
  | WEB-STRONGS | `53a3a8b91eea5749` |
  | GRC-BYZ | `97a54f039cae4738` |
  | HBO-WLC | `a42faf844d23fe27` |
  | ARB-VD | `ce24d8c69445f7e2` |
  | CMN-CUV | `549da8be55a53a1b` |

  No warnings.
- [ ] **Step 4:** Commit: `refactor: split the character-span open out of usfm__on_char`.

### Task 2: Targeted tests first

**Files:** `tests/test_fastpath.c`

- [ ] **Step 1:** Add `CHECK_FAST` cases. Each is a complete USFM snippet
  starting with `\\p ` (or `\\id GEN\n\\c 1\n\\p ` where verses are
  needed).
  ```c
  CHECK_FAST("\\p \\w a\\w* \\w b\\w* \\w c\\w*\n");
  CHECK_FAST("\\p \\w a|strong=\"H1\"\\w* \\w b|strong=\"H2\"\\w* c\n");
  CHECK_FAST("\\p \\wj x \\+w a\\+w* \\+w b\\w* y\\wj*\n");
  CHECK_FAST("\\p \\w a\\w* \\w b \\w c\\w* d\n");             /* \w inside an open \w */
  CHECK_FAST("\\p \\w a\\w* \\w b \\w c d\n");                 /* no closer ahead */
  CHECK_FAST("\\p \\w a\\w* \\xt Gen 1:1\\xt* b\n");          /* \xt outside a note */
  CHECK_FAST("\\id GEN\n\\c 1\n\\p \\v 1 \\w a\\w* \\v 2 \\w b\\w*\n");
  CHECK_FAST("\\p \\w a\\w* \\f + \\fr 1:1 \\ft x\\f* b\n");
  CHECK_FAST("\\p \\w a\\w*\n\\p \\w b\\w*\n");
  CHECK_FAST("\\p \\w a\\w* \\qt-s |who=\"x\"\\* b\\qt-e\\*\n");
  CHECK_FAST("\\p \\w a\\w* \\zfoo b\\zfoo* c\n");
  CHECK_FAST("\\p \\w a \\nd b\\nd*\\w* c\n");                 /* opener after opener */
  CHECK_FAST("\\p x\\f + \\ft \\w a\\w* \\w b\\w*\\f* y\n");  /* in a note */
  CHECK_FAST("\\tr \\tc1 \\w a\\w* \\w b\\w* \\tc2 c\n");     /* in a cell */
  CHECK_FAST("\\p \\w a\\w*\\w b\\w*\n");                      /* opener straight after a closer */
  ```
  Also add a depth-limit case where the span is opened by the fast path:
  64 or more openers nested after a closer. Build it in a buffer as the
  existing 70-deep `\+w` case does, starting with `\p \w a\w* ` and then
  70 × `\+w x `.
- [ ] **Step 2:** Run `./build.sh test`. Expected: passes. Task 3 hasn't
  changed the parser yet, so this checks the cases are well formed.
- [ ] **Step 3:** Commit: `test: fast-path cases for spans opened after a closer`.

### Task 3: Open character markers in the fast path

**Files:** `usfm.h`

- [ ] **Step 1:** In `usfm__char_body_fast`:
  - **Rename the parameters:** `open_offset` and `open_length` become
    locals `cur_offset` and `cur_length`, initialised from the parameters.
  - **Replace the marker branch:**
    ```c
    if (in[pos] == '\\' && !usfm__is_escape(in, len, pos)) {
        /* The existing byte-compare closer test, now also requiring
         * !closed and using cur_offset/cur_length. */
        if (!closed && len - pos > cur_length &&
            usfm__same_bytes(in + pos, in + cur_offset, cur_length) &&
            in[pos + cur_length] == '*') {
            ... existing byte-compare close, unchanged ...
        }
        usfm__scan_marker(s, &t);
        if (!closed && t.tok.kind == USFM_TOK_MARKER_END && t.name_length != 0 &&
            usfm__frame_matches(p, &p->stack[p->depth - 1], &t)) {
            ... existing scanned close, unchanged ...
        }
        if (t.tok.kind == USFM_TOK_MARKER && t.row != USFM_ROW_NONE &&
            !(USFM_FLAGS(t.tok.level_flags) & (USFM_TF_MILESTONE_START | USFM_TF_MILESTONE_END)) &&
            USFM_MarkerRows[t.row].role == USFM_ROLE_CHAR) {
            /* A character opener: open it as usfm__on_char does and read
             * the new span here. */
            USFM_ASSERT(s->expect_arg == USFM_ARG_NONE);
            if (usfm__char_open(p, &t) == USFM_NIL) return;
            cur_offset = t.tok.span.offset;
            cur_length = t.name_offset + t.name_length - t.tok.span.offset;
            closed = 0;
            USFM_ASSERT(s->position > pos);
            continue;
        }
        usfm__push_back(p, &t);   /* the main loop's next token */
        return;
    }
    ```
    Check `usfm__same_bytes`'s exact signature and return convention
    in the file before using it. Use the file's
    existing flag macros; check how `usfm__on_marker` tests milestone
    flags and copy that exactly.
  - **Check the remaining conditions:**
    - `usfm__char_open` can implicitly close spans and open an implied
      paragraph. After it, the top frame is the new CHAR span, and the
      loop's assert `closed || top == CHAR` must hold.
    - If `usfm__char_open` fails, `open_tok` has already appended the
      marker as TEXT (the depth limit), or `p->failed` is set. Either way,
      return.
  - **Update the function's header comment** to describe openers and
    push-back.
- [ ] **Step 2:** Run `./build.sh test`, `./build.sh test-neon`,
  `./build.sh test-wasm` and `./build.sh release`.

  Expected: all exit 0, with `Fast-path differences: 0` everywhere, the
  checksums unchanged, and no warnings.
- [ ] **Step 3: Mutation checks.** Make each change, rebuild the unit
  tests, confirm they FAIL, then restore:
  - drop `closed = 0;` after an open;
  - drop the `cur_offset`/`cur_length` update after an open;
  - in a temporary copy of `usfm__char_open`, skip the implicit-close
    loop, and call the copy from the fast path only;
  - drop the milestone-flag test.

  If a mutant survives, add a `CHECK_FAST` case that kills it, unless it
  is provably equivalent (then explain why).
- [ ] **Step 4: Fuzz.** Run `./build.sh fuzz`, then a copy of
  `build/fuzz_ast` for 10 minutes with the dictionary and a scratch corpus
  under /tmp, with `setarch -R` (ASan startup crashes on this kernel
  otherwise).

  Expected: no findings.
- [ ] **Step 5:** Commit: `perf: open character markers inside the fast path (step B.1)`.

### Task 4: Measure and record

**Files:** `docs/superpowers/specs/2026-11-19-step-b1-char-open-design.md`

- [ ] **Step 1: Measure** against the commit before Task 1, using the step
  A method:
  - clang `-std=c99 -O3 -march=native`, as in build.sh bench mode;
  - AVX2 adds `-mno-avx512f`; scalar adds `-DUSFM_SIMD_NONE`;
  - asserts on;
  - `taskset -c 2 … -n 27`, 3 interleaved rounds, all six corpora;
  - the minimum cycles and that pass's instructions.

  Also give the total change against the pre-step-A baseline `86dd228`.
- [ ] **Step 2: Profile.** Run callgrind on WEB-STRONGS (AVX2,
  `bench -n 1`) and give the top functions, before and after.
- [ ] **Step 3: Record.** Add a "Result (date)" section to the spec:
  - the table;
  - the keep criteria applied;
  - the mutation results;
  - the fuzz stats;
  - the profile;
  - a recommendation for the next B step (`\v` or paragraph markers),
    based on the profile.
- [ ] **Step 4:** Commit: `docs: step B.1 results`.

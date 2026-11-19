# Merged parser loop, step B.1: open character markers inside the fast path

Date: 2026-11-19. Branch: `feat/simd` (0.1.1). No API change.
Follows `2026-11-18-merged-parser-loop-design.md` (step A and its results).

## Why

Step A's results (10–25% band) say B starts by moving the marker open onto
the direct path. After step A, opening a marker is about 20–25% of the
remaining WEB-STRONGS instructions.

When a span closes, the fast path stops at the next marker and returns
without scanning it. On word-tagged text that marker is almost always the
next `\w`. The main loop then:

1. runs `usfm__next`: the push-back check and the `pipe_is_attr` rule;
2. scans the marker again with `scan_next`;
3. checks for an open milestone;
4. switches on the token kind, then on the marker's role;
5. calls `usfm__on_char`, which opens the span and enters the fast path
   again.

## The change

### Split `usfm__on_char`

- **`usfm__char_open(p, t)`** does what `on_char` does before the fast
  path. It returns the new node, or `USFM_NIL`:
  - `usfm__ensure_inline`;
  - for a plain (non-`+`) marker, the implicit-close loop over open
    character spans, with `usfm__closer_ahead`;
  - `usfm__open_tok`.
- **`usfm__on_char`** calls `usfm__char_open`, then `usfm__char_body_fast`
  as today: when the open succeeded, `p->fast` is set, and nothing is
  pushed back.
- **Every caller of `on_char` is unchanged:** the CHAR role, `\xt` outside
  a note, and an unknown marker with a closer ahead.
- **No recursion:** `char_body_fast` calls `char_open`, never `on_char`.

### Markers in `usfm__char_body_fast`

The fast path keeps track of the span it is reading:
- `cur_offset` and `cur_length`: the opening marker's bytes;
- `closed`: whether that span has been closed.

At a marker that is not an escape:

1. **The byte-compare closer.** It runs only while `closed == 0`, because
   only then is the top frame the span whose bytes are kept. It is
   unchanged.
2. **Every other marker** is scanned once, with `usfm__scan_marker`.
3. **A scanned closer:** `MARKER_END` with a name, `closed == 0`, and a
   match from `usfm__frame_matches` against the top frame. It closes the
   span as today.
4. **A character opener:** `MARKER`, no milestone flags, a known row
   (`row != USFM_ROW_NONE`) and `role == USFM_ROLE_CHAR`.
   - Assert that `expect_arg` is NONE (no CHAR row takes an argument).
   - Call `usfm__char_open`.
   - On success, the new span becomes the one being read: `closed = 0`,
     and the opener bytes come from the token, computed as in the step A
     hook. Continue.
   - On failure (the depth limit: `open_tok` already appended the marker
     as TEXT, or `p->failed` is set), return.
5. **Anything else:** `usfm__push_back`, then return. That covers `\v`,
   paragraphs, notes, note parts, milestones, unknown markers, and closers
   that do not match.
   - This also changes a case that exists today: after a close, the fast
     path used to return without scanning, so the main loop scanned the
     marker again. Now the main loop takes the token from the push-back
     slot.

### Unchanged

- The rule after a close: continue only while the top frame is PARA,
  CELL, NOTE or CHAR.
- `pipe_is_attr`, attributes, `//`, text and the trim bookkeeping.
- The bounded-loop argument and the per-pass asserts: every pass consumes
  input or returns. An open consumes the marker.

## Equivalence

1. **What happens next is the same.** Every path into `usfm__on_char`
   returns right after it, so handling the next token inside the fast path
   happens in the same order as the main loop would.
2. **No main-loop step is skipped.**
   - The milestone check needs a MILESTONE top frame, which never happens
     here: tops are PARA, CELL, NOTE or CHAR.
   - The UTF-8 check does nothing for markers.
   - `pipe_is_attr` doesn't affect `scan_marker`.
3. **The opener is handled the same way.** A CHAR-role marker that reaches
   the main loop goes to `usfm__on_marker`, then to `usfm__on_char`. The
   fast path calls the same `usfm__char_open` and then continues the same
   body.
4. **A pushed-back token matches.** It is the same token and scanner
   position the main loop's own scan would produce: `scan_marker` is the
   same code, and `expect_arg` is set the same way.

## Proving it

The step A differential (unit tests, corpus and fuzzer, fast against
reference) must stay at 0 on every SIMD path, with unchanged checksums.

New `CHECK_FAST` cases in `tests/test_fastpath.c`:
- runs of spans: `\w a\w* \w b\w* \w c\w*`, with attributes;
- `\+w` inside `\wj`, with closers of both spellings;
- a plain `\w` inside an open `\w`, with and without a closer ahead
  (implicit close);
- a span opened in the fast path at the depth limit;
- `\xt` outside a note after a closer;
- a `\v`, `\f`, `\p`, milestone and unknown marker right after a closer
  (push-back);
- an opener straight after an opener (`\w a \nd b\nd*\w*`), and in a note
  and a cell.

Mutation checks (each must fail the tests):
- not resetting `closed` after an open;
- keeping the old opener bytes after an open;
- skipping the implicit-close loop in the fast-path open;
- taking openers with a milestone flag.

## Measuring

The same method as step A:
- CPU 2, fastest of 27 passes, 3 rounds, asserts on;
- AVX2 and scalar builds, all six corpora;
- against the commit before B.1.

**Keep B.1 if:**
- WEB-STRONGS and GRC-BYZ improve by at least 3% in cycles on both builds;
- no other corpus slows by more than 1%;
- every test and differential passes.

Expected: 5–10% on word-tagged text.

## Not in this step

- `\v` on the direct path, the next most common marker in tagged text.
- Paragraph markers.
- Each gets its own section, decided after B.1 is measured.

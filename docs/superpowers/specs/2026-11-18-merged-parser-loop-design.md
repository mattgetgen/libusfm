# Merging the scanner into the parser loop, step A: a character-span fast path

Date: 2026-11-18. Branch: `feat/simd` (0.1.1). No API change.

## Why

### Where the time goes

On word-tagged text (WEB-STRONGS, GRC-BYZ), libusfm spends about 145
instructions per token, at about 5.5 instructions per cycle. Half of the
time is the scanner and half is building the tree. A tagged word
(`\w beginning|strong="H7225"\w*` and the text after it) is five tokens.
Each token makes the same round trip:

1. `usfm__next` checks for a pushed-back token and recomputes
   `pipe_is_attr` from the top frame.
2. `usfm__scan_next` decides what comes next and fills a 32-byte
   `USFM_ScanToken`.
3. The main loop checks for an open milestone, then `switch`es on the
   token kind.
4. The handler checks the top frame again and appends a node.

### Where usfm3 0.3.2 is now

usfm3 0.3.2 is within 1.0–1.65x of libusfm. The gap is smallest on plain
text and largest on word-tagged text, which is exactly the case this
overhead hurts.

### The plan

The roadmap's Phase 1 merges the scanner into the parser. This spec
covers **step A**: one fast path for the dominant pattern, measured
before any wider change (step B, decided later).

## Step A

### Trigger

The trigger is the generic path opening a character span. That is
`usfm__on_char`, reached for markers with `USFM_ROLE_CHAR` (`\w`, `\+w`,
`\nd`, `\wj`…). Note parts go through `usfm__on_note_part` and never
trigger it.

The opening keeps all its existing logic:
- the closer look-ahead and implicit closing of outer spans;
- `usfm__ensure_inline`;
- diagnostics;
- the depth limit.

### When it runs

`usfm__char_body_fast(p)` runs right after `usfm__open_tok` returns, and
only when all of these hold:
- `p->fast` is set;
- the open succeeded, so the new CHAR frame is the top frame. When opening
  fails at the depth limit, the marker becomes TEXT and the fast path
  does not run.
- no token is pushed back (`p->has_peeked == 0`).

### What it does

It starts at the scanner's position, with `p->scan.pipe_is_attr = 1`
(the top frame is CHAR, which is exactly what `usfm__next` would compute)
and `expect_arg` zero (character markers take no argument).

1. **Text.** If the next bytes are not a marker (a `\` that does not
   start an escape), `|` or `//`, it scans a text run with the scanner's
   existing
   text-end function: the block-mask version or the byte loop, whichever
   the build uses. Then it does exactly what the generic loop does for a
   TEXT token under a CHAR top frame:
   - `usfm__check_utf8` when `p->check_utf8` is set;
   - append the TEXT node with the token's cooked flags plus
     `USFM_TF_INVALID_UTF8`;
   - set `p->text_node`, `p->text_offset` and `p->text_first_loose`.
2. **Attributes.** If the next byte is `|`, it scans an attribute run
   with the existing attribute-end function, including the trimming of
   trailing whitespace. Then it does what `usfm__on_attributes` does for a
   CHAR top frame: the UTF-8 check, then append ATTRIBUTES with the
   invalid bit.
3. **Closer.** If the next bytes are an end marker (`\name*` or
   `\+name*`) and `usfm__frame_matches` accepts it for the top frame, it
   closes the span as `usfm__on_close` does for a top-frame match: update
   `last_end` from the closer's end, then `usfm__close_top`. The marker is
   classified with the same `usfm__scan_marker` code, through a scanner
   step that fills a token on the stack. A cheaper name compare is a step-B
   question.
4. **Text after the closer.** If the new top frame is PARA, CELL, NOTE or
   CHAR (not a block, table or milestone, where blank text and implied
   paragraphs need decisions), it reads a following text run as in
   step 1.
5. **Return.** In every other case it returns at the current position
   with no pending token, and the generic loop continues. That covers:
   - another marker, or a closer that doesn't match;
   - `//`;
   - the end of input;
   - a top frame outside the list in step 4.

   Each step either consumes at least one byte or returns, so the loop
   stays bounded by the input length.

### Equivalence

- **Same tokens.** The scanner primitives and scanner state are the same
  as the generic loop's, so every token boundary falls on the same byte.
- **Same node work.** Each step performs the same node and diagnostic
  operations as the generic handler would for that token kind and top
  frame, in the same order. So the node array, diagnostics, chapter and
  verse tables, trimming state and `last_end` are identical.

## Proving it

- **The switch.**
  - `USFM_Parser` gains `uint8_t fast`, taken from the existing reserved
    bytes, with no layout change.
  - The public `USFM_Ast_ParseOpts` sets it to 1.
  - Under `USFM_TEST_SCAN_REFERENCE` (test builds only), an internal entry
    point `usfm__parse_opts_impl(…, int fast)` lets tests parse with the
    fast path off. The generic loop is the reference, so the parser isn't
    duplicated.
- **The differential.** Every parse in the tests runs both ways and
  requires byte-identical results: the node array, the diagnostics array
  (including dropped counts), the chapter and verse tables, and the `err`
  outcome. On a mismatch it reports the first differing index, both
  values, and the input.
  - **Unit tests:** `check_tree_at` and `check_diags_at` in
    `tests/helpers.h`.
  - **Corpus test:** every file, on every SIMD path, with a new line
    `Fast-path differences: N` that must be 0.
  - **Fuzzer:** every input, with the same allocation-failure schedule on
    both parses.
- **Targeted unit tests** (`tests/test_fastpath.c`):
  - a closer for a different marker;
  - `\+w` inside `\wj`;
  - attributes with no closer;
  - `//` and an escape inside a span;
  - a span running to the end of input;
  - invalid UTF-8 inside a span (text and attributes);
  - a span opened at the depth limit;
  - a blank run after the closer, followed by a paragraph marker;
  - a closer followed directly by another opening marker;
  - a span inside a note, a cell and a milestone context;
  - CRLF text with trimming.

## Measuring

- **Method:** `tests/bench.c` on CPU 2, fastest of 27 passes, with
  instruction and cycle counters. AVX2 and scalar builds, all six
  corpora, before and after. Instructions per token come from the known
  token counts.
- **Profile:** one callgrind run on WEB-STRONGS before and after.
- **Keep A if:**
  - WEB-STRONGS and GRC-BYZ improve by **≥ 10% in cycles** on both
    builds;
  - no other corpus slows by more than 2%;
  - every test and differential passes.
- **Deciding on B:**

  | A's gain on WEB-STRONGS and GRC-BYZ | Next step |
  | --- | --- |
  | ≥ 25% | report, and suggest stopping or deferring B |
  | 10–25% | the new profile guides B's first step: moving the marker open onto the direct path |
  | < 10% | rethink before B |

  B gets its own spec section and plan. This spec commits only to A.

## Not in scope

- Markers other than a matching closer, arguments, notes, milestones,
  tables. Those are for step B.
- The closer look-ahead (`usfm__scan_segment`), which keeps using the
  scanner.
- Public API changes.

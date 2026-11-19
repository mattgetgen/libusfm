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

## Result of step A (2026-11-19)

### Measured

- **Method:** as in Measuring. clang 14, `-std=c99 -O3 -march=native`
  as in `build.sh bench`, asserts on (no `-DNDEBUG`). AVX2 adds
  `-mno-avx512f`; scalar adds `-DUSFM_SIMD_NONE`.
- **Before:** `86dd228`. **After:** `f22189a`.
- `taskset -c 2 bench -n 27`, 3 rounds, base and new interleaved. The
  minimum cycles, and the instructions of that pass.
- **Tokens:** the tokens the scanner hands the parser in one parse,
  counted with an instrumented copy of the baseline. WEB-STRONGS has
  3.55M. On this count the baseline spends 292 instructions per token on
  WEB-STRONGS, not "about 145" as in Why.

AVX2:

| Corpus | Cycles before | after | Δ | Instr. before | after | Δ | Instr./token |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| WEB | 19.4M | 19.8M | +2.1% | 88.5M | 89.5M | +1.1% | 447 → 452 |
| WEB-STRONGS | 190.2M | 149.4M | **−21.4%** | 1036.1M | 800.2M | −22.8% | 292 → 225 |
| GRC-BYZ | 38.4M | 30.7M | **−20.1%** | 214.2M | 166.3M | −22.3% | 299 → 232 |
| HBO-WLC | 9.0M | 9.2M | +1.6% | 43.6M | 44.0M | +0.9% | 591 → 596 |
| ARB-VD | 13.4M | 13.7M | +2.2% | 63.0M | 63.6M | +1.0% | 589 → 595 |
| CMN-CUV | 23.4M | 22.5M | −3.9% | 112.4M | 102.6M | −8.7% | 393 → 359 |

Scalar:

| Corpus | Cycles before | after | Δ | Instr. before | after | Δ | Instr./token |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| WEB | 34.7M | 34.8M | +0.4% | 140.0M | 140.4M | +0.3% | 707 → 710 |
| WEB-STRONGS | 253.4M | 217.7M | **−14.1%** | 1193.7M | 982.1M | −17.7% | 336 → 277 |
| GRC-BYZ | 53.2M | 45.5M | **−14.4%** | 252.3M | 208.4M | −17.4% | 352 → 291 |
| HBO-WLC | 30.0M | 30.1M | +0.3% | 100.5M | 100.8M | +0.3% | 1362 → 1366 |
| ARB-VD | 44.2M | 44.4M | +0.4% | 142.5M | 143.0M | +0.4% | 1332 → 1338 |
| CMN-CUV | 35.1M | 33.5M | −4.4% | 148.8M | 138.1M | −7.2% | 520 → 483 |

- These match the prototype within half a point, except on the small
  AVX2 corpora (prototype: 0 to +1.5%).
- **Rechecked** the three small corpora with 9 rounds. AVX2: WEB +2.3%,
  ARB-VD +1.4%, HBO-WLC +1.1%. Scalar: all under +0.6%.
- On WEB, AVX2, the commit before the close helper and asserts
  (`ad92b5d`) was +1.4%; `f22189a` is +2.1 to +2.6%. Its instructions
  differ by 0.04%, so this looks like code placement, not work.

### Acceptance

- **WEB-STRONGS and GRC-BYZ ≥ 10% in cycles, both builds:** pass.
  −21.4% and −20.1% (AVX2), −14.1% and −14.4% (scalar).
- **No other corpus more than 2% slower:** borderline.
  - Scalar passes everywhere (≤ +0.4%).
  - AVX2 WEB is +2.1% (3 rounds) and +2.3% (9 rounds), over the line by
    a few tenths of a point. ARB-VD is +2.2% in 3 rounds, +1.4% in 9.
  - Instructions grow 1.0–1.1% on these corpora (not broken down
    further).
- **Tests and differentials:** pass (below).

### Findings from the implementation

- **`USFM__INLINE`.** The scanner refactor (`f001c5b`) moved the text
  and attribute branches into helpers shared with the fast path.
  Without forced inlining, the refactor alone cost 2–3% on plain text
  in the scalar build: clang stopped inlining the helpers into
  `usfm__scan_next_with`. `USFM__INLINE` (`always_inline`) removes the
  cost.
- **An equivalent mutant.** Dropping the trim bookkeeping in the fast
  path (`text_node`, `text_offset`, `text_first_loose`) survives every
  test. It only disables the O(1) shortcut in `usfm__trim_last_text`;
  the fallback rescans the trimmed node for CR/LF/TAB and gives the same
  flags. No test can tell them apart, so the mutant is equivalent.

### Verification

- **Differential:** 0 fast-path differences on every SIMD path, in the
  unit tests and the corpus test.
- **Fuzzer:** a 10-minute run, 824k executions, no findings.
- **Review:** the reviewer's own differential, 64,900 parses, 0 diffs.

### Profile

`valgrind --tool=callgrind`, AVX2 build with `-g`, `bench -n 1` on
WEB-STRONGS (one warmup parse and one timed parse). Valgrind decodes
AVX2. Totals: 2074M instructions before, 1602M after (−22.7%).

As compiled (after inlining), self and inclusive, % of the total:

| Before | Self | Incl. | After | Self | Incl. |
| --- | ---: | ---: | --- | ---: | ---: |
| `usfm__scan_next` | 52.6% | 52.6% | `usfm__on_char` | 56.8% | 68.1% |
| `USFM_Ast_ParseOpts` | 21.6% | 100% | `usfm__scan_marker` | 15.2% | 15.2% |
| `usfm__on_text` | 10.3% | 13.4% | `USFM_Ast_ParseOpts` | 7.9% | 100% |
| `usfm__open` | 4.7% | 4.7% | `usfm__open` | 6.1% | 6.1% |
| `usfm__ensure_inline` | 4.7% | 4.7% | `usfm__scan_next` | 5.8% | 21.0% |
| `usfm__on_char` | 2.1% | 9.5% | `usfm__open_tok` | 2.5% | 8.6% |
| `usfm__open_tok` | 1.9% | 6.6% | `usfm__ensure_inline` | 2.1% | 2.1% |
| `usfm__utf8_valid_simd` | 0.9% | 0.9% | `memcmp` (libc) | 1.6% | 1.6% |
| `usfm__trim_last_text` | 0.5% | 0.5% | `usfm__trim_last_text` | 0.6% | 0.6% |
| `usfm__take_argument` | 0.3% | 0.7% | `usfm__take_argument` | 0.4% | 0.9% |

The fast path is inlined into `usfm__on_char`, so the table above hides
it. By source function (inlined code counted where it is written), % of
the total:

| Before | | After | |
| --- | ---: | --- | ---: |
| no line info | 16.2% | no line info | 15.3% |
| `usfm__scan_next_with` | 8.4% | `usfm__char_body_fast` | 13.9% |
| `usfm__append` | 7.7% | `usfm__append` | 9.4% |
| `usfm__scan_marker` | 7.4% | `usfm__scan_marker` | 7.1% |
| `usfm__scan_next` | 5.5% | `usfm__text_end_blocks` | 5.0% |
| `usfm__on_text` | 4.7% | `usfm__flag_span` | 3.6% |
| `usfm__next` | 4.0% | `usfm__on_char` | 3.4% |
| `usfm__text_end_blocks` | 4.0% | `usfm__open` | 3.0% |
| `usfm__top_kind` | 3.7% | `usfm__load_block` | 2.7% |
| `usfm__load_block` | 3.4% | `usfm__classify_block` | 2.3% |
| `usfm__ensure_inline` | 2.8% | `usfm__open_tok` | 2.3% |
| `usfm__flag_span` | 2.8% | `usfm__scan_next_with` | 2.2% |

- **What went away:** most of the generic round trip for text,
  attributes and closers. `usfm__next`, `usfm__scan_next(_with)`,
  `usfm__top_kind` and `usfm__on_text` fell from 26% to under 7%.
- **What remains:**
  - the span body itself (`usfm__char_body_fast`, 14%) and node appends
    (9%);
  - the marker open: the generic scan of the opening marker, then
    `usfm__scan_marker`, `usfm__marker_find` and `usfm__marker_hash`,
    `usfm__on_char`, `usfm__open_tok`, `usfm__open` and
    `usfm__ensure_inline`. Together about 20% of the total, about 25%
    with the generic scanner round trip that delivers the marker;
  - the byte-for-byte closer compare calls libc `memcmp` 1.36M times per
    parse, 1.6% of the total. The scanner avoids `memcmp` in its hot
    path for this reason (see `usfm__marker_find`).

### Decision on B

- The gain on WEB-STRONGS and GRC-BYZ is 20–21% (AVX2) and 14% (scalar).
  Both are in the **10–25%** row.
- So the new profile guides B's first step: **moving the marker open
  onto the direct path.** The profile agrees: the marker open is now the
  largest piece outside the span body.
- Smaller items for B's plan:
  - replace the `memcmp` call in the closer compare with an inline byte
    loop;
  - look at the +2% on small AVX2 corpora (the per-open fast-path check,
    and code placement).

### Follow-ups (2026-11-19)

Two small changes, measured as above (3 rounds; 9 rounds where noted).

- **`perf: always inline usfm__append` (`5103600`).** With asserts on,
  the code is byte-identical: clang already inlined it. With `-DNDEBUG`,
  WEB-STRONGS gets 6.5% faster (AVX2) and 2.4% faster (scalar), so NDEBUG
  is now faster than asserts on (AVX2 −2.6%, was +4.1%).
  `usfm__top_kind` was already inlined everywhere, so it is unchanged.
- **`perf: inline closer compare instead of memcmp` (`303a8e0`).** An
  inline byte loop replaces the `memcmp`. AVX2: −1.6% on WEB-STRONGS and
  GRC-BYZ. It is also −1.6 to −1.8% on WEB, HBO-WLC and ARB-VD, where the
  instruction count does not change, so that part is code placement.
  Scalar: −1.1% to +0.3%. A new test (a same-length closer that differs
  only in its last byte) kills the `open_length - 1` mutant, which used
  to survive every test.

After both, against `86dd228` (cycles; instructions):

| Corpus | AVX2 | | Scalar | |
| --- | ---: | ---: | ---: | ---: |
| WEB | +0.5% (+0.8%, 9 rounds) | +1.1% | +0.5% (+0.6%, 9 rounds) | +0.3% |
| WEB-STRONGS | **−22.7%** | −23.2% | **−14.1%** | −18.4% |
| GRC-BYZ | **−21.6%** | −22.8% | **−15.1%** | −18.1% |
| HBO-WLC | −0.1% (0.0%, 9 rounds) | +0.9% | +0.0% (0.0%, 9 rounds) | +0.3% |
| ARB-VD | −0.1% (−0.1%, 9 rounds) | +1.1% | +0.7% (+0.9%, 9 rounds) | +0.4% |
| CMN-CUV | −4.5% | −8.8% | −5.1% | −7.4% |

- **"No other corpus more than 2% slower" now passes** on both builds.
  AVX2 WEB is +0.5% (3 rounds) and +0.8% (9 rounds), down from +2.1% and
  +2.3%. Most of that gain is code placement, so a later change can move
  it again.
- About 1% more instructions on the small AVX2 corpora (the per-open
  fast-path check) remains. It no longer shows up in cycles.

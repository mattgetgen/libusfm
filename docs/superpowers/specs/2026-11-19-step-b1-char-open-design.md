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

## Result (2026-11-19)

### Measured

- **Method:** as in step A. clang 14, `-std=c99 -O3 -march=native`,
  asserts on (no `-DNDEBUG`). AVX2 adds `-mno-avx512f`; scalar adds
  `-DUSFM_SIMD_NONE`. `tests/bench.c` built against each version's
  `usfm.h`.
- **Versions:** before step A `86dd228`; before B.1 `c5e7d88` (tests
  only on top of `f5eeb0d`); B.1 `fbb419d`.
- `taskset -c 2 bench -n 27`, 3 rounds, all three versions interleaved.
  The minimum cycles, and the instructions of that pass. Machine idle.
- **Rechecked** WEB-STRONGS, GRC-BYZ and CMN-CUV with 9 rounds
  (`c5e7d88` and `fbb419d` only).

AVX2 (cycles and instructions in millions):

| Corpus | Cycles 86dd228 | c5e7d88 | fbb419d | Δ B.1 | Δ total | Instr. 86dd228 | c5e7d88 | fbb419d | Δ B.1 | Δ total |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| WEB | 19.35 | 19.52 | 19.56 | +0.2% | +1.1% | 88.53 | 89.48 | 89.56 | +0.1% | +1.2% |
| WEB-STRONGS | 190.07 | 147.04 | 144.92 | **−1.5%** | −23.8% | 1036.08 | 795.53 | 772.48 | −2.9% | −25.4% |
| GRC-BYZ | 38.38 | 30.09 | 29.63 | **−1.6%** | −22.8% | 214.16 | 165.34 | 160.64 | −2.9% | −25.0% |
| HBO-WLC | 8.99 | 9.05 | 9.00 | −0.5% | +0.2% | 43.58 | 43.99 | 43.99 | 0.0% | +0.9% |
| ARB-VD | 13.37 | 13.46 | 13.40 | −0.4% | +0.2% | 62.97 | 63.63 | 63.63 | 0.0% | +1.1% |
| CMN-CUV | 23.35 | 22.42 | 22.47 | +0.2% | −3.8% | 112.39 | 102.45 | 102.01 | −0.4% | −9.2% |

Scalar:

| Corpus | Cycles 86dd228 | c5e7d88 | fbb419d | Δ B.1 | Δ total | Instr. 86dd228 | c5e7d88 | fbb419d | Δ B.1 | Δ total |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| WEB | 34.60 | 34.68 | 34.79 | +0.3% | +0.5% | 140.01 | 140.43 | 140.31 | −0.1% | +0.2% |
| WEB-STRONGS | 252.26 | 216.51 | 205.86 | **−4.9%** | −18.4% | 1193.67 | 973.87 | 900.31 | −7.6% | −24.6% |
| GRC-BYZ | 53.58 | 45.24 | 43.06 | **−4.8%** | −19.6% | 252.27 | 206.67 | 191.66 | −7.3% | −24.0% |
| HBO-WLC | 29.94 | 30.04 | 30.15 | +0.4% | +0.7% | 100.47 | 100.81 | 100.81 | 0.0% | +0.3% |
| ARB-VD | 44.19 | 44.43 | 44.38 | −0.1% | +0.4% | 142.45 | 143.03 | 143.03 | 0.0% | +0.4% |
| CMN-CUV | 35.09 | 33.32 | 33.91 | **+1.8%** | −3.4% | 148.79 | 137.86 | 134.98 | −2.1% | −9.3% |

9-round recheck, cycles, B.1 against `c5e7d88`:

| Corpus | AVX2 | Scalar |
| --- | ---: | ---: |
| WEB-STRONGS | −1.6% | −5.1% |
| GRC-BYZ | −1.3% | −4.7% |
| CMN-CUV | +0.1% | +1.6% |

- The CMN-CUV scalar slowdown is steady: all 12 B.1 runs are slower than
  all 12 `c5e7d88` runs (33.79–34.09M against 33.26–33.39M cycles). It
  happens with 2.1% fewer instructions. CMN-CUV tags proper names
  (`\pn`, 33k), so the fast path now scans the marker after every `\pn*`
  (mostly `\v`, `\q`, `\pn`). Likely a branch-prediction or code-placement
  effect. Not checked further: no `perf` on this machine.

### Keep criteria

- **WEB-STRONGS and GRC-BYZ at least 3% fewer cycles, both builds:
  fail.** Scalar passes (−4.9% and −4.8%; 9 rounds −5.1% and −4.7%).
  AVX2 misses by about half (−1.5% and −1.6%; 9 rounds −1.6% and −1.3%).
  The expected 5–10% did not happen on AVX2.
- **No other corpus more than 1% slower: fail.** Scalar CMN-CUV is +1.8%
  (9 rounds +1.6%). Every other corpus is within ±0.5%.
- **Every test and differential passes:** pass (below).

**Verdict: B.1 fails its keep criteria.** It misses the AVX2 bar and
slows scalar CMN-CUV by 1.6–1.8%.

### Mutation checks

- **Killed (3):** keeping the old opener bytes after an open; skipping
  the implicit-close loop in the fast-path open; taking openers with a
  milestone flag.
- **Survives, but behaviour-equivalent (1):** not resetting `closed`
  after an open (dropping `closed = 0`). The new span then counts as
  closed. Its closer skips the byte compare and the scanned-closer
  branch, and is pushed back to the main loop. `usfm__on_close` closes
  it there exactly as the fast path would. The tree and the diagnostics
  are the same; only speed is lost. 0 corpus differences.

### Fuzz

The fast-against-reference differential fuzzer: 851k execs, 0 findings.

### Profile

callgrind, WEB-STRONGS, AVX2, `-g`, `bench -n 1` (a warm-up and one
pass). The figures below are per parse of the corpus: 796.4M
instructions before, 773.3M after.

Top functions, millions of instructions. clang inlines most of the
parser into `usfm__on_char` (the fast path) and `USFM_Ast_ParseOpts`
(the main loop).

| Function | Self before | after | Inclusive before | after |
| --- | ---: | ---: | ---: | ---: |
| `usfm__on_char` (incl. `char_body_fast`) | 463.7 | 489.1 | 540.9 | 695.1 |
| `usfm__scan_marker` (incl. classify) | 121.7 | 121.7 | 121.7 | 121.7 |
| `USFM_Ast_ParseOpts` (main loop) | 63.1 | 25.8 | 796.1 | 773.0 |
| `usfm__open` | 48.8 | 48.8 | 48.8 | 48.8 |
| `usfm__scan_next` | 46.6 | 14.1 | 168.0 | 28.1 |
| `usfm__char_open` | inlined | 21.4 | — | 98.3 |
| `usfm__open_tok` | 19.8 | 19.8 | 68.6 | 68.6 |
| `usfm__ensure_inline` | 17.2 | 17.2 | 17.2 | 17.2 |

- 680k character spans open per parse. Before B.1, 0 open in the fast
  path. After, 635k (93%) open in the fast path and 45k in the main
  loop.
- `scan_marker` runs the same number of times (766k) at the same cost.
  B.1 only moved who calls it: 680k calls now come from the fast path and
  86k from `scan_next`.

**Where opening a `\w` goes after B.1:** instructions per open in the
fast path, from per-line attribution. Inlined lines are counted where
they come from in the source.

| Part | Instr./open | Share |
| --- | ---: | ---: |
| `usfm__scan_marker` | 159 | 47% |
| — marker body (`\`, `+`, name loop, `*`, token fields) | 74 | |
| — `classify` + `marker_find` + `marker_hash` + `marker_slot` | 42 | |
| — no line info (inside `scan_marker`) | 35 | |
| — `skip_one_ws` | 8 | |
| `usfm__char_open` | 144 | 43% |
| — own body: call, flags test, implicit-close loop (exits at once: top is PARA) | 31 | |
| — `ensure_inline` | 25 | |
| — `open_tok` → `open` → `append` (node and frame push) | 90 | |
| Fast-path open branch (lines 3506–3527: role tests, call, opener bytes, asserts) | 34 | 10% |
| **Total** | **≈ 337** | |

- `closer_ahead` is never called on WEB-STRONGS. Each `\w` opens after
  its previous span closed, so the top frame is the paragraph and the
  implicit-close loop stops on its first test.
- **The round trip that B.1 removed was small.** Main loop plus
  `scan_next`, before against after:
  - 70M fewer instructions per parse: `ParseOpts` −37.3M, `scan_next`
    wrapper −32.5M. That is about 110 per open.
  - The fast path grows by 25.4M; the now out-of-line `char_open` adds
    about 9M of call and prologue.
  - Net: 23.1M, or **36 instructions per fast-path open**, which is
    2.9%.
- **This confirms the suspicion.** About 90% of the cost of opening a
  `\w` is the marker scan and classification (159) and the span open
  itself (144). The main-loop round trip was about a third of the
  opening cost (≈110 on top of ≈300 for scan and open), and B.1 kept only a third of it.
- **Main loop after B.1:** about 75M per parse, 9.7% of instructions.
  - `ParseOpts` self, excluding whole-input UTF-8 validation: 18.2M.
  - `scan_next` from the loop: 24.4M.
  - Verse handling (`take_argument`, open, trim): 14.2M.
  - `on_para`: 9.4M; `on_text`: 2.8M.
  - Main-loop `char_open`: 6.5M.

  The pure round trip (dispatch plus `scan_next`) is 42.6M, or 5.5%.
- **Tokens that still go through the main loop,** per parse:
  - `scan_next`: 115k tokens, plus 42k verse-number arguments;
  - `\v`: 38k;
  - paragraph markers (`\q#` 23k, `\p` 9k, `\b`, `\m`, …): 35k;
  - the first character span after each of these (and after notes): 45k;
  - text runs: 29k;
  - the rest: notes, `\c`, headers.

  Corpus marker counts: `\w` 640k, `\+w` 38k, `\v` 38k, `\q#` 23k,
  `\p` 9k, `\wj` 2.3k, notes 1.9k. All have matching closers.

### Recommendation

**Keep B.1 anyway, with the miss recorded. Don't try to tune it to the
bar.**
- It is a real gain on the target corpora on both builds: −1.5% AVX2,
  −5% scalar, −3% and −7.5% instructions.
- It is neutral on HBO-WLC, ARB-VD and WEB.
- It gives the fast path the structure later B steps need: every marker
  is scanned once and either handled or pushed back.
- The cost is scalar CMN-CUV, +1.6–1.8% in cycles with fewer
  instructions. Recheck it after the next step. If it stays over 1%,
  revert `fbb419d`, which is self-contained.

**The next lever is the marker scan and classification, then the span
open, not `\v` or paragraph markers.**
- **Classify / `scan_marker`.** 159 instructions per marker and 766k
  markers: 15.7% of all instructions.
  - A byte check for a repeated opener in the fast path would skip the
    hash lookup and the generic name loop. One way is to compare against
    the last opener's bytes, the way the closer is compared.
  - So would a cheaper `scan_marker` for the common short names. Find
    out first what the 35 instructions per call with no line info are.
- **Open / append.** `open_tok` → `open` → `append` is 90 per span
  (`char_open` 144 in all, 12.7% of all instructions). Trim the
  `open_tok` NIL handling and the `ensure_inline` call for the
  span-after-span case, where the top frame is known.
- **`\v` is a small lever.** The whole verse path is about 3–4% of
  instructions. Taking it into the fast path removes only its main-loop
  round trip and that of the `\w` after it. At ~36–110 instructions
  each, that is about 38k × 2 tokens, roughly 0.5–1%.
- **Paragraph markers** are smaller still (35k tokens).

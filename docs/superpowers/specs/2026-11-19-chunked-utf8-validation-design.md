# Chunked UTF-8 validation

Date: 2026-11-19. Branch: `feat/simd` (0.1.1). No API change.

**Status: measured, then reverted (2026-11-26).** Chunking alone, and
chunking only from a size threshold, both missed the keep criteria; see
[Threshold (2026-11-26)](#threshold-2026-11-26). `eba7f8b` reverts
`d8bf02e` and `3b02f55`; `e597980` keeps the per-run reference harness
and drops the chunk-size hook.

## Why

Today `USFM_Ast_ParseOpts` validates the whole input in one pass before
parsing starts: Keiser–Lemire on SIMD builds, the 32-byte-step DFA on scalar
builds.
- **Valid input** turns the per-run checks off (`p.check_utf8 = 0`).
- **Invalid input** leaves every TEXT, ARGUMENT and ATTRIBUTES run checked
  as it is scanned (`usfm__check_utf8`). The diagnostics come from those
  per-run checks.

On large books this costs two trips through main memory: the validator
streams the whole input, then the parser streams it again.

On ALB900a, a 255 MiB synthetic stress project from the dev server with
4–21 MB books of plain ASCII text, AVX2 parses in:
- 30.8 ms with validation;
- 20.6 ms without.

That is +49%. On WEB, whose books fit in cache, validation costs 4–8%.

## The change

1. **Two new parser fields:** `utf8_valid_to` and `utf8_invalid_seen`.
   - `utf8_valid_to` is the end offset of the prefix of the input known to
     be valid. It starts at 0.
   - `utf8_invalid_seen` is set once a chunk fails.
2. **The quick gate.** When validation is on, a TEXT, ARGUMENT or
   ATTRIBUTES token whose end is `<= utf8_valid_to` needs no check. This is
   one compare at the existing call sites, ahead of the out-of-line
   `usfm__check_utf8`, so the slow path stays out of the hot loop.
3. **Validating ahead.** Otherwise, while `!utf8_invalid_seen` and the
   token's end is `> utf8_valid_to`:
   - Validate the next chunk, `[utf8_valid_to, cut)`, where `cut` is about
     `utf8_valid_to + USFM__UTF8_CHUNK` (default 64 KiB), capped at the
     input length.
   - **Character boundary:** if `cut` is not the input length and
     `in[cut]` is a continuation byte (`0x80–0xBF`), move `cut` back, up to
     3 bytes, to the first byte that is not a continuation byte. If none is
     found within 3 bytes, keep `cut`: the input is invalid there, and the
     next chunk starts with a stray continuation byte and fails.
   - Valid: `utf8_valid_to = cut`. Invalid: set `utf8_invalid_seen`.

   Bounded: each pass moves `utf8_valid_to` forward by at least 1 byte or
   sets the flag.
4. **After the loop,** if the token is still not covered (an invalid chunk
   was found), run today's per-run check on the token.
5. **Full coverage.** When `utf8_valid_to` reaches the input length, set
   `p->check_utf8 = 0`. The rest of the parse then runs exactly as a valid
   input does today.
6. **Unchanged:**
   - The byte-order mark: the UTF-8 BOM is skipped, and UTF-16 and UTF-32
     are rejected.
   - `USFM_PARSE_NO_UTF8_CHECK`.
   - Diagnostic codes and spans, and token flags.
   - The public API.

   The validators (`usfm__utf8_valid_simd` and `usfm__utf8_valid_input`)
   are called on chunks instead of the whole input. A chunk may start at
   any offset, so they must not assume alignment.

## Equivalence

- **Cutting at a character boundary preserves validity.** In valid UTF-8
  every character is at most 4 bytes, so cutting before a byte that is not
  a continuation byte splits the input into two valid parts. Two valid
  parts joined are valid. So every chunk is valid if and only if the whole
  input is valid.
- **Skipping covered runs is safe.** A per-run check reports only runs
  containing invalid bytes, so skipping it on runs inside a valid prefix
  changes nothing.
- **Runs after an invalid chunk** are checked per run, as today.
- **So the output is identical:** the tree, flags, diagnostics (including
  dropped counts) and tables match today's for every input, on every path.

## Proving it

- **Test-only hooks** (under `USFM_TEST_SCAN_REFERENCE`):
  - a chunk-size override;
  - a "reference" mode that always checks every run, which defines the
    correct output.
- **Unit tests:**
  - invalid bytes at, just before and just after a chunk boundary;
  - a 2-, 3- and 4-byte character cut by the boundary;
  - an invalid sequence right after a long valid prefix;
  - stray continuation bytes, including 4 or more in a row at the cut;
  - invalid bytes in text, arguments and attributes;
  - an invalid byte in the last 3 bytes of the input;
  - the BOM.

  Each case is parsed at chunk sizes 1, 2, 3, 4, 7, 64 and 65,536 and
  compared with the reference.
- **Corpus test:** every file, on every path, at the default size and at a
  small size (e.g. 61 bytes), compared with the reference, with a
  `UTF-8 chunk differences: N` line.
- **Fuzzer:** a chunk size derived from the input, compared with the
  reference.
- **Mutation checks** (each must fail):
  - no move back to a character boundary;
  - `<` instead of `<=` in the gate;
  - not setting `utf8_invalid_seen`;
  - not clearing `check_utf8` at full coverage. This one is likely
    equivalent: it only costs speed.

## Measuring

- **Method:** CPU 2, fastest of 27 passes, 3 rounds, asserts on.
- **Builds:** AVX2 and scalar; SSSE3 as a spot check.
- **Corpora:** the six corpora plus ALB900a and 01DFM (local only, never
  committed).
- **Chunk sizes:** 16, 32, 64 and 256 KiB.

**Keep if:**
- ALB900a improves by at least 20% in AVX2 cycles;
- no other corpus slows by more than 0.5%;
- every test passes.

**Expected:**
- Most of ALB900a's validation cost goes away.
- Little change on books that fit in L2.
- Little change on HBO-WLC and ARB-VD, whose validation cost (about 22%)
  is computation, not memory traffic.

## Not in scope

Validating inside the scanner's own block classification (a fused pass).

## Result (2026-11-19)

**Commits:** `8c1edef` (test harness, no change to normal builds),
`d8bf02e` (chunked validation), `3b02f55` (one-compare gate, branch
hint, first chunk up front). Default chunk: 64 KiB.

### Measured

- **Method:** clang 14, `-std=c99 -O3 -march=native`, asserts on. AVX2
  adds `-mno-avx512f`, scalar adds `-DUSFM_SIMD_NONE`, the SSSE3 spot
  check adds `-mno-avx2 -mno-avx512f`. `tests/bench.c` built against
  each version's `usfm.h`. `taskset -c 2 bench -n 27` (ALB900a `-n 9`).
  Versions interleaved within each round. Cycles are the median over
  the rounds of each round's fastest pass.
- **Baseline:** `8c1edef`. Its normal builds are `624bf3d`'s: the
  `objdump -d` of the AVX2 and scalar bench builds differ only in the
  `__LINE__` arguments of `assert`, and are identical with `-DNDEBUG`.
- **No-validation builds** force validation off in the parser, to show
  how much of the validation cost remains.
- **Thermals:** the first round of measurements ran on an overheating
  laptop (package at 97–100 °C) and was discarded. Everything below was
  re-measured after cooling, waiting before each round until the package
  was at or below 70 °C (every round started at 42–58 °C, one at 71 °C).
  Wall-clock ms is less reliable than cycles: the first version in each
  round often runs at a higher clock.

AVX2, 64 KiB chunks, 9 rounds (cycles and instructions in millions):

| Corpus | Cycles 8c1edef | 3b02f55 | Δ | Instr. 8c1edef | 3b02f55 | Δ |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| WEB | 17.75 | 17.77 | +0.1% | 83.9 | 84.8 | +1.1% |
| WEB-STRONGS | 143.97 | 142.63 | −0.9% | 800.7 | 803.9 | +0.4% |
| GRC-BYZ | 29.05 | 28.73 | −1.1% | 164.8 | 165.5 | +0.4% |
| HBO-WLC | 8.27 | 8.30 | +0.4% | 39.3 | 39.7 | +1.0% |
| ARB-VD | 12.24 | 12.29 | +0.4% | 57.1 | 57.7 | +1.1% |
| CMN-CUV | 19.84 | 20.08 | **+1.2%** | 98.8 | 99.6 | +0.8% |
| 01DFM | 3.85 | 3.89 | **+1.0%** | 19.6 | 19.7 | +0.5% |
| ALB900a | 143.51 | 118.31 | **−17.6%** | 411.4 | 420.6 | +2.2% |

Validation cost, AVX2 (cycles with validation minus cycles without, in
millions, 5 rounds):

| Corpus | 8c1edef | 3b02f55 |
| --- | ---: | ---: |
| WEB | 1.47 | 1.33 |
| WEB-STRONGS | 5.73 | 4.32 |
| GRC-BYZ | 1.79 | 1.26 |
| HBO-WLC | 1.53 | 1.68 |
| ARB-VD | 2.14 | 2.24 |
| CMN-CUV | 0.82 | 1.18 |
| 01DFM | 0.08 | 0.11 |
| ALB900a | 46.58 | 21.79 |

About half of ALB900a's validation cost goes away; what remains is
mostly the validator's own work (HBO-WLC and ARB-VD, whose cost is
computation, do not change).

Scalar, 7 rounds (ALB900a 5), cycles in millions:

| Corpus | 8c1edef | 3b02f55 | Δ |
| --- | ---: | ---: | ---: |
| WEB | 28.72 | 29.31 | +2.1% |
| WEB-STRONGS | 186.89 | 187.44 | +0.3% |
| GRC-BYZ | 42.74 | 42.80 | +0.1% |
| HBO-WLC | 24.68 | 24.78 | +0.4% |
| ARB-VD | 35.10 | 35.43 | +0.9% |
| CMN-CUV | 32.41 | 33.09 | +2.1% |
| 01DFM | 4.66 | 4.76 | +2.1% |
| ALB900a | 608.67 | 602.23 | −1.1% |

SSSE3 spot check, cycles in millions: WEB 22.02 → 21.63 (−1.8%),
WEB-STRONGS 162.06 → 162.07 (0.0%), ALB900a 235.78 → 224.18 (−4.9%).

### The WEB regression in `d8bf02e`

`d8bf02e` cost WEB about 4% in cycles (17.84M → 18.64M) with almost the
same instruction count. A build with a 1 GiB chunk (the whole input
validated at the first text token) had the old instruction count and
was still slower, so the cost was not the chunking.

- `perf stat` on WEB: `frontend_retired.dsb_miss` 1.2M → 4.0M and
  `idq.mite_uops` 20M → 61M. `perf record` put the extra DSB misses in
  `USFM_Ast_ParseOpts`, `usfm__scan_next` and `usfm__take_argument`.
  Moving the validator out of `USFM_Ast_ParseOpts` (it shrank by 0x313
  bytes, `usfm__check_utf8` grew by 0x3f9) changed the layout of the
  hot code.
- Tried on WEB, 9 rounds, M cycles (8c1edef 17.84, d8bf02e 18.64):

  | Variant | Cycles |
  | --- | ---: |
  | gate as one compare, `end > utf8_valid_to` (no `check_utf8` load) | 18.01 |
  | gate with the compare first, then `check_utf8` | 18.27 |
  | first chunk validated at parse start | 18.20 |
  | chunk validator `USFM__NOINLINE` | 18.67 |
  | one compare + `__builtin_expect` | 18.18 |
  | one compare + `__builtin_expect` + first chunk up front | **17.76** |
  | `-falign-functions=64` (diagnostic): 8c1edef / d8bf02e | 18.00 / 18.43 |

- The last row is `3b02f55`. It brings the DSB misses back to 1.2M and
  the MITE uops to 23M. The hint alone cuts WEB-STRONGS from 813M to
  803M instructions.
- These effects are layout-sensitive. Removing the now barely used
  `check_utf8` field moved CMN-CUV and HBO-WLC by about 1%, so it was
  kept.

### Chunk size

AVX2, M cycles, 9 rounds, all sizes in one run per corpus against
8c1edef. ALB900a: 16, 32 and 64 KiB from a 9-round run (8c1edef 143.51);
128 and 256 KiB from a 5-round run (8c1edef 143.91, 64 KiB 118.52):

| Chunk | ALB900a | WEB | WEB-STRONGS | HBO-WLC |
| --- | ---: | ---: | ---: | ---: |
| 8c1edef | 143.51 | 17.86 | 143.80 | 8.23 |
| 16 KiB | 116.15 | 17.81 | 142.65 | 8.26 |
| 32 KiB | 118.84 | 17.77 | 142.79 | 8.27 |
| 64 KiB | 118.31 | 17.76 | 142.89 | 8.27 |
| 128 KiB | 118.65 | 17.77 | 142.88 | 8.29 |
| 256 KiB | 119.46 | 17.77 | 142.83 | 8.30 |

Smaller chunks help ALB900a more, and cost the smaller books more
(per-chunk calls, and no single up-front pass):

| Corpus | 8c1edef | 4 KiB | 8 KiB | 64 KiB |
| --- | ---: | ---: | ---: | ---: |
| ALB900a | 142.02 | 101.34 (−28.6%) | 110.18 (−22.4%) | 118.31 (−17.6%) |
| WEB | 17.75 | 18.00 (+1.4%) | 17.85 (+0.6%) | 17.77 (+0.1%) |
| WEB-STRONGS | 143.97 | 143.00 (−0.7%) | 142.86 (−0.8%) | 142.63 (−0.9%) |
| GRC-BYZ | 29.05 | 28.86 (−0.7%) | 28.75 (−1.0%) | 28.73 (−1.1%) |
| HBO-WLC | 8.27 | 8.43 (+1.9%) | 8.35 (+1.0%) | 8.30 (+0.4%) |
| ARB-VD | 12.24 | 12.58 (+2.8%) | 12.45 (+1.7%) | 12.29 (+0.4%) |
| CMN-CUV | 19.84 | 20.14 (+1.5%) | 20.09 (+1.3%) | 20.08 (+1.2%) |
| 01DFM | 3.85 | 3.88 (+0.8%) | 3.89 (+1.0%) | 3.89 (+1.0%) |

(ALB900a: 4 and 8 KiB from a separate 7-round run against 8c1edef at
142.02; 64 KiB from the 9-round run above.) The default stays 64 KiB.

### Keep criteria

- **ALB900a at least 20% fewer AVX2 cycles: fail at 64 KiB** (−17.6%).
  8 KiB (−22.4%) and 4 KiB (−28.6%) pass.
- **No other corpus more than 0.5% slower: fail.** At 64 KiB, AVX2:
  CMN-CUV +1.2% and 01DFM +1.0%; WEB, HBO-WLC and ARB-VD within 0.5%;
  WEB-STRONGS and GRC-BYZ about 1% faster. Scalar: WEB, CMN-CUV and
  01DFM about +2%. Smaller chunks make the small corpora worse.
- **Every test passes: pass.**
- No chunk size meets both performance criteria. Whether to keep the
  change, and at which size, is left to the user.

### Correctness

- **Tests:** `./build.sh test`, `test-neon`, `test-wasm` and `release`
  pass with no warnings. `UTF-8 chunk differences: 0` and
  `Fast-path differences: 0` on every path and corpus; checksums
  unchanged. The harness was checked by breaking the reference mode: a
  reference that never sets the flag fails 591 unit checks; one that
  flags every run also fails every corpus.
- **Move forward:** a cut that would move back to the chunk's own start
  (a chunk shorter than one character, e.g. sizes 1–3) moves forward
  instead, up to 3 bytes. Without this, a valid character would be split
  and the parser would fall back to per-run checks.
- **Mutations** (each in a copy; avx2 and scalar unit tests, WEB and
  CMN-CUV corpus tests):
  - No move back to a character boundary: equivalent in output (the
    split chunk fails and per-run checks take over), so a white-box test
    of `usfm__utf8_cut` was added (`test_utf8_chunk_cuts`); it kills the
    mutant (72 failures). No move forward: killed by the same test (24).
  - `<` instead of `<=` in the gate (the gate's `>` made `>=`):
    **equivalent**. Tokens that end exactly at `utf8_valid_to` go to the
    slow path, which finds them covered. The unsafe direction (skip
    tokens ending 1 byte past) is killed (583 failures).
  - Not setting `utf8_invalid_seen`: killed by the assert in
    `usfm__utf8_cover`. Without asserts it is equivalent in output but
    re-validates the failing chunk for every later token.
  - Not clearing `check_utf8` at full coverage: **equivalent**; since
    `3b02f55` the gate does not read it.
  - Not validating the first chunk up front: equivalent (speed only).
- **Fuzzing:** 10 minutes, `setarch -R taskset -c 0-3`, `-fork=4`, the
  dictionary, a scratch corpus. `d8bf02e`: about 545,000 runs, 0
  crashes, timeouts or OOMs. `3b02f55`: about 478,000 runs, 0 crashes,
  timeouts or OOMs.
- **AVX-512BW under Intel SDE** (`sde64 -spr`): `d8bf02e` passes, with
  0 differences and the same checksums; so does `3b02f55` (15,449,331
  checks; all 6 corpora with 0 differences and identical checksums).

## Threshold (2026-11-26)

The user's option B: inputs shorter than a threshold T keep the
whole-input pass (valid: per-run checks off; invalid: every run checked),
and only inputs of at least T are validated in chunks, with smaller chunks
since only large inputs see them. A test-only hook forced chunking at any
size so the boundary, corpus and fuzz comparisons still covered the
chunked path. Criteria, against `8c1edef`: every normal corpus (WEB,
WEB-STRONGS, GRC-BYZ, HBO-WLC, ARB-VD, CMN-CUV, 01DFM) within ±0.5% in
cycles on both AVX2 and scalar; ALB900a at least 15% fewer AVX2 cycles;
all tests pass.

**Result: no variant met them; the chunking was reverted.**

### Method

As in [Measured](#measured): clang 14, `-std=c99 -O3 -march=native`,
asserts on; AVX2 adds `-mno-avx512f`, scalar `-DUSFM_SIMD_NONE`;
`tests/bench.c` (printing cycles to 3 decimals) against each variant's
`usfm.h`; `taskset -c 2`, `-n 27` (ALB900a `-n 9`); 9 rounds, the order
of the versions rotated each round; the median over rounds of each
round's fastest pass. Before each block the package was at or below
55 °C, before each round at or below 70 °C. Round starts were 47–64 °C
in every block. **base2** is a byte-identical copy of the `8c1edef`
binary, which shows the noise: up to ±0.5%.

### Shapes tried

All at the threshold check in `usfm__parse_opts_impl`; T only decides
which inputs chunk (every normal-corpus book is under 1 MiB; the
largest, in WEB-STRONGS, is 1015 KiB).

- **A, one-compare gate:** `3b02f55`'s gate
  (`end > utf8_valid_to`); below T, a valid input sets `utf8_valid_to`
  to the input length, so the gate never fires; an invalid one sets
  `utf8_invalid_seen`, so every run is checked. The dead `check_utf8`
  field removed; **A-keep** keeps it (set, never read).
- **V2:** `check_utf8 && end > utf8_valid_to` (the byte flag first).
- **V3:** the gate exactly as in `8c1edef` (`if (p->check_utf8)`), the
  range compare moved into `usfm__check_utf8`.
- **V4:** V3 with the chunked start out of line (`usfm__utf8_start`):
  the parse start is `8c1edef`'s two lines plus one branch.
- **V4c:** V4 with the chunk functions `__attribute__((cold))`, to move
  them out of the hot functions' address range.

### AVX2, shape A, threshold × chunk

Cycle change against `8c1edef` (first row: M cycles). Round starts
55–64 °C (ALB900a 52–60 °C).

| Variant | WEB | WEB-STRONGS | GRC-BYZ | HBO-WLC | ARB-VD | CMN-CUV | 01DFM | ALB900a |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 8c1edef | 17.82 | 143.14 | 28.88 | 8.32 | 12.34 | 19.83 | 3.88 | 140.27 |
| T 256 KiB, 8 KiB | +3.92% | +0.08% | −0.09% | +1.11% | +0.66% | +2.47% | +0.36% | −23.0% |
| T 256 KiB, 16 KiB | +1.84% | −0.08% | −0.09% | +0.79% | +1.52% | +1.87% | +0.44% | −18.3% |
| T 256 KiB, 64 KiB | +4.07% | +0.19% | −0.20% | +0.70% | +1.05% | +2.68% | +0.83% | −17.5% |
| T 1 MiB, 8 KiB | +2.10% | +0.46% | −0.10% | +0.55% | +0.42% | +2.24% | +0.39% | −22.7% |
| T 1 MiB, 16 KiB | +1.89% | +0.21% | −0.09% | +1.06% | +0.60% | +2.19% | +0.31% | −18.2% |
| T 1 MiB, 64 KiB | +1.62% | +0.49% | −0.21% | +0.96% | +1.48% | +1.83% | +0.26% | −17.5% |
| T 2 MiB, 8 KiB | +2.03% | +0.32% | −0.22% | +0.95% | +0.63% | +2.70% | +0.34% | −22.0% |
| T 2 MiB, 16 KiB | +3.81% | +0.33% | −0.09% | +0.91% | +0.57% | +2.41% | +0.21% | −17.8% |
| T 2 MiB, 64 KiB | +2.16% | +0.35% | −0.05% | +1.32% | +0.15% | +2.02% | +0.28% | −17.1% |
| A-keep, T 1 MiB, 8 KiB | +3.37% | +0.09% | −0.32% | +1.00% | +1.48% | +0.75% | +0.59% | −22.6% |
| V2, T 1 MiB, 8 KiB | +0.69% | +0.86% | +0.72% | +0.32% | +1.19% | +1.30% | +0.15% | −22.8% |
| `3b02f55` (64 KiB, no T) | +0.11% | −0.26% | −0.62% | +0.18% | +1.25% | +1.04% | +1.06% | −17.7% |

### Scalar, shape A, threshold × chunk

Round starts 55–56 °C (ALB900a 53–57 °C).

| Variant | WEB | WEB-STRONGS | GRC-BYZ | HBO-WLC | ARB-VD | CMN-CUV | 01DFM | ALB900a |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 8c1edef | 28.79 | 186.50 | 42.73 | 24.98 | 35.11 | 32.38 | 4.71 | 608.84 |
| base2 | −0.04% | +0.04% | −0.13% | −0.56% | +0.02% | +0.14% | +0.00% | +0.1% |
| T 256 KiB, 8 KiB | +2.42% | −0.22% | −0.37% | +0.39% | +1.12% | +0.98% | +3.50% | −4.3% |
| T 256 KiB, 16 KiB | +2.23% | −0.32% | −0.23% | +0.07% | +0.99% | +0.94% | +3.65% | −2.2% |
| T 256 KiB, 64 KiB | +2.19% | −0.32% | −0.44% | +0.39% | +0.93% | +1.09% | +3.27% | −1.0% |
| T 1 MiB, 8 KiB | +2.19% | −0.44% | −0.62% | +0.23% | +0.95% | +0.95% | +2.72% | −4.2% |
| T 1 MiB, 16 KiB | +2.33% | −0.43% | −0.56% | +0.14% | +0.93% | +0.84% | +3.44% | −2.2% |
| T 1 MiB, 64 KiB | +2.31% | −0.28% | −0.57% | +0.31% | +0.89% | +1.00% | +3.22% | −1.0% |
| T 2 MiB, 8 KiB | +2.31% | −0.32% | −0.51% | +0.13% | +0.93% | +1.07% | +3.29% | −4.0% |
| T 2 MiB, 16 KiB | +2.29% | −0.38% | −0.56% | +0.18% | +0.90% | +1.00% | +3.44% | −2.1% |
| T 2 MiB, 64 KiB | +2.11% | −0.46% | −0.40% | +0.33% | +0.98% | +1.03% | +3.01% | −1.0% |

### The other shapes, T = 1 MiB

AVX2 (each block against its own `8c1edef`; round starts 48–61 °C):

| Variant | WEB | WEB-STRONGS | GRC-BYZ | HBO-WLC | ARB-VD | CMN-CUV | 01DFM | ALB900a |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| V3, 8 KiB | +1.40% | +0.55% | +0.62% | +0.88% | +1.05% | +0.81% | +0.65% | −22.8% |
| V3, 16 KiB | +1.25% | +0.48% | +0.44% | +0.95% | +0.57% | +1.91% | +1.01% | −18.2% |
| V4, 8 KiB | +2.87% | +0.84% | +0.44% | +1.62% | +2.58% | +2.60% | +2.71% | −22.6% |
| V4c, 8 KiB | +1.31% | +0.44% | +0.06% | −0.28% | +0.94% | −0.19% | −0.08% | −23.0% |
| V4c, 8 KiB (repeat) | +1.34% | +0.45% | +0.26% | −0.02% | +0.49% | +0.60% | −0.36% | −23.4% |
| V4c, 16 KiB (repeat) | +1.09% | +0.43% | +0.41% | −0.07% | +0.87% | +0.83% | +0.21% | −18.9% |
| base2 (same block) | +0.45% | +0.01% | +0.13% | −0.04% | +0.49% | +0.19% | +0.10% | +0.1% |

Scalar (round starts 47–54 °C, ALB900a 51–59 °C):

| Variant | WEB | WEB-STRONGS | GRC-BYZ | HBO-WLC | ARB-VD | CMN-CUV | 01DFM | ALB900a |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 8c1edef | 28.86 | 187.08 | 42.60 | 24.86 | 35.19 | 32.41 | 4.71 | 608.28 |
| base2 | −0.17% | +0.13% | +0.19% | −0.41% | −0.05% | +0.02% | −0.13% | +0.0% |
| A, 8 KiB | +2.24% | −0.54% | −0.16% | +0.41% | +1.54% | +0.98% | +2.89% | −4.1% |
| A-keep, 8 KiB | +0.96% | −0.18% | +0.15% | −0.14% | +0.43% | +0.70% | +1.10% | −4.2% |
| V3, 8 KiB | +2.08% | +0.42% | +0.82% | +0.07% | +0.73% | +1.77% | +1.63% | −4.1% |
| V4c, 8 KiB | +3.39% | +0.27% | +0.31% | +0.78% | +1.41% | +2.92% | +1.95% | −4.0% |

### Findings

- **ALB900a passes easily on AVX2** in every shape: −22% to −23% at
  8 KiB chunks, −18% at 16 KiB, −17% at 64 KiB. Scalar gains only 1–4%
  (the scalar DFA, not memory, is the cost there).
- **The normal corpora fail on every shape.** Shape A is +1.6% to
  +4.1% on AVX2 WEB and +2.1% to +3.7% on scalar WEB and 01DFM, at every T and
  chunk size. The best AVX2 shape, V4c at 8 KiB, is within 0.5% on six
  of seven corpora but +1.3% on WEB in both blocks; the same code on
  scalar is +3.4% on WEB and +2.9% on CMN-CUV. No variant is within
  0.5% everywhere on both builds.
- **It is code layout, not work.** The extra instructions are small:
  shape A's gate (add and 32-bit compare instead of a byte test) adds
  0.3–0.6%; V3, V4 and V4c run 0.1–0.2% *fewer* instructions than
  `8c1edef` on every normal corpus and are still slower. `perf stat` on
  WEB and CMN-CUV, V3 against `8c1edef`: `frontend_retired.dsb_miss`
  1.2M → 1.4–1.5M, `idq.mite_uops` 20–25M → 23–31M; V4c on WEB: DSB
  misses equal, branch misses +6%. Every hot function after
  `USFM_Ast_ParseOpts` moves, and their alignment decides 1–3%. Variants
  that differ only in the T constant differ by up to 1.9% on WEB.
- **T = 256 KiB costs the mid-size books** with the V3/V4 gate (one call
  per token while chunking): WEB-STRONGS +8.9%, GRC-BYZ +7.6%. Shape A
  avoids that but still fails WEB and CMN-CUV.
- **Removing the dead `check_utf8` field** (shape A against A-keep, T
  1 MiB, 8 KiB): AVX2 CMN-CUV +2.24% vs +0.75%, WEB +2.10% vs +3.37%,
  ARB-VD +0.42% vs +1.48%; scalar WEB +2.24% vs +0.96%, 01DFM +2.89% vs
  +1.10%. Mixed, ±1.5%: layout again, with no consistent direction.
- **Correctness** (shape A, T 1 MiB, 8 KiB, before the revert): AVX2
  and scalar unit tests passed (15,449,331 checks), comparing a normal
  parse and chunks forced at 1, 2, 3, 4, 7, 64, 8 KiB and 64 KiB with
  the reference.

### What was reverted, what was kept

- **Reverted** (`eba7f8b`): `d8bf02e` and `3b02f55`, with the white-box
  chunk-cut test. `USFM_Ast_ParseOpts` is again `8c1edef`'s: the AVX2 and
  scalar bench builds are byte-identical to `8c1edef`'s with `-DNDEBUG`,
  and with asserts their `objdump -d` is identical (only the `__FILE__`
  strings differ).
- **Kept** (`e597980`): the always-per-run reference mode
  (`USFM__ParseHooks.utf8_reference`) and its comparison with a normal
  parse, now `check_utf8_reference`, for every unit-test tree, every
  corpus book (`UTF-8 reference differences`) and every fuzz input. The
  boundary cases moved to `tests/test_utf8_reference.c`; their offsets
  61–68 straddle the SIMD validators' 32- and 64-byte steps.
- **Dropped:** the chunk-size hook and its loops; byte 0 of a fuzz input
  again picks only the version.
- **Checks after the revert:** `./build.sh test`, `test-neon`,
  `test-wasm` and `release` exit 0 with no warnings; every path and
  corpus has 0 scanner, classifier, fast-path and UTF-8 reference
  differences and the usual checksums. Under Intel SDE, `test-avx512`
  passes (15,448,764 checks). Mutants: a whole-input pass that turns the
  per-run checks off even for invalid input fails 1,196 checks; a
  reference that checks nothing fails 591. Fuzzing 10 minutes
  (`setarch -R taskset -c 0-3`, `-fork=4`, the dictionary, a scratch
  corpus): 704,178 runs, 0 crashes, timeouts or OOMs.
- If large inputs matter later, the gain is real (about 23% on AVX2) but
  needs a change that leaves the hot functions' layout alone, or a
  layout-stable build (e.g. aligned functions) to judge it by.

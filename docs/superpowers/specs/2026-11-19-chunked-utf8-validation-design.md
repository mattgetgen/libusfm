# Chunked UTF-8 validation

Date: 2026-11-19. Branch: `feat/simd` (0.1.1). No API change.

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

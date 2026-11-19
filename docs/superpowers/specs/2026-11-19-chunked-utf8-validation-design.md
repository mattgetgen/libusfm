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

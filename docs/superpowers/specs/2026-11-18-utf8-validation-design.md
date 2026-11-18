# UTF-8 validation

Date: 2026-11-18. The scalar part goes into 0.1.0 (feat/ast-parser, PR #1);
the SIMD part into 0.1.1 (feat/simd, PR #2).

## Goal

USFM is UTF-8 text, but libusfm 0.1 never checks that. Bytes of 0x80 and
above are treated as text without being looked at.

Many real files are not UTF-8. Bible translation projects that predate
Unicode support for their languages use legacy 8-bit encodings, converted
to and from Unicode with SIL's TECkit. Those encodings keep ASCII and
reassign 0x80–0xFF, so the USFM markup (backslashes, marker names, `*`,
`|`, spaces) is still intact. Only the text bytes are not UTF-8.

So:
- **Invalid UTF-8 is a diagnostic, not an error.** Parsing continues, the
  tree is the same as without validation, and every affected text,
  argument or attribute node is flagged. A caller can then convert those
  bytes (for example with TECkit) or reject the file.
- **UTF-16 and UTF-32 input is an error.** In those encodings every ASCII
  character is followed or preceded by zero bytes, so there is no USFM
  structure to recover. Parsing returns NULL.
- **Validation is on by default.** A parse flag turns it off for callers
  who have already validated, or who don't care.

Validation never changes the tree's structure, spans or chapter and verse
tables. It only adds diagnostics and the per-node flag.

## API (0.1.0)

| Addition | Value | Meaning |
| --- | --- | --- |
| `USFM_PARSE_NO_UTF8_CHECK` | `0x02` (parse flag) | skip validation: no `INVALID_UTF8` diagnostics, no `USFM_TF_INVALID_UTF8` flags |
| `USFM_ERR_UNSUPPORTED_ENCODING` | next `USFM_Status` value | the input starts with a UTF-16 or UTF-32 byte-order mark; `USFM_Err.pos` is 0 |
| `USFM_DIAGNOSTIC_INVALID_UTF8` | 19 (`CODE_COUNT` becomes 20) | a run contains bytes that are not valid UTF-8; severity ERROR; message "invalid UTF-8" |
| `USFM_TF_INVALID_UTF8` | `0x80` (flag bit) | on TEXT, ARGUMENT and ATTRIBUTES nodes: the node's bytes are not valid UTF-8 |

- **The encoding check ignores `USFM_PARSE_NO_UTF8_CHECK`.** UTF-16 and
  UTF-32 can never parse, so they are always an error.
- **`0x80` is free on these nodes.** For marker tokens it means
  `USFM_TF_SELF_CLOSED`, but TEXT, ARGUMENT and ATTRIBUTES nodes never
  carry marker flags. The header comment on the flag enums changes to
  say that text flags apply to TEXT nodes, and the new flag also to
  ARGUMENT and ATTRIBUTES nodes.
- **Cooking:** `USFM_TEXT_FLAGS_COOK` (`0x70`) is unchanged. Invalid
  UTF-8 is not something a consumer "cooks"; it's reported.

### Byte-order marks

| Input starts with | Result |
| --- | --- |
| `EF BB BF` (UTF-8) | skipped, as today; the first token starts after it |
| `FF FE 00 00` (UTF-32 LE) or `00 00 FE FF` (UTF-32 BE) | `USFM_ERR_UNSUPPORTED_ENCODING` |
| `FF FE` (UTF-16 LE) or `FE FF` (UTF-16 BE) | `USFM_ERR_UNSUPPORTED_ENCODING` |
| anything else | parsed as UTF-8 (or a legacy encoding) |

The UTF-32 LE mark begins with the UTF-16 LE mark; both are the same
error. UTF-16 or UTF-32 without a byte-order mark is not detected. It
parses into nonsense with diagnostics, as today. That is documented, not
fixed.

## What counts as valid

RFC 3629 (Unicode Table 3-7, well-formed byte sequences):

| Code points | Byte 1 | Byte 2 | Byte 3 | Byte 4 |
| --- | --- | --- | --- | --- |
| U+0000–U+007F | 00–7F | | | |
| U+0080–U+07FF | C2–DF | 80–BF | | |
| U+0800–U+0FFF | E0 | A0–BF | 80–BF | |
| U+1000–U+CFFF | E1–EC | 80–BF | 80–BF | |
| U+D000–U+D7FF | ED | 80–9F | 80–BF | |
| U+E000–U+FFFF | EE–EF | 80–BF | 80–BF | |
| U+10000–U+3FFFF | F0 | 90–BF | 80–BF | 80–BF |
| U+40000–U+FFFFF | F1–F3 | 80–BF | 80–BF | 80–BF |
| U+100000–U+10FFFF | F4 | 80–8F | 80–BF | 80–BF |

Everything else is invalid:
- C0, C1 and F5–FF;
- a continuation byte (80–BF) where a lead byte is expected;
- overlong forms;
- surrogates (`ED A0`–`ED BF`);
- code points above U+10FFFF;
- a sequence cut short by the end of a run or the end of the input.

## What gets checked: runs

- **Only three kinds of token can contain a byte of 0x80 or above:**
  TEXT, ARGUMENT and ATTRIBUTES.
  - Marker names stop at the first byte that is not `[A-Za-z0-9-]`.
  - The whitespace after a marker, `\*` and `//` are ASCII.
  - A backslash followed by a non-ASCII byte is a marker with an empty
    name, and the non-ASCII byte starts the next token, a TEXT.
- **A run** is the byte span of one such token. Each is checked on its
  own.
- **Sequences can't straddle runs.** Runs are separated by ASCII bytes (a
  `\`, `|` or whitespace), and a multi-byte sequence never contains an
  ASCII byte. A sequence that a marker interrupts (`\xC3\w`) is truncated,
  and invalid in the run before the marker.
- **For each run that contains invalid bytes:**
  - **One diagnostic.** `USFM_DIAGNOSTIC_INVALID_UTF8`, whose span is the
    first invalid sequence in the run: from its first byte up to, but not
    including, the first byte that can't continue it. That's 1–3 bytes.
    The marker is the innermost open marker, as for other diagnostics.
  - **The flag.** The TEXT, ARGUMENT or ATTRIBUTES node made from the run
    gets `USFM_TF_INVALID_UTF8`.
- **Tokens that become nodes of another kind** carry the flag on the
  node they become. An unclaimed ARGUMENT becomes TEXT, and stray
  ATTRIBUTES become TEXT.
- **Tokens that become no node** still get the diagnostic. For example,
  blank text between blocks is dropped; it is ASCII by definition, so in
  practice this never happens.
- **Diagnostics are emitted as runs are scanned,** interleaved with the
  parser's other diagnostics the same way theirs are. Consequences:
  diagnostics are in parse order, not sorted by offset; INVALID_UTF8 can
  precede diagnostics for its owning marker (`\c 1\xE9`: INVALID_UTF8 at 4
  before INVALID_NUMBER at 3); and its marker is the innermost open
  container when the run is scanned, so text that opens an implied
  paragraph reports the enclosing container (NONE at book level).
- **A test checks the run property:** every byte of 0x80 or above in the
  input lies inside some TEXT, ARGUMENT or ATTRIBUTES token.

## 0.1.0: the scalar validator

```c
/* Nonzero if s[0, n) is valid UTF-8. */
static int usfm__utf8_valid(const char *s, uint32_t n);

/* Offset of the first invalid sequence in s[0, n), or n if all valid;
 * *bad = length of that sequence (1-3). */
static uint32_t usfm__utf8_check(const char *s, uint32_t n, uint32_t *bad);
```

Two steps: a fast yes/no, then a precise locator that runs only on
failure.

- **`usfm__utf8_valid(s, n)`:** a shift-DFA. A state is a bit offset (a
  multiple of 6) into a 64-bit table entry per byte:
  `state = (USFM_Utf8Dfa[b] >> state) & 63`. There are 9 states; 0
  accepts and 6 is the absorbing error state. The table is generated by
  `codegen` (block `utf8_dfa`).
- **8-byte words:** load 8 bytes with `memcpy` into a `uint64_t`. If
  `word & 0x8080808080808080` is 0 and the state is accepting, skip the
  word. Otherwise run its 8 bytes through the DFA without branches.
- **`usfm__utf8_check(s, n, &bad)`:** the branchy RFC 3629 decoder: an
  ASCII fast path, then a sequence check. A lead byte selects the
  expected length and the allowed range of the second byte (the table
  above); the remaining bytes must be 80–BF. It runs only on runs the
  DFA rejects, and is the authority for the diagnostic's offset and
  length.
- **Bounds:** both loops advance on every pass: `usfm__utf8_check` by at
  least one byte, `usfm__utf8_valid` by 8 bytes per word (one per byte in
  the tail).
- **When it runs:** `usfm__utf8_valid` runs on each TEXT, ARGUMENT and
  ATTRIBUTES token just after it is scanned, unless
  `USFM_PARSE_NO_UTF8_CHECK` is set. `usfm__utf8_check` runs only on runs
  it rejects. The bytes were just read by the scanner, so they are in L1
  cache.
- **Not fused into `usfm__text_end_scalar`:** this keeps the scanner, and
  its token-by-token comparison test, unchanged. It costs a second pass
  over cached bytes. The benchmark decides whether fusing is worth it
  later.

### Measured cost (prototype)

Release build, CPU 2, `./usfm -b 27`, MiB/s:

| Corpus | Before | Validation off | Branchy only | DFA + branchy |
| --- | ---: | ---: | ---: | ---: |
| WEB-STRONGS | 408 | 391 | 329 | 350 (−14%) |
| WEB | 677 | 669 | 573 | 601 (−11%) |
| HBO-WLC | 922 | 913 | 478 | 660 (−28%) |
| CMN-CUV | 522 | 510 | 362 | 452 (−13%) |
| ARB-VD | 908 | 898 | 457 | 645 (−29%) |
| GRC-BYZ | 477 | 461 | 342 | 406 (−15%) |

- **Before:** the library without any validation code.
- **Validation off:** with the code, parsed with `USFM_PARSE_NO_UTF8_CHECK`.
- **Branchy only:** validation on, with only the branchy decoder.
- **DFA + branchy:** the final design.
- Percentages are relative to Before.

Standalone, before the 8-byte DFA loop, the branchy decoder and a
per-byte DFA both ran at about 1 GB/s on Hebrew and Arabic; the 8-byte
DFA reaches about 2 GB/s on non-ASCII text. SIMD in 0.1.1 is expected to
bring the cost close to zero.

## 0.1.1: SIMD

`usfm__utf8_check` (the scalar decoder) stays the reference and the authority. SIMD is a filter that proves
most of the input valid, so `usfm__utf8_check` runs only where it might
find something.

- **The classifier gets one more mask:** `USFM_BlockMasks` gains
  `uint64_t utf8_error`, the input positions where the Keiser–Lemire
  validator ("Validating UTF-8 in less than one instruction per byte",
  2021; as in simdutf) reports an error. It uses three 16-entry nibble
  lookups plus checks for 3- and 4-byte sequences, with the last three
  bytes before the block as context.
- **Each SIMD path gets its own version:**

  | Path | Version |
  | --- | --- |
  | SSSE3 (detected at compile time with `__SSSE3__`, e.g. `-mssse3`, `-march=x86-64-v2` or `-march=native`) | `pshufb`; the 16-byte classifier uses it for this mask only |
  | SSE2 without SSSE3 (a plain x86-64 build) | no `pshufb`: the mask is `usfm__utf8_check` run over the block |
  | AVX2, AVX-512BW | `vpshufb` |
  | NEON | `vqtbl1q_u8` |
  | wasm simd128 | `i8x16.swizzle` |

  A pure-ASCII block (no high bits, one `movemask`) sets no bits and
  skips the lookups.
- **The context bytes:** the scanner loads blocks in increasing order but
  can skip some (long arguments are scanned byte by byte). So the three
  bytes before a block are always read from the input (`base - 3 .. base
  - 1`, or zeros at the start), never carried over from the previous
  block.
- **How a run is checked:** if no `utf8_error` bit falls in the run's
  blocks, from the run's first block through the block after its last
  byte (a truncated sequence is reported at the next byte), the run is
  valid. Otherwise `usfm__utf8_check` runs on the run, and its answer
  (offset and length) is the one used. Diagnostics and flags are byte-identical on every
  path by construction.
- **Tests:**
  - The existing classifier comparison extends to the new mask. Each SIMD
    mask must equal a scalar reference mask computed by running
    `usfm__utf8_check` (the scalar decoder) over the block.
  - Every corpus and fuzz input must give identical diagnostics and flags
    on every path.

## Tests (0.1.0)

- **Unit tests (`tests/test_utf8.c`):**
  - every row of the validity table, at its edges;
  - each class of invalid byte;
  - overlong forms, surrogates, above U+10FFFF;
  - truncation at the end of a run, at a marker, and at the end of the
    input;
  - invalid bytes in an argument and in attributes;
  - one diagnostic per run with several bad sequences, and its span;
  - the flag on each node kind;
  - `USFM_PARSE_NO_UTF8_CHECK`;
  - each byte-order mark, including the UTF-8 BOM being skipped.
- **The validator against a reference:** `usfm__utf8_check` is compared
  with a straightforward decoder in the tests: on 100,000 random byte
  strings of up to 64 bytes, and on every 1- to 4-byte string over a
  reduced alphabet of edge bytes (00, 7F, 80, 8F, 90, 9F, A0, BF, C0,
  C1, C2, DF, E0, E1, EC, ED, EE, EF, F0, F1, F3, F4, F5, FF).
  `usfm__utf8_valid` (the DFA) is compared on the same inputs: it must
  return valid exactly when the reference finds no invalid sequence.
- **A legacy-encoding fixture:** a short book in Windows-1252, with
  accented Latin text. It gets the same structure counts as its UTF-8
  version, and one diagnostic per affected run.
- **The fuzzer** also asserts the run property, and that the tree with
  validation equals the tree without it, apart from the flag bit.
- **All six corpora** produce no INVALID_UTF8 diagnostics.
- **The `usfm` tool's `-d`** shows the new diagnostic.

## Performance

- **After 0.1.0:** rerun libusfm's benchmarks with validation on, the
  default, and with it off. The release notes report both.
- **The comparison table:** uses validation on, with a note that the
  other parsers' decoding is not timed.
- **After 0.1.1:** the same again.

## Not in scope

- Detecting UTF-16 or UTF-32 without a byte-order mark.
- Converting from legacy encodings. That's for TECkit or the caller.
- Normalization (NFC/NFD), and checking for unassigned code points.

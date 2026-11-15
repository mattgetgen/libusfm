# libusfm 0.1.1: SIMD scanner design

Date: 2026-11-15
Status: approved in conversation, pending written review
Builds on: `docs/superpowers/specs/2026-11-14-ast-parser-design.md` (v0.1)

## Goal

Make the scanner faster with SIMD, without changing the public API, the
AST, or any parse result. This is a speed-only release, so it is version
**0.1.1** (`USFM_VERSION "0.1.1-dev"` until release).

**Done when** all of these hold:

- Every SIMD path produces token streams identical to the scalar scanner,
  and an AST checksum identical to the scalar build, on the WEB corpus.
- A 30-minute libFuzzer run on the AVX2 build, with the two-scanner
  comparison enabled, finds nothing.
- No corpus is slower at any instruction-set level than v0.1's scalar
  scanner on the same machine. The speedups themselves are measured and
  reported; there is no fixed target.

## Why the scanner, and what to expect

Measured on v0.1 (P-core, clang -O3, hardware counters):

| Corpus | Bytes per token | Text share of bytes | Text run length | Scanner's share of parse cycles |
| --- | ---: | ---: | --- | ---: |
| WEB (every word `\w …\|strong="…"\w*`) | 5.6 | 26% (attributes: 51%) | 3.8 bytes average; 67% are under 4 | 64% |
| HPUX (a typical Paratext project) | 26.4 | 91% | 73 bytes average; 31% are 64 or more | 47% |
| UDI1_large | 17.9 | 87% | 48 bytes average | 50% |

- The scanner is about half the parse. The other half, building the tree,
  is out of scope for this release, so the whole-parse speedup is bounded
  by roughly 2x.
- Typical translation text has long text runs, which SIMD skips quickly.
  WEB has tiny tokens, where the win must come from reusing one block
  classification across many tokens. The chosen design serves both.

## Benchmark corpora

- **WEB** is in the repository (`tests/corpus/WEB.tar.gz`) and is the only
  corpus the tests use.
- Typical Paratext projects (on the developer machine: HPUX, UDI1_large)
  are benchmark inputs only. They are never committed, and no test or
  script hard-codes a path to them: the benchmark takes corpus
  directories as arguments.

## Architecture

### The block classifier (new internal unit)

```c
/* Masks for the 64 bytes at p: bit i describes byte p[i]. */
typedef struct USFM_BlockMasks {
    uint64_t backslash;     /* '\\' */
    uint64_t pipe;          /* '|'  */
    uint64_t slash;         /* '/'  */
    uint64_t nbsp;          /* '~'  (flag only) */
    uint64_t loose_ws;      /* CR, LF, TAB (flag only) */
} USFM_BlockMasks;

static void usfm__classify_block(const char *p, USFM_BlockMasks *m);
```

- One implementation per instruction set, chosen at compile time:

  | Path | Selected when | How 64 bytes are classified |
  | --- | --- | --- |
  | AVX-512BW | `__AVX512BW__` | one 64-byte compare per byte value, straight into mask registers |
  | AVX2 | `__AVX2__` | two 32-byte halves, `movemask` |
  | SSE2 | x86-64 (always available) | four 16-byte parts, `movemask` |
  | NEON | `__ARM_NEON` (ARM64) | four 16-byte parts, narrowing-shift bitmask |
  | wasm simd128 | `__wasm_simd128__` | four 16-byte parts, `i8x16.bitmask` |
  | scalar | anything else, or `USFM_SIMD_NONE` | byte loop |

- The scalar classifier is always compiled; it is the reference every SIMD
  classifier is tested against.
- **No reads past the input.** A block that would extend past the end of
  the input is copied into a zeroed 64-byte stack buffer first (AVX-512
  may use a masked load instead). Zero bytes are not special, so the
  padding never sets a mask bit.
- An internal macro names the selected path (for tests and the
  benchmark to print).

### Two kinds of special byte

- **Stop bytes** end a text run: `\` (unless it starts an escape), `|`
  (only where attributes are allowed) and `/` (only when the next byte is
  also `/`).
- **Flag-only bytes** never end a text run: `~` sets
  `USFM_TF_HAS_NBSP`; CR, LF and TAB set `USFM_TF_HAS_LOOSE_WS`.

v0.1's scalar loop stops at both kinds and then continues. With masks,
the stop mask alone decides where a token ends; the flag masks are tested
once per token (AND with the token's bit range), so the flags stay exact
at about one instruction per token.

### The scanner's cached block

- The scanner keeps the offset of its current 64-byte block and that
  block's masks. A new block is classified only when scanning crosses into
  it, so one classification serves every token inside the block (about 11
  tokens per block on WEB).
- **Text loop:** the next stop candidate is the lowest set bit at or after
  the current position in `backslash | slash | (pipe if attributes are
  allowed)`. A scalar check at that one position decides what it is: an
  escape (`\\ \/ \| \~ \"`) continues the run, a lone `/` continues it,
  `//` ends it. The byte after a `\` or `/` may lie in the next block.
- **Attributes loop** (51% of WEB's bytes): the next stop is the next `\`
  that does not start an escape (attribute escapes also include `\=`).
- Marker names (2-3 bytes on average) and arguments (1-2 bytes) stay byte
  loops: they are too short to gain.
- The closer lookahead copies the scanner, so it benefits automatically.

### Selection and configuration

- The path is chosen from the compiler's target macros; there is no
  runtime CPU detection. A build with `-mavx2`, `-mavx512bw` or
  `-march=native` gets those paths; a plain x86-64 build gets SSE2.
- `USFM_SIMD_NONE`, defined before including `usfm.h`, forces the scalar
  path. It is the only new public configuration macro, and it is
  documented in the header and the release notes.

## Testing

The risk is the scanner logic built on the masks: tokens crossing a block
boundary, escapes or `//` split across blocks, and the partial last block.
All testing is differential.

1. **Two scanners, always compared.** v0.1's scanner stays as
   `usfm__scan_next_scalar`: it is the `USFM_SIMD_NONE` path and is always
   compiled in test builds. Token streams (kind, span, marker, level and
   flags) from both scanners are compared:
   - in unit tests with crafted inputs: escapes, `//`, `|`, `~` and
     CR/LF/TAB at every offset around a 64-byte boundary, in both
     attribute modes, and every input length from 0 to 200 bytes;
   - over the whole WEB corpus in the corpus test;
   - on every input in the fuzz harness, which aborts on the first
     difference.
2. **Block classifier check.** Random and adversarial 64-byte blocks (all
   special bytes, bytes 0x80-0xFF, NUL bytes): each path's masks equal the
   scalar classifier's.
3. **AST checksum.** The corpus test prints one hash over all node arrays,
   chapter and verse tables and diagnostics. It must be identical for
   every path.
4. **Every path runs somewhere:**

   | Path | How it runs | `build.sh` mode |
   | --- | --- | --- |
   | scalar | native, `-DUSFM_SIMD_NONE` | `test` |
   | SSE2 | native, default x86-64 build | `test` |
   | AVX2 | native, `-mavx2` | `test` |
   | AVX-512BW | Intel SDE (`sde64`), found through the `USFM_SDE` environment variable; skipped with a message if unset | `test-avx512` |
   | NEON | `aarch64-linux-gnu-gcc` (fallback `zig cc -target aarch64-linux-musl`), run under `qemu-aarch64` | `test-neon` |
   | wasm simd128 | `zig cc -target wasm32-wasi -msimd128`, run under Node's WASI with the corpus directory preopened | `test-wasm` |

   - The full unit-test suite and the WEB corpus test (with the checksum)
     run on every path. The emulated and wasm builds have no ASan; the
     native builds cover that.
   - Each mode skips with a message when its tool is missing.
   - Intel SDE is downloaded by the developer under Intel's license; no
     part of it is committed.
5. **Fuzzing:** a clean 30-minute libFuzzer run on the AVX2 build with the
   two-scanner comparison on.

## Benchmark

- A committed `build.sh bench` mode builds a whole-corpus benchmark that
  takes corpus directories as arguments (WEB by default). It reads all
  files first, then reports the fastest of N passes: wall time, MiB/s,
  and instructions/cycles/L1D misses through `perf_event_open` when
  available.
- Measured for scalar, SSE2 and AVX2 (and AVX-512 if hardware is ever
  available; SDE timings are not meaningful) on WEB, HPUX and UDI1_large,
  against v0.1.

## Scope

In scope:

- the block classifier (6 implementations) and the scanner's text and
  attributes loops on cached block masks;
- the two-scanner comparison, classifier check and AST checksum tests;
- `build.sh` modes `test` (all native paths), `test-avx512`, `test-neon`,
  `test-wasm` and `bench`;
- header documentation of `USFM_SIMD_NONE`, path selection, and the
  no-reads-past-input guarantee; release notes for 0.1.1 in `docs/`.

Out of scope:

- the tree-building half of the parse;
- SIMD for marker names and arguments;
- runtime CPU dispatch;
- the libc-backed WebAssembly build mode and the `<string.h>` fix for
  `USFM_NO_LIBC` (the WebAssembly follow-up in the v0.1 spec).

## Branching

- Work happens on `feat/simd`, branched from `feat/ast-parser` at
  `78ef699`.
- The 0.1.1 pull request targets `main`. It is opened after PR #1 merges,
  with `feat/simd` rebased onto `main` first, so the PR shows only this
  work.

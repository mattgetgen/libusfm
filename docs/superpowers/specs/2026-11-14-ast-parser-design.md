# libusfm v0.1: AST parser design

Date: 2026-11-14
Status: approved in conversation, pending written review

## Goal

Ship a fast, single-header USFM parser whose public API parses USFM text
into an AST. It must be correct on real files of every USFM version from
2.x to 3.1.

**Done when** both of these hold:

- A 30-minute libFuzzer run finds nothing.
- All 83 files of the WEB corpus parse, with no failures and no sanitizer
  reports, and the totals match the counts that ParatextData, usfmtc and
  usfm3 agree on:

| Count | Expected |
| --- | --- |
| chapters | 1,402 |
| verses | 38,058 |
| paragraphs | 34,753 |
| character spans | 685,197 |
| notes | 2,218 |

The counts come from `docs/usfm-field-guide.md` §12. They were produced by
`~/repos/usfm/ParatextData/CountingSink.cs`, which counts ParatextData
parser events. Counting the markers in the WEB files checks the arithmetic:

- **Character spans:** every `\w`, `\+w`, `\wj`, `\fr`, `\ft`, `\fqa`,
  `\xt`, `\xo`, `\k`, `\+wh`, `\qs`, `\bk`, `\fl`, `\+bk` and `\fq` sums to
  exactly 685,197. So note parts count as character spans.
- **Paragraphs:** every paragraph marker, including `\h`, `\toc1-3`, `\ide`
  and `\mt#`, sums to 34,754. That is one more than 34,753, because
  ParatextData reports `\cp` (1 occurrence) as part of the chapter event,
  not as a paragraph.

libusfm's counter therefore counts:

- chapters: CHAPTER nodes;
- verses: VERSE nodes;
- character spans: CHAR nodes;
- notes: NOTE nodes;
- paragraphs: PARA nodes without `USFM_NF_IMPLIED` and whose marker is not
  `\cp`, plus HEADER nodes whose marker is not `\id`.

Per-book chapter, verse and paragraph counts from ParatextData are kept in
`tests/corpus/paratextdata-web.csv` for diffing.

## Scope

In scope, taken from `usfm_claude_api.h`. Names, struct layouts and
semantics follow that header unless this document says otherwise.

- Basic types: `USFM_Span`, `USFM_Status`, `USFM_Err`, `USFM_Version`,
  `USFM_Marker` ids, `USFM_MarkerClass`, `USFM_Config`.
- Allocator: `USFM_Alc` (sized frees), the libc default when the
  allocator is NULL, `USFM_NO_LIBC`, `USFM_Alc_Free`.
- `USFM_Ast_Parse`, `USFM_Ast_ParseOpts` (with `USFM_PARSE_COPY_INPUT`),
  `USFM_Ast_Free`.
- Accessors: `USFM_Ast_GetInput`, `GetDiagnostics`,
  `GetDroppedDiagnosticCount`, `GetNodes`, `GetChapters`, `GetVerses`,
  `GetMaxChapterNumber`, `GetBookCode`, `GetVersion`, `FindChapter`,
  `FindVerse`.
- `USFM_Node_GetLevel` / `GetFlags` / `HasFlags`.
- `USFM_DiagnosticCode_GetSeverity` / `GetMessage`.
- The chapter, chapter-verse and verse content iterators.
- The ABI static asserts for every struct above.

Out of scope for v0.1, as later milestones: the public tokenizer
(`USFM_Tokenize`), the CST, stylesheets (`.sty`), the generated `.sty`-based
tables, the attribute iterator, `USFM_DecodeText`, normalize, the mutable
AST and writer, search, the fixed and growable arenas, the
`*_GetMaxMemory` functions, and SIMD.

## Changes to the drafted API

- `enum USFM_AstKind` gains two values, appended after `USFM_AST_OPTBREAK`:
  - `USFM_AST_ARGUMENT`: a leaf for the verse number (`3-5a`), chapter
    number, book code, note caller, or `\periph` title.
  - `USFM_AST_ATTRIBUTES`: a leaf for the raw attribute text (`|lemma="x"`).

  An ARGUMENT, when present, is the first child of its owner. ATTRIBUTES
  appear where they were written: the last child of a character span or
  figure (`\w word|lemma="x"\w*`), right after the ARGUMENT of a `\periph`,
  and the only child of a milestone.
- `enum USFM_DiagnosticCode` gains `USFM_DIAGNOSTIC_IMPLIED_PARAGRAPH`.
- PARA nodes carry two node flags in the high bits of `level_flags`.
  `USFM_TF_NESTED` and the milestone flags never apply to paragraphs, so
  these bits are free on PARA nodes. The API already uses the same trick for
  the TEXT flags.
  - `USFM_NF_IMPLIED = 0x10`: the parser inserted this paragraph.
  - `USFM_NF_HEADING = 0x20`: a heading paragraph, such as `\s`, `\ms`, `\mt`
    or `\r`. The verse iterator skips these.
- `enum USFM_DiagnosticCode` gains `USFM_DIAGNOSTIC_TOO_DEEP`, emitted when
  a marker would push the stack past `USFM_STACK_DEPTH`.

## Ground rules

These are the rules from `usfm_claude_api.h`, restated because the design
depends on them:

- C99, compiles as C++, single header `usfm.h`, `USFM_IMPLEMENTATION` in
  one translation unit.
- No recursion. Every loop is bounded by the input length or a constant.
- All allocation goes through `USFM_Alc`.
- u32 sizes and indices. The input is capped at `USFM_INPUT_MAX`.
- Zero-copy: all text is a span into the input.
- Malformed USFM is never an error. It produces diagnostics.

## Architecture

```
input bytes --> scanner (internal, no allocation) --> parser loop --> USFM_Ast
                returns one token at a time           stack[64] of open nodes
```

One pass over the input, with no token array, in the style of yyjson.

### Marker table (codegen)

`codegen/codegen.c` holds a hand-written table with one row per marker
name: the name, `USFM_Marker` id, class, and any properties the parser
needs (for example "note part" or "takes an argument"). Codegen writes
the following into a `// USFM_CODEGEN` block in `usfm.h`:

- a perfect hash from marker name to table row;
- the table rows themselves.

The parser never reads the hand-written table directly. Both files carry
a `TODO`: the table should eventually be generated by parsing the official
`usfm.sty` files of every supported version, as the API header describes.
The generated output keeps the same shape when that happens.

**Keys.** The keys are base names (`q`, `tc`, `qt`, `s`) plus the few full
names whose meaning differs from their base, such as `k1`, `k2`, `p1` and
`p2`. The scanner parses a marker's level, its `-s`/`-e` suffix, the `+`
prefix and the `*` suffix separately, so those never become keys.

**Perfect hash.**

- **Hash input.** A `u32` packed from the first two and last two bytes of
  the name, combined with its length. The length is needed because short
  names share bytes. Bytes past the end of a short name are never read.
- **Slot.** `slot = ((uint32_t)(hash * seed) * (uint64_t)size) >> 32`: the product
  wraps modulo 2^32, then its high bits pick the slot. This is the classic
  multiply-shift hash (keeping the high half of the full 64-bit product
  instead would scale the hash linearly and never separate close keys). It
  needs no division, and the size can be any number, not just a power of
  two.
- **Search.** Codegen tries sizes from the key count upward. For each size
  it tries seeds up to a fixed bound, which is a codegen constant. It
  stops at the first `(size, seed)` that puts every key in its own slot. If
  no size up to a fixed maximum works, codegen fails with an error and
  writes nothing.
- **Hit check.** Each slot stores its name and length. A lookup hashes,
  then compares the name, because unknown markers like `\zfoo` also land
  on a slot.
- **Length bound.** Codegen emits `USFM_MARKER_NAME_MAX`, the length of the
  longest key. A name longer than that is unknown without hashing. The
  bound is derived from the table, so adding a longer marker raises it
  automatically.

The marker list is the one in `usfm_claude_api.h`. Class and properties
come from `docs/usfm-field-guide.md` §2 and §5.

### Scanner

The scanner is internal (`USFM_TokenState`, `USFM_Token_Next` in the API
header) and allocates nothing. It produces the API's token kinds: TEXT,
WHITESPACE, NEWLINE, MARKER, MARKER_END, ARGUMENT, ATTRIBUTES, OPTBREAK.

- **Markers.** `\` + optional `+` + name `[A-Za-z0-9-]+` + optional `*`. A
  bare `\*` closes a milestone. Lookup tries the full name first (so `\k1`
  is not `\k`). If that misses, it strips a trailing level number (`\q2` is
  `q`, level 2) and a `-s`/`-e` suffix (`\qt1-s` is `qt`, level 1,
  `MILESTONE_START`). A name that still isn't found is `USFM_M_UNKNOWN`.
- **Text.** The scanner skips to the next significant byte: `\`, `|`, `/`,
  `~`, CR or LF.
  - Escapes `\\ \/ \| \~ \"` stay inside TEXT and set `USFM_TF_HAS_ESCAPE`.
  - `~` sets `USFM_TF_HAS_NBSP`.
  - A CR, LF or TAB inside the text sets `USFM_TF_HAS_LOOSE_WS`. Runs of
    spaces are not detected: doing so would put the space byte on the hot
    path.
  - `//` is an OPTBREAK token.
- **Arguments.** The next word after `\c`, `\v`, `\id`, `\periph` or a note
  opener is an ARGUMENT. For notes, that word is the caller.
- **Attributes.** Inside a character span, a milestone, `\fig` or
  `\periph`, a `|` starts an ATTRIBUTES token that runs to the next `\`.
  Attributes are kept raw in v0.1.
- **Whitespace.** One whitespace byte after an opening marker is part of the
  marker. All other whitespace is kept, so every span is exact.
- **Trailing whitespace.** Before a paragraph-level marker, a `\v`, or the
  end of input, the parser trims trailing whitespace from the last TEXT
  node of the open container, and removes the node if nothing is left.
  Whitespace-only text at block level (directly under BOOK, SIDEBAR,
  PERIPHERAL or TABLE) is dropped.

### Parser loop

There is one loop over tokens, with a fixed stack of `USFM_STACK_DEPTH`
(64) open node indices. Each step does the same three things:

1. Classify the token.
2. Close what must close.
3. Open or append.

Nodes are appended in preorder to one growing array of `USFM_Node`.

- **Open:** append the node and push its index.
- **Close:** set `nodes[i].end = node_count` and pop.

The chapter and verse tables are filled in the same pass. Recording a `\c`
or `\v` node also adds its table entry and sets the previous entry's
`end_node`.

### AST shape

- `BOOK` is the root.
- Header markers (`\id`, `\usfm`, `\ide`, `\h`, `\toc#`, ...) are `HEADER`
  nodes.
- These are containers: `PARA`, `CHAR` (including note parts), `NOTE`,
  `TABLE` > `ROW` > `CELL`, `SIDEBAR`, `PERIPHERAL`, and `FIGURE`.
- `CHAPTER` and `VERSE` are leaf milestones, so a verse can cross
  paragraphs. Their ARGUMENT child holds the number, which is also parsed
  into the tables.
- `TEXT`, `OPTBREAK`, `MILESTONE`, `ARGUMENT` and `ATTRIBUTES` are leaves.
- A container's span covers the source bytes from its marker to the last byte
  consumed inside it, which may include whitespace trimmed from its last
  `TEXT` node.

### Closing rules

These follow `docs/usfm-field-guide.md` §3.

| Token | Closes | Then |
| --- | --- | --- |
| Paragraph-level marker (`\p`, `\q#`, `\s#`, `\c`, `\id`, `\tr`, `\esb`/`\esbe`, `\periph`) | `\c`, `\id` and `\periph` close everything down to BOOK (they start a new top-level division). Other paragraph-level markers close down to the nearest BOOK, SIDEBAR or PERIPHERAL. `\tr` stops at an open TABLE; any other marker also closes the TABLE. Unclosed notes get a diagnostic. | Opens PARA, ROW, etc. (`\tr` opens a TABLE first if none is open). `\c` appends a CHAPTER leaf. |
| `\v` | Open CHAR spans. A `\v` inside a note closes the note, with a diagnostic. | Appends a VERSE leaf. With no PARA open, it first opens an implied `\p` with a diagnostic. |
| Note opener (`\f \x \fe \ef \ex \efe`) | An open note, with a diagnostic (notes can't nest). | Opens NOTE, with the caller as its ARGUMENT. |
| Note part (`\fr \ft \fq \xo \xt` ...) | Every CHAR opened since the note's last note part. | Opens CHAR. |
| `\f*`, `\x*` ... | Everything inside the note, then the note. | — |
| `\X*` / `\+X*` | If `X` is open in the current paragraph or note: everything up to and including it. A stray note-part closer is ignored. Any other stray closer gets a diagnostic and is dropped. | — |
| Plain char marker inside an open char span | Nests if it has a `+`, or if the outer span's `\outer*` appears before the next paragraph-level marker (bounded lookahead). Otherwise it closes open character spans, innermost first, until one whose end marker appears before the next paragraph-level marker; it nests inside that one. Note parts are never closed this way. | Opens CHAR. |
| Cell (`\tc# \th# \tcr#` ...) | The previous cell. | Opens CELL. |
| Unknown marker | Followed by `\*`: a MILESTONE. At line start: a PARA. A matching `\name*` later in the same paragraph: a CHAR. Otherwise: an inline MILESTONE leaf with `USFM_M_UNKNOWN`. Its text is never dropped. | — |
| End of input | Everything. Unclosed notes get a diagnostic. | — |

Two rules are judgement calls that the corpus counts will check:

- `\v` closes open character spans. This is believed to match ParatextData.
- The lookahead runs only when a plain char marker opens inside another
  span, and it stops at the next paragraph-level marker.
- Each lookahead segment (up to that stop) is scanned once into a
  per-segment closer table (last end-marker offset per marker id, plus a
  small hash set for unknown names), so every question is O(1) and all
  lookahead is linear in the input.

### Versions

One algorithm handles every version. The version only selects which
diagnostics fire. For example, an unclosed character marker is accepted
silently in 2.x and reported in 3.1.

`USFM_VERSION_AUTO` reads `\usfm X.Y` and otherwise assumes 3.1.
`USFM_Ast_GetVersion` returns the resolved version.

## Errors, diagnostics and memory

- Errors follow the API's failure semantics:
  - NULL plus `err` means the tree could not be built (out of memory,
    input too large, or an invalid argument). Everything allocated is
    freed first.
  - Non-NULL with `USFM_OK` means the tree is complete.
  - Non-NULL with `USFM_ERR_NOMEM` means the tree is complete, but some
    diagnostics were dropped.
- Going past the stack depth is not an error. The overflowing marker is
  treated as text and gets a diagnostic.
- v0.1 implements these diagnostic codes, each with a fixed severity and
  message: `UNKNOWN_MARKER`, `UNCLOSED_MARKER`, `UNEXPECTED_CLOSE`,
  `INVALID_NESTING`, `INVALID_NUMBER`, `VERSE_BEFORE_CHAPTER`,
  `INVALID_BOOK_CODE`, `MISSING_CALLER`, `IMPLIED_PARAGRAPH`.
- These arrays grow geometrically (x2) through `USFM_Alc`, keeping the old
  block when a resize fails: nodes, chapters, verses, diagnostics. Initial
  sizes are estimated from the input length.
- The handle owns its arrays, a copy of `USFM_Alc`, and, with
  `USFM_PARSE_COPY_INPUT`, a copy of the input. `USFM_Ast_Free` releases
  everything in one call.

## Testing and build

- **Unit tests** live in `tests/`, with one dependency-free C runner. Each
  case parses a snippet and compares a tree dump (in the style of
  `test.ast`) against the expected text. There is at least one case for
  every closing-rule row and every diagnostic code.
- **Invariants** are checked by `check_ast()` in `tests/check.h`, after every
  parse in the unit tests, the corpus test and the fuzzer. The check is not
  compiled into the library, so callers' parses never pay for it. It verifies:
  - preorder ranges nest correctly;
  - every span lies inside the input;
  - chapter and verse entries point at CHAPTER and VERSE nodes.
- **The corpus test** parses the 83 WEB `.usfm` files and checks the totals
  in the Goal section.
  - The files are committed as a single archive, `tests/corpus/WEB.tar.gz`,
    together with the WEB copyright page (`copr.htm`). WEB is public domain;
    the name is a trademark of eBible.org.
  - `build.sh test` unpacks the archive into `build/corpus/WEB/`, which is
    gitignored, unless that directory is already up to date.
- **Fuzzing** uses libFuzzer and needs clang.
  - `tests/fuzz_ast.c` builds an AST from the fuzz input with
    `USFM_Ast_ParseOpts` and runs the invariant checks on every result.
  - The first input bytes choose the `USFM_Config` version and the parse
    flags. They also choose an allocation count after which a test
    allocator fails, so that every `USFM_ERR_NOMEM` path gets exercised.
  - Every result is either a NULL handle with a matching `err`, or a handle
    whose invariants hold. Any crash, sanitizer report, leak, or broken
    invariant is a bug.
  - The seed corpus includes the 2.x and 3.1 nesting cases from the unit
    tests, so the fuzzer mutates real closing-rule inputs. The fuzzer
    checks invariants, not tree shapes; the unit tests check shapes.
  - The seed corpus is the unit-test snippets plus a few WEB chapters.
    `tests/fuzz/usfm.dict` holds every marker name from the codegen table,
    plus `\*`, `|`, `//`, `~` and the escape sequences.
  - The harness also builds without libFuzzer, using gcc and a small
    `main()`, so it can replay crash files and run the seed corpus as an
    ordinary regression test inside `build.sh test`.
  - `build.sh fuzz` builds with
    `-fsanitize=fuzzer,address,undefined`.
  - v0.1 requires a 30-minute fuzz run with no findings.
- **The benchmark** is `main.c`, rewritten as a timing harness that
  reports cycles per byte on JER.
- **`build.sh`** gains `test` and `fuzz` modes. Debug builds keep ASan and UBSan.
  `build.sh` reruns codegen whenever `codegen/codegen.c` or
  `codegen/markers.h` is newer than `build/codegen.stamp`.


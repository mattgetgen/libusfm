# libusfm roadmap

Updated 2026-11-18. Each phase depends on the ones before it. Version
numbers follow the project's rule: a release that adds or changes API is
a minor version, and one that only changes speed or internals is a patch.

## Where libusfm stands

- **0.1.0 (PR #1):** the parser.
  - One pass, with no recursion and a 64-deep explicit stack.
  - A flat preorder node array whose text is spans into the input.
  - Chapter and verse tables with lookups and iterators.
  - Diagnostics, never errors, for malformed USFM.
  - UTF-8 validation (`INVALID_UTF8`, rejection of UTF-16 and UTF-32
    byte-order marks).
  - The `usfm` command-line tool, with USFM, tree and text views.
- **0.1.1 (PR #2):** SIMD scanning and SIMD UTF-8 validation (SSE2,
  SSSE3, AVX2, AVX-512BW, NEON, wasm simd128), chosen at compile time.
- **Speed:** in the 2026-11-18 measurements, AVX2 is 1.0-1.65x faster
  than usfm3 0.3.2, the fastest other parser.
  - Builds without SIMD lose to it on text without word tags, because
    usfm3's SIMD (through `memchr`) is chosen at runtime.
  - See `docs/release-notes/0.1.1.md`.

## What other parsers offer

From a survey of usfm3 0.2.1 and 0.3.2, usfm_tools, ParatextData, usfmtc,
usfm-grammar, tree-sitter-usfm3 and the `usfm` crate:

| Feature | Who has it |
| --- | --- |
| USX output | usfm3, usfm_tools, ParatextData, usfmtc, usfm-grammar |
| USJ output | usfm3, usfmtc, usfm-grammar |
| USFM output | most; **always normalized, never byte-exact** |
| HTML output | usfm_tools |
| Verse text / vref export | usfm3 0.3 (`vref_dict`, cleaned text), usfm-grammar (filters), usfmtc (`gettext`) |
| USX or USJ input | usfmtc, usfm-grammar; usfm_tools and ParatextData (USX) |
| Typed diagnostics | usfm_tools (about 70 codes, configurable levels, autofix), usfm3 |
| `.sty` stylesheets | usfm_tools, ParatextData |
| `markers.ext` | usfmtc |
| Versification (`.vrs`) | ParatextData, usfmtc |
| Reference parsing | usfmtc, ParatextData, usfm_tools |
| Editing API | partial: usfm_tools (visitors), usfmtc (trees), ParatextData |
| Incremental parsing | tree-sitter (syntax tree only) |
| Event (SAX) API | ParatextData (slow) |
| Paratext project folders | usfm_tools, ParatextData |
| Bindings | usfm3 (WASM, Python), usfm-grammar (JS, Python), tree-sitter |

## Where libusfm can go further

1. **A stable C ABI** that every language binding can share.
2. **A byte-exact round trip together with an editing API.**
3. **`.sty`, `markers.ext` and `.vrs` in one fast tool,** with nesting
   and missing/extra-verse checks inside the parser.
4. **Fast verse-text extraction.**
5. **One fast parser for every output:** USJ, USX, HTML and text.
6. **An event API at native speed, without allocation.**
7. **Incremental re-parse that keeps the meaning,** not just the syntax.
8. **Diagnostics that come with fixes.**

## Phases

### Phase 0: finish 0.1.x (in progress)
- Merge PR #1 and PR #2.
- Structural checks against usfmtc (the reference) and ParatextData
  (always compared, differences documented). The design is in
  `docs/superpowers/specs/2026-11-17-reference-parsers-design.md`; it is
  paused. Every writer below relies on it.

### Phase 1: fastest out of the box (0.1.x, no API change)
1. **A merged parser loop.**
   - **What changes:** the parser reads the input directly, with no token
     struct, no push-back, and one dispatch on the marker's role. A fast
     path handles character span, text, attributes and closer.
   - **Safety:** the current scanner stays as the reference, and a
     differential test checks that the tree is byte-identical.
   - **Expected gain:** 20-40% on word-tagged text. It starts as a
     prototype measured before the full change.
2. **Choosing the SIMD path at runtime.**
   - **What changes:** one build contains several instruction-set versions
     and detects the CPU, as usfm3 gets through `memchr`. A default
     `cc -O2` build then gets AVX2 speed.
   - **Prerequisite:** this is also required before shipping prebuilt
     binaries for bindings.

### Phase 2: read and write (0.2)
- **A byte-exact USFM writer.** The tree keeps the whitespace it drops
  today, and an unedited document writes back unchanged.
- **A reference API.** It parses `GEN 1:3-2:4` and looks ranges up and
  iterates over them; the code moves from the tool into the library.
- **Verse-text extraction.** A public, fast version of the tool's text
  view (notes, headings and attributes optional), plus vref/BibleNLP
  export.

### Phase 3: interchange (0.3)
- **USJ and USX writers,** with no dependencies, starting from the
  Phase 0 canonical form.
- **HTML output.**
- **USX or USJ input:** optional. It would need a JSON or XML reader.
  Only on request.
- **A first, small public announcement here.** USJ makes libusfm directly
  usable in Platform.Bible.

### Phase 4: stylesheets and validation (0.4)
- **Loading `.sty` and `markers.ext` at runtime,** through
  `USFM_Config.custom`. This replaces `TODO(sty)`.
- **Placement checks from `\OccursUnder`,** and configurable diagnostic
  levels.
- **Versification (`.vrs`):** missing, extra and out-of-order verse checks.

### Phase 5: editing (0.5)
- **A mutable document,** in yyjson's style. Edits write back byte-exact
  around the changed parts.
- **Fixes for diagnostics.**

### Phase 6: editor-grade (0.6)
- **An event API without allocation.**
- **Incremental re-parse** of a changed paragraph or chapter.
- **Token export** for syntax highlighting and language servers.

### Phase 7: reach (0.7 and later)
- **Bindings:** TS/WASM first (Platform.Bible), then Python, C#, and
  native Node.
- **Paratext project folders** (Settings.xml, book naming), probably in
  the bindings layer.

## Throughout

- **Every feature gets** fuzzing, corpus checks on every SIMD path, and,
  where it applies, differential checks against usfmtc and ParatextData.
- **Re-measure against other parsers when they release,** in one session
  with one method: same pinning, passes and warm-ups, power profile on
  "performance".
- **Performance work outside the plan above:**
  - the SIMD structural index (simdjson stage 1), which would follow the
    merged loop;
  - arena allocators.

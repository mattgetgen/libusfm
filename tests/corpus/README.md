# Test corpora

Each `NAME.tar.gz` holds one directory, `NAME/`, with the `.usfm` files
and the publisher's `copr.htm`. `./build.sh test` unpacks every archive
into `build/corpus/` and runs `tests/corpus_test.c` on each corpus for
every SIMD path; the AST checksum of a corpus must be identical on all
paths, and its totals must match its line in `expected.txt`:

    NAME books chapters verses paragraphs chars notes milestones

(counted as in `count_ast` in `tests/corpus_test.c`, which follows
ParatextData's CountingSink; "books" is the number of files).

Every line is externally verified. On 2026-11-15, three independent
parsers counted every corpus exactly as `expected.txt` says: books,
chapters, verses, paragraphs, character spans, notes and milestones. The
three are ParatextData 9.5.0.24 (C#), usfm3 0.2.1 (Rust, AST mode) and
usfmtc 0.4.8 (Python, the USFM committee's parser; 0.4.10 gives the
same counts, checked on 2026-11-17), each run with its own
harness from the parser benchmark repository. A fourth, usfm_tools 0.1.1
(Rust), gave the same counts on 2026-11-16.
`paratextdata-web.csv` has ParatextData's per-book counts for
WEB-STRONGS (compare with `build/corpus/libusfm-WEB-STRONGS.csv`). Every
corpus also parses in libusfm with 0 diagnostics, 0 scanner differences
and 0 classifier differences on every SIMD path.

Two other parsers disagree slightly, which is a known difference in them,
not in these corpora. usfm-grammar 3.2.0 counts a few fewer character
spans and notes on every corpus that has them, and counts GRC-BYZ as
31 books with 5,516 paragraphs. The `usfm` 0.2.10 Rust crate counts one
more paragraph in WEB-STRONGS and WEB (34,754).

`derive.py` removes word-level markup (`\w TEXT|ATTRS\w*` and
`\+w TEXT|ATTRS\+w*` become `TEXT`; nothing else changes) and copies
`copr.htm`:

    python3 -I tests/corpus/derive.py IN_DIR OUT_DIR

The archives were made with
`tar --sort=name --owner=0 --group=0 --numeric-owner --mtime='2026-11-15 00:00Z' -czf NAME.tar.gz NAME`.

## WEB-STRONGS

- Source: World English Bible, eBible.org USFM download
  (https://ebible.org/Scriptures/eng-web_usfm.zip; files `*eng-web.usfm`),
  unchanged. 83 files: Protestant canon, deuterocanon, FRT and GLO.
- Snapshot of the eBible release built from source files dated
  13 Nov 2026 (see `copr.htm`). eBible revises the WEB from time to time
  (the previous snapshot, dated 23 May 2026, differed in refined Strong's
  numbers and 4 more tagged words). When updating it, re-verify the
  counts: on 2026-11-15 ParatextData 9.5 and usfm3 0.2.1 (the harnesses in
  the usfm parser benchmark repository) both counted this snapshot as
  83 books, 1,402 chapters, 38,058 verses, 34,753 paragraphs,
  685,201 character spans and 2,218 notes, matching libusfm.
  `paratextdata-web.csv` holds ParatextData's per-book counts for it.
- License: public domain. "World English Bible" is a trademark of
  eBible.org (see `copr.htm`).
- Exercises: a Strong's tag on every word (`\w word|strong="H1234"\w*`,
  `\+w` inside `\wj` and a few footnotes), so tokens are tiny and attributes
  are everywhere; footnotes, poetry, `\wj`.
- Until the corpus was split into these archives this was the only
  corpus (then named WEB); the 0.1.1 performance numbers were measured
  on it.

## WEB

- Source: WEB-STRONGS, through `derive.py` (639,570 `\w` and 38,118 `\+w`
  wrappers removed).
- License: as WEB-STRONGS. Only markup was removed; the words are
  unchanged.
- Exercises: the same text and structure untagged, so long text runs
  between markers; footnotes, `\wj`, `\+wh`, `\k`, poetry.

## HBO-WLC

- Source: Hebrew Old Testament, Westminster Leningrad Codex,
  https://ebible.org/Scriptures/hboWLC_usfm.zip, through `derive.py`
  (299,878 `\w` wrappers removed). 39 books.
- License: the WLC text is public domain.
- NOTE: the eBible.org download also carries lemma and morphology data
  (`strong="…"` and `x-morph="…"` on every `\w`) from the Open Scriptures
  Hebrew Bible project, licensed CC BY-SA 3.0. That data was removed;
  only the public-domain text remains. The bundled `copr.htm` still
  mentions the CC BY-SA license because it describes the original
  download.
- Exercises: right-to-left Hebrew with dense combining marks (vowel
  points and cantillation accents, several per letter), maqaf and sof
  pasuq; no character markup and no notes.

## CMN-CUV

- Source: Chinese Union Version (simplified),
  https://ebible.org/Scriptures/cmn-cu89s_usfm.zip, unchanged. 66 books.
- License: public domain.
- Exercises: 3-byte UTF-8 throughout, no spaces between words, many
  short `\pn` (proper name) spans, `\add`, footnotes, poetry (`\q1`),
  ideographic spaces.

## ARB-VD

- Source: Arabic Van Dyck,
  https://ebible.org/Scriptures/arb-vd_usfm.zip, unchanged. 66 books.
- License: public domain.
- Exercises: right-to-left Arabic script (2-byte UTF-8, frequent
  combining vowel marks), long verses with no character markup and no notes.

## GRC-BYZ

- Source: 1904 Patriarchal Greek New Testament (Byzantine),
  https://ebible.org/Scriptures/grcbyz_usfm.zip, unchanged. 32 files:
  27 NT books plus FRT, INT and three XX books.
- License: public domain (its `copr.htm` gives no separate license for
  the Strong's tags, so they are kept).
- Exercises: polytonic Greek (2- and 3-byte UTF-8), a Strong's tag on
  every word, `\+w` nested inside `\bdit` quotations, introductions
  (`\im`, `\ib`, `\ip`), a few footnotes.

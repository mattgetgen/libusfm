# `usfm`: a reference command-line tool

Date: 2026-11-16. Branch: feat/ast-parser (PR #1).

## Goal

Replace `main.c`, a single-file x86 benchmark that nothing else uses,
with a small jq-like command-line tool. It reads USFM from stdin, a file
or a directory, and prints it in one of three views: colored USFM, the
AST as a tree, or plain text. It can select a chapter, a verse or a
range. It also has a benchmark mode. It is portable C99 for POSIX and
Windows, and it uses only the public `usfm.h` API, so it doubles as an
example program.

Non-goals: searching or filtering beyond a reference, editing, JSON
output, and a library-level USFM writer (the USFM view lives in the
tool for now).

## Usage

```
usfm [options] [PATH...] [REF]
```

- `PATH` is a file, or a directory. For a directory, the tool reads every
  file in it ending in `.usfm` or `.sfm` (case-insensitive), in byte
  order of name. Subdirectories are not read. With no `PATH`, or `-`, it
  reads stdin.
- `REF` selects content:

  | Form | Selects |
  | --- | --- |
  | `C` | chapter C |
  | `C:V` | verse V of chapter C |
  | `C:V-V2` | verses V to V2 of chapter C |
  | `C:V-C2:V2` | from C:V to C2:V2 |
  | `BOOK REF` (one argument, such as `"GEN 1:3"`) | REF in that book only; other inputs are skipped |

  `BOOK` is three ASCII letters or digits, compared case-insensitively
  with `USFM_Ast_GetBookCode`. An argument that matches this grammar is
  the `REF`; anything else is a `PATH` (write `./1:3` for a file with
  such a name). At most one `REF` is allowed.
- Options:

  | Option | Meaning |
  | --- | --- |
  | `-u`, `--usfm` | colored USFM (default) |
  | `-t`, `--tree` | AST tree |
  | `-x`, `--text` | plain text |
  | `-C` | color on |
  | `-M` | color off |
  | `-d`, `--diagnostics` | print each input's diagnostics to stderr |
  | `-b N`, `--bench N` | benchmark: parse each input N times |
  | `-h`, `--help` | usage, then exit 0 |

  The last of `-u`/`-t`/`-x` wins, and the last of `-C`/`-M` wins.
- **Color:** without `-C` or `-M`, color is on only when stdout is a
  terminal and `NO_COLOR` is unset or empty. A file or a pipe gets no
  color.
- **Several inputs:** before each input's output, a line `==> PATH <==`
  goes to stderr, so stdout carries only content.
- **Diagnostics (`-d`):** one line per diagnostic on stderr:
  `PATH:LINE:COL: SEVERITY: MESSAGE`. LINE and COL are 1-based, and COL
  counts bytes. Stdin is named `-`. A final line reports any dropped
  diagnostics.
- **Exit status:**
  - 0 on success.
  - 1 if a `REF` was given and matched nothing in any input.
  - 2 for a usage error, an unreadable path, an input over 4 GiB, or a
    parse that returned NULL.

  Diagnostics never change the exit status.

## Views

Every marker name is read from the input: a node's span starts at its
marker, so the name is the bytes after `\` (after `\+` for a nested
span), up to the first space, `*`, `\` or end of span. A PARA with
`USFM_NF_IMPLIED` has no marker in the source.

### USFM (`-u`)

- **Walk:** the tool walks the nodes in preorder with an explicit stack,
  with no recursion.
- **Line breaks:** a newline is written before every HEADER, CHAPTER,
  VERSE, PARA, TABLE row, SIDEBAR and PERIPHERAL node, except at the very
  start of output. `\v` on its own line is the usual USFM layout; the
  parser drops that newline when it reads the text back, so the tree
  does not change. BOOK and TABLE nodes have no marker and write
  nothing. A PARA flagged `IMPLIED` writes no marker, but still starts a
  line, and its children are written.
- **Markers:** each marker is written as `\name`. If the node has an
  ARGUMENT child, a space and the argument's bytes follow. A space
  separates the marker from following text or markers. A cell marker
  (`\tc2`) also gets a space before it, because the parser trims the
  space at the end of cell text.
- **ATTRIBUTES:** the node's raw bytes, which include the leading `|`.
- **Closing:** an end marker is written only where the source had one,
  copied as written: `\w*`, `\+w*`, `\*` for a milestone, `\esbe` for a
  sidebar. A span the source closed implicitly (such as `\fr` and `\ft`
  inside a footnote) stays implicit, so the output parses back to the
  same tree.
- **Stability:**
  - On the test fixture the view prints the input byte for byte.
  - On every corpus file, printing the view's own output again changes
    nothing.
- **TEXT:** the node's bytes are copied, except that each run of CR, LF
  and TAB becomes one space. OPTBREAK is written as `//`.
- **Colors (ANSI SGR):**

  | Element | Color |
  | --- | --- |
  | Paragraph-level markers (BOOK, HEADER, PARA, rows, cells, SIDEBAR, PERIPHERAL) | bold blue |
  | `\c`, `\v` and their numbers | bold yellow |
  | CHAR and MILESTONE markers | blue |
  | NOTE markers and everything inside a note | magenta |
  | Attributes | green |
  | Text | default |

### Tree (`-t`)

- **One line per node,** indented two spaces per depth: the kind name
  (`BOOK`, `PARA`, `TEXT`…), then `\name` for marker nodes, then the
  level if nonzero, then the flags as words, then `@offset+length`.
- **Flag names:**
  - Marker nodes: `nested`, `milestone-start`, `milestone-end`,
    `self-closed`.
  - PARA nodes: `implied`, `heading`.
  - TEXT nodes: `escape`, `nbsp`, `loose-ws`.
- **Contents:** TEXT, ARGUMENT and ATTRIBUTES nodes add their bytes as a
  quoted string, with `"` and `\` escaped, `\n`, `\r` and `\t` shown as
  escapes, and other bytes below 0x20 shown as `\xHH`.
- **Colors:**

  | Element | Color |
  | --- | --- |
  | Kind | bold |
  | Marker | blue |
  | Strings | green |
  | Span | dim |

### Text (`-x`)

- **Book:** BOOK and HEADER nodes are not printed.
- **Chapters:** a CHAPTER prints its number on its own line, in bold
  yellow.
- **Paragraphs:** a PARA starts a new line. A heading PARA (`heading`
  flag) is printed in bold on its own line.
- **Verses:** a VERSE prints its number and a space, in yellow, inline,
  with a space before it unless it starts the line.
- **Tables:** a row starts a new line, and cells are separated by a TAB.
- **Figures:** the caption is printed.
- **Removed:** NOTE subtrees, ATTRIBUTES, OPTBREAK and ARGUMENT nodes
  (other than chapter and verse numbers).
- **Escapes are cooked:**
  - `\\` `\/` `\|` `\~` `\"` become the escaped byte.
  - `~` becomes U+00A0.
  - Runs of CR, LF and TAB become one space.

## References

- **Lookup:** the tool resolves a REF with `USFM_Ast_FindChapter` and
  `USFM_Ast_FindVerse`. A verse range covers the verse table entries
  whose ranges overlap the requested range: a bridge `\v 3-4` matches
  `1:4`.
- **Selected nodes:** from the first matched entry's VERSE node up to the
  `end_node` of the last. A chapter covers its CHAPTER node up to its
  `end_node`.
- **Trimming:** trailing nodes with none of their content selected are
  dropped. A verse ends at the next `\v`, which lies inside the next
  paragraph, so without this every range would end with that
  paragraph's opening `\p`.
- **USFM and text views:** if the first selected node is not a direct
  child of the root, the views first open each enclosing PARA (USFM
  writes its marker, text starts its line) and close each enclosing
  CHAR or NOTE at the end. Every CHAPTER node inside the selection is
  printed.
- **Tree view:** prints the selected nodes indented by their true depth,
  preceded by their ancestors, each marked `(context)`.

## Benchmark (`-b N`)

- **What runs:** each input is read into memory once, then parsed N
  times, N ≥ 1.
- **What's timed:** each parse alone, with nothing printed.
- **Freeing:** the AST is freed after each pass, outside the timed
  region. Keeping every tree would hold N whole trees in memory (about
  6 GB for 100 passes over WEB-STRONGS). This is the only place the tool
  frees memory.
- **Per input:** a line `PATH  BYTES bytes  BEST ns  MiB/s` with the
  fastest pass.
- **After all inputs:** a total line, summing the best times and the
  bytes.
- **Ignored:** a REF and the view options.
- **Timer:** `clock_gettime(CLOCK_MONOTONIC)` on POSIX and
  `QueryPerformanceCounter` on Windows. There is no `rdtsc`, so the tool
  builds on any architecture.

## Portability

- **Where platform code lives:** in one block at the top of `main.c`,
  selected by `_WIN32`.

  | Need | POSIX | Windows |
  | --- | --- | --- |
  | Is stdout a terminal? | `isatty` | `_isatty` |
  | Directory listing | `opendir`/`readdir`, then `qsort` | `FindFirstFileA`/`FindNextFileA`, then `qsort` |
  | Binary stdin | (nothing needed) | `_setmode(_fileno(stdin), _O_BINARY)` |
  | Color | ANSI escape codes | ANSI escape codes, after `SetConsoleMode(... ENABLE_VIRTUAL_TERMINAL_PROCESSING)`; if that fails, color is off |
  | Timer | `clock_gettime` | `QueryPerformanceCounter` |

- **Input:** a file is read with `fopen`/`fread`; stdin is read in
  chunks into a growing buffer.
- **Memory:** the tool never frees memory, except for the benchmark's
  ASTs; the process exit releases it. Debug builds use AddressSanitizer,
  so `main.c` turns off its leak report (`__asan_default_options`).
- **Output:** written with `fwrite` through stdio's buffer.

## Build and files

- `main.c` is replaced by the tool. `./build.sh debug` (gcc, ASan +
  UBSan) and `./build.sh release` (clang -O3) still build `./usfm`.
- The spec's earlier description of `main.c` as the benchmark harness
  (`2026-11-14-ast-parser-design.md`) gets a note pointing here.

## Tests

`./build.sh test` builds the tool in the debug configuration and runs
`tests/cli_test.sh`:

- **Fixture:** `tests/cli/sample.usfm`, a small book covering:
  - a header and two chapters;
  - a verse bridge and a verse range split across paragraphs;
  - a heading, poetry, nested `\+w` with attributes;
  - a footnote, a milestone, an escape and `~`.
- **Golden files:** `tests/cli/expected/*.txt` hold the expected output
  for:
  - `-M -u`, `-M -t` and `-M -x` on the whole file;
  - `-M -u` and `-M -x` for `1`, `1:3`, `1:3-5`, `1:3-2:2`, and a
    bridge verse.

  `diff` must find no difference. File names use `.` instead of `:`
  (`usfm-1.3-5.txt`), because Windows can't check out names containing
  `:`.
- **Exit status:**
  - A reference that matches nothing exits 1 with empty stdout.
  - A missing path exits 2.
- **Input sources:**
  - stdin input gives output identical to the file path.
  - A directory holding the fixture twice gives it twice.
- **Color:**
  - `-C` output contains `ESC[`.
  - Default output to a file contains no `ESC`.
  - `NO_COLOR=1 … -C` still has color (`-C` overrides it).
- **Every corpus:** each WEB file in all three views goes to
  `/dev/null` under ASan/UBSan, and must exit 0.
- **Benchmark:** `-b 2` on the fixture exits 0 and prints a MiB/s line.
- **Windows:** `zig cc -target x86_64-windows-gnu` compiles `main.c`
  when zig is installed. This is compile-only; nothing runs it.

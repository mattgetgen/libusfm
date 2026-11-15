# \\usfm parser field guide

USFM 2.4 · 2.5 · 3.0 · 3.1.1 (+ 3.1.2/3.2 drafts)

Present in

## How to read this guide

USFM has no single formal grammar that covers every version. The 3.1 committee publishes a RelaxNG schema for USX (`grammar/usx.rnc`) annotated with `usfm:` rules that describe the backslash syntax. That schema is the closest thing to a normative USFM grammar, and the regexes on this page are quoted from it. Earlier versions are defined by prose manuals plus the Paratext stylesheets (`usfm.sty`), which carry each marker's type, end marker and allowed parents.

For a parser that accepts everything, a workable approach is **one tokenizer, one tree builder, with version-dependent closure rules**. Lexing barely changed from 1.0 to 3.1. What changed is how character spans close, where attributes may appear, and what escaping means.

**Versions covered.** 3.1.1 is the latest released spec (docs.usfm.bible marks it "latest"). 3.1.2 and 3.2 are published as pre-release docs and are labelled draft throughout. 3.0.x comes from ubsicap/usfm, 2.5 from the `usfm2.5` branch stylesheet, and 2.4 from the 2013 User Reference PDF.

## 1. Lexical structure

### Marker tokens

Every marker begins with a backslash. A tokenizer that handles all versions can use:

```
MARKER     = "\" ["+"] NAME ["*"]          ; \p  \v  \nd*  \+nd  \+nd*  \qt1-s  \tc1-2
MS_CLOSE   = "\*"                            ; milestone self-close (3.0+)
NAME       = [A-Za-z0-9-]+                   ; lowercase in practice; xtSee/xtSeeAlso and z-markers can have capitals
TAGEND     = /(?:[ \t\r\n]+|(?=[\\|]|$))/    ; 3.1 grammar: a marker ends at whitespace, "\", "|" or EOF
```

- **One whitespace character after an opening marker is structural**, not content. The 3.1 grammar consumes `ws+`, and 2.x/3.0 say "markers end with the next space". After a closing marker (`\nd*`) whitespace *is* content.
- The name includes hyphens. `\qt-s`, `\qt1-e`, `\ts-s` are milestones, and `\tc1-2` is a column-spanning cell (3.0+). Don't stop the name at `-`.
- Numbered markers: a trailing number is a level (`\q2`), a part (`\mt1`) or, for cells, a column. The bare form equals level 1: `\q` ≡ `\q1`. Keep the exact name and also record base + level. Don't strip digits blindly, because `\k1` is a paragraph while `\k` is a character marker, and `\p1` isn't a level of `\p`.
- `\+name` / `\+name*` is the nested form of a character marker (2.4 onward, deprecated in 3.1).
- Markers starting with `z` are user extensions (all versions). Keep them; don't reject them.
- Since 3.2 (draft), `\uXXXX` and `\UXXXXXXXX` are Unicode escapes, not markers. A pre-3.2 file could in theory contain a z-free marker named `u…`, but none is defined.

### Text tokens

```
TEXT  = /([^\\/]|\/(?!\/)|\\[\/~\\|]|\\u[0-9a-fA-F]{4}|\\U[0-9a-fA-F]{8})+?/   ; 3.1/3.2 grammar
~     → U+00A0 no-break space (since 2.2; before that "!$")
//    → optional line break <optbreak/>
```

### Escapes (3.1+)

| Context | Escapable | Notes |
| --- | --- | --- |
| Text | `\\` `\/` `\~` `\\|` | Release notes: "`\` escaping is required and supported for `\` `'` `"` `\|` `~` `//`". Before 3.1 there was no escape mechanism, so a literal backslash in old text is almost always an error or a broken marker. |
| Attribute value (quoted) | `\"` `\\` `\=` `\~` `\/` `\\|` | Grammar `ATTRIBTEXT = /(?:\\["\\=~/\|]\|[^\\"])*/` |
| Default attribute (unquoted) | `\\` `\=` `\\|` `\~` `\/` | `ATTRIBALL = /(?:[^\\=\|]\|\\[\\=\|~/])+(?=\\)/`. It runs to the next backslash, so it can't contain `=` unescaped. |

**Version trap.** In 2.x/3.0 text, `~` is always a no-break space and `//` is always a break. URLs like `http://` inside text become breaks unless you special-case them. Under 3.1, writers should have escaped them as `\/\/`.

## 2. Marker classes and what ends them

A parser needs to know the **class** of every marker, because the class decides what ends it. The stylesheet's `\StyleType` (Paragraph / Character / Note, plus Milestone in newer tooling) and `\Endmarker` give the answer for known markers. The 3.1 `markers.ext` gives a finer category.

| Class | Examples | Opens with | Ends at |
| --- | --- | --- | --- |
| Book identification | [\\id](#) | `\id CODE` | End of line |
| Paragraph | \\p \\q# \\s# \\mt# \\li# \\ip \\rem | Marker + space (or newline) | Next paragraph-level marker: any paragraph, \\c, \\tr, \\esb/\\esbe, \\periph, \\id. **\\v does not end a paragraph.** |
| Empty paragraph | [\\b](#) [\\pb](#) [\\sd#](#) | Marker alone | Immediately |
| Chapter | [\\c](#) | `\c N` | Next \\c / EOF. Closes the current paragraph. |
| Verse | [\\v](#) | `\v N␠` | Next \\v or \\c. A verse is a milestone, not a container. |
| Character | \\nd \\w \\add \\bk \\jmp | Marker + space | `\name*`. Before 3.1 also implicitly (see §3). |
| Note | [\\f](#) \\fe \\x \\ef \\efe \\ex | `\f + ` (caller + space) | `\f*`. Always explicit. |
| Note structural char | \\fr \\fq \\fqa \\fk \\fl \\fw \\fp \\ft \\fdc · \\xo \\xk \\xq \\xt \\xta \\xop \\xot \\xnt \\xdc | Marker + space | Next structural char, the note end, or an explicit \\x\* |
| Table row / cell | [\\tr](#), [\\tc#](#) \\th# … | Row: paragraph. Cell: `\tc1 ` | Cell ends at the next cell or the next paragraph-level marker |
| Milestone | [\\qt-s](#) \\ts-s | `\qt-s` \[`\|attrs`\] | `\*` straight after the name or attributes |
| Sidebar | [\\esb … \\esbe](#) | `\esb` | `\esbe` (no asterisk). Contains whole paragraphs. |
| Peripheral division | [\\periph](#) | `\periph Title\|id="x"` | Next \\periph / EOF |
| Figure | [\\fig](#) | `\fig ` | `\fig*` |
| Attribute-like | \\ca \\va \\vp \\cp \\cat \\usfm | As marked | Their content becomes an attribute of a neighbour in USX/USJ (markers.ext category `attribute`) |

**Unknown markers.** If a marker isn't in your table (a z-marker, a typo, a future marker), infer its class from context. A matching `\name*` later in the same paragraph suggests a character marker. A trailing `\*` means a milestone. Otherwise, at the start of a line, treat it as a paragraph marker. Mid-line with no end marker, keep it as an unknown inline marker. Never drop its text.

## 3. Character closure and nesting, by version

This is the biggest semantic difference between versions, and it decides how you build the tree.

| Version | Rule |
| --- | --- |
| 1.x – 2.3 | No nesting. Paratext treats a new character marker as an **implicit close** of the open one. End markers are optional in practice. A paragraph marker or note end closes everything. |
| 2.4 – 3.0.x | Nesting uses a `+` prefix: `\add an \+nd Lord\+nd* addition\add*`. A plain (non-`+`) character marker **closes all open character spans**, nested ones included. The 2.4 manual gives both of these as valid: `\add … \+nd Lord\+nd*\add*` and `\add … \+nd Lord\add*` (closing the outer span also closes the inner one). |
| 3.1 | Character markers **must be closed explicitly**, except note structural markers. Since closure is explicit, a plain `\nd` inside `\add` nests: `\add a \nd Lord\nd* b\add*`. The `\+` prefix is still accepted but deprecated, and may be removed later. |

**The ambiguous case.** Take `\add a \nd b\nd* c\add*`. Under 3.1 it is nested. Under 2.x/3.0 the `\nd` implicitly closes `\add`, which leaves `c` plain and `\add*` stray. Choose the mode from `\usfm` when present, and otherwise use this heuristic: *when a plain char marker opens inside another char span, nest it if the outer span's end marker appears later in the same paragraph or note before any paragraph marker. Otherwise close the outer span implicitly.* On real 2.x/3.0 data this matches what authors meant, and it gives the right answer for valid 3.1.

### A closure algorithm that works for every version

```
on opening char marker M (plus = had "+" prefix):
    if plus or mode == 3.1 or lookahead_finds("\\" + M.outer + "*"):
        push M onto char stack (nested)
    else:
        close all open char spans in the current container; push M
on "\X*" (or "\+X*"):
    if X is on the char stack: pop up to and including X   (implicitly closes inner spans)
    elif X is a note-structural marker: ignore (explicit close of an already-closed span)
    else: report a stray end marker and drop it
on note structural marker S inside a note: close every char span opened since the note's last structural marker; open S
on note end "\f*": close all spans inside the note, then close the note
on any paragraph-level marker: close all char spans and any unclosed note (report it), close the paragraph
```

USX 3.x marks spans the USFM writer left implicitly closed with `closed="false"` (e.g. `<char style="fk" closed="false">`), so round-tripping can reproduce the original markup.

## 4. Notes

```
\f + \fr 1.14 \fq religious festivals; \ft or \fqa seasons.\f*         (implicit structural closure, recommended)
\f + \fr 1.14 \fr*\fq religious festivals; \fq*\ft or \ft*\fqa seasons.\fqa*\f*   (explicit, also valid)
```

- **Caller**: `/[^\\\s]+/` then horizontal whitespace. `+` means auto-generate, `-` means no caller, anything else is a literal caller.
- An optional [\\cat …\\cat\*](#) may follow the caller (study Bibles). It becomes `category="…"` on `<note>`.
- **3.1**: all note text must be inside a structural char. In 2.x/3.0 files, `\f + text\f*` and `\x - Mt 1.1\x*` (no `\xt`) are common, so wrap the loose text in an implicit `\ft` / `\xt`.
- [\\fv](#) was structural before 3.1 and needs `\fv*` from 3.1. `\fm` is an ordinary character marker that needs closing. `\fdc`, `\xdc`, `\xot` and `\xnt` are structural in the 3.1 grammar (markers.ext categories footnotechar/crossreferencechar), but the 2.x manuals write them as pairs and every stylesheet gives them an end marker. Accept both forms. When `\xot …\xot*` wraps references inside `\xt`, treat it as nested.
- [\\fp](#) starts a new paragraph *inside* a footnote. It's still a structural char, not a paragraph marker.
- Notes can't nest. A `\f` inside a `\f` means the first one is unclosed.
- Notes may appear after `\c N` (before the first paragraph): the 3.1 grammar allows `Chapter = ChapterStart (Footnote | CrossReference)*`.

## 5. Attributes (3.0+)

```
\w gracious|lemma="grace" strong="H2587,G5485" x-myattr="1"\w*     named
\w gracious|grace\w*                                                default attribute (unquoted)
\qt-s |sid="qt_123" who="Pilate"\*                                  on a milestone (note the space before |)
\ip |aid="author"| The author is unknown                            3.2 draft: leading attributes on paragraphs
```

- **Delimiter**: `PIPE = /[ \t]*(?<!\\)\|[ \t]*/`, an unescaped `|` with optional horizontal space either side. Attributes run to the end marker / `\*`.
- **Pairs**: `name="value"` separated by whitespace. `ATTRIBNAME = [a-zA-Z_][a-zA-Z0-9\-_]*`. Values are double-quoted. Several values go comma-separated in one string, with whitespace around commas ignored. Parts of a compound value are colon-separated (e.g. `rb` gloss).
- **Default attribute**: when the text after `|` has no `=`, it's the value of the marker's default attribute. If the marker defines none, keep it as an unnamed default (the 3.1 grammar's `_default`) rather than failing.
- **User attributes**: 3.0 requires the `x-` prefix. 3.1 allows any name beginning with `x` or `z`.
- **Where**: 3.0 allows attributes on character markers and milestones (plus `\fig`, `\periph`). 3.1 adds explicit attribute lists on paragraphs (`AttributesPreAll`). 3.2 (draft, U25001) moves attribute lists to the front of content (`|attrs|`). The trailing position is still parsed.
- **Pre-3.0 files**: a `|` inside a character span is just text, with one exception, the 2.x `\fig` positional form (§8). If you parse a 2.x file in 3.x mode, a literal `|` in text gets read as an attribute delimiter. Under 3.1 it must be written `\|`.

| Marker | Default attribute | Defined attributes | Since |
| --- | --- | --- | --- |
| [\\w](#) | lemma | lemma, strong, srcloc | 3.0 |
| [\\rb](#) | gloss | gloss | 3.0 |
| [\\xt](#) | link-href (3.0) → href (3.1) | href (link-href deprecated) | 3.0 |
| [\\jmp](#) | href (3.1) | 3.0: link-href, link-title, link-id. 3.1: href, title, id | 3.0 |
| [\\k](#) | key | key | 3.1 |
| [\\ref](#) | loc | loc, gen | 3.1 |
| [\\tl](#), [\\wl](#) | lang | lang | 3.1.2 draft |
| [\\fig](#) | — | src, size, ref, alt, loc, copy (USX 3.0: file) | 3.0 |
| [\\qt#-s / -e](#) | who / eid | sid, who / eid | 3.0 |
| [\\ts-s / -e](#) | sid / eid | sid / eid | 3.0 |
| [\\vid](#) | ref | ref, h | 3.1.2 draft |
| [\\periph](#) | — | id | 3.0 |
| table cells | — | colspan, align (USX only, derived from the marker) | 3.0 |

## 6. Milestones (3.0+)

- Syntax: `\name` \[`Hs`\] \[`|attrs`\] `\*`. The self-closing `\*` is what makes it a milestone.
- Pairs use the suffixes `-s` / `-e` and may cross paragraphs, chapters and verses. Match a start to its end by `sid`/`eid` if given, otherwise by name (innermost open first).
- Standalone milestones without a suffix are allowed (`\zms\*`, `\ts\*`). The 3.1 grammar style pattern is `[a-z][a-z0-9-]*`.
- 3.0: any milestone could carry sid/eid. 3.1: only where specified. Parse them anyway.
- 3.2 drafts add `\list-s`/`-e`, `\table-s`/`-e`, `\wj-s`/`-e`, `\t-s`/`-e`, and a "standalone" category: a bare milestone with no attributes and no delimiter.
- Watch for `\qt` (character, quoted text) next to `\qt-s` (milestone).

## 7. Whitespace and newlines

### 3.1 rules (normative)

- Common whitespace is only `U+0009 U+000A U+000D U+0020`. Every other space-like character (NBSP, thin space, ZWSP, ideographic space) is content.
- A run of whitespace containing a newline reduces to one newline. A run without one reduces to one space.
- Whitespace may be dropped at the start of any element's content, and at its end, *except inside a character element when followed by text or another character element*.
- A newline before a character marker becomes a space, and that space is content. Any whitespace before a paragraph marker becomes a newline that is not content.
- Newlines are `\r\n`, `\n` or a lone `\r`.

### 3.0 rules (ubsicap docs)

- Significant: the space after a paragraph marker or opening char/note marker, and the newline before a paragraph marker.
- Multiple spaces between words, or between text and a char/note marker, normalise to one space. Whitespace before `\v` normalises to a newline, because of the common habit of starting verses on new lines.
- Whitespace around note pairs is kept. Validators may flag it.

### 2.x

No written rules. Paragraph markers "should be preceded by a newline", and markers end at the next space. Expect anything: `\v` on its own line, notes on new lines, trailing spaces before `\f*`, tabs.

**Practical rule for all versions.** Consume exactly one whitespace character after an opening marker. Turn any newline run inside a paragraph into a single space. Keep a space before an inline marker only if it was there (it's content). Trim paragraph-final whitespace. Never treat a newline as a paragraph break: only markers break paragraphs.

## 8. Document structure

```
Scripture book  = BOM? \id CODE text NL
                  \usfm?                                  (3.0+, recommended 3.1+)
                  BookHeaders*        \ide \h \h1-3 \toc1-3 \toca1-3 \rem \sts
                  BookTitles*         \mt# \rem (+ notes, chars, //)
                  BookIntroduction*   \imt# \is# \ip \ipi \im \imi \ipq \imq \ipr \ipc \iq# \ib \ili# \iot \io# \iex \lit \rem, tables
                  BookIntroductionEndTitles*   \imte# / \mt#
                  \cl?                (book-wide chapter label)
                  ( \c N [\ca N\ca*] [\cp X] [\cl …] ChapterContent+ )*
ChapterContent  = Section | Para | List | Table | Sidebar | Milestone | Figure (3.1, peripheral content)
Peripheral book = \id FRT|BAK|OTH|INT|XXA-XXG … ( \periph Title|id="…"  content )*      (divided)
                | \id CNC|GLO|TDX|NDX … content                                       (standalone)
```

- 3.1 makes BookTitles optional and allows any 3-character book code (with at least one letter, 3.1.1). 3.0's grammar only allowed the enumerated codes.
- `\v` belongs only inside verse paragraphs (`versepara`, `list`, table cells). 3.2 explicitly forbids it in `sectionpara`/`otherpara`. Old files often put `\v` right after `\s` or `\c`. Recover by opening an implied `\p`.
- `\ip` and `\iex` may appear in chapter content (study Bibles, explanatory text). Introduction markers after `\c 1` are not automatically errors.
- Tables: consecutive `\tr` paragraphs are one table. A table ends at the first paragraph that isn't `\tr`.
- Peripheral divisions (since 2.1): `\periph Title`. From 3.0, `\periph Title|id="title"`.

### Figures across versions

```
2.x:  \fig DESC|FILE|SIZE|LOC|COPY|CAP|REF\fig*
      \fig |avnt016.tif|span|||At once they left their nets.|1.18\fig*
3.x:  \fig At once they left their nets.|src="avnt016.jpg" size="span" ref="1.18"\fig*
```

Detection: if the content has six unescaped pipes and no `name="` pair, it's the 2.x positional form. The 3.1 RelaxNG still has this form as `FigureTwo`.

## 9. Version detection and a compatibility strategy

1. If `\usfm X.Y` is present, use it as the *starting* mode, not a guarantee. Tools can add or keep the line without the markup being migrated, so let evidence in the file override it.
2. Evidence for ≥3.0: `\*` milestones, `|name="…"` attributes, `\periph …|id=`, 3.0-only markers (\\sd#, \\lh, \\lf, \\lim#, \\liv#, \\lik, \\litl, \\po, \\qd, \\wa, \\png, \\rb, \\sup, \\jmp, \\fw, \\xop, \\xta, \\toca#).
3. Evidence for ≥3.1: `\ref`, escaped characters (`\|`, `\/`, `\~`, `\\`), plain nested char markers that are closed explicitly, `\ipc`, `\fv …\fv*`.
4. Evidence for ≤2.x: `\fig` positional form, `\h1`–`\h3`, 2.0x peripheral markers, `\pro`, `!$` (pre-2.2), unclosed char markers.

The most permissive configuration that still gives correct trees for valid files of every version:

- Accept `\+` nesting *and* plain nesting, and use the lookahead heuristic in §3 for plain nesting.
- Accept implicit closure of every character marker at paragraph and note boundaries. Report it under 3.1, accept it silently under older versions.
- Treat `|` as an attribute delimiter only inside character spans, milestones, `\fig` and `\periph`, and only when what follows looks like attributes (`name="` or a default value running up to the end marker). Otherwise keep it as text.
- Recognise both `link-*` and plain linking attribute names, and both `src` and `file` on figures.
- Keep unknown markers and z-markers in the tree with their raw text, so that writing the file back out loses nothing.
- Keep a raw-source span on every node. Validation and round-tripping both need it, and whitespace handling differs between versions.

## 10. Version history at a glance

**1.0**Jun 2003\
UBS working group (formed March 2002) unifies regional SFM dialects: reserved initial letters (i- introductions, f- footnotes, x- cross references, e- study), numbered markers. Stylesheet for Paratext 6. Pre-1.0 stylesheet changes add \\h1–3, \\no, \\w, \\ipi, \\cp, \\vp, \\sig, \\cls, \\restore, \\add, \\pn, \\fe, \\s4, \\mte1/2. \\spd removed, and \\qs changes from paragraph to character. `!$` is the no-break space.

**1.53 / 1.54**Aug 2004\
Last 1.x. Still has \\pdi \\pde \\wr \\ps \\psi.

**2.0**Oct 2004\
\+ \\em, \\sr, \\pm \\pmo \\pmc \\pmr \\qm#, \\pro, \\wj, peripheral books FRT/BAK/OTH (replacing XXA–XXC), \\intro and \\maps divisions. − \\pdi \\pde \\wr \\ps.

**2.03 / 2.04 / 2.05**2005–2006\
\+ \\toc1 \\toc2, \\fl, \\fp (2.03); \\toc3, \\efm (2.04); \\rq (2.05). Around 2.05 the stylesheet gains the `NEST` pseudo-marker in OccursUnder (Paratext 6.1).

**2.1**Apr 2007\
\+ \\ili, \\periph (one marker for all peripheral divisions), CNC/GLO/TDX/NDX/INT books, study Bible content (\\ef, \\esb…\\esbe, \\cat). \\rq and \\ca become character markers. \\pub \\intro \\toc \\chron \\wm \\maps \\cov \\spine marked obsolete.

**2.2**Oct 2008\
\+ \\xot \\xnt, \\iqt; \\imte#. No-break space is now `~` instead of `!$`.

**2.3**Jul 2010\
\+ \\ex (study cross reference). Deprecates \\fs. Sidebars may contain any title/paragraph/poetry/table markers.

**2.4**Jun 2013\
Character nesting with the `\+` prefix (Paratext ≥ 7.4). The 2.4 stylesheet (Feb 2012) adds a 5th table column and centred cells \\thc#/\\tcc#. Before that, tables had at most 4 columns.

**2.5**Oct 2013\
Stylesheet-only update for Paratext 7.5.

**3.0**Apr 2018 (3.0.0 May 2018)\
\+ \\usfm, \\toca#, \\po, \\lh \\lf \\lim#, \\lik \\liv# \\litl, \\qd, \\sd#, \\wa, \\png, \\fw, \\xta, \\xop, \\jmp, \\rb, \\sup, milestones (\\qt#-s/-e, \\ts-s/-e), attributes (|…), linking attributes, \\periph |id, table column spans, new \\fig syntax. \\pr restored. Deprecates \\xdc \\fdc \\addpn \\h# \\pro.

**3.0.1–3.0.11**2020–2025\
3.0.1: \\xt allowed broadly; `#!\ColorName`/`#!\Attributes` special comments. 3.0.2: a space must follow the verse number. 3.0.3: \\pmo after \\b. 3.0.4: `NEST` for \\xt; \\jmp gains link-id/link-title. 3.0.5: 12 table columns (study Bible sty). 3.0.6: **\\efe added** (study Bible sty). 3.0.7: \\ip anywhere in study Bibles; \\erqe name fixed. 3.0.8: \\periph nonpublishable. 3.0.9: \\th6–12/\\tc6–12 in usfm.sty, sty files harmonised. 3.0.10: \\fig allowed in more places. 3.0.11: \\cd publishable.

**3.1**Jul 2024\
Single content model for USFM/USX/USJ. Explicit char closure required (except note structure). \\+ deprecated. All note text inside structural chars. \\fv needs closing. + \\ref. \\xt outside notes and link-\* deprecated. \\k@key. sid/eid only on milestones that define them. Escaping. Formal whitespace rules. BOM allowed. Optional titles. Any book code. \\mi# gets 3 levels.

**3.1.1**latest release\
\+ \\ipc, introduction list set (i-prefixed list paragraphs), \\lit allowed in introductions. \\p1/\\p2 become periphpara. Book codes must contain a letter. \\category may list several values.

**3.1.2**draft\
\+ \\wl, \\pl, \\ta, \\vid milestone, @lang on \\tl/\\wl, markers.ext categories `cell` and `attribute`.

**3.2**draft\
Leading attribute lists (`\ip |aid="x"| …`), @aid, anchors, \\u/\\U Unicode escapes, list/table milestones, standalone milestones, \\rem runs to end of line, \\v forbidden in sectionpara/otherpara.

## 11. Stylesheets (.sty)

A USFM stylesheet (`usfm.sty`, `usfm_sb.sty`, a project's `custom.sty`) is a plain-text list of marker definitions. Paratext uses it for display, and for validation via `\OccursUnder`/`\Rank`. It is the only machine-readable marker inventory for USFM 2.x, so a parser that loads stylesheets can learn the class and end marker of markers it doesn't have hard-coded, including a project's own z-markers.

### File grammar

```
file       = { line } ;                        UTF-8, BOM possible; CRLF or LF
line       = blank | comment | special | property
comment    = "#" text                          ; e.g. "# Version=3.0.11", section headings
special    = "#!" property                     ; property disabled for older Paratext (see below)
property   = "\" KEY [ WS value ] [ WS "#" comment ]
KEY        = [A-Za-z]+                         ; match case-insensitively ("\color" occurs in shipped files)
record     = "\Marker" name  { property }      ; a record runs until the next \Marker (blank lines are not significant)
```

- **A record starts at `\Marker`**. Every following property belongs to it until the next `\Marker`. Don't rely on blank lines.
- **Trailing comments** come after values: `\FirstLineIndent -.125 # 1/8 inch indent, 1 inch wrap`. Strip from `" #"` on, except in `\Name` / `\Description`, which may legitimately contain "#".
- **Boolean properties** have no value: `\Bold`, `\Italic`, `\Underline`, `\Superscript`, `\Smallcaps`, `\NotRepeatable`. Presence means true. Some files write `\Bold ` with a trailing space.
- **Case varies in values**: `Paragraph`/`paragraph`, `Other`/`other`, `Right`/`right`, `Nonpublishable`/`nonpublishable`. Normalise to lowercase.
- **Duplicate markers**: later stylesheets (e.g. a project `custom.sty` loaded after `usfm.sty`) add or override definitions. Merge property by property, last one wins.
- **`#!` special comments** hide properties from older Paratext versions while keeping them in the file. They have been used for `#!\ColorName highcon blue`, `#!\Attributes ?lemma ?strong ?srcloc` and, in ubsicap 3.0.x, *whole milestone records* (`#!\Marker qt-s` … `#!\StyleType Milestone`). A modern reader should parse `#!\Key` lines as properties. ParatextData's own 3.1 stylesheet defines the same milestones without `#!`.
- **Context comments (3.1 grammar stylesheet)**: a line like `# char: title introduction sectionpara versepara …` right after `\Marker` gives the markers.ext category (before the colon) and the parent categories the marker may appear in.

### Properties

| Property | Value | Meaning / parser use |
| --- | --- | --- |
| `\Marker` | name (no backslash) | Starts a record. Names can contain digits and hyphens (`qt1-s`) and, for `xtSee`, capitals. |
| `\Endmarker` | name, usually with `*` | `nd*` → the marker is closed by `\nd*`. For milestones (Paratext 3.x sty) it names the *paired end milestone*: `\Marker qt-s` + `\Endmarker qt-e`. A character marker without `\Endmarker` is either a note structural char (fr, ft …) or a table cell. |
| `\Name` | text | UI label, by convention "`marker - Category - Description`". A prefix of `DEPRECATED` or `OBSOLETE` marks a retired marker, and that's the only machine-readable deprecation flag in a .sty. |
| `\Description` | text | Free text. "(basic)" at the end puts the marker in Paratext's short list. |
| `\StyleType` | `Paragraph` \| `Character` \| `Note` \| `Milestone` | **The marker's class.** Milestone exists only in 3.x-era Paratext stylesheets. Some 2.x/3.0 obsolete records (the 2.0x peripheral markers) use lowercase `paragraph`. |
| `\TextType` | `Title` \| `Section` \| `VerseText` \| `NoteText` \| `Other` \| `BackTranslation` \| `ChapterNumber` \| `VerseNumber` | Content kind. ChapterNumber / VerseNumber appear only on `\c` / `\v` (not in the README list). |
| `\TextProperties` | space-separated flags: `paragraph publishable nonpublishable vernacular nonvernacular poetic level_1…level_5 chapter verse book note crossreference` | `book` marks `\id`, `chapter` marks `\c`, `verse` marks `\v`, and `note` marks note containers and their content. If a character marker has no publishable/vernacular flag, it inherits them from its paragraph. |
| `\OccursUnder` | space-separated marker names (plus `NEST`) | Paragraph markers: at least one listed marker must occur *earlier* (e.g. `\p` under `c`). Character markers: the paragraph types they may appear inside. Pseudo-name `NEST` (3.0.x) means the marker may nest inside another character span. No property means "anywhere". Lists contain typos (`tch12`, duplicate `tc10`), so validate softly. |
| `\Rank` | integer | Ordering among markers that share a parent: rank of an earlier marker ≤ rank of a later one. 0 or missing means unordered. `\h` (1) before `\mt1` (3) under `id`. |
| `\NotRepeatable` | flag | Must not repeat under the same parent. |
| `\Attributes` | space-separated names, `?` = optional (3.x; often behind `#!`) | `?lemma ?strong ?srcloc`. The **first listed name is the default attribute** in Paratext's convention, e.g. `\w` → lemma, `\xt` → link-href. |
| `\ColorName` | theme colour name (3.x; behind `#!`) | e.g. `highcon blue`. Display only. |
| `\Color` | decimal integer | A Windows COLORREF (0x00BBGGRR), not RGB: `128` is red and `16384` is dark green. Display only. |
| `\FontSize`, `\Fontname`, `\Bold`, `\Italic`, `\Underline`, `\Superscript`, `\Smallcaps` | points / name / flags | Display only. FontSize is relative to a base of 12. |
| `\Justification` | `Left` \| `Center` \| `Right` \| `Both` | Paragraph alignment. For table cells it encodes start/center/end (USX `align`). |
| `\LeftMargin`, `\RightMargin`, `\FirstLineIndent` | inches (decimal, may be negative) | Display only. Negative FirstLineIndent = hanging indent. |
| `\SpaceBefore`, `\SpaceAfter`, `\LineSpacing` | points / 0,1,2 | Display only. |

### Turning a stylesheet into parser rules

```
class(marker) =
    StyleType == "milestone"                       → milestone (pair partner = Endmarker)
    StyleType == "note"                             → note (Endmarker required)
    StyleType == "character" and Endmarker          → character span
    StyleType == "character" and no Endmarker       → note structural char (if TextType=NoteText/"note")
                                                      or table cell (th*/tc*)
    TextProperties has "chapter" / "verse" / "book" → \c / \v / \id
    StyleType == "paragraph"                        → paragraph (or \c, \cp, \esb…)
    unknown                                         → infer from usage (see §2)
```

```
\Marker w
\Endmarker w*
\Name w...w* - Peripheral Ref - Wordlist Entry
\Description A wordlist text item
\OccursUnder ip im ipi imi ... f fe x NEST
\TextType VerseText
\TextProperties publishable vernacular
\StyleType Character
\FontSize 12
#!\Attributes ?lemma ?strong ?srcloc
```

Stylesheet versions seen here: `# Version=2.502` (usfm2.5 branch), `3.0.0`, `3.0.11` (ubsicap master), the 3.1 grammar stylesheet in usfm-bible/tcdocs (no version line, carries `#` context comments, no display properties for new markers), and ParatextData's bundled `usfm3_1.sty` (header `# Version=3.0.9`, but it defines `\ref`, `\ts` and live milestone records).

## 12. Real-world USFM and how existing tools cope

Collected from the source code and issue trackers of the main USFM processors, plus the benchmark notes in this repo. Where a tool's behaviour is described, it was read from that tool's code or issues, which are linked.

### Malformed input you will meet

- **Missing spaces and odd marker forms**: `\v1`, `\s5` (a level that doesn't exist), lowercase book codes in `\id`, `\c 1 5`, a stray lone `\`, mismatched pairs like `\add…\dd*`. ([usfm-grammar #242](https://github.com/Bridgeconn/usfm-grammar/issues/242))
- **Unclosed character markers**, e.g. an unclosed `\fm` in NIV11 ([machine.py #155](https://github.com/sillsdev/machine.py/issues/155)), and table cells straight after `\v` (`\v 3 \tcr2 …`).
- **`\c` followed directly by `\v`** with no paragraph marker. usfmtc inserts an implied `\p`. usfm-grammar rejects it (#347).
- **Empty elements**: empty `\c`, `\q#`, `\li`, stub books with only `\id`.
- **Explicitly closed note parts** (`\fr 2.4\fr*`) are common in older files: the 2.2 reference documents both styles.
- **BOMs**: UTF-8 BOM, and sometimes UTF-16/32 or a byte-swapped U+FFFE (usfm-js strips both). **Encodings**: eBible's haiola sniffs BOMs and falls back to cp1252. BibleMultiConverter re-decodes using the `\ide` code page, falling back to ISO-8859-1.
- **Line endings**: CRLF, LF and lone CR. Paratext strips them and re-inserts newlines before paragraph markers, `\c` and `\v`, so whether there's a newline before `\v` carries no meaning.
- **Directional marks**: Paratext inserts LRM/RLM in RTL projects ([tcdocs #22](https://github.com/usfm-bible/tcdocs/issues/22)). The 3.1 VERSE pattern allows U+200F inside verse ranges.
- **unfoldingWord alignment markup**: `\zaln-s |x-strong="…" …\*` / `\zaln-e\*` milestones around `\w …|x-occurrence="1"\w*`. Expect very large numbers of z-milestones and `x-` attributes. usfm-js also normalises `strongs`/`x-strong` to `strong`.
- **Legacy SFM collisions**: in pre-USFM "SIL PNG 1998" SFM, `\fe` meant *end of footnote*. In USFM it starts an endnote. Adapt It resolves this in USFM's favour ([Adapt It docs](http://adaptit.martintribe.org/Advanced_Topics/Standard_Format_Markings/Set_USFM_Preferences.htm)).

### What the reference implementations do

| Situation | ParatextData / machine.py | usfmtc (committee) | usfm-grammar |
| --- | --- | --- | --- |
| Unknown marker | Character style inside notes, paragraph elsewhere. An unknown `\x*` is always an end marker. | Bare milestone, upgraded to char if a matching `\x*` follows | z-markers are milestones unless declared in markers.ext (#237) |
| Plain char marker inside char span | Closes all open char spans (2.x semantics) | — | Error without `+` (#297) |
| Unmatched end marker | Closes the char spans above it on the stack and reports it | — | — |
| `\+x` with no open span | Flagged | — | — |
| Unclosed char at paragraph end | Closed and marked `closed="false"` (tcdocs Paratext tests; older Paratext closed at end of line) | — | — |
| `\v` inside a heading | Closes the heading | — | — |
| 2.x `\fig` (6 pipes) | Converted | Split into alt/src/size/loc/copy/caption/ref | Unsupported (#181) |
| `\c` then `\v`, no paragraph | — | Implied `\p` | Error (#347) |
| Missing `\usfm` | — | Assumes newest behaviour. Unparseable value → 3.0. | — |
| Whitespace | Treats all Unicode spaces except U+3000 (plus ZWSP) as non-semantic. **This conflicts with 3.1**, where only TAB/CR/LF/SPACE reduce. | — | Node version leaks CR into USJ (#436) |
| `\id` code | Upper-cased, truncated to 3 | — | — |

"—" means no source was found for that tool's behaviour.

Sources: [machine.py usfm_tokenizer.py / usfm_parser.py](https://github.com/sillsdev/machine.py/blob/main/machine/corpora/usfm_tokenizer.py) (a port of ParatextData), [usfm-bible/usfmtc](https://github.com/usfm-bible/usfmtc), [Bridgeconn/usfm-grammar issues](https://github.com/Bridgeconn/usfm-grammar/issues), [tcdocs Paratext round-trip tests](https://github.com/usfm-bible/tcdocs/tree/main/tests/paratextTests) (unknown markers become `<para status="unknown">`; character styles may span `\v`).

### Lessons from the benchmarks in this repo

- ParatextData, usfmtc and usfm3 agree exactly on the 83-file WEB corpus (1,402 chapters, 38,058 verses, 34,753 paragraphs, 685,197 character spans, 2,218 notes). That's a good conformance target.
- Parsers that keep marker knowledge in a **table** (a .sty, usfmtc's categories, usfm3's `markers.rs`) survive unknown markers. Parsers that build the marker inventory into the grammar (the PEG `usfm` crate, the npm Pratt parser) fail outright on `\z` markers or anything they haven't seen.
- The tolerant designs share one step: before handling any token, **close what must close** (ParatextData, usfmtc's `node_depths`), or run an infallible lexer followed by implicit closing (usfm3).
- Regex shortcuts break: splitting on `"\\v "` also matches inside footnotes (usfm-parser counts 33 extra verses).

### Policy statements worth knowing

- **3.0**: processors "are free to ignore" `\z` markup. Unnamed `\fig` values "should be interpreted according to the strict USFM 2.x order" (ubsicap #27).
- **Versioning policy (tcdocs, from 3.1.2)**: compatible within a major version. Third-level releases only add markers, which older parsers can learn through `markers.ext`. Only a major release removes deprecated syntax. ([USFMTC Versioning](https://github.com/usfm-bible/tcdocs/blob/main/docs/USFMTC%20Versioning.md))
- **3.2**: `\uXXXX` escapes and leading attributes mean a pre-3.2 tokenizer would misread `\u00A0` as a marker. Read the `\usfm` line before choosing the tokenizer mode.
- Paratext 9.6 makes USFM 3.1 an opt-in per project. Projects that opt in no longer open in 9.5 and earlier. That's reported in secondary sources only; no primary source says which Paratext versions write `\usfm`.
- The 3.1/3.2 docs no longer describe `~` (open issue [tcdocs #120](https://github.com/usfm-bible/tcdocs/issues/120)). The grammar still escapes it, so keep treating it as NBSP.

## Sources

- USFM 3.1.1 docs: [docs.usfm.bible/usfm/3.1.1](https://docs.usfm.bible/usfm/3.1.1/index.html). Source repo [usfm-bible/tcdocs](https://github.com/usfm-bible/tcdocs): `markers/**/*.adoc`, `grammar/usx.rnc` (USFM grammar annotations), `grammar/usfm.ext`, `grammar/usfm3_1.sty`, `grammar/changes.txt`, release notes.
- USFM 3.0 docs: [ubsicap.github.io/usfm](https://ubsicap.github.io/usfm/). Repo [ubsicap/usfm](https://github.com/ubsicap/usfm): `sty/usfm.sty` (3.0.11), `usfm3.0` branch (3.0.0), `CHANGELOG.md`.
- USFM 2.5: [ubsicap.github.io/usfm/v2.5](https://ubsicap.github.io/usfm/v2.5/index.html), `usfm2.5` branch stylesheets (usfm.sty 2.502, usfm_sb.sty 2.501).
- USFM 2.4: [USFM User Reference 2.4 (PDF, 2013)](https://paratext.org/wp-content/uploads/2026/09/usfmreference2_4.pdf).

Each family is checked against the 2.4 manual's marker lists and the official stylesheets: 2.5 (usfm.sty 2.502 + usfm_sb.sty), 3.0 (3.0.0), 3.0.11 and the 3.1 grammar stylesheet. The 3.1.1 docs' "Added" version overrides the stylesheets where they disagree. Select a marker to open it.
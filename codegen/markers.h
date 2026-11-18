#ifndef CODEGEN_MARKERS_H
#define CODEGEN_MARKERS_H
/* The standard USFM marker table: one row per hash key.
 *
 * Keys are base names (q, tc, qt), plus the few full names whose
 * meaning differs from their base: k1/k2 are paragraphs while k is a
 * character style, and the tool-specific s1e..s4e / ms2e / ms3e put the
 * number before a trailing letter. The scanner parses levels, -s/-e,
 * + and * separately, so they are never keys.
 *
 * Roles and properties follow docs/usfm-field-guide.md (sections 2 and
 * 5); marker ids are the USFM_M_* values in usfm.h.
 *
 * TODO(sty): generate this table by parsing the official usfm.sty of
 * every supported version (2.4, 2.5, 3.0, 3.0.11, 3.1) instead of
 * maintaining it by hand. The generated output in usfm.h keeps the same
 * shape, so the parser does not change.
 *
 * Each row must start with four spaces and `{ "` -- the fuzz dictionary
 * is generated from these lines (see build.sh). */

typedef struct CodeGen_MarkerDef {
    const char *name;
    const char *marker;                 /* USFM_M_* */
    const char *role;                   /* USFM_ROLE_* */
    unsigned    level;                  /* fixed level of a full-name key */
    const char *props;                  /* USFM_MP_* expression or "0" */
} CodeGen_MarkerDef;

#define M_(id) "USFM_M_" #id
#define R_PARA "USFM_ROLE_PARA"
#define R_HEADER "USFM_ROLE_HEADER"
#define R_ID "USFM_ROLE_ID"
#define R_CHAPTER "USFM_ROLE_CHAPTER"
#define R_VERSE "USFM_ROLE_VERSE"
#define R_CHAR "USFM_ROLE_CHAR"
#define R_NOTE "USFM_ROLE_NOTE"
#define R_PART "USFM_ROLE_NOTE_PART"
#define R_ROW "USFM_ROLE_ROW"
#define R_CELL "USFM_ROLE_CELL"
#define R_MILESTONE "USFM_ROLE_MILESTONE"
#define R_SIDEBAR "USFM_ROLE_SIDEBAR"
#define R_SIDEBAR_END "USFM_ROLE_SIDEBAR_END"
#define R_PERIPH "USFM_ROLE_PERIPH"
#define R_FIGURE "USFM_ROLE_FIGURE"
#define P_NONE "0"
#define P_ARGW "USFM_MP_ARG_WORD"
#define P_ARGL "USFM_MP_ARG_LINE"
#define P_HEAD "USFM_MP_HEADING"

static const CodeGen_MarkerDef CodeGen_Markers[] = {
    /* Identification and book headers */
    { "id", M_(ID), R_ID, 0, P_ARGW },
    { "usfm", M_(USFM), R_HEADER, 0, P_ARGW },
    { "ide", M_(IDE), R_HEADER, 0, P_NONE },
    { "h", M_(H), R_HEADER, 0, P_NONE },
    { "toc", M_(TOC), R_HEADER, 0, P_NONE },
    { "toca", M_(TOCA), R_HEADER, 0, P_NONE },
    { "sts", M_(STS), R_HEADER, 0, P_NONE },
    /* Chapters and verses */
    { "c", M_(C), R_CHAPTER, 0, P_ARGW },
    { "ca", M_(CA), R_CHAR, 0, P_NONE },
    { "cp", M_(CP), R_PARA, 0, P_NONE },
    { "v", M_(V), R_VERSE, 0, P_ARGW },
    { "va", M_(VA), R_CHAR, 0, P_NONE },
    { "vp", M_(VP), R_CHAR, 0, P_NONE },
    /* Paragraphs */
    { "b", M_(B), R_PARA, 0, P_NONE },
    { "cd", M_(CD), R_PARA, 0, P_HEAD },
    { "cl", M_(CL), R_PARA, 0, P_HEAD },
    { "cls", M_(CLS), R_PARA, 0, P_NONE },
    { "d", M_(D), R_PARA, 0, P_NONE },
    { "ib", M_(IB), R_PARA, 0, P_NONE },
    { "ie", M_(IE), R_PARA, 0, P_NONE },
    { "iex", M_(IEX), R_PARA, 0, P_NONE },
    { "ili", M_(ILI), R_PARA, 0, P_NONE },
    { "im", M_(IM), R_PARA, 0, P_NONE },
    { "imi", M_(IMI), R_PARA, 0, P_NONE },
    { "imq", M_(IMQ), R_PARA, 0, P_NONE },
    { "imt", M_(IMT), R_PARA, 0, P_HEAD },
    { "imte", M_(IMTE), R_PARA, 0, P_HEAD },
    { "io", M_(IO), R_PARA, 0, P_NONE },
    { "iot", M_(IOT), R_PARA, 0, P_HEAD },
    { "ip", M_(IP), R_PARA, 0, P_NONE },
    { "ipc", M_(IPC), R_PARA, 0, P_NONE },
    { "ipi", M_(IPI), R_PARA, 0, P_NONE },
    { "ipq", M_(IPQ), R_PARA, 0, P_NONE },
    { "ipr", M_(IPR), R_PARA, 0, P_NONE },
    { "iq", M_(IQ), R_PARA, 0, P_NONE },
    { "is", M_(IS), R_PARA, 0, P_HEAD },
    { "k1", M_(K), R_PARA, 1, P_NONE },
    { "k2", M_(K), R_PARA, 2, P_NONE },
    { "lf", M_(LF), R_PARA, 0, P_NONE },
    { "lh", M_(LH), R_PARA, 0, P_NONE },
    { "li", M_(LI), R_PARA, 0, P_NONE },
    { "lim", M_(LIM), R_PARA, 0, P_NONE },
    { "lit", M_(LIT), R_PARA, 0, P_NONE },
    { "m", M_(M), R_PARA, 0, P_NONE },
    { "mi", M_(MI), R_PARA, 0, P_NONE },
    { "mr", M_(MR), R_PARA, 0, P_HEAD },
    { "ms", M_(MS), R_PARA, 0, P_HEAD },
    { "mt", M_(MT), R_PARA, 0, P_HEAD },
    { "mte", M_(MTE), R_PARA, 0, P_HEAD },
    { "nb", M_(NB), R_PARA, 0, P_NONE },
    { "p", M_(P), R_PARA, 0, P_NONE },
    { "pb", M_(PB), R_PARA, 0, P_NONE },
    { "pc", M_(PC), R_PARA, 0, P_NONE },
    { "pi", M_(PI), R_PARA, 0, P_NONE },
    { "pm", M_(PM), R_PARA, 0, P_NONE },
    { "pmc", M_(PMC), R_PARA, 0, P_NONE },
    { "pmo", M_(PMO), R_PARA, 0, P_NONE },
    { "pmr", M_(PMR), R_PARA, 0, P_NONE },
    { "po", M_(PO), R_PARA, 0, P_NONE },
    { "pr", M_(PR), R_PARA, 0, P_NONE },
    { "q", M_(Q), R_PARA, 0, P_NONE },
    { "qa", M_(QA), R_PARA, 0, P_HEAD },
    { "qc", M_(QC), R_PARA, 0, P_NONE },
    { "qd", M_(QD), R_PARA, 0, P_NONE },
    { "qm", M_(QM), R_PARA, 0, P_NONE },
    { "qr", M_(QR), R_PARA, 0, P_NONE },
    { "r", M_(R), R_PARA, 0, P_HEAD },
    { "rem", M_(REM), R_PARA, 0, P_NONE },
    { "s", M_(S), R_PARA, 0, P_HEAD },
    { "sd", M_(SD), R_PARA, 0, P_HEAD },
    { "sp", M_(SP), R_PARA, 0, P_HEAD },
    { "sr", M_(SR), R_PARA, 0, P_HEAD },
    { "tr", M_(TR), R_ROW, 0, P_NONE },
    /* Character styles */
    { "add", M_(ADD), R_CHAR, 0, P_NONE },
    { "bd", M_(BD), R_CHAR, 0, P_NONE },
    { "bdit", M_(BDIT), R_CHAR, 0, P_NONE },
    { "bk", M_(BK), R_CHAR, 0, P_NONE },
    { "dc", M_(DC), R_CHAR, 0, P_NONE },
    { "em", M_(EM), R_CHAR, 0, P_NONE },
    { "fm", M_(FM), R_CHAR, 0, P_NONE },
    { "fv", M_(FV), R_CHAR, 0, P_NONE },
    { "ior", M_(IOR), R_CHAR, 0, P_NONE },
    { "iqt", M_(IQT), R_CHAR, 0, P_NONE },
    { "it", M_(IT), R_CHAR, 0, P_NONE },
    { "jmp", M_(JMP), R_CHAR, 0, P_NONE },
    { "k", M_(K), R_CHAR, 0, P_NONE },
    { "lik", M_(LIK), R_CHAR, 0, P_NONE },
    { "litl", M_(LITL), R_CHAR, 0, P_NONE },
    { "liv", M_(LIV), R_CHAR, 0, P_NONE },
    { "nd", M_(ND), R_CHAR, 0, P_NONE },
    { "no", M_(NO), R_CHAR, 0, P_NONE },
    { "ord", M_(ORD), R_CHAR, 0, P_NONE },
    { "pn", M_(PN), R_CHAR, 0, P_NONE },
    { "png", M_(PNG), R_CHAR, 0, P_NONE },
    { "qac", M_(QAC), R_CHAR, 0, P_NONE },
    { "qs", M_(QS), R_CHAR, 0, P_NONE },
    { "qt", M_(QT), R_CHAR, 0, P_NONE },
    { "rb", M_(RB), R_CHAR, 0, P_NONE },
    { "ref", M_(REF), R_CHAR, 0, P_NONE },
    { "rq", M_(RQ), R_CHAR, 0, P_NONE },
    { "sc", M_(SC), R_CHAR, 0, P_NONE },
    { "sig", M_(SIG), R_CHAR, 0, P_NONE },
    { "sls", M_(SLS), R_CHAR, 0, P_NONE },
    { "sup", M_(SUP), R_CHAR, 0, P_NONE },
    { "tl", M_(TL), R_CHAR, 0, P_NONE },
    { "w", M_(W), R_CHAR, 0, P_NONE },
    { "wa", M_(WA), R_CHAR, 0, P_NONE },
    { "wg", M_(WG), R_CHAR, 0, P_NONE },
    { "wh", M_(WH), R_CHAR, 0, P_NONE },
    { "wj", M_(WJ), R_CHAR, 0, P_NONE },
    /* Notes */
    { "ef", M_(EF), R_NOTE, 0, P_ARGW },
    { "efe", M_(EFE), R_NOTE, 0, P_ARGW },
    { "ex", M_(EX), R_NOTE, 0, P_ARGW },
    { "f", M_(F), R_NOTE, 0, P_ARGW },
    { "fe", M_(FE), R_NOTE, 0, P_ARGW },
    { "x", M_(X), R_NOTE, 0, P_ARGW },
    /* Note content */
    { "fk", M_(FK), R_PART, 0, P_NONE },
    { "fl", M_(FL), R_PART, 0, P_NONE },
    { "fp", M_(FP), R_PART, 0, P_NONE },
    { "fq", M_(FQ), R_PART, 0, P_NONE },
    { "fqa", M_(FQA), R_PART, 0, P_NONE },
    { "fr", M_(FR), R_PART, 0, P_NONE },
    { "ft", M_(FT), R_PART, 0, P_NONE },
    { "fw", M_(FW), R_PART, 0, P_NONE },
    { "xk", M_(XK), R_PART, 0, P_NONE },
    { "xnt", M_(XNT), R_PART, 0, P_NONE },
    { "xo", M_(XO), R_PART, 0, P_NONE },
    { "xop", M_(XOP), R_PART, 0, P_NONE },
    { "xot", M_(XOT), R_PART, 0, P_NONE },
    { "xq", M_(XQ), R_PART, 0, P_NONE },
    { "xt", M_(XT), R_PART, 0, P_NONE },
    { "xta", M_(XTA), R_PART, 0, P_NONE },
    /* Table cells */
    { "tc", M_(TC), R_CELL, 0, P_NONE },
    { "tcc", M_(TCC), R_CELL, 0, P_NONE },
    { "tcr", M_(TCR), R_CELL, 0, P_NONE },
    { "th", M_(TH), R_CELL, 0, P_NONE },
    { "thc", M_(THC), R_CELL, 0, P_NONE },
    { "thr", M_(THR), R_CELL, 0, P_NONE },
    /* Milestones */
    { "ts", M_(TS), R_MILESTONE, 0, P_NONE },
    /* Figures, peripherals, sidebars, categories */
    { "cat", M_(CAT), R_CHAR, 0, P_NONE },
    { "esb", M_(ESB), R_SIDEBAR, 0, P_NONE },
    { "esbe", M_(ESBE), R_SIDEBAR_END, 0, P_NONE },
    { "fig", M_(FIG), R_FIGURE, 0, P_NONE },
    { "periph", M_(PERIPH), R_PERIPH, 0, P_ARGL },
    /* Deprecated */
    { "addpn", M_(ADDPN), R_CHAR, 0, P_NONE },
    { "fdc", M_(FDC), R_PART, 0, P_NONE },
    { "fs", M_(FS), R_PART, 0, P_NONE },
    { "ph", M_(PH), R_PARA, 0, P_NONE },
    { "phi", M_(PHI), R_PARA, 0, P_NONE },
    { "pro", M_(PRO), R_CHAR, 0, P_NONE },
    { "xdc", M_(XDC), R_PART, 0, P_NONE },
    /* Obsolete */
    { "conc", M_(CONC), R_PARA, 0, P_NONE },
    { "cov", M_(COV), R_PARA, 0, P_NONE },
    { "glo", M_(GLO), R_PARA, 0, P_NONE },
    { "idx", M_(IDX), R_PARA, 0, P_NONE },
    { "intro", M_(INTRO), R_PARA, 0, P_NONE },
    { "maps", M_(MAPS), R_PARA, 0, P_NONE },
    { "pref", M_(PREF), R_PARA, 0, P_NONE },
    { "ps", M_(PS), R_PARA, 0, P_NONE },
    { "psi", M_(PSI), R_PARA, 0, P_NONE },
    { "pub", M_(PUB), R_PARA, 0, P_NONE },
    { "pubinfo", M_(PUBINFO), R_PARA, 0, P_NONE },
    { "spine", M_(SPINE), R_PARA, 0, P_NONE },
    { "wr", M_(WR), R_CHAR, 0, P_NONE },
    /* Stylesheet-only */
    { "efm", M_(EFM), R_PART, 0, P_NONE },
    { "ndx", M_(NDX), R_CHAR, 0, P_NONE },
    { "xtSee", M_(XTSEE), R_PART, 0, P_NONE },
    { "xtSeeAlso", M_(XTSEEALSO), R_PART, 0, P_NONE },
    /* Tool-specific */
    { "erq", M_(ERQ), R_PARA, 0, P_NONE },
    { "erqe", M_(ERQE), R_PARA, 0, P_NONE },
    { "mse", M_(MSE), R_PARA, 0, P_HEAD },
    { "ms2e", M_(MSE), R_PARA, 2, P_HEAD },
    { "ms3e", M_(MSE), R_PARA, 3, P_HEAD },
    { "restore", M_(RESTORE), R_PARA, 0, P_NONE },
    { "s1e", M_(SE), R_PARA, 1, P_HEAD },
    { "s2e", M_(SE), R_PARA, 2, P_HEAD },
    { "s3e", M_(SE), R_PARA, 3, P_HEAD },
    { "s4e", M_(SE), R_PARA, 4, P_HEAD },
    /* Drafts (3.1.2 / 3.2) */
    { "ilit", M_(ILIT), R_PARA, 0, P_NONE },
    { "list", M_(LIST), R_MILESTONE, 0, P_NONE },
    { "pl", M_(PL), R_PARA, 0, P_NONE },
    { "t", M_(T), R_MILESTONE, 0, P_NONE },
    { "ta", M_(TA), R_CHAR, 0, P_NONE },
    { "table", M_(TABLE), R_MILESTONE, 0, P_NONE },
    { "vid", M_(VID), R_MILESTONE, 0, P_NONE },
    { "wl", M_(WL), R_CHAR, 0, P_NONE },
};

#endif

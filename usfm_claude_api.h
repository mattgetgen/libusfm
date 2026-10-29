/* usfm.h - draft public API, v0.2 (design sketch, no implementation yet)
 *
 * Single-header USFM parser. C99, compiles as C++.
 * In exactly one translation unit:
 *     #define USFM_IMPLEMENTATION
 *     #include "usfm.h"
 *
 * Quick start:
 *     USFM_Err err;
 *     USFM_Ast *ast = USFM_Ast_Parse(src, len, NULL, &err); // defaults, libc
 *     if (!ast) { ... err.code, err.msg, err.pos ... }
 *     ...
 *     USFM_Ast_Free(ast);
 *
 * Four handle types, one model:
 *
 *     text --USFM_Cst_Parse--> USFM_Cst --USFM_Cst_ToAst--+
 *                                                           v
 *     text --USFM_Ast_Parse------------------------------> USFM_Ast
 *                                                         |    ^
 *                                          USFM_Ast_ToMut |    | USFM_MutAst_ToAst
 *                                                         v    |
 *     text <--USFM_MutAst_Write------------------------- USFM_MutAst
 *
 *     USFM_Stylesheet: parsed from .sty text, or from a .sty USFM_Cst.
 *
 * Ground rules (NASA Power of 10 + TigerStyle):
 *   - All allocation goes through a pluggable allocator (USFM_Alc).
 *     The library never calls malloc directly.
 *   - No recursion. Parsers drive an explicit stack of fixed depth.
 *   - Every loop has a static upper bound (input length or a constant).
 *   - No callbacks in the API: search and traversal are iterators.
 *     The single exception is USFM_Alc, whose function pointers ARE
 *     the allocator interface.
 *   - Trees use u32 indices, not pointers (one level of indirection).
 *   - Sizes are u32; inputs are capped at USFM_INPUT_MAX bytes.
 *   - Zero-copy: all text is a (offset, length) span into the input.
 *   - Malformed USFM is never an error: it produces diagnostics.
 *     Errors (USFM_Status) are reserved for caller/resource problems.
 *
 * Memory model
 *   Every handle (USFM_Cst, USFM_Ast, USFM_MutAst, USFM_Stylesheet)
 *   is opaque and owns everything it allocated, including its own
 *   diagnostics. It is created with an allocator, keeps a COPY of the
 *   USFM_Alc struct, and is released with one free call. alc->ctx
 *   must outlive the handle, just as a borrowed input must.
 *
 *   Conversions always produce an INDEPENDENT handle: freeing the
 *   source afterwards is safe. Owned data (diagnostics, a copied
 *   input) is copied; a borrowed input stays borrowed, so the
 *   caller's buffer must outlive every handle that refers to it.
 *
 *   Pass NULL for the allocator to get the built-in libc default.
 *   Define USFM_NO_LIBC to remove it: a NULL allocator then fails
 *   with USFM_ERR_NO_ALLOCATOR, so freestanding builds never link
 *   malloc. For static allocation, use the fixed arena
 *   (USFM_Alc_InitFixed) sized with the USFM_*_GetMaxMemory functions.
 *
 * Power of 10 rule 3 / TigerStyle
 *   usfm.h is rule-3-COMPATIBLE, not rule-3-ENFORCING. Rule 3 governs
 *   programs, not libraries: an application gets full compliance by
 *   initializing a fixed arena at startup and never allocating again.
 *
 *   Concern                          How usfm.h handles it
 *   -------------------------------  -----------------------------------
 *   Unbounded use                    O(input) per handle, with a
 *                                    documented constant (the
 *                                    USFM_*_GetMaxMemory functions);
 *                                    depth cap (USFM_STACK_DEPTH) and
 *                                    input cap (USFM_INPUT_MAX, below
 *                                    4 GiB for u32 offsets) asserted
 *   Fragmentation, allocation in     a few geometrically grown arrays
 *   hot paths                        per handle; never one per node
 *   Leaks and use-after-free         one owner (the handle), one free;
 *                                    no per-node frees in the API
 *   Unchecked failure                every allocation checked; running
 *                                    out of memory is a status code,
 *                                    never an assert
 *   Hidden heap use                  all allocation goes through
 *                                    USFM_Alc; USFM_NO_LIBC proves it
 *                                    at link time
 *
 * Failure semantics
 *   - Syntax problems: a handle is still returned, with diagnostics.
 *   - Resource failure while building a tree: everything allocated so
 *     far is freed, NULL is returned, err.code = USFM_ERR_NOMEM and
 *     err.pos = the byte reached. Callers never see a partial CST or
 *     AST: the preorder invariants only hold once a build is complete.
 *   - Resource failure while storing a DIAGNOSTIC is not fatal: the
 *     parse carries on, the diagnostic is counted as dropped, and the
 *     handle IS returned, with err.code = USFM_ERR_NOMEM and err.pos =
 *     the offset of the first dropped diagnostic. So one check covers
 *     everything:
 *         NULL                          -> failed, nothing to free
 *         non-NULL, err.code == OK      -> complete
 *         non-NULL, err.code == NOMEM   -> trees complete, some
 *                                          diagnostics dropped
 *   - Growth keeps the old block when resize fails, so it can still
 *     be freed.
 *   - Mutable ASTs use sticky errors instead (see that section).
 */
#ifndef USFM_H
#define USFM_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------
 * Compile-time configuration
 * ------------------------------------------------------------------- */
#ifndef USFM_STACK_DEPTH
#define USFM_STACK_DEPTH 64u            /* max marker nesting */
#endif
#ifndef USFM_INPUT_MAX
#define USFM_INPUT_MAX 0x7FFFFFFFu      /* 2 GiB - 1 */
#endif
#ifndef USFM_ASSERT
#include <assert.h>
#define USFM_ASSERT(x) assert(x)
#endif
/* USFM_SIMD: auto-detected (AVX2 > SSE2 > NEON > scalar).
 * Define USFM_SIMD_NONE to force the scalar path (e.g. for fuzzing).
 * Define USFM_NO_LIBC to drop the default malloc-based allocator. */

#define USFM_NIL 0xFFFFFFFFu            /* "no node" index (UINT32_MAX) */

/* Export control for shared-library builds, so the library can be
 * loaded from any language's FFI. Define USFM_SHARED when building
 * or using a DLL / .so; define USFM_BUILD only when building it. */
#ifndef USFM_API
#  if defined(USFM_SHARED) && defined(_WIN32)
#    ifdef USFM_BUILD
#      define USFM_API __declspec(dllexport)
#    else
#      define USFM_API __declspec(dllimport)
#    endif
#  elif defined(USFM_SHARED) && defined(__GNUC__)
#    define USFM_API __attribute__((visibility("default")))
#  else
#    define USFM_API
#  endif
#endif

/* ---------------------------------------------------------------------
 * Basic types
 * ------------------------------------------------------------------- */
typedef struct USFM_Span {
    uint32_t offset;                    /* byte offset into input */
    uint32_t length;                    /* byte length */
} USFM_Span;

typedef enum USFM_Status {
    USFM_OK = 0,
    USFM_ERR_CAPACITY,                  /* caller's output buffer too small */
    USFM_ERR_DEPTH,                     /* nesting > USFM_STACK_DEPTH */
    USFM_ERR_INPUT_TOO_LARGE,
    USFM_ERR_INVALID_ARG,
    USFM_ERR_NOMEM,                     /* the allocator returned NULL;
                                           with a non-NULL handle: only
                                           diagnostics were dropped */
    USFM_ERR_NO_ALLOCATOR               /* NULL allocator + USFM_NO_LIBC */
} USFM_Status;

/* Filled by every function that can fail (pass NULL to ignore). */
typedef struct USFM_Err {
    const char *msg;                    /* static string, never NULL */
    uint32_t    code;                   /* USFM_Status */
    uint32_t    pos;                    /* byte offset reached in input */
} USFM_Err;

enum USFM_Version {
    USFM_VERSION_AUTO = 0,              /* read \usfm, else assume 3.1 */
    USFM_VERSION_1,
    USFM_VERSION_2,
    USFM_VERSION_3_0,
    USFM_VERSION_3_1
};

/* Marker ids: every marker in USFM 2.4 through 3.1, plus markers
 * deprecated or removed in earlier versions (back to 1.0) that still
 * appear in real files, stylesheet-only and tool-specific markers,
 * and the 3.1.2 / 3.2 drafts. Source: the USFM Parser Field Guide.
 * Ids are interned via a generated perfect hash.
 *
 * Numbered markers share a base id and carry the number separately:
 * \q2 -> (USFM_M_Q, level 2). Milestones share their base id too and
 * are told apart by token flags: \qt1-s -> (USFM_M_QT, level 1,
 * USFM_TF_MILESTONE_START). Because \k is a character style but \k1
 * is a paragraph, a marker's class comes from the generated table
 * entry for its full name, never from the base id alone.
 *
 * Values are explicit because they are part of the ABI: never
 * renumber or reuse one. New markers are appended at the end of the
 * enum (before USFM_M_CUSTOM_BASE), whatever section they belong to.
 * The optional break // and the no-break space ~ are tokens, not
 * markers, so they have no id. */
typedef uint16_t USFM_Marker;
enum {
    USFM_M_NONE = 0,                    /* not a marker */
    USFM_M_UNKNOWN = 1,                 /* name available via span */

    /* Identification */
    USFM_M_ID = 2,                      /* \id */
    USFM_M_USFM = 3,                    /* \usfm (3.0+) */

    /* Chapters and verses */
    USFM_M_C = 4,                       /* \c */
    USFM_M_CA = 5,                      /* \ca */
    USFM_M_CP = 6,                      /* \cp */
    USFM_M_V = 7,                       /* \v */
    USFM_M_VA = 8,                      /* \va */
    USFM_M_VP = 9,                      /* \vp */

    /* Paragraphs */
    USFM_M_B = 10,                      /* \b */
    USFM_M_CD = 11,                     /* \cd */
    USFM_M_CL = 12,                     /* \cl */
    USFM_M_CLS = 13,                    /* \cls */
    USFM_M_D = 14,                      /* \d */
    USFM_M_H = 15,                      /* \h \h1..\h3 */
    USFM_M_IB = 16,                     /* \ib */
    USFM_M_IDE = 17,                    /* \ide */
    USFM_M_IE = 18,                     /* \ie */
    USFM_M_IEX = 19,                    /* \iex */
    USFM_M_ILI = 20,                    /* \ili \ili1 \ili2 */
    USFM_M_IM = 21,                     /* \im */
    USFM_M_IMI = 22,                    /* \imi */
    USFM_M_IMQ = 23,                    /* \imq */
    USFM_M_IMT = 24,                    /* \imt \imt1..\imt4 */
    USFM_M_IMTE = 25,                   /* \imte \imte1 \imte2 */
    USFM_M_IO = 26,                     /* \io \io1..\io4 */
    USFM_M_IOT = 27,                    /* \iot */
    USFM_M_IP = 28,                     /* \ip */
    USFM_M_IPC = 29,                    /* \ipc (3.1.1+) */
    USFM_M_IPI = 30,                    /* \ipi */
    USFM_M_IPQ = 31,                    /* \ipq */
    USFM_M_IPR = 32,                    /* \ipr */
    USFM_M_IQ = 33,                     /* \iq \iq1..\iq3 */
    USFM_M_IS = 34,                     /* \is \is1 \is2 */
    USFM_M_LF = 35,                     /* \lf (3.0+) */
    USFM_M_LH = 36,                     /* \lh (3.0+) */
    USFM_M_LI = 37,                     /* \li \li1..\li4 */
    USFM_M_LIM = 38,                    /* \lim \lim1..\lim4 (3.0+) */
    USFM_M_LIT = 39,                    /* \lit */
    USFM_M_M = 40,                      /* \m */
    USFM_M_MI = 41,                     /* \mi \mi1..\mi4 */
    USFM_M_MR = 42,                     /* \mr */
    USFM_M_MS = 43,                     /* \ms \ms1..\ms3 */
    USFM_M_MT = 44,                     /* \mt \mt1..\mt4 */
    USFM_M_MTE = 45,                    /* \mte \mte1 \mte2 */
    USFM_M_NB = 46,                     /* \nb */
    USFM_M_P = 47,                      /* \p \p1 \p2 (\p1 \p2 stylesheet-only) */
    USFM_M_PB = 48,                     /* \pb */
    USFM_M_PC = 49,                     /* \pc */
    USFM_M_PI = 50,                     /* \pi \pi1..\pi3 */
    USFM_M_PM = 51,                     /* \pm */
    USFM_M_PMC = 52,                    /* \pmc (2.0+) */
    USFM_M_PMO = 53,                    /* \pmo (2.0+) */
    USFM_M_PMR = 54,                    /* \pmr (2.0+) */
    USFM_M_PO = 55,                     /* \po (3.0+) */
    USFM_M_PR = 56,                     /* \pr */
    USFM_M_Q = 57,                      /* \q \q1..\q4 */
    USFM_M_QA = 58,                     /* \qa */
    USFM_M_QC = 59,                     /* \qc */
    USFM_M_QD = 60,                     /* \qd (3.0+) */
    USFM_M_QM = 61,                     /* \qm \qm1..\qm3 (2.0+) */
    USFM_M_QR = 62,                     /* \qr */
    USFM_M_R = 63,                      /* \r */
    USFM_M_REM = 64,                    /* \rem */
    USFM_M_S = 65,                      /* \s \s1..\s4 */
    USFM_M_SD = 66,                     /* \sd \sd1..\sd4 (3.0+) */
    USFM_M_SP = 67,                     /* \sp */
    USFM_M_SR = 68,                     /* \sr */
    USFM_M_STS = 69,                    /* \sts */
    USFM_M_TOC = 70,                    /* \toc \toc1..\toc3 (2.0+; bare \toc: obsolete 2.0 periph) */
    USFM_M_TOCA = 71,                   /* \toca1..\toca3 (3.0+) */
    USFM_M_TR = 72,                     /* \tr \tr1 \tr2 (\tr1 \tr2 obsolete) */

    /* Character styles */
    USFM_M_ADD = 73,                    /* \add */
    USFM_M_BD = 74,                     /* \bd */
    USFM_M_BDIT = 75,                   /* \bdit */
    USFM_M_BK = 76,                     /* \bk */
    USFM_M_DC = 77,                     /* \dc */
    USFM_M_EM = 78,                     /* \em (2.0+) */
    USFM_M_FM = 79,                     /* \fm */
    USFM_M_FV = 80,                     /* \fv */
    USFM_M_IOR = 81,                    /* \ior */
    USFM_M_IQT = 82,                    /* \iqt */
    USFM_M_IT = 83,                     /* \it */
    USFM_M_JMP = 84,                    /* \jmp (3.0+) */
    USFM_M_K = 85,                      /* \k \k1 \k2 (\k char; \k1 \k2 stylesheet-only para) */
    USFM_M_LIK = 86,                    /* \lik (3.0+) */
    USFM_M_LITL = 87,                   /* \litl (3.0+) */
    USFM_M_LIV = 88,                    /* \liv \liv1..\liv5 (3.0+) */
    USFM_M_ND = 89,                     /* \nd */
    USFM_M_NO = 90,                     /* \no */
    USFM_M_ORD = 91,                    /* \ord */
    USFM_M_PN = 92,                     /* \pn */
    USFM_M_PNG = 93,                    /* \png (3.0+) */
    USFM_M_QAC = 94,                    /* \qac */
    USFM_M_QS = 95,                     /* \qs */
    USFM_M_QT = 96,                     /* \qt; milestones \qt-s \qt-e \qt1-s..\qt5-e (3.0+) */
    USFM_M_RB = 97,                     /* \rb (3.0+) */
    USFM_M_REF = 98,                    /* \ref (3.1+) */
    USFM_M_RQ = 99,                     /* \rq (2.05+) */
    USFM_M_SC = 100,                    /* \sc */
    USFM_M_SIG = 101,                   /* \sig */
    USFM_M_SLS = 102,                   /* \sls */
    USFM_M_SUP = 103,                   /* \sup (3.0+) */
    USFM_M_TL = 104,                    /* \tl */
    USFM_M_W = 105,                     /* \w */
    USFM_M_WA = 106,                    /* \wa (3.0+) */
    USFM_M_WG = 107,                    /* \wg */
    USFM_M_WH = 108,                    /* \wh */
    USFM_M_WJ = 109,                    /* \wj \wj-e \wj-s (-s/-e form is a 3.2 draft) */

    /* Notes */
    USFM_M_EF = 110,                    /* \ef (2.1+) */
    USFM_M_EFE = 111,                   /* \efe (3.0.6+) */
    USFM_M_EX = 112,                    /* \ex (2.3+) */
    USFM_M_F = 113,                     /* \f */
    USFM_M_FE = 114,                    /* \fe */
    USFM_M_X = 115,                     /* \x */

    /* Note content */
    USFM_M_FK = 116,                    /* \fk */
    USFM_M_FL = 117,                    /* \fl (2.03+) */
    USFM_M_FP = 118,                    /* \fp (2.03+) */
    USFM_M_FQ = 119,                    /* \fq */
    USFM_M_FQA = 120,                   /* \fqa */
    USFM_M_FR = 121,                    /* \fr */
    USFM_M_FT = 122,                    /* \ft */
    USFM_M_FW = 123,                    /* \fw (3.0+) */
    USFM_M_XK = 124,                    /* \xk */
    USFM_M_XNT = 125,                   /* \xnt (2.2+) */
    USFM_M_XO = 126,                    /* \xo */
    USFM_M_XOP = 127,                   /* \xop (3.0+) */
    USFM_M_XOT = 128,                   /* \xot (2.2+) */
    USFM_M_XQ = 129,                    /* \xq */
    USFM_M_XT = 130,                    /* \xt */
    USFM_M_XTA = 131,                   /* \xta (3.0+) */

    /* Table cells */
    USFM_M_TC = 132,                    /* \tc1..\tc12 */
    USFM_M_TCC = 133,                   /* \tcc1..\tcc12 */
    USFM_M_TCR = 134,                   /* \tcr1..\tcr12 */
    USFM_M_TH = 135,                    /* \th1..\th12 */
    USFM_M_THC = 136,                   /* \thc1..\thc12 */
    USFM_M_THR = 137,                   /* \thr1..\thr12 */

    /* Milestones */
    USFM_M_TS = 138,                    /* \ts-s \ts-e \ts (3.0+) */

    /* Figures, peripherals, sidebars, categories */
    USFM_M_CAT = 139,                   /* \cat (2.1+) */
    USFM_M_ESB = 140,                   /* \esb (2.1+) */
    USFM_M_ESBE = 141,                  /* \esbe (2.1+) */
    USFM_M_FIG = 142,                   /* \fig */
    USFM_M_PERIPH = 143,                /* \periph (2.1+) */

    /* Deprecated (still found in older files) */
    USFM_M_ADDPN = 144,                 /* \addpn (deprecated 3.0) */
    USFM_M_FDC = 145,                   /* \fdc (deprecated 3.0) */
    USFM_M_FS = 146,                    /* \fs (deprecated 2.3) */
    USFM_M_PH = 147,                    /* \ph \ph1..\ph3 (deprecated 3.0) */
    USFM_M_PHI = 148,                   /* \phi (deprecated 2.0) */
    USFM_M_PRO = 149,                   /* \pro (deprecated 3.0) */
    USFM_M_XDC = 150,                   /* \xdc (deprecated 3.0) */

    /* Obsolete (removed before 2.1) */
    USFM_M_CONC = 151,                  /* \conc (obsolete 2.1) */
    USFM_M_COV = 152,                   /* \cov (obsolete 2.1) */
    USFM_M_GLO = 153,                   /* \glo (obsolete 2.1) */
    USFM_M_IDX = 154,                   /* \idx (obsolete 2.1) */
    USFM_M_INTRO = 155,                 /* \intro (obsolete 2.1) */
    USFM_M_MAPS = 156,                  /* \maps (obsolete 2.1) */
    USFM_M_PREF = 157,                  /* \pref (obsolete 2.1) */
    USFM_M_PS = 158,                    /* \ps (obsolete 2.0) */
    USFM_M_PSI = 159,                   /* \psi (obsolete 2.0) */
    USFM_M_PUB = 160,                   /* \pub (obsolete 2.1) */
    USFM_M_PUBINFO = 161,               /* \pubinfo (obsolete 2.1) */
    USFM_M_SPINE = 162,                 /* \spine (obsolete 2.1) */
    USFM_M_WR = 163,                    /* \wr (obsolete 2.0) */

    /* Stylesheet-only (defined in usfm.sty, not the spec) */
    USFM_M_EFM = 164,                   /* \efm */
    USFM_M_NDX = 165,                   /* \ndx */
    USFM_M_XTSEE = 166,                 /* \xtSee */
    USFM_M_XTSEEALSO = 167,             /* \xtSeeAlso */

    /* Tool-specific */
    USFM_M_ERQ = 168,                   /* \erq */
    USFM_M_ERQE = 169,                  /* \erqe */
    USFM_M_MSE = 170,                   /* \mse \ms2e \ms3e (tool-specific; number before the e) */
    USFM_M_RESTORE = 171,               /* \restore */
    USFM_M_SE = 172,                    /* \s1e..\s4e (tool-specific; number before the e) */

    /* Drafts (3.1.2 / 3.2 development, not yet released) */
    USFM_M_ILIT = 173,                  /* \ilit (draft 3.2) */
    USFM_M_LIST = 174,                  /* \list-s \list-e (draft 3.2) */
    USFM_M_PL = 175,                    /* \pl (draft 3.1.2) */
    USFM_M_T = 176,                     /* \t-s \t-e (draft 3.2) */
    USFM_M_TA = 177,                    /* \ta (draft 3.1.2) */
    USFM_M_TABLE = 178,                 /* \table-s \table-e (draft 3.2) */
    USFM_M_VID = 179,                   /* \vid (draft 3.1.2) */
    USFM_M_WL = 180,                    /* \wl (draft 3.1.2) */

    USFM_M_CUSTOM_BASE = 0x8000         /* + stylesheet style index */
};

/* Standard markers come from a table generated at pre-compile time
 * by usfm_codegen.c (see "Generated tables" below). Custom markers
 * come from a runtime-parsed stylesheet: they get ids from
 * USFM_M_CUSTOM_BASE upward, where (id - USFM_M_CUSTOM_BASE) is the
 * index into USFM_Stylesheet_GetStyles(). */
enum USFM_MarkerClass {
    USFM_CLASS_NONE = 0,                /* unset / unknown */
    USFM_CLASS_PARAGRAPH, USFM_CLASS_CHARACTER, USFM_CLASS_NOTE,
    USFM_CLASS_MILESTONE, USFM_CLASS_HEADER, USFM_CLASS_TABLE
};

typedef struct USFM_Stylesheet USFM_Stylesheet;    /* opaque */

/* Copied into every handle parsed with it (and carried through
 * conversions). The stylesheet itself is NOT copied: it must outlive
 * every handle that uses it, because custom marker ids index into it. */
typedef struct USFM_Config {
    uint8_t version;                    /* enum USFM_Version */
    uint8_t reserved[3];
    const USFM_Stylesheet *custom;      /* custom.sty, or NULL */
} USFM_Config;

/* Diagnostics: problems found in the input (malformed USFM, bad
 * numbers, attributes, escapes ...). Parsing always continues and
 * still produces output; diagnostics report what was recovered from.
 *
 * Handles own their diagnostics (USFM_Ast_GetDiagnostics() etc.),
 * grown through the allocator. If growing them fails, the parse
 * continues, further diagnostics are counted as dropped
 * (USFM_Ast_GetDroppedDiagnosticCount() etc.), and the parse reports
 * USFM_ERR_NOMEM alongside the returned handle (see "Failure
 * semantics" at the top).
 *
 * USFM_Tokenize and USFM_TokenizeStylesheet follow the same rules,
 * returning their diagnostics in a library-allocated
 * USFM_DiagnosticArray. Normalize and decode report no diagnostics;
 * parse for those. */
enum USFM_DiagnosticSeverity {
    USFM_SEVERITY_NONE = 0,
    USFM_SEVERITY_ERROR,                /* input is invalid USFM */
    USFM_SEVERITY_WARNING,              /* valid but suspicious */
    USFM_SEVERITY_INFO                  /* e.g. custom marker override */
};

/* Excerpt; the full list is part of the spec work. Each code has a
 * fixed severity, so it isn't stored per diagnostic. */
enum USFM_DiagnosticCode {
    USFM_DIAGNOSTIC_NONE = 0,
    USFM_DIAGNOSTIC_UNKNOWN_MARKER,
    USFM_DIAGNOSTIC_UNCLOSED_MARKER,     /* \w without \w* */
    USFM_DIAGNOSTIC_UNEXPECTED_CLOSE,    /* \w* with no open \w */
    USFM_DIAGNOSTIC_INVALID_NESTING,     /* para marker inside char */
    USFM_DIAGNOSTIC_INVALID_NUMBER,      /* \v abc, \c 0, \v 5-3 */
    USFM_DIAGNOSTIC_DUPLICATE_NUMBER,
    USFM_DIAGNOSTIC_VERSE_BEFORE_CHAPTER,
    USFM_DIAGNOSTIC_INVALID_BOOK_CODE,   /* \id GNE */
    USFM_DIAGNOSTIC_MISSING_CALLER,      /* \f with no caller */
    USFM_DIAGNOSTIC_UNTERMINATED_QUOTE,  /* attribute value */
    USFM_DIAGNOSTIC_UNKNOWN_ATTRIBUTE,
    USFM_DIAGNOSTIC_MISSING_ATTRIBUTE,   /* required by stylesheet */
    USFM_DIAGNOSTIC_INVALID_ESCAPE,
    USFM_DIAGNOSTIC_MILESTONE_MISMATCH,  /* sid/eid don't pair */
    USFM_DIAGNOSTIC_UNSUPPORTED_VERSION, /* \usfm 4.0 */
    USFM_DIAGNOSTIC_DUPLICATE_STYLE      /* stylesheet: later wins */
    /* ... */
};

typedef struct USFM_Diagnostic {
    USFM_Span   span;                   /* exact bytes at fault; length
                                           0 for "missing" at offset */
    uint16_t    code;                   /* enum USFM_DiagnosticCode */
    USFM_Marker marker;                 /* context marker, or NONE */
} USFM_Diagnostic;                      /* 12 bytes */

/* Filled by USFM_Tokenize / USFM_TokenizeStylesheet: the library sets
 * every member, allocating buffer through the call's allocator,
 * shrunk so capacity == length. Free: USFM_Alc_Free(alc, buffer,
 * capacity * sizeof(USFM_Diagnostic)). If growing it fails, further
 * diagnostics are counted in `dropped` and err.code is
 * USFM_ERR_NOMEM. Diagnostics are reports, so losing some never
 * changes the result. */
typedef struct USFM_DiagnosticArray {
    USFM_Diagnostic *buffer;
    uint32_t         capacity;          /* elements in buffer */
    uint32_t         length;            /* elements stored */
    uint32_t         dropped;           /* diagnostics that didn't fit */
} USFM_DiagnosticArray;

USFM_API
uint8_t     USFM_DiagnosticCode_GetSeverity(uint16_t code); /* enum
                                           USFM_DiagnosticSeverity */
USFM_API
const char *USFM_DiagnosticCode_GetMessage(uint16_t code); /* static string,
                                           e.g. "unclosed marker" */

/* ---------------------------------------------------------------------
 * Allocator (modeled on yyjson_alc)
 *
 * Contract, for anyone writing an allocator:
 *   - Every size the library requests is a multiple of 8, and every
 *     returned pointer must be 8-byte aligned.
 *   - Frees are SIZED (unlike yyjson): dealloc and resize get the
 *     exact size of the block, as last requested. The library always
 *     knows sizes; this lets arenas free their top block and debug
 *     allocators verify every free.
 *   - resize returning NULL must leave the old block valid. The
 *     library then still frees it with its old size.
 *   - The library never passes NULL to dealloc.
 *
 * Growth and arenas: arrays grow geometrically (x2). In a bump
 * arena, resizing the NEWEST block happens in place; resizing any
 * other block allocates a new one and copies, and the old block
 * stays dead until reset. With x2 growth an array's dead space never
 * exceeds its final size, so the waste is bounded and
 * the USFM_*_GetMaxMemory() functions already include it. The implementation
 * avoids dead space where cheap (e.g. sizing side tables from known
 * counts), but correctness never depends on allocation order.
 * ------------------------------------------------------------------- */
/* Members are deliberately NOT named malloc/realloc/free: a program
 * that defines those as function-like macros (common in debug and
 * leak-tracking setups) would otherwise rewrite alc->malloc(...). */
typedef struct USFM_Alc {
    void *(*alloc)(void *ctx, size_t size);
    void *(*resize)(void *ctx, void *ptr, size_t old_size,
                    size_t new_size);
    void  (*dealloc)(void *ctx, void *ptr, size_t size);
    void *ctx;
} USFM_Alc;

/* Releases a block the library allocated and handed to the caller
 * (the *_alloc text outputs, USFM_Tokenize arrays). Pass the size as
 * documented by the function that returned it: a byte length or an
 * element count times the element size. The library rounds every
 * allocation to round_up(max(size, 1), 8) and applies the same
 * rounding here, so callers always pass the logical size. NULL ptr
 * is a no-op. alc NULL = the default allocator. */
USFM_API
void USFM_Alc_Free(const USFM_Alc *alc, void *ptr, size_t size);

/* ---- Fixed arena: static allocation over a caller buffer ----
 *
 * Bump allocator. resize of the newest block grows (or shrinks) in
 * place; dealloc of the newest block rewinds; other deallocs are no-ops
 * until reset. Its state lives in a small header at the start of
 * `buf`, which must be 8-byte aligned. The USFM_*_GetMaxMemory()
 * functions already include that header, so a buffer of that size parses any
 * input of that length and never fails with USFM_ERR_NOMEM.
 * Returns 1 on success, 0 if buf is NULL, misaligned or too small. */
USFM_API
int    USFM_Alc_InitFixed(USFM_Alc *alc, void *buf, size_t size);
/* O(1): drops everything. Handles allocated from the arena are
 * gone; do not use them or call their free functions afterwards. */
USFM_API
void   USFM_Alc_ResetFixed(USFM_Alc *alc);
/* High-water mark in bytes since init (survives reset), for tuning
 * buffer sizes. */
USFM_API
size_t USFM_Alc_GetFixedUsed(const USFM_Alc *alc);

/* ---- Growable arena (cf. yyjson's dyn allocator) ----
 *
 * Like the fixed arena, but when a chunk runs out it allocates
 * another chunk from `backing` and keeps it. Reset rewinds all
 * chunks WITHOUT freeing them, so steady-state reuse (an LSP that
 * reparses on every keystroke) stops allocating after warm-up.
 * Requests larger than chunk_size get a dedicated chunk sized to fit.
 * The returned USFM_Alc itself is allocated from backing (NULL =
 * libc; an error under USFM_NO_LIBC). */
USFM_API
USFM_Alc *USFM_Alc_NewDyn(const USFM_Alc *backing, size_t chunk_size,
                           USFM_Err *err);
USFM_API
void      USFM_Alc_ResetDyn(USFM_Alc *alc);  /* same rules as fixed */
USFM_API
size_t    USFM_Alc_GetDynUsed(const USFM_Alc *alc);
USFM_API
void      USFM_Alc_FreeDyn(USFM_Alc *alc);   /* returns all chunks */

/* ---------------------------------------------------------------------
 * Layer 1: tokens
 *
 * Lossless: concatenating every token's span reproduces the input
 * exactly (whitespace and newlines are tokens). Asserted in debug.
 * 12 bytes per token. Worst case: USFM_Token_GetMaxCount(len) tokens.
 *
 * Escapes: a backslash followed by \ / | ~ or " is an escape, not a
 * marker (marker names start with a letter or +). Escaped bytes stay
 * inside the TEXT token, never trigger //, | or ~ handling, and set
 * USFM_TF_HAS_ESCAPE on the token. Decode with USFM_DecodeText().
 * ------------------------------------------------------------------- */
enum USFM_TokenKind {
    USFM_TOK_TEXT = 1,
    USFM_TOK_WHITESPACE,                /* spaces/tabs, not newlines */
    USFM_TOK_NEWLINE,
    USFM_TOK_MARKER,                    /* \p  \v  \w  \+w  \qt-s */
    USFM_TOK_MARKER_END,                /* \w*  \+w*  \*  \qt-e */
    USFM_TOK_ARGUMENT,                  /* "1-2a" after \v, "GEN" after \id,
                                           "+" caller after \f */
    USFM_TOK_ATTRIBUTES,                /* "|lemma="x" ..." up to closer;
                                           parsed lazily, see below */
    USFM_TOK_OPTBREAK                   /* "//" */
};

/* level_flags: low 4 bits = number (\q2 -> 2), high 4 bits = flags */
enum {
    USFM_TF_NESTED = 0x10,              /* \+w */
    USFM_TF_MILESTONE_START = 0x20,     /* \qt-s */
    USFM_TF_MILESTONE_END = 0x40,       /* \qt-e */
    USFM_TF_SELF_CLOSED = 0x80,         /* milestone ended with \* */
};

typedef struct USFM_Token {
    USFM_Span   span;                   /* bytes in the input */
    USFM_Marker marker;                 /* USFM_M_NONE for non-markers */
    uint8_t     kind;                   /* enum USFM_TokenKind */
    uint8_t     level_flags;
} USFM_Token;

/* Accessors for packed level_flags. Real exported functions, so every
 * FFI can call them: function-like macros and static inline functions
 * are invisible to FFI. C callers can use the macros at zero cost.
 * The same functions work on USFM_Node (identical packing). */
#define USFM_LEVEL(level_flags) ((uint8_t)((level_flags) & 0x0Fu))
#define USFM_FLAGS(level_flags) ((uint8_t)((level_flags) & 0xF0u))

USFM_API
uint8_t USFM_Token_GetLevel(const USFM_Token *token);  /* \q2 -> 2 */
USFM_API
uint8_t USFM_Token_GetFlags(const USFM_Token *token);  /* USFM_TF_* bits */
USFM_API
int     USFM_Token_HasFlags(const USFM_Token *token, uint32_t flags);

USFM_API
uint32_t USFM_Token_GetMaxCount(uint32_t input_length);   /* = length + 1 */

/* Tokenize everything into one allocated array of exactly *count
 * tokens. Everything is allocated through `alc`.
 * Returns NULL on failure (see err), having freed everything. Empty
 * input returns a valid array with *count == 0.
 * Free: USFM_Alc_Free(alc, tokens, *count * sizeof(USFM_Token)).
 * `diagnostics` is an OUTPUT (see USFM_DiagnosticArray); pass NULL to
 * skip collecting them. Dropped diagnostics follow the handle rule:
 * tokens returned, err.code = USFM_ERR_NOMEM. */
USFM_API
USFM_Token *USFM_Tokenize(const char *input, uint32_t input_length,
                          const USFM_Config *config, const USFM_Alc *alc,
                          uint32_t *count, USFM_DiagnosticArray *diagnostics,
                          USFM_Err *err);

/* ---------------------------------------------------------------------
 * Trees (shared layout for CST and AST)
 *
 * Nodes are stored in preorder. A node's descendants are the
 * contiguous range (i, end). So:
 *     first child  = i + 1            (if i + 1 < node.end)
 *     next sibling = nodes[child].end (if < parent.end)
 *     skip subtree = node.end
 * No child/sibling pointers, cache-friendly, trivially serialisable.
 * 20 bytes per node.
 * ------------------------------------------------------------------- */
typedef struct USFM_Node {
    uint32_t    parent;                 /* USFM_NIL for the root */
    uint32_t    end;                    /* one past last descendant */
    USFM_Span   span;                   /* source bytes covered */
    USFM_Marker marker;
    uint8_t     kind;                   /* USFM_CstKind / USFM_AstKind */
    uint8_t     level_flags;            /* as in USFM_Token */
} USFM_Node;

USFM_API
uint8_t USFM_Node_GetLevel(const USFM_Node *node);
USFM_API
uint8_t USFM_Node_GetFlags(const USFM_Node *node);
USFM_API
int     USFM_Node_HasFlags(const USFM_Node *node, uint32_t flags);

/* ---------------------------------------------------------------------
 * Parsing and conversion: conventions shared by every handle
 *
 * Input is BORROWED by default (zero-copy): it must outlive the
 * handle. USFM_PARSE_COPY_INPUT copies it into the handle instead,
 * for garbage-collected bindings where keeping the source buffer
 * alive is the main failure mode. Spans are offsets, so they are
 * valid against either buffer; *_input() returns the one in use.
 *
 * Every *_parse_* and conversion function:
 *   - comes as a short form (config may be NULL = defaults, libc
 *     allocator) and an _opts form taking flags and an allocator;
 *   - returns NULL if a tree can't be built, having freed everything,
 *     with err filled in. Callers never see a partially built tree,
 *     because the preorder invariants only hold once a build is
 *     complete. Running out of room for diagnostics only drops
 *     diagnostics (handle returned, err.code = USFM_ERR_NOMEM); this
 *     also applies to conversions that copy diagnostics;
 *   - has a matching *_GetMaxMemory(input_length, flags): a fixed
 *     arena of that size (header included) can never run out, so
 *     nothing is ever dropped.
 * All arrays are final once a handle is returned, so pointers from
 * its accessors stay valid until it is freed.
 * ------------------------------------------------------------------- */
typedef struct USFM_Cst USFM_Cst;       /* opaque */
typedef struct USFM_Ast USFM_Ast;       /* opaque */

enum {
    USFM_PARSE_DEFAULT = 0x00,
    USFM_PARSE_COPY_INPUT = 0x01        /* handle owns a copy of input */
};

/* ---------------------------------------------------------------------
 * Layer 2: CST (lossless, for LSPs / editors)
 *
 * Mirrors what was typed: every token belongs to exactly one leaf,
 * trivia included. Malformed input yields ERROR / MISSING nodes
 * rather than failure, so an editor always gets a tree.
 * ------------------------------------------------------------------- */
enum USFM_CstKind {
    USFM_CST_DOCUMENT = 1,
    USFM_CST_MARKER_BLOCK,              /* marker + its content */
    USFM_CST_MARKER, USFM_CST_MARKER_END, USFM_CST_ARGUMENT,
    USFM_CST_ATTRIBUTES, USFM_CST_TEXT, USFM_CST_TRIVIA,
    USFM_CST_ERROR,                     /* unexpected token(s) */
    USFM_CST_MISSING                    /* zero-width, e.g. missing \w* */
};

USFM_API
USFM_Cst *USFM_Cst_Parse(const char *input, uint32_t input_length,
                         const USFM_Config *config, USFM_Err *err);
USFM_API
USFM_Cst *USFM_Cst_ParseOpts(const char *input, uint32_t input_length,
                              const USFM_Config *config, uint32_t flags,
                              const USFM_Alc *alc, USFM_Err *err);
/* Also bounds stylesheet CSTs (USFM_Cst_ParseStylesheet). */
USFM_API
size_t    USFM_Cst_GetMaxMemory(uint32_t input_length, uint32_t flags);
USFM_API
void      USFM_Cst_Free(USFM_Cst *cst);             /* NULL: no-op */

USFM_API
const char *USFM_Cst_GetInput(const USFM_Cst *cst, uint32_t *length);
USFM_API
const USFM_Diagnostic *USFM_Cst_GetDiagnostics(const USFM_Cst *cst,
                                            uint32_t *count);
/* Diagnostics that couldn't be stored (0 unless the parse reported
 * USFM_ERR_NOMEM alongside a returned handle). */
USFM_API
uint32_t USFM_Cst_GetDroppedDiagnosticCount(const USFM_Cst *cst);
USFM_API
const USFM_Node  *USFM_Cst_GetNodes(const USFM_Cst *cst, uint32_t *count);
USFM_API
const USFM_Token *USFM_Cst_GetTokens(const USFM_Cst *cst, uint32_t *count);

/* Tokens covered by a node (binary search on offsets). */
USFM_API
void USFM_Cst_GetNodeTokens(const USFM_Cst *cst, uint32_t node,
                          uint32_t *first_token, uint32_t *token_count);
/* Innermost node at a byte offset (LSP hover / go-to). */
USFM_API
uint32_t USFM_Cst_GetNodeAt(const USFM_Cst *cst, uint32_t offset);

/* ---------------------------------------------------------------------
 * Layer 3: AST (semantic, USX/USJ-shaped)
 *
 * Paragraph markers become containers that close implicitly at the
 * next paragraph-level marker. Chapters and verses are milestones
 * (as in USX 3), because verses routinely cross paragraph
 * boundaries; verse extents are recovered via the verse index.
 * USFM_Ast_Parse parses straight from the input via an internal
 * fixed-size token batch, so no full token array is required. If you
 * need both trees (an LSP), parse a CST and convert: the input is
 * tokenized once and the AST inherits the CST's diagnostics.
 * ------------------------------------------------------------------- */
enum USFM_AstKind {
    USFM_AST_BOOK = 1, USFM_AST_HEADER,     /* \id, \h, \toc ... */
    USFM_AST_CHAPTER, USFM_AST_VERSE,       /* milestones */
    USFM_AST_PARA, USFM_AST_CHAR, USFM_AST_NOTE,
    USFM_AST_MILESTONE, USFM_AST_FIGURE,
    USFM_AST_TABLE, USFM_AST_ROW, USFM_AST_CELL,
    USFM_AST_SIDEBAR, USFM_AST_PERIPHERAL,
    USFM_AST_TEXT, USFM_AST_OPTBREAK
};

typedef struct USFM_VerseRef {
    uint16_t chapter;
    uint16_t verse_first;               /* \v 3-5a -> 3 */
    uint16_t verse_last;                /*         -> 5 */
    uint8_t  segment_first;             /* 'a' -> 1, none -> 0 */
    uint8_t  segment_last;
} USFM_VerseRef;

typedef struct USFM_VerseEntry {
    USFM_VerseRef ref;
    uint32_t node;                      /* the VERSE milestone node */
    uint32_t end_node;                  /* next VERSE/CHAPTER or end */
} USFM_VerseEntry;

/* One per \c in document order. A chapter's verses are contiguous in
 * the verse array: verses[first_verse .. first_verse + verse_count).
 * Verses before the first \c belong to no chapter. */
typedef struct USFM_ChapterEntry {
    uint16_t number;                    /* \c 3 -> 3 */
    uint16_t reserved;
    uint32_t node;                      /* the CHAPTER milestone node */
    uint32_t end_node;                  /* next CHAPTER or end */
    uint32_t first_verse;               /* index into the verse array */
    uint32_t verse_count;
} USFM_ChapterEntry;                    /* 20 bytes */

USFM_API
USFM_Ast *USFM_Ast_Parse(const char *input, uint32_t input_length,
                         const USFM_Config *config, USFM_Err *err);
USFM_API
USFM_Ast *USFM_Ast_ParseOpts(const char *input, uint32_t input_length,
                              const USFM_Config *config, uint32_t flags,
                              const USFM_Alc *alc, USFM_Err *err);
USFM_API
size_t    USFM_Ast_GetMaxMemory(uint32_t input_length, uint32_t flags);
USFM_API
void      USFM_Ast_Free(USFM_Ast *ast);             /* NULL: no-op */

/* CST -> AST without tokenizing again. Copies the CST's diagnostics, then
 * adds AST-level ones (e.g. duplicate verse numbers). */
USFM_API
USFM_Ast *USFM_Cst_ToAst(const USFM_Cst *cst, USFM_Err *err);
USFM_API
USFM_Ast *USFM_Cst_ToAstOpts(const USFM_Cst *cst, const USFM_Alc *alc,
                               USFM_Err *err);

USFM_API
const char *USFM_Ast_GetInput(const USFM_Ast *ast, uint32_t *length);
USFM_API
const USFM_Diagnostic *USFM_Ast_GetDiagnostics(const USFM_Ast *ast,
                                            uint32_t *count);
USFM_API
uint32_t USFM_Ast_GetDroppedDiagnosticCount(const USFM_Ast *ast);
USFM_API
const USFM_Node *USFM_Ast_GetNodes(const USFM_Ast *ast, uint32_t *count);
/* In document order. */
USFM_API
const USFM_ChapterEntry *USFM_Ast_GetChapters(const USFM_Ast *ast,
                                            uint32_t *count);
USFM_API
const USFM_VerseEntry *USFM_Ast_GetVerses(const USFM_Ast *ast,
                                        uint32_t *count);
/* Highest chapter number present. Not the number of chapters:
 * chapters may have gaps or repeat (see USFM_Ast_GetChapters). */
USFM_API
uint32_t    USFM_Ast_GetMaxChapterNumber(const USFM_Ast *ast);
/* "GEN"; NUL-terminated, never NULL ("" if there is no \id). */
USFM_API
const char *USFM_Ast_GetBookCode(const USFM_Ast *ast);
/* Resolved enum USFM_Version: never AUTO after parsing. */
USFM_API
uint8_t     USFM_Ast_GetVersion(const USFM_Ast *ast);

/* O(1) / O(log n) lookups. Return the node index, or USFM_NIL when
 * absent (then *out is untouched). If a chapter number repeats, the
 * first occurrence is found. */
USFM_API
uint32_t USFM_Ast_FindChapter(const USFM_Ast *ast, uint16_t chapter,
                              USFM_ChapterEntry *out);
USFM_API
uint32_t USFM_Ast_FindVerse(const USFM_Ast *ast, uint16_t chapter,
                        uint16_t verse, USFM_VerseEntry *out);

/* ---------------------------------------------------------------------
 * Search (iterator; no callbacks, no allocation)
 * ------------------------------------------------------------------- */
enum USFM_QueryKind {
    USFM_Q_CHAPTER = 1, USFM_Q_VERSE, USFM_Q_MARKER, USFM_Q_TEXT
};
enum USFM_QueryScope {
    USFM_SCOPE_VERSE = 1,               /* report the enclosing verse */
    USFM_SCOPE_PARAGRAPH                /* report the enclosing para */
};

typedef struct USFM_Query {
    uint8_t        kind;                /* enum USFM_QueryKind */
    uint8_t        scope;               /* enum USFM_QueryScope */
    USFM_Marker    marker;              /* USFM_Q_MARKER */
    USFM_VerseRef ref;                 /* USFM_Q_CHAPTER / _VERSE */
    const char    *text;                /* USFM_Q_TEXT, raw bytes */
    uint32_t       text_length;
} USFM_Query;

typedef struct USFM_Match {
    uint32_t         node;              /* node that matched */
    uint32_t         container;         /* enclosing PARA (scope=para) */
    USFM_VerseEntry verse;             /* enclosing verse (scope=verse) */
    USFM_Span        hit;               /* exact bytes matched */
} USFM_Match;

typedef struct USFM_SearchIter {
    const USFM_Ast   *ast;
    const USFM_Query *query;
    uint32_t          cursor;           /* private */
} USFM_SearchIter;

USFM_API
void USFM_SearchIter_Init(USFM_SearchIter *search, const USFM_Ast *ast,
                      const USFM_Query *query);
/* Returns 1 and fills *match, or 0 when exhausted. */
USFM_API
int  USFM_SearchIter_Next(USFM_SearchIter *search, USFM_Match *match);

/* ---------------------------------------------------------------------
 * Verse content iterator
 *
 * Walks the preorder range (entry.node, entry.end_node) and yields
 * the verse's content in reading order, crossing paragraph
 * boundaries. Never recurses: position only moves forward, either
 * by one node or by jumping to a node's `end` to skip a subtree, so
 * the loop is bounded by end_node - node.
 *
 * Defaults (flags == 0):
 *   - yields TEXT only
 *   - skips NOTE subtrees (\f, \fe, \x)
 *   - skips heading-class paragraphs (\s, \ms, \r, \sp ...), which
 *     sit inside the previous verse's range in preorder
 *   - paragraph breaks are not reported
 * ------------------------------------------------------------------- */
enum {
    USFM_VI_INCLUDE_NOTES = 0x01,       /* descend into NOTE subtrees */
    USFM_VI_INCLUDE_HEADINGS = 0x02,    /* descend into heading paras */
    USFM_VI_REPORT_BREAKS = 0x04,       /* yield USFM_VITEM_PARA_BREAK */
    USFM_VI_REPORT_NOTES = 0x08,        /* yield USFM_VITEM_NOTE as one
                                           item even when skipping */
};

enum USFM_VerseItemKind {
    USFM_VITEM_TEXT = 1,                /* text: raw TEXT span */
    USFM_VITEM_PARA_BREAK,              /* node: the new PARA */
    USFM_VITEM_NOTE,                    /* node: the NOTE (anchor) */
    USFM_VITEM_OPTBREAK                 /* "//" */
};

typedef struct USFM_VerseItem {
    uint8_t   kind;                     /* enum USFM_VerseItemKind */
    uint8_t   in_note;                  /* 1 if inside a NOTE subtree */
    uint16_t  reserved;
    uint32_t  node;                     /* node yielded */
    uint32_t  para;                     /* enclosing PARA node */
    uint32_t  style;                    /* innermost CHAR node, or USFM_NIL
                                           (e.g. \wj for words of Jesus) */
    USFM_Span text;                     /* raw span; see USFM_DecodeText */
} USFM_VerseItem;

typedef struct USFM_VerseIter {
    const USFM_Ast *ast;
    uint32_t        position;           /* private */
    uint32_t        end;                /* private: entry.end_node */
    uint32_t        flags;
} USFM_VerseIter;

USFM_API
void USFM_VerseIter_Init(USFM_VerseIter *it, const USFM_Ast *ast,
                          const USFM_VerseEntry *entry, uint32_t flags);
/* Returns 1 and fills *item, or 0 when the verse is exhausted. */
USFM_API
int  USFM_VerseIter_Next(USFM_VerseIter *it, USFM_VerseItem *item);

/* ---------------------------------------------------------------------
 * Chapter and chapter-verse iterators
 *
 * Walk the chapter and verse tables (no tree walking), so each step
 * is O(1) and every loop is bounded by the table length. They nest
 * naturally with the verse content iterator:
 *
 *     USFM_ChapterIter chapters;
 *     USFM_ChapterEntry chapter;
 *     USFM_ChapterIter_Init(&chapters, ast);
 *     while (USFM_ChapterIter_Next(&chapters, &chapter)) {
 *         USFM_ChapterVerseIter verses;
 *         USFM_VerseEntry verse;
 *         USFM_ChapterVerseIter_Init(&verses, ast, &chapter);
 *         while (USFM_ChapterVerseIter_Next(&verses, &verse)) {
 *             ... USFM_VerseIter over verse's content ...
 *         }
 *     }
 * ------------------------------------------------------------------- */
typedef struct USFM_ChapterIter {
    const USFM_Ast *ast;
    uint32_t        position;           /* private */
    uint32_t        end;                /* private */
} USFM_ChapterIter;

/* Every chapter in document order (repeats included). */
USFM_API
void USFM_ChapterIter_Init(USFM_ChapterIter *it, const USFM_Ast *ast);
/* Returns 1 and fills *chapter, or 0 when exhausted. */
USFM_API
int  USFM_ChapterIter_Next(USFM_ChapterIter *it,
                           USFM_ChapterEntry *chapter);

typedef struct USFM_ChapterVerseIter {
    const USFM_Ast *ast;
    uint32_t        position;           /* private */
    uint32_t        end;                /* private */
} USFM_ChapterVerseIter;

/* The verses of one chapter, in document order. Takes an entry from
 * USFM_ChapterIter_Next or USFM_Ast_FindChapter, so a repeated
 * chapter number is never ambiguous. */
USFM_API
void USFM_ChapterVerseIter_Init(USFM_ChapterVerseIter *it,
                                const USFM_Ast *ast,
                                const USFM_ChapterEntry *chapter);
/* Returns 1 and fills *verse, or 0 when the chapter is exhausted. */
USFM_API
int  USFM_ChapterVerseIter_Next(USFM_ChapterVerseIter *it,
                                USFM_VerseEntry *verse);

/* ---------------------------------------------------------------------
 * Stylesheets (.sty)
 *
 *     \Marker w
 *     \Endmarker w*
 *     \StyleType Character
 *     \Attributes lemma ?strong ?srcloc
 *     # comment
 *
 * Same shape as USFM: tokens -> CST (editors) and tokens -> table
 * (lookup). Reuses USFM_Token, USFM_Node and the CST accessors; for
 * stylesheet tokens, USFM_Token.marker holds a USFM_StyKey. Same
 * handle model as USFM: a .sty CST is a USFM_Cst (with sty kinds),
 * and the table is a USFM_Stylesheet, parsed from text or a CST.
 *
 * BOOTSTRAP RULE: nothing in this section may depend on the
 * generated tables, because usfm_codegen.c uses this parser to
 * produce them. Keys and marker names are matched as spans.
 * ------------------------------------------------------------------- */
enum USFM_StyTokenKind {
    USFM_STY_TOK_KEY = 1,               /* \Marker  \FontSize ... */
    USFM_STY_TOK_VALUE,                 /* rest of line, trimmed */
    USFM_STY_TOK_COMMENT,               /* # ... (incl. #! lines) */
    USFM_STY_TOK_WHITESPACE,
    USFM_STY_TOK_NEWLINE
};

typedef uint16_t USFM_StyKey;
enum {
    USFM_STY_KEY_UNKNOWN = 0,           /* preserved, see properties */
    USFM_STY_KEY_MARKER, USFM_STY_KEY_ENDMARKER,
    USFM_STY_KEY_NAME, USFM_STY_KEY_DESCRIPTION,
    USFM_STY_KEY_STYLETYPE, USFM_STY_KEY_TEXTTYPE,
    USFM_STY_KEY_TEXTPROPERTIES, USFM_STY_KEY_OCCURSUNDER,
    USFM_STY_KEY_ATTRIBUTES, USFM_STY_KEY_RANK,
    USFM_STY_KEY_FONTSIZE, USFM_STY_KEY_BOLD, USFM_STY_KEY_ITALIC,
    USFM_STY_KEY_JUSTIFICATION, USFM_STY_KEY_SPACEBEFORE,
    USFM_STY_KEY_SPACEAFTER, USFM_STY_KEY_LEFTMARGIN,
    USFM_STY_KEY_RIGHTMARGIN, USFM_STY_KEY_FIRSTLINEINDENT,
    USFM_STY_KEY_COLOR
    /* ... */
};

/* Same contract as USFM_Tokenize, diagnostics included. */
USFM_API
USFM_Token *USFM_TokenizeStylesheet(const char *input, uint32_t input_length,
                              const USFM_Alc *alc, uint32_t *count,
                              USFM_DiagnosticArray *diagnostics,
                              USFM_Err *err);

/* CST: DOCUMENT > STYLE (one per \Marker block) > PROPERTY > KEY/VALUE.
 * Lossless like the USFM CST; comments and blank lines are trivia
 * attached to the following STYLE so moving a block moves them too.
 * Works with USFM_Cst_GetNodeAt / USFM_Cst_GetNodeTokens unchanged. */
USFM_API
USFM_Cst *USFM_Cst_ParseStylesheet(const char *input, uint32_t input_length,
                             uint32_t flags, const USFM_Alc *alc,
                             USFM_Err *err);
enum USFM_StyCstKind {
    USFM_STY_CST_DOCUMENT = 1, USFM_STY_CST_STYLE,
    USFM_STY_CST_PROPERTY, USFM_STY_CST_KEY, USFM_STY_CST_VALUE,
    USFM_STY_CST_COMMENT, USFM_STY_CST_TRIVIA,
    USFM_STY_CST_ERROR                  /* e.g. property before \Marker */
};

/* Table form. All strings are spans into the stylesheet input. */
enum USFM_TextType {
    USFM_TEXT_TYPE_OTHER = 0, USFM_TEXT_TYPE_VERSE_TEXT,
    USFM_TEXT_TYPE_TITLE, USFM_TEXT_TYPE_SECTION,
    USFM_TEXT_TYPE_NOTE_TEXT, USFM_TEXT_TYPE_CHAPTER_NUMBER,
    USFM_TEXT_TYPE_VERSE_NUMBER
};
enum {
    USFM_STY_F_PUBLISHABLE = 0x01,      /* from \TextProperties */
    USFM_STY_F_VERNACULAR = 0x02,
    USFM_STY_F_NONPUBLISHABLE = 0x04,
    USFM_STY_F_PARAGRAPH = 0x08,
    USFM_STY_F_CROSSREFERENCE = 0x10,
};

typedef struct USFM_StyProperty {
    USFM_Span    key;                   /* without backslash */
    USFM_Span    value;
    USFM_StyKey key_id;
    uint16_t     reserved;
} USFM_StyProperty;

typedef struct USFM_Style {
    USFM_Span marker;                   /* "w" */
    USFM_Span endmarker;                /* "w*", or length 0 */
    USFM_Span name, description;
    USFM_Span occurs_under;             /* raw, space-separated */
    USFM_Span attributes;               /* raw; see USFM_StyAttrIter_Next */
    uint32_t  first_property;           /* every property of this style, */
    uint32_t  property_count;           /* known or not, in file order  */
    uint8_t   style_class;              /* enum USFM_MarkerClass */
    uint8_t   text_type;                /* enum USFM_TextType */
    uint8_t   rank;
    uint8_t   flags;                    /* USFM_STY_F_* */
} USFM_Style;

/* Table form. Lookup uses an open-addressing hash
 * (power-of-two slots, linear probing, probes bounded by slot count)
 * owned by the stylesheet. If a marker is defined twice, the later
 * block wins and a diagnostic is emitted: this is how custom.sty
 * overrides behave. Conventions as for every handle. */
USFM_API
USFM_Stylesheet *USFM_Stylesheet_Parse(const char *input,
                                       uint32_t input_length,
                                       uint32_t flags,
                                       const USFM_Alc *alc,
                                       USFM_Err *err);
/* From a .sty CST, without tokenizing again; copies its diagnostics. */
USFM_API
USFM_Stylesheet *USFM_Cst_ToStylesheet(const USFM_Cst *cst,
                                          const USFM_Alc *alc,
                                          USFM_Err *err);
USFM_API
size_t USFM_Stylesheet_GetMaxMemory(uint32_t input_length, uint32_t flags);
USFM_API
void   USFM_Stylesheet_Free(USFM_Stylesheet *sheet);  /* NULL: no-op */

USFM_API
const char *USFM_Stylesheet_GetInput(const USFM_Stylesheet *sheet,
                                  uint32_t *length);
USFM_API
const USFM_Style *USFM_Stylesheet_GetStyles(const USFM_Stylesheet *sheet,
                                         uint32_t *count);
USFM_API
const USFM_StyProperty *USFM_Stylesheet_GetProperties(
    const USFM_Stylesheet *sheet, uint32_t *count);
USFM_API
const USFM_Diagnostic *USFM_Stylesheet_GetDiagnostics(
    const USFM_Stylesheet *sheet, uint32_t *count);
USFM_API
uint32_t USFM_Stylesheet_GetDroppedDiagnosticCount(
    const USFM_Stylesheet *sheet);

/* Name without backslash ("zfoo"). Returns style index or USFM_NIL. */
USFM_API
uint32_t USFM_Stylesheet_FindStyle(const USFM_Stylesheet *sheet,
                              const char *name, uint32_t name_length);

/* \Attributes lemma ?strong ?srcloc
 * Yields each name with its required/optional flag. Which one is the
 * default attribute follows the spec's rule (verify against 3.1). */
typedef struct USFM_StyAttrIter {
    const char *input;
    uint32_t    position;
    uint32_t    end;
} USFM_StyAttrIter;

USFM_API
void USFM_StyAttrIter_Init(USFM_StyAttrIter *it,
                             const USFM_Stylesheet *sheet,
                             const USFM_Style *style);
USFM_API
int  USFM_StyAttrIter_Next(USFM_StyAttrIter *it, USFM_Span *name,
                        int *optional);

/* ---------------------------------------------------------------------
 * Generated tables (written by usfm_codegen.c; do not edit by hand)
 *
 * codegen parses the official usfm.sty of every supported version
 * with the API above and merges them into one table: each standard
 * marker appears once with a bitmask of the versions defining it,
 * plus a perfect hash over names. Lookup is then: perfect hash for
 * standard markers (checked against config->version), falling back
 * to USFM_Stylesheet_FindStyle on config->custom.
 *
 * Compile codegen with USFM_CODEGEN_BOOTSTRAP so a stale or empty
 * generated block can never break the build that regenerates it.
 * ------------------------------------------------------------------- */
#define USFM_VERSION_BIT(v) (1u << (v))   /* v: enum USFM_Version */

typedef struct USFM_StdMarker {
    const char *name;
    uint8_t     name_length;
    uint8_t     style_class;            /* enum USFM_MarkerClass */
    uint8_t     text_type;              /* enum USFM_TextType */
    uint8_t     version_mask;           /* USFM_VERSION_BIT(...) | ... */
    const char *default_attribute;      /* or NULL */
    uint8_t     default_attribute_length;
    uint8_t     flags;                  /* USFM_STY_F_* */
    uint16_t    reserved;
} USFM_StdMarker;

#ifndef USFM_CODEGEN_BOOTSTRAP
/* BEGIN GENERATED */
/* extern const USFM_StdMarker usfm_std_markers[]; ... perfect hash */
/* END GENERATED */
#endif

/* ---------------------------------------------------------------------
 * Mutable AST (cf. yyjson mut_doc) + USFM writer
 *
 * For building USFM from USX/USJ or other sources, and for editing.
 * Same node kinds and milestone model as the AST, but nodes are
 * linked (parent/first/last/next) so they can be inserted anywhere,
 * at the cost of preorder's cache-friendliness.
 *
 * Memory: the node and attribute arrays grow (resize), which is why
 * the API hands out u32 indices, never pointers to keep. Pointers
 * from USFM_MutAst_GetNode / _attr are valid only until the next
 * mutation. The copied-string pool uses chunks that NEVER move,
 * because USFM_Str holds raw pointers. In a fixed arena, growth
 * leaves bounded dead space (see the allocator section); calling
 * USFM_MutAst_Reserve first avoids it entirely.
 *
 * Errors are sticky (like ferror). Each builder call allocates
 * before it links anything, so every mutation is all-or-nothing: a
 * failing call returns USFM_NIL, records the status plus (for data
 * problems) a diagnostic, and every later call is a no-op. Build the
 * whole tree, check USFM_MutAst_GetStatus once at the end. After
 * USFM_ERR_NOMEM the tree is a valid but incomplete tree:
 * USFM_MutAst_Free is safe, and USFM_MutAst_Write refuses to write and
 * returns the sticky status. Bad nesting from foreign input (a para
 * inside a char) is a diagnostic, not an assertion: it's data, not a
 * programmer error. There is no source text, so these diagnostics
 * point at nodes (USFM_MutDiagnostic) rather than byte spans.
 *
 * Text passed in is PLAIN text (as in USX/USJ). The writer escapes
 * it (\ -> \\, // -> \/\/, | -> \| inside char markers ...), so this
 * is the inverse of USFM_DecodeText.
 * ------------------------------------------------------------------- */
typedef struct USFM_Str {               /* not NUL-terminated */
    const char *ptr;
    uint32_t    length;
} USFM_Str;

typedef struct USFM_MutNode {
    uint32_t    parent;
    uint32_t    first_child;
    uint32_t    last_child;             /* O(1) append */
    uint32_t    next_sibling;
    uint32_t    first_attr;             /* attr index, or USFM_NIL */
    USFM_Str    value;                  /* TEXT content, or the number /
                                           code / caller argument */
    USFM_Marker marker;
    uint8_t     kind;                   /* enum USFM_AstKind */
    uint8_t     level_flags;
} USFM_MutNode;

typedef struct USFM_MutAttr {
    USFM_Str key;                       /* length 0 -> default attribute */
    USFM_Str value;
    uint32_t next;                      /* next attr on node, USFM_NIL */
} USFM_MutAttr;

typedef struct USFM_MutDiagnostic {
    uint32_t    node;                   /* node involved, or USFM_NIL */
    uint16_t    code;                   /* enum USFM_DiagnosticCode */
    USFM_Marker marker;                 /* context marker, or NONE */
} USFM_MutDiagnostic;                  /* 8 bytes */

typedef struct USFM_MutAst USFM_MutAst;           /* opaque */

USFM_API
USFM_MutAst *USFM_MutAst_New(const USFM_Alc *alc, USFM_Err *err);
/* AST -> mutable AST, for editing an existing file. Independent of
 * the source like every conversion: if the AST owns a copy of its
 * input, the strings are copied into the string pool; if the input
 * is borrowed, they borrow it too (so it must outlive the result).
 * Parse diagnostics are NOT carried over: they are byte spans into
 * source text, and a mutable AST has no source text to point into. */
USFM_API
USFM_MutAst *USFM_Ast_ToMut(const USFM_Ast *ast, const USFM_Alc *alc,
                              USFM_Err *err);
/* Mutable -> immutable AST ("freeze", cf. yyjson_mut_doc_imut_copy),
 * to use search, the verse iterator and the fast preorder layout.
 * Immutable nodes are spans into USFM text, so freezing writes the
 * tree as USFM into a buffer the new AST owns, then parses it:
 * USFM_MutAst_ToAst(m) == USFM_Ast_Parse(USFM_MutAst_Write(m)) by
 * construction, with spans that point at real USFM. Diagnostics
 * come from that parse. Refuses (NULL) if a sticky error is set. */
USFM_API
USFM_Ast *USFM_MutAst_ToAst(const USFM_MutAst *mut,
                          const USFM_Config *config,
                          const USFM_Alc *alc, USFM_Err *err);
USFM_API
void          USFM_MutAst_Free(USFM_MutAst *mut);   /* NULL: no-op */

/* Pre-size the pools (optional; avoids growth entirely). */
USFM_API
USFM_Status USFM_MutAst_Reserve(USFM_MutAst *mut, uint32_t nodes,
                                 uint32_t attrs, uint32_t string_bytes);

USFM_API
USFM_Status USFM_MutAst_GetStatus(const USFM_MutAst *mut);  /* sticky */
USFM_API
uint32_t    USFM_MutAst_GetRoot(const USFM_MutAst *mut);  /* BOOK node */
USFM_API
const USFM_MutNode *USFM_MutAst_GetNode(const USFM_MutAst *mut,
                                       uint32_t node);
USFM_API
const USFM_MutAttr *USFM_MutAst_GetAttr(const USFM_MutAst *mut,
                                       uint32_t attr);
USFM_API
const USFM_MutDiagnostic *USFM_MutAst_GetDiagnostics(
    const USFM_MutAst *mut, uint32_t *count);
/* A diagnostic that can't be stored is counted here instead. The
 * failing builder call still sets the sticky status as usual. */
USFM_API
uint32_t USFM_MutAst_GetDroppedDiagnosticCount(const USFM_MutAst *mut);

/* USX style / USJ marker strings -> id + level: "q2" -> (Q, 2).
 * Checks custom markers from config too. Returns 0 if unknown. */
USFM_API
int USFM_Marker_Lookup(const char *name, uint32_t name_length,
                       const USFM_Config *config,
                       USFM_Marker *marker, uint8_t *level);

/* Strings: the plain functions BORROW caller memory (zero-copy; it
 * must outlive the tree). USFM_MUT_COPY copies into the string pool,
 * for transient buffers such as a streaming XML/JSON parser. */
enum {
    USFM_MUT_COPY = 0x01,
};

/* Construction. Each appends to `parent` and returns the new node. */
USFM_API
uint32_t USFM_MutAst_AddBook(USFM_MutAst *mut, USFM_Str code, uint32_t flags);
USFM_API
uint32_t USFM_MutAst_AddPara(USFM_MutAst *mut, uint32_t parent,
                       USFM_Marker marker, uint8_t level);
USFM_API
uint32_t USFM_MutAst_AddChapter(USFM_MutAst *mut, uint32_t parent,
                          USFM_Str number, uint32_t flags);
USFM_API
uint32_t USFM_MutAst_AddVerse(USFM_MutAst *mut, uint32_t parent,
                        USFM_Str number, uint32_t flags); /* "3-5a" */
USFM_API
uint32_t USFM_MutAst_AddChar(USFM_MutAst *mut, uint32_t parent,
                       USFM_Marker marker, uint8_t level);
USFM_API
uint32_t USFM_MutAst_AddNote(USFM_MutAst *mut, uint32_t parent,
                       USFM_Marker marker, USFM_Str caller,
                       uint32_t flags);
USFM_API
uint32_t USFM_MutAst_AddMilestone(USFM_MutAst *mut, uint32_t parent,
                            USFM_Marker marker, uint8_t level_flags);
USFM_API
uint32_t USFM_MutAst_AddText(USFM_MutAst *mut, uint32_t parent,
                       USFM_Str text, uint32_t flags);
/* Escape hatch for everything else (tables, sidebars, periph ...). */
USFM_API
uint32_t USFM_MutAst_AddNode(USFM_MutAst *mut, uint32_t parent,
                           uint8_t kind, USFM_Marker marker,
                           uint8_t level_flags);
/* Returns the new attribute index, or USFM_NIL. */
USFM_API
uint32_t USFM_MutAst_AddAttr(USFM_MutAst *mut, uint32_t node,
                           USFM_Str key, USFM_Str value, uint32_t flags);

/* Editing. */
USFM_API
void USFM_MutAst_InsertAfter(USFM_MutAst *mut, uint32_t sibling,
                           uint32_t node);
USFM_API
void USFM_MutAst_Detach(USFM_MutAst *mut, uint32_t node); /* node stays
                                           in the pool, unreachable */

/* Writer: mutable AST -> USFM. Layout matches USFM_Normalize with
 * USFM_NORM_DEFAULT, so write(from_ast(parse(x))) == normalize(x).
 * Iterative via parent/next_sibling links: needs no stack at all.
 * Nested char markers get \+ automatically for USFM 2+.
 * Pass out == NULL to measure: *out_length gets the exact size.
 * Returns the sticky status instead of writing if one is set. */
USFM_API
USFM_Status USFM_MutAst_Write(const USFM_MutAst *mut,
                           const USFM_Config *config,
                           char *out, uint32_t capacity,
                           uint32_t *out_length);
/* Allocating variant. Not NUL-terminated.
 * Free: USFM_Alc_Free(alc, text, *out_length). */
USFM_API
char *USFM_MutAst_WriteAlloc(const USFM_MutAst *mut,
                           const USFM_Config *config,
                           const USFM_Alc *alc, uint32_t *out_length,
                           USFM_Err *err);

/* ---------------------------------------------------------------------
 * Zero-copy helpers
 * ------------------------------------------------------------------- */

/* Attributes are stored as one raw span and parsed on demand.
 *
 * Three source forms, one output shape:
 *   3.x named:    \w grace|lemma="grace" strong="H2580"\w*
 *   3.x default:  \w grace|grace\w*          (key omitted -> lemma)
 *   2.x \fig:     \fig DESC|FILE|SIZE|LOC|COPY|CAP|REF\fig*
 *                 (positional; field order to verify against 2.x spec)
 *
 * Every attribute comes out with a `name` id, so consumers never
 * care which form was written. For 2.x \fig, fields are reported
 * under their 3.x names: DESC -> alt, FILE -> src, and so on. CAP is
 * not an attribute: the AST exposes it as the figure's text content,
 * exactly where 3.x puts the caption, so both versions look alike.
 */
typedef uint16_t USFM_AttrName;
enum {
    USFM_ATTR_NONE = 0,                 /* not a known name: see key
                                           (e.g. x-custom attributes) */
    USFM_ATTR_LEMMA, USFM_ATTR_STRONG, USFM_ATTR_SRCLOC,  /* \w */
    USFM_ATTR_GLOSS,                                      /* \rb */
    USFM_ATTR_ALT, USFM_ATTR_SRC, USFM_ATTR_SIZE,         /* \fig */
    USFM_ATTR_LOC, USFM_ATTR_COPY, USFM_ATTR_REF,
    USFM_ATTR_LINK_HREF, USFM_ATTR_LINK_TITLE, USFM_ATTR_LINK_ID,
    USFM_ATTR_SID, USFM_ATTR_EID, USFM_ATTR_WHO           /* milestones */
    /* ... generated from stylesheets by codegen */
};

enum {
    USFM_ATTR_F_DEFAULT = 0x01,         /* key omitted; name is the
                                           marker's default attribute */
    USFM_ATTR_F_POSITIONAL = 0x02,      /* 2.x field; no key in source */
    USFM_ATTR_F_ESCAPED = 0x04,         /* value needs USFM_DecodeText */
};

typedef struct USFM_Attr {
    USFM_Span      key;                 /* source bytes; length 0 when
                                           DEFAULT or POSITIONAL */
    USFM_Span      value;               /* raw, quotes excluded */
    USFM_AttrName name;                /* always set if known */
    uint8_t        flags;               /* USFM_ATTR_F_* */
    uint8_t        reserved;
} USFM_Attr;                            /* 20 bytes */

typedef struct USFM_AttrIter {
    const char    *input;
    USFM_Span      content;             /* private: 2.x \fig DESC */
    uint32_t       position;            /* private */
    uint32_t       end;                 /* private */
    uint16_t       field;               /* private: positional index */
    USFM_Marker    marker;
    uint8_t        version;             /* resolved enum USFM_Version */
    uint8_t        reserved;
    const USFM_Config *config;          /* custom default attributes */
} USFM_AttrIter;

/* `content` is the marker's text before the first |. Only 2.x \fig
 * reads it (DESC); other forms ignore it. `version` must be a
 * resolved version, e.g. USFM_Ast_GetVersion(ast), never
 * USFM_VERSION_AUTO. */
USFM_API
void USFM_AttrIter_Init(USFM_AttrIter *it, const char *input,
                         USFM_Marker marker, uint8_t version,
                         const USFM_Config *config,
                         USFM_Span content, USFM_Span attributes);
USFM_API
int  USFM_AttrIter_Next(USFM_AttrIter *it, USFM_Attr *out);

/* Copying text: the deliberate exceptions to zero-copy.
 *
 * Two different operations, two different output languages:
 *   USFM_DecodeText  one span   -> plain text (NOT valid USFM)
 *   USFM_Normalize    whole file -> canonical USFM (safe to save)
 *
 * Each has a caller-buffer form (zero allocation; out == NULL or a
 * short buffer reports the size needed) and an _alloc form whose
 * result is freed with USFM_Alc_Free(alc, ptr, *out_length). Allocated
 * results are not NUL-terminated either: pass (ptr, length) on.
 */

/* ---- Decode: span -> plain text, for use outside USFM ----
 *
 * TEXT spans (and attribute values) are raw source. The cooked
 * string can differ because of escapes, ~, // and whitespace
 * collapsing. Tokenizing records this per token, so the fast path
 * stays zero-copy:
 *
 *     if (!(node.level_flags & USFM_TEXT_FLAGS_COOK)) use span as-is;
 *     else USFM_DecodeText(...);
 *
 * These flags reuse the high bits of level_flags on TEXT tokens and
 * TEXT nodes only (marker flags never apply to text). */
enum {
    USFM_TF_HAS_ESCAPE = 0x10,          /* contains \\ \/ \| \~ \" */
    USFM_TF_HAS_NBSP = 0x20,            /* contains ~ */
    USFM_TF_HAS_LOOSE_WS = 0x40,        /* newline or space run */
    USFM_TEXT_FLAGS_COOK = 0x70,
};

/* What USFM_DecodeText should do. Escapes are always decoded. */
enum {
    USFM_DECODE_NBSP = 0x01,            /* ~  -> U+00A0 (2 bytes UTF-8) */
    USFM_DECODE_COLLAPSE_WS = 0x02,     /* newline/space runs -> ' ' */
    USFM_DECODE_DROP_OPTBREAK = 0x04,   /* remove //  (else -> '\n')  */
    USFM_DECODE_DEFAULT = 0x07,
};

/* Worst-case output size: ~ expands 1 -> 2 bytes. */
USFM_API
uint32_t USFM_GetMaxDecodedLength(USFM_Span text);    /* = 2 * length */

/* Decodes `text` into `out`. Never NUL-terminates. Returns the full
 * cooked length; if that exceeds capacity, only `capacity` bytes are
 * written and the caller can retry with a larger buffer.
 * Example: "https:\/\/example.org"  ->  "https://example.org" */
USFM_API
uint32_t USFM_DecodeText(const char *input, USFM_Span text,
                          uint32_t flags, char *out, uint32_t capacity);
USFM_API
char    *USFM_DecodeTextAlloc(const char *input, USFM_Span text,
                                uint32_t flags, const USFM_Alc *alc,
                                uint32_t *out_length, USFM_Err *err);

/* ---- Normalize: whole document -> canonical USFM, for saving ----
 *
 * Output is always valid USFM that parses to the same AST as the
 * input: escapes, ~ and // are preserved verbatim; only layout is
 * canonicalised. Tokenizes in small batches internally, so no token
 * array or tree is required. Idempotent: normalize(normalize(x)) ==
 * normalize(x) (asserted in tests).
 */
enum {
    USFM_NORM_LF = 0x01,                /* CRLF / CR -> LF */
    USFM_NORM_COLLAPSE_WS = 0x02,       /* space runs -> one space */
    USFM_NORM_TRIM_TRAILING = 0x04,     /* no trailing ws on lines */
    USFM_NORM_PARA_LINES = 0x08,        /* newline before each
                                           paragraph-level marker and
                                           \c / \v; none elsewhere */
    USFM_NORM_FINAL_NEWLINE = 0x10,     /* exactly one at end of file */
    USFM_NORM_DEFAULT = 0x1F,
};

/* Worst case: a newline inserted before every 2-byte marker. */
USFM_API
uint32_t USFM_GetMaxNormalizedLength(uint32_t input_length); /* = 2*len + 1 */

USFM_API
USFM_Status USFM_Normalize(const char *input, uint32_t input_length,
                           const USFM_Config *config, uint32_t flags,
                           char *out, uint32_t capacity,
                           uint32_t *out_length);
USFM_API
char *USFM_NormalizeAlloc(const char *input, uint32_t input_length,
                           const USFM_Config *config, uint32_t flags,
                           const USFM_Alc *alc, uint32_t *out_length,
                           USFM_Err *err);

/* ---------------------------------------------------------------------
 * ABI guarantees
 *
 * Handles are opaque, so their layout is not part of the
 * ABI. The value structs below are: FFI bindings mirror them by hand,
 * so a size change must break the build, not the bindings.
 * (C99 has no _Static_assert; a negative array size fails instead.)
 * All public structs use fixed-width fields only: no bitfields, no
 * bool, no enums as field types, so every FFI sees the same layout.
 * ------------------------------------------------------------------- */
#define USFM_STATIC_ASSERT(name, cond) \
    typedef char usfm_static_assert_##name[(cond) ? 1 : -1]
USFM_STATIC_ASSERT(span_size,       sizeof(USFM_Span) == 8);
USFM_STATIC_ASSERT(token_size,      sizeof(USFM_Token) == 12);
USFM_STATIC_ASSERT(node_size,       sizeof(USFM_Node) == 20);
USFM_STATIC_ASSERT(diagnostic_size, sizeof(USFM_Diagnostic) == 12);
USFM_STATIC_ASSERT(attr_size,       sizeof(USFM_Attr) == 20);
USFM_STATIC_ASSERT(mut_diag_size,   sizeof(USFM_MutDiagnostic) == 8);
USFM_STATIC_ASSERT(chapter_size,    sizeof(USFM_ChapterEntry) == 20);

#ifdef __cplusplus
}
#endif
#endif /* USFM_H */

#ifdef USFM_IMPLEMENTATION
/* ---------------------------------------------------------------------
 * Internal: NOT public API
 *
 * Names follow the public scheme for now; they will take the
 * internal-helper prefix once that convention is decided.
 *
 * Streaming tokenization with ZERO allocation. Every public entry
 * point that reads USFM (USFM_Tokenize, the CST and AST parsers,
 * normalize) runs on this, pulling tokens in small fixed batches.
 * When the fixed diagnostic batch is full, tokenizing carries on and
 * extra diagnostics are only counted in `dropped`.
 * ------------------------------------------------------------------- */
typedef struct USFM_TokenState {
    const char        *input;
    uint32_t           input_length;
    uint32_t           position;
    uint32_t           mode;            /* context: marker args, attrs */
    const USFM_Config *config;
} USFM_TokenState;

static void     USFM_Token_Init(USFM_TokenState *state, const char *input,
                                uint32_t input_length,
                                const USFM_Config *config);
/* Fills up to capacity tokens. Returns count; 0 means end of input. */
static uint32_t USFM_Token_Next(USFM_TokenState *state, USFM_Token *out,
                                uint32_t capacity,
                                USFM_DiagnosticArray *diagnostics);

/* implementation goes here */
#endif

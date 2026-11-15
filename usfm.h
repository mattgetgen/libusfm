/* usmf - alpha - USFM Parser
   by Matthew Getgen

   SECURITY

   FEATURE OVERVIEW

   COMPILING & LINKING

   API

   EXAMPLE USAGE

   VERSION HISTORY

   TODO

   LICENSE
     MIT License

     Copyright (c) 2026 Matthew Getgen

     Permission is hereby granted, free of charge, to any person obtaining a copy
     of this software and associated documentation files (the “Software”), to deal
     in the Software without restriction, including without limitation the rights
     to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
     copies of the Software, and to permit persons to whom the Software is
     furnished to do so, subject to the following conditions:

     The above copyright notice and this permission notice shall be included in all
     copies or substantial portions of the Software.

     THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
     IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
     FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
     AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
     LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
     OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
     SOFTWARE.
 */

#ifndef USFM_H
#define USFM_H

#define USFM_VERSION_MAJOR 0
#define USFM_VERSION_MINOR 1
#define USFM_VERSION_PATCH 0
#define USFM_VERSION "0.1.0-dev"

#include <stdint.h>
#include <stddef.h>
#ifndef USFM_ASSERT
#include <assert.h>
#define USFM_ASSERT(x) assert(x)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------
 * Overview
 *
 * Single-header USFM parser. C99, compiles as C++.
 * In exactly one translation unit:
 *     #define USFM_IMPLEMENTATION
 *     #include "usfm.h"
 *
 *     USFM_Err err;
 *     USFM_Ast *ast = USFM_Ast_Parse(src, len, NULL, &err);
 *     if (!ast) { ... err.code, err.msg, err.pos ... }
 *     ...
 *     USFM_Ast_Free(ast);
 *
 * Ground rules (NASA Power of 10 + TigerStyle):
 *   - All allocation goes through a pluggable allocator (USFM_Alc).
 *   - No recursion. The parser drives an explicit stack of
 *     USFM_STACK_DEPTH open containers.
 *   - Every loop is bounded by the input length or a constant.
 *   - Trees use u32 indices, not pointers. Sizes are u32.
 *   - Zero-copy: all text is a (offset, length) span into the input.
 *   - Malformed USFM is never an error: it produces diagnostics.
 *
 * Failure semantics
 *   NULL                          -> failed, nothing to free (see err)
 *   non-NULL, err.code == OK      -> complete
 *   non-NULL, err.code == NOMEM   -> tree complete, some diagnostics
 *                                    were dropped
 * ------------------------------------------------------------------- */

/* ---------------------------------------------------------------------
 * Compile-time configuration
 * ------------------------------------------------------------------- */
#ifndef USFM_STACK_DEPTH
#define USFM_STACK_DEPTH 64u            /* max open containers */
#endif
#ifndef USFM_INPUT_MAX
#define USFM_INPUT_MAX 0x7FFFFFFFu      /* 2 GiB - 1 */
#endif
/* Define USFM_ASSERT before including to replace assert().
 * Define USFM_NO_LIBC to drop the default malloc-based allocator. */

#define USFM_NIL 0xFFFFFFFFu            /* "no node" index */

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
    USFM_ERR_DEPTH,                     /* reserved */
    USFM_ERR_INPUT_TOO_LARGE,
    USFM_ERR_INVALID_ARG,
    USFM_ERR_NOMEM,                     /* allocator returned NULL */
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

/* Marker ids. Values are part of the ABI: never renumber or reuse one;
 * append new markers before USFM_M_CUSTOM_BASE. Numbered markers share
 * a base id and carry the number as a level: \q2 -> (USFM_M_Q, 2). */
typedef uint16_t USFM_Marker;
enum {
    USFM_M_NONE = 0,                    /* not a marker */
    USFM_M_UNKNOWN = 1,                 /* name available via span */

    /* Identification */
    USFM_M_ID = 2, USFM_M_USFM = 3,

    /* Chapters and verses */
    USFM_M_C = 4, USFM_M_CA = 5, USFM_M_CP = 6,
    USFM_M_V = 7, USFM_M_VA = 8, USFM_M_VP = 9,

    /* Paragraphs */
    USFM_M_B = 10, USFM_M_CD = 11, USFM_M_CL = 12, USFM_M_CLS = 13,
    USFM_M_D = 14, USFM_M_H = 15, USFM_M_IB = 16, USFM_M_IDE = 17,
    USFM_M_IE = 18, USFM_M_IEX = 19, USFM_M_ILI = 20, USFM_M_IM = 21,
    USFM_M_IMI = 22, USFM_M_IMQ = 23, USFM_M_IMT = 24, USFM_M_IMTE = 25,
    USFM_M_IO = 26, USFM_M_IOT = 27, USFM_M_IP = 28, USFM_M_IPC = 29,
    USFM_M_IPI = 30, USFM_M_IPQ = 31, USFM_M_IPR = 32, USFM_M_IQ = 33,
    USFM_M_IS = 34, USFM_M_LF = 35, USFM_M_LH = 36, USFM_M_LI = 37,
    USFM_M_LIM = 38, USFM_M_LIT = 39, USFM_M_M = 40, USFM_M_MI = 41,
    USFM_M_MR = 42, USFM_M_MS = 43, USFM_M_MT = 44, USFM_M_MTE = 45,
    USFM_M_NB = 46, USFM_M_P = 47, USFM_M_PB = 48, USFM_M_PC = 49,
    USFM_M_PI = 50, USFM_M_PM = 51, USFM_M_PMC = 52, USFM_M_PMO = 53,
    USFM_M_PMR = 54, USFM_M_PO = 55, USFM_M_PR = 56, USFM_M_Q = 57,
    USFM_M_QA = 58, USFM_M_QC = 59, USFM_M_QD = 60, USFM_M_QM = 61,
    USFM_M_QR = 62, USFM_M_R = 63, USFM_M_REM = 64, USFM_M_S = 65,
    USFM_M_SD = 66, USFM_M_SP = 67, USFM_M_SR = 68, USFM_M_STS = 69,
    USFM_M_TOC = 70, USFM_M_TOCA = 71, USFM_M_TR = 72,

    /* Character styles */
    USFM_M_ADD = 73, USFM_M_BD = 74, USFM_M_BDIT = 75, USFM_M_BK = 76,
    USFM_M_DC = 77, USFM_M_EM = 78, USFM_M_FM = 79, USFM_M_FV = 80,
    USFM_M_IOR = 81, USFM_M_IQT = 82, USFM_M_IT = 83, USFM_M_JMP = 84,
    USFM_M_K = 85, USFM_M_LIK = 86, USFM_M_LITL = 87, USFM_M_LIV = 88,
    USFM_M_ND = 89, USFM_M_NO = 90, USFM_M_ORD = 91, USFM_M_PN = 92,
    USFM_M_PNG = 93, USFM_M_QAC = 94, USFM_M_QS = 95, USFM_M_QT = 96,
    USFM_M_RB = 97, USFM_M_REF = 98, USFM_M_RQ = 99, USFM_M_SC = 100,
    USFM_M_SIG = 101, USFM_M_SLS = 102, USFM_M_SUP = 103, USFM_M_TL = 104,
    USFM_M_W = 105, USFM_M_WA = 106, USFM_M_WG = 107, USFM_M_WH = 108,
    USFM_M_WJ = 109,

    /* Notes */
    USFM_M_EF = 110, USFM_M_EFE = 111, USFM_M_EX = 112, USFM_M_F = 113,
    USFM_M_FE = 114, USFM_M_X = 115,

    /* Note content */
    USFM_M_FK = 116, USFM_M_FL = 117, USFM_M_FP = 118, USFM_M_FQ = 119,
    USFM_M_FQA = 120, USFM_M_FR = 121, USFM_M_FT = 122, USFM_M_FW = 123,
    USFM_M_XK = 124, USFM_M_XNT = 125, USFM_M_XO = 126, USFM_M_XOP = 127,
    USFM_M_XOT = 128, USFM_M_XQ = 129, USFM_M_XT = 130, USFM_M_XTA = 131,

    /* Table cells */
    USFM_M_TC = 132, USFM_M_TCC = 133, USFM_M_TCR = 134, USFM_M_TH = 135,
    USFM_M_THC = 136, USFM_M_THR = 137,

    /* Milestones */
    USFM_M_TS = 138,

    /* Figures, peripherals, sidebars, categories */
    USFM_M_CAT = 139, USFM_M_ESB = 140, USFM_M_ESBE = 141, USFM_M_FIG = 142,
    USFM_M_PERIPH = 143,

    /* Deprecated (still found in older files) */
    USFM_M_ADDPN = 144, USFM_M_FDC = 145, USFM_M_FS = 146, USFM_M_PH = 147,
    USFM_M_PHI = 148, USFM_M_PRO = 149, USFM_M_XDC = 150,

    /* Obsolete (removed before 2.1) */
    USFM_M_CONC = 151, USFM_M_COV = 152, USFM_M_GLO = 153, USFM_M_IDX = 154,
    USFM_M_INTRO = 155, USFM_M_MAPS = 156, USFM_M_PREF = 157, USFM_M_PS = 158,
    USFM_M_PSI = 159, USFM_M_PUB = 160, USFM_M_PUBINFO = 161,
    USFM_M_SPINE = 162, USFM_M_WR = 163,

    /* Stylesheet-only (defined in usfm.sty, not the spec) */
    USFM_M_EFM = 164, USFM_M_NDX = 165, USFM_M_XTSEE = 166,
    USFM_M_XTSEEALSO = 167,

    /* Tool-specific */
    USFM_M_ERQ = 168, USFM_M_ERQE = 169, USFM_M_MSE = 170,
    USFM_M_RESTORE = 171, USFM_M_SE = 172,

    /* Drafts (3.1.2 / 3.2 development, not yet released) */
    USFM_M_ILIT = 173, USFM_M_LIST = 174, USFM_M_PL = 175, USFM_M_T = 176,
    USFM_M_TA = 177, USFM_M_TABLE = 178, USFM_M_VID = 179, USFM_M_WL = 180,

    USFM_M_CUSTOM_BASE = 0x8000         /* + stylesheet style index */
};

enum USFM_MarkerClass {
    USFM_CLASS_NONE = 0,
    USFM_CLASS_PARAGRAPH, USFM_CLASS_CHARACTER, USFM_CLASS_NOTE,
    USFM_CLASS_MILESTONE, USFM_CLASS_HEADER, USFM_CLASS_TABLE
};

typedef struct USFM_Stylesheet USFM_Stylesheet;    /* opaque; later milestone */

typedef struct USFM_Config {
    uint8_t version;                    /* enum USFM_Version */
    uint8_t reserved[3];
    const USFM_Stylesheet *custom;      /* ignored in v0.1 */
} USFM_Config;

/* ---------------------------------------------------------------------
 * Diagnostics: problems found in the input. Parsing always continues.
 * ------------------------------------------------------------------- */
enum USFM_DiagnosticSeverity {
    USFM_SEVERITY_NONE = 0,
    USFM_SEVERITY_ERROR,                /* input is invalid USFM */
    USFM_SEVERITY_WARNING,              /* valid but suspicious */
    USFM_SEVERITY_INFO
};

enum USFM_DiagnosticCode {
    USFM_DIAGNOSTIC_NONE = 0,
    USFM_DIAGNOSTIC_UNKNOWN_MARKER = 1,
    USFM_DIAGNOSTIC_UNCLOSED_MARKER = 2,     /* \w without \w* */
    USFM_DIAGNOSTIC_UNEXPECTED_CLOSE = 3,    /* \w* with no open \w */
    USFM_DIAGNOSTIC_INVALID_NESTING = 4,     /* e.g. a cell outside a row */
    USFM_DIAGNOSTIC_INVALID_NUMBER = 5,      /* \v abc, \c 0, \v 5-3 */
    USFM_DIAGNOSTIC_DUPLICATE_NUMBER = 6,
    USFM_DIAGNOSTIC_VERSE_BEFORE_CHAPTER = 7,
    USFM_DIAGNOSTIC_INVALID_BOOK_CODE = 8,   /* \id gen */
    USFM_DIAGNOSTIC_MISSING_CALLER = 9,      /* \f with no caller */
    USFM_DIAGNOSTIC_UNTERMINATED_QUOTE = 10,
    USFM_DIAGNOSTIC_UNKNOWN_ATTRIBUTE = 11,
    USFM_DIAGNOSTIC_MISSING_ATTRIBUTE = 12,
    USFM_DIAGNOSTIC_INVALID_ESCAPE = 13,
    USFM_DIAGNOSTIC_MILESTONE_MISMATCH = 14,
    USFM_DIAGNOSTIC_UNSUPPORTED_VERSION = 15,
    USFM_DIAGNOSTIC_DUPLICATE_STYLE = 16,
    USFM_DIAGNOSTIC_IMPLIED_PARAGRAPH = 17,  /* \v with no paragraph open */
    USFM_DIAGNOSTIC_TOO_DEEP = 18,           /* > USFM_STACK_DEPTH open */
    USFM_DIAGNOSTIC_CODE_COUNT = 19
};

typedef struct USFM_Diagnostic {
    USFM_Span   span;                   /* bytes at fault; length 0 for
                                           "missing" at offset */
    uint16_t    code;                   /* enum USFM_DiagnosticCode */
    USFM_Marker marker;                 /* context marker, or NONE */
} USFM_Diagnostic;

USFM_API uint8_t     USFM_DiagnosticCode_GetSeverity(uint16_t code);
USFM_API const char *USFM_DiagnosticCode_GetMessage(uint16_t code);

/* ---------------------------------------------------------------------
 * Allocator (modeled on yyjson_alc)
 *
 * Every size the library requests is a multiple of 8, and every
 * returned pointer must be 8-byte aligned. Frees are SIZED: dealloc and
 * resize get the exact size last requested. resize returning NULL must
 * leave the old block valid. The library never passes NULL to dealloc.
 * ------------------------------------------------------------------- */
typedef struct USFM_Alc {
    void *(*alloc)(void *ctx, size_t size);
    void *(*resize)(void *ctx, void *ptr, size_t old_size, size_t new_size);
    void  (*dealloc)(void *ctx, void *ptr, size_t size);
    void *ctx;
} USFM_Alc;

/* Releases a block the library allocated and handed out. Pass the
 * logical size; the library applies its rounding. NULL ptr: no-op.
 * alc NULL = the default allocator. */
USFM_API void USFM_Alc_Free(const USFM_Alc *alc, void *ptr, size_t size);

/* ---------------------------------------------------------------------
 * Tokens (internal in v0.1; the type is public because it shares its
 * level_flags packing with USFM_Node)
 * ------------------------------------------------------------------- */
enum USFM_TokenKind {
    USFM_TOK_TEXT = 1,
    USFM_TOK_WHITESPACE,                /* reserved for the CST */
    USFM_TOK_NEWLINE,                   /* reserved for the CST */
    USFM_TOK_MARKER,                    /* \p  \v  \w  \+w  \qt-s */
    USFM_TOK_MARKER_END,                /* \w*  \+w*  \* */
    USFM_TOK_ARGUMENT,                  /* "1-2a" after \v, "GEN" after
                                           \id, "+" caller after \f */
    USFM_TOK_ATTRIBUTES,                /* "|lemma="x" ..." */
    USFM_TOK_OPTBREAK                   /* "//" */
};

/* level_flags: low 4 bits = number (\q2 -> 2), high 4 bits = flags.
 * Marker flags: */
enum {
    USFM_TF_NESTED = 0x10,              /* \+w */
    USFM_TF_MILESTONE_START = 0x20,     /* \qt-s */
    USFM_TF_MILESTONE_END = 0x40,       /* \qt-e */
    USFM_TF_SELF_CLOSED = 0x80          /* milestone ended with \* */
};
/* Text flags (TEXT tokens and TEXT nodes only): */
enum {
    USFM_TF_HAS_ESCAPE = 0x10,          /* contains \\ \/ \| \~ \" */
    USFM_TF_HAS_NBSP = 0x20,            /* contains ~ */
    USFM_TF_HAS_LOOSE_WS = 0x40,        /* contains CR, LF or TAB */
    USFM_TEXT_FLAGS_COOK = 0x70
};
/* Node flags (PARA nodes only): */
enum {
    USFM_NF_IMPLIED = 0x10,             /* inserted by the parser */
    USFM_NF_HEADING = 0x20              /* \s \ms \mt \r ... */
};

#define USFM_LEVEL(level_flags) ((uint8_t)((level_flags) & 0x0Fu))
#define USFM_FLAGS(level_flags) ((uint8_t)((level_flags) & 0xF0u))

typedef struct USFM_Token {
    USFM_Span   span;                   /* bytes in the input */
    USFM_Marker marker;                 /* USFM_M_NONE for non-markers */
    uint8_t     kind;                   /* enum USFM_TokenKind */
    uint8_t     level_flags;
} USFM_Token;

/* ---------------------------------------------------------------------
 * AST (semantic, USX/USJ-shaped)
 *
 * Nodes are stored in preorder. A node's descendants are the
 * contiguous range (i, end). So:
 *     first child  = i + 1            (if i + 1 < node.end)
 *     next sibling = nodes[child].end (if < parent.end)
 *     skip subtree = node.end
 *
 * Paragraph markers are containers that close implicitly at the next
 * paragraph-level marker. Chapters and verses are milestones (as in
 * USX 3); each has its number as an ARGUMENT child. An ARGUMENT is
 * always a node's first child; ATTRIBUTES appear where they were
 * written (last child of a char span, after the ARGUMENT of \periph).
 * ------------------------------------------------------------------- */
typedef struct USFM_Node {
    uint32_t    parent;                 /* USFM_NIL for the root */
    uint32_t    end;                    /* one past last descendant */
    USFM_Span   span;                   /* source bytes covered */
    USFM_Marker marker;
    uint8_t     kind;                   /* enum USFM_AstKind */
    uint8_t     level_flags;            /* see the flag enums above */
} USFM_Node;

USFM_API uint8_t USFM_Node_GetLevel(const USFM_Node *node);
USFM_API uint8_t USFM_Node_GetFlags(const USFM_Node *node);
USFM_API int     USFM_Node_HasFlags(const USFM_Node *node, uint32_t flags);

enum USFM_AstKind {
    USFM_AST_BOOK = 1, USFM_AST_HEADER,     /* \id, \h, \toc ... */
    USFM_AST_CHAPTER, USFM_AST_VERSE,       /* milestones */
    USFM_AST_PARA, USFM_AST_CHAR, USFM_AST_NOTE,
    USFM_AST_MILESTONE, USFM_AST_FIGURE,
    USFM_AST_TABLE, USFM_AST_ROW, USFM_AST_CELL,
    USFM_AST_SIDEBAR, USFM_AST_PERIPHERAL,
    USFM_AST_TEXT, USFM_AST_OPTBREAK,
    USFM_AST_ARGUMENT,                      /* \v 3-5a, \f +, \id GEN */
    USFM_AST_ATTRIBUTES                     /* |lemma="grace", raw */
};

typedef struct USFM_Ast USFM_Ast;       /* opaque */

enum {
    USFM_PARSE_DEFAULT = 0x00,
    USFM_PARSE_COPY_INPUT = 0x01        /* handle owns a copy of input */
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
    uint32_t node;                      /* the VERSE node */
    uint32_t end_node;                  /* next VERSE/CHAPTER, or end */
} USFM_VerseEntry;

/* One per \c in document order. A chapter's verses are
 * verses[first_verse .. first_verse + verse_count). Verses before the
 * first \c belong to no chapter. */
typedef struct USFM_ChapterEntry {
    uint16_t number;                    /* \c 3 -> 3; 0 if invalid */
    uint16_t reserved;
    uint32_t node;                      /* the CHAPTER node */
    uint32_t end_node;                  /* next CHAPTER, or end */
    uint32_t first_verse;
    uint32_t verse_count;
} USFM_ChapterEntry;

USFM_API USFM_Ast *USFM_Ast_Parse(const char *input, uint32_t input_length,
                                  const USFM_Config *config, USFM_Err *err);
USFM_API USFM_Ast *USFM_Ast_ParseOpts(const char *input, uint32_t input_length,
                                      const USFM_Config *config, uint32_t flags,
                                      const USFM_Alc *alc, USFM_Err *err);
USFM_API void      USFM_Ast_Free(USFM_Ast *ast);             /* NULL: no-op */

USFM_API const char *USFM_Ast_GetInput(const USFM_Ast *ast, uint32_t *length);
USFM_API const USFM_Diagnostic *USFM_Ast_GetDiagnostics(const USFM_Ast *ast,
                                                        uint32_t *count);
USFM_API uint32_t USFM_Ast_GetDroppedDiagnosticCount(const USFM_Ast *ast);
USFM_API const USFM_Node *USFM_Ast_GetNodes(const USFM_Ast *ast, uint32_t *count);
USFM_API const USFM_ChapterEntry *USFM_Ast_GetChapters(const USFM_Ast *ast,
                                                       uint32_t *count);
USFM_API const USFM_VerseEntry *USFM_Ast_GetVerses(const USFM_Ast *ast,
                                                   uint32_t *count);
/* Highest chapter number present (not the number of chapters). */
USFM_API uint32_t    USFM_Ast_GetMaxChapterNumber(const USFM_Ast *ast);
/* "GEN"; NUL-terminated, never NULL ("" if there is no \id). */
USFM_API const char *USFM_Ast_GetBookCode(const USFM_Ast *ast);
/* Resolved enum USFM_Version: never AUTO after parsing. */
USFM_API uint8_t     USFM_Ast_GetVersion(const USFM_Ast *ast);

/* Return the node index, or USFM_NIL when absent (*out untouched). A
 * repeated chapter number finds the first occurrence. FindVerse finds
 * the entry whose range contains `verse` (\v 3-5 contains 4). */
USFM_API uint32_t USFM_Ast_FindChapter(const USFM_Ast *ast, uint16_t chapter,
                                       USFM_ChapterEntry *out);
USFM_API uint32_t USFM_Ast_FindVerse(const USFM_Ast *ast, uint16_t chapter,
                                     uint16_t verse, USFM_VerseEntry *out);

/* ---------------------------------------------------------------------
 * Iterators (no callbacks, no allocation)
 * ------------------------------------------------------------------- */
enum {
    USFM_VI_INCLUDE_NOTES = 0x01,       /* descend into NOTE subtrees */
    USFM_VI_INCLUDE_HEADINGS = 0x02,    /* descend into heading paras */
    USFM_VI_REPORT_BREAKS = 0x04,       /* yield USFM_VITEM_PARA_BREAK */
    USFM_VI_REPORT_NOTES = 0x08         /* yield USFM_VITEM_NOTE */
};

enum USFM_VerseItemKind {
    USFM_VITEM_TEXT = 1,                /* text: raw TEXT span */
    USFM_VITEM_PARA_BREAK,              /* node: the new PARA */
    USFM_VITEM_NOTE,                    /* node: the NOTE */
    USFM_VITEM_OPTBREAK                 /* "//" */
};

typedef struct USFM_VerseItem {
    uint8_t   kind;                     /* enum USFM_VerseItemKind */
    uint8_t   in_note;                  /* 1 if inside a NOTE subtree */
    uint16_t  reserved;
    uint32_t  node;                     /* node yielded */
    uint32_t  para;                     /* enclosing PARA, or USFM_NIL */
    uint32_t  style;                    /* innermost CHAR, or USFM_NIL */
    USFM_Span text;                     /* raw span (TEXT, OPTBREAK) */
} USFM_VerseItem;

typedef struct USFM_VerseIter {
    const USFM_Ast *ast;
    uint32_t        position;           /* private */
    uint32_t        end;                /* private */
    uint32_t        flags;
} USFM_VerseIter;

/* Yields the verse's content in reading order, crossing paragraphs.
 * Defaults: TEXT only; skips notes and heading paragraphs. */
USFM_API void USFM_VerseIter_Init(USFM_VerseIter *it, const USFM_Ast *ast,
                                  const USFM_VerseEntry *entry, uint32_t flags);
USFM_API int  USFM_VerseIter_Next(USFM_VerseIter *it, USFM_VerseItem *item);

typedef struct USFM_ChapterIter {
    const USFM_Ast *ast;
    uint32_t        position;           /* private */
    uint32_t        end;                /* private */
} USFM_ChapterIter;

USFM_API void USFM_ChapterIter_Init(USFM_ChapterIter *it, const USFM_Ast *ast);
USFM_API int  USFM_ChapterIter_Next(USFM_ChapterIter *it,
                                    USFM_ChapterEntry *chapter);

typedef struct USFM_ChapterVerseIter {
    const USFM_Ast *ast;
    uint32_t        position;           /* private */
    uint32_t        end;                /* private */
} USFM_ChapterVerseIter;

USFM_API void USFM_ChapterVerseIter_Init(USFM_ChapterVerseIter *it,
                                         const USFM_Ast *ast,
                                         const USFM_ChapterEntry *chapter);
USFM_API int  USFM_ChapterVerseIter_Next(USFM_ChapterVerseIter *it,
                                         USFM_VerseEntry *verse);

/* ---------------------------------------------------------------------
 * ABI guarantees. C99 has no _Static_assert; a negative array size
 * fails the build instead.
 * ------------------------------------------------------------------- */
#define USFM_STATIC_ASSERT(name, cond) \
    typedef char usfm_static_assert_##name[(cond) ? 1 : -1]
USFM_STATIC_ASSERT(span_size,       sizeof(USFM_Span) == 8);
USFM_STATIC_ASSERT(token_size,      sizeof(USFM_Token) == 12);
USFM_STATIC_ASSERT(node_size,       sizeof(USFM_Node) == 20);
USFM_STATIC_ASSERT(diagnostic_size, sizeof(USFM_Diagnostic) == 12);
USFM_STATIC_ASSERT(chapter_size,    sizeof(USFM_ChapterEntry) == 20);
USFM_STATIC_ASSERT(verse_size,      sizeof(USFM_VerseEntry) == 16);

#ifdef __cplusplus
}
#endif
#endif /* USFM_H */

#ifdef USFM_IMPLEMENTATION
#ifndef USFM_IMPLEMENTATION_INCLUDED
#define USFM_IMPLEMENTATION_INCLUDED
/* ---------------------------------------------------------------------
 * Internal: NOT public API.
 *
 * This first part is compiled even under USFM_CODEGEN_BOOTSTRAP,
 * because codegen/codegen.c needs it to generate the tables below.
 * ------------------------------------------------------------------- */
#include <string.h>
#ifndef USFM_NO_LIBC
#include <stdlib.h>
#endif

/* What a marker does to the tree. Decided per row of the marker table,
 * not per marker id: \k is CHAR but \k1 is PARA. */
enum USFM_Role {
    USFM_ROLE_PARA = 1,                 /* \p \q \s: paragraph container */
    USFM_ROLE_HEADER,                   /* \ide \h \toc \usfm \sts */
    USFM_ROLE_ID,                       /* \id */
    USFM_ROLE_CHAPTER,                  /* \c */
    USFM_ROLE_VERSE,                    /* \v */
    USFM_ROLE_CHAR,                     /* \nd \w \add, closed by \X* */
    USFM_ROLE_NOTE,                     /* \f \x \fe \ef \ex \efe */
    USFM_ROLE_NOTE_PART,                /* \fr \ft \xo: closed implicitly */
    USFM_ROLE_ROW,                      /* \tr */
    USFM_ROLE_CELL,                     /* \tc# \th# ... */
    USFM_ROLE_MILESTONE,                /* \ts; also any -s/-e marker */
    USFM_ROLE_SIDEBAR,                  /* \esb */
    USFM_ROLE_SIDEBAR_END,              /* \esbe */
    USFM_ROLE_PERIPH,                   /* \periph */
    USFM_ROLE_FIGURE                    /* \fig */
};

/* Marker properties (USFM_MarkerRow.props). */
enum {
    USFM_MP_ARG_WORD = 0x01,            /* next word is an ARGUMENT */
    USFM_MP_ARG_LINE = 0x02,            /* rest of line up to | is one */
    USFM_MP_HEADING = 0x04              /* heading paragraph */
};

typedef struct USFM_MarkerRow {
    const char *name;
    uint8_t     name_length;
    uint8_t     role;                   /* enum USFM_Role */
    uint8_t     level;                  /* fixed level of a full-name
                                           key such as k1, else 0 */
    uint8_t     props;                  /* USFM_MP_* */
    USFM_Marker marker;
} USFM_MarkerRow;

#define USFM_ROW_NONE 0xFFFFu

/* Byte classes (USFM_ByteClass, generated). */
enum {
    USFM_BC_STOP = 0x01,                /* ends a fast text run:
                                           \ | / ~ CR LF TAB */
    USFM_BC_NAME = 0x02,                /* marker name: A-Z a-z 0-9 - */
    USFM_BC_ALPHA = 0x04,
    USFM_BC_DIGIT = 0x08,
    USFM_BC_SPACE = 0x10,               /* space, tab */
    USFM_BC_NEWLINE = 0x20,             /* CR, LF */
    USFM_BC_ESCAPE = 0x40               /* may follow \ as an escape:
                                           \ / | ~ " */
};

/* Perfect-hash input: first two and last two bytes plus the length.
 * Bytes past the end of a short name are never read. */
static uint32_t usfm__marker_hash(const char *name, uint32_t length)
{
    const uint8_t *s = (const uint8_t *)name;
    uint32_t b0, b1, b2, b3;
    USFM_ASSERT(name != NULL && length > 0);
    b0 = s[0];
    b1 = length > 1 ? s[1] : 0u;
    b2 = length > 2 ? s[length - 2] : 0u;
    b3 = length > 1 ? s[length - 1] : 0u;
    return (b0 | (b1 << 8) | (b2 << 16) | (b3 << 24)) ^ (length * 0x9E3779B1u);
}

/* Multiply-shift reduction: any slot_count, no division. */
static uint32_t usfm__marker_slot(uint32_t hash, uint32_t seed, uint32_t slot_count)
{
    uint32_t mixed = hash * seed;       /* wraps mod 2^32 on purpose */
    return (uint32_t)(((uint64_t)mixed * slot_count) >> 32);
}

#ifndef USFM_CODEGEN_BOOTSTRAP
/* ---------------------------------------------------------------------
 * Generated tables (codegen/codegen.c; do not edit by hand)
 * ------------------------------------------------------------------- */
// USFM_CODEGEN character_class start
/* USFM_CODEGEN_COMMENT --------------------------------------------------- *
 * NOTE: This code is generated via codegen. Please do not modify manually!
 * Last generated: 2026-11-15 04:51:49 GMT
 * Hash: f1a55f81
 * USFM_CODEGEN_COMMENT --------------------------------------------------- */
static const uint8_t USFM_ByteClass[256] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x21, 0x00, 0x00, 0x21, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x10, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x41,
    0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x00, 0x41, 0x00, 0x00, 0x00,
    0x00, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x00, 0x41, 0x00, 0x41, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
// USFM_CODEGEN character_class end

// USFM_CODEGEN marker_table start
/* USFM_CODEGEN_COMMENT --------------------------------------------------- *
 * NOTE: This code is generated via codegen. Please do not modify manually!
 * Last generated: 2026-11-15 04:51:53 GMT
 * Hash: 9d645bb9
 * USFM_CODEGEN_COMMENT --------------------------------------------------- */
#define USFM_MARKER_NAME_MAX 9u
#define USFM_MARKER_HASH_SEED 0xabc60b4cu
#define USFM_MARKER_SLOT_COUNT 1149u
static const USFM_MarkerRow USFM_MarkerRows[186] = {
    { "id", 2, USFM_ROLE_ID, 0, USFM_MP_ARG_WORD, USFM_M_ID },
    { "usfm", 4, USFM_ROLE_HEADER, 0, USFM_MP_ARG_WORD, USFM_M_USFM },
    { "ide", 3, USFM_ROLE_HEADER, 0, 0, USFM_M_IDE },
    { "h", 1, USFM_ROLE_HEADER, 0, 0, USFM_M_H },
    { "toc", 3, USFM_ROLE_HEADER, 0, 0, USFM_M_TOC },
    { "toca", 4, USFM_ROLE_HEADER, 0, 0, USFM_M_TOCA },
    { "sts", 3, USFM_ROLE_HEADER, 0, 0, USFM_M_STS },
    { "c", 1, USFM_ROLE_CHAPTER, 0, USFM_MP_ARG_WORD, USFM_M_C },
    { "ca", 2, USFM_ROLE_CHAR, 0, 0, USFM_M_CA },
    { "cp", 2, USFM_ROLE_PARA, 0, 0, USFM_M_CP },
    { "v", 1, USFM_ROLE_VERSE, 0, USFM_MP_ARG_WORD, USFM_M_V },
    { "va", 2, USFM_ROLE_CHAR, 0, 0, USFM_M_VA },
    { "vp", 2, USFM_ROLE_CHAR, 0, 0, USFM_M_VP },
    { "b", 1, USFM_ROLE_PARA, 0, 0, USFM_M_B },
    { "cd", 2, USFM_ROLE_PARA, 0, USFM_MP_HEADING, USFM_M_CD },
    { "cl", 2, USFM_ROLE_PARA, 0, USFM_MP_HEADING, USFM_M_CL },
    { "cls", 3, USFM_ROLE_PARA, 0, 0, USFM_M_CLS },
    { "d", 1, USFM_ROLE_PARA, 0, 0, USFM_M_D },
    { "ib", 2, USFM_ROLE_PARA, 0, 0, USFM_M_IB },
    { "ie", 2, USFM_ROLE_PARA, 0, 0, USFM_M_IE },
    { "iex", 3, USFM_ROLE_PARA, 0, 0, USFM_M_IEX },
    { "ili", 3, USFM_ROLE_PARA, 0, 0, USFM_M_ILI },
    { "im", 2, USFM_ROLE_PARA, 0, 0, USFM_M_IM },
    { "imi", 3, USFM_ROLE_PARA, 0, 0, USFM_M_IMI },
    { "imq", 3, USFM_ROLE_PARA, 0, 0, USFM_M_IMQ },
    { "imt", 3, USFM_ROLE_PARA, 0, USFM_MP_HEADING, USFM_M_IMT },
    { "imte", 4, USFM_ROLE_PARA, 0, USFM_MP_HEADING, USFM_M_IMTE },
    { "io", 2, USFM_ROLE_PARA, 0, 0, USFM_M_IO },
    { "iot", 3, USFM_ROLE_PARA, 0, USFM_MP_HEADING, USFM_M_IOT },
    { "ip", 2, USFM_ROLE_PARA, 0, 0, USFM_M_IP },
    { "ipc", 3, USFM_ROLE_PARA, 0, 0, USFM_M_IPC },
    { "ipi", 3, USFM_ROLE_PARA, 0, 0, USFM_M_IPI },
    { "ipq", 3, USFM_ROLE_PARA, 0, 0, USFM_M_IPQ },
    { "ipr", 3, USFM_ROLE_PARA, 0, 0, USFM_M_IPR },
    { "iq", 2, USFM_ROLE_PARA, 0, 0, USFM_M_IQ },
    { "is", 2, USFM_ROLE_PARA, 0, USFM_MP_HEADING, USFM_M_IS },
    { "k1", 2, USFM_ROLE_PARA, 1, 0, USFM_M_K },
    { "k2", 2, USFM_ROLE_PARA, 2, 0, USFM_M_K },
    { "lf", 2, USFM_ROLE_PARA, 0, 0, USFM_M_LF },
    { "lh", 2, USFM_ROLE_PARA, 0, 0, USFM_M_LH },
    { "li", 2, USFM_ROLE_PARA, 0, 0, USFM_M_LI },
    { "lim", 3, USFM_ROLE_PARA, 0, 0, USFM_M_LIM },
    { "lit", 3, USFM_ROLE_PARA, 0, 0, USFM_M_LIT },
    { "m", 1, USFM_ROLE_PARA, 0, 0, USFM_M_M },
    { "mi", 2, USFM_ROLE_PARA, 0, 0, USFM_M_MI },
    { "mr", 2, USFM_ROLE_PARA, 0, USFM_MP_HEADING, USFM_M_MR },
    { "ms", 2, USFM_ROLE_PARA, 0, USFM_MP_HEADING, USFM_M_MS },
    { "mt", 2, USFM_ROLE_PARA, 0, USFM_MP_HEADING, USFM_M_MT },
    { "mte", 3, USFM_ROLE_PARA, 0, USFM_MP_HEADING, USFM_M_MTE },
    { "nb", 2, USFM_ROLE_PARA, 0, 0, USFM_M_NB },
    { "p", 1, USFM_ROLE_PARA, 0, 0, USFM_M_P },
    { "pb", 2, USFM_ROLE_PARA, 0, 0, USFM_M_PB },
    { "pc", 2, USFM_ROLE_PARA, 0, 0, USFM_M_PC },
    { "pi", 2, USFM_ROLE_PARA, 0, 0, USFM_M_PI },
    { "pm", 2, USFM_ROLE_PARA, 0, 0, USFM_M_PM },
    { "pmc", 3, USFM_ROLE_PARA, 0, 0, USFM_M_PMC },
    { "pmo", 3, USFM_ROLE_PARA, 0, 0, USFM_M_PMO },
    { "pmr", 3, USFM_ROLE_PARA, 0, 0, USFM_M_PMR },
    { "po", 2, USFM_ROLE_PARA, 0, 0, USFM_M_PO },
    { "pr", 2, USFM_ROLE_PARA, 0, 0, USFM_M_PR },
    { "q", 1, USFM_ROLE_PARA, 0, 0, USFM_M_Q },
    { "qa", 2, USFM_ROLE_PARA, 0, USFM_MP_HEADING, USFM_M_QA },
    { "qc", 2, USFM_ROLE_PARA, 0, 0, USFM_M_QC },
    { "qd", 2, USFM_ROLE_PARA, 0, 0, USFM_M_QD },
    { "qm", 2, USFM_ROLE_PARA, 0, 0, USFM_M_QM },
    { "qr", 2, USFM_ROLE_PARA, 0, 0, USFM_M_QR },
    { "r", 1, USFM_ROLE_PARA, 0, USFM_MP_HEADING, USFM_M_R },
    { "rem", 3, USFM_ROLE_PARA, 0, 0, USFM_M_REM },
    { "s", 1, USFM_ROLE_PARA, 0, USFM_MP_HEADING, USFM_M_S },
    { "sd", 2, USFM_ROLE_PARA, 0, USFM_MP_HEADING, USFM_M_SD },
    { "sp", 2, USFM_ROLE_PARA, 0, USFM_MP_HEADING, USFM_M_SP },
    { "sr", 2, USFM_ROLE_PARA, 0, USFM_MP_HEADING, USFM_M_SR },
    { "tr", 2, USFM_ROLE_ROW, 0, 0, USFM_M_TR },
    { "add", 3, USFM_ROLE_CHAR, 0, 0, USFM_M_ADD },
    { "bd", 2, USFM_ROLE_CHAR, 0, 0, USFM_M_BD },
    { "bdit", 4, USFM_ROLE_CHAR, 0, 0, USFM_M_BDIT },
    { "bk", 2, USFM_ROLE_CHAR, 0, 0, USFM_M_BK },
    { "dc", 2, USFM_ROLE_CHAR, 0, 0, USFM_M_DC },
    { "em", 2, USFM_ROLE_CHAR, 0, 0, USFM_M_EM },
    { "fm", 2, USFM_ROLE_CHAR, 0, 0, USFM_M_FM },
    { "fv", 2, USFM_ROLE_CHAR, 0, 0, USFM_M_FV },
    { "ior", 3, USFM_ROLE_CHAR, 0, 0, USFM_M_IOR },
    { "iqt", 3, USFM_ROLE_CHAR, 0, 0, USFM_M_IQT },
    { "it", 2, USFM_ROLE_CHAR, 0, 0, USFM_M_IT },
    { "jmp", 3, USFM_ROLE_CHAR, 0, 0, USFM_M_JMP },
    { "k", 1, USFM_ROLE_CHAR, 0, 0, USFM_M_K },
    { "lik", 3, USFM_ROLE_CHAR, 0, 0, USFM_M_LIK },
    { "litl", 4, USFM_ROLE_CHAR, 0, 0, USFM_M_LITL },
    { "liv", 3, USFM_ROLE_CHAR, 0, 0, USFM_M_LIV },
    { "nd", 2, USFM_ROLE_CHAR, 0, 0, USFM_M_ND },
    { "no", 2, USFM_ROLE_CHAR, 0, 0, USFM_M_NO },
    { "ord", 3, USFM_ROLE_CHAR, 0, 0, USFM_M_ORD },
    { "pn", 2, USFM_ROLE_CHAR, 0, 0, USFM_M_PN },
    { "png", 3, USFM_ROLE_CHAR, 0, 0, USFM_M_PNG },
    { "qac", 3, USFM_ROLE_CHAR, 0, 0, USFM_M_QAC },
    { "qs", 2, USFM_ROLE_CHAR, 0, 0, USFM_M_QS },
    { "qt", 2, USFM_ROLE_CHAR, 0, 0, USFM_M_QT },
    { "rb", 2, USFM_ROLE_CHAR, 0, 0, USFM_M_RB },
    { "ref", 3, USFM_ROLE_CHAR, 0, 0, USFM_M_REF },
    { "rq", 2, USFM_ROLE_CHAR, 0, 0, USFM_M_RQ },
    { "sc", 2, USFM_ROLE_CHAR, 0, 0, USFM_M_SC },
    { "sig", 3, USFM_ROLE_CHAR, 0, 0, USFM_M_SIG },
    { "sls", 3, USFM_ROLE_CHAR, 0, 0, USFM_M_SLS },
    { "sup", 3, USFM_ROLE_CHAR, 0, 0, USFM_M_SUP },
    { "tl", 2, USFM_ROLE_CHAR, 0, 0, USFM_M_TL },
    { "w", 1, USFM_ROLE_CHAR, 0, 0, USFM_M_W },
    { "wa", 2, USFM_ROLE_CHAR, 0, 0, USFM_M_WA },
    { "wg", 2, USFM_ROLE_CHAR, 0, 0, USFM_M_WG },
    { "wh", 2, USFM_ROLE_CHAR, 0, 0, USFM_M_WH },
    { "wj", 2, USFM_ROLE_CHAR, 0, 0, USFM_M_WJ },
    { "ef", 2, USFM_ROLE_NOTE, 0, USFM_MP_ARG_WORD, USFM_M_EF },
    { "efe", 3, USFM_ROLE_NOTE, 0, USFM_MP_ARG_WORD, USFM_M_EFE },
    { "ex", 2, USFM_ROLE_NOTE, 0, USFM_MP_ARG_WORD, USFM_M_EX },
    { "f", 1, USFM_ROLE_NOTE, 0, USFM_MP_ARG_WORD, USFM_M_F },
    { "fe", 2, USFM_ROLE_NOTE, 0, USFM_MP_ARG_WORD, USFM_M_FE },
    { "x", 1, USFM_ROLE_NOTE, 0, USFM_MP_ARG_WORD, USFM_M_X },
    { "fk", 2, USFM_ROLE_NOTE_PART, 0, 0, USFM_M_FK },
    { "fl", 2, USFM_ROLE_NOTE_PART, 0, 0, USFM_M_FL },
    { "fp", 2, USFM_ROLE_NOTE_PART, 0, 0, USFM_M_FP },
    { "fq", 2, USFM_ROLE_NOTE_PART, 0, 0, USFM_M_FQ },
    { "fqa", 3, USFM_ROLE_NOTE_PART, 0, 0, USFM_M_FQA },
    { "fr", 2, USFM_ROLE_NOTE_PART, 0, 0, USFM_M_FR },
    { "ft", 2, USFM_ROLE_NOTE_PART, 0, 0, USFM_M_FT },
    { "fw", 2, USFM_ROLE_NOTE_PART, 0, 0, USFM_M_FW },
    { "xk", 2, USFM_ROLE_NOTE_PART, 0, 0, USFM_M_XK },
    { "xnt", 3, USFM_ROLE_NOTE_PART, 0, 0, USFM_M_XNT },
    { "xo", 2, USFM_ROLE_NOTE_PART, 0, 0, USFM_M_XO },
    { "xop", 3, USFM_ROLE_NOTE_PART, 0, 0, USFM_M_XOP },
    { "xot", 3, USFM_ROLE_NOTE_PART, 0, 0, USFM_M_XOT },
    { "xq", 2, USFM_ROLE_NOTE_PART, 0, 0, USFM_M_XQ },
    { "xt", 2, USFM_ROLE_NOTE_PART, 0, 0, USFM_M_XT },
    { "xta", 3, USFM_ROLE_NOTE_PART, 0, 0, USFM_M_XTA },
    { "tc", 2, USFM_ROLE_CELL, 0, 0, USFM_M_TC },
    { "tcc", 3, USFM_ROLE_CELL, 0, 0, USFM_M_TCC },
    { "tcr", 3, USFM_ROLE_CELL, 0, 0, USFM_M_TCR },
    { "th", 2, USFM_ROLE_CELL, 0, 0, USFM_M_TH },
    { "thc", 3, USFM_ROLE_CELL, 0, 0, USFM_M_THC },
    { "thr", 3, USFM_ROLE_CELL, 0, 0, USFM_M_THR },
    { "ts", 2, USFM_ROLE_MILESTONE, 0, 0, USFM_M_TS },
    { "cat", 3, USFM_ROLE_CHAR, 0, 0, USFM_M_CAT },
    { "esb", 3, USFM_ROLE_SIDEBAR, 0, 0, USFM_M_ESB },
    { "esbe", 4, USFM_ROLE_SIDEBAR_END, 0, 0, USFM_M_ESBE },
    { "fig", 3, USFM_ROLE_FIGURE, 0, 0, USFM_M_FIG },
    { "periph", 6, USFM_ROLE_PERIPH, 0, USFM_MP_ARG_LINE, USFM_M_PERIPH },
    { "addpn", 5, USFM_ROLE_CHAR, 0, 0, USFM_M_ADDPN },
    { "fdc", 3, USFM_ROLE_NOTE_PART, 0, 0, USFM_M_FDC },
    { "fs", 2, USFM_ROLE_NOTE_PART, 0, 0, USFM_M_FS },
    { "ph", 2, USFM_ROLE_PARA, 0, 0, USFM_M_PH },
    { "phi", 3, USFM_ROLE_PARA, 0, 0, USFM_M_PHI },
    { "pro", 3, USFM_ROLE_CHAR, 0, 0, USFM_M_PRO },
    { "xdc", 3, USFM_ROLE_NOTE_PART, 0, 0, USFM_M_XDC },
    { "conc", 4, USFM_ROLE_PARA, 0, 0, USFM_M_CONC },
    { "cov", 3, USFM_ROLE_PARA, 0, 0, USFM_M_COV },
    { "glo", 3, USFM_ROLE_PARA, 0, 0, USFM_M_GLO },
    { "idx", 3, USFM_ROLE_PARA, 0, 0, USFM_M_IDX },
    { "intro", 5, USFM_ROLE_PARA, 0, 0, USFM_M_INTRO },
    { "maps", 4, USFM_ROLE_PARA, 0, 0, USFM_M_MAPS },
    { "pref", 4, USFM_ROLE_PARA, 0, 0, USFM_M_PREF },
    { "ps", 2, USFM_ROLE_PARA, 0, 0, USFM_M_PS },
    { "psi", 3, USFM_ROLE_PARA, 0, 0, USFM_M_PSI },
    { "pub", 3, USFM_ROLE_PARA, 0, 0, USFM_M_PUB },
    { "pubinfo", 7, USFM_ROLE_PARA, 0, 0, USFM_M_PUBINFO },
    { "spine", 5, USFM_ROLE_PARA, 0, 0, USFM_M_SPINE },
    { "wr", 2, USFM_ROLE_CHAR, 0, 0, USFM_M_WR },
    { "efm", 3, USFM_ROLE_NOTE_PART, 0, 0, USFM_M_EFM },
    { "ndx", 3, USFM_ROLE_CHAR, 0, 0, USFM_M_NDX },
    { "xtSee", 5, USFM_ROLE_NOTE_PART, 0, 0, USFM_M_XTSEE },
    { "xtSeeAlso", 9, USFM_ROLE_NOTE_PART, 0, 0, USFM_M_XTSEEALSO },
    { "erq", 3, USFM_ROLE_PARA, 0, 0, USFM_M_ERQ },
    { "erqe", 4, USFM_ROLE_PARA, 0, 0, USFM_M_ERQE },
    { "mse", 3, USFM_ROLE_PARA, 0, USFM_MP_HEADING, USFM_M_MSE },
    { "ms2e", 4, USFM_ROLE_PARA, 2, USFM_MP_HEADING, USFM_M_MSE },
    { "ms3e", 4, USFM_ROLE_PARA, 3, USFM_MP_HEADING, USFM_M_MSE },
    { "restore", 7, USFM_ROLE_PARA, 0, 0, USFM_M_RESTORE },
    { "s1e", 3, USFM_ROLE_PARA, 1, USFM_MP_HEADING, USFM_M_SE },
    { "s2e", 3, USFM_ROLE_PARA, 2, USFM_MP_HEADING, USFM_M_SE },
    { "s3e", 3, USFM_ROLE_PARA, 3, USFM_MP_HEADING, USFM_M_SE },
    { "s4e", 3, USFM_ROLE_PARA, 4, USFM_MP_HEADING, USFM_M_SE },
    { "ilit", 4, USFM_ROLE_PARA, 0, 0, USFM_M_ILIT },
    { "list", 4, USFM_ROLE_MILESTONE, 0, 0, USFM_M_LIST },
    { "pl", 2, USFM_ROLE_PARA, 0, 0, USFM_M_PL },
    { "t", 1, USFM_ROLE_MILESTONE, 0, 0, USFM_M_T },
    { "ta", 2, USFM_ROLE_CHAR, 0, 0, USFM_M_TA },
    { "table", 5, USFM_ROLE_MILESTONE, 0, 0, USFM_M_TABLE },
    { "vid", 3, USFM_ROLE_MILESTONE, 0, 0, USFM_M_VID },
    { "wl", 2, USFM_ROLE_CHAR, 0, 0, USFM_M_WL },
};
static const uint8_t USFM_MarkerSlots[1149] = {
    0, 115, 0, 0, 0, 0, 0, 0, 0, 0, 0, 17, 0, 52, 0, 147,
    0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 84, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 96, 0, 0, 0, 9, 0, 0,
    0, 0, 0, 0, 3, 0, 0, 0, 139, 0, 0, 0, 0, 40, 0, 0,
    0, 0, 0, 0, 43, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    131, 69, 0, 0, 0, 0, 0, 0, 0, 0, 57, 0, 0, 0, 0, 182,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 30, 0, 176, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 82,
    0, 0, 0, 0, 0, 0, 0, 62, 0, 0, 0, 0, 0, 0, 0, 71,
    0, 0, 0, 121, 0, 0, 183, 0, 0, 1, 160, 0, 0, 0, 13, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7,
    0, 0, 0, 0, 0, 0, 0, 70, 0, 114, 0, 0, 63, 0, 0, 68,
    55, 0, 0, 0, 72, 0, 0, 0, 86, 168, 0, 133, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 91, 0, 0, 0, 0, 39, 0, 0,
    0, 162, 0, 0, 0, 151, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 59, 0, 154, 0, 0, 0, 0, 58, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 113, 77, 0, 0, 0, 103, 0, 0, 0, 0,
    16, 169, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 45, 0, 0, 118,
    0, 0, 0, 0, 0, 0, 158, 0, 0, 0, 0, 54, 0, 0, 0, 0,
    0, 56, 0, 0, 0, 0, 0, 0, 0, 19, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 142, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 179, 0, 0, 0,
    0, 0, 123, 0, 0, 0, 0, 0, 0, 38, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 105, 0, 0, 0, 0, 0, 174,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 47, 0, 0, 0, 81, 0, 0, 0, 0, 0, 0, 0,
    0, 185, 0, 159, 0, 0, 141, 0, 0, 178, 177, 0, 0, 97, 0, 0,
    0, 0, 0, 0, 0, 74, 0, 0, 0, 0, 0, 0, 0, 0, 10, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 51, 0, 119, 0, 0,
    0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 108, 0, 0, 0, 0, 76,
    0, 0, 0, 136, 0, 0, 15, 0, 11, 0, 0, 0, 0, 0, 0, 0,
    0, 26, 0, 0, 0, 155, 0, 0, 0, 0, 78, 0, 0, 0, 0, 0,
    165, 0, 0, 122, 166, 0, 0, 0, 0, 0, 0, 0, 0, 0, 150, 0,
    107, 0, 0, 33, 0, 0, 89, 0, 0, 0, 135, 0, 23, 0, 0, 0,
    0, 0, 0, 0, 14, 2, 0, 0, 0, 0, 0, 0, 0, 149, 0, 0,
    0, 0, 0, 0, 0, 0, 53, 0, 0, 111, 0, 0, 0, 0, 0, 64,
    0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 66, 0, 0,
    134, 44, 28, 0, 0, 0, 0, 0, 0, 0, 0, 0, 73, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 25,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 180, 0, 0, 0, 0, 0, 126,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 34, 0, 0, 85, 0, 41, 0, 0, 0, 0, 0, 127, 0, 0, 0,
    0, 184, 0, 0, 153, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 31, 0, 0, 35, 0, 0, 83, 0, 0,
    0, 98, 0, 0, 0, 0, 0, 163, 0, 0, 0, 0, 0, 27, 0, 128,
    0, 94, 0, 0, 181, 0, 0, 22, 0, 0, 0, 99, 0, 0, 0, 0,
    0, 0, 20, 186, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    36, 0, 0, 0, 0, 0, 0, 125, 0, 0, 0, 0, 0, 144, 170, 0,
    0, 0, 0, 0, 0, 130, 0, 0, 48, 93, 0, 0, 157, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 95, 0, 0, 0, 0, 88, 21, 0, 0, 0,
    0, 102, 0, 0, 0, 0, 0, 0, 0, 0, 0, 104, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 61, 0, 0, 42, 0, 0, 0, 0, 173, 0, 148,
    0, 0, 0, 67, 0, 0, 0, 0, 0, 156, 0, 0, 0, 109, 0, 0,
    75, 0, 106, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 116, 0, 0, 0, 0, 0, 0, 80, 0, 0, 0, 0, 0, 0, 172,
    0, 0, 0, 0, 0, 0, 0, 79, 0, 0, 12, 0, 37, 0, 0, 0,
    0, 0, 0, 110, 0, 0, 0, 0, 0, 0, 143, 0, 90, 0, 8, 0,
    0, 0, 0, 0, 0, 0, 49, 171, 0, 0, 0, 0, 0, 18, 112, 0,
    101, 0, 0, 0, 0, 0, 129, 0, 46, 0, 140, 0, 0, 0, 0, 0,
    0, 0, 32, 65, 0, 0, 0, 60, 0, 6, 0, 0, 0, 0, 0, 0,
    0, 0, 145, 0, 0, 0, 164, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 132,
    0, 124, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 161, 0, 0,
    0, 29, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 138, 92,
    0, 0, 0, 0, 146, 0, 0, 175, 152, 117, 0, 0, 0, 0, 24, 0,
    0, 0, 0, 0, 0, 0, 120, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 167, 0, 0, 0, 0, 50, 0, 0, 0,
    0, 0, 0, 87, 137, 0, 0, 0, 0, 0, 0, 0, 0,
};
// USFM_CODEGEN marker_table end

/* ===== Allocator ===== */

/* Largest request the library makes: the largest u32 that is a multiple of 8. */
#define USFM__ALLOC_MAX 0xFFFFFFF8u

/* Every request is rounded to a multiple of 8 (allocator contract). */
static size_t usfm__round8(size_t size)
{
    USFM_ASSERT(size <= USFM__ALLOC_MAX);
    if (size == 0) size = 1;
    return (size + 7u) & ~(size_t)7u;
}

#ifndef USFM_NO_LIBC
static void *usfm__libc_alloc(void *ctx, size_t size)
{
    (void)ctx;
    return malloc(size);
}
static void *usfm__libc_resize(void *ctx, void *ptr, size_t old_size, size_t new_size)
{
    (void)ctx;
    (void)old_size;
    return realloc(ptr, new_size);
}
static void usfm__libc_dealloc(void *ctx, void *ptr, size_t size)
{
    (void)ctx;
    (void)size;
    free(ptr);
}
static const USFM_Alc usfm__libc_alc = {
    usfm__libc_alloc, usfm__libc_resize, usfm__libc_dealloc, NULL
};
#endif

/* The allocator to use, or NULL when none was given and libc is off. */
static const USFM_Alc *usfm__alc_or_default(const USFM_Alc *alc)
{
    if (alc != NULL) return alc;
#ifndef USFM_NO_LIBC
    return &usfm__libc_alc;
#else
    return NULL;
#endif
}

static void *usfm__alloc(const USFM_Alc *alc, size_t size)
{
    return alc->alloc(alc->ctx, usfm__round8(size));
}

static void usfm__free(const USFM_Alc *alc, void *ptr, size_t size)
{
    if (ptr != NULL) alc->dealloc(alc->ctx, ptr, usfm__round8(size));
}

/* New capacity for growing `capacity` to hold `needed` elements of
 * elem_size: doubles (first step 8), clamped so the array stays within
 * USFM__ALLOC_MAX bytes. Returns 0 if `needed` can't fit. */
static uint32_t usfm__grow_capacity(uint32_t capacity, uint32_t elem_size, uint32_t needed)
{
    uint32_t max_capacity;
    uint64_t new_capacity;
    uint32_t i;
    USFM_ASSERT(elem_size > 0);
    if (needed <= capacity) return capacity;
    max_capacity = USFM__ALLOC_MAX / elem_size;
    if (needed > max_capacity) return 0;
    new_capacity = capacity ? (uint64_t)capacity * 2u : 8u;
    /* Bounded: each pass doubles a 64-bit value toward a 32-bit target. */
    for (i = 0; i < 32 && new_capacity < needed; i++) new_capacity *= 2u;
    if (new_capacity > max_capacity) new_capacity = max_capacity;
    return (uint32_t)new_capacity;
}

/* Makes room for `needed` elements of elem_size, doubling *capacity
 * (clamped at USFM__ALLOC_MAX bytes). Returns the (possibly moved)
 * array, or NULL on failure; on failure the old block stays valid and
 * *capacity is unchanged. */
static void *usfm__grow(const USFM_Alc *alc, void *array, uint32_t *capacity,
                        uint32_t elem_size, uint32_t needed)
{
    uint32_t new_capacity;
    void *block;
    if (needed <= *capacity) return array;
    new_capacity = usfm__grow_capacity(*capacity, elem_size, needed);
    if (new_capacity == 0) return NULL;
    if (array == NULL) {
        block = usfm__alloc(alc, (size_t)new_capacity * elem_size);
    } else {
        block = alc->resize(alc->ctx, array,
                            usfm__round8((size_t)*capacity * elem_size),
                            usfm__round8((size_t)new_capacity * elem_size));
    }
    if (block == NULL) return NULL;
    *capacity = new_capacity;
    return block;
}

/* Sizes above USFM__ALLOC_MAX can never have been allocated by the
 * library, so such a call is ignored (nothing is freed). */
USFM_API void USFM_Alc_Free(const USFM_Alc *alc, void *ptr, size_t size)
{
    const USFM_Alc *a = usfm__alc_or_default(alc);
    if (ptr == NULL || a == NULL || size > USFM__ALLOC_MAX) return;
    usfm__free(a, ptr, size);
}

/* ===== Errors and diagnostics ===== */

static const char *usfm__status_message(uint32_t code)
{
    switch (code) {
    case USFM_OK: return "ok";
    case USFM_ERR_CAPACITY: return "output buffer too small";
    case USFM_ERR_DEPTH: return "nesting too deep";
    case USFM_ERR_INPUT_TOO_LARGE: return "input larger than USFM_INPUT_MAX";
    case USFM_ERR_INVALID_ARG: return "invalid argument";
    case USFM_ERR_NOMEM: return "out of memory";
    case USFM_ERR_NO_ALLOCATOR: return "no allocator (USFM_NO_LIBC)";
    default: return "unknown error";
    }
}

static void usfm__set_err(USFM_Err *err, uint32_t code, uint32_t pos)
{
    if (err == NULL) return;
    err->code = code;
    err->pos = pos;
    err->msg = usfm__status_message(code);
}

static const uint8_t usfm__diag_severity[USFM_DIAGNOSTIC_CODE_COUNT] = {
    USFM_SEVERITY_NONE,                 /* NONE */
    USFM_SEVERITY_WARNING,              /* UNKNOWN_MARKER */
    USFM_SEVERITY_ERROR,                /* UNCLOSED_MARKER */
    USFM_SEVERITY_ERROR,                /* UNEXPECTED_CLOSE */
    USFM_SEVERITY_ERROR,                /* INVALID_NESTING */
    USFM_SEVERITY_ERROR,                /* INVALID_NUMBER */
    USFM_SEVERITY_ERROR,                /* DUPLICATE_NUMBER */
    USFM_SEVERITY_ERROR,                /* VERSE_BEFORE_CHAPTER */
    USFM_SEVERITY_ERROR,                /* INVALID_BOOK_CODE */
    USFM_SEVERITY_ERROR,                /* MISSING_CALLER */
    USFM_SEVERITY_ERROR,                /* UNTERMINATED_QUOTE */
    USFM_SEVERITY_WARNING,              /* UNKNOWN_ATTRIBUTE */
    USFM_SEVERITY_ERROR,                /* MISSING_ATTRIBUTE */
    USFM_SEVERITY_ERROR,                /* INVALID_ESCAPE */
    USFM_SEVERITY_ERROR,                /* MILESTONE_MISMATCH */
    USFM_SEVERITY_WARNING,              /* UNSUPPORTED_VERSION */
    USFM_SEVERITY_INFO,                 /* DUPLICATE_STYLE */
    USFM_SEVERITY_WARNING,              /* IMPLIED_PARAGRAPH */
    USFM_SEVERITY_ERROR                 /* TOO_DEEP */
};

static const char *const usfm__diag_message[USFM_DIAGNOSTIC_CODE_COUNT] = {
    "",
    "unknown marker",
    "unclosed marker",
    "end marker with no open marker",
    "invalid nesting",
    "invalid number",
    "duplicate number",
    "verse before the first chapter",
    "invalid book code",
    "missing note caller",
    "unterminated attribute quote",
    "unknown attribute",
    "missing required attribute",
    "invalid escape",
    "milestone start and end do not match",
    "unsupported USFM version",
    "style defined more than once",
    "implied paragraph",
    "nesting deeper than USFM_STACK_DEPTH"
};

USFM_API uint8_t USFM_DiagnosticCode_GetSeverity(uint16_t code)
{
    return code < USFM_DIAGNOSTIC_CODE_COUNT ? usfm__diag_severity[code]
                                             : (uint8_t)USFM_SEVERITY_NONE;
}

USFM_API const char *USFM_DiagnosticCode_GetMessage(uint16_t code)
{
    return code < USFM_DIAGNOSTIC_CODE_COUNT ? usfm__diag_message[code]
                                             : "unknown diagnostic";
}

/* ===== Marker lookup ===== */

/* Returns the USFM_MarkerRows index of an exact key, or USFM_ROW_NONE. */
static uint32_t usfm__marker_find(const char *name, uint32_t length)
{
    uint32_t slot, index;
    const USFM_MarkerRow *row;
    if (length == 0 || length > USFM_MARKER_NAME_MAX) return USFM_ROW_NONE;
    slot = usfm__marker_slot(usfm__marker_hash(name, length),
                             USFM_MARKER_HASH_SEED, USFM_MARKER_SLOT_COUNT);
    index = USFM_MarkerSlots[slot];
    if (index == 0) return USFM_ROW_NONE;
    row = &USFM_MarkerRows[index - 1];
    if (row->name_length != length || memcmp(row->name, name, length) != 0) {
        return USFM_ROW_NONE;
    }
    return index - 1;
}

/* A token plus what the parser needs about its marker. */
typedef struct USFM_ScanToken {
    USFM_Token tok;
    uint16_t   row;                     /* USFM_MarkerRows index, or
                                           USFM_ROW_NONE */
    uint16_t   reserved;
    uint32_t   name_offset;             /* marker name in the input */
    uint32_t   name_length;             /* 0 for "\*" and a lone "\" */
} USFM_ScanToken;

/* Sets t->row, t->tok.marker and the level / milestone bits of
 * t->tok.level_flags from a marker name. Tries the full name first
 * (k1, s1e), then base letters + level digits + optional -s / -e /
 * -<digits> (qt1-s, tc1-2). Anything else is USFM_M_UNKNOWN, including
 * a level above 15 and -s / -e on a marker that is not a character or
 * milestone marker (\p-s). */
static void usfm__classify(const char *name, uint32_t length, USFM_ScanToken *t)
{
    uint32_t row = usfm__marker_find(name, length);
    uint32_t i = 0, base, level = 0;
    uint8_t flags = 0;
    t->row = USFM_ROW_NONE;
    t->tok.marker = USFM_M_UNKNOWN;
    if (row != USFM_ROW_NONE) {
        t->row = (uint16_t)row;
        t->tok.marker = USFM_MarkerRows[row].marker;
        t->tok.level_flags |= USFM_MarkerRows[row].level;
        return;
    }
    while (i < length && (USFM_ByteClass[(uint8_t)name[i]] & USFM_BC_ALPHA)) i++;
    base = i;
    if (base == 0) return;
    while (i < length && (USFM_ByteClass[(uint8_t)name[i]] & USFM_BC_DIGIT)) {
        if (level < 100) level = level * 10u + (uint32_t)(name[i] - '0');
        i++;
    }
    if (i < length && name[i] == '-') {
        if (i + 2 == length && name[i + 1] == 's') {
            flags = USFM_TF_MILESTONE_START;
            i = length;
        } else if (i + 2 == length && name[i + 1] == 'e') {
            flags = USFM_TF_MILESTONE_END;
            i = length;
        } else if (level > 0) {         /* column span: \tc1-2 */
            uint32_t j = i + 1;
            while (j < length && (USFM_ByteClass[(uint8_t)name[j]] & USFM_BC_DIGIT)) j++;
            if (j > i + 1 && j == length) i = length;
        }
    }
    if (i != length) return;
    if (level > 15u) return;            /* doesn't fit the 4-bit level */
    row = usfm__marker_find(name, base);
    if (row == USFM_ROW_NONE) return;
    if (flags != 0 && USFM_MarkerRows[row].role != USFM_ROLE_CHAR &&
        USFM_MarkerRows[row].role != USFM_ROLE_MILESTONE) {
        return;                         /* -s / -e only on char / milestone */
    }
    t->row = (uint16_t)row;
    t->tok.marker = USFM_MarkerRows[row].marker;
    t->tok.level_flags |= (uint8_t)(level | flags);
}

/* ===== Scanner ===== */

enum { USFM_ARG_NONE = 0, USFM_ARG_WORD, USFM_ARG_LINE };

typedef struct USFM_Scanner {
    const char *input;
    uint32_t    length;
    uint32_t    position;
    uint8_t     expect_arg;             /* set after a marker that takes one */
    uint8_t     pipe_is_attr;           /* set by the parser before each call */
    uint16_t    reserved;
} USFM_Scanner;

static void usfm__scan_init(USFM_Scanner *s, const char *input, uint32_t length)
{
    memset(s, 0, sizeof *s);
    s->input = input;
    s->length = length;
    if (length >= 3 && (uint8_t)input[0] == 0xEF && (uint8_t)input[1] == 0xBB &&
        (uint8_t)input[2] == 0xBF) {
        s->position = 3;                /* UTF-8 BOM */
    }
}

static USFM_Span usfm__span(uint32_t offset, uint32_t length)
{
    USFM_Span s;
    s.offset = offset;
    s.length = length;
    return s;
}

static void usfm__tok(USFM_ScanToken *t, uint8_t kind, uint32_t start, uint32_t end)
{
    memset(t, 0, sizeof *t);
    t->tok.span = usfm__span(start, end - start);
    t->tok.kind = kind;
    t->row = USFM_ROW_NONE;
}

/* Consumes the one whitespace character that ends an opening marker or
 * an argument: space, tab, LF, CR or CRLF. */
static uint32_t usfm__skip_one_ws(const char *in, uint32_t len, uint32_t i)
{
    if (i >= len) return i;
    if (in[i] == ' ' || in[i] == '\t' || in[i] == '\n') return i + 1;
    if (in[i] == '\r') return (i + 1 < len && in[i + 1] == '\n') ? i + 2 : i + 1;
    return i;
}

/* in[i] is a backslash: is it an escape (\\ \/ \| \~ \") rather than a
 * marker? */
static int usfm__is_escape(const char *in, uint32_t len, uint32_t i)
{
    return i + 1 < len && (USFM_ByteClass[(uint8_t)in[i + 1]] & USFM_BC_ESCAPE) != 0;
}

static int usfm__scan_marker(USFM_Scanner *s, USFM_ScanToken *t)
{
    const char *in = s->input;
    uint32_t len = s->length;
    uint32_t start = s->position;
    uint32_t i = start + 1, name_start, name_length;
    uint8_t kind = USFM_TOK_MARKER, nested = 0;
    if (i < len && in[i] == '*') {      /* "\*" closes a milestone */
        usfm__tok(t, USFM_TOK_MARKER_END, start, i + 1);
        t->tok.level_flags = USFM_TF_SELF_CLOSED;
        s->position = i + 1;
        return 1;
    }
    if (i < len && in[i] == '+') {
        nested = 1;
        i++;
    }
    name_start = i;
    while (i < len && (USFM_ByteClass[(uint8_t)in[i]] & USFM_BC_NAME)) i++;
    name_length = i - name_start;
    if (name_length > 0 && i < len && in[i] == '*') {
        kind = USFM_TOK_MARKER_END;
        i++;
    } else {
        i = usfm__skip_one_ws(in, len, i);
    }
    usfm__tok(t, kind, start, i);
    t->name_offset = name_start;
    t->name_length = name_length;
    t->tok.level_flags = nested ? USFM_TF_NESTED : 0;
    usfm__classify(in + name_start, name_length, t);
    if (kind == USFM_TOK_MARKER && t->row != USFM_ROW_NONE &&
        !(t->tok.level_flags & (USFM_TF_MILESTONE_START | USFM_TF_MILESTONE_END))) {
        uint8_t props = USFM_MarkerRows[t->row].props;
        if (props & USFM_MP_ARG_WORD) s->expect_arg = USFM_ARG_WORD;
        else if (props & USFM_MP_ARG_LINE) s->expect_arg = USFM_ARG_LINE;
    }
    s->position = i;
    return 1;
}

/* Scans the argument a marker asked for. Returns 0 (consuming nothing)
 * if there is none. */
static int usfm__scan_argument(USFM_Scanner *s, USFM_ScanToken *t, uint8_t mode)
{
    const char *in = s->input;
    uint32_t len = s->length;
    uint32_t i = s->position, start, end;
    while (i < len && (in[i] == ' ' || in[i] == '\t')) i++;
    start = i;
    if (mode == USFM_ARG_WORD) {
        while (i < len && in[i] != '\\' &&
               !(USFM_ByteClass[(uint8_t)in[i]] & (USFM_BC_SPACE | USFM_BC_NEWLINE))) i++;
    } else {
        while (i < len && in[i] != '\\' && in[i] != '|' &&
               !(USFM_ByteClass[(uint8_t)in[i]] & USFM_BC_NEWLINE)) i++;
    }
    end = i;
    if (mode == USFM_ARG_LINE) {
        while (end > start && (in[end - 1] == ' ' || in[end - 1] == '\t')) end--;
    }
    if (end == start) return 0;
    usfm__tok(t, USFM_TOK_ARGUMENT, start, end);
    s->position = mode == USFM_ARG_WORD ? usfm__skip_one_ws(in, len, i) : i;
    return 1;
}

/* Returns 1 and fills *t, or 0 at end of input. Every call that
 * returns 1 consumes at least one byte, so a caller's loop is bounded
 * by the input length. */
static int usfm__scan_next(USFM_Scanner *s, USFM_ScanToken *t)
{
    const char *in = s->input;
    uint32_t len = s->length;
    uint32_t start, i;
    uint8_t flags = 0;
    if (s->expect_arg != USFM_ARG_NONE) {
        uint8_t mode = s->expect_arg;
        s->expect_arg = USFM_ARG_NONE;
        if (usfm__scan_argument(s, t, mode)) return 1;
    }
    start = s->position;
    if (start >= len) return 0;
    if (in[start] == '\\' && !usfm__is_escape(in, len, start)) return usfm__scan_marker(s, t);
    if (in[start] == '|' && s->pipe_is_attr) {
        uint32_t end;
        i = start + 1;
        /* Attributes run to the next marker; escapes don't end them. */
        while (i < len) {
            if (in[i] == '\\') {
                /* \= is an escape here only (field guide section 1). */
                if (!usfm__is_escape(in, len, i) &&
                    !(i + 1 < len && in[i + 1] == '=')) break;
                i += 2;
                continue;
            }
            i++;
        }
        if (i > len) i = len;
        end = i;
        while (end > start + 1 &&
               (USFM_ByteClass[(uint8_t)in[end - 1]] & (USFM_BC_SPACE | USFM_BC_NEWLINE))) end--;
        usfm__tok(t, USFM_TOK_ATTRIBUTES, start, end);
        s->position = i;
        return 1;
    }
    if (in[start] == '/' && start + 1 < len && in[start + 1] == '/') {
        usfm__tok(t, USFM_TOK_OPTBREAK, start, start + 2);
        s->position = start + 2;
        return 1;
    }
    /* Text: skip quickly over bytes that are not STOP bytes. */
    i = start;
    while (i < len) {
        uint8_t b = (uint8_t)in[i];
        if (!(USFM_ByteClass[b] & USFM_BC_STOP)) {
            i++;
            continue;
        }
        if (b == '\\') {
            /* \" is accepted as an escape in text too, as in attributes:
             * deliberate and harmless, since \" can't start a marker. */
            if (!usfm__is_escape(in, len, i)) break;
            flags |= USFM_TF_HAS_ESCAPE;
            i += 2;
            continue;
        }
        if (b == '|') {
            if (s->pipe_is_attr) break;
            i++;
            continue;
        }
        if (b == '/') {
            if (i + 1 < len && in[i + 1] == '/') break;
            i++;
            continue;
        }
        if (b == '~') flags |= USFM_TF_HAS_NBSP;
        else flags |= USFM_TF_HAS_LOOSE_WS;      /* CR, LF, TAB */
        i++;
    }
    if (i > len) i = len;
    USFM_ASSERT(i > start);
    usfm__tok(t, USFM_TOK_TEXT, start, i);
    t->tok.level_flags = flags;
    s->position = i;
    return 1;
}

/* ===== AST handle ===== */

struct USFM_Ast {
    USFM_Alc           alc;                 /* copy of the caller's allocator */
    const char        *input;
    char              *owned_input;         /* USFM_PARSE_COPY_INPUT, else NULL */
    uint32_t           input_length;
    uint8_t            version;             /* resolved enum USFM_Version */
    uint8_t            chapters_sorted;     /* strictly increasing numbers */
    uint8_t            verses_sorted;       /* per chapter: increasing, no overlap */
    uint8_t            reserved;
    USFM_Node         *nodes;
    uint32_t           node_count, node_capacity;
    USFM_ChapterEntry *chapters;
    uint32_t           chapter_count, chapter_capacity;
    USFM_VerseEntry   *verses;
    uint32_t           verse_count, verse_capacity;
    USFM_Diagnostic   *diagnostics;
    uint32_t           diagnostic_count, diagnostic_capacity;
    uint32_t           dropped_diagnostics;
    uint32_t           first_dropped_offset;
    uint32_t           max_chapter;
    char               book_code[4];        /* NUL-terminated */
};

USFM_API void USFM_Ast_Free(USFM_Ast *ast)
{
    USFM_Alc alc;
    if (ast == NULL) return;
    alc = ast->alc;
    usfm__free(&alc, ast->nodes, (size_t)ast->node_capacity * sizeof(USFM_Node));
    usfm__free(&alc, ast->chapters, (size_t)ast->chapter_capacity * sizeof(USFM_ChapterEntry));
    usfm__free(&alc, ast->verses, (size_t)ast->verse_capacity * sizeof(USFM_VerseEntry));
    usfm__free(&alc, ast->diagnostics,
               (size_t)ast->diagnostic_capacity * sizeof(USFM_Diagnostic));
    usfm__free(&alc, ast->owned_input, ast->input_length);
    usfm__free(&alc, ast, sizeof(USFM_Ast));
}

/* ===== Parser ===== */

enum { USFM_FRAME_NOTE_PART = 0x01 };

typedef struct USFM_Frame {
    uint32_t node;
    uint8_t  kind;                      /* USFM_AST_* of the node */
    uint8_t  flags;                     /* USFM_FRAME_* */
    uint16_t reserved;
} USFM_Frame;

/* One past the largest USFM_Marker id (USFM_M_WL = 180). A test checks
 * every USFM_MarkerRows entry against it. */
#define USFM__MARKER_ID_COUNT 181u
USFM_STATIC_ASSERT(marker_id_count, USFM_M_WL < USFM__MARKER_ID_COUNT && USFM_M_CUSTOM_BASE >= USFM__MARKER_ID_COUNT);
/* Slots in a closer table's unknown-name hash set (a power of two). */
#define USFM__NAME_SLOTS 64u

typedef struct USFM_NameSlot {
    uint32_t hash;                      /* FNV-1a of the marker name */
    uint32_t last;                      /* offset of its last closer */
    uint32_t stamp;                     /* == generation: slot in use */
} USFM_NameSlot;

/* Where each end marker last appears in one lookahead segment: the
 * stretch from `start` to the next token that ends every character span
 * (see usfm__closer_ahead). Entries are valid only when their stamp
 * equals `generation`, so a rescan never clears the arrays. */
typedef struct USFM_CloserTable {
    uint32_t      start;                /* scan position the segment began at */
    uint32_t      stop;                 /* offset of the stop token, or the
                                           input length */
    uint32_t      generation;           /* 0: no segment scanned yet */
    uint32_t      name_overflow;        /* segment had > USFM__NAME_SLOTS
                                           distinct unknown closer names */
    uint32_t      last_closer[USFM__MARKER_ID_COUNT];
    uint32_t      stamp[USFM__MARKER_ID_COUNT];
    USFM_NameSlot names[USFM__NAME_SLOTS];
} USFM_CloserTable;

typedef struct USFM_Parser {
    USFM_Ast        *ast;
    USFM_Scanner     scan;
    USFM_ScanToken   peeked;
    uint8_t          has_peeked;
    uint8_t          failed;            /* a tree array could not grow */
    uint8_t          version;           /* resolved; \usfm may change it */
    uint8_t          version_auto;      /* config said AUTO */
    uint8_t          chapter_open;      /* a \c is open: set by \c, cleared
                                           by \id and \periph */
    uint8_t          reserved[3];
    uint32_t         depth;
    uint32_t         last_end;          /* end of the last token placed */
    USFM_CloserTable closers[2];        /* [in_note]: see usfm__closer_ahead */
    USFM_Frame       stack[USFM_STACK_DEPTH];
} USFM_Parser;

static uint8_t usfm__top_kind(const USFM_Parser *p)
{
    return p->stack[p->depth - 1].kind;
}

static int usfm__is_block(uint8_t kind)
{
    return kind == USFM_AST_BOOK || kind == USFM_AST_SIDEBAR || kind == USFM_AST_PERIPHERAL;
}

static int usfm__is_blank(const char *in, USFM_Span s)
{
    uint32_t i;
    for (i = 0; i < s.length; i++) {
        char c = in[s.offset + i];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return 0;
    }
    return 1;
}

static int usfm__next(USFM_Parser *p, USFM_ScanToken *t)
{
    uint8_t top;
    if (p->has_peeked) {
        *t = p->peeked;
        p->has_peeked = 0;
        return 1;
    }
    top = usfm__top_kind(p);
    p->scan.pipe_is_attr = (uint8_t)(top == USFM_AST_CHAR || top == USFM_AST_MILESTONE ||
                                     top == USFM_AST_FIGURE || top == USFM_AST_PERIPHERAL);
    return usfm__scan_next(&p->scan, t);
}

static void usfm__push_back(USFM_Parser *p, const USFM_ScanToken *t)
{
    p->peeked = *t;
    p->has_peeked = 1;
}

/* Diagnostics never fail the parse: if the array can't grow, the
 * diagnostic is counted as dropped. */
static void usfm__diag(USFM_Parser *p, uint16_t code, USFM_Span span, USFM_Marker marker)
{
    USFM_Ast *ast = p->ast;
    USFM_Diagnostic *d;
    if (ast->diagnostic_count == ast->diagnostic_capacity) {
        void *grown = usfm__grow(&ast->alc, ast->diagnostics, &ast->diagnostic_capacity,
                                 sizeof(USFM_Diagnostic), ast->diagnostic_count + 1);
        if (grown == NULL) {
            if (ast->dropped_diagnostics == 0) ast->first_dropped_offset = span.offset;
            ast->dropped_diagnostics++;
            return;
        }
        ast->diagnostics = (USFM_Diagnostic *)grown;
    }
    d = &ast->diagnostics[ast->diagnostic_count++];
    d->span = span;
    d->code = code;
    d->marker = marker;
}

/* Appends a node as a child of the top frame. Returns its index, or
 * USFM_NIL (and sets p->failed) if the node array can't grow. */
static uint32_t usfm__append(USFM_Parser *p, uint8_t kind, USFM_Span span,
                             USFM_Marker marker, uint8_t level_flags)
{
    USFM_Ast *ast = p->ast;
    USFM_Node *n;
    uint32_t index, span_end;
    if (p->failed) return USFM_NIL;
    if (ast->node_count == ast->node_capacity) {
        void *grown = usfm__grow(&ast->alc, ast->nodes, &ast->node_capacity,
                                 sizeof(USFM_Node), ast->node_count + 1);
        if (grown == NULL) {
            p->failed = 1;
            return USFM_NIL;
        }
        ast->nodes = (USFM_Node *)grown;
    }
    index = ast->node_count++;
    n = &ast->nodes[index];
    n->parent = p->depth > 0 ? p->stack[p->depth - 1].node : USFM_NIL;
    n->end = index + 1;
    n->span = span;
    n->marker = marker;
    n->kind = kind;
    n->level_flags = level_flags;
    span_end = span.offset + span.length;
    if (span_end > p->last_end) p->last_end = span_end;
    return index;
}

/* Appends a container and pushes it. Returns USFM_NIL if the stack is
 * full (diagnostic added) or memory ran out (p->failed). */
static uint32_t usfm__open(USFM_Parser *p, uint8_t kind, USFM_Span span, USFM_Marker marker,
                           uint8_t level_flags, uint8_t frame_flags)
{
    uint32_t node;
    if (p->depth >= USFM_STACK_DEPTH) {
        usfm__diag(p, USFM_DIAGNOSTIC_TOO_DEEP, span, marker);
        return USFM_NIL;
    }
    node = usfm__append(p, kind, span, marker, level_flags);
    if (node == USFM_NIL) return USFM_NIL;
    p->stack[p->depth].node = node;
    p->stack[p->depth].kind = kind;
    p->stack[p->depth].flags = frame_flags;
    p->stack[p->depth].reserved = 0;
    p->depth++;
    return node;
}

/* Opens a container for a marker token. If the stack is full, the
 * marker is kept as plain text instead. */
static uint32_t usfm__open_tok(USFM_Parser *p, uint8_t kind, const USFM_ScanToken *t,
                               uint8_t level_flags, uint8_t frame_flags)
{
    uint32_t node = usfm__open(p, kind, t->tok.span, t->tok.marker, level_flags, frame_flags);
    if (node == USFM_NIL && !p->failed) {
        usfm__append(p, USFM_AST_TEXT, t->tok.span, USFM_M_NONE, 0);
    }
    return node;
}

/* Pops the top frame. Its span grows to the last byte placed inside it
 * (or to an end marker the caller just accounted for in last_end). */
static void usfm__close_top(USFM_Parser *p)
{
    USFM_Frame *f;
    USFM_Node *n;
    USFM_ASSERT(p->depth > 1);
    f = &p->stack[--p->depth];
    n = &p->ast->nodes[f->node];
    n->end = p->ast->node_count;
    if (p->last_end > n->span.offset + n->span.length) {
        n->span.length = p->last_end - n->span.offset;
    }
}

/* Closes the top frame without an end marker, reporting it when the
 * version requires an explicit close. */
static void usfm__close_implicit(USFM_Parser *p)
{
    const USFM_Frame *f = &p->stack[p->depth - 1];
    const USFM_Node *n = &p->ast->nodes[f->node];
    int report = f->kind == USFM_AST_NOTE || f->kind == USFM_AST_FIGURE ||
                 (f->kind == USFM_AST_CHAR && !(f->flags & USFM_FRAME_NOTE_PART) &&
                  p->version >= USFM_VERSION_3_1);
    if (report) usfm__diag(p, USFM_DIAGNOSTIC_UNCLOSED_MARKER, n->span, n->marker);
    usfm__close_top(p);
}

/* Bounded by the stack depth. */
static void usfm__close_to_depth(USFM_Parser *p, uint32_t depth)
{
    while (p->depth > depth && p->depth > 1) usfm__close_implicit(p);
}

/* Closes everything above the nearest BOOK / SIDEBAR / PERIPHERAL
 * (and above a TABLE too, if keep_table). */
static void usfm__close_to_block(USFM_Parser *p, int keep_table)
{
    while (p->depth > 1) {
        uint8_t k = usfm__top_kind(p);
        if (usfm__is_block(k) || (keep_table && k == USFM_AST_TABLE)) break;
        usfm__close_implicit(p);
    }
}

/* Depth (index + 1) of the nearest open frame of `kind`, or 0. */
static uint32_t usfm__find_kind(const USFM_Parser *p, uint8_t kind)
{
    uint32_t d;
    for (d = p->depth; d > 0; d--) {
        if (p->stack[d - 1].kind == kind) return d;
    }
    return 0;
}

/* Trims trailing whitespace from the last TEXT node if it belongs to the
 * open top container, removing the node if nothing is left. Called
 * before paragraph-level markers, \v and the end of input. */
static void usfm__trim_last_text(USFM_Parser *p)
{
    USFM_Ast *ast = p->ast;
    USFM_Node *n;
    if (ast->node_count == 0) return;
    n = &ast->nodes[ast->node_count - 1];
    if (n->kind != USFM_AST_TEXT || n->parent != p->stack[p->depth - 1].node) return;
    while (n->span.length > 0) {
        char c = ast->input[n->span.offset + n->span.length - 1];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        n->span.length--;
    }
    if (n->span.length == 0) ast->node_count--;
}

/* Inline content with no paragraph open gets an implied \p. */
static void usfm__ensure_inline(USFM_Parser *p, uint32_t offset)
{
    uint8_t k = usfm__top_kind(p);
    USFM_Span at;
    if (!usfm__is_block(k) && k != USFM_AST_TABLE) return;
    at = usfm__span(offset, 0);
    usfm__diag(p, USFM_DIAGNOSTIC_IMPLIED_PARAGRAPH, at, USFM_M_P);
    usfm__open(p, USFM_AST_PARA, at, USFM_M_P, USFM_NF_IMPLIED, 0);
}

/* Takes the ARGUMENT token the scanner produced after a marker, as a
 * leaf child of the top frame. Returns 0 if there was none (the next
 * token is pushed back; *arg is a zero-length span at `missing_at`). */
static int usfm__take_argument(USFM_Parser *p, USFM_Span *arg, uint32_t missing_at)
{
    USFM_ScanToken t;
    if (usfm__next(p, &t)) {
        if (t.tok.kind == USFM_TOK_ARGUMENT) {
            usfm__append(p, USFM_AST_ARGUMENT, t.tok.span, USFM_M_NONE, 0);
            *arg = t.tok.span;
            return 1;
        }
        usfm__push_back(p, &t);
    }
    *arg = usfm__span(missing_at, 0);
    return 0;
}

static uint32_t usfm__tok_end(const USFM_ScanToken *t)
{
    return t->tok.span.offset + t->tok.span.length;
}

/* Reads decimal digits at in[*i]; fails on no digits or > 65535. */
static int usfm__read_uint(const char *in, uint32_t *i, uint32_t end, uint32_t *value)
{
    uint32_t start = *i, v = 0;
    while (*i < end && in[*i] >= '0' && in[*i] <= '9') {
        if (v <= 65535u) v = v * 10u + (uint32_t)(in[*i] - '0');
        (*i)++;
    }
    *value = v;
    return *i > start && v <= 65535u;
}

/* "3", "3a", "3-5", "3a-5b". Returns 0 if malformed; *ref keeps what
 * was read. */
static int usfm__parse_verse(const char *in, USFM_Span s, USFM_VerseRef *ref)
{
    uint32_t i = s.offset, end = s.offset + s.length, v;
    if (!usfm__read_uint(in, &i, end, &v)) return 0;
    ref->verse_first = ref->verse_last = (uint16_t)v;
    if (i < end && in[i] >= 'a' && in[i] <= 'z') {
        ref->segment_first = ref->segment_last = (uint8_t)(in[i] - 'a' + 1);
        i++;
    }
    if (i < end && in[i] == '-') {
        i++;
        if (!usfm__read_uint(in, &i, end, &v)) return 0;
        ref->verse_last = (uint16_t)v;
        ref->segment_last = 0;
        if (i < end && in[i] >= 'a' && in[i] <= 'z') {
            ref->segment_last = (uint8_t)(in[i] - 'a' + 1);
            i++;
        }
        if (ref->verse_last < ref->verse_first) return 0;
    }
    return i == end;
}

/* Three bytes, A-Z or 0-9, at least one letter (USFM 3.1.1). */
static int usfm__valid_book_code(const char *in, USFM_Span s)
{
    uint32_t i;
    int letters = 0;
    if (s.length != 3) return 0;
    for (i = 0; i < 3; i++) {
        char c = in[s.offset + i];
        if (c >= 'A' && c <= 'Z') letters++;
        else if (c < '0' || c > '9') return 0;
    }
    return letters > 0;
}

/* The most recent verse ends where the next verse or chapter starts. */
static void usfm__end_open_verse(USFM_Parser *p, uint32_t node)
{
    USFM_Ast *ast = p->ast;
    if (ast->verse_count > 0 && ast->verses[ast->verse_count - 1].end_node == USFM_NIL) {
        ast->verses[ast->verse_count - 1].end_node = node;
    }
}

static void usfm__add_chapter(USFM_Parser *p, uint32_t node, uint32_t number)
{
    USFM_Ast *ast = p->ast;
    USFM_ChapterEntry *e;
    void *grown;
    usfm__end_open_verse(p, node);
    if (ast->chapter_count > 0) {
        USFM_ChapterEntry *prev = &ast->chapters[ast->chapter_count - 1];
        if (prev->end_node == USFM_NIL) prev->end_node = node;
        if (number <= prev->number) ast->chapters_sorted = 0;
    }
    grown = usfm__grow(&ast->alc, ast->chapters, &ast->chapter_capacity,
                       sizeof(USFM_ChapterEntry), ast->chapter_count + 1);
    if (grown == NULL) {
        p->failed = 1;
        return;
    }
    ast->chapters = (USFM_ChapterEntry *)grown;
    e = &ast->chapters[ast->chapter_count++];
    memset(e, 0, sizeof *e);
    e->number = (uint16_t)number;
    e->node = node;
    e->end_node = USFM_NIL;
    e->first_verse = ast->verse_count;
    if (number > ast->max_chapter) ast->max_chapter = number;
    p->chapter_open = 1;
}

/* \id and \periph start a new top-level division: the open verse and
 * chapter end there, so their content never runs into it. Verses after
 * it belong to no chapter until the next \c. */
static void usfm__end_division(USFM_Parser *p, uint32_t node)
{
    USFM_Ast *ast = p->ast;
    usfm__end_open_verse(p, node);
    p->chapter_open = 0;
    if (ast->chapter_count > 0 && ast->chapters[ast->chapter_count - 1].end_node == USFM_NIL) {
        ast->chapters[ast->chapter_count - 1].end_node = node;
    }
}

/* True if `next` can follow `prev` in a chapter's verse table without
 * breaking the sorted-and-non-overlapping order: it starts after prev
 * ends, or both share a verse number and next's segment starts after
 * prev's last segment (`\v 3a` then `\v 3b`). */
static int usfm__verse_follows(const USFM_VerseRef *prev, const USFM_VerseRef *next)
{
    if (next->verse_first > prev->verse_last) return 1;
    return next->verse_first == prev->verse_last && prev->segment_last != 0 &&
           next->segment_first > prev->segment_last;
}

static void usfm__add_verse(USFM_Parser *p, uint32_t node, const USFM_VerseRef *ref)
{
    USFM_Ast *ast = p->ast;
    USFM_VerseEntry *e;
    void *grown;
    usfm__end_open_verse(p, node);
    grown = usfm__grow(&ast->alc, ast->verses, &ast->verse_capacity,
                       sizeof(USFM_VerseEntry), ast->verse_count + 1);
    if (grown == NULL) {
        p->failed = 1;
        return;
    }
    ast->verses = (USFM_VerseEntry *)grown;
    e = &ast->verses[ast->verse_count++];
    e->ref = *ref;
    e->node = node;
    e->end_node = USFM_NIL;
    /* A reversed range (\v 7-4, diagnosed) breaks the binary search's
     * assumption that verse_last never decreases. */
    if (ref->verse_first > ref->verse_last) ast->verses_sorted = 0;
    if (p->chapter_open) {
        USFM_ChapterEntry *chapter = &ast->chapters[ast->chapter_count - 1];
        if (chapter->verse_count > 0 &&
            !usfm__verse_follows(&ast->verses[ast->verse_count - 2].ref, ref)) {
            ast->verses_sorted = 0;
        }
        chapter->verse_count++;
    }
}

static void usfm__set_version(USFM_Parser *p, USFM_Span arg)
{
    const char *s = p->ast->input + arg.offset;
    uint32_t i = 0, major = 0, minor = 0;
    uint8_t v;
    if (!p->version_auto) return;
    while (i < arg.length && s[i] >= '0' && s[i] <= '9' && major < 100) major = major * 10u + (uint32_t)(s[i++] - '0');
    if (i < arg.length && s[i] == '.') {
        i++;
        while (i < arg.length && s[i] >= '0' && s[i] <= '9' && minor < 100) minor = minor * 10u + (uint32_t)(s[i++] - '0');
    }
    if (major == 1) v = USFM_VERSION_1;
    else if (major == 2) v = USFM_VERSION_2;
    else if (major == 3 && minor == 0) v = USFM_VERSION_3_0;
    else if (major == 3) v = USFM_VERSION_3_1;
    else {
        usfm__diag(p, USFM_DIAGNOSTIC_UNSUPPORTED_VERSION, arg, USFM_M_USFM);
        v = USFM_VERSION_3_1;
    }
    p->version = v;
}

static void usfm__on_text(USFM_Parser *p, const USFM_ScanToken *t)
{
    uint8_t k = usfm__top_kind(p);
    if ((usfm__is_block(k) || k == USFM_AST_TABLE || k == USFM_AST_MILESTONE) &&
        usfm__is_blank(p->ast->input, t->tok.span)) {
        return;                         /* layout whitespace between blocks */
    }
    usfm__ensure_inline(p, t->tok.span.offset);
    usfm__append(p, USFM_AST_TEXT, t->tok.span, USFM_M_NONE,
                 (uint8_t)(t->tok.level_flags & USFM_TEXT_FLAGS_COOK));
}

static void usfm__on_optbreak(USFM_Parser *p, const USFM_ScanToken *t)
{
    usfm__ensure_inline(p, t->tok.span.offset);
    usfm__append(p, USFM_AST_OPTBREAK, t->tok.span, USFM_M_NONE, 0);
}

static void usfm__on_para(USFM_Parser *p, const USFM_ScanToken *t, uint8_t kind)
{
    const USFM_MarkerRow *row = &USFM_MarkerRows[t->row];
    uint8_t level_flags = USFM_LEVEL(t->tok.level_flags);
    uint32_t node;
    USFM_Span arg;
    usfm__trim_last_text(p);
    usfm__close_to_block(p, 0);
    if (kind == USFM_AST_PARA && (row->props & USFM_MP_HEADING)) level_flags |= USFM_NF_HEADING;
    node = usfm__open_tok(p, kind, t, level_flags, 0);
    if (node == USFM_NIL || !(row->props & USFM_MP_ARG_WORD)) return;
    if (usfm__take_argument(p, &arg, usfm__tok_end(t)) && t->tok.marker == USFM_M_USFM) {
        usfm__set_version(p, arg);
    }
}

static void usfm__on_id(USFM_Parser *p, const USFM_ScanToken *t)
{
    USFM_Ast *ast = p->ast;
    USFM_Span arg;
    int has;
    usfm__trim_last_text(p);
    usfm__close_to_depth(p, 1);
    usfm__end_division(p, p->ast->node_count);
    if (usfm__open_tok(p, USFM_AST_HEADER, t, 0, 0) == USFM_NIL) return;
    has = usfm__take_argument(p, &arg, usfm__tok_end(t));
    if (!has || !usfm__valid_book_code(ast->input, arg)) {
        usfm__diag(p, USFM_DIAGNOSTIC_INVALID_BOOK_CODE, arg, USFM_M_ID);
    }
    if (has && usfm__valid_book_code(ast->input, arg) && ast->book_code[0] == '\0') {
        memcpy(ast->book_code, ast->input + arg.offset, 3);
        ast->book_code[3] = '\0';
    }
}

static void usfm__on_chapter(USFM_Parser *p, const USFM_ScanToken *t)
{
    USFM_Span arg;
    uint32_t node, number = 0, i;
    int valid;
    usfm__trim_last_text(p);
    usfm__close_to_depth(p, 1);
    node = usfm__open_tok(p, USFM_AST_CHAPTER, t, 0, 0);
    if (node == USFM_NIL) return;
    valid = usfm__take_argument(p, &arg, usfm__tok_end(t));
    usfm__close_top(p);
    i = arg.offset;
    valid = valid && usfm__read_uint(p->ast->input, &i, arg.offset + arg.length, &number) &&
            i == arg.offset + arg.length && number > 0;
    if (!valid) {
        usfm__diag(p, USFM_DIAGNOSTIC_INVALID_NUMBER, arg, USFM_M_C);
        number = 0;
    }
    usfm__add_chapter(p, node, number);
}

static void usfm__on_verse(USFM_Parser *p, const USFM_ScanToken *t)
{
    USFM_Ast *ast = p->ast;
    USFM_VerseRef ref;
    USFM_Span arg;
    uint32_t node, note;
    uint8_t k;
    usfm__trim_last_text(p);
    /* A verse can't be inside a note: the note was left open. */
    note = usfm__find_kind(p, USFM_AST_NOTE);
    if (note != 0) usfm__close_to_depth(p, note - 1);
    /* Character spans end at a verse. Bounded by the stack depth. */
    while (p->depth > 1) {
        k = usfm__top_kind(p);
        if (k != USFM_AST_CHAR && k != USFM_AST_MILESTONE && k != USFM_AST_FIGURE) break;
        usfm__close_implicit(p);
    }
    /* A verse inside a heading closes the heading. */
    k = usfm__top_kind(p);
    if (p->depth > 1 &&
        (k == USFM_AST_HEADER ||
         (k == USFM_AST_PARA && (ast->nodes[p->stack[p->depth - 1].node].level_flags & USFM_NF_HEADING)))) {
        usfm__close_implicit(p);
    }
    usfm__ensure_inline(p, t->tok.span.offset);
    node = usfm__open_tok(p, USFM_AST_VERSE, t, 0, 0);
    if (node == USFM_NIL) return;
    memset(&ref, 0, sizeof ref);
    if (!usfm__take_argument(p, &arg, usfm__tok_end(t)) ||
        !usfm__parse_verse(ast->input, arg, &ref)) {
        usfm__diag(p, USFM_DIAGNOSTIC_INVALID_NUMBER, arg, USFM_M_V);
    }
    usfm__close_top(p);
    /* No open chapter (before the first \c, or after \id / \periph):
     * the verse belongs to no chapter. */
    if (!p->chapter_open) {
        usfm__diag(p, USFM_DIAGNOSTIC_VERSE_BEFORE_CHAPTER, t->tok.span, USFM_M_V);
    } else {
        ref.chapter = ast->chapters[ast->chapter_count - 1].number;
    }
    usfm__add_verse(p, node, &ref);
}

/* Name bytes of a marker node: after "\" and an optional "+". */
static void usfm__node_name(const USFM_Parser *p, uint32_t node, uint32_t *offset,
                            uint32_t *length)
{
    const char *in = p->ast->input;
    uint32_t len = p->ast->input_length;
    uint32_t i = p->ast->nodes[node].span.offset + 1, start;
    if (i < len && in[i] == '+') i++;
    start = i;
    while (i < len && (USFM_ByteClass[(uint8_t)in[i]] & USFM_BC_NAME)) i++;
    *offset = start;
    *length = i - start;
}

/* Does end marker `closer` close a span of `marker`? Unknown markers
 * match by name. */
static int usfm__closer_matches(const USFM_Parser *p, const USFM_ScanToken *closer,
                                USFM_Marker marker, uint32_t name_offset, uint32_t name_length)
{
    if (closer->name_length == 0) return 0;
    if (marker != USFM_M_UNKNOWN) return closer->tok.marker == marker;
    return closer->tok.marker == USFM_M_UNKNOWN && closer->name_length == name_length &&
           memcmp(p->ast->input + closer->name_offset, p->ast->input + name_offset,
                  name_length) == 0;
}

static int usfm__frame_matches(const USFM_Parser *p, const USFM_Frame *f,
                               const USFM_ScanToken *closer)
{
    const USFM_Node *n = &p->ast->nodes[f->node];
    uint32_t offset = 0, length = 0;
    if (n->marker == USFM_M_UNKNOWN) usfm__node_name(p, f->node, &offset, &length);
    return usfm__closer_matches(p, closer, n->marker, offset, length);
}

/* 32-bit FNV-1a of an unknown marker's name bytes. */
static uint32_t usfm__name_hash(const char *name, uint32_t length)
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < length; i++) {
        h ^= (uint8_t)name[i];
        h *= 16777619u;
    }
    return h;
}

/* Only spaces and tabs between the previous line break (or the start of
 * the input) and `offset`? Bounded by `offset`. */
static int usfm__at_line_start(const char *in, uint32_t offset)
{
    while (offset > 0 && (in[offset - 1] == ' ' || in[offset - 1] == '\t')) offset--;
    return offset == 0 || in[offset - 1] == '\n' || in[offset - 1] == '\r';
}

/* Is the byte run at `after` a "|" or a "\*"? That makes an unknown
 * marker the start of a milestone. */
static int usfm__milestone_follows(const char *in, uint32_t len, uint32_t after)
{
    /* Extra blanks may sit before the "|" or "\*". Bounded by the input;
     * the blanks are scanned again only as the following text token. */
    while (after < len && (in[after] == ' ' || in[after] == '\t')) after++;
    return after < len &&
           (in[after] == '|' || (in[after] == '\\' && after + 1 < len && in[after + 1] == '*'));
}

/* Will the parser turn unknown marker token `t` into a paragraph? Yes at
 * line start unless it is followed by "|" or "\*" (then it is a
 * milestone). The one place this is decided: usfm__on_unknown and
 * usfm__is_segment_stop both call it, so lookahead and parser agree. */
static int usfm__unknown_starts_para(const USFM_Parser *p, const USFM_ScanToken *t)
{
    const char *in = p->ast->input;
    return usfm__at_line_start(in, t->tok.span.offset) &&
           !usfm__milestone_follows(in, p->ast->input_length, usfm__tok_end(t));
}

/* Does marker token `t` end every open character span (the end of a
 * lookahead segment)? A known marker whose role is not character-level,
 * a note part or note end marker inside a note, and an unknown marker
 * that the parser will turn into a paragraph (at line start, not
 * followed by "|" or "\*"). */
static int usfm__is_segment_stop(const USFM_Parser *p, const USFM_ScanToken *t, int in_note)
{
    uint8_t role;
    if (t->tok.kind == USFM_TOK_MARKER_END) {
        return in_note && t->row != USFM_ROW_NONE &&
               USFM_MarkerRows[t->row].role == USFM_ROLE_NOTE;
    }
    if (t->tok.kind != USFM_TOK_MARKER) return 0;
    if (t->row == USFM_ROW_NONE) return usfm__unknown_starts_para(p, t);
    role = USFM_MarkerRows[t->row].role;
    if (role == USFM_ROLE_CHAR || role == USFM_ROLE_NOTE || role == USFM_ROLE_FIGURE ||
        role == USFM_ROLE_MILESTONE) {
        return 0;
    }
    return role != USFM_ROLE_NOTE_PART || in_note;
}

/* Records an unknown marker's closer at `offset` in c->names. Probing is
 * bounded by USFM__NAME_SLOTS. */
static void usfm__record_name(USFM_CloserTable *c, uint32_t hash, uint32_t offset)
{
    uint32_t i, slot;
    for (i = 0; i < USFM__NAME_SLOTS; i++) {
        USFM_NameSlot *n;
        slot = (hash + i) & (USFM__NAME_SLOTS - 1u);
        n = &c->names[slot];
        if (n->stamp != c->generation) {
            n->hash = hash;
            n->last = offset;
            n->stamp = c->generation;
            return;
        }
        if (n->hash == hash) {
            n->last = offset;
            return;
        }
    }
    c->name_overflow = 1;               /* table full: name not recorded */
}

/* Offset of the last closer recorded for an unknown name, or 0 with
 * *found = 0. Bounded by USFM__NAME_SLOTS. */
static uint32_t usfm__lookup_name(const USFM_CloserTable *c, uint32_t hash, int *found)
{
    uint32_t i, slot;
    *found = 0;
    for (i = 0; i < USFM__NAME_SLOTS; i++) {
        const USFM_NameSlot *n;
        slot = (hash + i) & (USFM__NAME_SLOTS - 1u);
        n = &c->names[slot];
        if (n->stamp != c->generation) return 0;     /* empty: not present */
        if (n->hash == hash) {
            *found = 1;
            return n->last;
        }
    }
    return 0;
}

/* Scans one segment: from the current scan position to the first stop
 * token (usfm__is_segment_stop), recording the offset of the last end
 * marker of each marker id, and of each unknown name, in it. Scans a
 * copy of the scanner, so it consumes nothing. Bounded by the input
 * length. */
static void usfm__scan_segment(USFM_Parser *p, USFM_CloserTable *c, int in_note)
{
    USFM_Scanner s = p->scan;
    USFM_ScanToken t;
    c->generation++;                    /* invalidates every entry */
    c->start = p->scan.position;
    c->stop = p->ast->input_length;
    c->name_overflow = 0;
    s.pipe_is_attr = 0;
    while (usfm__scan_next(&s, &t)) {
        if (usfm__is_segment_stop(p, &t, in_note)) {
            c->stop = t.tok.span.offset;
            return;
        }
        if (t.tok.kind != USFM_TOK_MARKER_END || t.name_length == 0) continue;
        if (t.tok.marker == USFM_M_UNKNOWN) {
            usfm__record_name(c, usfm__name_hash(p->ast->input + t.name_offset, t.name_length),
                              t.tok.span.offset);
            continue;
        }
        USFM_ASSERT((uint32_t)t.tok.marker < USFM__MARKER_ID_COUNT);
        c->last_closer[t.tok.marker] = t.tok.span.offset;
        c->stamp[t.tok.marker] = c->generation;
    }
}

/* Does the end marker for (marker, name) appear before the span must
 * end anyway: at a paragraph-level marker, a verse, a cell, or (inside
 * a note) the next note part or the note's end?
 *
 * Each segment is scanned once (usfm__scan_segment) into a per-segment
 * closer table, one for in_note = 0 and one for in_note = 1, and every
 * question about a position inside it is answered in O(1): the closer is
 * ahead if the last one in the segment starts at or after the current
 * position. Scan positions only grow, so a rescan starts past the
 * previous segment's stop and the scans cost O(input length) in total.
 *
 * Unknown markers match by a 32-bit FNV-1a hash of their name; two
 * different names with equal hashes (probability about 2^-32 per pair)
 * would be treated as the same name. If a segment has more distinct
 * unknown closer names than USFM__NAME_SLOTS, the extra names are not
 * recorded and a question about one answers "no": deterministic and
 * conservative (the span is closed, or the marker stays a milestone). */
static int usfm__closer_ahead(USFM_Parser *p, USFM_Marker marker, uint32_t name_offset,
                              uint32_t name_length)
{
    int in_note = usfm__find_kind(p, USFM_AST_NOTE) != 0;
    USFM_CloserTable *c = &p->closers[in_note];
    uint32_t pos = p->scan.position, last;
    int found;
    if (c->generation == 0 || pos < c->start || pos > c->stop) {
        usfm__scan_segment(p, c, in_note);
    }
    if (marker != USFM_M_UNKNOWN) {
        USFM_ASSERT((uint32_t)marker < USFM__MARKER_ID_COUNT);
        return c->stamp[marker] == c->generation && c->last_closer[marker] >= pos;
    }
    if (name_length == 0) return 0;
    last = usfm__lookup_name(c, usfm__name_hash(p->ast->input + name_offset, name_length),
                             &found);
    return found && last >= pos;
}

/* A plain (non-\+) character marker inside open character spans closes
 * them, innermost first, until one whose end marker is still ahead
 * (usfm__closer_ahead); it nests inside that one. Note parts are never
 * closed here. Bounded by the stack depth. */
static void usfm__on_char(USFM_Parser *p, const USFM_ScanToken *t)
{
    usfm__ensure_inline(p, t->tok.span.offset);
    if (!(t->tok.level_flags & USFM_TF_NESTED)) {
        while (p->depth > 1) {
            const USFM_Frame *top = &p->stack[p->depth - 1];
            const USFM_Node *outer = &p->ast->nodes[top->node];
            uint32_t offset = 0, length = 0;
            if (top->kind != USFM_AST_CHAR || (top->flags & USFM_FRAME_NOTE_PART)) break;
            if (outer->marker == USFM_M_UNKNOWN) usfm__node_name(p, top->node, &offset, &length);
            if (usfm__closer_ahead(p, outer->marker, offset, length)) break;
            usfm__close_implicit(p);
        }
    }
    usfm__open_tok(p, USFM_AST_CHAR, t, t->tok.level_flags, 0);
}

static void usfm__on_attributes(USFM_Parser *p, const USFM_ScanToken *t)
{
    uint8_t k = usfm__top_kind(p);
    if (k == USFM_AST_CHAR || k == USFM_AST_MILESTONE || k == USFM_AST_FIGURE ||
        k == USFM_AST_PERIPHERAL) {
        usfm__append(p, USFM_AST_ATTRIBUTES, t->tok.span, USFM_M_NONE, 0);
    } else {
        usfm__on_text(p, t);
    }
}

static void usfm__on_note(USFM_Parser *p, const USFM_ScanToken *t)
{
    USFM_Span arg;
    uint32_t note = usfm__find_kind(p, USFM_AST_NOTE);
    if (note != 0) usfm__close_to_depth(p, note - 1);   /* notes can't nest */
    /* No implied paragraph: USFM 3.1 allows notes directly after \c. */
    if (usfm__open_tok(p, USFM_AST_NOTE, t, USFM_LEVEL(t->tok.level_flags), 0) == USFM_NIL) return;
    if (!usfm__take_argument(p, &arg, usfm__tok_end(t))) {
        usfm__diag(p, USFM_DIAGNOSTIC_MISSING_CALLER, arg, t->tok.marker);
    }
}

static void usfm__on_note_part(USFM_Parser *p, const USFM_ScanToken *t)
{
    uint32_t note = usfm__find_kind(p, USFM_AST_NOTE);
    if (note == 0) {                    /* \xt outside a note (3.1) */
        usfm__on_char(p, t);
        return;
    }
    /* Close the previous part and every span opened inside it. */
    usfm__close_to_depth(p, note);
    usfm__open_tok(p, USFM_AST_CHAR, t, USFM_LEVEL(t->tok.level_flags), USFM_FRAME_NOTE_PART);
}

static void usfm__on_close(USFM_Parser *p, const USFM_ScanToken *t)
{
    uint32_t d, end = usfm__tok_end(t);
    if (t->name_length == 0) {          /* "\*" ends a milestone */
        if (usfm__top_kind(p) == USFM_AST_MILESTONE) {
            p->ast->nodes[p->stack[p->depth - 1].node].level_flags |= USFM_TF_SELF_CLOSED;
            if (end > p->last_end) p->last_end = end;
            usfm__close_top(p);
        } else {
            usfm__diag(p, USFM_DIAGNOSTIC_UNEXPECTED_CLOSE, t->tok.span, USFM_M_NONE);
        }
        return;
    }
    /* Search open spans in the current paragraph or note. Bounded by
     * the stack depth. */
    for (d = p->depth; d > 1; d--) {
        const USFM_Frame *f = &p->stack[d - 1];
        if (f->kind != USFM_AST_CHAR && f->kind != USFM_AST_NOTE && f->kind != USFM_AST_FIGURE) break;
        if (usfm__frame_matches(p, f, t)) {
            while (p->depth > d) usfm__close_implicit(p);
            if (end > p->last_end) p->last_end = end;
            usfm__close_top(p);
            return;
        }
        if (f->kind == USFM_AST_NOTE) break;
    }
    /* \fr* after \fr already closed: explicit close of a note part. */
    if (t->row != USFM_ROW_NONE && USFM_MarkerRows[t->row].role == USFM_ROLE_NOTE_PART) return;
    usfm__diag(p, USFM_DIAGNOSTIC_UNEXPECTED_CLOSE, t->tok.span, t->tok.marker);
}

static void usfm__on_milestone(USFM_Parser *p, const USFM_ScanToken *t)
{
    /* Open until "\*" or any other token arrives (see usfm__parse).
     * Milestones may sit anywhere, so there is no implied paragraph. */
    usfm__open_tok(p, USFM_AST_MILESTONE, t, t->tok.level_flags, 0);
}

static void usfm__on_row(USFM_Parser *p, const USFM_ScanToken *t)
{
    usfm__trim_last_text(p);
    usfm__close_to_block(p, 1);
    if (usfm__top_kind(p) != USFM_AST_TABLE) {
        usfm__open(p, USFM_AST_TABLE, usfm__span(t->tok.span.offset, 0), USFM_M_NONE, 0, 0);
    }
    usfm__open_tok(p, USFM_AST_ROW, t, USFM_LEVEL(t->tok.level_flags), 0);
}

static void usfm__on_cell(USFM_Parser *p, const USFM_ScanToken *t)
{
    uint32_t row;
    usfm__trim_last_text(p);
    row = usfm__find_kind(p, USFM_AST_ROW);
    if (row == 0) {
        USFM_Span at = usfm__span(t->tok.span.offset, 0);
        usfm__diag(p, USFM_DIAGNOSTIC_INVALID_NESTING, t->tok.span, t->tok.marker);
        usfm__close_to_block(p, 1);
        if (usfm__top_kind(p) != USFM_AST_TABLE) {
            usfm__open(p, USFM_AST_TABLE, at, USFM_M_NONE, 0, 0);
        }
        usfm__open(p, USFM_AST_ROW, at, USFM_M_TR, 0, 0);
    } else {
        usfm__close_to_depth(p, row);   /* the previous cell */
    }
    usfm__open_tok(p, USFM_AST_CELL, t, USFM_LEVEL(t->tok.level_flags), 0);
}

static void usfm__on_sidebar(USFM_Parser *p, const USFM_ScanToken *t)
{
    uint32_t open = usfm__find_kind(p, USFM_AST_SIDEBAR);
    usfm__trim_last_text(p);
    if (open != 0) usfm__close_to_depth(p, open - 1);   /* sidebars can't nest */
    usfm__close_to_block(p, 0);
    usfm__open_tok(p, USFM_AST_SIDEBAR, t, 0, 0);
}

static void usfm__on_sidebar_end(USFM_Parser *p, const USFM_ScanToken *t)
{
    uint32_t open = usfm__find_kind(p, USFM_AST_SIDEBAR);
    uint32_t end = usfm__tok_end(t);
    usfm__trim_last_text(p);
    if (open == 0) {
        usfm__diag(p, USFM_DIAGNOSTIC_UNEXPECTED_CLOSE, t->tok.span, t->tok.marker);
        return;
    }
    usfm__close_to_depth(p, open);
    if (end > p->last_end) p->last_end = end;
    usfm__close_top(p);
}

static void usfm__on_periph(USFM_Parser *p, const USFM_ScanToken *t)
{
    USFM_Span arg;
    usfm__trim_last_text(p);
    usfm__close_to_depth(p, 1);
    usfm__end_division(p, p->ast->node_count);
    if (usfm__open_tok(p, USFM_AST_PERIPHERAL, t, 0, 0) == USFM_NIL) return;
    usfm__take_argument(p, &arg, usfm__tok_end(t));
}

static void usfm__on_figure(USFM_Parser *p, const USFM_ScanToken *t)
{
    usfm__open_tok(p, USFM_AST_FIGURE, t, 0, 0);
}

/* Guide section 2: followed by "\*" or "|" -> milestone; at line start
 * -> paragraph (usfm__unknown_starts_para); a matching \name* ahead ->
 * character span; otherwise an inline milestone leaf. The text after it
 * is never dropped. */
static void usfm__on_unknown(USFM_Parser *p, const USFM_ScanToken *t)
{
    const char *in = p->ast->input;
    if (t->name_length == 0 || in[t->name_offset] != 'z') {
        usfm__diag(p, USFM_DIAGNOSTIC_UNKNOWN_MARKER, t->tok.span, USFM_M_UNKNOWN);
    }
    if (usfm__unknown_starts_para(p, t)) {
        usfm__trim_last_text(p);
        usfm__close_to_block(p, 0);
        usfm__open_tok(p, USFM_AST_PARA, t, 0, 0);
        return;
    }
    if (usfm__milestone_follows(in, p->ast->input_length, usfm__tok_end(t))) {
        usfm__on_milestone(p, t);
        return;
    }
    if (t->name_length > 0 &&
        usfm__closer_ahead(p, USFM_M_UNKNOWN, t->name_offset, t->name_length)) {
        usfm__on_char(p, t);
        return;
    }
    usfm__append(p, USFM_AST_MILESTONE, t->tok.span, USFM_M_UNKNOWN, 0);
}

static void usfm__on_marker(USFM_Parser *p, USFM_ScanToken *t)
{
    if (USFM_FLAGS(t->tok.level_flags) & (USFM_TF_MILESTONE_START | USFM_TF_MILESTONE_END)) {
        usfm__on_milestone(p, t);
        return;
    }
    if (t->row == USFM_ROW_NONE) {
        usfm__on_unknown(p, t);
        return;
    }
    switch (USFM_MarkerRows[t->row].role) {
    case USFM_ROLE_PARA: usfm__on_para(p, t, USFM_AST_PARA); break;
    case USFM_ROLE_HEADER: usfm__on_para(p, t, USFM_AST_HEADER); break;
    case USFM_ROLE_ID: usfm__on_id(p, t); break;
    case USFM_ROLE_CHAPTER: usfm__on_chapter(p, t); break;
    case USFM_ROLE_VERSE: usfm__on_verse(p, t); break;
    case USFM_ROLE_CHAR: usfm__on_char(p, t); break;
    case USFM_ROLE_NOTE: usfm__on_note(p, t); break;
    case USFM_ROLE_NOTE_PART: usfm__on_note_part(p, t); break;
    case USFM_ROLE_ROW: usfm__on_row(p, t); break;
    case USFM_ROLE_CELL: usfm__on_cell(p, t); break;
    case USFM_ROLE_MILESTONE: usfm__on_milestone(p, t); break;
    case USFM_ROLE_SIDEBAR: usfm__on_sidebar(p, t); break;
    case USFM_ROLE_SIDEBAR_END: usfm__on_sidebar_end(p, t); break;
    case USFM_ROLE_PERIPH: usfm__on_periph(p, t); break;
    case USFM_ROLE_FIGURE: usfm__on_figure(p, t); break;
    default: usfm__on_unknown(p, t); break;
    }
}

/* The main loop. Bounded: every token consumes at least one input byte,
 * and a pushed-back token is consumed by the next iteration. */
static void usfm__parse(USFM_Parser *p)
{
    USFM_ScanToken t;
    while (!p->failed && usfm__next(p, &t)) {
        if (usfm__top_kind(p) == USFM_AST_MILESTONE) {
            int keep = t.tok.kind == USFM_TOK_ATTRIBUTES ||
                       (t.tok.kind == USFM_TOK_MARKER_END && t.name_length == 0) ||
                       (t.tok.kind == USFM_TOK_TEXT && usfm__is_blank(p->ast->input, t.tok.span));
            if (!keep) usfm__close_top(p);
        }
        switch (t.tok.kind) {
        case USFM_TOK_TEXT: usfm__on_text(p, &t); break;
        case USFM_TOK_ARGUMENT: usfm__on_text(p, &t); break;   /* unclaimed */
        case USFM_TOK_ATTRIBUTES: usfm__on_attributes(p, &t); break;
        case USFM_TOK_OPTBREAK: usfm__on_optbreak(p, &t); break;
        case USFM_TOK_MARKER_END: usfm__on_close(p, &t); break;
        case USFM_TOK_MARKER: usfm__on_marker(p, &t); break;
        default: break;
        }
    }
}

static void usfm__finish(USFM_Parser *p)
{
    USFM_Ast *ast = p->ast;
    usfm__trim_last_text(p);
    while (p->depth > 1) usfm__close_implicit(p);
    ast->nodes[0].end = ast->node_count;
    if (ast->chapter_count > 0 && ast->chapters[ast->chapter_count - 1].end_node == USFM_NIL) {
        ast->chapters[ast->chapter_count - 1].end_node = ast->node_count;
    }
    if (ast->verse_count > 0 && ast->verses[ast->verse_count - 1].end_node == USFM_NIL) {
        ast->verses[ast->verse_count - 1].end_node = ast->node_count;
    }
}

USFM_API USFM_Ast *USFM_Ast_ParseOpts(const char *input, uint32_t input_length,
                                      const USFM_Config *config, uint32_t flags,
                                      const USFM_Alc *alc_in, USFM_Err *err)
{
    const USFM_Alc *alc = usfm__alc_or_default(alc_in);
    USFM_Ast *ast;
    USFM_Parser p;
    const char *text = input != NULL ? input : "";
    uint8_t version = config != NULL ? config->version : (uint8_t)USFM_VERSION_AUTO;
    void *grown;

    usfm__set_err(err, USFM_OK, 0);
    if (alc == NULL) {
        usfm__set_err(err, USFM_ERR_NO_ALLOCATOR, 0);
        return NULL;
    }
    if ((input == NULL && input_length > 0) || version > USFM_VERSION_3_1) {
        usfm__set_err(err, USFM_ERR_INVALID_ARG, 0);
        return NULL;
    }
    if (input_length > USFM_INPUT_MAX) {
        usfm__set_err(err, USFM_ERR_INPUT_TOO_LARGE, 0);
        return NULL;
    }
    ast = (USFM_Ast *)usfm__alloc(alc, sizeof(USFM_Ast));
    if (ast == NULL) {
        usfm__set_err(err, USFM_ERR_NOMEM, 0);
        return NULL;
    }
    memset(ast, 0, sizeof *ast);
    ast->alc = *alc;
    ast->chapters_sorted = 1;
    ast->verses_sorted = 1;
    if (input_length == 0) {
        text = ""; /* never hand back a caller pointer for an empty input */
    } else if (flags & USFM_PARSE_COPY_INPUT) {
        ast->owned_input = (char *)usfm__alloc(alc, input_length);
        if (ast->owned_input == NULL) {
            USFM_Ast_Free(ast);
            usfm__set_err(err, USFM_ERR_NOMEM, 0);
            return NULL;
        }
        memcpy(ast->owned_input, input, input_length);
        text = ast->owned_input;
    }
    ast->input = text;
    ast->input_length = input_length;

    /* Initial node capacity from the input length (WEB averages about
     * one node per 12 bytes); grown x2 as needed. */
    grown = usfm__grow(alc, NULL, &ast->node_capacity, sizeof(USFM_Node), input_length / 8u + 16u);
    if (grown == NULL) {
        USFM_Ast_Free(ast);
        usfm__set_err(err, USFM_ERR_NOMEM, 0);
        return NULL;
    }
    ast->nodes = (USFM_Node *)grown;

    memset(&p, 0, sizeof p);
    p.ast = ast;
    p.version_auto = version == USFM_VERSION_AUTO;
    p.version = p.version_auto ? (uint8_t)USFM_VERSION_3_1 : version;
    usfm__scan_init(&p.scan, text, input_length);
    usfm__open(&p, USFM_AST_BOOK, usfm__span(0, input_length), USFM_M_NONE, 0, 0);
    /* The BOOK span covers the whole input, which would otherwise make
     * every later container stretch to the end of the input on close. */
    p.last_end = 0;
    usfm__parse(&p);
    if (!p.failed) usfm__finish(&p);
    if (p.failed) {
        uint32_t pos = p.scan.position;
        USFM_Ast_Free(ast);
        usfm__set_err(err, USFM_ERR_NOMEM, pos);
        return NULL;
    }
    ast->version = p.version;
    if (ast->dropped_diagnostics > 0) {
        usfm__set_err(err, USFM_ERR_NOMEM, ast->first_dropped_offset);
    }
    return ast;
}

USFM_API USFM_Ast *USFM_Ast_Parse(const char *input, uint32_t input_length,
                                  const USFM_Config *config, USFM_Err *err)
{
    return USFM_Ast_ParseOpts(input, input_length, config, USFM_PARSE_DEFAULT, NULL, err);
}

/* ===== Lookups ===== */

USFM_API uint8_t USFM_Node_GetLevel(const USFM_Node *node) { return USFM_LEVEL(node->level_flags); }
USFM_API uint8_t USFM_Node_GetFlags(const USFM_Node *node) { return USFM_FLAGS(node->level_flags); }
USFM_API int USFM_Node_HasFlags(const USFM_Node *node, uint32_t flags)
{
    return (USFM_FLAGS(node->level_flags) & flags) == flags;
}

USFM_API const char *USFM_Ast_GetInput(const USFM_Ast *ast, uint32_t *length)
{
    if (length) *length = ast ? ast->input_length : 0;
    return ast ? ast->input : NULL;
}

USFM_API const USFM_Diagnostic *USFM_Ast_GetDiagnostics(const USFM_Ast *ast, uint32_t *count)
{
    if (count) *count = ast ? ast->diagnostic_count : 0;
    return ast ? ast->diagnostics : NULL;
}

USFM_API uint32_t USFM_Ast_GetDroppedDiagnosticCount(const USFM_Ast *ast)
{
    return ast ? ast->dropped_diagnostics : 0;
}

USFM_API const USFM_Node *USFM_Ast_GetNodes(const USFM_Ast *ast, uint32_t *count)
{
    if (count) *count = ast ? ast->node_count : 0;
    return ast ? ast->nodes : NULL;
}

USFM_API const USFM_ChapterEntry *USFM_Ast_GetChapters(const USFM_Ast *ast, uint32_t *count)
{
    if (count) *count = ast ? ast->chapter_count : 0;
    return ast ? ast->chapters : NULL;
}

USFM_API const USFM_VerseEntry *USFM_Ast_GetVerses(const USFM_Ast *ast, uint32_t *count)
{
    if (count) *count = ast ? ast->verse_count : 0;
    return ast ? ast->verses : NULL;
}

USFM_API uint32_t USFM_Ast_GetMaxChapterNumber(const USFM_Ast *ast)
{
    return ast ? ast->max_chapter : 0;
}

USFM_API const char *USFM_Ast_GetBookCode(const USFM_Ast *ast)
{
    return ast ? ast->book_code : "";
}

USFM_API uint8_t USFM_Ast_GetVersion(const USFM_Ast *ast)
{
    return ast ? ast->version : (uint8_t)USFM_VERSION_AUTO;
}

static uint32_t usfm__find_chapter_index(const USFM_Ast *ast, uint16_t number)
{
    uint32_t lo = 0, hi = ast->chapter_count, i;
    if (ast->chapters_sorted) {
        /* Bounded: the range halves each pass. */
        while (lo < hi) {
            uint32_t mid = lo + (hi - lo) / 2u;
            if (ast->chapters[mid].number < number) lo = mid + 1;
            else hi = mid;
        }
        return (lo < ast->chapter_count && ast->chapters[lo].number == number) ? lo : USFM_NIL;
    }
    /* Bounded by chapter_count. */
    for (i = 0; i < ast->chapter_count; i++) {
        if (ast->chapters[i].number == number) return i;
    }
    return USFM_NIL;
}

USFM_API uint32_t USFM_Ast_FindChapter(const USFM_Ast *ast, uint16_t chapter,
                                       USFM_ChapterEntry *out)
{
    uint32_t index;
    if (ast == NULL) return USFM_NIL;
    index = usfm__find_chapter_index(ast, chapter);
    if (index == USFM_NIL) return USFM_NIL;
    if (out) *out = ast->chapters[index];
    return ast->chapters[index].node;
}

USFM_API uint32_t USFM_Ast_FindVerse(const USFM_Ast *ast, uint16_t chapter, uint16_t verse,
                                     USFM_VerseEntry *out)
{
    uint32_t ci, first, end, i, found = USFM_NIL;
    if (ast == NULL) return USFM_NIL;
    ci = usfm__find_chapter_index(ast, chapter);
    if (ci == USFM_NIL) return USFM_NIL;
    first = ast->chapters[ci].first_verse;
    end = first + ast->chapters[ci].verse_count;
    if (ast->verses_sorted) {
        /* Lower bound: the first entry whose verse_last >= verse (this
         * is the first segment of a split verse), then check its start.
         * Bounded: the range halves each pass. */
        uint32_t lo = first, hi = end;
        while (lo < hi) {
            uint32_t mid = lo + (hi - lo) / 2u;
            if (ast->verses[mid].ref.verse_last < verse) lo = mid + 1;
            else hi = mid;
        }
        if (lo < end && ast->verses[lo].ref.verse_first <= verse) found = lo;
    } else {
        /* Bounded by verse_count. */
        for (i = first; i < end; i++) {
            if (ast->verses[i].ref.verse_first <= verse && verse <= ast->verses[i].ref.verse_last) {
                found = i;
                break;
            }
        }
    }
    if (found == USFM_NIL) return USFM_NIL;
    if (out) *out = ast->verses[found];
    return ast->verses[found].node;
}

/* ===== Iterators ===== */

USFM_API void USFM_ChapterIter_Init(USFM_ChapterIter *it, const USFM_Ast *ast)
{
    it->ast = ast;
    it->position = 0;
    it->end = ast ? ast->chapter_count : 0;
}

USFM_API int USFM_ChapterIter_Next(USFM_ChapterIter *it, USFM_ChapterEntry *chapter)
{
    if (it->ast == NULL || it->position >= it->end) return 0;
    *chapter = it->ast->chapters[it->position++];
    return 1;
}

USFM_API void USFM_ChapterVerseIter_Init(USFM_ChapterVerseIter *it, const USFM_Ast *ast,
                                         const USFM_ChapterEntry *chapter)
{
    it->ast = ast;
    it->position = 0;
    it->end = 0;
    if (ast == NULL || chapter == NULL) return;
    it->position = chapter->first_verse;
    it->end = chapter->first_verse + chapter->verse_count;
    if (it->end > ast->verse_count) it->end = ast->verse_count;
}

USFM_API int USFM_ChapterVerseIter_Next(USFM_ChapterVerseIter *it, USFM_VerseEntry *verse)
{
    if (it->ast == NULL || it->position >= it->end) return 0;
    *verse = it->ast->verses[it->position++];
    return 1;
}

USFM_API void USFM_VerseIter_Init(USFM_VerseIter *it, const USFM_Ast *ast,
                                  const USFM_VerseEntry *entry, uint32_t flags)
{
    it->ast = ast;
    it->flags = flags;
    it->position = 0;
    it->end = 0;
    if (ast == NULL || entry == NULL || entry->node >= ast->node_count) return;
    it->position = ast->nodes[entry->node].end;   /* skip VERSE + ARGUMENT */
    it->end = entry->end_node <= ast->node_count ? entry->end_node : ast->node_count;
}

static void usfm__verse_item(const USFM_Ast *ast, uint32_t node, uint8_t kind,
                             USFM_VerseItem *item)
{
    const USFM_Node *nodes = ast->nodes;
    uint32_t a = nodes[node].parent, steps;
    memset(item, 0, sizeof *item);
    item->kind = kind;
    item->node = node;
    item->para = kind == USFM_VITEM_PARA_BREAK ? node : USFM_NIL;
    item->style = USFM_NIL;
    item->text = (kind == USFM_VITEM_TEXT || kind == USFM_VITEM_OPTBREAK)
                     ? nodes[node].span : usfm__span(nodes[node].span.offset, 0);
    /* Bounded: a node has at most USFM_STACK_DEPTH ancestors. */
    for (steps = 0; a != USFM_NIL && steps <= USFM_STACK_DEPTH; steps++) {
        uint8_t k = nodes[a].kind;
        if (k == USFM_AST_CHAR && item->style == USFM_NIL) item->style = a;
        if (k == USFM_AST_NOTE) item->in_note = 1;
        if (k == USFM_AST_PARA && item->para == USFM_NIL) item->para = a;
        a = nodes[a].parent;
    }
}

USFM_API int USFM_VerseIter_Next(USFM_VerseIter *it, USFM_VerseItem *item)
{
    const USFM_Ast *ast = it->ast;
    if (ast == NULL) return 0;
    /* Bounded: position strictly increases and never passes end. */
    while (it->position < it->end) {
        uint32_t i = it->position;
        const USFM_Node *n = &ast->nodes[i];
        uint32_t skip_to = n->end < it->end ? n->end : it->end;
        switch (n->kind) {
        case USFM_AST_TEXT:
            it->position = i + 1;
            usfm__verse_item(ast, i, USFM_VITEM_TEXT, item);
            return 1;
        case USFM_AST_OPTBREAK:
            it->position = i + 1;
            usfm__verse_item(ast, i, USFM_VITEM_OPTBREAK, item);
            return 1;
        case USFM_AST_NOTE:
            it->position = (it->flags & USFM_VI_INCLUDE_NOTES) ? i + 1 : skip_to;
            if (it->flags & USFM_VI_REPORT_NOTES) {
                usfm__verse_item(ast, i, USFM_VITEM_NOTE, item);
                return 1;
            }
            break;
        case USFM_AST_PARA:
            if ((n->level_flags & USFM_NF_HEADING) && !(it->flags & USFM_VI_INCLUDE_HEADINGS)) {
                it->position = skip_to;
                break;
            }
            it->position = i + 1;
            if (it->flags & USFM_VI_REPORT_BREAKS) {
                usfm__verse_item(ast, i, USFM_VITEM_PARA_BREAK, item);
                return 1;
            }
            break;
        default:                        /* containers: step in; leaves:
                                           ARGUMENT / ATTRIBUTES skipped */
            it->position = i + 1;
            break;
        }
    }
    return 0;
}

#endif /* USFM_CODEGEN_BOOTSTRAP */
#endif /* USFM_IMPLEMENTATION_INCLUDED */
#endif /* USFM_IMPLEMENTATION */

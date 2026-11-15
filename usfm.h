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

/* ===== Errors and diagnostics ===== */

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

/* ===== Scanner ===== */

/* ===== AST handle ===== */

/* ===== Parser ===== */

/* ===== Lookups ===== */

/* ===== Iterators ===== */

#endif /* USFM_CODEGEN_BOOTSTRAP */
#endif /* USFM_IMPLEMENTATION_INCLUDED */
#endif /* USFM_IMPLEMENTATION */

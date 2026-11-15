#ifndef USFM_DUMP_H
#define USFM_DUMP_H
/* Text dumps of tokens, trees and diagnostics for the tests. Include
 * after usfm.h in a translation unit that defines USFM_IMPLEMENTATION. */
#include <stdio.h>
#include <string.h>

typedef struct DumpOut {
    char  *buf;
    size_t cap;
    size_t len;
} DumpOut;

static void dump_putc(DumpOut *o, char c)
{
    if (o->len + 1 < o->cap) o->buf[o->len++] = c;
    o->buf[o->len] = '\0';
}

static void dump_puts(DumpOut *o, const char *s)
{
    while (*s) dump_putc(o, *s++);
}

static void dump_quoted(DumpOut *o, const char *input, USFM_Span span)
{
    dump_putc(o, '"');
    for (uint32_t i = 0; i < span.length; i++) {
        char c = input[span.offset + i];
        if (c == '\n') dump_puts(o, "\\n");
        else if (c == '\r') dump_puts(o, "\\r");
        else if (c == '\t') dump_puts(o, "\\t");
        else if (c == '"') dump_puts(o, "\\\"");
        else dump_putc(o, c);
    }
    dump_putc(o, '"');
}

/* "q2", "k1", "s1e", "+nd", "qt1-s"; "?zfoo" for an unknown marker
 * (its name is read from the input after the backslash). marker_flags
 * says whether the high bits of level_flags are marker flags (they are
 * paragraph flags on PARA nodes). */
static void dump_marker(DumpOut *o, const char *input, uint32_t input_length,
                        uint32_t backslash, USFM_Marker marker,
                        uint8_t level_flags, int marker_flags)
{
    uint8_t level = USFM_LEVEL(level_flags);
    uint8_t flags = marker_flags ? USFM_FLAGS(level_flags) : 0;
    if (flags & USFM_TF_NESTED) dump_putc(o, '+');
    if (marker == USFM_M_UNKNOWN) {
        uint32_t i = backslash + 1;
        dump_putc(o, '?');
        if (i < input_length && input[i] == '+') i++;
        while (i < input_length && (USFM_ByteClass[(uint8_t)input[i]] & USFM_BC_NAME)) {
            dump_putc(o, input[i++]);
        }
    } else {
        const USFM_MarkerRow *base = NULL, *exact = NULL;
        size_t rows = sizeof USFM_MarkerRows / sizeof USFM_MarkerRows[0];
        for (size_t r = 0; r < rows; r++) {
            if (USFM_MarkerRows[r].marker != marker) continue;
            if (USFM_MarkerRows[r].level == 0 && base == NULL) base = &USFM_MarkerRows[r];
            if (level != 0 && USFM_MarkerRows[r].level == level && exact == NULL) {
                exact = &USFM_MarkerRows[r];
            }
        }
        if (exact != NULL) {
            dump_puts(o, exact->name);
        } else if (base != NULL) {
            dump_puts(o, base->name);
            if (level != 0) {
                char number[8];
                snprintf(number, sizeof number, "%u", (unsigned)level);
                dump_puts(o, number);
            }
        } else {
            dump_putc(o, '?');
        }
    }
    if (flags & USFM_TF_MILESTONE_START) dump_puts(o, "-s");
    if (flags & USFM_TF_MILESTONE_END) dump_puts(o, "-e");
}

/* Scans `input` to the end and writes one space-separated token list. */
static void dump_tokens(const char *input, int pipe_is_attr, char *out, size_t cap)
{
    DumpOut o;
    USFM_Scanner s;
    USFM_ScanToken t;
    uint32_t length = (uint32_t)strlen(input);
    int first = 1;
    o.buf = out;
    o.cap = cap;
    o.len = 0;
    out[0] = '\0';
    usfm__scan_init(&s, input, length);
    s.pipe_is_attr = (uint8_t)pipe_is_attr;
    while (usfm__scan_next(&s, &t)) {
        if (!first) dump_putc(&o, ' ');
        first = 0;
        switch (t.tok.kind) {
        case USFM_TOK_TEXT:
            dump_putc(&o, 'T');
            dump_quoted(&o, input, t.tok.span);
            if (USFM_FLAGS(t.tok.level_flags) != 0) {
                dump_putc(&o, '/');
                if (t.tok.level_flags & USFM_TF_HAS_ESCAPE) dump_putc(&o, 'e');
                if (t.tok.level_flags & USFM_TF_HAS_NBSP) dump_putc(&o, 'n');
                if (t.tok.level_flags & USFM_TF_HAS_LOOSE_WS) dump_putc(&o, 'w');
            }
            break;
        case USFM_TOK_MARKER:
            dump_puts(&o, "M:");
            dump_marker(&o, input, length, t.tok.span.offset, t.tok.marker, t.tok.level_flags, 1);
            break;
        case USFM_TOK_MARKER_END:
            dump_puts(&o, "E:");
            if (t.name_length == 0) dump_putc(&o, '*');
            else dump_marker(&o, input, length, t.tok.span.offset, t.tok.marker, t.tok.level_flags, 1);
            break;
        case USFM_TOK_ARGUMENT:
            dump_putc(&o, 'A');
            dump_quoted(&o, input, t.tok.span);
            break;
        case USFM_TOK_ATTRIBUTES:
            dump_putc(&o, '@');
            dump_quoted(&o, input, t.tok.span);
            break;
        case USFM_TOK_OPTBREAK:
            dump_puts(&o, "//");
            break;
        default:
            dump_puts(&o, "<?>");
            break;
        }
    }
}

static const char *const dump_kind_names[] = {
    "?", "BOOK", "HEADER", "CHAPTER", "VERSE", "PARA", "CHAR", "NOTE",
    "MILESTONE", "FIGURE", "TABLE", "ROW", "CELL", "SIDEBAR", "PERIPHERAL",
    "TEXT", "OPTBREAK", "ARGUMENT", "ATTRIBUTES"
};

static const char *const dump_diag_names[USFM_DIAGNOSTIC_CODE_COUNT] = {
    "NONE", "UNKNOWN_MARKER", "UNCLOSED_MARKER", "UNEXPECTED_CLOSE",
    "INVALID_NESTING", "INVALID_NUMBER", "DUPLICATE_NUMBER",
    "VERSE_BEFORE_CHAPTER", "INVALID_BOOK_CODE", "MISSING_CALLER",
    "UNTERMINATED_QUOTE", "UNKNOWN_ATTRIBUTE", "MISSING_ATTRIBUTE",
    "INVALID_ESCAPE", "MILESTONE_MISMATCH", "UNSUPPORTED_VERSION",
    "DUPLICATE_STYLE", "IMPLIED_PARAGRAPH", "TOO_DEEP"
};

static void dump_tree(const USFM_Ast *ast, char *out, size_t cap)
{
    DumpOut o;
    uint32_t count, input_length, depth = 0;
    uint32_t ends[USFM_STACK_DEPTH + 8];
    const USFM_Node *nodes = USFM_Ast_GetNodes(ast, &count);
    const char *input = USFM_Ast_GetInput(ast, &input_length);
    o.buf = out;
    o.cap = cap;
    o.len = 0;
    out[0] = '\0';
    for (uint32_t i = 0; i < count; i++) {
        const USFM_Node *n = &nodes[i];
        while (depth > 0 && i >= ends[depth - 1]) depth--;
        for (uint32_t d = 0; d < depth; d++) dump_puts(&o, "  ");
        dump_puts(&o, n->kind <= USFM_AST_ATTRIBUTES ? dump_kind_names[n->kind] : "?");
        if (n->kind == USFM_AST_TEXT || n->kind == USFM_AST_ARGUMENT ||
            n->kind == USFM_AST_ATTRIBUTES) {
            dump_putc(&o, ' ');
            dump_quoted(&o, input, n->span);
        } else if (n->marker != USFM_M_NONE) {
            dump_putc(&o, ' ');
            dump_marker(&o, input, input_length, n->span.offset, n->marker, n->level_flags,
                        n->kind == USFM_AST_CHAR || n->kind == USFM_AST_MILESTONE);
        }
        if (n->kind == USFM_AST_PARA && (n->level_flags & USFM_NF_IMPLIED)) dump_puts(&o, " implied");
        if (n->kind == USFM_AST_PARA && (n->level_flags & USFM_NF_HEADING)) dump_puts(&o, " heading");
        if (n->kind == USFM_AST_MILESTONE && (n->level_flags & USFM_TF_SELF_CLOSED)) dump_puts(&o, " closed");
        dump_putc(&o, '\n');
        if (depth < sizeof ends / sizeof ends[0]) ends[depth++] = n->end;
    }
}

static void dump_diags(const USFM_Ast *ast, char *out, size_t cap)
{
    DumpOut o;
    uint32_t count;
    const USFM_Diagnostic *diags = USFM_Ast_GetDiagnostics(ast, &count);
    o.buf = out;
    o.cap = cap;
    o.len = 0;
    out[0] = '\0';
    for (uint32_t i = 0; i < count; i++) {
        char number[16];
        dump_puts(&o, diags[i].code < USFM_DIAGNOSTIC_CODE_COUNT ? dump_diag_names[diags[i].code] : "?");
        snprintf(number, sizeof number, " %u\n", (unsigned)diags[i].span.offset);
        dump_puts(&o, number);
    }
}

#endif

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

#endif

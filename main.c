/* usfm: print, select and time USFM with libusfm.
 *
 *     usfm [options] [PATH...] [REF]
 *
 * Reads USFM from files, directories or stdin and prints it as colored
 * USFM (-u, the default), as the AST (-t) or as plain text (-x). REF
 * selects a chapter, a verse or a range ("1", "1:3", "1:3-5",
 * "1:3-2:4", "GEN 1:3"). -b N times parsing instead. Run `usfm -h`.
 *
 * Uses only the public usfm.h API. Portable C99 for POSIX and Windows;
 * all platform code is in the "Platform" section. Memory is never
 * freed except by the benchmark: the process exit releases it. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <fcntl.h>
#  include <io.h>
#else
#  include <dirent.h>
#  include <sys/stat.h>
#  include <time.h>
#  include <unistd.h>
#endif

#define USFM_IMPLEMENTATION
#include "usfm.h"

/* The tool never frees (see above), so leak reports in sanitizer builds
 * are noise. */
#if defined(__SANITIZE_ADDRESS__)
#  define USFM_CLI_ASAN 1
#elif defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    define USFM_CLI_ASAN 1
#  endif
#endif
#ifdef USFM_CLI_ASAN
const char *__asan_default_options(void);
const char *__asan_default_options(void) { return "detect_leaks=0"; }
#endif

#define EXIT_NO_MATCH 1
#define EXIT_USAGE    2

/* ===================================================================
 * Platform
 * =================================================================== */

static void *xmalloc(size_t size)
{
    void *p = malloc(size ? size : 1);
    if (p == NULL) {
        fprintf(stderr, "usfm: out of memory\n");
        exit(EXIT_USAGE);
    }
    return p;
}

static void *xrealloc(void *old, size_t size)
{
    void *p = realloc(old, size ? size : 1);
    if (p == NULL) {
        fprintf(stderr, "usfm: out of memory\n");
        exit(EXIT_USAGE);
    }
    return p;
}

static int plat_stdout_is_terminal(void)
{
#ifdef _WIN32
    return _isatty(_fileno(stdout));
#else
    return isatty(fileno(stdout));
#endif
}

/* Makes the console understand ANSI escapes. 0: it can't. */
static int plat_enable_ansi(void)
{
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (h == INVALID_HANDLE_VALUE || !GetConsoleMode(h, &mode)) return 0;
    return SetConsoleMode(h, mode | 0x0004 /* ENABLE_VIRTUAL_TERMINAL_PROCESSING */) != 0;
#else
    return 1;
#endif
}

static void plat_binary_stdin(void)
{
#ifdef _WIN32
    (void)_setmode(_fileno(stdin), _O_BINARY);
#endif
}

static int plat_is_directory(const char *path)
{
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

static uint64_t plat_now_ns(void)
{
#ifdef _WIN32
    static LARGE_INTEGER frequency;
    LARGE_INTEGER now;
    if (frequency.QuadPart == 0) QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&now);
    return (uint64_t)((double)now.QuadPart * 1e9 / (double)frequency.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
#endif
}

typedef struct Names {
    char  **items;
    size_t  count;
    size_t  capacity;
} Names;

static void names_push(Names *names, char *name)
{
    if (names->count == names->capacity) {
        names->capacity = names->capacity ? names->capacity * 2 : 16;
        names->items = (char **)xrealloc(names->items, names->capacity * sizeof(char *));
    }
    names->items[names->count++] = name;
}

/* Ends in .usfm or .sfm, in any case. */
static int has_usfm_extension(const char *name)
{
    static const char *const extensions[] = { ".usfm", ".sfm" };
    size_t length = strlen(name), e, i;
    for (e = 0; e < 2; e++) {
        size_t n = strlen(extensions[e]);
        if (length <= n) continue;
        for (i = 0; i < n; i++) {
            char c = name[length - n + i];
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            if (c != extensions[e][i]) break;
        }
        if (i == n) return 1;
    }
    return 0;
}

static char *join_path(const char *dir, const char *name)
{
    size_t a = strlen(dir), b = strlen(name);
    char *path = (char *)xmalloc(a + b + 2);
    memcpy(path, dir, a);
    path[a] = '/';
    memcpy(path + a + 1, name, b + 1);
    return path;
}

static int compare_names(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* Appends dir's .usfm/.sfm files (not subdirectories), sorted by name.
 * 0: the directory could not be read. */
static int plat_list_directory(const char *dir, Names *out)
{
    Names found = { NULL, 0, 0 };
    size_t i;
#ifdef _WIN32
    WIN32_FIND_DATAA data;
    char *pattern = join_path(dir, "*");
    HANDLE h = FindFirstFileA(pattern, &data);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (has_usfm_extension(data.cFileName)) names_push(&found, join_path(dir, data.cFileName));
    } while (FindNextFileA(h, &data));
    FindClose(h);
#else
    struct dirent *entry;
    DIR *d = opendir(dir);
    if (d == NULL) return 0;
    while ((entry = readdir(d)) != NULL) {
        char *path;
        struct stat st;
        if (!has_usfm_extension(entry->d_name)) continue;
        path = join_path(dir, entry->d_name);
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) names_push(&found, path);
    }
    closedir(d);
#endif
    if (found.count > 1) qsort(found.items, found.count, sizeof(char *), compare_names);
    for (i = 0; i < found.count; i++) names_push(out, found.items[i]);
    return 1;
}

/* ===================================================================
 * Input
 * =================================================================== */

typedef struct Input {
    const char *path;                   /* "-" for stdin */
    char       *data;
    uint32_t    length;
} Input;

/* Reads a whole stream. 0: read error or more than 4 GiB - 1 bytes. */
static int read_stream(FILE *f, Input *in)
{
    size_t length = 0, capacity = 1u << 16;
    char *data = (char *)xmalloc(capacity);
    for (;;) {
        size_t n;
        if (length == capacity) {
            if (capacity > (size_t)UINT32_MAX) return 0;
            capacity *= 2;
            data = (char *)xrealloc(data, capacity);
        }
        n = fread(data + length, 1, capacity - length, f);
        length += n;
        if (n == 0) break;
    }
    if (ferror(f) || length > (size_t)UINT32_MAX) return 0;
    in->data = data;
    in->length = (uint32_t)length;
    return 1;
}

static int read_input(Input *in)
{
    FILE *f;
    int ok;
    if (strcmp(in->path, "-") == 0) {
        plat_binary_stdin();
        return read_stream(stdin, in);
    }
    f = fopen(in->path, "rb");
    if (f == NULL) return 0;
    ok = read_stream(f, in);
    fclose(f);
    return ok;
}

/* ===================================================================
 * Output
 * =================================================================== */

typedef struct Out {
    int color;
    int at_line_start;                  /* nothing written on this line */
    int any;                            /* anything written at all */
    char last;                          /* last byte written */
} Out;

static Out out;

static void out_bytes(const char *s, size_t n)
{
    if (n == 0) return;
    fwrite(s, 1, n, stdout);
    out.at_line_start = s[n - 1] == '\n';
    out.last = s[n - 1];
    out.any = 1;
}

static void out_str(const char *s) { out_bytes(s, strlen(s)); }

static void out_char(char c) { out_bytes(&c, 1); }

static void out_newline_if_needed(void)
{
    if (out.any && !out.at_line_start) out_char('\n');
}

/* SGR colors. */
#define SGR_PARA   "1;34"                /* paragraph-level markers */
#define SGR_NUMBER "1;33"                /* \c, \v and their numbers */
#define SGR_CHAR   "34"                  /* character and milestone markers */
#define SGR_NOTE   "35"                  /* notes, marker and content */
#define SGR_ATTR   "32"                  /* attributes */
#define SGR_BOLD   "1"
#define SGR_STRING "32"
#define SGR_DIM    "2"

static void out_color(const char *sgr)
{
    if (!out.color || sgr == NULL) return;
    fputs("\x1b[", stdout);
    fputs(sgr, stdout);
    fputc('m', stdout);
}

static void out_reset(const char *sgr)
{
    if (out.color && sgr != NULL) fputs("\x1b[0m", stdout);
}

static int is_loose(char c) { return c == '\r' || c == '\n' || c == '\t'; }

/* Copies bytes, turning each run of CR, LF and TAB into one space. */
static void out_collapsed(const char *s, uint32_t n)
{
    uint32_t i = 0, start = 0;
    while (i < n) {
        if (!is_loose(s[i])) { i++; continue; }
        out_bytes(s + start, i - start);
        out_char(' ');
        while (i < n && is_loose(s[i])) i++;
        start = i;
    }
    out_bytes(s + start, n - start);
}

/* ===================================================================
 * Tree helpers
 * =================================================================== */

typedef struct Doc {
    const char      *path;
    const char      *input;
    uint32_t         length;
    const USFM_Node *nodes;
    uint32_t         count;
} Doc;

/* The marker as written in the source: "\p", "\+w", "\qt-s". Length 0
 * if the node has none (an implied paragraph, BOOK, TABLE, text). */
static uint32_t marker_length(const Doc *doc, const USFM_Node *n)
{
    uint32_t start = n->span.offset, end = n->span.offset + n->span.length, i;
    if (n->marker == USFM_M_NONE || n->span.length == 0 || doc->input[start] != '\\') return 0;
    if (n->kind == USFM_AST_PARA && (USFM_FLAGS(n->level_flags) & USFM_NF_IMPLIED)) return 0;
    i = start + 1;
    if (i < end && doc->input[i] == '+') i++;
    while (i < end) {
        char c = doc->input[i];
        if (c == ' ' || c == '*' || c == '\\' || c == '|' || is_loose(c)) break;
        i++;
    }
    return i - start;
}

/* The end marker the source wrote for this node ("\w*", "\+w*", "\*",
 * "\esbe"), or length 0 if it was closed implicitly. */
static uint32_t closer_offset(const Doc *doc, const USFM_Node *n, uint32_t *length)
{
    uint32_t start = n->span.offset, end = start + n->span.length, i;
    *length = 0;
    if (n->span.length < 2) return 0;
    if (n->kind == USFM_AST_SIDEBAR) {
        if (n->span.length >= 5 && memcmp(doc->input + end - 5, "\\esbe", 5) == 0) {
            *length = 5;
            return end - 5;
        }
        return 0;
    }
    if (doc->input[end - 1] != '*') return 0;
    for (i = end - 1; i > start; i--) {          /* bounded by the span */
        if (doc->input[i] == '\\') {
            *length = end - i;
            return i;
        }
    }
    return 0;
}

/* The ARGUMENT child of a node (always its first child), or NULL. */
static const USFM_Node *argument_of(const Doc *doc, uint32_t i)
{
    if (i + 1 < doc->nodes[i].end && doc->nodes[i + 1].kind == USFM_AST_ARGUMENT) {
        return &doc->nodes[i + 1];
    }
    return NULL;
}

static int is_line_kind(uint8_t kind)
{
    return kind == USFM_AST_HEADER || kind == USFM_AST_CHAPTER || kind == USFM_AST_VERSE ||
           kind == USFM_AST_PARA || kind == USFM_AST_ROW || kind == USFM_AST_SIDEBAR ||
           kind == USFM_AST_PERIPHERAL;
}

/* ===================================================================
 * Walking a node range
 *
 * A view gets open(i) for every node in [lo, hi) in preorder, and
 * close(i) after its descendants. Ancestors of lo are opened first, as
 * context. open returning 0 skips the node's descendants (close is
 * still called). The stack is bounded: the parser never nests deeper
 * than USFM_STACK_DEPTH, plus the root.
 * =================================================================== */

#define WALK_DEPTH (USFM_STACK_DEPTH + 8)

typedef struct View {
    int  (*open)(const Doc *doc, uint32_t i, uint32_t depth, int context);
    void (*close)(const Doc *doc, uint32_t i);
    void (*finish)(void);
} View;

static void walk(const Doc *doc, const View *view, uint32_t lo, uint32_t hi)
{
    uint32_t stack[WALK_DEPTH], context[WALK_DEPTH];
    uint32_t depth = 0, n_context = 0, i, p;
    if (lo >= hi) return;
    for (p = doc->nodes[lo].parent; p != USFM_NIL && n_context < WALK_DEPTH; p = doc->nodes[p].parent) {
        context[n_context++] = p;
    }
    while (n_context > 0) {                         /* outermost first */
        uint32_t c = context[--n_context];
        view->open(doc, c, depth, 1);
        stack[depth++] = c;
    }
    i = lo;
    while (i < hi) {                                /* each pass advances i */
        const USFM_Node *n = &doc->nodes[i];
        while (depth > 0 && doc->nodes[stack[depth - 1]].end <= i) view->close(doc, stack[--depth]);
        if (!view->open(doc, i, depth, 0)) {
            view->close(doc, i);
            i = n->end;
            continue;
        }
        if (n->end > i + 1 && depth < WALK_DEPTH) {
            stack[depth++] = i;
        } else {
            view->close(doc, i);
        }
        i++;
    }
    while (depth > 0) view->close(doc, stack[--depth]);
    view->finish();
}

/* ===================================================================
 * USFM view
 * =================================================================== */

static struct {
    int      need_space;                /* a marker or argument was just written */
    uint32_t note_depth;
} uv;

static const char *usfm_marker_color(const USFM_Node *n)
{
    if (uv.note_depth > 0 || n->kind == USFM_AST_NOTE) return SGR_NOTE;
    switch (n->kind) {
    case USFM_AST_CHAPTER: case USFM_AST_VERSE: return SGR_NUMBER;
    case USFM_AST_CHAR: case USFM_AST_MILESTONE: case USFM_AST_FIGURE: return SGR_CHAR;
    default: return SGR_PARA;
    }
}

static const char *usfm_text_color(void) { return uv.note_depth > 0 ? SGR_NOTE : NULL; }

static void usfm_space(void)
{
    if (uv.need_space) out_char(' ');
    uv.need_space = 0;
}

static int usfm_open(const Doc *doc, uint32_t i, uint32_t depth, int context)
{
    const USFM_Node *n = &doc->nodes[i];
    const char *src = doc->input + n->span.offset;
    const char *color;
    uint32_t mlen;
    (void)depth; (void)context;
    switch (n->kind) {
    case USFM_AST_TEXT:
        usfm_space();
        color = usfm_text_color();
        out_color(color);
        out_collapsed(src, n->span.length);
        out_reset(color);
        return 1;
    case USFM_AST_OPTBREAK:
        usfm_space();
        out_str("//");
        return 1;
    case USFM_AST_ARGUMENT:
        color = n->parent != USFM_NIL ? usfm_marker_color(&doc->nodes[n->parent]) : NULL;
        out_char(' ');
        out_color(color);
        out_bytes(src, n->span.length);
        out_reset(color);
        uv.need_space = 1;
        return 1;
    case USFM_AST_ATTRIBUTES:
        usfm_space();
        color = uv.note_depth > 0 ? SGR_NOTE : SGR_ATTR;
        out_color(color);
        out_bytes(src, n->span.length);    /* includes the leading | */
        out_reset(color);
        return 1;
    default:
        break;
    }
    if (is_line_kind(n->kind)) {
        out_newline_if_needed();
        uv.need_space = 0;
    }
    mlen = marker_length(doc, n);
    if (n->kind == USFM_AST_CELL && !out.at_line_start) uv.need_space = 1;
    if (mlen > 0) {
        usfm_space();
        color = usfm_marker_color(n);
        out_color(color);
        out_bytes(src, mlen);
        out_reset(color);
        uv.need_space = 1;
    }
    if (n->kind == USFM_AST_NOTE) uv.note_depth++;
    return 1;
}

static void usfm_close(const Doc *doc, uint32_t i)
{
    const USFM_Node *n = &doc->nodes[i];
    uint32_t length, offset;
    if (n->kind == USFM_AST_TEXT || n->kind == USFM_AST_OPTBREAK || n->kind == USFM_AST_ARGUMENT ||
        n->kind == USFM_AST_ATTRIBUTES) {
        return;
    }
    offset = closer_offset(doc, n, &length);
    if (length > 0) {
        const char *color = usfm_marker_color(n);
        if (n->kind == USFM_AST_SIDEBAR) out_newline_if_needed();
        out_color(color);
        out_bytes(doc->input + offset, length);
        out_reset(color);
        uv.need_space = 0;
    }
    if (n->kind == USFM_AST_NOTE && uv.note_depth > 0) uv.note_depth--;
}

static void usfm_finish(void)
{
    out_newline_if_needed();
    uv.need_space = 0;
    uv.note_depth = 0;
}

static const View usfm_view = { usfm_open, usfm_close, usfm_finish };

/* ===================================================================
 * Tree view
 * =================================================================== */

static const char *const kind_names[] = {
    "?", "BOOK", "HEADER", "CHAPTER", "VERSE", "PARA", "CHAR", "NOTE",
    "MILESTONE", "FIGURE", "TABLE", "ROW", "CELL", "SIDEBAR", "PERIPHERAL",
    "TEXT", "OPTBREAK", "ARGUMENT", "ATTRIBUTES"
};

static void tree_quoted(const char *s, uint32_t n)
{
    uint32_t i;
    out_color(SGR_STRING);
    out_char('"');
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\\') { out_char('\\'); out_char((char)c); }
        else if (c == '\n') out_str("\\n");
        else if (c == '\r') out_str("\\r");
        else if (c == '\t') out_str("\\t");
        else if (c < 0x20) {
            char hex[8];
            snprintf(hex, sizeof hex, "\\x%02X", c);
            out_str(hex);
        } else out_char((char)c);
    }
    out_char('"');
    out_reset(SGR_STRING);
}

static void tree_flag(const char *word)
{
    out_char(' ');
    out_str(word);
}

static int tree_open(const Doc *doc, uint32_t i, uint32_t depth, int context)
{
    const USFM_Node *n = &doc->nodes[i];
    uint8_t level = USFM_LEVEL(n->level_flags), flags = USFM_FLAGS(n->level_flags);
    uint32_t d, mlen = marker_length(doc, n);
    char number[32];
    for (d = 0; d < depth; d++) out_str("  ");
    out_color(SGR_BOLD);
    out_str(n->kind < sizeof kind_names / sizeof kind_names[0] ? kind_names[n->kind] : "?");
    out_reset(SGR_BOLD);
    if (mlen > 0) {
        out_char(' ');
        out_color(SGR_CHAR);
        out_bytes(doc->input + n->span.offset, mlen);
        out_reset(SGR_CHAR);
    }
    if (level != 0) {
        snprintf(number, sizeof number, " level %u", (unsigned)level);
        out_str(number);
    }
    if (n->kind == USFM_AST_PARA) {
        if (flags & USFM_NF_IMPLIED) tree_flag("implied");
        if (flags & USFM_NF_HEADING) tree_flag("heading");
    } else if (n->kind == USFM_AST_TEXT) {
        if (flags & USFM_TF_HAS_ESCAPE) tree_flag("escape");
        if (flags & USFM_TF_HAS_NBSP) tree_flag("nbsp");
        if (flags & USFM_TF_HAS_LOOSE_WS) tree_flag("loose-ws");
    } else if (n->kind != USFM_AST_ARGUMENT && n->kind != USFM_AST_ATTRIBUTES &&
               n->kind != USFM_AST_OPTBREAK) {
        if (flags & USFM_TF_NESTED) tree_flag("nested");
        if (flags & USFM_TF_MILESTONE_START) tree_flag("milestone-start");
        if (flags & USFM_TF_MILESTONE_END) tree_flag("milestone-end");
        if (flags & USFM_TF_SELF_CLOSED) tree_flag("self-closed");
    }
    if ((n->kind == USFM_AST_TEXT || n->kind == USFM_AST_ARGUMENT || n->kind == USFM_AST_ATTRIBUTES) &&
        (n->level_flags & USFM_TF_INVALID_UTF8)) {
        tree_flag("invalid-utf8");
    }
    if (n->kind == USFM_AST_TEXT || n->kind == USFM_AST_ARGUMENT || n->kind == USFM_AST_ATTRIBUTES) {
        out_char(' ');
        tree_quoted(doc->input + n->span.offset, n->span.length);
    }
    snprintf(number, sizeof number, " @%u+%u", (unsigned)n->span.offset, (unsigned)n->span.length);
    out_color(SGR_DIM);
    out_str(number);
    out_reset(SGR_DIM);
    if (context) out_str(" (context)");
    out_char('\n');
    return 1;
}

static void tree_close(const Doc *doc, uint32_t i) { (void)doc; (void)i; }

static void tree_finish(void) {}

static const View tree_view = { tree_open, tree_close, tree_finish };

/* ===================================================================
 * Text view
 * =================================================================== */

static struct {
    uint32_t heading_depth;             /* inside a heading paragraph */
    int      need_space;                /* a verse number was just written */
} tv;

/* Cooked text: escapes resolved, ~ as U+00A0, loose whitespace as one
 * space. */
static void text_cooked(const char *s, uint32_t n)
{
    uint32_t i = 0;
    while (i < n) {                                 /* each pass advances i */
        char c = s[i];
        if (c == '\\' && i + 1 < n) {
            out_char(s[i + 1]);
            i += 2;
        } else if (c == '~') {
            out_str("\xC2\xA0");
            i++;
        } else if (is_loose(c)) {
            out_char(' ');
            while (i < n && is_loose(s[i])) i++;
        } else {
            uint32_t start = i;
            while (i < n && s[i] != '\\' && s[i] != '~' && !is_loose(s[i])) i++;
            out_bytes(s + start, i - start);
        }
    }
}

static int text_open(const Doc *doc, uint32_t i, uint32_t depth, int context)
{
    const USFM_Node *n = &doc->nodes[i];
    const USFM_Node *arg;
    (void)depth; (void)context;
    switch (n->kind) {
    case USFM_AST_HEADER: case USFM_AST_NOTE: case USFM_AST_ATTRIBUTES:
    case USFM_AST_OPTBREAK: case USFM_AST_ARGUMENT:
        return 0;
    case USFM_AST_CHAPTER:
        arg = argument_of(doc, i);
        out_newline_if_needed();
        out_color(SGR_NUMBER);
        if (arg) out_bytes(doc->input + arg->span.offset, arg->span.length);
        out_reset(SGR_NUMBER);
        out_char('\n');
        return 0;
    case USFM_AST_VERSE:
        arg = argument_of(doc, i);
        if (!out.at_line_start && out.last != ' ' && out.last != '\t') out_char(' ');
        out_color(SGR_NUMBER);
        if (arg) out_bytes(doc->input + arg->span.offset, arg->span.length);
        out_reset(SGR_NUMBER);
        tv.need_space = 1;
        return 0;
    case USFM_AST_PARA: case USFM_AST_ROW: case USFM_AST_PERIPHERAL: case USFM_AST_SIDEBAR:
        out_newline_if_needed();
        tv.need_space = 0;
        if (n->kind == USFM_AST_PARA && (USFM_FLAGS(n->level_flags) & USFM_NF_HEADING)) {
            tv.heading_depth++;
        }
        return 1;
    case USFM_AST_CELL:
        if (!out.at_line_start) out_char('\t');
        tv.need_space = 0;
        return 1;
    case USFM_AST_TEXT:
        if (tv.need_space) out_char(' ');
        tv.need_space = 0;
        if (tv.heading_depth > 0) out_color(SGR_BOLD);
        text_cooked(doc->input + n->span.offset, n->span.length);
        if (tv.heading_depth > 0) out_reset(SGR_BOLD);
        return 1;
    default:
        return 1;
    }
}

static void text_close(const Doc *doc, uint32_t i)
{
    const USFM_Node *n = &doc->nodes[i];
    if (n->kind == USFM_AST_PARA && (USFM_FLAGS(n->level_flags) & USFM_NF_HEADING) &&
        tv.heading_depth > 0) {
        tv.heading_depth--;
    }
}

static void text_finish(void)
{
    out_newline_if_needed();
    tv.heading_depth = 0;
    tv.need_space = 0;
}

static const View text_view = { text_open, text_close, text_finish };

/* ===================================================================
 * References
 * =================================================================== */

typedef struct Ref {
    int      present;
    char     book[4];                   /* "" for any book */
    int      chapter_only;
    uint16_t chapter, verse;            /* first */
    uint16_t chapter_last, verse_last;  /* last */
} Ref;

static const char *parse_number(const char *s, uint16_t *value)
{
    uint32_t v = 0;
    if (*s < '0' || *s > '9') return NULL;
    while (*s >= '0' && *s <= '9') {                /* bounded by the string */
        v = v * 10u + (uint32_t)(*s - '0');
        if (v > 65535u) return NULL;
        s++;
    }
    if (v == 0) return NULL;
    *value = (uint16_t)v;
    return s;
}

static int is_alnum(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static char to_upper(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c; }

/* "C", "C:V", "C:V-V2", "C:V-C2:V2", each optionally after "BOOK ".
 * Returns 1 for a reference, 0 for anything else. *valid is 0 for a
 * reference that runs backwards. */
static int parse_ref(const char *s, Ref *ref, int *valid)
{
    Ref r;
    uint16_t a, b;
    memset(&r, 0, sizeof r);
    *valid = 1;
    if (is_alnum(s[0]) && is_alnum(s[1]) && is_alnum(s[2]) && s[3] == ' ') {
        r.book[0] = to_upper(s[0]);
        r.book[1] = to_upper(s[1]);
        r.book[2] = to_upper(s[2]);
        s += 4;
    }
    s = parse_number(s, &r.chapter);
    if (s == NULL) return 0;
    if (*s == '\0') {
        r.chapter_only = 1;
        r.chapter_last = r.chapter;
    } else {
        if (*s != ':') return 0;
        s = parse_number(s + 1, &r.verse);
        if (s == NULL) return 0;
        r.chapter_last = r.chapter;
        r.verse_last = r.verse;
        if (*s == '-') {
            s = parse_number(s + 1, &a);
            if (s == NULL) return 0;
            if (*s == ':') {
                s = parse_number(s + 1, &b);
                if (s == NULL) return 0;
                r.chapter_last = a;
                r.verse_last = b;
            } else {
                r.verse_last = a;
            }
        }
        if (*s != '\0') return 0;
        if (r.chapter_last < r.chapter ||
            (r.chapter_last == r.chapter && r.verse_last < r.verse)) {
            *valid = 0;
        }
    }
    r.present = 1;
    *ref = r;
    return 1;
}

static int book_matches(const Ref *ref, const USFM_Ast *ast)
{
    const char *code = USFM_Ast_GetBookCode(ast);
    int i;
    if (ref->book[0] == '\0') return 1;
    for (i = 0; i < 3; i++) {
        if (to_upper(code[i]) != ref->book[i]) return 0;
    }
    return code[3] == '\0';
}

/* The node range a reference selects. 0: nothing matched. */
static int resolve_ref(const USFM_Ast *ast, const Ref *ref, uint32_t *lo, uint32_t *hi)
{
    const USFM_VerseEntry *verses;
    uint32_t count, i, first = USFM_NIL, last = USFM_NIL;
    if (ref->chapter_only) {
        USFM_ChapterEntry chapter;
        if (USFM_Ast_FindChapter(ast, ref->chapter, &chapter) == USFM_NIL) return 0;
        *lo = chapter.node;
        *hi = chapter.end_node;
        return 1;
    }
    verses = USFM_Ast_GetVerses(ast, &count);
    for (i = 0; i < count; i++) {
        const USFM_VerseRef *v = &verses[i].ref;
        if (v->chapter == 0) continue;
        if (v->chapter < ref->chapter || (v->chapter == ref->chapter && v->verse_last < ref->verse)) continue;
        if (v->chapter > ref->chapter_last ||
            (v->chapter == ref->chapter_last && v->verse_first > ref->verse_last)) continue;
        if (first == USFM_NIL) first = i;
        last = i;
    }
    if (first == USFM_NIL) return 0;
    *lo = verses[first].node;
    *hi = verses[last].end_node;
    return 1;
}

/* Drops trailing nodes none of whose content is selected: the range
 * of verse 3 ends at the \v of verse 4, so it would otherwise end with
 * the opening of verse 4's paragraph. */
static uint32_t trim_range_end(const USFM_Node *nodes, uint32_t lo, uint32_t hi)
{
    while (hi > lo + 1 && nodes[hi - 1].end > hi) hi--;    /* bounded by hi - lo */
    return hi;
}

/* ===================================================================
 * Diagnostics and benchmark
 * =================================================================== */

static void print_diagnostics(const Doc *doc, const USFM_Ast *ast)
{
    static const char *const severities[] = { "note", "error", "warning", "info" };
    uint32_t count, i, dropped;
    const USFM_Diagnostic *d = USFM_Ast_GetDiagnostics(ast, &count);
    for (i = 0; i < count; i++) {
        uint32_t line = 1, column = 1, j, offset = d[i].span.offset;
        uint8_t severity = USFM_DiagnosticCode_GetSeverity(d[i].code);
        for (j = 0; j < offset && j < doc->length; j++) {   /* bounded by the input */
            if (doc->input[j] == '\n') { line++; column = 1; }
            else column++;
        }
        fprintf(stderr, "%s:%u:%u: %s: %s\n", doc->path, (unsigned)line, (unsigned)column,
                severity < 4 ? severities[severity] : "note",
                USFM_DiagnosticCode_GetMessage(d[i].code));
    }
    dropped = USFM_Ast_GetDroppedDiagnosticCount(ast);
    if (dropped > 0) fprintf(stderr, "%s: %u more diagnostics dropped\n", doc->path, (unsigned)dropped);
}

static double mib_per_s(uint64_t bytes, uint64_t ns)
{
    return ns ? ((double)bytes / (1024.0 * 1024.0)) / ((double)ns / 1e9) : 0.0;
}

/* ===================================================================
 * Main
 * =================================================================== */

static void usage(FILE *f)
{
    fputs("usage: usfm [options] [PATH...] [REF]\n"
          "\n"
          "Print USFM files, directories or stdin (no PATH, or -).\n"
          "\n"
          "REF   1  1:3  1:3-5  1:3-2:4  \"GEN 1:3\"\n"
          "\n"
          "  -u, --usfm          colored USFM (default)\n"
          "  -t, --tree          the AST as a tree\n"
          "  -x, --text          plain text\n"
          "  -C                  color on\n"
          "  -M                  color off (default when stdout is not a terminal\n"
          "                      or NO_COLOR is set)\n"
          "  -d, --diagnostics   print diagnostics to stderr\n"
          "  -b, --bench N       parse each input N times; print the fastest\n"
          "  -h, --help          this help\n", f);
}

static int fail(const char *message, const char *detail)
{
    fprintf(stderr, "usfm: %s%s%s\n", message, detail ? ": " : "", detail ? detail : "");
    return EXIT_USAGE;
}

int main(int argc, char **argv)
{
    const View *view = &usfm_view;
    Names paths = { NULL, 0, 0 };
    Ref ref;
    int color = -1, diagnostics = 0, matched = 0, refs = 0, i;
    unsigned long bench = 0;
    size_t p, shown = 0;
    uint64_t total_bytes = 0, total_ns = 0;
    const char *no_color = getenv("NO_COLOR");

    memset(&ref, 0, sizeof ref);
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        int valid;
        if (strcmp(a, "-u") == 0 || strcmp(a, "--usfm") == 0) view = &usfm_view;
        else if (strcmp(a, "-t") == 0 || strcmp(a, "--tree") == 0) view = &tree_view;
        else if (strcmp(a, "-x") == 0 || strcmp(a, "--text") == 0) view = &text_view;
        else if (strcmp(a, "-C") == 0) color = 1;
        else if (strcmp(a, "-M") == 0) color = 0;
        else if (strcmp(a, "-d") == 0 || strcmp(a, "--diagnostics") == 0) diagnostics = 1;
        else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) { usage(stdout); return 0; }
        else if (strcmp(a, "-b") == 0 || strcmp(a, "--bench") == 0) {
            char *end;
            if (i + 1 >= argc) return fail("missing count after", a);
            bench = strtoul(argv[++i], &end, 10);
            if (*end != '\0' || bench == 0) return fail("bad benchmark count", argv[i]);
        } else if (a[0] == '-' && a[1] != '\0') {
            usage(stderr);
            return fail("unknown option", a);
        } else if (parse_ref(a, &ref, &valid)) {
            if (!valid) return fail("reference runs backwards", a);
            if (refs++ > 0) return fail("more than one reference", a);
        } else if (plat_is_directory(a)) {
            if (!plat_list_directory(a, &paths)) return fail("cannot read directory", a);
        } else {
            names_push(&paths, (char *)a);
        }
    }
    if (paths.count == 0) names_push(&paths, (char *)"-");

    if (color < 0) color = plat_stdout_is_terminal() && !(no_color && no_color[0]);
    if (color && !bench && plat_stdout_is_terminal() && !plat_enable_ansi()) color = 0;
    out.color = color;
    out.at_line_start = 1;

    for (p = 0; p < paths.count; p++) {
        Input in;
        Doc doc;
        USFM_Ast *ast;
        USFM_Err err;
        uint32_t lo, hi;
        in.path = paths.items[p];
        if (!read_input(&in)) return fail("cannot read", in.path);

        if (bench) {
            uint64_t best = UINT64_MAX;
            unsigned long pass;
            for (pass = 0; pass < bench; pass++) {
                uint64_t start = plat_now_ns(), elapsed;
                ast = USFM_Ast_Parse(in.data, in.length, NULL, &err);
                elapsed = plat_now_ns() - start;
                if (ast == NULL) return fail("parse failed", err.msg);
                USFM_Ast_Free(ast);                 /* outside the timed region */
                if (elapsed < best) best = elapsed;
            }
            printf("%s  %u bytes  %llu ns  %.1f MiB/s\n", in.path, (unsigned)in.length,
                   (unsigned long long)best, mib_per_s(in.length, best));
            total_bytes += in.length;
            total_ns += best;
            continue;
        }

        ast = USFM_Ast_Parse(in.data, in.length, NULL, &err);
        if (ast == NULL) return fail("parse failed", err.msg);
        doc.path = in.path;
        doc.input = in.data;
        doc.length = in.length;
        doc.nodes = USFM_Ast_GetNodes(ast, &doc.count);
        if (diagnostics) print_diagnostics(&doc, ast);

        if (ref.present) {
            if (!book_matches(&ref, ast) || !resolve_ref(ast, &ref, &lo, &hi)) continue;
            hi = trim_range_end(doc.nodes, lo, hi);
            matched = 1;
        } else {
            lo = 0;
            hi = doc.count;
        }
        if (paths.count > 1) {
            fflush(stdout);
            fprintf(stderr, "%s==> %s <==\n", shown++ ? "\n" : "", in.path);
            fflush(stderr);
        }
        walk(&doc, view, lo, hi);
    }
    if (bench && paths.count > 1) {
        printf("total  %llu bytes  %llu ns  %.1f MiB/s\n", (unsigned long long)total_bytes,
               (unsigned long long)total_ns, mib_per_s(total_bytes, total_ns));
    }
    fflush(stdout);
    if (ref.present && !bench && !matched) return EXIT_NO_MATCH;
    return 0;
}

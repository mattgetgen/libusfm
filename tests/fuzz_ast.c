/* libFuzzer harness. Input layout: byte 0 picks the USFM version
 * (% 5), byte 1 picks flags: bit 0 = COPY_INPUT, bit 7 = fail the
 * allocator after (byte1 & 0x3F) calls. The rest is the USFM text.
 *
 * Every parse must either return NULL with USFM_ERR_NOMEM (and have
 * freed everything), or return a tree whose invariants hold. Any
 * crash, sanitizer report, leak or broken invariant is a finding.
 *
 * Built with -DUSFM_FUZZ_REPLAY, it gets a main() that runs each file
 * given on the command line, so seeds and crash files replay under gcc. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define USFM_TEST_SCAN_REFERENCE
#define USFM_IMPLEMENTATION
#include "../usfm.h"
#include "check.h"
#include "scan_compare.h"

typedef struct FuzzAlc {
    size_t   live_bytes;
    uint32_t calls_left;
} FuzzAlc;

/* Each block carries its requested size in an 8-byte header, which keeps
 * the returned pointer 8-byte aligned. Sized frees and resizes are
 * checked against it. */
#define FUZZ_ALC_HEADER 8u

static void *fuzz_alloc(void *ctx, size_t size)
{
    FuzzAlc *f = (FuzzAlc *)ctx;
    unsigned char *base;
    uint64_t stored = (uint64_t)size;
    if (size % 8 != 0) abort();
    if (f->calls_left == 0) return NULL;
    if (f->calls_left != UINT32_MAX) f->calls_left--;
    base = (unsigned char *)malloc(size + FUZZ_ALC_HEADER);
    if (!base) return NULL;
    memcpy(base, &stored, sizeof stored);
    f->live_bytes += size;
    return base + FUZZ_ALC_HEADER;
}

static void *fuzz_resize(void *ctx, void *ptr, size_t old_size, size_t new_size)
{
    FuzzAlc *f = (FuzzAlc *)ctx;
    unsigned char *base, *p;
    uint64_t stored;
    if (ptr == NULL) abort();
    base = (unsigned char *)ptr - FUZZ_ALC_HEADER;
    memcpy(&stored, base, sizeof stored);
    if (stored != (uint64_t)old_size) abort();
    if (old_size % 8 != 0 || new_size % 8 != 0) abort();
    if (f->calls_left == 0) return NULL;
    if (f->calls_left != UINT32_MAX) f->calls_left--;
    p = (unsigned char *)realloc(base, new_size + FUZZ_ALC_HEADER);
    if (!p) return NULL;
    stored = (uint64_t)new_size;
    memcpy(p, &stored, sizeof stored);
    f->live_bytes = f->live_bytes - old_size + new_size;
    return p + FUZZ_ALC_HEADER;
}

static void fuzz_dealloc(void *ctx, void *ptr, size_t size)
{
    FuzzAlc *f = (FuzzAlc *)ctx;
    unsigned char *base;
    uint64_t stored;
    if (ptr == NULL) abort();
    base = (unsigned char *)ptr - FUZZ_ALC_HEADER;
    memcpy(&stored, base, sizeof stored);
    if (stored != (uint64_t)size) abort();
    if (size % 8 != 0 || size > f->live_bytes) abort();
    f->live_bytes -= size;
    free(base);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    USFM_Config config;
    FuzzAlc state;
    USFM_Alc alc;
    USFM_Err err;
    USFM_Ast *ast;
    uint32_t flags;
    if (size < 2 || size - 2 > USFM_INPUT_MAX) return 0;
    {
        const char *text = (const char *)data + 2;
        uint32_t text_length = (uint32_t)(size - 2);
        if (scan_compare(text, text_length, 0u, 1) || scan_compare(text, text_length, 1u, 1) ||
            scan_compare(text, text_length, 2u + data[1], 1)) {
            abort();
        }
    }
    memset(&config, 0, sizeof config);
    config.version = (uint8_t)(data[0] % 5);
    flags = (data[1] & 0x01) ? USFM_PARSE_COPY_INPUT : USFM_PARSE_DEFAULT;
    state.live_bytes = 0;
    state.calls_left = (data[1] & 0x80) ? (uint32_t)(data[1] & 0x3F) : UINT32_MAX;
    alc.alloc = fuzz_alloc;
    alc.resize = fuzz_resize;
    alc.dealloc = fuzz_dealloc;
    alc.ctx = &state;

    ast = USFM_Ast_ParseOpts((const char *)data + 2, (uint32_t)(size - 2), &config, flags,
                             &alc, &err);
    if (ast == NULL) {
        if (state.live_bytes != 0) abort();
        if (err.code == USFM_ERR_UNSUPPORTED_ENCODING) {
            if (!usfm__wide_bom((const char *)data + 2, (uint32_t)(size - 2)) || err.pos != 0) abort();
            return 0;
        }
        if (err.code != USFM_ERR_NOMEM) abort();
        return 0;
    }
    if (err.code != USFM_OK && err.code != USFM_ERR_NOMEM) abort();
    if (check_ast(ast) != 0) abort();
    if (check_utf8_coverage(ast) != 0) abort();
    if (state.calls_left == UINT32_MAX && check_utf8_flags(ast) != 0) abort();
    if (state.calls_left == UINT32_MAX) {
        /* Validation adds diagnostics and the flag bit, nothing else. */
        USFM_Ast *plain = USFM_Ast_ParseOpts((const char *)data + 2, (uint32_t)(size - 2), &config,
                                             flags | USFM_PARSE_NO_UTF8_CHECK, NULL, &err);
        uint32_t na, nb, da, db, i, j;
        const USFM_Node *x, *y;
        const USFM_Diagnostic *dx, *dy;
        if (plain == NULL) abort();
        x = USFM_Ast_GetNodes(ast, &na);
        y = USFM_Ast_GetNodes(plain, &nb);
        if (na != nb) abort();
        for (i = 0; i < na; i++) {
            uint8_t k = x[i].kind;
            uint8_t ignore = (k == USFM_AST_TEXT || k == USFM_AST_ARGUMENT ||
                              k == USFM_AST_ATTRIBUTES) ? USFM_TF_INVALID_UTF8 : 0;
            if (x[i].kind != y[i].kind || x[i].marker != y[i].marker || x[i].parent != y[i].parent ||
                x[i].end != y[i].end || x[i].span.offset != y[i].span.offset ||
                x[i].span.length != y[i].span.length ||
                (uint8_t)(x[i].level_flags & ~ignore) != y[i].level_flags) abort();
        }
        dx = USFM_Ast_GetDiagnostics(ast, &da);
        dy = USFM_Ast_GetDiagnostics(plain, &db);
        for (i = 0, j = 0; i < da; i++) {
            if (dx[i].code == USFM_DIAGNOSTIC_INVALID_UTF8) continue;
            if (j >= db || dx[i].code != dy[j].code || dx[i].span.offset != dy[j].span.offset ||
                dx[i].span.length != dy[j].span.length || dx[i].marker != dy[j].marker) abort();
            j++;
        }
        if (j != db) abort();
        USFM_Ast_Free(plain);
    }
    {
        /* Exercise the iterators on every verse. */
        USFM_ChapterIter chapters;
        USFM_ChapterEntry chapter;
        uint32_t verse_count, i;
        const USFM_VerseEntry *verses = USFM_Ast_GetVerses(ast, &verse_count);
        for (i = 0; i < verse_count; i++) {
            USFM_VerseIter it;
            USFM_VerseItem item;
            USFM_VerseIter_Init(&it, ast, &verses[i],
                                USFM_VI_INCLUDE_NOTES | USFM_VI_REPORT_BREAKS |
                                USFM_VI_REPORT_NOTES | USFM_VI_INCLUDE_HEADINGS);
            while (USFM_VerseIter_Next(&it, &item)) {
                if (item.node >= ast->node_count) abort();
            }
            USFM_Ast_FindVerse(ast, verses[i].ref.chapter, verses[i].ref.verse_first, NULL);
        }
        USFM_ChapterIter_Init(&chapters, ast);
        while (USFM_ChapterIter_Next(&chapters, &chapter)) {
            /* The chapter's verses, in table order, then nothing more. */
            USFM_ChapterVerseIter chapter_verses;
            USFM_VerseEntry verse;
            uint32_t seen = 0;
            USFM_Ast_FindChapter(ast, chapter.number, NULL);
            USFM_ChapterVerseIter_Init(&chapter_verses, ast, &chapter);
            while (USFM_ChapterVerseIter_Next(&chapter_verses, &verse)) {
                if (seen >= chapter.verse_count ||
                    verse.node != verses[chapter.first_verse + seen].node ||
                    verse.ref.chapter != chapter.number) abort();
                seen++;
            }
            if (seen != chapter.verse_count) abort();
        }
    }
    USFM_Ast_Free(ast);
    if (state.live_bytes != 0) abort();
    return 0;
}

#ifdef USFM_FUZZ_REPLAY
int main(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb");
        uint8_t *data;
        long size;
        if (f == NULL) {
            fprintf(stderr, "cannot open %s\n", argv[i]);
            return 1;
        }
        fseek(f, 0, SEEK_END);
        size = ftell(f);
        fseek(f, 0, SEEK_SET);
        data = (uint8_t *)malloc(size > 0 ? (size_t)size : 1);
        if (data == NULL || (size > 0 && fread(data, 1, (size_t)size, f) != (size_t)size)) {
            fprintf(stderr, "cannot read %s\n", argv[i]);
            return 1;
        }
        fclose(f);
        LLVMFuzzerTestOneInput(data, (size_t)size);
        free(data);
    }
    printf("fuzz replay: %d inputs ok\n", argc - 1);
    return 0;
}
#endif

/* Usage: corpus_test DIR [EXPECTED_FILE]
 *
 * Parses every .usfm file in DIR, checks the AST invariants, compares the
 * block and byte-loop scanners, and prints an AST checksum. With
 * EXPECTED_FILE (tests/corpus/expected.txt), the totals must match the
 * line whose first field is DIR's basename:
 *
 *   NAME books chapters verses paragraphs chars notes milestones
 *
 * Also writes per-book counts to build/corpus/libusfm-NAME.csv; for
 * WEB-STRONGS that file can be diffed against
 * tests/corpus/paratextdata-web.csv. */
#define _DEFAULT_SOURCE 1
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define USFM_TEST_SCAN_REFERENCE
#define USFM_IMPLEMENTATION
#include "../usfm.h"
#include "check.h"
#include "scan_compare.h"

/* TEXT nodes whose flag disagrees with their text, per flag. */
typedef struct FlagMismatches {
    unsigned long long loose_ws, nbsp, escape;
} FlagMismatches;

static void count_flag_mismatches(const USFM_Ast *ast, FlagMismatches *m)
{
    uint32_t count, input_length, i;
    const USFM_Node *nodes = USFM_Ast_GetNodes(ast, &count);
    const char *input = USFM_Ast_GetInput(ast, &input_length);
    for (i = 0; i < count; i++) {
        uint8_t have, want;
        if (nodes[i].kind != USFM_AST_TEXT) continue;
        have = USFM_FLAGS(nodes[i].level_flags);
        want = check_text_flags(input, nodes[i].span);
        if ((have ^ want) & USFM_TF_HAS_LOOSE_WS) m->loose_ws++;
        if ((have ^ want) & USFM_TF_HAS_NBSP) m->nbsp++;
        if ((have ^ want) & USFM_TF_HAS_ESCAPE) m->escape++;
    }
}

typedef struct Counts {
    unsigned long long books, chapters, verses, paragraphs, chars, notes, milestones;
} Counts;

/* Counts what ParatextData's CountingSink counts (see the spec). */
static void count_ast(const USFM_Ast *ast, Counts *c)
{
    uint32_t count, i;
    const USFM_Node *nodes = USFM_Ast_GetNodes(ast, &count);
    c->books++;
    for (i = 0; i < count; i++) {
        const USFM_Node *n = &nodes[i];
        switch (n->kind) {
        case USFM_AST_CHAPTER: c->chapters++; break;
        case USFM_AST_VERSE: c->verses++; break;
        case USFM_AST_PARA:
            if (!(n->level_flags & USFM_NF_IMPLIED) && n->marker != USFM_M_CP) c->paragraphs++;
            break;
        case USFM_AST_HEADER:
            if (n->marker != USFM_M_ID) c->paragraphs++;
            break;
        case USFM_AST_CHAR: c->chars++; break;
        case USFM_AST_NOTE: c->notes++; break;
        case USFM_AST_MILESTONE: c->milestones++; break;
        default: break;
        }
    }
}

/* The last path component of dir, ignoring trailing slashes. */
static void corpus_name(const char *dir, char *out, size_t size)
{
    size_t end = strlen(dir), start;
    while (end > 1 && dir[end - 1] == '/') end--;
    start = end;
    while (start > 0 && dir[start - 1] != '/') start--;
    if (end - start >= size) end = start + size - 1;
    memcpy(out, dir + start, end - start);
    out[end - start] = '\0';
}

/* Finds name's line in the expected file. Returns 1 if found, 0 if not,
 * -1 if the file can't be read. */
static int read_expected(const char *path, const char *name, Counts *c)
{
    char line[512], first[256];
    int found = 0;
    FILE *f = fopen(path, "r");
    if (f == NULL) return -1;
    while (!found && fgets(line, sizeof line, f) != NULL) {
        if (line[0] == '#') continue;
        if (sscanf(line, "%255s %llu %llu %llu %llu %llu %llu %llu", first, &c->books,
                   &c->chapters, &c->verses, &c->paragraphs, &c->chars, &c->notes,
                   &c->milestones) == 8 &&
            strcmp(first, name) == 0) {
            found = 1;
        }
    }
    fclose(f);
    return found;
}

static char *read_file(const char *path, uint32_t *length)
{
    FILE *f = fopen(path, "rb");
    char *data;
    long size;
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = (char *)malloc(size > 0 ? (size_t)size : 1);
    if (data != NULL && size > 0 && fread(data, 1, (size_t)size, f) != (size_t)size) {
        free(data);
        data = NULL;
    }
    fclose(f);
    *length = (uint32_t)size;
    return data;
}

static int compare_names(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* FNV-1a over every field of the trees, tables and diagnostics (field
 * by field, so struct padding never enters the hash). */
static uint64_t fnv(uint64_t h, uint64_t v, uint32_t bytes)
{
    uint32_t k;
    for (k = 0; k < bytes; k++) {
        h ^= (v >> (8u * k)) & 0xFFu;
        h *= 0x100000001b3ull;
    }
    return h;
}

static uint64_t checksum_ast(uint64_t h, const USFM_Ast *ast)
{
    uint32_t count, i;
    const USFM_Node *nodes = USFM_Ast_GetNodes(ast, &count);
    const USFM_ChapterEntry *chapters;
    const USFM_VerseEntry *verses;
    const USFM_Diagnostic *diags;
    for (i = 0; i < count; i++) {
        h = fnv(h, nodes[i].parent, 4);
        h = fnv(h, nodes[i].end, 4);
        h = fnv(h, nodes[i].span.offset, 4);
        h = fnv(h, nodes[i].span.length, 4);
        h = fnv(h, nodes[i].marker, 2);
        h = fnv(h, nodes[i].kind, 1);
        h = fnv(h, nodes[i].level_flags, 1);
    }
    chapters = USFM_Ast_GetChapters(ast, &count);
    for (i = 0; i < count; i++) {
        h = fnv(h, chapters[i].number, 2);
        h = fnv(h, chapters[i].node, 4);
        h = fnv(h, chapters[i].end_node, 4);
        h = fnv(h, chapters[i].first_verse, 4);
        h = fnv(h, chapters[i].verse_count, 4);
    }
    verses = USFM_Ast_GetVerses(ast, &count);
    for (i = 0; i < count; i++) {
        h = fnv(h, verses[i].ref.chapter, 2);
        h = fnv(h, verses[i].ref.verse_first, 2);
        h = fnv(h, verses[i].ref.verse_last, 2);
        h = fnv(h, verses[i].ref.segment_first, 1);
        h = fnv(h, verses[i].ref.segment_last, 1);
        h = fnv(h, verses[i].node, 4);
        h = fnv(h, verses[i].end_node, 4);
    }
    diags = USFM_Ast_GetDiagnostics(ast, &count);
    for (i = 0; i < count; i++) {
        h = fnv(h, diags[i].span.offset, 4);
        h = fnv(h, diags[i].span.length, 4);
        h = fnv(h, diags[i].code, 2);
        h = fnv(h, diags[i].marker, 2);
    }
    return h;
}


int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/corpus/WEB-STRONGS";
    const char *expected_path = argc > 2 ? argv[2] : NULL;
    char name[256], csv_path[512];
    Counts expected;
    int have_expected = 0;
    Counts total;
    FlagMismatches mismatches;
    unsigned long long diag_totals[USFM_DIAGNOSTIC_CODE_COUNT];
    unsigned long long dropped_total = 0;
    char *names[256];
    size_t name_count = 0, i;
    int failures = 0;
    uint64_t checksum = 0xcbf29ce484222325ull;
    unsigned long long scan_diffs = 0;
    unsigned long long fast_diffs = 0;   /* fast path vs reference parser */
    unsigned long long block_diffs = 0;
    DIR *d = opendir(dir);
    FILE *csv;
    struct dirent *e;
    corpus_name(dir, name, sizeof name);
    snprintf(csv_path, sizeof csv_path, "build/corpus/libusfm-%s.csv", name);
    csv = fopen(csv_path, "w");
    memset(&expected, 0, sizeof expected);
    if (expected_path != NULL) {
        int r = read_expected(expected_path, name, &expected);
        if (r < 0) {
            fprintf(stderr, "cannot read %s\n", expected_path);
            failures++;
        } else if (r == 0) {
            fprintf(stderr, "%s: no line for %s\n", expected_path, name);
            failures++;
        } else {
            have_expected = 1;
        }
    }
    memset(&total, 0, sizeof total);
    memset(&mismatches, 0, sizeof mismatches);
    memset(diag_totals, 0, sizeof diag_totals);
    if (d == NULL) {
        fprintf(stderr, "cannot open %s\n", dir);
        if (csv) fclose(csv);
        return 1;
    }
    while ((e = readdir(d)) != NULL && name_count < 256) {
        size_t len = strlen(e->d_name);
        if ((len > 5 && strcmp(e->d_name + len - 5, ".usfm") == 0) ||
            (len > 4 && strcmp(e->d_name + len - 4, ".SFM") == 0)) {
            names[name_count++] = strdup(e->d_name);
        }
    }
    closedir(d);
    qsort(names, name_count, sizeof names[0], compare_names);
    for (i = 0; i < name_count; i++) {
        char path[1024];
        uint32_t length;
        char *data;
        USFM_Err err;
        USFM_Ast *ast;
        Counts book;
        snprintf(path, sizeof path, "%s/%s", dir, names[i]);
        data = read_file(path, &length);
        if (data == NULL) {
            fprintf(stderr, "cannot read %s\n", path);
            failures++;
            continue;
        }
#if USFM__USE_BLOCKS
        block_diffs += classifier_mismatches(data, length);
#endif
#if USFM__UTF8_SIMD
        if (usfm__utf8_valid_simd(data, length) != usfm__utf8_valid(data, length)) {
            fprintf(stderr, "%s: SIMD and scalar UTF-8 validators disagree\n", names[i]);
            failures++;
        }
#endif
        scan_diffs += (unsigned long long)(scan_compare(data, length, 0u, scan_diffs == 0) +
                                           scan_compare(data, length, 1u, scan_diffs == 0) +
                                           scan_compare(data, length, 7u + (uint32_t)i, scan_diffs == 0));
        fast_diffs += (unsigned long long)check_fast_path(data, length, NULL, 0, names[i]);
        ast = USFM_Ast_Parse(data, length, NULL, &err);
        if (ast == NULL || err.code != USFM_OK) {
            fprintf(stderr, "%s: parse failed: %s\n", names[i], err.msg);
            failures++;
        } else {
            checksum = checksum_ast(checksum, ast);
            count_flag_mismatches(ast, &mismatches);
            if (check_ast(ast) != 0) {
                fprintf(stderr, "%s: invariants failed\n", names[i]);
                failures++;
            }
            if (check_utf8_coverage(ast) != 0) {
                fprintf(stderr, "%s: bytes >= 0x80 outside every validated run\n", names[i]);
                failures++;
            }
            if (check_utf8_flags(ast) != 0) {
                fprintf(stderr, "%s: INVALID_UTF8 flags disagree with the reference validator\n",
                        names[i]);
                failures++;
            }
            {
                uint32_t diag_count, k;
                const USFM_Diagnostic *diags = USFM_Ast_GetDiagnostics(ast, &diag_count);
                for (k = 0; k < diag_count; k++) {
                    if (diags[k].code < USFM_DIAGNOSTIC_CODE_COUNT) diag_totals[diags[k].code]++;
                }
                dropped_total += USFM_Ast_GetDroppedDiagnosticCount(ast);
            }
            memset(&book, 0, sizeof book);
            count_ast(ast, &book);
            total.books += book.books;
            total.chapters += book.chapters;
            total.verses += book.verses;
            total.paragraphs += book.paragraphs;
            total.chars += book.chars;
            total.notes += book.notes;
            total.milestones += book.milestones;
            if (csv) {
                fprintf(csv, "%s,%llu,%llu,%llu\n", USFM_Ast_GetBookCode(ast),
                        book.chapters, book.verses, book.paragraphs);
            }
        }
        USFM_Ast_Free(ast);
        free(data);
        free(names[i]);
    }
    if (csv) fclose(csv);
    printf("Counted:    %llu books, %llu chapters, %llu verses, %llu paragraphs\n"
           "            %llu char styles, %llu notes, %llu milestones\n",
           total.books, total.chapters, total.verses, total.paragraphs,
           total.chars, total.notes, total.milestones);
    {
        /* The corpora are well-formed, so any diagnostic here deserves a look. */
        unsigned long long diag_sum = dropped_total;
        uint16_t k;
        for (k = 0; k < USFM_DIAGNOSTIC_CODE_COUNT; k++) diag_sum += diag_totals[k];
        printf("Diagnostics: %llu", diag_sum);
        if (dropped_total != 0) printf(" (%llu dropped)", dropped_total);
        printf("\n");
        for (k = 0; k < USFM_DIAGNOSTIC_CODE_COUNT; k++) {
            if (diag_totals[k] != 0) {
                printf("            %llu x %u %s\n", diag_totals[k], (unsigned)k,
                       USFM_DiagnosticCode_GetMessage(k));
            }
        }
    }
    printf("Text flag mismatches: %llu loose-ws, %llu nbsp, %llu escape\n",
           mismatches.loose_ws, mismatches.nbsp, mismatches.escape);
    if (mismatches.loose_ws + mismatches.nbsp + mismatches.escape != 0) failures++;
    printf("SIMD path: %s\n", USFM__SIMD_PATH);
    printf("UTF-8 validator: %s\n", USFM__UTF8_SIMD_PATH);
    printf("Scanner differences: %llu\n", scan_diffs);
    printf("Classifier differences: %llu\n", block_diffs);
    printf("Fast-path differences: %llu\n", fast_diffs);
    printf("AST checksum: %016llx\n", (unsigned long long)checksum);
    if (scan_diffs != 0 || block_diffs != 0 || fast_diffs != 0) failures++;
    if (expected_path == NULL) {
        printf("Expected:   (no expected file, counts not compared)\n");
    } else if (have_expected && memcmp(&total, &expected, sizeof total) != 0) {
        printf("Expected:   %llu books, %llu chapters, %llu verses, %llu paragraphs\n"
               "            %llu char styles, %llu notes, %llu milestones\n",
               expected.books, expected.chapters, expected.verses, expected.paragraphs,
               expected.chars, expected.notes, expected.milestones);
        failures++;
    }
    if (failures != 0) {
        printf("corpus: FAILED\n");
        return 1;
    }
    printf("corpus: ok\n");
    return 0;
}

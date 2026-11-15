/* Parses every .usfm file in a directory and checks the totals that
 * ParatextData, usfmtc and usfm3 agree on for the WEB corpus. Also
 * writes per-book counts to build/corpus/libusfm-web.csv for diffing
 * against tests/corpus/paratextdata-web.csv. */
#define _DEFAULT_SOURCE 1
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define USFM_IMPLEMENTATION
#include "../usfm.h"
#include "check.h"

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

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/corpus/WEB";
    const Counts expected = { 83, 1402, 38058, 34753, 685197, 2218, 0 };
    Counts total;
    FlagMismatches mismatches;
    unsigned long long diag_totals[USFM_DIAGNOSTIC_CODE_COUNT];
    unsigned long long dropped_total = 0;
    char *names[256];
    size_t name_count = 0, i;
    int failures = 0;
    DIR *d = opendir(dir);
    FILE *csv = fopen("build/corpus/libusfm-web.csv", "w");
    struct dirent *e;
    memset(&total, 0, sizeof total);
    memset(&mismatches, 0, sizeof mismatches);
    memset(diag_totals, 0, sizeof diag_totals);
    if (d == NULL) {
        fprintf(stderr, "cannot open %s\n", dir);
        return 1;
    }
    while ((e = readdir(d)) != NULL && name_count < 256) {
        size_t len = strlen(e->d_name);
        if (len > 5 && strcmp(e->d_name + len - 5, ".usfm") == 0) {
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
        ast = USFM_Ast_Parse(data, length, NULL, &err);
        if (ast == NULL || err.code != USFM_OK) {
            fprintf(stderr, "%s: parse failed: %s\n", names[i], err.msg);
            failures++;
        } else {
            count_flag_mismatches(ast, &mismatches);
            if (check_ast(ast) != 0) {
                fprintf(stderr, "%s: invariants failed\n", names[i]);
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
        /* WEB is well-formed, so any diagnostic here deserves a look. */
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
    if (memcmp(&total, &expected, sizeof total) != 0) {
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

#ifndef USFM_CHECK_H
#define USFM_CHECK_H
/* check_ast: verifies every structural invariant of a parsed AST.
 * Returns 0 if all hold; otherwise prints the first violation to stderr
 * and returns 1. Used by the unit, corpus and fuzz tests. */
#include <stdio.h>

#define CHECK_AST_FAIL(...) do { \
    fprintf(stderr, "invariant violated: "); \
    fprintf(stderr, __VA_ARGS__); \
    fputc('\n', stderr); \
    return 1; \
} while (0)

static int check_ast(const USFM_Ast *ast)
{
    uint32_t count, input_length, chapter_count, verse_count, diag_count, i;
    const USFM_Node *nodes = USFM_Ast_GetNodes(ast, &count);
    const USFM_ChapterEntry *chapters = USFM_Ast_GetChapters(ast, &chapter_count);
    const USFM_VerseEntry *verses = USFM_Ast_GetVerses(ast, &verse_count);
    const USFM_Diagnostic *diags = USFM_Ast_GetDiagnostics(ast, &diag_count);
    USFM_Ast_GetInput(ast, &input_length);

    if (count == 0) CHECK_AST_FAIL("no nodes");
    if (nodes[0].kind != USFM_AST_BOOK || nodes[0].parent != USFM_NIL || nodes[0].end != count)
        CHECK_AST_FAIL("node 0 is not a BOOK root covering every node");
    for (i = 0; i < count; i++) {
        const USFM_Node *n = &nodes[i];
        uint8_t k = n->kind;
        if ((uint64_t)n->span.offset + n->span.length > input_length)
            CHECK_AST_FAIL("node %u span %u+%u outside input of %u", i,
                           n->span.offset, n->span.length, input_length);
        if (n->end <= i || n->end > count) CHECK_AST_FAIL("node %u end %u", i, n->end);
        if (k < USFM_AST_BOOK || k > USFM_AST_ATTRIBUTES) CHECK_AST_FAIL("node %u kind %u", i, k);
        if ((k == USFM_AST_TEXT || k == USFM_AST_OPTBREAK || k == USFM_AST_ARGUMENT ||
             k == USFM_AST_ATTRIBUTES) && n->end != i + 1)
            CHECK_AST_FAIL("leaf node %u has children", i);
        if (i > 0) {
            uint32_t p = n->parent;
            if (p >= i) CHECK_AST_FAIL("node %u parent %u is not before it", i, p);
            if (i >= nodes[p].end || n->end > nodes[p].end)
                CHECK_AST_FAIL("node %u not inside parent %u", i, p);
        }
    }
    for (i = 0; i < chapter_count; i++) {
        const USFM_ChapterEntry *c = &chapters[i];
        if (c->node >= count || nodes[c->node].kind != USFM_AST_CHAPTER)
            CHECK_AST_FAIL("chapter %u node %u is not a CHAPTER", i, c->node);
        if (c->end_node <= c->node || c->end_node > count)
            CHECK_AST_FAIL("chapter %u end_node %u", i, c->end_node);
        if ((uint64_t)c->first_verse + c->verse_count > verse_count)
            CHECK_AST_FAIL("chapter %u verses out of range", i);
    }
    for (i = 0; i < verse_count; i++) {
        const USFM_VerseEntry *v = &verses[i];
        if (v->node >= count || nodes[v->node].kind != USFM_AST_VERSE)
            CHECK_AST_FAIL("verse %u node %u is not a VERSE", i, v->node);
        if (v->end_node <= v->node || v->end_node > count)
            CHECK_AST_FAIL("verse %u end_node %u", i, v->end_node);
        if (i > 0 && v->node <= verses[i - 1].node)
            CHECK_AST_FAIL("verse %u out of document order", i);
    }
    for (i = 0; i < diag_count; i++) {
        if ((uint64_t)diags[i].span.offset + diags[i].span.length > input_length)
            CHECK_AST_FAIL("diagnostic %u span outside input", i);
        if (diags[i].code == 0 || diags[i].code >= USFM_DIAGNOSTIC_CODE_COUNT)
            CHECK_AST_FAIL("diagnostic %u code %u", i, diags[i].code);
    }
    return 0;
}

#endif

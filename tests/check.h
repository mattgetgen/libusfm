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

/* The text flags a TEXT node with this span must carry, computed
 * independently of the scanner: HAS_ESCAPE for a backslash followed by
 * one of \\ / | ~ " (the pair is consumed, so an escaped ~ is not an
 * NBSP), HAS_NBSP for any other ~, HAS_LOOSE_WS for CR, LF or TAB. */
static uint8_t check_text_flags(const char *input, USFM_Span span)
{
    uint32_t i = span.offset, end = span.offset + span.length;
    uint8_t flags = 0;
    while (i < end) {
        char c = input[i];
        if (c == '\\' && i + 1 < end &&
            (input[i + 1] == '\\' || input[i + 1] == '/' || input[i + 1] == '|' ||
             input[i + 1] == '~' || input[i + 1] == '"')) {
            flags |= USFM_TF_HAS_ESCAPE;
            i += 2;
            continue;
        }
        if (c == '~') flags |= USFM_TF_HAS_NBSP;
        else if (c == '\r' || c == '\n' || c == '\t') flags |= USFM_TF_HAS_LOOSE_WS;
        i++;
    }
    return flags;
}

static int check_ast(const USFM_Ast *ast)
{
    uint32_t count, input_length, chapter_count, verse_count, diag_count, i;
    const USFM_Node *nodes = USFM_Ast_GetNodes(ast, &count);
    const char *input;
    const USFM_ChapterEntry *chapters = USFM_Ast_GetChapters(ast, &chapter_count);
    const USFM_VerseEntry *verses = USFM_Ast_GetVerses(ast, &verse_count);
    const USFM_Diagnostic *diags = USFM_Ast_GetDiagnostics(ast, &diag_count);
    input = USFM_Ast_GetInput(ast, &input_length);

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
        if (k == USFM_AST_TEXT && n->span.length == 0)
            CHECK_AST_FAIL("TEXT node %u has length 0", i);
        if (k == USFM_AST_TEXT && (uint64_t)n->span.offset + n->span.length <= input_length &&
            USFM_FLAGS(n->level_flags) != check_text_flags(input, n->span))
            CHECK_AST_FAIL("TEXT node %u flags 0x%02x, its text needs 0x%02x", i,
                           (unsigned)USFM_FLAGS(n->level_flags),
                           (unsigned)check_text_flags(input, n->span));
        if (n->end < count &&
            nodes[n->end].span.offset < (uint64_t)n->span.offset + n->span.length)
            CHECK_AST_FAIL("node %u (span %u+%u) overlaps next node %u at offset %u", i,
                           n->span.offset, n->span.length, n->end, nodes[n->end].span.offset);
        if (k < USFM_AST_BOOK || k > USFM_AST_ATTRIBUTES) CHECK_AST_FAIL("node %u kind %u", i, k);
        if ((k == USFM_AST_TEXT || k == USFM_AST_OPTBREAK || k == USFM_AST_ARGUMENT ||
             k == USFM_AST_ATTRIBUTES) && n->end != i + 1)
            CHECK_AST_FAIL("leaf node %u has children", i);
        if (i > 0) {
            uint32_t p = n->parent;
            if (p >= i) CHECK_AST_FAIL("node %u parent %u is not before it", i, p);
            if (i >= nodes[p].end || n->end > nodes[p].end)
                CHECK_AST_FAIL("node %u not inside parent %u", i, p);
            if (n->span.offset < nodes[p].span.offset ||
                (uint64_t)n->span.offset + n->span.length >
                    (uint64_t)nodes[p].span.offset + nodes[p].span.length)
                CHECK_AST_FAIL("node %u span %u+%u outside parent %u span %u+%u", i,
                               n->span.offset, n->span.length, p,
                               nodes[p].span.offset, nodes[p].span.length);
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
        {
            uint32_t v, last = c->first_verse + c->verse_count;
            for (v = c->first_verse; v < last; v++) {
                if (verses[v].node <= c->node || verses[v].node >= c->end_node)
                    CHECK_AST_FAIL("verse %u (node %u) outside chapter %u (nodes %u..%u)", v,
                                   verses[v].node, i, c->node, c->end_node);
            }
        }
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

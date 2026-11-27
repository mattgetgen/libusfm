#ifndef USFM_CHECK_H
#define USFM_CHECK_H
#include <stdlib.h>
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

/* Reference UTF-8 validator, written independently of usfm__utf8_check:
 * a prefix of a multi-byte sequence is acceptable if some completion of
 * it decodes to a scalar value of the right length (no overlong form,
 * no surrogate, at most U+10FFFF). Returns the offset of the first
 * invalid sequence in s[0, n), or n; *bad is the length of its longest
 * acceptable prefix, at least 1. */
static int check_utf8_prefix_ok(const unsigned char *s, uint32_t k, uint32_t len)
{
    static const uint32_t min_cp[5] = { 0, 0, 0x80, 0x800, 0x10000 };
    static const uint32_t max_cp[5] = { 0, 0, 0x7FF, 0xFFFF, 0x10FFFF };
    uint32_t lo, hi, j, a, b;
    lo = hi = (uint32_t)s[0] & (0x7Fu >> len);
    for (j = 1; j < len; j++) {
        uint32_t c_lo = j < k ? (uint32_t)(s[j] & 0x3F) : 0x00, c_hi = j < k ? c_lo : 0x3F;
        lo = (lo << 6) | c_lo;
        hi = (hi << 6) | c_hi;
    }
    a = lo > min_cp[len] ? lo : min_cp[len];
    b = hi < max_cp[len] ? hi : max_cp[len];
    if (a > b) return 0;
    if (a >= 0xD800 && b <= 0xDFFF) return 0;   /* only surrogates left */
    return 1;
}

static uint32_t check_utf8_first_bad(const char *str, uint32_t n, uint32_t *bad)
{
    const unsigned char *s = (const unsigned char *)str;
    uint32_t i = 0;
    *bad = 0;
    while (i < n) {
        uint32_t len, k;
        if (s[i] < 0x80) { i++; continue; }
        if ((s[i] & 0xE0) == 0xC0) len = 2;
        else if ((s[i] & 0xF0) == 0xE0) len = 3;
        else if ((s[i] & 0xF8) == 0xF0) len = 4;
        else { *bad = 1; return i; }
        if (!check_utf8_prefix_ok(s + i, 1, len)) { *bad = 1; return i; }
        for (k = 1; k < len; k++) {
            if (i + k >= n || (s[i + k] & 0xC0) != 0x80 || !check_utf8_prefix_ok(s + i, k + 1, len)) {
                *bad = k;
                return i;
            }
        }
        i += len;
    }
    return n;
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
            (USFM_FLAGS(n->level_flags) & USFM_TEXT_FLAGS_COOK) != check_text_flags(input, n->span))
            CHECK_AST_FAIL("TEXT node %u flags 0x%02x, its text needs 0x%02x", i,
                           (unsigned)USFM_FLAGS(n->level_flags),
                           (unsigned)check_text_flags(input, n->span));
        if ((k == USFM_AST_TEXT || k == USFM_AST_ARGUMENT || k == USFM_AST_ATTRIBUTES) &&
            (n->level_flags & USFM_TF_INVALID_UTF8) &&
            (uint64_t)n->span.offset + n->span.length <= input_length) {
            uint32_t bad;
            if (check_utf8_first_bad(input + n->span.offset, n->span.length, &bad) == n->span.length)
                CHECK_AST_FAIL("node %u is flagged invalid UTF-8 but its bytes are valid", i);
        }
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

/* With validation on (the default), the INVALID_UTF8 flag must be set on
 * exactly the TEXT, ARGUMENT and ATTRIBUTES nodes whose bytes are not
 * valid UTF-8. Returns the number of nodes that disagree. */
static uint32_t check_utf8_flags(const USFM_Ast *ast)
{
    uint32_t count, input_length, i, wrong = 0;
    const USFM_Node *nodes = USFM_Ast_GetNodes(ast, &count);
    const char *input = USFM_Ast_GetInput(ast, &input_length);
    for (i = 0; i < count; i++) {
        const USFM_Node *n = &nodes[i];
        uint32_t bad;
        int invalid;
        if (n->kind != USFM_AST_TEXT && n->kind != USFM_AST_ARGUMENT &&
            n->kind != USFM_AST_ATTRIBUTES) continue;
        invalid = check_utf8_first_bad(input + n->span.offset, n->span.length, &bad) !=
                  n->span.length;
        if (invalid != ((n->level_flags & USFM_TF_INVALID_UTF8) != 0)) wrong++;
    }
    return wrong;
}

/* Every byte >= 0x80 of the input must lie inside a TEXT, ARGUMENT or
 * ATTRIBUTES node: those are the only runs that are validated. Returns
 * the number of such bytes outside every one of them. */
static uint32_t check_utf8_coverage(const USFM_Ast *ast)
{
    uint32_t count, input_length, i, missing = 0;
    const USFM_Node *nodes = USFM_Ast_GetNodes(ast, &count);
    const unsigned char *input = (const unsigned char *)USFM_Ast_GetInput(ast, &input_length);
    unsigned char *covered = (unsigned char *)calloc(input_length + 1u, 1);
    if (covered == NULL) return 1;
    for (i = 0; i < count; i++) {
        const USFM_Node *n = &nodes[i];
        if (n->kind == USFM_AST_TEXT || n->kind == USFM_AST_ARGUMENT ||
            n->kind == USFM_AST_ATTRIBUTES) {
            memset(covered + n->span.offset, 1, n->span.length);
        }
    }
    /* The scanner skips a leading UTF-8 byte-order mark: no node covers it. */
    i = (input_length >= 3 && input[0] == 0xEF && input[1] == 0xBB && input[2] == 0xBF) ? 3u : 0u;
    for (; i < input_length; i++) {
        if (input[i] >= 0x80 && !covered[i]) missing++;
    }
    free(covered);
    return missing;
}

/* Compares two parses of the same input field by field: `a` under test,
 * `b` the reference (e.g. the fast path off, or per-run UTF-8 checks).
 * Returns 0 if identical; otherwise prints the first difference and
 * returns 1. */
static int check_same_parse(const USFM_Ast *a, const USFM_Err *ea, const USFM_Ast *b,
                            const USFM_Err *eb, const char *label)
{
    uint32_t na, nb, da, db, ca, cb, va, vb, i;
    const USFM_Node *xa, *xb;
    const USFM_Diagnostic *ga, *gb;
    const USFM_ChapterEntry *ha, *hb;
    const USFM_VerseEntry *wa, *wb;
    if ((a == NULL) != (b == NULL) || ea->code != eb->code) {
        fprintf(stderr, "%s: result %s/%u, reference %s/%u\n", label,
                a ? "tree" : "NULL", (unsigned)ea->code, b ? "tree" : "NULL", (unsigned)eb->code);
        return 1;
    }
    if (a == NULL) return 0;
    /* With a tree, err.pos is the first dropped offset (when NOMEM). */
    if (ea->pos != eb->pos) {
        fprintf(stderr, "%s: error position %u, reference %u\n", label,
                (unsigned)ea->pos, (unsigned)eb->pos);
        return 1;
    }
    xa = USFM_Ast_GetNodes(a, &na);
    xb = USFM_Ast_GetNodes(b, &nb);
    for (i = 0; i < na && i < nb; i++) {
        if (memcmp(&xa[i], &xb[i], sizeof xa[i]) != 0) {
            fprintf(stderr, "%s: node %u differs: kind %u/%u marker %u/%u span %u+%u/%u+%u"
                    " parent %u/%u end %u/%u flags 0x%02x/0x%02x\n", label, i,
                    xa[i].kind, xb[i].kind, xa[i].marker, xb[i].marker, xa[i].span.offset,
                    xa[i].span.length, xb[i].span.offset, xb[i].span.length, xa[i].parent,
                    xb[i].parent, xa[i].end, xb[i].end, xa[i].level_flags, xb[i].level_flags);
            return 1;
        }
    }
    if (na != nb) {
        fprintf(stderr, "%s: %u nodes, reference %u\n", label, na, nb);
        return 1;
    }
    ga = USFM_Ast_GetDiagnostics(a, &da);
    gb = USFM_Ast_GetDiagnostics(b, &db);
    for (i = 0; i < da && i < db; i++) {
        if (memcmp(&ga[i], &gb[i], sizeof ga[i]) != 0) {
            fprintf(stderr, "%s: diagnostic %u differs: code %u/%u at %u/%u\n", label, i,
                    ga[i].code, gb[i].code, ga[i].span.offset, gb[i].span.offset);
            return 1;
        }
    }
    if (da != db || USFM_Ast_GetDroppedDiagnosticCount(a) != USFM_Ast_GetDroppedDiagnosticCount(b)) {
        fprintf(stderr, "%s: %u diagnostics (%u dropped), reference %u (%u dropped)\n",
                label, da, USFM_Ast_GetDroppedDiagnosticCount(a), db,
                USFM_Ast_GetDroppedDiagnosticCount(b));
        return 1;
    }
    ha = USFM_Ast_GetChapters(a, &ca);
    hb = USFM_Ast_GetChapters(b, &cb);
    wa = USFM_Ast_GetVerses(a, &va);
    wb = USFM_Ast_GetVerses(b, &vb);
    if (ca != cb || va != vb || (ca && memcmp(ha, hb, ca * sizeof *ha) != 0) ||
        (va && memcmp(wa, wb, va * sizeof *wa) != 0) ||
        USFM_Ast_GetVersion(a) != USFM_Ast_GetVersion(b) ||
        strcmp(USFM_Ast_GetBookCode(a), USFM_Ast_GetBookCode(b)) != 0) {
        fprintf(stderr, "%s: chapter/verse tables, version or book code differ\n", label);
        return 1;
    }
    return 0;
}

/* Parses `input` with the fast path on and off (default allocator) and
 * compares the results. Returns 0 if identical. */
static int check_fast_path(const char *input, uint32_t length, const USFM_Config *config,
                           uint32_t flags, const char *label)
{
    USFM_Err ea, eb;
    USFM__ParseHooks generic;
    USFM_Ast *a, *b;
    int diff;
    char fast_label[300];
    memset(&generic, 0, sizeof generic);
    a = usfm__parse_opts_impl(input, length, config, flags, NULL, &ea, NULL);
    b = usfm__parse_opts_impl(input, length, config, flags, NULL, &eb, &generic);
    snprintf(fast_label, sizeof fast_label, "%s (fast path)", label);
    diff = check_same_parse(a, &ea, b, &eb, fast_label);
    USFM_Ast_Free(a);
    USFM_Ast_Free(b);
    return diff;
}

/* UTF-8 validation must not change anything: parses `input` normally
 * and with the reference validation (every run checked on its own), and
 * compares the two. Returns 0 if identical, 1 otherwise. */
static int check_utf8_reference(const char *input, uint32_t length, const USFM_Config *config,
                                uint32_t flags, const char *label)
{
    USFM_Err er, en;
    USFM__ParseHooks hooks;
    USFM_Ast *ref, *ast;
    int diff;
    char ref_label[300];
    memset(&hooks, 0, sizeof hooks);
    hooks.fast = 1;
    hooks.utf8_reference = 1;
    ref = usfm__parse_opts_impl(input, length, config, flags, NULL, &er, &hooks);
    ast = usfm__parse_opts_impl(input, length, config, flags, NULL, &en, NULL);
    snprintf(ref_label, sizeof ref_label, "%s (UTF-8 reference)", label);
    diff = check_same_parse(ast, &en, ref, &er, ref_label);
    USFM_Ast_Free(ast);
    USFM_Ast_Free(ref);
    return diff;
}

#endif

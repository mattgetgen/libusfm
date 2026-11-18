/* Struct sizes are part of the ABI; the header also asserts them at
 * compile time. */
static void test_struct_sizes(void)
{
    CHECK_EQ(sizeof(USFM_Span), 8);
    CHECK_EQ(sizeof(USFM_Token), 12);
    CHECK_EQ(sizeof(USFM_Node), 20);
    CHECK_EQ(sizeof(USFM_Diagnostic), 12);
    CHECK_EQ(sizeof(USFM_ChapterEntry), 20);
    CHECK_EQ(sizeof(USFM_VerseEntry), 16);
}

static void test_marker_hash_uses_length(void)
{
    /* "pp" and "p" share their first byte; the length must separate them. */
    CHECK(usfm__marker_hash("p", 1) != usfm__marker_hash("pp", 2));
    CHECK(usfm__marker_hash("add", 3) == usfm__marker_hash("add", 3));
}

static void test_node_level_and_flags(void)
{
    USFM_Node n;
    memset(&n, 0, sizeof n);
    n.level_flags = 0x93;
    CHECK_EQ(USFM_Node_GetLevel(&n), 3);
    CHECK_EQ(USFM_Node_GetFlags(&n), 0x90);
    CHECK_EQ(USFM_Node_HasFlags(&n, USFM_TF_NESTED), 1);
    CHECK_EQ(USFM_Node_HasFlags(&n, USFM_TF_NESTED | USFM_TF_SELF_CLOSED), 1);
    CHECK_EQ(USFM_Node_HasFlags(&n, USFM_TF_NESTED | USFM_TF_MILESTONE_START), 0);
    CHECK_EQ(USFM_Node_HasFlags(&n, 0), 1);
    /* The level bits are not flags. */
    CHECK_EQ(USFM_Node_HasFlags(&n, 0x01), 0);
}

/* The same accessors on parsed nodes. */
static void test_node_level_and_flags_parsed(void)
{
    USFM_Config config;
    USFM_Err err;
    const char *usfm = "\\s1 H\n\\q2 a \\+nd b\\+nd* \\qt-s\\*";
    USFM_Ast *ast;
    uint32_t count, i, seen = 0;
    const USFM_Node *nodes;
    memset(&config, 0, sizeof config);
    ast = USFM_Ast_Parse(usfm, (uint32_t)strlen(usfm), &config, &err);
    CHECK(ast != NULL);
    if (ast == NULL) return;
    nodes = USFM_Ast_GetNodes(ast, &count);
    for (i = 0; i < count; i++) {
        const USFM_Node *n = &nodes[i];
        if (n->kind == USFM_AST_PARA && n->marker == USFM_M_S) {
            seen++;
            CHECK_EQ(USFM_Node_GetLevel(n), 1);
            CHECK_EQ(USFM_Node_GetFlags(n), USFM_NF_HEADING);
            CHECK_EQ(USFM_Node_HasFlags(n, USFM_NF_HEADING), 1);
            CHECK_EQ(USFM_Node_HasFlags(n, USFM_NF_HEADING | USFM_NF_IMPLIED), 0);
        } else if (n->kind == USFM_AST_PARA && n->marker == USFM_M_Q) {
            seen++;
            CHECK_EQ(USFM_Node_GetLevel(n), 2);
            CHECK_EQ(USFM_Node_GetFlags(n), 0);
        } else if (n->kind == USFM_AST_CHAR && n->marker == USFM_M_ND) {
            seen++;
            CHECK_EQ(USFM_Node_GetLevel(n), 0);
            CHECK_EQ(USFM_Node_HasFlags(n, USFM_TF_NESTED), 1);
        } else if (n->kind == USFM_AST_MILESTONE) {
            seen++;
            CHECK_EQ(USFM_Node_HasFlags(n, USFM_TF_MILESTONE_START | USFM_TF_SELF_CLOSED), 1);
            CHECK_EQ(USFM_Node_HasFlags(n, USFM_TF_MILESTONE_END), 0);
        }
    }
    CHECK_EQ(seen, 4);
    USFM_Ast_Free(ast);
}

static void run_basics_tests(void)
{
    RUN(test_struct_sizes);
    RUN(test_marker_hash_uses_length);
    RUN(test_node_level_and_flags);
    RUN(test_node_level_and_flags_parsed);
}

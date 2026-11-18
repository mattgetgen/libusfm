static const char *alloc_doc =
    "\\id GEN Test\n\\c 1\n\\s1 Head\n\\p \\v 1 a \\w b|strong=\"H1\"\\w* \\f + \\ft n\\f*\n"
    "\\q1 \\v 2 c \\nd d\\nd*\n\\c 2\n\\p \\v 1 e \\xyz f\n";

static void check_alc_clean(const TestAlc *state)
{
    CHECK_EQ(state->bad_sizes, 0);
    CHECK_EQ(state->size_mismatches, 0);
    CHECK_EQ(state->null_frees, 0);
}

static void test_parse_with_custom_allocator(void)
{
    TestAlc state;
    USFM_Alc alc = test_alc(&state, UINT32_MAX);
    USFM_Err err;
    USFM_Ast *ast = USFM_Ast_ParseOpts(alloc_doc, (uint32_t)strlen(alloc_doc), NULL,
                                       USFM_PARSE_DEFAULT, &alc, &err);
    CHECK(ast != NULL);
    CHECK_EQ(err.code, USFM_OK);
    CHECK(state.live_bytes > 0);
    USFM_Ast_Free(ast);
    CHECK_EQ(state.live_bytes, 0);
    check_alc_clean(&state);
}

static void test_parse_fails_cleanly_at_every_allocation(void)
{
    /* Fail after N successful allocator calls, for every N until a
     * parse succeeds. Each failure must free everything. */
    int succeeded = 0;
    for (uint32_t n = 0; n < 64 && !succeeded; n++) {
        TestAlc state;
        USFM_Alc alc = test_alc(&state, n);
        USFM_Err err;
        USFM_Ast *ast = USFM_Ast_ParseOpts(alloc_doc, (uint32_t)strlen(alloc_doc), NULL,
                                           USFM_PARSE_COPY_INPUT, &alc, &err);
        if (ast == NULL) {
            CHECK_EQ(err.code, USFM_ERR_NOMEM);
            CHECK_EQ(state.live_bytes, 0);
        } else {
            succeeded = 1;
            CHECK_EQ(check_ast(ast), 0);
            USFM_Ast_Free(ast);
            CHECK_EQ(state.live_bytes, 0);
        }
        check_alc_clean(&state);
    }
    CHECK(succeeded);
}

static void test_dropped_diagnostics_keep_the_tree(void)
{
    /* Calls: 1 = the handle, 2 = the node array, 3 = the diagnostic
     * array. Failing the third drops the diagnostic but keeps the tree. */
    TestAlc state;
    USFM_Alc alc = test_alc(&state, 2);
    USFM_Err err;
    USFM_Ast *ast = USFM_Ast_ParseOpts("\\p a\\nd* b", 10, NULL, USFM_PARSE_DEFAULT, &alc, &err);
    CHECK(ast != NULL);
    if (ast == NULL) return;
    CHECK_EQ(err.code, USFM_ERR_NOMEM);
    CHECK_EQ(err.pos, 4);
    CHECK_EQ(USFM_Ast_GetDroppedDiagnosticCount(ast), 1);
    CHECK_EQ(check_ast(ast), 0);
    USFM_Ast_Free(ast);
    CHECK_EQ(state.live_bytes, 0);
    check_alc_clean(&state);
}

static void test_copy_input_is_independent(void)
{
    char buffer[] = "\\p hello";
    USFM_Err err;
    USFM_Ast *ast = USFM_Ast_ParseOpts(buffer, (uint32_t)strlen(buffer), NULL,
                                       USFM_PARSE_COPY_INPUT, NULL, &err);
    CHECK(ast != NULL);
    if (ast == NULL) return;
    memset(buffer, 'X', strlen(buffer));
    dump_tree(ast, helper_buf, sizeof helper_buf);
    CHECK_STR(helper_buf, "BOOK\n  PARA p\n    TEXT \"hello\"\n");
    USFM_Ast_Free(ast);
}

/* An allocator that refuses any single request above 24 MiB. */
static void *capped_alloc(void *ctx, size_t size)
{
    return size > ((size_t)24 << 20) ? NULL : test_alloc(ctx, size);
}

static void *capped_resize(void *ctx, void *ptr, size_t old_size, size_t new_size)
{
    return new_size > ((size_t)24 << 20) ? NULL : test_resize(ctx, ptr, old_size, new_size);
}

/* The initial node array is only an estimate from the input length; it
 * is capped (at 1 << 20 nodes), so a large input whose tree is small
 * does not fail just because of the estimate. */
static void test_initial_node_estimate_is_capped(void)
{
    uint32_t length = 9u << 20;
    char *input = (char *)malloc(length);
    TestAlc state;
    USFM_Alc alc = test_alc(&state, UINT32_MAX);
    USFM_Err err;
    USFM_Ast *ast;
    CHECK(input != NULL);
    if (input == NULL) return;
    memset(input, 'a', length);
    memcpy(input, "\\p ", 3);
    alc.alloc = capped_alloc;
    alc.resize = capped_resize;
    ast = USFM_Ast_ParseOpts(input, length, NULL, USFM_PARSE_DEFAULT, &alc, &err);
    CHECK(ast != NULL);
    CHECK_EQ(err.code, USFM_OK);
    if (ast != NULL) {
        uint32_t count;
        USFM_Ast_GetNodes(ast, &count);
        CHECK_EQ(count, 3);         /* BOOK, PARA, TEXT */
        USFM_Ast_Free(ast);
    }
    CHECK_EQ(state.live_bytes, 0);
    check_alc_clean(&state);
    free(input);
}

static void run_parse_alloc_tests(void)
{
    RUN(test_parse_with_custom_allocator);
    RUN(test_parse_fails_cleanly_at_every_allocation);
    RUN(test_dropped_diagnostics_keep_the_tree);
    RUN(test_copy_input_is_independent);
    RUN(test_initial_node_estimate_is_capped);
}

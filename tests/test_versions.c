static void test_usfm_line_selects_version(void)
{
    const char *v24 = "\\id GEN\n\\usfm 2.4\n\\p \\add a \\nd b";
    const char *v31 = "\\id GEN\n\\usfm 3.1\n\\p \\add a \\nd b";
    USFM_Ast *ast = parse_with_version(v24, USFM_VERSION_AUTO);
    CHECK(ast != NULL);
    if (ast != NULL) {
        CHECK_EQ(USFM_Ast_GetVersion(ast), USFM_VERSION_2);
        USFM_Ast_Free(ast);
    }
    CHECK_DIAGS(v24, USFM_VERSION_AUTO, "");
    CHECK_DIAGS(v31, USFM_VERSION_AUTO, "UNCLOSED_MARKER 21\nUNCLOSED_MARKER 28\n");
    /* An explicit config version wins over \usfm. */
    CHECK_DIAGS(v24, USFM_VERSION_3_1, "UNCLOSED_MARKER 21\nUNCLOSED_MARKER 28\n");
}

static void test_unsupported_version(void)
{
    USFM_Ast *ast = parse_with_version("\\usfm 4.0\n", USFM_VERSION_AUTO);
    CHECK(ast != NULL);
    if (ast != NULL) {
        CHECK_EQ(USFM_Ast_GetVersion(ast), USFM_VERSION_3_1);
        USFM_Ast_Free(ast);
    }
    CHECK_DIAGS("\\usfm 4.0\n", USFM_VERSION_AUTO, "UNSUPPORTED_VERSION 6\n");
}

static void test_depth_limit(void)
{
    /* BOOK + PARA + 62 nested spans fill the 64-deep stack; the
     * remaining 8 markers become text with a TOO_DEEP diagnostic. */
    char input[1024];
    size_t used = 0;
    uint32_t count, too_deep = 0;
    const USFM_Diagnostic *diags;
    USFM_Ast *ast;
    used += (size_t)snprintf(input + used, sizeof input - used, "\\p ");
    for (int i = 0; i < 70; i++) {
        used += (size_t)snprintf(input + used, sizeof input - used, "\\+nd x");
    }
    ast = parse_with_version(input, USFM_VERSION_AUTO);
    CHECK(ast != NULL);
    if (ast == NULL) return;
    CHECK_EQ(check_ast(ast), 0);
    diags = USFM_Ast_GetDiagnostics(ast, &count);
    for (uint32_t i = 0; i < count; i++) {
        if (diags[i].code == USFM_DIAGNOSTIC_TOO_DEEP) too_deep++;
    }
    CHECK_EQ(too_deep, 8);
    USFM_Ast_Free(ast);
}

static void test_verse_closes_heading(void)
{
    CHECK_TREE("\\c 1\n\\s1 Head \\v 1 x",
        "BOOK\n"
        "  CHAPTER c\n"
        "    ARGUMENT \"1\"\n"
        "  PARA s1 heading\n"
        "    TEXT \"Head\"\n"
        "  PARA p implied\n"
        "    VERSE v\n"
        "      ARGUMENT \"1\"\n"
        "    TEXT \"x\"\n");
}

static void run_versions_tests(void)
{
    RUN(test_usfm_line_selects_version);
    RUN(test_unsupported_version);
    RUN(test_depth_limit);
    RUN(test_verse_closes_heading);
}

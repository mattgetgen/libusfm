#ifndef USFM_HELPERS_H
#define USFM_HELPERS_H
/* Parse-and-compare helpers. Every parse is also run through
 * check_ast(), so each tree test is an invariant test too. */

static char helper_buf[16384];

static USFM_Ast *parse_with_version(const char *usfm, uint8_t version)
{
    USFM_Config config;
    USFM_Err err;
    memset(&config, 0, sizeof config);
    config.version = version;
    return USFM_Ast_Parse(usfm, (uint32_t)strlen(usfm), &config, &err);
}

/* Every tree and diagnostics check also parses with the fast path off,
 * and with per-run UTF-8 checks, and requires identical results. */
static void check_fast_equal_at(const char *file, int line, const char *usfm, uint8_t version)
{
    USFM_Config config;
    char label[256];
    memset(&config, 0, sizeof config);
    config.version = version;
    snprintf(label, sizeof label, "%s:%d", file, line);
    usfm_test_checks++;
    if (check_fast_path(usfm, (uint32_t)strlen(usfm), &config, 0, label) != 0) {
        usfm_test_failures++;
        fprintf(stderr, "%s: fast path differs for: %s\n", label, usfm);
    }
    usfm_test_checks++;
    if (check_utf8_reference(usfm, (uint32_t)strlen(usfm), &config, 0, label) != 0) {
        usfm_test_failures++;
        fprintf(stderr, "%s: UTF-8 validation differs from the reference for: %s\n", label, usfm);
    }
}

static void check_tree_at(const char *file, int line, const char *usfm, const char *expected)
{
    USFM_Ast *ast = parse_with_version(usfm, USFM_VERSION_AUTO);
    check_fast_equal_at(file, line, usfm, USFM_VERSION_AUTO);
    usfm_test_checks++;
    if (ast == NULL) {
        usfm_test_failures++;
        fprintf(stderr, "%s:%d: parse returned NULL\n", file, line);
        return;
    }
    if (check_ast(ast) != 0) {
        usfm_test_failures++;
        fprintf(stderr, "%s:%d: invariants failed for: %s\n", file, line, usfm);
    }
    dump_tree(ast, helper_buf, sizeof helper_buf);
    if (strcmp(helper_buf, expected) != 0) {
        usfm_test_failures++;
        fprintf(stderr, "%s:%d: tree mismatch for: %s\n--- expected\n%s--- actual\n%s",
                file, line, usfm, expected, helper_buf);
    }
    USFM_Ast_Free(ast);
}

static void check_diags_at(const char *file, int line, const char *usfm, uint8_t version,
                           const char *expected)
{
    USFM_Ast *ast = parse_with_version(usfm, version);
    check_fast_equal_at(file, line, usfm, version);
    usfm_test_checks++;
    if (ast == NULL) {
        usfm_test_failures++;
        fprintf(stderr, "%s:%d: parse returned NULL\n", file, line);
        return;
    }
    dump_diags(ast, helper_buf, sizeof helper_buf);
    if (strcmp(helper_buf, expected) != 0) {
        usfm_test_failures++;
        fprintf(stderr, "%s:%d: diagnostics mismatch for: %s\n--- expected\n%s--- actual\n%s",
                file, line, usfm, expected, helper_buf);
    }
    USFM_Ast_Free(ast);
}

#define CHECK_TREE(usfm, expected) check_tree_at(__FILE__, __LINE__, (usfm), (expected))
#define CHECK_DIAGS(usfm, version, expected) \
    check_diags_at(__FILE__, __LINE__, (usfm), (version), (expected))

#endif

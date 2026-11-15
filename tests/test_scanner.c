static char scan_buf[4096];

static const char *scan(const char *input, int pipe_is_attr)
{
    dump_tokens(input, pipe_is_attr, scan_buf, sizeof scan_buf);
    return scan_buf;
}

static void test_scan_id_line(void)
{
    CHECK_STR(scan("\\id GEN Test\n", 0), "M:id A\"GEN\" T\"Test\\n\"/w");
}

static void test_scan_levels(void)
{
    CHECK_STR(scan("\\q2 text", 0), "M:q2 T\"text\"");
    CHECK_STR(scan("\\k1 x", 0), "M:k1 T\"x\"");
    CHECK_STR(scan("\\s1e x", 0), "M:s1e T\"x\"");
    CHECK_STR(scan("\\tc1-2 x", 0), "M:tc1 T\"x\"");
}

static void test_scan_nested_and_closers(void)
{
    CHECK_STR(scan("\\+nd Lord\\+nd*", 0), "M:+nd T\"Lord\" E:+nd");
    CHECK_STR(scan("\\nd x\\nd* y", 0), "M:nd T\"x\" E:nd T\" y\"");
}

static void test_scan_milestone_with_attributes(void)
{
    CHECK_STR(scan("\\qt1-s |who=\"Pilate\"\\*", 1),
              "M:qt1-s @\"|who=\\\"Pilate\\\"\" E:*");
    CHECK_STR(scan("\\ts\\*", 0), "M:ts E:*");
}

static void test_scan_escapes_stay_in_text(void)
{
    CHECK_STR(scan("a\\/\\/b \\\\ c\\|d", 1), "T\"a\\/\\/b \\\\ c\\|d\"/e");
}

static void test_scan_optbreak_and_nbsp(void)
{
    CHECK_STR(scan("a//b~c", 0), "T\"a\" // T\"b~c\"/n");
    CHECK_STR(scan("a/b", 0), "T\"a/b\"");
}

static void test_scan_pipe_is_text_unless_attr(void)
{
    CHECK_STR(scan("a|b", 0), "T\"a|b\"");
    CHECK_STR(scan("a|b", 1), "T\"a\" @\"|b\"");
}

static void test_scan_unknown_and_lone_backslash(void)
{
    CHECK_STR(scan("\\zfoo x", 0), "M:?zfoo T\"x\"");
    CHECK_STR(scan("a\\", 0), "T\"a\" M:?");
}

static void test_scan_arguments(void)
{
    CHECK_STR(scan("\\v 3-5a text", 0), "M:v A\"3-5a\" T\"text\"");
    CHECK_STR(scan("\\c 1\n\\p", 0), "M:c A\"1\" M:p");
    CHECK_STR(scan("\\v\n\\p", 0), "M:v M:p");      /* missing argument */
    CHECK_STR(scan("\\f + \\fr 1.1 \\ft x\\f*", 0),
              "M:f A\"+\" M:fr T\"1.1 \" M:ft T\"x\" E:f");
}

static void test_scan_periph_argument_line(void)
{
    CHECK_STR(scan("\\periph Title Page|id=\"title\"\n\\p x", 1),
              "M:periph A\"Title Page\" @\"|id=\\\"title\\\"\" M:p T\"x\"");
}

static void test_scan_crlf_and_bom(void)
{
    CHECK_STR(scan("\\p\r\ntext", 0), "M:p T\"text\"");
    CHECK_STR(scan("\xEF\xBB\xBF\\id GEN", 0), "M:id A\"GEN\"");
}

static void test_scan_milestone_suffix_roles(void)
{
    CHECK_STR(scan("\\p-s x", 0), "M:?p-s T\"x\"");
    CHECK_STR(scan("\\qt-s x", 0), "M:qt-s T\"x\"");
}

static void test_scan_level_over_15_is_unknown(void)
{
    CHECK_STR(scan("\\q16 x", 0), "M:?q16 T\"x\"");
    CHECK_STR(scan("\\tc12 x", 0), "M:tc12 T\"x\"");
}

static void test_scan_attribute_escapes(void)
{
    CHECK_STR(scan("\\w x|id=\"a\\=b\"\\w*", 1),
              "M:w T\"x\" @\"|id=\\\"a\\=b\\\"\" E:w");
}

static void test_scan_edge_cases(void)
{
    CHECK_STR(scan("\\+", 0), "M:+?");
    CHECK_STR(scan("a\r", 0), "T\"a\\r\"/w");
    CHECK_STR(scan("\xEF\xBB", 0), "T\"\xEF\xBB\"");
    CHECK_STR(scan("\\q- x", 0), "M:?q- T\"x\"");
}

static void run_scanner_tests(void)
{
    RUN(test_scan_id_line);
    RUN(test_scan_levels);
    RUN(test_scan_nested_and_closers);
    RUN(test_scan_milestone_with_attributes);
    RUN(test_scan_escapes_stay_in_text);
    RUN(test_scan_optbreak_and_nbsp);
    RUN(test_scan_pipe_is_text_unless_attr);
    RUN(test_scan_unknown_and_lone_backslash);
    RUN(test_scan_arguments);
    RUN(test_scan_periph_argument_line);
    RUN(test_scan_crlf_and_bom);
    RUN(test_scan_milestone_suffix_roles);
    RUN(test_scan_level_over_15_is_unknown);
    RUN(test_scan_attribute_escapes);
    RUN(test_scan_edge_cases);
}

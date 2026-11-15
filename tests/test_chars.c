static void test_char_explicit_close(void)
{
    CHECK_TREE("\\p \\nd LORD\\nd* said",
        "BOOK\n"
        "  PARA p\n"
        "    CHAR nd\n"
        "      TEXT \"LORD\"\n"
        "    TEXT \" said\"\n");
}

static void test_char_plus_nesting(void)
{
    CHECK_TREE("\\p \\add an \\+nd Lord\\+nd* addition\\add*",
        "BOOK\n"
        "  PARA p\n"
        "    CHAR add\n"
        "      TEXT \"an \"\n"
        "      CHAR +nd\n"
        "        TEXT \"Lord\"\n"
        "      TEXT \" addition\"\n");
}

static void test_char_plain_nesting_when_outer_closes_later(void)
{
    /* 3.1 style: \nd nests because \add* appears later. */
    CHECK_TREE("\\p \\add a \\nd b\\nd* c\\add*",
        "BOOK\n"
        "  PARA p\n"
        "    CHAR add\n"
        "      TEXT \"a \"\n"
        "      CHAR nd\n"
        "        TEXT \"b\"\n"
        "      TEXT \" c\"\n");
    CHECK_DIAGS("\\p \\add a \\nd b\\nd* c\\add*", USFM_VERSION_AUTO, "");
}

static void test_char_plain_marker_closes_outer_without_closer(void)
{
    /* 2.x style: no \add* ahead, so \nd closes \add. */
    CHECK_TREE("\\p \\add a \\nd b\\nd* c",
        "BOOK\n"
        "  PARA p\n"
        "    CHAR add\n"
        "      TEXT \"a \"\n"
        "    CHAR nd\n"
        "      TEXT \"b\"\n"
        "    TEXT \" c\"\n");
    CHECK_DIAGS("\\p \\add a \\nd b\\nd* c", USFM_VERSION_AUTO, "UNCLOSED_MARKER 3\n");
    CHECK_DIAGS("\\p \\add a \\nd b\\nd* c", USFM_VERSION_2, "");
}

static void test_char_outer_close_closes_inner(void)
{
    CHECK_TREE("\\p \\add a \\+nd b\\add* c",
        "BOOK\n"
        "  PARA p\n"
        "    CHAR add\n"
        "      TEXT \"a \"\n"
        "      CHAR +nd\n"
        "        TEXT \"b\"\n"
        "    TEXT \" c\"\n");
    CHECK_DIAGS("\\p \\add a \\+nd b\\add* c", USFM_VERSION_AUTO, "UNCLOSED_MARKER 10\n");
    CHECK_DIAGS("\\p \\add a \\+nd b\\add* c", USFM_VERSION_2, "");
}

static void test_char_stray_end_marker(void)
{
    CHECK_TREE("\\p a\\nd* b",
        "BOOK\n"
        "  PARA p\n"
        "    TEXT \"a\"\n"
        "    TEXT \" b\"\n");
    CHECK_DIAGS("\\p a\\nd* b", USFM_VERSION_AUTO, "UNEXPECTED_CLOSE 4\n");
}

static void test_char_closed_by_verse(void)
{
    CHECK_TREE("\\c 1\n\\p \\wj a \\v 2 b\\wj*",
        "BOOK\n"
        "  CHAPTER c\n"
        "    ARGUMENT \"1\"\n"
        "  PARA p\n"
        "    CHAR wj\n"
        "      TEXT \"a\"\n"
        "    VERSE v\n"
        "      ARGUMENT \"2\"\n"
        "    TEXT \"b\"\n");
    CHECK_DIAGS("\\c 1\n\\p \\wj a \\v 2 b\\wj*", USFM_VERSION_AUTO,
                "UNCLOSED_MARKER 8\nUNEXPECTED_CLOSE 20\n");
}

static void test_char_attributes(void)
{
    CHECK_TREE("\\p \\w gracious|lemma=\"grace\"\\w*",
        "BOOK\n"
        "  PARA p\n"
        "    CHAR w\n"
        "      TEXT \"gracious\"\n"
        "      ATTRIBUTES \"|lemma=\\\"grace\\\"\"\n");
}

static void test_char_lookahead_cache(void)
{
    /* Two plain \w inside \wj: both nest; the second reuses the cached
     * lookahead result. */
    CHECK_TREE("\\p \\wj \\w a\\w* \\w b\\w*\\wj*",
        "BOOK\n"
        "  PARA p\n"
        "    CHAR wj\n"
        "      CHAR w\n"
        "        TEXT \"a\"\n"
        "      TEXT \" \"\n"
        "      CHAR w\n"
        "        TEXT \"b\"\n");
}

static void run_chars_tests(void)
{
    RUN(test_char_explicit_close);
    RUN(test_char_plus_nesting);
    RUN(test_char_plain_nesting_when_outer_closes_later);
    RUN(test_char_plain_marker_closes_outer_without_closer);
    RUN(test_char_outer_close_closes_inner);
    RUN(test_char_stray_end_marker);
    RUN(test_char_closed_by_verse);
    RUN(test_char_attributes);
    RUN(test_char_lookahead_cache);
}

static void test_note_parts_close_each_other(void)
{
    CHECK_TREE("\\p a\\f + \\fr 1.1 \\ft note\\f* b",
        "BOOK\n"
        "  PARA p\n"
        "    TEXT \"a\"\n"
        "    NOTE f\n"
        "      ARGUMENT \"+\"\n"
        "      CHAR fr\n"
        "        TEXT \"1.1 \"\n"
        "      CHAR ft\n"
        "        TEXT \"note\"\n"
        "    TEXT \" b\"\n");
    CHECK_DIAGS("\\p a\\f + \\fr 1.1 \\ft note\\f* b", USFM_VERSION_AUTO, "");
}

static void test_note_missing_caller(void)
{
    CHECK_TREE("\\p \\f \\ft x\\f*",
        "BOOK\n"
        "  PARA p\n"
        "    NOTE f\n"
        "      CHAR ft\n"
        "        TEXT \"x\"\n");
    CHECK_DIAGS("\\p \\f \\ft x\\f*", USFM_VERSION_AUTO, "MISSING_CALLER 6\n");
}

static void test_note_unclosed_at_paragraph(void)
{
    CHECK_TREE("\\p a\\f + \\ft x\n\\p b",
        "BOOK\n"
        "  PARA p\n"
        "    TEXT \"a\"\n"
        "    NOTE f\n"
        "      ARGUMENT \"+\"\n"
        "      CHAR ft\n"
        "        TEXT \"x\"\n"
        "  PARA p\n"
        "    TEXT \"b\"\n");
    CHECK_DIAGS("\\p a\\f + \\ft x\n\\p b", USFM_VERSION_AUTO, "UNCLOSED_MARKER 4\n");
}

static void test_note_directly_after_chapter(void)
{
    CHECK_TREE("\\c 1\n\\f + \\ft x\\f*\n\\p y",
        "BOOK\n"
        "  CHAPTER c\n"
        "    ARGUMENT \"1\"\n"
        "  NOTE f\n"
        "    ARGUMENT \"+\"\n"
        "    CHAR ft\n"
        "      TEXT \"x\"\n"
        "  PARA p\n"
        "    TEXT \"y\"\n");
}

static void test_note_closed_by_verse(void)
{
    CHECK_DIAGS("\\c 1\n\\p \\f + \\ft a \\v 2 b\\f*", USFM_VERSION_AUTO,
                "UNCLOSED_MARKER 8\nUNEXPECTED_CLOSE 25\n");
}

static void test_note_part_explicitly_closed(void)
{
    CHECK_TREE("\\p \\f + \\fr 1.1\\fr* \\ft x\\f*",
        "BOOK\n"
        "  PARA p\n"
        "    NOTE f\n"
        "      ARGUMENT \"+\"\n"
        "      CHAR fr\n"
        "        TEXT \"1.1\"\n"
        "      TEXT \" \"\n"
        "      CHAR ft\n"
        "        TEXT \"x\"\n");
    /* A second \ft* after the part closed is ignored silently. */
    CHECK_DIAGS("\\p \\f + \\ft x\\ft*\\ft*\\f*", USFM_VERSION_AUTO, "");
}

static void test_note_inside_char_span(void)
{
    CHECK_TREE("\\p \\w a\\f + \\ft n\\f*\\w*",
        "BOOK\n"
        "  PARA p\n"
        "    CHAR w\n"
        "      TEXT \"a\"\n"
        "      NOTE f\n"
        "        ARGUMENT \"+\"\n"
        "        CHAR ft\n"
        "          TEXT \"n\"\n");
}

static void test_note_char_inside_part(void)
{
    CHECK_TREE("\\p \\f + \\ft a \\nd b\\nd*\\f*",
        "BOOK\n"
        "  PARA p\n"
        "    NOTE f\n"
        "      ARGUMENT \"+\"\n"
        "      CHAR ft\n"
        "        TEXT \"a \"\n"
        "        CHAR nd\n"
        "          TEXT \"b\"\n");
}

static void test_note_part_outside_note_is_char(void)
{
    CHECK_TREE("\\p \\xt Mk 1.1\\xt*",
        "BOOK\n"
        "  PARA p\n"
        "    CHAR xt\n"
        "      TEXT \"Mk 1.1\"\n");
}

static void test_cross_reference(void)
{
    CHECK_TREE("\\p a\\x - \\xo 1.1: \\xt Mk 1.1\\x*",
        "BOOK\n"
        "  PARA p\n"
        "    TEXT \"a\"\n"
        "    NOTE x\n"
        "      ARGUMENT \"-\"\n"
        "      CHAR xo\n"
        "        TEXT \"1.1: \"\n"
        "      CHAR xt\n"
        "        TEXT \"Mk 1.1\"\n");
}

static void test_note_implicit_close_before_part(void)
{
    /* \add is closed at \nd (its closer lies beyond the \fq stop); the
     * later \add* is stray. */
    const char *in = "\\p \\f + \\ft \\add a \\nd b \\fq c\\add*\\f*";
    CHECK_TREE(in,
        "BOOK\n"
        "  PARA p\n"
        "    NOTE f\n"
        "      ARGUMENT \"+\"\n"
        "      CHAR ft\n"
        "        CHAR add\n"
        "          TEXT \"a \"\n"
        "        CHAR nd\n"
        "          TEXT \"b \"\n"
        "      CHAR fq\n"
        "        TEXT \"c\"\n");
    CHECK_DIAGS(in, USFM_VERSION_AUTO,
                "UNCLOSED_MARKER 12\nUNCLOSED_MARKER 19\nUNEXPECTED_CLOSE 30\n");
}

static void run_notes_tests(void)
{
    RUN(test_note_parts_close_each_other);
    RUN(test_note_missing_caller);
    RUN(test_note_unclosed_at_paragraph);
    RUN(test_note_directly_after_chapter);
    RUN(test_note_closed_by_verse);
    RUN(test_note_part_explicitly_closed);
    RUN(test_note_inside_char_span);
    RUN(test_note_char_inside_part);
    RUN(test_note_part_outside_note_is_char);
    RUN(test_cross_reference);
    RUN(test_note_implicit_close_before_part);
}

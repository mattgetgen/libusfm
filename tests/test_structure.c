static void test_table(void)
{
    CHECK_TREE("\\tr \\th1 A\\th2 B\n\\tr \\tc1 1\\tc2 2\n\\p after",
        "BOOK\n"
        "  TABLE\n"
        "    ROW tr\n"
        "      CELL th1\n"
        "        TEXT \"A\"\n"
        "      CELL th2\n"
        "        TEXT \"B\"\n"
        "    ROW tr\n"
        "      CELL tc1\n"
        "        TEXT \"1\"\n"
        "      CELL tc2\n"
        "        TEXT \"2\"\n"
        "  PARA p\n"
        "    TEXT \"after\"\n");
}

static void test_cell_outside_table(void)
{
    CHECK_TREE("\\c 1\n\\tc1 x",
        "BOOK\n"
        "  CHAPTER c\n"
        "    ARGUMENT \"1\"\n"
        "  TABLE\n"
        "    ROW tr\n"
        "      CELL tc1\n"
        "        TEXT \"x\"\n");
    CHECK_DIAGS("\\c 1\n\\tc1 x", USFM_VERSION_AUTO, "INVALID_NESTING 5\n");
}

static void test_milestones(void)
{
    CHECK_TREE("\\p a\\qt-s |who=\"Pilate\"\\*b\\qt-e\\*",
        "BOOK\n"
        "  PARA p\n"
        "    TEXT \"a\"\n"
        "    MILESTONE qt-s closed\n"
        "      ATTRIBUTES \"|who=\\\"Pilate\\\"\"\n"
        "    TEXT \"b\"\n"
        "    MILESTONE qt-e closed\n");
    /* \ts without \* is still a milestone; the next token ends it. */
    CHECK_TREE("\\p a\\ts b",
        "BOOK\n"
        "  PARA p\n"
        "    TEXT \"a\"\n"
        "    MILESTONE ts\n"
        "    TEXT \"b\"\n");
}

static void test_unknown_markers(void)
{
    CHECK_TREE("\\zp line start\n\\p a \\zx b \\zc c\\zc* \\zm\\*",
        "BOOK\n"
        "  PARA ?zp\n"
        "    TEXT \"line start\"\n"
        "  PARA p\n"
        "    TEXT \"a \"\n"
        "    MILESTONE ?zx\n"
        "    TEXT \"b \"\n"
        "    CHAR ?zc\n"
        "      TEXT \"c\"\n"
        "    TEXT \" \"\n"
        "    MILESTONE ?zm closed\n");
    CHECK_DIAGS("\\zp line start\n\\p a \\zx b \\zc c\\zc* \\zm\\*", USFM_VERSION_AUTO, "");
    CHECK_DIAGS("\\p \\xyz a", USFM_VERSION_AUTO, "UNKNOWN_MARKER 3\n");
}

static void test_sidebar(void)
{
    CHECK_TREE("\\esb\n\\p in\n\\esbe\n\\p out",
        "BOOK\n"
        "  SIDEBAR esb\n"
        "    PARA p\n"
        "      TEXT \"in\"\n"
        "  PARA p\n"
        "    TEXT \"out\"\n");
    CHECK_DIAGS("\\p a\\esbe", USFM_VERSION_AUTO, "UNEXPECTED_CLOSE 4\n");
}

static void test_peripheral(void)
{
    CHECK_TREE("\\id FRT\n\\periph Title Page|id=\"title\"\n\\p x",
        "BOOK\n"
        "  HEADER id\n"
        "    ARGUMENT \"FRT\"\n"
        "  PERIPHERAL periph\n"
        "    ARGUMENT \"Title Page\"\n"
        "    ATTRIBUTES \"|id=\\\"title\\\"\"\n"
        "    PARA p\n"
        "      TEXT \"x\"\n");
}

static void test_figure(void)
{
    CHECK_TREE("\\p \\fig At once|src=\"a.jpg\" size=\"span\"\\fig*",
        "BOOK\n"
        "  PARA p\n"
        "    FIGURE fig\n"
        "      TEXT \"At once\"\n"
        "      ATTRIBUTES \"|src=\\\"a.jpg\\\" size=\\\"span\\\"\"\n");
}

static void test_unknown_char_span_in_para(void)
{
    CHECK_TREE("\\p \\zq a\\zq* b",
        "BOOK\n"
        "  PARA p\n"
        "    CHAR ?zq\n"
        "      TEXT \"a\"\n"
        "    TEXT \" b\"\n");
}

static void test_unknown_milestone_extra_blanks(void)
{
    CHECK_TREE("\\p a \\zx  |a=\"b\"\\* c",
        "BOOK\n"
        "  PARA p\n"
        "    TEXT \"a \"\n"
        "    MILESTONE ?zx closed\n"
        "      ATTRIBUTES \"|a=\\\"b\\\"\"\n"
        "    TEXT \" c\"\n");
}

static void run_structure_tests(void)
{
    RUN(test_table);
    RUN(test_cell_outside_table);
    RUN(test_milestones);
    RUN(test_unknown_markers);
    RUN(test_sidebar);
    RUN(test_peripheral);
    RUN(test_figure);
    RUN(test_unknown_milestone_extra_blanks);
    RUN(test_unknown_char_span_in_para);
}

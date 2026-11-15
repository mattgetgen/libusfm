static void test_parse_empty_input(void)
{
    CHECK_TREE("", "BOOK\n");
    CHECK_TREE("\n\n", "BOOK\n");
}

static void test_parse_basic_book(void)
{
    CHECK_TREE("\\id GEN Test\n\\c 1\n\\p\n\\v 1 In the beginning.\n\\v 2 Next.\n",
        "BOOK\n"
        "  HEADER id\n"
        "    ARGUMENT \"GEN\"\n"
        "    TEXT \"Test\"\n"
        "  CHAPTER c\n"
        "    ARGUMENT \"1\"\n"
        "  PARA p\n"
        "    VERSE v\n"
        "      ARGUMENT \"1\"\n"
        "    TEXT \"In the beginning.\"\n"
        "    VERSE v\n"
        "      ARGUMENT \"2\"\n"
        "    TEXT \"Next.\"\n");
}

static void test_parse_poetry_and_headings(void)
{
    CHECK_TREE("\\c 1\n\\s1 Title\n\\q1 \\v 1 Line one\n\\q2 line two\n",
        "BOOK\n"
        "  CHAPTER c\n"
        "    ARGUMENT \"1\"\n"
        "  PARA s1 heading\n"
        "    TEXT \"Title\"\n"
        "  PARA q1\n"
        "    VERSE v\n"
        "      ARGUMENT \"1\"\n"
        "    TEXT \"Line one\"\n"
        "  PARA q2\n"
        "    TEXT \"line two\"\n");
}

static void test_parse_implied_paragraph(void)
{
    CHECK_TREE("\\c 1\n\\v 1 Text\n",
        "BOOK\n"
        "  CHAPTER c\n"
        "    ARGUMENT \"1\"\n"
        "  PARA p implied\n"
        "    VERSE v\n"
        "      ARGUMENT \"1\"\n"
        "    TEXT \"Text\"\n");
    CHECK_DIAGS("\\c 1\n\\v 1 Text\n", USFM_VERSION_AUTO, "IMPLIED_PARAGRAPH 5\n");
    CHECK_TREE("\\c 1\nstray\n",
        "BOOK\n"
        "  CHAPTER c\n"
        "    ARGUMENT \"1\"\n"
        "  PARA p implied\n"
        "    TEXT \"stray\"\n");
}

static void test_parse_trailing_whitespace(void)
{
    /* "\c 1  " (trailing spaces, as in WEB) leaves only blank text at
     * block level, which is dropped. */
    CHECK_TREE("\\c 1  \n\\p a  \n\\p b",
        "BOOK\n"
        "  CHAPTER c\n"
        "    ARGUMENT \"1\"\n"
        "  PARA p\n"
        "    TEXT \"a\"\n"
        "  PARA p\n"
        "    TEXT \"b\"\n");
}

static void test_parse_headers(void)
{
    CHECK_TREE("\\id GEN\n\\h Genesis\n\\toc1 The First Book\n\\mt1 Genesis\n",
        "BOOK\n"
        "  HEADER id\n"
        "    ARGUMENT \"GEN\"\n"
        "  HEADER h\n"
        "    TEXT \"Genesis\"\n"
        "  HEADER toc1\n"
        "    TEXT \"The First Book\"\n"
        "  PARA mt1 heading\n"
        "    TEXT \"Genesis\"\n");
}

static void test_parse_number_and_code_diagnostics(void)
{
    CHECK_DIAGS("\\id gen\n", USFM_VERSION_AUTO, "INVALID_BOOK_CODE 4\n");
    CHECK_DIAGS("\\c x\n", USFM_VERSION_AUTO, "INVALID_NUMBER 3\n");
    CHECK_DIAGS("\\c 0\n", USFM_VERSION_AUTO, "INVALID_NUMBER 3\n");
    CHECK_DIAGS("\\c 1\n\\p \\v 5-3 x", USFM_VERSION_AUTO, "INVALID_NUMBER 11\n");
    CHECK_DIAGS("\\p \\v 1 x", USFM_VERSION_AUTO, "VERSE_BEFORE_CHAPTER 3\n");
    CHECK_DIAGS("\\c 1\n\\p \\v\n", USFM_VERSION_AUTO, "INVALID_NUMBER 11\n");
}

static void test_parse_book_code_and_tables(void)
{
    USFM_Ast *ast = parse_with_version("\\id GEN x\n\\c 1\n\\p \\v 1 a \\v 2 b\n\\c 2\n\\p \\v 1 c",
                                       USFM_VERSION_AUTO);
    uint32_t chapter_count, verse_count;
    const USFM_ChapterEntry *chapters;
    const USFM_VerseEntry *verses;
    CHECK(ast != NULL);
    if (ast == NULL) return;
    CHECK_STR(USFM_Ast_GetBookCode(ast), "GEN");
    CHECK_EQ(USFM_Ast_GetMaxChapterNumber(ast), 2);
    CHECK_EQ(USFM_Ast_GetVersion(ast), USFM_VERSION_3_1);
    chapters = USFM_Ast_GetChapters(ast, &chapter_count);
    verses = USFM_Ast_GetVerses(ast, &verse_count);
    CHECK_EQ(chapter_count, 2);
    CHECK_EQ(verse_count, 3);
    CHECK_EQ(chapters[0].number, 1);
    CHECK_EQ(chapters[0].first_verse, 0);
    CHECK_EQ(chapters[0].verse_count, 2);
    CHECK_EQ(chapters[0].end_node, chapters[1].node);
    CHECK_EQ(chapters[1].first_verse, 2);
    CHECK_EQ(chapters[1].verse_count, 1);
    CHECK_EQ(verses[0].end_node, verses[1].node);
    CHECK_EQ(verses[1].end_node, chapters[1].node);
    CHECK_EQ(verses[2].ref.chapter, 2);
    CHECK_EQ(check_ast(ast), 0);
    USFM_Ast_Free(ast);
}

static void test_parse_verse_ranges(void)
{
    USFM_Ast *ast = parse_with_version("\\c 3\n\\p \\v 3-5a x", USFM_VERSION_AUTO);
    uint32_t verse_count;
    const USFM_VerseEntry *verses;
    CHECK(ast != NULL);
    if (ast == NULL) return;
    verses = USFM_Ast_GetVerses(ast, &verse_count);
    CHECK_EQ(verse_count, 1);
    CHECK_EQ(verses[0].ref.chapter, 3);
    CHECK_EQ(verses[0].ref.verse_first, 3);
    CHECK_EQ(verses[0].ref.verse_last, 5);
    CHECK_EQ(verses[0].ref.segment_first, 0);
    CHECK_EQ(verses[0].ref.segment_last, 1);
    USFM_Ast_Free(ast);
}

static void test_parse_bad_arguments(void)
{
    USFM_Err err;
    USFM_Config config;
    memset(&config, 0, sizeof config);
    CHECK(USFM_Ast_Parse(NULL, 5, NULL, &err) == NULL);
    CHECK_EQ(err.code, USFM_ERR_INVALID_ARG);
    config.version = 9;
    CHECK(USFM_Ast_Parse("\\p", 2, &config, &err) == NULL);
    CHECK_EQ(err.code, USFM_ERR_INVALID_ARG);
    CHECK(USFM_Ast_Parse("\\p", USFM_INPUT_MAX + 1u, NULL, &err) == NULL);
    CHECK_EQ(err.code, USFM_ERR_INPUT_TOO_LARGE);
}

static void run_parser_tests(void)
{
    RUN(test_parse_empty_input);
    RUN(test_parse_basic_book);
    RUN(test_parse_poetry_and_headings);
    RUN(test_parse_implied_paragraph);
    RUN(test_parse_trailing_whitespace);
    RUN(test_parse_headers);
    RUN(test_parse_number_and_code_diagnostics);
    RUN(test_parse_book_code_and_tables);
    RUN(test_parse_verse_ranges);
    RUN(test_parse_bad_arguments);
}

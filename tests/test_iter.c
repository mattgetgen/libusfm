static const char *iter_doc =
    "\\c 1\n\\p\n\\v 1 a\n\\v 2 b \\f + \\ft n\\f* c\n\\s1 H\n\\p\n\\v 3 d\n"
    "\\c 2\n\\p\n\\v 1 e\n";

/* Concatenates the TEXT items of verse (chapter, verse) under flags. */
static void verse_text(const USFM_Ast *ast, uint16_t chapter, uint16_t verse,
                       uint32_t flags, char *out, size_t cap)
{
    USFM_VerseEntry entry;
    USFM_VerseIter it;
    USFM_VerseItem item;
    uint32_t input_length;
    const char *input = USFM_Ast_GetInput(ast, &input_length);
    size_t len = 0;
    out[0] = '\0';
    if (USFM_Ast_FindVerse(ast, chapter, verse, &entry) == USFM_NIL) return;
    USFM_VerseIter_Init(&it, ast, &entry, flags);
    while (USFM_VerseIter_Next(&it, &item)) {
        if (item.kind != USFM_VITEM_TEXT) continue;
        if (len + item.text.length + 1 > cap) break;
        memcpy(out + len, input + item.text.offset, item.text.length);
        len += item.text.length;
        out[len] = '\0';
    }
}

static void test_find_chapter_and_verse(void)
{
    USFM_Ast *ast = parse_with_version(iter_doc, USFM_VERSION_AUTO);
    USFM_ChapterEntry chapter;
    USFM_VerseEntry verse;
    uint32_t count;
    const USFM_Node *nodes = USFM_Ast_GetNodes(ast, &count);
    CHECK(USFM_Ast_FindChapter(ast, 2, &chapter) != USFM_NIL);
    CHECK_EQ(chapter.number, 2);
    CHECK_EQ(nodes[chapter.node].kind, USFM_AST_CHAPTER);
    CHECK_EQ(USFM_Ast_FindChapter(ast, 3, &chapter), USFM_NIL);
    CHECK(USFM_Ast_FindVerse(ast, 1, 2, &verse) != USFM_NIL);
    CHECK_EQ(verse.ref.verse_first, 2);
    CHECK_EQ(USFM_Ast_FindVerse(ast, 1, 9, &verse), USFM_NIL);
    CHECK_EQ(USFM_Ast_FindVerse(ast, 5, 1, &verse), USFM_NIL);
    USFM_Ast_Free(ast);
}

static void test_find_verse_in_range(void)
{
    USFM_Ast *ast = parse_with_version("\\c 1\n\\p \\v 1 a \\v 2-4 b \\v 5 c", USFM_VERSION_AUTO);
    USFM_VerseEntry verse;
    CHECK(USFM_Ast_FindVerse(ast, 1, 3, &verse) != USFM_NIL);
    CHECK_EQ(verse.ref.verse_first, 2);
    CHECK_EQ(verse.ref.verse_last, 4);
    CHECK(USFM_Ast_FindVerse(ast, 1, 5, &verse) != USFM_NIL);
    CHECK_EQ(verse.ref.verse_first, 5);
    USFM_Ast_Free(ast);
}

static void test_find_unsorted_and_repeated_chapters(void)
{
    USFM_Ast *ast = parse_with_version("\\c 2\n\\c 1\n\\c 1\n", USFM_VERSION_AUTO);
    USFM_ChapterEntry chapter;
    uint32_t count;
    const USFM_ChapterEntry *chapters = USFM_Ast_GetChapters(ast, &count);
    CHECK_EQ(count, 3);
    CHECK_EQ(USFM_Ast_FindChapter(ast, 1, &chapter), chapters[1].node);
    CHECK_EQ(USFM_Ast_FindChapter(ast, 2, &chapter), chapters[0].node);
    USFM_Ast_Free(ast);
}

static void test_verse_iterator_flags(void)
{
    USFM_Ast *ast = parse_with_version(iter_doc, USFM_VERSION_AUTO);
    char text[256];
    verse_text(ast, 1, 1, 0, text, sizeof text);
    CHECK_STR(text, "a");
    verse_text(ast, 1, 2, 0, text, sizeof text);
    CHECK_STR(text, "b  c");
    verse_text(ast, 1, 2, USFM_VI_INCLUDE_NOTES, text, sizeof text);
    CHECK_STR(text, "b n c");
    verse_text(ast, 1, 2, USFM_VI_INCLUDE_HEADINGS, text, sizeof text);
    CHECK_STR(text, "b  cH");
    verse_text(ast, 1, 3, 0, text, sizeof text);
    CHECK_STR(text, "d");
    verse_text(ast, 2, 1, 0, text, sizeof text);
    CHECK_STR(text, "e");
    USFM_Ast_Free(ast);
}

static void test_verse_iterator_items(void)
{
    USFM_Ast *ast = parse_with_version(iter_doc, USFM_VERSION_AUTO);
    USFM_VerseEntry entry;
    USFM_VerseIter it;
    USFM_VerseItem item;
    uint32_t count, texts = 0, notes = 0, breaks = 0;
    const USFM_Node *nodes = USFM_Ast_GetNodes(ast, &count);
    USFM_Ast_FindVerse(ast, 1, 2, &entry);
    USFM_VerseIter_Init(&it, ast, &entry,
                        USFM_VI_REPORT_NOTES | USFM_VI_REPORT_BREAKS | USFM_VI_INCLUDE_NOTES);
    while (USFM_VerseIter_Next(&it, &item)) {
        if (item.kind == USFM_VITEM_TEXT) {
            texts++;
            CHECK(item.para != USFM_NIL);
            CHECK_EQ(nodes[item.para].kind, USFM_AST_PARA);
            if (item.in_note) {
                CHECK(item.style != USFM_NIL);
                CHECK_EQ(nodes[item.style].marker, USFM_M_FT);
            }
        } else if (item.kind == USFM_VITEM_NOTE) {
            notes++;
            CHECK_EQ(nodes[item.node].kind, USFM_AST_NOTE);
        } else if (item.kind == USFM_VITEM_PARA_BREAK) {
            breaks++;
        }
    }
    CHECK_EQ(texts, 3);         /* "b ", "n", " c" */
    CHECK_EQ(notes, 1);
    CHECK_EQ(breaks, 1);        /* the \p before verse 3; \s1 is skipped */
    USFM_Ast_Free(ast);
}

static void test_chapter_iterators(void)
{
    USFM_Ast *ast = parse_with_version(iter_doc, USFM_VERSION_AUTO);
    USFM_ChapterIter chapters;
    USFM_ChapterEntry chapter;
    uint32_t chapter_count = 0, verse_counts[2] = {0, 0};
    USFM_ChapterIter_Init(&chapters, ast);
    while (USFM_ChapterIter_Next(&chapters, &chapter)) {
        USFM_ChapterVerseIter verses;
        USFM_VerseEntry verse;
        USFM_ChapterVerseIter_Init(&verses, ast, &chapter);
        while (USFM_ChapterVerseIter_Next(&verses, &verse)) {
            CHECK_EQ(verse.ref.chapter, chapter.number);
            if (chapter_count < 2) verse_counts[chapter_count]++;
        }
        chapter_count++;
    }
    CHECK_EQ(chapter_count, 2);
    CHECK_EQ(verse_counts[0], 3);
    CHECK_EQ(verse_counts[1], 1);
    USFM_Ast_Free(ast);
}

static void test_find_verse_segments_and_overlap(void)
{
    USFM_Ast *ast = parse_with_version("\\c 1\n\\p \\v 3a a \\v 3b b \\v 4 c", USFM_VERSION_AUTO);
    USFM_VerseEntry verse;
    CHECK(USFM_Ast_FindVerse(ast, 1, 3, &verse) != USFM_NIL);
    CHECK_EQ(verse.ref.segment_first, 1);
    CHECK(USFM_Ast_FindVerse(ast, 1, 4, &verse) != USFM_NIL);
    CHECK_EQ(verse.ref.verse_first, 4);
    USFM_Ast_Free(ast);
    /* Unsorted chapter: the linear path returns the first containing entry. */
    ast = parse_with_version("\\c 1\n\\p \\v 1-5 a \\v 3 b", USFM_VERSION_AUTO);
    CHECK(USFM_Ast_FindVerse(ast, 1, 4, &verse) != USFM_NIL);
    CHECK_EQ(verse.ref.verse_first, 1);
    CHECK_EQ(verse.ref.verse_last, 5);
    USFM_Ast_Free(ast);
}

static void test_find_verse_reversed_range(void)
{
    USFM_Ast *ast = parse_with_version("\\c 1\n\\p \\v 3-6 a \\v 7-4 b \\v 8 c", USFM_VERSION_AUTO);
    USFM_VerseEntry verse;
    CHECK(USFM_Ast_FindVerse(ast, 1, 5, &verse) != USFM_NIL);
    CHECK_EQ(verse.ref.verse_first, 3);
    CHECK(USFM_Ast_FindVerse(ast, 1, 8, &verse) != USFM_NIL);
    CHECK_EQ(verse.ref.verse_first, 8);
    USFM_Ast_Free(ast);
}

static void test_last_verse_ends_at_periph(void)
{
    USFM_Ast *ast = parse_with_version(
        "\\id GEN\n\\c 1\n\\p \\v 1 a\n\\periph X\n\\p peripheral text", USFM_VERSION_AUTO);
    char text[256];
    uint32_t count;
    const USFM_ChapterEntry *chapters = USFM_Ast_GetChapters(ast, &count);
    const USFM_Node *nodes = USFM_Ast_GetNodes(ast, &count);
    verse_text(ast, 1, 1, 0, text, sizeof text);
    CHECK_STR(text, "a");
    CHECK_EQ(nodes[chapters[0].end_node].kind, USFM_AST_PERIPHERAL);
    CHECK_EQ(check_ast(ast), 0);
    USFM_Ast_Free(ast);
}

/* After \id or \periph no chapter is open: a verse there belongs to no
 * chapter, exactly like a verse before the first \c. */
static void test_verse_after_division_is_chapterless(void)
{
    static const char *docs[2] = {
        "\\id GEN\n\\c 1\n\\p \\v 1 a\n\\id EXO\n\\p \\v 1 b",
        "\\id GEN\n\\c 1\n\\p \\v 1 a\n\\periph X\n\\p \\v 1 b"
    };
    int d;
    for (d = 0; d < 2; d++) {
        USFM_Ast *ast = parse_with_version(docs[d], USFM_VERSION_AUTO);
        uint32_t chapter_count, verse_count, diag_count;
        const USFM_ChapterEntry *chapters = USFM_Ast_GetChapters(ast, &chapter_count);
        const USFM_VerseEntry *verses = USFM_Ast_GetVerses(ast, &verse_count);
        const USFM_Diagnostic *diags = USFM_Ast_GetDiagnostics(ast, &diag_count);
        CHECK_EQ(chapter_count, 1);
        CHECK_EQ(chapters[0].verse_count, 1);
        CHECK_EQ(verse_count, 2);
        CHECK_EQ(verses[0].ref.chapter, 1);
        CHECK_EQ(verses[1].ref.chapter, 0);
        CHECK_EQ(diag_count, 1);
        if (diag_count == 1) CHECK_EQ(diags[0].code, USFM_DIAGNOSTIC_VERSE_BEFORE_CHAPTER);
        CHECK_EQ(check_ast(ast), 0);
        USFM_Ast_Free(ast);
    }
    /* A \c after the division opens a chapter again. */
    {
        USFM_Ast *ast = parse_with_version("\\id GEN\n\\c 1\n\\p \\v 1 a\n\\id EXO\n\\c 1\n\\p \\v 1 b",
                                           USFM_VERSION_AUTO);
        uint32_t verse_count, diag_count;
        const USFM_VerseEntry *verses = USFM_Ast_GetVerses(ast, &verse_count);
        USFM_Ast_GetDiagnostics(ast, &diag_count);
        CHECK_EQ(verse_count, 2);
        if (verse_count == 2) CHECK_EQ(verses[1].ref.chapter, 1);
        CHECK_EQ(diag_count, 0);
        USFM_Ast_Free(ast);
    }
}

static void run_iter_tests(void)
{
    RUN(test_find_chapter_and_verse);
    RUN(test_find_verse_in_range);
    RUN(test_find_verse_segments_and_overlap);
    RUN(test_find_unsorted_and_repeated_chapters);
    RUN(test_verse_iterator_flags);
    RUN(test_verse_iterator_items);
    RUN(test_chapter_iterators);
    RUN(test_find_verse_reversed_range);
    RUN(test_last_verse_ends_at_periph);
    RUN(test_verse_after_division_is_chapterless);
}

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

static void run_basics_tests(void)
{
    RUN(test_struct_sizes);
    RUN(test_marker_hash_uses_length);
}

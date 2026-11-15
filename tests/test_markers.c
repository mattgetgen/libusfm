static void test_every_row_finds_itself(void)
{
    size_t count = sizeof USFM_MarkerRows / sizeof USFM_MarkerRows[0];
    for (size_t i = 0; i < count; i++) {
        const USFM_MarkerRow *row = &USFM_MarkerRows[i];
        CHECK_EQ(row->name_length, strlen(row->name));
        CHECK_EQ(usfm__marker_find(row->name, row->name_length), i);
    }
}

static void test_unknown_names_miss(void)
{
    CHECK_EQ(usfm__marker_find("zzz", 3), USFM_ROW_NONE);
    CHECK_EQ(usfm__marker_find("q2", 2), USFM_ROW_NONE);      /* levels are not keys */
    CHECK_EQ(usfm__marker_find("qt-s", 4), USFM_ROW_NONE);    /* suffixes are not keys */
    CHECK_EQ(usfm__marker_find("P", 1), USFM_ROW_NONE);       /* case-sensitive */
    CHECK_EQ(usfm__marker_find("", 0), USFM_ROW_NONE);
    CHECK_EQ(usfm__marker_find("xtSeeAlsoX", 10), USFM_ROW_NONE);
}

static void test_name_max_is_longest_key(void)
{
    size_t count = sizeof USFM_MarkerRows / sizeof USFM_MarkerRows[0];
    size_t longest = 0;
    for (size_t i = 0; i < count; i++) {
        if (USFM_MarkerRows[i].name_length > longest) longest = USFM_MarkerRows[i].name_length;
    }
    CHECK_EQ(USFM_MARKER_NAME_MAX, longest);
}

static void test_slots_are_collision_free(void)
{
    size_t count = sizeof USFM_MarkerRows / sizeof USFM_MarkerRows[0];
    size_t used = 0;
    CHECK(USFM_MARKER_SLOT_COUNT >= count);
    for (size_t s = 0; s < USFM_MARKER_SLOT_COUNT; s++) {
        if (USFM_MarkerSlots[s] != 0) used++;
    }
    CHECK_EQ(used, count);
}

static void test_k1_is_a_paragraph(void)
{
    uint32_t k = usfm__marker_find("k", 1);
    uint32_t k1 = usfm__marker_find("k1", 2);
    CHECK(k != USFM_ROW_NONE && k1 != USFM_ROW_NONE);
    CHECK_EQ(USFM_MarkerRows[k].role, USFM_ROLE_CHAR);
    CHECK_EQ(USFM_MarkerRows[k1].role, USFM_ROLE_PARA);
    CHECK_EQ(USFM_MarkerRows[k1].marker, USFM_M_K);
    CHECK_EQ(USFM_MarkerRows[k1].level, 1);
}

static void test_byte_classes(void)
{
    CHECK(USFM_ByteClass['\\'] & USFM_BC_STOP);
    CHECK(USFM_ByteClass['\\'] & USFM_BC_ESCAPE);
    CHECK(!(USFM_ByteClass['a'] & USFM_BC_STOP));
    CHECK(USFM_ByteClass['-'] & USFM_BC_NAME);
    CHECK(!(USFM_ByteClass['-'] & USFM_BC_ALPHA));
    CHECK(USFM_ByteClass['7'] & USFM_BC_DIGIT);
    CHECK(USFM_ByteClass['\t'] & USFM_BC_SPACE);
    CHECK(USFM_ByteClass['\r'] & USFM_BC_NEWLINE);
    CHECK(!(USFM_ByteClass[' '] & USFM_BC_STOP));
}

static void run_markers_tests(void)
{
    RUN(test_every_row_finds_itself);
    RUN(test_unknown_names_miss);
    RUN(test_name_max_is_longest_key);
    RUN(test_slots_are_collision_free);
    RUN(test_k1_is_a_paragraph);
    RUN(test_byte_classes);
}

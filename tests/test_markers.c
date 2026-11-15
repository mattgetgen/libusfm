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

static void test_role_spot_checks(void)
{
    /* Guards typos in the hand-written table: one row per role. */
    static const struct { const char *name; uint8_t role; uint8_t level; uint8_t props; } cases[] = {
        { "id", USFM_ROLE_ID, 0, USFM_MP_ARG_WORD },
        { "usfm", USFM_ROLE_HEADER, 0, USFM_MP_ARG_WORD },
        { "c", USFM_ROLE_CHAPTER, 0, USFM_MP_ARG_WORD },
        { "v", USFM_ROLE_VERSE, 0, USFM_MP_ARG_WORD },
        { "p", USFM_ROLE_PARA, 0, 0 },
        { "s", USFM_ROLE_PARA, 0, USFM_MP_HEADING },
        { "nd", USFM_ROLE_CHAR, 0, 0 },
        { "f", USFM_ROLE_NOTE, 0, USFM_MP_ARG_WORD },
        { "ft", USFM_ROLE_NOTE_PART, 0, 0 },
        { "xt", USFM_ROLE_NOTE_PART, 0, 0 },
        { "tr", USFM_ROLE_ROW, 0, 0 },
        { "tcr", USFM_ROLE_CELL, 0, 0 },
        { "ts", USFM_ROLE_MILESTONE, 0, 0 },
        { "esb", USFM_ROLE_SIDEBAR, 0, 0 },
        { "esbe", USFM_ROLE_SIDEBAR_END, 0, 0 },
        { "periph", USFM_ROLE_PERIPH, 0, USFM_MP_ARG_LINE },
        { "fig", USFM_ROLE_FIGURE, 0, 0 },
        { "s3e", USFM_ROLE_PARA, 3, USFM_MP_HEADING },
        { "ms2e", USFM_ROLE_PARA, 2, USFM_MP_HEADING },
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        uint32_t row = usfm__marker_find(cases[i].name, (uint32_t)strlen(cases[i].name));
        CHECK(row != USFM_ROW_NONE);
        if (row == USFM_ROW_NONE) continue;
        CHECK_EQ(USFM_MarkerRows[row].role, cases[i].role);
        CHECK_EQ(USFM_MarkerRows[row].level, cases[i].level);
        CHECK_EQ(USFM_MarkerRows[row].props, cases[i].props);
    }
}

static void test_lookup_respects_length(void)
{
    /* Names are spans into the input, not NUL-terminated strings. */
    CHECK_EQ(usfm__marker_find("pxx", 1), usfm__marker_find("p", 1));
    CHECK_EQ(usfm__marker_find("ndx", 2), usfm__marker_find("nd", 2));
    CHECK_EQ(usfm__marker_find("\xc3\xa9", 2), USFM_ROW_NONE);
}

/* usfm__on_marker's default case is unreachable only if every row has
 * one of the roles it switches on. */
static void test_every_role_is_handled(void)
{
    size_t count = sizeof USFM_MarkerRows / sizeof USFM_MarkerRows[0];
    uint32_t bad = 0;
    for (size_t i = 0; i < count; i++) {
        uint8_t role = USFM_MarkerRows[i].role;
        if (role < USFM_ROLE_PARA || role > USFM_ROLE_FIGURE) bad++;
    }
    CHECK_EQ(bad, 0);
}

/* Reference: linear scan of the rows. */
static uint32_t linear_find(const char *name, uint32_t length)
{
    size_t count = sizeof USFM_MarkerRows / sizeof USFM_MarkerRows[0];
    for (size_t i = 0; i < count; i++) {
        if (USFM_MarkerRows[i].name_length == length &&
            memcmp(USFM_MarkerRows[i].name, name, length) == 0) return (uint32_t)i;
    }
    return USFM_ROW_NONE;
}

static void test_lookup_matches_linear_scan(void)
{
    /* Every name of 1-3 lowercase letters (18,278 names), hits and misses. */
    char name[3];
    uint32_t mismatches = 0;
    for (uint32_t length = 1; length <= 3; length++) {
        uint32_t total = 1;
        for (uint32_t k = 0; k < length; k++) total *= 26;
        for (uint32_t n = 0; n < total; n++) {
            uint32_t v = n;
            for (uint32_t k = 0; k < length; k++) { name[k] = (char)('a' + v % 26); v /= 26; }
            if (usfm__marker_find(name, length) != linear_find(name, length)) mismatches++;
        }
    }
    CHECK_EQ(mismatches, 0);
}

static void run_markers_tests(void)
{
    RUN(test_every_row_finds_itself);
    RUN(test_unknown_names_miss);
    RUN(test_name_max_is_longest_key);
    RUN(test_slots_are_collision_free);
    RUN(test_k1_is_a_paragraph);
    RUN(test_byte_classes);
    RUN(test_role_spot_checks);
    RUN(test_lookup_respects_length);
    RUN(test_lookup_matches_linear_scan);
    RUN(test_every_role_is_handled);
}

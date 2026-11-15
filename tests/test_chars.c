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

static void test_char_plain_marker_closes_only_spans_without_closer(void)
{
    /* \w closes \nd (no \nd* ahead) but nests in \add (\add* ahead). */
    CHECK_TREE("\\p \\add a \\nd b \\w c\\w* d\\add*",
        "BOOK\n"
        "  PARA p\n"
        "    CHAR add\n"
        "      TEXT \"a \"\n"
        "      CHAR nd\n"
        "        TEXT \"b \"\n"
        "      CHAR w\n"
        "        TEXT \"c\"\n"
        "      TEXT \" d\"\n");
    CHECK_DIAGS("\\p \\add a \\nd b \\w c\\w* d\\add*", USFM_VERSION_AUTO,
                "UNCLOSED_MARKER 10\n");
}

static void test_char_nested_close_of_plain_span(void)
{
    CHECK_TREE("\\p \\nd a\\+nd* b",
        "BOOK\n"
        "  PARA p\n"
        "    CHAR nd\n"
        "      TEXT \"a\"\n"
        "    TEXT \" b\"\n");
}

static void test_char_stray_bare_close(void)
{
    CHECK_DIAGS("\\p a\\* b", USFM_VERSION_AUTO, "UNEXPECTED_CLOSE 4\n");
}

static void test_char_marker_ids_fit_closer_table(void)
{
    size_t count = sizeof USFM_MarkerRows / sizeof USFM_MarkerRows[0], i;
    for (i = 0; i < count; i++) {
        CHECK((uint32_t)USFM_MarkerRows[i].marker < USFM__MARKER_ID_COUNT);
    }
}

/* A parser positioned at `position` in `input` with the given frame
 * kinds open, for asking usfm__closer_ahead directly. */
static void lookahead_setup(USFM_Parser *p, USFM_Ast *ast, const char *input,
                            const uint8_t *kinds, uint32_t depth)
{
    uint32_t i;
    memset(ast, 0, sizeof *ast);
    memset(p, 0, sizeof *p);
    ast->input = input;
    ast->input_length = (uint32_t)strlen(input);
    p->ast = ast;
    usfm__scan_init(&p->scan, input, ast->input_length);
    for (i = 0; i < depth; i++) p->stack[i].kind = kinds[i];
    p->depth = depth;
}

static void test_char_lookahead_unknown_names(void)
{
    static const uint8_t para[] = { USFM_AST_BOOK, USFM_AST_PARA };
    const char *in = "\\p \\zq a\\zq* \\zr b\\zr*\n\\zp c\\zs*";
    USFM_Ast ast;
    USFM_Parser p;
    lookahead_setup(&p, &ast, in, para, 2);
    p.scan.position = 7;                                 /* after "\zq " */
    CHECK(usfm__closer_ahead(&p, USFM_M_UNKNOWN, 4, 2)); /* zq */
    CHECK(usfm__closer_ahead(&p, USFM_M_UNKNOWN, 14, 2)); /* zr */
    CHECK(!usfm__closer_ahead(&p, USFM_M_UNKNOWN, 24, 2)); /* zp: no closer */
    CHECK(!usfm__closer_ahead(&p, USFM_M_UNKNOWN, 0, 0));
    /* "\zp" at line start becomes a paragraph: \zs* is past the stop. */
    CHECK(!usfm__closer_ahead(&p, USFM_M_UNKNOWN, 29, 2)); /* zs */
    CHECK_EQ(p.closers[0].stop, 23);
    p.scan.position = 13;                                /* past \zq* */
    CHECK(!usfm__closer_ahead(&p, USFM_M_UNKNOWN, 4, 2));
    CHECK(usfm__closer_ahead(&p, USFM_M_UNKNOWN, 14, 2));
    CHECK_EQ(p.closers[0].generation, 1);                /* one scan */
}

static void test_char_lookahead_name_overflow(void)
{
    static const uint8_t para[] = { USFM_AST_BOOK, USFM_AST_PARA };
    static char in[1024];
    char name[8];
    uint32_t i, n = 0;
    USFM_Ast ast;
    USFM_Parser p;
    n += (uint32_t)sprintf(in + n, "\\p ");
    for (i = 0; i < USFM__NAME_SLOTS + 1u; i++) n += (uint32_t)sprintf(in + n, "\\z%u*", i);
    lookahead_setup(&p, &ast, in, para, 2);
    p.scan.position = 3;                                 /* after "\p " */
    sprintf(name, "z%u", 0u);
    CHECK(usfm__closer_ahead(&p, USFM_M_UNKNOWN, (uint32_t)(strstr(in, name) - in), 2));
    CHECK(p.closers[0].name_overflow);
    /* The 65th name was not recorded: "no", deterministically. */
    sprintf(name, "z%u*", USFM__NAME_SLOTS);
    CHECK(!usfm__closer_ahead(&p, USFM_M_UNKNOWN, (uint32_t)(strstr(in, name) - in),
                              (uint32_t)strlen(name) - 1u));
}

static void test_char_lookahead_in_note_stops_at_note_part(void)
{
    /* Inside a note, \add's closer is past the \fq note part, so it is
     * not ahead; outside a note \fq is not a stop. Both caches are kept. */
    static const uint8_t in_note[] = { USFM_AST_BOOK, USFM_AST_PARA, USFM_AST_NOTE,
                                       USFM_AST_CHAR, USFM_AST_CHAR };
    const char *in = "\\p \\f + \\ft \\add a \\nd b \\fq c\\add*\\f*";
    USFM_Ast ast;
    USFM_Parser p;
    lookahead_setup(&p, &ast, in, in_note, 5);
    p.scan.position = 23;                                /* after "\nd " */
    CHECK(!usfm__closer_ahead(&p, USFM_M_ADD, 0, 0));
    CHECK_EQ(p.closers[1].stop, 25);                     /* at \fq */
    p.depth = 2;                                         /* no note open */
    CHECK(usfm__closer_ahead(&p, USFM_M_ADD, 0, 0));
    CHECK(!usfm__closer_ahead(&p, USFM_M_ND, 0, 0));
    CHECK_EQ(p.closers[0].generation, 1);
    CHECK_EQ(p.closers[1].generation, 1);
}

/* "\p " plus `count` copies of `unit`, heap-allocated. */
static char *repeat_input(const char *unit, uint32_t count, uint32_t *length)
{
    uint32_t unit_length = (uint32_t)strlen(unit), i;
    char *buf = (char *)malloc(3u + (size_t)unit_length * count + 1u);
    if (buf == NULL) return NULL;
    memcpy(buf, "\\p ", 3);
    for (i = 0; i < count; i++) memcpy(buf + 3 + (size_t)i * unit_length, unit, unit_length);
    *length = 3u + unit_length * count;
    buf[*length] = '\0';
    return buf;
}

static double lookahead_seconds_nd;
static double lookahead_seconds_unknown;

static void test_char_lookahead_linear_nd(void)
{
    /* Every \nd asks whether the open \nd's closer is ahead. The old
     * cache only remembered "yes", so this was quadratic. */
    uint32_t length;
    char *in = repeat_input("\\nd a ", 200000u, &length);
    clock_t start;
    USFM_Ast *ast;
    USFM_Err err;
    CHECK(in != NULL);
    if (in == NULL) return;
    start = clock();
    ast = USFM_Ast_Parse(in, length, NULL, &err);
    lookahead_seconds_nd = (double)(clock() - start) / CLOCKS_PER_SEC;
    CHECK(ast != NULL);
    CHECK(lookahead_seconds_nd < 2.0);
    fprintf(stderr, "    %.3f s\n", lookahead_seconds_nd);
    USFM_Ast_Free(ast);
    free(in);
}

static void test_char_lookahead_linear_unknown(void)
{
    /* Parse, then ask the unknown-name question at every \zz, the way
     * the parser does for an unknown marker. */
    static const uint8_t para[] = { USFM_AST_BOOK, USFM_AST_PARA };
    uint32_t length, asked = 0;
    char *in = repeat_input("\\zz a ", 200000u, &length);
    clock_t start;
    USFM_Ast *ast, fake;
    USFM_Parser p;
    USFM_ScanToken t;
    USFM_Err err;
    int any = 0;
    CHECK(in != NULL);
    if (in == NULL) return;
    start = clock();
    ast = USFM_Ast_Parse(in, length, NULL, &err);
    CHECK(ast != NULL);
    USFM_Ast_Free(ast);
    lookahead_setup(&p, &fake, in, para, 2);
    while (usfm__scan_next(&p.scan, &t)) {
        if (t.tok.kind != USFM_TOK_MARKER || t.row != USFM_ROW_NONE) continue;
        any |= usfm__closer_ahead(&p, USFM_M_UNKNOWN, t.name_offset, t.name_length);
        asked++;
    }
    lookahead_seconds_unknown = (double)(clock() - start) / CLOCKS_PER_SEC;
    CHECK(!any);
    CHECK_EQ(asked, 200000u);
    CHECK(lookahead_seconds_unknown < 2.0);
    fprintf(stderr, "    %.3f s\n", lookahead_seconds_unknown);
    free(in);
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
    RUN(test_char_plain_marker_closes_only_spans_without_closer);
    RUN(test_char_nested_close_of_plain_span);
    RUN(test_char_stray_bare_close);
    RUN(test_char_marker_ids_fit_closer_table);
    RUN(test_char_lookahead_unknown_names);
    RUN(test_char_lookahead_name_overflow);
    RUN(test_char_lookahead_in_note_stops_at_note_part);
    RUN(test_char_lookahead_linear_nd);
    RUN(test_char_lookahead_linear_unknown);
}

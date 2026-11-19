/* UTF-8 validation (docs/superpowers/specs/2026-11-18-utf8-validation-design.md). */

static USFM_Ast *utf8_parse(const char *s, uint32_t n, uint32_t flags, USFM_Err *err)
{
    return USFM_Ast_ParseOpts(s, n, NULL, flags, NULL, err);
}

/* usfm__utf8_check against the independent reference in check.h. */
static void check_validator_matches(const unsigned char *s, uint32_t n)
{
    uint32_t bad_a, bad_b;
    uint32_t a = usfm__utf8_check((const char *)s, n, &bad_a);
    uint32_t b = check_utf8_first_bad((const char *)s, n, &bad_b);
    usfm_test_checks++;
    if (a != b || bad_a != bad_b || usfm__utf8_valid((const char *)s, n) != (b == n)) {
        uint32_t i;
        usfm_test_failures++;
        fprintf(stderr, "utf8 mismatch: check %u/%u, valid %d, reference %u/%u for", a, bad_a,
                usfm__utf8_valid((const char *)s, n), b, bad_b);
        for (i = 0; i < n; i++) fprintf(stderr, " %02X", s[i]);
        fprintf(stderr, "\n");
    }
}

static void test_utf8_validator_edges(void)
{
    static const unsigned char edge[] = {
        0x00, 0x7F, 0x80, 0x8F, 0x90, 0x9F, 0xA0, 0xBF, 0xC0, 0xC1, 0xC2, 0xDF,
        0xE0, 0xE1, 0xEC, 0xED, 0xEE, 0xEF, 0xF0, 0xF1, 0xF3, 0xF4, 0xF5, 0xFF
    };
    const uint32_t e = (uint32_t)sizeof edge;
    unsigned char s[4];
    uint32_t a, b, c, d;
    int before = usfm_test_failures;
    /* Every string of 1 to 4 edge bytes. */
    for (a = 0; a < e; a++) {
        s[0] = edge[a];
        check_validator_matches(s, 1);
        for (b = 0; b < e; b++) {
            s[1] = edge[b];
            check_validator_matches(s, 2);
            for (c = 0; c < e; c++) {
                s[2] = edge[c];
                check_validator_matches(s, 3);
                for (d = 0; d < e; d++) {
                    s[3] = edge[d];
                    check_validator_matches(s, 4);
                    if (usfm_test_failures - before > 20) return;
                }
            }
        }
    }
}

static void test_utf8_validator_random(void)
{
    uint32_t seed = 12345u, i, j;
    unsigned char s[64];
    int before = usfm_test_failures;
    for (i = 0; i < 100000u; i++) {
        uint32_t n;
        seed = seed * 1664525u + 1013904223u;
        n = (seed >> 16) % 65u;
        for (j = 0; j < n; j++) {
            seed = seed * 1664525u + 1013904223u;
            /* Mostly ASCII, so the 8-byte fast path is exercised too. */
            s[j] = (unsigned char)((seed >> 24) < 160u ? (seed >> 8) & 0x7Fu : seed >> 16);
        }
        check_validator_matches(s, n);
        if (usfm_test_failures - before > 20) return;
    }
}

/* A random valid UTF-8 string of 1 to 40 code points (at most 160 bytes). */
static uint32_t utf8_gen_valid(uint32_t *seedp, unsigned char *s)
{
    uint32_t seed = *seedp, count, n = 0, r, j;
    seed = seed * 1664525u + 1013904223u;
    count = 1u + (seed >> 16) % 40u;
    for (j = 0; j < count; j++) {
        uint32_t cp;
        seed = seed * 1664525u + 1013904223u;
        r = seed >> 8;
        switch ((seed >> 4) & 3u) {
        case 0: cp = r & 0x7Fu; break;
        case 1: cp = 0x80u + r % 0x780u; break;
        case 2: cp = 0x800u + r % 0xF800u; if (cp >= 0xD800u && cp <= 0xDFFFu) cp += 0x800u; break;
        default: cp = 0x10000u + r % 0x100000u; break;
        }
        if (cp < 0x80u) s[n++] = (unsigned char)cp;
        else if (cp < 0x800u) {
            s[n++] = (unsigned char)(0xC0u | (cp >> 6));
            s[n++] = (unsigned char)(0x80u | (cp & 0x3Fu));
        } else if (cp < 0x10000u) {
            s[n++] = (unsigned char)(0xE0u | (cp >> 12));
            s[n++] = (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu));
            s[n++] = (unsigned char)(0x80u | (cp & 0x3Fu));
        } else {
            s[n++] = (unsigned char)(0xF0u | (cp >> 18));
            s[n++] = (unsigned char)(0x80u | ((cp >> 12) & 0x3Fu));
            s[n++] = (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu));
            s[n++] = (unsigned char)(0x80u | (cp & 0x3Fu));
        }
    }
    *seedp = seed;
    return n;
}

/* Valid multi-byte text longer than 8 bytes, with sequences straddling
 * 8-byte word boundaries, then one mutation of each string. */
static void test_utf8_validator_encoded(void)
{
    uint32_t seed = 777u, i;
    unsigned char s[176];
    int before = usfm_test_failures;
    for (i = 0; i < 20000u; i++) {
        uint32_t n = utf8_gen_valid(&seed, s);
        {
            uint32_t bad;
            CHECK_EQ(usfm__utf8_check((const char *)s, n, &bad), n);
        }
        check_validator_matches(s, n);
        seed = seed * 1664525u + 1013904223u;
        if ((seed >> 20) & 1u) {
            seed = seed * 1664525u + 1013904223u;
            s[(seed >> 8) % n] = (unsigned char)(seed >> 24);
        } else {
            seed = seed * 1664525u + 1013904223u;
            n = (seed >> 8) % (n + 1u);
        }
        check_validator_matches(s, n);
        if (usfm_test_failures - before > 20) return;
    }
}

#if !USFM__UTF8_SIMD
/* Without SIMD the whole-input pass is the 32-byte-step scalar validator:
 * the same tests hold for it. */
#  define usfm__utf8_valid_simd usfm__utf8_valid_input
#endif
/* usfm__utf8_valid_simd against the scalar DFA; reports bytes, capped. */
static void check_simd_matches(const unsigned char *s, uint32_t n)
{
    int a = usfm__utf8_valid_simd((const char *)s, n) != 0;
    int b = usfm__utf8_valid((const char *)s, n) != 0;
    usfm_test_checks++;
    if (a != b) {
        uint32_t i;
        usfm_test_failures++;
        if (usfm_test_failures < 20) {
            fprintf(stderr, "utf8 simd mismatch: simd %d, scalar %d for", a, b);
            for (i = 0; i < n && i < 80; i++) fprintf(stderr, " %02X", s[i]);
            fprintf(stderr, "\n");
        }
    }
}

static void test_utf8_simd_validator(void)
{
    static const unsigned char edge[] = {
        0x00, 0x41, 0x7F, 0x80, 0x8F, 0x90, 0x9F, 0xA0, 0xBF, 0xC0, 0xC1, 0xC2, 0xDF,
        0xE0, 0xE1, 0xEC, 0xED, 0xEE, 0xEF, 0xF0, 0xF1, 0xF3, 0xF4, 0xF5, 0xFF
    };
    const uint32_t e = (uint32_t)sizeof edge;
    static const uint32_t offs[] = {
        0, 1, 2, 3, 12, 13, 14, 15, 16, 17, 28, 29, 30, 31, 32, 33,
        44, 45, 46, 47, 48, 49, 60, 61, 62, 63, 64, 65
    };
    unsigned char buf[256], seq[4];

    uint32_t len, a, b, c, d, k, off, tail, seed, i, j;
    int before = usfm_test_failures;
    /* The 42-byte case where a pending lead byte must still fail when a whole
     * ASCII chunk follows: 15 x, C3, 24 x, C3 A9. */
    memset(buf, 'x', 15);
    buf[15] = 0xC3;
    memset(buf + 16, 'x', 24);
    buf[40] = 0xC3;
    buf[41] = 0xA9;
    CHECK_EQ(usfm__utf8_valid_simd((const char *)buf, 42), 0);
    CHECK_EQ(usfm__utf8_valid((const char *)buf, 42), 0);
    check_simd_matches(buf, 42);
    /* 32-wide: lead at chunk position 31, a whole ASCII chunk, then more text. */
    memset(buf, 'x', 31);
    buf[31] = 0xC3;
    memset(buf + 32, 'x', 40);
    buf[72] = 0xC3;
    buf[73] = 0xA9;
    CHECK_EQ(usfm__utf8_valid_simd((const char *)buf, 74), 0);
    CHECK_EQ(usfm__utf8_valid((const char *)buf, 74), 0);
    check_simd_matches(buf, 74);
    /* 64-wide: lead at chunk position 63, a whole ASCII chunk, then more text. */
    memset(buf, 'x', 63);
    buf[63] = 0xC3;
    memset(buf + 64, 'x', 72);
    buf[136] = 0xC3;
    buf[137] = 0xA9;
    CHECK_EQ(usfm__utf8_valid_simd((const char *)buf, 138), 0);
    CHECK_EQ(usfm__utf8_valid((const char *)buf, 138), 0);
    check_simd_matches(buf, 138);
    /* Edge sequences at chunk-boundary offsets. */
    for (len = 1; len <= 4; len++) {
        uint32_t total = 1;
        for (i = 0; i < len; i++) total *= e;
        for (i = 0; i < total; i++) {
            uint32_t v = i;
            a = v % e; v /= e; b = v % e; v /= e; c = v % e; v /= e; d = v % e;
            seq[0] = edge[a]; seq[1] = edge[b]; seq[2] = edge[c]; seq[3] = edge[d];
            for (k = 0; k < (uint32_t)(sizeof offs / sizeof offs[0]); k++) {
                off = offs[k];
                if (len == 4 && (off & 15u) != 0u && (off & 15u) < 13u) continue;   /* keeps the run short */
                for (tail = 0; tail < 3; tail++) {
                    if (len == 4 && tail == 1) continue;   /* keeps the run short */
                    /* End of buffer; 5 'x' after; 128 'x' (whole ASCII chunks at 32/64 wide) then U+00E9. */
                    uint32_t n = off + len + (tail == 0 ? 0u : tail == 1 ? 5u : 130u);
                    memset(buf, 'x', sizeof buf);
                    memcpy(buf + off, seq, len);
                    if (tail == 2) { buf[n - 2] = 0xC3; buf[n - 1] = 0xA9; }
                    check_simd_matches(buf, n);
                }
            }
            if (usfm_test_failures - before > 20) return;
        }
    }
    /* Random strings of 0-200 bytes, 60% ASCII. */
    seed = 424242u;
    for (i = 0; i < 250000u; i++) {
        unsigned char r[200];
        uint32_t n;
        seed = seed * 1664525u + 1013904223u;
        n = (seed >> 16) % 201u;
        for (j = 0; j < n; j++) {
            seed = seed * 1664525u + 1013904223u;
            r[j] = (unsigned char)((seed >> 24) < 154u ? (seed >> 8) & 0x7Fu : seed >> 16);
        }
        check_simd_matches(r, n);
        if (usfm_test_failures - before > 20) return;
    }
    /* Valid encoded strings and one mutation of each. */
    seed = 991u;
    for (i = 0; i < 20000u; i++) {
        unsigned char s[176];
        uint32_t n = utf8_gen_valid(&seed, s);
        check_simd_matches(s, n);
        seed = seed * 1664525u + 1013904223u;
        if ((seed >> 20) & 1u) {
            seed = seed * 1664525u + 1013904223u;
            s[(seed >> 8) % n] = (unsigned char)(seed >> 24);
        } else {
            seed = seed * 1664525u + 1013904223u;
            n = (seed >> 8) % (n + 1u);
        }
        check_simd_matches(s, n);
        if (usfm_test_failures - before > 20) return;
    }
}

static void test_utf8_validator_table(void)
{
    uint32_t bad;
    /* The well-formed table (RFC 3629), at the edges of each row. */
    CHECK_EQ(usfm__utf8_check("\x7F", 1, &bad), 1);
    CHECK_EQ(usfm__utf8_check("\xC2\x80", 2, &bad), 2);
    CHECK_EQ(usfm__utf8_check("\xDF\xBF", 2, &bad), 2);
    CHECK_EQ(usfm__utf8_check("\xE0\xA0\x80", 3, &bad), 3);
    CHECK_EQ(usfm__utf8_check("\xED\x9F\xBF", 3, &bad), 3);
    CHECK_EQ(usfm__utf8_check("\xEE\x80\x80", 3, &bad), 3);
    CHECK_EQ(usfm__utf8_check("\xF0\x90\x80\x80", 4, &bad), 4);
    CHECK_EQ(usfm__utf8_check("\xF4\x8F\xBF\xBF", 4, &bad), 4);
    /* Invalid: lone continuation, C0/C1, F5-FF. */
    CHECK_EQ(usfm__utf8_check("ab\x80", 3, &bad), 2); CHECK_EQ(bad, 1);
    CHECK_EQ(usfm__utf8_check("\xC0\x80", 2, &bad), 0); CHECK_EQ(bad, 1);
    CHECK_EQ(usfm__utf8_check("\xC1\xBF", 2, &bad), 0); CHECK_EQ(bad, 1);
    CHECK_EQ(usfm__utf8_check("\xF5\x80\x80\x80", 4, &bad), 0); CHECK_EQ(bad, 1);
    /* Overlong, surrogate, above U+10FFFF: the lead byte alone. */
    CHECK_EQ(usfm__utf8_check("\xE0\x9F\xBF", 3, &bad), 0); CHECK_EQ(bad, 1);
    CHECK_EQ(usfm__utf8_check("\xED\xA0\x80", 3, &bad), 0); CHECK_EQ(bad, 1);
    CHECK_EQ(usfm__utf8_check("\xF0\x8F\xBF\xBF", 4, &bad), 0); CHECK_EQ(bad, 1);
    CHECK_EQ(usfm__utf8_check("\xF4\x90\x80\x80", 4, &bad), 0); CHECK_EQ(bad, 1);
    /* Cut short: by the end, and by an ASCII byte. */
    CHECK_EQ(usfm__utf8_check("abcdefgh\xE2\x82", 10, &bad), 8); CHECK_EQ(bad, 2);
    CHECK_EQ(usfm__utf8_check("\xF0\x9F\x98 ", 4, &bad), 0); CHECK_EQ(bad, 3);
}

static void test_utf8_diagnostics(void)
{
    /* Windows-1252 e-acute in a verse: one diagnostic, the TEXT flagged. */
    static const char legacy[] = "\\c 1\n\\p\n\\v 1 caf\xE9 au lait\n";
    USFM_Err err;
    USFM_Ast *ast = utf8_parse(legacy, (uint32_t)strlen(legacy), 0, &err);
    uint32_t count;
    const USFM_Diagnostic *d;
    CHECK(ast != NULL);
    if (ast == NULL) return;
    d = USFM_Ast_GetDiagnostics(ast, &count);
    CHECK_EQ(count, 1);
    if (count == 1) {
        CHECK_EQ(d[0].code, USFM_DIAGNOSTIC_INVALID_UTF8);
        CHECK_EQ(d[0].span.offset, 16);
        CHECK_EQ(d[0].span.length, 1);
        CHECK_EQ(d[0].marker, USFM_M_P);
    }
    CHECK_EQ(USFM_DiagnosticCode_GetSeverity(USFM_DIAGNOSTIC_INVALID_UTF8), USFM_SEVERITY_ERROR);
    CHECK_STR(USFM_DiagnosticCode_GetMessage(USFM_DIAGNOSTIC_INVALID_UTF8), "invalid UTF-8");
    CHECK_EQ(check_utf8_flags(ast), 0);
    CHECK_EQ(check_utf8_coverage(ast), 0);
    USFM_Ast_Free(ast);

    CHECK_TREE("\\p caf\xE9\n",
        "BOOK\n"
        "  PARA p\n"
        "    TEXT \"caf\xE9\" badutf8\n");
    /* Two bad sequences in one run: one diagnostic, at the first. */
    CHECK_DIAGS("\\p \xE9t\xE9\n", USFM_VERSION_AUTO, "INVALID_UTF8 3\n");
    /* Two runs: two diagnostics. */
    CHECK_DIAGS("\\p a\xE9 \\w b\xE9\\w*\n", USFM_VERSION_AUTO, "INVALID_UTF8 4\nINVALID_UTF8 10\n");
    /* Cut short by a marker: the run before it is invalid. */
    CHECK_DIAGS("\\p a\xC3\\w b\\w*\n", USFM_VERSION_AUTO, "INVALID_UTF8 4\n");
    /* Cut short by the end of the input. */
    CHECK_DIAGS("\\p a\xE2\x82", USFM_VERSION_AUTO, "INVALID_UTF8 4\n");
    /* In an argument and in attributes. */
    CHECK_TREE("\\c 1\xE9\n",
        "BOOK\n"
        "  CHAPTER c\n"
        "    ARGUMENT \"1\xE9\" badutf8\n");
    CHECK_TREE("\\p \\w word|lemma=\"caf\xE9\"\\w*\n",
        "BOOK\n"
        "  PARA p\n"
        "    CHAR w\n"
        "      TEXT \"word\"\n"
        "      ATTRIBUTES \"|lemma=\\\"caf\xE9\\\"\" badutf8\n");
    /* Valid multi-byte text: Hebrew, Chinese, an emoji. */
    CHECK_DIAGS("\\p \xD7\x91\xD6\xB0\xD7\xA8 \xE5\x9C\xA8 \xF0\x9F\x98\x80\n", USFM_VERSION_AUTO, "");
}

static void test_utf8_opt_out(void)
{
    static const char legacy[] = "\\p caf\xE9\n";
    USFM_Err err;
    uint32_t count, n, i, flagged = 0;
    USFM_Ast *ast = utf8_parse(legacy, (uint32_t)strlen(legacy), USFM_PARSE_NO_UTF8_CHECK, &err);
    const USFM_Node *nodes;
    CHECK(ast != NULL);
    if (ast == NULL) return;
    USFM_Ast_GetDiagnostics(ast, &count);
    CHECK_EQ(count, 0);
    nodes = USFM_Ast_GetNodes(ast, &n);
    for (i = 0; i < n; i++) flagged += (nodes[i].kind == USFM_AST_TEXT && (nodes[i].level_flags & USFM_TF_INVALID_UTF8));
    CHECK_EQ(flagged, 0);
    USFM_Ast_Free(ast);
}

/* The same book in UTF-8 and in Windows-1252 has the same structure. */
static void test_utf8_legacy_book(void)
{
    static const char utf8[] =
        "\\id GEN\n\\h Gen\xC3\xA8se\n\\c 1\n\\s1 La cr\xC3\xA9" "ation\n\\p\n"
        "\\v 1 Au commencement, Dieu cr\xC3\xA9" "a les cieux et la terre.\n"
        "\\v 2 La terre \xC3\xA9tait informe et vide\\f + \\ft H\xC3\xA9" "b. \\fq tohu\\fq*\\f*.\n";
    static const char cp1252[] =
        "\\id GEN\n\\h Gen\xE8se\n\\c 1\n\\s1 La cr\xE9" "ation\n\\p\n"
        "\\v 1 Au commencement, Dieu cr\xE9" "a les cieux et la terre.\n"
        "\\v 2 La terre \xE9tait informe et vide\\f + \\ft H\xE9" "b. \\fq tohu\\fq*\\f*.\n";
    USFM_Err err;
    USFM_Ast *a = utf8_parse(utf8, (uint32_t)strlen(utf8), 0, &err);
    USFM_Ast *b = utf8_parse(cp1252, (uint32_t)strlen(cp1252), 0, &err);
    uint32_t na, nb, i, da, db, same = 1;
    const USFM_Node *x, *y;
    CHECK(a != NULL && b != NULL);
    if (a == NULL || b == NULL) { USFM_Ast_Free(a); USFM_Ast_Free(b); return; }
    x = USFM_Ast_GetNodes(a, &na);
    y = USFM_Ast_GetNodes(b, &nb);
    CHECK_EQ(na, nb);
    for (i = 0; i < na && i < nb; i++) {
        if (x[i].kind != y[i].kind || x[i].marker != y[i].marker || x[i].parent != y[i].parent ||
            x[i].end != y[i].end) same = 0;
    }
    CHECK(same);
    USFM_Ast_GetDiagnostics(a, &da);
    USFM_Ast_GetDiagnostics(b, &db);
    CHECK_EQ(da, 0);
    CHECK_EQ(db, 5);            /* \h, \s1, verse 1, verse 2, \ft */
    CHECK_EQ(check_utf8_flags(b), 0);
    CHECK_EQ(check_utf8_coverage(b), 0);
    USFM_Ast_Free(a);
    USFM_Ast_Free(b);
}

/* Parses s, checks the flag and coverage helpers, and returns the number
 * of INVALID_UTF8 diagnostics (-1 if the parse failed). */
static int utf8_parse_counts(const char *s, uint32_t n, uint32_t flags)
{
    USFM_Err err;
    USFM_Ast *ast = utf8_parse(s, n, flags, &err);
    uint32_t count, i;
    int invalid = 0;
    const USFM_Diagnostic *d;
    CHECK(ast != NULL);
    if (ast == NULL) return -1;
    d = USFM_Ast_GetDiagnostics(ast, &count);
    for (i = 0; i < count; i++) invalid += d[i].code == USFM_DIAGNOSTIC_INVALID_UTF8;
    if (flags == 0) {
        CHECK_EQ(check_utf8_flags(ast), 0);
        CHECK_EQ(check_utf8_coverage(ast), 0);
    }
    USFM_Ast_Free(ast);
    return invalid;
}

static void test_utf8_parser_paths(void)
{
    static const char bom[] = "\xEF\xBB\xBF\\p \xE9\n";
    static const char pushed[] = "\\c\n\n\xE9 x";
    static const char both[] = "\\c 1\xE9\n\\p \\w a|lemma=\"\xE9\"\\w*\n";
    USFM_Err err;
    USFM_Ast *ast;
    uint32_t n, i, flagged = 0;
    const USFM_Node *nodes;

    /* A UTF-8 byte-order mark is not part of any run. */
    CHECK_EQ(utf8_parse_counts(bom, (uint32_t)strlen(bom), 0), 1);
    /* A token pushed back and replayed is validated once. */
    CHECK_EQ(utf8_parse_counts(pushed, (uint32_t)strlen(pushed), 0), 1);
    /* A pipe that is plain text in a paragraph: one flagged TEXT run. */
    CHECK_TREE("\\p a|b=\"\xE9\"\n",
        "BOOK\n"
        "  PARA p\n"
        "    TEXT \"a|b=\\\"\xE9\\\"\" badutf8\n");
    /* Trimming trailing whitespace keeps the flag. */
    CHECK_TREE("\\p a\xE9 \r\n\\p b\n",
        "BOOK\n"
        "  PARA p\n"
        "    TEXT \"a\xE9\" badutf8\n"
        "  PARA p\n"
        "    TEXT \"b\"\n");
    /* Milestone attributes. */
    CHECK_TREE("\\p \\qt-s |who=\"\xE9\"\\*x\\qt-e\\*\n",
        "BOOK\n"
        "  PARA p\n"
        "    MILESTONE qt-s closed\n"
        "      ATTRIBUTES \"|who=\\\"\xE9\\\"\" badutf8\n"
        "    TEXT \"x\"\n"
        "    MILESTONE qt-e closed\n");
    CHECK_DIAGS("\\p \\qt-s |who=\"\xE9\"\\*x\\qt-e\\*\n", USFM_VERSION_AUTO, "INVALID_UTF8 15\n");
    /* Opt-out covers ARGUMENT and ATTRIBUTES too. */
    ast = utf8_parse(both, (uint32_t)strlen(both), USFM_PARSE_NO_UTF8_CHECK, &err);
    CHECK(ast != NULL);
    if (ast == NULL) return;
    nodes = USFM_Ast_GetNodes(ast, &n);
    for (i = 0; i < n; i++) flagged += (nodes[i].level_flags & USFM_TF_INVALID_UTF8) != 0;
    CHECK_EQ(flagged, 0);
    {
        const USFM_Diagnostic *d = USFM_Ast_GetDiagnostics(ast, &n);
        uint32_t invalid = 0;
        for (i = 0; i < n; i++) invalid += d[i].code == USFM_DIAGNOSTIC_INVALID_UTF8;
        CHECK_EQ(invalid, 0);
    }
    USFM_Ast_Free(ast);
}

/* An ARGUMENT nobody claims becomes TEXT and keeps the flag. */
static void test_utf8_unclaimed_argument(void)
{
    /* BOOK + implied PARA + 62 CHAR = 64 open nodes, so \f is TOO_DEEP:
     * its marker becomes TEXT and its WORD argument is left unclaimed. */
    char buf[189];
    uint32_t i, count, n, invalid = 0, deep = 0;
    USFM_Err err;
    USFM_Ast *ast;
    const USFM_Diagnostic *d;
    const USFM_Node *nodes;
    for (i = 0; i < 62; i++) memcpy(buf + 3u * i, "\\+w", 3);
    memcpy(buf + 186, "\\f\xE9", 3);
    ast = utf8_parse(buf, 189, 0, &err);
    CHECK(ast != NULL);
    if (ast == NULL) return;
    nodes = USFM_Ast_GetNodes(ast, &n);
    CHECK(n >= 2);
    if (n >= 2) {
        CHECK_EQ(nodes[n - 1].kind, USFM_AST_TEXT);
        CHECK(nodes[n - 1].level_flags & USFM_TF_INVALID_UTF8);
        CHECK_EQ(nodes[n - 1].span.offset, 188);
        CHECK_EQ(nodes[n - 1].span.length, 1);
        CHECK_EQ(nodes[n - 2].kind, USFM_AST_TEXT);
        CHECK(!(nodes[n - 2].level_flags & USFM_TF_INVALID_UTF8));
        CHECK_EQ(nodes[n - 2].span.offset, 186);
        CHECK_EQ(nodes[n - 2].span.length, 2);
    }
    d = USFM_Ast_GetDiagnostics(ast, &count);
    for (i = 0; i < count; i++) {
        if (d[i].code == USFM_DIAGNOSTIC_INVALID_UTF8) {
            invalid++;
            CHECK_EQ(d[i].span.offset, 188);
            CHECK_EQ(d[i].span.length, 1);
        } else if (d[i].code == USFM_DIAGNOSTIC_TOO_DEEP) {
            deep++;
            CHECK_EQ(d[i].span.offset, 186);
        }
    }
    CHECK_EQ(invalid, 1);
    CHECK_EQ(deep, 1);
    CHECK_EQ(check_ast(ast), 0);
    CHECK_EQ(check_utf8_flags(ast), 0);
    CHECK_EQ(check_utf8_coverage(ast), 0);
    USFM_Ast_Free(ast);
}

/* Counts every allocator call, then forwards to the checking allocator. */
typedef struct CountAlc {
    TestAlc  inner;
    uint32_t calls;
} CountAlc;

static void *count_alloc(void *ctx, size_t size)
{
    CountAlc *c = (CountAlc *)ctx;
    c->calls++;
    return test_alloc(&c->inner, size);
}

static void *count_resize(void *ctx, void *ptr, size_t old_size, size_t new_size)
{
    CountAlc *c = (CountAlc *)ctx;
    c->calls++;
    return test_resize(&c->inner, ptr, old_size, new_size);
}

static void count_dealloc(void *ctx, void *ptr, size_t size)
{
    CountAlc *c = (CountAlc *)ctx;
    c->calls++;
    test_dealloc(&c->inner, ptr, size);
}

static void test_utf8_byte_order_marks(void)
{
    static const struct { const char *bytes; uint32_t length; } wide[] = {
        { "\xFF\xFE\\\0i\0d\0", 8 },            /* UTF-16 LE */
        { "\xFE\xFF\0\\\0i\0d", 8 },            /* UTF-16 BE */
        { "\xFF\xFE\0\0\\\0\0\0", 8 },          /* UTF-32 LE */
        { "\0\0\xFE\xFF\0\0\0\\", 8 }           /* UTF-32 BE */
    };
    USFM_Err err;
    USFM_Ast *ast;
    uint32_t i, count, n;
    const USFM_Node *nodes;
    for (i = 0; i < 4; i++) {
        uint32_t flags;
        for (flags = 0; flags <= USFM_PARSE_NO_UTF8_CHECK; flags += USFM_PARSE_NO_UTF8_CHECK) {
            ast = utf8_parse(wide[i].bytes, wide[i].length, flags, &err);
            CHECK(ast == NULL);
            CHECK_EQ(err.code, USFM_ERR_UNSUPPORTED_ENCODING);
            CHECK_EQ(err.pos, 0);
            USFM_Ast_Free(ast);
        }
    }
    /* Exact two-byte marks are rejected; near-misses parse. */
    {
        static const struct { const char *bytes; uint32_t length; int rejected; } edge[] = {
            { "\xFF\xFE", 2, 1 }, { "\xFE\xFF", 2, 1 },
            { "\xFF", 1, 0 }, { "\xFE", 1, 0 },
            { "\x00\x00\xFE", 3, 0 }, { "\x00\x00\xFF\xFE", 4, 0 }
        };
        for (i = 0; i < 6; i++) {
            ast = utf8_parse(edge[i].bytes, edge[i].length, 0, &err);
            if (edge[i].rejected) {
                CHECK(ast == NULL);
                CHECK_EQ(err.code, USFM_ERR_UNSUPPORTED_ENCODING);
            } else {
                CHECK(ast != NULL);
                CHECK_EQ(err.code, USFM_OK);
            }
            USFM_Ast_Free(ast);
        }
    }
    /* A rejected input makes no allocator call at all. */
    {
        CountAlc c;
        USFM_Alc alc;
        memset(&c, 0, sizeof c);
        c.inner.calls_left = UINT32_MAX;
        alc.alloc = count_alloc;
        alc.resize = count_resize;
        alc.dealloc = count_dealloc;
        alc.ctx = &c;
        ast = USFM_Ast_ParseOpts(wide[0].bytes, wide[0].length, NULL, 0, &alc, &err);
        CHECK(ast == NULL);
        CHECK_EQ(err.code, USFM_ERR_UNSUPPORTED_ENCODING);
        CHECK_EQ(c.calls, 0);
    }
    CHECK_STR(usfm__status_message(USFM_ERR_UNSUPPORTED_ENCODING), "input is UTF-16 or UTF-32, not UTF-8");
    /* A UTF-8 byte-order mark is skipped: no diagnostic, first node after it. */
    ast = utf8_parse("\xEF\xBB\xBF\\p a\n", 8, 0, &err);
    CHECK(ast != NULL);
    if (ast == NULL) return;
    USFM_Ast_GetDiagnostics(ast, &count);
    CHECK_EQ(count, 0);
    nodes = USFM_Ast_GetNodes(ast, &n);
    CHECK(n >= 2 && nodes[1].span.offset == 3);
    USFM_Ast_Free(ast);
}

static void test_utf8_diagnostic_context(void)
{
    static const struct { const char *s; uint32_t length; uint32_t offset; uint32_t bad; uint16_t marker; } cases[] = {
        { "\\p a\xF0\x9F\x98 ", 8, 4, 3, USFM_M_P },
        { "\\p a\xE2\x82", 6, 4, 2, USFM_M_P },
        { "\\p \\w a\xE9\\w*", 11, 7, 1, USFM_M_W }
    };
    uint32_t i;
    for (i = 0; i < 3; i++) {
        USFM_Err err;
        uint32_t count;
        USFM_Ast *ast = utf8_parse(cases[i].s, cases[i].length, 0, &err);
        const USFM_Diagnostic *d;
        CHECK(ast != NULL);
        if (ast == NULL) continue;
        d = USFM_Ast_GetDiagnostics(ast, &count);
        CHECK_EQ(count, 1);
        if (count == 1) {
            CHECK_EQ(d[0].code, USFM_DIAGNOSTIC_INVALID_UTF8);
            CHECK_EQ(d[0].span.offset, cases[i].offset);
            CHECK_EQ(d[0].span.length, cases[i].bad);
            CHECK_EQ(d[0].marker, cases[i].marker);
        }
        USFM_Ast_Free(ast);
    }
}

static void run_utf8_tests(void)
{
    test_utf8_validator_table();
    test_utf8_validator_edges();
    test_utf8_validator_random();
    test_utf8_validator_encoded();
    test_utf8_simd_validator();
    test_utf8_diagnostics();
    test_utf8_opt_out();
    test_utf8_legacy_book();
    test_utf8_parser_paths();
    test_utf8_unclaimed_argument();
    test_utf8_byte_order_marks();
    test_utf8_diagnostic_context();
}

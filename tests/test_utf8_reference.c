/* UTF-8 validation against the reference: every case is parsed normally
 * (one whole-input pass, then per-run checks only if it is invalid) and
 * with the always-per-run reference, and must match. Most cases put a
 * sequence at offsets 61 to 68, so the 32- and 64-byte steps of the SIMD
 * validators end before, inside and after it. (Written for chunked
 * validation, which was measured and reverted: see
 * docs/superpowers/specs/2026-11-19-chunked-utf8-validation-design.md.) */

/* Counts the INVALID_UTF8 diagnostics of a normal parse. */
static uint32_t ref_invalid_count(const char *s, uint32_t n)
{
    USFM_Err err;
    USFM_Ast *ast = USFM_Ast_ParseOpts(s, n, NULL, USFM_PARSE_DEFAULT, NULL, &err);
    uint32_t count = 0, k, total;
    const USFM_Diagnostic *d;
    if (ast == NULL) return 0;
    d = USFM_Ast_GetDiagnostics(ast, &total);
    for (k = 0; k < total; k++) count += d[k].code == USFM_DIAGNOSTIC_INVALID_UTF8;
    USFM_Ast_Free(ast);
    return count;
}

/* Compares a normal parse with the reference, with validation on and
 * off. `invalid` says whether the input should get INVALID_UTF8
 * diagnostics (so each case tests what it claims to). */
static void ref_case_at(const char *file, int line, const char *s, uint32_t n, int invalid,
                        const char *what)
{
    char label[256];
    snprintf(label, sizeof label, "%s:%d: %s", file, line, what);
    usfm_test_checks++;
    if (check_utf8_reference(s, n, NULL, USFM_PARSE_DEFAULT, label) != 0 ||
        check_utf8_reference(s, n, NULL, USFM_PARSE_NO_UTF8_CHECK, label) != 0) {
        usfm_test_failures++;
        fprintf(stderr, "%s: UTF-8 validation differs from the reference\n", label);
    }
    usfm_test_checks++;
    if ((ref_invalid_count(s, n) != 0) != (invalid != 0)) {
        usfm_test_failures++;
        fprintf(stderr, "%s: expected %s input\n", label, invalid ? "invalid" : "valid");
    }
}

#define REF_CASE(s, n, invalid, what) ref_case_at(__FILE__, __LINE__, (s), (n), (invalid), (what))
/* A string literal, which may contain NUL bytes. */
#define REF_LIT(lit, invalid, what) REF_CASE((lit), (uint32_t)(sizeof(lit) - 1u), (invalid), (what))

typedef struct RefSeq {
    const char *bytes;
    uint32_t    length;
    int         invalid;
    const char *name;
} RefSeq;

#define REF_SEQ(b, invalid, name) { (b), (uint32_t)(sizeof(b) - 1u), (invalid), (name) }

static const RefSeq ref_seqs[] = {
    REF_SEQ("\xFF", 1, "FF"),
    REF_SEQ("\xC0\x80", 1, "overlong 2-byte"),
    REF_SEQ("\xE0\x80\x80", 1, "overlong 3-byte"),
    REF_SEQ("\xED\xA0\x80", 1, "surrogate"),
    REF_SEQ("\xF4\x90\x80\x80", 1, "above U+10FFFF"),
    REF_SEQ("\xF5\x80\x80\x80", 1, "F5 lead"),
    REF_SEQ("\xE2\x82", 1, "truncated 3-byte"),
    REF_SEQ("\xF0\x9F\x98", 1, "truncated 4-byte"),
    REF_SEQ("\xC3", 1, "lone 2-byte lead"),
    REF_SEQ("\xC3\xA9", 0, "2-byte"),
    REF_SEQ("\xE2\x82\xAC", 0, "3-byte"),
    REF_SEQ("\xF0\x9F\x98\x80", 0, "4-byte"),
    REF_SEQ("\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80", 0, "2-, 3- and 4-byte"),
    REF_SEQ("\x80", 1, "1 stray continuation"),
    REF_SEQ("\x80\xBF\x80", 1, "3 stray continuations"),
    REF_SEQ("\x80\x80\x80\x80", 1, "4 stray continuations"),
    REF_SEQ("\xBF\xBF\xBF\xBF\xBF\xBF\xBF", 1, "7 stray continuations"),
    REF_SEQ("\xC3\xA9\xA9\xA9\xA9\xA9", 1, "2-byte then 4 continuations"),
    REF_SEQ("\xF0\x9F\x98\x80\x80\x80\x80\x80", 1, "4-byte then 4 continuations")
};

/* Templates: '#' is replaced by padding ('a's) and '@' by the sequence,
 * so the sequence starts at the requested offset. */
static const char *const ref_templates[] = {
    "\\id GEN\n\\c 1\n\\p\n\\v 1 #@ text\n\\v 2 more\n",          /* text */
    "\\id GEN\n\\c 1\n\\p\n\\v 1 #\n\\v 2@ text\n",               /* \v argument */
    "\\id GEN\n\\c 1\n\\p\n\\v 1 #\\w word|lemma=\"@\"\\w* x\n",  /* attributes */
    "\\id GEN\n\\c 1\n\\p\n\\v 1 #\\w w@ord\\w* x\n"              /* character span text */
};

/* Builds `tmpl` with `seq` starting at offset `at`; returns its length,
 * or 0 if `at` is before the padding. */
static uint32_t ref_build(char *out, uint32_t cap, const char *tmpl, const RefSeq *seq,
                            uint32_t at)
{
    const char *pad = strchr(tmpl, '#');
    const char *mark = strchr(tmpl, '@');
    uint32_t before = (uint32_t)(pad - tmpl) + (uint32_t)(mark - pad - 1);
    uint32_t pad_len, n = 0;
    const char *c;
    if (at < before) return 0;
    pad_len = at - before;
    if ((uint32_t)strlen(tmpl) + pad_len + seq->length >= cap) return 0;
    for (c = tmpl; *c; c++) {
        if (*c == '#') {
            memset(out + n, 'a', pad_len);
            n += pad_len;
        } else if (*c == '@') {
            memcpy(out + n, seq->bytes, seq->length);
            n += seq->length;
        } else {
            out[n++] = *c;
        }
    }
    return n;
}

/* Every sequence, in text, an argument, attributes and character-span
 * text, at offsets 61 to 68: invalid bytes before, at and after the
 * 64-byte boundary, and 2-, 3- and 4-byte characters cut by it. */
static void test_utf8_reference_at_boundary(void)
{
    char buf[256], what[160];
    uint32_t s, t, at, n;
    for (s = 0; s < sizeof ref_seqs / sizeof ref_seqs[0]; s++) {
        for (t = 0; t < sizeof ref_templates / sizeof ref_templates[0]; t++) {
            for (at = 61; at <= 68; at++) {
                n = ref_build(buf, sizeof buf, ref_templates[t], &ref_seqs[s], at);
                CHECK(n != 0);
                if (n == 0) continue;
                CHECK(memcmp(buf + at, ref_seqs[s].bytes, ref_seqs[s].length) == 0);
                snprintf(what, sizeof what, "%s in template %u at %u", ref_seqs[s].name,
                         (unsigned)t, (unsigned)at);
                REF_CASE(buf, n, ref_seqs[s].invalid, what);
            }
        }
    }
}

/* A long valid prefix of mixed 1- to 4-byte characters, then an invalid
 * sequence (or a valid one) about 65,536 bytes in. */
static void test_utf8_reference_after_long_prefix(void)
{
    static const char mix[] = "a\xC3\xA9 \xE2\x82\xAC\xF0\x9F\x98\x80";
    const uint32_t cap = 70000u;
    char *buf = (char *)malloc(cap);
    char what[96];
    uint32_t n, at, i;
    if (buf == NULL) {
        CHECK(buf != NULL);
        return;
    }
    for (at = 65530u; at <= 65540u; at++) {
        /* Fill with whole characters up to at most `at`, then pad with
         * ASCII so the invalid byte lands exactly on `at`. */
        n = (uint32_t)strlen("\\id GEN\n\\c 1\n\\p\n\\v 1 ");
        memcpy(buf, "\\id GEN\n\\c 1\n\\p\n\\v 1 ", n);
        while (n + sizeof mix - 1u <= at) {
            memcpy(buf + n, mix, sizeof mix - 1u);
            n += (uint32_t)(sizeof mix - 1u);
        }
        while (n < at) buf[n++] = 'b';
        for (i = 0; i < 2; i++) {
            uint32_t m = n;
            buf[m++] = i == 0 ? '\xFF' : '\x80';
            memcpy(buf + m, " tail\n", 6);
            m += 6;
            snprintf(what, sizeof what, "long prefix, %s at %u", i == 0 ? "FF" : "80", (unsigned)at);
            REF_CASE(buf, m, 1, what);
        }
        /* The same input, valid throughout. */
        memcpy(buf + n, "\xE2\x82\xAC tail\n", 9);
        snprintf(what, sizeof what, "long prefix, valid at %u", (unsigned)at);
        REF_CASE(buf, n + 9, 0, what);
    }
    free(buf);
}

/* An invalid (or valid multi-byte) sequence in the last 3 bytes of the
 * input, with input lengths around 64. */
static void test_utf8_reference_input_end(void)
{
    static const RefSeq ends[] = {
        REF_SEQ("\xFF", 1, "FF"),
        REF_SEQ("\xC3", 1, "lone lead"),
        REF_SEQ("\xE2\x82", 1, "truncated 3-byte"),
        REF_SEQ("\xF0\x9F\x98", 1, "truncated 4-byte"),
        REF_SEQ("\x80", 1, "stray continuation"),
        REF_SEQ("\xFFxy", 1, "FF then 2 bytes"),
        REF_SEQ("\xC3\xA9", 0, "2-byte"),
        REF_SEQ("\xE2\x82\xAC", 0, "3-byte")
    };
    char buf[128], what[96];
    uint32_t s, total, n;
    for (s = 0; s < sizeof ends / sizeof ends[0]; s++) {
        for (total = 60; total <= 70; total++) {
            n = (uint32_t)strlen("\\id GEN\n\\c 1\n\\p ");
            memcpy(buf, "\\id GEN\n\\c 1\n\\p ", n);
            while (n < total - ends[s].length) buf[n++] = 'c';
            memcpy(buf + n, ends[s].bytes, ends[s].length);
            n += ends[s].length;
            snprintf(what, sizeof what, "%s ending a %u-byte input", ends[s].name, (unsigned)n);
            REF_CASE(buf, n, ends[s].invalid, what);
        }
    }
    /* Inputs shorter than one validator step, invalid in their only
     * bytes. */
    REF_LIT("\xFF", 1, "lone FF");
    REF_LIT("\x80\x80\x80\x80", 1, "4 continuations only");
    REF_LIT("\xE2\x82\xAC", 0, "one 3-byte character");
}

/* The UTF-8 BOM is skipped, UTF-16 and UTF-32 BOMs are rejected (NULL
 * from both parses). */
static void test_utf8_reference_bom(void)
{
    char buf[256], what[96];
    uint32_t at, n;
    REF_LIT("\xEF\xBB\xBF", 0, "BOM only");
    REF_LIT("\xEF\xBB\xBF\\id GEN\n\\p x\n", 0, "BOM then text");
    REF_LIT("\xEF\xBB\xBF\x80\\id GEN\n", 1, "BOM then a continuation");
    REF_LIT("\xEF\xBB\\id GEN\n", 1, "truncated BOM");
    REF_LIT("\xFF\xFE\\\0i\0d\0", 0, "UTF-16LE BOM");
    REF_LIT("\xFE\xFF\0\\\0i\0d", 0, "UTF-16BE BOM");
    for (at = 61; at <= 68; at++) {
        static const RefSeq bad = REF_SEQ("\xE2\x82", 1, "truncated 3-byte");
        static const RefSeq good = REF_SEQ("\xF0\x9F\x98\x80", 0, "4-byte");
        static const char tmpl[] = "\xEF\xBB\xBF\\id GEN\n\\c 1\n\\p\n\\v 1 #@ text\n";
        n = ref_build(buf, sizeof buf, tmpl, &bad, at);
        snprintf(what, sizeof what, "BOM, truncated 3-byte at %u", (unsigned)at);
        REF_CASE(buf, n, 1, what);
        n = ref_build(buf, sizeof buf, tmpl, &good, at);
        snprintf(what, sizeof what, "BOM, 4-byte at %u", (unsigned)at);
        REF_CASE(buf, n, 0, what);
    }
}

static void run_utf8_reference_tests(void)
{
    RUN(test_utf8_reference_at_boundary);
    RUN(test_utf8_reference_after_long_prefix);
    RUN(test_utf8_reference_input_end);
    RUN(test_utf8_reference_bom);
}

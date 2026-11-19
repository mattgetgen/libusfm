/* Chunked UTF-8 validation
 * (docs/superpowers/specs/2026-11-19-chunked-utf8-validation-design.md):
 * every case is parsed with the always-per-run reference and at chunk
 * sizes 1, 2, 3, 4, 7, 64 and 65,536, and must match. Most cases put a
 * sequence at offsets 61 to 68, so the 64-byte chunk boundary falls
 * before, inside and after it. */

/* Counts the INVALID_UTF8 diagnostics of a normal parse. */
static uint32_t chunk_invalid_count(const char *s, uint32_t n)
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

/* Compares every chunk size with the reference, with validation on and
 * off. `invalid` says whether the input should get INVALID_UTF8
 * diagnostics (so each case tests what it claims to). */
static void chunk_case_at(const char *file, int line, const char *s, uint32_t n, int invalid,
                          const char *what)
{
    char label[256];
    snprintf(label, sizeof label, "%s:%d: %s", file, line, what);
    usfm_test_checks++;
    if (check_utf8_chunks(s, n, NULL, USFM_PARSE_DEFAULT, label) != 0 ||
        check_utf8_chunks(s, n, NULL, USFM_PARSE_NO_UTF8_CHECK, label) != 0) {
        usfm_test_failures++;
        fprintf(stderr, "%s: chunked UTF-8 validation differs from the reference\n", label);
    }
    usfm_test_checks++;
    if ((chunk_invalid_count(s, n) != 0) != (invalid != 0)) {
        usfm_test_failures++;
        fprintf(stderr, "%s: expected %s input\n", label, invalid ? "invalid" : "valid");
    }
}

#define CHUNK_CASE(s, n, invalid, what) chunk_case_at(__FILE__, __LINE__, (s), (n), (invalid), (what))
/* A string literal, which may contain NUL bytes. */
#define CHUNK_LIT(lit, invalid, what) CHUNK_CASE((lit), (uint32_t)(sizeof(lit) - 1u), (invalid), (what))

typedef struct ChunkSeq {
    const char *bytes;
    uint32_t    length;
    int         invalid;
    const char *name;
} ChunkSeq;

#define CHUNK_SEQ(b, invalid, name) { (b), (uint32_t)(sizeof(b) - 1u), (invalid), (name) }

static const ChunkSeq chunk_seqs[] = {
    CHUNK_SEQ("\xFF", 1, "FF"),
    CHUNK_SEQ("\xC0\x80", 1, "overlong 2-byte"),
    CHUNK_SEQ("\xE0\x80\x80", 1, "overlong 3-byte"),
    CHUNK_SEQ("\xED\xA0\x80", 1, "surrogate"),
    CHUNK_SEQ("\xF4\x90\x80\x80", 1, "above U+10FFFF"),
    CHUNK_SEQ("\xF5\x80\x80\x80", 1, "F5 lead"),
    CHUNK_SEQ("\xE2\x82", 1, "truncated 3-byte"),
    CHUNK_SEQ("\xF0\x9F\x98", 1, "truncated 4-byte"),
    CHUNK_SEQ("\xC3", 1, "lone 2-byte lead"),
    CHUNK_SEQ("\xC3\xA9", 0, "2-byte"),
    CHUNK_SEQ("\xE2\x82\xAC", 0, "3-byte"),
    CHUNK_SEQ("\xF0\x9F\x98\x80", 0, "4-byte"),
    CHUNK_SEQ("\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80", 0, "2-, 3- and 4-byte"),
    CHUNK_SEQ("\x80", 1, "1 stray continuation"),
    CHUNK_SEQ("\x80\xBF\x80", 1, "3 stray continuations"),
    CHUNK_SEQ("\x80\x80\x80\x80", 1, "4 stray continuations"),
    CHUNK_SEQ("\xBF\xBF\xBF\xBF\xBF\xBF\xBF", 1, "7 stray continuations"),
    CHUNK_SEQ("\xC3\xA9\xA9\xA9\xA9\xA9", 1, "2-byte then 4 continuations"),
    CHUNK_SEQ("\xF0\x9F\x98\x80\x80\x80\x80\x80", 1, "4-byte then 4 continuations")
};

/* Templates: '#' is replaced by padding ('a's) and '@' by the sequence,
 * so the sequence starts at the requested offset. */
static const char *const chunk_templates[] = {
    "\\id GEN\n\\c 1\n\\p\n\\v 1 #@ text\n\\v 2 more\n",          /* text */
    "\\id GEN\n\\c 1\n\\p\n\\v 1 #\n\\v 2@ text\n",               /* \v argument */
    "\\id GEN\n\\c 1\n\\p\n\\v 1 #\\w word|lemma=\"@\"\\w* x\n",  /* attributes */
    "\\id GEN\n\\c 1\n\\p\n\\v 1 #\\w w@ord\\w* x\n"              /* character span text */
};

/* Builds `tmpl` with `seq` starting at offset `at`; returns its length,
 * or 0 if `at` is before the padding. */
static uint32_t chunk_build(char *out, uint32_t cap, const char *tmpl, const ChunkSeq *seq,
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
static void test_utf8_chunks_at_boundary(void)
{
    char buf[256], what[160];
    uint32_t s, t, at, n;
    for (s = 0; s < sizeof chunk_seqs / sizeof chunk_seqs[0]; s++) {
        for (t = 0; t < sizeof chunk_templates / sizeof chunk_templates[0]; t++) {
            for (at = 61; at <= 68; at++) {
                n = chunk_build(buf, sizeof buf, chunk_templates[t], &chunk_seqs[s], at);
                CHECK(n != 0);
                if (n == 0) continue;
                CHECK(memcmp(buf + at, chunk_seqs[s].bytes, chunk_seqs[s].length) == 0);
                snprintf(what, sizeof what, "%s in template %u at %u", chunk_seqs[s].name,
                         (unsigned)t, (unsigned)at);
                CHUNK_CASE(buf, n, chunk_seqs[s].invalid, what);
            }
        }
    }
}

/* A long valid prefix of mixed 1- to 4-byte characters, then an invalid
 * sequence: once at offsets 61 to 68 (after a short valid prefix), and
 * around the default chunk size (65,536). */
static void test_utf8_chunks_after_long_prefix(void)
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
            CHUNK_CASE(buf, m, 1, what);
        }
        /* The same input, valid throughout. */
        memcpy(buf + n, "\xE2\x82\xAC tail\n", 9);
        snprintf(what, sizeof what, "long prefix, valid at %u", (unsigned)at);
        CHUNK_CASE(buf, n + 9, 0, what);
    }
    free(buf);
}

/* An invalid (or valid multi-byte) sequence in the last 3 bytes of the
 * input, with input lengths around 64. */
static void test_utf8_chunks_input_end(void)
{
    static const ChunkSeq ends[] = {
        CHUNK_SEQ("\xFF", 1, "FF"),
        CHUNK_SEQ("\xC3", 1, "lone lead"),
        CHUNK_SEQ("\xE2\x82", 1, "truncated 3-byte"),
        CHUNK_SEQ("\xF0\x9F\x98", 1, "truncated 4-byte"),
        CHUNK_SEQ("\x80", 1, "stray continuation"),
        CHUNK_SEQ("\xFFxy", 1, "FF then 2 bytes"),
        CHUNK_SEQ("\xC3\xA9", 0, "2-byte"),
        CHUNK_SEQ("\xE2\x82\xAC", 0, "3-byte")
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
            CHUNK_CASE(buf, n, ends[s].invalid, what);
        }
    }
    /* Inputs shorter than any chunk, invalid in their only bytes. */
    CHUNK_LIT("\xFF", 1, "lone FF");
    CHUNK_LIT("\x80\x80\x80\x80", 1, "4 continuations only");
    CHUNK_LIT("\xE2\x82\xAC", 0, "one 3-byte character");
}

/* The UTF-8 BOM is skipped, UTF-16 and UTF-32 BOMs are rejected (NULL
 * from both parses). */
static void test_utf8_chunks_bom(void)
{
    char buf[256], what[96];
    uint32_t at, n;
    CHUNK_LIT("\xEF\xBB\xBF", 0, "BOM only");
    CHUNK_LIT("\xEF\xBB\xBF\\id GEN\n\\p x\n", 0, "BOM then text");
    CHUNK_LIT("\xEF\xBB\xBF\x80\\id GEN\n", 1, "BOM then a continuation");
    CHUNK_LIT("\xEF\xBB\\id GEN\n", 1, "truncated BOM");
    CHUNK_LIT("\xFF\xFE\\\0i\0d\0", 0, "UTF-16LE BOM");
    CHUNK_LIT("\xFE\xFF\0\\\0i\0d", 0, "UTF-16BE BOM");
    for (at = 61; at <= 68; at++) {
        static const ChunkSeq bad = CHUNK_SEQ("\xE2\x82", 1, "truncated 3-byte");
        static const ChunkSeq good = CHUNK_SEQ("\xF0\x9F\x98\x80", 0, "4-byte");
        static const char tmpl[] = "\xEF\xBB\xBF\\id GEN\n\\c 1\n\\p\n\\v 1 #@ text\n";
        n = chunk_build(buf, sizeof buf, tmpl, &bad, at);
        snprintf(what, sizeof what, "BOM, truncated 3-byte at %u", (unsigned)at);
        CHUNK_CASE(buf, n, 1, what);
        n = chunk_build(buf, sizeof buf, tmpl, &good, at);
        snprintf(what, sizeof what, "BOM, 4-byte at %u", (unsigned)at);
        CHUNK_CASE(buf, n, 0, what);
    }
}

/* White box: the chunk cuts of a valid input end at character boundaries
 * and every chunk is valid. A cut inside a character would make a chunk
 * invalid; the parser would then check runs one by one, which gives the
 * same output (so the tests above cannot see it) but loses the speed. */
static void test_utf8_chunk_cuts(void)
{
    static const char *const inputs[] = {
        "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80" "b\xF0\x9F\x98\x80\xF0\x9F\x98\x80\xE2\x82\xAC\xC3\xA9z",
        "\xEF\xBB\xBF\\id GEN \xE4\xB8\xAD\xE6\x96\x87\xD8\xB9\xD8\xB1\xD8\xA8\xD9\x8A\n",
        "\xF0\x9F\x98\x80\xF0\x9F\x98\x80\xF0\x9F\x98\x80\xF0\x9F\x98\x80\xF0\x9F\x98\x80"
    };
    static const uint32_t chunks[] = {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 61u, 64u};
    uint32_t i, c, from, cut, guard;
    for (i = 0; i < sizeof inputs / sizeof inputs[0]; i++) {
        const uint8_t *u = (const uint8_t *)inputs[i];
        uint32_t n = (uint32_t)strlen(inputs[i]);
        CHECK(usfm__utf8_valid(inputs[i], n));
        for (c = 0; c < sizeof chunks / sizeof chunks[0]; c++) {
            from = 0;
            for (guard = 0; guard <= n && from < n; guard++) {   /* bounded: n + 1 */
                cut = usfm__utf8_cut(u, n, from, chunks[c]);
                CHECK(cut > from && cut <= n);
                CHECK(cut == n || !usfm__utf8_is_cont(u[cut]));
                if (!usfm__utf8_valid_chunk(inputs[i] + from, cut - from)) {
                    usfm_test_failures++;
                    fprintf(stderr, "input %u, chunk %u: chunk [%u, %u) invalid\n", (unsigned)i,
                            (unsigned)chunks[c], (unsigned)from, (unsigned)cut);
                    break;
                }
                usfm_test_checks++;
                if (cut <= from) break;
                from = cut;
            }
            CHECK_EQ(from, n);
        }
    }
    /* 4 or more continuation bytes in a row: no boundary within 3 bytes
     * either way, so the cut stays (and still moves forward). */
    CHECK_EQ(usfm__utf8_cut((const uint8_t *)"a\x80\x80\x80\x80\x80\x80\x80" "b", 9u, 0u, 4u), 4u);
    CHECK_EQ(usfm__utf8_cut((const uint8_t *)"a\x80\x80\x80\x80\x80\x80\x80" "b", 9u, 0u, 1u), 1u);
    CHECK_EQ(usfm__utf8_cut((const uint8_t *)"a\x80\x80\x80\x80\x80\x80\x80" "b", 9u, 1u, 2u), 3u);
}

static void run_utf8_chunks_tests(void)
{
    RUN(test_utf8_chunk_cuts);
    RUN(test_utf8_chunks_at_boundary);
    RUN(test_utf8_chunks_after_long_prefix);
    RUN(test_utf8_chunks_input_end);
    RUN(test_utf8_chunks_bom);
}

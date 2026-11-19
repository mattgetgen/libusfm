/* SIMD tests: the block classifier against the scalar reference, and
 * (Task 2) the block scanner against the byte-loop scanner. */

static uint32_t simd_rng = 12345u;

static uint32_t simd_rand(void)
{
    simd_rng = simd_rng * 1103515245u + 12345u;
    return simd_rng >> 8;
}

static void test_simd_path_name(void)
{
    fprintf(stderr, "    SIMD path: %s\n", USFM__SIMD_PATH);
    fprintf(stderr, "    UTF-8 validator: %s\n", USFM__UTF8_SIMD_PATH);
    CHECK(USFM__SIMD_PATH[0] != '\0');
}

#if USFM__USE_BLOCKS
static int simd_masks_equal(const USFM_BlockMasks *a, const USFM_BlockMasks *b)
{
    return a->backslash == b->backslash && a->pipe == b->pipe && a->slash == b->slash &&
           a->nbsp == b->nbsp && a->loose_ws == b->loose_ws;
}

static void test_classifier_matches_scalar(void)
{
    static const char special[] = "\\|/~\r\n\t";
    char block[64];
    USFM_BlockMasks a, b;
    uint32_t mismatches = 0, n, k;
    for (n = 0; n < 256; n++) {             /* every byte value, whole block */
        memset(block, (int)n, sizeof block);
        usfm__classify_block(block, &a);
        usfm__classify_block_scalar(block, &b);
        if (!simd_masks_equal(&a, &b)) mismatches++;
    }
    for (n = 0; n < 20000; n++) {           /* random, a quarter special bytes */
        for (k = 0; k < 64; k++) {
            uint32_t r = simd_rand();
            block[k] = (r & 3u) == 0 ? special[(r >> 2) % 7u] : (char)(r >> 4);
        }
        usfm__classify_block(block, &a);
        usfm__classify_block_scalar(block, &b);
        if (!simd_masks_equal(&a, &b)) mismatches++;
    }
    CHECK_EQ(mismatches, 0);
}

#if USFM__WINDOW_SCAN
/* The window scanner's text masks against the scalar block reference. */
static void test_text_classifier_matches_scalar(void)
{
    static const char special[] = "\\|/~\r\n\t";
    char block[64];
    USFM_BlockMasks b;
    uint32_t mismatches = 0, n, k, pipe;
    for (n = 0; n < 20256; n++) {     /* every byte value, whole block; then random */
        for (k = 0; k < 64; k++) {
            uint32_t r = simd_rand();
            block[k] = n < 256 ? (char)n : (r & 3u) == 0 ? special[(r >> 2) % 7u] : (char)(r >> 4);
        }
        usfm__classify_block_scalar(block, &b);
        for (pipe = 0; pipe < 2u; pipe++) {
            uint64_t stops, nbsp, loose;
            usfm__classify_text(block, (int)pipe, &stops, &nbsp, &loose);
            if (stops != (b.backslash | b.slash | (pipe ? b.pipe : 0u)) || nbsp != b.nbsp ||
                loose != b.loose_ws) mismatches++;
        }
    }
    CHECK_EQ(mismatches, 0);
}
#endif

static void test_bit_helpers(void)
{
    CHECK_EQ(usfm__ctz64(1u), 0);
    CHECK_EQ(usfm__ctz64((uint64_t)1 << 63), 63);
    CHECK_EQ(usfm__ctz64(0x50u), 4);
    CHECK_EQ(usfm__ctz64(~(uint64_t)0), 0);
    CHECK_EQ(usfm__ctz64((uint64_t)0x8000000000000000ull | 0x100u), 8);
#if !USFM__WINDOW_SCAN
    CHECK_EQ(usfm__bit_range(0, 64), ~(uint64_t)0);
    CHECK_EQ(usfm__bit_range(3, 5), 0x18u);
    CHECK_EQ(usfm__bit_range(64, 64), 0);
    CHECK_EQ(usfm__bit_range(0, 0), 0);
#endif
}

static void test_classifier_long_buffer(void)
{
    /* Every 64-byte block of a 16 KiB random buffer, via the shared helper. */
    static const char special[] = "\\|/~\r\n\t";
    static char buf[16384];
    uint32_t k;
    for (k = 0; k < sizeof buf; k++) {
        uint32_t r = simd_rand();
        buf[k] = (r & 3u) == 0 ? special[(r >> 2) % 7u] : (char)(r >> 4);
    }
    CHECK_EQ(classifier_mismatches(buf, (uint32_t)sizeof buf), 0);
}
#endif

static void test_scanner_block_boundaries(void)
{
    static const char *const snippets[] = {
        "\\\\", "\\/", "\\|", "\\~", "\\\"", "\\=", "//", "/", "|", "~", "\r\n", "\r", "\n",
        "\t", "\\w ", "\\w*", "\\*", "\\+nd ", "\\v 1 ", "|lemma=\"a\\\"b\"\\w*", "\\", "~~//\\\\|"
    };
    char buf[256];
    uint32_t bad = 0, si, off, pipe;
    for (si = 0; si < sizeof snippets / sizeof snippets[0]; si++) {
        uint32_t sl = (uint32_t)strlen(snippets[si]);
        for (off = 0; off < 140u; off++) {
            uint32_t len = off + sl + 40u, k;
            for (k = 0; k < len; k++) buf[k] = (char)('a' + k % 23u);
            memcpy(buf + off, snippets[si], sl);
            for (pipe = 0; pipe < 3u; pipe++) {
                bad += (uint32_t)scan_compare(buf, off + sl, pipe == 2u ? 77u + off : pipe, bad == 0);
                bad += (uint32_t)scan_compare(buf, len, pipe == 2u ? 91u + off : pipe, bad == 0);
            }
        }
    }
    CHECK_EQ(bad, 0);
}

static void test_scanner_every_length(void)
{
    static const char unit[] =
        "\\id GEN x\n\\c 1\n\\p \\v 1 a\\/b~c\td \\w In|strong=\"H1\\=x\"\\w* e//f "
        "\\f + \\ft n\\f* \\qt-s |who=\"P\"\\*g\r\nh\\\\i|j \\zz k\\zz* \\+nd l\\+nd*\n";
    char rich[512];
    uint32_t bad = 0, len, pipe, used = 0;
    while (used + sizeof unit - 1u < sizeof rich) {     /* bounded by sizeof rich */
        memcpy(rich + used, unit, sizeof unit - 1u);
        used += (uint32_t)(sizeof unit - 1u);
    }
    for (len = 0; len <= used; len++) {
        for (pipe = 0; pipe < 3u; pipe++) {
            bad += (uint32_t)scan_compare(rich, len, pipe == 2u ? 5u + len : pipe, bad == 0);
        }
    }
    CHECK_EQ(bad, 0);
}

/* Attribute runs of every length up to 200, ending at every offset of a
 * 64-byte window and past it: with no stop at all in the first window
 * (the run must be continued), and with '/' (a window stop that does not
 * end attributes) every 11 bytes. */
static void test_scanner_long_attributes(void)
{
    static const char *const ends[] = { "\\w*", "  \\w*", "\\\\\\w*", "\\=x\\w*", "\t\\*" };
    static const char filler[] = "lemma=\"H1 x\" strong";
    char buf[300];
    uint32_t bad = 0, len, e, slash;
    for (slash = 0; slash < 2u; slash++) {
        for (e = 0; e < sizeof ends / sizeof ends[0]; e++) {
            for (len = 0; len <= 200u; len++) {
                uint32_t n = 0, k, el = (uint32_t)strlen(ends[e]);
                memcpy(buf, "\\w x|", 5);
                n = 5;
                for (k = 0; k < len; k++) {
                    buf[n++] = slash && k % 11u == 10u ? '/' : filler[k % (sizeof filler - 1u)];
                }
                memcpy(buf + n, ends[e], el);
                n += el;
                memcpy(buf + n, " y", 2);
                n += 2;
                bad += (uint32_t)scan_compare(buf, n, 1u, bad == 0);
                bad += (uint32_t)scan_compare(buf, n - 2u - el, 1u, bad == 0);  /* run to the end */
            }
        }
    }
    CHECK_EQ(bad, 0);
}

static void test_scanner_random(void)
{
    static const char alphabet[] = "\\\\\\|/~\r\n\t \"=*+-abcwvpq1";
    char buf[320];
    uint32_t bad = 0, n;
    for (n = 0; n < 20000u; n++) {
        uint32_t len = simd_rand() % 300u, k;
        for (k = 0; k < len; k++) buf[k] = alphabet[simd_rand() % (sizeof alphabet - 1u)];
        bad += (uint32_t)scan_compare(buf, len, 2u + n, bad == 0);
    }
    CHECK_EQ(bad, 0);
}

static void run_simd_tests(void)
{
    RUN(test_simd_path_name);
    RUN(test_scanner_block_boundaries);
    RUN(test_scanner_every_length);
    RUN(test_scanner_random);
    RUN(test_scanner_long_attributes);
#if USFM__USE_BLOCKS
    RUN(test_bit_helpers);
    RUN(test_classifier_matches_scalar);
#if USFM__WINDOW_SCAN
    RUN(test_text_classifier_matches_scalar);
#endif
    RUN(test_classifier_long_buffer);
#endif
}

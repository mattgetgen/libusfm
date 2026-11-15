/* SIMD tests: the block classifier against the scalar reference, and
 * (Task 2) the block scanner against the byte-loop scanner. */

#if USFM__USE_BLOCKS
static uint32_t simd_rng = 12345u;

static uint32_t simd_rand(void)
{
    simd_rng = simd_rng * 1103515245u + 12345u;
    return simd_rng >> 8;
}

#endif

static void test_simd_path_name(void)
{
    fprintf(stderr, "    SIMD path: %s\n", USFM__SIMD_PATH);
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

static void test_bit_helpers(void)
{
    CHECK_EQ(usfm__ctz64(1u), 0);
    CHECK_EQ(usfm__ctz64((uint64_t)1 << 63), 63);
    CHECK_EQ(usfm__ctz64(0x50u), 4);
    CHECK_EQ(usfm__popcount64(0), 0);
    CHECK_EQ(usfm__popcount64(~(uint64_t)0), 64);
    CHECK_EQ(usfm__bit_range(0, 64), ~(uint64_t)0);
    CHECK_EQ(usfm__bit_range(3, 5), 0x18u);
    CHECK_EQ(usfm__bit_range(64, 64), 0);
    CHECK_EQ(usfm__bit_range(0, 0), 0);
}
#endif

static void run_simd_tests(void)
{
    RUN(test_simd_path_name);
#if USFM__USE_BLOCKS
    RUN(test_bit_helpers);
    RUN(test_classifier_matches_scalar);
#endif
}

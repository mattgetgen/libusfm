/* A counting allocator that checks the allocator contract and can be
 * told to fail after a number of successful calls. Shared by later
 * tests. */
typedef struct TestAlc {
    size_t   live_bytes;
    uint32_t calls_left;                /* UINT32_MAX = never fail */
    uint32_t bad_sizes;                 /* sizes that were not multiples of 8 */
    uint32_t size_mismatches;           /* sized free/resize that disagreed with the block */
    uint32_t null_frees;                /* dealloc called with NULL */
} TestAlc;

/* Each block carries its requested size in an 8-byte header, which keeps
 * the returned pointer 8-byte aligned. */
#define TEST_ALC_HEADER 8u

static void *test_alloc(void *ctx, size_t size)
{
    TestAlc *t = (TestAlc *)ctx;
    unsigned char *base;
    uint64_t stored = (uint64_t)size;
    if (size % 8 != 0) t->bad_sizes++;
    if (t->calls_left == 0) return NULL;
    if (t->calls_left != UINT32_MAX) t->calls_left--;
    base = (unsigned char *)malloc(size + TEST_ALC_HEADER);
    if (!base) return NULL;
    memcpy(base, &stored, sizeof stored);
    t->live_bytes += size;
    return base + TEST_ALC_HEADER;
}

static void *test_resize(void *ctx, void *ptr, size_t old_size, size_t new_size)
{
    TestAlc *t = (TestAlc *)ctx;
    unsigned char *base = (unsigned char *)ptr - TEST_ALC_HEADER;
    unsigned char *p;
    uint64_t stored;
    memcpy(&stored, base, sizeof stored);
    if (stored != (uint64_t)old_size) t->size_mismatches++;
    if (old_size % 8 != 0 || new_size % 8 != 0) t->bad_sizes++;
    if (t->calls_left == 0) return NULL;
    if (t->calls_left != UINT32_MAX) t->calls_left--;
    p = (unsigned char *)realloc(base, new_size + TEST_ALC_HEADER);
    if (!p) return NULL;
    stored = (uint64_t)new_size;
    memcpy(p, &stored, sizeof stored);
    t->live_bytes = t->live_bytes - old_size + new_size;
    return p + TEST_ALC_HEADER;
}

static void test_dealloc(void *ctx, void *ptr, size_t size)
{
    TestAlc *t = (TestAlc *)ctx;
    unsigned char *base;
    uint64_t stored;
    if (ptr == NULL) {
        t->null_frees++;
        return;
    }
    base = (unsigned char *)ptr - TEST_ALC_HEADER;
    memcpy(&stored, base, sizeof stored);
    if (stored != (uint64_t)size) t->size_mismatches++;
    if (size % 8 != 0) t->bad_sizes++;
    t->live_bytes -= size;
    free(base);
}

static USFM_Alc test_alc(TestAlc *state, uint32_t calls_left)
{
    USFM_Alc alc;
    memset(state, 0, sizeof *state);
    state->calls_left = calls_left;
    alc.alloc = test_alloc;
    alc.resize = test_resize;
    alc.dealloc = test_dealloc;
    alc.ctx = state;
    return alc;
}

static void test_round8(void)
{
    CHECK_EQ(usfm__round8(0), 8);
    CHECK_EQ(usfm__round8(1), 8);
    CHECK_EQ(usfm__round8(8), 8);
    CHECK_EQ(usfm__round8(20), 24);
}

static void test_grow_doubles_and_keeps_old_block_on_failure(void)
{
    TestAlc state;
    USFM_Alc alc = test_alc(&state, UINT32_MAX);
    uint32_t capacity = 0;
    void *block = usfm__grow(&alc, NULL, &capacity, 20, 1);
    CHECK(block != NULL);
    CHECK_EQ(capacity, 8);
    block = usfm__grow(&alc, block, &capacity, 20, 9);
    CHECK_EQ(capacity, 16);
    state.calls_left = 0;
    CHECK(usfm__grow(&alc, block, &capacity, 20, 17) == NULL);
    CHECK_EQ(capacity, 16);                     /* unchanged on failure */
    usfm__free(&alc, block, (size_t)capacity * 20);
    CHECK_EQ(state.live_bytes, 0);
    CHECK_EQ(state.bad_sizes, 0);
    CHECK_EQ(state.size_mismatches, 0);
    CHECK_EQ(state.null_frees, 0);
}

static void test_grow_capacity_math(void)
{
    CHECK_EQ(usfm__grow_capacity(0, 20, 1), 8);
    CHECK_EQ(usfm__grow_capacity(8, 20, 9), 16);
    CHECK_EQ(usfm__grow_capacity(5, 20, 5), 5);
    CHECK_EQ(usfm__grow_capacity(0x80000000u, 1, 0x80000001u), 0xFFFFFFF8u);
    CHECK_EQ(usfm__grow_capacity(0x80000000u, 1, 0xFFFFFFF9u), 0);
    CHECK_EQ(usfm__grow_capacity(0, 0xFFFFFFF8u, 1), 1);
}

static void test_alc_free_ignores_oversized(void)
{
    TestAlc state;
    USFM_Alc alc = test_alc(&state, UINT32_MAX);
    void *p = usfm__alloc(&alc, 16);
    CHECK(p != NULL);
    USFM_Alc_Free(&alc, p, (size_t)USFM__ALLOC_MAX + 1u);   /* ignored */
    CHECK_EQ(state.live_bytes, 16);
    USFM_Alc_Free(&alc, p, 16);
    CHECK_EQ(state.live_bytes, 0);
    CHECK_EQ(state.size_mismatches, 0);
    CHECK_EQ(state.null_frees, 0);
    CHECK_EQ(state.bad_sizes, 0);
}

static void test_alc_free_null_is_noop(void)
{
    USFM_Alc_Free(NULL, NULL, 10);
    CHECK(1);
}

static void test_diagnostic_metadata(void)
{
    for (uint16_t code = 1; code < USFM_DIAGNOSTIC_CODE_COUNT; code++) {
        CHECK(USFM_DiagnosticCode_GetMessage(code)[0] != '\0');
        CHECK(USFM_DiagnosticCode_GetSeverity(code) != USFM_SEVERITY_NONE);
    }
    CHECK_EQ(USFM_DiagnosticCode_GetSeverity(USFM_DIAGNOSTIC_IMPLIED_PARAGRAPH), USFM_SEVERITY_WARNING);
    CHECK_EQ(USFM_DiagnosticCode_GetSeverity(USFM_DIAGNOSTIC_UNCLOSED_MARKER), USFM_SEVERITY_ERROR);
    CHECK_STR(USFM_DiagnosticCode_GetMessage(999), "unknown diagnostic");
    CHECK_EQ(USFM_DiagnosticCode_GetSeverity(999), USFM_SEVERITY_NONE);
}

static void run_alloc_tests(void)
{
    RUN(test_round8);
    RUN(test_grow_doubles_and_keeps_old_block_on_failure);
    RUN(test_grow_capacity_math);
    RUN(test_alc_free_ignores_oversized);
    RUN(test_alc_free_null_is_noop);
    RUN(test_diagnostic_metadata);
}

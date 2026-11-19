/* The character-span fast path (docs/superpowers/specs/
 * 2026-11-18-merged-parser-loop-design.md) against the reference
 * parser: every input must give an identical parse with it on and off.
 * CHECK_TREE and CHECK_DIAGS already compare both ways; these inputs aim
 * at each way the fast path can stop or hand back to the main loop. */

static void check_fast_same(const char *file, int line, const char *usfm)
{
    char label[128];
    snprintf(label, sizeof label, "%s:%d", file, line);
    usfm_test_checks++;
    if (check_fast_path(usfm, (uint32_t)strlen(usfm), NULL, 0, label) != 0) {
        usfm_test_failures++;
        fprintf(stderr, "%s: fast path differs for: %s\n", label, usfm);
    }
}
#define CHECK_FAST(usfm) check_fast_same(__FILE__, __LINE__, (usfm))

static void test_fastpath_stops(void)
{
    CHECK_FAST("\\p \\w word|strong=\"H1\"\\w* and \\w more\\w* text.\n");
    CHECK_FAST("\\p \\w word\\nd*\n");                      /* a different closer */
    CHECK_FAST("\\p \\w word\\* tail\n");                    /* bare \\* */
    CHECK_FAST("\\p \\wj said \\+w light|strong=\"H216\"\\+w*, then\\wj*\n");
    CHECK_FAST("\\p \\wj said \\+w light\\w*, then\\wj*\n"); /* closer without + */
    CHECK_FAST("\\p \\w word|lemma=\"x\" no closer\n\\p next\n");
    CHECK_FAST("\\p \\w a // b\\w*\n");                      /* optional break */
    CHECK_FAST("\\p \\w a\\\\b \\/ c\\w*\n");                 /* escapes */
    CHECK_FAST("\\p \\w runs to the end");
    CHECK_FAST("\\p \\w ends|x=\"1\"");
    CHECK_FAST("\\p \\w caf\xE9|lemma=\"\xE9\"\\w* na\xEFve\n"); /* invalid UTF-8 */
    CHECK_FAST("\\p \\w a\\w*\n\n\\p b\n");                  /* blank run, then \\p */
    CHECK_FAST("\\p \\w a\\w*\\w b\\w*\\nd c\\nd*\n");      /* closer then opener */
    CHECK_FAST("\\p \\w a\\w*|stray\n");                     /* pipe after the closer */
    CHECK_FAST("\\p text\\f + \\fr 1:1 \\ft a \\w b\\w* c\\f* d\n"); /* inside a note */
    CHECK_FAST("\\tr \\tc1 \\w a\\w* b \\tc2 \\w c\\w*\n");  /* inside cells */
    CHECK_FAST("\\p \\qt-s |who=\"x\"\\*\\w a\\w* b\\qt-e\\*\n");
    CHECK_FAST("\\p \\w a\r\n b\\w*\r\n\\p c\r\n");          /* CRLF and trimming */
    CHECK_FAST("\\p \\w a \\w b\\w* c\\w*\n");                /* implicit close */
    CHECK_FAST("\\c 1\n\\v 1 \\w In\\w* \\w the\\w*\n\\v 2 \\w end\\w*\n");
    CHECK_FAST("\\w no paragraph\\w* text\n");                /* implied paragraph */
    CHECK_FAST("\\p \\w\\w*\\w \\w* x\n");                    /* empty spans */
    CHECK_FAST("\\p \\zfoo a\\zfoo* \\w b\\w*\n");
    /* After the closer the top frame can be a figure or a header: in a
     * figure '|' starts attributes, so the fast path must hand back. */
    CHECK_FAST("\\p \\fig A \\w b\\w*|src=\"x.jpg\" size=\"col\"\\fig* c\n");
    /* The same through the scanned-closer branch: "\\w*" does not repeat
     * the opening bytes "\\+w". */
    CHECK_FAST("\\p \\fig A \\+w b\\w*|src=\"x.jpg\" size=\"col\"\\fig* c\n");
    CHECK_FAST("\\id GEN\n\\h Gen \\w b\\w* c\n\\c 1\n");
}

/* A span opened at the depth limit fails to open and becomes text; the
 * fast path must not run then. */
static void test_fastpath_depth_limit(void)
{
    char in[512];
    uint32_t n = 0, i;
    memcpy(in + n, "\\p ", 3);
    n += 3;
    for (i = 0; i < 70; i++) {
        memcpy(in + n, "\\+w a", 5);
        n += 5;
    }
    memcpy(in + n, "\\+w*\\+w*\n", 10);
    n += 10;
    in[n] = '\0';
    CHECK_FAST(in);
}

static void run_fastpath_tests(void)
{
    test_fastpath_stops();
    test_fastpath_depth_limit();
}

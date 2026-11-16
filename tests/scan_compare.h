#ifndef USFM_SCAN_COMPARE_H
#define USFM_SCAN_COMPARE_H
/* Runs the block scanner (usfm__scan_next) and the byte-loop reference
 * (usfm__scan_next_scalar) in lockstep over the same input and returns
 * 0 if every token and scanner state agree, or 1 at the first
 * difference (described on stderr if `report` is set).
 *
 * pipe_mode: 0 = '|' never starts attributes, 1 = always, any other
 * value seeds a generator that picks the mode per token (the same for
 * both scanners), the way the parser switches it.
 * Needs USFM_TEST_SCAN_REFERENCE defined before including usfm.h. */
#include <stdio.h>

static int scan_compare(const char *input, uint32_t length, uint32_t pipe_mode, int report)
{
    USFM_Scanner a, b;
    USFM_ScanToken ta, tb;
    uint32_t seed = pipe_mode, index = 0;
    usfm__scan_init(&a, input, length);
    usfm__scan_init(&b, input, length);
    /* Bounded: each successful call consumes at least one byte. */
    for (;;) {
        int ra, rb;
        uint8_t pipe;
        if (pipe_mode <= 1u) {
            pipe = (uint8_t)pipe_mode;
        } else {
            seed = seed * 1103515245u + 12345u;
            pipe = (uint8_t)(seed >> 31);
        }
        a.pipe_is_attr = pipe;
        b.pipe_is_attr = pipe;
        ra = usfm__scan_next(&a, &ta);
        rb = usfm__scan_next_scalar(&b, &tb);
        if (ra != rb || a.position != b.position || a.expect_arg != b.expect_arg ||
            (ra && (ta.tok.span.offset != tb.tok.span.offset ||
                    ta.tok.span.length != tb.tok.span.length ||
                    ta.tok.kind != tb.tok.kind || ta.tok.marker != tb.tok.marker ||
                    ta.tok.level_flags != tb.tok.level_flags || ta.row != tb.row ||
                    ta.name_offset != tb.name_offset || ta.name_length != tb.name_length ||
                    ta.first_loose != tb.first_loose))) {
            if (report) {
                fprintf(stderr,
                        "scanner difference at token %u (pipe %u): blocks kind %u span %u+%u "
                        "flags 0x%02x pos %u | scalar kind %u span %u+%u flags 0x%02x pos %u\n",
                        index, (unsigned)pipe, ra ? ta.tok.kind : 0u, ra ? ta.tok.span.offset : 0u,
                        ra ? ta.tok.span.length : 0u, ra ? ta.tok.level_flags : 0u, a.position,
                        rb ? tb.tok.kind : 0u, rb ? tb.tok.span.offset : 0u,
                        rb ? tb.tok.span.length : 0u, rb ? tb.tok.level_flags : 0u, b.position);
            }
            return 1;
        }
        if (!ra) return 0;
        index++;
    }
}

#if USFM__USE_BLOCKS
/* Classifies every whole 64-byte block of `data` with the SIMD and the
 * scalar classifier; returns the number of blocks whose masks differ. */
static unsigned long long classifier_mismatches(const char *data, uint32_t length)
{
    unsigned long long bad = 0;
    uint32_t base;
    for (base = 0; base + 64u <= length; base += 64u) {     /* bounded by length */
        USFM_BlockMasks a, b;
        usfm__classify_block(data + base, &a);
        usfm__classify_block_scalar(data + base, &b);
        if (a.backslash != b.backslash || a.pipe != b.pipe || a.slash != b.slash ||
            a.nbsp != b.nbsp || a.loose_ws != b.loose_ws) bad++;
    }
    return bad;
}
#endif

#endif

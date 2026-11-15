/* Unity build of every unit test. Internal (static) functions of usfm.h
 * are visible here because this translation unit defines
 * USFM_IMPLEMENTATION. */
#include <stdlib.h>
#include <time.h>
#define USFM_TEST_SCAN_REFERENCE
#define USFM_IMPLEMENTATION
#include "../usfm.h"
#include "test.h"
#include "dump.h"
#include "check.h"
#include "helpers.h"

#include "test_basics.c"
#include "test_markers.c"
#include "test_alloc.c"
#include "test_scanner.c"
#include "test_parser.c"
#include "test_chars.c"
#include "test_notes.c"
#include "test_structure.c"
#include "test_versions.c"
#include "test_iter.c"
#include "test_parse_alloc.c"
#include "test_simd.c"

int main(void)
{
    fprintf(stderr, "basics\n");
    run_basics_tests();
    fprintf(stderr, "markers\n");
    run_markers_tests();
    fprintf(stderr, "alloc\n");
    run_alloc_tests();
    fprintf(stderr, "scanner\n");
    run_scanner_tests();
    fprintf(stderr, "parser\n");
    run_parser_tests();
    fprintf(stderr, "chars\n");
    run_chars_tests();
    fprintf(stderr, "notes\n");
    run_notes_tests();
    fprintf(stderr, "structure\n");
    run_structure_tests();
    fprintf(stderr, "versions\n");
    run_versions_tests();
    fprintf(stderr, "iterators\n");
    run_iter_tests();
    fprintf(stderr, "parse alloc\n");
    run_parse_alloc_tests();
    fprintf(stderr, "simd\n");
    run_simd_tests();
    if (usfm_test_failures != 0) {
        fprintf(stderr, "FAILED: %d of %d checks\n", usfm_test_failures, usfm_test_checks);
        return 1;
    }
    fprintf(stderr, "ok: %d checks\n", usfm_test_checks);
    return 0;
}

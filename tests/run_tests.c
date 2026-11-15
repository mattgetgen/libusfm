/* Unity build of every unit test. Internal (static) functions of usfm.h
 * are visible here because this translation unit defines
 * USFM_IMPLEMENTATION. */
#include <stdlib.h>
#define USFM_IMPLEMENTATION
#include "../usfm.h"
#include "test.h"
#include "dump.h"

#include "test_basics.c"
#include "test_markers.c"
#include "test_alloc.c"
#include "test_scanner.c"

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
    if (usfm_test_failures != 0) {
        fprintf(stderr, "FAILED: %d of %d checks\n", usfm_test_failures, usfm_test_checks);
        return 1;
    }
    fprintf(stderr, "ok: %d checks\n", usfm_test_checks);
    return 0;
}

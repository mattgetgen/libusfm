#ifndef USFM_TEST_H
#define USFM_TEST_H
/* Minimal test framework: no dependencies, no allocation. */
#include <stdio.h>
#include <string.h>

static int usfm_test_failures = 0;
static int usfm_test_checks = 0;

#define CHECK(cond) do { \
    usfm_test_checks++; \
    if (!(cond)) { \
        usfm_test_failures++; \
        fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

#define CHECK_EQ(actual, expected) do { \
    unsigned long long check_a_ = (unsigned long long)(actual); \
    unsigned long long check_e_ = (unsigned long long)(expected); \
    usfm_test_checks++; \
    if (check_a_ != check_e_) { \
        usfm_test_failures++; \
        fprintf(stderr, "%s:%d: %s == %llu, expected %llu\n", \
                __FILE__, __LINE__, #actual, check_a_, check_e_); \
    } \
} while (0)

#define CHECK_STR(actual, expected) do { \
    const char *check_a_ = (actual); \
    const char *check_e_ = (expected); \
    usfm_test_checks++; \
    if (strcmp(check_a_, check_e_) != 0) { \
        usfm_test_failures++; \
        fprintf(stderr, "%s:%d: %s\n--- expected\n%s\n--- actual\n%s\n", \
                __FILE__, __LINE__, #actual, check_e_, check_a_); \
    } \
} while (0)

#define RUN(test) do { fprintf(stderr, "  %s\n", #test); test(); } while (0)

#endif

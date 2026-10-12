#ifndef SIMPL_TEST_H
#define SIMPL_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned test_assertions;

#define TEST_ASSERT(condition) do { \
    ++test_assertions; \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: assertion failed: %s\n", \
                __FILE__, __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

#define TEST_EQ_INT(actual, expected) \
    TEST_ASSERT((actual) == (expected))

#define TEST_EQ_STR(actual, expected) \
    TEST_ASSERT(strcmp((actual), (expected)) == 0)

#define TEST_REPORT(suite) \
    printf("%s: %u проверок пройдено.\n", (suite), test_assertions)

#ifdef assert
#undef assert
#endif
#define assert TEST_ASSERT

#endif

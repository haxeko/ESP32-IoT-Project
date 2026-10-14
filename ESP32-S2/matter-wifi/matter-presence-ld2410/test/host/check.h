// Minimal test helpers for the host tests (test/host/run.sh)
#pragma once

#include <stdio.h>

static int g_failures;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                    \
            g_failures++;                                                                                              \
        }                                                                                                              \
    } while (0)

#define CHECK_EQ(actual, expected)                                                                                     \
    do {                                                                                                               \
        long long a_ = (long long)(actual), e_ = (long long)(expected);                                                \
        if (a_ != e_) {                                                                                                \
            printf("  FAIL %s:%d: %s == %lld, expected %lld\n", __FILE__, __LINE__, #actual, a_, e_);                    \
            g_failures++;                                                                                              \
        }                                                                                                              \
    } while (0)

static int test_result(const char *name)
{
    printf("%s: %s\n", name, g_failures ? "FAILED" : "ok");
    return g_failures ? 1 : 0;
}

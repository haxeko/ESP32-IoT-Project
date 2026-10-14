// Minimal check macro for the host tests: counts failures instead of stopping at the first one.
#pragma once

#include <stdio.h>

static int g_failures;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            g_failures++;                                                                                              \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                   \
        }                                                                                                              \
    } while (0)

static inline int check_report(void)
{
    printf(g_failures ? "  %d failure(s)\n" : "  OK\n", g_failures);
    return g_failures != 0;
}

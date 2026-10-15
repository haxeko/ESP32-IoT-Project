#pragma once
#include <stddef.h>
#define MALLOC_CAP_INTERNAL 0
static inline size_t heap_caps_get_free_size(int) { return 60000; }
static inline size_t heap_caps_get_minimum_free_size(int) { return 40000; }
static inline size_t heap_caps_get_largest_free_block(int) { return 30000; }

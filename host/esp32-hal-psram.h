#ifndef HOST_PSRAM_SHIM_H
#define HOST_PSRAM_SHIM_H
#include <stdlib.h>
#include <stddef.h>
static inline void* ps_malloc(size_t n) { return malloc(n); }
static inline void* ps_calloc(size_t n, size_t sz) { return calloc(n, sz); }
#endif

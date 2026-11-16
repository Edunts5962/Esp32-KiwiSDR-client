#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
void *plat_malloc_ps(size_t n) { return calloc(1, n); }
uint32_t g_ms = 0;
uint32_t plat_ms() { return g_ms; }

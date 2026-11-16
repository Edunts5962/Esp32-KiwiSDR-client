#include <stdlib.h>
#include <stdint.h>
#include <chrono>
void *plat_malloc_ps(size_t n) { return calloc(1, n); }
uint32_t plat_ms() { using namespace std::chrono; return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count(); }
uint64_t plat_epoch_ms() { return 0; }

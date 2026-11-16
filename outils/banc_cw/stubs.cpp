#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
void *plat_malloc_ps(size_t n) { return calloc(1, n); }
bool plat_sd() { return false; }
void plat_horodatage(char *s, size_t n) { if (n) s[0] = 0; }
bool plat_fichier_ouvrir(const char *) { return false; }
bool plat_fichier_ecrire(const void *, size_t) { return false; }
void plat_fichier_fermer() {}

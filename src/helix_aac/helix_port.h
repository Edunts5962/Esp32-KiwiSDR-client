/* helix_port.h - adaptation du decodeur AAC Helix (RealNetworks) au projet kiwi_console */
#pragma once
#include <stdlib.h>
#ifdef __cplusplus
extern "C" {
#endif
void *helix_malloc(int size);     /* kiwi_console : en PSRAM */
void helix_free(void *ptr);
#ifdef __cplusplus
}
#endif
#define PROGMEM
#define pgm_read_byte(addr) (*(const unsigned char *)(addr))
#define pgm_read_word(addr) (*(const unsigned short *)(addr))
#define HELIX_FEATURE_AUDIO_CODEC_AAC_SBR 1

// banc FT8 : decode un fichier WAV 12 kHz 16 bits avec le module de la carte, affiche comme WSJT-X
#include "ft8.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <chrono>
int main(int argc, char **argv)
{
    f8::position(48.85f, 2.35f);
    for (int a = 1; a < argc; a++) {
        FILE *f = fopen(argv[a], "rb"); if (!f) continue;
        std::vector<int16_t> x(15 * 12000);
        fseek(f, 44, SEEK_SET);
        int n = fread(x.data(), 2, x.size(), f); fclose(f);
        static f8::Message m[50];
        auto t0 = std::chrono::steady_clock::now();
        int k = f8::decoder(x.data(), n, 0, m, 50);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        for (int i = 0; i < k; i++) printf("000000 %3d %4.1f %4d ~  %-28s | %s %s %s %u km\n", m[i].snr, m[i].dt10 / 10.0, m[i].hz, m[i].texte, m[i].indicatif, m[i].loc, m[i].pays, m[i].km);
        fprintf(stderr, "temps %.1f ms\n", ms);
    }
}

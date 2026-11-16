// banc d'essai de la reserve audio : un reseau irregulier (paquets de 1024 echantillons a 12 kHz, gigue et
// blocages), une tache I2S qui lit 240 trames toutes les 5 ms ; on compte les coupures, les clics et le silence.
#include "audio.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <random>
extern uint32_t g_ms;
int main(int argc, char **argv)
{
    int scen = argc > 1 ? atoi(argv[1]) : 0;
    std::mt19937 rng(argc > 2 ? atoi(argv[2]) : 1);
    std::exponential_distribution<double> ex(1.0);
    aud::init(); aud::debit_entree(12000); aud::reserve_visee(350);
    const int PAQ = 1024;                       // ~85 ms
    double t_paquet = 0, ph = 0;                // heure d'arrivee du prochain paquet (ms)
    double t_bloque = -1;
    long clics = 0, trames_silence = 0, trames = 0; int16_t prec = 0; bool joue = false;
    const double DUREE = 600000;                // 10 minutes
    int16_t buf[PAQ], st[480];
    for (double t = 0; t < DUREE; t += 5) {
        g_ms = (uint32_t)t;
        // reseau : les paquets arrivent ; scenario 0 = regulier, 1 = gigue, 2 = gigue + blocages
        while (t_paquet <= t) {
            for (int i = 0; i < PAQ; i++) { ph += 2 * M_PI * 440 / 12000; buf[i] = (int16_t)(6000 * sin(ph)); }
            aud::pousser(buf, PAQ);
            double base = PAQ * 1000.0 / 12000;
            double d = base;
            if (scen >= 1) d = base * 0.4 + base * 0.6 * ex(rng);           // gigue (moyenne conservee)
            if (scen >= 2 && std::uniform_real_distribution<double>(0, 1)(rng) < 0.004) d += 500 + 700 * std::uniform_real_distribution<double>(0, 1)(rng);   // blocage WiFi
            t_paquet += d;
        }
        aud::lire_sortie(st, 240);
        for (int i = 0; i < 240; i++) {
            int16_t v = st[2 * i];
            trames++;
            if (abs(v - prec) > 1500) clics++;                      // saut impossible pour une sinusoide a 440 Hz
            if (v == 0 && prec == 0) trames_silence++;
            prec = v;
        }
    }
    printf("scenario %d : coupures %u, clics %ld, silence %.1f s, reserve finale %d ms\n", scen, (unsigned)aud::coupures(), clics, trames_silence / 48000.0, aud::reserve_ms());
}

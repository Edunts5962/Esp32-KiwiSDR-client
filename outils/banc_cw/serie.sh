#!/bin/sh
# serie complete, un processus par essai (comme une carte qui vient de demarrer) ; $1 = programme
B=$1
r() { $B o "$@" | sed 's/.*CER *//' | cut -d'%' -f1; }
printf "ouverture 12dB %s | " "$(r 12)"
for s in 8 5 3 0 -2; do printf "S/B%s %s | " $s "$(r $s)"; done; echo
for w in 8 12 20 30 40 45; do printf "%swpm %s | " $w "$(r 30 0 $w)"; done; echo
printf "jit10 %s | jit20 %s | jit30 %s | trait4 %s | trait2.5 %s | QSB10 %s | QSB20 %s | decal150 %s\n" "$(r 15 0 20 0.1)" "$(r 15 0 20 0.2)" "$(r 15 0 20 0.3)" "$(r 15 0 20 0 0 4)" "$(r 15 0 20 0 0 2.5)" "$(r 15 0 20 0 10)" "$(r 15 0 20 0 20)" "$(r 15 150)"

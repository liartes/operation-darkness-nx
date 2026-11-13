#!/bin/sh
# Script de manette (--od_manette) qui va au plus vite au début du tour d'Edward (mission 1) : START toutes les 3 s
# pendant les logos et la vidéo d'introduction (menu principal vers 30 s), A pour « New Game », le nom et la
# confirmation, START et A en alternance jusqu'à la bataille (~80 s : START y ouvrirait le menu système), puis A
# seulement. Menu d'action d'Edward vers 90 s (rendu natif, 60 images/s). À partir de 96 s, un B 1,5 s après un A sur
# deux : B termine les tutoriels (« Press A to continue or B to finish the tutorial »), sans quoi les A y bouclent
# (remarque de l'utilisateur, 2026-10-13) ; ailleurs il annule l'action en cours, que le A suivant relance.
# Usage : MANETTE="$(tools/scenario_combat.sh)" tools/captures.sh ...
S=""
for t in $(seq 3 3 27); do S="$S${S:+,}$t:start"; done
S="$S,32:a,36:a,40:a"
for t in $(seq 44 4 76); do S="$S,$t:start,$((t+2)):a"; done
for t in $(seq 82 3 94); do S="$S,$t:a"; done
for t in $(seq 97 6 300); do S="$S,$t:a,$((t+3)):a"; done
for t in $(seq 98 6 300); do S="$S,${t}.5:b"; done
echo "$S"

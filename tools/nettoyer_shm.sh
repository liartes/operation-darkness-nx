#!/bin/sh
# Supprime les fichiers de mémoire invitée (/dev/shm/xenia_memory_*) que plus aucun processus n'ouvre : le SDK ne
# les libère pas quand le jeu se termine brutalement (fin de délai, plantage), et /dev/shm plein donne « Erreur du
# bus » au lancement suivant. Appelé par les scripts de lancement.
for f in /dev/shm/xenia_memory_*; do
  [ -e "$f" ] || continue
  fuser "$f" >/dev/null 2>&1 || rm -f -- "$f"
done

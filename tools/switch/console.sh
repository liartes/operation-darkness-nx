#!/bin/bash
# Échanges avec la Switch branchée en MTP (DBI, monté par GVFS), sur le modèle de torchlight-nx switch/console.sh.
# Dossier sur la carte : sdmc:/switch/operation-darkness-nx/ = operation-darkness-nx.nro, sa2_shaders.mesp, cache_shaders.odsc,
# game_root/ (copie de assets/ : données du jeu), opdarkness.toml (écrit
# par le jeu au premier lancement), logs/ (journaux du SDK).
#
# Usage : tools/switch/console.sh <commande>
#   etat               ce qu'il y a sur la console
#   envoyer            copie le .nro, le paquet et, s'il est préparé (CACHE_PC=1), le cache de shaders du PC
#   vider-cache        supprime cache_shaders.odsc de la console (shaders traduits par elle : essai à froid)
#   envoyer-jeu        copie aussi assets/ dans game_root/ (4,6 Go, 5 302 fichiers : long en MTP ; une seule fois)
#   journaux [--vider] copie logs/ de la console dans out/switch-logs/ (vidé d'abord) ; --vider les efface ensuite
#
# Lecture par le point de montage GVFS, écritures et suppressions par gio (plus fiable en MTP que cp/rm).
set -e
RACINE="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$RACINE/out/switch-sd/switch/operation-darkness-nx"

# sdmc:/switch vu par GVFS : premier stockage « SD Card » d'un appareil MTP qui le contient.
trouver() {
  local d s
  for d in /run/user/"$(id -u)"/gvfs/mtp:host=*; do
    [ -d "$d" ] || continue
    for s in "$d"/*SD\ Card; do
      if [ -d "$s/switch" ]; then
        T="$s/switch/operation-darkness-nx"
        [ -d "$T" ] || gio mkdir "$T"
        return 0
      fi
    done
  done
  echo "Switch introuvable en MTP : lancer DBI en mode MTP et brancher la console." >&2
  exit 1
}

envoyer_fichier() {
  local src="$1" dst="$2"
  echo "envoi de $(basename "$src") ($(du -h "$src" | cut -f1))..."
  gio remove "$dst" 2>/dev/null || true
  gio copy "$src" "$dst"
}

cmd="${1:-etat}"
trouver
case "$cmd" in
  etat)
    echo "Console : $T"
    for f in operation-darkness-nx.nro opdarkness.toml sa2_shaders.mesp cache_shaders.odsc game_root/default.xex; do
      if [ -f "$T/$f" ]; then echo "  $f : $(du -h "$T/$f" | cut -f1)"; else echo "  $f : absent"; fi
    done
    echo "  journaux : $(find "$T/logs" -type f 2>/dev/null | wc -l) fichiers"
    ;;
  envoyer|envoyer-jeu)
    [ -f "$SRC/operation-darkness-nx.nro" ] || { echo "Pas de $SRC/operation-darkness-nx.nro : lancer tools/switch/carte_sd.sh" >&2; exit 1; }
    for f in operation-darkness-nx.nro sa2_shaders.mesp cache_shaders.odsc; do
      [ -f "$SRC/$f" ] && envoyer_fichier "$SRC/$f" "$T/$f"
    done
    if [ "$cmd" = envoyer-jeu ]; then
      echo "copie de assets/ dans game_root/ (long)..."
      gio mkdir "$T/game_root" 2>/dev/null || true
      (cd "$RACINE/assets" && find . -type d | sort) | while IFS= read -r d; do
        [ "$d" = . ] || gio mkdir "$T/game_root/${d#./}" 2>/dev/null || true
      done
      n=0
      (cd "$RACINE/assets" && find . -type f | sort) | while IFS= read -r f; do
        gio copy "$RACINE/assets/${f#./}" "$T/game_root/${f#./}"
        n=$((n+1)); [ $((n % 200)) = 0 ] && echo "  $n fichiers"
      done
    fi
    echo "fait"
    ;;
  vider-cache)
    gio remove "$T/cache_shaders.odsc" 2>/dev/null && echo "cache supprimé" || echo "pas de cache"
    ;;
  journaux)
    DEST="$RACINE/out/switch-logs"
    mkdir -p "$DEST"
    find "$DEST" -mindepth 1 -delete
    [ -d "$T/logs" ] && cp -r "$T/logs/." "$DEST/"
    echo "copié dans $DEST :"
    (cd "$DEST" && find . -type f | sort | sed 's/^/  /')
    if [ "${2:-}" = "--vider" ]; then
      find "$T/logs" -type f -print0 | while IFS= read -r -d '' f; do gio remove "$f"; done
      echo "journaux effacés sur la console"
    fi
    ;;
  *) echo "commande inconnue : $cmd" >&2; exit 1 ;;
esac

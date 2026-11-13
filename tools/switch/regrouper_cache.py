#!/usr/bin/env python3
"""Regroupe les shaders traduits en jeu sur PC (<build>/cache_shaders/*.odsh) en un seul fichier .odsc (les
enregistrements bout à bout), que la version Switch relit (src/od_traducteur.cpp : pas de traduction sur la console,
et lister un dossier y est peu fiable). Donnée dérivée du jeu : jamais versionnée ni distribuée.
  python3 tools/switch/regrouper_cache.py [dossier .odsh] [sortie .odsc]
"""
import os, sys

dossier = sys.argv[1] if len(sys.argv) > 1 else "out/build/sa2sdk/cache_shaders"
sortie = sys.argv[2] if len(sys.argv) > 2 else "out/build/switch/cache_shaders.odsc"
noms = sorted(f for f in os.listdir(dossier) if f.endswith(".odsh"))
with open(sortie, "wb") as out:
    for f in noms:
        d = open(os.path.join(dossier, f), "rb").read()
        if d[:8] != b"ODSHAD01":
            sys.exit(f"{f} : en-tête inattendu")
        out.write(d)
print(f"{len(noms)} shaders -> {sortie} ({os.path.getsize(sortie)} octets)")

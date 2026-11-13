#!/usr/bin/env python3
"""Trous du code : zones de la plage de code qu'aucune fonction générée ne couvre (code jamais atteint statiquement,
que le jeu peut appeler par un pointeur que tools/candidats.py n'a pas vu).

L'adresse de chaque instruction générée (une ligne « \\t// » par instruction) est recalée sur chaque label loc_XXXXXXXX :
les tables de saut que MSVC place dans le code (après le bctr d'un switch) ne sont pas des instructions, et un simple
comptage (find_gaps.py de sa2-nx) les compte comme des trous. Un trou est ensuite classé d'après l'image :
  remplissage  que des 0 ;
  table        que des adresses de la plage de code (table de saut ou de pointeurs) ;
  code         le reste (à examiner : tools/desas.py).

Usage : trous.py [--min 8] [--image out/analyse/image.bin]   ->  liste des trous « code » sur stdout, résumé sur stderr
"""
import collections
import glob
import re
import struct
import sys

BASE = 0x82000000
GEN = "generated/default/"


def couvertures():
    """{début: fin} de chaque fonction générée."""
    res = {}
    for path in glob.glob(GEN + "*_recomp.*.cpp"):
        debut = adresse = fin = None
        for ligne in open(path, errors="replace"):
            m = re.match(r"DEFINE_REX_FUNC\((?:__imp__)?sub_([0-9A-F]{8})\)", ligne)
            if m:
                if debut is not None:
                    res[debut] = fin
                debut = adresse = fin = int(m.group(1), 16)
                continue
            if debut is None:
                continue
            m = re.match(r"loc_([0-9A-F]{8}):", ligne)
            if m:
                adresse = int(m.group(1), 16)
                continue
            if ligne.startswith("\t// ") and "ERROR" not in ligne:
                adresse += 4
                fin = max(fin, adresse)
        if debut is not None:
            res[debut] = fin
    return res


def main():
    mini = int(sys.argv[sys.argv.index("--min") + 1]) if "--min" in sys.argv else 8
    image = sys.argv[sys.argv.index("--image") + 1] if "--image" in sys.argv else "out/analyse/image.bin"
    data = open(image, "rb").read()
    cov = couvertures()
    debuts = sorted(cov)
    lo, hi = debuts[0], max(cov.values())
    mot = lambda a: struct.unpack_from(">I", data, a - BASE)[0]
    stats = collections.Counter()
    octets = collections.Counter()
    fin_prec = lo
    for d in debuts + [hi]:
        if d > fin_prec and d - fin_prec >= mini:
            mots = [mot(a) for a in range(fin_prec, d, 4)]
            if all(w == 0 for w in mots):
                genre = "remplissage"
            elif all(w == 0 or lo <= w < hi for w in mots):
                genre = "table"
            else:
                genre = "code"
                print(f"0x{fin_prec:08X} {d - fin_prec:6d} octets (avant 0x{d:08X})")
            stats[genre] += 1
            octets[genre] += d - fin_prec
        fin_prec = max(fin_prec, cov.get(d, d))
    print(f"{len(cov)} fonctions ; trous >= {mini} octets : "
          + ", ".join(f"{g} {stats[g]} ({octets[g]} octets)" for g in ("code", "table", "remplissage")),
          file=sys.stderr)


if __name__ == "__main__":
    main()

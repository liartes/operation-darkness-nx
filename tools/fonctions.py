#!/usr/bin/env python3
"""Index des fonctions générées (taille, appels sortants et entrants), pour l'analyse du code du jeu.

  fonctions.py index                    construit out/analyse/fonctions.json depuis generated/default (~20 s)
  fonctions.py info <adresse> [...]     taille, appelants, appelés d'une fonction
  fonctions.py appelants <cible> [...]  fonctions qui appellent la cible (adresse ou import, ex. VdSwap)
  fonctions.py cherche <regex> [--plage 82300000:82400000]
                                        fonctions dont une instruction (listing PPC) correspond à la regex
"""
import glob
import json
import os
import re
import sys

GEN = "generated/default/"
INDEX = "out/analyse/fonctions.json"


def construire():
    fonctions = {}
    for path in glob.glob(GEN + "*_recomp.*.cpp"):
        cur = None
        for ligne in open(path, errors="replace"):
            m = re.match(r"DEFINE_REX_FUNC\((?:__imp__)?sub_([0-9A-F]+)\)", ligne)
            if m:
                cur = {"instr": [], "appels": []}
                fonctions[m.group(1)] = cur
                continue
            if cur is None:
                continue
            if ligne.startswith("\t// ") and "ERROR" not in ligne:
                instr = ligne[4:].rstrip("\n")
                cur["instr"].append(instr)
                m = re.match(r"bl 0x([0-9a-f]+)$", instr)
                if m:
                    cur["appels"].append(m.group(1).upper())
            m = re.search(r"__imp__([A-Za-z_][A-Za-z0-9_]*)\(ctx, base\)", ligne)
            if m and not m.group(1).startswith("sub_"):
                cur["appels"].append(m.group(1))
    os.makedirs(os.path.dirname(INDEX), exist_ok=True)
    json.dump(fonctions, open(INDEX, "w"))
    print(f"{len(fonctions)} fonctions -> {INDEX}")


def charger():
    f = json.load(open(INDEX))
    entrants = {}
    for a, d in f.items():
        for c in d["appels"]:
            entrants.setdefault(c, []).append(a)
    return f, entrants


def cle(a):
    a = a.upper().removeprefix("0X").removeprefix("SUB_")
    return a


def main():
    cmd = sys.argv[1]
    if cmd == "index":
        return construire()
    f, entrants = charger()
    if cmd == "info":
        for a in map(cle, sys.argv[2:]):
            d = f.get(a)
            if not d:
                print(f"sub_{a} : inconnue")
                continue
            ap = sorted(set(entrants.get(a, [])))
            print(f"sub_{a} : {len(d['instr'])} instr ; appelée {len(entrants.get(a, []))} fois par {len(ap)} "
                  f"fonctions {' '.join(ap[:12])}{' ...' if len(ap) > 12 else ''}")
            print(f"  appelle : {' '.join(d['appels'])}")
    elif cmd == "appelants":
        for a in sys.argv[2:]:
            a = a if not re.match(r"^(0x|sub_)?[0-9A-Fa-f]{8}$", a) else cle(a)
            print(f"{a} : {' '.join(sorted(set(entrants.get(a, []))))}")
    elif cmd == "cherche":
        motif = re.compile(sys.argv[2])
        lo, hi = 0, 0xFFFFFFFF
        if "--plage" in sys.argv:
            x, y = sys.argv[sys.argv.index("--plage") + 1].split(":")
            lo, hi = int(x, 16), int(y, 16)
        for a in sorted(f):
            if lo <= int(a, 16) < hi:
                t = [i for i in f[a]["instr"] if motif.search(i)]
                if t:
                    print(f"sub_{a}: " + " | ".join(t[:4]) + (" ..." if len(t) > 4 else ""))


if __name__ == "__main__":
    main()

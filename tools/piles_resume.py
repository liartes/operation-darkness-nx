#!/usr/bin/env python3
"""Résume out/profil/gdb.txt (tools/piles.sh) : pour chaque thread, combien d'échantillons dans chaque situation
(fonction hôte du sommet de pile + premières fonctions du jeu).

Usage : piles_resume.py [fichier] [--thread motif] [--profondeur 3]
"""
import collections
import re
import sys


def main():
    args = sys.argv[1:]
    prof = int(args[args.index("--profondeur") + 1]) if "--profondeur" in args else 3
    motif = args[args.index("--thread") + 1] if "--thread" in args else ""
    fichier = next((a for a in args if not a.startswith("--") and a not in (motif, str(prof))), "out/profil/gdb.txt")
    par_thread = collections.defaultdict(collections.Counter)
    nom = None
    pile = []

    def fin():
        if nom is None or not pile:
            return
        hote = next((f for f in pile if not f.startswith("sub_")), "?")
        jeu = [f for f in pile if f.startswith("sub_")][:prof]
        par_thread[nom][(hote, " < ".join(jeu))] += 1

    for ligne in open(fichier, errors="replace"):
        m = re.match(r'Thread \d+ \(Thread 0x[0-9a-f]+ \(LWP \d+\) "([^"]*)"\):', ligne)
        if m:
            fin()
            nom, pile = m.group(1), []
            continue
        m = re.match(r"#\d+\s+(?:0x[0-9a-f]+ in )?(\S+)", ligne)
        if m and nom is not None:
            f = m.group(1)
            f = re.sub(r"^__imp__", "", f)
            if "KeDelayExecutionThread" in f:
                f = "KeDelayExecutionThread"
            elif "KeWaitForSingleObject" in f or "NtWaitForSingleObjectEx" in f or "KeWaitForMultipleObjects" in f:
                f = re.search(r"(Ke\w+|Nt\w+)_entry", f).group(1) if re.search(r"(Ke\w+|Nt\w+)_entry", f) else f
            elif f.startswith("_ZN") or f.startswith("std::"):
                f = f[:60]
            pile.append(f)
            continue
        if not ligne.strip():
            fin()
            nom, pile = None, []
    fin()
    for t in sorted(par_thread):
        if motif and motif not in t:
            continue
        total = sum(par_thread[t].values())
        for (hote, jeu), n in par_thread[t].most_common(4):
            print(f"{t:22s} {n}/{total}  {hote[:40]:40s} {jeu}")


if __name__ == "__main__":
    main()

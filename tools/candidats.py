#!/usr/bin/env python3
"""Cherche les fonctions manquantes du manifeste : pointeurs vers le code trouvés dans les données de l'image
(tables virtuelles, rappels) ou calculés dans le code (lis/addi), qui ne sont pas des débuts de fonction générés, et dont l'instruction précédente
termine une fonction (blr, bctr, b, remplissage 0). Pour chacune, propose `{ end = ... }` : la fin est le premier
blr/bctr/b inconditionnel atteint en ligne droite (fonction feuille courte) ou, à défaut, rien.

Usage : candidats.py [--ecrire] [image] (image : out/analyse/image.bin, voir tools/dump_image.sh)
--ecrire : remplace la section dans opdarkness_manifest.toml au lieu d'écrire sur stdout.
Sortie : lignes de manifeste sur stdout (section à mettre en fin de manifeste après ENTETE), résumé sur stderr.
"""
import re
import struct
import sys

BASE = 0x82000000
GEN = "generated/default/"
ENTETE = "# Fonctions pointées par les données"


def main():
    args = [a for a in sys.argv[1:] if a != "--ecrire"]
    data = open(args[0] if args else "out/analyse/image.bin", "rb").read()
    reg = sorted(int(m, 16) for m in re.findall(r"SetFunction\(0x([0-9A-Fa-f]+),",
                                                open(GEN + "opdarkness_register.cpp").read()))
    # Les entrées de la section produite par ce script (fin du manifeste) ne comptent pas comme connues : la
    # section est régénérée en entier à chaque passage.
    manifeste = open("opdarkness_manifest.toml").read()
    section = manifeste[manifeste.index(ENTETE):] if ENTETE in manifeste else ""
    propres = set(int(m, 16) for m in re.findall(r"^0x([0-9A-Fa-f]+)\s*=", section, re.M))
    reg = [a for a in reg if a not in propres]
    regset = set(reg)
    code_lo, code_hi = reg[0], reg[-1] + 0x1000
    word = lambda a: struct.unpack_from(">I", data, a - BASE)[0]

    def fin_de_fonction(a):
        """L'instruction avant a termine-t-elle une fonction ? Un « b » court qui saute par-dessus a est un saut
        interne (a est alors une case de switch, dont l'adresse est aussi dans une table de saut) : rejeté."""
        if code_lo <= word(a) < code_hi:
            return False  # a contient une adresse de code : table de saut placée dans le code
        ins = word(a - 4)
        if ins == 0x4E800420:
            # bctr d'un switch : le registre passé à mtctr vient d'un lwzx ou d'un add (adresse indexée), et a est une
            # case ou la table. Un appel virtuel (lwz depuis la vtable) termine bien la fonction.
            rs = None
            for b in range(a - 8, a - 40, -4):
                i = word(b)
                if rs is None:
                    if i >> 26 == 31 and (i >> 1) & 0x3FF == 467 and (i >> 11) & 0x3FF == 0x120:  # mtctr rS
                        rs = (i >> 21) & 31
                    continue
                if (i >> 21) & 31 == rs and i >> 26 in (31, 32, 14, 15):  # dernière écriture de rS
                    return not (i >> 26 == 31 and (i >> 1) & 0x3FF in (23, 266))
            return True
        if ins in (0x4E800020, 0):
            return True
        if ins >> 26 == 18 and ins & 3 == 0:
            d = ins & 0x03FFFFFC
            cible = a - 4 + (d - 0x04000000 if d & 0x02000000 else d)
            return not (a <= cible < a + 0x1000)
        return False

    cibles = set()
    for off in range(0, len(data) - 3, 4):
        a = BASE + off
        if code_lo <= a < code_hi:
            continue
        v = struct.unpack_from(">I", data, off)[0]
        if code_lo <= v < code_hi and v % 4 == 0 and v not in regset and fin_de_fonction(v):
            cibles.add(v)
    # Adresses calculées dans le code (rappels, points d'entrée de threads) : « lis rX,haut » puis
    # « addi rY,rX,bas » quelques instructions plus loin.
    for a in range(code_lo, min(code_hi, BASE + len(data)), 4):
        ins = word(a)
        if ins >> 26 != 14 or (ins >> 16) & 31 == 0:
            continue
        ra = (ins >> 16) & 31
        bas = ins & 0xFFFF
        bas -= 0x10000 if bas & 0x8000 else 0
        for b in range(a - 4, max(a - 40, code_lo), -4):
            p = word(b)
            if p >> 26 == 15 and (p >> 21) & 31 == ra and (p >> 16) & 31 == 0:
                v = (((p & 0xFFFF) << 16) + bas) & 0xFFFFFFFF
                if code_lo <= v < code_hi and v % 4 == 0 and v not in regset and fin_de_fonction(v):
                    cibles.add(v)
                break
    import bisect
    bornes = sorted(regset | cibles)
    lignes = []
    for v in sorted(cibles):
        # fin : premier blr / bctr / b en ligne droite, sans dépasser la fonction suivante
        # fonction générée ou candidat suivant
        i = bisect.bisect_right(bornes, v)
        suivante = bornes[i] if i < len(bornes) else code_hi
        fin = None
        loin = v  # cible interne la plus lointaine : la fin est forcément après
        a = v
        while a < suivante and a < v + 0x400:
            ins = word(a)
            op = ins >> 26
            cible = None
            if op == 18 and ins & 1 == 0:  # b / ba (pas bl)
                d = ins & 0x03FFFFFC
                cible = (d - 0x04000000 if d & 0x02000000 else d) + (0 if ins & 2 else a)
            elif op == 16 and ins & 1 == 0:  # bc (pas bcl)
                d = ins & 0xFFFC
                cible = (d - 0x10000 if d & 0x8000 else d) + (0 if ins & 2 else a)
            if cible is not None and v <= cible < suivante:
                loin = max(loin, cible)
            terminale = ins in (0x4E800020, 0x4E800420) or (op == 18 and ins & 1 == 0 and not (v <= cible < suivante))
            if terminale and a >= loin:
                fin = a + 4
                break
            a += 4
        lignes.append(f"0x{v:08X} = {{ end = 0x{fin:08X} }}" if fin else f"0x{v:08X} = {{}}")
    print(f"{len(cibles)} candidats", file=sys.stderr)
    if "--ecrire" in sys.argv:
        debut = manifeste[:manifeste.index(ENTETE)] if ENTETE in manifeste else manifeste.rstrip("\n") + "\n"
        with open("opdarkness_manifest.toml", "w") as f:
            f.write(debut + ENTETE + " et absentes de l'analyse : tools/candidats.py\n" + "\n".join(lignes) + "\n")
    else:
        print("\n".join(lignes))


if __name__ == "__main__":
    main()

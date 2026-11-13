#!/usr/bin/env python3
"""Classe les fonctions du code généré par le travail compté dans le profil PGO (out/pgo/*.gcda).

Le .nro instrumenté est compilé sans .gcno : les compteurs n'ont pas de nom. GCC repère chaque fonction par
lineno_checksum = CRC32 (polynôme 0x04c11db7, bit de poids fort d'abord, NUL compris) de la ligne de sa définition,
puis du chemin absolu du fichier, puis de son nom d'assembleur (__imp__sub_XXXXXXXX) : recalculé ici pour chaque
DEFINE_REX_FUNC du code généré. Le « travail » est la somme des compteurs d'arcs de la fonction (branches du code C++
généré, dont les tests ajoutés par la traduction : lvrx, cas particuliers des flottants) : un ordre de grandeur, pas
un temps ; à croiser avec le profileur par échantillons de la console.
Les copies localisées (generated/hot/n_<ADDR>.h, compilées dans src/od_copies.cpp) sont nommées « copie n_<ADDR> »
(même somme : ligne de Native, chemin absolu de l'en-tête, nom C++ sa2::hot::n_<ADDR>::Native) ; le reste des autres
fichiers (code natif du projet, rendu, SDK) compte en un total par fichier, « [fichier] ... ».

  python3 tools/pgo_classement.py [nombre]
"""
import glob
import os
import re
import struct
import sys


def crc_octet(c, b):
    v = (b & 0xFF) << 24
    for _ in range(8):
        retour = 0x04C11DB7 if ((v ^ c) & 0x80000000) else 0
        c = ((c << 1) & 0xFFFFFFFF) ^ retour
        v = (v << 1) & 0xFFFFFFFF
    return c


def crc_chaine(c, s):
    for o in s.encode() + b'\0':
        c = crc_octet(c, o)
    return c


def somme_lignes(ligne, fichier, nom):
    return crc_chaine(crc_chaine(ligne, fichier), nom)


def lire_gcda(chemin):
    d = open(chemin, 'rb').read()
    pos, fonctions, courante = 16, [], None
    while pos + 8 <= len(d):
        etiquette, longueur = struct.unpack_from('<Ii', d, pos)
        pos += 8
        if longueur < 0:  # compteurs tous nuls
            continue
        if etiquette == 0x01000000:
            _, lignes, _ = struct.unpack_from('<III', d, pos)
            courante = {'lignes': lignes, 'arcs': []}
            fonctions.append(courante)
        elif etiquette == 0x01A10000 and courante is not None:
            courante['arcs'] = list(struct.unpack_from('<%dQ' % (longueur // 8), d, pos))
        pos += longueur
    return fonctions


def main():
    nombre = int(sys.argv[1]) if len(sys.argv) > 1 else 60
    racine = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    resultats, inconnues = [], 0
    for cpp in sorted(glob.glob(os.path.join(racine, 'generated/default/opdarkness_recomp.*.cpp'))):
        gcda = os.path.join(racine, 'out/pgo', 'CMakeFiles#opdarkness_recomp.dir#generated#default#'
                            + os.path.basename(cpp) + '.gcda')
        if not os.path.exists(gcda):
            continue
        lignes = open(cpp).read().split('\n')
        definitions = [(i + 1, m.group(1)) for i, l in enumerate(lignes)
                       for m in [re.match(r'DEFINE_REX_FUNC\((\w+)\)', l)] if m]
        tailles = {nom: (definitions[k + 1][0] if k + 1 < len(definitions) else len(lignes)) - ligne
                   for k, (ligne, nom) in enumerate(definitions)}
        par_somme = {somme_lignes(ligne, cpp, '__imp__' + nom): nom for ligne, nom in definitions}
        for f in lire_gcda(gcda):
            nom = par_somme.get(f['lignes'])
            if not nom:
                inconnues += 1
                continue
            resultats.append((sum(f['arcs']), max(f['arcs'], default=0), nom, tailles[nom]))
    # Copies localisées (dans od_copies.cpp) et totaux des autres fichiers profilés.
    deja = set()
    copies_gcda = os.path.join(racine, 'out/pgo', 'CMakeFiles#opdarkness.dir#src#od_copies.cpp.gcda')
    if os.path.exists(copies_gcda):
        fonctions = lire_gcda(copies_gcda)
        par_somme = {}
        for h in glob.glob(os.path.join(racine, 'generated/hot/n_*.h')):
            nom = os.path.basename(h)[:-2]
            lignes = open(h).read().split('\n')
            ligne = next((i + 1 for i, l in enumerate(lignes) if 'inline void Native(' in l), None)
            if ligne:
                mangle = '_ZN3sa23hot%d%s6NativeER10PPCContextPh' % (len(nom), nom)
                par_somme[somme_lignes(ligne, h, mangle)] = 'copie ' + nom
        for f in fonctions:
            nom = par_somme.get(f['lignes'])
            if nom:
                resultats.append((sum(f['arcs']), max(f['arcs'], default=0), nom, 0))
                deja.add(id(f))
        reste = sum(sum(f['arcs']) for f in fonctions if id(f) not in deja)
        resultats.append((reste, 0, '[fichier] src/od_copies.cpp (hors copies)', 0))
    for g in glob.glob(os.path.join(racine, 'out/pgo', '*.gcda')):
        base_nom = os.path.basename(g)
        if '#opdarkness_recomp.dir#' in base_nom or base_nom.endswith('src#od_copies.cpp.gcda'):
            continue
        source = base_nom.split('.dir#', 1)[-1][:-len('.gcda')].replace('#', '/')
        resultats.append((sum(sum(f['arcs']) for f in lire_gcda(g)), 0, '[fichier] ' + source, 0))
    total = sum(r[0] for r in resultats) or 1
    resultats.sort(reverse=True)
    print(f'{len(resultats)} entrées (fonctions du jeu, copies, fichiers), {inconnues} fonctions du code généré sans nom ; arcs {total:.3e}')
    cumul = 0
    print(' rang  part  cumul         arcs     max-arc  lignes  fonction')
    for i, (s, mx, nom, taille) in enumerate(resultats[:nombre]):
        cumul += s
        print(f'{i + 1:5d} {100 * s / total:5.2f} {100 * cumul / total:6.2f} {s:12d} {mx:11d} {taille or "":>7}  {nom}')
    for k in (10, 50, 100, 500):
        print(f'les {k} premières : {100 * sum(r[0] for r in resultats[:k]) / total:.1f} %')


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Extrait des entrées d'un paquet sa2_shaders.mesp en fichiers .odsh du cache de traduction (src/od_traducteur.cpp) :
sert à chercher quels shaders du paquet manquent au mode « sans paquet ».
  python3 tools/paquet_vers_cache.py <paquet.mesp> <dossier> [empreintes hexadécimales...]   (aucune = toutes)"""
import os, struct, sys

d = open(sys.argv[1], "rb").read()
assert d[:6] == b"MESSPV"
n = struct.unpack_from("<I", d, 12)[0]
voulues = {int(x, 16) for x in sys.argv[3:]}
os.makedirs(sys.argv[2], exist_ok=True)
p, ecrits = 24, 0
for _ in range(n):
    o, m, fp = struct.unpack_from("<IIQ", d, p)
    p += 16
    conteneur, spirv = d[p:p + o], d[p + o:p + o + 4 * m]
    p += o + 4 * m
    if voulues and fp not in voulues:
        continue
    nom = ("vs" if conteneur[3] & 1 else "ps") + "_%016x.odsh" % fp
    open(os.path.join(sys.argv[2], nom), "wb").write(b"ODSHAD01" + struct.pack("<II", o, m) + conteneur + spirv)
    ecrits += 1
print(f"{ecrits} entrées écrites dans {sys.argv[2]}")

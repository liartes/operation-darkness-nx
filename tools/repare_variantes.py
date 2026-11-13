#!/usr/bin/env python3
"""Répare les variantes de vertex shaders que Direct3D réécrit pour une déclaration de sommets (guide de sa2-nx §6.2,
« Vertex shaders Direct3D rewrites ») : chaque variante vidée par le rendu natif (--sa2_shader_misses -> variants/,
vs_from_n<N>_<clé>.bin) est le conteneur modèle avec le microcode tel que chargé. Le modèle est retrouvé parmi les
conteneurs (identique hors du microcode, même taille), puis repair_vertex_variant_declarations de sa2-nx (preuve
hors ligne, refus en cas de doute) écrit un conteneur réparé.

Usage : repare_variantes.py <dossier variants> <sortie> <dossiers de conteneurs...>
Outil : ~/workspace/sa2-nx/out/tools/repair_vertex_variant_declarations
  (clang++-20 -std=c++20 -O2 ~/workspace/sa2-nx/shaders/tools/repair_vertex_variant_declarations.cpp -o ...)
"""
import glob
import os
import struct
import subprocess
import sys

OUTIL = os.path.expanduser("~/workspace/sa2-nx/out/tools/repair_vertex_variant_declarations")


def zone_code(c):
    """(début, fin) du microcode dans le conteneur (même lecture que DumpLoadedVertexVariant)."""
    virtuel = struct.unpack_from(">I", c, 4)[0]
    entete = struct.unpack_from(">I", c, 24)[0]
    debut = virtuel + struct.unpack_from(">I", c, entete)[0]
    return debut, debut + struct.unpack_from(">I", c, entete + 4)[0]


def main():
    variantes, sortie, dossiers = sys.argv[1], sys.argv[2], sys.argv[3:]
    os.makedirs(sortie, exist_ok=True)
    modeles = {}
    for d in dossiers:
        for p in glob.glob(os.path.join(d, "vs_*.bin")):
            c = open(p, "rb").read()
            modeles.setdefault(len(c), []).append((p, c))
    ok = refus = sans = deja = 0
    for p in sorted(glob.glob(os.path.join(variantes, "vs_from_n*.bin"))):
        v = open(p, "rb").read()
        a, b = zone_code(v)
        trouve = [m for m, c in modeles.get(len(v), []) if c[:a] == v[:a] and c[b:] == v[b:]]
        if not trouve:
            print(f"{os.path.basename(p)} : modèle introuvable")
            sans += 1
            continue
        nom = "vs_r_" + os.path.basename(p).rsplit("_", 1)[1]  # clé seule : la même variante revient sous plusieurs n
        if os.path.exists(os.path.join(sortie, nom)):
            deja += 1
            continue
        r = subprocess.run([OUTIL, trouve[0], p, os.path.join(sortie, nom)], capture_output=True, text=True)
        if r.returncode == 0:
            ok += 1
            print(f"{os.path.basename(p)} : réparée depuis {os.path.basename(trouve[0])}")
        else:
            refus += 1
            print(f"{os.path.basename(p)} : refusée ({(r.stderr or r.stdout).strip()[:120]})")
    print(f"{ok} réparées, {refus} refusées, {sans} sans modèle, {deja} déjà réparées")


if __name__ == "__main__":
    main()

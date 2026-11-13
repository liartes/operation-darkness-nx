#!/usr/bin/env python3
"""Préparation du HLSL de XenosRecomp (avec shaders/shader_common_glslang.h) pour glslang, version Switch
(docs/SHADERS_EN_JEU.md) : stdin -> stdout. Porté à l'identique en C++ (src/od_prep_glslang.cpp).
  - __spirv__ défini (DXC le définit avec -spirv) ;
  - vk::RawBufferLoad<T>(...) -> ((T)0) : lectures par adresse 64 bits, que glslang ne connaît pas en HLSL ; toutes
    dans la branche « pointeur » de MASSEFFECT_UBO (fixé à 1), donc jamais prises ;
  - mots réservés de glslang employés comme noms : dword, texture ;
  - select(c, a, b) de HLSL 2021 -> (c ? a : b) (composante par composante en HLSL) ;
  - (1ull << n) (glslang refuse les entiers 64 bits en HLSL) -> (n < 32 ? (1u << n) : 0u) : même résultat une fois
    combiné à un mot de 32 bits (le seul emploi : swappedTexcoords & ...).
"""
import re, sys


def sans_lectures_brutes(t):
    sortie, i = [], 0
    motif = re.compile(r"vk::RawBufferLoad<(\w+)>\(")
    while True:
        m = motif.search(t, i)
        if not m:
            sortie.append(t[i:])
            return "".join(sortie)
        sortie.append(t[i:m.start()])
        j, niveau = m.end(), 1
        while niveau:
            niveau += {"(": 1, ")": -1}.get(t[j], 0)
            j += 1
        sortie.append(f"(({m.group(1)})0)")
        i = j


def preparer(t):
    t = sans_lectures_brutes(t)
    t = re.sub(r"\(1ull << (\w+)\)", r"(\1 < 32 ? (1u << \1) : 0u)", t)
    t = re.sub(r"\bdword\b", "od_dword", t)
    t = re.sub(r"\btexture\b", "od_texture", t)
    return "#define __spirv__ 1\n#define select(c, a, b) ((c) ? (a) : (b))\n" + t


if __name__ == "__main__":
    sys.stdout.write(preparer(sys.stdin.read()))

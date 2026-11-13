#!/usr/bin/env python3
"""Diagnostic (2026-10-21, traînées de fumée sur la console) : dans le GLSL des vertex shaders destinés à deko3d, une
position de sortie invalide (NaN ou infini) est remplacée par un point fixe au centre de l'écran. Si les traînées
partent alors du centre, les sommets sortent invalides du shader (le GPU Maxwell les trace, les cartes de PC et
l'émulateur les écartent).
Usage : od_vs_nan.py <dossier glsl> (modifie les vs_*.glsl sur place)"""
import glob, os, sys
LIGNE = ("    if (any(isnan(gl_Position)) || any(isinf(gl_Position))) { gl_Position = vec4(0.0, 0.0, 0.5, 1.0); }"
         "  // od_vs_nan\n")
n = 0
for f in sorted(glob.glob(os.path.join(sys.argv[1], 'vs_*.glsl'))):
    s = open(f).read()
    if 'od_vs_nan' in s or 'gl_Position' not in s:
        continue
    i = s.rstrip().rfind('}')
    s = s[:i] + LIGNE + s[i:]
    open(f, 'w').write(s)
    n += 1
print(f"od_vs_nan: {n} vertex shaders retouchés")

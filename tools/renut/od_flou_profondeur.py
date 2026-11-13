#!/usr/bin/env python3
"""Flou de profondeur de champ allégé pour le rendu reNut (2026-10-21), le même que od_flou_profondeur du rendu natif
(src/od_traducteur.cpp, docs/OPTIMISATION.md « Flou de profondeur de champ »).

Le pixel shader du jeu 08fb9abd43fe7c0c lit, pour 5 points (le pixel et 4 voisins écartés selon la profondeur), la
profondeur (s3), l'image nette (s1) et l'image floue (s2), mélange net -> flou selon |2 z - 1|, puis fait la moyenne
pondérée : 15 lectures de texture par pixel. Mode 1 : le mélange du seul point central (3 lectures ; même flou,
transitions plus franches au bord des objets). Mode 2 : comparaison, moitié gauche du jeu, moitié droite allégée,
trait jaune au milieu (1280 x 720).

Lancé entre les deux passages de XenosRecomp (gpu.cmake, après tools/renut/renut_early_alpha.py), sur le HLSL du
dossier BANJO_HLSL_OVERRIDE. Le HLSL de reNut est celui du rendu natif sans l'argument de taille des tfetch2D.
Usage : od_flou_profondeur.py <dossier hlsl> <mode 0|1|2>
"""
import os, sys

NOM = "ps_08fb9abd43fe7c0c.hlsl"
DEBUT = "\tr6.y = tfetch2D(nSampler3_Texture2DDescriptorIndex"
FIN = "\toC0.w = 1.0;"


def lecture(s, coord, champs):
    return (f"tfetch2D(nSampler{s}_Texture2DDescriptorIndex, nSampler{s}_SamplerDescriptorIndex, {coord}, "
            f"float2(0, 0)).{champs}")


def main():
    dossier, mode = sys.argv[1], int(sys.argv[2])
    chemin = os.path.join(dossier, NOM)
    if mode == 0:
        return 0
    if not os.path.exists(chemin):
        print(f"od_flou_profondeur: {NOM} absent, rien à faire")
        return 0
    hlsl = open(chemin).read()
    debut, fin = hlsl.find(DEBUT), hlsl.find(FIN)
    if debut < 0 or fin < 0 or fin < debut:
        print(f"od_flou_profondeur: {NOM} n'a pas la forme attendue, laissé tel quel", file=sys.stderr)
        return 1
    if mode == 2:
        ajout = ("\t[branch] if (iPos.x >= 640.0) {\n"
                 f"\t\tfloat zc = {lecture(3, 'iTexCoord0.xy', 'x')};\n"
                 f"\t\tfloat3 bc = {lecture(2, 'iTexCoord0.xy', 'xyz')};\n"
                 f"\t\tfloat3 sc = {lecture(1, 'iTexCoord0.xy', 'xyz')};\n"
                 "\t\toC0.xyz = (bc * c252.yyy + -sc) * abs(zc * 2.0 + -1.0) + sc;\n"
                 "\t}\n"
                 "\tif (abs(iPos.x - 640.0) < 1.0) oC0.xyz = float3(1.0, 0.85, 0.0);\n")
        hlsl = hlsl[:fin] + ajout + hlsl[fin:]
    else:
        corps = (f"\tr6.y = {lecture(3, 'r0.xy', 'x')};\n"
                 f"\tr4.xyz = {lecture(2, 'r0.xy', 'xyz')};\n"
                 f"\tr8.xyz = {lecture(1, 'r0.xy', 'xyz')};\n"
                 "\toC0.xyz = (r4.xyz * c252.yyy + -r8.xyz) * abs(r6.y * 2.0 + -1.0) + r8.xyz;\n")
        hlsl = hlsl[:debut] + corps + hlsl[fin:]
    open(chemin, "w").write(hlsl)
    print(f"od_flou_profondeur: {NOM} remplacé (mode {mode})")
    return 0


if __name__ == "__main__":
    sys.exit(main())

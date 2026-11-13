#!/usr/bin/env python3
"""Ombres portées découpées à la zone de la carte d'ombres (rendu reNut, 2026-10-22).

Les ombres portées d'Operation Darkness redessinent des morceaux de terrain, un dessin par personnage, avec un pixel
shader qui assombrit l'image (mélange dst * (1 - couleur)) : PS 4C5CFD54864D90BF (VS E45C1440418E62B8) et
EC78065CF82CFB45 (VS A75DAE485A8C8B7B). Le vertex shader écrit en TEXCOORD2 les coordonnées projetées de la carte
d'ombres (mtxShadowProj) ; le pixel shader rend 0 (aucun effet) si z <= 0, et lit la carte en xy / z sinon, en
« clamp to edge ». En combat, 98 % des pixels tramés par ces dessins ne changeaient rien (RenderDoc, captures du
2026-10-22) et ils faisaient 60 % du temps GPU de la console.

Ce script ajoute aux deux vertex shaders des distances de découpe (SV_ClipDistance) : z, x + m, z + m - x, y + m,
z + m - y avec m = 0,03 z, soit -0,03 <= xy / z <= 1,03 devant le projecteur (la marge garde le liseré du filtrage
bilinéaire au bord de la carte, environ 2 texels d'une carte de 64). Le GPU ne trame plus que la zone de la carte. Actives seulement quand la
constante partagée 1352 vaut 1 (reNut : od_renut_ombres_decoupe, et seulement pour ces deux pixel shaders) ; sinon
les distances valent 1 (rien n'est découpé).

Écart avec le rendu sans découpe (captures RenderDoc consécutives, 2026-10-22, sans la marge) : 12 800 pixels
assombris en plus, de 1/255 pour 85 % d'entre eux (traînées du bord « clamp » des cartes au bord pas tout à fait
noir, le mode du jeu), 16/255 au plus : invisible.

Les 8 distances sont écrites (les 3 dernières à 1, rien de découpé) : deko3d les active toutes les 8
(ClipDistanceEnable = 0xFF, gpu_3d_base.cpp) ; avec 5 seulement, les 3 autres prenaient sur la console des valeurs
indéterminées et des triangles d'ombre disparaissaient au hasard (scintillement, 2026-10-22 ; ni le PC ni Ryujinx).

Mode 2 (2026-10-22) : la découpe par distances (mode 1) fait scintiller les ombres sur la console (deko3d, même avec
les 8 distances écrites ; ni le PC ni Ryujinx). Le mode 2 laisse les vertex shaders tels quels et fait s'arrêter
(discard) les deux pixel shaders hors de la même zone, avant toute lecture de texture : même image, moins de gain
(les pixels sont encore tramés).

Lancé entre les deux passages de XenosRecomp (gpu.cmake), sur le HLSL du dossier BANJO_HLSL_OVERRIDE (vertex shaders
vidés grâce à OD_HLSL_DUMP_VS). Usage : od_ombres.py <dossier hlsl>
"""
import os, sys

NOMS = ["vs_a75dae485a8c8b7b.hlsl", "vs_e45c1440418e62b8.hlsl"]
NOMS_PS = ["ps_4c5cfd54864d90bf.hlsl", "ps_ec78065cf82cfb45.hlsl"]
DEBUT_PS = "\tCubeMapData cubeMapData = (CubeMapData)0;\n"
AJOUT_PS = ("\t// opdarkness (tools/renut/od_ombres.py) : hors de la zone de la carte d'ombres, rien à assombrir (mode 2 de\n"
            "\t// od_renut_ombres_decoupe) : arrêt avant toute lecture de texture.\n"
            "\tif (RENUT_SHARED_UINT(1352) == 2u)\n"
            "\t{\n"
            "\t\tconst float4 t = iTexCoord2;\n"
            "\t\tconst float m = 0.03 * t.z;\n"
            "\t\tif (t.z <= 0.0 || t.x < -m || t.y < -m || t.x > t.z + m || t.y > t.z + m)\n"
            "\t\t\tdiscard;\n"
            "\t}\n")
SORTIE = "\tout float oPointSize : POINTSIZE)"
FIN = "\toPointSize = oPts.z != 0.0 ? -1.0 : oPts.x;\n\treturn;\n}"
AJOUT = ("\t// opdarkness (tools/renut/od_ombres.py) : découpe à la zone de la carte d'ombres (od_renut_ombres_decoupe).\n"
         "\tif (RENUT_SHARED_UINT(1352) == 1u)\n"
         "\t{\n"
         "\t\tconst float m = 0.03 * oTexCoord2.z;  // marge : le filtrage bilinéaire au bord de la carte\n"
         "\t\toOdDecoupe0 = float4(oTexCoord2.z, oTexCoord2.x + m, oTexCoord2.z + m - oTexCoord2.x, oTexCoord2.y + m);\n"
         "\t\toOdDecoupe1 = float4(oTexCoord2.z + m - oTexCoord2.y, 1.0, 1.0, 1.0);\n"
         "\t}\n"
         "\telse\n"
         "\t{\n"
         "\t\toOdDecoupe0 = 1.0;\n"
         "\t\toOdDecoupe1 = 1.0;\n"
         "\t}\n")


def main():
    dossier = sys.argv[1]
    for nom in NOMS:
        chemin = os.path.join(dossier, nom)
        if not os.path.exists(chemin):
            print(f"od_ombres: {nom} absent, rien à faire")
            continue
        hlsl = open(chemin).read()
        if "oOdDecoupe0" in hlsl:
            continue
        corps = hlsl[hlsl.find("\nvoid main("):]
        if hlsl.count(SORTIE) != 1 or hlsl.count(FIN) != 1 or corps.count("return;") != 1:
            print(f"od_ombres: {nom} n'a pas la forme attendue, laissé tel quel", file=sys.stderr)
            continue
        hlsl = hlsl.replace(SORTIE, "\tout float oPointSize : POINTSIZE,\n"
                                    "\tout float4 oOdDecoupe0 : SV_ClipDistance0,\n"
                                    "\tout float4 oOdDecoupe1 : SV_ClipDistance1)")
        hlsl = hlsl.replace(FIN, AJOUT + FIN)
        open(chemin, "w").write(hlsl)
        print(f"od_ombres: {nom} découpé à la zone de la carte d'ombres")
    for nom in NOMS_PS:
        chemin = os.path.join(dossier, nom)
        if not os.path.exists(chemin):
            print(f"od_ombres: {nom} absent, rien à faire")
            continue
        hlsl = open(chemin).read()
        if "od_ombres.py" in hlsl:
            continue
        if hlsl.count(DEBUT_PS) != 1 or "iTexCoord2" not in hlsl:
            print(f"od_ombres: {nom} n'a pas la forme attendue, laissé tel quel", file=sys.stderr)
            continue
        hlsl = hlsl.replace(DEBUT_PS, DEBUT_PS + AJOUT_PS)
        open(chemin, "w").write(hlsl)
        print(f"od_ombres: {nom} arrêté hors de la zone de la carte d'ombres (mode 2)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

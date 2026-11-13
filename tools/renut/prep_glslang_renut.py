#!/usr/bin/env python3
"""Préparation du HLSL de reNut (XenosRecomp, prélude shader_common.h) pour glslang, en vue de la traduction des shaders
sur la console (deko3d ; 2026-10-22). stdin -> stdout. Reprend tools/prep_glslang.py du dépôt principal (rendu natif),
plus ce que le HLSL de reNut demande :
  - les fonctions qui renvoient une texture (getTexture2D, getTexture3D, getStackedTexture...) deviennent des macros :
    glslang plante sur un objet texture rendu par une fonction (handleSamplerTextureCombine) ; le reste (indexation des
    tableaux, variables locales, paramètres) passe ;
  - and(a, b) / or(a, b) de HLSL 2021 -> && / || ;
  - « const » des variables locales (indentées) retiré : glslang refuse une constante locale calculée à partir d'une
    constante de spécialisation (g_banjoTrimPoison...) ; sans effet sur le calcul ;
  - « $ » des noms de constantes (shaders compilés par D3DX : $diffuseMap, $matWVP) -> od_S_ : DXC l'accepte dans un
    identifiant, glslang non (2026-10-23).
"""
import re
import sys

sys.path.insert(0, __import__('os').path.expanduser('~/workspace/operation-darkness-nx/tools'))
import prep_glslang as base  # noqa: E402

RENVOI_TEXTURE = re.compile(
    r'^((?:RW)?Texture\w*<[\w ,]+>|SamplerState)\s+(\w+)\(([^)]*)\)\s*\{\s*return\s+(.*?);\s*\}', re.M | re.S)


def macros_textures(t):
    def remplace(m):
        params = [p.strip().split()[-1] for p in m.group(3).split(',') if p.strip()]
        corps = ' '.join(m.group(4).split())
        return f'#define {m.group(2)}({", ".join(params)}) {corps}'
    return RENVOI_TEXTURE.sub(remplace, t)


CONST_LOCAL = re.compile(r'^([ \t]+)const (?=\w)', re.M)


def preparer(t):
    # and() / or() de HLSL 2021 (logiques, composante par composante) : && et || le sont en HLSL d'avant 2021.
    return ('#define and(a, b) ((a) && (b))\n#define or(a, b) ((a) || (b))\n' +
            base.preparer(CONST_LOCAL.sub(r'\1', macros_textures(t.replace('$', 'od_S_')))))


if __name__ == '__main__':
    sys.stdout.write(preparer(sys.stdin.read()))

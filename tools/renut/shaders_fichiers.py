#!/usr/bin/env python3
"""Tous les conteneurs de shaders lisibles dans les fichiers du jeu, pour les traduire d'avance (reNut / deko3d,
2026-10-22) : la recherche de sa2-nx (tools/sa2_dump_shaders.py d'origin/main, vérification des conteneurs de reNut)
sur l'image de l'exécutable et sur assets/, les .igb compressés (XMemCompress) décompressés d'abord
(tools/renut/xmem_decompresser.c, compilé dans out/ au premier appel).

    python3 tools/renut/shaders_fichiers.py [--sortie out/renut_shader_dump]

Écrit <sortie>/{vs,ps}_<xxh3 du conteneur>.bin (noms de tools/dump_shaders.py ; ceux qui y sont déjà sont gardés) et
<sortie>/fichiers.csv (d'où vient chaque shader). Ne trouve pas ceux que le jeu fabrique pendant la partie (modèles de
HLSL de l'exécutable compilés par son D3DX, shaders des fichiers retouchés de quelques octets) : ceux-là restent à
capturer (renut_shader_capture_dir). Données du jeu : out/ seulement.
"""
import argparse
import ctypes
import os
import re
import struct
import subprocess
import sys
import tempfile

RACINE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.realpath(__file__))))
SA2 = os.path.expanduser('~/workspace/sa2-nx')
MAGIE = re.compile(rb'\x10\x2a\x11[\x00\x01]')

_xxh = ctypes.CDLL('libxxhash.so.0')
_xxh.XXH3_64bits.restype = ctypes.c_uint64
_xxh.XXH3_64bits.argtypes = [ctypes.c_char_p, ctypes.c_size_t]


def conteneurs(data):
    """La vérification de reNut (tools/dump_shaders.py, reprise par sa2-nx) : tailles, tables et partie physique."""
    for m in MAGIE.finditer(data):
        o = m.start()
        if o % 4 or o + 0x24 > len(data):
            continue
        virt, phys = struct.unpack_from('>II', data, o + 4)
        ct, dt, so = struct.unpack_from('>III', data, o + 0x10)
        if not (0x24 <= virt < 0x10000 and 0 < phys < 0x100000 and o + virt + phys <= len(data)):
            continue
        if ct >= virt or dt >= virt or not (0x24 <= so < virt - 8):
            continue
        poff, psize = struct.unpack_from('>II', data, o + so)
        if poff + psize != phys:
            continue
        flags = struct.unpack_from('>I', data, o)[0]
        blob = data[o:o + virt + phys]
        yield ('vs' if flags & 1 else 'ps') + '_%016x' % _xxh.XXH3_64bits(blob, len(blob)), blob


def decompresseur():
    exe = os.path.join(RACINE, 'out', 'outils', 'xmem_decompresser')
    src = os.path.join(RACINE, 'tools', 'renut', 'xmem_decompresser.c')
    if not os.path.isfile(exe) or os.path.getmtime(exe) < os.path.getmtime(src):
        m = os.path.join(SA2, 'sdk', 'thirdparty', 'libmspack', 'libmspack', 'mspack')
        os.makedirs(os.path.dirname(exe), exist_ok=True)
        subprocess.run(['cc', '-O2', '-w', '-I' + m, '-o', exe, src, os.path.join(m, 'lzxd.c'),
                        os.path.join(m, 'system.c')], check=True)
    return exe


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--image', default=os.path.join(RACINE, 'out', 'analyse', 'image.bin'))
    ap.add_argument('--assets', default=os.path.join(RACINE, 'assets'))
    ap.add_argument('--sortie', default=os.path.join(RACINE, 'out', 'renut_shader_dump'))
    a = ap.parse_args()
    if not os.path.isfile(a.image):
        a.image = os.path.expanduser('~/workspace/operation-darkness-nx/out/analyse/image.bin')
    exe = decompresseur()
    os.makedirs(a.sortie, exist_ok=True)
    deja = set(f[:-4] for f in os.listdir(a.sortie) if f.endswith('.bin'))
    origine = {}
    compresses = 0

    def lire(data, nom):
        for n, blob in conteneurs(data):
            if n in origine:
                continue
            origine[n] = nom
            if n not in deja:
                with open(os.path.join(a.sortie, n + '.bin'), 'wb') as f:
                    f.write(blob)

    lire(open(a.image, 'rb').read(), 'default.xex')
    with tempfile.TemporaryDirectory() as tmp:
        sortie = os.path.join(tmp, 'd.bin')
        for d, _, fs in sorted(os.walk(a.assets)):
            for f in sorted(fs):
                p = os.path.join(d, f)
                rel = os.path.relpath(p, a.assets)
                with open(p, 'rb') as h:
                    tete = h.read(4)
                if tete == b'\x0f\xf5\x12\xee':
                    compresses += 1
                    if subprocess.run([exe, p, sortie]).returncode:
                        print('échec de la décompression :', rel, file=sys.stderr)
                        continue
                    data = open(sortie, 'rb').read()
                else:
                    data = open(p, 'rb').read()
                    if b'\x10\x2a\x11' not in data:
                        continue
                lire(data, rel)
    with open(os.path.join(a.sortie, 'fichiers.csv'), 'w') as f:
        f.write('shader,fichier\n')
        for n in sorted(origine):
            f.write(f'{n},{origine[n]}\n')
    nouveaux = len(set(origine) - deja)
    print(f'{compresses} fichiers compressés lus ; {len(origine)} shaders dans les fichiers '
          f'({sum(1 for n in origine if n[:2] == "vs")} vs), {nouveaux} nouveaux dans {a.sortie} ; '
          f'{len(deja - set(origine))} des {len(deja)} déjà là ne sont dans aucun fichier')


if __name__ == '__main__':
    main()

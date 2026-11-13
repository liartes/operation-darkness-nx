#!/usr/bin/env python3
"""Convertit une capture xwd (ZPixmap 24/32 bits) en PNG. Usage : xwd2png.py <entrée.xwd> <sortie.png>"""
import struct
import sys

from PIL import Image

d = open(sys.argv[1], "rb").read()
h = struct.unpack(">25I", d[:100])
taille_entete, largeur, hauteur = h[0], h[4], h[5]
ordre, bpp, ligne, ncouleurs = h[7], h[11], h[12], h[19]
pixels = d[taille_entete + ncouleurs * 12:]
mode = "BGRX" if ordre == 0 else "XRGB"
if bpp != 32:
    raise SystemExit(f"{bpp} bits par pixel non pris en charge")
Image.frombuffer("RGB", (largeur, hauteur), pixels, "raw", mode, ligne, 1).save(sys.argv[2])

#!/usr/bin/env python3
"""Icône du .nro (JPEG 256x256, format demandé par nacptool/elf2nro) à partir de nro-icon.png à la racine du dépôt
(jaquette fournie par l'utilisateur, non versionnée : .gitignore). Cadrage : le carré du bas de l'image (personnages
et logo). Usage : tools/switch/icone.py <nro-icon.png> <sortie.jpg>"""
import sys
from PIL import Image

source, sortie = sys.argv[1], sys.argv[2]
im = Image.open(source).convert("RGB")
w, h = im.size
cote = min(w, h)
haut = h - cote  # le carré du bas (le logo est en bas de la jaquette)
im.crop((0, haut, cote, haut + cote)).resize((256, 256), Image.LANCZOS).save(sortie, "JPEG", quality=92)

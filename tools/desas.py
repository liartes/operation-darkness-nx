#!/usr/bin/env python3
"""Désassemble une zone de l'image invitée (out/analyse/image.bin, base 0x82000000) avec llvm-mc-20.

Usage : desas.py <début> <fin|+taille> [image]
L'image se produit avec tools/dump_image.sh.
"""
import subprocess
import sys

BASE = 0x82000000


def main():
    start = int(sys.argv[1], 16)
    end = sys.argv[2]
    end = start + int(end[1:], 16) if end.startswith("+") else int(end, 16)
    image = sys.argv[3] if len(sys.argv) > 3 else "out/analyse/image.bin"
    data = open(image, "rb").read()[start - BASE:end - BASE]
    words = [data[i:i + 4] for i in range(0, len(data), 4)]
    src = "\n".join(" ".join(f"0x{b:02x}" for b in w) for w in words)
    res = subprocess.run(["llvm-mc-20", "--disassemble", "-triple=powerpc-unknown-unknown", "-mcpu=pwr7"],
                         input=src, capture_output=True, text=True)
    lines = [l.strip() for l in res.stdout.splitlines() if l.strip() and not l.strip().startswith(".text")]
    # llvm-mc saute les mots invalides (avertissement sur stderr) : on réaligne en le signalant.
    if len(lines) != len(words):
        lines = []
        for w in words:
            r = subprocess.run(["llvm-mc-20", "--disassemble", "-triple=powerpc-unknown-unknown", "-mcpu=pwr7"],
                               input=" ".join(f"0x{b:02x}" for b in w), capture_output=True, text=True)
            out = [l.strip() for l in r.stdout.splitlines() if l.strip() and not l.strip().startswith(".text")]
            lines.append(out[0] if out else ".long 0x" + w.hex())
    for i, (w, l) in enumerate(zip(words, lines)):
        print(f"{start + 4 * i:08X}  {w.hex()}  {l}")


if __name__ == "__main__":
    main()

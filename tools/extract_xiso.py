#!/usr/bin/env python3
"""Extracts the files of an Xbox 360 disc image (XDVDFS, XGD1/XGD2/XGD3), with no dependency.

Usage: extract_xiso.py <image.iso> <output folder> [--list]
"""
import os
import struct
import sys

SECTOR = 2048
MAGIC = b"MICROSOFT*XBOX*MEDIA"
# Décalages connus de la partition du jeu : XISO « nue », XGD2, XGD1, XGD3.
PARTITION_OFFSETS = [0, 0x0FD90000, 0x18300000, 0x02080000]


def find_partition(f):
    for off in PARTITION_OFFSETS:
        f.seek(off + 32 * SECTOR)
        if f.read(len(MAGIC)) == MAGIC:
            return off
    raise SystemExit("No XDVDFS volume found in this image")


def walk(f, base, sector, size, path, out):
    """Parcourt un répertoire XDVDFS (arbre binaire stocké à plat, lu séquentiellement)."""
    if size == 0:
        return
    f.seek(base + sector * SECTOR)
    data = f.read(size)
    pos = 0
    while pos + 14 <= len(data):
        left, = struct.unpack_from("<H", data, pos)
        if left == 0xFFFF:  # remplissage jusqu'au secteur suivant
            pos = (pos // SECTOR + 1) * SECTOR
            continue
        _, sec, fsize, attr, nlen = struct.unpack_from("<HIIBB", data, pos + 2)
        name = data[pos + 14:pos + 14 + nlen].decode("latin-1")
        pos = (pos + 14 + nlen + 3) & ~3
        rel = os.path.join(path, name)
        if attr & 0x10:
            out.append((rel, None, None))
            walk(f, base, sec, fsize, rel, out)
        else:
            out.append((rel, sec, fsize))


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    iso, dest = sys.argv[1], sys.argv[2]
    only_list = "--list" in sys.argv
    with open(iso, "rb") as f:
        base = find_partition(f)
        f.seek(base + 32 * SECTOR + 20)
        root_sec, root_size = struct.unpack("<II", f.read(8))
        entries = []
        walk(f, base, root_sec, root_size, "", entries)
        total = 0
        for rel, sec, size in entries:
            target = os.path.join(dest, rel)
            if sec is None:
                if not only_list:
                    os.makedirs(target, exist_ok=True)
                continue
            total += size
            if only_list:
                print(f"{size:12d}  {rel}")
                continue
            os.makedirs(os.path.dirname(target) or ".", exist_ok=True)
            f.seek(base + sec * SECTOR)
            with open(target, "wb") as o:
                left = size
                while left:
                    chunk = f.read(min(left, 1 << 22))
                    o.write(chunk)
                    left -= len(chunk)
        print(f"partition 0x{base:X}, {sum(1 for e in entries if e[1] is not None)} files, {total} bytes",
              file=sys.stderr)


if __name__ == "__main__":
    main()

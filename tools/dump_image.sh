#!/bin/sh
# Extrait l'image invitée décodée de default.xex (déchiffrée, décompressée) dans out/analyse/image.bin
# (base 0x82000000) : le générateur rexglue la charge à 0x182000000 ; on la copie sous gdb juste avant qu'il libère
# l'arène mémoire. Sert à tools/desas.py (examen des erreurs d'analyse du manifeste).
set -e
cd "$(dirname "$0")/.."
SDK="${REXSDK:-$HOME/rexglue-sdk-0.10.0-linux-amd64/linux-amd64}"
TAILLE=$(python3 -I -c "import struct;d=open('assets/default.xex','rb').read();o=struct.unpack('>I',d[16:20])[0];print(hex(0x182000000+struct.unpack('>I',d[o+4:o+8])[0]))")
mkdir -p out/analyse
gdb -q -batch -ex 'set debuginfod enabled off' -ex 'catch syscall munmap' -ex 'condition 1 $rsi > 0x10000000' -ex run \
  -ex "dump binary memory out/analyse/image.bin 0x182000000 $TAILLE" -ex kill \
  --args "$SDK/bin/rexglue" codegen opdarkness_manifest.toml > out/analyse/dump.log 2>&1
[ -s out/analyse/image.bin ] && echo "out/analyse/image.bin ($TAILLE)" || { echo "échec, voir out/analyse/dump.log"; exit 1; }

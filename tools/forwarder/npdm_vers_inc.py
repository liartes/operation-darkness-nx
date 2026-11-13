#!/usr/bin/env python3
"""opdarkness (2026-11-01) : main.npdm (npdmtool od_forwarder.json main.npdm, conteneur devkitpro/devkita64) ->
~/workspace/sa2-nx/sdk/src/core/forwarder_npdm.inc (RexSwitchEnsureForwarderNpdm y écrit l'identifiant du programme).
Forme reprise de renut-nx (tools/forwarder)."""
import os, struct, sys
src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), 'main.npdm')
dst = sys.argv[2] if len(sys.argv) > 2 else os.path.expanduser('~/workspace/sa2-nx/sdk/src/core/forwarder_npdm.inc')
d = open(src, 'rb').read()
assert d[:4] == b'META'
aci0, = struct.unpack_from('<I', d, 0x70)
acid, = struct.unpack_from('<I', d, 0x78)
assert d[aci0:aci0 + 4] == b'ACI0' and d[acid + 0x200:acid + 0x204] == b'ACID', (hex(aci0), hex(acid))
lignes = ['// opdarkness (2026-11-01) : main.npdm d\'un forwarder aux quatre cœurs (tools/forwarder/od_forwarder.json du dépôt',
          '// opdarkness, npdmtool de devkitPro ; repris de renut-nx et NetherSX2_nx). Écrit par tools/forwarder/npdm_vers_inc.py.',
          f'constexpr unsigned kForwarderNpdmAci0 = 0x{aci0:X};  // ACI0 : identifiant du programme à +0x10',
          f'constexpr unsigned kForwarderNpdmAcid = 0x{acid:X};  // ACID (signature comprise) : plage d\'identifiants à +0x210 / +0x218',
          f'constexpr unsigned char kForwarderNpdm[{len(d)}] = {{']
for i in range(0, len(d), 16):
    lignes.append('    ' + ' '.join(f'0x{b:02X},' for b in d[i:i + 16]))
lignes.append('};')
open(dst, 'w').write('\n'.join(lignes) + '\n')
print(dst, len(d), hex(aci0), hex(acid))

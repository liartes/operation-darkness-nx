#!/usr/bin/env python3
"""Écritures vectorielles non alignées du code généré (stvlx / stvrx) en une seule copie au lieu d'une boucle d'octets.

Le générateur traduit stvlx vS,ea par « for i < 16 - (ea & 15) : octet ea + i = vS.u8[15 - i] » et stvrx par
« for i < (ea & 15) : octet ea - i - 1 = vS.u8[i] » : une écriture d'un octet par tour (750 stvlx et 293 stvrx dans le
jeu ; profil PGO du 2026-10-16 : le produit de matrices et le décodeur vidéo en tête). Les octets écrits sont contigus
et pris dans le vecteur inversé r (r[k] = vS.u8[15 - k]) :
  stvlx : n = 16 - (ea & 15) octets de r[0..n) à ea
  stvrx : n = ea & 15 octets de r[16 - n..16) à ea - n
tous dans le même bloc aligné de 16 octets (même décalage physique REX_PHYS_HOST_OFFSET). Même mémoire écrite, octet
par octet, sans lecture-modification-écriture du bloc (rien n'est écrit en dehors des octets de l'original).
Ajoute od_stvlx / od_stvrx à opdarkness_pch.h et remplace les boucles ; sans effet sur des fichiers déjà traités.

Aussi : vsubsws (soustraction saturée de mots signés, 375 dans le jeu, dont le décodeur vidéo WMV), traduit voie par voie
(différence sur 64 bits et deux comparaisons par voie), comme vaddsws l'est déjà : différence sur 32 bits, dépassement
quand a et b sont de signes différents et que le signe du résultat diffère de celui de a, valeur saturée INT_MAX si
a >= 0, INT_MIN sinon. Voies indépendantes : identique même quand vD est vA ou vB. temp reçoit la même dernière valeur
(différence de la voie 3 sur 64 bits).

  python3 tools/vmx_ecritures.py [dossier du code généré]
"""
import glob
import os
import re
import sys

AIDES = '''
// opdarkness (tools/vmx_ecritures.py) : stvlx / stvrx en une copie des octets du vecteur inversé.
#ifndef OD_VMX_ECRITURES
#define OD_VMX_ECRITURES
static inline void od_inverse16(uint8_t* r, const uint8_t* v) {
  for (int k = 0; k < 16; ++k) r[k] = v[15 - k];
}
static inline void od_stvlx(uint8_t* base, uint32_t ea, const uint8_t* v) {
  alignas(16) uint8_t r[16];
  od_inverse16(r, v);
  __builtin_memcpy(REX_RAW_ADDR(ea), r, 16 - (ea & 0xF));
}
static inline void od_stvrx(uint8_t* base, uint32_t ea, const uint8_t* v) {
  const uint32_t n = ea & 0xF;
  if (!n) return;
  alignas(16) uint8_t r[16];
  od_inverse16(r, v);
  __builtin_memcpy(REX_RAW_ADDR(ea - n), r + 16 - n, n);
}
#endif
'''

STVLX = re.compile(r'\tfor \(size_t i = 0; i < \(16 - \(ea & 0xF\)\); i\+\+\)\n'
                   r'\t\tREX_STORE_U8\(ea \+ i, ([\w.]+)\.u8\[15 - i\]\);')
STVRX = re.compile(r'\tfor \(size_t i = 0; i < \(ea & 0xF\); i\+\+\)\n'
                   r'\t\tREX_STORE_U8\(ea - i - 1, ([\w.]+)\.u8\[i\]\);')


VSUBSWS = re.compile(
    r'\t// vsubsws (v\d+),(v\d+),(v\d+)\n'
    + ''.join(r'\ttemp\.s64 = int64_t\(ctx\.\2\.s32\[%d\]\) - int64_t\(ctx\.\3\.s32\[%d\]\);\n'
              r'\tctx\.\1\.s32\[%d\] = temp\.s64 > INT_MAX \? INT_MAX : temp\.s64 < INT_MIN \? INT_MIN : temp\.s64;\n'
              % (i, i, i) for i in range(4)))


def vsubsws(m):
    d, a, b = m.groups()
    return (f'\t// vsubsws {d},{a},{b}\n'
            '\t{\n'
            f'\t\tsimde__m128i a = simde_mm_load_si128((simde__m128i*)ctx.{a}.u8);\n'
            f'\t\tsimde__m128i b = simde_mm_load_si128((simde__m128i*)ctx.{b}.u8);\n'
            '\t\tsimde__m128i diff = simde_mm_sub_epi32(a, b);\n'
            '\t\tsimde__m128i overflow = simde_mm_and_si128(simde_mm_xor_si128(a, b), simde_mm_xor_si128(a, diff));\n'
            '\t\tsimde__m128i sat_val = simde_mm_xor_si128(simde_mm_srai_epi32(a, 31), simde_mm_set1_epi32(0x7FFFFFFF));\n'
            '\t\ttemp.s64 = int64_t(simde_mm_extract_epi32(a, 3)) - int64_t(simde_mm_extract_epi32(b, 3));\n'
            f'\t\tsimde_mm_store_si128((simde__m128i*)ctx.{d}.u8, simde_mm_castps_si128(simde_mm_blendv_ps('
            'simde_mm_castsi128_ps(diff), simde_mm_castsi128_ps(sat_val), simde_mm_castsi128_ps(overflow))));\n'
            '\t}\n')


def main():
    gen = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'generated', 'default')
    pch = os.path.join(gen, 'opdarkness_pch.h')
    texte = open(pch).read()
    if 'OD_VMX_ECRITURES' not in texte:
        # Après REX_RAW_ADDR et REX_STORE_U8 (fin du fichier : les macros mémoire sont toutes définies).
        open(pch, 'w').write(texte.rstrip('\n') + '\n' + AIDES)
    total_l = total_r = total_s = 0
    for f in sorted(glob.glob(os.path.join(gen, 'opdarkness_recomp.*.cpp'))):
        s = open(f).read()
        s, nl = STVLX.subn(r'\tod_stvlx(base, ea, \1.u8);', s)
        s, nr = STVRX.subn(r'\tod_stvrx(base, ea, \1.u8);', s)
        s, ns = VSUBSWS.subn(vsubsws, s)
        total_s += ns
        if nl or nr or ns:
            open(f, 'w').write(s)
        total_l += nl
        total_r += nr
    print(f'vmx_ecritures : {total_l} stvlx et {total_r} stvrx en une copie, {total_s} vsubsws vectorisés')


if __name__ == '__main__':
    main()

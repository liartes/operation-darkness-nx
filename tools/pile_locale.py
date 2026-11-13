#!/usr/bin/env python3
"""Pile locale d'une copie localisée (generated/hot/n_<ADDR>.h, tools/apres_codegen.py ; 2026-10-22).

Une fonction feuille du jeu qui construit ses vecteurs en écrivant des flottants un par un sur sa pile (stfs) puis en
les relisant en vecteurs (lvx128) paie, dans le code traduit, chaque écriture dans la mémoire du jeu (avec inversion
des octets) et des relectures larges de petites écritures récentes (lentes sur ARM comme sur x86). Ici, la pile sous r1
devient un tableau local de la copie, avec le même ordre d'octets et le même alignement modulo 16 que la pile du jeu :
mêmes opérations, mêmes résultats ; seule différence, ces octets morts sous le pointeur de pile ne sont plus écrits en
mémoire du jeu (la fonction n'appelle rien et rend la main). Refuse (et laisse la copie telle quelle) si une adresse
vectorielle ne vient pas de r1, si r1 change, ou si la fonction appelle quoi que ce soit.

  python3 tools/pile_locale.py generated/hot/n_<ADDR>.h

Puis (2026-11-01, mots()) : quand la pile n'est touchée que par mots de 32 bits à des décalages fixes de r1 et par
vecteurs à des décalages multiples de 16, une seconde version tient ces mots dans un tableau de mots natifs (sans
inversion des octets) et assemble les vecteurs directement par voies (voie L = mot 3 - L, comme lvx128 + VectorMaskL) :
le compilateur garde le tout dans les registres. Même contenu, mêmes opérations. Elle sert si r1 est aligné sur 16
(cas de la convention d'appel) ; sinon, la version par octets.
"""
import re
import sys

ST = re.compile(r'REX_STORE_D_U32\(l_r1\.u32, (-?\d+), ')
LD = re.compile(r'REX_LOAD_D_U32\(l_r1\.u32, (-?\d+)\)')
EA = re.compile(r'^\s*ea = \((?:l_)?(r\d+)\.u32\) & ~0xF;')
FROM_R1 = re.compile(r'^\s*(?:l_)?(r\d+)\.s64 = l_r1\.s64 \+ (-?\d+);')
ASSIGN = re.compile(r'^\s*(?:l_)?(r\d+)\.[us]\d+ = ')
DEBUT = 'inline void Native(PPCContext& ctx, uint8_t* base) {\n'
TETE = ('  // opdarkness (tools/pile_locale.py) : la pile du jeu sous r1, locale (mêmes octets, même alignement modulo 16).\n'
        '  alignas(16) uint8_t od_pile[320];\n'
        '  const uint32_t od_base = (ctx.r1.u32 - 288u) & ~15u;\n'
        '#define OD_PILE(g) (od_pile + (uint32_t(g) - od_base))\n'
        '#define OD_PILE_ST32(o, y) do { const uint32_t od_v = __builtin_bswap32(uint32_t(y)); '
        'std::memcpy(OD_PILE(l_r1.u32 + (o)), &od_v, 4); } while (0)\n'
        '#define OD_PILE_LD32(o) [&] { uint32_t od_v; std::memcpy(&od_v, OD_PILE(l_r1.u32 + (o)), 4); '
        'return __builtin_bswap32(od_v); }()\n')


def main():
    chemin = sys.argv[1]
    texte = open(chemin, encoding='utf-8').read()
    if 'od_pile' in texte:
        return 0
    lignes = texte.split('\n')
    depuis_r1 = {}
    for i, l in enumerate(lignes):
        if '(ctx, base)' in l or 'SA2_SYNC_OUT(); ' in l and 'return' not in l:
            print(f'pile_locale: {chemin} : appel ligne {i + 1}, laissé tel quel', file=sys.stderr)
            return 0
        if re.match(r'^\s*l_r1\.', l):
            print(f'pile_locale: {chemin} : r1 modifié ligne {i + 1}, laissé tel quel', file=sys.stderr)
            return 0
        m = FROM_R1.match(l)
        if m:
            depuis_r1[m.group(1)] = True
            continue
        m = EA.match(l)
        if m:
            if not depuis_r1.get(m.group(1)):
                print(f'pile_locale: {chemin} : adresse vectorielle hors pile ligne {i + 1}, laissé tel quel', file=sys.stderr)
                return 0
            lignes[i + 1] = lignes[i + 1].replace('REX_RAW_ADDR(ea)', 'OD_PILE(ea)')
            if 'OD_PILE(ea)' not in lignes[i + 1]:
                print(f'pile_locale: {chemin} : accès vectoriel inattendu ligne {i + 2}, laissé tel quel', file=sys.stderr)
                return 0
            continue
        m = ASSIGN.match(l)
        if m:
            depuis_r1[m.group(1)] = False
    texte = '\n'.join(lignes)
    if 'REX_RAW_ADDR(ea)' in texte:
        print(f'pile_locale: {chemin} : accès vectoriel non traité, laissé tel quel', file=sys.stderr)
        return 0
    texte, n_st = ST.subn(lambda m: f'OD_PILE_ST32({m.group(1)}, ', texte)
    texte, n_ld = LD.subn(lambda m: f'OD_PILE_LD32({m.group(1)})', texte)
    if DEBUT not in texte:
        print(f'pile_locale: {chemin} : forme inattendue, laissé tel quel', file=sys.stderr)
        return 0
    texte = texte.replace(DEBUT, DEBUT + TETE, 1)
    texte = texte.replace('#undef SA2_SYNC_OUT\n', '#undef OD_PILE\n#undef OD_PILE_ST32\n#undef OD_PILE_LD32\n#undef SA2_SYNC_OUT\n', 1)
    open(chemin, 'w', encoding='utf-8').write(texte)
    print(f'pile_locale: {chemin} : {n_st} écritures, {n_ld} lectures et les accès vectoriels sur la pile locale')
    return 0


VLD = re.compile(r'^(\s*)simde_mm_store_si128\(\(simde__m128i\*\)l_(v\d+)\.u8, simde_mm_shuffle_epi8\(simde_mm_load_si128\('
                 r'\(simde__m128i\*\)OD_PILE\(ea\)\), simde_mm_load_si128\(\(simde__m128i\*\)VectorMaskL\)\)\);$')
VST = re.compile(r'^(\s*)simde_mm_store_si128\(\(simde__m128i\*\)OD_PILE\(ea\), simde_mm_shuffle_epi8\(simde_mm_load_si128\('
                 r'\(simde__m128i\*\)l_(v\d+)\.u8\), simde_mm_load_si128\(\(simde__m128i\*\)VectorMaskL\)\)\);$')
MOTS_TETE = ('  // opdarkness (tools/pile_locale.py, mots) : la pile du jeu sous r1 en mots natifs (r1 aligné sur 16).\n'
             '  uint32_t od_w[80];\n'
             '#define OD_MOT(o) ((320 + (o)) >> 2)\n')


def mots(chemin):
    texte = open(chemin, encoding='utf-8').read()
    if 'NativeOctets' in texte or 'od_pile' not in texte:
        return 0
    a = texte.index('inline void Native(PPCContext& ctx, uint8_t* base) {\n')
    b = texte.index('#undef SA2_SYNC_IN\n}\n', a) + len('#undef SA2_SYNC_IN\n}\n')
    octets = texte[a:b]
    corps = octets.split('\n')
    tete_fin = next(i for i, l in enumerate(corps) if l.startswith('#define OD_PILE_LD32'))
    sortie = [corps[0], MOTS_TETE.rstrip('\n')]
    k_de = {}
    ea_k = None
    for i, l in enumerate(corps[tete_fin + 1:], tete_fin + 1):
        m = FROM_R1.match(l)
        if m:
            k_de[m.group(1)] = int(m.group(2))
        elif ASSIGN.match(l):
            k_de[ASSIGN.match(l).group(1)] = None
        m = EA.match(l)
        if m:
            ea_k = k_de.get(m.group(1))
            if ea_k is None or ea_k % 16 or not -320 <= ea_k <= -16:
                print(f'pile_locale: {chemin} : vecteur de pile à {ea_k}, pas de version par mots', file=sys.stderr)
                return 0
            sortie.append(l)
            continue
        mv = VLD.match(l) or VST.match(l)
        if mv:
            ind, v = mv.group(1), mv.group(2)
            for voie in range(4):
                mot = f'od_w[OD_MOT({ea_k + 4 * (3 - voie)})]'
                sortie.append(f'{ind}l_{v}.u32[{voie}] = {mot};' if VLD.match(l) else f'{ind}{mot} = l_{v}.u32[{voie}];')
            continue
        if l.startswith('#undef OD_PILE'):
            if l == '#undef OD_PILE':
                sortie.append('#undef OD_MOT')
            continue
        if 'OD_PILE(' in l and 'OD_PILE_ST32' not in l and 'OD_PILE_LD32' not in l and not l.startswith('#'):
            print(f'pile_locale: {chemin} : accès de pile non traité ({l.strip()[:60]}), pas de version par mots', file=sys.stderr)
            return 0
        l = re.sub(r'OD_PILE_ST32\((-?\d+), ([^;]*)\);', lambda m: f'od_w[OD_MOT({m.group(1)})] = uint32_t({m.group(2)});', l)
        l = re.sub(r'OD_PILE_LD32\((-?\d+)\)', lambda m: f'od_w[OD_MOT({m.group(1)})]', l)
        if 'OD_PILE_ST32' in l or 'OD_PILE_LD32' in l:
            print(f'pile_locale: {chemin} : forme inattendue ({l.strip()[:60]}), pas de version par mots', file=sys.stderr)
            return 0
        sortie.append(l)
    mots_fn = '\n'.join(sortie)
    octets = octets.replace('inline void Native(', 'inline void NativeOctets(', 1)
    mots_fn = mots_fn.replace('inline void Native(PPCContext& ctx, uint8_t* base) {',
                              'inline void Native(PPCContext& ctx, uint8_t* base) {\n'
                              '  if (ctx.r1.u32 & 15u) { NativeOctets(ctx, base); return; }', 1)
    texte = texte[:a] + octets + '\n' + mots_fn + texte[b:]
    open(chemin, 'w', encoding='utf-8').write(texte)
    print(f'pile_locale: {chemin} : version par mots ajoutée')
    return 0


if __name__ == '__main__':
    r = main()
    sys.exit(r or mots(sys.argv[1]))

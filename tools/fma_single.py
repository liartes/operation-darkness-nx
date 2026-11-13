#!/usr/bin/env python3
"""fmadds / fmsubs / fnmadds / fnmsubs arrondis une seule fois, comme le Xenon (repris de renut_nx_ultimate_base, port 96
de blur-nx, tools/fma_single.py ; adapté : macros ajoutées à la fin de opdarkness_pch.h).

Le générateur écrit ces instructions en
    X.f64 = double(float(std::fma(A.f64, B.f64, C.f64)));        (fmsubs : -C.f64 ; fnmadds / fnmsubs : -std::fma)
soit une fma en double arrondie de nouveau en simple : deux arrondis là où le Xenon n'arrondit qu'une fois le résultat
exact (un ulp d'écart dans de rares cas, environ une opération sur quelques centaines de millions au hasard), et toute
chaîne en simple précision gardée dans des registres doubles (un fcvt avant, deux après chacune). Ce script écrit
    X.f64 = REX_FMADDS(A.f64, B.f64, C.f64);                      (fnmadds / fnmsubs : (-REX_FMADDS(...)))
REX_FMADDS = rex::ppc::FmaddsSingle (SDK, rex/ppc/fma_single.h) : une fmaf quand les trois entrées sont des flottants
exacts (le résultat du Xenon), la forme double sinon ; le compilateur supprime le test quand il voit d'où viennent les
entrées. La négation commute avec l'arrondi. REX_FMADDS_DOUBLE_ROUNDING (à la compilation) rend les formes doubles.

Avant les copies localisées et en masse (tools/apres_codegen.py), qui en héritent. Sans effet sur des fichiers déjà traités.
  python3 tools/fma_single.py [dossier du code généré] [--check]
"""
import glob
import os
import re
import sys

OPERAND = r'(-?[A-Za-z_][\w.]*)'
FORM = re.compile(r'double\(float\((-?)std::fma\(' + OPERAND + r', ' + OPERAND + r', ' + OPERAND + r'\)\)\)')

MARKER = '// opdarkness (tools/fma_single.py) : fmadds et apparentés arrondis une fois (renut_nx_ultimate_base, port 96)'
MACROS = MARKER + '''
#ifndef REX_FMADDS
#include <rex/ppc/fma_single.h>
#define REX_FMADDS(a, b, c) rex::ppc::FmaddsSingle((a), (b), (c))
#endif  // REX_FMADDS
'''


def rewrite(text):
    count = [0]

    def repl(m):
        count[0] += 1
        call = 'REX_FMADDS(%s, %s, %s)' % (m.group(2), m.group(3), m.group(4))
        return '(-%s)' % call if m.group(1) else call

    return FORM.sub(repl, text), count[0]


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    check = '--check' in sys.argv
    gen = args[0] if args else os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                                            'generated', 'default')
    if os.environ.get('OD_FMA_SINGLE_OFF'):
        print('fma_single : coupé (OD_FMA_SINGLE_OFF)')
        return 0
    total = files = 0
    for path in sorted(glob.glob(os.path.join(gen, '*_recomp.*.cpp'))):
        text = open(path, encoding='utf-8').read()
        new, n = rewrite(text)
        if n:
            total += n
            files += 1
            if not check:
                open(path, 'w', encoding='utf-8', newline='\n').write(new)
    if not check:
        pch = os.path.join(gen, 'opdarkness_pch.h')
        text = open(pch, encoding='utf-8').read()
        if MARKER not in text:
            open(pch, 'w', encoding='utf-8', newline='\n').write(text.rstrip('\n') + '\n' + MACROS)
    print('fma_single%s : %d multiplications-additions en simple arrondies une fois (%d fichiers)' % (
        ' (comptage seul)' if check else '', total, files))
    return 0


if __name__ == '__main__':
    sys.exit(main())

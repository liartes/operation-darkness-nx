#!/usr/bin/env python3
"""Fonctions du code du jeu dont le profil PGO n'est plus valable (repris de torchlight, switch/tools/audit_profil.py).

Le « 0 coverage mismatch » d'une compilation ne prouve rien pour le code généré : DEFINE_REX_FUNC vient d'un en-tête système
du SDK, et GCC y masque les avertissements ; notre CMakeLists.txt ajoute -Wno-missing-profile et -Wno-coverage-mismatch
(OD_PGO=use). Une fonction dont le graphe a changé perd son profil sans un mot. Ce script recompile chaque fichier du code
du jeu (opdarkness_recomp.N, copies en masse od_hot_*) avec ses options exactes, plus -Wsystem-headers, -Wmissing-profile et
-Wno-error=coverage-mismatch, et compte les fonctions signalées. La compilation n'est pas touchée : l'objet va dans
<OUT>/audit-pgo/<même chemin>, avec -fprofile-prefix-path=<OUT>/audit-pgo. GCC nomme le .gcda d'après le chemin de l'objet
(dossier courant + -o relatif) moins ce préfixe : le nom reste celui de la vraie compilation. Pièges (2026-11-05) : un objet
envoyé dans /dev/null, ou un -o absolu (chemin jamais transformé), ne trouve aucun profil. À lancer sur la machine et dans
le dossier qui ont compilé le .nro (profil, code généré et options identiques).

  OUT=out/build/switch-renut-pgo python3 tools/switch/audit_profil.py [--jobs 6] [--sortie liste.txt]
"""
import argparse
import concurrent.futures
import os
import re
import subprocess
import sys

RACINE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WS = os.path.join(os.path.expanduser('~'), 'workspace')
RE_OBJET = re.compile(r'(opdarkness_recomp\.\d+|od_hot_shard_\d+|od_hot_guest)\.cpp\.obj:')
AUDIT = 'audit-pgo'  # sous-dossier de OUT pour les objets de l'audit (ninja ne le connaît pas)
RE_FONCTION = re.compile(r"function '(?:__imp__)?(sub_[0-9A-F]{8})'")


def commandes(out):
    cibles = subprocess.run(['ninja', '-t', 'targets', 'all'], cwd=out, capture_output=True, text=True).stdout
    objets = [l.split(':')[0] for l in cibles.split('\n') if RE_OBJET.search(l)]
    if not objets:
        sys.exit('aucun objet du code du jeu dans ' + out)
    res = []
    for o in objets:
        c = subprocess.run(['ninja', '-t', 'commands', o], cwd=out, capture_output=True, text=True).stdout
        ligne = c.strip().split('\n')[-1]
        if '-fprofile-use' not in ligne or ' -fprofile-prefix-path=' + out + ' ' not in ligne or ' -o ' + o + ' ' not in ligne + ' ':
            sys.exit('commande inattendue pour ' + o)
        ligne = ligne.replace(' -Wno-missing-profile', ' -Wsystem-headers -Wmissing-profile')
        ligne = ligne.replace(' -Wno-coverage-mismatch', ' -Wno-error=coverage-mismatch')
        ligne = ligne.replace(' -fprofile-prefix-path=' + out + ' ',
                              ' -fprofile-prefix-path=' + os.path.join(out, AUDIT) + ' ')
        ligne = (ligne + ' ').replace(' -o ' + o + ' ', ' -o ' + os.path.join(AUDIT, o) + ' ')
        ligne = re.sub(r' -MF \S+', ' -MF /dev/null', ligne)
        os.makedirs(os.path.dirname(os.path.join(out, AUDIT, o)), exist_ok=True)
        res.append((o, ligne))
    return res


def compiler(out, objet, ligne):
    cmd = ['docker', 'run', '--rm', '-u', '%d:%d' % (os.getuid(), os.getgid()), '-e', 'HOME=' + os.path.expanduser('~'),
           '-e', 'DEVKITPRO=/opt/devkitpro', '-v', '%s:%s' % (WS, WS), '-w', out, 'devkitpro/devkita64', 'bash', '-c', ligne]
    r = subprocess.run(cmd, capture_output=True, text=True, errors='replace')
    texte = r.stdout + r.stderr
    controle, absents = set(), set()
    for l in texte.split('\n'):
        if 'does not match' in l or 'coverage mismatch' in l or 'number of counters' in l:
            controle.update(RE_FONCTION.findall(l))
        elif 'not found in profile' in l or 'profile count data' in l and 'function' in l:
            absents.update(RE_FONCTION.findall(l))
    fichier_absent = 'profile count data file not found' in texte
    return objet, r.returncode, controle, absents, fichier_absent, texte


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--jobs', type=int, default=6)
    ap.add_argument('--sortie', help='liste des fonctions au profil perdu')
    ap.add_argument('--brut', help='dossier où écrire la sortie de GCC fichier par fichier')
    ap.add_argument('--limite', type=int, help='seulement les N premiers fichiers (essai du script)')
    a = ap.parse_args()
    out = os.path.abspath(os.environ.get('OUT', os.path.join(RACINE, 'out', 'build', 'switch-renut-pgo')))
    travaux = commandes(out)
    if a.limite:
        travaux = [t for t in travaux if 'od_hot_shard_0' in t[0]] + travaux[:a.limite - 1]
    print('%d fichiers' % len(travaux), flush=True)
    controle, absents, sans_fichier, echecs = set(), set(), [], []
    if a.brut:
        os.makedirs(a.brut, exist_ok=True)
    with concurrent.futures.ThreadPoolExecutor(a.jobs) as ex:
        for objet, code, c, ab, fa, texte in ex.map(lambda t: compiler(out, *t), travaux):
            controle |= c
            absents |= ab
            if fa:
                sans_fichier.append(objet)
            if code:
                echecs.append(objet)
            if a.brut:
                with open(os.path.join(a.brut, os.path.basename(objet) + '.txt'), 'w') as f:
                    f.write(texte)
    print('profil perdu (graphe changé) : %d fonctions' % len(controle))
    print('absentes du profil : %d fonctions' % len(absents))
    print('fichiers sans .gcda : %d' % len(sans_fichier))
    for o in sans_fichier[:10]:
        print('  ', o)
    print('échecs de compilation : %d' % len(echecs))
    for o in echecs[:5]:
        print('  ', o)
    if a.sortie:
        with open(a.sortie, 'w') as f:
            for n in sorted(controle):
                f.write(n + '\n')


if __name__ == '__main__':
    main()

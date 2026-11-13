#!/usr/bin/env python3
"""Étapes d'après la génération du code (étape C de docs/OPTIMISATION.md), outils de sa2-nx :
  - pch_no_volatile.py : accès à la mémoire du jeu sans volatile (le compilateur peut garder et regrouper les
    lectures) ; REQUIERT loop_barriers.py : barrière du compilateur en tête de chaque boucle, sinon une boucle qui
    attend un mot changé par un autre fil (ou le GPU) peut être compilée en boucle infinie ;
  - tools/vmx_ecritures.py (du projet) : stvlx / stvrx en une copie au lieu d'une boucle d'octets ;
  - tools/fma_single.py (du projet, port 96 de renut_nx_ultimate_base) : fmadds arrondis une fois ;
  - direct_calls.py : appels sub_X -> __imp__sub_X (même fonction, mais que le compilateur peut intégrer) pour
    toute fonction sans interception ; les interceptions sont les adresses 82xxxxxx citées dans le code de src/ (hors
    commentaires et références __imp__), les macros comme REX_HOOK_RAW(sub_##ADDR) comprises.
Lancé par la compilation après chaque génération (CMakeLists.txt : add_custom_command APPEND) ; à la main :
  python3 tools/apres_codegen.py            (idempotent)
  python3 tools/apres_codegen.py --verifier out/build/sa2sdk/opdarkness
--verifier : compare la liste aux interceptions réelles de l'exécutable (symboles sub_X forts, nm) ; sortie 1 si
une fonction interceptée est appelée directement quelque part.
"""
import os, re, subprocess, sys

RACINE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GEN = os.path.join(RACINE, "generated", "default")
OUTILS = os.path.expanduser("~/workspace/sa2-nx/tools")
LISTE = os.path.join(RACINE, "out", "crochets.txt")
COPIES = os.path.join(RACINE, "src", "copies_localisees.txt")
MASSE = os.path.join(RACINE, "src", "copies_masse.txt")  # tools/copies_masse_liste.py
HOT = os.path.join(RACINE, "generated", "hot")


def code_seul(texte):
    texte = re.sub(r"/\*.*?\*/", " ", texte, flags=re.S)
    texte = re.sub(r"//[^\n]*", " ", texte)
    return re.sub(r"__imp__sub_[0-9A-Fa-f]{8}", " ", texte)


def crochets():
    noms = set()
    for d, _, fs in os.walk(os.path.join(RACINE, "src")):
        for f in fs:
            if f.endswith((".cpp", ".cc", ".c", ".h", ".hpp", ".inc")):
                texte = code_seul(open(os.path.join(d, f), encoding="utf-8", errors="ignore").read())
                noms |= {"sub_" + m.upper() for m in re.findall(r"(?<![0-9A-Fa-f])(82[0-9A-Fa-f]{6})(?![0-9A-Fa-f])", texte)}
    # Les copies en masse (2026-11-01) sont des interceptions aussi : leurs appelants passent par le crochet.
    if os.path.isfile(MASSE):
        for ligne in open(MASSE, encoding="utf-8"):
            m = re.match(r"\s*([0-9A-Fa-f]{8})\b", ligne)
            if m and not ligne.lstrip().startswith("#"):
                noms.add("sub_" + m.group(1).upper())
    return noms


def verifier(exe):
    sortie = subprocess.run(["nm", exe], capture_output=True, text=True, check=True).stdout
    forts = {l.split()[2] for l in sortie.splitlines() if re.match(r"\S+ T sub_[0-9A-F]{8}$", l)}
    liste = set(open(LISTE).read().split())
    directs = set()
    for f in os.listdir(GEN):
        if re.match(r"\w+_recomp\.\d+\.cpp$", f):
            directs |= {"sub_" + a for a in re.findall(r"__imp__sub_([0-9A-F]{8})\(ctx, base\);",
                                                         open(os.path.join(GEN, f)).read())}
    fautes = sorted((forts & directs) - set())
    print(f"interceptions dans l'exécutable : {len(forts)} ; dans la liste : {len(liste)} ; "
          f"interceptées mais appelées directement : {len(fautes)} {fautes[:10]}")
    return 1 if fautes else 0


def main():
    if "--verifier" in sys.argv:
        sys.exit(verifier(sys.argv[sys.argv.index("--verifier") + 1]))
    os.makedirs(os.path.dirname(LISTE), exist_ok=True)
    open(LISTE, "w").write("\n".join(sorted(crochets())) + "\n")
    py = sys.executable
    for cmd in ([py, "-I", os.path.join(OUTILS, "pch_no_volatile.py"), os.path.join(GEN, "opdarkness_pch.h")],
                [py, "-I", os.path.join(OUTILS, "loop_barriers.py"), GEN],
                [py, "-I", os.path.join(OUTILS, "direct_calls.py"), "--redo", "--gen", GEN, "--hooked-list", LISTE]):
        subprocess.run(cmd, check=True)
    # stvlx / stvrx en une copie au lieu d'une boucle d'octets (avant les copies localisées, qui en héritent).
    subprocess.run([py, "-I", os.path.join(RACINE, "tools", "vmx_ecritures.py"), GEN], check=True)
    # fmadds et apparentés arrondis une fois (renut_nx_ultimate_base, port 96 ; 2026-11-13), avant les copies aussi.
    subprocess.run([py, "-I", os.path.join(RACINE, "tools", "fma_single.py"), GEN], check=True)
    copies_localisees(py)
    # Copies en masse (src/copies_masse.txt -> generated/hot/masse/ ; outil de renut-nx adapté), après tout le reste :
    # elles sont faites du texte final.
    subprocess.run([py, "-I", os.path.join(RACINE, "tools", "copies_masse.py"), "--list", "--bulk"], check=True)


def copies_localisees(py):
    """Copies localisées (src/copies_localisees.txt -> generated/hot/n_<ADDR>.h), en synchronisation complète autour
    des appels (l'outil de sa2-nx ne connaît pas nos interceptions) ; doubles permis : -ffp-contract=off partout."""
    os.makedirs(HOT, exist_ok=True)
    for ligne in open(COPIES, encoding="utf-8"):
        ligne = ligne.strip()
        if not ligne or ligne.startswith("#"):
            continue
        adresse, _, quoi = ligne.partition(" ")
        subprocess.run([py, "-I", os.path.join(OUTILS, "localize_native.py"), adresse, "--what", quoi.strip(),
                        "--gen", GEN, "--out", os.path.join(HOT, f"n_{adresse.upper()}.h"), "--allow-double",
                        "--sync", "full"], check=True)
        if "[pile locale]" in quoi:  # opdarkness (2026-10-22) : tools/pile_locale.py
            subprocess.run([py, "-I", os.path.join(RACINE, "tools", "pile_locale.py"),
                            os.path.join(HOT, f"n_{adresse.upper()}.h")], check=True)


if __name__ == "__main__":
    main()

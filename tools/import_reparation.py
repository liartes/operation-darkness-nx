#!/usr/bin/env python3
"""Génère src/od_reparation_variantes.cpp depuis sa2-nx shaders/tools/repair_vertex_variant_declarations.cpp :
les mêmes preuves, appelées en mémoire (opdarkness::ReparerVariante) au lieu de fichiers, sans messages.
Usage : tools/import_reparation.py [dépôt sa2-nx]   (défaut ~/workspace/sa2-nx)
"""
import os
import re
import sys

sa2 = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser("~/workspace/sa2-nx")
s = open(os.path.join(sa2, "shaders/tools/repair_vertex_variant_declarations.cpp")).read()

def remplacer(ancien, nouveau):
    global s
    if ancien not in s:
        sys.exit(f"motif introuvable (l'outil de sa2-nx a changé) : {ancien[:60]!r}")
    s = s.replace(ancien, nouveau)

remplacer('''// Offline, fail-closed DECL repair. Does not modify sequencer code or packages.
// Usage: repair_vertex_variant_declarations template.bin actual.bin fresh-output.bin
#include "../../app/src/me/me_shader_identity.h"
#include "../../app/src/me/me_vertex_fetch_selection.h"
''', '''// opdarkness - réparation des variantes de vertex shaders en mémoire, pour la traduction au fil du jeu
// (src/od_variantes.cpp, docs/SHADERS_EN_JEU.md). Fichier GÉNÉRÉ par tools/import_reparation.py depuis
// sa2-nx shaders/tools/repair_vertex_variant_declarations.cpp (GPL-3.0) : les preuves sont celles de l'outil, seules
// les entrées et la sortie passent par la mémoire au lieu de fichiers, sans messages. Ne pas modifier à la main.
//
// Offline, fail-closed DECL repair. Does not modify sequencer code or packages.
#include "od_reparation_variantes.h"

#include "me/me_shader_identity.h"
#include "me/me_vertex_fetch_selection.h"
''')
remplacer('explicit Container(const char* path) : bytes(Read(path)) {', 'explicit Container(Bytes b) : bytes(std::move(b)) {')
# Lecture et écriture de fichiers : inutiles.
a = s.index('Bytes Read(const char* path) {'); b = s.index('uint32_t Be(const Bytes& b, size_t p) {')
s = s[:a] + s[b:]
a = s.index('#ifndef O_BINARY'); b = s.index('}  // namespace', a)
s = s[:a] + s[b:]
a = s.index('int main(int argc,char** argv) try {')
corps = s[a:]
corps = corps.replace('int main(int argc,char** argv) try {',
    'bool opdarkness::ReparerVariante(const std::vector<uint8_t>& modele, const std::vector<uint8_t>& chargee,\n'
    '                                 std::vector<uint8_t>& sortie, std::string& raison) try {')
corps = re.sub(r'  if\(argc!=4\)[^\n]*\n', '', corps)
corps = corps.replace('Container original(argv[1]), actual(argv[2]);', 'Container original(modele), actual(chargee);')
corps = corps.replace('  ExclusiveWrite(argv[3],output);\n', '  sortie=std::move(output);\n')
corps = corps.replace('\n  return 0;\n} catch', '\n  return true;\n} catch')
# Messages : chaque appel std::printf(...); (sur une ou plusieurs lignes) devient (void)0;.
sortie = []
i = 0
while True:
    j = corps.find('std::printf(', i)
    if j < 0:
        sortie.append(corps[i:]); break
    debut = corps.rfind('\n', 0, j) + 1
    prefixe = corps[debut:j]
    sortie.append(corps[i:debut] if prefixe.strip() == '' else corps[i:j])
    profondeur = 0; k = j + len('std::printf')
    while True:
        c = corps[k]
        if c == '(': profondeur += 1
        elif c == ')':
            profondeur -= 1
            if profondeur == 0: break
        elif c == '"':
            k += 1
            while corps[k] != '"':
                k += 2 if corps[k] == '\\' else 1
        k += 1
    fin = corps.index(';', k) + 1
    # Une instruction vide à la place : un « if (...) std::printf(...); » sans accolades garde son corps.
    sortie.append(prefixe + '(void)0;' if prefixe.strip() == '' else '(void)0;')
    i = fin
corps = ''.join(sortie)
corps = re.sub(r'\} catch\(const std::exception& e\) \{\n.*\n\}', '} catch(const std::exception& e) {\n  raison=e.what(); return false;\n}', corps)
s = s[:a] + corps
for interdit in ('printf', 'argv', 'ExclusiveWrite', 'Read('):
    if interdit in s:
        sys.exit(f"reste « {interdit} » dans le fichier généré")
open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'od_reparation_variantes.cpp'), 'w').write(s)
print("src/od_reparation_variantes.cpp généré")

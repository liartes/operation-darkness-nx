// opdarkness - préparation du HLSL de XenosRecomp pour glslang : portage à l'identique de tools/prep_glslang.py
// (le script dit pourquoi chaque transformation). Sans std::regex : appelé pour chaque shader traduit sur la console.

#include "od_prep_glslang.h"

#include <cctype>

namespace opdarkness::glslang_prep {
namespace {

bool CarMot(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// vk::RawBufferLoad<T>(...) -> ((T)0), parenthèses équilibrées.
std::string SansLecturesBrutes(std::string_view t) {
  static constexpr std::string_view kMotif = "vk::RawBufferLoad<";
  std::string sortie;
  sortie.reserve(t.size());
  size_t i = 0;
  while (true) {
    size_t m = t.find(kMotif, i);
    while (m != std::string_view::npos) {
      // Le motif Python demande un type (\w+) puis ">(".
      size_t j = m + kMotif.size(), k = j;
      while (k < t.size() && CarMot(t[k])) ++k;
      if (k > j && k + 1 < t.size() && t[k] == '>' && t[k + 1] == '(') break;
      m = t.find(kMotif, m + 1);
    }
    if (m == std::string_view::npos) {
      sortie.append(t.substr(i));
      return sortie;
    }
    sortie.append(t.substr(i, m - i));
    const size_t debut_type = m + kMotif.size();
    size_t fin_type = debut_type;
    while (CarMot(t[fin_type])) ++fin_type;
    size_t j = fin_type + 2;
    int niveau = 1;
    while (niveau && j < t.size()) {
      if (t[j] == '(') ++niveau;
      if (t[j] == ')') --niveau;
      ++j;
    }
    sortie += "((";
    sortie.append(t.substr(debut_type, fin_type - debut_type));
    sortie += ")0)";
    i = j;
  }
}

// (1ull << n) -> (n < 32 ? (1u << n) : 0u)
std::string SansDecalages64(std::string_view t) {
  static constexpr std::string_view kMotif = "(1ull << ";
  std::string sortie;
  sortie.reserve(t.size());
  size_t i = 0;
  for (size_t m; (m = t.find(kMotif, i)) != std::string_view::npos;) {
    size_t j = m + kMotif.size(), k = j;
    while (k < t.size() && CarMot(t[k])) ++k;
    if (k == j || k >= t.size() || t[k] != ')') {
      sortie.append(t.substr(i, m + 1 - i));
      i = m + 1;
      continue;
    }
    const std::string_view n = t.substr(j, k - j);
    sortie.append(t.substr(i, m - i));
    sortie += "(";
    sortie += n;
    sortie += " < 32 ? (1u << ";
    sortie += n;
    sortie += ") : 0u)";
    i = k + 1;
  }
  sortie.append(t.substr(i));
  return sortie;
}

// Mot entier (\b de Python : bornes entre un caractère de mot et un autre).
std::string RemplacerMot(std::string_view t, std::string_view mot, std::string_view par) {
  std::string sortie;
  sortie.reserve(t.size() + 64);
  size_t i = 0;
  for (size_t m; (m = t.find(mot, i)) != std::string_view::npos;) {
    const bool avant = m == 0 || !CarMot(t[m - 1]);
    const bool apres = m + mot.size() >= t.size() || !CarMot(t[m + mot.size()]);
    sortie.append(t.substr(i, m - i));
    if (avant && apres) {
      sortie.append(par);
    } else {
      sortie.append(mot);
    }
    i = m + mot.size();
  }
  sortie.append(t.substr(i));
  return sortie;
}

bool EstBlanc(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

size_t SauterBlancs(std::string_view t, size_t i) {
  while (i < t.size() && EstBlanc(t[i])) ++i;
  return i;
}

size_t SauterMot(std::string_view t, size_t i) {
  while (i < t.size() && CarMot(t[i])) ++i;
  return i;
}

bool Commence(std::string_view t, size_t i, std::string_view mot) { return t.substr(i, mot.size()) == mot; }

// Les mots d'un texte séparés par des blancs, joints par une espace (' '.join(s.split()) de Python).
std::string Normaliser(std::string_view s) {
  std::string sortie;
  size_t i = SauterBlancs(s, 0);
  while (i < s.size()) {
    size_t j = i;
    while (j < s.size() && !EstBlanc(s[j])) ++j;
    if (!sortie.empty()) sortie += ' ';
    sortie.append(s.substr(i, j - i));
    i = SauterBlancs(s, j);
  }
  return sortie;
}

// En début de ligne, une fonction qui renvoie une texture ou un échantillonneur :
//   ^((?:RW)?Texture\w*<[\w ,]+>|SamplerState)\s+(\w+)\(([^)]*)\)\s*\{\s*return\s+(.*?);\s*\}
// Rend la fin de la correspondance (0 sinon), le nom, les paramètres et le corps.
size_t FonctionTexture(std::string_view t, size_t i, std::string_view& nom, std::string_view& params,
                       std::string_view& corps) {
  size_t k = i;
  if (Commence(t, k, "SamplerState")) {
    k += 12;
  } else {
    if (Commence(t, k, "RW")) k += 2;
    if (!Commence(t, k, "Texture")) return 0;
    k = SauterMot(t, k + 7);
    if (k >= t.size() || t[k] != '<') return 0;
    const size_t debut = ++k;
    while (k < t.size() && (CarMot(t[k]) || t[k] == ' ' || t[k] == ',')) ++k;
    if (k == debut || k >= t.size() || t[k] != '>') return 0;
    ++k;
  }
  const size_t apres_type = k;
  k = SauterBlancs(t, k);
  if (k == apres_type) return 0;
  const size_t debut_nom = k;
  k = SauterMot(t, k);
  if (k == debut_nom || k >= t.size() || t[k] != '(') return 0;
  nom = t.substr(debut_nom, k - debut_nom);
  const size_t debut_params = ++k;
  while (k < t.size() && t[k] != ')') ++k;
  if (k >= t.size()) return 0;
  params = t.substr(debut_params, k - debut_params);
  k = SauterBlancs(t, k + 1);
  if (k >= t.size() || t[k] != '{') return 0;
  k = SauterBlancs(t, k + 1);
  if (!Commence(t, k, "return")) return 0;
  k += 6;
  const size_t apres_return = k;
  k = SauterBlancs(t, k);
  if (k == apres_return) return 0;
  for (size_t p = t.find(';', k); p != std::string_view::npos; p = t.find(';', p + 1)) {
    const size_t q = SauterBlancs(t, p + 1);
    if (q < t.size() && t[q] == '}') {
      corps = t.substr(k, p - k);
      return q + 1;
    }
  }
  return 0;
}

// Les fonctions qui renvoient une texture deviennent des macros (glslang plante sur un objet texture rendu par une
// fonction) : #define NOM(p1, p2) corps.
std::string MacrosTextures(std::string_view t) {
  std::string sortie;
  sortie.reserve(t.size());
  size_t copie = 0;
  for (size_t i = 0; i < t.size();) {
    std::string_view nom, params, corps;
    if (const size_t fin = FonctionTexture(t, i, nom, params, corps)) {
      sortie.append(t.substr(copie, i - copie));
      sortie += "#define ";
      sortie.append(nom);
      sortie += '(';
      bool premier = true;
      for (size_t a = 0; a <= params.size();) {
        size_t b = params.find(',', a);
        if (b == std::string_view::npos) b = params.size();
        const std::string mots = Normaliser(params.substr(a, b - a));
        if (!mots.empty()) {
          if (!premier) sortie += ", ";
          premier = false;
          const size_t espace = mots.rfind(' ');
          sortie += mots.substr(espace == std::string::npos ? 0 : espace + 1);
        }
        a = b + 1;
      }
      sortie += ") ";
      sortie += Normaliser(corps);
      copie = i = fin;
    }
    // ^ ne correspond qu'en début de ligne : reprendre à la ligne suivante.
    const size_t nl = t.find('\n', i);
    if (nl == std::string_view::npos) break;
    i = nl + 1;
  }
  sortie.append(t.substr(copie));
  return sortie;
}

// ^([ \t]+)const (?=\w) -> \1 : le « const » des variables locales (indentées).
std::string SansConstLocaux(std::string_view t) {
  std::string sortie;
  sortie.reserve(t.size());
  size_t copie = 0;
  for (size_t i = 0; i < t.size();) {
    size_t k = i;
    while (k < t.size() && (t[k] == ' ' || t[k] == '\t')) ++k;
    if (k > i && Commence(t, k, "const ") && k + 6 < t.size() && CarMot(t[k + 6])) {
      sortie.append(t.substr(copie, k - copie));
      copie = k + 6;
    }
    const size_t nl = t.find('\n', i);
    if (nl == std::string_view::npos) break;
    i = nl + 1;
  }
  sortie.append(t.substr(copie));
  return sortie;
}

}  // namespace

std::string PreparerRenut(std::string_view hlsl) {
  // « $ » des noms de constantes des shaders compilés par D3DX ($diffuseMap) : DXC l'accepte, glslang non.
  std::string t;
  t.reserve(hlsl.size() + 64);
  for (const char c : hlsl) {
    if (c == '$') t += "od_S_";
    else t += c;
  }
  return "#define and(a, b) ((a) && (b))\n#define or(a, b) ((a) || (b))\n" +
         Preparer(SansConstLocaux(MacrosTextures(t)));
}

std::string Preparer(std::string_view hlsl) {
  std::string t = SansLecturesBrutes(hlsl);
  t = SansDecalages64(t);
  t = RemplacerMot(t, "dword", "od_dword");
  t = RemplacerMot(t, "texture", "od_texture");
  return "#define __spirv__ 1\n#define select(c, a, b) ((c) ? (a) : (b))\n" + t;
}

}  // namespace opdarkness::glslang_prep

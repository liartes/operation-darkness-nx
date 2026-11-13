// opdarkness - conversion du GLSL de SPIRV-Cross pour uam : portage de tools/renut/deko3d_glsl.py (od_deko3d_glsl.h).
// Les étapes et leur ordre sont celles de convert() ; chaque motif du script est rappelé au-dessus de son équivalent.
// Sans std::regex (appelé sur la console pour chaque shader traduit ; les motifs à .*? du script feraient de longues
// récursions).

#include "od_deko3d_glsl.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <fmt/format.h>

namespace opdarkness::deko3d_glsl {
namespace {

constexpr int kPushUboBinding = 5;
constexpr int kRawWindowSet = 15;

struct NonPris : std::runtime_error {
  using std::runtime_error::runtime_error;
};

using sv = std::string_view;

bool Mot(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }
bool Chiffre(char c) { return c >= '0' && c <= '9'; }
bool Blanc(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
bool Commence(sv t, size_t i, sv m) { return i <= t.size() && t.substr(i, m.size()) == m; }

sv Rogner(sv s) {
  size_t a = 0, b = s.size();
  while (a < b && Blanc(s[a])) ++a;
  while (b > a && Blanc(s[b - 1])) --b;
  return s.substr(a, b - a);
}

sv RognerGauche(sv s) {
  size_t a = 0;
  while (a < s.size() && Blanc(s[a])) ++a;
  return s.substr(a);
}

size_t FinMot(sv t, size_t i) {
  while (i < t.size() && Mot(t[i])) ++i;
  return i;
}

size_t FinChiffres(sv t, size_t i) {
  while (i < t.size() && Chiffre(t[i])) ++i;
  return i;
}

int Entier(sv s) { return std::stoi(std::string(s)); }

// matching() : l'indice du crochet qui ferme celui de `ouvre`.
size_t Fermant(sv t, size_t ouvre, char o, char f) {
  int niveau = 0;
  for (size_t i = ouvre; i < t.size(); ++i) {
    if (t[i] == o) {
      ++niveau;
    } else if (t[i] == f) {
      if (--niveau == 0) return i;
    }
  }
  throw NonPris(fmt::format("unbalanced {}{} at {}", o, f, ouvre));
}

// strip_nonuniform() : nonuniformEXT(x) -> (x).
std::string SansNonUniform(sv t) {
  static constexpr sv kCle = "nonuniformEXT(";
  std::string sortie;
  size_t i = 0;
  while (true) {
    const size_t j = t.find(kCle, i);
    if (j == sv::npos) {
      sortie.append(t.substr(i));
      return sortie;
    }
    sortie.append(t.substr(i, j - i));
    const size_t fin = Fermant(t, j + kCle.size() - 1, '(', ')');
    sortie += '(';
    sortie.append(t.substr(j + kCle.size(), fin - j - kCle.size()));
    sortie += ')';
    i = fin + 1;
  }
}

// array_index() : pour NOM[expr] avec NOM fini en `fin_nom`, (expr, indice après ']').
std::optional<std::pair<sv, size_t>> IndiceTableau(sv t, size_t fin_nom) {
  size_t k = fin_nom;
  while (k < t.size() && t[k] == ' ') ++k;
  if (k >= t.size() || t[k] != '[') return std::nullopt;
  const size_t fin = Fermant(t, k, '[', ']');
  return std::make_pair(t.substr(k + 1, fin - k - 1), fin + 1);
}

// Là où le script dépaquette array_index() sans vérifier (il y lèverait une TypeError).
std::pair<sv, size_t> IndiceExige(sv t, size_t fin_nom) {
  const auto idx = IndiceTableau(t, fin_nom);
  if (!idx) throw NonPris("index expected at " + std::to_string(fin_nom));
  return *idx;
}

// strip_index_mask() : le masque des indices de descripteurs de reNut (& 0x03FFFFFF) retiré.
std::string SansMasque(sv expr) {
  std::string e(Rogner(expr));
  static constexpr sv kMasque = "67108863u";
  for (int tour = 0; tour < 4; ++tour) {
    while (!e.empty() && e[0] == '(' && Fermant(e, 0, '(', ')') == e.size() - 1)
      e = std::string(Rogner(sv(e).substr(1, e.size() - 2)));
    // ^(.*?)\s*&\s*67108863u$
    if (e.size() < kMasque.size() || sv(e).substr(e.size() - kMasque.size()) != kMasque) break;
    size_t k = e.size() - kMasque.size();
    while (k > 0 && Blanc(e[k - 1])) --k;
    if (k == 0 || e[k - 1] != '&') break;
    --k;
    while (k > 0 && Blanc(e[k - 1])) --k;
    const sv gauche = Rogner(sv(e).substr(0, k));
    int niveau = 0;
    bool equilibre = true;
    for (const char c : gauche) {
      niveau += (c == '(' || c == '[');
      niveau -= (c == ')' || c == ']');
      if (niveau < 0) equilibre = false;
    }
    if (!equilibre || niveau != 0) break;
    e = std::string(gauche);
  }
  return e;
}

// SHARED_WORD_RE : ^g_UboShared(?:_1)?\.v\[(\d+)u?\]\.([xyzw])$ -> décalage en octets.
std::optional<int> MotPartage(sv e) {
  size_t k = 0;
  if (!Commence(e, 0, "g_UboShared")) return std::nullopt;
  k = 11;
  if (Commence(e, k, "_1")) k += 2;
  if (!Commence(e, k, ".v[")) return std::nullopt;
  k += 3;
  const size_t debut = k;
  k = FinChiffres(e, k);
  if (k == debut) return std::nullopt;
  const int indice = Entier(e.substr(debut, k - debut));
  if (Commence(e, k, "u")) ++k;
  if (!Commence(e, k, "].") || k + 3 != e.size()) return std::nullopt;
  const size_t composante = sv("xyzw").find(e[k + 2]);
  if (composante == sv::npos) return std::nullopt;
  return indice * 16 + int(composante) * 4;
}

using Temporaires = std::map<std::string, std::string, std::less<>>;

// shared_word_offset() : un mot de texture ou d'échantillonneur lu tel quel dans SharedConstants.
std::optional<int> DecalagePartage(sv expr, const Temporaires& temps) {
  std::string e = SansMasque(expr);
  for (int tour = 0; tour < 3; ++tour) {
    const auto it = temps.find(e);
    if (it == temps.end()) break;
    e = SansMasque(it->second);
  }
  return MotPartage(e);
}

const std::map<std::string, std::string, std::less<>>& EchantillonneurDe() {
  static const std::map<std::string, std::string, std::less<>> m = {
      {"texture1D", "sampler1D"},           {"texture2D", "sampler2D"},
      {"texture3D", "sampler3D"},           {"textureCube", "samplerCube"},
      {"texture2DArray", "sampler2DArray"}, {"textureCubeArray", "samplerCubeArray"},
      {"texture2DMS", "sampler2DMS"},       {"texture2DMSArray", "sampler2DMSArray"},
      {"texture1DArray", "sampler1DArray"}, {"utexture2D", "usampler2D"},
      {"itexture2D", "isampler2D"},         {"utexture3D", "usampler3D"},
      {"utexture2DMS", "usampler2DMS"}};
  return m;
}

// Une ligne entière (sans son \n) de t à partir de i.
sv Ligne(sv t, size_t i, size_t& fin) {
  fin = t.find('\n', i);
  if (fin == sv::npos) fin = t.size();
  return t.substr(i, fin - i);
}

// Les mots entiers (\bNOM\b) d'un ensemble : rappel pour chaque occurrence, qui rend la suite du texte à copier.
template <typename Ensemble, typename Fn>
std::string ParMots(sv t, const Ensemble& noms, Fn&& fn) {
  std::string sortie;
  size_t pos = 0, i = 0;
  while (i < t.size()) {
    if (!Mot(t[i]) || (i > 0 && Mot(t[i - 1]))) {
      ++i;
      continue;
    }
    const size_t fin = FinMot(t, i);
    const sv mot = t.substr(i, fin - i);
    if (noms.find(mot) == noms.end()) {
      i = fin;
      continue;
    }
    sortie.append(t.substr(pos, i - pos));
    pos = i = fn(mot, i, fin, sortie);
  }
  sortie.append(t.substr(pos));
  return sortie;
}

bool ContientMot(sv t, sv mot) {
  for (size_t m = t.find(mot); m != sv::npos; m = t.find(mot, m + 1)) {
    const bool avant = m == 0 || !Mot(t[m - 1]);
    const bool apres = m + mot.size() >= t.size() || !Mot(t[m + mot.size()]);
    if (avant && apres) return true;
  }
  return false;
}

std::string RemplacerMot(sv t, sv mot, sv par) {
  std::string sortie;
  size_t i = 0;
  for (size_t m; (m = t.find(mot, i)) != sv::npos;) {
    const bool avant = m == 0 || !Mot(t[m - 1]);
    const bool apres = m + mot.size() >= t.size() || !Mot(t[m + mot.size()]);
    sortie.append(t.substr(i, m - i));
    sortie.append(avant && apres ? par : mot);
    i = m + mot.size();
  }
  sortie.append(t.substr(i));
  return sortie;
}

// Liaison fixe d'une texture : (type d'échantillonneur, mot de texture, mot d'échantillonneur ou -1).
using CleLiaison = std::tuple<std::string, int, int>;

struct Liaisons {
  std::map<CleLiaison, int> indices;
  std::vector<CleLiaison> ordre;
  std::optional<std::string> Lier(const std::string& type, int t, int s) {
    const CleLiaison cle{type, t, s};
    auto it = indices.find(cle);
    if (it == indices.end()) {
      if (indices.size() >= 32) return std::nullopt;
      it = indices.emplace(cle, int(ordre.size())).first;
      ordre.push_back(cle);
    }
    return fmt::format("dk_tex_b{}", it->second);
  }
};

// ^\s*uint (_\d+) = ([^;]+); (re.M), sur le texte d'origine.
Temporaires LireTemporaires(sv t) {
  Temporaires temps;
  size_t i = 0;
  while (i < t.size()) {
    size_t k = i;
    while (k < t.size() && Blanc(t[k])) ++k;
    size_t suite = sv::npos;
    if (Commence(t, k, "uint _")) {
      const size_t debut = k + 5;
      const size_t fin_nom = FinChiffres(t, debut + 1);
      if (fin_nom > debut + 1 && Commence(t, fin_nom, " = ")) {
        const size_t debut_val = fin_nom + 3;
        const size_t pv = t.find(';', debut_val);
        if (pv != sv::npos && pv > debut_val) {
          temps[std::string(t.substr(debut, fin_nom - debut))] =
              std::string(Rogner(t.substr(debut_val, pv - debut_val)));
          suite = pv + 1;
        }
      }
    }
    const size_t nl = t.find('\n', suite == sv::npos ? i : suite);
    if (nl == sv::npos) break;
    i = nl + 1;
  }
  return temps;
}

// Les tableaux de textures et d'échantillonneurs :
//   ^layout\(set = (\d+), binding = (\d+)\) uniform (\w+) (\w+)(\[\d*\])?;\n (re.M), retirés.
std::string RetirerDeclarations(sv t, std::map<std::string, std::string, std::less<>>& textures,
                                std::set<std::string, std::less<>>& echantillonneurs) {
  std::string sortie;
  size_t i = 0;
  while (i < t.size()) {
    size_t fin;
    const sv l = Ligne(t, i, fin);
    bool prise = false;
    if (fin < t.size() && Commence(l, 0, "layout(set = ")) {
      size_t k = FinChiffres(l, 13);
      if (k > 13 && Commence(l, k, ", binding = ")) {
        const size_t b = k + 12;
        k = FinChiffres(l, b);
        if (k > b && Commence(l, k, ") uniform ")) {
          const size_t d_type = k + 10;
          const size_t f_type = FinMot(l, d_type);
          if (f_type > d_type && Commence(l, f_type, " ")) {
            const size_t d_nom = f_type + 1;
            const size_t f_nom = FinMot(l, d_nom);
            size_t q = f_nom;
            if (Commence(l, q, "[")) {
              q = FinChiffres(l, q + 1);
              q = Commence(l, q, "]") ? q + 1 : sv::npos;
            }
            if (f_nom > d_nom && q != sv::npos && q + 1 == l.size() && l[q] == ';') {
              const std::string type(l.substr(d_type, f_type - d_type));
              const std::string nom(l.substr(d_nom, f_nom - d_nom));
              if (type == "sampler") {
                echantillonneurs.insert(nom);
              } else if (const auto it = EchantillonneurDe().find(type); it != EchantillonneurDe().end()) {
                textures[nom] = it->second;
              } else if (type.rfind("sampler", 0) == 0 || type.rfind("image", 0) == 0) {
                throw NonPris(fmt::format("combined sampler or image declaration {} {}", type, nom));
              } else {
                throw NonPris(fmt::format("unknown uniform declaration {} {}", type, nom));
              }
              prise = true;
            }
          }
        }
      }
    }
    if (!prise) sortie.append(t.substr(i, std::min(fin + 1, t.size()) - i));
    i = fin + 1;
  }
  return sortie;
}

// sampler2D(T[t], S[s]) -> dk_tex_bK (mots lus tels quels dans SharedConstants) ou sampler2D(dkh(t, s)).
// Motif : \b(u|i)?sampler(1D|2D|3D|Cube|2DArray|CubeArray|2DMS|2DMSArray|1DArray)\(
std::string Constructeurs(sv t, const std::map<std::string, std::string, std::less<>>& textures,
                          const std::set<std::string, std::less<>>& echantillonneurs, const Temporaires& temps,
                          Liaisons& liaisons) {
  static const std::set<std::string, std::less<>> kSuffixes = {"1D",   "2D",          "3D",   "Cube",      "2DArray",
                                                               "CubeArray", "2DMS", "2DMSArray", "1DArray"};
  std::string sortie;
  size_t pos = 0;
  size_t cherche = 0;
  while (true) {
    // Prochaine correspondance à partir de `cherche`.
    size_t debut = sv::npos, ouvre = 0;
    for (size_t p = t.find("sampler", cherche); p != sv::npos; p = t.find("sampler", p + 1)) {
      size_t d;
      if (p > 0 && (t[p - 1] == 'u' || t[p - 1] == 'i') && p - 1 >= cherche && (p == 1 || !Mot(t[p - 2]))) {
        d = p - 1;
      } else if (p == 0 || !Mot(t[p - 1])) {
        d = p;
      } else {
        continue;
      }
      const size_t f = FinMot(t, p + 7);
      if (f < t.size() && t[f] == '(' && kSuffixes.count(t.substr(p + 7, f - p - 7))) {
        debut = d;
        ouvre = f;
        break;
      }
    }
    if (debut == sv::npos) {
      sortie.append(t.substr(pos));
      return sortie;
    }
    const size_t ferme = Fermant(t, ouvre, '(', ')');
    const sv interieur = t.substr(ouvre + 1, ferme - ouvre - 1);
    // \s*(\w+)\s*\[
    size_t k = 0;
    while (k < interieur.size() && Blanc(interieur[k])) ++k;
    const size_t d_nom = k;
    const size_t f_nom = FinMot(interieur, k);
    size_t q = f_nom;
    while (q < interieur.size() && Blanc(interieur[q])) ++q;
    const bool table = f_nom > d_nom && q < interieur.size() && interieur[q] == '[' &&
                       textures.count(interieur.substr(d_nom, f_nom - d_nom));
    if (!table) {
      sortie.append(t.substr(pos, ferme + 1 - pos));
      pos = cherche = ferme + 1;
      continue;
    }
    const auto [t_expr, apres] = IndiceExige(interieur, f_nom);
    sv reste = RognerGauche(interieur.substr(apres));
    if (reste.empty() || reste[0] != ',')
      throw NonPris("sampler constructor without a sampler: " + std::string(interieur.substr(0, 80)));
    reste = RognerGauche(reste.substr(1));
    // (\w+)\s*\[
    const size_t f_s = FinMot(reste, 0);
    size_t qs = f_s;
    while (qs < reste.size() && Blanc(reste[qs])) ++qs;
    if (f_s == 0 || qs >= reste.size() || reste[qs] != '[' || !echantillonneurs.count(reste.substr(0, f_s)))
      throw NonPris("sampler constructor with a non-table sampler: " + std::string(interieur.substr(0, 80)));
    const auto [s_expr, s_apres] = IndiceExige(reste, f_s);
    if (!Rogner(reste.substr(s_apres)).empty())
      throw NonPris("unexpected sampler constructor arguments: " + std::string(interieur.substr(0, 80)));
    sortie.append(t.substr(pos, debut - pos));
    const std::string type(t.substr(debut, ouvre - debut));
    std::optional<std::string> var;
    const auto t_off = DecalagePartage(t_expr, temps);
    const auto s_off = DecalagePartage(s_expr, temps);
    if (t_off && s_off) var = liaisons.Lier(type, *t_off, *s_off);
    if (var) {
      sortie += *var;
    } else {
      sortie += type;
      sortie += "(dkh(";
      sortie.append(t_expr);
      sortie += ", ";
      sortie.append(s_expr);
      sortie += "))";
    }
    pos = cherche = ferme + 1;
  }
}

// layout\(push_constant, std430\) uniform (\w+) (re.M) -> bloc std140 en liaison 5, et ses membres vérifiés.
std::string ConstantesPoussees(sv t) {
  static constexpr sv kAvant = "layout(push_constant, std430) uniform ";
  std::string sortie;
  size_t i = 0, copie = 0;
  bool trouve = false;
  while (i < t.size()) {
    if (Commence(t, i, kAvant) && FinMot(t, i + kAvant.size()) > i + kAvant.size()) {
      sortie.append(t.substr(copie, i - copie));
      sortie += fmt::format("layout(std140, binding = {}) uniform ", kPushUboBinding);
      copie = i + kAvant.size();
      trouve = true;
    }
    const size_t nl = t.find('\n', i);
    if (nl == sv::npos) break;
    i = nl + 1;
  }
  sortie.append(t.substr(copie));
  if (!trouve) return sortie;
  // layout\(std140, binding = 5\) uniform \w+\n\{\n(.*?)\n\}
  const std::string entete = fmt::format("layout(std140, binding = {}) uniform ", kPushUboBinding);
  for (size_t m = sortie.find(entete); m != std::string::npos; m = sortie.find(entete, m + 1)) {
    const size_t f = FinMot(sortie, m + entete.size());
    if (f == m + entete.size() || !Commence(sortie, f, "\n{\n")) continue;
    const size_t corps = f + 3;
    const size_t fin = sortie.find("\n}", corps);
    if (fin == std::string::npos) break;
    sv membres = sv(sortie).substr(corps, fin - corps);
    for (size_t a = 0; a <= membres.size();) {
      size_t b = membres.find('\n', a);
      if (b == sv::npos) b = membres.size();
      const sv ligne = membres.substr(a, b - a);
      // \s*(?:layout\(offset = \d+\) )?(\w+) \w+(\[\d+\])?;
      size_t k = 0;
      while (k < ligne.size() && Blanc(ligne[k])) ++k;
      if (Commence(ligne, k, "layout(offset = ")) {
        const size_t c = FinChiffres(ligne, k + 16);
        if (c > k + 16 && Commence(ligne, c, ") ")) k = c + 2;
      }
      const size_t f_type = FinMot(ligne, k);
      bool ok = f_type > k && Commence(ligne, f_type, " ");
      size_t f_nom = ok ? FinMot(ligne, f_type + 1) : 0;
      ok = ok && f_nom > f_type + 1;
      bool tableau = false;
      if (ok && Commence(ligne, f_nom, "[")) {
        const size_t c = FinChiffres(ligne, f_nom + 1);
        ok = c > f_nom + 1 && Commence(ligne, c, "]");
        f_nom = c + 1;
        tableau = true;
      }
      ok = ok && Commence(ligne, f_nom, ";");
      if (!ok) throw NonPris("push constant member not understood: " + std::string(Rogner(ligne)));
      static const std::set<std::string, std::less<>> kScalaires = {"float", "uint",  "int",  "vec2",
                                                                   "uvec2", "ivec2", "bool"};
      if (tableau && kScalaires.count(ligne.substr(k, f_type - k)))
        throw NonPris("push constant array with a std430-only stride: " + std::string(Rogner(ligne)));
      a = b + 1;
    }
    break;
  }
  return sortie;
}

// layout\(set = (\d+), binding = (\d+), std140\) uniform -> layout(binding = K, std140) uniform (--ubo-map 5:0,6:1,7:2)
std::string TamponsConstants(sv t) {
  static constexpr sv kAvant = "layout(set = ";
  std::string sortie;
  size_t i = 0;
  for (size_t m; (m = t.find(kAvant, i)) != sv::npos;) {
    const size_t s0 = m + kAvant.size();
    const size_t s1 = FinChiffres(t, s0);
    bool ok = s1 > s0 && Commence(t, s1, ", binding = ");
    const size_t b0 = s1 + 12;
    const size_t b1 = ok ? FinChiffres(t, b0) : 0;
    ok = ok && b1 > b0 && Commence(t, b1, ", std140) uniform");
    if (!ok) {
      sortie.append(t.substr(i, m + 1 - i));
      i = m + 1;
      continue;
    }
    const int s = Entier(t.substr(s0, s1 - s0)), b = Entier(t.substr(b0, b1 - b0));
    if (b != 0 || s < 5 || s > 7)
      throw NonPris(fmt::format("constant buffer at set {} binding {} has no UBO binding (--ubo-map)", s, b));
    sortie.append(t.substr(i, m - i));
    sortie += fmt::format("layout(binding = {}, std140) uniform", s - 5);
    i = b1 + 17;
  }
  sortie.append(t.substr(i));
  return sortie;
}

// Composante d'un vecteur de tampon constant choisie à l'exécution : NOM.v[a][b] -> dk_pick_T(NOM.v[a], uint(b)).
std::string ChoixComposantes(std::string t) {
  // uniform \w+\n\{\n\s*(u?vec4|ivec4) v\[\d+\];\n\} (\w+);
  std::map<std::string, std::string, std::less<>> blocs;
  for (size_t m = t.find("uniform "); m != std::string::npos; m = t.find("uniform ", m + 1)) {
    size_t k = FinMot(t, m + 8);
    if (k == m + 8 || !Commence(t, k, "\n{\n")) continue;
    k += 3;
    while (k < t.size() && Blanc(t[k])) ++k;
    std::string type;
    for (const char* c : {"uvec4", "ivec4", "vec4"}) {
      if (Commence(t, k, c) && Commence(t, k + std::char_traits<char>::length(c), " v[")) {
        type = c;
        break;
      }
    }
    if (type.empty()) continue;
    k += type.size() + 3;
    const size_t c = FinChiffres(t, k);
    if (c == k || !Commence(t, c, "];\n} ")) continue;
    const size_t d_nom = c + 5;
    const size_t f_nom = FinMot(t, d_nom);
    if (f_nom == d_nom || !Commence(t, f_nom, ";")) continue;
    blocs[t.substr(d_nom, f_nom - d_nom)] = type;
  }
  if (blocs.empty()) return t;
  std::set<std::string> choix;
  std::string sortie;
  size_t pos = 0, i = 0;
  while (i < t.size()) {
    if (!Mot(t[i]) || (i > 0 && Mot(t[i - 1]))) {
      ++i;
      continue;
    }
    const size_t fin = FinMot(t, i);
    const auto it = blocs.find(sv(t).substr(i, fin - i));
    if (it == blocs.end() || !Commence(t, fin, ".v[")) {
      i = fin;
      continue;
    }
    const size_t ouvre1 = fin + 2;
    const size_t ferme1 = Fermant(t, ouvre1, '[', ']');
    const size_t k = ferme1 + 1;
    if (k < t.size() && t[k] == '[') {
      const size_t ferme2 = Fermant(t, k, '[', ']');
      const sv comp = Rogner(sv(t).substr(k + 1, ferme2 - k - 1));
      size_t c = FinChiffres(comp, 0);
      if (c > 0 && c < comp.size() && comp[c] == 'u') ++c;
      if (c == 0 || c != comp.size()) {
        choix.insert(it->second);
        sortie.append(t, pos, i - pos);
        sortie += fmt::format("dk_pick_{}({}, uint({}))", it->second, sv(t).substr(i, ferme1 + 1 - i), comp);
        pos = i = ferme2 + 1;
        continue;
      }
    }
    sortie.append(t, pos, ferme1 + 1 - pos);
    pos = i = ferme1 + 1;
  }
  sortie.append(t, pos);
  if (choix.empty()) return sortie;
  static const std::map<std::string, std::string> kScalaire = {
      {"vec4", "float"}, {"uvec4", "uint"}, {"ivec4", "int"}};
  std::string aides;
  for (const auto& type : choix)
    aides += fmt::format("{} dk_pick_{}({} v, uint i) {{ return v[i]; }}\n", kScalaire.at(type), type, type);
  const size_t at = sortie.find("\nvoid main()");
  if (at == std::string::npos) throw NonPris("no main() for the component pick helpers");
  sortie.insert(at + 1, aides);
  return sortie;
}

// Un bloc de stockage :
//   layout\(set = (\d+), binding = (\d+), std430\) (readonly |writeonly |restrict )*buffer (\w+)\n\{(.*?)\n\} (\w+)(\[(\d+)\])?;
// Comme dans le script, seul le dernier qualificatif répété est gardé (groupe 3 de Python).
struct BlocStockage {
  size_t debut, fin;
  int set, binding;
  std::string qualif, nom, corps, instance, tableau;
  int elements;
};

std::optional<BlocStockage> LireBloc(sv t, size_t m) {
  static constexpr sv kAvant = "layout(set = ";
  BlocStockage b{};
  b.debut = m;
  size_t k = m + kAvant.size();
  size_t c = FinChiffres(t, k);
  if (c == k || !Commence(t, c, ", binding = ")) return std::nullopt;
  b.set = Entier(t.substr(k, c - k));
  k = c + 12;
  c = FinChiffres(t, k);
  if (c == k || !Commence(t, c, ", std430) ")) return std::nullopt;
  b.binding = Entier(t.substr(k, c - k));
  k = c + 10;
  for (bool encore = true; encore;) {
    encore = false;
    for (const char* q : {"readonly ", "writeonly ", "restrict "}) {
      if (Commence(t, k, q)) {
        b.qualif = q;
        k += std::char_traits<char>::length(q);
        encore = true;
      }
    }
  }
  if (!Commence(t, k, "buffer ")) return std::nullopt;
  k += 7;
  c = FinMot(t, k);
  if (c == k || !Commence(t, c, "\n{")) return std::nullopt;
  b.nom = std::string(t.substr(k, c - k));
  const size_t corps = c + 2;
  for (size_t f = t.find("\n} ", corps); f != sv::npos; f = t.find("\n} ", f + 1)) {
    const size_t d_inst = f + 3;
    const size_t f_inst = FinMot(t, d_inst);
    if (f_inst == d_inst) continue;
    size_t q = f_inst;
    std::string tableau;
    int elements = 0;
    if (Commence(t, q, "[")) {
      const size_t e = FinChiffres(t, q + 1);
      if (e > q + 1 && Commence(t, e, "]")) {
        tableau = std::string(t.substr(q, e + 1 - q));
        elements = Entier(t.substr(q + 1, e - q - 1));
        q = e + 1;
      }
    }
    if (!Commence(t, q, ";")) continue;
    b.corps = std::string(t.substr(corps, f - corps));
    b.instance = std::string(t.substr(d_inst, f_inst - d_inst));
    b.tableau = std::move(tableau);
    b.elements = elements;
    b.fin = q + 1;
    return b;
  }
  return std::nullopt;
}

std::string TamponsStockage(sv t) {
  std::vector<BlocStockage> blocs;
  for (size_t m = t.find("layout(set = "); m != sv::npos;) {
    if (auto b = LireBloc(t, m)) {
      blocs.push_back(*b);
      m = t.find("layout(set = ", b->fin);
    } else {
      m = t.find("layout(set = ", m + 1);
    }
  }
  std::vector<std::tuple<int, int, size_t>> trouves;
  for (const auto& b : blocs) trouves.emplace_back(b.set, b.binding, b.debut);
  std::sort(trouves.begin(), trouves.end());
  std::map<std::pair<int, int>, int> places;
  int suivante = 0;
  for (const auto& [s, b, debut] : trouves) {
    if (s == kRawWindowSet) {
      places[{s, b}] = b;
      continue;
    }
    places[{s, b}] = suivante;
    // Le premier bloc de ce (set, binding) dans le texte.
    for (const auto& bl : blocs) {
      if (bl.set == s && bl.binding == b) {
        suivante += bl.tableau.empty() ? 1 : bl.elements;
        break;
      }
    }
  }
  if (suivante > 15) throw NonPris(fmt::format("{} storage buffers", suivante));
  std::string sortie;
  size_t pos = 0;
  for (const auto& b : blocs) {
    sortie.append(t.substr(pos, b.debut - pos));
    sortie += fmt::format("layout(binding = {}, std430) {}buffer {}\n{{{}\n}} {}{};", places[{b.set, b.binding}],
                          b.qualif, b.nom, b.corps, b.instance, b.tableau);
    pos = b.fin;
  }
  sortie.append(t.substr(pos));
  return sortie;
}

// ^layout\(constant_id = (\d+)\) const (\w+) (\w+) = ([^;]+); (re.M) -> défaut par #ifndef SPIRV_CROSS_CONSTANT_ID_n.
std::string ConstantesSpecialisation(sv t) {
  static constexpr sv kAvant = "layout(constant_id = ";
  std::string sortie;
  size_t i = 0, copie = 0;
  while (i < t.size()) {
    size_t suite = sv::npos;
    if (Commence(t, i, kAvant)) {
      const size_t d_n = i + kAvant.size();
      const size_t f_n = FinChiffres(t, d_n);
      if (f_n > d_n && Commence(t, f_n, ") const ")) {
        const size_t d_t = f_n + 8, f_t = FinMot(t, d_t);
        const size_t d_v = f_t + 1, f_v = f_t < t.size() && t[f_t] == ' ' ? FinMot(t, d_v) : 0;
        if (f_t > d_t && f_v > d_v && Commence(t, f_v, " = ")) {
          const size_t d_x = f_v + 3;
          const size_t pv = t.find(';', d_x);
          if (pv != sv::npos && pv > d_x) {
            const sv n = t.substr(d_n, f_n - d_n);
            sortie.append(t.substr(copie, i - copie));
            sortie += fmt::format(
                "#ifndef SPIRV_CROSS_CONSTANT_ID_{0}\n#define SPIRV_CROSS_CONSTANT_ID_{0} {1}\n#endif\n"
                "const {2} {3} = SPIRV_CROSS_CONSTANT_ID_{0};",
                n, t.substr(d_x, pv - d_x), t.substr(d_t, f_t - d_t), t.substr(d_v, f_v - d_v));
            copie = suite = pv + 1;
          }
        }
      }
    }
    const size_t nl = t.find('\n', suite == sv::npos ? i : suite);
    if (nl == sv::npos) break;
    i = nl + 1;
  }
  sortie.append(t.substr(copie));
  return sortie;
}

// ^layout\(location = (\d+)\) in (\w+) (\w+)(\[\d+\])?; (re.M) : les entrées renumérotées 0..15 dans l'ordre.
std::string EntreesSommets(sv t, std::vector<int>& vin) {
  static constexpr sv kAvant = "layout(location = ";
  struct Entree {
    size_t debut, fin;
    int location;
    std::string type, nom;
  };
  std::vector<Entree> entrees;
  size_t i = 0;
  while (i < t.size()) {
    size_t suite = sv::npos;
    if (Commence(t, i, kAvant)) {
      const size_t d = i + kAvant.size(), f = FinChiffres(t, d);
      if (f > d && Commence(t, f, ") in ")) {
        const size_t d_t = f + 5, f_t = FinMot(t, d_t);
        const size_t d_n = f_t + 1, f_n = f_t < t.size() && t[f_t] == ' ' ? FinMot(t, d_n) : 0;
        if (f_t > d_t && f_n > d_n) {
          size_t q = f_n;
          bool tableau = false;
          if (Commence(t, q, "[")) {
            const size_t c = FinChiffres(t, q + 1);
            if (c > q + 1 && Commence(t, c, "]")) {
              q = c + 1;
              tableau = true;
            }
          }
          if (Commence(t, q, ";")) {
            if (tableau) throw NonPris("vertex input array");
            entrees.push_back({i, q + 1, Entier(t.substr(d, f - d)), std::string(t.substr(d_t, f_t - d_t)),
                               std::string(t.substr(d_n, f_n - d_n))});
            suite = q + 1;
          }
        }
      }
    }
    const size_t nl = t.find('\n', suite == sv::npos ? i : suite);
    if (nl == sv::npos) break;
    i = nl + 1;
  }
  vin.clear();
  for (const auto& e : entrees) vin.push_back(e.location);
  std::sort(vin.begin(), vin.end());
  if (vin.size() > 16) throw NonPris(fmt::format("{} vertex inputs", vin.size()));
  std::map<int, int> nouvelle;
  for (size_t k = 0; k < vin.size(); ++k) nouvelle[vin[k]] = int(k);
  std::string sortie;
  size_t pos = 0;
  for (const auto& e : entrees) {
    sortie.append(t.substr(pos, e.debut - pos));
    sortie += fmt::format("layout(location = {}) in {} {};", nouvelle[e.location], e.type, e.nom);
    pos = e.fin;
  }
  sortie.append(t.substr(pos));
  return sortie;
}

// layout\((?:std140, )?binding = (\d+)(?:, std140)?\) uniform
uint32_t MasqueUbo(sv t) {
  uint32_t masque = 0;
  for (size_t m = t.find("layout("); m != sv::npos; m = t.find("layout(", m + 1)) {
    size_t k = m + 7;
    if (Commence(t, k, "std140, ")) k += 8;
    if (!Commence(t, k, "binding = ")) continue;
    k += 10;
    const size_t c = FinChiffres(t, k);
    if (c == k) continue;
    size_t q = c;
    if (Commence(t, q, ", std140")) q += 8;
    if (!Commence(t, q, ") uniform")) continue;
    masque |= 1u << Entier(t.substr(k, c - k));
  }
  return masque;
}

// layout\(binding = (\d+), std430\) (?:\w+ )*buffer \w+\n\{.*?\n\} \w+(?:\[(\d+)\])?; (re.S)
uint32_t MasqueSsbo(sv t) {
  uint32_t masque = 0;
  static constexpr sv kAvant = "layout(binding = ";
  for (size_t m = t.find(kAvant); m != sv::npos;) {
    size_t k = m + kAvant.size();
    const size_t c = FinChiffres(t, k);
    size_t suite = m + 1;
    if (c > k && Commence(t, c, ", std430) ")) {
      const int liaison = Entier(t.substr(k, c - k));
      size_t q = c + 10;
      // (?:\w+ )*buffer \w+\n\{ : des mots suivis d'une espace, dont le dernier avant le nom est « buffer ».
      size_t corps = sv::npos;
      for (size_t w = q;;) {
        const size_t f = FinMot(t, w);
        if (f == w || !Commence(t, f, " ")) break;
        if (t.substr(w, f - w) == "buffer") {
          const size_t f_nom = FinMot(t, f + 1);
          if (f_nom > f + 1 && Commence(t, f_nom, "\n{")) {
            corps = f_nom + 2;
            break;
          }
        }
        w = f + 1;
      }
      if (corps != sv::npos) {
        for (size_t f = t.find("\n} ", corps); f != sv::npos; f = t.find("\n} ", f + 1)) {
          const size_t f_inst = FinMot(t, f + 3);
          if (f_inst == f + 3) continue;
          size_t e = f_inst;
          int elements = 1;
          if (Commence(t, e, "[")) {
            const size_t d = FinChiffres(t, e + 1);
            if (d > e + 1 && Commence(t, d, "]")) {
              elements = Entier(t.substr(e + 1, d - e - 1));
              e = d + 1;
            }
          }
          if (!Commence(t, e, ";")) continue;
          for (int n = 0; n < elements; ++n) masque |= 1u << (liaison + n);
          suite = e + 1;
          break;
        }
      }
    }
    m = t.find(kAvant, suite);
  }
  return masque;
}

}  // namespace

bool Convertir(sv vk, bool vertex, std::string& sortie, std::string& erreur) {
  try {
    std::string src;
    src.reserve(vk.size());
    for (size_t i = 0; i < vk.size(); ++i) {
      if (vk[i] == '\r' && i + 1 < vk.size() && vk[i + 1] == '\n') continue;
      src += vk[i];
    }
    const Temporaires temps = LireTemporaires(vk);
    Liaisons liaisons;
    std::map<std::string, std::string, std::less<>> textures;
    std::set<std::string, std::less<>> echantillonneurs;

    std::string t = RetirerDeclarations(src, textures, echantillonneurs);
    t = SansNonUniform(t);

    if (!textures.empty()) {
      t = Constructeurs(t, textures, echantillonneurs, temps, liaisons);
      // T[t] seul (texelFetch, textureSize, textureQueryLevels) -> sampler2D(uvec2(uint(t), 0u)).
      t = ParMots(t, textures, [&](sv nom, size_t, size_t fin, std::string& out) {
        const auto idx = IndiceTableau(t, fin);
        if (!idx) throw NonPris(fmt::format("texture table {} used without an index", nom));
        const auto [expr, apres] = *idx;
        const std::string& type = textures.find(nom)->second;
        std::optional<std::string> var;
        if (const auto t_off = DecalagePartage(expr, temps)) var = liaisons.Lier(type, *t_off, -1);
        out += var ? *var : fmt::format("{}(uvec2(uint({}), 0u))", type, SansMasque(expr));
        return apres;
      });
    }
    for (const auto& s : echantillonneurs) {
      if (ContientMot(t, s)) throw NonPris(fmt::format("sampler table {} used outside a sampler constructor", s));
    }

    t = ConstantesPoussees(t);
    t = TamponsConstants(t);
    t = ChoixComposantes(std::move(t));
    t = TamponsStockage(t);
    t = ConstantesSpecialisation(t);

    t = RemplacerMot(t, "gl_VertexIndex", "gl_VertexID");
    t = RemplacerMot(t, "gl_InstanceIndex", "gl_InstanceID");
    for (const sv mauvais : {"gl_BaseInstance", "gl_BaseVertex", "subpassInput", "spirv_instruction",
                             "buffer_reference", "push_constant", "layout(set =", "GL_EXT_buffer_reference",
                             "gl_DrawID"}) {
      if (t.find(mauvais) != std::string::npos) throw NonPris(fmt::format("{} remains", mauvais));
    }

    std::vector<int> vin;
    if (vertex) t = EntreesSommets(t, vin);

    const uint32_t ubo = MasqueUbo(t);
    const uint32_t ssbo = MasqueSsbo(t);
    const bool sans_liaison = !textures.empty() && (t.find("dkh(") != std::string::npos ||
                                                    t.find("uvec2(uint(") != std::string::npos);
    std::string meta = fmt::format("// banjo-dk ubo=0x{:x} ssbo=0x{:x} tex={}", ubo, ssbo, sans_liaison ? 1 : 0);
    if (!vin.empty()) {
      meta += " vin=";
      for (size_t k = 0; k < vin.size(); ++k) meta += fmt::format("{}{}", k ? "," : "", vin[k]);
    }
    if (!liaisons.ordre.empty()) {
      meta += " texb=";
      for (size_t k = 0; k < liaisons.ordre.size(); ++k) {
        const auto& [type, to, so] = liaisons.ordre[k];
        meta += fmt::format("{}{}:{}:{}", k ? "," : "", k, to, so < 0 ? 65535 : so);
      }
    }

    // Les extensions retirées, puis les lignes ajoutées après #version.
    static constexpr sv kRetirees[] = {"GL_EXT_nonuniform_qualifier", "GL_EXT_samplerless_texture_functions",
                                       "GL_EXT_spirv_intrinsics", "GL_EXT_scalar_block_layout"};
    std::vector<sv> lignes;
    for (size_t a = 0; a <= t.size();) {
      size_t b = t.find('\n', a);
      if (b == std::string::npos) b = t.size();
      const sv l = sv(t).substr(a, b - a);
      bool retiree = false;
      for (const sv e : kRetirees) {
        if (Commence(l, 0, "#extension ") && Commence(l, 11, e)) retiree = true;
      }
      if (!retiree) lignes.push_back(l);
      a = b + 1;
    }
    if (lignes.empty() || !Commence(lignes[0], 0, "#version")) throw NonPris("no #version line");
    std::vector<std::string> ajout = {meta};
    if (sans_liaison) {
      ajout.emplace_back("#extension GL_ARB_bindless_texture : require");
      ajout.emplace_back("#define dkh(t, s) uvec2((uint(t) & 0xFFFFFu) | (uint(s) & 0xFFF00000u), 0u)");
    }
    for (size_t k = 0; k < liaisons.ordre.size(); ++k)
      ajout.push_back(fmt::format("layout(binding = {}) uniform {} dk_tex_b{};", k, std::get<0>(liaisons.ordre[k]), k));

    sortie.clear();
    sortie.reserve(t.size() + 512);
    for (size_t k = 0; k < lignes.size(); ++k) {
      if (k) sortie += '\n';
      sortie.append(lignes[k]);
      if (k == 0) {
        for (const auto& a : ajout) {
          sortie += '\n';
          sortie += a;
        }
      }
    }
    return true;
  } catch (const NonPris& e) {
    erreur = e.what();
    return false;
  }
}

}  // namespace opdarkness::deko3d_glsl

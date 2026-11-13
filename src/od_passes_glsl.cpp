// opdarkness - passes du GLSL final pour uam (od_passes_glsl.h), traduction fidèle de tools/renut/deko3d_glsl_opt.py.
// Chaque expression régulière du script est réécrite à la main ; les commentaires en rappellent la forme.

#include "od_passes_glsl.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace opdarkness::passes_glsl {
namespace {

constexpr size_t npos = std::string::npos;

// \s de Python pour un texte ASCII (str.strip enlève les mêmes).
bool Blanc(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }
bool Chiffre(char c) { return c >= '0' && c <= '9'; }
// \w
bool Mot(char c) { return Chiffre(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }

std::string Strip(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && Blanc(s[a])) ++a;
  while (b > a && Blanc(s[b - 1])) --b;
  return s.substr(a, b - a);
}
std::string RStrip(const std::string& s) {
  size_t b = s.size();
  while (b > 0 && Blanc(s[b - 1])) --b;
  return s.substr(0, b);
}
// Les blancs de tête (s[:len(s) - len(s.lstrip())]).
std::string Tete(const std::string& s) {
  size_t a = 0;
  while (a < s.size() && Blanc(s[a])) ++a;
  return s.substr(0, a);
}
bool Commence(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

std::vector<std::string> Lignes(const std::string& s) {
  std::vector<std::string> l;
  size_t a = 0;
  for (;;) {
    const size_t b = s.find('\n', a);
    if (b == npos) {
      l.push_back(s.substr(a));
      return l;
    }
    l.push_back(s.substr(a, b - a));
    a = b + 1;
  }
}
std::string Joindre(const std::vector<std::string>& l) {
  std::string s;
  for (size_t i = 0; i < l.size(); ++i) {
    if (i) s += '\n';
    s += l[i];
  }
  return s;
}

// matching() : l'indice de la fermante qui répond à l'ouvrante de `debut`, ou npos.
size_t Fermante(const std::string& t, size_t debut, char ouvre, char ferme) {
  int profondeur = 0;
  for (size_t i = debut; i < t.size(); ++i) {
    if (t[i] == ouvre) {
      ++profondeur;
    } else if (t[i] == ferme) {
      if (--profondeur == 0) return i;
    }
  }
  return npos;
}

// main_span() : [a, b) du corps de main().
bool CorpsMain(const std::string& t, size_t& a, size_t& b) {
  static constexpr char kMain[] = "\nvoid main()\n{";
  const size_t m = t.find(kMain);
  if (m == npos) return false;
  const size_t ouvre = m + sizeof(kMain) - 2;
  const size_t ferme = Fermante(t, ouvre, '{', '}');
  if (ferme == npos) return false;
  a = ouvre + 1;
  b = ferme;
  return true;
}

std::string AvantMain(const std::string& t, const std::string& code) {
  const size_t at = t.find("\nvoid main()");
  if (at == npos) return t;
  return t.substr(0, at + 1) + code + t.substr(at + 1);
}

template <class F>
std::string SurMain(const std::string& t, F&& f) {
  size_t a, b;
  if (!CorpsMain(t, a, b)) return t;
  return t.substr(0, a) + f(t.substr(a, b - a)) + t.substr(b);
}

// `_\d+` à `i` : la fin des chiffres, ou npos (pas de `_` suivi d'au moins un chiffre).
size_t FinVariable(const std::string& s, size_t i) {
  if (i + 1 >= s.size() || s[i] != '_' || !Chiffre(s[i + 1])) return npos;
  size_t j = i + 1;
  while (j < s.size() && Chiffre(s[j])) ++j;
  return j;
}
// (?<![\w.]) à i
bool PasApresMotOuPoint(const std::string& s, size_t i) { return i == 0 || !(Mot(s[i - 1]) || s[i - 1] == '.'); }

// ------------------------------------------------------------------------------------------------------------------
// vsout

// ^layout\(location = (\d+)\) out (\w+) (\w+);$ sur une ligne : (emplacement, nom).
bool LigneSortie(const std::string& l, int& emplacement, std::string& nom) {
  static constexpr char kTete[] = "layout(location = ";
  if (!Commence(l, kTete)) return false;
  size_t i = sizeof(kTete) - 1, j = i;
  while (j < l.size() && Chiffre(l[j])) ++j;
  if (j == i || l.compare(j, 6, ") out ") != 0) return false;
  long v = 0;
  for (size_t k = i; k < j; ++k) v = v * 10 + (l[k] - '0'), v = v > 100000 ? 100000 : v;
  size_t t = j + 6, u = t;
  while (u < l.size() && Mot(l[u])) ++u;
  if (u == t || u >= l.size() || l[u] != ' ') return false;
  size_t n = u + 1, w = n;
  while (w < l.size() && Mot(l[w])) ++w;
  if (w == n || w + 1 != l.size() || l[w] != ';') return false;
  emplacement = int(v);
  nom = l.substr(n, w - n);
  return true;
}

// (?<![\w.])nom\b
size_t Usages(const std::string& corps, const std::string& nom) {
  size_t n = 0;
  for (size_t p = corps.find(nom); p != npos; p = corps.find(nom, p + 1)) {
    const size_t f = p + nom.size();
    if (PasApresMotOuPoint(corps, p) && (f >= corps.size() || !Mot(corps[f]))) ++n;
  }
  return n;
}

// ^(\s*)(nom(?:\.[xyzw]+)?\s*=[^=][^;]*;)$ essayé au début de ligne p : fin des blancs de tête et fin de l'écriture.
bool Ecriture(const std::string& c, size_t p, const std::string& nom, size_t& blancs, size_t& fin) {
  size_t i = p;
  while (i < c.size() && Blanc(c[i])) ++i;
  if (c.compare(i, nom.size(), nom) != 0) return false;
  blancs = i;
  i += nom.size();
  if (i < c.size() && c[i] == '.') {
    size_t j = i + 1;
    while (j < c.size() && (c[j] == 'x' || c[j] == 'y' || c[j] == 'z' || c[j] == 'w')) ++j;
    if (j > i + 1) i = j;
  }
  while (i < c.size() && Blanc(c[i])) ++i;
  if (i + 1 >= c.size() || c[i] != '=' || c[i + 1] == '=') return false;
  const size_t pv = c.find(';', i + 2);
  if (pv == npos || !(pv + 1 == c.size() || c[pv + 1] == '\n')) return false;
  fin = pv + 1;
  return true;
}

// Les écritures de `nom` dans l'ordre (finditer : on reprend après chaque écriture trouvée).
std::vector<std::pair<size_t, std::pair<size_t, size_t>>> Ecritures(const std::string& c, const std::string& nom) {
  std::vector<std::pair<size_t, std::pair<size_t, size_t>>> r;
  size_t reprise = 0;
  for (size_t p = 0; p <= c.size();) {
    if (p >= reprise && (p == 0 || c[p - 1] == '\n')) {
      size_t blancs, fin;
      if (Ecriture(c, p, nom, blancs, fin)) {
        r.push_back({p, {blancs, fin}});
        reprise = fin;
        p = fin;
        continue;
      }
    }
    const size_t nl = c.find('\n', p);
    if (nl == npos) break;
    p = nl + 1;
  }
  return r;
}

}  // namespace

std::string VsOut(const std::string& texte) {
  std::vector<std::pair<int, std::string>> sorties;
  for (const std::string& l : Lignes(texte)) {
    int e;
    std::string nom;
    if (LigneSortie(l, e, nom) && e < 32) sorties.push_back({e, nom});
  }
  if (sorties.empty()) return texte;
  size_t a, b;
  if (!CorpsMain(texte, a, b)) return texte;
  std::string corps = texte.substr(a, b - a);
  int faites = 0;
  for (const auto& [e, nom] : sorties) {
    const auto ecr = Ecritures(corps, nom);
    if (ecr.empty() || ecr.size() != Usages(corps, nom)) continue;
    std::string neuf;
    size_t avant = 0;
    for (const auto& [p, bf] : ecr) {
      neuf += corps.substr(avant, p - avant);
      neuf += corps.substr(p, bf.first - p);
      neuf += "if ((g_banjoVsOutputs & " + std::to_string(1ull << e) + "u) != 0u) { ";
      neuf += corps.substr(bf.first, bf.second - bf.first);
      neuf += " }";
      avant = bf.second;
    }
    neuf += corps.substr(avant);
    corps = std::move(neuf);
    ++faites;
  }
  if (!faites) return texte;
  return AvantMain(texte.substr(0, a) + corps + texte.substr(b),
                   "#ifndef SPIRV_CROSS_CONSTANT_ID_905\n#define SPIRV_CROSS_CONSTANT_ID_905 4294967295u\n#endif\n"
                   "const uint g_banjoVsOutputs = SPIRV_CROSS_CONSTANT_ID_905;\n");
}

// ------------------------------------------------------------------------------------------------------------------
// pred

namespace {

struct Bloc {
  size_t retrait;
  std::string var;  // vide : bloc sans condition connue
  bool val;
};

// ^(\s*)(?:else )?if \((!?)(_\d+)\)$
bool LigneSi(const std::string& l, std::string& blancs, bool& non, std::string& var) {
  size_t i = 0;
  while (i < l.size() && Blanc(l[i])) ++i;
  blancs = l.substr(0, i);
  if (l.compare(i, 5, "else ") == 0 && l.compare(i + 5, 4, "if (") == 0) i += 5;
  if (l.compare(i, 4, "if (") != 0) return false;
  i += 4;
  non = i < l.size() && l[i] == '!';
  if (non) ++i;
  const size_t f = FinVariable(l, i);
  if (f == npos || f + 1 != l.size() || l[f] != ')') return false;
  var = l.substr(i, f - i);
  return true;
}

}  // namespace

std::string Pred(const std::string& texte) {
  size_t a, b;
  if (!CorpsMain(texte, a, b)) return texte;
  std::vector<std::string> lignes = Lignes(texte.substr(a, b - a));
  // ^\s*bool (_\d+)\b
  std::vector<std::string> bools;
  auto EstBool = [&](const std::string& v) {
    for (const std::string& x : bools)
      if (x == v) return true;
    return false;
  };
  for (const std::string& l : lignes) {
    size_t i = 0;
    while (i < l.size() && Blanc(l[i])) ++i;
    if (l.compare(i, 5, "bool ") != 0) continue;
    const size_t f = FinVariable(l, i + 5);
    if (f == npos || (f < l.size() && Mot(l[f]))) continue;
    const std::string v = l.substr(i + 5, f - i - 5);
    if (!EstBool(v)) bools.push_back(v);
  }
  if (bools.empty()) return texte;
  const size_t n_lignes = lignes.size();
  std::vector<size_t> fermeture(n_lignes, npos);  // close_of
  {
    std::vector<size_t> ouverts;
    for (size_t n = 0; n < n_lignes; ++n) {
      const std::string st = Strip(lignes[n]);
      if (st == "{") {
        ouverts.push_back(n);
      } else if ((st == "}" || Commence(st, "} while")) && !ouverts.empty()) {
        fermeture[ouverts.back()] = n;
        ouverts.pop_back();
      }
    }
  }
  // (?<![\w.])(_\d+)\s*=[^=] : les lignes qui écrivent chaque bool
  std::vector<std::pair<std::string, size_t>> ecrit;
  for (size_t n = 0; n < n_lignes; ++n) {
    const std::string& l = lignes[n];
    for (size_t i = 0; i < l.size();) {
      const size_t f = PasApresMotOuPoint(l, i) ? FinVariable(l, i) : npos;
      if (f == npos) {
        ++i;
        continue;
      }
      size_t k = f;
      while (k < l.size() && Blanc(l[k])) ++k;
      if (k + 1 < l.size() && l[k] == '=' && l[k + 1] != '=') {
        const std::string v = l.substr(i, f - i);
        if (EstBool(v)) ecrit.push_back({v, n});
        i = k + 2;
        continue;
      }
      ++i;
    }
  }
  auto Stable = [&](const std::string& var, size_t ouverture) {
    const size_t fin = fermeture[ouverture] == npos ? n_lignes : fermeture[ouverture];
    for (const auto& [v, k] : ecrit)
      if (v == var && ouverture < k && k < fin) return false;
    return true;
  };

  std::vector<Bloc> pile;
  bool attente = false;
  Bloc attendu{};
  std::vector<std::pair<size_t, std::pair<std::string, bool>>> dernier_si;  // last_if : retrait -> (var, val)
  int changes = 0;
  for (size_t n = 0; n < n_lignes; ++n) {
    const std::string ligne = lignes[n];
    const std::string st = Strip(ligne);
    const size_t retrait = Tete(ligne).size();
    auto Connu = [&](const std::string& v, bool& val) {
      for (size_t k = pile.size(); k-- > 0;)
        if (!pile[k].var.empty() && pile[k].var == v) {
          val = pile[k].val;
          return true;
        }
      return false;
    };
    bool connus = false;
    for (const Bloc& bl : pile) connus |= !bl.var.empty();
    std::string blancs, var;
    bool non = false;
    const bool si = LigneSi(ligne, blancs, non, var);
    bool val;
    if (si && Connu(var, val)) {
      const bool v = val != non;
      lignes[n] = blancs + (Commence(st, "else") ? "else " : "") + "if (" + (v ? "true" : "false") + ")";
      ++changes;
    }
    if (si && EstBool(var)) {
      attente = true;
      attendu = {retrait, var, !non};
      continue;
    }
    if (st == "else") {
      bool trouve = false;
      for (size_t k = 0; k < dernier_si.size(); ++k)
        if (dernier_si[k].first == retrait) {
          attente = true;
          attendu = {retrait, dernier_si[k].second.first, !dernier_si[k].second.second};
          dernier_si.erase(dernier_si.begin() + long(k));
          trouve = true;
          break;
        }
      if (trouve) continue;
    }
    if (st == "{") {
      if (attente && attendu.retrait == retrait && Stable(attendu.var, n))
        pile.push_back(attendu);
      else
        pile.push_back({retrait, {}, false});
      attente = false;
      continue;
    }
    attente = false;
    if (st == "}" || Commence(st, "} while")) {
      if (!pile.empty()) {
        const Bloc bl = pile.back();
        pile.pop_back();
        if (!bl.var.empty() && st == "}") {
          bool remplace = false;
          for (auto& d : dernier_si)
            if (d.first == bl.retrait) d.second = {bl.var, bl.val}, remplace = true;
          if (!remplace) dernier_si.push_back({bl.retrait, {bl.var, bl.val}});
        }
      }
      continue;
    }
    for (size_t k = dernier_si.size(); k-- > 0;)
      if (!(dernier_si[k].first < retrait)) dernier_si.erase(dernier_si.begin() + long(k));
    if (connus) {
      // (?<![\w.])(_\d+)(?![\w\[])(?!\s*=[^=])
      std::string neuf;
      const std::string& l = ligne;
      size_t i = 0, copie = 0;
      while (i < l.size()) {
        const size_t f = PasApresMotOuPoint(l, i) ? FinVariable(l, i) : npos;
        if (f == npos) {
          ++i;
          continue;
        }
        bool ok = !(f < l.size() && (Mot(l[f]) || l[f] == '['));
        if (ok) {
          size_t k = f;
          while (k < l.size() && Blanc(l[k])) ++k;
          if (k + 1 < l.size() && l[k] == '=' && l[k + 1] != '=') ok = false;
        }
        bool v;
        if (ok && Connu(l.substr(i, f - i), v)) {
          neuf += l.substr(copie, i - copie);
          neuf += v ? "true" : "false";
          copie = f;
        }
        i = ok ? f : i + 1;
      }
      neuf += l.substr(copie);
      if (neuf != l) {
        lignes[n] = neuf;
        ++changes;
      }
    }
  }
  if (!changes) return texte;
  return texte.substr(0, a) + Joindre(lignes) + texte.substr(b);
}

// ------------------------------------------------------------------------------------------------------------------
// sel

namespace {

bool EstAffectation(const std::string& s, size_t i) {
  if (i + 1 < s.size() && s[i + 1] == '=') return false;
  const char avant = i > 0 ? s[i - 1] : '\0';
  if (avant == '=' || avant == '!') return false;
  if (avant == '<' || avant == '>') return i > 1 && s[i - 2] == avant;
  return true;
}

bool Dans(char c, const char* ens) {
  for (; *ens; ++ens)
    if (c == *ens) return true;
  return false;
}

// _ternary_at
bool Ternaire(const std::string& s, size_t q, size_t& debut, size_t& deux_points, size_t& fin) {
  int profondeur = 0;
  long i = long(q) - 1;
  while (i >= 0) {
    const char c = s[size_t(i)];
    if (Dans(c, ")]")) {
      ++profondeur;
    } else if (Dans(c, "([")) {
      if (profondeur == 0) break;
      --profondeur;
    } else if (profondeur == 0) {
      if (Dans(c, ",;{}?:")) break;
      if (c == '=' && EstAffectation(s, size_t(i))) break;
    }
    --i;
  }
  size_t start = size_t(i + 1);
  {
    // re.match(r'\s*return\b', s[start:q])
    size_t k = start;
    while (k < q && Blanc(s[k])) ++k;
    if (k + 6 <= q && s.compare(k, 6, "return") == 0 && (k + 6 == q || !Mot(s[k + 6]))) {
      const size_t idx = s.substr(start, q - start).find("return");
      start = start + idx + 6;
    }
  }
  profondeur = 0;
  int attente = 0;
  size_t j = q + 1;
  size_t colon = npos;
  while (j < s.size()) {
    const char c = s[j];
    if (Dans(c, "([")) {
      ++profondeur;
    } else if (Dans(c, ")]")) {
      if (--profondeur < 0) return false;
    } else if (profondeur == 0) {
      if (c == '?') {
        ++attente;
      } else if (c == ':') {
        if (attente == 0) {
          colon = j;
          break;
        }
        --attente;
      } else if (Dans(c, ",;")) {
        return false;
      }
    }
    ++j;
  }
  if (colon == npos) return false;
  profondeur = 0;
  attente = 0;
  size_t k = colon + 1;
  while (k < s.size()) {
    const char c = s[k];
    if (Dans(c, "([")) {
      ++profondeur;
    } else if (Dans(c, ")]")) {
      if (profondeur == 0) break;
      --profondeur;
    } else if (profondeur == 0) {
      if (Dans(c, ",;")) break;
      if (c == '?') {
        ++attente;
      } else if (c == ':') {
        if (attente == 0) break;
        --attente;
      }
    }
    ++k;
  }
  debut = start;
  deux_points = colon;
  fin = k;
  return true;
}

const char* const kTypesSel[][2] = {
    {"float", "mix(b, a, c)"}, {"vec2", "mix(b, a, bvec2(c))"}, {"vec3", "mix(b, a, bvec3(c))"},
    {"vec4", "mix(b, a, bvec4(c))"}, {"int", "mix(b, a, c)"}, {"ivec2", "mix(b, a, bvec2(c))"},
    {"ivec3", "mix(b, a, bvec3(c))"}, {"ivec4", "mix(b, a, bvec4(c))"}, {"uint", "mix(b, a, c)"},
    {"uvec2", "mix(b, a, bvec2(c))"}, {"uvec3", "mix(b, a, bvec3(c))"}, {"uvec4", "mix(b, a, bvec4(c))"},
    {"bool", "mix(b, a, c)"}, {"bvec2", "mix(b, a, bvec2(c))"}, {"bvec3", "mix(b, a, bvec3(c))"},
    {"bvec4", "mix(b, a, bvec4(c))"}};

}  // namespace

std::string Sel(const std::string& texte) {
  int compte = 0;
  std::string sortie = SurMain(texte, [&](const std::string& corps) {
    std::vector<std::string> lignes = Lignes(corps);
    for (std::string& ligne : lignes) {
      if (ligne.find('?') == npos) continue;
      const std::string tete = ligne.substr(Tete(ligne).size());
      if (Commence(tete, "#") || Commence(tete, "//")) continue;
      int garde = 0;
      while (ligne.find('?') != npos && garde < 400) {
        ++garde;
        bool fait = false;
        for (size_t q = ligne.find('?'); q != npos; q = ligne.find('?', q + 1)) {
          size_t debut, colon, fin;
          if (!Ternaire(ligne, q, debut, colon, fin)) continue;
          const std::string cond = ligne.substr(debut, q - debut), x = ligne.substr(q + 1, colon - q - 1),
                            y = ligne.substr(colon + 1, fin - colon - 1);
          if (cond.find('?') != npos || x.find('?') != npos || y.find('?') != npos) continue;
          const std::string lead = Tete(cond);
          const std::string trail = y.substr(RStrip(y).size());
          ligne = ligne.substr(0, debut) + lead + "dk_sel(" + Strip(cond) + ", " + Strip(x) + ", " + Strip(y) + ")" +
                  trail + ligne.substr(fin);
          ++compte;
          fait = true;
          break;
        }
        if (!fait) break;
      }
    }
    return Joindre(lignes);
  });
  if (compte) {
    std::string aides;
    for (const auto& t : kTypesSel)
      aides += std::string(t[0]) + " dk_sel(bool c, " + t[0] + " a, " + t[0] + " b) { return " + t[1] + "; }\n";
    sortie = AvantMain(sortie, aides);
  }
  return sortie;
}

// ------------------------------------------------------------------------------------------------------------------
// ifmerge

namespace {

struct SiVar {
  size_t debut, fin;  // m.start() (le \n), m.end() (après la {)
  std::string retrait;
  bool non;
  std::string var;
};

// \n([ \t]*)if \((!?)(_\d+)\)\n\1\{ à p
bool SiVarA(const std::string& b, size_t p, SiVar& m) {
  if (p >= b.size() || b[p] != '\n') return false;
  size_t i = p + 1;
  while (i < b.size() && (b[i] == ' ' || b[i] == '\t')) ++i;
  const std::string retrait = b.substr(p + 1, i - p - 1);
  if (b.compare(i, 4, "if (") != 0) return false;
  i += 4;
  const bool non = i < b.size() && b[i] == '!';
  if (non) ++i;
  const size_t f = FinVariable(b, i);
  if (f == npos || b.compare(f, 2, ")\n") != 0) return false;
  const std::string var = b.substr(i, f - i);
  size_t k = f + 2;
  if (b.compare(k, retrait.size(), retrait) != 0) return false;
  k += retrait.size();
  if (k >= b.size() || b[k] != '{') return false;
  m = {p, k + 1, retrait, non, var};
  return true;
}

bool ChercherSiVar(const std::string& b, size_t pos, SiVar& m) {
  for (size_t p = b.find('\n', pos); p != npos; p = b.find('\n', p + 1))
    if (SiVarA(b, p, m)) return true;
  return false;
}

// _if_end
bool FinSi(const std::string& b, const SiVar& m, std::string& alors, bool& a_sinon, std::string& sinon, size_t& fin) {
  const size_t ouvre = m.fin - 1;
  const size_t ferme = Fermante(b, ouvre, '{', '}');
  if (ferme == npos) return false;
  alors = b.substr(ouvre + 1, ferme - ouvre - 1);
  const std::string motif = "\n" + m.retrait + "else\n" + m.retrait + "{";
  if (b.compare(ferme + 1, motif.size(), motif) != 0) {
    a_sinon = false;
    sinon.clear();
    fin = ferme + 1;
    return true;
  }
  const size_t eo = ferme + motif.size();
  const size_t ec = Fermante(b, eo, '{', '}');
  if (ec == npos) return false;
  a_sinon = true;
  sinon = b.substr(eo + 1, ec - eo - 1);
  fin = ec + 1;
  return true;
}

// \n([ \t]*)(?:float|int|uint|bool|[biu]?vec[234]|mat[234]) _\d+; à `at` : sa fin et son retrait.
bool DeclarationSeule(const std::string& b, size_t at, std::string& retrait, size_t& fin) {
  if (at >= b.size() || b[at] != '\n') return false;
  size_t i = at + 1;
  while (i < b.size() && (b[i] == ' ' || b[i] == '\t')) ++i;
  size_t j = i;
  while (j < b.size() && b[j] != ' ' && Mot(b[j])) ++j;
  const std::string type = b.substr(i, j - i);
  static const char* const kTypes[] = {"float", "int",   "uint",  "bool",  "vec2",  "vec3",  "vec4",
                                       "bvec2", "bvec3", "bvec4", "ivec2", "ivec3", "ivec4", "uvec2",
                                       "uvec3", "uvec4", "mat2",  "mat3",  "mat4"};
  bool ok = false;
  for (const char* t : kTypes) ok |= type == t;
  if (!ok || j >= b.size() || b[j] != ' ') return false;
  const size_t f = FinVariable(b, j + 1);
  if (f == npos || f >= b.size() || b[f] != ';') return false;
  retrait = b.substr(at + 1, i - at - 1);
  fin = f + 1;
  return true;
}

// \bvar\s*(?:[-+*/%&|^]|<<|>>)?=(?!=)
bool Ecrit(const std::string& s, const std::string& var) {
  for (size_t p = s.find(var); p != npos; p = s.find(var, p + 1)) {
    if (p > 0 && Mot(s[p - 1])) continue;
    size_t k = p + var.size();
    while (k < s.size() && Blanc(s[k])) ++k;
    auto Egal = [&](size_t e) { return e < s.size() && s[e] == '=' && !(e + 1 < s.size() && s[e + 1] == '='); };
    if (k < s.size() && Dans(s[k], "-+*/%&|^") && Egal(k + 1)) return true;
    if (k + 1 < s.size() && (s.compare(k, 2, "<<") == 0 || s.compare(k, 2, ">>") == 0) && Egal(k + 2)) return true;
    if (Egal(k)) return true;
  }
  return false;
}

}  // namespace

std::string IfMerge(const std::string& texte) {
  return SurMain(texte, [](std::string b) {
    size_t pos = 0;
    for (;;) {
      SiVar m;
      if (!ChercherSiVar(b, pos, m)) return b;
      std::string alors1, sinon1;
      bool a_sinon1;
      size_t fin1;
      if (!FinSi(b, m, alors1, a_sinon1, sinon1, fin1)) {
        pos = m.fin;
        continue;
      }
      size_t at = fin1;
      std::vector<std::string> decls;
      for (;;) {
        std::string r;
        size_t f;
        if (!DeclarationSeule(b, at, r, f) || r != m.retrait) break;
        decls.push_back(Strip(b.substr(at + 1, f - at - 1)));
        at = f;
      }
      SiVar m2;
      if (!SiVarA(b, at, m2) || m2.retrait != m.retrait || m2.non != m.non || m2.var != m.var ||
          Ecrit(alors1, m.var) || (a_sinon1 && Ecrit(sinon1, m.var))) {
        pos = m.fin;
        continue;
      }
      std::string alors2, sinon2;
      bool a_sinon2;
      size_t fin2;
      if (!FinSi(b, m2, alors2, a_sinon2, sinon2, fin2)) {
        pos = m.fin;
        continue;
      }
      const std::string& ind = m.retrait;
      std::string fusion;
      for (const std::string& d : decls) fusion += "\n" + ind + d;
      fusion += "\n" + ind + "if (" + (m.non ? "!" : "") + m.var + ")\n" + ind + "{" + RStrip(alors1) + RStrip(alors2) +
                "\n" + ind + "}";
      if (a_sinon1 || a_sinon2)
        fusion += "\n" + ind + "else\n" + ind + "{" + RStrip(sinon1) + RStrip(sinon2) + "\n" + ind + "}";
      b = b.substr(0, m.debut) + fusion + b.substr(fin2);
      pos = m.debut;
    }
  });
}

int Appliquer(std::string& glsl, bool vertex) {
  int faites = 0;
  auto Passe = [&](std::string (*f)(const std::string&)) {
    std::string t = f(glsl);
    if (t != glsl) {
      glsl = std::move(t);
      ++faites;
    }
  };
  if (vertex) Passe(VsOut);
  Passe(Pred);
  Passe(Sel);
  Passe(IfMerge);
  return faites;
}

}  // namespace opdarkness::passes_glsl

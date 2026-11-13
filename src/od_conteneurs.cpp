// opdarkness - conteneurs de shaders des fichiers du jeu (od_conteneurs.h).

#include "od_conteneurs.h"

#include <pthread.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <cstdio>
#include <cstring>
#include <system_error>
#include <utility>

#include <fmt/format.h>

#define XXH_INLINE_ALL
#include <xxhash.h>

#include "od_alloc_fils.h"
#include "od_switch_cpu.h"

// SDK (src/system/lzx.cpp) : décompression LZX d'un flux en mémoire, celle du chargeur de XEX.
int lzx_decompress(const void* lzx_data, size_t lzx_len, void* dest, size_t dest_len, uint32_t window_size,
                   void* window_data, size_t window_data_len);

namespace opdarkness::conteneurs {
namespace {

using Horloge = std::chrono::steady_clock;

uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint32_t Be16(const uint8_t* p) { return uint32_t(p[0]) << 8 | p[1]; }

double Ms(Horloge::time_point debut) {
  return std::chrono::duration<double, std::milli>(Horloge::now() - debut).count();
}

bool LireFichier(const std::filesystem::path& chemin, std::vector<uint8_t>& sortie) {
  std::FILE* f = std::fopen(chemin.string().c_str(), "rb");
  if (!f) return false;
  bool ok = std::fseek(f, 0, SEEK_END) == 0;
  const long n = ok ? std::ftell(f) : -1;
  ok = n >= 0 && std::fseek(f, 0, SEEK_SET) == 0;
  if (ok) {
    sortie.resize(size_t(n));
    ok = std::fread(sortie.data(), 1, sortie.size(), f) == sortie.size();
  }
  std::fclose(f);
  return ok;
}

bool EstIgb(const std::filesystem::path& p) {
  std::string e = p.extension().string();
  std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  return e == ".igb";
}

}  // namespace

// La vérification de reNut (tools/dump_shaders.py, reprise par sa2-nx ; shaders_fichiers.py conteneurs()) : signature
// 10 2A 11 0x alignée sur 4, tailles, tables et partie physique cohérentes. Les conteneurs peuvent se chevaucher : on
// avance toujours de 4.
size_t Chercher(const uint8_t* d, size_t n, const std::string& fichier, Liste& liste) {
  size_t nouveaux = 0;
  for (size_t o = 0; o + 0x24 <= n; o += 4) {
    if (d[o] != 0x10 || d[o + 1] != 0x2A || d[o + 2] != 0x11 || d[o + 3] > 1) continue;
    const uint32_t virt = Be32(d + o + 4), phys = Be32(d + o + 8);
    if (!(virt >= 0x24 && virt < 0x10000 && phys > 0 && phys < 0x100000 && o + virt + phys <= n)) continue;
    const uint32_t ct = Be32(d + o + 0x10), dt = Be32(d + o + 0x14), so = Be32(d + o + 0x18);
    if (ct >= virt || dt >= virt || !(so >= 0x24 && so < virt - 8)) continue;
    if (Be32(d + o + so) + Be32(d + o + so + 4) != phys) continue;
    const size_t taille = size_t(virt) + phys;
    const uint64_t empreinte = XXH3_64bits(d + o, taille);
    auto [it, nouveau] = liste.try_emplace(empreinte);
    if (!nouveau) continue;
    it->second.vertex = d[o + 3] & 1;
    it->second.donnees.assign(d + o, d + o + taille);
    it->second.fichier = fichier;
    ++nouveaux;
  }
  return nouveaux;
}

// Format (tools/renut/xmem_decompresser.c) : en-tête de 0x30 octets (grand-boutiste : signature, version, fenêtre en
// 0x10, taille décompressée sur 64 bits en 0x18), puis des blocs (u32 taille), faits de trames LZX précédées de leur
// taille compressée (u16 ; 0xFF, u16 taille décompressée, u16 taille compressée pour une trame de moins de 32 Ko). Les
// trames mises bout à bout forment un seul flux LZX.
bool DecompresserXmem(const std::vector<uint8_t>& entree, std::vector<uint8_t>& sortie) {
  const uint8_t* d = entree.data();
  const size_t n = entree.size();
  if (n < 0x30 || Be32(d) != 0x0FF512EEu) return false;
  const uint32_t fenetre = Be32(d + 0x10);
  const uint64_t taille = uint64_t(Be32(d + 0x18)) << 32 | Be32(d + 0x1C);
  if (fenetre == 0 || (fenetre & (fenetre - 1)) || taille > (1ull << 31)) return false;
  std::vector<uint8_t> flux;
  flux.reserve(n);
  size_t p = 0x30;
  while (p + 4 <= n) {
    const uint32_t bloc = Be32(d + p);
    p += 4;
    const size_t fin = std::min(n, p + bloc);
    while (p < fin) {
      uint32_t c;
      if (d[p] == 0xFF) {
        if (p + 5 > fin) {
          p = fin;
          break;
        }
        c = Be16(d + p + 3);
        p += 5;
      } else {
        if (p + 2 > fin) {
          p = fin;
          break;
        }
        c = Be16(d + p);
        p += 2;
      }
      if (c == 0 || p + c > fin) {
        p = fin;
        break;
      }
      flux.insert(flux.end(), d + p, d + p + c);
      p += c;
    }
  }
  sortie.assign(size_t(taille), 0);
  return lzx_decompress(flux.data(), flux.size(), sortie.data(), sortie.size(), fenetre, nullptr, 0) == 0;
}

namespace {

// Un .igb : lu, décompressé si besoin, cherché dans une liste à lui (fusionnées ensuite dans l'ordre des fichiers).
struct Fichier {
  std::string rel;
  Liste liste;
  bool echec = false, compresse = false;
  uint64_t octets = 0;
  double ms_lecture = 0, ms_lzx = 0, ms_recherche = 0;
};

struct Travaux {
  const std::filesystem::path* racine;
  std::vector<Fichier>* fichiers;
  std::atomic<size_t> suivant{0}, faits{0};
  std::mutex verrou;  // progression
  const std::function<void(size_t, size_t)>* progression;
  bool allocateur = false;
};

void Traiter(const std::filesystem::path& racine, Fichier& f, std::vector<uint8_t>& donnees,
             std::vector<uint8_t>& decompresse) {
  auto t = Horloge::now();
  const bool lu = LireFichier(racine / f.rel, donnees);
  f.ms_lecture = Ms(t);
  if (!lu) {
    f.echec = true;
    return;
  }
  f.octets = donnees.size();
  const std::vector<uint8_t>* a_lire = &donnees;
  if (donnees.size() >= 4 && Be32(donnees.data()) == 0x0FF512EEu) {
    f.compresse = true;
    t = Horloge::now();
    if (!DecompresserXmem(donnees, decompresse)) {
      f.ms_lzx = Ms(t);
      f.echec = true;
      return;
    }
    f.ms_lzx = Ms(t);
    a_lire = &decompresse;
  }
  t = Horloge::now();
  Chercher(a_lire->data(), a_lire->size(), f.rel, f.liste);
  f.ms_recherche = Ms(t);
}

void* Fil(void* p) {
  auto& w = *static_cast<Travaux*>(p);
  const bool allocateur = w.allocateur && alloc_fils::Activer();
  {
    std::vector<uint8_t> donnees, decompresse;
    auto& fichiers = *w.fichiers;
    for (size_t i; (i = w.suivant.fetch_add(1)) < fichiers.size();) {
      Traiter(*w.racine, fichiers[i], donnees, decompresse);
      const size_t faits = w.faits.fetch_add(1) + 1;
      if (*w.progression) {
        std::lock_guard<std::mutex> l(w.verrou);
        (*w.progression)(faits, fichiers.size());
      }
    }
  }
  if (allocateur) alloc_fils::Desactiver();
  return nullptr;
}

}  // namespace

Bilan ChercherDansLeJeu(const uint8_t* image, size_t taille_image, const std::filesystem::path& racine, Liste& liste,
                        const Reglages& reglages, const std::function<void(size_t, size_t)>& progression) {
  Bilan b;
  const auto debut = Horloge::now();
  if (image && taille_image) {
    const auto t = Horloge::now();
    Chercher(image, taille_image, "default.xex", liste);
    b.ms_recherche += Ms(t);
  }
  // Ordre de shaders_fichiers.py (sorted(os.walk) puis sorted(fichiers)) : par dossier, puis par nom ; le premier
  // fichier où un shader est vu est celui du relevé.
  std::vector<std::pair<std::string, std::string>> igb;
  std::error_code ec;
  for (auto it = std::filesystem::recursive_directory_iterator(racine, ec);
       !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
    if (!it->is_regular_file(ec) || !EstIgb(it->path())) continue;
    const auto rel = it->path().lexically_relative(racine);
    igb.emplace_back(rel.parent_path().generic_string(), rel.filename().string());
  }
  std::sort(igb.begin(), igb.end());
  std::vector<Fichier> fichiers(igb.size());
  for (size_t i = 0; i < igb.size(); ++i)
    fichiers[i].rel = igb[i].first.empty() ? igb[i].second : igb[i].first + "/" + igb[i].second;

  Travaux w;
  w.racine = &racine;
  w.fichiers = &fichiers;
  w.progression = &progression;
  w.allocateur = reglages.allocateur_par_fil;
  const int n = std::max(1, reglages.fils);
  std::vector<pthread_t> fils(static_cast<size_t>(n));
  std::vector<bool> lance(static_cast<size_t>(n), false);
  for (int i = 0; i < n; ++i) {
    // Sur la Switch, chaque fil sur son cœur (0, 1, 2 tour à tour) : placé par lui-même au début de Fil.
    lance[size_t(i)] = pthread_create(&fils[size_t(i)], nullptr, [](void* p) -> void* {
      auto* a = static_cast<std::pair<Travaux*, int>*>(p);
      if (a->second >= 0) switch_cpu::FixerCoeur(a->second);
      Travaux* w = a->first;
      delete a;
      return Fil(w);
    }, new std::pair<Travaux*, int>(&w, reglages.placer ? i % 3 : -1)) == 0;
  }
  for (int i = 0; i < n; ++i)
    if (lance[size_t(i)]) pthread_join(fils[size_t(i)], nullptr);
  if (std::none_of(lance.begin(), lance.end(), [](bool l) { return l; })) Fil(&w);

  for (auto& f : fichiers) {
    ++b.fichiers;
    b.compresses += f.compresse;
    b.echecs += f.echec;
    b.octets += f.octets;
    b.ms_lecture += f.ms_lecture;
    b.ms_lzx += f.ms_lzx;
    b.ms_recherche += f.ms_recherche;
    for (auto& [empreinte, c] : f.liste) liste.try_emplace(empreinte, std::move(c));
  }
  b.ms_total = Ms(debut);
  return b;
}

std::string Nom(uint64_t empreinte, const Conteneur& c) {
  return fmt::format("{}_{:016x}", c.vertex ? "vs" : "ps", empreinte);
}

}  // namespace opdarkness::conteneurs

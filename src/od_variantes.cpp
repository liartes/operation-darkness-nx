// opdarkness - variantes de vertex shaders traduites au fil du jeu (docs/SHADERS_EN_JEU.md).
//
// Direct3D réécrit certains vertex shaders au moment du dessin pour la déclaration de sommets de l'objet ; le
// programme chargé n'est alors plus celui du shader créé par le jeu, et le rendu natif ne le reconnaît pas. Le rendu
// confie la variante (me::native::SetVariantHandler) à ce fil de fond, qui :
//   1. la répare (od_reparation_variantes.cpp, preuves de sa2-nx : la réécriture n'est qu'une permutation des
//      lectures de sommets ; sinon refus) ;
//   2. traduit le conteneur réparé et l'ajoute à la bibliothèque et au cache (od_traducteur.cpp).
// Même file pour les chargements immédiats inconnus de shaders de Direct3D réécrits (me::native::SetImmediateHandler) :
// le rendu a déjà reconstruit un conteneur complet, traduit directement (étape 2 seule).
// Le programme est reconnu au chargement suivant (le rendu ne garde en mémoire que les identifications réussies) ;
// d'ici là, ses dessins sont sautés (quelques dizaines de ms).

#include <pthread.h>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#define XXH_INLINE_ALL
#include <xxhash.h>

#include <rex/logging.h>

#include "me/me_native_system.h"
#include "od_reparation_variantes.h"
#include "od_traducteur.h"

namespace {

struct Travail {
  std::vector<uint8_t> modele, variante;  // modele vide : conteneur déjà complet (chargement immédiat)
};

std::mutex g_mutex;
std::condition_variable g_reveil;
std::deque<Travail> g_file;
bool g_fil_lance = false;

uint32_t Gros(const std::vector<uint8_t>& c, size_t p) {
  return p + 4 <= c.size() ? uint32_t(c[p]) << 24 | uint32_t(c[p + 1]) << 16 | uint32_t(c[p + 2]) << 8 | c[p + 3] : 0;
}

// Microcode du conteneur, mots dans l'ordre de l'hôte (vide si l'en-tête est incohérent).
std::vector<uint32_t> Microcode(const std::vector<uint8_t>& c) {
  const uint32_t virtuel = Gros(c, 4), entete = Gros(c, 24);
  const uint32_t debut = Gros(c, entete), octets = Gros(c, entete + 4);
  if (!octets || size_t(virtuel) + debut + octets > c.size()) return {};
  std::vector<uint32_t> mots(octets / 4);
  for (size_t i = 0; i < mots.size(); ++i) mots[i] = Gros(c, virtuel + debut + i * 4);
  return mots;
}

// Empreinte du microcode, celle que le rendu affiche (loaded_hash, VS identity mismatch) pour le programme chargé.
uint64_t EmpreinteMicrocode(const std::vector<uint8_t>& c) {
  const auto mots = Microcode(c);
  return mots.empty() ? 0 : XXH3_64bits(mots.data(), mots.size() * 4);
}

void Traiter(Travail& t) {
  const uint64_t h = XXH3_64bits(t.variante.data(), t.variante.size());
  if (t.modele.empty()) {
    REXLOG_INFO("[variantes] shader de Direct3D chargé en immédiat {:016x}, traduction", h);
    opdarkness::traducteur::AssurerShader(t.variante.data(), t.variante.size());
    return;
  }
  // Comme le paquet (wrap_discovered.py de sa2-nx enveloppait le microcode de chaque variante découverte) : la variante
  // est aussi enveloppée par registres. L'index par code du rendu préfère l'entrée liée par registres quand deux
  // entrées partagent un microcode. Sans elle, la variante réparée du vertex shader d'Edward (microcode 4A9A0A4E,
  // modèle C08A1416) ne dessinait rien : personnages absents sans paquet (2026-10-09, docs/SHADERS_EN_JEU.md).
  if (const auto mots = Microcode(t.variante); !mots.empty()) {
    std::vector<uint8_t> enveloppe = me::native::WrapVertexMicrocode(t.variante, mots);
    if (!enveloppe.empty()) {
      REXLOG_INFO("[variantes] variante {:016x} (microcode {:016X}) enveloppée par registres, traduction", h,
                  EmpreinteMicrocode(t.variante));
      opdarkness::traducteur::AssurerShader(enveloppe.data(), enveloppe.size());
    }
  }
  std::vector<uint8_t> reparee;
  std::string raison;
  if (!opdarkness::ReparerVariante(t.modele, t.variante, reparee, raison)) {
    REXLOG_WARN("[variantes] variante {:016x} (microcode {:016X}) non réparée : {}", h, EmpreinteMicrocode(t.variante),
                raison);
    return;
  }
  REXLOG_INFO("[variantes] variante {:016x} (microcode {:016X}) réparée, traduction", h, EmpreinteMicrocode(t.variante));
  opdarkness::traducteur::AssurerShader(reparee.data(), reparee.size());
}

void* Boucle(void*) {
  for (;;) {
    Travail t;
    {
      std::unique_lock<std::mutex> lock(g_mutex);
      g_reveil.wait(lock, [] { return !g_file.empty(); });
      t = std::move(g_file.front());
      g_file.pop_front();
    }
    Traiter(t);
  }
}

// Appelé par le fil de l'anneau : met en file et rend la main.
void Recevoir(const std::vector<uint8_t>& modele, std::vector<uint8_t> variante) {
  static std::unordered_set<uint64_t> vues;  // sous g_mutex
  const uint64_t h = XXH3_64bits(variante.data(), variante.size());
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!vues.insert(h).second) return;
  if (!g_fil_lance) {
    // pthread avec une pile explicite : std::thread lève std::system_error sur la Switch (torchlight-nx). XenosRecomp
    // et glslang sont gourmands en pile : 4 Mo.
    pthread_attr_t attributs;
    pthread_attr_init(&attributs);
    pthread_attr_setstacksize(&attributs, 4u << 20);
    pthread_t fil;
    if (pthread_create(&fil, &attributs, &Boucle, nullptr) == 0) pthread_detach(fil);
    pthread_attr_destroy(&attributs);
    g_fil_lance = true;
  }
  g_file.push_back(Travail{modele, std::move(variante)});
  g_reveil.notify_one();
}

void RecevoirImmediat(std::vector<uint8_t> conteneur) { Recevoir({}, std::move(conteneur)); }

const bool g_enregistre = [] {
  me::native::SetVariantHandler(&Recevoir);
  me::native::SetImmediateHandler(&RecevoirImmediat);
  return true;
}();

}  // namespace

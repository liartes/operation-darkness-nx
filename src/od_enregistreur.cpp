// opdarkness - enregistrement et rejeu des entrées de la manette, au niveau où le jeu les lit.
//
// Le jeu lit la manette par deux fonctions d'enveloppe (docs/D3D.md n'en parle pas : ce sont des fonctions de XAPI) :
//   sub_82399730 = XInputGetState(r3 = joueur, r4 = X_INPUT_STATE*) -> XamInputGetState(joueur, 1, état),
//                  appelée seulement par sub_8232EC90 (thread readDeviceThread) ;
//   sub_82399808 = XInputGetKeystroke(r3 = joueur, r4 = drapeaux, r5 = X_INPUT_KEYSTROKE*) -> XamInputGetKeystrokeEx,
//                  appelée par sub_8217BC78 (événements de touches).
//
// --od_enregistrer=<fichier> : chaque changement d'état (code de retour ou contenu) d'un joueur et chaque événement de
// touche sont écrits, une ligne chacun, avec le numéro d'image du jeu (od_images.cpp) et le temps en ms :
//   E <image> <ms> <joueur> <retour> <boutons> <gâchette G> <gâchette D> <LX> <LY> <RX> <RY> <lecture>
//   K <image> <ms> <joueur> <retour> <touche> <unicode> <drapeaux> <hid> <lecture>
// <lecture> : nombre de lectures de la manette du joueur 1 par la tâche du moteur sub_8232EC90 (les 4 manettes à
// chaque appel), qui suit la logique du jeu plutôt que l'affichage.
// --od_rejouer=<fichier> : la vraie manette est ignorée ; chaque lecture reçoit l'état enregistré à l'image courante
// (dernier E d'image <= image courante), et les événements K sont rendus un par un quand leur image est atteinte.
// --od_rejeu_horloge=lectures (défaut) : se repérer au nombre de lectures de la manette ; =images : au numéro d'image
// (rejeux du 2026-10-09 : décalages après les chargements) ; =ms : au temps écoulé. Les fichiers sans colonne
// <lecture> (avant le 2026-10-09) se rejouent par images.
//
// Limite : le jeu n'est pas déterministe (durées des chargements, fils d'exécution) ; se repérer aux images tient
// mieux que le temps quand une machine est plus lente, mais un chargement plus long décale ce qui suit. À vérifier
// par des captures au rejeu.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>

#include "od_images.h"

REXCVAR_DEFINE_STRING(od_enregistrer, "", "OpDarkness",
                      "Fichier où enregistrer les entrées de la manette (états et touches, repérés par image)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_STRING(od_rejouer, "", "OpDarkness",
                      "Fichier d'entrées enregistré (--od_enregistrer) à rejouer à la place de la manette")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_STRING(od_rejeu_horloge, "lectures", "OpDarkness",
                      "Repère du rejeu : lectures (lectures de la manette par le moteur), images (numéro d'image) "
                      "ou ms (temps écoulé)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace {

using Clock = std::chrono::steady_clock;

constexpr uint32_t kErrorEmpty = 0x10D2;  // ERROR_EMPTY : pas d'événement de touche

uint16_t Be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
void PutBe16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v >> 8); p[1] = uint8_t(v); }
void PutBe32(uint8_t* p, uint32_t v) {
  p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v);
}

struct Etat {  // X_INPUT_GAMEPAD et code de retour
  uint32_t retour = 0x48F;  // ERROR_DEVICE_NOT_CONNECTED
  uint16_t boutons = 0;
  uint8_t gachette_g = 0, gachette_d = 0;
  int16_t lx = 0, ly = 0, rx = 0, ry = 0;
  bool operator==(const Etat&) const = default;
};

struct Touche {
  uint64_t repere = 0;
  uint32_t joueur = 0, retour = 0;
  uint16_t touche = 0, unicode = 0, drapeaux = 0;
  uint8_t hid = 0;
};

struct EvenementEtat {
  uint64_t repere = 0;
  uint32_t joueur = 0;
  Etat etat;
};

const Clock::time_point g_debut = Clock::now();

// Lectures de la manette du joueur 1 (XInputGetState(0, ...)), une par appel de la tâche sub_8232EC90.
std::atomic<uint64_t> g_lectures{0};

// Bilan des lectures par seconde, comparées aux images, toutes les 10 s (diagnostic du repère).
void BilanLectures() {
  static Clock::time_point debut = Clock::now();
  static uint64_t lectures0 = 0, images0 = 0;
  static std::mutex mutex;
  std::lock_guard<std::mutex> lock(mutex);
  const auto maintenant = Clock::now();
  const double s = std::chrono::duration<double>(maintenant - debut).count();
  if (s < 10.0) return;
  const uint64_t l = g_lectures.load(), i = opdarkness::ImagesPresentees();
  REXLOG_INFO("[entrées] {:.1f} lectures/s, {:.1f} images/s", double(l - lectures0) / s, double(i - images0) / s);
  debut = maintenant;
  lectures0 = l;
  images0 = i;
}

uint64_t Ms() {
  return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - g_debut).count());
}

// --- Enregistrement ---------------------------------------------------------------------------------------------

class Enregistreur {
 public:
  bool Actif() {
    std::call_once(ouverture_, [this] {
      const std::string chemin = REXCVAR_GET(od_enregistrer);
      if (chemin.empty()) return;
      fichier_ = std::fopen(chemin.c_str(), "w");
      if (fichier_) {
        std::fprintf(fichier_, "# opdarkness : entrées de la manette (src/od_enregistreur.cpp)\n");
        REXLOG_INFO("[entrées] enregistrement dans {}", chemin);
      } else {
        REXLOG_ERROR("[entrées] impossible d'écrire {}", chemin);
      }
    });
    return fichier_ != nullptr;
  }

  void NoteEtat(uint32_t joueur, const Etat& e) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (joueur >= 4) return;
    if (vu_[joueur] && dernier_[joueur] == e) return;
    vu_[joueur] = true;
    dernier_[joueur] = e;
    std::fprintf(fichier_, "E %llu %llu %u %X %04X %u %u %d %d %d %d %llu\n",
                 static_cast<unsigned long long>(opdarkness::ImagesPresentees()),
                 static_cast<unsigned long long>(Ms()), joueur, e.retour, e.boutons, e.gachette_g, e.gachette_d,
                 e.lx, e.ly, e.rx, e.ry, static_cast<unsigned long long>(g_lectures.load()));
    std::fflush(fichier_);
  }

  void NoteTouche(const Touche& t) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::fprintf(fichier_, "K %llu %llu %u %X %04X %04X %04X %u %llu\n",
                 static_cast<unsigned long long>(opdarkness::ImagesPresentees()),
                 static_cast<unsigned long long>(Ms()), t.joueur, t.retour, t.touche, t.unicode, t.drapeaux, t.hid,
                 static_cast<unsigned long long>(g_lectures.load()));
    std::fflush(fichier_);
  }

 private:
  std::once_flag ouverture_;
  std::FILE* fichier_ = nullptr;
  std::mutex mutex_;
  bool vu_[4] = {};
  Etat dernier_[4];
};

// --- Rejeu ------------------------------------------------------------------------------------------------------

class Rejoueur {
 public:
  bool Actif() {
    std::call_once(ouverture_, [this] { Charger(); });
    return actif_;
  }

  uint64_t Repere() const {
    switch (horloge_) {
      case Horloge::kMs: return Ms();
      case Horloge::kImages: return opdarkness::ImagesPresentees();
      default: return g_lectures.load();
    }
  }

  // État du joueur au repère courant (dernier événement de repère <= courant), et un numéro de paquet qui change
  // avec lui.
  Etat EtatCourant(uint32_t joueur, uint32_t& paquet) {
    std::lock_guard<std::mutex> lock(mutex_);
    const uint64_t r = Repere();
    if (joueur >= 4) return Etat{};
    auto& liste = etats_[joueur];
    size_t& i = position_[joueur];
    while (i < liste.size() && liste[i].repere <= r) {
      courant_[joueur] = liste[i].etat;
      ++paquets_[joueur];
      ++i;
    }
    paquet = paquets_[joueur];
    return courant_[joueur];
  }

  // Prochain événement de touche arrivé à échéance pour ce joueur (0xFF = n'importe lequel).
  bool ToucheSuivante(uint32_t joueur, Touche& t) {
    std::lock_guard<std::mutex> lock(mutex_);
    const uint64_t r = Repere();
    for (auto it = touches_.begin(); it != touches_.end() && it->repere <= r; ++it) {
      if (joueur == 0xFF || it->joueur == joueur) {
        t = *it;
        touches_.erase(it);
        return true;
      }
    }
    return false;
  }

 private:
  void Charger() {
    const std::string chemin = REXCVAR_GET(od_rejouer);
    if (chemin.empty()) return;
    const std::string h = REXCVAR_GET(od_rejeu_horloge);
    horloge_ = h == "ms" ? Horloge::kMs : h == "images" ? Horloge::kImages : Horloge::kLectures;
    std::ifstream f(chemin);
    if (!f) {
      REXLOG_ERROR("[entrées] impossible de lire {}", chemin);
      return;
    }
    std::string ligne;
    size_t n_etats = 0;
    while (std::getline(f, ligne)) {
      if (ligne.empty() || ligne[0] == '#') continue;
      std::istringstream in(ligne);
      char genre = 0;
      unsigned long long image = 0, ms = 0;
      in >> genre >> image >> ms;
      // Le repère « lectures » est la dernière colonne ; lu après les autres champs ci-dessous.
      uint64_t repere = horloge_ == Horloge::kMs ? ms : image;
      if (genre == 'E') {
        EvenementEtat e;
        e.repere = repere;
        unsigned joueur = 0, g = 0, d = 0;
        int lx = 0, ly = 0, rx = 0, ry = 0;
        uint32_t retour = 0, boutons = 0;
        in >> joueur >> std::hex >> retour >> boutons >> std::dec >> g >> d >> lx >> ly >> rx >> ry;
        if (!in || joueur >= 4) continue;
        if (horloge_ == Horloge::kLectures) repere = Lecture(in, image);
        e.joueur = joueur;
        e.etat = Etat{retour, uint16_t(boutons), uint8_t(g), uint8_t(d), int16_t(lx), int16_t(ly), int16_t(rx),
                      int16_t(ry)};
        etats_[joueur].push_back(e);
        ++n_etats;
      } else if (genre == 'K') {
        Touche t;
        t.repere = repere;
        unsigned joueur = 0, hid = 0;
        uint32_t retour = 0, touche = 0, unicode = 0, drapeaux = 0;
        in >> joueur >> std::hex >> retour >> touche >> unicode >> drapeaux >> std::dec >> hid;
        if (!in) continue;
        if (horloge_ == Horloge::kLectures) repere = Lecture(in, image);
        t.joueur = joueur;
        t.retour = retour;
        t.touche = uint16_t(touche);
        t.unicode = uint16_t(unicode);
        t.drapeaux = uint16_t(drapeaux);
        t.hid = uint8_t(hid);
        touches_.push_back(t);
      }
    }
    if (horloge_ == Horloge::kLectures && sans_lecture_) {
      REXLOG_WARN("[entrées] {} n'a pas de colonne <lecture> : rejeu par images", chemin);
      horloge_ = Horloge::kImages;
    }
    actif_ = true;
    REXLOG_INFO("[entrées] rejeu de {} : {} états, {} touches, repère {}", chemin, n_etats, touches_.size(),
                horloge_ == Horloge::kMs ? "ms" : horloge_ == Horloge::kImages ? "images" : "lectures");
  }

  // Colonne <lecture> en fin de ligne ; absente (ancien fichier) : on retombe sur l'image.
  uint64_t Lecture(std::istringstream& in, uint64_t image) {
    unsigned long long l = 0;
    if (in >> l) return l;
    sans_lecture_ = true;
    return image;
  }

  std::once_flag ouverture_;
  enum class Horloge { kLectures, kImages, kMs };
  bool actif_ = false;
  Horloge horloge_ = Horloge::kLectures;
  bool sans_lecture_ = false;
  std::mutex mutex_;
  std::vector<EvenementEtat> etats_[4];
  size_t position_[4] = {};
  Etat courant_[4];
  uint32_t paquets_[4] = {};
  std::deque<Touche> touches_;
};

Enregistreur g_enregistreur;
Rejoueur g_rejoueur;

Etat LireEtat(const uint8_t* s, uint32_t retour) {  // X_INPUT_STATE : paquet (4), puis X_INPUT_GAMEPAD (12)
  Etat e;
  e.retour = retour;
  if (retour != 0) return e;
  const uint8_t* g = s + 4;
  e.boutons = Be16(g);
  e.gachette_g = g[2];
  e.gachette_d = g[3];
  e.lx = int16_t(Be16(g + 4));
  e.ly = int16_t(Be16(g + 6));
  e.rx = int16_t(Be16(g + 8));
  e.ry = int16_t(Be16(g + 10));
  return e;
}

void EcrireEtat(uint8_t* s, uint32_t paquet, const Etat& e) {
  PutBe32(s, paquet);
  uint8_t* g = s + 4;
  PutBe16(g, e.boutons);
  g[2] = e.gachette_g;
  g[3] = e.gachette_d;
  PutBe16(g + 4, uint16_t(e.lx));
  PutBe16(g + 6, uint16_t(e.ly));
  PutBe16(g + 8, uint16_t(e.rx));
  PutBe16(g + 10, uint16_t(e.ry));
}

}  // namespace

#if OD_RENUT
namespace od::renut {
void CallReadHook();  // src/renut_engine/od_compat/od_compat.cpp
}  // namespace od::renut
#endif

REX_EXTERN(__imp__sub_82399730);
REX_HOOK_RAW(sub_82399730) {  // XInputGetState(r3 = joueur, r4 = X_INPUT_STATE*)
  const uint32_t joueur = ctx.r3.u32;
  const uint32_t adresse = ctx.r4.u32;
  if (joueur == 0) {
    g_lectures.fetch_add(1, std::memory_order_relaxed);
    BilanLectures();
#if OD_RENUT
    od::renut::CallReadHook();  // rendu reNut : lecture tardive et mesure de latence (rex/input/read_hook.h)
#endif
  }
  __imp__sub_82399730(ctx, base);
  if (!adresse) return;
  uint8_t* etat = base + adresse;
  if (g_rejoueur.Actif()) {
    uint32_t paquet = 0;
    const Etat e = g_rejoueur.EtatCourant(joueur, paquet);
    ctx.r3.u64 = e.retour;
    if (e.retour == 0) EcrireEtat(etat, paquet, e);
    return;
  }
  if (g_enregistreur.Actif()) g_enregistreur.NoteEtat(joueur, LireEtat(etat, ctx.r3.u32));
}

REX_EXTERN(__imp__sub_82399808);
REX_HOOK_RAW(sub_82399808) {  // XInputGetKeystroke(r3 = joueur, r4 = drapeaux, r5 = X_INPUT_KEYSTROKE*)
  const uint32_t joueur = ctx.r3.u32;
  const uint32_t adresse = ctx.r5.u32;
  if (g_rejoueur.Actif()) {
    __imp__sub_82399808(ctx, base);  // la vraie touche est lue puis ignorée (file du SDK vidée)
    Touche t;
    if (adresse && g_rejoueur.ToucheSuivante(joueur, t)) {
      uint8_t* k = base + adresse;  // X_INPUT_KEYSTROKE : touche, unicode, drapeaux (16 bits), joueur, hid (8 bits)
      PutBe16(k, t.touche);
      PutBe16(k + 2, t.unicode);
      PutBe16(k + 4, t.drapeaux);
      k[6] = uint8_t(t.joueur);
      k[7] = t.hid;
      ctx.r3.u64 = t.retour;
    } else {
      ctx.r3.u64 = kErrorEmpty;
    }
    return;
  }
  __imp__sub_82399808(ctx, base);
  if (adresse && ctx.r3.u32 == 0 && g_enregistreur.Actif()) {
    const uint8_t* k = base + adresse;
    Touche t;
    t.joueur = k[6];
    t.retour = 0;
    t.touche = Be16(k);
    t.unicode = Be16(k + 2);
    t.drapeaux = Be16(k + 4);
    t.hid = k[7];
    g_enregistreur.NoteTouche(t);
  }
}

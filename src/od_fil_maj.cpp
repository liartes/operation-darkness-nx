// opdarkness (od_fil_maj, 2026-11-03 ; par défaut depuis le 2026-11-06) : la mise à jour des scènes sur un second fil.
//
// sub_822DF6F8 parcourt ses scènes sous le grand verrou du moteur (sub_8232C5B0, global, récursif) et appelle pour chacune
// sub_822EEE10 : une tête (mise à jour des objets, aucun appel Direct3D), puis une queue (caméra et envoi des dessins). Le
// fil de dessin garde les queues, dans l'ordre. Un fil d'aide exécute les têtes à l'avance : la tête d'une scène peut
// ainsi tourner pendant la queue de la précédente, et le fil de dessin n'attend que la tête de sa propre scène.
// Les éléments d'autres classes intercalés dans la liste (822806B8, 82270418, 8226C3B0, 822A70E8) ne chevauchent aucune
// tête : le fil de dessin attend que le fil d'aide ait fini avant eux (leur chevauchement faisait disparaître des
// personnages en vue plongeante ; mot en conflit non identifié).
// Étude et mesures : docs/DEKO3D.md, « Paralléliser le fil de dessin » et « Grandes pages, profil PGO et allocateur »
// (console, alternance dans la partie : -0,50 ± 0,17 ms par image).
//
// Le grand verrou est tenu par le fil de dessin pendant tout le parcours (le fil des objets attend donc comme avant) : pour
// le fil d'aide, il est considéré comme acquis. Si le fil de dessin ne le tient pas, ou si l'appel vient d'un autre fil,
// sub_822EEE10 s'exécute à l'ancienne.
#include "opdarkness_pch.h"

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xthread.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

REXCVAR_DEFINE_BOOL(od_fil_maj, true, "OpDarkness",
                    "Mise à jour des scènes (tête de sub_822EEE10) sur un second fil")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace banjo::renut_gpu {
extern std::atomic<int> g_ab_off;  // alternance dans la partie (banjo_renut_ab_target = 19 : moitié sans second fil)
}

REX_EXTERN(__imp__sub_822EEE10);
REX_EXTERN(__imp__sub_8232C5B0);
REX_EXTERN(__imp__sub_8232C720);
REX_EXTERN(sub_822EFB98);
REX_EXTERN(sub_822EDB40);
REX_EXTERN(sub_82223080);
REX_EXTERN(sub_822EE928);

namespace {

constexpr uint32_t kContexteCourant = 0x829E77B0;  // écrit par la tête, lu par les fonctions de mise à jour
constexpr uint32_t kGrandVerrou = 0x8299C70C;      // objet de sub_8232C5B0 ; +92 : propriétaire
constexpr uint32_t kFonctionScene = 0x822EEE10;
using Horloge = std::chrono::steady_clock;

inline uint32_t Lit(uint8_t* base, uint32_t a) { return __builtin_bswap32(*reinterpret_cast<uint32_t*>(base + a)); }
inline void Ecrit(uint8_t* base, uint32_t a, uint32_t v) { *reinterpret_cast<uint32_t*>(base + a) = __builtin_bswap32(v); }

inline void AppelVirtuel(PPCContext& ctx, uint8_t* base, uint32_t objet, uint32_t decalage) {
  const uint32_t cible = Lit(base, Lit(base, objet) + decalage);
  ctx.r3.u64 = objet;
  REX_CALL_INDIRECT_FUNC(cible);
}

// Tête de sub_822EEE10 (jusqu'avant lwz r3,56(r31)), mêmes opérations, même ordre.
void Tete(PPCContext& ctx, uint8_t* base, uint32_t n) {
  const uint32_t pile = ctx.r1.u32;
  ctx.r1.u32 = pile - 128;
  Ecrit(base, ctx.r1.u32, pile);
  const uint32_t r27 = n + 20;
  const uint32_t r29 = Lit(base, Lit(base, n + 48) + 16);
  ctx.r4.s64 = 1;
  AppelVirtuel(ctx, base, r29, 124);
  ctx.r4.s64 = 1;
  AppelVirtuel(ctx, base, Lit(base, r27 + 4), 124);
  uint32_t noeud = Lit(base, Lit(base, n + 48) + 8);
  Ecrit(base, kContexteCourant, r27);
  while (noeud) {
    AppelVirtuel(ctx, base, noeud, 8);
    noeud = Lit(base, noeud + 8);
  }
  const uint32_t suite = Lit(base, n + 52);
  if (suite) {
    ctx.r3.u64 = suite;
    ctx.r4.u64 = n != 20 ? r27 : 0;
    sub_822EFB98(ctx, base);
  }
  Ecrit(base, kContexteCourant, 0);
  AppelVirtuel(ctx, base, Lit(base, n + 24), 116);
  AppelVirtuel(ctx, base, r29, 116);
  ctx.r1.u32 = pile;
}

// Queue de sub_822EEE10.
void Queue(PPCContext& ctx, uint8_t* base, uint32_t n) {
  const uint32_t pile = ctx.r1.u32;
  ctx.r1.u32 = pile - 128;
  Ecrit(base, ctx.r1.u32, pile);
  uint32_t r3 = Lit(base, n + 56);
  if (r3) {
    ctx.r3.u64 = r3;
    sub_822EDB40(ctx, base);
  }
  r3 = Lit(base, n + 44);
  if (r3) {
    ctx.r3.u64 = r3;
    sub_82223080(ctx, base);
  }
  ctx.r3.u64 = n + 20;
  sub_822EE928(ctx, base);
  r3 = Lit(base, n + 60);
  if (r3) {
    ctx.r3.u64 = r3;
    sub_822EDB40(ctx, base);
  }
  ctx.r1.u32 = pile;
}

// La scène suivante de la liste, par-dessus les éléments d'autres classes (exécutés par le fil de dessin, à leur place).
inline uint32_t SceneSuivante(uint8_t* base, uint32_t n) {
  for (uint32_t suite = Lit(base, n + 12); suite; suite = Lit(base, suite + 12))
    if (Lit(base, Lit(base, suite) + 4) == kFonctionScene) return suite;
  return 0;
}

std::atomic<uint32_t> g_r13_aide{0};
uint32_t g_r13_dessin = 0;
std::mutex g_mutex;
std::condition_variable g_cv_aide, g_cv_dessin;
uint32_t g_poste = 0;                // sous g_mutex : première scène d'une chaîne
bool g_aide_libre = true;            // sous g_mutex
bool g_dessin_attend = false;        // sous g_mutex
std::atomic<uint32_t> g_faites{0};   // têtes finies de la chaîne en cours
uint32_t g_attendue = 0, g_rang = 0; // fil de dessin seulement
bool g_chaine_ancienne = false;      // fil de dessin : chaîne en cours à l'ancienne (moitié « off » de l'alternance)
uint64_t g_chaines_anciennes = 0;    // fil de dessin : chaînes passées à l'ancienne par l'alternance (bilan)
rex::system::object_ref<rex::system::XHostThread> g_fil;
std::atomic<uint64_t> g_ns_tetes{0};
uint64_t g_ns_attente = 0, g_scenes = 0, g_chaines = 0, g_a_l_ancienne = 0, g_hors_chaine = 0;
Horloge::time_point g_bilan{};

int BoucleAide() {
  auto* fil = rex::system::XThread::GetCurrentThread();
  auto* etat = fil->thread_state();
  PPCContext& ctx = *etat->context();
  uint8_t* base = rex::system::kernel_state()->memory()->virtual_membase();
  ctx.fpscr.InitHost();
  ctx.r1.u32 -= 256;
  g_r13_aide.store(ctx.r13.u32, std::memory_order_release);
  g_cv_dessin.notify_all();
  for (;;) {
    uint32_t n;
    {
      std::unique_lock lock(g_mutex);
      g_cv_aide.wait(lock, [] { return g_poste != 0; });
      n = g_poste;
      g_poste = 0;
      g_aide_libre = false;
    }
    const auto debut = Horloge::now();
    for (; n; n = SceneSuivante(base, n)) {
      Tete(ctx, base, n);
      g_faites.fetch_add(1, std::memory_order_release);
      bool reveil;
      {
        std::lock_guard lock(g_mutex);
        reveil = g_dessin_attend;
      }
      if (reveil) g_cv_dessin.notify_all();
    }
    g_ns_tetes.fetch_add(uint64_t(std::chrono::nanoseconds(Horloge::now() - debut).count()), std::memory_order_relaxed);
    {
      std::lock_guard lock(g_mutex);
      g_aide_libre = true;
    }
    g_cv_dessin.notify_all();
  }
  return 0;
}

void Demarrer() {
  g_fil = rex::system::object_ref<rex::system::XHostThread>(
      new rex::system::XHostThread(rex::system::kernel_state(), 1024 * 1024, 0, [] { return BoucleAide(); }));
  g_fil->set_name("vmsMajScenes");
  g_fil->Create();
  std::unique_lock lock(g_mutex);
  g_cv_dessin.wait(lock, [] { return g_r13_aide.load(std::memory_order_acquire) != 0; });
  REXLOG_INFO("[fil-maj] fil d'aide en place");
}

// Fil de dessin : attendre que le fil d'aide ait fini sa chaîne (et qu'aucune ne soit en attente de lui).
void AttendreAideLibre() {
  std::unique_lock lock(g_mutex);
  g_dessin_attend = true;
  g_cv_dessin.wait(lock, [] { return g_aide_libre && !g_poste; });
  g_dessin_attend = false;
}

// Fil de dessin : attendre la tête de la scène de rang donné dans la chaîne (quelques tours d'abord, elle est souvent finie).
void AttendreTete(uint32_t rang, uint32_t n) {
  const auto faite = [rang] { return g_faites.load(std::memory_order_acquire) > rang; };
  for (int i = 0; i < 2000; ++i) {
    if (faite()) return;
  }
  std::unique_lock lock(g_mutex);
  g_dessin_attend = true;
  while (!g_cv_dessin.wait_for(lock, std::chrono::seconds(3), faite))
    REXLOG_ERROR("[fil-maj] attente de plus de 3 s : tête d'une scène (scène {:08X}, rang {}, faites {})", n, rang,
                 g_faites.load());
  g_dessin_attend = false;
}

void Bilan() {
  const auto maintenant = Horloge::now();
  if (g_bilan == Horloge::time_point{}) g_bilan = maintenant;
  if (maintenant - g_bilan < std::chrono::seconds(10)) return;
  const double duree_ms = std::chrono::duration<double, std::milli>(maintenant - g_bilan).count();
  REXLOG_INFO("[fil-maj] 10 s : {} scènes en {} chaînes, à l'ancienne {}, hors chaîne {}, chaînes sans second fil "
              "(alternance) {} ; fil d'aide occupé {:.1f} %, fil de dessin en attente de lui {:.1f} %",
              g_scenes, g_chaines, g_a_l_ancienne, g_hors_chaine, g_chaines_anciennes,
              double(g_ns_tetes.exchange(0, std::memory_order_relaxed)) / 1e4 / duree_ms,
              double(g_ns_attente) / 1e4 / duree_ms);
  g_scenes = g_chaines = g_a_l_ancienne = g_hors_chaine = g_ns_attente = g_chaines_anciennes = 0;
  g_bilan = maintenant;
}

}  // namespace

REX_HOOK_RAW(sub_822EEE10) {
  static const bool actif = REXCVAR_GET(od_fil_maj);
  if (!actif) {
    __imp__sub_822EEE10(ctx, base);
    return;
  }
  if (!g_r13_dessin) {
    // Premier appelant : le fil de dessin (sub_822DF6F8, relevé du 2026-11-03 ; le fil d'aide n'existe pas encore).
    g_r13_dessin = ctx.r13.u32;
    Demarrer();
  }
  // Le fil de dessin, sous le grand verrou : sinon, à l'ancienne (une fois le fil d'aide au repos).
  const uint32_t moi = Lit(base, Lit(base, ctx.r13.u32 + 256) + 332);
  const bool permis = ctx.r13.u32 == g_r13_dessin && Lit(base, kGrandVerrou + 92) == moi;
  const uint32_t n = ctx.r3.u32;
  if (!permis) {
    ++g_a_l_ancienne;
    if (ctx.r13.u32 == g_r13_dessin) {
      AttendreAideLibre();
      g_attendue = 0;
    }
    __imp__sub_822EEE10(ctx, base);
    return;
  }
  // Alternance dans la partie (banjo_renut_ab_target = 19) : décidée à l'entrée d'une chaîne (n != g_attendue, comme
  // l'envoi d'une chaîne au fil d'aide ci-dessous), gardée jusqu'à sa fin. Avant de passer à l'ancienne, le fil d'aide doit
  // avoir fini les têtes de la chaîne précédente.
  if (n != g_attendue)
    g_chaine_ancienne = banjo::renut_gpu::g_ab_off.load(std::memory_order_relaxed) == 19;
  if (g_chaine_ancienne) {
    if (n != g_attendue) {
      ++g_chaines_anciennes;
      AttendreAideLibre();
    }
    g_attendue = SceneSuivante(base, n);
    __imp__sub_822EEE10(ctx, base);
    Bilan();
    return;
  }
  const auto debut = Horloge::now();
  if (n != g_attendue) {
    // Nouvelle chaîne de scènes : confiée au fil d'aide.
    if (g_attendue) ++g_hors_chaine;
    {
      std::unique_lock lock(g_mutex);
      g_dessin_attend = true;
      g_cv_dessin.wait(lock, [] { return g_aide_libre && !g_poste; });
      g_dessin_attend = false;
      g_faites.store(0, std::memory_order_relaxed);
      g_rang = 0;
      g_poste = n;
    }
    g_cv_aide.notify_one();
    ++g_chaines;
  }
  AttendreTete(g_rang, n);
  g_ns_attente += uint64_t(std::chrono::nanoseconds(Horloge::now() - debut).count());
  ++g_rang;
  ++g_scenes;
  g_attendue = SceneSuivante(base, n);
  Queue(ctx, base, n);
  // Les éléments intercalés ne doivent pas chevaucher une tête : le fil d'aide doit avoir fini avant eux.
  const uint32_t suivant = Lit(base, n + 12);
  if (suivant && Lit(base, Lit(base, suivant) + 4) != kFonctionScene) AttendreAideLibre();
  Bilan();
}

// Le grand verrou : pour le fil d'aide, déjà acquis (le fil de dessin le tient pendant tout le parcours).
REX_HOOK_RAW(sub_8232C5B0) {
  if (ctx.r13.u32 == g_r13_aide.load(std::memory_order_relaxed)) {
    ctx.r3.u64 = 0;
    return;
  }
  __imp__sub_8232C5B0(ctx, base);
}

REX_HOOK_RAW(sub_8232C720) {
  if (ctx.r13.u32 == g_r13_aide.load(std::memory_order_relaxed)) {
    ctx.r3.u64 = 0;
    return;
  }
  __imp__sub_8232C720(ctx, base);
}

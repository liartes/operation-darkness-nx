// opdarkness - boost du processeur pendant les chargements (2026-11-01).
//
// Les chargements du jeu se font derrière un écran animé (2D\BG\LOADING_*.ptc, 60 images/s) : on ne peut pas les
// reconnaître à l'absence d'images. Ils se reconnaissent au fil de lecture du moteur (readDeviceThread, sub_82330990 :
// ouvrir, taille, allouer, lire, décompresser) occupé presque sans arrêt, alors qu'en partie il ne lit que par à-coups
// (combat : ~4 % du temps). Un fil de fond sonde toutes les 10 ms si une lecture est en cours (od_lecture.cpp appelle
// DebutLecture / FinLecture autour de sub_82330990) ; par fenêtres de 100 ms :
//   - od_boost_chargement_debut fenêtres de suite occupées à od_boost_chargement_seuil % au moins : début d'un
//     chargement ; boost (ApmCpuBoostMode_FastLoad : processeur à 1 785 MHz, plus sur une console réglée plus haut, et
//     GPU au plus bas) si od_boost_chargement ;
//   - od_boost_chargement_fin fenêtres de suite occupées à moins de 10 % : fin ; vitesse normale. Au plus
//     od_boost_chargement_max_s secondes de boost d'affilée (un fil qui lirait sans fin ne garde pas le GPU au plus bas).
// Chaque chargement est relevé dans le journal (« [chargement] »), boost ou pas : durée, temps de lecture, octets,
// images/s pendant le chargement, pour comparer avec et sans boost.
// Sur PC : relevé seulement (le boost n'existe pas).

#include <algorithm>
#include <atomic>
#include <chrono>
#include <pthread.h>
#include <cstdint>
#include <thread>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "od_images.h"
#include "od_switch_cpu.h"

REXCVAR_DEFINE_BOOL(od_boost_chargement, true, "OpDarkness",
                    "Switch : boost du processeur (FastLoad, GPU au plus bas) pendant les chargements du jeu (fil de "
                    "lecture du moteur occupé) ; false = relevé des chargements seulement");
REXCVAR_DEFINE_INT32(od_boost_chargement_seuil, 60, "OpDarkness",
                     "Part du temps (en %) où le fil de lecture du moteur lit, dans une fenêtre de 100 ms, au-delà de "
                     "laquelle la fenêtre compte comme chargement");
REXCVAR_DEFINE_INT32(od_boost_chargement_debut, 3, "OpDarkness",
                     "Fenêtres de 100 ms de suite au-delà du seuil pour reconnaître un chargement");
REXCVAR_DEFINE_INT32(od_boost_chargement_fin, 5, "OpDarkness",
                     "Fenêtres de 100 ms de suite sous 10 % pour finir un chargement");
REXCVAR_DEFINE_INT32(od_boost_chargement_max_s, 60, "OpDarkness",
                     "Durée maximale d'un boost d'affilée, en secondes");

namespace opdarkness::chargement {

namespace {
std::atomic<int32_t> g_actives{0};
std::atomic<uint64_t> g_octets{0};
std::atomic<uint64_t> g_lectures{0};
std::atomic<bool> g_parti{false};

using Horloge = std::chrono::steady_clock;

void Surveiller() {
  constexpr auto kSonde = std::chrono::milliseconds(10);
  constexpr int kSondesParFenetre = 10;
  int occupees = 0, sondes = 0;  // dans la fenêtre en cours
  int de_suite_hautes = 0, de_suite_basses = 0;
  bool en_chargement = false, boost = false;
  Horloge::time_point debut{};
  int sondes_occupees_total = 0;
  uint64_t octets0 = 0, lectures0 = 0, images0 = 0;
  for (;;) {
    std::this_thread::sleep_for(kSonde);
    if (g_actives.load(std::memory_order_relaxed) > 0) ++occupees;
    if (++sondes < kSondesParFenetre) continue;
    const int part = occupees * 100 / sondes;
    occupees = sondes = 0;
    if (en_chargement) sondes_occupees_total += part;
    if (part >= REXCVAR_GET(od_boost_chargement_seuil)) {
      ++de_suite_hautes;
      de_suite_basses = 0;
    } else if (part < 10) {
      ++de_suite_basses;
      de_suite_hautes = 0;
    } else {
      de_suite_hautes = de_suite_basses = 0;
    }
    const auto maintenant = Horloge::now();
    if (!en_chargement && de_suite_hautes >= std::max(1, REXCVAR_GET(od_boost_chargement_debut))) {
      en_chargement = true;
      // Le chargement a commencé avec la première de ces fenêtres.
      debut = maintenant - std::chrono::milliseconds(100 * de_suite_hautes);
      sondes_occupees_total = 100 * de_suite_hautes;
      octets0 = g_octets.load(std::memory_order_relaxed);
      lectures0 = g_lectures.load(std::memory_order_relaxed);
      images0 = ImagesPresentees();
      boost = REXCVAR_GET(od_boost_chargement) && switch_cpu::Boost(true);
    }
    if (!en_chargement) continue;
    const double duree = std::chrono::duration<double>(maintenant - debut).count();
    const bool trop_long = boost && duree >= double(std::max(1, REXCVAR_GET(od_boost_chargement_max_s)));
    if (de_suite_basses < std::max(1, REXCVAR_GET(od_boost_chargement_fin)) && !trop_long) continue;
    if (boost) switch_cpu::Boost(false);
    // La fin : sans les fenêtres calmes qui l'ont reconnue.
    const double fin = duree - (trop_long ? 0.0 : 0.1 * de_suite_basses);
    const uint64_t images = ImagesPresentees() - images0;
    REXLOG_INFO("[chargement] {:.2f} s{} : fil de lecture du moteur occupé {:.0f} % du temps, {} lectures, {:.1f} Mo, "
                "{:.1f} images/s{}",
                fin, boost ? " en boost" : (REXCVAR_GET(od_boost_chargement) ? " (boost refusé ou absent)" : ""),
                fin > 0 ? sondes_occupees_total / (fin * 10.0) : 0.0,
                g_lectures.load(std::memory_order_relaxed) - lectures0,
                double(g_octets.load(std::memory_order_relaxed) - octets0) / (1024.0 * 1024.0),
                duree > 0 ? double(images) / duree : 0.0, trop_long ? " ; arrêt du boost à la durée maximale" : "");
    en_chargement = boost = false;
    de_suite_hautes = de_suite_basses = 0;
  }
}

void* FilSurveiller(void*) {
  Surveiller();
  return nullptr;
}

void Partir() {
  bool attendu = false;
  if (!g_parti.compare_exchange_strong(attendu, true)) return;
  // pthread et pas std::thread(...).detach() : la libnx n'a pas de détachement (__syscall_thread_detach absent),
  // pthread_detach y rend une erreur et std::thread::detach lève alors std::system_error : plantage au démarrage,
  // à la première lecture (2026-11-02). Le fil ne se termine jamais : le résultat du détachement ne compte pas.
  pthread_attr_t attributs;
  pthread_attr_init(&attributs);
  pthread_attr_setstacksize(&attributs, 1u << 20);
  pthread_t fil;
  if (pthread_create(&fil, &attributs, &FilSurveiller, nullptr) == 0) {
    pthread_detach(fil);
  } else {
    REXLOG_ERROR("[chargement] fil de surveillance non créé : ni relevé ni boost des chargements");
  }
  pthread_attr_destroy(&attributs);
}
}  // namespace

void DebutLecture() {
  Partir();
  g_actives.fetch_add(1, std::memory_order_relaxed);
}

void FinLecture(uint32_t octets) {
  g_actives.fetch_sub(1, std::memory_order_relaxed);
  g_lectures.fetch_add(1, std::memory_order_relaxed);
  g_octets.fetch_add(octets, std::memory_order_relaxed);
}

}  // namespace opdarkness::chargement

// opdarkness - attente active du moteur audio du jeu (XAudio).
//
// sub_82890C30 : rendez-vous des fils de mixage. Le fil met son octet à 1 dans un mot de 64 bits partagé (r4 +
// octet n° [r13 + 268]), construit le masque des fils attendus (6 indicateurs aux décalages 308 à 328 de r3, un octet
// chacun, du plus fort au plus faible), puis boucle tant que le mot ne vaut ni 0 (un autre fil a conclu) ni ce masque
// (il conclut : remet le mot à 0). Sur la Xbox, la boucle contient db16cyc (céder le cœur à l'autre fil matériel) ;
// recompilée, elle tourne à plein : sur la console, les deux fils de mixage y passent plus de la moitié de leur temps
// (53 et 57 % de leurs échantillons, profil du 2026-10-16, ~25-30 % d'un cœur sur 3). Ici : mêmes calculs, mêmes accès
// mémoire et mêmes registres en sortie ; seule la façon d'attendre change (od_audio_attente).
//
// Mode 2 (bloquer) : le fil tourne un court instant (od_audio_attente_tours), puis dort sur une variable de condition.
// Tout fil qui écrit le mot (son octet, ou le 0 de la conclusion) réveille les dormeurs ; l'attente est bornée
// (1 ms) au cas où le mot changerait ailleurs que dans cette fonction.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>

#if defined(__x86_64__)
#include <immintrin.h>
#endif

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>

#include "od_switch_cpu.h"

// Mode 1 mesuré sur la console (2026-10-14, tutoriel, moyennes de 19 intervalles) : les fils de mixage passent de 25,6
// à 18,3 % mais le fil qui dessine de 7,8 à 16,1 % et vmsObjectThread de 10,0 à 15,0 % (total 147 -> 158 %).
REXCVAR_DEFINE_INT32(od_audio_attente, 0, "OpDarkness",
                     "Rendez-vous des fils de mixage audio du jeu (sub_82890C30) : 0 = code d'origine (attente "
                     "active), 1 = céder le processeur après quelques tours, 2 = dormir jusqu'au réveil par un autre "
                     "fil (même résultat)");
REXCVAR_DEFINE_INT32(od_audio_attente_tours, 200, "OpDarkness",
                     "Mode 2 de od_audio_attente : tours d'attente active avant de dormir");
REXCVAR_DEFINE_BOOL(od_audio_stats, false, "OpDarkness",
                    "Journal du rendez-vous des fils de mixage toutes les 10 s (appels, temps d'attente, sommeils)");

namespace {

inline uint32_t Load32(const uint8_t* base, uint32_t address) {
  uint32_t v;
  std::memcpy(&v, base + address, 4);
  return __builtin_bswap32(v);
}

inline uint64_t Load64(uint8_t* base, uint32_t address) {
  return __builtin_bswap64(__atomic_load_n(reinterpret_cast<uint64_t*>(base + address), __ATOMIC_SEQ_CST));
}

inline void Pause() {
#if defined(__x86_64__)
  _mm_pause();
#elif defined(__aarch64__)
  asm volatile("yield");
#endif
}

// Mode 2 : dormeurs et réveil. Le dormeur s'inscrit (g_dormeurs), relit le mot sous le verrou, puis dort ; l'écrivain
// écrit le mot, puis prend le verrou pour réveiller s'il y a des inscrits : pas de réveil perdu.
std::mutex g_verrou;
std::condition_variable g_reveil;
std::atomic<int> g_dormeurs{0};

void Reveiller() {
  if (g_dormeurs.load(std::memory_order_seq_cst) == 0) return;
  std::lock_guard<std::mutex> g(g_verrou);
  g_reveil.notify_all();
}

// Relevé (od_audio_stats).
std::atomic<uint64_t> g_appels{0}, g_ns_attente{0}, g_sommeils{0}, g_ns_pire{0};

void Bilan(std::chrono::steady_clock::time_point maintenant) {
  static std::mutex verrou;
  static auto precedent = maintenant;
  std::lock_guard<std::mutex> g(verrou);
  if (maintenant - precedent < std::chrono::seconds(10)) return;
  const double s = std::chrono::duration<double>(maintenant - precedent).count();
  precedent = maintenant;
  const uint64_t appels = g_appels.exchange(0), ns = g_ns_attente.exchange(0), sommeils = g_sommeils.exchange(0),
                 pire = g_ns_pire.exchange(0);
  REXLOG_INFO("[audio] rendez-vous des fils de mixage (mode {}) : {:.0f} par seconde, attente moyenne {:.1f} us "
              "(pire {:.0f} us), {:.1f} % du temps d'un cœur ; {:.0f} sommeils par seconde",
              REXCVAR_GET(od_audio_attente), appels / s, appels ? ns / 1e3 / appels : 0.0, pire / 1e3,
              ns / 1e7 / s, sommeils / s);
}

}  // namespace

REX_EXTERN(__imp__sub_82890C30);
REX_HOOK_RAW(sub_82890C30) {
  // Mode quatre cœurs (2026-11-01, comme renut-nx pour les fils de mixage d'XAudio) : seuls les fils de mixage passent
  // par ce rendez-vous ; au premier passage, chacun va seul sur le cœur gardé (sans effet avec trois cœurs). Tous y
  // vont, sinon ceux qui y restent attendraient les autres à chaque rendez-vous.
  static thread_local bool place = false;
  if (!place) [[unlikely]] {
    place = true;
    if (opdarkness::switch_cpu::VersCoeurSysteme(opdarkness::switch_cpu::PrioriteMixage()))
      REXLOG_INFO("[audio] fil de mixage sur le cœur du système (od_coeur_systeme)");
  }
  const int32_t mode = REXCVAR_GET(od_audio_attente);
  const bool stats = REXCVAR_GET(od_audio_stats);
  if (mode == 0 && !stats) {
    __imp__sub_82890C30(ctx, base);
    return;
  }
  const uint32_t r3 = ctx.r3.u32, r4 = ctx.r4.u32;
  // lwz r11,304(r3) ; cmpwi cr6,r11,0 ; beqlr cr6
  ctx.r11.u64 = Load32(base, r3 + 304);
  if (ctx.r11.u32 == 0) return;
  const auto debut = std::chrono::steady_clock::now();
  // lbz r11,268(r13) ; li r10,1 ; stbx r10,r11,r4
  ctx.r11.u64 = base[ctx.r13.u32 + 268];
  ctx.r10.u64 = 1;
  __atomic_store_n(base + r4 + ctx.r11.u32, uint8_t(1), __ATOMIC_SEQ_CST);
  if (mode == 2) Reveiller();
  // Masque : indicateur non nul -> 1 dans l'octet de poids fort, puis décalage arithmétique de 8 (rldicr / sradi / or).
  // a = f328 << 56 ; puis a = (f << 56) | (a >> 8) pour 324, 320, 316, 312 ; r9 = a >> 8 ; masque = (f308 << 56) | r9.
  const auto bit = [&](uint32_t decalage) {
    return Load32(base, r3 + decalage) != 0 ? int64_t(1) << 56 : int64_t(0);
  };
  int64_t a = bit(328);
  for (const uint32_t decalage : {324u, 320u, 316u, 312u}) a = bit(decalage) | (a >> 8);
  const int64_t r9 = a >> 8;
  const uint64_t masque = uint64_t(bit(308) | r9);
  ctx.r9.s64 = r9;
  ctx.r10.u64 = masque;
  const uint32_t tours_avant_sommeil = uint32_t(std::max(REXCVAR_GET(od_audio_attente_tours), 0));
  // ld r11,0(r4) ; cmpdi cr6,r11,0 ; beqlr cr6 ; boucle : cmpd r10,r11 ; beq -> std 0 ; db16cyc ; ld ; bne
  uint64_t mot = Load64(base, r4);
  for (uint32_t tours = 0;; ++tours) {
    if (mot == 0 || mot == masque) {
      ctx.r11.u64 = 0;
      if (mot == masque) {
        __atomic_store_n(reinterpret_cast<uint64_t*>(base + r4), uint64_t(0), __ATOMIC_SEQ_CST);
        if (mode == 2) Reveiller();
      }
      if (stats) {
        const auto fin = std::chrono::steady_clock::now();
        const uint64_t ns = uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(fin - debut).count());
        g_appels.fetch_add(1, std::memory_order_relaxed);
        g_ns_attente.fetch_add(ns, std::memory_order_relaxed);
        uint64_t pire = g_ns_pire.load(std::memory_order_relaxed);
        while (ns > pire && !g_ns_pire.compare_exchange_weak(pire, ns, std::memory_order_relaxed)) {
        }
        Bilan(fin);
      }
      return;
    }
    if (mode == 2 && tours >= tours_avant_sommeil) {
      g_dormeurs.fetch_add(1, std::memory_order_seq_cst);
      {
        std::unique_lock<std::mutex> g(g_verrou);
        if (Load64(base, r4) == mot) g_reveil.wait_for(g, std::chrono::milliseconds(1));
      }
      g_dormeurs.fetch_sub(1, std::memory_order_seq_cst);
      if (stats) g_sommeils.fetch_add(1, std::memory_order_relaxed);
    } else if (mode == 1 && tours >= 64) {
      std::this_thread::yield();
    } else {
      Pause();
    }
    mot = Load64(base, r4);
  }
}

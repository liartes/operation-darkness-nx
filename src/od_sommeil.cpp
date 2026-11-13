// opdarkness - Sleep(0) du jeu.
//
// Le moteur du jeu (threads « vms » : vmsDrawThread, vmsSystemThread, vmsObjectThread, readDeviceThread...) se passe
// la main par une boucle « RtlTryEnterCriticalSection, sinon Sleep(0), recommencer » (sub_8232C5B0, sub_8232F258,
// sub_82330CC8...). Sur la console, Sleep(0) cède seulement le processeur. Le SDK (XThread::Delay) fait de même pour
// un thread de priorité normale, mais dort 100 µs pour un thread de priorité « below normal » ou moins, ce que sont ces
// threads : chaque passage de main coûte au moins 100 µs (plus la marge des minuteries de Linux), et le jeu tombait à
// ~8 images/s sans qu'aucun thread soit occupé (tools/piles.sh, 2026-10-08).
//
// --od_sleep0_cede (vrai par défaut) : SleepEx(0, FALSE) du jeu (sub_8239AF20, appelé par Sleep, sub_82398CF0) renvoie
// 0 comme l'original sans passer par le noyau du SDK. Céder partout (std::this_thread::yield) occupait ~5 cœurs : les
// threads qui scrutent une file de travail (lecture de fichiers, de périphérique, un ouvrier) tournent alors à plein.
// Ces sites (adresses de retour, --od_sleep0_scruteurs, relevés par --od_sleep0_stats) dorment --od_sleep0_us µs ;
// partout ailleurs (Sleep(0) de courtoisie après RtlLeaveCriticalSection, sur le chemin de chaque image) on cède
// seulement. Marge des minuteries de Linux ramenée à 1 µs pour ces threads (50 µs par défaut).
//
// Mesures en combat (tools/mesure_combat.sh, 2026-10-08) : 16 Sleep(0) consécutifs qui cèdent puis 20 µs : 24,8
// images/s, 1,6 cœur ; céder toujours : 59,8 images/s mais 5 cœurs. Les Sleep(0) de courtoisie (comptes de références
// du moteur, sub_8232CEC0...) se suivent : le compteur de consécutifs les faisait dormir. Politique par site d'appel :
// 59,8 images/s à 2,3 cœurs (scruteurs à 200 µs), 60,0 à 1,8 cœur (1 ms, valeur retenue). L'attente du verrou raté
// (od_sleep0_verrou_us) n'a rien changé (24,0 contre 25,0) : ces Sleep(0) ne suivent presque jamais un TryEnter raté. La boucle « TryEnter, sinon Sleep(0) » n'est qu'une prise bloquante du
// verrou : avec le SDK de sa2-nx corrigé (patches/sa2-nx-sdk.patch, WaitMissedCriticalSection), un Sleep(0) qui suit un
// RtlTryEnterCriticalSection raté attend que ce verrou soit libéré (au plus --od_sleep0_verrou_us µs) au lieu de
// tourner ou de dormir un temps fixe. Les autres Sleep(0) (threads qui attendent du travail) gardent la politique
// ci-dessus.
//
// --od_sleep0_courtoisie (vrai par défaut, 2026-10-09) : le Sleep(0) qui suit immédiatement un
// RtlLeaveCriticalSection (« bl RtlLeaveCriticalSection ; li r3,0 ; bl Sleep », reconnu au code avant l'adresse de
// retour) rend la main tout de suite, sans céder le processeur. C'est la politesse du moteur après chaque verrou
// relâché : comptes de références (sub_8232CEC0, sub_8232CF90), files (sub_8232C5B0, sub_8232C720)... ~75 % des
// ~120 000 Sleep(0) par seconde en combat, presque tous sur le fil qui dessine (un appel système sched_yield
// chacun). Sur la console, ce Sleep(0) laissait un autre fil du même cœur prendre le verrou ; ici un fil qui l'attend
// est réveillé par la libération elle-même (WaitMissedCriticalSection) et tourne sur un autre cœur.

#if defined(__linux__)
#include <sys/prctl.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/kernel/xboxkrnl/rtl.h>
#include <rex/logging.h>
#include <rex/ppc.h>

REXCVAR_DEFINE_BOOL(od_sleep0_cede, true, "OpDarkness",
                    "Sleep(0) du jeu : céder le processeur (comme la console) au lieu de dormir 100 µs dans le SDK "
                    "pour les threads de basse priorité");

REXCVAR_DEFINE_STRING(od_sleep0_scruteurs, "82330D28,8232FF00,82330194,82237B30", "OpDarkness",
                      "Adresses de retour des Sleep(0) des threads qui attendent du travail (scrutation) : ceux-là "
                      "dorment od_sleep0_us µs, tous les autres cèdent seulement le processeur");
REXCVAR_DEFINE_INT32(od_sleep0_us, 1000, "OpDarkness",
                     "Sommeil (µs) des Sleep(0) des sites de od_sleep0_scruteurs (0 = céder aussi)");

REXCVAR_DEFINE_INT32(od_sleep0_verrou_us, 1000, "OpDarkness",
                     "Sleep(0) après un RtlTryEnterCriticalSection raté : attendre la libération de ce verrou, au plus "
                     "ce nombre de µs (0 = non ; SDK de sa2-nx corrigé seulement)");

REXCVAR_DEFINE_BOOL(od_sleep0_courtoisie, true, "OpDarkness",
                    "Sleep(0) qui suit un RtlLeaveCriticalSection : rendre la main sans céder le processeur");

REXCVAR_DEFINE_BOOL(od_sleep0_stats, false, "OpDarkness",
                    "Diagnostic : compte les Sleep(0) par site d'appel (adresse de retour) et les écrit toutes les 10 s");

namespace {

// --od_sleep0_stats : nombre de Sleep(0) par adresse de retour (lr : Sleep, sub_82398CF0, saute à SleepEx sans
// changer lr), bilan toutes les 10 s.
void Statistiques(uint32_t lr) {
  using Clock = std::chrono::steady_clock;
  static std::mutex mutex;
  static std::unordered_map<uint32_t, uint64_t> comptes;
  static Clock::time_point debut = Clock::now();
  std::lock_guard<std::mutex> lock(mutex);
  ++comptes[lr];
  const auto maintenant = Clock::now();
  if (maintenant - debut < std::chrono::seconds(10)) return;
  std::vector<std::pair<uint32_t, uint64_t>> tri(comptes.begin(), comptes.end());
  std::sort(tri.begin(), tri.end(), [](auto& a, auto& b) { return a.second > b.second; });
  std::string ligne;
  for (size_t i = 0; i < tri.size() && i < 10; ++i) ligne += fmt::format(" {:08X}:{}", tri[i].first, tri[i].second);
  REXLOG_INFO("[sleep0] 10 s, par adresse de retour :{}", ligne);
  comptes.clear();
  debut = maintenant;
}

// Sites de scrutation (od_sleep0_scruteurs), lus une fois.
const std::vector<uint32_t>& Scruteurs() {
  static const std::vector<uint32_t> sites = [] {
    std::vector<uint32_t> v;
    const std::string liste = REXCVAR_GET(od_sleep0_scruteurs);
    size_t i = 0;
    while (i < liste.size()) {
      size_t j = liste.find(',', i);
      if (j == std::string::npos) j = liste.size();
      if (j > i) v.push_back(uint32_t(std::stoul(liste.substr(i, j - i), nullptr, 16)));
      i = j + 1;
    }
    return v;
  }();
  return sites;
}

inline uint32_t Load32(const uint8_t* base, uint32_t address) {
  uint32_t v;
  std::memcpy(&v, base + address, 4);
  return __builtin_bswap32(v);
}

// Adresse de la fonction d'importation RtlLeaveCriticalSection du jeu (bl 0x828E5A84 dans le code traduit).
constexpr uint32_t kRtlLeaveCriticalSection = 0x828E5A84;

// Sleep(0) de courtoisie : « bl RtlLeaveCriticalSection ; li r3,0 ; bl Sleep » juste avant l'adresse de retour.
bool Courtoisie(const uint8_t* base, uint32_t lr) {
  if (lr < 0x82000000u + 12 || lr >= 0x83000000u) return false;
  if (Load32(base, lr - 8) != 0x38600000u) return false;  // li r3,0
  const uint32_t bl = Load32(base, lr - 12);
  if ((bl & 0xFC000003u) != 0x48000001u) return false;  // bl (relatif, avec lien)
  const int32_t deplacement = int32_t(bl << 6) >> 6 & ~3;
  return lr - 12 + uint32_t(deplacement) == kRtlLeaveCriticalSection;
}

void Sleep0(uint32_t lr) {
#if defined(__linux__)
  thread_local bool marge_reglee = false;  // thread_local : un appel de fonction sur la Switch, d'où la garde
  if (!marge_reglee) {
    prctl(PR_SET_TIMERSLACK, 1000UL, 0, 0, 0);  // ns
    marge_reglee = true;
  }
#endif
#if defined(REX_HAS_WAIT_MISSED_CRITICAL_SECTION)
  if (const int32_t verrou = REXCVAR_GET(od_sleep0_verrou_us);
      verrou > 0 && rex::kernel::xboxkrnl::WaitMissedCriticalSection(uint32_t(verrou))) {
    return;
  }
#endif
  const int32_t us = REXCVAR_GET(od_sleep0_us);
  const auto& scruteurs = Scruteurs();
  if (us > 0 && std::find(scruteurs.begin(), scruteurs.end(), lr) != scruteurs.end()) {
    std::this_thread::sleep_for(std::chrono::microseconds(us));
  } else {
    std::this_thread::yield();
  }
}

}  // namespace

REX_EXTERN(__imp__sub_8239AF20);
REX_HOOK_RAW(sub_8239AF20) {  // SleepEx(r3 = millisecondes, r4 = alertable)
  if (ctx.r3.u32 == 0 && REXCVAR_GET(od_sleep0_stats)) Statistiques(uint32_t(ctx.lr));
  if (ctx.r3.u32 == 0 && (ctx.r4.u32 & 0xFF) == 0 && REXCVAR_GET(od_sleep0_cede)) {
    if (!(REXCVAR_GET(od_sleep0_courtoisie) && Courtoisie(base, uint32_t(ctx.lr)))) Sleep0(uint32_t(ctx.lr));
    ctx.r3.u64 = 0;
    return;
  }
  __imp__sub_8239AF20(ctx, base);
}

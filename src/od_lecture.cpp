// opdarkness - chargeur de fichiers du moteur qui annonce « terminé » avec un tampon nul (plantage du 2026-10-14).
//
// sub_82216820 : méthode « état » (vtable +8) d'un chargeur asynchrone (vtable 8210F334) ; elle fait avancer son
// automate (champ +4) : 1 -> 2 (ouvrir) -> 3 (attendre la lecture du fil readDeviceThread ; à la fin : taille au champ
// +12, tampon au champ +16, ou retour en 2 si la taille est négative) -> 4 -> 5 (terminé). L'interface
// (sub_82206878) lit alors le tampon sans le vérifier. Vu une fois sur la console, au démarrage, pendant le
// chargement des fichiers 2D\MENU\*.ptc (lecture de l'adresse 4), jamais sur PC : un tampon nul annoncé terminé.
//
// Ici, seulement dans ce cas (état 5 rendu avec un tampon nul) : le journal décrit le chargeur et sa requête, et
// l'automate repart en 2 (rouvrir et relire), comme le jeu le fait pour une taille négative ; au plus 5 fois par
// chargeur, ensuite comportement d'origine. Tout autre retour est inchangé.

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <mutex>
#include <unordered_map>
#include <chrono>
#include <thread>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>

REXCVAR_DEFINE_BOOL(od_chargeur_relance, true, "OpDarkness",
                    "Chargeur de fichiers du moteur (sub_82216820) terminé avec un tampon nul : journal et nouvel essai "
                    "au lieu du plantage de l'interface");

namespace opdarkness::chargement {
void DebutLecture();
void FinLecture(uint32_t octets);
}  // namespace opdarkness::chargement

namespace {

uint32_t L32(const uint8_t* base, uint32_t a) {
  uint32_t v;
  std::memcpy(&v, base + a, 4);
  return __builtin_bswap32(v);
}

void S32(uint8_t* base, uint32_t a, uint32_t v) {
  v = __builtin_bswap32(v);
  std::memcpy(base + a, &v, 4);
}

}  // namespace

REX_EXTERN(__imp__sub_82216820);
REX_HOOK_RAW(sub_82216820) {
  const uint32_t chargeur = ctx.r3.u32;
  __imp__sub_82216820(ctx, base);
  if (ctx.r3.u32 != 5 || L32(base, chargeur + 16) != 0 || !REXCVAR_GET(od_chargeur_relance)) return;
  static std::mutex verrou;
  static std::unordered_map<uint32_t, int> essais;
  int n;
  {
    std::lock_guard<std::mutex> g(verrou);
    n = ++essais[chargeur];
  }
  const uint32_t requete = L32(base, chargeur + 356);  // champ +4 de l'objet de lecture (+352)
  REXLOG_WARN("[chargeur] {:08X} terminé avec un tampon nul (essai {}) : +8 {:08X}, taille {}, +348 {} ; requête "
              "{:08X} : +100 {:08X} +104 {:08X} +108 {:08X} +112 {:08X} +116 {:08X} +128 {:08X}",
              chargeur, n, L32(base, chargeur + 8), int32_t(L32(base, chargeur + 12)), L32(base, chargeur + 348),
              requete, requete ? L32(base, requete + 100) : 0, requete ? L32(base, requete + 104) : 0,
              requete ? L32(base, requete + 108) : 0, requete ? L32(base, requete + 112) : 0,
              requete ? L32(base, requete + 116) : 0, requete ? L32(base, requete + 128) : 0);
  if (n > 5) return;
  S32(base, chargeur + 4, 2);  // rouvrir et relire (l'automate du jeu)
  ctx.r3.u64 = 2;
}

// Fin de lecture du moteur (sub_821A8A48, fil readDeviceThread) : récupère le tampon de la requête (sub_82330418 :
// objet de requête Q = [H + 4], tampon Q + 112, taille Q + 108) et le marque (sub_8232C460(tampon, 4)), ce qui plante
// sur un tampon nul (vu sur la console au démarrage, avec ou sans les options de l'étape D : 2026-10-14 et 2026-10-16,
// juste après deux ouvertures du même fichier). Ici : tampon nul -> journal (fichier, taille, état) et fin de lecture
// sautée au lieu du plantage.
REXCVAR_DEFINE_BOOL(od_lecture_nulle_sautee, true, "OpDarkness",
                    "Fin de lecture du moteur (sub_821A8A48) avec un tampon nul : journal et traitement sauté au lieu "
                    "du plantage");

namespace {

std::string ChaineInvitee(const uint8_t* base, uint32_t adresse) {
  std::string s;
  if (adresse < 0x10000 || adresse >= 0xF0000000u) return s;
  for (uint32_t i = 0; i < 160; ++i) {
    const char c = char(base[adresse + i]);
    if (!c) break;
    s += (c >= 32 && c < 127) ? c : '?';
  }
  return s;
}

// Dernière lecture rendue par sub_82330990 (relevé du 2026-10-21) : requête, tampon et taille juste après la lecture,
// pour les comparer à la fin de lecture qui les trouve à 0.
std::atomic<uint32_t> g_derniere_q{0}, g_dernier_tampon{0}, g_derniere_taille{0}, g_dernier_fil{0}, g_dernier_nom{0};

}  // namespace

REX_EXTERN(__imp__sub_821A8A48);
REX_HOOK_RAW(sub_821A8A48) {
  const uint32_t h = ctx.r3.u32;
  const uint32_t q = h ? L32(base, h + 4) : 0;
  if (q && L32(base, q + 112) == 0 && REXCVAR_GET(od_lecture_nulle_sautee)) {
    const uint32_t t = g_dernier_fil.load(), rq = t ? L32(base, t + 12) : 0;
    const uint32_t o52 = rq ? L32(base, rq + 52) : 0;
    REXLOG_WARN("[lecture] dernière lecture : fil {:08X} requête {:08X} '{}' tampon {:08X} taille {} ; requête maintenant "
                "{:08X} : +40 {:08X} +100 '{}' +108 {} +112 {:08X} +116 {:08X} ; méthode +52 : objet {:08X} [1] {:08X}",
                t, g_derniere_q.load(), ChaineInvitee(base, g_dernier_nom.load()), g_dernier_tampon.load(),
                g_derniere_taille.load(), rq, rq ? L32(base, rq + 40) : 0,
                rq ? ChaineInvitee(base, L32(base, rq + 100)) : "", rq ? int32_t(L32(base, rq + 108)) : 0,
                rq ? L32(base, rq + 112) : 0, rq ? L32(base, rq + 116) : 0, o52, o52 ? L32(base, o52 + 4) : 0);
    REXLOG_WARN("[lecture] fin de lecture sans tampon : requête {:08X} objet {:08X} fichier '{}' ; +96 {:08X} +104 "
                "{:08X} taille {} +116 {:08X} +120 {:08X} +124 {:08X} +128 {:08X} +132 {:08X} : traitement sauté",
                h, q, ChaineInvitee(base, L32(base, q + 100)), L32(base, q + 96), L32(base, q + 104),
                int32_t(L32(base, q + 108)), L32(base, q + 116), L32(base, q + 120), L32(base, q + 124),
                L32(base, q + 128), L32(base, q + 132));
    return;
  }
  __imp__sub_821A8A48(ctx, base);
}

// sub_8232C460(bloc, n) : sous le verrou de l'allocateur du moteur, lit l'en-tête du bloc ([bloc - 64]) et n'agit que
// pour un bloc de type 1. Appelée avec le tampon nul d'une lecture ratée (fin de lecture sub_821A8A48, puis côté jeu
// sub_821A8E30) : plantage. Un pointeur nul n'a pas d'en-tête : retour immédiat, comme pour un bloc d'un autre type.
REX_EXTERN(__imp__sub_8232C460);
REX_HOOK_RAW(sub_8232C460) {
  if (ctx.r3.u32 == 0 && REXCVAR_GET(od_lecture_nulle_sautee)) {
    static std::atomic<uint32_t> n{0};
    if (++n <= 20) REXLOG_WARN("[lecture] sub_8232C460 appelée avec un bloc nul (n = {}) : ignorée", ctx.r4.u32);
    return;
  }
  __imp__sub_8232C460(ctx, base);
}

// ReadFile du jeu (sub_82398878 : r3 fichier, r4 tampon, r5 taille, r6 octets lus, r7 OVERLAPPED) : journal des échecs
// avec le code d'erreur du fil (GetLastError, sub_82399368 -> sub_8239ACB8 : [[r13 + 256] + 352], 0 si [r13 + 336]).
REX_EXTERN(__imp__sub_82398878);
REX_HOOK_RAW(sub_82398878) {
  const uint32_t fichier = ctx.r3.u32, tampon = ctx.r4.u32, taille = ctx.r5.u32, lus = ctx.r6.u32,
                 overlapped = ctx.r7.u32, r13 = ctx.r13.u32;
  __imp__sub_82398878(ctx, base);
  if (ctx.r3.u32 != 0) return;
  const uint32_t erreur = L32(base, r13 + 336) ? 0 : L32(base, L32(base, r13 + 256) + 352);
  if (overlapped && erreur == 997) return;  // ERROR_IO_PENDING : lecture asynchrone en cours (normal)
  static std::atomic<uint32_t> n{0};
  if (++n <= 50)
    REXLOG_WARN("[lecture] ReadFile du jeu en échec : fichier {:08X} tampon {:08X} taille {} lus {} overlapped {:08X} "
                "erreur {:08X}", fichier, tampon, taille, lus ? int32_t(L32(base, lus)) : -1, overlapped, erreur);
}

// GetFileSize du jeu (sub_82398810 : r3 fichier, r4 mot haut ; rend 0xFFFFFFFF en échec). Hypothèse du tampon nul
// (2026-10-16) : une taille en échec fait demander au fil de lecture (sub_82330990) une allocation impossible (tampon
// nul) puis une lecture de 0xFFFFFFFF octets que l'enveloppe ReadFile prend pour une fin de fichier réussie. Sans effet
// de bord : en échec, journal et nouvel essai (5 fois, 2 ms).
REX_EXTERN(__imp__sub_82398810);
REX_HOOK_RAW(sub_82398810) {
  const PPCContext avant = ctx;
  for (int essai = 1;; ++essai) {
    __imp__sub_82398810(ctx, base);
    if (ctx.r3.u32 != 0xFFFFFFFFu) {
      if (essai > 1) REXLOG_WARN("[lecture] GetFileSize du fichier {:08X} réussi au {}e essai : {}", avant.r3.u32, essai,
                                 ctx.r3.u32);
      return;
    }
    const uint32_t r13 = avant.r13.u32;
    const uint32_t erreur = L32(base, r13 + 336) ? 0 : L32(base, L32(base, r13 + 256) + 352);
    REXLOG_WARN("[lecture] GetFileSize du jeu en échec : fichier {:08X} erreur {:08X} (essai {})", avant.r3.u32, erreur,
                essai);
    if (essai >= 5 || !REXCVAR_GET(od_lecture_nulle_sautee)) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    ctx = avant;
  }
}

// Lecture d'une requête par le fil readDeviceThread (sub_82330990 : r3 fil T, r4 mode = [T], r5 requête Q). Le mode
// choisit la source : 0 et 1 lisent le fichier puis écrivent la taille (Q + 108) et le tampon (Q + 112) ; tout autre
// mode écrit un message de débogage du moteur et rend 0 (« réussi ») sans rien lire : la fin de lecture reçoit alors
// la taille et le tampon laissés à 0 (2026-10-21, la cause des « fins de lecture sans tampon »). Relevé de ces cas.
REXCVAR_DEFINE_BOOL(od_lecture_mode_releve, true, "OpDarkness",
                    "Journal des lectures du fil readDeviceThread (sub_82330990) rendues réussies sans tampon "
                    "(mode de lecture autre que 0 ou 1)");
REX_EXTERN(__imp__sub_82330990);
REX_HOOK_RAW(sub_82330990) {
  const uint32_t t = ctx.r3.u32, mode = ctx.r4.u32, q = ctx.r5.u32;
  const uint32_t mode_t = t ? L32(base, t) : 0;
  opdarkness::chargement::DebutLecture();  // od_boost_chargement.cpp
  __imp__sub_82330990(ctx, base);
  opdarkness::chargement::FinLecture(q && ctx.r3.u32 == 0 ? L32(base, q + 108) : 0);
  if (q && ctx.r3.u32 == 0) {
    g_dernier_fil = t;
    g_derniere_q = q;
    g_dernier_tampon = L32(base, q + 112);
    g_derniere_taille = L32(base, q + 108);
    g_dernier_nom = L32(base, q + 100);
  }
  if (!REXCVAR_GET(od_lecture_mode_releve) || !q || ctx.r3.u32 != 0 || L32(base, q + 112) != 0) return;
  static std::atomic<int> n{0};
  if (++n > 40) return;
  REXLOG_WARN("[lecture] sub_82330990 réussie sans tampon : fil {:08X} mode {} (mode du fil avant {}, après {}) "
              "requête {:08X} fichier '{}' ; fil +4 {:08X} +8 {:08X} +12 {:08X} ; requête +40 {:08X} +104 {:08X}",
              t, mode, mode_t, L32(base, t), q, ChaineInvitee(base, L32(base, q + 100)), L32(base, t + 4),
              L32(base, t + 8), L32(base, t + 12), L32(base, q + 40), L32(base, q + 104));
}

// Répartiteur des requêtes de lecture (sub_82330018, r3 gestionnaire D) : retire une requête de la liste sous le verrou
// de D, puis, verrou relâché, cherche un fil de lecture au repos (événement T + 4 signalé), écrit la requête dans
// T + 12 et remet l'événement à « occupé ». Deux répartiteurs en même temps peuvent choisir le même fil : le second
// remplace la requête du premier pendant sa lecture (relevé du 2026-10-21). Relevé des appels simultanés.
REXCVAR_DEFINE_BOOL(od_lecture_repartiteur_releve, true, "OpDarkness",
                    "Journal des appels simultanés du répartiteur des lectures du moteur (sub_82330018)");
REX_EXTERN(__imp__sub_82330018);
REX_HOOK_RAW(sub_82330018) {
  static std::atomic<int> en_cours{0}, n{0};
  const uint32_t d = ctx.r3.u32;
  const uint32_t q = d ? L32(base, d + 128) : 0;
  const int avant = en_cours.fetch_add(1);
  if (REXCVAR_GET(od_lecture_repartiteur_releve) && avant > 0 && ++n <= 40)
    REXLOG_WARN("[lecture] répartiteur appelé pendant un autre ({} en cours) : gestionnaire {:08X} requête {:08X} '{}' ; "
                "{} fil(s) de lecture", avant, d, q, q ? ChaineInvitee(base, L32(base, q + 100)) : "",
                d ? int32_t(L32(base, d + 56)) : 0);
  __imp__sub_82330018(ctx, base);
  en_cours.fetch_sub(1);
}

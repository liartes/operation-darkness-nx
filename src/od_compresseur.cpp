// opdarkness - compresseur audio du moteur (sub_82335928) en natif.
//
// Profil de la console du 2026-10-17 (.nro LTO + PGO n°3) : 46 % du fil « Audio Worker », plus 12 % dans log10
// (sub_826A6A80) qu'il appelle pour chaque échantillon, soit ~5 % d'un cœur. La copie localisée (n_82335928) garde la
// structure de l'original : un appel de log10 par échantillon, avec synchronisation de tous les registres autour.
//
// Ce que fait la fonction (r3 = objet du compresseur, r4 = tampon audio) :
//   p = r4 ? r4 - 8 : 0 ; canaux = octet p+21 ; échantillons = mot p+28 (sub_82890440 recopie ces mots sur la pile)
//   si objet+44 : enveloppe (48), gain (52) et gain lissé (56) remis à la valeur de 820D93CC
//   pour chaque canal, 256 flottants (1 024 octets par canal) :
//     env = float(float(fma(c76, env, |x|)) / c72)                      (stocké en 48)
//     si env > seuil (c12) : g = pow(f29, float(-float(float(float(float(log10(env)) * f28) - c12) * c68) * f30))
//     sinon g = f27
//     lissé = float(fma(float(g - lissé), g > gain ? c60 : c64, lissé)) ; gain = g
//     si !objet+44 ou échantillon > 1 : x = float(float(c28 * lissé) * x)
// Ici : les mêmes opérations dans le même ordre et avec les mêmes arrondis (doubles des flottants, fma en double puis
// arrondi en float, comme le code généré). log10 n'est appelé que si env dépasse le seuil : c'est le seul cas où son
// résultat sert, et log / log10 du jeu n'écrivent que sur leur pile (vérifié sur leurs copies localisées) ; il passe
// par son interception (sub_826A6A80 : log de l'hôte, od_log_natif), pow par l'original du jeu, avec le même retour
// qu'avant (lr), sur un cadre de pile au même endroit.
// od_compresseur_verif : exécute aussi l'original et compare l'objet, les échantillons et r3.

#include "opdarkness_pch.h"

#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include "generated/hot/n_82335928.h"

REXCVAR_DEFINE_BOOL(od_compresseur, true, "OpDarkness",
                    "Compresseur audio du moteur (sub_82335928) en natif : log10 et pow seulement au-dessus du seuil");
REXCVAR_DEFINE_BOOL(od_compresseur_verif, false, "OpDarkness",
                    "Diagnostic : le compresseur natif est comparé à l'original à chaque appel (objet, échantillons, r3)");
REXCVAR_DEFINE_DOUBLE(od_compresseur_test_seuil, 0.0, "OpDarkness",
                      "Diagnostic avec od_compresseur_verif : seuil abaissé à cette valeur (en enveloppe linéaire) le "
                      "temps de la comparaison, pour exercer log10 et pow ; le son est alors compressé. 0 = seuil du jeu");
REXCVAR_DECLARE(bool, od_copies_localisees);

REX_EXTERN(__imp__sub_82335928);
REX_EXTERN(__imp__sub_826A17E8);  // pow
REX_EXTERN(sub_826A6A80);         // log10 (interception de od_copies.cpp)

namespace {

constexpr uint32_t kConstantes = 0x820D9424;  // r11 de l'original : f27 en 0, valeur de remise à zéro en -88
constexpr uint32_t kF29 = 0x8211B6C8;         // double
constexpr uint32_t kF30 = 0x8211B62C;
constexpr uint32_t kF28 = 0x8211B6EC;
constexpr uint32_t kRetourLog = 0x823359D0;
constexpr uint32_t kRetourPow = 0x82335A00;

std::atomic<uint64_t> g_echantillons{0}, g_au_dessus{0};  // pour le relevé de od_compresseur_verif

inline double Lf(uint8_t* base, uint32_t ea) {
  const uint32_t u = REX_LOAD_U32(ea);
  float f;
  std::memcpy(&f, &u, 4);
  return double(f);
}

inline void Ef(uint8_t* base, uint32_t ea, double d) {
  const float f = float(d);
  uint32_t u;
  std::memcpy(&u, &f, 4);
  REX_STORE_U32(ea, u);
}

inline double Arr(double d) { return double(float(d)); }  // arrondi en simple précision (frsp, opérations « s »)
// fmadds comme le code généré (tools/fma_single.py, 2026-11-13 : arrondi une fois, REX_FMADDS du pch ; la forme double
// d'avant si REX_FMADDS n'est pas défini).
inline double Fmadds(double a, double b, double c) {
#if defined(REX_FMADDS)
  return REX_FMADDS(a, b, c);
#else
  return Arr(std::fma(a, b, c));
#endif
}

struct Tampon {
  uint32_t canaux = 0;
  uint32_t echantillons = 0;
};

Tampon LireTampon(uint8_t* base, uint32_t r4) {
  const uint32_t p = r4 ? r4 - 8 : 0;
  return {uint32_t(REX_LOAD_U8(p + 21)), uint32_t(REX_LOAD_U32(p + 28))};
}

void Compresseur(PPCContext& ctx, uint8_t* base) {
  const uint32_t o = ctx.r3.u32;
  const uint64_t r1 = ctx.r1.u64;
  const uint64_t lr = ctx.lr;
  const uint32_t cadre = uint32_t(r1) - 192;
  ctx.fpscr.disableFlushMode();
  const Tampon t = LireTampon(base, ctx.r4.u32);
  if (REX_LOAD_U32(o + 44) != 0) {
    const double z = Lf(base, kConstantes - 88);
    Ef(base, o + 48, z);
    Ef(base, o + 52, z);
    Ef(base, o + 56, z);
  }
  if (t.canaux) {
    const double f27 = Lf(base, kConstantes);
    PPCRegister f29;
    f29.u64 = REX_LOAD_U64(kF29);
    const double f30 = Lf(base, kF30);
    const double f28 = Lf(base, kF28);
    bool cadre_pret = false;
    // Réglages lus une fois par appel (l'original les relit à chaque échantillon : seul un autre fil qui les change
    // pendant l'appel verrait la différence, au plus 256 échantillons plus tard). L'état (enveloppe 48, gain 52,
    // gain lissé 56) reste en registres et est écrit à chaque échantillon, comme dans l'original.
    const double seuil = Lf(base, o + 12), c28 = Lf(base, o + 28), c60 = Lf(base, o + 60), c64 = Lf(base, o + 64);
    const double c68 = Lf(base, o + 68), c72 = Lf(base, o + 72), c76 = Lf(base, o + 76);
    const bool debut_garde = REX_LOAD_U32(o + 44) != 0;  // les deux premiers échantillons de chaque canal intacts
    double env = Lf(base, o + 48), gain = Lf(base, o + 52), lisse = Lf(base, o + 56);
    for (uint32_t c = 0; c < t.canaux; ++c) {
      for (uint32_t i = 0; i < 256; ++i) {
        const uint32_t ea = t.echantillons + (c << 10) + i * 4;
        const double x = Lf(base, ea);
        env = Arr(Fmadds(c76, env, std::fabs(x)) / c72);
        Ef(base, o + 48, env);
        double g;
        if (env > seuil) {
          g_au_dessus.fetch_add(1, std::memory_order_relaxed);
          if (!cadre_pret) {  // le cadre de pile de l'original (chaîne arrière), pour log10 et pow
            REX_STORE_U32(cadre, uint32_t(r1));
            cadre_pret = true;
          }
          ctx.r1.u64 = cadre;
          ctx.f1.f64 = env;
          ctx.lr = kRetourLog;
          sub_826A6A80(ctx, base);
          ctx.fpscr.disableFlushMode();
          const double db = Arr(Arr(ctx.f1.f64) * f28);
          double a = Arr(db - seuil);
          a = Arr(a * c68);
          PPCRegister n;
          n.f64 = a;
          n.u64 ^= 0x8000000000000000ull;
          ctx.f2.f64 = Arr(n.f64 * f30);
          ctx.f1.u64 = f29.u64;
          ctx.lr = kRetourPow;
          __imp__sub_826A17E8(ctx, base);
          ctx.fpscr.disableFlushMode();
          g = Arr(ctx.f1.f64);
        } else {
          g = f27;
        }
        const double coef = g > gain ? c60 : c64;
        lisse = Fmadds(Arr(g - lisse), coef, lisse);
        if (!debut_garde || i > 1) Ef(base, ea, Arr(Arr(c28 * lisse) * x));
        gain = g;
        Ef(base, o + 52, g);
        Ef(base, o + 56, lisse);
      }
    }
  }
  g_echantillons.fetch_add(uint64_t(t.canaux) * 256, std::memory_order_relaxed);
  ctx.r1.u64 = r1;
  ctx.lr = lr;
  ctx.r3.u64 = 0;
}

std::atomic<uint64_t> g_verifs{0}, g_ecarts{0};

void Verifier(PPCContext& ctx, uint8_t* base) {
  const uint32_t o = ctx.r3.u32;
  const Tampon t = LireTampon(base, ctx.r4.u32);
  const size_t octets = size_t(t.canaux) * 1024;
  std::vector<uint8_t> objet_avant(80), ech_avant(octets), objet_attendu(80), ech_attendu(octets);
  const double test = REXCVAR_GET(od_compresseur_test_seuil);
  const uint32_t seuil_jeu = REX_LOAD_U32(o + 12);
  static std::atomic<uint32_t> seuils_vus{0};
  if (seuils_vus.fetch_add(1) < 4) {
    float f;
    std::memcpy(&f, &seuil_jeu, 4);
    REXLOG_INFO("[compresseur] seuil du jeu {:.6g}, {} canaux", f, t.canaux);
  }
  if (test > 0.0) Ef(base, o + 12, test);
  std::memcpy(objet_avant.data(), REX_RAW_ADDR(o), 80);
  if (octets) std::memcpy(ech_avant.data(), REX_RAW_ADDR(t.echantillons), octets);
  PPCContext reference = ctx;
  __imp__sub_82335928(reference, base);
  const uint32_t seuil_apres_original = REX_LOAD_U32(o + 12);
  std::memcpy(objet_attendu.data(), REX_RAW_ADDR(o), 80);
  if (octets) std::memcpy(ech_attendu.data(), REX_RAW_ADDR(t.echantillons), octets);
  std::memcpy(REX_RAW_ADDR(o), objet_avant.data(), 80);
  if (octets) std::memcpy(REX_RAW_ADDR(t.echantillons), ech_avant.data(), octets);
  Compresseur(ctx, base);
  if (test > 0.0) {
    // Un autre fil du jeu réécrit les réglages du compresseur (seuil compris) : une comparaison pendant laquelle le
    // seuil d'essai a été remplacé n'a pas vu le même seuil des deux côtés.
    const uint32_t essai = REX_LOAD_U32(o + 12);
    if (seuil_apres_original != essai || std::memcmp(objet_avant.data() + 12, &objet_attendu[12], 4) ||
        __builtin_bswap32(essai) != [&] { uint32_t u; std::memcpy(&u, objet_avant.data() + 12, 4); return u; }()) {
      static std::atomic<uint64_t> perturbees{0};
      ++perturbees;
      REX_STORE_U32(o + 12, seuil_jeu);
      if ((perturbees & (perturbees - 1)) == 0)
        REXLOG_INFO("[compresseur] {} comparaisons écartées (seuil réécrit par le jeu pendant la comparaison)",
                    perturbees.load());
      return;
    }
  }
  const bool objet = std::memcmp(objet_attendu.data(), REX_RAW_ADDR(o), 80) == 0;
  const bool ech = !octets || std::memcmp(ech_attendu.data(), REX_RAW_ADDR(t.echantillons), octets) == 0;
  if (test > 0.0) {  // le seuil du jeu revient (dans l'objet attendu aussi, pour la comparaison)
    REX_STORE_U32(o + 12, seuil_jeu);
    const uint32_t be = __builtin_bswap32(seuil_jeu);
    std::memcpy(objet_attendu.data() + 12, &be, 4);
  }
  const bool r3 = reference.r3.u64 == ctx.r3.u64 && reference.r1.u64 == ctx.r1.u64;
  const uint64_t n = ++g_verifs;
  if (!objet || !ech || !r3) {
    const uint64_t e = ++g_ecarts;
    if (e <= 5) {
      auto f = [](const uint8_t* p) {
        uint32_t u;
        std::memcpy(&u, p, 4);
        u = __builtin_bswap32(u);
        float x;
        std::memcpy(&x, &u, 4);
        return x;
      };
      for (size_t k = 0; k < 80; k += 4)
        if (std::memcmp(objet_attendu.data() + k, REX_RAW_ADDR(o + k), 4))
          REXLOG_WARN("[compresseur]   objet+{} : {:.9g} attendu, {:.9g} obtenu", k, f(objet_attendu.data() + k),
                      f(REX_RAW_ADDR(o + k)));
      for (size_t k = 0; k < octets; k += 4)
        if (std::memcmp(ech_attendu.data() + k, REX_RAW_ADDR(t.echantillons + k), 4)) {
          REXLOG_WARN("[compresseur]   échantillon {} (canal {}) : {:.9g} attendu, {:.9g} obtenu, entrée {:.9g}", k / 4 % 256,
                      k / 1024, f(ech_attendu.data() + k), f(REX_RAW_ADDR(t.echantillons + k)), f(ech_avant.data() + k));
          break;
        }
    }
    if (e <= 10 || (e & (e - 1)) == 0)
      REXLOG_WARN("[compresseur] écart ({} / {}, {} canaux) : objet {}, échantillons {}, r1/r3 {}", e, n, t.canaux,
                  objet ? "identique" : "DIFFÉRENT", ech ? "identiques" : "DIFFÉRENTS", r3 ? "identiques" : "DIFFÉRENTS");
  } else if ((n & (n - 1)) == 0 && n >= 1024) {
    REXLOG_INFO("[compresseur] {} vérifications, {} écarts ; {} échantillons dont {} au-dessus du seuil", n,
                g_ecarts.load(), g_echantillons.load(), g_au_dessus.load());
  }
}

}  // namespace

REX_HOOK_RAW(sub_82335928) {
  if (!REXCVAR_GET(od_copies_localisees)) __imp__sub_82335928(ctx, base);
  else if (!REXCVAR_GET(od_compresseur)) sa2::hot::n_82335928::Native(ctx, base);
  else if (REXCVAR_GET(od_compresseur_verif)) Verifier(ctx, base);
  else Compresseur(ctx, base);
}

// opdarkness - produit de matrices 4x4 du moteur, en natif (sub_82609660 et sub_82609788).
//
// Profil PGO du 2026-10-16 (tools/pgo_classement.py) : les deux fonctions les plus lourdes du jeu après le mixage
// audio, 12,5 % des branches exécutées ; profileur de la console : 8 % du fil vmsObjectThread, 2 % du fil de dessin.
//   sub_82609660 : r3 = r4 x r5    sub_82609788 : r3 = r4 x r3 (sur place)
// Matrices 4x4 de flottants, lignes de 16 octets, adresses quelconques (lvlx/lvrx, stvlx/stvrx). La seconde matrice
// est transposée (vmrghw/vmrglw), puis 16 produits scalaires vmsum4fp128 et l'assemblage des lignes.
//
// Ici : les mêmes opérations sur les mêmes vecteurs, dans le même ordre (simde_mm_dp_ps comme le code généré : même
// résultat au bit près), mais chaque ligne lue en une fois (lvlx a | lvrx a+16 = les 16 octets à a, ordre inversé) et
// écrite en une fois (stvlx a + stvrx a+16 = 16 octets à a), au lieu de 16 écritures d'un octet par ligne dans le
// code généré. Toutes les lectures précèdent les écritures, comme dans l'original (le produit sur place reste juste).
// Les registres que l'original laisse modifiés (r8-r11, v0-v13, v29-v31) reçoivent les mêmes valeurs.
// od_matrices_verif : exécute aussi l'original et compare mémoire écrite et contexte entier.

#include "opdarkness_pch.h"

#include <atomic>
#include <cstring>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

REXCVAR_DEFINE_BOOL(od_matrices, true, "OpDarkness",
                    "Produit de matrices 4x4 du moteur (sub_82609660, sub_82609788) en natif (même résultat)");
REXCVAR_DEFINE_BOOL(od_matrices_verif, false, "OpDarkness",
                    "Diagnostic : le produit natif est comparé à l'original à chaque appel (mémoire et registres)");

namespace {

using V = simde__m128i;

inline V Inverse(V v) {  // ordre des octets du jeu <-> ordre de l'hôte (VectorMaskL[0] : décalage nul)
  return simde_mm_shuffle_epi8(v, simde_mm_load_si128(reinterpret_cast<const V*>(&VectorMaskL[0])));
}

inline V Lire(uint8_t* base, uint32_t ea) {
  V v;
  std::memcpy(&v, REX_RAW_ADDR(ea), 16);
  return Inverse(v);
}

inline void Ecrire(uint8_t* base, uint32_t ea, V v) {
  v = Inverse(v);
  std::memcpy(REX_RAW_ADDR(ea), &v, 16);
}

// vmrghw d,a,b = unpackhi(b,a) ; vmrglw d,a,b = unpacklo(b,a) (comme le code généré).
inline V Mrghw(V a, V b) { return simde_mm_unpackhi_epi32(b, a); }
inline V Mrglw(V a, V b) { return simde_mm_unpacklo_epi32(b, a); }
inline V Msum4(V a, V b) {
  return simde_mm_castps_si128(simde_mm_dp_ps(simde_mm_castsi128_ps(a), simde_mm_castsi128_ps(b), 0xFF));
}
inline void Mettre(PPCVRegister& r, V v) { simde_mm_store_si128(reinterpret_cast<V*>(r.u8), v); }

// r3 = r4 x (matrice en b). Noms de variables = registres de l'original au même point.
void Produit(PPCContext& ctx, uint8_t* base, uint32_t b) {
  const uint32_t r3 = ctx.r3.u32, r4 = ctx.r4.u32;
  ctx.fpscr.enableFlushMode();
  // Lignes de la seconde matrice (b), puis de la première (r4).
  const V b0 = Lire(base, b), b1 = Lire(base, b + 16), b2 = Lire(base, b + 32), b3 = Lire(base, b + 48);
  const V a0 = Lire(base, r4), a1 = Lire(base, r4 + 16), a2 = Lire(base, r4 + 32), a3 = Lire(base, r4 + 48);
  // Transposition (vmrglw v6,v0,v12 ; vmrghw v8,v0,v12 ; vmrghw v7,v13,v11 ; vmrglw v5,v13,v11 ; ...).
  const V t6 = Mrglw(b0, b2), t8 = Mrghw(b0, b2), t7 = Mrghw(b1, b3), t5 = Mrglw(b1, b3);
  const V c9 = Mrghw(t6, t5), c10 = Mrghw(t8, t7), c8 = Mrglw(t8, t7), c7 = Mrglw(t6, t5);
  // vmsum4fp128 dans l'ordre de l'original.
  const V v3 = Msum4(a0, c9), v6 = Msum4(a0, c10), v1 = Msum4(a0, c8), v0a = Msum4(a0, c7);
  const V v5 = Msum4(a1, c10), v2 = Msum4(a1, c9), v31 = Msum4(a1, c8), v13a = Msum4(a1, c7);
  const V v4 = Msum4(a2, c10), v30 = Msum4(a2, c7), v29 = Msum4(a2, c8), v12a = Msum4(a2, c9);
  const V v7 = Msum4(a3, c7), v8 = Msum4(a3, c8), v9 = Msum4(a3, c9), v11a = Msum4(a3, c10);
  // Assemblage des lignes.
  const V v10a = Mrghw(v6, v3);
  const V v0b = Mrghw(v1, v0a);
  const V v6b = Mrghw(v5, v2);
  const V v13b = Mrghw(v31, v13a);
  const V v0 = Mrghw(v10a, v0b);
  const V v5b = Mrghw(v29, v30);
  const V v12b = Mrghw(v4, v12a);
  const V v13 = Mrghw(v6b, v13b);
  const V v10 = Mrghw(v8, v7);
  const V v11b = Mrghw(v11a, v9);
  const V v12 = Mrghw(v12b, v5b);
  const V v11 = Mrghw(v11b, v10);
  Ecrire(base, r3, v0);
  Ecrire(base, r3 + 16, v13);
  Ecrire(base, r3 + 32, v12);
  Ecrire(base, r3 + 48, v11);
  // Registres laissés par l'original.
  ctx.r8.s64 = 64;
  ctx.r9.s64 = 16;
  ctx.r10.s64 = 32;
  ctx.r11.s64 = 48;
  Mettre(ctx.v0, v0);
  Mettre(ctx.v1, v1);
  Mettre(ctx.v2, v2);
  Mettre(ctx.v3, v3);
  Mettre(ctx.v4, v4);
  Mettre(ctx.v5, v5b);
  Mettre(ctx.v6, v6b);
  Mettre(ctx.v7, v7);
  Mettre(ctx.v8, v8);
  Mettre(ctx.v9, v9);
  Mettre(ctx.v10, v10);
  Mettre(ctx.v11, v11);
  Mettre(ctx.v12, v12);
  Mettre(ctx.v13, v13);
  Mettre(ctx.v29, v29);
  Mettre(ctx.v30, v30);
  Mettre(ctx.v31, v31);
}

std::atomic<uint64_t> g_verifs{0}, g_ecarts{0};

template <void (*Original)(PPCContext&, uint8_t*)>
void Verifier(PPCContext& ctx, uint8_t* base, uint32_t b, uint32_t adresse) {
  const uint32_t sortie = ctx.r3.u32;
  uint8_t avant[64], attendu[64];
  std::memcpy(avant, REX_RAW_ADDR(sortie), 64);
  PPCContext reference = ctx;
  Original(reference, base);
  std::memcpy(attendu, REX_RAW_ADDR(sortie), 64);
  std::memcpy(REX_RAW_ADDR(sortie), avant, 64);  // les entrées éventuellement recouvertes reviennent aussi
  Produit(ctx, base, b == 0xFFFFFFFFu ? sortie : b);
  const uint64_t n = ++g_verifs;
  const bool memoire = std::memcmp(attendu, REX_RAW_ADDR(sortie), 64) == 0;
  const bool registres = std::memcmp(&reference, &ctx, sizeof(ctx)) == 0;
  if (!memoire || !registres) {
    const uint64_t e = ++g_ecarts;
    if (e <= 10 || (e & (e - 1)) == 0)
      REXLOG_WARN("[matrices] écart sur sub_{:08X} ({} / {}) : mémoire {}, registres {}", adresse, e, n,
                  memoire ? "identique" : "DIFFÉRENTE", registres ? "identiques" : "DIFFÉRENTS");
  } else if ((n & (n - 1)) == 0 && n >= 1024) {
    REXLOG_INFO("[matrices] {} vérifications, {} écarts", n, g_ecarts.load());
  }
}

}  // namespace

REX_EXTERN(__imp__sub_82609660);
REX_HOOK_RAW(sub_82609660) {
  if (!REXCVAR_GET(od_matrices)) __imp__sub_82609660(ctx, base);
  else if (REXCVAR_GET(od_matrices_verif)) Verifier<__imp__sub_82609660>(ctx, base, ctx.r5.u32, 0x82609660);
  else Produit(ctx, base, ctx.r5.u32);
}

REX_EXTERN(__imp__sub_82609788);
REX_HOOK_RAW(sub_82609788) {
  if (!REXCVAR_GET(od_matrices)) __imp__sub_82609788(ctx, base);
  else if (REXCVAR_GET(od_matrices_verif)) Verifier<__imp__sub_82609788>(ctx, base, 0xFFFFFFFFu, 0x82609788);
  else Produit(ctx, base, ctx.r3.u32);
}

// opdarkness (2026-10-20): see od_diag_barrieres.h.
#include "renut_engine/od_compat/od_diag_barrieres.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include <fmt/format.h>
#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_BOOL(od_renut_diag_barrieres, false, "OpDarkness",
                    "Rendu reNut : relevé des changements d'état des surfaces et textures (une barrière chacun), les "
                    "plus fréquents journalisés toutes les 10 s");

namespace od::renut {
namespace {
using Cle = std::tuple<std::string, uint32_t, uint32_t, uint32_t, int, int>;
std::map<Cle, uint64_t> g_comptes;
uint64_t g_total = 0;
auto g_depuis = std::chrono::steady_clock::now();
}  // namespace

bool DiagBarrieresOn() { return REXCVAR_GET(od_renut_diag_barrieres); }

void NoteBarriere(const char *kind, uint32_t guest_va, uint32_t width, uint32_t height, int from, int to) {
  ++g_comptes[Cle{kind, guest_va, width, height, from, to}];
  ++g_total;
  const auto maintenant = std::chrono::steady_clock::now();
  if (maintenant - g_depuis < std::chrono::seconds(10)) return;
  g_depuis = maintenant;
  std::vector<std::pair<uint64_t, Cle>> tri;
  for (const auto &[cle, n] : g_comptes) tri.emplace_back(n, cle);
  std::sort(tri.begin(), tri.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
  std::string texte;
  for (size_t i = 0; i < tri.size() && i < 10; ++i) {
    const auto &[k, va, w, h, de, vers] = tri[i].second;
    texte += fmt::format(" | {} {} {:08X} {}x{} {}->{}", tri[i].first, k, va, w, h, de, vers);
  }
  REXLOG_INFO("[renut] barrières (od_renut_diag_barrieres), 10 s : {} changements d'état{}", g_total, texte);
  g_comptes.clear();
  g_total = 0;
}

}  // namespace od::renut

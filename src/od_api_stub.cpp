// opdarkness - la couche Direct3D native de sa2-nx (sa2_api_*.cpp, guide de portage §10) n'est pas importée : elle
// est écrite pour le Direct3D de SA2 (XDK 2012). Le rendu natif (src/me) n'en appelle que le rapport périodique ; les
// autres points d'entrée répondent « ne s'applique pas », comme une clé sa2_api_* désactivée.

#include "me/sa2_api.h"

namespace sa2::api {

bool DrawVertices(PPCContext&, uint8_t*) { return false; }
bool BeginVertices(PPCContext&, uint8_t*) { return false; }
bool DrawVerticesUP(PPCContext&, uint8_t*) { return false; }
bool ShadowInlineAlloc(PPCContext&, uint8_t*) { return false; }
bool Clear(PPCContext&, uint8_t*) { return false; }
bool Resolve(PPCContext&, uint8_t*) { return false; }
std::string ResolveReport() { return {}; }
bool WindowScissor(PPCContext&, uint8_t*) { return false; }
void Report() {}

}  // namespace sa2::api

// opdarkness - retouches du HLSL de reNut (od_retouches.h).

#include "od_retouches.h"

#include <string_view>

namespace opdarkness::retouches {
namespace {

size_t Compter(const std::string& s, std::string_view motif) {
  size_t n = 0;
  for (size_t p = s.find(motif); p != std::string::npos; p = s.find(motif, p + motif.size())) ++n;
  return n;
}

void RemplacerTout(std::string& s, std::string_view de, std::string_view par) {
  for (size_t p = s.find(de); p != std::string::npos; p = s.find(de, p + par.size())) s.replace(p, de.size(), par);
}

// ---- tools/renut/od_flou_profondeur.py
constexpr std::string_view kFlouNom = "ps_08fb9abd43fe7c0c";
constexpr std::string_view kFlouDebut = "\tr6.y = tfetch2D(nSampler3_Texture2DDescriptorIndex";
constexpr std::string_view kFlouFin = "\toC0.w = 1.0;";

std::string Lecture(int s, std::string_view coord, std::string_view champs) {
  const std::string n = std::to_string(s);
  return "tfetch2D(nSampler" + n + "_Texture2DDescriptorIndex, nSampler" + n + "_SamplerDescriptorIndex, " +
         std::string(coord) + ", float2(0, 0))." + std::string(champs);
}

int Flou(std::string& hlsl, int mode) {
  if (mode == 0) return 0;
  const size_t debut = hlsl.find(kFlouDebut), fin = hlsl.find(kFlouFin);
  if (debut == std::string::npos || fin == std::string::npos || fin < debut) return 0;
  if (mode == 2) {
    const std::string ajout = "\t[branch] if (iPos.x >= 640.0) {\n"
                              "\t\tfloat zc = " + Lecture(3, "iTexCoord0.xy", "x") + ";\n"
                              "\t\tfloat3 bc = " + Lecture(2, "iTexCoord0.xy", "xyz") + ";\n"
                              "\t\tfloat3 sc = " + Lecture(1, "iTexCoord0.xy", "xyz") + ";\n"
                              "\t\toC0.xyz = (bc * c252.yyy + -sc) * abs(zc * 2.0 + -1.0) + sc;\n"
                              "\t}\n"
                              "\tif (abs(iPos.x - 640.0) < 1.0) oC0.xyz = float3(1.0, 0.85, 0.0);\n";
    hlsl.insert(fin, ajout);
  } else {
    const std::string corps = "\tr6.y = " + Lecture(3, "r0.xy", "x") + ";\n"
                              "\tr4.xyz = " + Lecture(2, "r0.xy", "xyz") + ";\n"
                              "\tr8.xyz = " + Lecture(1, "r0.xy", "xyz") + ";\n"
                              "\toC0.xyz = (r4.xyz * c252.yyy + -r8.xyz) * abs(r6.y * 2.0 + -1.0) + r8.xyz;\n";
    hlsl.replace(debut, fin - debut, corps);
  }
  return 1;
}

// ---- tools/renut/od_ombres.py
constexpr std::string_view kOmbresVs[] = {"vs_a75dae485a8c8b7b", "vs_e45c1440418e62b8"};
constexpr std::string_view kOmbresPs[] = {"ps_4c5cfd54864d90bf", "ps_ec78065cf82cfb45"};
constexpr std::string_view kDebutPs = "\tCubeMapData cubeMapData = (CubeMapData)0;\n";
constexpr std::string_view kAjoutPs =
    "\t// opdarkness (tools/renut/od_ombres.py) : hors de la zone de la carte d'ombres, rien à assombrir (mode 2 de\n"
    "\t// od_renut_ombres_decoupe) : arrêt avant toute lecture de texture.\n"
    "\tif (RENUT_SHARED_UINT(1352) == 2u)\n"
    "\t{\n"
    "\t\tconst float4 t = iTexCoord2;\n"
    "\t\tconst float m = 0.03 * t.z;\n"
    "\t\tif (t.z <= 0.0 || t.x < -m || t.y < -m || t.x > t.z + m || t.y > t.z + m)\n"
    "\t\t\tdiscard;\n"
    "\t}\n";
constexpr std::string_view kSortie = "\tout float oPointSize : POINTSIZE)";
constexpr std::string_view kSortieDecoupe =
    "\tout float oPointSize : POINTSIZE,\n"
    "\tout float4 oOdDecoupe0 : SV_ClipDistance0,\n"
    "\tout float4 oOdDecoupe1 : SV_ClipDistance1)";
constexpr std::string_view kFin = "\toPointSize = oPts.z != 0.0 ? -1.0 : oPts.x;\n\treturn;\n}";
constexpr std::string_view kAjoutVs =
    "\t// opdarkness (tools/renut/od_ombres.py) : découpe à la zone de la carte d'ombres (od_renut_ombres_decoupe).\n"
    "\tif (RENUT_SHARED_UINT(1352) == 1u)\n"
    "\t{\n"
    "\t\tconst float m = 0.03 * oTexCoord2.z;  // marge : le filtrage bilinéaire au bord de la carte\n"
    "\t\toOdDecoupe0 = float4(oTexCoord2.z, oTexCoord2.x + m, oTexCoord2.z + m - oTexCoord2.x, oTexCoord2.y + m);\n"
    "\t\toOdDecoupe1 = float4(oTexCoord2.z + m - oTexCoord2.y, 1.0, 1.0, 1.0);\n"
    "\t}\n"
    "\telse\n"
    "\t{\n"
    "\t\toOdDecoupe0 = 1.0;\n"
    "\t\toOdDecoupe1 = 1.0;\n"
    "\t}\n";

int Ombres(const std::string& nom, std::string& hlsl) {
  for (std::string_view vs : kOmbresVs) {
    if (nom != vs || hlsl.find("oOdDecoupe0") != std::string::npos) continue;
    const size_t main = hlsl.find("\nvoid main(");
    const std::string corps = main == std::string::npos ? std::string() : hlsl.substr(main);
    if (Compter(hlsl, kSortie) != 1 || Compter(hlsl, kFin) != 1 || Compter(corps, "return;") != 1) return 0;
    RemplacerTout(hlsl, kSortie, kSortieDecoupe);
    std::string fin_ajout(kAjoutVs);
    fin_ajout += kFin;
    RemplacerTout(hlsl, kFin, fin_ajout);
    return 1;
  }
  for (std::string_view ps : kOmbresPs) {
    if (nom != ps || hlsl.find("od_ombres.py") != std::string::npos) continue;
    if (Compter(hlsl, kDebutPs) != 1 || hlsl.find("iTexCoord2") == std::string::npos) return 0;
    std::string ajout(kDebutPs);
    ajout += kAjoutPs;
    RemplacerTout(hlsl, kDebutPs, ajout);
    return 1;
  }
  return 0;
}

}  // namespace

int Appliquer(const std::string& nom, std::string& hlsl, const Reglages& reglages) {
  int n = 0;
  // L'ordre de gpu.cmake : flou, puis ombres.
  if (nom == kFlouNom) n += Flou(hlsl, reglages.flou);
  if (reglages.ombres) n += Ombres(nom, hlsl);
  return n;
}

}  // namespace opdarkness::retouches

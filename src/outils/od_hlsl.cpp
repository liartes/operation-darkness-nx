// opdarkness - outil PC : HLSL de XenosRecomp pour des conteneurs de shaders (.odsh du cache de traduction, ou
// conteneurs bruts), dans un dossier ; sert à mettre au point le prélude glslang de la Switch (docs/SHADERS_EN_JEU.md).
// Usage : od_hlsl [--prelude <fichier>] <dossier de sortie> <fichier.odsh | conteneur>...

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "XenosRecomp/shader_recompiler.h"
#include "od_prep_glslang.h"

namespace {
constexpr char kPrelude[] =
#include "od_shader_common.inc"
    ;
}

int main(int argc, char** argv) {
  std::string prelude(kPrelude, sizeof(kPrelude) - 1);
  bool glslang = false;
  if (argc > 1 && std::string_view(argv[1]) == "--glslang") {
    glslang = true;
    ++argv;
    --argc;
  }
  if (argc > 2 && std::string_view(argv[1]) == "--prelude") {
    std::ifstream p(argv[2], std::ios::binary);
    prelude.assign((std::istreambuf_iterator<char>(p)), std::istreambuf_iterator<char>());
    argv += 2;
    argc -= 2;
  }
  if (argc < 3) {
    std::fprintf(stderr, "usage : od_hlsl <sortie> <fichier.odsh|conteneur>...\n");
    return 2;
  }
  std::filesystem::create_directories(argv[1]);
  int echecs = 0;
  for (int i = 2; i < argc; ++i) {
    std::ifstream in(argv[i], std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<uint8_t> conteneur = d;
    if (d.size() >= 16 && std::memcmp(d.data(), "ODSHAD01", 8) == 0) {
      uint32_t octets;
      std::memcpy(&octets, &d[8], 4);
      conteneur.assign(d.begin() + 16, d.begin() + 16 + octets);
    }
    try {
      ShaderRecompiler r;
      r.recompile(conteneur.data(), prelude);
      const auto nom = std::filesystem::path(argv[i]).stem().string() + ".hlsl";
      std::string texte(r.out.data(), r.out.size());
      if (glslang) texte = opdarkness::glslang_prep::Preparer(texte);
      std::ofstream(std::filesystem::path(argv[1]) / nom).write(texte.data(), std::streamsize(texte.size()));
    } catch (const std::exception& x) {
      std::fprintf(stderr, "%s : %s\n", argv[i], x.what());
      ++echecs;
    }
  }
  std::printf("%d fichiers, %d échecs\n", argc - 2, echecs);
  return echecs ? 1 : 0;
}

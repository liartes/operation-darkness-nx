// Outil PC : od_passes_glsl <entrée> <sortie> : les passes de src/od_passes_glsl.cpp sur chaque vs_/ps_<empreinte>.glsl
// (pour comparer au script tools/renut/deko3d_glsl_opt.py, octet pour octet).
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "od_passes_glsl.h"

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage : od_passes_glsl <entrée> <sortie>\n";
    return 2;
  }
  std::filesystem::create_directories(argv[2]);
  int n = 0;
  for (const auto& e : std::filesystem::directory_iterator(argv[1])) {
    const std::string nom = e.path().filename().string();
    if (nom.size() < 3 || (nom.compare(0, 3, "vs_") != 0 && nom.compare(0, 3, "ps_") != 0)) continue;
    std::ifstream f(e.path(), std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    std::string t = ss.str();
    opdarkness::passes_glsl::Appliquer(t, nom[0] == 'v');
    std::ofstream(std::filesystem::path(argv[2]) / nom, std::ios::binary) << t;
    ++n;
  }
  std::cout << n << " shaders\n";
  return 0;
}

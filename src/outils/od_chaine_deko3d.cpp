// opdarkness - outil PC : la chaîne de traduction des shaders pour deko3d de la console (src/od_chaine_deko3d.cpp) sur
// un dossier de conteneurs, pour la comparer à la chaîne des scripts et à la sortie de la console (docs/DEKO3D.md).
//   od_chaine_deko3d <dossier de conteneurs> <sortie> [--sans-uam] [--fils N] [--allocateur] [--retouches]
//     --retouches : celles du paquet du PC (od_retouches.h), à comparer aux scripts sur le hlsl/ d'une sortie sans
//     (tools/renut/comparer_retouches.sh)
//   od_chaine_deko3d --fichiers <image.bin> <racine du jeu> <sortie> [fils] : la recherche des conteneurs de la console
//     (src/od_conteneurs.cpp) ; <sortie>/{vs,ps}_<empreinte>.bin et fichiers.csv, à comparer à shaders_fichiers.py.

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <chrono>
#include <fstream>
#include <iterator>

#include "od_chaine_deko3d.h"
#include "od_conteneurs.h"

namespace {

int Fichiers(const char* image, const char* racine, const char* sortie, int fils) {
  namespace oc = opdarkness::conteneurs;
  std::ifstream f(image, std::ios::binary);
  const std::vector<uint8_t> img((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  oc::Liste liste;
  const oc::Bilan b = oc::ChercherDansLeJeu(img.data(), img.size(), racine, liste, {.fils = fils});
  std::filesystem::create_directories(sortie);
  std::ofstream csv(std::filesystem::path(sortie) / "fichiers.csv");
  csv << "shader,fichier\n";
  size_t vs = 0;
  for (const auto& [empreinte, c] : liste) {
    const std::string nom = oc::Nom(empreinte, c);
    csv << nom << ',' << c.fichier << '\n';
    std::ofstream(std::filesystem::path(sortie) / (nom + ".bin"), std::ios::binary)
        .write(reinterpret_cast<const char*>(c.donnees.data()), std::streamsize(c.donnees.size()));
    vs += c.vertex;
  }
  std::printf("%d fils : %zu .igb lus (%zu compressés, %zu échecs, %.1f Mo) ; %zu shaders (%zu vs) ; lecture %.0f ms, LZX %.0f ms, "
              "recherche %.0f ms, total %.0f ms\n",
              fils, b.fichiers, b.compresses, b.echecs, b.octets / 1e6, liste.size(), vs, b.ms_lecture, b.ms_lzx,
              b.ms_recherche, b.ms_total);
  return b.echecs ? 2 : 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 5 && std::strcmp(argv[1], "--fichiers") == 0)
    return Fichiers(argv[2], argv[3], argv[4], argc > 5 ? std::atoi(argv[5]) : 1);
  if (argc < 3) {
    std::fprintf(stderr, "usage : %s <dossier de conteneurs> <sortie> [--sans-uam] [--fils N] [--allocateur]\n", argv[0]);
    return 1;
  }
  opdarkness::chaine_deko3d::Reglages reglages;
  for (int i = 3; i < argc; ++i) {
    if (std::strcmp(argv[i], "--sans-uam") == 0) reglages.uam = false;
    if (std::strcmp(argv[i], "--fils") == 0 && i + 1 < argc) reglages.fils = std::atoi(argv[++i]);
    if (std::strcmp(argv[i], "--allocateur") == 0) reglages.allocateur_par_fil = true;
    if (std::strcmp(argv[i], "--retouches") == 0) {
      static const opdarkness::retouches::Reglages retouches;
      opdarkness::chaine_deko3d::FixerRetouches(&retouches);
    }
  }
  opdarkness::chaine_deko3d::Essai(argv[1], argv[2], reglages);
  return 0;
}

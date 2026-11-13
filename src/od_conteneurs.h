// opdarkness - les conteneurs de shaders des fichiers du jeu, trouvés par la console elle-même (installation sans PC,
// docs/DEKO3D.md « Prochaine session : installation sur la console sans PC », point 1) : la recherche de
// tools/renut/shaders_fichiers.py (vérification de reNut reprise par sa2-nx) sur l'image de default.xex chargée en
// mémoire et sur les .igb du jeu, ceux compressés en XMemCompress (signature 0x0FF512EE) décompressés d'abord par le LZX
// du SDK (lzx_decompress, celui du chargeur de XEX).
// Seuls default.xex et les .igb en contiennent (relevé du script sur tout assets/ : 121 + 45 fichiers, 166 shaders).
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace opdarkness::conteneurs {

struct Conteneur {
  bool vertex = false;
  std::vector<uint8_t> donnees;  // en-tête, partie virtuelle et partie physique
  std::string fichier;           // le premier où il a été vu (« default.xex » ou chemin relatif à la racine du jeu)
};

// Par empreinte XXH3 du conteneur (noms des fichiers du paquet : vs_/ps_%016x).
using Liste = std::map<uint64_t, Conteneur>;

// Ceux d'un tampon (sans doublons dans `liste`) ; rend le nombre de nouveaux.
size_t Chercher(const uint8_t* donnees, size_t taille, const std::string& fichier, Liste& liste);

// Un fichier XMemCompress en entier ; faux si ce n'est pas ce format ou si le LZX échoue.
bool DecompresserXmem(const std::vector<uint8_t>& entree, std::vector<uint8_t>& sortie);

struct Reglages {
  int fils = 1;                     // fils de lecture et de décompression (fichiers pris à tour de rôle)
  bool placer = false;              // Switch : chaque fil sur son cœur (0, 1, 2 tour à tour)
  bool allocateur_par_fil = false;  // Switch : rpmalloc dans ces fils (od_alloc_fils.h)
};

struct Bilan {
  size_t fichiers = 0;    // .igb lus
  size_t compresses = 0;  // dont décompressés
  size_t echecs = 0;      // illisibles ou décompression en échec
  uint64_t octets = 0;    // lus sur la carte
  double ms_lecture = 0, ms_lzx = 0, ms_recherche = 0;  // tous fils confondus
  double ms_total = 0;
};

// Image de default.xex (`image`, `taille_image` ; nulle : sautée) puis tous les .igb sous `racine`, dans l'ordre des
// chemins (le fichier d'origine retenu est le premier dans cet ordre, quels que soient les fils). `progression(fait,
// total)` après chaque fichier (total = nombre de .igb), depuis les fils de travail, un appel à la fois.
Bilan ChercherDansLeJeu(const uint8_t* image, size_t taille_image, const std::filesystem::path& racine, Liste& liste,
                        const Reglages& reglages = {}, const std::function<void(size_t, size_t)>& progression = {});

// Nom du paquet : vs_<empreinte> ou ps_<empreinte>.
std::string Nom(uint64_t empreinte, const Conteneur& c);

}  // namespace opdarkness::conteneurs

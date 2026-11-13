// opdarkness - captures d'écran aux instants donnés, pour les essais sur la Switch (l'album de la console ne capture
// pas un homebrew) et sur PC sans fenêtre visible. --od_ecrans="60,90,120" (secondes depuis le démarrage du jeu) :
// l'image de sortie du présentateur (CaptureGuestOutput, comme sa2-nx sa2_capture.cpp) est écrite en demi-résolution
// au format PPM (P6, sans dépendance) dans <od_ecrans_dossier>/ecran_<s>s.ppm, à côté de l'exécutable par défaut
// (Switch : sdmc:/switch/operation-darkness-nx/captures). Fil créé par pthread (std::thread lève une exception sur
// la console).

#include <pthread.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/ui/presenter.h>

#include "od_ecrans.h"

REXCVAR_DEFINE_STRING(od_ecrans, "", "OpDarkness",
                      "Captures d'écran aux secondes données depuis le démarrage (ex. 60,90,120), en PPM")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_STRING(od_ecrans_dossier, "captures", "OpDarkness",
                      "Dossier des captures de od_ecrans (relatif : à côté de l'exécutable)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace {

struct Travail {
  std::function<rex::ui::Presenter*()> presentateur;
  std::function<bool(rex::ui::RawImage&)> source;  // à la place du présentateur (rendu reNut)
  std::vector<int> secondes;
  std::filesystem::path dossier;
};

bool Ecrire(const rex::ui::RawImage& image, const std::filesystem::path& chemin) {
  const uint32_t l = image.width / 2, h = image.height / 2;
  std::vector<uint8_t> rgb(size_t(l) * h * 3);
  for (uint32_t y = 0; y < h; ++y) {
    const uint8_t* ligne = image.data.data() + size_t(y * 2) * image.stride;
    for (uint32_t x = 0; x < l; ++x) {
      const uint8_t* p = ligne + size_t(x * 2) * 4;
      uint8_t* q = &rgb[(size_t(y) * l + x) * 3];
      q[0] = p[0];
      q[1] = p[1];
      q[2] = p[2];
    }
  }
  std::FILE* f = std::fopen(chemin.string().c_str(), "wb");
  if (!f) return false;
  std::fprintf(f, "P6\n%u %u\n255\n", l, h);
  const bool ok = std::fwrite(rgb.data(), 1, rgb.size(), f) == rgb.size();
  std::fclose(f);
  return ok;
}

void* Boucle(void* arg) {
  std::unique_ptr<Travail> t(static_cast<Travail*>(arg));
  const auto debut = std::chrono::steady_clock::now();
  std::error_code ec;
  std::filesystem::create_directories(t->dossier, ec);
  for (int s : t->secondes) {
    std::this_thread::sleep_until(debut + std::chrono::seconds(s));
    rex::ui::Presenter* presentateur = t->presentateur ? t->presentateur() : nullptr;
    rex::ui::RawImage image;
    const bool ok = t->source ? t->source(image) : presentateur && presentateur->CaptureGuestOutput(image);
    if (!ok || image.width < 2 || image.height < 2) {
      REXLOG_WARN("[ecrans] {} s : pas d'image (pas de présentateur ou de sortie du jeu)", s);
      continue;
    }
    const auto chemin = t->dossier / ("ecran_" + std::to_string(s) + "s.ppm");
    if (Ecrire(image, chemin)) {
      REXLOG_INFO("[ecrans] {} s : {}", s, chemin.string());
    } else {
      REXLOG_WARN("[ecrans] {} s : écriture de {} échouée", s, chemin.string());
    }
  }
  return nullptr;
}

}  // namespace

namespace opdarkness::ecrans {

namespace {
void Lancer(std::function<rex::ui::Presenter*()> presentateur, std::function<bool(rex::ui::RawImage&)> source) {
  const std::string liste = REXCVAR_GET(od_ecrans);
  if (liste.empty()) return;
  auto t = std::make_unique<Travail>();
  t->presentateur = std::move(presentateur);
  t->source = std::move(source);
  size_t i = 0;
  while (i < liste.size()) {
    size_t j = liste.find(',', i);
    if (j == std::string::npos) j = liste.size();
    if (j > i) t->secondes.push_back(std::stoi(liste.substr(i, j - i)));
    i = j + 1;
  }
  const std::filesystem::path dossier{std::string(REXCVAR_GET(od_ecrans_dossier))};
  t->dossier = dossier.is_relative() ? rex::filesystem::GetExecutableFolder() / dossier : dossier;
  pthread_attr_t attributs;
  pthread_attr_init(&attributs);
  pthread_attr_setstacksize(&attributs, 1u << 20);
  pthread_t fil;
  Travail* brut = t.release();
  if (pthread_create(&fil, &attributs, &Boucle, brut) == 0) {
    pthread_detach(fil);
  } else {
    delete brut;
  }
  pthread_attr_destroy(&attributs);
}
}  // namespace

void Demarrer(std::function<rex::ui::Presenter*()> presentateur) { Lancer(std::move(presentateur), nullptr); }

void DemarrerAvecSource(std::function<bool(rex::ui::RawImage&)> source) { Lancer(nullptr, std::move(source)); }

}  // namespace opdarkness::ecrans

// opdarkness - ReXGlue Recompiled Project
//
// Application : points d'entrée du SDK (rex::ReXApp) propres au portage d'Operation Darkness.

#pragma once

#include <rex/logging.h>
#include <rex/rex_app.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include <rex/cvar.h>
#include <rex/filesystem.h>

#include "od_clavier.h"
#include "od_ecrans.h"
#include "od_pgo.h"
#include "opdarkness_manette.h"

namespace opdarkness {
void InstallerTracePlantages();  // od_plantage.cpp
}

#if defined(OD_BUILTIN_CONFIG)
#include "od_builtin_config.h"  // généré depuis switch/defauts.toml et switch/opdarkness.toml (CMakeLists.txt)
#endif

// Rendu natif (OD_NATIF : cmake/rendu_natif.cmake, SDK de sa2-nx).
#if defined(OD_NATIF)
#include "cpu/sa2_enhancements.h"
#include "me/me_native_system.h"
#endif
// Rendu reNut (OD_RENUT : cmake/rendu_renut.cmake ; od_renderer = "renut", docs/DEKO3D.md).
#if defined(OD_RENUT)
#include "renut_engine/gpu/banjo_renut_select.h"
#include "renut_engine/gpu/overlay/app_integration.h"
#include "renut_engine/gpu/banjo_renut_capture.h"
#include "od_chaine_deko3d.h"
#include "od_installation.h"
#include <rex/runtime.h>
#include <rex/system/xmemory.h>
#endif

class OpdarknessApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<OpdarknessApp>(new OpdarknessApp(ctx, "opdarkness",
        PPCImageConfig));
  }

  // Version Switch : réglages fixes compilés dans le .nro (switch/defauts.toml), appliqués avant opdarkness.toml ; et
  // le opdarkness.toml du joueur, écrit à côté du .nro s'il n'y en a pas (switch/opdarkness.toml).
  std::string_view BuiltInConfig() const override {
#if defined(OD_BUILTIN_CONFIG)
    return kOdBuiltInConfig;
#else
    return {};
#endif
  }
  std::string_view DefaultConfigFile() const override {
#if defined(OD_BUILTIN_CONFIG)
    return kOdPlayerConfig;
#else
    return {};
#endif
  }

  void OnPostInitLogging() override {
    // Le SDK ne charge aucun plugin graphique par défaut : l'émulation Xenos, sauf choix explicite (avec le rendu
    // natif, elle n'est pas chargée du tout).
    if (!rex::cvar::HasNonDefaultValue("gpu_plugin")) rex::cvar::SetFlagByName("gpu_plugin", "xenos");
#if defined(OD_NATIF)
    // Améliorations du rendu de sa2-nx (docs/OPTIMISATION.md) : chacune a sa clé, avec deux interrupteurs généraux
    // (sa2_cpu_enhancements, sa2_noncpu_enhancements) ; sans cet appel, comme dans l'application de sa2-nx, toutes
    // restaient éteintes (mise à l'échelle du rendu comprise). Avant tout fil du jeu.
    sa2::enh::Resolve();
#endif
  }

  // Sans --game_data_root : un dossier game_root/ (ou son paquet game_root.nxpack) à côté de l'exécutable ou plus
  // haut (Switch : sdmc:/switch/opdarkness/game_root), comme sa2-nx. Un chemin explicite l'emporte toujours.
  void OnConfigurePaths(rex::PathConfig& paths) override {
    if (!paths.game_data_root.empty()) return;
    std::error_code ec;
    const auto dossier = rex::filesystem::GetExecutableFolder();
    for (auto candidat = dossier; !candidat.empty(); candidat = candidat.parent_path()) {
      auto paquet = candidat / "game_root";
      paquet += ".nxpack";
      if (std::filesystem::is_regular_file(candidat / "game_root" / "default.xex", ec) ||
          std::filesystem::is_regular_file(paquet, ec)) {
        paths.game_data_root = candidat / "game_root";
        return;
      }
      if (candidat == candidat.parent_path()) break;
    }
  }

  // Manette de script pour les essais automatiques (--od_manette, opdarkness_manette.cpp). Avec OD_NATIF et
  // --sa2_renderer_native=true, le système graphique est le rendu natif de src/me (lecture de l'anneau PM4, Vulkan) :
  // ReXApp ne charge alors pas le plugin d'émulation xenos (il ne le charge que si config.graphics est vide).
  void OnPreSetup(rex::RuntimeConfig& config) override {
#if defined(OD_RENUT)
    // Essai de la traduction des shaders pour deko3d (--od_essai_deko3d) : avant le rendu, qui prend uam pour son fil.
    opdarkness::chaine_deko3d::EssaiAuDemarrage();
#endif
    opdarkness::manette::WrapInput(config);
    opdarkness::InstallerTracePlantages();
#if defined(OD_NATIF)
    // Portable : profil GPU officiel 460,8 MHz (sa2_gpu_boost, repris de sa2-nx ; rien sur télé ni sur PC).
    sa2::enh::ApplyGpuBoost();
#if defined(OD_RENUT)
    if (banjo::renut_gpu::Active()) {
      // Ni système graphique du SDK ni plugin : le rendu est celui de l'application (sa fenêtre, son fil).
      renut::gpu::ConfigureRuntime(config);
      REXLOG_INFO("[opdarkness] graphismes : rendu reNut au niveau de Direct3D (ni anneau PM4 ni EDRAM)");
      return;
    }
#endif
    if (me::native::Enabled()) {
      config.graphics = me::native::CreateGraphicsSystem();
      REXLOG_INFO("[opdarkness] graphismes : rendu natif Vulkan (anneau PM4)");
    }
#endif
  }

#if defined(OD_RENUT)
  // Rendu reNut : il dessine les menus du SDK avec son propre ImGui sur plume, gère sa chaîne d'images et démarre une
  // fois la fenêtre créée (les chemins sont alors définitifs).
  std::unique_ptr<rex::ui::ImmediateDrawer> OnCreateImmediateDrawer() override {
    if (banjo::renut_gpu::Active()) return renut::gpu::CreateOverlayDrawer();
    return rex::ReXApp::OnCreateImmediateDrawer();
  }
  void OnWindowPixelSizeChanged(uint32_t pixel_width, uint32_t pixel_height) override {
    rex::ReXApp::OnWindowPixelSizeChanged(pixel_width, pixel_height);
    if (banjo::renut_gpu::Active()) renut::gpu::OnWindowResized();
  }
  std::optional<rex::PathConfig> OnFinalizePaths(const rex::PathConfig& defaults,
                                                 std::function<void(rex::PathConfig)> resume) override {
    if (banjo::renut_gpu::Active()) renut::gpu::StartRenderer(app_context(), [this] { return imgui_drawer(); });
    return rex::ReXApp::OnFinalizePaths(defaults, std::move(resume));
  }
#endif

#if defined(OD_RENUT)
  // Essai de la recherche des conteneurs de shaders (--od_essai_conteneurs) : l'image de default.xex est en mémoire,
  // telle que chargée, le jeu n'a pas encore démarré.
  void OnPostLoadXexImage() override {
    auto* memoire = runtime() ? runtime()->memory() : nullptr;
    if (memoire)
      opdarkness::installation::AuChargement(
          memoire->TranslateVirtual<const uint8_t*>(PPCImageConfig.image_base), PPCImageConfig.image_size,
          runtime()->game_data_root(), banjo::renut_gpu::Active() ? imgui_drawer() : nullptr,
          [this] { renut::gpu::PresentOverlayNow(imgui_drawer()); });
    if (memoire)
      opdarkness::chaine_deko3d::EssaiConteneurs(memoire->TranslateVirtual<const uint8_t*>(PPCImageConfig.image_base),
                                                 PPCImageConfig.image_size, runtime()->game_data_root());
  }
#endif

  // Captures d'écran aux instants donnés (--od_ecrans, od_ecrans.cpp).
  void OnPostSetup() override {
#if defined(OD_RENUT)
    if (banjo::renut_gpu::Active())
      opdarkness::ecrans::DemarrerAvecSource([](rex::ui::RawImage& image) { return renut::gpu::CaptureFrame(image); });
    else
#endif
    opdarkness::ecrans::Demarrer([this]() -> rex::ui::Presenter* {
      const auto* rt = runtime();
      const auto* graphismes = rt ? rt->graphics_system() : nullptr;
      return graphismes ? graphismes->presenter() : nullptr;
    });
    // Switch : clavier du système pour XamShowKeyboardUI (nom du joueur) ; PC : la fenêtre du SDK.
    opdarkness::clavier::Installer();
    // .nro instrumenté pour le PGO (OD_PGO=generate) : compteurs écrits dans pgo/ toutes les 2 minutes (rien sinon).
    opdarkness::pgo::Start();
  }

  // .nro instrumenté : compteurs écrits quand le jeu perd la main (menu HOME), d'où il peut être fermé.
  void OnWindowFocusChanged(bool focused) override {
    if (!focused) opdarkness::pgo::DumpNow();
  }

  void OnShutdown() override {
#if defined(OD_RENUT)
    if (banjo::renut_gpu::Active()) renut::gpu::ShutdownRenderer();
#endif
    opdarkness::pgo::Stop();
  }

  // Le jeu a terminé son titre (XamLoaderLaunchTitle(NULL) ou XamLoaderTerminateTitle) : son thread principal vient
  // de finir. Le SDK enchaînerait sur la destruction du runtime, qui plantait sur torchlight-nx (threads du jeu
  // encore vivants). On sort comme à la fermeture de la fenêtre (ReXApp::OnClosing) : journal vidé, puis
  // std::_Exit. Posté au thread de l'interface.
  void OnGuestThreadExit(rex::system::XThread* thread) override {
    (void)thread;
    REXLOG_INFO("[opdarkness] fin du titre demandée par le jeu : sortie directe (sans destruction du runtime)");
    app_context().CallInUIThread([] { SortirDirectement(); });
  }

  [[noreturn]] static void SortirDirectement() {
    opdarkness::pgo::DumpNow();
    rex::FlushLogging();
    std::fflush(nullptr);
    std::_Exit(0);
  }
};

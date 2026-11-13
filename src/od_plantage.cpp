// opdarkness - trace des plantages sur PC.
//
// Le journal du SDK est écrit en différé : un plantage en perd la fin (session du 2026-10-08, mission 1 : le journal
// s'arrêtait net, sans FATAL). Avec --od_plantage_dossier=<dossier>, les signaux fataux que le SDK ne rattrape pas
// (SIGABRT, SIGILL, SIGFPE, SIGBUS ; SIGSEGV reste au SDK, qui s'en sert pour les pages surveillées) écrivent tout de
// suite, sans allocation, la pile native du thread fautif (backtrace) dans <dossier>/plantage-<pid>.txt, puis le
// signal reprend son action par défaut. Les adresses se symbolisent avec addr2line / llvm-symbolizer sur l'exécutable.

#if !defined(__linux__)
// Switch : le SDK a son propre gestionnaire d'exceptions (logs/rex/rex_crash.log).
namespace opdarkness {
void InstallerTracePlantages() {}
}  // namespace opdarkness
#else

#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <string>

#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_STRING(od_plantage_dossier, "", "OpDarkness",
                      "Dossier où écrire la pile native d'un plantage (signal fatal), vide = rien")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace {

char g_chemin[512] = {};

void Ecrire(int fd, const char* texte) { (void)!write(fd, texte, std::strlen(texte)); }

void Gestionnaire(int signal, siginfo_t* info, void*) {
  const int fd = open(g_chemin, O_WRONLY | O_CREAT | O_APPEND, 0644);
  if (fd >= 0) {
    char ligne[160];
    std::snprintf(ligne, sizeof(ligne), "signal %d (%s), adresse %p, thread %d\n", signal, strsignal(signal),
                  info ? info->si_addr : nullptr, int(gettid()));
    Ecrire(fd, ligne);
    void* pile[64];
    const int n = backtrace(pile, 64);
    backtrace_symbols_fd(pile, n, fd);
    Ecrire(fd, "\n");
    close(fd);
  }
  ::signal(signal, SIG_DFL);
  raise(signal);
}

}  // namespace

namespace opdarkness {

// Appelée par OpdarknessApp::OnPreSetup.
void InstallerTracePlantages() {
  const std::string dossier = REXCVAR_GET(od_plantage_dossier);
  if (dossier.empty()) return;
  std::snprintf(g_chemin, sizeof(g_chemin), "%s/plantage-%d.txt", dossier.c_str(), int(getpid()));
  void* pile[4];
  backtrace(pile, 4);  // charge libgcc maintenant : backtrace alloue au premier appel
  struct sigaction sa = {};
  sa.sa_sigaction = Gestionnaire;
  sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
  sigemptyset(&sa.sa_mask);
  for (int s : {SIGABRT, SIGILL, SIGFPE, SIGBUS}) sigaction(s, &sa, nullptr);
  REXLOG_INFO("[plantage] pile native des signaux fataux dans {}", g_chemin);
}

}  // namespace opdarkness

#endif  // __linux__

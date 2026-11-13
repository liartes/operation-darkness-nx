// opdarkness - allocateur à cache par fil pour les fils de traduction des shaders (od_alloc_fils.h).

#include "od_alloc_fils.h"

// OD_ALLOC_FILS_FORCE : le même code sur PC, dans l'outil od_chaine_deko3d (pour l'essayer hors de la console).
#if defined(__SWITCH__) || defined(OD_ALLOC_FILS_FORCE)

#include <malloc.h>
#include <pthread.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>

extern "C" {
#include <rpmalloc.h>

#if defined(__SWITCH__)
// sysconf pour rpmalloc (newlib de la Switch n'en a pas ; cmake/rendu_renut.cmake) : il n'y lit que la page.
long od_rpmalloc_sysconf(int) { return 4096; }
#endif
}

namespace opdarkness::alloc_fils {
namespace {

// La zone : des blocs de 64 Ko (la taille des « spans » de rpmalloc, qui veut ses spans alignés sur elle), suivis par
// une table d'occupation. Ses bornes ne changent plus une fois posées (lues sans verrou par delete).
constexpr size_t kBloc = 64 * 1024;
constexpr size_t kTailleZone = 256u << 20;
constexpr size_t kBlocs = kTailleZone / kBloc;

uintptr_t g_debut = 0;
size_t g_taille = 0;
bool g_occupe[kBlocs];
pthread_mutex_t g_verrou = PTHREAD_MUTEX_INITIALIZER;
bool g_pret = false;

thread_local bool t_actif = false;

bool DansZone(const void* p) { return uintptr_t(p) - g_debut < g_taille; }

// memory_map de rpmalloc : `taille` octets alignés sur 64 Ko, premier trou assez grand (les appels sont rares : les
// spans sont gardés en cache par rpmalloc).
void* Prendre(size_t taille, size_t* decalage) {
  const size_t n = (taille + kBloc - 1) / kBloc;
  pthread_mutex_lock(&g_verrou);
  void* p = nullptr;
  for (size_t i = 0, libres = 0; i < kBlocs; ++i) {
    libres = g_occupe[i] ? 0 : libres + 1;
    if (libres == n) {
      const size_t premier = i + 1 - n;
      for (size_t k = premier; k <= i; ++k) g_occupe[k] = true;
      p = reinterpret_cast<void*>(g_debut + premier * kBloc);
      break;
    }
  }
  pthread_mutex_unlock(&g_verrou);
  *decalage = 0;
  return p;
}

// memory_unmap : `liberer` (taille entière) rend les blocs ; 0 = abandon partiel des pages, sans objet ici.
void Rendre(void* adresse, size_t, size_t, size_t liberer) {
  if (!liberer) return;
  const size_t premier = (uintptr_t(adresse) - g_debut) / kBloc;
  const size_t n = (liberer + kBloc - 1) / kBloc;
  pthread_mutex_lock(&g_verrou);
  for (size_t k = premier; k < premier + n && k < kBlocs; ++k) g_occupe[k] = false;
  pthread_mutex_unlock(&g_verrou);
}

void Initialiser() {
  void* zone = memalign(kBloc, kTailleZone);
  if (!zone) return;
  std::memset(g_occupe, 0, sizeof(g_occupe));
  g_debut = uintptr_t(zone);
  g_taille = kTailleZone;
  rpmalloc_config_t config;
  std::memset(&config, 0, sizeof(config));
  config.memory_map = Prendre;
  config.memory_unmap = Rendre;
  config.page_size = 4096;
  config.span_size = kBloc;
  g_pret = rpmalloc_initialize_config(&config) == 0;
}

void* Allouer(size_t n) {
  if (t_actif) {
    if (void* p = rpmalloc(n ? n : 1)) return p;
  }
  return std::malloc(n ? n : 1);
}

void* AllouerAligne(size_t n, size_t alignement) {
  if (t_actif) {
    if (void* p = rpaligned_alloc(alignement, n ? n : 1)) return p;
  }
  return memalign(alignement, n ? n : 1);
}

void Liberer(void* p) {
  if (!p) return;
  if (DansZone(p)) {
    rpfree(p);
  } else {
    std::free(p);
  }
}

}  // namespace

bool Activer() {
  static std::once_flag une_fois;
  std::call_once(une_fois, Initialiser);
  if (!g_pret) return false;
  rpmalloc_thread_initialize();
  t_actif = true;
  return true;
}

void Desactiver() {
  if (!t_actif) return;
  t_actif = false;
  // Les caches du fil rendus au reste de rpmalloc ; ce qu'il a alloué et qui vit encore reste valable.
  rpmalloc_thread_finalize(1);
}

}  // namespace opdarkness::alloc_fils

// Remplacement des opérateurs globaux (tout le programme : un test de variable locale au fil et d'adresse en plus).
using opdarkness::alloc_fils::Allouer;
using opdarkness::alloc_fils::AllouerAligne;
using opdarkness::alloc_fils::Liberer;

void* operator new(std::size_t n) {
  if (void* p = Allouer(n)) return p;
  throw std::bad_alloc();
}
void* operator new[](std::size_t n) {
  if (void* p = Allouer(n)) return p;
  throw std::bad_alloc();
}
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return Allouer(n); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return Allouer(n); }
void* operator new(std::size_t n, std::align_val_t a) {
  if (void* p = AllouerAligne(n, std::size_t(a))) return p;
  throw std::bad_alloc();
}
void* operator new[](std::size_t n, std::align_val_t a) {
  if (void* p = AllouerAligne(n, std::size_t(a))) return p;
  throw std::bad_alloc();
}
void* operator new(std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
  return AllouerAligne(n, std::size_t(a));
}
void* operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
  return AllouerAligne(n, std::size_t(a));
}
void operator delete(void* p) noexcept { Liberer(p); }
void operator delete[](void* p) noexcept { Liberer(p); }
void operator delete(void* p, std::size_t) noexcept { Liberer(p); }
void operator delete[](void* p, std::size_t) noexcept { Liberer(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { Liberer(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { Liberer(p); }
void operator delete(void* p, std::align_val_t) noexcept { Liberer(p); }
void operator delete[](void* p, std::align_val_t) noexcept { Liberer(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { Liberer(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { Liberer(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { Liberer(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { Liberer(p); }

#else

namespace opdarkness::alloc_fils {
bool Activer() { return false; }
void Desactiver() {}
}  // namespace opdarkness::alloc_fils

#endif

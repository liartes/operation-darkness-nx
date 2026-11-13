// opdarkness - <sys/mman.h> pour compiler rpmalloc sur la Switch (src/od_alloc_fils.cpp) : newlib n'en a pas. Ces
// fonctions ne servent jamais : rpmalloc reçoit ses propres fonctions de mise en correspondance (memory_map,
// memory_unmap), qui découpent une zone prise à newlib.
#pragma once

#include <stddef.h>

#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_PRIVATE 2
#define MAP_ANONYMOUS 0x20
#define MAP_FAILED ((void*)-1)
#define MADV_DONTNEED 4

static inline void* mmap(void* adresse, size_t taille, int protection, int options, int fd, long decalage) {
  (void)adresse; (void)taille; (void)protection; (void)options; (void)fd; (void)decalage;
  return MAP_FAILED;
}
static inline int munmap(void* adresse, size_t taille) {
  (void)adresse; (void)taille;
  return -1;
}
static inline int madvise(void* adresse, size_t taille, int conseil) {
  (void)adresse; (void)taille; (void)conseil;
  return -1;
}

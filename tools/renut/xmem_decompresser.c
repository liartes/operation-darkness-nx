// opdarkness - décompression d'un fichier XMemCompress (format natif, signature 0x0FF512EE : les .igb du jeu) avec lzxd
// de libmspack (le SDK l'a ; sa2-nx s'en sert pour l'installateur). Utilisé par tools/renut/shaders_fichiers.py.
//   xmem_decompresser <entrée> <sortie>   (code 3 : pas ce format ; 2 : LZX en échec)
// Format : en-tête de 0x30 octets (grand-boutiste : signature, version, fenêtre en 0x10, taille décompressée sur 64 bits
// en 0x18), puis des blocs (u32 taille), faits de trames LZX précédées de leur taille compressée (u16 ; 0xFF, u16
// taille décompressée, u16 taille compressée pour une trame plus courte que 32 Ko). Les trames mises bout à bout forment
// un seul flux LZX.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lzx.h"
#include "mspack.h"
extern struct mspack_system *mspack_default_system;

static unsigned be32(const unsigned char *p) { return (unsigned)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static unsigned be16(const unsigned char *p) { return p[0] << 8 | p[1]; }

int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "usage : xmem_decompresser <entrée> <sortie>\n");
    return 1;
  }
  FILE *f = fopen(argv[1], "rb");
  if (!f) return 1;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  unsigned char *d = malloc(n);
  if (fread(d, 1, n, f) != (size_t)n) return 1;
  fclose(f);
  if (n < 0x30 || be32(d) != 0x0FF512EEu) return 3;
  const unsigned fenetre = be32(d + 0x10);
  const unsigned long long taille = (unsigned long long)be32(d + 0x18) << 32 | be32(d + 0x1C);
  int bits = 0;
  while ((1u << bits) < fenetre) bits++;
  unsigned char *flux = malloc(n);
  long lf = 0, p = 0x30;
  while (p + 4 <= n) {
    const unsigned bloc = be32(d + p);
    p += 4;
    long fin = p + bloc;
    if (fin > n) fin = n;
    while (p < fin) {
      unsigned c;
      if (d[p] == 0xFF) {
        c = be16(d + p + 3);
        p += 5;
      } else {
        c = be16(d + p);
        p += 2;
      }
      if (c == 0 || p + c > fin) {
        p = fin;
        break;
      }
      memcpy(flux + lf, d + p, c);
      lf += c;
      p += c;
    }
  }
  char tmp[4096];
  snprintf(tmp, sizeof tmp, "%s.lzx", argv[2]);
  FILE *t = fopen(tmp, "wb");
  fwrite(flux, 1, lf, t);
  fclose(t);
  struct mspack_system *s = mspack_default_system;
  struct mspack_file *in = s->open(s, tmp, MSPACK_SYS_OPEN_READ), *out = s->open(s, argv[2], MSPACK_SYS_OPEN_WRITE);
  struct lzxd_stream *z = lzxd_init(s, in, out, bits, 0, 0x8000, (off_t)taille, 0);
  const int r = z ? lzxd_decompress(z, (off_t)taille) : -1;
  if (z) lzxd_free(z);
  s->close(in);
  s->close(out);
  remove(tmp);
  if (r) {
    fprintf(stderr, "%s : LZX en échec (%d)\n", argv[1], r);
    return 2;
  }
  return 0;
}

#define XXH_INLINE_ALL
#include <xxhash.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
static uint32_t be(const std::vector<unsigned char>& c, size_t p) { return p + 4 <= c.size() ? uint32_t(c[p]) << 24 | c[p+1] << 16 | c[p+2] << 8 | c[p+3] : 0; }
int main(int n, char** v) {
  for (int i = 1; i < n; ++i) {
    std::ifstream in(v[i], std::ios::binary); std::vector<unsigned char> d((std::istreambuf_iterator<char>(in)), {});
    uint32_t o; std::memcpy(&o, &d[8], 4); std::vector<unsigned char> c(d.begin() + 16, d.begin() + 16 + o);
    uint32_t virt = be(c, 4), h = be(c, 24), off = be(c, h), bytes = be(c, h + 4);
    if (!bytes || size_t(virt) + off + bytes > c.size()) continue;
    std::vector<uint32_t> w(bytes / 4); for (size_t k = 0; k < w.size(); ++k) w[k] = be(c, virt + off + k * 4);
    std::printf("%016llX %s\n", (unsigned long long)XXH3_64bits(w.data(), w.size() * 4), v[i]);
  }
}

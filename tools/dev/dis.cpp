// Désassembleur SPIR-V d'un .odsh (cache de traduction) ou d'un .spv brut.
// clang++-20 -std=c++17 -O1 dis.cpp -I ~/workspace/sa2-nx/sdk/thirdparty/spirv-tools/include \
//   ~/workspace/sa2-nx/sdk/out/linux-amd64/libSPIRV-Tools.a -o dis
#include <spirv-tools/libspirv.hpp>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>
int main(int c, char** v) {
  if (c < 2) return 2;
  std::ifstream in(v[1], std::ios::binary);
  std::vector<char> d((std::istreambuf_iterator<char>(in)), {});
  size_t o = 0, n = d.size() / 4;
  if (d.size() > 16 && !std::memcmp(d.data(), "ODSHAD01", 8)) {
    uint32_t a, b;
    std::memcpy(&a, &d[8], 4);
    std::memcpy(&b, &d[12], 4);
    o = 16 + a;
    n = b;
  }
  std::vector<uint32_t> w(n);
  std::memcpy(w.data(), d.data() + o, n * 4);
  spvtools::SpirvTools t(SPV_ENV_VULKAN_1_2);
  std::string s;
  t.Disassemble(w, &s, SPV_BINARY_TO_TEXT_OPTION_FRIENDLY_NAMES | SPV_BINARY_TO_TEXT_OPTION_NO_HEADER);
  std::cout << s;
}

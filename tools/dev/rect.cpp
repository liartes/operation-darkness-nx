// Applique la conversion des listes de rectangles du rendu (src/me/native/me_rectangle_spirv.h) au SPIR-V d'un
// vertex shader .odsh et fait valider le résultat par SPIRV-Tools ; écrit le désassemblage si un 2e argument est donné.
// clang++-20 -std=c++20 -O1 rect.cpp -I src/me/native -I ~/workspace/sa2-nx/sdk/thirdparty/spirv-tools/include \
//   ~/workspace/sa2-nx/sdk/out/linux-amd64/libSPIRV-Tools.a -o rect
#include <spirv-tools/libspirv.hpp>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>
#include "me_rectangle_spirv.h"
int main(int c, char** v) {
  if (c < 2) return 2;
  std::ifstream in(v[1], std::ios::binary);
  std::vector<char> d((std::istreambuf_iterator<char>(in)), {});
  uint32_t a, b;
  std::memcpy(&a, &d[8], 4);
  std::memcpy(&b, &d[12], 4);
  std::vector<uint32_t> w(b);
  std::memcpy(w.data(), d.data() + 16 + a, b * 4);
  auto r = me::native::ExpandRectangleShader(w);
  if (r.words.empty()) { std::cout << "refusé\n"; return 1; }
  spvtools::SpirvTools t(SPV_ENV_VULKAN_1_2);
  t.SetMessageConsumer([](spv_message_level_t, const char*, const spv_position_t& p, const char* m) {
    std::cout << "validation : " << m << " (mot " << p.index << ")\n";
  });
  std::cout << (t.Validate(r.words) ? "valide\n" : "INVALIDE\n");
  if (c > 2) {
    std::string s;
    t.Disassemble(r.words, &s, SPV_BINARY_TO_TEXT_OPTION_FRIENDLY_NAMES | SPV_BINARY_TO_TEXT_OPTION_NO_HEADER);
    std::ofstream(v[2]) << s;
  }
}

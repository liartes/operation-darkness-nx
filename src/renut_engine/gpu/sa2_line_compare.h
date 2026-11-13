// sa2-nx (2026-10-20): whether two 64-byte lines differ, for the device chunks of the delta chain (backend/queued.cpp)
// and the pushed shared constants' lines (render/draw.cpp). GCC left the eight 64-bit words of the plain version scalar
// (about 30 instructions a line: 9 % of the game thread in the second console run); four 128-bit loads a side, XORed,
// ORed and one horizontal max are about 10 (sa2_renut_neon_compare).
#pragma once

#include <cstdint>
#include <cstring>

#if defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace sa2::renut {

inline bool Line64DiffersPlain(const uint8_t *a, const uint8_t *b) {
  uint64_t diff = 0;
  for (uint32_t k = 0; k < 64; k += 8) {
    uint64_t x, y;
    std::memcpy(&x, a + k, 8);
    std::memcpy(&y, b + k, 8);
    diff |= x ^ y;
  }
  return diff != 0;
}

inline bool Line64Differs(const uint8_t *a, const uint8_t *b, bool neon) {
#if defined(__aarch64__)
  if (neon) {
    const uint8x16x4_t x = vld1q_u8_x4(a);
    const uint8x16x4_t y = vld1q_u8_x4(b);
    const uint8x16_t d = vorrq_u8(vorrq_u8(veorq_u8(x.val[0], y.val[0]), veorq_u8(x.val[1], y.val[1])),
                                  vorrq_u8(veorq_u8(x.val[2], y.val[2]), veorq_u8(x.val[3], y.val[3])));
    return vmaxvq_u32(vreinterpretq_u32_u8(d)) != 0;
  }
#else
  (void)neon;
#endif
  return Line64DiffersPlain(a, b);
}

// opdarkness (2026-11-01): Line64Differs, and when the line differs, a copies it to shadow and to out from the
// registers the compare loaded (CaptureDelta called memmove twice for each changed chunk, reading it again).
inline bool Line64CopyIfDiffers(const uint8_t *a, uint8_t *shadow, uint8_t *out, bool neon) {
#if defined(__aarch64__)
  if (neon) {
    const uint8x16x4_t x = vld1q_u8_x4(a);
    const uint8x16x4_t y = vld1q_u8_x4(shadow);
    const uint8x16_t d = vorrq_u8(vorrq_u8(veorq_u8(x.val[0], y.val[0]), veorq_u8(x.val[1], y.val[1])),
                                  vorrq_u8(veorq_u8(x.val[2], y.val[2]), veorq_u8(x.val[3], y.val[3])));
    if (vmaxvq_u32(vreinterpretq_u32_u8(d)) == 0)
      return false;
    vst1q_u8_x4(shadow, x);
    vst1q_u8_x4(out, x);
    return true;
  }
#endif
  if (!Line64DiffersPlain(a, shadow))
    return false;
  std::memcpy(shadow, a, 64);
  std::memcpy(out, a, 64);
  return true;
}

// opdarkness (2026-11-13, from renut_nx_ultimate_base, bangaio-nx's banjo_renut_delta_masks): which of 'count' (at most
// 32) consecutive 64-byte lines differ, as bit k for line k: one NEON reduction per four lines and no branch per line.
inline uint32_t Lines64DifferMask(const uint8_t *a, const uint8_t *b, uint32_t count) {
  uint32_t mask = 0;
  uint32_t i = 0;
#if defined(__aarch64__)
  const uint32x4_t bits = {1u, 2u, 4u, 8u};
  auto line = [](const uint8_t *x, const uint8_t *y) {
    const uint8x16x4_t p = vld1q_u8_x4(x);
    const uint8x16x4_t q = vld1q_u8_x4(y);
    return vreinterpretq_u32_u8(vorrq_u8(vorrq_u8(veorq_u8(p.val[0], q.val[0]), veorq_u8(p.val[1], q.val[1])),
                                         vorrq_u8(veorq_u8(p.val[2], q.val[2]), veorq_u8(p.val[3], q.val[3]))));
  };
  const auto group = [&](uint32_t g) {  // lines g..g+3's bits, at bit g
    const uint32x4_t l0 = line(a + g * 64, b + g * 64), l1 = line(a + g * 64 + 64, b + g * 64 + 64);
    const uint32x4_t l2 = line(a + g * 64 + 128, b + g * 64 + 128), l3 = line(a + g * 64 + 192, b + g * 64 + 192);
    const uint32x4_t m = vpmaxq_u32(vpmaxq_u32(l0, l1), vpmaxq_u32(l2, l3));  // lane k: line g + k's maximum
    return vaddvq_u32(vandq_u32(vtstq_u32(m, m), bits)) << g;
  };
  for (; i + 4 <= count; i += 4)
    mask |= group(i);
  if (i < count && count >= 4) {  // 1-3 lines left: the last four again (the overlap's bits are the same)
    mask |= group(count - 4);
    i = count;
  }
  for (; i < count; ++i)
    mask |= uint32_t(Line64Differs(a + i * 64, b + i * 64, true)) << i;
#else
  for (; i < count; ++i)
    mask |= uint32_t(Line64DiffersPlain(a + i * 64, b + i * 64)) << i;
#endif
  return mask;
}

}  // namespace sa2::renut

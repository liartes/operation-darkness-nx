// The addresses both renderers hook (banjo-nx's tools/renut_gpu_import.py generated this list for N&B). sa2-nx
// (2026-10-20): SA2's. sa2-nx's own hook there (src/me, src/cpu) calls renut_native_sub_X when Renut's renderer is
// chosen (sa2_renut_yield.h).
#pragma once

#include <rex/ppc.h>

extern "C" void renut_native_sub_82349A30(PPCContext& ctx, uint8_t* base);  // CBlocker::Check (opdarkness)
extern "C" void renut_native_sub_8234A5C8(PPCContext& ctx, uint8_t* base);  // D3DDevice_Swap (opdarkness)
extern "C" void renut_native_sub_826BD808(PPCContext& ctx, uint8_t* base);  // BeginVertices (opdarkness)
extern "C" void renut_native_sub_826BDD00(PPCContext& ctx, uint8_t* base);  // BeginIndexedVertices (opdarkness)
extern "C" void renut_native_sub_826BE2D0(PPCContext& ctx, uint8_t* base);  // DrawVertices (opdarkness)
extern "C" void renut_native_sub_826BE6B8(PPCContext& ctx, uint8_t* base);  // DrawIndexedVertices (opdarkness)

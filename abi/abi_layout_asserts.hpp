#pragma once
// abi_layout_asserts.hpp - compile-time guard for the binary layout of structs shared with plugins.
//
// Include it AFTER dreamkurumiHT_plugin.h (anywhere in the core build and in the CI test).
// A failed assert means a plugin built against the previous header would read/write the wrong bytes:
// that is an ABI break and needs a major version bump (or a new struct version), never a silent edit.
//
// Numbers were measured from the real header (dreamkurumiHT_plugin.h, v3.8.3) and match the comments in it.
// Windows x64 only (the framework's only target).

#include <cstddef>
#include <type_traits>

#if !defined(_WIN64) && !defined(__x86_64__)
#error "dreamkurumiHT ABI is defined for x86_64 only"
#endif

namespace dreamkurumi::abi_v1 {

#define DK_ABI_SIZE(T, n)       static_assert(sizeof(T) == (n), #T " size changed: ABI break")
#define DK_ABI_OFF(T, m, off)   static_assert(offsetof(T, m) == (off), #T "::" #m " moved: ABI break")

// ---- DreamYmmRegister --------------------------------------------------------------------------------
DK_ABI_SIZE(DreamYmmRegister, 32);
static_assert(alignof(DreamYmmRegister) == 1, "pack(1) expected");

// ---- DreamCpuContext (MidHook stub frame: the stub builds exactly this layout on the stack) ----------
DK_ABI_SIZE(DreamCpuContext, 640);                       // 0x280
static_assert(alignof(DreamCpuContext) == 1, "pack(1) expected");
static_assert(std::is_standard_layout_v<DreamCpuContext>, "must stay standard layout");
static_assert(std::is_trivially_copyable_v<DreamCpuContext>, "stub and VEH copy it with memcpy");

DK_ABI_OFF(DreamCpuContext, r15,    0x200);
DK_ABI_OFF(DreamCpuContext, r14,    0x208);
DK_ABI_OFF(DreamCpuContext, r13,    0x210);
DK_ABI_OFF(DreamCpuContext, r12,    0x218);
DK_ABI_OFF(DreamCpuContext, r11,    0x220);
DK_ABI_OFF(DreamCpuContext, r10,    0x228);
DK_ABI_OFF(DreamCpuContext, r9,     0x230);
DK_ABI_OFF(DreamCpuContext, r8,     0x238);
DK_ABI_OFF(DreamCpuContext, rbp,    0x240);
DK_ABI_OFF(DreamCpuContext, rdi,    0x248);
DK_ABI_OFF(DreamCpuContext, rsi,    0x250);
DK_ABI_OFF(DreamCpuContext, rdx,    0x258);
DK_ABI_OFF(DreamCpuContext, rcx,    0x260);
DK_ABI_OFF(DreamCpuContext, rbx,    0x268);
DK_ABI_OFF(DreamCpuContext, rax,    0x270);
DK_ABI_OFF(DreamCpuContext, rflags, 0x278);
// the context ends exactly where the first pushed register (rflags) ends: original RSP == (char*)ctx + sizeof
static_assert(offsetof(DreamCpuContext, rflags) + sizeof(uint64_t) == sizeof(DreamCpuContext), "context must end at rflags");
// vector slots are 32 bytes apart for BOTH xmm[] and ymm[] views (xmm[i] is the low half of ymm[i]).
// NOTE: CONTEXT.Xmm0..15 are 16 bytes apart, so Windows CONTEXT <-> DreamCpuContext needs a per-register copy.
static_assert(sizeof(DreamCpuContext::xmm[0]) == 32, "xmm slot must be 32 bytes (low 16 + pad 16)");
static_assert(sizeof(DreamCpuContext::ymm[0]) == 32, "ymm slot must be 32 bytes");

// ---- DreamWorldCursor ---------------------------------------------------------------------------------
DK_ABI_SIZE(DreamWorldCursor, 52);
static_assert(alignof(DreamWorldCursor) == 1, "pack(1) expected");
static_assert(std::is_standard_layout_v<DreamWorldCursor>, "must stay standard layout");
static_assert(std::is_trivially_copyable_v<DreamWorldCursor>, "copied across the DLL boundary by value");
DK_ABI_OFF(DreamWorldCursor, posX,     0x00);
DK_ABI_OFF(DreamWorldCursor, posY,     0x04);
DK_ABI_OFF(DreamWorldCursor, posZ,     0x08);
DK_ABI_OFF(DreamWorldCursor, dirX,     0x0C);
DK_ABI_OFF(DreamWorldCursor, dirY,     0x10);
DK_ABI_OFF(DreamWorldCursor, dirZ,     0x14);
DK_ABI_OFF(DreamWorldCursor, originX,  0x18);
DK_ABI_OFF(DreamWorldCursor, originY,  0x1C);
DK_ABI_OFF(DreamWorldCursor, originZ,  0x20);
DK_ABI_OFF(DreamWorldCursor, distance, 0x24);
DK_ABI_OFF(DreamWorldCursor, flags,    0x28);
DK_ABI_OFF(DreamWorldCursor, sequence, 0x2C);
DK_ABI_OFF(DreamWorldCursor, isValid,  0x30);
DK_ABI_OFF(DreamWorldCursor, _pad,     0x31);

// ---- fundamental widths the C ABI relies on -----------------------------------------------------------
static_assert(sizeof(void*) == 8 && sizeof(size_t) == 8, "x64 only");
static_assert(sizeof(bool) == 1, "bool is passed as 1 byte in the exported signatures");

#undef DK_ABI_SIZE
#undef DK_ABI_OFF

// When you ADD a plugin-visible struct, add it here in the same commit. When you must change one, create
// DreamXxxV2 and keep V1 (and its asserts) alive for the whole major version.

} // namespace dreamkurumi::abi_v1

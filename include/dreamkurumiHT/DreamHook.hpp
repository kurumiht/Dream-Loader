#pragma once
#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <algorithm>
#include <string>
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#include "DreamCore.hpp"
#include "DreamDisasm.hpp"
#include "DreamPool.hpp"
#include "DreamThread.hpp"
#include "DreamCrashGuard.hpp"
#include "DreamRelocator.hpp"
#include "DreamPatcher.hpp"
#include "DreamFiberGuard.hpp"

namespace dreamkurumi {

// ============================================================================
// CPU AVX / SIMD Capability Detection
// ============================================================================
inline bool CpuSupportsAvx() {
    static int s_supported = -1;
    if (s_supported != -1) return s_supported == 1;

#if defined(_MSC_VER)
    int cpuInfo[4] = {0};
    __cpuid(cpuInfo, 1);
    bool osxsave = (cpuInfo[2] & (1 << 27)) != 0;
    bool avx = (cpuInfo[2] & (1 << 28)) != 0;
    if (!osxsave || !avx) {
        s_supported = 0;
        return false;
    }
    uint64_t xcr0 = _xgetbv(0);
    s_supported = ((xcr0 & 6) == 6) ? 1 : 0;
    return s_supported == 1;
#else
    unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
    if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        s_supported = 0;
        return false;
    }
    bool osxsave = (ecx & (1 << 27)) != 0;
    bool avx = (ecx & (1 << 28)) != 0;
    if (!osxsave || !avx) {
        s_supported = 0;
        return false;
    }
    uint32_t xcr_eax = 0, xcr_edx = 0;
    __asm__ __volatile__("xgetbv" : "=a"(xcr_eax), "=d"(xcr_edx) : "c"(0));
    uint64_t xcr0 = (static_cast<uint64_t>(xcr_edx) << 32) | xcr_eax;
    s_supported = ((xcr0 & 6) == 6) ? 1 : 0;
    return s_supported == 1;
#endif
}

// ============================================================================
// Extended SIMD / AVX2 CPU Register Context for Mid-Function Hooks
// SafetyHook-style full 256-bit YMM register preservation + 100% backward compat
// ============================================================================
#pragma pack(push, 1)
struct DreamYmmRegister {
    union {
        float    f32[8];
        double   f64[4];
        uint32_t u32[8];
        uint64_t u64[4];
        uint8_t  u8[32];
    };
};

struct DreamCpuContext {
    union {
        DreamYmmRegister ymm[16];   // 0x000 - 0x1FF (512 bytes, full 256-bit AVX/AVX2)
        struct {
            union {
                float    f32[4];
                double   f64[2];
                uint32_t u32[4];
                uint64_t u64[2];
                uint8_t  u8[16];
            };
            uint8_t _pad_high[16];  // upper 128 bits of 256-bit YMM slot
        } xmm[16];                  // 0x000 - 0x1FF (overlays with ymm[16] for 100% backward compatibility)
    };

    uint64_t r15;       // 0x200
    uint64_t r14;       // 0x208
    uint64_t r13;       // 0x210
    uint64_t r12;       // 0x218
    uint64_t r11;       // 0x220
    uint64_t r10;       // 0x228
    uint64_t r9;        // 0x230
    uint64_t r8;        // 0x238
    uint64_t rbp;       // 0x240
    uint64_t rdi;       // 0x248
    uint64_t rsi;       // 0x250
    uint64_t rdx;       // 0x258
    uint64_t rcx;       // 0x260
    uint64_t rbx;       // 0x268
    uint64_t rax;       // 0x270
    uint64_t rflags;    // 0x278

    // Helper methods for easy access in hooks
    float GetFloatArg(int index) const {
        if (index >= 0 && index < 16) return xmm[index].f32[0];
        return 0.0f;
    }
    void SetFloatArg(int index, float val) {
        if (index >= 0 && index < 16) xmm[index].f32[0] = val;
    }
};
#pragma pack(pop)

static_assert(sizeof(DreamCpuContext) == 640, "DreamCpuContext must be exactly 640 bytes (0x280)");
static_assert(offsetof(DreamCpuContext, r15) == 512, "r15 must be at offset 512 (0x200)");
static_assert(offsetof(DreamCpuContext, rax) == 624, "rax must be at offset 624 (0x270)");
static_assert(offsetof(DreamCpuContext, rflags) == 632, "rflags must be at offset 632 (0x278)");

using MidHookCallback = void (*)(DreamCpuContext& ctx);
using MidHookContextCallback = void (*)(DreamCpuContext& ctx, void* userData);

// ============================================================================
// DreamInlineHook (Function Entry Point & Detour)
// ============================================================================
class DreamInlineHook {
public:
    DreamInlineHook() = default;
    ~DreamInlineHook() { Disable(); }

    bool Install(void* target, void* detour) {
        if (!target || !detour) return false;
        if (m_isInstalled) return true;

        m_target = reinterpret_cast<uint8_t*>(target);
        m_detour = reinterpret_cast<uint8_t*>(detour);

        // 1. Determine instruction boundary (need at least 5 bytes for JMP rel32)
        m_stolenLen = DreamDisasm::GetBoundary(m_target, 5);
        if (m_stolenLen < 5 || m_stolenLen > 32) return false;

        // 2. Backup original bytes
        m_originalBytes.resize(m_stolenLen);
        std::memcpy(m_originalBytes.data(), m_target, m_stolenLen);

        // 3. Allocate executable trampoline memory near target (+/- 2GB) using Near-Memory Arena
        m_trampoline = reinterpret_cast<uint8_t*>(DreamTrampolinePool::Instance().Allocate(m_target, 128));
        if (!m_trampoline) return false;

        // 4. Build Trampoline:
        // Check for pre-existing overlay hooks (Steam, Discord, OBS, ReShade) first
        m_ipMapCount = 0;
        size_t trampOffset = 0;
        size_t curOffset = 0;
        bool chainedOverlay = false;

        if (TryChainExistingOverlayHook(m_target, m_trampoline, curOffset, trampOffset, chainedOverlay) && chainedOverlay) {
            DREAM_LOG_INFO("DreamInlineHook: Detected and chained pre-existing overlay hook at 0x%p", m_target);
        } else {
            while (curOffset < m_stolenLen) {
                auto decoded = DreamDisasm::Decode(m_target + curOffset);
                if (decoded.length == 0) return false;

                // Relocation Guard: Reject unsupported LOOP/JRCXZ instructions (0xE0..0xE3)
                if (DreamDisasm::IsLoopOrJrcxz(decoded.opcode)) {
                    DREAM_LOG_WARN("DreamInlineHook: Cannot relocate LOOP/JRCXZ instruction (0x%02X) at 0x%p",
                                   decoded.opcode, m_target + curOffset);
                    return false;
                }

                // Relocation Guard: Reject terminal instructions (RET/JMP/INT3) in the middle of stolen bytes
                if ((curOffset + decoded.length) < m_stolenLen &&
                    DreamDisasm::IsTerminalInstruction(decoded.opcode, decoded.opcode2, decoded.modrmReg)) {
                    DREAM_LOG_WARN("DreamInlineHook: Mid-region terminal instruction (0x%02X) detected at 0x%p",
                                   decoded.opcode, m_target + curOffset);
                    return false;
                }

                if (m_ipMapCount < 16) {
                    m_ipMap[m_ipMapCount++] = { static_cast<uint16_t>(curOffset), static_cast<uint16_t>(trampOffset) };
                }

                // Handle short unconditional jump EB rel8 -> widen to E9 rel32
                if (decoded.opcode == 0xEB && decoded.length == 2) {
                    int8_t oldDisp = *reinterpret_cast<const int8_t*>(m_target + curOffset + 1);
                    uintptr_t destAddr = reinterpret_cast<uintptr_t>(m_target + curOffset + 2) + oldDisp;
                    // Branch bounds: reject jump inside stolen region
                    if (destAddr >= reinterpret_cast<uintptr_t>(m_target) && destAddr < reinterpret_cast<uintptr_t>(m_target + m_stolenLen)) return false;
                    uintptr_t newInstrEnd = reinterpret_cast<uintptr_t>(m_trampoline + trampOffset + 5);
                    int64_t newDisp = static_cast<int64_t>(destAddr - newInstrEnd);
                    if (newDisp < INT32_MIN || newDisp > INT32_MAX) return false;
                    m_trampoline[trampOffset] = 0xE9; // Widen to near jump
                    *reinterpret_cast<int32_t*>(m_trampoline + trampOffset + 1) = static_cast<int32_t>(newDisp);
                    curOffset += 2;
                    trampOffset += 5;
                    continue;
                }

                // Handle short conditional jump 70..7F rel8 -> widen to 0F 80..8F rel32
                if ((decoded.opcode >= 0x70 && decoded.opcode <= 0x7F) && decoded.length == 2) {
                    int8_t oldDisp = *reinterpret_cast<const int8_t*>(m_target + curOffset + 1);
                    uintptr_t destAddr = reinterpret_cast<uintptr_t>(m_target + curOffset + 2) + oldDisp;
                    if (destAddr >= reinterpret_cast<uintptr_t>(m_target) && destAddr < reinterpret_cast<uintptr_t>(m_target + m_stolenLen)) return false;
                    uintptr_t newInstrEnd = reinterpret_cast<uintptr_t>(m_trampoline + trampOffset + 6);
                    int64_t newDisp = static_cast<int64_t>(destAddr - newInstrEnd);
                    if (newDisp < INT32_MIN || newDisp > INT32_MAX) return false;
                    m_trampoline[trampOffset] = 0x0F;
                    m_trampoline[trampOffset + 1] = static_cast<uint8_t>(0x80 + (decoded.opcode & 0x0F));
                    *reinterpret_cast<int32_t*>(m_trampoline + trampOffset + 2) = static_cast<int32_t>(newDisp);
                    curOffset += 2;
                    trampOffset += 6;
                    continue;
                }

                std::memcpy(m_trampoline + trampOffset, m_target + curOffset, decoded.length);

                // Handle 0xE8 (CALL rel32) and 0xE9 (JMP rel32) displacement fixups
                if ((decoded.opcode == 0xE8 || decoded.opcode == 0xE9) && decoded.length == 5) {
                    int32_t oldDisp = *reinterpret_cast<const int32_t*>(m_target + curOffset + 1);
                    uintptr_t destAddr = reinterpret_cast<uintptr_t>(m_target + curOffset + 5) + oldDisp;
                    if (destAddr >= reinterpret_cast<uintptr_t>(m_target) && destAddr < reinterpret_cast<uintptr_t>(m_target + m_stolenLen)) return false;
                    uintptr_t newInstrEnd = reinterpret_cast<uintptr_t>(m_trampoline + trampOffset + 5);
                    int64_t newDisp = static_cast<int64_t>(destAddr - newInstrEnd);
                    if (newDisp < INT32_MIN || newDisp > INT32_MAX) {
                        return false; // Target unreachable within 32-bit relative displacement
                    }
                    *reinterpret_cast<int32_t*>(m_trampoline + trampOffset + 1) = static_cast<int32_t>(newDisp);
                }
                // Handle 0F 80..8F (Near conditional jump Jcc rel32, 6 bytes)
                else if (decoded.opcode == 0x0F && (decoded.opcode2 >= 0x80 && decoded.opcode2 <= 0x8F) && decoded.length == 6) {
                    int32_t oldDisp = *reinterpret_cast<const int32_t*>(m_target + curOffset + 2);
                    uintptr_t destAddr = reinterpret_cast<uintptr_t>(m_target + curOffset + 6) + oldDisp;
                    if (destAddr >= reinterpret_cast<uintptr_t>(m_target) && destAddr < reinterpret_cast<uintptr_t>(m_target + m_stolenLen)) return false;
                    uintptr_t newInstrEnd = reinterpret_cast<uintptr_t>(m_trampoline + trampOffset + 6);
                    int64_t newDisp = static_cast<int64_t>(destAddr - newInstrEnd);
                    if (newDisp < INT32_MIN || newDisp > INT32_MAX) return false;
                    *reinterpret_cast<int32_t*>(m_trampoline + trampOffset + 2) = static_cast<int32_t>(newDisp);
                }
                // Handle RIP-relative ModR/M addressing (e.g. mov rax, [rip + disp32])
                else if (decoded.isRipRelative && decoded.dispSize == 4) {
                    uintptr_t oldTargetAddr = reinterpret_cast<uintptr_t>(m_target + curOffset + decoded.length) + decoded.dispVal;
                    uintptr_t newInstrEnd = reinterpret_cast<uintptr_t>(m_trampoline + trampOffset + decoded.length);
                    int64_t newDisp = static_cast<int64_t>(oldTargetAddr - newInstrEnd);

                    if (newDisp < INT32_MIN || newDisp > INT32_MAX) {
                        return false; // Displacement out of 32-bit range
                    }

                    *reinterpret_cast<int32_t*>(m_trampoline + trampOffset + decoded.dispOffset) = static_cast<int32_t>(newDisp);
                }

                curOffset += decoded.length;
                trampOffset += decoded.length;
            }

            // Append 14-byte 64-bit absolute jump back to target + stolenLen
            trampOffset += EmitAbsoluteJump64(m_trampoline + trampOffset, reinterpret_cast<uintptr_t>(m_target + m_stolenLen));
        }

        MemoryHelper::FlushCache(m_trampoline, trampOffset);

        // 5. Check if detour is within +/- 2GB range for JMP rel32.
        // If detour is > 2GB away (common with 64-bit ASLR DLLs), allocate a Near Relay stub!
        int64_t relOffset = reinterpret_cast<uint8_t*>(m_detour) - (m_target + 5);
        if (relOffset < INT32_MIN || relOffset > INT32_MAX) {
            m_relay = reinterpret_cast<uint8_t*>(DreamTrampolinePool::Instance().Allocate(m_target, 16));
            if (!m_relay) {
                DREAM_LOG_ERROR("DreamInlineHook: Failed to allocate near relay for 64-bit detour at 0x%p", m_detour);
                return false;
            }
            // Emit 14-byte 64-bit absolute indirect jump: FF 25 00 00 00 00 [64-bit m_detour]
            EmitAbsoluteJump64(m_relay, reinterpret_cast<uintptr_t>(m_detour));
            MemoryHelper::FlushCache(m_relay, 14);
            DREAM_LOG_DEBUG("DreamInlineHook: Created 64-bit near relay at 0x%p -> 0x%p", m_relay, m_detour);
        }

        // 6. Patch target with JMP rel32 (E9 <rel32>)
        return Enable();
    }

    bool Enable() {
        if (!m_target || !m_detour || m_isInstalled) return false;

        uint8_t* destination = m_relay ? m_relay : m_detour;
        int64_t relOffset = destination - (m_target + 5);
        if (relOffset < INT32_MIN || relOffset > INT32_MAX) {
            DREAM_LOG_ERROR("DreamInlineHook: Destination 0x%p out of range for rel32 jump from 0x%p", destination, m_target);
            return false;
        }

        // Tier 2: Try lock-free atomic CAS 5-byte patch if first instruction >= 5 bytes and inside single cache line
        auto firstDecoded = DreamDisasm::Decode(m_target);
        if (firstDecoded.length >= 5) {
            if (TryAtomicFiveBytePatch(m_target, static_cast<int32_t>(relOffset), firstDecoded.length)) {
                if (m_stolenLen > 5) {
                    ScopedMemoryProtect protect(m_target + 5, m_stolenLen - 5, PAGE_EXECUTE_READWRITE);
                    if (protect.IsSuccess()) {
                        std::memset(m_target + 5, 0x90, m_stolenLen - 5);
                        MemoryHelper::FlushCache(m_target + 5, m_stolenLen - 5);
                    }
                }
                m_isInstalled = true;
                return true;
            }
        }

        // Tier 3: Batch process-only thread freeze via ScopedThreadFreeze (NtGetNextThread)
        ScopedThreadFreeze freeze(m_target, m_stolenLen, m_trampoline, m_ipMap, m_ipMapCount);

        ScopedMemoryProtect protect(m_target, m_stolenLen, PAGE_EXECUTE_READWRITE);
        if (!protect.IsSuccess()) return false;

        m_target[0] = 0xE9; // JMP rel32
        *reinterpret_cast<int32_t*>(m_target + 1) = static_cast<int32_t>(relOffset);

        for (size_t i = 5; i < m_stolenLen; ++i) {
            m_target[i] = 0x90; // NOP
        }

        MemoryHelper::FlushCache(m_target, m_stolenLen);

        m_isInstalled = true;
        return true;
    }

    bool Disable() {
        if (!m_target || !m_isInstalled) return false;

        // Freeze threads while restoring original bytes (do not relocate to deactivated trampoline)
        ScopedThreadFreeze freeze(nullptr, 0, nullptr);

        ScopedMemoryProtect protect(m_target, m_stolenLen, PAGE_EXECUTE_READWRITE);
        if (!protect.IsSuccess()) return false;

        std::memcpy(m_target, m_originalBytes.data(), m_stolenLen);
        MemoryHelper::FlushCache(m_target, m_stolenLen);
        m_isInstalled = false;
        return true;
    }

    void* GetOriginal() const {
        return m_trampoline;
    }

    bool IsInstalled() const { return m_isInstalled; }

private:
    uint8_t* m_target{nullptr};
    uint8_t* m_detour{nullptr};
    uint8_t* m_trampoline{nullptr};
    uint8_t* m_relay{nullptr};
    size_t m_stolenLen{0};
    DreamIpMap m_ipMap[16]{};
    size_t m_ipMapCount{0};
    std::vector<uint8_t> m_originalBytes;
    bool m_isInstalled{false};
};

// ============================================================================
// DreamMidHook (Mid-Function Register Interceptor without Assembly files)
// ============================================================================
class DreamMidHook {
public:
    static void DispatchSafeCallback(DreamCpuContext* ctx, void* userData, MidHookContextCallback cb, DreamMidHook* hook = nullptr) {
        if (!cb || !ctx) return;
        if (hook && hook->m_circuitTripped.load(std::memory_order_relaxed)) return;

        // RAII In-Flight Tracking: Guarantees module hot-reload drains safely
        struct InFlightGuard {
            std::atomic<int32_t>* m_counter{nullptr};
            explicit InFlightGuard(std::atomic<int32_t>* c) : m_counter(c) {
                if (m_counter) m_counter->fetch_add(1, std::memory_order_relaxed);
            }
            ~InFlightGuard() {
                if (m_counter) m_counter->fetch_sub(1, std::memory_order_release);
            }
        } inFlightGuard(hook ? &hook->m_inFlight : nullptr);

        ScopedFiberTebGuard fiberGuard; // Protects TEB StackBase/StackLimit during custom fiber execution
        try {
            bool ok = DreamCrashGuard::ExecuteSafely("MidHookCallback", [&]() {
                cb(*ctx, userData);
            });
            if (hook) {
                if (ok) {
                    hook->m_consecutiveFaults.store(0, std::memory_order_relaxed);
                } else {
                    uint32_t f = ++hook->m_consecutiveFaults;
                    if (f >= 5) {
                        hook->m_circuitTripped.store(true, std::memory_order_relaxed);
                        DREAM_LOG_ERROR("DreamMidHook: Circuit breaker TRIPPED for hook at %p (5 consecutive faults). Callback disarmed.",
                                        hook->m_target);
                    }
                }
            }
        } catch (...) {
            if (hook) {
                uint32_t f = ++hook->m_consecutiveFaults;
                if (f >= 5) {
                    hook->m_circuitTripped.store(true, std::memory_order_relaxed);
                }
            }
        }
    }

    DreamMidHook() = default;
    ~DreamMidHook() { Disable(); }

    bool Install(void* hookSite, MidHookContextCallback callback, void* userData = nullptr) {
        if (!hookSite || !callback) return false;
        if (m_isInstalled) return true;

        m_target = reinterpret_cast<uint8_t*>(hookSite);
        m_callback = callback;
        m_userData = userData;

        m_stolenLen = DreamDisasm::GetBoundary(m_target, 5);
        if (m_stolenLen < 5 || m_stolenLen > 32) return false;

        m_originalBytes.resize(m_stolenLen);
        std::memcpy(m_originalBytes.data(), m_target, m_stolenLen);

        // Allocate framed trampoline stub near hook site using Near-Memory Arena
        m_stubPage = reinterpret_cast<uint8_t*>(DreamTrampolinePool::Instance().AllocateMidHook(m_target, 512));
        if (!m_stubPage) return false;

        // Construct dynamic stub in memory:
        // 1. pushfq
        // 2. push GPRs (rax, rbx, rcx, rdx, rsi, rdi, rbp, r8..r15)
        // 3. sub rsp, 0x100; save xmm0..xmm15
        // 4. mov rcx, rsp (ctx pointer)
        // 5. sub rsp, 0x28 (shadow space + align); call m_callback; add rsp, 0x28
        // 6. restore xmm0..xmm15; add rsp, 0x100
        // 7. pop GPRs; popfq
        // 8. execute relocated stolen bytes
        // 9. jmp back to target + stolenLen

        std::vector<uint8_t> code;
        auto emit8 = [&](uint8_t b) { code.push_back(b); };
        auto emit32 = [&](uint32_t d) {
            code.push_back(d & 0xFF);
            code.push_back((d >> 8) & 0xFF);
            code.push_back((d >> 16) & 0xFF);
            code.push_back((d >> 24) & 0xFF);
        };
        auto emit64 = [&](uint64_t q) {
            emit32(static_cast<uint32_t>(q & 0xFFFFFFFF));
            emit32(static_cast<uint32_t>(q >> 32));
        };

        // pushfq (9C)
        emit8(0x9C);
        // push rax..r15
        emit8(0x50); // push rax
        emit8(0x53); // push rbx
        emit8(0x51); // push rcx
        emit8(0x52); // push rdx
        emit8(0x56); // push rsi
        emit8(0x57); // push rdi
        emit8(0x55); // push rbp
        emit8(0x41); emit8(0x50); // push r8
        emit8(0x41); emit8(0x51); // push r9
        emit8(0x41); emit8(0x52); // push r10
        emit8(0x41); emit8(0x53); // push r11
        emit8(0x41); emit8(0x54); // push r12
        emit8(0x41); emit8(0x55); // push r13
        emit8(0x41); emit8(0x56); // push r14
        emit8(0x41); emit8(0x57); // push r15

        // sub rsp, 0x200 (48 81 EC 00 02 00 00) -> 512 bytes for YMM0..YMM15 vector registers
        emit8(0x48); emit8(0x81); emit8(0xEC); emit32(0x200);

        // mov rbp, rsp (48 89 E5) -> Prologue complete! Exactly matches MinimalUnwindInfo SizeOfProlog = 0x22
        emit8(0x48); emit8(0x89); emit8(0xE5);

        bool useAvx = CpuSupportsAvx();

        if (useAvx) {
            // Save YMM0..YMM15 (VMOVDQU [rsp + i*32], ymm_i)
            for (int i = 0; i < 16; ++i) {
                uint32_t disp = static_cast<uint32_t>(i * 32);
                if (i == 0) {
                    emit8(0xC5); emit8(0xFE); emit8(0x7F); emit8(0x04); emit8(0x24);
                } else if (i == 1) {
                    emit8(0xC5); emit8(0xFE); emit8(0x7F); emit8(0x4C); emit8(0x24); emit8(0x20);
                } else if (i == 2) {
                    emit8(0xC5); emit8(0xFE); emit8(0x7F); emit8(0x54); emit8(0x24); emit8(0x40);
                } else if (i == 3) {
                    emit8(0xC5); emit8(0xFE); emit8(0x7F); emit8(0x5C); emit8(0x24); emit8(0x60);
                } else if (i < 8) {
                    // YMM4..YMM7 with disp32 (disp >= 0x80)
                    emit8(0xC5); emit8(0xFE); emit8(0x7F);
                    emit8(static_cast<uint8_t>(0x80 | ((i & 7) << 3) | 0x04));
                    emit8(0x24);
                    emit32(disp);
                } else {
                    // YMM8..YMM15 with disp32 (VEX.R = 0 -> 0xC5 0x7E)
                    emit8(0xC5); emit8(0x7E); emit8(0x7F);
                    emit8(static_cast<uint8_t>(0x80 | ((i & 7) << 3) | 0x04));
                    emit8(0x24);
                    emit32(disp);
                }
            }
            // vzeroupper (C5 F8 77) - prevent AVX-SSE transition penalty during callback
            emit8(0xC5); emit8(0xF8); emit8(0x77);
        } else {
            // Non-AVX fallback: Save XMM0..XMM15 into 32-byte slots (movdqu [rsp + i*32], xmm_i)
            for (int i = 0; i < 16; ++i) {
                uint32_t disp = static_cast<uint32_t>(i * 32);
                if (i < 4) {
                    emit8(0xF3); emit8(0x0F); emit8(0x7F);
                    emit8(static_cast<uint8_t>(0x40 | ((i & 7) << 3) | 0x04));
                    emit8(0x24);
                    emit8(static_cast<uint8_t>(disp));
                } else if (i < 8) {
                    emit8(0xF3); emit8(0x0F); emit8(0x7F);
                    emit8(static_cast<uint8_t>(0x80 | ((i & 7) << 3) | 0x04));
                    emit8(0x24);
                    emit32(disp);
                } else {
                    emit8(0xF3); emit8(0x44); emit8(0x0F); emit8(0x7F);
                    emit8(static_cast<uint8_t>(0x80 | ((i & 7) << 3) | 0x04));
                    emit8(0x24);
                    emit32(disp);
                }
            }
        }

        // cld (FC) -> guarantee Direction Flag DF=0 per Windows x64 ABI
        emit8(0xFC);

        // mov rcx, rbp (48 89 E9) -> rcx = &DreamCpuContext
        emit8(0x48); emit8(0x89); emit8(0xE9);

        // mov rdx, userData (48 BA [64-bit]) -> rdx = m_userData
        emit8(0x48); emit8(0xBA); emit64(reinterpret_cast<uint64_t>(m_userData));

        // mov r8, callback (49 B8 [64-bit]) -> r8 = callback
        emit8(0x49); emit8(0xB8); emit64(reinterpret_cast<uint64_t>(callback));

        // mov r9, this (49 B9 [64-bit]) -> r9 = this (DreamMidHook instance for per-hook circuit breaker)
        emit8(0x49); emit8(0xB9); emit64(reinterpret_cast<uint64_t>(this));

        // and rsp, -16 (48 83 E4 F0) -> guarantee 16-byte stack alignment
        emit8(0x48); emit8(0x83); emit8(0xE4); emit8(0xF0);

        // sub rsp, 0x20 (48 83 EC 20) -> 32-byte shadow space for x64 ABI
        emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x20);

        // mov rax, DispatchSafeCallback (48 B8 [64-bit]) -> compiled function with valid SEH unwind table
        emit8(0x48); emit8(0xB8); emit64(reinterpret_cast<uint64_t>(&DispatchSafeCallback));

        // call rax (FF D0)
        emit8(0xFF); emit8(0xD0);

        // mov rsp, rbp (48 89 EC) -> restore exact rsp pointing to context
        emit8(0x48); emit8(0x89); emit8(0xEC);

        // Restore vector registers
        if (useAvx) {
            // Restore YMM0..YMM15 (VMOVDQU ymm_i, [rsp + i*32])
            for (int i = 0; i < 16; ++i) {
                uint32_t disp = static_cast<uint32_t>(i * 32);
                if (i == 0) {
                    emit8(0xC5); emit8(0xFE); emit8(0x6F); emit8(0x04); emit8(0x24);
                } else if (i == 1) {
                    emit8(0xC5); emit8(0xFE); emit8(0x6F); emit8(0x4C); emit8(0x24); emit8(0x20);
                } else if (i == 2) {
                    emit8(0xC5); emit8(0xFE); emit8(0x6F); emit8(0x54); emit8(0x24); emit8(0x40);
                } else if (i == 3) {
                    emit8(0xC5); emit8(0xFE); emit8(0x6F); emit8(0x5C); emit8(0x24); emit8(0x60);
                } else if (i < 8) {
                    emit8(0xC5); emit8(0xFE); emit8(0x6F);
                    emit8(static_cast<uint8_t>(0x80 | ((i & 7) << 3) | 0x04));
                    emit8(0x24);
                    emit32(disp);
                } else {
                    emit8(0xC5); emit8(0x7E); emit8(0x6F);
                    emit8(static_cast<uint8_t>(0x80 | ((i & 7) << 3) | 0x04));
                    emit8(0x24);
                    emit32(disp);
                }
            }
        } else {
            // Restore XMM0..XMM15 from 32-byte slots
            for (int i = 0; i < 16; ++i) {
                uint32_t disp = static_cast<uint32_t>(i * 32);
                if (i < 4) {
                    emit8(0xF3); emit8(0x0F); emit8(0x6F);
                    emit8(static_cast<uint8_t>(0x40 | ((i & 7) << 3) | 0x04));
                    emit8(0x24);
                    emit8(static_cast<uint8_t>(disp));
                } else if (i < 8) {
                    emit8(0xF3); emit8(0x0F); emit8(0x6F);
                    emit8(static_cast<uint8_t>(0x80 | ((i & 7) << 3) | 0x04));
                    emit8(0x24);
                    emit32(disp);
                } else {
                    emit8(0xF3); emit8(0x44); emit8(0x0F); emit8(0x6F);
                    emit8(static_cast<uint8_t>(0x80 | ((i & 7) << 3) | 0x04));
                    emit8(0x24);
                    emit32(disp);
                }
            }
        }

        // add rsp, 0x200 (48 81 C4 00 02 00 00)
        emit8(0x48); emit8(0x81); emit8(0xC4); emit32(0x200);

        // pop r15..rax
        emit8(0x41); emit8(0x5F); // pop r15
        emit8(0x41); emit8(0x5E); // pop r14
        emit8(0x41); emit8(0x5D); // pop r13
        emit8(0x41); emit8(0x5C); // pop r12
        emit8(0x41); emit8(0x5B); // pop r11
        emit8(0x41); emit8(0x5A); // pop r10
        emit8(0x41); emit8(0x59); // pop r9
        emit8(0x41); emit8(0x58); // pop r8
        emit8(0x5D); // pop rbp
        emit8(0x5F); // pop rdi
        emit8(0x5E); // pop rsi
        emit8(0x5A); // pop rdx
        emit8(0x59); // pop rcx
        emit8(0x5B); // pop rbx
        emit8(0x58); // pop rax
        emit8(0x9D); // popfq

        // Multi-Loader Coexistence: If target already has an E9/FF 25 hook, chain directly to foreign destination!
        bool existingForeignHook = false;
        uintptr_t foreignDest = 0;
        if (m_target[0] == 0xE9) {
            int32_t oldRel = *reinterpret_cast<const int32_t*>(m_target + 1);
            foreignDest = reinterpret_cast<uintptr_t>(m_target + 5) + oldRel;
            existingForeignHook = true;
        } else if (m_target[0] == 0xFF && m_target[1] == 0x25) {
            int32_t disp = *reinterpret_cast<const int32_t*>(m_target + 2);
            uintptr_t ptrAddress = reinterpret_cast<uintptr_t>(m_target + 6) + disp;
            foreignDest = *reinterpret_cast<const uintptr_t*>(ptrAddress);
            existingForeignHook = true;
        }

        m_ipMapCount = 0;
        if (existingForeignHook && foreignDest != 0) {
            DREAM_LOG_INFO("DreamMidHook: Transparently chaining over foreign hook at 0x%p -> 0x%p", m_target, foreignDest);
            m_ipMap[0] = { 0, static_cast<uint16_t>(code.size()) };
            m_ipMapCount = 1;
            emit8(0xFF); emit8(0x25);
            emit32(0); // [rip + 0]
            emit64(foreignDest);
        } else {
            // Append stolen instructions
            size_t stolenOffset = 0;
            while (stolenOffset < m_stolenLen) {
                auto decoded = DreamDisasm::Decode(m_target + stolenOffset);
                if (decoded.length == 0) return false;

                // Relocation Guard: Reject unsupported LOOP/JRCXZ instructions (0xE0..0xE3)
                if (DreamDisasm::IsLoopOrJrcxz(decoded.opcode)) {
                    DREAM_LOG_WARN("DreamMidHook: Cannot relocate LOOP/JRCXZ instruction (0x%02X) at 0x%p",
                                   decoded.opcode, m_target + stolenOffset);
                    return false;
                }

                // Relocation Guard: Reject terminal instructions (RET/JMP/INT3) in the middle of stolen bytes
                if ((stolenOffset + decoded.length) < m_stolenLen &&
                    DreamDisasm::IsTerminalInstruction(decoded.opcode, decoded.opcode2, decoded.modrmReg)) {
                    DREAM_LOG_WARN("DreamMidHook: Mid-region terminal instruction (0x%02X) detected at 0x%p",
                                   decoded.opcode, m_target + stolenOffset);
                    return false;
                }

                size_t instrStart = code.size();
                if (m_ipMapCount < 16) {
                    m_ipMap[m_ipMapCount++] = {
                        static_cast<uint16_t>(stolenOffset),
                        static_cast<uint16_t>(instrStart)
                    };
                }

                // Handle 0xEB short relative jump (convert to 0xE9 near jump)
                if (decoded.opcode == 0xEB && decoded.length == 2) {
                    int8_t rel8 = static_cast<int8_t>(m_target[stolenOffset + 1]);
                    uintptr_t destAddr = reinterpret_cast<uintptr_t>(m_target + stolenOffset + 2) + rel8;
                    if (destAddr >= reinterpret_cast<uintptr_t>(m_target) && destAddr < reinterpret_cast<uintptr_t>(m_target + m_stolenLen)) return false;
                    uintptr_t newInstrEnd = reinterpret_cast<uintptr_t>(m_stubPage + instrStart + 5);
                    int64_t newDisp = static_cast<int64_t>(destAddr - newInstrEnd);
                    if (newDisp < INT32_MIN || newDisp > INT32_MAX) return false;
                    code.push_back(0xE9);
                    emit32(static_cast<int32_t>(newDisp));
                    stolenOffset += 2;
                    continue;
                }

                // Handle short conditional jump 70..7F rel8 -> widen to 0F 80..8F rel32
                if ((decoded.opcode >= 0x70 && decoded.opcode <= 0x7F) && decoded.length == 2) {
                    int8_t oldDisp = static_cast<int8_t>(m_target[stolenOffset + 1]);
                    uintptr_t destAddr = reinterpret_cast<uintptr_t>(m_target + stolenOffset + 2) + oldDisp;
                    if (destAddr >= reinterpret_cast<uintptr_t>(m_target) && destAddr < reinterpret_cast<uintptr_t>(m_target + m_stolenLen)) return false;
                    uintptr_t newInstrEnd = reinterpret_cast<uintptr_t>(m_stubPage + instrStart + 6);
                    int64_t newDisp = static_cast<int64_t>(destAddr - newInstrEnd);
                    if (newDisp < INT32_MIN || newDisp > INT32_MAX) return false;
                    code.push_back(0x0F);
                    code.push_back(static_cast<uint8_t>(0x80 + (decoded.opcode & 0x0F)));
                    emit32(static_cast<int32_t>(newDisp));
                    stolenOffset += 2;
                    continue;
                }

                // Handle 0xE8 (CALL rel32) and 0xE9 (JMP rel32) displacement fixups
                if ((decoded.opcode == 0xE8 || decoded.opcode == 0xE9) && decoded.length == 5) {
                    int32_t oldDisp = *reinterpret_cast<const int32_t*>(m_target + stolenOffset + 1);
                    uintptr_t destAddr = reinterpret_cast<uintptr_t>(m_target + stolenOffset + 5) + oldDisp;
                    if (destAddr >= reinterpret_cast<uintptr_t>(m_target) && destAddr < reinterpret_cast<uintptr_t>(m_target + m_stolenLen)) return false;
                    uintptr_t newInstrEnd = reinterpret_cast<uintptr_t>(m_stubPage + instrStart + 5);
                    int64_t newDisp = static_cast<int64_t>(destAddr - newInstrEnd);
                    if (newDisp < INT32_MIN || newDisp > INT32_MAX) return false;
                    code.push_back(decoded.opcode);
                    emit32(static_cast<int32_t>(newDisp));
                    stolenOffset += 5;
                    continue;
                }

                // Handle 0F 80..8F (Near conditional jump Jcc rel32, 6 bytes)
                if (decoded.opcode == 0x0F && (decoded.opcode2 >= 0x80 && decoded.opcode2 <= 0x8F) && decoded.length == 6) {
                    int32_t oldDisp = *reinterpret_cast<const int32_t*>(m_target + stolenOffset + 2);
                    uintptr_t destAddr = reinterpret_cast<uintptr_t>(m_target + stolenOffset + 6) + oldDisp;
                    if (destAddr >= reinterpret_cast<uintptr_t>(m_target) && destAddr < reinterpret_cast<uintptr_t>(m_target + m_stolenLen)) return false;
                    uintptr_t newInstrEnd = reinterpret_cast<uintptr_t>(m_stubPage + instrStart + 6);
                    int64_t newDisp = static_cast<int64_t>(destAddr - newInstrEnd);
                    if (newDisp < INT32_MIN || newDisp > INT32_MAX) return false;
                    code.push_back(0x0F);
                    code.push_back(decoded.opcode2);
                    emit32(static_cast<int32_t>(newDisp));
                    stolenOffset += 6;
                    continue;
                }

                // Copy instruction bytes
                for (size_t b = 0; b < decoded.length; ++b) {
                    code.push_back(m_target[stolenOffset + b]);
                }

                // Handle RIP-relative ModR/M addressing (e.g. mov rax, [rip + disp32])
                if (decoded.isRipRelative && decoded.dispSize == 4) {
                    uintptr_t oldTargetAddr = reinterpret_cast<uintptr_t>(m_target + stolenOffset + decoded.length) + decoded.dispVal;
                    uintptr_t newInstrEnd = reinterpret_cast<uintptr_t>(m_stubPage + instrStart + decoded.length);
                    int64_t newDisp = static_cast<int64_t>(oldTargetAddr - newInstrEnd);
                    if (newDisp < INT32_MIN || newDisp > INT32_MAX) {
                        DREAM_LOG_WARN("DreamMidHook: RIP-relative operand out of +-2GB range at 0x%p", m_target + stolenOffset);
                        return false;
                    }
                    *reinterpret_cast<int32_t*>(&code[instrStart + decoded.dispOffset]) = static_cast<int32_t>(newDisp);
                }

                stolenOffset += decoded.length;
            }

            // Jump back to target + stolenLen (FF 25 00 00 00 00 [64-bit])
            emit8(0xFF); emit8(0x25);
            emit32(0); // [rip + 0]
            emit64(reinterpret_cast<uint64_t>(m_target + m_stolenLen));
        }

        // Copy generated stub to executable page
        std::memcpy(m_stubPage, code.data(), code.size());
        MemoryHelper::FlushCache(m_stubPage, code.size());

        // Patch target site with JMP rel32 to m_stubPage
        return Enable();
    }

    bool Enable() {
        if (!m_target || !m_stubPage || m_isInstalled) return false;

        // Freeze worker threads during mid-hook patching with accurate instruction pointer mapping
        ScopedThreadFreeze freeze(m_target, m_stolenLen, m_stubPage, m_ipMap, m_ipMapCount);

        ScopedMemoryProtect protect(m_target, m_stolenLen, PAGE_EXECUTE_READWRITE);
        if (!protect.IsSuccess()) return false;

        int64_t relOffset = m_stubPage - (m_target + 5);
        if (relOffset < INT32_MIN || relOffset > INT32_MAX) return false;

        m_target[0] = 0xE9; // JMP rel32
        *reinterpret_cast<int32_t*>(m_target + 1) = static_cast<int32_t>(relOffset);

        for (size_t i = 5; i < m_stolenLen; ++i) {
            m_target[i] = 0x90; // NOP
        }

        MemoryHelper::FlushCache(m_target, m_stolenLen);

        m_isInstalled = true;
        return true;
    }

    bool Disable() {
        if (!m_target || !m_isInstalled) return false;

        // Freeze worker threads during restore (do not relocate to deactivated stub)
        ScopedThreadFreeze freeze(nullptr, 0, nullptr);

        ScopedMemoryProtect protect(m_target, m_stolenLen, PAGE_EXECUTE_READWRITE);
        if (!protect.IsSuccess()) return false;

        std::memcpy(m_target, m_originalBytes.data(), m_stolenLen);
        MemoryHelper::FlushCache(m_target, m_stolenLen);
        m_isInstalled = false;
        return true;
    }

    bool Install(void* hookSite, MidHookCallback callback) {
        return Install(hookSite, reinterpret_cast<MidHookContextCallback>(callback), nullptr);
    }

    bool IsInstalled() const { return m_isInstalled; }
    void* GetTarget() const { return m_target; }
    int32_t GetInFlightCount() const { return m_inFlight.load(std::memory_order_relaxed); }

private:
    uint8_t* m_target{nullptr};
    uint8_t* m_stubPage{nullptr};
    MidHookContextCallback m_callback{nullptr};
    void* m_userData{nullptr};
    size_t m_stolenLen{0};
    DreamIpMap m_ipMap[16]{};
    size_t m_ipMapCount{0};
    std::atomic<uint32_t> m_consecutiveFaults{0};
    std::atomic<bool> m_circuitTripped{false};
    std::atomic<int32_t> m_inFlight{0};
    std::vector<uint8_t> m_originalBytes;
    bool m_isInstalled{false};
};

// ============================================================================
// DreamChainedHookPipeline - Multi-Subscriber MidHook Dispatcher
// Automatically coordinates multiple independent plugins hooking the same site!
// ============================================================================
class DreamChainedHookPipeline {
public:
    using SubscriberFn = std::function<void(DreamCpuContext&)>;

    struct Subscriber {
        std::string name;
        SubscriberFn callback;
        int priority{0}; // Higher priority runs earlier
    };

    static DreamChainedHookPipeline& Instance() {
        static DreamChainedHookPipeline s_inst;
        return s_inst;
    }

    bool Register(void* target, SubscriberFn cb, int priority = 0, const std::string& name = "") {
        if (!target || !cb) return false;
        std::lock_guard<std::mutex> lock(m_mutex);

        auto it = m_chains.find(target);
        if (it == m_chains.end()) {
            auto chain = std::make_unique<ChainNode>();
            chain->target = target;
            chain->subscribers.push_back({ name, cb, priority });
            chain->subscribersSnapshot.store(std::make_shared<const std::vector<Subscriber>>(chain->subscribers));

            bool ok = chain->midHook.Install(target, &DreamChainedHookPipeline::Dispatcher, chain.get());
            if (!ok) return false;

            m_chains[target] = std::move(chain);
            return true;
        }

        auto& chain = it->second;
        chain->subscribers.push_back({ name, cb, priority });
        std::sort(chain->subscribers.begin(), chain->subscribers.end(), [](const Subscriber& a, const Subscriber& b) {
            return a.priority > b.priority;
        });
        chain->subscribersSnapshot.store(std::make_shared<const std::vector<Subscriber>>(chain->subscribers));
        return true;
    }

private:
    struct ChainNode {
        void* target{nullptr};
        DreamMidHook midHook;
        std::vector<Subscriber> subscribers;
        std::atomic<std::shared_ptr<const std::vector<Subscriber>>> subscribersSnapshot;
    };

    static void Dispatcher(DreamCpuContext& ctx, void* userData) {
        auto* node = reinterpret_cast<ChainNode*>(userData);
        if (!node) return;
        auto snap = node->subscribersSnapshot.load();
        if (!snap) return;

        for (const auto& sub : *snap) {
            if (sub.callback) {
                LARGE_INTEGER freq, t0, t1;
                QueryPerformanceFrequency(&freq);
                QueryPerformanceCounter(&t0);

                DreamCrashGuard::ExecuteSafely(sub.name.empty() ? "ChainedSubscriber" : sub.name.c_str(), [&]() {
                    sub.callback(ctx);
                });

                QueryPerformanceCounter(&t1);
                double elapsedMs = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;
                if (elapsedMs > 10.0) {
                    DREAM_LOG_WARN("[HookLatency] Subscriber '%s' took %.2f ms at %p (spike threshold: 10ms)",
                                   sub.name.empty() ? "unnamed" : sub.name.c_str(), elapsedMs, node->target);
                }
            }
        }
    }

    std::unordered_map<void*, std::unique_ptr<ChainNode>> m_chains;
    std::mutex m_mutex;
};

} // namespace dreamkurumi

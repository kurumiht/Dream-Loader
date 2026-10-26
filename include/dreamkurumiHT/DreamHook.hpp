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
#include "DreamCore.hpp"
#include "DreamDisasm.hpp"
#include "DreamPool.hpp"
#include "DreamThread.hpp"
#include "DreamCrashGuard.hpp"

namespace dreamkurumi {

// ============================================================================
// CPU Register Context for Mid-Function Hooks
// ============================================================================
#pragma pack(push, 1)
struct DreamCpuContext {
    union {
        float    f32[4];
        double   f64[2];
        uint32_t u32[4];
        uint64_t u64[2];
    } xmm[16];          // 0x000 - 0x0FF (256 bytes)

    uint64_t r15;       // 0x100
    uint64_t r14;       // 0x108
    uint64_t r13;       // 0x110
    uint64_t r12;       // 0x118
    uint64_t r11;       // 0x120
    uint64_t r10;       // 0x128
    uint64_t r9;        // 0x130
    uint64_t r8;        // 0x138
    uint64_t rbp;       // 0x140
    uint64_t rdi;       // 0x148
    uint64_t rsi;       // 0x150
    uint64_t rdx;       // 0x158
    uint64_t rcx;       // 0x160
    uint64_t rbx;       // 0x168
    uint64_t rax;       // 0x170
    uint64_t rflags;    // 0x178
};
#pragma pack(pop)

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
        // Copy stolen bytes and relocate RIP-relative offsets if necessary
        size_t trampOffset = 0;
        size_t curOffset = 0;

        while (curOffset < m_stolenLen) {
            auto decoded = DreamDisasm::Decode(m_target + curOffset);
            if (decoded.length == 0) return false;

            std::memcpy(m_trampoline + trampOffset, m_target + curOffset, decoded.length);

            if (decoded.isRipRelative && decoded.dispSize == 4) {
                // Adjust RIP displacement:
                // oldTargetAddr = target + curOffset + decoded.length + dispVal
                // newDispVal = oldTargetAddr - (trampoline + trampOffset + decoded.length)
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

        // Append 14-byte 64-bit absolute jump back to target + stolenLen:
        // FF 25 00 00 00 00 [64-bit address]
        uint8_t* jmpBack = m_trampoline + trampOffset;
        jmpBack[0] = 0xFF;
        jmpBack[1] = 0x25;
        *reinterpret_cast<int32_t*>(jmpBack + 2) = 0; // [rip + 0]
        *reinterpret_cast<void**>(jmpBack + 6) = m_target + m_stolenLen;
        trampOffset += 14;

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
            m_relay[0] = 0xFF;
            m_relay[1] = 0x25;
            *reinterpret_cast<int32_t*>(m_relay + 2) = 0; // [rip + 0]
            *reinterpret_cast<void**>(m_relay + 6) = m_detour;
            MemoryHelper::FlushCache(m_relay, 14);
            DREAM_LOG_DEBUG("DreamInlineHook: Created 64-bit near relay at 0x%p -> 0x%p", m_relay, m_detour);
        }

        // 6. Patch target with JMP rel32 (E9 <rel32>)
        return Enable();
    }

    bool Enable() {
        if (!m_target || !m_detour || m_isInstalled) return false;

        // Freeze all worker threads and relocate RIP if paused inside patch bytes
        ScopedThreadFreeze freeze(m_target, m_stolenLen, m_trampoline);

        ScopedMemoryProtect protect(m_target, m_stolenLen, PAGE_EXECUTE_READWRITE);
        if (!protect.IsSuccess()) return false;

        uint8_t* destination = m_relay ? m_relay : m_detour;
        int64_t relOffset = destination - (m_target + 5);
        if (relOffset < INT32_MIN || relOffset > INT32_MAX) {
            DREAM_LOG_ERROR("DreamInlineHook: Destination 0x%p out of range for rel32 jump from 0x%p", destination, m_target);
            return false;
        }

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

        // Freeze threads while restoring original bytes
        ScopedThreadFreeze freeze(m_target, m_stolenLen, m_trampoline);

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
    std::vector<uint8_t> m_originalBytes;
    bool m_isInstalled{false};
};

// ============================================================================
// DreamMidHook (Mid-Function Register Interceptor without Assembly files)
// ============================================================================
class DreamMidHook {
public:
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

        // Allocate trampoline stub near hook site using Near-Memory Arena
        m_stubPage = reinterpret_cast<uint8_t*>(DreamTrampolinePool::Instance().Allocate(m_target, 512));
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

        // sub rsp, 0x100 (48 81 EC 00 01 00 00)
        emit8(0x48); emit8(0x81); emit8(0xEC); emit32(0x100);

        // Save XMM0..XMM15 (movdqu [rsp + i*16], xmm_i)
        for (int i = 0; i < 16; ++i) {
            if (i < 8) {
                // ModR/M: mod=01 (disp8), reg=(i&7), r/m=100 (SIB) -> 0x40 | ((i & 7) << 3) | 0x04
                emit8(0xF3); emit8(0x0F); emit8(0x7F);
                emit8(static_cast<uint8_t>(0x40 | ((i & 7) << 3) | 0x04));
                emit8(0x24); // SIB: [rsp]
                emit8(static_cast<uint8_t>(i * 16));
            } else {
                // REX.R (0x44) extends reg to XMM8..XMM15
                // ModR/M: mod=10 (disp32), reg=(i&7), r/m=100 (SIB) -> 0x80 | ((i & 7) << 3) | 0x04
                emit8(0xF3); emit8(0x44); emit8(0x0F); emit8(0x7F);
                emit8(static_cast<uint8_t>(0x80 | ((i & 7) << 3) | 0x04));
                emit8(0x24); // SIB: [rsp]
                emit32(i * 16);
            }
        }

        // mov rcx, rsp (48 89 E1) -> rcx = &DreamCpuContext
        emit8(0x48); emit8(0x89); emit8(0xE1);

        // mov rdx, userData (48 BA [64-bit]) -> rdx = m_userData
        emit8(0x48); emit8(0xBA); emit64(reinterpret_cast<uint64_t>(m_userData));

        // mov rbp, rsp (48 89 E5) -> save context rsp in rbp (rbp was pushed on stack)
        emit8(0x48); emit8(0x89); emit8(0xE5);

        // and rsp, -16 (48 83 E4 F0) -> guarantee 16-byte stack alignment
        emit8(0x48); emit8(0x83); emit8(0xE4); emit8(0xF0);

        // sub rsp, 0x20 (48 83 EC 20) -> 32-byte shadow space for x64 ABI
        emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x20);

        // mov rax, callback (48 B8 [64-bit])
        emit8(0x48); emit8(0xB8); emit64(reinterpret_cast<uint64_t>(callback));

        // call rax (FF D0)
        emit8(0xFF); emit8(0xD0);

        // mov rsp, rbp (48 89 EC) -> restore exact rsp pointing to context
        emit8(0x48); emit8(0x89); emit8(0xEC);

        // Restore XMM0..XMM15
        for (int i = 0; i < 16; ++i) {
            if (i < 8) {
                emit8(0xF3); emit8(0x0F); emit8(0x6F);
                emit8(static_cast<uint8_t>(0x40 | ((i & 7) << 3) | 0x04));
                emit8(0x24);
                emit8(static_cast<uint8_t>(i * 16));
            } else {
                emit8(0xF3); emit8(0x44); emit8(0x0F); emit8(0x6F);
                emit8(static_cast<uint8_t>(0x80 | ((i & 7) << 3) | 0x04));
                emit8(0x24);
                emit32(i * 16);
            }
        }

        // add rsp, 0x100 (48 81 C4 00 01 00 00)
        emit8(0x48); emit8(0x81); emit8(0xC4); emit32(0x100);

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

        // Append stolen instructions
        size_t stolenOffset = 0;
        while (stolenOffset < m_stolenLen) {
            auto decoded = DreamDisasm::Decode(m_target + stolenOffset);
            if (decoded.length == 0) return false;

            size_t instrStart = code.size();
            for (size_t b = 0; b < decoded.length; ++b) {
                code.push_back(m_target[stolenOffset + b]);
            }

            // Handle 0xEB short relative jump (convert to 0xE9 near jump)
            if (decoded.opcode == 0xEB && decoded.length == 2) {
                int8_t rel8 = static_cast<int8_t>(m_target[stolenOffset + 1]);
                uintptr_t destAddr = reinterpret_cast<uintptr_t>(m_target + stolenOffset + 2) + rel8;
                code.pop_back(); code.pop_back();
                instrStart = code.size();
                code.push_back(0xE9);
                uintptr_t newInstrEnd = reinterpret_cast<uintptr_t>(m_stubPage + instrStart + 5);
                int32_t newDisp = static_cast<int32_t>(destAddr - newInstrEnd);
                emit32(newDisp);
                stolenOffset += decoded.length;
                continue;
            }

            // Handle 0xE8 (CALL rel32) and 0xE9 (JMP rel32) displacement fixups
            if ((decoded.opcode == 0xE8 || decoded.opcode == 0xE9) && decoded.length == 5) {
                int32_t oldDisp = *reinterpret_cast<const int32_t*>(m_target + stolenOffset + 1);
                uintptr_t destAddr = reinterpret_cast<uintptr_t>(m_target + stolenOffset + 5) + oldDisp;
                uintptr_t newInstrEnd = reinterpret_cast<uintptr_t>(m_stubPage + instrStart + 5);
                int32_t newDisp = static_cast<int32_t>(destAddr - newInstrEnd);
                *reinterpret_cast<int32_t*>(&code[instrStart + 1]) = newDisp;
                stolenOffset += decoded.length;
                continue;
            }

            if (decoded.isRipRelative && decoded.dispSize == 4) {
                uintptr_t oldTargetAddr = reinterpret_cast<uintptr_t>(m_target + stolenOffset + decoded.length) + decoded.dispVal;
                uintptr_t newInstrEnd = reinterpret_cast<uintptr_t>(m_stubPage + instrStart + decoded.length);
                int64_t newDisp = static_cast<int64_t>(oldTargetAddr - newInstrEnd);
                *reinterpret_cast<int32_t*>(&code[instrStart + decoded.dispOffset]) = static_cast<int32_t>(newDisp);
            }

            stolenOffset += decoded.length;
        }

        // Jump back to target + stolenLen (FF 25 00 00 00 00 [64-bit])
        emit8(0xFF); emit8(0x25);
        emit32(0); // [rip + 0]
        emit64(reinterpret_cast<uint64_t>(m_target + m_stolenLen));

        // Copy generated stub to executable page
        std::memcpy(m_stubPage, code.data(), code.size());
        MemoryHelper::FlushCache(m_stubPage, code.size());

        // Patch target site with JMP rel32 to m_stubPage
        return Enable();
    }

    bool Enable() {
        if (!m_target || !m_stubPage || m_isInstalled) return false;

        // Freeze worker threads during mid-hook patching
        ScopedThreadFreeze freeze(m_target, m_stolenLen, m_stubPage);

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

        // Freeze worker threads during restore
        ScopedThreadFreeze freeze(m_target, m_stolenLen, m_stubPage);

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

private:
    uint8_t* m_target{nullptr};
    uint8_t* m_stubPage{nullptr};
    MidHookContextCallback m_callback{nullptr};
    void* m_userData{nullptr};
    size_t m_stolenLen{0};
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
        return true;
    }

private:
    struct ChainNode {
        void* target{nullptr};
        DreamMidHook midHook;
        std::vector<Subscriber> subscribers;
    };

    static void Dispatcher(DreamCpuContext& ctx, void* userData) {
        auto* node = reinterpret_cast<ChainNode*>(userData);
        if (node) {
            for (const auto& sub : node->subscribers) {
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
    }

    std::unordered_map<void*, std::unique_ptr<ChainNode>> m_chains;
    std::mutex m_mutex;
};

} // namespace dreamkurumi

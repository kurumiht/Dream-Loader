#pragma once
#include <windows.h>
#include <intrin.h>
#include <cstdint>
#include <string>
#include <functional>
#include <mutex>
#include <atomic>
#include <type_traits>
#include <unordered_map>
#include <sstream>
#include <iomanip>
#include "DreamLog.hpp"

extern "C" {
#include "hde/hde64.h"
}

namespace dreamkurumi {

// ============================================================================
// Instruction Mnemonic Decoder via HDE64
// Provides human-readable assembly operation hints in crash diagnostics
// ============================================================================
static inline const char* DreamGetOpcodeMnemonic(const hde64s& hs) {
    if (hs.flags & F_ERROR) return "Unknown";
    if (hs.flags & F_PREFIX_REP) return "REP string op";

    switch (hs.opcode) {
        case 0x88: case 0x89: case 0x8A: case 0x8B:
        case 0xC6: case 0xC7:
            return "MOV (Memory transfer)";
        case 0x8D:
            return "LEA (Load effective address)";
        case 0xE8:
            return "CALL (Relative call)";
        case 0xFF: {
            uint8_t reg = (hs.modrm >> 3) & 7;
            if (reg == 2) return "CALL (Indirect pointer call)";
            if (reg == 4) return "JMP (Indirect jump)";
            if (reg == 0) return "INC (Increment)";
            if (reg == 1) return "DEC (Decrement)";
            return "GRP5 (Call/Jmp/Push)";
        }
        case 0xE9: case 0xEB:
            return "JMP (Unconditional jump)";
        case 0x84: case 0x85:
            return "TEST (Bit test)";
        case 0x38: case 0x39: case 0x3A: case 0x3B: case 0x3C: case 0x3D:
            return "CMP (Compare)";
        case 0x80: case 0x81: case 0x83:
            return "ALU (Arithmetic / Logic)";
        case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
            return "PUSH register";
        case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F:
            return "POP register";
        case 0x0F: {
            switch (hs.opcode2) {
                case 0x10: case 0x11: return "MOVUPS / MOVSS (Float transfer)";
                case 0x28: case 0x29: return "MOVAPS (16-byte aligned SSE)";
                case 0x6F: case 0x7F: return "MOVDQA (16-byte aligned SIMD)";
                case 0xB6: case 0xB7: return "MOVZX (Zero-extend read)";
                case 0xBE: case 0xBF: return "MOVSX (Sign-extend read)";
                case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85: case 0x86: case 0x87:
                case 0x88: case 0x89: case 0x8A: case 0x8B: case 0x8C: case 0x8D: case 0x8E: case 0x8F:
                    return "Jcc (Conditional branch)";
                default: return "Two-byte opcode (0F xx)";
            }
        }
        default:
            if (hs.opcode >= 0x70 && hs.opcode <= 0x7F) return "Jcc (Short branch)";
            return "Instruction";
    }
}

// ============================================================================
// TEB Stack Boundary Accessors (Windows x64)
// Prevents out-of-bounds stack reads during stack unwinding inside VEH
// ============================================================================
static inline uintptr_t DreamGetTebStackLimit() {
#if defined(_MSC_VER) || defined(__GNUC__) || defined(__clang__)
    return __readgsqword(0x10); // TEB StackLimit
#else
    return 0;
#endif
}

static inline uintptr_t DreamGetTebStackBase() {
#if defined(_MSC_VER) || defined(__GNUC__) || defined(__clang__)
    return __readgsqword(0x08); // TEB StackBase
#else
    return 0;
#endif
}

static inline bool IsSafeStackQword(uintptr_t ptr, uintptr_t stackLimit, uintptr_t stackBase) noexcept {
    return (ptr >= stackLimit) &&
           (stackBase >= sizeof(uintptr_t)) &&
           (ptr <= (stackBase - sizeof(uintptr_t))) &&
           ((ptr & 0x7ULL) == 0); // Strictly 8-byte aligned
}

typedef PVOID (NTAPI *pfnRtlPcToFileHeader)(PVOID PcValue, PVOID *BaseOfImage);

static inline pfnRtlPcToFileHeader DreamGetRtlPcToFileHeader() {
    static pfnRtlPcToFileHeader s_pfn = []() {
        HMODULE hNt = GetModuleHandleW(L"ntdll.dll");
        return hNt ? reinterpret_cast<pfnRtlPcToFileHeader>(GetProcAddress(hNt, "RtlPcToFileHeader")) : nullptr;
    }();
    return s_pfn;
}

// Checks if candidateAddr resides inside a loaded PE module and is immediately preceded by a valid x64 CALL instruction
static inline bool IsLikelyReturnAddress(uintptr_t candidateAddr) noexcept {
    // 1. Canonical user-mode address check
    if (candidateAddr < 0x10000ULL || candidateAddr > 0x00007FFFFFFFFFFFULL) {
        return false;
    }

    // 2. Verify address belongs to a valid mapped PE image in process (lock-free NTDLL lookup)
    PVOID imageBase = nullptr;
    auto pfn = DreamGetRtlPcToFileHeader();
    if (!pfn || !pfn(reinterpret_cast<PVOID>(candidateAddr), &imageBase) || !imageBase) {
        return false;
    }

    // Ensure we can safely read up to 7 bytes before candidateAddr within the same module
    const uintptr_t baseAddr = reinterpret_cast<uintptr_t>(imageBase);
    if (candidateAddr < baseAddr + 0x1000ULL) {
        return false;
    }

    const uint8_t* p = reinterpret_cast<const uint8_t*>(candidateAddr);

    // 3. Check for standard x64 CALL encodings ending exactly at candidateAddr:
    // - E8 xx xx xx xx             (5-byte relative CALL rel32 - most common)
    if (p[-5] == 0xE8) return true;

    // - FF 15 xx xx xx xx          (6-byte RIP-relative indirect CALL [rip+disp32])
    if (p[-6] == 0xFF && p[-5] == 0x15) return true;

    // - FF D0..D7 / FF 10..17      (2-byte indirect CALL reg / CALL [reg])
    if (p[-2] == 0xFF && ((p[-1] & 0x38) == 0x10)) return true;

    // - 41 FF D0..D7 or FF 50 xx   (3-byte REX + CALL reg or CALL [reg+disp8])
    if (p[-3] == 0xFF && ((p[-2] & 0x38) == 0x10)) return true;

    // - FF 54 24 xx or 41 FF 50 xx (4-byte SIB/REX indirect CALL [rsp+disp8])
    if (p[-4] == 0xFF && ((p[-3] & 0x38) == 0x10)) return true;

    // - FF 90 xx xx xx xx          (6-byte indirect CALL [reg+disp32] - vtable calls!)
    if (p[-6] == 0xFF && ((p[-5] & 0x38) == 0x10)) return true;

    // - 41 FF 90 xx xx xx xx       (7-byte REX/SIB indirect CALL [r8+disp32])
    if (p[-7] == 0xFF && ((p[-6] & 0x38) == 0x10)) return true;

    return false;
}

// ============================================================================
// Capture Stack Backtrace inside VEH (Zero Allocation, Zero Lock, TEB-Bounded)
// Steps through frames using RtlVirtualUnwind while strictly respecting stack limits.
// Fallback uses Bounded Heuristic Scan (Dev 2 Architecture) up to 8 slots.
// ============================================================================
static inline uint8_t CaptureFrames(CONTEXT cx, uint64_t* out, size_t max) {
    const uintptr_t lo = DreamGetTebStackLimit(), hi = DreamGetTebStackBase();
    size_t n = 0;
    uintptr_t prevRip = 0;
    while (n < max && cx.Rip > 0x10000) {
        if (cx.Rip == prevRip) break;
        prevRip = cx.Rip;
        out[n++] = cx.Rip;
        if (!IsSafeStackQword(cx.Rsp, lo, hi)) break; // never read outside this thread's stack
        DWORD64 base = 0;
        auto* fe = RtlLookupFunctionEntry(cx.Rip, &base, nullptr);
        if (fe) {
            PVOID h = nullptr;
            DWORD64 ef = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, cx.Rip, fe, &cx, &h, &ef, nullptr);
        } else {
            // Bounded Heuristic Scan (Dev 2 Architecture): scan up to 8 slots [RSP+0x00] .. [RSP+0x38]
            bool unwoundLeaf = false;
            constexpr size_t MAX_LEAF_SCAN_SLOTS = 8;
            for (size_t slot = 0; slot < MAX_LEAF_SCAN_SLOTS; ++slot) {
                const uintptr_t scanRsp = cx.Rsp + (slot * sizeof(DWORD64));
                if (!IsSafeStackQword(scanRsp, lo, hi)) break;

                const uintptr_t candidateRet = *reinterpret_cast<const uintptr_t*>(scanRsp);
                if (IsLikelyReturnAddress(candidateRet) && candidateRet != cx.Rip) {
                    cx.Rip = candidateRet;
                    cx.Rsp = scanRsp + sizeof(DWORD64); // Pop past the found return address slot
                    unwoundLeaf = true;
                    break;
                }
            }

            if (!unwoundLeaf) {
                break; // Cannot reliably unwind further without risking junk RIP
            }
        }
    }
    return static_cast<uint8_t>(n);
}

// ============================================================================
// DreamCrashGuard - Anti-Crash Mod Sandbox Shield (v3.8.5 Hardened Edition)
// Protects the game process from crashing when mod code throws Access Violations.
// Architecture:
// 1. Direct RtlCaptureContext / NtContinue context restoration (Zero CRT longjmp)
// 2. Lock-free VEH register & stack frame snapshot bounded by thread TEB
// 3. Fast lock-free module resolution via RtlPcToFileHeader
// 4. Detailed Access Violation root cause diagnostics (Null Pointer, #GP, MEM_FREE)
// 5. Fault site deduplication by (module, rva) to prevent log flooding
// 6. Hook-isolated atomic circuit breakers to stop infinite crash loops
// ============================================================================
class DreamCrashGuard {
public:
    struct SandboxContext {
        volatile bool inSandbox{false};
        volatile bool faultOccurred{false};
        volatile bool isMemoryProbe{false};
        volatile uint32_t entryGen{0};
        const char* modName{nullptr};
        uintptr_t probeAddress{0};
        size_t probeSize{0};
        alignas(16) CONTEXT savedCtx{};

        // Registers captured in VEH
        uintptr_t faultRip{0};
        uintptr_t faultRsp{0};
        uintptr_t faultRax{0};
        uintptr_t faultRbx{0};
        uintptr_t faultRcx{0};
        uintptr_t faultRdx{0};
        uintptr_t faultRsi{0};
        uintptr_t faultRdi{0};
        uintptr_t faultRbp{0};
        uintptr_t faultR8{0};
        uintptr_t faultR9{0};
        uintptr_t faultR10{0};
        uintptr_t faultR11{0};
        DWORD faultCode{0};
        int faultAccessType{-1};        // 0 = READ, 1 = WRITE, 8 = EXECUTE
        uintptr_t faultTargetMemory{0}; // Target address attempted to access
        uint8_t faultOpcodes[16]{0};
        uint8_t faultOpcodesLen{0};

        // Stack peek (top 4 QWORDs on stack [rsp])
        uint64_t faultStackWords[4]{0};
        uint8_t faultStackWordsCount{0};

        // Fixed-size stack backtrace captured in VEH (Zero allocation)
        uint64_t backtrace[12]{0};
        uint8_t backtraceCount{0};

        SandboxContext* prevCtx{nullptr};
    };

    struct FaultSiteRecord {
        uint64_t count{0};
        uint64_t lastLogTimeMs{0};
    };

    static DreamCrashGuard& Instance() {
        static DreamCrashGuard s_instance;
        return s_instance;
    }

    static DWORD GetTlsIndex() {
        static DWORD s_tls = TlsAlloc();
        return s_tls;
    }

    static void SetDebuggerPassThrough(bool enable) noexcept {
        Instance().m_passThroughToDebugger.store(enable, std::memory_order_relaxed);
    }

    static bool GetDebuggerPassThrough() noexcept {
        return Instance().m_passThroughToDebugger.load(std::memory_order_relaxed);
    }

    static bool IsHardwareDebuggerAttached() noexcept {
#if defined(_M_X64) || defined(__x86_64__)
        const uint8_t* peb = reinterpret_cast<const uint8_t*>(__readgsqword(0x60));
        return peb && (peb[2] != 0); // PEB->BeingDebugged
#else
        return IsDebuggerPresent() != FALSE;
#endif
    }

    // Initialize Vectored Exception Handler for the Crash Guard (Zero lock overhead on hot path)
    static void Initialize() {
        static bool s_alreadyInit = false;
        if (s_alreadyInit) return;
        std::lock_guard<std::mutex> lock(Instance().m_mutex);
        if (!Instance().m_vehHandle) {
            char dbgEnv[8] = {0};
            if (GetEnvironmentVariableA("DREAM_DEBUG_PASSTHROUGH", dbgEnv, sizeof(dbgEnv)) > 0 && dbgEnv[0] == '1') {
                Instance().m_passThroughToDebugger.store(true);
            }
            Instance().m_vehHandle = AddVectoredExceptionHandler(1, VectoredCrashHandler);
            if (Instance().m_vehHandle) {
                DREAM_LOG_INFO("DreamCrashGuard: Vectored Exception Handler installed successfully");
            }
            s_alreadyInit = true;
        }
    }

    // Unregister VEH handler on shutdown
    static void Shutdown() {
        std::lock_guard<std::mutex> lock(Instance().m_mutex);
        if (Instance().m_vehHandle) {
            RemoveVectoredExceptionHandler(Instance().m_vehHandle);
            Instance().m_vehHandle = nullptr;
        }
    }

    static size_t HashModName(const char* s) noexcept {
        uint64_t h = 14695981039346656037ULL;
        while (s && *s) { h ^= static_cast<uint8_t>(*s++); h *= 1099511628211ULL; }
        return static_cast<size_t>(h & 63);
    }

    struct CircuitSlot {
        std::atomic<uint64_t> state{0}; // High 32 bits = Generation, Low 32 bits = FaultCount
    };

    static uint32_t GetCurrentGeneration(const char* modName) noexcept {
        if (!modName) return 0;
        uint64_t val = Instance().m_circuits[HashModName(modName)].state.load(std::memory_order_relaxed);
        return static_cast<uint32_t>(val >> 32);
    }

    // Check if mod circuit breaker is tripped (too many consecutive crashes) - Zero Lock, Zero Alloc
    static bool IsCircuitTripped(const char* modName) noexcept {
        if (!modName) return false;
        uint64_t val = Instance().m_circuits[HashModName(modName)].state.load(std::memory_order_relaxed);
        return static_cast<uint32_t>(val & 0xFFFFFFFFULL) >= Instance().m_circuitThreshold.load(std::memory_order_relaxed);
    }

    static uint32_t GetFaultCount(const char* modName) noexcept {
        if (!modName) return 0;
        uint64_t val = Instance().m_circuits[HashModName(modName)].state.load(std::memory_order_relaxed);
        return static_cast<uint32_t>(val & 0xFFFFFFFFULL);
    }

    static void SetCircuitThreshold(uint32_t threshold) noexcept {
        Instance().m_circuitThreshold.store(threshold > 0 ? threshold : 5, std::memory_order_relaxed);
    }

    static uint32_t GetCircuitThreshold() noexcept {
        return Instance().m_circuitThreshold.load(std::memory_order_relaxed);
    }

    static void RecordSuccess(const char* modName, uint32_t entryGen) noexcept {
        if (!modName) return;
        auto& atom = Instance().m_circuits[HashModName(modName)].state;
        uint64_t cur = atom.load(std::memory_order_relaxed);
        while (static_cast<uint32_t>(cur >> 32) == entryGen && (cur & 0xFFFFFFFFULL) != 0) {
            uint64_t next = (static_cast<uint64_t>(entryGen) << 32); // Reset faults to 0, keep entryGen
            if (atom.compare_exchange_weak(cur, next, std::memory_order_relaxed)) break;
        }
    }

    using CrashToastCallback = void (*)(const char* msg, float durationSec, uint32_t colorRgb);
    using LoopRecoveryCallback = bool (*)(PEXCEPTION_POINTERS pEx);

    static void SetToastCallback(CrashToastCallback cb) noexcept {
        Instance().m_toastCallback = cb;
    }

    static void SetLoopRecoveryCallback(LoopRecoveryCallback cb) noexcept {
        Instance().m_loopRecoveryCallback = cb;
    }

    static void TriggerToast(const char* msg, float durationSec, uint32_t colorRgb) noexcept {
        auto cb = Instance().m_toastCallback;
        if (cb && msg) {
            cb(msg, durationSec, colorRgb);
        }
    }

    static void RecordFault(const char* modName, uint32_t entryGen) noexcept {
        if (!modName) return;
        auto& atom = Instance().m_circuits[HashModName(modName)].state;
        uint64_t cur = atom.load(std::memory_order_relaxed);
        uint32_t thresh = Instance().m_circuitThreshold.load(std::memory_order_relaxed);
        while (static_cast<uint32_t>(cur >> 32) == entryGen) {
            uint32_t faults = static_cast<uint32_t>(cur & 0xFFFFFFFFULL) + 1;
            uint64_t next = (static_cast<uint64_t>(entryGen) << 32) | faults;
            if (atom.compare_exchange_weak(cur, next, std::memory_order_relaxed)) {
                if (faults == thresh) {
                    DREAM_LOG_ERROR("DreamCrashGuard: CIRCUIT BREAKER TRIPPED for mod '%s' (%u consecutive crashes). Mod callback disabled.", modName, thresh);
                    char toastMsg[128];
                    snprintf(toastMsg, sizeof(toastMsg), "[Circuit Tripped] %s Disabled (%u crashes)", modName, thresh);
                    TriggerToast(toastMsg, 5.0f, 0xFF2222);
                }
                break;
            }
        }
    }

    // Reset circuit breaker for a specific mod (e.g. on hot-reload) - Atomically increments generation & resets faults
    static void ResetCircuit(const char* modName) noexcept {
        if (!modName) return;
        auto& atom = Instance().m_circuits[HashModName(modName)].state;
        uint64_t cur = atom.load(std::memory_order_relaxed);
        uint64_t next;
        do {
            uint32_t nextGen = static_cast<uint32_t>(cur >> 32) + 1;
            next = (static_cast<uint64_t>(nextGen) << 32); // FaultCount = 0
        } while (!atom.compare_exchange_weak(cur, next, std::memory_order_release, std::memory_order_relaxed));
    }

    // Reset all circuit breakers
    static void ResetAllCircuits() noexcept {
        for (auto& slot : Instance().m_circuits) {
            uint64_t cur = slot.state.load(std::memory_order_relaxed);
            uint64_t next;
            do {
                uint32_t nextGen = static_cast<uint32_t>(cur >> 32) + 1;
                next = (static_cast<uint64_t>(nextGen) << 32);
            } while (!slot.state.compare_exchange_weak(cur, next, std::memory_order_release, std::memory_order_relaxed));
        }
    }

    // Execute mod callback inside an isolated sandbox shield
    template <typename Callback, typename Fallback>
    __attribute__((noinline)) static auto Execute(const char* modName, Callback&& callback, Fallback&& fallback, uintptr_t probeAddr = 0, size_t probeSz = 0)
        -> decltype(callback())
    {
        Initialize();

        using ReturnType = decltype(callback());

        if (IsCircuitTripped(modName)) {
            if constexpr (std::is_void_v<ReturnType>) {
                fallback();
                return;
            } else {
                return fallback();
            }
        }

        uint32_t entryGen = GetCurrentGeneration(modName);

        SandboxContext ctx;
        ctx.inSandbox = true;
        ctx.faultOccurred = false;
        ctx.entryGen = entryGen;
        ctx.modName = modName ? modName : "UnknownMod";
        ctx.probeAddress = probeAddr;
        ctx.probeSize = probeSz;
        ctx.isMemoryProbe = (probeAddr != 0) || (modName && (strncmp(modName, "MemoryRead", 10) == 0 ||
                                                             strncmp(modName, "MemoryWrite", 11) == 0));
        ctx.prevCtx = reinterpret_cast<SandboxContext*>(TlsGetValue(GetTlsIndex()));
        TlsSetValue(GetTlsIndex(), &ctx);

        // Capture caller context directly using Windows kernel API (Zero CRT longjmp)
        RtlCaptureContext(&ctx.savedCtx);
#if defined(__GNUC__) || defined(__clang__)
        asm volatile("" ::: "memory");
#else
        _ReadWriteBarrier();
#endif

        if (!ctx.faultOccurred) {
            // Normal execution path
            if constexpr (std::is_void_v<ReturnType>) {
                callback();
                ctx.inSandbox = false;
                TlsSetValue(GetTlsIndex(), ctx.prevCtx);
                RecordSuccess(ctx.modName, ctx.entryGen);
                return;
            } else {
                ReturnType result = callback();
                ctx.inSandbox = false;
                TlsSetValue(GetTlsIndex(), ctx.prevCtx);
                RecordSuccess(ctx.modName, ctx.entryGen);
                return result;
            }
        } else {
            // Execution arrived here on clean stack after NtContinue restored savedCtx!
#if defined(__AVX__) || defined(__AVX2__)
            _mm256_zeroupper();
#elif defined(__GNUC__) || defined(__clang__)
            __asm__ __volatile__("vzeroupper" ::: "memory");
#endif

            ctx.inSandbox = false;
            TlsSetValue(GetTlsIndex(), ctx.prevCtx);
            Instance().m_totalCrashesPrevented++;
            if (!ctx.isMemoryProbe) {
                RecordFault(ctx.modName, ctx.entryGen);
                // Emit comprehensive single-message diagnostic report on clean stack
                Instance().LogFaultReport(ctx);
            }

            if constexpr (std::is_void_v<ReturnType>) {
                fallback();
                return;
            } else {
                return fallback();
            }
        }
    }

    // Simple void execution overload
    template <typename Callback>
    __attribute__((noinline)) static bool ExecuteSafely(const char* modName, Callback&& callback, uintptr_t probeAddr = 0, size_t probeSz = 0) {
        bool succeeded = true;
        Execute(modName, [&]() {
            callback();
        }, [&]() {
            succeeded = false;
        }, probeAddr, probeSz);
        return succeeded;
    }

    size_t GetTotalCrashesPrevented() const {
        return m_totalCrashesPrevented;
    }

    typedef PVOID (NTAPI *pfnRtlPcToFileHeader)(PVOID PcValue, PVOID *BaseOfImage);

    static pfnRtlPcToFileHeader GetRtlPcToFileHeader() {
        static pfnRtlPcToFileHeader s_pfn = []() {
            HMODULE hNt = GetModuleHandleW(L"ntdll.dll");
            return hNt ? reinterpret_cast<pfnRtlPcToFileHeader>(GetProcAddress(hNt, "RtlPcToFileHeader")) : nullptr;
        }();
        return s_pfn;
    }

    // Fast lock-free module name resolution (avoids loader data locks)
    static std::string ExtractCleanModuleName(uintptr_t pc, uintptr_t& outImageBase, uintptr_t& outRva) {
        PVOID base = nullptr;
        auto pfn = GetRtlPcToFileHeader();
        if (pfn && pfn(reinterpret_cast<PVOID>(pc), &base) && base) {
            outImageBase = reinterpret_cast<uintptr_t>(base);
            outRva = pc - outImageBase;

            char fullPath[MAX_PATH] = {0};
            if (GetModuleFileNameA(reinterpret_cast<HMODULE>(base), fullPath, MAX_PATH)) {
                std::string full(fullPath);
                size_t slash = full.find_last_of("\\/");
                std::string filename = (slash != std::string::npos) ? full.substr(slash + 1) : full;

                // Strip shadow-copy prefix (e.g. shadow_1793347602_DreamAoEHarvest.dll -> DreamAoEHarvest.dll)
                if (filename.rfind("shadow_", 0) == 0) {
                    size_t secondUnderscore = filename.find('_', 7);
                    if (secondUnderscore != std::string::npos && secondUnderscore + 1 < filename.size()) {
                        filename = filename.substr(secondUnderscore + 1);
                    }
                }
                return filename;
            }
        }
        outImageBase = 0;
        outRva = pc;
        return "UnknownModule";
    }

private:
    DreamCrashGuard() = default;
    ~DreamCrashGuard() { Shutdown(); }

    static bool IsFatalHardwareException(DWORD code) {
        switch (code) {
            case EXCEPTION_ACCESS_VIOLATION:        // 0xC0000005
            case EXCEPTION_IN_PAGE_ERROR:           // 0xC0000006
            case EXCEPTION_ILLEGAL_INSTRUCTION:     // 0xC000001D
            case EXCEPTION_INT_DIVIDE_BY_ZERO:      // 0xC0000094
            case EXCEPTION_PRIV_INSTRUCTION:        // 0xC0000096
            case EXCEPTION_DATATYPE_MISALIGNMENT:   // 0x80000002
                // Stack Overflow 0xC00000FD removed as recommended by Dev 1 until recovery tests exist
                return true;
            default:
                return false;
        }
    }

    static LONG WINAPI VectoredCrashHandler(PEXCEPTION_POINTERS pExceptionInfo) {
        if (!pExceptionInfo || !pExceptionInfo->ExceptionRecord || !pExceptionInfo->ContextRecord) {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        DWORD code = pExceptionInfo->ExceptionRecord->ExceptionCode;

        // Whitelist only fatal hardware exception codes
        // Never catch MSVC C++ exceptions (0xE06D7363) or non-hardware exceptions!
        if (!IsFatalHardwareException(code)) {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        auto* ctx = reinterpret_cast<SandboxContext*>(TlsGetValue(GetTlsIndex()));

        // Check if crash happened inside an active mod sandbox
        if (ctx && ctx->inSandbox) {
            // Memory Probe Boundary Guard (Dev 2 Kernel Hardening):
            // Whitelist ONLY genuine memory access violations (0xC0000005) and in-page errors (0xC0000006).
            // Pass through STATUS_GUARD_PAGE_VIOLATION (0x80000001) and STATUS_STACK_OVERFLOW (0xC00000FD)
            // to avoid stripping PAGE_GUARD from PTEs!
            if (ctx->isMemoryProbe) {
                if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_IN_PAGE_ERROR) {
                    return EXCEPTION_CONTINUE_SEARCH;
                }

                // Verify fault address falls within probe bounds
                if (ctx->probeAddress != 0) {
                    const ULONG_PTR faultAddr = (code == EXCEPTION_ACCESS_VIOLATION && pExceptionInfo->ExceptionRecord->NumberParameters >= 2)
                        ? pExceptionInfo->ExceptionRecord->ExceptionInformation[1] : 0;
                    if (faultAddr < ctx->probeAddress || (ctx->probeSize > 0 && faultAddr >= (ctx->probeAddress + ctx->probeSize))) {
                        return EXCEPTION_CONTINUE_SEARCH;
                    }
                }
            }

            // Optional Debugger Pass-Through (Dev 2 Architecture)
            // If developer explicitly enabled pass-through AND debugger is attached AND not a memory probe:
            // Allow debugger to catch the crash directly!
            if (Instance().m_passThroughToDebugger.load(std::memory_order_relaxed) &&
                IsHardwareDebuggerAttached() &&
                !ctx->isMemoryProbe)
            {
                ctx->inSandbox = false;
                return EXCEPTION_CONTINUE_SEARCH;
            }

            // Disarm sandbox immediately
            ctx->inSandbox = false;
            ctx->faultOccurred = true;

            auto* c = pExceptionInfo->ContextRecord;
            auto* r = pExceptionInfo->ExceptionRecord;

            // Capture CPU Registers inside VEH (Zero allocation, zero locks)
            ctx->faultRip = c->Rip;
            ctx->faultRsp = c->Rsp;
            ctx->faultRax = c->Rax;
            ctx->faultRbx = c->Rbx;
            ctx->faultRcx = c->Rcx;
            ctx->faultRdx = c->Rdx;
            ctx->faultRsi = c->Rsi;
            ctx->faultRdi = c->Rdi;
            ctx->faultRbp = c->Rbp;
            ctx->faultR8  = c->R8;
            ctx->faultR9  = c->R9;
            ctx->faultR10 = c->R10;
            ctx->faultR11 = c->R11;
            ctx->faultCode = code;

            if (code == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2) {
                ctx->faultAccessType = static_cast<int>(r->ExceptionInformation[0]);
                ctx->faultTargetMemory = static_cast<uintptr_t>(r->ExceptionInformation[1]);
            }

            // Safe read up to 15 opcode bytes at RIP if not an execute fault and not a bad RIP (Zero syscalls in VEH)
            const ULONG_PTR faultType = (code == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 1) ? r->ExceptionInformation[0] : 0;
            const ULONG_PTR faultAddr = (code == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2) ? r->ExceptionInformation[1] : 0;
            const bool isBadRip = (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR) &&
                                  (faultType == 8 || faultAddr == c->Rip || c->Rip < 0x10000 || c->Rip > 0x00007FFFFFFFFFFFULL);

            ctx->faultOpcodesLen = 0;
            if (!isBadRip) {
                std::memcpy(ctx->faultOpcodes, reinterpret_cast<const void*>(c->Rip), 15);
                ctx->faultOpcodesLen = 15;
            }

            // Safe peek top 4 QWORDs on stack [rsp]
            ctx->faultStackWordsCount = 0;
            const uintptr_t lo = DreamGetTebStackLimit(), hi = DreamGetTebStackBase();
            if (IsSafeStackQword(c->Rsp, lo, hi) && (c->Rsp <= hi - sizeof(uint64_t) * 4)) {
                const auto* sp = reinterpret_cast<const uint64_t*>(c->Rsp);
                for (int i = 0; i < 4; ++i) ctx->faultStackWords[i] = sp[i];
                ctx->faultStackWordsCount = 4;
            }

            // Capture backtrace in VEH from exception ContextRecord (Zero allocation, TEB-bounded)
            ctx->backtraceCount = CaptureFrames(*c, ctx->backtrace, 12);

            // Direct context restoration via RtlCaptureContext / NtContinue
            // 1. Preserve active Hardware Breakpoints (DreamVeh DR0..DR3, DR7)
            const DWORD64 savedDr0 = c->Dr0, savedDr1 = c->Dr1;
            const DWORD64 savedDr2 = c->Dr2, savedDr3 = c->Dr3;
            const DWORD64 savedDr7 = c->Dr7;

            // 2. Restore captured sandbox landing pad context
            *c = ctx->savedCtx;

            // 3. Re-apply preserved debug registers and clear DR6 status
            c->Dr0 = savedDr0; c->Dr1 = savedDr1;
            c->Dr2 = savedDr2; c->Dr3 = savedDr3;
            c->Dr6 = 0;        c->Dr7 = savedDr7;
            c->ContextFlags |= CONTEXT_DEBUG_REGISTERS;

            // 4. Sanitize MXCSR sticky exception flags (bits 0..5) in BOTH x64 CONTEXT slots
            c->MxCsr         &= ~0x3FU;
            c->FltSave.MxCsr &= ~0x3FU;

            return EXCEPTION_CONTINUE_EXECUTION;
        }

        // 5. Try known engine loop recovery (Dev 2 Architecture)
        // If crash happened at a known engine batch loop (e.g. 0xA1394 interaction_attempt),
        // safely advance loop latch without leaking locks or dropping frames/server
        auto loopCb = Instance().m_loopRecoveryCallback;
        if (loopCb && loopCb(pExceptionInfo)) {
            return EXCEPTION_CONTINUE_EXECUTION;
        }

        return EXCEPTION_CONTINUE_SEARCH;
    }

    void LogFaultReport(const SandboxContext& ctx) {
        uintptr_t imageBase = 0;
        uintptr_t rva = 0;
        std::string modFileName = ExtractCleanModuleName(ctx.faultRip, imageBase, rva);

        // Deduplication hash by (moduleName, rva)
        uint64_t siteHash = HashModName(modFileName.c_str()) ^ (static_cast<uint64_t>(rva) << 16);
        uint64_t nowMs = GetTickCount64();

        uint64_t siteCount = 0;
        bool isFirst = false;
        bool shouldLogFull = false;
        {
            std::lock_guard<std::mutex> lock(m_siteMutex);
            auto& rec = m_faultSites[siteHash];
            rec.count++;
            siteCount = rec.count;
            if (siteCount == 1) {
                isFirst = true;
                shouldLogFull = true;
                rec.lastLogTimeMs = nowMs;
            } else if (nowMs - rec.lastLogTimeMs >= 5000) {
                shouldLogFull = true;
                rec.lastLogTimeMs = nowMs;
            }
        }

        if (!shouldLogFull) {
            // Throttled summary (Dev 1 recommendation: print xN instead of flooding log)
            DREAM_LOG_WARN("DreamCrashGuard: Suppressed crash in mod '%s' at %s + 0x%llX (repeated %llu times)",
                           ctx.modName, modFileName.c_str(), static_cast<unsigned long long>(rva),
                           static_cast<unsigned long long>(siteCount));
            return;
        }

        // Build single atomic report string: prevents multi-thread log interleaving
        uint32_t timeDateStamp = 0;
        uint64_t preferredImageBase = 0;
        if (imageBase) {
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(imageBase);
            if (dos->e_magic == IMAGE_DOS_SIGNATURE && dos->e_lfanew > 0 && dos->e_lfanew < 0x1000) {
                const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
                    reinterpret_cast<const uint8_t*>(imageBase) + dos->e_lfanew);
                if (nt->Signature == IMAGE_NT_SIGNATURE) {
                    timeDateStamp = nt->FileHeader.TimeDateStamp;
                    preferredImageBase = nt->OptionalHeader.ImageBase;
                }
            }
        }

        std::ostringstream ss;
        ss << "\n======================== [DREAM CRASH GUARD DIAGNOSTIC] ========================\n";
        ss << "  CRASH INTERCEPTED in mod: '" << ctx.modName << "' (Fault Site: " << modFileName << " + 0x" << std::hex << rva << std::dec << ")\n";
        ss << "  * Fault Location: " << modFileName << " + 0x" << std::hex << rva
           << " (Runtime Base: 0x" << imageBase;
        if (preferredImageBase != 0) {
            ss << ", Preferred Base: 0x" << preferredImageBase;
        }
        if (timeDateStamp != 0) {
            ss << ", Timestamp: 0x" << timeDateStamp;
        }
        ss << ", RIP: 0x" << ctx.faultRip << ")\n" << std::dec;

        uint64_t addr2lineTarget = (preferredImageBase != 0) ? (preferredImageBase + rva) : (imageBase + rva);
        ss << "  * Symbol Hint   : Run 'addr2line -f -C -e " << modFileName << " 0x" << std::hex << addr2lineTarget << "' to view source line\n" << std::dec;
        ss << "  * Registers     : RAX=0x" << std::hex << ctx.faultRax << "  RBX=0x" << ctx.faultRbx
           << "  RCX=0x" << ctx.faultRcx << "  RDX=0x" << ctx.faultRdx << "\n"
           << "                    RSI=0x" << ctx.faultRsi << "  RDI=0x" << ctx.faultRdi
           << "  RBP=0x" << ctx.faultRbp << "  RSP=0x" << ctx.faultRsp << "\n"
           << "                    R8 =0x" << ctx.faultR8  << "  R9 =0x" << ctx.faultR9
           << "  R10=0x" << ctx.faultR10 << "  R11=0x" << ctx.faultR11 << "\n" << std::dec;

        // Top of stack words peek
        if (ctx.faultStackWordsCount > 0) {
            ss << "  * Stack at [RSP]: ";
            for (uint8_t i = 0; i < ctx.faultStackWordsCount; ++i) {
                ss << "0x" << std::hex << ctx.faultStackWords[i] << " ";
            }
            ss << "\n" << std::dec;
        }

        // Disassemble / decode opcode bytes at RIP using HDE64
        if (ctx.faultOpcodesLen > 0) {
            hde64s hs{};
            unsigned int decodedLen = hde64_disasm(ctx.faultOpcodes, &hs);
            const char* mnemonic = DreamGetOpcodeMnemonic(hs);

            uint8_t printBytes = (decodedLen > 0 && decodedLen <= ctx.faultOpcodesLen) ? static_cast<uint8_t>(decodedLen) : ctx.faultOpcodesLen;
            if (printBytes > 10) printBytes = 10;

            ss << "  * Code at RIP   : ";
            for (uint8_t i = 0; i < printBytes; ++i) {
                char hx[4];
                snprintf(hx, sizeof(hx), "%02X ", ctx.faultOpcodes[i]);
                ss << hx;
            }
            ss << "(";
            if (decodedLen > 0 && !(hs.flags & F_ERROR)) {
                ss << decodedLen << " bytes: " << mnemonic;
            } else {
                ss << "raw opcode";
            }
            ss << ")\n";
        }

        // Access Violation / Exception root cause diagnosis
        if (ctx.faultCode == EXCEPTION_ACCESS_VIOLATION) {
            const char* opStr = (ctx.faultAccessType == 0) ? "READ" :
                                (ctx.faultAccessType == 1) ? "WRITE" :
                                (ctx.faultAccessType == 8) ? "EXECUTE" : "ACCESS";

            uint64_t low32 = ctx.faultTargetMemory & 0xFFFFFFFFULL;
            if (low32 == 0xCDCDCDCD || ctx.faultTargetMemory == 0xCDCDCDCDCDCDCDCDULL) {
                ss << "  * Root Cause    : UNINITIALIZED HEAP MEMORY (0xCDCDCDCD)! Pointer was allocated from heap but never initialized.\n";
            } else if (low32 == 0xDDDDDDDD || ctx.faultTargetMemory == 0xDDDDDDDDDDDDDDDDULL) {
                ss << "  * Root Cause    : USE-AFTER-FREE (0xDDDDDDDD)! Mod attempted to access heap memory that was already freed/deleted.\n";
            } else if (low32 == 0xFEEEFEEE || ctx.faultTargetMemory == 0xFEEEFEEEFEEEFEEEULL) {
                ss << "  * Root Cause    : USE-AFTER-FREE (0xFEEEFEEE)! Target memory belongs to a previously freed heap block.\n";
            } else if (low32 == 0xCCCCCCCC || ctx.faultTargetMemory == 0xCCCCCCCCCCCCCCCCULL) {
                ss << "  * Root Cause    : UNINITIALIZED STACK VARIABLE (0xCCCCCCCC)! Local pointer variable on stack was never initialized.\n";
            } else if (low32 == 0xBAADF00D || ctx.faultTargetMemory == 0xBAADF00DBAADF00DULL) {
                ss << "  * Root Cause    : UNINITIALIZED ALLOC MEMORY (0xBAADF00D)! Dereferenced uninitialized LocalAlloc buffer.\n";
            } else if (ctx.faultTargetMemory == 0xFFFFFFFFFFFFFFFFULL) {
                ss << "  * Root Cause    : General Protection Fault (#GP)! Likely unaligned 16-byte SSE (movaps/movdqa) or non-canonical 64-bit pointer.\n";
            } else if (ctx.faultTargetMemory < 0x10000) {
                ss << "  * Root Cause    : NULL POINTER DEREFERENCE! Attempted to " << opStr << " address 0x" << std::hex << ctx.faultTargetMemory << std::dec
                   << " (Offset +0x" << std::hex << ctx.faultTargetMemory << "). Check if base object/entity is NULL!\n" << std::dec;
            } else if (ctx.faultAccessType == 8 && ctx.faultRip == ctx.faultTargetMemory) {
                ss << "  * Root Cause    : EXECUTE ACCESS VIOLATION! Attempted to call an invalid or corrupted function pointer / VTable entry.\n";
                if (ctx.faultStackWordsCount > 0) {
                    uintptr_t callerBase = 0, callerRva = 0;
                    std::string callerMod = ExtractCleanModuleName(ctx.faultStackWords[0], callerBase, callerRva);
                    ss << "  * Caller Site   : " << callerMod << " + 0x" << std::hex << callerRva
                       << " (Caller return address read from [RSP]: 0x" << ctx.faultStackWords[0] << ")\n" << std::dec;
                }
            } else {
                MEMORY_BASIC_INFORMATION mbi{};
                if (VirtualQuery(reinterpret_cast<LPCVOID>(ctx.faultTargetMemory), &mbi, sizeof(mbi))) {
                    if (mbi.State == MEM_FREE) {
                        ss << "  * Root Cause    : USE-AFTER-FREE or UNMAPPED MEMORY! Target address 0x" << std::hex << ctx.faultTargetMemory << " is MEM_FREE.\n" << std::dec;
                    } else if (ctx.faultAccessType == 1 && (mbi.Protect & (PAGE_READONLY | PAGE_EXECUTE_READ))) {
                        ss << "  * Root Cause    : WRITE TO READ-ONLY MEMORY! Attempted to write to write-protected memory (.rdata / .text page).\n";
                    } else {
                        ss << "  * Root Cause    : Access Violation (" << opStr << " on 0x" << std::hex << ctx.faultTargetMemory << "). Protect: 0x" << mbi.Protect << "\n" << std::dec;
                    }
                } else {
                    ss << "  * Root Cause    : Access Violation attempting to " << opStr << " address 0x" << std::hex << ctx.faultTargetMemory << "\n" << std::dec;
                }
            }
        } else {
            ss << "  * Exception Code: 0x" << std::hex << ctx.faultCode << "\n" << std::dec;
        }

        // Captured Stack Backtrace & Mod Call Site Identification
        uintptr_t modCallSiteRva = 0;
        uintptr_t modCallSiteImageBase = 0;
        std::string modCallSiteFileName;
        int modCallSiteFrameIndex = -1;

        if (ctx.backtraceCount > 0) {
            ss << "  * Callstack Trace (" << static_cast<int>(ctx.backtraceCount) << " frames captured inside VEH):\n";
            for (uint8_t i = 0; i < ctx.backtraceCount; ++i) {
                uintptr_t frameBase = 0;
                uintptr_t frameRva = 0;
                std::string frameMod = ExtractCleanModuleName(ctx.backtrace[i], frameBase, frameRva);
                ss << "     #" << std::setw(2) << std::setfill('0') << static_cast<int>(i) << ": "
                   << frameMod << " + 0x" << std::hex << frameRva
                   << " (0x" << ctx.backtrace[i] << ")\n" << std::dec;

                // If fault occurred in game exe or system DLL, identify first mod frame in callstack
                if (modCallSiteFrameIndex < 0 && i > 0) {
                    std::string lowerMod = frameMod;
                    for (char& c : lowerMod) c = static_cast<char>(tolower(c));
                    if (lowerMod != "enshrouded.exe" && lowerMod != "test_dreamkurumi.exe" &&
                        lowerMod.find("ntdll") == std::string::npos &&
                        lowerMod.find("kernel") == std::string::npos &&
                        lowerMod.find("dreamkurumiht") == std::string::npos &&
                        lowerMod.find(".dll") != std::string::npos) {
                        modCallSiteFrameIndex = i;
                        modCallSiteRva = frameRva;
                        modCallSiteImageBase = frameBase;
                        modCallSiteFileName = frameMod;
                    }
                }
            }
        }

        if (modCallSiteFrameIndex >= 0) {
            uint64_t lineTarget = modCallSiteImageBase + modCallSiteRva;
            ss << "  * Mod Call Site : " << modCallSiteFileName << " + 0x" << std::hex << modCallSiteRva
               << " (Callstack Frame #" << modCallSiteFrameIndex << ")\n" << std::dec;
            ss << "  * Mod Line Hint : Run 'addr2line -f -C -e " << modCallSiteFileName << " 0x" << std::hex << lineTarget
               << "' (This line in mod code initiated the failing call!)\n" << std::dec;
        }

        ss << "  DreamCrashGuard: Safely suppressed crash and resumed game execution.\n";
        ss << "================================================================================\n";

        // In-game toast alert (Zero GPU overhead, auto-disabled on dedicated server)
        if (isFirst) {
            char toastMsg[128];
            snprintf(toastMsg, sizeof(toastMsg), "[Crash Intercepted] %s (See Log)", ctx.modName);
            TriggerToast(toastMsg, 3.5f, 0xFF4444);
        }

        // Single atomic log call: prevents multi-thread log interleaving
        std::string report = ss.str();
        DREAM_LOG_ERROR("%s", report.c_str());
    }

    PVOID m_vehHandle{nullptr};
    std::mutex m_mutex;
    std::mutex m_siteMutex;
    CrashToastCallback m_toastCallback{nullptr};
    LoopRecoveryCallback m_loopRecoveryCallback{nullptr};
    std::unordered_map<uint64_t, FaultSiteRecord> m_faultSites;
    CircuitSlot m_circuits[64]{};
    std::atomic<uint32_t> m_circuitThreshold{5};
    std::atomic<bool> m_passThroughToDebugger{false};
    std::atomic<size_t> m_totalCrashesPrevented{0};
};

} // namespace dreamkurumi

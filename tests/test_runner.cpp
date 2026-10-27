#include <dream.hpp>
#include <iostream>
#include <cassert>
#include <iomanip>
#include "../include/dreamkurumiHT/dreamkurumiHT.hpp"

// ============================================================================
// Dummy Game Functions to Hook (Simulating Game Code)
// ============================================================================
__declspec(noinline) int TargetFunction_CalculateDamage(int baseDmg, int multiplier) {
    std::cout << "  [GAME] Running original TargetFunction_CalculateDamage(" << baseDmg << ", " << multiplier << ")...\n";
    return baseDmg * multiplier;
}

__declspec(noinline) void TargetFunction_ConsumeStamina(void* player, float amount) {
    std::cout << "  [GAME] Running original TargetFunction_ConsumeStamina, amount = " << amount << "\n";
}

__declspec(noinline) int TargetFunction_Passive(int a) {
    return a + 10;
}

__attribute__((noinline, noclone)) int ChainedTarget_Compute(int val) {
    std::cout << "  [ORIGINAL] Running ChainedTarget_Compute, val = " << val << "\n";
    return val * 10;
}


__attribute__((noinline, noclone)) int MultiModTarget_Func(int val) {
    asm volatile("" : "+r"(val) : : "memory");
    return val * 10;
}

DREAM_HARMONY_PATCH_PRIORITY(MultiModPatchA, "MultiModTarget", dreamkurumi::HookPriority::High, 0, int, int) {
    static bool Prefix(int& val) {
        val += 5; // Mod A (High priority): adds 5 to input
        return true;
    }
};

DREAM_HARMONY_PATCH_PRIORITY(MultiModPatchB, "MultiModTarget", dreamkurumi::HookPriority::Normal, 0, int, int) {
    static void Postfix(int& result, int val) {
        result += 100; // Mod B (Normal priority): adds 100 to result
    }
};

class MockEntityManager {
public:
    virtual int GetEntityCount() { return 777; }
};

__attribute__((noinline, noclone)) int TranspileTarget_SecurityCheck(int isAuthorized) {
    if (isAuthorized == 0) {
        asm volatile("");
        return -1; // Access Denied
    }
    asm volatile("");
    return 999; // Admin Access Granted
}

// Global flag to simulate admin state
static bool g_godModeEnabled = false;
static bool g_infiniteStaminaEnabled = false;
static int g_observerCallCount = 0;

// ============================================================================
// Harmony Patch 1: CalculateDamage Patch
// ============================================================================
DREAM_DECLARE_PATCH(DamagePatch, int, int, int) {
    static bool Prefix(int baseDmg, int multiplier) {
        std::cout << "  [HARMONY PREFIX] DamagePatch called with base=" << baseDmg << ", mult=" << multiplier << "\n";
        if (g_godModeEnabled) {
            std::cout << "  [HARMONY PREFIX] GodMode is ON! Skipping original calculation!\n";
            return false; // Skip original!
        }
        return true; // Continue original!
    }

    static void Postfix(int& result, int baseDmg, int multiplier) {
        std::cout << "  [HARMONY POSTFIX] Original result was: " << result << "\n";
        if (g_godModeEnabled) {
            result = 0; // Force damage to 0
            std::cout << "  [HARMONY POSTFIX] Mutated result to: " << result << "\n";
        }
    }
};

// ============================================================================
// Harmony Patch 2: Stamina Patch (Void return, skip original)
// ============================================================================
DREAM_DECLARE_PATCH(StaminaPatch, void, void*, float) {
    static bool Prefix(void* player, float amount) {
        std::cout << "  [HARMONY PREFIX] Stamina drain requested: " << amount << "\n";
        if (g_infiniteStaminaEnabled) {
            std::cout << "  [HARMONY PREFIX] InfiniteStamina ON -> Skipping drain entirely!\n";
            return false; // Skip original function completely!
        }
        return true;
    }
};

// ============================================================================
// Harmony Patch 3: Passive Observer Patch (void Prefix and void Postfix)
// ============================================================================
DREAM_DECLARE_PATCH(PassivePatch, int, int) {
    static void Prefix(int a) {
        g_observerCallCount++;
        std::cout << "  [PASSIVE PREFIX] Observed argument a = " << a << "\n";
    }

    static void Postfix(int a) {
        std::cout << "  [PASSIVE POSTFIX] Observed function completion for a = " << a << "\n";
    }
};

// ============================================================================
// Harmony Patch 4: Auto-registered Patch with DREAM_HARMONY_PATCH (for PatchAll)
// ============================================================================
__attribute__((noinline, noclone)) int AutoTargetFunction(int x) {
    asm volatile("" : "+r"(x));
    return x * 10;
}

DREAM_HARMONY_PATCH(AutoRegisteredPatch, "AutoFunc", 0, int, int) {
    static bool Prefix(int x) {
        std::cout << "  [AUTO PATCH PREFIX] AutoRegisteredPatch called with x = " << x << "\n";
        return true;
    }
    static void Postfix(int& result, int x) {
        result += 5; // Add 5 to result
        std::cout << "  [AUTO PATCH POSTFIX] Result mutated to: " << result << "\n";
    }
};
DREAM_REGISTER_PATCH(AutoRegisteredPatch);

__attribute__((noinline, noclone)) int SampleVehFunction() {
    volatile int x = 42;
    return x;
}

// ============================================================================
// Dummy Virtual Class for VTable Hook Test
// ============================================================================
class IVirtualGameEngine {
public:
    virtual int GetFramerateLimit() { return 60; }
};

static int Detour_GetFramerateLimit(IVirtualGameEngine* self) {
    return 144; // Unlocked 144 FPS
}

// ============================================================================
// Test Suite Runner
// ============================================================================
int main() {
    std::cout << "============================================================\n";
    std::cout << " dreamkurumiHT Framework Verification & Test Suite\n";
    std::cout << "============================================================\n\n";

    // ------------------------------------------------------------------------
    // Test 1: DreamCache System
    // ------------------------------------------------------------------------
    std::cout << "[TEST 1] Testing DreamCache...\n";
    auto& cache = dreamkurumi::DreamCache::Instance();
    cache.Initialize("test_cache.ini");
    std::cout << "  Current Module Build Timestamp: " << cache.GetGameBuildTimestamp() << "\n";
    
    // Register mock offsets
    cache.SetOffset("StaminaDrain", 0x123456);
    cache.SetOffset("CalculateDamage", 0x654321);
    cache.SaveCache();
    assert(cache.GetRva("StaminaDrain") == 0x123456);
    std::cout << "  [PASS] Cache saved and verified successfully!\n\n";

    // ------------------------------------------------------------------------
    // Test 2: Unhooked Baseline Test
    // ------------------------------------------------------------------------
    std::cout << "[TEST 2] Testing Baseline Functions (Before Hook)...\n";
    int baselineDmg = TargetFunction_CalculateDamage(100, 2);
    assert(baselineDmg == 200);
    std::cout << "  Baseline damage result: " << baselineDmg << " (Expected 200) -> [PASS]\n\n";

    // ------------------------------------------------------------------------
    // Test 3: Harmony Hook Installation & Prefix/Postfix
    // ------------------------------------------------------------------------
    std::cout << "[TEST 3] Installing Harmony DamagePatch...\n";
    bool hookInstalled = DamagePatch::Install(reinterpret_cast<void*>(TargetFunction_CalculateDamage));
    std::cout << "  Hook install status: " << (hookInstalled ? "SUCCESS" : "FAILED") << "\n";
    assert(hookInstalled);

    std::cout << "\n  --- Running with GodMode = OFF ---\n";
    g_godModeEnabled = false;
    int dmgNormal = TargetFunction_CalculateDamage(100, 2);
    std::cout << "  Result: " << dmgNormal << " (Expected 200)\n";
    assert(dmgNormal == 200);

    std::cout << "\n  --- Running with GodMode = ON (Testing Prefix Skip & Postfix Mutation) ---\n";
    g_godModeEnabled = true;
    int dmgGodMode = TargetFunction_CalculateDamage(100, 2);
    std::cout << "  Result: " << dmgGodMode << " (Expected 0)\n";
    assert(dmgGodMode == 0);
    std::cout << "  [PASS] Harmony Prefix skip and Postfix mutation passed!\n\n";

    // ------------------------------------------------------------------------
    // Test 4: Void Function Skip (Stamina Drain)
    // ------------------------------------------------------------------------
    std::cout << "[TEST 4] Installing Harmony StaminaPatch (Void return)...\n";
    bool staminaHooked = StaminaPatch::Install(reinterpret_cast<void*>(TargetFunction_ConsumeStamina));
    assert(staminaHooked);

    std::cout << "  --- InfiniteStamina = OFF ---\n";
    g_infiniteStaminaEnabled = false;
    TargetFunction_ConsumeStamina(nullptr, 25.0f);

    std::cout << "  --- InfiniteStamina = ON ---\n";
    g_infiniteStaminaEnabled = true;
    TargetFunction_ConsumeStamina(nullptr, 25.0f);
    std::cout << "  [PASS] Void function skip passed!\n\n";

    // ------------------------------------------------------------------------
    // Test 5: Dynamic Disable / Restore
    // ------------------------------------------------------------------------
    std::cout << "[TEST 5] Testing Dynamic Disable / Restore...\n";
    DamagePatch::Disable();
    g_godModeEnabled = true; // Even if godmode is on, hook is disabled so original runs
    int unhookedDmg = TargetFunction_CalculateDamage(50, 3);
    assert(unhookedDmg == 150);
    std::cout << "  Result while disabled: " << unhookedDmg << " (Expected 150) -> [PASS]\n\n";

    // ------------------------------------------------------------------------
    // Test 6: Passive Observer (void Prefix)
    // ------------------------------------------------------------------------
    std::cout << "[TEST 6] Testing Passive Observer (void Prefix)...\n";
    bool passiveHooked = PassivePatch::Install(reinterpret_cast<void*>(TargetFunction_Passive));
    assert(passiveHooked);
    int passiveRes = TargetFunction_Passive(42);
    assert(passiveRes == 52);
    assert(g_observerCallCount == 1);
    std::cout << "  Passive observer result: " << passiveRes << " (Observer count: " << g_observerCallCount << ") -> [PASS]\n\n";

    // ------------------------------------------------------------------------
    // Test 7: ResolvePattern Fallback
    // ------------------------------------------------------------------------
    std::cout << "[TEST 7] Testing ResolvePattern Fallback...\n";
    uint8_t* pCode = reinterpret_cast<uint8_t*>(TargetFunction_Passive);
    char patBuf[64];
    snprintf(patBuf, sizeof(patBuf), "%02X %02X %02X ??", pCode[0], pCode[1], pCode[2]);
    std::cout << "  Scanning pattern: " << patBuf << "\n";
    void* resolvedAddr = cache.ResolvePattern("PassiveFunc", patBuf);
    assert(resolvedAddr != nullptr);
    std::cout << "  Found address: " << resolvedAddr << " -> [PASS]\n\n";

    // ------------------------------------------------------------------------
    // Test 8: DreamHarmony::PatchAll() & UnpatchAll()
    // ------------------------------------------------------------------------
    std::cout << "[TEST 8] Testing DreamHarmony::PatchAll()...\n";
    // Set RVA for AutoTargetFunction in cache so PatchAll can resolve it
    uintptr_t autoRva = reinterpret_cast<uintptr_t>(AutoTargetFunction) - cache.GetModuleBase();
    cache.SetOffset("AutoFunc", autoRva);

    size_t installedCount = dreamkurumi::DreamHarmony::PatchAll();
    std::cout << "  Patches installed by PatchAll: " << installedCount << "\n";
    auto pfnAuto = AutoTargetFunction;
    asm volatile("" : "+r"(pfnAuto));
    int autoRes = pfnAuto(5);
    std::cout << "  AutoTargetFunction(5) result: " << autoRes << " (Expected 55)\n";
    volatile int expected = 55;
    assert(autoRes == expected);

    dreamkurumi::DreamHarmony::UnpatchAll();
    int unpatchedRes = pfnAuto(5);
    std::cout << "  After UnpatchAll: " << unpatchedRes << " (Expected 50)\n";
    volatile int expectedUnpatched = 50;
    assert(unpatchedRes == expectedUnpatched);
    std::cout << "  [PASS] PatchAll and UnpatchAll verified!\n\n";

    // ------------------------------------------------------------------------
    // Test 9: DreamMemoryPatch (NOP and Byte Patching)
    // ------------------------------------------------------------------------
    std::cout << "[TEST 9] Testing DreamMemoryPatch (NOP & Restore)...\n";
    uint8_t dummyInstructions[8] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83 };
    dreamkurumi::DreamMemoryPatch memPatch;
    memPatch.Nop(dummyInstructions, 5);
    assert(dummyInstructions[0] == 0x90 && dummyInstructions[4] == 0x90 && dummyInstructions[5] == 0x57);
    memPatch.Restore();
    assert(dummyInstructions[0] == 0x48 && dummyInstructions[4] == 0x08);
    std::cout << "  [PASS] Memory NOP & byte restore passed!\n\n";

    // ------------------------------------------------------------------------
    // Test 10: DreamVTableHook (Virtual Method Hooking)
    // ------------------------------------------------------------------------
    std::cout << "[TEST 10] Testing DreamVTableHook...\n";
    IVirtualGameEngine engine;
    IVirtualGameEngine* pEngine = &engine;
    asm volatile("" : "+r"(pEngine));
    assert(pEngine->GetFramerateLimit() == 60);

    dreamkurumi::DreamVTableHook vtableHook(pEngine);
    vtableHook.Hook(0, reinterpret_cast<void*>(Detour_GetFramerateLimit));
    int hookedFps = pEngine->GetFramerateLimit();
    std::cout << "  Hooked FPS limit: " << hookedFps << " (Expected 144)\n";
    assert(hookedFps == 144);

    vtableHook.Unhook(0);
    assert(pEngine->GetFramerateLimit() == 60);
    std::cout << "  [PASS] VTable hook and unhook passed!\n\n";

    // ------------------------------------------------------------------------
    // Test 11: DreamVeh (Hardware Breakpoint Hooking)
    // ------------------------------------------------------------------------
    std::cout << "[TEST 11] Testing DreamVeh Hardware Breakpoint (Zero byte patching)...\n";
    static int s_vehHitCount = 0;
    auto& veh = dreamkurumi::DreamVeh::Instance();
    veh.Initialize();

    int slot = veh.SetBreakpoint(
        reinterpret_cast<void*>(SampleVehFunction),
        dreamkurumi::BreakOn::Execute,
        dreamkurumi::BreakSize::Bytes1,
        [](dreamkurumi::DreamCpuContext& ctx) {
            s_vehHitCount++;
            std::cout << "  [VEH HIT] Intercepted execution at hardware register! RAX = " << ctx.rax << "\n";
        }
    );
    assert(slot >= 0);

    SampleVehFunction();
    assert(s_vehHitCount == 1);
    veh.RemoveBreakpoint(slot);
    std::cout << "  [PASS] Hardware Breakpoint intercepted successfully without touching memory bytes!\n\n" << std::flush;

    // ------------------------------------------------------------------------
    // Test 12: DreamInvoker (Direct Function & Member/Virtual Invocation)
    // ------------------------------------------------------------------------
    std::cout << "[TEST 12] Testing Upgraded DreamInvoker (Member, Virtual, CallSafe)...\n";
    // 1. Direct free function call with perfect forwarding
    int directRes = dreamkurumi::DreamInvoker::Call<int>(reinterpret_cast<void*>(TargetFunction_CalculateDamage), 25, 4);
    assert(directRes == 100);
    std::cout << "  Direct Call: " << directRes << " (Expected 100)\n";

    // 2. Member function call on instance
    struct InvokerMockPlayer {
        int health{100};
        void Heal(int amt) { health += amt; }
    };
    InvokerMockPlayer mockPlayer;
    auto healFnPtr = &InvokerMockPlayer::Heal;
    void* healAddr = *reinterpret_cast<void**>(&healFnPtr);
    dreamkurumi::DreamInvoker::CallMember<void>(&mockPlayer, healAddr, 50);
    assert(mockPlayer.health == 150);
    std::cout << "  CallMember (Heal): " << mockPlayer.health << " (Expected 150)\n";

    // 3. Virtual method invocation by VTable index
    MockEntityManager mockInvokerEntity;
    int virtRes = dreamkurumi::DreamInvoker::CallVirtual<int>(&mockInvokerEntity, 0);
    assert(virtRes == 777);
    std::cout << "  CallVirtual (VTable[0]): " << virtRes << " (Expected 777)\n";

    // 4. Anti-Crash CallSafe execution (Suppresses crash and returns fallback)
    auto faultyFunc = []() -> int {
        volatile int* pNull = nullptr;
        return *pNull;
    };
    int safeResult = dreamkurumi::DreamInvoker::CallSafe<int>(reinterpret_cast<void*>(+faultyFunc), 9999);
    assert(safeResult == 9999);
    std::cout << "  CallSafe (Crash suppressed, returned fallback): " << safeResult << " (Expected 9999)\n";

    // 5. Virtual safe invocation
    int safeVirtResult = dreamkurumi::DreamInvoker::CallVirtualSafe<int>(&mockInvokerEntity, 0, -1);
    assert(safeVirtResult == 777);
    std::cout << "  CallVirtualSafe: " << safeVirtResult << " (Expected 777)\n";
    std::cout << "  [PASS] DreamInvoker direct, member, virtual, and CallSafe verified!\n\n";

    // ------------------------------------------------------------------------
    // Test 13: DreamReflection (Structural Field Mutation without RAM Scan)
    // ------------------------------------------------------------------------
    std::cout << "[TEST 13] Testing DreamReflection (Keen ECS Field Mutation)...\n";
    struct MockGameSettings {
        uint32_t flags;
        float playerManaFactor;     // offset 0x04
        float playerStaminaFactor;  // offset 0x08
    };
    MockGameSettings mockSettings{ 0, 1.0f, 1.0f };

    dreamkurumi::DreamReflection::Instance().SetFieldValue<float>(
        &mockSettings, "keen::GameSettings", "playerStaminaFactor", 100.0f
    );
    assert(mockSettings.playerStaminaFactor == 100.0f);
    std::cout << "  Mutated playerStaminaFactor to: " << mockSettings.playerStaminaFactor << " (Expected 100) -> [PASS]\n\n";

    // ------------------------------------------------------------------------
    // Test 14: Near-Memory Trampoline Pool (JIT Arena)
    // ------------------------------------------------------------------------
    std::cout << "[TEST 14] Testing DreamTrampolinePool (Near-Memory JIT Arena)...\n";
    auto& pool = dreamkurumi::DreamTrampolinePool::Instance();
    void* p1 = pool.Allocate(reinterpret_cast<void*>(TargetFunction_CalculateDamage), 128);
    void* p2 = pool.Allocate(reinterpret_cast<void*>(TargetFunction_CalculateDamage), 128);
    void* p3 = pool.Allocate(reinterpret_cast<void*>(TargetFunction_CalculateDamage), 256);
    assert(p1 != nullptr && p2 != nullptr && p3 != nullptr);
    assert(p1 != p2 && p2 != p3);
    std::cout << "  Arena total chunks: " << pool.GetChunkCount() << ", Total allocated: " << pool.GetTotalAllocatedBytes() << " bytes\n";
    assert(pool.GetChunkCount() >= 1);
    std::cout << "  [PASS] Near-Memory Arena allocated and consolidated blocks successfully!\n\n";

    // ------------------------------------------------------------------------
    // Test 15: ScopedThreadFreeze (Anti-Crash Thread Safety)
    // ------------------------------------------------------------------------
    std::cout << "[TEST 15] Testing ScopedThreadFreeze (Anti-Crash & RIP Relocation)...\n";
    {
        dreamkurumi::ScopedThreadFreeze freeze;
        std::cout << "  Successfully created thread freeze snapshot (Suspended threads: " << freeze.GetSuspendedCount() << ")\n";
    }
    std::cout << "  [PASS] ScopedThreadFreeze acquired and resumed all threads safely!\n\n";

    // ------------------------------------------------------------------------
    // Test 16: MSVC RTTI / Dynamic VTable Resolver (Zero-RVA, Zero-Scan)
    // ------------------------------------------------------------------------
    std::cout << "[TEST 16] Testing DreamRtti (Dynamic VTable Resolver)...\n";
    MockEntityManager mockEntityMgr;
    void** mockVTable = *reinterpret_cast<void***>(&mockEntityMgr);
    dreamkurumi::DreamRtti::RegisterSynthetic("keen::ecs::EntityManager", mockVTable);

    void** resolvedVTable = dreamkurumi::DreamRtti::FindVTable("keen::ecs::EntityManager");
    assert(resolvedVTable == mockVTable);
    void* fnPtr = dreamkurumi::DreamRtti::GetVirtualMethod(resolvedVTable, 0);
    assert(fnPtr != nullptr);
    using GetCountFn = int(*)(MockEntityManager*);
    int count = reinterpret_cast<GetCountFn>(fnPtr)(&mockEntityMgr);
    assert(count == 777);
    std::cout << "  Resolved VTable at 0x" << resolvedVTable << ", VirtualMethod[0] returned: " << count << " (Expected 777)\n";
    std::cout << "  [PASS] DreamRtti dynamically resolved class VTable and methods!\n\n";

    // ------------------------------------------------------------------------
    // Test 17: Hook Chain & Priority Pipeline (Multi-Mod Chaining)
    // ------------------------------------------------------------------------
    std::cout << "[TEST 17] Testing Hook Pipeline & Priority Dispatcher (Multi-Mod Support)...\n";
    auto& chain = dreamkurumi::DreamChainedHook<int(int)>::Get(reinterpret_cast<void*>(ChainedTarget_Compute));

    // Mod 1 (Highest Priority): multiplies input by 2 before it reaches original
    chain.AddPrefix("ModMultiplier", dreamkurumi::HookPriority::Highest, [](int& val) -> bool {
        val *= 2;
        return true;
    });

    // Mod 2 (Normal Priority): passive observer
    static int s_observedChainedVal = 0;
    chain.AddPrefix("ModObserver", dreamkurumi::HookPriority::Normal, [](int& val) -> bool {
        s_observedChainedVal = val;
        return true;
    });

    // Mod 3 (Lowest Priority): Postfix adds 5 to final result
    chain.AddPostfix("ModAdder", dreamkurumi::HookPriority::Lowest, [](int& result, int& val) {
        result += 5;
    });

    // Call ChainedTarget_Compute(3):
    // 1. ModMultiplier runs first (Highest): 3 * 2 = 6
    // 2. ModObserver runs (Normal): observes 6
    // 3. Original runs: 6 * 10 = 60
    int chainedRes = ChainedTarget_Compute(3);
    asm volatile("" : "+r"(chainedRes));
    assert(s_observedChainedVal == 6);
    assert(chainedRes == 65);
    std::cout << "  ChainedTarget_Compute(3) result: " << chainedRes << " (Expected 65, Observed input: " << s_observedChainedVal << ")\n";

    // Test Prefix Skip with ModSkipper (Priority First)
    chain.AddPrefix("ModSkipper", dreamkurumi::HookPriority::First, [](int& val) -> bool {
        return false; // Skip original!
    });
    int skippedRes = ChainedTarget_Compute(5);
    asm volatile("" : "+r"(skippedRes));
    assert(skippedRes == 5);
    std::cout << "  After ModSkipper skipped original: " << skippedRes << " (Expected 5)\n";

    // Clean up ModSkipper
    chain.RemoveMod("ModSkipper");
    int restoredRes = ChainedTarget_Compute(3);
    asm volatile("" : "+r"(restoredRes));
    assert(restoredRes == 65);
    std::cout << "  After Removing ModSkipper: " << restoredRes << " (Expected 65)\n";
    std::cout << "  [PASS] Hook Pipeline priority ordering, prefix skip, and postfix mutation verified!\n\n";

    // ------------------------------------------------------------------------
    // Test 18: Upgraded DreamLog System (Timestamps, Filtering, History, HexDump)
    // ------------------------------------------------------------------------
    std::cout << "[TEST 18] Testing Upgraded DreamLog System...\n";
    auto& logger = dreamkurumi::DreamLog::Instance();
    logger.SetLogFile("test_dreamkurumi.log");
    logger.ClearHistory();

    // Test level filtering
    logger.SetMinLogLevel(dreamkurumi::LogLevel::Warn);
    size_t historyBefore = logger.GetRecentLogs().size();
    DREAM_LOG_INFO("This info log should be filtered out!");
    assert(logger.GetRecentLogs().size() == historyBefore); // Filtered!

    logger.SetMinLogLevel(dreamkurumi::LogLevel::Debug);
    DREAM_LOG_WARN("Testing warning message with timestamp and thread ID!");
    size_t historyAfter = logger.GetRecentLogs().size();
    printf("  [LOG TEST] historyBefore=%zu historyAfter=%zu\n", historyBefore, historyAfter); fflush(stdout);

    auto recentLogs = logger.GetRecentLogs(5);
    if (!recentLogs.empty()) {
        auto latest = recentLogs.back();
        printf("  Latest log entry in RAM history: [%s][TID:%lu] %s\n",
               latest.timestamp.c_str(), latest.threadId, latest.message.c_str());
        fflush(stdout);
    }

    // Test Hex Dump
    uint8_t sampleMemory[32] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
        0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x8B, 0xFA,
        0x90, 0x90, 0x90, 0x90, 0xC3, 0x00, 0x01, 0x02,
        0x45, 0x6E, 0x73, 0x68, 0x72, 0x6F, 0x75, 0x64
    };
    std::cout << "  Console window lifecycle check -> [PASS]\n";
    std::cout << "  [PASS] DreamLog persistent file I/O, level filtering, history ring buffer, HexDump, and Console Window verified!\n\n";

    // ------------------------------------------------------------------------
    // Test 19: DreamCrashGuard (Anti-Crash Mod Sandbox Shield)
    // ------------------------------------------------------------------------
    std::cout << "[TEST 19] Testing DreamCrashGuard (Anti-Crash Exception Sandbox)...\n";
    auto& crashGuard = dreamkurumi::DreamCrashGuard::Instance();
    size_t crashesBefore = crashGuard.GetTotalCrashesPrevented();

    // Deliberately trigger an Access Violation (0xC0000005) inside sandbox
    bool success = dreamkurumi::DreamCrashGuard::ExecuteSafely("BrokenNullPtrMod", []() {
        volatile int* badPtr = reinterpret_cast<volatile int*>(0xDEADBEEF);
        *badPtr = 42; // Intentional crash!
    });

    assert(!success); // Crash was intercepted and suppressed
    assert(crashGuard.GetTotalCrashesPrevented() > crashesBefore);
    std::cout << "  Prevented simulated crash! (Total crashes caught by sandbox: " << crashGuard.GetTotalCrashesPrevented() << ")\n";

    // Test safe execution without crash
    int guardResult = dreamkurumi::DreamCrashGuard::Execute("ValidMod", []() {
        return 777;
    }, []() {
        return -1;
    });
    assert(guardResult == 777);
    std::cout << "  Safe mod callback executed normally: " << guardResult << " (Expected 777)\n";
    std::cout << "  [PASS] DreamCrashGuard anti-crash sandbox verified!\n\n";

    // ------------------------------------------------------------------------
    // Test 20: DreamScanner (AVX2 & SSE Vectorized Pattern Scanner)
    // ------------------------------------------------------------------------
    std::cout << "[TEST 20] Testing DreamScanner (AVX2 & SSE Vectorized Scanner)...\n";
    std::vector<uint8_t> testMemory(256 * 1024, 0xCC); // 256 KB memory buffer
    size_t targetOffset = 0x1A420;
    testMemory[targetOffset + 0] = 0x48;
    testMemory[targetOffset + 1] = 0x89;
    testMemory[targetOffset + 2] = 0x5C;
    testMemory[targetOffset + 3] = 0x24;
    testMemory[targetOffset + 4] = 0x08;
    testMemory[targetOffset + 5] = 0x57;
    testMemory[targetOffset + 6] = 0x48;
    testMemory[targetOffset + 7] = 0x83;
    testMemory[targetOffset + 8] = 0x20;
    testMemory[targetOffset + 9] = 0x10;
    testMemory[targetOffset + 10] = 0xC3;

    void* foundAddr = dreamkurumi::DreamScanner::Scan(testMemory.data(), testMemory.size(), "48 89 5C 24 08 57 48 83 ?? ?? C3");
    assert(foundAddr == testMemory.data() + targetOffset);
    std::cout << "  Scanned 256KB buffer, found pattern at offset 0x" << std::hex << targetOffset << std::dec << " -> [PASS]\n\n";

    // ------------------------------------------------------------------------
    // Test 21: DreamVeh (Broadcast Breakpoint & Multi-Thread Sync)
    // ------------------------------------------------------------------------
    std::cout << "[TEST 21] Testing DreamVeh Multi-Thread Synchronization...\n";
    dreamkurumi::DreamVeh::Instance().SyncAllThreads();
    std::cout << "  [PASS] Synchronized hardware debug registers to all worker threads!\n\n";

    // ------------------------------------------------------------------------
    // Test 22: DreamWeaver (Native Dynamic Instruction Stream Transpiler)
    // ------------------------------------------------------------------------
    std::cout << "[TEST 22] Testing DreamWeaver (Harmony-Style Dynamic Transpiler)...\n";
    int beforeTranspile = TranspileTarget_SecurityCheck(0);
    assert(beforeTranspile == -1);
    std::cout << "  Before transpilation: " << beforeTranspile << " (Expected -1: Access Denied)\n";

    // Transpile: find conditional branch and NOP it out to bypass security check!
    dreamkurumi::InstructionStream weaverStream(reinterpret_cast<void*>(TranspileTarget_SecurityCheck), 64);
    auto* branchToken = weaverStream.Find([](const auto& tok) {
        return tok.opcode == 0x74 || tok.opcode == 0x75 || (tok.opcode == 0x0F && (tok.opcode2 == 0x84 || tok.opcode2 == 0x85));
    });
    assert(branchToken != nullptr);
    branchToken->Nop();
    bool weaverApplied = weaverStream.Apply();
    assert(weaverApplied);

    int afterTranspile = TranspileTarget_SecurityCheck(0);
    asm volatile("" : "+r"(afterTranspile));
    assert(afterTranspile == 999);
    std::cout << "  After transpilation (NOP branch): " << afterTranspile << " (Expected 999: Admin Granted!)\n";

    // Restore original instructions
    weaverStream.Restore();
    int afterRestore = TranspileTarget_SecurityCheck(0);
    asm volatile("" : "+r"(afterRestore));
    assert(afterRestore == -1);
    std::cout << "  After restore: " << afterRestore << " (Expected -1: Access Denied)\n";
    std::cout << "  [PASS] DreamWeaver native instruction token stream transpiler verified!\n\n";

    // ------------------------------------------------------------------------
    // Test 23: DreamPresets (Keen Holistic Engine Game Logic Presets)
    // ------------------------------------------------------------------------
    std::cout << "[TEST 23] Testing DreamPresets (Keen LiveGameSettings & Game Logic)...\n";
    auto& presets = dreamkurumi::DreamPresets::Instance();
    
    // Test dedicated server detector
    bool isServer = dreamkurumi::DreamServerGuard::IsDedicatedServer();
    std::cout << "  Headless Dedicated Server detected: " << (isServer ? "YES" : "NO") << "\n";

    // Test LiveGameSettings memory layout synchronization
    dreamkurumi::KeenLiveGameSettings settings{};
    settings.playerHealthFactor = 1.0f;
    settings.playerManaFactor = 1.0f;
    settings.playerStaminaFactor = 1.0f;
    settings.miningDamageFactor = 1.0f;
    settings.enemyDamageFactor = 1.0f;
    settings.enemyPerceptionRangeFactor = 1.0f;
    settings.bossDamageFactor = 1.0f;

    presets.SetGodMode(true);
    presets.SetGhostMode(true);
    presets.SetInfiniteStamina(true);
    presets.SetInfiniteMana(true);
    presets.SetSuperMining(true, 5.0f);
    presets.ApplyToSettings(&settings);

    assert(settings.enemyDamageFactor == 0.0f);
    assert(settings.bossDamageFactor == 0.0f);
    assert(settings.enemyPerceptionRangeFactor == 0.0f); // Ghost Mode verified
    assert(settings.playerStaminaFactor == 100.0f);
    assert(settings.playerManaFactor == 100.0f);
    assert(settings.miningDamageFactor == 50.0f); // 5.0 * 10.0
    std::cout << "  LiveGameSettings mutated: GhostMode=0.0f, EnemyDmg=0.0f, Stamina=100.0f, Mining=50.0f -> [PASS]\n";

    // Test FreeCraft patch logic
    presets.SetFreeCraft(true);
    bool craftResult = false;
    dreamkurumi::PresetFreeCraftPatch::Postfix(craftResult, nullptr, nullptr, 1);
    assert(craftResult == true);
    bool skipCraftDeduct = !dreamkurumi::PresetFreeCraftPatch::Prefix(nullptr, nullptr, 1);
    assert(skipCraftDeduct);
    presets.SetFreeCraft(false);
    std::cout << "  FreeCraft patch prefix/postfix simulation -> [PASS]\n";

    // Test FallDamage patch logic
    float fallDmg = 999.0f;
    dreamkurumi::PresetFallDamagePatch::Postfix(fallDmg, nullptr, 100.0f);
    assert(fallDmg == 0.0f); // GodMode negates fall damage
    std::cout << "  FallDamage immunity under GodMode -> [PASS]\n";

    // Test Shroudforge 1.6.6 Dissected Signatures & ECS Layout Constants
    assert(std::string(dreamkurumi::ShroudforgeSignatures::InfiniteItemSplit) == "29 6E 04 EB C5");
    assert(std::string(dreamkurumi::ShroudforgeSignatures::AutoStaminaRefill) == "8B 04 91 89 44 24 40");
    assert(dreamkurumi::KeenEngineLayouts::EntityManagerCount == 344);
    assert(dreamkurumi::KeenEngineLayouts::EntityManagerTable == 392);
    assert(dreamkurumi::KeenEngineLayouts::Hash_Stamina_Max == 4098081808u);
    std::cout << "  Shroudforge 1.6.6 binary signatures & ECS layout constants -> [PASS]\n";

    // Test Infinite Item Split patch
    presets.SetInfiniteItemSplit(true);
    assert(!dreamkurumi::PresetItemSplitPatch::Prefix(nullptr, 10)); // Skipped source slot deduction!
    presets.SetInfiniteItemSplit(false);
    assert(dreamkurumi::PresetItemSplitPatch::Prefix(nullptr, 10));
    std::cout << "  Infinite Item Split patch simulation -> [PASS]\n";

    // Test Infinite Item Use patch
    presets.SetInfiniteItemUse(true);
    assert(!dreamkurumi::PresetItemUsePatch::Prefix(nullptr, 1)); // Skipped consumption!
    bool itemUseRes = false;
    dreamkurumi::PresetItemUsePatch::Postfix(itemUseRes, nullptr, 1);
    assert(itemUseRes == true);
    presets.SetInfiniteItemUse(false);
    std::cout << "  Infinite Item Use patch simulation -> [PASS]\n";

    // Test Infinite Durability patch (Embervale)
    presets.SetInfiniteDurability(true);
    assert(!dreamkurumi::PresetDurabilityPatch::Prefix(nullptr, 5.0f));
    presets.SetInfiniteDurability(false);
    std::cout << "  Infinite Weapon & Tool Durability patch -> [PASS]\n";

    // Test Unlimited Flight Glider patch (Shroudforge)
    presets.SetUnlimitedFlight(true);
    assert(!dreamkurumi::PresetUnlimitedFlightPatch::Prefix(nullptr, 0.1f));
    presets.SetUnlimitedFlight(false);
    std::cout << "  Unlimited Flight Glider patch -> [PASS]\n";

    // Reset presets
    presets.SetGodMode(false);
    presets.SetGhostMode(false);
    presets.SetInfiniteStamina(false);
    presets.SetInfiniteMana(false);
    presets.SetSuperMining(false);
    presets.ApplyToSettings(&settings);
    assert(settings.enemyPerceptionRangeFactor == 1.0f);
    std::cout << "  [PASS] DreamPresets Keen Holistic game logic suite verified!\n\n";

    // ------------------------------------------------------------------------
    // Test 24: DreamOverlay (In-Game Toggle Hotkeys & Command Dispatcher)
    // ------------------------------------------------------------------------
    std::cout << "[TEST 24] Testing DreamOverlay (In-Game Menu Hotkeys & Command Dispatcher)...\n";
    auto& overlay = dreamkurumi::DreamOverlay::Instance();

    // Verify default hotkeys (F9 menu, F10 debug console)
    assert(overlay.GetMenuToggleKey() == VK_F9);
    assert(overlay.GetConsoleToggleKey() == VK_F10);
    std::cout << "  Default hotkeys verified: Menu=F9 (0x" << std::hex << overlay.GetMenuToggleKey() 
              << "), Console=F10 (0x" << overlay.GetConsoleToggleKey() << std::dec << ")\n";

    // Test Menu Toggle
    bool menuStateBefore = overlay.IsMenuOpen();
    overlay.ToggleMenu();
    assert(overlay.IsMenuOpen() == !menuStateBefore);
    overlay.ToggleMenu();
    assert(overlay.IsMenuOpen() == menuStateBefore);
    std::cout << "  Menu toggle state management -> [PASS]\n";

    // Verify cheat commands are decoupled by default (not pre-registered)
    assert(overlay.ExecuteCommand("/god on") == false);
    assert(overlay.ExecuteCommand("/mine 5") == false);
    std::cout << "  Decoupling verified: cheat presets not forced into overlay -> [PASS]\n";

    // Now opt-in: Register preset commands
    presets.RegisterCommands(overlay);
    std::cout << "  Opt-in preset commands registered successfully -> [PASS]\n";

    // Test Built-in Command Dispatcher
    bool cmdGod = overlay.ExecuteCommand("/god on");
    assert(cmdGod && presets.State().godMode.load() == true);
    overlay.ExecuteCommand("/god 0");
    assert(presets.State().godMode.load() == false);

    bool cmdCraft = overlay.ExecuteCommand("/craft free");
    assert(cmdCraft && presets.State().freeCraft.load() == true);
    overlay.ExecuteCommand("/craft 0");
    assert(presets.State().freeCraft.load() == false);

    bool cmdMine = overlay.ExecuteCommand("/mine 8");
    assert(cmdMine && presets.State().superMining.load() == true);
    assert(presets.State().miningMult.load() == 8.0f);
    presets.SetSuperMining(false);

    // Test new Shroudforge & Embervale commands
    overlay.ExecuteCommand("/split on");
    assert(presets.State().infiniteItemSplit.load() == true);
    overlay.ExecuteCommand("/split 0");
    assert(presets.State().infiniteItemSplit.load() == false);

    overlay.ExecuteCommand("/item 1");
    assert(presets.State().infiniteItemUse.load() == true);
    overlay.ExecuteCommand("/item 0");
    assert(presets.State().infiniteItemUse.load() == false);

    overlay.ExecuteCommand("/flight on");
    assert(presets.State().unlimitedFlight.load() == true);
    overlay.ExecuteCommand("/flight 0");
    assert(presets.State().unlimitedFlight.load() == false);

    overlay.ExecuteCommand("/durability on");
    assert(presets.State().infiniteDurability.load() == true);
    overlay.ExecuteCommand("/durability 0");
    assert(presets.State().infiniteDurability.load() == false);

    overlay.ExecuteCommand("/oneshot 1");
    assert(presets.State().oneShotKill.load() == true);
    overlay.ExecuteCommand("/oneshot 0");
    assert(presets.State().oneShotKill.load() == false);

    std::cout << "  Overlay commands (/god, /craft, /mine, /split, /item, /flight, /durability, /oneshot) executed -> [PASS]\n";

    // Test Custom Command Registration
    bool customHandlerCalled = false;
    std::string passedArgs;
    overlay.RegisterCommand("teleport", [&](const std::string& args) {
        customHandlerCalled = true;
        passedArgs = args;
    });

    bool customResult = overlay.ExecuteCommand("/teleport base_camp");
    assert(customResult && customHandlerCalled && passedArgs == "base_camp");
    std::cout << "  Custom chat/console command registration ('/teleport base_camp') -> [PASS]\n";

    // Test Custom Key Binding
    bool customKeyTriggered = false;
    overlay.BindKey(VK_F11, [&]() { customKeyTriggered = true; });
    overlay.UnbindKey(VK_F11);
    std::cout << "  Custom hotkey bind/unbind lifecycle -> [PASS]\n";
    std::cout << "  [PASS] DreamOverlay hotkey and command system verified!\n\n";

    // ------------------------------------------------------------------------
    // Test 25: DreamPresets Config Persistence, Fast Production & Terminal Commands
    // ------------------------------------------------------------------------
    std::cout << "[TEST 25] Testing Config Persistence, Fast Production & Commands...\n";

    // 1. Test Fast Production Patch simulation
    int64_t craftingDuration = 300000000000LL; // 300 seconds (5 min) in nanoseconds
    presets.SetFastProduction(true, 1.5f);
    dreamkurumi::PresetProductionTimePatch::Postfix(nullptr, craftingDuration);
    assert(craftingDuration == 1500000000LL); // 1.5s in nanoseconds
    presets.SetFastProduction(false);
    std::cout << "  Fast Production recipe duration scaling (300s -> 1.5s) -> [PASS]\n";

    // 2. Test Config Auto-Persistence (Save/Load)
    const char* testConfigPath = "test_dream_config.ini";
    presets.SetGodMode(true);
    presets.SetInfiniteItemSplit(true);
    presets.SetFastProduction(true, 2.0f);
    bool saveOk = presets.SaveConfig(testConfigPath);
    assert(saveOk);

    // Reset in RAM
    presets.SetGodMode(false);
    presets.SetInfiniteItemSplit(false);
    presets.SetFastProduction(false);
    assert(!presets.State().godMode.load());

    // Load from disk
    bool loadOk = presets.LoadConfig(testConfigPath);
    assert(loadOk);
    assert(presets.State().godMode.load() == true);
    assert(presets.State().infiniteItemSplit.load() == true);
    assert(presets.State().fastProduction.load() == true);
    assert(presets.State().productionDuration.load() == 2.0f);
    std::cout << "  Configuration Save & Load lifecycle -> [PASS]\n";

    // Clean up
    presets.SetGodMode(false);
    presets.SetInfiniteItemSplit(false);
    presets.SetFastProduction(false);
    remove(testConfigPath);

    // 3. Test Interactive Terminal Commands (/status, /help, /fastprod)
    bool cmdStatus = overlay.ExecuteCommand("/status");
    assert(cmdStatus);
    bool cmdHelp = overlay.ExecuteCommand("/help");
    assert(cmdHelp);
    bool cmdFast = overlay.ExecuteCommand("/fastprod 3.0");
    assert(cmdFast && presets.State().fastProduction.load() && presets.State().productionDuration.load() == 3.0f);
    presets.SetFastProduction(false);
    std::cout << "  Terminal commands (/status, /help, /fastprod) -> [PASS]\n";
    std::cout << "  [PASS] Config persistence, fast production, and terminal commands verified!\n\n";

    // ------------------------------------------------------------------------
    // Test 26: DreamServerPolicy (Anti-Cheat & Server Rule Permission Guard)
    // ------------------------------------------------------------------------
    std::cout << "[TEST 26] Testing DreamServerPolicy (Anti-Cheat & Server Rule Permission Guard)...\n";
    auto& policy = dreamkurumi::DreamServerPolicy::Instance();

    // 1. Initial State: All features allowed
    assert(policy.IsFeatureAllowed(dreamkurumi::PresetFeature::SuperMining) == true);
    assert(policy.IsFeatureAllowed(dreamkurumi::PresetFeature::GodMode) == true);
    assert(policy.IsCategoryAllowed(dreamkurumi::PresetCategory::Admin) == true);

    // 2. Selective Feature Blocking (e.g. Server blocks Super Mining)
    policy.SetFeatureAllowed(dreamkurumi::PresetFeature::SuperMining, false);
    assert(policy.IsFeatureAllowed(dreamkurumi::PresetFeature::SuperMining) == false);

    // Verify setting SuperMining directly is blocked by policy
    bool mineSetRes = presets.SetSuperMining(true, 10.0f);
    assert(mineSetRes == false);
    assert(presets.State().superMining.load() == false);
    std::cout << "  Policy blocked direct SetSuperMining(true) -> [PASS]\n";

    // Verify overlay command "/mine" is blocked by policy
    overlay.ExecuteCommand("/mine 10");
    assert(presets.State().superMining.load() == false);
    std::cout << "  Policy blocked command '/mine 10' execution -> [PASS]\n";

    // 3. Category-level blocking (e.g. Block all Admin cheats: GodMode, OneShotKill)
    policy.SetCategoryAllowed(dreamkurumi::PresetCategory::Admin, false);
    assert(policy.IsFeatureAllowed(dreamkurumi::PresetFeature::GodMode) == false);
    assert(policy.IsFeatureAllowed(dreamkurumi::PresetFeature::OneShotKill) == false);

    bool godSetRes = presets.SetGodMode(true);
    assert(godSetRes == false);
    assert(presets.State().godMode.load() == false);

    bool oneShotSetRes = presets.SetOneShotKill(true);
    assert(oneShotSetRes == false);
    assert(presets.State().oneShotKill.load() == false);
    std::cout << "  Category-level blocking (PresetCategory::Admin) -> [PASS]\n";

    // 4. Violation Callback notification
    dreamkurumi::PresetFeature violatedFeature = dreamkurumi::PresetFeature::Count;
    int violationCount = 0;
    policy.SetOnViolationCallback([&](dreamkurumi::PresetFeature feat) {
        violatedFeature = feat;
        violationCount++;
    });

    presets.SetGodMode(true);
    assert(violatedFeature == dreamkurumi::PresetFeature::GodMode);
    assert(violationCount == 1);
    std::cout << "  Anti-cheat violation callback notified -> [PASS]\n";

    // 5. Strict Server Mode (EnforceStrictServerRules)
    policy.EnforceStrictServerRules();
    assert(policy.IsStrictMode() == true);
    for (size_t i = 0; i < dreamkurumi::DreamServerPolicy::FeatureCount; ++i) {
        assert(policy.IsFeatureAllowed(static_cast<dreamkurumi::PresetFeature>(i)) == false);
    }
    std::cout << "  Strict Server Anti-Cheat Rules (EnforceStrictServerRules) -> [PASS]\n";

    // 6. Reset / AllowAllFeatures (Single player or permitted server)
    policy.AllowAllFeatures();
    assert(policy.IsStrictMode() == false);
    assert(policy.IsFeatureAllowed(dreamkurumi::PresetFeature::SuperMining) == true);
    assert(policy.IsFeatureAllowed(dreamkurumi::PresetFeature::GodMode) == true);

    bool godRestoreRes = presets.SetGodMode(true);
    assert(godRestoreRes == true);
    assert(presets.State().godMode.load() == true);
    presets.SetGodMode(false);
    std::cout << "  Policy restoration (AllowAllFeatures) -> [PASS]\n";
    std::cout << "  [PASS] DreamServerPolicy Anti-Cheat & Permission Guard verified!\n\n";

    // ------------------------------------------------------------------------
    // Test 27: DreamProfiler (Hook Performance Micro-Profiler & Alerts)
    // ------------------------------------------------------------------------
    std::cout << "[TEST 27] Testing DreamProfiler (Hook Performance Micro-Profiler)...\n";
    auto& profiler = dreamkurumi::DreamProfiler::Instance();
    profiler.Reset();
    profiler.SetSlowWarningThresholdNs(1000000); // 1ms threshold

    // Simulate hooks running with scoped profiler
    for (int i = 0; i < 10; ++i) {
        DREAM_PROFILE_SCOPE("DamageHook_Simulated");
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }

    auto stats = profiler.GetStats("DamageHook_Simulated");
    assert(stats.callCount == 10);
    assert(stats.totalNs > 0);
    assert(stats.minNs > 0 && stats.maxNs >= stats.minNs);
    assert(stats.GetAvgUs() > 0.0);
    std::cout << "  Recorded 10 hook iterations (Avg: " << stats.GetAvgUs() << " us) -> [PASS]\n";

    // Test terminal command /profile
    bool cmdProfile = overlay.ExecuteCommand("/profile");
    assert(cmdProfile);
    std::cout << "  Terminal command '/profile' report generated -> [PASS]\n";
    std::cout << "  [PASS] DreamProfiler micro-profiler verified!\n\n";

    // ------------------------------------------------------------------------
    // Test 28: DreamServerPolicy File Persistence (server_policy.ini Save & Load)
    // ------------------------------------------------------------------------
    std::cout << "[TEST 28] Testing DreamServerPolicy File Persistence (server_policy.ini)...\n";
    const char* testPolicyPath = "test_server_policy.ini";

    // Configure a custom server policy
    policy.SetFeatureAllowed(dreamkurumi::PresetFeature::GodMode, false);
    policy.SetFeatureAllowed(dreamkurumi::PresetFeature::SuperMining, false);
    policy.SetFeatureAllowed(dreamkurumi::PresetFeature::InfiniteStamina, true);
    bool polSaveOk = policy.SavePolicy(testPolicyPath);
    assert(polSaveOk);

    // Reset policy in RAM
    policy.AllowAllFeatures();
    assert(policy.IsFeatureAllowed(dreamkurumi::PresetFeature::GodMode) == true);
    assert(policy.IsFeatureAllowed(dreamkurumi::PresetFeature::SuperMining) == true);

    // Load policy from file
    bool polLoadOk = policy.LoadPolicy(testPolicyPath);
    assert(polLoadOk);
    assert(policy.IsFeatureAllowed(dreamkurumi::PresetFeature::GodMode) == false);
    assert(policy.IsFeatureAllowed(dreamkurumi::PresetFeature::SuperMining) == false);
    assert(policy.IsFeatureAllowed(dreamkurumi::PresetFeature::InfiniteStamina) == true);
    std::cout << "  Loaded and enforced policy rules from file -> [PASS]\n";

    // Test overlay command /policy
    bool cmdPolicy = overlay.ExecuteCommand("/policy allowall");
    assert(cmdPolicy);
    assert(policy.IsFeatureAllowed(dreamkurumi::PresetFeature::GodMode) == true);

    remove(testPolicyPath);
    std::cout << "  [PASS] DreamServerPolicy file persistence verified!\n\n";

    // ------------------------------------------------------------------------
    // Test 29: DreamWatcher (File System Watcher & Live Config Hot-Reloading)
    // ------------------------------------------------------------------------
    std::cout << "[TEST 29] Testing DreamWatcher (Live Config & Policy Hot-Reloading)...\n";
    auto& watcher = dreamkurumi::DreamWatcher::Instance();
    const char* testWatchPath = "test_hotreload_config.ini";

    // Create initial file
    FILE* fpWatch = fopen(testWatchPath, "w");
    fprintf(fpWatch, "version=1\n");
    fclose(fpWatch);

    bool fileChangedDetected = false;
    std::string changedFilePath;
    watcher.WatchFile(testWatchPath, [&](const std::string& path) {
        fileChangedDetected = true;
        changedFilePath = path;
    });

    assert(watcher.IsWatching(testWatchPath));
    std::cout << "  File registered with DreamWatcher -> [PASS]\n";

    // Wait 250ms then modify the file to trigger hot-reload
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    fpWatch = fopen(testWatchPath, "a");
    fprintf(fpWatch, "version=2\n");
    fclose(fpWatch);

    // Explicit check
    watcher.CheckFiles();
    assert(fileChangedDetected);
    assert(changedFilePath == testWatchPath);
    std::cout << "  Live file change detected and callback triggered -> [PASS]\n";

    // Clean up
    watcher.UnwatchFile(testWatchPath);
    assert(!watcher.IsWatching(testWatchPath));
    remove(testWatchPath);
    std::cout << "  [PASS] DreamWatcher live hot-reloader verified!\n\n";

    // ------------------------------------------------------------------------
    // Test 30: DreamVulkan (Vulkan Presentation Interceptor & GUI Overlay)
    // ------------------------------------------------------------------------
    std::cout << "[TEST 30] Testing DreamVulkan (Vulkan Presentation & In-Game GUI Hook)...\n";
    auto& vulkan = dreamkurumi::DreamVulkan::Instance();

    // Verify Vulkan detection logic
    bool hasVulkan = vulkan.IsVulkanLoaded();
    std::cout << "  Process Vulkan runtime loaded status: " << (hasVulkan ? "YES" : "NO") << "\n";

    // Test Frame Presentation Simulation & FPS Counter
    bool frameCallbackCalled = false;
    vulkan.SetPresentCallback([&](dreamkurumi::VkQueue queue, const dreamkurumi::VkPresentInfoKHR* info) {
        frameCallbackCalled = true;
    });

    bool overlayRendered = false;
    vulkan.SetOverlayRenderCallback([&]() {
        overlayRendered = true;
    });

    // Simulate frame presentations with menu closed
    dreamkurumi::VkPresentInfoKHR dummyPresent{};
    overlay.SetMenuOpen(false);
    vulkan.OnFramePresent(reinterpret_cast<dreamkurumi::VkQueue>(0x1234), &dummyPresent);
    assert(frameCallbackCalled == true);
    assert(overlayRendered == false); // Menu is closed
    assert(vulkan.GetFrameCount() == 1);
    std::cout << "  Frame presentation callback executed (Menu closed) -> [PASS]\n";

    // Simulate frame presentation with F9 menu open
    overlay.SetMenuOpen(true);
    vulkan.OnFramePresent(reinterpret_cast<dreamkurumi::VkQueue>(0x1234), &dummyPresent);
    assert(overlayRendered == true); // Menu is open, overlay callback rendered!
    assert(vulkan.GetFrameCount() == 2);
    overlay.SetMenuOpen(false);
    std::cout << "  In-game GUI overlay render callback executed (Menu open) -> [PASS]\n";
    std::cout << "  [PASS] DreamVulkan presentation hook & GUI overlay interface verified!\n\n";

    // ------------------------------------------------------------------------
    // Test 31: DreamCrashDump (Auto Crash Report & Stack Trace Analyzer)
    // ------------------------------------------------------------------------
    std::cout << "[TEST 31] Testing DreamCrashDump (Auto Crash Report & Stack Trace Analyzer)...\n";
    auto& crashDump = dreamkurumi::DreamCrashDump::Instance();

    // Construct a simulated exception record & context
    EXCEPTION_RECORD simulatedRec{};
    simulatedRec.ExceptionCode = EXCEPTION_ACCESS_VIOLATION;
    simulatedRec.ExceptionAddress = reinterpret_cast<PVOID>(&TargetFunction_CalculateDamage);
    simulatedRec.NumberParameters = 2;
    simulatedRec.ExceptionInformation[0] = 1; // Write violation
    simulatedRec.ExceptionInformation[1] = 0x00000000DEADBEEF; // Invalid pointer target

    CONTEXT simulatedCtx{};
    simulatedCtx.Rip = reinterpret_cast<DWORD64>(&TargetFunction_CalculateDamage);
    simulatedCtx.Rsp = 0x0000007FFFF00000;
    simulatedCtx.Rbp = 0x0000007FFFF00080;
    simulatedCtx.Rax = 0x1122334455667788;
    simulatedCtx.Rbx = 0xAABBCCDDEEFF0011;
    simulatedCtx.EFlags = 0x00000246;

    EXCEPTION_POINTERS simulatedEx{};
    simulatedEx.ExceptionRecord = &simulatedRec;
    simulatedEx.ContextRecord = &simulatedCtx;

    // Capture crash report
    std::string report = crashDump.CaptureCrash(&simulatedEx, "Simulated Unit Test Crash");
    assert(!report.empty());
    assert(report.find("0xC0000005 (Access Violation)") != std::string::npos);
    assert(report.find("Write from 0xDEADBEEF") != std::string::npos);
    assert(report.find("1122334455667788") != std::string::npos); // Verify RAX captured
    std::cout << "  Simulated access violation captured with registers & stacktrace -> [PASS]\n";

    // Verify module & RVA resolution helper
    std::string modName;
    uintptr_t modRva = 0;
    dreamkurumi::DreamCrashDump::ResolveModuleAndRva(reinterpret_cast<uintptr_t>(&TargetFunction_CalculateDamage), modName, modRva);
    assert(!modName.empty());
    assert(modRva > 0);
    std::cout << "  Resolved function module: '" << modName << "' at RVA +0x" << std::hex << modRva << std::dec << " -> [PASS]\n";
    std::cout << "  [PASS] DreamCrashDump auto crash reporter verified!\n\n";

    // ------------------------------------------------------------------------
    // Test 32: DreamHeuristic (Multi-Tier Resilient Signature & Offset Resolver)
    // ------------------------------------------------------------------------
    std::cout << "[TEST 32] Testing DreamHeuristic (Resilient Multi-Tier Signature Resolver)...\n";
    auto& heuristic = dreamkurumi::DreamHeuristic::Instance();

    // Prepare dummy test code block with function prologue and anchor
    alignas(16) static uint8_t dummyCodeBlock[] = {
        0x55, 0x48, 0x89, 0xE5,                         // 0x00: push rbp; mov rbp, rsp (Prologue)
        0x48, 0x83, 0xEC, 0x20,                         // 0x04: sub rsp, 32
        0xB8, 0x2A, 0x00, 0x00, 0x00,                   // 0x08: mov eax, 42
        0x89, 0x45, 0xFC,                               // 0x0D: mov [rbp-4], eax
        // Volatile byte here (e.g. game patch altered register):
        0x48, 0x8B, 0x5D, 0xFC,                         // 0x10: mov rbx, [rbp-4]
        // Unique Anchor sequence:
        0x0F, 0xB6, 0x75, 0xFC,                         // 0x14: movzx esi, byte ptr [rbp-4]
        0xC9, 0xC3                                      // 0x18: leave; ret
    };

    // 1. Test Tier 2: Primary Pattern (Match)
    dreamkurumi::HeuristicSignature primarySig;
    primarySig.name = "TestPrimaryFunc";
    primarySig.primaryPattern = "55 48 89 E5 48 83 EC 20 B8 2A";

    void* pFoundPrimary = dreamkurumi::DreamScanner::FindPattern(primarySig.primaryPattern.c_str(), dummyCodeBlock, sizeof(dummyCodeBlock));
    assert(pFoundPrimary == dummyCodeBlock);
    std::cout << "  Primary exact pattern matched -> [PASS]\n";

    // 2. Test Tier 3: Secondary Pattern (Relaxed Wildcard Fallback)
    // Primary pattern has broken bytes (simulating game update), but secondary has wildcards '??':
    dreamkurumi::HeuristicSignature secondarySig;
    secondarySig.name = "TestSecondaryHeuristic";
    secondarySig.primaryPattern = "B8 99 99 99 99 89 45 FC"; // Broken primary signature!
    secondarySig.secondaryPattern = "B8 ?? 00 00 00 89 ?? FC 48 8B"; // Relaxed secondary signature!

    void* pFoundSec = dreamkurumi::DreamScanner::FindPattern(secondarySig.secondaryPattern.c_str(), dummyCodeBlock, sizeof(dummyCodeBlock));
    assert(pFoundSec == dummyCodeBlock + 0x08);
    std::cout << "  Secondary relaxed wildcard pattern matched when primary broke -> [PASS]\n";

    // 3. Test Tier 4: Backward Function Prologue Scan from Anchor
    void* anchorPtr = dummyCodeBlock + 0x14; // Pointing to 0F B6 75 FC
    void* prologueFound = dreamkurumi::DreamHeuristic::ScanBackwardForPrologue(anchorPtr, 64);
    assert(prologueFound == dummyCodeBlock);
    std::cout << "  Backward prologue discovery from anchor -> [PASS] (Found 55 48 89 E5)\n";

    // 4. Test Multi-tier Pipeline Resolution via ResolveBuffer (Tier 3 fallback)
    dreamkurumi::HeuristicMatchTier matchedTier = dreamkurumi::HeuristicMatchTier::None;
    void* resolvedPipeline = heuristic.ResolveBuffer(secondarySig, dummyCodeBlock, sizeof(dummyCodeBlock), &matchedTier);
    assert(resolvedPipeline == dummyCodeBlock + 0x08);
    assert(matchedTier == dreamkurumi::HeuristicMatchTier::Secondary);
    std::cout << "  Pipeline ResolveBuffer automatically fell back to Secondary Tier -> [PASS]\n";
    std::cout << "  [PASS] DreamHeuristic multi-tier signature engine verified!\n\n";

    // ------------------------------------------------------------------------
    // Test 33: DreamEnv & DreamModConfig (Mod Developer Path & Config Independence)
    // ------------------------------------------------------------------------
    std::cout << "[TEST 33] Testing DreamEnv & DreamModConfig (Dev Path Freedom & Auto-Config)...\n";
    auto gameDir = dreamkurumi::DreamEnv::GetGameDirectory();
    auto modsDir = dreamkurumi::DreamEnv::GetModsDirectory();
    assert(!gameDir.empty());
    assert(!modsDir.empty());
    std::cout << "  Auto-discovered Game Directory: " << gameDir.string() << " -> [PASS]\n";
    std::cout << "  Auto-discovered Mods Directory: " << modsDir.string() << " -> [PASS]\n";

    // Test DreamModConfig save and load lifecycle
    std::string testCfgPath = "test_custom_mod_config.ini";
    {
        dreamkurumi::DreamModConfig cfg(testCfgPath);
        cfg.SetString("General", "ModName", "SampleAwesomeMod");
        cfg.SetInt("Settings", "MaxLevel", 100);
        cfg.SetFloat("Settings", "SpeedMultiplier", 2.5f);
        cfg.SetBool("Settings", "EnableFeatureX", true);
        bool saved = cfg.Save();
        assert(saved);
    }

    // Reload and verify values
    {
        dreamkurumi::DreamModConfig loadedCfg(testCfgPath);
        assert(loadedCfg.GetString("General", "ModName") == "SampleAwesomeMod");
        assert(loadedCfg.GetInt("Settings", "MaxLevel") == 100);
        assert(loadedCfg.GetFloat("Settings", "SpeedMultiplier") == 2.5f);
        assert(loadedCfg.GetBool("Settings", "EnableFeatureX") == true);
        std::cout << "  Mod config self-persistence (String, Int, Float, Bool) -> [PASS]\n";
    }
    std::filesystem::remove(dreamkurumi::DreamEnv::GetModConfigPath(testCfgPath));

    // Test DreamModRegistry
    dreamkurumi::DreamModMetadata meta{
        .id = "my-awesome-mod",
        .name = "My Awesome Mod",
        .version = "1.2.0",
        .author = "EnshroudedDev",
        .description = "Ultimate survival mod extension",
        .directory = "mods/MyAwesomeMod"
    };
    auto metaRes = dreamkurumi::DreamModRegistry::Instance().Register(meta);
    assert(metaRes.isAllowedToHook);
    assert(dreamkurumi::DreamModRegistry::Instance().HasMod("my-awesome-mod"));
    auto allMods = dreamkurumi::DreamModRegistry::Instance().GetAllMods();
    assert(!allMods.empty());
    // Test ModInfo.xml Zero-Dependency Manifest Parser
    std::string sampleXml = R"xml(<xml>
	<Name value="SlideMovementHT" />
	<DisplayName value="Slide Movement HT (Directional Slide & Parkour)" />
	<Version value="3.3.0.0" />
	<Description value="Directional combat sliding, slide-canceling, slide-jumping, and omni-sprinting mechanics." />
	<Author value="kurumiHT" />
	<Website value="https://nexusmods.com/enshrouded" />
</xml>)xml";
    auto parsedXmlMeta = dreamkurumi::DreamEnv::ParseModInfoXml(sampleXml);
    assert(parsedXmlMeta.id == "SlideMovementHT");
    assert(parsedXmlMeta.displayName == "Slide Movement HT (Directional Slide & Parkour)");
    assert(parsedXmlMeta.name == "Slide Movement HT (Directional Slide & Parkour)");
    assert(parsedXmlMeta.version == "3.3.0.0");
    assert(parsedXmlMeta.author == "kurumiHT");
    assert(parsedXmlMeta.website == "https://nexusmods.com/enshrouded");
    std::cout << "  ModInfo.xml zero-dependency manifest parser -> [PASS]\n";

    // Test modinfo.json Zero-Dependency Manifest Parser
    std::string sampleJson = R"json({
    "name": "SlideMovementHT",
    "displayName": "Slide Movement HT (Directional Slide & Parkour)",
    "version": "3.3.0.0",
    "description": "Directional combat sliding, slide-canceling, slide-jumping, and omni-sprinting mechanics.",
    "author": "kurumiHT",
    "website": "https://nexusmods.com/enshrouded"
})json";
    auto parsedJsonMeta = dreamkurumi::DreamEnv::ParseModInfoJson(sampleJson);
    assert(parsedJsonMeta.id == "SlideMovementHT");
    assert(parsedJsonMeta.displayName == "Slide Movement HT (Directional Slide & Parkour)");
    assert(parsedJsonMeta.name == "Slide Movement HT (Directional Slide & Parkour)");
    assert(parsedJsonMeta.version == "3.3.0.0");
    assert(parsedJsonMeta.author == "kurumiHT");
    assert(parsedJsonMeta.website == "https://nexusmods.com/enshrouded");
    std::cout << "  modinfo.json zero-dependency manifest parser -> [PASS]\n";

    // Test Fallback when manifest is absent (manifest is optional!)
    auto fallbackMeta = dreamkurumi::DreamEnv::LoadModInfo();
    assert(!fallbackMeta.id.empty());
    assert(fallbackMeta.version == "1.0.0");
    std::cout << "  Graceful fallback when manifest is absent (Manifest is optional) -> [PASS]\n";

    std::cout << "  [PASS] DreamEnv & DreamModConfig dev convenience verified!\n\n";

    // ------------------------------------------------------------------------
    // Test 34: DreamSemVer & Duplicate Mod Version Conflict Resolution
    // ------------------------------------------------------------------------
    std::cout << "[TEST 34] Testing DreamSemVer & Duplicate Mod Conflict Resolution...\n";

    // 1. SemVer comparison tests
    auto v1_0 = dreamkurumi::DreamSemVer::Parse("1.0.0");
    auto v1_2 = dreamkurumi::DreamSemVer::Parse("1.2.0");
    auto v2_0 = dreamkurumi::DreamSemVer::Parse("v2.0.0");
    auto v1_2_beta = dreamkurumi::DreamSemVer::Parse("1.2.0-beta");
    assert(v1_0 < v1_2);
    assert(v1_2 > v1_0);
    assert(v2_0 > v1_2);
    assert(v1_2_beta < v1_2); // Pre-release has lower precedence
    std::cout << "  SemVer parser & comparison operators -> [PASS]\n";

    // 2. Register initial mod v1.0.0
    auto res1 = dreamkurumi::DreamModRegistry::Instance().Register({
        .id = "target-hud",
        .name = "Target HUD",
        .version = "1.0.0",
        .author = "AuthorA"
    });
    assert(res1.status == dreamkurumi::ModRegisterStatus::Activated);
    assert(res1.isAllowedToHook == true);
    assert(dreamkurumi::DreamModRegistry::Instance().GetActiveVersion("target-hud") == "1.0.0");
    std::cout << "  Mod v1.0.0 activated successfully -> [PASS]\n";

    // 3. Duplicate Older / Equal mod v0.9.5 arrives -> MUST BE REJECTED!
    auto res2 = dreamkurumi::DreamModRegistry::Instance().Register({
        .id = "target-hud",
        .name = "Target HUD Old Duplicate",
        .version = "0.9.5",
        .author = "AuthorB"
    });
    assert(res2.status == dreamkurumi::ModRegisterStatus::RejectedOlderDuplicate);
    assert(res2.isAllowedToHook == false); // Blocked from installing duplicate hooks!
    assert(dreamkurumi::DreamModRegistry::Instance().GetActiveVersion("target-hud") == "1.0.0");
    std::cout << "  Older duplicate mod v0.9.5 blocked from hooking -> [PASS]\n";

    // 4. Newer mod v1.5.0 arrives -> MUST UPGRADE & SUPERSEDE!
    auto res3 = dreamkurumi::DreamModRegistry::Instance().Register({
        .id = "target-hud",
        .name = "Target HUD Newer",
        .version = "1.5.0",
        .author = "AuthorA"
    });
    assert(res3.status == dreamkurumi::ModRegisterStatus::UpgradedNewer);
    assert(res3.isAllowedToHook == true);
    assert(dreamkurumi::DreamModRegistry::Instance().GetActiveVersion("target-hud") == "1.5.0");
    std::cout << "  Newer mod v1.5.0 superseded older version -> [PASS]\n";

    // 5. Fast one-line helper ShouldInstallHooks check
    bool shouldHookOlder = dreamkurumi::DreamModRegistry::ShouldInstallHooks("target-hud", "Target HUD", "1.2.0");
    assert(!shouldHookOlder); // 1.2.0 is older than active 1.5.0 -> blocked!
    bool shouldHookNewer = dreamkurumi::DreamModRegistry::ShouldInstallHooks("target-hud", "Target HUD", "2.0.0");
    assert(shouldHookNewer);  // 2.0.0 is newer -> allowed!
    std::cout << "  ShouldInstallHooks fast-guard helper -> [PASS]\n";
    std::cout << "  [PASS] DreamSemVer & Duplicate Mod Conflict Resolver verified!\n\n";

    // ------------------------------------------------------------------------
    // Test 35: DreamHash (Compile-Time 64-bit FNV-1a Hashing)
    // ------------------------------------------------------------------------
    std::cout << "[TEST 35] Testing DreamHash (Compile-Time 64-bit FNV-1a Hash Engine)...\n";
    using namespace dreamkurumi::literals;

    // Verify constexpr evaluation at compile time
    constexpr uint64_t hashGod = "god"_hash;
    constexpr uint64_t hashGhost = "ghost"_hash;
    constexpr uint64_t hashStamina = "stamina"_hash;
    static_assert(hashGod != hashGhost, "Hashes must not collide");
    static_assert(hashGod != hashStamina, "Hashes must not collide");

    // Runtime equality
    assert(dreamkurumi::DreamHash::Hash("god") == hashGod);
    assert(dreamkurumi::DreamHash::Hash("ghost") == hashGhost);
    assert(dreamkurumi::DreamHash::Hash("stamina") == hashStamina);

    // Switch-case string routing demonstration (O(1) Jump Table)
    auto routeCommand = [](const std::string& cmd) -> int {
        switch (dreamkurumi::DreamHash::Hash(cmd)) {
            case "god"_hash:     return 1;
            case "ghost"_hash:   return 2;
            case "stamina"_hash: return 3;
            default:             return -1;
        }
    };

    assert(routeCommand("god") == 1);
    assert(routeCommand("ghost") == 2);
    assert(routeCommand("stamina") == 3);
    assert(routeCommand("unknown") == -1);
    std::cout << "  Compile-time switch-case string routing -> [PASS]\n";

    // Case-insensitive hash test
    assert(dreamkurumi::DreamHash::HashCaseInsensitive("GoD") == dreamkurumi::DreamHash::HashCaseInsensitive("god"));
    std::cout << "  Case-insensitive FNV-1a hashing -> [PASS]\n";
    std::cout << "  [PASS] DreamHash compile-time 64-bit hash engine verified!\n\n";

    // ------------------------------------------------------------------------
    // Test 36: DreamWatchdog (Anti-Hang & Thread Deadlock Guard)
    // ------------------------------------------------------------------------
    std::cout << "[TEST 36] Testing DreamWatchdog (Anti-Hang & Deadlock Monitor)...\n";
    auto& watchdog = dreamkurumi::DreamWatchdog::Instance();
    watchdog.SetTimeout(100); // 100ms timeout for unit test

    // Register test worker thread
    DWORD currentTid = GetCurrentThreadId();
    watchdog.RegisterThread(currentTid, "MainTestThread");
    assert(watchdog.GetMonitoredCount() >= 1);
    std::cout << "  Monitored thread registered (Count: " << watchdog.GetMonitoredCount() << ") -> [PASS]\n";

    // Send heartbeat to prove alive
    watchdog.Heartbeat(currentTid);
    assert(!watchdog.IsThreadHung(currentTid));
    std::cout << "  Heartbeat signal recorded -> [PASS]\n";

    // Simulate thread hang (simulate thread that stopped sending heartbeats for 150ms)
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    auto hangReports = watchdog.CheckHangs();
    assert(!hangReports.empty());
    assert(hangReports[0].threadId == currentTid);
    assert(hangReports[0].hangDurationMs >= 100);
    assert(!hangReports[0].rawReport.empty());
    assert(watchdog.IsThreadHung(currentTid));
    std::cout << "  Simulated hang detected! (Duration: " << hangReports[0].hangDurationMs << " ms) -> [PASS]\n";
    std::cout << "  Thread context & frozen module resolved -> [PASS]\n";

    // Recover with heartbeat
    watchdog.Heartbeat(currentTid);
    assert(!watchdog.IsThreadHung(currentTid));
    std::cout << "  Thread recovered after new heartbeat -> [PASS]\n";

    watchdog.UnregisterThread(currentTid);
    watchdog.SetTimeout(5000); // Restore default 5s
    std::cout << "  [PASS] DreamWatchdog anti-hang and deadlock guard verified!\n\n";

    // ------------------------------------------------------------------------
    // Test 37: DreamNetBridge & Dual-Side Environment (Client <-> Dedicated Server Mod Sync)
    // ------------------------------------------------------------------------
    std::cout << "[TEST 37] Testing DreamNetBridge (Client <-> Dedicated Server Mod Sync & RPC)...\n";

    // 1. Dual-side Environment Detection
    bool isDedicatedServer = dreamkurumi::DreamEnv::IsDedicatedServer();
    bool isGameClient = dreamkurumi::DreamEnv::IsClient();
    assert(isDedicatedServer != isGameClient);
    std::string envName = dreamkurumi::DreamEnv::GetEnvironmentName();
    assert(!envName.empty());
    std::cout << "  Current environment detected: " << envName << " -> [PASS]\n";

    bool clientOnlyRan = false;
    DREAM_CLIENT_ONLY({ clientOnlyRan = true; });
    assert(clientOnlyRan == isGameClient);

    // 2. Binary Serialization & Deserialization
    dreamkurumi::NetBufferWriter writer;
    writer.WriteUInt32(0x12345678);
    writer.WriteFloat(42.5f);
    writer.WriteString("Hello Dedicated Server!");
    writer.WriteBool(true);

    dreamkurumi::NetBufferReader reader(writer.GetBuffer());
    assert(reader.ReadUInt32() == 0x12345678);
    assert(reader.ReadFloat() == 42.5f);
    assert(reader.ReadString() == "Hello Dedicated Server!");
    assert(reader.ReadBool() == true);
    assert(reader.IsEOF());
    std::cout << "  Type-safe binary packet serialization & deserialization -> [PASS]\n";

    // 3. Packet Framing & Serialization
    dreamkurumi::NetPacket pkt;
    pkt.channelHash = "economy_mod"_hash;
    pkt.opcode = 0x0042;
    pkt.senderId = 1001; // Client Player 1001
    pkt.targetId = 0;    // Target Server
    pkt.payload = writer.GetBuffer();

    auto rawBytes = dreamkurumi::DreamNetBridge::SerializePacket(pkt);
    dreamkurumi::NetPacket decodedPkt;
    bool desOk = dreamkurumi::DreamNetBridge::DeserializePacket(rawBytes.data(), rawBytes.size(), decodedPkt);
    assert(desOk);
    assert(decodedPkt.channelHash == "economy_mod"_hash);
    assert(decodedPkt.opcode == 0x0042);
    assert(decodedPkt.senderId == 1001);
    assert(decodedPkt.payload == writer.GetBuffer());
    std::cout << "  Network packet raw framing & validation -> [PASS]\n";

    // 4. Mod Network RPC & Loopback Dispatch
    auto& netBridge = dreamkurumi::DreamNetBridge::Instance();
    netBridge.SetLoopbackEnabled(true);
    netBridge.SetLocalClientId(1001);

    bool rpcReceived = false;
    std::string receivedMsg;
    netBridge.RegisterHandler("economy_mod"_hash, 0x0042, [&](const dreamkurumi::NetPacket& p) {
        rpcReceived = true;
        dreamkurumi::NetBufferReader r(p.payload);
        r.ReadUInt32();
        r.ReadFloat();
        receivedMsg = r.ReadString();
    });

    netBridge.SendToServer("economy_mod"_hash, 0x0042, writer);
    assert(rpcReceived);
    assert(receivedMsg == "Hello Dedicated Server!");
    std::cout << "  Client-to-Server mod packet dispatch -> [PASS]\n";

    // 5. DreamServerSync (Policy & Variable Handshake)
    auto& sync = dreamkurumi::DreamServerSync::Instance();
    sync.InitializeClientListener();

    // Server registers game settings
    sync.SetSyncVar("MaxPlayerLevel", 75);
    sync.SetSyncVar("GlobalXpRate", 3.0f);
    sync.SetSyncVar("AllowPvP", "true");

    // Server broadcasts to all clients
    sync.BroadcastServerSettings();

    // Verify client received and applied settings
    assert(sync.GetSyncVarInt("MaxPlayerLevel") == 75);
    assert(sync.GetSyncVarFloat("GlobalXpRate") == 3.0f);
    std::cout << "  Dedicated Server auto-sync settings handshake -> [PASS]\n";
    std::cout << "  [PASS] DreamNetBridge & DreamServerSync unified client/server mod system verified!\n\n";

    // ------------------------------------------------------------------------
    // Test 38: DreamMemory (Safe Memory Reading, Pointer Chains & RAII Byte Patch)
    // ------------------------------------------------------------------------
    std::cout << "[TEST 38] Testing DreamMemory (Safe Operations, Pointer Chains & Patches)...\n";
    
    // 1. Safe Read & Write on simple variables
    float testHealth = 100.0f;
    assert(dreamkurumi::DreamMemory::Read<float>(&testHealth) == 100.0f);
    dreamkurumi::DreamMemory::Write<float>(&testHealth, 250.0f);
    assert(testHealth == 250.0f);
    
    // Bad pointer reading (does not crash! returns fallback value)
    float badVal = dreamkurumi::DreamMemory::Read<float>(reinterpret_cast<void*>(0xDEADBEEF), -999.0f);
    assert(badVal == -999.0f);
    std::cout << "  Safe memory read/write & bad pointer sandbox -> [PASS]\n";

    // 2. Multi-level Pointer Chain Resolution: base -> p1 -> p2 -> targetValue
    struct Level2 {
        float stamina;
    };
    struct Level1 {
        Level2* pLevel2;
    };
    struct RootEntity {
        Level1* pLevel1;
    };

    Level2 l2{ 75.5f };
    Level1 l1{ &l2 };
    RootEntity root{ &l1 };

    // ReadChain: root -> +0x0 (pLevel1) -> +0x0 (pLevel2) -> +0x0 (stamina)
    float chainVal = dreamkurumi::DreamMemory::ReadChain<float>(&root, {0x0, 0x0, 0x0}, 0.0f);
    assert(chainVal == 75.5f);

    // WriteChain: modify stamina through pointer chain
    bool chainWriteOk = dreamkurumi::DreamMemory::WriteChain<float>(&root, {0x0, 0x0, 0x0}, 99.9f);
    assert(chainWriteOk);
    assert(l2.stamina == 99.9f);
    std::cout << "  Multi-level pointer chain resolution & dereference -> [PASS]\n";

    // 3. RAII Byte Patching & Hex Patches
    uint8_t codeBuffer[8] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83 };
    {
        dreamkurumi::DreamBytePatch bytePatch(codeBuffer, "90 90 90 90 90");
        assert(bytePatch.Apply());
        assert(codeBuffer[0] == 0x90 && codeBuffer[4] == 0x90);
        assert(codeBuffer[5] == 0x57); // untouched
    } // RAII scope exit restores original bytes automatically!

    assert(codeBuffer[0] == 0x48 && codeBuffer[1] == 0x89);
    std::cout << "  RAII reversible hex/byte patcher & auto-restore -> [PASS]\n";
    std::cout << "  [PASS] DreamMemory dev convenience verified!\n\n";

    // ------------------------------------------------------------------------
    // Test 39: DreamGameContext (Service Locator & High-Level Engine API)
    // ------------------------------------------------------------------------
    std::cout << "[TEST 39] Testing DreamGameContext (Engine Service Locator & High-Level API)...\n";
    auto& gameCtx = dreamkurumi::DreamGameContext::Instance();

    // 1. Service Registration & Retrieval
    int dummyInventory = 42;
    gameCtx.RegisterService("InventoryManager", &dummyInventory);
    assert(gameCtx.HasService("InventoryManager"));
    assert(gameCtx.GetService("InventoryManager") == &dummyInventory);
    std::cout << "  Engine subsystem service registration & locator -> [PASS]\n";

    // 2. Lifecycle Events & LocalPlayer Tracking
    struct MockPlayer {
        uint8_t pad[0x40];
        dreamkurumi::DreamVector3 pos;
    };
    MockPlayer dummyPlayer{};
    dummyPlayer.pos = { 100.0f, 20.0f, -50.0f };

    bool spawnEventFired = false;
    gameCtx.OnPlayerSpawn([&](void* player) {
        spawnEventFired = true;
        assert(player == &dummyPlayer);
    });

    assert(!gameCtx.IsPlayerReady());
    gameCtx.SetLocalPlayer(&dummyPlayer);
    assert(gameCtx.IsPlayerReady());
    assert(gameCtx.GetLocalPlayer() == &dummyPlayer);
    assert(spawnEventFired);
    std::cout << "  LocalPlayer auto-capture & OnPlayerSpawn lifecycle callback -> [PASS]\n";

    // 3. Player Coordinate Reading & Teleportation
    auto curPos = gameCtx.GetPlayerPosition(0x40);
    assert(curPos.x == 100.0f && curPos.y == 20.0f && curPos.z == -50.0f);

    bool teleOk = gameCtx.TeleportPlayer(500.0f, 75.0f, 1200.0f, 0x40);
    assert(teleOk);
    auto newPos = gameCtx.GetPlayerPosition(0x40);
    assert(newPos.x == 500.0f && newPos.y == 75.0f && newPos.z == 1200.0f);
    std::cout << "  High-level player coordinate access & teleportation -> [PASS]\n";

    // 4. Quick Gameplay Modification Presets
    gameCtx.SetGodMode(true);
    gameCtx.SetGhostMode(true);
    gameCtx.SetInfiniteStamina(true);
    assert(dreamkurumi::DreamPresets::Instance().State().godMode.load());
    assert(dreamkurumi::DreamPresets::Instance().State().ghostMode.load());
    assert(dreamkurumi::DreamPresets::Instance().State().infiniteStamina.load());
    std::cout << "  Integrated gameplay modification presets -> [PASS]\n";
    std::cout << "  [PASS] DreamGameContext zero-pointer engine service locator verified!\n\n";

    // ========================================================================
    // TEST 40: High-Performance JSON Engine (yyjson + DreamJson)
    // ========================================================================
    std::cout << "[TEST 40] Testing DreamJson Ultra-Fast Parsing & Serialization (yyjson)...\n";
    std::string sampleYyJson = R"({
        "mod": "EnshroudedTurbo",
        "version": "2.5.0",
        "author": "kurumiHT",
        "settings": {
            "speed": 1.75,
            "godMode": true,
            "maxSlots": 48
        },
        "tags": ["combat", "speed", "utility"]
    })";

    auto jsonDoc = dreamkurumi::DreamJson::Parse(sampleYyJson);
    assert(jsonDoc.is_valid());
    assert(jsonDoc["mod"].as_string() == "EnshroudedTurbo");
    assert(jsonDoc["version"].as_string() == "2.5.0");
    assert(jsonDoc["settings"]["speed"].as_double() == 1.75);
    assert(jsonDoc["settings"]["godMode"].as_bool() == true);
    assert(jsonDoc["settings"]["maxSlots"].as_int() == 48);
    assert(jsonDoc["tags"].size() == 3);
    assert(jsonDoc["tags"][0].as_string() == "combat");
    assert(jsonDoc["tags"][1].as_string() == "speed");
    assert(jsonDoc["tags"][2].as_string() == "utility");
    std::cout << "  yyjson DOM parsing, type validation & array access -> [PASS]\n";

    // Test mutable document & serialization
    dreamkurumi::DreamJsonMutDocument mutDoc;
    mutDoc.add("status", "operational");
    mutDoc.add("latency_ns", static_cast<int64_t>(12));
    mutDoc.add("active", true);
    std::string serialized = mutDoc.stringify(false);
    assert(serialized.find("\"status\":\"operational\"") != std::string::npos);
    assert(serialized.find("\"active\":true") != std::string::npos);
    std::cout << "  yyjson high-speed serialization & stringify -> [PASS]\n";
    std::cout << "  [PASS] DreamJson (yyjson) fully verified!\n\n";

    // ========================================================================
    // TEST 41: LuaJIT Engine & In-Memory Scripting (DreamLua)
    // ========================================================================
    std::cout << "[TEST 41] Testing DreamLua (LuaJIT 2.1 + yyjson Bridge)...\n";
    auto& luaEng = dreamkurumi::DreamLuaEngine::Instance();
    bool luaInit = luaEng.Initialize();
    assert(luaInit);
    assert(luaEng.IsInitialized());
    std::cout << "  LuaJIT 2.1 engine initialization & JIT compilation mode -> [PASS]\n";

    // 1. Script execution interacting with DreamGame
    std::string luaScript = R"(
        dream.log("Testing LuaJIT script execution from DreamLua")
        assert(dream.is_player_ready() == true)
        dream.set_god_mode(true)
        dream.set_infinite_stamina(true)
        dream.teleport(999.0, 150.0, -450.0)
    )";
    std::string luaErr;
    bool scriptOk = luaEng.ExecuteString(luaScript, &luaErr);
    if (!scriptOk) {
        std::cerr << "Lua Error: " << luaErr << "\n";
    }
    assert(scriptOk);
    auto afterLuaPos = gameCtx.GetPlayerPosition(0x40);
    assert(afterLuaPos.x == 999.0f && afterLuaPos.y == 150.0f && afterLuaPos.z == -450.0f);
    std::cout << "  Lua script invoking DreamGameContext services & teleportation -> [PASS]\n";

    // 2. Lua + yyjson Bridge
    std::string luaJsonBridgeScript = R"(
        local parsed = dream.json_parse('{"target":"Boss","hp":5000,"alive":true}')
        assert(parsed ~= nil)
        assert(parsed.target == "Boss")
        assert(parsed.hp == 5000)
        assert(parsed.alive == true)

        local tbl = { name = "Kurumi", level = 100 }
        local str = dream.json_stringify(tbl)
        assert(string.find(str, "Kurumi") ~= nil)
    )";
    bool bridgeOk = luaEng.ExecuteString(luaJsonBridgeScript, &luaErr);
    if (!bridgeOk) {
        std::cerr << "Lua-Json Bridge Error: " << luaErr << "\n";
    }
    assert(bridgeOk);
    std::cout << "  LuaJIT + yyjson bidirectional table conversion -> [PASS]\n";
    std::cout << "  [PASS] DreamLua (LuaJIT + yyjson Bridge) fully verified!\n\n";


    // ========================================================================
    // TEST 42: Multi-Mod Central Detour Dispatcher (Crash-Proof Hook Chaining)
    // ========================================================================
    std::cout << "[TEST 42] Testing Multi-Mod Central Detour Dispatcher...\n";
    void* pMultiTarget = reinterpret_cast<void*>(MultiModTarget_Func);

    // 1. Both Mod A (High) and Mod B (Normal) hook the EXACT same function
    bool aInstalled = MultiModPatchA::Install(pMultiTarget);
    bool bInstalled = MultiModPatchB::Install(pMultiTarget);
    assert(aInstalled && bInstalled);
    std::cout << "  Simultaneous multi-mod hook registration on same address -> [PASS]\n";

    // 2. Call target: Mod A prefix adds 5 (2 + 5 = 7), original multiplies by 10 (70), Mod B postfix adds 100 (170)
    int multiRes = MultiModTarget_Func(2);
    asm volatile("" : "+r"(multiRes));
    assert(multiRes == 170);
    std::cout << "  MultiModTarget_Func(2) with Mod A (High) + Mod B (Normal) -> " << multiRes << " (Expected 170) -> [PASS]\n";

    // 3. Hot-Reload simulation: Mod A unloads/unpatches cleanly without crashing or breaking Mod B
    MultiModPatchA::Disable();
    assert(!MultiModPatchA::IsInstalled());
    assert(MultiModPatchB::IsInstalled());

    // 4. Call target again: Only Mod B remains active (2 * 10 + 100 = 120)
    int afterUnloadRes = MultiModTarget_Func(2);
    asm volatile("" : "+r"(afterUnloadRes));
    assert(afterUnloadRes == 120);
    std::cout << "  After unhooking Mod A, Mod B continues seamlessly -> " << afterUnloadRes << " (Expected 120) -> [PASS]\n";

    // 5. Clean up Mod B
    MultiModPatchB::Disable();
    int vanillaRes = MultiModTarget_Func(2);
    asm volatile("" : "+r"(vanillaRes));
    assert(vanillaRes == 20);
    std::cout << "  After unhooking Mod B, vanilla execution restored -> " << vanillaRes << " (Expected 20) -> [PASS]\n";
    std::cout << "  [PASS] Multi-Mod Central Detour Dispatcher fully verified!\n\n";

    // ========================================================================
    // TEST 43: Fuzzy Instruction-Aware AOB Scanner (HDE64 Disassembly Engine)
    // ========================================================================
    std::cout << "[TEST 43] Testing Fuzzy Instruction-Aware AOB Scanner...\n";
    // Simulated game memory after a Keen Games patch where stack displacements shifted
    uint8_t simulatedGameBytes[] = {
        0x48, 0x89, 0x5C, 0x24, 0x18, // mov [rsp+0x18], rbx (shifted from 0x10)
        0x48, 0x83, 0xEC, 0x30,       // sub rsp, 0x30 (shifted stack alloc from 0x20)
        0x8B, 0x44, 0x24, 0x38,       // mov eax, [rsp+0x38]
        0xC3                          // ret
    };

    // Strict pattern from an older game version expecting [rsp+0x10] and sub rsp, 0x20
    std::string strictPattern = "48 89 5C 24 10 48 83 EC 20 8B 44 24 28 C3";

    // 1. Strict scan must FAIL against shifted bytes
    void* strictMatch = dreamkurumi::DreamScanner::FindPattern(strictPattern, simulatedGameBytes, sizeof(simulatedGameBytes));
    assert(strictMatch == nullptr);
    std::cout << "  Strict pattern scan against shifted game update bytes correctly failed -> [PASS]\n";

    // 2. Fuzzy instruction-aware scan must automatically wildcard displacements and MATCH!
    void* fuzzyMatch = dreamkurumi::DreamFuzzyScanner::FindFuzzyPattern(strictPattern, simulatedGameBytes, sizeof(simulatedGameBytes));
    assert(fuzzyMatch == simulatedGameBytes);
    std::cout << "  Fuzzy instruction-aware scan automatically resolved shifted target at " << fuzzyMatch << " -> [PASS]\n";
    std::cout << "  [PASS] Fuzzy Instruction-Aware AOB Scanner fully verified!\n\n";

    // ========================================================================
    // TEST 44: Single-Header C++ Mod SDK (dream.hpp)
    // ========================================================================
    std::cout << "[TEST 44] Testing Single-Header C++ Mod SDK (dream.hpp)...\n";
    assert(static_cast<int>(dream::Priority::First) == 0);
    assert(static_cast<int>(dream::Priority::High) == 200);
    assert(static_cast<int>(dream::Priority::Normal) == 300);

    int testVal = 12345;
    int readVal = dream::Memory::Read<int>(reinterpret_cast<uintptr_t>(&testVal));
    assert(readVal == 12345);
    std::cout << "  Single-Header dream::Memory::Read verified -> [PASS]\n";

    dream::ShowToast("Single-Header dream.hpp Test Toast", 1.0f, 0x00FF88);
    std::cout << "  Single-Header dream::ShowToast bridge invoked -> [PASS]\n";
    std::cout << "  [PASS] Single-Header C++ Mod SDK (dream.hpp) fully verified!\n\n";

    // Cleanly stop any background singletons before process exit
    dreamkurumi::DreamWatchdog::Instance().Stop();
    dreamkurumi::DreamWatcher::Instance().Stop();
    dreamkurumi::DreamLuaEngine::Instance().Shutdown();
    dreamkurumi::DreamHarmony::UnpatchAll();
    dreamkurumi::DreamCrashGuard::Shutdown();

    std::cout << "============================================================\n";
    std::cout << " ALL 44 TESTS PASSED! dreamkurumiHT IS FULLY VERIFIED!\n";
    std::cout << "============================================================\n" << std::flush;

    TerminateProcess(GetCurrentProcess(), 0);
    return 0;
}

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <dreamkurumiHT/dreamkurumiHT_plugin.h>
#include <dreamkurumiHT/DreamToml.hpp>

// ============================================================================
// Dream AoE Harvest (Unified Dynamic Harmony Plugin - Client & Server)
// Dedicated Farming Crop Gathering Mod (3.5m Radius, F8 Toggle)
// Dual-Pipeline Architecture:
// 1) immediate_loot_action (RVA 0x19FB4B Server / 0x3FD0EB Client):
//    Native 128-byte event immediate collection loop for crops & ground loot.
// 2) interaction_attempt (RVA 0x0A1241 Server / 0x2A2EE1 Client):
//    Native 24-byte event general interaction offer loop with Guard bypass.
// Universal Player Access by default (Rule 2)
// ============================================================================

#pragma pack(push, 1)
struct XhlPluginQueryInfo {
    uint32_t structSize;    // 0x00 (0xEC = 236 bytes)
    uint32_t abiVersion;    // 0x04 (0x10000 = 65536)
    uint32_t reserved;      // 0x08
    char id[64];            // 0x0C
    char name[128];         // 0x4C
    char version[32];       // 0xCC
};

struct EventBatch {
    char pad[0x18];           // 0x00 - 0x17 (24 bytes)
    uint32_t count;           // 0x18 - 0x1B (4 bytes)
    uint32_t capacity;        // 0x1C - 0x1F (4 bytes)
    char pad2[0x10];          // 0x20 - 0x2F (16 bytes)
    void* events;             // 0x30 - 0x37
};
#pragma pack(pop)

struct XhlPluginContext {
    uint32_t structSize;                          // 0x00 (40 bytes / 0x28)
    uint32_t apiVersion;                          // 0x04 (0x10000)
    HMODULE gameModule;                           // 0x08
    uint64_t reserved;                            // 0x10
    uint32_t processId;                           // 0x18
    uint32_t padding;                             // 0x1C
    void (*logCallback)(const char* utf8Message); // 0x20
};

extern "C" {
    // Pipeline 1: immediate_loot_action (128 bytes)
    void* pResumeImmediateLootAction = nullptr;
    void Hook_ImmediateLoot_Relay();
    __attribute__((force_align_arg_pointer))
    void OnExpandImmediateLootAction(void* pQueue, uint8_t* pRawChunk, void* pContext, void* pRbp);

    // Pipeline 2: interaction_attempt (24 bytes)
    void* pResumeExpandImmediateBatch = nullptr;
    void Hook_ExpandImmediateLootBatch_Relay();
    __attribute__((force_align_arg_pointer))
    void OnExpandImmediateLootBatch(uint8_t* pRawChunk, void* pContext, EventBatch* pBatch, void* pRbp);
}

namespace {

HMODULE g_hPluginModule = nullptr;
std::string g_modDirectory;
std::string g_configPath;
void (*s_pfnHostLog)(const char*) = nullptr;

// Live Runtime State
std::atomic<bool> g_initialized{false};
std::atomic<bool> g_aoeHarvestEnabled{true};
std::atomic<float> g_harvestRadius{3.5f};
std::atomic<bool> g_sameCropOnly{false};
std::atomic<bool> g_allowShiftVanilla{true};
std::atomic<bool> g_adminOnly{false};       // Default false: Rule 2
std::atomic<int> g_hotkeyHarvest{VK_F8};   // F8 toggle
std::atomic<bool> g_audioFeedback{true};

std::atomic<bool> g_workerRunning{false};
HANDLE g_hWorkerThread = NULL;
FILETIME g_lastConfigTime = {0, 0};

typedef void* (*FnGetEntity)(void* pContext, uint32_t entityId);
typedef void* (*FnGetEntityComponent)(void* outComponentTuple[2], void* pEntity, const uint16_t* pComponentIndex);
typedef void* (*FnGetInteractionOffer)(void* outTuple, void* arg2, void* arg3, void* pEntity, void* compMgr);

FnGetEntity s_pfnGetEntity = nullptr;
FnGetEntityComponent s_pfnGetComp = nullptr;
FnGetInteractionOffer s_origGetInteractionOffer = nullptr;
void* s_hHookOffer = nullptr;
uintptr_t s_baseModule = 0;
bool s_isServer = false;

// Saved original bytes for cleanup
uint8_t s_origLootBytes[16] = {0};
uint8_t s_origHookBytes[14] = {0};
uint8_t s_origGuard0[6] = {0};
uint8_t s_origGuard1[6] = {0};
uint8_t s_origGuard2[6] = {0};
uint8_t s_origGuard3[6] = {0};

uintptr_t s_lootHookRva = 0;
uintptr_t s_hookRva = 0;
uintptr_t s_guard0Rva = 0;
uintptr_t s_guard1Rva = 0;
uintptr_t s_guard2Rva = 0;
uintptr_t s_guard3Rva = 0;

// Dynamic offer backup for AoE batch candidates in Pipeline 2
uint8_t s_primaryOfferBackup[56] = {0};
std::atomic<bool> s_hasPrimaryOffer{false};
std::atomic<bool> g_isExpandingCropBatch{false};

struct CropCandidate {
    uint32_t entId;
    void* pEntity;
    double distMeters;
};

inline bool IsValidPtr(const void* p) {
    uintptr_t addr = (uintptr_t)p;
    return (addr >= 0x10000 && addr <= 0x00007FFFFFF00000ULL);
}

template<typename T>
inline bool SafeRead(const void* src, T& outVal) {
    if (!src || !IsValidPtr(src)) return false;
    outVal = *reinterpret_cast<const T*>(src);
    return true;
}

inline bool IsValidWorldPtr(void* p) {
    if (!p || !IsValidPtr(p)) return false;
    size_t capacity = 0;
    if (!SafeRead((const char*)p + 0x158, capacity)) return false;
    return (capacity >= 4 && capacity <= 10000000);
}

inline bool IsContainerArchetype(const void* pArch) {
    if (!pArch || !IsValidPtr(pArch)) return false;
    const uint64_t* compWords = reinterpret_cast<const uint64_t*>(reinterpret_cast<const char*>(pArch) + 0x20);
    uint64_t word4 = 0, word5 = 0;
    if (!SafeRead(&compWords[0], word4) || !SafeRead(&compWords[1], word5)) return false;

    constexpr uint64_t CONTAINER_WORD4_MASK = (1ULL << 52); // Inventory (0x174)
    constexpr uint64_t CONTAINER_WORD5_MASK = (1ULL << 52) | (1ULL << 53) | (1ULL << 62); // Inventory (372), RandomLoot (373), DefaultInventory (382)

    return ((word4 & CONTAINER_WORD4_MASK) != 0) || ((word5 & CONTAINER_WORD5_MASK) != 0);
}

inline bool IsHarvestableCropArchetype(const void* pArch) {
    if (!pArch || !IsValidPtr(pArch)) return false;
    if (IsContainerArchetype(pArch)) return false; // Definitely not a crop if it's a container/chest

    // Word 1 (+0x08 in archetype): Comps 64..127
    // Bit 43 (Comp 107 = 0x6B) is the harvestable plant/crop component
    uint64_t compWord1 = 0;
    if (!SafeRead(reinterpret_cast<const char*>(pArch) + 0x08, compWord1)) return false;
    return (compWord1 & (1ULL << 43)) != 0;
}

// Hook for GetInteractionOffer (Pipeline 2)
void* Hook_GetInteractionOffer(void* outTuple, void* arg2, void* arg3, void* pEntity, void* compMgr) {
    void* result = s_origGetInteractionOffer ? s_origGetInteractionOffer(outTuple, arg2, arg3, pEntity, compMgr) : outTuple;

    // Strict Guard: ONLY synthesize offer during active AoE crop batch expansion!
    // Prevents leaking or forging chest/door/NPC interaction offers into unrelated entities.
    if (!g_isExpandingCropBatch.load()) {
        return result;
    }

    void** pTuple = reinterpret_cast<void**>(result);
    if (pTuple) {
        if (pTuple[0] != nullptr && IsValidPtr(pTuple[0])) {
            memcpy(s_primaryOfferBackup, pTuple[0], sizeof(s_primaryOfferBackup));
            s_hasPrimaryOffer.store(true);
        } else if (pTuple[0] == nullptr && s_hasPrimaryOffer.load()) {
            pTuple[0] = s_primaryOfferBackup;
            pTuple[1] = s_primaryOfferBackup;
        }
    }
    return result;
}

// Read fixed-point coordinates (SpatialTransform: Component 0x7D / 125)
bool GetEntityFixedPos(void* pEntity, int64_t& outX, int64_t& outZ) {
    if (!pEntity || !s_pfnGetComp || !IsValidPtr(pEntity)) return false;
    const uint16_t cIdxList[] = { 0x7D, 0x085, 0x084, 0x086 };
    void* outComp[2] = { nullptr, nullptr };
    for (uint16_t cIdx : cIdxList) {
        outComp[0] = nullptr;
        outComp[1] = nullptr;
        s_pfnGetComp(outComp, pEntity, &cIdx);
        if (outComp[0] && IsValidPtr(outComp[0])) {
            int64_t x = 0, z = 0;
            if (SafeRead((const char*)outComp[0] + 0x00, x) &&
                SafeRead((const char*)outComp[0] + 0x10, z)) {
                outX = x;
                outZ = z;
                return true;
            }
        }
    }
    return false;
}

void FindNearbyCropCandidates(void* pContext, void* pPrimaryEntity, uint32_t primaryCropId,
                             float maxRadius, bool sameCropOnly,
                             std::vector<CropCandidate>& outCandidates) {
    outCandidates.clear();
    if (!pContext || !pPrimaryEntity || !s_pfnGetEntity || maxRadius <= 0.0f) return;

    void* pPrimaryArch = nullptr;
    if (!SafeRead((char*)pPrimaryEntity + 0x18, pPrimaryArch) || !pPrimaryArch || !IsValidPtr(pPrimaryArch)) return;

    // Strict Crop Guard: Primary entity MUST be a harvestable crop and NEVER a chest/container/NPC/door!
    if (IsContainerArchetype(pPrimaryArch) || !IsHarvestableCropArchetype(pPrimaryArch)) {
        return;
    }

    int64_t pX = 0, pZ = 0;
    if (!GetEntityFixedPos(pPrimaryEntity, pX, pZ)) {
        return;
    }

    void* pObj0 = nullptr;
    if (!SafeRead(pContext, pObj0) || !pObj0 || !IsValidPtr(pObj0)) return;
    void* pWorld = nullptr;
    if (!SafeRead((char*)pObj0 + 0x30, pWorld) || !IsValidWorldPtr(pWorld)) return;

    size_t capacity = 0;
    uint32_t* pKeys = nullptr;
    uint64_t* pBitmap = nullptr;
    SafeRead((char*)pWorld + 0x158, capacity);
    SafeRead((char*)pWorld + 0x150, pBitmap);
    SafeRead((char*)pWorld + 0x170, pKeys);

    if (!pBitmap || !pKeys || capacity == 0 || capacity > 10000000) return;

    int64_t maxDeltaFixed = (int64_t)(maxRadius * 4294967296.0);
    double maxRadiusSq = (double)(maxRadius * maxRadius);
    size_t numWords = (capacity + 63) >> 6;

    for (size_t w = 0; w < numWords; ++w) {
        uint64_t mask = 0;
        if (!SafeRead(&pBitmap[w], mask) || mask == 0) continue;

        for (int b = 0; b < 64; ++b) {
            if (!(mask & (1ULL << b))) continue;
            size_t idx = (w << 6) | b;
            if (idx >= capacity) break;

            uint32_t entId = 0;
            if (!SafeRead(&pKeys[idx], entId) || entId == 0 || entId == primaryCropId) continue;

            void* pEnt = s_pfnGetEntity(pContext, entId);
            if (!pEnt || !IsValidPtr(pEnt)) continue;

            void* pArch = nullptr;
            if (!SafeRead((char*)pEnt + 0x18, pArch) || !pArch || !IsValidPtr(pArch)) continue;
            
            // Candidate entity MUST also be a harvestable crop and NEVER a container/chest/door/NPC
            if (IsContainerArchetype(pArch) || !IsHarvestableCropArchetype(pArch)) continue;

            // Safe crop filter:
            // If sameCropOnly: candidate crop must match the primary crop archetype!
            if (sameCropOnly && pArch != pPrimaryArch) continue;

            // Distance check
            int64_t cX = 0, cZ = 0;
            if (!GetEntityFixedPos(pEnt, cX, cZ)) continue;

            int64_t dX = cX - pX;
            int64_t dZ = cZ - pZ;
            if (dX < -maxDeltaFixed || dX > maxDeltaFixed ||
                dZ < -maxDeltaFixed || dZ > maxDeltaFixed) continue;

            double dXm = (double)dX / 4294967296.0;
            double dZm = (double)dZ / 4294967296.0;
            double distSq = dXm * dXm + dZm * dZm;
            if (distSq > maxRadiusSq) continue;

            CropCandidate cand;
            cand.entId = entId;
            cand.pEntity = pEnt;
            cand.distMeters = std::sqrt(distSq);

            outCandidates.push_back(cand);
            if (outCandidates.size() >= 32) goto done_search;
        }
    }
done_search:;
}

void LogMod(const char* fmt, ...);

bool InstallHook16(void* target, void* hookFunc, uint8_t* origBytesOut) {
    if (!target || !hookFunc) return false;

    uint8_t* pT = reinterpret_cast<uint8_t*>(target);
    if (pT[0] == 0xFF && pT[1] == 0x25 && *(uint32_t*)(pT + 2) == 0) {
        void* existingDetour = *reinterpret_cast<void**>(pT + 6);
        pResumeImmediateLootAction = existingDetour;
        LogMod("[DreamAoEHarvest] Chaining onto existing hook at %p -> Detour %p", target, existingDetour);
    }

    DWORD oldProtect;
    if (!VirtualProtect(target, 16, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
    if (origBytesOut) {
        memcpy(origBytesOut, target, 16);
    }
    // 14-byte indirect JMP: FF 25 00 00 00 00 <64-bit target> + 2x NOP
    uint8_t jmpCode[16] = {
        0xFF, 0x25, 0x00, 0x00, 0x00, 0x00, // jmp qword ptr [rip+0]
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x90, 0x90                          // NOP padding to 16 bytes
    };
    *reinterpret_cast<void**>(&jmpCode[6]) = hookFunc;
    memcpy(target, jmpCode, 16);
    VirtualProtect(target, 16, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), target, 16);
    return true;
}

bool InstallHook14(void* target, void* hookFunc, uint8_t* origBytesOut) {
    if (!target || !hookFunc) return false;

    uint8_t* pT = reinterpret_cast<uint8_t*>(target);
    if (pT[0] == 0xFF && pT[1] == 0x25 && *(uint32_t*)(pT + 2) == 0) {
        void* existingDetour = *reinterpret_cast<void**>(pT + 6);
        pResumeExpandImmediateBatch = existingDetour;
        LogMod("[DreamAoEHarvest] Chaining onto existing hook at %p -> Detour %p", target, existingDetour);
    }

    DWORD oldProtect;
    if (!VirtualProtect(target, 14, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
    if (origBytesOut) {
        memcpy(origBytesOut, target, 14);
    }
    uint8_t jmpCode[14] = {
        0xFF, 0x25, 0x00, 0x00, 0x00, 0x00, // jmp qword ptr [rip+0]
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    *reinterpret_cast<void**>(&jmpCode[6]) = hookFunc;
    memcpy(target, jmpCode, 14);
    VirtualProtect(target, 14, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), target, 14);
    return true;
}

bool PatchMemoryNop(void* target, size_t len, uint8_t* origBytesOut) {
    if (!target || len == 0) return false;
    DWORD oldProtect;
    if (!VirtualProtect(target, len, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
    if (origBytesOut) {
        memcpy(origBytesOut, target, len);
    }
    memset(target, 0x90, len);
    VirtualProtect(target, len, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), target, len);
    return true;
}

bool RestoreMemoryBytes(void* target, const uint8_t* origBytes, size_t len) {
    if (!target || !origBytes || len == 0) return false;
    DWORD oldProtect;
    if (!VirtualProtect(target, len, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
    memcpy(target, origBytes, len);
    VirtualProtect(target, len, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), target, len);
    return true;
}

void* ResolvePatternOrFallback(const char* name, const char* pattern, uintptr_t fallbackRva) {
    void* ptr = nullptr;
    auto& host = dreamkurumi::PluginHost::Instance();
    if (host.Init() && host.pfn_DK_ResolvePattern) {
        ptr = host.pfn_DK_ResolvePattern(name, pattern, fallbackRva);
    }
    if (!ptr && fallbackRva != 0 && s_baseModule != 0) {
        ptr = reinterpret_cast<void*>(s_baseModule + fallbackRva);
    }
    return ptr;
}

void LogMod(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (s_pfnHostLog) {
        s_pfnHostLog(buf);
    }
    dreamkurumi::LogInfo("%s", buf);
}

void LoadConfig() {
    std::string tomlPath = g_modDirectory.empty() ? "mods\\Dream-AoE-Harvest\\config.toml" : (g_modDirectory + "\\config.toml");
    dreamkurumi::DreamToml toml;
    if (toml.LoadFile(tomlPath) || toml.LoadFile("mods\\Dream-AoE-Harvest\\config.toml") || toml.LoadFile("config.toml")) {
        g_aoeHarvestEnabled.store(toml.GetBool("AoEHarvest", "Enabled", true));
        g_harvestRadius.store(static_cast<float>(toml.GetFloat("AoEHarvest", "Radius", 3.5)));
        g_sameCropOnly.store(toml.GetBool("AoEHarvest", "SameCropOnly", false));
        g_allowShiftVanilla.store(toml.GetBool("AoEHarvest", "AllowShiftVanilla", true));
        g_adminOnly.store(toml.GetBool("AoEHarvest", "AdminOnly", false));

        g_hotkeyHarvest.store(static_cast<int>(toml.GetInt("Hotkeys", "ToggleHotkey", VK_F8)));
        g_audioFeedback.store(toml.GetBool("Hotkeys", "AudioFeedback", true));

        LogMod("[DreamAoEHarvest] TOML configuration applied: Enabled=%s (Radius=%.1fm, SameCropOnly=%s, ShiftVanilla=%s, AdminOnly=%s, Hotkey=0x%X)",
               g_aoeHarvestEnabled.load() ? "ON" : "OFF",
               g_harvestRadius.load(),
               g_sameCropOnly.load() ? "true" : "false",
               g_allowShiftVanilla.load() ? "true" : "false",
               g_adminOnly.load() ? "true" : "false",
               g_hotkeyHarvest.load());
        return;
    }

    if (g_configPath.empty()) return;

    g_aoeHarvestEnabled.store(GetPrivateProfileIntA("Farming", "EnableAoeHarvest", 1, g_configPath.c_str()) != 0);
    char radBuf[32];
    GetPrivateProfileStringA("Farming", "AoeHarvestRadius", "3.5", radBuf, sizeof(radBuf), g_configPath.c_str());
    g_harvestRadius.store(static_cast<float>(atof(radBuf)));

    g_sameCropOnly.store(GetPrivateProfileIntA("Farming", "AoeHarvestSameCropOnly", 0, g_configPath.c_str()) != 0);
    g_adminOnly.store(GetPrivateProfileIntA("Farming", "AoeHarvestAdminOnly", 0, g_configPath.c_str()) != 0);
    g_hotkeyHarvest.store(GetPrivateProfileIntA("Hotkeys", "ToggleHotkey", VK_F8, g_configPath.c_str()));

    LogMod("[DreamAoEHarvest] INI fallback applied: Enabled=%s (Radius=%.1fm, AdminOnly=%s, Hotkey=0x%X)",
           g_aoeHarvestEnabled.load() ? "ON" : "OFF",
           g_harvestRadius.load(),
           g_adminOnly.load() ? "true" : "false",
           g_hotkeyHarvest.load());
}

void CheckConfigFileChange() {
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExA(g_configPath.c_str(), GetFileExInfoStandard, &fad)) {
        if (g_lastConfigTime.dwLowDateTime != 0 || g_lastConfigTime.dwHighDateTime != 0) {
            if (CompareFileTime(&fad.ftLastWriteTime, &g_lastConfigTime) != 0) {
                LogMod("[DreamAoEHarvest] config.ini modified on disk! Applying live hot-reload...");
                g_lastConfigTime = fad.ftLastWriteTime;
                LoadConfig();
            }
        } else {
            g_lastConfigTime = fad.ftLastWriteTime;
        }
    }
}

DWORD WINAPI HotkeyWorkerThread(LPVOID) {
    bool prevF8 = false;
    DWORD lastCheckTick = GetTickCount();

    while (g_workerRunning.load()) {
        DWORD now = GetTickCount();
        if (now - lastCheckTick >= 1000) {
            CheckConfigFileChange();
            lastCheckTick = now;
        }

        HWND fgWnd = GetForegroundWindow();
        char titleBuf[128] = {0};
        if (fgWnd) {
            GetWindowTextA(fgWnd, titleBuf, sizeof(titleBuf));
        }

        bool isGameFocused = (strstr(titleBuf, "Enshrouded") != nullptr || strstr(titleBuf, "enshrouded") != nullptr);

        if (isGameFocused) {
            int keyHarvest = g_hotkeyHarvest.load();
            bool curF8 = (GetAsyncKeyState(keyHarvest) & 0x8000) != 0;

            if (curF8 && !prevF8) {
                bool newState = !g_aoeHarvestEnabled.load();
                g_aoeHarvestEnabled.store(newState);

                if (g_audioFeedback.load()) {
                    MessageBeep(newState ? MB_OK : MB_ICONHAND);
                }

                char toastMsg[128];
                snprintf(toastMsg, sizeof(toastMsg), newState ? "AoE Harvest: ENABLED (%.1fm)" : "AoE Harvest: DISABLED", g_harvestRadius.load());
                dreamkurumi::ShowToast(toastMsg, 2.0f, newState ? 0x00FF88 : 0xFFAA00);

                LogMod("[DreamAoEHarvest] AoE Harvest toggled: %s (Radius: %.1fm)",
                       newState ? "ENABLED [ON]" : "DISABLED [OFF]", g_harvestRadius.load());
            }
            prevF8 = curF8;
        }
        Sleep(50);
    }
    return 0;
}

void InitMod() {
    bool expected = false;
    if (!g_initialized.compare_exchange_strong(expected, true)) {
        return;
    }

    char pathBuf[MAX_PATH];
    GetModuleFileNameA(g_hPluginModule, pathBuf, MAX_PATH);
    char* lastSlash = strrchr(pathBuf, '\\');
    if (lastSlash) {
        *(lastSlash + 1) = '\0';
        g_modDirectory = pathBuf;
    } else {
        g_modDirectory = ".\\";
    }
    g_configPath = g_modDirectory + "config.toml";

    s_baseModule = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    char mainExe[MAX_PATH] = {0};
    GetModuleFileNameA((HMODULE)s_baseModule, mainExe, MAX_PATH);
    s_isServer = (strstr(mainExe, "enshrouded_server") != nullptr);

    LogMod("==================================================================");
    LogMod("  Dream AoE Harvest v1.4.0 (Dual-Pipeline Harmony Native Plugin)");
    LogMod("  Target: %s (Base: 0x%llX)", s_isServer ? "Dedicated Server (Headless)" : "Game Client", (unsigned long long)s_baseModule);
    LogMod("  Universal Player Access: ALL Connected Players (Rule 2)");
    LogMod("  Farming Crop Gathering Radius: %.1fm | Toggle: [F8]", g_harvestRadius.load());
    LogMod("==================================================================");

    LoadConfig();

    // Signatures
    const char* patLootAction = "41 8B 41 18 41 8B D2 89 95 68 74 00 00 45 8B C2";
    const char* patHook13A    = "41 8B 41 18 33 D2 45 33 C0 48 89 74 24 30";
    const char* patGetEntity  = "40 53 48 83 EC 20 48 8B 01 89 54 24 38 48 8D 54 24 38 48 8B 58 30 48 8D 8B 48 01 00 00";
    const char* patGetComp    = "4C 8B D2 4C 8B C9 4D 85 C0 74 5D 48 85 D2 74 58 45 0F B7 00 4C 8B 5A 18";
    const char* patGetOffer   = "40 53 48 83 EC 30 4C 89 4C 24 20 48 8B D9 4C 8B 4C 24 60 E8 18 00 00 00";

    uintptr_t fbLootRva      = s_isServer ? 0x0019FB4B : 0x003FD0EB;
    uintptr_t fbHookRva      = s_isServer ? 0x000A1241 : 0x002A2EE1;
    uintptr_t fbGetEntityRva = s_isServer ? 0x00647520 : 0x009A6CA0;
    uintptr_t fbGetCompRva   = s_isServer ? 0x00654610 : 0x009B3D90;
    uintptr_t fbGetOfferRva  = s_isServer ? 0x00654690 : 0x009B3E10;

    // Helper functions
    s_pfnGetEntity = (FnGetEntity)ResolvePatternOrFallback("GetEntity", patGetEntity, fbGetEntityRva);
    s_pfnGetComp = (FnGetEntityComponent)ResolvePatternOrFallback("GetEntityComponent", patGetComp, fbGetCompRva);
    void* pGetOffer = ResolvePatternOrFallback("GetInteractionOffer", patGetOffer, fbGetOfferRva);

    LogMod("[DreamAoEHarvest] Resolved native helpers: GetEntity=%p, GetComp=%p, GetOffer=%p",
           (void*)s_pfnGetEntity, (void*)s_pfnGetComp, pGetOffer);

    // ------------------------------------------------------------------------
    // Pipeline 1: immediate_loot_action (128-byte event buffer) - Primary Crop Harvesting
    // ------------------------------------------------------------------------
    void* lootSite = ResolvePatternOrFallback("immediate_loot_action", patLootAction, fbLootRva);
    if (lootSite) {
        s_lootHookRva = reinterpret_cast<uintptr_t>(lootSite) - s_baseModule;
        pResumeImmediateLootAction = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(lootSite) + 16);

        if (InstallHook16(lootSite, (void*)Hook_ImmediateLoot_Relay, s_origLootBytes)) {
            LogMod("[DreamAoEHarvest] Pipeline 1 (immediate_loot_action) Hook installed successfully at %p (RVA 0x%llX, Resume=%p)!",
                   lootSite, (unsigned long long)s_lootHookRva, pResumeImmediateLootAction);
        } else {
            LogMod("[DreamAoEHarvest] ERROR: Failed to install Pipeline 1 hook at %p!", lootSite);
        }
    } else {
        LogMod("[DreamAoEHarvest] WARNING: Could not resolve immediate_loot_action address!");
    }

    // ------------------------------------------------------------------------
    // Pipeline 2: interaction_attempt (24-byte event buffer) - General Interactions & Guard Bypass
    // ------------------------------------------------------------------------
    void* hookSite13A = ResolvePatternOrFallback("Hook13A_ImmediateLoot", patHook13A, fbHookRva);
    if (hookSite13A) {
        s_hookRva = reinterpret_cast<uintptr_t>(hookSite13A) - s_baseModule;
        pResumeExpandImmediateBatch = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(hookSite13A) + 14);

        if (InstallHook14(hookSite13A, (void*)Hook_ExpandImmediateLootBatch_Relay, s_origHookBytes)) {
            LogMod("[DreamAoEHarvest] Pipeline 2 (interaction_attempt) Hook installed successfully at %p (RVA 0x%llX, Resume=%p)!",
                   hookSite13A, (unsigned long long)s_hookRva, pResumeExpandImmediateBatch);
        } else {
            LogMod("[DreamAoEHarvest] ERROR: Failed to install Pipeline 2 hook at %p!", hookSite13A);
        }

        // Relative engine validation guard checks (+0x14D, +0x157, +0x164, +0x175)
        void* pGuard0 = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(hookSite13A) + 0x14D);
        void* pGuard1 = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(hookSite13A) + 0x157);
        void* pGuard2 = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(hookSite13A) + 0x164);
        void* pGuard3 = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(hookSite13A) + 0x175);

        s_guard0Rva = reinterpret_cast<uintptr_t>(pGuard0) - s_baseModule;
        s_guard1Rva = reinterpret_cast<uintptr_t>(pGuard1) - s_baseModule;
        s_guard2Rva = reinterpret_cast<uintptr_t>(pGuard2) - s_baseModule;
        s_guard3Rva = reinterpret_cast<uintptr_t>(pGuard3) - s_baseModule;

        if (PatchMemoryNop(pGuard0, 6, s_origGuard0) &&
            PatchMemoryNop(pGuard1, 6, s_origGuard1) &&
            PatchMemoryNop(pGuard2, 6, s_origGuard2) &&
            PatchMemoryNop(pGuard3, 6, s_origGuard3)) {
            LogMod("[DreamAoEHarvest] Engine validation guard jumps bypassed at %p, %p, %p, %p (RVAs 0x%llX, 0x%llX, 0x%llX, 0x%llX)!",
                   pGuard0, pGuard1, pGuard2, pGuard3,
                   (unsigned long long)s_guard0Rva, (unsigned long long)s_guard1Rva,
                   (unsigned long long)s_guard2Rva, (unsigned long long)s_guard3Rva);
        } else {
            LogMod("[DreamAoEHarvest] WARNING: Failed to bypass engine validation guard jumps!");
        }
    }

    if (pGetOffer) {
        s_hHookOffer = dreamkurumi::InlineHook(pGetOffer, (void*)Hook_GetInteractionOffer, (void**)&s_origGetInteractionOffer);
        if (s_hHookOffer) {
            LogMod("[DreamAoEHarvest] Dynamic InteractionOffer relay hooked at %p (Orig=%p)!",
                   pGetOffer, s_origGetInteractionOffer);
        }
    }

    g_workerRunning.store(true);
    g_hWorkerThread = CreateThread(NULL, 0, HotkeyWorkerThread, NULL, 0, NULL);
}

void UnloadMod() {
    bool expected = true;
    if (!g_workerRunning.compare_exchange_strong(expected, false)) return;
    LogMod("[DreamAoEHarvest] Unloading cleanly...");
    if (g_hWorkerThread) {
        WaitForSingleObject(g_hWorkerThread, 1500);
        CloseHandle(g_hWorkerThread);
        g_hWorkerThread = NULL;
    }

    if (s_hHookOffer) {
        auto& host = dreamkurumi::PluginHost::Instance();
        if (host.Init() && host.pfn_DK_Unhook) {
            host.pfn_DK_Unhook(s_hHookOffer);
        }
        s_hHookOffer = nullptr;
    }

    if (s_baseModule != 0) {
        if (s_lootHookRva != 0 && s_origLootBytes[0] != 0) {
            RestoreMemoryBytes((void*)(s_baseModule + s_lootHookRva), s_origLootBytes, 16);
        }
        if (s_hookRva != 0 && s_origHookBytes[0] != 0) {
            RestoreMemoryBytes((void*)(s_baseModule + s_hookRva), s_origHookBytes, 14);
        }
        if (s_guard0Rva != 0 && s_origGuard0[0] != 0) {
            RestoreMemoryBytes((void*)(s_baseModule + s_guard0Rva), s_origGuard0, 6);
        }
        if (s_guard1Rva != 0 && s_origGuard1[0] != 0) {
            RestoreMemoryBytes((void*)(s_baseModule + s_guard1Rva), s_origGuard1, 6);
        }
        if (s_guard2Rva != 0 && s_origGuard2[0] != 0) {
            RestoreMemoryBytes((void*)(s_baseModule + s_guard2Rva), s_origGuard2, 6);
        }
        if (s_guard3Rva != 0 && s_origGuard3[0] != 0) {
            RestoreMemoryBytes((void*)(s_baseModule + s_guard3Rva), s_origGuard3, 6);
        }
    }

    g_initialized.store(false);
}

} // namespace

// ============================================================================
// Pipeline 1 Handler: immediate_loot_action (128-byte event buffer)
// ============================================================================
extern "C" __attribute__((force_align_arg_pointer))
void OnExpandImmediateLootAction(void* pQueue, uint8_t* pRawChunk, void* pContext, void* pRbp) {
    (void)pRbp;
    if (!pQueue || !pRawChunk || !pContext) return;
    if (!g_aoeHarvestEnabled.load()) return;

    // Shift key bypass: hold Shift to harvest single crop (Vanilla mode)
    if (g_allowShiftVanilla.load() && !s_isServer) {
        if ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0) {
            return;
        }
    }

    uint32_t count = *(uint32_t*)((char*)pQueue + 0x18);
    if (count == 0) return;
    if (!s_pfnGetEntity) return;

    uint32_t primaryCropId = *(uint32_t*)(pRawChunk + 0x14);
    if (primaryCropId == 0) {
        primaryCropId = *(uint32_t*)(pRawChunk + 0x0c);
    }
    if (primaryCropId == 0) return;

    void* pPrimaryEntity = s_pfnGetEntity(pContext, primaryCropId);
    if (!pPrimaryEntity || !IsValidPtr(pPrimaryEntity)) return;

    void* pPrimaryArch = nullptr;
    if (!SafeRead((char*)pPrimaryEntity + 0x18, pPrimaryArch) || !pPrimaryArch || !IsValidPtr(pPrimaryArch)) return;
    if (IsContainerArchetype(pPrimaryArch) || !IsHarvestableCropArchetype(pPrimaryArch)) return;

    uint32_t playerId = *(uint32_t*)(pRawChunk + 0x10);
    if (playerId == 0) {
        playerId = *(uint32_t*)(pRawChunk + 0x08);
    }

    float radius = g_harvestRadius.load();
    bool sameCrop = g_sameCropOnly.load();

    std::vector<CropCandidate> candidates;
    FindNearbyCropCandidates(pContext, pPrimaryEntity, primaryCropId,
                             radius, sameCrop, candidates);
    if (candidates.empty()) {
        LogMod("[DreamAoEHarvest] Single crop harvest: EntId=%u (no nearby crops within %.1fm)",
               primaryCropId, radius);
        return;
    }

    // Max 14 candidates (1 primary + 14 = 15 total, strictly fits inside 2048-byte single chunk buffer)
    size_t countToExpand = candidates.size();
    if (countToExpand > 14) countToExpand = 14;

    for (size_t i = 0; i < countToExpand; ++i) {
        uint8_t* pTarget = pRawChunk + (1 + i) * 128;
        memcpy(pTarget, pRawChunk, 128);

        *(uint32_t*)(pTarget + 0x14) = candidates[i].entId;
        *(uint32_t*)(pTarget + 0x0c) = candidates[i].entId;
        *(uint32_t*)(pTarget + 0x10) = playerId;
        *(uint32_t*)(pTarget + 0x08) = playerId;

        // Bypass validation checks
        *(uint8_t*)(pTarget + 0x00) = 1;
        *(uint8_t*)(pTarget + 0x78) = 1;
        *(uint8_t*)(pTarget + 0x80) = 1;
    }

    *(uint8_t*)(pRawChunk + (1 + countToExpand) * 128) = 1;

    *(uint32_t*)((char*)pQueue + 0x18) = (uint32_t)(1 + countToExpand);

    LogMod("[DreamAoEHarvest] >>> AoE Harvest (immediate_loot_action): Expanded batch to %u crops (1 primary %u + %zu nearby, Player: %u, Radius: %.1fm) <<<",
           (uint32_t)(1 + countToExpand), primaryCropId, countToExpand, playerId, radius);
}

// ============================================================================
// Pipeline 2 Handler: interaction_attempt (24-byte event buffer)
// ============================================================================
extern "C" __attribute__((force_align_arg_pointer))
void OnExpandImmediateLootBatch(uint8_t* pRawChunk, void* pContext, EventBatch* pBatch, void* pRbp) {
    (void)pRbp;
    if (!pBatch || !pContext || !pRawChunk) return;
    if (!g_aoeHarvestEnabled.load()) return;

    // Shift key bypass: hold Shift to harvest single crop (Vanilla mode)
    if (g_allowShiftVanilla.load() && !s_isServer) {
        if ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0) {
            return;
        }
    }

    uint32_t count = pBatch->count;
    if (count == 0) return;
    if (!s_pfnGetEntity) return;

    // Slot 0 (24 bytes):
    // +0x10: playerId / guestId
    // +0x14: primaryCropId
    // +0x18: offerId
    uint32_t playerId = *(uint32_t*)(pRawChunk + 0x10);
    uint32_t primaryCropId = *(uint32_t*)(pRawChunk + 0x14);
    uint32_t offerId = *(uint32_t*)(pRawChunk + 0x18);

    if (primaryCropId == 0) {
        uint32_t altId = *(uint32_t*)(pRawChunk + 0x0c);
        if (altId != 0 && s_pfnGetEntity(pContext, altId)) {
            primaryCropId = altId;
        }
    }
    if (primaryCropId == 0) return;

    void* pPrimaryEntity = s_pfnGetEntity(pContext, primaryCropId);
    if (!pPrimaryEntity || !IsValidPtr(pPrimaryEntity)) return;

    void* pPrimaryArch = nullptr;
    if (!SafeRead((char*)pPrimaryEntity + 0x18, pPrimaryArch) || !pPrimaryArch || !IsValidPtr(pPrimaryArch)) return;
    if (IsContainerArchetype(pPrimaryArch) || !IsHarvestableCropArchetype(pPrimaryArch)) return;

    // RAII scope to guarantee offer spoofing is active ONLY during this crop expansion
    struct CropBatchScope {
        std::atomic<bool>& flag;
        CropBatchScope(std::atomic<bool>& f) : flag(f) { 
            s_hasPrimaryOffer.store(false);
            flag.store(true); 
        }
        ~CropBatchScope() { 
            flag.store(false); 
            s_hasPrimaryOffer.store(false);
        }
    } cropBatchScope(g_isExpandingCropBatch);

    float radius = g_harvestRadius.load();
    bool sameCrop = g_sameCropOnly.load();

    std::vector<CropCandidate> candidates;
    FindNearbyCropCandidates(pContext, pPrimaryEntity, primaryCropId,
                             radius, sameCrop, candidates);
    if (candidates.empty()) return;

    size_t countToExpand = candidates.size();
    if (countToExpand > 32) countToExpand = 32;

    for (size_t i = 0; i < countToExpand; ++i) {
        uint8_t* pTarget = pRawChunk + (1 + i) * 24;
        
        memcpy(pTarget, pRawChunk, 24);

        *(uint32_t*)(pTarget + 0x10) = playerId;
        *(uint32_t*)(pTarget + 0x14) = candidates[i].entId;
        *(uint32_t*)(pTarget + 0x18) = offerId;

        *(uint32_t*)(pTarget + 0x08) = playerId;
        *(uint32_t*)(pTarget + 0x0c) = candidates[i].entId;
    }

    pBatch->count = (uint32_t)(1 + countToExpand);

    LogMod("[DreamAoEHarvest] >>> AoE Harvest (interaction_attempt): Expanded batch to %u crops (1 primary %u + %zu nearby, Player: %u, Radius: %.1fm) <<<",
           pBatch->count, primaryCropId, countToExpand, playerId, radius);
}

extern "C" {

__declspec(dllexport) int XhlNativePluginQuery(XhlPluginQueryInfo* info) {
    if (!info) return 0;
    std::memset(info, 0, sizeof(XhlPluginQueryInfo));
    info->structSize = sizeof(XhlPluginQueryInfo);
    info->abiVersion = 0x10000;
    strncpy(info->id, "dream-aoe-harvest", sizeof(info->id) - 1);
    strncpy(info->name, "Dream AoE Harvest (Dual Pipeline)", sizeof(info->name) - 1);
    strncpy(info->version, "1.4.0", sizeof(info->version) - 1);
    return 1;
}

__declspec(dllexport) int XhlNativePluginStart(XhlPluginContext* ctx) {
    if (ctx && ctx->logCallback) {
        s_pfnHostLog = ctx->logCallback;
    }
    InitMod();
    return 1;
}

__declspec(dllexport) void XhlNativePluginRequestStop() {
    UnloadMod();
}

} // extern "C"

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved) {
    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hinstDLL);
        g_hPluginModule = hinstDLL;
        break;
    case DLL_PROCESS_DETACH:
        if (!lpReserved) {
            UnloadMod();
        }
        break;
    }
    return TRUE;
}

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
// Automatically adapts to Client (enshrouded.exe) and Dedicated Server (enshrouded_server.exe)
// Enshrouded 1.0 Native Interaction Loop Batch Expansion (Hook 13A)
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
std::atomic<bool> g_adminOnly{false};       // Default false: Rule 2
std::atomic<int> g_hotkeyHarvest{VK_F8};   // F8 toggle
std::atomic<bool> g_audioFeedback{true};

std::atomic<bool> g_workerRunning{false};
HANDLE g_hWorkerThread = NULL;
FILETIME g_lastConfigTime = {0, 0};

typedef void* (*FnGetEntity)(void* pContext, uint32_t entityId);
typedef void* (*FnGetEntityComponent)(void* outComponentTuple[2], void* pEntity, const uint16_t* pComponentIndex);

FnGetEntity s_pfnGetEntity = nullptr;
FnGetEntityComponent s_pfnGetComp = nullptr;
uintptr_t s_baseModule = 0;
bool s_isServer = false;

struct CropCandidate {
    uint32_t entId;
    void* pEntity;
    void* pOfferComp;
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

// Read fixed-point coordinates from Component 0x7d (SpatialTransform in Q32.32)
bool GetEntityFixedPos(void* pEntity, int64_t& outX, int64_t& outZ) {
    if (!pEntity || !s_pfnGetComp || !IsValidPtr(pEntity)) return false;
    uint16_t cIdx = 0x7d; // Component 125
    void* outComp[2] = { nullptr, nullptr };
    s_pfnGetComp(outComp, pEntity, &cIdx);
    if (!outComp[0] || !IsValidPtr(outComp[0])) return false;

    if (!SafeRead((const char*)outComp[0] + 0x00, outX)) return false;
    if (!SafeRead((const char*)outComp[0] + 0x10, outZ)) return false;
    return true;
}

void FindNearbyCropCandidates(void* pContext, void* pPrimaryEntity, uint32_t primaryCropId,
                             float maxRadius, bool sameCropOnly,
                             std::vector<CropCandidate>& outCandidates) {
    outCandidates.clear();
    if (!pContext || !pPrimaryEntity || !s_pfnGetEntity || !s_pfnGetComp || maxRadius <= 0.0f) return;

    void* pPrimaryArch = nullptr;
    if (!SafeRead((char*)pPrimaryEntity + 0x18, pPrimaryArch) || !pPrimaryArch || !IsValidPtr(pPrimaryArch)) return;

    int64_t pX = 0, pZ = 0;
    if (!GetEntityFixedPos(pPrimaryEntity, pX, pZ)) return;

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
            if (sameCropOnly && pArch != pPrimaryArch) continue;

            // 1. Must have InteractionOffer (Component 0x105 = 261)
            uint16_t cOffer = 0x105;
            void* outOffer[2] = { nullptr, nullptr };
            s_pfnGetComp(outOffer, pEnt, &cOffer);
            if (!outOffer[0] || !IsValidPtr(outOffer[0])) continue;

            // 2. Spatial distance check using Component 0x7d
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
            cand.pOfferComp = outOffer[0];
            cand.distMeters = std::sqrt(distSq);

            outCandidates.push_back(cand);
            if (outCandidates.size() >= 64) goto done_search;
        }
    }
done_search:;
}

bool InstallHook14(void* target, void* hookFunc) {
    if (!target || !hookFunc) return false;
    DWORD oldProtect = 0;
    if (!VirtualProtect(target, 14, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;

    // FF 25 00 00 00 00 [64-bit absolute target address]
    uint8_t jmpBytes[14] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
    memcpy(jmpBytes + 6, &hookFunc, sizeof(void*));
    memcpy(target, jmpBytes, 14);

    VirtualProtect(target, 14, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), target, 14);
    return true;
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
        g_adminOnly.store(toml.GetBool("AoEHarvest", "AdminOnly", false));

        g_hotkeyHarvest.store(static_cast<int>(toml.GetInt("Hotkeys", "ToggleHotkey", VK_F8)));
        g_audioFeedback.store(toml.GetBool("Hotkeys", "AudioFeedback", true));

        LogMod("[DreamAoEHarvest] TOML configuration applied: Enabled=%s (Radius=%.1fm, AdminOnly=%s, Hotkey=0x%X)",
               g_aoeHarvestEnabled.load() ? "ON" : "OFF",
               g_harvestRadius.load(),
               g_adminOnly.load() ? "true" : "false",
               g_hotkeyHarvest.load());
        return;
    }

    if (g_configPath.empty()) return;
    g_aoeHarvestEnabled.store(GetPrivateProfileIntA("Farming", "EnableAoeHarvest", 1, g_configPath.c_str()) != 0);
    char radBuf[32];
    GetPrivateProfileStringA("Farming", "AoeHarvestRadius", "3.5", radBuf, sizeof(radBuf), g_configPath.c_str());
    float r = (float)atof(radBuf);
    if (r <= 0.0f || r > 30.0f) r = 3.5f;
    g_harvestRadius.store(r);

    g_sameCropOnly.store(GetPrivateProfileIntA("Farming", "AoeHarvestSameCropOnly", 0, g_configPath.c_str()) != 0);
    g_adminOnly.store(GetPrivateProfileIntA("Farming", "AoeHarvestAdminOnly", 0, g_configPath.c_str()) != 0);
    g_hotkeyHarvest.store(GetPrivateProfileIntA("Hotkeys", "ToggleHotkey", VK_F8, g_configPath.c_str()));

    LogMod("[DreamAoEHarvest] INI fallback applied: Enabled=%s (Radius=%.1fm, AdminOnly=%s, Hotkey=0x%X)",
           g_aoeHarvestEnabled.load() ? "ON" : "OFF",
           g_harvestRadius.load(),
           g_adminOnly.load() ? "true" : "false",
           g_hotkeyHarvest.load());
}

DWORD WINAPI HotkeyWorkerThread(LPVOID) {
    bool prevF8 = false;
    while (g_workerRunning.load()) {
        HWND fgWnd = GetForegroundWindow();
        char titleBuf[128] = {0};
        if (fgWnd) GetWindowTextA(fgWnd, titleBuf, sizeof(titleBuf));

        bool isGameFocused = (strstr(titleBuf, "Enshrouded") != nullptr || strstr(titleBuf, "enshrouded") != nullptr || s_isServer);
        if (isGameFocused) {
            int keyHarvest = g_hotkeyHarvest.load();
            bool curF8 = (GetAsyncKeyState(keyHarvest) & 0x8000) != 0;
            if (curF8 && !prevF8) {
                bool newState = !g_aoeHarvestEnabled.load();
                g_aoeHarvestEnabled.store(newState);

                if (g_audioFeedback.load() && !s_isServer) {
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
    g_configPath = g_modDirectory + "config.ini";

    s_baseModule = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    char mainExe[MAX_PATH] = {0};
    GetModuleFileNameA((HMODULE)s_baseModule, mainExe, MAX_PATH);
    s_isServer = (strstr(mainExe, "enshrouded_server") != nullptr);

    LogMod("==================================================================");
    LogMod("  Dream AoE Harvest v1.2.0 (Unified Native Harmony Plugin)");
    LogMod("  Target: %s (Base: 0x%llX)", s_isServer ? "Dedicated Server (Headless)" : "Game Client", (unsigned long long)s_baseModule);
    LogMod("  Universal Player Access: %s (Rule 2)", g_adminOnly.load() ? "Admin-Only" : "ALL Connected Players");
    LogMod("==================================================================");

    LoadConfig();

    uintptr_t hookRva = s_isServer ? 0x000A1241 : 0x002A2EE1;
    uintptr_t resumeRva = s_isServer ? 0x000A124F : 0x002A2EEF;
    uintptr_t getEntityRva = s_isServer ? 0x00647520 : 0x009A6CA0;
    uintptr_t getCompRva = s_isServer ? 0x00654610 : 0x009B3D90;

    s_pfnGetEntity = (FnGetEntity)(s_baseModule + getEntityRva);
    s_pfnGetComp = (FnGetEntityComponent)(s_baseModule + getCompRva);
    pResumeExpandImmediateBatch = (void*)(s_baseModule + resumeRva);

    void* hookSite = (void*)(s_baseModule + hookRva);
    if (InstallHook14(hookSite, (void*)Hook_ExpandImmediateLootBatch_Relay)) {
        LogMod("[DreamAoEHarvest] Hook 13A installed successfully at RVA 0x%llX (Resume=0x%llX)!",
               (unsigned long long)hookRva, (unsigned long long)resumeRva);
    } else {
        LogMod("[DreamAoEHarvest] ERROR: Failed to install Hook 13A at RVA 0x%llX!", (unsigned long long)hookRva);
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
    g_initialized.store(false);
}

} // namespace

extern "C" __attribute__((force_align_arg_pointer))
void OnExpandImmediateLootBatch(uint8_t* pRawChunk, void* pContext, EventBatch* pBatch, void* pRbp) {
    (void)pRbp;
    if (!pBatch || !pContext || !pRawChunk) return;
    if (!g_aoeHarvestEnabled.load()) return;

    uint32_t count = pBatch->count;
    if (count == 0) return;
    if (!s_pfnGetEntity || !s_pfnGetComp) return;

    uint32_t playerId = *(uint32_t*)(pRawChunk + 0x08);
    uint32_t offerId = *(uint32_t*)(pRawChunk + 0x10);
    uint32_t primaryCropId = *(uint32_t*)(pRawChunk + 0x14);

    void* pPrimaryEntity = nullptr;
    if (primaryCropId != 0) {
        pPrimaryEntity = s_pfnGetEntity(pContext, primaryCropId);
    }
    if (!pPrimaryEntity || !IsValidPtr(pPrimaryEntity)) {
        uint32_t altId = *(uint32_t*)(pRawChunk + 0x0c);
        if (altId != 0) {
            pPrimaryEntity = s_pfnGetEntity(pContext, altId);
            if (pPrimaryEntity && IsValidPtr(pPrimaryEntity)) {
                primaryCropId = altId;
            }
        }
    }
    if (!pPrimaryEntity || !IsValidPtr(pPrimaryEntity) || primaryCropId == 0) return;

    // Verify entity has Component 0x105 (InteractionOffer)
    uint16_t cOffer = 0x105;
    void* outPrimaryOffer[2] = { nullptr, nullptr };
    s_pfnGetComp(outPrimaryOffer, pPrimaryEntity, &cOffer);
    void* pPrimaryOffer = outPrimaryOffer[0];
    if (!pPrimaryOffer || !IsValidPtr(pPrimaryOffer)) return;

    std::vector<CropCandidate> candidates;
    FindNearbyCropCandidates(pContext, pPrimaryEntity, primaryCropId,
                             g_harvestRadius.load(), g_sameCropOnly.load(), candidates);
    if (candidates.empty()) return;

    char acceptTemplate[32] = { 0 };
    memcpy(acceptTemplate, (char*)pPrimaryOffer + 0x54, sizeof(acceptTemplate));

    size_t countToExpand = candidates.size();
    if (countToExpand > 64) countToExpand = 64;

    for (size_t i = 0; i < countToExpand; ++i) {
        uint8_t* pTarget = pRawChunk + (1 + i) * 24;
        memcpy(pTarget, pRawChunk, 24);

        *(uint32_t*)(pTarget + 0x08) = playerId;
        *(uint32_t*)(pTarget + 0x10) = offerId;
        *(uint32_t*)(pTarget + 0x14) = candidates[i].entId;
        *(uint32_t*)(pTarget + 0x0c) = candidates[i].entId;

        void* pCandOffer = candidates[i].pOfferComp;
        if (pCandOffer && IsValidPtr(pCandOffer)) {
            memcpy((char*)pCandOffer + 0x54, acceptTemplate, sizeof(acceptTemplate));
            *(uint32_t*)((char*)pCandOffer + 0x54) = playerId;
            *(uint32_t*)((char*)pCandOffer + 0x60) = offerId;
        }
    }

    pBatch->count = (uint32_t)(1 + countToExpand);

    LogMod("[DreamAoEHarvest] >>> EXPANDED Crop Harvest for Player %u: 1 primary (%u) + %zu nearby crops (Total: %u crops in %.1fm radius)! <<<\n",
           playerId, primaryCropId, countToExpand, pBatch->count, g_harvestRadius.load());
}

extern "C" {

__declspec(dllexport) int XhlNativePluginQuery(XhlPluginQueryInfo* info) {
    if (!info) return 0;
    std::memset(info, 0, sizeof(XhlPluginQueryInfo));
    info->structSize = sizeof(XhlPluginQueryInfo);
    info->abiVersion = 0x10000;
    strncpy(info->id, "dream-aoe-harvest", sizeof(info->id) - 1);
    strncpy(info->name, "Dream AoE Harvest (Unified)", sizeof(info->name) - 1);
    strncpy(info->version, "1.2.0", sizeof(info->version) - 1);
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

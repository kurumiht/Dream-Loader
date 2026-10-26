// ============================================================================
// example_enshrouded_mod.cpp
//
// Shows how to write an Enshrouded Native Mod using dreamkurumiHT
// ZERO assembly files, ZERO hardcoded byte counts, and O(1) Cache resolution!
// ============================================================================

#include <windows.h>
#include <iostream>
#include "../include/dreamkurumiHT/dreamkurumiHT.hpp"

// Global mod state
struct AdminState {
    static inline bool godMode = false;
    static inline bool infiniteStamina = true;
    static inline bool freeCraft = true;
};

// ============================================================================
// 1. Stamina Drain Hook (Harmony Style with Auto-Register)
// ============================================================================
DREAM_HARMONY_PATCH(Enshrouded_StaminaDrain, "StaminaDrain", 0x2917FD, void, void* player, float amount) {
    // Prefix: return false to skip stamina drain!
    static bool Prefix(void* player, float amount) {
        if (AdminState::infiniteStamina) {
            // Skip original game calculation!
            return false;
        }
        return true;
    }
};
DREAM_REGISTER_PATCH(Enshrouded_StaminaDrain);

// ============================================================================
// 2. Free Craft Hook (Harmony Style with Auto-Register)
// ============================================================================
DREAM_HARMONY_PATCH(Enshrouded_FreeCraft, "FreeCraft", 0x3822B0, bool, void* recipeContext) {
    // Postfix: force return true so any recipe is craftable without materials!
    static void Postfix(bool& canCraft, void* recipeContext) {
        if (AdminState::freeCraft) {
            canCraft = true;
        }
    }
};
DREAM_REGISTER_PATCH(Enshrouded_FreeCraft);

// ============================================================================
// 3. Mid-Function Hook (For deep internal registers, ZERO assembly files!)
// ============================================================================
static dreamkurumi::DreamMidHook g_midStaminaHook;

void MyStaminaRegisterCallback(dreamkurumi::DreamCpuContext& ctx) {
    if (AdminState::infiniteStamina) {
        // Directly modify float in SSE XMM6 register in C++!
        ctx.xmm[6].f32[0] = 0.0f;
    }
}

// ============================================================================
// Plugin Lifecycle (Compatible with XHL or DLL Proxy)
// ============================================================================
void OnPluginStart(void (*hostLog)(const char*)) {
    if (hostLog) {
        dreamkurumi::DreamLog::Instance().SetExternalCallback(hostLog);
    }

    DREAM_LOG_INFO("Starting Enshrouded Mod using dreamkurumiHT...");

    // 1. Initialize offset cache
    dreamkurumi::DreamCache::Instance().Initialize("dreamkurumi_cache.ini");

    // 2. Install ALL registered hooks with a single line!
    size_t installed = dreamkurumi::DreamHarmony::PatchAll();
    DREAM_LOG_INFO("Successfully installed %zu hooks!", installed);

    // 3. (Optional) Manual Mid-hook if needed:
    void* midSite = dreamkurumi::DreamCache::Instance().Resolve("StaminaMidSite", 0x2917FD);
    if (midSite) {
        // g_midStaminaHook.Install(midSite, MyStaminaRegisterCallback);
    }
}

void OnPluginStop() {
    DREAM_LOG_INFO("Stopping Enshrouded Mod, restoring original bytes...");
    // Restores original game bytes cleanly with zero crash!
    dreamkurumi::DreamHarmony::UnpatchAll();
}

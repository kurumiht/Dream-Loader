#include <windows.h>
#include <dreamkurumiHT/dreamkurumiHT.hpp>

// ============================================================================
// 1. Example Hook: Infinite Stamina Patch (Harmony-Style with AOB Signature)
// Using DREAM_HARMONY_AOB_AUTO ensures the mod keeps working even after game updates!
// ============================================================================
DREAM_HARMONY_AOB_AUTO(StaminaDrainPatch, "StaminaDrain", "48 89 5C 24 ?? 57 48 83 EC", void, void* player, float amount) {
    static bool Prefix(void* player, float amount) {
        // Return false to skip stamina drain! (Infinite Stamina)
        return false;
    }
};
DREAM_REGISTER_PATCH(StaminaDrainPatch);

// ============================================================================
// 2. Zero-Boilerplate Mod Entry (DREAM_MOD_ENTRY)
// Automatically handles:
//  - Enshrouded Mod Loader ABI exports (XhlNativePluginQuery / Start / Stop)
//  - Automatic manifest reading from modinfo.json or ModInfo.xml
//  - Harmony.PatchAll() and UnpatchAll()
//  - Diagnostics console on Client, hotkeys (F9/F10), and crash guard sandboxing
// ============================================================================
DREAM_MOD_ENTRY(
    /* OnStart Callback */ {
        DREAM_LOG_INFO("My Dream Mod initialized via dreamkurumiHT!");

        // Example: Safe Memory Read / Pointer Chain manipulation
        // float currentStamina = dreamkurumi::DreamMemory::ReadChain<float>(playerPtr, {0x18, 0x48, 0x10}, 100.0f);

        // Example: Branching for Dedicated Server vs Client
        DREAM_SERVER_ONLY({
            DREAM_LOG_INFO("[Server] Running in Headless Dedicated Server mode");
        });
        DREAM_CLIENT_ONLY({
            DREAM_LOG_INFO("[Client] Running in Game Client mode with live HUD support");
        });
    },
    /* OnStop Callback */ {
        DREAM_LOG_INFO("Unloading My Dream Mod cleanly...");
    }
)

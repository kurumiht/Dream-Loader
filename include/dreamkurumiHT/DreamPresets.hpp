#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <atomic>
#include <algorithm>
#include <functional>
#include "DreamHarmony.hpp"
#include "DreamReflection.hpp"
#include "DreamLog.hpp"
#include "DreamCache.hpp"
#include "DreamWatcher.hpp"

namespace dreamkurumi {

class DreamOverlay;

// ============================================================================
// Keen Holistic Engine Game Settings Structure (Enshrouded Memory Layout)
// Dissected from live engine memory & community toolkits (Embervale / Shroudforge)
// ============================================================================
#pragma pack(push, 1)
struct KeenLiveGameSettings {
    float playerHealthFactor;           // +0x00
    float playerManaFactor;             // +0x04
    float playerStaminaFactor;          // +0x08
    float playerBodyHeatFactor;         // +0x0C
    float playerDivingTimeFactor;       // +0x10
    uint8_t pad14[0x30 - 0x14];         // +0x14 .. +0x2F
    float miningDamageFactor;           // +0x30
    uint8_t pad34[0x58 - 0x34];         // +0x34 .. +0x57
    float enemyDamageFactor;            // +0x58
    float enemyHealthFactor;            // +0x5C
    float enemyStaminaFactor;           // +0x60
    float enemyPerceptionRangeFactor;   // +0x64 (Ghost Mode / Invisibility)
    float bossDamageFactor;             // +0x68
    float bossHealthFactor;             // +0x6C
    float threatBonus;                  // +0x70
    uint8_t pacifyAllEnemies;           // +0x74
};
#pragma pack(pop)

// ============================================================================
// Official Shroudforge 1.6.6 Signatures & Binary Patches
// Dissected directly from runtime profile (1076226.json)
// ============================================================================
struct ShroudforgeSignatures {
    // 1. Auto Stamina Refill: writes Stamina_Max into Stamina
    static constexpr const char* AutoStaminaRefill = "8B 04 91 89 44 24 40";
    // 2. Infinite Item Split: suppress 'sub [rsi+4], ebp' in shared inventory
    static constexpr const char* InfiniteItemSplit = "29 6E 04 EB C5";
    // 3. Infinite Item Use: forward false (0) for consumption flag
    static constexpr const char* InfiniteItemUse   = "0F B6 85 ?? ?? 00 00 48 8D 4C 24 40 88 44 24 30 44 8B CE";
    // 4. No Fall Damage: suppress Health store in fall damage infliction
    static constexpr const char* NoFallDamage      = "45 89 0C 88 48 8B CB";
    // 5. No Resource Cost: set 6th argument (required count) to 0
    static constexpr const char* NoResourceCost    = "44 8B A5 08 01 00 00 4C 8B F9";
    // 6. Unlimited Flight: sets rotation pitch constant to -1.57f (-pi/2)
    static constexpr const char* UnlimitedFlight   = "F3 0F 10 05 ?? ?? ?? ?? F2 0F 11 4C";
};

// ============================================================================
// Keen Engine Reflected Assets & ECS Layout (Dissected from Shroudforge 1.6.6)
// ============================================================================
struct KeenEngineLayouts {
    static constexpr uint32_t EntityManagerCount  = 344;
    static constexpr uint32_t EntityManagerTable  = 392;
    static constexpr uint32_t ComponentBits       = 0;
    static constexpr uint32_t ComponentOffsets    = 132;
    static constexpr uint32_t ComponentStrides    = 2692;

    static constexpr uint32_t Hash_Stamina_Max    = 4098081808u; // 0xF4438410
    static constexpr uint32_t Hash_Stamina        = 79080075u;   // 0x04B6A68B
    static constexpr uint32_t Hash_Health         = 2394442133u; // 0x8EB88995

    static constexpr const char* RecipeRegistryType     = "keen::RecipeRegistryResource";
    static constexpr const char* KnowledgeQueryType     = "keen::GameKnowledgeQueryResourceDb";
    static constexpr const char* QueryUnlockFlameAltar  = "Unlock_Flame_Altar_PK";
    static constexpr const char* ActionNpcFlameHint     = "NPC_Flame_Hint01";
};

// ============================================================================
// Dedicated Server & Headless Mode Detection (Inspired by Embervale Toolkit)
// ============================================================================
class DreamServerGuard {
public:
    static bool IsDedicatedServer() {
        static bool s_checked = false;
        static bool s_isServer = false;
        if (!s_checked) {
            char path[MAX_PATH] = {0};
            if (GetModuleFileNameA(NULL, path, MAX_PATH)) {
                std::string exeName = path;
                std::transform(exeName.begin(), exeName.end(), exeName.begin(), ::tolower);
                if (exeName.find("enshrouded_server") != std::string::npos ||
                    exeName.find("server") != std::string::npos) {
                    s_isServer = true;
                }
            }
            s_checked = true;
        }
        return s_isServer;
    }
};

// ============================================================================
// Feature Categories & Permission Enums
// Allows servers & mods to selectively permit or block cheat features
// ============================================================================
enum class PresetCategory {
    Admin,     // GodMode, OneShotKill
    Stealth,   // GhostMode
    Survival,  // InfiniteStamina, InfiniteMana, InfiniteShroud
    Economy,   // FreeCraft, NoResourceCost, InfiniteItemSplit, InfiniteItemUse, FastProduction
    World      // SuperMining, UnlimitedFlight, InfiniteDurability
};

enum class PresetFeature {
    GodMode,
    GhostMode,
    InfiniteStamina,
    InfiniteMana,
    InfiniteShroud,
    FreeCraft,
    SuperMining,
    UnlimitedFlight,
    InfiniteItemSplit,
    InfiniteItemUse,
    NoResourceCost,
    InfiniteDurability,
    OneShotKill,
    FastProduction,
    Count
};

inline const char* GetPresetFeatureName(PresetFeature feat) {
    switch (feat) {
        case PresetFeature::GodMode: return "GodMode";
        case PresetFeature::GhostMode: return "GhostMode";
        case PresetFeature::InfiniteStamina: return "InfiniteStamina";
        case PresetFeature::InfiniteMana: return "InfiniteMana";
        case PresetFeature::InfiniteShroud: return "InfiniteShroud";
        case PresetFeature::FreeCraft: return "FreeCraft";
        case PresetFeature::SuperMining: return "SuperMining";
        case PresetFeature::UnlimitedFlight: return "UnlimitedFlight";
        case PresetFeature::InfiniteItemSplit: return "InfiniteItemSplit";
        case PresetFeature::InfiniteItemUse: return "InfiniteItemUse";
        case PresetFeature::NoResourceCost: return "NoResourceCost";
        case PresetFeature::InfiniteDurability: return "InfiniteDurability";
        case PresetFeature::OneShotKill: return "OneShotKill";
        case PresetFeature::FastProduction: return "FastProduction";
        default: return "Unknown";
    }
}

inline PresetCategory GetPresetFeatureCategory(PresetFeature feat) {
    switch (feat) {
        case PresetFeature::GodMode:
        case PresetFeature::OneShotKill:
            return PresetCategory::Admin;
        case PresetFeature::GhostMode:
            return PresetCategory::Stealth;
        case PresetFeature::InfiniteStamina:
        case PresetFeature::InfiniteMana:
        case PresetFeature::InfiniteShroud:
            return PresetCategory::Survival;
        case PresetFeature::FreeCraft:
        case PresetFeature::NoResourceCost:
        case PresetFeature::InfiniteItemSplit:
        case PresetFeature::InfiniteItemUse:
        case PresetFeature::FastProduction:
            return PresetCategory::Economy;
        case PresetFeature::SuperMining:
        case PresetFeature::UnlimitedFlight:
        case PresetFeature::InfiniteDurability:
        default:
            return PresetCategory::World;
    }
}

inline const char* GetPresetCategoryName(PresetCategory cat) {
    switch (cat) {
        case PresetCategory::Admin: return "Admin";
        case PresetCategory::Stealth: return "Stealth";
        case PresetCategory::Survival: return "Survival";
        case PresetCategory::Economy: return "Economy";
        case PresetCategory::World: return "World";
        default: return "General";
    }
}

// ============================================================================
// DreamServerPolicy - Anti-Cheat & Server Rule Permission Guard
// Allows servers to strictly enforce vanilla gameplay or block specific cheats
// ============================================================================
class DreamServerPolicy {
public:
    static constexpr size_t FeatureCount = static_cast<size_t>(PresetFeature::Count);

    static DreamServerPolicy& Instance() {
        static DreamServerPolicy s_instance;
        return s_instance;
    }

    bool IsFeatureAllowed(PresetFeature feature) const {
        size_t idx = static_cast<size_t>(feature);
        if (idx < FeatureCount) {
            return m_allowedFeatures[idx].load();
        }
        return false;
    }

    bool IsCategoryAllowed(PresetCategory category) const {
        for (size_t i = 0; i < FeatureCount; ++i) {
            PresetFeature f = static_cast<PresetFeature>(i);
            if (GetPresetFeatureCategory(f) == category && !m_allowedFeatures[i].load()) {
                return false;
            }
        }
        return true;
    }

    void SetFeatureAllowed(PresetFeature feature, bool allowed) {
        size_t idx = static_cast<size_t>(feature);
        if (idx < FeatureCount) {
            m_allowedFeatures[idx].store(allowed);
            DREAM_LOG_INFO("DreamServerPolicy: Feature '%s' is now %s",
                           GetPresetFeatureName(feature),
                           allowed ? "ALLOWED" : "PROHIBITED");
        }
    }

    void SetCategoryAllowed(PresetCategory category, bool allowed) {
        for (size_t i = 0; i < FeatureCount; ++i) {
            PresetFeature f = static_cast<PresetFeature>(i);
            if (GetPresetFeatureCategory(f) == category) {
                m_allowedFeatures[i].store(allowed);
            }
        }
        DREAM_LOG_INFO("DreamServerPolicy: Category '%s' is now %s",
                       GetPresetCategoryName(category),
                       allowed ? "ALLOWED" : "PROHIBITED");
    }

    // Strict Anti-Cheat: Prohibit all cheat/exploit presets on server
    void EnforceStrictServerRules() {
        m_strictMode.store(true);
        for (size_t i = 0; i < FeatureCount; ++i) {
            m_allowedFeatures[i].store(false);
        }
        DREAM_LOG_WARN("DreamServerPolicy: Strict Server Anti-Cheat Rules ENFORCED. All cheat presets PROHIBITED.");
    }

    // Open/Unrestricted policy (Single player / Admin server)
    void AllowAllFeatures() {
        m_strictMode.store(false);
        for (size_t i = 0; i < FeatureCount; ++i) {
            m_allowedFeatures[i].store(true);
        }
        DREAM_LOG_INFO("DreamServerPolicy: All features ALLOWED.");
    }

    bool IsStrictMode() const {
        return m_strictMode.load();
    }

    void SetOnViolationCallback(std::function<void(PresetFeature)> cb) {
        m_onViolation = cb;
    }

    void NotifyViolation(PresetFeature feature) {
        DREAM_LOG_WARN("DreamServerPolicy: [POLICY BLOCKED] Feature '%s' (%s) is prohibited by server policy!",
                       GetPresetFeatureName(feature),
                       GetPresetCategoryName(GetPresetFeatureCategory(feature)));
        if (m_onViolation) {
            m_onViolation(feature);
        }
    }

    bool SavePolicy(const std::string& path = "server_policy.ini") const {
        FILE* fp = fopen(path.c_str(), "w");
        if (!fp) return false;
        fprintf(fp, "# dreamkurumiHT Dedicated Server Policy Configuration\n");
        fprintf(fp, "# Use this file to restrict or allow cheat presets on this server\n\n");
        fprintf(fp, "[policy]\n");
        fprintf(fp, "strictMode=%s\n", m_strictMode.load() ? "true" : "false");
        fprintf(fp, "adminCategory=%s\n", IsCategoryAllowed(PresetCategory::Admin) ? "true" : "false");
        fprintf(fp, "stealthCategory=%s\n", IsCategoryAllowed(PresetCategory::Stealth) ? "true" : "false");
        fprintf(fp, "survivalCategory=%s\n", IsCategoryAllowed(PresetCategory::Survival) ? "true" : "false");
        fprintf(fp, "economyCategory=%s\n", IsCategoryAllowed(PresetCategory::Economy) ? "true" : "false");
        fprintf(fp, "worldCategory=%s\n", IsCategoryAllowed(PresetCategory::World) ? "true" : "false");
        fprintf(fp, "\n[features]\n");
        for (size_t i = 0; i < FeatureCount; ++i) {
            PresetFeature f = static_cast<PresetFeature>(i);
            fprintf(fp, "%s=%s\n", GetPresetFeatureName(f), m_allowedFeatures[i].load() ? "true" : "false");
        }
        fclose(fp);
        DREAM_LOG_INFO("DreamServerPolicy: Saved server policy to '%s'", path.c_str());
        return true;
    }

    bool LoadPolicy(const std::string& path = "server_policy.ini") {
        FILE* fp = fopen(path.c_str(), "r");
        if (!fp) return false;
        char line[256];
        bool inPolicySec = false;
        bool inFeatureSec = false;
        while (fgets(line, sizeof(line), fp)) {
            std::string s = line;
            while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ')) s.pop_back();
            if (s.empty() || s[0] == '#' || s[0] == ';') continue;
            if (s == "[policy]") { inPolicySec = true; inFeatureSec = false; continue; }
            if (s == "[features]") { inPolicySec = false; inFeatureSec = true; continue; }

            size_t eq = s.find('=');
            if (eq == std::string::npos) continue;
            std::string key = s.substr(0, eq);
            std::string val = s.substr(eq + 1);
            bool bVal = (val == "true" || val == "1" || val == "on");

            if (inPolicySec) {
                if (key == "strictMode" && bVal) {
                    EnforceStrictServerRules();
                } else if (key == "adminCategory") {
                    SetCategoryAllowed(PresetCategory::Admin, bVal);
                } else if (key == "stealthCategory") {
                    SetCategoryAllowed(PresetCategory::Stealth, bVal);
                } else if (key == "survivalCategory") {
                    SetCategoryAllowed(PresetCategory::Survival, bVal);
                } else if (key == "economyCategory") {
                    SetCategoryAllowed(PresetCategory::Economy, bVal);
                } else if (key == "worldCategory") {
                    SetCategoryAllowed(PresetCategory::World, bVal);
                }
            } else if (inFeatureSec) {
                for (size_t i = 0; i < FeatureCount; ++i) {
                    PresetFeature f = static_cast<PresetFeature>(i);
                    if (key == GetPresetFeatureName(f)) {
                        SetFeatureAllowed(f, bVal);
                        break;
                    }
                }
            }
        }
        fclose(fp);
        DREAM_LOG_INFO("DreamServerPolicy: Successfully loaded server policy from '%s'", path.c_str());
        return true;
    }

    void EnableAutoHotReload(const std::string& path = "server_policy.ini") {
        DreamWatcher::Instance().WatchFile(path, [this](const std::string& file) {
            LoadPolicy(file);
        });
    }

private:
    DreamServerPolicy() {
        for (size_t i = 0; i < FeatureCount; ++i) {
            m_allowedFeatures[i].store(true);
        }
    }

    std::atomic<bool> m_strictMode{false};
    std::atomic<bool> m_allowedFeatures[FeatureCount];
    std::function<void(PresetFeature)> m_onViolation;
};

// ============================================================================
// Mod Preset Configuration State
// ============================================================================
struct DreamPresetState {
    std::atomic<bool> godMode{false};
    std::atomic<bool> ghostMode{false};
    std::atomic<bool> infiniteStamina{false};
    std::atomic<bool> infiniteMana{false};
    std::atomic<bool> infiniteShroud{false};
    std::atomic<bool> freeCraft{false};
    std::atomic<bool> superMining{false};
    std::atomic<bool> unlimitedFlight{false};
    std::atomic<bool> infiniteItemSplit{false};
    std::atomic<bool> infiniteItemUse{false};
    std::atomic<bool> noResourceCost{false};
    std::atomic<bool> infiniteDurability{false};
    std::atomic<bool> oneShotKill{false};
    std::atomic<bool> fastProduction{false};
    std::atomic<float> productionDuration{1.0f};
    std::atomic<float> damageDealtMult{1.0f};
    std::atomic<float> damageTakenMult{1.0f};
    std::atomic<float> miningMult{1.0f};
};

// ============================================================================
// DreamPresets - High-Level Enshrouded Game Logic Suite
// Provides ready-to-use Harmony patches and direct Keen Engine memory controls
// ============================================================================
class DreamPresets {
public:
    static DreamPresets& Instance() {
        static DreamPresets s_instance;
        return s_instance;
    }

    DreamPresetState& State() { return m_state; }
    const DreamPresetState& State() const { return m_state; }

    // Register cheat and preset commands with DreamOverlay (opt-in)
    void RegisterCommands(DreamOverlay& overlay);

    // Direct synchronization into Keen Holistic Engine LiveGameSettings block
    void ApplyToSettings(KeenLiveGameSettings* settings) {
        if (!settings) return;
        const auto& policy = DreamServerPolicy::Instance();

        // 1. Ghost Mode: zero out perception range so enemies never alert
        if (m_state.ghostMode.load() && policy.IsFeatureAllowed(PresetFeature::GhostMode)) {
            settings->enemyPerceptionRangeFactor = 0.0f;
        } else {
            settings->enemyPerceptionRangeFactor = 1.0f;
        }

        // 2. Infinite Stamina multiplier
        if (m_state.infiniteStamina.load() && policy.IsFeatureAllowed(PresetFeature::InfiniteStamina)) {
            settings->playerStaminaFactor = 100.0f;
        } else {
            settings->playerStaminaFactor = 1.0f;
        }

        // 3. Infinite Mana multiplier
        if (m_state.infiniteMana.load() && policy.IsFeatureAllowed(PresetFeature::InfiniteMana)) {
            settings->playerManaFactor = 100.0f;
        } else {
            settings->playerManaFactor = 1.0f;
        }

        // 4. Mining multiplier
        if (m_state.superMining.load() && policy.IsFeatureAllowed(PresetFeature::SuperMining)) {
            settings->miningDamageFactor = m_state.miningMult.load() * 10.0f;
        } else {
            settings->miningDamageFactor = 1.0f;
        }

        // 5. God Mode: negate all enemy & boss damage
        if (m_state.godMode.load() && policy.IsFeatureAllowed(PresetFeature::GodMode)) {
            settings->enemyDamageFactor = 0.0f;
            settings->bossDamageFactor = 0.0f;
        } else {
            settings->enemyDamageFactor = m_state.damageTakenMult.load();
            settings->bossDamageFactor = m_state.damageTakenMult.load();
        }
    }

    // Toggle presets with server policy enforcement and automatic diagnostics logging
    bool SetGodMode(bool enable) {
        if (enable && !DreamServerPolicy::Instance().IsFeatureAllowed(PresetFeature::GodMode)) {
            DreamServerPolicy::Instance().NotifyViolation(PresetFeature::GodMode);
            m_state.godMode.store(false);
            return false;
        }
        m_state.godMode.store(enable);
        DREAM_LOG_INFO("DreamPresets: GodMode set to %s", enable ? "ENABLED" : "DISABLED");
        return true;
    }

    bool SetGhostMode(bool enable) {
        if (enable && !DreamServerPolicy::Instance().IsFeatureAllowed(PresetFeature::GhostMode)) {
            DreamServerPolicy::Instance().NotifyViolation(PresetFeature::GhostMode);
            m_state.ghostMode.store(false);
            return false;
        }
        m_state.ghostMode.store(enable);
        DREAM_LOG_INFO("DreamPresets: GhostMode set to %s", enable ? "ENABLED" : "DISABLED");
        return true;
    }

    bool SetInfiniteStamina(bool enable) {
        if (enable && !DreamServerPolicy::Instance().IsFeatureAllowed(PresetFeature::InfiniteStamina)) {
            DreamServerPolicy::Instance().NotifyViolation(PresetFeature::InfiniteStamina);
            m_state.infiniteStamina.store(false);
            return false;
        }
        m_state.infiniteStamina.store(enable);
        DREAM_LOG_INFO("DreamPresets: InfiniteStamina set to %s", enable ? "ENABLED" : "DISABLED");
        return true;
    }

    bool SetInfiniteMana(bool enable) {
        if (enable && !DreamServerPolicy::Instance().IsFeatureAllowed(PresetFeature::InfiniteMana)) {
            DreamServerPolicy::Instance().NotifyViolation(PresetFeature::InfiniteMana);
            m_state.infiniteMana.store(false);
            return false;
        }
        m_state.infiniteMana.store(enable);
        DREAM_LOG_INFO("DreamPresets: InfiniteMana set to %s", enable ? "ENABLED" : "DISABLED");
        return true;
    }

    bool SetInfiniteShroud(bool enable) {
        if (enable && !DreamServerPolicy::Instance().IsFeatureAllowed(PresetFeature::InfiniteShroud)) {
            DreamServerPolicy::Instance().NotifyViolation(PresetFeature::InfiniteShroud);
            m_state.infiniteShroud.store(false);
            return false;
        }
        m_state.infiniteShroud.store(enable);
        DREAM_LOG_INFO("DreamPresets: InfiniteShroud set to %s", enable ? "ENABLED" : "DISABLED");
        return true;
    }

    bool SetFreeCraft(bool enable) {
        if (enable && !DreamServerPolicy::Instance().IsFeatureAllowed(PresetFeature::FreeCraft)) {
            DreamServerPolicy::Instance().NotifyViolation(PresetFeature::FreeCraft);
            m_state.freeCraft.store(false);
            return false;
        }
        m_state.freeCraft.store(enable);
        DREAM_LOG_INFO("DreamPresets: FreeCraft set to %s", enable ? "ENABLED" : "DISABLED");
        return true;
    }

    bool SetSuperMining(bool enable, float mult = 5.0f) {
        if (enable && !DreamServerPolicy::Instance().IsFeatureAllowed(PresetFeature::SuperMining)) {
            DreamServerPolicy::Instance().NotifyViolation(PresetFeature::SuperMining);
            m_state.superMining.store(false);
            return false;
        }
        m_state.superMining.store(enable);
        m_state.miningMult.store(mult);
        DREAM_LOG_INFO("DreamPresets: SuperMining set to %s (multiplier: %.1f)", enable ? "ENABLED" : "DISABLED", mult);
        return true;
    }

    bool SetUnlimitedFlight(bool enable) {
        if (enable && !DreamServerPolicy::Instance().IsFeatureAllowed(PresetFeature::UnlimitedFlight)) {
            DreamServerPolicy::Instance().NotifyViolation(PresetFeature::UnlimitedFlight);
            m_state.unlimitedFlight.store(false);
            return false;
        }
        m_state.unlimitedFlight.store(enable);
        DREAM_LOG_INFO("DreamPresets: UnlimitedFlight set to %s", enable ? "ENABLED" : "DISABLED");
        return true;
    }

    bool SetInfiniteItemSplit(bool enable) {
        if (enable && !DreamServerPolicy::Instance().IsFeatureAllowed(PresetFeature::InfiniteItemSplit)) {
            DreamServerPolicy::Instance().NotifyViolation(PresetFeature::InfiniteItemSplit);
            m_state.infiniteItemSplit.store(false);
            return false;
        }
        m_state.infiniteItemSplit.store(enable);
        DREAM_LOG_INFO("DreamPresets: InfiniteItemSplit set to %s", enable ? "ENABLED" : "DISABLED");
        return true;
    }

    bool SetInfiniteItemUse(bool enable) {
        if (enable && !DreamServerPolicy::Instance().IsFeatureAllowed(PresetFeature::InfiniteItemUse)) {
            DreamServerPolicy::Instance().NotifyViolation(PresetFeature::InfiniteItemUse);
            m_state.infiniteItemUse.store(false);
            return false;
        }
        m_state.infiniteItemUse.store(enable);
        DREAM_LOG_INFO("DreamPresets: InfiniteItemUse set to %s", enable ? "ENABLED" : "DISABLED");
        return true;
    }

    bool SetNoResourceCost(bool enable) {
        if (enable && !DreamServerPolicy::Instance().IsFeatureAllowed(PresetFeature::NoResourceCost)) {
            DreamServerPolicy::Instance().NotifyViolation(PresetFeature::NoResourceCost);
            m_state.noResourceCost.store(false);
            return false;
        }
        m_state.noResourceCost.store(enable);
        DREAM_LOG_INFO("DreamPresets: NoResourceCost set to %s", enable ? "ENABLED" : "DISABLED");
        return true;
    }

    bool SetInfiniteDurability(bool enable) {
        if (enable && !DreamServerPolicy::Instance().IsFeatureAllowed(PresetFeature::InfiniteDurability)) {
            DreamServerPolicy::Instance().NotifyViolation(PresetFeature::InfiniteDurability);
            m_state.infiniteDurability.store(false);
            return false;
        }
        m_state.infiniteDurability.store(enable);
        DREAM_LOG_INFO("DreamPresets: InfiniteDurability set to %s", enable ? "ENABLED" : "DISABLED");
        return true;
    }

    bool SetOneShotKill(bool enable) {
        if (enable && !DreamServerPolicy::Instance().IsFeatureAllowed(PresetFeature::OneShotKill)) {
            DreamServerPolicy::Instance().NotifyViolation(PresetFeature::OneShotKill);
            m_state.oneShotKill.store(false);
            return false;
        }
        m_state.oneShotKill.store(enable);
        DREAM_LOG_INFO("DreamPresets: OneShotKill set to %s", enable ? "ENABLED" : "DISABLED");
        return true;
    }

    bool SetFastProduction(bool enable, float seconds = 1.0f) {
        if (enable && !DreamServerPolicy::Instance().IsFeatureAllowed(PresetFeature::FastProduction)) {
            DreamServerPolicy::Instance().NotifyViolation(PresetFeature::FastProduction);
            m_state.fastProduction.store(false);
            return false;
        }
        m_state.fastProduction.store(enable);
        m_state.productionDuration.store(seconds);
        DREAM_LOG_INFO("DreamPresets: FastProduction set to %s (%.1f seconds)", enable ? "ENABLED" : "DISABLED", seconds);
        return true;
    }

    bool SaveConfig(const std::string& path = "dreamkurumi_config.ini") const {
        FILE* fp = fopen(path.c_str(), "w");
        if (!fp) return false;
        fprintf(fp, "# dreamkurumiHT Preset Configuration\n");
        fprintf(fp, "[presets]\n");
        fprintf(fp, "godMode=%s\n", m_state.godMode.load() ? "true" : "false");
        fprintf(fp, "ghostMode=%s\n", m_state.ghostMode.load() ? "true" : "false");
        fprintf(fp, "infiniteStamina=%s\n", m_state.infiniteStamina.load() ? "true" : "false");
        fprintf(fp, "infiniteMana=%s\n", m_state.infiniteMana.load() ? "true" : "false");
        fprintf(fp, "infiniteShroud=%s\n", m_state.infiniteShroud.load() ? "true" : "false");
        fprintf(fp, "freeCraft=%s\n", m_state.freeCraft.load() ? "true" : "false");
        fprintf(fp, "noResourceCost=%s\n", m_state.noResourceCost.load() ? "true" : "false");
        fprintf(fp, "infiniteItemSplit=%s\n", m_state.infiniteItemSplit.load() ? "true" : "false");
        fprintf(fp, "infiniteItemUse=%s\n", m_state.infiniteItemUse.load() ? "true" : "false");
        fprintf(fp, "unlimitedFlight=%s\n", m_state.unlimitedFlight.load() ? "true" : "false");
        fprintf(fp, "infiniteDurability=%s\n", m_state.infiniteDurability.load() ? "true" : "false");
        fprintf(fp, "oneShotKill=%s\n", m_state.oneShotKill.load() ? "true" : "false");
        fprintf(fp, "superMining=%s\n", m_state.superMining.load() ? "true" : "false");
        fprintf(fp, "miningMult=%.2f\n", m_state.miningMult.load());
        fprintf(fp, "fastProduction=%s\n", m_state.fastProduction.load() ? "true" : "false");
        fprintf(fp, "productionDuration=%.2f\n", m_state.productionDuration.load());
        fclose(fp);
        DREAM_LOG_INFO("DreamPresets: Saved configuration to '%s'", path.c_str());
        return true;
    }

    bool LoadConfig(const std::string& path = "dreamkurumi_config.ini") {
        FILE* fp = fopen(path.c_str(), "r");
        if (!fp) return false;
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            std::string s = line;
            size_t eq = s.find('=');
            if (eq == std::string::npos) continue;
            std::string key = s.substr(0, eq);
            std::string val = s.substr(eq + 1);
            while (!val.empty() && (val.back() == '\r' || val.back() == '\n' || val.back() == ' ')) val.pop_back();
            bool bVal = (val == "true" || val == "1" || val == "on");

            if (key == "godMode") SetGodMode(bVal);
            else if (key == "ghostMode") SetGhostMode(bVal);
            else if (key == "infiniteStamina") SetInfiniteStamina(bVal);
            else if (key == "infiniteMana") SetInfiniteMana(bVal);
            else if (key == "infiniteShroud") SetInfiniteShroud(bVal);
            else if (key == "freeCraft") SetFreeCraft(bVal);
            else if (key == "noResourceCost") SetNoResourceCost(bVal);
            else if (key == "infiniteItemSplit") SetInfiniteItemSplit(bVal);
            else if (key == "infiniteItemUse") SetInfiniteItemUse(bVal);
            else if (key == "unlimitedFlight") SetUnlimitedFlight(bVal);
            else if (key == "infiniteDurability") SetInfiniteDurability(bVal);
            else if (key == "oneShotKill") SetOneShotKill(bVal);
            else if (key == "fastProduction") SetFastProduction(bVal, m_state.productionDuration.load());
            else if (key == "superMining") SetSuperMining(bVal, m_state.miningMult.load());
            else if (key == "miningMult") m_state.miningMult.store(static_cast<float>(atof(val.c_str())));
            else if (key == "productionDuration") m_state.productionDuration.store(static_cast<float>(atof(val.c_str())));
        }
        fclose(fp);
        DREAM_LOG_INFO("DreamPresets: Loaded configuration from '%s'", path.c_str());
        return true;
    }

    void EnableAutoHotReload(const std::string& path = "dreamkurumi_config.ini") {
        DreamWatcher::Instance().WatchFile(path, [this](const std::string& file) {
            LoadConfig(file);
        });
    }

private:
    DreamPresets() = default;
    DreamPresetState m_state;
};

// ============================================================================
// Built-in Harmony Patches for Enshrouded Game Logic
// Inspired by Shroudforge QoL and Embervale Creative Building
// ============================================================================

// 1. Free Crafting & Building: Skip resource requirements and ensure craft succeeds
DREAM_DECLARE_PATCH(PresetFreeCraftPatch, bool, void*, void*, int) {
    static bool Prefix(void* recipe, void* inventory, int count) {
        if (DreamPresets::Instance().State().freeCraft.load() ||
            DreamPresets::Instance().State().noResourceCost.load()) {
            return false; // Skip original resource deduction
        }
        return true;
    }

    static void Postfix(bool& result, void* recipe, void* inventory, int count) {
        if (DreamPresets::Instance().State().freeCraft.load() ||
            DreamPresets::Instance().State().noResourceCost.load()) {
            result = true; // Always succeed crafting
        }
    }
};

// 2. Infinite Stamina Patch: Suppress stamina drain
DREAM_DECLARE_PATCH(PresetStaminaPatch, void, void*, float) {
    static bool Prefix(void* player, float amount) {
        if (DreamPresets::Instance().State().infiniteStamina.load()) {
            return false; // Skip original stamina consumption
        }
        return true;
    }
};

// 3. Fall Damage Immunity Patch: Zero out damage from high falls
DREAM_DECLARE_PATCH(PresetFallDamagePatch, float, void*, float) {
    static bool Prefix(void* player, float fallHeight) {
        if (DreamPresets::Instance().State().godMode.load()) {
            return false; // Skip calculation
        }
        return true;
    }

    static void Postfix(float& damage, void* player, float fallHeight) {
        if (DreamPresets::Instance().State().godMode.load()) {
            damage = 0.0f; // Force 0 fall damage
        }
    }
};

// 4. Shroud Timer Freezing Patch: Prevent shroud countdown
DREAM_DECLARE_PATCH(PresetShroudPatch, void, void*, float) {
    static bool Prefix(void* timerCtx, float deltaSeconds) {
        if (DreamPresets::Instance().State().infiniteShroud.load()) {
            return false; // Skip time deduction inside Shroud
        }
        return true;
    }
};

// 5. Infinite Item Split Patch (Dissected from Shroudforge 1.6.6)
// Suppresses subtracting the split amount from the source inventory slot
DREAM_DECLARE_PATCH(PresetItemSplitPatch, void, void*, int) {
    static bool Prefix(void* itemSlot, int splitAmount) {
        if (DreamPresets::Instance().State().infiniteItemSplit.load()) {
            return false; // Duplicates item stack on split
        }
        return true;
    }
};

// 6. Infinite Item Use Patch (Dissected from Shroudforge 1.6.6)
// Prevents item consumption when eating food, drinking potions, or firing ammo
DREAM_DECLARE_PATCH(PresetItemUsePatch, bool, void*, int) {
    static bool Prefix(void* itemSlot, int useCount) {
        if (DreamPresets::Instance().State().infiniteItemUse.load()) {
            return false; // Skip decrementing item count
        }
        return true;
    }
    static void Postfix(bool& result, void* itemSlot, int useCount) {
        if (DreamPresets::Instance().State().infiniteItemUse.load()) {
            result = true; // Item usage succeeds without consuming stack
        }
    }
};

// 7. Infinite Durability Patch (Dissected from Embervale Toolkit)
// Suppresses weapon and tool durability loss on hits and actions
DREAM_DECLARE_PATCH(PresetDurabilityPatch, void, void*, float) {
    static bool Prefix(void* item, float wearAmount) {
        if (DreamPresets::Instance().State().infiniteDurability.load()) {
            return false; // Weapons and tools never wear down
        }
        return true;
    }
};

// 8. Unlimited Flight / Glider Patch (Dissected from Shroudforge 1.6.6)
// Prevents downward pitch and altitude drop while gliding
DREAM_DECLARE_PATCH(PresetUnlimitedFlightPatch, void, void*, float) {
    static bool Prefix(void* gliderCtx, float deltaPitch) {
        if (DreamPresets::Instance().State().unlimitedFlight.load()) {
            return false; // Lock glider descent
        }
        return true;
    }
};

// 9. Fast Production Patch (Dissected from Shroudforge sf-production-time)
// Overrides crafting station duration (in nanoseconds) to speed up production
DREAM_DECLARE_PATCH(PresetProductionTimePatch, void, void*, int64_t&) {
    static void Postfix(void* recipe, int64_t& durationNanoSeconds) {
        if (DreamPresets::Instance().State().fastProduction.load()) {
            float sec = DreamPresets::Instance().State().productionDuration.load();
            durationNanoSeconds = static_cast<int64_t>(sec * 1000000000.0f); // convert to keen::Time nanoseconds
        }
    }
};

} // namespace dreamkurumi

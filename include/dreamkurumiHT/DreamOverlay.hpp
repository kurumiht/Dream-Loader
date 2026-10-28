#pragma once
#include <windows.h>
#include <cstdint>
#include <functional>
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <sstream>
#include <unordered_map>
#include "DreamPresets.hpp"
#include "DreamLog.hpp"
#include "DreamProfiler.hpp"
#include "DreamEnv.hpp"
#include "DreamWatchdog.hpp"
#include "DreamCrashGuard.hpp"
#include "DreamMemory.hpp"
#include "DreamScanner.hpp"
#include "DreamGameContext.hpp"
#include "DreamLua.hpp"
#include "DreamLiveConfig.hpp"
#include "DreamPlayerRegistry.hpp"
#include "DreamBackup.hpp"
#include <psapi.h>

namespace dreamkurumi {

// ============================================================================
// DreamOverlay - In-Game Menu Toggle, Hotkey Manager & Console Command System
// Inspired by Shroudforge (F9 Menu / F10 Debug) and Admin-Menu Architecture
// ============================================================================
class DreamOverlay {
public:
    using KeyCallback = std::function<void()>;
    using CommandHandler = std::function<void(const std::string& args)>;

    static DreamOverlay& Instance() {
        static DreamOverlay s_instance;
        return s_instance;
    }

    // Configure main hotkeys
    void SetMenuToggleKey(int vKey) { m_menuKey = vKey; }
    int GetMenuToggleKey() const { return m_menuKey; }

    void SetConsoleToggleKey(int vKey) { m_consoleKey = vKey; }
    int GetConsoleToggleKey() const { return m_consoleKey; }

    void SetAudioFeedback(bool enable) { m_audioFeedback = enable; }
    bool GetAudioFeedback() const { return m_audioFeedback; }

    bool IsMenuOpen() const { return m_isMenuOpen.load(); }
    bool IsConsoleOpen() const { return m_isConsoleOpen.load(); }

    void SetMenuOpen(bool open) {
        m_isMenuOpen.store(open);
        if (m_audioFeedback && !DreamServerGuard::IsDedicatedServer()) {
            MessageBeep(open ? MB_OK : MB_ICONASTERISK);
        }
        if (m_onMenuToggle) {
            m_onMenuToggle(open);
        }
        DREAM_LOG_INFO("DreamOverlay: In-Game Menu %s (Key: 0x%02X)", open ? "OPENED" : "CLOSED", m_menuKey);
    }

    void ToggleMenu() {
        SetMenuOpen(!IsMenuOpen());
    }

    void ToggleConsole() {
        bool newState = !m_isConsoleOpen.load();
        m_isConsoleOpen.store(newState);
        if (newState) {
            DreamLog::Instance().AllocateConsoleWindow("dreamkurumiHT Diagnostics Console (Client)");
        }
        if (m_onConsoleToggle) {
            m_onConsoleToggle(newState);
        }
        DREAM_LOG_INFO("DreamOverlay: Debug Console %s", newState ? "ENABLED" : "DISABLED");
    }

    // Callbacks for custom UI renderers (e.g. ImGui, DirectX Present, or Win32 Layered Window)
    void SetOnMenuToggle(std::function<void(bool isOpen)> cb) { m_onMenuToggle = cb; }
    void SetOnConsoleToggle(std::function<void(bool isOpen)> cb) { m_onConsoleToggle = cb; }

    // Register custom hotkey actions
    void BindKey(int vKey, KeyCallback callback) {
        m_customBinds[vKey] = callback;
    }

    void UnbindKey(int vKey) {
        m_customBinds.erase(vKey);
    }

    // Register in-game chat or console command (e.g. "/god", "/freecraft")
    void RegisterCommand(const std::string& commandName, CommandHandler handler, const std::string& desc = "") {
        std::string lowerCmd = commandName;
        std::transform(lowerCmd.begin(), lowerCmd.end(), lowerCmd.begin(), ::tolower);
        std::lock_guard<std::mutex> lock(m_cmdMutex);
        m_commands[lowerCmd] = handler;
        if (!desc.empty()) {
            m_descriptions[lowerCmd] = desc;
        }
    }

    // Execute a single command string with CrashGuard sandbox protection & typo suggestions
    bool ExecuteSingleCommand(const std::string& fullCommand) {
        if (fullCommand.empty()) return false;

        std::string cmd = fullCommand;
        if (cmd[0] == '/') cmd = cmd.substr(1);

        size_t spaceIdx = cmd.find(' ');
        std::string cmdName = (spaceIdx == std::string::npos) ? cmd : cmd.substr(0, spaceIdx);
        std::string args = (spaceIdx == std::string::npos) ? "" : cmd.substr(spaceIdx + 1);

        std::transform(cmdName.begin(), cmdName.end(), cmdName.begin(), ::tolower);

        CommandHandler handler = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_cmdMutex);
            auto it = m_commands.find(cmdName);
            if (it != m_commands.end()) {
                handler = it->second;
            }
        }

        if (handler) {
            // Execute command inside isolated CrashGuard sandbox shield
            bool executedOk = true;
            std::string guardName = "Command:" + cmdName;
            DreamCrashGuard::ExecuteSafely(guardName.c_str(), [&]() {
                handler(args);
            });
            return executedOk;
        }

        // Fuzzy typo suggestion
        std::string suggestion;
        {
            std::lock_guard<std::mutex> lock(m_cmdMutex);
            for (const auto& [name, _] : m_commands) {
                if (name.length() >= 3 && cmdName.length() >= 3) {
                    if (name.find(cmdName) == 0 || cmdName.find(name) == 0) {
                        suggestion = name;
                        break;
                    }
                }
            }
        }

        DREAM_LOG_WARN("DreamOverlay: Unknown command '/%s'", cmdName.c_str());
        if (!suggestion.empty()) {
            printf("\033[91m[Dream CMD] Unknown command '%s'. Did you mean '\033[93m%s\033[91m'? (Type 'help' for commands)\033[0m\n",
                   cmdName.c_str(), suggestion.c_str());
        } else {
            printf("\033[91m[Dream CMD] Unknown command '%s'. Type 'help' or '?' for available commands.\033[0m\n", cmdName.c_str());
        }
        fflush(stdout);
        return false;
    }

    // Execute command line string (supports command chaining with ';')
    bool ExecuteCommand(const std::string& fullCommand) {
        if (fullCommand.empty()) return false;

        // Support command chaining with ';' (e.g. "god on; stamina on; fastprod 1")
        if (fullCommand.find(';') != std::string::npos) {
            std::istringstream stream(fullCommand);
            std::string sub;
            bool anySuccess = false;
            while (std::getline(stream, sub, ';')) {
                while (!sub.empty() && (sub.front() == ' ' || sub.front() == '\t')) sub.erase(sub.begin());
                while (!sub.empty() && (sub.back() == ' ' || sub.back() == '\t' || sub.back() == '\r' || sub.back() == '\n')) sub.pop_back();
                if (!sub.empty()) {
                    if (ExecuteSingleCommand(sub)) anySuccess = true;
                }
            }
            return anySuccess;
        }

        return ExecuteSingleCommand(fullCommand);
    }

    // Poll hotkeys synchronously inside game frame loop
    void PollInput() {
        if (DreamServerGuard::IsDedicatedServer()) return;

        static bool s_menuKeyPressed = false;
        if (GetAsyncKeyState(m_menuKey) & 0x8000) {
            if (!s_menuKeyPressed) {
                ToggleMenu();
                s_menuKeyPressed = true;
            }
        } else {
            s_menuKeyPressed = false;
        }

        static bool s_consoleKeyPressed = false;
        if (GetAsyncKeyState(m_consoleKey) & 0x8000) {
            if (!s_consoleKeyPressed) {
                ToggleConsole();
                s_consoleKeyPressed = true;
            }
        } else {
            s_consoleKeyPressed = false;
        }

        // Custom binds with debounce tracking (prevents rapid toggle bouncing!)
        for (const auto& [key, cb] : m_customBinds) {
            bool isDown = (GetAsyncKeyState(key) & 0x8000) != 0;
            bool wasDown = m_keyStates[key];
            if (isDown && !wasDown) {
                if (cb) cb();
            }
            m_keyStates[key] = isDown;
        }
    }

    // Start background hotkey listener thread
    void StartHotkeyWatcher(int pollIntervalMs = 50) {
        if (m_watcherRunning.load()) return;
        if (DreamServerGuard::IsDedicatedServer()) {
            DREAM_LOG_INFO("DreamOverlay: Headless dedicated server detected. Suppressing hotkey watcher");
            return;
        }

        m_watcherRunning.store(true);
        m_watcherThread = std::thread([this, pollIntervalMs]() {
            while (m_watcherRunning.load()) {
                PollInput();
                std::this_thread::sleep_for(std::chrono::milliseconds(pollIntervalMs));
            }
        });
        DREAM_LOG_INFO("DreamOverlay: Background hotkey watcher started (Menu: 0x%02X, Console: 0x%02X)",
                       m_menuKey, m_consoleKey);
    }

    void StopHotkeyWatcher() {
        if (!m_watcherRunning.load()) return;
        m_watcherRunning.store(false);
        if (m_watcherThread.joinable()) {
            if (m_watcherThread.get_id() != std::this_thread::get_id()) {
                m_watcherThread.join();
            } else {
                m_watcherThread.detach();
            }
        }
        DREAM_LOG_INFO("DreamOverlay: Background hotkey watcher stopped");
    }

    // Register preset cheat commands (opt-in extension)
    void RegisterPresetCommands();

    // Initialize default generic engine commands
    void InitializeDefaultCommands() {
        RegisterCommand("console", [this](const std::string& args) {
            ToggleConsole();
        });

        RegisterCommand("clear", [](const std::string&) {
            printf("\033[2J\033[H");
            fflush(stdout);
        });

        RegisterCommand("profile", [](const std::string& args) {
            if (args == "clear" || args == "reset") {
                DreamProfiler::Instance().Reset();
            } else if (args == "off" || args == "disable") {
                DreamProfiler::Instance().SetEnabled(false);
                DREAM_LOG_INFO("DreamProfiler: Disabled");
            } else if (args == "on" || args == "enable") {
                DreamProfiler::Instance().SetEnabled(true);
                DREAM_LOG_INFO("DreamProfiler: Enabled");
            } else {
                DreamProfiler::Instance().PrintReport();
            }
        });

        RegisterCommand("help", [this](const std::string& args) {
            if (!args.empty()) {
                std::string target = args;
                std::transform(target.begin(), target.end(), target.begin(), ::tolower);
                if (target == "sig") {
                    printf("\n\033[96m==================== HELP: SIG COMMAND ====================\033[0m\n");
                    printf("  sig <pattern>          - Scan module for AOB pattern using AVX2/SSE SIMD\n");
                    printf("  sig gen <addr> [len]   - Automatically generate unique, resilient AOB signature\n");
                    printf("  Example: sig gen 0x7FF7BD135F10 24\n");
                    printf("\033[96m===========================================================\033[0m\n\n");
                    fflush(stdout);
                    return;
                }
                if (target == "trace") {
                    printf("\n\033[96m=================== HELP: TRACE COMMAND ===================\033[0m\n");
                    printf("  trace <addr> [label]   - Non-blocking parameter tracer (RCX, RDX, R8, R9, XMM0)\n");
                    printf("  trace list             - List all actively traced functions\n");
                    printf("  trace remove <id>      - Stop tracing specific hook ID\n");
                    printf("  trace clear            - Remove all active trace hooks\n");
                    printf("\033[96m===========================================================\033[0m\n\n");
                    fflush(stdout);
                    return;
                }
                if (target == "inspect") {
                    printf("\n\033[96m================== HELP: INSPECT COMMAND ==================\033[0m\n");
                    printf("  inspect <addr>         - In-depth RTTI Class & field structure inspector\n");
                    printf("  Resolves MSVC VTable -> Complete Object Locator -> Class Name\n");
                    printf("  Example: inspect 0x7FF745892100\n");
                    printf("\033[96m===========================================================\033[0m\n\n");
                    fflush(stdout);
                    return;
                }
                if (target == "backup") {
                    printf("\n\033[96m=================== HELP: BACKUP COMMAND ==================\033[0m\n");
                    printf("  backup [tag]           - Immediate hot-backup of savegame/world0\n");
                    printf("  backup list            - List all savegame backup archives\n");
                    printf("  Example: backup pre_boss_fight\n");
                    printf("\033[96m===========================================================\033[0m\n\n");
                    fflush(stdout);
                    return;
                }
            }

            printf("\n\033[93m======================= DREAM COMMANDS =======================\033[0m\n");
            printf("  help [cmd]           - Display command help or specific command guide\n");
            printf("  status (alias: s)    - Display framework status, mode & policies\n");
            printf("  perf (alias: p)      - Display memory working set and crash prevention stats\n");
            printf("  health               - Check physical memory and background process health\n");
            printf("  clear / cls / c      - Clear console screen\n");
            printf("  mods (alias: m)      - List all installed & active dream mods\n");
            printf("  reload               - Live reload framework presets and configuration\n");
            printf("  backup [tag]         - Hot-backup world0 savegame to save_backups/ immediately\n");
            printf("  backup list          - List all savegame backup archives\n");
            printf("  check / doctor       - Audit mod integrity, DLL health, loader conflicts & VEH\n");
            printf("  tail [N] (alias: t)  - View last N logs directly in console (e.g. tail 50)\n");
            printf("  grep <pattern> [N]   - Search recent logs in memory for keyword\n");
            printf("  verbose <ch> <0-3>   - Set channel verbosity level [0=Off..3=Trace] (alias: v)\n");
            printf("  get [key]            - Read live mod settings (e.g. get aoe.radius or get all)\n");
            printf("  set <key> <val>      - Tweak live settings in memory (e.g. set aoe.radius 4.5)\n");
            printf("  players              - List active players and entity IDs\n");
            printf("  say / broadcast <msg>- Broadcast an announcement banner to all connected players\n");
            printf("  kick <id|name> [msg] - Kick a player from the server with optional reason\n");
            printf("  snapshot             - Export comprehensive diagnostic report file\n");
            printf("  dump <addr> [len]    - Hex dump memory buffer safely (e.g. dump 0x7FF... 64)\n");
            printf("  sig <pattern>        - AVX2/SSE SIMD scan IDA pattern in game executable\n");
            printf("  sig gen <addr> [len] - Auto-generate unique resilient AOB signature for function\n");
            printf("  inspect <addr>       - In-depth RTTI class, vtable and field structure inspector\n");
            printf("  trace <addr> [label] - In-engine live non-blocking function parameter tracing\n");
            printf("  ecs <id>             - Inspect ECS entity info by ID\n");
            printf("  lua <expr>           - Execute live LuaJIT script / expression (alias: l)\n");
            printf("  luafile <path>       - Execute live Lua script file from disk\n");
            printf("  exit / quit / q      - Shut down application cleanly\n");

            bool hasGod = false;
            {
                std::lock_guard<std::mutex> lock(m_cmdMutex);
                hasGod = (m_commands.find("god") != m_commands.end());
            }

            if (hasGod) {
                printf("\n  [Presets / Admin Extension Enabled]:\n");
                printf("  god [on/off]         - Toggle invincible God Mode\n");
                printf("  ghost [on/off]       - Toggle enemy perception invisibility\n");
                printf("  stamina [on/off]     - Toggle unlimited stamina\n");
                printf("  mana [on/off]        - Toggle unlimited mana\n");
                printf("  shroud [on/off]      - Toggle unlimited shroud time\n");
                printf("  craft [free/off]     - Toggle zero-cost crafting\n");
                printf("  split [on/off]       - Toggle item duplication on stack split\n");
                printf("  item [on/off]        - Toggle infinite item use (potions, food, arrows)\n");
                printf("  flight [on/off]      - Toggle unlimited glider flight\n");
                printf("  durability [on/off]  - Toggle unbreakable weapons and tools\n");
                printf("  oneshot [on/off]     - Toggle 1-hit kill against enemies & objects\n");
                printf("  fastprod [sec]       - Set fast production speed (e.g. fastprod 1)\n");
                printf("  mine [multiplier]    - Set mining damage multiplier (e.g. mine 10)\n");
                printf("  save [filename]      - Save preset state to INI file\n");
                printf("  load [filename]      - Load preset state from INI file\n");
            }

            {
                std::lock_guard<std::mutex> lock(m_cmdMutex);
                if (!m_descriptions.empty()) {
                    printf("\n  \033[97m[Plugin / Registered Commands]:\033[0m\n");
                    for (const auto& [name, desc] : m_descriptions) {
                        printf("  \033[92m%-20s\033[0m - %s\n", name.c_str(), desc.c_str());
                    }
                }
            }
            printf("\033[93m==============================================================\033[0m\n\n");
            fflush(stdout);
        });

        RegisterCommand("mods", [](const std::string&) {
            auto mods = DreamModRegistry::Instance().GetAllMods();
            printf("\n\033[95m======================= INSTALLED DREAM MODS =======================\033[0m\n");
            if (!mods.empty()) {
                for (const auto& m : mods) {
                    printf("  * \033[97m%s\033[0m (v%s) by %s\n", m.name.c_str(), m.version.c_str(), m.author.c_str());
                    if (!m.description.empty()) printf("    Desc: %s\n", m.description.c_str());
                    if (!m.directory.empty()) printf("    Path: %s\n", m.directory.c_str());
                }
            } else {
                WIN32_FIND_DATAW fd;
                HANDLE hFind = FindFirstFileW(L"mods\\*", &fd);
                if (hFind != INVALID_HANDLE_VALUE) {
                    int count = 0;
                    do {
                        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                            if (wcscmp(fd.cFileName, L".") != 0 && wcscmp(fd.cFileName, L"..") != 0 &&
                                fd.cFileName[0] != L'_' && wcsstr(fd.cFileName, L"backup") == nullptr) {
                                count++;
                                wchar_t iniPath[MAX_PATH];
                                swprintf_s(iniPath, L"mods\\%ls\\native-plugin.ini", fd.cFileName);
                                int enabled = 1;
                                wchar_t dllName[MAX_PATH] = {0};
                                if (GetFileAttributesW(iniPath) != INVALID_FILE_ATTRIBUTES) {
                                    enabled = GetPrivateProfileIntW(L"Plugin", L"Enabled", 1, iniPath);
                                    GetPrivateProfileStringW(L"Plugin", L"Dll", L"", dllName, MAX_PATH, iniPath);
                                }
                                printf("  * \033[97m%ls\033[0m [%s]\n",
                                       fd.cFileName,
                                       enabled ? "\033[92mACTIVE / ENABLED\033[0m" : "\033[91mDISABLED\033[0m");
                                if (wcslen(dllName) > 0) {
                                    printf("    Module: %ls\n", dllName);
                                }
                            }
                        }
                    } while (FindNextFileW(hFind, &fd));
                    FindClose(hFind);
                    if (count == 0) printf("  No plugins found in 'mods/' directory.\n");
                } else {
                    printf("  Could not inspect 'mods/' directory.\n");
                }
            }
            printf("\033[95m====================================================================\033[0m\n\n");
            fflush(stdout);
        });

        RegisterCommand("status", [this](const std::string&) {
            printf("\n\033[96m======================= FRAMEWORK STATUS =======================\033[0m\n");
            bool isServer = DreamServerGuard::IsDedicatedServer();
            printf("  Execution Mode      : %s\n", isServer ? "Dedicated Server (Headless)" : "Game Client");
            printf("  Process ID          : %lu\n", GetCurrentProcessId());
            if (!isServer) {
                printf("  In-Game Menu        : %s\n", m_isMenuOpen.load() ? "OPEN" : "CLOSED");
                printf("  Console Window      : %s\n", DreamLog::Instance().IsConsoleAllocated() ? "ACTIVE" : "INACTIVE");
            }
            printf("  Strict Server Policy: %s\n", DreamServerPolicy::Instance().IsStrictMode() ? "\033[91mENFORCED\033[0m" : "\033[92mOFF\033[0m");
            if (m_commands.find("god") != m_commands.end()) {
                auto& p = DreamPresets::Instance();
                const auto& s = p.State();
                const auto& pol = DreamServerPolicy::Instance();
                printf("  --- Presets Module Status ---\n");
                printf("  God Mode            : %s %s\n", s.godMode.load() ? "\033[92mENABLED\033[0m" : "\033[90mDISABLED\033[0m", pol.IsFeatureAllowed(PresetFeature::GodMode) ? "" : "\033[91m[POLICY BLOCKED]\033[0m");
                printf("  Ghost Mode (Invis)  : %s %s\n", s.ghostMode.load() ? "\033[92mENABLED\033[0m" : "\033[90mDISABLED\033[0m", pol.IsFeatureAllowed(PresetFeature::GhostMode) ? "" : "\033[91m[POLICY BLOCKED]\033[0m");
                printf("  Infinite Stamina    : %s %s\n", s.infiniteStamina.load() ? "\033[92mENABLED\033[0m" : "\033[90mDISABLED\033[0m", pol.IsFeatureAllowed(PresetFeature::InfiniteStamina) ? "" : "\033[91m[POLICY BLOCKED]\033[0m");
                printf("  Infinite Mana       : %s %s\n", s.infiniteMana.load() ? "\033[92mENABLED\033[0m" : "\033[90mDISABLED\033[0m", pol.IsFeatureAllowed(PresetFeature::InfiniteMana) ? "" : "\033[91m[POLICY BLOCKED]\033[0m");
                printf("  Free Crafting       : %s %s\n", s.freeCraft.load() ? "\033[92mENABLED\033[0m" : "\033[90mDISABLED\033[0m", pol.IsFeatureAllowed(PresetFeature::FreeCraft) ? "" : "\033[91m[POLICY BLOCKED]\033[0m");
                printf("  Infinite Item Split : %s %s\n", s.infiniteItemSplit.load() ? "\033[92mENABLED\033[0m" : "\033[90mDISABLED\033[0m", pol.IsFeatureAllowed(PresetFeature::InfiniteItemSplit) ? "" : "\033[91m[POLICY BLOCKED]\033[0m");
                printf("  Infinite Item Use   : %s %s\n", s.infiniteItemUse.load() ? "\033[92mENABLED\033[0m" : "\033[90mDISABLED\033[0m", pol.IsFeatureAllowed(PresetFeature::InfiniteItemUse) ? "" : "\033[91m[POLICY BLOCKED]\033[0m");
                printf("  Unlimited Flight    : %s %s\n", s.unlimitedFlight.load() ? "\033[92mENABLED\033[0m" : "\033[90mDISABLED\033[0m", pol.IsFeatureAllowed(PresetFeature::UnlimitedFlight) ? "" : "\033[91m[POLICY BLOCKED]\033[0m");
                printf("  Infinite Durability : %s %s\n", s.infiniteDurability.load() ? "\033[92mENABLED\033[0m" : "\033[90mDISABLED\033[0m", pol.IsFeatureAllowed(PresetFeature::InfiniteDurability) ? "" : "\033[91m[POLICY BLOCKED]\033[0m");
                printf("  One-Shot Kills      : %s %s\n", s.oneShotKill.load() ? "\033[92mENABLED\033[0m" : "\033[90mDISABLED\033[0m", pol.IsFeatureAllowed(PresetFeature::OneShotKill) ? "" : "\033[91m[POLICY BLOCKED]\033[0m");
                printf("  Fast Production     : %s (%.1fs) %s\n", s.fastProduction.load() ? "\033[92mENABLED\033[0m" : "\033[90mDISABLED\033[0m", s.productionDuration.load(), pol.IsFeatureAllowed(PresetFeature::FastProduction) ? "" : "\033[91m[POLICY BLOCKED]\033[0m");
                printf("  Super Mining        : %s (x%.1f) %s\n", s.superMining.load() ? "\033[92mENABLED\033[0m" : "\033[90mDISABLED\033[0m", s.miningMult.load(), pol.IsFeatureAllowed(PresetFeature::SuperMining) ? "" : "\033[91m[POLICY BLOCKED]\033[0m");
            }
            printf("\033[96m================================================================\033[0m\n\n");
            fflush(stdout);
        });

        RegisterCommand("perf", [](const std::string&) {
            MEMORYSTATUSEX memStatus{};
            memStatus.dwLength = sizeof(memStatus);
            GlobalMemoryStatusEx(&memStatus);
            PROCESS_MEMORY_COUNTERS pmc{};
            GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
            printf("\n\033[92m======================= SERVER PERFORMANCE =======================\033[0m\n");
            printf("  Process Working Set    : %.2f MB\n", static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0));
            printf("  Peak Working Set       : %.2f MB\n", static_cast<double>(pmc.PeakWorkingSetSize) / (1024.0 * 1024.0));
            printf("  System RAM Free        : %llu MB / %llu MB (Memory Load: %u%%)\n",
                   memStatus.ullAvailPhys / (1024 * 1024), memStatus.ullTotalPhys / (1024 * 1024), memStatus.dwMemoryLoad);
            printf("  Crashes Prevented (VEH): %zu\n", DreamCrashGuard::Instance().GetTotalCrashesPrevented());
            printf("\033[92m==================================================================\033[0m\n\n");
            fflush(stdout);
        });

        RegisterCommand("health", [](const std::string&) {
            printf("\033[96m[DreamOverlay] Auditing system health and background processes...\033[0m\n");
            DreamWatchdog::Instance().CheckSystemHealth();
            fflush(stdout);
        });

        RegisterCommand("reload", [](const std::string&) {
            printf("\033[93m[DreamOverlay] Reloading presets, configuration files (TOML/INI) and Lua scripts...\033[0m\n");
            DreamPresets::Instance().LoadConfig("dreamkurumi_config.ini");
            bool reloaded = DreamConfig.ReloadFromFiles();
            DreamLua.ResetRuntimeState();
            size_t scriptsCount = DreamLua.LoadScriptsFromDirectory("mods/scripts");
            if (reloaded) {
                printf("\033[92m[DreamOverlay] Successfully hot-reloaded configuration files from disk!\033[0m\n");
            } else {
                printf("\033[93m[DreamOverlay] Presets reloaded. (No additional mod TOML/INI found)\033[0m\n");
            }
            if (scriptsCount > 0) {
                printf("\033[92m[DreamOverlay] Loaded and executed %zu Lua script(s) from mods/scripts/\033[0m\n", scriptsCount);
            }
            fflush(stdout);
        });

        RegisterCommand("dump", [](const std::string& args) {
            if (args.empty()) {
                printf("\033[91mUsage: dump <addr_hex> [length_dec]\033[0m\n");
                return;
            }
            std::istringstream iss(args);
            std::string addrStr;
            size_t len = 64;
            iss >> addrStr;
            if (iss >> len) {
                if (len == 0) len = 64;
                if (len > 1024) len = 1024;
            }
            uintptr_t addr = std::strtoull(addrStr.c_str(), nullptr, 16);
            if (!MemoryHelper::IsValidPtr(reinterpret_cast<const void*>(addr))) {
                printf("\033[91m[Dump Error] Invalid or unreadable address 0x%llX\033[0m\n", (unsigned long long)addr);
                return;
            }
            std::vector<uint8_t> buf(len);
            bool readOk = false;
            DreamCrashGuard::ExecuteSafely("ConsoleDump", [&]() {
                std::memcpy(buf.data(), reinterpret_cast<const void*>(addr), len);
                readOk = true;
            });
            if (readOk) {
                DreamLog::HexDump("Memory Dump", buf.data(), len);
            } else {
                printf("\033[91m[Dump Error] Access violation reading 0x%llX\033[0m\n", (unsigned long long)addr);
            }
        });

        RegisterCommand("sig", [](const std::string& args) {
            if (args.empty()) {
                printf("\033[91mUsage: sig <ida_pattern>          - Scan module for AOB pattern\n"
                       "       sig gen <addr_hex> [len]   - Automatically generate signature for function\033[0m\n");
                return;
            }

            if (args.rfind("gen ", 0) == 0 || args.rfind("gen", 0) == 0) {
                std::istringstream iss(args);
                std::string subCmd, addrStr;
                iss >> subCmd >> addrStr;
                if (addrStr.empty()) {
                    printf("\033[91mUsage: sig gen <addr_hex> [len]\033[0m\n");
                    return;
                }
                uintptr_t addr = std::strtoull(addrStr.c_str(), nullptr, 16);
                size_t minLen = 24;
                iss >> minLen;

                printf("\033[96m[DreamScanner] Generating resilient AOB Signature for 0x%llX (min %zu bytes)...\033[0m\n",
                       static_cast<unsigned long long>(addr), minLen);

                auto sig = DreamScanner::GenerateSignature(reinterpret_cast<const void*>(addr), minLen);
                if (sig.pattern.empty()) {
                    printf("\033[91m[DreamScanner] Failed to generate signature for address 0x%llX\033[0m\n",
                           static_cast<unsigned long long>(addr));
                    return;
                }

                printf("\n\033[92m==================== GENERATED SIGNATURE ====================\033[0m\n");
                printf("  Address    : 0x%llX\n", static_cast<unsigned long long>(addr));
                printf("  Length     : %zu bytes\n", sig.byteLength);
                printf("  Uniqueness : %s (%zu match%s in main module)\n",
                       sig.isUnique ? "\033[92mUNIQUE MATCH (100% RELIABLE)\033[0m" : "\033[93mMULTIPLE MATCHES\033[0m",
                       sig.matchCount, sig.matchCount == 1 ? "" : "es");
                printf("  IDA Pattern: \033[96m%s\033[0m\n\n", sig.pattern.c_str());
                printf("  C++ Snippet:\n");
                printf("    \033[93m%s\033[0m\n", sig.cppSnippet.c_str());
                printf("\033[92m============================================================\033[0m\n\n");
                fflush(stdout);
                return;
            }

            printf("\033[96m[DreamScanner] Scanning main module for pattern: '%s'...\033[0m\n", args.c_str());
            void* res = DreamScanner::ScanModule(nullptr, args);
            if (res) {
                HMODULE hMod = GetModuleHandleA(nullptr);
                uintptr_t rva = reinterpret_cast<uintptr_t>(res) - reinterpret_cast<uintptr_t>(hMod);
                printf("\033[92m[DreamScanner] MATCH FOUND at 0x%p (Base+0x%llX)\033[0m\n",
                       res, static_cast<unsigned long long>(rva));
                DreamLog::HexDump("Pattern Match", res, 32);
            } else {
                printf("\033[91m[DreamScanner] Pattern not found in main module.\033[0m\n");
            }
        });

        RegisterCommand("inspect", [](const std::string& args) {
            if (args.empty()) {
                printf("\033[91mUsage: inspect <address_hex> (e.g. inspect 0x7FF71A4010)\033[0m\n");
                return;
            }
            uintptr_t addr = std::strtoull(args.c_str(), nullptr, 16);
            const void* ptr = reinterpret_cast<const void*>(addr);
            if (!MemoryHelper::IsValidPtr(ptr)) {
                printf("\033[91m[Inspect] Invalid or inaccessible pointer: 0x%p\033[0m\n", ptr);
                return;
            }

            printf("\n\033[96m======================= DREAM OBJECT INSPECTOR =======================\033[0m\n");
            printf("  Address: 0x%p\n", ptr);

            HMODULE hMod = nullptr;
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   reinterpret_cast<LPCSTR>(ptr), &hMod) && hMod) {
                char modPath[MAX_PATH] = {0};
                GetModuleFileNameA(hMod, modPath, sizeof(modPath));
                const char* modBaseName = strrchr(modPath, '\\');
                modBaseName = modBaseName ? modBaseName + 1 : modPath;
                uintptr_t rva = addr - reinterpret_cast<uintptr_t>(hMod);
                printf("  Module : %s + 0x%llX (Base: 0x%p)\n", modBaseName, static_cast<unsigned long long>(rva), hMod);
            }

            std::string rttiClass = DreamRtti::GetClassName(ptr);
            if (!rttiClass.empty()) {
                void** vtable = *reinterpret_cast<void** const*>(ptr);
                printf("  \033[92mRTTI Class : %s\033[0m\n", rttiClass.c_str());
                printf("  VTable     : 0x%p\n", vtable);
            } else {
                printf("  RTTI Class : (Non-polymorphic / No RTTI)\n");
            }

            printf("\n  \033[93m[Memory Field Interpretation]\033[0m\n");
            const uint8_t* bytePtr = reinterpret_cast<const uint8_t*>(ptr);
            for (size_t off = 0; off < 64; off += 8) {
                uint64_t u64Val = 0;
                uint32_t u32_1 = 0, u32_2 = 0;
                float f1 = 0.0f, f2 = 0.0f;
                DreamCrashGuard::ExecuteSafely("InspectRead", [&]() {
                    u64Val = *reinterpret_cast<const uint64_t*>(bytePtr + off);
                    u32_1 = *reinterpret_cast<const uint32_t*>(bytePtr + off);
                    u32_2 = *reinterpret_cast<const uint32_t*>(bytePtr + off + 4);
                    f1 = *reinterpret_cast<const float*>(bytePtr + off);
                    f2 = *reinterpret_cast<const float*>(bytePtr + off + 4);
                });

                printf("  +0x%02X | Hex: 0x%016llX | u32: %10u, %10u | f32: %8.2f, %8.2f",
                       static_cast<unsigned int>(off),
                       static_cast<unsigned long long>(u64Val),
                       u32_1, u32_2, f1, f2);

                const void* subPtr = reinterpret_cast<const void*>(u64Val);
                if (MemoryHelper::IsValidPtr(subPtr)) {
                    std::string subClass = DreamRtti::GetClassName(subPtr);
                    if (!subClass.empty()) {
                        printf(" -> \033[96m[%s]\033[0m", subClass.c_str());
                    } else {
                        const char* s = reinterpret_cast<const char*>(subPtr);
                        if (MemoryHelper::IsValidPtr(s) && s[0] >= 32 && s[0] <= 126 && s[1] >= 32 && s[1] <= 126) {
                            printf(" -> \"%.16s\"", s);
                        }
                    }
                }
                printf("\n");
            }
            printf("\033[96m======================================================================\033[0m\n\n");
            fflush(stdout);
        });

        RegisterCommand("ecs", [](const std::string& args) {
            if (args.empty()) {
                printf("\033[91mUsage: ecs <entity_id> (e.g. ecs 1045 or ecs 0x415)\033[0m\n");
                return;
            }
            uint32_t entId = static_cast<uint32_t>(std::strtoul(args.c_str(), nullptr, 0));
            printf("\033[96m[ECS Inspector] Querying Entity ID: %u (0x%X)...\033[0m\n", entId, entId);
            auto& ctx = DreamGameContext::Instance();
            void* pWorld = ctx.GetWorld();
            void* pPlayer = ctx.GetLocalPlayer();
            printf("  GameContext World   : %p\n", pWorld);
            printf("  GameContext Player  : %p\n", pPlayer);
            if (pPlayer) {
                DreamVector3 pos = ctx.GetPlayerPosition();
                printf("  Player Position     : (%.2f, %.2f, %.2f)\n", pos.x, pos.y, pos.z);
            }
        });

        // --------------------------------------------------------------------
        // In-Engine Non-Blocking Function Tracer (trace <addr> [label])
        // --------------------------------------------------------------------
        RegisterCommand("trace", [](const std::string& args) {
            if (args.empty() || args == "help") {
                printf("\n\033[96m======================= DREAM LIVE TRACER =======================\033[0m\n");
                printf("  Usage: trace <addr_hex> [label]   - Trace function calls & register parameters live\n");
                printf("  Usage: trace list                 - List all actively traced functions\n");
                printf("  Usage: trace remove <id>          - Stop tracing specific hook ID\n");
                printf("  Usage: trace clear                - Remove all active trace hooks\n");
                printf("  Example: trace 0x7FF7BD135F10 CombatDamage\n");
                printf("\033[96m=================================================================\033[0m\n\n");
                fflush(stdout);
                return;
            }

            std::istringstream iss(args);
            std::string subCmd;
            iss >> subCmd;

            if (subCmd == "list") {
                auto list = DreamTracer::Instance().GetActiveTraces();
                printf("\n\033[96m====================== ACTIVE TRACE HOOKS (%zu) ======================\033[0m\n", list.size());
                if (list.empty()) {
                    printf("  No active trace hooks.\n");
                } else {
                    printf("  %-4s | %-18s | %-20s | %-10s\n", "ID", "Address", "Label", "Hits");
                    printf("  -----+--------------------+----------------------+-----------\n");
                    for (const auto& t : list) {
                        printf("  %-4u | 0x%-16p | %-20s | %-10llu\n",
                               t.id, t.address, t.label.c_str(), static_cast<unsigned long long>(t.count));
                    }
                }
                printf("\033[96m====================================================================\033[0m\n\n");
                fflush(stdout);
                return;
            }

            if (subCmd == "clear") {
                DreamTracer::Instance().ClearAll();
                printf("\033[92m[DreamTracer] Cleared all active trace hooks.\033[0m\n");
                fflush(stdout);
                return;
            }

            if (subCmd == "remove" || subCmd == "off") {
                uint32_t id = 0;
                if (iss >> id) {
                    if (DreamTracer::Instance().RemoveTrace(id)) {
                        printf("\033[92m[DreamTracer] Stopped tracing hook ID %u.\033[0m\n", id);
                    } else {
                        printf("\033[91m[DreamTracer] Trace hook ID %u not found.\033[0m\n", id);
                    }
                } else {
                    printf("\033[91mUsage: trace remove <id>\033[0m\n");
                }
                fflush(stdout);
                return;
            }

            uintptr_t addr = std::strtoull(subCmd.c_str(), nullptr, 16);
            if (!MemoryHelper::IsValidPtr(reinterpret_cast<const void*>(addr))) {
                printf("\033[91m[DreamTracer] Invalid or inaccessible address: '%s'\033[0m\n", subCmd.c_str());
                return;
            }

            std::string label;
            iss >> label;
            uint32_t id = DreamTracer::Instance().AddTrace(reinterpret_cast<void*>(addr), label);
            if (id > 0) {
                printf("\033[92m[DreamTracer] Successfully installed Live Trace on 0x%p (ID: %u, Label: '%s')\033[0m\n",
                       reinterpret_cast<void*>(addr), id, label.empty() ? "None" : label.c_str());
                printf("  Any incoming function calls will log RCX, RDX, R8, R9, XMM0 without freezing the game.\n");
            } else {
                printf("\033[91m[DreamTracer] Failed to install trace hook on 0x%p\033[0m\n", reinterpret_cast<void*>(addr));
            }
            fflush(stdout);
        });

        RegisterCommand("lua", [](const std::string& args) {
            if (args.empty()) {
                printf("\033[91mUsage: lua <expression or script>\033[0m\n");
                return;
            }
            std::string err;
            bool ok = DreamLua.ExecuteString(args, &err);
            if (!ok) {
                printf("\033[91m[Lua REPL Error] %s\033[0m\n", err.c_str());
            } else {
                printf("\033[92m[Lua REPL] Executed successfully.\033[0m\n");
            }
        });

        RegisterCommand("luafile", [](const std::string& args) {
            if (args.empty()) {
                printf("\033[91mUsage: luafile <script_path.lua>\033[0m\n");
                return;
            }
            std::string err;
            bool ok = DreamLua.ExecuteFile(args, &err);
            if (!ok) {
                printf("\033[91m[Lua File Error] %s\033[0m\n", err.c_str());
            } else {
                printf("\033[92m[Lua File] Executed '%s' successfully.\033[0m\n", args.c_str());
            }
        });

        // --------------------------------------------------------------------
        // In-Console Tail & Grep
        // --------------------------------------------------------------------
        auto tailHandler = [](const std::string& args) {
            size_t count = 20;
            if (!args.empty()) {
                size_t parsed = static_cast<size_t>(std::strtoul(args.c_str(), nullptr, 10));
                if (parsed > 0) count = (parsed > 200) ? 200 : parsed;
            }
            auto logs = DreamLog::Instance().GetRecentLogs(count);
            printf("\n\033[96m=================== RECENT LOGS (Last %zu) ===================\033[0m\n", logs.size());
            if (logs.empty()) {
                printf("  No recent log entries in memory.\n");
            } else {
                for (const auto& entry : logs) {
                    printf("%s", entry.rawFormatted.c_str());
                }
            }
            printf("\033[96m==============================================================\033[0m\n\n");
            fflush(stdout);
        };
        RegisterCommand("tail", tailHandler);
        RegisterCommand("log", tailHandler);
        RegisterCommand("t", tailHandler);

        RegisterCommand("grep", [](const std::string& args) {
            if (args.empty()) {
                printf("\033[91mUsage: grep <pattern> [max_count]\033[0m\n");
                return;
            }
            std::istringstream iss(args);
            std::string pattern;
            size_t count = 30;
            iss >> pattern;
            if (iss >> count) {
                if (count == 0) count = 30;
                if (count > 100) count = 100;
            }
            auto matches = DreamLog::Instance().GrepLogs(pattern, count);
            printf("\n\033[93m=================== GREP: '%s' (%zu matches) ===================\033[0m\n",
                   pattern.c_str(), matches.size());
            if (matches.empty()) {
                printf("  No matching lines found in memory.\n");
            } else {
                for (const auto& m : matches) {
                    printf("%s", m.rawFormatted.c_str());
                }
            }
            printf("\033[93m=================================================================\033[0m\n\n");
            fflush(stdout);
        });

        // --------------------------------------------------------------------
        // Channel-Based Verbosity Control
        // --------------------------------------------------------------------
        auto verboseHandler = [](const std::string& args) {
            if (args.empty()) {
                printf("\n\033[95m===================== VERBOSE CHANNELS =====================\033[0m\n");
                printf("  Levels: 0=Silent, 1=Summary (Default), 2=Detailed, 3=Trace\n");
                printf("  Channels:\n");
                const char* chList[] = { "global", "aoe", "loot", "admin", "xp", "hook", "net", "ecs" };
                for (const char* ch : chList) {
                    auto lvl = DreamLog::Instance().GetChannelVerbosity(DreamLog::ChannelFromString(ch));
                    const char* lvlDesc = (lvl == VerbosityLevel::Silent) ? "Silent" :
                                          (lvl == VerbosityLevel::Summary) ? "Summary" :
                                          (lvl == VerbosityLevel::Detailed) ? "Detailed" : "Trace";
                    printf("  - %-8s : [%u] %s\n", ch, static_cast<unsigned>(lvl), lvlDesc);
                }
                printf("\n  Usage: verbose <channel|all> <0-3> [duration_seconds]\n");
                printf("  Example: verbose aoe 3 30   (Trace AoE for 30s then auto-reverts)\n");
                printf("\033[95m============================================================\033[0m\n\n");
                fflush(stdout);
                return;
            }
            std::istringstream iss(args);
            std::string chName;
            int level = 1;
            uint32_t duration = 0;
            iss >> chName >> level;
            if (iss >> duration) {
                // timed burst
            }
            DreamLog::Instance().SetChannelVerbosity(chName, level, duration);
            if (duration > 0) {
                printf("\033[92m[DreamLog] Set '%s' verbosity to %d for %u seconds (timed burst)\033[0m\n",
                       chName.c_str(), level, duration);
            } else {
                printf("\033[92m[DreamLog] Set '%s' verbosity to %d\033[0m\n", chName.c_str(), level);
            }
        };
        RegisterCommand("verbose", verboseHandler);
        RegisterCommand("v", verboseHandler);

        // --------------------------------------------------------------------
        // Live Config Tweaker (get / set)
        // --------------------------------------------------------------------
        RegisterCommand("get", [](const std::string& args) {
            if (args.empty() || args == "all") {
                auto allProps = DreamConfig.GetAll();
                printf("\n\033[96m======================= LIVE CONFIG PROPERTIES =======================\033[0m\n");
                for (const auto& p : allProps) {
                    printf("  %-18s = \033[93m%-8s\033[0m (%s)\n",
                           p.key.c_str(), p.valueStr.c_str(), p.description.c_str());
                }
                printf("\033[96m=====================================================================\033[0m\n\n");
                fflush(stdout);
                return;
            }
            std::string val = DreamConfig.Get(args);
            if (val.empty()) {
                printf("\033[91mUnknown config key '%s'. Type 'get all' to list.\033[0m\n", args.c_str());
            } else {
                printf("\033[92m%s = %s\033[0m\n", args.c_str(), val.c_str());
            }
        });

        RegisterCommand("set", [](const std::string& args) {
            std::istringstream iss(args);
            std::string key, val;
            if (!(iss >> key >> val)) {
                printf("\033[91mUsage: set <module.key> <value> (e.g. set aoe.radius 4.5)\033[0m\n");
                return;
            }
            if (DreamConfig.Set(key, val)) {
                printf("\033[92m[Config] %s set to '%s' (Applied live in memory!)\033[0m\n", key.c_str(), val.c_str());
            } else {
                printf("\033[91mUnknown config key '%s'. Type 'get all' to see valid keys.\033[0m\n", key.c_str());
            }
        });

        // --------------------------------------------------------------------
        // Player Inspection (Dedicated Server & Client)
        // --------------------------------------------------------------------
        RegisterCommand("players", [](const std::string&) {
            printf("\n\033[96m======================= ONLINE PLAYERS =======================\033[0m\n");
            auto players = DreamPlayerRegistry::Instance().GetAllPlayers();
            if (players.empty()) {
                if (DreamServerGuard::IsDedicatedServer()) {
                    printf("  No remote players currently connected to dedicated server.\n");
                } else {
                    auto& ctx = DreamGameContext::Instance();
                    void* pPlayer = ctx.GetLocalPlayer();
                    if (pPlayer) {
                        DreamVector3 pos = ctx.GetPlayerPosition();
                        printf("  * Local Player Entity : 0x%p\n", pPlayer);
                        printf("    World Coordinates   : (%.2fm, %.2fm, %.2fm)\n", pos.x, pos.y, pos.z);
                    } else {
                        printf("  Local player not spawned yet.\n");
                    }
                }
            } else {
                printf("  Total Connected: %zu\n\n", players.size());
                printf("  %-10s | %-24s | %-10s | %-12s\n", "Entity ID", "Player Name", "Role", "Online Time");
                printf("  -----------+--------------------------+------------+-------------\n");
                uint64_t now = GetTickCount64();
                for (const auto& p : players) {
                    uint64_t durSec = (now >= p.loginTimestamp) ? ((now - p.loginTimestamp) / 1000) : 0;
                    uint32_t mins = static_cast<uint32_t>(durSec / 60);
                    uint32_t secs = static_cast<uint32_t>(durSec % 60);
                    char timeBuf[32];
                    snprintf(timeBuf, sizeof(timeBuf), "%02um %02us", mins, secs);

                    printf("  0x%-8X | %-24s | %-10s | %-12s\n",
                           p.entityId,
                           p.name.c_str(),
                           p.isAdmin ? "\033[93mADMIN\033[0m" : "\033[92mPLAYER\033[0m",
                           timeBuf);
                }
            }
            printf("\033[96m==============================================================\033[0m\n\n");
            fflush(stdout);
        });

        // --------------------------------------------------------------------
        // Server Announcement & Chat Broadcast (broadcast / say)
        // --------------------------------------------------------------------
        auto broadcastHandler = [](const std::string& args) {
            if (args.empty()) {
                printf("\033[91mUsage: broadcast <message> (alias: say <message>)\033[0m\n");
                return;
            }
            DreamPlayerRegistry::Instance().Broadcast(args);
            printf("\033[92m[Broadcast Sent] \"%s\"\033[0m\n", args.c_str());
            fflush(stdout);
        };
        RegisterCommand("broadcast", broadcastHandler);
        RegisterCommand("say", broadcastHandler);

        // --------------------------------------------------------------------
        // Player Kick (kick <id | name> [reason])
        // --------------------------------------------------------------------
        RegisterCommand("kick", [](const std::string& args) {
            if (args.empty()) {
                printf("\033[91mUsage: kick <entity_id | player_name> [reason]\033[0m\n");
                printf("  Example: kick 0x415\n");
                printf("  Example: kick Peemm Bad behavior\n");
                return;
            }

            std::istringstream iss(args);
            std::string target;
            iss >> target;
            std::string reason;
            std::string word;
            while (iss >> word) {
                if (!reason.empty()) reason += " ";
                reason += word;
            }
            if (reason.empty()) {
                reason = "Kicked by server administrator";
            }

            ConnectedPlayerInfo player;
            if (!DreamPlayerRegistry::Instance().FindPlayer(target, player)) {
                printf("\033[91m[Kick Error] Player '%s' not found online. Type 'players' to view active players.\033[0m\n",
                       target.c_str());
                return;
            }

            printf("\033[93m[Kick] Kicking player '%s' (EntityId: 0x%X) - Reason: \"%s\"...\033[0m\n",
                   player.name.c_str(), player.entityId, reason.c_str());

            bool ok = DreamPlayerRegistry::Instance().Kick(player.entityId, reason);
            if (ok) {
                printf("\033[92m[Kick Success] Player '%s' (0x%X) has been kicked from the server.\033[0m\n",
                       player.name.c_str(), player.entityId);
            } else {
                printf("\033[91m[Kick Failed] Failed to kick entity 0x%X.\033[0m\n", player.entityId);
            }
            fflush(stdout);
        });

        // --------------------------------------------------------------------
        // Savegame Hot Backup (backup / savebackup)
        // --------------------------------------------------------------------
        auto backupHandler = [](const std::string& args) {
            if (args == "list") {
                auto list = DreamBackup::Instance().ListBackups();
                printf("\n\033[96m======================= SAVEGAME BACKUPS =======================\033[0m\n");
                if (list.empty()) {
                    printf("  No backup archives found in 'save_backups/' directory.\n");
                } else {
                    printf("  Total Backups: %zu\n\n", list.size());
                    printf("  %-44s | %-10s\n", "Filename", "File Size");
                    printf("  ---------------------------------------------+-----------\n");
                    for (const auto& b : list) {
                        double mb = static_cast<double>(b.fileSize) / (1024.0 * 1024.0);
                        printf("  %-44s | %6.2f MB\n", b.filename.c_str(), mb);
                    }
                }
                printf("\033[96m================================================================\033[0m\n\n");
                fflush(stdout);
                return;
            }

            printf("\033[96m[DreamBackup] Starting world save hot-backup...\033[0m\n");
            auto res = DreamBackup::Instance().CreateBackup(args);
            if (res.success) {
                printf("\033[92m[DreamBackup] %s\033[0m\n", res.message.c_str());
                for (const auto& f : res.backedUpFiles) {
                    printf("   -> %s\n", f.c_str());
                }
            } else {
                printf("\033[91m[DreamBackup Failed] %s\033[0m\n", res.message.c_str());
            }
            fflush(stdout);
        };
        RegisterCommand("backup", backupHandler);
        RegisterCommand("savebackup", backupHandler);

        // --------------------------------------------------------------------
        // Mod Integrity & Health Checker (check / doctor)
        // --------------------------------------------------------------------
        auto checkHandler = [](const std::string&) {
            printf("\n\033[96m==================== MOD INTEGRITY & HEALTH CHECK ====================\033[0m\n");
            int issues = 0;

            // 1. Loader & DLL Conflict Check
            printf("  \033[97m[Loader Audit]\033[0m\n");
            bool hasDInput8 = (GetFileAttributesA("dinput8.dll") != INVALID_FILE_ATTRIBUTES);
            bool hasDbghelp = (GetFileAttributesA("dbghelp.dll") != INVALID_FILE_ATTRIBUTES);

            if (hasDInput8) {
                printf("    [\033[92mOK\033[0m] Primary Mod Loader: dinput8.dll present and active\n");
            } else {
                printf("    [\033[93mWARN\033[0m] dinput8.dll not found in current directory\n");
            }

            if (hasDbghelp) {
                printf("    [\033[91mCONFLICT\033[0m] Active dbghelp.dll detected alongside dinput8.dll!\n");
                printf("             Recommendation: Rename to dbghelp.dll.disabled to avoid loader clash.\n");
                issues++;
            } else {
                printf("    [\033[92mOK\033[0m] No obsolete dbghelp.dll conflicts detected (clean loader environment)\n");
            }

            // 2. dreamkurumiHT Harmony Framework Core
            printf("\n  \033[97m[Framework Engine]\033[0m\n");
            HMODULE hEngine = GetModuleHandleA("dreamkurumiHT.dll");
            if (hEngine) {
                printf("    [\033[92mOK\033[0m] dreamkurumiHT.dll loaded into process (Base: 0x%p)\n", (void*)hEngine);
                bool hasInline = (GetProcAddress(hEngine, "DK_InlineHook") != nullptr);
                bool hasMid = (GetProcAddress(hEngine, "DK_MidHook") != nullptr);
                bool hasResolve = (GetProcAddress(hEngine, "DK_Resolve") != nullptr);
                printf("    [%s] Core Harmony Exports: DK_InlineHook=%s, DK_MidHook=%s, DK_Resolve=%s\n",
                       (hasInline && hasMid && hasResolve) ? "\033[92mOK\033[0m" : "\033[91mFAIL\033[0m",
                       hasInline ? "Yes" : "No", hasMid ? "Yes" : "No", hasResolve ? "Yes" : "No");
            } else {
                printf("    [\033[93mWARN\033[0m] dreamkurumiHT.dll is not currently linked as loaded module\n");
            }

            // 3. Mod Modules Audit
            printf("\n  \033[97m[Installed Mods Audit]\033[0m\n");
            WIN32_FIND_DATAA fd;
            HANDLE hFind = FindFirstFileA("mods\\*", &fd);
            int modCount = 0;
            if (hFind != INVALID_HANDLE_VALUE) {
                do {
                    if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                        strcmp(fd.cFileName, ".") != 0 && strcmp(fd.cFileName, "..") != 0 &&
                        fd.cFileName[0] != '_') {

                        std::string modDir = std::string("mods\\") + fd.cFileName;
                        std::string tomlPath = modDir + "\\config.toml";
                        std::string iniPath = modDir + "\\config.ini";
                        std::string pluginIni = modDir + "\\native-plugin.ini";

                        bool hasToml = (GetFileAttributesA(tomlPath.c_str()) != INVALID_FILE_ATTRIBUTES);
                        bool hasIni = (GetFileAttributesA(iniPath.c_str()) != INVALID_FILE_ATTRIBUTES);
                        bool hasPluginIni = (GetFileAttributesA(pluginIni.c_str()) != INVALID_FILE_ATTRIBUTES);

                        modCount++;
                        printf("    * \033[97m%-24s\033[0m: Config=[%s%s%s] PluginDef=[%s]\n",
                               fd.cFileName,
                               hasToml ? "\033[92mTOML\033[0m" : "",
                               (hasToml && hasIni) ? "+" : "",
                               hasIni ? "\033[93mINI\033[0m" : (hasToml ? "" : "\033[91mNONE\033[0m"),
                               hasPluginIni ? "\033[92mOK\033[0m" : "\033[90mN/A\033[0m");
                    }
                } while (FindNextFileA(hFind, &fd));
                FindClose(hFind);
            }
            if (modCount == 0) {
                printf("    No mods found in 'mods/' directory.\n");
            }

            // 4. Memory & VEH Guard Status
            printf("\n  \033[97m[Runtime Health & Memory Guard]\033[0m\n");
            size_t crashesPrevented = DreamCrashGuard::Instance().GetTotalCrashesPrevented();
            printf("    [\033[92mOK\033[0m] VEH CrashGuard Protection : Active (%zu crash intercepts prevented)\n", crashesPrevented);

            MEMORYSTATUSEX memStatus{};
            memStatus.dwLength = sizeof(memStatus);
            GlobalMemoryStatusEx(&memStatus);
            PROCESS_MEMORY_COUNTERS pmc{};
            GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
            double workingSetMb = static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
            printf("    [\033[92mOK\033[0m] Process Working Set       : %.2f MB\n", workingSetMb);
            printf("    [\033[92mOK\033[0m] System Memory Load        : %u%% (Free: %llu MB)\n",
                   memStatus.dwMemoryLoad, memStatus.ullAvailPhys / (1024 * 1024));

            printf("\n  Summary: %d issue(s) detected. %s\n",
                   issues, issues == 0 ? "\033[92mAll systems healthy!\033[0m" : "\033[93mReview warnings above.\033[0m");
            printf("\033[96m======================================================================\033[0m\n\n");
            fflush(stdout);
        };
        RegisterCommand("check", checkHandler);
        RegisterCommand("doctor", checkHandler);

        // --------------------------------------------------------------------
        // Diagnostics Snapshot Export
        // --------------------------------------------------------------------
        RegisterCommand("snapshot", [](const std::string&) {
            SYSTEMTIME st;
            GetLocalTime(&st);
            char filename[64];
            snprintf(filename, sizeof(filename), "snapshot_%04d%02d%02d_%02d%02d%02d.txt",
                     st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

            FILE* fp = fopen(filename, "w");
            if (!fp) {
                printf("\033[91mFailed to create snapshot file '%s'\033[0m\n", filename);
                return;
            }

            MEMORYSTATUSEX memStatus{};
            memStatus.dwLength = sizeof(memStatus);
            GlobalMemoryStatusEx(&memStatus);
            PROCESS_MEMORY_COUNTERS pmc{};
            GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));

            fprintf(fp, "======================================================================\n");
            fprintf(fp, "  ENSHROUDED DREAM DIAGNOSTIC SNAPSHOT\n");
            fprintf(fp, "  Timestamp: %04d-%02d-%02d %02d:%02d:%02d\n",
                    st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
            fprintf(fp, "======================================================================\n\n");

            fprintf(fp, "[SYSTEM PERFORMANCE]\n");
            fprintf(fp, "  Process Working Set : %.2f MB\n", pmc.WorkingSetSize / (1024.0 * 1024.0));
            fprintf(fp, "  Peak Working Set    : %.2f MB\n", pmc.PeakWorkingSetSize / (1024.0 * 1024.0));
            fprintf(fp, "  System RAM Free     : %llu MB / %llu MB (Load: %u%%)\n",
                    memStatus.ullAvailPhys / (1024 * 1024), memStatus.ullTotalPhys / (1024 * 1024), memStatus.dwMemoryLoad);
            fprintf(fp, "  Crashes Prevented   : %zu\n\n", DreamCrashGuard::Instance().GetTotalCrashesPrevented());

            fprintf(fp, "[LIVE CONFIGURATION]\n");
            for (const auto& p : DreamConfig.GetAll()) {
                fprintf(fp, "  %-18s = %-8s (%s)\n", p.key.c_str(), p.valueStr.c_str(), p.description.c_str());
            }

            fprintf(fp, "\n[RECENT LOG ENTRIES]\n");
            auto recent = DreamLog::Instance().GetRecentLogs(200);
            for (const auto& e : recent) {
                fprintf(fp, "%s", e.rawFormatted.c_str());
            }

            fclose(fp);
            printf("\033[92m[DreamOverlay] Diagnostic snapshot saved to '%s' (%zu logs included)!\033[0m\n",
                   filename, recent.size());
            fflush(stdout);
        });

        // --------------------------------------------------------------------
        // Short Aliases & Core Controls
        // --------------------------------------------------------------------
        RegisterCommand("p", [this](const std::string& args) { ExecuteCommand("perf " + args); });
        RegisterCommand("s", [this](const std::string& args) { ExecuteCommand("status " + args); });
        RegisterCommand("c", [this](const std::string& args) { ExecuteCommand("clear " + args); });
        RegisterCommand("cls", [this](const std::string& args) { ExecuteCommand("clear " + args); });
        RegisterCommand("h", [this](const std::string& args) { ExecuteCommand("help " + args); });
        RegisterCommand("?", [this](const std::string& args) { ExecuteCommand("help " + args); });
        RegisterCommand("m", [this](const std::string& args) { ExecuteCommand("mods " + args); });
        RegisterCommand("l", [this](const std::string& args) { ExecuteCommand("lua " + args); });
        RegisterCommand("exit", [](const std::string&) {
            printf("\n\033[93m[Dream CMD] Shutting down application cleanly...\033[0m\n");
            fflush(stdout);
            ExitProcess(0);
        });
        RegisterCommand("quit", [this](const std::string& args) { ExecuteCommand("exit " + args); });
        RegisterCommand("q", [this](const std::string& args) { ExecuteCommand("exit " + args); });
    }

    void StartConsoleInputListener() {
        if (m_consoleListenerRunning.load()) return;
        if (DreamServerGuard::IsDedicatedServer() || GetConsoleWindow() == NULL) {
            return;
        }

        m_consoleListenerRunning.store(true);
        m_consoleListenerThread = std::thread([this]() {
            HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
            if (hOut != INVALID_HANDLE_VALUE && hOut != NULL) {
                DWORD dwMode = 0;
                if (GetConsoleMode(hOut, &dwMode)) {
                    SetConsoleMode(hOut, dwMode | 0x0004 /* ENABLE_VIRTUAL_TERMINAL_PROCESSING */);
                }
            }
            SetConsoleOutputCP(CP_UTF8);
            SetConsoleCP(CP_UTF8);

            HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
            if (hIn == INVALID_HANDLE_VALUE || hIn == NULL) {
                m_consoleListenerRunning.store(false);
                return;
            }

            printf("\n\033[96mdream\033[0m> ");
            fflush(stdout);

            while (m_consoleListenerRunning.load()) {
                DWORD fileType = GetFileType(hIn);
                if (fileType == FILE_TYPE_PIPE) {
                    DWORD bytesAvail = 0;
                    if (!PeekNamedPipe(hIn, NULL, 0, NULL, &bytesAvail, NULL) || bytesAvail == 0) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(150));
                        continue;
                    }
                } else if (fileType == FILE_TYPE_CHAR) {
                    DWORD numEvents = 0;
                    if (!GetNumberOfConsoleInputEvents(hIn, &numEvents) || numEvents == 0) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(150));
                        continue;
                    }
                }

                std::string line;
                if (std::getline(std::cin, line)) {
                    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) {
                        line.pop_back();
                    }
                    if (!line.empty()) {
                        ExecuteCommand(line);
                    }
                    if (m_consoleListenerRunning.load()) {
                        printf("\033[96mdream\033[0m> ");
                        fflush(stdout);
                    }
                } else {
                    std::cin.clear();
                    std::this_thread::sleep_for(std::chrono::milliseconds(150));
                }
            }
        });
        DREAM_LOG_INFO("DreamOverlay: Interactive console command listener started (Type 'help' or '?' for commands)");
    }

    void StopConsoleInputListener() {
        if (!m_consoleListenerRunning.load()) return;
        m_consoleListenerRunning.store(false);
        if (m_consoleListenerThread.joinable()) {
            if (m_consoleListenerThread.get_id() != std::this_thread::get_id()) {
                m_consoleListenerThread.join();
            } else {
                m_consoleListenerThread.detach();
            }
        }
    }

private:
    DreamOverlay() {
        InitializeDefaultCommands();
    }

    ~DreamOverlay() {
        StopHotkeyWatcher();
        StopConsoleInputListener();
    }

    int m_menuKey{VK_F9};      // Default F9 (Shroudforge standard)
    int m_consoleKey{VK_F10};  // Default F10 (Shroudforge standard)
    bool m_audioFeedback{false};

    std::atomic<bool> m_isMenuOpen{false};
    std::atomic<bool> m_isConsoleOpen{false};

    std::function<void(bool)> m_onMenuToggle;
    std::function<void(bool)> m_onConsoleToggle;

    std::unordered_map<int, KeyCallback> m_customBinds;
    std::unordered_map<int, bool> m_keyStates;
    std::unordered_map<std::string, CommandHandler> m_commands;
    std::unordered_map<std::string, std::string> m_descriptions;
    mutable std::mutex m_cmdMutex;

    std::atomic<bool> m_watcherRunning{false};
    std::thread m_watcherThread;

    std::atomic<bool> m_consoleListenerRunning{false};
    std::thread m_consoleListenerThread;
};

// ============================================================================
// Implementation of DreamPresets::RegisterCommands and DreamOverlay::RegisterPresetCommands
// Decoupled extension connecting Presets to DreamOverlay
// ============================================================================
inline void DreamPresets::RegisterCommands(DreamOverlay& overlay) {
    overlay.RegisterCommand("god", [](const std::string& args) {
        bool on = (args == "1" || args == "on" || args == "true" || args.empty());
        if (!DreamPresets::Instance().SetGodMode(on) && on) {
            printf("\033[91m[POLICY BLOCKED] Command '/god' is blocked by server policy!\033[0m\n");
            fflush(stdout);
        }
    });

    overlay.RegisterCommand("ghost", [](const std::string& args) {
        bool on = (args == "1" || args == "on" || args == "true" || args.empty());
        if (!DreamPresets::Instance().SetGhostMode(on) && on) {
            printf("\033[91m[POLICY BLOCKED] Command '/ghost' is blocked by server policy!\033[0m\n");
            fflush(stdout);
        }
    });

    overlay.RegisterCommand("stamina", [](const std::string& args) {
        bool on = (args == "1" || args == "on" || args == "true" || args.empty());
        if (!DreamPresets::Instance().SetInfiniteStamina(on) && on) {
            printf("\033[91m[POLICY BLOCKED] Command '/stamina' is blocked by server policy!\033[0m\n");
            fflush(stdout);
        }
    });

    overlay.RegisterCommand("mana", [](const std::string& args) {
        bool on = (args == "1" || args == "on" || args == "true" || args.empty());
        if (!DreamPresets::Instance().SetInfiniteMana(on) && on) {
            printf("\033[91m[POLICY BLOCKED] Command '/mana' is blocked by server policy!\033[0m\n");
            fflush(stdout);
        }
    });

    overlay.RegisterCommand("shroud", [](const std::string& args) {
        bool on = (args == "1" || args == "on" || args == "true" || args.empty());
        if (!DreamPresets::Instance().SetInfiniteShroud(on) && on) {
            printf("\033[91m[POLICY BLOCKED] Command '/shroud' is blocked by server policy!\033[0m\n");
            fflush(stdout);
        }
    });

    auto craftHandler = [](const std::string& args) {
        bool on = (args == "1" || args == "on" || args == "free" || args.empty());
        if (!DreamPresets::Instance().SetFreeCraft(on) && on) {
            printf("\033[91m[POLICY BLOCKED] Command '/craft' is blocked by server policy!\033[0m\n");
            fflush(stdout);
        }
    };
    overlay.RegisterCommand("craft", craftHandler, "Toggle zero-cost crafting");
    overlay.RegisterCommand("freecraft", craftHandler, "Selective player FreeCraft control (AdminControlServer)");
    overlay.RegisterCommand("fc", craftHandler, "Alias for freecraft");

    overlay.RegisterCommand("mine", [](const std::string& args) {
        float mult = 5.0f;
        if (!args.empty()) {
            mult = static_cast<float>(atof(args.c_str()));
        }
        if (!DreamPresets::Instance().SetSuperMining(true, mult)) {
            printf("\033[91m[POLICY BLOCKED] Command '/mine' is blocked by server policy!\033[0m\n");
            fflush(stdout);
        }
    });

    overlay.RegisterCommand("split", [](const std::string& args) {
        bool on = (args == "1" || args == "on" || args == "true" || args.empty());
        if (!DreamPresets::Instance().SetInfiniteItemSplit(on) && on) {
            printf("\033[91m[POLICY BLOCKED] Command '/split' is blocked by server policy!\033[0m\n");
            fflush(stdout);
        }
    });

    overlay.RegisterCommand("item", [](const std::string& args) {
        bool on = (args == "1" || args == "on" || args == "true" || args.empty());
        if (!DreamPresets::Instance().SetInfiniteItemUse(on) && on) {
            printf("\033[91m[POLICY BLOCKED] Command '/item' is blocked by server policy!\033[0m\n");
            fflush(stdout);
        }
    });

    overlay.RegisterCommand("flight", [](const std::string& args) {
        bool on = (args == "1" || args == "on" || args == "true" || args.empty());
        if (!DreamPresets::Instance().SetUnlimitedFlight(on) && on) {
            printf("\033[91m[POLICY BLOCKED] Command '/flight' is blocked by server policy!\033[0m\n");
            fflush(stdout);
        }
    });

    overlay.RegisterCommand("cost", [](const std::string& args) {
        bool on = (args == "0" || args == "free" || args == "none" || args.empty());
        if (!DreamPresets::Instance().SetNoResourceCost(on) && on) {
            printf("\033[91m[POLICY BLOCKED] Command '/cost' is blocked by server policy!\033[0m\n");
            fflush(stdout);
        }
    });

    overlay.RegisterCommand("durability", [](const std::string& args) {
        bool on = (args == "1" || args == "on" || args == "true" || args.empty());
        if (!DreamPresets::Instance().SetInfiniteDurability(on) && on) {
            printf("\033[91m[POLICY BLOCKED] Command '/durability' is blocked by server policy!\033[0m\n");
            fflush(stdout);
        }
    });

    overlay.RegisterCommand("oneshot", [](const std::string& args) {
        bool on = (args == "1" || args == "on" || args == "true" || args.empty());
        if (!DreamPresets::Instance().SetOneShotKill(on) && on) {
            printf("\033[91m[POLICY BLOCKED] Command '/oneshot' is blocked by server policy!\033[0m\n");
            fflush(stdout);
        }
    });

    overlay.RegisterCommand("fastprod", [](const std::string& args) {
        float sec = 1.0f;
        if (!args.empty() && args != "on" && args != "1") {
            sec = static_cast<float>(atof(args.c_str()));
            if (sec <= 0.0f) sec = 1.0f;
        }
        bool on = (args != "0" && args != "off");
        if (!DreamPresets::Instance().SetFastProduction(on, sec) && on) {
            printf("\033[91m[POLICY BLOCKED] Command '/fastprod' is blocked by server policy!\033[0m\n");
            fflush(stdout);
        }
    });

    overlay.RegisterCommand("save", [](const std::string& args) {
        std::string path = args.empty() ? "dreamkurumi_config.ini" : args;
        DreamPresets::Instance().SaveConfig(path);
    });

    overlay.RegisterCommand("load", [](const std::string& args) {
        std::string path = args.empty() ? "dreamkurumi_config.ini" : args;
        DreamPresets::Instance().LoadConfig(path);
    });

    overlay.RegisterCommand("policy", [](const std::string& args) {
        if (args == "strict") {
            DreamServerPolicy::Instance().EnforceStrictServerRules();
        } else if (args == "allowall" || args == "open") {
            DreamServerPolicy::Instance().AllowAllFeatures();
        } else if (args.rfind("load", 0) == 0) {
            std::string file = args.length() > 5 ? args.substr(5) : "server_policy.ini";
            DreamServerPolicy::Instance().LoadPolicy(file);
        } else if (args.rfind("save", 0) == 0) {
            std::string file = args.length() > 5 ? args.substr(5) : "server_policy.ini";
            DreamServerPolicy::Instance().SavePolicy(file);
        } else {
            printf("\n  Usage: /policy [strict | allowall | load <file> | save <file>]\n\n");
            fflush(stdout);
        }
    });

    DREAM_LOG_INFO("DreamPresets: In-game preset cheat commands successfully registered with DreamOverlay");
}

inline void DreamOverlay::RegisterPresetCommands() {
    DreamPresets::Instance().RegisterCommands(*this);
}

} // namespace dreamkurumi

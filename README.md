# dreamkurumiHT Framework (Dream Core)

> **Native Harmony Hooking Framework and Mod Loader for Enshrouded (C++20 / x64 Windows)**  
> **Framework Version:** `3.9.1` | **Architecture:** x64 Windows | **Target Game:** Enshrouded 1.0+ (Client & Dedicated Server)

---

<p align="center">
  <b>Language Switcher / สลับภาษา:</b><br>
  <a href="README.md">🇬🇧 <b>English (Default)</b></a> •
  <a href="README_TH.md">🇹🇭 <b>ภาษาไทย (Thai)</b></a><br><br>
  <a href="https://github.com/kurumiht/Dream-Loader/releases/download/v3.9.1/Dream-Core-v3.9.1.zip">
    <img src="https://img.shields.io/badge/Download-Dream--Core%20v3.9.1%20(Framework)-brightgreen?style=for-the-badge&logo=windows" alt="Download Dream-Core Framework" />
  </a>
  &nbsp;&nbsp;
  <a href="https://github.com/kurumiht/Dream-Loader/releases/download/v3.9.1/Dream-AoE-Harvest-v1.5.1.zip">
    <img src="https://img.shields.io/badge/Download-Dream--AoE--Harvest%20v1.5.1%20(Mod)-blue?style=for-the-badge&logo=windows" alt="Download Dream-AoE-Harvest Mod" />
  </a>
</p>

---

## Overview

**dreamkurumiHT** is a native modding runtime and Harmony-style hooking engine designed specifically for **Enshrouded (Keen Holistic Engine)** on 64-bit Windows. It provides a modular C-ABI plugin interface and detouring capabilities for developers building native mods in C++20, C# (.NET Native AOT), or LuaJIT.

### Key Capabilities
- **Declarative Harmony Hooking:** Intuitive `Prefix` and `Postfix` detours with return value and argument mutation, as well as `SkipOriginal` support.
- **Dynamic Register Interception (`DreamMidHook`):** Reads and modifies CPU registers directly in C++ without requiring external assembly files.
- **Near-Memory JIT Trampolines:** Automatically allocates hook trampolines within relative branch range of game code.
- **Thread-Safe Relocation:** Suspends threads during hook installation and safely relocates instruction pointers (`RIP`).
- **Dedicated Server Session Synchronization (`DreamSessionSync`):** Detects server reboots and reconnects to prevent crashes from stale memory pointers.
- **Resilience Engine (`DreamCrashGuard`):** Structured exception handling prevents callback memory faults from crashing the game or server.
- **Non-Intrusive Defaults:** Diagnostic overlays are disabled by default to keep player hotkeys free from conflicts.

---

## Downloads & Packages

Downloads are provided in two separate packages:
1. **`Dream-Core-v3.9.1.zip`**: The core native hooking engine (`dreamkurumiHT.dll`), universal proxy loader (`dinput8.dll`), C-ABI headers (`include/`), templates, and documentation.
2. **`Dream-AoE-Harvest-v1.5.1.zip`**: Standalone crop harvesting mod plugin (3.5m radius, F8 toggle, Shift+E vanilla mode). Works for all players on the server without requiring admin privileges.

---

## Installation

### For Game Client (Steam)
1. Extract `Dream-Core-v3.9.1.zip` directly into your Enshrouded game folder (where `enshrouded.exe` is located).
2. To add mods (e.g., `Dream-AoE-Harvest`), extract the mod archive into the `mods/` directory.
3. Launch Enshrouded via Steam.

### For Dedicated Server
1. Extract `Dream-Core-v3.9.1.zip` directly into your server directory (where `enshrouded_server.exe` is located).
2. Place desired mod plugins in the `mods/` directory.
3. Start `enshrouded_server.exe`.

---

## Detailed Documentation

To keep this document concise, in-depth architectural and developer guides are split into separate documents:

- **[Technical Architecture & Capabilities](docs/ARCHITECTURE.md)**: Deep dive into the internal subsystems, JIT trampolines, thread safety, and the 98 automated unit tests.
- **[Developer Guide (English)](DEVELOPER_GUIDE_EN.md)**: Complete guide to creating custom C++ and C# mods using the C-ABI plugin API.
- **[Developer Guide (Thai)](DEVELOPER_GUIDE.md)**: คู่มือการพัฒนาม็อดสำหรับผู้พัฒนาภาษาไทย
- **[Game Update Runbook](GAME_UPDATE_GUIDE.md)**: Maintenance checklist, AOB patterns, and RVAs for Enshrouded game updates.
- **[Changelog](CHANGELOG.md)**: Detailed history of updates, fixes, and improvements across versions.

---

## Quickstart: Creating a Plugin

```cpp
#include <dreamkurumiHT/dreamkurumiHT_plugin.h>

class MyPlugin {
public:
    static void Init() {
        auto& host = dreamkurumi::PluginHost::Instance();
        host.LogInfo("[MyPlugin] Initialized successfully.");
    }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        MyPlugin::Init();
    }
    return TRUE;
}
```

---

## License

This project is released under the **MIT License**.

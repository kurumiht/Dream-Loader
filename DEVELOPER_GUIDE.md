# Enshrouded 1.0 Modding Architecture & Developer Guide

## 1. Core Architectural Rules

### Rule 1: Strict Architectural Separation of `dreamkurumiHT` and Plugins
- **`dreamkurumiHT` is strictly the Core Native Engine & Hooking Framework.**
  - Its sole purpose is providing hooking primitives (`InlineHook`, `MidHook`, `PatchBytes`), AOB pattern scanning, near-memory trampolines, watchdog, crash guards, and dynamic plugin binding.
  - **Never hardcode game-specific mod logic directly into `dreamkurumiHT`.**
  - **Framework is Framework, Mods are Plugins.** They must always remain decoupled.
- **Every Mod must be completely modular and live in its own independent directory:**
  - `mods\dreamkurumiHT\` (`dreamkurumiHT.dll`): Core Harmony Engine only.
  - `mods\Your-Plugin-Name\` (`YourPluginName.dll` + `config.toml`): Standalone mod plugin.
- Mods link dynamically to `dreamkurumiHT.dll` at runtime via the zero-dependency C-ABI header (`dreamkurumiHT_plugin.h`).

---

### Rule 2: Universal Player Access Policy
- **Universal Player Access by Default:**
  - Gameplay and quality-of-life plugins created on this platform should be usable by all players connected to the dedicated server by default.
  - Any `AdminOnly` configuration option should default to `false`.
  - Normal players should be able to enjoy mod features without requiring administrative privileges.
- **Admin-Whitelist Exception:**
  - Only specific administrative tools, debug commands, or cheat capabilities (e.g., GodMode, GhostMode, FreeCraft, Infinite Shroud/Mana) should be restricted to verified administrators (`adminGroupName` / `adminPlayerName` / admin entity IDs).

---

## 2. dreamkurumiHT API & Hooking Quickstart

### 1. Mid-Function Hooking (`DreamMidHook`) - Zero Assembly
Intercept any instruction boundary in memory and read/write CPU registers dynamically:
```cpp
#include <dreamkurumiHT/dreamkurumiHT.hpp>

static dreamkurumi::DreamMidHook s_myHook;

static void OnInstructionHit(dreamkurumi::DreamCpuContext& ctx) {
    // Read or modify 64-bit general purpose registers:
    uint64_t targetRcx = ctx.rcx;
    ctx.rax = 1; // Change return or computation value

    // Read or modify SIMD vector registers (XMM0..XMM15):
    float speedMult = ctx.GetFloatXmm(0);
    ctx.SetFloatXmm(0, speedMult * 1.5f);
}

// Installation (zero assembly file required):
void InstallMyHook(void* targetAddress) {
    s_myHook.Install(targetAddress, OnInstructionHit);
}
```

### 2. Standard Inline Hooking (`DreamInlineHook`)
Redirect whole functions with prefix, postfix, or original bypass:
```cpp
typedef int (*FnOriginalGameFunc)(void* pContext, int value);
static dreamkurumi::DreamInlineHook s_fnHook;

static int Detour_GameFunc(void* pContext, int value) {
    // Prefix logic
    if (value < 0) return 0; // Skip original

    // Call original function via trampoline
    auto orig = reinterpret_cast<FnOriginalGameFunc>(s_fnHook.GetOriginal());
    int result = orig(pContext, value);

    // Postfix logic
    return result * 2;
}
```

### 3. AOB Pattern Scanning (`DreamScanner`)
Search `.text` sections for resilient byte patterns at runtime:
```cpp
uintptr_t fnAddr = dreamkurumi::DreamScanner::FindPattern(
    "48 89 5C 24 ?? 48 89 74 24 ?? 57 48 83 EC"
);
if (fnAddr) {
    dreamkurumi::LogInfo("Resolved function at: 0x%p", (void*)fnAddr);
}
```

### 4. Live Configuration Binding (`DreamConfig`)
Bind typed variables with automatic file-watching and hot-reloading:
```cpp
// Automatically binds to config.toml:
static auto cfgEnabled = dreamkurumi::DreamConfig::Bind("General", "Enabled", true, "Master switch");
static auto cfgMultiplier = dreamkurumi::DreamConfig::Bind("General", "Multiplier", 1.5f, "Effect multiplier");

void OnUpdate() {
    if (!cfgEnabled.Get()) return;
    float currentMult = cfgMultiplier.Get();
}
```

---

## 3. Building the Core Engine and Custom Plugins

### Building dreamkurumiHT Core
The core framework is compiled using GCC with C++20:
```cmd
build.bat
```
This builds `dreamkurumiHT.dll` inside `mods/dreamkurumiHT/` and copies dependencies automatically.

### Scaffolding and Building a New Plugin
To generate a brand new mod project in seconds:
```cmd
create_mod.bat "MyCustomPlugin"
```
To compile your plugin:
```cmd
cd mods\MyCustomPlugin
build.bat
```

---

## 4. In-Engine Reverse Engineering Suite

dreamkurumiHT includes built-in live reverse engineering tools designed to run directly in-game without causing watchdog timeouts, game freezes, or server disconnects:

### 1. In-Engine Parameter Tracer (`trace`)
- **CLI Command:** `trace <addr_hex> [label]`
- **Capabilities:** Hooks target address using near-memory midhook trampoline. Captures `RCX`, `RDX`, `R8`, `R9`, `XMM0`, `XMM1` in nanoseconds and continues execution at full frame rate (144 FPS).
- **Sub-commands:** `trace list`, `trace remove <id>`, `trace clear`.

### 2. Auto-Resilient AOB Signature Generator (`sig gen`)
- **CLI Command:** `sig gen <addr_hex> [min_bytes=24]`
- **Capabilities:** Disassembles instructions forward from target address, auto-masks displacement bytes and relative offsets (`??`), and verifies pattern uniqueness across `.text` section.

### 3. Structural RTTI Introspector (`rtti`)
- **CLI Command:** `rtti <addr_hex>`
- **Capabilities:** Resolves MSVC RTTI Complete Object Locator from pointer's VTable and extracts demangled type descriptor names.

### 4. Interactive Command REPL
- Integrated in-game console accessible via dedicated server console or in-game hotkey.
- Type `help` or `?` inside console for full command syntax.

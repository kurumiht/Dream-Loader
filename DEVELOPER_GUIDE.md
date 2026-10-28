# dreamkurumiHT - Mod Developer Comprehensive Guide
> **คู่มือการพัฒนาม็อด Enshrouded ฉบับสมบูรณ์สำหรับนักพัฒนา (C++20 & C# .NET Native AOT)**  
> Framework เวอร์ชั่น: `3.4.0` | สถาปัตยกรรม: x64 Windows | อัปเดตล่าสุด: ตุลาคม 2026  
> [🇬🇧 Switch to English Guide (DEVELOPER_GUIDE_EN.md)](DEVELOPER_GUIDE_EN.md)

---

## สารบัญ (Table of Contents)
1. [กฎเหล็กและสถาปัตยกรรมของ Framework (Core Rules & Architecture)](#1-กฎเหล็กและสถาปัตยกรรมของ-framework-core-rules--architecture)
   - 1.1 [Rule 1: การแยกขาดระหว่าง dreamkurumiHT (Engine) และ Mods (Decoupling)](#11-rule-1-การแยกขาดระหว่าง-dreamkurumiht-engine-และ-mods-decoupling)
   - 1.2 [Rule 2: นโยบาย Universal Player Access vs Admin-Whitelist](#12-rule-2-นโยบาย-universal-player-access-vs-admin-whitelist)
   - 1.3 [โครงสร้างโฟลเดอร์มาตรฐานบน Client และ Dedicated Server](#13-โครงสร้างโฟลเดอร์มาตรฐานบน-client-และ-dedicated-server)
2. [ระบบเชื่อมต่อปลั๊กอิน C-ABI แบบไดนามิก (Dynamic Plugin Binding)](#2-ระบบเชื่อมต่อปลั๊กอิน-c-abi-แบบไดนามิก-dynamic-plugin-binding)
   - 2.1 [ทำไมต้องใช้ C-ABI Plugin Interface (`dreamkurumiHT_plugin.h`)](#21-ทำไมต้องใช้-c-abi-plugin-interface-dreamkurumiht_pluginh)
   - 2.2 [โครงสร้าง PluginHost และฟังก์ชัน Exports](#22-โครงสร้าง-pluginhost-และฟังก์ชัน-exports)
3. [ไฟล์ระบุข้อมูลม็อด (Mod Manifest: JSON & XML)](#3-ไฟล์ระบุข้อมูลม็อด-mod-manifest-json--xml)
4. [เริ่มต้นสร้างม็อดตัวแรกใน 15 บรรทัด (Quickstart Guide)](#4-เริ่มต้นสร้างม็อดตัวแรกใน-15-บรรทัด-quickstart-guide)
   - 4.1 [แบบที่ 1: Dynamic C-ABI Plugin (แนะนำสำหรับม็อดอิสระขนาดเล็ก ~50KB)](#41-แบบที่-1-dynamic-c-abi-plugin-แนะนำสำหรับม็อดอิสระขนาดเล็ก-50kb)
   - 4.2 [แบบที่ 2: Header-Only Direct Macro (`dreamkurumiHT.hpp`)](#42-แบบที่-2-header-only-direct-macro-dreamkurumihthpp)
5. [คู่มือระบบ Hooking แบบ Harmony ครบวงจร](#5-คู่มือระบบ-hooking-แบบ-harmony-ครบวงจร)
   - 5.1 [AOB Signature Scanning และระบบ Multi-Tier Cache](#51-aob-signature-scanning-และระบบ-multi-tier-cache)
   - 5.2 [Inline Hooking พร้อม Near-Memory Trampoline Pool (64KB Arena)](#52-inline-hooking-พร้อม-near-memory-trampoline-pool-64kb-arena)
   - 5.3 [Chained Mid-Hooking พร้อมระบบ Priority (แก้ปัญหาม็อดชนกัน)](#53-chained-mid-hooking-พร้อมระบบ-priority-แก้ปัญหาม็อดชนกัน)
   - 5.4 [การข้ามฟังก์ชันเดิม (Skip Original) และการแก้ Arguments/Return](#54-การข้ามฟังก์ชันเดิม-skip-original-และการแก้-argumentsreturn)
   - 5.5 [Hardware Breakpoint VEH Hooking (Zero-Byte Patching)](#55-hardware-breakpoint-veh-hooking-zero-byte-patching)
   - 5.6 [Mid-Function Register Hooking (อ่าน/เขียน CPU Context: RAX, RCX, XMM)](#56-mid-function-register-hooking-อ่านเขียน-cpu-context-rax-rcx-xmm)
   - 5.7 [VTable & RTTI Hooking (ดักจับ C++ Class Virtual Methods)](#57-vtable--rtti-hooking-ดักจับ-c-class-virtual-methods)
6. [การจัดการ Memory และ Pointer Chains ปลอดภัย 100%](#6-การจัดการ-memory-และ-pointer-chains-ปลอดภัย-100)
   - 6.1 [Sandbox Memory Safe Read/Write (ป้องกัน Access Violation)](#61-sandbox-memory-safe-readwrite-ป้องกัน-access-violation)
   - 6.2 [Multi-Level Pointer Chains (`ReadChain` / `WriteChain`)](#62-multi-level-pointer-chains-readchain--writechain)
   - 6.3 [RAII Byte Patching & NOPs (`DreamBytePatch`)](#63-raii-byte-patching--nops-dreambytepatch)
7. [ระบบ Asynchronous Double-Buffered Logging Engine (`DreamLog`)](#7-ระบบ-asynchronous-double-buffered-logging-engine-dreamlog)
   - 7.1 [สถาปัตยกรรม Double-Buffer Queue & Background Worker (<1µs Overhead)](#71-สถาปัตยกรรม-double-buffer-queue--background-worker-1µs-overhead)
   - 7.2 [ตัวกรอง Anti-Spam Deduplication (หน้าต่าง 2.0 วินาที)](#72-ตัวกรอง-anti-spam-deduplication-หน้าต่าง-20-วินาที)
   - 7.3 [Channel Verbosity และ Timed Burst Debugging](#73-channel-verbosity-และ-timed-burst-debugging)
   - 7.4 [In-Memory Ring Buffer และฟังก์ชัน Grep / Tail Logs](#74-in-memory-ring-buffer-และฟังก์ชัน-grep--tail-logs)
8. [ระบบ Dynamic Runtime Configuration (`DreamLiveConfig`)](#8-ระบบ-dynamic-runtime-configuration-dreamliveconfig)
   - 8.1 [การลงทะเบียน Config Properties (`RegisterFloat`, `RegisterBool`, ฯลฯ)](#81-การลงทะเบียน-config-properties-registerfloat-registerbool-ฯลฯ)
   - 8.2 [การ Hook Callback ปรับค่าแบบ Hot-Reload ในหน่วยความจำโดยไม่ต้องรีสตาร์ต](#82-การ-hook-callback-ปรับค่าแบบ-hot-reload-ในหน่วยความจำโดยไม่ต้องรีสตาร์ต)
9. [คอนโซลอินเตอร์แอคทีฟและระบบ Overlay (`DreamOverlay`)](#9-คอนโซลอินเตอร์แอคทีฟและระบบ-overlay-dreamoverlay)
   - 9.1 [รองรับทั้ง Windows GUI Overlay (`F10`) และ Piped Stdin (Headless Daemon)](#91-รองรับทั้ง-windows-gui-overlay-f10-และ-piped-stdin-headless-daemon)
   - 9.2 [ตารางสรุปคำสั่ง Console Command Reference & Short Aliases](#92-ตารางสรุปคำสั่ง-console-command-reference--short-aliases)
   - 9.3 [การสร้างไฟล์วิเคราะห์เซิร์ฟเวอร์แบบองค์รวม (`snapshot`)](#93-การสร้างไฟล์วิเคราะห์เซิร์ฟเวอร์แบบองค์รวม-snapshot)
10. [การเข้าถึงตัวเกมและ ECS Service Locator (`DreamGameContext`)](#10-การเข้าถึงตัวเกมและ-ecs-service-locator-dreamgamecontext)
    - 10.1 [การค้นหา Entity, Component และพิกัด 3 มิติ](#101-การค้นหา-entity-component-และพิกัด-3-มิติ)
    - 10.2 [การอ่านพิกัดตัวละครและการวาร์ป (Teleportation)](#102-การอ่านพิกัดตัวละครและการวาร์ป-teleportation)
    - 10.3 [ระบบเกมเพลย์ Presets (GodMode, InfiniteStamina, FreeCraft, ฯลฯ)](#103-ระบบเกมเพลย์-presets-godmode-infinitestamina-freecraft-ฯลฯ)
11. [การแยกโค้ด Dedicated Server และ Client](#11-การแยกโค้ด-dedicated-server-และ-client)
    - 11.1 [Macro แยกแพลตฟอร์ม (`DREAM_SERVER_ONLY` / `DREAM_CLIENT_ONLY`)](#111-macro-แยกแพลตฟอร์ม-dream_server_only--dream_client_only)
    - 11.2 [เครือข่าย Network RPC Bridge (`DreamNetBridge`)](#112-เครือข่าย-network-rpc-bridge-dreamnetbridge)
    - 11.3 [การกระจายค่าคอนฟิกจาก Server สู่ผู้เล่น (`DreamServerSync`)](#113-การกระจายค่าคอนฟิกจาก-server-สู่ผู้เล่น-dreamserversync)
12. [คู่มือสำหรับนักพัฒนา C# .NET 8/10 (Native AOT)](#12-คู่มือสำหรับนักพัฒนา-c-net-810-native-aot)
13. [เอนจิน JSON ความเร็วสูง (DreamJson Powered by yyjson)](#13-เอนจิน-json-ความเร็วสูง-dreamjson-powered-by-yyjson)
14. [เอนจินสคริปต์ในหน่วยความจำ (DreamLua Powered by LuaJIT 2.1)](#14-เอนจินสคริปต์ในหน่วยความจำ-dreamlua-powered-by-luajit-21)
15. [เครื่องมือพัฒนาและ Build Engine (`dev_sync.ps1`)](#15-เครื่องมือพัฒนาและ-build-engine-dev_syncps1)
    - 15.1 [การ Build & Sync ทั้งระบบอัตโนมัติ](#151-การ-build--sync-ทั้งระบบอัตโนมัติ)
    - 15.2 [เทคนิค Safe Hot-Replacement สลับ DLL ขณะเกมกำลังรัน (`.locked_*`)](#152-เทคนิค-safe-hot-replacement-สลับ-dll-ขณะเกมกำลังรัน-locked_)
16. [แนวทางปฏิบัติที่ดีที่สุดและการป้องกันข้อผิดพลาด (Best Practices)](#16-แนวทางปฏิบัติที่ดีที่สุดและการป้องกันข้อผิดพลาด-best-practices)

---

## 1. กฎเหล็กและสถาปัตยกรรมของ Framework (Core Rules & Architecture)

### 1.1 Rule 1: การแยกขาดระหว่าง dreamkurumiHT (Engine) และ Mods (Decoupling)
* **`dreamkurumiHT` (`dreamkurumiHT.dll`) คือ Harmony Hooking Framework / Core Native Engine เท่านั้น:**
  * หน้าที่หลักมีเพียง: จัดเตรียม Hooking Primitives (`InlineHook`, `MidHook`, `PatchBytes`), AOB Multi-Tier Signature Scanner, Near-Memory 64KB Trampoline Arena, Deadlock/Hang Watchdog, Async Double-Buffered Logger, และ Dynamic Plugin Binding
  * **ข้อห้ามเด็ดขาด:** ห้ามนำ Logic ของม็อดเกม (เช่น เก็บผัก AoE, ดูดของ Auto Pickup, แชร์ EXP, ระบบตกปลา) เข้าไปรวมไว้ในโค้ดของ `dreamkurumiHT.dll` โดยตรง!
  * **หลักการ:** **Mod คือ Mod, Dream คือ Harmony Engine** ทั้งคู่ต้องแยกขาดจากกัน (Decoupled) เสมอ
* **ม็อดทุกตัวต้องเป็นโมดูลอิสระ (Modular) และแยกโฟลเดอร์ของตนเองอย่างชัดเจน:**
  * `mods\dreamkurumiHT\` (`dreamkurumiHT.dll`): ตัวเอนจินหลักตัวเดียว
  * `mods\Dream-AoE-Harvest\` (`DreamAoEHarvest.dll` + `config.ini`): ม็อดเก็บเกี่ยวพืชไร่ฟาร์ม (รัศมี 3.5m, ปุ่ม F8)
  * `mods\Dream-Auto-Pickup\` (`DreamAutoPickup.dll` + `config.ini`): ม็อดเก็บไอเทมตกพื้น ดรอปมอนสเตอร์ กิ่งไม้ ท่อนซุง หิน แร่ (รัศมี 5.0m, ปุ่ม F7)
  * `mods\Admin-Control-Server\` (`AdminControlServer.dll` + `admin_fishing_config.ini`): ระบบตกปลาและคำสั่งแอดมิน
  * `mods\XP-Share\` (`XPShare.dll` + `config.ini`): ระบบแชร์ EXP ในปาร์ตี้
* ม็อดทั้งหมดเชื่อมต่อเข้ากับ `dreamkurumiHT.dll` ผ่าน **C-ABI Header ขนาดเบา (`dreamkurumiHT_plugin.h`)** แบบ Dynamic Link

---

### 1.2 Rule 2: นโยบาย Universal Player Access vs Admin-Whitelist
* **ผู้เล่นทุกคนต้องใช้งานม็อด Gameplay และ QoL ได้ตามค่าเริ่มต้น (Universal Access by Default):**
  * ม็อดแนวอำนวยความสะดวกในการเล่น (`Dream-AoE-Harvest`, `Dream-Auto-Pickup`, `XP-Share` ฯลฯ) **ต้องเปิดให้ผู้เล่นทุกคนที่เชื่อมต่อเข้ามายัง Dedicated Server ใช้งานได้ทันทีโดยไม่ต้องขอสิทธิ์แอดมิน**
  * ตัวแปรตั้งค่า `AdminOnly` ในคอนฟิกหรือโค้ด **ต้องมีค่า Default เป็น `false` (หรือ 0) เสมอ**
  * ผู้เล่นทั่วไปต้องสามารถเก็บผักเป็นวงกว้าง (AoE Harvest), ดูดไอเทมที่พื้น (Auto-Pickup), และรับแชร์ EXP กับเพื่อนในปาร์ตี้ได้โดยไม่มีอุปสรรค
* **ข้อยกเว้นสำหรับระบบแอดมิน (Admin-Whitelist Exception):**
  * มีเพียงฟังก์ชันที่เป็นคำสั่งโกงหรือเครื่องมือแอดมินระดับสูง เช่น GodMode, GhostMode, FreeCraft, Infinite Shroud, มานาไม่จำกัด, หรือ Instant Catch ปลาของแอดมินใน `AdminControlServer` เท่านั้น ที่ต้องตรวจเช็ค Whitelist แอดมิน (`adminGroupName` / `adminPlayerName` / SteamID)

---

### 1.3 โครงสร้างโฟลเดอร์มาตรฐานบน Client และ Dedicated Server

```text
Enshrouded/ (หรือ EnshroudedServer/)
├── enshrouded.exe (หรือ enshrouded_server.exe)
├── dbghelp.dll                  <-- Mod Loader Proxy (Hook ตอนโหลดเกม)
├── mod_loader.log               <-- Log การค้นหาและโหลดโมดูล DLL
├── mods/
│   ├── dreamkurumiHT/
│   │   ├── dreamkurumiHT.dll    <-- Core Native Harmony Engine
│   │   ├── lua51.dll            <-- LuaJIT 2.1 x64 Runtime
│   │   ├── dreamkurumi.ini      <-- การตั้งค่า Watchdog, Thread, Log
│   │   └── dreamkurumi_cache.ini<-- แคช AOB Signatures (เปิดเกมเร็ว 0.0001ms)
│   ├── Dream-AoE-Harvest/
│   │   ├── DreamAoEHarvest.dll  <-- Plugin DLL (~50-180 KB)
│   │   ├── config.ini           <-- Radius=3.5, SameCrop=1, AdminOnly=0
│   │   └── modinfo.json
│   ├── Dream-Auto-Pickup/
│   │   ├── DreamAutoPickup.dll
│   │   ├── config.ini           <-- Radius=5.0, FastProximity=1, AdminOnly=0
│   │   └── modinfo.json
│   ├── XP-Share/
│   │   ├── XPShare.dll
│   │   ├── config.ini           <-- Enabled=1, Multiplier=0.35
│   │   └── modinfo.json
│   └── Admin-Control-Server/
│       ├── AdminControlServer.dll
│       └── admin_fishing_config.ini
```

---

## 2. ระบบเชื่อมต่อปลั๊กอิน C-ABI แบบไดนามิก (Dynamic Plugin Binding)

### 2.1 ทำไมต้องใช้ C-ABI Plugin Interface (`dreamkurumiHT_plugin.h`)
การสร้างม็อดโดยนำ Header ทั้งหมดของ Framework ไปคอมไพล์ในทุกม็อดจะทำให้ไฟล์ DLL บวม (2-4 MB ต่อม็อด) และเกิดปัญหา Hook ชนกันเองระหว่างม็อด
เราจึงออกแบบ `dreamkurumiHT_plugin.h` ซึ่งเป็น **Pure C-ABI Dynamic Interface**:
* ม็อดของคุณจะมีขนาดเล็กเพียง **30 KB - 180 KB**
* เชื่อมต่อไปยัง `dreamkurumiHT.dll` ในหน่วยความจำโดยอัตโนมัติผ่าน `GetProcAddress`
* ไม่ต้องแนบไฟล์ `.lib` หรือคอมไพล์ซ้ำซ้อน
* รองรับทั้งการเขียนด้วย Modern C++20 และ C# (.NET Native AOT)

### 2.2 โครงสร้าง PluginHost และฟังก์ชัน Exports
การเรียกใช้งานทำได้ง่ายผ่าน Singleton `dreamkurumi::PluginHost`:

```cpp
#include <dreamkurumiHT/dreamkurumiHT_plugin.h>

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        
        // 1. ผูกความสัมพันธ์กับ Core Engine อัตโนมัติ
        auto& host = dreamkurumi::PluginHost::Instance();
        if (!host.Init()) {
            OutputDebugStringA("[MyMod] Failed to bind dreamkurumiHT.dll!\n");
            return TRUE;
        }

        // 2. ใช้งาน Logging, Hooks, Memory Scan ได้ทันที
        host.Log(1, "[MyMod] Successfully linked to dreamkurumiHT Core Engine!");
    }
    return TRUE;
}
```

#### รายการ Core C-ABI Exports ที่พร้อมใช้งาน:
| Function Pointer | ความสามารถ |
|---|---|
| `DK_ResolvePattern(name, sig, rva)` | ค้นหา Pattern AOB ความเร็วสูงพร้อมระบบแคช |
| `DK_InlineHook(target, detour, ppOrig)` | ติดตั้ง Detour Hook ด้วย Near-Memory Trampoline |
| `DK_RegisterChainedMidHook(target, cb, prio, name)` | ติดตั้ง Mid-Hook แบบลูกโซ่เรียงลำดับ Priority |
| `DK_PatchBytes(target, bytes, count)` | เขียนทับไบต์พร้อม VirtualProtect และจำค่าเดิมไว้คืนค่า |
| `DK_Nop(target, count)` | NOP คำสั่ง Assembly ตามจำนวนไบต์ |
| `DK_Log(level, msg)` | ส่งข้อความเข้า Asynchronous Double-Buffered Queue |
| `DK_Preset_SetGodMode(bool)` | สั่งเปิด/ปิด โหมดอมตะ |
| `DK_Preset_SetInfiniteStamina(bool)` | สั่งเปิด/ปิด วิ่งไม่เหนื่อย |
| `DK_Preset_SetUnlimitedFlight(bool)` | สั่งเปิด/ปิด ร่อนเครื่องร่อนไม่จำกัด |
| `DK_GetEntityPosition(pEnt, &x, &y, &z)` | อ่านพิกัด 3 มิติของ Entity ในระบบ ECS |

---

## 3. ไฟล์ระบุข้อมูลม็อด (Mod Manifest: JSON & XML)

Framework มีระบบ **Universal Auto-Discovery** ซึ่งจะอ่านข้อมูลของม็อดขึ้นมาแสดงในคอนโซลและบันทึกลง Log โดยไม่ต้อง Hardcode ในภาษา C++:

### แบบที่ 1: `modinfo.json` (แนะนำที่สุด ⭐)
วางไฟล์นี้ไว้ในโฟลเดอร์เดียวกับ DLL ของม็อด:
```json
{
  "name": "DreamAoEHarvest",
  "displayName": "Dream AoE Crop Harvest",
  "version": "3.6.0-beta",
  "author": "dreamkurumi",
  "description": "AoE farming crop harvesting with 3.5m radius and F8 hotkey toggle.",
  "website": "https://github.com/your-repo/enshrouded-mods"
}
```

### แบบที่ 2: `ModInfo.xml` (รองรับรูปแบบ XML สไตล์ 7 Days to Die)
```xml
<?xml version="1.0" encoding="utf-8"?>
<xml>
    <Name value="DreamAutoPickup" />
    <DisplayName value="Dream Auto Pickup Ground Loot" />
    <Version value="1.0.0" />
    <Author value="dreamkurumi" />
    <Description value="Ground loot, monster drops, logs, and ore auto pickup 5.0m." />
</xml>
```

---

## 4. เริ่มต้นสร้างม็อดตัวแรกใน 15 บรรทัด (Quickstart Guide)

### 4.1 แบบที่ 1: Dynamic C-ABI Plugin (แนะนำสำหรับม็อดอิสระขนาดเล็ก ~50KB)
วิธีนี้สอดคล้องกับ Rule 1 100% ตัว DLL มีขนาดเล็กและโหลดได้ทั้งบน Client และ Dedicated Server:

```cpp
#include <windows.h>
#include <dreamkurumiHT/dreamkurumiHT_plugin.h>

// ตัวแปรเก็บฟังก์ชันดั้งเดิมของเกม
typedef void (*FnTakeDamage)(void* character, float damage, uint32_t damageType);
FnTakeDamage g_pOriginalTakeDamage = nullptr;

// Detour ฟังก์ชัน: หากเปิดใช้งาน ให้ลดดาเมจลงครึ่งหนึ่ง
void DetourTakeDamage(void* character, float damage, uint32_t damageType) {
    damage *= 0.5f; // รับดาเมจเบาลง 50%
    if (g_pOriginalTakeDamage) {
        g_pOriginalTakeDamage(character, damage, damageType);
    }
}

extern "C" __declspec(dllexport) int XhlNativePluginStart(void* ctx) {
    auto& host = dreamkurumi::PluginHost::Instance();
    if (!host.Init()) return 0;

    // ค้นหาตำแหน่งฟังก์ชันผ่าน AOB Signature
    void* pTarget = host.ResolvePattern("TakeDamage", "40 53 48 83 EC 20 48 8B D9 0F 29", 0);
    if (pTarget) {
        host.InlineHook(pTarget, (void*)DetourTakeDamage, (void**)&g_pOriginalTakeDamage);
        host.Log(1, "[DamageMod] TakeDamage hook installed cleanly!");
    }
    return 1;
}

extern "C" __declspec(dllexport) void XhlNativePluginRequestStop() {
    // Framework จะถอน Hook และเคลียร์ทรัพยากรให้อัตโนมัติ
}
```

---

### 4.2 แบบที่ 2: Header-Only Direct Macro (`dreamkurumiHT.hpp`)
สำหรับกรณีต้องการใช้ Syntax สไตล์ Harmony เต็มรูปแบบ:

```cpp
#include <windows.h>
#include <dreamkurumiHT/dreamkurumiHT.hpp>

// 1. ประกาศ Hook ด้วย Signature AOB
DREAM_HARMONY_AOB_AUTO(StaminaPatch, "StaminaDrain", "48 89 5C 24 ?? 57 48 83 EC", void, void* player, float amount) {
    static bool Prefix(void* player, float amount) {
        // คืนค่า false เพื่อข้ามฟังก์ชันลด Stamina (Stamina ไม่ลด!)
        return false;
    }
};
DREAM_REGISTER_PATCH(StaminaPatch);

// 2. Entry Point
DREAM_MOD_ENTRY(
    /* OnStart */ {
        DREAM_LOG_INFO("Infinite Stamina Mod loaded successfully!");
    },
    /* OnStop */ {
        DREAM_LOG_INFO("Infinite Stamina Mod unloaded.");
    }
)
```

---

## 5. คู่มือระบบ Hooking แบบ Harmony ครบวงจร

### 5.1 AOB Signature Scanning และระบบ Multi-Tier Cache
การระบุตำแหน่งด้วย Memory Address หรือ Offset ตรงๆ จะใช้งานไม่ได้เมื่อเกมมีการอัปเดตเวอร์ชัน
`dreamkurumiHT` ใช้ตัวสแกน Multi-Tier AOB ที่รองรับ Wildcard Byte `??`:

```cpp
// สแกนหาตำแหน่งฟังก์ชัน พร้อมระบุ Fallback RVA ในกรณีฉุกเฉิน
void* addr = host.ResolvePattern(
    "CropInteractCheck", 
    "48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 30 48 8B FA 48 8B D9", 
    0xCCD430
);
```
* **Tier 1 (Memory Cache):** ครั้งแรกที่สแกนเจอ แอดเดรสจะถูกเก็บลงไฟล์ `mods/dreamkurumiHT/dreamkurumi_cache.ini`
* **Tier 2 (Fast Reload):** เมื่อเปิดเกมรอบต่อไป ระบบจะอ่านค่าจาก Cache และตรวจสอบไบต์ยืนยันทันที โดยใช้เวลาเพียง **0.0001 ms**
* **Tier 3 (Auto Invalidation):** หากเกมมีการอัปเดตแพตช์ (Build Timestamp ของ `enshrouded.exe` เปลี่ยนไป) ระบบจะสแกนใหม่และอัปเดตแคชให้อัตโนมัติ

---

### 5.2 Inline Hooking พร้อม Near-Memory Trampoline Pool (64KB Arena)
บนสถาปัตยกรรม x64 คำสั่งกระโดดแบบ Direct Jump `E9 [disp32]` สามารถกระโดดได้ไกลสูงสุด $\pm 2\text{ GB}$ จากตำแหน่งเดิม
หากใช้ `VirtualAlloc` ทั่วไป แรมที่ได้อาจอยู่ห่างเกิน $2\text{ GB}$ ทำให้ต้องใช้ 14-byte Indirect Jump (`FF 25 [00000000] [64-bit target]`) ซึ่งจะกินเนื้อที่คำสั่งของเกมมากเกินไปและเสี่ยงแครช

**การแก้ปัญหาของ `dreamkurumiHT` (`DreamPool.hpp`):**
* ระบบจะทำการสแกนหาช่องว่างหน่วยความจำที่ยังว่างอยู่ในระยะ $\pm 2\text{ GB}$ จาก Module ของเกม
* จัดสรรเป็น **64KB Near-Memory Arena Chunk**
* รับประกันว่าสามารถสร้าง Trampoline ขนาดกะทัดรัดด้วย Direct 5-byte Jump ได้ 100%

```cpp
void* pOriginal = nullptr;
void* hookHandle = host.InlineHook(targetFunc, (void*)MyDetour, &pOriginal);
```

---

### 5.3 Chained Mid-Hooking พร้อมระบบ Priority (แก้ปัญหาม็อดชนกัน)
ในเซิร์ฟเวอร์ Enshrouded ปัญหาคลาสสิกคือ: **ม็อด AoE Harvest และม็อด Auto Pickup ต้องการ Hook จุดรับไอเทมเดียวกัน**
หากใช้ Inline Hook ธรรมดา ม็อดตัวที่สองจะเขียนทับม็อดตัวแรกจนพัง

`dreamkurumiHT` แก้ไขปัญหานี้ด้วย **Chained Mid-Hook Architecture**:
* ม็อดแต่ละตัวสามารถลงทะเบียน Callback เข้าจุดเดียวกันได้หลายตัว
* มีระบบกำหนดลำดับความสำคัญ (`priority`: ค่ายิ่งสูง ยิ่งได้รันก่อน)

```cpp
// ม็อด AoE Harvest ลงทะเบียน Priority 100
host.RegisterChainedMidHook(targetLootInstruction, [](void* ctx) {
    auto cpu = (dreamkurumi::DreamCpuContext*)ctx;
    // ตรวจสอบและประมวลผลพืชไร่...
}, 100, "DreamAoEHarvest");

// ม็อด Auto Pickup ลงทะเบียนจุดเดียวกันที่ Priority 50
host.RegisterChainedMidHook(targetLootInstruction, [](void* ctx) {
    auto cpu = (dreamkurumi::DreamCpuContext*)ctx;
    // ตรวจสอบและประมวลผลไอเทมตกพื้น...
}, 50, "DreamAutoPickup");
```
ทั้งสองม็อดจะทำงานร่วมกันได้อย่างราบรื่น 0% Conflict!

---

### 5.4 การข้ามฟังก์ชันเดิม (Skip Original) และการแก้ Arguments/Return

* **ข้ามฟังก์ชันดั้งเดิม:** สั่ง `return false;` ใน `Prefix`
* **ปล่อยฟังก์ชันดั้งเดิมทำงานต่อ:** สั่ง `return true;` ใน `Prefix`

```cpp
DREAM_HARMONY_AOB_AUTO(FallDamagePatch, "FallDamage", "...", void, void* player, float height) {
    static bool Prefix(void* player, float height) {
        if (height > 50.0f) {
            DREAM_LOG_INFO("Prevented lethal fall damage!");
            return false; // ไม่คิดแดเมจตกจากที่สูง
        }
        return true;
    }
    
    static void Postfix(void* player, float height) {
        // ทำงานหลังจากโค้ดเกมคิดแดเมจเสร็จ
    }
};
```

---

### 5.5 Hardware Breakpoint VEH Hooking (Zero-Byte Patching)
สำหรับกรณีที่เกมมีระบบตรวจเช็ค Integrity ของ Code Section หรือไม่ต้องการแก้ไขไบต์ใน Memory:
```cpp
auto& veh = dreamkurumi::DreamVeh::Instance();
veh.SetHardwareBreakpoint(targetAddress, 0, dreamkurumi::BreakpointCondition::Execute, [](PCONTEXT ctx) {
    DREAM_LOG_INFO("Hardware Breakpoint Triggered! RAX = 0x%llX", ctx->Rax);
    // ctx->Rax = 100; // แก้ไขค่า Register ของ CPU ได้โดยตรง
});
```

---

### 5.6 Mid-Function Register Hooking (อ่าน/เขียน CPU Context: RAX, RCX, XMM)
ดักจับจังหวะกึ่งกลางฟังก์ชัน Assembly เพื่อดึง Pointer ตัวละคร หรือแก้ค่า Floating-point ใน Register XMM:

```cpp
dreamkurumi::DreamMidHook midHook;
midHook.Install(targetInstructionAddress, [](dreamkurumi::DreamCpuContext* ctx) {
    // อ่านค่า CPU Registers ทั่วไป
    uint64_t characterPtr = ctx->rcx;
    
    // อ่านค่า Floating Point Register (XMM0)
    float currentHealth = ctx->GetXmmFloat(0);
    
    // บังคับเปลี่ยนค่า Register ก่อนที่เกมจะประมวลผลต่อ
    ctx->SetXmmFloat(0, currentHealth + 50.0f);
});
```

---

### 5.7 VTable & RTTI Hooking (ดักจับ C++ Class Virtual Methods)
Enshrouded เขียนด้วยภาษา C++ และมีการใช้ Virtual Table (VTable) จำนวนมาก:
```cpp
// 1. ค้นหา VTable ของคลาสด้วยชื่อ RTTI (Run-Time Type Information)
void** vtable = dreamkurumi::DreamRtti::FindVTable("keen::Character");

// 2. Hook Virtual Method ตาม Index (เช่น Method ลำดับที่ 4)
dreamkurumi::DreamVTableHook vHook;
vHook.Hook(vtable, 4, reinterpret_cast<void*>(MyCustomTakeDamage));
```

---

## 6. การจัดการ Memory และ Pointer Chains ปลอดภัย 100%

### 6.1 Sandbox Memory Safe Read/Write (ป้องกัน Access Violation)
ในภาษา C++ ทั่วไป การอ่าน Pointer ที่เป็น Bad Address / Null Address จะทำให้ตัวเกมเกิด Access Violation (`0xC0000005`) และ Crash ทันที
`DreamMemory` มีระบบ `DreamCrashGuard` (VEH/SEH Sandbox) คุ้มกันทุกคำสั่ง:

```cpp
using namespace dreamkurumi;

// หาก pHealth เป็น Null Pointer หรือ Memory หลุดขอบเขต ฟังก์ชันจะคืนค่า 0.0f แทน โดยที่เกมไม่เด้งดับ!
float hp = DreamMemory::Read<float>(pHealth, 0.0f);

// เขียนค่าลงหน่วยความจำอย่างปลอดภัย (สลับ VirtualProtect ให้อัตโนมัติ)
bool success = DreamMemory::Write<float>(pHealth, 500.0f);
```

---

### 6.2 Multi-Level Pointer Chains (`ReadChain` / `WriteChain`)
แกะ Pointer ซ้อนกันหลายชั้นเหมือนการทำงานของ Cheat Engine เช่น `[[[pBase + 0x18] + 0x30] + 0x48]`:

```cpp
using namespace dreamkurumi;

// อ่านค่า float จาก Pointer Chain 3 ชั้น
float stamina = DreamMemory::ReadChain<float>(pPlayer, { 0x18, 0x30, 0x48 }, 100.0f);

// เขียนค่าลงปลายทางของ Pointer Chain
DreamMemory::WriteChain<float>(pPlayer, { 0x18, 0x30, 0x48 }, 999.0f);
```

---

### 6.3 RAII Byte Patching & NOPs (`DreamBytePatch`)
เขียนทับคำสั่ง Assembly ด้วย NOP หรือ Custom Bytes โดยให้คืนค่าเดิมอัตโนมัติเมื่อหลุด Scope (RAII):

```cpp
using namespace dreamkurumi;

{
    // NOP 5 ไบต์ ณ ตำแหน่ง codeAddress
    DreamBytePatch patch(codeAddress, "90 90 90 90 90");
    patch.Apply();
    
    // โค้ดของเกมจะถูกข้ามชั่วคราว...
} 
// เมื่อหลุดออกจากบล็อกปีกกา ไบต์ดั้งเดิมของเกมจะถูกกู้คืนกลับมาอัตโนมัติ 100%!
```

---

## 7. ระบบ Asynchronous Double-Buffered Logging Engine (`DreamLog`)

### 7.1 สถาปัตยกรรม Double-Buffer Queue & Background Worker (<1µs Overhead)
ในการเล่นเกมที่ 144+ FPS หรือบน Dedicated Server ที่มี Entity นับหมื่นตัว การเขียน Log ลงดิสก์ (`std::ofstream`) บน Game Thread โดยตรงจะทำให้เกมเกิดอาการ **Micro-stutter / Frame Drop** ทันที

`dreamkurumiHT v3.6.0-beta` ปรับใช้สถาปัตยกรรม **Double-Buffered Lock-Free Queue**:
```text
[Game Thread 1] ---> \
[Game Thread 2] -----> [ Incoming Lock-Free Queue ]  <-- Overhead < 1 ไมโครวินาที (0 Disk I/O)
[Server Thread] ---> /                |
                               (Atomic Swap ทุก 10ms)
                                      v
                             [ Processing Queue ]
                                      |
                           [ Background Worker Thread ]
                                      |
                     +----------------+----------------+
                     v                                 v
             [ In-Memory Ring Buffer ]        [ Disk Log File (.log) ]
```

### 7.2 ตัวกรอง Anti-Spam Deduplication (หน้าต่าง 2.0 วินาที)
หากมี Entity 50 ตัวกระตุ้นข้อความเดิมซ้ำๆ ระบบจะไม่เขียน Log ซ้ำ 50 บรรทัด แต่จะทำการรวมข้อความ:
```text
[14:28:05.294][global:INFO] [DreamAoEHarvest] Scanning crop entities...
[14:28:07.310][global:INFO] [DreamAoEHarvest] Scanning crop entities... [Repeated 49 times]
```

---

### 7.3 Channel Verbosity และ Timed Burst Debugging
แยกช่องทาง Log เป็น Channels อิสระ:
* `LogChannel::Global`
* `LogChannel::AoE`
* `LogChannel::Loot`
* `LogChannel::Admin`
* `LogChannel::XP`
* `LogChannel::Hook`
* `LogChannel::Net`
* `LogChannel::ECS`

#### ปรับระดับ Verbosity:
* `0 = Off` (ปิดข้อความ)
* `1 = Info` (ข้อมูลสำคัญ)
* `2 = Warning` (แจ้งเตือน)
* `3 = Trace/Debug` (ข้อมูลละเอียดระดับ Loop)

#### Timed Burst Mode (ฟีเจอร์เด็ดสำหรับตรวจจับ Bug):
สั่งเปิด Log ระดับ Trace เฉพาะ Channel นั้นๆ เป็นการชั่วคราว (เช่น 30 วินาที) เมื่อครบเวลาระบบจะลดกลับเป็นระดับปกติอัตโนมัติ:
```cpp
// เปิด Trace Channel AoE เป็นเวลา 30 วินาที
DreamLog::Instance().SetChannelVerbosity(LogChannel::AoE, 3, 30);
```

---

### 7.4 In-Memory Ring Buffer และฟังก์ชัน Grep / Tail Logs
Log ล่าสุด 2,048 รายการจะถูกเก็บไว้ในหน่วยความจำ RAM เสมอ ทำให้นักพัฒนาสามารถค้นหาได้ทันที:
* **Tail Logs:** ดึง N บรรทัดล่าสุดมาแสดงบน Console
* **Grep Logs:** ค้นหาคำค้นหา (Substring / Regex) ใน Memory โดยไม่ต้องเปิดไฟล์ text ภายนอก

---

## 8. ระบบ Dynamic Runtime Configuration (`DreamLiveConfig`)

### 8.1 การลงทะเบียน Config Properties
`DreamLiveConfig` ช่วยให้ม็อดสามารถสร้างตัวแปรที่สามารถอ่านและปรับแต่งค่าได้แบบสดๆ (Live Tweaking) ในหน่วยความจำ:

```cpp
#include <dreamkurumiHT/DreamLiveConfig.hpp>

auto& cfg = dreamkurumi::DreamLiveConfig::Instance();

// ลงทะเบียนตัวแปร float พร้อมฟังก์ชัน Callback เมื่อค่าเปลี่ยน
cfg.RegisterFloat("aoe.radius", 3.5f, "AoE Crop Harvest Radius in meters", [](float newRadius) {
    g_harvestRadius = newRadius;
    DREAM_LOG_INFO("[DreamAoEHarvest] Live updated radius to %.2fm", newRadius);
});

// ลงทะเบียนตัวแปร bool
cfg.RegisterBool("aoe.samecrop", true, "Harvest matching species only", [](bool val) {
    g_sameCropOnly = val;
});
```

### 8.2 การ Hook Callback ปรับค่าแบบ Hot-Reload ในหน่วยความจำโดยไม่ต้องรีสตาร์ต
เมื่อพิมพ์คำสั่ง `set aoe.radius 4.5` ใน Console ระบบจะเรียก Callback ทันที ทำให้ผู้เล่นและเซิร์ฟเวอร์ปรับขนาดการเก็บผักหรือระยะดูดของได้ในเสี้ยววินาทีโดยไม่ต้องปิดเกมหรือเซิร์ฟเวอร์!

---

## 9. คอนโซลอินเตอร์แอคทีฟและระบบ Overlay (`DreamOverlay`)

### 9.1 รองรับทั้ง Windows GUI Overlay (`F10`) และ Piped Stdin (Headless Daemon)
* **บน Game Client:** กดปุ่ม **`F10`** เพื่อเปิดหน้าต่าง Windows Console สีสันสวยงาม หรือกด **`F9`** เพื่อเปิด In-Game ImGui Menu
* **บน Dedicated Server (Headless):** ระบบมี Background Stdin Listener รองรับการพิมพ์คำสั่งผ่าน Command Prompt, PowerShell, SSH, หรือ Task Automation (`send_input`) ได้โดยตรง 100%

---

### 9.2 ตารางสรุปคำสั่ง Console Command Reference & Short Aliases

| Command | Short Alias | รูปแบบการใช้งาน | คำอธิบาย |
|---|---|---|---|
| `help` | `h` | `help` หรือ `h` | แสดงรายการคำสั่งทั้งหมดพร้อมคำอธิบาย |
| `status` | `s` | `status` หรือ `s` | แสดงสถานะการทำงานของ Framework และเซิร์ฟเวอร์ |
| `perf` | `p` | `perf` หรือ `p` | ตรวจสอบ RAM Working Set, Peak, Free RAM, และสถิติ VEH |
| `clear` | `c` | `clear` หรือ `c` | ล้างหน้าจอคอนโซล |
| `get` | - | `get <key>` หรือ `get all` | อ่านค่าตัวแปรตั้งค่าสดในหน่วยความจำ |
| `set` | - | `set <key> <val>` | ปรับแต่งค่าตัวแปรแบบสดๆ เช่น `set aoe.radius 4.5` |
| `verbose` | `v` | `verbose <ch> <0-3> [sec]` | ปรับระดับ Log และเปิด Timed Burst เช่น `v aoe 3 30` |
| `tail` / `log` | `t` | `tail [N]` หรือ `t 10` | ดึง Log N บรรทัดล่าสุดขึ้นมาแสดงในคอนโซลทันที |
| `grep` | - | `grep <pattern> [N]` | ค้นหาข้อความ Log ในหน่วยความจำ (In-Memory Ring Buffer) |
| `players` | - | `players` | แสดงรายชื่อผู้เล่นและ Entity IDs ที่เชื่อมต่ออยู่ในเซิร์ฟเวอร์ |
| `snapshot` | - | `snapshot` | บันทึกรายงานสถานะระบบและ Log สดเป็นไฟล์ `snapshot_*.txt` |
| `dump` | - | `dump <hex_addr> [len]` | สแกนดูไบต์ใน Memory อย่างปลอดภัย (เช่น `dump 0x7FF... 64`) |
| `sig` | - | `sig <pattern>` | ค้นหา Pattern AOB ในหน่วยความจำแบบ Real-time |
| `ecs` | - | `ecs <id>` | ตรวจสอบข้อมูล Entity และ Components ของระบบ ECS |
| `lua` | `l` | `lua <expression>` | ประมวลผลคำสั่ง LuaJIT ในหน่วยความจำทันที |
| `luafile` | - | `luafile <path>` | รันไฟล์สคริปต์ `.lua` จากดิสก์ |

---

### 9.3 การสร้างไฟล์วิเคราะห์เซิร์ฟเวอร์แบบองค์รวม (`snapshot`)
เมื่อเกิดปัญหาหรือต้องการ Audit ประสิทธิภาพ เพียงพิมพ์คำสั่ง:
```text
snapshot
```
ระบบจะรวบรวมสถานะ Working Set, การใช้ RAM ของระบบ, ตัวแปร Live Configuration ทั้งหมด, และ Log ล่าสุด เซฟออกมาเป็นไฟล์ `snapshot_YYYYMMDD_HHMMSS.txt` ทันที

---

## 10. การเข้าถึงตัวเกมและ ECS Service Locator (`DreamGameContext`)

### 10.1 การค้นหา Entity, Component และพิกัด 3 มิติ
Enshrouded ใช้สถาปัตยกรรม Entity Component System (ECS):

```cpp
auto& host = dreamkurumi::PluginHost::Instance();

// ดึง Entity ตาม Entity ID
void* pEntity = host.GetEntity(pGameContext, entityId);
if (pEntity) {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    if (host.GetEntityPosition(pEntity, &x, &y, &z)) {
        DREAM_LOG_INFO("Entity %u Position: X=%.2f Y=%.2f Z=%.2f", entityId, x, y, z);
    }
}
```

---

### 10.2 การอ่านพิกัดตัวละครและการวาร์ป (Teleportation)
```cpp
auto& game = dreamkurumi::DreamGame::Instance();

if (game.IsPlayerReady()) {
    DreamVector3 pos = game.GetPlayerPosition();
    DREAM_LOG_INFO("Current Pos: X=%.2f Y=%.2f Z=%.2f", pos.x, pos.y, pos.z);
    
    // สั่งวาร์ปตัวละครไปยังพิกัดเป้าหมาย
    game.TeleportPlayer(150.0f, 85.0f, -320.0f);
}
```

---

### 10.3 ระบบเกมเพลย์ Presets (GodMode, InfiniteStamina, FreeCraft, ฯลฯ)
Framework มีฟังก์ชันควบคุม Preset พื้นฐานที่ผ่านการ Reverse Engineering ไว้อย่างสมบูรณ์:
```cpp
game.SetGodMode(true);             // อมตะ ไม่โดนดาเมจ
game.SetGhostMode(true);           // มอนสเตอร์มองไม่เห็นตัว (Invisibility)
game.SetInfiniteStamina(true);     // สตามินาไม่ลด
game.SetInfiniteMana(true);        // มานาไม่ลด
game.SetInfiniteShroud(true);      // เวลาในม่านหมอกสีฟ้าไม่ลด
game.SetFreeCraft(true);           // คราฟต์ของได้ฟรีโดยไม่ต้องใช้วัตถุดิบ
game.SetUnlimitedFlight(true);     // ร่อนเครื่องร่อนได้ไม่จำกัดระยะ
game.SetSuperMining(true, 5.0f);   // ขุดแร่ ขุดหิน ไวขึ้น 5 เท่า
```

---

## 11. การแยกโค้ด Dedicated Server และ Client

### 11.1 Macro แยกแพลตฟอร์ม (`DREAM_SERVER_ONLY` / `DREAM_CLIENT_ONLY`)
ช่วยให้คอมไพล์ซอร์สโค้ดชุดเดียวกันเป็น DLL สำหรับรันบนเซิร์ฟเวอร์หรือบนไคลเอนต์ได้อย่างชาญฉลาด:

```cpp
DREAM_SERVER_ONLY({
    // ส่วนนี้จะทำงานเฉพาะบน Dedicated Server (enshrouded_server.exe)
    DREAM_LOG_INFO("Dedicated Server Mode Active");
});

DREAM_CLIENT_ONLY({
    // ส่วนนี้จะทำงานเฉพาะบน Game Client (enshrouded.exe)
    DREAM_LOG_INFO("Client GUI Mode Active");
});
```

---

### 11.2 เครือข่าย Network RPC Bridge (`DreamNetBridge`)
ส่งข้อมูลข้ามเครื่องระหว่าง Client และ Dedicated Server:

```cpp
// ฝั่ง Client ส่งข้อมูลขอรับ Buff จาก Server
dreamkurumi::NetBufferWriter writer;
writer.WriteString("RequestSpeedBuff");
dreamkurumi::DreamNetBridge::Instance().SendToServer("BuffChannel"_hash, 0x101, writer);

// ฝั่ง Server ดักจับและประมวลผล
dreamkurumi::DreamNetBridge::Instance().RegisterHandler("BuffChannel"_hash, 0x101, [](const auto& pkt) {
    dreamkurumi::NetBufferReader reader(pkt.payload);
    std::string buffName = reader.ReadString();
    DREAM_LOG_INFO("Player %u requested: %s", pkt.senderId, buffName.c_str());
});
```

---

### 11.3 การกระจายค่าคอนฟิกจาก Server สู่ผู้เล่น (`DreamServerSync`)
```cpp
// ฝั่ง Server กำหนดตัวคูณและกระจายหาผู้เล่นทุกคน
dreamkurumi::DreamServerSync::Instance().SetSyncVar("HarvestMultiplier", 2.0f);
dreamkurumi::DreamServerSync::Instance().BroadcastServerSettings();

// ฝั่ง Client รับค่าที่เซิร์ฟเวอร์กำหนด
float mult = dreamkurumi::DreamServerSync::Instance().GetSyncVarFloat("HarvestMultiplier", 1.0f);
```

---

## 12. คู่มือสำหรับนักพัฒนา C# .NET 8/10 (Native AOT)

สำหรับผู้ที่ถนัด C# สามารถสร้างม็อดความเร็วสูงระดับ Native ด้วย .NET 8/10 ผ่านฟีเจอร์ **Native AOT** (คอมไพล์ C# ออกมาเป็น x64 Native DLL โดยไม่ต้องพึ่งพา CLR runtime):

```csharp
using System;
using System.Runtime.InteropServices;
using DreamKurumi;

namespace MyDreamModCSharp
{
    public static unsafe class ModMain
    {
        [UnmanagedCallersOnly(EntryPoint = "XhlNativePluginStart")]
        public static int XhlNativePluginStart(IntPtr ctx)
        {
            DreamEngine.DK_Log(1, "[C# Mod] Started via dreamkurumiHT Native AOT!");

            if (DreamEngine.DK_Game_IsPlayerReady())
            {
                DreamEngine.DK_Game_SetGodMode(true);
                DreamEngine.DK_Game_TeleportPlayer(150f, 90f, 200f);
            }

            return 1;
        }

        [UnmanagedCallersOnly(EntryPoint = "XhlNativePluginRequestStop")]
        public static void XhlNativePluginRequestStop()
        {
            DreamEngine.DK_UnpatchAll();
        }
    }
}
```

---

## 13. เอนจิน JSON ความเร็วสูง (DreamJson Powered by yyjson)

Framework ได้รวมเอา **yyjson** ซึ่งเป็น JSON Library ภาษา C ที่เร็วที่สุดในโลก มาห่อหุ้มเป็น Modern C++20 API:

```cpp
#include <dreamkurumiHT/dreamkurumiHT.hpp>

// 1. อ่านข้อมูล JSON (Fast DOM Reader)
std::string jsonStr = R"({"radius": 5.0, "enabled": true, "targets": ["stone", "wood"]})";
auto doc = dreamkurumi::DreamJson::Parse(jsonStr);
if (doc.is_valid()) {
    double radius = doc["radius"].as_double(3.5);
    bool enabled  = doc["enabled"].as_bool(true);
}

// 2. สร้างข้อมูล JSON (Mutable Document)
dreamkurumi::DreamJsonMutDocument mutDoc;
mutDoc.add("status", "healthy");
mutDoc.add("entities", 1024);
std::string output = mutDoc.stringify(true); // Pretty Print
```

---

## 14. เอนจินสคริปต์ในหน่วยความจำ (DreamLua Powered by LuaJIT 2.1)

ฝังเอนจิน **LuaJIT 2.1 x64** เพื่อให้นักพัฒนาสามารถเขียนสูตรคำนวณ หรือให้ผู้เล่นแก้สคริปต์ `.lua` โดยไม่ต้องคอมไพล์ C++ ใหม่:

```cpp
auto& lua = dreamkurumi::DreamLuaEngine::Instance();
lua.Initialize();

// สั่งรันโค้ด Lua ที่ผูกกับ API ของ Dream
lua.ExecuteString(R"(
    dream.log("Hello from LuaJIT inside Enshrouded!")
    if dream.is_player_ready() then
        dream.set_god_mode(true)
        dream.teleport(200, 100, 300)
    end
)");
```

---

## 15. เครื่องมือพัฒนาและ Build Engine (`dev_sync.ps1`)

### 15.1 การ Build & Sync ทั้งระบบอัตโนมัติ
ในโฟลเดอร์ `tools/` มีสคริปต์ PowerShell สำหรับคอมไพล์และกระจาย DLL ทั้งหมด:

```powershell
# คอมไพล์และติดตั้งทุกอย่าง (Core, Server Loader, Server Mods, Client Mods)
powershell -ExecutionPolicy Bypass -File tools/dev_sync.ps1 -Target All

# คอมไพล์เฉพาะ Core Engine
powershell -ExecutionPolicy Bypass -File tools/dev_sync.ps1 -Target Core

# คอมไพล์เฉพาะม็อดฝั่ง Server
powershell -ExecutionPolicy Bypass -File tools/dev_sync.ps1 -Target ServerMods
```

### 15.2 เทคนิค Safe Hot-Replacement สลับ DLL ขณะเกมกำลังรัน (`.locked_*`)
บน Windows หากเกมกำลังเปิดอยู่ (`enshrouded.exe`) ระบบปฏิบัติการจะล็อกไฟล์ DLL ทำให้ไม่สามารถเขียนทับได้
สคริปต์ `dev_sync.ps1` ใช้เทคนิค **Win32 Rename Trick**:
* ไฟล์ DLL ที่ถูกล็อกอยู่จะถูกเปลี่ยนชื่อเป็น `.locked_<timestamp>` ทันที (Windows อนุญาตให้เปลี่ยนชื่อไฟล์ที่เปิดอยู่ได้!)
* วางไฟล์ DLL ที่คอมไพล์ใหม่ลงแทนที่ชื่อเดิม
* เกมสามารถโหลด DLL ใหม่ได้ทันทีโดยไม่ต้อง Restart หรือเสี่ยงเจอ Permission Denied Error!

---

## 16. แนวทางปฏิบัติที่ดีที่สุดและการป้องกันข้อผิดพลาด (Best Practices)

1. **อย่ารัน I/O หรือ Mutex หน่วงใน Hot Path:**
   * โค้ดใน Hook ที่รันทุกเฟรมหรือรันทุกครั้งที่เก็บไอเทม ห้ามเรียก `std::cout`, `printf` หรือ Disk File Write โดยตรง ให้ส่งผ่าน `host.Log()` ซึ่งทำงานแบบ Asynchronous แทน
2. **ปฏิบัติตาม Rule 1 & Rule 2 เสมอ:**
   * แยกม็อดเป็นโฟลเดอร์ของตนเอง อย่าแก้ไขโค้ดของ Core Framework
   * ตั้งค่า `AdminOnly = false` เป็น Default สำหรับม็อด QoL เพื่อให้ผู้เล่นทุกคนในเซิร์ฟเวอร์ได้รับประสบการณ์การเล่นที่ดีที่สุด
3. **ตรวจสอบ Null Pointer ผ่าน `DreamMemory`:**
   * หลีกเลี่ยง Raw Pointer Dereference (`*p = 10;`) โดยตรง ให้ใช้ `DreamMemory::Read<T>()` และ `DreamMemory::Write<T>()` เพื่อความปลอดภัย 100%
4. **ใช้ AOB Signatures แทน Fixed RVAs:**
   * สร้าง Signature ที่มี Wildcard `??` ในส่วนของ Offset/Displacement เพื่อให้ม็อดทนทานต่อการอัปเดตของเกม Enshrouded ทุกแพตช์

---
*จัดทำขึ้นเพื่อให้การพัฒนาม็อดเกม Enshrouded รวดเร็ว ปลอดภัย และมีประสิทธิภาพสูงสุด — dreamkurumiHT Team*

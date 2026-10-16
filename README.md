# dreamkurumiHT

Native Harmony C++20 Hooking Framework & Mod Engine for Enshrouded 1.0

[ภาษาไทย](#ภาษาไทย) | [English](#english)

---

## ภาษาไทย

### ภาพรวมของโปรเจกต์
dreamkurumiHT คือ Hooking Framework และ Modding Engine สำหรับเกม Enshrouded (เวอร์ชัน 1.0) ทำหน้าที่เป็นตัวโหลดและแพลตฟอร์มตัวกลางในการขับเคลื่อนม็อด เช่นเดียวกับ BepInEx ใน Unity หรือ SKSE ใน Skyrim

ตัวโปรแกรมประกอบด้วย:
1. `dinput8.dll` : ตัวโหลดม็อด (Universal Mod Loader) ทำหน้าที่โหลดระบบม็อดเข้าตัวเกมอัตโนมัติเมื่อเปิดเกม
2. `mods/dreamkurumiHT/` : ตัวเอนจินหลัก (Core Engine) ควบคุมการ Hook และจัดการสคริปต์ต่างๆ

ผู้เล่นทั่วไปสามารถนำไปติดตั้งเพื่อใช้งานม็อดต่างๆ ได้ทันที ส่วนนักพัฒนาสามารถสร้างม็อดของตัวเองในรูปแบบปลั๊กอิน C++ หรือสคริปต์ Lua มาเชื่อมต่อได้โดยตรง

---

### โครงสร้างไฟล์สำหรับผู้ใช้งานทั่วไป

```text
โฟลเดอร์เกม Enshrouded/
├── enshrouded.exe
├── dinput8.dll                 <-- วางไว้ข้างไฟล์เกม enshrouded.exe
└── mods/                       <-- โฟลเดอร์ม็อด
    ├── dreamkurumiHT/          <-- โฟลเดอร์หลักของ dreamkurumiHT
    │   ├── dreamkurumiHT.dll
    │   ├── lua51.dll
    │   ├── config.toml
    │   ├── native-plugin.ini
    │   ├── mod.json
    │   └── modinfo.json
    └── scripts/                <-- โฟลเดอร์สำหรับใส่ม็อด Lua
        └── example_mod.lua
```

---

### วิธีติดตั้งใช้งานแบบง่าย (สำหรับผู้เล่นทั่วไป)

1. ดาวน์โหลดไฟล์ `dreamkurumiHT-v1.0.0.zip` จากหน้า Releases
2. แตกไฟล์ ZIP
3. ก๊อปปี้ 2 อย่างนี้ไปวางในโฟลเดอร์เกม Enshrouded (โฟลเดอร์เดียวกับที่มี `enshrouded.exe`):
   * ไฟล์ `dinput8.dll`
   * โฟลเดอร์ `mods/`
4. เปิดเล่นเกมตามปกติ ตัวเอนจินจะเริ่มทำงานอัตโนมัติ

> หมายเหตุสำหรับ Dedicated Server:
> สามารถนำ `dinput8.dll` และโฟลเดอร์ `mods/` ไปวางที่โฟลเดอร์หลักของ `EnshroudedServer` ได้เช่นเดียวกัน

---

### วิธีสร้างม็อด (สำหรับนักพัฒนา)

#### 1. สร้างม็อด C++ (Native Plugin)
ดับเบิ้ลคลิกไฟล์ `create_mod.bat` หรือรันคำสั่ง:
```cmd
create_mod.bat "ชื่อม็อดของคุณ"
```
ระบบจะสร้างโครงสร้างโปรเจกต์ม็อดให้ทันที:
* `mods/ชื่อม็อด/src/dllmain.cpp` : โค้ดม็อด (เชื่อมต่อผ่าน `include/dreamkurumiHT/dreamkurumiHT_plugin.h`)
* `mods/ชื่อม็อด/native-plugin.ini` : คอนฟิกการโหลดม็อด
* `mods/ชื่อม็อด/config.toml` : การตั้งค่าของม็อด
* `mods/ชื่อม็อด/build.bat` : สคริปต์คอมไพล์ม็อดเป็นไฟล์ `.dll`

#### 2. สร้างม็อดด้วย Lua (สคริปต์ง่ายๆ ไม่ต้องคอมไพล์)
สร้างไฟล์ `.lua` แล้วนำไปวางไว้ในโฟลเดอร์ `mods/scripts/` เช่น `mods/scripts/my_mod.lua`:
```lua
dream.set_interval(function()
    dream.log("Hello from Lua Mod!")
end, 5000)
```
เมื่อเซฟไฟล์ ตัวเอนจินจะโหลดและทำงานใหม่ให้อัตโนมัติ (Hot-Reload) โดยไม่ต้องปิดเกม

---

### ฟีเจอร์ของเอนจิน

1. **Dynamic MidHook**: ดักจับและอ่าน/เขียนค่ารีจิสเตอร์ CPU (RAX-R15, XMM0-XMM15) ที่คำสั่งใดๆ ในเกมได้โดยตรง ไม่ต้องเขียน Assembly
2. **Declarative Harmony Hooking**: รองรับ Prefix, Postfix และ SkipOriginal พร้อมตัววิเคราะห์ความยาวคำสั่ง HDE64 ในตัว
3. **Hardware Breakpoints (VEH Engine)**: ดักจับการเข้าถึงหน่วยความจำผ่าน DR0-DR3 โดยไม่ต้องแก้ไขโค้ดเกมใน RAM
4. **CrashGuard**: ป้องกันเกมแครชจาก Access Violation (0xC0000005) ในฟังก์ชัน Hook หรือ Lua
5. **Vulkan Present Interceptor**: ดักจับเฟรม Vulkan (vkQueuePresentKHR) พร้อมปุ่มลัด F9 (Menu) / F10 (Console) สำหรับดีบัก
6. **Embedded LuaJIT 2.1**: รองรับการเขียนสคริปต์ Lua พร้อมระบบตรวจจับและรีโหลดอัตโนมัติ

---

### สัญญาอนุญาต
เผยแพร่ภายใต้สัญญาอนุญาต [MIT License](LICENSE) ลิขสิทธิ์ (c) 2026 kurumiht

---
---

## English

### Project Overview
dreamkurumiHT is a native Harmony C++20 hooking framework and modding platform for Enshrouded (Version 1.0). It acts as a modular mod loader and runtime, similar to BepInEx for Unity or SKSE for Skyrim.

The distribution consists of:
1. `dinput8.dll` : The Universal Mod Loader proxy that automatically boots the mod system when the game launches.
2. `mods/dreamkurumiHT/` : The core native engine handling memory hooking, logging, and script execution.

Players can drop it into the game folder to run mods immediately. Developers can create plugins in C++ or scripts in Lua without needing to modify dreamkurumiHT itself.

---

### File Structure for Players

```text
Enshrouded Game Directory/
├── enshrouded.exe
├── dinput8.dll                 <-- Place next to enshrouded.exe
└── mods/                       <-- Mod directory
    ├── dreamkurumiHT/          <-- Core engine directory
    │   ├── dreamkurumiHT.dll
    │   ├── lua51.dll
    │   ├── config.toml
    │   ├── native-plugin.ini
    │   ├── mod.json
    │   └── modinfo.json
    └── scripts/                <-- Lua scripts directory
        └── example_mod.lua
```

---

### Easy Installation Guide (For Players)

1. Download `dreamkurumiHT-v1.0.0.zip` from the Releases tab.
2. Extract the ZIP archive.
3. Copy these two items into your Enshrouded game folder (the directory containing `enshrouded.exe`):
   * `dinput8.dll`
   * `mods/` folder
4. Start the game normally. The engine will initialize automatically.

> Note for Dedicated Servers:
> Place `dinput8.dll` and the `mods/` directory into your `EnshroudedServer` root folder using the same steps.

---

### Creating Mods (For Developers)

#### 1. C++ Native Plugin
Run the project scaffolding script:
```cmd
create_mod.bat "YourModName"
```
Generated structure:
* `mods/YourModName/src/dllmain.cpp` : Mod source code (links via `include/dreamkurumiHT/dreamkurumiHT_plugin.h`)
* `mods/YourModName/native-plugin.ini` : Mod loader configuration
* `mods/YourModName/config.toml` : Runtime mod settings
* `mods/YourModName/build.bat` : Compiles your mod into `.dll`

#### 2. Lua Script Modding
Drop `.lua` files into `mods/scripts/`, for example `mods/scripts/my_mod.lua`:
```lua
dream.set_interval(function()
    dream.log("Hello from Lua Mod!")
end, 5000)
```
Scripts are loaded automatically and support live hot-reloading on save.

---

### Engine Features

1. **Dynamic MidHook**: Read and modify CPU registers (RAX-R15, XMM0-XMM15) at any instruction without writing assembly.
2. **Declarative Harmony Hooking**: Prefix, Postfix, and SkipOriginal execution flow with integrated HDE64 length disassembler.
3. **Hardware Breakpoints (VEH Engine)**: Monitor memory access using DR0-DR3 without modifying game code in RAM.
4. **CrashGuard**: Catches Access Violations (0xC0000005) inside hooks and scripts to prevent game crashes.
5. **Vulkan Present Interceptor**: Intercepts Vulkan frames (vkQueuePresentKHR) with F9 Menu / F10 Console hotkeys.
6. **Embedded LuaJIT 2.1**: Scripting engine with automatic script detection and hot-reloading.

---

### License
Distributed under the [MIT License](LICENSE). Copyright (c) 2026 kurumiht.

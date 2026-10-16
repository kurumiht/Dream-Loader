# dreamkurumiHT

Native Harmony C++20 Hooking Framework & Mod Engine for Enshrouded 1.0+

[ภาษาไทย](#ภาษาไทย) | [English](#english)

---

## ภาษาไทย

### ภาพรวมของโปรเจกต์
dreamkurumiHT คือ Core Native Hooking Framework และ Modding Engine สำหรับเกม Enshrouded (เวอร์ชัน 1.0) ทำหน้าที่เป็นแพลตฟอร์มตัวกลางในการขับเคลื่อนม็อด เช่นเดียวกับ BepInEx ในเกมเอนจิน Unity หรือ SKSE ใน Skyrim

จุดประสงค์ของ Engine นี้คือการเป็นแกนกลางที่สมบูรณ์ในตัวเอง นักพัฒนาสามารถศึกษาคู่มือแล้วสร้างม็อดของตัวเองในรูปแบบ Dynamic Plugins (C++) หรือสคริปต์ Lua มาเชื่อมต่อเข้ากับ Engine ได้ทันที โดยไม่มีความจำเป็นต้องแก้ไขหรือคอมไพล์ตัว dreamkurumiHT ใหม่

---

### จุดเด่นและฟีเจอร์หลัก

1. **Dynamic MidHook (Zero Assembly)**
   * ดักจับและแทรกการทำงานที่ระดับคำสั่ง CPU ใดๆ ในเกมได้โดยตรง
   * อ่านและเขียนค่ารีจิสเตอร์ CPU ได้ครบถ้วน ทั้ง General Purpose (RAX ถึง R15) และ Vector Registers (XMM0 ถึง XMM15) โดยไม่ต้องเขียนโค้ด Assembly
2. **Declarative Harmony Hooking**
   * ควบคุมการทำงานของฟังก์ชันในเกมได้อย่างอิสระ ทั้งแบบรันก่อน (Prefix), รันหลัง (Postfix) หรือสั่งข้ามฟังก์ชันเดิมของเกม (SkipOriginal)
   * มาพร้อมระบบถอดรหัสความยาวคำสั่ง CPU (HDE64 Disassembler) ในตัว ป้องกันปัญหา Opcode ฉีกขาด
3. **Hardware Breakpoints (VEH Engine)**
   * ดักจับการอ่าน เขียน หรือรันโค้ดในหน่วยความจำโดยใช้ Debug Registers (DR0 ถึง DR3) ของซีพียู โดยไม่แก้ไขโค้ดเกมใน RAM (Zero-byte Patching)
4. **CrashGuard Resilience**
   * ระบบ Structured Exception Handling (SEH) ช่วยดักจับข้อผิดพลาดร้ายแรง (เช่น Access Violation 0xC0000005) ภายใน Callbacks หรือ Lua Hooks ป้องกันไม่ให้ตัวเกมหรือเซิร์ฟเวอร์แครชล้มลง
5. **Vulkan Presentation Interceptor & Hotkey Console (F9)**
   * ระบบดักจับเฟรม Vulkan (vkQueuePresentKHR) พร้อมตัวจัดการปุ่มลัด (F9 Menu, F10 Console) และระบบ Interactive REPL Console สำหรับพิมพ์คำสั่งดีบักแบบเรียลไทม์
6. **Embedded LuaJIT 2.1 Engine**
   * รองรับการเขียนม็อดด้วยภาษา Lua เพียงวางไฟล์ไว้ในโฟลเดอร์ `mods/scripts/` พร้อมระบบ Hot-Reload แก้โค้ดแล้วทำงานใหม่ทันทีโดยไม่ต้องรีสตาร์ตเกม
7. **Universal C-ABI Dynamic Linking**
   * รองรับการสร้างปลั๊กอินด้วยภาษา C++, C#, Rust, Python หรือ Zig ผ่านส่วนหัว `dreamkurumiHT_plugin.h`

---

### กฎสถาปัตยกรรม (Architectural Principle)

* **Mod is Mod, Dream is Harmony Engine:** ตัว dreamkurumiHT ทำหน้าที่เป็นโครงสร้างพื้นฐานสำหรับ Hook และจัดการระบบความปลอดภัยเท่านั้น จะไม่มีตรรกะหรือโค้ดเฉพาะเจาะจงของม็อดเกมรวมอยู่ภายใน Engine
* ม็อดทั้งหมดต้องแยกอยู่อย่างเป็นอิสระและเชื่อมต่อเข้ามาผ่าน C-ABI Interface

---

### วิธีสร้างม็อดสำหรับนักพัฒนา

#### วิธีที่ 1: สร้างม็อด C++ (Native Plugin)
ใช้สคริปต์สร้างโครงสร้างม็อดอัตโนมัติ:
```cmd
create_mod.bat "My-Mod-Name"
```
ระบบจะสร้างโฟลเดอร์และไฟล์ที่จำเป็นให้ทันที:
* `mods/My-Mod-Name/src/dllmain.cpp` : ไฟล์เขียนโค้ดม็อด
* `mods/My-Mod-Name/native-plugin.ini` : คอนฟิกการโหลดม็อด
* `mods/My-Mod-Name/config.toml` : ไฟล์ตั้งค่าของม็อด
* `mods/My-Mod-Name/build.bat` : สคริปต์คอมไพล์ม็อด

#### วิธีที่ 2: สร้างม็อดด้วยสคริปต์ Lua
สร้างไฟล์สคริปต์ `.lua` ในโฟลเดอร์ `mods/scripts/` เช่น:
```lua
-- ตัวอย่าง: ดักจับและพิมพ์ข้อความแจ้งเตือนทุก 5 วินาที
dream.set_interval(function()
    dream.log("Hello from Lua Mod!")
end, 5000)
```
เมื่อเปิดเกม ตัว Engine จะตรวจพบและรันสคริปต์ให้อัตโนมัติ

---

### วิธีคอมไพล์ Engine จาก Source Code

#### ข้อกำหนดของระบบ:
* คอมไพเลอร์ GCC ที่รองรับ C++20 (เช่น w64devkit หรือ MSYS2 MinGW-w64)
* Windows 10/11 64-bit

#### ขั้นตอนการคอมไพล์:
รันสคริปต์บิลด์ที่โฟลเดอร์หลัก:
```cmd
build.bat
```
สคริปต์จะตรวจหาคอมไพเลอร์ในระบบและคอมไพล์ `dreamkurumiHT.dll` ออกมาให้ทันที

---

### เอกสารทางเทคนิคเพิ่มเติม
* **คู่มือนักพัฒนา (Developer Guide):** ศึกษาโครงสร้าง C-ABI และตัวอย่างการใช้งาน API ที่ [DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md)
* **คัมภีร์เจาะลึกโครงสร้างเกม 1.0 (Engine Internals Bible):** ศึกษา Structs, Signatures และ RVAs ได้ที่ [ENSHROUDED_1.0_ENGINE_INTERNALS_BIBLE.md](ENSHROUDED_1.0_ENGINE_INTERNALS_BIBLE.md)

---

### สัญญาอนุญาต
โปรเจกต์นี้เผยแพร่ภายใต้สัญญาอนุญาต [MIT License](LICENSE) ลิขสิทธิ์ (c) 2026 kurumiht

---
---

## English

### Project Overview
dreamkurumiHT is a native hooking framework and modding platform engineered specifically for Enshrouded (Release 1.0). It acts as an extensible mod runtime, similar to BepInEx in Unity or SKSE in Skyrim.

The core engine is self-contained. Mod developers can study the provided documentation and create independent plugins (in C++) or Lua scripts without needing to modify, recompile, or interfere with dreamkurumiHT itself.

---

### Key Engine Features

1. **Dynamic MidHook (Zero Assembly)**
   * Hook arbitrary CPU instructions with complete runtime control.
   * Read and modify general-purpose registers (RAX through R15) and vector registers (XMM0 through XMM15) with zero standalone assembly files.
2. **Declarative Harmony Hooking**
   * Prefix, Postfix, and SkipOriginal execution flow control.
   * Integrated HDE64 instruction length disassembler prevents opcode boundary truncation.
3. **Hardware Breakpoints (VEH Engine)**
   * Intercept memory read, write, and execute operations via hardware debug registers (DR0 through DR3) with zero RAM patching.
4. **CrashGuard Resilience**
   * Structured Exception Handling (SEH) isolates Access Violations (0xC0000005) inside callbacks and Lua hooks, keeping the game client and dedicated server stable.
5. **Vulkan Presentation Interceptor & Hotkey Console (F9)**
   * Intercepts Vulkan presentation frames (vkQueuePresentKHR) with built-in hotkey polling (F9 Menu, F10 Console) and an interactive REPL console for runtime diagnostics.
6. **Embedded LuaJIT 2.1 Engine**
   * Create lightweight mods in Lua with automatic hot-reloading from `mods/scripts/`.
7. **Universal C-ABI Dynamic Linking**
   * Build plugins in C++, C#, Rust, Python, or Zig using the header `dreamkurumiHT_plugin.h`.

---

### Architectural Principle

* **Mod is Mod, Dream is Harmony Engine:** dreamkurumiHT serves exclusively as a native hooking framework and runtime safety layer. No game-specific mod logic is embedded in the engine core.
* All plugins remain completely modular and communicate via the stable C-ABI interface.

---

### Creating Mods (Developer Quickstart)

#### Method 1: C++ Native Plugin
Generate a complete mod scaffold using the included batch tool:
```cmd
create_mod.bat "My-Mod-Name"
```
Generated structure:
* `mods/My-Mod-Name/src/dllmain.cpp` : Plugin source code
* `mods/My-Mod-Name/native-plugin.ini` : Plugin loader configuration
* `mods/My-Mod-Name/config.toml` : Live runtime settings
* `mods/My-Mod-Name/build.bat` : Standalone build script

#### Method 2: Lua Scripting
Place `.lua` files in `mods/scripts/`:
```lua
-- Example: recurring timer callback
dream.set_interval(function()
    dream.log("Hello from Lua Mod!")
end, 5000)
```
The engine automatically loads and hot-reloads scripts on save.

---

### Building the Engine from Source

#### Prerequisites:
* GCC compiler with C++20 support (e.g., w64devkit or MSYS2 MinGW-w64).
* Windows 10/11 64-bit.

#### Build Command:
Execute the root build script:
```cmd
build.bat
```
The script locates your compiler and builds `dreamkurumiHT.dll`.

---

### Technical References
* **Developer Guide:** Architecture deep-dive, ABI specifications, and API examples in [DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md).
* **Engine Internals Bible:** Verified signatures, RVAs, and struct definitions in [ENSHROUDED_1.0_ENGINE_INTERNALS_BIBLE.md](ENSHROUDED_1.0_ENGINE_INTERNALS_BIBLE.md).

---

### License
Distributed under the [MIT License](LICENSE). Copyright (c) 2026 kurumiht.

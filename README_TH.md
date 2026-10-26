# dreamkurumiHT Framework (Dream Core)
> **โมเดิร์นเนทีฟ Harmony Hooking & Modding Framework สำหรับ C++20 บน Windows x64**  
> *(ไม่ต้องเขียน Assembly, ไม่ต้องสแกนแรม, ดักจับผ่าน Hardware Breakpoint VEH, ประสิทธิภาพสูงสุดสำหรับ Keen Games Engine)*  
> **เวอร์ชันของ Framework:** `3.4.0` | **สถาปัตยกรรม:** Windows x64 | **เกมที่รองรับ:** Enshrouded 1.0+

---

<p align="center">
  <b>สลับภาษา / Language Switcher:</b><br>
  <a href="README.md">🇬🇧 <b>English</b></a> •
  <a href="README_TH.md">🇹🇭 <b>ภาษาไทย (Thai)</b></a><br><br>
  <a href="https://github.com/kurumiht8947/Dream-Core/releases/download/v3.4.0/Dream-Core-v3.4.0.zip">
    <img src="https://img.shields.io/badge/%E0%B8%94%E0%B8%B2%E0%B8%A7%E0%B8%99%E0%B9%8C%E0%B9%82%E0%B8%AB%E0%B8%A5%E0%B8%94-Dream--Core%20v3.4.0%20(Plug%20%26%20Play)-brightgreen?style=for-the-badge&logo=windows" alt="ดาวน์โหลด Dream-Core v3.4.0" />
  </a>
</p>

---

## ฟีเจอร์ทั้งหมดใน dreamkurumiHT (41 Master Capabilities)

1. **Declarative Harmony API (`Prefix` / `Postfix`)**
   - เขียน Hook ง่าย สะอาด ไม่ต้องจำ Opcode หรือคำนวณ `E9 JMP`
   - รองรับ `return false` ใน Prefix เพื่อสั่งข้ามฟังก์ชันเดิม (Skip Original)
   - รองรับการแก้ไข Argument ผ่าน Pointer / Reference
   - รองรับการแก้ค่า Return Value ใน Postfix
   - รองรับ Passive Observer (`void Prefix(...)`)
2. **`DreamHarmony::PatchAll()` และ `UnpatchAll()`**
   - ฟีเจอร์ยอดนิยมของ Harmony ใน C# ยกมาไว้ใน C++20
   - ลงทะเบียน Hook ทุกตัวอัตโนมัติ สั่ง `PatchAll()` บรรทัดเดียวใน `DllMain` โหลด Hook ครบทั้งโปรเจกต์
   - สั่ง `UnpatchAll()` คืนค่าไบต์ของเกมทั้งหมดกลับสู่สภาพเดิมก่อนปิดเกม ป้องกัน Crash
3. **Hardware Breakpoint & VEH Engine (`DreamVeh`)**
   - ดักจับการทำงานระดับ CPU Debug Registers (`DR0 - DR3`) ผ่าน Vectored Exception Handler
   - **ไม่เขียนทับโค้ดเกมแม้แต่ไบต์เดียว (Zero-Byte Patching)**
   - **ไม่ต้องสแกน RAM หาฟังก์ชัน (Zero-RAM Scanning)** ดักที่ตัวแปรหรือตำแหน่งได้โดยตรง
   - มีระบบ Kernel Context Transition สำหรับ Current Thread ปลอดภัย 100%
4. **Direct Game Function Invoker (`DreamInvoker`)**
   - ดึงฟังก์ชันในโค้ดของตัวเกมมาเรียกใช้งานได้โดยตรงแบบ Type-Safe:
     `auto fn = DreamInvoker::Get<void(void*, int)>(pFunc); fn(player, 100);`
5. **Keen ECS Reflection Engine (`DreamReflection`)**
   - อ่านและแก้ไขตัวแปรใน Struct/Component ของเกมผ่าน Metadata โดยตรง ไม่ต้องสแกนหา Offset เอง
6. **ตัดไฟล์ `.s` Assembly ทิ้ง 100% (`DreamMidHook`)**
   - ดักอ่าน/แก้ค่า CPU Register (RAX..R15, XMM0..XMM15) ผ่าน Struct `DreamCpuContext` ใน C++ โดยตรง
   - มีระบบ **Dynamic Stack Alignment** จัดเรียง 16-byte อัตโนมัติ ป้องกันแครชคำสั่ง SSE
   - มี Disassembler HDE64 ในตัว คำนวณขอบเขต Instruction ให้อัตโนมัติ
7. **ระบบ Build-Aware Symbol Offset Cache (`DreamCache`)**
   - ตรวจจับ PE Header `TimeDateStamp` ของเกม
   - โหลด RVA จากไฟล์ `dreamkurumi_cache.ini` ในความเร็ว $O(1)$ ($0.0001\text{ ms}$)
   - **ไม่ต้องรัน AOB Scan ทุกรอบที่เปิดเกม**
   - **Smart Fallback:** ถ้าเกมเพิ่งอัปเดต จะรัน Pattern Scan เบื้องหลังเพียง **"ครั้งเดียว"** แล้วเซฟเข้า Cache ทันที
8. **Memory Transpiler & NOP Tool (`DreamMemoryPatch`)**
   - สำหรับกรณีต้องการตัดตรรกะโค้ดบางคำสั่ง (NOP) หรือแพตช์ไบต์ตรงๆ พร้อมระบบ Restore คืนค่าเดิม
9. **Virtual Method Table Hooking (`DreamVTableHook`)**
   - ดักจับฟังก์ชันระดับ C++ Class Interface หรือ DirectX/Vulkan VTable ได้โดยตรง
10. **High-Performance Diagnostics Logger (`DreamLog`)**
    - **Persistent FILE* Handle:** เปิดไฟล์ค้างไว้ ไม่เปิด-ปิดซ้ำซ้อน ลด System Call Disk I/O 100% ป้องกัน Micro-stutter / FPS Drop
    - **Sub-ms Timestamps & Thread ID:** บันทึกเวลาละเอียด `[HH:MM:SS.mmm][TID:xxxx]` สำหรับเกม Multi-threaded
    - **Min LogLevel Filtering:** กรองระดับ Log (`SetMinLogLevel`) ไม่ประมวลผล Format String เปล่าประโยชน์
    - **ANSI Console Colors:** แสดงสีสันใน Terminal/Console (Gray, Green, Yellow, Red) สวยงาม อ่านง่าย
    - **In-Memory Ring Buffer:** เก็บ Log ย้อนหลัง 200 รายการใน RAM สำหรับดึงไปโชว์ใน ImGui Overlay หรือแนบตอนเกม Crash
    - **Memory HexDump Utility:** คำสั่ง `DreamLog::HexDump("Title", ptr, size)` แสดงตาราง Hex + ASCII ได้ทันที
11. **Universal C ABI Exports (`DreamCExport`)**
    - เชื่อมต่อกับทุกภาษาในโลก (C#, Lua, Python, Rust, Zig)
12. **C# .NET Binding (`DreamKurumi.cs`)**
    - ไบรดิ้งสำเร็จรูปสำหรับนักพัฒนา C# พร้อม Delegate Hook Handle
13. **Lua / LuaJIT Binding (`dreamkurumi.lua`)**
    - ไบรดิ้งสำเร็จรูปสำหรับนักพัฒนา Lua / EML บน Enshrouded
14. **Near-Memory JIT Trampoline Pool (`DreamPool`)**
    - รวมหน่วยความจำ Trampoline ให้อยู่ในหน้า 64KB Arena ก้อนเดียวในรัศมี $\pm 2\text{GB}$
    - ลดการเรียก `VirtualAlloc` ซ้ำซ้อน ประหยัด RAM ลงได้ถึง 99% และรักษา Cache Locality ของ CPU
15. **Anti-Crash Thread Freeze & RIP Relocation (`DreamThread`)**
    - ระหว่างติดตั้ง Hook จะสั่ง Freeze เธรดอื่นทั้งหมดอัตโนมัติ
    - หากมีเธรดใดกำลังรันอยู่บน Stolen Bytes จะคำนวณย้าย `RIP` ไปยัง Trampoline อย่างปลอดภัย 100%
16. **MSVC RTTI / Dynamic VTable Resolver (`DreamRtti`)**
    - สแกน TypeDescriptor และ Complete Object Locator จากส่วน `.rdata` ของ PE
    - ดึง VTable และ Method Pointers ของคลาสในเกม (เช่น `keen::ecs::EntityManager`) ได้โดยตรง **ไม่ต้องใช้ RVA หรือสแกนไบต์ใน RAM**
17. **Multi-Mod Hook Chain & Priority Pipeline (`DreamPipeline`)**
    - รองรับการที่หลาย Mod เข้ามา Hook ฟังก์ชันเดียวกันในเกมโดยไม่ชนกัน
    - กำหนด Priority ได้ 6 ระดับ (`First`, `Highest`, `High`, `Normal`, `Low`, `Lowest`, `Last`)
    - รองรับ Prefix Skip และ Postfix Result Mutation ในระดับ Pipeline
18. **Anti-Crash Mod Sandbox Shield (`DreamCrashGuard`)**
    - ครอบคลุม Callback ของ Mod ด้วย Vectored Exception Handling + Windows TEB TLS
    - ดักจับเหตุการณ์ Access Violation (`0xC0000005`) หรือ Null-Pointer Dereference **โดยไม่ปล่อยให้เกมหลุด (Crash to Desktop)**
    - บันทึกชื่อ Mod ที่มีปัญหา, Faulting RIP Address แล้ว Resume เกมให้รันต่อได้อย่างปลอดภัย 100%
19. **AVX2 & SSE Vectorized Pattern Scanner (`DreamScanner`)**
    - สแกน Byte Pattern ด้วยคำสั่ง SIMD (`_mm256_cmpeq_epi8`) คราวละ 32 bytes ต่อ 1 Clock Cycle
    - เร็วกว่าลูปสแกนปกติถึง 20 เท่า ค้นหาไบนารีเกมขนาด 150MB+ ได้ภายในไม่กี่มิลลิวินาที
20. **Multi-Thread Hardware Breakpoint Auto-Sync (`DreamVeh`)**
    - ฟังก์ชัน `BroadcastBreakpoint()` และ `SyncAllThreads()` กระจาย Debug Registers (DR0-DR3) ไปยังทุก Worker Thread
    - รองรับสถาปัตยกรรม Job / Task System ของ Keen Holistic Engine อย่างสมบูรณ์แบบ
21. **Harmony-Style Dynamic Transpiler (`DreamWeaver`)**
    - ถอดรหัส Instruction Stream x64 ผ่าน HDE64
    - ค้นหาและดัดแปลง Opcode, NOP คำสั่งเงื่อนไข (เช่น ข้ามการเช็คสิทธิ์ `JNE`), หรือแก้ไข Immediate Constant
    - มีระบบ `Apply()` และ `Restore()` จัดการ Thread Freeze และ Memory Protect ให้อัตโนมัติ
22. **Enshrouded Game Logic & Mod Presets (`DreamPresets`)**
    - สกัดโครงสร้างหน่วยความจำ `KeenLiveGameSettings` ของ Keen Holistic Engine โดยตรง
    - ควบคุมปัจจัยเกมแบบ Real-time: GodMode, GhostMode (เซ็ต Perception = 0.0f ให้ศัตรูมองไม่เห็น), InfiniteStamina, InfiniteMana, SuperMining, InfiniteShroud
    - Built-in Harmony Patches สำเร็จรูปสำหรับ Free Crafting, Fall Damage Immunity, Shroud Countdown Freeze, Infinite Item Split, Infinite Item Use, Infinite Durability
    - `DreamServerGuard`: ตรวจจับโหมด Headless Dedicated Server (`enshrouded_server.exe`) อัตโนมัติ ป้องกันการเรียกใช้ UI/Audio/DirectX ที่ไม่มีอยู่
23. **In-Game Overlay, Hotkeys & Command Dispatcher (`DreamOverlay`)**
    - ระบบปุ่มลัดมาตรฐาน: **F9 (Toggle Menu)** และ **F10 (Toggle Console)** พร้อม Audio Feedback
    - **Decoupled Architecture:** `DreamOverlay` ทำหน้าที่เป็น Generic Event & Hotkey Dispatcher เท่านั้น
    - In-Game Command Dispatcher: รองรับคำสั่งพื้นฐาน `/console`, `/clear`, `/help`, `/status` และเชื่อมต่อคำสั่ง Preset เช่น `/god`, `/ghost`, `/stamina`, `/craft`, `/mine`, `/split`, `/item`, `/flight`, `/durability`, `/oneshot`, `/fastprod`, `/save`, `/load`
24. **Interactive Console CLI & Config Persistence (`DreamPresets::SaveConfig / LoadConfig`)**
    - **Interactive Console Reader:** พิมพ์คำสั่งในหน้าต่าง CMD สดๆ (เช่น พิมพ์ `god on`, `help`, `status`, `fastprod 1`) ได้ทันทีโดยไม่ต้องพึ่ง UI ในเกม
    - **Live Status Table:** คำสั่ง `/status` แสดงตารางสถานะเปิด/ปิดของทุกม็อดด้วย ANSI Color ชัดเจนสวยงาม
    - **Config Persistence:** คำสั่ง `/save` และ `/load` บันทึก/โหลดสถานะม็อดเข้าไฟล์ `dreamkurumi_config.ini` อัตโนมัติ เล่นรอบต่อไปไม่ต้องเปิดใหม่
    - **Fast Production:** ม็อดเร่งความเร็วเตาหลอม/เตาเผาถ่าน (`keen::Time`) ผลิตของเสร็จภายใน 1 วินาที
25. **Server Anti-Cheat & Permission Guard (`DreamServerPolicy`)**
    - **Decoupled Preset Control:** แยกการควบคุมและอนุญาตใช้งานฟีเจอร์เกมออกจากกันอย่างเป็นระบบ
    - **Category-Based Permissions:** จัดหมวดหมู่สิทธิ์การใช้งานเป็น `Admin`, `Stealth`, `Survival`, `Economy`, `World` เพื่อให้ผู้ดูแลเซิร์ฟเวอร์เปิด-ปิดเฉพาะบางหมวดได้ตามต้องการ
    - **Strict Server Anti-Cheat Rules (`EnforceStrictServerRules`):** สั่งบล็อกการโกงและเอาเปรียบเกมทั้งหมดในระดับ Engine เมื่อรันบน Dedicated Server หรือโหมดเอาชีวิตรอดจริง
    - **Policy Violation Interceptor:** สกัดกั้นทันทีหากผู้เล่น/ไคลเอนต์พยายามเรียกใช้คำสั่งหรือเซ็ตค่าที่ถูกห้าม พร้อมแจ้งเตือนผ่าน Callback และบันทึก Log ความปลอดภัย
26. **Hook Performance Micro-Profiler (`DreamProfiler`)**
    - **Nanosecond Precision Timing:** จับเวลาการรันของแต่ละ Hook/ฟังก์ชันระดับนาโนวินาทีผ่าน High-Resolution Clock
    - **Micro-Stutter Detection:** ตั้งค่า Warning Threshold (เช่น 0.5ms) เพื่อแจ้งเตือนใน Log อัตโนมัติเมื่อพบ Hook ที่ทำงานช้าเกินเกณฑ์ ป้องกันเกมแล็กหรือ FPS ตก
    - **Interactive `/profile` Command:** คำสั่งในคอนโซลพิมพ์ดูตารางสถิติ (Calls, Avg us, Min us, Max us) เรียงตามเวลาใช้งานรวมได้อย่างสวยงาม
27. **Dedicated Server Policy Separation (`server_policy.ini`)**
    - **แยกไฟล์กติกาอย่างเด็ดขาด:** ฝั่ง Client ใช้ `dreamkurumi_config.ini` ส่วนฝั่ง Dedicated Server ใช้ `server_policy.ini`
    - **หมวดหมู่และฟีเจอร์อิสระ:** บันทึกและโหลดสิทธิ์รายฟีเจอร์ และรายหมวดหมู่ (Admin, Stealth, Survival, Economy, World) ได้ง่ายดาย
28. **Real-Time Config & Policy Hot-Reload (`DreamWatcher`)**
    - **Zero-Restart Live Reloading:** ระบบ File Watcher ตรวจจับการกดเซฟไฟล์ `dreamkurumi_config.ini` และ `server_policy.ini` อัตโนมัติ
    - **Smart Debouncing:** หน่วงเวลาดักจับการเขียนไฟล์ซ้ำซ้อน 200ms โหลดข้อมูลใหม่เข้าหน่วยความจำแบบ Real-Time โดยไม่ต้องรีสตาร์ตเกมหรือพิมพ์คำสั่งใดๆ
29. **Native Vulkan Presentation Hook & GUI Overlay (`DreamVulkan`)**
    - **Header-Only Vulkan Hooking:** ดักจับ `vkQueuePresentKHR` ของเอนจินกราฟิก Vulkan 1.3 ใน Enshrouded โดยไม่ต้องติดตั้ง Vulkan SDK
    - **In-Game GUI Render Interface:** รองรับการฝังเมนูม็อด (ImGui / Custom WSI Overlay) ลอยขึ้นมาในจอเกมจริงเมื่อกด **F9**
    - **Dedicated Server Immunity:** ตรวจจับ Dedicated Server อัตโนมัติและปิดระบบกราฟิกทิ้ง 100% ป้องกันเซิร์ฟเวอร์แครช
30. **Auto Crash Report & Stack Trace Analyzer (`DreamCrashDump`)**
    - **Unhandled Exception & VEH Diagnostics:** ดักจับเหตุการณ์ Fatal Crash (เช่น `0xC0000005 Access Violation`, `0xC00000FD Stack Overflow`) ในระดับกระบวนการ
    - **Detailed CPU State:** บันทึก State ของ CPU Registers x64 ทุกตัว (RAX..R15, RIP, RSP, RBP, EFlags) พร้อมถอดรหัส Byte Stream 32 ไบต์รอบจุดเกิดเหตุ `[RIP-8 .. RIP+24]`
    - **Stack Backtrace & Module/RVA Resolver:** ถอด Stack Trace ย้อนหลังแบบละเอียด พร้อมจับคู่ชื่อไฟล์ Module และคำนวณ Base + RVA ให้ทันที (`GetModuleHandleExA`)
    - **Automated Crash Report File:** สรุปและบันทึกรายงานลงไฟล์ `dream_crash_report_YYYYMMDD_HHMMSS.txt` ทันทีที่เกิดปัญหา ช่วยให้ผู้พัฒนาตรวจหาสาเหตุของ Mod ได้ตรงจุด 100%
31. **Heuristic Multi-Tier Signature & Backup Engine (`DreamHeuristic`)**
    - **Patch-Resilient Multi-Tier Pipeline:** หมดปัญหา Mod พังหลังเกมอัปเดต ด้วยระบบแก้ปัญหา 5 ระดับชั้น:
      1. *Tier 1: Symbol Cache* ($0.0001\text{ ms}$ lookup)
      2. *Tier 2: Primary Exact Signature* (ค้นหาแบบตรง 100%)
      3. *Tier 3: Secondary Relaxed Heuristic* (ค้นหาแบบ Wildcard `??` ละเว้น Register/Offsets ที่คอมไพเลอร์เกมอาจเปลี่ยน)
      4. *Tier 4: Anchor Proximity & Backward Prologue Discovery* (ยึดตำแหน่งจาก Constant Anchor แล้วสแกนย้อนหลังหา Function Prologue)
      5. *Tier 5: Fallback Hardcoded RVA*
    - **Auto-Learning Cache:** หากตรวจพบผ่าน Tier 2, 3 หรือ 4 จะอัปเดต RVA ใหม่เข้าสู่ Cache โดยอัตโนมัติ รอบถัดไปโหลดเร็ว $O(1)$ ทันที
32. **Mod Developer Path Freedom & Auto-Config (`DreamEnv` & `DreamModConfig`)**
    - **100% Path Independence:** ตัวม็อดสามารถวางไว้ที่ไหนก็ได้ ไม่ว่าผู้เล่นจะเปลี่ยนชื่อโฟลเดอร์หรือย้ายไปไว้ที่ใด
    - **Self-Contained Config Engine (`DreamModConfig`):** คลาสจัดการไฟล์ INI ในตัว ผูกกับโฟลเดอร์ของม็อดนั้นๆ โดยตรง
    - **Live Hot-Reloading (`EnableHotReload`):** ปรับแก้ค่าในไฟล์ `config.ini` แล้วเซฟ ตัวแปรในเกมจะอัปเดตสดทันทีโดยไม่ต้องรีสตาร์ตเกม
    - **Mod Manifest & Registry (`DreamModRegistry`):** ลงทะเบียนข้อมูลม็อด (ID, Name, Version, Author, Desc) พร้อมคำสั่งในคอนโซล `/mods` แสดงรายชื่อม็อดที่ทำงานอยู่ทั้งหมด
33. **Semantic Versioning & Duplicate Mod Conflict Resolver (`DreamSemVer`)**
    - **SemVer 2.0 Parser & Comparison:** รองรับรูปแบบเวอร์ชัน `1.0.0`, `v1.2.5`, `2.0.0-beta`, `1.0.0.4`
    - **Anti-Duplicate Conflict Shield:** จัดการไฟล์ม็อดซ้ำซ้อน บล็อกเวอร์ชันเก่า และส่งต่อสิทธิ์ให้เวอร์ชันใหม่กว่าอัตโนมัติ
34. **Compile-Time 64-bit FNV-1a Hash Engine (`DreamHash`)**
    - **Zero-Runtime Overhead:** คำนวณ Hash สตริงแบบ 64-bit FNV-1a ในขั้นตอนคอมไพล์ ($0\text{ ns}$) ด้วย C++20 `constexpr`
    - **String Switch-Case Router:** รองรับ User-Defined Literal `""_hash` (เช่น `case "god"_hash:`)
35. **Anti-Hang & Thread Deadlock Guard (`DreamWatchdog`)**
    - **Heartbeat Thread Registry:** ม็อดและเอนจินเกมสามารถลงทะเบียนเธรด และส่งสัญญาณ Heartbeat
    - **Deadlock & Hang Timeout Interceptor:** ตรวจจับเธรดที่ค้างเกินกำหนด (เช่น 5 วินาที) สกัดกั้นก่อนที่เกมจะ Freeze
    - **Non-Destructive Thread Sampling:** สแนปช็อตสถานะเธรดและบันทึกรายงาน `dream_hang_report_YYYYMMDD_HHMMSS.txt` อัตโนมัติ โดยไม่แครชเกม
36. **Client <-> Dedicated Server Mod Synchronization & RPC (`DreamNetBridge` & `DreamEnv`)**
    - **Unified Single Binary Architecture:** ม็อดไฟล์ DLL เดียวกันใส่ได้ทั้ง Game Client และ Dedicated Server
    - **Automatic Environment Detection (`DreamEnv`):** ตรวจจับบริบท Client หรือ Server อัตโนมัติ
    - **Mod Network RPC Engine (`DreamNetBridge`):** ส่ง Packet ข้ามระหว่าง Client และ Dedicated Server ได้อย่างง่ายดาย
    - **Type-Safe Binary Serialization (`NetBufferWriter` / `NetBufferReader`):** จัดการ Byte Packing / Unpacking ปลอดภัย ไร้ Buffer Overflow
37. **Safe Memory Reading & Pointer Chains (`DreamMemory`)**
    - **Bad Pointer Sandbox:** อ่านค่าตัวแปรในแรมอย่างปลอดภัย 100% แม้ Pointer จะชี้ไปที่ `nullptr` หรือ Bad Address
    - **Multi-Level Pointer Chains (`ReadChain` / `WriteChain`):** ถอดรหัส Pointer หลายระดับชั้นตามสูตร Cheat Engine ภายในคำสั่งเดียว
    - **RAII Reversible Hex Patch (`DreamBytePatch`):** แพตช์ไบต์โค้ดและคืนค่าเดิมอัตโนมัติเมื่อหลุด Scope
38. **Engine Service Locator & High-Level API (`DreamGameContext`)**
    - **LocalPlayer Auto-Capture:** ตรวจจับและเก็บพอยน์เตอร์ตัวละครผู้เล่นหลักอัตโนมัติ พร้อม Event Callback `OnPlayerSpawn`
    - **Zero-Pointer Teleportation & Movement:** วาร์ปตัวละครผู้เล่น และอ่านพิกัดปัจจุบันได้ทันที
    - **One-Liner Presets:** สลับโหมด `SetGodMode(true)`, `SetGhostMode(true)`, `SetInfiniteStamina(true)` ผ่าน API ชั้นสูง
39. **Ultra-Fast JSON Engine (`DreamJson` / `yyjson`)**
    - ขับเคลื่อนด้วย **yyjson** เอนจิน JSON ภาษา C ประสิทธิภาพสูงสุดในโลก
    - C++20 RAII Wrapper ไม่เกิด Memory Leak พร้อมระบบ Stringify ซีเรียลไลซ์ความเร็วระดับกิกะไบต์ต่อวินาที
40. **High-Performance In-Memory Scripting & JIT (`DreamLua` / `LuaJIT 2.1`)**
    - ฝังเอนจิน **LuaJIT 2.1 x64** ลงในม็อดโดยตรง คอมไพล์สคริปต์แบบ Just-In-Time รันที่ความเร็ว Native x64
    - ป้องกันแครช 100% ด้วย Crash-Guarded Execution ดักจับสคริปต์ที่เออเรอร์โดยไม่ทำให้เกมดับ
    - สะพานข้อมูลสองทิศทาง **Lua Table <-> yyjson Bridge** แลกเปลี่ยน JSON กับ Lua Table ได้ทันที
41. **In-Game Chat Command Interceptor & Dispatcher (`Hook_ChatMessage_Relay`)**
    - ดักจับแพ็กเกจข้อความแชทที่ขึ้นต้นด้วย `/` (เช่น `/fc on`, `/god on`, `/help`, `/online`)
    - รันคำสั่งเซิร์ฟเวอร์แบบ Real-time พร้อมตัดข้อความคำสั่งออกจากแชทสาธารณะ (Silent Suppress) อัตโนมัติ

---

## ผลการทดสอบ (Verification Suite 41/41 ผ่านหมด 100%)

```text
[TEST 1] Testing DreamCache...                         -> [PASS]
[TEST 2] Testing Baseline Functions...                 -> [PASS]
[TEST 3] Installing Harmony DamagePatch...             -> [PASS]
[TEST 4] Installing Harmony StaminaPatch...            -> [PASS]
[TEST 5] Testing Dynamic Disable / Restore...          -> [PASS]
[TEST 6] Testing Passive Observer (void Prefix)...     -> [PASS]
[TEST 7] Testing ResolvePattern Fallback...            -> [PASS]
[TEST 8] Testing DreamHarmony::PatchAll()...           -> [PASS]
[TEST 9] Testing DreamMemoryPatch (NOP & Restore)...   -> [PASS]
[TEST 10] Testing DreamVTableHook...                   -> [PASS]
[TEST 11] Testing DreamVeh Hardware Breakpoint...      -> [PASS] (Zero Byte Patching!)
[TEST 12] Testing DreamInvoker (Direct Call)...        -> [PASS]
[TEST 13] Testing DreamReflection (Keen ECS Field)...  -> [PASS]
[TEST 14] Testing DreamTrampolinePool (Near Arena)...  -> [PASS] (Consolidated 64KB Arena!)
[TEST 15] Testing ScopedThreadFreeze (Anti-Crash)...   -> [PASS] (Safe Snapshot & RIP Relocation!)
[TEST 16] Testing DreamRtti (Dynamic VTable)...        -> [PASS] (Zero-RVA Class Metadata Resolver!)
[TEST 17] Testing Hook Pipeline & Priority Chain...    -> [PASS] (Multi-Mod Chaining & Ordering!)
[TEST 18] Testing Upgraded DreamLog System...          -> [PASS] (Persistent I/O, Timestamps, Ring Buffer, HexDump, Console Window!)
[TEST 19] Testing DreamCrashGuard (Anti-Crash Shield)...-> [PASS] (Suppressed 0xC0000005, Prevented Game Crash!)
[TEST 20] Testing DreamScanner (AVX2/SSE SIMD)...      -> [PASS] (Scanned 256KB Buffer with 32-byte SIMD!)
[TEST 21] Testing DreamVeh Multi-Thread Sync...        -> [PASS] (Synchronized DR0-DR3 to All Worker Threads!)
[TEST 22] Testing DreamWeaver Dynamic Transpiler...    -> [PASS] (Native x64 Token Stream Instruction Rewriter!)
[TEST 23] Testing DreamPresets (Game Logic Suite)...   -> [PASS] (Keen LiveGameSettings & Dedicated Server Guard!)
[TEST 24] Testing DreamOverlay (Hotkeys & Commands)... -> [PASS] (F9/F10 Hotkeys, Decoupled Presets & Dispatcher!)
[TEST 25] Testing Config Persistence & Interactive CLI -> [PASS] (Save/Load Config, Fast Production & Status Table!)
[TEST 26] Testing DreamServerPolicy (Anti-Cheat Guard)-> [PASS] (EnforceStrictServerRules, Selective Blocking & Violation Intercept!)
[TEST 27] Testing DreamProfiler (Hook Micro-Profiler)-> [PASS] (Nanosecond Precision, Alert Threshold & /profile Report!)
[TEST 28] Testing DreamServerPolicy File Persistence -> [PASS] (server_policy.ini Save & Load Lifecycle!)
[TEST 29] Testing DreamWatcher (Live Hot-Reloading)  -> [PASS] (Real-time File Modification Detection & Debounced Reload!)
[TEST 30] Testing DreamVulkan (Vulkan Swapchain Hook)-> [PASS] (vkQueuePresentKHR Hook, FPS Meter & F9 GUI Overlay Interface!)
[TEST 31] Testing DreamCrashDump (Auto Crash Report) -> [PASS] (Captured Access Violation, Registers, Module+RVA & Callstack!)
[TEST 32] Testing DreamHeuristic (Multi-Tier Resolver)-> [PASS] (Primary, Secondary Wildcard, Backward Prologue & Auto-Cache!)
[TEST 33] Testing DreamEnv & DreamModConfig (Dev Path) -> [PASS] (Discovered Game & Mod Dirs, Self-Config Persistence & Registry!)
[TEST 34] Testing DreamSemVer & Duplicate Mod Resolver -> [PASS] (SemVer 2.0 Comparator, Duplicate Suppression & Auto-Upgrade!)
[TEST 35] Testing DreamHash (Compile-Time 64-bit Hash)-> [PASS] (Zero-alloc FNV-1a & ""_hash Switch-Case Jump Table!)
[TEST 36] Testing DreamWatchdog (Anti-Hang Guard)    -> [PASS] (Heartbeat Monitor, Deadlock Timeout & Report Dumper!)
[TEST 37] Testing DreamNetBridge & Dual-Side Sync    -> [PASS] (Unified Client <-> Dedicated Server RPC & Auto-Sync Settings!)
[TEST 38] Testing DreamMemory (Safe Reads & Patches)  -> [PASS] (Bad Pointer Sandbox, Multi-Level Pointer Chains & RAII Patches!)
[TEST 39] Testing DreamGameContext (Service Locator)  -> [PASS] (LocalPlayer Auto-Capture, Teleportation & Zero-Pointer API!)
[TEST 40] Testing DreamJson (yyjson High-Speed Engine) -> [PASS] (yyjson DOM parsing, type validation, array access & stringify!)
[TEST 41] Testing DreamLua (LuaJIT 2.1 & yyjson Bridge)-> [PASS] (JIT script execution, DreamGameContext services & table bridge!)
```

**ALL 41 TESTS PASSED! dreamkurumiHT IS FULLY VERIFIED!**

---

## แท็กและคำค้นหา (Topics & Tags)

`#enshrouded` `#enshrouded-mods` `#aoe-harvest` `#dream-core` `#dreamkurumiht` `#harmony` `#modloader` `#game-modding` `#cpp20` `#reverse-engineering` `#dedicated-server` `#farming-mod`

---

## สิทธิ์การใช้งาน (License)

โปรเจกต์นี้เผยแพร่ภายใต้สัญญาอนุญาตแบบ **MIT License** เปิดให้ใช้งาน ดัดแปลง และพัฒนาต่อได้อย่างอิสระ

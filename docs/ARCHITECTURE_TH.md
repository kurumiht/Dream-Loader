# รายละเอียดเชิงเทคนิคและสถาปัตยกรรม dreamkurumiHT

เอกสารนี้รวบรวมรายละเอียดโครงสร้างระบบภายใน และชุดทดสอบความถูกต้อง (Test Verification Suite) ของ **dreamkurumiHT**

---

## 1. ภาพรวมของระบบหลัก (Subsystems)

### ระบบ Hooking สไตล์ Harmony (`DreamHarmony`)
- **Prefix / Postfix:** ควบคุมการทำงานของฟังก์ชันได้ง่ายโดยไม่ต้องคำนวณ Opcode ด้วยตัวเอง การคืนค่า `false` ใน `Prefix` จะสั่งข้ามฟังก์ชันเดิมทันที (`SkipOriginal`)
- **PatchAll & UnpatchAll:** ค้นหาและลงทะเบียน Hook อัตโนมัติ พร้อมคืนค่าไบต์เดิมของเกมทั้งหมดเมื่อปิดโปรเซส (RAI Clean Shutdown)
- **Dynamic Register Interception (`DreamMidHook`):** เข้าถึงและแก้ไขค่าในรีจิสเตอร์ CPU (`RAX`..`R15`, `XMM0`..`XMM15`) โดยตรงจากโค้ด C++
- **Hardware Breakpoints (`DreamVeh`):** ทำงานผ่าน Debug Registers ของ CPU (`DR0`..`DR3`) เพื่อดักจับโดยไม่แตะต้องไบต์หน่วยความจำของเกมเลย (Zero-Byte Patching)

### การจัดการหน่วยความจำและ JIT Trampolines
- **Near-Memory Trampoline Arena (`DreamTrampolinePool`):** จอง Trampoline ภายในรัศมี ±2GB ของโค้ดเกม เพื่อประสิทธิภาพสูงสุดของแคช CPU
- **Thread Freezing & RIP Relocation (`ScopedThreadFreeze`):** สั่งหยุดเธรดอื่นชั่วคราวขณะลง Hook และย้ายตำแหน่งคำสั่ง (RIP) หากเธรดกำลังรันอยู่ตรงตำแหน่งที่ถูกแทนที่ ป้องกันแครชจากการแย่งจังหวะ (Race Conditions)
- **HDE64 Instruction Decoder:** ถอดรหัสความยาวคำสั่ง CPU อัตโนมัติเพื่อป้องกันคำสั่งขาด

### ระบบความปลอดภัยและการกู้คืน (Resilience)
- **Kernel CrashGuard (`DreamCrashGuard`):** ดักจับความผิดพลาดทางหน่วยความจำ (0xC0000005) ภายใน Callbacks ไม่ให้ลามไปทำให้ตัวเกมหรือเซิร์ฟเวอร์แครช
- **Lock-Free Circuit Breaker:** นับจำนวนข้อผิดพลาดที่เกิดขึ้นของแต่ละ Hook อัตโนมัติ หากเกินเกณฑ์จะตัดการทำงานของ Hook นั้นเพื่อรักษาเสถียรภาพของเกม
- **Session Epoch & Auto-Resync (`DreamSessionSync`):** ตรวจจับการรีสตาร์ทของ Dedicated Server และเข้าสู่ Safe-Mode อัตโนมัติ เพื่อป้องกันการเรียกใช้ Pointer เดิมที่ถูกทำลายไปแล้ว

### ระบบเครือข่ายและ RPC Plumbing
- **RPC Multiplexer (`DreamRpcMultiplexer`):** ท่อส่งข้อมูลกลางสำหรับส่งแพ็กเก็ตของม็อดผ่านโครงสร้างเน็ตเวิร์กของเกม
- **Rate Limiting & Anti-Spoofing:** ตรวจสอบ Caller ID และจำกัดความถี่ 120 ครั้ง/วินาที พร้อมตรวจสอบว่าแพ็กเก็ตมาจากเซิร์ฟเวอร์จริง
- **Network State Tracker (`DreamNetStateTracker`):** ติดตามสถานะการเชื่อมต่อ GNS, ค่า Ping, และตรวจจับการตัดการเชื่อมต่อ

### คอนฟิกและเครื่องมือ
- **รองรับทั้ง TOML และ INI (`DreamToml`):** อ่านและเขียนไฟล์คอนฟิกพร้อมรองรับการโหลดค่าใหม่แบบสด
- **C-ABI Export Surface (`dreamkurumiHT_plugin.h`):** อินเทอร์เฟซ C-ABI v1 ที่เสถียรสำหรับม็อดภาษา C++, C# (.NET Native AOT), หรือ Lua
- **Zero-Audio Diagnostic HUD:** หน้าต่างวิเคราะห์สถานะ ImGui (ปิดเป็นค่าเริ่มต้นเพื่อไม่ให้รบกวนปุ่มลัดของผู้เล่น)

---

## 2. ชุดทดสอบความถูกต้องอัตโนมัติ (98/98 Green)

ระบบผ่านการตรวจสอบผ่าน Unit Test Suite (`test_runner.cpp`) ครบทุกหมวดหมู่ รวม 98 รายการ โดยได้ผลลัพธ์ผ่าน 100%

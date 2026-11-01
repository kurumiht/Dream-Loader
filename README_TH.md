# dreamkurumiHT Framework (Dream Core)

> **ระบบ Modding Runtime และ Native Harmony Hooking Framework สำหรับ Enshrouded (C++20 / x64 Windows)**  
> **เวอร์ชัน:** `3.9.1` | **สถาปัตยกรรม:** x64 Windows | **รองรับเกม:** Enshrouded 1.0+ (Client และ Dedicated Server)

---

<p align="center">
  <b>Language Switcher / สลับภาษา:</b><br>
  <a href="README.md">🇬🇧 <b>English (Default)</b></a> •
  <a href="README_TH.md">🇹🇭 <b>ภาษาไทย (Thai)</b></a><br><br>
  <a href="https://github.com/kurumiht/Dream-Loader/releases/download/v3.9.1/Dream-Core-v3.9.1.zip">
    <img src="https://img.shields.io/badge/Download-Dream--Core%20v3.9.1%20(Framework)-brightgreen?style=for-the-badge&logo=windows" alt="ดาวน์โหลด Dream-Core Framework" />
  </a>
  &nbsp;&nbsp;
  <a href="https://github.com/kurumiht/Dream-Loader/releases/download/v3.9.1/Dream-AoE-Harvest-v1.5.1.zip">
    <img src="https://img.shields.io/badge/Download-Dream--AoE--Harvest%20v1.5.1%20(Mod)-blue?style=for-the-badge&logo=windows" alt="ดาวน์โหลด Dream-AoE-Harvest Mod" />
  </a>
</p>

---

## ภาพรวมโปรเจกต์

**dreamkurumiHT** เป็นระบบรันไทม์และตัวโหลดม็อด (Mod Loader) ระดับเนทีฟสำหรับเกม **Enshrouded (Keen Holistic Engine)** บนระบบปฏิบัติการ Windows 64-bit ออกแบบมาเพื่อให้นักพัฒนาม็อดสามารถดักจับการทำงานของฟังก์ชันในเกมในรูปแบบ Harmony Hooking และสร้างม็อดแยกอิสระในรูปแบบ C-ABI Plugins ผ่านภาษา C++20, C# (.NET Native AOT), หรือ LuaJIT ได้อย่างปลอดภัยและมีเสถียรภาพ

### คุณสมบัติหลัก
- **Declarative Harmony Hooking:** รองรับการดักฟังก์ชันแบบ `Prefix` และ `Postfix` สามารถแก้ไขค่าพารามิเตอร์หรือผลลัพธ์ของฟังก์ชัน และสั่งข้ามฟังก์ชันเดิม (`SkipOriginal`) ได้
- **Dynamic Register Interception (`DreamMidHook`):** อ่านและปรับแต่งค่ารีจิสเตอร์ของ CPU ได้โดยตรงในโค้ด C++ โดยไม่ต้องเขียนไฟล์ Assembly แยก
- **Near-Memory JIT Trampolines:** จัดสรรหน่วยความจำสำหรับ Trampoline ภายในรัศมีคำสั่งกระโดดของโค้ดเกมอัตโนมัติ เพื่อการทำงานที่รวดเร็วและไม่แครช
- **Thread-Safe Relocation:** หยุดการทำงานของเธรดชั่วคราวขณะลง Hook และย้ายตำแหน่งคำสั่ง (`RIP`) อัตโนมัติหากตรงกับจุดที่มีการแทนที่
- **Session Synchronization (`DreamSessionSync`):** ตรวจจับการรีสตาร์ทของ Dedicated Server เพื่อเข้าสู่ Safe-Mode อัตโนมัติ ป้องกันเกมแครชจากการเรียกใช้ Pointer เดิมที่หมดอายุ
- **ระบบป้องกันการแครช (`DreamCrashGuard`):** มี SEH คอยดักจับข้อผิดพลาดของหน่วยความจำภายใน Callback ป้องกันไม่ให้ตัวเกมหรือเซิร์ฟเวอร์ล้ม
- **ไม่รบกวนปุ่มลัดของผู้เล่น:** ปิดหน้าต่างวิเคราะห์สถานะ (Diagnostic HUD) เป็นค่าเริ่มต้น ทำให้ปุ่ม F9 และปุ่มอื่นๆ ในเกมใช้งานได้ตามปกติ

---

## แพ็กเกจสำหรับดาวน์โหลด

เพื่อความสะดวกและป้องกันความสับสน ไฟล์ดาวน์โหลดจะถูกแยกออกเป็น 2 แพ็กเกจ:
1. **`Dream-Core-v3.9.1.zip`**: ตัวแกนหลักของ Framework (`dreamkurumiHT.dll`), ตัวโหลด Proxy (`dinput8.dll`), ไฟล์ Header C-ABI (`include/`), เทมเพลตเริ่มต้น และเอกสารประกอบ (ไม่มีม็อดเกมรวมอยู่)
2. **`Dream-AoE-Harvest-v1.5.1.zip`**: ม็อดเกี่ยวพืชผลอัตโนมัติรอบตัว (รัศมี 3.5 เมตร, กด F8 เพื่อเปิด/ปิด, กด Shift+E เพื่อเกี่ยวทีละต้นแบบปกติ) ผู้เล่นทุกคนในเซิร์ฟเวอร์ใช้งานได้ทันทีโดยไม่ต้องมีสิทธิ์ Admin

---

## วิธีการติดตั้ง

### สำหรับคนเล่นปกติ (Steam Client)
1. แตกไฟล์ `Dream-Core-v3.9.1.zip` ลงในโฟลเดอร์หลักของเกม Enshrouded (โฟลเดอร์ที่มีไฟล์ `enshrouded.exe`)
2. หากต้องการลงม็อด (เช่น `Dream-AoE-Harvest`) ให้นำไฟล์ของม็อดไปวางในโฟลเดอร์ `mods/`
3. เปิดเข้าเกมผ่าน Steam ตามปกติ

### สำหรับคนเปิด Dedicated Server
1. แตกไฟล์ `Dream-Core-v3.9.1.zip` ลงในโฟลเดอร์ของเซิร์ฟเวอร์ (โฟลเดอร์ที่มีไฟล์ `enshrouded_server.exe`)
2. นำม็อดที่ต้องการไปวางไว้ในโฟลเดอร์ `mods/`
3. เปิดรัน `enshrouded_server.exe`

---

## สารบัญเอกสารประกอบ

เพื่อให้หน้านี้กระชับ รายละเอียดเชิงลึกจึงถูกแยกไว้ในเอกสารเฉพาะทาง:

- **[รายละเอียดเชิงเทคนิคและสถาปัตยกรรม](docs/ARCHITECTURE_TH.md)**: อธิบายโครงสร้างระบบภายใน การจัดการ Trampoline และชุดทดสอบ Unit Test 98 รายการ
- **[คู่มือการพัฒนาม็อด (ภาษาไทย)](DEVELOPER_GUIDE.md)**: แนะนำขั้นตอนการสร้างม็อด C++ และ C# ผ่าน C-ABI Plugin
- **[คู่มือการพัฒนาม็อด (English)](DEVELOPER_GUIDE_EN.md)**: Developer documentation in English
- **[คู่มือการอัปเดตเกม](GAME_UPDATE_GUIDE.md)**: บันทึก RVA, AOB Patterns และขั้นตอนการตรวจเช็กเมื่อเกมมีอัปเดตใหม่
- **[บันทึกการเปลี่ยนแปลง (Changelog)](CHANGELOG.md)**: ประวัติการแก้ไข ปรับปรุง และฟีเจอร์ในแต่ละเวอร์ชัน

---

## ลิขสิทธิ์

โปรเจกต์นี้เผยแพร่ภายใต้สัญญาอนุญาตแบบ **MIT License**

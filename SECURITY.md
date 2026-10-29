# Security Policy

## 🛡️ Supported Versions (เวอร์ชันที่ได้รับการสนับสนุน)

We actively provide security updates and patches for the current major release line of **dreamkurumiHT** / **Dream-Loader**.

| Version Line | Supported          | Status                                      |
| :---         | :---:              | :---                                        |
| **v3.8.x**   | :white_check_mark: | **Current Active Release** (Full Support)   |
| **v3.7.x**   | :x:                | Deprecated (Please upgrade to v3.8.x)       |
| **< v3.7.0** | :x:                | End of Life                                 |

---

## 🔒 Reporting a Vulnerability (การรายงานช่องโหว่ด้านความปลอดภัย)

We take the security and integrity of player environments and dedicated servers seriously. If you discover a security vulnerability or exploit in **dreamkurumiHT** or official Dream plugins, **please do NOT report it via public GitHub issues or public forums**.

### Preferred Reporting Method (วิธีที่แนะนำ):
Please submit reports privately via **GitHub Private Vulnerability Reporting**:
👉 **[Report a Vulnerability](https://github.com/kurumiht/Dream-Loader/security/advisories/new)**

### What to Include in Your Report (ข้อมูลที่ควรระบุ):
1. **Description**: A clear description of the vulnerability and its potential impact.
2. **Reproduction Steps**: Step-by-step instructions or Proof of Concept (PoC) to reproduce the issue safely.
3. **Affected Environment**: Client (`enshrouded.exe`) vs Dedicated Server (`enshrouded_server.exe`), OS version, and mod version.
4. **Proposed Fix / Remediation** *(Optional)*: If you have already identified a potential code fix.

### Response Timeline (ระยะเวลาดำเนินการ):
- **Initial Response & Acknowledgment**: Within **48 hours**.
- **Assessment & Triage**: Within **5 business days**.
- **Fix & Patch Release**: Critical vulnerabilities will be prioritized and released as a hotfix accompanied by a security advisory.

---

## 🎯 Scope (ขอบเขตความปลอดภัย)

### ✅ In Scope (อยู่ในขอบเขตที่รับรายงาน):
- **Remote Code Execution (RCE)** or arbitrary code injection via network packet parsing or NetBridge channels.
- **Denial of Service (DoS) / Server Crashers**: Unauthenticated network packets that crash or stall `enshrouded_server.exe`.
- **Arbitrary File Access**: Directory traversal or unauthorized reading/writing outside the server/game directory.
- **Privilege Escalation**: Flaws allowing normal connected players to bypass server admin authentication or gain host system access.

### ❌ Out of Scope (อยู่นอกขอบเขตความปลอดภัย):
- **In-Game Modding Mechanics**: Client-side memory hooking, AoE harvest, auto-pickup, or game presets (these are intended mod functionalities).
- **Third-Party Mod Conflicts**: Crashes or incompatibilities caused by third-party loaders or mods not maintained in this repository.
- **Physical / Local Admin Access**: Attacks requiring root/admin access to the host machine or pre-existing local malware.
- **Game Engine Bugs**: Bugs native to vanilla Enshrouded that exist independently of dreamkurumiHT.

---

## 💡 Best Practices for Users & Server Admins

1. **Official Sources Only**: Always download official binaries directly from the official repository: [kurumiht/Dream-Loader Releases](https://github.com/kurumiht/Dream-Loader/releases).
2. **Least Privilege**: Run dedicated game servers under dedicated standard user accounts rather than Administrator/root privileges.
3. **Firewall Protection**: Only expose required game ports (default UDP 15636-15637) to the public internet. Keep internal administration ports local.

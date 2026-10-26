#pragma once
#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include "DreamLog.hpp"
#include "DreamCore.hpp"
#include "DreamCrashGuard.hpp"

namespace dreamkurumi {

// ============================================================================
// MSVC 64-bit RTTI Structures
// ============================================================================
#pragma pack(push, 4)
struct MSVC_RTTICompleteObjectLocator {
    uint32_t signature;           // 1 for 64-bit
    uint32_t offset;              // Offset of this vtable in the complete class (0 for primary)
    uint32_t cdOffset;            // Constructor displacement offset
    int32_t  typeDescriptorRva;   // RVA from image base to TypeDescriptor
    int32_t  classDescriptorRva;  // RVA to RTTIClassHierarchyDescriptor
    int32_t  objectBaseRva;       // RVA to object base
};
#pragma pack(pop)

struct MSVC_TypeDescriptor {
    const void* pVFTable;         // Pointer to type_info vftable
    void* spare;                  // Reserved runtime data
    char name[1];                 // Mangled class name (e.g., ".?AVEntityManager@ecs@keen@@")
};

// ============================================================================
// Dynamic MSVC RTTI & VTable Resolver (Zero-RVA, Zero-Scan from Engine Metadata)
// ============================================================================
class DreamRtti {
public:
    static DreamRtti& Instance() {
        static DreamRtti s_instance;
        return s_instance;
    }

    // Demangle MSVC class symbol (e.g. ".?AVPlayerCharacter@@" -> "PlayerCharacter")
    static std::string DemangleMangledName(const char* raw) {
        if (!raw) return "";
        if (std::strncmp(raw, ".?AV", 4) == 0) raw += 4;
        else if (std::strncmp(raw, ".?AU", 4) == 0) raw += 4;

        std::string demangled = raw;
        if (demangled.ends_with("@@")) {
            demangled = demangled.substr(0, demangled.size() - 2);
        }

        // MSVC namespaces are ordered: Class@Namespace2@Namespace1
        std::vector<std::string> parts;
        size_t start = 0;
        while (start < demangled.size()) {
            size_t at = demangled.find('@', start);
            if (at == std::string::npos) {
                parts.push_back(demangled.substr(start));
                break;
            }
            parts.push_back(demangled.substr(start, at - start));
            start = at + 1;
        }

        if (parts.size() > 1) {
            std::string formatted;
            for (int i = static_cast<int>(parts.size()) - 1; i >= 0; --i) {
                if (!parts[i].empty()) {
                    if (!formatted.empty()) formatted += "::";
                    formatted += parts[i];
                }
            }
            return formatted;
        }
        return demangled;
    }

    // Inspect arbitrary pointer: resolves MSVC RTTI Complete Object Locator -> Class Name!
    static std::string GetClassName(const void* pObject) {
        if (!pObject || !MemoryHelper::IsValidPtr(pObject)) return "";

        std::string result;
        DreamCrashGuard::ExecuteSafely("RttiGetClassName", [&]() {
            void** vtable = *reinterpret_cast<void** const*>(pObject);
            if (!MemoryHelper::IsValidPtr(vtable)) return;

            auto* col = reinterpret_cast<const MSVC_RTTICompleteObjectLocator*>(vtable[-1]);
            if (!MemoryHelper::IsValidPtr(col)) return;

            // MSVC 64-bit signature must be 1
            if (col->signature != 1) return;

            HMODULE hMod = nullptr;
            if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   reinterpret_cast<LPCSTR>(col), &hMod) || !hMod) {
                return;
            }
            uintptr_t base = reinterpret_cast<uintptr_t>(hMod);

            uintptr_t tdAddr = base + col->typeDescriptorRva;
            if (!MemoryHelper::IsValidPtr(reinterpret_cast<const void*>(tdAddr))) return;

            auto* td = reinterpret_cast<const MSVC_TypeDescriptor*>(tdAddr);
            if (!td->name || td->name[0] != '.' || td->name[1] != '?') return;

            result = DemangleMangledName(td->name);
        });
        return result;
    }

    // Register synthetic/manual VTable (useful for testing or non-MSVC host environments)
    static void RegisterSynthetic(const std::string& className, void** vtable) {
        std::lock_guard<std::mutex> lock(Instance().m_mutex);
        Instance().m_syntheticVtables[className] = vtable;
        DREAM_LOG_INFO("DreamRtti: Registered synthetic VTable for '%s' at 0x%p", className.c_str(), vtable);
    }

    // Find class VTable pointer dynamically by demangled or partial class name
    // e.g., FindVTable("keen::ecs::EntityManager") or FindVTable("EntityManager")
    static void** FindVTable(const std::string& className, HMODULE hModule = nullptr) {
        // 1. Check synthetic registry first
        {
            std::lock_guard<std::mutex> lock(Instance().m_mutex);
            auto it = Instance().m_syntheticVtables.find(className);
            if (it != Instance().m_syntheticVtables.end()) {
                return it->second;
            }
        }

        // 2. Scan PE Image RTTI Metadata
        if (!hModule) {
            hModule = GetModuleHandleA(nullptr);
        }
        if (!hModule) return nullptr;

        uint8_t* base = reinterpret_cast<uint8_t*>(hModule);
        auto dos = reinterpret_cast<PIMAGE_DOS_HEADER>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;

        auto nt = reinterpret_cast<PIMAGE_NT_HEADERS>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;

        // Locate .rdata and .data sections
        auto section = IMAGE_FIRST_SECTION(nt);
        uint8_t* rdataStart = nullptr;
        size_t rdataSize = 0;
        uint8_t* dataStart = nullptr;
        size_t dataSize = 0;

        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
            char secName[9] = {0};
            std::memcpy(secName, section->Name, 8);
            if (std::strcmp(secName, ".rdata") == 0) {
                rdataStart = base + section->VirtualAddress;
                rdataSize = section->Misc.VirtualSize;
            } else if (std::strcmp(secName, ".data") == 0) {
                dataStart = base + section->VirtualAddress;
                dataSize = section->Misc.VirtualSize;
            }
        }

        if (!rdataStart || rdataSize == 0) {
            DREAM_LOG_WARN("DreamRtti: .rdata section not found in module 0x%p", hModule);
            return nullptr;
        }

        // 3. Search for TypeDescriptor in .data and .rdata
        std::vector<std::pair<uint8_t*, size_t>> scanRegions;
        if (dataStart && dataSize > 0) scanRegions.push_back({dataStart, dataSize});
        scanRegions.push_back({rdataStart, rdataSize});

        uintptr_t typeDescriptorAddr = 0;
        for (const auto& [regionStart, regionLen] : scanRegions) {
            if (regionLen < sizeof(MSVC_TypeDescriptor) + 4) continue;
            for (size_t offset = 0; offset <= regionLen - 16; ++offset) {
                uint8_t* cur = regionStart + offset;
                // TypeDescriptor names begin with ".?AV" (class) or ".?AU" (struct)
                if (cur[0] == '.' && cur[1] == '?' && (cur[2] == 'A') && (cur[3] == 'V' || cur[3] == 'U')) {
                    const char* nameStr = reinterpret_cast<const char*>(cur);
                    if (std::strstr(nameStr, className.c_str()) != nullptr) {
                        // Offset to start of TypeDescriptor is 2 pointers before name (pVFTable, spare)
                        typeDescriptorAddr = reinterpret_cast<uintptr_t>(cur) - (sizeof(void*) * 2);
                        DREAM_LOG_INFO("DreamRtti: Found TypeDescriptor for '%s' ('%s') at 0x%p",
                                       className.c_str(), nameStr, reinterpret_cast<void*>(typeDescriptorAddr));
                        break;
                    }
                }
            }
            if (typeDescriptorAddr != 0) break;
        }

        if (typeDescriptorAddr == 0) {
            DREAM_LOG_WARN("DreamRtti: Could not find TypeDescriptor for class '%s'", className.c_str());
            return nullptr;
        }

        int32_t targetTdRva = static_cast<int32_t>(typeDescriptorAddr - reinterpret_cast<uintptr_t>(base));

        // 4. Search .rdata for MSVC_RTTICompleteObjectLocator
        uintptr_t locatorAddr = 0;
        for (size_t offset = 0; offset <= rdataSize - sizeof(MSVC_RTTICompleteObjectLocator); offset += 4) {
            auto loc = reinterpret_cast<MSVC_RTTICompleteObjectLocator*>(rdataStart + offset);
            if (loc->signature == 1 && loc->offset == 0 && loc->typeDescriptorRva == targetTdRva) {
                locatorAddr = reinterpret_cast<uintptr_t>(loc);
                DREAM_LOG_INFO("DreamRtti: Found RTTICompleteObjectLocator at 0x%p", loc);
                break;
            }
        }

        if (locatorAddr == 0) {
            DREAM_LOG_WARN("DreamRtti: Could not find RTTICompleteObjectLocator for RVA 0x%x", targetTdRva);
            return nullptr;
        }

        // 5. Search .rdata for pointer pointing to locatorAddr
        // The VTable immediately succeeds this pointer: vtable[-1] == &locator
        for (size_t offset = 0; offset <= rdataSize - sizeof(void*); offset += sizeof(void*)) {
            uintptr_t* ptr = reinterpret_cast<uintptr_t*>(rdataStart + offset);
            if (*ptr == locatorAddr) {
                void** vtable = reinterpret_cast<void**>(ptr + 1);
                DREAM_LOG_INFO("DreamRtti: Resolved VTable for class '%s' at 0x%p", className.c_str(), vtable);
                return vtable;
            }
        }

        DREAM_LOG_WARN("DreamRtti: Could not find VTable reference pointing to locator 0x%p", reinterpret_cast<void*>(locatorAddr));
        return nullptr;
    }

    // Get virtual method pointer at specific VTable index
    static void* GetVirtualMethod(void** vtable, size_t index) {
        if (!vtable) return nullptr;
        return vtable[index];
    }

private:
    DreamRtti() = default;

    std::mutex m_mutex;
    std::unordered_map<std::string, void**> m_syntheticVtables;
};

} // namespace dreamkurumi

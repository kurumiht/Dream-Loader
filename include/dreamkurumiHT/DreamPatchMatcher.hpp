#pragma once

// ============================================================================
// DreamPatchMatcher.hpp - Game Update Binary Diff Auto-Resolver & Offset Matcher
//
// Part of the dreamkurumiHT Framework (Item 11: Game Update Binary Diff Auto-Resolver)
// Features:
// - Multi-stage heuristic resolution: Exact Pattern -> Fuzzy Opcode -> String XRef Proximity
// - Backtraces function prologues (push rbp, sub rsp, XX) from string references
// - Automatically updates shifted RVAs across game patches (e.g. 1.0.0 -> 1.0.1)
// - Computes match confidence score (0.0 to 1.0) and generates JSON migration reports
// ============================================================================

#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <sstream>
#include <iomanip>
#include <fstream>
#include "DreamLog.hpp"
#include "DreamCrashGuard.hpp"
#include "DreamScanner.hpp"
#include "DreamFuzzyScanner.hpp"

namespace dreamkurumi {

enum class MatchResolutionStatus : uint8_t {
    Unresolved = 0,
    MatchedExact = 1,
    MatchedFuzzy = 2,
    ResolvedViaStringXRef = 3
};

struct PatchSignatureProfile {
    std::string name;
    uintptr_t originalRva{0};
    std::string idaPattern;
    std::string fuzzyPattern;
    std::string referencedString;
    size_t expectedSize{32};
};

struct MatchResolutionResult {
    std::string name;
    uintptr_t oldRva{0};
    uintptr_t newRva{0};
    uintptr_t newAbsoluteAddress{0};
    int64_t rvaShift{0};
    float confidence{0.0f};
    MatchResolutionStatus status{MatchResolutionStatus::Unresolved};

    const char* GetStatusString() const {
        switch (status) {
            case MatchResolutionStatus::MatchedExact: return "MATCHED_EXACT";
            case MatchResolutionStatus::MatchedFuzzy: return "MATCHED_FUZZY";
            case MatchResolutionStatus::ResolvedViaStringXRef: return "RESOLVED_STRING_XREF";
            default: return "UNRESOLVED";
        }
    }
};

class DreamPatchMatcher {
public:
    static DreamPatchMatcher& Instance() {
        static DreamPatchMatcher s_instance;
        return s_instance;
    }

    // Register a known function signature profile to monitor across patches
    void RegisterProfile(const PatchSignatureProfile& profile) {
        m_profiles[profile.name] = profile;
    }

    void SetConfidenceThreshold(float threshold) { m_confidenceThreshold = threshold; }
    float GetConfidenceThreshold() const { return m_confidenceThreshold; }
    bool IsConfident(const MatchResolutionResult& res) const { return res.confidence >= m_confidenceThreshold; }

    // Resolve a specific profile against the target module
    MatchResolutionResult ResolveProfile(const PatchSignatureProfile& profile, HMODULE hMod = NULL) {
        if (!hMod) hMod = GetModuleHandleA(NULL);
        uintptr_t base = reinterpret_cast<uintptr_t>(hMod);

        MatchResolutionResult res;
        res.name = profile.name;
        res.oldRva = profile.originalRva;

        // Stage 1: Exact Pattern Match
        if (!profile.idaPattern.empty()) {
            uintptr_t exactAddr = reinterpret_cast<uintptr_t>(DreamScanner::FindPattern(profile.idaPattern.c_str(), hMod));
            if (exactAddr != 0) {
                res.newAbsoluteAddress = exactAddr;
                res.newRva = exactAddr - base;
                res.rvaShift = static_cast<int64_t>(res.newRva) - static_cast<int64_t>(res.oldRva);
                res.confidence = 1.0f;
                res.status = MatchResolutionStatus::MatchedExact;
                DREAM_LOG_INFO("DreamPatchMatcher: '%s' resolved via EXACT pattern at 0x%p (RVA: 0x%llX)",
                               profile.name.c_str(), reinterpret_cast<void*>(exactAddr),
                               static_cast<unsigned long long>(res.newRva));
                return res;
            }
        }

        // Stage 2: Fuzzy Instruction-Aware Pattern Match
        if (!profile.fuzzyPattern.empty()) {
            uintptr_t fuzzyAddr = reinterpret_cast<uintptr_t>(DreamScanner::FindPattern(profile.fuzzyPattern.c_str(), hMod));
            if (fuzzyAddr != 0) {
                res.newAbsoluteAddress = fuzzyAddr;
                res.newRva = fuzzyAddr - base;
                res.rvaShift = static_cast<int64_t>(res.newRva) - static_cast<int64_t>(res.oldRva);
                res.confidence = 0.90f;
                res.status = MatchResolutionStatus::MatchedFuzzy;
                DREAM_LOG_INFO("DreamPatchMatcher: '%s' resolved via FUZZY pattern at 0x%p (RVA: 0x%llX, Shift: %+lld)",
                               profile.name.c_str(), reinterpret_cast<void*>(fuzzyAddr),
                               static_cast<unsigned long long>(res.newRva),
                               static_cast<long long>(res.rvaShift));
                return res;
            }
        }

        // Stage 3: String Cross-Reference Proximity Resolution
        if (!profile.referencedString.empty()) {
            uintptr_t xrefFunc = ResolveViaStringXRef(profile.referencedString, hMod);
            if (xrefFunc != 0) {
                res.newAbsoluteAddress = xrefFunc;
                res.newRva = xrefFunc - base;
                res.rvaShift = static_cast<int64_t>(res.newRva) - static_cast<int64_t>(res.oldRva);
                res.confidence = 0.75f;
                res.status = MatchResolutionStatus::ResolvedViaStringXRef;
                DREAM_LOG_INFO("DreamPatchMatcher: '%s' resolved via STRING XREF ('%s') at 0x%p (RVA: 0x%llX)",
                               profile.name.c_str(), profile.referencedString.c_str(),
                               reinterpret_cast<void*>(xrefFunc),
                               static_cast<unsigned long long>(res.newRva));
                return res;
            }
        }

        res.status = MatchResolutionStatus::Unresolved;
        res.confidence = 0.0f;
        DREAM_LOG_WARN("DreamPatchMatcher: Failed to resolve function '%s' across update", profile.name.c_str());
        return res;
    }

    // Resolve all registered profiles and generate an auto-migration report
    std::vector<MatchResolutionResult> ResolveAll(HMODULE hMod = NULL) {
        std::vector<MatchResolutionResult> results;
        for (const auto& [_, prof] : m_profiles) {
            results.push_back(ResolveProfile(prof, hMod));
        }
        return results;
    }

    // Export migration report to JSON file
    bool ExportReportJson(const std::string& filePath, const std::vector<MatchResolutionResult>& results) {
        std::ofstream out(filePath);
        if (!out.is_open()) return false;

        out << "{\n  \"timestamp\": " << GetTickCount64() << ",\n  \"matches\": [\n";
        for (size_t i = 0; i < results.size(); ++i) {
            const auto& r = results[i];
            out << "    {\n"
                << "      \"name\": \"" << r.name << "\",\n"
                << "      \"status\": \"" << r.GetStatusString() << "\",\n"
                << "      \"oldRva\": \"0x" << std::hex << r.oldRva << "\",\n"
                << "      \"newRva\": \"0x" << r.newRva << "\",\n"
                << "      \"rvaShift\": " << std::dec << r.rvaShift << ",\n"
                << "      \"confidence\": " << r.confidence << "\n"
                << "    }" << (i + 1 < results.size() ? "," : "") << "\n";
        }
        out << "  ]\n}\n";
        out.close();
        DREAM_LOG_INFO("DreamPatchMatcher: Exported migration report to '%s'", filePath.c_str());
        return true;
    }

private:
    DreamPatchMatcher() = default;

    // Locate string in memory, find referencing instructions (LEA rcx, [rip+offset]), and scan backward for prologue
    uintptr_t ResolveViaStringXRef(const std::string& str, HMODULE hMod) {
        uintptr_t base = reinterpret_cast<uintptr_t>(hMod);
        auto* dos = reinterpret_cast<PIMAGE_DOS_HEADER>(base);
        auto* nt = reinterpret_cast<PIMAGE_NT_HEADERS>(base + dos->e_lfanew);

        // 1. Find string literal bytes
        std::vector<uint8_t> strBytes(str.begin(), str.end());
        strBytes.push_back(0); // null terminator

        auto* textSec = IMAGE_FIRST_SECTION(nt);
        uintptr_t strAddr = 0;

        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
            auto* sec = &textSec[i];
            const char* secName = reinterpret_cast<const char*>(sec->Name);
            if (strncmp(secName, ".rdata", 6) == 0 || strncmp(secName, ".data", 5) == 0) {
                const auto* pStart = reinterpret_cast<const uint8_t*>(base + sec->VirtualAddress);
                size_t secSize = sec->Misc.VirtualSize;
                for (size_t o = 0; o + strBytes.size() <= secSize; ++o) {
                    if (std::memcmp(pStart + o, strBytes.data(), strBytes.size()) == 0) {
                        strAddr = base + sec->VirtualAddress + o;
                        break;
                    }
                }
            }
            if (strAddr != 0) break;
        }

        if (strAddr == 0) return 0;

        // 2. Scan .text section for LEA RDX/RCX/R8, [RIP + disp32] referencing strAddr
        uintptr_t textBase = base + textSec[0].VirtualAddress;
        size_t textSize = textSec[0].Misc.VirtualSize;
        const auto* code = reinterpret_cast<const uint8_t*>(textBase);

        uintptr_t xrefInstruction = 0;
        for (size_t o = 0; o + 7 <= textSize; ++o) {
            // 48 8D 0D / 48 8D 15 (LEA rcx/rdx, [rip+disp32])
            if (code[o] == 0x48 && code[o+1] == 0x8D && (code[o+2] == 0x0D || code[o+2] == 0x15 || code[o+2] == 0x05)) {
                int32_t disp = *reinterpret_cast<const int32_t*>(&code[o+3]);
                uintptr_t target = textBase + o + 7 + disp;
                if (target == strAddr) {
                    xrefInstruction = textBase + o;
                    break;
                }
            }
        }

        if (xrefInstruction == 0) return 0;

        // 3. Primary Resolution: Windows x64 .pdata (RUNTIME_FUNCTION) Lookup
        // As recommended by Senior Dev: RtlLookupFunctionEntry yields exact function BeginAddress
        DWORD64 imageBase = 0;
        UNWIND_HISTORY_TABLE historyTable = {0};
        auto* pRuntimeFunc = RtlLookupFunctionEntry(static_cast<DWORD64>(xrefInstruction), &imageBase, &historyTable);
        if (pRuntimeFunc && imageBase != 0) {
            uintptr_t funcStart = static_cast<uintptr_t>(imageBase + pRuntimeFunc->BeginAddress);
            DREAM_LOG_DEBUG("DreamPatchMatcher: Resolved exact function start via .pdata at 0x%p (BeginRVA: 0x%X)",
                            reinterpret_cast<void*>(funcStart), pRuntimeFunc->BeginAddress);
            return funcStart;
        }

        // 4. Heuristic Fallback: Scan backwards up to 512 bytes for MSVC prologue
        for (size_t back = 1; back < 512 && (xrefInstruction - back) >= textBase; ++back) {
            const auto* p = reinterpret_cast<const uint8_t*>(xrefInstruction - back);
            // Check for CC / 90 alignment padding before prologue
            if (p[-1] == 0xCC || p[-1] == 0x90) {
                if ((p[0] == 0x48 && p[1] == 0x83 && p[2] == 0xEC) || // sub rsp, XX
                    (p[0] == 0x48 && p[1] == 0x89 && p[2] == 0x5C) || // mov [rsp+XX], rbx
                    (p[0] == 0x40 && p[1] == 0x53) ||                 // push rbx
                    (p[0] == 0x55 && p[1] == 0x48 && p[2] == 0x89)) { // push rbp; mov rbp, rsp
                    return xrefInstruction - back;
                }
            }
        }

        return xrefInstruction; // Fallback to xref location
    }

    float m_confidenceThreshold{0.80f};
    std::unordered_map<std::string, PatchSignatureProfile> m_profiles;
};

} // namespace dreamkurumi

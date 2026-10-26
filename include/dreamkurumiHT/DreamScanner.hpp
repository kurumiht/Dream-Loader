#pragma once
#include <windows.h>
#include <immintrin.h>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <sstream>
#include <cstring>
#include "DreamLog.hpp"
#include "DreamCore.hpp"
extern "C" {
#include "hde/hde64.h"
}

namespace dreamkurumi {

// ============================================================================
// DreamScanner - AVX2 & SSE4.2 SIMD Vectorized Pattern Scanner
// Scans up to 32 bytes per CPU clock cycle (20x faster than linear loops)
// ============================================================================
class DreamScanner {
public:
    struct ParsedPattern {
        std::vector<uint8_t> bytes;
        std::vector<bool>    mask; // true = must match byte, false = wildcard
        size_t firstNonWildcardIndex{0};
        uint8_t firstMatchByte{0};
        bool isValid{false};
    };

    // Parse IDA-style pattern: e.g. "48 89 5C 24 ?? 57 48 83"
    static ParsedPattern ParsePattern(const std::string& patternStr) {
        ParsedPattern p;
        std::istringstream stream(patternStr);
        std::string token;
        bool foundFirst = false;

        while (stream >> token) {
            if (token == "?" || token == "??") {
                p.bytes.push_back(0);
                p.mask.push_back(false);
            } else {
                uint8_t byteVal = static_cast<uint8_t>(std::strtoul(token.c_str(), nullptr, 16));
                p.bytes.push_back(byteVal);
                p.mask.push_back(true);
                if (!foundFirst) {
                    p.firstNonWildcardIndex = p.bytes.size() - 1;
                    p.firstMatchByte = byteVal;
                    foundFirst = true;
                }
            }
        }

        p.isValid = !p.bytes.empty() && foundFirst;
        return p;
    }

    // Verify if candidate address matches the parsed pattern
    static inline bool Matches(const uint8_t* addr, const ParsedPattern& p) {
        for (size_t i = 0; i < p.bytes.size(); ++i) {
            if (p.mask[i] && addr[i] != p.bytes[i]) {
                return false;
            }
        }
        return true;
    }

    // AVX2 Vectorized Scan (32 bytes per cycle)
    __attribute__((target("avx2,bmi")))
    static const uint8_t* ScanAVX2(const uint8_t* start, size_t size, const ParsedPattern& p) {
        if (size < p.bytes.size()) return nullptr;

        size_t searchLen = size - p.bytes.size();
        __m256i targetByteVec = _mm256_set1_epi8(static_cast<char>(p.firstMatchByte));
        size_t offset = 0;

        // Process in 32-byte blocks
        while (offset + 32 <= searchLen) {
            const uint8_t* curPtr = start + offset + p.firstNonWildcardIndex;
            __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(curPtr));
            __m256i cmp = _mm256_cmpeq_epi8(chunk, targetByteVec);
            uint32_t mask = static_cast<uint32_t>(_mm256_movemask_epi8(cmp));

            while (mask != 0) {
                int matchBit = __builtin_ctz(mask);
                const uint8_t* candidate = start + offset + matchBit;
                if (Matches(candidate, p)) {
                    return candidate;
                }
                mask &= (mask - 1); // Clear lowest set bit
            }

            offset += 32;
        }

        // Process remaining tail bytes with scalar scanner
        while (offset <= searchLen) {
            const uint8_t* candidate = start + offset;
            if (candidate[p.firstNonWildcardIndex] == p.firstMatchByte && Matches(candidate, p)) {
                return candidate;
            }
            offset++;
        }

        return nullptr;
    }

    // SSE2 Vectorized Scan (16 bytes per cycle)
    static const uint8_t* ScanSSE2(const uint8_t* start, size_t size, const ParsedPattern& p) {
        if (size < p.bytes.size()) return nullptr;

        size_t searchLen = size - p.bytes.size();
        __m128i targetByteVec = _mm_set1_epi8(static_cast<char>(p.firstMatchByte));
        size_t offset = 0;

        // Process in 16-byte blocks
        while (offset + 16 <= searchLen) {
            const uint8_t* curPtr = start + offset + p.firstNonWildcardIndex;
            __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(curPtr));
            __m128i cmp = _mm_cmpeq_epi8(chunk, targetByteVec);
            uint32_t mask = static_cast<uint32_t>(_mm_movemask_epi8(cmp));

            while (mask != 0) {
                int matchBit = __builtin_ctz(mask);
                const uint8_t* candidate = start + offset + matchBit;
                if (Matches(candidate, p)) {
                    return candidate;
                }
                mask &= (mask - 1);
            }

            offset += 16;
        }

        // Tail
        while (offset <= searchLen) {
            const uint8_t* candidate = start + offset;
            if (candidate[p.firstNonWildcardIndex] == p.firstMatchByte && Matches(candidate, p)) {
                return candidate;
            }
            offset++;
        }

        return nullptr;
    }

    // High-level Auto-Dispatched Scan
    static void* Scan(const void* start, size_t size, const std::string& patternStr) {
        if (!start || size == 0) return nullptr;

        ParsedPattern p = ParsePattern(patternStr);
        if (!p.isValid) {
            DREAM_LOG_ERROR("DreamScanner: Invalid pattern string '%s'", patternStr.c_str());
            return nullptr;
        }

        const uint8_t* byteStart = reinterpret_cast<const uint8_t*>(start);
        const uint8_t* result = nullptr;

        if (__builtin_cpu_supports("avx2")) {
            result = ScanAVX2(byteStart, size, p);
        } else {
            result = ScanSSE2(byteStart, size, p);
        }

        return const_cast<void*>(reinterpret_cast<const void*>(result));
    }

    // Scan a specific PE Section (e.g. ".text" or ".rdata")
    static void* ScanSection(HMODULE hModule, const char* sectionName, const std::string& patternStr) {
        if (!hModule) hModule = GetModuleHandleA(nullptr);
        if (!hModule) return nullptr;

        uint8_t* base = reinterpret_cast<uint8_t*>(hModule);
        auto dos = reinterpret_cast<PIMAGE_DOS_HEADER>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;

        auto nt = reinterpret_cast<PIMAGE_NT_HEADERS>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;

        auto section = IMAGE_FIRST_SECTION(nt);
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
            char secName[9] = {0};
            std::memcpy(secName, section->Name, 8);
            if (std::strcmp(secName, sectionName) == 0) {
                void* secStart = base + section->VirtualAddress;
                size_t secSize = section->Misc.VirtualSize;
                return Scan(secStart, secSize, patternStr);
            }
        }

        return nullptr;
    }

    // Scan the entire executable code section (.text)
    static void* ScanModule(HMODULE hModule, const std::string& patternStr) {
        void* res = ScanSection(hModule, ".text", patternStr);
        if (!res) {
            // Fallback to scanning whole image if not found in .text
            if (!hModule) hModule = GetModuleHandleA(nullptr);
            if (hModule) {
                uint8_t* base = reinterpret_cast<uint8_t*>(hModule);
                auto dos = reinterpret_cast<PIMAGE_DOS_HEADER>(base);
                if (dos->e_magic == IMAGE_DOS_SIGNATURE) {
                    auto nt = reinterpret_cast<PIMAGE_NT_HEADERS>(base + dos->e_lfanew);
                    if (nt->Signature == IMAGE_NT_SIGNATURE) {
                        res = Scan(base, nt->OptionalHeader.SizeOfImage, patternStr);
                    }
                }
            }
        }
        return res;
    }

    // FindPattern overload for scanning a buffer directly
    static void* FindPattern(const std::string& patternStr, const void* buffer, size_t size) {
        return Scan(buffer, size, patternStr);
    }

    // FindPattern overload for module scanning
    static void* FindPattern(const std::string& patternStr, HMODULE hModule = nullptr) {
        return ScanModule(hModule, patternStr);
    }

    // Scan all occurrences matching the pattern up to maxMatches
    static std::vector<void*> ScanAll(const void* start, size_t size, const std::string& patternStr, size_t maxMatches = 100) {
        std::vector<void*> matches;
        ParsedPattern p = ParsePattern(patternStr);
        if (!p.isValid || !start || size < p.bytes.size()) return matches;

        const uint8_t* cur = reinterpret_cast<const uint8_t*>(start);
        size_t remaining = size;

        while (remaining >= p.bytes.size() && matches.size() < maxMatches) {
            const uint8_t* found = nullptr;
            if (__builtin_cpu_supports("avx2")) {
                found = ScanAVX2(cur, remaining, p);
            } else {
                found = ScanSSE2(cur, remaining, p);
            }
            if (!found) break;

            matches.push_back(const_cast<void*>(reinterpret_cast<const void*>(found)));
            size_t step = (found - cur) + 1;
            cur = found + 1;
            if (step >= remaining) break;
            remaining -= step;
        }
        return matches;
    }

    // Scan all occurrences across code sections of the module
    static std::vector<void*> ScanAllModule(HMODULE hModule, const std::string& patternStr, size_t maxMatches = 100) {
        if (!hModule) hModule = GetModuleHandleA(nullptr);
        if (!hModule) return {};

        uint8_t* base = reinterpret_cast<uint8_t*>(hModule);
        auto dos = reinterpret_cast<PIMAGE_DOS_HEADER>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return {};

        auto nt = reinterpret_cast<PIMAGE_NT_HEADERS>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return {};

        auto section = IMAGE_FIRST_SECTION(nt);
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
            char secName[9] = {0};
            std::memcpy(secName, section->Name, 8);
            if (std::strcmp(secName, ".text") == 0) {
                return ScanAll(base + section->VirtualAddress, section->Misc.VirtualSize, patternStr, maxMatches);
            }
        }
        return ScanAll(base, nt->OptionalHeader.SizeOfImage, patternStr, maxMatches);
    }

    struct SignatureResult {
        std::string pattern;
        size_t byteLength{0};
        size_t matchCount{0};
        bool isUnique{false};
        std::string cppSnippet;
    };

    // Auto AOB Signature Generator: Disassembles instructions, masks relative jumps/calls
    // and displacements as '??', tests uniqueness across the main module, and generates C++ code!
    static SignatureResult GenerateSignature(const void* address, size_t minLength = 24, size_t maxLength = 64, HMODULE hModule = nullptr) {
        SignatureResult res{};
        if (!address || !MemoryHelper::IsValidPtr(address)) return res;

        const uint8_t* code = reinterpret_cast<const uint8_t*>(address);
        size_t currentOffset = 0;
        std::vector<std::string> byteTokens;

        while (currentOffset < maxLength) {
            hde64s hs;
            unsigned int len = hde64_disasm(code + currentOffset, &hs);
            if (len == 0 || (hs.flags & F_ERROR)) {
                len = 1;
                char buf[8];
                snprintf(buf, sizeof(buf), "%02X", code[currentOffset]);
                byteTokens.push_back(buf);
                currentOffset++;
                continue;
            }

            bool isRelative = (hs.flags & F_RELATIVE) != 0;
            bool isRipRelative = ((hs.flags & F_MODRM) && (hs.modrm_mod == 0) && (hs.modrm_rm == 5));
            bool isImm64 = (hs.flags & F_IMM64) != 0;

            for (unsigned int b = 0; b < len; ++b) {
                bool isWildcard = false;
                if (isRelative && b >= len - 4) {
                    isWildcard = true;
                } else if (isRipRelative && b >= len - 4) {
                    isWildcard = true;
                } else if (isImm64 && b >= len - 8) {
                    isWildcard = true;
                }

                if (isWildcard) {
                    byteTokens.push_back("??");
                } else {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "%02X", code[currentOffset + b]);
                    byteTokens.push_back(buf);
                }
            }

            currentOffset += len;

            if (currentOffset >= minLength) {
                std::ostringstream ss;
                for (size_t i = 0; i < byteTokens.size(); ++i) {
                    if (i > 0) ss << " ";
                    ss << byteTokens[i];
                }
                std::string pat = ss.str();
                auto matches = ScanAllModule(hModule, pat, 2);
                if (matches.size() == 1) {
                    res.pattern = pat;
                    res.byteLength = byteTokens.size();
                    res.matchCount = 1;
                    res.isUnique = true;
                    res.cppSnippet = "auto* pFn = reinterpret_cast<void*>(DreamScanner::ScanModule(nullptr, \"" + pat + "\"));";
                    return res;
                }
            }
        }

        std::ostringstream ss;
        for (size_t i = 0; i < byteTokens.size(); ++i) {
            if (i > 0) ss << " ";
            ss << byteTokens[i];
        }
        res.pattern = ss.str();
        res.byteLength = byteTokens.size();
        auto matches = ScanAllModule(hModule, res.pattern, 10);
        res.matchCount = matches.size();
        res.isUnique = (matches.size() == 1);
        res.cppSnippet = "auto* pFn = reinterpret_cast<void*>(DreamScanner::ScanModule(nullptr, \"" + res.pattern + "\"));";
        return res;
    }
};

} // namespace dreamkurumi

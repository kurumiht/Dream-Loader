#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include "DreamScanner.hpp"
#include "DreamFuzzyScanner.hpp"
#include "DreamCache.hpp"
#include "DreamLog.hpp"

namespace dreamkurumi {

// ============================================================================
// Multi-Tier Heuristic Definition
// Provides Primary, Secondary (wildcarded), Fuzzy (instruction-aware),
// and Anchor-prologue fallback patterns
// ============================================================================
struct HeuristicSignature {
    std::string name;
    std::string primaryPattern;     // Exact signature (strict)
    std::string secondaryPattern;   // Relaxed wildcarded pattern (registers masked)
    std::string anchorPattern;      // Proximity anchor (constant or string reference)
    int64_t anchorOffset{0};        // Expected offset from anchor (or 0 for prologue scan)
    uintptr_t fallbackRva{0};       // Default RVA if pattern scan fails
};

enum class HeuristicMatchTier {
    None = 0,
    Cache = 1,
    Primary = 2,
    Secondary = 3,
    Fuzzy = 4,
    Anchor = 5,
    Fallback = 6
};

// ============================================================================
// DreamHeuristic - Multi-Tier Resilient Signature & Offset Resolver
// Keeps hooks working across game patches without manual re-scanning
// ============================================================================
class DreamHeuristic {
public:
    static DreamHeuristic& Instance() {
        static DreamHeuristic s_instance;
        return s_instance;
    }

    // Resolves a symbol across all 6 resilience tiers
    void* Resolve(const HeuristicSignature& sig, HeuristicMatchTier* outTier = nullptr) {
        if (outTier) *outTier = HeuristicMatchTier::None;

        auto& cache = DreamCache::Instance();
        uintptr_t moduleBase = cache.GetModuleBase();
        if (moduleBase == 0) {
            moduleBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(NULL));
        }

        // Tier 1: Cache (Instant O(1))
        void* cachedAddr = cache.Resolve(sig.name.c_str(), 0);
        if (cachedAddr != nullptr) {
            if (outTier) *outTier = HeuristicMatchTier::Cache;
            return cachedAddr;
        }

        // Tier 2: Primary Pattern (Strict)
        if (!sig.primaryPattern.empty()) {
            void* found = DreamScanner::FindPattern(sig.primaryPattern.c_str());
            if (found) {
                uintptr_t rva = reinterpret_cast<uintptr_t>(found) - moduleBase;
                cache.Store(sig.name.c_str(), rva);
                DREAM_LOG_INFO("DreamHeuristic: '%s' resolved via [Primary Pattern] at %p (RVA: +0x%llX)",
                               sig.name.c_str(), found, static_cast<unsigned long long>(rva));
                if (outTier) *outTier = HeuristicMatchTier::Primary;
                return found;
            }
        }

        // Tier 3: Secondary Pattern (Relaxed Wildcards)
        if (!sig.secondaryPattern.empty()) {
            void* found = DreamScanner::FindPattern(sig.secondaryPattern.c_str());
            if (found) {
                uintptr_t rva = reinterpret_cast<uintptr_t>(found) - moduleBase;
                cache.Store(sig.name.c_str(), rva);
                DREAM_LOG_WARN("DreamHeuristic: [PATCH RESILIENT] '%s' resolved via [Secondary Pattern] at %p (RVA: +0x%llX)",
                               sig.name.c_str(), found, static_cast<unsigned long long>(rva));
                if (outTier) *outTier = HeuristicMatchTier::Secondary;
                return found;
            }
        }

        // Tier 4: Fuzzy Instruction-Aware Scan (Disassembly-based automatic masking via HDE64)
        if (!sig.primaryPattern.empty()) {
            void* found = DreamFuzzyScanner::FindFuzzyPattern(sig.primaryPattern);
            if (found) {
                uintptr_t rva = reinterpret_cast<uintptr_t>(found) - moduleBase;
                cache.Store(sig.name.c_str(), rva);
                DREAM_LOG_WARN("DreamHeuristic: [FUZZY INSTRUCTION SCAN] '%s' resolved via instruction-aware fuzzy scan at %p (RVA: +0x%llX)",
                               sig.name.c_str(), found, static_cast<unsigned long long>(rva));
                if (outTier) *outTier = HeuristicMatchTier::Fuzzy;
                return found;
            }
        }

        // Tier 5: Anchor Proximity / Function Prologue Scan
        if (!sig.anchorPattern.empty()) {
            void* anchor = DreamScanner::FindPattern(sig.anchorPattern.c_str());
            if (anchor) {
                void* resolved = nullptr;
                if (sig.anchorOffset != 0) {
                    resolved = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(anchor) + sig.anchorOffset);
                } else {
                    // Backward scan for standard x64 function prologue
                    resolved = ScanBackwardForPrologue(anchor, 512);
                }

                if (resolved) {
                    uintptr_t rva = reinterpret_cast<uintptr_t>(resolved) - moduleBase;
                    cache.Store(sig.name.c_str(), rva);
                    DREAM_LOG_WARN("DreamHeuristic: [ANCHOR HEURISTIC] '%s' resolved from anchor at %p -> Function: %p (RVA: +0x%llX)",
                                   sig.name.c_str(), anchor, resolved, static_cast<unsigned long long>(rva));
                    if (outTier) *outTier = HeuristicMatchTier::Anchor;
                    return resolved;
                }
            }
        }

        // Tier 6: Fallback RVA
        if (sig.fallbackRva != 0) {
            void* fallbackAddr = reinterpret_cast<void*>(moduleBase + sig.fallbackRva);
            DREAM_LOG_WARN("DreamHeuristic: [FALLBACK RVA] '%s' pattern matching failed; using hardcoded RVA +0x%llX at %p",
                           sig.name.c_str(), static_cast<unsigned long long>(sig.fallbackRva), fallbackAddr);
            if (outTier) *outTier = HeuristicMatchTier::Fallback;
            return fallbackAddr;
        }

        DREAM_LOG_ERROR("DreamHeuristic: Failed to resolve symbol '%s' across all tiers!", sig.name.c_str());
        return nullptr;
    }

    // Resolves a symbol within an explicit memory buffer (e.g. for testing or isolated regions)
    void* ResolveBuffer(const HeuristicSignature& sig, const void* buffer, size_t bufferSize, HeuristicMatchTier* outTier = nullptr) {
        if (outTier) *outTier = HeuristicMatchTier::None;
        if (!buffer || bufferSize == 0) return nullptr;

        const uint8_t* base = reinterpret_cast<const uint8_t*>(buffer);

        // Tier 2: Primary Pattern (Strict)
        if (!sig.primaryPattern.empty()) {
            void* found = DreamScanner::FindPattern(sig.primaryPattern, buffer, bufferSize);
            if (found) {
                if (outTier) *outTier = HeuristicMatchTier::Primary;
                return found;
            }
        }

        // Tier 3: Secondary Pattern (Relaxed Wildcards)
        if (!sig.secondaryPattern.empty()) {
            void* found = DreamScanner::FindPattern(sig.secondaryPattern, buffer, bufferSize);
            if (found) {
                if (outTier) *outTier = HeuristicMatchTier::Secondary;
                return found;
            }
        }

        // Tier 4: Fuzzy Instruction-Aware Scan
        if (!sig.primaryPattern.empty()) {
            void* found = DreamFuzzyScanner::FindFuzzyPattern(sig.primaryPattern, buffer, bufferSize);
            if (found) {
                if (outTier) *outTier = HeuristicMatchTier::Fuzzy;
                return found;
            }
        }

        // Tier 5: Anchor Proximity / Function Prologue Scan
        if (!sig.anchorPattern.empty()) {
            void* anchor = DreamScanner::FindPattern(sig.anchorPattern, buffer, bufferSize);
            if (anchor) {
                void* resolved = nullptr;
                if (sig.anchorOffset != 0) {
                    resolved = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(anchor) + sig.anchorOffset);
                } else {
                    resolved = ScanBackwardForPrologue(anchor, 512);
                }
                if (resolved && resolved >= buffer && resolved < base + bufferSize) {
                    if (outTier) *outTier = HeuristicMatchTier::Anchor;
                    return resolved;
                }
            }
        }

        // Tier 6: Fallback RVA (offset from buffer start)
        if (sig.fallbackRva != 0 && sig.fallbackRva < bufferSize) {
            void* fallbackAddr = const_cast<uint8_t*>(base + sig.fallbackRva);
            if (outTier) *outTier = HeuristicMatchTier::Fallback;
            return fallbackAddr;
        }

        return nullptr;
    }

    // Scans backwards from an anchor point to find the x64 function entry prologue
    static void* ScanBackwardForPrologue(void* anchorAddr, size_t maxScanBytes = 512) {
        if (!anchorAddr) return nullptr;

        uint8_t* curr = reinterpret_cast<uint8_t*>(anchorAddr);
        for (size_t i = 0; i < maxScanBytes; ++i) {
            uint8_t* p = curr - i;

            // Pattern A: 48 89 5C 24 ?? (mov [rsp+??], rbx)
            if (p[0] == 0x48 && p[1] == 0x89 && p[2] == 0x5C && p[3] == 0x24) {
                return p;
            }

            // Pattern B: 48 83 EC ?? (sub rsp, ??)
            if (p[0] == 0x48 && p[1] == 0x83 && p[2] == 0xEC) {
                // Check if preceded by push rbp; mov rbp, rsp (55 48 89 E5 or 55 48 8B EC)
                if (p >= curr - maxScanBytes + 4 && *(p - 4) == 0x55 && *(p - 3) == 0x48 &&
                    ((*(p - 2) == 0x89 && *(p - 1) == 0xE5) || (*(p - 2) == 0x8B && *(p - 1) == 0xEC))) {
                    return p - 4;
                }
                // If preceded by standard push rbp or push rdi (0x55 or 0x57)
                if (p > curr - maxScanBytes && (*(p - 1) == 0x55 || *(p - 1) == 0x57 || *(p - 1) == 0x53)) {
                    return p - 1;
                }
                return p;
            }

            // Pattern C: 40 53 48 83 EC (push rbx; sub rsp, ??)
            if (p[0] == 0x40 && p[1] == 0x53 && p[2] == 0x48 && p[3] == 0x83 && p[4] == 0xEC) {
                return p;
            }

            // Pattern D: 55 48 89 E5 or 55 48 8B EC (push rbp; mov rbp, rsp)
            if (p[0] == 0x55 && p[1] == 0x48 && ((p[2] == 0x89 && p[3] == 0xE5) || (p[2] == 0x8B && p[3] == 0xEC))) {
                return p;
            }

            // Stop backward scan if we hit a CC int3 or C3 ret alignment padding boundary
            if (i > 16 && (p[0] == 0xC3 || p[0] == 0xCC)) {
                // Return the instruction immediately following the alignment padding
                return p + 1;
            }
        }

        return nullptr;
    }

private:
    DreamHeuristic() = default;
};

} // namespace dreamkurumi

#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include "DreamScanner.hpp"
#include "DreamCache.hpp"
#include "DreamLog.hpp"

namespace dreamkurumi {

// ============================================================================
// Multi-Tier Heuristic Definition
// Provides Primary, Secondary (wildcarded), and Anchor-prologue fallback patterns
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
    Anchor = 4,
    Fallback = 5
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

    // Resolves a symbol using the multi-tier heuristic pipeline:
    // 1. Cache -> 2. Primary -> 3. Secondary Heuristic -> 4. Anchor Proximity -> 5. Fallback RVA
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
                DREAM_LOG_WARN("DreamHeuristic: [PATCH RESILIENT] '%s' resolved via [Secondary Heuristic Pattern] at %p (RVA: +0x%llX)",
                               sig.name.c_str(), found, static_cast<unsigned long long>(rva));
                if (outTier) *outTier = HeuristicMatchTier::Secondary;
                return found;
            }
        }

        // Tier 4: Anchor Proximity / Function Prologue Scan
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

        // Tier 5: Fallback RVA
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

        // Tier 4: Anchor Proximity / Function Prologue Scan
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

        // Tier 5: Fallback RVA (offset from buffer start)
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

        for (size_t i = 1; i < maxScanBytes; ++i) {
            uint8_t* candidate = curr - i;

            // Pattern A: 55 48 89 E5 (push rbp; mov rbp, rsp)
            if (candidate[0] == 0x55 && candidate[1] == 0x48 && candidate[2] == 0x89 && candidate[3] == 0xE5) {
                return candidate;
            }

            // Pattern B: 48 89 5C 24 (mov [rsp+??], rbx)
            if (candidate[0] == 0x48 && candidate[1] == 0x89 && candidate[2] == 0x5C && candidate[3] == 0x24) {
                // Must be preceded by INT3 (0xCC) or RET (0xC3)
                if (candidate[-1] == 0xCC || candidate[-1] == 0xC3) {
                    return candidate;
                }
            }

            // Pattern C: 48 83 EC ?? (sub rsp, ??) preceded by CC or C3
            if (candidate[0] == 0x48 && candidate[1] == 0x83 && candidate[2] == 0xEC) {
                if (candidate[-1] == 0xCC || candidate[-1] == 0xC3) {
                    return candidate;
                }
            }

            // Pattern D: 40 53 48 83 EC (push rbx; sub rsp, ??)
            if (candidate[0] == 0x40 && candidate[1] == 0x53 && candidate[2] == 0x48 && candidate[3] == 0x83 && candidate[4] == 0xEC) {
                return candidate;
            }
        }

        return nullptr;
    }

private:
    DreamHeuristic() = default;
};

} // namespace dreamkurumi

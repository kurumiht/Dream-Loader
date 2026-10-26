#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>

namespace dreamkurumi {

// ============================================================================
// DreamHash - Compile-Time 64-bit FNV-1a Hash Engine
// Enables 0-ns compile-time string hashing and switch-case on strings!
// Standard FNV-1a 64-bit constants:
//   Offset Basis = 0xcbf29ce484222325ULL (14695981039346656037)
//   Prime        = 0x100000001b3ULL      (1099511628211)
// ============================================================================
class DreamHash {
public:
    static constexpr uint64_t FNV1A_OFFSET_BASIS = 0xcbf29ce484222325ULL;
    static constexpr uint64_t FNV1A_PRIME        = 0x100000001b3ULL;

    // Compile-time or runtime FNV-1a 64-bit hash from raw buffer
    static constexpr uint64_t Fnv1a64(const char* data, size_t length) noexcept {
        uint64_t hash = FNV1A_OFFSET_BASIS;
        for (size_t i = 0; i < length; ++i) {
            hash ^= static_cast<uint8_t>(data[i]);
            hash *= FNV1A_PRIME;
        }
        return hash;
    }

    // Compile-time or runtime hash from null-terminated C string
    static constexpr uint64_t Hash(const char* str) noexcept {
        if (!str) return 0;
        uint64_t hash = FNV1A_OFFSET_BASIS;
        while (*str) {
            hash ^= static_cast<uint8_t>(*str++);
            hash *= FNV1A_PRIME;
        }
        return hash;
    }

    // Hash from std::string_view
    static constexpr uint64_t Hash(std::string_view sv) noexcept {
        return Fnv1a64(sv.data(), sv.size());
    }

    // Hash from std::string (Runtime)
    static uint64_t Hash(const std::string& str) noexcept {
        return Fnv1a64(str.data(), str.size());
    }

    // Case-insensitive FNV-1a 64-bit hash
    static constexpr uint64_t HashCaseInsensitive(const char* str) noexcept {
        if (!str) return 0;
        uint64_t hash = FNV1A_OFFSET_BASIS;
        while (*str) {
            char c = *str++;
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
            hash ^= static_cast<uint8_t>(c);
            hash *= FNV1A_PRIME;
        }
        return hash;
    }

    static uint64_t HashCaseInsensitive(const std::string& str) noexcept {
        uint64_t hash = FNV1A_OFFSET_BASIS;
        for (char c : str) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
            hash ^= static_cast<uint8_t>(c);
            hash *= FNV1A_PRIME;
        }
        return hash;
    }
};

// ============================================================================
// User-Defined String Literal operator""_hash
// Usage: switch(DreamHash::Hash(cmd)) { case "god"_hash: ... }
// ============================================================================
namespace literals {
    constexpr uint64_t operator""_hash(const char* str, size_t len) noexcept {
        return DreamHash::Fnv1a64(str, len);
    }

    constexpr uint64_t operator""_ihash(const char* str, size_t) noexcept {
        return DreamHash::HashCaseInsensitive(str);
    }
} // namespace literals

} // namespace dreamkurumi

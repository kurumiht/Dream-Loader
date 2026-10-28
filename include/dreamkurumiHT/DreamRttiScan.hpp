#pragma once
// DreamRttiScan.hpp - one-pass MSVC x64 RTTI index + keyword query + vtable slot export.
//
// Pipeline (single pass over the module's non-executable sections, then O(1) lookups):
//   1. TypeDescriptors : find ".?AV<name>@@" / ".?AU<name>@@" strings; TD = string address - 16
//   2. CompleteObjectLocators : 24-byte records with signature==1, pSelf == own RVA, pTypeDescriptor == a known TD
//   3. vtables : any qword that equals a COL address; vtable = that qword's address + 8
//   4. slots   : consecutive qwords pointing into executable sections (stops at the next vtable's COL pointer)
//   5. each slot target is resolved to a function start (follows E9 thunks, then .pdata BeginAddress)
//   6. sharedBy = how many vtables reference the same function; high values are _purecall / common thunks /
//      ICF-folded stubs: DON'T hook those, they fire for unrelated classes.
//
// Limits (be aware):
//   * Only works if the binary was built with RTTI (/GR) by MSVC or clang-cl (same MS ABI layout). If step 1
//     finds nothing, RTTI is stripped (or the image is packed/encrypted at the time you scan).
//   * Classes with no virtual functions have no RTTI; template names ("?$...") are left mangled.
//   * Slots are *candidates*. Virtual getters/destructors can be called thousands of times per frame:
//     install in small batches and look at counts before widening.
//   * Secondary vtables (multiple inheritance) are separate Vtable entries with thisOffset > 0.
//
// Scan once at startup (tens of ms to ~1 s depending on image size), not on a hot path.

#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "DreamTrace.hpp"   // for InstallTraceHooks glue (optional)

namespace dream::rtti {

struct Vtable {
    uint32_t classId = 0;
    uint32_t thisOffset = 0;              // COL.offset: 0 = primary, >0 = secondary (this-adjust)
    uintptr_t addr = 0;
    std::vector<uintptr_t> slots;         // raw slot targets
    std::vector<uintptr_t> starts;        // resolved function starts (parallel to slots)
};

struct ClassInfo {
    std::string raw;                      // ".?AVFoo@Bar@@"
    std::string name;                     // "Bar::Foo" (or raw for templates)
    uintptr_t td = 0;
    std::vector<uint32_t> vtables;        // indices into Index::Vtables()
};

class Index {
public:
    struct Candidate {
        const ClassInfo* cls;
        const Vtable* vt;
        size_t slot;
        uintptr_t fn;                     // slot target
        uintptr_t fnStart;                // function start to hook
        uint32_t sharedBy;                // number of vtables using this function
    };

    bool Build(HMODULE mod = GetModuleHandleW(nullptr)) {
        Clear();
        m_base = reinterpret_cast<uintptr_t>(mod);
        if (!mod) return false;
        auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(m_base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
        auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(m_base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return false;

        const IMAGE_SECTION_HEADER* sh = IMAGE_FIRST_SECTION(nt);
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
            DWORD sz = sh[i].Misc.VirtualSize ? sh[i].Misc.VirtualSize : sh[i].SizeOfRawData;
            Section s;
            s.name.assign(reinterpret_cast<const char*>(sh[i].Name),
                          strnlen(reinterpret_cast<const char*>(sh[i].Name), 8));
            s.begin = m_base + sh[i].VirtualAddress;
            s.end = s.begin + sz;
            s.chars = sh[i].Characteristics;
            m_sections.push_back(std::move(s));
        }
        ScanTypeDescriptors();
        ScanLocators();
        ScanVtables();
        ResolveAndCountSharing();
        return !m_vtables.empty();
    }

    uintptr_t Base() const { return m_base; }
    const std::vector<ClassInfo>& Classes() const { return m_classes; }
    const std::vector<Vtable>& Vtables() const { return m_vtables; }

    // Case-insensitive substring match on demangled and raw names.
    std::vector<Candidate> Query(const std::vector<std::string>& keywords, uint32_t maxSharedBy = 3) const {
        std::vector<std::string> kw;
        for (auto& k : keywords) kw.push_back(Lower(k));
        std::vector<Candidate> out;
        for (const auto& c : m_classes) {
            if (!Matches(c, kw)) continue;
            for (uint32_t vi : c.vtables) {
                const Vtable& vt = m_vtables[vi];
                for (size_t s = 0; s < vt.slots.size(); ++s) {
                    const uintptr_t st = vt.starts[s];
                    auto it = m_share.find(st);
                    const uint32_t shared = it == m_share.end() ? 1 : it->second;
                    if (shared > maxSharedBy) continue;
                    out.push_back({&c, &vt, s, vt.slots[s], st, shared});
                }
            }
        }
        return out;
    }

    // JSON for the matching classes: every vtable, every slot with RVAs and sharing counts.
    bool ExportJson(const char* path, const std::vector<std::string>& keywords) const {
        FILE* f = std::fopen(path, "wb");
        if (!f) return false;
        std::vector<std::string> kw;
        for (auto& k : keywords) kw.push_back(Lower(k));
        std::fprintf(f, "{\"module_base\":\"0x%llx\",\"classes\":[\n", (unsigned long long)m_base);
        bool firstC = true;
        for (const auto& c : m_classes) {
            if (!Matches(c, kw)) continue;
            std::fprintf(f, "%s{\"name\":\"%s\",\"raw\":\"%s\",\"vtables\":[", firstC ? "" : ",\n",
                         Esc(c.name).c_str(), Esc(c.raw).c_str());
            firstC = false;
            for (size_t v = 0; v < c.vtables.size(); ++v) {
                const Vtable& vt = m_vtables[c.vtables[v]];
                std::fprintf(f, "%s{\"vtable_rva\":\"0x%llx\",\"this_offset\":%u,\"slots\":[", v ? "," : "",
                             (unsigned long long)(vt.addr - m_base), vt.thisOffset);
                for (size_t s = 0; s < vt.slots.size(); ++s) {
                    auto it = m_share.find(vt.starts[s]);
                    std::fprintf(f, "%s{\"i\":%zu,\"slot_rva\":\"0x%llx\",\"fn_rva\":\"0x%llx\",\"shared_by\":%u}",
                                 s ? "," : "", s, (unsigned long long)(vt.slots[s] - m_base),
                                 (unsigned long long)(vt.starts[s] - m_base),
                                 it == m_share.end() ? 1u : it->second);
                }
                std::fprintf(f, "]}");
            }
            std::fprintf(f, "]}");
        }
        std::fprintf(f, "\n]}\n");
        std::fclose(f);
        return true;
    }

    // Discovery aid: every class with its vtable RVAs, so you can browse names and pick keywords.
    bool ExportClassList(const char* path) const {
        FILE* f = std::fopen(path, "wb");
        if (!f) return false;
        for (const auto& c : m_classes) {
            if (c.vtables.empty()) continue;
            std::fprintf(f, "%s\t", c.name.c_str());
            for (size_t v = 0; v < c.vtables.size(); ++v) {
                const Vtable& vt = m_vtables[c.vtables[v]];
                std::fprintf(f, "%s+0x%x:vt=0x%llx:n=%zu", v ? " " : "", vt.thisOffset,
                             (unsigned long long)(vt.addr - m_base), vt.slots.size());
            }
            std::fprintf(f, "\n");
        }
        std::fclose(f);
        return true;
    }

private:
    struct Section { std::string name; uintptr_t begin = 0, end = 0; DWORD chars = 0; };
    struct COL { uint32_t signature, offset, cdOffset; int32_t pTypeDescriptor, pClassDescriptor, pSelf; };
    struct ColInfo { uint32_t classId; uint32_t offset; };

    void Clear() {
        m_sections.clear(); m_classes.clear(); m_vtables.clear();
        m_tdToClass.clear(); m_colToInfo.clear(); m_share.clear();
        m_colMin = ~uintptr_t(0); m_colMax = 0;
    }

    static bool IsData(const Section& s) {
        return (s.chars & IMAGE_SCN_MEM_READ) && !(s.chars & IMAGE_SCN_MEM_EXECUTE) &&
               !(s.chars & IMAGE_SCN_MEM_DISCARDABLE);
    }
    bool IsExec(uintptr_t a) const {
        for (const auto& s : m_sections)
            if ((s.chars & IMAGE_SCN_MEM_EXECUTE) && a >= s.begin && a < s.end) return true;
        return false;
    }

    // Visit only committed, readable, non-guard ranges: never touches memory that could fault.
    template <class F>
    static void ForEachReadable(uintptr_t b, uintptr_t e, F&& fn) {
        while (b < e) {
            MEMORY_BASIC_INFORMATION mbi;
            if (!VirtualQuery(reinterpret_cast<void*>(b), &mbi, sizeof(mbi))) break;
            const uintptr_t rb = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
            const uintptr_t re = rb + mbi.RegionSize;
            const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                                   PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
            const bool ok = mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
                            (mbi.Protect & readable);
            const uintptr_t s = std::max(b, rb), t = std::min(e, re);
            if (ok && s < t) fn(s, t);
            if (re <= b) break;
            b = re;
        }
    }

    static std::string Lower(std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }
    static std::string Esc(std::string s) {
        for (char& c : s) if (c == '"' || c == '\\') c = '_';
        return s;
    }
    static bool Matches(const ClassInfo& c, const std::vector<std::string>& kw) {
        if (kw.empty()) return true;
        const std::string n = Lower(c.name), r = Lower(c.raw);
        for (const auto& k : kw)
            if (n.find(k) != std::string::npos || r.find(k) != std::string::npos) return true;
        return false;
    }

    // ".?AVFoo@Bar@@" -> "Bar::Foo". Templates ("?$") stay raw.
    static std::string Demangle(const std::string& raw) {
        if (raw.size() < 7) return raw;
        std::string s = raw.substr(4, raw.size() - 6);
        if (s.find('?') != std::string::npos) return raw;
        std::vector<std::string> parts;
        size_t st = 0;
        while (st <= s.size()) {
            size_t p = s.find('@', st);
            if (p == std::string::npos) p = s.size();
            if (p > st) parts.push_back(s.substr(st, p - st));
            st = p + 1;
        }
        std::string out;
        for (size_t i = parts.size(); i-- > 0;) { out += parts[i]; if (i) out += "::"; }
        return out.empty() ? raw : out;
    }

    static uintptr_t ResolveFunctionStart(uintptr_t fn) {
        for (int i = 0; i < 2; ++i) {                        // follow incremental-link / ICF thunks
            const uint8_t* b = reinterpret_cast<const uint8_t*>(fn);
            if (b[0] != 0xE9) break;
            int32_t d; std::memcpy(&d, b + 1, 4);
            fn = fn + 5 + d;
        }
        DWORD64 base = 0;
        PRUNTIME_FUNCTION fe = RtlLookupFunctionEntry(fn, &base, nullptr);
        return fe ? static_cast<uintptr_t>(base + fe->BeginAddress) : fn;
    }

    void ScanTypeDescriptors() {
        for (const auto& sec : m_sections) {
            if (!IsData(sec)) continue;
            ForEachReadable(sec.begin, sec.end, [&](uintptr_t b, uintptr_t e) {
                for (uintptr_t p = b; p + 8 <= e; ++p) {
                    const char* c = reinterpret_cast<const char*>(p);
                    if (c[0] != '.' || c[1] != '?' || c[2] != 'A' || (c[3] != 'V' && c[3] != 'U')) continue;
                    const uintptr_t td = p - 16;
                    if ((td & 7) || td < sec.begin) continue;
                    size_t n = 0;
                    while (p + n < e && n < 256 && c[n]) ++n;
                    if (p + n >= e || c[n] != 0 || n < 6) continue;
                    bool ok = true;
                    for (size_t k = 0; k < n; ++k) {
                        const unsigned char ch = static_cast<unsigned char>(c[k]);
                        if (ch < 0x21 || ch > 0x7E) { ok = false; break; }
                    }
                    if (!ok || c[n - 1] != '@' || c[n - 2] != '@') continue;
                    const uint32_t rva = static_cast<uint32_t>(td - m_base);
                    if (m_tdToClass.count(rva)) continue;
                    ClassInfo ci;
                    ci.raw.assign(c, n);
                    ci.name = Demangle(ci.raw);
                    ci.td = td;
                    m_tdToClass[rva] = static_cast<uint32_t>(m_classes.size());
                    m_classes.push_back(std::move(ci));
                }
            });
        }
    }

    void ScanLocators() {
        if (m_tdToClass.empty()) return;
        for (const auto& sec : m_sections) {
            if (!IsData(sec)) continue;
            ForEachReadable(sec.begin, sec.end, [&](uintptr_t b, uintptr_t e) {
                for (uintptr_t p = (b + 3) & ~uintptr_t(3); p + sizeof(COL) <= e; p += 4) {
                    const COL* c = reinterpret_cast<const COL*>(p);
                    if (c->signature != 1) continue;                               // 1 = x64 (RVA-based)
                    if (c->pSelf != static_cast<int32_t>(p - m_base)) continue;    // strong false-positive filter
                    auto it = m_tdToClass.find(static_cast<uint32_t>(c->pTypeDescriptor));
                    if (it == m_tdToClass.end()) continue;
                    m_colToInfo[p] = {it->second, c->offset};
                    m_colMin = std::min(m_colMin, p);
                    m_colMax = std::max(m_colMax, p);
                }
            });
        }
    }

    void ScanVtables() {
        if (m_colToInfo.empty()) return;
        for (const auto& sec : m_sections) {
            if (!IsData(sec)) continue;
            ForEachReadable(sec.begin, sec.end, [&](uintptr_t b, uintptr_t e) {
                for (uintptr_t p = (b + 7) & ~uintptr_t(7); p + 8 <= e; p += 8) {
                    const uint64_t q = *reinterpret_cast<const uint64_t*>(p);
                    if (q < m_colMin || q > m_colMax) continue;
                    auto it = m_colToInfo.find(static_cast<uintptr_t>(q));
                    if (it == m_colToInfo.end()) continue;
                    Vtable v;
                    v.classId = it->second.classId;
                    v.thisOffset = it->second.offset;
                    v.addr = p + 8;
                    for (size_t i = 0; i < 1024 && v.addr + 8 * (i + 1) <= e; ++i) {
                        const uint64_t fn = *reinterpret_cast<const uint64_t*>(v.addr + 8 * i);
                        if (!IsExec(static_cast<uintptr_t>(fn))) break;            // next COL ptr / data => end
                        v.slots.push_back(static_cast<uintptr_t>(fn));
                    }
                    if (v.slots.empty()) continue;
                    m_classes[v.classId].vtables.push_back(static_cast<uint32_t>(m_vtables.size()));
                    m_vtables.push_back(std::move(v));
                }
            });
        }
    }

    void ResolveAndCountSharing() {
        std::unordered_map<uintptr_t, uintptr_t> cache;
        for (auto& v : m_vtables) {
            v.starts.resize(v.slots.size());
            std::unordered_set<uintptr_t> uniq;
            for (size_t i = 0; i < v.slots.size(); ++i) {
                auto c = cache.find(v.slots[i]);
                if (c == cache.end()) c = cache.emplace(v.slots[i], ResolveFunctionStart(v.slots[i])).first;
                v.starts[i] = c->second;
                uniq.insert(c->second);
            }
            for (uintptr_t s : uniq) ++m_share[s];
        }
    }

    uintptr_t m_base = 0;
    uintptr_t m_colMin = ~uintptr_t(0), m_colMax = 0;
    std::vector<Section> m_sections;
    std::vector<ClassInfo> m_classes;
    std::vector<Vtable> m_vtables;
    std::unordered_map<uint32_t, uint32_t> m_tdToClass;      // TD RVA -> class index
    std::unordered_map<uintptr_t, ColInfo> m_colToInfo;      // COL address -> class + this offset
    std::unordered_map<uintptr_t, uint32_t> m_share;         // function start -> number of vtables using it
};

// Attach DreamTrace mid-hooks to unique candidate functions (custom hook installer).
inline size_t InstallTraceHooks(const std::vector<Index::Candidate>& cands,
                                dream::trace::InstallMidHookFn install, size_t maxHooks = 30) {
    std::unordered_set<uintptr_t> seen;
    size_t n = 0;
    for (const auto& c : cands) {
        if (n >= maxHooks) break;
        if (!seen.insert(c.fnStart).second) continue;
        char name[96];
        std::snprintf(name, sizeof(name), "%s::vf%zu", c.cls->name.c_str(), c.slot);
        if (dream::trace::TraceFunctionCustom(install, name, reinterpret_cast<void*>(c.fnStart))) ++n;
    }
    return n;
}

// Attach DreamTrace mid-hooks using dreamkurumi's default DreamMidHook engine.
inline size_t InstallTraceHooks(const std::vector<Index::Candidate>& cands, size_t maxHooks = 30) {
    std::unordered_set<uintptr_t> seen;
    size_t n = 0;
    for (const auto& c : cands) {
        if (n >= maxHooks) break;
        if (!seen.insert(c.fnStart).second) continue;
        char name[96];
        std::snprintf(name, sizeof(name), "%s::vf%zu", c.cls->name.c_str(), c.slot);
        if (dream::trace::TraceFunction(name, reinterpret_cast<void*>(c.fnStart))) ++n;
    }
    return n;
}

} // namespace dream::rtti

namespace dreamkurumi {
namespace rtti = dream::rtti;
}

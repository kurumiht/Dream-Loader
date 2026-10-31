#!/usr/bin/env python3
"""Generate abi/golden_plugin/golden_plugin.cpp from the BIND_DK list of a (frozen) dreamkurumiHT_plugin.h.

usage: python abi/make_golden_plugin.py abi/golden/v1.0.0-rc1/dreamkurumiHT_plugin.h
"""
import re, sys, os

HEAD = '// golden_plugin.cpp - "golden plugin" for the dreamkurumiHT plugin ABI.\n//\n// Built ONCE per ABI release against the FROZEN copy of the plugin header (abi/golden/<tag>/dreamkurumiHT_plugin.h,\n// created by abi/freeze_header.sh) and then loaded by the CURRENT core in CI (abi/golden_host_test.cpp).\n// If a new core still resolves every symbol this old plugin binds, old plugins keep working.\n//\n// This file is GENERATED from the BIND_DK list of the frozen header; regenerate with abi/make_golden_plugin.py.\n// Build (MinGW):  g++ -std=c++20 -shared -O1 -I abi/golden/<tag> abi/golden_plugin/golden_plugin.cpp -o golden_plugin_<tag>.dll\n// Do NOT commit the resulting DLL (build it in CI, cache as a CI artifact).\n\n#include "dreamkurumiHT_plugin.h"\n#include <cstdio>\n#include <cstring>\n\n#ifdef _WIN32\n#define GOLDEN_EXPORT extern "C" __declspec(dllexport)\n#else\n#define GOLDEN_EXPORT extern "C"\n#endif\n\nusing namespace dreamkurumi;\n\n// Returns 0 when the core resolved everything this plugin version binds.\n// Otherwise returns the number of unresolved symbols and writes their names (comma separated) into err.\n'
PRE = 'GOLDEN_EXPORT int GoldenSelfTest(char* err, int cap) {\n    auto& h = PluginHost::Instance();\n    if (!h.Init()) { if (err && cap) std::snprintf(err, (size_t)cap, "PluginHost::Init failed (dreamkurumiHT.dll not found)"); return -1; }\n    int missing = 0; std::string names;\n'
CHECK_DEF = '#define CHECK(ptr, nm) do { if (!h.ptr) { ++missing; if (!names.empty()) names += ","; names += nm; } } while (0)\n'
TAIL = '#undef CHECK\n    if (err && cap) std::snprintf(err, (size_t)cap, "%s", names.c_str());\n    return missing;\n}\n\n// A call through the real C ABI: logging must not crash and must accept the old signature.\nGOLDEN_EXPORT int GoldenSmokeCalls() {\n    auto& h = PluginHost::Instance();\n    if (!h.Init()) return -1;\n    if (h.pfn_DK_Log) h.pfn_DK_Log(1, "golden plugin: ABI smoke call");\n    if (h.pfn_DK_GetModuleBase && h.pfn_DK_GetModuleBase() == 0) return 1;   // 0 is allowed in a test host: informational\n    return 0;\n}\n'

def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    h = open(sys.argv[1], encoding="utf-8-sig").read()
    binds = sorted(set(re.findall(r"BIND_DK\((DK_\w+)\)", h)) - {"name"})
    lines = "\n".join(f'    CHECK(pfn_{b}, "{b}");' for b in binds)
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "golden_plugin", "golden_plugin.cpp")
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(HEAD + PRE + CHECK_DEF + lines + "\n" + TAIL)
    print(f"{out}: {len(binds)} symbols")

if __name__ == "__main__":
    main()

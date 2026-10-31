// golden_host_test.cpp - loads the CURRENT core and a GOLDEN (old-ABI) plugin and checks they still fit.
//
//   golden_host_test.exe <path\to\dreamkurumiHT.dll> <path\to\golden_plugin_<tag>.dll>
//
// Exit codes: 0 = compatible, 1 = golden plugin binds symbols the core no longer exports, 2 = setup problem.
// Run it in CI for every golden tag you keep (v1.0.0-rc1, v1.0.0, ...). Windows only; NOT run by the author
// of this file (no Windows toolchain was available), so treat the first CI run as the real test.

#include <windows.h>
#include <cstdio>

using SelfTestFn = int (*)(char*, int);
using SmokeFn    = int (*)();

int main(int argc, char** argv) {
    if (argc != 3) { std::fprintf(stderr, "usage: %s <core.dll> <golden_plugin.dll>\n", argv[0]); return 2; }

    // Add core DLL's directory to search path so dependent DLLs (e.g. lua51.dll) load smoothly on Windows
    char coreDir[MAX_PATH] = {};
    std::snprintf(coreDir, sizeof(coreDir), "%s", argv[1]);
    char* pSlash = std::strrchr(coreDir, '\\');
    if (!pSlash) pSlash = std::strrchr(coreDir, '/');
    if (pSlash) {
        *pSlash = '\0';
        SetDllDirectoryA(coreDir);
    }

    // The plugin header's PluginHost::Init() does GetModuleHandleA("dreamkurumiHT.dll") first, so load the core
    // under that name by absolute path BEFORE the plugin.
    HMODULE core = LoadLibraryA(argv[1]);
    if (!core) { std::fprintf(stderr, "cannot load core '%s' (error %lu)\n", argv[1], GetLastError()); return 2; }
    char base[MAX_PATH] = {};
    GetModuleFileNameA(core, base, MAX_PATH);
    std::printf("core loaded: %s\n", base);

    HMODULE plug = LoadLibraryA(argv[2]);
    if (!plug) { std::fprintf(stderr, "cannot load golden plugin '%s' (error %lu)\n", argv[2], GetLastError()); return 2; }

    auto selfTest = reinterpret_cast<SelfTestFn>(GetProcAddress(plug, "GoldenSelfTest"));
    auto smoke    = reinterpret_cast<SmokeFn>(GetProcAddress(plug, "GoldenSmokeCalls"));
    if (!selfTest || !smoke) { std::fprintf(stderr, "golden plugin lacks GoldenSelfTest/GoldenSmokeCalls\n"); return 2; }

    char err[4096] = {};
    const int missing = selfTest(err, sizeof(err));
    if (missing != 0) {
        std::fprintf(stderr, "ABI BREAK: %d symbol(s) the golden plugin binds are not resolvable in this core: %s\n", missing, err);
        return 1;
    }
    const int s = smoke();
    std::printf("golden plugin OK (smoke=%d)\n", s);
    return s < 0 ? 2 : 0;
}

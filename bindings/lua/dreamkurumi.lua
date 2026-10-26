-- ============================================================================
-- dreamkurumi.lua - Lua / LuaJIT Binding for dreamkurumiHT Framework
--
-- Compatible with Enshrouded Mod Loader (EML) and LuaJIT FFI!
-- Allows Lua modders to resolve RVAs, NOP instructions, and hook functions.
-- ============================================================================

local ffi = require("ffi")

ffi.cdef[[
    bool DK_Initialize(const char* cachePath);
    size_t DK_PatchAll();
    void DK_UnpatchAll();
    void* DK_Resolve(const char* symbolName, uintptr_t fallbackRva);
    void* DK_ResolvePattern(const char* symbolName, const char* pattern, uintptr_t fallbackRva);
    uintptr_t DK_GetModuleBase();
    uint32_t DK_GetGameBuildTimestamp();
    void* DK_Nop(void* target, size_t count);
    bool DK_RestorePatch(void* patchHandle);
    void DK_Log(int level, const char* msg);

    void DK_Preset_SetGodMode(bool enable);
    void DK_Preset_SetGhostMode(bool enable);
    void DK_Preset_SetInfiniteStamina(bool enable);
    void DK_Preset_SetInfiniteMana(bool enable);
    void DK_Preset_SetInfiniteShroud(bool enable);
    void DK_Preset_SetFreeCraft(bool enable);
    void DK_Preset_SetSuperMining(bool enable, float mult);
    void DK_Preset_SetUnlimitedFlight(bool enable);
    void DK_Preset_SetInfiniteItemSplit(bool enable);
    void DK_Preset_SetInfiniteItemUse(bool enable);
    void DK_Preset_SetNoResourceCost(bool enable);
    void DK_Preset_SetInfiniteDurability(bool enable);
    void DK_Preset_SetOneShotKill(bool enable);
    void DK_Preset_SetFastProduction(bool enable, float seconds);
    bool DK_Preset_SaveConfig(const char* path);
    bool DK_Preset_LoadConfig(const char* path);
    bool DK_Preset_IsDedicatedServer();

    void DK_Overlay_ToggleMenu();
    bool DK_Overlay_IsMenuOpen();
    void DK_Overlay_SetMenuKey(int vKey);
    bool DK_Overlay_ExecuteCommand(const char* cmd);
]]

local M = {}
local dk = nil

local function load_native_library()
    if dk then return dk end
    local ok, lib = pcall(ffi.load, "dreamkurumi.dll")
    if ok then
        dk = lib
        return dk
    end
    -- Fallback search in mod folder
    ok, lib = pcall(ffi.load, "./dreamkurumi.dll")
    if ok then
        dk = lib
        return dk
    end
    return nil
end

function M.init(cachePath)
    local lib = load_native_library()
    if not lib then return false end
    return lib.DK_Initialize(cachePath or "dreamkurumi_cache.ini")
end

function M.resolve(symbolName, fallbackRva)
    local lib = load_native_library()
    if not lib then return nil end
    local ptr = lib.DK_Resolve(symbolName, fallbackRva or 0)
    if ptr == nil or tonumber(ffi.cast("uintptr_t", ptr)) == 0 then
        return nil
    end
    return ptr
end

function M.resolvePattern(symbolName, pattern, fallbackRva)
    local lib = load_native_library()
    if not lib then return nil end
    local ptr = lib.DK_ResolvePattern(symbolName, pattern, fallbackRva or 0)
    if ptr == nil or tonumber(ffi.cast("uintptr_t", ptr)) == 0 then
        return nil
    end
    return ptr
end

function M.nop(address, count)
    local lib = load_native_library()
    if not lib or not address then return nil end
    return lib.DK_Nop(address, count)
end

function M.restore(patchHandle)
    local lib = load_native_library()
    if not lib or not patchHandle then return false end
    return lib.DK_RestorePatch(patchHandle)
end

function M.log(message, level)
    local lib = load_native_library()
    if lib then
        lib.DK_Log(level or 1, tostring(message))
    else
        print("[dreamkurumi:lua] " .. tostring(message))
    end
end

M.presets = {
    setGodMode = function(enable) local lib = load_native_library() if lib then lib.DK_Preset_SetGodMode(enable) end end,
    setGhostMode = function(enable) local lib = load_native_library() if lib then lib.DK_Preset_SetGhostMode(enable) end end,
    setInfiniteStamina = function(enable) local lib = load_native_library() if lib then lib.DK_Preset_SetInfiniteStamina(enable) end end,
    setInfiniteMana = function(enable) local lib = load_native_library() if lib then lib.DK_Preset_SetInfiniteMana(enable) end end,
    setInfiniteShroud = function(enable) local lib = load_native_library() if lib then lib.DK_Preset_SetInfiniteShroud(enable) end end,
    setFreeCraft = function(enable) local lib = load_native_library() if lib then lib.DK_Preset_SetFreeCraft(enable) end end,
    setSuperMining = function(enable, mult) local lib = load_native_library() if lib then lib.DK_Preset_SetSuperMining(enable, mult or 10.0) end end,
    setUnlimitedFlight = function(enable) local lib = load_native_library() if lib then lib.DK_Preset_SetUnlimitedFlight(enable) end end,
    setInfiniteItemSplit = function(enable) local lib = load_native_library() if lib then lib.DK_Preset_SetInfiniteItemSplit(enable) end end,
    setInfiniteItemUse = function(enable) local lib = load_native_library() if lib then lib.DK_Preset_SetInfiniteItemUse(enable) end end,
    setNoResourceCost = function(enable) local lib = load_native_library() if lib then lib.DK_Preset_SetNoResourceCost(enable) end end,
    setInfiniteDurability = function(enable) local lib = load_native_library() if lib then lib.DK_Preset_SetInfiniteDurability(enable) end end,
    setOneShotKill = function(enable) local lib = load_native_library() if lib then lib.DK_Preset_SetOneShotKill(enable) end end,
    setFastProduction = function(enable, sec) local lib = load_native_library() if lib then lib.DK_Preset_SetFastProduction(enable, sec or 1.0) end end,
    saveConfig = function(path) local lib = load_native_library() return lib and lib.DK_Preset_SaveConfig(path) or false end,
    loadConfig = function(path) local lib = load_native_library() return lib and lib.DK_Preset_LoadConfig(path) or false end,
    isDedicatedServer = function() local lib = load_native_library() return lib and lib.DK_Preset_IsDedicatedServer() or false end
}

M.overlay = {
    toggleMenu = function() local lib = load_native_library() if lib then lib.DK_Overlay_ToggleMenu() end end,
    isMenuOpen = function() local lib = load_native_library() return lib and lib.DK_Overlay_IsMenuOpen() or false end,
    setMenuKey = function(vKey) local lib = load_native_library() if lib then lib.DK_Overlay_SetMenuKey(vKey) end end,
    executeCommand = function(cmd) local lib = load_native_library() return lib and lib.DK_Overlay_ExecuteCommand(tostring(cmd)) or false end
}

return M

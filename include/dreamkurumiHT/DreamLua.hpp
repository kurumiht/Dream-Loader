#pragma once

// ============================================================================
// DreamLua.hpp - High-Performance LuaJIT & Scripting Engine for dreamkurumiHT
//
// Integrates LuaJIT with JIT compilation, Lua FFI, yyjson fast JSON parser,
// in-engine Native MidHooks, EventBus pub/sub, recurring timers, and RE Suite!
// Provides safe, sandboxed execution with DreamCrashGuard and full thread-safety.
// ============================================================================

#include "lua/lua.hpp"
#include "lua/luajit.h"
#include "DreamCrashGuard.hpp"
#include "DreamGameContext.hpp"
#include "DreamJson.hpp"
#include "DreamLog.hpp"
#include "DreamMemory.hpp"
#include "DreamScanner.hpp"
#include "DreamRtti.hpp"
#include "DreamTracer.hpp"
#include "DreamHook.hpp"
#include "DreamEventBus.hpp"
#include <string>
#include <functional>
#include <iostream>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>

namespace dreamkurumi {

struct LuaMidHookEntry {
    uint32_t id{0};
    void* address{nullptr};
    int callbackRef{LUA_NOREF};
    std::unique_ptr<DreamMidHook> hook;
};

struct LuaEventSubEntry {
    uint64_t tokenId{0};
    int callbackRef{LUA_NOREF};
};

struct LuaTimerEntry {
    uint32_t id{0};
    int callbackRef{LUA_NOREF};
    uint32_t intervalMs{1000};
    uint64_t nextTrigger{0};
};

class DreamLuaEngine {
private:
    lua_State* m_L = nullptr;
    bool m_initialized = false;
    mutable std::recursive_mutex m_mutex;

    // Active MidHooks from Lua
    uint32_t m_nextHookId{0};
    std::unordered_map<uint32_t, std::unique_ptr<LuaMidHookEntry>> m_luaHooks;

    // Active EventBus Subscriptions from Lua
    std::vector<LuaEventSubEntry> m_luaEventSubs;

    // Active Timers from Lua
    uint32_t m_nextTimerId{0};
    std::unordered_map<uint32_t, LuaTimerEntry> m_luaTimers;
    std::atomic<bool> m_timerRunning{false};
    std::thread m_timerThread;

    // Helper: Push yyjson_val to Lua stack
    static void PushJsonValToLua(lua_State* L, yyjson_val* val) {
        if (!val || yyjson_is_null(val)) {
            lua_pushnil(L);
        } else if (yyjson_is_bool(val)) {
            lua_pushboolean(L, yyjson_get_bool(val) ? 1 : 0);
        } else if (yyjson_is_int(val)) {
            lua_pushinteger(L, static_cast<lua_Integer>(yyjson_get_sint(val)));
        } else if (yyjson_is_uint(val)) {
            lua_pushnumber(L, static_cast<lua_Number>(yyjson_get_uint(val)));
        } else if (yyjson_is_real(val)) {
            lua_pushnumber(L, yyjson_get_real(val));
        } else if (yyjson_is_str(val)) {
            lua_pushstring(L, yyjson_get_str(val));
        } else if (yyjson_is_arr(val)) {
            lua_newtable(L);
            size_t idx = 0;
            yyjson_val* item = nullptr;
            yyjson_arr_iter iter;
            yyjson_arr_iter_init(val, &iter);
            while ((item = yyjson_arr_iter_next(&iter))) {
                PushJsonValToLua(L, item);
                lua_rawseti(L, -2, static_cast<int>(++idx));
            }
        } else if (yyjson_is_obj(val)) {
            lua_newtable(L);
            yyjson_val* k = nullptr;
            yyjson_val* v = nullptr;
            yyjson_obj_iter iter;
            yyjson_obj_iter_init(val, &iter);
            while ((k = yyjson_obj_iter_next(&iter))) {
                v = yyjson_obj_iter_get_val(k);
                const char* keyStr = yyjson_get_str(k);
                if (keyStr) {
                    lua_pushstring(L, keyStr);
                    PushJsonValToLua(L, v);
                    lua_rawset(L, -3);
                }
            }
        } else {
            lua_pushnil(L);
        }
    }

    // Helper: Serialize Lua table to yyjson_mut_val
    static yyjson_mut_val* LuaTableToMutVal(lua_State* L, int idx, yyjson_mut_doc* doc) {
        if (idx < 0 && idx > LUA_REGISTRYINDEX) {
            idx = lua_gettop(L) + idx + 1;
        }
        int t = lua_type(L, idx);
        if (t == LUA_TNIL) {
            return yyjson_mut_null(doc);
        } else if (t == LUA_TBOOLEAN) {
            return yyjson_mut_bool(doc, lua_toboolean(L, idx) != 0);
        } else if (t == LUA_TNUMBER) {
            lua_Number num = lua_tonumber(L, idx);
            lua_Integer intVal = lua_tointeger(L, idx);
            if (static_cast<lua_Number>(intVal) == num) {
                return yyjson_mut_sint(doc, intVal);
            } else {
                return yyjson_mut_real(doc, num);
            }
        } else if (t == LUA_TSTRING) {
            return yyjson_mut_str(doc, lua_tostring(L, idx));
        } else if (t == LUA_TTABLE) {
            size_t len = lua_objlen(L, idx);
            if (len > 0) {
                yyjson_mut_val* arr = yyjson_mut_arr(doc);
                for (size_t i = 1; i <= len; ++i) {
                    lua_rawgeti(L, idx, static_cast<int>(i));
                    yyjson_mut_val* item = LuaTableToMutVal(L, -1, doc);
                    yyjson_mut_arr_append(arr, item);
                    lua_pop(L, 1);
                }
                return arr;
            } else {
                yyjson_mut_val* obj = yyjson_mut_obj(doc);
                lua_pushnil(L);
                while (lua_next(L, idx) != 0) {
                    const char* key = lua_tostring(L, -2);
                    if (key) {
                        yyjson_mut_val* val = LuaTableToMutVal(L, -1, doc);
                        yyjson_mut_obj_add(obj, yyjson_mut_str(doc, key), val);
                    }
                    lua_pop(L, 1);
                }
                return obj;
            }
        }
        return yyjson_mut_null(doc);
    }

    // Dispatcher for in-engine Native MidHooks to Lua callbacks
    static void LuaMidHookDispatcher(DreamCpuContext& ctx, void* userData) {
        auto* entry = reinterpret_cast<LuaMidHookEntry*>(userData);
        if (!entry) return;

        auto& engine = DreamLuaEngine::Instance();
        std::lock_guard<std::recursive_mutex> lock(engine.GetMutex());
        lua_State* L = engine.GetState();
        if (!L || entry->callbackRef == LUA_NOREF) return;

        lua_rawgeti(L, LUA_REGISTRYINDEX, entry->callbackRef);
        if (!lua_isfunction(L, -1)) {
            lua_pop(L, 1);
            return;
        }

        // Push ctx table to Lua
        lua_newtable(L);
        auto pushHex = [&](const char* name, uint64_t val) {
            char buf[32];
            snprintf(buf, sizeof(buf), "0x%llX", static_cast<unsigned long long>(val));
            lua_pushstring(L, buf);
            lua_setfield(L, -2, name);
        };

        pushHex("rcx", ctx.rcx);
        pushHex("rdx", ctx.rdx);
        pushHex("r8", ctx.r8);
        pushHex("r9", ctx.r9);
        pushHex("rax", ctx.rax);
        pushHex("rbx", ctx.rbx);
        pushHex("rsi", ctx.rsi);
        pushHex("rdi", ctx.rdi);
        pushHex("rbp", ctx.rbp);
        pushHex("r10", ctx.r10);
        pushHex("r11", ctx.r11);
        pushHex("r12", ctx.r12);
        pushHex("r13", ctx.r13);
        pushHex("r14", ctx.r14);
        pushHex("r15", ctx.r15);
        lua_pushnumber(L, ctx.xmm[0].f32[0]); lua_setfield(L, -2, "xmm0");
        lua_pushnumber(L, ctx.xmm[1].f32[0]); lua_setfield(L, -2, "xmm1");

        DreamCrashGuard::ExecuteSafely("LuaMidHook", [&]() {
            if (lua_pcall(L, 1, 0, 0) != 0) {
                const char* err = lua_tostring(L, -1);
                DREAM_LOG_ERROR("[Lua MidHook Error] %s", err ? err : "Unknown");
                lua_pop(L, 1);
            }
        });
    }

    // Lua C functions
    static int Lua_Log(lua_State* L) {
        const char* msg = luaL_optstring(L, 1, "");
        DREAM_LOG_INFO("[Lua] %s", msg);
        return 0;
    }

    static int Lua_IsPlayerReady(lua_State* L) {
        lua_pushboolean(L, DreamGame::Instance().IsPlayerReady() ? 1 : 0);
        return 1;
    }

    static int Lua_GetPlayerPos(lua_State* L) {
        DreamVector3 pos = DreamGame::Instance().GetPlayerPosition();
        lua_newtable(L);
        lua_pushnumber(L, pos.x); lua_setfield(L, -2, "x");
        lua_pushnumber(L, pos.y); lua_setfield(L, -2, "y");
        lua_pushnumber(L, pos.z); lua_setfield(L, -2, "z");
        return 1;
    }

    static int Lua_Teleport(lua_State* L) {
        float x = static_cast<float>(luaL_checknumber(L, 1));
        float y = static_cast<float>(luaL_checknumber(L, 2));
        float z = static_cast<float>(luaL_checknumber(L, 3));
        bool ok = DreamGame::Instance().TeleportPlayer(x, y, z);
        lua_pushboolean(L, ok ? 1 : 0);
        return 1;
    }

    static int Lua_SetGodMode(lua_State* L) {
        bool en = lua_toboolean(L, 1) != 0;
        DreamGame::Instance().SetGodMode(en);
        return 0;
    }

    static int Lua_SetGhostMode(lua_State* L) {
        bool en = lua_toboolean(L, 1) != 0;
        DreamGame::Instance().SetGhostMode(en);
        return 0;
    }

    static int Lua_SetInfiniteStamina(lua_State* L) {
        bool en = lua_toboolean(L, 1) != 0;
        DreamGame::Instance().SetInfiniteStamina(en);
        return 0;
    }

    static int Lua_SetInfiniteMana(lua_State* L) {
        bool en = lua_toboolean(L, 1) != 0;
        DreamGame::Instance().SetInfiniteMana(en);
        return 0;
    }

    static int Lua_SetUnlimitedFlight(lua_State* L) {
        bool en = lua_toboolean(L, 1) != 0;
        DreamGame::Instance().SetUnlimitedFlight(en);
        return 0;
    }

    // Fast JSON parsing powered by yyjson!
    static int Lua_JsonParse(lua_State* L) {
        size_t len = 0;
        const char* jsonStr = luaL_checklstring(L, 1, &len);
        if (!jsonStr || len == 0) {
            lua_pushnil(L);
            return 1;
        }

        DreamJsonDocument doc = DreamJson::Parse(std::string_view(jsonStr, len));
        if (!doc.is_valid()) {
            lua_pushnil(L);
            lua_pushstring(L, "Failed to parse JSON string");
            return 2;
        }

        PushJsonValToLua(L, doc.root().raw());
        return 1;
    }

    // Fast JSON serialization powered by yyjson!
    static int Lua_JsonStringify(lua_State* L) {
        if (!lua_istable(L, 1)) {
            lua_pushstring(L, "{}");
            return 1;
        }
        bool pretty = lua_toboolean(L, 2) != 0;

        yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
        yyjson_mut_val* root = LuaTableToMutVal(L, 1, doc);
        yyjson_mut_doc_set_root(doc, root);

        size_t len = 0;
        yyjson_write_flag flg = pretty ? YYJSON_WRITE_PRETTY : 0;
        char* json = yyjson_mut_write(doc, flg, &len);
        if (json) {
            lua_pushlstring(L, json, len);
            free(json);
        } else {
            lua_pushstring(L, "{}");
        }

        yyjson_mut_doc_free(doc);
        return 1;
    }

    // ========================================================================
    // In-Engine Dynamic Reverse Engineering Lua APIs
    // Allows instant probing, memory reading/writing, AOB scanning, and RTTI
    // ========================================================================
    static int Lua_ReadU8(lua_State* L) {
        const char* addrStr = luaL_checkstring(L, 1);
        uintptr_t addr = std::strtoull(addrStr, nullptr, 0);
        uint8_t val = DreamMemory::Read<uint8_t>(reinterpret_cast<const void*>(addr), 0);
        lua_pushinteger(L, val);
        return 1;
    }

    static int Lua_ReadU16(lua_State* L) {
        const char* addrStr = luaL_checkstring(L, 1);
        uintptr_t addr = std::strtoull(addrStr, nullptr, 0);
        uint16_t val = DreamMemory::Read<uint16_t>(reinterpret_cast<const void*>(addr), 0);
        lua_pushinteger(L, val);
        return 1;
    }

    static int Lua_ReadU32(lua_State* L) {
        const char* addrStr = luaL_checkstring(L, 1);
        uintptr_t addr = std::strtoull(addrStr, nullptr, 0);
        uint32_t val = DreamMemory::Read<uint32_t>(reinterpret_cast<const void*>(addr), 0);
        lua_pushnumber(L, static_cast<lua_Number>(val));
        return 1;
    }

    static int Lua_ReadU64(lua_State* L) {
        const char* addrStr = luaL_checkstring(L, 1);
        uintptr_t addr = std::strtoull(addrStr, nullptr, 0);
        uint64_t val = DreamMemory::Read<uint64_t>(reinterpret_cast<const void*>(addr), 0);
        char buf[32];
        snprintf(buf, sizeof(buf), "0x%llX", static_cast<unsigned long long>(val));
        lua_pushstring(L, buf);
        return 1;
    }

    static int Lua_ReadFloat(lua_State* L) {
        const char* addrStr = luaL_checkstring(L, 1);
        uintptr_t addr = std::strtoull(addrStr, nullptr, 0);
        float val = DreamMemory::Read<float>(reinterpret_cast<const void*>(addr), 0.0f);
        lua_pushnumber(L, val);
        return 1;
    }

    static int Lua_ReadString(lua_State* L) {
        const char* addrStr = luaL_checkstring(L, 1);
        uintptr_t addr = std::strtoull(addrStr, nullptr, 0);
        size_t maxLen = static_cast<size_t>(luaL_optinteger(L, 2, 64));
        const void* ptr = reinterpret_cast<const void*>(addr);
        if (!MemoryHelper::IsValidPtr(ptr)) {
            lua_pushnil(L);
            return 1;
        }
        std::string s;
        DreamCrashGuard::ExecuteSafely("LuaReadString", [&]() {
            const char* strPtr = reinterpret_cast<const char*>(ptr);
            size_t len = 0;
            while (len < maxLen && strPtr[len] != '\0') len++;
            s.assign(strPtr, len);
        });
        lua_pushstring(L, s.c_str());
        return 1;
    }

    static int Lua_WriteFloat(lua_State* L) {
        const char* addrStr = luaL_checkstring(L, 1);
        uintptr_t addr = std::strtoull(addrStr, nullptr, 0);
        float val = static_cast<float>(luaL_checknumber(L, 2));
        bool ok = DreamMemory::Write<float>(reinterpret_cast<void*>(addr), val);
        lua_pushboolean(L, ok ? 1 : 0);
        return 1;
    }

    static int Lua_WriteU32(lua_State* L) {
        const char* addrStr = luaL_checkstring(L, 1);
        uintptr_t addr = std::strtoull(addrStr, nullptr, 0);
        uint32_t val = static_cast<uint32_t>(luaL_checkinteger(L, 2));
        bool ok = DreamMemory::Write<uint32_t>(reinterpret_cast<void*>(addr), val);
        lua_pushboolean(L, ok ? 1 : 0);
        return 1;
    }

    static int Lua_Rtti(lua_State* L) {
        const char* addrStr = luaL_checkstring(L, 1);
        uintptr_t addr = std::strtoull(addrStr, nullptr, 0);
        std::string cls = DreamRtti::GetClassName(reinterpret_cast<const void*>(addr));
        lua_pushstring(L, cls.c_str());
        return 1;
    }

    static int Lua_Scan(lua_State* L) {
        const char* pat = luaL_checkstring(L, 1);
        void* addr = DreamScanner::ScanModule(nullptr, pat);
        if (addr) {
            char buf[32];
            snprintf(buf, sizeof(buf), "0x%p", addr);
            lua_pushstring(L, buf);
        } else {
            lua_pushnil(L);
        }
        return 1;
    }

    static int Lua_SigGen(lua_State* L) {
        const char* addrStr = luaL_checkstring(L, 1);
        uintptr_t addr = std::strtoull(addrStr, nullptr, 0);
        size_t minLen = static_cast<size_t>(luaL_optinteger(L, 2, 24));
        auto sig = DreamScanner::GenerateSignature(reinterpret_cast<const void*>(addr), minLen);
        lua_newtable(L);
        lua_pushstring(L, sig.pattern.c_str()); lua_setfield(L, -2, "pattern");
        lua_pushinteger(L, sig.byteLength); lua_setfield(L, -2, "length");
        lua_pushboolean(L, sig.isUnique ? 1 : 0); lua_setfield(L, -2, "unique");
        lua_pushinteger(L, sig.matchCount); lua_setfield(L, -2, "matches");
        lua_pushstring(L, sig.cppSnippet.c_str()); lua_setfield(L, -2, "cpp");
        return 1;
    }

    static int Lua_Add(lua_State* L) {
        const char* addrStr = luaL_checkstring(L, 1);
        int64_t offset = static_cast<int64_t>(luaL_checknumber(L, 2));
        uintptr_t addr = std::strtoull(addrStr, nullptr, 0);
        uintptr_t result = addr + offset;
        char buf[32];
        snprintf(buf, sizeof(buf), "0x%llX", static_cast<unsigned long long>(result));
        lua_pushstring(L, buf);
        return 1;
    }

    static int Lua_Trace(lua_State* L) {
        const char* addrStr = luaL_checkstring(L, 1);
        const char* label = luaL_optstring(L, 2, "LuaTrace");
        uintptr_t addr = std::strtoull(addrStr, nullptr, 0);
        uint32_t id = DreamTracer::Instance().AddTrace(reinterpret_cast<void*>(addr), label);
        lua_pushinteger(L, id);
        return 1;
    }

    static int Lua_Untrace(lua_State* L) {
        uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
        bool ok = DreamTracer::Instance().RemoveTrace(id);
        lua_pushboolean(L, ok ? 1 : 0);
        return 1;
    }

    // ========================================================================
    // Native MidHook with Lua Callback
    // ========================================================================
    static int Lua_MidHook(lua_State* L) {
        const char* addrStr = luaL_checkstring(L, 1);
        if (!lua_isfunction(L, 2)) {
            luaL_error(L, "dream.midhook requires (address, callback_function)");
            return 0;
        }

        uintptr_t addr = std::strtoull(addrStr, nullptr, 0);
        if (!MemoryHelper::IsValidPtr(reinterpret_cast<const void*>(addr))) {
            luaL_error(L, "dream.midhook: Invalid address pointer '%s'", addrStr);
            return 0;
        }

        lua_pushvalue(L, 2);
        int ref = luaL_ref(L, LUA_REGISTRYINDEX);

        auto& engine = DreamLuaEngine::Instance();
        std::lock_guard<std::recursive_mutex> lock(engine.GetMutex());
        uint32_t id = ++engine.m_nextHookId;

        auto entry = std::make_unique<LuaMidHookEntry>();
        entry->id = id;
        entry->address = reinterpret_cast<void*>(addr);
        entry->callbackRef = ref;
        entry->hook = std::make_unique<DreamMidHook>();

        bool ok = entry->hook->Install(entry->address, LuaMidHookDispatcher, entry.get());
        if (!ok) {
            luaL_unref(L, LUA_REGISTRYINDEX, ref);
            lua_pushnil(L);
            lua_pushstring(L, "Failed to install MidHook on address");
            return 2;
        }

        engine.m_luaHooks[id] = std::move(entry);
        DREAM_LOG_INFO("[DreamLua] Installed Native MidHook #%u at 0x%p", id, reinterpret_cast<void*>(addr));
        lua_pushinteger(L, id);
        return 1;
    }

    static int Lua_Unhook(lua_State* L) {
        uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
        auto& engine = DreamLuaEngine::Instance();
        std::lock_guard<std::recursive_mutex> lock(engine.GetMutex());
        auto it = engine.m_luaHooks.find(id);
        if (it != engine.m_luaHooks.end()) {
            if (it->second->callbackRef != LUA_NOREF) {
                luaL_unref(L, LUA_REGISTRYINDEX, it->second->callbackRef);
            }
            engine.m_luaHooks.erase(it);
            DREAM_LOG_INFO("[DreamLua] Uninstalled Native MidHook #%u", id);
            lua_pushboolean(L, 1);
            return 1;
        }
        lua_pushboolean(L, 0);
        return 1;
    }

    // ========================================================================
    // EventBus Pub/Sub Integration in Lua
    // ========================================================================
    static int Lua_OnEvent(lua_State* L) {
        const char* evName = luaL_checkstring(L, 1);
        if (!lua_isfunction(L, 2)) {
            luaL_error(L, "dream.on requires (eventName, callback_function)");
            return 0;
        }
        lua_pushvalue(L, 2);
        int ref = luaL_ref(L, LUA_REGISTRYINDEX);

        auto& engine = DreamLuaEngine::Instance();
        std::lock_guard<std::recursive_mutex> lock(engine.GetMutex());

        uint64_t token = DreamEventBus::Instance().Subscribe(evName, [ref, nameStr = std::string(evName)](const DreamEvent& ev) {
            auto& lua = DreamLuaEngine::Instance();
            std::lock_guard<std::recursive_mutex> lock(lua.GetMutex());
            lua_State* L = lua.GetState();
            if (!L) return;

            lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
            if (!lua_isfunction(L, -1)) {
                lua_pop(L, 1);
                return;
            }

            std::string payload = ev.GetString();
            if (!payload.empty() && (payload.front() == '{' || payload.front() == '[')) {
                DreamJsonDocument doc = DreamJson::Parse(payload);
                if (doc.is_valid()) {
                    PushJsonValToLua(L, doc.root().raw());
                } else {
                    lua_pushstring(L, payload.c_str());
                }
            } else {
                lua_pushstring(L, payload.c_str());
            }

            DreamCrashGuard::ExecuteSafely("LuaEventBusCallback", [&]() {
                if (lua_pcall(L, 1, 0, 0) != 0) {
                    const char* err = lua_tostring(L, -1);
                    DREAM_LOG_ERROR("[Lua Event Error '%s'] %s", nameStr.c_str(), err ? err : "Unknown");
                    lua_pop(L, 1);
                }
            });
        });

        engine.m_luaEventSubs.push_back({ token, ref });
        lua_pushnumber(L, static_cast<lua_Number>(token));
        return 1;
    }

    static int Lua_EmitEvent(lua_State* L) {
        const char* evName = luaL_checkstring(L, 1);
        std::string payload;

        if (lua_istable(L, 2)) {
            yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
            yyjson_mut_val* root = LuaTableToMutVal(L, 2, doc);
            yyjson_mut_doc_set_root(doc, root);
            size_t len = 0;
            char* json = yyjson_mut_write(doc, 0, &len);
            if (json) {
                payload.assign(json, len);
                free(json);
            }
            yyjson_mut_doc_free(doc);
        } else if (lua_isstring(L, 2)) {
            payload = lua_tostring(L, 2);
        }

        DreamEventBus::Instance().Publish(evName, payload.data(), payload.size());
        return 0;
    }

    // ========================================================================
    // Recurring Timer System (dream.set_interval / clear_interval)
    // ========================================================================
    static int Lua_SetInterval(lua_State* L) {
        if (!lua_isfunction(L, 1)) {
            luaL_error(L, "dream.set_interval requires (callback_function, intervalMs)");
            return 0;
        }
        uint32_t ms = static_cast<uint32_t>(luaL_optinteger(L, 2, 1000));
        lua_pushvalue(L, 1);
        int ref = luaL_ref(L, LUA_REGISTRYINDEX);

        auto& engine = DreamLuaEngine::Instance();
        std::lock_guard<std::recursive_mutex> lock(engine.GetMutex());
        uint32_t id = ++engine.m_nextTimerId;

        LuaTimerEntry timer;
        timer.id = id;
        timer.callbackRef = ref;
        timer.intervalMs = ms;
        timer.nextTrigger = GetTickCount64() + ms;

        engine.m_luaTimers[id] = timer;
        lua_pushinteger(L, id);
        return 1;
    }

    static int Lua_ClearInterval(lua_State* L) {
        uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
        auto& engine = DreamLuaEngine::Instance();
        std::lock_guard<std::recursive_mutex> lock(engine.GetMutex());
        auto it = engine.m_luaTimers.find(id);
        if (it != engine.m_luaTimers.end()) {
            if (it->second.callbackRef != LUA_NOREF) {
                luaL_unref(L, LUA_REGISTRYINDEX, it->second.callbackRef);
            }
            engine.m_luaTimers.erase(it);
            lua_pushboolean(L, 1);
            return 1;
        }
        lua_pushboolean(L, 0);
        return 1;
    }

public:
    static DreamLuaEngine& Instance() {
        static DreamLuaEngine s_instance;
        return s_instance;
    }

    std::recursive_mutex& GetMutex() { return m_mutex; }

    bool Initialize() {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (m_initialized && m_L) return true;

        m_L = luaL_newstate();
        if (!m_L) {
            DREAM_LOG_ERROR("Failed to create Lua state!");
            return false;
        }

        luaL_openlibs(m_L);

        // Turn on LuaJIT engine mode
        luaJIT_setmode(m_L, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_ON);

        // Register built-in "dream" table
        lua_newtable(m_L);

        lua_pushcfunction(m_L, Lua_Log); lua_setfield(m_L, -2, "log");
        lua_pushcfunction(m_L, Lua_IsPlayerReady); lua_setfield(m_L, -2, "is_player_ready");
        lua_pushcfunction(m_L, Lua_GetPlayerPos); lua_setfield(m_L, -2, "get_player_pos");
        lua_pushcfunction(m_L, Lua_Teleport); lua_setfield(m_L, -2, "teleport");
        lua_pushcfunction(m_L, Lua_SetGodMode); lua_setfield(m_L, -2, "set_god_mode");
        lua_pushcfunction(m_L, Lua_SetGhostMode); lua_setfield(m_L, -2, "set_ghost_mode");
        lua_pushcfunction(m_L, Lua_SetInfiniteStamina); lua_setfield(m_L, -2, "set_infinite_stamina");
        lua_pushcfunction(m_L, Lua_SetInfiniteMana); lua_setfield(m_L, -2, "set_infinite_mana");
        lua_pushcfunction(m_L, Lua_SetUnlimitedFlight); lua_setfield(m_L, -2, "set_unlimited_flight");

        // Fast JSON APIs
        lua_pushcfunction(m_L, Lua_JsonParse); lua_setfield(m_L, -2, "json_parse");
        lua_pushcfunction(m_L, Lua_JsonStringify); lua_setfield(m_L, -2, "json_stringify");

        // Reverse Engineering Memory APIs
        lua_pushcfunction(m_L, Lua_ReadU8); lua_setfield(m_L, -2, "read_u8");
        lua_pushcfunction(m_L, Lua_ReadU16); lua_setfield(m_L, -2, "read_u16");
        lua_pushcfunction(m_L, Lua_ReadU32); lua_setfield(m_L, -2, "read_u32");
        lua_pushcfunction(m_L, Lua_ReadU64); lua_setfield(m_L, -2, "read_u64");
        lua_pushcfunction(m_L, Lua_ReadFloat); lua_setfield(m_L, -2, "read_float");
        lua_pushcfunction(m_L, Lua_ReadString); lua_setfield(m_L, -2, "read_string");
        lua_pushcfunction(m_L, Lua_WriteFloat); lua_setfield(m_L, -2, "write_float");
        lua_pushcfunction(m_L, Lua_WriteU32); lua_setfield(m_L, -2, "write_u32");
        lua_pushcfunction(m_L, Lua_Rtti); lua_setfield(m_L, -2, "rtti");
        lua_pushcfunction(m_L, Lua_Scan); lua_setfield(m_L, -2, "scan");
        lua_pushcfunction(m_L, Lua_SigGen); lua_setfield(m_L, -2, "sig_gen");
        lua_pushcfunction(m_L, Lua_Add); lua_setfield(m_L, -2, "add");

        // Native MidHooks and Traces
        lua_pushcfunction(m_L, Lua_Trace); lua_setfield(m_L, -2, "trace");
        lua_pushcfunction(m_L, Lua_Untrace); lua_setfield(m_L, -2, "untrace");
        lua_pushcfunction(m_L, Lua_MidHook); lua_setfield(m_L, -2, "midhook");
        lua_pushcfunction(m_L, Lua_Unhook); lua_setfield(m_L, -2, "unhook");

        // EventBus Integration
        lua_pushcfunction(m_L, Lua_OnEvent); lua_setfield(m_L, -2, "on");
        lua_pushcfunction(m_L, Lua_EmitEvent); lua_setfield(m_L, -2, "emit");

        // Recurring Timers
        lua_pushcfunction(m_L, Lua_SetInterval); lua_setfield(m_L, -2, "set_interval");
        lua_pushcfunction(m_L, Lua_ClearInterval); lua_setfield(m_L, -2, "clear_interval");

        lua_setglobal(m_L, "dream");

        // Start background timer thread
        if (!m_timerRunning.load()) {
            m_timerRunning = true;
            m_timerThread = std::thread([this]() {
                while (m_timerRunning.load()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    if (!m_timerRunning.load()) break;

                    uint64_t now = GetTickCount64();
                    std::vector<int> callbacksToFire;

                    {
                        std::lock_guard<std::recursive_mutex> lock(m_mutex);
                        if (!m_L) continue;
                        for (auto& [id, timer] : m_luaTimers) {
                            if (now >= timer.nextTrigger) {
                                callbacksToFire.push_back(timer.callbackRef);
                                timer.nextTrigger = now + timer.intervalMs;
                            }
                        }
                    }

                    if (!callbacksToFire.empty()) {
                        std::lock_guard<std::recursive_mutex> lock(m_mutex);
                        if (!m_L) continue;
                        for (int ref : callbacksToFire) {
                            lua_rawgeti(m_L, LUA_REGISTRYINDEX, ref);
                            if (lua_isfunction(m_L, -1)) {
                                DreamCrashGuard::ExecuteSafely("LuaTimer", [&]() {
                                    if (lua_pcall(m_L, 0, 0, 0) != 0) {
                                        const char* err = lua_tostring(m_L, -1);
                                        DREAM_LOG_ERROR("[Lua Timer Error] %s", err ? err : "Unknown");
                                        lua_pop(m_L, 1);
                                    }
                                });
                            } else {
                                lua_pop(m_L, 1);
                            }
                        }
                    }
                }
            });
        }

        m_initialized = true;
        DREAM_LOG_INFO("LuaJIT 2.1 Engine initialized successfully with JIT, yyjson, Native MidHooks & RE Suite!");
        return true;
    }

    // Reset runtime state: cleanly removes all Lua midhooks, EventBus listeners and timers!
    void ResetRuntimeState() {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (!m_L) return;

        // 1. Remove all Lua midhooks
        for (auto& [id, entry] : m_luaHooks) {
            if (entry->callbackRef != LUA_NOREF) {
                luaL_unref(m_L, LUA_REGISTRYINDEX, entry->callbackRef);
            }
        }
        m_luaHooks.clear();

        // 2. Unsubscribe all EventBus listeners
        for (const auto& sub : m_luaEventSubs) {
            DreamEventBus::Instance().Unsubscribe(sub.tokenId);
            if (sub.callbackRef != LUA_NOREF) {
                luaL_unref(m_L, LUA_REGISTRYINDEX, sub.callbackRef);
            }
        }
        m_luaEventSubs.clear();

        // 3. Clear all timers
        for (auto& [id, timer] : m_luaTimers) {
            if (timer.callbackRef != LUA_NOREF) {
                luaL_unref(m_L, LUA_REGISTRYINDEX, timer.callbackRef);
            }
        }
        m_luaTimers.clear();

        DREAM_LOG_INFO("[DreamLua] Runtime state reset cleanly (all Lua hooks, events, timers cleared).");
    }

    void Shutdown() {
        m_timerRunning = false;
        if (m_timerThread.joinable()) {
            m_timerThread.join();
        }

        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        ResetRuntimeState();

        if (m_L) {
            lua_close(m_L);
            m_L = nullptr;
        }
        m_initialized = false;
    }

    bool ExecuteString(const std::string& script, std::string* errorOut = nullptr) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (!m_initialized && !Initialize()) return false;

        bool success = false;
        DreamCrashGuard::Execute("DreamLua", [&]() {
            int status = luaL_loadstring(m_L, script.c_str());
            if (status == 0) {
                status = lua_pcall(m_L, 0, LUA_MULTRET, 0);
            }

            if (status != 0) {
                const char* err = lua_tostring(m_L, -1);
                std::string errStr = err ? err : "Unknown Lua error";
                if (errorOut) *errorOut = errStr;
                DREAM_LOG_ERROR("[Lua Error] %s", errStr.c_str());
                lua_pop(m_L, 1);
                success = false;
            } else {
                success = true;
            }
        }, [&]() {
            if (errorOut) *errorOut = "Lua execution intercepted by CrashGuard";
            success = false;
        });

        return success;
    }

    bool ExecuteFile(const std::string& filepath, std::string* errorOut = nullptr) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (!m_initialized && !Initialize()) return false;

        bool success = false;
        DreamCrashGuard::Execute("DreamLua", [&]() {
            int status = luaL_loadfile(m_L, filepath.c_str());
            if (status == 0) {
                status = lua_pcall(m_L, 0, LUA_MULTRET, 0);
            }

            if (status != 0) {
                const char* err = lua_tostring(m_L, -1);
                std::string errStr = err ? err : "Unknown Lua file error";
                if (errorOut) *errorOut = errStr;
                DREAM_LOG_ERROR("[Lua File Error] %s", errStr.c_str());
                lua_pop(m_L, 1);
                success = false;
            } else {
                success = true;
            }
        }, [&]() {
            if (errorOut) *errorOut = "Lua file execution intercepted by CrashGuard";
            success = false;
        });

        return success;
    }

    void RegisterFunction(const std::string& name, lua_CFunction func) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (!m_initialized && !Initialize()) return;
        lua_register(m_L, name.c_str(), func);
    }

    size_t LoadScriptsFromDirectory(const std::string& dirPath) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (!m_initialized && !Initialize()) return 0;
        size_t count = 0;
        std::string searchPattern = dirPath + "/*.lua";
        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA(searchPattern.c_str(), &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    std::string fullPath = dirPath + "/" + fd.cFileName;
                    std::string err;
                    if (ExecuteFile(fullPath, &err)) {
                        DREAM_LOG_INFO("DreamLua: Loaded script: %s", fd.cFileName);
                        count++;
                    } else {
                        DREAM_LOG_WARN("DreamLua: Failed script %s: %s", fd.cFileName, err.c_str());
                    }
                }
            } while (FindNextFileA(hFind, &fd));
            FindClose(hFind);
        }
        return count;
    }

    lua_State* GetState() const { return m_L; }
    bool IsInitialized() const { return m_initialized; }
};

#define DreamLua dreamkurumi::DreamLuaEngine::Instance()

} // namespace dreamkurumi

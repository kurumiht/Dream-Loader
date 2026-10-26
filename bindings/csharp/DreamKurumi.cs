// ============================================================================
// DreamKurumi.cs - C# .NET Binding for dreamkurumiHT Framework
//
// Allows C# / .NET mod developers to write high-performance native hooks
// with full Harmony-style semantics via P/Invoke!
// ============================================================================

using System;
using System.Runtime.InteropServices;

namespace DreamKurumi
{
    public static class DreamEngine
    {
        private const string DllName = "dreamkurumi.dll";

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern bool DK_Initialize([MarshalAs(UnmanagedType.LPStr)] string cachePath);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern ulong DK_PatchAll();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void DK_UnpatchAll();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr DK_Resolve([MarshalAs(UnmanagedType.LPStr)] string symbolName, ulong fallbackRva);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr DK_ResolvePattern([MarshalAs(UnmanagedType.LPStr)] string symbolName, [MarshalAs(UnmanagedType.LPStr)] string pattern, ulong fallbackRva);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern ulong DK_GetModuleBase();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern uint DK_GetGameBuildTimestamp();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr DK_InlineHook(IntPtr target, IntPtr detour, out IntPtr original);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern bool DK_Unhook(IntPtr hookHandle);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr DK_Nop(IntPtr target, ulong count);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern bool DK_RestorePatch(IntPtr patchHandle);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void DK_Log(int level, [MarshalAs(UnmanagedType.LPStr)] string msg);

        // ====================================================================
        // Presets & Overlay
        // ====================================================================
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void DK_Preset_SetGodMode(bool enable);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void DK_Preset_SetGhostMode(bool enable);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void DK_Preset_SetInfiniteStamina(bool enable);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void DK_Preset_SetInfiniteMana(bool enable);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void DK_Preset_SetInfiniteShroud(bool enable);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void DK_Preset_SetFreeCraft(bool enable);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void DK_Preset_SetSuperMining(bool enable, float mult);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void DK_Preset_SetUnlimitedFlight(bool enable);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void DK_Preset_SetInfiniteItemSplit(bool enable);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void DK_Preset_SetInfiniteItemUse(bool enable);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void DK_Preset_SetNoResourceCost(bool enable);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void DK_Preset_SetInfiniteDurability(bool enable);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void DK_Preset_SetOneShotKill(bool enable);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void DK_Preset_SetFastProduction(bool enable, float seconds);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern bool DK_Preset_SaveConfig([MarshalAs(UnmanagedType.LPStr)] string path);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern bool DK_Preset_LoadConfig([MarshalAs(UnmanagedType.LPStr)] string path);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern bool DK_Preset_IsDedicatedServer();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void DK_Overlay_ToggleMenu();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern bool DK_Overlay_IsMenuOpen();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern bool DK_Overlay_ExecuteCommand([MarshalAs(UnmanagedType.LPStr)] string cmd);

        // ====================================================================
        // Server Policy & Anti-Cheat
        // ====================================================================
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void DK_Policy_EnforceStrictRules();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void DK_Policy_AllowAllFeatures();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void DK_Policy_SetFeatureAllowed(int feature, bool allowed);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern bool DK_Policy_IsFeatureAllowed(int feature);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern bool DK_Policy_SavePolicy([MarshalAs(UnmanagedType.LPStr)] string path);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern bool DK_Policy_LoadPolicy([MarshalAs(UnmanagedType.LPStr)] string path);

        // ====================================================================
        // Micro-Profiler
        // ====================================================================
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void DK_Profiler_PrintReport();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void DK_Profiler_Reset();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void DK_Profiler_SetEnabled(bool enable);

        // ====================================================================
        // Environment & Dual-Side Detection
        // ====================================================================
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern bool DK_Env_IsDedicatedServer();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern bool DK_Env_IsClient();

        // ====================================================================
        // Compile-Time / Runtime 64-bit Hash Engine
        // ====================================================================
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern ulong DK_Hash_Fnv1a64([MarshalAs(UnmanagedType.LPStr)] string str);

        // ====================================================================
        // Watchdog (Anti-Hang & Deadlock Monitor)
        // ====================================================================
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void DK_Watchdog_Heartbeat();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void DK_Watchdog_RegisterThread([MarshalAs(UnmanagedType.LPStr)] string name);

        // ====================================================================
        // NetBridge (Client <-> Dedicated Server Mod Sync)
        // ====================================================================
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern bool DK_Net_SendToServer(ulong channelHash, ushort opcode, byte[] payload, uint payloadSize);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern bool DK_Net_BroadcastToClients(ulong channelHash, ushort opcode, byte[] payload, uint payloadSize);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void DK_Net_SyncSetVar([MarshalAs(UnmanagedType.LPStr)] string key, [MarshalAs(UnmanagedType.LPStr)] string value);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern bool DK_Net_SyncGetVar([MarshalAs(UnmanagedType.LPStr)] string key, byte[] outBuffer, uint outSize);

        // ====================================================================
        // GameContext (Service Locator & High-Level Gameplay API)
        // ====================================================================
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr DK_Game_GetLocalPlayer();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void DK_Game_SetLocalPlayer(IntPtr playerPtr);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern bool DK_Game_IsPlayerReady();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern bool DK_Game_TeleportPlayer(float x, float y, float z);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void DK_Game_SetGodMode(bool enabled);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void DK_Game_SetGhostMode(bool enabled);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void DK_Game_SetInfiniteStamina(bool enabled);

        /// <summary>
        /// Installs a type-safe hook using a C# Delegate
        /// </summary>
        public static HookHandle? Hook<TDelegate>(IntPtr target, TDelegate detour, out TDelegate? original) where TDelegate : Delegate
        {
            IntPtr detourPtr = Marshal.GetFunctionPointerForDelegate(detour);
            IntPtr hookHandle = DK_InlineHook(target, detourPtr, out IntPtr origPtr);
            
            if (hookHandle == IntPtr.Zero)
            {
                original = null;
                return null;
            }

            original = Marshal.GetDelegateForFunctionPointer<TDelegate>(origPtr);
            return new HookHandle(hookHandle, detour); // Retain detour reference to prevent GC
        }

        public class HookHandle : IDisposable
        {
            private IntPtr _handle;
            private object? _detourPin; // Keep alive in garbage collector

            internal HookHandle(IntPtr handle, object detourPin)
            {
                _handle = handle;
                _detourPin = detourPin;
            }

            public void Dispose()
            {
                if (_handle != IntPtr.Zero)
                {
                    DK_Unhook(_handle);
                    _handle = IntPtr.Zero;
                    _detourPin = null;
                }
            }
        }
    }

    public enum PresetFeature
    {
        GodMode = 0,
        GhostMode = 1,
        InfiniteStamina = 2,
        InfiniteMana = 3,
        InfiniteShroud = 4,
        FreeCraft = 5,
        SuperMining = 6,
        UnlimitedFlight = 7,
        InfiniteItemSplit = 8,
        InfiniteItemUse = 9,
        NoResourceCost = 10,
        InfiniteDurability = 11,
        OneShotKill = 12,
        FastProduction = 13
    }
}

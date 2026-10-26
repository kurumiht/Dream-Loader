using System;
using System.Runtime.InteropServices;
using DreamKurumi;

namespace MyDreamModCSharp
{
    // ========================================================================
    // XHL Native Plugin ABI (Struct definition for Native AOT)
    // ========================================================================
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public unsafe struct XhlPluginQueryInfo
    {
        public uint structSize;
        public uint abiVersion;
        public uint reserved;
        public fixed byte id[64];
        public fixed byte name[128];
        public fixed byte version[32];
    }

    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct XhlPluginContext
    {
        public uint structSize;
        public uint apiVersion;
        public IntPtr gameModule;
        public ulong reserved;
        public uint processId;
        public uint padding;
        public delegate* unmanaged[Cdecl]<byte*, void> logCallback;
    }

    // ========================================================================
    // Native Entry Points (Exported as pure unmanaged C functions via Native AOT)
    // ========================================================================
    public static unsafe class ModMain
    {
        [UnmanagedCallersOnly(EntryPoint = "XhlNativePluginQuery")]
        public static int XhlNativePluginQuery(XhlPluginQueryInfo* info)
        {
            if (info == null) return 0;
            
            info->structSize = (uint)sizeof(XhlPluginQueryInfo);
            info->abiVersion = 0x10000;
            
            WriteString(info->id, 64, "my-dream-mod-csharp");
            WriteString(info->name, 128, "My Dream Mod (C# .NET 8/10)");
            WriteString(info->version, 32, "1.0.0");
            
            return 1;
        }

        [UnmanagedCallersOnly(EntryPoint = "XhlNativePluginStart")]
        public static int XhlNativePluginStart(XhlPluginContext* ctx)
        {
            DreamEngine.DK_Log(2, "My Dream Mod (C# .NET 8/10 NativeAOT) started!");
            
            // Initialize dreamkurumi cache
            DreamEngine.DK_Initialize("dreamkurumi_cache.ini");
            
            // Example: Enable hotkey watcher and console in C#
            DreamEngine.DK_Overlay_ExecuteCommand("help");
            
            return 1;
        }

        [UnmanagedCallersOnly(EntryPoint = "XhlNativePluginRequestStop")]
        public static void XhlNativePluginRequestStop()
        {
            DreamEngine.DK_Log(2, "My Dream Mod (C#) stopped cleanly.");
            DreamEngine.DK_UnpatchAll();
        }

        private static void WriteString(byte* dest, int maxLen, string str)
        {
            byte[] bytes = System.Text.Encoding.UTF8.GetBytes(str);
            int len = Math.Min(bytes.Length, maxLen - 1);
            for (int i = 0; i < len; i++) dest[i] = bytes[i];
            dest[len] = 0;
        }
    }
}

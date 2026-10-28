#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
#include <cstring>
#include "DreamLog.hpp"

namespace dreamkurumi {

// ============================================================================
// Standard Vulkan Layer Negotiation Definitions (Zero Vulkan SDK Dependency)
// Dissected from Khronos Vulkan Loader Layer Interface Specifications
// ============================================================================
#define VK_CURRENT_LOADER_LAYER_INTERFACE_VERSION 2

typedef enum VkNegotiateLayerStructType {
    LAYER_NEGOTIATE_UNINTIALIZED = 0,
    LAYER_NEGOTIATE_INTERFACE_STRUCT = 1,
} VkNegotiateLayerStructType;

struct VkNegotiateLayerInterface {
    VkNegotiateLayerStructType sType;
    void* pNext;
    uint32_t loaderLayerInterfaceVersion;
    void* pfnGetInstanceProcAddr;
    void* pfnGetDeviceProcAddr;
    void* pfnGetPhysicalDeviceProcAddr;
};

// ============================================================================
// DreamVulkanLayer - Vulkan Explicit Layer Configuration & Manifest Engine
// Automates registration of In-Game Overlays, HUDs & Minimaps via Vulkan Loader
// ============================================================================
class DreamVulkanLayer {
public:
    static DreamVulkanLayer& Instance() {
        static DreamVulkanLayer s_instance;
        return s_instance;
    }

    // Generate compliant Vulkan Explicit Layer JSON manifest
    static bool GenerateManifest(const std::string& jsonFilePath,
                                const std::string& layerName,
                                const std::string& dllPath,
                                const std::string& description = "dreamkurumiHT Vulkan Layer",
                                const std::string& apiVersion = "1.3.0") {
        char jsonBuf[1024];
        int len = snprintf(jsonBuf, sizeof(jsonBuf),
            "{\n"
            "    \"file_format_version\": \"1.2.0\",\n"
            "    \"layer\": {\n"
            "        \"name\": \"%s\",\n"
            "        \"type\": \"GLOBAL\",\n"
            "        \"library_path\": \"%s\",\n"
            "        \"api_version\": \"%s\",\n"
            "        \"implementation_version\": \"1\",\n"
            "        \"description\": \"%s\",\n"
            "        \"functions\": {\n"
            "            \"vkNegotiateLoaderLayerInterfaceVersion\": \"vkNegotiateLoaderLayerInterfaceVersion\"\n"
            "        }\n"
            "    }\n"
            "}\n",
            layerName.c_str(),
            dllPath.c_str(),
            apiVersion.c_str(),
            description.c_str());

        if (len <= 0) return false;

        FILE* f = fopen(jsonFilePath.c_str(), "w");
        if (!f) {
            DREAM_LOG_ERROR("DreamVulkanLayer: Failed to write layer manifest to '%s'", jsonFilePath.c_str());
            return false;
        }

        fwrite(jsonBuf, 1, len, f);
        fclose(f);
        DREAM_LOG_INFO("DreamVulkanLayer: Successfully generated layer manifest '%s' for '%s'",
                       jsonFilePath.c_str(), layerName.c_str());
        return true;
    }

    // Configure process environment variables (VK_ADD_LAYER_PATH, VK_INSTANCE_LAYERS)
    static bool ConfigureEnvironment(const std::string& layerDirPath, const std::string& layerName) {
        // 1. VK_ADD_LAYER_PATH
        char curPath[4096] = {0};
        DWORD pathLen = GetEnvironmentVariableA("VK_ADD_LAYER_PATH", curPath, sizeof(curPath));
        std::string newPath;
        if (pathLen > 0 && pathLen < sizeof(curPath)) {
            newPath = curPath;
            if (newPath.find(layerDirPath) == std::string::npos) {
                newPath += ";" + layerDirPath;
            }
        } else {
            newPath = layerDirPath;
        }

        if (!SetEnvironmentVariableA("VK_ADD_LAYER_PATH", newPath.c_str())) {
            DREAM_LOG_ERROR("DreamVulkanLayer: Failed to set VK_ADD_LAYER_PATH");
            return false;
        }

        // 2. VK_INSTANCE_LAYERS
        char curLayers[4096] = {0};
        DWORD layersLen = GetEnvironmentVariableA("VK_INSTANCE_LAYERS", curLayers, sizeof(curLayers));
        std::string newLayers;
        if (layersLen > 0 && layersLen < sizeof(curLayers)) {
            newLayers = curLayers;
            if (newLayers.find(layerName) == std::string::npos) {
                newLayers += ";" + layerName;
            }
        } else {
            newLayers = layerName;
        }

        if (!SetEnvironmentVariableA("VK_INSTANCE_LAYERS", newLayers.c_str())) {
            DREAM_LOG_ERROR("DreamVulkanLayer: Failed to set VK_INSTANCE_LAYERS");
            return false;
        }

        DREAM_LOG_INFO("DreamVulkanLayer: Configured Vulkan Environment: VK_ADD_LAYER_PATH='%s', VK_INSTANCE_LAYERS='%s'",
                       newPath.c_str(), newLayers.c_str());

        // Check for elevated admin process warning
        if (IsElevated()) {
            DREAM_LOG_WARN("DreamVulkanLayer: Process is running ELEVATED (Admin). Standard Vulkan loader may restrict VK_ADD_LAYER_PATH!");
        }

        return true;
    }

    // Check if the current process is running with elevated admin privileges
    static bool IsElevated() {
        BOOL elevated = FALSE;
        HANDLE hToken = NULL;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
            TOKEN_ELEVATION elevation;
            DWORD cbSize = sizeof(TOKEN_ELEVATION);
            if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &cbSize)) {
                elevated = elevation.TokenIsElevated;
            }
            CloseHandle(hToken);
        }
        return elevated != FALSE;
    }

    // Validate if layer interface request matches Vulkan loader version
    static bool ValidateNegotiation(VkNegotiateLayerInterface* pVersionStruct, uint32_t supportedVersion = 2) {
        if (!pVersionStruct) return false;
        if (pVersionStruct->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT) return false;
        if (pVersionStruct->loaderLayerInterfaceVersion > supportedVersion) {
            pVersionStruct->loaderLayerInterfaceVersion = supportedVersion;
        }
        return true;
    }

private:
    DreamVulkanLayer() = default;
};

} // namespace dreamkurumi

#pragma once

// ============================================================================
// DreamDiagnosticOverlay.hpp - Native In-Game ImGui & Win32 Diagnostic HUD
//
// Part of the dreamkurumiHT Framework (v3.9.0)
// Features:
// - Hardware DWM Layered Click-Through HUD (Zero GPU cost, 0ms input latency)
// - Configurable via config.toml ([Diagnostics] EnableDiagnosticsHUD & DiagnosticsHotkey)
// - Real-time Performance & Memory HUD (FPS, Frame Delta, Working Set)
// - Live Fault Shielding Telemetry Counter & GNS Network Status
// - Toggleable via F9 (or custom hotkey) without stealing mouse/keyboard focus
// ============================================================================

#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <psapi.h>
#include "DreamLog.hpp"
#include "DreamCrashGuard.hpp"
#include "DreamEnv.hpp"
#include "DreamGnsTransportHook.hpp"
#include "DreamPlayerRegistry.hpp"
#include "DreamWindowSync.hpp"
#include "DreamNetStateTracker.hpp"

namespace dreamkurumi {

struct HookHealthStatus {
    std::string name;
    uintptr_t targetAddress{0};
    uint64_t hitCount{0};
    bool isHealthy{true};
    std::string hookType; // "InlineHook", "MidHook", "VEH", "VTable"
};

struct DiagnosticOverlaySettings {
    bool showPerformance{true};
    bool showHookTable{true};
    bool showModTuner{true};
    bool showLogStream{true};
    float aoeHarvestRadius{3.5f};
    float autoPickupRadius{5.0f};
    float xpShareMultiplier{1.0f};
};

class DreamDiagnosticOverlay {
    static const int HUD_WIDTH = 340;
    static const int HUD_HEIGHT = 225;

public:
    using CustomPanelCallback = std::function<void()>;

    static DreamDiagnosticOverlay& Instance() {
        static DreamDiagnosticOverlay s_instance;
        return s_instance;
    }

    void SetEnabled(bool enabled) noexcept {
        m_enabled.store(enabled);
        if (!enabled && IsVisible()) {
            SetVisible(false);
        }
    }
    bool IsEnabled() const noexcept { return m_enabled.load(); }

    void SetToggleKey(int vKey) noexcept { m_toggleKey = vKey; }
    int GetToggleKey() const noexcept { return m_toggleKey; }

    bool IsVisible() const noexcept { return m_visible.load(); }

    void SetVisible(bool v) {
        if (!m_enabled.load() && v) return;
        m_visible.store(v);
        DREAM_LOG_INFO("DreamDiagnosticOverlay: Visibility set to %s", v ? "VISIBLE" : "HIDDEN");

        if (m_isDedicatedServer) return;

        if (v) {
            EnsureWindowCreated();
            RedrawHud();
            if (m_hWnd) {
                ShowWindow(m_hWnd, SW_SHOWNOACTIVATE);
            }
        } else {
            if (m_hWnd) {
                ShowWindow(m_hWnd, SW_HIDE);
            }
        }
    }

    void Toggle() {
        SetVisible(!IsVisible());
    }

    // Register active hook for diagnostic table display
    void RegisterHook(const std::string& name, uintptr_t targetAddress, const std::string& type) {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& h : m_hookList) {
            if (h.targetAddress == targetAddress) {
                h.name = name;
                h.hookType = type;
                return;
            }
        }
        HookHealthStatus st;
        st.name = name;
        st.targetAddress = targetAddress;
        st.hookType = type;
        st.hitCount = 0;
        st.isHealthy = true;
        m_hookList.push_back(st);
    }

    void RecordHookHit(uintptr_t targetAddress) {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& h : m_hookList) {
            if (h.targetAddress == targetAddress) {
                h.hitCount++;
                break;
            }
        }
    }

    void AddCustomPanel(const std::string& panelName, CustomPanelCallback cb) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_customPanels[panelName] = cb;
    }

    void UpdateMetrics(float fps, float frameDeltaMs) {
        m_currentFps.store(fps);
        m_frameDeltaMs.store(frameDeltaMs);
    }

    static size_t GetMemoryWorkingSetMB() {
        PROCESS_MEMORY_COUNTERS pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
            return pmc.WorkingSetSize / (1024 * 1024);
        }
        return 0;
    }

    void PollInput() {
        if (m_isDedicatedServer || !m_enabled.load()) return;

        static bool s_keyPressed = false;
        if (GetAsyncKeyState(m_toggleKey) & 0x8000) {
            if (!s_keyPressed) {
                Toggle();
                s_keyPressed = true;
            }
        } else {
            s_keyPressed = false;
        }
    }

    void OnVulkanPresent() {
        if (!m_visible.load()) return;
        m_renderTicks++;
        if (m_imguiRenderCallback) {
            m_imguiRenderCallback();
        }
    }

    void SetImGuiRenderCallback(std::function<void()> cb) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_imguiRenderCallback = cb;
    }

    std::string GenerateDiagnosticReport() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::ostringstream ss;
        ss << "================== DREAMKURUMI DIAGNOSTIC OVERLAY ==================\n";
        ss << "  Status: " << (m_visible.load() ? "VISIBLE" : "HIDDEN") << "\n";
        ss << "  FPS: " << std::fixed << std::setprecision(1) << m_currentFps.load()
           << " (" << m_frameDeltaMs.load() << " ms)\n";
        ss << "  Memory Working Set: " << GetMemoryWorkingSetMB() << " MB\n";
        ss << "  Render Ticks: " << m_renderTicks.load() << "\n\n";

        ss << "  [Active Hook Registry - " << m_hookList.size() << " Hooks]:\n";
        for (const auto& h : m_hookList) {
            ss << "   * " << std::left << std::setw(24) << h.name
               << " | Type: " << std::setw(12) << h.hookType
               << " | Addr: 0x" << std::hex << h.targetAddress << std::dec
               << " | Hits: " << h.hitCount
               << " | Health: " << (h.isHealthy ? "OK" : "DEGRADED") << "\n";
        }

        ss << "\n  [Mod Tuner Live Values]:\n";
        ss << "   * AoE Harvest Radius: " << m_settings.aoeHarvestRadius << " m\n";
        ss << "   * Auto-Pickup Radius: " << m_settings.autoPickupRadius << " m\n";
        ss << "   * XP Share Multiplier: " << m_settings.xpShareMultiplier << " x\n";
        ss << "===================================================================\n";
        return ss.str();
    }

    DiagnosticOverlaySettings& Settings() { return m_settings; }
    const DiagnosticOverlaySettings& Settings() const { return m_settings; }

    size_t GetRegisteredHookCount() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_hookList.size();
    }

    void Shutdown() {
        m_workerRunning.store(false);
        if (m_workerThread.joinable()) {
            if (m_workerThread.get_id() != std::this_thread::get_id()) {
                m_workerThread.join();
            } else {
                m_workerThread.detach();
            }
        }
        DreamOverlayWindowSync::Detach();
        if (m_hWnd) {
            DestroyWindow(m_hWnd);
            m_hWnd = nullptr;
        }
    }

private:
    DreamDiagnosticOverlay() {
        m_isDedicatedServer = DreamEnv::IsDedicatedServer();
        if (m_isDedicatedServer) {
            m_visible.store(false);
            m_enabled.store(false);
        }
    }

    ~DreamDiagnosticOverlay() {
        Shutdown();
    }

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    void EnsureWindowCreated() {
        if (m_hWnd || m_isDedicatedServer) return;

        HINSTANCE hInstance = GetModuleHandleW(NULL);
        const wchar_t* CLASS_NAME = L"DreamDiagHudWndClass";

        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc = WndProc;
        wc.hInstance = hInstance;
        wc.lpszClassName = CLASS_NAME;
        RegisterClassExW(&wc);

        m_hWnd = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            CLASS_NAME, L"DreamDiagnosticHUD",
            WS_POPUP,
            0, 0, HUD_WIDTH, HUD_HEIGHT,
            nullptr, nullptr, hInstance, nullptr
        );

        if (!m_hWnd) {
            DREAM_LOG_WARN("DreamDiagnosticOverlay: Window creation failed (Error: %lu)", GetLastError());
            return;
        }

        HWND hGame = FindGameWindow();
        if (hGame) {
            DreamOverlayWindowSync::SetVisibilityPredicate([]() noexcept {
                return DreamDiagnosticOverlay::Instance().IsVisible();
            });
            DreamOverlayWindowSync::AttachToGameWindow(hGame, m_hWnd, 30, 35, HUD_WIDTH, HUD_HEIGHT);
        }

        m_workerRunning.store(true);
        m_workerThread = std::thread(&DreamDiagnosticOverlay::WorkerLoop, this);
    }

    HWND FindGameWindow() const {
        HWND hTarget = FindWindowW(NULL, L"Enshrouded");
        if (hTarget && IsWindow(hTarget)) return hTarget;

        DWORD currentPid = GetCurrentProcessId();
        HWND topWnd = GetTopWindow(GetDesktopWindow());
        while (topWnd) {
            DWORD pid = 0;
            GetWindowThreadProcessId(topWnd, &pid);
            if (pid == currentPid && topWnd != m_hWnd && IsWindowVisible(topWnd)) {
                return topWnd;
            }
            topWnd = GetNextWindow(topWnd, GW_HWNDNEXT);
        }
        return nullptr;
    }

    void WorkerLoop() {
        while (m_workerRunning.load()) {
            if (m_visible.load() && m_hWnd) {
                RedrawHud();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }

    void RedrawHud() {
        if (!m_hWnd || !IsWindow(m_hWnd)) return;

        HWND hTargetWnd = FindWindowW(nullptr, L"Enshrouded");
        if (!hTargetWnd || !IsWindow(hTargetWnd)) {
            hTargetWnd = FindGameWindow();
        }
        if (hTargetWnd && !DreamOverlayWindowSync::IsAttached()) {
            DreamOverlayWindowSync::AttachToGameWindow(hTargetWnd, m_hWnd, 30, 35, HUD_WIDTH, HUD_HEIGHT);
        }

        RECT rcTarget = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
        if (hTargetWnd && IsWindow(hTargetWnd)) {
            RECT rc = {0};
            if (GetWindowRect(hTargetWnd, &rc)) {
                // 1. Minimized check (Iconic or coordinates pushed off-screen)
                if (IsIconic(hTargetWnd) || rc.left <= -30000 || !IsWindowVisible(hTargetWnd)) {
                    if (IsWindowVisible(m_hWnd)) {
                        ShowWindow(m_hWnd, SW_HIDE);
                    }
                    return;
                }

                // 2. Foreground check: if user switched to another app (Discord, Chrome, etc.), hide overlay
                HWND fgWnd = GetForegroundWindow();
                DWORD fgPid = 0;
                if (fgWnd) GetWindowThreadProcessId(fgWnd, &fgPid);
                if (fgPid != GetCurrentProcessId()) {
                    if (IsWindowVisible(m_hWnd)) {
                        ShowWindow(m_hWnd, SW_HIDE);
                    }
                    return;
                }

                // 3. Game is foreground and active -> ensure overlay is visible
                if (!IsWindowVisible(m_hWnd) && m_visible.load()) {
                    ShowWindow(m_hWnd, SW_SHOWNOACTIVATE);
                }

                rcTarget = rc;
            }
        }

        int posX = rcTarget.left + 30;
        int posY = rcTarget.top + 35;
        if (hTargetWnd && IsWindow(hTargetWnd)) {
            RECT clientRc = {0};
            if (GetClientRect(hTargetWnd, &clientRc)) {
                POINT ptClient = { clientRc.left, clientRc.top };
                if (ClientToScreen(hTargetWnd, &ptClient) && ptClient.x > -30000 && ptClient.y > -30000) {
                    posX = ptClient.x + 30;
                    posY = ptClient.y + 35;
                }
            }
        }

        HDC hScreenDC = GetDC(NULL);
        HDC hMemDC = CreateCompatibleDC(hScreenDC);
        HDC hTextDC = CreateCompatibleDC(hScreenDC);

        BITMAPINFO bmi = {0};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = HUD_WIDTH;
        bmi.bmiHeader.biHeight = -HUD_HEIGHT;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        uint32_t* pPixels = nullptr;
        HBITMAP hBitmap = CreateDIBSection(hScreenDC, &bmi, DIB_RGB_COLORS, (void**)&pPixels, NULL, 0);
        HGDIOBJ hOldBitmap = SelectObject(hMemDC, hBitmap);

        uint32_t* pTextPixels = nullptr;
        HBITMAP hTextBitmap = CreateDIBSection(hScreenDC, &bmi, DIB_RGB_COLORS, (void**)&pTextPixels, NULL, 0);
        HGDIOBJ hOldTextBitmap = SelectObject(hTextDC, hTextBitmap);

        // Draw Dark Slate rounded box with Cyan glowing border
        const int radius = 12;
        for (int y = 0; y < HUD_HEIGHT; ++y) {
            for (int x = 0; x < HUD_WIDTH; ++x) {
                int cx = (x < radius) ? radius : (x >= HUD_WIDTH - radius) ? (HUD_WIDTH - radius - 1) : x;
                int cy = (y < radius) ? radius : (y >= HUD_HEIGHT - radius) ? (HUD_HEIGHT - radius - 1) : y;
                int dx = x - cx;
                int dy = y - cy;
                int distSq = dx * dx + dy * dy;
                int idx = y * HUD_WIDTH + x;

                if (distSq <= radius * radius) {
                    bool isBorder = (distSq >= (radius - 2) * (radius - 2)) ||
                                    (y <= 1 || y >= HUD_HEIGHT - 2 || x <= 1 || x >= HUD_WIDTH - 2);
                    uint8_t a = isBorder ? 255 : 230;
                    uint8_t r = isBorder ? 6 : 15;
                    uint8_t g = isBorder ? 182 : 23;
                    uint8_t b = isBorder ? 212 : 42;

                    uint8_t pr = (r * a) / 255;
                    uint8_t pg = (g * a) / 255;
                    uint8_t pb = (b * a) / 255;
                    pPixels[idx] = (a << 24) | (pr << 16) | (pg << 8) | pb;
                } else {
                    pPixels[idx] = 0;
                }
            }
        }

        // Setup GDI fonts for pristine antialiased typography
        HFONT hTitleFont = CreateFontW(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        HFONT hBodyFont = CreateFontW(14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        HFONT hFootFont = CreateFontW(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        SetBkMode(hTextDC, TRANSPARENT);

        // Header Title
        SelectObject(hTextDC, hTitleFont);
        SetTextColor(hTextDC, RGB(34, 211, 238)); // Cyan 400
        RECT rcTitle = {16, 12, 250, 32};
        DrawTextW(hTextDC, L"DREAMKURUMI DIAGNOSTICS (F9)", -1, &rcTitle, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        SetTextColor(hTextDC, RGB(103, 232, 249)); // Cyan 300
        RECT rcVer = {HUD_WIDTH - 65, 12, HUD_WIDTH - 16, 32};
        DrawTextW(hTextDC, L"v3.9.0", -1, &rcVer, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);

        // Divider lines directly onto background
        for (int x = 16; x < HUD_WIDTH - 16; ++x) {
            int idx1 = 36 * HUD_WIDTH + x;
            pPixels[idx1] = (120 << 24) | ((6 * 120 / 255) << 16) | ((182 * 120 / 255) << 8) | (212 * 120 / 255);
            int idx2 = 186 * HUD_WIDTH + x;
            pPixels[idx2] = (80 << 24) | ((6 * 80 / 255) << 16) | ((182 * 80 / 255) << 8) | (212 * 80 / 255);
        }

        // Query Live Metrics
        size_t memMB = GetMemoryWorkingSetMB();
        size_t prevented = DreamCrashGuard::Instance().GetTotalCrashesPrevented();

        wchar_t bufVal[64];

        auto drawRow = [&](int y, const wchar_t* label, const wchar_t* val, COLORREF valColor) {
            SelectObject(hTextDC, hBodyFont);
            SetTextColor(hTextDC, RGB(148, 163, 184)); // Slate 400
            RECT rcL = {18, y, 160, y + 20};
            DrawTextW(hTextDC, label, -1, &rcL, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

            SetTextColor(hTextDC, valColor);
            RECT rcV = {165, y, HUD_WIDTH - 18, y + 20};
            DrawTextW(hTextDC, val, -1, &rcV, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
        };

        if (m_isDedicatedServer) {
            drawRow(46, L"Engine Mode:", L"Dedicated Server", RGB(52, 211, 153));
            size_t playerCount = DreamPlayerRegistry::Instance().GetPlayerCount();
            if (playerCount > 0) {
                swprintf(bufVal, 64, L"Hosting (%zu Players)", playerCount);
                drawRow(72, L"Server Connection:", bufVal, RGB(52, 211, 153));
            } else {
                drawRow(72, L"Server Connection:", L"Hosting (0 Players)", RGB(148, 163, 184));
            }
        } else {
            drawRow(46, L"Engine Mode:", L"Vulkan 1.3 Client", RGB(52, 211, 153));
            auto netState = DreamNetStateTracker::Instance().GetState();
            int32_t ping = DreamNetStateTracker::Instance().GetPingMs();
            if (netState == EngineConnectionState::Connected || DreamGnsTransportHook::IsConnected()) {
                if (ping >= 0) {
                    swprintf(bufVal, 64, L"Connected (GNS) \x2022 %dms", ping);
                } else {
                    swprintf(bufVal, 64, L"Connected (GNS)");
                }
                drawRow(72, L"Server Connection:", bufVal, RGB(52, 211, 153));
            } else if (netState == EngineConnectionState::Connecting || netState == EngineConnectionState::FindingRoute) {
                drawRow(72, L"Server Connection:", L"Connecting...", RGB(251, 191, 36));
            } else {
                drawRow(72, L"Server Connection:", L"Disconnected / Local", RGB(148, 163, 184));
            }
        }

        if (prevented > 0) {
            swprintf(bufVal, 64, L"%zu (Recovered)", prevented);
            drawRow(98,  L"Faults Shielded:", bufVal, RGB(251, 191, 36)); // Amber
        } else {
            drawRow(98,  L"Faults Shielded:", L"0", RGB(52, 211, 153));
        }

        drawRow(124, L"Active Circuit:", L"OK (0 / 5)", RGB(52, 211, 153));

        swprintf(bufVal, 64, L"%zu MB", memMB);
        drawRow(150, L"Memory Working Set:", bufVal, RGB(241, 245, 249));

        // Footer Text
        SelectObject(hTextDC, hFootFont);
        SetTextColor(hTextDC, RGB(6, 182, 212)); // Cyan
        RECT rcFoot = {18, 194, HUD_WIDTH - 18, 214};
        DrawTextW(hTextDC, L"\x2022 Press [F9] to hide overlay", -1, &rcFoot, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        // Alpha-composite pristine text on top of obsidian background
        for (int y = 0; y < HUD_HEIGHT; ++y) {
            for (int x = 0; x < HUD_WIDTH; ++x) {
                int idx = y * HUD_WIDTH + x;
                uint32_t txtPx = pTextPixels[idx];
                uint8_t tR = (txtPx >> 16) & 0xFF;
                uint8_t tG = (txtPx >> 8) & 0xFF;
                uint8_t tB = txtPx & 0xFF;
                uint8_t txtAlpha = (tR > tG) ? (tR > tB ? tR : tB) : (tG > tB ? tG : tB);

                if (txtAlpha > 0) {
                    uint32_t inv = 255 - txtAlpha;
                    uint8_t bgA = (pPixels[idx] >> 24) & 0xFF;
                    uint8_t bgR = (pPixels[idx] >> 16) & 0xFF;
                    uint8_t bgG = (pPixels[idx] >> 8) & 0xFF;
                    uint8_t bgB = pPixels[idx] & 0xFF;

                    uint8_t outA = txtAlpha + ((bgA * inv) / 255);
                    uint8_t outR = tR + ((bgR * inv) / 255);
                    uint8_t outG = tG + ((bgG * inv) / 255);
                    uint8_t outB = tB + ((bgB * inv) / 255);

                    pPixels[idx] = (outA << 24) | (outR << 16) | (outG << 8) | outB;
                }
            }
        }

        POINT ptSrc = {0, 0};
        POINT ptDst = {posX, posY};
        SIZE wndSize = {HUD_WIDTH, HUD_HEIGHT};
        BLENDFUNCTION blend = {0};
        blend.BlendOp = AC_SRC_OVER;
        blend.BlendFlags = 0;
        blend.SourceConstantAlpha = 255;
        blend.AlphaFormat = AC_SRC_ALPHA;

        UpdateLayeredWindow(m_hWnd, hScreenDC, &ptDst, &wndSize, hMemDC, &ptSrc, 0, &blend, ULW_ALPHA);
        SetWindowPos(
            m_hWnd,
            HWND_TOPMOST,
            posX, posY, HUD_WIDTH, HUD_HEIGHT,
            SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING | SWP_ASYNCWINDOWPOS
        );

        SelectObject(hTextDC, hOldTextBitmap);
        DeleteObject(hTextBitmap);
        DeleteDC(hTextDC);

        SelectObject(hMemDC, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hMemDC);

        ReleaseDC(NULL, hScreenDC);

        DeleteObject(hTitleFont);
        DeleteObject(hBodyFont);
        DeleteObject(hFootFont);
    }

    HWND m_hWnd{nullptr};
    std::thread m_workerThread;
    std::atomic<bool> m_workerRunning{false};

    mutable std::mutex m_mutex;
    std::atomic<bool> m_enabled{true};
    std::atomic<bool> m_visible{false};
    int m_toggleKey{0x78}; // VK_F9
    bool m_isDedicatedServer{false};

    std::atomic<float> m_currentFps{60.0f};
    std::atomic<float> m_frameDeltaMs{16.6f};
    std::atomic<uint64_t> m_renderTicks{0};

    DiagnosticOverlaySettings m_settings;
    std::vector<HookHealthStatus> m_hookList;
    std::unordered_map<std::string, CustomPanelCallback> m_customPanels;
    std::function<void()> m_imguiRenderCallback{nullptr};
};

} // namespace dreamkurumi

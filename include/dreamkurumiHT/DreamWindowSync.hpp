#pragma once
#include <windows.h>
#include <atomic>

namespace dreamkurumi {

class DreamOverlayWindowSync {
    using VisibilityPredicate = bool (*)();
    static inline WNDPROC             s_origGameWndProc = nullptr;
    static inline HWND                s_hOverlayWnd     = nullptr;
    static inline HWND                s_hGameWnd        = nullptr;
    static inline VisibilityPredicate s_pfnIsVisible    = nullptr;
    static inline int                 s_offsetX         = 30;
    static inline int                 s_offsetY         = 35;
    static inline int                 s_hudWidth        = 340;
    static inline int                 s_hudHeight       = 225;

public:
    static void SetVisibilityPredicate(VisibilityPredicate pfn) noexcept {
        s_pfnIsVisible = pfn;
    }

    static void AttachToGameWindow(HWND hGameWnd, HWND hOverlayWnd, int offX = 30, int offY = 35, int w = 340, int h = 225) noexcept {
        if (!hGameWnd || !hOverlayWnd) return;
        if (s_origGameWndProc && s_hGameWnd == hGameWnd && s_hOverlayWnd == hOverlayWnd) return;

        s_hGameWnd    = hGameWnd;
        s_hOverlayWnd = hOverlayWnd;
        s_offsetX     = offX;
        s_offsetY     = offY;
        s_hudWidth    = w;
        s_hudHeight   = h;

        // Initial sync before hooking WndProc
        SyncOverlayToClientArea(hGameWnd, hOverlayWnd);

        if (!s_origGameWndProc) {
            s_origGameWndProc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrW(hGameWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&HookedGameWndProc))
            );
        }
    }

    static void Detach() noexcept {
        if (s_hGameWnd && s_origGameWndProc) {
            SetWindowLongPtrW(s_hGameWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(s_origGameWndProc));
            s_origGameWndProc = nullptr;
            s_hGameWnd        = nullptr;
            s_hOverlayWnd     = nullptr;
        }
    }

    static void SyncOverlayToClientArea(HWND hGame, HWND hOverlay) noexcept {
        if (!hGame || !hOverlay || !IsWindow(hGame) || !IsWindow(hOverlay)) return;

        // If game is minimized or off-screen, hide overlay immediately
        if (IsIconic(hGame)) {
            if (IsWindowVisible(hOverlay)) {
                ShowWindow(hOverlay, SW_HIDE);
            }
            return;
        }

        RECT clientRect{};
        if (GetClientRect(hGame, &clientRect)) {
            POINT topLeft{ clientRect.left, clientRect.top };
            ClientToScreen(hGame, &topLeft);

            if (topLeft.x <= -30000 || topLeft.y <= -30000) {
                if (IsWindowVisible(hOverlay)) {
                    ShowWindow(hOverlay, SW_HIDE);
                }
                return;
            }

            // SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_ASYNCWINDOWPOS prevents blocking Vulkan WSI!
            SetWindowPos(
                hOverlay,
                HWND_TOPMOST,
                topLeft.x + s_offsetX, topLeft.y + s_offsetY, s_hudWidth, s_hudHeight,
                SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING | SWP_ASYNCWINDOWPOS
            );
        }
    }

    static bool IsAttached() noexcept {
        return s_origGameWndProc != nullptr;
    }

private:
    static LRESULT CALLBACK HookedGameWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
            case WM_WINDOWPOSCHANGED:
            case WM_DISPLAYCHANGE:
            case WM_SIZE:
            case WM_MOVE:
                if (s_hOverlayWnd) {
                    SyncOverlayToClientArea(hWnd, s_hOverlayWnd);
                }
                break;

            case WM_ACTIVATEAPP:
                if (s_hOverlayWnd) {
                    // Hide TOPMOST overlay when user Alt-Tabs out of Enshrouded
                    bool shouldShow = (wParam != FALSE);
                    if (shouldShow && s_pfnIsVisible && !s_pfnIsVisible()) {
                        shouldShow = false;
                    }
                    ShowWindow(s_hOverlayWnd, shouldShow ? SW_SHOWNOACTIVATE : SW_HIDE);
                }
                break;

            case WM_DESTROY:
                Detach();
                break;
        }
        return CallWindowProcW(s_origGameWndProc, hWnd, msg, wParam, lParam);
    }
};

} // namespace dreamkurumi

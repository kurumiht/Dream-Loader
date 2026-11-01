#pragma once
#include <windows.h>
#include <atomic>

namespace dreamkurumi {

// ============================================================================
// DreamOverlayWindowSync: Crash-Proof Native Win32 Window Synchronization
//
// Tracks the client-area coordinates of the Enshrouded game window to position
// the overlay cleanly without linking Win32 input queues across threads.
//
// Key Advantages:
// 1. Zero crash risk: Never subclasses or intercepts the game's WndProc.
// 2. Zero input interference: Standalone layered popup with no cross-thread ownership
//    (GWLP_HWNDPARENT omitted to prevent OS input queue attachment/deadlocks).
// 3. Foreground & Minimize awareness: Auto-hides when Enshrouded loses focus or minimizes.
// ============================================================================
class DreamOverlayWindowSync {
    static inline HWND                s_hOverlayWnd{nullptr};
    static inline HWND                s_hGameWnd{nullptr};
    static inline std::atomic<bool>   s_isAttached{false};
    static inline int                 s_offsetX{30};
    static inline int                 s_offsetY{35};
    static inline int                 s_hudWidth{340};
    static inline int                 s_hudHeight{225};

public:
    using VisibilityPredicate = bool (*)();
    static inline VisibilityPredicate s_pfnIsVisible{nullptr};

    static void SetVisibilityPredicate(VisibilityPredicate pfn) noexcept {
        s_pfnIsVisible = pfn;
    }

    static void AttachToGameWindow(HWND hGameWnd, HWND hOverlayWnd, int offX = 30, int offY = 35, int w = 340, int h = 225) noexcept {
        if (!hGameWnd || !hOverlayWnd || !IsWindow(hGameWnd) || !IsWindow(hOverlayWnd)) return;

        s_hGameWnd    = hGameWnd;
        s_hOverlayWnd = hOverlayWnd;
        s_offsetX     = offX;
        s_offsetY     = offY;
        s_hudWidth    = w;
        s_hudHeight   = h;
        s_isAttached.store(true);

        SyncOverlayToClientArea(hGameWnd, hOverlayWnd);
    }

    static void Detach() noexcept {
        s_isAttached.store(false);
        s_hGameWnd    = nullptr;
        s_hOverlayWnd = nullptr;
    }

    static void SyncOverlayToClientArea(HWND hGame, HWND hOverlay) noexcept {
        if (!hGame || !hOverlay || !IsWindow(hGame) || !IsWindow(hOverlay)) return;

        // If game is minimized or off-screen, hide overlay immediately
        if (IsIconic(hGame) || !IsWindowVisible(hGame)) {
            if (IsWindowVisible(hOverlay)) {
                ShowWindow(hOverlay, SW_HIDE);
            }
            return;
        }

        // Foreground check: hide when game is not the active foreground window
        HWND fg = GetForegroundWindow();
        if (fg && fg != hGame && fg != hOverlay) {
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

            SetWindowPos(
                hOverlay,
                HWND_TOPMOST,
                topLeft.x + s_offsetX, topLeft.y + s_offsetY, s_hudWidth, s_hudHeight,
                SWP_NOACTIVATE | SWP_NOSENDCHANGING | SWP_ASYNCWINDOWPOS
            );
        }
    }

    static bool IsAttached() noexcept {
        return s_isAttached.load();
    }
};

} // namespace dreamkurumi

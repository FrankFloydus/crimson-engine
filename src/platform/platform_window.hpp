////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>

namespace quader {

inline constexpr int kPlatformWindowMinimumWidth = 800;
inline constexpr int kPlatformWindowMinimumHeight = 600;

/**
 * Stores the Platform Window Options data contract used by the platform integration layer.
 */
struct PlatformWindowOptions {
    bool borderless = false;
};

/**
 * Stores borderless chrome hit-test geometry in SDL window coordinates.
 */
struct PlatformWindowChromeHitTest {
    int windowWidth = 1;
    int windowHeight = 1;
    int pointX = 0;
    int pointY = 0;
    int resizeBorder = 6;
    int titleBarHeight = 35;
    int leftInteractiveWidth = 440;
    int rightControlsWidth = 105;
    bool maximized = false;
};

/**
 * Stores a window rectangle in SDL window coordinates.
 */
struct PlatformWindowChromeFrame {
    int x = 0;
    int y = 0;
    int width = 1;
    int height = 1;
};

/**
 * Stores one manual borderless resize drag.
 */
struct PlatformWindowChromeResizeDrag {
    PlatformWindowChromeFrame start;
    int deltaX = 0;
    int deltaY = 0;
    int minWidth = kPlatformWindowMinimumWidth;
    int minHeight = kPlatformWindowMinimumHeight;
    SDL_HitTestResult edge = SDL_HITTEST_NORMAL;
};

/**
 * Stores persisted normal window placement plus native maximized state.
 */
struct PlatformWindowPlacement {
    PlatformWindowChromeFrame normalFrame{0, 0, 1600, 960};
    bool maximized = false;
};

[[nodiscard]] SDL_HitTestResult platformWindowChromeHitTestResult(const PlatformWindowChromeHitTest& hitTest);
[[nodiscard]] PlatformWindowChromeFrame platformWindowChromeResizeFrame(const PlatformWindowChromeResizeDrag& drag);
[[nodiscard]] int platformWindowChromeResizeBorderForSystemMetrics(int fallbackBorder, int sizeFrame, int paddedBorder);
[[nodiscard]] bool platformWindowCanRenderFrames(std::uint64_t windowFlags, int pixelWidth, int pixelHeight);
[[nodiscard]] PlatformWindowChromeFrame platformWindowRecoverFrameToDisplayWorkArea(
        PlatformWindowChromeFrame frame,
        std::span<const PlatformWindowChromeFrame> displayWorkAreas);

/**
 * Owns Platform Window behavior within the platform integration layer.
 */
class PlatformWindow {
public:
    using LiveResizeFrameCallback = std::function<void()>;

    PlatformWindow() = default;
    ~PlatformWindow();

    PlatformWindow(const PlatformWindow&) = delete;
    PlatformWindow& operator=(const PlatformWindow&) = delete;

    bool create(const char* title, int width, int height, PlatformWindowOptions options = {});
    void destroy();

    SDL_Window* sdlWindow() const { return window_; }
    void* nativeWindowHandle() const;

    std::uint32_t widthPixels() const { return widthPixels_; }
    std::uint32_t heightPixels() const { return heightPixels_; }
    void refreshPixelSize();
    void refreshNativeChrome();
    void minimize();
    void toggleMaximizeRestore();
    [[nodiscard]] bool canRenderFrames() const;
    [[nodiscard]] std::optional<PlatformWindowPlacement> placement() const;
    void applyPlacement(const PlatformWindowPlacement& placement);
    [[nodiscard]] bool processChromeEvent(const SDL_Event& event);
    [[nodiscard]] bool chromeResizeActive() const;
    [[nodiscard]] bool maximized() const;
    void setLiveResizeFrameCallback(LiveResizeFrameCallback callback);

private:
    void installBorderlessChrome();
    void applyNativeBorderlessChromeAdornments();
    void installNativeChromeCursorHook();
    void uninstallNativeChromeCursorHook();
    void beginChromeInteraction(SDL_HitTestResult hit, float globalMouseX, float globalMouseY, float windowMouseX, float windowMouseY);
    void updateChromeInteraction(float globalMouseX, float globalMouseY);
    void endChromeInteraction();
    void cancelChromeInteraction();
    void requestLiveResizeFrame(bool force = false);
    void resetLiveResizeFrameCoalescing();
    void updateChromeHoverCursor(float windowMouseX, float windowMouseY);
    void releaseChromeResizeCursor();
    void setChromeCursor(SDL_HitTestResult hit);
    SDL_Cursor* chromeCursor(SDL_SystemCursor cursor);
#if defined(_WIN32)
    static std::intptr_t __stdcall nativeChromeWindowProc(
            void* windowHandle,
            unsigned int message,
            std::uintptr_t wParam,
            std::intptr_t lParam);
    [[nodiscard]] std::intptr_t handleNativeChromeWindowMessage(
            void* windowHandle,
            unsigned int message,
            std::uintptr_t wParam,
            std::intptr_t lParam);
    [[nodiscard]] SDL_HitTestResult nativeChromeHitTestFromScreenPoint(int screenX, int screenY) const;
    [[nodiscard]] int nativeChromeResizeBorderForWindow(void* windowHandle) const;
    void clearNativeResizeState();
#endif

    /// Tracks the active native chrome drag or resize gesture.
    struct ChromeInteraction {
        bool active = false;
        bool pendingMaximizedRestore = false;
        SDL_HitTestResult hit = SDL_HITTEST_NORMAL;
        PlatformWindowChromeFrame startFrame;
        float startGlobalMouseX = 0.0f;
        float startGlobalMouseY = 0.0f;
        float restoreWindowMouseX = 0.0f;
        float restoreWindowMouseY = 0.0f;
        int moveOffsetX = 0;
        int moveOffsetY = 0;
    };

    SDL_Window* window_ = nullptr;
    std::uint32_t widthPixels_ = 1;
    std::uint32_t heightPixels_ = 1;
    PlatformWindowChromeHitTest chromeHitTest_;
    ChromeInteraction chromeInteraction_;
    bool borderlessChrome_ = false;
    bool chromeMouseCaptured_ = false;
    bool chromeResizeCursorActive_ = false;
    bool liveResizeFrameSizeValid_ = false;
    std::uint32_t liveResizeFrameWidthPixels_ = 0;
    std::uint32_t liveResizeFrameHeightPixels_ = 0;
    LiveResizeFrameCallback liveResizeFrameCallback_;
#if defined(_WIN32)
    void* nativeChromeWindowHandle_ = nullptr;
    std::intptr_t previousNativeChromeWindowProc_ = 0;
    bool nativeResizeActive_ = false;
#endif
    std::array<SDL_Cursor*, SDL_SYSTEM_CURSOR_COUNT> chromeCursors_{};
};

} // namespace quader

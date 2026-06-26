////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "platform/PlatformWindow.h"

#include "core/Logger.h"
#include "platform/SdlEventUtils.h"

#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_video.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#endif

#include <algorithm>
#include <cmath>
#include <optional>

namespace quader {
namespace {

int gSdlVideoWindowCount = 0;
constexpr float kChromeDragRestoreThreshold = 4.0f;

#if defined(_WIN32)
constexpr wchar_t kDwmRegistryKey[] = L"SOFTWARE\\Microsoft\\Windows\\DWM";
constexpr wchar_t kBorderlessChromeWindowProp[] = L"QuaderPlatformWindowChrome";

HCURSOR nativeChromeCursorForHit(SDL_HitTestResult hit) {
    switch (hit) {
    case SDL_HITTEST_RESIZE_TOPLEFT:
    case SDL_HITTEST_RESIZE_BOTTOMRIGHT:
        return LoadCursor(nullptr, IDC_SIZENWSE);
    case SDL_HITTEST_RESIZE_TOP:
    case SDL_HITTEST_RESIZE_BOTTOM:
        return LoadCursor(nullptr, IDC_SIZENS);
    case SDL_HITTEST_RESIZE_TOPRIGHT:
    case SDL_HITTEST_RESIZE_BOTTOMLEFT:
        return LoadCursor(nullptr, IDC_SIZENESW);
    case SDL_HITTEST_RESIZE_RIGHT:
    case SDL_HITTEST_RESIZE_LEFT:
        return LoadCursor(nullptr, IDC_SIZEWE);
    default:
        return nullptr;
    }
}

LRESULT nativeChromeHitTestCodeForHit(SDL_HitTestResult hit) {
    switch (hit) {
    case SDL_HITTEST_RESIZE_TOPLEFT:
        return HTTOPLEFT;
    case SDL_HITTEST_RESIZE_TOP:
        return HTTOP;
    case SDL_HITTEST_RESIZE_TOPRIGHT:
        return HTTOPRIGHT;
    case SDL_HITTEST_RESIZE_RIGHT:
        return HTRIGHT;
    case SDL_HITTEST_RESIZE_BOTTOMRIGHT:
        return HTBOTTOMRIGHT;
    case SDL_HITTEST_RESIZE_BOTTOM:
        return HTBOTTOM;
    case SDL_HITTEST_RESIZE_BOTTOMLEFT:
        return HTBOTTOMLEFT;
    case SDL_HITTEST_RESIZE_LEFT:
        return HTLEFT;
    default:
        return HTCLIENT;
    }
}

bool isNativeChromeResizeHit(SDL_HitTestResult hit) {
    return nativeChromeHitTestCodeForHit(hit) != HTCLIENT;
}

std::optional<DWORD> readDwmRegistryDword(const wchar_t* valueName) {
    DWORD value = 0;
    DWORD valueSize = sizeof(value);
    const LSTATUS result = RegGetValueW(
            HKEY_CURRENT_USER,
            kDwmRegistryKey,
            valueName,
            RRF_RT_REG_DWORD,
            nullptr,
            &value,
            &valueSize);
    if (result != ERROR_SUCCESS) {
        return std::nullopt;
    }

    return value;
}

bool accentColorOnTitleBarsEnabled() {
    return readDwmRegistryDword(L"ColorPrevalence").value_or(0u) != 0u;
}

std::optional<COLORREF> systemAccentBorderColor() {
    const std::optional<DWORD> accentColor = readDwmRegistryDword(L"AccentColor");
    if (!accentColor) {
        return std::nullopt;
    }

    return RGB(GetRValue(*accentColor), GetGValue(*accentColor), GetBValue(*accentColor));
}
#endif

bool acquireSdlVideo() {
    if (gSdlVideoWindowCount == 0 && !SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        LOG_ERROR("SDL_Init failed: {}", SDL_GetError());
        return false;
    }

    ++gSdlVideoWindowCount;
    return true;
}

void releaseSdlVideo() {
    if (gSdlVideoWindowCount <= 0) {
        return;
    }

    --gSdlVideoWindowCount;
    if (gSdlVideoWindowCount == 0) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    }
}

} // namespace

SDL_HitTestResult platformWindowChromeHitTestResult(const PlatformWindowChromeHitTest& hitTest) {
    const int windowWidth = std::max(1, hitTest.windowWidth);
    const int windowHeight = std::max(1, hitTest.windowHeight);
    const int pointX = hitTest.pointX;
    const int pointY = hitTest.pointY;
    if (pointX < 0 || pointY < 0 || pointX >= windowWidth || pointY >= windowHeight) {
        return SDL_HITTEST_NORMAL;
    }

    const int resizeBorder = std::max(0, hitTest.resizeBorder);
    if (!hitTest.maximized && resizeBorder > 0) {
        const bool left = pointX < resizeBorder;
        const bool right = pointX >= windowWidth - resizeBorder;
        const bool top = pointY < resizeBorder;
        const bool bottom = pointY >= windowHeight - resizeBorder;

        if (top && left) {
            return SDL_HITTEST_RESIZE_TOPLEFT;
        }
        if (top && right) {
            return SDL_HITTEST_RESIZE_TOPRIGHT;
        }
        if (bottom && right) {
            return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
        }
        if (bottom && left) {
            return SDL_HITTEST_RESIZE_BOTTOMLEFT;
        }
        if (top) {
            return SDL_HITTEST_RESIZE_TOP;
        }
        if (right) {
            return SDL_HITTEST_RESIZE_RIGHT;
        }
        if (bottom) {
            return SDL_HITTEST_RESIZE_BOTTOM;
        }
        if (left) {
            return SDL_HITTEST_RESIZE_LEFT;
        }
    }

    const int titleBarHeight = std::max(0, hitTest.titleBarHeight);
    const int rightControlsWidth = std::max(0, hitTest.rightControlsWidth);
    const int draggableRight = std::max(0, windowWidth - rightControlsWidth);
    if (pointY < titleBarHeight && pointX < draggableRight) {
        return SDL_HITTEST_DRAGGABLE;
    }

    return SDL_HITTEST_NORMAL;
}

PlatformWindowChromeFrame platformWindowChromeResizeFrame(const PlatformWindowChromeResizeDrag& drag) {
    const int minWidth = std::max(1, drag.minWidth);
    const int minHeight = std::max(1, drag.minHeight);
    const int startLeft = drag.start.x;
    const int startTop = drag.start.y;
    const int startRight = drag.start.x + std::max(1, drag.start.width);
    const int startBottom = drag.start.y + std::max(1, drag.start.height);

    int left = startLeft;
    int top = startTop;
    int right = startRight;
    int bottom = startBottom;

    switch (drag.edge) {
    case SDL_HITTEST_RESIZE_TOPLEFT:
    case SDL_HITTEST_RESIZE_LEFT:
    case SDL_HITTEST_RESIZE_BOTTOMLEFT:
        left = std::min(startLeft + drag.deltaX, startRight - minWidth);
        break;
    default:
        break;
    }

    switch (drag.edge) {
    case SDL_HITTEST_RESIZE_TOPRIGHT:
    case SDL_HITTEST_RESIZE_RIGHT:
    case SDL_HITTEST_RESIZE_BOTTOMRIGHT:
        right = std::max(startRight + drag.deltaX, startLeft + minWidth);
        break;
    default:
        break;
    }

    switch (drag.edge) {
    case SDL_HITTEST_RESIZE_TOPLEFT:
    case SDL_HITTEST_RESIZE_TOP:
    case SDL_HITTEST_RESIZE_TOPRIGHT:
        top = std::min(startTop + drag.deltaY, startBottom - minHeight);
        break;
    default:
        break;
    }

    switch (drag.edge) {
    case SDL_HITTEST_RESIZE_BOTTOMLEFT:
    case SDL_HITTEST_RESIZE_BOTTOM:
    case SDL_HITTEST_RESIZE_BOTTOMRIGHT:
        bottom = std::max(startBottom + drag.deltaY, startTop + minHeight);
        break;
    default:
        break;
    }

    return {
            .x = left,
            .y = top,
            .width = std::max(1, right - left),
            .height = std::max(1, bottom - top),
    };
}

PlatformWindow::~PlatformWindow() {
    destroy();
}

bool PlatformWindow::create(const char* title, int width, int height, PlatformWindowOptions options) {
    if (!acquireSdlVideo()) {
        return false;
    }

    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (options.borderless) {
        flags |= SDL_WINDOW_BORDERLESS;
        LOG_INFO("Creating borderless platform window");
    }

    window_ = SDL_CreateWindow(
            title,
            width,
            height,
            flags);

    if (!window_) {
        LOG_ERROR("SDL_CreateWindow failed: {}", SDL_GetError());
        releaseSdlVideo();
        return false;
    }
    if (!SDL_SetWindowMinimumSize(window_, kPlatformWindowMinimumWidth, kPlatformWindowMinimumHeight)) {
        LOG_WARN("SDL_SetWindowMinimumSize failed: {}", SDL_GetError());
    }

    refreshPixelSize();
    if (options.borderless) {
        installBorderlessChrome();
        applyNativeBorderlessChromeAdornments();
    }
    return true;
}

void PlatformWindow::destroy() {
    if (window_) {
        cancelChromeInteraction();
        uninstallNativeChromeCursorHook();
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        releaseSdlVideo();
    }
    for (SDL_Cursor*& cursor : chromeCursors_) {
        if (cursor) {
            SDL_DestroyCursor(cursor);
            cursor = nullptr;
        }
    }
    borderlessChrome_ = false;
}

void* PlatformWindow::nativeWindowHandle() const {
    if (!window_) {
        return nullptr;
    }

    SDL_PropertiesID props = SDL_GetWindowProperties(window_);
#if defined(_WIN32)
    return SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#else
    return nullptr;
#endif
}

void PlatformWindow::refreshPixelSize() {
    if (!window_) {
        return;
    }

    int w = 1;
    int h = 1;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    widthPixels_ = static_cast<std::uint32_t>(w > 0 ? w : 1);
    heightPixels_ = static_cast<std::uint32_t>(h > 0 ? h : 1);
}

void PlatformWindow::refreshNativeChrome() {
    applyNativeBorderlessChromeAdornments();
}

void PlatformWindow::minimize() {
    if (window_ && !SDL_MinimizeWindow(window_)) {
        LOG_WARN("SDL_MinimizeWindow failed: {}", SDL_GetError());
    }
}

void PlatformWindow::toggleMaximizeRestore() {
    if (!window_) {
        return;
    }

    if (maximized()) {
        if (!SDL_RestoreWindow(window_)) {
            LOG_WARN("SDL_RestoreWindow failed: {}", SDL_GetError());
        }
        return;
    }

    if (!SDL_MaximizeWindow(window_)) {
        LOG_WARN("SDL_MaximizeWindow failed: {}", SDL_GetError());
    }
}

bool PlatformWindow::processChromeEvent(const SDL_Event& event) {
    if (!window_ || !borderlessChrome_) {
        return false;
    }

    const SDL_WindowID windowId = SDL_GetWindowID(window_);
    const std::optional<SDL_WindowID> eventWindow = sdlEventWindowId(event);
    if (!eventWindow || *eventWindow != windowId) {
        return false;
    }

    if (chromeInteraction_.active) {
        switch (event.type) {
        case SDL_EVENT_MOUSE_MOTION: {
            if ((event.motion.state & SDL_BUTTON_LMASK) == 0u) {
                cancelChromeInteraction();
                return true;
            }
            float globalX = 0.0f;
            float globalY = 0.0f;
            SDL_GetGlobalMouseState(&globalX, &globalY);
            updateChromeInteraction(globalX, globalY);
            return true;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event.button.button == SDL_BUTTON_LEFT) {
                float globalX = 0.0f;
                float globalY = 0.0f;
                SDL_GetGlobalMouseState(&globalX, &globalY);
                updateChromeInteraction(globalX, globalY);
                endChromeInteraction();
                return true;
            }
            return true;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        case SDL_EVENT_WINDOW_FOCUS_LOST:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            cancelChromeInteraction();
            return true;
        default:
            return true;
        }
    }

    if (event.type == SDL_EVENT_MOUSE_MOTION) {
        updateChromeHoverCursor(event.motion.x, event.motion.y);
        return false;
    }

    if (event.type != SDL_EVENT_MOUSE_BUTTON_DOWN ||
            event.button.button != SDL_BUTTON_LEFT) {
        return false;
    }

    float globalX = 0.0f;
    float globalY = 0.0f;
    SDL_GetGlobalMouseState(&globalX, &globalY);
    int width = 1;
    int height = 1;
    SDL_GetWindowSize(window_, &width, &height);
    PlatformWindowChromeHitTest hitTest = chromeHitTest_;
    hitTest.windowWidth = std::max(1, width);
    hitTest.windowHeight = std::max(1, height);
    hitTest.pointX = static_cast<int>(std::lround(event.button.x));
    hitTest.pointY = static_cast<int>(std::lround(event.button.y));
    hitTest.maximized = maximized();

    const SDL_HitTestResult hit = platformWindowChromeHitTestResult(hitTest);
    if (hit == SDL_HITTEST_NORMAL) {
        return false;
    }
    if (event.button.clicks > 1) {
        if (hit == SDL_HITTEST_DRAGGABLE) {
            toggleMaximizeRestore();
            return true;
        }
        return false;
    }

    beginChromeInteraction(hit, globalX, globalY, event.button.x, event.button.y);
    return true;
}

bool PlatformWindow::maximized() const {
    return window_ && (SDL_GetWindowFlags(window_) & SDL_WINDOW_MAXIMIZED) != 0u;
}

void PlatformWindow::installBorderlessChrome() {
    if (!window_) {
        return;
    }

    borderlessChrome_ = true;
    installNativeChromeCursorHook();
}

void PlatformWindow::installNativeChromeCursorHook() {
#if defined(_WIN32)
    if (!window_ || nativeChromeWindowHandle_) {
        return;
    }

    HWND hwnd = static_cast<HWND>(nativeWindowHandle());
    if (!hwnd) {
        return;
    }

    if (!SetPropW(hwnd, kBorderlessChromeWindowProp, reinterpret_cast<HANDLE>(this))) {
        LOG_WARN("SetPropW failed for borderless chrome cursor hook: {}", GetLastError());
        return;
    }

    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previousWindowProc = SetWindowLongPtrW(
            hwnd,
            GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(&PlatformWindow::nativeChromeWindowProc));
    const DWORD error = GetLastError();
    if (previousWindowProc == 0 && error != ERROR_SUCCESS) {
        LOG_WARN("SetWindowLongPtrW failed for borderless chrome cursor hook: {}", error);
        RemovePropW(hwnd, kBorderlessChromeWindowProp);
        return;
    }

    nativeChromeWindowHandle_ = hwnd;
    previousNativeChromeWindowProc_ = static_cast<std::intptr_t>(previousWindowProc);
#endif
}

void PlatformWindow::uninstallNativeChromeCursorHook() {
#if defined(_WIN32)
    if (!nativeChromeWindowHandle_) {
        return;
    }

    HWND hwnd = static_cast<HWND>(nativeChromeWindowHandle_);
    const LONG_PTR currentWindowProc = GetWindowLongPtrW(hwnd, GWLP_WNDPROC);
    if (previousNativeChromeWindowProc_ != 0 &&
            currentWindowProc == reinterpret_cast<LONG_PTR>(&PlatformWindow::nativeChromeWindowProc)) {
        SetLastError(ERROR_SUCCESS);
        if (SetWindowLongPtrW(hwnd, GWLP_WNDPROC, static_cast<LONG_PTR>(previousNativeChromeWindowProc_)) == 0 &&
                GetLastError() != ERROR_SUCCESS) {
            LOG_WARN("SetWindowLongPtrW failed while removing borderless chrome cursor hook: {}", GetLastError());
        }
    }

    RemovePropW(hwnd, kBorderlessChromeWindowProp);
    nativeChromeWindowHandle_ = nullptr;
    previousNativeChromeWindowProc_ = 0;
#endif
}

void PlatformWindow::applyNativeBorderlessChromeAdornments() {
#if defined(_WIN32)
    HWND hwnd = static_cast<HWND>(nativeWindowHandle());
    if (!hwnd) {
        return;
    }

    const DWM_WINDOW_CORNER_PREFERENCE cornerPreference = DWMWCP_ROUND;
    if (FAILED(DwmSetWindowAttribute(
                hwnd,
                DWMWA_WINDOW_CORNER_PREFERENCE,
                &cornerPreference,
                sizeof(cornerPreference)))) {
        LOG_WARN("DwmSetWindowAttribute corner preference failed");
    }

    COLORREF borderColor = DWMWA_COLOR_DEFAULT;
    const bool active = (SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS) != 0u;
    if (active && accentColorOnTitleBarsEnabled()) {
        borderColor = systemAccentBorderColor().value_or(DWMWA_COLOR_DEFAULT);
    }
    if (FAILED(DwmSetWindowAttribute(
                hwnd,
                DWMWA_BORDER_COLOR,
                &borderColor,
                sizeof(borderColor)))) {
        LOG_WARN("DwmSetWindowAttribute border color failed");
    }
#endif
}

#if defined(_WIN32)
std::intptr_t __stdcall PlatformWindow::nativeChromeWindowProc(
        void* windowHandle,
        unsigned int message,
        std::uintptr_t wParam,
        std::intptr_t lParam) {
    HWND hwnd = static_cast<HWND>(windowHandle);
    PlatformWindow* platformWindow = static_cast<PlatformWindow*>(GetPropW(hwnd, kBorderlessChromeWindowProp));
    if (!platformWindow) {
        return DefWindowProcW(hwnd, message, static_cast<WPARAM>(wParam), static_cast<LPARAM>(lParam));
    }

    return platformWindow->handleNativeChromeWindowMessage(windowHandle, message, wParam, lParam);
}

std::intptr_t PlatformWindow::handleNativeChromeWindowMessage(
        void* windowHandle,
        unsigned int message,
        std::uintptr_t wParam,
        std::intptr_t lParam) {
    HWND hwnd = static_cast<HWND>(windowHandle);
    auto callPreviousWindowProc = [&]() -> std::intptr_t {
        if (previousNativeChromeWindowProc_ != 0) {
            return CallWindowProcW(
                    reinterpret_cast<WNDPROC>(previousNativeChromeWindowProc_),
                    hwnd,
                    message,
                    static_cast<WPARAM>(wParam),
                    static_cast<LPARAM>(lParam));
        }

        return DefWindowProcW(hwnd, message, static_cast<WPARAM>(wParam), static_cast<LPARAM>(lParam));
    };

    if (borderlessChrome_ && !chromeInteraction_.active &&
            (message == WM_SETCURSOR || message == WM_MOUSEMOVE || message == WM_NCMOUSEMOVE)) {
        POINT cursorPoint{};
        if (GetCursorPos(&cursorPoint)) {
            const SDL_HitTestResult hit = nativeChromeHitTestFromScreenPoint(cursorPoint.x, cursorPoint.y);
            if (HCURSOR cursor = nativeChromeCursorForHit(hit)) {
                SetCursor(cursor);
                chromeResizeCursorActive_ = true;
                if (message == WM_SETCURSOR) {
                    return TRUE;
                }
            } else {
                releaseChromeResizeCursor();
            }
        }
    }

    if (borderlessChrome_ && chromeResizeCursorActive_) {
        switch (message) {
        case WM_MOUSEMOVE:
        case WM_NCMOUSEMOVE: {
            POINT cursorPoint{};
            if (GetCursorPos(&cursorPoint) &&
                    !isNativeChromeResizeHit(nativeChromeHitTestFromScreenPoint(cursorPoint.x, cursorPoint.y))) {
                releaseChromeResizeCursor();
            }
            break;
        }
        case WM_MOUSELEAVE:
        case WM_NCMOUSELEAVE:
        case WM_KILLFOCUS:
            releaseChromeResizeCursor();
            break;
        default:
            break;
        }
    }

    return callPreviousWindowProc();
}

SDL_HitTestResult PlatformWindow::nativeChromeHitTestFromScreenPoint(int screenX, int screenY) const {
    if (!window_ || !nativeChromeWindowHandle_ || !borderlessChrome_) {
        return SDL_HITTEST_NORMAL;
    }

    HWND hwnd = static_cast<HWND>(nativeChromeWindowHandle_);
    RECT clientRect{};
    if (!GetClientRect(hwnd, &clientRect)) {
        return SDL_HITTEST_NORMAL;
    }

    const int width = std::max(1, static_cast<int>(clientRect.right - clientRect.left));
    const int height = std::max(1, static_cast<int>(clientRect.bottom - clientRect.top));
    POINT clientPoint{static_cast<LONG>(screenX), static_cast<LONG>(screenY)};
    if (!ScreenToClient(hwnd, &clientPoint)) {
        return SDL_HITTEST_NORMAL;
    }

    const int outsideResizeBand = std::max(1, chromeHitTest_.resizeBorder);
    if (clientPoint.x < -outsideResizeBand ||
            clientPoint.y < -outsideResizeBand ||
            clientPoint.x >= width + outsideResizeBand ||
            clientPoint.y >= height + outsideResizeBand) {
        return SDL_HITTEST_NORMAL;
    }

    PlatformWindowChromeHitTest hitTest = chromeHitTest_;
    hitTest.windowWidth = width;
    hitTest.windowHeight = height;
    hitTest.pointX = std::clamp(static_cast<int>(clientPoint.x), 0, std::max(0, width - 1));
    hitTest.pointY = std::clamp(static_cast<int>(clientPoint.y), 0, std::max(0, height - 1));
    hitTest.maximized = IsZoomed(hwnd) != FALSE;
    return platformWindowChromeHitTestResult(hitTest);
}
#endif

void PlatformWindow::beginChromeInteraction(
        SDL_HitTestResult hit,
        float globalMouseX,
        float globalMouseY,
        float windowMouseX,
        float windowMouseY) {
    if (!window_ || hit == SDL_HITTEST_NORMAL) {
        return;
    }

    int windowWidth = 1;
    int windowHeight = 1;
    SDL_GetWindowSize(window_, &windowWidth, &windowHeight);
    windowWidth = std::max(1, windowWidth);
    windowHeight = std::max(1, windowHeight);

    const bool pendingMaximizedRestore = hit == SDL_HITTEST_DRAGGABLE && maximized();

    int windowX = 0;
    int windowY = 0;
    SDL_GetWindowPosition(window_, &windowX, &windowY);
    SDL_GetWindowSize(window_, &windowWidth, &windowHeight);

    chromeInteraction_ = {
            .active = true,
            .pendingMaximizedRestore = pendingMaximizedRestore,
            .hit = hit,
            .startFrame = {
                    .x = windowX,
                    .y = windowY,
                    .width = std::max(1, windowWidth),
                    .height = std::max(1, windowHeight),
            },
            .startGlobalMouseX = globalMouseX,
            .startGlobalMouseY = globalMouseY,
            .restoreWindowMouseX = windowMouseX,
            .restoreWindowMouseY = windowMouseY,
            .moveOffsetX = static_cast<int>(std::lround(globalMouseX)) - windowX,
            .moveOffsetY = static_cast<int>(std::lround(globalMouseY)) - windowY,
    };

    if (!SDL_CaptureMouse(true)) {
        LOG_WARN("SDL_CaptureMouse failed for borderless window chrome: {}", SDL_GetError());
    } else {
        chromeMouseCaptured_ = true;
    }
    setChromeCursor(hit);
}

void PlatformWindow::updateChromeInteraction(float globalMouseX, float globalMouseY) {
    if (!window_ || !chromeInteraction_.active) {
        return;
    }

    if (chromeInteraction_.hit == SDL_HITTEST_DRAGGABLE) {
        if (chromeInteraction_.pendingMaximizedRestore) {
            const float deltaX = globalMouseX - chromeInteraction_.startGlobalMouseX;
            const float deltaY = globalMouseY - chromeInteraction_.startGlobalMouseY;
            if (std::abs(deltaX) < kChromeDragRestoreThreshold &&
                    std::abs(deltaY) < kChromeDragRestoreThreshold) {
                return;
            }

            const float xRatio = std::clamp(
                    chromeInteraction_.restoreWindowMouseX / static_cast<float>(std::max(1, chromeInteraction_.startFrame.width)),
                    0.0f,
                    1.0f);
            const int restoreMouseY = std::max(0,
                    std::min(
                            static_cast<int>(std::lround(chromeInteraction_.restoreWindowMouseY)),
                            chromeHitTest_.titleBarHeight));
            if (!SDL_RestoreWindow(window_)) {
                LOG_WARN("SDL_RestoreWindow failed before toolbar drag: {}", SDL_GetError());
            }
            if (!SDL_SyncWindow(window_)) {
                LOG_WARN("SDL_SyncWindow failed after toolbar drag restore: {}", SDL_GetError());
            }

            int restoredWidth = 1;
            int restoredHeight = 1;
            SDL_GetWindowSize(window_, &restoredWidth, &restoredHeight);
            restoredWidth = std::max(1, restoredWidth);
            restoredHeight = std::max(1, restoredHeight);
            const int restoreMouseX = std::clamp(
                    static_cast<int>(std::lround(xRatio * static_cast<float>(restoredWidth))),
                    0,
                    std::max(0, restoredWidth - 1));
            const int restoredX = static_cast<int>(std::lround(globalMouseX)) - restoreMouseX;
            const int restoredY = static_cast<int>(std::lround(globalMouseY)) - restoreMouseY;
            if (!SDL_SetWindowPosition(window_, restoredX, restoredY)) {
                LOG_WARN("SDL_SetWindowPosition failed after toolbar drag restore: {}", SDL_GetError());
            }
            if (!SDL_SyncWindow(window_)) {
                LOG_WARN("SDL_SyncWindow failed after toolbar drag positioning: {}", SDL_GetError());
            }

            chromeInteraction_.pendingMaximizedRestore = false;
            chromeInteraction_.moveOffsetX = restoreMouseX;
            chromeInteraction_.moveOffsetY = restoreMouseY;
            chromeInteraction_.startFrame = {
                    .x = restoredX,
                    .y = restoredY,
                    .width = restoredWidth,
                    .height = restoredHeight,
            };
        }

        const int x = static_cast<int>(std::lround(globalMouseX)) - chromeInteraction_.moveOffsetX;
        const int y = static_cast<int>(std::lround(globalMouseY)) - chromeInteraction_.moveOffsetY;
        if (!SDL_SetWindowPosition(window_, x, y)) {
            LOG_WARN("SDL_SetWindowPosition failed during toolbar drag: {}", SDL_GetError());
        }
        return;
    }

    const PlatformWindowChromeFrame frame = platformWindowChromeResizeFrame({
            .start = chromeInteraction_.startFrame,
            .deltaX = static_cast<int>(std::lround(globalMouseX - chromeInteraction_.startGlobalMouseX)),
            .deltaY = static_cast<int>(std::lround(globalMouseY - chromeInteraction_.startGlobalMouseY)),
            .edge = chromeInteraction_.hit,
    });

    if (!SDL_SetWindowPosition(window_, frame.x, frame.y)) {
        LOG_WARN("SDL_SetWindowPosition failed during borderless resize: {}", SDL_GetError());
    }
    if (!SDL_SetWindowSize(window_, frame.width, frame.height)) {
        LOG_WARN("SDL_SetWindowSize failed during borderless resize: {}", SDL_GetError());
    }
}

void PlatformWindow::endChromeInteraction() {
    if (!chromeInteraction_.active) {
        return;
    }

    chromeInteraction_ = {};
    if (chromeMouseCaptured_) {
        if (!SDL_CaptureMouse(false)) {
            LOG_WARN("SDL_CaptureMouse release failed for borderless window chrome: {}", SDL_GetError());
        }
        chromeMouseCaptured_ = false;
    }
    setChromeCursor(SDL_HITTEST_NORMAL);
    refreshPixelSize();
    refreshNativeChrome();
}

void PlatformWindow::cancelChromeInteraction() {
    if (!chromeInteraction_.active && !chromeMouseCaptured_) {
        return;
    }

    chromeInteraction_ = {};
    if (chromeMouseCaptured_) {
        if (!SDL_CaptureMouse(false)) {
            LOG_WARN("SDL_CaptureMouse release failed for borderless window chrome: {}", SDL_GetError());
        }
        chromeMouseCaptured_ = false;
    }
    setChromeCursor(SDL_HITTEST_NORMAL);
    refreshPixelSize();
    refreshNativeChrome();
}

void PlatformWindow::updateChromeHoverCursor(float windowMouseX, float windowMouseY) {
    if (!window_) {
        return;
    }

    int width = 1;
    int height = 1;
    SDL_GetWindowSize(window_, &width, &height);
    PlatformWindowChromeHitTest hitTest = chromeHitTest_;
    hitTest.windowWidth = std::max(1, width);
    hitTest.windowHeight = std::max(1, height);
    hitTest.pointX = static_cast<int>(std::lround(windowMouseX));
    hitTest.pointY = static_cast<int>(std::lround(windowMouseY));
    hitTest.maximized = maximized();
    const SDL_HitTestResult hit = platformWindowChromeHitTestResult(hitTest);
    if (hit != SDL_HITTEST_NORMAL && hit != SDL_HITTEST_DRAGGABLE) {
        setChromeCursor(hit);
        return;
    }

    releaseChromeResizeCursor();
}

void PlatformWindow::releaseChromeResizeCursor() {
    if (!chromeResizeCursorActive_) {
        return;
    }

    if (SDL_Cursor* cursor = chromeCursor(SDL_SYSTEM_CURSOR_DEFAULT)) {
        SDL_SetCursor(cursor);
    }
#if defined(_WIN32)
    SetCursor(LoadCursor(nullptr, IDC_ARROW));
#endif
    chromeResizeCursorActive_ = false;
}

void PlatformWindow::setChromeCursor(SDL_HitTestResult hit) {
    SDL_SystemCursor cursor = SDL_SYSTEM_CURSOR_DEFAULT;
    switch (hit) {
    case SDL_HITTEST_RESIZE_TOPLEFT:
        cursor = SDL_SYSTEM_CURSOR_NW_RESIZE;
        break;
    case SDL_HITTEST_RESIZE_TOP:
        cursor = SDL_SYSTEM_CURSOR_N_RESIZE;
        break;
    case SDL_HITTEST_RESIZE_TOPRIGHT:
        cursor = SDL_SYSTEM_CURSOR_NE_RESIZE;
        break;
    case SDL_HITTEST_RESIZE_RIGHT:
        cursor = SDL_SYSTEM_CURSOR_E_RESIZE;
        break;
    case SDL_HITTEST_RESIZE_BOTTOMRIGHT:
        cursor = SDL_SYSTEM_CURSOR_SE_RESIZE;
        break;
    case SDL_HITTEST_RESIZE_BOTTOM:
        cursor = SDL_SYSTEM_CURSOR_S_RESIZE;
        break;
    case SDL_HITTEST_RESIZE_BOTTOMLEFT:
        cursor = SDL_SYSTEM_CURSOR_SW_RESIZE;
        break;
    case SDL_HITTEST_RESIZE_LEFT:
        cursor = SDL_SYSTEM_CURSOR_W_RESIZE;
        break;
    default:
        break;
    }

    if (SDL_Cursor* sdlCursor = chromeCursor(cursor)) {
        SDL_SetCursor(sdlCursor);
    }
    chromeResizeCursorActive_ = cursor != SDL_SYSTEM_CURSOR_DEFAULT;
}

SDL_Cursor* PlatformWindow::chromeCursor(SDL_SystemCursor cursor) {
    const std::size_t index = static_cast<std::size_t>(cursor);
    if (index >= chromeCursors_.size()) {
        return SDL_GetDefaultCursor();
    }
    if (!chromeCursors_[index]) {
        chromeCursors_[index] = SDL_CreateSystemCursor(cursor);
    }
    return chromeCursors_[index] ? chromeCursors_[index] : SDL_GetDefaultCursor();
}

} // namespace quader

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "platform/platform_window.hpp"

#include "core/logger.hpp"
#include "platform/sdl_event_utils.hpp"

#include <SDL3/SDL_hints.h>
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
#include <windowsx.h>
#include <dwmapi.h>
#endif

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <optional>
#include <utility>
#include <vector>

namespace quader {
namespace {

int gSdlVideoWindowCount = 0;
constexpr float kChromeDragRestoreThreshold = 4.0f;

#if defined(_WIN32)
constexpr wchar_t kDwmRegistryKey[] = L"SOFTWARE\\Microsoft\\Windows\\DWM";
constexpr wchar_t kBorderlessChromeWindowProp[] = L"QuaderPlatformWindowChrome";

/**
 * Stores monitor and work-area rectangles used for native borderless maximize.
 */
struct NativeMonitorWorkArea {
    RECT monitor;
    RECT work;
};

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
    case SDL_HITTEST_DRAGGABLE:
        return HTCAPTION;
    default:
        return HTCLIENT;
    }
}

bool isNativeChromeResizeHit(SDL_HitTestResult hit) {
    switch (hit) {
    case SDL_HITTEST_RESIZE_TOPLEFT:
    case SDL_HITTEST_RESIZE_TOP:
    case SDL_HITTEST_RESIZE_TOPRIGHT:
    case SDL_HITTEST_RESIZE_RIGHT:
    case SDL_HITTEST_RESIZE_BOTTOMRIGHT:
    case SDL_HITTEST_RESIZE_BOTTOM:
    case SDL_HITTEST_RESIZE_BOTTOMLEFT:
    case SDL_HITTEST_RESIZE_LEFT:
        return true;
    default:
        return false;
    }
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

PlatformWindowChromeFrame frameFromRect(const RECT& rect) {
    return {
            .x = static_cast<int>(rect.left),
            .y = static_cast<int>(rect.top),
            .width = std::max(1, static_cast<int>(rect.right - rect.left)),
            .height = std::max(1, static_cast<int>(rect.bottom - rect.top)),
    };
}

RECT rectFromFrame(PlatformWindowChromeFrame frame) {
    RECT rect{};
    rect.left = static_cast<LONG>(frame.x);
    rect.top = static_cast<LONG>(frame.y);
    rect.right = static_cast<LONG>(frame.x + std::max(1, frame.width));
    rect.bottom = static_cast<LONG>(frame.y + std::max(1, frame.height));
    return rect;
}

std::optional<COLORREF> systemAccentBorderColor() {
    const std::optional<DWORD> accentColor = readDwmRegistryDword(L"AccentColor");
    if (!accentColor) {
        return std::nullopt;
    }

    return RGB(GetRValue(*accentColor), GetGValue(*accentColor), GetBValue(*accentColor));
}

std::optional<NativeMonitorWorkArea> nativeMonitorWorkArea(HWND hwnd) {
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (monitor == nullptr) {
        return std::nullopt;
    }

    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!GetMonitorInfoW(monitor, &monitorInfo)) {
        return std::nullopt;
    }

    return NativeMonitorWorkArea{
            .monitor = monitorInfo.rcMonitor,
            .work = monitorInfo.rcWork,
    };
}

void applyBorderlessChromeMaximizedWorkArea(HWND hwnd, MINMAXINFO& minMaxInfo) {
    const std::optional<NativeMonitorWorkArea> area = nativeMonitorWorkArea(hwnd);
    if (!area) {
        return;
    }

    const RECT& workArea = area->work;
    const RECT& monitorArea = area->monitor;
    minMaxInfo.ptMaxPosition.x = workArea.left - monitorArea.left;
    minMaxInfo.ptMaxPosition.y = workArea.top - monitorArea.top;
    minMaxInfo.ptMaxSize.x = workArea.right - workArea.left;
    minMaxInfo.ptMaxSize.y = workArea.bottom - workArea.top;
    minMaxInfo.ptMaxTrackSize.x = minMaxInfo.ptMaxSize.x;
    minMaxInfo.ptMaxTrackSize.y = minMaxInfo.ptMaxSize.y;
    minMaxInfo.ptMinTrackSize.x = std::max<LONG>(minMaxInfo.ptMinTrackSize.x, kPlatformWindowMinimumWidth);
    minMaxInfo.ptMinTrackSize.y = std::max<LONG>(minMaxInfo.ptMinTrackSize.y, kPlatformWindowMinimumHeight);
}

int nativeChromeMaximizedResizeFrameThicknessX(HWND hwnd) {
    const UINT dpi = GetDpiForWindow(hwnd);
    return platformWindowChromeResizeBorderForSystemMetrics(
            0,
            GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi),
            GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi));
}

int nativeChromeMaximizedResizeFrameThicknessY(HWND hwnd) {
    const UINT dpi = GetDpiForWindow(hwnd);
    return platformWindowChromeResizeBorderForSystemMetrics(
            0,
            GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi),
            GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi));
}

bool applyBorderlessChromeMaximizedClientArea(HWND hwnd, NCCALCSIZE_PARAMS& params) {
    if (IsZoomed(hwnd) == FALSE) {
        return false;
    }

    const std::optional<NativeMonitorWorkArea> area = nativeMonitorWorkArea(hwnd);
    if (area) {
        params.rgrc[0] = area->work;
        return true;
    }

    const int frameThicknessX = std::max(0, nativeChromeMaximizedResizeFrameThicknessX(hwnd));
    const int frameThicknessY = std::max(0, nativeChromeMaximizedResizeFrameThicknessY(hwnd));
    params.rgrc[0].left += frameThicknessX;
    params.rgrc[0].top += frameThicknessY;
    params.rgrc[0].right -= frameThicknessX;
    params.rgrc[0].bottom -= frameThicknessY;
    return true;
}
#endif

std::vector<PlatformWindowChromeFrame> displayWorkAreas() {
    std::vector<PlatformWindowChromeFrame> areas;
    int displayCount = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&displayCount);
    if (displays == nullptr || displayCount <= 0) {
        if (displays != nullptr) {
            SDL_free(displays);
        }
        return areas;
    }

    areas.reserve(static_cast<std::size_t>(displayCount));
    for (int index = 0; index < displayCount; ++index) {
        SDL_Rect rect{};
        if (SDL_GetDisplayUsableBounds(displays[index], &rect) && rect.w > 0 && rect.h > 0) {
            areas.push_back({
                    .x = rect.x,
                    .y = rect.y,
                    .width = rect.w,
                    .height = rect.h,
            });
        }
    }
    SDL_free(displays);
    return areas;
}

std::int64_t intersectionArea(PlatformWindowChromeFrame frame, PlatformWindowChromeFrame area) {
    const int left = std::max(frame.x, area.x);
    const int top = std::max(frame.y, area.y);
    const int right = std::min(frame.x + std::max(1, frame.width), area.x + std::max(1, area.width));
    const int bottom = std::min(frame.y + std::max(1, frame.height), area.y + std::max(1, area.height));
    if (right <= left || bottom <= top) {
        return 0;
    }
    return static_cast<std::int64_t>(right - left) * static_cast<std::int64_t>(bottom - top);
}

int clampWindowOrigin(int origin, int size, int areaOrigin, int areaSize) {
    if (size >= areaSize) {
        return areaOrigin;
    }
    return std::clamp(origin, areaOrigin, areaOrigin + areaSize - size);
}

bool acquireSdlVideo() {
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
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
    const int leftInteractiveWidth = std::clamp(hitTest.leftInteractiveWidth, 0, windowWidth);
    const int rightControlsWidth = std::max(0, hitTest.rightControlsWidth);
    const int draggableRight = std::max(0, windowWidth - rightControlsWidth);
    const int draggableLeft = std::min(leftInteractiveWidth, draggableRight);
    if (pointY < titleBarHeight && pointX >= draggableLeft && pointX < draggableRight) {
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

int platformWindowChromeResizeBorderForSystemMetrics(int fallbackBorder, int sizeFrame, int paddedBorder) {
    return std::max({1, fallbackBorder, std::max(0, sizeFrame) + std::max(0, paddedBorder)});
}

bool platformWindowCanRenderFrames(std::uint64_t windowFlags, int pixelWidth, int pixelHeight) {
    constexpr std::uint64_t kNonDrawableWindowFlags =
            SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN | SDL_WINDOW_OCCLUDED;
    return (windowFlags & kNonDrawableWindowFlags) == 0u && pixelWidth > 0 && pixelHeight > 0;
}

PlatformWindowChromeFrame platformWindowRecoverFrameToDisplayWorkArea(
        PlatformWindowChromeFrame frame,
        std::span<const PlatformWindowChromeFrame> displayWorkAreas) {
    frame.width = std::max(kPlatformWindowMinimumWidth, frame.width);
    frame.height = std::max(kPlatformWindowMinimumHeight, frame.height);
    if (displayWorkAreas.empty()) {
        return frame;
    }

    const PlatformWindowChromeFrame* targetArea = &displayWorkAreas.front();
    std::int64_t bestArea = 0;
    for (const PlatformWindowChromeFrame& workArea : displayWorkAreas) {
        if (workArea.width <= 0 || workArea.height <= 0) {
            continue;
        }
        const std::int64_t area = intersectionArea(frame, workArea);
        if (area > bestArea) {
            bestArea = area;
            targetArea = &workArea;
        }
    }

    const int maxWidth = std::max(kPlatformWindowMinimumWidth, targetArea->width);
    const int maxHeight = std::max(kPlatformWindowMinimumHeight, targetArea->height);
    frame.width = std::clamp(frame.width, kPlatformWindowMinimumWidth, maxWidth);
    frame.height = std::clamp(frame.height, kPlatformWindowMinimumHeight, maxHeight);
    frame.x = clampWindowOrigin(frame.x, frame.width, targetArea->x, std::max(1, targetArea->width));
    frame.y = clampWindowOrigin(frame.y, frame.height, targetArea->y, std::max(1, targetArea->height));
    return frame;
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
#if defined(_WIN32)
        clearNativeResizeState();
#endif
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
    if (!window_) {
        return;
    }
    if (!SDL_MinimizeWindow(window_)) {
        LOG_WARN("SDL_MinimizeWindow failed: {}", SDL_GetError());
        return;
    }
    if (!SDL_SyncWindow(window_)) {
        LOG_WARN("SDL_SyncWindow failed after minimize: {}", SDL_GetError());
    }
    refreshPixelSize();
}

void PlatformWindow::toggleMaximizeRestore() {
    if (!window_) {
        return;
    }

#if defined(_WIN32)
    if (borderlessChrome_ && nativeChromeWindowHandle_ != nullptr) {
        HWND hwnd = static_cast<HWND>(nativeChromeWindowHandle_);
        if (IsZoomed(hwnd) != FALSE) {
            ShowWindow(hwnd, SW_RESTORE);
            if (!SDL_SyncWindow(window_)) {
                LOG_WARN("SDL_SyncWindow failed after native borderless restore: {}", SDL_GetError());
            }
            refreshPixelSize();
            requestLiveResizeFrame(true);
            return;
        }

        ShowWindow(hwnd, SW_MAXIMIZE);
        if (!SDL_SyncWindow(window_)) {
            LOG_WARN("SDL_SyncWindow failed after native borderless maximize: {}", SDL_GetError());
        }
        refreshPixelSize();
        requestLiveResizeFrame(true);
        return;
    }
#endif

    if (maximized()) {
        if (!SDL_RestoreWindow(window_)) {
            LOG_WARN("SDL_RestoreWindow failed: {}", SDL_GetError());
        }
        requestLiveResizeFrame(true);
        return;
    }

    if (!SDL_MaximizeWindow(window_)) {
        LOG_WARN("SDL_MaximizeWindow failed: {}", SDL_GetError());
    }
    requestLiveResizeFrame(true);
}

std::optional<PlatformWindowPlacement> PlatformWindow::placement() const {
    if (!window_) {
        return std::nullopt;
    }

#if defined(_WIN32)
    if (nativeChromeWindowHandle_ != nullptr) {
        HWND hwnd = static_cast<HWND>(nativeChromeWindowHandle_);
        WINDOWPLACEMENT nativePlacement{};
        nativePlacement.length = sizeof(nativePlacement);
        if (GetWindowPlacement(hwnd, &nativePlacement)) {
            return PlatformWindowPlacement{
                    .normalFrame = frameFromRect(nativePlacement.rcNormalPosition),
                    .maximized = nativePlacement.showCmd == SW_SHOWMAXIMIZED ||
                            (nativePlacement.flags & WPF_RESTORETOMAXIMIZED) != 0u ||
                            IsZoomed(hwnd) != FALSE,
            };
        }
        LOG_WARN("GetWindowPlacement failed while saving window placement: {}", GetLastError());
    }
#endif

    int x = 0;
    int y = 0;
    int width = 1;
    int height = 1;
    if (!SDL_GetWindowPosition(window_, &x, &y)) {
        LOG_WARN("SDL_GetWindowPosition failed while saving window placement: {}", SDL_GetError());
    }
    if (!SDL_GetWindowSize(window_, &width, &height)) {
        LOG_WARN("SDL_GetWindowSize failed while saving window placement: {}", SDL_GetError());
    }
    return PlatformWindowPlacement{
            .normalFrame = {
                    .x = x,
                    .y = y,
                    .width = std::max(1, width),
                    .height = std::max(1, height),
            },
            .maximized = maximized(),
    };
}

void PlatformWindow::applyPlacement(const PlatformWindowPlacement& placement) {
    if (!window_) {
        return;
    }

    const std::vector<PlatformWindowChromeFrame> workAreas = displayWorkAreas();
    const PlatformWindowChromeFrame recoveredFrame =
            platformWindowRecoverFrameToDisplayWorkArea(placement.normalFrame, workAreas);

#if defined(_WIN32)
    if (nativeChromeWindowHandle_ != nullptr) {
        HWND hwnd = static_cast<HWND>(nativeChromeWindowHandle_);
        WINDOWPLACEMENT nativePlacement{};
        nativePlacement.length = sizeof(nativePlacement);
        if (!GetWindowPlacement(hwnd, &nativePlacement)) {
            nativePlacement.length = sizeof(nativePlacement);
        }
        nativePlacement.flags = 0;
        nativePlacement.showCmd = SW_SHOWNORMAL;
        nativePlacement.rcNormalPosition = rectFromFrame(recoveredFrame);
        if (!SetWindowPlacement(hwnd, &nativePlacement)) {
            LOG_WARN("SetWindowPlacement failed while applying window placement: {}", GetLastError());
        }
        if (placement.maximized) {
            ShowWindow(hwnd, SW_MAXIMIZE);
        }
        if (!SDL_SyncWindow(window_)) {
            LOG_WARN("SDL_SyncWindow failed after applying native window placement: {}", SDL_GetError());
        }
        refreshPixelSize();
        requestLiveResizeFrame(true);
        return;
    }
#endif

    if (!SDL_SetWindowSize(window_, recoveredFrame.width, recoveredFrame.height)) {
        LOG_WARN("SDL_SetWindowSize failed while applying window placement: {}", SDL_GetError());
    }
    if (!SDL_SetWindowPosition(window_, recoveredFrame.x, recoveredFrame.y)) {
        LOG_WARN("SDL_SetWindowPosition failed while applying window placement: {}", SDL_GetError());
    }
    if (placement.maximized && !SDL_MaximizeWindow(window_)) {
        LOG_WARN("SDL_MaximizeWindow failed while applying window placement: {}", SDL_GetError());
    }
    if (!SDL_SyncWindow(window_)) {
        LOG_WARN("SDL_SyncWindow failed after applying window placement: {}", SDL_GetError());
    }
    refreshPixelSize();
    requestLiveResizeFrame(true);
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
        case SDL_EVENT_WINDOW_MINIMIZED:
        case SDL_EVENT_WINDOW_HIDDEN:
        case SDL_EVENT_WINDOW_OCCLUDED:
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
#if defined(_WIN32)
    if (nativeChromeWindowHandle_ != nullptr &&
            (isNativeChromeResizeHit(hit) || hit == SDL_HITTEST_DRAGGABLE)) {
        return true;
    }
#endif
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
#if defined(_WIN32)
    if (nativeChromeWindowHandle_ != nullptr) {
        return IsZoomed(static_cast<HWND>(nativeChromeWindowHandle_)) != FALSE;
    }
#endif
    return window_ && (SDL_GetWindowFlags(window_) & SDL_WINDOW_MAXIMIZED) != 0u;
}

bool PlatformWindow::canRenderFrames() const {
    if (!window_) {
        return false;
    }

    int pixelWidth = 0;
    int pixelHeight = 0;
    SDL_GetWindowSizeInPixels(window_, &pixelWidth, &pixelHeight);
    return platformWindowCanRenderFrames(SDL_GetWindowFlags(window_), pixelWidth, pixelHeight);
}

bool PlatformWindow::chromeResizeActive() const {
#if defined(_WIN32)
    if (nativeResizeActive_) {
        return true;
    }
#endif
    return chromeInteraction_.active && chromeInteraction_.hit != SDL_HITTEST_DRAGGABLE;
}

void PlatformWindow::setLiveResizeFrameCallback(LiveResizeFrameCallback callback) {
    liveResizeFrameCallback_ = std::move(callback);
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

    if (borderlessChrome_ && message == WM_GETMINMAXINFO && lParam != 0) {
        const std::intptr_t result = callPreviousWindowProc();
        applyBorderlessChromeMaximizedWorkArea(hwnd, *reinterpret_cast<MINMAXINFO*>(lParam));
        return result;
    }

    if (borderlessChrome_ && message == WM_NCCALCSIZE && wParam != 0 && lParam != 0) {
        if (applyBorderlessChromeMaximizedClientArea(hwnd, *reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam))) {
            return 0;
        }
    }

    if (borderlessChrome_ && !chromeInteraction_.active && message == WM_NCHITTEST) {
        const SDL_HitTestResult hit = nativeChromeHitTestFromScreenPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        const LRESULT hitCode = nativeChromeHitTestCodeForHit(hit);
        if (hitCode != HTCLIENT) {
            return hitCode;
        }
    }

    if (borderlessChrome_) {
        if (message == WM_ENTERSIZEMOVE) {
            resetLiveResizeFrameCoalescing();
        } else if (message == WM_SIZING) {
            nativeResizeActive_ = true;
        }
    }

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
        case WM_CANCELMODE:
        case WM_CAPTURECHANGED:
            releaseChromeResizeCursor();
            break;
        default:
            break;
        }
    }

    const bool redrawNativeResizeFrame =
            borderlessChrome_ &&
            (nativeResizeActive_ || IsZoomed(hwnd) != FALSE) &&
            (message == WM_SIZE || message == WM_WINDOWPOSCHANGED || message == WM_DPICHANGED);
    const bool clearNativeResize =
            borderlessChrome_ &&
            nativeResizeActive_ &&
            (message == WM_EXITSIZEMOVE ||
                    message == WM_CANCELMODE ||
                    message == WM_CAPTURECHANGED ||
                    message == WM_KILLFOCUS ||
                    message == WM_NCDESTROY);

    const std::intptr_t result = callPreviousWindowProc();

    if (redrawNativeResizeFrame) {
        requestLiveResizeFrame();
    }
    if (clearNativeResize) {
        clearNativeResizeState();
        requestLiveResizeFrame(true);
    }

    return result;
}

int PlatformWindow::nativeChromeResizeBorderForWindow(void* windowHandle) const {
    HWND hwnd = static_cast<HWND>(windowHandle);
    if (hwnd == nullptr) {
        return std::max(1, chromeHitTest_.resizeBorder);
    }

    const UINT dpi = GetDpiForWindow(hwnd);
    return platformWindowChromeResizeBorderForSystemMetrics(
            chromeHitTest_.resizeBorder,
            GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi),
            GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi));
}

void PlatformWindow::clearNativeResizeState() {
    nativeResizeActive_ = false;
    resetLiveResizeFrameCoalescing();
    releaseChromeResizeCursor();
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

    const int resizeBorder = nativeChromeResizeBorderForWindow(hwnd);
    const int outsideResizeBand = std::max(1, resizeBorder);
    if (clientPoint.x < -outsideResizeBand ||
            clientPoint.y < -outsideResizeBand ||
            clientPoint.x >= width + outsideResizeBand ||
            clientPoint.y >= height + outsideResizeBand) {
        return SDL_HITTEST_NORMAL;
    }

    PlatformWindowChromeHitTest hitTest = chromeHitTest_;
    hitTest.windowWidth = width;
    hitTest.windowHeight = height;
    hitTest.resizeBorder = resizeBorder;
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

#if defined(_WIN32)
    if (nativeChromeWindowHandle_ != nullptr) {
        HWND hwnd = static_cast<HWND>(nativeChromeWindowHandle_);
        if (SetWindowPos(
                    hwnd,
                    nullptr,
                    frame.x,
                    frame.y,
                    frame.width,
                    frame.height,
                    SWP_NOZORDER | SWP_NOACTIVATE)) {
            return;
        }
        LOG_WARN("SetWindowPos failed during borderless resize: {}", GetLastError());
    }
#endif

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
    resetLiveResizeFrameCoalescing();
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
    resetLiveResizeFrameCoalescing();
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

void PlatformWindow::requestLiveResizeFrame(bool force) {
    refreshPixelSize();
    if (!force &&
            liveResizeFrameSizeValid_ &&
            liveResizeFrameWidthPixels_ == widthPixels_ &&
            liveResizeFrameHeightPixels_ == heightPixels_) {
        return;
    }
    liveResizeFrameSizeValid_ = true;
    liveResizeFrameWidthPixels_ = widthPixels_;
    liveResizeFrameHeightPixels_ = heightPixels_;
    if (liveResizeFrameCallback_) {
        liveResizeFrameCallback_();
    }
}

void PlatformWindow::resetLiveResizeFrameCoalescing() {
    liveResizeFrameSizeValid_ = false;
    liveResizeFrameWidthPixels_ = 0;
    liveResizeFrameHeightPixels_ = 0;
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

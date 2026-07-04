////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "platform/platform_window.hpp"

#include "core/logger.hpp"
#include "platform/platform_event.hpp"
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

int g_sdl_video_window_count = 0;
constexpr float kChromeDragRestoreThreshold = 4.0f;

#if defined(_WIN32)
constexpr wchar_t kDwmRegistryKey[] = L"SOFTWARE\\Microsoft\\Windows\\DWM";
constexpr wchar_t kBorderlessChromeWindowProp[] = L"QuaderPlatformWindowChrome";
constexpr UINT kLiveResizeFrameMessage = WM_APP + 0x51u;

/**
 * Stores monitor and work-area rectangles used for native borderless maximize.
 */
struct NativeMonitorWorkArea {
    RECT monitor;
    RECT work;
};

HCURSOR native_chrome_cursor_for_hit(SDL_HitTestResult hit) {
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

LRESULT native_chrome_hit_test_code_for_hit(SDL_HitTestResult hit) {
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

bool is_native_chrome_resize_hit(SDL_HitTestResult hit) {
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

bool is_native_chrome_system_hit(SDL_HitTestResult hit) {
  return is_native_chrome_resize_hit(hit) || hit == SDL_HITTEST_DRAGGABLE;
}

std::optional<DWORD> read_dwm_registry_dword(const wchar_t *value_name) {
  DWORD value = 0;
  DWORD value_size = sizeof(value);
  const LSTATUS result =
      RegGetValueW(HKEY_CURRENT_USER, kDwmRegistryKey, value_name,
                   RRF_RT_REG_DWORD, nullptr, &value, &value_size);
  if (result != ERROR_SUCCESS) {
    return std::nullopt;
  }

  return value;
}

bool accent_color_on_title_bars_enabled() {
  return read_dwm_registry_dword(L"ColorPrevalence").value_or(0u) != 0u;
}

PlatformWindowChromeFrame frame_from_rect(const RECT &rect) {
  return {
      .x = static_cast<int>(rect.left),
      .y = static_cast<int>(rect.top),
      .width = std::max(1, static_cast<int>(rect.right - rect.left)),
      .height = std::max(1, static_cast<int>(rect.bottom - rect.top)),
  };
}

RECT rect_from_frame(PlatformWindowChromeFrame frame) {
  RECT rect{};
  rect.left = static_cast<LONG>(frame.x);
  rect.top = static_cast<LONG>(frame.y);
  rect.right = static_cast<LONG>(frame.x + std::max(1, frame.width));
  rect.bottom = static_cast<LONG>(frame.y + std::max(1, frame.height));
  return rect;
}

std::optional<COLORREF> system_accent_border_color() {
  const std::optional<DWORD> accent_color =
      read_dwm_registry_dword(L"AccentColor");
  if (!accent_color) {
    return std::nullopt;
  }

  return RGB(GetRValue(*accent_color), GetGValue(*accent_color),
             GetBValue(*accent_color));
}

std::optional<NativeMonitorWorkArea> native_monitor_work_area(HWND hwnd) {
  HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
  if (monitor == nullptr) {
    return std::nullopt;
  }

  MONITORINFO monitor_info{};
  monitor_info.cbSize = sizeof(monitor_info);
  if (!GetMonitorInfoW(monitor, &monitor_info)) {
    return std::nullopt;
  }

  return NativeMonitorWorkArea{
      .monitor = monitor_info.rcMonitor,
      .work = monitor_info.rcWork,
  };
}

void apply_borderless_chrome_maximized_work_area(HWND hwnd,
                                                 MINMAXINFO &min_max_info) {
  const std::optional<NativeMonitorWorkArea> area =
      native_monitor_work_area(hwnd);
  if (!area) {
    return;
  }

  const RECT &work_area = area->work;
  const RECT &monitor_area = area->monitor;
  min_max_info.ptMaxPosition.x = work_area.left - monitor_area.left;
  min_max_info.ptMaxPosition.y = work_area.top - monitor_area.top;
  min_max_info.ptMaxSize.x = work_area.right - work_area.left;
  min_max_info.ptMaxSize.y = work_area.bottom - work_area.top;
  min_max_info.ptMaxTrackSize.x = min_max_info.ptMaxSize.x;
  min_max_info.ptMaxTrackSize.y = min_max_info.ptMaxSize.y;
  min_max_info.ptMinTrackSize.x = std::max<LONG>(min_max_info.ptMinTrackSize.x,
                                                 kPlatformWindowMinimumWidth);
  min_max_info.ptMinTrackSize.y = std::max<LONG>(min_max_info.ptMinTrackSize.y,
                                                 kPlatformWindowMinimumHeight);
}

int native_chrome_maximized_resize_frame_thickness_x(HWND hwnd) {
  const UINT dpi = GetDpiForWindow(hwnd);
  return platform_window_chrome_resize_border_for_system_metrics(
      0, GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi),
      GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi));
}

int native_chrome_maximized_resize_frame_thickness_y(HWND hwnd) {
  const UINT dpi = GetDpiForWindow(hwnd);
  return platform_window_chrome_resize_border_for_system_metrics(
      0, GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi),
      GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi));
}

bool apply_borderless_chrome_maximized_client_area(HWND hwnd,
                                                   NCCALCSIZE_PARAMS &params) {
  if (IsZoomed(hwnd) == FALSE) {
    return false;
  }

  const std::optional<NativeMonitorWorkArea> area =
      native_monitor_work_area(hwnd);
  if (area) {
    params.rgrc[0] = area->work;
    return true;
  }

  const int frame_thickness_x =
      std::max(0, native_chrome_maximized_resize_frame_thickness_x(hwnd));
  const int frame_thickness_y =
      std::max(0, native_chrome_maximized_resize_frame_thickness_y(hwnd));
  params.rgrc[0].left += frame_thickness_x;
  params.rgrc[0].top += frame_thickness_y;
  params.rgrc[0].right -= frame_thickness_x;
  params.rgrc[0].bottom -= frame_thickness_y;
  return true;
}
#endif

std::vector<PlatformWindowChromeFrame> display_work_areas() {
  std::vector<PlatformWindowChromeFrame> areas;
  int display_count = 0;
  SDL_DisplayID *displays = SDL_GetDisplays(&display_count);
  if (displays == nullptr || display_count <= 0) {
    if (displays != nullptr) {
      SDL_free(displays);
    }
    return areas;
  }

  areas.reserve(static_cast<std::size_t>(display_count));
  for (int index = 0; index < display_count; ++index) {
    SDL_Rect rect{};
    if (SDL_GetDisplayUsableBounds(displays[index], &rect) && rect.w > 0 &&
        rect.h > 0) {
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

std::int64_t intersection_area(PlatformWindowChromeFrame frame,
                               PlatformWindowChromeFrame area) {
  const int left = std::max(frame.x, area.x);
  const int top = std::max(frame.y, area.y);
  const int right = std::min(frame.x + std::max(1, frame.width),
                             area.x + std::max(1, area.width));
  const int bottom = std::min(frame.y + std::max(1, frame.height),
                              area.y + std::max(1, area.height));
  if (right <= left || bottom <= top) {
    return 0;
  }
  return static_cast<std::int64_t>(right - left) *
         static_cast<std::int64_t>(bottom - top);
}

int clamp_window_origin(int origin, int size, int area_origin, int area_size) {
  if (size >= area_size) {
    return area_origin;
  }
  return std::clamp(origin, area_origin, area_origin + area_size - size);
}

bool acquire_sdl_video() {
  SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
  if (g_sdl_video_window_count == 0 &&
      !SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
    LOG_ERROR("SDL_Init failed: {}", SDL_GetError());
    return false;
  }

  ++g_sdl_video_window_count;
  return true;
}

void release_sdl_video() {
  if (g_sdl_video_window_count <= 0) {
    return;
  }

  --g_sdl_video_window_count;
  if (g_sdl_video_window_count == 0) {
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
  }
}

} // namespace

SDL_HitTestResult platform_window_chrome_hit_test_result(
    const PlatformWindowChromeHitTest &hit_test) {
  const int window_width = std::max(1, hit_test.window_width);
  const int window_height = std::max(1, hit_test.window_height);
  const int point_x = hit_test.point_x;
  const int point_y = hit_test.point_y;
  if (point_x < 0 || point_y < 0 || point_x >= window_width ||
      point_y >= window_height) {
    return SDL_HITTEST_NORMAL;
  }

  const int resize_border = std::max(0, hit_test.resize_border);
  if (!hit_test.maximized && resize_border > 0) {
    const bool left = point_x < resize_border;
    const bool right = point_x >= window_width - resize_border;
    const bool top = point_y < resize_border;
    const bool bottom = point_y >= window_height - resize_border;

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

  const int title_bar_height = std::max(0, hit_test.title_bar_height);
  const int left_interactive_width =
      std::clamp(hit_test.left_interactive_width, 0, window_width);
  const int right_controls_width = std::max(0, hit_test.right_controls_width);
  const int draggable_right = std::max(0, window_width - right_controls_width);
  const int draggable_left = std::min(left_interactive_width, draggable_right);
  if (point_y < title_bar_height && point_x >= draggable_left &&
      point_x < draggable_right) {
    return SDL_HITTEST_DRAGGABLE;
  }

  return SDL_HITTEST_NORMAL;
}

PlatformWindowChromeFrame platform_window_chrome_resize_frame(
    const PlatformWindowChromeResizeDrag &drag) {
  const int min_width = std::max(1, drag.min_width);
  const int min_height = std::max(1, drag.min_height);
  const int start_left = drag.start.x;
  const int start_top = drag.start.y;
  const int start_right = drag.start.x + std::max(1, drag.start.width);
  const int start_bottom = drag.start.y + std::max(1, drag.start.height);

  int left = start_left;
  int top = start_top;
  int right = start_right;
  int bottom = start_bottom;

  switch (drag.edge) {
  case SDL_HITTEST_RESIZE_TOPLEFT:
  case SDL_HITTEST_RESIZE_LEFT:
  case SDL_HITTEST_RESIZE_BOTTOMLEFT:
    left = std::min(start_left + drag.delta_x, start_right - min_width);
    break;
  default:
    break;
  }

  switch (drag.edge) {
  case SDL_HITTEST_RESIZE_TOPRIGHT:
  case SDL_HITTEST_RESIZE_RIGHT:
  case SDL_HITTEST_RESIZE_BOTTOMRIGHT:
    right = std::max(start_right + drag.delta_x, start_left + min_width);
    break;
  default:
    break;
  }

  switch (drag.edge) {
  case SDL_HITTEST_RESIZE_TOPLEFT:
  case SDL_HITTEST_RESIZE_TOP:
  case SDL_HITTEST_RESIZE_TOPRIGHT:
    top = std::min(start_top + drag.delta_y, start_bottom - min_height);
    break;
  default:
    break;
  }

  switch (drag.edge) {
  case SDL_HITTEST_RESIZE_BOTTOMLEFT:
  case SDL_HITTEST_RESIZE_BOTTOM:
  case SDL_HITTEST_RESIZE_BOTTOMRIGHT:
    bottom = std::max(start_bottom + drag.delta_y, start_top + min_height);
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

int platform_window_chrome_resize_border_for_system_metrics(int fallback_border,
                                                            int size_frame,
                                                            int padded_border) {
  return std::max({1, fallback_border,
                   std::max(0, size_frame) + std::max(0, padded_border)});
}

bool platform_window_can_render_frames(std::uint64_t window_flags,
                                       int pixel_width, int pixel_height) {
  constexpr std::uint64_t kNonDrawableWindowFlags =
      SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN | SDL_WINDOW_OCCLUDED;
  return (window_flags & kNonDrawableWindowFlags) == 0u && pixel_width > 0 &&
         pixel_height > 0;
}

PlatformWindowChromeFrame platform_window_recover_frame_to_display_work_area(
    PlatformWindowChromeFrame frame,
    std::span<const PlatformWindowChromeFrame> display_work_areas) {
  frame.width = std::max(kPlatformWindowMinimumWidth, frame.width);
  frame.height = std::max(kPlatformWindowMinimumHeight, frame.height);
  if (display_work_areas.empty()) {
    return frame;
  }

  const PlatformWindowChromeFrame *target_area = &display_work_areas.front();
  std::int64_t best_area = 0;
  for (const PlatformWindowChromeFrame &work_area : display_work_areas) {
    if (work_area.width <= 0 || work_area.height <= 0) {
      continue;
    }
    const std::int64_t area = intersection_area(frame, work_area);
    if (area > best_area) {
      best_area = area;
      target_area = &work_area;
    }
  }

  const int max_width =
      std::max(kPlatformWindowMinimumWidth, target_area->width);
  const int max_height =
      std::max(kPlatformWindowMinimumHeight, target_area->height);
  frame.width = std::clamp(frame.width, kPlatformWindowMinimumWidth, max_width);
  frame.height =
      std::clamp(frame.height, kPlatformWindowMinimumHeight, max_height);
  frame.x = clamp_window_origin(frame.x, frame.width, target_area->x,
                                std::max(1, target_area->width));
  frame.y = clamp_window_origin(frame.y, frame.height, target_area->y,
                                std::max(1, target_area->height));
  return frame;
}

PlatformWindow::~PlatformWindow() {
    destroy();
}

bool PlatformWindow::create(const char* title, int width, int height, PlatformWindowOptions options) {
  if (!acquire_sdl_video()) {
    return false;
  }

    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (options.borderless) {
        LOG_INFO("Creating native-frame custom chrome platform window");
    }

    window_ = SDL_CreateWindow(
            title,
            width,
            height,
            flags);

    if (!window_) {
        LOG_ERROR("SDL_CreateWindow failed: {}", SDL_GetError());
        release_sdl_video();
        return false;
    }
    if (!SDL_SetWindowMinimumSize(window_, kPlatformWindowMinimumWidth, kPlatformWindowMinimumHeight)) {
        LOG_WARN("SDL_SetWindowMinimumSize failed: {}", SDL_GetError());
    }

#if defined(_WIN32)
    install_native_chrome_cursor_hook();
#endif
    refresh_pixel_size();
    if (options.borderless) {
      install_borderless_chrome();
      apply_native_borderless_chrome_adornments();
      refresh_pixel_size();
    }
    return true;
}

void PlatformWindow::destroy() {
    if (window_) {
#if defined(_WIN32)
      clear_native_move_size_state();
#endif
      cancel_chrome_interaction();
      uninstall_native_chrome_cursor_hook();
      SDL_DestroyWindow(window_);
      window_ = nullptr;
      release_sdl_video();
    }
    for (SDL_Cursor *&cursor : chrome_cursors_) {
      if (cursor) {
        SDL_DestroyCursor(cursor);
        cursor = nullptr;
      }
    }
    borderless_chrome_ = false;
}

void *PlatformWindow::native_window_handle() const {
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

void PlatformWindow::refresh_pixel_size() {
  if (!window_) {
    return;
  }

#if defined(_WIN32)
  if (native_chrome_window_handle_ != nullptr) {
    RECT client_rect{};
    if (GetClientRect(static_cast<HWND>(native_chrome_window_handle_),
                      &client_rect)) {
      width_pixels_ = static_cast<std::uint32_t>(
          std::max<LONG>(1, client_rect.right - client_rect.left));
      height_pixels_ = static_cast<std::uint32_t>(
          std::max<LONG>(1, client_rect.bottom - client_rect.top));
      return;
    }
  }
#endif

    int w = 1;
    int h = 1;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    width_pixels_ = static_cast<std::uint32_t>(w > 0 ? w : 1);
    height_pixels_ = static_cast<std::uint32_t>(h > 0 ? h : 1);
}

void PlatformWindow::note_frame_presented(std::uint32_t framebuffer_width,
                                          std::uint32_t framebuffer_height) {
  live_resize_frame_size_valid_ = true;
  live_resize_frame_width_pixels_ = std::max(1u, framebuffer_width);
  live_resize_frame_height_pixels_ = std::max(1u, framebuffer_height);
}

void PlatformWindow::refresh_native_chrome() {
  if (!borderless_chrome_) {
    return;
  }
  apply_native_borderless_chrome_adornments();
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
    refresh_pixel_size();
}

void PlatformWindow::toggle_maximize_restore() {
  if (!window_) {
    return;
  }

#if defined(_WIN32)
  if (borderless_chrome_ && native_chrome_window_handle_ != nullptr) {
    HWND hwnd = static_cast<HWND>(native_chrome_window_handle_);
    if (IsZoomed(hwnd) != FALSE) {
      ShowWindow(hwnd, SW_RESTORE);
      if (!SDL_SyncWindow(window_)) {
        LOG_WARN("SDL_SyncWindow failed after native borderless restore: {}",
                 SDL_GetError());
      }
      refresh_pixel_size();
      request_live_resize_frame(true);
      return;
    }

    ShowWindow(hwnd, SW_MAXIMIZE);
    if (!SDL_SyncWindow(window_)) {
      LOG_WARN("SDL_SyncWindow failed after native borderless maximize: {}",
               SDL_GetError());
    }
    refresh_pixel_size();
    request_live_resize_frame(true);
    return;
  }
#endif

    if (maximized()) {
        if (!SDL_RestoreWindow(window_)) {
            LOG_WARN("SDL_RestoreWindow failed: {}", SDL_GetError());
        }
        request_live_resize_frame(true);
        return;
    }

    if (!SDL_MaximizeWindow(window_)) {
        LOG_WARN("SDL_MaximizeWindow failed: {}", SDL_GetError());
    }
    request_live_resize_frame(true);
}

std::optional<PlatformWindowPlacement> PlatformWindow::placement() const {
    if (!window_) {
        return std::nullopt;
    }

#if defined(_WIN32)
    if (native_chrome_window_handle_ != nullptr) {
      HWND hwnd = static_cast<HWND>(native_chrome_window_handle_);
      WINDOWPLACEMENT native_placement{};
      native_placement.length = sizeof(native_placement);
      if (GetWindowPlacement(hwnd, &native_placement)) {
        return PlatformWindowPlacement{
            .normal_frame = frame_from_rect(native_placement.rcNormalPosition),
            .maximized =
                native_placement.showCmd == SW_SHOWMAXIMIZED ||
                (native_placement.flags & WPF_RESTORETOMAXIMIZED) != 0u ||
                IsZoomed(hwnd) != FALSE,
        };
      }
      LOG_WARN("GetWindowPlacement failed while saving window placement: {}",
               GetLastError());
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
        .normal_frame =
            {
                .x = x,
                .y = y,
                .width = std::max(1, width),
                .height = std::max(1, height),
            },
        .maximized = maximized(),
    };
}

void PlatformWindow::apply_placement(const PlatformWindowPlacement &placement) {
  if (!window_) {
    return;
  }

  const std::vector<PlatformWindowChromeFrame> work_areas =
      display_work_areas();
  const PlatformWindowChromeFrame recovered_frame =
      platform_window_recover_frame_to_display_work_area(placement.normal_frame,
                                                         work_areas);

#if defined(_WIN32)
  if (native_chrome_window_handle_ != nullptr) {
    HWND hwnd = static_cast<HWND>(native_chrome_window_handle_);
    WINDOWPLACEMENT native_placement{};
    native_placement.length = sizeof(native_placement);
    if (!GetWindowPlacement(hwnd, &native_placement)) {
      native_placement.length = sizeof(native_placement);
    }
    native_placement.flags = 0;
    native_placement.showCmd = SW_SHOWNORMAL;
    native_placement.rcNormalPosition = rect_from_frame(recovered_frame);
    if (!SetWindowPlacement(hwnd, &native_placement)) {
      LOG_WARN("SetWindowPlacement failed while applying window placement: {}",
               GetLastError());
    }
    if (placement.maximized) {
      ShowWindow(hwnd, SW_MAXIMIZE);
    }
    if (!SDL_SyncWindow(window_)) {
      LOG_WARN(
          "SDL_SyncWindow failed after applying native window placement: {}",
          SDL_GetError());
    }
    refresh_pixel_size();
    request_live_resize_frame(true);
    return;
  }
#endif

  if (!SDL_SetWindowSize(window_, recovered_frame.width,
                         recovered_frame.height)) {
    LOG_WARN("SDL_SetWindowSize failed while applying window placement: {}",
             SDL_GetError());
  }
  if (!SDL_SetWindowPosition(window_, recovered_frame.x, recovered_frame.y)) {
    LOG_WARN("SDL_SetWindowPosition failed while applying window placement: {}",
             SDL_GetError());
  }
    if (placement.maximized && !SDL_MaximizeWindow(window_)) {
        LOG_WARN("SDL_MaximizeWindow failed while applying window placement: {}", SDL_GetError());
    }
    if (!SDL_SyncWindow(window_)) {
        LOG_WARN("SDL_SyncWindow failed after applying window placement: {}", SDL_GetError());
    }
    refresh_pixel_size();
    request_live_resize_frame(true);
}

bool PlatformWindow::process_chrome_event(const PlatformEvent &event) {
  const auto *sdl_event = static_cast<const SDL_Event *>(event.native_event);
  return sdl_event != nullptr && process_chrome_event(*sdl_event);
}

bool PlatformWindow::process_chrome_event(const SDL_Event &event) {
  if (!window_ || !borderless_chrome_) {
    return false;
  }

  const SDL_WindowID window_id = SDL_GetWindowID(window_);
  const std::optional<SDL_WindowID> event_window = sdl_event_window_id(event);
  if (!event_window || *event_window != window_id) {
    return false;
  }

  if (chrome_interaction_.active) {
    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION: {
      if ((event.motion.state & SDL_BUTTON_LMASK) == 0u) {
        cancel_chrome_interaction();
        return true;
      }
      float global_x = 0.0f;
      float global_y = 0.0f;
      SDL_GetGlobalMouseState(&global_x, &global_y);
      update_chrome_interaction(global_x, global_y);
      return true;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP:
      if (event.button.button == SDL_BUTTON_LEFT) {
        float global_x = 0.0f;
        float global_y = 0.0f;
        SDL_GetGlobalMouseState(&global_x, &global_y);
        update_chrome_interaction(global_x, global_y);
        end_chrome_interaction();
        return true;
      }
      return true;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
    case SDL_EVENT_WINDOW_MINIMIZED:
    case SDL_EVENT_WINDOW_HIDDEN:
    case SDL_EVENT_WINDOW_OCCLUDED:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
      cancel_chrome_interaction();
      return true;
    default:
      return true;
    }
  }

  if (event.type == SDL_EVENT_MOUSE_MOTION) {
    update_chrome_hover_cursor(event.motion.x, event.motion.y);
    return false;
  }

  if (event.type != SDL_EVENT_MOUSE_BUTTON_DOWN ||
      event.button.button != SDL_BUTTON_LEFT) {
    return false;
  }

  float global_x = 0.0f;
  float global_y = 0.0f;
  SDL_GetGlobalMouseState(&global_x, &global_y);
  int width = 1;
  int height = 1;
  SDL_GetWindowSize(window_, &width, &height);
  PlatformWindowChromeHitTest hit_test = chrome_hit_test_;
  hit_test.window_width = std::max(1, width);
  hit_test.window_height = std::max(1, height);
  hit_test.point_x = static_cast<int>(std::lround(event.button.x));
  hit_test.point_y = static_cast<int>(std::lround(event.button.y));
  hit_test.maximized = maximized();

  const SDL_HitTestResult hit =
      platform_window_chrome_hit_test_result(hit_test);
  if (hit == SDL_HITTEST_NORMAL) {
    return false;
  }
#if defined(_WIN32)
  if (native_chrome_window_handle_ != nullptr &&
      is_native_chrome_system_hit(hit)) {
    return true;
  }
#endif
    if (event.button.clicks > 1) {
        if (hit == SDL_HITTEST_DRAGGABLE) {
          toggle_maximize_restore();
          return true;
        }
        return false;
    }

    begin_chrome_interaction(hit, global_x, global_y, event.button.x,
                             event.button.y);
    return true;
}

bool PlatformWindow::maximized() const {
#if defined(_WIN32)
  if (native_chrome_window_handle_ != nullptr) {
    return IsZoomed(static_cast<HWND>(native_chrome_window_handle_)) != FALSE;
  }
#endif
    return window_ && (SDL_GetWindowFlags(window_) & SDL_WINDOW_MAXIMIZED) != 0u;
}

bool PlatformWindow::can_render_frames() const {
  if (!window_) {
    return false;
  }

#if defined(_WIN32)
  if (native_chrome_window_handle_ != nullptr) {
    RECT client_rect{};
    if (GetClientRect(static_cast<HWND>(native_chrome_window_handle_),
                      &client_rect)) {
      const int pixel_width = static_cast<int>(
          std::max<LONG>(0, client_rect.right - client_rect.left));
      const int pixel_height = static_cast<int>(
          std::max<LONG>(0, client_rect.bottom - client_rect.top));
      return platform_window_can_render_frames(SDL_GetWindowFlags(window_),
                                               pixel_width, pixel_height);
    }
  }
#endif

  int pixel_width = 0;
  int pixel_height = 0;
  SDL_GetWindowSizeInPixels(window_, &pixel_width, &pixel_height);
  return platform_window_can_render_frames(SDL_GetWindowFlags(window_),
                                           pixel_width, pixel_height);
}

bool PlatformWindow::chrome_resize_active() const {
#if defined(_WIN32)
  if (native_resize_active_) {
    return true;
  }
#endif
  return chrome_interaction_.active &&
         chrome_interaction_.hit != SDL_HITTEST_DRAGGABLE;
}

bool PlatformWindow::native_chrome_message_active() const {
#if defined(_WIN32)
  return native_chrome_message_depth_ > 0;
#else
    return false;
#endif
}

bool PlatformWindow::native_live_resize_frame_active() const {
#if defined(_WIN32)
  return native_live_resize_frame_active_;
#else
    return false;
#endif
}

bool PlatformWindow::native_chrome_interaction_active() const {
#if defined(_WIN32)
  return native_move_size_active_ || native_chrome_message_depth_ > 0;
#else
    return false;
#endif
}

void PlatformWindow::set_live_resize_frame_callback(
    LiveResizeFrameCallback callback) {
  live_resize_frame_callback_ = std::move(callback);
  if (!live_resize_frame_callback_) {
    live_resize_frame_callback_pending_ = false;
    live_resize_frame_pending_width_pixels_ = 0;
    live_resize_frame_pending_height_pixels_ = 0;
#if defined(_WIN32)
    live_resize_frame_callback_posted_ = false;
    live_resize_frame_paint_scheduled_ = false;
#endif
  }
}

void PlatformWindow::install_borderless_chrome() {
  if (!window_) {
    return;
  }

  borderless_chrome_ = true;
  install_native_chrome_cursor_hook();
  apply_native_custom_chrome_styles();
}

void PlatformWindow::install_native_chrome_cursor_hook() {
#if defined(_WIN32)
  if (!window_ || native_chrome_window_handle_) {
    return;
  }

  HWND hwnd = static_cast<HWND>(native_window_handle());
  if (!hwnd) {
    return;
  }

    if (!SetPropW(hwnd, kBorderlessChromeWindowProp, reinterpret_cast<HANDLE>(this))) {
        LOG_WARN("SetPropW failed for borderless chrome cursor hook: {}", GetLastError());
        return;
    }

    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previous_window_proc = SetWindowLongPtrW(
        hwnd, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(&PlatformWindow::native_chrome_window_proc));
    const DWORD error = GetLastError();
    if (previous_window_proc == 0 && error != ERROR_SUCCESS) {
      LOG_WARN("SetWindowLongPtrW failed for borderless chrome cursor hook: {}",
               error);
      RemovePropW(hwnd, kBorderlessChromeWindowProp);
      return;
    }

    native_chrome_window_handle_ = hwnd;
    previous_native_chrome_window_proc_ =
        static_cast<std::intptr_t>(previous_window_proc);
#endif
}

void PlatformWindow::apply_native_custom_chrome_styles() {
#if defined(_WIN32)
  HWND hwnd = static_cast<HWND>(native_window_handle());
  if (!hwnd) {
    return;
  }

    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    const LONG_PTR custom_chrome_style = style | WS_CAPTION | WS_THICKFRAME |
                                         WS_SYSMENU | WS_MINIMIZEBOX |
                                         WS_MAXIMIZEBOX;
    if (custom_chrome_style != style) {
      SetLastError(ERROR_SUCCESS);
      if (SetWindowLongPtrW(hwnd, GWL_STYLE, custom_chrome_style) == 0 &&
          GetLastError() != ERROR_SUCCESS) {
        LOG_WARN("SetWindowLongPtrW failed while applying custom chrome window "
                 "styles: {}",
                 GetLastError());
      }
    }
    if (!SetWindowPos(
                hwnd,
                nullptr,
                0,
                0,
                0,
                0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED)) {
        LOG_WARN("SetWindowPos frame refresh failed while applying custom chrome: {}", GetLastError());
    }
#endif
}

void PlatformWindow::uninstall_native_chrome_cursor_hook() {
#if defined(_WIN32)
  if (!native_chrome_window_handle_) {
    return;
  }

  HWND hwnd = static_cast<HWND>(native_chrome_window_handle_);
  const LONG_PTR current_window_proc = GetWindowLongPtrW(hwnd, GWLP_WNDPROC);
  if (previous_native_chrome_window_proc_ != 0 &&
      current_window_proc == reinterpret_cast<LONG_PTR>(
                                 &PlatformWindow::native_chrome_window_proc)) {
    SetLastError(ERROR_SUCCESS);
    if (SetWindowLongPtrW(
            hwnd, GWLP_WNDPROC,
            static_cast<LONG_PTR>(previous_native_chrome_window_proc_)) == 0 &&
        GetLastError() != ERROR_SUCCESS) {
      LOG_WARN("SetWindowLongPtrW failed while removing borderless chrome "
               "cursor hook: {}",
               GetLastError());
    }
  }

    RemovePropW(hwnd, kBorderlessChromeWindowProp);
    native_chrome_window_handle_ = nullptr;
    previous_native_chrome_window_proc_ = 0;
#endif
}

void PlatformWindow::apply_native_borderless_chrome_adornments() {
#if defined(_WIN32)
  HWND hwnd = static_cast<HWND>(native_window_handle());
  if (!hwnd) {
    return;
  }

  const DWM_WINDOW_CORNER_PREFERENCE corner_preference = DWMWCP_ROUND;
  if (FAILED(DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE,
                                   &corner_preference,
                                   sizeof(corner_preference)))) {
    LOG_WARN("DwmSetWindowAttribute corner preference failed");
  }

  COLORREF border_color = DWMWA_COLOR_DEFAULT;
  const bool active =
      (SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS) != 0u;
  if (active && accent_color_on_title_bars_enabled()) {
    border_color = system_accent_border_color().value_or(DWMWA_COLOR_DEFAULT);
  }
  if (FAILED(DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &border_color,
                                   sizeof(border_color)))) {
    LOG_WARN("DwmSetWindowAttribute border color failed");
  }
#endif
}

#if defined(_WIN32)
std::intptr_t __stdcall PlatformWindow::native_chrome_window_proc(
    void *window_handle, unsigned int message, std::uintptr_t w_param,
    std::intptr_t l_param) {
  HWND hwnd = static_cast<HWND>(window_handle);
  PlatformWindow *platform_window = static_cast<PlatformWindow *>(
      GetPropW(hwnd, kBorderlessChromeWindowProp));
  if (!platform_window) {
    return DefWindowProcW(hwnd, message, static_cast<WPARAM>(w_param),
                          static_cast<LPARAM>(l_param));
  }

  return platform_window->handle_native_chrome_window_message(
      window_handle, message, w_param, l_param);
}

std::intptr_t PlatformWindow::handle_native_chrome_window_message(
    void *window_handle, unsigned int message, std::uintptr_t w_param,
    std::intptr_t l_param) {
  HWND hwnd = static_cast<HWND>(window_handle);
  if (message == WM_ERASEBKGND) {
    return 1;
  }
  if (message == WM_PAINT) {
    live_resize_frame_paint_scheduled_ = false;
    PAINTSTRUCT paint{};
    BeginPaint(hwnd, &paint);
    EndPaint(hwnd, &paint);
    if (live_resize_frame_callback_pending_) {
      dispatch_live_resize_frame_callback();
    }
    return 0;
  }
  if (message == kLiveResizeFrameMessage) {
    live_resize_frame_callback_posted_ = false;
    dispatch_live_resize_frame_callback();
    return 0;
  }

  /**
   * Tracks nested native window messages while deferring live resize redraws.
   */
  struct NativeChromeMessageScope {
    explicit NativeChromeMessageScope(PlatformWindow &owner) : owner(owner) {
      ++owner.native_chrome_message_depth_;
    }

    ~NativeChromeMessageScope() {
      owner.native_chrome_message_depth_ =
          std::max(0, owner.native_chrome_message_depth_ - 1);
      if (owner.native_chrome_message_depth_ == 0 &&
          owner.live_resize_frame_callback_pending_) {
        owner.schedule_live_resize_frame_callback();
      }
    }

    PlatformWindow &owner;
  } message_scope{*this};

  auto call_previous_window_proc = [&]() -> std::intptr_t {
    if (previous_native_chrome_window_proc_ != 0) {
      return CallWindowProcW(
          reinterpret_cast<WNDPROC>(previous_native_chrome_window_proc_), hwnd,
          message, static_cast<WPARAM>(w_param), static_cast<LPARAM>(l_param));
    }

    return DefWindowProcW(hwnd, message, static_cast<WPARAM>(w_param),
                          static_cast<LPARAM>(l_param));
  };

  if (borderless_chrome_ && message == WM_GETMINMAXINFO && l_param != 0) {
    const std::intptr_t result = call_previous_window_proc();
    apply_borderless_chrome_maximized_work_area(
        hwnd, *reinterpret_cast<MINMAXINFO *>(l_param));
    return result;
  }

  if (borderless_chrome_ && message == WM_NCCALCSIZE && w_param != 0 &&
      l_param != 0) {
    (void)apply_borderless_chrome_maximized_client_area(
        hwnd, *reinterpret_cast<NCCALCSIZE_PARAMS *>(l_param));
    return 0;
  }

  if (borderless_chrome_ && !chrome_interaction_.active &&
      message == WM_NCHITTEST) {
    const SDL_HitTestResult hit = native_chrome_hit_test_from_screen_point(
        GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param));
    if (is_native_chrome_system_hit(hit)) {
      return native_chrome_hit_test_code_for_hit(hit);
    }
  }

  if (message == WM_ENTERSIZEMOVE) {
    native_move_size_active_ = true;
    native_resize_active_ = false;
    prime_live_resize_frame_coalescing();
  } else if (message == WM_SIZING) {
    native_move_size_active_ = true;
    native_resize_active_ = true;
  } else if (message == WM_SIZE && native_move_size_active_ &&
             w_param != SIZE_MINIMIZED) {
    native_resize_active_ = true;
  }

  if (borderless_chrome_ && !chrome_interaction_.active &&
      (message == WM_SETCURSOR || message == WM_MOUSEMOVE ||
       message == WM_NCMOUSEMOVE)) {
    POINT cursor_point{};
    if (GetCursorPos(&cursor_point)) {
      const SDL_HitTestResult hit = native_chrome_hit_test_from_screen_point(
          cursor_point.x, cursor_point.y);
      if (HCURSOR cursor = native_chrome_cursor_for_hit(hit)) {
        SetCursor(cursor);
        chrome_resize_cursor_active_ = true;
        if (message == WM_SETCURSOR) {
          return TRUE;
        }
      } else {
        release_chrome_resize_cursor();
        if (message == WM_SETCURSOR && hit == SDL_HITTEST_DRAGGABLE) {
          SetCursor(LoadCursor(nullptr, IDC_ARROW));
          return TRUE;
        }
      }
    }
  }

  if (borderless_chrome_ && chrome_resize_cursor_active_) {
    switch (message) {
    case WM_MOUSEMOVE:
    case WM_NCMOUSEMOVE: {
      POINT cursor_point{};
      if (GetCursorPos(&cursor_point) &&
          !is_native_chrome_resize_hit(native_chrome_hit_test_from_screen_point(
              cursor_point.x, cursor_point.y))) {
        release_chrome_resize_cursor();
      }
      break;
    }
    case WM_MOUSELEAVE:
    case WM_NCMOUSELEAVE:
    case WM_KILLFOCUS:
    case WM_CANCELMODE:
    case WM_CAPTURECHANGED:
      release_chrome_resize_cursor();
      break;
    default:
      break;
    }
  }

  const bool redraw_native_resize_frame =
      (message != WM_SIZE || w_param != SIZE_MINIMIZED) &&
      (message == WM_SIZE || message == WM_DPICHANGED);
  const bool clear_native_resize =
      native_move_size_active_ &&
      (message == WM_EXITSIZEMOVE || message == WM_CANCELMODE ||
       message == WM_CAPTURECHANGED || message == WM_KILLFOCUS ||
       message == WM_NCDESTROY);

  const std::intptr_t result = call_previous_window_proc();

  if (redraw_native_resize_frame) {
    request_live_resize_frame();
  }
  if (clear_native_resize) {
    clear_native_move_size_state();
    request_live_resize_frame(true);
  }

  return result;
}

int PlatformWindow::native_chrome_resize_border_for_window(
    void *window_handle) const {
  HWND hwnd = static_cast<HWND>(window_handle);
  if (hwnd == nullptr) {
    return std::max(1, chrome_hit_test_.resize_border);
  }

  const UINT dpi = GetDpiForWindow(hwnd);
  return platform_window_chrome_resize_border_for_system_metrics(
      chrome_hit_test_.resize_border,
      GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi),
      GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi));
}

void PlatformWindow::clear_native_move_size_state() {
  native_move_size_active_ = false;
  native_resize_active_ = false;
  reset_live_resize_frame_coalescing();
  release_chrome_resize_cursor();
}

SDL_HitTestResult
PlatformWindow::native_chrome_hit_test_from_screen_point(int screen_x,
                                                         int screen_y) const {
  if (!window_ || !native_chrome_window_handle_ || !borderless_chrome_) {
    return SDL_HITTEST_NORMAL;
  }

  HWND hwnd = static_cast<HWND>(native_chrome_window_handle_);
  RECT client_rect{};
  if (!GetClientRect(hwnd, &client_rect)) {
    return SDL_HITTEST_NORMAL;
  }

  const int width =
      std::max(1, static_cast<int>(client_rect.right - client_rect.left));
  const int height =
      std::max(1, static_cast<int>(client_rect.bottom - client_rect.top));
  POINT client_point{static_cast<LONG>(screen_x), static_cast<LONG>(screen_y)};
  if (!ScreenToClient(hwnd, &client_point)) {
    return SDL_HITTEST_NORMAL;
  }

  const int resize_border = native_chrome_resize_border_for_window(hwnd);
  const int outside_resize_band = std::max(1, resize_border);
  if (client_point.x < -outside_resize_band ||
      client_point.y < -outside_resize_band ||
      client_point.x >= width + outside_resize_band ||
      client_point.y >= height + outside_resize_band) {
    return SDL_HITTEST_NORMAL;
  }

  PlatformWindowChromeHitTest hit_test = chrome_hit_test_;
  hit_test.window_width = width;
  hit_test.window_height = height;
  hit_test.resize_border = resize_border;
  hit_test.point_x =
      std::clamp(static_cast<int>(client_point.x), 0, std::max(0, width - 1));
  hit_test.point_y =
      std::clamp(static_cast<int>(client_point.y), 0, std::max(0, height - 1));
  hit_test.maximized = IsZoomed(hwnd) != FALSE;
  return platform_window_chrome_hit_test_result(hit_test);
}
#endif

void PlatformWindow::begin_chrome_interaction(SDL_HitTestResult hit,
                                              float global_mouse_x,
                                              float global_mouse_y,
                                              float window_mouse_x,
                                              float window_mouse_y) {
  if (!window_ || hit == SDL_HITTEST_NORMAL) {
    return;
  }

  int window_width = 1;
  int window_height = 1;
  SDL_GetWindowSize(window_, &window_width, &window_height);
  window_width = std::max(1, window_width);
  window_height = std::max(1, window_height);

  const bool pending_maximized_restore =
      hit == SDL_HITTEST_DRAGGABLE && maximized();

  int window_x = 0;
  int window_y = 0;
  SDL_GetWindowPosition(window_, &window_x, &window_y);
  SDL_GetWindowSize(window_, &window_width, &window_height);

  chrome_interaction_ = {
      .active = true,
      .pending_maximized_restore = pending_maximized_restore,
      .hit = hit,
      .start_frame =
          {
              .x = window_x,
              .y = window_y,
              .width = std::max(1, window_width),
              .height = std::max(1, window_height),
          },
      .start_global_mouse_x = global_mouse_x,
      .start_global_mouse_y = global_mouse_y,
      .restore_window_mouse_x = window_mouse_x,
      .restore_window_mouse_y = window_mouse_y,
      .move_offset_x = static_cast<int>(std::lround(global_mouse_x)) - window_x,
      .move_offset_y = static_cast<int>(std::lround(global_mouse_y)) - window_y,
  };

  if (!SDL_CaptureMouse(true)) {
    LOG_WARN("SDL_CaptureMouse failed for borderless window chrome: {}",
             SDL_GetError());
  } else {
    chrome_mouse_captured_ = true;
  }
  set_chrome_cursor(hit);
}

void PlatformWindow::update_chrome_interaction(float global_mouse_x,
                                               float global_mouse_y) {
  if (!window_ || !chrome_interaction_.active) {
    return;
  }

  if (chrome_interaction_.hit == SDL_HITTEST_DRAGGABLE) {
    if (chrome_interaction_.pending_maximized_restore) {
      const float delta_x =
          global_mouse_x - chrome_interaction_.start_global_mouse_x;
      const float delta_y =
          global_mouse_y - chrome_interaction_.start_global_mouse_y;
      if (std::abs(delta_x) < kChromeDragRestoreThreshold &&
          std::abs(delta_y) < kChromeDragRestoreThreshold) {
        return;
      }

      const float x_ratio =
          std::clamp(chrome_interaction_.restore_window_mouse_x /
                         static_cast<float>(std::max(
                             1, chrome_interaction_.start_frame.width)),
                     0.0f, 1.0f);
      const int restore_mouse_y =
          std::max(0, std::min(static_cast<int>(std::lround(
                                   chrome_interaction_.restore_window_mouse_y)),
                               chrome_hit_test_.title_bar_height));
      if (!SDL_RestoreWindow(window_)) {
        LOG_WARN("SDL_RestoreWindow failed before toolbar drag: {}",
                 SDL_GetError());
      }
      if (!SDL_SyncWindow(window_)) {
        LOG_WARN("SDL_SyncWindow failed after toolbar drag restore: {}",
                 SDL_GetError());
      }

      int restored_width = 1;
      int restored_height = 1;
      SDL_GetWindowSize(window_, &restored_width, &restored_height);
      restored_width = std::max(1, restored_width);
      restored_height = std::max(1, restored_height);
      const int restore_mouse_x =
          std::clamp(static_cast<int>(std::lround(
                         x_ratio * static_cast<float>(restored_width))),
                     0, std::max(0, restored_width - 1));
      const int restored_x =
          static_cast<int>(std::lround(global_mouse_x)) - restore_mouse_x;
      const int restored_y =
          static_cast<int>(std::lround(global_mouse_y)) - restore_mouse_y;
      if (!SDL_SetWindowPosition(window_, restored_x, restored_y)) {
        LOG_WARN("SDL_SetWindowPosition failed after toolbar drag restore: {}",
                 SDL_GetError());
      }
      if (!SDL_SyncWindow(window_)) {
        LOG_WARN("SDL_SyncWindow failed after toolbar drag positioning: {}",
                 SDL_GetError());
      }

      chrome_interaction_.pending_maximized_restore = false;
      chrome_interaction_.move_offset_x = restore_mouse_x;
      chrome_interaction_.move_offset_y = restore_mouse_y;
      chrome_interaction_.start_frame = {
          .x = restored_x,
          .y = restored_y,
          .width = restored_width,
          .height = restored_height,
      };
    }

    const int x = static_cast<int>(std::lround(global_mouse_x)) -
                  chrome_interaction_.move_offset_x;
    const int y = static_cast<int>(std::lround(global_mouse_y)) -
                  chrome_interaction_.move_offset_y;
    if (!SDL_SetWindowPosition(window_, x, y)) {
      LOG_WARN("SDL_SetWindowPosition failed during toolbar drag: {}",
               SDL_GetError());
    }
    return;
  }

  const PlatformWindowChromeFrame frame = platform_window_chrome_resize_frame({
      .start = chrome_interaction_.start_frame,
      .delta_x = static_cast<int>(std::lround(
          global_mouse_x - chrome_interaction_.start_global_mouse_x)),
      .delta_y = static_cast<int>(std::lround(
          global_mouse_y - chrome_interaction_.start_global_mouse_y)),
      .edge = chrome_interaction_.hit,
  });

#if defined(_WIN32)
  if (native_chrome_window_handle_ != nullptr) {
    HWND hwnd = static_cast<HWND>(native_chrome_window_handle_);
    if (SetWindowPos(hwnd, nullptr, frame.x, frame.y, frame.width, frame.height,
                     SWP_NOZORDER | SWP_NOACTIVATE)) {
      return;
    }
    LOG_WARN("SetWindowPos failed during borderless resize: {}",
             GetLastError());
  }
#endif

    if (!SDL_SetWindowPosition(window_, frame.x, frame.y)) {
        LOG_WARN("SDL_SetWindowPosition failed during borderless resize: {}", SDL_GetError());
    }
    if (!SDL_SetWindowSize(window_, frame.width, frame.height)) {
        LOG_WARN("SDL_SetWindowSize failed during borderless resize: {}", SDL_GetError());
    }
}

void PlatformWindow::end_chrome_interaction() {
  if (!chrome_interaction_.active) {
    return;
  }

  chrome_interaction_ = {};
  reset_live_resize_frame_coalescing();
  if (chrome_mouse_captured_) {
    if (!SDL_CaptureMouse(false)) {
      LOG_WARN(
          "SDL_CaptureMouse release failed for borderless window chrome: {}",
          SDL_GetError());
    }
    chrome_mouse_captured_ = false;
  }
  set_chrome_cursor(SDL_HITTEST_NORMAL);
  refresh_pixel_size();
  refresh_native_chrome();
}

void PlatformWindow::cancel_chrome_interaction() {
  if (!chrome_interaction_.active && !chrome_mouse_captured_) {
    return;
  }

  chrome_interaction_ = {};
  reset_live_resize_frame_coalescing();
  if (chrome_mouse_captured_) {
    if (!SDL_CaptureMouse(false)) {
      LOG_WARN(
          "SDL_CaptureMouse release failed for borderless window chrome: {}",
          SDL_GetError());
    }
    chrome_mouse_captured_ = false;
  }
  set_chrome_cursor(SDL_HITTEST_NORMAL);
  refresh_pixel_size();
  refresh_native_chrome();
}

void PlatformWindow::request_live_resize_frame(bool force) {
  refresh_pixel_size();
  if (!force && live_resize_frame_size_valid_ &&
      live_resize_frame_width_pixels_ == width_pixels_ &&
      live_resize_frame_height_pixels_ == height_pixels_) {
    return;
  }
  if (!live_resize_frame_callback_) {
    live_resize_frame_callback_pending_ = false;
    live_resize_frame_pending_width_pixels_ = 0;
    live_resize_frame_pending_height_pixels_ = 0;
    return;
  }
  if (!force && live_resize_frame_callback_pending_ &&
      live_resize_frame_pending_width_pixels_ == width_pixels_ &&
      live_resize_frame_pending_height_pixels_ == height_pixels_) {
    return;
  }
  live_resize_frame_callback_pending_ = true;
  live_resize_frame_pending_width_pixels_ = width_pixels_;
  live_resize_frame_pending_height_pixels_ = height_pixels_;
  schedule_live_resize_frame_callback();
}

void PlatformWindow::schedule_live_resize_frame_callback() {
  if (!live_resize_frame_callback_ || !live_resize_frame_callback_pending_) {
    return;
  }
#if defined(_WIN32)
  if (native_chrome_window_handle_ != nullptr) {
    const HWND hwnd = static_cast<HWND>(native_chrome_window_handle_);
    if (native_chrome_message_depth_ > 0) {
      if (live_resize_frame_paint_scheduled_) {
        return;
      }
      if (RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE)) {
        live_resize_frame_paint_scheduled_ = true;
        return;
      }
      LOG_WARN(
          "RedrawWindow failed while scheduling live resize paint frame: {}",
          GetLastError());
      return;
    }
    if (live_resize_frame_callback_posted_) {
      return;
    }
    if (PostMessageW(hwnd, kLiveResizeFrameMessage, 0, 0)) {
      live_resize_frame_callback_posted_ = true;
      return;
    }
    LOG_WARN("PostMessageW failed while scheduling live resize frame: {}",
             GetLastError());
    if (native_chrome_message_depth_ > 0) {
      return;
    }
  }
#endif
  dispatch_live_resize_frame_callback();
}

void PlatformWindow::dispatch_live_resize_frame_callback() {
  if (!live_resize_frame_callback_ || !live_resize_frame_callback_pending_) {
    return;
  }
  const std::uint32_t pending_width = live_resize_frame_pending_width_pixels_;
  const std::uint32_t pending_height = live_resize_frame_pending_height_pixels_;
  live_resize_frame_callback_pending_ = false;
  live_resize_frame_pending_width_pixels_ = 0;
  live_resize_frame_pending_height_pixels_ = 0;
  /**
   * Marks the active live-resize frame callback dispatch.
   */
  struct LiveResizeFrameDispatchScope {
    explicit LiveResizeFrameDispatchScope(PlatformWindow &owner)
        : owner(owner) {
#if defined(_WIN32)
      owner.native_live_resize_frame_active_ = true;
#endif
        }

        ~LiveResizeFrameDispatchScope() {
#if defined(_WIN32)
          owner.native_live_resize_frame_active_ = false;
#endif
        }

        PlatformWindow& owner;
  } dispatch_scope{*this};
  if (live_resize_frame_callback_()) {
    note_frame_presented(pending_width, pending_height);
  } else {
    live_resize_frame_callback_pending_ = true;
    live_resize_frame_pending_width_pixels_ = pending_width;
    live_resize_frame_pending_height_pixels_ = pending_height;
  }
}

void PlatformWindow::prime_live_resize_frame_coalescing() {
  refresh_pixel_size();
  live_resize_frame_callback_pending_ = false;
  live_resize_frame_pending_width_pixels_ = 0;
  live_resize_frame_pending_height_pixels_ = 0;
#if defined(_WIN32)
  live_resize_frame_paint_scheduled_ = false;
#endif
}

void PlatformWindow::reset_live_resize_frame_coalescing() {
  live_resize_frame_size_valid_ = false;
  live_resize_frame_width_pixels_ = 0;
  live_resize_frame_height_pixels_ = 0;
  live_resize_frame_callback_pending_ = false;
  live_resize_frame_pending_width_pixels_ = 0;
  live_resize_frame_pending_height_pixels_ = 0;
#if defined(_WIN32)
  live_resize_frame_callback_posted_ = false;
  live_resize_frame_paint_scheduled_ = false;
#endif
}

void PlatformWindow::update_chrome_hover_cursor(float window_mouse_x,
                                                float window_mouse_y) {
  if (!window_) {
    return;
  }

  int width = 1;
  int height = 1;
  SDL_GetWindowSize(window_, &width, &height);
  PlatformWindowChromeHitTest hit_test = chrome_hit_test_;
  hit_test.window_width = std::max(1, width);
  hit_test.window_height = std::max(1, height);
  hit_test.point_x = static_cast<int>(std::lround(window_mouse_x));
  hit_test.point_y = static_cast<int>(std::lround(window_mouse_y));
  hit_test.maximized = maximized();
  const SDL_HitTestResult hit =
      platform_window_chrome_hit_test_result(hit_test);
  if (hit != SDL_HITTEST_NORMAL && hit != SDL_HITTEST_DRAGGABLE) {
    set_chrome_cursor(hit);
    return;
  }

  release_chrome_resize_cursor();
}

void PlatformWindow::release_chrome_resize_cursor() {
  if (!chrome_resize_cursor_active_) {
    return;
  }

  if (SDL_Cursor *cursor = chrome_cursor(SDL_SYSTEM_CURSOR_DEFAULT)) {
    SDL_SetCursor(cursor);
  }
#if defined(_WIN32)
    SetCursor(LoadCursor(nullptr, IDC_ARROW));
#endif
    chrome_resize_cursor_active_ = false;
}

void PlatformWindow::set_chrome_cursor(SDL_HitTestResult hit) {
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

  if (SDL_Cursor *sdl_cursor = chrome_cursor(cursor)) {
    SDL_SetCursor(sdl_cursor);
  }
  chrome_resize_cursor_active_ = cursor != SDL_SYSTEM_CURSOR_DEFAULT;
}

SDL_Cursor *PlatformWindow::chrome_cursor(SDL_SystemCursor cursor) {
  const std::size_t index = static_cast<std::size_t>(cursor);
  if (index >= chrome_cursors_.size()) {
    return SDL_GetDefaultCursor();
  }
  if (!chrome_cursors_[index]) {
    chrome_cursors_[index] = SDL_CreateSystemCursor(cursor);
  }
  return chrome_cursors_[index] ? chrome_cursors_[index]
                                : SDL_GetDefaultCursor();
}

} // namespace quader

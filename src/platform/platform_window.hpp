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

struct PlatformEvent;

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
  int window_width = 1;
  int window_height = 1;
  int point_x = 0;
  int point_y = 0;
  int resize_border = 6;
  int title_bar_height = 35;
  int left_interactive_width = 440;
  int right_controls_width = 105;
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
    int delta_x = 0;
    int delta_y = 0;
    int min_width = kPlatformWindowMinimumWidth;
    int min_height = kPlatformWindowMinimumHeight;
    SDL_HitTestResult edge = SDL_HITTEST_NORMAL;
};

/**
 * Stores persisted normal window placement plus native maximized state.
 */
struct PlatformWindowPlacement {
  PlatformWindowChromeFrame normal_frame{0, 0, 1600, 960};
  bool maximized = false;
};

[[nodiscard]] SDL_HitTestResult platform_window_chrome_hit_test_result(
    const PlatformWindowChromeHitTest &hit_test);
[[nodiscard]] PlatformWindowChromeFrame
platform_window_chrome_resize_frame(const PlatformWindowChromeResizeDrag &drag);
[[nodiscard]] int platform_window_chrome_resize_border_for_system_metrics(
    int fallback_border, int size_frame, int padded_border);
[[nodiscard]] bool platform_window_can_render_frames(std::uint64_t window_flags,
                                                     int pixel_width,
                                                     int pixel_height);
[[nodiscard]] PlatformWindowChromeFrame
platform_window_recover_frame_to_display_work_area(
    PlatformWindowChromeFrame frame,
    std::span<const PlatformWindowChromeFrame> display_work_areas);

/**
 * Owns Platform Window behavior within the platform integration layer.
 */
class PlatformWindow {
public:
    using LiveResizeFrameCallback = std::function<bool()>;

    PlatformWindow() = default;
    ~PlatformWindow();

    PlatformWindow(const PlatformWindow&) = delete;
    PlatformWindow& operator=(const PlatformWindow&) = delete;

    bool create(const char* title, int width, int height, PlatformWindowOptions options = {});
    void destroy();

    SDL_Window *sdl_window() const { return window_; }
    void *native_window_handle() const;

    std::uint32_t width_pixels() const { return width_pixels_; }
    std::uint32_t height_pixels() const { return height_pixels_; }
    void refresh_pixel_size();
    void note_frame_presented(std::uint32_t framebuffer_width,
                              std::uint32_t framebuffer_height);
    void refresh_native_chrome();
    void minimize();
    void toggle_maximize_restore();
    [[nodiscard]] bool can_render_frames() const;
    [[nodiscard]] std::optional<PlatformWindowPlacement> placement() const;
    void apply_placement(const PlatformWindowPlacement &placement);
    [[nodiscard]] bool process_chrome_event(const PlatformEvent &event);
    [[nodiscard]] bool process_chrome_event(const SDL_Event &event);
    [[nodiscard]] bool chrome_resize_active() const;
    [[nodiscard]] bool native_chrome_message_active() const;
    [[nodiscard]] bool native_live_resize_frame_active() const;
    [[nodiscard]] bool native_chrome_interaction_active() const;
    [[nodiscard]] bool maximized() const;
    void set_live_resize_frame_callback(LiveResizeFrameCallback callback);

  private:
    void install_borderless_chrome();
    void apply_native_borderless_chrome_adornments();
    void install_native_chrome_cursor_hook();
    void uninstall_native_chrome_cursor_hook();
    void begin_chrome_interaction(SDL_HitTestResult hit, float global_mouse_x,
                                  float global_mouse_y, float window_mouse_x,
                                  float window_mouse_y);
    void update_chrome_interaction(float global_mouse_x, float global_mouse_y);
    void end_chrome_interaction();
    void cancel_chrome_interaction();
    void request_live_resize_frame(bool force = false);
    void schedule_live_resize_frame_callback();
    void dispatch_live_resize_frame_callback();
    void prime_live_resize_frame_coalescing();
    void reset_live_resize_frame_coalescing();
    void update_chrome_hover_cursor(float window_mouse_x, float window_mouse_y);
    void release_chrome_resize_cursor();
    void set_chrome_cursor(SDL_HitTestResult hit);
    SDL_Cursor *chrome_cursor(SDL_SystemCursor cursor);
#if defined(_WIN32)
    void apply_native_custom_chrome_styles();
    static std::intptr_t __stdcall
    native_chrome_window_proc(void *window_handle, unsigned int message,
                              std::uintptr_t w_param, std::intptr_t l_param);
    [[nodiscard]] std::intptr_t handle_native_chrome_window_message(
        void *window_handle, unsigned int message, std::uintptr_t w_param,
        std::intptr_t l_param);
    [[nodiscard]] SDL_HitTestResult
    native_chrome_hit_test_from_screen_point(int screen_x, int screen_y) const;
    [[nodiscard]] int
    native_chrome_resize_border_for_window(void *window_handle) const;
    void clear_native_move_size_state();
#endif

    /// Tracks the active native chrome drag or resize gesture.
    struct ChromeInteraction {
        bool active = false;
        bool pending_maximized_restore = false;
        SDL_HitTestResult hit = SDL_HITTEST_NORMAL;
        PlatformWindowChromeFrame start_frame;
        float start_global_mouse_x = 0.0f;
        float start_global_mouse_y = 0.0f;
        float restore_window_mouse_x = 0.0f;
        float restore_window_mouse_y = 0.0f;
        int move_offset_x = 0;
        int move_offset_y = 0;
    };

    SDL_Window* window_ = nullptr;
    std::uint32_t width_pixels_ = 1;
    std::uint32_t height_pixels_ = 1;
    PlatformWindowChromeHitTest chrome_hit_test_;
    ChromeInteraction chrome_interaction_;
    bool borderless_chrome_ = false;
    bool chrome_mouse_captured_ = false;
    bool chrome_resize_cursor_active_ = false;
    bool live_resize_frame_size_valid_ = false;
    std::uint32_t live_resize_frame_width_pixels_ = 0;
    std::uint32_t live_resize_frame_height_pixels_ = 0;
    bool live_resize_frame_callback_pending_ = false;
    std::uint32_t live_resize_frame_pending_width_pixels_ = 0;
    std::uint32_t live_resize_frame_pending_height_pixels_ = 0;
    LiveResizeFrameCallback live_resize_frame_callback_;
#if defined(_WIN32)
    void *native_chrome_window_handle_ = nullptr;
    std::intptr_t previous_native_chrome_window_proc_ = 0;
    int native_chrome_message_depth_ = 0;
    bool native_move_size_active_ = false;
    bool native_resize_active_ = false;
    bool native_live_resize_frame_active_ = false;
    bool live_resize_frame_callback_posted_ = false;
    bool live_resize_frame_paint_scheduled_ = false;
#endif
    std::array<SDL_Cursor *, SDL_SYSTEM_CURSOR_COUNT> chrome_cursors_{};
};

} // namespace quader

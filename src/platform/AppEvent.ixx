module;

#include <cstdint>
#include <variant>

export module quader.platform.app_event;

export namespace quader {

enum class AppKey : uint8_t {
    unknown,
    w,
    a,
    s,
    d,
    q,
    e,
    r,
    t,
    escape,
    left_shift,
    right_shift,
    count,
};

enum class AppMouseButton : uint8_t {
    unknown,
    left,
    right,
    middle,
    count,
};

enum class AppWindowEventType : uint8_t {
    shown,
    hidden,
    resized,
    pixel_size_changed,
    minimized,
    maximized,
    restored,
    mouse_enter,
    mouse_leave,
    focus_gained,
    focus_lost,
    close_requested,
};

struct AppKeyEvent {
    AppKey key = AppKey::unknown;
    bool pressed = false;
    bool repeat = false;
};

struct AppMouseMotionEvent {
    float x = 0.0f;
    float y = 0.0f;
    float delta_x = 0.0f;
    float delta_y = 0.0f;
};

struct AppMouseButtonEvent {
    AppMouseButton button = AppMouseButton::unknown;
    bool pressed = false;
    uint8_t clicks = 0;
    float x = 0.0f;
    float y = 0.0f;
};

struct AppMouseWheelEvent {
    float horizontal = 0.0f;
    float vertical = 0.0f;
    float x = 0.0f;
    float y = 0.0f;
};

struct AppWindowEvent {
    AppWindowEventType type = AppWindowEventType::shown;
    int width = 0;
    int height = 0;
};

struct AppQuitEvent {
};

using AppEvent = std::variant<
    AppKeyEvent,
    AppMouseMotionEvent,
    AppMouseButtonEvent,
    AppMouseWheelEvent,
    AppWindowEvent,
    AppQuitEvent>;

} // namespace quader

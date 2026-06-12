module;

#include <SDL3/SDL.h>

#include <cstdint>
#include <optional>

module quader.platform.sdl_event_source;

namespace quader {
namespace {

AppKey to_app_key(SDL_Scancode scancode)
{
    switch (scancode) {
    case SDL_SCANCODE_W:
        return AppKey::w;
    case SDL_SCANCODE_A:
        return AppKey::a;
    case SDL_SCANCODE_S:
        return AppKey::s;
    case SDL_SCANCODE_D:
        return AppKey::d;
    case SDL_SCANCODE_Q:
        return AppKey::q;
    case SDL_SCANCODE_E:
        return AppKey::e;
    case SDL_SCANCODE_R:
        return AppKey::r;
    case SDL_SCANCODE_T:
        return AppKey::t;
    case SDL_SCANCODE_ESCAPE:
        return AppKey::escape;
    case SDL_SCANCODE_LSHIFT:
        return AppKey::left_shift;
    case SDL_SCANCODE_RSHIFT:
        return AppKey::right_shift;
    default:
        return AppKey::unknown;
    }
}

AppMouseButton to_app_mouse_button(uint8_t button)
{
    switch (button) {
    case SDL_BUTTON_LEFT:
        return AppMouseButton::left;
    case SDL_BUTTON_RIGHT:
        return AppMouseButton::right;
    case SDL_BUTTON_MIDDLE:
        return AppMouseButton::middle;
    default:
        return AppMouseButton::unknown;
    }
}

std::optional<AppWindowEventType> to_app_window_event_type(SDL_EventType event_type)
{
    switch (event_type) {
    case SDL_EVENT_WINDOW_SHOWN:
        return AppWindowEventType::shown;
    case SDL_EVENT_WINDOW_HIDDEN:
        return AppWindowEventType::hidden;
    case SDL_EVENT_WINDOW_RESIZED:
        return AppWindowEventType::resized;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        return AppWindowEventType::pixel_size_changed;
    case SDL_EVENT_WINDOW_MINIMIZED:
        return AppWindowEventType::minimized;
    case SDL_EVENT_WINDOW_MAXIMIZED:
        return AppWindowEventType::maximized;
    case SDL_EVENT_WINDOW_RESTORED:
        return AppWindowEventType::restored;
    case SDL_EVENT_WINDOW_MOUSE_ENTER:
        return AppWindowEventType::mouse_enter;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        return AppWindowEventType::mouse_leave;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        return AppWindowEventType::focus_gained;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        return AppWindowEventType::focus_lost;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        return AppWindowEventType::close_requested;
    default:
        return std::nullopt;
    }
}

bool is_event_for_window(SDL_WindowID event_window_id, const SdlWindow& window)
{
    return event_window_id == window.id();
}

std::optional<AppEvent> translate_sdl_event(const SDL_Event& event, const SdlWindow& window)
{
    switch (event.type) {
    case SDL_EVENT_QUIT:
        return AppQuitEvent {};
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        if (!is_event_for_window(event.key.windowID, window)) {
            return std::nullopt;
        }
        return AppKeyEvent {
            .key = to_app_key(event.key.scancode),
            .pressed = event.key.down,
            .repeat = event.key.repeat,
        };
    case SDL_EVENT_MOUSE_MOTION:
        if (!is_event_for_window(event.motion.windowID, window)) {
            return std::nullopt;
        }
        return AppMouseMotionEvent {
            .x = event.motion.x,
            .y = event.motion.y,
            .delta_x = event.motion.xrel,
            .delta_y = event.motion.yrel,
        };
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (!is_event_for_window(event.button.windowID, window)) {
            return std::nullopt;
        }
        return AppMouseButtonEvent {
            .button = to_app_mouse_button(event.button.button),
            .pressed = event.button.down,
            .clicks = event.button.clicks,
            .x = event.button.x,
            .y = event.button.y,
        };
    case SDL_EVENT_MOUSE_WHEEL:
        if (!is_event_for_window(event.wheel.windowID, window)) {
            return std::nullopt;
        }
        return AppMouseWheelEvent {
            .horizontal = event.wheel.x,
            .vertical = event.wheel.y,
            .x = event.wheel.mouse_x,
            .y = event.wheel.mouse_y,
        };
    default:
        break;
    }

    if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST) {
        if (!is_event_for_window(event.window.windowID, window)) {
            return std::nullopt;
        }

        if (const std::optional<AppWindowEventType> window_event_type =
                to_app_window_event_type(static_cast<SDL_EventType>(event.type))) {
            return AppWindowEvent {
                .type = *window_event_type,
                .width = event.window.data1,
                .height = event.window.data2,
            };
        }
    }

    return std::nullopt;
}

} // namespace

SdlEventSource::SdlEventSource(const SdlWindow& window)
    : window_(window)
{
}

void SdlEventSource::poll_events(AppEventDispatcher& dispatcher, ISdlNativeEventSink& native_sink)
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        native_sink.process_sdl_event(event);
        if (const std::optional<AppEvent> app_event = translate_sdl_event(event, window_)) {
            dispatcher.dispatch(*app_event);
        }
    }
}

} // namespace quader

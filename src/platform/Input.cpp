module;

#include <variant>

module quader.platform.input;

namespace quader {
namespace {

void update_button_state(InputButtonState& state, bool down, bool repeat = false)
{
    if (down) {
        if (!state.down && !repeat) {
            state.pressed = true;
        }
        state.down = true;
        return;
    }

    if (state.down) {
        state.released = true;
    }
    state.down = false;
}

template<class... Visitors>
struct Overloaded : Visitors... {
    using Visitors::operator()...;
};

template<class... Visitors>
Overloaded(Visitors...) -> Overloaded<Visitors...>;

} // namespace

void Input::begin_frame()
{
    clear_button_transitions();
    mouse_delta_ = {};
    mouse_wheel_delta_ = {};
    close_requested_ = false;
    window_resized_this_frame_ = false;
}

void Input::handle_event(const AppEvent& event)
{
    std::visit(
        Overloaded {
            [this](const AppKeyEvent& key_event) { handle_key_event(key_event); },
            [this](const AppMouseMotionEvent& mouse_event) { handle_mouse_motion_event(mouse_event); },
            [this](const AppMouseButtonEvent& mouse_event) { handle_mouse_button_event(mouse_event); },
            [this](const AppMouseWheelEvent& wheel_event) { handle_mouse_wheel_event(wheel_event); },
            [this](const AppWindowEvent& window_event) { handle_window_event(window_event); },
            [this](const AppQuitEvent&) { close_requested_ = true; },
        },
        event);
}

bool Input::is_key_down(AppKey key) const
{
    return can_index(key) && keys_[index_of(key)].down;
}

bool Input::was_key_pressed(AppKey key) const
{
    return can_index(key) && keys_[index_of(key)].pressed;
}

bool Input::was_key_released(AppKey key) const
{
    return can_index(key) && keys_[index_of(key)].released;
}

bool Input::is_mouse_button_down(AppMouseButton button) const
{
    return can_index(button) && mouse_buttons_[index_of(button)].down;
}

bool Input::was_mouse_button_pressed(AppMouseButton button) const
{
    return can_index(button) && mouse_buttons_[index_of(button)].pressed;
}

bool Input::was_mouse_button_released(AppMouseButton button) const
{
    return can_index(button) && mouse_buttons_[index_of(button)].released;
}

MousePosition Input::mouse_position() const
{
    return mouse_position_;
}

MouseDelta Input::mouse_delta() const
{
    return mouse_delta_;
}

MouseDelta Input::mouse_wheel_delta() const
{
    return mouse_wheel_delta_;
}

bool Input::close_requested() const
{
    return close_requested_;
}

bool Input::window_has_keyboard_focus() const
{
    return window_keyboard_focus_;
}

bool Input::window_has_mouse_focus() const
{
    return window_mouse_focus_;
}

bool Input::window_resized_this_frame() const
{
    return window_resized_this_frame_;
}

int Input::window_width() const
{
    return window_width_;
}

int Input::window_height() const
{
    return window_height_;
}

void Input::handle_key_event(const AppKeyEvent& event)
{
    if (!can_index(event.key)) {
        return;
    }

    update_button_state(keys_[index_of(event.key)], event.pressed, event.repeat);
    if (event.key == AppKey::escape && event.pressed && !event.repeat) {
        close_requested_ = true;
    }
}

void Input::handle_mouse_motion_event(const AppMouseMotionEvent& event)
{
    mouse_position_ = { event.x, event.y };
    mouse_delta_.x += event.delta_x;
    mouse_delta_.y += event.delta_y;
}

void Input::handle_mouse_button_event(const AppMouseButtonEvent& event)
{
    mouse_position_ = { event.x, event.y };
    if (!can_index(event.button)) {
        return;
    }

    update_button_state(mouse_buttons_[index_of(event.button)], event.pressed);
}

void Input::handle_mouse_wheel_event(const AppMouseWheelEvent& event)
{
    mouse_position_ = { event.x, event.y };
    mouse_wheel_delta_.x += event.horizontal;
    mouse_wheel_delta_.y += event.vertical;
}

void Input::handle_window_event(const AppWindowEvent& event)
{
    switch (event.type) {
    case AppWindowEventType::resized:
    case AppWindowEventType::pixel_size_changed:
        window_width_ = event.width;
        window_height_ = event.height;
        window_resized_this_frame_ = true;
        break;
    case AppWindowEventType::mouse_enter:
        window_mouse_focus_ = true;
        break;
    case AppWindowEventType::mouse_leave:
        window_mouse_focus_ = false;
        break;
    case AppWindowEventType::focus_gained:
        window_keyboard_focus_ = true;
        break;
    case AppWindowEventType::focus_lost:
        window_keyboard_focus_ = false;
        clear_button_state();
        break;
    case AppWindowEventType::close_requested:
        close_requested_ = true;
        break;
    case AppWindowEventType::shown:
    case AppWindowEventType::hidden:
    case AppWindowEventType::minimized:
    case AppWindowEventType::maximized:
    case AppWindowEventType::restored:
        break;
    }
}

void Input::clear_button_transitions()
{
    for (InputButtonState& key : keys_) {
        key.pressed = false;
        key.released = false;
    }

    for (InputButtonState& button : mouse_buttons_) {
        button.pressed = false;
        button.released = false;
    }
}

void Input::clear_button_state()
{
    for (InputButtonState& key : keys_) {
        key = {};
    }

    for (InputButtonState& button : mouse_buttons_) {
        button = {};
    }
}

bool Input::can_index(AppKey key)
{
    return key != AppKey::unknown && index_of(key) < key_count;
}

bool Input::can_index(AppMouseButton button)
{
    return button != AppMouseButton::unknown && index_of(button) < mouse_button_count;
}

std::size_t Input::index_of(AppKey key)
{
    return static_cast<std::size_t>(key);
}

std::size_t Input::index_of(AppMouseButton button)
{
    return static_cast<std::size_t>(button);
}

} // namespace quader

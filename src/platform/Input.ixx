module;

#include <array>
#include <cstddef>

export module quader.platform.input;

export import quader.platform.app_event_dispatcher;

export namespace quader {

struct InputButtonState {
    bool down = false;
    bool pressed = false;
    bool released = false;
};

struct MousePosition {
    float x = 0.0f;
    float y = 0.0f;
};

struct MouseDelta {
    float x = 0.0f;
    float y = 0.0f;
};

class Input final : public IAppEventListener {
public:
    void begin_frame();
    void handle_event(const AppEvent& event) override;

    [[nodiscard]] bool is_key_down(AppKey key) const;
    [[nodiscard]] bool was_key_pressed(AppKey key) const;
    [[nodiscard]] bool was_key_released(AppKey key) const;

    [[nodiscard]] bool is_mouse_button_down(AppMouseButton button) const;
    [[nodiscard]] bool was_mouse_button_pressed(AppMouseButton button) const;
    [[nodiscard]] bool was_mouse_button_released(AppMouseButton button) const;

    [[nodiscard]] MousePosition mouse_position() const;
    [[nodiscard]] MouseDelta mouse_delta() const;
    [[nodiscard]] MouseDelta mouse_wheel_delta() const;

    [[nodiscard]] bool close_requested() const;
    [[nodiscard]] bool window_has_keyboard_focus() const;
    [[nodiscard]] bool window_has_mouse_focus() const;
    [[nodiscard]] bool window_resized_this_frame() const;
    [[nodiscard]] int window_width() const;
    [[nodiscard]] int window_height() const;

private:
    static constexpr std::size_t key_count = static_cast<std::size_t>(AppKey::count);
    static constexpr std::size_t mouse_button_count = static_cast<std::size_t>(AppMouseButton::count);

    void handle_key_event(const AppKeyEvent& event);
    void handle_mouse_motion_event(const AppMouseMotionEvent& event);
    void handle_mouse_button_event(const AppMouseButtonEvent& event);
    void handle_mouse_wheel_event(const AppMouseWheelEvent& event);
    void handle_window_event(const AppWindowEvent& event);
    void clear_button_transitions();
    void clear_button_state();

    [[nodiscard]] static bool can_index(AppKey key);
    [[nodiscard]] static bool can_index(AppMouseButton button);
    [[nodiscard]] static std::size_t index_of(AppKey key);
    [[nodiscard]] static std::size_t index_of(AppMouseButton button);

    std::array<InputButtonState, key_count> keys_ {};
    std::array<InputButtonState, mouse_button_count> mouse_buttons_ {};
    MousePosition mouse_position_;
    MouseDelta mouse_delta_;
    MouseDelta mouse_wheel_delta_;
    bool close_requested_ = false;
    bool window_keyboard_focus_ = true;
    bool window_mouse_focus_ = true;
    bool window_resized_this_frame_ = false;
    int window_width_ = 0;
    int window_height_ = 0;
};

} // namespace quader

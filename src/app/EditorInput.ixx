module;

#include <array>
#include <cstddef>
#include <cstdint>

export module quader.app.editor_input;

export import quader.platform.input;

export namespace quader {

enum class EditorInputContext : uint8_t {
    global,
    editor_tools,
    viewport_navigation,
};

enum class EditorInputAction : uint8_t {
    transform_select_move_tool,
    transform_select_rotate_tool,
    transform_select_scale_tool,
    transform_toggle_space,
    viewport_navigation_activate,
    viewport_fly_forward,
    viewport_fly_backward,
    viewport_fly_left,
    viewport_fly_right,
    viewport_fly_boost,
    count,
};

enum class EditorInputTriggerKind : uint8_t {
    key,
    mouse_button,
};

struct EditorInputEnvironment {
    bool window_has_keyboard_focus = true;
    bool window_has_mouse_focus = true;
    bool text_input_active = false;
    bool viewport_hovered = false;
    bool viewport_navigation_captured = false;
    bool transform_gizmo_dragging = false;
};

struct EditorInputBinding {
    EditorInputAction action = EditorInputAction::count;
    EditorInputContext context = EditorInputContext::global;
    EditorInputTriggerKind trigger_kind = EditorInputTriggerKind::key;
    AppKey key = AppKey::unknown;
    AppMouseButton mouse_button = AppMouseButton::unknown;
};

struct EditorInputActionState {
    bool down = false;
    bool pressed = false;
    bool released = false;
};

class ResolvedEditorInput {
public:
    [[nodiscard]] bool is_action_down(EditorInputAction action) const noexcept;
    [[nodiscard]] bool was_action_pressed(EditorInputAction action) const noexcept;
    [[nodiscard]] bool was_action_released(EditorInputAction action) const noexcept;

private:
    static constexpr std::size_t action_count = static_cast<std::size_t>(EditorInputAction::count);

    void merge_action_state(EditorInputAction action, EditorInputActionState state) noexcept;

    std::array<EditorInputActionState, action_count> actions_ {};

    friend class EditorInputResolver;
};

class EditorInputResolver {
public:
    [[nodiscard]] ResolvedEditorInput resolve(
        const Input& input,
        const EditorInputEnvironment& environment) const;
    [[nodiscard]] bool is_context_active(
        const Input& input,
        const EditorInputEnvironment& environment,
        EditorInputContext context) const;

private:
    [[nodiscard]] bool viewport_navigation_gate(const EditorInputEnvironment& environment) const noexcept;
    [[nodiscard]] bool viewport_navigation_activation_down(const Input& input) const;
};

struct TransformToolInputActions {
    bool select_move_pressed = false;
    bool select_rotate_pressed = false;
    bool select_scale_pressed = false;
    bool toggle_space_pressed = false;
};

struct ViewportNavigationInputActions {
    bool activate_pressed = false;
    bool activate_down = false;
    bool activate_released = false;
    bool fly_forward_down = false;
    bool fly_backward_down = false;
    bool fly_left_down = false;
    bool fly_right_down = false;
    bool boost_down = false;
};

[[nodiscard]] EditorInputEnvironment editor_input_environment_from_input(
    const Input& input,
    bool text_input_active,
    bool viewport_hovered = false,
    bool viewport_navigation_captured = false,
    bool transform_gizmo_dragging = false) noexcept;
[[nodiscard]] TransformToolInputActions transform_tool_input_actions_from(
    const ResolvedEditorInput& input) noexcept;
[[nodiscard]] ViewportNavigationInputActions viewport_navigation_input_actions_from(
    const ResolvedEditorInput& input) noexcept;

} // namespace quader

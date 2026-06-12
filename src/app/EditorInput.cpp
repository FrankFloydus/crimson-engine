module;

#include <array>
#include <cstddef>

module quader.app.editor_input;

namespace quader {
namespace {

constexpr std::array<EditorInputBinding, 10> default_bindings {
    EditorInputBinding {
        .action = EditorInputAction::transform_select_move_tool,
        .context = EditorInputContext::editor_tools,
        .trigger_kind = EditorInputTriggerKind::key,
        .key = AppKey::w,
    },
    EditorInputBinding {
        .action = EditorInputAction::transform_select_rotate_tool,
        .context = EditorInputContext::editor_tools,
        .trigger_kind = EditorInputTriggerKind::key,
        .key = AppKey::r,
    },
    EditorInputBinding {
        .action = EditorInputAction::transform_select_scale_tool,
        .context = EditorInputContext::editor_tools,
        .trigger_kind = EditorInputTriggerKind::key,
        .key = AppKey::e,
    },
    EditorInputBinding {
        .action = EditorInputAction::transform_toggle_space,
        .context = EditorInputContext::editor_tools,
        .trigger_kind = EditorInputTriggerKind::key,
        .key = AppKey::t,
    },
    EditorInputBinding {
        .action = EditorInputAction::viewport_navigation_activate,
        .context = EditorInputContext::viewport_navigation,
        .trigger_kind = EditorInputTriggerKind::mouse_button,
        .mouse_button = AppMouseButton::right,
    },
    EditorInputBinding {
        .action = EditorInputAction::viewport_fly_forward,
        .context = EditorInputContext::viewport_navigation,
        .trigger_kind = EditorInputTriggerKind::key,
        .key = AppKey::w,
    },
    EditorInputBinding {
        .action = EditorInputAction::viewport_fly_backward,
        .context = EditorInputContext::viewport_navigation,
        .trigger_kind = EditorInputTriggerKind::key,
        .key = AppKey::s,
    },
    EditorInputBinding {
        .action = EditorInputAction::viewport_fly_left,
        .context = EditorInputContext::viewport_navigation,
        .trigger_kind = EditorInputTriggerKind::key,
        .key = AppKey::a,
    },
    EditorInputBinding {
        .action = EditorInputAction::viewport_fly_right,
        .context = EditorInputContext::viewport_navigation,
        .trigger_kind = EditorInputTriggerKind::key,
        .key = AppKey::d,
    },
    EditorInputBinding {
        .action = EditorInputAction::viewport_fly_boost,
        .context = EditorInputContext::viewport_navigation,
        .trigger_kind = EditorInputTriggerKind::key,
        .key = AppKey::left_shift,
    },
};

[[nodiscard]] bool can_use_keyboard(const EditorInputEnvironment& environment) noexcept
{
    return environment.window_has_keyboard_focus && !environment.text_input_active;
}

[[nodiscard]] bool trigger_down(const Input& input, const EditorInputBinding& binding)
{
    switch (binding.trigger_kind) {
    case EditorInputTriggerKind::key:
        return input.is_key_down(binding.key);
    case EditorInputTriggerKind::mouse_button:
        return input.is_mouse_button_down(binding.mouse_button);
    }

    return false;
}

[[nodiscard]] bool trigger_pressed(const Input& input, const EditorInputBinding& binding)
{
    switch (binding.trigger_kind) {
    case EditorInputTriggerKind::key:
        return input.was_key_pressed(binding.key);
    case EditorInputTriggerKind::mouse_button:
        return input.was_mouse_button_pressed(binding.mouse_button);
    }

    return false;
}

[[nodiscard]] bool trigger_released(const Input& input, const EditorInputBinding& binding)
{
    switch (binding.trigger_kind) {
    case EditorInputTriggerKind::key:
        return input.was_key_released(binding.key);
    case EditorInputTriggerKind::mouse_button:
        return input.was_mouse_button_released(binding.mouse_button);
    }

    return false;
}

[[nodiscard]] EditorInputActionState trigger_state(const Input& input, const EditorInputBinding& binding)
{
    return EditorInputActionState {
        .down = trigger_down(input, binding),
        .pressed = trigger_pressed(input, binding),
        .released = trigger_released(input, binding),
    };
}

[[nodiscard]] std::size_t action_index(EditorInputAction action) noexcept
{
    return static_cast<std::size_t>(action);
}

} // namespace

bool ResolvedEditorInput::is_action_down(EditorInputAction action) const noexcept
{
    if (action == EditorInputAction::count) {
        return false;
    }

    return actions_[action_index(action)].down;
}

bool ResolvedEditorInput::was_action_pressed(EditorInputAction action) const noexcept
{
    if (action == EditorInputAction::count) {
        return false;
    }

    return actions_[action_index(action)].pressed;
}

bool ResolvedEditorInput::was_action_released(EditorInputAction action) const noexcept
{
    if (action == EditorInputAction::count) {
        return false;
    }

    return actions_[action_index(action)].released;
}

void ResolvedEditorInput::merge_action_state(EditorInputAction action, EditorInputActionState state) noexcept
{
    if (action == EditorInputAction::count) {
        return;
    }

    EditorInputActionState& existing = actions_[action_index(action)];
    existing.down = existing.down || state.down;
    existing.pressed = existing.pressed || state.pressed;
    existing.released = existing.released || state.released;
}

ResolvedEditorInput EditorInputResolver::resolve(
    const Input& input,
    const EditorInputEnvironment& environment) const
{
    ResolvedEditorInput resolved;
    for (const EditorInputBinding& binding : default_bindings) {
        const bool can_resolve_activation =
            binding.action == EditorInputAction::viewport_navigation_activate &&
            viewport_navigation_gate(environment);
        if (!can_resolve_activation && !is_context_active(input, environment, binding.context)) {
            continue;
        }

        resolved.merge_action_state(binding.action, trigger_state(input, binding));
    }

    return resolved;
}

bool EditorInputResolver::is_context_active(
    const Input& input,
    const EditorInputEnvironment& environment,
    EditorInputContext context) const
{
    switch (context) {
    case EditorInputContext::global:
        return can_use_keyboard(environment);
    case EditorInputContext::editor_tools:
        return can_use_keyboard(environment) &&
            !environment.transform_gizmo_dragging &&
            !is_context_active(input, environment, EditorInputContext::viewport_navigation);
    case EditorInputContext::viewport_navigation:
        return can_use_keyboard(environment) &&
            environment.window_has_mouse_focus &&
            viewport_navigation_gate(environment) &&
            viewport_navigation_activation_down(input);
    }

    return false;
}

bool EditorInputResolver::viewport_navigation_gate(const EditorInputEnvironment& environment) const noexcept
{
    return environment.viewport_hovered || environment.viewport_navigation_captured;
}

bool EditorInputResolver::viewport_navigation_activation_down(const Input& input) const
{
    for (const EditorInputBinding& binding : default_bindings) {
        if (binding.action == EditorInputAction::viewport_navigation_activate) {
            return trigger_down(input, binding);
        }
    }

    return false;
}

EditorInputEnvironment editor_input_environment_from_input(
    const Input& input,
    bool text_input_active,
    bool viewport_hovered,
    bool viewport_navigation_captured,
    bool transform_gizmo_dragging) noexcept
{
    return EditorInputEnvironment {
        .window_has_keyboard_focus = input.window_has_keyboard_focus(),
        .window_has_mouse_focus = input.window_has_mouse_focus(),
        .text_input_active = text_input_active,
        .viewport_hovered = viewport_hovered,
        .viewport_navigation_captured = viewport_navigation_captured,
        .transform_gizmo_dragging = transform_gizmo_dragging,
    };
}

TransformToolInputActions transform_tool_input_actions_from(const ResolvedEditorInput& input) noexcept
{
    return TransformToolInputActions {
        .select_move_pressed = input.was_action_pressed(EditorInputAction::transform_select_move_tool),
        .select_rotate_pressed = input.was_action_pressed(EditorInputAction::transform_select_rotate_tool),
        .select_scale_pressed = input.was_action_pressed(EditorInputAction::transform_select_scale_tool),
        .toggle_space_pressed = input.was_action_pressed(EditorInputAction::transform_toggle_space),
    };
}

ViewportNavigationInputActions viewport_navigation_input_actions_from(const ResolvedEditorInput& input) noexcept
{
    return ViewportNavigationInputActions {
        .activate_pressed = input.was_action_pressed(EditorInputAction::viewport_navigation_activate),
        .activate_down = input.is_action_down(EditorInputAction::viewport_navigation_activate),
        .activate_released = input.was_action_released(EditorInputAction::viewport_navigation_activate),
        .fly_forward_down = input.is_action_down(EditorInputAction::viewport_fly_forward),
        .fly_backward_down = input.is_action_down(EditorInputAction::viewport_fly_backward),
        .fly_left_down = input.is_action_down(EditorInputAction::viewport_fly_left),
        .fly_right_down = input.is_action_down(EditorInputAction::viewport_fly_right),
        .boost_down = input.is_action_down(EditorInputAction::viewport_fly_boost),
    };
}

} // namespace quader

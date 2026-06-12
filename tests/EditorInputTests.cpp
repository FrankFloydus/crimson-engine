#include <gtest/gtest.h>

import quader.app.editor_input;

namespace {

quader::ResolvedEditorInput resolve(
    quader::Input& input,
    bool text_input_active = false,
    bool viewport_hovered = false,
    bool viewport_navigation_captured = false,
    bool transform_gizmo_dragging = false)
{
    const quader::EditorInputResolver resolver;
    return resolver.resolve(
        input,
        quader::editor_input_environment_from_input(
            input,
            text_input_active,
            viewport_hovered,
            viewport_navigation_captured,
            transform_gizmo_dragging));
}

} // namespace

TEST(EditorInput, WWithoutRmbResolvesMoveToolNotFlyForward)
{
    quader::Input input;
    input.begin_frame();
    input.handle_event(quader::AppKeyEvent { .key = quader::AppKey::w, .pressed = true });

    const quader::ResolvedEditorInput resolved = resolve(input);

    EXPECT_TRUE(resolved.was_action_pressed(quader::EditorInputAction::transform_select_move_tool));
    EXPECT_FALSE(resolved.is_action_down(quader::EditorInputAction::viewport_fly_forward));
}

TEST(EditorInput, WWithRmbOverViewportResolvesFlyForwardNotMoveTool)
{
    quader::Input input;
    input.begin_frame();
    input.handle_event(quader::AppMouseButtonEvent {
        .button = quader::AppMouseButton::right,
        .pressed = true,
    });
    input.handle_event(quader::AppKeyEvent { .key = quader::AppKey::w, .pressed = true });

    const quader::ResolvedEditorInput resolved = resolve(input, false, true);

    EXPECT_FALSE(resolved.was_action_pressed(quader::EditorInputAction::transform_select_move_tool));
    EXPECT_TRUE(resolved.was_action_pressed(quader::EditorInputAction::viewport_navigation_activate));
    EXPECT_TRUE(resolved.is_action_down(quader::EditorInputAction::viewport_fly_forward));
}

TEST(EditorInput, KeyboardFlyRequiresViewportNavigationContext)
{
    quader::Input input;
    input.begin_frame();
    input.handle_event(quader::AppKeyEvent { .key = quader::AppKey::w, .pressed = true });

    const quader::ResolvedEditorInput hovered_without_rmb = resolve(input, false, true);

    EXPECT_FALSE(hovered_without_rmb.is_action_down(quader::EditorInputAction::viewport_fly_forward));
}

TEST(EditorInput, RmbReleaseEndsViewportNavigationEvenIfWIsHeld)
{
    quader::Input input;
    input.begin_frame();
    input.handle_event(quader::AppMouseButtonEvent {
        .button = quader::AppMouseButton::right,
        .pressed = true,
    });
    input.handle_event(quader::AppKeyEvent { .key = quader::AppKey::w, .pressed = true });

    input.begin_frame();
    input.handle_event(quader::AppMouseButtonEvent {
        .button = quader::AppMouseButton::right,
        .pressed = false,
    });

    const quader::ResolvedEditorInput resolved = resolve(input, false, false, true);

    EXPECT_TRUE(resolved.was_action_released(quader::EditorInputAction::viewport_navigation_activate));
    EXPECT_FALSE(resolved.is_action_down(quader::EditorInputAction::viewport_fly_forward));
}

TEST(EditorInput, TextInputSuppressesToolAndViewportActions)
{
    quader::Input input;
    input.begin_frame();
    input.handle_event(quader::AppMouseButtonEvent {
        .button = quader::AppMouseButton::right,
        .pressed = true,
    });
    input.handle_event(quader::AppKeyEvent { .key = quader::AppKey::w, .pressed = true });

    const quader::ResolvedEditorInput resolved = resolve(input, true, true);

    EXPECT_FALSE(resolved.was_action_pressed(quader::EditorInputAction::transform_select_move_tool));
    EXPECT_FALSE(resolved.is_action_down(quader::EditorInputAction::viewport_fly_forward));
}

TEST(EditorInput, GizmoDragSuppressesToolHotkeys)
{
    quader::Input input;
    input.begin_frame();
    input.handle_event(quader::AppKeyEvent { .key = quader::AppKey::r, .pressed = true });

    const quader::ResolvedEditorInput resolved = resolve(input, false, false, false, true);

    EXPECT_FALSE(resolved.was_action_pressed(quader::EditorInputAction::transform_select_rotate_tool));
}

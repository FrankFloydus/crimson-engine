#include <gtest/gtest.h>

#include <optional>

import quader.app.transform_gizmo;
import quader.app.editor_input;
import quader.platform.input;

namespace {

quader::ViewportSurfaceResult drag_surface(
    quader::ViewportSurfacePointer pointer,
    bool left_down = true,
    bool left_released = false)
{
    return quader::ViewportSurfaceResult {
        .requested_extent = { 800, 600 },
        .image_bounds = {
            .width = 800.0f,
            .height = 600.0f,
        },
        .visible = true,
        .left_down = left_down,
        .left_released = left_released,
        .pointer = pointer,
    };
}

quader::ViewportCamera test_camera()
{
    return quader::ViewportCamera {
        .position = { 0.0f, 0.0f, 5.0f },
        .forward = { 0.0f, 0.0f, -1.0f },
        .up = { 0.0f, 1.0f, 0.0f },
    };
}

quader::ViewportCamera close_test_camera()
{
    return quader::ViewportCamera {
        .position = { 0.0f, 0.0f, 1.5f },
        .forward = { 0.0f, 0.0f, -1.0f },
        .up = { 0.0f, 1.0f, 0.0f },
    };
}

quader::ViewportProjection test_projection()
{
    return quader::ViewportProjection {
        .kind = quader::ViewportProjectionKind::perspective,
        .vertical_fov_degrees = 52.0f,
    };
}

quader::ViewportProjection test_orthographic_projection()
{
    return quader::ViewportProjection {
        .kind = quader::ViewportProjectionKind::orthographic,
        .orthographic_size = 10.0f,
    };
}

quader::TransformToolInputActions tool_actions_from_input(
    quader::Input& input,
    bool text_input_active = false,
    bool viewport_navigation_captured = false,
    bool transform_gizmo_dragging = false)
{
    const quader::EditorInputResolver resolver;
    const quader::ResolvedEditorInput resolved = resolver.resolve(
        input,
        quader::editor_input_environment_from_input(
            input,
            text_input_active,
            false,
            viewport_navigation_captured,
            transform_gizmo_dragging));
    return quader::transform_tool_input_actions_from(resolved);
}

bool apply_drag_update(
    quader::Scene& scene,
    quader::TransformGizmoController& controller,
    quader::ViewportCamera camera,
    quader::ViewportProjection projection,
    const quader::ViewportSurfaceResult& surface)
{
    const quader::TransformGizmoController::DragUpdate update =
        controller.update_drag_preview(scene, camera, projection, surface);
    if (!update.transform_changed || !update.entity_id.has_value() || !update.preview_transform.has_value()) {
        return false;
    }

    return scene.set_transform(*update.entity_id, *update.preview_transform);
}

} // namespace

TEST(TransformGizmo, HotkeysSelectMoveRotateAndScaleTools)
{
    quader::TransformGizmoController controller;
    quader::Input input;

    input.begin_frame();
    input.handle_event(quader::AppKeyEvent { .key = quader::AppKey::r, .pressed = true });
    EXPECT_TRUE(controller.update_tool_hotkeys(tool_actions_from_input(input)));
    EXPECT_EQ(quader::TransformGizmoTool::rotate, controller.active_tool());

    input.begin_frame();
    input.handle_event(quader::AppKeyEvent { .key = quader::AppKey::e, .pressed = true });
    EXPECT_TRUE(controller.update_tool_hotkeys(tool_actions_from_input(input)));
    EXPECT_EQ(quader::TransformGizmoTool::scale, controller.active_tool());

    input.begin_frame();
    input.handle_event(quader::AppKeyEvent { .key = quader::AppKey::w, .pressed = true });
    EXPECT_TRUE(controller.update_tool_hotkeys(tool_actions_from_input(input)));
    EXPECT_EQ(quader::TransformGizmoTool::move, controller.active_tool());
}

TEST(TransformGizmo, TKeyTogglesGlobalAndLocalTransformSpace)
{
    quader::TransformGizmoController controller;
    quader::Input input;

    EXPECT_EQ(quader::TransformGizmoSpace::global, controller.active_space());

    input.begin_frame();
    input.handle_event(quader::AppKeyEvent { .key = quader::AppKey::t, .pressed = true });
    EXPECT_TRUE(controller.update_tool_hotkeys(tool_actions_from_input(input)));
    EXPECT_EQ(quader::TransformGizmoSpace::local, controller.active_space());

    input.begin_frame();
    input.handle_event(quader::AppKeyEvent { .key = quader::AppKey::t, .pressed = false });

    input.begin_frame();
    input.handle_event(quader::AppKeyEvent { .key = quader::AppKey::t, .pressed = true });
    EXPECT_TRUE(controller.update_tool_hotkeys(tool_actions_from_input(input)));
    EXPECT_EQ(quader::TransformGizmoSpace::global, controller.active_space());
}

TEST(TransformGizmo, ContextResolverSuppressesToolHotkeys)
{
    quader::TransformGizmoController controller;
    quader::Input input;

    input.begin_frame();
    input.handle_event(quader::AppKeyEvent { .key = quader::AppKey::r, .pressed = true });
    EXPECT_FALSE(controller.update_tool_hotkeys(tool_actions_from_input(input, true)));
    EXPECT_EQ(quader::TransformGizmoTool::move, controller.active_tool());

    input.begin_frame();
    input.handle_event(quader::AppMouseButtonEvent {
        .button = quader::AppMouseButton::right,
        .pressed = true,
    });
    input.handle_event(quader::AppKeyEvent { .key = quader::AppKey::w, .pressed = true });
    EXPECT_FALSE(controller.update_tool_hotkeys(tool_actions_from_input(input, false, true)));
    EXPECT_EQ(quader::TransformGizmoTool::move, controller.active_tool());
}

TEST(TransformGizmo, MeshEntitiesCanScaleButLightsCannot)
{
    quader::Scene scene;
    const quader::SceneEntityId cube = scene.create_cube();
    const quader::SceneEntityId point_light = scene.create_point_light();
    const quader::SceneEntityId directional_light = scene.create_directional_light();

    EXPECT_TRUE(quader::scene_entity_can_scale(scene, cube));
    EXPECT_FALSE(quader::scene_entity_can_scale(scene, point_light));
    EXPECT_FALSE(quader::scene_entity_can_scale(scene, directional_light));
}

TEST(TransformGizmo, HoveredPayloadIsIncludedInRenderRequest)
{
    quader::Scene scene;
    const quader::SceneEntityId cube = scene.create_cube();
    quader::TransformGizmoController controller;
    const quader::DebugDrawPickPayload payload = quader::transform_gizmo_pick_payload(
        quader::RenderEntityId { cube.value },
        quader::DebugDrawPickRole::transform_move_axis,
        quader::DebugDrawAxis::x);

    EXPECT_TRUE(controller.set_hovered_payload(payload));
    const std::optional<quader::TransformGizmoRequest> request =
        controller.render_request_for_selection(scene, cube, test_camera(), test_projection());

    ASSERT_TRUE(request.has_value());
    ASSERT_TRUE(request->highlighted_payload.has_value());
    EXPECT_EQ(payload, *request->highlighted_payload);
}

TEST(TransformGizmo, ScaleToolRequestMarksLightsAsNonScalable)
{
    quader::Scene scene;
    const quader::SceneEntityId point_light = scene.create_point_light();
    quader::TransformGizmoController controller;
    quader::Input input;

    input.begin_frame();
    input.handle_event(quader::AppKeyEvent { .key = quader::AppKey::e, .pressed = true });
    ASSERT_TRUE(controller.update_tool_hotkeys(tool_actions_from_input(input)));

    const std::optional<quader::TransformGizmoRequest> request =
        controller.render_request_for_selection(scene, point_light, test_camera(), test_projection());

    ASSERT_TRUE(request.has_value());
    ASSERT_TRUE(request->target.has_value());
    EXPECT_EQ(quader::TransformGizmoTool::scale, request->tool);
    EXPECT_FALSE(request->target->can_scale);
}

TEST(TransformGizmo, MoveAxisDragMutatesSelectedTransform)
{
    quader::Scene scene;
    const quader::SceneEntityId cube = scene.create_cube();
    quader::TransformGizmoController controller;

    const quader::DebugDrawPickPayload payload = quader::transform_gizmo_pick_payload(
        quader::RenderEntityId { cube.value },
        quader::DebugDrawPickRole::transform_move_axis,
        quader::DebugDrawAxis::x);
    ASSERT_TRUE(controller.try_begin_drag(
        payload,
        scene,
        test_camera(),
        test_projection(),
        drag_surface({ 400.0f, 300.0f })));

    EXPECT_TRUE(apply_drag_update(
        scene,
        controller,
        test_camera(),
        test_projection(),
        drag_surface({ 500.0f, 300.0f })));

    const quader::TransformComponent* transform = scene.transform(cube);
    ASSERT_NE(nullptr, transform);
    EXPECT_GT(transform->position.x, 0.0f);
    EXPECT_FLOAT_EQ(0.0f, transform->position.y);
    EXPECT_FLOAT_EQ(0.0f, transform->position.z);
}

TEST(TransformGizmo, MovePlaneDragUsesRayPlaneIntersection)
{
    quader::Scene scene;
    const quader::SceneEntityId cube = scene.create_cube();
    quader::TransformGizmoController controller;

    const quader::DebugDrawPickPayload payload = quader::transform_gizmo_pick_payload(
        quader::RenderEntityId { cube.value },
        quader::DebugDrawPickRole::transform_move_plane,
        quader::DebugDrawAxis::z);
    ASSERT_TRUE(controller.try_begin_drag(
        payload,
        scene,
        test_camera(),
        test_orthographic_projection(),
        drag_surface({ 460.0f, 240.0f })));

    EXPECT_TRUE(apply_drag_update(
        scene,
        controller,
        test_camera(),
        test_orthographic_projection(),
        drag_surface({ 520.0f, 210.0f })));

    const quader::TransformComponent* transform = scene.transform(cube);
    ASSERT_NE(nullptr, transform);
    EXPECT_NEAR(1.0f, transform->position.x, 0.0001f);
    EXPECT_NEAR(0.5f, transform->position.y, 0.0001f);
    EXPECT_FLOAT_EQ(0.0f, transform->position.z);
}

TEST(TransformGizmo, UniformScaleDragMutatesAllScaleAxes)
{
    quader::Scene scene;
    const quader::SceneEntityId cube = scene.create_cube();
    quader::TransformGizmoController controller;

    const quader::DebugDrawPickPayload payload = quader::transform_gizmo_pick_payload(
        quader::RenderEntityId { cube.value },
        quader::DebugDrawPickRole::transform_uniform_scale,
        quader::DebugDrawAxis::none);
    ASSERT_TRUE(controller.try_begin_drag(
        payload,
        scene,
        test_camera(),
        test_projection(),
        drag_surface({ 400.0f, 300.0f })));

    EXPECT_TRUE(apply_drag_update(
        scene,
        controller,
        test_camera(),
        test_projection(),
        drag_surface({ 460.0f, 240.0f })));

    const quader::TransformComponent* transform = scene.transform(cube);
    ASSERT_NE(nullptr, transform);
    EXPECT_GT(transform->scale.x, 1.0f);
    EXPECT_FLOAT_EQ(transform->scale.x, transform->scale.y);
    EXPECT_FLOAT_EQ(transform->scale.x, transform->scale.z);
}

TEST(TransformGizmo, UniformScaleDragWorksInOrthographicProjection)
{
    quader::Scene scene;
    const quader::SceneEntityId cube = scene.create_cube();
    quader::TransformGizmoController controller;

    const quader::DebugDrawPickPayload payload = quader::transform_gizmo_pick_payload(
        quader::RenderEntityId { cube.value },
        quader::DebugDrawPickRole::transform_uniform_scale,
        quader::DebugDrawAxis::none);
    ASSERT_TRUE(controller.try_begin_drag(
        payload,
        scene,
        test_camera(),
        test_orthographic_projection(),
        drag_surface({ 400.0f, 300.0f })));

    EXPECT_TRUE(apply_drag_update(
        scene,
        controller,
        test_camera(),
        test_orthographic_projection(),
        drag_surface({ 460.0f, 240.0f })));

    const quader::TransformComponent* transform = scene.transform(cube);
    ASSERT_NE(nullptr, transform);
    EXPECT_GT(transform->scale.x, 1.0f);
    EXPECT_FLOAT_EQ(transform->scale.x, transform->scale.y);
    EXPECT_FLOAT_EQ(transform->scale.x, transform->scale.z);
}

TEST(TransformGizmo, UniformScaleDragSensitivityIsCameraDistanceIndependent)
{
    quader::Scene far_scene;
    const quader::SceneEntityId far_cube = far_scene.create_cube();
    quader::TransformGizmoController far_controller;
    const quader::DebugDrawPickPayload far_payload = quader::transform_gizmo_pick_payload(
        quader::RenderEntityId { far_cube.value },
        quader::DebugDrawPickRole::transform_uniform_scale,
        quader::DebugDrawAxis::none);
    ASSERT_TRUE(far_controller.try_begin_drag(
        far_payload,
        far_scene,
        test_camera(),
        test_projection(),
        drag_surface({ 400.0f, 300.0f })));
    ASSERT_TRUE(apply_drag_update(
        far_scene,
        far_controller,
        test_camera(),
        test_projection(),
        drag_surface({ 500.0f, 300.0f })));

    quader::Scene close_scene;
    const quader::SceneEntityId close_cube = close_scene.create_cube();
    quader::TransformGizmoController close_controller;
    const quader::DebugDrawPickPayload close_payload = quader::transform_gizmo_pick_payload(
        quader::RenderEntityId { close_cube.value },
        quader::DebugDrawPickRole::transform_uniform_scale,
        quader::DebugDrawAxis::none);
    ASSERT_TRUE(close_controller.try_begin_drag(
        close_payload,
        close_scene,
        close_test_camera(),
        test_projection(),
        drag_surface({ 400.0f, 300.0f })));
    ASSERT_TRUE(apply_drag_update(
        close_scene,
        close_controller,
        close_test_camera(),
        test_projection(),
        drag_surface({ 500.0f, 300.0f })));

    const quader::TransformComponent* far_transform = far_scene.transform(far_cube);
    const quader::TransformComponent* close_transform = close_scene.transform(close_cube);
    ASSERT_NE(nullptr, far_transform);
    ASSERT_NE(nullptr, close_transform);
    EXPECT_GT(far_transform->scale.x, 1.5f);
    EXPECT_NEAR(far_transform->scale.x, close_transform->scale.x, 0.0001f);
}

TEST(TransformGizmo, ScalePlaneDragMutatesOnlyPlaneAxesIndependently)
{
    quader::Scene scene;
    const quader::SceneEntityId cube = scene.create_cube();
    quader::TransformGizmoController controller;

    const quader::DebugDrawPickPayload payload = quader::transform_gizmo_pick_payload(
        quader::RenderEntityId { cube.value },
        quader::DebugDrawPickRole::transform_scale_plane,
        quader::DebugDrawAxis::z);
    ASSERT_TRUE(controller.try_begin_drag(
        payload,
        scene,
        test_camera(),
        test_orthographic_projection(),
        drag_surface({ 460.0f, 240.0f })));

    EXPECT_TRUE(apply_drag_update(
        scene,
        controller,
        test_camera(),
        test_orthographic_projection(),
        drag_surface({ 520.0f, 210.0f })));

    const quader::TransformComponent* transform = scene.transform(cube);
    ASSERT_NE(nullptr, transform);
    EXPECT_NEAR(1.5555556f, transform->scale.x, 0.0001f);
    EXPECT_NEAR(1.2777778f, transform->scale.y, 0.0001f);
    EXPECT_FLOAT_EQ(1.0f, transform->scale.z);
}

TEST(TransformGizmo, ScaleAxisDragSensitivityIsCameraDistanceIndependent)
{
    quader::Scene far_scene;
    const quader::SceneEntityId far_cube = far_scene.create_cube();
    quader::TransformGizmoController far_controller;
    const quader::DebugDrawPickPayload far_payload = quader::transform_gizmo_pick_payload(
        quader::RenderEntityId { far_cube.value },
        quader::DebugDrawPickRole::transform_scale_axis,
        quader::DebugDrawAxis::x);
    ASSERT_TRUE(far_controller.try_begin_drag(
        far_payload,
        far_scene,
        test_camera(),
        test_projection(),
        drag_surface({ 400.0f, 300.0f })));
    ASSERT_TRUE(apply_drag_update(
        far_scene,
        far_controller,
        test_camera(),
        test_projection(),
        drag_surface({ 500.0f, 300.0f })));

    quader::Scene close_scene;
    const quader::SceneEntityId close_cube = close_scene.create_cube();
    quader::TransformGizmoController close_controller;
    const quader::DebugDrawPickPayload close_payload = quader::transform_gizmo_pick_payload(
        quader::RenderEntityId { close_cube.value },
        quader::DebugDrawPickRole::transform_scale_axis,
        quader::DebugDrawAxis::x);
    ASSERT_TRUE(close_controller.try_begin_drag(
        close_payload,
        close_scene,
        close_test_camera(),
        test_projection(),
        drag_surface({ 400.0f, 300.0f })));
    ASSERT_TRUE(apply_drag_update(
        close_scene,
        close_controller,
        close_test_camera(),
        test_projection(),
        drag_surface({ 500.0f, 300.0f })));

    const quader::TransformComponent* far_transform = far_scene.transform(far_cube);
    const quader::TransformComponent* close_transform = close_scene.transform(close_cube);
    ASSERT_NE(nullptr, far_transform);
    ASSERT_NE(nullptr, close_transform);
    EXPECT_GT(far_transform->scale.x, 1.5f);
    EXPECT_NEAR(far_transform->scale.x, close_transform->scale.x, 0.0001f);
    EXPECT_FLOAT_EQ(1.0f, far_transform->scale.y);
    EXPECT_FLOAT_EQ(1.0f, close_transform->scale.y);
}

TEST(TransformGizmo, RotateAxisDragWorksWhenAxisIsEdgeOnToCamera)
{
    quader::Scene scene;
    const quader::SceneEntityId cube = scene.create_cube();
    quader::TransformGizmoController controller;

    const quader::DebugDrawPickPayload payload = quader::transform_gizmo_pick_payload(
        quader::RenderEntityId { cube.value },
        quader::DebugDrawPickRole::transform_rotate_axis,
        quader::DebugDrawAxis::x);
    ASSERT_TRUE(controller.try_begin_drag(
        payload,
        scene,
        test_camera(),
        test_projection(),
        drag_surface({ 400.0f, 300.0f })));

    EXPECT_TRUE(apply_drag_update(
        scene,
        controller,
        test_camera(),
        test_projection(),
        drag_surface({ 400.0f, 360.0f })));

    const quader::TransformComponent* transform = scene.transform(cube);
    ASSERT_NE(nullptr, transform);
    EXPECT_GT(transform->get_rotation_degrees().x, 1.0f);
    EXPECT_FLOAT_EQ(0.0f, transform->get_rotation_degrees().y);
    EXPECT_FLOAT_EQ(0.0f, transform->get_rotation_degrees().z);
}

TEST(TransformGizmo, ScaleAxisDragCannotStartForLightTransform)
{
    quader::Scene scene;
    const quader::SceneEntityId point_light = scene.create_point_light();
    quader::TransformGizmoController controller;

    const quader::DebugDrawPickPayload payload = quader::transform_gizmo_pick_payload(
        quader::RenderEntityId { point_light.value },
        quader::DebugDrawPickRole::transform_scale_axis,
        quader::DebugDrawAxis::x);
    EXPECT_FALSE(controller.try_begin_drag(
        payload,
        scene,
        test_camera(),
        test_projection(),
        drag_surface({ 400.0f, 300.0f })));
    EXPECT_FALSE(controller.is_dragging());

    const quader::TransformComponent* transform = scene.transform(point_light);
    ASSERT_NE(nullptr, transform);
    EXPECT_FLOAT_EQ(1.0f, transform->scale.x);
    EXPECT_FLOAT_EQ(1.0f, transform->scale.y);
    EXPECT_FLOAT_EQ(1.0f, transform->scale.z);
}

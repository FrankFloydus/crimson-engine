#include <gtest/gtest.h>

#include <optional>
#include <variant>
#include <vector>

import quader.app.viewport_instance;
import quader.render.viewport_gizmos;

namespace {

quader::ViewportRenderFrame completed_frame_from(const quader::ViewportRenderRequest& request)
{
    quader::ViewportRenderFrame frame {
        .viewport_id = request.viewport_id,
        .generation = request.generation,
        .request_serial = request.request_serial,
        .frame_id = quader::ViewportFrameId { 1 },
        .extent = request.extent,
    };
    frame.payload.temporary_readback = quader::TemporaryRgba8ReadbackFrame {
        .extent = request.extent,
        .rgba = std::vector<uint8_t>(
            static_cast<size_t>(request.extent.width) * static_cast<size_t>(request.extent.height) * 4U,
            255),
    };
    return frame;
}

quader::Scene make_test_scene()
{
    quader::Scene scene("Viewport Test Scene");

    quader::TransformComponent cube_transform;
    cube_transform.position = { 0.0f, 1.0f, 0.0f };
    const quader::SceneEntityId cube = scene.create_cube("Cube", cube_transform);
    scene.material(cube)->material.roughness = 0.75f;

    quader::TransformComponent plane_transform;
    plane_transform.scale = { 10.0f, 1.0f, 10.0f };
    const quader::SceneEntityId plane = scene.create_plane("Plane", plane_transform);
    scene.mesh(plane)->casts_shadows = false;

    const quader::SceneEntityId sun = scene.create_directional_light("Sun");
    (void)sun;

    quader::TransformComponent point_transform;
    point_transform.position = { 1.0f, 2.0f, 0.0f };
    const quader::SceneEntityId point_light = scene.create_point_light("Point Light", point_transform);
    (void)point_light;
    return scene;
}

} // namespace

TEST(ViewportInstance, ViewportIdsAndGenerationsExposeValidity)
{
    EXPECT_FALSE(quader::ViewportId {}.is_valid());
    EXPECT_TRUE((quader::ViewportId { 7 }).is_valid());
    EXPECT_FALSE(quader::ViewportGeneration {}.is_valid());
    EXPECT_TRUE((quader::ViewportGeneration { 1 }).is_valid());
}

TEST(ViewportInstance, SceneViewportKeepsOriginalCameraPresetAsDefault)
{
    quader::ViewportInstance viewport =
        quader::ViewportInstance::scene_viewport({ 12 }, "Top", quader::ViewportCameraPreset::top);

    EXPECT_EQ(quader::ViewportCameraPreset::top, viewport.camera_preset());
    EXPECT_EQ(quader::ViewportCameraPreset::top, viewport.default_camera_preset());

    viewport.set_camera_preset(quader::ViewportCameraPreset::front);

    EXPECT_EQ(quader::ViewportCameraPreset::front, viewport.camera_preset());
    EXPECT_EQ(quader::ViewportCameraPreset::top, viewport.default_camera_preset());
}

TEST(ViewportInstance, SceneViewportBuildsSceneRenderRequest)
{
    quader::Scene scene = make_test_scene();
    quader::EnvironmentLightingSettings environment;
    environment.intensity = 12000.0f;
    environment.rotation_degrees = 30.0f;
    environment.show_skybox = false;
    scene.set_environment_settings(environment);

    quader::ViewportInstance viewport =
        quader::ViewportInstance::scene_viewport({ 1 }, "Perspective", quader::ViewportCameraPreset::perspective);
    viewport.set_requested_extent({ 320, 200 });

    std::optional<quader::ViewportRenderRequest> request =
        viewport.try_begin_render_request(scene);

    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(quader::ViewportId { 1 }, request->viewport_id);
    EXPECT_EQ(320U, request->extent.width);
    ASSERT_TRUE(std::holds_alternative<quader::SceneViewportSubject>(request->subject));
    const quader::SceneViewportSubject& subject =
        std::get<quader::SceneViewportSubject>(request->subject);
    EXPECT_FLOAT_EQ(12000.0f, subject.snapshot.environment.intensity);
    EXPECT_FLOAT_EQ(30.0f, subject.snapshot.environment.rotation_degrees);
    EXPECT_FALSE(subject.snapshot.environment.show_skybox);
    ASSERT_EQ(2U, subject.snapshot.meshes.size());
    EXPECT_TRUE(subject.snapshot.meshes[0].entity_id.is_valid());
    EXPECT_EQ(quader::ViewportMeshKind::cube, subject.snapshot.meshes[0].mesh_kind);
    EXPECT_FLOAT_EQ(0.75f, subject.snapshot.meshes[0].material.roughness);
    EXPECT_TRUE(subject.snapshot.meshes[0].casts_shadows);
    EXPECT_TRUE(subject.snapshot.meshes[0].receives_shadows);
    EXPECT_EQ(quader::ViewportMeshKind::plane, subject.snapshot.meshes[1].mesh_kind);
    EXPECT_FLOAT_EQ(10.0f, subject.snapshot.meshes[1].scale.x);
    EXPECT_FLOAT_EQ(10.0f, subject.snapshot.meshes[1].scale.z);
    EXPECT_FALSE(subject.snapshot.meshes[1].casts_shadows);
    EXPECT_TRUE(subject.snapshot.meshes[1].receives_shadows);
    EXPECT_EQ(1U, subject.snapshot.directional_lights.size());
    EXPECT_EQ(1U, subject.snapshot.point_lights.size());
    EXPECT_TRUE(subject.snapshot.point_lights[0].entity_id.is_valid());
    EXPECT_FALSE(request->debug_draw.empty());
    EXPECT_GE(request->debug_draw.depth_tested.lines().size(), 5U);
    EXPECT_EQ(3U, request->debug_draw.depth_tested.pick_proxies().size());
    EXPECT_TRUE(viewport.request_pending());
    EXPECT_FALSE(viewport.try_begin_render_request(scene).has_value());
}

TEST(ViewportInstance, SceneViewportBuildsPickableLightGizmoDebugDraw)
{
    quader::Scene scene = make_test_scene();
    quader::ViewportInstance viewport =
        quader::ViewportInstance::scene_viewport({ 11 }, "Perspective", quader::ViewportCameraPreset::perspective);
    viewport.set_requested_extent({ 320, 200 });

    const std::optional<quader::ViewportRenderRequest> request =
        viewport.try_begin_render_request(scene);

    ASSERT_TRUE(request.has_value());
    const std::vector<quader::DebugDrawLine>& lines = request->debug_draw.depth_tested.lines();
    const std::vector<quader::DebugDrawPickProxy>& pick_proxies =
        request->debug_draw.depth_tested.pick_proxies();

    ASSERT_FALSE(lines.empty());
    ASSERT_FALSE(pick_proxies.empty());
    for (const quader::DebugDrawLine& line : lines) {
        ASSERT_TRUE(line.pick.has_value());
        EXPECT_TRUE(line.pick->is_valid());
        EXPECT_TRUE(scene.contains(quader::SceneEntityId { line.pick->owner_entity_id }));
    }
    for (const quader::DebugDrawPickProxy& proxy : pick_proxies) {
        EXPECT_TRUE(proxy.is_valid());
        EXPECT_TRUE(scene.contains(quader::SceneEntityId { proxy.payload.owner_entity_id }));
        EXPECT_TRUE(proxy.payload.role == quader::DebugDrawPickRole::directional_light_gizmo ||
            proxy.payload.role == quader::DebugDrawPickRole::point_light_gizmo);
    }
}

TEST(ViewportInstance, DirectionalLightGizmoUsesRolledBasisForArrowHead)
{
    quader::SceneRenderSnapshot snapshot;
    snapshot.directional_lights.push_back(quader::SceneRenderDirectionalLight {
        .entity_id = quader::RenderEntityId { 7 },
        .position = { 0.0f, 0.0f, 0.0f },
        .direction = { 0.0f, 0.0f, -1.0f },
        .right = { 0.0f, 1.0f, 0.0f },
        .up = { -1.0f, 0.0f, 0.0f },
    });

    quader::DebugDrawFrame debug_draw;
    quader::append_light_gizmos(snapshot, debug_draw);

    const std::vector<quader::DebugDrawLine>& lines = debug_draw.depth_tested.lines();
    ASSERT_GE(lines.size(), 5U);
    EXPECT_FLOAT_EQ(0.0f, lines[1].b.position.x);
    EXPECT_FLOAT_EQ(0.12f, lines[1].b.position.y);
    EXPECT_FLOAT_EQ(-0.78f, lines[1].b.position.z);
    EXPECT_FLOAT_EQ(-0.12f, lines[3].b.position.x);
    EXPECT_FLOAT_EQ(0.0f, lines[3].b.position.y);
    EXPECT_FLOAT_EQ(-0.78f, lines[3].b.position.z);
}

TEST(ViewportInstance, AcceptsMatchingFrameAndStoresDisplayedExtent)
{
    quader::Scene scene = make_test_scene();
    quader::ViewportInstance viewport =
        quader::ViewportInstance::scene_viewport({ 3 }, "Top", quader::ViewportCameraPreset::top);
    viewport.set_requested_extent({ 64, 32 });

    const quader::ViewportRenderRequest request = *viewport.try_begin_render_request(scene);
    const quader::ViewportFrameAcceptance acceptance = viewport.accept_frame(completed_frame_from(request));

    EXPECT_TRUE(acceptance.accepted);
    EXPECT_FALSE(acceptance.stale);
    EXPECT_FALSE(viewport.request_pending());
    EXPECT_EQ(64U, viewport.displayed_extent().width);
    EXPECT_EQ(32U, viewport.displayed_extent().height);
}

TEST(ViewportInstance, RejectsFrameWhenExtentChangedWhilePending)
{
    quader::Scene scene = make_test_scene();
    quader::ViewportInstance viewport =
        quader::ViewportInstance::scene_viewport({ 4 }, "Perspective", quader::ViewportCameraPreset::perspective);
    viewport.set_requested_extent({ 64, 32 });

    const quader::ViewportRenderRequest request = *viewport.try_begin_render_request(scene);
    viewport.set_requested_extent({ 128, 64 });

    const quader::ViewportFrameAcceptance acceptance = viewport.accept_frame(completed_frame_from(request));

    EXPECT_FALSE(acceptance.accepted);
    EXPECT_TRUE(acceptance.stale);
    EXPECT_FALSE(viewport.request_pending());
    EXPECT_TRUE(viewport.is_dirty());
}

TEST(ViewportInstance, ScenePerspectiveNavigationLocksMouseForRightDrag)
{
    quader::ViewportInstance viewport =
        quader::ViewportInstance::scene_viewport({ 5 }, "Perspective", quader::ViewportCameraPreset::perspective);

    quader::ViewportNavigationSnapshot input;
    input.hovered = true;
    input.navigation_activate_pressed = true;
    input.navigation_activate_down = true;
    input.mouse_delta = { 12.0f, -4.0f };

    EXPECT_TRUE(viewport.should_start_navigation_mouse_lock(input));
    EXPECT_TRUE(viewport.should_keep_navigation_mouse_lock(input));
    EXPECT_TRUE(viewport.update_navigation(input, true, 0.016f));
}

TEST(ViewportInstance, ScenePerspectiveNavigationDoesNotLockMouseForKeyboardFlyWithoutRmb)
{
    quader::ViewportInstance viewport =
        quader::ViewportInstance::scene_viewport({ 10 }, "Perspective", quader::ViewportCameraPreset::perspective);

    quader::ViewportNavigationSnapshot input;
    input.hovered = true;
    input.fly_forward = true;

    EXPECT_FALSE(viewport.should_start_navigation_mouse_lock(input));

    input.hovered = false;
    EXPECT_FALSE(viewport.should_keep_navigation_mouse_lock(input));
    EXPECT_FALSE(viewport.update_navigation(input, false, 0.016f));
}

TEST(ViewportInstance, ScenePerspectiveNavigationMovesOnlyWhileRmbContextIsActive)
{
    quader::ViewportInstance viewport =
        quader::ViewportInstance::scene_viewport({ 11 }, "Perspective", quader::ViewportCameraPreset::perspective);

    quader::ViewportNavigationSnapshot input;
    input.hovered = true;
    input.navigation_activate_pressed = true;
    input.navigation_activate_down = true;
    input.fly_forward = true;

    EXPECT_TRUE(viewport.should_start_navigation_mouse_lock(input));
    EXPECT_TRUE(viewport.should_keep_navigation_mouse_lock(input));
    EXPECT_TRUE(viewport.update_navigation(input, true, 0.016f));
}

TEST(ViewportInstance, OrthographicSceneRightDragPansOnlyThatViewport)
{
    quader::ViewportInstance viewport =
        quader::ViewportInstance::scene_viewport({ 8 }, "Top", quader::ViewportCameraPreset::top);

    quader::ViewportNavigationSnapshot input;
    input.hovered = true;
    input.navigation_activate_pressed = true;
    input.navigation_activate_down = true;
    input.mouse_delta = { 12.0f, -4.0f };

    EXPECT_TRUE(viewport.should_start_navigation_mouse_lock(input));
    EXPECT_TRUE(viewport.should_keep_navigation_mouse_lock(input));
    EXPECT_TRUE(viewport.update_navigation(input, true, 0.016f));
}

TEST(ViewportInstance, PerspectiveSceneDoesNotStartMouseLockAwayFromViewport)
{
    quader::ViewportInstance viewport =
        quader::ViewportInstance::scene_viewport({ 9 }, "Perspective", quader::ViewportCameraPreset::perspective);

    quader::ViewportNavigationSnapshot input;
    input.hovered = false;
    input.navigation_activate_pressed = false;
    input.navigation_activate_down = true;
    input.mouse_delta = { 12.0f, -4.0f };

    EXPECT_FALSE(viewport.should_start_navigation_mouse_lock(input));
    EXPECT_FALSE(viewport.update_navigation(input, false, 0.016f));
}

TEST(ViewportInstance, NonInteractiveViewportIgnoresNavigationInput)
{
    quader::ViewportInstance viewport = quader::ViewportInstance::non_interactive_scene_viewport(
        { 7 },
        "Read Only",
        quader::ViewportCameraPreset::front);

    quader::ViewportNavigationSnapshot input;
    input.hovered = true;
    input.navigation_activate_pressed = true;
    input.navigation_activate_down = true;
    input.mouse_delta = { 15.0f, 15.0f };

    EXPECT_FALSE(viewport.can_capture_navigation());
    EXPECT_FALSE(viewport.should_start_navigation_mouse_lock(input));
    EXPECT_FALSE(viewport.should_keep_navigation_mouse_lock(input));
    EXPECT_FALSE(viewport.update_navigation(input, true, 0.016f));
}

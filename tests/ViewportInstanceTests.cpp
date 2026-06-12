#include <gtest/gtest.h>

#include <optional>
#include <variant>
#include <vector>

import quader.app.viewport_instance;

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

} // namespace

TEST(ViewportInstance, ViewportIdsAndGenerationsExposeValidity)
{
    EXPECT_FALSE(quader::ViewportId {}.is_valid());
    EXPECT_TRUE((quader::ViewportId { 7 }).is_valid());
    EXPECT_FALSE(quader::ViewportGeneration {}.is_valid());
    EXPECT_TRUE((quader::ViewportGeneration { 1 }).is_valid());
}

TEST(ViewportInstance, SceneViewportBuildsSceneRenderRequest)
{
    quader::Scene scene = quader::create_default_scene();
    quader::ViewportInstance viewport =
        quader::ViewportInstance::scene_viewport({ 1 }, "Perspective", quader::ViewportCameraPreset::perspective);
    viewport.set_requested_extent({ 320, 200 });

    std::optional<quader::ViewportRenderRequest> request =
        viewport.try_begin_render_request(scene, {}, {});

    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(quader::ViewportId { 1 }, request->viewport_id);
    EXPECT_EQ(320U, request->extent.width);
    ASSERT_TRUE(std::holds_alternative<quader::SceneViewportSubject>(request->subject));
    const quader::SceneViewportSubject& subject =
        std::get<quader::SceneViewportSubject>(request->subject);
    EXPECT_EQ(1U, subject.snapshot.meshes.size());
    EXPECT_EQ(1U, subject.snapshot.directional_lights.size());
    EXPECT_TRUE(viewport.request_pending());
    EXPECT_FALSE(viewport.try_begin_render_request(scene, {}, {}).has_value());
}

TEST(ViewportInstance, PreviewViewportBuildsPreviewRenderRequest)
{
    quader::Scene scene = quader::create_default_scene();
    quader::PbrMaterialSettings material;
    material.roughness = 0.75f;
    quader::DirectionalLightSettings light;
    light.intensity = 1200.0f;
    quader::ViewportInstance viewport = quader::ViewportInstance::preview_viewport({ 2 }, "Preview");
    viewport.set_requested_extent({ 128, 128 });

    std::optional<quader::ViewportRenderRequest> request =
        viewport.try_begin_render_request(scene, material, light);

    ASSERT_TRUE(request.has_value());
    ASSERT_TRUE(std::holds_alternative<quader::PreviewViewportSubject>(request->subject));
    const quader::PreviewViewportSubject& subject =
        std::get<quader::PreviewViewportSubject>(request->subject);
    EXPECT_FLOAT_EQ(0.75f, subject.material.roughness);
    EXPECT_FLOAT_EQ(1200.0f, subject.light.intensity);
}

TEST(ViewportInstance, AcceptsMatchingFrameAndStoresDisplayedExtent)
{
    quader::Scene scene = quader::create_default_scene();
    quader::ViewportInstance viewport =
        quader::ViewportInstance::scene_viewport({ 3 }, "Top", quader::ViewportCameraPreset::top);
    viewport.set_requested_extent({ 64, 32 });

    const quader::ViewportRenderRequest request = *viewport.try_begin_render_request(scene, {}, {});
    const quader::ViewportFrameAcceptance acceptance = viewport.accept_frame(completed_frame_from(request));

    EXPECT_TRUE(acceptance.accepted);
    EXPECT_FALSE(acceptance.stale);
    EXPECT_FALSE(viewport.request_pending());
    EXPECT_EQ(64U, viewport.displayed_extent().width);
    EXPECT_EQ(32U, viewport.displayed_extent().height);
}

TEST(ViewportInstance, RejectsFrameWhenExtentChangedWhilePending)
{
    quader::Scene scene = quader::create_default_scene();
    quader::ViewportInstance viewport =
        quader::ViewportInstance::scene_viewport({ 4 }, "Perspective", quader::ViewportCameraPreset::perspective);
    viewport.set_requested_extent({ 64, 32 });

    const quader::ViewportRenderRequest request = *viewport.try_begin_render_request(scene, {}, {});
    viewport.set_requested_extent({ 128, 64 });

    const quader::ViewportFrameAcceptance acceptance = viewport.accept_frame(completed_frame_from(request));

    EXPECT_FALSE(acceptance.accepted);
    EXPECT_TRUE(acceptance.stale);
    EXPECT_FALSE(viewport.request_pending());
    EXPECT_TRUE(viewport.is_dirty());
}

TEST(ViewportInstance, ScenePerspectiveNavigationCapturesRelativeMouse)
{
    quader::ViewportInstance viewport =
        quader::ViewportInstance::scene_viewport({ 5 }, "Perspective", quader::ViewportCameraPreset::perspective);

    quader::ViewportNavigationSnapshot input;
    input.hovered = true;
    input.right_mouse_pressed = true;
    input.right_mouse_down = true;
    input.mouse_delta = { 12.0f, -4.0f };

    EXPECT_TRUE(viewport.should_start_navigation_capture(input));
    EXPECT_TRUE(viewport.update_navigation(input, true, 0.016f));
}

TEST(ViewportInstance, OrthographicSceneRightDragPansOnlyThatViewport)
{
    quader::ViewportInstance viewport =
        quader::ViewportInstance::scene_viewport({ 8 }, "Top", quader::ViewportCameraPreset::top);

    quader::ViewportNavigationSnapshot input;
    input.hovered = true;
    input.right_mouse_pressed = true;
    input.right_mouse_down = true;
    input.mouse_delta = { 12.0f, -4.0f };

    EXPECT_TRUE(viewport.should_start_navigation_capture(input));
    EXPECT_TRUE(viewport.update_navigation(input, true, 0.016f));
}

TEST(ViewportInstance, PerspectiveSceneIgnoresRightDragCapturedByAnotherViewport)
{
    quader::ViewportInstance viewport =
        quader::ViewportInstance::scene_viewport({ 9 }, "Perspective", quader::ViewportCameraPreset::perspective);

    quader::ViewportNavigationSnapshot input;
    input.hovered = false;
    input.right_mouse_pressed = false;
    input.right_mouse_down = true;
    input.mouse_delta = { 12.0f, -4.0f };

    EXPECT_FALSE(viewport.should_start_navigation_capture(input));
    EXPECT_FALSE(viewport.update_navigation(input, false, 0.016f));
}

TEST(ViewportInstance, PreviewOrbitNavigationChangesOnWheel)
{
    quader::ViewportInstance viewport = quader::ViewportInstance::preview_viewport({ 6 }, "Preview");

    quader::ViewportNavigationSnapshot input;
    input.hovered = true;
    input.mouse_wheel_delta = { 0.0f, 1.0f };

    EXPECT_TRUE(viewport.update_navigation(input, false, 0.016f));
    EXPECT_TRUE(viewport.is_dirty());
}

TEST(ViewportInstance, NonInteractiveViewportIgnoresNavigationInput)
{
    quader::ViewportInstance viewport = quader::ViewportInstance::non_interactive_scene_viewport(
        { 7 },
        "Read Only",
        quader::ViewportCameraPreset::front);

    quader::ViewportNavigationSnapshot input;
    input.hovered = true;
    input.right_mouse_pressed = true;
    input.right_mouse_down = true;
    input.mouse_delta = { 15.0f, 15.0f };

    EXPECT_FALSE(viewport.can_capture_navigation());
    EXPECT_FALSE(viewport.should_start_navigation_capture(input));
    EXPECT_FALSE(viewport.update_navigation(input, true, 0.016f));
}

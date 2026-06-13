#include <gtest/gtest.h>

import quader.app.editor_camera;

TEST(EditorCamera, MovesForwardFromNavigationInput)
{
    quader::EditorCameraNavigationInput navigation_input;
    quader::EditorCamera camera;
    const quader::RenderCamera before = camera.render_camera();

    navigation_input.move_forward = true;

    EXPECT_TRUE(camera.update_from_navigation_input(navigation_input, 1.0f));

    const quader::RenderCamera after = camera.render_camera();
    EXPECT_NE(before.position.x, after.position.x);
    EXPECT_NE(before.position.y, after.position.y);
    EXPECT_NE(before.position.z, after.position.z);
}

TEST(EditorCamera, ActiveLookInputChangesYawAndPitch)
{
    quader::EditorCameraNavigationInput navigation_input;
    quader::EditorCamera camera;
    const quader::RenderCamera before = camera.render_camera();

    navigation_input.look_active = true;
    navigation_input.look_delta.x = 10.0f;
    navigation_input.look_delta.y = -5.0f;

    EXPECT_TRUE(camera.update_from_navigation_input(navigation_input, 0.016f));

    const quader::RenderCamera after = camera.render_camera();
    EXPECT_GT(after.yaw_degrees, before.yaw_degrees);
    EXPECT_GT(after.pitch_degrees, before.pitch_degrees);
}

TEST(EditorCamera, ActiveLookInputPreservesMouseLookSignConvention)
{
    quader::EditorCameraNavigationInput navigation_input;
    quader::EditorCamera camera;
    const quader::RenderCamera before = camera.render_camera();

    navigation_input.look_active = true;
    navigation_input.look_delta.x = -10.0f;
    navigation_input.look_delta.y = 5.0f;

    EXPECT_TRUE(camera.update_from_navigation_input(navigation_input, 0.016f));

    const quader::RenderCamera after = camera.render_camera();
    EXPECT_LT(after.yaw_degrees, before.yaw_degrees);
    EXPECT_LT(after.pitch_degrees, before.pitch_degrees);
}

TEST(EditorCamera, InactiveLookInputDoesNotChangeYawOrPitch)
{
    quader::EditorCameraNavigationInput navigation_input;
    quader::EditorCamera camera;
    const quader::RenderCamera before = camera.render_camera();

    navigation_input.look_delta.x = 10.0f;
    navigation_input.look_delta.y = -5.0f;

    EXPECT_FALSE(camera.update_from_navigation_input(navigation_input, 0.016f));

    const quader::RenderCamera after = camera.render_camera();
    EXPECT_EQ(before.yaw_degrees, after.yaw_degrees);
    EXPECT_EQ(before.pitch_degrees, after.pitch_degrees);
}

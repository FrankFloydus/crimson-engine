#include <gtest/gtest.h>

import quader.app.editor_camera;
import quader.platform.input;

TEST(EditorCamera, MovesForwardFromWasdInput)
{
    quader::Input input;
    quader::EditorCamera camera;
    const quader::RenderCamera before = camera.render_camera();

    input.begin_frame();
    input.handle_event(quader::AppKeyEvent {
        .key = quader::AppKey::w,
        .pressed = true,
        .repeat = false,
    });

    EXPECT_TRUE(camera.update_from_input(input, 1.0f));

    const quader::RenderCamera after = camera.render_camera();
    EXPECT_NE(before.position.x, after.position.x);
    EXPECT_NE(before.position.y, after.position.y);
    EXPECT_NE(before.position.z, after.position.z);
}

TEST(EditorCamera, RightMouseDragChangesYawAndPitch)
{
    quader::Input input;
    quader::EditorCamera camera;
    const quader::RenderCamera before = camera.render_camera();

    input.begin_frame();
    input.handle_event(quader::AppMouseButtonEvent {
        .button = quader::AppMouseButton::right,
        .pressed = true,
    });
    input.handle_event(quader::AppMouseMotionEvent {
        .delta_x = 10.0f,
        .delta_y = -5.0f,
    });

    EXPECT_TRUE(camera.update_from_input(input, 0.016f));

    const quader::RenderCamera after = camera.render_camera();
    EXPECT_GT(after.yaw_degrees, before.yaw_degrees);
    EXPECT_GT(after.pitch_degrees, before.pitch_degrees);
}

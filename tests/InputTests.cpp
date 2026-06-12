#include <gtest/gtest.h>

import quader.platform.input;

TEST(Input, TracksKeyPressHoldAndReleaseAcrossFrames)
{
    quader::Input input;

    input.begin_frame();
    input.handle_event(quader::AppKeyEvent {
        .key = quader::AppKey::w,
        .pressed = true,
        .repeat = false,
    });

    EXPECT_TRUE(input.is_key_down(quader::AppKey::w));
    EXPECT_TRUE(input.was_key_pressed(quader::AppKey::w));
    EXPECT_FALSE(input.was_key_released(quader::AppKey::w));

    input.begin_frame();
    EXPECT_TRUE(input.is_key_down(quader::AppKey::w));
    EXPECT_FALSE(input.was_key_pressed(quader::AppKey::w));

    input.handle_event(quader::AppKeyEvent {
        .key = quader::AppKey::w,
        .pressed = false,
        .repeat = false,
    });

    EXPECT_FALSE(input.is_key_down(quader::AppKey::w));
    EXPECT_TRUE(input.was_key_released(quader::AppKey::w));
}

TEST(Input, AccumulatesMouseMotionAndWheelDeltasPerFrame)
{
    quader::Input input;

    input.begin_frame();
    input.handle_event(quader::AppMouseMotionEvent {
        .x = 10.0f,
        .y = 20.0f,
        .delta_x = 2.0f,
        .delta_y = -3.0f,
    });
    input.handle_event(quader::AppMouseMotionEvent {
        .x = 12.0f,
        .y = 18.0f,
        .delta_x = 4.0f,
        .delta_y = 1.0f,
    });
    input.handle_event(quader::AppMouseWheelEvent {
        .horizontal = 1.0f,
        .vertical = -2.0f,
        .x = 12.0f,
        .y = 18.0f,
    });

    EXPECT_FLOAT_EQ(12.0f, input.mouse_position().x);
    EXPECT_FLOAT_EQ(18.0f, input.mouse_position().y);
    EXPECT_FLOAT_EQ(6.0f, input.mouse_delta().x);
    EXPECT_FLOAT_EQ(-2.0f, input.mouse_delta().y);
    EXPECT_FLOAT_EQ(1.0f, input.mouse_wheel_delta().x);
    EXPECT_FLOAT_EQ(-2.0f, input.mouse_wheel_delta().y);

    input.begin_frame();
    EXPECT_FLOAT_EQ(0.0f, input.mouse_delta().x);
    EXPECT_FLOAT_EQ(0.0f, input.mouse_delta().y);
    EXPECT_FLOAT_EQ(0.0f, input.mouse_wheel_delta().x);
    EXPECT_FLOAT_EQ(0.0f, input.mouse_wheel_delta().y);
}

TEST(Input, RequestsCloseForEscapeQuitAndWindowClose)
{
    quader::Input input;

    input.begin_frame();
    input.handle_event(quader::AppKeyEvent {
        .key = quader::AppKey::escape,
        .pressed = true,
        .repeat = false,
    });
    EXPECT_TRUE(input.close_requested());

    input.begin_frame();
    input.handle_event(quader::AppQuitEvent {});
    EXPECT_TRUE(input.close_requested());

    input.begin_frame();
    input.handle_event(quader::AppWindowEvent {
        .type = quader::AppWindowEventType::close_requested,
    });
    EXPECT_TRUE(input.close_requested());
}

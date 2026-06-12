#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <optional>

import quader.render.viewport_gizmos;

namespace {

quader::TransformGizmoRequest request_for(quader::TransformGizmoTool tool)
{
    return quader::TransformGizmoRequest {
        .tool = tool,
        .target = quader::TransformGizmoTarget {
            .entity_id = quader::RenderEntityId { 7 },
            .origin = {},
        },
        .world_scale = 1.0f,
    };
}

void expect_transform_payloads_are_valid(const quader::DebugDrawList& draw)
{
    for (const quader::DebugDrawLine& line : draw.lines()) {
        ASSERT_TRUE(line.pick.has_value());
        EXPECT_TRUE(line.pick->is_valid());
    }

    for (const quader::DebugDrawTriangle& triangle : draw.triangles()) {
        ASSERT_TRUE(triangle.pick.has_value());
        EXPECT_TRUE(triangle.pick->is_valid());
    }

    for (const quader::DebugDrawPickProxy& proxy : draw.pick_proxies()) {
        EXPECT_TRUE(proxy.is_valid());
        EXPECT_EQ(7U, proxy.payload.owner_entity_id);
        if (proxy.payload.role == quader::DebugDrawPickRole::transform_uniform_scale) {
            EXPECT_EQ(quader::DebugDrawAxis::none, proxy.payload.axis);
        } else {
            EXPECT_NE(quader::DebugDrawAxis::none, proxy.payload.axis);
        }
    }
}

} // namespace

TEST(ViewportGizmos, MoveGizmoEmitsFilledConesAndPickLeeway)
{
    quader::DebugDrawFrame frame;
    quader::append_transform_gizmo(request_for(quader::TransformGizmoTool::move), frame);

    EXPECT_TRUE(frame.overlay.lines().empty());
    EXPECT_GT(frame.overlay.triangles().size(), 0U);
    EXPECT_EQ(9U, frame.overlay.pick_proxies().size());
    expect_transform_payloads_are_valid(frame.overlay);
}

TEST(ViewportGizmos, DefaultMoveAndScaleStyleUsesSlimGodotLikeProportions)
{
    const quader::TransformGizmoStyle style = quader::default_transform_gizmo_style();

    EXPECT_LT(style.shaft_half_extent / style.axis_length, 0.007f);
    EXPECT_LT(style.cone_radius / style.axis_length, 0.04f);
    EXPECT_LT(style.cube_half_extent / style.axis_length, 0.045f);
    EXPECT_GT(style.pick_radius / style.axis_length, 0.075f);
}

TEST(ViewportGizmos, RotateGizmoEmitsFilledRingsAndRingPickProxies)
{
    quader::DebugDrawFrame frame;
    quader::append_transform_gizmo(request_for(quader::TransformGizmoTool::rotate), frame);

    EXPECT_TRUE(frame.overlay.lines().empty());
    EXPECT_GT(frame.overlay.triangles().size(), 0U);
    EXPECT_EQ(3U, frame.overlay.pick_proxies().size());
    for (const quader::DebugDrawPickProxy& proxy : frame.overlay.pick_proxies()) {
        EXPECT_EQ(quader::DebugDrawPickProxyKind::ring, proxy.kind);
    }
    expect_transform_payloads_are_valid(frame.overlay);
}

TEST(ViewportGizmos, RotateGizmoRingsHaveDepthForEdgeOnVisibility)
{
    quader::DebugDrawFrame frame;
    quader::append_transform_gizmo(request_for(quader::TransformGizmoTool::rotate), frame);

    bool saw_front_depth = false;
    bool saw_back_depth = false;
    for (const quader::DebugDrawTriangle& triangle : frame.overlay.triangles()) {
        if (!triangle.pick.has_value() || triangle.pick->role != quader::DebugDrawPickRole::transform_rotate_axis ||
            triangle.pick->axis != quader::DebugDrawAxis::z) {
            continue;
        }

        saw_front_depth |= triangle.a.position.z > 0.001f || triangle.b.position.z > 0.001f ||
            triangle.c.position.z > 0.001f;
        saw_back_depth |= triangle.a.position.z < -0.001f || triangle.b.position.z < -0.001f ||
            triangle.c.position.z < -0.001f;
    }

    EXPECT_TRUE(saw_front_depth);
    EXPECT_TRUE(saw_back_depth);
}

TEST(ViewportGizmos, ScaleGizmoEmitsFilledCubesAndPickLeeway)
{
    quader::DebugDrawFrame frame;
    quader::append_transform_gizmo(request_for(quader::TransformGizmoTool::scale), frame);

    EXPECT_TRUE(frame.overlay.lines().empty());
    EXPECT_GT(frame.overlay.triangles().size(), 36U);
    EXPECT_EQ(10U, frame.overlay.pick_proxies().size());
    bool has_uniform_scale = false;
    for (const quader::DebugDrawPickProxy& proxy : frame.overlay.pick_proxies()) {
        has_uniform_scale |= proxy.payload.role == quader::DebugDrawPickRole::transform_uniform_scale;
    }
    EXPECT_TRUE(has_uniform_scale);
    expect_transform_payloads_are_valid(frame.overlay);
}

TEST(ViewportGizmos, UniformScalePickProxyWinsCenterOverlap)
{
    quader::DebugDrawFrame frame;
    quader::append_transform_gizmo(request_for(quader::TransformGizmoTool::scale), frame);

    const quader::DebugDrawPickProxy* uniform_scale_proxy = nullptr;
    uint8_t highest_competing_scale_priority = 0;
    for (const quader::DebugDrawPickProxy& proxy : frame.overlay.pick_proxies()) {
        if (proxy.payload.role == quader::DebugDrawPickRole::transform_uniform_scale) {
            uniform_scale_proxy = &proxy;
            continue;
        }

        if (proxy.payload.role == quader::DebugDrawPickRole::transform_scale_axis ||
            proxy.payload.role == quader::DebugDrawPickRole::transform_scale_plane) {
            highest_competing_scale_priority = std::max(highest_competing_scale_priority, proxy.priority);
        }
    }

    ASSERT_NE(nullptr, uniform_scale_proxy);
    EXPECT_EQ(quader::DebugDrawPickProxyKind::oriented_box, uniform_scale_proxy->kind);
    EXPECT_GT(
        static_cast<int>(uniform_scale_proxy->priority),
        static_cast<int>(highest_competing_scale_priority));
}

TEST(ViewportGizmos, HighlightedPayloadBrightensMatchingAxis)
{
    quader::TransformGizmoRequest request = request_for(quader::TransformGizmoTool::move);
    request.highlighted_payload = quader::transform_gizmo_pick_payload(
        quader::RenderEntityId { 7 },
        quader::DebugDrawPickRole::transform_move_axis,
        quader::DebugDrawAxis::x);

    quader::DebugDrawFrame frame;
    quader::append_transform_gizmo(request, frame);

    const quader::DebugDrawTriangle* highlighted = nullptr;
    for (const quader::DebugDrawTriangle& triangle : frame.overlay.triangles()) {
        if (!triangle.pick.has_value()) {
            continue;
        }
        if (*triangle.pick == *request.highlighted_payload) {
            highlighted = &triangle;
        }
    }

    ASSERT_NE(nullptr, highlighted);
    EXPECT_FLOAT_EQ(1.0f, highlighted->a.color.r);
    EXPECT_GT(highlighted->a.color.g, 0.5f);
    EXPECT_GT(highlighted->a.color.b, 0.5f);
}

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

import quader.render.viewport_gizmos;
import quader.render.debug_draw_picking;

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

    for (const quader::DebugDrawPoint& point : draw.points()) {
        ASSERT_TRUE(point.pick.has_value());
        EXPECT_TRUE(point.pick->is_valid());
        EXPECT_TRUE(point.is_valid());
    }

    for (const quader::DebugDrawWireSphere& sphere : draw.wire_spheres()) {
        ASSERT_TRUE(sphere.pick.has_value());
        EXPECT_TRUE(sphere.pick->is_valid());
        EXPECT_TRUE(sphere.is_valid());
    }

    for (const quader::DebugDrawWireCylinder& cylinder : draw.wire_cylinders()) {
        ASSERT_TRUE(cylinder.pick.has_value());
        EXPECT_TRUE(cylinder.pick->is_valid());
        EXPECT_TRUE(cylinder.is_valid());
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

TEST(ViewportGizmos, DebugDrawWireSpherePreservesPickPayload)
{
    const quader::DebugDrawPickPayload pick = quader::point_light_gizmo_pick_payload(quader::RenderEntityId { 7 });
    quader::DebugDrawFrame frame;

    frame.depth_tested.add_wire_sphere(
        quader::Vec3 { 1.0f, 2.0f, 3.0f },
        0.5f,
        quader::LinearRgba { 0.1f, 0.2f, 0.3f, 1.0f },
        pick,
        16);

    ASSERT_EQ(1U, frame.depth_tested.wire_spheres().size());
    const quader::DebugDrawWireSphere& sphere = frame.depth_tested.wire_spheres()[0];
    EXPECT_TRUE(sphere.is_valid());
    ASSERT_TRUE(sphere.pick.has_value());
    EXPECT_EQ(pick, *sphere.pick);
    EXPECT_FLOAT_EQ(0.5f, sphere.radius);
    EXPECT_EQ(16U, sphere.segments);
}

TEST(ViewportGizmos, DebugDrawPointPreservesPickPayload)
{
    const quader::DebugDrawPickPayload pick = quader::point_light_gizmo_pick_payload(quader::RenderEntityId { 7 });
    quader::DebugDrawFrame frame;

    frame.overlay.add_point(
        quader::Vec3 { 1.0f, 2.0f, 3.0f },
        0.25f,
        quader::LinearRgba { 0.1f, 0.2f, 0.3f, 1.0f },
        pick);

    ASSERT_EQ(1U, frame.overlay.points().size());
    const quader::DebugDrawPoint& point = frame.overlay.points()[0];
    EXPECT_TRUE(point.is_valid());
    ASSERT_TRUE(point.pick.has_value());
    EXPECT_EQ(pick, *point.pick);
    EXPECT_FLOAT_EQ(0.25f, point.radius);
}

TEST(ViewportGizmos, DebugDrawWireCylinderPreservesPickPayload)
{
    const quader::DebugDrawPickPayload pick = quader::point_light_gizmo_pick_payload(quader::RenderEntityId { 7 });
    quader::DebugDrawFrame frame;

    frame.depth_tested.add_wire_cylinder(
        quader::Vec3 { 0.0f, 0.0f, 0.0f },
        quader::Vec3 { 0.0f, 2.0f, 0.0f },
        0.4f,
        quader::LinearRgba { 0.3f, 0.2f, 0.1f, 1.0f },
        pick,
        12);

    ASSERT_EQ(1U, frame.depth_tested.wire_cylinders().size());
    const quader::DebugDrawWireCylinder& cylinder = frame.depth_tested.wire_cylinders()[0];
    EXPECT_TRUE(cylinder.is_valid());
    ASSERT_TRUE(cylinder.pick.has_value());
    EXPECT_EQ(pick, *cylinder.pick);
    EXPECT_FLOAT_EQ(0.4f, cylinder.radius);
    EXPECT_EQ(12U, cylinder.segments);
}

TEST(ViewportGizmos, DebugDrawPickingReturnsNearestValidProxy)
{
    const quader::DebugDrawPickPayload far_pick = quader::point_light_gizmo_pick_payload(quader::RenderEntityId { 7 });
    const quader::DebugDrawPickPayload near_pick =
        quader::directional_light_gizmo_pick_payload(quader::RenderEntityId { 8 });
    quader::DebugDrawFrame frame;

    frame.depth_tested.add_pick_sphere(far_pick, quader::Vec3 { 0.0f, 0.0f, 8.0f }, 1.0f);
    frame.depth_tested.add_pick_sphere(near_pick, quader::Vec3 { 0.0f, 0.0f, 4.0f }, 1.0f);
    frame.depth_tested.add_pick_sphere(
        quader::DebugDrawPickPayload {},
        quader::Vec3 { 0.0f, 0.0f, 1.0f },
        1.0f);

    const std::optional<quader::DebugDrawPickHit> hit = quader::pick_debug_draw_frame(
        frame,
        quader::DebugDrawPickRay {
            .origin = quader::Vec3 { 0.0f, 0.0f, 0.0f },
            .direction = quader::Vec3 { 0.0f, 0.0f, 1.0f },
        });

    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(near_pick, hit->payload);
    EXPECT_FLOAT_EQ(3.0f, hit->distance);
}

TEST(ViewportGizmos, DebugDrawPickingPrefersHigherPriorityOverDistance)
{
    const quader::DebugDrawPickPayload low_priority_pick =
        quader::point_light_gizmo_pick_payload(quader::RenderEntityId { 7 });
    const quader::DebugDrawPickPayload high_priority_pick =
        quader::directional_light_gizmo_pick_payload(quader::RenderEntityId { 8 });
    quader::DebugDrawFrame frame;

    frame.depth_tested.add_pick_sphere(
        low_priority_pick,
        quader::Vec3 { 0.0f, 0.0f, 3.0f },
        1.0f,
        1);
    frame.overlay.add_pick_sphere(
        high_priority_pick,
        quader::Vec3 { 0.0f, 0.0f, 8.0f },
        1.0f,
        9);

    const std::optional<quader::DebugDrawPickHit> hit = quader::pick_debug_draw_frame(
        frame,
        quader::DebugDrawPickRay {
            .origin = quader::Vec3 { 0.0f, 0.0f, 0.0f },
            .direction = quader::Vec3 { 0.0f, 0.0f, 1.0f },
        });

    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(high_priority_pick, hit->payload);
    EXPECT_EQ(9U, hit->priority);
}

TEST(ViewportGizmos, DebugDrawBillboardPreservesPickPayloadAndCameraBasis)
{
    const quader::DebugDrawPickPayload pick = quader::point_light_gizmo_pick_payload(quader::RenderEntityId { 7 });
    quader::DebugDrawFrame frame;

    frame.overlay.add_billboard(
        quader::Vec3 { 1.0f, 2.0f, 3.0f },
        quader::Vec3 { 1.0f, 0.0f, 0.0f },
        quader::Vec3 { 0.0f, 1.0f, 0.0f },
        0.5f,
        0.75f,
        quader::LinearRgba { 0.1f, 0.2f, 0.3f, 1.0f },
        pick);

    ASSERT_EQ(1U, frame.overlay.billboards().size());
    const quader::DebugDrawBillboard& billboard = frame.overlay.billboards()[0];
    EXPECT_TRUE(billboard.is_valid());
    ASSERT_TRUE(billboard.pick.has_value());
    EXPECT_EQ(pick, *billboard.pick);
    EXPECT_FLOAT_EQ(0.5f, billboard.width);
    EXPECT_FLOAT_EQ(0.75f, billboard.height);
    EXPECT_FLOAT_EQ(1.0f, billboard.camera_right.x);
    EXPECT_FLOAT_EQ(1.0f, billboard.camera_up.y);
}

TEST(ViewportGizmos, DebugDrawRibbonPreservesPointColorsWidthAndPickPayload)
{
    const quader::DebugDrawPickPayload pick = quader::point_light_gizmo_pick_payload(quader::RenderEntityId { 7 });
    quader::DebugDrawFrame frame;
    std::vector<quader::DebugDrawVertex> points {
        quader::DebugDrawVertex {
            .position = { 0.0f, 0.0f, 0.0f },
            .color = { 1.0f, 0.0f, 0.0f, 1.0f },
        },
        quader::DebugDrawVertex {
            .position = { 1.0f, 0.0f, 0.0f },
            .color = { 0.0f, 1.0f, 0.0f, 1.0f },
        },
        quader::DebugDrawVertex {
            .position = { 1.0f, 1.0f, 0.0f },
            .color = { 0.0f, 0.0f, 1.0f, 1.0f },
        },
    };

    frame.depth_tested.add_ribbon(
        std::move(points),
        0.125f,
        quader::Vec3 { 0.0f, 1.0f, 0.0f },
        pick);

    ASSERT_EQ(1U, frame.depth_tested.ribbons().size());
    const quader::DebugDrawRibbon& ribbon = frame.depth_tested.ribbons()[0];
    EXPECT_TRUE(ribbon.is_valid());
    ASSERT_TRUE(ribbon.pick.has_value());
    EXPECT_EQ(pick, *ribbon.pick);
    ASSERT_EQ(3U, ribbon.points.size());
    EXPECT_FLOAT_EQ(0.125f, ribbon.width);
    EXPECT_FLOAT_EQ(1.0f, ribbon.points[0].color.r);
    EXPECT_FLOAT_EQ(1.0f, ribbon.points[1].color.g);
    EXPECT_FLOAT_EQ(1.0f, ribbon.points[2].color.b);
}

TEST(ViewportGizmos, DebugDrawPersistentTracePreservesIdentitySamplesAndPickPayload)
{
    const quader::DebugDrawPickPayload pick = quader::point_light_gizmo_pick_payload(quader::RenderEntityId { 7 });
    quader::DebugDrawFrame frame;
    std::vector<quader::DebugDrawVertex> samples {
        quader::DebugDrawVertex {
            .position = { 0.0f, 0.0f, 0.0f },
            .color = { 1.0f, 1.0f, 0.0f, 1.0f },
        },
        quader::DebugDrawVertex {
            .position = { 0.0f, 0.0f, 1.0f },
            .color = { 1.0f, 0.5f, 0.0f, 1.0f },
        },
    };

    frame.overlay.add_persistent_trace(
        42U,
        9U,
        std::move(samples),
        0.0625f,
        quader::Vec3 { 0.0f, 1.0f, 0.0f },
        pick);

    ASSERT_EQ(1U, frame.overlay.persistent_traces().size());
    const quader::DebugDrawPersistentTrace& trace = frame.overlay.persistent_traces()[0];
    EXPECT_TRUE(trace.is_valid());
    EXPECT_EQ(42U, trace.trace_id);
    EXPECT_EQ(9U, trace.revision);
    ASSERT_TRUE(trace.pick.has_value());
    EXPECT_EQ(pick, *trace.pick);
    ASSERT_EQ(2U, trace.samples.size());
    EXPECT_FLOAT_EQ(0.0625f, trace.width);
    EXPECT_FLOAT_EQ(1.0f, trace.samples[1].position.z);
}

TEST(ViewportGizmos, DebugDrawClearAndEmptyIncludeBillboardsRibbonsAndPersistentTraces)
{
    quader::DebugDrawFrame frame;

    frame.overlay.reserve_billboards(1);
    frame.overlay.reserve_ribbons(1);
    frame.overlay.reserve_persistent_traces(1);
    frame.overlay.add_billboard(
        quader::Vec3 {},
        quader::Vec3 { 1.0f, 0.0f, 0.0f },
        quader::Vec3 { 0.0f, 1.0f, 0.0f },
        0.5f,
        0.5f,
        quader::LinearRgba {});
    frame.overlay.add_ribbon(
        std::vector<quader::DebugDrawVertex> {
            quader::DebugDrawVertex { .position = { 0.0f, 0.0f, 0.0f }, .color = {} },
            quader::DebugDrawVertex { .position = { 1.0f, 0.0f, 0.0f }, .color = {} },
        },
        0.1f);
    frame.overlay.add_persistent_trace(
        1U,
        1U,
        std::vector<quader::DebugDrawVertex> {
            quader::DebugDrawVertex { .position = { 0.0f, 0.0f, 0.0f }, .color = {} },
            quader::DebugDrawVertex { .position = { 0.0f, 0.0f, 1.0f }, .color = {} },
        },
        0.1f);

    EXPECT_FALSE(frame.empty());
    frame.clear();
    EXPECT_TRUE(frame.empty());
    EXPECT_TRUE(frame.overlay.billboards().empty());
    EXPECT_TRUE(frame.overlay.ribbons().empty());
    EXPECT_TRUE(frame.overlay.persistent_traces().empty());
}

TEST(ViewportGizmos, PointLightGizmoEmitsWireSphereAndPickProxy)
{
    quader::SceneRenderSnapshot snapshot;
    snapshot.point_lights.push_back(quader::SceneRenderPointLight {
        .entity_id = quader::RenderEntityId { 7 },
        .position = { 1.0f, 2.0f, 3.0f },
    });

    quader::DebugDrawFrame frame;
    quader::append_light_gizmos(snapshot, frame);

    ASSERT_EQ(1U, frame.depth_tested.wire_spheres().size());
    const quader::DebugDrawWireSphere& sphere = frame.depth_tested.wire_spheres()[0];
    EXPECT_TRUE(sphere.is_valid());
    ASSERT_TRUE(sphere.pick.has_value());
    EXPECT_EQ(quader::DebugDrawPickRole::point_light_gizmo, sphere.pick->role);
    EXPECT_EQ(7U, sphere.pick->owner_entity_id);
    ASSERT_EQ(1U, frame.depth_tested.pick_proxies().size());
    EXPECT_EQ(quader::DebugDrawPickProxyKind::sphere, frame.depth_tested.pick_proxies()[0].kind);
    EXPECT_EQ(*sphere.pick, frame.depth_tested.pick_proxies()[0].payload);
}

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

TEST(ViewportGizmos, ScaleGizmoEmitsNoHandlesWhenTargetCannotScale)
{
    quader::TransformGizmoRequest request = request_for(quader::TransformGizmoTool::scale);
    ASSERT_TRUE(request.target.has_value());
    request.target->can_scale = false;

    quader::DebugDrawFrame frame;
    quader::append_transform_gizmo(request, frame);

    EXPECT_TRUE(frame.overlay.lines().empty());
    EXPECT_TRUE(frame.overlay.triangles().empty());
    EXPECT_TRUE(frame.overlay.pick_proxies().empty());
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

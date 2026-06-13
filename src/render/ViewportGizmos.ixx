module;

#include <cstdint>
#include <optional>

export module quader.render.viewport_gizmos;

export import quader.render.debug_draw;
export import quader.render.viewport_types;

export namespace quader {

struct LightGizmoStyle {
    float directional_arrow_length = 1.0f;
    float directional_arrow_head_length = 0.22f;
    float directional_arrow_head_width = 0.12f;
    float directional_pick_radius = 0.12f;
    float point_radius = 0.24f;
    float point_pick_radius = 0.34f;
    uint32_t point_ring_segments = 32;
};

enum class TransformGizmoTool : uint8_t {
    move,
    rotate,
    scale,
};

enum class TransformGizmoSpace : uint8_t {
    global,
    local,
};

struct TransformGizmoTarget {
    RenderEntityId entity_id;
    Vec3 origin;
    Vec3 x_axis { 1.0f, 0.0f, 0.0f };
    Vec3 y_axis { 0.0f, 1.0f, 0.0f };
    Vec3 z_axis { 0.0f, 0.0f, 1.0f };
    bool can_scale = true;
};

struct TransformGizmoRequest {
    TransformGizmoTool tool = TransformGizmoTool::move;
    TransformGizmoSpace space = TransformGizmoSpace::global;
    std::optional<TransformGizmoTarget> target;
    std::optional<DebugDrawPickPayload> highlighted_payload;
    float world_scale = 1.0f;
};

struct TransformGizmoStyle {
    float axis_length = 1.45f;
    float shaft_half_extent = 0.009f;
    float cone_length = 0.29f;
    float cone_radius = 0.054f;
    float cube_half_extent = 0.062f;
    float center_cube_half_extent = 0.052f;
    float plane_size = 0.18f;
    float plane_offset = 0.34f;
    float plane_pick_thickness = 0.075f;
    float ring_radius = 0.9f;
    float ring_visual_thickness = 0.024f;
    float pick_radius = 0.13f;
    float ring_pick_thickness = 0.16f;
    uint32_t cone_segments = 24;
    uint32_t ring_segments = 128;
    uint8_t pick_priority = 90;
};

[[nodiscard]] DebugDrawPickPayload directional_light_gizmo_pick_payload(RenderEntityId entity_id) noexcept;
[[nodiscard]] DebugDrawPickPayload point_light_gizmo_pick_payload(RenderEntityId entity_id) noexcept;
[[nodiscard]] DebugDrawPickPayload transform_gizmo_pick_payload(
    RenderEntityId entity_id,
    DebugDrawPickRole role,
    DebugDrawAxis axis) noexcept;
[[nodiscard]] LightGizmoStyle default_light_gizmo_style() noexcept;
[[nodiscard]] TransformGizmoStyle default_transform_gizmo_style() noexcept;
[[nodiscard]] bool transform_gizmo_target_is_valid(const TransformGizmoTarget& target) noexcept;
void append_light_gizmos(
    const SceneRenderSnapshot& snapshot,
    DebugDrawFrame& debug_draw,
    LightGizmoStyle style = default_light_gizmo_style());
void append_transform_gizmo(
    const TransformGizmoRequest& request,
    DebugDrawFrame& debug_draw,
    TransformGizmoStyle style = default_transform_gizmo_style());

} // namespace quader

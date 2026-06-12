module;

#include <cstdint>

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

[[nodiscard]] DebugDrawPickPayload directional_light_gizmo_pick_payload(RenderEntityId entity_id) noexcept;
[[nodiscard]] DebugDrawPickPayload point_light_gizmo_pick_payload(RenderEntityId entity_id) noexcept;
[[nodiscard]] LightGizmoStyle default_light_gizmo_style() noexcept;
void append_light_gizmos(
    const SceneRenderSnapshot& snapshot,
    DebugDrawFrame& debug_draw,
    LightGizmoStyle style = default_light_gizmo_style());

} // namespace quader

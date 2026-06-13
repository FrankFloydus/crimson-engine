module;

#include <cstdint>
#include <optional>

export module quader.render.debug_draw_picking;

export import quader.render.debug_draw;
export import quader.render.types;

export namespace quader {

struct DebugDrawPickRay {
    Vec3 origin;
    Vec3 direction;
};

struct DebugDrawPickHit {
    DebugDrawPickPayload payload;
    float distance = 0.0f;
    uint8_t priority = 0;
};

[[nodiscard]] std::optional<DebugDrawPickHit> pick_debug_draw_list(
    const DebugDrawList& draw,
    const DebugDrawPickRay& ray);

[[nodiscard]] std::optional<DebugDrawPickHit> pick_debug_draw_frame(
    const DebugDrawFrame& frame,
    const DebugDrawPickRay& ray);

} // namespace quader

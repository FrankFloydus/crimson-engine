module;

#include <cstdint>
#include <cstddef>
#include <optional>
#include <vector>

export module quader.render.debug_draw;

export import quader.render.types;

export namespace quader {

enum class DebugDrawPickRole : uint8_t {
    none,
    directional_light_gizmo,
    point_light_gizmo,
};

enum class DebugDrawPickProxyKind : uint8_t {
    sphere,
    capsule,
};

struct DebugDrawPickPayload {
    uint32_t owner_entity_id = 0;
    DebugDrawPickRole role = DebugDrawPickRole::none;

    [[nodiscard]] bool is_valid() const noexcept
    {
        return owner_entity_id != 0 && role != DebugDrawPickRole::none;
    }
};

struct DebugDrawVertex {
    Vec3 position;
    LinearRgba color;
};

struct DebugDrawLine {
    DebugDrawVertex a;
    DebugDrawVertex b;
    std::optional<DebugDrawPickPayload> pick;
};

struct DebugDrawPickProxy {
    DebugDrawPickPayload payload;
    DebugDrawPickProxyKind kind = DebugDrawPickProxyKind::sphere;
    Vec3 center;
    Vec3 segment_a;
    Vec3 segment_b;
    float radius = 0.0f;
    uint8_t priority = 0;

    [[nodiscard]] bool is_valid() const noexcept
    {
        return payload.is_valid() && radius > 0.0f;
    }
};

class DebugDrawList {
public:
    void clear_keep_capacity()
    {
        lines_.clear();
        pick_proxies_.clear();
    }

    void reserve_lines(std::size_t count)
    {
        lines_.reserve(count);
    }

    void reserve_pick_proxies(std::size_t count)
    {
        pick_proxies_.reserve(count);
    }

    void add_line(
        Vec3 start,
        Vec3 end,
        LinearRgba color,
        std::optional<DebugDrawPickPayload> pick = std::nullopt)
    {
        lines_.push_back(DebugDrawLine {
            .a = DebugDrawVertex { .position = start, .color = color },
            .b = DebugDrawVertex { .position = end, .color = color },
            .pick = pick,
        });
    }

    void add_pick_sphere(DebugDrawPickPayload payload, Vec3 center, float radius, uint8_t priority = 0)
    {
        pick_proxies_.push_back(DebugDrawPickProxy {
            .payload = payload,
            .kind = DebugDrawPickProxyKind::sphere,
            .center = center,
            .radius = radius,
            .priority = priority,
        });
    }

    void add_pick_capsule(
        DebugDrawPickPayload payload,
        Vec3 start,
        Vec3 end,
        float radius,
        uint8_t priority = 0)
    {
        pick_proxies_.push_back(DebugDrawPickProxy {
            .payload = payload,
            .kind = DebugDrawPickProxyKind::capsule,
            .segment_a = start,
            .segment_b = end,
            .radius = radius,
            .priority = priority,
        });
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return lines_.empty() && pick_proxies_.empty();
    }

    [[nodiscard]] const std::vector<DebugDrawLine>& lines() const noexcept
    {
        return lines_;
    }

    [[nodiscard]] const std::vector<DebugDrawPickProxy>& pick_proxies() const noexcept
    {
        return pick_proxies_;
    }

private:
    std::vector<DebugDrawLine> lines_;
    std::vector<DebugDrawPickProxy> pick_proxies_;
};

struct DebugDrawFrame {
    DebugDrawList depth_tested;
    DebugDrawList overlay;

    void clear_keep_capacity()
    {
        depth_tested.clear_keep_capacity();
        overlay.clear_keep_capacity();
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return depth_tested.empty() && overlay.empty();
    }
};

} // namespace quader

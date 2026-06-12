module;

#include <cstdint>
#include <cstddef>
#include <optional>
#include <vector>

export module quader.render.debug_draw;

export import quader.render.types;

export namespace quader {

enum class DebugDrawAxis : uint8_t {
    none,
    x,
    y,
    z,
};

enum class DebugDrawPickRole : uint8_t {
    none,
    directional_light_gizmo,
    point_light_gizmo,
    transform_move_axis,
    transform_move_plane,
    transform_rotate_axis,
    transform_scale_axis,
    transform_scale_plane,
    transform_uniform_scale,
};

enum class DebugDrawPickProxyKind : uint8_t {
    sphere,
    capsule,
    oriented_box,
    ring,
};

struct DebugDrawPickPayload {
    uint32_t owner_entity_id = 0;
    DebugDrawPickRole role = DebugDrawPickRole::none;
    DebugDrawAxis axis = DebugDrawAxis::none;

    [[nodiscard]] friend bool operator==(const DebugDrawPickPayload&, const DebugDrawPickPayload&) = default;

    [[nodiscard]] bool is_valid() const noexcept
    {
        if (owner_entity_id == 0 || role == DebugDrawPickRole::none) {
            return false;
        }

        switch (role) {
        case DebugDrawPickRole::directional_light_gizmo:
        case DebugDrawPickRole::point_light_gizmo:
            return axis == DebugDrawAxis::none;
        case DebugDrawPickRole::transform_move_axis:
        case DebugDrawPickRole::transform_move_plane:
        case DebugDrawPickRole::transform_rotate_axis:
        case DebugDrawPickRole::transform_scale_axis:
        case DebugDrawPickRole::transform_scale_plane:
            return axis != DebugDrawAxis::none;
        case DebugDrawPickRole::transform_uniform_scale:
            return axis == DebugDrawAxis::none;
        case DebugDrawPickRole::none:
            break;
        }

        return false;
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

struct DebugDrawTriangle {
    DebugDrawVertex a;
    DebugDrawVertex b;
    DebugDrawVertex c;
    std::optional<DebugDrawPickPayload> pick;
};

struct DebugDrawPickProxy {
    DebugDrawPickPayload payload;
    DebugDrawPickProxyKind kind = DebugDrawPickProxyKind::sphere;
    Vec3 center;
    Vec3 segment_a;
    Vec3 segment_b;
    Vec3 axis_x { 1.0f, 0.0f, 0.0f };
    Vec3 axis_y { 0.0f, 1.0f, 0.0f };
    Vec3 axis_z { 0.0f, 0.0f, 1.0f };
    Vec3 half_extents;
    Vec3 normal { 0.0f, 1.0f, 0.0f };
    float radius = 0.0f;
    float thickness = 0.0f;
    uint8_t priority = 0;

    [[nodiscard]] bool is_valid() const noexcept
    {
        if (!payload.is_valid()) {
            return false;
        }

        switch (kind) {
        case DebugDrawPickProxyKind::sphere:
        case DebugDrawPickProxyKind::capsule:
            return radius > 0.0f;
        case DebugDrawPickProxyKind::oriented_box:
            return half_extents.x > 0.0f && half_extents.y > 0.0f && half_extents.z > 0.0f;
        case DebugDrawPickProxyKind::ring:
            return radius > 0.0f && thickness > 0.0f;
        }

        return false;
    }
};

class DebugDrawList {
public:
    void clear_keep_capacity()
    {
        lines_.clear();
        triangles_.clear();
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

    void reserve_triangles(std::size_t count)
    {
        triangles_.reserve(count);
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

    void add_triangle(
        DebugDrawVertex a,
        DebugDrawVertex b,
        DebugDrawVertex c,
        std::optional<DebugDrawPickPayload> pick = std::nullopt)
    {
        triangles_.push_back(DebugDrawTriangle {
            .a = a,
            .b = b,
            .c = c,
            .pick = pick,
        });
    }

    void add_triangle(
        Vec3 a,
        Vec3 b,
        Vec3 c,
        LinearRgba color,
        std::optional<DebugDrawPickPayload> pick = std::nullopt)
    {
        add_triangle(
            DebugDrawVertex { .position = a, .color = color },
            DebugDrawVertex { .position = b, .color = color },
            DebugDrawVertex { .position = c, .color = color },
            pick);
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

    void add_pick_oriented_box(
        DebugDrawPickPayload payload,
        Vec3 center,
        Vec3 axis_x,
        Vec3 axis_y,
        Vec3 axis_z,
        Vec3 half_extents,
        uint8_t priority = 0)
    {
        pick_proxies_.push_back(DebugDrawPickProxy {
            .payload = payload,
            .kind = DebugDrawPickProxyKind::oriented_box,
            .center = center,
            .axis_x = axis_x,
            .axis_y = axis_y,
            .axis_z = axis_z,
            .half_extents = half_extents,
            .priority = priority,
        });
    }

    void add_pick_ring(
        DebugDrawPickPayload payload,
        Vec3 center,
        Vec3 normal,
        float radius,
        float thickness,
        uint8_t priority = 0)
    {
        pick_proxies_.push_back(DebugDrawPickProxy {
            .payload = payload,
            .kind = DebugDrawPickProxyKind::ring,
            .center = center,
            .normal = normal,
            .radius = radius,
            .thickness = thickness,
            .priority = priority,
        });
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return lines_.empty() && triangles_.empty() && pick_proxies_.empty();
    }

    [[nodiscard]] const std::vector<DebugDrawLine>& lines() const noexcept
    {
        return lines_;
    }

    [[nodiscard]] const std::vector<DebugDrawTriangle>& triangles() const noexcept
    {
        return triangles_;
    }

    [[nodiscard]] const std::vector<DebugDrawPickProxy>& pick_proxies() const noexcept
    {
        return pick_proxies_;
    }

private:
    std::vector<DebugDrawLine> lines_;
    std::vector<DebugDrawTriangle> triangles_;
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

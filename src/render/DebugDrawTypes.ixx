module;

#include <cstdint>
#include <cstddef>
#include <optional>
#include <utility>
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

struct DebugDrawPoint {
    Vec3 position;
    float radius = 0.04f;
    LinearRgba color;
    std::optional<DebugDrawPickPayload> pick;

    [[nodiscard]] bool is_valid() const noexcept
    {
        return radius > 0.0f;
    }
};

struct DebugDrawWireSphere {
    Vec3 center;
    float radius = 0.0f;
    LinearRgba color;
    uint32_t segments = 32;
    std::optional<DebugDrawPickPayload> pick;

    [[nodiscard]] bool is_valid() const noexcept
    {
        return radius > 0.0f;
    }
};

struct DebugDrawWireCylinder {
    Vec3 start;
    Vec3 end;
    float radius = 0.0f;
    LinearRgba color;
    uint32_t segments = 24;
    std::optional<DebugDrawPickPayload> pick;

    [[nodiscard]] bool is_valid() const noexcept
    {
        return radius > 0.0f && segments >= 6 &&
            (start.x != end.x || start.y != end.y || start.z != end.z);
    }
};

struct DebugDrawBillboard {
    Vec3 center;
    Vec3 camera_right { 1.0f, 0.0f, 0.0f };
    Vec3 camera_up { 0.0f, 1.0f, 0.0f };
    float width = 0.0f;
    float height = 0.0f;
    LinearRgba color;
    std::optional<DebugDrawPickPayload> pick;

    [[nodiscard]] bool is_valid() const noexcept
    {
        return width > 0.0f && height > 0.0f &&
            (camera_right.x != 0.0f || camera_right.y != 0.0f || camera_right.z != 0.0f) &&
            (camera_up.x != 0.0f || camera_up.y != 0.0f || camera_up.z != 0.0f);
    }
};

struct DebugDrawRibbon {
    std::vector<DebugDrawVertex> points;
    float width = 0.0f;
    Vec3 face_normal { 0.0f, 1.0f, 0.0f };
    std::optional<DebugDrawPickPayload> pick;

    [[nodiscard]] bool is_valid() const noexcept
    {
        return width > 0.0f && points.size() >= 2 &&
            (face_normal.x != 0.0f || face_normal.y != 0.0f || face_normal.z != 0.0f);
    }
};

struct DebugDrawPersistentTrace {
    uint64_t trace_id = 0;
    uint64_t revision = 0;
    std::vector<DebugDrawVertex> samples;
    float width = 0.0f;
    Vec3 face_normal { 0.0f, 1.0f, 0.0f };
    std::optional<DebugDrawPickPayload> pick;

    [[nodiscard]] bool is_valid() const noexcept
    {
        return trace_id != 0 && width > 0.0f && samples.size() >= 2 &&
            (face_normal.x != 0.0f || face_normal.y != 0.0f || face_normal.z != 0.0f);
    }
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
    void clear()
    {
        clear_keep_capacity();
    }

    void clear_keep_capacity()
    {
        lines_.clear();
        triangles_.clear();
        points_.clear();
        wire_spheres_.clear();
        wire_cylinders_.clear();
        billboards_.clear();
        ribbons_.clear();
        persistent_traces_.clear();
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

    void reserve_points(std::size_t count)
    {
        points_.reserve(count);
    }

    void reserve_wire_spheres(std::size_t count)
    {
        wire_spheres_.reserve(count);
    }

    void reserve_wire_cylinders(std::size_t count)
    {
        wire_cylinders_.reserve(count);
    }

    void reserve_billboards(std::size_t count)
    {
        billboards_.reserve(count);
    }

    void reserve_ribbons(std::size_t count)
    {
        ribbons_.reserve(count);
    }

    void reserve_persistent_traces(std::size_t count)
    {
        persistent_traces_.reserve(count);
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

    void add_point(
        Vec3 position,
        float radius,
        LinearRgba color,
        std::optional<DebugDrawPickPayload> pick = std::nullopt)
    {
        points_.push_back(DebugDrawPoint {
            .position = position,
            .radius = radius,
            .color = color,
            .pick = pick,
        });
    }

    void add_wire_sphere(
        Vec3 center,
        float radius,
        LinearRgba color,
        std::optional<DebugDrawPickPayload> pick = std::nullopt,
        uint32_t segments = 32)
    {
        wire_spheres_.push_back(DebugDrawWireSphere {
            .center = center,
            .radius = radius,
            .color = color,
            .segments = segments,
            .pick = pick,
        });
    }

    void add_wire_cylinder(
        Vec3 start,
        Vec3 end,
        float radius,
        LinearRgba color,
        std::optional<DebugDrawPickPayload> pick = std::nullopt,
        uint32_t segments = 24)
    {
        wire_cylinders_.push_back(DebugDrawWireCylinder {
            .start = start,
            .end = end,
            .radius = radius,
            .color = color,
            .segments = segments,
            .pick = pick,
        });
    }

    void add_billboard(
        Vec3 center,
        Vec3 camera_right,
        Vec3 camera_up,
        float width,
        float height,
        LinearRgba color,
        std::optional<DebugDrawPickPayload> pick = std::nullopt)
    {
        billboards_.push_back(DebugDrawBillboard {
            .center = center,
            .camera_right = camera_right,
            .camera_up = camera_up,
            .width = width,
            .height = height,
            .color = color,
            .pick = pick,
        });
    }

    void add_ribbon(
        std::vector<DebugDrawVertex> points,
        float width,
        Vec3 face_normal = { 0.0f, 1.0f, 0.0f },
        std::optional<DebugDrawPickPayload> pick = std::nullopt)
    {
        ribbons_.push_back(DebugDrawRibbon {
            .points = std::move(points),
            .width = width,
            .face_normal = face_normal,
            .pick = pick,
        });
    }

    void add_persistent_trace(
        uint64_t trace_id,
        uint64_t revision,
        std::vector<DebugDrawVertex> samples,
        float width,
        Vec3 face_normal = { 0.0f, 1.0f, 0.0f },
        std::optional<DebugDrawPickPayload> pick = std::nullopt)
    {
        persistent_traces_.push_back(DebugDrawPersistentTrace {
            .trace_id = trace_id,
            .revision = revision,
            .samples = std::move(samples),
            .width = width,
            .face_normal = face_normal,
            .pick = pick,
        });
    }

    void add_persistent_trace(DebugDrawPersistentTrace trace)
    {
        persistent_traces_.push_back(std::move(trace));
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
        return lines_.empty() && triangles_.empty() && points_.empty() &&
            wire_spheres_.empty() && wire_cylinders_.empty() && billboards_.empty() &&
            ribbons_.empty() && persistent_traces_.empty() && pick_proxies_.empty();
    }

    [[nodiscard]] const std::vector<DebugDrawLine>& lines() const noexcept
    {
        return lines_;
    }

    [[nodiscard]] const std::vector<DebugDrawTriangle>& triangles() const noexcept
    {
        return triangles_;
    }

    [[nodiscard]] const std::vector<DebugDrawPoint>& points() const noexcept
    {
        return points_;
    }

    [[nodiscard]] const std::vector<DebugDrawWireSphere>& wire_spheres() const noexcept
    {
        return wire_spheres_;
    }

    [[nodiscard]] const std::vector<DebugDrawWireCylinder>& wire_cylinders() const noexcept
    {
        return wire_cylinders_;
    }

    [[nodiscard]] const std::vector<DebugDrawBillboard>& billboards() const noexcept
    {
        return billboards_;
    }

    [[nodiscard]] const std::vector<DebugDrawRibbon>& ribbons() const noexcept
    {
        return ribbons_;
    }

    [[nodiscard]] const std::vector<DebugDrawPersistentTrace>& persistent_traces() const noexcept
    {
        return persistent_traces_;
    }

    [[nodiscard]] const std::vector<DebugDrawPickProxy>& pick_proxies() const noexcept
    {
        return pick_proxies_;
    }

private:
    std::vector<DebugDrawLine> lines_;
    std::vector<DebugDrawTriangle> triangles_;
    std::vector<DebugDrawPoint> points_;
    std::vector<DebugDrawWireSphere> wire_spheres_;
    std::vector<DebugDrawWireCylinder> wire_cylinders_;
    std::vector<DebugDrawBillboard> billboards_;
    std::vector<DebugDrawRibbon> ribbons_;
    std::vector<DebugDrawPersistentTrace> persistent_traces_;
    std::vector<DebugDrawPickProxy> pick_proxies_;
};

struct DebugDrawFrame {
    DebugDrawList depth_tested;
    DebugDrawList overlay;

    void clear()
    {
        clear_keep_capacity();
    }

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

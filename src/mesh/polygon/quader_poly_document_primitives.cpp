////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/document.h>

#include <mesh/polygon/internal/quader_poly_document_constants.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <map>
#include <utility>
#include <vector>

namespace quader_poly {
namespace {

using namespace document_internal;

float clamp_primitive_dimension(float value)
{
    if (!std::isfinite(value)) {
        return 1.0F;
    }
    return std::clamp(value, min_primitive_dimension, max_primitive_dimension);
}

int clamp_primitive_count(int value, int min_value, int max_value)
{
    return std::clamp(value, min_value, max_value);
}

Vec3 spherical_position(float radius, float theta, float phi)
{
    const float sin_phi = std::sin(phi);
    return {
        radius * sin_phi * std::cos(theta),
        radius * std::cos(phi),
        radius * sin_phi * std::sin(theta),
    };
}

Vec3 normalize_to_radius(Vec3 value, float radius)
{
    const Vec3 normal = normalize_or_zero(value);
    if (length_squared(normal) <= epsilon) {
        return { 0.0F, radius, 0.0F };
    }
    return normal * radius;
}

void add_oriented_triangle(Document& document, ElementId a, ElementId b, ElementId c)
{
    const Vertex* vertex_a = find_vertex(document, a);
    const Vertex* vertex_b = find_vertex(document, b);
    const Vertex* vertex_c = find_vertex(document, c);
    if (vertex_a == nullptr || vertex_b == nullptr || vertex_c == nullptr) {
        return;
    }

    const Vec3 normal = cross(vertex_b->position - vertex_a->position, vertex_c->position - vertex_a->position);
    const Vec3 centroid = (vertex_a->position + vertex_b->position + vertex_c->position) / 3.0F;
    if (dot(normal, centroid) >= 0.0F) {
        const std::array vertices { a, b, c };
        [[maybe_unused]] const ElementId face_id = add_face(document, vertices);
    } else {
        const std::array vertices { a, c, b };
        [[maybe_unused]] const ElementId face_id = add_face(document, vertices);
    }
}

ElementId cube_sphere_vertex(
    Document& document,
    std::map<std::array<int, 3>, ElementId>& vertices_by_cube_key,
    const std::array<int, 3>& key,
    int segments,
    float radius)
{
    const auto existing = vertices_by_cube_key.find(key);
    if (existing != vertices_by_cube_key.end()) {
        return existing->second;
    }

    const float scale = 1.0F / static_cast<float>(segments);
    const Vec3 cube_position {
        static_cast<float>(key[0]) * scale,
        static_cast<float>(key[1]) * scale,
        static_cast<float>(key[2]) * scale,
    };
    const ElementId id = add_vertex(document, normalize_to_radius(cube_position, radius));
    vertices_by_cube_key.emplace(key, id);
    return id;
}

void append_cube_sphere_face(
    Document& document,
    std::map<std::array<int, 3>, ElementId>& vertices_by_cube_key,
    int fixed_axis,
    int fixed_value,
    int u_axis,
    int v_axis,
    int segments,
    float radius)
{
    std::vector<ElementId> grid(static_cast<std::size_t>((segments + 1) * (segments + 1)));
    const auto grid_index = [segments](int u, int v) {
        return static_cast<std::size_t>(u * (segments + 1) + v);
    };

    for (int u = 0; u <= segments; ++u) {
        for (int v = 0; v <= segments; ++v) {
            std::array<int, 3> key {};
            key[fixed_axis] = fixed_value;
            key[u_axis] = -segments + (2 * u);
            key[v_axis] = -segments + (2 * v);
            grid[grid_index(u, v)] = cube_sphere_vertex(document, vertices_by_cube_key, key, segments, radius);
        }
    }

    for (int u = 0; u < segments; ++u) {
        for (int v = 0; v < segments; ++v) {
            const std::array vertices {
                grid[grid_index(u, v)],
                grid[grid_index(u + 1, v)],
                grid[grid_index(u + 1, v + 1)],
                grid[grid_index(u, v + 1)],
            };
            [[maybe_unused]] const ElementId face_id = add_face(document, vertices);
        }
    }
}

} // namespace

Document make_cube(const CubePrimitiveSpec& spec)
{
    Document document;
    const float width = clamp_primitive_dimension(spec.width);
    const float height = clamp_primitive_dimension(spec.height);
    const float depth = clamp_primitive_dimension(spec.depth);
    const int width_segments = clamp_primitive_count(spec.width_segments, 1, 128);
    const int height_segments = clamp_primitive_count(spec.height_segments, 1, 128);
    const int depth_segments = clamp_primitive_count(spec.depth_segments, 1, 128);

    std::map<std::array<int, 3>, ElementId> vertices_by_key;
    const auto vertex_for = [&document, &vertices_by_key, width, height, depth, width_segments, height_segments, depth_segments](int x, int y, int z) {
        const std::array key { x, y, z };
        const auto existing = vertices_by_key.find(key);
        if (existing != vertices_by_key.end()) {
            return existing->second;
        }

        const Vec3 position {
            -width * 0.5F + (width * static_cast<float>(x) / static_cast<float>(width_segments)),
            -height * 0.5F + (height * static_cast<float>(y) / static_cast<float>(height_segments)),
            -depth * 0.5F + (depth * static_cast<float>(z) / static_cast<float>(depth_segments)),
        };
        const ElementId id = add_vertex(document, position);
        vertices_by_key.emplace(key, id);
        return id;
    };

    for (int x = 0; x < width_segments; ++x) {
        for (int y = 0; y < height_segments; ++y) {
            const std::array front {
                vertex_for(x, y, depth_segments),
                vertex_for(x + 1, y, depth_segments),
                vertex_for(x + 1, y + 1, depth_segments),
                vertex_for(x, y + 1, depth_segments),
            };
            [[maybe_unused]] const ElementId front_face = add_face(document, front);

            const std::array back {
                vertex_for(x, y, 0),
                vertex_for(x, y + 1, 0),
                vertex_for(x + 1, y + 1, 0),
                vertex_for(x + 1, y, 0),
            };
            [[maybe_unused]] const ElementId back_face = add_face(document, back);
        }
    }

    for (int y = 0; y < height_segments; ++y) {
        for (int z = 0; z < depth_segments; ++z) {
            const std::array right {
                vertex_for(width_segments, y, z),
                vertex_for(width_segments, y + 1, z),
                vertex_for(width_segments, y + 1, z + 1),
                vertex_for(width_segments, y, z + 1),
            };
            [[maybe_unused]] const ElementId right_face = add_face(document, right);

            const std::array left {
                vertex_for(0, y, z),
                vertex_for(0, y, z + 1),
                vertex_for(0, y + 1, z + 1),
                vertex_for(0, y + 1, z),
            };
            [[maybe_unused]] const ElementId left_face = add_face(document, left);
        }
    }

    for (int x = 0; x < width_segments; ++x) {
        for (int z = 0; z < depth_segments; ++z) {
            const std::array top {
                vertex_for(x, height_segments, z),
                vertex_for(x, height_segments, z + 1),
                vertex_for(x + 1, height_segments, z + 1),
                vertex_for(x + 1, height_segments, z),
            };
            [[maybe_unused]] const ElementId top_face = add_face(document, top);

            const std::array bottom {
                vertex_for(x, 0, z),
                vertex_for(x + 1, 0, z),
                vertex_for(x + 1, 0, z + 1),
                vertex_for(x, 0, z + 1),
            };
            [[maybe_unused]] const ElementId bottom_face = add_face(document, bottom);
        }
    }

    return document;
}

Document make_plane(const PlanePrimitiveSpec& spec)
{
    Document document;
    const float width = clamp_primitive_dimension(spec.width);
    const float depth = clamp_primitive_dimension(spec.depth);
    const int width_segments = clamp_primitive_count(spec.width_segments, 1, 128);
    const int depth_segments = clamp_primitive_count(spec.depth_segments, 1, 128);

    std::vector<ElementId> grid(static_cast<std::size_t>((width_segments + 1) * (depth_segments + 1)));
    const auto grid_index = [depth_segments](int x, int z) {
        return static_cast<std::size_t>(x * (depth_segments + 1) + z);
    };

    for (int x = 0; x <= width_segments; ++x) {
        const float px = -width * 0.5F + (width * static_cast<float>(x) / static_cast<float>(width_segments));
        for (int z = 0; z <= depth_segments; ++z) {
            const float pz = -depth * 0.5F + (depth * static_cast<float>(z) / static_cast<float>(depth_segments));
            grid[grid_index(x, z)] = add_vertex(document, { px, 0.0F, pz });
        }
    }

    for (int x = 0; x < width_segments; ++x) {
        for (int z = 0; z < depth_segments; ++z) {
            const std::array vertices {
                grid[grid_index(x, z)],
                grid[grid_index(x, z + 1)],
                grid[grid_index(x + 1, z + 1)],
                grid[grid_index(x + 1, z)],
            };
            [[maybe_unused]] const ElementId face_id = add_face(document, vertices);
        }
    }

    return document;
}

Document make_cylinder(const CylinderPrimitiveSpec& spec)
{
    Document document;
    const float radius = clamp_primitive_dimension(spec.radius);
    const float height = clamp_primitive_dimension(spec.height);
    const int sides = clamp_primitive_count(spec.sides, 3, 128);
    const float half_height = height * 0.5F;

    std::vector<ElementId> bottom_ring;
    std::vector<ElementId> top_ring;
    bottom_ring.reserve(static_cast<std::size_t>(sides));
    top_ring.reserve(static_cast<std::size_t>(sides));

    for (int side = 0; side < sides; ++side) {
        const float theta = 2.0F * pi * static_cast<float>(side) / static_cast<float>(sides);
        const float x = radius * std::cos(theta);
        const float z = radius * std::sin(theta);
        bottom_ring.push_back(add_vertex(document, { x, -half_height, z }));
        top_ring.push_back(add_vertex(document, { x, half_height, z }));
    }

    for (int side = 0; side < sides; ++side) {
        const int next = (side + 1) % sides;
        const std::array vertices {
            bottom_ring[static_cast<std::size_t>(side)],
            top_ring[static_cast<std::size_t>(side)],
            top_ring[static_cast<std::size_t>(next)],
            bottom_ring[static_cast<std::size_t>(next)],
        };
        [[maybe_unused]] const ElementId face_id = add_face(document, vertices);
    }

    if (spec.cap_bottom) {
        [[maybe_unused]] const ElementId face_id = add_face(document, bottom_ring);
    }
    if (spec.cap_top) {
        std::vector<ElementId> top_cap(top_ring.rbegin(), top_ring.rend());
        [[maybe_unused]] const ElementId face_id = add_face(document, top_cap);
    }

    return document;
}

Document make_cone(const ConePrimitiveSpec& spec)
{
    Document document;
    const float radius = clamp_primitive_dimension(spec.radius);
    const float height = clamp_primitive_dimension(spec.height);
    const int sides = clamp_primitive_count(spec.sides, 3, 128);
    const float half_height = height * 0.5F;

    std::vector<ElementId> base_ring;
    base_ring.reserve(static_cast<std::size_t>(sides));
    for (int side = 0; side < sides; ++side) {
        const float theta = 2.0F * pi * static_cast<float>(side) / static_cast<float>(sides);
        base_ring.push_back(add_vertex(document, { radius * std::cos(theta), -half_height, radius * std::sin(theta) }));
    }

    const ElementId apex = add_vertex(document, { 0.0F, half_height, 0.0F });
    for (int side = 0; side < sides; ++side) {
        const int next = (side + 1) % sides;
        const std::array vertices {
            base_ring[static_cast<std::size_t>(side)],
            apex,
            base_ring[static_cast<std::size_t>(next)],
        };
        [[maybe_unused]] const ElementId face_id = add_face(document, vertices);
    }

    if (spec.cap_bottom) {
        [[maybe_unused]] const ElementId face_id = add_face(document, base_ring);
    }

    return document;
}

Document make_quad_sphere(const QuadSpherePrimitiveSpec& spec)
{
    Document document;
    const float radius = clamp_primitive_dimension(spec.radius);
    const int segments = clamp_primitive_count(spec.segments_per_face, 1, 64);
    std::map<std::array<int, 3>, ElementId> vertices_by_cube_key;

    append_cube_sphere_face(document, vertices_by_cube_key, 0, segments, 1, 2, segments, radius);
    append_cube_sphere_face(document, vertices_by_cube_key, 0, -segments, 2, 1, segments, radius);
    append_cube_sphere_face(document, vertices_by_cube_key, 1, segments, 2, 0, segments, radius);
    append_cube_sphere_face(document, vertices_by_cube_key, 1, -segments, 0, 2, segments, radius);
    append_cube_sphere_face(document, vertices_by_cube_key, 2, segments, 0, 1, segments, radius);
    append_cube_sphere_face(document, vertices_by_cube_key, 2, -segments, 1, 0, segments, radius);

    return document;
}

Document make_uv_sphere(const UvSpherePrimitiveSpec& spec)
{
    Document document;
    const float radius = clamp_primitive_dimension(spec.radius);
    const int segments = clamp_primitive_count(spec.segments, 3, 128);
    const int rings = clamp_primitive_count(spec.rings, 2, 64);

    const ElementId top = add_vertex(document, { 0.0F, radius, 0.0F });
    std::vector<std::vector<ElementId>> latitude_rings(static_cast<std::size_t>(rings - 1));
    for (int ring = 1; ring < rings; ++ring) {
        std::vector<ElementId>& ring_vertices = latitude_rings[static_cast<std::size_t>(ring - 1)];
        ring_vertices.reserve(static_cast<std::size_t>(segments));
        const float phi = pi * static_cast<float>(ring) / static_cast<float>(rings);
        for (int segment = 0; segment < segments; ++segment) {
            const float theta = 2.0F * pi * static_cast<float>(segment) / static_cast<float>(segments);
            ring_vertices.push_back(add_vertex(document, spherical_position(radius, theta, phi)));
        }
    }
    const ElementId bottom = add_vertex(document, { 0.0F, -radius, 0.0F });

    const std::vector<ElementId>& first_ring = latitude_rings.front();
    for (int segment = 0; segment < segments; ++segment) {
        const int next = (segment + 1) % segments;
        const std::array vertices {
            top,
            first_ring[static_cast<std::size_t>(next)],
            first_ring[static_cast<std::size_t>(segment)],
        };
        [[maybe_unused]] const ElementId face_id = add_face(document, vertices);
    }

    for (int ring = 0; ring + 1 < static_cast<int>(latitude_rings.size()); ++ring) {
        const std::vector<ElementId>& upper = latitude_rings[static_cast<std::size_t>(ring)];
        const std::vector<ElementId>& lower = latitude_rings[static_cast<std::size_t>(ring + 1)];
        for (int segment = 0; segment < segments; ++segment) {
            const int next = (segment + 1) % segments;
            const std::array vertices {
                upper[static_cast<std::size_t>(segment)],
                upper[static_cast<std::size_t>(next)],
                lower[static_cast<std::size_t>(next)],
                lower[static_cast<std::size_t>(segment)],
            };
            [[maybe_unused]] const ElementId face_id = add_face(document, vertices);
        }
    }

    const std::vector<ElementId>& last_ring = latitude_rings.back();
    for (int segment = 0; segment < segments; ++segment) {
        const int next = (segment + 1) % segments;
        const std::array vertices {
            last_ring[static_cast<std::size_t>(segment)],
            last_ring[static_cast<std::size_t>(next)],
            bottom,
        };
        [[maybe_unused]] const ElementId face_id = add_face(document, vertices);
    }

    return document;
}

Document make_ico_sphere(const IcoSpherePrimitiveSpec& spec)
{
    const float radius = clamp_primitive_dimension(spec.radius);
    const int subdivisions = clamp_primitive_count(spec.subdivisions, 0, 5);
    constexpr float t = 1.6180339887498948482F;

    std::vector<Vec3> positions {
        { -1.0F, t, 0.0F },
        { 1.0F, t, 0.0F },
        { -1.0F, -t, 0.0F },
        { 1.0F, -t, 0.0F },
        { 0.0F, -1.0F, t },
        { 0.0F, 1.0F, t },
        { 0.0F, -1.0F, -t },
        { 0.0F, 1.0F, -t },
        { t, 0.0F, -1.0F },
        { t, 0.0F, 1.0F },
        { -t, 0.0F, -1.0F },
        { -t, 0.0F, 1.0F },
    };
    for (Vec3& position : positions) {
        position = normalize_to_radius(position, 1.0F);
    }

    std::vector<std::array<int, 3>> triangles {
        { 0, 11, 5 },
        { 0, 5, 1 },
        { 0, 1, 7 },
        { 0, 7, 10 },
        { 0, 10, 11 },
        { 1, 5, 9 },
        { 5, 11, 4 },
        { 11, 10, 2 },
        { 10, 7, 6 },
        { 7, 1, 8 },
        { 3, 9, 4 },
        { 3, 4, 2 },
        { 3, 2, 6 },
        { 3, 6, 8 },
        { 3, 8, 9 },
        { 4, 9, 5 },
        { 2, 4, 11 },
        { 6, 2, 10 },
        { 8, 6, 7 },
        { 9, 8, 1 },
    };

    for (int subdivision = 0; subdivision < subdivisions; ++subdivision) {
        std::map<std::pair<int, int>, int> midpoint_indices;
        const auto midpoint_index = [&positions, &midpoint_indices](int a, int b) {
            if (a > b) {
                std::swap(a, b);
            }
            const std::pair key { a, b };
            const auto existing = midpoint_indices.find(key);
            if (existing != midpoint_indices.end()) {
                return existing->second;
            }

            const Vec3 midpoint = normalize_to_radius((positions[static_cast<std::size_t>(a)] + positions[static_cast<std::size_t>(b)]) * 0.5F, 1.0F);
            const int index = static_cast<int>(positions.size());
            positions.push_back(midpoint);
            midpoint_indices.emplace(key, index);
            return index;
        };

        std::vector<std::array<int, 3>> subdivided;
        subdivided.reserve(triangles.size() * 4U);
        for (const std::array<int, 3>& triangle : triangles) {
            const int ab = midpoint_index(triangle[0], triangle[1]);
            const int bc = midpoint_index(triangle[1], triangle[2]);
            const int ca = midpoint_index(triangle[2], triangle[0]);
            subdivided.push_back({ triangle[0], ab, ca });
            subdivided.push_back({ triangle[1], bc, ab });
            subdivided.push_back({ triangle[2], ca, bc });
            subdivided.push_back({ ab, bc, ca });
        }
        triangles = std::move(subdivided);
    }

    Document document;
    std::vector<ElementId> vertex_ids;
    vertex_ids.reserve(positions.size());
    for (const Vec3& position : positions) {
        vertex_ids.push_back(add_vertex(document, position * radius));
    }

    for (const std::array<int, 3>& triangle : triangles) {
        add_oriented_triangle(
            document,
            vertex_ids[static_cast<std::size_t>(triangle[0])],
            vertex_ids[static_cast<std::size_t>(triangle[1])],
            vertex_ids[static_cast<std::size_t>(triangle[2])]);
    }

    return document;
}

Document make_torus(const TorusPrimitiveSpec& spec)
{
    Document document;
    const float major_radius = clamp_primitive_dimension(spec.major_radius);
    const float minor_radius = clamp_primitive_dimension(spec.minor_radius);
    const int major_segments = clamp_primitive_count(spec.major_segments, 3, 128);
    const int minor_segments = clamp_primitive_count(spec.minor_segments, 3, 128);

    std::vector<ElementId> vertices(static_cast<std::size_t>(major_segments * minor_segments));
    const auto vertex_index = [minor_segments](int major, int minor) {
        return static_cast<std::size_t>(major * minor_segments + minor);
    };

    for (int major = 0; major < major_segments; ++major) {
        const float theta = 2.0F * pi * static_cast<float>(major) / static_cast<float>(major_segments);
        const float cos_theta = std::cos(theta);
        const float sin_theta = std::sin(theta);
        for (int minor = 0; minor < minor_segments; ++minor) {
            const float phi = 2.0F * pi * static_cast<float>(minor) / static_cast<float>(minor_segments);
            const float tube_radius = major_radius + (minor_radius * std::cos(phi));
            vertices[vertex_index(major, minor)] = add_vertex(document, {
                tube_radius * cos_theta,
                minor_radius * std::sin(phi),
                tube_radius * sin_theta,
            });
        }
    }

    for (int major = 0; major < major_segments; ++major) {
        const int next_major = (major + 1) % major_segments;
        for (int minor = 0; minor < minor_segments; ++minor) {
            const int next_minor = (minor + 1) % minor_segments;
            const std::array face_vertices {
                vertices[vertex_index(major, minor)],
                vertices[vertex_index(major, next_minor)],
                vertices[vertex_index(next_major, next_minor)],
                vertices[vertex_index(next_major, minor)],
            };
            [[maybe_unused]] const ElementId face_id = add_face(document, face_vertices);
        }
    }

    return document;
}

} // namespace quader_poly

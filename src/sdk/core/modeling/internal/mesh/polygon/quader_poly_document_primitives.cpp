////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/neutral/polygon/document.hpp>

#include <quader/modeling/neutral/polygon/internal/quader_poly_document_constants.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <span>
#include <vector>

namespace quader_poly {
namespace {

using namespace document_internal;

float clamp_primitive_dimension(float value)
{
    if (!std::isfinite(value)) {
        return 1.0F;
    }
    return std::clamp(value, kMinPrimitiveDimension, kMaxPrimitiveDimension);
}

int clamp_primitive_count(int value, int min_value, int max_value)
{
    return std::clamp(value, min_value, max_value);
}

float clamp_torus_tube_radius_ratio(float value)
{
    if (!std::isfinite(value)) {
        return 0.2F;
    }
    return std::clamp(value, 0.01F, 0.49F);
}

void set_face_shading(Document& document, ElementId face_id, SurfaceShading shading)
{
    if (Face* face = find_face(document, face_id); face != nullptr) {
        face->normal_shading = shading;
    }
}

void add_shaded_face(
    Document& document,
    std::span<const ElementId> vertices,
    SurfaceShading shading)
{
    set_face_shading(document, add_face(document, vertices), shading);
}

void reverse_all_faces(Document& document)
{
    for (Face& face : document.faces) {
        std::ranges::reverse(face.vertices);
        std::ranges::reverse(face.uvs);
    }
}

std::vector<ElementId> add_radial_ring(
    Document& document,
    int radial_segments,
    float half_width,
    float y,
    float half_depth,
    float radial_scale)
{
    std::vector<ElementId> ring;
    ring.reserve(static_cast<std::size_t>(radial_segments));
    for (int segment = 0; segment < radial_segments; ++segment) {
        const float angle = kTau * static_cast<float>(segment) / static_cast<float>(radial_segments);
        ring.push_back(add_vertex(document, {
            half_width * radial_scale * std::cos(angle),
            y,
            half_depth * radial_scale * std::sin(angle),
        }));
    }
    return ring;
}

void add_radial_band(
    Document& document,
    std::span<const ElementId> lower_ring,
    std::span<const ElementId> upper_ring)
{
    const std::size_t segment_count = lower_ring.size();
    for (std::size_t segment = 0; segment < segment_count; ++segment) {
        const std::size_t next = (segment + 1U) % segment_count;
        const std::array face {
            lower_ring[segment],
            upper_ring[segment],
            upper_ring[next],
            lower_ring[next],
        };
        add_shaded_face(document, face, SurfaceShading::Smooth);
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

        const quader::QVec3 position {
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

    if (spec.flip_faces) {
        reverse_all_faces(document);
    }

    return document;
}

Document make_cylinder(const CylinderPrimitiveSpec& spec)
{
    Document document;
    const float width = clamp_primitive_dimension(spec.width);
    const float height = clamp_primitive_dimension(spec.height);
    const float depth = clamp_primitive_dimension(spec.depth);
    const int side_count = clamp_primitive_count(spec.side_count, 3, 128);
    const float half_width = width * 0.5F;
    const float half_height = height * 0.5F;
    const float half_depth = depth * 0.5F;

    const std::vector<ElementId> bottom_ring =
        add_radial_ring(document, side_count, half_width, -half_height, half_depth, 1.0F);
    const std::vector<ElementId> top_ring =
        add_radial_ring(document, side_count, half_width, half_height, half_depth, 1.0F);
    add_radial_band(document, bottom_ring, top_ring);

    if (spec.capped) {
        add_shaded_face(document, bottom_ring, SurfaceShading::Flat);
        const std::vector<ElementId> top_cap(top_ring.rbegin(), top_ring.rend());
        add_shaded_face(document, top_cap, SurfaceShading::Flat);
    }

    if (spec.flip_faces) {
        reverse_all_faces(document);
    }
    return document;
}

Document make_cone(const ConePrimitiveSpec& spec)
{
    Document document;
    const float width = clamp_primitive_dimension(spec.width);
    const float height = clamp_primitive_dimension(spec.height);
    const float depth = clamp_primitive_dimension(spec.depth);
    const int side_count = clamp_primitive_count(spec.side_count, 3, 128);
    const float half_width = width * 0.5F;
    const float half_height = height * 0.5F;
    const float half_depth = depth * 0.5F;

    const std::vector<ElementId> base_ring =
        add_radial_ring(document, side_count, half_width, -half_height, half_depth, 1.0F);
    const ElementId apex = add_vertex(document, { 0.0F, half_height, 0.0F });
    for (int segment = 0; segment < side_count; ++segment) {
        const std::size_t current = static_cast<std::size_t>(segment);
        const std::size_t next = (current + 1U) % base_ring.size();
        const std::array face { base_ring[current], apex, base_ring[next] };
        add_shaded_face(document, face, SurfaceShading::Smooth);
    }

    if (spec.capped) {
        add_shaded_face(document, base_ring, SurfaceShading::Flat);
    }

    if (spec.flip_faces) {
        reverse_all_faces(document);
    }
    return document;
}

Document make_sphere(const SpherePrimitiveSpec& spec)
{
    Document document;
    const float width = clamp_primitive_dimension(spec.width);
    const float height = clamp_primitive_dimension(spec.height);
    const float depth = clamp_primitive_dimension(spec.depth);
    const int radial_segments = clamp_primitive_count(spec.radial_segments, 3, 128);
    const int rings = clamp_primitive_count(spec.rings, 2, 128);
    const float half_width = width * 0.5F;
    const float half_height = height * 0.5F;
    const float half_depth = depth * 0.5F;

    if (spec.hemisphere) {
        std::vector<std::vector<ElementId>> radial_rings;
        radial_rings.reserve(static_cast<std::size_t>(rings));
        for (int ring_index = 0; ring_index < rings; ++ring_index) {
            const float latitude = (kPi * 0.5F) * static_cast<float>(ring_index) / static_cast<float>(rings);
            radial_rings.push_back(add_radial_ring(
                document,
                radial_segments,
                half_width,
                -half_height + height * std::sin(latitude),
                half_depth,
                std::cos(latitude)));
        }

        for (std::size_t ring_index = 0; ring_index + 1U < radial_rings.size(); ++ring_index) {
            add_radial_band(document, radial_rings[ring_index], radial_rings[ring_index + 1U]);
        }

        const ElementId top = add_vertex(document, { 0.0F, half_height, 0.0F });
        const std::vector<ElementId>& last_ring = radial_rings.back();
        for (int segment = 0; segment < radial_segments; ++segment) {
            const std::size_t current = static_cast<std::size_t>(segment);
            const std::size_t next = (current + 1U) % last_ring.size();
            const std::array face { last_ring[current], top, last_ring[next] };
            add_shaded_face(document, face, SurfaceShading::Smooth);
        }

        add_shaded_face(document, radial_rings.front(), SurfaceShading::Flat);
    } else {
        const ElementId bottom = add_vertex(document, { 0.0F, -half_height, 0.0F });
        std::vector<std::vector<ElementId>> radial_rings;
        radial_rings.reserve(static_cast<std::size_t>(rings - 1));
        for (int ring_index = 1; ring_index < rings; ++ring_index) {
            const float latitude =
                -kPi * 0.5F + kPi * static_cast<float>(ring_index) / static_cast<float>(rings);
            radial_rings.push_back(add_radial_ring(
                document,
                radial_segments,
                half_width,
                half_height * std::sin(latitude),
                half_depth,
                std::cos(latitude)));
        }

        const std::vector<ElementId>& first_ring = radial_rings.front();
        for (int segment = 0; segment < radial_segments; ++segment) {
            const std::size_t current = static_cast<std::size_t>(segment);
            const std::size_t next = (current + 1U) % first_ring.size();
            const std::array face { bottom, first_ring[current], first_ring[next] };
            add_shaded_face(document, face, SurfaceShading::Smooth);
        }

        for (std::size_t ring_index = 0; ring_index + 1U < radial_rings.size(); ++ring_index) {
            add_radial_band(document, radial_rings[ring_index], radial_rings[ring_index + 1U]);
        }

        const ElementId top = add_vertex(document, { 0.0F, half_height, 0.0F });
        const std::vector<ElementId>& last_ring = radial_rings.back();
        for (int segment = 0; segment < radial_segments; ++segment) {
            const std::size_t current = static_cast<std::size_t>(segment);
            const std::size_t next = (current + 1U) % last_ring.size();
            const std::array face { last_ring[current], top, last_ring[next] };
            add_shaded_face(document, face, SurfaceShading::Smooth);
        }
    }

    if (spec.flip_faces) {
        reverse_all_faces(document);
    }
    return document;
}

Document make_torus(const TorusPrimitiveSpec& spec)
{
    Document document;
    const float width = clamp_primitive_dimension(spec.width);
    const float height = clamp_primitive_dimension(spec.height);
    const float depth = clamp_primitive_dimension(spec.depth);
    const int major_segments = clamp_primitive_count(spec.major_segments, 3, 128);
    const int minor_segments = clamp_primitive_count(spec.minor_segments, 3, 128);
    const float tube_radius_ratio = clamp_torus_tube_radius_ratio(spec.tube_radius_ratio);
    const float half_width = width * 0.5F;
    const float half_height = height * 0.5F;
    const float half_depth = depth * 0.5F;
    const float tube_radius_x = half_width * tube_radius_ratio;
    const float tube_radius_z = half_depth * tube_radius_ratio;
    const float major_radius_x = half_width - tube_radius_x;
    const float major_radius_z = half_depth - tube_radius_z;

    std::vector<std::vector<ElementId>> rings;
    rings.reserve(static_cast<std::size_t>(major_segments));
    for (int major_index = 0; major_index < major_segments; ++major_index) {
        const float major_angle =
            kTau * static_cast<float>(major_index) / static_cast<float>(major_segments);
        const float major_cos = std::cos(major_angle);
        const float major_sin = std::sin(major_angle);
        std::vector<ElementId>& ring = rings.emplace_back();
        ring.reserve(static_cast<std::size_t>(minor_segments));
        for (int minor_index = 0; minor_index < minor_segments; ++minor_index) {
            const float minor_angle =
                kTau * static_cast<float>(minor_index) / static_cast<float>(minor_segments);
            const float minor_cos = std::cos(minor_angle);
            ring.push_back(add_vertex(document, {
                (major_radius_x + tube_radius_x * minor_cos) * major_cos,
                half_height * std::sin(minor_angle),
                (major_radius_z + tube_radius_z * minor_cos) * major_sin,
            }));
        }
    }

    for (int major_index = 0; major_index < major_segments; ++major_index) {
        const std::size_t current_major = static_cast<std::size_t>(major_index);
        const std::size_t next_major = (current_major + 1U) % rings.size();
        for (int minor_index = 0; minor_index < minor_segments; ++minor_index) {
            const std::size_t current_minor = static_cast<std::size_t>(minor_index);
            const std::size_t next_minor = (current_minor + 1U) % rings[current_major].size();
            const std::array face {
                rings[current_major][current_minor],
                rings[current_major][next_minor],
                rings[next_major][next_minor],
                rings[next_major][current_minor],
            };
            add_shaded_face(document, face, SurfaceShading::Smooth);
        }
    }

    if (spec.flip_faces) {
        reverse_all_faces(document);
    }
    return document;
}

} // namespace quader_poly

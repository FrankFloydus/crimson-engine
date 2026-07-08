////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/document.hpp>

#include <mesh/polygon/internal/quader_poly_document_constants.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

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

    return document;
}

} // namespace quader_poly

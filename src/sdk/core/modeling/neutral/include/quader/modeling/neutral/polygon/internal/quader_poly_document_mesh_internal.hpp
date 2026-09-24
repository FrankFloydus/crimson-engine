////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

// Source-only neutral modeling core contract shared by SDK and native owners.

#include <quader/modeling/neutral/polygon/document.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace quader_poly::document_internal {

/**
 * Resolves authored vertex IDs for one immutable document operation.
 *
 * Dense append-only IDs keep the existing direct-slot fast path. Documents
 * with deleted, compacted, or imported IDs build one fallback index instead of
 * rescanning the complete vertex array for every face corner. Duplicate and
 * invalid IDs retain find_vertex's exact resolution order.
 */
class VertexLookupIndex final {
public:
    explicit VertexLookupIndex(const Document& document)
        : vertices_(document.vertices)
    {
        std::size_t fallback_vertex_count = 0U;
        for (const Vertex& vertex : vertices_) {
            if (!resolves_from_dense_hint(vertex.id)) {
                ++fallback_vertex_count;
            }
        }
        if (fallback_vertex_count == 0U) {
            return;
        }

        fallback_vertices_.reserve(fallback_vertex_count);
        for (const Vertex& vertex : vertices_) {
            if (!resolves_from_dense_hint(vertex.id)) {
                fallback_vertices_.try_emplace(vertex.id, &vertex);
            }
        }
    }

    [[nodiscard]] const Vertex* find(ElementId id) const
    {
        if (resolves_from_dense_hint(id)) {
            return &vertices_[static_cast<std::size_t>(id - 1U)];
        }
        const auto vertex = fallback_vertices_.find(id);
        return vertex == fallback_vertices_.end() ? nullptr : vertex->second;
    }

private:
    [[nodiscard]] bool resolves_from_dense_hint(ElementId id) const noexcept
    {
        if (id == kInvalidElementId) {
            return false;
        }
        const std::size_t hinted_index = static_cast<std::size_t>(id - 1U);
        return hinted_index < vertices_.size() &&
               vertices_[hinted_index].id == id;
    }

    const std::vector<Vertex>& vertices_;
    std::unordered_map<ElementId, const Vertex*> fallback_vertices_;
};

/**
 * Represents a Triangle value used by the polygon document and mesh editing core.
 */
struct Triangle {
    std::uint32_t a = 0;
    std::uint32_t b = 0;
    std::uint32_t c = 0;
};

/**
 * Represents a Face UV Basis value used by the polygon document and mesh editing core.
 */
struct FaceUvBasis {
    quader::QVec3 origin;
    quader::QVec3 u_axis { 1.0F, 0.0F, 0.0F };
    quader::QVec3 v_axis { 0.0F, 1.0F, 0.0F };
    bool valid = false;
};

/**
 * Represents a Face UV Projection Assignment value used by the polygon document and mesh editing core.
 */
struct FaceUvProjectionAssignment {
    std::uint32_t material_slot = 0;
    FaceUvBasis basis;
    ElementId source_face_id = kInvalidElementId;
    bool inherited_loop_basis = false;
    FaceTextureTransform texture_transform{};
};

/**
 * Represents a Merge Face UV Assignment value used by the polygon document and mesh editing core.
 */
struct MergeFaceUvAssignment {
  ElementId face_id = kInvalidElementId;
  FaceUvProjectionAssignment assignment;
};

[[nodiscard]] int dropped_axis_for_normal(const quader::QVec3& normal);
[[nodiscard]] std::vector<Triangle> triangulate_face_local_indices(const Document& document, const Face& face);
[[nodiscard]] std::vector<Triangle> triangulate_face_local_indices(
    const Document& document,
    const Face& face,
    const VertexLookupIndex& vertex_lookup);
[[nodiscard]] std::vector<Triangle> triangulate_face_local_indices(
    const Document& document,
    const Face& face,
    const VertexLookupIndex& vertex_lookup,
    const quader::QVec3& normal);

// A projected triangulation may skip only corners on a straight 3D boundary.
[[nodiscard]] bool triangulation_preserves_face_boundary(
    const Face& face, const VertexLookupIndex& vertex_lookup,
    std::span<const Triangle> triangles);

[[nodiscard]] FaceUvBasis generated_face_uv_basis(const Document& document, const Face& face, const quader::QVec3& normal);
[[nodiscard]] FaceUvBasis generated_face_uv_basis(
    const Document& document,
    const Face& face,
    const quader::QVec3& normal,
    const VertexLookupIndex& vertex_lookup);
[[nodiscard]] FaceUvBasis project_uv_basis_to_face_plane(const FaceUvBasis& generated_basis, const quader::QVec3& normal, const quader::QVec3& origin);
[[nodiscard]] FaceUvBasis face_local_uv_basis(const Document& document, const Face& face, const quader::QVec3& normal);
[[nodiscard]] quader::QVec2 generated_face_uv(const quader::QVec3& position, const FaceUvBasis& basis);
[[nodiscard]] quader::QVec2 generated_uv_for_position(const quader::QVec3& position, const FaceUvBasis& basis, int dropped_axis);
[[nodiscard]] bool face_has_loop_uvs(const Face& face);
[[nodiscard]] bool assign_generated_face_uvs(Document& document, Face& face);
[[nodiscard]] bool assign_face_uvs_from_basis(Document& document, Face& face, const FaceUvBasis& basis);
[[nodiscard]] bool assign_face_local_uvs(Document& document, Face& face);
[[nodiscard]] quader::QVec2 translation_uv_delta(const Document& document, const Face& face, const quader::QVec3& delta);
[[nodiscard]] bool ensure_face_loop_uvs(Document& document, Face& face);

} // namespace quader_poly::document_internal

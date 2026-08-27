////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_uv_helpers.hpp>
#include <quader/modeling/neutral/polygon/document_texture.hpp>

#include <quader/modeling/neutral/polygon/internal/quader_poly_document_hull_plane_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_topology_helpers.hpp>

#include <quader/modeling/neutral/geometry/geometry.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <exception>
#include <iterator>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace quader_poly::document_internal {

quader_geometry::QVec2f geometry_uv(quader::QVec2 value)
{
    return { value.x, value.y };
}

std::vector<ElementId> unique_vertex_ids(std::vector<ElementId> ids)
{
    std::ranges::sort(ids);
    ids.erase(std::ranges::unique(ids).begin(), ids.end());
    return ids;
}

std::optional<quader::QVec3> uv_basis_origin_for_anchor(const quader::QVec3& position, const quader::QVec2& uv, const quader::QVec3& u_axis, const quader::QVec3& v_axis)
{
    const float uu = dot(u_axis, u_axis);
    const float uv_dot = dot(u_axis, v_axis);
    const float vv = dot(v_axis, v_axis);
    const float determinant = (uu * vv) - (uv_dot * uv_dot);
    if (std::abs(determinant) <= kEpsilon) {
      return std::nullopt;
    }

    const float target_u =
        dot(position, u_axis) - (uv.x / kGeneratedUvTilesPerWorldUnit);
    const float target_v =
        dot(position, v_axis) - (uv.y / kGeneratedUvTilesPerWorldUnit);
    const float alpha = ((target_u * vv) - (target_v * uv_dot)) / determinant;
    const float beta = ((target_v * uu) - (target_u * uv_dot)) / determinant;
    return (u_axis * alpha) + (v_axis * beta);
}

bool uv_basis_varies_on_face(const Document& document, const Face& face, const FaceUvBasis& basis)
{
    if (!basis.valid || face.vertices.size() < 3) {
        return false;
    }

    const Vertex* origin = find_vertex(document, face.vertices.front());
    if (origin == nullptr) {
        return false;
    }

    std::vector<quader::QVec2> projected;
    projected.reserve(face.vertices.size());
    for (const ElementId vertex_id : face.vertices) {
        const Vertex* vertex = find_vertex(document, vertex_id);
        if (vertex == nullptr) {
            return false;
        }
        const quader::QVec3 edge = vertex->position - origin->position;
        projected.push_back({ dot(edge, basis.u_axis), dot(edge, basis.v_axis) });
    }

    for (std::size_t first = 1; first < projected.size(); ++first) {
        for (std::size_t second = first + 1U; second < projected.size(); ++second) {
            const float determinant =
                (projected[first].x * projected[second].y) -
                (projected[first].y * projected[second].x);
            if (std::abs(determinant) > kEpsilon) {
              return true;
            }
        }
    }
    return false;
}

std::optional<FaceUvBasis> face_uv_basis_from_loop_uvs(const Document& document, const Face& face)
{
    if (!face_has_loop_uvs(face) || face.vertices.size() < 3) {
        return std::nullopt;
    }

    for (std::size_t anchor_index = 0; anchor_index < face.vertices.size(); ++anchor_index) {
        const Vertex* anchor = find_vertex(document, face.vertices[anchor_index]);
        if (anchor == nullptr) {
            continue;
        }

        for (std::size_t first_index = 0; first_index < face.vertices.size(); ++first_index) {
            if (first_index == anchor_index) {
                continue;
            }
            const Vertex* first = find_vertex(document, face.vertices[first_index]);
            if (first == nullptr) {
                continue;
            }

            for (std::size_t second_index = first_index + 1U; second_index < face.vertices.size(); ++second_index) {
                if (second_index == anchor_index) {
                    continue;
                }
                const Vertex* second = find_vertex(document, face.vertices[second_index]);
                if (second == nullptr) {
                    continue;
                }

                const quader::QVec3 first_edge = first->position - anchor->position;
                const quader::QVec3 second_edge = second->position - anchor->position;
                const float first_first = dot(first_edge, first_edge);
                const float first_second = dot(first_edge, second_edge);
                const float second_second = dot(second_edge, second_edge);
                const float determinant = (first_first * second_second) - (first_second * first_second);
                if (std::abs(determinant) <= kEpsilon) {
                  continue;
                }

                const auto solve_axis = [&](float first_delta, float second_delta) {
                    const float alpha = ((first_delta * second_second) - (second_delta * first_second)) / determinant;
                    const float beta = ((second_delta * first_first) - (first_delta * first_second)) / determinant;
                    return (first_edge * alpha) + (second_edge * beta);
                };

                const quader::QVec2 anchor_uv = face.uvs[anchor_index];
                const quader::QVec2 first_uv = face.uvs[first_index];
                const quader::QVec2 second_uv = face.uvs[second_index];
                FaceUvBasis basis;
                basis.u_axis = solve_axis((first_uv.x - anchor_uv.x) /
                                              kGeneratedUvTilesPerWorldUnit,
                                          (second_uv.x - anchor_uv.x) /
                                              kGeneratedUvTilesPerWorldUnit);
                basis.v_axis = solve_axis((first_uv.y - anchor_uv.y) /
                                              kGeneratedUvTilesPerWorldUnit,
                                          (second_uv.y - anchor_uv.y) /
                                              kGeneratedUvTilesPerWorldUnit);
                basis.valid = length_squared(basis.u_axis) > kEpsilon &&
                              length_squared(basis.v_axis) > kEpsilon;
                if (!basis.valid || !uv_basis_varies_on_face(document, face, basis)) {
                    continue;
                }

                const std::optional<quader::QVec3> origin =
                    uv_basis_origin_for_anchor(anchor->position, anchor_uv, basis.u_axis, basis.v_axis);
                if (!origin.has_value()) {
                    continue;
                }
                basis.origin = *origin;

                bool matches_loops = true;
                for (std::size_t loop_index = 0; loop_index < face.vertices.size(); ++loop_index) {
                    const Vertex* vertex = find_vertex(document, face.vertices[loop_index]);
                    if (vertex == nullptr) {
                        matches_loops = false;
                        break;
                    }
                    const quader::QVec2 projected_uv = generated_face_uv(vertex->position, basis);
                    if (std::abs(projected_uv.x - face.uvs[loop_index].x) > 0.01F ||
                        std::abs(projected_uv.y - face.uvs[loop_index].y) > 0.01F) {
                        matches_loops = false;
                        break;
                    }
                }
                if (matches_loops) {
                    return basis;
                }
            }
        }
    }

    return std::nullopt;
}

std::optional<FaceUvProjectionAssignment> face_uv_projection_assignment_from_source(
        const Document& document,
        const Face& face,
        const Document& source_document,
        const std::set<ElementId>& merge_vertex_ids,
        ElementId survivor_vertex_id)
{
    const quader::QVec3 target_normal = face_normal(document, face);
    if (length_squared(target_normal) <= kEpsilon) {
      return std::nullopt;
    }

    const auto target_contains = [&face](ElementId vertex_id) {
        return std::ranges::find(face.vertices, vertex_id) != face.vertices.end();
    };
    const auto face_contains_edge = [](const Face& candidate, Edge edge) {
        return face_uses_edge(candidate, edge);
    };

    const Face* best_source_face = nullptr;
    float best_score = -std::numeric_limits<float>::infinity();
    for (const Face& source_face : source_document.faces) {
        if (source_face.vertices.size() < 3) {
            continue;
        }

        int covered_vertices = 0;
        for (const ElementId target_vertex_id : face.vertices) {
            const bool covered = std::ranges::any_of(source_face.vertices, [target_vertex_id, &merge_vertex_ids, survivor_vertex_id](ElementId source_vertex_id) {
                return mapped_vertex_for_merge(source_vertex_id, merge_vertex_ids, survivor_vertex_id) == target_vertex_id;
            });
            if (covered) {
                ++covered_vertices;
            }
        }
        if (covered_vertices < 2) {
            continue;
        }

        int exact_vertices = 0;
        for (const ElementId source_vertex_id : source_face.vertices) {
            if (target_contains(source_vertex_id)) {
                ++exact_vertices;
            }
        }

        int exact_edges = 0;
        int mapped_edges = 0;
        for (std::size_t index = 0; index < face.vertices.size(); ++index) {
            const Edge target_edge = make_edge(face.vertices[index], face.vertices[(index + 1U) % face.vertices.size()]);
            if (face_contains_edge(source_face, target_edge)) {
                ++exact_edges;
            }
            for (std::size_t source_index = 0; source_index < source_face.vertices.size(); ++source_index) {
                const ElementId mapped_a = mapped_vertex_for_merge(source_face.vertices[source_index], merge_vertex_ids, survivor_vertex_id);
                const ElementId mapped_b = mapped_vertex_for_merge(source_face.vertices[(source_index + 1U) % source_face.vertices.size()], merge_vertex_ids, survivor_vertex_id);
                if (mapped_a == mapped_b) {
                    continue;
                }
                if (make_edge(mapped_a, mapped_b) == target_edge) {
                    ++mapped_edges;
                    break;
                }
            }
        }

        const quader::QVec3 source_normal = face_normal(source_document, source_face);
        const float normal_dot = dot(normalize_or_zero(source_normal), normalize_or_zero(target_normal));
        if (normal_dot < -0.001F) {
            continue;
        }

        const float score =
            (static_cast<float>(exact_edges) * 100000.0F) +
            (static_cast<float>(mapped_edges) * 10000.0F) +
            (static_cast<float>(covered_vertices) * 1000.0F) +
            (static_cast<float>(exact_vertices) * 100.0F) +
            normal_dot;
        if (score > best_score) {
            best_source_face = &source_face;
            best_score = score;
        }
    }

    if (best_source_face == nullptr) {
        return std::nullopt;
    }

    const quader::QVec3 source_normal = face_normal(source_document, *best_source_face);
    const int source_dropped_axis = dropped_axis_for_normal(source_normal);
    FaceUvBasis source_basis;
    bool inherited_loop_basis = false;
    if (face_has_loop_uvs(*best_source_face)) {
        const std::optional<FaceUvBasis> loop_basis =
            face_uv_basis_from_loop_uvs(source_document, *best_source_face);
        if (loop_basis.has_value()) {
            source_basis = *loop_basis;
            inherited_loop_basis = true;
        }
    }
    if (!source_basis.valid) {
        source_basis = generated_face_uv_basis(source_document, *best_source_face, source_normal);
    }
    if (!source_basis.valid) {
        return std::nullopt;
    }

    std::vector<quader::QVec2> source_loop_uvs;
    source_loop_uvs.reserve(best_source_face->vertices.size());
    if (face_has_loop_uvs(*best_source_face)) {
        source_loop_uvs = best_source_face->uvs;
    } else {
        for (const ElementId source_vertex_id : best_source_face->vertices) {
            const Vertex* source_vertex = find_vertex(source_document, source_vertex_id);
            if (source_vertex == nullptr) {
                return std::nullopt;
            }
            source_loop_uvs.push_back(generated_uv_for_position(source_vertex->position, source_basis, source_dropped_axis));
        }
    }

    const Vertex* anchor_vertex = nullptr;
    quader::QVec2 anchor_uv {};
    float best_anchor_distance_sq = std::numeric_limits<float>::infinity();
    bool found_anchor = false;
    for (const ElementId target_vertex_id : face.vertices) {
        const Vertex* target_vertex = find_vertex(document, target_vertex_id);
        if (target_vertex == nullptr) {
            continue;
        }
        for (std::size_t source_loop_index = 0; source_loop_index < best_source_face->vertices.size(); ++source_loop_index) {
            const ElementId source_vertex_id = best_source_face->vertices[source_loop_index];
            if (mapped_vertex_for_merge(source_vertex_id, merge_vertex_ids, survivor_vertex_id) != target_vertex_id) {
                continue;
            }
            const Vertex* source_vertex = find_vertex(source_document, source_vertex_id);
            if (source_vertex == nullptr) {
                continue;
            }
            const float distance_sq = length_squared(target_vertex->position - source_vertex->position);
            const bool exact_id_match = source_vertex_id == target_vertex_id;
            const float anchor_score_distance = exact_id_match ? distance_sq * 0.25F : distance_sq;
            if (!found_anchor || anchor_score_distance < best_anchor_distance_sq) {
                anchor_vertex = target_vertex;
                anchor_uv = source_loop_uvs[source_loop_index];
                best_anchor_distance_sq = anchor_score_distance;
                found_anchor = true;
            }
        }
    }

    if (!found_anchor || anchor_vertex == nullptr) {
        return std::nullopt;
    }

    FaceUvBasis basis = source_basis;
    const bool source_basis_varies = uv_basis_varies_on_face(document, face, basis);
    if (!source_basis_varies) {
        basis = project_uv_basis_to_face_plane(source_basis, target_normal, anchor_vertex->position);
    }
    if (!basis.valid) {
        return std::nullopt;
    }
    const std::optional<quader::QVec3> origin =
        uv_basis_origin_for_anchor(anchor_vertex->position, anchor_uv, basis.u_axis, basis.v_axis);
    if (!origin.has_value()) {
        return std::nullopt;
    }
    basis.origin = *origin;

    return FaceUvProjectionAssignment {
        best_source_face->material_slot,
        basis,
        best_source_face->id,
        inherited_loop_basis,
        best_source_face->texture_transform,
    };
}

bool assign_face_uvs_from_projection_assignment(
        Document& document,
        Face& face,
        const FaceUvProjectionAssignment& assignment)
{
    if (!assignment.basis.valid) {
        return false;
    }

    face.material_slot = assignment.material_slot;
    face.texture_transform = assignment.texture_transform;
    face.uvs.clear();
    face.uvs.reserve(face.vertices.size());
    for (const ElementId vertex_id : face.vertices) {
        const Vertex* vertex = find_vertex(document, vertex_id);
        if (vertex == nullptr) {
            face.uvs.clear();
            return false;
        }
        face.uvs.push_back(generated_face_uv(vertex->position, assignment.basis));
    }
    return face_has_loop_uvs(face);
}

std::optional<std::size_t> face_vertex_index(const Face& face, ElementId vertex_id)
{
    for (std::size_t index = 0; index < face.vertices.size(); ++index) {
        if (face.vertices[index] == vertex_id) {
            return index;
        }
    }
    return std::nullopt;
}

std::optional<quader::QVec2> face_uv_for_vertex_id(const Face& face, ElementId vertex_id)
{
    if (!face_has_loop_uvs(face)) {
        return std::nullopt;
    }

    const std::optional<std::size_t> index = face_vertex_index(face, vertex_id);
    if (!index.has_value()) {
        return std::nullopt;
    }
    return face.uvs[*index];
}

bool face_has_y_span(const Document& document, const Face& face)
{
    quader_geometry::QAabb3f bounds = quader_geometry::empty_aabb3<float>();
    for (const ElementId vertex_id : face.vertices) {
        const Vertex* vertex = find_vertex(document, vertex_id);
        if (vertex == nullptr) {
            return false;
        }
        quader_geometry::aabb_include(bounds, geometry_vec3(vertex->position));
    }
    return quader_geometry::aabb_is_valid(bounds) && bounds.max.y - bounds.min.y > 0.001F;
}

bool face_is_merge_split_slope(const Document& document, const Face& face)
{
    const quader::QVec3 normal = face_normal(document, face);
    return face_has_loop_uvs(face) &&
        std::abs(normal.y) > 0.25F &&
        face_has_y_span(document, face);
}

std::optional<Edge> shared_edge_between_faces(const Face& left, const Face& right)
{
    for (const Edge& edge : face_edges(left)) {
        if (face_uses_edge(right, edge)) {
            return edge;
        }
    }
    return std::nullopt;
}

quader::QVec2 face_uv_delta_for_edge(const Face& face, Edge edge)
{
    const std::optional<quader::QVec2> a = face_uv_for_vertex_id(face, edge.a);
    const std::optional<quader::QVec2> b = face_uv_for_vertex_id(face, edge.b);
    if (!a.has_value() || !b.has_value()) {
        return {};
    }
    return { b->x - a->x, b->y - a->y };
}

float uv_delta_length_squared(quader::QVec2 delta)
{
    return quader_geometry::length_squared(geometry_uv(delta));
}

const MergeFaceUvAssignment* uv_assignment_for_face(
        std::span<const MergeFaceUvAssignment> assignments,
        ElementId face_id)
{
    const auto found = std::ranges::find_if(assignments, [face_id](const MergeFaceUvAssignment& assignment) {
        return assignment.face_id == face_id;
    });
    return found == assignments.end() ? nullptr : &*found;
}

void stitch_uv_axis_to_reference_seam(Face& target_face, const Face& reference_face, Edge shared_edge, bool stitch_u_axis)
{
    const std::optional<quader::QVec2> target_a = face_uv_for_vertex_id(target_face, shared_edge.a);
    const std::optional<quader::QVec2> target_b = face_uv_for_vertex_id(target_face, shared_edge.b);
    const std::optional<quader::QVec2> reference_a = face_uv_for_vertex_id(reference_face, shared_edge.a);
    const std::optional<quader::QVec2> reference_b = face_uv_for_vertex_id(reference_face, shared_edge.b);
    if (!target_a.has_value() || !target_b.has_value() || !reference_a.has_value() || !reference_b.has_value()) {
        return;
    }

    const float target_start = stitch_u_axis ? target_a->x : target_a->y;
    const float target_end = stitch_u_axis ? target_b->x : target_b->y;
    const float reference_start = stitch_u_axis ? reference_a->x : reference_a->y;
    const float reference_end = stitch_u_axis ? reference_b->x : reference_b->y;
    const float target_delta = target_end - target_start;
    if (std::abs(target_delta) <= kEpsilon) {
      return;
    }

    const float scale = (reference_end - reference_start) / target_delta;
    for (quader::QVec2& uv : target_face.uvs) {
        float& component = stitch_u_axis ? uv.x : uv.y;
        component = reference_start + ((component - target_start) * scale);
    }
}

void stitch_merge_split_uv_pair(const Face& reference_face, Face& target_face, Edge shared_edge)
{
  // Stitching base coordinates across different authored transforms would
  // change the visible scale/rotation instead of repairing a shared seam.
  if (reference_face.texture_transform != target_face.texture_transform) {
    return;
  }
  constexpr float kSeamStretchRatio = 1.25F;
  constexpr float kSeamEndpointEpsilon = 0.01F;

  const auto should_stitch_axis = [&](float reference_start,
                                      float reference_end, float target_start,
                                      float target_end) {
    const float reference_delta_axis = reference_end - reference_start;
    const float target_delta_axis = target_end - target_start;
    return std::abs(target_delta_axis) > kEpsilon &&
           std::abs(reference_delta_axis) >
               std::abs(target_delta_axis) * kSeamStretchRatio &&
           (std::abs(reference_start - target_start) > kSeamEndpointEpsilon ||
            std::abs(reference_end - target_end) > kSeamEndpointEpsilon);
  };

  const std::optional<quader::QVec2> reference_a =
      face_uv_for_vertex_id(reference_face, shared_edge.a);
  const std::optional<quader::QVec2> reference_b =
      face_uv_for_vertex_id(reference_face, shared_edge.b);
  const std::optional<quader::QVec2> target_a =
      face_uv_for_vertex_id(target_face, shared_edge.a);
  const std::optional<quader::QVec2> target_b =
      face_uv_for_vertex_id(target_face, shared_edge.b);
  if (!reference_a.has_value() || !reference_b.has_value() ||
      !target_a.has_value() || !target_b.has_value()) {
    return;
  }

    if (should_stitch_axis(reference_a->x, reference_b->x, target_a->x, target_b->x)) {
        stitch_uv_axis_to_reference_seam(target_face, reference_face, shared_edge, true);
    }
    if (should_stitch_axis(reference_a->y, reference_b->y, target_a->y, target_b->y)) {
        stitch_uv_axis_to_reference_seam(target_face, reference_face, shared_edge, false);
    }
}

void stitch_merge_split_uv_density(Document& document, std::span<const MergeFaceUvAssignment> assignments)
{
    if (assignments.size() < 2) {
        return;
    }

    std::set<ElementId> stitched_faces;
    for (std::size_t left_index = 0; left_index < document.faces.size(); ++left_index) {
        for (std::size_t right_index = left_index + 1U; right_index < document.faces.size(); ++right_index) {
            Face& left_face = document.faces[left_index];
            Face& right_face = document.faces[right_index];
            if (!face_is_merge_split_slope(document, left_face) ||
                !face_is_merge_split_slope(document, right_face)) {
                continue;
            }

            const std::optional<Edge> shared_edge = shared_edge_between_faces(left_face, right_face);
            if (!shared_edge.has_value()) {
                continue;
            }

            const MergeFaceUvAssignment* left_assignment = uv_assignment_for_face(assignments, left_face.id);
            const MergeFaceUvAssignment* right_assignment = uv_assignment_for_face(assignments, right_face.id);
            if (left_assignment == nullptr || right_assignment == nullptr) {
                continue;
            }

            const quader::QVec2 left_delta = face_uv_delta_for_edge(left_face, *shared_edge);
            const quader::QVec2 right_delta = face_uv_delta_for_edge(right_face, *shared_edge);
            const float left_length_sq = uv_delta_length_squared(left_delta);
            const float right_length_sq = uv_delta_length_squared(right_delta);
            Face* reference_face = &left_face;
            Face* target_face = &right_face;
            const MergeFaceUvAssignment* target_assignment = right_assignment;
            if (right_length_sq > left_length_sq) {
                reference_face = &right_face;
                target_face = &left_face;
                target_assignment = left_assignment;
            }

            if (!target_assignment->assignment.inherited_loop_basis ||
                stitched_faces.contains(target_face->id)) {
                continue;
            }

            stitch_merge_split_uv_pair(*reference_face, *target_face, *shared_edge);
            stitched_faces.insert(target_face->id);
        }
    }
}

bool assign_face_uvs_from_source_projection(
        Document& document,
        Face& face,
        const Document& source_document,
        const std::set<ElementId>& merge_vertex_ids,
        ElementId survivor_vertex_id)
{
    const std::optional<FaceUvProjectionAssignment> assignment =
        face_uv_projection_assignment_from_source(document, face, source_document, merge_vertex_ids, survivor_vertex_id);
    return assignment.has_value() && assign_face_uvs_from_projection_assignment(document, face, *assignment);
}

quader::QVec3 transform_point(const Transform3& transform, quader::QVec3 point)
{
    return transform.origin +
        (transform.x_axis * point.x) +
        (transform.y_axis * point.y) +
        (transform.z_axis * point.z);
}

} // namespace quader_poly::document_internal

namespace quader_poly {
namespace {

[[nodiscard]] std::array<double, 4>
texture_linear_transform(const FaceTextureTransform &transform) {
  const double radians = std::remainder(
      static_cast<double>(transform.rotation_degrees), 360.0) *
      (std::numbers::pi / 180.0);
  const double cosine = std::cos(radians);
  const double sine = std::sin(radians);
  return {cosine / transform.scale.x, -sine / transform.scale.x,
          sine / transform.scale.y, cosine / transform.scale.y};
}

[[nodiscard]] bool is_identity_texture_mapping(const Transform3 &mapping) {
  return mapping.x_axis.x == 1.0F && mapping.x_axis.y == 0.0F &&
         mapping.x_axis.z == 0.0F && mapping.y_axis.x == 0.0F &&
         mapping.y_axis.y == 1.0F && mapping.y_axis.z == 0.0F &&
         mapping.z_axis.x == 0.0F && mapping.z_axis.y == 0.0F &&
         mapping.z_axis.z == 1.0F && mapping.origin.x == 0.0F &&
         mapping.origin.y == 0.0F && mapping.origin.z == 0.0F;
}

} // namespace

bool valid_face_texture_transform(
    const FaceTextureTransform &transform) noexcept {
  return std::isfinite(transform.scale.x) &&
         std::isfinite(transform.scale.y) && transform.scale.x != 0.0F &&
         transform.scale.y != 0.0F &&
         std::isfinite(1.0F / transform.scale.x) &&
         std::isfinite(1.0F / transform.scale.y) &&
         std::isfinite(transform.shift_uv.x) &&
         std::isfinite(transform.shift_uv.y) &&
         std::isfinite(transform.rotation_degrees);
}

quader::QVec2 apply_face_texture_transform(
    quader::QVec2 base_uv, const FaceTextureTransform &transform) {
  if (transform == FaceTextureTransform{}) {
    return base_uv;
  }
  const auto linear = texture_linear_transform(transform);
  return {
      static_cast<float>(linear[0] * base_uv.x + linear[1] * base_uv.y +
                         transform.shift_uv.x),
      static_cast<float>(linear[2] * base_uv.x + linear[3] * base_uv.y +
                         transform.shift_uv.y),
  };
}

quader::QVec2 unapply_face_texture_transform(
    quader::QVec2 uv, const FaceTextureTransform &transform) {
  if (transform == FaceTextureTransform{}) {
    return uv;
  }
  const double radians = std::remainder(
      static_cast<double>(transform.rotation_degrees), 360.0) *
      (std::numbers::pi / 180.0);
  const double cosine = std::cos(radians);
  const double sine = std::sin(radians);
  const double x = (static_cast<double>(uv.x) - transform.shift_uv.x) *
                   transform.scale.x;
  const double y = (static_cast<double>(uv.y) - transform.shift_uv.y) *
                   transform.scale.y;
  return {static_cast<float>(cosine * x + sine * y),
          static_cast<float>(-sine * x + cosine * y)};
}

std::array<float, 4> face_texture_carrier(
    std::span<const quader::QVec3> positions,
    std::span<const quader::QVec2> base_uvs,
    quader::QVec3 geometric_normal, const FaceTextureTransform &transform) {
  if (!valid_face_texture_transform(transform)) {
    return {};
  }
  const auto linear = texture_linear_transform(transform);
  std::array<double, 4> carrier{1.0, 0.0, 0.0, 1.0};
  if (!base_uvs.empty()) {
    if (positions.size() != base_uvs.size() || positions.size() < 3U) {
      return {};
    }
    double mean_qx = 0.0;
    double mean_qy = 0.0;
    double mean_u = 0.0;
    double mean_v = 0.0;
    for (std::size_t index = 0; index < positions.size(); ++index) {
      const auto q = generated_texture_uv(positions[index], geometric_normal);
      mean_qx += q.x;
      mean_qy += q.y;
      mean_u += base_uvs[index].x;
      mean_v += base_uvs[index].y;
    }
    const double inverse_count = 1.0 / static_cast<double>(positions.size());
    mean_qx *= inverse_count;
    mean_qy *= inverse_count;
    mean_u *= inverse_count;
    mean_v *= inverse_count;
    double xx = 0.0;
    double xy = 0.0;
    double yy = 0.0;
    double ux = 0.0;
    double uy = 0.0;
    double vx = 0.0;
    double vy = 0.0;
    for (std::size_t index = 0; index < positions.size(); ++index) {
      const auto q = generated_texture_uv(positions[index], geometric_normal);
      const double x = q.x - mean_qx;
      const double y = q.y - mean_qy;
      const double u = base_uvs[index].x - mean_u;
      const double v = base_uvs[index].y - mean_v;
      xx += x * x;
      xy += x * y;
      yy += y * y;
      ux += u * x;
      uy += u * y;
      vx += v * x;
      vy += v * y;
    }
    const double determinant = xx * yy - xy * xy;
    const double trace = xx + yy;
    if (!std::isfinite(determinant) ||
        determinant <= 1.0e-12 * trace * trace) {
      return {};
    }
    carrier = {(ux * yy - uy * xy) / determinant,
               (uy * xx - ux * xy) / determinant,
               (vx * yy - vy * xy) / determinant,
               (vy * xx - vx * xy) / determinant};
  }
  const std::array<float, 4> result{
      static_cast<float>(linear[0] * carrier[0] + linear[1] * carrier[2]),
      static_cast<float>(linear[0] * carrier[1] + linear[1] * carrier[3]),
      static_cast<float>(linear[2] * carrier[0] + linear[3] * carrier[2]),
      static_cast<float>(linear[2] * carrier[1] + linear[3] * carrier[3]),
  };
  return std::ranges::all_of(result, [](float value) {
           return std::isfinite(value);
         }) ? result : std::array<float, 4>{};
}

std::array<float, 4> face_texture_carrier(
    const Document &document, const Face &face,
    quader::QVec3 geometric_normal) {
  if (face.uvs.empty()) {
    return face_texture_carrier({}, {}, geometric_normal,
                                face.texture_transform);
  }
  const document_internal::VertexLookupIndex vertex_lookup(document);
  std::vector<quader::QVec3> positions;
  positions.reserve(face.vertices.size());
  for (const ElementId id : face.vertices) {
    const Vertex *vertex = vertex_lookup.find(id);
    if (vertex == nullptr) {
      return {};
    }
    positions.push_back(vertex->position);
  }
  return face_texture_carrier(positions, face.uvs, geometric_normal,
                              face.texture_transform);
}

quader::QVec2 evaluate_texture_mapping(
    quader::QVec2 uv0, quader::QVec3 position,
    quader::QVec3 geometric_normal, const std::array<float, 4> &carrier,
    const Transform3 &sampling_from_local) {
  if (is_identity_texture_mapping(sampling_from_local) ||
      carrier == std::array<float, 4>{}) {
    return uv0;
  }
  const auto cross_product = [](quader::QVec3 a, quader::QVec3 b) {
    return quader::QVec3{a.y * b.z - a.z * b.y,
                        a.z * b.x - a.x * b.z,
                        a.x * b.y - a.y * b.x};
  };
  // Cofactor columns retain the winding orientation under reflections.
  quader::QVec3 mapped_normal =
      cross_product(sampling_from_local.y_axis, sampling_from_local.z_axis) *
          geometric_normal.x +
      cross_product(sampling_from_local.z_axis, sampling_from_local.x_axis) *
          geometric_normal.y +
      cross_product(sampling_from_local.x_axis, sampling_from_local.y_axis) *
          geometric_normal.z;
  // Cofactor magnitude varies quadratically with scale. Rescale before the
  // canonical normal's degeneracy test so a small valid transform cannot
  // turn a side-face chart into the fallback up-face projection.
  const float normal_scale = std::max(
      {std::abs(mapped_normal.x), std::abs(mapped_normal.y),
       std::abs(mapped_normal.z)});
  if (std::isfinite(normal_scale) && normal_scale > 0.0F) {
    mapped_normal = mapped_normal / normal_scale;
  } else {
    mapped_normal = {0.0F, 1.0F, 0.0F};
  }
  const auto mapped_position =
      document_internal::transform_point(sampling_from_local, position);
  const auto before = generated_texture_uv(position, geometric_normal);
  const auto after = generated_texture_uv(mapped_position, mapped_normal);
  const auto delta = after - before;
  return {uv0.x + carrier[0] * delta.x + carrier[1] * delta.y,
          uv0.y + carrier[2] * delta.x + carrier[3] * delta.y};
}

std::optional<std::vector<quader::QVec2>> solve_face_texture_base_uvs(
    std::span<const quader::QVec3> positions,
    std::span<const quader::QVec2> effective_uvs,
    quader::QVec3 geometric_normal, const FaceTextureTransform &transform,
    const Transform3 &sampling_from_local) {
  if (positions.size() < 3U || positions.size() != effective_uvs.size() ||
      !valid_face_texture_transform(transform)) {
    return std::nullopt;
  }
  const auto finite_uv = [](quader::QVec2 uv) {
    return std::isfinite(uv.x) && std::isfinite(uv.y);
  };
  std::vector<quader::QVec2> base_uvs;
  base_uvs.reserve(positions.size());
  if (is_identity_texture_mapping(sampling_from_local)) {
    for (const auto uv : effective_uvs) {
      const auto base = unapply_face_texture_transform(uv, transform);
      if (!finite_uv(uv) || !finite_uv(base)) {
        return std::nullopt;
      }
      base_uvs.push_back(base);
    }
  } else {
    std::vector<quader::QVec2> deltas;
    deltas.reserve(positions.size());
    constexpr std::array<float, 4> kIdentityCarrier{1.0F, 0.0F, 0.0F, 1.0F};
    for (std::size_t index = 0U; index < positions.size(); ++index) {
      const auto position = positions[index];
      const auto q = generated_texture_uv(position, geometric_normal);
      const auto mapped = evaluate_texture_mapping(
          q, position, geometric_normal, kIdentityCarrier, sampling_from_local);
      if (!finite_uv(mapped) || !finite_uv(effective_uvs[index])) {
        return std::nullopt;
      }
      deltas.push_back(mapped - q);
    }
    const auto delta_carrier = face_texture_carrier(
        positions, deltas, geometric_normal, {});
    const auto target_carrier = face_texture_carrier(
        positions, effective_uvs, geometric_normal, {});
    // K(E) = K(f) * (I + K(d)); solve by RIGHT multiplication. Do not
    // invert K(E): constant and rank-one authored targets are legitimate.
    const std::array<double, 4> matrix{
        1.0 + delta_carrier[0], delta_carrier[1],
        delta_carrier[2], 1.0 + delta_carrier[3]};
    const double scale = std::max({std::abs(matrix[0]), std::abs(matrix[1]),
                                   std::abs(matrix[2]), std::abs(matrix[3])});
    if (!std::isfinite(scale) || scale == 0.0) {
      return std::nullopt;
    }
    const double a = matrix[0] / scale;
    const double b = matrix[1] / scale;
    const double c = matrix[2] / scale;
    const double d = matrix[3] / scale;
    const double determinant = a * d - b * c;
    if (!std::isfinite(determinant) || std::abs(determinant) <= 1.0e-10) {
      return std::nullopt;
    }
    const double inverse = 1.0 / (determinant * scale);
    const std::array<double, 4> carrier{
        (target_carrier[0] * d - target_carrier[1] * c) * inverse,
        (target_carrier[1] * a - target_carrier[0] * b) * inverse,
        (target_carrier[2] * d - target_carrier[3] * c) * inverse,
        (target_carrier[3] * a - target_carrier[2] * b) * inverse};
    for (std::size_t index = 0U; index < positions.size(); ++index) {
      const auto target = effective_uvs[index];
      const auto delta = deltas[index];
      const quader::QVec2 before_compensation{
          static_cast<float>(target.x - carrier[0] * delta.x -
                             carrier[1] * delta.y),
          static_cast<float>(target.y - carrier[2] * delta.x -
                             carrier[3] * delta.y)};
      const auto base = unapply_face_texture_transform(before_compensation,
                                                      transform);
      if (!finite_uv(base)) {
        return std::nullopt;
      }
      base_uvs.push_back(base);
    }
  }
  const auto actual_carrier = face_texture_carrier(
      positions, base_uvs, geometric_normal, transform);
  for (std::size_t index = 0U; index < positions.size(); ++index) {
    const auto actual = evaluate_texture_mapping(
        apply_face_texture_transform(base_uvs[index], transform),
        positions[index], geometric_normal, actual_carrier,
        sampling_from_local);
    const auto close = [](float left, float right) {
      constexpr float kRelativeTolerance =
          16.0F * std::numeric_limits<float>::epsilon();
      return std::isfinite(left) && std::isfinite(right) &&
             std::abs(left - right) <= 2.0e-5F +
                 kRelativeTolerance * std::max(std::abs(left), std::abs(right));
    };
    if (!close(actual.x, effective_uvs[index].x) ||
        !close(actual.y, effective_uvs[index].y)) {
      return std::nullopt;
    }
  }
  return base_uvs;
}

bool bake_document_texture_mapping(Document &document,
                                   const Transform3 &sampling_from_local) {
  if (is_identity_texture_mapping(sampling_from_local)) {
    return false;
  }
  /**
   * Stages one face's baked base UVs until every face has passed validation.
   */
  struct FaceUvUpdate {
    std::size_t face_index = 0U;
    std::vector<quader::QVec2> base_uvs;
  };
  std::vector<FaceUvUpdate> updates;
  const document_internal::VertexLookupIndex vertex_lookup(document);
  std::vector<quader::QVec3> positions;
  for (std::size_t face_index = 0U; face_index < document.faces.size();
       ++face_index) {
    const Face &face = document.faces[face_index];
    if (!valid_face_texture_transform(face.texture_transform)) {
      throw std::invalid_argument("Cannot bake invalid face texture settings.");
    }
    const auto normal = face_normal(document, face);
    positions.clear();
    positions.reserve(face.vertices.size());
    for (const ElementId id : face.vertices) {
      const Vertex *vertex = vertex_lookup.find(id);
      if (vertex == nullptr) {
        throw std::invalid_argument("Cannot bake an unresolved face vertex.");
      }
      positions.push_back(vertex->position);
    }
    const auto carrier = face_texture_carrier(
        positions, face.uvs, normal, face.texture_transform);
    FaceUvUpdate update{.face_index = face_index};
    update.base_uvs.reserve(positions.size());
    bool changed = false;
    for (std::size_t corner = 0U; corner < positions.size(); ++corner) {
      const auto base = face.uvs.size() == positions.size()
                            ? face.uvs[corner]
                            : generated_texture_uv(positions[corner], normal);
      const auto original =
          apply_face_texture_transform(base, face.texture_transform);
      const auto effective = evaluate_texture_mapping(
          original, positions[corner], normal, carrier, sampling_from_local);
      const auto baked = effective.x == original.x && effective.y == original.y
                             ? base
                             : unapply_face_texture_transform(
                                   effective, face.texture_transform);
      if (!std::isfinite(baked.x) || !std::isfinite(baked.y)) {
        throw std::invalid_argument("Texture mapping bake is non-finite.");
      }
      changed = changed || baked.x != base.x || baked.y != base.y;
      update.base_uvs.push_back(baked);
    }
    if (changed) {
      updates.push_back(std::move(update));
    }
  }
  for (auto &update : updates) {
    document.faces[update.face_index].uvs = std::move(update.base_uvs);
  }
  return !updates.empty();
}

} // namespace quader_poly

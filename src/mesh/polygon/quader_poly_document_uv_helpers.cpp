////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/internal/quader_poly_document_uv_helpers.hpp>

#include <mesh/polygon/internal/quader_poly_document_hull_plane_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_topology_helpers.hpp>

#include <mesh/geometry/geometry.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <exception>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <span>
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

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
#include <quader/modeling/neutral/geometry/circular_uv_interpolation.hpp>

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
#include <unordered_map>
#include <utility>
#include <vector>

namespace quader_poly::document_internal {

namespace {

constexpr float kCornerPositionTolerance = 0.00001F;

quader::QVec2 interpolate_corner_uv(quader::QVec2 first,
                                  quader::QVec2 second, float factor)
{
    return { first.x + (second.x - first.x) * factor,
             first.y + (second.y - first.y) * factor };
}

std::optional<quader::QVec3> triangle_weights(
    quader::QVec3 point, quader::QVec3 a, quader::QVec3 b, quader::QVec3 c)
{
    const quader::QVec3 ab = b - a;
    const quader::QVec3 ac = c - a;
    const quader::QVec3 ap = point - a;
    const double aa = dot(ab, ab);
    const double bb = dot(ac, ac);
    const double cross = dot(ab, ac);
    const double determinant = aa * bb - cross * cross;
    if (determinant <= std::numeric_limits<double>::epsilon() * aa * bb) {
        return std::nullopt;
    }
    const double pa = dot(ap, ab);
    const double pb = dot(ap, ac);
    const float wb = static_cast<float>((pa * bb - pb * cross) / determinant);
    const float wc = static_cast<float>((pb * aa - pa * cross) / determinant);
    const float wa = 1.0F - wb - wc;
    if (wa < -kCornerPositionTolerance || wb < -kCornerPositionTolerance ||
        wc < -kCornerPositionTolerance) {
        return std::nullopt;
    }
    const quader::QVec3 projected = a + ab * wb + ac * wc;
    const float tolerance = kCornerPositionTolerance *
        std::max(1.0F, std::sqrt(static_cast<float>(std::max(aa, bb))));
    if (length_squared(point - projected) > tolerance * tolerance) {
        return std::nullopt;
    }
    return quader::QVec3 { wa, wb, wc };
}

quader::QVec2 nearest_face_base_uv(const FaceUvSampler& sampler,
    const Face& source_face, quader::QVec3 position)
{
    const auto as_double = [](quader::QVec3 value) {
        return quader_geometry::QVec3d { value.x, value.y, value.z };
    };
    const auto finite = [](quader::QVec2 uv) {
        return std::isfinite(uv.x) && std::isfinite(uv.y);
    };
    const auto positions = sampler.positions();
    const auto point = as_double(position);
    quader::QVec2 nearest {};
    for (const auto uv : source_face.uvs) {
        if (finite(uv)) {
            nearest = uv;
            break;
        }
    }
    double distance = std::numeric_limits<double>::infinity();
    const auto keep_nearest = [&](quader::QVec2 uv, double candidate_distance) {
        if (finite(uv) && candidate_distance < distance) {
            nearest = uv;
            distance = candidate_distance;
        }
    };
    for (const Triangle& triangle : sampler.triangles()) {
        const quader_geometry::QTriangle3<double> geometry {
            as_double(positions[triangle.a]), as_double(positions[triangle.b]),
            as_double(positions[triangle.c]) };
        const auto weights = quader_geometry::barycentric_coordinates(point, geometry);
        if (!weights.valid || weights.u < 0.0 || weights.v < 0.0 || weights.w < 0.0) {
            continue;
        }
        const auto projected = geometry.a * weights.u + geometry.b * weights.v +
            geometry.c * weights.w;
        const auto sampled = sampler.sample({ static_cast<float>(projected.x),
            static_cast<float>(projected.y), static_cast<float>(projected.z) });
        const auto a = source_face.uvs[triangle.a];
        const auto b = source_face.uvs[triangle.b];
        const auto c = source_face.uvs[triangle.c];
        const quader::QVec2 interpolated {
            static_cast<float>(a.x * weights.u + b.x * weights.v + c.x * weights.w),
            static_cast<float>(a.y * weights.u + b.y * weights.v + c.y * weights.w) };
        keep_nearest(sampled.has_value() && finite(*sampled) ? *sampled : interpolated,
            quader_geometry::length_squared(point - projected));
    }
    for (std::size_t index = 0U; index < positions.size(); ++index) {
        const std::size_t next = (index + 1U) % positions.size();
        const auto closest = quader_geometry::closest_point_on_segment<double>(point,
            { as_double(positions[index]), as_double(positions[next]) });
        const auto first_uv = source_face.uvs[index];
        const auto second_uv = source_face.uvs[next];
        keep_nearest({
            static_cast<float>(std::lerp(static_cast<double>(first_uv.x),
                static_cast<double>(second_uv.x), closest.segment_factor)),
            static_cast<float>(std::lerp(static_cast<double>(first_uv.y),
                static_cast<double>(second_uv.y), closest.segment_factor)) },
            closest.distance_squared);
    }
    return nearest;
}

} // namespace

FaceUvSampler::FaceUvSampler(const Document& document, const Face& face)
    : FaceUvSampler(document, face, VertexLookupIndex(document))
{
}

FaceUvSampler::FaceUvSampler(const Document& document, const Face& face,
                            const VertexLookupIndex& vertex_lookup)
    : transform_(face.texture_transform)
{
    if (face.vertices.size() < 3U) {
        return;
    }
    const bool authored = face_has_loop_uvs(face);
    const quader::QVec3 normal = face_normal(document, face);
    positions_.reserve(face.vertices.size());
    base_uvs_.reserve(face.vertices.size());
    for (std::size_t index = 0; index < face.vertices.size(); ++index) {
        const Vertex* vertex = vertex_lookup.find(face.vertices[index]);
        if (vertex == nullptr) {
            positions_.clear();
            base_uvs_.clear();
            return;
        }
        positions_.push_back(vertex->position);
        base_uvs_.push_back(authored ? face.uvs[index]
            : generated_texture_uv(vertex->position, normal));
    }
    triangles_ = triangulate_face_local_indices(document, face, vertex_lookup, normal);
    const bool circular_requested = face.circular_uv_interpolation &&
        valid_circular_uv_interpolation(*face.circular_uv_interpolation);
    if (circular_requested) {
        circular_ = face.circular_uv_interpolation;
        for (const auto position : positions_) {
            const auto coordinate = circular_uv_coordinates(*circular_, position);
            if (!coordinate) {
                circular_.reset();
                circular_coordinates_.clear();
                break;
            }
            circular_coordinates_.push_back(*coordinate);
        }
    }
    if (!circular_requested && positions_.size() == 4U && triangles_.size() == 2U) {
        std::array<quader_geometry::QVec3f, 4> points;
        std::array<quader_geometry::QVec2f, 4> effective_uvs;
        for (std::size_t index = 0; index < points.size(); ++index) {
            points[index] = geometry_vec3(positions_[index]);
            const quader::QVec2 uv = apply_face_texture_transform(base_uvs_[index], transform_);
            effective_uvs[index] = {uv.x, uv.y};
        }
        quad_ = quader_geometry::build_quad_uv_interpolation(points, effective_uvs);
    }
}

std::optional<quader::QVec2> FaceUvSampler::sample(quader::QVec3 position) const
{
    // Edges are evaluated before triangles to keep adjacent source triangles
    // from perturbing the interpolation on a face boundary.
    for (std::size_t index = 0; index < positions_.size(); ++index) {
        const std::size_t next = (index + 1U) % positions_.size();
        const quader::QVec3 first = positions_[index];
        const quader::QVec3 second = positions_[next];
        const quader::QVec3 edge = second - first;
        const float edge_length_squared = length_squared(edge);
        const float tolerance_squared = kCornerPositionTolerance *
            kCornerPositionTolerance * std::max(1.0F, edge_length_squared);
        if (length_squared(position - first) <= tolerance_squared) {
            return base_uvs_[index];
        }
        if (edge_length_squared == 0.0F) {
            continue;
        }
        const float factor = dot(position - first, edge) / edge_length_squared;
        if (factor >= 0.0F && factor <= 1.0F &&
            length_squared(position - (first + edge * factor)) <= tolerance_squared) {
            const auto uv = interpolate_corner_uv(base_uvs_[index], base_uvs_[next], factor);
            if (!circular_) return uv;
            std::array<float, 4> coordinate{};
            for (std::size_t axis = 0; axis < coordinate.size(); ++axis) {
                coordinate[axis] = circular_coordinates_[index][axis] * (1.0F - factor) +
                    circular_coordinates_[next][axis] * factor;
            }
            return evaluate_circular_uv_interpolation(uv, coordinate, circular_->uv_from_polar);
        }
    }
    for (const Triangle& triangle : triangles_) {
        const auto weights = triangle_weights(position, positions_[triangle.a],
                                               positions_[triangle.b], positions_[triangle.c]);
        if (!weights.has_value()) {
            continue;
        }
        const quader::QVec2 ua = base_uvs_[triangle.a];
        const quader::QVec2 ub = base_uvs_[triangle.b];
        const quader::QVec2 uc = base_uvs_[triangle.c];
        const quader::QVec2 triangle_uv {
            ua.x * weights->x + ub.x * weights->y + uc.x * weights->z,
            ua.y * weights->x + ub.y * weights->y + uc.y * weights->z };
        if (circular_) {
            std::array<float, 4> coordinate{};
            for (std::size_t axis = 0; axis < coordinate.size(); ++axis) {
                coordinate[axis] = circular_coordinates_[triangle.a][axis] * weights->x +
                    circular_coordinates_[triangle.b][axis] * weights->y +
                    circular_coordinates_[triangle.c][axis] * weights->z;
            }
            return evaluate_circular_uv_interpolation(triangle_uv, coordinate, circular_->uv_from_polar);
        }
        if (!quad_.has_value()) {
            return triangle_uv;
        }
        const auto coordinate = quad_->coordinates[triangle.a] * weights->x +
            quad_->coordinates[triangle.b] * weights->y + quad_->coordinates[triangle.c] * weights->z;
        const quader::QVec2 effective_uv = apply_face_texture_transform(triangle_uv, transform_);
        const auto interpolated = quader_geometry::evaluate_quad_uv_interpolation(
            {effective_uv.x, effective_uv.y}, coordinate, quad_->geometry_warp, quad_->uv_correction);
        return unapply_face_texture_transform({interpolated.x, interpolated.y}, transform_);
    }
    return std::nullopt;
}

std::optional<quader::QVec2> sample_face_base_uv(
    const Document& document, const Face& face, quader::QVec3 position)
{
    return FaceUvSampler(document, face).sample(position);
}

bool face_vertex_uv_is_redundant(
    const Document& document, const Face& face, ElementId vertex_id)
{
    if (!face_has_loop_uvs(face)) {
        return true;
    }
    const auto index = face_vertex_index(face, vertex_id);
    if (!index.has_value() || face.vertices.size() < 3U) {
        return false;
    }
    const std::size_t previous = (*index + face.vertices.size() - 1U) % face.vertices.size();
    const std::size_t next = (*index + 1U) % face.vertices.size();
    const Vertex* a = find_vertex(document, face.vertices[previous]);
    const Vertex* b = find_vertex(document, face.vertices[next]);
    const Vertex* vertex = find_vertex(document, vertex_id);
    if (a == nullptr || b == nullptr || vertex == nullptr) {
        return false;
    }
    const quader::QVec3 edge = b->position - a->position;
    const float denominator = length_squared(edge);
    if (denominator == 0.0F) {
        return false;
    }
    const float factor = dot(vertex->position - a->position, edge) / denominator;
    const quader::QVec2 expected = interpolate_corner_uv(face.uvs[previous], face.uvs[next], factor);
    const quader::QVec2 actual = face.uvs[*index];
    return std::abs(expected.x - actual.x) <= 0.00001F &&
           std::abs(expected.y - actual.y) <= 0.00001F;
}

bool preserve_face_uvs(const Document& source_document, const Face& source_face,
    const Document& destination_document, Face& destination_face)
{
    if ((!source_face.uvs.empty() && !face_has_loop_uvs(source_face)) ||
        std::ranges::any_of(source_face.uvs, [](quader::QVec2 uv) {
            return !std::isfinite(uv.x) || !std::isfinite(uv.y);
        })) {
        return false;
    }
    if (!face_has_loop_uvs(source_face)) {
        destination_face.uvs.clear();
        return true;
    }
    std::vector<quader::QVec2> corners;
    corners.reserve(destination_face.vertices.size());
    std::optional<FaceUvSampler> sampler;
    for (const ElementId vertex_id : destination_face.vertices) {
        const auto original = face_uv_for_vertex_id(source_face, vertex_id);
        if (original.has_value()) {
            corners.push_back(*original);
            continue;
        }
        const Vertex* vertex = find_vertex(destination_document, vertex_id);
        if (vertex == nullptr || !std::isfinite(vertex->position.x) ||
            !std::isfinite(vertex->position.y) || !std::isfinite(vertex->position.z)) {
            return false;
        }
        if (!sampler.has_value()) {
            const VertexLookupIndex source_lookup(source_document);
            for (const ElementId source_id : source_face.vertices) {
                const Vertex* source_vertex = source_lookup.find(source_id);
                if (source_vertex == nullptr || !std::isfinite(source_vertex->position.x) ||
                    !std::isfinite(source_vertex->position.y) ||
                    !std::isfinite(source_vertex->position.z)) {
                    return false;
                }
            }
            sampler.emplace(source_document, source_face, source_lookup);
        }
        const auto interpolated = sampler->sample(vertex->position);
        // Geometry edits can move a new corner away from the source surface.
        // Keep its closest chart value when exact interpolation is unavailable.
        corners.push_back(interpolated.has_value() &&
                std::isfinite(interpolated->x) && std::isfinite(interpolated->y)
            ? *interpolated
            : nearest_face_base_uv(*sampler, source_face, vertex->position));
    }
    destination_face.uvs = std::move(corners);
    return true;
}

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

    std::unordered_map<ElementId, const MergeFaceUvAssignment*> assignments_by_face;
    assignments_by_face.reserve(assignments.size());
    for (const MergeFaceUvAssignment& assignment : assignments) {
        assignments_by_face.try_emplace(assignment.face_id, &assignment);
    }
    struct AssignedSlopeFace {
        Face* face = nullptr;
        const MergeFaceUvAssignment* assignment = nullptr;
    };
    std::vector<AssignedSlopeFace> assigned_faces;
    assigned_faces.reserve(std::min(document.faces.size(), assignments.size()));
    // Unassigned faces cannot participate. Keep document order so seam repair
    // still chooses the same reference and stitches each target at the same turn.
    for (Face& face : document.faces) {
        const auto assignment = assignments_by_face.find(face.id);
        if (assignment != assignments_by_face.end() &&
            face_is_merge_split_slope(document, face)) {
            assigned_faces.push_back({&face, assignment->second});
        }
    }

    std::set<ElementId> stitched_faces;
    for (std::size_t left_index = 0; left_index < assigned_faces.size(); ++left_index) {
        for (std::size_t right_index = left_index + 1U; right_index < assigned_faces.size(); ++right_index) {
            Face& left_face = *assigned_faces[left_index].face;
            Face& right_face = *assigned_faces[right_index].face;

            const std::optional<Edge> shared_edge = shared_edge_between_faces(left_face, right_face);
            if (!shared_edge.has_value()) {
                continue;
            }

            const MergeFaceUvAssignment* left_assignment = assigned_faces[left_index].assignment;
            const MergeFaceUvAssignment* right_assignment = assigned_faces[right_index].assignment;

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

[[nodiscard]] bool is_translation_texture_mapping(const Transform3 &mapping) {
  return mapping.x_axis.x == 1.0F && mapping.x_axis.y == 0.0F &&
         mapping.x_axis.z == 0.0F && mapping.y_axis.x == 0.0F &&
         mapping.y_axis.y == 1.0F && mapping.y_axis.z == 0.0F &&
         mapping.z_axis.x == 0.0F && mapping.z_axis.y == 0.0F &&
         mapping.z_axis.z == 1.0F;
}

[[nodiscard]] bool is_identity_texture_mapping(const Transform3 &mapping) {
  return is_translation_texture_mapping(mapping) && mapping.origin.x == 0.0F &&
         mapping.origin.y == 0.0F && mapping.origin.z == 0.0F;
}

/** Centered, column-scaled QR keeps narrow valid charts distinct from rank loss. */
struct TextureChartFit {
  std::vector<std::array<double, 2>> columns;
  double scale_x = 0.0;
  double scale_y = 0.0;
  double correlation = 0.0;
  double residual_norm = 0.0;

  [[nodiscard]] std::array<double, 2> sensitivity(std::size_t index) const {
    const double y = columns[index][1] / residual_norm;
    return {(columns[index][0] - correlation * y) / scale_x, y / scale_y};
  }

  [[nodiscard]] std::array<double, 4> fit(
      std::span<const quader::QVec2> values) const {
    std::array<double, 2> mean{};
    for (const auto value : values) {
      mean[0] += value.x;
      mean[1] += value.y;
    }
    mean[0] /= static_cast<double>(values.size());
    mean[1] /= static_cast<double>(values.size());
    std::array<double, 4> result{};
    for (std::size_t index = 0U; index < values.size(); ++index) {
      const auto weight = sensitivity(index);
      const double u = static_cast<double>(values[index].x) - mean[0];
      const double v = static_cast<double>(values[index].y) - mean[1];
      result[0] += weight[0] * u;
      result[1] += weight[1] * u;
      result[2] += weight[0] * v;
      result[3] += weight[1] * v;
    }
    return result;
  }
};

template <typename CoordinateAt>
[[nodiscard]] std::optional<TextureChartFit> prepare_texture_chart_fit(
    std::size_t count, CoordinateAt coordinate_at) {
  if (count < 3U) {
    return std::nullopt;
  }
  TextureChartFit fit;
  fit.columns.reserve(count);
  std::array<double, 2> mean{};
  for (std::size_t index = 0U; index < count; ++index) {
    const auto coordinate = coordinate_at(index);
    fit.columns.push_back(coordinate);
    mean[0] += coordinate[0];
    mean[1] += coordinate[1];
  }
  mean[0] /= static_cast<double>(count);
  mean[1] /= static_cast<double>(count);
  for (auto &column : fit.columns) {
    column[0] -= mean[0];
    column[1] -= mean[1];
    fit.scale_x += column[0] * column[0];
    fit.scale_y += column[1] * column[1];
  }
  fit.scale_x = std::sqrt(fit.scale_x);
  fit.scale_y = std::sqrt(fit.scale_y);
  if (!std::isfinite(fit.scale_x) || !std::isfinite(fit.scale_y) ||
      fit.scale_x == 0.0 || fit.scale_y == 0.0) {
    return std::nullopt;
  }
  for (auto &column : fit.columns) {
    column[0] /= fit.scale_x;
    column[1] /= fit.scale_y;
    fit.correlation += column[0] * column[1];
  }
  double correction = 0.0;
  for (auto &column : fit.columns) {
    column[1] -= fit.correlation * column[0];
    correction += column[0] * column[1];
  }
  fit.correlation += correction;
  for (auto &column : fit.columns) {
    column[1] -= correction * column[0];
    fit.residual_norm += column[1] * column[1];
  }
  fit.residual_norm = std::sqrt(fit.residual_norm);
  const double rank_roundoff = 32.0 * std::numeric_limits<double>::epsilon() *
                               static_cast<double>(count);
  if (!std::isfinite(fit.residual_norm) || fit.residual_norm <= rank_roundoff) {
    return std::nullopt;
  }
  for (auto &column : fit.columns) {
    column[1] /= fit.residual_norm;
  }
  return fit;
}

[[nodiscard]] std::optional<TextureChartFit> prepare_texture_chart_fit(
    std::span<const quader::QVec3> positions, quader::QVec3 normal) {
  return prepare_texture_chart_fit(positions.size(), [&](std::size_t index) {
    const auto q = generated_texture_uv(positions[index], normal);
    return std::array<double, 2>{q.x, q.y};
  });
}

[[nodiscard]] double texture_half_ulp(float value) noexcept {
  if (!std::isfinite(value)) {
    return std::numeric_limits<double>::infinity();
  }
  const double down = static_cast<double>(value) -
      std::nextafter(value, -std::numeric_limits<float>::infinity());
  const double up = static_cast<double>(std::nextafter(
      value, std::numeric_limits<float>::infinity())) - value;
  return 0.5 * std::max(std::isfinite(down) ? down : 0.0,
                        std::isfinite(up) ? up : 0.0);
}

} // namespace

bool texture_uv_coordinate_close(double fitted, float stored, double span) noexcept {
  const double tolerance = 8.0 * std::numeric_limits<float>::epsilon() *
                               std::max(1.0, std::abs(span)) +
                           texture_half_ulp(stored);
  return std::isfinite(fitted) && std::isfinite(stored) &&
         std::abs(fitted - stored) <= tolerance;
}

bool valid_circular_uv_interpolation(
    const CircularUvInterpolation &interpolation) noexcept {
  return quader_geometry::valid_circular_uv_frame(
      quader_geometry::QVec3f(interpolation.center), quader_geometry::QVec3f(interpolation.axis_u),
      quader_geometry::QVec3f(interpolation.axis_v), interpolation.uv_from_polar);
}

std::optional<std::array<float, 4>> circular_uv_coordinates(
    const CircularUvInterpolation &interpolation, quader::QVec3 position) noexcept {
  return quader_geometry::circular_uv_coordinates(quader_geometry::QVec3f(position),
      quader_geometry::QVec3f(interpolation.center), quader_geometry::QVec3f(interpolation.axis_u),
      quader_geometry::QVec3f(interpolation.axis_v));
}

quader::QVec2 evaluate_circular_uv_interpolation(
    quader::QVec2 triangle_uv, const std::array<float, 4> &coordinates,
    const std::array<float, 4> &uv_from_polar) noexcept {
  const auto uv = quader_geometry::evaluate_circular_uv_interpolation(
      quader_geometry::QVec2f(triangle_uv), coordinates, uv_from_polar);
  return {uv.x, uv.y};
}

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

std::optional<TextureAffineChart> fit_texture_affine_chart(
    std::span<const std::array<double, 2>> coordinates,
    std::span<const quader::QVec2> uvs) {
  if (coordinates.size() != uvs.size()) {
    return std::nullopt;
  }
  const auto fit = prepare_texture_chart_fit(coordinates.size(),
      [&](std::size_t index) { return coordinates[index]; });
  if (!fit.has_value()) {
    return std::nullopt;
  }
  TextureAffineChart chart{.carrier = fit->fit(uvs)};
  std::array<double, 2> mean_q{};
  std::array<double, 2> mean_uv{};
  for (std::size_t index = 0U; index < coordinates.size(); ++index) {
    mean_q[0] += coordinates[index][0];
    mean_q[1] += coordinates[index][1];
    mean_uv[0] += uvs[index].x;
    mean_uv[1] += uvs[index].y;
  }
  const double count = static_cast<double>(coordinates.size());
  chart.offset = {
      (mean_uv[0] - chart.carrier[0] * mean_q[0] - chart.carrier[1] * mean_q[1]) / count,
      (mean_uv[1] - chart.carrier[2] * mean_q[0] - chart.carrier[3] * mean_q[1]) / count};
  const auto finite = [](double value) { return std::isfinite(value); };
  return std::ranges::all_of(chart.carrier, finite) &&
         std::ranges::all_of(chart.offset, finite)
      ? std::optional{chart} : std::nullopt;
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
    const auto fit = prepare_texture_chart_fit(positions, geometric_normal);
    if (!fit.has_value()) {
      return {};
    }
    bool authored_generated_chart = true;
    for (std::size_t index = 0U; index < positions.size(); ++index) {
      const auto generated = generated_texture_uv(positions[index], geometric_normal);
      if (base_uvs[index].x != generated.x || base_uvs[index].y != generated.y) {
        authored_generated_chart = false;
        break;
      }
    }
    // Storing an unchanged procedural chart must retain its exact carrier.
    // Do not manufacture tiny cross-axis terms by refitting the identity map.
    // The rank gate above remains identical to every other authored chart.
    if (!authored_generated_chart) {
      carrier = fit->fit(base_uvs);
    }
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

namespace {

/** Affine generated-chart difference, shared by evaluation and its error bound. */
struct TextureMappingDelta {
  quader::QVec2 x;
  quader::QVec2 y;
  quader::QVec2 z;
  quader::QVec2 origin;
};

[[nodiscard]] TextureMappingDelta texture_mapping_coefficients(
    quader::QVec3 geometric_normal, const Transform3 &sampling_from_local) {
  if (is_translation_texture_mapping(sampling_from_local)) {
    // The generated chart is linear for a fixed normal. Evaluate translation
    // directly so rounded (p + t) - p cannot invent a varying chart gradient.
    return {.origin = generated_texture_uv(sampling_from_local.origin,
                                            geometric_normal)};
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
  // Difference the projection coefficients before multiplying large positions.
  // This is the same affine evaluation used by the material shaders.
  const auto x = generated_texture_uv(sampling_from_local.x_axis, mapped_normal) -
                 generated_texture_uv({1.0F, 0.0F, 0.0F}, geometric_normal);
  const auto y = generated_texture_uv(sampling_from_local.y_axis, mapped_normal) -
                 generated_texture_uv({0.0F, 1.0F, 0.0F}, geometric_normal);
  const auto z = generated_texture_uv(sampling_from_local.z_axis, mapped_normal) -
                 generated_texture_uv({0.0F, 0.0F, 1.0F}, geometric_normal);
  return {x, y, z,
          generated_texture_uv(sampling_from_local.origin, mapped_normal)};
}

[[nodiscard]] quader::QVec2 texture_mapping_delta(
    quader::QVec3 position, quader::QVec3 geometric_normal,
    const Transform3 &sampling_from_local) {
  const auto coefficients = texture_mapping_coefficients(geometric_normal,
                                                         sampling_from_local);
  if (is_translation_texture_mapping(sampling_from_local)) {
    return coefficients.origin;
  }
  return coefficients.x * position.x + coefficients.y * position.y +
         coefficients.z * position.z + coefficients.origin;
}

[[nodiscard]] double delta_evaluation_roundoff(
    float x, float y, float z, float origin, quader::QVec3 position) {
  const float px = x * position.x;
  const float py = y * position.y;
  const float pz = z * position.z;
  // Zero coefficients and additions have exact results; do not charge the
  // translation fast path with uncertainty in operations it never performs.
  const float xy = px + py;
  const float xyz = xy + pz;
  return (x != 0.0F ? texture_half_ulp(px) : 0.0) +
         (y != 0.0F ? texture_half_ulp(py) : 0.0) +
         (z != 0.0F ? texture_half_ulp(pz) : 0.0) +
         (px != 0.0F && py != 0.0F ? texture_half_ulp(xy) : 0.0) +
         (xy != 0.0F && pz != 0.0F ? texture_half_ulp(xyz) : 0.0) +
         (xyz != 0.0F && origin != 0.0F ? texture_half_ulp(xyz + origin) : 0.0);
}

} // namespace

quader::QVec2 evaluate_texture_mapping(
    quader::QVec2 uv0, quader::QVec3 position,
    quader::QVec3 geometric_normal, const std::array<float, 4> &carrier,
    const Transform3 &sampling_from_local) {
  if (is_identity_texture_mapping(sampling_from_local) ||
      carrier == std::array<float, 4>{}) {
    return uv0;
  }
  const auto delta = texture_mapping_delta(position, geometric_normal,
                                            sampling_from_local);
  return {uv0.x + carrier[0] * delta.x + carrier[1] * delta.y,
          uv0.y + carrier[2] * delta.x + carrier[3] * delta.y};
}

std::optional<TextureAffineChart> fit_face_texture_affine_chart(
    std::span<const std::array<double, 2>> coordinates,
    std::span<const quader::QVec3> positions,
    std::span<const quader::QVec2> base_uvs,
    quader::QVec3 geometric_normal, const FaceTextureTransform &transform,
    const Transform3 &sampling_from_local) {
  if (coordinates.size() != positions.size() ||
      (!base_uvs.empty() && base_uvs.size() != positions.size()) ||
      !valid_face_texture_transform(transform)) {
    return std::nullopt;
  }
  const auto fit = prepare_texture_chart_fit(coordinates.size(),
      [&](std::size_t index) { return coordinates[index]; });
  if (!fit.has_value()) {
    return std::nullopt;
  }
  const auto linear = texture_linear_transform(transform);
  const auto carrier = face_texture_carrier(positions, base_uvs,
                                             geometric_normal, transform);
  const auto correction = texture_mapping_coefficients(geometric_normal,
                                                        sampling_from_local);
  const auto base_at = [&](std::size_t index) {
    return base_uvs.empty() ? generated_texture_uv(positions[index], geometric_normal)
                            : base_uvs[index];
  };
  const auto first = base_at(0U);
  std::vector<std::array<double, 2>> differences;
  differences.reserve(positions.size());
  std::array<double, 2> mean_uv{};
  std::array<double, 2> mean_q{};
  for (std::size_t index = 0U; index < positions.size(); ++index) {
    const auto base = base_at(index);
    const double u = static_cast<double>(base.x) - first.x;
    const double v = static_cast<double>(base.y) - first.y;
    const double x = static_cast<double>(positions[index].x) - positions[0].x;
    const double y = static_cast<double>(positions[index].y) - positions[0].y;
    const double z = static_cast<double>(positions[index].z) - positions[0].z;
    const double du = correction.x.x * x + correction.y.x * y + correction.z.x * z;
    const double dv = correction.x.y * x + correction.y.y * y + correction.z.y * z;
    const std::array difference{
        linear[0] * u + linear[1] * v + carrier[0] * du + carrier[1] * dv,
        linear[2] * u + linear[3] * v + carrier[2] * du + carrier[3] * dv};
    differences.push_back(difference);
    for (std::size_t axis = 0U; axis < 2U; ++axis) {
      mean_uv[axis] += difference[axis] / static_cast<double>(positions.size());
      mean_q[axis] += coordinates[index][axis] / static_cast<double>(positions.size());
    }
  }
  TextureAffineChart chart{};
  for (std::size_t index = 0U; index < differences.size(); ++index) {
    const auto weight = fit->sensitivity(index);
    for (std::size_t axis = 0U; axis < 2U; ++axis) {
      const double centered = differences[index][axis] - mean_uv[axis];
      chart.carrier[axis * 2U] += weight[0] * centered;
      chart.carrier[axis * 2U + 1U] += weight[1] * centered;
    }
  }
  const auto position = positions.front();
  const double du = static_cast<double>(correction.x.x) * position.x +
      static_cast<double>(correction.y.x) * position.y +
      static_cast<double>(correction.z.x) * position.z + correction.origin.x;
  const double dv = static_cast<double>(correction.x.y) * position.x +
      static_cast<double>(correction.y.y) * position.y +
      static_cast<double>(correction.z.y) * position.z + correction.origin.y;
  const std::array origin{
      linear[0] * first.x + linear[1] * first.y + transform.shift_uv.x +
          carrier[0] * du + carrier[1] * dv,
      linear[2] * first.x + linear[3] * first.y + transform.shift_uv.y +
          carrier[2] * du + carrier[3] * dv};
  for (std::size_t axis = 0U; axis < 2U; ++axis) {
    chart.offset[axis] = origin[axis] + mean_uv[axis] -
        chart.carrier[axis * 2U] * mean_q[0] -
        chart.carrier[axis * 2U + 1U] * mean_q[1];
  }
  const auto finite = [](double value) { return std::isfinite(value); };
  return std::ranges::all_of(chart.carrier, finite) &&
         std::ranges::all_of(chart.offset, finite)
      ? std::optional{chart} : std::nullopt;
}

std::vector<std::array<double, 2>> face_texture_precision_bounds(
    std::span<const quader::QVec3> positions,
    std::span<const quader::QVec2> base_uvs, quader::QVec3 geometric_normal,
    const FaceTextureTransform &transform, const Transform3 &sampling_from_local,
    TextureUvPrecision precision) {
  if ((!base_uvs.empty() && positions.size() != base_uvs.size()) ||
      !valid_face_texture_transform(transform)) {
    return {};
  }
  const auto fit = prepare_texture_chart_fit(positions, geometric_normal);
  if (!fit.has_value()) {
    return {};
  }
  const auto linear = texture_linear_transform(transform);
  const auto carrier = face_texture_carrier(positions, base_uvs, geometric_normal,
                                           transform);
  const auto coefficients = texture_mapping_coefficients(geometric_normal,
                                                         sampling_from_local);
  const bool mapped = !is_identity_texture_mapping(sampling_from_local) &&
                      carrier != std::array<float, 4>{};
  std::vector<std::array<double, 2>> input_error(positions.size());
  std::array<double, 4> carrier_error{};
  for (std::size_t index = 0U; index < positions.size(); ++index) {
    if (!base_uvs.empty()) {
      const double u = texture_half_ulp(base_uvs[index].x);
      const double v = texture_half_ulp(base_uvs[index].y);
      input_error[index] = {std::abs(linear[0]) * u + std::abs(linear[1]) * v,
                            std::abs(linear[2]) * u + std::abs(linear[3]) * v};
      const auto weight = fit->sensitivity(index);
      carrier_error[0] += std::abs(weight[0]) * input_error[index][0];
      carrier_error[1] += std::abs(weight[1]) * input_error[index][0];
      carrier_error[2] += std::abs(weight[0]) * input_error[index][1];
      carrier_error[3] += std::abs(weight[1]) * input_error[index][1];
    }
  }
  for (std::size_t component = 0U; component < carrier.size(); ++component) {
    carrier_error[component] += texture_half_ulp(carrier[component]);
  }
  auto result = input_error;
  for (std::size_t index = 0U; index < positions.size(); ++index) {
    const auto base = base_uvs.empty()
        ? generated_texture_uv(positions[index], geometric_normal) : base_uvs[index];
    const auto uv = apply_face_texture_transform(base, transform);
    if (!(transform == FaceTextureTransform{})) {
      result[index][0] += texture_half_ulp(uv.x);
      result[index][1] += texture_half_ulp(uv.y);
    }
    if (!mapped) {
      continue;
    }
    const auto delta = texture_mapping_delta(positions[index], geometric_normal,
                                             sampling_from_local);
    const std::array<double, 2> delta_error{
        delta_evaluation_roundoff(coefficients.x.x, coefficients.y.x,
            coefficients.z.x, coefficients.origin.x, positions[index]),
        delta_evaluation_roundoff(coefficients.x.y, coefficients.y.y,
            coefficients.z.y, coefficients.origin.y, positions[index])};
    for (std::size_t axis = 0U; axis < 2U; ++axis) {
      const float value = axis == 0U ? uv.x : uv.y;
      const float cx = carrier[axis * 2U];
      const float cy = carrier[axis * 2U + 1U];
      const float first = cx * delta.x;
      const float second = cy * delta.y;
      const float sum = value + first;
      result[index][axis] += std::abs(cx) * delta_error[0] +
                             std::abs(cy) * delta_error[1];
      if (first != 0.0F) {
        result[index][axis] += texture_half_ulp(first) + texture_half_ulp(sum);
      }
      if (second != 0.0F) {
        result[index][axis] += texture_half_ulp(second) + texture_half_ulp(sum + second);
      }
      if (precision == TextureUvPrecision::Reconstruction) {
        result[index][axis] +=
            std::abs(delta.x) * carrier_error[axis * 2U] +
            std::abs(delta.y) * carrier_error[axis * 2U + 1U];
      }
    }
  }
  if (precision == TextureUvPrecision::AffineResidual) {
    // The affine projector removes carrier quantization, including potentially
    // large translated phase terms. Only non-affine corner uncertainty remains.
    const auto local_error = result;
    const double inverse_count = 1.0 / static_cast<double>(positions.size());
    for (std::size_t row = 0U; row < positions.size(); ++row) {
      result[row] = {};
      for (std::size_t column = 0U; column < positions.size(); ++column) {
        const double projector = inverse_count +
            fit->columns[row][0] * fit->columns[column][0] +
            fit->columns[row][1] * fit->columns[column][1];
        const double residual_weight =
            std::abs((row == column ? 1.0 : 0.0) - projector);
        result[row][0] += residual_weight * local_error[column][0];
        result[row][1] += residual_weight * local_error[column][1];
      }
    }
  }
  // Storage uncertainty is not permission to accept visibly wrong charts.
  // Preserve the solver's existing sub-millitexel ceiling even when a very
  // narrow face or distant occurrence amplifies its mathematical error bound.
  constexpr double kMaximumStorageError = 5.0e-4;
  for (auto &corner : result) {
    for (auto &axis : corner) {
      if (std::isfinite(axis)) {
        axis = std::min(axis, kMaximumStorageError);
      }
    }
  }
  return result;
}

bool face_texture_uvs_reconstruct(
    std::span<const quader::QVec3> positions,
    std::span<const quader::QVec2> base_uvs,
    std::span<const quader::QVec2> desired_uvs, quader::QVec3 geometric_normal,
    const FaceTextureTransform &transform, const Transform3 &sampling_from_local) {
  if (positions.size() != desired_uvs.size() || positions.empty()) {
    return false;
  }
  const auto bounds = face_texture_precision_bounds(
      positions, base_uvs, geometric_normal, transform, sampling_from_local,
      TextureUvPrecision::Reconstruction);
  if (bounds.size() != positions.size()) {
    return false;
  }
  auto minimum = desired_uvs.front();
  auto maximum = minimum;
  for (const auto desired : desired_uvs) {
    minimum.x = std::min(minimum.x, desired.x);
    minimum.y = std::min(minimum.y, desired.y);
    maximum.x = std::max(maximum.x, desired.x);
    maximum.y = std::max(maximum.y, desired.y);
  }
  const std::array<double, 2> span{static_cast<double>(maximum.x) - minimum.x,
                                  static_cast<double>(maximum.y) - minimum.y};
  const auto carrier = face_texture_carrier(positions, base_uvs, geometric_normal,
                                           transform);
  for (std::size_t index = 0U; index < positions.size(); ++index) {
    const auto base = base_uvs.empty()
        ? generated_texture_uv(positions[index], geometric_normal) : base_uvs[index];
    const auto actual = evaluate_texture_mapping(
        apply_face_texture_transform(base, transform), positions[index],
        geometric_normal, carrier, sampling_from_local);
    for (std::size_t axis = 0U; axis < 2U; ++axis) {
      const double value = axis == 0U ? actual.x : actual.y;
      const float desired = axis == 0U ? desired_uvs[index].x : desired_uvs[index].y;
      const double error = value - desired;
      const double uncertainty = bounds[index][axis];
      if (!std::isfinite(value) || !std::isfinite(desired) ||
          !std::isfinite(uncertainty) ||
          (std::abs(error) > uncertainty &&
           !texture_uv_coordinate_close(value - std::copysign(uncertainty, error),
                                         desired, span[axis]))) {
        return false;
      }
    }
  }
  return true;
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
  std::optional<std::vector<quader::QVec2>> certified_uvs;
  std::vector<quader::QVec2> deltas;
  std::optional<TextureChartFit> inverse_fit;
  std::array<double, 4> inverse_mapping{};
  const auto inverse_carrier = [&](const std::array<double, 4> &carrier) {
    return std::array<double, 4>{
        carrier[0] * inverse_mapping[0] + carrier[1] * inverse_mapping[2],
        carrier[0] * inverse_mapping[1] + carrier[1] * inverse_mapping[3],
        carrier[2] * inverse_mapping[0] + carrier[3] * inverse_mapping[2],
        carrier[2] * inverse_mapping[1] + carrier[3] * inverse_mapping[3]};
  };
  if (is_identity_texture_mapping(sampling_from_local)) {
    for (const auto uv : effective_uvs) {
      const auto base = unapply_face_texture_transform(uv, transform);
      if (!finite_uv(uv) || !finite_uv(base)) {
        return std::nullopt;
      }
      base_uvs.push_back(base);
    }
  } else {
    deltas.reserve(positions.size());
    for (std::size_t index = 0U; index < positions.size(); ++index) {
      const auto position = positions[index];
      const auto delta = texture_mapping_delta(position, geometric_normal,
                                                 sampling_from_local);
      if (!finite_uv(delta) || !finite_uv(effective_uvs[index])) {
        return std::nullopt;
      }
      deltas.push_back(delta);
    }
    inverse_fit = prepare_texture_chart_fit(positions, geometric_normal);
    if (!inverse_fit.has_value()) {
      return std::nullopt;
    }
    // The inverse identity requires the linear, unrounded fit. Quantizing
    // either fitted operand before inversion introduces a slope error that
    // the forward storage/evaluation precision contract does not cover.
    const auto delta_carrier = inverse_fit->fit(deltas);
    const auto target_carrier = inverse_fit->fit(effective_uvs);
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
    const double matrix_norm =
        std::max(std::abs(a) + std::abs(b), std::abs(c) + std::abs(d));
    const double inverse_norm =
        std::max(std::abs(d) + std::abs(b),
                 std::abs(c) + std::abs(a)) /
        std::abs(determinant);
    if (!std::isfinite(matrix_norm * inverse_norm)) {
      return std::nullopt;
    }
    const double inverse = 1.0 / (determinant * scale);
    inverse_mapping = {d * inverse, -b * inverse, -c * inverse, a * inverse};
    const auto carrier = inverse_carrier(target_carrier);
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
  if (!is_identity_texture_mapping(sampling_from_local)) {
    // Quantizing the stored chart changes its fitted carrier slightly. A
    // distant occurrence multiplies that difference into a phase residual,
    // especially on thin faces. Correct against the actual float evaluator;
    // retain the best candidate and leave the strict reconstruction test below
    // responsible for accepting it. Neither texture settings nor rank change.
    const auto retain_certified = [&](const std::vector<quader::QVec2> &candidate) {
      if (face_texture_uvs_reconstruct(positions, candidate, effective_uvs,
              geometric_normal, transform, sampling_from_local)) {
        certified_uvs = candidate;
      }
    };
    retain_certified(base_uvs);
    const auto measure = [&](std::span<const quader::QVec2> candidate) {
      const auto carrier = face_texture_carrier(
          positions, candidate, geometric_normal, transform);
      std::array<double, 4> result{};
      for (std::size_t index = 0U; index < positions.size(); ++index) {
        const auto actual = evaluate_texture_mapping(
            apply_face_texture_transform(candidate[index], transform),
            positions[index], geometric_normal, carrier, sampling_from_local);
        const double x = static_cast<double>(effective_uvs[index].x) - actual.x;
        const double y = static_cast<double>(effective_uvs[index].y) - actual.y;
        if (!finite_uv(candidate[index]) || !std::isfinite(x) || !std::isfinite(y)) {
          return std::array<double, 4>{0.0, 0.0,
              std::numeric_limits<double>::infinity(),
              std::numeric_limits<double>::infinity()};
        }
        result[0] += x;
        result[1] += y;
        result[2] = std::max({result[2], std::abs(x), std::abs(y)});
        result[3] += x * x + y * y;
      }
      return result;
    };
    FaceTextureTransform linear_transform = transform;
    linear_transform.shift_uv = {};
    auto score = measure(base_uvs);
    auto candidate = base_uvs;
    auto best = base_uvs;
    std::vector<quader::QVec2> residuals(positions.size());
    const bool translation = is_translation_texture_mapping(sampling_from_local);
    constexpr std::size_t kMaximumStorageCorrections = 8U;
    for (std::size_t iteration = 0U; iteration < kMaximumStorageCorrections;
         ++iteration) {
      if (score[2] == 0.0 || !std::isfinite(score[3])) {
        break;
      }
      auto best_score = score;
      const double count = static_cast<double>(positions.size());
      if (!translation) {
        const auto carrier = face_texture_carrier(
            positions, base_uvs, geometric_normal, transform);
        for (std::size_t index = 0U; index < positions.size(); ++index) {
          const auto actual = evaluate_texture_mapping(
              apply_face_texture_transform(base_uvs[index], transform),
              positions[index], geometric_normal, carrier, sampling_from_local);
          residuals[index] = effective_uvs[index] - actual;
        }
      }
      const auto residual_carrier = translation ? std::array<double, 4>{}
          : inverse_carrier(inverse_fit->fit(residuals));
      // Float binades differ between U and V. Test independent phase corrections
      // as well as their combination so rounding in one axis cannot discard an
      // improvement in the other. The constant correction preserves the chart's
      // ideal slopes; a general occurrence also tests the inverse of its full
      // measured residual, retaining non-affine corner detail.
      for (unsigned axes = 1U; axes <= 3U; ++axes) {
        const auto correction = unapply_face_texture_transform(
            {static_cast<float>((axes & 1U) != 0U ? score[0] / count : 0.0),
             static_cast<float>((axes & 2U) != 0U ? score[1] / count : 0.0)},
            linear_transform);
        for (std::size_t index = 0U; index < base_uvs.size(); ++index) {
          candidate[index] = base_uvs[index] + correction;
        }
        const auto candidate_score = measure(candidate);
        if (candidate_score[2] < best_score[2] ||
            (candidate_score[2] == best_score[2] && candidate_score[3] < best_score[3])) {
          best_score = candidate_score;
          best = candidate;
          retain_certified(candidate);
        }
        if (translation) {
          continue;
        }
        for (std::size_t index = 0U; index < positions.size(); ++index) {
          const auto delta = deltas[index];
          const auto residual = residuals[index];
          const quader::QVec2 local_correction{
              static_cast<float>((axes & 1U) != 0U
                  ? residual.x - residual_carrier[0] * delta.x -
                                     residual_carrier[1] * delta.y : 0.0),
              static_cast<float>((axes & 2U) != 0U
                  ? residual.y - residual_carrier[2] * delta.x -
                                     residual_carrier[3] * delta.y : 0.0)};
          candidate[index] = base_uvs[index] + unapply_face_texture_transform(
              local_correction, linear_transform);
        }
        const auto refined_score = measure(candidate);
        if (refined_score[2] < best_score[2] ||
            (refined_score[2] == best_score[2] && refined_score[3] < best_score[3])) {
          best_score = refined_score;
          best = candidate;
          retain_certified(candidate);
        }
      }
      if (best_score[2] == score[2] && best_score[3] == score[3]) {
        break;
      }
      base_uvs = best;
      score = best_score;
    }
  }
  if (!face_texture_uvs_reconstruct(positions, base_uvs, effective_uvs,
                                    geometric_normal, transform,
                                    sampling_from_local)) {
    // Minimizing the largest raw residual need not improve every corner's
    // individual bound. A previously certified stored candidate stays valid.
    return certified_uvs;
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

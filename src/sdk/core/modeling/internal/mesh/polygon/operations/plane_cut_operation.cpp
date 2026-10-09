////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/neutral/polygon/document.hpp>
#include <quader/modeling/neutral/polygon/poly_operation.hpp>
#include <quader/modeling/neutral/polygon/poly_operation_descriptor.hpp>
#include <diagnostics/profile.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_bridge_surface_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_hull_plane_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_knife_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_mesh_internal.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_topology_helpers.hpp>

#include <quader/modeling/neutral/geometry/geometry.hpp>
#include <quader/modeling/neutral/geometry/polygon_triangulation.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <string_view>

namespace quader_poly {

/**
 * Implements the Plane Cut Operation modeling operation for the polygon document and mesh editing core.
 */
class PlaneCutOperation final : public PolyOperation {
public:
    explicit PlaneCutOperation(PlaneCutRequest request);

    [[nodiscard]] std::string_view id() const override { return poly_operation_descriptor(PolyOperationKey::PlaneCut).id; }
    [[nodiscard]] std::string_view label() const override { return poly_operation_descriptor(PolyOperationKey::PlaneCut).label; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;

private:
    PlaneCutRequest request_;
};

using namespace document_internal;

namespace {

using namespace document_internal;

inline constexpr float kPlaneCutEpsilon = 0.0001F;
inline constexpr float kPlaneCutEpsilonSquared =
    kPlaneCutEpsilon * kPlaneCutEpsilon;

using PlaneCutPlane = quader_geometry::QPlane3<float>;

/**
 * Represents a Plane Cut Vertex value used by the polygon document and mesh editing core.
 */
struct PlaneCutVertex {
  ElementId id = kInvalidElementId;
  quader::QVec3 position;
  bool has_uv = false;
  quader::QVec2 uv;
};

/**
 * Represents a Plane Cut Point value used by the polygon document and mesh editing core.
 */
struct PlaneCutPoint {
  ElementId id = kInvalidElementId;
  quader::QVec3 position;
  bool has_uv = false;
  quader::QVec2 uv;
  bool on_plane = false;
};

/**
 * Represents a Plane Cut Intersection value used by the polygon document and mesh editing core.
 */
struct PlaneCutIntersection {
  ElementId id = kInvalidElementId;
  quader::QVec3 position;
};

/**
 * Represents a Plane Cut Builder value used by the polygon document and mesh editing core.
 */
struct PlaneCutBuilder {
    Document document;
    Selection selection;
    std::set<ElementId> emitted_vertices;
    std::vector<ElementId> cap_face_ids;
};

/**
 * Represents a Plane Cut Work value used by the polygon document and mesh editing core.
 */
struct PlaneCutWork {
    PlaneCutPlane plane;
    std::map<ElementId, PlaneCutVertex> source_vertices;
    std::map<std::pair<ElementId, ElementId>, PlaneCutIntersection> intersections;
    std::vector<Edge> cut_segments;
    ElementId next_vertex_id = 1;
};

std::optional<PlaneCutPlane> plane_from_request(const PlaneCutRequest& request)
{
    const quader::QVec3 first_delta = request.second_point - request.first_point;
    const quader::QVec3 second_delta = request.third_point - request.first_point;
    const quader_geometry::QVec3f normal =
        quader_geometry::cross(geometry_vec3(first_delta), geometry_vec3(second_delta));
    const PlaneCutPlane plane = quader_geometry::plane_from_point_normal<float>(
        geometry_vec3(request.first_point), normal, kEpsilon);
    if (quader_geometry::length_squared(plane.normal) <=
        kPlaneCutEpsilonSquared) {
      return std::nullopt;
    }
    return plane;
}

float signed_distance_to_cut_plane(const PlaneCutPlane& plane, quader::QVec3 point)
{
    return quader_geometry::signed_distance_to_plane<float>(geometry_vec3(point), plane);
}

int plane_side(float distance)
{
  if (distance > kPlaneCutEpsilon) {
    return 1;
  }
  if (distance < -kPlaneCutEpsilon) {
    return -1;
  }
    return 0;
}

float cut_edge_factor(const PlaneCutPlane& plane,
                      quader::QVec3 first_position,
                      quader::QVec3 second_position, float first_distance,
                      float second_distance);

quader::QVec2 interpolate_uv(quader::QVec2 first, quader::QVec2 second, float factor)
{
    return {
        first.x + ((second.x - first.x) * factor),
        first.y + ((second.y - first.y) * factor),
    };
}

float cut_edge_factor(const PlaneCutPlane& plane, quader::QVec3 first_position, quader::QVec3 second_position, float first_distance, float second_distance)
{
  const quader_geometry::QSegmentPlaneHit<float> hit =
      quader_geometry::intersect_segment_plane<float>(
          {geometry_vec3(first_position), geometry_vec3(second_position)},
          plane, kPlaneCutEpsilon);
  if (hit.type == quader_geometry::QPlaneIntersectionType::Point) {
    return std::clamp(hit.segment_factor, 0.0F, 1.0F);
  }
  if (hit.type == quader_geometry::QPlaneIntersectionType::Coplanar) {
    return 0.5F;
  }

    (void)first_distance;
    (void)second_distance;
    return 0.5F;
}

void ensure_builder_vertex(PlaneCutBuilder& builder, ElementId id, quader::QVec3 position)
{
  if (id == kInvalidElementId || builder.emitted_vertices.contains(id)) {
    return;
  }
    builder.document.vertices.push_back({ id, position });
    builder.document.next_vertex_id = std::max(builder.document.next_vertex_id, id + 1U);
    builder.emitted_vertices.insert(id);
}

PlaneCutPoint source_point_for_face_vertex(
    const PlaneCutWork& work,
    const Face& face,
    std::size_t vertex_index,
    bool has_loop_uvs)
{
    const ElementId vertex_id = face.vertices[vertex_index];
    const auto found = work.source_vertices.find(vertex_id);
    PlaneCutPoint point;
    point.id = vertex_id;
    if (found != work.source_vertices.end()) {
        point.position = found->second.position;
    }
    point.on_plane = plane_side(signed_distance_to_cut_plane(work.plane, point.position)) == 0;
    point.has_uv = has_loop_uvs;
    if (has_loop_uvs) {
        point.uv = face.uvs[vertex_index];
    }
    return point;
}

PlaneCutIntersection intersection_for_edge(
    PlaneCutWork& work,
    ElementId first_id,
    ElementId second_id,
    quader::QVec3 first_position,
    quader::QVec3 second_position,
    float first_distance,
    float second_distance)
{
    const Edge edge = make_edge(first_id, second_id);
    const std::pair<ElementId, ElementId> key { edge.a, edge.b };
    const auto found = work.intersections.find(key);
    if (found != work.intersections.end()) {
        return found->second;
    }

    const float factor = cut_edge_factor(work.plane, first_position, second_position, first_distance, second_distance);
    PlaneCutIntersection intersection;
    intersection.id = work.next_vertex_id++;
    intersection.position = first_position + ((second_position - first_position) * factor);
    work.intersections[key] = intersection;
    return intersection;
}

PlaneCutPoint intersection_point_for_edge(
    PlaneCutWork& work,
    const Face& face,
    std::size_t first_index,
    std::size_t second_index,
    float first_distance,
    float second_distance,
    bool has_loop_uvs)
{
    const PlaneCutPoint first = source_point_for_face_vertex(work, face, first_index, has_loop_uvs);
    const PlaneCutPoint second = source_point_for_face_vertex(work, face, second_index, has_loop_uvs);
    const PlaneCutIntersection intersection = intersection_for_edge(
        work,
        first.id,
        second.id,
        first.position,
        second.position,
        first_distance,
        second_distance);

    const float factor = cut_edge_factor(work.plane, first.position, second.position, first_distance, second_distance);
    PlaneCutPoint point;
    point.id = intersection.id;
    point.position = intersection.position;
    point.on_plane = true;
    point.has_uv = has_loop_uvs;
    if (has_loop_uvs) {
        point.uv = interpolate_uv(first.uv, second.uv, factor);
    }
    return point;
}

std::vector<PlaneCutPoint> compact_output_points(std::vector<PlaneCutPoint> points)
{
    std::vector<PlaneCutPoint> compact;
    compact.reserve(points.size());
    for (const PlaneCutPoint& point : points) {
      if (point.id == kInvalidElementId) {
        continue;
      }
        if (!compact.empty() && compact.back().id == point.id) {
            continue;
        }
        compact.push_back(point);
    }
    if (compact.size() > 1U && compact.front().id == compact.back().id) {
        compact.pop_back();
    }

    std::set<ElementId> seen;
    for (const PlaneCutPoint& point : compact) {
        if (!seen.insert(point.id).second) {
            return {};
        }
    }
    return compact;
}

bool append_face_from_points(
    PlaneCutBuilder& builder,
    const Face& source_face,
    std::vector<PlaneCutPoint> points,
    std::string& message,
    std::optional<ElementId> face_id = std::nullopt)
{
    points = compact_output_points(std::move(points));
    if (points.empty()) {
        return true;
    }
    if (points.size() < 3U) {
        message = "Cut would create a degenerate face.";
        return false;
    }

    Face face;
    face.id = face_id.value_or(source_face.id);
    face.material_slot = source_face.material_slot;
    face.texture_transform = source_face.texture_transform;
    face.normal_shading = source_face.normal_shading;
    face.vertices.reserve(points.size());
    const bool has_uvs = std::ranges::all_of(points, [](const PlaneCutPoint& point) {
        return point.has_uv;
    });
    if (has_uvs) {
        face.uvs.reserve(points.size());
    }

    for (const PlaneCutPoint& point : points) {
        ensure_builder_vertex(builder, point.id, point.position);
        face.vertices.push_back(point.id);
        if (has_uvs) {
            face.uvs.push_back(point.uv);
        }
    }

    if (triangulate_face_local_indices(builder.document, face).empty()) {
        message = "Cut would create a face that cannot be triangulated.";
        return false;
    }
    builder.document.faces.push_back(std::move(face));
    return true;
}

using PlaneFaceLoop = std::vector<PlaneCutPoint>;

struct PlaneFaceParts {
    std::vector<PlaneFaceLoop> front;
    std::vector<PlaneFaceLoop> back;
};

bool face_parts_by_plane(PlaneCutWork& work, const Face& face,
                         PlaneFaceParts& parts, std::string& message)
{
    if (face.vertices.size() < 3U) {
        message = "Cut requires valid face loops.";
        return false;
    }
    const bool has_uvs = face_has_loop_uvs(face);
    PlaneFaceLoop boundary;
    std::vector<float> distances;
    bool has_front = false;
    bool has_back = false;
    for (std::size_t index = 0; index < face.vertices.size(); ++index) {
        const auto vertex = work.source_vertices.find(face.vertices[index]);
        if (vertex == work.source_vertices.end()) {
            message = "Cut could not find a face vertex.";
            return false;
        }
        const float distance = signed_distance_to_cut_plane(work.plane, vertex->second.position);
        if (!std::isfinite(distance)) {
            message = "Cut requires finite face positions.";
            return false;
        }
        distances.push_back(distance);
        has_front = has_front || plane_side(distance) > 0;
        has_back = has_back || plane_side(distance) < 0;
        boundary.push_back(source_point_for_face_vertex(work, face, index, has_uvs));
    }
    if (!has_front || !has_back) {
        if (has_front || !has_back) {
            parts.front.push_back(boundary);
        }
        if (has_back) {
            parts.back.push_back(std::move(boundary));
        }
        return true;
    }

    PlaneFaceLoop expanded;
    for (std::size_t index = 0; index < boundary.size(); ++index) {
        const auto next = (index + 1U) % boundary.size();
        expanded.push_back(boundary[index]);
        if (plane_side(distances[index]) * plane_side(distances[next]) == -1) {
            expanded.push_back(intersection_point_for_edge(
                work, face, index, next, distances[index], distances[next], has_uvs));
        }
    }
    Document local;
    Face expanded_face = face;
    expanded_face.vertices.clear();
    expanded_face.uvs.clear();
    std::map<ElementId, PlaneCutPoint> points;
    std::map<ElementId, std::size_t> boundary_order;
    std::vector<ElementId> on_plane;
    for (const auto& point : expanded) {
        local.vertices.push_back({point.id, point.position});
        boundary_order.emplace(point.id, expanded_face.vertices.size());
        expanded_face.vertices.push_back(point.id);
        points.emplace(point.id, point);
        if (point.has_uv) {
            expanded_face.uvs.push_back(point.uv);
        }
        if (point.on_plane) {
            on_plane.push_back(point.id);
        }
    }
    local.faces.push_back(expanded_face);
    if (on_plane.size() < 2U) {
        message = "Cut could not resolve the face intersection.";
        return false;
    }

    // The plane meets a planar face along a line. Consecutive points along
    // that line bound potential interior intervals; concave gaps are excluded
    // by the existing authored-boundary chord predicate.
    quader::QVec3 minimum = points.at(on_plane.front()).position;
    quader::QVec3 maximum = minimum;
    for (const auto id : on_plane) {
        const auto position = points.at(id).position;
        minimum = {std::min(minimum.x, position.x), std::min(minimum.y, position.y),
                   std::min(minimum.z, position.z)};
        maximum = {std::max(maximum.x, position.x), std::max(maximum.y, position.y),
                   std::max(maximum.z, position.z)};
    }
    const auto extent = maximum - minimum;
    const int axis = extent.x >= extent.y && extent.x >= extent.z ? 0
                     : extent.y >= extent.z ? 1 : 2;
    const auto coordinate = [axis](quader::QVec3 point) {
        return axis == 0 ? point.x : axis == 1 ? point.y : point.z;
    };
    std::ranges::sort(on_plane, [&](ElementId first, ElementId second) {
        const float a = coordinate(points.at(first).position);
        const float b = coordinate(points.at(second).position);
        return a == b ? first < second : a < b;
    });

    std::vector<std::vector<ElementId>> loops{expanded_face.vertices};
    std::size_t chord_count = 0;
    for (std::size_t index = 1; index < on_plane.size(); ++index) {
        const auto first = on_plane[index - 1U];
        const auto second = on_plane[index];
        if (face_uses_edge(expanded_face, make_edge(first, second)) ||
            !authored_face_chord_is_valid(local, expanded_face, first, second)) {
            continue;
        }
        bool partitioned = false;
        for (std::size_t loop_index = 0; loop_index < loops.size(); ++loop_index) {
            const auto& loop = loops[loop_index];
            const auto first_position = std::ranges::find(loop, first);
            const auto second_position = std::ranges::find(loop, second);
            if (first_position == loop.end() || second_position == loop.end()) {
                continue;
            }
            const auto first_index = static_cast<std::size_t>(first_position - loop.begin());
            const auto second_index = static_cast<std::size_t>(second_position - loop.begin());
            if ((first_index + 1U) % loop.size() == second_index ||
                (second_index + 1U) % loop.size() == first_index) {
                continue;
            }
            std::vector<ElementId> first_loop;
            std::vector<ElementId> second_loop;
            for (std::size_t cursor = first_index;; cursor = (cursor + 1U) % loop.size()) {
                first_loop.push_back(loop[cursor]);
                if (cursor == second_index) {
                    break;
                }
            }
            for (std::size_t cursor = second_index;; cursor = (cursor + 1U) % loop.size()) {
                second_loop.push_back(loop[cursor]);
                if (cursor == first_index) {
                    break;
                }
            }
            loops[loop_index] = std::move(first_loop);
            loops.push_back(std::move(second_loop));
            add_unique_edge(work.cut_segments, make_edge(first, second));
            ++chord_count;
            partitioned = true;
            break;
        }
        if (!partitioned) {
            message = "Cut face intervals could not be partitioned consistently.";
            return false;
        }
    }
    if (chord_count == 0U) {
        message = "Cut could not create a valid face intersection.";
        return false;
    }
    // Keep the first surviving authored corner used by the previous clipper.
    for (auto& loop : loops) {
        const auto first = std::ranges::min_element(loop, [&](ElementId a, ElementId b) {
            return boundary_order.at(a) < boundary_order.at(b);
        });
        std::rotate(loop.begin(), first, loop.end());
    }
    std::ranges::sort(loops, [&](const auto& first, const auto& second) {
        return boundary_order.at(first.front()) < boundary_order.at(second.front());
    });
    for (const auto& loop : loops) {
        PlaneFaceLoop output;
        int side = 0;
        for (const auto id : loop) {
            const auto& point = points.at(id);
            const int vertex_side = plane_side(signed_distance_to_cut_plane(work.plane, point.position));
            if (side != 0 && vertex_side != 0 && side != vertex_side) {
                message = "Cut face remains on both sides of the plane.";
                return false;
            }
            if (vertex_side != 0) {
                side = vertex_side;
            }
            output.push_back(point);
        }
        if (side == 0) {
            message = "Cut would create a collapsed face.";
            return false;
        }
        (side > 0 ? parts.front : parts.back).push_back(std::move(output));
    }
    return !parts.front.empty() && !parts.back.empty();
}

bool append_face_parts(PlaneCutBuilder& builder, const Face& face,
                       std::vector<PlaneFaceLoop>& parts, bool& emitted_source,
                       std::vector<ElementId>* created_faces, std::string& message)
{
    for (auto& points : parts) {
        const ElementId id = emitted_source ? builder.document.next_face_id++ : face.id;
        if (!append_face_from_points(builder, face, std::move(points), message, id)) {
            return false;
        }
        if (emitted_source && created_faces != nullptr) {
            add_unique_id(*created_faces, id);
        }
        emitted_source = true;
    }
    return true;
}

bool split_face_by_plane(PlaneCutWork& work, PlaneCutBuilder& front,
                         PlaneCutBuilder& back, const Face& face,
                         std::string& message)
{
    PlaneFaceParts parts;
    if (!face_parts_by_plane(work, face, parts, message)) {
        return false;
    }
    bool emitted_front = false;
    bool emitted_back = false;
    return append_face_parts(front, face, parts.front, emitted_front, nullptr, message) &&
           append_face_parts(back, face, parts.back, emitted_back, nullptr, message);
}

bool append_surface_face_by_plane(
    PlaneCutWork& work, PlaneCutBuilder& builder, const Face& face,
    std::vector<ElementId>& cut_vertex_ids,
    std::vector<ElementId>& created_face_ids,
    std::vector<ElementId>& affected_face_ids, std::string& message,
    PlaneCutMode mode = PlaneCutMode::KeepBoth)
{
    PlaneFaceParts parts;
    const auto segment_start = work.cut_segments.size();
    if (!face_parts_by_plane(work, face, parts, message)) {
        return false;
    }
    const bool split = !parts.front.empty() && !parts.back.empty();
    bool emitted_source = false;
    if (mode != PlaneCutMode::KeepBack &&
        !append_face_parts(builder, face, parts.front, emitted_source, &created_face_ids, message)) {
        return false;
    }
    if (mode != PlaneCutMode::KeepFront &&
        !append_face_parts(builder, face, parts.back, emitted_source, &created_face_ids, message)) {
        return false;
    }
    if (split) {
        add_unique_id(affected_face_ids, face.id);
    }
    for (std::size_t index = segment_start; index < work.cut_segments.size(); ++index) {
        add_unique_id(cut_vertex_ids, work.cut_segments[index].a);
        add_unique_id(cut_vertex_ids, work.cut_segments[index].b);
    }
    return true;
}

std::map<std::pair<ElementId, ElementId>, ElementId>
plane_cut_split_vertices(
    const std::map<std::pair<ElementId, ElementId>, PlaneCutIntersection>&
        intersections)
{
    std::map<std::pair<ElementId, ElementId>, ElementId> split_vertices;
    for (const auto& [edge, intersection] : intersections) {
        if (intersection.id != kInvalidElementId) {
            split_vertices[edge] = intersection.id;
        }
    }
    return split_vertices;
}

void preserve_clipped_plane_cut_edge_normal_modes(
    const Document& source,
    Document& candidate,
    const std::map<std::pair<ElementId, ElementId>, PlaneCutIntersection>&
        intersections)
{
    candidate.hard_normal_edges.clear();
    candidate.soft_normal_edges.clear();
    std::vector<EdgeNormalModeCopy> normal_mode_copies;
    normal_mode_copies.reserve(
        (source.hard_normal_edges.size() + source.soft_normal_edges.size()) *
        3U);
    const auto preserve_mode = [&](Edge source_edge) {
        source_edge = make_edge(source_edge.a, source_edge.b);
        if (edge_exists(candidate, source_edge)) {
            normal_mode_copies.push_back({source_edge, source_edge});
        }
        const auto intersection =
            intersections.find({source_edge.a, source_edge.b});
        if (intersection == intersections.end() ||
            intersection->second.id == kInvalidElementId) {
            return;
        }
        for (const Edge descendant : {
                 make_edge(source_edge.a, intersection->second.id),
                 make_edge(intersection->second.id, source_edge.b),
            }) {
            if (edge_exists(candidate, descendant)) {
                normal_mode_copies.push_back({source_edge, descendant});
            }
        }
    };
    for (const Edge edge : source.hard_normal_edges) {
        preserve_mode(edge);
    }
    for (const Edge edge : source.soft_normal_edges) {
        preserve_mode(edge);
    }
    (void)copy_edge_normal_modes(source, candidate, normal_mode_copies);
    prune_normal_edge_states(candidate);
}

std::vector<Edge> exposed_plane_boundary(const Document& source,
                                         const Document& candidate,
                                         const PlaneCutPlane& plane)
{
    const auto source_counts = edge_incidence_counts(source);
    const auto candidate_counts = edge_incidence_counts(candidate);
    std::vector<Edge> boundary;
    for (const auto& [edge, count] : candidate_counts) {
        if (count != 1) {
            continue;
        }
        const auto old = source_counts.find(edge);
        if (old != source_counts.end() && old->second == 1) {
            continue;
        }
        const auto* first = find_vertex(candidate, edge.first);
        const auto* second = find_vertex(candidate, edge.second);
        if (first != nullptr && second != nullptr &&
            plane_side(signed_distance_to_cut_plane(plane, first->position)) == 0 &&
            plane_side(signed_distance_to_cut_plane(plane, second->position)) == 0) {
            boundary.push_back(make_edge(edge.first, edge.second));
        }
    }
    return boundary;
}

std::vector<Edge> selected_existing_plane_sections(
    const Document& document, const std::set<ElementId>& selected_faces,
    const PlaneCutPlane& plane)
{
    std::map<std::pair<ElementId, ElementId>, unsigned> adjacent_sides;
    for (const auto& face : document.faces) {
        if (!selected_faces.contains(face.id)) {
            continue;
        }
        unsigned sides = 0U;
        for (const auto id : face.vertices) {
            const auto* vertex = find_vertex(document, id);
            if (vertex == nullptr) {
                continue;
            }
            const int side = plane_side(signed_distance_to_cut_plane(plane, vertex->position));
            sides |= side > 0 ? 1U : side < 0 ? 2U : 0U;
        }
        for (const auto edge : face_edges(face)) {
            const auto* first = find_vertex(document, edge.a);
            const auto* second = find_vertex(document, edge.b);
            if (first != nullptr && second != nullptr &&
                plane_side(signed_distance_to_cut_plane(plane, first->position)) == 0 &&
                plane_side(signed_distance_to_cut_plane(plane, second->position)) == 0) {
                adjacent_sides[{edge.a, edge.b}] |= sides;
            }
        }
    }
    std::vector<Edge> result;
    for (const auto& [edge, sides] : adjacent_sides) {
        if (sides == 3U) {
            result.push_back(make_edge(edge.first, edge.second));
        }
    }
    return result;
}

std::optional<std::vector<std::vector<ElementId>>> cap_loops_from_segments(
    std::span<const Edge> segments, std::string& message)
{
    std::map<ElementId, std::vector<ElementId>> adjacency;
    std::set<std::pair<ElementId, ElementId>> remaining;
    for (const Edge edge : segments) {
        if (edge.a == kInvalidElementId || edge.b == kInvalidElementId || edge.a == edge.b) {
            message = "Cut would create an invalid cap edge.";
            return std::nullopt;
        }
        if (remaining.emplace(edge.a, edge.b).second) {
            adjacency[edge.a].push_back(edge.b);
            adjacency[edge.b].push_back(edge.a);
        }
    }
    for (const auto& [vertex, neighbors] : adjacency) {
        (void)vertex;
        if (neighbors.size() > 2U) {
            message = "Cut cap boundary has a branch.";
            return std::nullopt;
        }
    }

    std::vector<std::vector<ElementId>> loops;
    while (!remaining.empty()) {
        const auto [start, second] = *remaining.begin();
        std::vector<ElementId> component{start};
        std::set<ElementId> component_vertices{start};
        bool open = false;
        for (std::size_t index = 0; index < component.size(); ++index) {
            const auto& neighbors = adjacency.at(component[index]);
            open = open || neighbors.size() != 2U;
            for (const auto neighbor : neighbors) {
                if (component_vertices.insert(neighbor).second) {
                    component.push_back(neighbor);
                }
            }
        }
        if (open) {
            std::erase_if(remaining, [&](const auto& edge) {
                return component_vertices.contains(edge.first);
            });
            continue;
        }

        std::vector<ElementId> loop{start};
        ElementId previous = start;
        ElementId current = second;
        remaining.erase({start, second});
        while (current != start) {
            loop.push_back(current);
            const auto& neighbors = adjacency.at(current);
            const auto next = neighbors[0] == previous ? neighbors[1] : neighbors[0];
            const auto edge = make_edge(current, next);
            if (remaining.erase({edge.a, edge.b}) != 1U ||
                loop.size() > component.size()) {
                message = "Cut cap boundary traversal is inconsistent.";
                return std::nullopt;
            }
            previous = current;
            current = next;
        }
        if (loop.size() < 3U) {
            message = "Cut cap boundary is degenerate.";
            return std::nullopt;
        }
        loops.push_back(std::move(loop));
    }
    return loops;
}

bool append_generated_caps(PlaneCutBuilder& builder, const Document& source,
                           const PlaneCutPlane& plane, std::span<const Edge> boundary,
                           std::string& message)
{
    auto loops = cap_loops_from_segments(boundary, message);
    if (!loops) {
        return false;
    }
    const int dropped_axis = dropped_axis_for_normal(poly_vec3(plane.normal));
    std::vector<Face> attributes_by_loop;
    std::vector<std::vector<quader_geometry::QVec2d>> projected;
    for (auto& loop : *loops) {
        const auto first_edge = make_edge(loop[0], loop[1]);
        const auto incident = std::ranges::find_if(builder.document.faces, [&](const Face& face) {
            return face_uses_edge(face, first_edge);
        });
        if (incident == builder.document.faces.end()) {
            message = "Cut cap has no retained incident face.";
            return false;
        }
        const Face attributes = *incident;
        const auto edge_index = face_edge_index(attributes, first_edge);
        const Edge direction = directed_face_edge(attributes, *edge_index);
        if (direction.a == loop[0]) {
            Face winding_face;
            winding_face.vertices = std::move(loop);
            reverse_face_winding(winding_face);
            loop = std::move(winding_face.vertices);
        }
        projected.emplace_back();
        for (std::size_t index = 0; index < loop.size(); ++index) {
            const ElementId first = loop[index];
            const ElementId second = loop[(index + 1U) % loop.size()];
            const auto adjacent = std::ranges::find_if(builder.document.faces, [&](const Face& face) {
                return face_uses_edge(face, make_edge(first, second));
            });
            if (adjacent == builder.document.faces.end()) {
                message = "Cut cap references a missing boundary edge.";
                return false;
            }
            const auto adjacent_index = face_edge_index(*adjacent, make_edge(first, second));
            if (directed_face_edge(*adjacent, *adjacent_index).a == first) {
                message = "Cut cap boundary has inconsistent face winding.";
                return false;
            }
            const auto point = knife_project_vertex(builder.document, first, dropped_axis);
            projected.back().push_back({point[0], point[1]});
        }
        attributes_by_loop.push_back(attributes);
    }

    std::vector<std::size_t> parents(loops->size(), loops->size());
    std::vector<double> signed_areas;
    for (const auto& polygon : projected) {
        signed_areas.push_back(quader_geometry::polygon_signed_area<double>(polygon));
    }
    for (std::size_t inner = 0; inner < loops->size(); ++inner) {
        for (std::size_t outer = 0; outer < loops->size(); ++outer) {
            if (outer == inner) {
                continue;
            }
            for (std::size_t first = 0; first < projected[inner].size(); ++first) {
                for (std::size_t second = 0; second < projected[outer].size(); ++second) {
                    if (quader_geometry::segments_intersect_2d<double>(
                            projected[inner][first], projected[inner][(first + 1U) % projected[inner].size()],
                            projected[outer][second], projected[outer][(second + 1U) % projected[outer].size()])) {
                        message = "Cut cap rings intersect or touch.";
                        return false;
                    }
                }
            }
            if (std::abs(signed_areas[outer]) > std::abs(signed_areas[inner]) &&
                quader_geometry::point_in_or_on_polygon_2d<double>(projected[inner].front(), projected[outer]) &&
                (parents[inner] == loops->size() ||
                 std::abs(signed_areas[outer]) < std::abs(signed_areas[parents[inner]]))) {
                parents[inner] = outer;
            }
        }
    }
    // Opposite boundary orientations describe a hollow section; equal orientations
    // belong to independent nested shells and need independent caps.
    std::vector<std::size_t> ring_order;
    for (std::size_t index = 0; index < loops->size(); ++index) {
        ring_order.push_back(index);
    }
    std::ranges::sort(ring_order, [&](std::size_t first, std::size_t second) {
        return std::abs(signed_areas[first]) > std::abs(signed_areas[second]);
    });
    std::vector<std::size_t> hole_owners(loops->size(), loops->size());
    std::vector<std::vector<std::size_t>> unmatched_ancestors(loops->size());
    for (const auto index : ring_order) {
        const auto parent = parents[index];
        auto ancestors = parent == loops->size() ? std::vector<std::size_t>{} : unmatched_ancestors[parent];
        if (!ancestors.empty() && signed_areas[index] * signed_areas[ancestors.back()] < 0.0) {
            hole_owners[index] = ancestors.back();
            ancestors.pop_back();
        } else {
            ancestors.push_back(index);
        }
        unmatched_ancestors[index] = std::move(ancestors);
    }
    const auto append_cap = [&](const Face& attributes, std::span<const ElementId> vertices) {
        PlaneFaceLoop points;
        for (const auto id : vertices) {
            const auto* vertex = find_vertex(builder.document, id);
            if (vertex == nullptr) {
                message = "Cut cap references a missing vertex.";
                return false;
            }
            points.push_back({.id = id, .position = vertex->position, .on_plane = true});
        }
        const ElementId cap_id = builder.document.next_face_id++;
        if (!append_face_from_points(builder, attributes, std::move(points), message, cap_id)) {
            return false;
        }
        builder.cap_face_ids.push_back(cap_id);
        return true;
    };
    for (std::size_t outer = 0; outer < loops->size(); ++outer) {
        if (hole_owners[outer] != loops->size()) {
            continue;
        }
        std::vector<std::vector<quader_geometry::QVec2d>> rings{projected[outer]};
        std::vector<ElementId> vertices = (*loops)[outer];
        for (std::size_t hole = 0; hole < loops->size(); ++hole) {
            if (hole_owners[hole] == outer) {
                rings.push_back(projected[hole]);
                vertices.insert(vertices.end(), (*loops)[hole].begin(), (*loops)[hole].end());
            }
        }
        if (rings.size() == 1U) {
            if (!append_cap(attributes_by_loop[outer], vertices)) {
                return false;
            }
            continue;
        }
        const auto triangles = quader_geometry::triangulate_projected_polygon_rings(rings);
        if (triangles.empty()) {
            message = "Cut could not triangulate the cap boundary and its holes.";
            return false;
        }
        std::vector<quader_geometry::QVec2d> points;
        for (const auto& ring : rings) {
            points.insert(points.end(), ring.begin(), ring.end());
        }
        for (const auto triangle : triangles) {
            std::array<ElementId, 3> triangle_vertices{
                vertices[triangle[0]], vertices[triangle[1]], vertices[triangle[2]]};
            const double area = quader_geometry::triangle_signed_area(
                points[triangle[0]], points[triangle[1]], points[triangle[2]]);
            if (area * signed_areas[outer] < 0.0) {
                std::swap(triangle_vertices[1], triangle_vertices[2]);
            }
            if (!append_cap(attributes_by_loop[outer], triangle_vertices)) {
                return false;
            }
        }
    }
    const auto incidence = edge_incidence_counts(builder.document);
    for (const auto& loop : *loops) {
        for (std::size_t index = 0; index < loop.size(); ++index) {
            const auto edge = make_edge(loop[index], loop[(index + 1U) % loop.size()]);
            const auto found = incidence.find({edge.a, edge.b});
            if (found == incidence.end() || found->second != 2) {
                message = "Cut cap did not preserve every authored boundary edge.";
                return false;
            }
        }
    }
    const auto validation = validate_authored_operation_candidate(source, builder.document);
    if (!validation.success) {
        message = validation.message;
        return false;
    }
    return true;
}

void publish_cap_selection(PlaneCutBuilder& builder)
{
    builder.selection.clear();
    builder.selection.mode = SelectionMode::Face;
    builder.selection.faces = builder.cap_face_ids;
    if (!builder.selection.faces.empty()) {
        builder.selection.has_active = true;
        builder.selection.active_kind = ElementKind::Face;
        builder.selection.active_face = builder.selection.faces.front();
    }
}

bool finalize_cut_document(PlaneCutBuilder& builder, const Document& source,
                           std::string& message)
{
    prune_unused_vertices(builder.document);
    const auto validation = validate_authored_operation_candidate(source, builder.document);
    if (!validation.success) {
        message = validation.message;
        return false;
    }
    publish_cap_selection(builder);
    return true;
}

std::optional<PlaneCutPlane> scoped_cut_plane(const PlaneCutRequest& request)
{
    for (const quader::QVec3 point : {
             request.first_point, request.second_point, request.third_point}) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
            !std::isfinite(point.z)) {
            return std::nullopt;
        }
    }
    const auto plane = plane_from_request(request);
    if (!plane || !std::isfinite(plane->normal.x) ||
        !std::isfinite(plane->normal.y) || !std::isfinite(plane->normal.z)) {
        return std::nullopt;
    }
    return plane;
}

PlaneCutResult plane_cut_impl(const Document& document, const PlaneCutRequest& request)
{
    QDR_PROFILE_SCOPE("qdr_document.plane_cut_impl");
    PlaneCutResult result;
    if (document.faces.empty()) {
        result.message = "Cut needs a mesh.";
        return result;
    }
    const auto source_validation = validate_authored_operation_candidate(document, document);
    if (!source_validation.success) {
        result.message = source_validation.message;
        return result;
    }

    const std::optional<PlaneCutPlane> plane = scoped_cut_plane(request);
    if (!plane.has_value()) {
        result.message = "Cut plane needs three non-collinear points.";
        return result;
    }

    PlaneCutWork work;
    work.plane = *plane;
    work.next_vertex_id = document.next_vertex_id;
    bool has_front = false;
    bool has_back = false;
    for (const Vertex& vertex : document.vertices) {
        work.source_vertices[vertex.id] = { vertex.id, vertex.position };
        const auto side = plane_side(signed_distance_to_cut_plane(*plane, vertex.position));
        has_front = has_front || side > 0;
        has_back = has_back || side < 0;
    }
    if (!has_front || !has_back) {
        result.message = "Cut plane must pass through the mesh interior.";
        return result;
    }

    PlaneCutBuilder front;
    PlaneCutBuilder back;
    front.document.next_vertex_id = document.next_vertex_id;
    front.document.next_face_id = document.next_face_id;
    back.document.next_vertex_id = document.next_vertex_id;
    back.document.next_face_id = document.next_face_id;

    std::string message;
    for (const Face& face : document.faces) {
        if (!split_face_by_plane(work, front, back, face, message)) {
            result.message = message.empty() ? "Cut could not split the mesh." : std::move(message);
            return result;
        }
    }

    const auto front_boundary = exposed_plane_boundary(document, front.document, *plane);
    const auto back_boundary = exposed_plane_boundary(document, back.document, *plane);
    if ((front_boundary.empty() && back_boundary.empty()) ||
        front.document.faces.empty() || back.document.faces.empty()) {
        result.message = "Cut plane must pass through the mesh interior.";
        return result;
    }

    if (request.cap) {
        if (!append_generated_caps(front, document, *plane, front_boundary, message) ||
            !append_generated_caps(back, document, *plane, back_boundary, message)) {
            result.message = std::move(message);
            return result;
        }
    }

    preserve_clipped_plane_cut_edge_normal_modes(
        document, front.document, work.intersections);
    preserve_clipped_plane_cut_edge_normal_modes(
        document, back.document, work.intersections);

    if (!finalize_cut_document(front, document, message) ||
        !finalize_cut_document(back, document, message)) {
        result.message = message.empty() ? "Cut would create invalid mesh geometry." : std::move(message);
        return result;
    }

    result.changed = true;
    result.front_document = std::move(front.document);
    result.front_selection = std::move(front.selection);
    result.back_document = std::move(back.document);
    result.back_selection = std::move(back.selection);
    result.split_count = 1;
    result.removed_count = request.mode == PlaneCutMode::KeepBoth ? 0 : 1;
    result.affected.edges = front_boundary;
    for (const auto edge : back_boundary) {
        add_unique_edge(result.affected.edges, edge);
    }
    return result;
}

void collect_scoped_cut_deltas(const Document& source, const Document& candidate,
                              OperationResult& result)
{
    std::set<std::pair<ElementId, ElementId>> source_edges;
    std::set<std::pair<ElementId, ElementId>> candidate_edges;
    for (const Edge edge : document_edges(source)) {
        source_edges.emplace(edge.a, edge.b);
    }
    for (const Edge edge : document_edges(candidate)) {
        candidate_edges.emplace(edge.a, edge.b);
        if (!source_edges.contains({edge.a, edge.b})) {
            result.created.edges.push_back(edge);
        }
    }
    for (const auto& [first, second] : source_edges) {
        if (!candidate_edges.contains({first, second})) {
            result.deleted.edges.push_back(make_edge(first, second));
        }
    }
    std::set<ElementId> source_vertices;
    std::set<ElementId> candidate_vertices;
    for (const Vertex& vertex : source.vertices) {
        source_vertices.insert(vertex.id);
    }
    for (const Vertex& vertex : candidate.vertices) {
        candidate_vertices.insert(vertex.id);
        if (!source_vertices.contains(vertex.id)) {
            result.created.vertices.push_back(vertex.id);
        }
    }
    for (const Vertex& vertex : source.vertices) {
        if (!candidate_vertices.contains(vertex.id)) {
            result.deleted.vertices.push_back(vertex.id);
        }
    }
    result.affected.edges = result.created.edges;
    result.affected.vertices = result.created.vertices;
}

bool expand_scoped_cut_boundary(
    Document& candidate,
    const std::map<std::pair<ElementId, ElementId>, ElementId>& split_vertices,
    OperationResult& result)
{
    bool needs_expansion = false;
    for (const Face& face : candidate.faces) {
        for (std::size_t index = 0; index < face.vertices.size(); ++index) {
            const Edge edge = make_edge(
                face.vertices[index],
                face.vertices[(index + 1U) % face.vertices.size()]);
            if (split_vertices.contains({edge.a, edge.b})) {
                add_unique_id(result.affected.faces, face.id);
                needs_expansion = true;
                break;
            }
        }
    }
    return !needs_expansion ||
           apply_knife_edge_splits_to_faces(candidate, split_vertices);
}


} // namespace

PlaneCutOperation::PlaneCutOperation(PlaneCutRequest request) : request_(request) {}

OperationResult PlaneCutOperation::apply(Document& document, Selection& selection) const
{
    QDR_PROFILE_SCOPE("qdr_document.PlaneCutOperation.apply");
    PlaneCutResult result = plane_cut_impl(document, request_);
    if (!result.changed) {
        return OperationResult::failure(
            result.message.empty()
                ? std::string("Plane Cut could not split the document.")
                : std::move(result.message));
    }

    switch (request_.mode) {
    case PlaneCutMode::KeepFront:
      document = std::move(result.front_document);
      selection = std::move(result.front_selection);
      return {true, {}};
    case PlaneCutMode::KeepBack:
      document = std::move(result.back_document);
      selection = std::move(result.back_selection);
      return {true, {}};
    case PlaneCutMode::KeepBoth:
      return OperationResult::failure(
          "Plane Cut keep_both produces two documents; use plane_cut() or "
          "choose keep_front/keep_back for single-document operations.");
    }

    return OperationResult::failure("Plane Cut received an unsupported mode.");
}

PlaneCutResult plane_cut(const Document& document, const PlaneCutRequest& request)
{
    QDR_PROFILE_SCOPE("qdr_document.plane_cut");
    return plane_cut_impl(document, request);
}

OperationResult plane_surface_cut(Document &document, Selection &selection,
                                  const PlaneCutRequest &request) {
  QDR_PROFILE_SCOPE("qdr_document.plane_surface_cut");
  if (document.faces.empty()) {
    return OperationResult::failure("Cut needs a mesh.");
  }

  const std::optional<PlaneCutPlane> plane = plane_from_request(request);
  if (!plane.has_value()) {
    return OperationResult::failure("Cut plane needs three non-collinear points.");
  }

  PlaneCutWork work;
  work.plane = *plane;
  work.next_vertex_id = document.next_vertex_id;
  for (const Vertex& vertex : document.vertices) {
    work.source_vertices[vertex.id] = {vertex.id, vertex.position};
  }

  PlaneCutBuilder builder;
  builder.document.next_vertex_id = document.next_vertex_id;
  builder.document.next_face_id = document.next_face_id;

  OperationResult result;
  std::vector<ElementId> cut_vertex_ids;
  std::string message;
  for (const Face& face : document.faces) {
    if (!append_surface_face_by_plane(
            work,
            builder,
            face,
            cut_vertex_ids,
            result.created.faces,
            result.affected.faces,
            message)) {
      return OperationResult::failure(
          message.empty() ? std::string("Cut could not split the mesh faces.")
                          : std::move(message));
    }
  }

  if (work.cut_segments.empty()) {
    return {false, "Cut plane did not create any face cuts."};
  }

  remap_normal_edge_states_after_edge_splits(
      document,
      builder.document,
      plane_cut_split_vertices(work.intersections));
  prune_unused_vertices(builder.document);
  restore_source_face_orientation(document, builder.document);
  if (!every_face_triangulates(builder.document)) {
    return OperationResult::failure(
        "Cut would create a face that cannot be triangulated.");
  }

  for (const auto& [edge, intersection] : work.intersections) {
    (void)edge;
    add_unique_id(result.created.vertices, intersection.id);
  }
  result.affected.edges = work.cut_segments;
  result.changed = true;

  document = std::move(builder.document);
  selection.clear();
  selection.mode = SelectionMode::Vertex;
  selection.vertices = std::move(cut_vertex_ids);
  activate_last_selection(selection);
  return result;
}

OperationResult plane_cut_selected_faces(Document& document,
                                         Selection& selection,
                                         const PlaneCutRequest& request)
{
    const auto plane = scoped_cut_plane(request);
    if (!plane) {
        return OperationResult::failure(
            "Cut plane needs three finite non-collinear points.");
    }
    if (request.mode != PlaneCutMode::KeepBoth &&
        request.mode != PlaneCutMode::KeepFront &&
        request.mode != PlaneCutMode::KeepBack) {
        return OperationResult::failure("Cut received an unsupported keep mode.");
    }
    const std::set<ElementId> selected_faces(selection.faces.begin(),
                                              selection.faces.end());
    for (const ElementId face_id : selected_faces) {
        if (find_face(document, face_id) == nullptr) {
            return OperationResult::failure("Cut selected face is unavailable.");
        }
    }
    if (selected_faces.empty()) {
        return {false, "Cut needs selected faces."};
    }

    PlaneCutWork work;
    work.plane = *plane;
    work.next_vertex_id = document.next_vertex_id;
    PlaneCutBuilder builder;
    builder.document = document;
    builder.document.faces.clear();
    for (const Vertex& vertex : document.vertices) {
        work.source_vertices[vertex.id] = {vertex.id, vertex.position};
        builder.emitted_vertices.insert(vertex.id);
    }

    OperationResult result;
    std::vector<ElementId> cut_vertices;
    std::set<ElementId> selected_source_vertices;
    std::string message;
    for (const Face& face : document.faces) {
        if (!selected_faces.contains(face.id)) {
            builder.document.faces.push_back(face);
            continue;
        }
        bool has_front = false;
        bool has_back = false;
        for (const ElementId vertex_id : face.vertices) {
            selected_source_vertices.insert(vertex_id);
            const auto vertex = work.source_vertices.find(vertex_id);
            if (vertex == work.source_vertices.end()) {
                return OperationResult::failure("Cut face vertex is unavailable.");
            }
            const float distance = signed_distance_to_cut_plane(
                *plane, vertex->second.position);
            if (!std::isfinite(distance)) {
                return OperationResult::failure("Cut face positions must be finite.");
            }
            has_front = has_front || plane_side(distance) > 0;
            has_back = has_back || plane_side(distance) < 0;
        }
        if ((request.mode == PlaneCutMode::KeepFront && has_back && !has_front) ||
            (request.mode == PlaneCutMode::KeepBack && has_front && !has_back)) {
            result.deleted.faces.push_back(face.id);
            continue;
        }
        if (!has_front || !has_back) {
            builder.document.faces.push_back(face);
            continue;
        }
        if (!append_surface_face_by_plane(
                work, builder, face, cut_vertices, result.created.faces,
                result.affected.faces, message, request.mode)) {
            return OperationResult::failure(std::move(message));
        }
    }
    if (result.affected.faces.empty() && result.deleted.faces.empty()) {
        result.message = "Cut plane did not change selected faces.";
        result.affected.edges = selected_existing_plane_sections(document, selected_faces, *plane);
        return result;
    }

    const auto split_vertices = plane_cut_split_vertices(work.intersections);
    if (!expand_scoped_cut_boundary(builder.document, split_vertices, result)) {
        return OperationResult::failure("Cut could not expand incident face loops.");
    }
    std::set<ElementId> retained_vertices;
    for (const Face& face : builder.document.faces) {
        retained_vertices.insert(face.vertices.begin(), face.vertices.end());
    }
    std::erase_if(builder.document.vertices, [&](const Vertex& vertex) {
        return selected_source_vertices.contains(vertex.id) &&
               !retained_vertices.contains(vertex.id);
    });
    remap_normal_edge_states_after_edge_splits(
        document, builder.document, split_vertices);
    prune_normal_edge_states(builder.document);
    const auto cut_boundary = exposed_plane_boundary(document, builder.document, *plane);
    if (request.cap && request.mode != PlaneCutMode::KeepBoth) {
        if (!append_generated_caps(builder, document, *plane, cut_boundary, message)) {
            return OperationResult::failure(std::move(message));
        }
        for (const auto face_id : builder.cap_face_ids) {
            add_unique_id(result.created.faces, face_id);
        }
    }
    const auto validation = validate_authored_operation_candidate(document, builder.document);
    if (!validation.success) {
        return validation;
    }

    Selection output;
    output.mode = SelectionMode::Face;
    for (const ElementId face_id : selection.faces) {
        if (find_face(builder.document, face_id) != nullptr) {
            add_unique_id(output.faces, face_id);
        }
    }
    for (const ElementId face_id : result.created.faces) {
        add_unique_id(output.faces, face_id);
    }
    activate_last_selection(output);
    if (selection.has_active &&
        contains_id(output.faces, selection.active_face)) {
        output.active_face = selection.active_face;
    }
    collect_scoped_cut_deltas(document, builder.document, result);
    result.affected.edges = work.cut_segments;
    for (const auto edge : selected_existing_plane_sections(document, selected_faces, *plane)) {
        add_unique_edge(result.affected.edges, edge);
    }
    for (const auto edge : cut_boundary) {
        add_unique_edge(result.affected.edges, edge);
    }
    result.changed = true;
    document = std::move(builder.document);
    selection = std::move(output);
    return result;
}

OperationResult plane_cut_selected_edges(Document& document,
                                         Selection& selection,
                                         const PlaneCutRequest& request)
{
    const auto plane = scoped_cut_plane(request);
    if (!plane) {
        return OperationResult::failure(
            "Cut plane needs three finite non-collinear points.");
    }
    std::set<std::pair<ElementId, ElementId>> available_edges;
    for (const Edge edge : document_edges(document)) {
        available_edges.emplace(edge.a, edge.b);
    }
    for (const Edge edge : selection.edges) {
        const Edge canonical = make_edge(edge.a, edge.b);
        if (!available_edges.contains({canonical.a, canonical.b})) {
            return OperationResult::failure("Cut selected edge is unavailable.");
        }
    }
    Document candidate = document;
    std::map<std::pair<ElementId, ElementId>, ElementId> split_vertices;
    for (const Edge edge : selection.edges) {
        const Vertex* first = find_vertex(document, edge.a);
        const Vertex* second = find_vertex(document, edge.b);
        if (first == nullptr || second == nullptr) {
            return OperationResult::failure("Cut edge vertex is unavailable.");
        }
        const float first_distance = signed_distance_to_cut_plane(*plane, first->position);
        const float second_distance = signed_distance_to_cut_plane(*plane, second->position);
        if (!std::isfinite(first_distance) || !std::isfinite(second_distance)) {
            return OperationResult::failure("Cut edge positions must be finite.");
        }
        if (plane_side(first_distance) * plane_side(second_distance) != -1) {
            continue;
        }
        const float factor = cut_edge_factor(
            *plane, first->position, second->position, first_distance, second_distance);
        if (split_vertex_for_edge(candidate, split_vertices, edge, factor) ==
            kInvalidElementId) {
            return OperationResult::failure("Cut could not insert an edge vertex.");
        }
    }
    if (split_vertices.empty()) {
        return {false, "Cut plane did not intersect selected edge interiors."};
    }
    OperationResult result;
    if (!expand_scoped_cut_boundary(candidate, split_vertices, result)) {
        return OperationResult::failure("Cut could not expand incident face loops.");
    }
    remap_normal_edge_states_after_edge_splits(document, candidate, split_vertices);
    const auto validation = validate_authored_operation_candidate(document, candidate);
    if (!validation.success) {
        return validation;
    }
    Selection output;
    output.mode = SelectionMode::Edge;
    for (const Edge edge : selection.edges) {
        const Edge canonical = make_edge(edge.a, edge.b);
        const auto split = split_vertices.find({canonical.a, canonical.b});
        if (split == split_vertices.end()) {
            add_unique_edge(output.edges, canonical);
        } else {
            add_unique_edge(output.edges, make_edge(canonical.a, split->second));
            add_unique_edge(output.edges, make_edge(split->second, canonical.b));
        }
    }
    activate_last_selection(output);
    if (selection.has_active) {
        const Edge active = make_edge(selection.active_edge.a, selection.active_edge.b);
        const auto split = split_vertices.find({active.a, active.b});
        const Edge remapped = split == split_vertices.end()
                                  ? active : make_edge(active.a, split->second);
        if (contains_edge(output.edges, remapped)) {
            output.active_edge = remapped;
        }
    }
    collect_scoped_cut_deltas(document, candidate, result);
    result.changed = true;
    document = std::move(candidate);
    selection = std::move(output);
    return result;
}

} // namespace quader_poly

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

#include <algorithm>
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
    std::map<ElementId, quader::QVec3> plane_vertex_positions;
    std::vector<Edge> cut_segments;
    ElementId next_vertex_id = 1;
    std::uint32_t cap_material_slot = 0;
    FaceTextureTransform cap_texture_transform{};
    bool has_cap_material_slot = false;
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
    work.plane_vertex_positions[intersection.id] = intersection.position;
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

std::vector<ElementId> plane_vertices_for_split_loop(
    const PlaneCutPlane& plane,
    const std::map<ElementId, quader::QVec3>& positions,
    std::span<const PlaneCutPoint> points)
{
    std::vector<ElementId> ids;
    for (const PlaneCutPoint& point : points) {
      const bool on_plane =
          point.on_plane ||
          std::abs(signed_distance_to_cut_plane(plane, point.position)) <=
              kPlaneCutEpsilon ||
          (positions.contains(point.id) &&
           std::abs(signed_distance_to_cut_plane(
               plane, positions.at(point.id))) <= kPlaneCutEpsilon);
      if (on_plane && !contains_id(ids, point.id)) {
        ids.push_back(point.id);
      }
    }
    return ids;
}

bool append_cut_segment_from_split_face(
    PlaneCutWork& work,
    std::span<const PlaneCutPoint> front_points,
    std::span<const PlaneCutPoint> back_points,
    std::string& message)
{
    std::vector<ElementId> plane_ids = plane_vertices_for_split_loop(work.plane, work.plane_vertex_positions, front_points);
    for (const ElementId id : plane_vertices_for_split_loop(work.plane, work.plane_vertex_positions, back_points)) {
        add_unique_id(plane_ids, id);
    }
    if (plane_ids.size() != 2U) {
        message = "Cut would create an unsupported cap topology.";
        return false;
    }
    const Edge edge = make_edge(plane_ids[0], plane_ids[1]);
    if (edge.a == edge.b || contains_edge(work.cut_segments, edge)) {
        message = "Cut would create a duplicate cap segment.";
        return false;
    }
    work.cut_segments.push_back(edge);
    return true;
}

bool split_face_by_plane(
    PlaneCutWork& work,
    PlaneCutBuilder& front,
    PlaneCutBuilder& back,
    const Face& face,
    std::string& message)
{
    if (face.vertices.size() < 3U) {
        return true;
    }

    const bool has_loop_uvs = face_has_loop_uvs(face);
    std::vector<float> distances;
    distances.reserve(face.vertices.size());
    bool has_front = false;
    bool has_back = false;
    bool has_on = false;
    for (const ElementId vertex_id : face.vertices) {
        const auto found = work.source_vertices.find(vertex_id);
        if (found == work.source_vertices.end()) {
            message = "Cut could not find a face vertex.";
            return false;
        }
        const float distance = signed_distance_to_cut_plane(work.plane, found->second.position);
        distances.push_back(distance);
        const int side = plane_side(distance);
        has_front = has_front || side > 0;
        has_back = has_back || side < 0;
        has_on = has_on || side == 0;
        if (side == 0) {
            work.plane_vertex_positions[vertex_id] = found->second.position;
        }
    }

    if (has_on && !has_front && !has_back) {
        message = "Cut plane lies on an existing face.";
        return false;
    }

    if (!has_back) {
        std::vector<PlaneCutPoint> points;
        points.reserve(face.vertices.size());
        for (std::size_t index = 0; index < face.vertices.size(); ++index) {
            points.push_back(source_point_for_face_vertex(work, face, index, has_loop_uvs));
        }
        return append_face_from_points(front, face, std::move(points), message);
    }

    if (!has_front) {
        std::vector<PlaneCutPoint> points;
        points.reserve(face.vertices.size());
        for (std::size_t index = 0; index < face.vertices.size(); ++index) {
            points.push_back(source_point_for_face_vertex(work, face, index, has_loop_uvs));
        }
        return append_face_from_points(back, face, std::move(points), message);
    }

    if (!work.has_cap_material_slot) {
        work.cap_material_slot = face.material_slot;
        work.cap_texture_transform = face.texture_transform;
        work.has_cap_material_slot = true;
    }

    std::vector<PlaneCutPoint> front_points;
    std::vector<PlaneCutPoint> back_points;
    front_points.reserve(face.vertices.size() + 2U);
    back_points.reserve(face.vertices.size() + 2U);

    for (std::size_t index = 0; index < face.vertices.size(); ++index) {
        const std::size_t next_index = (index + 1U) % face.vertices.size();
        const int side = plane_side(distances[index]);
        const int next_side = plane_side(distances[next_index]);
        const PlaneCutPoint current = source_point_for_face_vertex(work, face, index, has_loop_uvs);

        if (side >= 0) {
            front_points.push_back(current);
        }
        if (side <= 0) {
            back_points.push_back(current);
        }

        if ((side > 0 && next_side < 0) || (side < 0 && next_side > 0)) {
            PlaneCutPoint intersection = intersection_point_for_edge(
                work,
                face,
                index,
                next_index,
                distances[index],
                distances[next_index],
                has_loop_uvs);
            front_points.push_back(intersection);
            back_points.push_back(intersection);
        }
    }

    front_points = compact_output_points(std::move(front_points));
    back_points = compact_output_points(std::move(back_points));
    if (front_points.size() < 3U || back_points.size() < 3U) {
        message = "Cut would create degenerate split faces.";
        return false;
    }
    if (!append_cut_segment_from_split_face(work, front_points, back_points, message)) {
        return false;
    }
    return
        append_face_from_points(front, face, std::move(front_points), message) &&
        append_face_from_points(back, face, std::move(back_points), message);
}

std::vector<PlaneCutPoint> source_points_for_face(
    const PlaneCutWork& work,
    const Face& face,
    bool has_loop_uvs)
{
    std::vector<PlaneCutPoint> points;
    points.reserve(face.vertices.size());
    for (std::size_t index = 0; index < face.vertices.size(); ++index) {
        points.push_back(source_point_for_face_vertex(work, face, index, has_loop_uvs));
    }
    return points;
}

bool append_surface_cut_segment_from_split_face(
    PlaneCutWork& work,
    std::span<const PlaneCutPoint> front_points,
    std::span<const PlaneCutPoint> back_points,
    std::vector<ElementId>& cut_vertex_ids,
    std::string& message)
{
    std::vector<ElementId> plane_ids =
        plane_vertices_for_split_loop(work.plane, work.plane_vertex_positions, front_points);
    for (const ElementId id :
         plane_vertices_for_split_loop(work.plane, work.plane_vertex_positions, back_points)) {
        add_unique_id(plane_ids, id);
    }
    if (plane_ids.size() != 2U) {
        message = "Cut plane intersects a face in unsupported topology.";
        return false;
    }

    const Edge edge = make_edge(plane_ids[0], plane_ids[1]);
    if (edge.a == edge.b) {
        message = "Cut would create a degenerate face split.";
        return false;
    }
    add_unique_edge(work.cut_segments, edge);
    add_unique_id(cut_vertex_ids, plane_ids[0]);
    add_unique_id(cut_vertex_ids, plane_ids[1]);
    return true;
}

bool append_surface_face_by_plane(
    PlaneCutWork& work,
    PlaneCutBuilder& builder,
    const Face& face,
    std::vector<ElementId>& cut_vertex_ids,
    std::vector<ElementId>& created_face_ids,
    std::vector<ElementId>& affected_face_ids,
    std::string& message,
    PlaneCutMode mode = PlaneCutMode::KeepBoth)
{
    if (face.vertices.size() < 3U) {
        return true;
    }

    const bool has_loop_uvs = face_has_loop_uvs(face);
    std::vector<float> distances;
    distances.reserve(face.vertices.size());
    bool has_front = false;
    bool has_back = false;
    bool has_on = false;
    for (const ElementId vertex_id : face.vertices) {
        const auto found = work.source_vertices.find(vertex_id);
        if (found == work.source_vertices.end()) {
            message = "Cut could not find a face vertex.";
            return false;
        }
        const float distance =
            signed_distance_to_cut_plane(work.plane, found->second.position);
        distances.push_back(distance);
        const int side = plane_side(distance);
        has_front = has_front || side > 0;
        has_back = has_back || side < 0;
        has_on = has_on || side == 0;
        if (side == 0) {
            work.plane_vertex_positions[vertex_id] = found->second.position;
        }
    }

    if ((has_on && !has_front && !has_back) || !has_back || !has_front) {
        return append_face_from_points(
            builder,
            face,
            source_points_for_face(work, face, has_loop_uvs),
            message);
    }

    std::vector<PlaneCutPoint> front_points;
    std::vector<PlaneCutPoint> back_points;
    front_points.reserve(face.vertices.size() + 2U);
    back_points.reserve(face.vertices.size() + 2U);

    for (std::size_t index = 0; index < face.vertices.size(); ++index) {
        const std::size_t next_index = (index + 1U) % face.vertices.size();
        const int side = plane_side(distances[index]);
        const int next_side = plane_side(distances[next_index]);
        const PlaneCutPoint current =
            source_point_for_face_vertex(work, face, index, has_loop_uvs);

        if (side >= 0) {
            front_points.push_back(current);
        }
        if (side <= 0) {
            back_points.push_back(current);
        }

        if ((side > 0 && next_side < 0) || (side < 0 && next_side > 0)) {
            PlaneCutPoint intersection = intersection_point_for_edge(
                work,
                face,
                index,
                next_index,
                distances[index],
                distances[next_index],
                has_loop_uvs);
            front_points.push_back(intersection);
            back_points.push_back(intersection);
        }
    }

    front_points = compact_output_points(std::move(front_points));
    back_points = compact_output_points(std::move(back_points));
    if (front_points.size() < 3U || back_points.size() < 3U) {
        message = "Cut would create degenerate split faces.";
        return false;
    }
    if (!append_surface_cut_segment_from_split_face(
            work, front_points, back_points, cut_vertex_ids, message)) {
        return false;
    }

    if (mode == PlaneCutMode::KeepBoth) {
        const ElementId created_face_id = builder.document.next_face_id++;
        if (!append_face_from_points(builder, face, std::move(front_points), message) ||
            !append_face_from_points(
                builder, face, std::move(back_points), message, created_face_id)) {
            return false;
        }
        add_unique_id(created_face_ids, created_face_id);
    } else if (!append_face_from_points(
                   builder, face,
                   mode == PlaneCutMode::KeepFront ? std::move(front_points)
                                                  : std::move(back_points),
                   message)) {
        return false;
    }
    add_unique_id(affected_face_ids, face.id);
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

std::optional<std::vector<std::vector<ElementId>>> cap_loops_from_segments(std::span<const Edge> segments, std::string& message)
{
    std::map<ElementId, std::vector<ElementId>> adjacency;
    std::set<std::pair<ElementId, ElementId>> remaining;
    for (Edge edge : segments) {
        edge = make_edge(edge.a, edge.b);
        if (edge.a == kInvalidElementId || edge.b == kInvalidElementId ||
            edge.a == edge.b) {
          message = "Cut would create an invalid cap edge.";
          return std::nullopt;
        }
        adjacency[edge.a].push_back(edge.b);
        adjacency[edge.b].push_back(edge.a);
        remaining.emplace(edge.a, edge.b);
    }

    for (const auto& [vertex_id, neighbors] : adjacency) {
        (void)vertex_id;
        if (neighbors.size() != 2U) {
            message = "Cut cap boundary is not closed.";
            return std::nullopt;
        }
    }

    std::vector<std::vector<ElementId>> loops;
    while (!remaining.empty()) {
        const auto [start, second] = *remaining.begin();
        std::vector<ElementId> loop { start, second };
        remaining.erase(remaining.begin());

        ElementId previous = start;
        ElementId current = second;
        while (current != start) {
            const auto found = adjacency.find(current);
            if (found == adjacency.end() || found->second.size() != 2U) {
                message = "Cut cap boundary is not closed.";
                return std::nullopt;
            }

            const ElementId next = found->second[0] == previous ? found->second[1] : found->second[0];
            const Edge edge = make_edge(current, next);
            const auto remaining_edge = remaining.find({ edge.a, edge.b });
            if (remaining_edge == remaining.end() && next != start) {
                message = "Cut cap boundary has a self-intersection or branch.";
                return std::nullopt;
            }
            if (remaining_edge != remaining.end()) {
                remaining.erase(remaining_edge);
            }
            previous = current;
            current = next;
            loop.push_back(current);
            if (loop.size() > segments.size() + 1U) {
                message = "Cut cap boundary traversal did not close.";
                return std::nullopt;
            }
        }

        loop = unique_valid_face_loop(std::move(loop));
        if (loop.size() < 3U) {
            message = "Cut cap boundary is degenerate.";
            return std::nullopt;
        }
        loops.push_back(std::move(loop));
    }
    return loops;
}

bool append_cap_face(
    PlaneCutBuilder& builder,
    std::span<const ElementId> loop,
    const std::map<ElementId, quader::QVec3>& positions,
    std::uint32_t material_slot,
    const FaceTextureTransform& texture_transform,
    quader::QVec3 expected_normal,
    std::string& message)
{
    Face face;
    face.id = builder.document.next_face_id++;
    face.material_slot = material_slot;
    face.texture_transform = texture_transform;
    face.vertices.assign(loop.begin(), loop.end());
    for (const ElementId vertex_id : face.vertices) {
        const auto found = positions.find(vertex_id);
        if (found == positions.end()) {
            message = "Cut cap references a missing vertex.";
            return false;
        }
        ensure_builder_vertex(builder, vertex_id, found->second);
    }

    if (triangulate_face_local_indices(builder.document, face).empty()) {
        reverse_face_winding(face);
        if (triangulate_face_local_indices(builder.document, face).empty()) {
            message = "Cut cap could not be triangulated.";
            return false;
        }
    }
    if (dot(face_normal(builder.document, face), expected_normal) < 0.0F) {
        reverse_face_winding(face);
    }
    if (triangulate_face_local_indices(builder.document, face).empty()) {
        message = "Cut cap could not be triangulated.";
        return false;
    }

    builder.cap_face_ids.push_back(face.id);
    builder.document.faces.push_back(std::move(face));
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

bool finalize_cut_document(PlaneCutBuilder& builder, bool require_closed,
                           std::string& message)
{
    prune_invalid_faces(builder.document);
    prune_unused_vertices(builder.document);
    if (builder.document.faces.empty()) {
        message = "Cut removed all faces from one side.";
        return false;
    }
    if ((require_closed && !document_is_closed_manifold(builder.document)) ||
        (!require_closed &&
         document_has_nonmanifold_edges(builder.document))) {
        message = require_closed
                      ? "Cut would create open or non-manifold geometry."
                      : "Cut would create non-manifold open geometry.";
        return false;
    }
    if (!every_face_triangulates(builder.document)) {
        message = "Cut would create a face that cannot be triangulated.";
        return false;
    }
    publish_cap_selection(builder);
    return true;
}

PlaneCutResult plane_cut_impl(const Document& document, const PlaneCutRequest& request)
{
    QDR_PROFILE_SCOPE("qdr_document.plane_cut_impl");
    PlaneCutResult result;
    if (document.faces.empty()) {
        result.message = "Cut needs a closed mesh.";
        return result;
    }
    if (!document_is_closed_manifold(document)) {
        result.message = "Cut supports closed manifold meshes only.";
        return result;
    }

    const std::optional<PlaneCutPlane> plane = plane_from_request(request);
    if (!plane.has_value()) {
        result.message = "Cut plane needs three non-collinear points.";
        return result;
    }

    PlaneCutWork work;
    work.plane = *plane;
    work.next_vertex_id = document.next_vertex_id;
    for (const Vertex& vertex : document.vertices) {
        work.source_vertices[vertex.id] = { vertex.id, vertex.position };
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

    if (work.cut_segments.empty() || front.document.faces.empty() || back.document.faces.empty()) {
        result.message = "Cut plane must pass through the mesh interior.";
        return result;
    }

    if (request.cap) {
        std::optional<std::vector<std::vector<ElementId>>> cap_loops =
            cap_loops_from_segments(work.cut_segments, message);
        if (!cap_loops.has_value()) {
            result.message = message.empty() ? "Cut cap boundary is invalid."
                                             : std::move(message);
            return result;
        }

        for (const std::vector<ElementId>& loop : *cap_loops) {
            if (!append_cap_face(front, loop, work.plane_vertex_positions,
                                 work.cap_material_slot,
                                 work.cap_texture_transform,
                                 poly_vec3(work.plane.normal * -1.0F), message) ||
                !append_cap_face(back, loop, work.plane_vertex_positions,
                                 work.cap_material_slot,
                                 work.cap_texture_transform,
                                 poly_vec3(work.plane.normal), message)) {
                result.message = message.empty()
                                     ? "Cut could not create cap faces."
                                     : std::move(message);
                return result;
            }
        }
    }

    preserve_clipped_plane_cut_edge_normal_modes(
        document, front.document, work.intersections);
    preserve_clipped_plane_cut_edge_normal_modes(
        document, back.document, work.intersections);

    if (!finalize_cut_document(front, request.cap, message) ||
        !finalize_cut_document(back, request.cap, message)) {
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
    return result;
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
        return {false, "Cut plane did not change selected faces."};
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

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/internal/quader_poly_document_hull_plane_helpers.hpp>

#include <mesh/polygon/internal/quader_poly_document_topology_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_uv_helpers.hpp>

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
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace quader_poly::document_internal {

ElementId mapped_vertex_for_merge(ElementId vertex_id, const std::set<ElementId>& merge_vertex_ids, ElementId active_vertex_id);
std::optional<FaceUvProjectionAssignment> face_uv_projection_assignment_from_source(
        const Document& document,
        const Face& face,
        const Document& source_document,
        const std::set<ElementId>& merge_vertex_ids,
        ElementId survivor_vertex_id);
bool assign_face_uvs_from_projection_assignment(
        Document& document,
        Face& face,
        const FaceUvProjectionAssignment& assignment);
void stitch_merge_split_uv_density(Document& document, std::span<const MergeFaceUvAssignment> assignments);
bool assign_face_uvs_from_source_projection(
        Document& document,
        Face& face,
        const Document& source_document,
        const std::set<ElementId>& merge_vertex_ids,
        ElementId survivor_vertex_id);

bool points_nearly_equal(quader::QVec3 left, quader::QVec3 right,
                         float tolerance = kHullDistanceEpsilon) {
  return quader_geometry::length_squared(geometry_vec3(left) -
                                         geometry_vec3(right)) <=
         tolerance * tolerance;
}

bool append_unique_hull_plane(std::vector<QPlane3>& planes, QPlane3 plane)
{
  plane = quader_geometry::normalize_plane(plane, kEpsilon);
  if (quader_geometry::length_squared(plane.normal) <= kEpsilon * kEpsilon) {
    return false;
  }
    for (const QPlane3& existing : planes) {
      if (quader_geometry::planes_nearly_equal(existing, plane,
                                               kHullDistanceEpsilon)) {
        return false;
      }
    }
    planes.push_back(plane);
    return true;
}

std::vector<HullVertex> unique_hull_vertices_from_document(const Document& document)
{
    std::vector<HullVertex> vertices;
    vertices.reserve(document.vertices.size());
    for (const Vertex& vertex : document.vertices) {
      if (vertex.id == kInvalidElementId) {
        continue;
      }

        const auto duplicate = std::ranges::find_if(vertices, [&vertex](const HullVertex& existing) {
            return points_nearly_equal(existing.position, vertex.position);
        });
        if (duplicate == vertices.end()) {
            vertices.push_back({ vertex.id, vertex.position });
        }
    }
    return vertices;
}

std::vector<QPlane3> build_hull_planes_from_vertices(std::span<const HullVertex> vertices)
{
    std::vector<QPlane3> planes;
    if (vertices.size() < 4) {
        return planes;
    }

    quader::QVec3 center;
    for (const HullVertex& vertex : vertices) {
        center += vertex.position;
    }
    center = center / static_cast<float>(vertices.size());

    for (std::size_t i = 0; i < vertices.size(); ++i) {
        for (std::size_t j = i + 1U; j < vertices.size(); ++j) {
            for (std::size_t k = j + 1U; k < vertices.size(); ++k) {
                const quader_geometry::QTriangle3<float> triangle {
                    geometry_vec3(vertices[i].position),
                    geometry_vec3(vertices[j].position),
                    geometry_vec3(vertices[k].position),
                };
                const quader_geometry::QVec3f area_vector = quader_geometry::triangle_area_vector(triangle);
                if (quader_geometry::length_squared(area_vector) * 4.0F <=
                    kEpsilon) {
                  continue;
                }
                quader_geometry::QPlane3<float> plane =
                    quader_geometry::plane_from_point_normal<float>(
                        triangle.a, area_vector, kEpsilon);
                if (quader_geometry::length_squared(plane.normal) <=
                    kEpsilon * kEpsilon) {
                  continue;
                }
                bool has_front = false;
                bool has_back = false;
                for (const HullVertex& point : vertices) {
                    const float side = quader_geometry::signed_distance_to_plane<float>(geometry_vec3(point.position), plane);
                    if (side > kHullDistanceEpsilon) {
                      has_front = true;
                    } else if (side < -kHullDistanceEpsilon) {
                      has_back = true;
                    }
                    if (has_front && has_back) {
                        break;
                    }
                }
                if (has_front && has_back) {
                    continue;
                }
                if (quader_geometry::signed_distance_to_plane<float>(geometry_vec3(center), plane) > 0.0F) {
                    plane.normal = plane.normal * -1.0F;
                    plane.d = -plane.d;
                }
                append_unique_hull_plane(planes, plane);
            }
        }
    }

    return planes;
}

namespace {

bool point_inside_hull_planes(quader::QVec3 point, std::span<const QPlane3> planes)
{
  return std::ranges::all_of(planes, [point](const QPlane3 &plane) {
    return quader_geometry::plane_side<float>(geometry_vec3(point), plane,
                                              kHullDistanceEpsilon) !=
           quader_geometry::QPlaneSide::Positive;
  });
}

std::optional<quader::QVec3> intersect_hull_planes(const QPlane3& a, const QPlane3& b, const QPlane3& c)
{
  const quader_geometry::QThreePlaneIntersection<float> intersection =
      quader_geometry::intersect_three_planes(a, b, c, kHullDistanceEpsilon);
  if (!intersection.hit) {
    return std::nullopt;
  }
    return poly_vec3(intersection.point);
}

} // namespace

ElementId hull_vertex_id_for_position(std::span<const HullVertex> source_vertices, quader::QVec3 position)
{
  ElementId best_id = kInvalidElementId;
  float best_distance = std::numeric_limits<float>::infinity();
  for (const HullVertex &vertex : source_vertices) {
    const float distance = quader_geometry::length_squared(
        geometry_vec3(vertex.position) - geometry_vec3(position));
    if (distance < best_distance) {
      best_distance = distance;
      best_id = vertex.id;
    }
    }
    return best_distance <= kHullDistanceEpsilon * kHullDistanceEpsilon
               ? best_id
               : kInvalidElementId;
}

const HullVertex* find_hull_vertex_by_id(const HullGeometry& geometry, ElementId id)
{
    const auto found = std::ranges::find_if(geometry.vertices, [id](const HullVertex& vertex) {
        return vertex.id == id;
    });
    return found == geometry.vertices.end() ? nullptr : &*found;
}

quader::QVec3 hull_position_for_id(const HullGeometry& geometry, ElementId id)
{
    const HullVertex* vertex = find_hull_vertex_by_id(geometry, id);
    return vertex == nullptr ? quader::QVec3 {} : vertex->position;
}

void sort_hull_face_vertices(const HullGeometry& geometry, HullFace& face)
{
    if (face.vertex_ids.size() < 3) {
        return;
    }

    quader::QVec3 center;
    for (const ElementId vertex_id : face.vertex_ids) {
        center += hull_position_for_id(geometry, vertex_id);
    }
    center = center / static_cast<float>(face.vertex_ids.size());

    const QPlane3& geometry_face_plane = face.plane;
    quader_geometry::QVec3f preferred_u =
        quader_geometry::cross(geometry_face_plane.normal, quader_geometry::QVec3f { 0.0F, 1.0F, 0.0F });
    if (quader_geometry::length_squared(preferred_u) <= kEpsilon) {
      preferred_u =
          quader_geometry::cross(geometry_face_plane.normal,
                                 quader_geometry::QVec3f{1.0F, 0.0F, 0.0F});
    }
    const quader_geometry::QPlaneBasis3<float> basis =
        quader_geometry::make_plane_basis(geometry_vec3(center),
                                          geometry_face_plane.normal,
                                          preferred_u, kEpsilon);
    if (!basis.valid) {
        return;
    }

    std::ranges::sort(face.vertex_ids, [&geometry, basis](ElementId left_id, ElementId right_id) {
        const quader_geometry::QPlaneCoordinates3<float> left_coordinates =
            quader_geometry::plane_coordinates(geometry_vec3(hull_position_for_id(geometry, left_id)), basis);
        const quader_geometry::QPlaneCoordinates3<float> right_coordinates =
            quader_geometry::plane_coordinates(geometry_vec3(hull_position_for_id(geometry, right_id)), basis);
        const float left_angle = std::atan2(left_coordinates.v, left_coordinates.u);
        const float right_angle = std::atan2(right_coordinates.v, right_coordinates.u);
        return left_angle < right_angle;
    });

    const quader_geometry::QTriangle3<float> orientation_triangle {
        geometry_vec3(hull_position_for_id(geometry, face.vertex_ids[0])),
        geometry_vec3(hull_position_for_id(geometry, face.vertex_ids[1])),
        geometry_vec3(hull_position_for_id(geometry, face.vertex_ids[2])),
    };
    if (quader_geometry::dot(
            quader_geometry::triangle_area_vector(orientation_triangle),
            geometry_face_plane.normal) < 0.0F) {
        std::ranges::reverse(face.vertex_ids);
    }
}

bool hull_has_equivalent_face(const HullGeometry& geometry, const QPlane3& plane)
{
  return std::ranges::any_of(geometry.faces, [&plane](const HullFace &face) {
    return face.vertex_ids.size() >= 3 &&
           quader_geometry::planes_nearly_equal(face.plane, plane,
                                                kHullDistanceEpsilon);
  });
}

bool build_convex_hull_geometry(std::span<const HullVertex> source_vertices, HullGeometry& geometry)
{
    geometry = {};
    const std::vector<QPlane3> planes = build_hull_planes_from_vertices(source_vertices);
    if (planes.size() < 4) {
        return false;
    }

    for (std::size_t i = 0; i < planes.size(); ++i) {
        for (std::size_t j = i + 1U; j < planes.size(); ++j) {
            for (std::size_t k = j + 1U; k < planes.size(); ++k) {
                const std::optional<quader::QVec3> point = intersect_hull_planes(planes[i], planes[j], planes[k]);
                if (!point.has_value() || !point_inside_hull_planes(*point, planes)) {
                    continue;
                }

                const ElementId vertex_id = hull_vertex_id_for_position(source_vertices, *point);
                if (vertex_id == kInvalidElementId) {
                  continue;
                }
                const bool duplicate = std::ranges::any_of(geometry.vertices, [vertex_id](const HullVertex& vertex) {
                    return vertex.id == vertex_id;
                });
                if (!duplicate) {
                    const auto source = std::ranges::find_if(source_vertices, [vertex_id](const HullVertex& vertex) {
                        return vertex.id == vertex_id;
                    });
                    if (source != source_vertices.end()) {
                        geometry.vertices.push_back(*source);
                    }
                }
            }
        }
    }

    if (geometry.vertices.size() < 4) {
        return false;
    }

    for (const QPlane3& plane : planes) {
        if (hull_has_equivalent_face(geometry, plane)) {
            continue;
        }

        HullFace face;
        face.plane = plane;
        const QPlane3& geometry_hull_plane = plane;
        for (const HullVertex& vertex : geometry.vertices) {
          if (std::abs(quader_geometry::signed_distance_to_plane<float>(
                  geometry_vec3(vertex.position), geometry_hull_plane)) <=
              kHullDistanceEpsilon) {
            face.vertex_ids.push_back(vertex.id);
          }
        }
        if (face.vertex_ids.size() >= 3) {
            sort_hull_face_vertices(geometry, face);
            geometry.faces.push_back(std::move(face));
        }
    }

    return geometry.faces.size() >= 4;
}

std::vector<ElementId> sorted_face_vertex_ids(std::span<const ElementId> vertex_ids)
{
    std::vector<ElementId> sorted(vertex_ids.begin(), vertex_ids.end());
    std::ranges::sort(sorted);
    return sorted;
}

bool document_matches_convex_hull_shape(const Document& document, const HullGeometry& hull)
{
    if (hull.vertices.size() != document.vertices.size() || hull.faces.size() != document.faces.size()) {
        return false;
    }

    std::vector<std::vector<ElementId>> hull_faces;
    hull_faces.reserve(hull.faces.size());
    for (const HullFace& face : hull.faces) {
        hull_faces.push_back(sorted_face_vertex_ids(face.vertex_ids));
    }

    for (const Face& face : document.faces) {
        const std::vector<ElementId> face_key = sorted_face_vertex_ids(face.vertices);
        const auto found = std::ranges::find(hull_faces, face_key);
        if (found == hull_faces.end()) {
            return false;
        }
        hull_faces.erase(found);
    }

    return hull_faces.empty();
}

std::optional<std::pair<HullFace, HullFace>> split_hull_face_by_edge(const HullFace& face, Edge edge)
{
    const auto first = std::ranges::find(face.vertex_ids, edge.a);
    const auto second = std::ranges::find(face.vertex_ids, edge.b);
    if (first == face.vertex_ids.end() || second == face.vertex_ids.end()) {
        return std::nullopt;
    }

    const std::size_t first_index = static_cast<std::size_t>(std::distance(face.vertex_ids.begin(), first));
    const std::size_t second_index = static_cast<std::size_t>(std::distance(face.vertex_ids.begin(), second));
    const std::size_t count = face.vertex_ids.size();
    if (count < 4 ||
        (first_index + 1U) % count == second_index ||
        (second_index + 1U) % count == first_index) {
        return std::nullopt;
    }

    HullFace left;
    left.plane = face.plane;
    for (std::size_t index = first_index;; index = (index + 1U) % count) {
        left.vertex_ids.push_back(face.vertex_ids[index]);
        if (index == second_index) {
            break;
        }
    }

    HullFace right;
    right.plane = face.plane;
    for (std::size_t index = second_index;; index = (index + 1U) % count) {
        right.vertex_ids.push_back(face.vertex_ids[index]);
        if (index == first_index) {
            break;
        }
    }

    if (left.vertex_ids.size() < 3 || right.vertex_ids.size() < 3) {
        return std::nullopt;
    }
    return std::pair { left, right };
}

std::vector<Edge> source_split_edges_for_hull_face(
        const Document& source_document,
        const HullFace& hull_face,
        const std::set<ElementId>& merge_vertex_ids,
        ElementId survivor_vertex_id)
{
    std::vector<Edge> split_edges;
    for (const Face& source_face : source_document.faces) {
        if (source_face.vertices.size() < 2) {
            continue;
        }
        for (std::size_t index = 0; index < source_face.vertices.size(); ++index) {
            const ElementId a = mapped_vertex_for_merge(source_face.vertices[index], merge_vertex_ids, survivor_vertex_id);
            const ElementId b = mapped_vertex_for_merge(source_face.vertices[(index + 1U) % source_face.vertices.size()], merge_vertex_ids, survivor_vertex_id);
            if (a == b ||
                !contains_id(hull_face.vertex_ids, a) ||
                !contains_id(hull_face.vertex_ids, b)) {
                continue;
            }

            const Edge edge = make_edge(a, b);
            const auto already_present = std::ranges::find_if(split_edges, [edge](Edge existing) {
                return make_edge(existing.a, existing.b) == edge;
            });
            if (already_present == split_edges.end()) {
                split_edges.push_back(edge);
            }
        }
    }
    return split_edges;
}

std::vector<HullFace> split_hull_face_by_source_edges(
        const Document& source_document,
        const HullFace& hull_face,
        const std::set<ElementId>& merge_vertex_ids,
        ElementId survivor_vertex_id)
{
    std::vector<HullFace> faces { hull_face };
    const std::vector<Edge> split_edges = source_split_edges_for_hull_face(source_document, hull_face, merge_vertex_ids, survivor_vertex_id);
    for (const Edge& edge : split_edges) {
        for (std::size_t face_index = 0; face_index < faces.size(); ++face_index) {
            std::optional<std::pair<HullFace, HullFace>> split = split_hull_face_by_edge(faces[face_index], edge);
            if (!split.has_value()) {
                continue;
            }

            faces[face_index] = std::move(split->first);
            faces.push_back(std::move(split->second));
            break;
        }
    }
    return faces;
}

Document document_from_hull_geometry(
        const Document& source_document,
        const HullGeometry& hull,
        const std::set<ElementId>& merge_vertex_ids,
        ElementId survivor_vertex_id)
{
    Document document;
    document.next_vertex_id = source_document.next_vertex_id;
    document.next_face_id = source_document.next_face_id;
    document.vertices.reserve(hull.vertices.size());
    for (const HullVertex& hull_vertex : hull.vertices) {
        document.vertices.push_back({ hull_vertex.id, hull_vertex.position });
    }

    std::vector<MergeFaceUvAssignment> uv_assignments;
    for (const HullFace& hull_face : hull.faces) {
        for (const HullFace& split_face : split_hull_face_by_source_edges(source_document, hull_face, merge_vertex_ids, survivor_vertex_id)) {
            Face face;
            face.id = next_valid_face_id(document);
            face.vertices = split_face.vertex_ids;
            face.material_slot = 0;
            face.uvs.clear();
            document.faces.push_back(std::move(face));
            std::optional<FaceUvProjectionAssignment> assignment = face_uv_projection_assignment_from_source(
                document,
                document.faces.back(),
                source_document,
                merge_vertex_ids,
                survivor_vertex_id);
            if (assignment.has_value() &&
                assign_face_uvs_from_projection_assignment(document, document.faces.back(), *assignment)) {
                uv_assignments.push_back({ document.faces.back().id, *assignment });
            }
        }
    }
    stitch_merge_split_uv_density(document, uv_assignments);
    return document;
}

bool document_is_simple_convex_hull(const Document& document)
{
    if (!document_is_closed_manifold(document)) {
        return false;
    }

    const std::vector<HullVertex> source_vertices = unique_hull_vertices_from_document(document);
    HullGeometry hull;
    return build_convex_hull_geometry(source_vertices, hull) && document_matches_convex_hull_shape(document, hull);
}

bool build_convex_hull_vertex_merge_candidate(
        const Document& document,
        Document& candidate,
        const std::set<ElementId>& merge_vertex_ids,
        ElementId survivor_vertex_id,
        quader::QVec3 target_position)
{
    if (!document_is_simple_convex_hull(document)) {
        return false;
    }

    std::vector<HullVertex> merged_vertices;
    merged_vertices.reserve(document.vertices.size());
    for (const Vertex& vertex : document.vertices) {
        if (merge_vertex_ids.contains(vertex.id)) {
            continue;
        }
        const quader::QVec3 position = vertex.id == survivor_vertex_id ? target_position : vertex.position;
        const auto duplicate_position = std::ranges::find_if(merged_vertices, [position](const HullVertex& existing) {
            return points_nearly_equal(existing.position, position);
        });
        if (duplicate_position == merged_vertices.end()) {
            merged_vertices.push_back({ vertex.id, position });
        } else if (vertex.id == survivor_vertex_id) {
            *duplicate_position = { vertex.id, position };
        }
    }

    if (merged_vertices.size() < 4 ||
        std::ranges::none_of(merged_vertices, [survivor_vertex_id](const HullVertex& vertex) { return vertex.id == survivor_vertex_id; })) {
        return false;
    }

    HullGeometry hull;
    if (!build_convex_hull_geometry(merged_vertices, hull)) {
        return false;
    }

    candidate = document_from_hull_geometry(document, hull, merge_vertex_ids, survivor_vertex_id);
    return find_vertex(candidate, survivor_vertex_id) != nullptr && every_face_triangulates(candidate);
}

bool every_face_triangulates(const Document& document)
{
    return std::ranges::all_of(document.faces, [&document](const Face& face) {
        return !triangulate_face_local_indices(document, face).empty();
    });
}

ElementId mapped_vertex_for_merge(ElementId vertex_id, const std::set<ElementId>& merge_vertex_ids, ElementId active_vertex_id)
{
    return merge_vertex_ids.contains(vertex_id) ? active_vertex_id : vertex_id;
}

bool face_needs_vertex_merge_repair(
        const Face& face,
        const std::set<ElementId>& merge_vertex_ids,
        ElementId survivor_vertex_id,
        bool survivor_vertex_moved)
{
    return face_uses_any_vertex(face, merge_vertex_ids) ||
        (survivor_vertex_moved && face_uses_vertex(face, survivor_vertex_id));
}

float face_loop_area_score(const Document& document, std::span<const ElementId> vertex_ids)
{
    quader::QVec3 area;
    if (vertex_ids.size() < 3) {
        return 0.0F;
    }

    for (std::size_t index = 0; index < vertex_ids.size(); ++index) {
        const Vertex* current = find_vertex(document, vertex_ids[index]);
        const Vertex* next = find_vertex(document, vertex_ids[(index + 1U) % vertex_ids.size()]);
        if (current == nullptr || next == nullptr) {
            return 0.0F;
        }

        area.x += (current->position.y - next->position.y) * (current->position.z + next->position.z);
        area.y += (current->position.z - next->position.z) * (current->position.x + next->position.x);
        area.z += (current->position.x - next->position.x) * (current->position.y + next->position.y);
    }
    return quader_geometry::length_squared(geometry_vec3(area));
}

bool face_loop_is_valid_after_merge(const Document& document, std::span<const ElementId> vertex_ids)
{
    if (vertex_ids.size() < 3 || has_repeated_vertex(vertex_ids)) {
        return false;
    }
    for (const ElementId vertex_id : vertex_ids) {
        if (find_vertex(document, vertex_id) == nullptr) {
            return false;
        }
    }

    Face face;
    face.vertices.assign(vertex_ids.begin(), vertex_ids.end());
    return face_loop_area_score(document, face.vertices) >
               kFaceAreaScoreEpsilon &&
           !triangulate_face_local_indices(document, face).empty();
}

Face make_repaired_merge_face(const Face& source_face, std::vector<ElementId> vertices)
{
    Face repaired;
    repaired.id = source_face.id;
    repaired.vertices = std::move(vertices);
    repaired.material_slot = source_face.material_slot;
    repaired.uvs.clear();
    return repaired;
}

std::vector<ElementId> duplicate_face_key(const Face& face)
{
    std::vector<ElementId> best;
    if (face.vertices.empty()) {
        return best;
    }

    const auto consider_loop = [&best](const std::vector<ElementId>& loop) {
        for (std::size_t start = 0; start < loop.size(); ++start) {
            std::vector<ElementId> candidate;
            candidate.reserve(loop.size());
            for (std::size_t offset = 0; offset < loop.size(); ++offset) {
                candidate.push_back(loop[(start + offset) % loop.size()]);
            }
            if (best.empty() || std::lexicographical_compare(candidate.begin(), candidate.end(), best.begin(), best.end())) {
                best = std::move(candidate);
            }
        }
    };

    consider_loop(face.vertices);
    std::vector<ElementId> reversed = face.vertices;
    std::ranges::reverse(reversed);
    consider_loop(reversed);
    return best;
}

bool remove_duplicate_faces_created_by_merge(Document& document, const std::set<ElementId>& touched_face_ids)
{
    if (touched_face_ids.empty()) {
        return false;
    }

    std::map<std::vector<ElementId>, std::size_t> kept_face_index_by_key;
    std::vector<Face> unique_faces;
    unique_faces.reserve(document.faces.size());
    bool removed = false;

    for (const Face& face : document.faces) {
        std::vector<ElementId> key = duplicate_face_key(face);
        if (key.size() < 3U || has_repeated_vertex(key)) {
            unique_faces.push_back(face);
            continue;
        }

        const auto kept = kept_face_index_by_key.find(key);
        if (kept == kept_face_index_by_key.end()) {
            kept_face_index_by_key.emplace(std::move(key), unique_faces.size());
            unique_faces.push_back(face);
            continue;
        }

        Face& kept_face = unique_faces[kept->second];
        const bool kept_was_touched = touched_face_ids.contains(kept_face.id);
        const bool current_was_touched = touched_face_ids.contains(face.id);
        if (!kept_was_touched && !current_was_touched) {
            unique_faces.push_back(face);
            continue;
        }

        if (kept_was_touched && !current_was_touched) {
            kept_face = face;
        }
        removed = true;
    }

    if (removed) {
        document.faces = std::move(unique_faces);
    }
    return removed;
}

std::vector<Face> repaired_faces_for_vertex_merge(
        const Document& source_document,
        const Document& candidate_document,
        const Face& source_face,
        const std::set<ElementId>& merge_vertex_ids,
        ElementId active_vertex_id)
{
    std::vector<ElementId> compact_vertices = compact_face_vertices_for_merge(source_face, merge_vertex_ids, active_vertex_id);
    if (face_loop_is_valid_after_merge(candidate_document, compact_vertices)) {
        return { make_repaired_merge_face(source_face, std::move(compact_vertices)) };
    }
    if (has_repeated_vertex(compact_vertices)) {
        return {};
    }

    std::vector<std::size_t> active_occurrences;
    active_occurrences.reserve(compact_vertices.size());
    for (std::size_t index = 0; index < compact_vertices.size(); ++index) {
        if (compact_vertices[index] == active_vertex_id) {
            active_occurrences.push_back(index);
        }
    }

    std::vector<ElementId> best_vertices;
    float best_area_score = 0.0F;
    if (active_occurrences.size() > 1) {
        for (const std::size_t kept_active_index : active_occurrences) {
            std::vector<ElementId> candidate_vertices;
            candidate_vertices.reserve(compact_vertices.size());
            for (std::size_t index = 0; index < compact_vertices.size(); ++index) {
                if (compact_vertices[index] == active_vertex_id && index != kept_active_index) {
                    continue;
                }
                if (!candidate_vertices.empty() && candidate_vertices.back() == compact_vertices[index]) {
                    continue;
                }
                candidate_vertices.push_back(compact_vertices[index]);
            }

            if (candidate_vertices.size() > 1 && candidate_vertices.front() == candidate_vertices.back()) {
                candidate_vertices.pop_back();
            }
            if (!face_loop_is_valid_after_merge(candidate_document, candidate_vertices)) {
                continue;
            }

            const float area_score = face_loop_area_score(candidate_document, candidate_vertices);
            if (best_vertices.empty() || area_score > best_area_score) {
                best_vertices = std::move(candidate_vertices);
                best_area_score = area_score;
            }
        }
    }

    if (!best_vertices.empty()) {
        return { make_repaired_merge_face(source_face, std::move(best_vertices)) };
    }

    std::vector<Face> repaired_faces;
    std::set<std::array<ElementId, 3>> emitted_triangles;
    for (const Triangle& triangle : triangulate_face_local_indices(source_document, source_face)) {
        std::array<ElementId, 3> vertices {
            mapped_vertex_for_merge(source_face.vertices[triangle.a], merge_vertex_ids, active_vertex_id),
            mapped_vertex_for_merge(source_face.vertices[triangle.b], merge_vertex_ids, active_vertex_id),
            mapped_vertex_for_merge(source_face.vertices[triangle.c], merge_vertex_ids, active_vertex_id),
        };
        if (vertices[0] == vertices[1] || vertices[0] == vertices[2] || vertices[1] == vertices[2]) {
            continue;
        }

        std::array<ElementId, 3> triangle_key = vertices;
        std::ranges::sort(triangle_key);
        if (!emitted_triangles.insert(triangle_key).second || !face_loop_is_valid_after_merge(candidate_document, vertices)) {
            continue;
        }

        repaired_faces.push_back(make_repaired_merge_face(source_face, { vertices[0], vertices[1], vertices[2] }));
    }

    return repaired_faces;
}

OperationResult build_topology_vertex_merge_candidate(
        const Document& document,
        Document& candidate,
        const std::set<ElementId>& merge_vertex_ids,
        ElementId survivor_vertex_id,
        quader::QVec3 target_position)
{
    candidate = document;
    Vertex* survivor_vertex = find_vertex(candidate, survivor_vertex_id);
    if (survivor_vertex == nullptr) {
        return { false, "Merge would remove the survivor vertex." };
    }

    const bool survivor_vertex_moved =
        length_squared(survivor_vertex->position - target_position) > kEpsilon;
    bool changed = survivor_vertex_moved;
    survivor_vertex->position = target_position;

    std::vector<Face> rebuilt_faces;
    rebuilt_faces.reserve(document.faces.size());
    std::vector<MergeFaceUvAssignment> uv_assignments;
    std::set<ElementId> merge_touched_face_ids;

    std::set<ElementId> used_face_ids;
    for (const Face& face : document.faces) {
        used_face_ids.insert(face.id);
    }

    auto next_repaired_face_id = [&candidate, &used_face_ids]() {
      while (candidate.next_face_id == kInvalidElementId ||
             used_face_ids.contains(candidate.next_face_id)) {
        ++candidate.next_face_id;
      }
      const ElementId face_id = candidate.next_face_id++;
      used_face_ids.insert(face_id);
      return face_id;
    };

    for (const Face& face : document.faces) {
        if (!face_needs_vertex_merge_repair(face, merge_vertex_ids, survivor_vertex_id, survivor_vertex_moved)) {
            rebuilt_faces.push_back(face);
            continue;
        }

        std::vector<Face> repaired_faces =
                repaired_faces_for_vertex_merge(document, candidate, face, merge_vertex_ids, survivor_vertex_id);
        if (repaired_faces.empty()) {
            merge_touched_face_ids.insert(face.id);
            changed = true;
            continue;
        }

        for (std::size_t index = 0; index < repaired_faces.size(); ++index) {
            Face& repaired_face = repaired_faces[index];
            repaired_face.id = index == 0 ? face.id : next_repaired_face_id();
            merge_touched_face_ids.insert(repaired_face.id);
            repaired_face.material_slot = face.material_slot;
            repaired_face.uvs.clear();
            const std::optional<FaceUvProjectionAssignment> assignment = face_uv_projection_assignment_from_source(
                    candidate,
                    repaired_face,
                    document,
                    merge_vertex_ids,
                    survivor_vertex_id);
            if (assignment.has_value() &&
                    assign_face_uvs_from_projection_assignment(candidate, repaired_face, *assignment)) {
                uv_assignments.push_back({ repaired_face.id, *assignment });
            }
            rebuilt_faces.push_back(std::move(repaired_face));
        }

        changed = changed ||
            repaired_faces.size() != 1 ||
            rebuilt_faces.back().vertices != face.vertices ||
            !face.uvs.empty();
    }

    candidate.faces = std::move(rebuilt_faces);
    changed = remove_duplicate_faces_created_by_merge(candidate, merge_touched_face_ids) || changed;
    stitch_merge_split_uv_density(candidate, uv_assignments);
    std::erase_if(candidate.vertices, [&merge_vertex_ids](const Vertex& vertex) {
        return merge_vertex_ids.contains(vertex.id);
    });

    changed = changed || candidate.vertices.size() != document.vertices.size();
    if (!changed) {
        return { false, "No selected vertices were merged." };
    }

    restore_source_face_orientation(document, candidate);
    if (find_vertex(candidate, survivor_vertex_id) == nullptr) {
        return { false, "Merge would remove the survivor vertex." };
    }
    if (!every_face_triangulates(candidate)) {
        return { false, "Merge would create a face that cannot be triangulated." };
    }
    if (document_has_unreferenced_vertices(candidate)) {
        return { false, "Merge would create a floating vertex." };
    }

    return { true, {} };
}

OperationResult build_vertex_merge_candidate(
        const Document& document,
        Document& candidate,
        const std::set<ElementId>& merge_vertex_ids,
        ElementId survivor_vertex_id,
        quader::QVec3 target_position)
{
    const OperationResult topology_result =
            build_topology_vertex_merge_candidate(document, candidate, merge_vertex_ids, survivor_vertex_id, target_position);
    if (topology_result.changed) {
        return topology_result;
    }

    if (build_convex_hull_vertex_merge_candidate(document, candidate, merge_vertex_ids, survivor_vertex_id, target_position)) {
        return { true, {} };
    }

    return topology_result;
}


void prune_unused_vertices(Document& document)
{
    std::vector<ElementId> referenced_vertices;
    for (const Face& face : document.faces) {
        referenced_vertices.insert(referenced_vertices.end(), face.vertices.begin(), face.vertices.end());
    }
    std::ranges::sort(referenced_vertices);
    referenced_vertices.erase(std::ranges::unique(referenced_vertices).begin(), referenced_vertices.end());

    std::erase_if(document.vertices, [&referenced_vertices](const Vertex& vertex) {
        return !contains_id(referenced_vertices, vertex.id);
    });
}

void prune_invalid_faces(Document& document)
{
    std::erase_if(document.faces, [&document](const Face& face) {
        if (face.vertices.size() < 3) {
            return true;
        }

        std::set<ElementId> unique_vertices;
        for (const ElementId vertex_id : face.vertices) {
            const bool vertex_exists = std::ranges::any_of(document.vertices, [vertex_id](const Vertex& vertex) {
                return vertex.id == vertex_id;
            });
            if (!vertex_exists) {
                return true;
            }
            unique_vertices.insert(vertex_id);
        }
        return unique_vertices.size() < 3;
    });
}

} // namespace quader_poly::document_internal

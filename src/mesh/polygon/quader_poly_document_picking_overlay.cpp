////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/document.hpp>

#include <diagnostics/profile.hpp>

#include <mesh/polygon/internal/quader_poly_document_constants.hpp>
#include <mesh/polygon/internal/quader_poly_document_knife_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_mesh_internal.hpp>
#include <mesh/polygon/internal/quader_poly_document_picking_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_topology_helpers.hpp>

#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace quader_poly {

using namespace document_internal;

namespace {

/**
 * Represents a Face Ray Hit value used by the polygon document and mesh editing core.
 */
struct FaceRayHit {
  ElementId face_id = kInvalidElementId;
  float distance = std::numeric_limits<float>::max();
  bool front_facing = false;
};

quader::QVec3 min_vec(quader::QVec3 left, quader::QVec3 right)
{
    return {
        std::min(left.x, right.x),
        std::min(left.y, right.y),
        std::min(left.z, right.z),
    };
}

quader::QVec3 max_vec(quader::QVec3 left, quader::QVec3 right)
{
    return {
        std::max(left.x, right.x),
        std::max(left.y, right.y),
        std::max(left.z, right.z),
    };
}

quader_geometry::QAabb3<float> triangle_pick_bounds(const PickFaceTriangle& triangle)
{
    return {
        geometry_vec3(triangle.bounds_min),
        geometry_vec3(triangle.bounds_max),
    };
}

bool face_is_front_facing_from_ray(quader::QVec3 normal, const Ray& ray)
{
  return dot(normal, ray.direction) < -kEpsilon;
}

bool face_hit_is_better(const FaceRayHit& candidate, const std::optional<FaceRayHit>& nearest, const PickOptions& options)
{
    if (!nearest.has_value()) {
        return true;
    }
    if (options.prefer_front_facing_faces && candidate.front_facing != nearest->front_facing) {
        return candidate.front_facing;
    }
    if (std::abs(candidate.distance - nearest->distance) > kEpsilon) {
      return candidate.distance < nearest->distance;
    }
    return !options.prefer_front_facing_faces &&
        candidate.front_facing &&
        !nearest->front_facing;
}

float face_hit_query_max_distance(
    const std::optional<FaceRayHit>& nearest,
    bool front_facing,
    const PickOptions& options)
{
    if (!nearest.has_value()) {
        return std::numeric_limits<float>::infinity();
    }
    if (options.prefer_front_facing_faces && front_facing != nearest->front_facing) {
        return std::numeric_limits<float>::infinity();
    }
    return nearest->distance;
}

std::optional<FaceRayHit> nearest_face_ray_hit(std::span<const PickFaceTriangle> triangles, const Ray& ray, const PickOptions& options = {})
{
    QDR_PROFILE_SCOPE("qdr_document.nearest_cached_face_ray_hit");
    std::optional<FaceRayHit> nearest;
    const quader_geometry::QRay3<float> geometry_pick_ray = geometry_ray(ray);
    const float picking_epsilon = std::nextafter(kEpsilon, 0.0F);
    for (const PickFaceTriangle& triangle : triangles) {
        const bool front_facing = face_is_front_facing_from_ray(triangle.normal, ray);
        if (!front_facing && !options.pick_backfaces) {
            continue;
        }

        const quader_geometry::QTriangle3<float> geometry_triangle {
            geometry_vec3(triangle.a),
            geometry_vec3(triangle.b),
            geometry_vec3(triangle.c),
        };
        const quader_geometry::QRayBoundedTriangleQueryOptions<float> query_options {
            0.0F,
            face_hit_query_max_distance(nearest, front_facing, options),
            picking_epsilon,
        };
        const quader_geometry::QRayBoundedTriangleHit<float> hit =
            quader_geometry::intersect_ray_bounded_triangle<float>(
                geometry_pick_ray,
                geometry_triangle,
                triangle_pick_bounds(triangle),
                query_options);
        const FaceRayHit candidate { triangle.face_id, hit.triangle_hit.distance, front_facing };
        if (hit.hit && face_hit_is_better(candidate, nearest, options)) {
            nearest = candidate;
        }
    }
    return nearest;
}

bool face_can_be_picked(const Face& face, const PickOptions& options)
{
    return !options.hides_material_slot(face.material_slot);
}

bool vertex_can_be_picked(const Document& document, ElementId vertex_id, const PickOptions& options)
{
    if (!options.has_hidden_material_slot) {
        return true;
    }

    bool used_by_face = false;
    for (const Face& face : document.faces) {
        if (!face_uses_vertex(face, vertex_id)) {
            continue;
        }

        used_by_face = true;
        if (face_can_be_picked(face, options)) {
            return true;
        }
    }
    return !used_by_face;
}

bool edge_can_be_picked(const Document& document, Edge edge, const PickOptions& options)
{
    if (!options.has_hidden_material_slot) {
        return true;
    }

    bool used_by_face = false;
    for (const Face& face : document.faces) {
        if (!face_uses_edge(face, edge)) {
            continue;
        }

        used_by_face = true;
        if (face_can_be_picked(face, options)) {
            return true;
        }
    }
    return !used_by_face;
}

bool vertex_has_front_facing_pickable_face(
    const Document& document,
    ElementId vertex_id,
    const Ray& ray,
    const PickOptions& options)
{
    for (const Face& face : document.faces) {
        if (!face_uses_vertex(face, vertex_id) || !face_can_be_picked(face, options)) {
            continue;
        }
        if (face_is_front_facing_from_ray(face_normal(document, face), ray)) {
            return true;
        }
    }
    return false;
}

bool edge_has_front_facing_pickable_face(
    const Document& document,
    Edge edge,
    const Ray& ray,
    const PickOptions& options)
{
    for (const Face& face : document.faces) {
        if (!face_uses_edge(face, edge) || !face_can_be_picked(face, options)) {
            continue;
        }
        if (face_is_front_facing_from_ray(face_normal(document, face), ray)) {
            return true;
        }
    }
    return false;
}

ElementId edge_pick_context_face_id(
    const Document& document,
    Edge edge,
    const Ray& ray,
    const PickOptions& options,
    const std::optional<FaceRayHit>& surface_hit)
{
    ElementId best_face_id = kInvalidElementId;
    float best_score = -std::numeric_limits<float>::infinity();
    for (const Face& face : document.faces) {
        if (!face_uses_edge(face, edge) || !face_can_be_picked(face, options)) {
            continue;
        }
        const float score = -dot(face_normal(document, face), ray.direction);
        if (best_face_id == kInvalidElementId ||
            score > best_score + kEpsilon) {
            best_face_id = face.id;
            best_score = score;
        }
    }
    if (best_face_id != kInvalidElementId) {
        return best_face_id;
    }
    if (surface_hit.has_value()) {
        const Face* face = find_face(document, surface_hit->face_id);
        if (face != nullptr && face_uses_edge(*face, edge)) {
            return face->id;
        }
    }
    return best_face_id;
}

bool component_pick_is_better(
    float pick_distance,
    float ray_distance,
    bool front_facing,
    const PickResult& best,
    float best_pick_distance,
    bool best_front_facing,
    const PickOptions& options)
{
    if (!best.hit) {
        return true;
    }
    if (pick_distance < best_pick_distance - kEpsilon) {
      return true;
    }
    if (std::abs(pick_distance - best_pick_distance) > kEpsilon) {
      return false;
    }
    if (options.pick_backfaces && front_facing != best_front_facing) {
        return front_facing;
    }
    return ray_distance < best.distance;
}

std::optional<FaceRayHit> nearest_face_ray_hit(const Document& document, const Ray& ray, const PickOptions& options = {})
{
    QDR_PROFILE_SCOPE("qdr_document.nearest_face_ray_hit");
    std::optional<FaceRayHit> nearest;
    for (const Face& face : document.faces) {
        if (!face_can_be_picked(face, options)) {
            continue;
        }

        const quader::QVec3 normal = face_normal(document, face);
        const bool front_facing = face_is_front_facing_from_ray(normal, ray);
        if (!front_facing && !options.pick_backfaces) {
            continue;
        }

        const std::vector<Triangle> triangles = triangulate_face_local_indices(document, face);
        for (const Triangle& triangle : triangles) {
            const Vertex* a = find_vertex(document, face.vertices[triangle.a]);
            const Vertex* b = find_vertex(document, face.vertices[triangle.b]);
            const Vertex* c = find_vertex(document, face.vertices[triangle.c]);
            if (a == nullptr || b == nullptr || c == nullptr) {
                continue;
            }

            float distance = 0.0F;
            quader::QVec3 position;
            if (ray_intersects_triangle(ray, a->position, b->position, c->position, distance, position) &&
                face_hit_is_better({ face.id, distance, front_facing }, nearest, options)) {
                nearest = FaceRayHit { face.id, distance, front_facing };
            }
        }
    }
    return nearest;
}

bool component_depth_is_occluded(float component_distance, const std::optional<FaceRayHit>& surface_hit)
{
    return surface_hit.has_value() && component_distance > surface_hit->distance + 0.001F;
}

bool vertex_pick_is_occluded(const Document& document, ElementId vertex_id, float ray_distance, const std::optional<FaceRayHit>& surface_hit)
{
    if (!component_depth_is_occluded(ray_distance, surface_hit)) {
        return false;
    }

    const Face* surface_face = find_face(document, surface_hit->face_id);
    return surface_face == nullptr || !face_uses_vertex(*surface_face, vertex_id);
}

bool edge_pick_is_occluded(const Document& document, Edge edge, float ray_distance, const std::optional<FaceRayHit>& surface_hit)
{
    if (!component_depth_is_occluded(ray_distance, surface_hit)) {
        return false;
    }

    const Face* surface_face = find_face(document, surface_hit->face_id);
    return surface_face == nullptr || !face_uses_edge(*surface_face, edge);
}

} // namespace

std::vector<PickFaceTriangle> build_face_pick_geometry(const Document& document)
{
    return build_face_pick_geometry(document, {});
}

std::vector<PickFaceTriangle> build_face_pick_geometry(const Document& document, const PickOptions& options)
{
    QDR_PROFILE_SCOPE("qdr_document.build_face_pick_geometry");
    std::vector<PickFaceTriangle> result;
    result.reserve(document.faces.size());
    for (const Face& face : document.faces) {
        if (!face_can_be_picked(face, options)) {
            continue;
        }

        const quader::QVec3 normal = face_normal(document, face);
        const std::vector<Triangle> triangles = triangulate_face_local_indices(document, face);
        for (const Triangle& triangle : triangles) {
            const Vertex* a = find_vertex(document, face.vertices[triangle.a]);
            const Vertex* b = find_vertex(document, face.vertices[triangle.b]);
            const Vertex* c = find_vertex(document, face.vertices[triangle.c]);
            if (a == nullptr || b == nullptr || c == nullptr) {
                continue;
            }

            const quader::QVec3 bounds_min = min_vec(min_vec(a->position, b->position), c->position);
            const quader::QVec3 bounds_max = max_vec(max_vec(a->position, b->position), c->position);
            result.push_back({
                face.id,
                a->position,
                b->position,
                c->position,
                normal,
                bounds_min,
                bounds_max,
            });
        }
    }
    return result;
}

PickResult pick_face_geometry(std::span<const PickFaceTriangle> triangles, const Ray& local_ray, const PickOptions& options)
{
    QDR_PROFILE_SCOPE("qdr_document.pick_face_geometry");
    const Ray ray {
        local_ray.origin,
        normalize_or_zero(local_ray.direction),
    };
    if (length_squared(ray.direction) <= kEpsilon) {
      return {};
    }

    const std::optional<FaceRayHit> hit = nearest_face_ray_hit(triangles, ray, options);
    if (!hit.has_value()) {
        return {};
    }
    return {
        true,
        ElementKind::Face,
        kInvalidElementId,
        {},
        hit->face_id,
        hit->distance,
        ray.origin + ray.direction * hit->distance,
    };
}

PickResult pick_element(const Document& document, const Ray& local_ray, const PickOptions& options)
{
    QDR_PROFILE_SCOPE("qdr_document.pick_element");
    const Ray ray {
        local_ray.origin,
        normalize_or_zero(local_ray.direction),
    };
    if (length_squared(ray.direction) <= kEpsilon) {
      return {};
    }

    PickResult best;
    best.distance = std::numeric_limits<float>::max();
    float best_pick_distance = std::numeric_limits<float>::max();
    bool best_front_facing = false;
    const std::optional<FaceRayHit> surface_hit =
        options.mode == SelectionMode::Vertex ||
                options.mode == SelectionMode::Edge
            ? nearest_face_ray_hit(document, ray, options)
            : std::optional<FaceRayHit>{};

    if (options.mode == SelectionMode::Vertex) {
      for (const Vertex &vertex : document.vertices) {
        if (!vertex_can_be_picked(document, vertex.id, options)) {
          continue;
        }

        float ray_distance = 0.0F;
        const float distance =
            point_ray_distance(ray, vertex.position, ray_distance);
        const bool front_facing = vertex_has_front_facing_pickable_face(
            document, vertex.id, ray, options);
        if (distance <= options.vertex_radius &&
            !vertex_pick_is_occluded(document, vertex.id, ray_distance,
                                     surface_hit) &&
            component_pick_is_better(distance, ray_distance, front_facing, best,
                                     best_pick_distance, best_front_facing,
                                     options)) {
          best = {
              true,
              ElementKind::Vertex,
              vertex.id,
              {},
              kInvalidElementId,
              ray_distance,
              vertex.position,
          };
          best_pick_distance = distance;
          best_front_facing = front_facing;
        }
      }
      return best.hit ? best : PickResult{};
    }

    if (options.mode == SelectionMode::Edge) {
      for (const Edge &edge : document_edges(document)) {
        if (!edge_can_be_picked(document, edge, options)) {
          continue;
        }

        const Vertex *a = find_vertex(document, edge.a);
        const Vertex *b = find_vertex(document, edge.b);
        if (a == nullptr || b == nullptr) {
          continue;
        }

        float ray_distance = 0.0F;
        quader::QVec3 position;
        const float distance = segment_ray_distance(
            ray, a->position, b->position, ray_distance, position);
        const bool front_facing =
            edge_has_front_facing_pickable_face(document, edge, ray, options);
        if (distance <= options.edge_radius &&
            !edge_pick_is_occluded(document, edge, ray_distance, surface_hit) &&
            component_pick_is_better(distance, ray_distance, front_facing, best,
                                     best_pick_distance, best_front_facing,
                                     options)) {
          best = {
              true,     ElementKind::Edge, kInvalidElementId,
              edge,
              edge_pick_context_face_id(document, edge, ray, options,
                                        surface_hit),
              ray_distance,
              position,
          };
          best_pick_distance = distance;
          best_front_facing = front_facing;
        }
      }
      return best.hit ? best : PickResult{};
    }

    for (const Face& face : document.faces) {
        if (!face_can_be_picked(face, options)) {
            continue;
        }

        const quader::QVec3 normal = face_normal(document, face);
        const bool front_facing = face_is_front_facing_from_ray(normal, ray);
        if (!front_facing && !options.pick_backfaces) {
            continue;
        }

        const std::vector<Triangle> triangles = triangulate_face_local_indices(document, face);
        for (const Triangle& triangle : triangles) {
            const Vertex* a = find_vertex(document, face.vertices[triangle.a]);
            const Vertex* b = find_vertex(document, face.vertices[triangle.b]);
            const Vertex* c = find_vertex(document, face.vertices[triangle.c]);
            if (a == nullptr || b == nullptr || c == nullptr) {
                continue;
            }

            float distance = 0.0F;
            quader::QVec3 position;
            if (ray_intersects_triangle(ray, a->position, b->position, c->position, distance, position) &&
                face_hit_is_better({ face.id, distance, front_facing }, best.hit ? std::optional<FaceRayHit> {
                    FaceRayHit { best.face_id, best.distance, best_front_facing }
                } : std::optional<FaceRayHit> {}, options)) {
              best = {
                  true,    ElementKind::Face, kInvalidElementId, {},
                  face.id, distance,          position,
              };
              best_front_facing = front_facing;
            }
        }
    }
    return best.hit ? best : PickResult {};
}

OverlayData build_edge_loop_preview(const Document& document, Edge edge, float factor, const OverlayStyle& style)
{
    QDR_PROFILE_SCOPE("qdr_document.build_edge_loop_preview");
    OverlayData overlay;
    factor = std::clamp(factor, 0.01F, 0.99F);

    const std::vector<EdgeLoopFaceSplit> splits = collect_edge_loop_splits(document, edge);
    overlay.lines.reserve(splits.size());
    for (const EdgeLoopFaceSplit& split : splits) {
        const Face* face = find_face(document, split.face_id);
        if (face == nullptr || face->vertices.size() != 4) {
            continue;
        }

        const std::optional<std::size_t> edge_index = face_edge_index(*face, split.entry_edge);
        if (!edge_index.has_value()) {
            continue;
        }

        const std::size_t i0 = *edge_index;
        const std::size_t i1 = (i0 + 1U) % 4U;
        const std::size_t i2 = (i0 + 2U) % 4U;
        const std::size_t i3 = (i0 + 3U) % 4U;
        const Edge face_entry = directed_face_edge(*face, i0);
        const Edge entry = same_directed_edge(split.entry_edge, face_entry) ? face_entry : Edge { face->vertices[i1], face->vertices[i0] };
        const Edge opposite = oriented_loop_opposite_edge(*face, i0, entry);
        const std::optional<quader::QVec3> entry_position = split_edge_position(document, entry, factor);
        const std::optional<quader::QVec3> opposite_position = split_edge_position(document, opposite, factor);
        if (!entry_position.has_value() || !opposite_position.has_value()) {
            continue;
        }

        overlay.lines.push_back({
            *entry_position,
            *opposite_position,
            style.loop_preview_color,
            style.loop_preview_width_pixels,
        });
    }

    return overlay;
}

KnifeSegmentPreview build_knife_segment_preview(
    const Document& document,
    const KnifePointTarget& previous,
    const KnifePointTarget& current,
    const OverlayStyle& style)
{
    QDR_PROFILE_SCOPE("qdr_document.build_knife_segment_preview");
    KnifeSegmentPreview preview;
    KnifeSegmentCandidate candidate = build_knife_segment_candidate(document, previous, current);
    preview.valid = candidate.changed;
    preview.start = candidate.previous_position;
    preview.end = candidate.current_position;
    preview.message = std::move(candidate.message);
    if (preview.valid) {
        preview.overlay.lines.push_back({
            preview.start,
            preview.end,
            style.loop_preview_color,
            style.loop_preview_width_pixels,
        });
    }
    return preview;
}

OverlayData build_overlay(const Document& document, const Selection& selection, const OverlayStyle& style)
{
    QDR_PROFILE_SCOPE("qdr_document.build_overlay");
    OverlayData overlay;
    for (const Edge& edge : document_edges(document)) {
        const Vertex* a = find_vertex(document, edge.a);
        const Vertex* b = find_vertex(document, edge.b);
        if (a == nullptr || b == nullptr) {
            continue;
        }

        const bool selected = selection_contains(selection, edge);
        overlay.lines.push_back({
            a->position,
            b->position,
            selected ? style.selected_edge_color : style.edge_color,
            selected ? style.selected_edge_width_pixels : style.edge_width_pixels,
            edge,
        });
    }

    for (const Face& face : document.faces) {
        if (!selection_contains_face(selection, face.id) || face.vertices.size() < 2) {
            continue;
        }

        const std::vector<Triangle> triangles = triangulate_face_local_indices(document, face);
        for (const Triangle& triangle : triangles) {
            const Vertex* a = find_vertex(document, face.vertices[triangle.a]);
            const Vertex* b = find_vertex(document, face.vertices[triangle.b]);
            const Vertex* c = find_vertex(document, face.vertices[triangle.c]);
            if (a == nullptr || b == nullptr || c == nullptr) {
                continue;
            }

            overlay.triangles.push_back({
                a->position,
                b->position,
                c->position,
                style.face_fill_color,
                face.id,
            });
        }

        for (std::size_t index = 0; index < face.vertices.size(); ++index) {
            const Vertex* a = find_vertex(document, face.vertices[index]);
            const Vertex* b = find_vertex(document, face.vertices[(index + 1) % face.vertices.size()]);
            if (a != nullptr && b != nullptr) {
                overlay.lines.push_back({
                    a->position,
                    b->position,
                    style.face_border_color,
                    style.face_border_width_pixels,
                    make_edge(face.vertices[index], face.vertices[(index + 1) % face.vertices.size()]),
                    face.id,
                });
            }
        }
    }

    if (selection.mode == SelectionMode::Vertex) {
      for (const Vertex &vertex : document.vertices) {
        overlay.points.push_back({
            vertex.position,
            selection_contains(selection, vertex.id)
                ? style.selected_vertex_color
                : style.vertex_color,
            selection_contains(selection, vertex.id)
                ? style.selected_vertex_size_pixels
                : style.vertex_size_pixels,
            vertex.id,
        });
      }
    }

    return overlay;
}

} // namespace quader_poly

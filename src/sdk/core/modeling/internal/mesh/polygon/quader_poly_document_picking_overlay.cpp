////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/neutral/polygon/document.hpp>

#include <diagnostics/profile.hpp>

#include <quader/modeling/neutral/polygon/internal/quader_poly_document_constants.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_knife_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_mesh_internal.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_picking_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_topology_helpers.hpp>

#include <cmath>
#include <limits>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>
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

bool face_is_front_facing_from_ray(
    quader::QVec3 normal,
    const Ray& ray,
    bool reverse_face_winding = false)
{
  const float facing = dot(normal, ray.direction);
  return reverse_face_winding ? facing > kEpsilon : facing < -kEpsilon;
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
        const bool front_facing = face_is_front_facing_from_ray(
            triangle.normal, ray, options.reverse_face_winding);
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
    if (options.prefer_front_facing_faces &&
        front_facing != best_front_facing) {
        return front_facing;
    }
    return ray_distance < best.distance;
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

namespace document_internal {

std::uint64_t DocumentPickFacts::edge_key(Edge edge) noexcept
{
    edge = make_edge(edge.a, edge.b);
    return (static_cast<std::uint64_t>(edge.a) << 32U) |
        static_cast<std::uint64_t>(edge.b);
}

DocumentPickFacts::DocumentPickFacts(
    const Document& document,
    const PickOptions& options,
    PickWorkCounters* work_counters)
    : document_(document)
    , options_(options)
    , work_counters_(work_counters)
{
    QDR_PROFILE_SCOPE("qdr_document.build_pick_facts");
    if (work_counters_ != nullptr) {
        ++work_counters_->fact_sets_built;
    }
    face_facts_.reserve(document.faces.size());
    incident_faces_by_vertex_.reserve(document.vertices.size());
    incident_faces_by_edge_.reserve(document.faces.size() * 2U);

    std::set<std::pair<ElementId, ElementId>> unique_edges;
    for (std::size_t face_index = 0U;
         face_index < document.faces.size();
         ++face_index) {
        const Face& face = document.faces[face_index];
        FaceFact fact;
        fact.face = &face;
        fact.pickable = face_can_be_picked(face, options_);
        if (fact.pickable) {
            fact.normal = face_normal(document_, face);
        }
        face_facts_.push_back(std::move(fact));
        if (work_counters_ != nullptr) {
            ++work_counters_->face_facts_built;
        }

        std::unordered_set<ElementId> face_vertex_ids;
        face_vertex_ids.reserve(face.vertices.size());
        for (const ElementId vertex_id : face.vertices) {
            if (face_vertex_ids.insert(vertex_id).second) {
                incident_faces_by_vertex_[vertex_id].push_back(face_index);
            }
        }

        if (face.vertices.size() < 2U) {
            continue;
        }
        std::unordered_set<std::uint64_t> face_edge_keys;
        face_edge_keys.reserve(face.vertices.size());
        for (std::size_t vertex_index = 0U;
             vertex_index < face.vertices.size();
             ++vertex_index) {
            const Edge edge = make_edge(
                face.vertices[vertex_index],
                face.vertices[(vertex_index + 1U) % face.vertices.size()]);
            if (edge.a == kInvalidElementId ||
                edge.b == kInvalidElementId ||
                edge.a == edge.b) {
                continue;
            }
            unique_edges.emplace(edge.a, edge.b);
            const std::uint64_t key = edge_key(edge);
            if (face_edge_keys.insert(key).second) {
                incident_faces_by_edge_[key].push_back(face_index);
            }
        }
    }

    edges_.reserve(unique_edges.size());
    for (const auto& [a, b] : unique_edges) {
        edges_.push_back({a, b});
    }
}

std::span<const std::size_t> DocumentPickFacts::incident_faces(
    ElementId vertex_id) const
{
    const auto found = incident_faces_by_vertex_.find(vertex_id);
    return found == incident_faces_by_vertex_.end()
        ? std::span<const std::size_t>{}
        : std::span<const std::size_t>(
              found->second.data(), found->second.size());
}

std::span<const std::size_t> DocumentPickFacts::incident_faces(
    Edge edge) const
{
    const auto found = incident_faces_by_edge_.find(edge_key(edge));
    return found == incident_faces_by_edge_.end()
        ? std::span<const std::size_t>{}
        : std::span<const std::size_t>(
              found->second.data(), found->second.size());
}

DocumentPickFacts::FaceFact* DocumentPickFacts::face_fact(ElementId face_id)
{
    const Face* face = find_face(document_, face_id);
    if (face == nullptr || document_.faces.empty()) {
        return nullptr;
    }
    const std::ptrdiff_t face_index = face - document_.faces.data();
    return face_index >= 0 &&
            static_cast<std::size_t>(face_index) < face_facts_.size()
        ? &face_facts_[static_cast<std::size_t>(face_index)]
        : nullptr;
}

const DocumentPickFacts::FaceFact* DocumentPickFacts::face_fact(
    ElementId face_id) const
{
    const Face* face = find_face(document_, face_id);
    if (face == nullptr || document_.faces.empty()) {
        return nullptr;
    }
    const std::ptrdiff_t face_index = face - document_.faces.data();
    return face_index >= 0 &&
            static_cast<std::size_t>(face_index) < face_facts_.size()
        ? &face_facts_[static_cast<std::size_t>(face_index)]
        : nullptr;
}

void DocumentPickFacts::build_face_triangles(FaceFact& fact)
{
    if (fact.triangles_built) {
        return;
    }
    fact.triangles_built = true;
    if (!fact.pickable || fact.face == nullptr) {
        return;
    }

    const std::vector<Triangle> triangles =
        triangulate_face_local_indices(document_, *fact.face);
    fact.triangles.reserve(triangles.size());
    for (const Triangle& triangle : triangles) {
        const Vertex* a =
            find_vertex(document_, fact.face->vertices[triangle.a]);
        const Vertex* b =
            find_vertex(document_, fact.face->vertices[triangle.b]);
        const Vertex* c =
            find_vertex(document_, fact.face->vertices[triangle.c]);
        if (a == nullptr || b == nullptr || c == nullptr) {
            continue;
        }
        fact.triangles.push_back({
            a->position,
            b->position,
            c->position,
        });
        if (work_counters_ != nullptr) {
            ++work_counters_->face_triangle_facts_built;
        }
    }
}

PickResult DocumentPickFacts::normalized_surface_hit(const Ray& ray)
{
    QDR_PROFILE_SCOPE("qdr_document.nearest_prepared_face_ray_hit");
    std::optional<FaceRayHit> nearest;
    for (FaceFact& fact : face_facts_) {
        if (!fact.pickable || fact.face == nullptr) {
            continue;
        }

        const bool front_facing = face_is_front_facing_from_ray(
            fact.normal, ray, options_.reverse_face_winding);
        if (!front_facing && !options_.pick_backfaces) {
            continue;
        }

        build_face_triangles(fact);
        for (const FaceTriangleFact& triangle : fact.triangles) {
            if (work_counters_ != nullptr) {
                ++work_counters_->surface_triangle_tests;
            }
            float distance = 0.0F;
            quader::QVec3 position;
            if (ray_intersects_triangle(
                    ray,
                    triangle.a,
                    triangle.b,
                    triangle.c,
                    distance,
                    position) &&
                face_hit_is_better(
                    {fact.face->id, distance, front_facing},
                    nearest,
                    options_)) {
                nearest = FaceRayHit{
                    fact.face->id,
                    distance,
                    front_facing,
                };
            }
        }
    }
    if (!nearest.has_value()) {
        return {};
    }
    return {
        true,
        ElementKind::Face,
        kInvalidElementId,
        {},
        nearest->face_id,
        nearest->distance,
        ray.origin + ray.direction * nearest->distance,
    };
}

PickResult DocumentPickFacts::surface_hit(const Ray& local_ray)
{
    const Ray ray{
        local_ray.origin,
        normalize_or_zero(local_ray.direction),
    };
    return length_squared(ray.direction) <= kEpsilon
        ? PickResult{}
        : normalized_surface_hit(ray);
}

bool DocumentPickFacts::vertex_can_be_picked(ElementId vertex_id) const
{
    if (!options_.has_hidden_material_slot) {
        return true;
    }
    const std::span<const std::size_t> faces = incident_faces(vertex_id);
    for (const std::size_t face_index : faces) {
        if (work_counters_ != nullptr) {
            ++work_counters_->incident_face_visits;
        }
        if (face_index < face_facts_.size() &&
            face_facts_[face_index].pickable) {
            return true;
        }
    }
    return faces.empty();
}

bool DocumentPickFacts::edge_can_be_picked(Edge edge) const
{
    if (!options_.has_hidden_material_slot) {
        return true;
    }
    const std::span<const std::size_t> faces = incident_faces(edge);
    for (const std::size_t face_index : faces) {
        if (work_counters_ != nullptr) {
            ++work_counters_->incident_face_visits;
        }
        if (face_index < face_facts_.size() &&
            face_facts_[face_index].pickable) {
            return true;
        }
    }
    return faces.empty();
}

bool DocumentPickFacts::vertex_has_incident_face(ElementId vertex_id) const
{
    return !incident_faces(vertex_id).empty();
}

bool DocumentPickFacts::edge_has_incident_face(Edge edge) const
{
    return !incident_faces(edge).empty();
}

bool DocumentPickFacts::face_is_front_facing_pickable(
    ElementId face_id,
    const Ray& ray) const
{
    const FaceFact* fact = face_fact(face_id);
    return fact != nullptr && fact->pickable &&
        face_is_front_facing_from_ray(
            fact->normal, ray, options_.reverse_face_winding);
}

bool DocumentPickFacts::vertex_has_front_facing_pickable_face(
    ElementId vertex_id,
    const Ray& ray) const
{
    for (const std::size_t face_index : incident_faces(vertex_id)) {
        if (work_counters_ != nullptr) {
            ++work_counters_->incident_face_visits;
        }
        if (face_index >= face_facts_.size()) {
            continue;
        }
        const FaceFact& fact = face_facts_[face_index];
        if (fact.pickable &&
            face_is_front_facing_from_ray(
                fact.normal, ray, options_.reverse_face_winding)) {
            return true;
        }
    }
    return false;
}

bool DocumentPickFacts::edge_has_front_facing_pickable_face(
    Edge edge,
    const Ray& ray,
    bool include_edge_on) const
{
    for (const std::size_t face_index : incident_faces(edge)) {
        if (work_counters_ != nullptr) {
            ++work_counters_->incident_face_visits;
        }
        if (face_index >= face_facts_.size()) {
            continue;
        }
        const FaceFact& fact = face_facts_[face_index];
        if (fact.pickable &&
            (face_is_front_facing_from_ray(
                 fact.normal, ray, options_.reverse_face_winding) ||
             (include_edge_on &&
              length_squared(fact.normal) > kEpsilon * kEpsilon &&
              std::abs(dot(fact.normal, ray.direction)) <= kEpsilon))) {
            return true;
        }
    }
    return false;
}

ElementId DocumentPickFacts::best_edge_context_face_id(
    Edge edge,
    const Ray& ray) const
{
    ElementId best_face_id = kInvalidElementId;
    float best_score = -std::numeric_limits<float>::infinity();
    for (const std::size_t face_index : incident_faces(edge)) {
        if (work_counters_ != nullptr) {
            ++work_counters_->incident_face_visits;
        }
        if (face_index >= face_facts_.size()) {
            continue;
        }
        const FaceFact& fact = face_facts_[face_index];
        if (!fact.pickable || fact.face == nullptr) {
            continue;
        }
        const float score = options_.reverse_face_winding
            ? dot(fact.normal, ray.direction)
            : -dot(fact.normal, ray.direction);
        if (best_face_id == kInvalidElementId ||
            score > best_score + kEpsilon) {
            best_face_id = fact.face->id;
            best_score = score;
        }
    }
    return best_face_id;
}

ElementId DocumentPickFacts::edge_pick_context_face_id(
    Edge edge,
    const Ray& ray,
    const std::optional<PickResult>& surface_hit) const
{
    if (surface_hit.has_value() && surface_hit->hit) {
        const FaceFact* surface_face = face_fact(surface_hit->face_id);
        if (surface_face != nullptr &&
            surface_face->face != nullptr &&
            face_uses_edge(*surface_face->face, edge)) {
            return surface_face->face->id;
        }
    }
    return best_edge_context_face_id(edge, ray);
}

std::span<const Edge> DocumentPickFacts::edges() const noexcept
{
    return edges_;
}

PickResult DocumentPickFacts::pick_element(
    const Ray& local_ray,
    const std::optional<PickResult>* precomputed_surface_hit)
{
    return pick_element(local_ray, options_.mode, precomputed_surface_hit);
}

PickResult DocumentPickFacts::pick_element(
    const Ray& local_ray,
    SelectionMode mode,
    const std::optional<PickResult>* precomputed_surface_hit)
{
    const Ray ray{
        local_ray.origin,
        normalize_or_zero(local_ray.direction),
    };
    if (length_squared(ray.direction) <= kEpsilon) {
        return {};
    }
    if (mode == SelectionMode::Face) {
        return normalized_surface_hit(ray);
    }

    std::optional<PickResult> computed_surface_hit;
    const std::optional<PickResult>* surface_hit = precomputed_surface_hit;
    if (surface_hit == nullptr) {
        PickResult hit = normalized_surface_hit(ray);
        if (hit.hit) {
            computed_surface_hit = hit;
        }
        surface_hit = &computed_surface_hit;
    }
    std::optional<FaceRayHit> surface_face_hit;
    if (surface_hit->has_value() && surface_hit->value().hit) {
        surface_face_hit = FaceRayHit{
            surface_hit->value().face_id,
            surface_hit->value().distance,
            false,
        };
    }

    PickResult best;
    best.distance = std::numeric_limits<float>::max();
    float best_pick_distance = std::numeric_limits<float>::max();
    bool best_front_facing = false;

    if (mode == SelectionMode::Vertex) {
        for (const Vertex& vertex : document_.vertices) {
            if (work_counters_ != nullptr) {
                ++work_counters_->component_candidates_tested;
            }
            if (!vertex_can_be_picked(vertex.id)) {
                continue;
            }

            float ray_distance = 0.0F;
            const float distance =
                point_ray_distance(ray, vertex.position, ray_distance);
            if (distance > options_.vertex_radius) {
                continue;
            }
            const bool front_facing =
                vertex_has_front_facing_pickable_face(vertex.id, ray);
            if (!options_.pick_backfaces && !front_facing &&
                !incident_faces(vertex.id).empty()) {
                continue;
            }
            if (!vertex_pick_is_occluded(
                    document_,
                    vertex.id,
                    ray_distance,
                    surface_face_hit) &&
                component_pick_is_better(
                    distance,
                    ray_distance,
                    front_facing,
                    best,
                    best_pick_distance,
                    best_front_facing,
                    options_)) {
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

    for (const Edge& edge : edges_) {
        if (work_counters_ != nullptr) {
            ++work_counters_->component_candidates_tested;
        }
        if (!edge_can_be_picked(edge)) {
            continue;
        }

        const Vertex* a = find_vertex(document_, edge.a);
        const Vertex* b = find_vertex(document_, edge.b);
        if (a == nullptr || b == nullptr) {
            continue;
        }

        float ray_distance = 0.0F;
        quader::QVec3 position;
        const float distance = segment_ray_distance(
            ray,
            a->position,
            b->position,
            ray_distance,
            position);
        if (distance > options_.edge_radius) {
            continue;
        }
        const bool front_facing =
            edge_has_front_facing_pickable_face(edge, ray);
        if (!options_.pick_backfaces && !front_facing &&
            !incident_faces(edge).empty()) {
            continue;
        }
        if (!edge_pick_is_occluded(
                document_,
                edge,
                ray_distance,
                surface_face_hit) &&
            component_pick_is_better(
                distance,
                ray_distance,
                front_facing,
                best,
                best_pick_distance,
                best_front_facing,
                options_)) {
            best = {
                true,
                ElementKind::Edge,
                kInvalidElementId,
                edge,
                edge_pick_context_face_id(
                    edge, ray, *surface_hit),
                ray_distance,
                position,
            };
            best_pick_distance = distance;
            best_front_facing = front_facing;
        }
    }
    return best.hit ? best : PickResult{};
}

} // namespace document_internal

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
    document_internal::DocumentPickFacts facts(document, options);
    return facts.pick_element(local_ray);
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

OverlayData build_overlay(
    const Document& document,
    const Selection& selection,
    const OverlayStyle& style,
    OverlayBuildOptions options)
{
    QDR_PROFILE_SCOPE("qdr_document.build_overlay");
    OverlayData overlay;
    std::unordered_map<ElementId, const Vertex*> vertices_by_id;
    vertices_by_id.reserve(document.vertices.size());
    for (const Vertex& vertex : document.vertices) {
        vertices_by_id.try_emplace(vertex.id, &vertex);
    }
    const auto overlay_vertex = [&vertices_by_id](ElementId id) -> const Vertex* {
        const auto vertex = vertices_by_id.find(id);
        return vertex == vertices_by_id.end() ? nullptr : vertex->second;
    };

    std::unordered_set<ElementId> selected_vertex_ids;
    if (options.include_points) {
        selected_vertex_ids.reserve(selection.vertices.size());
        selected_vertex_ids.insert(selection.vertices.begin(), selection.vertices.end());
    }
    std::unordered_set<ElementId> selected_face_ids;
    if (options.include_lines || options.include_triangles) {
        selected_face_ids.reserve(selection.faces.size());
        selected_face_ids.insert(selection.faces.begin(), selection.faces.end());
    }
    const auto edge_key = [](Edge edge) {
        return (static_cast<std::uint64_t>(edge.a) << 32U) |
            static_cast<std::uint64_t>(edge.b);
    };
    std::unordered_set<std::uint64_t> selected_edge_keys;
    if (options.include_lines) {
        selected_edge_keys.reserve(selection.edges.size());
        for (const Edge edge : selection.edges) {
            selected_edge_keys.insert(edge_key(make_edge(edge.a, edge.b)));
        }
    }

    if (options.include_lines) {
        for (const Edge& edge : document_edges(document)) {
            const Vertex* a = overlay_vertex(edge.a);
            const Vertex* b = overlay_vertex(edge.b);
            if (a == nullptr || b == nullptr) {
                continue;
            }

            const Edge normalized_edge = make_edge(edge.a, edge.b);
            const bool selected =
                selected_edge_keys.find(edge_key(normalized_edge)) !=
                selected_edge_keys.end();
            overlay.lines.push_back({
                a->position,
                b->position,
                selected ? style.selected_edge_color : style.edge_color,
                selected ? style.selected_edge_width_pixels : style.edge_width_pixels,
                edge,
            });
        }
    }

    if (options.include_lines || options.include_triangles) {
        for (const Face& face : document.faces) {
            if (selected_face_ids.find(face.id) == selected_face_ids.end() ||
                face.vertices.size() < 2) {
                continue;
            }

            if (options.include_triangles) {
                const std::vector<Triangle> triangles =
                    triangulate_face_local_indices(document, face);
                for (const Triangle& triangle : triangles) {
                    const Vertex* a = overlay_vertex(face.vertices[triangle.a]);
                    const Vertex* b = overlay_vertex(face.vertices[triangle.b]);
                    const Vertex* c = overlay_vertex(face.vertices[triangle.c]);
                    if (a == nullptr || b == nullptr || c == nullptr) {
                        continue;
                    }

                    overlay.triangles.push_back({
                        a->position,
                        b->position,
                        c->position,
                        style.face_fill_color,
                        face.id,
                        a->id,
                        b->id,
                        c->id,
                    });
                }
            }

            if (options.include_lines) {
                for (std::size_t index = 0; index < face.vertices.size(); ++index) {
                    const Vertex* a = overlay_vertex(face.vertices[index]);
                    const Vertex* b = overlay_vertex(
                        face.vertices[(index + 1) % face.vertices.size()]);
                    if (a != nullptr && b != nullptr) {
                        overlay.lines.push_back({
                            a->position,
                            b->position,
                            style.face_border_color,
                            style.face_border_width_pixels,
                            make_edge(
                                face.vertices[index],
                                face.vertices[(index + 1) % face.vertices.size()]),
                            face.id,
                        });
                    }
                }
            }
        }
    }

    if (options.include_points && selection.mode == SelectionMode::Vertex) {
      for (const Vertex &vertex : document.vertices) {
        const bool selected = selected_vertex_ids.find(vertex.id) !=
            selected_vertex_ids.end();
        overlay.points.push_back({
            vertex.position,
            selected ? style.selected_vertex_color : style.vertex_color,
            selected ? style.selected_vertex_size_pixels
                     : style.vertex_size_pixels,
            vertex.id,
        });
      }
    }

    return overlay;
}

} // namespace quader_poly

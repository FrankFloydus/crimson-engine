////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "session_internal.hpp"

#include <diagnostics/profile.hpp>
#include <editor/modeling/runtime/session.hpp>

#include <quader/modeling/neutral/geometry/geometry.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_picking_helpers.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader_editor_core {
namespace {

constexpr double kDefaultVertexPickRadius = 0.12;
constexpr double kDefaultEdgePickRadius = 0.10;
constexpr double kMinPickRadius = 0.001;
constexpr double kMaxPickRadius = 2.0;

quader::QVec3 affine_transform_point(const quader::QMat4 &matrix,
                                     quader::QVec3 point) {
  const auto &m = matrix.values;
  const float x = m[0] * point.x + m[4] * point.y + m[8] * point.z + m[12];
  const float y = m[1] * point.x + m[5] * point.y + m[9] * point.z + m[13];
  const float z = m[2] * point.x + m[6] * point.y + m[10] * point.z + m[14];
  const float w = m[3] * point.x + m[7] * point.y + m[11] * point.z + m[15];
  if (std::abs(w) <= kTransformPointEpsilon) {
    return {x, y, z};
  }
  return {x / w, y / w, z / w};
}

quader::QVec3 affine_transform_vector(const quader::QMat4 &matrix,
                                      quader::QVec3 vector) {
  const auto &m = matrix.values;
  return {
      m[0] * vector.x + m[4] * vector.y + m[8] * vector.z,
      m[1] * vector.x + m[5] * vector.y + m[9] * vector.z,
      m[2] * vector.x + m[6] * vector.y + m[10] * vector.z,
  };
}

quader::QVec3 affine_transform_covector(const quader::QMat4 &matrix,
                                        quader::QVec3 covector) {
  const auto &m = matrix.values;
  return {
      m[0] * covector.x + m[1] * covector.y + m[2] * covector.z,
      m[4] * covector.x + m[5] * covector.y + m[6] * covector.z,
      m[8] * covector.x + m[9] * covector.y + m[10] * covector.z,
  };
}

const PickObjectTransform *cached_transform_for_object(
    std::span<const PickObjectTransform> object_transforms, int object_index,
    const PolygonObject &object) {
  if (object_index >= 0 &&
      object_index < static_cast<int>(object_transforms.size()) &&
      object_transforms[static_cast<std::size_t>(object_index)].object_id ==
          object.id) {
    return &object_transforms[static_cast<std::size_t>(object_index)];
  }
  for (const PickObjectTransform &transform : object_transforms) {
    if (transform.object_id == object.id) {
      return &transform;
    }
  }
  return nullptr;
}

quader_poly::Ray object_space_pick_ray(
    const quader_poly::Ray &world_ray,
    const PickObjectTransform *object_transform) {
  if (object_transform == nullptr) {
    return world_ray;
  }
  const quader::QVec3 local_direction = quader::normalize_or_zero(
      affine_transform_vector(object_transform->object_from_world,
                              world_ray.direction));
  return {
      affine_transform_point(object_transform->object_from_world,
                             world_ray.origin),
      local_direction,
  };
}

quader::QVec3 pick_world_position(
    quader::QVec3 local_position,
    const PickObjectTransform *object_transform) {
  return object_transform == nullptr
             ? local_position
             : affine_transform_point(object_transform->world_from_object,
                                      local_position);
}

float world_ray_distance(const quader_poly::Ray &world_ray,
                         quader::QVec3 world_position) {
  const quader::QVec3 direction =
      quader::normalize_or_zero(world_ray.direction);
  return quader::dot(world_position - world_ray.origin, direction);
}

float world_pick_distance(const quader_poly::Ray &world_ray,
                          quader::QVec3 world_position) {
  const quader::QVec3 direction =
      quader::normalize_or_zero(world_ray.direction);
  const quader::QVec3 offset = world_position - world_ray.origin;
  const float distance = quader::dot(offset, direction);
  const quader::QVec3 closest = world_ray.origin + direction * distance;
  return quader::length(world_position - closest);
}

quader::QVec3 pick_world_normal(
    quader::QVec3 local_normal,
    const PickObjectTransform *object_transform) {
  if (object_transform == nullptr) {
    return local_normal;
  }
  return quader::normalize_or_zero(
      affine_transform_covector(object_transform->object_from_world,
                                local_normal));
}

void apply_world_pick_metrics(quader_poly::PickResult &hit,
                              const quader_poly::Ray &world_ray,
                              const PickObjectTransform *object_transform) {
  if (!hit.hit) {
    return;
  }
  hit.distance = world_ray_distance(
      world_ray, pick_world_position(hit.position, object_transform));
}

float object_space_pick_radius(
    float world_radius, const PickObjectTransform *object_transform) {
  if (object_transform == nullptr) {
    return world_radius;
  }
  const float x = quader::length(affine_transform_vector(
      object_transform->object_from_world, {world_radius, 0.0F, 0.0F}));
  const float y = quader::length(affine_transform_vector(
      object_transform->object_from_world, {0.0F, world_radius, 0.0F}));
  const float z = quader::length(affine_transform_vector(
      object_transform->object_from_world, {0.0F, 0.0F, world_radius}));
  return std::max({x, y, z});
}

/**
 * Stores the active source-wire component pick used by component hover and selection.
 */
struct SourceWireComponentPick {
	int object_index = -1;
	quader_poly::PickResult hit;
	float pick_distance = std::numeric_limits<float>::max();
	bool selected_target = false;
};

std::uint64_t component_edge_key(quader_poly::Edge edge)
{
  edge = quader_poly::make_edge(edge.a, edge.b);
  return (static_cast<std::uint64_t>(edge.a) << 32U) |
         static_cast<std::uint64_t>(edge.b);
}

/// Indexes selected source components for constant-time wire classification.
class SourceWireSelectionIndex final {
public:
  explicit SourceWireSelectionIndex(const PolygonObject &object)
      : selected_(object.selected), mode_(object.selection.mode) {
    if (!selected_) {
      return;
    }
    if (mode_ == quader_poly::SelectionMode::Vertex) {
      vertices_.reserve(object.selection.vertices.size());
      vertices_.insert(object.selection.vertices.begin(),
                       object.selection.vertices.end());
      return;
    }
    if (mode_ == quader_poly::SelectionMode::Edge) {
      edges_.reserve(object.selection.edges.size());
      for (const quader_poly::Edge edge : object.selection.edges) {
        edges_.insert(component_edge_key(edge));
      }
    }
  }

  [[nodiscard]] bool contains(const quader_poly::PickResult &hit) const {
    if (!selected_) {
      return false;
    }
    if (hit.kind == quader_poly::ElementKind::Vertex &&
        mode_ == quader_poly::SelectionMode::Vertex) {
      return vertices_.contains(hit.vertex_id);
    }
    return hit.kind == quader_poly::ElementKind::Edge &&
           mode_ == quader_poly::SelectionMode::Edge &&
           edges_.contains(component_edge_key(hit.edge));
  }

private:
  bool selected_ = false;
  quader_poly::SelectionMode mode_ = quader_poly::SelectionMode::Vertex;
  std::unordered_set<quader_poly::ElementId> vertices_;
  std::unordered_set<std::uint64_t> edges_;
};

void keep_component_edit_target(PolygonObject& object, int& active_object_id)
{
	object.selected = true;
	object.component_edit_target = true;
	if (object.id > 0) {
		active_object_id = object.id;
	}
}

quader_geometry::QVec2f to_geometry_vec2(quader::QVec2 value)
{
	return quader_geometry::QVec2f(value);
}

quader_geometry::QVec3f to_geometry_vec3(quader::QVec3 value)
{
	return quader_geometry::QVec3f(value);
}

quader_geometry::QRay3<float> to_geometry_ray(const quader_poly::Ray& ray)
{
	return { to_geometry_vec3(ray.origin), to_geometry_vec3(ray.direction) };
}

float pick_bounds_padding(const quader_poly::PickOptions& options)
{
	switch (options.mode) {
        case quader_poly::SelectionMode::Vertex:
          return std::max(0.0F, options.vertex_radius);
        case quader_poly::SelectionMode::Edge:
          return std::max(0.0F, options.edge_radius);
        case quader_poly::SelectionMode::Face:
        default:
          return 0.0F;
	}
}

quader_geometry::QAabb3<float> object_pick_bounds(
		const PolygonObject& object,
		const quader_poly::PickOptions& options)
{
	quader_geometry::QAabb3<float> bounds = quader_geometry::empty_aabb3<float>();
	for (const quader_poly::Vertex& vertex : object.document.vertices) {
		quader_geometry::aabb_include(bounds, to_geometry_vec3(vertex.position));
	}
	const float padding = pick_bounds_padding(options);
	return padding > 0.0F ? quader_geometry::aabb_expanded(bounds, padding) : bounds;
}

quader_geometry::QAabb3<float> object_pick_bounds(
		const PickObjectBounds& cached_bounds,
		const quader_poly::PickOptions& options)
{
	if (cached_bounds.empty) {
		return quader_geometry::empty_aabb3<float>();
	}
	quader_geometry::QAabb3<float> bounds {
		to_geometry_vec3(cached_bounds.min),
		to_geometry_vec3(cached_bounds.max),
	};
	const float padding = pick_bounds_padding(options);
	return padding > 0.0F ? quader_geometry::aabb_expanded(bounds, padding) : bounds;
}

const PickObjectBounds* cached_bounds_for_object(
		std::span<const PickObjectBounds> object_bounds,
		int object_index,
		const PolygonObject& object)
{
	if (object_index >= 0 && object_index < static_cast<int>(object_bounds.size()) &&
			object_bounds[static_cast<std::size_t>(object_index)].object_id == object.id) {
		return &object_bounds[static_cast<std::size_t>(object_index)];
	}
	for (const PickObjectBounds& bounds : object_bounds) {
		if (bounds.object_id == object.id) {
			return &bounds;
		}
	}
	return nullptr;
}

bool ray_intersects_object_pick_bounds(
		const PolygonObject& object,
		const quader_poly::Ray& ray,
		const quader_poly::PickOptions& options,
		const PickObjectBounds* cached_bounds);
const PickObjectFaceGeometry* cached_face_geometry_for_object(
		std::span<const PickObjectFaceGeometry> face_geometry,
		int object_index,
		const PolygonObject& object);

bool object_draws_active_source_wire(const PolygonObject& object, int candidate_object_id)
{
	return object.selected || (object.id > 0 && object.id == candidate_object_id);
}

bool source_wire_face_contains_edge(const quader_poly::Face& face, quader_poly::Edge edge)
{
	const quader_poly::Edge normalized = quader_poly::make_edge(edge.a, edge.b);
	if (face.vertices.size() < 2) {
		return false;
	}
	for (std::size_t index = 0; index < face.vertices.size(); ++index) {
		const quader_poly::ElementId a = face.vertices[index];
		const quader_poly::ElementId b = face.vertices[(index + 1) % face.vertices.size()];
		if (quader_poly::make_edge(a, b) == normalized) {
			return true;
		}
	}
	return false;
}

bool source_wire_face_contains_vertex(const quader_poly::Face& face, quader_poly::ElementId vertex_id)
{
	return std::ranges::find(face.vertices, vertex_id) != face.vertices.end();
}

bool source_wire_component_is_on_face(const quader_poly::Face& face, const quader_poly::PickResult& hit)
{
  if (hit.kind == quader_poly::ElementKind::Vertex) {
    return source_wire_face_contains_vertex(face, hit.vertex_id);
  }
  if (hit.kind == quader_poly::ElementKind::Edge) {
    return source_wire_face_contains_edge(face, hit.edge);
  }
        return true;
}

bool source_wire_component_is_occluded_by_object_faces(
		const PolygonObject& object,
		const std::optional<quader_poly::PickResult>& surface_hit,
		const quader_poly::PickResult& hit)
{
	if (!surface_hit.has_value() ||
			hit.distance <= surface_hit->distance + 0.001F) {
		return false;
	}
	const quader_poly::Face* surface_face =
		quader_poly::find_face(object.document, surface_hit->face_id);
	return surface_face == nullptr || !source_wire_component_is_on_face(*surface_face, hit);
}

bool source_wire_candidate_is_better(
		const SourceWireComponentPick& candidate,
		const std::optional<SourceWireComponentPick>& best)
{
	if (!best.has_value()) {
		return true;
	}
        if (std::abs(candidate.pick_distance - best->pick_distance) >
            kTransformPointEpsilon) {
          return candidate.pick_distance < best->pick_distance;
        }
        if (std::abs(candidate.hit.distance - best->hit.distance) >
            kTransformPointEpsilon) {
          return candidate.hit.distance < best->hit.distance;
        }
        return candidate.object_index < best->object_index;
}

void keep_source_wire_candidate(
		std::optional<SourceWireComponentPick>& best_selected,
		std::optional<SourceWireComponentPick>& best_unselected,
		SourceWireComponentPick candidate)
{
	std::optional<SourceWireComponentPick>& bucket = candidate.selected_target ? best_selected : best_unselected;
	if (source_wire_candidate_is_better(candidate, bucket)) {
		bucket = std::move(candidate);
	}
}

std::optional<SourceWireComponentPick> resolve_source_wire_component_pick(
		const std::optional<SourceWireComponentPick>& best_selected,
		const std::optional<SourceWireComponentPick>& best_unselected,
		float radius,
		bool prefer_selected_target)
{
	if (!best_selected.has_value()) {
		return best_unselected;
	}
	if (!best_unselected.has_value()) {
		return best_selected;
	}

        const float selected_tolerance =
            prefer_selected_target
                ? radius
                : std::max(kTransformPointEpsilon, radius * 0.25F);
        if (best_selected->pick_distance <= best_unselected->pick_distance + selected_tolerance) {
		return best_selected;
	}
	return best_unselected;
}

std::optional<SourceWireComponentPick> pick_active_source_wire_component(
		const std::vector<PolygonObject>& objects,
		SelectionMode mode,
		const quader_poly::Ray& ray,
		quader_poly::PickOptions options,
		int candidate_object_id,
		bool prefer_selected_target,
		std::span<const PickObjectBounds> object_bounds = {},
		std::span<const PickObjectFaceGeometry> face_geometry = {},
		std::span<const PickObjectTransform> object_transforms = {})
{
  if (mode == SelectionMode::Object || mode == SelectionMode::Face) {
    return std::nullopt;
  }

        options.mode = component_selection_mode(mode);
	const float radius = pick_bounds_padding(options);
	if (radius <= 0.0F) {
		return std::nullopt;
	}

	const quader::QVec3 direction = quader::normalize_or_zero(ray.direction);
        if (quader::length_squared(direction) <=
            kTransformPointEpsilon * kTransformPointEpsilon) {
          return std::nullopt;
        }
        const quader_poly::Ray world_pick_ray { ray.origin, direction };
	std::optional<SourceWireComponentPick> best_selected;
	std::optional<SourceWireComponentPick> best_unselected;

	for (int object_index = 0; object_index < static_cast<int>(objects.size()); ++object_index) {
		const PolygonObject& object = objects[object_index];
		if (!object_draws_active_source_wire(object, candidate_object_id)) {
			continue;
		}
		if (!ray_intersects_object_pick_bounds(
					object,
					world_pick_ray,
					options,
					cached_bounds_for_object(object_bounds, object_index, object))) {
			continue;
		}

                const PickObjectTransform *object_transform =
                    cached_transform_for_object(object_transforms,
                                                object_index, object);
                const quader_poly::Ray pick_ray =
                    object_space_pick_ray(world_pick_ray, object_transform);
		const quader_geometry::QRay3<float> geometry_pick_ray =
                    to_geometry_ray(pick_ray);
                const float local_radius =
                    object_space_pick_radius(radius, object_transform);
                quader_poly::document_internal::DocumentPickFacts pick_facts(
                    object.document, options);
                const SourceWireSelectionIndex selection_index(object);
                bool surface_hit_computed = false;
                std::optional<quader_poly::PickResult> surface_hit;
                const auto ensure_surface_hit =
                    [&]() -> const std::optional<quader_poly::PickResult> & {
                  if (surface_hit_computed) {
                    return surface_hit;
                  }
                  surface_hit_computed = true;
                  quader_poly::PickResult hit;
                  if (const PickObjectFaceGeometry *cached_geometry =
                          cached_face_geometry_for_object(
                              face_geometry, object_index, object)) {
                    hit = quader_poly::pick_face_geometry(
                        cached_geometry->triangle_span(), pick_ray, options);
                  } else {
                    hit = pick_facts.surface_hit(pick_ray);
                  }
                  if (hit.hit) {
                    surface_hit = hit;
                  }
                  return surface_hit;
                };

                if (options.mode == quader_poly::SelectionMode::Vertex) {
                  for (const quader_poly::Vertex &vertex :
                       object.document.vertices) {
                    if (!pick_facts.vertex_can_be_picked(vertex.id)) {
                      continue;
                    }

                    const quader::QVec3 offset =
                        vertex.position - pick_ray.origin;
                    const float projected_ray_distance =
                        quader::dot(offset, pick_ray.direction);
                    if (projected_ray_distance < 0.0F) {
                      continue;
                    }

                    const quader_geometry::QClosestRayPoint<float> closest =
                        quader_geometry::closest_point_on_ray<float>(
                            to_geometry_vec3(vertex.position),
                            geometry_pick_ray, kTransformPointEpsilon);
                    if (!closest.valid) {
                      continue;
                    }
                    const float local_pick_distance =
                        std::sqrt(closest.distance_squared);
                    if (local_pick_distance > local_radius) {
                      continue;
                    }

                    quader_poly::PickResult hit;
                    hit.hit = true;
                    hit.kind = quader_poly::ElementKind::Vertex;
                    hit.vertex_id = vertex.id;
                    hit.distance = closest.ray_distance;
                    hit.position = vertex.position;
                    const bool selected_target =
                        selection_index.contains(hit);
                    if (!selected_target &&
                        source_wire_component_is_occluded_by_object_faces(
                            object, ensure_surface_hit(), hit)) {
                      continue;
                    }
                    const quader::QVec3 world_position =
                        pick_world_position(hit.position, object_transform);
                    hit.distance =
                        world_ray_distance(world_pick_ray, world_position);
                    const float pick_distance =
                        world_pick_distance(world_pick_ray, world_position);
                    keep_source_wire_candidate(
                        best_selected, best_unselected,
                        {
                            object_index,
                            hit,
                            pick_distance,
                            selected_target,
                        });
                  }
                  continue;
                }

                if (options.mode != quader_poly::SelectionMode::Edge) {
                  continue;
                }
                for (const quader_poly::Edge edge : pick_facts.edges()) {
			if (!pick_facts.edge_can_be_picked(edge)) {
				continue;
			}
			const quader_poly::Vertex* a = quader_poly::find_vertex(object.document, edge.a);
			const quader_poly::Vertex* b = quader_poly::find_vertex(object.document, edge.b);
			if (a == nullptr || b == nullptr) {
				continue;
			}

                        const quader_geometry::QSegmentRayClosestPoints<float>
                            closest =
                                quader_geometry::closest_points_segment_ray<
                                    float>({to_geometry_vec3(a->position),
                                            to_geometry_vec3(b->position)},
                                           geometry_pick_ray,
                                           kTransformPointEpsilon);
                        if (!closest.valid || closest.ray_distance < 0.0F) {
				continue;
			}
			const float local_pick_distance =
                            std::sqrt(closest.distance_squared);
			if (local_pick_distance > local_radius) {
				continue;
			}

			quader_poly::PickResult hit;
			hit.hit = true;
                        hit.kind = quader_poly::ElementKind::Edge;
                        hit.edge = quader_poly::make_edge(edge.a, edge.b);
			hit.distance = closest.ray_distance;
			hit.position = {
				closest.segment_point.x,
				closest.segment_point.y,
				closest.segment_point.z,
			};
                        hit.face_id =
                            pick_facts.best_edge_context_face_id(
                                hit.edge, pick_ray);
                        if (hit.face_id == quader_poly::kInvalidElementId) {
                          hit.face_id =
                              pick_facts.edge_pick_context_face_id(
                                  hit.edge, pick_ray,
                                  ensure_surface_hit());
                        }
                        const bool selected_target =
                            selection_index.contains(hit);
			if (!selected_target &&
                            source_wire_component_is_occluded_by_object_faces(
                                object, ensure_surface_hit(), hit)) {
				continue;
			}
			const quader::QVec3 world_position =
                            pick_world_position(hit.position, object_transform);
			hit.distance =
                            world_ray_distance(world_pick_ray, world_position);
			const float pick_distance =
                            world_pick_distance(world_pick_ray, world_position);
			keep_source_wire_candidate(
				best_selected,
				best_unselected,
				{
					object_index,
					hit,
					pick_distance,
					selected_target,
				});
		}
	}

	return resolve_source_wire_component_pick(best_selected, best_unselected, radius, prefer_selected_target);
}

const PickObjectFaceGeometry* cached_face_geometry_for_object(
		std::span<const PickObjectFaceGeometry> face_geometry,
		int object_index,
		const PolygonObject& object)
{
	if (object_index >= 0 && object_index < static_cast<int>(face_geometry.size()) &&
			face_geometry[static_cast<std::size_t>(object_index)].object_id == object.id) {
		return &face_geometry[static_cast<std::size_t>(object_index)];
	}
	for (const PickObjectFaceGeometry& geometry : face_geometry) {
		if (geometry.object_id == object.id) {
			return &geometry;
		}
	}
	return nullptr;
}

bool ray_intersects_object_pick_bounds(
		const PolygonObject& object,
		const quader_poly::Ray& ray,
		const quader_poly::PickOptions& options,
		const PickObjectBounds* cached_bounds)
{
	const quader_geometry::QAabb3<float> bounds =
		cached_bounds == nullptr ? object_pick_bounds(object, options) : object_pick_bounds(*cached_bounds, options);
	return quader_geometry::intersect_ray_aabb(to_geometry_ray(ray), bounds).hit;
}

quader_poly::PickResult pick_object_element(
		const PolygonObject& object,
		int object_index,
		const quader_poly::Ray& world_ray,
		const quader_poly::PickOptions& options,
		std::span<const PickObjectFaceGeometry> face_geometry,
		std::span<const PickObjectTransform> object_transforms,
		const std::optional<quader_poly::PickResult>*
                    precomputed_surface_hit = nullptr,
                quader_poly::PickResult* local_hit_out = nullptr,
                quader_poly::document_internal::PickWorkCounters*
                    work_counters = nullptr)
{
  const PickObjectTransform *object_transform = cached_transform_for_object(
      object_transforms, object_index, object);
  const quader_poly::Ray ray =
      object_space_pick_ray(world_ray, object_transform);
  quader_poly::PickOptions local_options = options;
  local_options.vertex_radius =
      object_space_pick_radius(options.vertex_radius, object_transform);
  local_options.edge_radius =
      object_space_pick_radius(options.edge_radius, object_transform);
  quader_poly::PickResult hit;
  if (options.mode == quader_poly::SelectionMode::Face) {
    if (const PickObjectFaceGeometry *cached_geometry =
            cached_face_geometry_for_object(face_geometry, object_index,
                                            object)) {
      hit = quader_poly::pick_face_geometry(cached_geometry->triangle_span(),
                                            ray, local_options);
      if (local_hit_out != nullptr) {
        *local_hit_out = hit;
      }
      apply_world_pick_metrics(hit, world_ray, object_transform);
      return hit;
    }
  }
  quader_poly::document_internal::DocumentPickFacts pick_facts(
      object.document, local_options, work_counters);
  hit = pick_facts.pick_element(
      ray, local_options.mode, precomputed_surface_hit);
  if (local_hit_out != nullptr) {
    *local_hit_out = hit;
  }
  apply_world_pick_metrics(hit, world_ray, object_transform);
  return hit;
}

std::optional<PickHitRecord> nearest_surface_hit(
		const std::vector<PolygonObject>& objects,
		const quader_poly::Ray& ray,
		const quader_poly::PickOptions& options,
		std::span<const PickObjectBounds> object_bounds,
		std::span<const PickObjectFaceGeometry> face_geometry = {},
		int target_object_id = -1,
		std::span<const PickObjectTransform> object_transforms = {},
                std::vector<std::optional<quader_poly::PickResult>>*
                    local_surface_hits = nullptr);
bool component_pick_is_occluded_by_surface(
		const std::vector<PolygonObject>& objects,
		int object_index,
		const quader_poly::PickResult& component_hit,
		const std::optional<PickHitRecord>& surface_hit);

double normalize_pick_radius(double value, double fallback)
{
  return std::clamp(std::isfinite(value) ? value : fallback, kMinPickRadius,
                    kMaxPickRadius);
}

void apply_editor_surface_pick_policy(quader_poly::PickOptions& options)
{
	options.pick_backfaces = true;
	options.prefer_front_facing_faces = false;
}

bool pick_nearest_object(
		const std::vector<PolygonObject>& objects,
		const quader_poly::Ray& ray,
		quader_poly::PickOptions options,
		int& hit_index,
		quader_poly::PickResult& hit,
		std::span<const PickObjectBounds> object_bounds = {},
		std::span<const PickObjectFaceGeometry> face_geometry = {},
		int target_object_id = -1,
		std::span<const PickObjectTransform> object_transforms = {})
{
	QDR_PROFILE_SCOPE("qdr_editor_core.pick_nearest_object");
	hit_index = -1;
	hit = quader_poly::PickResult();
	hit.distance = std::numeric_limits<float>::max();
        std::vector<std::optional<quader_poly::PickResult>>
            local_surface_hits;
        const bool component_mode =
            options.mode == quader_poly::SelectionMode::Vertex ||
            options.mode == quader_poly::SelectionMode::Edge;
        if (component_mode) {
          local_surface_hits.resize(objects.size());
        }
        const std::optional<PickHitRecord> surface_hit =
            component_mode
                ? nearest_surface_hit(objects, ray, options, object_bounds,
                                      face_geometry, target_object_id,
                                      object_transforms,
                                      &local_surface_hits)
                : std::optional<PickHitRecord>{};
        for (int index = 0; index < static_cast<int>(objects.size()); ++index) {
		if (target_object_id >= 0 &&
			objects[index].id != target_object_id) {
			continue;
		}
		if (!ray_intersects_object_pick_bounds(
					objects[index],
					ray,
					options,
					cached_bounds_for_object(object_bounds, index, objects[index]))) {
			continue;
		}
		const quader_poly::PickResult candidate = pick_object_element(
                    objects[index], index, ray, options, face_geometry,
                    object_transforms,
                    component_mode
                        ? &local_surface_hits[
                              static_cast<std::size_t>(index)]
                        : nullptr);
		if (candidate.hit &&
				!component_pick_is_occluded_by_surface(objects, index, candidate, surface_hit) &&
				(hit_index < 0 || candidate.distance < hit.distance)) {
			hit_index = index;
			hit = candidate;
		}
	}
	return hit_index >= 0;
}

bool face_contains_edge(const quader_poly::Face& face, quader_poly::Edge edge)
{
	const quader_poly::Edge normalized = quader_poly::make_edge(edge.a, edge.b);
	if (face.vertices.size() < 2) {
		return false;
	}
	for (std::size_t index = 0; index < face.vertices.size(); ++index) {
		const quader_poly::ElementId a = face.vertices[index];
		const quader_poly::ElementId b = face.vertices[(index + 1) % face.vertices.size()];
		if (quader_poly::make_edge(a, b) == normalized) {
			return true;
		}
	}
	return false;
}

quader_poly::ElementId best_edge_context_face_id(
		const quader_poly::Document& document,
		quader_poly::Edge edge,
		const quader_poly::Ray& ray,
		const quader_poly::PickOptions& options)
{
	quader_poly::ElementId best_face_id = quader_poly::kInvalidElementId;
	float best_score = -std::numeric_limits<float>::infinity();
	for (const quader_poly::Face& face : document.faces) {
		if (!face_contains_edge(face, edge) ||
				options.hides_material_slot(face.material_slot)) {
			continue;
		}
		const float score = -quader::dot(quader_poly::face_normal(document, face), ray.direction);
		if (best_face_id == quader_poly::kInvalidElementId ||
				score > best_score + kTransformPointEpsilon) {
			best_face_id = face.id;
			best_score = score;
		}
	}
	return best_face_id;
}

quader_poly::ElementId edge_loop_context_face_id(
		const quader_poly::Document& document,
		const quader_poly::PickResult& hit,
		const quader_poly::Ray& ray,
		quader_poly::PickOptions options)
{
	if (hit.kind != quader_poly::ElementKind::Edge) {
		return quader_poly::kInvalidElementId;
	}
	const quader_poly::ElementId best_context_face =
		best_edge_context_face_id(document, hit.edge, ray, options);
	if (best_context_face != quader_poly::kInvalidElementId) {
		return best_context_face;
	}
	if (const quader_poly::Face* picked_face =
			quader_poly::find_face(document, hit.face_id);
			picked_face != nullptr && face_contains_edge(*picked_face, hit.edge)) {
		return picked_face->id;
	}

	options.mode = quader_poly::SelectionMode::Face;
	const quader_poly::PickResult surface_hit =
		quader_poly::pick_element(document, ray, options);
	if (!surface_hit.hit) {
		return best_edge_context_face_id(document, hit.edge, ray, options);
	}
	const quader_poly::Face* surface_face =
		quader_poly::find_face(document, surface_hit.face_id);
	return surface_face != nullptr && face_contains_edge(*surface_face, hit.edge)
		? surface_face->id
		: quader_poly::kInvalidElementId;
}

bool face_contains_vertex(const quader_poly::Face& face, quader_poly::ElementId vertex_id)
{
	return std::ranges::find(face.vertices, vertex_id) != face.vertices.end();
}

quader::QVec3 averaged_vertex_normal(const quader_poly::Document& document, quader_poly::ElementId vertex_id)
{
	quader::QVec3 normal;
	for (const quader_poly::Face& face : document.faces) {
		if (std::find(face.vertices.begin(), face.vertices.end(), vertex_id) != face.vertices.end()) {
			normal += quader_poly::face_normal(document, face);
		}
	}
	return quader::normalize_or_zero(normal);
}

quader::QVec3 averaged_edge_normal(const quader_poly::Document& document, quader_poly::Edge edge)
{
	quader::QVec3 normal;
	for (const quader_poly::Face& face : document.faces) {
		if (face_contains_edge(face, edge)) {
			normal += quader_poly::face_normal(document, face);
		}
	}
	return quader::normalize_or_zero(normal);
}

quader::QVec3 pick_hit_normal(const quader_poly::Document& document, const quader_poly::PickResult& hit)
{
	switch (hit.kind) {
        case quader_poly::ElementKind::Vertex:
          return averaged_vertex_normal(document, hit.vertex_id);
        case quader_poly::ElementKind::Edge:
          return averaged_edge_normal(document, hit.edge);
        case quader_poly::ElementKind::Face:
        default:
          if (const quader_poly::Face *face =
                  quader_poly::find_face(document, hit.face_id)) {
            return quader_poly::face_normal(document, *face);
          }
          return {};
	}
}

std::string pick_hit_signature(int object_id, const quader_poly::PickResult& hit)
{
	std::string signature = "object:";
	signature += std::to_string(object_id);
	switch (hit.kind) {
        case quader_poly::ElementKind::Vertex:
          signature += "/vertex:";
          signature += std::to_string(hit.vertex_id);
          break;
        case quader_poly::ElementKind::Edge:
          signature += "/edge:";
          signature += std::to_string(hit.edge.a);
          signature += "-";
          signature += std::to_string(hit.edge.b);
          break;
        case quader_poly::ElementKind::Face:
        default:
          signature += "/face:";
          signature += std::to_string(hit.face_id);
          break;
	}
	return signature;
}

PickHitRecord make_pick_hit_record(
    const PolygonObject &object, int object_index, const PickRequest &request,
    const quader_poly::PickResult &hit,
    const PickObjectTransform *object_transform = nullptr)
{
  const quader::QVec3 world_position =
      pick_world_position(hit.position, object_transform);
  const quader::QVec3 world_normal = pick_world_normal(
      pick_hit_normal(object.document, hit), object_transform);
  return {
      true,
      object.id,
      object_index,
      object.material_id,
      hit.kind,
      hit.vertex_id,
      hit.edge,
      hit.face_id,
      world_position,
      world_normal,
      world_ray_distance(request.ray, world_position),
      hit.kind == quader_poly::ElementKind::Face
          ? 0.0F
          : world_pick_distance(request.ray, world_position),
      pick_hit_signature(object.id, hit),
  };
}

std::optional<PickHitRecord> nearest_surface_hit(
		const std::vector<PolygonObject>& objects,
		const quader_poly::Ray& ray,
		const quader_poly::PickOptions& options,
		std::span<const PickObjectBounds> object_bounds,
		std::span<const PickObjectFaceGeometry> face_geometry,
		int target_object_id,
		std::span<const PickObjectTransform> object_transforms,
                std::vector<std::optional<quader_poly::PickResult>>*
                    local_surface_hits)
{
	quader_poly::PickOptions surface_options = options;
        surface_options.mode = quader_poly::SelectionMode::Face;
	const PickRequest surface_request {ray, surface_options, target_object_id};
	std::optional<PickHitRecord> nearest;
	for (int index = 0; index < static_cast<int>(objects.size()); ++index) {
		if (target_object_id >= 0 &&
			objects[index].id != target_object_id) {
			continue;
		}
		if (!ray_intersects_object_pick_bounds(
					objects[index],
					ray,
					surface_options,
					cached_bounds_for_object(object_bounds, index, objects[index]))) {
			continue;
		}
                quader_poly::PickResult local_surface_hit;
		const quader_poly::PickResult surface_hit = pick_object_element(
                    objects[index], index, ray, surface_options, face_geometry,
                    object_transforms, nullptr, &local_surface_hit);
                if (local_surface_hits != nullptr &&
                    static_cast<std::size_t>(index) <
                        local_surface_hits->size() &&
                    local_surface_hit.hit) {
                  (*local_surface_hits)[static_cast<std::size_t>(index)] =
                      local_surface_hit;
                }
		if (!surface_hit.hit) {
			continue;
		}

		PickHitRecord record = make_pick_hit_record(
                    objects[index], index, surface_request, surface_hit,
                    cached_transform_for_object(object_transforms, index,
                                                objects[index]));
                if (!nearest.has_value() ||
                    record.ray_distance < nearest->ray_distance ||
                    (std::abs(record.ray_distance - nearest->ray_distance) <=
                         kTransformPointEpsilon &&
                     record.object_index < nearest->object_index)) {
                  nearest = std::move(record);
                }
        }
	return nearest;
}

bool component_pick_is_occluded_by_surface(
		const std::vector<PolygonObject>& objects,
		int object_index,
		const quader_poly::PickResult& component_hit,
		const std::optional<PickHitRecord>& surface_hit)
{
  if (!surface_hit.has_value() ||
      component_hit.kind == quader_poly::ElementKind::Face) {
    return false;
  }
        if (component_hit.distance <= surface_hit->ray_distance + 0.001F) {
		return false;
	}

	if (object_index == surface_hit->object_index && object_index >= 0 && object_index < static_cast<int>(objects.size())) {
		const quader_poly::Face* surface_face = quader_poly::find_face(objects[object_index].document, surface_hit->face_id);
		if (surface_face != nullptr) {
                  if (component_hit.kind == quader_poly::ElementKind::Vertex &&
                      face_contains_vertex(*surface_face,
                                           component_hit.vertex_id)) {
                    return false;
                  }
                  if (component_hit.kind == quader_poly::ElementKind::Edge &&
                      face_contains_edge(*surface_face, component_hit.edge)) {
                    return false;
                  }
                }
	}

	return true;
}

std::optional<int> fallback_pick_hit_index(const std::vector<PickHitRecord>& hits)
{
	if (hits.empty()) {
		return std::nullopt;
	}
	return hits[0].hit ? std::optional<int>(0) : std::nullopt;
}

std::optional<int> replacement_pick_hit_index(
		PickCycleState& state,
		const std::vector<PickHitRecord>& hits,
		bool keep_all_component_hits,
		bool can_pick_all,
		quader::QVec2 screen_position,
		float max_screen_distance,
		PickCycleStep& r_step)
{
	if (!can_pick_all) {
		reset_pick_cycle(state);
		return fallback_pick_hit_index(hits);
	}

	const std::vector<int> candidate_indices = pick_cycle_candidate_indices(hits, keep_all_component_hits);
	if (candidate_indices.empty()) {
		reset_pick_cycle(state);
		return std::nullopt;
	}

	std::vector<PickHitRecord> candidate_hits;
	candidate_hits.reserve(candidate_indices.size());
	for (const int candidate_index : candidate_indices) {
		if (candidate_index >= 0 && candidate_index < static_cast<int>(hits.size())) {
			candidate_hits.push_back(hits[candidate_index]);
		}
	}
	if (candidate_hits.empty()) {
		reset_pick_cycle(state);
		return std::nullopt;
	}

	r_step = advance_pick_cycle(
			state,
			pick_hits_signature(candidate_hits),
			static_cast<int>(candidate_hits.size()),
			screen_position,
			max_screen_distance);
	if (!r_step.active || r_step.index < 0 || r_step.index >= static_cast<int>(candidate_indices.size())) {
		return std::nullopt;
	}
	return candidate_indices[r_step.index];
}

ClickPickPlan plan_click_pick_for_hits(
		PickCycleState& state,
		const PickSelectionState& selection,
		const std::vector<PickHitRecord>& hits,
		const ClickPickRequest& request,
		PickHitSet hit_set)
{
	ClickPickPlan plan;
	plan.hit_set = hit_set;
        plan.preserve_selection =
            request.operation == SelectionEditOperation::Add ||
            request.operation == SelectionEditOperation::Remove;
        plan.clear_with_handle_state_on_miss = request.handles_enabled;

	std::optional<int> target_index;
        if (request.operation == SelectionEditOperation::Remove) {
          target_index = hit_set == PickHitSet::Handle
                             ? selected_handle_pick_hit_index(selection, hits)
                             : selected_surface_pick_hit_index(selection, hits);
        } else if (request.operation == SelectionEditOperation::Add) {
          target_index =
              hit_set == PickHitSet::Handle
                  ? front_unselected_handle_pick_hit_index(selection, hits)
                  : front_unselected_surface_pick_hit_index(selection, hits);
        } else {
          plan.used_pick_cycle = true;
          target_index = replacement_pick_hit_index(
              state, hits, request.handles_enabled, request.can_pick_all,
              request.screen_position, request.max_screen_distance, plan.cycle);
        }

        if (!target_index.has_value() || target_index.value() < 0 || target_index.value() >= static_cast<int>(hits.size()) || !hits[target_index.value()].hit) {
		plan.has_target = false;
                plan.hit_set = PickHitSet::None;
                plan.hit_index = -1;
		return plan;
	}

	plan.has_target = true;
	plan.hit_index = target_index.value();
	return plan;
}

} // namespace

quader_poly::PickOptions selection_pick_options_from_radii(double vertex_radius, double edge_radius)
{
	quader_poly::PickOptions options;
        options.vertex_radius = static_cast<float>(
            normalize_pick_radius(vertex_radius, kDefaultVertexPickRadius));
        options.edge_radius = static_cast<float>(
            normalize_pick_radius(edge_radius, kDefaultEdgePickRadius));
        apply_editor_surface_pick_policy(options);
	return options;
}

std::vector<PickObjectFaceGeometry> build_pick_face_geometry(const std::vector<PolygonObject>& objects)
{
	return build_pick_face_geometry(objects, {});
}

std::vector<PickObjectFaceGeometry> build_pick_face_geometry(
		const std::vector<PolygonObject>& objects,
		quader_poly::PickOptions pick_options)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.build_pick_face_geometry");
	std::vector<PickObjectFaceGeometry> result;
	result.reserve(objects.size());
	for (const PolygonObject& object : objects) {
		result.push_back({
			object.id,
			quader_poly::build_face_pick_geometry(object.document, pick_options),
		});
	}
	return result;
}

std::optional<PickHitRecord> pick_nearest(const std::vector<PolygonObject>& objects, const PickRequest& request)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.pick_nearest");
	std::optional<PickHitRecord> nearest;
        const bool component_mode =
            request.options.mode == quader_poly::SelectionMode::Vertex ||
            request.options.mode == quader_poly::SelectionMode::Edge;
        std::vector<std::optional<quader_poly::PickResult>>
            local_surface_hits(component_mode ? objects.size() : 0U);
        const std::optional<PickHitRecord> surface_hit =
            component_mode
                ? nearest_surface_hit(objects, request.ray, request.options, {},
                                      {}, request.target_object_id, {},
                                      &local_surface_hits)
                : std::optional<PickHitRecord>{};
        for (int index = 0; index < static_cast<int>(objects.size()); ++index) {
		if (request.target_object_id >= 0 &&
			objects[index].id != request.target_object_id) {
			continue;
		}
		if (!ray_intersects_object_pick_bounds(objects[index], request.ray, request.options, nullptr)) {
			continue;
		}
		const quader_poly::PickResult hit = pick_object_element(
                    objects[index], index, request.ray, request.options, {}, {},
                    component_mode
                        ? &local_surface_hits[
                              static_cast<std::size_t>(index)]
                        : nullptr);
		if (!hit.hit || component_pick_is_occluded_by_surface(objects, index, hit, surface_hit)) {
			continue;
		}
		PickHitRecord record = make_pick_hit_record(objects[index], index, request, hit);
                if (!nearest.has_value() ||
                    record.ray_distance < nearest->ray_distance ||
                    (std::abs(record.ray_distance - nearest->ray_distance) <=
                         kTransformPointEpsilon &&
                     record.pick_distance < nearest->pick_distance) ||
                    (std::abs(record.ray_distance - nearest->ray_distance) <=
                         kTransformPointEpsilon &&
                     std::abs(record.pick_distance - nearest->pick_distance) <=
                         kTransformPointEpsilon &&
                     record.object_index < nearest->object_index)) {
                  nearest = std::move(record);
                }
        }
	return nearest;
}

std::optional<PickHitRecord> pick_nearest(
		const std::vector<PolygonObject>& objects,
		const PickRequest& request,
		std::span<const PickObjectBounds> object_bounds,
		std::span<const PickObjectFaceGeometry> face_geometry,
		std::span<const PickObjectTransform> object_transforms)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.pick_nearest.cached");
	int hit_index = -1;
	quader_poly::PickResult hit;
	if (!pick_nearest_object(objects, request.ray, request.options, hit_index,
			hit, object_bounds, face_geometry, request.target_object_id,
                        object_transforms) ||
		hit_index < 0) {
		return std::nullopt;
	}
	return make_pick_hit_record(
            objects[static_cast<std::size_t>(hit_index)], hit_index, request,
            hit, cached_transform_for_object(
                     object_transforms, hit_index,
                     objects[static_cast<std::size_t>(hit_index)]));
}

std::optional<PickHitRecord> pick_nearest_priority(
    const std::vector<PolygonObject> &objects, const PickRequest &request,
    std::span<const SelectionMode> priority_modes,
    std::span<const PickObjectBounds> object_bounds,
    std::span<const PickObjectFaceGeometry> face_geometry,
    std::span<const PickObjectTransform> object_transforms,
    quader_poly::document_internal::PickWorkCounters *work_counters) {
  QDR_PROFILE_SCOPE("qdr_editor_core.pick_nearest_priority");

  /// Retains per-object ray and surface facts shared by priority pick passes.
  struct PreparedObjectPick {
    int object_index = -1;
    const PickObjectTransform *object_transform = nullptr;
    quader_poly::Ray local_ray;
    std::optional<quader_poly::PickResult> surface_hit;
    std::unique_ptr<quader_poly::document_internal::DocumentPickFacts> facts;
  };

  std::vector<PreparedObjectPick> prepared;
  prepared.reserve(objects.size());
  std::optional<PickHitRecord> nearest_surface;
  quader_poly::PickOptions surface_options = request.options;
  surface_options.mode = quader_poly::SelectionMode::Face;
  const PickRequest surface_request{
      request.ray, surface_options, request.target_object_id};

  for (int index = 0; index < static_cast<int>(objects.size()); ++index) {
    const PolygonObject &object = objects[static_cast<std::size_t>(index)];
    if (request.target_object_id >= 0 &&
        object.id != request.target_object_id) {
      continue;
    }
    if (!ray_intersects_object_pick_bounds(
            object, request.ray, request.options,
            cached_bounds_for_object(object_bounds, index, object))) {
      continue;
    }

    const PickObjectTransform *object_transform =
        cached_transform_for_object(object_transforms, index, object);
    const quader_poly::Ray local_ray =
        object_space_pick_ray(request.ray, object_transform);
    quader_poly::PickOptions local_options = request.options;
    local_options.vertex_radius = object_space_pick_radius(
        request.options.vertex_radius, object_transform);
    local_options.edge_radius = object_space_pick_radius(
        request.options.edge_radius, object_transform);

    PreparedObjectPick context;
    context.object_index = index;
    context.object_transform = object_transform;
    context.local_ray = local_ray;
    context.facts = std::make_unique<
        quader_poly::document_internal::DocumentPickFacts>(
        object.document, local_options, work_counters);

    quader_poly::PickResult local_surface_hit;
    if (const PickObjectFaceGeometry *cached_geometry =
            cached_face_geometry_for_object(face_geometry, index, object)) {
      local_surface_hit = quader_poly::pick_face_geometry(
          cached_geometry->triangle_span(), local_ray, local_options);
    } else {
      local_surface_hit = context.facts->surface_hit(local_ray);
    }
    if (local_surface_hit.hit) {
      context.surface_hit = local_surface_hit;
      quader_poly::PickResult world_surface_hit = local_surface_hit;
      apply_world_pick_metrics(
          world_surface_hit, request.ray, object_transform);
      PickHitRecord surface_record = make_pick_hit_record(
          object, index, surface_request, world_surface_hit, object_transform);
      if (!nearest_surface.has_value() ||
          surface_record.ray_distance < nearest_surface->ray_distance ||
          (std::abs(surface_record.ray_distance -
                    nearest_surface->ray_distance) <=
               kTransformPointEpsilon &&
           surface_record.object_index < nearest_surface->object_index)) {
        nearest_surface = std::move(surface_record);
      }
    }
    prepared.push_back(std::move(context));
  }

  const auto record_is_better =
      [](const PickHitRecord &candidate,
         const std::optional<PickHitRecord> &nearest) {
        return !nearest.has_value() ||
               candidate.ray_distance < nearest->ray_distance ||
               (std::abs(candidate.ray_distance -
                         nearest->ray_distance) <=
                    kTransformPointEpsilon &&
                candidate.pick_distance < nearest->pick_distance) ||
               (std::abs(candidate.ray_distance -
                         nearest->ray_distance) <=
                    kTransformPointEpsilon &&
                std::abs(candidate.pick_distance -
                         nearest->pick_distance) <=
                    kTransformPointEpsilon &&
                candidate.object_index < nearest->object_index);
      };

  for (const SelectionMode priority_mode : priority_modes) {
    const quader_poly::SelectionMode mode =
        priority_mode == SelectionMode::Vertex
            ? quader_poly::SelectionMode::Vertex
        : priority_mode == SelectionMode::Edge
            ? quader_poly::SelectionMode::Edge
            : quader_poly::SelectionMode::Face;
    std::optional<PickHitRecord> nearest;
    for (PreparedObjectPick &context : prepared) {
      const PolygonObject &object =
          objects[static_cast<std::size_t>(context.object_index)];
      quader_poly::PickResult local_hit;
      if (mode == quader_poly::SelectionMode::Face) {
        if (!context.surface_hit.has_value()) {
          continue;
        }
        local_hit = *context.surface_hit;
      } else {
        local_hit = context.facts->pick_element(
            context.local_ray, mode, &context.surface_hit);
      }
      if (!local_hit.hit) {
        continue;
      }

      quader_poly::PickResult world_hit = local_hit;
      apply_world_pick_metrics(
          world_hit, request.ray, context.object_transform);
      if (component_pick_is_occluded_by_surface(
              objects, context.object_index, world_hit, nearest_surface)) {
        continue;
      }
      PickHitRecord record = make_pick_hit_record(
          object, context.object_index, request, world_hit,
          context.object_transform);
      if (record_is_better(record, nearest)) {
        nearest = std::move(record);
      }
    }
    if (nearest.has_value()) {
      return nearest;
    }
  }
  return std::nullopt;
}

std::vector<PickHitRecord> pick_all(const std::vector<PolygonObject>& objects, const PickRequest& request)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.pick_all");
	std::vector<PickHitRecord> hits;
        const bool component_mode =
            request.options.mode == quader_poly::SelectionMode::Vertex ||
            request.options.mode == quader_poly::SelectionMode::Edge;
        std::vector<std::optional<quader_poly::PickResult>>
            local_surface_hits(component_mode ? objects.size() : 0U);
        const std::optional<PickHitRecord> surface_hit =
            component_mode
                ? nearest_surface_hit(objects, request.ray, request.options, {},
                                      {}, request.target_object_id, {},
                                      &local_surface_hits)
                : std::optional<PickHitRecord>{};
        for (int index = 0; index < static_cast<int>(objects.size()); ++index) {
		if (request.target_object_id >= 0 &&
			objects[index].id != request.target_object_id) {
			continue;
		}
		if (!ray_intersects_object_pick_bounds(objects[index], request.ray, request.options, nullptr)) {
			continue;
		}
		const quader_poly::PickResult hit = pick_object_element(
                    objects[index], index, request.ray, request.options, {}, {},
                    component_mode
                        ? &local_surface_hits[
                              static_cast<std::size_t>(index)]
                        : nullptr);
		if (hit.hit && !component_pick_is_occluded_by_surface(objects, index, hit, surface_hit)) {
			hits.push_back(make_pick_hit_record(objects[index], index, request, hit));
		}
	}
        std::sort(hits.begin(), hits.end(),
                  [](const PickHitRecord &left, const PickHitRecord &right) {
                    if (std::abs(left.ray_distance - right.ray_distance) >
                        kTransformPointEpsilon) {
                      return left.ray_distance < right.ray_distance;
                    }
                    if (std::abs(left.pick_distance - right.pick_distance) >
                        kTransformPointEpsilon) {
                      return left.pick_distance < right.pick_distance;
                    }
                    if (left.object_id != right.object_id) {
                      return left.object_id < right.object_id;
                    }
                    return left.signature < right.signature;
                  });
        return hits;
}

std::vector<PickHitRecord> pick_all(
		const std::vector<PolygonObject>& objects,
		const PickRequest& request,
		std::span<const PickObjectBounds> object_bounds,
		std::span<const PickObjectFaceGeometry> face_geometry,
		std::span<const PickObjectTransform> object_transforms)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.pick_all.cached");
	std::vector<PickHitRecord> hits;
        const bool component_mode =
            request.options.mode == quader_poly::SelectionMode::Vertex ||
            request.options.mode == quader_poly::SelectionMode::Edge;
        std::vector<std::optional<quader_poly::PickResult>>
            local_surface_hits(component_mode ? objects.size() : 0U);
        const std::optional<PickHitRecord> surface_hit =
            component_mode
                ? nearest_surface_hit(objects, request.ray, request.options,
                                      object_bounds, face_geometry,
                                      request.target_object_id,
                                      object_transforms,
                                      &local_surface_hits)
                : std::optional<PickHitRecord>{};
        for (int index = 0; index < static_cast<int>(objects.size()); ++index) {
		if (request.target_object_id >= 0 &&
			objects[index].id != request.target_object_id) {
			continue;
		}
		if (!ray_intersects_object_pick_bounds(
					objects[index],
					request.ray,
					request.options,
					cached_bounds_for_object(object_bounds, index, objects[index]))) {
			continue;
		}
		const quader_poly::PickResult hit =
			pick_object_element(objects[index], index, request.ray,
                                        request.options, face_geometry,
                                        object_transforms,
                                        component_mode
                                            ? &local_surface_hits[
                                                  static_cast<std::size_t>(
                                                      index)]
                                            : nullptr);
		if (hit.hit && !component_pick_is_occluded_by_surface(objects, index, hit, surface_hit)) {
			hits.push_back(make_pick_hit_record(
                            objects[index], index, request, hit,
                            cached_transform_for_object(
                                object_transforms, index, objects[index])));
		}
	}
        std::sort(hits.begin(), hits.end(),
                  [](const PickHitRecord &left, const PickHitRecord &right) {
                    if (std::abs(left.ray_distance - right.ray_distance) >
                        kTransformPointEpsilon) {
                      return left.ray_distance < right.ray_distance;
                    }
                    if (std::abs(left.pick_distance - right.pick_distance) >
                        kTransformPointEpsilon) {
                      return left.pick_distance < right.pick_distance;
                    }
                    if (left.object_id != right.object_id) {
                      return left.object_id < right.object_id;
                    }
                    return left.signature < right.signature;
                  });
        return hits;
}

std::string pick_hits_signature(const std::vector<PickHitRecord>& hits)
{
	std::string signature;
	for (const PickHitRecord& hit : hits) {
		if (!signature.empty()) {
			signature += "|";
		}
		signature += pick_hit_signature(hit);
	}
	return signature;
}

std::string pick_hit_signature(const PickHitRecord& hit)
{
	if (!hit.signature.empty()) {
		return hit.signature;
	}
	std::string signature = "object:";
	signature += std::to_string(hit.object_id);
	switch (hit.kind) {
        case quader_poly::ElementKind::Vertex:
          signature += "/vertex:";
          signature += std::to_string(hit.vertex_id);
          break;
        case quader_poly::ElementKind::Edge:
          signature += "/edge:";
          signature += std::to_string(hit.edge.a);
          signature += "-";
          signature += std::to_string(hit.edge.b);
          break;
        case quader_poly::ElementKind::Face:
        default:
          signature += "/face:";
          signature += std::to_string(hit.face_id);
          break;
	}
	return signature;
}

namespace {

/// Identifies one object or component in the current pick selection.
struct PickSelectionKey {
  int object_id = -1;
  quader_poly::ElementKind kind = quader_poly::ElementKind::Vertex;
  quader_poly::ElementId first = quader_poly::kInvalidElementId;
  quader_poly::ElementId second = quader_poly::kInvalidElementId;

  friend bool operator==(const PickSelectionKey &left,
                         const PickSelectionKey &right) = default;
};

/// Hashes normalized pick-selection identities for membership queries.
struct PickSelectionKeyHash {
  [[nodiscard]] std::size_t
  operator()(const PickSelectionKey &key) const noexcept {
    std::size_t seed = std::hash<int>{}(key.object_id);
    const auto combine = [&seed](std::size_t value) {
      seed ^= value + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
    };
    combine(std::hash<int>{}(static_cast<int>(key.kind)));
    combine(std::hash<quader_poly::ElementId>{}(key.first));
    combine(std::hash<quader_poly::ElementId>{}(key.second));
    return seed;
  }
};

/// Provides constant-time selection membership during pick overlay projection.
class PickSelectionIndex final {
public:
  explicit PickSelectionIndex(const PickSelectionState &selection)
      : mode_(selection.mode) {
    object_ids_.reserve(selection.selected_object_ids.size());
    object_ids_.insert(selection.selected_object_ids.begin(),
                       selection.selected_object_ids.end());
    component_keys_.reserve(selection.selected_faces.size() +
                            selection.selected_edges.size() +
                            selection.selected_vertices.size());
    for (const PickSelectionGroup &group : selection.selected_faces) {
      if (group.kind == quader_poly::ElementKind::Face) {
        component_keys_.insert(
            {group.object_id, group.kind, group.face_id,
             quader_poly::kInvalidElementId});
      }
    }
    for (const PickSelectionGroup &group : selection.selected_edges) {
      if (group.kind != quader_poly::ElementKind::Edge) {
        continue;
      }
      const quader_poly::Edge edge =
          quader_poly::make_edge(group.edge.a, group.edge.b);
      component_keys_.insert(
          {group.object_id, group.kind, edge.a, edge.b});
    }
    for (const PickSelectionGroup &group : selection.selected_vertices) {
      if (group.kind == quader_poly::ElementKind::Vertex) {
        component_keys_.insert(
            {group.object_id, group.kind, group.vertex_id,
             quader_poly::kInvalidElementId});
      }
    }
  }

  [[nodiscard]] bool hit_target_is_selected(
      const PickHitRecord &hit) const {
    if (!hit.hit || hit.object_id < 0) {
      return false;
    }
    if (mode_ == SelectionMode::Object) {
      return object_ids_.contains(hit.object_id);
    }
    if (mode_ == SelectionMode::Face) {
      return hit.kind == quader_poly::ElementKind::Face &&
             contains_face(hit);
    }
    if (mode_ == SelectionMode::Edge) {
      return hit.kind == quader_poly::ElementKind::Edge &&
             contains_edge(hit);
    }
    return hit.kind == quader_poly::ElementKind::Vertex &&
           contains_vertex(hit);
  }

  [[nodiscard]] bool surface_target_is_selected(
      const PickHitRecord &hit) const {
    if (!hit.hit || hit.object_id < 0) {
      return false;
    }
    return mode_ == SelectionMode::Face
               ? contains_face(hit)
               : object_ids_.contains(hit.object_id);
  }

  [[nodiscard]] bool handle_target_is_selected(
      const PickHitRecord &hit) const {
    if (!hit.hit || hit.object_id < 0) {
      return false;
    }
    switch (hit.kind) {
    case quader_poly::ElementKind::Face:
      return contains_face(hit);
    case quader_poly::ElementKind::Edge:
      return contains_edge(hit);
    case quader_poly::ElementKind::Vertex:
      return contains_vertex(hit);
    }
    return false;
  }

private:
  [[nodiscard]] bool contains_face(const PickHitRecord &hit) const {
    return hit.face_id != quader_poly::kInvalidElementId &&
           component_keys_.contains(
               {hit.object_id, quader_poly::ElementKind::Face, hit.face_id,
                quader_poly::kInvalidElementId});
  }

  [[nodiscard]] bool contains_edge(const PickHitRecord &hit) const {
    if (hit.edge.a == quader_poly::kInvalidElementId ||
        hit.edge.b == quader_poly::kInvalidElementId) {
      return false;
    }
    const quader_poly::Edge edge =
        quader_poly::make_edge(hit.edge.a, hit.edge.b);
    return component_keys_.contains(
        {hit.object_id, quader_poly::ElementKind::Edge, edge.a, edge.b});
  }

  [[nodiscard]] bool contains_vertex(const PickHitRecord &hit) const {
    return hit.vertex_id != quader_poly::kInvalidElementId &&
           component_keys_.contains(
               {hit.object_id, quader_poly::ElementKind::Vertex,
                hit.vertex_id, quader_poly::kInvalidElementId});
  }

  SelectionMode mode_ = SelectionMode::Object;
  std::unordered_set<int> object_ids_;
  std::unordered_set<PickSelectionKey, PickSelectionKeyHash> component_keys_;
};

} // namespace

bool pick_hit_target_is_selected(const PickSelectionState& selection, const PickHitRecord& hit)
{
  return PickSelectionIndex(selection).hit_target_is_selected(hit);
}

bool pick_surface_hit_target_is_selected(const PickSelectionState& selection, const PickHitRecord& hit)
{
  return PickSelectionIndex(selection).surface_target_is_selected(hit);
}

bool pick_handle_target_is_selected(const PickSelectionState& selection, const PickHitRecord& hit)
{
  return PickSelectionIndex(selection).handle_target_is_selected(hit);
}

std::optional<int> selected_pick_hit_index(const PickSelectionState& selection, const std::vector<PickHitRecord>& hits)
{
        const PickSelectionIndex selection_index(selection);
	for (int index = 0; index < static_cast<int>(hits.size()); ++index) {
		if (selection_index.hit_target_is_selected(hits[index])) {
			return index;
		}
	}
	return std::nullopt;
}

std::optional<int> first_unselected_pick_hit_index(const PickSelectionState& selection, const std::vector<PickHitRecord>& hits)
{
        const PickSelectionIndex selection_index(selection);
	for (int index = 0; index < static_cast<int>(hits.size()); ++index) {
		const PickHitRecord& hit = hits[index];
		if (hit.hit && !selection_index.hit_target_is_selected(hit)) {
			return index;
		}
	}
	return std::nullopt;
}

std::optional<int> selected_surface_pick_hit_index(const PickSelectionState& selection, const std::vector<PickHitRecord>& hits)
{
        const PickSelectionIndex selection_index(selection);
	for (int index = 0; index < static_cast<int>(hits.size()); ++index) {
		if (selection_index.surface_target_is_selected(hits[index])) {
			return index;
		}
	}
	return std::nullopt;
}

std::optional<int> front_unselected_surface_pick_hit_index(const PickSelectionState& selection, const std::vector<PickHitRecord>& hits)
{
        const PickSelectionIndex selection_index(selection);
	for (int index = 0; index < static_cast<int>(hits.size()); ++index) {
		const PickHitRecord& hit = hits[index];
		if (!hit.hit) {
			continue;
		}
		if (!selection_index.surface_target_is_selected(hit)) {
			return index;
		}
		return std::nullopt;
	}
	return std::nullopt;
}

std::optional<int> selected_handle_pick_hit_index(const PickSelectionState& selection, const std::vector<PickHitRecord>& hits)
{
        const PickSelectionIndex selection_index(selection);
	for (int index = 0; index < static_cast<int>(hits.size()); ++index) {
		if (selection_index.handle_target_is_selected(hits[index])) {
			return index;
		}
	}
	return std::nullopt;
}

std::optional<int> front_unselected_handle_pick_hit_index(const PickSelectionState& selection, const std::vector<PickHitRecord>& hits)
{
        const PickSelectionIndex selection_index(selection);
	for (int index = 0; index < static_cast<int>(hits.size()); ++index) {
		const PickHitRecord& hit = hits[index];
		if (!hit.hit) {
			continue;
		}
		if (!selection_index.handle_target_is_selected(hit)) {
			return index;
		}
		return std::nullopt;
	}
	return std::nullopt;
}

std::vector<int> pick_cycle_candidate_indices(const std::vector<PickHitRecord>& hits, bool keep_all_component_hits)
{
	std::vector<int> indices;
	std::unordered_set<int> seen_object_ids;
	indices.reserve(hits.size());
	seen_object_ids.reserve(hits.size());
	for (int index = 0; index < static_cast<int>(hits.size()); ++index) {
		const PickHitRecord& hit = hits[index];
		if (!hit.hit || hit.object_id < 0) {
			continue;
		}
		if (keep_all_component_hits) {
			indices.push_back(index);
			continue;
		}
		if (!seen_object_ids.insert(hit.object_id).second) {
			continue;
		}
		indices.push_back(index);
	}
	return indices;
}

void reset_pick_cycle(PickCycleState& state)
{
	state = PickCycleState();
}

PickCycleStep advance_pick_cycle(PickCycleState& state, std::string signature, int candidate_count, quader::QVec2 screen_position, float max_screen_distance)
{
	if (candidate_count <= 0) {
		reset_pick_cycle(state);
		return {};
	}

	const float max_distance = std::max(0.0F, max_screen_distance);
	const float screen_distance_squared =
		quader_geometry::length_squared(to_geometry_vec2(screen_position) - to_geometry_vec2(state.screen_position));
	const bool moved_too_far = state.active && screen_distance_squared > max_distance * max_distance;
	const bool same_cycle = state.active && state.signature == signature && !moved_too_far;
	state.active = true;
	state.screen_position = screen_position;
	state.signature = std::move(signature);
	if (!same_cycle) {
		state.index = 0;
		return { true, false, state.index };
	}

	state.index = (state.index + 1) % candidate_count;
	return { true, true, state.index };
}

ClickPickPlan plan_click_pick(PickCycleState& state, const PickSelectionState& selection, const std::vector<PickHitRecord>& handle_hits, const std::vector<PickHitRecord>& surface_hits, const ClickPickRequest& request)
{
	if (request.handles_enabled) {
          ClickPickPlan handle_plan = plan_click_pick_for_hits(
              state, selection, handle_hits, request, PickHitSet::Handle);
          if (handle_plan.has_target) {
            return handle_plan;
          }
	}

        ClickPickPlan surface_plan = plan_click_pick_for_hits(
            state, selection, surface_hits, request, PickHitSet::Surface);
        if (surface_plan.has_target) {
		return surface_plan;
	}

	ClickPickPlan miss_plan;
        miss_plan.preserve_selection =
            request.operation == SelectionEditOperation::Add ||
            request.operation == SelectionEditOperation::Remove;
        miss_plan.clear_with_handle_state_on_miss = request.handles_enabled;
	return miss_plan;
}

DragSeedPickPlan plan_drag_seed_pick(const PickSelectionState& selection, const std::vector<PickHitRecord>& handle_hits, const std::vector<PickHitRecord>& surface_hits, const DragSeedPickRequest& request)
{
	const auto plan_for_hits = [&](const std::vector<PickHitRecord>& hits, PickHitSet hit_set) {
		DragSeedPickPlan plan;
		plan.hit_set = hit_set;

		std::optional<int> target_index;
		if (request.allow_selected_target) {
                  target_index =
                      hit_set == PickHitSet::Handle
                          ? selected_handle_pick_hit_index(selection, hits)
                          : selected_surface_pick_hit_index(selection, hits);
                  if (target_index.has_value() && request.additive &&
                      request.preserve_selected_additive_target) {
                    const PickHitRecord &hit = hits[target_index.value()];
                    plan.has_target = true;
                    plan.keep_existing_selection = true;
                    plan.hit_index = target_index.value();
                    plan.preferred_object_id = hit.object_id;
                    return plan;
                  }
		}

		if (!target_index.has_value()) {
                  target_index = hit_set == PickHitSet::Handle
                                     ? front_unselected_handle_pick_hit_index(
                                           selection, hits)
                                     : front_unselected_surface_pick_hit_index(
                                           selection, hits);
                }
		if (!target_index.has_value() || target_index.value() < 0 || target_index.value() >= static_cast<int>(hits.size()) || !hits[target_index.value()].hit) {
			plan.has_target = false;
                        plan.hit_set = PickHitSet::None;
                        plan.hit_index = -1;
			plan.preferred_object_id = -1;
			return plan;
		}

		const PickHitRecord& hit = hits[target_index.value()];
		plan.has_target = true;
		plan.keep_existing_selection = false;
		plan.hit_index = target_index.value();
		plan.preferred_object_id = hit.object_id;
		return plan;
	};

	if (request.handles_enabled) {
          const DragSeedPickPlan handle_plan =
              plan_for_hits(handle_hits, PickHitSet::Handle);
          if (handle_plan.has_target) {
            return handle_plan;
          }
	}

        const DragSeedPickPlan surface_plan =
            plan_for_hits(surface_hits, PickHitSet::Surface);
        if (surface_plan.has_target) {
		return surface_plan;
	}
	return {};
}

SelectionHoverUpdateResult update_selection_hover(
		const std::vector<PolygonObject>& objects,
		SelectionMode mode,
		const SelectionHoverState& current_hover,
		const quader_poly::Ray& ray,
		bool selected_modifier_pressed,
		quader_poly::PickOptions pick_options,
		std::span<const PickObjectBounds> object_bounds,
		std::span<const PickObjectFaceGeometry> face_geometry,
                bool face_plane_hover,
                std::span<const PickObjectTransform> object_transforms)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.update_selection_hover");
	apply_editor_surface_pick_policy(pick_options);
	SelectionHoverState next;
	next.candidate_object_id = current_hover.candidate_object_id;

        if (face_plane_hover) {
          int hit_index = -1;
          quader_poly::PickResult hit;
          quader_poly::PickOptions options = pick_options;
          options.mode = quader_poly::SelectionMode::Face;
          if (pick_nearest_object(objects, ray, options, hit_index, hit,
                                  object_bounds, face_geometry, -1,
                                  object_transforms) &&
              hit_index >= 0 && hit.kind == quader_poly::ElementKind::Face) {
            next.object_id = objects[hit_index].id;
            next.kind = SelectionHoverKind::Face;
            next.face_id = hit.face_id;
            next.face_plane_hover = true;
            if (next.object_id > 0) {
              next.candidate_object_id = next.object_id;
            }
          }
        } else if (mode == SelectionMode::Object) {
          int hit_index = -1;
          quader_poly::PickResult hit;
          quader_poly::PickOptions options = pick_options;
          options.mode = quader_poly::SelectionMode::Face;
          if (pick_nearest_object(objects, ray, options, hit_index, hit,
                                  object_bounds, face_geometry, -1,
                                  object_transforms) &&
              hit_index >= 0) {
            next.object_id = objects[hit_index].id;
            if (next.object_id > 0) {
              next.candidate_object_id = next.object_id;
            }
          }
        } else if (!objects.empty()) {
          int hit_index = -1;
          quader_poly::PickResult hit;
          quader_poly::PickOptions options = pick_options;
          options.mode = component_selection_mode(mode);
          bool component_hover = false;
          if (const std::optional<SourceWireComponentPick> source_wire_hit =
                  pick_active_source_wire_component(
                      objects, mode, ray, options,
                      current_hover.candidate_object_id,
                      selected_modifier_pressed, object_bounds,
                      face_geometry, object_transforms)) {
            hit_index = source_wire_hit->object_index;
            hit = source_wire_hit->hit;
            component_hover = true;
          } else {
            component_hover =
                pick_nearest_object(objects, ray, options, hit_index, hit,
                                    object_bounds, face_geometry, -1,
                                    object_transforms);
          }
          if (!component_hover) {
            options.mode = quader_poly::SelectionMode::Face;
            pick_nearest_object(objects, ray, options, hit_index, hit,
                                object_bounds, face_geometry, -1,
                                object_transforms);
          }
          if (hit_index >= 0) {
            next.object_id = objects[hit_index].id;
            if (next.object_id > 0) {
              next.candidate_object_id = next.object_id;
            }
            if (component_hover) {
              switch (hit.kind) {
              case quader_poly::ElementKind::Vertex:
                next.kind = SelectionHoverKind::Vertex;
                next.vertex_id = hit.vertex_id;
                break;
              case quader_poly::ElementKind::Edge:
                next.kind = SelectionHoverKind::Edge;
                next.edge = hit.edge;
                break;
              case quader_poly::ElementKind::Face:
                next.kind = SelectionHoverKind::Face;
                next.face_id = hit.face_id;
                break;
              }
              next.selected_modifier_pressed =
                  next.kind != SelectionHoverKind::None &&
                  selected_modifier_pressed;
            }
          }
        }

        const bool changed = current_hover.object_id != next.object_id ||
		current_hover.kind != next.kind ||
		current_hover.vertex_id != next.vertex_id ||
		current_hover.edge != next.edge ||
		current_hover.face_id != next.face_id ||
		current_hover.selected_modifier_pressed != next.selected_modifier_pressed ||
                current_hover.face_plane_hover != next.face_plane_hover ||
		current_hover.candidate_object_id != next.candidate_object_id;
	return { changed, next };
}

bool selection_hover_target_matches_selection(std::span<const PolygonObject> objects, SelectionMode mode, const SelectionHoverState& hover)
{
  if (mode == SelectionMode::Object || hover.object_id <= 0 ||
      hover.kind == SelectionHoverKind::None) {
    return false;
  }
        for (const PolygonObject& object : objects) {
		if (object.id != hover.object_id || !object.selected) {
			continue;
		}
                if (hover.kind == SelectionHoverKind::Face &&
                    mode == SelectionMode::Face) {
                  return quader_poly::selection_contains_face(object.selection,
                                                              hover.face_id);
                }
                if (hover.kind == SelectionHoverKind::Edge &&
                    mode == SelectionMode::Edge) {
                  return quader_poly::selection_contains(object.selection,
                                                         hover.edge);
                }
                if (hover.kind == SelectionHoverKind::Vertex &&
                    mode == SelectionMode::Vertex) {
                  return quader_poly::selection_contains(object.selection,
                                                         hover.vertex_id);
                }
                return false;
	}
	return false;
}

bool selection_hover_target_matches_selection(const std::vector<PolygonObject>& objects, SelectionMode mode, const SelectionHoverState& hover)
{
	return selection_hover_target_matches_selection(std::span<const PolygonObject>(objects), mode, hover);
}

bool selection_hover_target_is_selected(std::span<const PolygonObject> objects, SelectionMode mode, const SelectionHoverState& hover)
{
	return hover.selected_modifier_pressed && selection_hover_target_matches_selection(objects, mode, hover);
}

bool selection_hover_target_is_selected(const std::vector<PolygonObject>& objects, SelectionMode mode, const SelectionHoverState& hover)
{
	return selection_hover_target_is_selected(std::span<const PolygonObject>(objects), mode, hover);
}

bool pick_select(
		std::vector<PolygonObject>& objects,
		SelectionMode mode,
		SelectionEditOperation operation,
		const quader_poly::Ray& ray,
		int& active_object_id,
		quader_poly::PickOptions pick_options,
		bool allow_component_fallback,
		std::span<const PickObjectBounds> object_bounds,
		std::span<const PickObjectFaceGeometry> face_geometry,
		std::span<const PickObjectTransform> object_transforms)
{
  const bool replace_selection = operation == SelectionEditOperation::Replace;
  const bool add_selection_value = operation == SelectionEditOperation::Add;
  const bool remove_selection_value =
      operation == SelectionEditOperation::Remove;
  apply_editor_surface_pick_policy(pick_options);
  if (objects.empty()) {
    if (replace_selection) {
      clear_selection(objects, active_object_id);
    }
    return false;
  }

	int hit_index = -1;
	quader_poly::PickResult hit;
	quader_poly::PickOptions options = pick_options;
        options.mode = mode == SelectionMode::Object
                           ? quader_poly::SelectionMode::Face
                           : component_selection_mode(mode);
        const bool component_mode = mode != SelectionMode::Object;
        bool component_hit = false;
	if (component_mode) {
		if (const std::optional<SourceWireComponentPick> source_wire_hit =
					pick_active_source_wire_component(
						objects,
						mode,
						ray,
						options,
						-1,
						remove_selection_value, object_bounds,
                                                face_geometry,
                                                object_transforms)) {
			hit_index = source_wire_hit->object_index;
			hit = source_wire_hit->hit;
			component_hit = true;
		}
	}
	if (!component_hit) {
		component_hit = pick_nearest_object(
                    objects, ray, options, hit_index, hit, object_bounds,
                    face_geometry, -1, object_transforms);
	}
	if (!component_hit && component_mode && allow_component_fallback) {
          options.mode = quader_poly::SelectionMode::Face;
          pick_nearest_object(objects, ray, options, hit_index, hit,
                              object_bounds, face_geometry, -1,
                              object_transforms);
	}

	if (hit_index < 0) {
		if (replace_selection) {
			clear_selection(objects, active_object_id);
		}
		return false;
	}

        if (mode == SelectionMode::Object) {
          if (replace_selection) {
            clear_selection(objects, active_object_id);
          }
          PolygonObject &object = objects[hit_index];
          object.selected = !remove_selection_value;
          object.component_edit_target = false;
          if (object.selected) {
            active_object_id = object.id;
          } else {
            normalize_active_object_id(objects, active_object_id);
          }
          return true;
        }

        for (int index = 0; index < static_cast<int>(objects.size()); ++index) {
		if (index != hit_index && replace_selection) {
			objects[index].selected = false;
			objects[index].component_edit_target = false;
			objects[index].selection = quader_poly::Selection();
		}
	}
	PolygonObject& object = objects[hit_index];
	const bool was_edit_target = object.selected || object.id == active_object_id;
	object.selection.mode = component_selection_mode(mode);
	if (!component_hit) {
		if (remove_selection_value) {
			return false;
		}
		if (replace_selection) {
			object.selection = quader_poly::Selection();
			object.selection.mode = component_selection_mode(mode);
		}
		if (!remove_selection_value) {
			object.selected = true;
			object.component_edit_target = true;
			active_object_id = object.id;
		} else if (was_edit_target) {
			keep_component_edit_target(object, active_object_id);
		} else {
			object.selected = false;
			object.component_edit_target = false;
			normalize_active_object_id(objects, active_object_id);
		}
		return true;
	}
	if (remove_selection_value) {
		quader_poly::remove_selection(object.selection, hit);
	} else if (add_selection_value) {
		quader_poly::add_selection(object.selection, hit);
	} else {
		quader_poly::select_only(object.selection, hit);
	}
	if (!object.selection.empty()) {
		object.selected = true;
		object.component_edit_target = true;
		if (!remove_selection_value) {
			active_object_id = object.id;
		} else {
			normalize_active_object_id(objects, active_object_id);
		}
	} else if (remove_selection_value && was_edit_target) {
		keep_component_edit_target(object, active_object_id);
	} else {
		object.selected = false;
		object.component_edit_target = false;
		normalize_active_object_id(objects, active_object_id);
	}
	return true;
}

bool pick_select_face(
		std::vector<PolygonObject>& objects,
		SelectionMode& mode,
		SelectionEditOperation operation,
		const quader_poly::Ray& ray,
		int& active_object_id,
		quader_poly::PickOptions pick_options)
{
  const bool replace_selection = operation == SelectionEditOperation::Replace;
  const bool add_selection_value = operation == SelectionEditOperation::Add;
  const bool remove_selection_value =
      operation == SelectionEditOperation::Remove;
  apply_editor_surface_pick_policy(pick_options);
  if (objects.empty()) {
    if (replace_selection) {
      clear_selection(objects, active_object_id);
    }
    return false;
  }
  mode = SelectionMode::Face;
  int hit_index = -1;
  quader_poly::PickResult hit;
  quader_poly::PickOptions options = pick_options;
  options.mode = quader_poly::SelectionMode::Face;
  if (!pick_nearest_object(objects, ray, options, hit_index, hit)) {
    if (replace_selection) {
      clear_selection(objects, active_object_id);
    }
    return false;
  }
	for (int index = 0; index < static_cast<int>(objects.size()); ++index) {
		if (index != hit_index && replace_selection) {
			objects[index].selected = false;
			objects[index].component_edit_target = false;
			objects[index].selection = quader_poly::Selection();
		}
	}
	PolygonObject& object = objects[hit_index];
	const bool was_edit_target = object.selected || object.id == active_object_id;
        object.selection.mode = quader_poly::SelectionMode::Face;
        if (remove_selection_value) {
		quader_poly::remove_selection(object.selection, hit);
	} else if (add_selection_value) {
		quader_poly::add_selection(object.selection, hit);
	} else {
		quader_poly::select_only(object.selection, hit);
	}
	if (!object.selection.empty()) {
		object.selected = true;
		object.component_edit_target = true;
		if (!remove_selection_value) {
			active_object_id = object.id;
		} else {
			normalize_active_object_id(objects, active_object_id);
		}
	} else if (remove_selection_value && was_edit_target) {
		keep_component_edit_target(object, active_object_id);
	} else {
		object.selected = false;
		object.component_edit_target = false;
		normalize_active_object_id(objects, active_object_id);
	}
	return true;
}

bool pick_select_all_faces(
		std::vector<PolygonObject>& objects,
		SelectionMode& mode,
		SelectionEditOperation operation,
		const quader_poly::Ray& ray,
		int& active_object_id,
		quader_poly::PickOptions pick_options,
		std::span<const PickObjectBounds> object_bounds,
		std::span<const PickObjectFaceGeometry> face_geometry,
		std::span<const PickObjectTransform> object_transforms)
{
  const bool replace_selection = operation == SelectionEditOperation::Replace;
  const bool remove_selection_value =
      operation == SelectionEditOperation::Remove;
  apply_editor_surface_pick_policy(pick_options);
  if (objects.empty()) {
    if (replace_selection) {
      clear_selection(objects, active_object_id);
    }
    return false;
  }
  mode = SelectionMode::Face;
  int hit_index = -1;
  quader_poly::PickResult hit;
  quader_poly::PickOptions options = pick_options;
  options.mode = quader_poly::SelectionMode::Face;
  if (!pick_nearest_object(objects, ray, options, hit_index, hit, object_bounds,
                           face_geometry, -1, object_transforms)) {
    if (replace_selection) {
      clear_selection(objects, active_object_id);
    }
    return false;
  }
	for (int index = 0; index < static_cast<int>(objects.size()); ++index) {
		if (index != hit_index && replace_selection) {
			objects[index].selected = false;
			objects[index].component_edit_target = false;
			objects[index].selection = quader_poly::Selection();
		}
	}
	PolygonObject& object = objects[hit_index];
	if (object.document.faces.empty()) {
		return false;
	}
	const bool was_edit_target = object.selected || object.id == active_object_id;
        object.selection.mode = quader_poly::SelectionMode::Face;
        if (remove_selection_value) {
		object.selection.faces.clear();
		object.selection.clear();
		if (was_edit_target) {
			keep_component_edit_target(object, active_object_id);
		} else {
			object.selected = false;
			object.component_edit_target = false;
			normalize_active_object_id(objects, active_object_id);
		}
		return true;
	}
	if (replace_selection) {
		object.selection.faces.clear();
	}
	object.selection.faces.reserve(object.document.faces.size());
	for (const quader_poly::Face& face : object.document.faces) {
		if (!options.hides_material_slot(face.material_slot) &&
				!quader_poly::selection_contains_face(object.selection, face.id)) {
			object.selection.faces.push_back(face.id);
		}
	}
	if (object.selection.faces.empty()) {
		object.selected = false;
		object.component_edit_target = false;
		normalize_active_object_id(objects, active_object_id);
		return was_edit_target;
	}
	object.selection.has_active = !object.selection.faces.empty();
        object.selection.active_kind = quader_poly::ElementKind::Face;
        object.selection.active_face = object.selection.faces.empty()
                                           ? quader_poly::kInvalidElementId
                                           : object.selection.faces.back();
        object.selected = true;
        object.component_edit_target = true;
	active_object_id = object.id;
	return true;
}

bool pick_select_edge_loop(
		std::vector<PolygonObject>& objects,
		SelectionMode& mode,
		SelectionEditOperation operation,
		const quader_poly::Ray& ray,
		int& active_object_id,
		quader_poly::PickOptions pick_options,
		std::span<const PickObjectBounds> object_bounds,
		std::span<const PickObjectFaceGeometry> face_geometry,
		std::span<const PickObjectTransform> object_transforms)
{
  const bool replace_selection = operation == SelectionEditOperation::Replace;
  const bool remove_selection_value =
      operation == SelectionEditOperation::Remove;
  apply_editor_surface_pick_policy(pick_options);
  if (objects.empty()) {
    if (replace_selection) {
      clear_selection(objects, active_object_id);
    }
    return false;
  }
  mode = SelectionMode::Edge;
  int hit_index = -1;
  quader_poly::PickResult hit;
  quader_poly::PickOptions options = pick_options;
  options.mode = quader_poly::SelectionMode::Edge;
  bool component_hit = false;
  if (const std::optional<SourceWireComponentPick> source_wire_hit =
          pick_active_source_wire_component(objects, SelectionMode::Edge, ray,
                                            options, -1,
                                            remove_selection_value,
                                            object_bounds,
                                            face_geometry,
                                            object_transforms)) {
    hit_index = source_wire_hit->object_index;
    hit = source_wire_hit->hit;
    component_hit = true;
  }
  if ((!component_hit &&
       !pick_nearest_object(objects, ray, options, hit_index, hit,
                            object_bounds, face_geometry, -1,
                            object_transforms)) ||
      hit.kind != quader_poly::ElementKind::Edge) {
    if (replace_selection) {
      clear_selection(objects, active_object_id);
    }
    return false;
  }
        const quader_poly::Ray local_ray = object_space_pick_ray(
            ray, cached_transform_for_object(object_transforms, hit_index,
                                             objects[hit_index]));
        const quader_poly::ElementId context_face_id =
            edge_loop_context_face_id(objects[hit_index].document, hit,
                                      local_ray, options);
        const std::vector<quader_poly::Edge> loop_edges =
            quader_poly::edge_loop_edges(objects[hit_index].document, hit.edge,
                                         context_face_id);
	if (loop_edges.empty()) {
		return false;
	}
	for (int index = 0; index < static_cast<int>(objects.size()); ++index) {
		if (index != hit_index && replace_selection) {
			objects[index].selected = false;
			objects[index].component_edit_target = false;
			objects[index].selection = quader_poly::Selection();
		}
	}

	PolygonObject& object = objects[hit_index];
	const bool was_edit_target = object.selected || object.id == active_object_id;
        quader_poly::document_internal::DocumentPickFacts pick_facts(
            object.document, options);
        object.selection.mode = quader_poly::SelectionMode::Edge;
        if (replace_selection) {
		object.selection.clear();
                object.selection.mode = quader_poly::SelectionMode::Edge;
        }
	if (remove_selection_value) {
		for (const quader_poly::Edge& loop_edge : loop_edges) {
			if (!pick_facts.edge_can_be_picked(loop_edge)) {
				continue;
			}
			const quader_poly::Edge normalized = quader_poly::make_edge(loop_edge.a, loop_edge.b);
			std::erase_if(object.selection.edges, [normalized](quader_poly::Edge edge) {
				return quader_poly::make_edge(edge.a, edge.b) == normalized;
			});
		}
	} else {
		for (const quader_poly::Edge& loop_edge : loop_edges) {
			if (pick_facts.edge_can_be_picked(loop_edge) &&
					!quader_poly::selection_contains(object.selection, loop_edge)) {
				object.selection.edges.push_back(quader_poly::make_edge(loop_edge.a, loop_edge.b));
			}
		}
	}
        object.selection.active_kind = quader_poly::ElementKind::Edge;
        object.selection.has_active = !object.selection.edges.empty();
	object.selection.active_edge = object.selection.edges.empty() ? quader_poly::Edge {} : (remove_selection_value ? object.selection.edges.back() : quader_poly::make_edge(hit.edge.a, hit.edge.b));
	if (object.selection.edges.empty()) {
		if (remove_selection_value && was_edit_target) {
			keep_component_edit_target(object, active_object_id);
		} else {
			object.selected = false;
			object.component_edit_target = false;
			normalize_active_object_id(objects, active_object_id);
		}
		return true;
	}
	object.selected = true;
	object.component_edit_target = true;
	active_object_id = object.id;
	return true;
}

bool pick_select_all_vertices(
		std::vector<PolygonObject>& objects,
		SelectionMode& mode,
		SelectionEditOperation operation,
		const quader_poly::Ray& ray,
		int& active_object_id,
		quader_poly::PickOptions pick_options,
		std::span<const PickObjectBounds> object_bounds,
		std::span<const PickObjectFaceGeometry> face_geometry,
		std::span<const PickObjectTransform> object_transforms)
{
  const bool replace_selection = operation == SelectionEditOperation::Replace;
  const bool remove_selection_value =
      operation == SelectionEditOperation::Remove;
  apply_editor_surface_pick_policy(pick_options);
  if (objects.empty()) {
    if (replace_selection) {
      clear_selection(objects, active_object_id);
    }
    return false;
  }
  mode = SelectionMode::Vertex;
  int hit_index = -1;
  quader_poly::PickResult hit;
  quader_poly::PickOptions options = pick_options;
  options.mode = quader_poly::SelectionMode::Vertex;
  bool component_hit = false;
  if (const std::optional<SourceWireComponentPick> source_wire_hit =
          pick_active_source_wire_component(objects, SelectionMode::Vertex, ray,
                                            options, -1,
                                            remove_selection_value,
                                            object_bounds,
                                            face_geometry,
                                            object_transforms)) {
    hit_index = source_wire_hit->object_index;
    hit = source_wire_hit->hit;
    component_hit = true;
  }
  if ((!component_hit &&
       !pick_nearest_object(objects, ray, options, hit_index, hit,
                            object_bounds, face_geometry, -1,
                            object_transforms)) ||
      hit.kind != quader_poly::ElementKind::Vertex) {
    if (replace_selection) {
      clear_selection(objects, active_object_id);
    }
    return false;
  }
        for (int index = 0; index < static_cast<int>(objects.size()); ++index) {
		if (index != hit_index && replace_selection) {
			objects[index].selected = false;
			objects[index].component_edit_target = false;
			objects[index].selection = quader_poly::Selection();
		}
	}

	PolygonObject& object = objects[hit_index];
	const bool was_edit_target = object.selected || object.id == active_object_id;
        quader_poly::document_internal::DocumentPickFacts pick_facts(
            object.document, options);
        object.selection.mode = quader_poly::SelectionMode::Vertex;
        if (replace_selection) {
		object.selection.clear();
                object.selection.mode = quader_poly::SelectionMode::Vertex;
        }
	if (remove_selection_value) {
		object.selection.vertices.clear();
	} else {
		for (const quader_poly::Vertex& vertex : object.document.vertices) {
			if (pick_facts.vertex_can_be_picked(vertex.id) &&
					!quader_poly::selection_contains(object.selection, vertex.id)) {
				object.selection.vertices.push_back(vertex.id);
			}
		}
	}
	object.selection.has_active = !object.selection.vertices.empty();
        object.selection.active_kind = quader_poly::ElementKind::Vertex;
        object.selection.active_vertex = object.selection.vertices.empty()
                                             ? quader_poly::kInvalidElementId
                                             : object.selection.vertices.back();
        if (object.selection.vertices.empty()) {
		if (remove_selection_value && was_edit_target) {
			keep_component_edit_target(object, active_object_id);
		} else {
			object.selected = false;
			object.component_edit_target = false;
			normalize_active_object_id(objects, active_object_id);
		}
		return true;
	}
	object.selected = true;
	object.component_edit_target = true;
	active_object_id = object.id;
	return true;
}

namespace {

bool session_has_component_selection(const EditorSession& session, SelectionMode mode)
{
  if (mode == SelectionMode::Object) {
    return false;
  }
        for (const PolygonObject& object : session.objects()) {
		if (!object.selected) {
			continue;
		}
                if (mode == SelectionMode::Vertex &&
                    !object.selection.vertices.empty()) {
                  return true;
                }
                if (mode == SelectionMode::Edge &&
                    !object.selection.edges.empty()) {
                  return true;
                }
                if (mode == SelectionMode::Face &&
                    !object.selection.faces.empty()) {
                  return true;
                }
        }
	return false;
}

bool pick_selection_request_hits_target(
		const std::vector<PolygonObject>& picking_objects,
		SelectionMode mode,
		const quader_poly::Ray& ray,
	const quader_poly::PickOptions& pick_options,
	std::span<const PickObjectBounds> object_bounds,
	std::span<const PickObjectFaceGeometry> face_geometry,
	std::span<const PickObjectTransform> object_transforms)
{
	const SelectionHoverUpdateResult hovered =
			update_selection_hover(
					picking_objects,
					mode,
					{},
					ray,
					false,
					pick_options,
					object_bounds,
					face_geometry, false, object_transforms);
	return hovered.hover.object_id > 0;
}

bool apply_picked_selection_to_session(
		EditorSession& session,
		const std::vector<PolygonObject>& picking_objects,
		SelectionMode mode,
		int active_object_id)
{
	SessionState& state = session.state();
	bool changed = state.selection_mode != mode || state.active_object_id != active_object_id;
	for (PolygonObject& object : state.objects) {
		const auto picked = std::find_if(
				picking_objects.begin(),
				picking_objects.end(),
				[&](const PolygonObject& candidate) {
					return candidate.id == object.id;
				});
		const bool next_selected = picked != picking_objects.end() && picked->selected;
		const quader_poly::Selection next_selection =
				picked != picking_objects.end() ? picked->selection : quader_poly::Selection{};
		changed = changed ||
				object.selected != next_selected ||
				object.component_edit_target !=
				    (picked != picking_objects.end() &&
				     picked->component_edit_target) ||
				!polygon_selection_matches(object.selection, next_selection);
		object.selected = next_selected;
		object.component_edit_target =
				picked != picking_objects.end() && picked->component_edit_target;
		object.selection = next_selection;
	}
	if (!changed) {
		return false;
	}
	state.selection_mode = mode;
	state.active_object_id = active_object_id;
	normalize_active_object_id(state.objects, state.active_object_id);
	state.selection_revision =
		advance_session_revision(state.selection_revision);
	return true;
}

} // namespace

ApplyPickSelectionResult apply_pick_selection(
		EditorSession& session,
		std::vector<PolygonObject> picking_objects,
		const ApplyPickSelectionRequest& request)
{
	ApplyPickSelectionResult result;
	SelectionMode mode = session.selection_mode();
	int active_object_id = session.active_object_id();
	SelectionEditOperation operation = request.operation;
	const bool paint_stroke = request.click_count == 0;

	if (paint_stroke) {
          if (mode != SelectionMode::Object) {
            if (operation == SelectionEditOperation::Replace) {
              operation = SelectionEditOperation::Add;
            }
            result.hit =
                pick_select(picking_objects, mode, operation, request.ray,
                            active_object_id, request.pick_options, false,
                            request.object_bounds, request.face_geometry,
                            request.object_transforms);
            result.changed = apply_picked_selection_to_session(
                session, picking_objects, mode, active_object_id);
          }
                return result;
	}

	const bool edge_loop_select =
		request.edge_loop_select && mode == SelectionMode::Edge;
	const bool multi_click = request.click_count > 1;
	const bool single_component_pick = !multi_click && !edge_loop_select;
        if (request.preserve_existing_component_selection_on_hit &&
            operation == SelectionEditOperation::Replace &&
            single_component_pick &&
            session_has_component_selection(session, mode) &&
            pick_selection_request_hits_target(
                picking_objects, mode, request.ray, request.pick_options,
                request.object_bounds, request.face_geometry,
                request.object_transforms)) {
          result.applied = false;
          result.hit = true;
          return result;
        }

        if (request.preserve_selection_on_selected_hit &&
            operation == SelectionEditOperation::Replace &&
            single_component_pick) {
          const SelectionHoverUpdateResult hovered = update_selection_hover(
              picking_objects, mode, {}, request.ray, false,
              request.pick_options, request.object_bounds,
              request.face_geometry, false, request.object_transforms);
          bool hit_already_selected = false;
          if (mode == SelectionMode::Object && hovered.hover.object_id > 0) {
            hit_already_selected =
                std::any_of(picking_objects.begin(), picking_objects.end(),
                            [&](const PolygonObject &object) {
                              return object.id == hovered.hover.object_id &&
                                     object.selected;
                            });
          } else {
            hit_already_selected = selection_hover_target_matches_selection(
                picking_objects, mode, hovered.hover);
          }
          if (hit_already_selected) {
            result.applied = false;
            result.hit = true;
            return result;
          }
        }

        if (multi_click && mode == SelectionMode::Face) {
          result.hit = pick_select_all_faces(
              picking_objects, mode, operation, request.ray, active_object_id,
              request.pick_options, request.object_bounds,
              request.face_geometry, request.object_transforms);
        } else if ((multi_click || edge_loop_select) &&
                   mode == SelectionMode::Edge) {
          result.hit = pick_select_edge_loop(
              picking_objects, mode, operation, request.ray, active_object_id,
              request.pick_options, request.object_bounds,
              request.face_geometry, request.object_transforms);
        } else if (multi_click && mode == SelectionMode::Vertex) {
          result.hit = pick_select_all_vertices(
              picking_objects, mode, operation, request.ray, active_object_id,
              request.pick_options, request.object_bounds,
              request.face_geometry, request.object_transforms);
        } else {
          result.hit =
              pick_select(picking_objects, mode, operation, request.ray,
                          active_object_id, request.pick_options, true,
                          request.object_bounds, request.face_geometry,
                          request.object_transforms);
        }
        if (result.hit || !request.preserve_selection_on_miss) {
		result.changed = apply_picked_selection_to_session(session, picking_objects, mode, active_object_id);
	}
	return result;
}

} // namespace quader_editor_core

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "session_internal.hpp"

#include <diagnostics/profile.hpp>
#include <editor/modeling/runtime/session.hpp>
#include <editor/interactions/viewport_segment_projection.hpp>

#include <quader/modeling/neutral/geometry/bvh.hpp>
#include <quader/modeling/neutral/geometry/geometry.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_constants.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_picking_helpers.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
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

double affine_orientation_determinant(const quader::QMat4 &matrix) {
  const auto &m = matrix.values;
  return static_cast<double>(m[0]) *
             (static_cast<double>(m[5]) * m[10] -
              static_cast<double>(m[9]) * m[6]) -
         static_cast<double>(m[4]) *
             (static_cast<double>(m[1]) * m[10] -
              static_cast<double>(m[9]) * m[2]) +
         static_cast<double>(m[8]) *
             (static_cast<double>(m[1]) * m[6] -
              static_cast<double>(m[5]) * m[2]);
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
  quader::QVec3 world_normal = quader::normalize_or_zero(
      affine_transform_covector(object_transform->object_from_world,
                                local_normal));
  if (object_transform != nullptr &&
      pick_object_transform_reverses_face_winding(*object_transform)) {
    world_normal = world_normal * -1.0F;
  }
  return world_normal;
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

void apply_object_pick_selection(std::vector<PolygonObject> &objects,
                                 int hit_index,
                                 SelectionEditOperation operation,
                                 int &active_object_id) {
  if (operation == SelectionEditOperation::Replace) {
    clear_selection(objects, active_object_id);
  }
  PolygonObject &object = objects[hit_index];
  object.selected = operation != SelectionEditOperation::Remove;
  object.component_edit_target = false;
  if (object.selected) {
    active_object_id = object.id;
  } else {
    if (active_object_id == object.id) {
      active_object_id = selected_object_id(objects, -1);
    }
    normalize_active_object_id(objects, active_object_id);
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
		const PickObjectBounds* cached_bounds,
                const PickObjectTransform* object_transform = nullptr);
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
                const PickObjectTransform *object_transform =
                    cached_transform_for_object(object_transforms,
                                                object_index, object);
		if (!ray_intersects_object_pick_bounds(
					object,
					world_pick_ray,
					options,
					cached_bounds_for_object(object_bounds, object_index, object),
                                        object_transform)) {
			continue;
		}

                const quader_poly::Ray pick_ray =
                    object_space_pick_ray(world_pick_ray, object_transform);
                quader_poly::PickOptions local_options = options;
                local_options.reverse_face_winding =
                    local_options.reverse_face_winding !=
                    (object_transform != nullptr &&
                     pick_object_transform_reverses_face_winding(
                         *object_transform));
		const quader_geometry::QRay3<float> geometry_pick_ray =
                    to_geometry_ray(world_pick_ray);
                quader_poly::document_internal::DocumentPickFacts pick_facts(
                    object.document, local_options);
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
                        cached_geometry->triangle_span(), pick_ray,
                        local_options);
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
                    if (!local_options.pick_backfaces &&
                        pick_facts.vertex_has_incident_face(vertex.id) &&
                        !pick_facts.vertex_has_front_facing_pickable_face(
                            vertex.id, pick_ray)) {
                      continue;
                    }

                    const quader::QVec3 offset =
                        pick_world_position(vertex.position, object_transform) -
                        world_pick_ray.origin;
                    const float projected_ray_distance =
                        quader::dot(offset, world_pick_ray.direction);
                    if (projected_ray_distance < 0.0F) {
                      continue;
                    }

                    const quader_geometry::QClosestRayPoint<float> closest =
                        quader_geometry::closest_point_on_ray<float>(
                            to_geometry_vec3(pick_world_position(
                                vertex.position, object_transform)),
                            geometry_pick_ray, kTransformPointEpsilon);
                    if (!closest.valid) {
                      continue;
                    }
                    const float world_metric_distance =
                        std::sqrt(closest.distance_squared);
                    if (world_metric_distance > radius) {
                      continue;
                    }

                    quader_poly::PickResult hit;
                    hit.hit = true;
                    hit.kind = quader_poly::ElementKind::Vertex;
                    hit.vertex_id = vertex.id;
                    hit.distance = quader::dot(vertex.position - pick_ray.origin,
                                               pick_ray.direction);
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
			if (!local_options.pick_backfaces &&
				pick_facts.edge_has_incident_face(edge) &&
				!pick_facts.edge_has_front_facing_pickable_face(edge, pick_ray)) {
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
                                    float>({to_geometry_vec3(pick_world_position(
                                                a->position, object_transform)),
                                            to_geometry_vec3(pick_world_position(
                                                b->position, object_transform))},
                                           geometry_pick_ray,
                                           kTransformPointEpsilon);
                        if (!closest.valid || closest.ray_distance < 0.0F) {
				continue;
			}
			const float world_metric_distance =
                            std::sqrt(closest.distance_squared);
			if (world_metric_distance > radius) {
				continue;
			}

			quader_poly::PickResult hit;
			hit.hit = true;
                        hit.kind = quader_poly::ElementKind::Edge;
                        hit.edge = quader_poly::make_edge(edge.a, edge.b);
			hit.position = a->position + (b->position - a->position) *
                            closest.segment_factor;
                        hit.distance = quader::dot(hit.position - pick_ray.origin,
                                                   pick_ray.direction);
                        if (world_ray_distance(world_pick_ray, pick_world_position(
                                hit.position, object_transform)) < 0.0F) {
                          continue;
                        }
                        hit.face_id = pick_facts.edge_pick_context_face_id(
                            hit.edge, pick_ray, ensure_surface_hit());
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
		const PickObjectBounds* cached_bounds,
                const PickObjectTransform* object_transform)
{
  // Cached bounds are world-space. A local AABB cannot conservatively reject
  // a world ray when the caller supplies an occurrence transform without it.
  if (cached_bounds == nullptr && object_transform != nullptr) {
    return true;
  }
	const quader_geometry::QAabb3<float> bounds =
		cached_bounds == nullptr ? object_pick_bounds(object, options) : object_pick_bounds(*cached_bounds, options);
	return quader_geometry::intersect_ray_aabb(to_geometry_ray(ray), bounds).hit;
}

quader_poly::PickResult pick_component_with_world_metric(
    const PolygonObject &object, const quader_poly::Ray &world_ray,
    const quader_poly::Ray &local_ray,
    const quader_poly::PickOptions &options,
    const PickObjectTransform *object_transform,
    quader_poly::document_internal::DocumentPickFacts &facts,
    const std::optional<quader_poly::PickResult> *precomputed_surface_hit,
    quader_poly::document_internal::PickWorkCounters *work_counters = nullptr,
    const PickScreenContext *screen_context = nullptr,
    const std::function<bool(const quader_poly::PickResult &,
                             const quader_poly::Ray &)> &screen_visible = {},
    float *metric_distance_out = nullptr,
    float *metric_depth_out = nullptr) {
  const quader_poly::Ray normalized_world_ray{
      world_ray.origin, quader::normalize_or_zero(world_ray.direction)};
  if (quader::length_squared(normalized_world_ray.direction) <=
      quader_poly::document_internal::kEpsilon) {
    return {};
  }
  std::optional<quader_poly::PickResult> computed_surface_hit;
  if (screen_context == nullptr && precomputed_surface_hit == nullptr) {
    const auto surface = facts.surface_hit(local_ray);
    if (surface.hit) {
      computed_surface_hit = surface;
    }
    precomputed_surface_hit = &computed_surface_hit;
  }
  const auto geometry_ray = to_geometry_ray(normalized_world_ray);
  const double radius = screen_context != nullptr
      ? (options.mode == quader_poly::SelectionMode::Vertex
             ? screen_context->vertex_radius_pixels
             : screen_context->edge_radius_pixels)
      : (options.mode == quader_poly::SelectionMode::Vertex
             ? options.vertex_radius : options.edge_radius);
  if (!std::isfinite(radius) || radius < 0.0) {
    return {};
  }
  quader_poly::PickResult best;
  float best_pick_distance = std::numeric_limits<float>::max();
  float best_world_depth = std::numeric_limits<float>::max();
  bool best_front_facing = false;
  quader_poly::Ray best_local_ray = local_ray;
  const auto consider = [&](quader_poly::PickResult candidate,
                            float pick_distance,
                            quader::QVec2 screen_position = {}) {
    const quader::QVec3 world_position =
        pick_world_position(candidate.position, object_transform);
    if (!std::isfinite(pick_distance) || pick_distance > radius) {
      return;
    }
    const quader_poly::Ray candidate_ray = screen_context != nullptr
        ? ray_from_viewport_screen_point(screen_context->camera, screen_position)
        : normalized_world_ray;
    const float world_depth = world_ray_distance(candidate_ray, world_position);
    if (!std::isfinite(world_depth) || world_depth < 0.0F) {
      return;
    }
    const quader_poly::Ray candidate_local_ray = screen_context != nullptr
        ? object_space_pick_ray(candidate_ray, object_transform) : local_ray;
    const bool vertex = candidate.kind == quader_poly::ElementKind::Vertex;
    const bool front_facing = vertex
        ? facts.vertex_has_front_facing_pickable_face(candidate.vertex_id,
                                                       candidate_local_ray)
        : facts.edge_has_front_facing_pickable_face(candidate.edge,
                                                     candidate_local_ray);
    const bool has_face = vertex
        ? facts.vertex_has_incident_face(candidate.vertex_id)
        : facts.edge_has_incident_face(candidate.edge);
    if (!options.pick_backfaces && !front_facing && has_face) {
      return;
    }
    candidate.distance = quader::dot(
        candidate.position - candidate_local_ray.origin,
        candidate_local_ray.direction);
    constexpr float kMetricEpsilon = quader_poly::document_internal::kEpsilon;
    if (best.hit) {
      if (pick_distance > best_pick_distance + kMetricEpsilon) {
        return;
      }
      if (std::abs(pick_distance - best_pick_distance) <= kMetricEpsilon) {
        if (options.prefer_front_facing_faces &&
            front_facing != best_front_facing) {
          if (!front_facing) {
            return;
          }
        } else if (world_depth >= best_world_depth) {
          return;
        }
      }
    }
    // Only an already-visible winner can reject a later candidate. Keep the
    // original ranking and iteration order before the expensive visibility ray.
    if (screen_context != nullptr) {
      if (screen_visible && !screen_visible(candidate, candidate_ray)) {
        return;
      }
    } else if (source_wire_component_is_occluded_by_object_faces(
                   object, *precomputed_surface_hit, candidate)) {
      return;
    }
    best = candidate;
    best_pick_distance = pick_distance;
    best_world_depth = world_depth;
    best_front_facing = front_facing;
    best_local_ray = candidate_local_ray;
  };
  const auto screen_distance = [&](quader::QVec2 point) {
    const quader::QVec2 delta = point - screen_context->screen_position;
    return std::sqrt(delta.x * delta.x + delta.y * delta.y);
  };

  if (options.mode == quader_poly::SelectionMode::Vertex) {
    for (const quader_poly::Vertex &vertex : object.document.vertices) {
      if (work_counters != nullptr) {
        ++work_counters->component_candidates_tested;
      }
      if (!facts.vertex_can_be_picked(vertex.id)) {
        continue;
      }
      const auto world_position =
          pick_world_position(vertex.position, object_transform);
      const quader_poly::PickResult candidate{
          .hit = true, .kind = quader_poly::ElementKind::Vertex,
          .vertex_id = vertex.id, .position = vertex.position};
      if (screen_context != nullptr) {
        const auto projected = quader::project_visible_viewport_segment(
            screen_context->camera, world_position, world_position,
            screen_context->near_plane, screen_context->far_plane, false);
        if (projected.has_value()) {
          consider(candidate, screen_distance(projected->start),
                   projected->start);
        }
      } else {
        const auto closest = quader_geometry::closest_point_on_ray<float>(
            to_geometry_vec3(world_position), geometry_ray,
            kTransformPointEpsilon);
        if (closest.valid) {
          consider(candidate, std::sqrt(closest.distance_squared));
        }
      }
    }
  } else {
    for (const quader_poly::Edge edge : facts.edges()) {
      if (work_counters != nullptr) {
        ++work_counters->component_candidates_tested;
      }
      if (!facts.edge_can_be_picked(edge)) {
        continue;
      }
      const auto *a = quader_poly::find_vertex(object.document, edge.a);
      const auto *b = quader_poly::find_vertex(object.document, edge.b);
      if (a == nullptr || b == nullptr) {
        continue;
      }
      const auto world_a = pick_world_position(a->position, object_transform);
      const auto world_b = pick_world_position(b->position, object_transform);
      quader_poly::PickResult candidate{
          .hit = true, .kind = quader_poly::ElementKind::Edge, .edge = edge};
      if (screen_context != nullptr) {
        const auto projected = quader::project_visible_viewport_segment(
            screen_context->camera, world_a, world_b,
            screen_context->near_plane, screen_context->far_plane, false);
        if (!projected.has_value()) {
          continue;
        }
        const double dx = static_cast<double>(projected->end.x) - projected->start.x;
        const double dy = static_cast<double>(projected->end.y) - projected->start.y;
        const double length_squared = dx * dx + dy * dy;
        const double screen_factor = length_squared > 0.0
            ? std::clamp(((static_cast<double>(screen_context->screen_position.x) - projected->start.x) * dx +
                          (static_cast<double>(screen_context->screen_position.y) - projected->start.y) * dy) /
                             length_squared, 0.0, 1.0)
            : 0.0;
        const quader::QVec2 closest{
            static_cast<float>(std::lerp(static_cast<double>(projected->start.x),
                                         projected->end.x, screen_factor)),
            static_cast<float>(std::lerp(static_cast<double>(projected->start.y),
                                         projected->end.y, screen_factor))};
        const double clipped_factor =
            screen_context->camera.projection == CameraProjection::Orthographic
                ? screen_factor
                : screen_factor * projected->start_depth /
                      std::lerp(projected->end_depth, projected->start_depth,
                                screen_factor);
        const float world_factor = static_cast<float>(std::lerp(
            projected->start_parameter, projected->end_parameter, clipped_factor));
        candidate.position = a->position + (b->position - a->position) * world_factor;
        consider(candidate, screen_distance(closest), closest);
      } else {
        // The segment factor must minimize world distance too; transforming a
        // local closest point is incorrect under nonuniform scales and shear.
        const auto closest = quader_geometry::closest_points_segment_ray<float>(
            {to_geometry_vec3(world_a), to_geometry_vec3(world_b)},
            geometry_ray, kTransformPointEpsilon);
        if (closest.valid) {
          candidate.position = a->position + (b->position - a->position) *
                                               closest.segment_factor;
          consider(candidate, std::sqrt(closest.distance_squared));
        }
      }
    }
  }
  if (best.hit && best.kind == quader_poly::ElementKind::Edge) {
    best.face_id = screen_context != nullptr
        ? facts.best_edge_context_face_id(best.edge, best_local_ray)
        : facts.edge_pick_context_face_id(best.edge, local_ray,
                                           *precomputed_surface_hit);
  }
  if (metric_distance_out != nullptr) {
    *metric_distance_out = best_pick_distance;
  }
  if (metric_depth_out != nullptr) {
    *metric_depth_out = best_world_depth;
  }
  return best;
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
  local_options.reverse_face_winding =
      local_options.reverse_face_winding !=
      (object_transform != nullptr &&
       pick_object_transform_reverses_face_winding(*object_transform));
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
  hit = object_transform != nullptr &&
                options.mode != quader_poly::SelectionMode::Face
            ? pick_component_with_world_metric(
                  object, world_ray, ray, options, object_transform,
                  pick_facts, precomputed_surface_hit, work_counters)
            : pick_facts.pick_element(ray, local_options.mode,
                                     precomputed_surface_hit);
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
        float hit_pick_distance = std::numeric_limits<float>::max();
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
					cached_bounds_for_object(object_bounds, index, objects[index]),
                                        cached_transform_for_object(object_transforms, index, objects[index]))) {
			continue;
		}
		const quader_poly::PickResult candidate = pick_object_element(
                    objects[index], index, ray, options, face_geometry,
                    object_transforms,
                    component_mode
                        ? &local_surface_hits[
                              static_cast<std::size_t>(index)]
                        : nullptr);
		const float candidate_pick_distance = candidate.hit
                    ? world_pick_distance(ray, pick_world_position(
                          candidate.position, cached_transform_for_object(
                              object_transforms, index, objects[index])))
                    : std::numeric_limits<float>::max();
		if (candidate.hit &&
				!component_pick_is_occluded_by_surface(objects, index, candidate, surface_hit) &&
				(hit_index < 0 || candidate.distance < hit.distance ||
                                 (std::abs(candidate.distance - hit.distance) <=
                                      kTransformPointEpsilon &&
                                  candidate_pick_distance < hit_pick_distance))) {
			hit_index = index;
			hit = candidate;
                        hit_pick_distance = candidate_pick_distance;
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
	return quader_poly::document_internal::DocumentPickFacts(document, options)
		.best_edge_context_face_id(edge, ray);
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

quader_poly::Edge component_loop_pointer_hint(
    const PolygonObject &object, const PickRequest &request,
    const quader_poly::PickResult &hit,
    const PickObjectTransform *object_transform) {
  quader_poly::Edge nearest;
  double nearest_distance = std::numeric_limits<double>::infinity();
  const auto consider = [&](quader_poly::Edge edge) {
    if (hit.kind == quader_poly::ElementKind::Vertex &&
        edge.a != hit.vertex_id && edge.b != hit.vertex_id) return;
    const auto *a = quader_poly::find_vertex(object.document, edge.a);
    const auto *b = quader_poly::find_vertex(object.document, edge.b);
    if (a == nullptr || b == nullptr) return;
    const auto world_a = pick_world_position(a->position, object_transform);
    const auto world_b = pick_world_position(b->position, object_transform);
    double distance = std::numeric_limits<double>::infinity();
    if (request.screen_context.has_value()) {
      const auto &screen = *request.screen_context;
      const auto segment = quader::project_visible_viewport_segment(
          screen.camera, world_a, world_b, screen.near_plane,
          screen.far_plane, false);
      if (!segment.has_value()) return;
      const auto delta = segment->end - segment->start;
      const auto offset = screen.screen_position - segment->start;
      const double squared_length = delta.x * delta.x + delta.y * delta.y;
      const double factor = squared_length > 0.0
          ? std::clamp((offset.x * delta.x + offset.y * delta.y) /
                           squared_length, 0.0, 1.0)
          : 0.0;
      const double x = offset.x - delta.x * factor;
      const double y = offset.y - delta.y * factor;
      distance = x * x + y * y;
    } else {
      const auto closest = quader_geometry::closest_points_segment_ray<float>(
          {to_geometry_vec3(world_a), to_geometry_vec3(world_b)},
          {to_geometry_vec3(request.ray.origin),
           to_geometry_vec3(request.ray.direction)},
          kTransformPointEpsilon);
      if (!closest.valid) return;
      distance = closest.distance_squared;
    }
    if (distance < nearest_distance ||
        (distance == nearest_distance &&
         std::pair{edge.a, edge.b} < std::pair{nearest.a, nearest.b})) {
      nearest_distance = distance;
      nearest = edge;
    }
  };
  for (const auto &face : object.document.faces) {
    if (hit.kind == quader_poly::ElementKind::Face && face.id != hit.face_id)
      continue;
    for (std::size_t index = 0; index < face.vertices.size(); ++index) {
      consider(quader_poly::make_edge(
          face.vertices[index], face.vertices[(index + 1) % face.vertices.size()]));
    }
  }
  return nearest;
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
      request.resolve_component_loop_hint &&
              hit.kind != quader_poly::ElementKind::Edge
          ? component_loop_pointer_hint(object, request, hit, object_transform)
          : hit.edge,
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

std::optional<quader_poly::PickResult> object_space_surface_cutoff(
    const quader_poly::Ray &world_ray,
    const PickObjectTransform *object_transform,
    std::optional<double> maximum_world_ray_distance) {
  if (!maximum_world_ray_distance.has_value() ||
      *maximum_world_ray_distance >
          static_cast<double>(std::numeric_limits<float>::max()) * 0.25) {
    return std::nullopt;
  }
  const quader::QVec3 world_direction =
      quader::normalize_or_zero(world_ray.direction);
  if (quader::length_squared(world_direction) <=
      quader_poly::document_internal::kEpsilon) {
    return std::nullopt;
  }
  const quader::QVec3 world_cutoff =
      world_ray.origin +
      world_direction * static_cast<float>(*maximum_world_ray_distance);
  const quader_poly::Ray local_ray =
      object_space_pick_ray(world_ray, object_transform);
  const quader::QVec3 local_cutoff =
      object_transform == nullptr
          ? world_cutoff
          : affine_transform_point(object_transform->object_from_world,
                                   world_cutoff);
  const float local_cutoff_distance =
      quader::dot(local_cutoff - local_ray.origin, local_ray.direction);
  if (!std::isfinite(local_cutoff_distance)) {
    return std::nullopt;
  }

  // DocumentPickFacts applies its component coplanarity tolerance after the
  // supplied surface distance. Compensate here so the caller's inclusive
  // world cutoff remains exact even under nonuniform occurrence transforms.
  return quader_poly::PickResult{
      .hit = true,
      .kind = quader_poly::ElementKind::Face,
      .face_id = quader_poly::kInvalidElementId,
      .distance =
          local_cutoff_distance -
          quader_poly::document_internal::kComponentPickOcclusionTolerance,
      .position = local_cutoff,
  };
}

bool pick_hit_depth_less(const PickHitRecord &left,
                         const PickHitRecord &right) {
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
}

std::optional<quader_poly::Ray> screen_bounded_ray(
    const PickScreenContext &screen, const quader_poly::Ray &ray) {
  const auto direction = quader::normalize_or_zero(ray.direction);
  const auto forward = quader::normalize_or_zero(
      screen.camera.transform.z_axis * -1.0F);
  const double depth_direction = quader::dot(direction, forward);
  const double origin_depth = quader::dot(
      ray.origin - screen.camera.transform.origin, forward);
  if (!std::isfinite(screen.near_plane) || !std::isfinite(screen.far_plane) ||
      screen.near_plane < 0.0 || screen.near_plane > screen.far_plane ||
      depth_direction <= 0.0 || !std::isfinite(depth_direction)) {
    return std::nullopt;
  }
  const double distance = std::max(
      0.0, (screen.near_plane - origin_depth) / depth_direction);
  if (origin_depth + distance * depth_direction > screen.far_plane) {
    return std::nullopt;
  }
  return quader_poly::Ray{
      ray.origin + direction * static_cast<float>(distance), direction};
}

/** Preserves every authored ray bit, including signed zero and NaN payloads. */
struct ScreenPickRayKey {
  std::array<std::uint32_t, 6> components;

  explicit ScreenPickRayKey(const quader_poly::Ray &ray)
      : components{std::bit_cast<std::uint32_t>(ray.origin.x),
                   std::bit_cast<std::uint32_t>(ray.origin.y),
                   std::bit_cast<std::uint32_t>(ray.origin.z),
                   std::bit_cast<std::uint32_t>(ray.direction.x),
                   std::bit_cast<std::uint32_t>(ray.direction.y),
                   std::bit_cast<std::uint32_t>(ray.direction.z)} {}

  [[nodiscard]] bool operator==(const ScreenPickRayKey &) const = default;
};

/** Hashes exact ray keys without quantizing neighboring component rays. */
struct ScreenPickRayHash {
  [[nodiscard]] std::size_t operator()(const ScreenPickRayKey &key) const noexcept {
    std::size_t result = 0u;
    for (const std::uint32_t component : key.components) {
      result ^= std::hash<std::uint32_t>{}(component) +
                static_cast<std::size_t>(0x9e3779b9u) +
                (result << 6u) + (result >> 2u);
    }
    return result;
  }
};

std::vector<PickHitRecord> pick_screen_hits(
    const std::vector<PolygonObject> &objects, const PickRequest &request,
    std::span<const PickObjectBounds> object_bounds,
    std::span<const PickObjectFaceGeometry> face_geometry,
    std::span<const PickObjectTransform> object_transforms) {
  const PickScreenContext &screen = *request.screen_context;
  if (!std::isfinite(screen.near_plane) || !std::isfinite(screen.far_plane) ||
      screen.near_plane < 0.0 || screen.near_plane > screen.far_plane) {
    return {};
  }
  /// Shares immutable topology/facing facts across all candidate visibility rays.
  struct ScreenObjectPick {
    int index = -1;
    const PickObjectTransform *transform = nullptr;
    const PickObjectBounds *bounds = nullptr;
    const PickObjectFaceGeometry *geometry = nullptr;
    quader_poly::PickOptions options;
    std::unique_ptr<quader_poly::document_internal::DocumentPickFacts> facts;
  };
  std::vector<ScreenObjectPick> prepared;
  prepared.reserve(objects.size());
  for (int index = 0; index < static_cast<int>(objects.size()); ++index) {
    if (request.target_object_id >= 0 &&
        objects[index].id != request.target_object_id) {
      continue;
    }
    const auto *transform =
        cached_transform_for_object(object_transforms, index, objects[index]);
    quader_poly::PickOptions local_options = request.options;
    local_options.reverse_face_winding =
        local_options.reverse_face_winding !=
        (transform != nullptr &&
         pick_object_transform_reverses_face_winding(*transform));
    prepared.push_back({.index = index, .transform = transform,
                        .bounds = cached_bounds_for_object(
                            object_bounds, index, objects[index]),
                        .geometry = cached_face_geometry_for_object(
                            face_geometry, index, objects[index]),
                        .options = local_options});
  }
  const auto ensure_facts = [&](ScreenObjectPick &context)
      -> quader_poly::document_internal::DocumentPickFacts & {
    if (context.facts == nullptr) {
      context.facts = std::make_unique<
          quader_poly::document_internal::DocumentPickFacts>(
          objects[context.index].document, context.options);
    }
    return *context.facts;
  };
  const auto surface_for_object = [&](ScreenObjectPick &context,
                                      const quader_poly::Ray &ray,
                                      const quader_poly::Ray &clipped_ray)
      -> std::optional<PickHitRecord> {
    if (request.screen_work_counters != nullptr) {
      ++request.screen_work_counters->visibility_object_tests;
    }
    const auto &object = objects[context.index];
    quader_poly::PickOptions surface_options = context.options;
    surface_options.mode = quader_poly::SelectionMode::Face;
    if (!ray_intersects_object_pick_bounds(
            object, clipped_ray, surface_options, context.bounds,
            context.transform)) {
      return std::nullopt;
    }
    const auto local_ray = object_space_pick_ray(clipped_ray, context.transform);
    quader_poly::PickResult hit;
    if (request.screen_work_counters != nullptr) {
      ++request.screen_work_counters->surface_geometry_queries;
    }
    if (context.geometry != nullptr) {
      hit = quader_poly::pick_face_geometry(context.geometry->triangle_span(),
                                            local_ray, surface_options);
    } else {
      hit = ensure_facts(context).surface_hit(local_ray);
    }
    if (!hit.hit) {
      return std::nullopt;
    }
    const auto world_position = pick_world_position(hit.position, context.transform);
    const auto forward = quader::normalize_or_zero(
        screen.camera.transform.z_axis * -1.0F);
    const double depth = quader::dot(
        world_position - screen.camera.transform.origin, forward);
    if (depth > screen.far_plane) {
      return std::nullopt;
    }
    const PickRequest surface_request{.ray = ray, .options = surface_options};
    return make_pick_hit_record(object, context.index, surface_request, hit,
                                context.transform);
  };
  const auto object_may_hit_cursor = [&](const ScreenObjectPick &context) {
    if (request.screen_work_counters != nullptr) {
      ++request.screen_work_counters->projected_object_bounds;
    }
    const auto *bounds = context.bounds;
    if (bounds == nullptr) {
      return true;
    }
    if (bounds->empty) {
      return false;
    }
    const auto forward = quader::normalize_or_zero(
        screen.camera.transform.z_axis * -1.0F);
    double min_depth = std::numeric_limits<double>::max();
    double max_depth = std::numeric_limits<double>::lowest();
    quader::QVec2 minimum{std::numeric_limits<float>::max(),
                         std::numeric_limits<float>::max()};
    quader::QVec2 maximum{std::numeric_limits<float>::lowest(),
                         std::numeric_limits<float>::lowest()};
    bool projection_incomplete = false;
    for (int corner = 0; corner < 8; ++corner) {
      const quader::QVec3 point{
          (corner & 1) != 0 ? bounds->max.x : bounds->min.x,
          (corner & 2) != 0 ? bounds->max.y : bounds->min.y,
          (corner & 4) != 0 ? bounds->max.z : bounds->min.z};
      const double depth = quader::dot(
          point - screen.camera.transform.origin, forward);
      min_depth = std::min(min_depth, depth);
      max_depth = std::max(max_depth, depth);
      quader::QVec2 projected;
      if (project_viewport_point(screen.camera, point, projected)) {
        minimum.x = std::min(minimum.x, projected.x);
        minimum.y = std::min(minimum.y, projected.y);
        maximum.x = std::max(maximum.x, projected.x);
        maximum.y = std::max(maximum.y, projected.y);
      } else {
        projection_incomplete = true;
      }
    }
    if (max_depth < screen.near_plane || min_depth > screen.far_plane) {
      return false;
    }
    // Near-plane crossings and unprojectable corners need clipped bounds;
    // retain them for the exact candidate and visibility queries.
    if (min_depth <= screen.near_plane || projection_incomplete) {
      return true;
    }
    const double radius = request.options.mode == quader_poly::SelectionMode::Vertex
        ? screen.vertex_radius_pixels : screen.edge_radius_pixels;
    return screen.screen_position.x >= minimum.x - radius &&
           screen.screen_position.x <= maximum.x + radius &&
           screen.screen_position.y >= minimum.y - radius &&
           screen.screen_position.y <= maximum.y + radius;
  };
  if (request.options.mode != quader_poly::SelectionMode::Face) {
    // Every component visibility ray lies inside this cursor-radius square.
    // Filter once so each candidate also avoids unrelated scene occluders.
    std::erase_if(prepared, [&](const ScreenObjectPick &context) {
      return !object_may_hit_cursor(context);
    });
  }
  quader_geometry::QBoundsBvh<float> visibility_bounds;
  quader_geometry::QBvhTraversalScratch visibility_scratch;
  std::vector<std::size_t> unbounded_indices;
  if (request.options.mode != quader_poly::SelectionMode::Face &&
      prepared.size() > 8u) {
    std::vector<quader_geometry::QAabb3<float>> bounds;
    std::vector<std::size_t> indices;
    bounds.reserve(prepared.size());
    indices.reserve(prepared.size());
    for (std::size_t index = 0u; index < prepared.size(); ++index) {
      const auto *cached_bounds = prepared[index].bounds;
      if (cached_bounds == nullptr) {
        unbounded_indices.push_back(index);
        continue;
      }
      const quader_geometry::QAabb3<float> bound{
          to_geometry_vec3(cached_bounds->min),
          to_geometry_vec3(cached_bounds->max)};
      if (!quader_geometry::aabb_is_valid(bound) ||
          !std::isfinite(bound.min.x) || !std::isfinite(bound.min.y) ||
          !std::isfinite(bound.min.z) || !std::isfinite(bound.max.x) ||
          !std::isfinite(bound.max.y) || !std::isfinite(bound.max.z)) {
        unbounded_indices.push_back(index);
        continue;
      }
      bounds.push_back(bound);
      indices.push_back(index);
    }
    (void)visibility_bounds.build(bounds, indices);
  }
  std::unordered_map<ScreenPickRayKey, std::optional<PickHitRecord>,
                     ScreenPickRayHash> surface_hits;
  std::vector<std::size_t> surface_candidates;
  const auto nearest_surface = [&](const quader_poly::Ray &ray)
      -> std::optional<PickHitRecord> {
    if (request.screen_work_counters != nullptr) {
      ++request.screen_work_counters->visibility_queries;
    }
    const ScreenPickRayKey key(ray);
    const auto [entry, inserted] = surface_hits.try_emplace(key);
    if (!inserted) {
      if (request.screen_work_counters != nullptr) {
        ++request.screen_work_counters->visibility_cache_hits;
      }
      return entry->second;
    }
    if (request.screen_work_counters != nullptr) {
      ++request.screen_work_counters->visibility_ray_queries;
    }
    const auto clipped_ray = screen_bounded_ray(screen, ray);
    if (!clipped_ray.has_value()) {
      return std::nullopt;
    }
    const auto consider_surface = [&](ScreenObjectPick &context) {
      const auto hit = surface_for_object(context, ray, *clipped_ray);
      if (hit.has_value() &&
          (!entry->second.has_value() ||
           pick_hit_depth_less(*hit, *entry->second))) {
        entry->second = hit;
      }
    };
    const auto finite_vector = [](quader::QVec3 value) {
      return std::isfinite(value.x) && std::isfinite(value.y) &&
             std::isfinite(value.z);
    };
    if (!visibility_bounds.empty() && finite_vector(clipped_ray->origin) &&
        finite_vector(clipped_ray->direction)) {
      surface_candidates.assign(unbounded_indices.begin(),
                                unbounded_indices.end());
      (void)visibility_bounds.for_each_ray_candidate(
          to_geometry_ray(*clipped_ray), {}, visibility_scratch,
          [&](std::size_t index, std::size_t,
              const quader_geometry::QRayAabbHit<float> &, float &) {
            surface_candidates.push_back(index);
            return true;
          });
      // The epsilon-based surface comparison is order-sensitive. The BVH only
      // excludes bounds misses; retain the original participant reduction order.
      std::sort(surface_candidates.begin(), surface_candidates.end());
      for (const std::size_t index : surface_candidates) {
        consider_surface(prepared[index]);
      }
    } else {
      for (auto &context : prepared) {
        consider_surface(context);
      }
    }
    return entry->second;
  };
  std::vector<PickHitRecord> hits;
  hits.reserve(prepared.size());
  for (auto &context : prepared) {
    if (request.options.mode == quader_poly::SelectionMode::Face) {
      if (const auto clipped_ray = screen_bounded_ray(screen, request.ray)) {
        if (auto hit = surface_for_object(context, request.ray, *clipped_ray)) {
          hits.push_back(std::move(*hit));
        }
      }
      continue;
    }
    const auto &object = objects[context.index];
    const auto local_ray = object_space_pick_ray(request.ray, context.transform);
    const auto visible = [&](const quader_poly::PickResult &candidate,
                             const quader_poly::Ray &candidate_ray) {
      const auto surface = nearest_surface(candidate_ray);
      quader_poly::PickResult world_hit = candidate;
      apply_world_pick_metrics(world_hit, candidate_ray, context.transform);
      return !component_pick_is_occluded_by_surface(
          objects, context.index, world_hit, surface);
    };
    float screen_distance = std::numeric_limits<float>::max();
    float candidate_depth = std::numeric_limits<float>::max();
    const auto hit = pick_component_with_world_metric(
        object, request.ray, local_ray, request.options, context.transform,
        ensure_facts(context), nullptr, nullptr, &screen, visible,
        &screen_distance, &candidate_depth);
    if (hit.hit) {
      auto record = make_pick_hit_record(object, context.index, request, hit,
                                         context.transform);
      record.pick_distance = screen_distance;
      record.ray_distance = candidate_depth;
      hits.push_back(std::move(record));
    }
  }
  std::sort(hits.begin(), hits.end(), pick_hit_depth_less);
  return hits;
}

std::optional<PickHitRecord> pick_mesh_boundary(
    const PolygonObject &object, int object_index, const PickRequest &request,
    const PickObjectTransform *object_transform) {
  quader_poly::PickOptions local_options = request.options;
  local_options.reverse_face_winding =
      local_options.reverse_face_winding !=
      (object_transform != nullptr &&
       pick_object_transform_reverses_face_winding(*object_transform));
  const quader_poly::Ray local_ray =
      object_space_pick_ray(request.ray, object_transform);
  quader_poly::document_internal::DocumentPickFacts facts(object.document,
                                                         local_options);
  const auto world_ray = to_geometry_ray(request.ray);
  const float radius_squared =
      request.options.edge_radius * request.options.edge_radius;
  std::optional<PickHitRecord> nearest;
  for (const quader_poly::Edge edge : facts.edges()) {
    if (!facts.edge_can_be_picked(edge) ||
        (!local_options.pick_backfaces && facts.edge_has_incident_face(edge) &&
         !facts.edge_has_front_facing_pickable_face(edge, local_ray, true))) {
      continue;
    }
    const auto *a = quader_poly::find_vertex(object.document, edge.a);
    const auto *b = quader_poly::find_vertex(object.document, edge.b);
    if (a == nullptr || b == nullptr) {
      continue;
    }
    // Measure the authored segment in world space. A conservative local
    // radius can otherwise choose the wrong edge under nonuniform scaling.
    const auto closest = quader_geometry::closest_points_segment_ray<float>(
        {to_geometry_vec3(pick_world_position(a->position, object_transform)),
         to_geometry_vec3(pick_world_position(b->position, object_transform))},
        world_ray, kTransformPointEpsilon);
    if (!closest.valid ||
        !std::isfinite(closest.distance_squared) ||
        closest.distance_squared > radius_squared) {
      continue;
    }
    const quader_poly::PickResult hit{
        .hit = true,
        .kind = quader_poly::ElementKind::Edge,
        .edge = edge,
        .position = a->position +
                    (b->position - a->position) * closest.segment_factor,
    };
    PickHitRecord record = make_pick_hit_record(
        object, object_index, request, hit, object_transform);
    if (!std::isfinite(record.ray_distance) || record.ray_distance < 0.0F) {
      continue;
    }
    if (!nearest.has_value() ||
        record.pick_distance < nearest->pick_distance - kTransformPointEpsilon ||
        (std::abs(record.pick_distance - nearest->pick_distance) <=
             kTransformPointEpsilon &&
         pick_hit_depth_less(record, *nearest))) {
      nearest = std::move(record);
    }
  }
  // This fallback is reached only after the authoritative surface picker
  // missed. Do not repeat a full face traversal; resolve context only for the
  // winning boundary edge from its incident faces.
  if (nearest.has_value()) {
    nearest->face_id =
        facts.best_edge_context_face_id(nearest->edge, local_ray);
  }
  return nearest;
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
					cached_bounds_for_object(object_bounds, index, objects[index]),
                                        cached_transform_for_object(object_transforms, index, objects[index]))) {
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

bool pick_object_transform_reverses_face_winding(
    const PickObjectTransform &transform) {
  const double determinant =
      affine_orientation_determinant(transform.world_from_object);
  return std::isfinite(determinant) && determinant < 0.0;
}

quader_poly::PickOptions selection_pick_options_from_radii(
    double vertex_radius, double edge_radius, bool pick_backfaces) {
  quader_poly::PickOptions options;
  options.vertex_radius = static_cast<float>(
      normalize_pick_radius(vertex_radius, kDefaultVertexPickRadius));
  options.edge_radius = static_cast<float>(
      normalize_pick_radius(edge_radius, kDefaultEdgePickRadius));
  options.pick_backfaces = pick_backfaces;
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
        if (request.screen_context.has_value()) {
          const auto hits = pick_screen_hits(objects, request, {}, {}, {});
          return hits.empty() ? std::nullopt
                              : std::optional<PickHitRecord>(hits.front());
        }
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
        if (request.screen_context.has_value()) {
          const auto hits = pick_screen_hits(objects, request, object_bounds,
                                             face_geometry, object_transforms);
          return hits.empty() ? std::nullopt
                              : std::optional<PickHitRecord>(hits.front());
        }
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
  if (request.screen_context.has_value()) {
    for (const SelectionMode mode : priority_modes) {
      PickRequest mode_request = request;
      mode_request.options.mode = mode == SelectionMode::Object
          ? quader_poly::SelectionMode::Face : component_selection_mode(mode);
      if (const auto hit = pick_nearest(objects, mode_request, object_bounds,
                                        face_geometry, object_transforms)) {
        return hit;
      }
    }
    return std::nullopt;
  }

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
            cached_bounds_for_object(object_bounds, index, object),
            cached_transform_for_object(object_transforms, index, object))) {
      continue;
    }

    const PickObjectTransform *object_transform =
        cached_transform_for_object(object_transforms, index, object);
    const quader_poly::Ray local_ray =
        object_space_pick_ray(request.ray, object_transform);
    quader_poly::PickOptions local_options = request.options;
    local_options.reverse_face_winding =
        local_options.reverse_face_winding !=
        (object_transform != nullptr &&
         pick_object_transform_reverses_face_winding(*object_transform));
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
        quader_poly::PickOptions component_options = request.options;
        component_options.mode = mode;
        local_hit = context.object_transform != nullptr
            ? pick_component_with_world_metric(
                  object, request.ray, context.local_ray, component_options,
                  context.object_transform, *context.facts,
                  &context.surface_hit, work_counters)
            : context.facts->pick_element(
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
        if (request.screen_context.has_value()) {
          return pick_screen_hits(objects, request, {}, {}, {});
        }
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
        std::sort(hits.begin(), hits.end(), pick_hit_depth_less);
        return hits;
}

std::optional<PickHitRecord> pick_all_with_surface_into(
		const std::vector<PolygonObject>& objects,
		const PickRequest& request,
		std::span<const PickObjectBounds> object_bounds,
		std::span<const PickObjectFaceGeometry> face_geometry,
		std::span<const PickObjectTransform> object_transforms,
		PickAllWithSurfaceScratch& scratch)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.pick_all.cached");
	if (request.screen_context.has_value()) {
          scratch.hits = pick_screen_hits(objects, request, object_bounds,
                                          face_geometry, object_transforms);
          scratch.local_surface_hits.clear();
          PickRequest surface_request = request;
          surface_request.options.mode = quader_poly::SelectionMode::Face;
          return pick_nearest(objects, surface_request, object_bounds,
                               face_geometry, object_transforms);
        }
	std::vector<PickHitRecord>& hits = scratch.hits;
	hits.clear();
	hits.reserve(objects.size());
        const bool component_mode =
            request.options.mode == quader_poly::SelectionMode::Vertex ||
            request.options.mode == quader_poly::SelectionMode::Edge;
        std::vector<std::optional<quader_poly::PickResult>>&
            local_surface_hits = scratch.local_surface_hits;
        if (component_mode) {
          local_surface_hits.resize(objects.size());
          std::fill(local_surface_hits.begin(), local_surface_hits.end(),
                    std::nullopt);
        } else {
          local_surface_hits.clear();
        }
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
					cached_bounds_for_object(object_bounds, index, objects[index]),
                                        cached_transform_for_object(object_transforms, index, objects[index]))) {
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
        std::sort(hits.begin(), hits.end(), pick_hit_depth_less);
        return surface_hit;
}

void pick_all_without_surface_into(
    const std::vector<PolygonObject> &objects, const PickRequest &request,
    std::span<const PickObjectBounds> object_bounds,
    std::span<const PickObjectTransform> object_transforms,
    std::optional<double> maximum_world_ray_distance,
    PickAllWithSurfaceScratch &scratch) {
  QDR_PROFILE_SCOPE("qdr_editor_core.pick_all.without_surface");
  std::vector<PickHitRecord> &hits = scratch.hits;
  hits.clear();
  hits.reserve(objects.size());
  scratch.local_surface_hits.clear();
  if (maximum_world_ray_distance.has_value() &&
      (!std::isfinite(*maximum_world_ray_distance) ||
       *maximum_world_ray_distance < 0.0)) {
    return;
  }
  for (int index = 0; index < static_cast<int>(objects.size()); ++index) {
    const PolygonObject &object = objects[static_cast<std::size_t>(index)];
    if (request.target_object_id >= 0 &&
        object.id != request.target_object_id) {
      continue;
    }
    if (!ray_intersects_object_pick_bounds(
            object, request.ray, request.options,
            cached_bounds_for_object(object_bounds, index, object),
            cached_transform_for_object(object_transforms, index, object))) {
      continue;
    }
    const PickObjectTransform *object_transform =
        cached_transform_for_object(object_transforms, index, object);
    const std::optional<quader_poly::PickResult> surface_cutoff =
        object_space_surface_cutoff(request.ray, object_transform,
                                    maximum_world_ray_distance);
    const quader_poly::PickResult hit = pick_object_element(
        object, index, request.ray, request.options, {}, object_transforms,
        &surface_cutoff);
    if (!hit.hit) {
      continue;
    }
    hits.push_back(make_pick_hit_record(
        object, index, request, hit,
        object_transform));
  }
  std::sort(hits.begin(), hits.end(), pick_hit_depth_less);
}

PickAllWithSurfaceResult pick_all_with_surface(
		const std::vector<PolygonObject>& objects,
		const PickRequest& request,
		std::span<const PickObjectBounds> object_bounds,
		std::span<const PickObjectFaceGeometry> face_geometry,
		std::span<const PickObjectTransform> object_transforms)
{
        PickAllWithSurfaceScratch scratch;
        std::optional<PickHitRecord> surface_hit = pick_all_with_surface_into(
            objects, request, object_bounds, face_geometry, object_transforms,
            scratch);
        return {
            .hits = std::move(scratch.hits),
            .nearest_surface_hit = std::move(surface_hit),
        };
}

std::vector<PickHitRecord> pick_all(
    const std::vector<PolygonObject>& objects,
    const PickRequest& request,
    std::span<const PickObjectBounds> object_bounds,
    std::span<const PickObjectFaceGeometry> face_geometry,
    std::span<const PickObjectTransform> object_transforms)
{
  return pick_all_with_surface(objects, request, object_bounds, face_geometry,
                               object_transforms)
      .hits;
}

std::vector<PickHitRecord> pick_mesh_depth_hits(
    const std::vector<PolygonObject> &objects, const PickRequest &request,
    std::span<const PickObjectBounds> object_bounds,
    std::span<const PickObjectFaceGeometry> face_geometry,
    std::span<const PickObjectTransform> object_transforms) {
  if (request.screen_context.has_value()) {
    const auto clipped = screen_bounded_ray(*request.screen_context, request.ray);
    if (!clipped.has_value()) {
      return {};
    }
    PickRequest clipped_request = request;
    clipped_request.ray = *clipped;
    clipped_request.screen_context.reset();
    auto hits = pick_mesh_depth_hits(objects, clipped_request, object_bounds,
                                     face_geometry, object_transforms);
    const auto &screen = *request.screen_context;
    const auto forward = quader::normalize_or_zero(
        screen.camera.transform.z_axis * -1.0F);
    std::erase_if(hits, [&](PickHitRecord &hit) {
      const double depth = quader::dot(
          hit.local_position - screen.camera.transform.origin, forward);
      hit.ray_distance = world_ray_distance(request.ray, hit.local_position);
      return depth < screen.near_plane || depth > screen.far_plane;
    });
    return hits;
  }

  QDR_PROFILE_SCOPE("qdr_editor_core.pick_mesh_depth_hits");
  PickRequest surface_request = request;
  surface_request.ray.direction =
      quader::normalize_or_zero(request.ray.direction);
  if (quader::length_squared(surface_request.ray.direction) <=
      kTransformPointEpsilon * kTransformPointEpsilon) {
    return {};
  }
  surface_request.options.mode = quader_poly::SelectionMode::Face;
  apply_editor_surface_pick_policy(surface_request.options);
  quader_poly::PickOptions bounds_options = surface_request.options;
  bounds_options.mode = quader_poly::SelectionMode::Edge;
  std::vector<PickHitRecord> hits;
  for (int index = 0; index < static_cast<int>(objects.size()); ++index) {
    const PolygonObject &object = objects[index];
    if (request.target_object_id >= 0 && object.id != request.target_object_id) {
      continue;
    }
    const auto *transform =
        cached_transform_for_object(object_transforms, index, object);
    const auto *bounds = cached_bounds_for_object(object_bounds, index, object);
    // Transformed objects need world bounds for a conservative radius test.
    // Without that cache, proceed to exact world-space boundary distances.
    if ((bounds != nullptr || transform == nullptr) &&
        !ray_intersects_object_pick_bounds(
            object, surface_request.ray, bounds_options, bounds)) {
      continue;
    }
    const auto surface = pick_object_element(
        object, index, surface_request.ray, surface_request.options,
        face_geometry, object_transforms);
    if (surface.hit) {
      hits.push_back(make_pick_hit_record(object, index, surface_request,
                                          surface, transform));
    } else if (const auto boundary =
                   pick_mesh_boundary(object, index, surface_request, transform)) {
      hits.push_back(*boundary);
    }
  }
  std::sort(hits.begin(), hits.end(), pick_hit_depth_less);
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

PickCycleStep advance_depth_pick_cycle(PickCycleState &state,
                                      std::string signature,
                                      int candidate_count,
                                      int active_candidate_index) {
  if ((!state.active || state.signature != signature) && candidate_count > 0) {
    state.active = true;
    state.signature = signature;
    // The caller supplies the ordinary surface target independently of the
    // previous selection. With only boundary hits, start at the first one.
    state.index = active_candidate_index >= 0 && active_candidate_index < candidate_count
                      ? active_candidate_index
                      : candidate_count - 1;
    state.screen_position = {};
  }
  return advance_pick_cycle(state, std::move(signature), candidate_count, {},
                            0.0F);
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
                next.face_id = hit.face_id;
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
          apply_object_pick_selection(objects, hit_index, operation,
                                      active_object_id);
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

bool pick_select_edge_traversal(
		std::vector<PolygonObject>& objects,
		SelectionMode& mode,
		SelectionEditOperation operation,
		EdgeSelectionTraversal traversal,
		const quader_poly::Ray& ray,
		int& active_object_id,
		quader_poly::PickOptions pick_options,
		std::span<const PickObjectBounds> object_bounds,
		std::span<const PickObjectFaceGeometry> face_geometry,
		std::span<const PickObjectTransform> object_transforms)
{
  const bool replace_selection = operation == SelectionEditOperation::Replace;
  const bool prefer_selected_hit =
      operation == SelectionEditOperation::Remove ||
      operation == SelectionEditOperation::Toggle;
  apply_editor_surface_pick_policy(pick_options);
  if (objects.empty()) {
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
                                            prefer_selected_hit,
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
    return false;
  }
        const PickObjectTransform *object_transform =
            cached_transform_for_object(object_transforms, hit_index,
                                        objects[hit_index]);
        const quader_poly::Ray local_ray =
            object_space_pick_ray(ray, object_transform);
        quader_poly::PickOptions context_options = options;
        context_options.reverse_face_winding =
            context_options.reverse_face_winding !=
            (object_transform != nullptr &&
             pick_object_transform_reverses_face_winding(*object_transform));
        const quader_poly::ElementId context_face_id =
            edge_loop_context_face_id(objects[hit_index].document, hit,
                                      local_ray, context_options);
        quader_poly::Document const& hit_document =
            objects[hit_index].document;
        std::vector<quader_poly::Edge> traversal_edges =
            traversal == EdgeSelectionTraversal::Ring
                ? quader_poly::edge_ring_edges(hit_document, hit.edge)
                : quader_poly::edge_loop_edges(hit_document, hit.edge,
                                               context_face_id);
	if (traversal_edges.empty()) {
		return false;
	}
	PolygonObject& object = objects[hit_index];
	quader_poly::document_internal::DocumentPickFacts pick_facts(
		object.document, options);
	std::erase_if(traversal_edges, [&](const quader_poly::Edge edge) {
		return !pick_facts.edge_can_be_picked(edge);
	});
	if (traversal_edges.empty()) {
		return false;
	}
	const bool target_selected =
		quader_poly::selection_contains(object.selection, hit.edge);
	const bool remove_selection_value =
		operation == SelectionEditOperation::Remove ||
		(operation == SelectionEditOperation::Toggle && target_selected);
	if (traversal == EdgeSelectionTraversal::Loop && replace_selection &&
		target_selected &&
		std::ranges::all_of(traversal_edges, [&](const quader_poly::Edge edge) {
			return quader_poly::selection_contains(object.selection, edge);
		})) {
		std::vector<quader_poly::Edge> boundary_edges =
			quader_poly::edge_boundary_component_edges(hit_document, hit.edge);
		std::erase_if(boundary_edges, [&](const quader_poly::Edge edge) {
			return !pick_facts.edge_can_be_picked(edge);
		});
		if (!boundary_edges.empty() &&
			!std::ranges::all_of(boundary_edges, [&](const quader_poly::Edge edge) {
				return quader_poly::selection_contains(object.selection, edge);
			})) {
			traversal_edges = std::move(boundary_edges);
		}
	}
	for (int index = 0; index < static_cast<int>(objects.size()); ++index) {
		if (index != hit_index && replace_selection) {
			objects[index].selected = false;
			objects[index].component_edit_target = false;
			objects[index].selection = quader_poly::Selection();
		}
	}

	const bool was_edit_target = object.selected || object.id == active_object_id;
        object.selection.mode = quader_poly::SelectionMode::Edge;
        if (replace_selection) {
		object.selection.clear();
                object.selection.mode = quader_poly::SelectionMode::Edge;
        }
	if (remove_selection_value) {
		for (const quader_poly::Edge& loop_edge : traversal_edges) {
			const quader_poly::Edge normalized = quader_poly::make_edge(loop_edge.a, loop_edge.b);
			std::erase_if(object.selection.edges, [normalized](quader_poly::Edge edge) {
				return quader_poly::make_edge(edge.a, edge.b) == normalized;
			});
		}
	} else {
		for (const quader_poly::Edge& loop_edge : traversal_edges) {
			if (!quader_poly::selection_contains(object.selection, loop_edge)) {
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
	return pick_select_edge_traversal(
		objects, mode, operation, EdgeSelectionTraversal::Loop, ray,
		active_object_id, pick_options, object_bounds, face_geometry,
		object_transforms);
}

bool pick_select_edge_ring(
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
	return pick_select_edge_traversal(
		objects, mode, operation, EdgeSelectionTraversal::Ring, ray,
		active_object_id, pick_options, object_bounds, face_geometry,
		object_transforms);
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

bool has_component_selection(
		std::span<const PolygonObject> objects,
		SelectionMode mode)
{
  if (mode == SelectionMode::Object) {
    return false;
  }
	for (const PolygonObject& object : objects) {
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

bool pick_select_component_traversal(
    std::vector<PolygonObject> &objects, SelectionMode mode,
    int &active_object_id, const ApplyPickSelectionRequest &request) {
  quader_poly::PickOptions options = request.pick_options;
  options.mode = component_selection_mode(mode);
  apply_editor_surface_pick_policy(options);
  const auto hit = pick_nearest(
      objects, {.ray = request.ray, .options = options,
                .resolve_component_loop_hint = true},
      request.object_bounds,
      request.face_geometry, request.object_transforms);
  if (!hit.has_value()) {
    return false;
  }
  PolygonObject &object = objects[hit->object_index];
  const quader_poly::Document &document = object.document;
  const quader_poly::Selection direction_selection =
      object.id == active_object_id ? object.selection : quader_poly::Selection{};
  const auto seed = quader_poly::component_loop_seed(
      document, {.hit = true, .kind = hit->kind, .vertex_id = hit->vertex_id,
                 .edge = hit->edge, .face_id = hit->face_id},
      direction_selection, hit->edge,
      request.component_traversal == ComponentSelectionTraversal::VertexRing);
  if (!seed.has_value()) return false;
  std::vector<quader_poly::ElementId> components;
  if (mode == SelectionMode::Vertex &&
      request.component_traversal == ComponentSelectionTraversal::VertexRing) {
    for (const auto edge : quader_poly::edge_ring_edges(document, *seed)) {
      components.push_back(edge.a);
      components.push_back(edge.b);
    }
  } else {
    components = mode == SelectionMode::Vertex
        ? quader_poly::vertex_loop_vertices(document, *seed, hit->face_id)
        : quader_poly::face_loop_faces(document, *seed, hit->face_id);
  }
  // Direction recovery may reverse enumeration while retaining the same band.
  // The picked component remains active independently of this canonical order.
  std::ranges::sort(components);
  components.erase(std::unique(components.begin(), components.end()), components.end());
  if (components.empty()) return false;
  if (mode == SelectionMode::Face &&
      std::ranges::find(components, hit->face_id) == components.end()) return false;

  const quader_poly::ElementId seed_component = mode == SelectionMode::Vertex
      ? hit->vertex_id
      : hit->face_id;
  const bool seed_has_selection = object.selected &&
      object.selection.mode == component_selection_mode(mode);
  const bool seed_selected = seed_has_selection && (mode == SelectionMode::Vertex
      ? quader_poly::selection_contains(object.selection, seed_component)
      : quader_poly::selection_contains_face(object.selection, seed_component));
  const bool remove_selection =
      request.operation == SelectionEditOperation::Remove ||
      (request.operation == SelectionEditOperation::Toggle && seed_selected);
  const bool was_edit_target = object.selected || object.id == active_object_id;
  if (request.operation == SelectionEditOperation::Replace) {
    for (PolygonObject &candidate : objects) {
      candidate.selected = false;
      candidate.component_edit_target = false;
      candidate.selection.clear();
    }
  }
  object.selection.mode = mode == SelectionMode::Vertex
      ? quader_poly::SelectionMode::Vertex : quader_poly::SelectionMode::Face;
  std::vector<quader_poly::ElementId> &selected = mode == SelectionMode::Vertex
      ? object.selection.vertices : object.selection.faces;
  for (const quader_poly::ElementId component : components) {
    if (remove_selection) {
      std::erase(selected, component);
    } else if (std::ranges::find(selected, component) == selected.end()) {
      selected.push_back(component);
    }
  }
  object.selection.has_active = !selected.empty();
  object.selection.active_kind = mode == SelectionMode::Vertex
      ? quader_poly::ElementKind::Vertex : quader_poly::ElementKind::Face;
  const quader_poly::ElementId active_component = selected.empty()
      ? quader_poly::kInvalidElementId
      : (remove_selection ? selected.back() : seed_component);
  if (mode == SelectionMode::Vertex) {
    object.selection.active_vertex = active_component;
  } else {
    object.selection.active_face = active_component;
  }
  if (selected.empty()) {
    if (remove_selection && was_edit_target) {
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

bool pick_select_connected_component(
    std::vector<PolygonObject> &objects, SelectionMode mode,
    int &active_object_id, const ApplyPickSelectionRequest &request) {
  const bool replace_selection =
      request.operation == SelectionEditOperation::Replace;
  const bool remove_selection =
      request.operation == SelectionEditOperation::Remove;
  const bool toggle_selection =
      request.operation == SelectionEditOperation::Toggle;
  quader_poly::PickOptions options = request.pick_options;
  options.mode = component_selection_mode(mode);
  apply_editor_surface_pick_policy(options);
  int hit_index = -1;
  quader_poly::PickResult hit;
  bool component_hit = false;
  if (const auto source_wire_hit = pick_active_source_wire_component(
          objects, mode, request.ray, options, -1,
          remove_selection || toggle_selection, request.object_bounds,
          request.face_geometry, request.object_transforms)) {
    hit_index = source_wire_hit->object_index;
    hit = source_wire_hit->hit;
    component_hit = true;
  }
  if (!component_hit) {
    component_hit = pick_nearest_object(
        objects, request.ray, options, hit_index, hit, request.object_bounds,
        request.face_geometry, -1, request.object_transforms);
  }
  const quader_poly::ElementKind expected_kind =
      mode == SelectionMode::Vertex
          ? quader_poly::ElementKind::Vertex
          : (mode == SelectionMode::Edge ? quader_poly::ElementKind::Edge
                                         : quader_poly::ElementKind::Face);
  if (!component_hit || hit.kind != expected_kind) {
    if (replace_selection && mode != SelectionMode::Edge) {
      clear_selection(objects, active_object_id);
    }
    return false;
  }
  PolygonObject &object = objects[hit_index];
  quader_poly::Selection connected =
      quader_poly::connected_component_selection(object.document, hit);
  if (options.has_hidden_material_slot) {
    if (mode == SelectionMode::Face) {
      std::unordered_set<quader_poly::ElementId> hidden_faces;
      for (const quader_poly::Face &face : object.document.faces) {
        if (options.hides_material_slot(face.material_slot)) {
          hidden_faces.insert(face.id);
        }
      }
      std::erase_if(connected.faces, [&](quader_poly::ElementId face) {
        return hidden_faces.contains(face);
      });
    } else {
      const quader_poly::document_internal::DocumentPickFacts pick_facts(
          object.document, options);
      std::erase_if(connected.vertices, [&](quader_poly::ElementId vertex) {
        return !pick_facts.vertex_can_be_picked(vertex);
      });
      std::erase_if(connected.edges, [&](quader_poly::Edge edge) {
        return !pick_facts.edge_can_be_picked(edge);
      });
    }
  }
  if (connected.empty()) {
    return false;
  }
  const bool was_edit_target = object.selected || object.id == active_object_id;
  if (replace_selection) {
    clear_selection(objects, active_object_id);
    object.selection = std::move(connected);
  } else {
    object.selection.mode = connected.mode;
    const auto edit_components = [&](auto &selected, const auto &components,
                                     const auto &key) {
      std::unordered_set<std::uint64_t> existing;
      existing.reserve(selected.size());
      for (const auto component : selected) {
        existing.insert(key(component));
      }
      if (remove_selection || toggle_selection) {
        std::unordered_set<std::uint64_t> affected;
        affected.reserve(components.size());
        for (const auto component : components) {
          affected.insert(key(component));
        }
        std::erase_if(selected, [&](const auto component) {
          return affected.contains(key(component));
        });
      }
      if (!remove_selection) {
        for (const auto component : components) {
          if (existing.insert(key(component)).second) {
            selected.push_back(component);
          }
        }
      }
    };
    const auto element_key = [](quader_poly::ElementId element) {
      return static_cast<std::uint64_t>(element);
    };
    edit_components(object.selection.vertices, connected.vertices, element_key);
    edit_components(object.selection.edges, connected.edges, component_edge_key);
    edit_components(object.selection.faces, connected.faces, element_key);
    object.selection.has_active = !object.selection.empty();
    object.selection.active_kind = hit.kind;
    if (!object.selection.vertices.empty()) {
      object.selection.active_vertex =
          !remove_selection &&
                  quader_poly::selection_contains(object.selection, hit.vertex_id)
              ? hit.vertex_id
              : object.selection.vertices.back();
    }
    if (!object.selection.edges.empty()) {
      object.selection.active_edge =
          !remove_selection &&
                  quader_poly::selection_contains(object.selection, hit.edge)
              ? quader_poly::make_edge(hit.edge.a, hit.edge.b)
              : object.selection.edges.back();
    }
    if (!object.selection.faces.empty()) {
      object.selection.active_face =
          !remove_selection &&
                  quader_poly::selection_contains_face(object.selection, hit.face_id)
              ? hit.face_id
              : object.selection.faces.back();
    }
  }
  if (object.selection.empty()) {
    object.selection.clear();
    object.selection.mode = options.mode;
    if ((remove_selection || toggle_selection) && was_edit_target) {
      keep_component_edit_target(object, active_object_id);
    } else {
      object.selected = false;
      object.component_edit_target = false;
      normalize_active_object_id(objects, active_object_id);
    }
  } else {
    object.selected = true;
    object.component_edit_target = true;
    if (remove_selection) {
      normalize_active_object_id(objects, active_object_id);
    } else {
      active_object_id = object.id;
    }
  }
  return true;
}

bool apply_picked_selection_to_projection(
		std::vector<PolygonObject>& selection_objects,
		SelectionMode& selection_mode,
		int& selection_active_object_id,
		const std::vector<PolygonObject>& picking_objects,
		SelectionMode mode,
		int active_object_id)
{
	bool changed = selection_mode != mode ||
		selection_active_object_id != active_object_id;
	for (PolygonObject& object : selection_objects) {
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
	selection_mode = mode;
	selection_active_object_id = active_object_id;
	normalize_active_object_id(selection_objects, selection_active_object_id);
	return true;
}

} // namespace

ApplyPickSelectionProjectionResult apply_pick_selection(
		std::vector<PolygonObject> selection_objects,
		SelectionMode selection_mode,
		int active_object_id,
		std::vector<PolygonObject> picking_objects,
		const ApplyPickSelectionRequest& request)
{
	ApplyPickSelectionProjectionResult projection{
		.objects = std::move(selection_objects),
		.selection_mode = selection_mode,
		.active_object_id = active_object_id,
	};
	ApplyPickSelectionResult& result = projection.result;
	SelectionMode mode = projection.selection_mode;
	int resolved_active_object_id = projection.active_object_id;
	SelectionEditOperation operation = request.operation;
	const bool paint_stroke = request.click_count == 0;

        if (request.cycle_depth && mode == SelectionMode::Object && !paint_stroke &&
            (operation == SelectionEditOperation::Replace ||
             operation == SelectionEditOperation::Add ||
             operation == SelectionEditOperation::Remove) &&
            request.edge_traversal == EdgeSelectionTraversal::None) {
          quader_poly::PickOptions options = request.pick_options;
          apply_editor_surface_pick_policy(options);
          options.mode = quader_poly::SelectionMode::Face;
          const std::vector<PickHitRecord> hits = pick_mesh_depth_hits(
              picking_objects,
              PickRequest{.ray = request.ray, .options = options},
              request.object_bounds, request.face_geometry,
              request.object_transforms);
          const std::vector<int> candidates =
              pick_cycle_candidate_indices(hits, false);
          PickCycleState local_cycle;
          PickCycleState &cycle = request.depth_cycle != nullptr
                                      ? *request.depth_cycle
                                      : local_cycle;
          if (candidates.empty()) {
            reset_pick_cycle(cycle);
            result.applied = false;
            return projection;
          }

          int ordinary_candidate = -1;
          std::string signature =
              "object:" + std::to_string(static_cast<int>(operation));
          for (int index = 0; index < static_cast<int>(candidates.size());
               ++index) {
            const PickHitRecord &hit = hits[candidates[index]];
            signature += "|" + std::to_string(hit.object_id);
            if (ordinary_candidate < 0 &&
                hit.kind == quader_poly::ElementKind::Face) {
              ordinary_candidate = index;
            }
          }
          const PickCycleStep step = advance_depth_pick_cycle(
              cycle, std::move(signature), static_cast<int>(candidates.size()),
              ordinary_candidate);
          const PickHitRecord &hit = hits[candidates[step.index]];
          apply_object_pick_selection(picking_objects, hit.object_index,
                                      operation, resolved_active_object_id);
          result.hit = true;
          result.changed = apply_picked_selection_to_projection(
              projection.objects, projection.selection_mode,
              projection.active_object_id, picking_objects, mode,
              resolved_active_object_id);
          return projection;
        }

	if (paint_stroke) {
          if (mode != SelectionMode::Object) {
            if (operation == SelectionEditOperation::Replace) {
              operation = SelectionEditOperation::Add;
            }
            result.hit =
                pick_select(picking_objects, mode, operation, request.ray,
                            resolved_active_object_id, request.pick_options,
                            false,
                            request.object_bounds, request.face_geometry,
                            request.object_transforms);
            result.changed = apply_picked_selection_to_projection(
                projection.objects, projection.selection_mode,
                projection.active_object_id, picking_objects, mode,
                resolved_active_object_id);
          }
		return projection;
	}

	const bool edge_traversal_select =
		request.edge_traversal != EdgeSelectionTraversal::None &&
		mode == SelectionMode::Edge;
	const bool component_traversal_select =
            (mode == SelectionMode::Vertex &&
             (request.component_traversal == ComponentSelectionTraversal::VertexLoop ||
              request.component_traversal == ComponentSelectionTraversal::VertexRing)) ||
            (mode == SelectionMode::Face &&
             request.component_traversal == ComponentSelectionTraversal::FaceLoop);
	const bool multi_click = request.click_count > 1;
	const bool single_component_pick =
            !multi_click && !edge_traversal_select && !component_traversal_select;
        if (request.preserve_existing_component_selection_on_hit &&
            operation == SelectionEditOperation::Replace &&
            single_component_pick &&
            has_component_selection(projection.objects, mode) &&
            pick_selection_request_hits_target(
                picking_objects, mode, request.ray, request.pick_options,
                request.object_bounds, request.face_geometry,
                request.object_transforms)) {
          result.applied = false;
          result.hit = true;
		  return projection;
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
            return projection;
          }
        }

        if (component_traversal_select) {
          result.hit = pick_select_component_traversal(
              picking_objects, mode, resolved_active_object_id, request);
        } else if (edge_traversal_select) {
          result.hit =
              request.edge_traversal == EdgeSelectionTraversal::Ring
                  ? pick_select_edge_ring(
                        picking_objects, mode, operation, request.ray,
                        resolved_active_object_id, request.pick_options,
                        request.object_bounds, request.face_geometry,
                        request.object_transforms)
                  : pick_select_edge_loop(
                        picking_objects, mode, operation, request.ray,
                        resolved_active_object_id, request.pick_options,
                        request.object_bounds, request.face_geometry,
                        request.object_transforms);
        } else if (multi_click && mode != SelectionMode::Object) {
          result.hit = pick_select_connected_component(
              picking_objects, mode, resolved_active_object_id, request);
        } else {
          result.hit =
              pick_select(picking_objects, mode, operation, request.ray,
                          resolved_active_object_id, request.pick_options, true,
                          request.object_bounds, request.face_geometry,
                          request.object_transforms);
        }
        if (result.hit || !request.preserve_selection_on_miss) {
		result.changed = apply_picked_selection_to_projection(
				projection.objects, projection.selection_mode,
				projection.active_object_id, picking_objects, mode,
				resolved_active_object_id);
	}
	return projection;
}

ApplyPickSelectionResult apply_pick_selection(
		EditorSession& session,
		std::vector<PolygonObject> picking_objects,
		const ApplyPickSelectionRequest& request)
{
	SessionState& state = session.state();
	ApplyPickSelectionProjectionResult projection = apply_pick_selection(
			state.objects, state.selection_mode, state.active_object_id,
			std::move(picking_objects), request);
	if (projection.result.changed) {
		state.objects = std::move(projection.objects);
		state.selection_mode = projection.selection_mode;
		state.active_object_id = projection.active_object_id;
		state.selection_revision = advance_session_revision(
				state.selection_revision);
	}
	return projection.result;
}

} // namespace quader_editor_core

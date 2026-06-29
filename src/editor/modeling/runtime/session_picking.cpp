////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "session_internal.hpp"

#include <diagnostics/profile.hpp>
#include <editor/modeling/runtime/session.hpp>

#include <mesh/geometry/geometry.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader_editor_core {
namespace {

constexpr double default_vertex_pick_radius = 0.12;
constexpr double default_edge_pick_radius = 0.10;
constexpr double min_pick_radius = 0.001;
constexpr double max_pick_radius = 2.0;

/**
 * Stores the active source-wire component pick used by component hover and selection.
 */
struct SourceWireComponentPick {
	int object_index = -1;
	quader_poly::PickResult hit;
	float pick_distance = std::numeric_limits<float>::max();
	bool selected_target = false;
};

void keep_component_edit_target(PolygonObject& object, int& active_object_id)
{
	object.selected = true;
	if (object.id > 0) {
		active_object_id = object.id;
	}
}

bool edge_visible_for_pick_expansion(
		const quader_poly::Document& document,
		quader_poly::Edge edge,
		const quader_poly::PickOptions& options)
{
	if (!options.has_hidden_material_slot) {
		return true;
	}
	bool has_incident_face = false;
	for (const quader_poly::Face& face : document.faces) {
		if (face.vertices.size() < 2) {
			continue;
		}
		bool uses_edge = false;
		for (std::size_t index = 0; index < face.vertices.size(); ++index) {
			const quader_poly::ElementId a = face.vertices[index];
			const quader_poly::ElementId b = face.vertices[(index + 1U) % face.vertices.size()];
			if ((a == edge.a && b == edge.b) || (a == edge.b && b == edge.a)) {
				uses_edge = true;
				break;
			}
		}
		if (!uses_edge) {
			continue;
		}
		has_incident_face = true;
		if (!options.hides_material_slot(face.material_slot)) {
			return true;
		}
	}
	return !has_incident_face;
}

bool vertex_visible_for_pick_expansion(
		const quader_poly::Document& document,
		quader_poly::ElementId vertex_id,
		const quader_poly::PickOptions& options)
{
	if (!options.has_hidden_material_slot) {
		return true;
	}
	bool has_incident_face = false;
	for (const quader_poly::Face& face : document.faces) {
		if (std::find(face.vertices.begin(), face.vertices.end(), vertex_id) == face.vertices.end()) {
			continue;
		}
		has_incident_face = true;
		if (!options.hides_material_slot(face.material_slot)) {
			return true;
		}
	}
	return !has_incident_face;
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
		case quader_poly::SelectionMode::vertex:
			return std::max(0.0F, options.vertex_radius);
		case quader_poly::SelectionMode::edge:
			return std::max(0.0F, options.edge_radius);
		case quader_poly::SelectionMode::face:
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

bool object_draws_active_source_wire(const PolygonObject& object, int candidate_object_id)
{
	return object.selected || (object.id > 0 && object.id == candidate_object_id);
}

bool source_wire_component_is_selected(const PolygonObject& object, const quader_poly::PickResult& hit)
{
	if (!object.selected) {
		return false;
	}
	if (hit.kind == quader_poly::ElementKind::vertex && object.selection.mode == quader_poly::SelectionMode::vertex) {
		return quader_poly::selection_contains(object.selection, hit.vertex_id);
	}
	if (hit.kind == quader_poly::ElementKind::edge && object.selection.mode == quader_poly::SelectionMode::edge) {
		return quader_poly::selection_contains(object.selection, hit.edge);
	}
	return false;
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
	if (hit.kind == quader_poly::ElementKind::vertex) {
		return source_wire_face_contains_vertex(face, hit.vertex_id);
	}
	if (hit.kind == quader_poly::ElementKind::edge) {
		return source_wire_face_contains_edge(face, hit.edge);
	}
	return true;
}

bool source_wire_component_is_occluded_by_object_faces(
		const PolygonObject& object,
		const quader_poly::Ray& ray,
		const quader_poly::PickOptions& options,
		const quader_poly::PickResult& hit)
{
	if (source_wire_component_is_selected(object, hit)) {
		return false;
	}
	quader_poly::PickOptions face_options = options;
	face_options.mode = quader_poly::SelectionMode::face;
	const quader_poly::PickResult surface_hit = quader_poly::pick_element(object.document, ray, face_options);
	if (!surface_hit.hit || hit.distance <= surface_hit.distance + 0.001F) {
		return false;
	}
	const quader_poly::Face* surface_face = quader_poly::find_face(object.document, surface_hit.face_id);
	return surface_face == nullptr || !source_wire_component_is_on_face(*surface_face, hit);
}

bool source_wire_candidate_is_better(
		const SourceWireComponentPick& candidate,
		const std::optional<SourceWireComponentPick>& best)
{
	if (!best.has_value()) {
		return true;
	}
	if (std::abs(candidate.pick_distance - best->pick_distance) > transform_point_epsilon) {
		return candidate.pick_distance < best->pick_distance;
	}
	if (std::abs(candidate.hit.distance - best->hit.distance) > transform_point_epsilon) {
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
		prefer_selected_target ? radius : std::max(transform_point_epsilon, radius * 0.25F);
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
		std::span<const PickObjectBounds> object_bounds = {})
{
	if (mode == SelectionMode::object || mode == SelectionMode::face) {
		return std::nullopt;
	}

	options.mode = component_selection_mode(mode);
	const float radius = pick_bounds_padding(options);
	if (radius <= 0.0F) {
		return std::nullopt;
	}

	const quader::QVec3 direction = quader::normalize_or_zero(ray.direction);
	if (quader::length_squared(direction) <= transform_point_epsilon * transform_point_epsilon) {
		return std::nullopt;
	}
	const quader_poly::Ray pick_ray { ray.origin, direction };
	const quader_geometry::QRay3<float> geometry_pick_ray = to_geometry_ray(pick_ray);
	std::optional<SourceWireComponentPick> best_selected;
	std::optional<SourceWireComponentPick> best_unselected;

	for (int object_index = 0; object_index < static_cast<int>(objects.size()); ++object_index) {
		const PolygonObject& object = objects[object_index];
		if (!object_draws_active_source_wire(object, candidate_object_id)) {
			continue;
		}
		if (!ray_intersects_object_pick_bounds(
					object,
					pick_ray,
					options,
					cached_bounds_for_object(object_bounds, object_index, object))) {
			continue;
		}

		if (options.mode == quader_poly::SelectionMode::vertex) {
			for (const quader_poly::Vertex& vertex : object.document.vertices) {
				if (!vertex_visible_for_pick_expansion(object.document, vertex.id, options)) {
					continue;
				}

				const quader::QVec3 offset = vertex.position - pick_ray.origin;
				const float projected_ray_distance = quader::dot(offset, pick_ray.direction);
				if (projected_ray_distance < 0.0F) {
					continue;
				}

				const quader_geometry::QClosestRayPoint<float> closest =
					quader_geometry::closest_point_on_ray<float>(
						to_geometry_vec3(vertex.position),
						geometry_pick_ray,
						transform_point_epsilon);
				if (!closest.valid) {
					continue;
				}
				const float pick_distance = std::sqrt(closest.distance_squared);
				if (pick_distance > radius) {
					continue;
				}

				quader_poly::PickResult hit;
				hit.hit = true;
				hit.kind = quader_poly::ElementKind::vertex;
				hit.vertex_id = vertex.id;
				hit.distance = closest.ray_distance;
				hit.position = vertex.position;
				if (source_wire_component_is_occluded_by_object_faces(object, pick_ray, options, hit)) {
					continue;
				}
				keep_source_wire_candidate(
					best_selected,
					best_unselected,
					{
						object_index,
						hit,
						pick_distance,
						source_wire_component_is_selected(object, hit),
					});
			}
			continue;
		}

		if (options.mode != quader_poly::SelectionMode::edge) {
			continue;
		}
		for (const quader_poly::Edge edge : quader_poly::document_edges(object.document)) {
			if (!edge_visible_for_pick_expansion(object.document, edge, options)) {
				continue;
			}
			const quader_poly::Vertex* a = quader_poly::find_vertex(object.document, edge.a);
			const quader_poly::Vertex* b = quader_poly::find_vertex(object.document, edge.b);
			if (a == nullptr || b == nullptr) {
				continue;
			}

			const quader_geometry::QSegmentRayClosestPoints<float> closest =
				quader_geometry::closest_points_segment_ray<float>(
					{ to_geometry_vec3(a->position), to_geometry_vec3(b->position) },
					geometry_pick_ray,
					transform_point_epsilon);
			if (!closest.valid || closest.ray_distance < 0.0F) {
				continue;
			}
			const float pick_distance = std::sqrt(closest.distance_squared);
			if (pick_distance > radius) {
				continue;
			}

			quader_poly::PickResult hit;
			hit.hit = true;
			hit.kind = quader_poly::ElementKind::edge;
			hit.edge = quader_poly::make_edge(edge.a, edge.b);
			hit.distance = closest.ray_distance;
			hit.position = {
				closest.segment_point.x,
				closest.segment_point.y,
				closest.segment_point.z,
			};
			if (source_wire_component_is_occluded_by_object_faces(object, pick_ray, options, hit)) {
				continue;
			}
			keep_source_wire_candidate(
				best_selected,
				best_unselected,
				{
					object_index,
					hit,
					pick_distance,
					source_wire_component_is_selected(object, hit),
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
		const quader_poly::Ray& ray,
		const quader_poly::PickOptions& options,
		std::span<const PickObjectFaceGeometry> face_geometry)
{
	if (options.mode == quader_poly::SelectionMode::face) {
		if (const PickObjectFaceGeometry* cached_geometry =
					cached_face_geometry_for_object(face_geometry, object_index, object)) {
			return quader_poly::pick_face_geometry(cached_geometry->triangles, ray, options);
		}
	}
	return quader_poly::pick_element(object.document, ray, options);
}

std::optional<PickHitRecord> nearest_surface_hit(
		const std::vector<PolygonObject>& objects,
		const quader_poly::Ray& ray,
		const quader_poly::PickOptions& options,
		std::span<const PickObjectBounds> object_bounds,
		std::span<const PickObjectFaceGeometry> face_geometry = {});
bool component_pick_is_occluded_by_surface(
		const std::vector<PolygonObject>& objects,
		int object_index,
		const quader_poly::PickResult& component_hit,
		const std::optional<PickHitRecord>& surface_hit);

double normalize_pick_radius(double value, double fallback)
{
	return std::clamp(std::isfinite(value) ? value : fallback, min_pick_radius, max_pick_radius);
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
		std::span<const PickObjectFaceGeometry> face_geometry = {})
{
	QDR_PROFILE_SCOPE("qdr_editor_core.pick_nearest_object");
	hit_index = -1;
	hit = quader_poly::PickResult();
	hit.distance = std::numeric_limits<float>::max();
	const std::optional<PickHitRecord> surface_hit =
			(options.mode == quader_poly::SelectionMode::vertex || options.mode == quader_poly::SelectionMode::edge) ?
			nearest_surface_hit(objects, ray, options, object_bounds, face_geometry) :
			std::optional<PickHitRecord> {};
	for (int index = 0; index < static_cast<int>(objects.size()); ++index) {
		if (!ray_intersects_object_pick_bounds(
					objects[index],
					ray,
					options,
					cached_bounds_for_object(object_bounds, index, objects[index]))) {
			continue;
		}
		const quader_poly::PickResult candidate = pick_object_element(objects[index], index, ray, options, face_geometry);
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

bool face_contains_vertex(const quader_poly::Face& face, quader_poly::ElementId vertex_id)
{
	return std::ranges::find(face.vertices, vertex_id) != face.vertices.end();
}

float pick_distance_from_ray(const quader_poly::Ray& ray, quader::QVec3 position)
{
	const quader_geometry::QRay3<float> geometry_ray = to_geometry_ray(ray);
	if (quader_geometry::length_squared(geometry_ray.direction) <= transform_point_epsilon * transform_point_epsilon) {
		return 0.0F;
	}
	const float distance_squared = quader_geometry::point_ray_distance_squared<float>(
		to_geometry_vec3(position),
		geometry_ray,
		transform_point_epsilon);
	return std::isfinite(distance_squared) ? std::sqrt(distance_squared) : 0.0F;
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
		case quader_poly::ElementKind::vertex:
			return averaged_vertex_normal(document, hit.vertex_id);
		case quader_poly::ElementKind::edge:
			return averaged_edge_normal(document, hit.edge);
		case quader_poly::ElementKind::face:
		default:
			if (const quader_poly::Face* face = quader_poly::find_face(document, hit.face_id)) {
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
		case quader_poly::ElementKind::vertex:
			signature += "/vertex:";
			signature += std::to_string(hit.vertex_id);
			break;
		case quader_poly::ElementKind::edge:
			signature += "/edge:";
			signature += std::to_string(hit.edge.a);
			signature += "-";
			signature += std::to_string(hit.edge.b);
			break;
		case quader_poly::ElementKind::face:
		default:
			signature += "/face:";
			signature += std::to_string(hit.face_id);
			break;
	}
	return signature;
}

PickHitRecord make_pick_hit_record(const PolygonObject& object, int object_index, const PickRequest& request, const quader_poly::PickResult& hit)
{
	return {
		true,
		object.id,
		object_index,
		object.material_id,
		hit.kind,
		hit.vertex_id,
		hit.edge,
		hit.face_id,
		hit.position,
		pick_hit_normal(object.document, hit),
		hit.distance,
		hit.kind == quader_poly::ElementKind::face ? 0.0F : pick_distance_from_ray(request.ray, hit.position),
		pick_hit_signature(object.id, hit),
	};
}

std::optional<PickHitRecord> nearest_surface_hit(
		const std::vector<PolygonObject>& objects,
		const quader_poly::Ray& ray,
		const quader_poly::PickOptions& options,
		std::span<const PickObjectBounds> object_bounds,
		std::span<const PickObjectFaceGeometry> face_geometry)
{
	quader_poly::PickOptions surface_options = options;
	surface_options.mode = quader_poly::SelectionMode::face;
	const PickRequest surface_request { ray, surface_options };
	std::optional<PickHitRecord> nearest;
	for (int index = 0; index < static_cast<int>(objects.size()); ++index) {
		if (!ray_intersects_object_pick_bounds(
					objects[index],
					ray,
					surface_options,
					cached_bounds_for_object(object_bounds, index, objects[index]))) {
			continue;
		}
		const quader_poly::PickResult surface_hit = pick_object_element(objects[index], index, ray, surface_options, face_geometry);
		if (!surface_hit.hit) {
			continue;
		}

		PickHitRecord record = make_pick_hit_record(objects[index], index, surface_request, surface_hit);
		if (!nearest.has_value() ||
				record.ray_distance < nearest->ray_distance ||
				(std::abs(record.ray_distance - nearest->ray_distance) <= transform_point_epsilon && record.object_index < nearest->object_index)) {
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
	if (!surface_hit.has_value() || component_hit.kind == quader_poly::ElementKind::face) {
		return false;
	}
	if (component_hit.distance <= surface_hit->ray_distance + 0.001F) {
		return false;
	}

	if (object_index == surface_hit->object_index && object_index >= 0 && object_index < static_cast<int>(objects.size())) {
		const quader_poly::Face* surface_face = quader_poly::find_face(objects[object_index].document, surface_hit->face_id);
		if (surface_face != nullptr) {
			if (component_hit.kind == quader_poly::ElementKind::vertex && face_contains_vertex(*surface_face, component_hit.vertex_id)) {
				return false;
			}
			if (component_hit.kind == quader_poly::ElementKind::edge && face_contains_edge(*surface_face, component_hit.edge)) {
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
	plan.preserve_selection = request.operation == SelectionEditOperation::add || request.operation == SelectionEditOperation::remove;
	plan.clear_with_handle_state_on_miss = request.handles_enabled;

	std::optional<int> target_index;
	if (request.operation == SelectionEditOperation::remove) {
		target_index = hit_set == PickHitSet::handle ?
				selected_handle_pick_hit_index(selection, hits) :
				selected_surface_pick_hit_index(selection, hits);
	} else if (request.operation == SelectionEditOperation::add) {
		target_index = hit_set == PickHitSet::handle ?
				front_unselected_handle_pick_hit_index(selection, hits) :
				front_unselected_surface_pick_hit_index(selection, hits);
	} else {
		plan.used_pick_cycle = true;
		target_index = replacement_pick_hit_index(
				state,
				hits,
				request.handles_enabled,
				request.can_pick_all,
				request.screen_position,
				request.max_screen_distance,
				plan.cycle);
	}

	if (!target_index.has_value() || target_index.value() < 0 || target_index.value() >= static_cast<int>(hits.size()) || !hits[target_index.value()].hit) {
		plan.has_target = false;
		plan.hit_set = PickHitSet::none;
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
	options.vertex_radius = static_cast<float>(normalize_pick_radius(vertex_radius, default_vertex_pick_radius));
	options.edge_radius = static_cast<float>(normalize_pick_radius(edge_radius, default_edge_pick_radius));
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
	const std::optional<PickHitRecord> surface_hit =
			objects.size() > 1U && (request.options.mode == quader_poly::SelectionMode::vertex || request.options.mode == quader_poly::SelectionMode::edge) ?
				nearest_surface_hit(objects, request.ray, request.options, {}) :
				std::optional<PickHitRecord> {};
	for (int index = 0; index < static_cast<int>(objects.size()); ++index) {
		if (!ray_intersects_object_pick_bounds(objects[index], request.ray, request.options, nullptr)) {
			continue;
		}
		const quader_poly::PickResult hit = quader_poly::pick_element(objects[index].document, request.ray, request.options);
		if (!hit.hit || component_pick_is_occluded_by_surface(objects, index, hit, surface_hit)) {
			continue;
		}
		PickHitRecord record = make_pick_hit_record(objects[index], index, request, hit);
		if (!nearest.has_value() ||
				record.ray_distance < nearest->ray_distance ||
				(std::abs(record.ray_distance - nearest->ray_distance) <= transform_point_epsilon && record.pick_distance < nearest->pick_distance) ||
				(std::abs(record.ray_distance - nearest->ray_distance) <= transform_point_epsilon &&
						std::abs(record.pick_distance - nearest->pick_distance) <= transform_point_epsilon &&
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
		std::span<const PickObjectFaceGeometry> face_geometry)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.pick_nearest.cached");
	int hit_index = -1;
	quader_poly::PickResult hit;
	if (!pick_nearest_object(objects, request.ray, request.options, hit_index, hit, object_bounds, face_geometry) || hit_index < 0) {
		return std::nullopt;
	}
	return make_pick_hit_record(objects[static_cast<std::size_t>(hit_index)], hit_index, request, hit);
}

std::vector<PickHitRecord> pick_all(const std::vector<PolygonObject>& objects, const PickRequest& request)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.pick_all");
	std::vector<PickHitRecord> hits;
	const std::optional<PickHitRecord> surface_hit =
			objects.size() > 1U && (request.options.mode == quader_poly::SelectionMode::vertex || request.options.mode == quader_poly::SelectionMode::edge) ?
				nearest_surface_hit(objects, request.ray, request.options, {}) :
				std::optional<PickHitRecord> {};
	for (int index = 0; index < static_cast<int>(objects.size()); ++index) {
		if (!ray_intersects_object_pick_bounds(objects[index], request.ray, request.options, nullptr)) {
			continue;
		}
		const quader_poly::PickResult hit = quader_poly::pick_element(objects[index].document, request.ray, request.options);
		if (hit.hit && !component_pick_is_occluded_by_surface(objects, index, hit, surface_hit)) {
			hits.push_back(make_pick_hit_record(objects[index], index, request, hit));
		}
	}
	std::sort(hits.begin(), hits.end(), [](const PickHitRecord& left, const PickHitRecord& right) {
		if (std::abs(left.ray_distance - right.ray_distance) > transform_point_epsilon) {
			return left.ray_distance < right.ray_distance;
		}
		if (std::abs(left.pick_distance - right.pick_distance) > transform_point_epsilon) {
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
		std::span<const PickObjectFaceGeometry> face_geometry)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.pick_all.cached");
	std::vector<PickHitRecord> hits;
	const std::optional<PickHitRecord> surface_hit =
			objects.size() > 1U && (request.options.mode == quader_poly::SelectionMode::vertex || request.options.mode == quader_poly::SelectionMode::edge) ?
				nearest_surface_hit(objects, request.ray, request.options, object_bounds, face_geometry) :
				std::optional<PickHitRecord> {};
	for (int index = 0; index < static_cast<int>(objects.size()); ++index) {
		if (!ray_intersects_object_pick_bounds(
					objects[index],
					request.ray,
					request.options,
					cached_bounds_for_object(object_bounds, index, objects[index]))) {
			continue;
		}
		const quader_poly::PickResult hit =
			pick_object_element(objects[index], index, request.ray, request.options, face_geometry);
		if (hit.hit && !component_pick_is_occluded_by_surface(objects, index, hit, surface_hit)) {
			hits.push_back(make_pick_hit_record(objects[index], index, request, hit));
		}
	}
	std::sort(hits.begin(), hits.end(), [](const PickHitRecord& left, const PickHitRecord& right) {
		if (std::abs(left.ray_distance - right.ray_distance) > transform_point_epsilon) {
			return left.ray_distance < right.ray_distance;
		}
		if (std::abs(left.pick_distance - right.pick_distance) > transform_point_epsilon) {
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
		case quader_poly::ElementKind::vertex:
			signature += "/vertex:";
			signature += std::to_string(hit.vertex_id);
			break;
		case quader_poly::ElementKind::edge:
			signature += "/edge:";
			signature += std::to_string(hit.edge.a);
			signature += "-";
			signature += std::to_string(hit.edge.b);
			break;
		case quader_poly::ElementKind::face:
		default:
			signature += "/face:";
			signature += std::to_string(hit.face_id);
			break;
	}
	return signature;
}

bool pick_hit_target_is_selected(const PickSelectionState& selection, const PickHitRecord& hit)
{
	if (!hit.hit || hit.object_id < 0) {
		return false;
	}
	if (selection.mode == SelectionMode::object) {
		return std::find(selection.selected_object_ids.begin(), selection.selected_object_ids.end(), hit.object_id) != selection.selected_object_ids.end();
	}
	if (selection.mode == SelectionMode::face) {
		if (hit.kind != quader_poly::ElementKind::face || hit.face_id == quader_poly::invalid_element_id) {
			return false;
		}
		return std::find_if(selection.selected_faces.begin(), selection.selected_faces.end(), [&hit](const PickSelectionGroup& group) {
			return group.object_id == hit.object_id &&
				group.kind == quader_poly::ElementKind::face &&
				group.face_id == hit.face_id;
		}) != selection.selected_faces.end();
	}
	if (selection.mode == SelectionMode::edge) {
		if (hit.kind != quader_poly::ElementKind::edge || hit.edge.a == quader_poly::invalid_element_id || hit.edge.b == quader_poly::invalid_element_id) {
			return false;
		}
		const quader_poly::Edge hit_edge = quader_poly::make_edge(hit.edge.a, hit.edge.b);
		return std::find_if(selection.selected_edges.begin(), selection.selected_edges.end(), [&hit, hit_edge](const PickSelectionGroup& group) {
			return group.object_id == hit.object_id &&
				group.kind == quader_poly::ElementKind::edge &&
				quader_poly::make_edge(group.edge.a, group.edge.b) == hit_edge;
		}) != selection.selected_edges.end();
	}
	if (hit.kind != quader_poly::ElementKind::vertex || hit.vertex_id == quader_poly::invalid_element_id) {
		return false;
	}
	return std::find_if(selection.selected_vertices.begin(), selection.selected_vertices.end(), [&hit](const PickSelectionGroup& group) {
		return group.object_id == hit.object_id &&
			group.kind == quader_poly::ElementKind::vertex &&
			group.vertex_id == hit.vertex_id;
	}) != selection.selected_vertices.end();
}

bool pick_surface_hit_target_is_selected(const PickSelectionState& selection, const PickHitRecord& hit)
{
	if (!hit.hit || hit.object_id < 0) {
		return false;
	}
	if (selection.mode == SelectionMode::face) {
		if (hit.face_id == quader_poly::invalid_element_id) {
			return false;
		}
		return std::find_if(selection.selected_faces.begin(), selection.selected_faces.end(), [&hit](const PickSelectionGroup& group) {
			return group.object_id == hit.object_id &&
				group.kind == quader_poly::ElementKind::face &&
				group.face_id == hit.face_id;
		}) != selection.selected_faces.end();
	}
	return std::find(selection.selected_object_ids.begin(), selection.selected_object_ids.end(), hit.object_id) != selection.selected_object_ids.end();
}

bool pick_handle_target_is_selected(const PickSelectionState& selection, const PickHitRecord& hit)
{
	if (!hit.hit || hit.object_id < 0) {
		return false;
	}
	if (hit.kind == quader_poly::ElementKind::face) {
		if (hit.face_id == quader_poly::invalid_element_id) {
			return false;
		}
		return std::find_if(selection.selected_faces.begin(), selection.selected_faces.end(), [&hit](const PickSelectionGroup& group) {
			return group.object_id == hit.object_id &&
				group.kind == quader_poly::ElementKind::face &&
				group.face_id == hit.face_id;
		}) != selection.selected_faces.end();
	}
	if (hit.kind == quader_poly::ElementKind::edge) {
		if (hit.edge.a == quader_poly::invalid_element_id || hit.edge.b == quader_poly::invalid_element_id) {
			return false;
		}
		const quader_poly::Edge hit_edge = quader_poly::make_edge(hit.edge.a, hit.edge.b);
		return std::find_if(selection.selected_edges.begin(), selection.selected_edges.end(), [&hit, hit_edge](const PickSelectionGroup& group) {
			return group.object_id == hit.object_id &&
				group.kind == quader_poly::ElementKind::edge &&
				quader_poly::make_edge(group.edge.a, group.edge.b) == hit_edge;
		}) != selection.selected_edges.end();
	}
	if (hit.vertex_id == quader_poly::invalid_element_id) {
		return false;
	}
	return std::find_if(selection.selected_vertices.begin(), selection.selected_vertices.end(), [&hit](const PickSelectionGroup& group) {
		return group.object_id == hit.object_id &&
			group.kind == quader_poly::ElementKind::vertex &&
			group.vertex_id == hit.vertex_id;
	}) != selection.selected_vertices.end();
}

std::optional<int> selected_pick_hit_index(const PickSelectionState& selection, const std::vector<PickHitRecord>& hits)
{
	for (int index = 0; index < static_cast<int>(hits.size()); ++index) {
		if (pick_hit_target_is_selected(selection, hits[index])) {
			return index;
		}
	}
	return std::nullopt;
}

std::optional<int> first_unselected_pick_hit_index(const PickSelectionState& selection, const std::vector<PickHitRecord>& hits)
{
	for (int index = 0; index < static_cast<int>(hits.size()); ++index) {
		const PickHitRecord& hit = hits[index];
		if (hit.hit && !pick_hit_target_is_selected(selection, hit)) {
			return index;
		}
	}
	return std::nullopt;
}

std::optional<int> selected_surface_pick_hit_index(const PickSelectionState& selection, const std::vector<PickHitRecord>& hits)
{
	for (int index = 0; index < static_cast<int>(hits.size()); ++index) {
		if (pick_surface_hit_target_is_selected(selection, hits[index])) {
			return index;
		}
	}
	return std::nullopt;
}

std::optional<int> front_unselected_surface_pick_hit_index(const PickSelectionState& selection, const std::vector<PickHitRecord>& hits)
{
	for (int index = 0; index < static_cast<int>(hits.size()); ++index) {
		const PickHitRecord& hit = hits[index];
		if (!hit.hit) {
			continue;
		}
		if (!pick_surface_hit_target_is_selected(selection, hit)) {
			return index;
		}
		return std::nullopt;
	}
	return std::nullopt;
}

std::optional<int> selected_handle_pick_hit_index(const PickSelectionState& selection, const std::vector<PickHitRecord>& hits)
{
	for (int index = 0; index < static_cast<int>(hits.size()); ++index) {
		if (pick_handle_target_is_selected(selection, hits[index])) {
			return index;
		}
	}
	return std::nullopt;
}

std::optional<int> front_unselected_handle_pick_hit_index(const PickSelectionState& selection, const std::vector<PickHitRecord>& hits)
{
	for (int index = 0; index < static_cast<int>(hits.size()); ++index) {
		const PickHitRecord& hit = hits[index];
		if (!hit.hit) {
			continue;
		}
		if (!pick_handle_target_is_selected(selection, hit)) {
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
		ClickPickPlan handle_plan = plan_click_pick_for_hits(state, selection, handle_hits, request, PickHitSet::handle);
		if (handle_plan.has_target) {
			return handle_plan;
		}
	}

	ClickPickPlan surface_plan = plan_click_pick_for_hits(state, selection, surface_hits, request, PickHitSet::surface);
	if (surface_plan.has_target) {
		return surface_plan;
	}

	ClickPickPlan miss_plan;
	miss_plan.preserve_selection = request.operation == SelectionEditOperation::add || request.operation == SelectionEditOperation::remove;
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
			target_index = hit_set == PickHitSet::handle ?
					selected_handle_pick_hit_index(selection, hits) :
					selected_surface_pick_hit_index(selection, hits);
			if (target_index.has_value() && request.additive && request.preserve_selected_additive_target) {
				const PickHitRecord& hit = hits[target_index.value()];
				plan.has_target = true;
				plan.keep_existing_selection = true;
				plan.hit_index = target_index.value();
				plan.preferred_object_id = hit.object_id;
				return plan;
			}
		}

		if (!target_index.has_value()) {
			target_index = hit_set == PickHitSet::handle ?
					front_unselected_handle_pick_hit_index(selection, hits) :
					front_unselected_surface_pick_hit_index(selection, hits);
		}
		if (!target_index.has_value() || target_index.value() < 0 || target_index.value() >= static_cast<int>(hits.size()) || !hits[target_index.value()].hit) {
			plan.has_target = false;
			plan.hit_set = PickHitSet::none;
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
		const DragSeedPickPlan handle_plan = plan_for_hits(handle_hits, PickHitSet::handle);
		if (handle_plan.has_target) {
			return handle_plan;
		}
	}

	const DragSeedPickPlan surface_plan = plan_for_hits(surface_hits, PickHitSet::surface);
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
		std::span<const PickObjectFaceGeometry> face_geometry)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.update_selection_hover");
	apply_editor_surface_pick_policy(pick_options);
	SelectionHoverState next;
	next.candidate_object_id = current_hover.candidate_object_id;

	if (mode == SelectionMode::object) {
		int hit_index = -1;
		quader_poly::PickResult hit;
		quader_poly::PickOptions options = pick_options;
		options.mode = quader_poly::SelectionMode::face;
		if (pick_nearest_object(objects, ray, options, hit_index, hit, object_bounds, face_geometry) && hit_index >= 0) {
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
						objects,
						mode,
						ray,
						options,
						current_hover.candidate_object_id,
						selected_modifier_pressed,
						object_bounds)) {
			hit_index = source_wire_hit->object_index;
			hit = source_wire_hit->hit;
			component_hover = true;
		} else {
			component_hover = pick_nearest_object(objects, ray, options, hit_index, hit, object_bounds, face_geometry);
		}
		if (!component_hover) {
			options.mode = quader_poly::SelectionMode::face;
			pick_nearest_object(objects, ray, options, hit_index, hit, object_bounds, face_geometry);
		}
		if (hit_index >= 0) {
			next.object_id = objects[hit_index].id;
			if (next.object_id > 0) {
				next.candidate_object_id = next.object_id;
			}
			if (component_hover) {
				switch (hit.kind) {
					case quader_poly::ElementKind::vertex:
						next.kind = SelectionHoverKind::vertex;
						next.vertex_id = hit.vertex_id;
						break;
					case quader_poly::ElementKind::edge:
						next.kind = SelectionHoverKind::edge;
						next.edge = hit.edge;
						break;
					case quader_poly::ElementKind::face:
						next.kind = SelectionHoverKind::face;
						next.face_id = hit.face_id;
						break;
				}
				next.selected_modifier_pressed = next.kind != SelectionHoverKind::none && selected_modifier_pressed;
			}
		}
	}

	const bool changed = current_hover.object_id != next.object_id ||
		current_hover.kind != next.kind ||
		current_hover.vertex_id != next.vertex_id ||
		current_hover.edge != next.edge ||
		current_hover.face_id != next.face_id ||
		current_hover.selected_modifier_pressed != next.selected_modifier_pressed ||
		current_hover.candidate_object_id != next.candidate_object_id;
	return { changed, next };
}

bool selection_hover_target_matches_selection(std::span<const PolygonObject> objects, SelectionMode mode, const SelectionHoverState& hover)
{
	if (mode == SelectionMode::object || hover.object_id <= 0 || hover.kind == SelectionHoverKind::none) {
		return false;
	}
	for (const PolygonObject& object : objects) {
		if (object.id != hover.object_id || !object.selected) {
			continue;
		}
		if (hover.kind == SelectionHoverKind::face && mode == SelectionMode::face) {
			return quader_poly::selection_contains_face(object.selection, hover.face_id);
		}
		if (hover.kind == SelectionHoverKind::edge && mode == SelectionMode::edge) {
			return quader_poly::selection_contains(object.selection, hover.edge);
		}
		if (hover.kind == SelectionHoverKind::vertex && mode == SelectionMode::vertex) {
			return quader_poly::selection_contains(object.selection, hover.vertex_id);
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
		std::span<const PickObjectFaceGeometry> face_geometry)
{
	const bool replace_selection = operation == SelectionEditOperation::replace;
	const bool add_selection_value = operation == SelectionEditOperation::add;
	const bool remove_selection_value = operation == SelectionEditOperation::remove;
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
	options.mode = mode == SelectionMode::object ? quader_poly::SelectionMode::face : component_selection_mode(mode);
	const bool component_mode = mode != SelectionMode::object;
	bool component_hit = false;
	if (component_mode) {
		if (const std::optional<SourceWireComponentPick> source_wire_hit =
					pick_active_source_wire_component(
						objects,
						mode,
						ray,
						options,
						-1,
						remove_selection_value)) {
			hit_index = source_wire_hit->object_index;
			hit = source_wire_hit->hit;
			component_hit = true;
		}
	}
	if (!component_hit) {
		component_hit = pick_nearest_object(objects, ray, options, hit_index, hit, object_bounds, face_geometry);
	}
	if (!component_hit && component_mode && allow_component_fallback) {
		options.mode = quader_poly::SelectionMode::face;
		pick_nearest_object(objects, ray, options, hit_index, hit, object_bounds, face_geometry);
	}

	if (hit_index < 0) {
		if (replace_selection) {
			clear_selection(objects, active_object_id);
		}
		return false;
	}

	if (mode == SelectionMode::object) {
		if (replace_selection) {
			clear_selection(objects, active_object_id);
		}
		PolygonObject& object = objects[hit_index];
		object.selected = !remove_selection_value;
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
			active_object_id = object.id;
		} else if (was_edit_target) {
			keep_component_edit_target(object, active_object_id);
		} else {
			object.selected = false;
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
		if (!remove_selection_value) {
			active_object_id = object.id;
		} else {
			normalize_active_object_id(objects, active_object_id);
		}
	} else if (remove_selection_value && was_edit_target) {
		keep_component_edit_target(object, active_object_id);
	} else {
		object.selected = false;
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
	const bool replace_selection = operation == SelectionEditOperation::replace;
	const bool add_selection_value = operation == SelectionEditOperation::add;
	const bool remove_selection_value = operation == SelectionEditOperation::remove;
	apply_editor_surface_pick_policy(pick_options);
	if (objects.empty()) {
		if (replace_selection) {
			clear_selection(objects, active_object_id);
		}
		return false;
	}
	mode = SelectionMode::face;
	int hit_index = -1;
	quader_poly::PickResult hit;
	quader_poly::PickOptions options = pick_options;
	options.mode = quader_poly::SelectionMode::face;
	if (!pick_nearest_object(objects, ray, options, hit_index, hit)) {
		if (replace_selection) {
			clear_selection(objects, active_object_id);
		}
		return false;
	}
	for (int index = 0; index < static_cast<int>(objects.size()); ++index) {
		if (index != hit_index && replace_selection) {
			objects[index].selected = false;
			objects[index].selection = quader_poly::Selection();
		}
	}
	PolygonObject& object = objects[hit_index];
	const bool was_edit_target = object.selected || object.id == active_object_id;
	object.selection.mode = quader_poly::SelectionMode::face;
	if (remove_selection_value) {
		quader_poly::remove_selection(object.selection, hit);
	} else if (add_selection_value) {
		quader_poly::add_selection(object.selection, hit);
	} else {
		quader_poly::select_only(object.selection, hit);
	}
	if (!object.selection.empty()) {
		object.selected = true;
		if (!remove_selection_value) {
			active_object_id = object.id;
		} else {
			normalize_active_object_id(objects, active_object_id);
		}
	} else if (remove_selection_value && was_edit_target) {
		keep_component_edit_target(object, active_object_id);
	} else {
		object.selected = false;
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
		std::span<const PickObjectFaceGeometry> face_geometry)
{
	const bool replace_selection = operation == SelectionEditOperation::replace;
	const bool remove_selection_value = operation == SelectionEditOperation::remove;
	apply_editor_surface_pick_policy(pick_options);
	if (objects.empty()) {
		if (replace_selection) {
			clear_selection(objects, active_object_id);
		}
		return false;
	}
	mode = SelectionMode::face;
	int hit_index = -1;
	quader_poly::PickResult hit;
	quader_poly::PickOptions options = pick_options;
	options.mode = quader_poly::SelectionMode::face;
	if (!pick_nearest_object(objects, ray, options, hit_index, hit, object_bounds, face_geometry)) {
		if (replace_selection) {
			clear_selection(objects, active_object_id);
		}
		return false;
	}
	for (int index = 0; index < static_cast<int>(objects.size()); ++index) {
		if (index != hit_index && replace_selection) {
			objects[index].selected = false;
			objects[index].selection = quader_poly::Selection();
		}
	}
	PolygonObject& object = objects[hit_index];
	if (object.document.faces.empty()) {
		return false;
	}
	const bool was_edit_target = object.selected || object.id == active_object_id;
	object.selection.mode = quader_poly::SelectionMode::face;
	if (remove_selection_value) {
		object.selection.faces.clear();
		object.selection.clear();
		if (was_edit_target) {
			keep_component_edit_target(object, active_object_id);
		} else {
			object.selected = false;
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
		normalize_active_object_id(objects, active_object_id);
		return was_edit_target;
	}
	object.selection.has_active = !object.selection.faces.empty();
	object.selection.active_kind = quader_poly::ElementKind::face;
	object.selection.active_face = object.selection.faces.empty() ? quader_poly::invalid_element_id : object.selection.faces.back();
	object.selected = true;
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
		std::span<const PickObjectFaceGeometry> face_geometry)
{
	const bool replace_selection = operation == SelectionEditOperation::replace;
	const bool remove_selection_value = operation == SelectionEditOperation::remove;
	apply_editor_surface_pick_policy(pick_options);
	if (objects.empty()) {
		if (replace_selection) {
			clear_selection(objects, active_object_id);
		}
		return false;
	}
	mode = SelectionMode::edge;
	int hit_index = -1;
	quader_poly::PickResult hit;
	quader_poly::PickOptions options = pick_options;
	options.mode = quader_poly::SelectionMode::edge;
	bool component_hit = false;
	if (const std::optional<SourceWireComponentPick> source_wire_hit =
				pick_active_source_wire_component(
					objects,
					SelectionMode::edge,
					ray,
					options,
					-1,
					remove_selection_value)) {
		hit_index = source_wire_hit->object_index;
		hit = source_wire_hit->hit;
		component_hit = true;
	}
	if ((!component_hit && !pick_nearest_object(objects, ray, options, hit_index, hit, object_bounds, face_geometry)) || hit.kind != quader_poly::ElementKind::edge) {
		if (replace_selection) {
			clear_selection(objects, active_object_id);
		}
		return false;
	}
	const std::vector<quader_poly::Edge> loop_edges = quader_poly::edge_loop_edges(objects[hit_index].document, hit.edge);
	if (loop_edges.empty()) {
		return false;
	}
	for (int index = 0; index < static_cast<int>(objects.size()); ++index) {
		if (index != hit_index && replace_selection) {
			objects[index].selected = false;
			objects[index].selection = quader_poly::Selection();
		}
	}

	PolygonObject& object = objects[hit_index];
	const bool was_edit_target = object.selected || object.id == active_object_id;
	object.selection.mode = quader_poly::SelectionMode::edge;
	if (replace_selection) {
		object.selection.clear();
		object.selection.mode = quader_poly::SelectionMode::edge;
	}
	if (remove_selection_value) {
		for (const quader_poly::Edge& loop_edge : loop_edges) {
			if (!edge_visible_for_pick_expansion(object.document, loop_edge, options)) {
				continue;
			}
			const quader_poly::Edge normalized = quader_poly::make_edge(loop_edge.a, loop_edge.b);
			std::erase_if(object.selection.edges, [normalized](quader_poly::Edge edge) {
				return quader_poly::make_edge(edge.a, edge.b) == normalized;
			});
		}
	} else {
		for (const quader_poly::Edge& loop_edge : loop_edges) {
			if (edge_visible_for_pick_expansion(object.document, loop_edge, options) &&
					!quader_poly::selection_contains(object.selection, loop_edge)) {
				object.selection.edges.push_back(quader_poly::make_edge(loop_edge.a, loop_edge.b));
			}
		}
	}
	object.selection.active_kind = quader_poly::ElementKind::edge;
	object.selection.has_active = !object.selection.edges.empty();
	object.selection.active_edge = object.selection.edges.empty() ? quader_poly::Edge {} : (remove_selection_value ? object.selection.edges.back() : quader_poly::make_edge(hit.edge.a, hit.edge.b));
	if (object.selection.edges.empty()) {
		if (remove_selection_value && was_edit_target) {
			keep_component_edit_target(object, active_object_id);
		} else {
			object.selected = false;
			normalize_active_object_id(objects, active_object_id);
		}
		return true;
	}
	object.selected = true;
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
		std::span<const PickObjectFaceGeometry> face_geometry)
{
	const bool replace_selection = operation == SelectionEditOperation::replace;
	const bool remove_selection_value = operation == SelectionEditOperation::remove;
	apply_editor_surface_pick_policy(pick_options);
	if (objects.empty()) {
		if (replace_selection) {
			clear_selection(objects, active_object_id);
		}
		return false;
	}
	mode = SelectionMode::vertex;
	int hit_index = -1;
	quader_poly::PickResult hit;
	quader_poly::PickOptions options = pick_options;
	options.mode = quader_poly::SelectionMode::vertex;
	bool component_hit = false;
	if (const std::optional<SourceWireComponentPick> source_wire_hit =
				pick_active_source_wire_component(
					objects,
					SelectionMode::vertex,
					ray,
					options,
					-1,
					remove_selection_value)) {
		hit_index = source_wire_hit->object_index;
		hit = source_wire_hit->hit;
		component_hit = true;
	}
	if ((!component_hit && !pick_nearest_object(objects, ray, options, hit_index, hit, object_bounds, face_geometry)) || hit.kind != quader_poly::ElementKind::vertex) {
		if (replace_selection) {
			clear_selection(objects, active_object_id);
		}
		return false;
	}
	for (int index = 0; index < static_cast<int>(objects.size()); ++index) {
		if (index != hit_index && replace_selection) {
			objects[index].selected = false;
			objects[index].selection = quader_poly::Selection();
		}
	}

	PolygonObject& object = objects[hit_index];
	const bool was_edit_target = object.selected || object.id == active_object_id;
	object.selection.mode = quader_poly::SelectionMode::vertex;
	if (replace_selection) {
		object.selection.clear();
		object.selection.mode = quader_poly::SelectionMode::vertex;
	}
	if (remove_selection_value) {
		object.selection.vertices.clear();
	} else {
		for (const quader_poly::Vertex& vertex : object.document.vertices) {
			if (vertex_visible_for_pick_expansion(object.document, vertex.id, options) &&
					!quader_poly::selection_contains(object.selection, vertex.id)) {
				object.selection.vertices.push_back(vertex.id);
			}
		}
	}
	object.selection.has_active = !object.selection.vertices.empty();
	object.selection.active_kind = quader_poly::ElementKind::vertex;
	object.selection.active_vertex = object.selection.vertices.empty() ? quader_poly::invalid_element_id : object.selection.vertices.back();
	if (object.selection.vertices.empty()) {
		if (remove_selection_value && was_edit_target) {
			keep_component_edit_target(object, active_object_id);
		} else {
			object.selected = false;
			normalize_active_object_id(objects, active_object_id);
		}
		return true;
	}
	object.selected = true;
	active_object_id = object.id;
	return true;
}

namespace {

bool session_has_component_selection(const EditorSession& session, SelectionMode mode)
{
	if (mode == SelectionMode::object) {
		return false;
	}
	for (const PolygonObject& object : session.objects()) {
		if (!object.selected) {
			continue;
		}
		if (mode == SelectionMode::vertex && !object.selection.vertices.empty()) {
			return true;
		}
		if (mode == SelectionMode::edge && !object.selection.edges.empty()) {
			return true;
		}
		if (mode == SelectionMode::face && !object.selection.faces.empty()) {
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
		std::span<const PickObjectFaceGeometry> face_geometry)
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
					face_geometry);
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
				!polygon_selection_matches(object.selection, next_selection);
		object.selected = next_selected;
		object.selection = next_selection;
	}
	if (!changed) {
		return false;
	}
	state.selection_mode = mode;
	state.active_object_id = active_object_id;
	normalize_active_object_id(state.objects, state.active_object_id);
	state.dirty = true;
	++state.selection_revision;
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
		if (mode != SelectionMode::object) {
			if (operation == SelectionEditOperation::replace) {
				operation = SelectionEditOperation::add;
			}
			result.hit = pick_select(
					picking_objects,
					mode,
					operation,
					request.ray,
					active_object_id,
					request.pick_options,
					false,
					request.object_bounds,
					request.face_geometry);
			result.changed = apply_picked_selection_to_session(session, picking_objects, mode, active_object_id);
		}
		return result;
	}

	const bool multi_click = request.click_count > 1;
	if (request.preserve_existing_component_selection_on_hit &&
			operation == SelectionEditOperation::replace &&
			!multi_click &&
			session_has_component_selection(session, mode) &&
			pick_selection_request_hits_target(
					picking_objects,
					mode,
					request.ray,
					request.pick_options,
					request.object_bounds,
					request.face_geometry)) {
		result.applied = false;
		result.hit = true;
		return result;
	}

	if (request.preserve_selection_on_selected_hit &&
			operation == SelectionEditOperation::replace &&
			!multi_click) {
		const SelectionHoverUpdateResult hovered =
				update_selection_hover(
						picking_objects,
						mode,
						{},
						request.ray,
						false,
						request.pick_options,
						request.object_bounds,
						request.face_geometry);
		bool hit_already_selected = false;
		if (mode == SelectionMode::object && hovered.hover.object_id > 0) {
			hit_already_selected = std::any_of(
					picking_objects.begin(),
					picking_objects.end(),
					[&](const PolygonObject& object) {
						return object.id == hovered.hover.object_id && object.selected;
					});
		} else {
			hit_already_selected =
					selection_hover_target_matches_selection(picking_objects, mode, hovered.hover);
		}
		if (hit_already_selected) {
			result.applied = false;
			result.hit = true;
			return result;
		}
	}

	if (multi_click && mode == SelectionMode::face) {
		result.hit = pick_select_all_faces(
				picking_objects,
				mode,
				operation,
				request.ray,
				active_object_id,
				request.pick_options,
				request.object_bounds,
				request.face_geometry);
	} else if (multi_click && mode == SelectionMode::edge) {
		result.hit = pick_select_edge_loop(
				picking_objects,
				mode,
				operation,
				request.ray,
				active_object_id,
				request.pick_options,
				request.object_bounds,
				request.face_geometry);
	} else if (multi_click && mode == SelectionMode::vertex) {
		result.hit = pick_select_all_vertices(
				picking_objects,
				mode,
				operation,
				request.ray,
				active_object_id,
				request.pick_options,
				request.object_bounds,
				request.face_geometry);
	} else {
		result.hit = pick_select(
				picking_objects,
				mode,
				operation,
				request.ray,
				active_object_id,
				request.pick_options,
				true,
				request.object_bounds,
				request.face_geometry);
	}
	if (result.hit || !request.preserve_selection_on_miss) {
		result.changed = apply_picked_selection_to_session(session, picking_objects, mode, active_object_id);
	}
	return result;
}

} // namespace quader_editor_core

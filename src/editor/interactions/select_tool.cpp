////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/modeling/runtime/editor_tool.hpp>
#include <editor/interactions/selection_box.hpp>
#include <editor/interactions/selection_edit.hpp>

#include "tool_math_internal.hpp"

#include <quader/modeling/neutral/geometry/geometry.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <vector>

namespace quader_editor_core {
namespace {

quader_geometry::QVec2d to_geometry_vec2(quader::QVec2 value)
{
	return quader_geometry::QVec2d(value);
}

ScreenRect to_screen_rect(const quader_geometry::QRect2d& rect)
{
	return { rect.min.x, rect.min.y, rect.max.x, rect.max.y };
}

quader_geometry::QRect2d to_geometry_rect(const ScreenRect& rect)
{
	return { { rect.min_x, rect.min_y }, { rect.max_x, rect.max_y } };
}

bool contains_id(const std::vector<int>& ids, int id)
{
	return std::find(ids.begin(), ids.end(), id) != ids.end();
}

void append_unique_selection_id(std::vector<int>& ids, int id)
{
	if (id >= 0 && !contains_id(ids, id)) {
		ids.push_back(id);
	}
}

bool face_is_valid(RectangleSelectionFace face)
{
	return face.object_id >= 0 && face.face_index >= 0;
}

bool edge_is_valid(RectangleSelectionEdge edge)
{
	return edge.object_id >= 0 && edge.a_index >= 0 && edge.b_index >= 0;
}

bool vertex_is_valid(RectangleSelectionVertex vertex)
{
	return vertex.object_id >= 0 && vertex.vertex_index >= 0;
}

RectangleSelectionEdge normalized_edge(RectangleSelectionEdge edge)
{
	if (edge.a_index > edge.b_index) {
		std::swap(edge.a_index, edge.b_index);
	}
	return edge;
}

void append_unique_face(std::vector<RectangleSelectionFace>& faces, RectangleSelectionFace face)
{
	if (!face_is_valid(face)) {
		return;
	}
	const auto found = std::find_if(faces.begin(), faces.end(), [face](RectangleSelectionFace existing) {
		return existing.object_id == face.object_id && existing.face_index == face.face_index;
	});
	if (found == faces.end()) {
		faces.push_back(face);
	}
}

bool handle_is_empty(const SelectionHandleTarget& handle)
{
  return handle.kind == ActiveSelectionKind::None && handle.object_id < 0 &&
         handle.face_index < 0 && handle.vertex_indices.empty() &&
         handle.element_groups.empty();
}

bool hit_is_empty(const SelectionHitTarget& hit)
{
	return hit.object_id < 0 && hit.face_index < 0;
}

bool has_partial_transform_selection(const RectangleSelectionState& selection)
{
	return !selection.selected_faces.empty() ||
		!selection.selected_edges.empty() ||
		!selection.selected_vertices.empty() ||
		!selection.active_edge_vertex_indices.empty() ||
		!selection.active_vertex_indices.empty() ||
		(selection.active_object_id >= 0 && selection.active_face_index >= 0);
}

bool element_handle_distance_equal(double a, double b)
{
	const double scale = std::max(1.0, std::max(std::abs(a), std::abs(b)));
	return std::abs(a - b) <= 0.00001 * scale;
}

/**
 * Implements Select Tool behavior for the modal modeling interaction layer.
 */
class SelectTool final : public EditorTool {
public:
	[[nodiscard]] ViewportInteractionTool tool_id() const override
	{
          return ViewportInteractionTool::Select;
        }

	[[nodiscard]] std::string_view id() const override
	{
		return viewport_interaction_tool_id(tool_id());
	}

	[[nodiscard]] std::string_view label() const override
	{
		return "Select";
	}

	void reset_runtime_state(EditorRuntimeState& runtime, bool) const override
	{
		clear_editor_runtime_selection_presentation_state(runtime);
	}

	[[nodiscard]] SelectionEditOperation edit_operation_from_id(int operation) const
	{
          if (operation == static_cast<int>(SelectionEditOperation::Add)) {
            return SelectionEditOperation::Add;
          }
          if (operation == static_cast<int>(SelectionEditOperation::Remove)) {
            return SelectionEditOperation::Remove;
          }
          if (operation == static_cast<int>(SelectionEditOperation::Toggle)) {
            return SelectionEditOperation::Toggle;
          }
          return SelectionEditOperation::Replace;
        }

	[[nodiscard]] int edit_operation_id(SelectionEditOperation operation) const
	{
		return static_cast<int>(operation);
	}

	[[nodiscard]] SelectionEditOperation edit_operation_from_modifiers(bool ctrl_pressed, bool shift_pressed) const
	{
		if (ctrl_pressed) {
                  return SelectionEditOperation::Remove;
                }
		if (shift_pressed) {
                  return SelectionEditOperation::Add;
                }
                return SelectionEditOperation::Replace;
        }

	[[nodiscard]] bool edit_operation_preserves_existing(SelectionEditOperation operation) const
	{
          return operation == SelectionEditOperation::Add ||
                 operation == SelectionEditOperation::Remove ||
                 operation == SelectionEditOperation::Toggle;
        }

	void begin_box(SelectionBoxState& state, quader::QVec2 position, SelectionEditOperation operation) const
	{
		state.active = true;
		state.dragging = false;
		state.operation = operation;
		state.start = position;
		state.end = position;
	}

	void update_box(SelectionBoxState& state, quader::QVec2 position, double drag_threshold) const
	{
		if (!state.active) {
			return;
		}
		state.end = position;
		if (!state.dragging && quader_geometry::length(to_geometry_vec2(state.end) - to_geometry_vec2(state.start)) >= drag_threshold) {
			state.dragging = true;
		}
	}

	[[nodiscard]] SelectionBoxCommit finish_box(SelectionBoxState& state, quader::QVec2 position, SelectionEditOperation operation, double drag_threshold) const
	{
		(void)operation;
		SelectionBoxCommit commit;
		if (!state.active) {
			return commit;
		}
		state.end = position;
		commit.active = true;
		commit.operation = state.operation;
		commit.dragged = state.dragging || quader_geometry::length(to_geometry_vec2(state.end) - to_geometry_vec2(state.start)) >= drag_threshold;
		commit.start = state.start;
		commit.end = state.end;
		clear_box(state);
		return commit;
	}

	void clear_box(SelectionBoxState& state) const
	{
		state = SelectionBoxState();
	}

	[[nodiscard]] ScreenRect normalized_rect(quader::QVec2 a, quader::QVec2 b) const
	{
		quader_geometry::QRect2d rect = quader_geometry::empty_rect2<double>();
		quader_geometry::rect_include(rect, to_geometry_vec2(a));
		quader_geometry::rect_include(rect, to_geometry_vec2(b));
		return to_screen_rect(rect);
	}

	[[nodiscard]] bool rect_contains(const ScreenRect& rect, quader::QVec2 point) const
	{
		return quader_geometry::rect_contains_point(to_geometry_rect(rect), to_geometry_vec2(point));
	}

	[[nodiscard]] bool rect_intersects_bounds(const ScreenRect& rect, quader::QVec2 min_bounds, quader::QVec2 max_bounds) const
	{
		quader_geometry::QRect2d bounds = quader_geometry::empty_rect2<double>();
		quader_geometry::rect_include(bounds, to_geometry_vec2(min_bounds));
		quader_geometry::rect_include(bounds, to_geometry_vec2(max_bounds));
		return quader_geometry::rect_overlaps(to_geometry_rect(rect), bounds);
	}

	[[nodiscard]] std::array<quader::QVec2, 4> rect_corners(const ScreenRect& rect) const
	{
		return { {
			{ static_cast<float>(rect.min_x), static_cast<float>(rect.min_y) },
			{ static_cast<float>(rect.max_x), static_cast<float>(rect.min_y) },
			{ static_cast<float>(rect.max_x), static_cast<float>(rect.max_y) },
			{ static_cast<float>(rect.min_x), static_cast<float>(rect.max_y) },
		} };
	}

	[[nodiscard]] bool all_points_inside_rect(const ScreenRect& rect, const std::vector<quader::QVec2>& points) const
	{
		return !points.empty() && std::all_of(points.begin(), points.end(), [&](quader::QVec2 point) {
			return rect_contains(rect, point);
		});
	}

	[[nodiscard]] bool bounds_inside_rect(const ScreenRect& rect, quader::QVec2 min_bounds, quader::QVec2 max_bounds) const
	{
		for (const quader::QVec2 corner : rect_corners(ScreenRect {
				 min_bounds.x,
				 min_bounds.y,
				 max_bounds.x,
				 max_bounds.y,
			 })) {
			if (!rect_contains(rect, corner)) {
				return false;
			}
		}
		return true;
	}

	[[nodiscard]] double orientation(quader::QVec2 a, quader::QVec2 b, quader::QVec2 c) const
	{
		return (static_cast<double>(b.x) - static_cast<double>(a.x)) * (static_cast<double>(c.y) - static_cast<double>(a.y)) -
			(static_cast<double>(b.y) - static_cast<double>(a.y)) * (static_cast<double>(c.x) - static_cast<double>(a.x));
	}

	[[nodiscard]] bool point_on_segment(quader::QVec2 point, quader::QVec2 a, quader::QVec2 b) const
	{
          constexpr double kEpsilon = 0.0001;
          if (std::abs(orientation(a, b, point)) > kEpsilon) {
            return false;
          }
          return static_cast<double>(point.x) >=
                     std::min(static_cast<double>(a.x),
                              static_cast<double>(b.x)) -
                         kEpsilon &&
                 static_cast<double>(point.x) <=
                     std::max(static_cast<double>(a.x),
                              static_cast<double>(b.x)) +
                         kEpsilon &&
                 static_cast<double>(point.y) >=
                     std::min(static_cast<double>(a.y),
                              static_cast<double>(b.y)) -
                         kEpsilon &&
                 static_cast<double>(point.y) <=
                     std::max(static_cast<double>(a.y),
                              static_cast<double>(b.y)) +
                         kEpsilon;
        }

	[[nodiscard]] bool segments_intersect(quader::QVec2 a0, quader::QVec2 a1, quader::QVec2 b0, quader::QVec2 b1) const
	{
          constexpr double kEpsilon = 0.0001;
          const double o1 = orientation(a0, a1, b0);
          const double o2 = orientation(a0, a1, b1);
          const double o3 = orientation(b0, b1, a0);
          const double o4 = orientation(b0, b1, a1);
          if (((o1 > kEpsilon && o2 < -kEpsilon) ||
               (o1 < -kEpsilon && o2 > kEpsilon)) &&
              ((o3 > kEpsilon && o4 < -kEpsilon) ||
               (o3 < -kEpsilon && o4 > kEpsilon))) {
            return true;
          }
                return point_on_segment(b0, a0, a1) ||
			point_on_segment(b1, a0, a1) ||
			point_on_segment(a0, b0, b1) ||
			point_on_segment(a1, b0, b1);
	}

	[[nodiscard]] bool segment_intersects_rect(const ScreenRect& rect, quader::QVec2 a, quader::QVec2 b) const
	{
		if (rect_contains(rect, a) || rect_contains(rect, b)) {
			return true;
		}
		const std::array<quader::QVec2, 4> corners = rect_corners(rect);
		for (std::size_t index = 0; index < corners.size(); ++index) {
			if (segments_intersect(a, b, corners[index], corners[(index + 1U) % corners.size()])) {
				return true;
			}
		}
		return false;
	}

	[[nodiscard]] bool point_in_polygon(quader::QVec2 point, const std::vector<quader::QVec2>& polygon) const
	{
		if (polygon.size() < 3U) {
			return false;
		}
		bool inside = false;
		for (std::size_t current = 0, previous = polygon.size() - 1U; current < polygon.size(); previous = current++) {
			const quader::QVec2 a = polygon[previous];
			const quader::QVec2 b = polygon[current];
			if (point_on_segment(point, a, b)) {
				return true;
			}
			const bool crosses = (static_cast<double>(a.y) > static_cast<double>(point.y)) !=
				(static_cast<double>(b.y) > static_cast<double>(point.y));
			if (crosses) {
				const double x_intersect = static_cast<double>(a.x) +
					(static_cast<double>(point.y) - static_cast<double>(a.y)) *
						(static_cast<double>(b.x) - static_cast<double>(a.x)) /
						(static_cast<double>(b.y) - static_cast<double>(a.y));
				if (x_intersect >= static_cast<double>(point.x)) {
					inside = !inside;
				}
			}
		}
		return inside;
	}

	[[nodiscard]] bool any_rect_corner_inside_polygon(const ScreenRect& rect, const std::vector<quader::QVec2>& polygon) const
	{
		for (const quader::QVec2 corner : rect_corners(rect)) {
			if (point_in_polygon(corner, polygon)) {
				return true;
			}
		}
		return false;
	}

	[[nodiscard]] bool same_screen_point(quader::QVec2 a, quader::QVec2 b) const
	{
          constexpr double kEpsilon = 0.0001;
          return std::abs(static_cast<double>(a.x) -
                          static_cast<double>(b.x)) <= kEpsilon &&
                 std::abs(static_cast<double>(a.y) -
                          static_cast<double>(b.y)) <= kEpsilon;
        }

	[[nodiscard]] std::vector<quader::QVec2> convex_hull(std::vector<quader::QVec2> points) const
	{
		std::sort(points.begin(), points.end(), [](quader::QVec2 left, quader::QVec2 right) {
			if (left.x == right.x) {
				return left.y < right.y;
			}
			return left.x < right.x;
		});
		points.erase(
			std::unique(points.begin(), points.end(), [&](quader::QVec2 left, quader::QVec2 right) {
				return same_screen_point(left, right);
			}),
			points.end());
		if (points.size() <= 2U) {
			return points;
		}

                constexpr double kEpsilon = 0.0001;
                std::vector<quader::QVec2> lower;
		lower.reserve(points.size());
		for (const quader::QVec2 point : points) {
                  while (lower.size() >= 2U &&
                         orientation(lower[lower.size() - 2U], lower.back(),
                                     point) <= kEpsilon) {
                    lower.pop_back();
                  }
                        lower.push_back(point);
		}

		std::vector<quader::QVec2> upper;
		upper.reserve(points.size());
		for (auto it = points.rbegin(); it != points.rend(); ++it) {
                  while (upper.size() >= 2U &&
                         orientation(upper[upper.size() - 2U], upper.back(),
                                     *it) <= kEpsilon) {
                    upper.pop_back();
                  }
                        upper.push_back(*it);
		}

		lower.pop_back();
		upper.pop_back();
		lower.insert(lower.end(), upper.begin(), upper.end());
		return lower;
	}

	[[nodiscard]] bool polygon_intersects_rect(const ScreenRect& rect, const std::vector<quader::QVec2>& polygon) const
	{
		if (polygon.empty()) {
			return false;
		}
		for (const quader::QVec2 point : polygon) {
			if (rect_contains(rect, point)) {
				return true;
			}
		}
		if (polygon.size() == 2U) {
			return segment_intersects_rect(rect, polygon[0], polygon[1]);
		}
		if (polygon.size() < 3U) {
			return false;
		}
		for (std::size_t index = 0; index < polygon.size(); ++index) {
			if (segment_intersects_rect(rect, polygon[index], polygon[(index + 1U) % polygon.size()])) {
				return true;
			}
		}
		return any_rect_corner_inside_polygon(rect, polygon);
	}

	[[nodiscard]] bool projected_points_shape_intersects_rect(
			const ScreenRect& rect,
			const std::vector<quader::QVec2>& points) const
	{
		if (points.empty()) {
			return false;
		}
		if (points.size() == 1U) {
			return rect_contains(rect, points.front());
		}
		if (points.size() == 2U) {
			return segment_intersects_rect(rect, points[0], points[1]);
		}
		return polygon_intersects_rect(rect, convex_hull(points));
	}

	[[nodiscard]] bool projected_object_matches(const RectangleProjectedObject& object, const RectangleProjectedHitsRequest& request) const
	{
          if (object.has_screen_bounds &&
              !rect_intersects_bounds(request.rect, object.screen_min,
                                      object.screen_max)) {
            return false;
          }
          if (request.match == RectangleSelectionMatch::Contain) {
            if (object.has_screen_bounds) {
              return bounds_inside_rect(request.rect, object.screen_min,
                                        object.screen_max);
            }
            return object.has_screen_center &&
                   rect_contains(request.rect, object.screen_center);
          }

                for (const quader::QVec2 point : object.screen_points) {
			if (rect_contains(request.rect, point)) {
				return true;
			}
		}
		for (const RectangleProjectedSegment segment : object.screen_segments) {
			if (segment_intersects_rect(request.rect, segment.start, segment.end)) {
				return true;
			}
		}
		for (const std::vector<quader::QVec2>& polygon : object.screen_polygons) {
			if (polygon_intersects_rect(request.rect, polygon)) {
				return true;
			}
		}

		const bool has_projected_geometry = !object.screen_segments.empty() || !object.screen_polygons.empty();
		if (has_projected_geometry) {
			return false;
		}
		if (projected_points_shape_intersects_rect(request.rect, object.screen_points)) {
			return true;
		}
		return (object.has_screen_center && rect_contains(request.rect, object.screen_center)) ||
			(object.has_screen_bounds && rect_intersects_bounds(request.rect, object.screen_min, object.screen_max));
	}

	[[nodiscard]] bool projected_face_matches(const RectangleProjectedFace& face, const RectangleProjectedHitsRequest& request) const
	{
          if (face.has_screen_bounds &&
              !rect_intersects_bounds(request.rect, face.screen_min,
                                      face.screen_max)) {
            return false;
          }
          if (request.match == RectangleSelectionMatch::Contain) {
            if (face.has_screen_bounds) {
              return bounds_inside_rect(request.rect, face.screen_min,
                                        face.screen_max);
            }
            if (!face.screen_vertices.empty()) {
              return all_points_inside_rect(request.rect, face.screen_vertices);
            }
            return face.has_screen_position &&
                   rect_contains(request.rect, face.screen_position);
          }
                if (face.has_screen_position && rect_contains(request.rect, face.screen_position)) {
			return true;
		}
		for (const quader::QVec2 point : face.screen_vertices) {
			if (rect_contains(request.rect, point)) {
				return true;
			}
		}
		if (face.screen_vertices.size() >= 2U) {
			for (std::size_t index = 0; index < face.screen_vertices.size(); ++index) {
				if (segment_intersects_rect(
							request.rect,
							face.screen_vertices[index],
							face.screen_vertices[(index + 1U) % face.screen_vertices.size()])) {
					return true;
				}
			}
			if (any_rect_corner_inside_polygon(request.rect, face.screen_vertices)) {
				return true;
			}
		}
		return false;
	}

	[[nodiscard]] bool projected_edge_matches(const RectangleProjectedEdge& edge, const RectangleProjectedHitsRequest& request) const
	{
		const bool has_segment = edge.has_screen_start && edge.has_screen_end;
                if (has_segment &&
                    !rect_intersects_bounds(
                        request.rect,
                        {std::min(edge.screen_start.x, edge.screen_end.x),
                         std::min(edge.screen_start.y, edge.screen_end.y)},
                        {std::max(edge.screen_start.x, edge.screen_end.x),
                         std::max(edge.screen_start.y, edge.screen_end.y)})) {
                  return false;
                }
                if (request.match == RectangleSelectionMatch::Contain) {
                  if (has_segment) {
                    return rect_contains(request.rect, edge.screen_start) &&
                           rect_contains(request.rect, edge.screen_end);
                  }
                  return edge.has_screen_position &&
                         rect_contains(request.rect, edge.screen_position);
                }
                if (has_segment) {
			return segment_intersects_rect(request.rect, edge.screen_start, edge.screen_end);
		}
		return edge.has_screen_position && rect_contains(request.rect, edge.screen_position);
	}

	[[nodiscard]] RectangleSelectionHits rectangle_hits_from_projected_candidates(const RectangleProjectedHitsRequest& request) const
	{
		RectangleSelectionHits hits;
                if (request.mode == SelectionMode::Object) {
                  for (const RectangleProjectedObject &object :
                       request.objects) {
                    if (object.object_id < 0) {
                      continue;
                    }
                    if (projected_object_matches(object, request)) {
                      hits.object_ids.push_back(object.object_id);
                    }
                  }
                  return hits;
                }
                if (request.mode == SelectionMode::Face) {
                  for (const RectangleProjectedFace &face : request.faces) {
                    if (face.object_id >= 0 && face.face_index >= 0 &&
                        projected_face_matches(face, request)) {
                      hits.faces.push_back(
                          {face.object_id, face.face_index, {}});
                    }
                  }
                  return hits;
                }
                if (request.mode == SelectionMode::Edge) {
                  for (const RectangleProjectedEdge &edge : request.edges) {
                    if (edge.object_id >= 0 && edge.a_index >= 0 &&
                        edge.b_index >= 0 &&
                        projected_edge_matches(edge, request)) {
                      hits.edges.push_back(
                          {edge.object_id, edge.a_index, edge.b_index});
                    }
                  }
                  return hits;
                }
                if (request.mode == SelectionMode::Vertex) {
                  for (const RectangleProjectedVertex &vertex :
                       request.vertices) {
                    if (vertex.object_id >= 0 && vertex.vertex_index >= 0 &&
                        vertex.has_screen_position &&
                        rect_contains(request.rect, vertex.screen_position)) {
                      hits.vertices.push_back(
                          {vertex.object_id, vertex.vertex_index});
                    }
                  }
                }
                return hits;
	}

	[[nodiscard]] SelectionBoxPrimitives build_box_primitives(const SelectionBoxState& state, bool camera_matches) const
	{
		SelectionBoxPrimitives primitives;
		if (!state.active || !camera_matches) {
			return primitives;
		}
		primitives.ok = true;
		primitives.dragging = state.dragging;
		primitives.polygon.push_back(state.start);
		primitives.polygon.push_back({ state.end.x, state.start.y });
		primitives.polygon.push_back(state.end);
		primitives.polygon.push_back({ state.start.x, state.end.y });
		return primitives;
	}

	[[nodiscard]] std::vector<int> handle_target_object_ids_for_selection(const SelectionHandleTargetObjectRequest& request) const
	{
		const RectangleSelectionState& selection = request.current_selection;
		std::vector<int> target_ids = selection.selected_object_ids;
		auto append_extra_targets = [&]() {
			for (const int object_id : request.extra_target_object_ids) {
				append_unique_selection_id(target_ids, object_id);
			}
		};

		if (!target_ids.empty()) {
			append_extra_targets();
			return target_ids;
		}

		for (const RectangleSelectionEdge edge : selection.selected_edges) {
			append_unique_selection_id(target_ids, edge.object_id);
		}
		for (const RectangleSelectionVertex vertex : selection.selected_vertices) {
			append_unique_selection_id(target_ids, vertex.object_id);
		}
		if (!target_ids.empty()) {
			append_extra_targets();
			return target_ids;
		}

		if (selection.active_object_id >= 0) {
			append_unique_selection_id(target_ids, selection.active_object_id);
			append_extra_targets();
			return target_ids;
		}

		append_unique_selection_id(target_ids, request.hovered_object_id);
		append_extra_targets();
		return target_ids;
	}

	[[nodiscard]] std::vector<ElementHandlePickHit> pick_element_handle_candidates(
			const std::vector<ElementHandlePickCandidate>& candidates,
			quader::QVec2 screen_position,
			double pick_radius) const
	{
		const double radius = std::max(1.0, pick_radius);
		const double radius_squared = radius * radius;
		std::vector<ElementHandlePickHit> hits;
		hits.reserve(candidates.size());
		for (int index = 0; index < static_cast<int>(candidates.size()); ++index) {
			const ElementHandlePickCandidate& candidate = candidates[index];
			const double screen_distance_squared = vec2_distance_squared(screen_position, candidate.screen_position);
			if (screen_distance_squared > radius_squared) {
				continue;
			}
			ElementHandlePickHit hit;
			hit.candidate_index = candidate.candidate_index >= 0 ? candidate.candidate_index : index;
			hit.screen_distance_squared = screen_distance_squared;
			hit.screen_distance = std::sqrt(screen_distance_squared);
			hits.push_back(hit);
		}

		std::stable_sort(hits.begin(), hits.end(), [&candidates](const ElementHandlePickHit& a, const ElementHandlePickHit& b) {
			if (!element_handle_distance_equal(a.screen_distance_squared, b.screen_distance_squared)) {
				return a.screen_distance_squared < b.screen_distance_squared;
			}
			const ElementHandlePickCandidate& candidate_a = candidates[a.candidate_index];
			const ElementHandlePickCandidate& candidate_b = candidates[b.candidate_index];
			if (!element_handle_distance_equal(candidate_a.camera_distance, candidate_b.camera_distance)) {
				return candidate_a.camera_distance < candidate_b.camera_distance;
			}
			if (candidate_a.kind != candidate_b.kind) {
				return static_cast<int>(candidate_a.kind) < static_cast<int>(candidate_b.kind);
			}
			if (candidate_a.object_id != candidate_b.object_id) {
				return candidate_a.object_id < candidate_b.object_id;
			}
			if (candidate_a.a_index != candidate_b.a_index) {
				return candidate_a.a_index < candidate_b.a_index;
			}
			return candidate_a.b_index < candidate_b.b_index;
		});
		return hits;
	}

	[[nodiscard]] bool mode_uses_element_handles(SelectionMode mode) const
	{
          return mode == SelectionMode::Face || mode == SelectionMode::Edge ||
                 mode == SelectionMode::Vertex;
        }

	[[nodiscard]] ViewportToolIntent default_viewport_tool_for_selection_mode(SelectionMode mode) const
	{
          (void)mode;
          return ViewportToolIntent::Move;
        }

	[[nodiscard]] bool selection_has_move_tool_target(const RectangleSelectionState& selection) const
	{
		switch (selection.mode) {
                case SelectionMode::Object:
                  return !selection.selected_object_ids.empty();
                case SelectionMode::Face:
                  return !selection.selected_faces.empty();
                case SelectionMode::Edge:
                  return !selection.selected_edges.empty() ||
                         !selection.active_edge_vertex_indices.empty() ||
                         !selection.selected_edge_full_object_ids.empty();
                case SelectionMode::Vertex:
                  return !selection.selected_vertices.empty() ||
                         !selection.active_vertex_indices.empty() ||
                         !selection.selected_vertex_full_object_ids.empty();
                default:
                  break;
		}
		return false;
	}

	[[nodiscard]] SelectionHoverFeedback hover_feedback_for_selection(const SelectionHoverFeedbackRequest& request) const
	{
		SelectionHoverFeedback feedback;
                feedback.highlight_selection.mode = SelectionMode::Face;
                if (request.current_selection.mode == SelectionMode::Face) {
                  append_unique_face(
                      feedback.highlight_selection.selected_faces,
                      {request.hovered_hit.object_id,
                       request.hovered_hit.face_index});
                  if (request.hovered_handle.kind ==
                      ActiveSelectionKind::Face) {
                    append_unique_face(
                        feedback.highlight_selection.selected_faces,
                        {request.hovered_handle.object_id,
                         request.hovered_handle.face_index});
                  }
                }

                if (!request.selected_modifier_pressed ||
                    request.current_selection.mode == SelectionMode::Object) {
                  return feedback;
                }

                if (!handle_is_empty(request.hovered_handle)) {
			switch (request.hovered_handle.kind) {
                        case ActiveSelectionKind::Face:
                          feedback.hover_target_selected = selection_has_face(
                              request.current_selection,
                              {request.hovered_handle.object_id,
                               request.hovered_handle.face_index});
                          return feedback;
                        case ActiveSelectionKind::Edge:
                          if (request.hovered_handle.vertex_indices.size() >=
                              2) {
                            feedback.hover_target_selected = selection_has_edge(
                                request.current_selection,
                                {request.hovered_handle.object_id,
                                 request.hovered_handle.vertex_indices[0],
                                 request.hovered_handle.vertex_indices[1]});
                          }
                          return feedback;
                        case ActiveSelectionKind::Vertex:
                          if (!request.hovered_handle.vertex_indices.empty()) {
                            feedback.hover_target_selected =
                                selection_has_vertex(
                                    request.current_selection,
                                    {request.hovered_handle.object_id,
                                     request.hovered_handle.vertex_indices[0]});
                          }
                          return feedback;
                        case ActiveSelectionKind::Object:
                          feedback.hover_target_selected = selection_has_object(
                              request.current_selection,
                              request.hovered_handle.object_id);
                          return feedback;
                        case ActiveSelectionKind::None:
                        default:
                          break;
			}
		}

		if (!hit_is_empty(request.hovered_hit)) {
                  if (request.current_selection.mode == SelectionMode::Face) {
                    feedback.hover_target_selected =
                        selection_has_face(request.current_selection,
                                           {request.hovered_hit.object_id,
                                            request.hovered_hit.face_index});
                  } else {
                    feedback.hover_target_selected =
                        selection_has_object(request.current_selection,
                                             request.hovered_hit.object_id);
                  }
                }
		return feedback;
	}

	[[nodiscard]] bool selection_has_object(const RectangleSelectionState& selection, int object_id) const
	{
		return contains_id(selection.selected_object_ids, object_id);
	}

	[[nodiscard]] bool selection_has_face(const RectangleSelectionState& selection, RectangleSelectionFace face) const
	{
		if (!face_is_valid(face)) {
			return false;
		}
		return std::find_if(selection.selected_faces.begin(), selection.selected_faces.end(), [face](RectangleSelectionFace selected_face) {
			return selected_face.object_id == face.object_id && selected_face.face_index == face.face_index;
		}) != selection.selected_faces.end();
	}

	[[nodiscard]] bool selection_has_edge(const RectangleSelectionState& selection, RectangleSelectionEdge edge) const
	{
		if (!edge_is_valid(edge)) {
			return false;
		}
		if (contains_id(selection.selected_edge_full_object_ids, edge.object_id)) {
			return true;
		}
		const RectangleSelectionEdge key = normalized_edge(edge);
		return std::find_if(selection.selected_edges.begin(), selection.selected_edges.end(), [edge, key](RectangleSelectionEdge selected_edge) {
			selected_edge = normalized_edge(selected_edge);
			return selected_edge.object_id == edge.object_id && selected_edge.a_index == key.a_index && selected_edge.b_index == key.b_index;
		}) != selection.selected_edges.end();
	}

	[[nodiscard]] bool selection_has_vertex(const RectangleSelectionState& selection, RectangleSelectionVertex vertex) const
	{
		if (!vertex_is_valid(vertex)) {
			return false;
		}
		if (contains_id(selection.selected_vertex_full_object_ids, vertex.object_id)) {
			return true;
		}
		return std::find_if(selection.selected_vertices.begin(), selection.selected_vertices.end(), [vertex](RectangleSelectionVertex selected_vertex) {
			return selected_vertex.object_id == vertex.object_id && selected_vertex.vertex_index == vertex.vertex_index;
		}) != selection.selected_vertices.end();
	}

	[[nodiscard]] std::vector<int> source_object_ids_for_hit(const RectangleSelectionState& selection, int clicked_object_id) const
	{
		if (clicked_object_id < 0) {
			return {};
		}
		if (contains_id(selection.selected_object_ids, clicked_object_id)) {
			return selection.selected_object_ids;
		}
		return { clicked_object_id };
	}

	[[nodiscard]] std::vector<int> target_object_ids_for_hit(const RectangleSelectionState& selection, int object_id) const
	{
		if (object_id < 0) {
			return {};
		}
		const std::vector<int> selected_targets = selected_or_active_object_ids(selection);
		if (!selected_targets.empty()) {
			if (!contains_id(selected_targets, object_id)) {
				return {};
			}
			return selected_targets;
		}
		if (!selection.selected_object_ids.empty() && !contains_id(selection.selected_object_ids, object_id)) {
			return {};
		}
		return { object_id };
	}

	[[nodiscard]] std::vector<int> whole_object_transform_ids(const RectangleSelectionState& selection) const
	{
		std::vector<int> ids;
		for (const int object_id : selection.selected_object_ids) {
			append_unique_selection_id(ids, object_id);
		}
		for (const int object_id : selection.selected_edge_full_object_ids) {
			append_unique_selection_id(ids, object_id);
		}
		for (const int object_id : selection.selected_vertex_full_object_ids) {
			append_unique_selection_id(ids, object_id);
		}
		if (!selection.selected_edge_full_object_ids.empty() || !selection.selected_vertex_full_object_ids.empty()) {
			append_unique_selection_id(ids, selection.active_object_id);
		}
		return ids;
	}

	[[nodiscard]] std::optional<RectangleSelectionState> whole_object_transform_selection(const RectangleSelectionState& selection) const
	{
		const std::vector<int> ids = whole_object_transform_ids(selection);
		if (ids.empty() || has_partial_transform_selection(selection)) {
			return std::nullopt;
		}
		if (selection.active_object_id >= 0 && !contains_id(ids, selection.active_object_id)) {
			return std::nullopt;
		}

		RectangleSelectionState whole_object_selection;
                whole_object_selection.mode = SelectionMode::Object;
                whole_object_selection.selected_object_ids = ids;
		whole_object_selection.active_object_id = selection.active_object_id >= 0 ? selection.active_object_id : ids.front();
		return whole_object_selection;
	}

	[[nodiscard]] bool hover_handle_changed(const SelectionHandleTarget& current_handle, const SelectionHandleTarget& next_handle) const
	{
		if (handle_is_empty(current_handle) != handle_is_empty(next_handle)) {
			return true;
		}
		if (handle_is_empty(current_handle) && handle_is_empty(next_handle)) {
			return false;
		}
		if (current_handle.kind != next_handle.kind) {
			return true;
		}
		if (current_handle.object_id != next_handle.object_id) {
			return true;
		}
		if (current_handle.face_index != next_handle.face_index) {
			return true;
		}
		if (current_handle.vertex_indices != next_handle.vertex_indices) {
			return true;
		}
		return vec3_distance_squared(current_handle.position, next_handle.position) > 0.000001F;
	}

	[[nodiscard]] bool hover_hit_changed(const SelectionHitTarget& current_hit, const SelectionHitTarget& next_hit) const
	{
		if (hit_is_empty(current_hit) != hit_is_empty(next_hit)) {
			return true;
		}
		if (hit_is_empty(current_hit) && hit_is_empty(next_hit)) {
			return false;
		}
		return current_hit.object_id != next_hit.object_id || current_hit.face_index != next_hit.face_index;
	}
};

const SelectTool& select_tool_impl()
{
  static const SelectTool kTool;
  return kTool;
}

} // namespace

const EditorTool& select_tool()
{
	return select_tool_impl();
}

SelectionEditOperation selection_edit_operation_from_id(int operation)
{
	return select_tool_impl().edit_operation_from_id(operation);
}

int selection_edit_operation_id(SelectionEditOperation operation)
{
	return select_tool_impl().edit_operation_id(operation);
}

SelectionEditOperation selection_edit_operation_from_modifiers(bool ctrl_pressed, bool shift_pressed)
{
	return select_tool_impl().edit_operation_from_modifiers(ctrl_pressed, shift_pressed);
}

bool selection_edit_operation_preserves_existing(SelectionEditOperation operation)
{
	return select_tool_impl().edit_operation_preserves_existing(operation);
}

void begin_selection_box(SelectionBoxState& state, quader::QVec2 position, SelectionEditOperation operation)
{
	select_tool_impl().begin_box(state, position, operation);
}

void update_selection_box(SelectionBoxState& state, quader::QVec2 position, double drag_threshold)
{
	select_tool_impl().update_box(state, position, drag_threshold);
}

SelectionBoxCommit finish_selection_box(SelectionBoxState& state, quader::QVec2 position, SelectionEditOperation operation, double drag_threshold)
{
	return select_tool_impl().finish_box(state, position, operation, drag_threshold);
}

void clear_selection_box(SelectionBoxState& state)
{
	select_tool_impl().clear_box(state);
}

ScreenRect normalized_screen_rect(quader::QVec2 a, quader::QVec2 b)
{
	return select_tool_impl().normalized_rect(a, b);
}

bool screen_rect_contains(const ScreenRect& rect, quader::QVec2 point)
{
	return select_tool_impl().rect_contains(rect, point);
}

bool screen_rect_intersects_bounds(const ScreenRect& rect, quader::QVec2 min_bounds, quader::QVec2 max_bounds)
{
	return select_tool_impl().rect_intersects_bounds(rect, min_bounds, max_bounds);
}

RectangleSelectionHits rectangle_hits_from_projected_candidates(const RectangleProjectedHitsRequest& request)
{
	return select_tool_impl().rectangle_hits_from_projected_candidates(request);
}

SelectionBoxPrimitives build_selection_box_primitives(const SelectionBoxState& state, bool camera_matches)
{
	return select_tool_impl().build_box_primitives(state, camera_matches);
}

std::vector<int> handle_target_object_ids_for_selection(const SelectionHandleTargetObjectRequest& request)
{
	return select_tool_impl().handle_target_object_ids_for_selection(request);
}

std::vector<ElementHandlePickHit> pick_element_handle_candidates(
		const std::vector<ElementHandlePickCandidate>& candidates,
		quader::QVec2 screen_position,
		double pick_radius)
{
	return select_tool_impl().pick_element_handle_candidates(candidates, screen_position, pick_radius);
}

bool selection_mode_uses_element_handles(SelectionMode mode)
{
	return select_tool_impl().mode_uses_element_handles(mode);
}

ViewportToolIntent default_viewport_tool_for_selection_mode(SelectionMode mode)
{
	return select_tool_impl().default_viewport_tool_for_selection_mode(mode);
}

bool selection_has_move_tool_target(const RectangleSelectionState& selection)
{
	return select_tool_impl().selection_has_move_tool_target(selection);
}

SelectionHoverFeedback hover_feedback_for_selection(const SelectionHoverFeedbackRequest& request)
{
	return select_tool_impl().hover_feedback_for_selection(request);
}

bool selection_has_object(const RectangleSelectionState& selection, int object_id)
{
	return select_tool_impl().selection_has_object(selection, object_id);
}

bool selection_has_face(const RectangleSelectionState& selection, RectangleSelectionFace face)
{
	return select_tool_impl().selection_has_face(selection, face);
}

bool selection_has_edge(const RectangleSelectionState& selection, RectangleSelectionEdge edge)
{
	return select_tool_impl().selection_has_edge(selection, edge);
}

bool selection_has_vertex(const RectangleSelectionState& selection, RectangleSelectionVertex vertex)
{
	return select_tool_impl().selection_has_vertex(selection, vertex);
}

std::vector<int> source_object_ids_for_hit(const RectangleSelectionState& selection, int clicked_object_id)
{
	return select_tool_impl().source_object_ids_for_hit(selection, clicked_object_id);
}

std::vector<int> target_object_ids_for_hit(const RectangleSelectionState& selection, int object_id)
{
	return select_tool_impl().target_object_ids_for_hit(selection, object_id);
}

std::vector<int> whole_object_transform_ids(const RectangleSelectionState& selection)
{
	return select_tool_impl().whole_object_transform_ids(selection);
}

std::optional<RectangleSelectionState> whole_object_transform_selection(const RectangleSelectionState& selection)
{
	return select_tool_impl().whole_object_transform_selection(selection);
}

bool selection_hover_handle_changed(const SelectionHandleTarget& current_handle, const SelectionHandleTarget& next_handle)
{
	return select_tool_impl().hover_handle_changed(current_handle, next_handle);
}

bool selection_hover_hit_changed(const SelectionHitTarget& current_hit, const SelectionHitTarget& next_hit)
{
	return select_tool_impl().hover_hit_changed(current_hit, next_hit);
}

} // namespace quader_editor_core

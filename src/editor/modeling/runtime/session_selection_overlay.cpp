////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/modeling/runtime/session.hpp>
#include <mesh/geometry/geometry.hpp>
#include <mesh/polygon/document.hpp>

#include <diagnostics/profile.hpp>

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace quader_editor_core {
namespace {

constexpr double kDiagnosticEdgeEpsilon = 0.00001;
constexpr double kDiagnosticEdgeFactorEpsilon = 0.00001;

/**
 * Represents a Diagnostic Edge Ref value used by the modeling runtime domain.
 */
struct DiagnosticEdgeRef {
	int object_id = -1;
	quader_poly::Edge edge;
	quader::QVec3 start;
	quader::QVec3 end;
	quader_geometry::QSegment3<double> segment;
	quader_geometry::QAabb3<double> bounds;
};

quader_geometry::QVec3d to_diagnostic_vec3(quader::QVec3 value)
{
	return {
		static_cast<double>(value.x),
		static_cast<double>(value.y),
		static_cast<double>(value.z),
	};
}

quader_geometry::QAabb3<double> diagnostic_edge_bounds(
		quader_geometry::QVec3d start,
		quader_geometry::QVec3d end)
{
  return {
      {
          std::min(start.x, end.x) - kDiagnosticEdgeEpsilon,
          std::min(start.y, end.y) - kDiagnosticEdgeEpsilon,
          std::min(start.z, end.z) - kDiagnosticEdgeEpsilon,
      },
      {
          std::max(start.x, end.x) + kDiagnosticEdgeEpsilon,
          std::max(start.y, end.y) + kDiagnosticEdgeEpsilon,
          std::max(start.z, end.z) + kDiagnosticEdgeEpsilon,
      },
  };
}

bool diagnostic_factor_is_interior(double factor)
{
  return factor > kDiagnosticEdgeFactorEpsilon &&
         factor < 1.0 - kDiagnosticEdgeFactorEpsilon;
}

bool diagnostic_edges_overlap_colinearly(const DiagnosticEdgeRef& first, const DiagnosticEdgeRef& second)
{
	const quader_geometry::QVec3d first_direction = first.segment.b - first.segment.a;
	const quader_geometry::QVec3d second_direction = second.segment.b - second.segment.a;
	const double first_length_squared = quader_geometry::length_squared(first_direction);
	const double second_length_squared = quader_geometry::length_squared(second_direction);
        const double epsilon_squared =
            kDiagnosticEdgeEpsilon * kDiagnosticEdgeEpsilon;
        if (first_length_squared <= epsilon_squared || second_length_squared <= epsilon_squared) {
		return false;
	}

	const double parallel_limit = epsilon_squared * first_length_squared * second_length_squared;
	if (quader_geometry::length_squared(quader_geometry::cross(first_direction, second_direction)) > parallel_limit) {
		return false;
	}

	const double line_limit = epsilon_squared * first_length_squared;
	if (quader_geometry::length_squared(quader_geometry::cross(second.segment.a - first.segment.a, first_direction)) > line_limit ||
			quader_geometry::length_squared(quader_geometry::cross(second.segment.b - first.segment.a, first_direction)) > line_limit) {
		return false;
	}

	const double second_start_on_first =
		quader_geometry::dot(second.segment.a - first.segment.a, first_direction) / first_length_squared;
	const double second_end_on_first =
		quader_geometry::dot(second.segment.b - first.segment.a, first_direction) / first_length_squared;
	const double overlap_start = std::max(0.0, std::min(second_start_on_first, second_end_on_first));
	const double overlap_end = std::min(1.0, std::max(second_start_on_first, second_end_on_first));
        return (overlap_end - overlap_start) * std::sqrt(first_length_squared) >
               kDiagnosticEdgeEpsilon;
}

bool diagnostic_edges_point_intersect(const DiagnosticEdgeRef& first, const DiagnosticEdgeRef& second)
{
  const quader_geometry::QSegmentSegmentClosestPoints<double> closest =
      quader_geometry::closest_points_segment_segment(
          first.segment, second.segment, kDiagnosticEdgeEpsilon);
  if (!closest.valid || !closest.intersects) {
    return false;
  }

	return diagnostic_factor_is_interior(closest.first_factor) ||
		diagnostic_factor_is_interior(closest.second_factor);
}

bool diagnostic_edges_conflict(const DiagnosticEdgeRef& first, const DiagnosticEdgeRef& second)
{
	if (!quader_geometry::aabb_overlaps(first.bounds, second.bounds)) {
		return false;
	}
	return diagnostic_edges_overlap_colinearly(first, second) ||
		diagnostic_edges_point_intersect(first, second);
}

quader_poly::Selection overlay_all_face_selection(const quader_poly::Document& document)
{
	quader_poly::Selection selection;
        selection.mode = quader_poly::SelectionMode::Face;
        selection.faces.reserve(document.faces.size());
	for (const quader_poly::Face& face : document.faces) {
		selection.faces.push_back(face.id);
	}
	if (!selection.faces.empty()) {
		selection.has_active = true;
                selection.active_kind = quader_poly::ElementKind::Face;
                selection.active_face = selection.faces.back();
	}
	return selection;
}

std::uint64_t overlay_face_edge_key(quader_poly::ElementId a_id, quader_poly::ElementId b_id)
{
	const std::uint32_t a = static_cast<std::uint32_t>(std::min(a_id, b_id));
	const std::uint32_t b = static_cast<std::uint32_t>(std::max(a_id, b_id));
	return (static_cast<std::uint64_t>(a) << 32U) | static_cast<std::uint64_t>(b);
}

/**
 * Represents an Open Edge Overlay Ref value used by the modeling runtime domain.
 */
struct OpenEdgeOverlayRef {
	quader_poly::Edge edge;
	quader::QVec3 face_centroid;
	quader::QVec3 face_normal;
	bool has_face = false;
};

quader::QVec3 overlay_face_centroid(const quader_poly::Document& document, const quader_poly::Face& face)
{
	quader::QVec3 centroid;
	int count = 0;
	for (const quader_poly::ElementId vertex_id : face.vertices) {
		const quader_poly::Vertex* vertex = quader_poly::find_vertex(document, vertex_id);
		if (vertex == nullptr) {
			continue;
		}
		centroid += vertex->position;
		++count;
	}
	return count == 0 ? quader::QVec3 {} : centroid / static_cast<float>(count);
}

void overlay_append_face_edges_to_set(
		const quader_poly::Document& document,
		quader_poly::ElementId face_id,
		std::unordered_set<std::uint64_t>& edges)
{
	const quader_poly::Face* face = quader_poly::find_face(document, face_id);
	if (face == nullptr || face->vertices.size() < 2) {
		return;
	}
	for (std::size_t index = 0; index < face->vertices.size(); ++index) {
		edges.insert(overlay_face_edge_key(face->vertices[index], face->vertices[(index + 1U) % face->vertices.size()]));
	}
}

bool overlay_selection_covers_all_faces(const quader_poly::Document& document, const quader_poly::Selection& selection)
{
	if (document.faces.empty() || selection.faces.size() < document.faces.size()) {
		return false;
	}
	std::unordered_set<quader_poly::ElementId> selected_faces;
	selected_faces.reserve(selection.faces.size());
	for (const quader_poly::ElementId face_id : selection.faces) {
		selected_faces.insert(face_id);
	}
	for (const quader_poly::Face& face : document.faces) {
		if (selected_faces.find(face.id) == selected_faces.end()) {
			return false;
		}
	}
	return true;
}

void append_source_wire_depth_stamp(
		SelectionOverlayFrame& frame,
		const PolygonObject& object,
		const SelectionOverlayStyle& style)
{
	quader_poly::Selection selection = overlay_all_face_selection(object.document);
	if (selection.faces.empty()) {
		return;
	}
	const quader_poly::OverlayData overlay = quader_poly::build_overlay(object.document, selection);
	for (const quader_poly::OverlayTriangle& triangle : overlay.triangles) {
          frame.triangles.push_back({
              SelectionOverlayLayer::SourceWireDepthStamp,
              triangle.a,
              triangle.b,
              triangle.c,
              style.source_wire_color_abgr,
              object.id,
              triangle.face_id,
          });
        }
}

void append_source_wire_line(
		SelectionOverlayFrame& frame,
		quader::QVec3 start,
		quader::QVec3 end,
		std::uint32_t color,
		float width,
		int object_id,
		quader_poly::ElementId face_id,
		quader_poly::Edge edge)
{
	frame.lines.push_back({
			SelectionOverlayLayer::SourceWire,
			start,
			end,
			color,
			width,
			object_id,
			face_id,
			edge,
	});
}

void append_source_wire_face_edges(
		SelectionOverlayFrame& frame,
		const PolygonObject& object,
		const SelectionOverlayStyle& style)
{
	for (const quader_poly::Face& face : object.document.faces) {
		if (face.vertices.size() < 2) {
			continue;
		}
		for (std::size_t index = 0; index < face.vertices.size(); ++index) {
			const quader_poly::ElementId a_id = face.vertices[index];
			const quader_poly::ElementId b_id = face.vertices[(index + 1U) % face.vertices.size()];
			const quader_poly::Vertex* a = quader_poly::find_vertex(object.document, a_id);
			const quader_poly::Vertex* b = quader_poly::find_vertex(object.document, b_id);
			if (a == nullptr || b == nullptr) {
				continue;
			}
			const quader_poly::Edge edge = quader_poly::make_edge(a_id, b_id);
			append_source_wire_line(frame, a->position, b->position,
					style.source_wire_color_abgr,
					style.edge_line_width_pixels, object.id, face.id, edge);
		}
	}
}

void append_hover_source_wire(
		SelectionOverlayFrame& frame,
		const PolygonObject& object,
		const SelectionOverlayStyle& style,
		bool stamp_depth = false)
{
	if (stamp_depth) {
		append_source_wire_depth_stamp(frame, object, style);
	}
	for (const quader_poly::Edge edge : quader_poly::document_edges(object.document)) {
		const quader_poly::Vertex* a = quader_poly::find_vertex(object.document, edge.a);
		const quader_poly::Vertex* b = quader_poly::find_vertex(object.document, edge.b);
		if (a == nullptr || b == nullptr) {
			continue;
		}
                append_source_wire_line(frame, a->position, b->position,
                    style.source_wire_color_abgr, style.edge_line_width_pixels,
                    object.id, quader_poly::kInvalidElementId, edge);
        }
}

void append_hover_face_overlay(
		SelectionOverlayFrame& frame,
		int object_id,
		const quader_poly::Document& document,
		quader_poly::ElementId face_id,
		bool selected_hover_target,
		const SelectionOverlayStyle& style)
{
	quader_poly::Selection hover_selection;
        hover_selection.mode = quader_poly::SelectionMode::Face;
        hover_selection.faces.push_back(face_id);
	const quader_poly::OverlayData overlay = quader_poly::build_overlay(document, hover_selection);
	const std::uint32_t triangle_color =
		selected_hover_target ? style.hover_selected_face_color_abgr : style.hover_face_color_abgr;
	for (const quader_poly::OverlayTriangle& triangle : overlay.triangles) {
          frame.triangles.push_back({
              SelectionOverlayLayer::HoverFace,
              triangle.a,
              triangle.b,
              triangle.c,
              triangle_color,
              object_id,
              face_id,
          });
        }

	const quader_poly::Face* face = quader_poly::find_face(document, face_id);
	if (face == nullptr || face->vertices.size() < 2) {
		return;
	}
	const std::uint32_t line_color =
		selected_hover_target ? style.hover_selected_line_color_abgr : style.hover_face_edge_color_abgr;
	for (std::size_t index = 0; index < face->vertices.size(); ++index) {
		const quader_poly::Vertex* a = quader_poly::find_vertex(document, face->vertices[index]);
		const quader_poly::Vertex* b = quader_poly::find_vertex(document, face->vertices[(index + 1U) % face->vertices.size()]);
		if (a == nullptr || b == nullptr) {
			continue;
		}
                frame.lines.push_back({
                    SelectionOverlayLayer::HoverFaceEdge,
                    a->position,
                    b->position,
                    line_color,
                    style.edge_line_width_pixels,
                    object_id,
                    face_id,
                    quader_poly::make_edge(
                        face->vertices[index],
                        face->vertices[(index + 1U) % face->vertices.size()]),
                });
        }
}

void append_hover_edge_overlay(
		SelectionOverlayFrame& frame,
		int object_id,
		const quader_poly::Document& document,
		quader_poly::Edge edge,
		bool selected_hover_target,
		const SelectionOverlayStyle& style)
{
	const quader_poly::Vertex* a = quader_poly::find_vertex(document, edge.a);
	const quader_poly::Vertex* b = quader_poly::find_vertex(document, edge.b);
	if (a == nullptr || b == nullptr) {
		return;
	}
        frame.lines.push_back({
            SelectionOverlayLayer::HoverEdge,
            a->position,
            b->position,
            selected_hover_target ? style.hover_selected_line_color_abgr
                                  : style.hover_edge_color_abgr,
            style.single_edge_line_width_pixels,
            object_id,
            quader_poly::kInvalidElementId,
            edge,
        });
}

void append_hover_vertex_overlay(
		SelectionOverlayFrame& frame,
		int object_id,
		const quader_poly::Document& document,
		quader_poly::ElementId vertex_id,
		bool selected_hover_target,
		const SelectionOverlayStyle& style)
{
	const quader_poly::Vertex* vertex = quader_poly::find_vertex(document, vertex_id);
	if (vertex == nullptr) {
		return;
	}
        frame.points.push_back({
            SelectionOverlayLayer::HoverVertex,
            vertex->position,
            selected_hover_target ? style.hover_selected_line_color_abgr
                                  : style.hover_vertex_color_abgr,
            style.vertex_point_size_pixels +
                style.selected_hover_point_growth_pixels,
            object_id,
            vertex_id,
        });
}

bool object_has_sticky_component_source_wire(const PolygonObject &object,
                                             SelectionMode mode)
{
  return object.selected && object.id > 0 &&
         component_selection_has_mode_components(object.selection, mode);
}

int inferred_component_source_wire_object_id(
    std::span<const PolygonObject> objects, SelectionMode mode,
    int active_object_id, const SelectionHoverState &hover)
{
  if (mode == SelectionMode::Object) {
    return -1;
  }
  if (active_object_id > 0) {
    for (const PolygonObject &object : objects) {
      if (object.id == active_object_id &&
          object_has_sticky_component_source_wire(object, mode)) {
        return object.id;
      }
    }
  }
  for (const PolygonObject &object : objects) {
    if (object_has_sticky_component_source_wire(object, mode)) {
      return object.id;
    }
  }
  if (hover.candidate_object_id > 0) {
    return hover.candidate_object_id;
  }
  if (active_object_id > 0) {
    for (const PolygonObject &object : objects) {
      if (object.id == active_object_id && object.selected && object.id > 0) {
        return object.id;
      }
    }
  }
  for (const PolygonObject &object : objects) {
    if (object.selected && object.id > 0) {
      return object.id;
    }
  }
  return -1;
}

} // namespace

SelectionOverlayFrame build_selection_overlay_frame(
		std::span<const PolygonObject> objects,
		SelectionMode mode,
		int active_object_id,
		const SelectionHoverState& hover,
		const SelectionOverlayStyle& style)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.build_selection_overlay_frame");
	SelectionOverlayFrame frame;
	const bool selected_hover_target = selection_hover_target_matches_selection(objects, mode, hover);
	frame.hover_target_selected = hover.selected_modifier_pressed && selected_hover_target;

        auto append_line =
            [&](SelectionOverlayLayer layer, const quader::QVec3 &a,
                const quader::QVec3 &b, std::uint32_t color, float width,
                int object_id = -1,
                quader_poly::ElementId face_id = quader_poly::kInvalidElementId,
                quader_poly::Edge edge = {}) {
              frame.lines.push_back(
                  {layer, a, b, color, width, object_id, face_id, edge});
            };

        auto append_point =
            [&](SelectionOverlayLayer layer, const quader::QVec3 &position,
                std::uint32_t color, float size, int object_id = -1,
                quader_poly::ElementId vertex_id =
                    quader_poly::kInvalidElementId) {
              frame.points.push_back(
                  {layer, position, color, size, object_id, vertex_id});
            };

        auto append_open_edge_overlay = [&](int object_id, const quader_poly::Document& document, const std::vector<quader_poly::Edge>& selected_edges) {
		if (selected_edges.empty()) {
			return;
		}
                constexpr float kEdgeEpsilon = 0.00001F;
                constexpr float kOpenEdgeTickSpacing = 0.035F;
                constexpr int kOpenEdgeTickLimit = 256;
                std::unordered_map<std::uint64_t, int> incidence_counts;
		std::unordered_map<std::uint64_t, OpenEdgeOverlayRef> open_edge_refs;
		incidence_counts.reserve(document.faces.size() * 4U);
		open_edge_refs.reserve(document.faces.size() * 4U);

		for (const quader_poly::Face& face : document.faces) {
			if (face.vertices.size() < 2) {
				continue;
			}
			const quader::QVec3 centroid = overlay_face_centroid(document, face);
			const quader::QVec3 normal = quader_poly::face_normal(document, face);
			for (std::size_t index = 0; index < face.vertices.size(); ++index) {
				const quader_poly::ElementId a_id = face.vertices[index];
				const quader_poly::ElementId b_id = face.vertices[(index + 1U) % face.vertices.size()];
				const std::uint64_t edge_key = overlay_face_edge_key(a_id, b_id);
				incidence_counts[edge_key] += 1;
				OpenEdgeOverlayRef& edge_ref = open_edge_refs[edge_key];
				if (!edge_ref.has_face) {
					edge_ref.edge = quader_poly::make_edge(a_id, b_id);
					edge_ref.face_centroid = centroid;
					edge_ref.face_normal = normal;
					edge_ref.has_face = true;
				}
			}
		}

		std::unordered_set<std::uint64_t> emitted_selected_open_edges;
		emitted_selected_open_edges.reserve(selected_edges.size());
		for (const quader_poly::Edge selected_edge : selected_edges) {
			const std::uint64_t edge_key = overlay_face_edge_key(selected_edge.a, selected_edge.b);
			if (!emitted_selected_open_edges.insert(edge_key).second) {
				continue;
			}
			const auto ref_it = open_edge_refs.find(edge_key);
			if (ref_it == open_edge_refs.end()) {
				continue;
			}
			const auto count_it = incidence_counts.find(edge_key);
			if (count_it == incidence_counts.end() || count_it->second != 1) {
				continue;
			}
			const OpenEdgeOverlayRef& edge_ref = ref_it->second;
			const quader_poly::Vertex* a = quader_poly::find_vertex(document, edge_ref.edge.a);
			const quader_poly::Vertex* b = quader_poly::find_vertex(document, edge_ref.edge.b);
			if (a == nullptr || b == nullptr) {
				continue;
			}
                        append_line(
                            SelectionOverlayLayer::OpenEdge, a->position,
                            b->position, style.open_edge_color_abgr,
                            style.open_edge_line_width_pixels, object_id,
                            quader_poly::kInvalidElementId, edge_ref.edge);

                        const quader::QVec3 edge_vector = b->position - a->position;
			const float edge_length = quader::length(edge_vector);
                        if (edge_length <= kEdgeEpsilon) {
                          continue;
                        }
                        const quader::QVec3 tangent = edge_vector / edge_length;
			const quader::QVec3 midpoint = (a->position + b->position) * 0.5F;
			quader::QVec3 tick_direction = edge_ref.face_centroid - midpoint;
			tick_direction = tick_direction - tangent * quader::dot(tick_direction, tangent);
                        if (quader::length_squared(tick_direction) <=
                            kEdgeEpsilon * kEdgeEpsilon) {
                          tick_direction =
                              quader::cross(edge_ref.face_normal, tangent);
                        }
                        if (quader::length_squared(tick_direction) <=
                            kEdgeEpsilon * kEdgeEpsilon) {
                          continue;
                        }
                        tick_direction = quader::normalize_or_zero(tick_direction);
                        const int tick_count =
                            std::clamp(static_cast<int>(std::floor(
                                           edge_length / kOpenEdgeTickSpacing)),
                                       1, kOpenEdgeTickLimit);
                        const float tick_length = std::clamp(edge_length * 0.018F, 0.012F, 0.04F);
			for (int tick_index = 0; tick_index < tick_count; ++tick_index) {
				const float t = (static_cast<float>(tick_index) + 0.5F) / static_cast<float>(tick_count);
				const quader::QVec3 tick_start = a->position + edge_vector * t;
                                append_line(
                                    SelectionOverlayLayer::OpenEdge, tick_start,
                                    tick_start + tick_direction * tick_length,
                                    style.open_edge_color_abgr,
                                    style.open_edge_line_width_pixels,
                                    object_id, quader_poly::kInvalidElementId,
                                    edge_ref.edge);
                        }
		}
	};

	auto append_face_selection = [&](int object_id,
									 const quader_poly::Document& document,
									 const quader_poly::Selection& selection,
									 SelectionOverlayLayer triangle_layer,
									 SelectionOverlayLayer line_layer,
									 std::vector<SelectionOverlayTriangle>* triangles,
									 std::vector<SelectionOverlayLine>* lines,
									 const std::unordered_set<std::uint64_t>* hidden_line_edges = nullptr,
									 const std::unordered_set<quader_poly::ElementId>* hidden_triangle_faces = nullptr) {
		QDR_PROFILE_SCOPE("qdr_editor_core.build_selection_overlay_frame.append_face_selection");
		if (triangles != nullptr) {
			quader_poly::Selection triangle_selection = selection;
			if (hidden_triangle_faces != nullptr && !hidden_triangle_faces->empty()) {
				triangle_selection.faces.clear();
				triangle_selection.faces.reserve(selection.faces.size());
				for (const quader_poly::ElementId face_id : selection.faces) {
					if (hidden_triangle_faces->find(face_id) == hidden_triangle_faces->end()) {
						triangle_selection.faces.push_back(face_id);
					}
				}
			}
			const quader_poly::OverlayData overlay = quader_poly::build_overlay(document, triangle_selection);
			for (const quader_poly::OverlayTriangle& triangle : overlay.triangles) {
                          triangles->push_back({
                              triangle_layer,
                              triangle.a,
                              triangle.b,
                              triangle.c,
                              triangle_layer == SelectionOverlayLayer::HoverFace
                                  ? (frame.hover_target_selected
                                         ? style.hover_selected_face_color_abgr
                                         : style.hover_face_color_abgr)
                                  : style.selection_face_color_abgr,
                              object_id,
                              triangle.face_id,
                          });
                        }
		}

		if (lines == nullptr) {
			return;
		}
		std::unordered_set<std::uint64_t> emitted_edges;
		emitted_edges.reserve(selection.faces.size() * 4U);
		for (const quader_poly::ElementId face_id : selection.faces) {
			const quader_poly::Face* face = quader_poly::find_face(document, face_id);
			if (face == nullptr || face->vertices.size() < 2) {
				continue;
			}
			for (std::size_t index = 0; index < face->vertices.size(); ++index) {
				const quader_poly::ElementId a_id = face->vertices[index];
				const quader_poly::ElementId b_id = face->vertices[(index + 1U) % face->vertices.size()];
				const std::uint64_t edge_key = overlay_face_edge_key(a_id, b_id);
				if (hidden_line_edges != nullptr && hidden_line_edges->find(edge_key) != hidden_line_edges->end()) {
					continue;
				}
				if (!emitted_edges.insert(edge_key).second) {
					continue;
				}
				const quader_poly::Vertex* a = quader_poly::find_vertex(document, a_id);
				const quader_poly::Vertex* b = quader_poly::find_vertex(document, b_id);
				if (a == nullptr || b == nullptr) {
					continue;
				}
                                lines->push_back({
                                    line_layer,
                                    a->position,
                                    b->position,
                                    line_layer ==
                                            SelectionOverlayLayer::HoverFaceEdge
                                        ? (frame.hover_target_selected
                                               ? style
                                                     .hover_selected_line_color_abgr
                                               : style
                                                     .hover_face_edge_color_abgr)
                                        : style.selected_edge_color_abgr,
                                    style.edge_line_width_pixels,
                                    object_id,
                                    face_id,
                                    quader_poly::make_edge(a_id, b_id),
                                });
                        }
		}
	};

        const int component_source_wire_object_id =
            inferred_component_source_wire_object_id(objects, mode,
                                                     active_object_id, hover);
	auto object_draws_source_wire = [&](const PolygonObject& object) {
		if (mode == SelectionMode::Object) {
			return object.selected;
		}
		return object.id > 0 && object.id == component_source_wire_object_id;
	};

	for (const PolygonObject& object : objects) {
		if (!object_draws_source_wire(object)) {
			continue;
		}
                if (mode != SelectionMode::Object) {
                  append_hover_source_wire(frame, object, style, true);
                  continue;
                }
                for (const quader_poly::Edge edge : quader_poly::document_edges(object.document)) {
			const quader_poly::Vertex* a = quader_poly::find_vertex(object.document, edge.a);
			const quader_poly::Vertex* b = quader_poly::find_vertex(object.document, edge.b);
			if (a != nullptr && b != nullptr) {
                          if (mode == SelectionMode::Object &&
                              object.selected) {
                            append_line(SelectionOverlayLayer::SelectedEdge,
                                        a->position, b->position,
                                        style.selected_edge_color_abgr,
                                        style.edge_line_width_pixels, object.id,
                                        quader_poly::kInvalidElementId, edge);
                          } else {
                            append_line(SelectionOverlayLayer::SourceWire,
                                        a->position, b->position,
                                        style.source_wire_color_abgr,
                                        style.edge_line_width_pixels, object.id,
                                        quader_poly::kInvalidElementId, edge);
                          }
                        }
		}
	}

	for (const PolygonObject& object : objects) {
          const bool vertex_candidate_object =
              !object.selected && mode == SelectionMode::Vertex &&
              object.id > 0 && object.id == hover.candidate_object_id;
          if (object.selected || vertex_candidate_object) {
            if (mode == SelectionMode::Object) {
              quader_poly::Selection selection =
                  overlay_all_face_selection(object.document);
              append_face_selection(object.id, object.document, selection,
                                    SelectionOverlayLayer::SelectedXrayFace,
                                    SelectionOverlayLayer::SelectedFaceEdge,
                                    &frame.triangles, nullptr);
              append_face_selection(object.id, object.document, selection,
                                    SelectionOverlayLayer::SelectedFaceMask,
                                    SelectionOverlayLayer::SelectedFaceEdge,
                                    &frame.triangles, nullptr);
            } else if (mode == SelectionMode::Face) {
              std::unordered_set<std::uint64_t> hidden_hover_line_edges;
              std::unordered_set<quader_poly::ElementId>
                  hidden_hover_mask_faces;
              if (frame.hover_target_selected && object.id == hover.object_id &&
                  hover.kind == SelectionHoverKind::Face &&
                  quader_poly::selection_contains_face(object.selection,
                                                       hover.face_id)) {
                hidden_hover_mask_faces.insert(hover.face_id);
                overlay_append_face_edges_to_set(object.document, hover.face_id,
                                                 hidden_hover_line_edges);
              }
              const std::unordered_set<std::uint64_t> *hidden_line_edges =
                  hidden_hover_line_edges.empty() ? nullptr
                                                  : &hidden_hover_line_edges;
              const std::unordered_set<quader_poly::ElementId>
                  *hidden_triangle_faces = hidden_hover_mask_faces.empty()
                                               ? nullptr
                                               : &hidden_hover_mask_faces;
              append_face_selection(
                  object.id, object.document, object.selection,
                  SelectionOverlayLayer::SelectedVisibleFace,
                  SelectionOverlayLayer::SelectedFaceEdge, &frame.triangles,
                  &frame.lines, hidden_line_edges, hidden_triangle_faces);
              append_face_selection(
                  object.id, object.document, object.selection,
                  SelectionOverlayLayer::SelectedFaceMask,
                  SelectionOverlayLayer::SelectedFaceEdge, &frame.triangles,
                  nullptr, nullptr, hidden_triangle_faces);
              append_face_selection(object.id, object.document,
                                    object.selection,
                                    SelectionOverlayLayer::SelectedVisibleFace,
                                    SelectionOverlayLayer::SelectedEdge,
                                    nullptr, &frame.lines, hidden_line_edges);
            } else if (mode == SelectionMode::Edge) {
              append_open_edge_overlay(object.id, object.document,
                                       object.selection.edges);
              for (const quader_poly::Edge edge : object.selection.edges) {
                if (frame.hover_target_selected &&
                    object.id == hover.object_id &&
                    hover.kind == SelectionHoverKind::Edge &&
                    quader_poly::selection_contains(object.selection,
                                                    hover.edge) &&
                    edge == hover.edge) {
                  continue;
                }
                const quader_poly::Vertex *a =
                    quader_poly::find_vertex(object.document, edge.a);
                const quader_poly::Vertex *b =
                    quader_poly::find_vertex(object.document, edge.b);
                if (a != nullptr && b != nullptr) {
                  append_line(SelectionOverlayLayer::SelectedEdge, a->position,
                              b->position, style.selected_edge_color_abgr,
                              style.single_edge_line_width_pixels, object.id,
                              quader_poly::kInvalidElementId, edge);
                }
              }
            } else if (mode == SelectionMode::Vertex) {
              const bool suppress_hover_vertex =
                  frame.hover_target_selected && object.id == hover.object_id &&
                  hover.kind == SelectionHoverKind::Vertex &&
                  quader_poly::selection_contains(object.selection,
                                                  hover.vertex_id);
              for (const quader_poly::Vertex &vertex :
                   object.document.vertices) {
                if (!object.selected) {
                  append_point(SelectionOverlayLayer::UnselectedVertex,
                               vertex.position, style.handle_color_abgr,
                               style.vertex_point_size_pixels, object.id,
                               vertex.id);
                } else if (quader_poly::selection_contains(object.selection,
                                                           vertex.id)) {
                  if (!suppress_hover_vertex || vertex.id != hover.vertex_id) {
                    append_point(SelectionOverlayLayer::SelectedVertex,
                                 vertex.position,
                                 style.selected_handle_color_abgr,
                                 style.vertex_point_size_pixels +
                                     style.selected_hover_point_growth_pixels,
                                 object.id, vertex.id);
                  }
                } else {
                  append_point(SelectionOverlayLayer::UnselectedVertex,
                               vertex.position, style.handle_color_abgr,
                               style.vertex_point_size_pixels, object.id,
                               vertex.id);
                }
              }
            }
                }

                if (object.id != hover.object_id ||
                    hover.kind == SelectionHoverKind::None) {
                  continue;
                }
                if (hover.kind == SelectionHoverKind::Face) {
                  quader_poly::Selection hover_selection;
                  hover_selection.mode = quader_poly::SelectionMode::Face;
                  hover_selection.faces.push_back(hover.face_id);
                  append_face_selection(object.id, object.document,
                                        hover_selection,
                                        SelectionOverlayLayer::HoverFace,
                                        SelectionOverlayLayer::HoverFaceEdge,
                                        &frame.triangles, &frame.lines);
                } else if (hover.kind == SelectionHoverKind::Edge) {
                  const quader_poly::Vertex *a =
                      quader_poly::find_vertex(object.document, hover.edge.a);
                  const quader_poly::Vertex *b =
                      quader_poly::find_vertex(object.document, hover.edge.b);
                  if (a != nullptr && b != nullptr) {
                    append_line(SelectionOverlayLayer::HoverEdge, a->position,
                                b->position,
                                frame.hover_target_selected
                                    ? style.hover_selected_line_color_abgr
                                    : style.hover_edge_color_abgr,
                                style.single_edge_line_width_pixels, object.id,
                                quader_poly::kInvalidElementId, hover.edge);
                  }
                } else if (hover.kind == SelectionHoverKind::Vertex) {
                  const quader_poly::Vertex *vertex = quader_poly::find_vertex(
                      object.document, hover.vertex_id);
                  if (vertex != nullptr) {
                    append_point(SelectionOverlayLayer::HoverVertex,
                                 vertex->position,
                                 frame.hover_target_selected
                                     ? style.hover_selected_line_color_abgr
                                     : style.hover_vertex_color_abgr,
                                 style.vertex_point_size_pixels +
                                     style.selected_hover_point_growth_pixels,
                                 object.id, hover.vertex_id);
                  }
                }
        }

	return frame;
}

SelectionOverlayFrame build_selection_overlay_frame(
		std::span<const PolygonObject> objects,
		SelectionMode mode,
		const SelectionHoverState& hover,
		const SelectionOverlayStyle& style)
{
	return build_selection_overlay_frame(
			objects, mode,
			inferred_component_source_wire_object_id(objects, mode, -1, hover),
			hover, style);
}

SelectionOverlayFrame build_selection_overlay_frame(
		const std::vector<PolygonObject>& objects,
		SelectionMode mode,
		int active_object_id,
		const SelectionHoverState& hover,
		const SelectionOverlayStyle& style)
{
	return build_selection_overlay_frame(
			std::span<const PolygonObject>(objects), mode, active_object_id,
			hover, style);
}

SelectionOverlayFrame build_selection_overlay_frame(
		const std::vector<PolygonObject>& objects,
		SelectionMode mode,
		const SelectionHoverState& hover,
		const SelectionOverlayStyle& style)
{
	return build_selection_overlay_frame(std::span<const PolygonObject>(objects), mode, hover, style);
}

SelectionOverlayFrame build_selection_hover_overlay_frame(
		std::span<const PolygonObject> objects,
		SelectionMode mode,
		const SelectionHoverState& hover,
		const SelectionOverlayStyle& style)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.build_selection_hover_overlay_frame");
	SelectionOverlayFrame frame;
	const bool selected_hover_target = selection_hover_target_matches_selection(objects, mode, hover);
	frame.hover_target_selected = hover.selected_modifier_pressed && selected_hover_target;

	for (const PolygonObject& object : objects) {
          const bool component_candidate =
              mode != SelectionMode::Object && hover.candidate_object_id > 0 &&
              object.id == hover.candidate_object_id;
          if (!object.selected && component_candidate) {
            append_hover_source_wire(frame, object, style, component_candidate);
            if (mode == SelectionMode::Vertex) {
              for (const quader_poly::Vertex &vertex :
                   object.document.vertices) {
                frame.points.push_back({
                    SelectionOverlayLayer::UnselectedVertex,
                    vertex.position,
                    style.handle_color_abgr,
                    style.vertex_point_size_pixels,
                    object.id,
                    vertex.id,
                });
              }
            }
                }

                if (object.id != hover.object_id ||
                    hover.kind == SelectionHoverKind::None) {
                  continue;
                }

                if (hover.kind == SelectionHoverKind::Face) {
                  append_hover_face_overlay(frame, object.id, object.document,
                                            hover.face_id,
                                            frame.hover_target_selected, style);
                } else if (hover.kind == SelectionHoverKind::Edge) {
                  append_hover_edge_overlay(frame, object.id, object.document,
                                            hover.edge,
                                            frame.hover_target_selected, style);
                } else if (hover.kind == SelectionHoverKind::Vertex) {
                  append_hover_vertex_overlay(
                      frame, object.id, object.document, hover.vertex_id,
                      frame.hover_target_selected, style);
                }
        }

	return frame;
}

SelectionOverlayFrame build_selection_hover_overlay_frame(
		const std::vector<PolygonObject>& objects,
		SelectionMode mode,
		const SelectionHoverState& hover,
		const SelectionOverlayStyle& style)
{
	return build_selection_hover_overlay_frame(std::span<const PolygonObject>(objects), mode, hover, style);
}

SelectionOverlayFrame build_diagnostic_edge_overlay_frame(
		std::span<const PolygonObject> objects,
		const SelectionOverlayStyle& style)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.build_diagnostic_edge_overlay_frame");
	std::vector<DiagnosticEdgeRef> edges;
	for (const PolygonObject& object : objects) {
		for (const quader_poly::Edge edge : quader_poly::document_edges(object.document)) {
			const quader_poly::Vertex* start_vertex = quader_poly::find_vertex(object.document, edge.a);
			const quader_poly::Vertex* end_vertex = quader_poly::find_vertex(object.document, edge.b);
			if (start_vertex == nullptr || end_vertex == nullptr) {
				continue;
			}
			const quader_geometry::QVec3d start = to_diagnostic_vec3(start_vertex->position);
			const quader_geometry::QVec3d end = to_diagnostic_vec3(end_vertex->position);
                        if (quader_geometry::length_squared(end - start) <=
                            kDiagnosticEdgeEpsilon * kDiagnosticEdgeEpsilon) {
                          continue;
                        }
                        edges.push_back({
				object.id,
				edge,
				start_vertex->position,
				end_vertex->position,
				{ start, end },
				diagnostic_edge_bounds(start, end),
			});
		}
	}

	std::vector<bool> marked_edges(edges.size(), false);
	for (std::size_t first_index = 0; first_index < edges.size(); ++first_index) {
		for (std::size_t second_index = first_index + 1U; second_index < edges.size(); ++second_index) {
			if (!diagnostic_edges_conflict(edges[first_index], edges[second_index])) {
				continue;
			}
			marked_edges[first_index] = true;
			marked_edges[second_index] = true;
		}
	}

	SelectionOverlayFrame frame;
	frame.lines.reserve(edges.size());
	for (std::size_t index = 0; index < edges.size(); ++index) {
		if (!marked_edges[index]) {
			continue;
		}
		const DiagnosticEdgeRef& edge = edges[index];
                frame.lines.push_back({
                    SelectionOverlayLayer::DiagnosticEdge,
                    edge.start,
                    edge.end,
                    style.diagnostic_edge_color_abgr,
                    style.diagnostic_edge_line_width_pixels,
                    edge.object_id,
                    quader_poly::kInvalidElementId,
                    edge.edge,
                });
        }
	return frame;
}

SelectionOverlayFrame build_diagnostic_edge_overlay_frame(
		const std::vector<PolygonObject>& objects,
		const SelectionOverlayStyle& style)
{
	return build_diagnostic_edge_overlay_frame(std::span<const PolygonObject>(objects), style);
}

} // namespace quader_editor_core

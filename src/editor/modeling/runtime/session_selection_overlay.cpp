////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/modeling/runtime/session.hpp>
#include <quader/modeling/neutral/geometry/geometry.hpp>
#include <quader/modeling/neutral/polygon/document.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_mesh_internal.hpp>

#include <diagnostics/profile.hpp>

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
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
	const quader_poly::Face *face = nullptr;
	int incidence_count = 0;
};

/**
 * Indexes polygon element IDs for repeated lookups within one overlay build.
 */
class SelectionOverlayDocumentLookup {
public:
	explicit SelectionOverlayDocumentLookup(
		const quader_poly::Document& document, bool include_faces = true)
		: document_(&document)
	{
		dense_vertices_ = true;
		for (std::size_t index = 0U; index < document.vertices.size(); ++index) {
			if (document.vertices[index].id != index + 1U) {
				dense_vertices_ = false;
				break;
			}
		}
		if (!dense_vertices_) {
			vertices_by_id_.reserve(document.vertices.size());
			for (const quader_poly::Vertex& vertex : document.vertices) {
				vertices_by_id_.try_emplace(vertex.id, &vertex);
			}
		}
		if (!include_faces) {
			return;
		}
		dense_faces_ = true;
		for (std::size_t index = 0U; index < document.faces.size(); ++index) {
			if (document.faces[index].id != index + 1U) {
				dense_faces_ = false;
				break;
			}
		}
		if (!dense_faces_) {
			faces_by_id_.reserve(document.faces.size());
			for (const quader_poly::Face& face : document.faces) {
				faces_by_id_.try_emplace(face.id, &face);
			}
		}
	}

	[[nodiscard]] const quader_poly::Vertex* vertex(quader_poly::ElementId id) const
	{
		if (dense_vertices_ && id != quader_poly::kInvalidElementId &&
			id <= document_->vertices.size()) {
			return &document_->vertices[static_cast<std::size_t>(id - 1U)];
		}
		const auto found = vertices_by_id_.find(id);
		return found == vertices_by_id_.end() ? nullptr : found->second;
	}

	[[nodiscard]] const quader_poly::Face* face(quader_poly::ElementId id) const
	{
		if (dense_faces_ && id != quader_poly::kInvalidElementId &&
			id <= document_->faces.size()) {
			return &document_->faces[static_cast<std::size_t>(id - 1U)];
		}
		const auto found = faces_by_id_.find(id);
		return found == faces_by_id_.end() ? nullptr : found->second;
	}

private:
	const quader_poly::Document *document_ = nullptr;
	bool dense_vertices_ = false;
	bool dense_faces_ = false;
	std::unordered_map<quader_poly::ElementId, const quader_poly::Vertex*> vertices_by_id_;
	std::unordered_map<quader_poly::ElementId, const quader_poly::Face*> faces_by_id_;
};

quader::QVec3 overlay_face_centroid(
		const SelectionOverlayDocumentLookup& lookup,
		const quader_poly::Face& face)
{
	quader::QVec3 centroid;
	int count = 0;
	for (const quader_poly::ElementId vertex_id : face.vertices) {
		const quader_poly::Vertex* vertex = lookup.vertex(vertex_id);
		if (vertex == nullptr) {
			continue;
		}
		centroid += vertex->position;
		++count;
	}
	return count == 0 ? quader::QVec3 {} : centroid / static_cast<float>(count);
}

std::optional<quader::QVec3> overlay_edge_inside_reference(
    const quader_poly::Document &document,
    const SelectionOverlayDocumentLookup &lookup,
    quader_poly::ElementId face_id, quader_poly::Edge edge) {
  const quader_poly::Face *face = lookup.face(face_id);
  if (face == nullptr || face->vertices.size() < 3U) {
    return std::nullopt;
  }

  constexpr float kDirectionEpsilonSquared = 1.0e-10F;
  const quader::QVec3 face_normal = quader_poly::face_normal(document, *face);
  if (quader::length_squared(face_normal) <= kDirectionEpsilonSquared) {
    return std::nullopt;
  }
  for (std::size_t index = 0U; index < face->vertices.size(); ++index) {
    const quader_poly::ElementId start_id = face->vertices[index];
    const quader_poly::ElementId end_id =
        face->vertices[(index + 1U) % face->vertices.size()];
    if (quader_poly::make_edge(start_id, end_id) != edge) {
      continue;
    }
    const quader_poly::Vertex *start = lookup.vertex(start_id);
    const quader_poly::Vertex *end = lookup.vertex(end_id);
    if (start == nullptr || end == nullptr) {
      return std::nullopt;
    }
    const quader::QVec3 directed_edge = end->position - start->position;
    const float edge_length = quader::length(directed_edge);
    if (edge_length * edge_length <= kDirectionEpsilonSquared) {
      return std::nullopt;
    }
    const quader::QVec3 tangent = directed_edge / edge_length;
    const quader::QVec3 inward =
        quader::normalize_or_zero(quader::cross(face_normal, tangent));
    if (quader::length_squared(inward) <= kDirectionEpsilonSquared) {
      return std::nullopt;
    }
    const quader::QVec3 midpoint =
        (start->position + end->position) * 0.5F;
    return midpoint + inward * (edge_length * 0.25F);
  }
  return std::nullopt;
}

void overlay_append_face_edges_to_set(
		const SelectionOverlayDocumentLookup& lookup,
		quader_poly::ElementId face_id,
		std::unordered_set<std::uint64_t>& edges)
{
	const quader_poly::Face* face = lookup.face(face_id);
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
		const SelectionOverlayDocumentLookup& lookup,
		const SelectionOverlayStyle& style)
{
	for (const quader_poly::Face& face : object.document.faces) {
		if (face.vertices.size() < 2) {
			continue;
		}
		for (std::size_t index = 0; index < face.vertices.size(); ++index) {
			const quader_poly::ElementId a_id = face.vertices[index];
			const quader_poly::ElementId b_id = face.vertices[(index + 1U) % face.vertices.size()];
			const quader_poly::Vertex* a = lookup.vertex(a_id);
			const quader_poly::Vertex* b = lookup.vertex(b_id);
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
		const SelectionOverlayDocumentLookup& lookup,
		const SelectionOverlayStyle& style,
		bool stamp_depth = false)
{
	std::unordered_set<std::uint64_t> emitted_edges;
	emitted_edges.reserve(object.document.faces.size() * 4U);
	for (const quader_poly::Face &face : object.document.faces) {
		if (stamp_depth) {
			const std::vector<quader_poly::document_internal::Triangle> triangles =
				quader_poly::document_internal::triangulate_face_local_indices(
					object.document, face);
			for (const quader_poly::document_internal::Triangle &triangle :
				 triangles) {
				const quader_poly::Vertex *a =
					lookup.vertex(face.vertices[triangle.a]);
				const quader_poly::Vertex *b =
					lookup.vertex(face.vertices[triangle.b]);
				const quader_poly::Vertex *c =
					lookup.vertex(face.vertices[triangle.c]);
				if (a == nullptr || b == nullptr || c == nullptr) {
					continue;
				}
				// SourceWireDepthStamp remains non-rendered visibility metadata.
				// Emit it from the same direct face walk as the source wire so we do
				// not rebuild a second polygon overlay, and retain authored vertex IDs
				// for position-only transform streaming.
				frame.triangles.push_back({
					SelectionOverlayLayer::SourceWireDepthStamp,
					a->position,
					b->position,
					c->position,
					style.source_wire_color_abgr,
					object.id,
					face.id,
					a->id,
					b->id,
					c->id,
				});
			}
		}
		for (std::size_t index = 0U; index < face.vertices.size(); ++index) {
			const quader_poly::ElementId a_id = face.vertices[index];
			const quader_poly::ElementId b_id =
				face.vertices[(index + 1U) % face.vertices.size()];
			if (!emitted_edges.insert(overlay_face_edge_key(a_id, b_id)).second) {
				continue;
			}
			const quader_poly::Vertex *a = lookup.vertex(a_id);
			const quader_poly::Vertex *b = lookup.vertex(b_id);
			if (a == nullptr || b == nullptr) {
				continue;
			}
			append_source_wire_line(
				frame, a->position, b->position, style.source_wire_color_abgr,
				style.edge_line_width_pixels, object.id,
				quader_poly::kInvalidElementId,
				quader_poly::make_edge(a_id, b_id));
		}
	}
}

void append_hover_face_overlay(
		SelectionOverlayFrame& frame,
		int object_id,
		const quader_poly::Document& document,
		const SelectionOverlayDocumentLookup& lookup,
		quader_poly::ElementId face_id,
		bool selected_hover_target,
                bool face_plane_hover,
		const SelectionOverlayStyle& style)
{
	const quader_poly::Face* face = lookup.face(face_id);
	if (face == nullptr) {
		return;
	}
        const std::uint32_t triangle_color =
            face_plane_hover
                ? style.face_plane_hover_face_color_abgr
                : (selected_hover_target ? style.hover_selected_face_color_abgr
                                         : style.hover_face_color_abgr);
	const std::vector<quader_poly::document_internal::Triangle> triangles =
		quader_poly::document_internal::triangulate_face_local_indices(document,
															 *face);
	for (const quader_poly::document_internal::Triangle &triangle : triangles) {
		const quader_poly::Vertex *a =
			lookup.vertex(face->vertices[triangle.a]);
		const quader_poly::Vertex *b =
			lookup.vertex(face->vertices[triangle.b]);
		const quader_poly::Vertex *c =
			lookup.vertex(face->vertices[triangle.c]);
		if (a == nullptr || b == nullptr || c == nullptr) {
			continue;
		}
          frame.triangles.push_back({
              SelectionOverlayLayer::HoverFace,
              a->position,
              b->position,
              c->position,
              triangle_color,
              object_id,
              face_id,
              a->id,
              b->id,
              c->id,
          });
        }

	if (face->vertices.size() < 2) {
		return;
	}
        const std::uint32_t line_color =
            face_plane_hover
                ? style.face_plane_hover_line_color_abgr
                : (selected_hover_target ? style.hover_selected_line_color_abgr
                                         : style.hover_face_edge_color_abgr);
	for (std::size_t index = 0; index < face->vertices.size(); ++index) {
		const quader_poly::Vertex* a = lookup.vertex(face->vertices[index]);
		const quader_poly::Vertex* b = lookup.vertex(face->vertices[(index + 1U) % face->vertices.size()]);
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
		const SelectionOverlayDocumentLookup& lookup,
		quader_poly::Edge edge,
		quader_poly::ElementId face_id,
		bool selected_hover_target,
		const SelectionOverlayStyle& style)
{
	const quader_poly::Vertex* a = lookup.vertex(edge.a);
	const quader_poly::Vertex* b = lookup.vertex(edge.b);
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
            face_id,
            edge,
        });
        if (const std::optional<quader::QVec3> inside_reference =
                overlay_edge_inside_reference(document, lookup, face_id,
                                              edge)) {
          frame.hover_edge_dimension_context =
              SelectionHoverEdgeDimensionContext{
                  .inside_reference = *inside_reference,
                  .object_id = object_id,
                  .face_id = face_id,
                  .edge = edge,
              };
        }
}

void append_hover_vertex_overlay(
		SelectionOverlayFrame& frame,
		int object_id,
		const SelectionOverlayDocumentLookup& lookup,
		quader_poly::ElementId vertex_id,
		bool selected_hover_target,
		const SelectionOverlayStyle& style)
{
	const quader_poly::Vertex* vertex = lookup.vertex(vertex_id);
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
  return mode != SelectionMode::Object && object.selected && object.id > 0 &&
         (object.component_edit_target ||
          component_selection_has_any_components(object.selection));
}

std::vector<int> inferred_component_source_wire_object_ids(
    std::span<const PolygonObject> objects, SelectionMode mode,
    int active_object_id, const SelectionHoverState &hover)
{
  std::vector<int> object_ids;
  if (mode == SelectionMode::Object) {
    return object_ids;
  }
  auto append_unique_object_id = [&](int object_id) {
    if (object_id <= 0 ||
        std::find(object_ids.begin(), object_ids.end(), object_id) !=
            object_ids.end()) {
      return;
    }
    object_ids.push_back(object_id);
  };

  for (const PolygonObject &object : objects) {
    if (object_has_sticky_component_source_wire(object, mode)) {
      append_unique_object_id(object.id);
    }
  }
  append_unique_object_id(hover.candidate_object_id);
  if (!object_ids.empty()) {
    return object_ids;
  }

  if (active_object_id > 0) {
    for (const PolygonObject &object : objects) {
      if (object.id == active_object_id && object.selected && object.id > 0) {
        append_unique_object_id(object.id);
        return object_ids;
      }
    }
  }
  for (const PolygonObject &object : objects) {
    if (object.selected && object.id > 0) {
      append_unique_object_id(object.id);
      return object_ids;
    }
  }
  return object_ids;
}

int inferred_component_source_wire_object_id(
    std::span<const PolygonObject> objects, SelectionMode mode,
    int active_object_id, const SelectionHoverState &hover)
{
  const std::vector<int> object_ids =
      inferred_component_source_wire_object_ids(objects, mode,
                                                active_object_id, hover);
  return object_ids.empty() ? -1 : object_ids.front();
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
        const bool face_plane_hover =
            hover.face_plane_hover && hover.kind == SelectionHoverKind::Face;
	std::unordered_map<const quader_poly::Document*, SelectionOverlayDocumentLookup>
		document_lookups;
	document_lookups.reserve(objects.size());
	const auto document_lookup = [&document_lookups](
			const quader_poly::Document& document) -> const SelectionOverlayDocumentLookup& {
		const auto [lookup, inserted] = document_lookups.try_emplace(&document, document);
		(void)inserted;
		return lookup->second;
	};

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

        auto append_open_edge_overlay = [&](int object_id, const quader_poly::Document& document,
			const SelectionOverlayDocumentLookup& lookup,
			const std::vector<quader_poly::Edge>& selected_edges) {
		if (selected_edges.empty()) {
			return;
		}
                constexpr float kEdgeEpsilon = 0.00001F;
                constexpr float kOpenEdgeTickSpacing = 0.035F;
                constexpr int kOpenEdgeTickLimit = 256;
		std::unordered_set<std::uint64_t> selected_edge_keys;
		std::unordered_map<std::uint64_t, OpenEdgeOverlayRef> open_edge_refs;
		selected_edge_keys.reserve(selected_edges.size());
		open_edge_refs.reserve(selected_edges.size());
		for (const quader_poly::Edge edge : selected_edges) {
			selected_edge_keys.insert(overlay_face_edge_key(edge.a, edge.b));
		}

		for (const quader_poly::Face& face : document.faces) {
			if (face.vertices.size() < 2) {
				continue;
			}
			for (std::size_t index = 0; index < face.vertices.size(); ++index) {
				const quader_poly::ElementId a_id = face.vertices[index];
				const quader_poly::ElementId b_id = face.vertices[(index + 1U) % face.vertices.size()];
				const std::uint64_t edge_key = overlay_face_edge_key(a_id, b_id);
				if (!selected_edge_keys.contains(edge_key)) {
					continue;
				}
				OpenEdgeOverlayRef& edge_ref = open_edge_refs[edge_key];
				++edge_ref.incidence_count;
				if (edge_ref.face == nullptr) {
					edge_ref.edge = quader_poly::make_edge(a_id, b_id);
					edge_ref.face = &face;
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
			if (ref_it->second.incidence_count != 1 ||
				ref_it->second.face == nullptr) {
				continue;
			}
			const OpenEdgeOverlayRef& edge_ref = ref_it->second;
			const quader::QVec3 face_centroid =
				overlay_face_centroid(lookup, *edge_ref.face);
			const quader::QVec3 face_normal =
				quader_poly::face_normal(document, *edge_ref.face);
			const quader_poly::Vertex* a = lookup.vertex(edge_ref.edge.a);
			const quader_poly::Vertex* b = lookup.vertex(edge_ref.edge.b);
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
			quader::QVec3 tick_direction = face_centroid - midpoint;
			tick_direction = tick_direction - tangent * quader::dot(tick_direction, tangent);
                        if (quader::length_squared(tick_direction) <=
                            kEdgeEpsilon * kEdgeEpsilon) {
                          tick_direction =
                              quader::cross(face_normal, tangent);
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
									 const SelectionOverlayDocumentLookup& lookup,
									 const quader_poly::Selection& selection,
									 SelectionOverlayLayer triangle_layer,
									 SelectionOverlayLayer line_layer,
									 std::vector<SelectionOverlayTriangle>* triangles,
									 std::vector<SelectionOverlayLine>* lines,
									 const std::unordered_set<std::uint64_t>* hidden_line_edges = nullptr,
									 const std::unordered_set<quader_poly::ElementId>* hidden_triangle_faces = nullptr,
                                                                         std::optional<SelectionOverlayLayer> secondary_triangle_layer = std::nullopt) {
		QDR_PROFILE_SCOPE("qdr_editor_core.build_selection_overlay_frame.append_face_selection");
		if (triangles != nullptr) {
			std::vector<SelectionOverlayTriangle> secondary_records;
			if (secondary_triangle_layer.has_value()) {
				secondary_records.reserve(selection.faces.size() * 2U);
			}
			std::unordered_set<quader_poly::ElementId> emitted_faces;
			emitted_faces.reserve(selection.faces.size());
			for (const quader_poly::ElementId face_id : selection.faces) {
				if (!emitted_faces.insert(face_id).second ||
					(hidden_triangle_faces != nullptr &&
					 hidden_triangle_faces->contains(face_id))) {
					continue;
				}
				const quader_poly::Face *face = lookup.face(face_id);
				if (face == nullptr) {
					continue;
				}
				const std::vector<quader_poly::document_internal::Triangle>
					face_triangles =
						quader_poly::document_internal::triangulate_face_local_indices(
							document, *face);
				for (const quader_poly::document_internal::Triangle &triangle :
					 face_triangles) {
					const quader_poly::Vertex *a =
						lookup.vertex(face->vertices[triangle.a]);
					const quader_poly::Vertex *b =
						lookup.vertex(face->vertices[triangle.b]);
					const quader_poly::Vertex *c =
						lookup.vertex(face->vertices[triangle.c]);
					if (a == nullptr || b == nullptr || c == nullptr) {
						continue;
					}
					SelectionOverlayTriangle record{
						triangle_layer,
						a->position,
						b->position,
						c->position,
						triangle_layer == SelectionOverlayLayer::HoverFace
							? (face_plane_hover
								   ? style.face_plane_hover_face_color_abgr
								   : (frame.hover_target_selected
										  ? style.hover_selected_face_color_abgr
										  : style.hover_face_color_abgr))
							: style.selection_face_color_abgr,
						object_id,
						face_id,
						a->id,
						b->id,
						c->id,
					};
					triangles->push_back(record);
					if (secondary_triangle_layer.has_value()) {
						record.layer = *secondary_triangle_layer;
						record.color_abgr = style.selection_face_color_abgr;
						secondary_records.push_back(record);
					}
				}
			}
			for (SelectionOverlayTriangle &record : secondary_records) {
				triangles->push_back(std::move(record));
			}
		}

		if (lines == nullptr) {
			return;
		}
		std::unordered_set<std::uint64_t> emitted_edges;
		emitted_edges.reserve(selection.faces.size() * 4U);
		for (const quader_poly::ElementId face_id : selection.faces) {
			const quader_poly::Face* face = lookup.face(face_id);
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
				const quader_poly::Vertex* a = lookup.vertex(a_id);
				const quader_poly::Vertex* b = lookup.vertex(b_id);
				if (a == nullptr || b == nullptr) {
					continue;
				}
                                lines->push_back({
                                    line_layer,
                                    a->position,
                                    b->position,
                                    line_layer ==
                                            SelectionOverlayLayer::HoverFaceEdge
                                        ? (face_plane_hover
                                               ? style
                                                     .face_plane_hover_line_color_abgr
                                               : (frame.hover_target_selected
                                                      ? style
                                                            .hover_selected_line_color_abgr
                                                      : style
                                                            .hover_face_edge_color_abgr))
                                        : style.selected_edge_color_abgr,
                                    style.edge_line_width_pixels,
                                    object_id,
                                    face_id,
                                    quader_poly::make_edge(a_id, b_id),
                                });
                        }
		}
	};

	const std::vector<int> component_source_wire_object_id_list =
		inferred_component_source_wire_object_ids(objects, mode,
									 active_object_id, hover);
	const std::unordered_set<int> component_source_wire_object_ids(
		component_source_wire_object_id_list.begin(),
		component_source_wire_object_id_list.end());
	auto object_draws_source_wire = [&](const PolygonObject& object) {
		if (mode == SelectionMode::Object) {
			return object.selected;
		}
		return object.id > 0 &&
			   component_source_wire_object_ids.contains(object.id);
	};

	for (const PolygonObject& object : objects) {
		if (!object_draws_source_wire(object)) {
			continue;
		}
                const SelectionOverlayDocumentLookup& lookup = document_lookup(object.document);
                if (mode != SelectionMode::Object) {
                  append_hover_source_wire(frame, object, lookup, style, true);
                  continue;
                }
                for (const quader_poly::Edge edge : quader_poly::document_edges(object.document)) {
			const quader_poly::Vertex* a = lookup.vertex(edge.a);
			const quader_poly::Vertex* b = lookup.vertex(edge.b);
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
              mode == SelectionMode::Vertex && object.id > 0 &&
              object.id == hover.candidate_object_id &&
              object.selection.vertices.empty();
          const bool selected_component_object =
              object.selected &&
              (mode != SelectionMode::Vertex ||
               !object.selection.vertices.empty() ||
               object_draws_source_wire(object));
          if (selected_component_object || vertex_candidate_object) {
            if (mode == SelectionMode::Object) {
              quader_poly::Selection selection =
                  overlay_all_face_selection(object.document);
              const SelectionOverlayDocumentLookup& lookup =
                  document_lookup(object.document);
              append_face_selection(object.id, object.document, lookup, selection,
                                    SelectionOverlayLayer::SelectedXrayFace,
                                    SelectionOverlayLayer::SelectedFaceEdge,
                                    &frame.triangles, nullptr, nullptr, nullptr,
                                    SelectionOverlayLayer::SelectedFaceMask);
            } else if (mode == SelectionMode::Face) {
              const SelectionOverlayDocumentLookup& lookup =
                  document_lookup(object.document);
              std::unordered_set<std::uint64_t> hidden_hover_line_edges;
              std::unordered_set<quader_poly::ElementId>
                  hidden_hover_mask_faces;
              if (frame.hover_target_selected && object.id == hover.object_id &&
                  hover.kind == SelectionHoverKind::Face &&
                  quader_poly::selection_contains_face(object.selection,
                                                       hover.face_id)) {
                hidden_hover_mask_faces.insert(hover.face_id);
                overlay_append_face_edges_to_set(lookup, hover.face_id,
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
                  object.id, object.document, lookup, object.selection,
                  SelectionOverlayLayer::SelectedVisibleFace,
                  SelectionOverlayLayer::SelectedFaceEdge, &frame.triangles,
                  &frame.lines, hidden_line_edges, hidden_triangle_faces,
                  SelectionOverlayLayer::SelectedFaceMask);
            } else if (mode == SelectionMode::Edge) {
              const SelectionOverlayDocumentLookup& lookup =
                  document_lookup(object.document);
              append_open_edge_overlay(object.id, object.document,
                                       lookup,
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
                    lookup.vertex(edge.a);
                const quader_poly::Vertex *b =
                    lookup.vertex(edge.b);
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
              bool all_vertices_selected =
                  object.selection.vertices.size() ==
                  object.document.vertices.size();
              for (std::size_t index = 0U;
                   all_vertices_selected &&
                   index < object.document.vertices.size();
                   ++index) {
                all_vertices_selected =
                    object.selection.vertices[index] ==
                    object.document.vertices[index].id;
              }
              std::vector<std::uint8_t> selected_by_dense_vertex_id;
              std::unordered_set<quader_poly::ElementId>
                  selected_sparse_vertex_ids;
              bool dense_vertex_ids = !all_vertices_selected;
              if (!all_vertices_selected) {
                for (std::size_t index = 0U;
                     dense_vertex_ids &&
                     index < object.document.vertices.size();
                     ++index) {
                  dense_vertex_ids =
                      object.document.vertices[index].id == index + 1U;
                }
                selected_by_dense_vertex_id.assign(
                    object.document.vertices.size(), 0U);
                for (const quader_poly::ElementId id :
                     object.selection.vertices) {
                  if (id == quader_poly::kInvalidElementId ||
                      id > object.document.vertices.size() ||
                      object.document.vertices[id - 1U].id != id) {
                    dense_vertex_ids = false;
                    break;
                  }
                  selected_by_dense_vertex_id[id - 1U] = 1U;
                }
                if (!dense_vertex_ids) {
                  selected_by_dense_vertex_id.clear();
                  selected_sparse_vertex_ids.reserve(
                      object.selection.vertices.size());
                  selected_sparse_vertex_ids.insert(
                      object.selection.vertices.begin(),
                      object.selection.vertices.end());
                }
              }
              for (const quader_poly::Vertex &vertex :
                   object.document.vertices) {
                const bool vertex_selected =
                    all_vertices_selected ||
                    (dense_vertex_ids
                         ? selected_by_dense_vertex_id[vertex.id - 1U] != 0U
                         : selected_sparse_vertex_ids.contains(vertex.id));
                if (!object.selected) {
                  append_point(SelectionOverlayLayer::UnselectedVertex,
                               vertex.position, style.handle_color_abgr,
                               style.vertex_point_size_pixels, object.id,
                               vertex.id);
                } else if (vertex_selected) {
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
                  const SelectionOverlayDocumentLookup& lookup =
                      document_lookup(object.document);
                  quader_poly::Selection hover_selection;
                  hover_selection.mode = quader_poly::SelectionMode::Face;
                  hover_selection.faces.push_back(hover.face_id);
                  append_face_selection(object.id, object.document,
                                        lookup,
                                        hover_selection,
                                        SelectionOverlayLayer::HoverFace,
                                        SelectionOverlayLayer::HoverFaceEdge,
                                        &frame.triangles, &frame.lines);
                } else if (hover.kind == SelectionHoverKind::Edge) {
                  const SelectionOverlayDocumentLookup& lookup =
                      document_lookup(object.document);
                  append_hover_edge_overlay(
                      frame, object.id, object.document, lookup, hover.edge,
                      hover.face_id, frame.hover_target_selected, style);
                } else if (hover.kind == SelectionHoverKind::Vertex) {
                  const SelectionOverlayDocumentLookup& lookup =
                      document_lookup(object.document);
                  const quader_poly::Vertex *vertex =
                      lookup.vertex(hover.vertex_id);
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
	return build_selection_overlay_frame(objects, mode, -1, hover, style);
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

SelectionOverlayFrame build_object_selection_wire_overlay_frame(
		const PolygonObject& object,
		const SelectionOverlayStyle& style)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.build_object_selection_wire_overlay_frame");
	SelectionOverlayFrame frame;
	const SelectionOverlayDocumentLookup lookup(object.document, false);
	const std::vector<quader_poly::Edge> edges =
		quader_poly::document_edges(object.document);
	frame.lines.reserve(edges.size());
	for (const quader_poly::Edge edge : edges) {
		const quader_poly::Vertex* a = lookup.vertex(edge.a);
		const quader_poly::Vertex* b = lookup.vertex(edge.b);
		if (a == nullptr || b == nullptr) {
			continue;
		}
		frame.lines.push_back({
			.layer = SelectionOverlayLayer::SelectedEdge,
			.start = a->position,
			.end = b->position,
			.color_abgr = style.selected_edge_color_abgr,
			.width_pixels = style.edge_line_width_pixels,
			.object_id = object.id,
			.face_id = quader_poly::kInvalidElementId,
			.edge = edge,
		});
	}
	return frame;
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
        const bool face_plane_hover =
            hover.face_plane_hover && hover.kind == SelectionHoverKind::Face;
	std::unordered_map<const quader_poly::Document*, SelectionOverlayDocumentLookup>
		document_lookups;
	document_lookups.reserve(objects.size());
	const auto document_lookup = [&document_lookups](
			const quader_poly::Document& document) -> const SelectionOverlayDocumentLookup& {
		const auto [lookup, inserted] = document_lookups.try_emplace(&document, document);
		(void)inserted;
		return lookup->second;
	};

	for (const PolygonObject& object : objects) {
          const bool component_candidate =
              mode != SelectionMode::Object && hover.candidate_object_id > 0 &&
              object.id == hover.candidate_object_id;
          if (component_candidate &&
              !object_has_sticky_component_source_wire(object, mode)) {
            append_hover_source_wire(
                frame, object, document_lookup(object.document), style,
                component_candidate);
          }
          if (component_candidate && mode == SelectionMode::Vertex &&
              object.selection.vertices.empty()) {
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

                if (object.id != hover.object_id ||
                    hover.kind == SelectionHoverKind::None) {
                  continue;
                }

                if (hover.kind == SelectionHoverKind::Face) {
                  append_hover_face_overlay(frame, object.id, object.document,
                                            document_lookup(object.document),
                                            hover.face_id,
                                            frame.hover_target_selected,
                                            face_plane_hover, style);
                } else if (hover.kind == SelectionHoverKind::Edge) {
                  append_hover_edge_overlay(
                      frame, object.id, object.document,
                      document_lookup(object.document), hover.edge,
                      hover.face_id, frame.hover_target_selected, style);
                } else if (hover.kind == SelectionHoverKind::Vertex) {
                  append_hover_vertex_overlay(
                      frame, object.id, document_lookup(object.document),
                      hover.vertex_id,
                      frame.hover_target_selected, style);
                }
        }

	return frame;
}

SelectionOverlayFrame build_source_wire_overlay_frame(
		const PolygonObject& object,
		const SelectionOverlayStyle& style)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.build_source_wire_overlay_frame");
	SelectionOverlayFrame frame;
	const SelectionOverlayDocumentLookup lookup(object.document);
	append_hover_source_wire(frame, object, lookup, style, true);
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

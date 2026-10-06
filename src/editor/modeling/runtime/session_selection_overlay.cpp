////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/modeling/runtime/session.hpp>
#include <quader/modeling/neutral/polygon/document.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_mesh_internal.hpp>

#include <diagnostics/profile.hpp>

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader_editor_core {
namespace {

std::vector<quader_poly::document_internal::Triangle> overlay_face_triangles(
    const quader_poly::DocumentReadView &document, const quader_poly::Face &face) {
  const quader_poly::document_internal::VertexLookupIndex lookup(document);
  return quader_poly::document_internal::triangulate_face_local_indices(
      face, lookup,
      quader_poly::document_internal::face_normal_from_vertex_lookup(face, lookup));
}

quader_poly::Selection overlay_all_face_selection(const quader_poly::DocumentReadView& document)
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
	std::size_t face_index = std::numeric_limits<std::size_t>::max();
	int incidence_count = 0;
	quader::QVec3 face_centroid;
	quader::QVec3 face_normal;
};

const quader_poly::Face *open_edge_face(const quader_poly::DocumentReadView &document,
                                      const OpenEdgeOverlayRef &edge) {
  return edge.face_index < document.faces.size()
             ? &document.faces[edge.face_index] : nullptr;
}

/**
 * Indexes polygon element IDs for repeated lookups within one overlay build.
 */
class SelectionOverlayDocumentLookup {
public:
	explicit SelectionOverlayDocumentLookup(
		const quader_poly::DocumentReadView& document, bool include_faces = true)
		: document_(document)
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
			id <= document_.vertices.size()) {
			return &document_.vertices[static_cast<std::size_t>(id - 1U)];
		}
		const auto found = vertices_by_id_.find(id);
		return found == vertices_by_id_.end() ? nullptr : found->second;
	}

	[[nodiscard]] const quader_poly::Face* face(quader_poly::ElementId id) const
	{
		if (dense_faces_ && id != quader_poly::kInvalidElementId &&
			id <= document_.faces.size()) {
			return &document_.faces[static_cast<std::size_t>(id - 1U)];
		}
		const auto found = faces_by_id_.find(id);
		return found == faces_by_id_.end() ? nullptr : found->second;
	}

private:
	quader_poly::DocumentReadView document_;
	bool dense_vertices_ = false;
	bool dense_faces_ = false;
	std::unordered_map<quader_poly::ElementId, const quader_poly::Vertex*> vertices_by_id_;
	std::unordered_map<quader_poly::ElementId, const quader_poly::Face*> faces_by_id_;
};

} // namespace

/// Derived overlay data shares the immutable document's lifetime, never selection.
class SelectionOverlayGeometry final {
public:
  SelectionOverlayGeometry(const CowPolygonDocument &source,
                           SelectionOverlayGeometryWorkCounters &work);
  SelectionOverlayGeometry(const CowPolygonDocument &source,
                           const SelectionOverlayGeometry &previous,
                           SelectionOverlayGeometryWorkCounters &work);

  CowPolygonDocument document;
  std::optional<CowPolygonDocument> position_source_document;
  SelectionOverlayDocumentLookup lookup;
  std::unordered_map<quader_poly::ElementId,
                     std::vector<quader_poly::document_internal::Triangle>>
      face_triangles;
  std::vector<SelectionOverlayTriangle> source_triangles;
  std::vector<SelectionOverlayLine> source_lines;
  std::shared_ptr<const std::vector<quader_poly::Edge>> source_line_vertices;
  std::shared_ptr<const std::vector<quader_poly::Edge>> object_edges;
  std::shared_ptr<const std::unordered_map<std::uint64_t, OpenEdgeOverlayRef>>
      open_edges;
  bool has_boundary_edges = false;
  bool retained_triangulation = false;
};

namespace {

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
    const quader_poly::DocumentReadView &document,
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

bool overlay_selection_covers_all_faces(const quader_poly::DocumentReadView& document, const quader_poly::Selection& selection)
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
		bool stamp_depth = false,
		const SelectionOverlayGeometry *geometry = nullptr)
{
	if (geometry != nullptr) {
		if (stamp_depth) {
			for (SelectionOverlayTriangle triangle : geometry->source_triangles) {
				triangle.color_abgr = style.source_wire_color_abgr;
				triangle.object_id = object.id;
				frame.triangles.push_back(triangle);
			}
		}
		for (SelectionOverlayLine line : geometry->source_lines) {
			line.color_abgr = style.source_wire_color_abgr;
			line.width_pixels = style.edge_line_width_pixels;
			line.object_id = object.id;
			frame.lines.push_back(line);
		}
		return;
	}
	std::unordered_set<std::uint64_t> emitted_edges;
	emitted_edges.reserve(object.document.faces.size() * 4U);
	for (const quader_poly::Face &face : object.document.faces) {
		if (stamp_depth) {
			const std::vector<quader_poly::document_internal::Triangle> triangles =
				overlay_face_triangles(
					object.document.read_view(), face);
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
		const quader_poly::DocumentReadView& document,
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
		overlay_face_triangles(document,
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
		const quader_poly::DocumentReadView& document,
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

SelectionOverlayGeometry::SelectionOverlayGeometry(
    const CowPolygonDocument &source, SelectionOverlayGeometryWorkCounters &work)
    : document(source), lookup(document.read_view()) {
  QDR_PROFILE_SCOPE("Selection overlay extract immutable geometry");
  ++work.geometry_build_count;
  face_triangles.reserve(document.faces.size());
  auto edge_refs =
      std::make_shared<std::unordered_map<std::uint64_t, OpenEdgeOverlayRef>>();
  auto line_vertices = std::make_shared<std::vector<quader_poly::Edge>>();
  edge_refs->reserve(document.faces.size() * 4U);
  std::unordered_set<std::uint64_t> emitted_edges;
  emitted_edges.reserve(document.faces.size() * 4U);
  for (const quader_poly::Face &face : document.faces) {
    ++work.face_triangulation_count;
    auto triangles = overlay_face_triangles(document.read_view(), face);
    for (const auto &triangle : triangles) {
      const auto *a = lookup.vertex(face.vertices[triangle.a]);
      const auto *b = lookup.vertex(face.vertices[triangle.b]);
      const auto *c = lookup.vertex(face.vertices[triangle.c]);
      if (a != nullptr && b != nullptr && c != nullptr) {
        source_triangles.push_back({SelectionOverlayLayer::SourceWireDepthStamp,
                                    a->position, b->position, c->position, 0U,
                                    -1, face.id, a->id, b->id, c->id});
      }
    }
    face_triangles.try_emplace(face.id, std::move(triangles));
    for (std::size_t index = 0U; index < face.vertices.size(); ++index) {
      const auto a_id = face.vertices[index];
      const auto b_id = face.vertices[(index + 1U) % face.vertices.size()];
      const auto edge_key = overlay_face_edge_key(a_id, b_id);
      if (face.vertices.size() >= 2U) {
        auto &edge_ref = (*edge_refs)[edge_key];
        ++edge_ref.incidence_count;
        if (edge_ref.face_index == std::numeric_limits<std::size_t>::max()) {
          edge_ref.edge = quader_poly::make_edge(a_id, b_id);
          edge_ref.face_index = static_cast<std::size_t>(&face - document.faces.data());
        }
      }
      if (!emitted_edges.insert(edge_key).second) {
        continue;
      }
      const auto *a = lookup.vertex(a_id);
      const auto *b = lookup.vertex(b_id);
      if (a != nullptr && b != nullptr) {
        source_lines.push_back({SelectionOverlayLayer::SourceWire, a->position,
                                b->position, 0U, 0.0F, -1,
                                quader_poly::kInvalidElementId,
                                quader_poly::make_edge(a_id, b_id)});
        line_vertices->push_back({a_id, b_id});
      }
    }
  }
  for (auto &[edge_key, edge_ref] : *edge_refs) {
    (void)edge_key;
    const auto *face = open_edge_face(document.read_view(), edge_ref);
    if (edge_ref.incidence_count == 1 && face != nullptr) {
      has_boundary_edges = true;
      edge_ref.face_centroid = overlay_face_centroid(lookup, *face);
      edge_ref.face_normal = quader_poly::face_normal(document.read_view(), *face);
    }
  }
  open_edges = std::move(edge_refs);
  source_line_vertices = std::move(line_vertices);
  object_edges = std::make_shared<const std::vector<quader_poly::Edge>>(
      quader_poly::document_edges(document.read_view()));
}

namespace {

bool same_overlay_position(quader::QVec3 left, quader::QVec3 right) {
  return left.x == right.x && left.y == right.y && left.z == right.z;
}

bool same_overlay_topology(const quader_poly::DocumentReadView &left,
                           const quader_poly::DocumentReadView &right) {
  if (left.vertices.size() != right.vertices.size() ||
      left.faces.size() != right.faces.size()) return false;
  for (std::size_t index = 0U; index < left.vertices.size(); ++index) {
    if (left.vertices[index].id != right.vertices[index].id) return false;
  }
  for (std::size_t index = 0U; index < left.faces.size(); ++index) {
    if (left.faces[index].id != right.faces[index].id ||
        left.faces[index].vertices != right.faces[index].vertices) return false;
  }
  return true;
}

} // namespace

SelectionOverlayGeometry::SelectionOverlayGeometry(
    const CowPolygonDocument &source, const SelectionOverlayGeometry &previous,
    SelectionOverlayGeometryWorkCounters &work)
    : document(source),
      position_source_document(previous.document), lookup(document.read_view()),
      face_triangles(previous.face_triangles),
      source_lines(previous.source_lines),
      source_line_vertices(previous.source_line_vertices),
      object_edges(previous.object_edges),
      open_edges(previous.open_edges),
      has_boundary_edges(previous.has_boundary_edges), retained_triangulation(true) {
  QDR_PROFILE_SCOPE("Selection overlay refresh immutable positions");
  ++work.geometry_position_refresh_count;
  source_triangles.reserve(previous.source_triangles.size());
  for (const quader_poly::Face &face : document.faces) {
    const bool changed = std::ranges::any_of(face.vertices, [&](auto id) {
      const auto *before = previous.lookup.vertex(id);
      const auto *after = lookup.vertex(id);
      return before == nullptr || after == nullptr ||
             !same_overlay_position(before->position, after->position);
    });
    auto &triangles = face_triangles.at(face.id);
    if (changed) {
      ++work.face_triangulation_count;
      auto next_triangles = overlay_face_triangles(document.read_view(), face);
      retained_triangulation = retained_triangulation &&
          std::ranges::equal(triangles, next_triangles,
                             [](const auto &left, const auto &right) {
                               return left.a == right.a && left.b == right.b &&
                                      left.c == right.c;
                             });
      triangles = std::move(next_triangles);
    }
    for (const auto &triangle : triangles) {
      const auto *a = lookup.vertex(face.vertices[triangle.a]);
      const auto *b = lookup.vertex(face.vertices[triangle.b]);
      const auto *c = lookup.vertex(face.vertices[triangle.c]);
      if (a != nullptr && b != nullptr && c != nullptr) {
        source_triangles.push_back({SelectionOverlayLayer::SourceWireDepthStamp,
                                    a->position, b->position, c->position, 0U,
                                    -1, face.id, a->id, b->id, c->id});
      }
    }
  }
  for (std::size_t index = 0U; index < source_lines.size(); ++index) {
    // Source lines preserve first-face loop orientation rather than sorted IDs.
    auto &line = source_lines[index];
    const auto &vertices = (*source_line_vertices)[index];
    const auto *a = lookup.vertex(vertices.a);
    const auto *b = lookup.vertex(vertices.b);
    if (a == nullptr || b == nullptr) continue;
    line.start = a->position;
    line.end = b->position;
  }
  if (has_boundary_edges) {
    auto edge_refs = std::make_shared<
        std::unordered_map<std::uint64_t, OpenEdgeOverlayRef>>(*open_edges);
    for (auto &[key, edge_ref] : *edge_refs) {
      (void)key;
      const auto *face = open_edge_face(document.read_view(), edge_ref);
      if (edge_ref.incidence_count == 1 && face != nullptr) {
        edge_ref.face_centroid = overlay_face_centroid(lookup, *face);
        edge_ref.face_normal = quader_poly::face_normal(document.read_view(), *face);
      }
    }
    open_edges = std::move(edge_refs);
  }
}

std::shared_ptr<const SelectionOverlayGeometry> build_selection_overlay_geometry(
    const CowPolygonDocument &document,
    SelectionOverlayGeometryWorkCounters &work,
    const SelectionOverlayGeometry *previous) {
  if (previous != nullptr &&
      previous->face_triangles.size() == previous->document.faces.size() &&
      same_overlay_topology(previous->document.read_view(), document.read_view())) {
    return std::make_shared<const SelectionOverlayGeometry>(document, *previous,
                                                            work);
  }
  return std::make_shared<const SelectionOverlayGeometry>(document, work);
}

bool refresh_selection_overlay_frame_positions(
    SelectionOverlayFrame &frame, const SelectionOverlayGeometry &previous,
    const SelectionOverlayGeometry &current,
    SelectionOverlayGeometryWorkCounters &work) {
  if (!current.position_source_document ||
      !current.position_source_document->shares_storage_with(previous.document) ||
      !current.retained_triangulation || frame.hover_edge_dimension_context) {
    return false;
  }
  QDR_PROFILE_SCOPE("Selection overlay refresh semantic positions");
  for (auto &triangle : frame.triangles) {
    const auto *a = current.lookup.vertex(triangle.vertex_a);
    const auto *b = current.lookup.vertex(triangle.vertex_b);
    const auto *c = current.lookup.vertex(triangle.vertex_c);
    if (a == nullptr || b == nullptr || c == nullptr) return false;
    triangle.a = a->position;
    triangle.b = b->position;
    triangle.c = c->position;
  }
  for (auto &line : frame.lines) {
    // Diagnostic ticks have an edge identity but do not bind its endpoints.
    if (line.layer == SelectionOverlayLayer::OpenEdge ||
        line.layer == SelectionOverlayLayer::DiagnosticEdge) return false;
    const auto *before_a = previous.lookup.vertex(line.edge.a);
    const auto *before_b = previous.lookup.vertex(line.edge.b);
    const auto *after_a = current.lookup.vertex(line.edge.a);
    const auto *after_b = current.lookup.vertex(line.edge.b);
    if (before_a == nullptr || before_b == nullptr || after_a == nullptr ||
        after_b == nullptr) return false;
    const bool forward = same_overlay_position(line.start, before_a->position) &&
                         same_overlay_position(line.end, before_b->position);
    const bool reverse = same_overlay_position(line.start, before_b->position) &&
                         same_overlay_position(line.end, before_a->position);
    if ((!forward && !reverse) ||
        (forward && reverse &&
         !same_overlay_position(after_a->position, after_b->position))) {
      return false;
    }
    line.start = forward ? after_a->position : after_b->position;
    line.end = forward ? after_b->position : after_a->position;
  }
  for (auto &point : frame.points) {
    const auto *vertex = current.lookup.vertex(point.vertex_id);
    if (vertex == nullptr) return false;
    point.position = vertex->position;
  }
  ++work.frame_position_refresh_count;
  return true;
}

static SelectionOverlayFrame build_selection_overlay_frame_impl(
		std::span<const PolygonObject> objects,
		SelectionMode mode,
		int active_object_id,
		const SelectionHoverState& hover,
		const SelectionOverlayStyle& style,
		const SelectionOverlayGeometry *geometry)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.build_selection_overlay_frame");
	SelectionOverlayFrame frame;
	const bool selected_hover_target = selection_hover_target_matches_selection(objects, mode, hover);
	frame.hover_target_selected = hover.selected_modifier_pressed && selected_hover_target;
        const bool face_plane_hover =
            hover.face_plane_hover && hover.kind == SelectionHoverKind::Face;
	std::unordered_map<const CowPolygonDocument*, SelectionOverlayDocumentLookup>
		document_lookups;
	document_lookups.reserve(objects.size());
	const auto document_lookup = [&document_lookups, geometry](
			const CowPolygonDocument& document) -> const SelectionOverlayDocumentLookup& {
		if (geometry != nullptr) {
			return geometry->lookup;
		}
		const auto [lookup, inserted] = document_lookups.try_emplace(&document, document.read_view());
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

        auto append_open_edge_overlay = [&](int object_id, const quader_poly::DocumentReadView& document,
			const SelectionOverlayDocumentLookup& lookup,
			const std::vector<quader_poly::Edge>& selected_edges) {
		if (selected_edges.empty()) {
			return;
		}
                constexpr float kEdgeEpsilon = 0.00001F;
                constexpr float kOpenEdgeTickSpacing = 0.035F;
                constexpr int kOpenEdgeTickLimit = 256;
		std::unordered_map<std::uint64_t, OpenEdgeOverlayRef> open_edge_refs;
		if (geometry == nullptr) {
			std::unordered_set<std::uint64_t> selected_edge_keys;
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
					if (edge_ref.face_index == std::numeric_limits<std::size_t>::max()) {
						edge_ref.edge = quader_poly::make_edge(a_id, b_id);
						edge_ref.face_index = static_cast<std::size_t>(&face - document.faces.data());
					}
				}
			}
		}
		const auto &edge_refs = geometry != nullptr ? *geometry->open_edges : open_edge_refs;

		std::unordered_set<std::uint64_t> emitted_selected_open_edges;
		emitted_selected_open_edges.reserve(selected_edges.size());
		for (const quader_poly::Edge selected_edge : selected_edges) {
			const std::uint64_t edge_key = overlay_face_edge_key(selected_edge.a, selected_edge.b);
			if (!emitted_selected_open_edges.insert(edge_key).second) {
				continue;
			}
			const auto ref_it = edge_refs.find(edge_key);
			if (ref_it == edge_refs.end()) {
				continue;
			}
			if (ref_it->second.incidence_count != 1 ||
				open_edge_face(document, ref_it->second) == nullptr) {
				continue;
			}
			const OpenEdgeOverlayRef& edge_ref = ref_it->second;
			const quader::QVec3 face_centroid =
				geometry != nullptr ? edge_ref.face_centroid
                    : overlay_face_centroid(lookup, *open_edge_face(document, edge_ref));
			const quader::QVec3 face_normal =
				geometry != nullptr ? edge_ref.face_normal
                    : quader_poly::face_normal(document, *open_edge_face(document, edge_ref));
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
									 const quader_poly::DocumentReadView& document,
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
		const bool has_active_face =
			mode == SelectionMode::Face && object_id == active_object_id &&
			selection.has_active &&
			selection.active_kind == quader_poly::ElementKind::Face &&
			quader_poly::selection_contains_face(selection, selection.active_face);
		const auto selected_face_color = [&](quader_poly::ElementId face_id) {
			return has_active_face && selection.active_face == face_id
				? style.active_selection_face_color_abgr
				: style.selection_face_color_abgr;
		};
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
				std::vector<quader_poly::document_internal::Triangle> uncached_triangles;
				if (geometry == nullptr) {
					uncached_triangles =
						overlay_face_triangles(document, *face);
				}
				const auto &face_triangles = geometry != nullptr
					? geometry->face_triangles.at(face_id) : uncached_triangles;
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
							: selected_face_color(face_id),
						object_id,
						face_id,
						a->id,
						b->id,
						c->id,
					};
					triangles->push_back(record);
					if (secondary_triangle_layer.has_value()) {
						record.layer = *secondary_triangle_layer;
						record.color_abgr = selected_face_color(face_id);
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
		std::unordered_set<std::uint64_t> active_face_edges;
		if (has_active_face) {
			overlay_append_face_edges_to_set(lookup, selection.active_face,
				active_face_edges);
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
										: (active_face_edges.contains(edge_key)
											   ? style.active_selected_edge_color_abgr
											   : style.selected_edge_color_abgr),
                                    style.edge_line_width_pixels,
                                    object_id,
                                    active_face_edges.contains(edge_key)
                                        ? selection.active_face : face_id,
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
                  append_hover_source_wire(frame, object, lookup, style, true, geometry);
                  continue;
                }
                std::vector<quader_poly::Edge> uncached_edges;
                if (geometry == nullptr) {
                  uncached_edges = quader_poly::document_edges(object.document.read_view());
                }
                const auto &edges = geometry != nullptr ? *geometry->object_edges : uncached_edges;
                for (const quader_poly::Edge edge : edges) {
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
                  overlay_all_face_selection(object.document.read_view());
              const SelectionOverlayDocumentLookup& lookup =
                  document_lookup(object.document);
              append_face_selection(object.id, object.document.read_view(), lookup, selection,
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
                  object.id, object.document.read_view(), lookup, object.selection,
                  SelectionOverlayLayer::SelectedVisibleFace,
                  SelectionOverlayLayer::SelectedFaceEdge, &frame.triangles,
                  &frame.lines, hidden_line_edges, hidden_triangle_faces,
                  SelectionOverlayLayer::SelectedFaceMask);
            } else if (mode == SelectionMode::Edge) {
              const SelectionOverlayDocumentLookup& lookup =
                  document_lookup(object.document);
              append_open_edge_overlay(object.id, object.document.read_view(),
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
                  const bool active_edge =
                      object.id == active_object_id &&
                      object.selection.has_active &&
                      object.selection.active_kind == quader_poly::ElementKind::Edge &&
                      quader_poly::make_edge(edge.a, edge.b) ==
                          quader_poly::make_edge(object.selection.active_edge.a,
                                                object.selection.active_edge.b);
                  append_line(SelectionOverlayLayer::SelectedEdge, a->position,
                              b->position,
                              active_edge ? style.active_selected_edge_color_abgr
                                          : style.selected_edge_color_abgr,
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
                    const bool active_vertex =
                        object.id == active_object_id &&
                        object.selection.has_active &&
                        object.selection.active_kind == quader_poly::ElementKind::Vertex &&
                        object.selection.active_vertex == vertex.id;
                    append_point(SelectionOverlayLayer::SelectedVertex,
                                 vertex.position,
                                 active_vertex
                                     ? style.active_selected_handle_color_abgr
                                     : style.selected_handle_color_abgr,
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
                  append_face_selection(object.id, object.document.read_view(),
                                        lookup,
                                        hover_selection,
                                        SelectionOverlayLayer::HoverFace,
                                        SelectionOverlayLayer::HoverFaceEdge,
                                        &frame.triangles, &frame.lines);
                } else if (hover.kind == SelectionHoverKind::Edge) {
                  const SelectionOverlayDocumentLookup& lookup =
                      document_lookup(object.document);
                  append_hover_edge_overlay(
                      frame, object.id, object.document.read_view(), lookup, hover.edge,
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
    std::span<const PolygonObject> objects, SelectionMode mode,
    int active_object_id, const SelectionHoverState &hover,
    const SelectionOverlayStyle &style) {
  return build_selection_overlay_frame_impl(objects, mode, active_object_id,
                                            hover, style, nullptr);
}

SelectionOverlayFrame build_selection_overlay_frame(
    const PolygonObject &object, SelectionMode mode, int active_object_id,
    const SelectionHoverState &hover, const SelectionOverlayStyle &style,
    const SelectionOverlayGeometry &geometry) {
  return build_selection_overlay_frame_impl(
      std::span<const PolygonObject>(&object, 1U), mode, active_object_id, hover,
      style, &geometry);
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
		const SelectionOverlayStyle& style,
		const SelectionOverlayGeometry *geometry)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.build_object_selection_wire_overlay_frame");
	SelectionOverlayFrame frame;
	std::optional<SelectionOverlayDocumentLookup> uncached_lookup;
	std::vector<quader_poly::Edge> uncached_edges;
	if (geometry == nullptr) {
		uncached_lookup.emplace(object.document.read_view(), false);
		uncached_edges = quader_poly::document_edges(object.document.read_view());
	}
	const auto &lookup = geometry != nullptr ? geometry->lookup : *uncached_lookup;
	const auto &edges = geometry != nullptr ? *geometry->object_edges : uncached_edges;
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
	std::unordered_map<const CowPolygonDocument*, SelectionOverlayDocumentLookup>
		document_lookups;
	document_lookups.reserve(objects.size());
	const auto document_lookup = [&document_lookups](
			const CowPolygonDocument& document) -> const SelectionOverlayDocumentLookup& {
		const auto [lookup, inserted] = document_lookups.try_emplace(&document, document.read_view());
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
                  append_hover_face_overlay(frame, object.id, object.document.read_view(),
                                            document_lookup(object.document),
                                            hover.face_id,
                                            frame.hover_target_selected,
                                            face_plane_hover, style);
                } else if (hover.kind == SelectionHoverKind::Edge) {
                  append_hover_edge_overlay(
                      frame, object.id, object.document.read_view(),
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
		const SelectionOverlayStyle& style,
		const SelectionOverlayGeometry *geometry)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.build_source_wire_overlay_frame");
	SelectionOverlayFrame frame;
	std::optional<SelectionOverlayDocumentLookup> uncached_lookup;
	if (geometry == nullptr) {
		uncached_lookup.emplace(object.document.read_view());
	}
	const auto &lookup = geometry != nullptr ? geometry->lookup : *uncached_lookup;
	append_hover_source_wire(frame, object, lookup, style, true, geometry);
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
    const PolygonObject &object, const SelectionOverlayStyle &style,
    const SelectionOverlayGeometry *geometry,
    SelectionOverlayGeometryWorkCounters *work) {
  QDR_PROFILE_SCOPE("qdr_editor_core.build_diagnostic_edge_overlay_frame");
  const auto document = object.document.read_view();
  std::optional<quader_poly::document_internal::VertexLookupIndex> lookup;
  if (geometry == nullptr) {
    lookup.emplace(document);
  }
  SelectionOverlayFrame frame;
  std::unordered_set<std::uint64_t> emitted_edges;
  for (const auto &face : document.faces) {
    bool triangulation_failed = false;
    if (geometry != nullptr) {
      triangulation_failed = geometry->face_triangles.at(face.id).empty();
    } else {
      if (work != nullptr) {
        ++work->diagnostic_face_triangulation_count;
      }
      triangulation_failed =
          quader_poly::document_internal::triangulate_face_local_indices(
              face, *lookup,
              quader_poly::document_internal::face_normal_from_vertex_lookup(
                  face, *lookup)).empty();
    }
    if (!triangulation_failed) {
      continue;
    }
    for (std::size_t index = 0U; index < face.vertices.size(); ++index) {
      const auto a_id = face.vertices[index];
      const auto b_id = face.vertices[(index + 1U) % face.vertices.size()];
      if (a_id == b_id ||
          !emitted_edges.insert(overlay_face_edge_key(a_id, b_id)).second) {
        continue;
      }
      const auto *a = geometry != nullptr ? geometry->lookup.vertex(a_id)
                                          : lookup->find(a_id);
      const auto *b = geometry != nullptr ? geometry->lookup.vertex(b_id)
                                          : lookup->find(b_id);
      if (a == nullptr || b == nullptr ||
          same_overlay_position(a->position, b->position)) {
        continue;
      }
      frame.lines.push_back({SelectionOverlayLayer::DiagnosticEdge,
                             a->position, b->position,
                             style.diagnostic_edge_color_abgr,
                             style.diagnostic_edge_line_width_pixels,
                             object.id, face.id,
                             quader_poly::make_edge(a_id, b_id)});
    }
  }
  return frame;
}

SelectionOverlayFrame build_diagnostic_edge_overlay_frame(
    std::span<const PolygonObject> objects,
    const SelectionOverlayStyle &style) {
  SelectionOverlayFrame frame;
  for (const auto &object : objects) {
    auto object_frame = build_diagnostic_edge_overlay_frame(object, style);
    frame.lines.insert(frame.lines.end(), object_frame.lines.begin(),
                       object_frame.lines.end());
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

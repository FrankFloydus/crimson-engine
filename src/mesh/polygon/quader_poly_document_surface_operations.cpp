////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/document.hpp>

#include <diagnostics/profile.hpp>

#include <mesh/polygon/internal/quader_poly_document_bridge_surface_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_hull_plane_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_mesh_internal.hpp>
#include <mesh/polygon/internal/quader_poly_document_topology_helpers.hpp>

namespace quader_poly {

using namespace document_internal;

namespace {

quader::QVec3 basis_transform_point(const Transform3& transform, quader::QVec3 point)
{
    return {
        (transform.x_axis.x * point.x) + (transform.y_axis.x * point.y) + (transform.z_axis.x * point.z),
        (transform.x_axis.y * point.x) + (transform.y_axis.y * point.y) + (transform.z_axis.y * point.z),
        (transform.x_axis.z * point.x) + (transform.y_axis.z * point.y) + (transform.z_axis.z * point.z),
    };
}

quader::QVec3 transform_point_around_pivot(const Transform3& transform, quader::QVec3 pivot, quader::QVec3 point)
{
    const quader::QVec3 pivot_after_basis = basis_transform_point(transform, pivot);
    const quader::QVec3 translation = pivot + transform.origin - pivot_after_basis;
    return basis_transform_point(transform, point) + translation;
}

ElementId copied_vertex_with_offset(Document& document, std::map<ElementId, ElementId>& copied_vertices, ElementId source_id, quader::QVec3 offset)
{
    const auto existing = copied_vertices.find(source_id);
    if (existing != copied_vertices.end()) {
        return existing->second;
    }

    const Vertex* source = find_vertex(document, source_id);
    if (source == nullptr) {
      return kInvalidElementId;
    }

    const ElementId copied_id = add_vertex(document, source->position + offset);
    copied_vertices[source_id] = copied_id;
    return copied_id;
}

ElementId copied_vertex_with_transform(
    Document& document,
    std::map<ElementId, ElementId>& copied_vertices,
    ElementId source_id,
    const Transform3& transform,
    quader::QVec3 pivot)
{
    const auto existing = copied_vertices.find(source_id);
    if (existing != copied_vertices.end()) {
        return existing->second;
    }

    const Vertex* source = find_vertex(document, source_id);
    if (source == nullptr) {
      return kInvalidElementId;
    }

    const ElementId copied_id = add_vertex(document, transform_point_around_pivot(transform, pivot, source->position));
    copied_vertices[source_id] = copied_id;
    return copied_id;
}

/**
 * Represents an Edge Extrude Face Source value used by the polygon document and mesh editing core.
 */
struct EdgeExtrudeFaceSource {
  ElementId face_id = kInvalidElementId;
  std::pair<ElementId, ElementId> oriented_edge;
  quader::QVec3 source_normal;
  std::uint32_t material_slot = 0;
};

/**
 * Represents an Edge Extrude Source value used by the polygon document and mesh editing core.
 */
struct EdgeExtrudeSource {
    std::vector<EdgeExtrudeFaceSource> faces;
};

EdgeExtrudeSource edge_extrude_source_for_edge(const Document& document, Edge edge)
{
    edge = make_edge(edge.a, edge.b);
    EdgeExtrudeSource source;
    for (const Face& face : document.faces) {
        const std::optional<std::pair<ElementId, ElementId>> oriented = oriented_edge_in_face(face, edge);
        if (!oriented.has_value()) {
            continue;
        }

        source.faces.push_back({
            face.id,
            *oriented,
            face_normal(document, face),
            face.material_slot,
        });
    }
    return source;
}

quader::QVec3 copied_edge_delta(
    const Document& document,
    Edge edge,
    const std::map<ElementId, ElementId>& copied_vertices)
{
    const auto copied_a_id = copied_vertices.find(edge.a);
    const auto copied_b_id = copied_vertices.find(edge.b);
    const Vertex* source_a = find_vertex(document, edge.a);
    const Vertex* source_b = find_vertex(document, edge.b);
    const Vertex* copied_a = copied_a_id == copied_vertices.end() ? nullptr : find_vertex(document, copied_a_id->second);
    const Vertex* copied_b = copied_b_id == copied_vertices.end() ? nullptr : find_vertex(document, copied_b_id->second);
    if (source_a == nullptr || source_b == nullptr || copied_a == nullptr || copied_b == nullptr) {
        return {};
    }

    return ((copied_a->position - source_a->position) + (copied_b->position - source_b->position)) * 0.5F;
}

std::pair<const EdgeExtrudeFaceSource*, const EdgeExtrudeFaceSource*> closed_edge_source_pair_for_delta(
    const Document& document,
    const EdgeExtrudeSource& source,
    Edge edge,
    const std::map<ElementId, ElementId>& copied_vertices)
{
    if (source.faces.size() != 2U) {
        return { nullptr, nullptr };
    }

    const quader::QVec3 delta = normalize_or_zero(copied_edge_delta(document, edge, copied_vertices));
    if (length_squared(delta) <= kEpsilon) {
      return {&source.faces[0], &source.faces[1]};
    }

    const float first_score = std::abs(dot(normalize_or_zero(source.faces[0].source_normal), delta));
    const float second_score = std::abs(dot(normalize_or_zero(source.faces[1].source_normal), delta));
    if (first_score <= second_score) {
        return { &source.faces[0], &source.faces[1] };
    }
    return { &source.faces[1], &source.faces[0] };
}

bool append_renderable_face(Document& document, std::span<const ElementId> vertices, std::uint32_t material_slot)
{
    if (vertices.size() < 3) {
        return false;
    }

    Face face;
    face.vertices.assign(vertices.begin(), vertices.end());
    if (face_loop_area_score(document, face.vertices) <=
            kFaceAreaScoreEpsilon ||
        triangulate_face_local_indices(document, face).empty()) {
      return false;
    }

    [[maybe_unused]] const ElementId face_id = add_face(document, vertices, material_slot);
    return true;
}

bool append_renderable_face_oriented(Document& document, std::span<const ElementId> vertices, std::uint32_t material_slot, quader::QVec3 expected_normal)
{
    if (vertices.size() < 3) {
        return false;
    }

    Face face;
    face.vertices.assign(vertices.begin(), vertices.end());
    orient_face_toward_normal(document, face, expected_normal);
    return append_renderable_face(document, face.vertices, material_slot);
}

template <typename CopyVertex>
bool append_closed_edge_ledge_extrusion_with_copied_vertices(
    Document& document,
    Edge edge,
    const EdgeExtrudeSource& edge_source,
    std::map<ElementId, ElementId>& copied_vertices,
    CopyVertex& copy_vertex,
    float ledge_size,
    std::vector<Edge>& selected_edges)
{
    if (edge_source.faces.size() != 2U) {
        return false;
    }

    const ElementId copied_a = copy_vertex(copied_vertices, edge.a);
    const ElementId copied_b = copy_vertex(copied_vertices, edge.b);
    if (copied_a == kInvalidElementId || copied_b == kInvalidElementId ||
        copied_a == copied_b) {
      return false;
    }

    const auto [ledge_source, cap_source] = closed_edge_source_pair_for_delta(document, edge_source, edge, copied_vertices);
    if (ledge_source == nullptr || cap_source == nullptr) {
        return false;
    }

    const Face* cap_face = find_face(document, cap_source->face_id);
    if (cap_face == nullptr) {
        return false;
    }
    const Face cap_face_snapshot = *cap_face;
    const auto cap_from_entry = std::ranges::find(cap_face_snapshot.vertices, cap_source->oriented_edge.first);
    if (cap_from_entry == cap_face_snapshot.vertices.end() || cap_face_snapshot.vertices.size() < 4U) {
        return false;
    }

    const std::size_t cap_from_index = static_cast<std::size_t>(std::distance(cap_face_snapshot.vertices.begin(), cap_from_entry));
    const std::size_t cap_to_index = (cap_from_index + 1U) % cap_face_snapshot.vertices.size();
    if (cap_face_snapshot.vertices[cap_to_index] != cap_source->oriented_edge.second) {
        return false;
    }

    const ElementId cap_from = cap_source->oriented_edge.first;
    const ElementId cap_to = cap_source->oriented_edge.second;
    const std::size_t from_neighbor_index = (cap_from_index + cap_face_snapshot.vertices.size() - 1U) % cap_face_snapshot.vertices.size();
    const std::size_t to_neighbor_index = (cap_to_index + 1U) % cap_face_snapshot.vertices.size();
    const ElementId from_neighbor = cap_face_snapshot.vertices[from_neighbor_index];
    const ElementId to_neighbor = cap_face_snapshot.vertices[to_neighbor_index];
    if (from_neighbor == kInvalidElementId ||
        to_neighbor == kInvalidElementId || from_neighbor == cap_to ||
        to_neighbor == cap_from) {
      return false;
    }

    const quader::QVec3 cap_from_position = vertex_position_or_zero(document, cap_from);
    const quader::QVec3 cap_to_position = vertex_position_or_zero(document, cap_to);
    const quader::QVec3 from_neighbor_position = vertex_position_or_zero(document, from_neighbor);
    const quader::QVec3 to_neighbor_position = vertex_position_or_zero(document, to_neighbor);
    const quader::QVec3 from_strip_direction = normalize_or_zero(from_neighbor_position - cap_from_position);
    const quader::QVec3 to_strip_direction = normalize_or_zero(to_neighbor_position - cap_to_position);
    const float from_strip_length = length(from_neighbor_position - cap_from_position);
    const float to_strip_length = length(to_neighbor_position - cap_to_position);
    const float drag_length = length(copied_edge_delta(document, edge, copied_vertices));
    const float requested_depth =
        ledge_size > kEpsilon ? ledge_size : drag_length;
    const float strip_depth = std::min(requested_depth, std::min(from_strip_length, to_strip_length) * 0.5F);
    if (strip_depth <= kEpsilon ||
        length_squared(from_strip_direction) <= kEpsilon ||
        length_squared(to_strip_direction) <= kEpsilon) {
      return false;
    }

    const ElementId from_inner = add_vertex(document, cap_from_position + (from_strip_direction * strip_depth));
    const ElementId to_inner = add_vertex(document, cap_to_position + (to_strip_direction * strip_depth));
    const ElementId copied_cap_from = copy_vertex(copied_vertices, cap_from);
    const ElementId copied_cap_to = copy_vertex(copied_vertices, cap_to);
    const ElementId root_cap_from = add_vertex(document, cap_from_position);
    const ElementId root_cap_to = add_vertex(document, cap_to_position);
    if (from_inner == kInvalidElementId || to_inner == kInvalidElementId ||
        copied_cap_from == kInvalidElementId ||
        copied_cap_to == kInvalidElementId ||
        root_cap_from == kInvalidElementId ||
        root_cap_to == kInvalidElementId) {
      return false;
    }

    const std::array ledge {
        root_cap_from,
        root_cap_to,
        copied_cap_to,
        copied_cap_from,
    };
    if (!append_renderable_face_oriented(document, ledge, ledge_source->material_slot, ledge_source->source_normal)) {
        return false;
    }

    const quader::QVec3 edge_direction = normalize_or_zero(cap_to_position - cap_from_position);
    const std::array to_cap {
        root_cap_to,
        to_inner,
        copied_cap_to,
    };
    [[maybe_unused]] const bool to_cap_added = append_renderable_face_oriented(document, to_cap, cap_source->material_slot, edge_direction);

    const std::array from_cap {
        from_inner,
        root_cap_from,
        copied_cap_from,
    };
    [[maybe_unused]] const bool from_cap_added = append_renderable_face_oriented(document, from_cap, cap_source->material_slot, edge_direction * -1.0F);

    add_unique_edge(selected_edges, make_edge(copied_a, copied_b));
    return true;
}

template <typename CopyVertex>
OperationResult extrude_selected_faces_with_copied_vertices(
    Document& document,
    Selection& selection,
    CopyVertex&& copy_vertex,
    std::string_view empty_message,
    std::string_view missing_message,
    std::string_view unchanged_message)
{
  if (selection.mode != SelectionMode::Face || selection.faces.empty()) {
    return {false, std::string(empty_message)};
  }

    const ElementId active_face_id = active_face_or_invalid(selection);
    const std::vector<Face> faces = selected_face_copies(document, selection);
    if (faces.empty()) {
        return { false, std::string(missing_message) };
    }

    std::map<std::pair<ElementId, ElementId>, int> selected_edge_counts;
    for (const Face& face : faces) {
        for (const Edge& edge : face_edges(face)) {
            ++selected_edge_counts[{ edge.a, edge.b }];
        }
    }

    std::map<ElementId, ElementId> copied_vertices;
    std::vector<ElementId> selected_faces;
    selected_faces.reserve(faces.size());
    bool changed = false;

    for (const Face& original_face : faces) {
        Face* face = find_face(document, original_face.id);
        if (face == nullptr || original_face.vertices.size() < 3) {
            continue;
        }

        std::vector<ElementId> cap_vertices;
        cap_vertices.reserve(original_face.vertices.size());
        bool valid_face = true;
        for (const ElementId vertex_id : original_face.vertices) {
            const ElementId copied_id = copy_vertex(copied_vertices, vertex_id);
            if (copied_id == kInvalidElementId) {
              valid_face = false;
              break;
            }
            cap_vertices.push_back(copied_id);
        }

        if (!valid_face || cap_vertices.size() != original_face.vertices.size()) {
            continue;
        }

        face->vertices = cap_vertices;
        face->uvs.clear();
        selected_faces.push_back(original_face.id);
        changed = true;
    }

    if (!changed) {
        return { false, std::string(unchanged_message) };
    }

    for (const Face& original_face : faces) {
        if (original_face.vertices.size() < 3) {
            continue;
        }

        for (std::size_t index = 0; index < original_face.vertices.size(); ++index) {
            const ElementId from_id = original_face.vertices[index];
            const ElementId to_id = original_face.vertices[(index + 1U) % original_face.vertices.size()];
            const Edge edge = make_edge(from_id, to_id);
            const auto edge_count = selected_edge_counts.find({ edge.a, edge.b });
            if (edge_count == selected_edge_counts.end() || edge_count->second != 1) {
                continue;
            }

            const auto copied_from = copied_vertices.find(from_id);
            const auto copied_to = copied_vertices.find(to_id);
            if (copied_from == copied_vertices.end() || copied_to == copied_vertices.end()) {
                continue;
            }

            const std::array side {
                from_id,
                to_id,
                copied_to->second,
                copied_from->second,
            };
            [[maybe_unused]] const ElementId side_face_id = add_face(document, side, original_face.material_slot);
        }
    }

    prune_invalid_faces(document);
    selection.clear();
    selection.mode = SelectionMode::Face;
    selection.faces = std::move(selected_faces);
    activate_face_or_last_selection(selection, active_face_id);
    return { true, {} };
}

template <typename CopyVertex>
OperationResult extrude_selected_edges_with_copied_vertices(
    Document& document,
    Selection& selection,
    CopyVertex&& copy_vertex,
    float closed_edge_ledge_size,
    std::string_view empty_message,
    std::string_view missing_message,
    std::string_view unchanged_message)
{
  if (selection.mode != SelectionMode::Edge || selection.edges.empty()) {
    return {false, std::string(empty_message)};
  }

    const std::vector<Edge> edges = selected_valid_edges(document, selection);
    if (edges.empty()) {
        return { false, std::string(missing_message) };
    }

    std::map<ElementId, ElementId> copied_vertices;
    std::vector<Edge> selected_edges;
    selected_edges.reserve(edges.size());
    std::vector<Edge> open_edges;
    std::map<std::pair<ElementId, ElementId>, EdgeExtrudeSource> sources_by_edge;
    for (const Edge& edge : edges) {
        const EdgeExtrudeSource edge_source = edge_extrude_source_for_edge(document, edge);
        if (edge_source.faces.size() == 1U) {
            open_edges.push_back(edge);
            sources_by_edge[{ edge.a, edge.b }] = edge_source;
        } else if (edge_source.faces.size() == 2U) {
            sources_by_edge[{ edge.a, edge.b }] = edge_source;
        }
    }

    bool changed = false;
    for (const Edge& edge : edges) {
        const auto source_entry = sources_by_edge.find({ edge.a, edge.b });
        if (source_entry == sources_by_edge.end() || source_entry->second.faces.size() != 2U) {
            continue;
        }
        if (append_closed_edge_ledge_extrusion_with_copied_vertices(
            document,
            edge,
            source_entry->second,
            copied_vertices,
            copy_vertex,
            closed_edge_ledge_size,
            selected_edges)) {
            changed = true;
        }
    }

    for (const Edge& edge : open_edges) {
        const auto source_entry = sources_by_edge.find({ edge.a, edge.b });
        if (source_entry == sources_by_edge.end() || source_entry->second.faces.empty()) {
            continue;
        }

        const EdgeExtrudeFaceSource& source = source_entry->second.faces.front();

        const ElementId from_id = source.oriented_edge.first;
        const ElementId to_id = source.oriented_edge.second;
        const ElementId copied_from = copy_vertex(copied_vertices, from_id);
        const ElementId copied_to = copy_vertex(copied_vertices, to_id);
        if (copied_from == kInvalidElementId ||
            copied_to == kInvalidElementId || copied_from == copied_to) {
          continue;
        }

        std::array side {
            to_id,
            from_id,
            copied_from,
            copied_to,
        };

        [[maybe_unused]] const ElementId side_face_id = add_face(document, side, source.material_slot);
        add_unique_edge(selected_edges, make_edge(copied_from, copied_to));
        changed = true;
    }

    if (!changed || selected_edges.empty()) {
        return { false, std::string(unchanged_message) };
    }

    prune_invalid_faces(document);
    selection.clear();
    selection.mode = SelectionMode::Edge;
    selection.edges = std::move(selected_edges);
    activate_last_selection(selection);
    return { true, {} };
}

} // namespace

namespace document_internal {

OperationResult extrude_selected_elements_impl(Document& document, Selection& selection, quader::QVec3 offset, float closed_edge_ledge_size)
{
    QDR_PROFILE_SCOPE("qdr_document.extrude_selected_elements_impl");
    if (length(offset) <= kEpsilon) {
      return {false, "Extrude distance must be non-zero."};
    }

    if (selection.mode == SelectionMode::Face) {
      return extrude_selected_faces_with_copied_vertices(
          document, selection,
          [&document, offset](std::map<ElementId, ElementId> &copied_vertices,
                              ElementId source_id) {
            return copied_vertex_with_offset(document, copied_vertices,
                                             source_id, offset);
          },
          "Select one or more faces to extrude.",
          "No selected faces were found.", "No selected faces were extruded.");
    }

    if (selection.mode == SelectionMode::Edge) {
      return extrude_selected_edges_with_copied_vertices(
          document, selection,
          [&document, offset](std::map<ElementId, ElementId> &copied_vertices,
                              ElementId source_id) {
            return copied_vertex_with_offset(document, copied_vertices,
                                             source_id, offset);
          },
          closed_edge_ledge_size, "Select one or more edges to extrude.",
          "Select one or more valid edges to extrude.",
          "No selected edges were extruded.");
    }

    return { false, "Extrude needs a face or edge selection." };
}

OperationResult transform_extrude_selected_elements_impl(Document& document, Selection& selection, const Transform3& transform, quader::QVec3 pivot)
{
    QDR_PROFILE_SCOPE("qdr_document.transform_extrude_selected_elements_impl");
    if (selection.mode == SelectionMode::Face) {
      return extrude_selected_faces_with_copied_vertices(
          document, selection,
          [&document, &transform,
           pivot](std::map<ElementId, ElementId> &copied_vertices,
                  ElementId source_id) {
            return copied_vertex_with_transform(document, copied_vertices,
                                                source_id, transform, pivot);
          },
          "Select one or more faces to extrude.",
          "No selected faces were found.", "No selected faces were extruded.");
    }

    if (selection.mode == SelectionMode::Edge) {
      return extrude_selected_edges_with_copied_vertices(
          document, selection,
          [&document, &transform,
           pivot](std::map<ElementId, ElementId> &copied_vertices,
                  ElementId source_id) {
            return copied_vertex_with_transform(document, copied_vertices,
                                                source_id, transform, pivot);
          },
          0.0F, "Select one or more edges to extrude.",
          "Select one or more valid edges to extrude.",
          "No selected edges were extruded.");
    }

    return { false, "Extrude needs a face or edge selection." };
}

OperationResult inset_selected_elements_impl(Document& document, Selection& selection, const Transform3& transform, quader::QVec3 pivot)
{
    QDR_PROFILE_SCOPE("qdr_document.inset_selected_elements_impl");
    if (selection.mode == SelectionMode::Face) {
      return extrude_selected_faces_with_copied_vertices(
          document, selection,
          [&document, &transform,
           pivot](std::map<ElementId, ElementId> &copied_vertices,
                  ElementId source_id) {
            return copied_vertex_with_transform(document, copied_vertices,
                                                source_id, transform, pivot);
          },
          "Select one or more faces to inset.", "No selected faces were found.",
          "No selected faces were inset.");
    }

    if (selection.mode == SelectionMode::Edge) {
      return extrude_selected_edges_with_copied_vertices(
          document, selection,
          [&document, &transform,
           pivot](std::map<ElementId, ElementId> &copied_vertices,
                  ElementId source_id) {
            return copied_vertex_with_transform(document, copied_vertices,
                                                source_id, transform, pivot);
          },
          0.0F, "Select one or more edges to inset.",
          "Select one or more valid edges to inset.",
          "No selected edges were inset.");
    }

    return { false, "Inset needs a face or edge selection." };
}

} // namespace document_internal

} // namespace quader_poly

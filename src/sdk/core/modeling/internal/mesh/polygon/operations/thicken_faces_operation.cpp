////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/poly_operation.hpp>
#include <mesh/polygon/poly_operation_descriptor.hpp>

#include <mesh/polygon/internal/quader_poly_document_bridge_surface_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_hull_plane_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_mesh_internal.hpp>
#include <mesh/polygon/internal/quader_poly_document_topology_helpers.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <ranges>
#include <set>
#include <vector>

namespace quader_poly {

using namespace document_internal;

namespace {

/**
 * Implements the Thicken Faces Operation modeling operation for the polygon document and mesh editing core.
 */
class ThickenFacesOperation final : public PolyOperation {
public:
    ThickenFacesOperation(float thickness = 0.25F, bool from_center = false);

    [[nodiscard]] std::string_view id() const override { return poly_operation_descriptor(PolyOperationKey::ThickenSelectedFaces).id; }
    [[nodiscard]] std::string_view label() const override { return poly_operation_descriptor(PolyOperationKey::ThickenSelectedFaces).label; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;

private:
    float thickness_ = 0.25F;
    bool from_center_ = false;
};

ThickenFacesOperation::ThickenFacesOperation(float thickness, bool from_center)
    : thickness_(thickness)
    , from_center_(from_center)
{
}

OperationResult thicken_selected_faces_impl(Document& document, Selection& selection, float thickness, bool from_center)
{
    (void)from_center;
    if (selection.mode != SelectionMode::Face || selection.faces.empty()) {
      return {false, "Select one or more open faces to thicken."};
    }
    if (!std::isfinite(thickness) || std::abs(thickness) <= kEpsilon) {
      return {false, "Thicken Faces needs a non-zero finite thickness."};
    }

    const ElementId active_face_id = active_face_or_invalid(selection);
    const FacePerimeterInfo perimeter = selected_faces_perimeter_info(document, selection);
    if (perimeter.empty() || !perimeter.has_only_open_edges()) {
        return { false, "Thicken Faces needs selected faces with an open outer perimeter." };
    }

    const std::vector<Face> selected_faces = selected_face_copies(document, selection);
    if (selected_faces.empty()) {
        return { false, "No selected faces were found." };
    }

    std::set<ElementId> selected_face_ids;
    std::map<ElementId, quader::QVec3> normal_sums;
    for (const Face& face : selected_faces) {
        selected_face_ids.insert(face.id);
        const quader::QVec3 normal = face_normal(document, face);
        for (const ElementId vertex_id : face.vertices) {
            normal_sums[vertex_id] += normal;
        }
    }

    std::map<ElementId, quader::QVec3> source_positions;
    for (const auto& [vertex_id, normal_sum] : normal_sums) {
        (void)normal_sum;
        const Vertex* vertex = find_vertex(document, vertex_id);
        if (vertex == nullptr) {
            return { false, "Thicken Faces could not find selected face vertices." };
        }
        source_positions[vertex_id] = vertex->position;
    }

    Document candidate = document;
    std::map<ElementId, ElementId> duplicate_vertices;
    OperationResult result;
    auto duplicate_vertex = [&](ElementId source_vertex_id) -> ElementId {
        const auto existing = duplicate_vertices.find(source_vertex_id);
        if (existing != duplicate_vertices.end()) {
            return existing->second;
        }

        const auto source_position = source_positions.find(source_vertex_id);
        const auto normal_sum = normal_sums.find(source_vertex_id);
        if (source_position == source_positions.end() || normal_sum == normal_sums.end()) {
          return kInvalidElementId;
        }

        const quader::QVec3 normal = normalize_or_zero(normal_sum->second);
        if (length_squared(normal) <= kEpsilon) {
          return kInvalidElementId;
        }
        const quader::QVec3 offset = normal * -thickness;
        const ElementId duplicate_id = add_vertex(candidate, source_position->second + offset);
        duplicate_vertices[source_vertex_id] = duplicate_id;
        result.created.vertices.push_back(duplicate_id);
        return duplicate_id;
    };

    std::vector<ElementId> cap_face_ids;
    std::vector<ElementId> created_cap_face_ids;
    cap_face_ids.reserve(selected_faces.size() * 2U);
    created_cap_face_ids.reserve(selected_faces.size());
    ElementId active_cap_face_id = active_face_id;
    const bool flip_normals = thickness < 0.0F;
    for (const Face& source_face : selected_faces) {
        Face* original_face = find_face(candidate, source_face.id);
        if (original_face == nullptr) {
            continue;
        }

        cap_face_ids.push_back(original_face->id);

        Face cap_face;
        cap_face.id = candidate.next_face_id++;
        cap_face.material_slot = source_face.material_slot;
        cap_face.vertices.reserve(source_face.vertices.size());
        for (const ElementId vertex_id : source_face.vertices) {
            const ElementId duplicate_id = duplicate_vertex(vertex_id);
            if (duplicate_id == kInvalidElementId) {
              return {false, "Thicken Faces could not create offset vertices."};
            }
            cap_face.vertices.push_back(duplicate_id);
        }
        if (face_has_loop_uvs(source_face)) {
            cap_face.uvs = source_face.uvs;
        }
        std::ranges::reverse(cap_face.vertices);
        if (face_has_loop_uvs(cap_face)) {
            std::ranges::reverse(cap_face.uvs);
        }
        if (triangulate_face_local_indices(candidate, cap_face).empty()) {
            return { false, "Thicken Faces would create invalid cap geometry." };
        }
        cap_face_ids.push_back(cap_face.id);
        created_cap_face_ids.push_back(cap_face.id);
        candidate.faces.push_back(std::move(cap_face));
    }

    const quader::QVec3 selected_center = [&source_positions]() {
        quader::QVec3 center;
        for (const auto& [vertex_id, position] : source_positions) {
            (void)vertex_id;
            center += position;
        }
        return source_positions.empty() ? quader::QVec3 {} : center / static_cast<float>(source_positions.size());
    }();

    std::vector<ElementId> side_face_ids;
    side_face_ids.reserve(perimeter.edges.size());
    for (const Edge& edge : perimeter.edges) {
        const auto duplicate_a = duplicate_vertices.find(edge.a);
        const auto duplicate_b = duplicate_vertices.find(edge.b);
        if (duplicate_a == duplicate_vertices.end() || duplicate_b == duplicate_vertices.end()) {
            return { false, "Thicken Faces could not create side wall vertices." };
        }

        std::vector<ElementId> side_vertices = !flip_normals ?
            std::vector<ElementId> { edge.a, edge.b, duplicate_b->second, duplicate_a->second } :
            std::vector<ElementId> { edge.b, edge.a, duplicate_a->second, duplicate_b->second };
        Face normal_probe;
        normal_probe.vertices = side_vertices;
        const quader::QVec3 side_outward_normal = face_centroid(candidate, normal_probe) - selected_center;
        if (!append_bridge_face(
                candidate,
                std::move(side_vertices),
                material_slot_for_open_edge(document, edge),
                flip_normals ? side_outward_normal * -1.0F : side_outward_normal,
                side_face_ids)) {
            return { false, "Thicken Faces could not create valid side wall geometry." };
        }
    }

    prune_invalid_faces(candidate);
    prune_unused_vertices(candidate);
    if (cap_face_ids.empty() || !every_face_triangulates(candidate)) {
        return { false, "Thicken Faces would create invalid face geometry." };
    }

    result.affected.faces.assign(selected_face_ids.begin(), selected_face_ids.end());
    result.created.faces = std::move(created_cap_face_ids);
    result.created.faces.insert(
        result.created.faces.end(), side_face_ids.begin(), side_face_ids.end());
    document = std::move(candidate);
    selection.clear();
    selection.mode = SelectionMode::Face;
    selection.faces = std::move(cap_face_ids);
    selection.faces.insert(selection.faces.end(), side_face_ids.begin(), side_face_ids.end());
    if (active_cap_face_id != kInvalidElementId) {
      activate_face_selection(selection, active_cap_face_id);
    } else {
      activate_last_selection(selection);
    }
    result.changed = true;
    return result;
}

OperationResult ThickenFacesOperation::apply(Document& document, Selection& selection) const
{
    return thicken_selected_faces_impl(document, selection, thickness_, from_center_);
}

} // namespace

OperationResult thicken_selected_faces(Document& document, Selection& selection, float thickness, bool from_center)
{
    return ThickenFacesOperation(thickness, from_center).apply(document, selection);
}

} // namespace quader_poly

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/document.hpp>
#include <mesh/polygon/poly_operation_descriptor.hpp>

#include <diagnostics/profile.hpp>

#include <algorithm>
#include <set>
#include <string_view>

namespace quader_poly {
namespace {

/**
 * Represents an Edge Less value used by the polygon document and mesh editing core.
 */
struct EdgeLess {
    bool operator()(Edge left, Edge right) const
    {
        left = make_edge(left.a, left.b);
        right = make_edge(right.a, right.b);
        if (left.a != right.a) {
            return left.a < right.a;
        }
        return left.b < right.b;
    }
};

/**
 * Implements the Shade Faces Operation modeling operation for the polygon document and mesh editing core.
 */
class ShadeFacesOperation final : public PolyOperation {
public:
    explicit ShadeFacesOperation(SurfaceShading shading) : shading_(shading) {}

    [[nodiscard]] std::string_view id() const override
    {
        return metadata().id;
    }

    [[nodiscard]] std::string_view label() const override
    {
        return metadata().label;
    }

    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;

private:
    [[nodiscard]] const PolyOperationMetadata& metadata() const
    {
      return poly_operation_descriptor(
          shading_ == SurfaceShading::Smooth
              ? PolyOperationKey::ShadeSelectedFacesSmooth
              : PolyOperationKey::ShadeSelectedFacesFlat);
    }

    SurfaceShading shading_ = SurfaceShading::Flat;
};

/**
 * Implements the Edge Normal Hardness Operation modeling operation for the polygon document and mesh editing core.
 */
class EdgeNormalHardnessOperation final : public PolyOperation {
public:
    explicit EdgeNormalHardnessOperation(bool hard) : hard_(hard) {}

    [[nodiscard]] std::string_view id() const override
    {
        return metadata().id;
    }

    [[nodiscard]] std::string_view label() const override
    {
        return metadata().label;
    }

    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;

private:
    [[nodiscard]] const PolyOperationMetadata& metadata() const
    {
        return poly_operation_descriptor(
                hard_ ? PolyOperationKey::HardenSelectedEdgeNormals : PolyOperationKey::SoftenSelectedEdgeNormals);
    }

    bool hard_ = false;
};

OperationResult set_selected_face_shading(Document& document, const Selection& selection, SurfaceShading shading)
{
    QDR_PROFILE_SCOPE("qdr_document.set_selected_face_shading");
    if (selection.mode != SelectionMode::Face || selection.faces.empty()) {
      return {false, "Select one or more faces before changing shade mode."};
    }

    bool changed = false;
    for (ElementId face_id : selection.faces) {
        Face* face = find_face(document, face_id);
        if (face == nullptr) {
            continue;
        }
        if (face->normal_shading == shading) {
            continue;
        }
        face->normal_shading = shading;
        changed = true;
    }

    if (!changed) {
      return {
          false,
          shading == SurfaceShading::Smooth
              ? "Selected faces are already shade smooth."
              : "Selected faces are already shade flat.",
      };
    }
    return { true, {} };
}

std::set<Edge, EdgeLess> document_edge_set(const Document& document)
{
    std::set<Edge, EdgeLess> edges;
    for (Edge edge : document_edges(document)) {
        edges.insert(make_edge(edge.a, edge.b));
    }
    return edges;
}

bool face_contains_edge(const Face& face, Edge edge)
{
    if (face.vertices.size() < 2U) {
        return false;
    }

    edge = make_edge(edge.a, edge.b);
    for (std::size_t index = 0; index < face.vertices.size(); ++index) {
        const Edge face_edge = make_edge(face.vertices[index], face.vertices[(index + 1U) % face.vertices.size()]);
        if (face_edge == edge) {
            return true;
        }
    }
    return false;
}

bool smooth_faces_touching_edge(Document& document, Edge edge)
{
    bool changed = false;
    for (Face& face : document.faces) {
      if (!face_contains_edge(face, edge) ||
          face.normal_shading == SurfaceShading::Smooth) {
        continue;
      }
      face.normal_shading = SurfaceShading::Smooth;
      changed = true;
    }
    return changed;
}

OperationResult set_selected_edge_normal_hardness(Document& document, const Selection& selection, bool hard)
{
    QDR_PROFILE_SCOPE("qdr_document.set_selected_edge_normal_hardness");
    if (selection.mode != SelectionMode::Edge || selection.edges.empty()) {
      return {false,
              "Select one or more edges before changing normal hardness."};
    }

    const std::set<Edge, EdgeLess> valid_edges = document_edge_set(document);
    std::set<Edge, EdgeLess> hard_edges;
    for (Edge edge : document.hard_normal_edges) {
        edge = make_edge(edge.a, edge.b);
        if (valid_edges.find(edge) != valid_edges.end()) {
            hard_edges.insert(edge);
        }
    }

    bool changed = false;
    for (Edge edge : selection.edges) {
        edge = make_edge(edge.a, edge.b);
        if (valid_edges.find(edge) == valid_edges.end()) {
            continue;
        }
        if (hard) {
            changed = hard_edges.insert(edge).second || changed;
        } else {
            changed = hard_edges.erase(edge) > 0U || changed;
            changed = smooth_faces_touching_edge(document, edge) || changed;
        }
    }

    if (!changed) {
        return {
            false,
            hard ?
                "Selected edges already have hard normals." :
                "Selected edges already have soft normals.",
        };
    }

    document.hard_normal_edges.assign(hard_edges.begin(), hard_edges.end());
    return { true, {} };
}

OperationResult ShadeFacesOperation::apply(Document& document, Selection& selection) const
{
    return set_selected_face_shading(document, selection, shading_);
}

OperationResult EdgeNormalHardnessOperation::apply(Document& document, Selection& selection) const
{
    return set_selected_edge_normal_hardness(document, selection, hard_);
}

} // namespace

OperationResult shade_selected_faces_smooth(Document& document, const Selection& selection)
{
    Selection selection_copy = selection;
    return ShadeFacesOperation{SurfaceShading::Smooth}.apply(document,
                                                             selection_copy);
}

OperationResult shade_selected_faces_flat(Document& document, const Selection& selection)
{
    Selection selection_copy = selection;
    return ShadeFacesOperation{SurfaceShading::Flat}.apply(document,
                                                           selection_copy);
}

OperationResult harden_selected_edge_normals(Document& document, const Selection& selection)
{
    Selection selection_copy = selection;
    return EdgeNormalHardnessOperation { true }.apply(document, selection_copy);
}

OperationResult soften_selected_edge_normals(Document& document, const Selection& selection)
{
    Selection selection_copy = selection;
    return EdgeNormalHardnessOperation { false }.apply(document, selection_copy);
}

} // namespace quader_poly

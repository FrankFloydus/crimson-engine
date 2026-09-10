////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/neutral/polygon/document.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_topology_helpers.hpp>
#include <quader/modeling/neutral/polygon/poly_operation_descriptor.hpp>

#include <diagnostics/profile.hpp>

#include <string_view>

namespace quader_poly {
namespace {

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
    explicit EdgeNormalHardnessOperation(EdgeNormalMode mode) : mode_(mode) {}

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
        switch (mode_) {
        case EdgeNormalMode::Default:
          return poly_operation_descriptor(
              PolyOperationKey::ResetSelectedEdgeNormalsToDefault);
        case EdgeNormalMode::Hard:
          return poly_operation_descriptor(
              PolyOperationKey::HardenSelectedEdgeNormals);
        case EdgeNormalMode::Soft:
          return poly_operation_descriptor(
              PolyOperationKey::SoftenSelectedEdgeNormals);
        }
        return poly_operation_descriptor(
            PolyOperationKey::ResetSelectedEdgeNormalsToDefault);
    }

    EdgeNormalMode mode_ = EdgeNormalMode::Default;
};

OperationResult set_selected_face_shading(Document& document, const Selection& selection, SurfaceShading shading)
{
    QDR_PROFILE_SCOPE("qdr_document.set_selected_face_shading");
    if (selection.mode != SelectionMode::Face || selection.faces.empty()) {
      return OperationResult::failure(
          "Select one or more faces before changing shade mode.");
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

OperationResult set_selected_edge_normal_mode(
    Document& document, const Selection& selection, EdgeNormalMode mode)
{
    QDR_PROFILE_SCOPE("qdr_document.set_selected_edge_normal_mode");
    if (selection.mode != SelectionMode::Edge || selection.edges.empty()) {
      return OperationResult::failure(
          "Select one or more edges before changing normal mode.");
    }

    std::vector<document_internal::EdgeNormalModeAssignment> assignments;
    assignments.reserve(selection.edges.size());
    for (Edge edge : selection.edges) {
        edge = make_edge(edge.a, edge.b);
        assignments.push_back({edge, mode});
    }
    const bool changed = document_internal::set_edge_normal_modes(
        document, assignments);

    if (!changed) {
        const char* message = "Selected edges already use default normals.";
        switch (mode) {
        case EdgeNormalMode::Default:
          message = "Selected edges already use default normals.";
          break;
        case EdgeNormalMode::Hard:
          message = "Selected edges already have hard normals.";
          break;
        case EdgeNormalMode::Soft:
          message = "Selected edges already have soft normals.";
          break;
        }
        return {
            false,
            message,
        };
    }
    return { true, {} };
}

OperationResult ShadeFacesOperation::apply(Document& document, Selection& selection) const
{
    return set_selected_face_shading(document, selection, shading_);
}

OperationResult EdgeNormalHardnessOperation::apply(Document& document, Selection& selection) const
{
    return set_selected_edge_normal_mode(document, selection, mode_);
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
    return EdgeNormalHardnessOperation { EdgeNormalMode::Hard }.apply(
        document, selection_copy);
}

OperationResult soften_selected_edge_normals(Document& document, const Selection& selection)
{
    Selection selection_copy = selection;
    return EdgeNormalHardnessOperation { EdgeNormalMode::Soft }.apply(
        document, selection_copy);
}

OperationResult reset_selected_edge_normals_to_default(
    Document& document, const Selection& selection)
{
    Selection selection_copy = selection;
    return EdgeNormalHardnessOperation { EdgeNormalMode::Default }.apply(
        document, selection_copy);
}

} // namespace quader_poly

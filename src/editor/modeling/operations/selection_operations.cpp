////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/runtime/session_internal.hpp"

#include <editor/modeling/operations/operation_registry.hpp>

namespace quader_editor_core {
namespace {

using SelectionExecuteFn = CommandResult (EditorSession::*)();

/**
 * Implements the Selection Operation modeling operation for the modeling
 * operation adapter layer.
 */
class SelectionOperation final : public EditorOperation {
public:
  constexpr SelectionOperation(EditorOperationId operation_id,
                               std::string_view id, std::string_view label,
                               SelectionExecuteFn execute)
      : operation_id_(operation_id), id_(id), label_(label), execute_(execute) {
  }

  [[nodiscard]] EditorOperationId operation_id() const override {
    return operation_id_;
  }

  [[nodiscard]] std::string_view id() const override { return id_; }

  [[nodiscard]] std::string_view label() const override { return label_; }

  [[nodiscard]] bool supports_preview() const override { return false; }

  [[nodiscard]] CommandResult
  execute(EditorSession &session,
          const EditorOperationSettings &settings) const override {
    if (!std::holds_alternative<SelectionOperationSettings>(settings)) {
      return {false, false,
              "Operation command settings do not match the operation id."};
    }
    return (session.*execute_)();
  }

private:
  EditorOperationId operation_id_;
  std::string_view id_;
  std::string_view label_;
  SelectionExecuteFn execute_;
};

/**
 * Implements the Recalculate Face Normals Operation modeling operation for the
 * modeling operation adapter layer.
 */
class RecalculateFaceNormalsOperation final : public EditorOperation {
public:
  [[nodiscard]] EditorOperationId operation_id() const override {
    return EditorOperationId::RecalculateFaceNormals;
  }

  [[nodiscard]] std::string_view id() const override {
    return "recalculate_face_normals";
  }

  [[nodiscard]] std::string_view label() const override {
    return "Recalculate Face Normals";
  }

  [[nodiscard]] bool supports_preview() const override { return false; }

  [[nodiscard]] CommandResult
  execute(EditorSession &session,
          const EditorOperationSettings &settings) const override {
    const auto *recalculate_settings =
        std::get_if<RecalculateFaceNormalsOperationSettings>(&settings);
    if (recalculate_settings == nullptr) {
      return {false, false,
              "Operation command settings do not match the operation id."};
    }
    return session.recalculate_selected_face_normals(
        recalculate_settings->outside);
  }
};

/**
 * Implements the Delete Selection Operation modeling operation for the modeling
 * operation adapter layer.
 */
class DeleteSelectionOperation final : public EditorOperation {
public:
  [[nodiscard]] EditorOperationId operation_id() const override {
    return EditorOperationId::DeleteSelection;
  }

  [[nodiscard]] std::string_view id() const override {
    return "delete_selection";
  }

  [[nodiscard]] std::string_view label() const override {
    return "Delete Selection";
  }

  [[nodiscard]] bool supports_preview() const override { return false; }

  [[nodiscard]] CommandResult
  execute(EditorSession &session,
          const EditorOperationSettings &settings) const override {
    if (!std::holds_alternative<SelectionOperationSettings>(settings)) {
      return {false, false,
              "Operation command settings do not match the operation id."};
    }
    if (!session.has_selection()) {
      return {true, false, ""};
    }
    return session.delete_selected();
  }
};

} // namespace

const EditorOperation &connect_vertices_operation() {
  static const SelectionOperation kOperation(
      EditorOperationId::ConnectVertices, "connect_vertices",
      "Connect Vertices", &EditorSession::connect_selected_vertices);
  return kOperation;
}

const EditorOperation &dissolve_vertices_operation() {
  static const SelectionOperation kOperation(
      EditorOperationId::DissolveVertices, "dissolve_vertices",
      "Dissolve Vertices", &EditorSession::dissolve_selected_vertices);
  return kOperation;
}

const EditorOperation &connect_edges_operation() {
  static const SelectionOperation kOperation(
      EditorOperationId::ConnectEdges, "connect_edges", "Connect Edges",
      &EditorSession::connect_selected_edges);
  return kOperation;
}

const EditorOperation &split_edges_operation() {
  static const SelectionOperation kOperation(
      EditorOperationId::SplitEdges, "split_edges", "Split Edges",
      &EditorSession::split_selected_edges);
  return kOperation;
}

const EditorOperation &dissolve_edges_operation() {
  static const SelectionOperation kOperation(
      EditorOperationId::DissolveEdges, "dissolve_edges", "Dissolve Edges",
      &EditorSession::dissolve_selected_edges);
  return kOperation;
}

const EditorOperation &merge_edges_operation() {
  static const SelectionOperation kOperation(
      EditorOperationId::MergeEdges, "merge_edges", "Merge Edges",
      &EditorSession::merge_selected_edges);
  return kOperation;
}

const EditorOperation &collapse_edges_operation() {
  static const SelectionOperation kOperation(
      EditorOperationId::CollapseEdges, "collapse_edges", "Collapse Edges",
      &EditorSession::collapse_selected_edges);
  return kOperation;
}

const EditorOperation &fill_hole_operation() {
  static const SelectionOperation kOperation(
      EditorOperationId::FillHole, "fill_hole", "Fill Hole",
      &EditorSession::fill_hole_from_selected_edges);
  return kOperation;
}

const EditorOperation &remove_doubles_operation() {
  static const SelectionOperation kOperation(EditorOperationId::RemoveDoubles,
                                             "remove_doubles", "Remove Doubles",
                                             &EditorSession::remove_doubles);
  return kOperation;
}

const EditorOperation &combine_faces_operation() {
  static const SelectionOperation kOperation(
      EditorOperationId::CombineFaces, "combine_faces", "Combine Faces",
      &EditorSession::combine_selected_faces);
  return kOperation;
}

const EditorOperation &collapse_faces_operation() {
  static const SelectionOperation kOperation(
      EditorOperationId::CollapseFaces, "collapse_faces", "Collapse Faces",
      &EditorSession::collapse_selected_faces);
  return kOperation;
}

const EditorOperation &radial_align_operation() {
  static const SelectionOperation kOperation(
      EditorOperationId::RadialAlign, "radial_align", "Radial Align",
      &EditorSession::radial_align_selected);
  return kOperation;
}

const EditorOperation &flip_face_normals_operation() {
  static const SelectionOperation kOperation(
      EditorOperationId::FlipFaceNormals, "flip_face_normals",
      "Flip Face Normals", &EditorSession::flip_selected_face_normals);
  return kOperation;
}

const EditorOperation &shade_faces_smooth_operation() {
  static const SelectionOperation kOperation(
      EditorOperationId::ShadeFacesSmooth, "shade_faces_smooth",
      "Shade Faces Smooth", &EditorSession::shade_selected_faces_smooth);
  return kOperation;
}

const EditorOperation &shade_faces_flat_operation() {
  static const SelectionOperation kOperation(
      EditorOperationId::ShadeFacesFlat, "shade_faces_flat", "Shade Faces Flat",
      &EditorSession::shade_selected_faces_flat);
  return kOperation;
}

const EditorOperation &harden_edge_normals_operation() {
  static const SelectionOperation kOperation(
      EditorOperationId::HardenEdgeNormals, "harden_edge_normals",
      "Harden Edge Normals", &EditorSession::harden_selected_edge_normals);
  return kOperation;
}

const EditorOperation &soften_edge_normals_operation() {
  static const SelectionOperation kOperation(
      EditorOperationId::SoftenEdgeNormals, "soften_edge_normals",
      "Soften Edge Normals", &EditorSession::soften_selected_edge_normals);
  return kOperation;
}

const EditorOperation &combine_meshes_operation() {
  static const SelectionOperation kOperation(
      EditorOperationId::CombineMeshes, "combine_meshes", "Combine Meshes",
      &EditorSession::combine_selected_meshes);
  return kOperation;
}

const EditorOperation &extract_faces_operation() {
  static const SelectionOperation kOperation(
      EditorOperationId::ExtractFaces, "extract_faces", "Extract Faces",
      &EditorSession::extract_selected_faces);
  return kOperation;
}

const EditorOperation &detach_faces_operation() {
  static const SelectionOperation kOperation(
      EditorOperationId::DetachFaces, "detach_faces", "Detach Faces",
      &EditorSession::detach_selected_faces);
  return kOperation;
}

const EditorOperation &snap_vertices_to_active_operation() {
  static const SelectionOperation kOperation(
      EditorOperationId::SnapVerticesToActive, "snap_vertices_to_active",
      "Snap Vertices To Active",
      &EditorSession::snap_selected_vertices_to_active);
  return kOperation;
}

const EditorOperation &snap_edges_to_active_operation() {
  static const SelectionOperation kOperation(
      EditorOperationId::SnapEdgesToActive, "snap_edges_to_active",
      "Snap Edges To Active", &EditorSession::snap_selected_edges_to_active);
  return kOperation;
}

const EditorOperation &merge_vertices_to_active_operation() {
  static const SelectionOperation kOperation(
      EditorOperationId::MergeVerticesToActive, "merge_vertices_to_active",
      "Merge Vertices To Active",
      &EditorSession::merge_selected_vertices_to_active);
  return kOperation;
}

const EditorOperation &merge_vertices_to_center_operation() {
  static const SelectionOperation kOperation(
      EditorOperationId::MergeVerticesToCenter, "merge_vertices_to_center",
      "Merge Vertices To Center",
      &EditorSession::merge_selected_vertices_to_center);
  return kOperation;
}

const EditorOperation &recalculate_face_normals_operation() {
  static const RecalculateFaceNormalsOperation kOperation;
  return kOperation;
}

const EditorOperation &delete_selection_operation() {
  static const DeleteSelectionOperation kOperation;
  return kOperation;
}

} // namespace quader_editor_core

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_modeling_operation_catalog.hpp"

#include "core/localization_service.hpp"

#include <string>

namespace quader {
namespace {

using Mode = QuaderSelectionMode;

constexpr QtModelingOperationSpec kOperationSpecs[]{
    {Mode::Vertex, quader_command::kModelingConnectVertices, "Connect Vertices", 6},
    {Mode::Vertex, quader_command::kModelingBevelVertices, "Bevel Vertices", 0},
    {Mode::Vertex, quader_command::kModelingMergeVerticesToActive, "Merge to Active", 4},
    {Mode::Edge, quader_command::kToolsActivateInsertEdgeLoop, "Insert Edge Loop", 1},
    {Mode::Edge, quader_command::kModelingConnectEdges, "Connect Edges", 6},
    {Mode::Edge, quader_command::kModelingSplitEdges, "Split Edges", 5},
    {Mode::Edge, quader_command::kModelingBevelEdges, "Bevel Edges", 0},
    {Mode::Edge, quader_command::kModelingPreviewBridgeEdges, "Bridge Edges", 2},
    {Mode::Edge, quader_command::kModelingFillHole, "Fill Hole", 3},
    {Mode::Edge, quader_command::kModelingMergeEdges, "Merge Edges", 4},
    {Mode::Face, quader_command::kModelingPreviewBridgeFaces, "Bridge Faces", 2},
    {Mode::Face, quader_command::kModelingThickenFaces, "Thicken Faces", 0},
    {Mode::Face, quader_command::kModelingSliceQuad, "Slice Quads", 1},
    {Mode::Face, quader_command::kModelingCombineFaces, "Combine Faces", 4},
    {Mode::Face, quader_command::kModelingExtractFaces, "Extract Faces", 3},
    {Mode::Face, quader_command::kModelingDetachFaces, "Detach Faces", 5},
    {Mode::Face, quader_command::kModelingCollapseFaces, "Collapse Faces", 6},
    {Mode::Object, quader_command::kEditCreateInstance, "Create Instance", 0},
    {Mode::Object, quader_command::kModelingInvertMesh, "Flip Normals", 2},
    {Mode::Object, quader_command::kModelingSnapMeshesToFloor, "Snap to Floor", 4},
    {Mode::Group, quader_command::kDuplicateSelection, "Duplicate", 0},
    {Mode::Group, quader_command::kGroupSelection, "Group", 2},
    {Mode::Group, quader_command::kUngroupSelection, "Ungroup", 6},
    {Mode::Group, quader_command::kParentToActive, "Parent to Active", 3},
    {Mode::Group, quader_command::kUnparentSelection, "Unparent", 5},
};

} // namespace

std::span<const QtModelingOperationSpec> qt_modeling_operation_specs() {
  return kOperationSpecs;
}

QString qt_modeling_operation_label(const LocalizationService &localization,
                                    QuaderCommandId command_id,
                                    std::string_view fallback) {
  const std::string id(command_id.string());
  std::string label = localization.text(
      "keybindings.target." + id + ".label", fallback);
  if (id.starts_with("modeling.")) {
    const std::string action_id = id.substr(std::string_view("modeling.").size());
    label = localization.text("modeling.action." + action_id + ".label", label);
    label = localization.text(
        "ui.viewport.context_menu.action." + action_id + ".label", label);
  } else if (id.starts_with("edit.")) {
    label = localization.text("ui.menu." + id, label);
  }
  return QString::fromStdString(label);
}

} // namespace quader

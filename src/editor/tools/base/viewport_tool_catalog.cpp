////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/tools/viewport_tool_catalog.hpp"

#include "viewport/host_viewport_contract_adapter.hpp"
#include "editor/tools/interactive/create_primitive_tool.hpp"
#include "editor/tools/interactive/cut_tool.hpp"
#include "editor/tools/interactive/extrude_tool.hpp"
#include "editor/tools/interactive/insert_edge_loop_tool.hpp"
#include "editor/tools/interactive/knife_tool.hpp"
#include "editor/tools/interactive/mirror_tool.hpp"
#include "editor/tools/interactive/move_tool.hpp"
#include "editor/tools/interactive/poly_tool.hpp"
#include "editor/tools/interactive/rotate_tool.hpp"
#include "editor/tools/interactive/scale_tool.hpp"
#include "editor/tools/interactive/select_tool.hpp"

#include <algorithm>
#include <array>

namespace quader {

std::span<const ViewportTool *const> viewport_tool_catalog() {
  static const SelectTool kSelectTool;
  static const MoveTool kMoveTool;
  static const ExtrudeTool kExtrudeTool;
  static const RotateTool kRotateTool;
  static const ScaleTool kScaleTool;
  static const CreatePrimitiveTool kCreatePrimitiveTool;
  static const InsertEdgeLoopTool kInsertEdgeLoopTool;
  static const KnifeTool kNifeTool;
  static const CutTool kCutTool;
  static const PolyTool kPolyTool;
  static const MirrorTool kMirrorTool;
  static const ViewportTool kPivotTool(ViewportToolId::Pivot);
  static const ViewportTool kLinearArrayTool(ViewportToolId::LinearArray);
  static const ViewportTool kRadialArrayTool(ViewportToolId::RadialArray);
  static const std::array<const ViewportTool *, 14> kTools{{
      &kSelectTool,
      &kMoveTool,
      &kExtrudeTool,
      &kRotateTool,
      &kScaleTool,
      &kCreatePrimitiveTool,
      &kInsertEdgeLoopTool,
      &kNifeTool,
      &kCutTool,
      &kPolyTool,
      &kMirrorTool,
      &kPivotTool,
      &kLinearArrayTool,
      &kRadialArrayTool,
  }};
  return {kTools.data(), kTools.size()};
}

const ViewportTool *find_viewport_tool(ViewportToolId tool) {
  const std::span<const ViewportTool *const> tools = viewport_tool_catalog();
  const auto found =
      std::find_if(tools.begin(), tools.end(), [&](const Tool *item) {
        return item != nullptr && item->viewport_tool() == tool;
      });
  return found == tools.end() ? nullptr : *found;
}

const ViewportTool *find_viewport_tool(std::string_view id) {
  const std::span<const ViewportTool *const> tools = viewport_tool_catalog();
  const auto found =
      std::find_if(tools.begin(), tools.end(), [&](const Tool *item) {
        return item != nullptr && item->id() == id;
      });
  return found == tools.end() ? nullptr : *found;
}

bool viewport_tool_available(ViewportToolId tool) {
  const ViewportTool *item = find_viewport_tool(tool);
  return item != nullptr &&
         viewport_tool_has_native_controller(item->viewport_tool());
}

std::string_view viewport_tool_catalog_id(ViewportToolId tool) {
  const ViewportTool *item = find_viewport_tool(tool);
  return item != nullptr ? item->id() : std::string_view{"unknown"};
}

std::string_view viewport_tool_label(ViewportToolId tool) {
  const ViewportTool *item = find_viewport_tool(tool);
  return item != nullptr ? item->label() : std::string_view{"Unknown"};
}

bool viewport_tool_owns_viewport(ViewportToolId tool) {
  const ViewportTool *item = find_viewport_tool(tool);
  return item != nullptr && item->owns_viewport();
}

bool viewport_tool_blocks_focus_shortcuts(ViewportToolId tool) {
  const ViewportTool *item = find_viewport_tool(tool);
  return item != nullptr && item->blocks_viewport_focus_shortcuts();
}

bool viewport_tool_supports_transform_gizmo(ViewportToolId tool) {
  const ViewportTool *item = find_viewport_tool(tool);
  return item != nullptr && item->supports_transform_gizmo();
}

ModelingTransformTool viewport_tool_transform_tool(ViewportToolId tool) {
  const ViewportTool *item = find_viewport_tool(tool);
  return item != nullptr ? item->transform_tool() : ModelingTransformTool::Move;
}

quader_editor_core::ViewportInteractionTool
viewport_tool_interaction_tool(ViewportToolId tool) {
  const ViewportTool *item = find_viewport_tool(tool);
  if (item == nullptr) {
    return quader_editor_core::ViewportInteractionTool::Select;
  }
  const quader_editor_core::ViewportInteractionTool runtime_tool =
      quader_editor_core::viewport_interaction_tool_from_id(item->id());
  return runtime_tool == quader_editor_core::ViewportInteractionTool::Unknown
             ? quader_editor_core::ViewportInteractionTool::Select
             : runtime_tool;
}

ModelingViewportTool modeling_viewport_tool_for(ViewportToolId tool) {
  const ViewportTool *item = find_viewport_tool(tool);
  return item != nullptr ? item->modeling_tool() : ModelingViewportTool::Select;
}

ViewportToolId viewport_tool_for_modeling(ModelingViewportTool tool) {
  if (tool == ModelingViewportTool::Cylinder ||
      tool == ModelingViewportTool::Sphere) {
    tool = ModelingViewportTool::Box;
  }
  const std::span<const ViewportTool *const> tools = viewport_tool_catalog();
  const auto found =
      std::find_if(tools.begin(), tools.end(), [&](const Tool *item) {
        return item != nullptr && item->modeling_tool() == tool;
      });
  return found == tools.end() ? ViewportToolId::Select
                              : (*found)->viewport_tool();
}

} // namespace quader

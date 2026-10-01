////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "viewport/host_viewport_contract_adapter.hpp"

#include "api/quader_command_ids.hpp"
#include "core/public_command_targets.hpp"
#include "editor/commands/modeling_command_tool_catalog.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "editor/tools/viewport_tool_descriptor_catalog.hpp"

#include <editor/metadata/host_contract.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace quader {
namespace {

std::string viewport_shortcut_label(bool primary, bool shift, bool alt,
                                    std::string_view key) {
  std::string shortcut;
  if (primary) {
    shortcut += "Ctrl+";
  }
  if (shift) {
    shortcut += "Shift+";
  }
  if (alt) {
    shortcut += "Alt+";
  }
  shortcut += key;
  return shortcut;
}

ViewportShortcut viewport_target_shortcut(
    ViewportShortcutKind kind,
    ViewportShortcutContext context = ViewportShortcutContext::Viewport) {
  return {
      .kind = kind,
      .context = context,
  };
}

ViewportShortcut viewport_mirror_target_shortcut(ViewportMirrorAxis axis) {
  return {
      .kind = ViewportShortcutKind::MirrorAxis,
      .context = ViewportShortcutContext::Viewport,
      .mirror_axis = axis,
  };
}

ViewportShortcut viewport_view_target_shortcut(ViewportView view) {
  return {
      .kind = ViewportShortcutKind::SetView,
      .context = ViewportShortcutContext::Viewport,
      .view = view,
  };
}

std::optional<std::string> viewport_modeling_command_target_id_for_host_target(
    std::string_view target_id) {
  for (const CommandTool *tool : modeling_command_tools()) {
    if (tool == nullptr) {
      continue;
    }
    if (tool->id() == target_id ||
        tool->host_shortcut_target_id() == target_id) {
      return tool->public_command_target_id();
    }
  }
  return std::nullopt;
}

std::optional<std::string> viewport_command_target_id_for_shortcut_descriptor(
    const ViewportShortcutTargetDescriptor &descriptor) {
  if (!descriptor.contract_target_id.empty()) {
    if (std::optional<std::string> modeling_target =
            viewport_modeling_command_target_id_for_host_target(
                descriptor.contract_target_id)) {
      return modeling_target;
    }
  }
  if (!descriptor.command_target_id.empty()) {
    return std::string(descriptor.command_target_id);
  }
  return std::nullopt;
}

constexpr std::uint32_t
viewport_selection_mode_mask(ViewportSelectionMode mode) {
  switch (mode) {
  case ViewportSelectionMode::Vertex:
    return 1u << 0u;
  case ViewportSelectionMode::Edge:
    return 1u << 1u;
  case ViewportSelectionMode::Face:
    return 1u << 2u;
  case ViewportSelectionMode::Object:
    return 1u << 3u;
  case ViewportSelectionMode::Group:
    return 1u << 4u;
  }
  return 0u;
}

constexpr std::uint32_t kMaterialSourceSelectionModes =
    viewport_selection_mode_mask(ViewportSelectionMode::Vertex) |
    viewport_selection_mode_mask(ViewportSelectionMode::Edge) |
    viewport_selection_mode_mask(ViewportSelectionMode::Face) |
    viewport_selection_mode_mask(ViewportSelectionMode::Object) |
    viewport_selection_mode_mask(ViewportSelectionMode::Group);

} // namespace

bool viewport_host_contract_available() { return true; }

std::string viewport_input_action_target_id(std::string_view suffix) {
  std::string target_id("viewport.");
  target_id.append(suffix);
  return target_id;
}

std::string viewport_selection_mode_target_id(std::string_view mode) {
  return quader_viewport_selection_mode_target_id(mode);
}

std::string viewport_grid_preset_target_id(int preset) {
  return quader_viewport_grid_preset_target_id(preset);
}

std::span<const ViewportShortcutTargetDescriptor>
viewport_shortcut_target_catalog() {
  static const std::array<ViewportShortcutTargetDescriptor, 33> kDescriptors{{
      {"new_scene", quader_command::kSceneDocumentNew.string(),
       viewport_target_shortcut(ViewportShortcutKind::None,
                                ViewportShortcutContext::Global)},
      {"open_scene", quader_command::kSceneDocumentOpen.string(),
       viewport_target_shortcut(ViewportShortcutKind::None,
                                ViewportShortcutContext::Global)},
      {"save_scene", quader_command::kSceneDocumentSave.string(),
       viewport_target_shortcut(ViewportShortcutKind::None,
                                ViewportShortcutContext::Global)},
      {"save_scene_as", quader_command::kSceneDocumentSaveAs.string(),
       viewport_target_shortcut(ViewportShortcutKind::None,
                                ViewportShortcutContext::Global)},
      {"save_all_scenes", quader_command::kSceneDocumentSaveAll.string(),
       viewport_target_shortcut(ViewportShortcutKind::None,
                                ViewportShortcutContext::Global)},
      {"close_scene", quader_command::kSceneDocumentClose.string(),
       viewport_target_shortcut(ViewportShortcutKind::None,
                                ViewportShortcutContext::Global)},
      {"next_scene", quader_command::kSceneDocumentNext.string(),
       viewport_target_shortcut(ViewportShortcutKind::None,
                                ViewportShortcutContext::Global)},
      {"previous_scene", quader_command::kSceneDocumentPrevious.string(),
       viewport_target_shortcut(ViewportShortcutKind::None,
                                ViewportShortcutContext::Global)},
      {"select_all",
       {},
       viewport_target_shortcut(ViewportShortcutKind::SelectAll,
                                ViewportShortcutContext::Global)},
      {"copy_meshes",
       {},
       viewport_target_shortcut(ViewportShortcutKind::Copy,
                                ViewportShortcutContext::Global)},
      {"duplicate_selection", quader_command::kDuplicateSelection.string(),
       viewport_target_shortcut(ViewportShortcutKind::Duplicate,
                                ViewportShortcutContext::Global)},
      {"invert_selection",
       {},
       viewport_target_shortcut(ViewportShortcutKind::InvertSelection,
                                ViewportShortcutContext::Global)},
      {"repeat_last_action",
       {},
       viewport_target_shortcut(ViewportShortcutKind::RepeatLastAction,
                                ViewportShortcutContext::Global)},
      {"paste_meshes",
       {},
       viewport_target_shortcut(ViewportShortcutKind::Paste,
                                ViewportShortcutContext::Global)},
      {"undo", quader_command::kEditUndo.string(),
       viewport_target_shortcut(ViewportShortcutKind::Undo,
                                ViewportShortcutContext::Global)},
      {"redo", quader_command::kEditRedo.string(),
       viewport_target_shortcut(ViewportShortcutKind::Redo,
                                ViewportShortcutContext::Global)},
      {"delete", quader_command::kDeleteSelection.string(),
       viewport_target_shortcut(ViewportShortcutKind::DeleteSelection,
                                ViewportShortcutContext::Global)},
      {"merge_vertices",
       {},
       viewport_target_shortcut(ViewportShortcutKind::MergeActive)},
      {"merge_vertices_at_center",
       {},
       viewport_target_shortcut(ViewportShortcutKind::MergeCenter)},
      {"invert_mesh",
       {},
       viewport_target_shortcut(ViewportShortcutKind::FlipMeshNormals)},
      {"focus_all", quader_command::kViewportFocusAll.string(),
       viewport_target_shortcut(ViewportShortcutKind::FocusDocument)},
      {"focus_selection", quader_command::kViewportFrameSelection.string(), {}},
      {"focus_origin", quader_command::kViewportFocusOrigin.string(),
       viewport_target_shortcut(ViewportShortcutKind::FocusOrigin)},
      {"mirror_axis_x",
       {},
       viewport_mirror_target_shortcut(ViewportMirrorAxis::X)},
      {"mirror_axis_y",
       {},
       viewport_mirror_target_shortcut(ViewportMirrorAxis::Y)},
      {"mirror_axis_z",
       {},
       viewport_mirror_target_shortcut(ViewportMirrorAxis::Z)},
      {"flip_horizontal",
       {},
       viewport_target_shortcut(ViewportShortcutKind::FlipHorizontal)},
      {"flip_vertical",
       {},
       viewport_target_shortcut(ViewportShortcutKind::FlipVertical)},
      {"", "viewport.cancel_active_tool",
       viewport_target_shortcut(ViewportShortcutKind::CancelActiveTool)},
      {"", "viewport.toggle_maximized",
       viewport_target_shortcut(ViewportShortcutKind::ToggleMaximized)},
      {"", "viewport.delete_popup",
       viewport_target_shortcut(ViewportShortcutKind::DeletePopup)},
      {"", "viewport.add_popup",
       viewport_target_shortcut(ViewportShortcutKind::AddPopup)},
      {"", "viewport.marking_menu",
       viewport_target_shortcut(ViewportShortcutKind::MarkingMenu)},
  }};
  return kDescriptors;
}

std::vector<ViewportInputActionBindingDescriptor>
viewport_input_action_binding_catalog() {
  return {
      {
          .target_id = viewport_input_action_target_id("navigation.orbit"),
          .label = "Orbit View",
          .localization_key =
              "keybindings.target.viewport.navigation.orbit.label",
          .shortcut = "MiddleMouseDrag",
          .action = ViewportInputActionKind::Orbit,
      },
      {
          .target_id = viewport_input_action_target_id("navigation.orbit_snap"),
          .label = "Orbit View with Snap",
          .localization_key =
              "keybindings.target.viewport.navigation.orbit_snap.label",
          .shortcut = "Alt+MiddleMouseDrag",
          .action = ViewportInputActionKind::OrbitSnap,
      },
      {
          .target_id = viewport_input_action_target_id("navigation.pan"),
          .label = "Pan View",
          .localization_key =
              "keybindings.target.viewport.navigation.pan.label",
          .shortcut = "Shift+MiddleMouseDrag",
          .action = ViewportInputActionKind::Pan,
      },
      {
          .target_id = viewport_input_action_target_id("navigation.fly"),
          .label = "Fly Navigation",
          .localization_key =
              "keybindings.target.viewport.navigation.fly.label",
          .shortcut = "RightMouseDrag",
          .action = ViewportInputActionKind::Fly,
      },
      {
          .target_id =
              viewport_input_action_target_id("navigation.reset_fly_speed"),
          .label = "Reset Fly Speed",
          .localization_key =
              "keybindings.target.viewport.navigation.reset_fly_speed.label",
          .context = ViewportInputBindingContext::FlyNavigation,
          .shortcut = "MiddleMouse",
          .when = "viewportFocus && !textInputActive && navigation.flyActive",
          .action = ViewportInputActionKind::ResetFlySpeed,
      },
      {
          .target_id = viewport_input_action_target_id("navigation.cancel"),
          .label = "Cancel Navigation",
          .localization_key =
              "keybindings.target.viewport.navigation.cancel.label",
          .shortcut = "Escape",
          .when = "viewportFocus && !textInputActive && navigation.active",
          .weight = 30,
          .action = ViewportInputActionKind::CancelNavigation,
      },
      {
          .target_id = viewport_input_action_target_id("navigation.zoom"),
          .label = "Zoom / Adjust Fly Speed",
          .localization_key =
              "keybindings.target.viewport.navigation.zoom.label",
          .shortcut = "Wheel",
          .action = ViewportInputActionKind::Zoom,
      },
      {
          .target_id =
              viewport_input_action_target_id("navigation.zoom_to_cursor"),
          .label = "Zoom to Cursor / Adjust Fly Speed",
          .localization_key =
              "keybindings.target.viewport.navigation.zoom_to_cursor.label",
          .shortcut = "Ctrl+Wheel",
          .action = ViewportInputActionKind::ZoomToCursor,
      },
      {
          .target_id =
              viewport_input_action_target_id("navigation.fly_forward"),
          .label = "Fly Forward",
          .localization_key =
              "keybindings.target.viewport.navigation.fly_forward.label",
          .context = ViewportInputBindingContext::FlyNavigation,
          .shortcut = "W",
          .when = "viewportFocus && !textInputActive && navigation.flyActive",
          .action = ViewportInputActionKind::FlyForward,
      },
      {
          .target_id =
              viewport_input_action_target_id("navigation.fly_backward"),
          .label = "Fly Backward",
          .localization_key =
              "keybindings.target.viewport.navigation.fly_backward.label",
          .context = ViewportInputBindingContext::FlyNavigation,
          .shortcut = "S",
          .when = "viewportFocus && !textInputActive && navigation.flyActive",
          .action = ViewportInputActionKind::FlyBackward,
      },
      {
          .target_id = viewport_input_action_target_id("navigation.fly_right"),
          .label = "Fly Right",
          .localization_key =
              "keybindings.target.viewport.navigation.fly_right.label",
          .context = ViewportInputBindingContext::FlyNavigation,
          .shortcut = "D",
          .when = "viewportFocus && !textInputActive && navigation.flyActive",
          .action = ViewportInputActionKind::FlyRight,
      },
      {
          .target_id = viewport_input_action_target_id("navigation.fly_left"),
          .label = "Fly Left",
          .localization_key =
              "keybindings.target.viewport.navigation.fly_left.label",
          .context = ViewportInputBindingContext::FlyNavigation,
          .shortcut = "A",
          .when = "viewportFocus && !textInputActive && navigation.flyActive",
          .action = ViewportInputActionKind::FlyLeft,
      },
      {
          .target_id = viewport_input_action_target_id("selection.cycle_depth"),
          .action_id = "viewport.selection.cycle_depth",
          .label = "Cycle Mesh Selection Depth (Orthographic)",
          .localization_key =
              "keybindings.target.viewport.selection.cycle_depth.label",
          .context = ViewportInputBindingContext::Viewport,
          .shortcut = "LeftMouseDoubleClick",
          .when = "viewportFocus && viewport.orthographic && !textInputActive && "
                  "!navigation.active && !modalTool.active && "
                  "!activeTool.ownsViewport && !transformDrag.active && "
                  "!operationPreview.active",
          .selection_mode_mask =
              viewport_selection_mode_mask(ViewportSelectionMode::Object),
          .weight = 30,
          .action = ViewportInputActionKind::CycleSelectionDepth,
      },
      {
          .target_id = viewport_input_action_target_id("selection.add_depth"),
          .action_id = "viewport.selection.add_depth",
          .label = "Add Mesh at Selection Depth (Orthographic)",
          .localization_key =
              "keybindings.target.viewport.selection.add_depth.label",
          .context = ViewportInputBindingContext::Viewport,
          .shortcut = "Shift+LeftMouseDoubleClick",
          .when = "viewportFocus && viewport.orthographic && !textInputActive && "
                  "!navigation.active && !modalTool.active && "
                  "!activeTool.ownsViewport && !transformDrag.active && "
                  "!operationPreview.active",
          .selection_mode_mask =
              viewport_selection_mode_mask(ViewportSelectionMode::Object),
          .weight = 30,
          .action = ViewportInputActionKind::AddSelectionDepth,
      },
      {
          .target_id = viewport_input_action_target_id("selection.remove_depth"),
          .action_id = "viewport.selection.remove_depth",
          .label = "Remove Mesh at Selection Depth (Orthographic)",
          .localization_key =
              "keybindings.target.viewport.selection.remove_depth.label",
          .context = ViewportInputBindingContext::Viewport,
          .shortcut = "Ctrl+LeftMouseDoubleClick",
          .when = "viewportFocus && viewport.orthographic && !textInputActive && "
                  "!navigation.active && !modalTool.active && "
                  "!activeTool.ownsViewport && !transformDrag.active && "
                  "!operationPreview.active",
          .selection_mode_mask =
              viewport_selection_mode_mask(ViewportSelectionMode::Object),
          .weight = 30,
          .action = ViewportInputActionKind::RemoveSelectionDepth,
      },
      {
          .target_id =
              viewport_input_action_target_id("material.apply_face_to_mesh"),
          .action_id = "viewport.material.apply_face_to_mesh",
          .label = "Apply Clicked Face Material to Mesh",
          .localization_key = "keybindings.target.viewport.material."
                              "apply_face_to_mesh.label",
          .context = ViewportInputBindingContext::Viewport,
          .shortcut = "Ctrl+RightMouseDoubleClick",
          .when = "viewportFocus && !textInputActive && !navigation.active && "
                  "!modalTool.active && !activeTool.ownsViewport && "
                  "!transformDrag.active && !operationPreview.active",
          .selection_mode_mask = kMaterialSourceSelectionModes,
          .weight = 30,
          .action = ViewportInputActionKind::ApplyFaceMaterialToMesh,
      },
      {
          .target_id = viewport_input_action_target_id(
              "material.apply_selected_face_to_face"),
          .action_id = "viewport.material.apply_selected_face_to_face",
          .label = "Apply Selected Face Material to Clicked Face",
          .localization_key = "keybindings.target.viewport.material."
                              "apply_selected_face_to_face.label",
          .context = ViewportInputBindingContext::Viewport,
          .shortcut = "Shift+RightMouseClick",
          .when = "viewportFocus && !textInputActive && !navigation.active && "
                  "!modalTool.active && !activeTool.ownsViewport && "
                  "!transformDrag.active && !operationPreview.active",
          .selection_mode_mask =
              viewport_selection_mode_mask(ViewportSelectionMode::Face),
          .weight = 30,
          .action = ViewportInputActionKind::ApplySelectedFaceMaterialToFace,
      },
      {
          .target_id = viewport_input_action_target_id(
              "material.apply_selected_face_to_mesh"),
          .action_id = "viewport.material.apply_selected_face_to_mesh",
          .label = "Apply Selected Face Material to Clicked Mesh",
          .localization_key = "keybindings.target.viewport.material."
                              "apply_selected_face_to_mesh.label",
          .context = ViewportInputBindingContext::Viewport,
          .shortcut = "Shift+RightMouseDoubleClick",
          .when = "viewportFocus && !textInputActive && !navigation.active && "
                  "!modalTool.active && !activeTool.ownsViewport && "
                  "!transformDrag.active && !operationPreview.active",
          .selection_mode_mask =
              viewport_selection_mode_mask(ViewportSelectionMode::Face),
          .weight = 30,
          .action = ViewportInputActionKind::ApplySelectedFaceMaterialToMesh,
      },
      {
          .target_id =
              viewport_input_action_target_id("material.apply_face_to_selection"),
          .action_id = "viewport.material.apply_face_to_selection",
          .label = "Apply Clicked Face Material to Selected Faces",
          .localization_key = "keybindings.target.viewport.material."
                              "apply_face_to_selection.label",
          .context = ViewportInputBindingContext::Viewport,
          .shortcut = "Ctrl+RightMouseClick",
          .when = "viewportFocus && !textInputActive && !navigation.active && "
                  "!modalTool.active && !activeTool.ownsViewport && "
                  "!transformDrag.active && !operationPreview.active",
          .selection_mode_mask =
              viewport_selection_mode_mask(ViewportSelectionMode::Face),
          .weight = 30,
          .action = ViewportInputActionKind::ApplyFaceMaterialToSelection,
      },
      {
          .target_id =
              viewport_input_action_target_id("material.transfer_face_texture"),
          .action_id = "viewport.material.transfer_face_texture",
          .label = "Continue Texture from Selected Face",
          .localization_key = "keybindings.target.viewport.material."
                              "transfer_face_texture.label",
          .context = ViewportInputBindingContext::Viewport,
          .shortcut = "Alt+RightMouse",
          .when = "viewportFocus && !textInputActive && !navigation.active && "
                  "!modalTool.active && !activeTool.ownsViewport && "
                  "!transformDrag.active && !operationPreview.active",
          .selection_mode_mask =
              viewport_selection_mode_mask(ViewportSelectionMode::Face),
          .weight = 30,
          .action = ViewportInputActionKind::TransferFaceTexture,
      },
      {
          .target_id =
              viewport_input_action_target_id("curve.toggle_point_smoothness"),
          .action_id = "viewport.curve.toggle_point_smoothness",
          .label = "Toggle Curve Point Corner / Smooth",
          .localization_key =
              "keybindings.target.viewport.curve.toggle_point_smoothness.label",
          .context = ViewportInputBindingContext::CurveTool,
          .shortcut = "Alt+LeftMouse",
          .when = "viewportFocus && !textInputActive && !navigation.active && "
                  "((activeTool.curve && activeTool.ownsViewport) || "
                  "(curveControls.editable && selectionMode.object && "
                  "!activeTool.ownsViewport && !modalTool.active)) && "
                  "!transformDrag.active && !operationPreview.active",
          .weight = 30,
          .action = ViewportInputActionKind::ToggleCurvePointSmoothness,
      },
      {
          .target_id = viewport_input_action_target_id("modal_tool.finalize"),
          .action_id = "tools.finalize",
          .label = "Finalize Tool",
          .localization_key =
              "keybindings.action.tools.finalize.label",
          .context = ViewportInputBindingContext::ModalTool,
          .shortcut = "Space",
          .when = "viewportFocus && !textInputActive && "
                  "!operationPreview.active && !activeTool.create_primitive && "
                  "(modalTool.active || activeTool.ownsViewport || "
                  "activeTool.nativeLifecycle) && "
                  "!navigation.active",
          .weight = 30,
          .action = ViewportInputActionKind::ModalToolFinalize,
      },
      {
          .target_id = viewport_input_action_target_id("modal_tool.mode_cycle"),
          .label = "Cycle Cut Keep Mode",
          .localization_key =
              "keybindings.target.viewport.modal_tool.mode_cycle.label",
          .context = ViewportInputBindingContext::ModalTool,
          .shortcut = "F",
          .when = "viewportFocus && !textInputActive && activeTool.cut && "
                  "!navigation.active",
          .weight = 30,
          .action = ViewportInputActionKind::ModalToolModeCycle,
      },
      {
          .target_id = viewport_input_action_target_id("quick_pivot"),
          .label = "Quick Pivot",
          .localization_key =
              "keybindings.target.viewport.quick_pivot.label",
          .context = ViewportInputBindingContext::Viewport,
          .shortcut = "D",
          .when = "viewportFocus && !textInputActive && !navigation.active && "
                  "!modalTool.active && !activeTool.ownsViewport",
          .weight = 20,
          .action = ViewportInputActionKind::QuickPivot,
      },
      {
          .target_id =
              viewport_input_action_target_id("transform.vertex_snap"),
          .action_id = "viewport.transform.vertex_snap",
          .label = "Hold Vertex Snap",
          .localization_key =
              "keybindings.target.viewport.transform.vertex_snap.label",
          .context = ViewportInputBindingContext::Viewport,
          .shortcut = "V",
          .when = "viewportFocus && !textInputActive && !navigation.active && "
                  "(transformDrag.active || activeTool.move || "
                  "activeTool.extrude || curveControls.editable)",
          .weight = 40,
          .action = ViewportInputActionKind::TransformVertexSnap,
      },
      {
          .target_id =
              viewport_input_action_target_id("linear_array.constraint_x"),
          .label = "Linear Array X Constraint",
          .localization_key =
              "keybindings.target.viewport.linear_array.constraint_x.label",
          .context = ViewportInputBindingContext::LinearArrayTool,
          .shortcut = "X",
          .when = "viewportFocus && !textInputActive && !navigation.active && "
                  "activeTool.linear_array",
          .weight = 40,
          .action = ViewportInputActionKind::ToolConstraintX,
      },
      {
          .target_id =
              viewport_input_action_target_id("extrude.constraint_x"),
          .label = "Extrude X Constraint",
          .localization_key =
              "keybindings.target.viewport.extrude.constraint_x.label",
          .context = ViewportInputBindingContext::ExtrudeTool,
          .shortcut = "X",
          .when = "viewportFocus && !textInputActive && !navigation.active && "
                  "activeTool.extrude",
          .weight = 40,
          .action = ViewportInputActionKind::ToolConstraintX,
      },
      {
          .target_id =
              viewport_input_action_target_id("extrude.constraint_y"),
          .label = "Extrude Y Constraint",
          .localization_key =
              "keybindings.target.viewport.extrude.constraint_y.label",
          .context = ViewportInputBindingContext::ExtrudeTool,
          .shortcut = "Y",
          .when = "viewportFocus && !textInputActive && !navigation.active && "
                  "activeTool.extrude",
          .weight = 40,
          .action = ViewportInputActionKind::ToolConstraintY,
      },
      {
          .target_id =
              viewport_input_action_target_id("extrude.constraint_z"),
          .label = "Extrude Z Constraint",
          .localization_key =
              "keybindings.target.viewport.extrude.constraint_z.label",
          .context = ViewportInputBindingContext::ExtrudeTool,
          .shortcut = "Z",
          .when = "viewportFocus && !textInputActive && !navigation.active && "
                  "activeTool.extrude",
          .weight = 40,
          .action = ViewportInputActionKind::ToolConstraintZ,
      },
      {
          .target_id =
              viewport_input_action_target_id("linear_array.constraint_y"),
          .label = "Linear Array Y Constraint",
          .localization_key =
              "keybindings.target.viewport.linear_array.constraint_y.label",
          .context = ViewportInputBindingContext::LinearArrayTool,
          .shortcut = "Y",
          .when = "viewportFocus && !textInputActive && !navigation.active && "
                  "activeTool.linear_array",
          .weight = 40,
          .action = ViewportInputActionKind::ToolConstraintY,
      },
      {
          .target_id =
              viewport_input_action_target_id("linear_array.constraint_z"),
          .label = "Linear Array Z Constraint",
          .localization_key =
              "keybindings.target.viewport.linear_array.constraint_z.label",
          .context = ViewportInputBindingContext::LinearArrayTool,
          .shortcut = "Z",
          .when = "viewportFocus && !textInputActive && !navigation.active && "
                  "activeTool.linear_array",
          .weight = 40,
          .action = ViewportInputActionKind::ToolConstraintZ,
      },
      {
          .target_id = viewport_input_action_target_id(
              "linear_array.increase_copies"),
          .label = "Increase Linear Array Copies",
          .localization_key = "keybindings.target.viewport.linear_array."
                              "increase_copies.label",
          .context = ViewportInputBindingContext::LinearArrayTool,
          .shortcut = "Shift+WheelUp",
          .when = "viewportFocus && !textInputActive && !navigation.active && "
                  "activeTool.linear_array",
          .weight = 40,
          .action = ViewportInputActionKind::LinearArrayIncreaseCopies,
      },
      {
          .target_id = viewport_input_action_target_id(
              "linear_array.decrease_copies"),
          .label = "Decrease Linear Array Copies",
          .localization_key = "keybindings.target.viewport.linear_array."
                              "decrease_copies.label",
          .context = ViewportInputBindingContext::LinearArrayTool,
          .shortcut = "Shift+WheelDown",
          .when = "viewportFocus && !textInputActive && !navigation.active && "
                  "activeTool.linear_array",
          .weight = 40,
          .action = ViewportInputActionKind::LinearArrayDecreaseCopies,
      },
      {
          .target_id = viewport_input_action_target_id(
              "radial_array.increase_copies"),
          .label = "Increase Radial Array Copies",
          .localization_key = "keybindings.target.viewport.radial_array."
                              "increase_copies.label",
          .context = ViewportInputBindingContext::RadialArrayTool,
          .shortcut = "Shift+WheelUp",
          .when = "viewportFocus && !textInputActive && !navigation.active && "
                  "activeTool.radial_array",
          .weight = 40,
          .action = ViewportInputActionKind::RadialArrayIncreaseCopies,
      },
      {
          .target_id = viewport_input_action_target_id(
              "radial_array.decrease_copies"),
          .label = "Decrease Radial Array Copies",
          .localization_key = "keybindings.target.viewport.radial_array."
                              "decrease_copies.label",
          .context = ViewportInputBindingContext::RadialArrayTool,
          .shortcut = "Shift+WheelDown",
          .when = "viewportFocus && !textInputActive && !navigation.active && "
                  "activeTool.radial_array",
          .weight = 40,
          .action = ViewportInputActionKind::RadialArrayDecreaseCopies,
      },
      {
          .target_id = viewport_input_action_target_id(
              "radial_array.increase_repeats"),
          .label = "Increase Radial Array Repeats",
          .localization_key = "keybindings.target.viewport.radial_array."
                              "increase_repeats.label",
          .context = ViewportInputBindingContext::RadialArrayTool,
          .shortcut = "Ctrl+WheelUp",
          .when = "viewportFocus && !textInputActive && !navigation.active && "
                  "activeTool.radial_array",
          .weight = 40,
          .action = ViewportInputActionKind::RadialArrayIncreaseRepeats,
      },
      {
          .target_id = viewport_input_action_target_id(
              "radial_array.decrease_repeats"),
          .label = "Decrease Radial Array Repeats",
          .localization_key = "keybindings.target.viewport.radial_array."
                              "decrease_repeats.label",
          .context = ViewportInputBindingContext::RadialArrayTool,
          .shortcut = "Ctrl+WheelDown",
          .when = "viewportFocus && !textInputActive && !navigation.active && "
                  "activeTool.radial_array",
          .weight = 40,
          .action = ViewportInputActionKind::RadialArrayDecreaseRepeats,
      },
      {
          .target_id = viewport_input_action_target_id(
              "radial_array.toggle_instances"),
          .label = "Toggle Radial Array Instances",
          .localization_key = "keybindings.target.viewport.radial_array."
                              "toggle_instances.label",
          .context = ViewportInputBindingContext::RadialArrayTool,
          .shortcut = "I",
          .when = "viewportFocus && !textInputActive && !navigation.active && "
                  "activeTool.radial_array",
          .weight = 40,
          .action = ViewportInputActionKind::RadialArrayToggleInstances,
      },
      {
          .target_id = viewport_input_action_target_id("radial_array.finalize"),
          .action_id = "tools.finalize",
          .label = "Finalize Radial Array",
          .localization_key =
              "keybindings.target.viewport.radial_array.finalize.label",
          .context = ViewportInputBindingContext::RadialArrayTool,
          .shortcut = "Enter",
          .when = "viewportFocus && !textInputActive && !navigation.active && "
                  "activeTool.radial_array",
          .weight = 40,
          .action = ViewportInputActionKind::ModalToolFinalize,
      },
  };
}

bool viewport_input_action_for_target_id(std::string_view target_id,
                                         ViewportInputActionKind &action) {
  for (const ViewportInputActionBindingDescriptor &descriptor :
       viewport_input_action_binding_catalog()) {
    if (descriptor.target_id == target_id) {
      action = descriptor.action;
      return true;
    }
  }
  return false;
}

std::vector<ViewportShortcutBindingDescriptor>
viewport_shortcut_binding_catalog() {
  std::vector<ViewportShortcutBindingDescriptor> bindings;
  for (const quader_host_contract::HostShortcutDescriptor &descriptor :
       quader_host_contract::shortcut_catalog()) {
    bindings.push_back({
        .target_id = std::string(descriptor.target_id),
        .shortcut = viewport_shortcut_label(descriptor.primary_modifier,
                                            descriptor.shift, descriptor.alt,
                                            descriptor.key),
    });
  }
  return bindings;
}

std::vector<ViewportGridShortcutBindingDescriptor>
viewport_grid_shortcut_binding_catalog() {
  std::vector<ViewportGridShortcutBindingDescriptor> bindings;
  const auto presets = quader_host_contract::grid_preset_catalog();
  bindings.reserve(presets.size());
  for (std::size_t index = 0u; index < presets.size(); ++index) {
    if (presets[index].shortcut.empty()) {
      continue;
    }
    bindings.push_back({
        .preset = static_cast<int>(index + 1u),
        .shortcut = std::string(presets[index].shortcut),
    });
  }
  return bindings;
}

std::vector<ViewportGridPresetDescriptor> viewport_grid_preset_catalog() {
  const auto presets = quader_host_contract::grid_preset_catalog();
  std::vector<ViewportGridPresetDescriptor> catalog;
  catalog.reserve(presets.size());
  for (std::size_t index = 0u; index < presets.size(); ++index) {
    catalog.push_back({
        .preset = static_cast<int>(index + 1u),
        .map_units = presets[index].map_units,
        .world_units = presets[index].world_units,
        .is_default = presets[index].default_preset,
    });
  }
  return catalog;
}

std::optional<ViewportView>
viewport_view_for_command_target_id(std::string_view target_id) {
  if (target_id == quader_command::kViewportViewPerspective.string()) {
    return ViewportView::Perspective;
  }
  if (target_id == quader_command::kViewportViewTop.string()) {
    return ViewportView::Top;
  }
  if (target_id == quader_command::kViewportViewBottom.string()) {
    return ViewportView::Bottom;
  }
  if (target_id == quader_command::kViewportViewLeft.string()) {
    return ViewportView::Left;
  }
  if (target_id == quader_command::kViewportViewRight.string()) {
    return ViewportView::Side;
  }
  if (target_id == quader_command::kViewportViewFront.string()) {
    return ViewportView::Front;
  }
  if (target_id == quader_command::kViewportViewBack.string()) {
    return ViewportView::Back;
  }
  return std::nullopt;
}

std::optional<std::string_view>
viewport_command_target_id_for_view(ViewportView view) {
  switch (view) {
  case ViewportView::Perspective:
    return quader_command::kViewportViewPerspective.string();
  case ViewportView::Top:
    return quader_command::kViewportViewTop.string();
  case ViewportView::Bottom:
    return quader_command::kViewportViewBottom.string();
  case ViewportView::Left:
    return quader_command::kViewportViewLeft.string();
  case ViewportView::Side:
    return quader_command::kViewportViewRight.string();
  case ViewportView::Front:
    return quader_command::kViewportViewFront.string();
  case ViewportView::Back:
    return quader_command::kViewportViewBack.string();
  }
  return std::nullopt;
}

std::string_view viewport_tool_contract_id(ViewportToolId tool) {
  const ToolDescriptor *descriptor = find_viewport_tool_descriptor(tool);
  return descriptor != nullptr ? descriptor->id : std::string_view{};
}

bool viewport_tool_has_host_contract(ViewportToolId tool) {
  const std::string_view id = viewport_tool_contract_id(tool);
  return !id.empty() &&
         quader_host_contract::find_tool_descriptor(id) != nullptr;
}

bool viewport_tool_has_native_controller(ViewportToolId tool) {
  const ToolDescriptor *descriptor = find_viewport_tool_descriptor(tool);
  return descriptor != nullptr &&
         (descriptor->editor_owned_native_lifecycle ||
          viewport_tool_has_host_contract(tool));
}

bool viewport_tool_allows_selection_fallback(ViewportToolId tool) {
  const ToolDescriptor *descriptor = find_viewport_tool_descriptor(tool);
  return descriptor != nullptr && descriptor->allows_selection_fallback;
}

bool viewport_tool_from_contract_id(std::string_view id, ViewportToolId &tool) {
  if (const ToolDescriptor *descriptor = find_viewport_tool_descriptor(id)) {
    tool = descriptor->tool;
    return true;
  }
  if (const quader_host_contract::HostActionDescriptor *descriptor =
          quader_host_contract::find_action_descriptor(id);
      descriptor != nullptr &&
      descriptor->kind == quader_host_contract::HostActionKind::Tool &&
      !descriptor->tool_id.empty()) {
    return viewport_tool_from_contract_id(descriptor->tool_id, tool);
  }
  return false;
}

bool viewport_selection_mode_from_contract_id(std::string_view id,
                                              ViewportSelectionMode &mode) {
  if (const std::optional<quader_host_contract::HostSelectionMode> host_mode =
          host_selection_mode_from_id(id)) {
    mode = host_selection_mode_to_viewport(*host_mode);
    return true;
  }
  return false;
}

std::optional<std::string>
viewport_command_target_id_for_contract_target(std::string_view target_id) {
  for (const ViewportShortcutTargetDescriptor &descriptor :
       viewport_shortcut_target_catalog()) {
    if (!descriptor.contract_target_id.empty() &&
        descriptor.contract_target_id == target_id) {
      return viewport_command_target_id_for_shortcut_descriptor(descriptor);
    }
  }

  ViewportToolId tool = ViewportToolId::Select;
  if (viewport_tool_from_contract_id(target_id, tool)) {
    return quader_tool_activation_command_target_id(
        viewport_tool_contract_id(tool));
  }

  ViewportSelectionMode mode = ViewportSelectionMode::Object;
  if (viewport_selection_mode_from_contract_id(target_id, mode)) {
    return viewport_selection_mode_target_id(viewport_selection_mode_id(mode));
  }

  return std::nullopt;
}

bool viewport_shortcut_from_contract_target(std::string_view target_id,
                                            ViewportShortcut &shortcut) {
  for (const ViewportShortcutTargetDescriptor &descriptor :
       viewport_shortcut_target_catalog()) {
    if (!descriptor.contract_target_id.empty() &&
        descriptor.contract_target_id == target_id) {
      shortcut = descriptor.shortcut;
      return true;
    }
  }

  ViewportToolId tool = ViewportToolId::Select;
  if (viewport_tool_from_contract_id(target_id, tool)) {
    shortcut.kind = ViewportShortcutKind::ActivateTool;
    shortcut.context = ViewportShortcutContext::Viewport;
    shortcut.tool = tool;
    return true;
  }

  ViewportSelectionMode mode = ViewportSelectionMode::Object;
  if (viewport_selection_mode_from_contract_id(target_id, mode)) {
    shortcut.kind = ViewportShortcutKind::SetSelectionMode;
    shortcut.context = ViewportShortcutContext::Viewport;
    shortcut.selection_mode = mode;
    return true;
  }

  return false;
}

bool viewport_shortcut_for_command_target_id(std::string_view target_id,
                                             ViewportShortcut &shortcut) {
  if (const std::optional<ViewportView> view =
          viewport_view_for_command_target_id(target_id)) {
    shortcut = viewport_view_target_shortcut(*view);
    return true;
  }

  for (const ViewportShortcutTargetDescriptor &descriptor :
       viewport_shortcut_target_catalog()) {
    const std::optional<std::string> command_target =
        viewport_command_target_id_for_shortcut_descriptor(descriptor);
    if (command_target && *command_target == target_id) {
      shortcut = descriptor.shortcut;
      return true;
    }
  }

  if (const std::optional<std::string_view> tool_id =
          quader_tool_id_from_activation_command_target(target_id)) {
    ViewportToolId tool = ViewportToolId::Select;
    if (!viewport_tool_from_contract_id(*tool_id, tool)) {
      return false;
    }
    shortcut.kind = ViewportShortcutKind::ActivateTool;
    shortcut.context = ViewportShortcutContext::Viewport;
    shortcut.tool = tool;
    return true;
  }

  if (const std::optional<std::string_view> selection_mode_id =
          quader_selection_mode_id_from_command_target(target_id)) {
    ViewportSelectionMode mode = ViewportSelectionMode::Object;
    if (!viewport_selection_mode_from_contract_id(*selection_mode_id, mode)) {
      return false;
    }
    shortcut.kind = ViewportShortcutKind::SetSelectionMode;
    shortcut.context = ViewportShortcutContext::Viewport;
    shortcut.selection_mode = mode;
    return true;
  }

  if (const std::optional<std::string_view> grid_preset_id =
          quader_grid_preset_id_from_command_target(target_id)) {
    const std::string preset_text(*grid_preset_id);
    try {
      shortcut.kind = ViewportShortcutKind::SetGridPreset;
      shortcut.context = ViewportShortcutContext::Viewport;
      shortcut.grid_preset = std::stoi(preset_text);
      return true;
    } catch (...) {
      return false;
    }
  }

  return false;
}

bool viewport_target_requires_router_dispatch(std::string_view target_id) {
  // This retained binding target is a Box command alias. Routing it through
  // generic tool activation would ignore shape changes in an active session.
  if (target_id == quader_command::kToolsActivateCreatePrimitive.string()) {
    return false;
  }
  const bool known_viewport_tool_target = [&]() {
    const std::optional<std::string_view> tool_id =
        quader_tool_id_from_activation_command_target(target_id);
    if (!tool_id) {
      return false;
    }
    ViewportToolId tool = ViewportToolId::Select;
    return viewport_tool_from_contract_id(*tool_id, tool);
  }();

  return viewport_view_for_command_target_id(target_id).has_value() ||
         target_id == quader_command::kViewportFocusAll.string() ||
         target_id == quader_command::kViewportFocusOrigin.string() ||
         target_id == quader_command::kViewportGridIncrement.string() ||
         target_id == quader_command::kViewportGridDecrement.string() ||
         target_id == "viewport.cancel_active_tool" ||
         target_id == "viewport.toggle_maximized" ||
         target_id == "viewport.delete_popup" ||
         target_id == "viewport.add_popup" ||
         target_id == "viewport.marking_menu" ||
         known_viewport_tool_target ||
         quader_selection_mode_id_from_command_target(target_id).has_value() ||
         quader_grid_preset_id_from_command_target(target_id).has_value();
}

bool viewport_shortcuts_match(const ViewportShortcut &lhs,
                              const ViewportShortcut &rhs) {
  if (lhs.kind != rhs.kind || lhs.context != rhs.context) {
    return false;
  }

  switch (lhs.kind) {
  case ViewportShortcutKind::ActivateTool:
    return lhs.tool == rhs.tool;
  case ViewportShortcutKind::SetSelectionMode:
    return lhs.selection_mode == rhs.selection_mode;
  case ViewportShortcutKind::SetGridPreset:
    return lhs.grid_preset == rhs.grid_preset;
  case ViewportShortcutKind::SetView:
    return lhs.view == rhs.view;
  case ViewportShortcutKind::MirrorAxis:
    return lhs.mirror_axis == rhs.mirror_axis;
  case ViewportShortcutKind::None:
  case ViewportShortcutKind::FocusDocument:
  case ViewportShortcutKind::FocusOrigin:
  case ViewportShortcutKind::ToggleMaximized:
  case ViewportShortcutKind::CancelActiveTool:
  case ViewportShortcutKind::DeletePopup:
  case ViewportShortcutKind::AddPopup:
  case ViewportShortcutKind::MarkingMenu:
  case ViewportShortcutKind::DeleteSelection:
  case ViewportShortcutKind::SelectAll:
  case ViewportShortcutKind::InvertSelection:
  case ViewportShortcutKind::Copy:
  case ViewportShortcutKind::Paste:
  case ViewportShortcutKind::Duplicate:
  case ViewportShortcutKind::Undo:
  case ViewportShortcutKind::Redo:
  case ViewportShortcutKind::RepeatLastAction:
  case ViewportShortcutKind::MergeActive:
  case ViewportShortcutKind::MergeCenter:
  case ViewportShortcutKind::FlipMeshNormals:
  case ViewportShortcutKind::FlipHorizontal:
  case ViewportShortcutKind::FlipVertical:
    return true;
  }
  return false;
}

std::optional<std::string> viewport_command_target_id_for_router_shortcut(
    const ViewportShortcut &shortcut) {
  if (shortcut.kind == ViewportShortcutKind::None) {
    return std::nullopt;
  }
  for (const ViewportShortcutTargetDescriptor &descriptor :
       viewport_shortcut_target_catalog()) {
    const std::optional<std::string> command_target =
        viewport_command_target_id_for_shortcut_descriptor(descriptor);
    if (!command_target ||
        viewport_target_requires_router_dispatch(*command_target) ||
        !viewport_shortcuts_match(shortcut, descriptor.shortcut)) {
      continue;
    }
    return *command_target;
  }
  return std::nullopt;
}

bool viewport_router_shortcut_for_target_id(std::string_view target_id,
                                            ViewportShortcut &shortcut) {
  return viewport_target_requires_router_dispatch(target_id) &&
         viewport_shortcut_for_command_target_id(target_id, shortcut);
}

bool viewport_router_shortcut_for_target_id(std::string_view target_id,
                                            int current_grid_preset,
                                            ViewportShortcut &shortcut) {
  int requested_grid_preset = current_grid_preset;
  if (target_id == quader_command::kViewportGridIncrement.string()) {
    ++requested_grid_preset;
  } else if (target_id == quader_command::kViewportGridDecrement.string()) {
    --requested_grid_preset;
  } else {
    return viewport_router_shortcut_for_target_id(target_id, shortcut);
  }

  shortcut = {
      .kind = ViewportShortcutKind::SetGridPreset,
      .context = ViewportShortcutContext::Viewport,
      .grid_preset = viewport_clamp_grid_preset(requested_grid_preset),
  };
  return true;
}

} // namespace quader

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_commands_api.hpp"
#include "core/user_config_paths.hpp"

#include "api/quader_api.hpp"
#include "api/quader_mesh_arrangement_catalog.hpp"
#ifndef NDEBUG
#include "api/quader_dev_tools_api.hpp"
#endif
#include "api/quader_project_api.hpp"
#include "api/quader_scene_documents_api.hpp"
#include "api/quader_preferences_api.hpp"
#include "api/quader_tools_api.hpp"
#include "api/quader_ui_api.hpp"
#include "api/quader_viewport_api.hpp"
#include "core/public_command_targets.hpp"
#include "core/release_features.hpp"
#include "editor/metadata/release_feature_requirements.hpp"
#include "diagnostics/profile.hpp"
#include "editor/commands/editor_command_registry.hpp"
#include "editor/commands/modeling_command_tool_catalog.hpp"
#include "editor/input/quader_keybinding_service.hpp"
#include "editor/input/quader_keybinding_target_catalog.hpp"
#include "editor/input/viewport_input_binding_adapter.hpp"
#include "editor/modeling/services/modeling_action_catalog.hpp"
#include "editor/modeling/services/modeling_instant_operation_catalog.hpp"
#include "editor/tools/viewport_tool_catalog.hpp"
#include "io/qdr/qdr_atomic_write.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"
#include "viewport/viewport_input_types.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <utility>

namespace quader {
namespace {

/// Maps one native modeling action token to its dedicated public command.
struct NativeModelingActionCommand final {
  std::string_view action_id;
  QuaderCommandId command_id;
};

constexpr std::array<NativeModelingActionCommand, 7>
    kNativeModelingActionCommands{{
        {"snap_meshes_to_floor",
         quader_command::kModelingSnapMeshesToFloor},
        {"bake_transforms", quader_command::kModelingBakeTransforms},
        {"bake_location", quader_command::kModelingBakeLocation},
        {"bake_rotation", quader_command::kModelingBakeRotation},
        {"bake_scale", quader_command::kModelingBakeScale},
        {"copy_pivot_from_active",
         quader_command::kModelingCopyPivotFromActive},
        {"snap_vertices_to_floor",
         quader_command::kModelingSnapVerticesToFloor},
    }};

/// Connects a typed texture command to the modeling owner action.
struct FaceTextureCommand final {
  QuaderCommandId command_id;
  QuaderFaceTextureAction action;
  bool mesh_level = false;
};

constexpr std::array<FaceTextureCommand, 28> kFaceTextureCommands{{
        {quader_command::kModelingTextureAlignToGrid,
         QuaderFaceTextureAction::AlignToGrid},
        {quader_command::kModelingTextureAlignToFace,
         QuaderFaceTextureAction::AlignToFace},
        {quader_command::kModelingTextureAlignToView,
         QuaderFaceTextureAction::AlignToView},
        {quader_command::kModelingTextureProjectCube,
         QuaderFaceTextureAction::ProjectCube, true},
        {quader_command::kModelingTextureProjectCylinder,
         QuaderFaceTextureAction::ProjectCylinder, true},
        {quader_command::kModelingTextureProjectSphere,
         QuaderFaceTextureAction::ProjectSphere, true},
        {quader_command::kModelingTextureScaleXUp,
         QuaderFaceTextureAction::ScaleXUp},
        {quader_command::kModelingTextureScaleXDown,
         QuaderFaceTextureAction::ScaleXDown},
        {quader_command::kModelingTextureScaleYUp,
         QuaderFaceTextureAction::ScaleYUp},
        {quader_command::kModelingTextureScaleYDown,
         QuaderFaceTextureAction::ScaleYDown},
        {quader_command::kModelingTextureShiftLeft,
         QuaderFaceTextureAction::ShiftLeft},
        {quader_command::kModelingTextureShiftRight,
         QuaderFaceTextureAction::ShiftRight},
        {quader_command::kModelingTextureShiftUp,
         QuaderFaceTextureAction::ShiftUp},
        {quader_command::kModelingTextureShiftDown,
         QuaderFaceTextureAction::ShiftDown},
        {quader_command::kModelingTextureRotateClockwise,
         QuaderFaceTextureAction::RotateClockwise},
        {quader_command::kModelingTextureRotateCounterClockwise,
         QuaderFaceTextureAction::RotateCounterClockwise},
        {quader_command::kModelingTextureFlipTextureHorizontal,
         QuaderFaceTextureAction::FlipTextureHorizontal},
        {quader_command::kModelingTextureFlipTextureVertical,
         QuaderFaceTextureAction::FlipTextureVertical},
        {quader_command::kModelingTextureFlipFaceHorizontal,
         QuaderFaceTextureAction::FlipFaceHorizontal},
        {quader_command::kModelingTextureFlipFaceVertical,
         QuaderFaceTextureAction::FlipFaceVertical},
        {quader_command::kModelingTextureFitBoth,
         QuaderFaceTextureAction::FitBoth},
        {quader_command::kModelingTextureFitX,
         QuaderFaceTextureAction::FitX},
        {quader_command::kModelingTextureFitY,
         QuaderFaceTextureAction::FitY},
        {quader_command::kModelingTextureJustifyLeft,
         QuaderFaceTextureAction::JustifyLeft},
        {quader_command::kModelingTextureJustifyRight,
         QuaderFaceTextureAction::JustifyRight},
        {quader_command::kModelingTextureJustifyTop,
         QuaderFaceTextureAction::JustifyTop},
        {quader_command::kModelingTextureJustifyBottom,
         QuaderFaceTextureAction::JustifyBottom},
        {quader_command::kModelingTextureJustifyCenter,
         QuaderFaceTextureAction::JustifyCenter},
    }};

std::string command_id_for_modeling_action(std::string_view action_id) {
  const auto native_action = std::find_if(
      kNativeModelingActionCommands.begin(),
      kNativeModelingActionCommands.end(), [&](const auto &candidate) {
        return candidate.action_id == action_id;
      });
  if (native_action != kNativeModelingActionCommands.end()) {
    return std::string(native_action->command_id.string());
  }
  if (const CommandTool *tool = find_modeling_command_tool(action_id)) {
    return tool->public_command_target_id();
  }

  return quader_modeling_command_target_id(action_id);
}

bool modeling_action_registers_dedicated_command(const CommandTool &tool) {
  return tool.registers_public_command();
}

bool duplicate_selection_available(const QuaderApi &api) {
  QDR_PROFILE_FUNCTION();
  return api.modeling().can_duplicate_selected_entity_hierarchy();
}

std::string command_id_for_tool_activation(std::string_view tool_id) {
  return quader_tool_activation_command_target_id(tool_id);
}

std::string_view selection_mode_id(QuaderSelectionMode mode) {
  switch (mode) {
  case QuaderSelectionMode::Vertex:
    return "vertex";
  case QuaderSelectionMode::Edge:
    return "edge";
  case QuaderSelectionMode::Face:
    return "face";
  case QuaderSelectionMode::Object:
    return "object";
  case QuaderSelectionMode::Group:
    return "group";
  }
  return "object";
}

std::string
keybinding_target_id_for_selection_mode(QuaderSelectionMode mode) {
  return quader_viewport_selection_mode_target_id(selection_mode_id(mode));
}

std::string keybinding_target_id_for_grid_preset(int preset) {
  return viewport_grid_preset_target_id(preset);
}

std::filesystem::path default_keybindings_path() {
  return quader_user_config_directory({}) / "keybindings.json";
}

constexpr std::string_view global_command_keybinding_context() {
  return "!textInputActive";
}

constexpr std::string_view save_command_keybinding_context() {
  // Saving is an application-level lifecycle action, not a text-editing
  // action. Keep it available while a retained editor has keyboard focus.
  return "true";
}

constexpr std::string_view undo_command_keybinding_context() {
  return global_command_keybinding_context();
}

std::optional<std::string>
write_keybindings_atomically(const std::filesystem::path &path,
                             std::string_view bytes) {
  const quader_io::QdrAtomicWriteResult written =
      quader_io::write_qdr_text_atomic(path, bytes);
  if (!written.ok) {
    return written.message.empty() ? "Unable to write keybindings file."
                                   : written.message;
  }
  return std::nullopt;
}

constexpr std::string_view redo_command_keybinding_context() {
  return global_command_keybinding_context();
}

constexpr std::string_view viewport_command_keybinding_context() {
  return "viewportFocus && !textInputActive && !navigation.active";
}

constexpr std::string_view viewport_maximize_keybinding_context() {
  return "viewportFocus && !textInputActive && !navigation.active && "
         "!operationPreview.active";
}

constexpr std::string_view editor_command_keybinding_context() {
  return "(viewportFocus || outlinerFocus || editorFocus) && !textInputActive && "
         "!navigation.active";
}

bool editor_panel_can_apply_shortcut(std::string_view target_id) {
  ViewportShortcut shortcut;
  return viewport_shortcut_for_command_target_id(target_id, shortcut) &&
         (shortcut.kind == ViewportShortcutKind::SetSelectionMode ||
          shortcut.kind == ViewportShortcutKind::SetGridPreset);
}

constexpr std::string_view primitive_flip_keybinding_context() {
  return "(viewportFocus || outlinerFocus || editorFocus) && !textInputActive && "
         "!navigation.active && activeTool.create_primitive && "
         "primitiveCreation.active";
}

constexpr std::string_view primitive_caps_keybinding_context() {
  return "(viewportFocus || outlinerFocus || editorFocus) && !textInputActive && "
         "!navigation.active && activeTool.create_primitive && "
         "(primitiveCreation.cylinder || primitiveCreation.cone)";
}

constexpr std::string_view primitive_hemisphere_keybinding_context() {
  return "(viewportFocus || outlinerFocus || editorFocus) && !textInputActive && "
         "!navigation.active && activeTool.create_primitive && "
         "primitiveCreation.sphere";
}

bool is_primitive_caps_conflicting_tool(std::string_view command_id) {
  // Knife owns default C; Cut may have an existing user override on C.
  return command_id == quader_command::kToolsActivateKnife.string() ||
         command_id == quader_command::kToolsActivateCut.string();
}

bool is_modeling_face_flip_command(std::string_view command_id) {
  // All resolver aliases share the same Settings action. Their combined
  // context must remain disjoint from the primitive tool's flip action.
  return command_id == quader_command::kModelingInvertMesh.string() ||
         command_id == quader_command::kModelingInvertFaces.string() ||
         command_id == quader_command::kModelingFlipFaceNormals.string();
}

constexpr std::string_view unobstructed_editor_command_keybinding_context() {
  return "(viewportFocus || outlinerFocus || editorFocus) && !textInputActive && "
         "!navigation.active && !modalTool.active && !activeTool.ownsViewport && "
         "!transformDrag.active && !operationPreview.active";
}

bool is_material_viewport_action(
    const QuaderKeybindingActionKey &action) {
  return action.action_id == "viewport.material.apply_face_to_mesh" ||
         action.action_id == "viewport.material.apply_selected_face_to_face" ||
         action.action_id == "viewport.material.apply_selected_face_to_mesh" ||
         action.action_id == "viewport.material.apply_face_to_selection" ||
         action.action_id == "viewport.material.transfer_face_texture";
}

bool is_reserved_bare_right_mouse_material_chord(
    const QuaderInputChord &chord) {
  if (chord.device != QuaderInputDevice::Mouse ||
      chord.mouse_button != QuaderMouseButton::Right || chord.primary ||
      chord.shift || chord.alt) {
    return false;
  }
  return chord.gesture == QuaderInputGesture::Press ||
         chord.gesture == QuaderInputGesture::Click ||
         chord.gesture == QuaderInputGesture::DoubleClick ||
         chord.gesture == QuaderInputGesture::Drag;
}

bool material_assignment_uses_reserved_bare_right_mouse(
    const QuaderKeybindingActionKey &action,
    const std::vector<QuaderInputChord> &sequence) {
  return is_material_viewport_action(action) &&
         std::ranges::any_of(
             sequence, is_reserved_bare_right_mouse_material_chord);
}

bool source_preserving_material_assignment_uses_unsupported_pointer_gesture(
    const QuaderKeybindingActionKey &action,
    const std::vector<QuaderInputChord> &sequence) {
  if (!is_material_viewport_action(action) ||
      action.action_id == "viewport.material.apply_face_to_mesh") {
    return false;
  }
  return std::ranges::any_of(sequence, [](const QuaderInputChord &chord) {
    return chord.device == QuaderInputDevice::Mouse &&
           (chord.gesture == QuaderInputGesture::Drag ||
            chord.gesture == QuaderInputGesture::Wheel ||
            chord.gesture == QuaderInputGesture::WheelUp ||
            chord.gesture == QuaderInputGesture::WheelDown ||
            chord.gesture == QuaderInputGesture::WheelLeft ||
            chord.gesture == QuaderInputGesture::WheelRight);
  });
}

bool transform_vertex_snap_assignment_uses_non_holdable_shortcut(
    const QuaderKeybindingActionKey &action,
    const std::vector<QuaderInputChord> &sequence) {
  if (action.action_id != "viewport.transform.vertex_snap") {
    return false;
  }
  return sequence.size() != 1u ||
         sequence.front().device != QuaderInputDevice::Keyboard ||
         sequence.front().key == QuaderKeyCode::None ||
         sequence.front().gesture != QuaderInputGesture::Press;
}

bool command_semantically_requires_viewport(std::string_view command_id) {
  if (command_id == quader_command::kViewportSpawnWindow.string()) {
    return false;
  }
  return command_id.starts_with("modeling.") ||
         command_id.starts_with("select.") ||
         command_id.starts_with("viewport.") ||
         command_id == quader_command::kToolsActivateCreateBox.string() ||
         command_id == quader_command::kToolsActivateCreateCylinder.string() ||
         command_id == quader_command::kToolsActivateCreateSphere.string() ||
         command_id == quader_command::kToolsActivateCreateTorus.string() ||
         command_id == quader_command::kToolsActivateCreateCone.string() ||
         command_id == quader_command::kToolsActivateCreateCurve.string() ||
         command_id == quader_command::kToolsActivateExtrudeIndividual.string() ||
         command_id == quader_command::kToolsActivateExtrudeAlongNormals.string() ||
         command_id == quader_command::kToolsFlipPrimitiveFaces.string() ||
         command_id == quader_command::kToolsTogglePrimitiveCaps.string() ||
         command_id == quader_command::kToolsTogglePrimitiveHemisphere.string() ||
         command_id == quader_command::kEditClearSelection.string() ||
         command_id == quader_command::kDeleteSelection.string() ||
         command_id == quader_command::kCutSelection.string() ||
         command_id == quader_command::kCopySelection.string() ||
         command_id == quader_command::kPasteSelection.string() ||
         command_id == quader_command::kDuplicateSelection.string() ||
         command_id == quader_command::kGroupSelection.string() ||
         command_id == quader_command::kUngroupSelection.string() ||
         command_id == quader_command::kParentToActive.string() ||
         command_id == quader_command::kUnparentSelection.string() ||
         command_id == quader_command::kSceneHideSelection.string() ||
         command_id ==
             quader_command::kSceneUnhideHiddenObjects.string();
}

std::string_view
default_keybinding_context_for_command(std::string_view command_id) {
  if (command_id == quader_command::kSceneDocumentSave.string() ||
      command_id == quader_command::kSceneDocumentSaveAs.string() ||
      command_id == quader_command::kSceneDocumentSaveAll.string()) {
    return save_command_keybinding_context();
  }
  if (command_id == quader_command::kEditClearSelection.string()) {
    return "(viewportFocus || outlinerFocus || editorFocus) && !textInputActive && "
           "!navigation.active && !modalTool.active && !activeTool.ownsViewport && "
           "!transformDrag.active && !operationPreview.active && "
           "!activeTool.nativeLifecycle && selection.allowedByTool";
  }
  if (command_id == quader_command::kViewportFrameSelection.string()) {
    return unobstructed_editor_command_keybinding_context();
  }
  if (command_id == quader_command::kParentToActive.string() ||
      command_id == quader_command::kUnparentSelection.string()) {
    return unobstructed_editor_command_keybinding_context();
  }
  if (command_id == quader_command::kToolsFlipPrimitiveFaces.string()) {
    return primitive_flip_keybinding_context();
  }
  if (command_id == quader_command::kToolsTogglePrimitiveCaps.string()) {
    return primitive_caps_keybinding_context();
  }
  if (command_id == quader_command::kToolsTogglePrimitiveHemisphere.string()) {
    return primitive_hemisphere_keybinding_context();
  }
  if (command_id == quader_command::kSceneHideSelection.string()) {
    return "(viewportFocus || outlinerFocus || editorFocus) && !textInputActive && "
           "!navigation.active && "
           "!primitiveCreation.sphere";
  }
  if (is_primitive_caps_conflicting_tool(command_id)) {
    return "(viewportFocus || outlinerFocus || editorFocus) && !textInputActive && "
           "!navigation.active && !primitiveCreation.cylinder && "
           "!primitiveCreation.cone";
  }
  if (is_modeling_face_flip_command(command_id)) {
    return "(viewportFocus || outlinerFocus || editorFocus) && !textInputActive && "
           "!navigation.active && "
           "!primitiveCreation.active";
  }
  if (command_id == quader_command::kEditUndo.string()) {
    return undo_command_keybinding_context();
  }
  if (command_id == quader_command::kEditRedo.string()) {
    return redo_command_keybinding_context();
  }
  if (command_id ==
          quader_command::kViewportToggleLocalVisibility.string() ||
      command_id ==
          quader_command::kViewportToggleTransformSpace.string()) {
    return unobstructed_editor_command_keybinding_context();
  }
  if (command_semantically_requires_viewport(command_id)) {
    return editor_command_keybinding_context();
  }
  ViewportShortcut shortcut;
  if (viewport_shortcut_for_command_target_id(command_id, shortcut) &&
      shortcut.context == ViewportShortcutContext::Viewport) {
    return editor_command_keybinding_context();
  }
  return global_command_keybinding_context();
}

int default_keybinding_weight_for_command(std::string_view command_id) {
#ifndef NDEBUG
  if (command_id == quader_command::kDevCopySelectionJson.string()) {
    return 2000;
  }
#else
  (void)command_id;
#endif
  return 0;
}

std::string_view arrangement_axis_label(QuaderTransformAxis axis) {
  switch (axis) {
  case QuaderTransformAxis::X:
    return "X";
  case QuaderTransformAxis::Y:
    return "Y";
  case QuaderTransformAxis::Z:
    return "Z";
  case QuaderTransformAxis::None:
  case QuaderTransformAxis::XY:
  case QuaderTransformAxis::XZ:
  case QuaderTransformAxis::YZ:
  case QuaderTransformAxis::All:
    return "";
  }
  return "";
}

std::string arrangement_command_label(
    const QuaderMeshArrangementDescriptor &descriptor) {
  std::string label;
  if (descriptor.kind == QuaderMeshArrangementKind::Alignment) {
    label = "Align ";
    label.append(arrangement_axis_label(descriptor.axis));
    label.push_back(' ');
    switch (descriptor.alignment) {
    case QuaderMeshAlignment::Minimum:
      label.append("Minimum");
      break;
    case QuaderMeshAlignment::Center:
      label.append("Center");
      break;
    case QuaderMeshAlignment::Maximum:
      label.append("Maximum");
      break;
    }
    return label;
  }
  label = descriptor.distribution == QuaderMeshDistribution::Centers
              ? "Distribute Centers Along "
              : "Distribute Gaps Along ";
  label.append(arrangement_axis_label(descriptor.axis));
  return label;
}

std::string_view
default_keybinding_context_for_contract_target(std::string_view target_id) {
  const std::optional<std::string> command_target =
      viewport_command_target_id_for_contract_target(target_id);
  if (command_target) {
    return default_keybinding_context_for_command(*command_target);
  }
  ViewportShortcut shortcut;
  if (viewport_shortcut_from_contract_target(target_id, shortcut) &&
      shortcut.context == ViewportShortcutContext::Global) {
    return global_command_keybinding_context();
  }
  return viewport_command_keybinding_context();
}

QuaderKeybindingContext modal_lifecycle_keybinding_context() {
  QuaderKeybindingContext context;
  context.set("viewportFocus", true);
  context.set("modalTool.active", true);
  return context;
}

QuaderKeybindingContext
viewport_keybinding_context(const ViewportToolState &state,
                            bool viewport_focused, bool text_input_active,
                            QuaderViewportKeybindingContext input_context,
                            bool can_undo, bool can_redo,
                            const QuaderToolsApi &tools) {
  QuaderKeybindingContext context;
  context.set("viewportFocus", viewport_focused);
  context.set("outlinerFocus", input_context.outliner_focused);
  context.set("editorFocus", input_context.editor_focused);
  context.set("viewport.hovered", input_context.viewport_hovered);
  context.set("viewport.orthographic", input_context.orthographic_view);
  context.set("textInputActive", text_input_active);
  context.set("editorTextFocus", text_input_active);
  context.set("canUndo", can_undo);
  context.set("canRedo", can_redo);
  context.set("navigation.active", input_context.navigation_active);
  context.set("navigation.flyActive", input_context.fly_navigation_active);
  context.set("navigation.orbitActive", input_context.orbit_navigation_active);
  context.set("navigation.panActive", input_context.pan_navigation_active);
  context.set("modalTool.active", input_context.modal_tool_active);
  context.set("activeTool.ownsViewport",
              input_context.active_tool_owns_viewport);
  const ToolDescriptor *descriptor =
      find_viewport_tool_descriptor(state.active_tool);
  context.set("activeTool.nativeLifecycle",
              descriptor != nullptr && descriptor->editor_owned_native_lifecycle);
  context.set("selection.allowedByTool",
              descriptor != nullptr && descriptor->allows_selection_fallback);
  context.set("transformDrag.active", input_context.active_transform_drag);
  context.set("operationPreview.active",
              input_context.operation_preview_active);
  context.set("curveControls.editable",
              input_context.curve_controls_editable);
  context.set("activeTool." + std::string(viewport_tool_id(state.active_tool)),
              true);
  const std::string primitive_kind =
      state.active_tool == ViewportToolId::CreatePrimitive
          ? tools.active_primitive_type() : std::string{};
  context.set("primitiveCreation.box", primitive_kind == "box");
  context.set("primitiveCreation.cylinder", primitive_kind == "cylinder");
  context.set("primitiveCreation.sphere", primitive_kind == "sphere");
  context.set("primitiveCreation.cone", primitive_kind == "cone");
  context.set("primitiveCreation.active", !primitive_kind.empty());
  context.set("selectionMode." +
                  std::string(viewport_selection_mode_id(state.selection_mode)),
              true);
  return context;
}

constexpr std::uint32_t kSelectionModeVertex = 1u << 0u;
constexpr std::uint32_t kSelectionModeEdge = 1u << 1u;
constexpr std::uint32_t kSelectionModeFace = 1u << 2u;
constexpr std::uint32_t kSelectionModeObject = 1u << 3u;
constexpr std::uint32_t kSelectionModeGroup = 1u << 4u;
constexpr std::uint32_t kSelectionModeComponent =
    kSelectionModeVertex | kSelectionModeEdge | kSelectionModeFace;
constexpr std::uint32_t kSelectionModeAny =
    kSelectionModeComponent | kSelectionModeObject | kSelectionModeGroup;

std::uint32_t selection_mode_mask(ModelingSelectionMode mode) {
  switch (mode) {
  case ModelingSelectionMode::Vertex:
    return kSelectionModeVertex;
  case ModelingSelectionMode::Edge:
    return kSelectionModeEdge;
  case ModelingSelectionMode::Face:
    return kSelectionModeFace;
  case ModelingSelectionMode::Object:
    return kSelectionModeObject;
  case ModelingSelectionMode::Group:
    return kSelectionModeGroup;
  }
  return 0u;
}

std::uint32_t selection_mode_mask(
    const ModelingActionAvailabilityRule &availability) {
  switch (availability.requirement) {
  case ModelingActionSelectionRequirement::AnySelection:
    return kSelectionModeAny;
  case ModelingActionSelectionRequirement::SelectionMode:
    return selection_mode_mask(availability.mode);
  case ModelingActionSelectionRequirement::ComponentSelection:
    return kSelectionModeComponent;
  case ModelingActionSelectionRequirement::ObjectOrGroupSelection:
    return kSelectionModeObject | kSelectionModeGroup;
  case ModelingActionSelectionRequirement::ObjectOrFaceSelection:
    return kSelectionModeObject | kSelectionModeFace;
  case ModelingActionSelectionRequirement::EdgeOrFaceSelection:
    return kSelectionModeEdge | kSelectionModeFace;
  }
  return 0u;
}

std::string stable_keybinding_localization_key(std::string_view target_id) {
  std::string key("keybindings.target.");
  key.append(target_id);
  key.append(".label");
  return key;
}

std::string explicit_keybinding_label(std::string_view command_id,
                                      std::string_view command_label) {
  const auto is = [&](QuaderCommandId id) { return command_id == id.string(); };
  if (is(quader_command::kCopySelection)) {
    return "Copy Selection";
  }
  if (is(quader_command::kCutSelection)) {
    return "Cut Selection";
  }
  if (is(quader_command::kPasteSelection)) {
    return "Paste Selection";
  }
  if (is(quader_command::kDuplicateSelection)) {
    return "Duplicate Selection";
  }
  if (is(quader_command::kDeleteSelection)) {
    return "Delete Selection";
  }
  if (is(quader_command::kGroupSelection)) {
    return "Group Selection";
  }
  if (is(quader_command::kUngroupSelection)) {
    return "Ungroup Selection";
  }
  if (is(quader_command::kParentToActive)) {
    return "Parent to Active";
  }
  if (is(quader_command::kUnparentSelection)) {
    return "Unparent";
  }
  if (is(quader_command::kModelingCopyPivotFromActive)) {
    return "Copy Pivot from Active Object";
  }
  if (is(quader_command::kProjectExportSelectionObj)) {
    return "Export Selected Objects as OBJ";
  }
  if (is(quader_command::kSceneCreatePointLight)) {
    return "Create Point Light";
  }
  if (is(quader_command::kSceneCreateDirectionalLight)) {
    return "Create Directional Light";
  }
  if (is(quader_command::kSceneCreateEmpty)) {
    return "Create Empty";
  }
  if (is(quader_command::kSceneHideSelection)) {
    return "Hide Selected Objects";
  }
  if (is(quader_command::kSceneUnhideHiddenObjects)) {
    return "Unhide All Hidden Objects";
  }
  if (is(quader_command::kViewportFrameSelection)) {
    return "Frame Selected Objects in View";
  }
  if (is(quader_command::kViewportFocusAll)) {
    return "Frame All Objects in View";
  }
  if (is(quader_command::kViewportFocusOrigin)) {
    return "Center View on World Origin";
  }
  if (command_id.starts_with("viewport.view.")) {
    return "Switch to " + std::string(command_label);
  }
  if (is(quader_command::kViewportToggleGrid)) {
    return "Toggle Viewport Grid";
  }
  if (is(quader_command::kViewportToggleGizmos)) {
    return "Toggle Transform Gizmos";
  }
  if (is(quader_command::kViewportToggleMeshGrid)) {
    return "Toggle Mesh Grid Overlay";
  }
  if (is(quader_command::kViewportToggleOverlays)) {
    return "Toggle Viewport Overlays";
  }
  if (is(quader_command::kViewportToggleDimensions)) {
    return "Toggle Selection Dimensions";
  }
  if (is(quader_command::kViewportToggleCollisions)) {
    return "Toggle Collision Display";
  }
  if (is(quader_command::kViewportToggleInfo)) {
    return "Toggle Viewport Info";
  }
  if (is(quader_command::kViewportToggleTextureLock)) {
    return "Toggle Texture Lock";
  }
  if (is(quader_command::kViewportToggleTextureLockScale)) {
    return "Toggle Texture Lock Scale";
  }
  if (is(quader_command::kViewportShadingWireframe)) {
    return "Set Viewport Shading to Wireframe";
  }
  if (is(quader_command::kViewportShadingSolid)) {
    return "Set Viewport Shading to Solid";
  }
  if (is(quader_command::kViewportShadingShaded)) {
    return "Set Viewport Shading to Shaded";
  }
  if (is(quader_command::kViewportShadingRendered)) {
    return "Set Viewport Shading to Rendered";
  }
  if (is(quader_command::kViewportLayoutSingle)) {
    return "Switch to Single Pane View";
  }
  if (is(quader_command::kViewportLayoutTwoVertical)) {
    return "Switch to Two-View Vertical Layout";
  }
  if (is(quader_command::kViewportLayoutTwoHorizontal)) {
    return "Switch to Two-View Horizontal Layout";
  }
  if (is(quader_command::kViewportLayoutThreePrimaryRight)) {
    return "Switch to Three-View Layout with the Primary View on the Right";
  }
  if (is(quader_command::kViewportLayoutThreePrimaryLeft)) {
    return "Switch to Three-View Layout with the Primary View on the Left";
  }
  if (is(quader_command::kViewportLayoutThreePrimaryBottom)) {
    return "Switch to Three-View Layout with the Primary View on the Bottom";
  }
  if (is(quader_command::kViewportLayoutThreePrimaryTop)) {
    return "Switch to Three-View Layout with the Primary View on the Top";
  }
  if (is(quader_command::kViewportLayoutFourQuad)) {
    return "Switch to Four-View Quad Layout";
  }
  if (is(quader_command::kToolsActivateCreateBox)) {
    return "Create Box";
  }
  if (is(quader_command::kToolsActivateCreateCylinder)) {
    return "Create Cylinder";
  }
  if (is(quader_command::kToolsActivateCreateSphere)) {
    return "Create Sphere";
  }
  if (is(quader_command::kToolsActivateCreateTorus)) {
    return "Create Torus";
  }
  if (is(quader_command::kToolsActivateCreateCone)) {
    return "Create Cone";
  }
  if (is(quader_command::kToolsActivateCreateCurve)) {
    return "Activate New Curve Creation Tool";
  }
  if (is(quader_command::kToolsActivateExtrudeIndividual)) {
    return "Activate Extrude Individually";
  }
  if (is(quader_command::kToolsActivateExtrudeAlongNormals)) {
    return "Activate Extrude Along Normals";
  }
  if (const std::optional<std::string_view> tool_id =
          quader_tool_id_from_activation_command_target(command_id)) {
    if (*tool_id == "create_primitive") {
      return "Activate Primitive Creation Tool";
    }
    if (*tool_id == "insert_edge_loop") {
      return "Activate Edge Loop Insertion Tool";
    }
    return "Activate " + std::string(command_label) + " Tool";
  }
  return std::string(command_label);
}

std::string command_keybinding_localization_key(std::string_view command_id) {
  const auto stable = [&] {
    return stable_keybinding_localization_key(command_id);
  };

  if (command_id.starts_with("modeling.texture.")) {
    return "command." + std::string(command_id);
  }
  if (command_id == quader_command::kModelingUvPeel.string()) {
    return "command.modeling.uv_peel";
  }
  if (command_id == quader_command::kEditUndo.string()) {
    return "ui.menu.edit.undo";
  }
  if (command_id == quader_command::kEditRedo.string()) {
    return "ui.menu.edit.redo";
  }
  if (command_id == quader_command::kEditRepeatLastAction.string()) {
    return "ui.menu.edit.repeat_last_action";
  }
  if (command_id == quader_command::kCopySelection.string()) {
    return stable();
  }
  if (command_id == quader_command::kCutSelection.string()) {
    return stable();
  }
  if (command_id == quader_command::kPasteSelection.string()) {
    return stable();
  }
  if (command_id == quader_command::kDuplicateSelection.string()) {
    return stable();
  }
  if (command_id == quader_command::kEditCreateInstance.string()) {
    return "ui.menu.edit.create_instance";
  }
  if (command_id == quader_command::kEditFlattenInstance.string()) {
    return "ui.menu.edit.flatten_instance";
  }
  if (command_id == quader_command::kEditClearSelection.string()) {
    return "ui.menu.edit.clear_selection";
  }
  if (command_id.starts_with("select.")) {
    std::string key("ui.menu.");
    key.append(command_id);
    return key;
  }
  if (command_id == quader_command::kDeleteSelection.string()) {
    return stable();
  }
  if (command_id == quader_command::kGroupSelection.string()) {
    return stable();
  }
  if (command_id == quader_command::kUngroupSelection.string()) {
    return stable();
  }
  if (command_id == quader_command::kParentToActive.string() ||
      command_id == quader_command::kUnparentSelection.string()) {
    return stable();
  }

  if (command_id == quader_command::kProjectNew.string()) {
    return "ui.menu.file.new_project";
  }
  if (command_id == quader_command::kProjectOpen.string()) {
    return "ui.menu.file.open_project";
  }
  if (command_id == quader_command::kProjectClose.string()) {
    return "ui.menu.file.close_project";
  }
  if (command_id == quader_command::kProjectExportSelectionObj.string()) {
    return stable();
  }

  if (command_id.starts_with("scene_document.")) {
    const std::string_view suffix = command_id.substr(15u);
    if (suffix == "new") {
      return "ui.menu.file.new_scene";
    }
    if (suffix == "open") {
      return "ui.menu.file.open_scene";
    }
    if (suffix == "save") {
      return "ui.menu.file.save_scene";
    }
    if (suffix == "save_as") {
      return "ui.menu.file.save_scene_as";
    }
    if (suffix == "save_all") {
      return "ui.menu.file.save_all_scenes";
    }
    if (suffix == "close") {
      return "ui.menu.file.close_scene";
    }
    if (suffix == "next") {
      return "ui.menu.file.next_scene";
    }
    if (suffix == "previous") {
      return "ui.menu.file.previous_scene";
    }
  }

  if (command_id == quader_command::kSceneCreatePointLight.string()) {
    return stable();
  }
  if (command_id == quader_command::kSceneCreateDirectionalLight.string()) {
    return stable();
  }
  if (command_id == quader_command::kSceneCreateEmpty.string()) {
    return stable();
  }
  if (command_id == quader_command::kSceneHideSelection.string()) {
    return stable();
  }
  if (command_id ==
      quader_command::kSceneUnhideHiddenObjects.string()) {
    return stable();
  }
  if (command_id == quader_command::kViewportFrameSelection.string()) {
    return stable();
  }
  if (command_id == quader_command::kViewportFocusAll.string()) {
    return stable();
  }
  if (command_id.starts_with("viewport.view.")) {
    return stable();
  }
  if (command_id == quader_command::kViewportToggleGrid.string()) {
    return stable();
  }
  if (command_id == quader_command::kViewportToggleGizmos.string()) {
    return stable();
  }
  if (command_id == quader_command::kViewportToggleMeshGrid.string()) {
    return stable();
  }
  if (command_id == quader_command::kViewportToggleOverlays.string()) {
    return stable();
  }
  if (command_id == quader_command::kViewportToggleDimensions.string()) {
    return stable();
  }
  if (command_id == quader_command::kViewportToggleCollisions.string()) {
    return stable();
  }
  if (command_id == quader_command::kViewportToggleInfo.string()) {
    return stable();
  }
  if (command_id == quader_command::kViewportToggleTextureLock.string()) {
    return stable();
  }
  if (command_id == quader_command::kViewportToggleTextureLockScale.string()) {
    return stable();
  }
  if (command_id == quader_command::kViewportShadingWireframe.string()) {
    return stable();
  }
  if (command_id == quader_command::kViewportShadingSolid.string()) {
    return stable();
  }
  if (command_id == quader_command::kViewportShadingShaded.string()) {
    return stable();
  }
  if (command_id == quader_command::kViewportShadingRendered.string()) {
    return stable();
  }
  if (command_id == quader_command::kViewportLayoutSingle.string()) {
    return stable();
  }
  if (command_id == quader_command::kViewportLayoutTwoVertical.string()) {
    return stable();
  }
  if (command_id == quader_command::kViewportLayoutTwoHorizontal.string()) {
    return stable();
  }
  if (command_id ==
      quader_command::kViewportLayoutThreePrimaryRight.string()) {
    return stable();
  }
  if (command_id ==
      quader_command::kViewportLayoutThreePrimaryLeft.string()) {
    return stable();
  }
  if (command_id ==
      quader_command::kViewportLayoutThreePrimaryBottom.string()) {
    return stable();
  }
  if (command_id == quader_command::kViewportLayoutThreePrimaryTop.string()) {
    return stable();
  }
  if (command_id == quader_command::kViewportLayoutFourQuad.string()) {
    return stable();
  }

  for (const QuaderMeshArrangementDescriptor &descriptor :
       quader_mesh_arrangement_catalog()) {
    if (descriptor.command_id.string() == command_id) {
      return std::string(descriptor.keybinding_localization_key);
    }
  }

  if (command_id == quader_command::kModelingRemoveDoubles.string()) {
    return "ui.menu.cleanup.remove_doubles";
  }
  if (const std::optional<std::string_view> action_id =
          quader_command_target_suffix(command_id,
                                       kQuaderModelingCommandTargetPrefix)) {
    const std::string localization_key =
        modeling_command_tool_localization_key(*action_id);
    if (!localization_key.empty()) {
      return localization_key;
    }
  }
  if (quader_tool_id_from_activation_command_target(command_id)) {
    return stable();
  }
  return stable();
}

QuaderKeybindingContextKind
default_keybinding_context_kind_for_command(std::string_view command_id) {
  if (command_semantically_requires_viewport(command_id)) {
    return QuaderKeybindingContextKind::Viewport;
  }
  ViewportShortcut shortcut;
  if (viewport_shortcut_for_command_target_id(command_id, shortcut) &&
      shortcut.context == ViewportShortcutContext::Viewport) {
    return QuaderKeybindingContextKind::Viewport;
  }
  return QuaderKeybindingContextKind::Global;
}

QuaderKeybindingContextKind keybinding_context_kind_for_viewport_input(
    ViewportInputBindingContext context) {
  switch (context) {
  case ViewportInputBindingContext::Viewport:
    return QuaderKeybindingContextKind::Viewport;
  case ViewportInputBindingContext::Navigation:
    return QuaderKeybindingContextKind::ViewportNavigation;
  case ViewportInputBindingContext::FlyNavigation:
    return QuaderKeybindingContextKind::FlyNavigation;
  case ViewportInputBindingContext::ModalTool:
    return QuaderKeybindingContextKind::ModalTool;
  case ViewportInputBindingContext::LinearArrayTool:
    return QuaderKeybindingContextKind::LinearArrayTool;
  case ViewportInputBindingContext::RadialArrayTool:
    return QuaderKeybindingContextKind::RadialArrayTool;
  case ViewportInputBindingContext::CurveTool:
    return QuaderKeybindingContextKind::CurveTool;
  case ViewportInputBindingContext::ExtrudeTool:
    return QuaderKeybindingContextKind::ExtrudeTool;
  }
  return QuaderKeybindingContextKind::Custom;
}

} // namespace

/**
 * Owns command registration, dispatch, and keybinding adaptation for QuaderCommandsApi.
 */
class QuaderCommandsApi::Impl final {
public:
#ifndef NDEBUG
    Impl(QuaderApi& api, QuaderProjectApi& project,
         QuaderSceneDocumentsApi &scene_documents,
         QuaderViewportApi& viewports, QuaderToolsApi& tools,
         QuaderPreferencesApi& preferences, QuaderUIApi &ui,
         QuaderDevToolsApi& dev_tools);
#else
    Impl(QuaderApi& api, QuaderProjectApi& project,
         QuaderSceneDocumentsApi &scene_documents,
         QuaderViewportApi& viewports, QuaderToolsApi& tools,
         QuaderPreferencesApi& preferences, QuaderUIApi &ui);
#endif

    QuaderApiVoidResult execute(std::string_view command_id);
    QuaderApiVoidResult execute(QuaderCommandId command_id);
    [[nodiscard]] std::optional<QuaderApiStatus> last_status() const;
    [[nodiscard]] std::uint64_t last_status_revision() const;
    [[nodiscard]] std::optional<QuaderCommandDescriptor>
    descriptor(std::string_view command_id) const;
    [[nodiscard]] std::vector<QuaderCommandDescriptor> available() const;
    [[nodiscard]] std::string
    command_id_for_modeling_action(std::string_view action_id) const;
    [[nodiscard]] std::string
    command_id_for_tool_activation(std::string_view tool_id) const;
    [[nodiscard]] std::string
    keybinding_target_id_for_selection_mode(QuaderSelectionMode mode) const;
    [[nodiscard]] std::string
    keybinding_target_id_for_grid_preset(int preset) const;
    [[nodiscard]] std::string shortcut_for(std::string_view command_id) const;
    [[nodiscard]] std::string shortcut_for(QuaderCommandId command_id) const;
    [[nodiscard]] std::uint64_t keybindings_revision() const noexcept;
    [[nodiscard]] std::vector<QuaderKeybindingRule> keybindings() const;
    [[nodiscard]] std::vector<QuaderKeybindingCatalogEntry>
    keybinding_catalog() const;
    [[nodiscard]] std::string
    format_input_mapping(const QuaderInputChord &chord) const;
    [[nodiscard]] std::vector<QuaderKeybindingConflict>
    conflicts_for_shortcut(std::string_view shortcut,
                           std::string_view when = {}) const;
    [[nodiscard]] QuaderKeybindingAssignmentAnalysis
    analyze_shortcut_assignment(const QuaderKeybindingActionKey &action,
                                std::string_view shortcut) const;
    [[nodiscard]] bool resolve_viewport_shortcut(
        ViewportKey key, ViewportModifiers modifiers,
        const ViewportToolState &state, bool viewport_focused,
        bool text_input_active, ViewportShortcut &shortcut,
        QuaderViewportKeybindingContext input_context = {});
    [[nodiscard]] bool resolve_viewport_shortcut(
        const QuaderInputChord &chord, const ViewportToolState &state,
        bool viewport_focused, bool text_input_active,
        ViewportShortcut &shortcut,
        QuaderViewportKeybindingContext input_context = {});
    [[nodiscard]] bool resolve_viewport_input_action(
        const QuaderInputChord &chord, const ViewportToolState &state,
        bool viewport_focused, bool text_input_active,
        ViewportInputActionKind &action,
        QuaderViewportKeybindingContext input_context = {});
    [[nodiscard]] QuaderViewportKeybindingDispatch dispatch_viewport_keybinding(
        ViewportKey key, ViewportModifiers modifiers,
        const ViewportToolState &state, bool viewport_focused,
        bool text_input_active,
        QuaderViewportKeybindingContext input_context = {});
    [[nodiscard]] QuaderViewportKeybindingDispatch
    dispatch_viewport_input_binding(
        const QuaderInputChord &chord, const ViewportToolState &state,
        bool viewport_focused, bool text_input_active,
        QuaderViewportKeybindingContext input_context = {});
    [[nodiscard]] std::optional<std::string>
    resolve_immediate_global_command(
        const QuaderInputChord &chord, const ViewportToolState &state,
        bool text_input_active,
        QuaderViewportKeybindingContext input_context = {}) const;
    [[nodiscard]] QuaderNativeToolSettingsDispatch
    dispatch_native_tool_settings_input_binding(
        const QuaderInputChord &chord, const ViewportToolState &state);
    [[nodiscard]] QuaderNativeToolSettingsDispatch
    resolve_native_tool_settings_input_binding(
        const QuaderInputChord &chord, const ViewportToolState &state) const;
    [[nodiscard]] std::optional<QuaderKeybindingContext>
    tool_settings_keybinding_context(const ViewportToolState &state) const;
    [[nodiscard]] QuaderNativeToolSettingsIntent
    tool_settings_intent(std::string_view target_id) const;
    [[nodiscard]] QuaderNativeToolSettingsDispatch
    resolve_modal_lifecycle_input_binding(const QuaderInputChord &chord) const;
    [[nodiscard]] QuaderNativeToolSettingsDispatch
    dispatch_modal_lifecycle_input_binding(const QuaderInputChord &chord);
    void reset_pending_modal_lifecycle_input_binding();
    QuaderApiVoidResult execute_native_tool_settings_intent(
        QuaderNativeToolSettingsIntent intent);
    QuaderApiVoidResult execute_modeling_preview_intent(
        QuaderNativeToolSettingsIntent intent);
    void reset_pending_keybinding_chord();
    void reset_pending_non_modal_keybinding_chord();
    void reset_pending_viewport_keybinding_chord();
    [[nodiscard]] bool has_pending_keybinding_chord() const;
    QuaderApiVoidResult set_shortcut(std::string_view command_id,
                                     std::string shortcut);
    QuaderApiVoidResult set_shortcut(QuaderCommandId command_id,
                                     std::string shortcut);
    QuaderApiVoidResult remove_shortcut(std::string_view command_id);
    QuaderApiVoidResult remove_shortcut(QuaderCommandId command_id);
    QuaderApiVoidResult reset_shortcut(std::string_view command_id);
    QuaderApiVoidResult reset_shortcut(QuaderCommandId command_id);
    QuaderApiVoidResult set_action_shortcut(
        const QuaderKeybindingActionKey &action,
        std::string shortcut,
        QuaderKeybindingConflictPolicy policy);
    QuaderApiVoidResult remove_action_shortcut(
        const QuaderKeybindingActionKey &action);
    QuaderApiVoidResult reset_action_shortcut(
        const QuaderKeybindingActionKey &action,
        QuaderKeybindingConflictPolicy policy);
    QuaderApiVoidResult reset_all_shortcuts();
    QuaderApiVoidResult set_keybinding_storage_path(std::filesystem::path path);

  private:
    void register_commands();
    void register_default_keybindings();
    void register_viewport_default_keybindings();
    void register_keybinding_target_metadata(
        std::string target_id, std::string label,
        std::string localization_key, QuaderKeybindingContextKind context,
        std::string when, std::string default_shortcut = {},
        std::string action_id = {});
    void set_keybinding_action_id(std::string_view target_id,
                                  std::string action_id);
    void set_keybinding_selection_modes(std::string_view target_id,
                                        std::uint32_t selection_mode_mask);
    void set_keybinding_selection_modes_for_modeling_action(
        std::string_view target_id, std::string_view modeling_action_id);
    void finalize_keybinding_scope_metadata();
    void normalize_default_action_keybindings();
    void normalize_loaded_user_keybindings();
    [[nodiscard]] const QuaderKeybindingTargetMetadata *
    keybinding_target_metadata(std::string_view target_id) const;
    [[nodiscard]] const QuaderKeybindingCatalogEntry *
    keybinding_catalog_entry(
        const std::vector<QuaderKeybindingCatalogEntry> &catalog,
        const QuaderKeybindingActionKey &action) const;
    [[nodiscard]] const QuaderKeybindingCatalogEntry *
    keybinding_catalog_entry_for_target(
        const std::vector<QuaderKeybindingCatalogEntry> &catalog,
        std::string_view target_id, bool &ambiguous) const;
    void write_action_override(
        std::vector<QuaderKeybindingRule> &candidate,
        const QuaderKeybindingCatalogEntry &entry,
        std::string_view shortcut) const;
    void register_viewport_view_command(QuaderCommandId id, std::string label,
                                        ViewportView view,
                                        std::string shortcut = {});
    void register_command(QuaderCommandId id, std::string label,
                          std::function<QuaderApiVoidResult()> execute,
                          std::string shortcut = {},
                          std::function<bool()> enabled = {},
                          bool visible = true);
    void register_command(std::string id, std::string label,
                          std::function<QuaderApiVoidResult()> execute,
                          std::string shortcut = {},
                          std::function<bool()> enabled = {},
                          bool visible = true);
    [[nodiscard]] QuaderApiVoidResult failure(std::string message) const;

    QuaderApi& api_;
    QuaderProjectApi& project_;
    QuaderSceneDocumentsApi &scene_documents_;
    QuaderViewportApi& viewports_;
    QuaderToolsApi& tools_;
    QuaderPreferencesApi& preferences_;
    QuaderUIApi &ui_;
#ifndef NDEBUG
    QuaderDevToolsApi& dev_tools_;
#endif
    EditorCommandRegistry registry_;
    QuaderKeybindingService keybindings_;
    /// Identifies which adapter owns the shared in-progress sequence.
    enum class KeybindingInputOwner {
      Viewport, Outliner, Editor, ToolSettings, ModalDialog
    };
    KeybindingInputOwner pending_keybinding_owner_ =
        KeybindingInputOwner::Viewport;
    QuaderKeybindingTargetCatalog keybinding_target_catalog_;
    std::optional<QuaderApiStatus> last_status_;
    std::uint64_t last_status_revision_ = 0;
};

QuaderCommandsApi::Impl::Impl(
    QuaderApi &api, QuaderProjectApi &project,
    QuaderSceneDocumentsApi &scene_documents, QuaderViewportApi &viewports,
    QuaderToolsApi &tools, QuaderPreferencesApi &preferences, QuaderUIApi &ui
#ifndef NDEBUG
    ,
    QuaderDevToolsApi &dev_tools
#endif
    )
    : api_(api)
    , project_(project)
    , scene_documents_(scene_documents)
    , viewports_(viewports)
    , tools_(tools)
    , preferences_(preferences)
    , ui_(ui)
#ifndef NDEBUG
    , dev_tools_(dev_tools)
#endif
{
  keybindings_.set_atomic_text_writer(write_keybindings_atomically);
  keybindings_.set_storage_path(default_keybindings_path());
  register_commands();
  register_default_keybindings();
  if (keybindings_.load_user_keybindings() == std::nullopt) {
    normalize_loaded_user_keybindings();
  }
}

QuaderApiVoidResult
QuaderCommandsApi::Impl::execute(QuaderCommandId command_id) {
  QDR_PROFILE_FUNCTION();
  const std::string_view id = command_id.string();
  QDR_PROFILE_NAME(id.data(), id.size());
  return execute(command_id.string());
}

QuaderApiVoidResult
QuaderCommandsApi::Impl::execute(std::string_view command_id) {
  QDR_PROFILE_FUNCTION();
  QDR_PROFILE_NAME(command_id.data(), command_id.size());
  QDR_PROFILE_MESSAGE_DYNAMIC(command_id.data(), command_id.size());
  if (std::optional<QuaderApiVoidResult> result =
          registry_.execute(command_id)) {
    result->revisions = api_.revisions();
    last_status_ = *result;
    ++last_status_revision_;
    return *result;
  }
  QuaderApiVoidResult result = failure("Unknown command id.");
  last_status_ = result;
  ++last_status_revision_;
  return result;
}

std::optional<QuaderApiStatus>
QuaderCommandsApi::Impl::last_status() const {
  return last_status_;
}

std::uint64_t QuaderCommandsApi::Impl::last_status_revision() const {
  return last_status_revision_;
}

std::uint64_t QuaderCommandsApi::Impl::keybindings_revision() const noexcept {
  return keybindings_.revision();
}

std::optional<QuaderCommandDescriptor>
QuaderCommandsApi::Impl::descriptor(std::string_view command_id) const {
  std::optional<QuaderCommandDescriptor> command =
      registry_.descriptor(command_id);
  if (command.has_value()) {
    command->shortcut = shortcut_for(command->id);
  }
  return command;
}

std::vector<QuaderCommandDescriptor> QuaderCommandsApi::Impl::available() const {
  QDR_PROFILE_FUNCTION();
  std::vector<QuaderCommandDescriptor> commands;
  {
    QDR_PROFILE_SCOPE("Quader commands registry scan");
    commands = registry_.available();
  }
  {
    QDR_PROFILE_SCOPE("Quader commands shortcut enrichment");
    for (QuaderCommandDescriptor &command : commands) {
      command.shortcut = shortcut_for(command.id);
    }
  }
  return commands;
}

std::string QuaderCommandsApi::Impl::command_id_for_modeling_action(
    std::string_view action_id) const {
  return quader::command_id_for_modeling_action(action_id);
}

std::string QuaderCommandsApi::Impl::command_id_for_tool_activation(
    std::string_view tool_id) const {
  return quader::command_id_for_tool_activation(tool_id);
}

std::string QuaderCommandsApi::Impl::keybinding_target_id_for_selection_mode(
    QuaderSelectionMode mode) const {
  return quader::keybinding_target_id_for_selection_mode(mode);
}

std::string QuaderCommandsApi::Impl::keybinding_target_id_for_grid_preset(
    int preset) const {
  return quader::keybinding_target_id_for_grid_preset(preset);
}

std::string
QuaderCommandsApi::Impl::shortcut_for(std::string_view command_id) const {
  if (!release_allows_input_target(command_id)) {
    return {};
  }
  return keybindings_.shortcut_for(command_id);
}

std::string
QuaderCommandsApi::Impl::shortcut_for(QuaderCommandId command_id) const {
  return shortcut_for(command_id.string());
}

std::vector<QuaderKeybindingRule> QuaderCommandsApi::Impl::keybindings() const {
  auto rules = keybindings_.keybindings();
  std::erase_if(rules, [](const QuaderKeybindingRule &rule) {
    return !release_allows_input_target(rule.target_id);
  });
  return rules;
}

std::vector<QuaderKeybindingCatalogEntry>
QuaderCommandsApi::Impl::keybinding_catalog() const {
  const std::vector<QuaderKeybindingRule> effective = keybindings_.keybindings();
  const std::vector<QuaderKeybindingRule> defaults =
      keybindings_.default_keybindings();
  const std::vector<QuaderKeybindingRule> user =
      keybindings_.user_keybindings();

  /// Accumulates resolver aliases for one real action in one stable scope.
  struct CatalogGroup {
    std::string action_id;
    std::string label;
    std::string localization_key;
    QuaderKeybindingContextKind context =
        QuaderKeybindingContextKind::Global;
    std::string scope_id;
    std::string scope_label;
    std::string scope_localization_key;
    std::vector<std::string> target_ids;
    std::vector<std::string> whens;
    /// Stores one ordered effective mapping while resolver aliases are grouped.
    struct Mapping {
      std::size_t order = 0u;
      std::string shortcut;
    };
    std::vector<Mapping> mappings;
    std::vector<Mapping> default_mappings;
    bool customized = false;
  };

  std::vector<CatalogGroup> groups;
  groups.reserve(keybinding_target_catalog_.entries().size());
  const auto group_for = [&](const QuaderKeybindingTargetMetadata &metadata)
      -> CatalogGroup & {
    const std::string_view action_id = metadata.action_id.empty()
                                           ? std::string_view(metadata.target_id)
                                           : std::string_view(metadata.action_id);
    const auto found = std::find_if(
        groups.begin(), groups.end(), [&](const CatalogGroup &group) {
          return group.action_id == action_id &&
                 group.scope_id == metadata.scope_id;
        });
    if (found != groups.end()) {
      return *found;
    }
    groups.push_back({
        .action_id = std::string(action_id),
        .label = metadata.label,
        .localization_key = metadata.localization_key,
        .context = metadata.context,
        .scope_id = metadata.scope_id,
        .scope_label = metadata.scope_label,
        .scope_localization_key = metadata.scope_localization_key,
    });
    return groups.back();
  };
  const auto append_unique = [](std::vector<std::string> &values,
                                std::string_view value) {
    if (!value.empty() &&
        std::find(values.begin(), values.end(), value) == values.end()) {
      values.emplace_back(value);
    }
  };

  for (const QuaderKeybindingTargetMetadata &metadata :
       keybinding_target_catalog_.entries()) {
    CatalogGroup &group = group_for(metadata);
    append_unique(group.target_ids, metadata.target_id);
    append_unique(group.whens, metadata.when);
  }

  for (CatalogGroup &group : groups) {
    for (const QuaderKeybindingRule &rule : effective) {
      if (std::find(group.target_ids.begin(), group.target_ids.end(),
                    rule.target_id) != group.target_ids.end() &&
          !rule.shortcut.empty()) {
        group.mappings.push_back({rule.order, rule.shortcut});
      }
    }
    for (const QuaderKeybindingRule &rule : defaults) {
      if (std::find(group.target_ids.begin(), group.target_ids.end(),
                    rule.target_id) == group.target_ids.end()) {
        continue;
      }
      if (rule.shortcut.empty()) {
        continue;
      }
      group.default_mappings.push_back({rule.order, rule.shortcut});
    }
    group.customized = std::any_of(
        user.begin(), user.end(), [&](const QuaderKeybindingRule &rule) {
          return std::find(group.target_ids.begin(), group.target_ids.end(),
                           rule.target_id) != group.target_ids.end();
        });
  }

  const auto joined_when = [](const std::vector<std::string> &values) {
    if (values.size() == 1u) {
      return values.front();
    }
    std::string result;
    for (const std::string &value : values) {
      if (!result.empty()) {
        result.append(" || ");
      }
      result.append("(").append(value).append(")");
    }
    return result;
  };

  std::vector<QuaderKeybindingCatalogEntry> catalog;
  catalog.reserve(groups.size());
  for (CatalogGroup &group : groups) {
    std::stable_sort(group.mappings.begin(), group.mappings.end(),
                     [](const auto &left, const auto &right) {
                       return left.order < right.order;
                     });
    std::stable_sort(group.default_mappings.begin(),
                     group.default_mappings.end(),
                     [](const auto &left, const auto &right) {
                       return left.order < right.order;
                     });
    const std::string shortcut =
        group.mappings.empty() ? std::string{}
                               : group.mappings.back().shortcut;
    const std::string default_shortcut =
        group.default_mappings.empty()
            ? std::string{}
            : group.default_mappings.back().shortcut;
    const bool editable = std::all_of(
        group.target_ids.begin(), group.target_ids.end(),
        [&](const std::string &target) {
          return keybindings_.target_editable(target);
        });
    catalog.push_back({
        .key = {.action_id = group.action_id, .scope_id = group.scope_id},
        .action_id = group.action_id,
        .target_id = group.target_ids.empty() ? std::string{}
                                               : group.target_ids.front(),
        .target_ids = std::move(group.target_ids),
        .label = std::move(group.label),
        .localization_key = std::move(group.localization_key),
        .context = group.context,
        .scope_id = std::move(group.scope_id),
        .scope_label = std::move(group.scope_label),
        .scope_localization_key =
            std::move(group.scope_localization_key),
        .when = joined_when(group.whens),
        .shortcut = shortcut,
        .default_shortcut = default_shortcut,
        .source = group.customized ? QuaderKeybindingSourceKind::User
                                   : QuaderKeybindingSourceKind::Default,
        .customized = group.customized,
        .unassigned = shortcut.empty(),
        .editable = editable,
    });
  }
  return catalog;
}

std::string QuaderCommandsApi::Impl::format_input_mapping(
    const QuaderInputChord &chord) const {
  return quader_input_format_chord(chord);
}

std::vector<QuaderKeybindingConflict>
QuaderCommandsApi::Impl::conflicts_for_shortcut(std::string_view shortcut,
                                                std::string_view when) const {
  auto conflicts = keybindings_.conflicts_for(shortcut, when);
  std::erase_if(conflicts, [](const QuaderKeybindingConflict &conflict) {
    return !release_allows_input_target(conflict.target_id);
  });
  return conflicts;
}

const QuaderKeybindingCatalogEntry *
QuaderCommandsApi::Impl::keybinding_catalog_entry(
    const std::vector<QuaderKeybindingCatalogEntry> &catalog,
    const QuaderKeybindingActionKey &action) const {
  const auto found = std::find_if(
      catalog.begin(), catalog.end(),
      [&](const QuaderKeybindingCatalogEntry &entry) {
        return entry.key == action;
      });
  return found == catalog.end() ? nullptr : &*found;
}

const QuaderKeybindingCatalogEntry *
QuaderCommandsApi::Impl::keybinding_catalog_entry_for_target(
    const std::vector<QuaderKeybindingCatalogEntry> &catalog,
    std::string_view target_id, bool &ambiguous) const {
  ambiguous = false;
  const QuaderKeybindingCatalogEntry *result = nullptr;
  for (const QuaderKeybindingCatalogEntry &entry : catalog) {
    if (std::find(entry.target_ids.begin(), entry.target_ids.end(), target_id) ==
        entry.target_ids.end()) {
      continue;
    }
    if (result != nullptr && result->key != entry.key) {
      ambiguous = true;
      return nullptr;
    }
    result = &entry;
  }
  return result;
}

QuaderKeybindingAssignmentAnalysis
QuaderCommandsApi::Impl::analyze_shortcut_assignment(
    const QuaderKeybindingActionKey &action,
    std::string_view shortcut) const {
  QuaderKeybindingAssignmentAnalysis analysis;
  const std::optional<std::vector<QuaderInputChord>> proposed =
      quader_input_parse_shortcut(shortcut);
  if (!proposed || proposed->empty()) {
    analysis.message = "Shortcut is invalid.";
    return analysis;
  }
  analysis.canonical_shortcut = quader_input_format_shortcut(*proposed);

  const std::vector<QuaderKeybindingCatalogEntry> catalog =
      keybinding_catalog();
  const QuaderKeybindingCatalogEntry *proposed_entry =
      keybinding_catalog_entry(catalog, action);
  if (proposed_entry == nullptr) {
    analysis.message = "Unknown keybinding action.";
    return analysis;
  }
  if (!release_allows_input_target(proposed_entry->target_id)) {
    analysis.message = release_feature_unavailable_message();
    return analysis;
  }
  if (material_assignment_uses_reserved_bare_right_mouse(action,
                                                           *proposed)) {
    analysis.message =
        "Unmodified right mouse gestures are reserved for viewport "
        "navigation and context menus.";
    return analysis;
  }
  if (source_preserving_material_assignment_uses_unsupported_pointer_gesture(
          action, *proposed)) {
    analysis.message =
        "Face material transfer requires a mouse button press, click, or "
        "double-click; drag and wheel gestures do not identify one exact "
        "owned pointer edge.";
    return analysis;
  }
  if (transform_vertex_snap_assignment_uses_non_holdable_shortcut(
          action, *proposed)) {
    analysis.message =
        "Hold Vertex Snap requires exactly one keyboard press chord so it "
        "can remain active while held.";
    return analysis;
  }
  if (const auto error = keybindings_.validate_user_keybinding({
          .target_id = proposed_entry->target_id,
          .sequence = *proposed,
          .when = proposed_entry->when,
      })) {
    analysis.message = *error;
    return analysis;
  }

  const bool proposed_is_default =
      proposed_entry->default_shortcut == analysis.canonical_shortcut;

  for (const QuaderKeybindingCatalogEntry &entry : catalog) {
    if (entry.key == action || entry.shortcut.empty() ||
        !release_allows_input_target(entry.target_id)) {
      continue;
    }
    const std::string &existing_shortcut = entry.shortcut;
    const std::optional<std::vector<QuaderInputChord>> existing =
        quader_input_parse_shortcut(existing_shortcut);
    if (!existing || existing->empty()) {
      continue;
    }

    const bool exact = quader_input_shortcuts_equal(*proposed, *existing);
    const bool proposed_prefix =
        !exact && quader_input_sequence_starts_with(*existing, *proposed);
    const bool existing_prefix =
        !exact && quader_input_sequence_starts_with(*proposed, *existing);
    if (!exact && !proposed_prefix && !existing_prefix) {
      continue;
    }

    const bool overlaps = keybinding_detail::when_expressions_overlap(
        proposed_entry->when, entry.when);
    const bool baseline_default_relationship =
        proposed_is_default &&
        entry.default_shortcut == existing_shortcut &&
        entry.source == QuaderKeybindingSourceKind::Default;
    if (overlaps && baseline_default_relationship) {
      continue;
    }

    QuaderKeybindingCollision collision{
        .action = entry.key,
        .target_id = entry.target_id,
        .label = entry.label,
        .localization_key = entry.localization_key,
        .shortcut = existing_shortcut,
        .when = entry.when,
        .source = entry.source,
        .kind = overlaps
                    ? (exact ? QuaderKeybindingCollisionKind::ExactConflict
                             : QuaderKeybindingCollisionKind::PrefixConflict)
                    : QuaderKeybindingCollisionKind::CompatibleReuse,
    };
    if (overlaps) {
      analysis.conflicts.push_back(std::move(collision));
    } else {
      analysis.compatible_reuses.push_back(std::move(collision));
    }
  }

  analysis.ok = true;
  analysis.message = analysis.conflicts.empty()
                         ? "Shortcut is available in this context."
                         : "Shortcut conflicts with another action.";
  return analysis;
}

void QuaderCommandsApi::Impl::write_action_override(
    std::vector<QuaderKeybindingRule> &candidate,
    const QuaderKeybindingCatalogEntry &entry,
    std::string_view shortcut) const {
  candidate.erase(
      std::remove_if(candidate.begin(), candidate.end(),
                     [&](const QuaderKeybindingRule &rule) {
                       return std::find(entry.target_ids.begin(),
                                        entry.target_ids.end(),
                                        rule.target_id) != entry.target_ids.end();
                     }),
      candidate.end());

  if (shortcut == entry.default_shortcut) {
    return;
  }
  for (const std::string &target_id : entry.target_ids) {
    const QuaderKeybindingTargetMetadata *metadata =
        keybinding_target_metadata(target_id);
    if (metadata == nullptr) {
      continue;
    }
    if (shortcut.empty()) {
      candidate.push_back({
          .target_id = target_id,
          .source = "user",
          .removal = true,
      });
      continue;
    }
    candidate.push_back({
        .target_id = target_id,
        .shortcut = std::string(shortcut),
        .when = metadata->when,
        .source = "user",
    });
  }
}

QuaderApiVoidResult QuaderCommandsApi::Impl::set_action_shortcut(
    const QuaderKeybindingActionKey &action,
    std::string shortcut,
    QuaderKeybindingConflictPolicy policy) {
  const std::vector<QuaderKeybindingCatalogEntry> catalog =
      keybinding_catalog();
  const QuaderKeybindingCatalogEntry *entry =
      keybinding_catalog_entry(catalog, action);
  if (entry == nullptr) {
    return failure("Unknown keybinding action.");
  }

  QuaderKeybindingAssignmentAnalysis analysis =
      analyze_shortcut_assignment(action, shortcut);
  if (!analysis.ok) {
    return failure(std::move(analysis.message));
  }

  if (analysis.canonical_shortcut == entry->shortcut) {
    return {
        .ok = true,
        .changed = false,
        .message = "Keybindings unchanged.",
        .mutation_kind = QuaderMutationKind::None,
        .revisions = api_.revisions(),
    };
  }
  if (!analysis.conflicts.empty() &&
      policy == QuaderKeybindingConflictPolicy::Reject) {
    return failure("Keybinding change has unresolved conflicts.");
  }

  std::vector<QuaderKeybindingRule> candidate =
      keybindings_.user_keybindings();
  if (policy == QuaderKeybindingConflictPolicy::Replace) {
    std::vector<QuaderKeybindingActionKey> replaced_actions;
    for (const QuaderKeybindingCollision &collision : analysis.conflicts) {
      if (std::find(replaced_actions.begin(), replaced_actions.end(),
                    collision.action) != replaced_actions.end()) {
        continue;
      }
      const QuaderKeybindingCatalogEntry *conflicting_entry =
          keybinding_catalog_entry(catalog, collision.action);
      if (conflicting_entry == nullptr) {
        continue;
      }
      write_action_override(candidate, *conflicting_entry, {});
      replaced_actions.push_back(collision.action);
    }
  }

  write_action_override(candidate, *entry, analysis.canonical_shortcut);
  const QuaderKeybindingMutationResult mutation =
      keybindings_.replace_user_keybindings(std::move(candidate));
  if (!mutation.ok) {
    return failure(mutation.message);
  }
  return {
      .ok = true,
      .changed = mutation.changed,
      .message = mutation.changed ? "Keybindings updated." : mutation.message,
      .mutation_kind = QuaderMutationKind::None,
      .revisions = api_.revisions(),
  };
}

QuaderApiVoidResult QuaderCommandsApi::Impl::remove_action_shortcut(
    const QuaderKeybindingActionKey &action) {
  const std::vector<QuaderKeybindingCatalogEntry> catalog =
      keybinding_catalog();
  const QuaderKeybindingCatalogEntry *entry =
      keybinding_catalog_entry(catalog, action);
  if (entry == nullptr) {
    return failure("Unknown keybinding action.");
  }
  if (!entry->editable) {
    return failure("Orbit and Pan bindings are fixed and cannot be changed.");
  }
  if (entry->unassigned) {
    return {
        .ok = true,
        .changed = false,
        .message = "Keybinding already unassigned.",
        .mutation_kind = QuaderMutationKind::None,
        .revisions = api_.revisions(),
    };
  }
  std::vector<QuaderKeybindingRule> candidate =
      keybindings_.user_keybindings();
  write_action_override(candidate, *entry, {});
  const QuaderKeybindingMutationResult mutation =
      keybindings_.replace_user_keybindings(std::move(candidate));
  if (!mutation.ok) {
    return failure(mutation.message);
  }
  return {
      .ok = true,
      .changed = mutation.changed,
      .message = mutation.changed ? "Keybinding unassigned." : mutation.message,
      .mutation_kind = QuaderMutationKind::None,
      .revisions = api_.revisions(),
  };
}

QuaderApiVoidResult QuaderCommandsApi::Impl::reset_action_shortcut(
    const QuaderKeybindingActionKey &action,
    QuaderKeybindingConflictPolicy policy) {
  const std::vector<QuaderKeybindingCatalogEntry> catalog =
      keybinding_catalog();
  const QuaderKeybindingCatalogEntry *entry =
      keybinding_catalog_entry(catalog, action);
  if (entry == nullptr) {
    return failure("Unknown keybinding action.");
  }
  if (!entry->default_shortcut.empty()) {
    return set_action_shortcut(action, entry->default_shortcut, policy);
  }
  if (!entry->customized) {
    return {
        .ok = true,
        .changed = false,
        .message = "Keybinding already at default.",
        .mutation_kind = QuaderMutationKind::None,
        .revisions = api_.revisions(),
    };
  }
  std::vector<QuaderKeybindingRule> candidate =
      keybindings_.user_keybindings();
  write_action_override(candidate, *entry, {});
  const QuaderKeybindingMutationResult mutation =
      keybindings_.replace_user_keybindings(std::move(candidate));
  if (!mutation.ok) {
    return failure(mutation.message);
  }
  return {
      .ok = true,
      .changed = mutation.changed,
      .message = mutation.changed ? "Keybinding reset." : mutation.message,
      .mutation_kind = QuaderMutationKind::None,
      .revisions = api_.revisions(),
  };
}

QuaderApiVoidResult QuaderCommandsApi::Impl::reset_all_shortcuts() {
  // Keep dormant overrides so a later cumulative release restores them.
  auto retained = keybindings_.user_keybindings();
  std::erase_if(retained, [](const QuaderKeybindingRule &rule) {
    return release_allows_input_target(rule.target_id);
  });
  const QuaderKeybindingMutationResult mutation =
      keybindings_.replace_user_keybindings(std::move(retained));
  if (!mutation.ok) {
    return failure(mutation.message);
  }
  if (!mutation.changed) {
    if (const std::optional<std::string> error =
            keybindings_.save_user_keybindings()) {
      return failure(*error);
    }
  }
  return {
      .ok = true,
      .changed = mutation.changed,
      .message = mutation.changed ? "All keybindings reset." : mutation.message,
      .mutation_kind = QuaderMutationKind::None,
      .revisions = api_.revisions(),
  };
}

bool QuaderCommandsApi::Impl::resolve_viewport_shortcut(
    ViewportKey key, ViewportModifiers modifiers,
    const ViewportToolState &state, bool viewport_focused,
    bool text_input_active, ViewportShortcut &shortcut,
    QuaderViewportKeybindingContext input_context) {
  const std::optional<QuaderInputChord> chord =
      quader_input_chord_for_viewport_key(key, modifiers);
  return chord ? resolve_viewport_shortcut(*chord, state, viewport_focused,
                                           text_input_active, shortcut,
                                           input_context)
               : false;
}

bool QuaderCommandsApi::Impl::resolve_viewport_shortcut(
    const QuaderInputChord &chord, const ViewportToolState &state,
    bool viewport_focused, bool text_input_active, ViewportShortcut &shortcut,
    QuaderViewportKeybindingContext input_context) {
  const QuaderViewportKeybindingDispatch dispatch =
      dispatch_viewport_input_binding(chord, state, viewport_focused,
                                      text_input_active, input_context);
  if (dispatch.kind == QuaderViewportKeybindingDispatch::Kind::PendingChord) {
    shortcut.kind = ViewportShortcutKind::None;
    return true;
  }
  if (dispatch.kind !=
      QuaderViewportKeybindingDispatch::Kind::ViewportShortcut) {
    return false;
  }
  shortcut = dispatch.shortcut;
  return true;
}

bool QuaderCommandsApi::Impl::resolve_viewport_input_action(
    const QuaderInputChord &chord, const ViewportToolState &state,
    bool viewport_focused, bool text_input_active,
    ViewportInputActionKind &action,
    QuaderViewportKeybindingContext input_context) {
  action = ViewportInputActionKind::None;
  if (chord.empty()) {
    return false;
  }

  const QuaderKeybindingContext context = viewport_keybinding_context(
      state, viewport_focused, text_input_active, input_context,
      api_.history().can_undo(), api_.history().can_redo(), tools_);
  const QuaderKeybindingDispatch dispatch =
      keybindings_.resolve_sequence(
          {chord}, context, [](std::string_view target_id) {
            if (!release_allows_input_target(target_id)) {
              return false;
            }
            ViewportInputActionKind candidate = ViewportInputActionKind::None;
            return viewport_input_action_for_target_id(target_id, candidate);
          });
  return dispatch.kind == QuaderKeybindingDispatch::Kind::ExactMatch &&
         viewport_input_action_for_target_id(dispatch.target_id, action);
}

QuaderViewportKeybindingDispatch
QuaderCommandsApi::Impl::dispatch_viewport_keybinding(
    ViewportKey key, ViewportModifiers modifiers,
    const ViewportToolState &state, bool viewport_focused,
    bool text_input_active, QuaderViewportKeybindingContext input_context) {
  const std::optional<QuaderInputChord> chord =
      quader_input_chord_for_viewport_key(key, modifiers);
  return chord
             ? dispatch_viewport_input_binding(*chord, state, viewport_focused,
                                               text_input_active, input_context)
             : QuaderViewportKeybindingDispatch{};
}

QuaderViewportKeybindingDispatch
QuaderCommandsApi::Impl::dispatch_viewport_input_binding(
    const QuaderInputChord &chord, const ViewportToolState &state,
    bool viewport_focused, bool text_input_active,
    QuaderViewportKeybindingContext input_context) {
  if (chord.empty()) {
    reset_pending_keybinding_chord();
    return {};
  }

  const KeybindingInputOwner input_owner = input_context.outliner_focused
      ? KeybindingInputOwner::Outliner
      : input_context.editor_focused ? KeybindingInputOwner::Editor
                                     : KeybindingInputOwner::Viewport;
  const bool panel_owns_input = input_owner != KeybindingInputOwner::Viewport;
  if (keybindings_.has_pending_chord() &&
      pending_keybinding_owner_ != input_owner) {
    reset_pending_keybinding_chord();
  }

  const QuaderKeybindingContext context = viewport_keybinding_context(
      state, viewport_focused, text_input_active, input_context,
      api_.history().can_undo(), api_.history().can_redo(), tools_);
  const auto target_available = [this, &state, panel_owns_input](
                                    std::string_view target_id) {
    if (!release_allows_input_target(target_id)) {
      return false;
    }
    const bool registered = registry_.contains(target_id);
    // Editor panels execute commands and the two owner-only mode/grid intents.
    // Pointer, navigation, and popup targets need the viewport's semantic router.
    // Exclude those before resolving a sequence so they cannot take a prefix.
    if (panel_owns_input && !registered &&
        !editor_panel_can_apply_shortcut(target_id)) {
      return false;
    }
    if (registered && !registry_.enabled(target_id)) {
      return false;
    }
    ViewportInputActionKind action = ViewportInputActionKind::None;
    if (viewport_input_action_for_target_id(target_id, action)) {
      return true;
    }
    if (registered && !viewport_target_requires_router_dispatch(target_id)) {
      return true;
    }
    ViewportShortcut shortcut;
    if (viewport_router_shortcut_for_target_id(target_id, state.grid_preset,
                                               shortcut)) {
      return true;
    }
    return registered;
  };
  const QuaderKeybindingDispatch dispatch =
      keybindings_.dispatch(chord, context, target_available);
  pending_keybinding_owner_ = input_owner;
  if (dispatch.kind == QuaderKeybindingDispatch::Kind::PendingChord) {
    return {
        .kind = QuaderViewportKeybindingDispatch::Kind::PendingChord,
    };
  }
  if (dispatch.kind != QuaderKeybindingDispatch::Kind::ExactMatch) {
    return {};
  }

  ViewportInputActionKind action = ViewportInputActionKind::None;
  if (viewport_input_action_for_target_id(dispatch.target_id, action)) {
    return {
        .kind = QuaderViewportKeybindingDispatch::Kind::ViewportInputAction,
        .action = action,
    };
  }

  if (registry_.contains(dispatch.target_id) &&
      (panel_owns_input ||
       !viewport_target_requires_router_dispatch(dispatch.target_id))) {
    return {
        .kind = QuaderViewportKeybindingDispatch::Kind::Command,
        .command_id = dispatch.target_id,
    };
  }

  ViewportShortcut shortcut;
  if (viewport_router_shortcut_for_target_id(
          dispatch.target_id, state.grid_preset, shortcut)) {
    return {
        .kind = QuaderViewportKeybindingDispatch::Kind::ViewportShortcut,
        .shortcut = shortcut,
    };
  }

  if (registry_.contains(dispatch.target_id)) {
    return {
        .kind = QuaderViewportKeybindingDispatch::Kind::Command,
        .command_id = dispatch.target_id,
    };
  }

  return {};
}

std::optional<std::string>
QuaderCommandsApi::Impl::resolve_immediate_global_command(
    const QuaderInputChord &chord, const ViewportToolState &state,
    bool text_input_active,
    QuaderViewportKeybindingContext input_context) const {
  if (chord.empty()) {
    return std::nullopt;
  }

  const QuaderKeybindingContext context = viewport_keybinding_context(
      state, true, text_input_active, input_context,
      api_.history().can_undo(), api_.history().can_redo(), tools_);
  const QuaderKeybindingDispatch dispatch = keybindings_.resolve_sequence(
      {chord}, context, [this](std::string_view target_id) {
        const QuaderKeybindingTargetMetadata *metadata =
            keybinding_target_metadata(target_id);
        return metadata != nullptr &&
               metadata->context == QuaderKeybindingContextKind::Global &&
               registry_.contains(target_id) && registry_.enabled(target_id) &&
               !viewport_target_requires_router_dispatch(target_id);
      });
  return dispatch.kind == QuaderKeybindingDispatch::Kind::ExactMatch
             ? std::optional<std::string>{dispatch.target_id}
             : std::nullopt;
}

std::optional<QuaderKeybindingContext>
QuaderCommandsApi::Impl::tool_settings_keybinding_context(
    const ViewportToolState &state) const {
  const auto pane = viewports_.focused_pane();
  if (!tools_.pane_owns_executing_tool(pane)) {
    return std::nullopt;
  }
  const auto settings = tools_.settings_for_pane(pane);
  if (!settings.active || (!settings.apply_visible && !settings.can_cancel)) {
    return std::nullopt;
  }
  const bool native = tools_.active_native_tool_lifecycle(state.active_tool);
  return viewport_keybinding_context(
      state, true, false,
      {.modal_tool_active = native,
       .active_tool_owns_viewport = native,
       .operation_preview_active = !native && settings.preview_active},
      api_.history().can_undo(), api_.history().can_redo(), tools_);
}

QuaderNativeToolSettingsIntent
QuaderCommandsApi::Impl::tool_settings_intent(std::string_view target_id) const {
  if (!release_allows_input_target(target_id)) {
    return QuaderNativeToolSettingsIntent::None;
  }
  if (target_id == quader_command::kModelingApplyActivePreview.string()) {
    return QuaderNativeToolSettingsIntent::Apply;
  }
  if (target_id == quader_command::kModelingCancelActivePreview.string()) {
    return QuaderNativeToolSettingsIntent::Cancel;
  }
  const auto *metadata = keybinding_target_metadata(target_id);
  if (metadata != nullptr && metadata->action_id == "tools.finalize") {
    return QuaderNativeToolSettingsIntent::Apply;
  }
  if (metadata != nullptr && metadata->action_id == "tools.cancel") {
    return QuaderNativeToolSettingsIntent::Cancel;
  }
  return QuaderNativeToolSettingsIntent::None;
}

QuaderNativeToolSettingsDispatch
QuaderCommandsApi::Impl::resolve_native_tool_settings_input_binding(
    const QuaderInputChord &chord, const ViewportToolState &state) const {
  const auto context = tool_settings_keybinding_context(state);
  if (!context) {
    return {};
  }
  const auto dispatch = keybindings_.resolve_next_chord(
      chord, *context,
      [this](std::string_view target_id) {
        return tool_settings_intent(target_id) !=
               QuaderNativeToolSettingsIntent::None;
      },
      pending_keybinding_owner_ == KeybindingInputOwner::ToolSettings);
  if (dispatch.kind == QuaderKeybindingDispatch::Kind::PendingChord) {
    return {.kind = QuaderNativeToolSettingsDispatch::Kind::PendingChord};
  }
  if (dispatch.kind == QuaderKeybindingDispatch::Kind::ExactMatch) {
    return {.kind = QuaderNativeToolSettingsDispatch::Kind::Intent,
            .intent = tool_settings_intent(dispatch.target_id)};
  }
  if (pending_keybinding_owner_ == KeybindingInputOwner::ToolSettings &&
      keybindings_.has_pending_chord()) {
    return {.kind = QuaderNativeToolSettingsDispatch::Kind::Consumed};
  }
  return {};
}

QuaderNativeToolSettingsDispatch
QuaderCommandsApi::Impl::dispatch_native_tool_settings_input_binding(
    const QuaderInputChord &chord, const ViewportToolState &state) {
  const auto resolved = resolve_native_tool_settings_input_binding(chord, state);
  if (resolved.kind == QuaderNativeToolSettingsDispatch::Kind::Consumed) {
    reset_pending_keybinding_chord();
    return resolved;
  }
  if (resolved.kind == QuaderNativeToolSettingsDispatch::Kind::NoMatch) {
    return {};
  }
  const auto context = tool_settings_keybinding_context(state);
  if (!context) {
    return {};
  }
  if (pending_keybinding_owner_ != KeybindingInputOwner::ToolSettings) {
    reset_pending_keybinding_chord();
  }
  const auto dispatch = keybindings_.dispatch(
      chord, *context, [this](std::string_view target_id) {
        return tool_settings_intent(target_id) !=
               QuaderNativeToolSettingsIntent::None;
      });
  pending_keybinding_owner_ = KeybindingInputOwner::ToolSettings;
  if (dispatch.kind == QuaderKeybindingDispatch::Kind::PendingChord) {
    return {.kind = QuaderNativeToolSettingsDispatch::Kind::PendingChord};
  }
  if (dispatch.kind == QuaderKeybindingDispatch::Kind::ExactMatch) {
    return {.kind = QuaderNativeToolSettingsDispatch::Kind::Intent,
            .intent = tool_settings_intent(dispatch.target_id)};
  }
  return {};
}

QuaderNativeToolSettingsDispatch
QuaderCommandsApi::Impl::resolve_modal_lifecycle_input_binding(
    const QuaderInputChord &chord) const {
  const auto dispatch = keybindings_.resolve_next_chord(
      chord, modal_lifecycle_keybinding_context(),
      [this](std::string_view target_id) {
        if (!release_allows_input_target(target_id)) {
          return false;
        }
        const auto *metadata = keybinding_target_metadata(target_id);
        return metadata != nullptr &&
            (metadata->action_id == "tools.finalize" ||
             metadata->action_id == "tools.cancel");
      },
      pending_keybinding_owner_ == KeybindingInputOwner::ModalDialog);
  if (dispatch.kind == QuaderKeybindingDispatch::Kind::PendingChord) {
    return {.kind = QuaderNativeToolSettingsDispatch::Kind::PendingChord};
  }
  if (dispatch.kind == QuaderKeybindingDispatch::Kind::ExactMatch) {
    return {.kind = QuaderNativeToolSettingsDispatch::Kind::Intent,
            .intent = tool_settings_intent(dispatch.target_id)};
  }
  if (pending_keybinding_owner_ == KeybindingInputOwner::ModalDialog &&
      keybindings_.has_pending_chord()) {
    return {.kind = QuaderNativeToolSettingsDispatch::Kind::Consumed};
  }
  return {};
}

QuaderNativeToolSettingsDispatch
QuaderCommandsApi::Impl::dispatch_modal_lifecycle_input_binding(
    const QuaderInputChord &chord) {
  const auto resolved = resolve_modal_lifecycle_input_binding(chord);
  if (resolved.kind == QuaderNativeToolSettingsDispatch::Kind::Consumed) {
    reset_pending_keybinding_chord();
    return resolved;
  }
  if (resolved.kind == QuaderNativeToolSettingsDispatch::Kind::NoMatch) {
    return {};
  }
  if (pending_keybinding_owner_ != KeybindingInputOwner::ModalDialog) {
    reset_pending_keybinding_chord();
  }
  const auto dispatch = keybindings_.dispatch(
      chord, modal_lifecycle_keybinding_context(),
      [this](std::string_view target_id) {
        if (!release_allows_input_target(target_id)) {
          return false;
        }
        const auto *metadata = keybinding_target_metadata(target_id);
        return metadata != nullptr &&
            (metadata->action_id == "tools.finalize" ||
             metadata->action_id == "tools.cancel");
      });
  pending_keybinding_owner_ = KeybindingInputOwner::ModalDialog;
  if (dispatch.kind == QuaderKeybindingDispatch::Kind::PendingChord) {
    return {.kind = QuaderNativeToolSettingsDispatch::Kind::PendingChord};
  }
  if (dispatch.kind == QuaderKeybindingDispatch::Kind::ExactMatch) {
    return {.kind = QuaderNativeToolSettingsDispatch::Kind::Intent,
            .intent = tool_settings_intent(dispatch.target_id)};
  }
  return {};
}

void QuaderCommandsApi::Impl::reset_pending_modal_lifecycle_input_binding() {
  if (pending_keybinding_owner_ == KeybindingInputOwner::ModalDialog) {
    reset_pending_keybinding_chord();
  }
}

QuaderApiVoidResult
QuaderCommandsApi::Impl::execute_native_tool_settings_intent(
    QuaderNativeToolSettingsIntent intent) {
  const auto pane = viewports_.focused_pane();
  const auto settings = tools_.settings_for_pane(pane);
  if (intent != QuaderNativeToolSettingsIntent::None &&
      (!tools_.pane_owns_executing_tool(pane) || !settings.active ||
       (intent == QuaderNativeToolSettingsIntent::Apply && !settings.can_apply) ||
       (intent == QuaderNativeToolSettingsIntent::Cancel && !settings.can_cancel))) {
    return {.ok = true, .changed = false, .revisions = api_.revisions()};
  }
  const auto *descriptor = find_viewport_tool_descriptor(tools_.active_tool());
  const bool native = descriptor != nullptr &&
      tools_.active_native_tool_lifecycle(descriptor->tool);
  switch (intent) {
  case QuaderNativeToolSettingsIntent::Apply:
    return native ? tools_.commit_active_tool_preview()
                  : execute_modeling_preview_intent(intent);
  case QuaderNativeToolSettingsIntent::Cancel:
    return native ? tools_.cancel_active_tool_preview()
                  : execute_modeling_preview_intent(intent);
  case QuaderNativeToolSettingsIntent::None:
    break;
  }
  return failure("No native Tool Settings lifecycle intent was provided.");
}

QuaderApiVoidResult QuaderCommandsApi::Impl::execute_modeling_preview_intent(
    QuaderNativeToolSettingsIntent intent) {
  const auto binding = api_.query().document_binding();
  const auto origin = tools_.active_settings_presentation_origin();
  const auto pane = origin.serial != 0u ? origin.pane : viewports_.focused_pane();
  const auto previous_tool = tools_.active_tool();
  auto result = intent == QuaderNativeToolSettingsIntent::Apply
      ? api_.modeling().commit_preview("Apply Tool Preview")
      : api_.modeling().cancel_preview();
  // A lifecycle callback may synchronously replace the document or start a
  // different operation. Only synchronize the owner that actually completed.
  if (result.ok() && api_.query().document_binding() == binding &&
      tools_.active_settings_presentation_origin() == origin &&
      tools_.active_tool() != previous_tool) {
    tools_.synchronize_pane_tool_after_execution(pane);
  }
  return result;
}

void QuaderCommandsApi::Impl::reset_pending_keybinding_chord() {
  keybindings_.reset_pending_chord();
  pending_keybinding_owner_ = KeybindingInputOwner::Viewport;
}

void QuaderCommandsApi::Impl::reset_pending_non_modal_keybinding_chord() {
  if (pending_keybinding_owner_ != KeybindingInputOwner::ModalDialog) {
    reset_pending_keybinding_chord();
  }
}

void QuaderCommandsApi::Impl::reset_pending_viewport_keybinding_chord() {
  if (pending_keybinding_owner_ == KeybindingInputOwner::Viewport) {
    reset_pending_keybinding_chord();
  }
}

bool QuaderCommandsApi::Impl::has_pending_keybinding_chord() const {
  return keybindings_.has_pending_chord();
}

QuaderApiVoidResult
QuaderCommandsApi::Impl::set_shortcut(std::string_view command_id,
                                      std::string shortcut) {
  const std::vector<QuaderKeybindingCatalogEntry> catalog =
      keybinding_catalog();
  bool ambiguous = false;
  const QuaderKeybindingCatalogEntry *entry =
      keybinding_catalog_entry_for_target(catalog, command_id, ambiguous);
  if (ambiguous) {
    return failure("Keybinding target belongs to more than one action scope.");
  }
  if (entry == nullptr) {
    return failure("Unknown keybinding target.");
  }
  return set_action_shortcut(entry->key, std::move(shortcut),
                             QuaderKeybindingConflictPolicy::Reject);
}

QuaderApiVoidResult
QuaderCommandsApi::Impl::set_shortcut(QuaderCommandId command_id,
                                      std::string shortcut) {
  return set_shortcut(command_id.string(), std::move(shortcut));
}

QuaderApiVoidResult
QuaderCommandsApi::Impl::remove_shortcut(std::string_view command_id) {
  const std::vector<QuaderKeybindingCatalogEntry> catalog =
      keybinding_catalog();
  bool ambiguous = false;
  const QuaderKeybindingCatalogEntry *entry =
      keybinding_catalog_entry_for_target(catalog, command_id, ambiguous);
  if (ambiguous) {
    return failure("Keybinding target belongs to more than one action scope.");
  }
  return entry != nullptr ? remove_action_shortcut(entry->key)
                          : failure("Unknown keybinding target.");
}

QuaderApiVoidResult
QuaderCommandsApi::Impl::remove_shortcut(QuaderCommandId command_id) {
  return remove_shortcut(command_id.string());
}

QuaderApiVoidResult
QuaderCommandsApi::Impl::reset_shortcut(std::string_view command_id) {
  const std::vector<QuaderKeybindingCatalogEntry> catalog =
      keybinding_catalog();
  bool ambiguous = false;
  const QuaderKeybindingCatalogEntry *entry =
      keybinding_catalog_entry_for_target(catalog, command_id, ambiguous);
  if (ambiguous) {
    return failure("Keybinding target belongs to more than one action scope.");
  }
  return entry != nullptr
             ? reset_action_shortcut(entry->key,
                                     QuaderKeybindingConflictPolicy::Reject)
             : failure("Unknown keybinding target.");
}

QuaderApiVoidResult
QuaderCommandsApi::Impl::reset_shortcut(QuaderCommandId command_id) {
  return reset_shortcut(command_id.string());
}

QuaderApiVoidResult QuaderCommandsApi::Impl::set_keybinding_storage_path(
    std::filesystem::path path) {
  const std::filesystem::path previous_path = keybindings_.storage_path();
  const std::vector<QuaderKeybindingRule> previous_rules =
      keybindings_.user_keybindings();
  keybindings_.set_storage_path(std::move(path));
  keybindings_.clear_user_keybindings();
  if (const std::optional<std::string> error =
          keybindings_.load_user_keybindings()) {
    keybindings_.set_storage_path(previous_path);
    (void)keybindings_.adopt_loaded_user_keybindings(previous_rules);
    return failure(*error);
  }
  normalize_loaded_user_keybindings();
  return {
      .ok = true,
      .changed = false,
      .message = "Shortcut storage path updated.",
      .mutation_kind = QuaderMutationKind::None,
      .revisions = api_.revisions(),
  };
}

void QuaderCommandsApi::Impl::register_commands() {
  auto register_modeling_tool_public_command =
      [this](QuaderCommandId command_id, std::string_view tool_id,
             std::string fallback_label, std::string shortcut = {}) {
        const CommandTool *tool = find_modeling_command_tool(tool_id);
        const std::string label = tool != nullptr && !tool->label().empty()
                                      ? std::string(tool->label())
                                      : std::move(fallback_label);
        register_command(
            command_id, label,
            [this, target_tool_id = std::string(tool_id)] {
              return api_.modeling().execute_tool(target_tool_id);
            },
            std::move(shortcut),
            [this, target_tool_id = std::string(tool_id)] {
              return api_.modeling().can_execute_tool(target_tool_id);
            });
        set_keybinding_selection_modes_for_modeling_action(
            command_id.string(), tool_id);
      };

  register_command(
      quader_command::kEditUndo, "Undo",
      [this] { return api_.history().undo(); }, "Ctrl+Z",
      [this] { return api_.history().can_undo(); });
  register_command(
      quader_command::kEditRedo, "Redo",
      [this] { return api_.history().redo(); }, "Ctrl+Shift+Z",
      [this] { return api_.history().can_redo(); });
  register_command(
      quader_command::kEditRepeatLastAction, "Repeat Last Action",
      [this] { return api_.history().repeat_last_action(); }, "Ctrl+R");
  register_command(quader_command::kEditClearSelection, "Clear Selection",
                   [this] { return api_.selection().clear(); }, "Escape");
  register_command(
      quader_command::kUiSearchCommands, "Search Commands",
      [this] { return ui_.show_command_search(); }, "Ctrl+Shift+P",
      [this] { return ui_.command_search_available(); });
  register_command(
      quader_command::kSelectAll, "Select All",
      [this] { return api_.selection().select_all(); }, "Ctrl+A");
  set_keybinding_selection_modes(quader_command::kSelectAll.string(),
                                 kSelectionModeAny);
  register_command(
      quader_command::kSelectInverse, "Select Inverse",
      [this] { return api_.selection().select_inverse(); }, "Ctrl+I");
  set_keybinding_selection_modes(quader_command::kSelectInverse.string(),
                                 kSelectionModeAny);
  register_command(
      quader_command::kSelectToggleBackfaceSelection,
      "Toggle Backface Selection",
      [this] {
        return preferences_.set_backface_selection_enabled(
            !preferences_.backface_selection_enabled());
      },
      "Ctrl+Shift+F9");
  set_keybinding_selection_modes(
      quader_command::kSelectToggleBackfaceSelection.string(),
      kSelectionModeAny);
  register_command(
      quader_command::kSelectEdgeDirection, "Select Direction",
      [this] { return api_.selection().select_edge_direction(); }, {},
      [this] {
        const QuaderSelectionSummary summary = api_.selection().summary();
        return summary.mode == QuaderSelectionMode::Edge &&
               summary.selected_edge_count > 0u;
      });
  set_keybinding_selection_modes(
      quader_command::kSelectEdgeDirection.string(),
      kSelectionModeEdge);
  register_command(
      quader_command::kSelectEdgeLoops, "Select Edge Loops",
      [this] { return api_.selection().select_edge_loops(); }, {},
      [this] {
        const QuaderSelectionSummary summary = api_.selection().summary();
        return summary.mode == QuaderSelectionMode::Edge &&
               summary.selected_edge_count > 0u;
      });
  set_keybinding_selection_modes(
      quader_command::kSelectEdgeLoops.string(), kSelectionModeEdge);
  register_command(
      quader_command::kSelectEdgeRing, "Select Edge Ring",
      [this] { return api_.selection().select_edge_ring(); }, {},
      [this] {
        const QuaderSelectionSummary summary = api_.selection().summary();
        return summary.mode == QuaderSelectionMode::Edge &&
               summary.selected_edge_count > 0u;
      });
  set_keybinding_selection_modes(
      quader_command::kSelectEdgeRing.string(), kSelectionModeEdge);
  register_command(
      quader_command::kSelectFacesWithSameMaterial,
      "Select Faces With Same Material",
      [this] { return api_.selection().select_faces_with_same_material(); },
      {}, [this] {
        const QuaderSelectionSummary summary = api_.selection().summary();
        return summary.mode == QuaderSelectionMode::Face &&
               summary.selected_face_count > 0u;
      });
  set_keybinding_selection_modes(
      quader_command::kSelectFacesWithSameMaterial.string(),
      kSelectionModeFace);
  register_command(
      quader_command::kSelectCoplanarFaces,
      "Select Co-planar Faces",
      [this] { return api_.selection().select_coplanar_faces(); }, {},
      [this] {
        const QuaderSelectionSummary summary = api_.selection().summary();
        return summary.mode == QuaderSelectionMode::Face &&
               summary.selected_face_count > 0u;
      });
  set_keybinding_selection_modes(
      quader_command::kSelectCoplanarFaces.string(),
      kSelectionModeFace);
  register_command(
      quader_command::kSelectFaceNormal, "Select Normal",
      [this] { return api_.selection().select_face_normal(); }, {}, [this] {
        const QuaderSelectionSummary summary = api_.selection().summary();
        return summary.mode == QuaderSelectionMode::Face &&
               summary.selected_face_count > 0u;
      });
  set_keybinding_selection_modes(
      quader_command::kSelectFaceNormal.string(), kSelectionModeFace);
  register_command(
      quader_command::kSelectMeshNGons, "Select N-Gons",
      [this] { return api_.selection().select_mesh_ngons(); }, {}, [this] {
        const QuaderSelectionSummary summary = api_.selection().summary();
        return summary.mode == QuaderSelectionMode::Object &&
               summary.selected_object_count > 0u;
      });
  set_keybinding_selection_modes(
      quader_command::kSelectMeshNGons.string(), kSelectionModeObject);
  register_command(
      quader_command::kSelectSimilarMeshes, "Select Similar Meshes",
      [this] { return api_.selection().select_similar_meshes(); }, {},
      [this] {
        const QuaderSelectionSummary summary = api_.selection().summary();
        return summary.mode == QuaderSelectionMode::Object &&
               summary.selected_object_count > 0u;
      });
  set_keybinding_selection_modes(
      quader_command::kSelectSimilarMeshes.string(),
      kSelectionModeObject);
  register_command(
      quader_command::kSelectGrow, "Grow Selection",
      [this] { return api_.selection().grow(); }, {}, [this] {
        const QuaderSelectionSummary summary = api_.selection().summary();
        return (summary.mode == QuaderSelectionMode::Vertex ||
                summary.mode == QuaderSelectionMode::Edge ||
                summary.mode == QuaderSelectionMode::Face) &&
               summary.has_selection;
      });
  set_keybinding_selection_modes(quader_command::kSelectGrow.string(),
                                 kSelectionModeComponent);
  register_command(
      quader_command::kSelectShrink, "Shrink Selection",
      [this] { return api_.selection().shrink(); }, {}, [this] {
        const QuaderSelectionSummary summary = api_.selection().summary();
        return (summary.mode == QuaderSelectionMode::Vertex ||
                summary.mode == QuaderSelectionMode::Edge ||
                summary.mode == QuaderSelectionMode::Face) &&
               summary.has_selection;
      });
  set_keybinding_selection_modes(quader_command::kSelectShrink.string(),
                                 kSelectionModeComponent);
  register_modeling_tool_public_command(quader_command::kDeleteSelection,
                                        "delete_selection", "Delete Selection");
  register_command(
      quader_command::kCutSelection, "Cut Selection",
      [this] { return api_.modeling().cut_selection(); }, "Ctrl+X",
      [this] { return api_.modeling().can_cut_selection(); });
  set_keybinding_selection_modes_for_modeling_action(
      quader_command::kCutSelection.string(), "copy_selection");
  register_modeling_tool_public_command(quader_command::kCopySelection,
                                        "copy_selection", "Copy Selection",
                                        "Ctrl+C");
  register_modeling_tool_public_command(quader_command::kPasteSelection,
                                        "paste_selection", "Paste Selection",
                                        "Ctrl+V");
  register_command(
      quader_command::kDuplicateSelection, "Duplicate Selection",
      [this] { return api_.modeling().duplicate_selected_entity_hierarchy(); },
      "Ctrl+D", [this] { return duplicate_selection_available(api_); });
  register_command(
      quader_command::kEditCreateInstance, "Create Instance",
      [this] { return api_.modeling().create_instance(); }, {},
      [this] { return api_.modeling().can_create_instance(); });
  register_command(
      quader_command::kEditFlattenInstance, "Flatten Instance",
      [this] { return api_.modeling().flatten_instance(); }, {},
      [this] { return api_.modeling().can_flatten_instance(); });
  register_command(
      quader_command::kEditExitInstanceEdit, "Exit Instance Edit",
      [this] { return api_.modeling().exit_instance_edit(); }, {},
      [this] { return api_.modeling().can_exit_instance_edit(); });
  register_command(
      quader_command::kGroupSelection, "Group Selection",
      [this] {
        const QuaderApiResult<QuaderEntityRef> result =
            api_.scene().group_selection();
        return quader_api_status_from_result(result);
      },
      "Ctrl+G");
  register_command(
      quader_command::kUngroupSelection, "Ungroup Selection",
      [this] { return api_.scene().ungroup_selection(); }, "Shift+Ctrl+G");
  register_command(
      quader_command::kParentToActive, "Parent to Active",
      [this] { return api_.scene().parent_selection_to_active(); }, "P",
      [this] { return api_.scene().can_parent_selection_to_active(); });
  register_command(
      quader_command::kUnparentSelection, "Unparent",
      [this] { return api_.scene().unparent_selection(); }, "Ctrl+P",
      [this] { return api_.scene().can_unparent_selection(); });
  set_keybinding_selection_modes(quader_command::kParentToActive.string(),
                                 kSelectionModeObject | kSelectionModeGroup);
  set_keybinding_selection_modes(quader_command::kUnparentSelection.string(),
                                 kSelectionModeObject | kSelectionModeGroup);
  register_command(
      quader_command::kModelingSnapMeshesToFloor,
      "Snap Meshes to Floor",
      [this] { return api_.modeling().snap_selected_meshes_to_floor(); }, {},
      [this] {
        return api_.modeling().can_snap_selected_meshes_to_floor();
      });
  set_keybinding_selection_modes(
      quader_command::kModelingSnapMeshesToFloor.string(),
      kSelectionModeObject);
  // Baking is deliberately unassigned by default and remains user-bindable
  // only in Mesh/Object selection mode.
  register_command(
      quader_command::kModelingBakeTransforms, "Bake Transforms",
      [this] {
        return api_.modeling().bake_transforms(
            QuaderTransformBakeMode::Transforms);
      },
      {}, [this] {
        return api_.modeling().can_bake_transforms(
            QuaderTransformBakeMode::Transforms);
      });
  set_keybinding_selection_modes(
      quader_command::kModelingBakeTransforms.string(), kSelectionModeObject);
  register_command(
      quader_command::kModelingBakeLocation, "Bake Location",
      [this] {
        return api_.modeling().bake_transforms(
            QuaderTransformBakeMode::Location);
      },
      {}, [this] {
        return api_.modeling().can_bake_transforms(
            QuaderTransformBakeMode::Location);
      });
  set_keybinding_selection_modes(
      quader_command::kModelingBakeLocation.string(), kSelectionModeObject);
  register_command(
      quader_command::kModelingBakeRotation, "Bake Rotation",
      [this] {
        return api_.modeling().bake_transforms(
            QuaderTransformBakeMode::Rotation);
      },
      {}, [this] {
        return api_.modeling().can_bake_transforms(
            QuaderTransformBakeMode::Rotation);
      });
  set_keybinding_selection_modes(
      quader_command::kModelingBakeRotation.string(), kSelectionModeObject);
  register_command(
      quader_command::kModelingBakeScale, "Bake Scale",
      [this] {
        return api_.modeling().bake_transforms(
            QuaderTransformBakeMode::Scale);
      },
      {}, [this] {
        return api_.modeling().can_bake_transforms(
            QuaderTransformBakeMode::Scale);
      });
  set_keybinding_selection_modes(
      quader_command::kModelingBakeScale.string(), kSelectionModeObject);
  register_command(
      quader_command::kModelingSnapVerticesToFloor,
      "Snap Vertices to Floor",
      [this] { return api_.modeling().snap_selected_vertices_to_floor(); }, {},
      [this] {
        return api_.modeling().can_snap_selected_vertices_to_floor();
      });
  set_keybinding_selection_modes(
      quader_command::kModelingSnapVerticesToFloor.string(),
      kSelectionModeVertex);
  register_command(
      quader_command::kModelingCopyPivotFromActive, "Copy Pivot From Active",
      [this] { return api_.modeling().copy_pivot_from_active(); }, {},
      [this] { return api_.modeling().can_copy_pivot_from_active(); });
  for (const FaceTextureCommand &command : kFaceTextureCommands) {
    register_command(
        command.command_id,
        std::string(quader_face_texture_action_label(command.action)),
        [this, action = command.action, mesh_level = command.mesh_level] {
          const auto settings = api_.modeling().face_texture_settings();
          const QuaderSelectionSummary selection = api_.selection().summary();
          const std::uint64_t selection_token =
              mesh_level && selection.mode == QuaderSelectionMode::Object
                  ? selection.selection_revision
                  : settings.selection_token;
          return api_.modeling().apply_face_texture_action(
              {.action = action, .selection_token = selection_token});
        },
        {}, [this, mesh_level = command.mesh_level] {
          if (mesh_level) {
            return api_.modeling().can_project_uv();
          }
          return api_.modeling().face_texture_settings().editable;
        });
    set_keybinding_selection_modes(command.command_id.string(),
                                   command.mesh_level
                                       ? kSelectionModeObject | kSelectionModeFace
                                       : kSelectionModeFace);
  }
#ifndef NDEBUG
  register_command(quader_command::kDevCopySelectionJson,
                   "Dev: Copy Selection JSON",
                   [this] {
                     return dev_tools_.copy_selection_json_to_clipboard();
                   },
                   "Ctrl+Shift+C");
#endif
  register_command(
      quader_command::kModelingApplyActivePreview, "Apply Active Preview",
      [this] {
        return execute_modeling_preview_intent(QuaderNativeToolSettingsIntent::Apply);
      });
  register_command(quader_command::kModelingCancelActivePreview,
                   "Cancel Active Preview",
                   [this] {
                     return execute_modeling_preview_intent(
                         QuaderNativeToolSettingsIntent::Cancel);
                   });
  register_command(quader_command::kToolsApplyPrimitiveCreation,
                   "Finalize Tool",
                   [this] {
                     return tools_.has_active_primitive_creation()
                                ? tools_.commit_active_tool_preview()
                                : failure(
                                      "No primitive creation session is active.");
                   },
                   {}, [this] {
                     return tools_.has_active_primitive_creation();
                   });
  register_command(quader_command::kToolsCancelPrimitiveCreation,
                   "Cancel Tool",
                   [this] {
                     return tools_.has_active_primitive_creation()
                                ? tools_.cancel_active_tool_preview()
                                : failure(
                                      "No primitive creation session is active.");
                   },
                   {}, [this] {
                     return tools_.has_active_primitive_creation();
                   });
  register_command(quader_command::kToolsFlipPrimitiveFaces,
                   "Flip Primitive Faces",
                   [this] { return tools_.flip_primitive_faces(); }, "Shift+F",
                   [this] {
                     return !tools_.active_primitive_type().empty();
                   });
  register_command(quader_command::kToolsTogglePrimitiveCaps,
                   "Toggle Primitive Caps",
                   [this] { return tools_.toggle_primitive_caps(); }, "C",
                   [this] {
                     const std::string kind = tools_.active_primitive_type();
                     return kind == "cylinder" || kind == "cone";
                   });
  register_command(quader_command::kToolsTogglePrimitiveHemisphere,
                   "Toggle Primitive Hemisphere",
                   [this] { return tools_.toggle_primitive_hemisphere(); }, "H",
                   [this] {
                     return tools_.active_primitive_type() == "sphere";
                   });
  register_command(
      quader_command::kProjectNew, "New Project",
      [this] { return project_.new_project(); });
  register_command(
      quader_command::kProjectOpen, "Open Project",
      [this] { return project_.open({}); });
  register_command(
      quader_command::kProjectClose, "Close Project",
      [this] { return project_.close(); }, {},
      [this] { return project_.has_project(); });
  register_command(
      quader_command::kSceneDocumentNew, "New Scene",
      [this] {
        return quader_api_status_from_result(scene_documents_.new_scene());
      },
      "Ctrl+N", [this] { return project_.has_project(); });
  register_command(
      quader_command::kSceneDocumentOpen, "Open Scene",
      [this] {
        return quader_api_status_from_result(scene_documents_.open());
      },
      "Ctrl+O", [this] { return project_.has_project(); });
  register_command(
      quader_command::kSceneDocumentSave, "Save Scene",
      [this] { return scene_documents_.save_active(); }, "Ctrl+S",
      [this] { return scene_documents_.has_active_scene(); });
  register_command(
      quader_command::kSceneDocumentSaveAs, "Save Scene As",
      [this] {
        const std::optional<SceneSessionId> id =
            scene_documents_.active_scene_id();
        return id.has_value()
                   ? scene_documents_.save_as(*id)
                   : failure("No scene is active.");
      },
      "Ctrl+Alt+S", [this] { return scene_documents_.has_active_scene(); });
  register_command(
      quader_command::kSceneDocumentSaveAll, "Save All Scenes",
      [this] { return scene_documents_.save_all(); }, "Ctrl+Shift+S",
      [this] { return scene_documents_.has_active_scene(); });
  register_command(
      quader_command::kSceneDocumentClose, "Close Scene",
      [this] { return scene_documents_.close_active(); }, "Ctrl+W",
      [this] { return scene_documents_.has_active_scene(); });
  register_command(quader_command::kSceneDocumentNext, "Next Scene",
                   [this] { return scene_documents_.activate_next(); },
                   "Ctrl+Tab",
                   [this] { return scene_documents_.open_scenes().size() > 1u; });
  register_command(
      quader_command::kSceneDocumentPrevious, "Previous Scene",
      [this] { return scene_documents_.activate_previous(); },
      "Ctrl+Shift+Tab",
      [this] { return scene_documents_.open_scenes().size() > 1u; });
  register_command(
      quader_command::kProjectExportSelectionObj, "Export Selection OBJ",
      [this] {
        return quader_api_status_from_result(
            project_.export_selected_meshes_as_obj({}));
      },
      {}, [this] { return project_.can_export_selected_meshes_as_obj(); });
  register_command(quader_command::kSceneCreatePointLight, "Create Point Light",
                   [this] {
                     return tools_.begin_light_creation(
                         QuaderLightType::Point);
                   },
                   {}, [this] { return scene_documents_.has_active_scene(); });
  register_command(quader_command::kSceneCreateDirectionalLight,
                   "Create Directional Light", [this] {
                     return tools_.begin_light_creation(
                         QuaderLightType::Directional);
                   },
                   {}, [this] { return scene_documents_.has_active_scene(); });
  register_command(
      quader_command::kSceneCreateEmpty, "Create Empty",
      [this] {
        return quader_api_status_from_result(api_.scene().create_empty());
      },
      {}, [this] { return scene_documents_.has_active_scene(); });
  const auto register_primitive_creation_command =
      [this](QuaderCommandId command_id, std::string label,
             QuaderPrimitiveCreationShape shape, std::string shortcut = {}) {
        register_command(
            command_id, std::move(label),
            [this, shape] { return tools_.begin_primitive_creation(shape); },
            std::move(shortcut), [this] {
              return scene_documents_.has_active_scene() &&
                     tools_.can_activate(ViewportToolId::CreatePrimitive);
            });
      };
  register_primitive_creation_command(
      quader_command::kToolsActivateCreateBox, "Create Box",
      QuaderPrimitiveCreationShape::Box, "Shift+B");
  register_primitive_creation_command(
      quader_command::kToolsActivateCreateCylinder, "Create Cylinder",
      QuaderPrimitiveCreationShape::Cylinder, "Shift+C");
  register_primitive_creation_command(
      quader_command::kToolsActivateCreateSphere, "Create Sphere",
      QuaderPrimitiveCreationShape::Sphere, "Shift+S");
  // Cone and Torus stay bindable without claiming a default shortcut.
  register_primitive_creation_command(
      quader_command::kToolsActivateCreateTorus, "Create Torus",
      QuaderPrimitiveCreationShape::Torus);
  register_primitive_creation_command(
      quader_command::kToolsActivateCreateCone, "Create Cone",
      QuaderPrimitiveCreationShape::Cone);
  // Preserve old assignments and deliberate removals as aliases of Create Box.
  // The presentation catalog keeps this compatibility target out of menus.
  register_primitive_creation_command(
      quader_command::kToolsActivateCreatePrimitive, "Create Box",
      QuaderPrimitiveCreationShape::Box);
  set_keybinding_action_id(
      quader_command::kToolsActivateCreatePrimitive.string(),
      std::string(quader_command::kToolsActivateCreateBox.string()));
  register_command(
      quader_command::kToolsActivateCreateCurve, "Create Curve",
      [this] { return tools_.begin_new_curve_creation(); }, {},
      [this] {
        return scene_documents_.has_active_scene() &&
               tools_.can_begin_new_curve_creation();
      });
  const auto register_extrude_mode_command =
      [this](QuaderCommandId command_id, std::string label,
             std::string shortcut, QuaderExtrudeMode mode) {
        register_command(
            command_id, std::move(label),
            [this, mode] { return tools_.activate_extrude(mode); },
            std::move(shortcut), [this] {
              return scene_documents_.has_active_scene() &&
                     tools_.can_activate(ViewportToolId::Extrude);
            });
      };
  register_extrude_mode_command(
      quader_command::kToolsActivateExtrudeIndividual, "Extrude Individually",
      "Ctrl+E", QuaderExtrudeMode::Individual);
  register_extrude_mode_command(
      quader_command::kToolsActivateExtrudeAlongNormals, "Extrude Along Normals",
      "Alt+E", QuaderExtrudeMode::AlongNormals);
  register_command(
      quader_command::kSceneHideSelection, "Hide Selection",
      [this] {
        std::vector<QuaderSceneVisibilityEdit> edits;
        for (const QuaderEntitySnapshot &snapshot : api_.query().entities()) {
          if (snapshot.selected &&
              snapshot.visible) {
            edits.push_back({
                .entity = snapshot.id,
                .before = true,
                .after = false,
            });
          }
        }
        QuaderApiVoidResult result = api_.scene().set_visibility(edits);
        if (!result.ok()) {
          return result;
        }
        const QuaderApiVoidResult clear_result = api_.selection().clear();
        return clear_result.ok() ? result : clear_result;
      },
      "H", [this] {
        const QuaderSelectionMode mode = api_.query().selection().mode;
        if (mode != QuaderSelectionMode::Object &&
            mode != QuaderSelectionMode::Group) {
          return false;
        }
        const std::vector<QuaderEntitySnapshot> entities =
            api_.query().entities();
        return std::ranges::any_of(entities, [](const auto &snapshot) {
          return snapshot.selected &&
                 snapshot.visible;
        });
      });
  set_keybinding_selection_modes(
      quader_command::kSceneHideSelection.string(),
      kSelectionModeObject | kSelectionModeGroup);
  register_command(
      quader_command::kSceneUnhideHiddenObjects, "Unhide Hidden Objects",
      [this] {
        std::vector<QuaderSceneVisibilityEdit> edits;
        for (const QuaderEntitySnapshot &snapshot : api_.query().entities()) {
          if (!snapshot.visible) {
            edits.push_back({
                .entity = snapshot.id,
                .before = false,
                .after = true,
            });
          }
        }
        return api_.scene().set_visibility(edits);
      },
      "Alt+H", [this] {
        const QuaderSelectionMode mode = api_.query().selection().mode;
        if (mode != QuaderSelectionMode::Object &&
            mode != QuaderSelectionMode::Group) {
          return false;
        }
        const std::vector<QuaderEntitySnapshot> entities =
            api_.query().entities();
        return std::ranges::any_of(entities, [](const auto &snapshot) {
          return !snapshot.visible;
        });
      });
  set_keybinding_selection_modes(
      quader_command::kSceneUnhideHiddenObjects.string(),
      kSelectionModeObject | kSelectionModeGroup);
  register_command(
      quader_command::kSceneLayersRemoveSelection,
      "Remove Objects From Layer",
      [this] { return api_.layers().remove_selection_from_all_layers(); }, {},
      [this] {
        return api_.layers().can_remove_selection_from_all_layers();
      });
  register_command(
      quader_command::kViewportFrameSelection, "Focus Selection",
      [this] { return viewports_.frame_selection(viewports_.focused_pane()); });
  register_command(quader_command::kViewportFocusAll, "Focus All",
                   [this] {
                     return viewports_.frame_all(viewports_.focused_pane());
                   },
                   "Home");
  register_command(
      quader_command::kViewportFocusOrigin, "Focus Origin",
      [this] { return viewports_.focus_origin(viewports_.focused_pane()); });
  register_command(
      quader_command::kViewportSpawnWindow, "New Viewport Window",
      [this] { return viewports_.spawn_window(); }, {},
      [this] { return viewports_.can_spawn_window(); });
  register_command(
      quader_command::kViewportToggleLocalVisibility,
      "Toggle Local Visibility",
      [this] { return viewports_.toggle_local_visibility(); }, "Backslash",
      [this] { return viewports_.can_toggle_local_visibility(); });
  register_command(
      quader_command::kViewportToggleTransformSpace,
      "Toggle Global/Local Transforms",
      [this] { return viewports_.toggle_transform_space(); }, "T");
  register_viewport_view_command(quader_command::kViewportViewPerspective,
                                 "Perspective View",
                                 ViewportView::Perspective, "F4");
  register_viewport_view_command(quader_command::kViewportViewTop, "Top View",
                                 ViewportView::Top, "F1");
  register_viewport_view_command(quader_command::kViewportViewBottom,
                                 "Bottom View", ViewportView::Bottom);
  register_viewport_view_command(quader_command::kViewportViewLeft, "Left View",
                                 ViewportView::Left);
  register_viewport_view_command(quader_command::kViewportViewRight,
                                 "Side View", ViewportView::Side, "F2");
  register_viewport_view_command(quader_command::kViewportViewFront,
                                 "Front View", ViewportView::Front, "F3");
  register_viewport_view_command(quader_command::kViewportViewBack, "Back View",
                                 ViewportView::Back);
  register_command(
      quader_command::kViewportShadingWireframe, "Wireframe",
      [this] {
        return viewports_.set_shading_mode(viewports_.focused_pane(),
                                           QuaderShadingMode::Wireframe);
      },
      "F5");
  register_command(
      quader_command::kViewportShadingSolid, "Solid",
      [this] {
        return viewports_.set_shading_mode(viewports_.focused_pane(),
                                           QuaderShadingMode::Solid);
      });
  register_command(
      quader_command::kViewportShadingShaded, "Shaded",
      [this] {
        return viewports_.set_shading_mode(viewports_.focused_pane(),
                                           QuaderShadingMode::Shaded);
      },
      "F6");
  register_command(
      quader_command::kViewportShadingRendered, "Rendered",
      [this] {
        return viewports_.set_shading_mode(viewports_.focused_pane(),
                                           QuaderShadingMode::Rendered);
      },
      "F7");
  register_command(
      quader_command::kViewportLayoutSingle, "Single Viewport",
      [this] { return viewports_.set_layout(ViewportLayoutId::Single); },
      "Ctrl+1");
  register_command(
      quader_command::kViewportLayoutTwoVertical, "Two Vertical Viewports",
      [this] { return viewports_.set_layout(ViewportLayoutId::TwoVertical); },
      "Ctrl+2");
  register_command(
      quader_command::kViewportLayoutTwoHorizontal,
      "Two Horizontal Viewports",
      [this] { return viewports_.set_layout(ViewportLayoutId::TwoHorizontal); },
      "Ctrl+3");
  register_command(
      quader_command::kViewportLayoutThreePrimaryRight,
      "Three Viewports, Primary Right",
      [this] {
        return viewports_.set_layout(ViewportLayoutId::ThreePrimaryRight);
      });
  register_command(
      quader_command::kViewportLayoutThreePrimaryLeft,
      "Three Viewports, Primary Left",
      [this] {
        return viewports_.set_layout(ViewportLayoutId::ThreePrimaryLeft);
      });
  register_command(
      quader_command::kViewportLayoutThreePrimaryBottom,
      "Three Viewports, Primary Bottom",
      [this] {
        return viewports_.set_layout(ViewportLayoutId::ThreePrimaryBottom);
      });
  register_command(
      quader_command::kViewportLayoutThreePrimaryTop,
      "Three Viewports, Primary Top",
      [this] {
        return viewports_.set_layout(ViewportLayoutId::ThreePrimaryTop);
      });
  register_command(
      quader_command::kViewportLayoutFourQuad, "Four Viewports",
      [this] { return viewports_.set_layout(ViewportLayoutId::FourQuad); },
      "Ctrl+4");
  register_command(quader_command::kViewportToggleGrid, "Toggle Grid",
                   [this] {
                     return viewports_.set_control(
                         QuaderViewportControl::ShowGrid,
                         !viewports_.control(QuaderViewportControl::ShowGrid));
                   });
  register_command(quader_command::kViewportToggleGizmos, "Toggle Gizmos",
                   [this] {
                     return viewports_.set_control(
                         QuaderViewportControl::ShowGizmos,
                         !viewports_.control(QuaderViewportControl::ShowGizmos));
                   });
  register_command(quader_command::kViewportToggleMeshGrid,
                   "Toggle Mesh Grid", [this] {
                     return viewports_.set_control(
                         QuaderViewportControl::ShowMeshGrid,
                         !viewports_.control(QuaderViewportControl::ShowMeshGrid));
                   });
  register_command(quader_command::kViewportToggleOverlays,
                   "Toggle Overlays", [this] {
                     return viewports_.set_control(
                         QuaderViewportControl::ShowOverlays,
                         !viewports_.control(QuaderViewportControl::ShowOverlays));
                   });
  // These display switches are bindable commands, deliberately unassigned by
  // default. Menus and other retained controls dispatch through these routes.
  register_command(quader_command::kViewportToggleDimensions,
                   "Toggle Dimensions", [this] {
                     return preferences_.set_dimensions_visible(
                         !preferences_.dimensions_visible());
                   });
  register_command(quader_command::kViewportToggleCollisions,
                   "Collisions", [this] {
                     return preferences_.set_collisions_visible(
                         !preferences_.collisions_visible());
                   });
  register_command(quader_command::kViewportToggleInfo,
                   "Toggle Viewport Info", [this] {
                     return preferences_.set_viewport_info_visible(
                         !preferences_.viewport_info_visible());
                   });
  // Texture-lock switches are bindable viewport actions, deliberately unassigned
  // by default so adding the controls does not claim existing user shortcuts.
  register_command(quader_command::kViewportToggleTextureLock,
                   "Texture Lock", [this] {
                     return preferences_.set_texture_lock_enabled(
                         !preferences_.texture_lock_enabled());
                   });
  register_command(quader_command::kViewportToggleTextureLockScale,
                   "Texture Lock Scale", [this] {
                     return preferences_.set_texture_lock_scale_enabled(
                         !preferences_.texture_lock_scale_enabled());
                   });
  register_command(
      quader_command::kViewportGridIncrement, "Increase Grid Size",
      [this] {
        return viewports_.set_grid_preset(viewports_.grid_preset() + 1);
      },
      "Alt+WheelUp", [this] {
        return viewports_.grid_preset() <
               static_cast<int>(viewport_grid_preset_catalog().size());
      });
  register_command(
      quader_command::kViewportGridDecrement, "Decrease Grid Size",
      [this] {
        return viewports_.set_grid_preset(viewports_.grid_preset() - 1);
      },
      "Alt+WheelDown", [this] { return viewports_.grid_preset() > 1; });

  for (const QuaderMeshArrangementDescriptor &descriptor :
       quader_mesh_arrangement_catalog()) {
    register_command(
        descriptor.command_id, arrangement_command_label(descriptor),
        [this, descriptor] {
          const QuaderMeshArrangementOptions options{
              .snap_to_grid =
                  viewports_.control(QuaderViewportControl::GridSnap),
              .grid_size = viewport_grid_world_size_for_preset(
                  viewports_.grid_preset()),
          };
          return descriptor.kind == QuaderMeshArrangementKind::Alignment
                     ? api_.modeling().align_selected_meshes(
                           descriptor.axis, descriptor.alignment, options)
                     : api_.modeling().distribute_selected_meshes(
                           descriptor.axis, descriptor.distribution, options);
        },
        {}, [this, kind = descriptor.kind] {
          return kind == QuaderMeshArrangementKind::Alignment
                     ? api_.modeling().can_align_selected_meshes()
                     : api_.modeling().can_distribute_selected_meshes();
        });
  }

  for (const CommandTool *tool : modeling_command_tools()) {
    if (tool == nullptr) {
      continue;
    }
    const std::string command_id = command_id_for_modeling_action(tool->id());
    if (!modeling_action_registers_dedicated_command(*tool)) {
      continue;
    }
    register_command(
        command_id, std::string(tool->label()),
        [this, tool_id = std::string(tool->id())] {
          return api_.modeling().execute_tool(tool_id);
        },
        tool->id() == "bridge" ? "B" : "",
        [this, tool_id = std::string(tool->id())] {
          return api_.modeling().can_execute_tool(tool_id);
        });
    if (tool->kind() != CommandToolKind::Preview &&
        !tool->operation_id().empty()) {
      set_keybinding_action_id(
          command_id,
          "modeling.operation." + std::string(tool->operation_id()));
    } else if (tool->id() == "interpolated_bridge_edges" ||
               tool->id() == "interpolated_bridge_faces") {
      set_keybinding_action_id(command_id,
                               "modeling.preview.interpolated_bridge");
    }
  }
  set_keybinding_selection_modes(
      quader_command::kModelingFlipHorizontal.string(),
      kSelectionModeObject | kSelectionModeGroup);
  for (const ViewportTool *tool : viewport_tool_catalog()) {
    if (tool == nullptr ||
        tool->viewport_tool() == ViewportToolId::CreatePrimitive) {
      continue;
    }
    const std::string activation_command =
        command_id_for_tool_activation(tool->id());
    register_command(
        activation_command, std::string(tool->label()),
        [this, tool_id = std::string(tool->id())] {
          return tools_.activate(tool_id);
        },
        std::string(tool->shortcut()),
        [this, tool_kind = tool->viewport_tool()] {
          return viewport_tool_available(tool_kind) &&
                 tools_.can_activate(tool_kind);
        });
    if (tool->viewport_tool() == ViewportToolId::Knife ||
        tool->viewport_tool() == ViewportToolId::UvPeel) {
      // Peel activates before guides are selected and intentionally has no
      // default shortcut; existing assignments keep their canonical command.
      set_keybinding_selection_modes(activation_command, kSelectionModeAny);
    } else if (tool->viewport_tool() == ViewportToolId::PathArray) {
      // Path Array starts from one selected Curve, which is exposed through
      // the Mesh/Object selection scope. The command remains user-bindable
      // while deliberately shipping without a default shortcut.
      set_keybinding_selection_modes(activation_command, kSelectionModeObject);
    } else {
      set_keybinding_selection_modes_for_modeling_action(activation_command,
                                                         tool->id());
    }
  }
}

void QuaderCommandsApi::Impl::register_viewport_view_command(
    QuaderCommandId id, std::string label, ViewportView view,
    std::string shortcut) {
  register_command(
      id, std::move(label),
      [this, view] {
        return viewports_.set_view(viewports_.focused_pane(), view);
      },
      std::move(shortcut));
}

void QuaderCommandsApi::Impl::register_default_keybindings() {
  constexpr std::string_view kOperationPreviewContext =
      "(viewportFocus || outlinerFocus || editorFocus) && !textInputActive && "
      "!navigation.active && operationPreview.active";
  constexpr std::string_view kPrimitiveToolContext =
      "(viewportFocus || outlinerFocus || editorFocus) && !textInputActive && "
      "!navigation.active && activeTool.create_primitive";
  register_keybinding_target_metadata(
      std::string(quader_command::kModelingApplyActivePreview.string()),
      "Apply Active Preview",
      stable_keybinding_localization_key(
          quader_command::kModelingApplyActivePreview.string()),
      QuaderKeybindingContextKind::OperationPreview,
      std::string(kOperationPreviewContext));
  register_keybinding_target_metadata(
      std::string(quader_command::kModelingCancelActivePreview.string()),
      "Cancel Active Preview",
      stable_keybinding_localization_key(
          quader_command::kModelingCancelActivePreview.string()),
      QuaderKeybindingContextKind::OperationPreview,
      std::string(kOperationPreviewContext));
  register_keybinding_target_metadata(
      std::string(quader_command::kToolsApplyPrimitiveCreation.string()),
      "Finalize Tool", "keybindings.action.tools.finalize.label",
      QuaderKeybindingContextKind::ModalTool,
      std::string(kPrimitiveToolContext), {}, "tools.finalize");
  register_keybinding_target_metadata(
      std::string(quader_command::kToolsCancelPrimitiveCreation.string()),
      "Cancel Tool", "keybindings.action.tools.cancel.label",
      QuaderKeybindingContextKind::ModalTool,
      std::string(kPrimitiveToolContext), {}, "tools.cancel");
  register_keybinding_target_metadata(
      std::string(quader_command::kToolsFlipPrimitiveFaces.string()),
      "Flip Primitive Faces",
      stable_keybinding_localization_key(
          quader_command::kToolsFlipPrimitiveFaces.string()),
      QuaderKeybindingContextKind::ModalTool,
      std::string(primitive_flip_keybinding_context()));
  register_keybinding_target_metadata(
      std::string(quader_command::kToolsTogglePrimitiveCaps.string()),
      "Toggle Primitive Caps",
      stable_keybinding_localization_key(
          quader_command::kToolsTogglePrimitiveCaps.string()),
      QuaderKeybindingContextKind::ModalTool,
      std::string(primitive_caps_keybinding_context()));
  register_keybinding_target_metadata(
      std::string(quader_command::kToolsTogglePrimitiveHemisphere.string()),
      "Toggle Primitive Hemisphere",
      stable_keybinding_localization_key(
          quader_command::kToolsTogglePrimitiveHemisphere.string()),
      QuaderKeybindingContextKind::ModalTool,
      std::string(primitive_hemisphere_keybinding_context()));

  for (const QuaderKeybindingTargetMetadata &metadata :
       keybinding_target_catalog_.entries()) {
    keybindings_.register_target(metadata.target_id);
    if (!metadata.default_shortcut.empty()) {
      keybindings_.add_default_keybinding(
          metadata.target_id, metadata.default_shortcut, metadata.when,
          default_keybinding_weight_for_command(metadata.target_id));
    }
  }

  register_viewport_default_keybindings();
  finalize_keybinding_scope_metadata();
  normalize_default_action_keybindings();
}

void QuaderCommandsApi::Impl::register_viewport_default_keybindings() {
  constexpr std::string_view kViewportContext =
      "viewportFocus && !textInputActive";
  constexpr std::string_view kOperationPreviewContext =
      "(viewportFocus || outlinerFocus || editorFocus) && !textInputActive && "
      "!navigation.active && operationPreview.active";
  constexpr std::string_view kPrimitiveToolContext =
      "(viewportFocus || outlinerFocus || editorFocus) && !textInputActive && "
      "!navigation.active && activeTool.create_primitive";

  register_keybinding_target_metadata(
      viewport_input_action_target_id("cancel_active_tool"),
      "Cancel Tool", "keybindings.action.tools.cancel.label",
      QuaderKeybindingContextKind::ModalTool,
      "viewportFocus && !textInputActive && !navigation.active && "
      "!operationPreview.active && !activeTool.create_primitive && "
      "(modalTool.active || activeTool.ownsViewport || "
      "transformDrag.active || activeTool.nativeLifecycle)",
      {}, "tools.cancel");
  register_keybinding_target_metadata(
      viewport_input_action_target_id("toggle_maximized"),
      "Toggle Maximized Viewport",
      stable_keybinding_localization_key("viewport.toggle_maximized"),
      QuaderKeybindingContextKind::Viewport,
      std::string(viewport_maximize_keybinding_context()));
  register_keybinding_target_metadata(
      viewport_input_action_target_id("delete_popup"), "Open Delete Menu",
      stable_keybinding_localization_key("viewport.delete_popup"),
      QuaderKeybindingContextKind::Viewport,
      std::string(viewport_command_keybinding_context()));
  register_keybinding_target_metadata(
      viewport_input_action_target_id("add_popup"), "Open Add Menu",
      stable_keybinding_localization_key("viewport.add_popup"),
      QuaderKeybindingContextKind::Viewport,
      std::string(viewport_command_keybinding_context()));
  register_keybinding_target_metadata(
      viewport_input_action_target_id("shading_popup"), "Open Shading Menu",
      stable_keybinding_localization_key("viewport.shading_popup"),
      QuaderKeybindingContextKind::Viewport,
      std::string(viewport_command_keybinding_context()));

  for (const ViewportInputActionBindingDescriptor &descriptor :
       viewport_input_action_binding_catalog()) {
    const bool fixed_navigation =
        descriptor.action == ViewportInputActionKind::Orbit ||
        descriptor.action == ViewportInputActionKind::OrbitSnap ||
        descriptor.action == ViewportInputActionKind::Pan;
    const std::string when = fixed_navigation
        ? "viewportFocus && !textInputActive && !navigation.flyActive"
        : (descriptor.when.empty() ? std::string(kViewportContext)
                                   : std::string(descriptor.when));
    register_keybinding_target_metadata(
        descriptor.target_id, std::string(descriptor.label),
        std::string(descriptor.localization_key),
        keybinding_context_kind_for_viewport_input(descriptor.context),
        when,
        {}, descriptor.action_id.empty() ? descriptor.target_id
                                         : std::string(descriptor.action_id));
    if (fixed_navigation) {
      keybindings_.lock_target(descriptor.target_id, when);
    }
    if (descriptor.selection_mode_mask != 0u) {
      keybinding_target_catalog_.set_selection_modes(
          descriptor.target_id, descriptor.selection_mode_mask,
          descriptor.when.empty() ? kViewportContext : descriptor.when);
    }
  }

  /// Stores the stable presentation metadata for one selection-mode target.
  struct SelectionModeKeybindingMetadata {
    std::string_view id;
    std::string_view label;
  };
  static constexpr std::array<SelectionModeKeybindingMetadata, 5>
      kSelectionModes{{
          {"vertex", "Switch to Vertex Selection Mode"},
          {"edge", "Switch to Edge Selection Mode"},
          {"face", "Switch to Face Selection Mode"},
          {"object", "Switch to Object Selection Mode"},
          {"group", "Switch to Group Selection Mode"},
      }};
  for (const SelectionModeKeybindingMetadata &mode : kSelectionModes) {
    const std::string target_id = viewport_selection_mode_target_id(mode.id);
    register_keybinding_target_metadata(
        target_id, std::string(mode.label),
        stable_keybinding_localization_key(target_id),
        QuaderKeybindingContextKind::Viewport,
        std::string(editor_command_keybinding_context()));
  }

  for (const ViewportGridPresetDescriptor &preset :
       viewport_grid_preset_catalog()) {
    const std::string target_id =
        viewport_grid_preset_target_id(preset.preset);
    std::string label = "Set Grid Size to " +
                        std::to_string(static_cast<int>(preset.map_units));
    if (preset.is_default) {
      label.append(" (Default)");
    }
    register_keybinding_target_metadata(
        target_id, std::move(label),
        stable_keybinding_localization_key(target_id),
        QuaderKeybindingContextKind::Viewport,
        std::string(editor_command_keybinding_context()));
  }

  keybindings_.add_default_keybinding(
      std::string(quader_command::kModelingApplyActivePreview.string()),
      "Space", std::string(kOperationPreviewContext), 20);
  keybindings_.add_default_keybinding(
      std::string(quader_command::kModelingCancelActivePreview.string()),
      "Escape", std::string(kOperationPreviewContext), 20);
  keybindings_.add_default_keybinding(
      std::string(quader_command::kToolsCancelPrimitiveCreation.string()),
      "Escape", std::string(kPrimitiveToolContext), 50);

  keybindings_.add_default_keybinding(
      viewport_input_action_target_id("cancel_active_tool"), "Escape",
      "viewportFocus && !textInputActive && !navigation.active && "
      "!operationPreview.active && !activeTool.create_primitive && "
      "(modalTool.active || activeTool.ownsViewport || "
      "transformDrag.active || activeTool.nativeLifecycle)",
      10);
  keybindings_.add_default_keybinding(
      viewport_input_action_target_id("toggle_maximized"), "Space",
      std::string(viewport_maximize_keybinding_context()), 10);
  keybindings_.add_default_keybinding(
      viewport_input_action_target_id("delete_popup"), "X",
      std::string(viewport_command_keybinding_context()), 10);
  keybindings_.add_default_keybinding(
      viewport_input_action_target_id("add_popup"), "Shift+A",
      std::string(viewport_command_keybinding_context()), 10);
  keybindings_.add_default_keybinding(
      viewport_input_action_target_id("shading_popup"), "Z",
      std::string(viewport_command_keybinding_context()), 10);
  for (const ViewportInputActionBindingDescriptor &descriptor :
       viewport_input_action_binding_catalog()) {
    const QuaderKeybindingTargetMetadata *metadata =
        keybinding_target_metadata(descriptor.target_id);
    keybindings_.add_default_keybinding(
        descriptor.target_id, descriptor.shortcut,
        metadata != nullptr
            ? metadata->when
            : (descriptor.when.empty() ? std::string(kViewportContext)
                                       : std::string(descriptor.when)),
        descriptor.weight);
  }

  for (const ViewportShortcutBindingDescriptor &descriptor :
       viewport_shortcut_binding_catalog()) {
    const std::optional<std::string> target_id =
        viewport_command_target_id_for_contract_target(descriptor.target_id);
    if (!target_id) {
      continue;
    }
    const QuaderKeybindingTargetMetadata *metadata =
        keybinding_target_metadata(*target_id);
    keybindings_.add_default_keybinding(
        *target_id, descriptor.shortcut,
        metadata != nullptr
            ? metadata->when
            : std::string(default_keybinding_context_for_contract_target(
                  descriptor.target_id)),
        10);
  }

  keybindings_.add_default_keybinding(
      viewport_selection_mode_target_id("vertex"), "1",
      std::string(editor_command_keybinding_context()), 5);
  keybindings_.add_default_keybinding(
      viewport_selection_mode_target_id("edge"), "2",
      std::string(editor_command_keybinding_context()), 5);
  keybindings_.add_default_keybinding(
      viewport_selection_mode_target_id("face"), "3",
      std::string(editor_command_keybinding_context()), 5);
  keybindings_.add_default_keybinding(
      viewport_selection_mode_target_id("object"), "4",
      std::string(editor_command_keybinding_context()), 5);
  keybindings_.add_default_keybinding(
      viewport_selection_mode_target_id("group"), "5",
      std::string(editor_command_keybinding_context()), 5);

  const std::vector<ViewportGridShortcutBindingDescriptor> presets =
      viewport_grid_shortcut_binding_catalog();
  for (const ViewportGridShortcutBindingDescriptor &preset : presets) {
    keybindings_.add_default_keybinding(
        viewport_grid_preset_target_id(preset.preset), preset.shortcut,
        std::string(editor_command_keybinding_context()), 10);
  }

}

void QuaderCommandsApi::Impl::register_command(
    QuaderCommandId id, std::string label,
    std::function<QuaderApiVoidResult()> execute, std::string shortcut,
    std::function<bool()> enabled, bool visible) {
  register_command(std::string(id.string()), std::move(label),
                   std::move(execute), std::move(shortcut), std::move(enabled),
                   visible);
}

void QuaderCommandsApi::Impl::register_command(
    std::string id, std::string label,
    std::function<QuaderApiVoidResult()> execute, std::string shortcut,
    std::function<bool()> enabled, bool visible) {
  const std::string metadata_id = id;
  const std::string metadata_label = explicit_keybinding_label(id, label);
  const std::string metadata_shortcut = shortcut;
  registry_.register_command(EditorCommandRegistration{
      .id = std::move(id),
      .label = std::move(label),
      .shortcut = std::move(shortcut),
      .visible = visible,
      .execute = std::move(execute),
      .enabled = std::move(enabled),
  });
  register_keybinding_target_metadata(
      metadata_id, metadata_label,
      command_keybinding_localization_key(metadata_id),
      default_keybinding_context_kind_for_command(metadata_id),
      std::string(default_keybinding_context_for_command(metadata_id)),
      metadata_shortcut);
  if (const std::optional<std::string_view> action_id =
          quader_command_target_suffix(metadata_id,
                                       kQuaderModelingCommandTargetPrefix)) {
    set_keybinding_selection_modes_for_modeling_action(metadata_id,
                                                       *action_id);
  }
}

void QuaderCommandsApi::Impl::register_keybinding_target_metadata(
    std::string target_id, std::string label, std::string localization_key,
    QuaderKeybindingContextKind context, std::string when,
    std::string default_shortcut, std::string action_id) {
  // Deliberately unassigned targets are still bindable, including viewport
  // metadata registered after the compiled command-default pass.
  keybindings_.register_target(target_id);
  keybinding_target_catalog_.register_target(
      std::move(target_id), std::move(label), std::move(localization_key),
      context, std::move(when), std::move(default_shortcut),
      std::move(action_id));
}

void QuaderCommandsApi::Impl::set_keybinding_action_id(
    std::string_view target_id, std::string action_id) {
  keybinding_target_catalog_.set_action_id(target_id, std::move(action_id));
}

void QuaderCommandsApi::Impl::set_keybinding_selection_modes(
    std::string_view target_id, std::uint32_t mode_mask) {
  keybinding_target_catalog_.set_selection_modes(
      target_id, mode_mask,
      (command_semantically_requires_viewport(target_id) ||
       is_primitive_caps_conflicting_tool(target_id))
          ? default_keybinding_context_for_command(target_id)
          : editor_command_keybinding_context());
}

void QuaderCommandsApi::Impl::set_keybinding_selection_modes_for_modeling_action(
    std::string_view target_id, std::string_view modeling_action_id) {
  std::uint32_t mode_mask = 0u;
  for (const ModelingInstantOperationCatalogEntry &entry :
       modeling_instant_operation_catalog()) {
    if (entry.id == modeling_action_id ||
        entry.operation_id == modeling_action_id) {
      mode_mask |= selection_mode_mask(entry.availability);
    }
  }
  for (const ModelingSelectionMode mode :
       ModelingActionCatalog::selection_modes_for_action(
           modeling_action_id)) {
    mode_mask |= selection_mode_mask(mode);
  }
  set_keybinding_selection_modes(target_id, mode_mask);
}

void QuaderCommandsApi::Impl::finalize_keybinding_scope_metadata() {
  keybinding_target_catalog_.finalize_scope_metadata();
}

void QuaderCommandsApi::Impl::normalize_default_action_keybindings() {
  const std::vector<QuaderKeybindingCatalogEntry> catalog =
      keybinding_catalog();
  const std::vector<QuaderKeybindingRule> defaults =
      keybindings_.default_keybindings();
  for (const QuaderKeybindingCatalogEntry &entry : catalog) {
    if (entry.default_shortcut.empty()) {
      continue;
    }
    int weight = 0;
    for (const QuaderKeybindingRule &rule : defaults) {
      if (std::find(entry.target_ids.begin(), entry.target_ids.end(),
                    rule.target_id) != entry.target_ids.end()) {
        weight = std::max(weight, rule.weight);
      }
    }
    for (const std::string &target_id : entry.target_ids) {
      const QuaderKeybindingTargetMetadata *metadata =
          keybinding_target_metadata(target_id);
      if (metadata != nullptr) {
        (void)keybindings_.set_default_keybinding(
            target_id, entry.default_shortcut, metadata->when, weight);
      }
    }
  }
}

void QuaderCommandsApi::Impl::normalize_loaded_user_keybindings() {
  const std::vector<QuaderKeybindingRule> loaded =
      keybindings_.user_keybindings();
  if (loaded.empty()) {
    return;
  }
  const std::vector<QuaderKeybindingCatalogEntry> catalog =
      keybinding_catalog();
  /// Points from one grouped catalog action to its latest legacy override.
  struct LoadedGroupOverride {
    const QuaderKeybindingCatalogEntry *entry = nullptr;
    const QuaderKeybindingRule *rule = nullptr;
  };
  std::vector<LoadedGroupOverride> overrides;
  for (const QuaderKeybindingCatalogEntry &entry : catalog) {
    const QuaderKeybindingRule *latest = nullptr;
    for (const QuaderKeybindingRule &rule : loaded) {
      if (std::find(entry.target_ids.begin(), entry.target_ids.end(),
                    rule.target_id) == entry.target_ids.end()) {
        continue;
      }
      if (latest == nullptr || latest->order < rule.order) {
        latest = &rule;
      }
    }
    if (latest != nullptr) {
      overrides.push_back({.entry = &entry, .rule = latest});
    }
  }
  std::stable_sort(overrides.begin(), overrides.end(),
                   [](const LoadedGroupOverride &left,
                      const LoadedGroupOverride &right) {
                     return left.rule->order < right.rule->order;
                   });

  std::vector<QuaderKeybindingRule> canonical;
  for (const LoadedGroupOverride &override : overrides) {
    if (!override.rule->removal) {
      const std::optional<std::vector<QuaderInputChord>> parsed =
          quader_input_parse_shortcut(override.rule->shortcut);
      if (!parsed || material_assignment_uses_reserved_bare_right_mouse(
                         override.entry->key, *parsed) ||
          transform_vertex_snap_assignment_uses_non_holdable_shortcut(
              override.entry->key, *parsed)) {
        continue;
      }
    }
    write_action_override(canonical, *override.entry,
                          override.rule->removal
                              ? std::string_view{}
                              : std::string_view(override.rule->shortcut));
  }
  (void)keybindings_.adopt_loaded_user_keybindings(std::move(canonical));
}

const QuaderKeybindingTargetMetadata *
QuaderCommandsApi::Impl::keybinding_target_metadata(
    std::string_view target_id) const {
  return keybinding_target_catalog_.find(target_id);
}

QuaderApiVoidResult QuaderCommandsApi::Impl::failure(std::string message) const {
  return {
      .ok = false,
      .changed = false,
      .message = std::move(message),
      .mutation_kind = QuaderMutationKind::None,
      .revisions = api_.revisions(),
  };
}

#ifndef NDEBUG
QuaderCommandsApi::QuaderCommandsApi(QuaderApi &api,
                                     QuaderProjectApi &project,
                                     QuaderSceneDocumentsApi &scene_documents,
                                     QuaderViewportApi &viewports,
                                     QuaderToolsApi &tools,
                                     QuaderPreferencesApi &preferences,
                                     QuaderUIApi &ui,
                                     QuaderDevToolsApi &dev_tools)
    : impl_(std::make_unique<Impl>(api, project, scene_documents, viewports,
                                   tools, preferences, ui, dev_tools)) {}
#else
QuaderCommandsApi::QuaderCommandsApi(QuaderApi &api,
                                     QuaderProjectApi &project,
                                     QuaderSceneDocumentsApi &scene_documents,
                                     QuaderViewportApi &viewports,
                                     QuaderToolsApi &tools,
                                     QuaderPreferencesApi &preferences,
                                     QuaderUIApi &ui)
    : impl_(std::make_unique<Impl>(api, project, scene_documents, viewports,
                                   tools, preferences, ui)) {}
#endif

QuaderCommandsApi::~QuaderCommandsApi() = default;

QuaderApiVoidResult QuaderCommandsApi::execute(QuaderCommandId command_id) {
  return impl_->execute(command_id);
}

QuaderApiVoidResult QuaderCommandsApi::execute(std::string_view command_id) {
  return impl_->execute(command_id);
}

std::optional<QuaderApiStatus> QuaderCommandsApi::last_status() const {
    return impl_->last_status();
}

std::uint64_t QuaderCommandsApi::last_status_revision() const {
  return impl_->last_status_revision();
}

std::uint64_t QuaderCommandsApi::keybindings_revision() const noexcept {
  return impl_->keybindings_revision();
}

std::optional<QuaderCommandDescriptor>
QuaderCommandsApi::descriptor(std::string_view command_id) const {
  return impl_->descriptor(command_id);
}

std::optional<QuaderCommandDescriptor>
QuaderCommandsApi::descriptor(QuaderCommandId command_id) const {
  return impl_->descriptor(command_id.string());
}

std::vector<QuaderCommandDescriptor> QuaderCommandsApi::available() const {
    return impl_->available();
}

std::string QuaderCommandsApi::command_id_for_modeling_action(
    std::string_view action_id) const {
  return impl_->command_id_for_modeling_action(action_id);
}

std::string QuaderCommandsApi::command_id_for_tool_activation(
    std::string_view tool_id) const {
  return impl_->command_id_for_tool_activation(tool_id);
}

std::string QuaderCommandsApi::keybinding_target_id_for_selection_mode(
    QuaderSelectionMode mode) const {
  return impl_->keybinding_target_id_for_selection_mode(mode);
}

std::string QuaderCommandsApi::keybinding_target_id_for_grid_preset(
    int preset) const {
  return impl_->keybinding_target_id_for_grid_preset(preset);
}

std::string QuaderCommandsApi::shortcut_for(std::string_view command_id) const {
  return impl_->shortcut_for(command_id);
}

std::string QuaderCommandsApi::shortcut_for(QuaderCommandId command_id) const {
  return impl_->shortcut_for(command_id);
}

std::vector<QuaderKeybindingRule> QuaderCommandsApi::keybindings() const {
    return impl_->keybindings();
}

std::vector<QuaderKeybindingCatalogEntry>
QuaderCommandsApi::keybinding_catalog() const {
  auto catalog = impl_->keybinding_catalog();
  std::erase_if(catalog, [](const QuaderKeybindingCatalogEntry &entry) {
    return !release_allows_input_target(entry.target_id);
  });
  return catalog;
}

std::string QuaderCommandsApi::format_input_mapping(
    const QuaderInputChord &chord) const {
  return impl_->format_input_mapping(chord);
}

std::vector<QuaderKeybindingConflict>
QuaderCommandsApi::conflicts_for_shortcut(std::string_view shortcut,
                                          std::string_view when) const {
  return impl_->conflicts_for_shortcut(shortcut, when);
}

QuaderKeybindingAssignmentAnalysis
QuaderCommandsApi::analyze_shortcut_assignment(
    const QuaderKeybindingActionKey &action,
    std::string_view shortcut) const {
  return impl_->analyze_shortcut_assignment(action, shortcut);
}

bool QuaderCommandsApi::resolve_viewport_shortcut(
    ViewportKey key, ViewportModifiers modifiers,
    const ViewportToolState &state, bool viewport_focused,
    bool text_input_active, ViewportShortcut &shortcut,
    QuaderViewportKeybindingContext input_context) {
  return impl_->resolve_viewport_shortcut(key, modifiers, state,
                                          viewport_focused, text_input_active,
                                          shortcut, input_context);
}

bool QuaderCommandsApi::resolve_viewport_shortcut(
    const QuaderInputChord &chord, const ViewportToolState &state,
    bool viewport_focused, bool text_input_active, ViewportShortcut &shortcut,
    QuaderViewportKeybindingContext input_context) {
  return impl_->resolve_viewport_shortcut(chord, state, viewport_focused,
                                          text_input_active, shortcut,
                                          input_context);
}

bool QuaderCommandsApi::resolve_viewport_input_action(
    const QuaderInputChord &chord, const ViewportToolState &state,
    bool viewport_focused, bool text_input_active,
    ViewportInputActionKind &action,
    QuaderViewportKeybindingContext input_context) {
  return impl_->resolve_viewport_input_action(
      chord, state, viewport_focused, text_input_active, action, input_context);
}

QuaderViewportKeybindingDispatch
QuaderCommandsApi::dispatch_viewport_keybinding(
    ViewportKey key, ViewportModifiers modifiers,
    const ViewportToolState &state, bool viewport_focused,
    bool text_input_active, QuaderViewportKeybindingContext input_context) {
  return impl_->dispatch_viewport_keybinding(key, modifiers, state,
                                             viewport_focused,
                                             text_input_active, input_context);
}

QuaderViewportKeybindingDispatch
QuaderCommandsApi::dispatch_viewport_input_binding(
    const QuaderInputChord &chord, const ViewportToolState &state,
    bool viewport_focused, bool text_input_active,
    QuaderViewportKeybindingContext input_context) {
  return impl_->dispatch_viewport_input_binding(
      chord, state, viewport_focused, text_input_active, input_context);
}

std::optional<std::string>
QuaderCommandsApi::resolve_immediate_global_command(
    const QuaderInputChord &chord, const ViewportToolState &state,
    bool text_input_active,
    QuaderViewportKeybindingContext input_context) const {
  return impl_->resolve_immediate_global_command(
      chord, state, text_input_active, input_context);
}

QuaderNativeToolSettingsDispatch
QuaderCommandsApi::dispatch_native_tool_settings_input_binding(
    const QuaderInputChord &chord, const ViewportToolState &state) {
  return impl_->dispatch_native_tool_settings_input_binding(chord, state);
}

QuaderNativeToolSettingsDispatch
QuaderCommandsApi::resolve_native_tool_settings_input_binding(
    const QuaderInputChord &chord, const ViewportToolState &state) const {
  return impl_->resolve_native_tool_settings_input_binding(chord, state);
}

QuaderNativeToolSettingsDispatch
QuaderCommandsApi::resolve_modal_lifecycle_input_binding(
    const QuaderInputChord &chord) const {
  return impl_->resolve_modal_lifecycle_input_binding(chord);
}

QuaderNativeToolSettingsDispatch
QuaderCommandsApi::dispatch_modal_lifecycle_input_binding(
    const QuaderInputChord &chord) {
  return impl_->dispatch_modal_lifecycle_input_binding(chord);
}

void QuaderCommandsApi::reset_pending_modal_lifecycle_input_binding() {
  impl_->reset_pending_modal_lifecycle_input_binding();
}

QuaderApiVoidResult QuaderCommandsApi::execute_native_tool_settings_intent(
    QuaderNativeToolSettingsIntent intent) {
  return impl_->execute_native_tool_settings_intent(intent);
}

void QuaderCommandsApi::reset_pending_keybinding_chord() {
  impl_->reset_pending_keybinding_chord();
}

void QuaderCommandsApi::reset_pending_non_modal_keybinding_chord() {
  impl_->reset_pending_non_modal_keybinding_chord();
}

void QuaderCommandsApi::reset_pending_viewport_keybinding_chord() {
  impl_->reset_pending_viewport_keybinding_chord();
}

bool QuaderCommandsApi::has_pending_keybinding_chord() const {
  return impl_->has_pending_keybinding_chord();
}

QuaderApiVoidResult QuaderCommandsApi::set_shortcut(std::string_view command_id,
                                                    std::string shortcut) {
  return impl_->set_shortcut(command_id, std::move(shortcut));
}

QuaderApiVoidResult QuaderCommandsApi::set_shortcut(QuaderCommandId command_id,
                                                    std::string shortcut) {
  return impl_->set_shortcut(command_id, std::move(shortcut));
}

QuaderApiVoidResult
QuaderCommandsApi::remove_shortcut(std::string_view command_id) {
  return impl_->remove_shortcut(command_id);
}

QuaderApiVoidResult
QuaderCommandsApi::remove_shortcut(QuaderCommandId command_id) {
  return impl_->remove_shortcut(command_id);
}

QuaderApiVoidResult
QuaderCommandsApi::reset_shortcut(std::string_view command_id) {
  return impl_->reset_shortcut(command_id);
}

QuaderApiVoidResult
QuaderCommandsApi::reset_shortcut(QuaderCommandId command_id) {
  return impl_->reset_shortcut(command_id);
}

QuaderApiVoidResult QuaderCommandsApi::set_action_shortcut(
    const QuaderKeybindingActionKey &action,
    std::string shortcut,
    QuaderKeybindingConflictPolicy policy) {
  return impl_->set_action_shortcut(action, std::move(shortcut), policy);
}

QuaderApiVoidResult QuaderCommandsApi::remove_action_shortcut(
    const QuaderKeybindingActionKey &action) {
  return impl_->remove_action_shortcut(action);
}

QuaderApiVoidResult QuaderCommandsApi::reset_action_shortcut(
    const QuaderKeybindingActionKey &action,
    QuaderKeybindingConflictPolicy policy) {
  return impl_->reset_action_shortcut(action, policy);
}

QuaderApiVoidResult QuaderCommandsApi::reset_all_shortcuts() {
  return impl_->reset_all_shortcuts();
}

QuaderApiVoidResult
QuaderCommandsApi::set_keybinding_storage_path(std::filesystem::path path) {
  return impl_->set_keybinding_storage_path(std::move(path));
}

} // namespace quader

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_tools_api.hpp"
#include "api/quader_api_session_internal_access.hpp"

#include "api/quader_api_status_internal.hpp"
#include "editor/array/linear_array_tool_service.hpp"
#include "editor/array/radial_array_tool_service.hpp"
#include "editor/modeling/operations/operation_schema.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/modeling/services/modeling_operation_settings.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/pivot/pivot_tool_service.hpp"
#include "editor/tools/native_viewport_tool_coordinator.hpp"
#include "editor/modeling/services/primitive_creation_tool_service.hpp"
#include "editor/tools/viewport_tool_catalog.hpp"
#include "scene/scene_document.hpp"

#include <editor/modeling/runtime/tool_runtime_policy.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader {
namespace {

/// Identifies the owner whose active tool-settings revision is exposed.
enum class ActiveToolSettingsSourceKind : std::uint8_t {
  Modeling,
  Primitive,
  RadialArray,
  LinearArray,
  Pivot,
};

/// Couples a settings owner identity with its monotonic presentation revision.
struct ActiveToolSettingsSource {
  const void *identity = nullptr;
  std::uint64_t revision = 0;
  ActiveToolSettingsSourceKind kind = ActiveToolSettingsSourceKind::Modeling;
  // Captures owner presentation state that is intentionally independent from
  // its settings revision. Primitive placement freezes reset mid-gesture.
  bool reset_availability_token = false;
};

SceneSession &scene_session(QuaderApiSession session) {
  return QuaderApiSessionInternalAccess::scene_session(session);
}

MaterialReference active_project_material(QuaderApiSession session) {
  ProjectSession *project =
      QuaderApiSessionInternalAccess::project_session(session);
  if (project == nullptr) {
    return {};
  }
  const std::optional<ProjectAssetId> id = project->active_material_id();
  return id.has_value() ? MaterialReference::project(*id)
                        : MaterialReference{};
}

ActiveToolSettingsSource
active_tool_settings_source(const SceneSession &session) {
  const PrimitiveCreationToolService &primitive =
      session.primitive_creation();
  if (primitive.active()) {
    return {
        .identity = &primitive,
        .revision = primitive.tool_settings_revision(),
        .kind = ActiveToolSettingsSourceKind::Primitive,
        .reset_availability_token = primitive.can_reset_settings(),
    };
  }
  const RadialArrayToolService &radial_array = session.radial_array();
  if (radial_array.active()) {
    return {
        .identity = &radial_array,
        .revision = radial_array.tool_settings_revision(),
        .kind = ActiveToolSettingsSourceKind::RadialArray,
    };
  }
  const LinearArrayToolService &linear_array = session.linear_array();
  if (linear_array.active()) {
    return {
        .identity = &linear_array,
        .revision = linear_array.tool_settings_revision(),
        .kind = ActiveToolSettingsSourceKind::LinearArray,
    };
  }
  const PivotToolService &pivot = session.pivot();
  if (pivot.persistent_active()) {
    return {
        .identity = &pivot,
        .revision = pivot.tool_settings_revision(),
        .kind = ActiveToolSettingsSourceKind::Pivot,
    };
  }
  const ModelingSessionService &modeling = session.modeling();
  return {
      .identity = &modeling,
      .revision = modeling.tool_settings_revision(),
      .kind = ActiveToolSettingsSourceKind::Modeling,
  };
}

CommandResult runtime_tool_activation_preflight(
    const SceneSession &session, const ViewportTool &tool) {
  const quader_editor_core::ViewportInteractionTool requested_tool =
      quader_editor_core::viewport_interaction_tool_from_id(tool.id());
  if (requested_tool == quader_editor_core::ViewportInteractionTool::Unknown) {
    return command_invalid_result("Unknown viewport tool.");
  }
  if (!quader_editor_core::viewport_tool_requires_selected_objects(
          requested_tool)) {
    return command_unchanged_result();
  }

  const std::vector<EntityId> selected_entities =
      session.scene().selected_entities();
  std::size_t selected_object_count = 0;
  if (!selected_entities.empty()) {
    // Runtime activation promotes scene selection first. In particular, a
    // selected group must not inherit stale selected mesh objects from the
    // runtime session when deciding whether Cut or Mirror can activate.
    selected_object_count = static_cast<std::size_t>(std::count_if(
        selected_entities.begin(), selected_entities.end(),
        [&session](EntityId entity) {
          return session.scene().modeling_object(entity) != nullptr;
        }));
  } else {
    const ModelingSelectionExport exported =
        session.modeling().export_selection();
    selected_object_count = static_cast<std::size_t>(std::count_if(
        exported.objects.begin(), exported.objects.end(),
        [](const ModelingSelectionExportObject &object) {
          return object.selected;
        }));
  }
  const quader_editor_core::ViewportToolActivationRequest request{
      .requested_tool = requested_tool,
      // Cut and Mirror promote eligible scene/runtime object selection before
      // applying the runtime activation policy.
      .selection_mode = quader_editor_core::SelectionMode::Object,
      .selected_object_count = selected_object_count,
      .selected_cut_target_object_count = selected_object_count,
  };
  if (!quader_editor_core::viewport_tool_activation_has_selection_prerequisite(
          request)) {
    return command_invalid_result(std::string(
        quader_editor_core::viewport_tool_selection_prerequisite_message(
            requested_tool)));
  }
  return command_unchanged_result();
}

bool is_cut_mode_setting(std::string_view tool_id, std::string_view name) {
  const bool cut_tool = tool_id.empty() || tool_id == "cut";
  const bool cut_setting = name == "operation.cut.keep_mode" ||
                           name == "keep_mode" || name == "cut_mode";
  return cut_tool && cut_setting;
}

std::optional<ModelingCutMode> cut_mode_from_setting_value(double value) {
  switch (static_cast<int>(value)) {
  case static_cast<int>(ModelingCutMode::DiscardLeft):
    return ModelingCutMode::DiscardLeft;
  case static_cast<int>(ModelingCutMode::DiscardRight):
    return ModelingCutMode::DiscardRight;
  case static_cast<int>(ModelingCutMode::KeepBoth):
    return ModelingCutMode::KeepBoth;
  default:
    break;
  }
  return std::nullopt;
}

QuaderToolSettingControl toggle_control(std::string id, std::string label,
                                        bool value) {
  return {
      .kind = QuaderToolSettingControlKind::Toggle,
      .id = std::move(id),
      .label = std::move(label),
      .bool_value = value,
      .number_value = value ? 1.0 : 0.0,
  };
}

QuaderToolSettingControl number_control(std::string id, std::string label,
                                        double value, double min_value,
                                        double max_value, double step) {
  return {
      .kind = QuaderToolSettingControlKind::Number,
      .id = std::move(id),
      .label = std::move(label),
      .number_value = value,
      .min_value = min_value,
      .max_value = max_value,
      .step = step,
  };
}

QuaderToolSettingControl choice_control(
    std::string id, std::string label, std::string choice_value,
    std::vector<QuaderToolSettingChoiceOption> choices) {
  return {
      .kind = QuaderToolSettingControlKind::Choice,
      .id = std::move(id),
      .label = std::move(label),
      .choice_value = std::move(choice_value),
      .choices = std::move(choices),
  };
}

std::string_view primitive_kind_setting_id(PrimitiveCreationKind kind) {
  switch (kind) {
  case PrimitiveCreationKind::Box:
    return "box";
  case PrimitiveCreationKind::Cylinder:
    return "cylinder";
  case PrimitiveCreationKind::Sphere:
    return "sphere";
  case PrimitiveCreationKind::Cone:
    return "cone";
  case PrimitiveCreationKind::Torus:
    return "torus";
  }
  return {};
}

double primitive_kind_setting_value(PrimitiveCreationKind kind) {
  return static_cast<double>(static_cast<int>(kind));
}

std::optional<PrimitiveCreationKind>
primitive_kind_from_setting_id(std::string_view value) {
  if (value == "box") {
    return PrimitiveCreationKind::Box;
  }
  if (value == "cylinder") {
    return PrimitiveCreationKind::Cylinder;
  }
  if (value == "sphere") {
    return PrimitiveCreationKind::Sphere;
  }
  if (value == "cone") {
    return PrimitiveCreationKind::Cone;
  }
  if (value == "torus") {
    return PrimitiveCreationKind::Torus;
  }
  return std::nullopt;
}

std::optional<PrimitiveCreationKind>
primitive_kind_from_setting_value(double value) {
  if (!std::isfinite(value)) {
    return std::nullopt;
  }
  if (value == primitive_kind_setting_value(PrimitiveCreationKind::Box)) {
    return PrimitiveCreationKind::Box;
  }
  if (value ==
      primitive_kind_setting_value(PrimitiveCreationKind::Cylinder)) {
    return PrimitiveCreationKind::Cylinder;
  }
  if (value == primitive_kind_setting_value(PrimitiveCreationKind::Sphere)) {
    return PrimitiveCreationKind::Sphere;
  }
  if (value == primitive_kind_setting_value(PrimitiveCreationKind::Cone)) {
    return PrimitiveCreationKind::Cone;
  }
  if (value == primitive_kind_setting_value(PrimitiveCreationKind::Torus)) {
    return PrimitiveCreationKind::Torus;
  }
  return std::nullopt;
}

QuaderToolSettingControl action_control(std::string id, std::string label) {
  return {
      .kind = QuaderToolSettingControlKind::Action,
      .id = std::move(id),
      .label = std::move(label),
  };
}

QuaderToolSettingsTarget
pivot_tool_settings_target(const PivotToolService &pivot) {
  const PivotSnapSettings snap = pivot.snap_settings();
  QuaderToolSettingsTarget result{
      .active = true,
      .preview_active = true,
      .tool_id = std::string(kPivotToolId),
      .title = "Pivot",
      .description =
          "Move the selected object's pivot and choose which features it can "
          "snap to.",
      .can_reset = true,
      .apply_visible = true,
      .can_apply = true,
      .can_cancel = true,
  };
  result.controls.push_back(action_control("pivot.align.prev", "Prev"));
  result.controls.push_back(action_control("pivot.align.next", "Next"));
  result.controls.push_back(toggle_control(std::string(kPivotSnapVertexSetting),
                                           "Vertex", snap.vertex));
  result.controls.push_back(toggle_control(
      std::string(kPivotSnapEdgeMidpointSetting), "Edge Midpoint",
      snap.edge_midpoint));
  result.controls.push_back(toggle_control(
      std::string(kPivotSnapFaceCenterSetting), "Face Center",
      snap.face_center));
  result.controls.push_back(
      toggle_control(std::string(kPivotSnapGridSetting), "Grid", snap.grid));
  return result;
}

QuaderToolSettingsTarget linear_array_tool_settings_target(
    const LinearArrayToolService &linear_array) {
  QuaderToolSettingsTarget result{
      .active = true,
      .preview_active = true,
      .tool_id = std::string(kLinearArrayToolId),
      .title = "Linear Array",
      .description =
          "Drag an endpoint to create evenly spaced copies along a line.",
      .can_reset = true,
      .apply_visible = true,
      .can_apply = true,
      .can_cancel = true,
  };
  result.controls.push_back(number_control(
      std::string(kLinearArrayCountSetting), "Num Copies", linear_array.count(),
      kLinearArrayMinimumCount, linear_array.maximum_count(), 1.0));
  return result;
}

QuaderToolSettingsTarget radial_array_tool_settings_target(
    const RadialArrayToolService &radial_array) {
  const bool center_locked = radial_array.center_locked();
  QuaderToolSettingsTarget result{
      .active = true,
      .preview_active = center_locked,
      .tool_id = std::string(kRadialArrayToolId),
      .title = "Radial Array",
      .description =
          "Choose a center and arrange copies around its axis.",
      .can_reset = true,
      .apply_visible = true,
      .can_apply = center_locked,
      .can_cancel = true,
  };
  result.controls.push_back(number_control(
      std::string(kRadialArrayDegreesSetting), "Degrees",
      radial_array.degrees(), kRadialArrayMinimumDegrees,
      kRadialArrayMaximumDegrees, 1.0));
  result.controls.push_back(number_control(
      std::string(kRadialArrayCopiesSetting), "Number of Copies",
      radial_array.copies(), kRadialArrayMinimumCopies,
      radial_array.maximum_copies(), 1.0));
  result.controls.push_back(number_control(
      std::string(kRadialArrayRepeatsSetting), "Repeats",
      radial_array.repeats(), kRadialArrayMinimumRepeats,
      radial_array.maximum_repeats(), 1.0));
  result.controls.push_back(number_control(
      std::string(kRadialArrayRepeatSpacingSetting), "Repeat Spacing",
      radial_array.repeat_spacing(), kRadialArrayMinimumRepeatSpacing,
      kRadialArrayMaximumRepeatSpacing, 0.001));
  return result;
}

QuaderToolSettingsTarget primitive_tool_settings_target(
    const PrimitiveCreationToolService &primitive) {
  const std::optional<PrimitiveCreationKind> kind = primitive.active_kind();
  if (!kind.has_value()) {
    return {};
  }

  QuaderToolSettingsTarget result{
      .active = true,
      .preview_active = primitive.shape_count() > 0U,
      .tool_id = std::string(kCreatePrimitiveToolId),
      .title = "Create Primitive",
      .description =
          "Draw a footprint, set its height, and refine the primitive before "
          "applying it.",
      .can_reset = primitive.can_reset_settings(),
      .apply_visible = true,
      .can_apply = primitive.can_apply(),
      .can_cancel = true,
  };
  result.controls.push_back(choice_control(
      std::string(kPrimitiveTypeSetting), "Primitive Type",
      std::string(primitive_kind_setting_id(*kind)),
      {
          {.id = "box",
           .label = "Box",
           .value =
               primitive_kind_setting_value(PrimitiveCreationKind::Box)},
          {.id = "cylinder",
           .label = "Cylinder",
           .value = primitive_kind_setting_value(
               PrimitiveCreationKind::Cylinder)},
          {.id = "sphere",
           .label = "Sphere",
           .value =
               primitive_kind_setting_value(PrimitiveCreationKind::Sphere)},
          {.id = "cone",
           .label = "Cone",
           .value =
               primitive_kind_setting_value(PrimitiveCreationKind::Cone)},
          {.id = "torus",
           .label = "Torus",
           .value =
               primitive_kind_setting_value(PrimitiveCreationKind::Torus)},
      }));
  switch (*kind) {
  case PrimitiveCreationKind::Box: {
    const BoxPrimitiveCreationSettings &settings = primitive.box_settings();
    result.controls.push_back(number_control(
        std::string(kBoxSubdivisionsXSetting), "X Subdivisions",
        settings.subdivisions_x, kPrimitiveMinimumSubdivisionCount,
        kPrimitiveMaximumSubdivisionCount, 1.0));
    result.controls.push_back(number_control(
        std::string(kBoxSubdivisionsYSetting), "Y Subdivisions",
        settings.subdivisions_y, kPrimitiveMinimumSubdivisionCount,
        kPrimitiveMaximumSubdivisionCount, 1.0));
    result.controls.push_back(number_control(
        std::string(kBoxSubdivisionsZSetting), "Z Subdivisions",
        settings.subdivisions_z, kPrimitiveMinimumSubdivisionCount,
        kPrimitiveMaximumSubdivisionCount, 1.0));
    result.controls.push_back(toggle_control(
        std::string(kPrimitiveFlipFacesSetting), "Flip Faces",
        settings.flip_faces));
    break;
  }
  case PrimitiveCreationKind::Cylinder: {
    const CylinderPrimitiveCreationSettings &settings =
        primitive.cylinder_settings();
    result.controls.push_back(number_control(
        std::string(kCylinderSideCountSetting), "Sides", settings.side_count,
        kCylinderMinimumSideCount, kCylinderMaximumSideCount, 1.0));
    result.controls.push_back(toggle_control(
        std::string(kCylinderCapsSetting), "Caps", settings.caps));
    result.controls.push_back(toggle_control(
        std::string(kPrimitiveFlipFacesSetting), "Flip Faces",
        settings.flip_faces));
    break;
  }
  case PrimitiveCreationKind::Cone: {
    const ConePrimitiveCreationSettings &settings = primitive.cone_settings();
    result.controls.push_back(number_control(
        std::string(kConeSideCountSetting), "Sides", settings.side_count,
        kConeMinimumSideCount, kConeMaximumSideCount, 1.0));
    result.controls.push_back(toggle_control(
        std::string(kConeCapsSetting), "Base Cap", settings.caps));
    result.controls.push_back(toggle_control(
        std::string(kPrimitiveFlipFacesSetting), "Flip Faces",
        settings.flip_faces));
    break;
  }
  case PrimitiveCreationKind::Sphere: {
    const SpherePrimitiveCreationSettings &settings =
        primitive.sphere_settings();
    result.controls.push_back(number_control(
        std::string(kSphereRadialSegmentsSetting), "Radial Segments",
        settings.radial_segments, kSphereMinimumRadialSegments,
        kSphereMaximumRadialSegments, 1.0));
    result.controls.push_back(number_control(
        std::string(kSphereRingsSetting), "Rings", settings.rings,
        kSphereMinimumRings, kSphereMaximumRings, 1.0));
    result.controls.push_back(toggle_control(
        std::string(kSphereHemisphereSetting), "Hemisphere",
        settings.hemisphere));
    result.controls.push_back(toggle_control(
        std::string(kPrimitiveFlipFacesSetting), "Flip Faces",
        settings.flip_faces));
    break;
  }
  case PrimitiveCreationKind::Torus: {
    const TorusPrimitiveCreationSettings &settings = primitive.torus_settings();
    result.controls.push_back(number_control(
        std::string(kTorusMajorSegmentsSetting), "Major Segments",
        settings.major_segments, kTorusMinimumMajorSegments,
        kTorusMaximumMajorSegments, 1.0));
    result.controls.push_back(number_control(
        std::string(kTorusMinorSegmentsSetting), "Minor Segments",
        settings.minor_segments, kTorusMinimumMinorSegments,
        kTorusMaximumMinorSegments, 1.0));
    result.controls.push_back(number_control(
        std::string(kTorusTubeRadiusRatioSetting), "Tube Radius Ratio",
        settings.tube_radius_ratio, kTorusMinimumTubeRadiusRatio,
        kTorusMaximumTubeRadiusRatio, 0.01));
    result.controls.push_back(toggle_control(
        std::string(kPrimitiveFlipFacesSetting), "Flip Faces",
        settings.flip_faces));
    break;
  }
  }
  return result;
}

QuaderApiVoidResult api_status_from_command(SceneSession &session,
                                            CommandResult result,
                                            QuaderMutationKind mutation,
                                            std::string_view fallback) {
  return quader_api_void_status(session, result.ok, result.changed,
                                std::move(result.message), mutation,
                                fallback);
}

std::string tool_settings_target_tool_id(
    const ModelingToolSettingsTarget &target) {
  if (!target.operation_id.empty()) {
    return target.operation_id;
  }
  if (!target.action_id.empty()) {
    return target.action_id;
  }
  switch (target.kind) {
  case ModelingToolSettingsTargetKind::Cut:
    return "cut";
  case ModelingToolSettingsTargetKind::Mirror:
    return "mirror";
  case ModelingToolSettingsTargetKind::Bridge:
    return "bridge";
  case ModelingToolSettingsTargetKind::EdgeBevel:
    return "edge_bevel";
  case ModelingToolSettingsTargetKind::Thicken:
    return "thicken";
  case ModelingToolSettingsTargetKind::SliceQuad:
    return "slice_quad";
  case ModelingToolSettingsTargetKind::MergeByDistance:
    return "merge_by_distance";
  case ModelingToolSettingsTargetKind::HalfCut:
    return "half_cut";
  case ModelingToolSettingsTargetKind::None:
    break;
  }
  return {};
}

const char *fallback_tool_settings_description(
    ModelingToolSettingsTargetKind kind) {
  switch (kind) {
  case ModelingToolSettingsTargetKind::Cut:
    return "Draw a cutting plane through the selection and choose which side "
           "to keep.";
  case ModelingToolSettingsTargetKind::Mirror:
    return "Draw a plane that mirrors the selected geometry.";
  case ModelingToolSettingsTargetKind::Bridge:
    return "Connect selected boundary edges with an adjustable interpolated "
           "bridge.";
  case ModelingToolSettingsTargetKind::EdgeBevel:
    return "Round selected edges and adjust the bevel width, segments, and "
           "profile.";
  case ModelingToolSettingsTargetKind::Thicken:
    return "Add adjustable thickness to the selected faces.";
  case ModelingToolSettingsTargetKind::SliceQuad:
    return "Subdivide selected quads into an adjustable grid.";
  case ModelingToolSettingsTargetKind::MergeByDistance:
    return "Merge selected vertices that fall within the chosen tolerance.";
  case ModelingToolSettingsTargetKind::HalfCut:
    return "Split selected meshes along a local axis and choose how the parts "
           "are produced.";
  case ModelingToolSettingsTargetKind::None:
    break;
  }
  return "Adjust settings for the active tool.";
}

QuaderToolSettingsTarget modeling_tool_settings_target(
    const ModelingToolSettingsTarget &target,
    const ModelingInteractionSettings &settings) {
  QuaderToolSettingsTarget result;
  if (!target.active) {
    return result;
  }

  result.active = true;
  result.preview_active = target.operation_preview_active;
  result.tool_id = tool_settings_target_tool_id(target);
  result.title = target.label.empty()
                     ? fallback_tool_settings_label(target.kind)
                     : target.label;
  result.description = fallback_tool_settings_description(target.kind);
  result.apply_visible = target.operation_preview_active;
  result.can_apply = target.operation_preview_active;
  result.can_cancel = target.operation_preview_active;

  constexpr quader_editor_core::BridgeOperationSchema kBridge =
      quader_editor_core::bridge_operation_schema();
  constexpr quader_editor_core::EdgeBevelOperationSchema kEdgeBevel =
      quader_editor_core::edge_bevel_operation_schema();
  constexpr quader_editor_core::ThickenOperationSchema kThicken =
      quader_editor_core::thicken_operation_schema();
  constexpr quader_editor_core::SliceQuadOperationSchema kSliceQuad =
      quader_editor_core::slice_quad_operation_schema();
  constexpr quader_editor_core::MergeByDistanceOperationSchema
      kMergeByDistance =
          quader_editor_core::merge_by_distance_operation_schema();

  switch (target.kind) {
  case ModelingToolSettingsTargetKind::Cut: {
    const int cut_mode = static_cast<int>(settings.cut_mode);
    result.controls.push_back(choice_control(
        "operation.cut.keep_mode", "Keep Mode", std::to_string(cut_mode),
        {
            {.id = "discard_left",
             .label = "Discard Left",
             .value = static_cast<double>(ModelingCutMode::DiscardLeft)},
            {.id = "discard_right",
             .label = "Discard Right",
             .value = static_cast<double>(ModelingCutMode::DiscardRight)},
            {.id = "keep_both",
             .label = "Keep Both",
             .value = static_cast<double>(ModelingCutMode::KeepBoth)},
        }));
    break;
  }
  case ModelingToolSettingsTargetKind::EdgeBevel: {
    result.controls.push_back(number_control(
        "operation.edge_bevel.width", "Width", settings.edge_bevel_width,
        kEdgeBevel.width.minimum, kEdgeBevel.width.maximum,
        kEdgeBevel.width.step));
    result.controls.push_back(number_control(
        "operation.edge_bevel.segments", "Segments",
        settings.edge_bevel_segments,
        static_cast<double>(kEdgeBevel.segments.minimum),
        static_cast<double>(kEdgeBevel.segments.maximum),
        static_cast<double>(kEdgeBevel.segments.step)));
    result.controls.push_back(number_control(
        "operation.edge_bevel.profile", "Profile",
        settings.edge_bevel_profile, kEdgeBevel.profile.minimum,
        kEdgeBevel.profile.maximum, kEdgeBevel.profile.step));
    break;
  }
  case ModelingToolSettingsTargetKind::Bridge:
    result.controls.push_back(number_control(
        "operation.bridge.steps", "Steps", settings.bridge_steps,
        static_cast<double>(kBridge.steps.minimum),
        static_cast<double>(kBridge.steps.maximum),
        static_cast<double>(kBridge.steps.step)));
    break;
  case ModelingToolSettingsTargetKind::Thicken:
    result.controls.push_back(number_control(
        "operation.thicken.thickness", "Distance",
        settings.thicken_thickness, kThicken.thickness.minimum,
        kThicken.thickness.maximum, kThicken.thickness.step));
    result.controls.push_back(toggle_control("operation.thicken.from_center",
                                             "From Center",
                                             settings.thicken_from_center));
    break;
  case ModelingToolSettingsTargetKind::SliceQuad:
    result.controls.push_back(number_control(
        "operation.slice_quad.x_slices", "X", settings.slice_quad_x_slices,
        static_cast<double>(kSliceQuad.x_slices.minimum),
        static_cast<double>(kSliceQuad.x_slices.maximum),
        static_cast<double>(kSliceQuad.x_slices.step)));
    result.controls.push_back(number_control(
        "operation.slice_quad.y_slices", "Y", settings.slice_quad_y_slices,
        static_cast<double>(kSliceQuad.y_slices.minimum),
        static_cast<double>(kSliceQuad.y_slices.maximum),
        static_cast<double>(kSliceQuad.y_slices.step)));
    break;
  case ModelingToolSettingsTargetKind::MergeByDistance:
    result.controls.push_back(number_control(
        "operation.merge_by_distance.tolerance", "Tolerance",
        settings.merge_by_distance_tolerance,
        kMergeByDistance.tolerance.minimum,
        kMergeByDistance.tolerance.maximum,
        kMergeByDistance.tolerance.step));
    break;
  case ModelingToolSettingsTargetKind::HalfCut: {
    const int axis = static_cast<int>(settings.half_cut_axis);
    result.controls.push_back(choice_control(
        "operation.half_cut.axis", "Axis", std::to_string(axis),
        {
            {.id = "axis_x",
             .label = "X",
             .value = static_cast<double>(ModelingHalfCutAxis::X)},
            {.id = "axis_y",
             .label = "Y",
             .value = static_cast<double>(ModelingHalfCutAxis::Y)},
            {.id = "axis_z",
             .label = "Z",
             .value = static_cast<double>(ModelingHalfCutAxis::Z)},
        }));
    result.controls.push_back(toggle_control(
        "operation.half_cut.separate_parts", "Separate Parts",
        settings.half_cut_separate_parts));
    result.controls.push_back(toggle_control(
        "operation.half_cut.fill_parts", "Fill Parts",
        settings.half_cut_fill_parts));
    break;
  }
  case ModelingToolSettingsTargetKind::Mirror:
  case ModelingToolSettingsTargetKind::None:
    break;
  }

  result.can_reset = std::any_of(
      result.controls.begin(), result.controls.end(),
      [](const QuaderToolSettingControl &control) {
        return control.kind != QuaderToolSettingControlKind::Action;
      });
  return result;
}

} // namespace

QuaderToolsApi::QuaderToolsApi(QuaderApiSession session)
    : session_(session) {}

void QuaderToolsApi::set_runtime_hooks(QuaderToolsRuntimeHooks hooks) {
  hooks_ = std::move(hooks);
}

void QuaderToolsApi::rebind_session(QuaderApiSession session) {
  session_ = session;
  active_settings_source_valid_ = false;
}

QuaderApiVoidResult QuaderToolsApi::activate(std::string_view tool_id) {
  if (hooks_.activate) {
    return hooks_.activate(tool_id);
  }

  SceneSession &session = scene_session(session_);
  if (session.native_tools().handles(tool_id)) {
    const double grid_size =
        hooks_.grid_world_size ? hooks_.grid_world_size() : 1.0;
    return api_status_from_command(
        session, session.native_tools().activate(tool_id, grid_size),
        QuaderMutationKind::Runtime, tool_id);
  }

  const ViewportTool *tool = find_viewport_tool(tool_id);
  if (tool == nullptr) {
    return quader_api_failure_status(session, "Unknown viewport tool.");
  }
  const CommandResult preflight =
      runtime_tool_activation_preflight(session, *tool);
  if (!preflight.ok) {
    return api_status_from_command(session, preflight,
                                   QuaderMutationKind::Runtime, tool_id);
  }

  const ModelingSessionService &modeling = session.modeling();
  const bool repeated_without_competing_preview =
      !session.native_tools().has_pending_tool() &&
      modeling.tool_status().active_tool == tool->modeling_tool() &&
      !modeling.can_cancel_active_preview_for_undo();
  CommandResult canceled = command_unchanged_result();
  if (!repeated_without_competing_preview) {
    canceled =
        session.native_tools().cancel_competing_state_for_tool_activation();
    if (!canceled.ok) {
      return api_status_from_command(session, canceled,
                                     QuaderMutationKind::Runtime, tool_id);
    }
  }

  ModelingCommandResult result = tool->activate(session.modeling());
  const bool changed = result.changed || canceled.changed;
  return quader_api_void_status(session, result.ok,
                                changed, std::move(result.message),
                                QuaderMutationKind::Modeling, tool_id);
}

QuaderApiVoidResult QuaderToolsApi::activate(ViewportToolId tool) {
  return activate(viewport_tool_catalog_id(tool));
}

bool QuaderToolsApi::can_activate(std::string_view tool_id) const {
  const SceneSession &session = scene_session(session_);
  if (session.native_tools().handles(tool_id)) {
    return session.native_tools().can_activate(tool_id);
  }
  const ViewportTool *tool = find_viewport_tool(tool_id);
  return tool != nullptr && runtime_tool_activation_preflight(session, *tool).ok;
}

bool QuaderToolsApi::can_activate(ViewportToolId tool) const {
  return can_activate(viewport_tool_catalog_id(tool));
}

QuaderToolSettingsTarget QuaderToolsApi::active_settings() const {
  const SceneSession &session = scene_session(session_);
  if (session.primitive_creation().active()) {
    return primitive_tool_settings_target(session.primitive_creation());
  }
  if (session.radial_array().active()) {
    return radial_array_tool_settings_target(session.radial_array());
  }
  if (session.linear_array().active()) {
    return linear_array_tool_settings_target(session.linear_array());
  }
  if (session.pivot().persistent_active()) {
    return pivot_tool_settings_target(session.pivot());
  }
  const ModelingSessionService &modeling = session.modeling();
  return modeling_tool_settings_target(modeling.tool_settings_target(),
                                       modeling.interaction_settings());
}

std::uint64_t QuaderToolsApi::active_settings_revision() const {
  const ActiveToolSettingsSource source =
      active_tool_settings_source(scene_session(session_));
  const std::uint8_t source_kind = static_cast<std::uint8_t>(source.kind);
  if (!active_settings_source_valid_ ||
      active_settings_source_identity_ != source.identity ||
      active_settings_source_revision_ != source.revision ||
      active_settings_source_kind_ != source_kind ||
      active_settings_source_reset_availability_token_ !=
          source.reset_availability_token) {
    active_settings_source_identity_ = source.identity;
    active_settings_source_revision_ = source.revision;
    active_settings_source_kind_ = source_kind;
    active_settings_source_reset_availability_token_ =
        source.reset_availability_token;
    active_settings_source_valid_ = true;
    ++active_settings_revision_;
  }
  return active_settings_revision_;
}

QuaderApiVoidResult QuaderToolsApi::set_setting(std::string_view tool_id,
                                                 std::string_view name,
                                                 double value) {
  SceneSession &session = scene_session(session_);
  if (session.primitive_creation().active()) {
    if (!tool_id.empty() && tool_id != kCreatePrimitiveToolId) {
      return quader_api_failure_status(
          session, "Tool setting does not belong to the active primitive.");
    }
    if (name == kPrimitiveTypeSetting) {
      const std::optional<PrimitiveCreationKind> kind =
          primitive_kind_from_setting_value(value);
      if (!kind.has_value()) {
        return quader_api_failure_status(session, "Invalid primitive type.");
      }
      return api_status_from_command(
          session, session.primitive_creation().set_kind(*kind),
          QuaderMutationKind::Runtime, "Set Primitive Type");
    }
    return api_status_from_command(
        session, session.primitive_creation().set_setting(name, value),
        QuaderMutationKind::Runtime, "Set Primitive Tool Setting");
  }
  const bool radial_array_tool =
      tool_id.empty() || tool_id == kRadialArrayToolId;
  if (radial_array_tool && name == kRadialArrayDegreesSetting) {
    if (!std::isfinite(value) || value < kRadialArrayMinimumDegrees ||
        value > kRadialArrayMaximumDegrees) {
      return quader_api_failure_status(session,
                                       "Invalid Radial Array degrees.");
    }
    return api_status_from_command(
        session, session.radial_array().set_degrees(value),
        QuaderMutationKind::Runtime, "Set Radial Array Degrees");
  }
  if (radial_array_tool && name == kRadialArrayCopiesSetting) {
    if (!std::isfinite(value) || std::round(value) != value ||
        value < static_cast<double>(kRadialArrayMinimumCopies) ||
        value > static_cast<double>(session.radial_array().maximum_copies())) {
      return quader_api_failure_status(session,
                                       "Invalid Radial Array copy count.");
    }
    return api_status_from_command(
        session,
        session.radial_array().set_copies(static_cast<std::uint32_t>(value)),
        QuaderMutationKind::Runtime, "Set Radial Array Copies");
  }
  if (radial_array_tool && name == kRadialArrayRepeatsSetting) {
    if (!std::isfinite(value) || std::round(value) != value ||
        value < static_cast<double>(kRadialArrayMinimumRepeats) ||
        value > static_cast<double>(session.radial_array().maximum_repeats())) {
      return quader_api_failure_status(session,
                                       "Invalid Radial Array repeats.");
    }
    return api_status_from_command(
        session,
        session.radial_array().set_repeats(static_cast<std::uint32_t>(value)),
        QuaderMutationKind::Runtime, "Set Radial Array Repeats");
  }
  if (radial_array_tool && name == kRadialArrayRepeatSpacingSetting) {
    if (!std::isfinite(value) || value < kRadialArrayMinimumRepeatSpacing ||
        value > kRadialArrayMaximumRepeatSpacing) {
      return quader_api_failure_status(
          session, "Invalid Radial Array repeat spacing.");
    }
    return api_status_from_command(
        session, session.radial_array().set_repeat_spacing(value),
        QuaderMutationKind::Runtime, "Set Radial Array Repeat Spacing");
  }
  if ((tool_id.empty() || tool_id == kLinearArrayToolId) &&
      name == kLinearArrayCountSetting) {
    if (!std::isfinite(value) || std::round(value) != value ||
        value < static_cast<double>(kLinearArrayMinimumCount) ||
        value > static_cast<double>(session.linear_array().maximum_count())) {
      return quader_api_failure_status(session,
                                       "Invalid Linear Array count.");
    }
    return api_status_from_command(
        session,
        session.linear_array().set_count(static_cast<std::uint32_t>(value)),
        QuaderMutationKind::Runtime, "Set Linear Array Count");
  }
  if (tool_id == kPivotToolId) {
    return api_status_from_command(
        session, session.pivot().set_snap_enabled(name, value != 0.0),
        QuaderMutationKind::Runtime, "Set Pivot Setting");
  }

  if (is_cut_mode_setting(tool_id, name)) {
    const std::optional<ModelingCutMode> mode =
        cut_mode_from_setting_value(value);
    if (!mode.has_value()) {
      return quader_api_failure_status(session, "Invalid cut mode.");
    }
    ModelingCommandResult result = session.modeling().set_cut_mode(*mode);
    return quader_api_void_status(session, result.ok,
                                  result.changed, std::move(result.message),
                                  QuaderMutationKind::Modeling,
                                  "Set Tool Setting");
  }

  ModelingInteractionSettings settings = session.modeling().interaction_settings();

  if (!apply_modeling_interaction_setting(settings, tool_id, name, value)) {
    return quader_api_failure_status(session, "Unknown tool setting.");
  }
  ModelingCommandResult result = session.modeling().set_interaction_settings(settings);
  return quader_api_void_status(session, result.ok,
                                result.changed, std::move(result.message),
                                QuaderMutationKind::Modeling,
                                "Set Tool Setting");
}

QuaderApiVoidResult QuaderToolsApi::set_setting(std::string_view tool_id,
                                                 std::string_view name,
                                                 bool value) {
  return set_setting(tool_id, name, value ? 1.0 : 0.0);
}

QuaderApiVoidResult QuaderToolsApi::set_setting(std::string_view tool_id,
                                                 std::string_view name,
                                                 std::string_view value) {
  SceneSession &session = scene_session(session_);
  if (session.primitive_creation().active() &&
      name == kPrimitiveTypeSetting) {
    if (!tool_id.empty() && tool_id != kCreatePrimitiveToolId) {
      return quader_api_failure_status(
          session, "Tool setting does not belong to Create Primitive.");
    }
    const std::optional<PrimitiveCreationKind> kind =
        primitive_kind_from_setting_id(value);
    if (!kind.has_value()) {
      return quader_api_failure_status(session, "Invalid primitive type.");
    }
    return set_setting(tool_id, name, primitive_kind_setting_value(*kind));
  }

  char *end = nullptr;
  const std::string text(value);
  const double numeric = std::strtod(text.c_str(), &end);
  if (end == text.c_str() || (end != nullptr && *end != '\0')) {
    return quader_api_failure_status(session, "Invalid tool setting value.");
  }
  return set_setting(tool_id, name, numeric);
}

QuaderApiVoidResult QuaderToolsApi::set_setting(std::string_view tool_id,
                                                 std::string_view name,
                                                 const char *value) {
  if (value == nullptr) {
    return quader_api_failure_status(scene_session(session_),
                                     "Tool setting value is missing.");
  }
  return set_setting(tool_id, name, std::string_view(value));
}

QuaderApiVoidResult QuaderToolsApi::execute_action(std::string_view tool_id,
                                                    std::string_view action_id) {
  SceneSession &session = scene_session(session_);
  if (tool_id == kPivotToolId) {
    return api_status_from_command(
        session, session.pivot().execute_align_action(action_id),
        QuaderMutationKind::Runtime, "Pivot Action");
  }
  return quader_api_failure_status(session, "Unknown tool action.");
}

QuaderApiVoidResult QuaderToolsApi::reset_active_settings() {
  SceneSession &session = scene_session(session_);
  if (session.primitive_creation().active()) {
    return api_status_from_command(
        session, session.primitive_creation().reset_settings(),
        QuaderMutationKind::Runtime, "Reset Primitive Settings");
  }
  if (session.radial_array().active()) {
    return api_status_from_command(
        session, session.radial_array().reset_settings(),
        QuaderMutationKind::Runtime, "Reset Radial Array Settings");
  }
  if (session.linear_array().active()) {
    return api_status_from_command(
        session, session.linear_array().reset_settings(),
        QuaderMutationKind::Runtime, "Reset Linear Array Settings");
  }
  if (session.pivot().persistent_active()) {
    return api_status_from_command(
        session, session.pivot().reset_settings(),
        QuaderMutationKind::Runtime, "Reset Pivot Settings");
  }

  ModelingCommandResult result =
      session.modeling().reset_active_tool_settings();
  return quader_api_void_status(session, result.ok, result.changed,
                                std::move(result.message),
                                QuaderMutationKind::Modeling,
                                "Reset Tool Settings");
}

QuaderApiVoidResult QuaderToolsApi::accept_active_primitive(
    std::string name, AssetId default_material, bool snap_enabled) {
  SceneSession &session = scene_session(session_);
  MaterialReference material = active_project_material(session_);
  if (!material.valid() && default_material != kInvalidAssetId) {
    material = MaterialReference(default_material);
  }
  return api_status_from_command(
      session,
      session.native_tools().accept_active_primitive(std::move(name),
                                                      material,
                                                      snap_enabled),
      QuaderMutationKind::Modeling, "Create Primitive");
}

bool QuaderToolsApi::has_active_primitive_creation() const {
  return scene_session(session_).primitive_creation().active();
}

std::string QuaderToolsApi::active_primitive_type() const {
  const std::optional<PrimitiveCreationKind> kind =
      scene_session(session_).primitive_creation().active_kind();
  if (!kind.has_value()) {
    return {};
  }
  switch (*kind) {
  case PrimitiveCreationKind::Box:
    return "box";
  case PrimitiveCreationKind::Cylinder:
    return "cylinder";
  case PrimitiveCreationKind::Sphere:
    return "sphere";
  case PrimitiveCreationKind::Cone:
    return "cone";
  case PrimitiveCreationKind::Torus:
    return "torus";
  }
  return {};
}

std::string QuaderToolsApi::active_primitive_default_name() const {
  const std::optional<PrimitiveCreationKind> kind =
      scene_session(session_).primitive_creation().active_kind();
  return kind.has_value()
             ? std::string(primitive_creation_default_name(*kind))
             : std::string{};
}

QuaderApiVoidResult QuaderToolsApi::commit_active_tool_preview() {
  SceneSession &session = scene_session(session_);
  if (session.native_tools().has_pending_tool()) {
    const bool linear_array = session.linear_array().has_session();
    const bool radial_array = session.radial_array().has_session();
    const bool primitive = session.primitive_creation().active();
    const bool modeling_tool = linear_array || radial_array || primitive;
    return api_status_from_command(
        session, session.native_tools().commit_active(),
        modeling_tool ? QuaderMutationKind::Modeling
                      : QuaderMutationKind::Document,
        radial_array ? "Apply Radial Array"
        : linear_array ? "Apply Linear Array"
        : primitive    ? "Apply Primitive Tool"
                       : "Apply Pivot");
  }
  if (hooks_.commit_active_tool_preview) {
    return hooks_.commit_active_tool_preview();
  }
  return quader_api_failure_status(session, "No active tool preview.");
}

QuaderApiVoidResult QuaderToolsApi::cancel_active_tool_preview() {
  SceneSession &session = scene_session(session_);
  if (session.native_tools().has_pending_tool()) {
    return api_status_from_command(
        session, session.native_tools().cancel_active(),
        QuaderMutationKind::Runtime, "Cancel Native Tool");
  }
  if (hooks_.cancel_active_tool_preview) {
    return hooks_.cancel_active_tool_preview();
  }
  return quader_api_failure_status(session, "No active tool preview.");
}

QuaderApiVoidResult QuaderToolsApi::cancel_modal_tool() {
  SceneSession &session = scene_session(session_);
  if (session.native_tools().has_pending_tool()) {
    return api_status_from_command(
        session, session.native_tools().cancel_active(),
        QuaderMutationKind::Runtime, "Cancel Native Tool");
  }
  if (hooks_.cancel_modal_tool) {
    return hooks_.cancel_modal_tool();
  }
  ModelingCommandResult result = session.modeling().cancel_active_modal_tool();
  return quader_api_void_status(session, result.ok,
                                result.changed, std::move(result.message),
                                QuaderMutationKind::Modeling,
                                "Cancel Modal Tool");
}

std::string QuaderToolsApi::active_tool() const {
  const SceneSession &session = scene_session(session_);
  if (const std::string_view native_tool =
          session.native_tools().active_tool_id();
      !native_tool.empty()) {
    return std::string(native_tool);
  }
  return std::string(viewport_tool_catalog_id(viewport_tool_for_modeling(
      session.modeling().tool_status().active_tool)));
}

} // namespace quader

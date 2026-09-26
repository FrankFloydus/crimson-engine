////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_tools_api.hpp"
#include "assets/default_material_catalog.hpp"
#include "core/release_features.hpp"
#include "editor/metadata/release_feature_requirements.hpp"
#include "editor/metadata/operation_input_hint_catalog.hpp"
#include "editor/metadata/transform_operation_input_hints.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_api_mutation_runner.hpp"
#include "api/quader_settings_presentation_internal.hpp"

#include "api/quader_api_status_internal.hpp"
#include "editor/array/linear_array_tool_service.hpp"
#include "editor/array/path_array_tool_service.hpp"
#include "editor/array/radial_array_tool_service.hpp"
#include "editor/curve/curve_tool_service.hpp"
#include "editor/commands/document_command_service.hpp"
#include "editor/instances/scene_instance_service.hpp"
#include "editor/modeling/operations/operation_schema.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/session/project_session.hpp"
#include "editor/modeling/services/modeling_operation_settings.hpp"
#include "editor/modeling/modeling_result_adapters.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/pivot/pivot_tool_service.hpp"
#include "editor/tools/light_creation_tool_service.hpp"
#include "editor/tools/native_viewport_tool_coordinator.hpp"
#include "editor/modeling/services/primitive_creation_tool_service.hpp"
#include "editor/tools/viewport_tool_catalog.hpp"
#include "editor/tools/uv_peel_tool_service.hpp"
#include "editor/tools/extrude_tool_service.hpp"
#include "scene/scene_document.hpp"
#include "math/glm_bridge.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"

#include <editor/modeling/runtime/tool_runtime_policy.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <limits>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader {
namespace {

bool release_allows_tool_setting(std::string_view tool_id,
                                 std::string_view setting_id) {
  if (!release_allows_tool(tool_id)) {
    return false;
  }
  constexpr std::string_view kOperationPrefix = "operation.";
  if (setting_id.starts_with(kOperationPrefix)) {
    setting_id.remove_prefix(kOperationPrefix.size());
    const auto operation = setting_id.substr(0, setting_id.find('.'));
    return release_allows_operation(operation) &&
           release_allows_tool(operation);
  }
  return release_allows_tool(setting_id.substr(0, setting_id.find('.')));
}

std::optional<QuaderPrimitiveCreationShape>
primitive_creation_shape(PrimitiveCreationKind kind) {
  switch (kind) {
  case PrimitiveCreationKind::Box: return QuaderPrimitiveCreationShape::Box;
  case PrimitiveCreationKind::Cylinder: return QuaderPrimitiveCreationShape::Cylinder;
  case PrimitiveCreationKind::Cone: return QuaderPrimitiveCreationShape::Cone;
  case PrimitiveCreationKind::Sphere: return QuaderPrimitiveCreationShape::Sphere;
  case PrimitiveCreationKind::Torus: return QuaderPrimitiveCreationShape::Torus;
  }
  return std::nullopt;
}

std::optional<PrimitiveCreationKind>
primitive_creation_kind(QuaderPrimitiveCreationShape shape) {
  switch (shape) {
  case QuaderPrimitiveCreationShape::Box:
    return PrimitiveCreationKind::Box;
  case QuaderPrimitiveCreationShape::Cylinder:
    return PrimitiveCreationKind::Cylinder;
  case QuaderPrimitiveCreationShape::Cone:
    return PrimitiveCreationKind::Cone;
  case QuaderPrimitiveCreationShape::Sphere:
    return PrimitiveCreationKind::Sphere;
  case QuaderPrimitiveCreationShape::Torus:
    return PrimitiveCreationKind::Torus;
  }
  return std::nullopt;
}

/// Identifies the owner whose active tool-settings revision is exposed.
enum class ActiveToolSettingsSourceKind : std::uint8_t {
  Modeling,
  Primitive,
  PathArray,
  RadialArray,
  LinearArray,
  Pivot,
  Curve,
  UvPeel,
  Extrude,
};

/// Couples a settings owner identity with its monotonic presentation revision.
struct ActiveToolSettingsSource {
  const void *identity = nullptr;
  std::uint64_t revision = 0;
  std::uint64_t dependent_revision = 0;
  ActiveToolSettingsSourceKind kind = ActiveToolSettingsSourceKind::Modeling;
  // Captures owner presentation state that is intentionally independent from
  // its settings revision. Primitive placement freezes reset mid-gesture.
  bool reset_availability_token = false;
};

SceneSession &scene_session(QuaderApiSession session) {
  return QuaderApiSessionInternalAccess::scene_session(session);
}

bool passive_curve_settings_available(const SceneSession &session) {
  return release_has_feature(ReleaseFeature::Curves) &&
         session.modeling().tool_status().active_tool !=
             ModelingViewportTool::LassoSelect &&
         session.curve().has_editable_existing_curve();
}

MaterialReference default_project_material(QuaderApiSession session) {
  ProjectSession *project =
      QuaderApiSessionInternalAccess::project_session(session);
  if (project == nullptr) {
    return {};
  }
  return project->manifest() != nullptr
             ? MaterialReference::project(
                   *ProjectAssetId::parse(kDefaultProjectMaterialId))
             : MaterialReference{};
}

MaterialReference primitive_creation_material(
    QuaderApiSession session, QuaderMaterialReference requested) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return default_project_material(session);
  }
  if (requested.valid()) {
    if (requested.embedded() &&
        scene_session(session).assets().material(requested.embedded_asset) !=
            nullptr) {
      return MaterialReference(requested.embedded_asset);
    }
    const ProjectSession *project =
        QuaderApiSessionInternalAccess::project_session(session);
    if (requested.project_owned() && project != nullptr &&
        project->manifest() != nullptr &&
        !project->manifest()->is_material_deleted(requested.project_asset) &&
        project->material_catalog().find(requested.project_asset) != nullptr) {
      return MaterialReference::project(requested.project_asset);
    }
  }
  return default_project_material(session);
}

ActiveToolSettingsSource
active_tool_settings_source(const SceneSession &session) {
  const ExtrudeToolService &extrude = session.extrude();
  if (extrude.active()) {
    return {
        .identity = &extrude,
        .revision = extrude.tool_settings_revision(),
        .dependent_revision = session.modeling().selection_revision(),
        .kind = ActiveToolSettingsSourceKind::Extrude,
    };
  }
  const UvPeelToolService &uv_peel = session.uv_peel();
  if (uv_peel.active()) {
    return {
        .identity = &uv_peel,
        .revision = uv_peel.tool_settings_revision(),
        .kind = ActiveToolSettingsSourceKind::UvPeel,
    };
  }
  const PathArrayToolService &path_array = session.path_array();
  if (path_array.active()) {
    return {
        .identity = &path_array,
        .revision = path_array.tool_settings_revision(),
        .kind = ActiveToolSettingsSourceKind::PathArray,
    };
  }
  const CurveToolService &curve = session.curve();
  if (curve.active()) {
    return {
        .identity = &curve,
        .revision = curve.tool_settings_revision(),
        .dependent_revision = session.modeling().content_revision(),
        .kind = ActiveToolSettingsSourceKind::Curve,
        .reset_availability_token = curve.can_reset_settings(),
    };
  }
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
  if (passive_curve_settings_available(session)) {
    return {
        .identity = &curve,
        .revision = curve.tool_settings_revision(),
        .dependent_revision = session.modeling().content_revision(),
        .kind = ActiveToolSettingsSourceKind::Curve,
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
                                        double max_value, double step,
                                        QuaderToolSettingNumberInteraction
                                            interaction =
                                                QuaderToolSettingNumberInteraction::
                                                    Continuous,
                                        QuaderToolSettingNumberPresentation
                                            presentation =
                                                QuaderToolSettingNumberPresentation::
                                                    SliderSpinBox) {
  return {
      .kind = QuaderToolSettingControlKind::Number,
      .id = std::move(id),
      .label = std::move(label),
      .number_value = value,
      .min_value = min_value,
      .max_value = max_value,
      .step = step,
      .number_interaction = interaction,
      .number_presentation = presentation,
  };
}

QuaderToolSettingControl vector3_number_control(
    std::string id, std::string label, std::string group_id,
    std::array<std::string, 3> component_ids, const QVec3 &value,
    double min_value, double max_value, double step) {
  QuaderToolSettingControl result{
      .kind = QuaderToolSettingControlKind::Vector3Number,
      .id = std::move(id),
      .label = std::move(label),
      .group_id = std::move(group_id),
  };
  result.vector3_components = {{
      {
          .id = std::move(component_ids[0]),
          .value = static_cast<double>(value.x),
          .min_value = min_value,
          .max_value = max_value,
          .step = step,
      },
      {
          .id = std::move(component_ids[1]),
          .value = static_cast<double>(value.y),
          .min_value = min_value,
          .max_value = max_value,
          .step = step,
      },
      {
          .id = std::move(component_ids[2]),
          .value = static_cast<double>(value.z),
          .min_value = min_value,
          .max_value = max_value,
          .step = step,
      },
  }};
  return result;
}

QuaderToolSettingControl choice_control(
    std::string id, std::string label, std::string choice_value,
    std::vector<QuaderToolSettingChoiceOption> choices,
    QuaderToolSettingChoicePresentation presentation =
        QuaderToolSettingChoicePresentation::Radio) {
  return {
      .kind = QuaderToolSettingControlKind::Choice,
      .id = std::move(id),
      .label = std::move(label),
      .choice_value = std::move(choice_value),
      .choices = std::move(choices),
      .choice_presentation = presentation,
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

std::string_view curve_creation_kind_setting_id(CurveCreationKind kind) {
  switch (kind) {
  case CurveCreationKind::Draw:
    return "draw";
  case CurveCreationKind::Circle:
    return "circle";
  case CurveCreationKind::Oval:
    return "oval";
  }
  return {};
}

double curve_creation_kind_setting_value(CurveCreationKind kind) {
  return static_cast<double>(static_cast<int>(kind));
}

std::optional<CurveCreationKind>
curve_creation_kind_from_setting_id(std::string_view value) {
  if (value == "draw") {
    return CurveCreationKind::Draw;
  }
  if (value == "circle") {
    return CurveCreationKind::Circle;
  }
  if (value == "oval") {
    return CurveCreationKind::Oval;
  }
  return std::nullopt;
}

std::optional<CurveCreationKind>
curve_creation_kind_from_setting_value(double value) {
  if (!std::isfinite(value)) {
    return std::nullopt;
  }
  for (const CurveCreationKind kind : {
           CurveCreationKind::Draw, CurveCreationKind::Circle,
           CurveCreationKind::Oval}) {
    if (value == curve_creation_kind_setting_value(kind)) {
      return kind;
    }
  }
  return std::nullopt;
}

std::string_view path_array_axis_setting_id(PathArraySignedAxis axis) {
  switch (axis) {
  case PathArraySignedAxis::PositiveX:
    return "positive_x";
  case PathArraySignedAxis::NegativeX:
    return "negative_x";
  case PathArraySignedAxis::PositiveY:
    return "positive_y";
  case PathArraySignedAxis::NegativeY:
    return "negative_y";
  case PathArraySignedAxis::PositiveZ:
    return "positive_z";
  case PathArraySignedAxis::NegativeZ:
    return "negative_z";
  }
  return {};
}

std::optional<PathArraySignedAxis>
path_array_axis_from_setting_value(double value) {
  if (!std::isfinite(value) || std::round(value) != value || value < 0.0 ||
      value > 5.0) {
    return std::nullopt;
  }
  return static_cast<PathArraySignedAxis>(static_cast<std::uint8_t>(value));
}

std::optional<PathArraySignedAxis>
path_array_axis_from_setting_id(std::string_view value) {
  for (const PathArraySignedAxis axis : {
           PathArraySignedAxis::PositiveX, PathArraySignedAxis::NegativeX,
           PathArraySignedAxis::PositiveY, PathArraySignedAxis::NegativeY,
           PathArraySignedAxis::PositiveZ, PathArraySignedAxis::NegativeZ}) {
    if (path_array_axis_setting_id(axis) == value) {
      return axis;
    }
  }
  return std::nullopt;
}

std::optional<std::size_t> path_array_vector_component(
    std::string_view name, std::string_view x_setting,
    std::string_view y_setting, std::string_view z_setting) {
  if (name == x_setting) {
    return 0U;
  }
  if (name == y_setting) {
    return 1U;
  }
  if (name == z_setting) {
    return 2U;
  }
  return std::nullopt;
}

void set_path_array_vector_component(QVec3 &setting, std::size_t component,
                                     double value) {
  const float converted = static_cast<float>(value);
  switch (component) {
  case 0U:
    setting.x = converted;
    break;
  case 1U:
    setting.y = converted;
    break;
  case 2U:
    setting.z = converted;
    break;
  default:
    break;
  }
}

QuaderToolSettingControl action_control(std::string id, std::string label) {
  return {
      .kind = QuaderToolSettingControlKind::Action,
      .id = std::move(id),
      .label = std::move(label),
  };
}

QuaderToolSettingsTarget extrude_tool_settings_target(
    const ExtrudeToolService &extrude) {
  const auto mode = extrude.mode();
  const std::string mode_id =
      mode == modeling::FaceExtrudeMode::AlongNormals ? "along_normals"
      : mode == modeling::FaceExtrudeMode::Individual ? "individual" : "region";
  QuaderToolSettingsTarget result{
      .active = true,
      .preview_active = extrude.preview_active(),
      .tool_id = "extrude",
      .title = "Extrude",
      .can_reset = !extrude.dragging(),
      .apply_visible = true,
      .can_apply = extrude.preview_active() && !extrude.dragging(),
      .can_cancel = true,
  };
  auto type = choice_control(
      "extrude.type", "Type", mode_id,
      {{.id = "region", .label = "Extrude", .value = 0.0},
       {.id = "along_normals", .label = "Extrude along normals", .value = 1.0},
       {.id = "individual", .label = "Extrude individually", .value = 2.0}},
      QuaderToolSettingChoicePresentation::Dropdown);
  type.enabled = !extrude.dragging();
  result.controls.push_back(std::move(type));
  auto amount = number_control(
      "extrude.amount", "Amount", extrude.amount(),
      -std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
      0.0001, QuaderToolSettingNumberInteraction::Continuous,
      QuaderToolSettingNumberPresentation::SpinBoxOnly);
  amount.enabled = !extrude.dragging();
  amount.number_grid_scale = viewport_host_defaults().map_units_per_world_unit;
  amount.tooltip = "Extrusion distance in displayed scene units. Negative values extrude inward.";
  result.controls.push_back(std::move(amount));
  for (auto &control : result.controls) {
    control.label_key = "tool_settings." + control.id;
  }
  return result;
}

QuaderToolSettingsTarget uv_peel_tool_settings_target(
    const UvPeelToolService &uv_peel) {
  const ModelingUvPeelOptions options = uv_peel.options();
  QuaderToolSettingsTarget result{
      .active = true,
      .preview_active = uv_peel.preview_valid(),
      .tool_id = "uv_peel",
      .title = "Guided UV Peel",
      .description =
          "Select edge chains to guide U or V texture flow across adjacent "
          "faces, adjust the preview, then Apply or Cancel. Connected patches "
          "are mapped independently and can overlap.",
      .can_reset = true,
      .apply_visible = true,
      .can_apply = uv_peel.preview_valid(),
      .can_cancel = true,
      .status_message = uv_peel.preview_valid() ? std::string{}
                                                : uv_peel.preview_error(),
  };
  result.controls.push_back(choice_control(
      "uv_peel.axis", "Axis",
      options.axis == ModelingUvPeelAxis::U ? "u" : "v",
      {{.id = "u", .label = "U", .value = 0.0},
       {.id = "v", .label = "V", .value = 1.0}},
      QuaderToolSettingChoicePresentation::Dropdown));
  auto world_space = toggle_control("uv_peel.world_space", "World Density",
                                    options.world_space);
  world_space.tooltip =
      "Use one tile per 64 displayed map units along the guide before Repeat "
      "and Offset. Quad strips normalize transverse lanes to the widest span. "
      "Turn off to fit each guide and its transverse span to one tile; the "
      "guide stays at the offset and opposite sides use opposite signs.";
  result.controls.push_back(std::move(world_space));
  const auto append_number = [&result](std::string id, std::string label,
                                       double value, std::string tooltip) {
    auto control = number_control(
        std::move(id), std::move(label), value,
        -std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
        0.1, QuaderToolSettingNumberInteraction::Continuous,
        QuaderToolSettingNumberPresentation::SpinBoxOnly);
    control.tooltip = std::move(tooltip);
    result.controls.push_back(std::move(control));
  };
  append_number("uv_peel.u_repeat", "U Repeat", options.u_repeat,
                "Multiply U repetitions after World Density or per-patch normalization. Larger values make the texture smaller.");
  append_number("uv_peel.v_repeat", "V Repeat", options.v_repeat,
                "Multiply V repetitions after World Density or per-patch normalization. Larger values make the texture smaller.");
  append_number("uv_peel.u_offset", "U Offset (tiles)", options.u_offset,
                "Shift the generated U coordinates in texture tiles after Repeat. One unit equals a full texture tile.");
  append_number("uv_peel.v_offset", "V Offset (tiles)", options.v_offset,
                "Shift the generated V coordinates in texture tiles after Repeat. One unit equals a full texture tile.");
  for (auto &control : result.controls) {
    control.label_key = "tool_settings." + control.id;
    control.number_positive_only = control.id == "uv_peel.u_repeat" ||
                                   control.id == "uv_peel.v_repeat";
  }
  return result;
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
      kLinearArrayMinimumCount, linear_array.maximum_count(), 1.0,
      QuaderToolSettingNumberInteraction::Stepped));
  return result;
}

QuaderToolSettingsTarget
path_array_tool_settings_target(const PathArrayToolService &path_array) {
  constexpr std::string_view kPlacementGroup =
      "path_array.group.placement";
  constexpr std::string_view kRotationGroup = "path_array.group.rotation";
  constexpr std::string_view kScaleGroup = "path_array.group.scale";

  const bool preview_valid = path_array.preview_valid();
  QuaderToolSettingsTarget result{
      .active = true,
      .preview_active = preview_valid,
      .tool_id = std::string(kPathArrayToolId),
      .title = "Path Array",
      .description =
          "Pick one mesh, linked instance, or group to preview copies along "
          "the selected curve.",
      .can_reset = true,
      .apply_visible = true,
      .can_apply = preview_valid,
      .can_cancel = true,
  };
  result.groups = {
      {.id = std::string(kPlacementGroup),
       .label = "Placement",
       .expanded_by_default = true},
      {.id = std::string(kRotationGroup),
       .label = "Rotation",
       .expanded_by_default = false},
      {.id = std::string(kScaleGroup),
       .label = "Scale",
       .expanded_by_default = false},
  };

  QuaderToolSettingControl count = number_control(
      std::string(kPathArrayCountSetting), "Num Copies", path_array.count(),
      kPathArrayMinimumCount, path_array.maximum_count(), 1.0,
      QuaderToolSettingNumberInteraction::Stepped,
      QuaderToolSettingNumberPresentation::DragSpinner);
  count.group_id = kPlacementGroup;
  result.controls.push_back(std::move(count));

  QuaderToolSettingControl distance = number_control(
      std::string(kPathArrayDistanceSetting), "Distance",
      path_array.distance(), 0.0, path_array.maximum_distance(), 0.001);
  distance.group_id = kPlacementGroup;
  result.controls.push_back(std::move(distance));

  QuaderToolSettingControl follow_path = toggle_control(
      std::string(kPathArrayFollowPathSetting), "Follow Path",
      path_array.follow_path());
  follow_path.group_id = kPlacementGroup;
  result.controls.push_back(std::move(follow_path));

  QuaderToolSettingControl axis = choice_control(
      std::string(kPathArrayForwardAxisSetting), "Forward Axis",
      std::string(path_array_axis_setting_id(path_array.forward_axis())),
      {
          {.id = "positive_x", .label = "+X", .value = 0.0},
          {.id = "negative_x", .label = "-X", .value = 1.0},
          {.id = "positive_y", .label = "+Y", .value = 2.0},
          {.id = "negative_y", .label = "-Y", .value = 3.0},
          {.id = "positive_z", .label = "+Z", .value = 4.0},
          {.id = "negative_z", .label = "-Z", .value = 5.0},
      },
      QuaderToolSettingChoicePresentation::Dropdown);
  axis.group_id = kPlacementGroup;
  axis.enabled = path_array.follow_path();
  result.controls.push_back(std::move(axis));

  result.controls.push_back(vector3_number_control(
      "path_array.rotation_offset", "Offset per Copy",
      std::string(kRotationGroup),
      {std::string(kPathArrayRotationOffsetXSetting),
       std::string(kPathArrayRotationOffsetYSetting),
       std::string(kPathArrayRotationOffsetZSetting)},
      path_array.rotation_offset_degrees(),
      kPathArrayRotationUiMinimumDegrees,
      kPathArrayRotationUiMaximumDegrees, kPathArrayRotationUiStepDegrees));
  result.controls.push_back(vector3_number_control(
      "path_array.rotation_random_min", "Random Minimum",
      std::string(kRotationGroup),
      {std::string(kPathArrayRotationRandomMinXSetting),
       std::string(kPathArrayRotationRandomMinYSetting),
       std::string(kPathArrayRotationRandomMinZSetting)},
      path_array.rotation_random_min_degrees(),
      kPathArrayRotationUiMinimumDegrees,
      kPathArrayRotationUiMaximumDegrees, kPathArrayRotationUiStepDegrees));
  result.controls.push_back(vector3_number_control(
      "path_array.rotation_random_max", "Random Maximum",
      std::string(kRotationGroup),
      {std::string(kPathArrayRotationRandomMaxXSetting),
       std::string(kPathArrayRotationRandomMaxYSetting),
       std::string(kPathArrayRotationRandomMaxZSetting)},
      path_array.rotation_random_max_degrees(),
      kPathArrayRotationUiMinimumDegrees,
      kPathArrayRotationUiMaximumDegrees, kPathArrayRotationUiStepDegrees));

  result.controls.push_back(vector3_number_control(
      "path_array.scale_offset", "Scale Factor per Copy",
      std::string(kScaleGroup),
      {std::string(kPathArrayScaleOffsetXSetting),
       std::string(kPathArrayScaleOffsetYSetting),
       std::string(kPathArrayScaleOffsetZSetting)},
      path_array.scale_offset_factors(), kPathArrayScaleUiMinimumFactor,
      kPathArrayScaleUiMaximumFactor, kPathArrayScaleUiStepFactor));
  result.controls.push_back(vector3_number_control(
      "path_array.scale_random_min", "Random Minimum",
      std::string(kScaleGroup),
      {std::string(kPathArrayScaleRandomMinXSetting),
       std::string(kPathArrayScaleRandomMinYSetting),
       std::string(kPathArrayScaleRandomMinZSetting)},
      path_array.scale_random_min_factors(), kPathArrayScaleUiMinimumFactor,
      kPathArrayScaleUiMaximumFactor, kPathArrayScaleUiStepFactor));
  result.controls.push_back(vector3_number_control(
      "path_array.scale_random_max", "Random Maximum",
      std::string(kScaleGroup),
      {std::string(kPathArrayScaleRandomMaxXSetting),
       std::string(kPathArrayScaleRandomMaxYSetting),
       std::string(kPathArrayScaleRandomMaxZSetting)},
      path_array.scale_random_max_factors(), kPathArrayScaleUiMinimumFactor,
      kPathArrayScaleUiMaximumFactor, kPathArrayScaleUiStepFactor));
  return result;
}

QuaderToolSettingsTarget radial_array_tool_settings_target(
    const RadialArrayToolService &radial_array) {
  const bool has_center = radial_array.has_valid_center();
  QuaderToolSettingsTarget result{
      .active = true,
      .preview_active = has_center,
      .tool_id = std::string(kRadialArrayToolId),
      .title = "Radial Array",
      .description =
          "Choose a center and arrange copies around its axis.",
      .can_reset = true,
      .apply_visible = true,
      .can_apply = has_center,
      .can_cancel = true,
  };
  result.controls.push_back(choice_control(
      std::string(kRadialArrayModeSetting), "Mode",
      radial_array.mode() == RadialArrayMode::Total ? "total" : "spacing",
      {{.id = "total",
        .label = "Total",
        .tooltip = "Set the total angle covered by the copies."},
       {.id = "spacing",
        .label = "Spacing",
        .tooltip = "Set the angle between consecutive copies."}},
      QuaderToolSettingChoicePresentation::Buttons));
  result.controls.push_back(number_control(
      std::string(kRadialArrayDegreesSetting),
      radial_array.mode() == RadialArrayMode::Spacing
          ? "Angle Spacing (°)" : "Total Angle (°)",
      radial_array.degrees(), kRadialArrayMinimumDegrees,
      kRadialArrayMaximumDegrees, 1.0));
  result.controls.back().label_key =
      radial_array.mode() == RadialArrayMode::Spacing
          ? "radial_array.angle_spacing" : "radial_array.total_angle";
  result.controls.push_back(number_control(
      std::string(kRadialArrayCopiesSetting), "Number of Copies",
      radial_array.copies(), kRadialArrayMinimumCopies,
      radial_array.maximum_copies(), 1.0,
      QuaderToolSettingNumberInteraction::Stepped));
  result.controls.push_back(number_control(
      std::string(kRadialArrayRepeatsSetting), "Repeats",
      radial_array.repeats(), kRadialArrayMinimumRepeats,
      radial_array.maximum_repeats(), 1.0));
  result.controls.push_back(number_control(
      std::string(kRadialArrayRepeatSpacingSetting), "Repeat Spacing",
      radial_array.repeat_spacing(), kRadialArrayMinimumRepeatSpacing,
      kRadialArrayMaximumRepeatSpacing, 0.001));
  result.controls.push_back(toggle_control(
      std::string(kRadialArrayCreateInstancesSetting), "Create Instances",
      radial_array.create_instances()));
  result.controls.back().tooltip =
      "Create linked instances so geometry edits update every copy.";
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
      .title = "Create " + std::string(primitive_creation_default_name(*kind)),
      .description =
          "Draw a footprint, set its height, and refine the primitive before "
          "applying it.",
      .can_reset = primitive.can_reset_settings(),
      .apply_visible = true,
      .can_apply = primitive.can_apply(),
      .can_cancel = true,
      .title_key = "keybindings.target.tools.activate.create_" +
                   std::string(primitive_kind_setting_id(*kind)) + ".label",
  };
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
    result.controls.push_back(choice_control(
        std::string(kSphereTypeSetting), "Type",
        settings.type == PrimitiveSphereType::Quad ? "quad" : "uv",
        {{.id = "uv", .label = "UV Sphere", .value = 0.0},
         {.id = "quad", .label = "Quad Sphere", .value = 1.0}},
        QuaderToolSettingChoicePresentation::Dropdown));
    std::erase_if(result.controls.back().choices, [](const auto &choice) {
      return choice.id == "quad" &&
             !primitive_sphere_type_available_in_release(PrimitiveSphereType::Quad);
    });
    if (settings.type == PrimitiveSphereType::Quad) {
      result.controls.push_back(number_control(
          std::string(kSphereSubdivisionSetting), "Subdivision",
          settings.subdivision, kSphereMinimumSubdivision,
          kSphereMaximumSubdivision, 1.0));
    } else {
      result.controls.push_back(number_control(
          std::string(kSphereRadialSegmentsSetting), "Radial Segments",
          settings.radial_segments, kSphereMinimumRadialSegments,
          kSphereMaximumRadialSegments, 1.0));
      result.controls.push_back(number_control(
          std::string(kSphereRingsSetting), "Rings", settings.rings,
          kSphereMinimumRings, kSphereMaximumRings, 1.0));
    }
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
  case ModelingToolSettingsTargetKind::Move:
    return "move";
  case ModelingToolSettingsTargetKind::LassoSelect:
    return "lasso_select";
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

std::string_view active_tool_id(const SceneSession &session) {
  if (const std::string_view native_tool =
          session.native_tools().active_tool_id();
      !native_tool.empty()) {
    return native_tool;
  }
  return viewport_tool_catalog_id(
      viewport_tool_for_modeling(session.modeling().tool_status().active_tool));
}

std::vector<QuaderOperationInputHint>
viewport_transform_array_curve_input_hints(
    const SceneSession &session, std::string_view tool_id,
    const QuaderOperationInputContext &context) {
  TransformOperationHintState state;
  if (tool_id == "curve") {
    const auto &curve = session.curve();
    state.can_apply = curve.can_apply();
    state.owner_dragging = curve.dragging();
    state.curve_drawing = curve.drawing();
    state.curve_preset = curve.creation_kind() != CurveCreationKind::Draw;
    state.curve_construction_dragging = curve.point_construction_dragging();
  } else if (tool_id == "pivot") {
    state.can_apply = session.pivot().persistent_active();
  } else if (tool_id == "linear_array") {
    const auto handle = session.linear_array().handle_state();
    state.can_apply = handle.visible;
    state.owner_dragging = handle.dragging;
  } else if (tool_id == "radial_array") {
    state.can_apply = session.radial_array().has_valid_center();
    state.radial_center_locked = session.radial_array().center_locked();
    state.instances_available = release_has_feature(ReleaseFeature::Instancing);
  } else if (tool_id == "path_array") {
    state.can_apply = session.path_array().preview_valid();
    state.path_has_source = session.path_array().has_source();
  }
  return transform_operation_input_hints(tool_id, state, context);
}

const char *fallback_tool_settings_description(
    ModelingToolSettingsTargetKind kind) {
  switch (kind) {
  case ModelingToolSettingsTargetKind::Move:
    return "Move the selection and optionally merge nearby vertices on acceptance.";
  case ModelingToolSettingsTargetKind::LassoSelect:
    return "Draw a freehand region to select enclosed geometry through occluders.";
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
  case ModelingToolSettingsTargetKind::Move:
    result.controls.push_back(toggle_control(
        "auto_merge_vertices", "Auto Merge Vertices",
        settings.move_auto_merge_vertices));
    break;
  case ModelingToolSettingsTargetKind::LassoSelect:
    result.controls.push_back(toggle_control(
        "select_partial_intersection", "Enable select partial intersection",
        settings.lasso_select_partial_intersection));
    break;
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
    auto width = number_control(
        "operation.edge_bevel.width", "Width", settings.edge_bevel_width,
        kEdgeBevel.width.minimum,
        target.edge_bevel_maximum_width.value_or(kEdgeBevel.width.maximum),
        0.01);
    width.number_drag_sensitivity = width.step;
    width.number_decimals = std::numeric_limits<double>::max_digits10;
    result.controls.push_back(std::move(width));
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

QuaderToolSettingsTarget
curve_tool_settings_target(const CurveToolService &curve) {
  constexpr double kPresetDimensionStep = 0.001;
  const bool active_tool = curve.active();

  QuaderToolSettingsTarget result{
      .active = true,
      .preview_active = active_tool && (curve.drawing() || curve.dragging()),
      .tool_id = std::string(kCurveToolId),
      .title = "Curve",
      .description = active_tool
                         ? "Draw a curve point by point or choose a Circle or "
                           "Oval preset. Use Apply to finish. Use the configured "
                           "point conversion gesture to toggle corner/smooth. "
                           "Select a curve and use this tool to edit its points "
                           "and handles."
                         : "Edit the selected Curve and its solidified shape.",
      .can_reset = curve.can_reset_settings(),
      .apply_visible = active_tool,
      .can_apply = curve.can_apply(),
      .can_cancel = active_tool,
  };
  if (!curve.drawing()) {
    result.controls.push_back(toggle_control(std::string(kCurveClosedSetting),
                                             "Closed", curve.closed()));
    const bool solidify = curve.solidify_enabled();
    result.controls.push_back(toggle_control(
        std::string(kCurveSolidifySetting), "Solidify", solidify));
    QuaderToolSettingControl resolution = number_control(
        std::string(kCurveSolidifyResolutionSetting), "Resolution",
        curve.solidify_resolution(), kCurveSolidifyMinimumResolution,
        kCurveSolidifyMaximumResolution, 1.0,
        QuaderToolSettingNumberInteraction::Stepped,
        QuaderToolSettingNumberPresentation::DragSpinner);
    resolution.enabled = solidify;
    result.controls.push_back(std::move(resolution));
    QuaderToolSettingControl radius = number_control(
        std::string(kCurveSolidifyRadiusSetting), "Radius",
        curve.solidify_radius(), kCurveSolidifyMinimumRadius,
        kCurveSolidifyMaximumRadius, kCurveSolidifyMinimumRadius);
    radius.enabled = solidify;
    result.controls.push_back(std::move(radius));
    if (curve.has_editable_existing_curve()) {
      QuaderToolSettingControl apply_solidify = action_control(
          std::string(kCurveApplySolidifyAction), "Apply Solidify");
      apply_solidify.enabled = curve.can_apply_solidify();
      result.controls.push_back(std::move(apply_solidify));
    }
    if (active_tool) {
      result.controls.push_back(
          action_control(std::string(kCurveNewAction), "New Curve"));
    }
    return result;
  }

  const CurveCreationKind kind = curve.creation_kind();
  QuaderToolSettingControl type = choice_control(
      std::string(kCurveTypeSetting), "Curve Type",
      std::string(curve_creation_kind_setting_id(kind)),
      {
          {.id = "draw",
           .label = "Draw",
           .value = curve_creation_kind_setting_value(CurveCreationKind::Draw)},
          {.id = "circle",
           .label = "Circle",
           .value = curve_creation_kind_setting_value(
               CurveCreationKind::Circle)},
          {.id = "oval",
           .label = "Oval",
           .value = curve_creation_kind_setting_value(CurveCreationKind::Oval)},
      },
      QuaderToolSettingChoicePresentation::Dropdown);
  type.enabled = curve.can_change_creation_kind();
  result.controls.push_back(std::move(type));

  switch (kind) {
  case CurveCreationKind::Draw:
    result.controls.push_back(toggle_control(std::string(kCurveClosedSetting),
                                             "Closed", curve.closed()));
    break;
  case CurveCreationKind::Circle:
    result.controls.push_back(
        number_control(std::string(kCurveRadiusSetting), "Radius",
                       curve.preset_radius(), kCurvePresetMinimumRadius,
                       kCurvePresetMaximumRadius, kPresetDimensionStep));
    break;
  case CurveCreationKind::Oval: {
    const double radius = curve.preset_radius();
    const double length = curve.oval_length();
    result.controls.push_back(
        number_control(std::string(kCurveRadiusSetting), "Radius", radius,
                       kCurvePresetMinimumRadius,
                       std::min(kCurvePresetMaximumRadius, length * 0.5),
                       kPresetDimensionStep));
    result.controls.push_back(number_control(
        std::string(kCurveLengthSetting), "Length", length, radius * 2.0,
        kCurvePresetMaximumLength, kPresetDimensionStep));
    break;
  }
  }
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
  active_tool_valid_ = false;
  input_hints_source_valid_ = false;
  primitive_creation_state_.reset();
}

QuaderApiVoidResult QuaderToolsApi::activate(std::string_view tool_id) {
  if (!release_allows_tool(tool_id)) {
    return quader_api_failure_status(scene_session(session_),
                                     release_feature_unavailable_message());
  }
  const QuaderSettingsOperationOriginCapture origin(session_);
  const auto activate_tool = [&]() -> QuaderApiVoidResult {
    if (scene_session(session_).extrude().preview_active() &&
        tool_id != viewport_tool_catalog_id(ViewportToolId::Extrude)) {
      return quader_api_failure_status(
          scene_session(session_), "Apply or cancel the extrusion before changing tools.");
    }
    if (scene_session(session_).document_commands().property_edit_active()) {
      return quader_api_failure_status(
          scene_session(session_), "Finish the active property edit before changing tools.");
    }
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
  };
  QuaderApiVoidResult result = activate_tool();
  if (result.ok() && result.changed()) {
    origin.publish();
  }
  return result;
}

QuaderApiVoidResult QuaderToolsApi::activate(ViewportToolId tool) {
  return activate(viewport_tool_catalog_id(tool));
}

QuaderApiVoidResult QuaderToolsApi::activate_extrude(QuaderExtrudeMode mode) {
  if (!release_has_feature(ReleaseFeature::ExtrudeTool)) {
    return quader_api_failure_status(scene_session(session_),
                                     release_feature_unavailable_message());
  }
  const QuaderSettingsOperationOriginCapture origin(session_);
  SceneSession &session = scene_session(session_);
  if (session.document_commands().property_edit_active()) {
    return quader_api_failure_status(
        session, "Finish the active property edit before changing tools.");
  }
  modeling::FaceExtrudeMode native_mode;
  switch (mode) {
  case QuaderExtrudeMode::Region:
    native_mode = modeling::FaceExtrudeMode::Region;
    break;
  case QuaderExtrudeMode::AlongNormals:
    native_mode = modeling::FaceExtrudeMode::AlongNormals;
    break;
  case QuaderExtrudeMode::Individual:
    native_mode = modeling::FaceExtrudeMode::Individual;
    break;
  default:
    return quader_api_failure_status(session, "Unknown extrusion mode.");
  }
  auto result = api_status_from_command(
      session, session.native_tools().activate_extrude(native_mode),
      QuaderMutationKind::Runtime, "Extrude");
  if (result.ok() && result.changed()) {
    origin.publish();
  }
  return result;
}

bool QuaderToolsApi::can_activate(std::string_view tool_id) const {
  if (!release_allows_tool(tool_id)) {
    return false;
  }
  SceneSession &session = scene_session(session_);
  if (session.extrude().preview_active() &&
      tool_id != viewport_tool_catalog_id(ViewportToolId::Extrude)) {
    return false;
  }
  if (session.document_commands().property_edit_active()) {
    return false;
  }
  if (session.native_tools().handles(tool_id)) {
    return session.native_tools().can_activate(tool_id);
  }
  const ViewportTool *tool = find_viewport_tool(tool_id);
  return tool != nullptr && runtime_tool_activation_preflight(session, *tool).ok;
}

bool QuaderToolsApi::can_activate(ViewportToolId tool) const {
  return can_activate(viewport_tool_catalog_id(tool));
}

QuaderApiVoidResult QuaderToolsApi::begin_primitive_creation(
    QuaderPrimitiveCreationShape shape) {
  const QuaderSettingsOperationOriginCapture origin(session_);
  QuaderApiMutationRunner mutations(session_);
  SceneSession &session = scene_session(session_);
  if (!mutations.available()) {
    return quader_api_failure_status(session, "No scene is active.");
  }
  if (session.document_commands().property_edit_active()) {
    return quader_api_failure_status(
        session,
        "Finish the active property edit before creating a primitive.");
  }

  const std::optional<PrimitiveCreationKind> kind =
      primitive_creation_kind(shape);
  if (!kind.has_value()) {
    return quader_api_failure_status(session, "Unknown primitive shape.");
  }
  if (!primitive_creation_available_in_release(*kind)) {
    return quader_api_failure_status(session, release_feature_unavailable_message());
  }

  const bool was_active = session.primitive_creation().active();
  CommandResult canceled = command_unchanged_result();
  if (!was_active) {
    canceled =
        session.native_tools().cancel_competing_state_for_tool_activation();
    if (!canceled.ok) {
      return api_status_from_command(session, canceled,
                                     QuaderMutationKind::Runtime,
                                     "Create Primitive");
    }
  }
  CommandResult activated = session.primitive_creation().activate(*kind);
  if (activated.ok && activated.changed) {
    origin.publish();
  }
  activated.changed = activated.changed || canceled.changed;
  return api_status_from_command(session, activated,
                                 QuaderMutationKind::Runtime,
                                 "Create Primitive");
}

QuaderApiVoidResult QuaderToolsApi::begin_new_curve_creation() {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return quader_api_failure_status(scene_session(session_),
                                     release_feature_unavailable_message());
  }
  const QuaderSettingsOperationOriginCapture origin(session_);
  QuaderApiMutationRunner mutations(session_);
  SceneSession &session = scene_session(session_);
  if (!mutations.available()) {
    return quader_api_failure_status(session, "No scene is active.");
  }
  if (session.document_commands().property_edit_active()) {
    return quader_api_failure_status(
        session, "Finish the active property edit before creating a Curve.");
  }

  CommandResult canceled = command_unchanged_result();
  if (!session.curve().active()) {
    canceled =
        session.native_tools().cancel_competing_state_for_tool_activation();
    if (!canceled.ok) {
      return api_status_from_command(session, canceled,
                                     QuaderMutationKind::Runtime,
                                     "Create Curve");
    }
  }
  CommandResult activated = session.curve().begin_new_curve();
  if (activated.ok && activated.changed) {
    origin.publish();
  }
  activated.changed = activated.changed || canceled.changed;
  return api_status_from_command(session, activated,
                                 QuaderMutationKind::Runtime, "Create Curve");
}

bool QuaderToolsApi::can_begin_new_curve_creation() const {
  const SceneSession &session = scene_session(session_);
  return can_activate(ViewportToolId::Curve) &&
         session.curve().can_begin_new_curve();
}

QuaderApiVoidResult
QuaderToolsApi::begin_light_creation(QuaderLightType type) {
  if (!release_has_feature(ReleaseFeature::Lights)) {
    return quader_api_failure_status(scene_session(session_),
                                     release_feature_unavailable_message());
  }
  QuaderApiMutationRunner mutations(session_);
  SceneSession &session = scene_session(session_);
  if (!mutations.available()) {
    return quader_api_failure_status(session, "No scene is active.");
  }
  if (session.document_commands().property_edit_active()) {
    return quader_api_failure_status(
        session,
        "Finish the active property edit before creating a light.");
  }

  LightCreationKind kind;
  std::string_view operation;
  switch (type) {
  case QuaderLightType::Point:
    kind = LightCreationKind::Point;
    operation = "Create Point Light";
    break;
  case QuaderLightType::Directional:
    kind = LightCreationKind::Directional;
    operation = "Create Directional Light";
    break;
  case QuaderLightType::Spot:
    return quader_api_failure_status(
        session, "Spot-light viewport creation is not available.");
  default:
    return quader_api_failure_status(session, "Unknown light type.");
  }

  return api_status_from_command(
      session, session.native_tools().begin_light_creation(kind),
      QuaderMutationKind::Runtime, operation);
}

QuaderToolSettingsTarget QuaderToolsApi::active_settings() const {
  const SceneSession &session = scene_session(session_);
  if (session.extrude().active()) {
    return extrude_tool_settings_target(session.extrude());
  }
  if (session.uv_peel().active()) {
    return uv_peel_tool_settings_target(session.uv_peel());
  }
  if (session.path_array().active()) {
    return path_array_tool_settings_target(session.path_array());
  }
  if (session.curve().active()) {
    return curve_tool_settings_target(session.curve());
  }
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
  if (passive_curve_settings_available(session)) {
    return curve_tool_settings_target(session.curve());
  }
  const ModelingSessionService &modeling = session.modeling();
  return modeling_tool_settings_target(modeling.tool_settings_target(),
                                       modeling.interaction_settings());
}

const QuaderSettingsPresentationOrigin &
QuaderToolsApi::active_settings_presentation_origin() const {
  return scene_session(session_).settings_presentation_origin();
}

void QuaderToolsApi::record_active_settings_origin(ViewportPaneId pane) {
  switch (pane) {
  case ViewportPaneId::Perspective:
  case ViewportPaneId::Top:
  case ViewportPaneId::Front:
  case ViewportPaneId::Side:
    break;
  default:
    return;
  }
  const QuaderToolSettingsTarget target = active_settings();
  if (target.active && !target.controls.empty()) {
    scene_session(session_).record_settings_presentation_origin(target.tool_id,
                                                               pane);
  }
}

std::uint64_t QuaderToolsApi::active_settings_revision() const {
  const ActiveToolSettingsSource source =
      active_tool_settings_source(scene_session(session_));
  const std::uint8_t source_kind = static_cast<std::uint8_t>(source.kind);
  if (!active_settings_source_valid_ ||
      active_settings_source_identity_ != source.identity ||
      active_settings_source_revision_ != source.revision ||
      active_settings_source_dependent_revision_ !=
          source.dependent_revision ||
      active_settings_source_kind_ != source_kind ||
      active_settings_source_reset_availability_token_ !=
          source.reset_availability_token) {
    active_settings_source_identity_ = source.identity;
    active_settings_source_revision_ = source.revision;
    active_settings_source_dependent_revision_ = source.dependent_revision;
    active_settings_source_kind_ = source_kind;
    active_settings_source_reset_availability_token_ =
        source.reset_availability_token;
    active_settings_source_valid_ = true;
    ++active_settings_revision_;
  }
  return active_settings_revision_;
}

std::uint64_t QuaderToolsApi::active_tool_revision() const {
  const std::string_view current_id =
      active_tool_id(scene_session(session_));
  if (!active_tool_valid_ || active_tool_id_ != current_id) {
    active_tool_id_.assign(current_id);
    active_tool_valid_ = true;
    ++active_tool_revision_;
  }
  return active_tool_revision_;
}

std::uint64_t QuaderToolsApi::active_input_hints_revision(
    const QuaderOperationInputContext &context) const {
  const SceneSession &session = scene_session(session_);
  const auto &modeling = session.modeling();
  const auto &primitive = session.primitive_creation();
  const auto &extrude = session.extrude();
  // Read only source revisions and scalar phases here. tool_status() constructs
  // plane overlays, so defer it to projection after this retained gate changes.
  const std::array<std::uint64_t, 17> source{
      modeling.tool_settings_revision(), modeling.tool_preview_source_revision(),
      session.scene().selection_revision(), modeling.selection_revision(),
      modeling.content_revision(), primitive.tool_settings_revision(),
      extrude.tool_settings_revision(), session.uv_peel().tool_settings_revision(),
      session.curve().tool_settings_revision(),
      session.curve().control_selection_revision(),
      session.linear_array().tool_settings_revision(),
      session.radial_array().tool_settings_revision(),
      session.path_array().tool_settings_revision(),
      session.pivot().tool_settings_revision(),
      session.pivot().selection_pivot_revision(),
      static_cast<std::uint64_t>(modeling.box_drag_state()) |
          (static_cast<std::uint64_t>(primitive.active()) << 3u) |
          (static_cast<std::uint64_t>(primitive.can_apply()) << 4u),
      static_cast<std::uint64_t>(extrude.active()) |
          (static_cast<std::uint64_t>(extrude.dragging()) << 1u) |
          (static_cast<std::uint64_t>(extrude.preview_active()) << 2u) |
          (static_cast<std::uint64_t>(session.curve().dragging()) << 3u) |
          (static_cast<std::uint64_t>(session.curve().point_construction_dragging()) << 4u)};
  const std::string_view native_id = session.native_tools().active_tool_id();
  if (!input_hints_source_valid_ || input_hints_source_ != source ||
      input_hints_context_ != context || input_hints_native_tool_id_ != native_id) {
    input_hints_source_ = source;
    input_hints_context_ = context;
    input_hints_native_tool_id_ = native_id;
    input_hints_source_valid_ = true;
    ++input_hints_revision_;
  }
  return input_hints_revision_;
}

std::vector<QuaderOperationInputHint> QuaderToolsApi::active_input_hints(
    const QuaderOperationInputContext &context) const {
  if (!context.viewport_available) return {};
  if (context.navigation != QuaderInputNavigation::None) {
    return viewport_navigation_input_hints(context);
  }
  if (context.selection_gesture != QuaderInputSelectionGesture::None) {
    if (context.selection_gesture == QuaderInputSelectionGesture::EnterInstance &&
        !release_has_feature(ReleaseFeature::Instancing)) return {};
    return selection_gesture_input_hints(context);
  }
  if (context.point_light_range_hovered || context.point_light_range_dragging ||
      context.material_transfer != QuaderInputMaterialTransfer::None) {
    if ((context.point_light_range_hovered || context.point_light_range_dragging) &&
        !release_has_feature(ReleaseFeature::Lights)) return {};
    auto auxiliary = viewport_auxiliary_input_hints(context);
    std::erase_if(auxiliary, [](const QuaderOperationInputHint &hint) {
      return hint.gesture == QuaderOperationInputGesture::Keybinding &&
             !release_allows_input_target(hint.command.string());
    });
    return auxiliary;
  }
  const SceneSession &session = scene_session(session_);
  const auto &modeling = session.modeling();
  std::vector<QuaderOperationInputHint> hints;
  const auto &primitive = session.primitive_creation();
  const auto kind = primitive.active_kind();
  const auto shape = kind ? primitive_creation_shape(*kind) : std::nullopt;
  if (shape) {
    auto primitive_context = context;
    primitive_context.pointer_dragging = context.pointer_dragging ||
        modeling.box_drag_state() != ModelingBoxDragState::Idle;
    hints = primitive_creation_input_hints(*shape, primitive.can_apply(), primitive_context);
  } else if (session.extrude().active()) {
    const auto &extrude = session.extrude();
    hints = viewport_tool_input_hints("extrude", {
        .dragging = extrude.dragging(),
        .extrusion_pending = extrude.preview_active(),
        .extrusion_region = extrude.mode() == modeling::FaceExtrudeMode::Region}, context);
  } else if (session.uv_peel().active()) {
    hints = viewport_tool_input_hints("uv_peel", {.can_apply = session.uv_peel().preview_valid()}, context);
  } else if (modeling.operation_preview_active()) {
    hints = viewport_tool_input_hints("operation", {.can_apply = true}, context);
  } else {
    const std::string_view active_id = active_tool_id(session);
    const bool passive_curve_context = (active_id == "select" || active_id == "move") &&
        (context.curve_anchor_hovered || context.curve_tangent_hovered ||
         (context.curve_anchor_selection_only && context.transform_handle != QuaderInputTransformHandle::None));
    const std::string_view tool_id = passive_curve_context ? "curve" : active_id;
    if (tool_id == "move" || tool_id == "rotate" || tool_id == "scale" ||
        tool_id == "pivot" || tool_id == "curve" || tool_id == "linear_array" ||
        tool_id == "radial_array" || tool_id == "path_array" || context.quick_pivot) {
      hints = viewport_transform_array_curve_input_hints(session, tool_id, context);
      if (hints.empty() && (tool_id == "move" || tool_id == "rotate" || tool_id == "scale" || passive_curve_context)) {
        hints = viewport_navigation_input_hints(context);
      }
    } else {
      ModelingOperationHintState state;
      if (tool_id == "knife" || tool_id == "poly" || tool_id == "cut" || tool_id == "mirror") {
        const auto status = modeling.tool_status();
        if (tool_id == "knife") {
          state.can_apply = status.knife_can_commit;
          state.active_stroke = status.knife_has_active_stroke;
        } else if (tool_id == "poly") {
          state.can_apply = status.poly_can_commit;
          state.active_stroke = status.poly_point_count > 0;
          state.closed = status.poly_closed;
        } else if (tool_id == "cut") {
          state.can_apply = status.cut_preview_valid;
          state.dragging = status.cut_dragging;
        } else {
          state.can_apply = status.mirror_preview_valid;
          state.dragging = status.mirror_dragging;
        }
      }
      hints = viewport_tool_input_hints(tool_id, state, context);
    }
  }
  std::erase_if(hints, [](const QuaderOperationInputHint &hint) {
    return hint.gesture == QuaderOperationInputGesture::Command
               ? !release_allows_command(hint.command.string())
               : (hint.gesture == QuaderOperationInputGesture::Keybinding ||
                  hint.gesture == QuaderOperationInputGesture::KeybindingHold ||
                  hint.gesture == QuaderOperationInputGesture::KeybindingModifier) &&
                     !release_allows_input_target(hint.command.string());
  });
  return hints;
}

QuaderApiVoidResult QuaderToolsApi::set_setting(std::string_view tool_id,
                                                 std::string_view name,
                                                 double value,
                                                 QuaderToolSettingEditOptions
                                                     options) {
  if (!primitive_setting_available_in_release(name, value)) {
    return quader_api_failure_status(scene_session(session_),
                                     release_feature_unavailable_message());
  }
  if (!release_allows_tool_setting(tool_id, name)) {
    return quader_api_failure_status(scene_session(session_),
                                     release_feature_unavailable_message());
  }
  SceneSession &session = scene_session(session_);
  if (session.extrude().active()) {
    if (!tool_id.empty() && tool_id != "extrude") {
      return quader_api_failure_status(
          session, "Tool setting does not belong to Extrude.");
    }
    if (name == "extrude.amount") {
      return api_status_from_command(
          session, session.extrude().set_amount(value),
          QuaderMutationKind::Modeling, "Set Extrusion Amount");
    }
    if (name == "extrude.type" &&
        (value == 0.0 || value == 1.0 || value == 2.0)) {
      const auto mode = value == 1.0 ? modeling::FaceExtrudeMode::AlongNormals
                        : value == 2.0 ? modeling::FaceExtrudeMode::Individual
                                       : modeling::FaceExtrudeMode::Region;
      return api_status_from_command(
          session, session.extrude().set_mode(mode),
          QuaderMutationKind::Modeling, "Set Extrusion Type");
    }
    return quader_api_failure_status(session, "Invalid Extrusion setting.");
  }
  if (session.uv_peel().active()) {
    if (!tool_id.empty() && tool_id != "uv_peel") {
      return quader_api_failure_status(
          session, "Tool setting does not belong to UV Peel.");
    }
    ModelingUvPeelOptions peel_options = session.uv_peel().options();
    if (name == "uv_peel.axis" && (value == 0.0 || value == 1.0)) {
      peel_options.axis = value == 0.0 ? ModelingUvPeelAxis::U
                                       : ModelingUvPeelAxis::V;
    } else if (name == "uv_peel.world_space" &&
               (value == 0.0 || value == 1.0)) {
      peel_options.world_space = value == 1.0;
    } else if (name == "uv_peel.u_repeat") {
      peel_options.u_repeat = value;
    } else if (name == "uv_peel.v_repeat") {
      peel_options.v_repeat = value;
    } else if (name == "uv_peel.u_offset") {
      peel_options.u_offset = value;
    } else if (name == "uv_peel.v_offset") {
      peel_options.v_offset = value;
    } else {
      return quader_api_failure_status(session, "Invalid UV Peel setting.");
    }
    return api_status_from_command(
        session, session.uv_peel().set_options(peel_options),
        QuaderMutationKind::Runtime, "Set UV Peel Setting");
  }
  if (session.path_array().active()) {
    if (!tool_id.empty() && tool_id != kPathArrayToolId) {
      return quader_api_failure_status(
          session, "Tool setting does not belong to Path Array.");
    }
    if (name == kPathArrayCountSetting) {
      if (!std::isfinite(value) || std::round(value) != value ||
          value < static_cast<double>(kPathArrayMinimumCount) ||
          value > static_cast<double>(session.path_array().maximum_count())) {
        return quader_api_failure_status(session,
                                         "Invalid Path Array copy count.");
      }
      return api_status_from_command(
          session,
          session.path_array().set_count(static_cast<std::uint32_t>(value)),
          QuaderMutationKind::Runtime, "Set Path Array Count");
    }
    if (name == kPathArrayDistanceSetting) {
      if (!std::isfinite(value) || value < 0.0) {
        return quader_api_failure_status(session,
                                         "Invalid Path Array distance.");
      }
      return api_status_from_command(
          session, session.path_array().set_distance(value),
          QuaderMutationKind::Runtime, "Set Path Array Distance");
    }
    if (name == kPathArrayFollowPathSetting) {
      if (value != 0.0 && value != 1.0) {
        return quader_api_failure_status(session,
                                         "Invalid Path Array Follow Path.");
      }
      return api_status_from_command(
          session, session.path_array().set_follow_path(value != 0.0),
          QuaderMutationKind::Runtime, "Set Path Array Follow Path");
    }
    if (name == kPathArrayForwardAxisSetting) {
      const std::optional<PathArraySignedAxis> axis =
          path_array_axis_from_setting_value(value);
      if (!axis.has_value()) {
        return quader_api_failure_status(session,
                                         "Invalid Path Array forward axis.");
      }
      return api_status_from_command(
          session, session.path_array().set_forward_axis(*axis),
          QuaderMutationKind::Runtime, "Set Path Array Forward Axis");
    }
    if (const auto component = path_array_vector_component(
            name, kPathArrayRotationOffsetXSetting,
            kPathArrayRotationOffsetYSetting,
            kPathArrayRotationOffsetZSetting)) {
      QVec3 setting = session.path_array().rotation_offset_degrees();
      set_path_array_vector_component(setting, *component, value);
      return api_status_from_command(
          session,
          session.path_array().set_rotation_offset_degrees(setting),
          QuaderMutationKind::Runtime, "Set Path Array Rotation Offset");
    }
    if (const auto component = path_array_vector_component(
            name, kPathArrayRotationRandomMinXSetting,
            kPathArrayRotationRandomMinYSetting,
            kPathArrayRotationRandomMinZSetting)) {
      QVec3 setting = session.path_array().rotation_random_min_degrees();
      set_path_array_vector_component(setting, *component, value);
      return api_status_from_command(
          session,
          session.path_array().set_rotation_random_min_degrees(setting),
          QuaderMutationKind::Runtime,
          "Set Path Array Rotation Random Minimum");
    }
    if (const auto component = path_array_vector_component(
            name, kPathArrayRotationRandomMaxXSetting,
            kPathArrayRotationRandomMaxYSetting,
            kPathArrayRotationRandomMaxZSetting)) {
      QVec3 setting = session.path_array().rotation_random_max_degrees();
      set_path_array_vector_component(setting, *component, value);
      return api_status_from_command(
          session,
          session.path_array().set_rotation_random_max_degrees(setting),
          QuaderMutationKind::Runtime,
          "Set Path Array Rotation Random Maximum");
    }
    if (const auto component = path_array_vector_component(
            name, kPathArrayScaleOffsetXSetting,
            kPathArrayScaleOffsetYSetting, kPathArrayScaleOffsetZSetting)) {
      QVec3 setting = session.path_array().scale_offset_factors();
      set_path_array_vector_component(setting, *component, value);
      return api_status_from_command(
          session, session.path_array().set_scale_offset_factors(setting),
          QuaderMutationKind::Runtime, "Set Path Array Scale Offset");
    }
    if (const auto component = path_array_vector_component(
            name, kPathArrayScaleRandomMinXSetting,
            kPathArrayScaleRandomMinYSetting,
            kPathArrayScaleRandomMinZSetting)) {
      QVec3 setting = session.path_array().scale_random_min_factors();
      set_path_array_vector_component(setting, *component, value);
      return api_status_from_command(
          session,
          session.path_array().set_scale_random_min_factors(setting),
          QuaderMutationKind::Runtime,
          "Set Path Array Scale Random Minimum");
    }
    if (const auto component = path_array_vector_component(
            name, kPathArrayScaleRandomMaxXSetting,
            kPathArrayScaleRandomMaxYSetting,
            kPathArrayScaleRandomMaxZSetting)) {
      QVec3 setting = session.path_array().scale_random_max_factors();
      set_path_array_vector_component(setting, *component, value);
      return api_status_from_command(
          session,
          session.path_array().set_scale_random_max_factors(setting),
          QuaderMutationKind::Runtime,
          "Set Path Array Scale Random Maximum");
    }
    return quader_api_failure_status(session, "Unknown Path Array setting.");
  }
  CurveToolService &curve = session.curve();
  const bool curve_setting_target =
      curve.active() ||
      (passive_curve_settings_available(session) &&
       (tool_id.empty() || tool_id == kCurveToolId));
  if (curve_setting_target) {
    if (!tool_id.empty() && tool_id != kCurveToolId) {
      return quader_api_failure_status(
          session, "Tool setting does not belong to Curve.");
    }
    if (name == kCurveTypeSetting) {
      const std::optional<CurveCreationKind> kind =
          curve_creation_kind_from_setting_value(value);
      if (!kind.has_value()) {
        return quader_api_failure_status(session, "Invalid Curve type.");
      }
      return api_status_from_command(
          session, curve.set_creation_kind(*kind),
          QuaderMutationKind::Runtime, "Set Curve Type");
    }
    if (name == kCurveRadiusSetting) {
      return api_status_from_command(
          session, curve.set_preset_radius(value),
          QuaderMutationKind::Runtime, "Set Curve Radius");
    }
    if (name == kCurveLengthSetting) {
      return api_status_from_command(
          session, curve.set_oval_length(value),
          QuaderMutationKind::Runtime, "Set Curve Length");
    }
    if (name == kCurveSolidifySetting) {
      if ((value != 0.0 && value != 1.0) ||
          !curve.has_editable_existing_curve()) {
        return quader_api_failure_status(session,
                                         "Invalid Curve Solidify setting.");
      }
      return api_status_from_command(
          session, curve.set_solidify(value != 0.0),
          QuaderMutationKind::Document, "Set Curve Solidify");
    }
    if (name == kCurveSolidifyResolutionSetting) {
      if (!std::isfinite(value) || std::round(value) != value ||
          value < static_cast<double>(kCurveSolidifyMinimumResolution) ||
          value > static_cast<double>(kCurveSolidifyMaximumResolution) ||
          !curve.has_editable_existing_curve()) {
        return quader_api_failure_status(
            session, "Invalid Curve Solidify resolution.");
      }
      return api_status_from_command(
          session,
          curve.set_solidify_resolution(
              static_cast<std::uint32_t>(value),
              {.merge_key = std::move(options.merge_key)}),
          QuaderMutationKind::Document, "Set Curve Solidify Resolution");
    }
    if (name == kCurveSolidifyRadiusSetting) {
      if (!std::isfinite(value) || value < kCurveSolidifyMinimumRadius ||
          value > kCurveSolidifyMaximumRadius ||
          !curve.has_editable_existing_curve()) {
        return quader_api_failure_status(session,
                                         "Invalid Curve Solidify radius.");
      }
      return api_status_from_command(
          session,
          curve.set_solidify_radius(
              value, {.merge_key = std::move(options.merge_key)}),
          QuaderMutationKind::Document, "Set Curve Solidify Radius");
    }
    if (name != kCurveClosedSetting ||
        !std::isfinite(value) || (value != 0.0 && value != 1.0)) {
      return quader_api_failure_status(session, "Invalid Curve setting.");
    }
    const bool drawing = curve.drawing();
    if (drawing && curve.creation_kind() != CurveCreationKind::Draw) {
      return quader_api_failure_status(
          session, "Closed is only available for drawn Curves.");
    }
    return api_status_from_command(
        session, curve.set_closed(value != 0.0),
        drawing ? QuaderMutationKind::Runtime : QuaderMutationKind::Modeling,
        "Set Curve Closed");
  }
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
  if (radial_array_tool && name == kRadialArrayCreateInstancesSetting) {
    if (value != 0.0 && value != 1.0) {
      return quader_api_failure_status(
          session, "Invalid Radial Array Create Instances setting.");
    }
    return api_status_from_command(
        session, session.radial_array().set_create_instances(value != 0.0),
        QuaderMutationKind::Runtime, "Set Radial Array Create Instances");
  }
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
  const auto primitive_kind = name == kPrimitiveTypeSetting
                                  ? primitive_kind_from_setting_id(value)
                                  : std::nullopt;
  if ((primitive_kind && !primitive_creation_available_in_release(*primitive_kind)) ||
      (name == kSphereTypeSetting && value == "quad" &&
       !primitive_sphere_type_available_in_release(PrimitiveSphereType::Quad))) {
    return quader_api_failure_status(scene_session(session_),
                                     release_feature_unavailable_message());
  }
  if (!release_allows_tool_setting(tool_id, name)) {
    return quader_api_failure_status(scene_session(session_),
                                     release_feature_unavailable_message());
  }
  SceneSession &session = scene_session(session_);
  if (session.extrude().active() && name == "extrude.type") {
    if (value != "region" && value != "along_normals" && value != "individual") {
      return quader_api_failure_status(session, "Invalid Extrusion type.");
    }
    return set_setting(tool_id, name, value == "along_normals" ? 1.0
                                      : value == "individual" ? 2.0 : 0.0);
  }
  if (session.uv_peel().active() && name == "uv_peel.axis") {
    if (value != "u" && value != "v") {
      return quader_api_failure_status(session, "Invalid UV Peel axis.");
    }
    return set_setting(tool_id, name, value == "u" ? 0.0 : 1.0);
  }
  if ((tool_id.empty() || tool_id == kRadialArrayToolId) &&
      name == kRadialArrayModeSetting) {
    if (value != "total" && value != "spacing") {
      return quader_api_failure_status(session, "Invalid Radial Array mode.");
    }
    return api_status_from_command(
        session, session.radial_array().set_mode(
                     value == "total" ? RadialArrayMode::Total
                                      : RadialArrayMode::Spacing),
        QuaderMutationKind::Runtime, "Set Radial Array Mode");
  }
  if (session.path_array().active() &&
      name == kPathArrayForwardAxisSetting) {
    if (!tool_id.empty() && tool_id != kPathArrayToolId) {
      return quader_api_failure_status(
          session, "Tool setting does not belong to Path Array.");
    }
    const std::optional<PathArraySignedAxis> axis =
        path_array_axis_from_setting_id(value);
    if (!axis.has_value()) {
      return quader_api_failure_status(session,
                                       "Invalid Path Array forward axis.");
    }
    return api_status_from_command(
        session, session.path_array().set_forward_axis(*axis),
        QuaderMutationKind::Runtime, "Set Path Array Forward Axis");
  }
  if (session.curve().active() && name == kCurveTypeSetting) {
    if (!tool_id.empty() && tool_id != kCurveToolId) {
      return quader_api_failure_status(
          session, "Tool setting does not belong to Curve.");
    }
    const std::optional<CurveCreationKind> kind =
        curve_creation_kind_from_setting_id(value);
    if (!kind.has_value()) {
      return quader_api_failure_status(session, "Invalid Curve type.");
    }
    return set_setting(tool_id, name, curve_creation_kind_setting_value(*kind));
  }
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
  if (session.primitive_creation().active() && name == kSphereTypeSetting &&
      (value == "uv" || value == "quad")) {
    return set_setting(tool_id, name, value == "uv" ? 0.0 : 1.0);
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
  if (!release_allows_tool_setting(tool_id, action_id)) {
    return quader_api_failure_status(scene_session(session_),
                                     release_feature_unavailable_message());
  }
  SceneSession &session = scene_session(session_);
  if (tool_id == "extrude" && action_id == "extrude.apply") {
    const QuaderSettingsOperationOriginCapture origin(session_);
    auto result = api_status_from_command(
        session, session.extrude().activate_preview(),
        QuaderMutationKind::Modeling, "Extrude");
    if (result.ok() && result.changed()) {
      origin.publish();
    }
    return result;
  }
  if (tool_id == kCurveToolId && action_id == kCurveNewAction) {
    return api_status_from_command(session, session.curve().new_curve(),
                                    QuaderMutationKind::Runtime, "New Curve");
  }
  if (tool_id == kCurveToolId && action_id == kCurveApplySolidifyAction) {
    return api_status_from_command(
        session, session.curve().apply_solidify(),
        QuaderMutationKind::Document, "Apply Curve Solidify");
  }
  if (tool_id == kPivotToolId) {
    return api_status_from_command(
        session, session.pivot().execute_align_action(action_id),
        QuaderMutationKind::Runtime, "Pivot Action");
  }
  return quader_api_failure_status(session, "Unknown tool action.");
}

QuaderApiVoidResult QuaderToolsApi::reset_active_settings() {
  SceneSession &session = scene_session(session_);
  if (session.extrude().active()) {
    return api_status_from_command(
        session, session.extrude().reset_settings(),
        QuaderMutationKind::Modeling, "Reset Extrusion Settings");
  }
  if (session.uv_peel().active()) {
    return api_status_from_command(
        session, session.uv_peel().reset_settings(),
        QuaderMutationKind::Runtime, "Reset UV Peel Settings");
  }
  if (session.path_array().active()) {
    return api_status_from_command(
        session, session.path_array().reset_settings(),
        QuaderMutationKind::Runtime, "Reset Path Array Settings");
  }
  if (session.curve().active()) {
    return api_status_from_command(
        session, session.curve().reset_settings(),
        QuaderMutationKind::Runtime, "Reset Curve Settings");
  }
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
  if (passive_curve_settings_available(session)) {
    return quader_api_failure_status(
        session, "Selected Curve settings do not have a reset action.");
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
  const MaterialReference material = default_material != kInvalidAssetId
      ? MaterialReference(default_material)
      : default_project_material(session_);
  return api_status_from_command(
      session,
      session.native_tools().accept_active_primitive(std::move(name),
                                                      material,
                                                      snap_enabled),
      QuaderMutationKind::Modeling, "Create Primitive");
}

QuaderApiVoidResult QuaderToolsApi::accept_active_primitive(
    std::string name, QuaderMaterialReference material, bool snap_enabled) {
  SceneSession &session = scene_session(session_);
  return api_status_from_command(
      session,
      session.native_tools().accept_active_primitive(
          std::move(name), primitive_creation_material(session_, material),
          snap_enabled),
      QuaderMutationKind::Modeling, "Create Primitive");
}

bool QuaderToolsApi::has_active_primitive_creation() const {
  return scene_session(session_).primitive_creation().active();
}

QuaderPrimitiveCreationShape QuaderToolsApi::preferred_primitive_shape() const {
  return primitive_creation_shape(
             scene_session(session_).primitive_creation().preferred_kind())
      .value_or(QuaderPrimitiveCreationShape::Box);
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

std::optional<QuaderPrimitiveCreationState>
QuaderToolsApi::live_primitive_creation() const {
  const SceneSession &session = scene_session(session_);
  const std::optional<PrimitiveCreationState> primitive =
      session.primitive_creation().live_primitive_creation();
  if (!primitive.has_value()) {
    primitive_creation_state_.reset();
    return std::nullopt;
  }
  const std::uint64_t transform_revision = session.scene().transform_revision();
  const std::uint64_t selection_revision =
      session.scene().selection_revision() + session.modeling().selection_revision();
  const std::uint64_t instance_revision =
      session.instances().instance_edit_revision();
  if (primitive_creation_state_.has_value() &&
      primitive_creation_owner_revision_ == primitive->revision &&
      primitive_creation_transform_revision_ == transform_revision &&
      primitive_creation_selection_revision_ == selection_revision &&
      primitive_creation_instance_revision_ == instance_revision) {
    return primitive_creation_state_;
  }
  const std::optional<EntityId> entity =
      session.modeling().entity_for_modeling_object(primitive->modeling_object_id);
  if (!entity.has_value() || !session.scene().exists(*entity)) {
    primitive_creation_state_.reset();
    return std::nullopt;
  }
  const std::optional<QMat4> occurrence_world =
      session.instances().effective_member_world_transform(*entity);
  const QMat4 world = occurrence_world.has_value()
                          ? *occurrence_world
                          : session.scene().world_transform_matrix(*entity);
  const auto shape = primitive_creation_shape(primitive->kind);
  if (!shape) {
    return std::nullopt;
  }
  primitive_creation_state_ = QuaderPrimitiveCreationState{
      .object_id = primitive->modeling_object_id,
      .shape = *shape,
      .local_min = primitive->local_min,
      .local_max = primitive->local_max,
      .world_origin = transform_point(world, {}),
      .world_x = transform_vector(world, {1.0f, 0.0f, 0.0f}),
      .world_y = transform_vector(world, {0.0f, 1.0f, 0.0f}),
      .world_z = transform_vector(world, {0.0f, 0.0f, 1.0f}),
      .subdivisions = primitive->subdivisions,
      .side_count = primitive->side_count,
      .caps = primitive->caps,
      .radial_segments = primitive->radial_segments,
      .rings = primitive->rings,
      .hemisphere = primitive->hemisphere,
      .major_segments = primitive->major_segments,
      .minor_segments = primitive->minor_segments,
      .tube_radius_ratio = primitive->tube_radius_ratio,
      .flip_faces = primitive->flip_faces,
      .resizing = primitive->resizing,
      .revision = ++primitive_creation_revision_,
      .sphere_type = primitive->sphere_type == PrimitiveSphereType::Quad
                         ? QuaderSphereType::Quad
                         : QuaderSphereType::Uv,
      .sphere_subdivision = primitive->sphere_subdivision,
  };
  primitive_creation_owner_revision_ = primitive->revision;
  primitive_creation_transform_revision_ = transform_revision;
  primitive_creation_selection_revision_ = selection_revision;
  primitive_creation_instance_revision_ = instance_revision;
  return primitive_creation_state_;
}

std::optional<QuaderPrimitiveCubeState>
QuaderToolsApi::live_primitive_cube() const {
  const auto primitive = live_primitive_creation();
  if (!primitive || primitive->shape != QuaderPrimitiveCreationShape::Box) {
    return std::nullopt;
  }
  return QuaderPrimitiveCubeState{
      .object_id = primitive->object_id,
      .local_min = primitive->local_min,
      .local_max = primitive->local_max,
      .world_origin = primitive->world_origin,
      .world_x = primitive->world_x,
      .world_y = primitive->world_y,
      .world_z = primitive->world_z,
      .subdivisions = primitive->subdivisions,
      .flip_faces = primitive->flip_faces,
      .resizing = primitive->resizing,
      .revision = primitive->revision,
  };
}

QuaderApiVoidResult QuaderToolsApi::begin_primitive_resize(
    QuaderPrimitiveResizeControl control, QVec3 radial_axis) {
  SceneSession &session = scene_session(session_);
  PrimitiveResizeControl native_control;
  switch (control) {
  case QuaderPrimitiveResizeControl::PositiveX:
    native_control = PrimitiveResizeControl::PositiveX;
    break;
  case QuaderPrimitiveResizeControl::NegativeX:
    native_control = PrimitiveResizeControl::NegativeX;
    break;
  case QuaderPrimitiveResizeControl::PositiveY:
    native_control = PrimitiveResizeControl::PositiveY;
    break;
  case QuaderPrimitiveResizeControl::NegativeY:
    native_control = PrimitiveResizeControl::NegativeY;
    break;
  case QuaderPrimitiveResizeControl::PositiveZ:
    native_control = PrimitiveResizeControl::PositiveZ;
    break;
  case QuaderPrimitiveResizeControl::NegativeZ:
    native_control = PrimitiveResizeControl::NegativeZ;
    break;
  case QuaderPrimitiveResizeControl::Radius:
    native_control = PrimitiveResizeControl::Radius;
    break;
  case QuaderPrimitiveResizeControl::OuterRadius:
    native_control = PrimitiveResizeControl::OuterRadius;
    break;
  case QuaderPrimitiveResizeControl::InnerRadius:
    native_control = PrimitiveResizeControl::InnerRadius;
    break;
  default:
    return quader_api_failure_status(session, "Invalid primitive resize control.");
  }
  return api_status_from_command(
      session, session.primitive_creation().begin_resize(native_control, radial_axis),
      QuaderMutationKind::Runtime, "Begin Primitive Resize");
}

QuaderApiVoidResult QuaderToolsApi::update_primitive_resize(double displacement) {
  SceneSession &session = scene_session(session_);
  return api_status_from_command(
      session, session.primitive_creation().update_resize(displacement),
      QuaderMutationKind::Modeling, "Resize Primitive");
}

QuaderApiVoidResult QuaderToolsApi::end_primitive_resize() {
  SceneSession &session = scene_session(session_);
  return api_status_from_command(
      session, session.primitive_creation().end_resize(),
      QuaderMutationKind::Runtime, "End Primitive Resize");
}

QuaderApiVoidResult QuaderToolsApi::cancel_primitive_resize() {
  SceneSession &session = scene_session(session_);
  return api_status_from_command(
      session, session.primitive_creation().cancel_resize(),
      QuaderMutationKind::Modeling, "Cancel Primitive Resize");
}

QuaderApiVoidResult QuaderToolsApi::toggle_primitive_caps() {
  SceneSession &session = scene_session(session_);
  return api_status_from_command(
      session, session.primitive_creation().toggle_caps(),
      QuaderMutationKind::Runtime, "Toggle Primitive Caps");
}

QuaderApiVoidResult QuaderToolsApi::toggle_primitive_hemisphere() {
  SceneSession &session = scene_session(session_);
  return api_status_from_command(
      session, session.primitive_creation().toggle_hemisphere(),
      QuaderMutationKind::Runtime, "Toggle Primitive Hemisphere");
}

QuaderApiVoidResult QuaderToolsApi::begin_primitive_cube_resize(
    QuaderPrimitiveCubeAxis axis, bool positive) {
  SceneSession &session = scene_session(session_);
  PrimitiveCubeAxis native_axis;
  switch (axis) {
  case QuaderPrimitiveCubeAxis::X:
    native_axis = PrimitiveCubeAxis::X;
    break;
  case QuaderPrimitiveCubeAxis::Y:
    native_axis = PrimitiveCubeAxis::Y;
    break;
  case QuaderPrimitiveCubeAxis::Z:
    native_axis = PrimitiveCubeAxis::Z;
    break;
  default:
    return quader_api_failure_status(session, "Invalid cube resize axis.");
  }
  return api_status_from_command(
      session, session.primitive_creation().begin_cube_resize(native_axis, positive),
      QuaderMutationKind::Runtime, "Begin Cube Resize");
}

QuaderApiVoidResult
QuaderToolsApi::update_primitive_cube_resize(double displacement) {
  SceneSession &session = scene_session(session_);
  return api_status_from_command(
      session, session.primitive_creation().update_cube_resize(displacement),
      QuaderMutationKind::Modeling, "Resize Cube");
}

QuaderApiVoidResult QuaderToolsApi::end_primitive_cube_resize() {
  SceneSession &session = scene_session(session_);
  return api_status_from_command(
      session, session.primitive_creation().end_cube_resize(),
      QuaderMutationKind::Runtime, "End Cube Resize");
}

QuaderApiVoidResult QuaderToolsApi::cancel_primitive_cube_resize() {
  SceneSession &session = scene_session(session_);
  return api_status_from_command(
      session, session.primitive_creation().cancel_cube_resize(),
      QuaderMutationKind::Modeling, "Cancel Cube Resize");
}

QuaderApiVoidResult QuaderToolsApi::flip_primitive_faces() {
  SceneSession &session = scene_session(session_);
  return api_status_from_command(
      session, session.primitive_creation().flip_faces(),
      QuaderMutationKind::Runtime, "Flip Primitive Faces");
}

QuaderApiVoidResult QuaderToolsApi::commit_active_tool_preview() {
  SceneSession &session = scene_session(session_);
  if (const std::optional<ViewportNativeToolCommitMetadata> metadata =
          session.native_tools().active_commit_metadata()) {
    return api_status_from_command(
        session, session.native_tools().commit_active(),
        metadata->domain == ViewportNativeToolCommitDomain::Modeling
            ? QuaderMutationKind::Modeling
            : QuaderMutationKind::Document,
        metadata->label);
  }
  if (hooks_.commit_active_tool_preview) {
    return hooks_.commit_active_tool_preview();
  }
  return quader_api_failure_status(session, "No active tool preview.");
}

QuaderApiVoidResult QuaderToolsApi::commit_modeling_tool(ViewportToolId tool) {
  if (!release_allows_tool(viewport_tool_catalog_id(tool))) {
    return quader_api_failure_status(scene_session(session_),
                                     release_feature_unavailable_message());
  }
  QuaderApiMutationRunner mutations(session_);
  SceneSession &session = scene_session(session_);
  if (!mutations.available()) {
    return quader_api_failure_status(session, "No scene is active.");
  }
  if (!mutations.can_mutate_modeling_content()) {
    return mutations.modeling_content_blocked_status(
        "committing a viewport modeling tool.");
  }
  const ToolDescriptor *descriptor = find_viewport_tool_descriptor(tool);
  if (descriptor == nullptr) {
    return quader_api_failure_status(session, "Unknown viewport tool.");
  }
  ModelingCommandExecutionService execution;
  ModelingCommandResult result = execution.commit_viewport_tool(
      session.history(), session.modeling(), descriptor->modeling_tool,
      std::string(descriptor->label),
      default_project_material(session_));
  return api_status_from_command(
      session, command_result_from_modeling(std::move(result)),
      QuaderMutationKind::Modeling, descriptor->label);
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

bool QuaderToolsApi::active_native_tool_lifecycle(
    ViewportToolId tool) const {
  return scene_session(session_)
      .native_tools()
      .runtime()
      .native_lifecycle_active(viewport_tool_id(tool));
}

std::string QuaderToolsApi::active_tool() const {
  return std::string(active_tool_id(scene_session(session_)));
}

} // namespace quader

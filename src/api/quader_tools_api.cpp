////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_tools_api.hpp"
#include "api/quader_api_session_internal_access.hpp"

#include "api/quader_api_status_internal.hpp"
#include "editor/session/project_session.hpp"
#include "editor/modeling/services/modeling_operation_settings.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/pivot/pivot_tool_service.hpp"
#include "editor/tools/viewport_tool_catalog.hpp"

#include <optional>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader {
namespace {

ProjectSession &project_session(QuaderApiSession session) {
  return QuaderApiSessionInternalAccess::project_session(session);
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

QuaderApiVoidResult api_status_from_command(ProjectSession &session,
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
  case ModelingToolSettingsTargetKind::None:
    break;
  }
  return {};
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
  result.can_apply = target.operation_preview_active;
  result.can_cancel = target.operation_preview_active;

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
  case ModelingToolSettingsTargetKind::EdgeBevel:
    result.controls.push_back(number_control(
        "operation.edge_bevel.width", "Width", settings.edge_bevel_width,
        0.0625, 16384.0, 0.001));
    result.controls.push_back(number_control(
        "operation.edge_bevel.segments", "Segments",
        settings.edge_bevel_segments, 1.0, 16.0, 1.0));
    result.controls.push_back(number_control(
        "operation.edge_bevel.profile", "Profile",
        settings.edge_bevel_profile, 0.0, 1.0, 0.01));
    break;
  case ModelingToolSettingsTargetKind::Bridge:
    result.controls.push_back(number_control("operation.bridge.steps", "Steps",
                                             settings.bridge_steps, 1.0, 64.0,
                                             1.0));
    break;
  case ModelingToolSettingsTargetKind::Thicken:
    result.controls.push_back(number_control(
        "operation.thicken.thickness", "Distance",
        settings.thicken_thickness, -4.0, 4.0, 0.001));
    result.controls.push_back(toggle_control("operation.thicken.from_center",
                                             "From Center",
                                             settings.thicken_from_center));
    break;
  case ModelingToolSettingsTargetKind::SliceQuad:
    result.controls.push_back(number_control(
        "operation.slice_quad.x_slices", "X", settings.slice_quad_x_slices,
        0.0, 64.0, 1.0));
    result.controls.push_back(number_control(
        "operation.slice_quad.y_slices", "Y", settings.slice_quad_y_slices,
        0.0, 64.0, 1.0));
    break;
  case ModelingToolSettingsTargetKind::MergeByDistance:
    result.controls.push_back(number_control(
        "operation.merge_by_distance.tolerance", "Tolerance",
        settings.merge_by_distance_tolerance, 0.0, 128.0, 0.0001));
    break;
  case ModelingToolSettingsTargetKind::Mirror:
  case ModelingToolSettingsTargetKind::None:
    break;
  }

  return result;
}

} // namespace

QuaderToolsApi::QuaderToolsApi(QuaderApiSession session)
    : session_(session) {}

void QuaderToolsApi::set_runtime_hooks(QuaderToolsRuntimeHooks hooks) {
  hooks_ = std::move(hooks);
}

QuaderApiVoidResult QuaderToolsApi::activate(std::string_view tool_id) {
  if (hooks_.activate) {
    return hooks_.activate(tool_id);
  }

  ProjectSession &session = project_session(session_);
  if (tool_id == kPivotToolId) {
    return api_status_from_command(
        session, session.pivot().activate_persistent(session.selection()),
        QuaderMutationKind::Runtime, "Pivot");
  }

  const ViewportTool *tool = find_viewport_tool(tool_id);
  if (tool == nullptr) {
    return quader_api_failure_status(session, "Unknown viewport tool.");
  }
  ModelingCommandResult result = tool->activate(session.modeling());
  return quader_api_void_status(session, result.ok,
                                result.changed, std::move(result.message),
                                QuaderMutationKind::Modeling, tool_id);
}

QuaderApiVoidResult QuaderToolsApi::activate(ViewportToolId tool) {
  return activate(viewport_tool_catalog_id(tool));
}

QuaderToolSettingsTarget QuaderToolsApi::active_settings() const {
  const ProjectSession &session = project_session(session_);
  if (session.pivot().persistent_active()) {
    return pivot_tool_settings_target(session.pivot());
  }
  const ModelingSessionService &modeling = session.modeling();
  return modeling_tool_settings_target(modeling.tool_settings_target(),
                                       modeling.interaction_settings());
}

QuaderApiVoidResult QuaderToolsApi::set_setting(std::string_view tool_id,
                                                 std::string_view name,
                                                 double value) {
  ProjectSession &session = project_session(session_);
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
  char *end = nullptr;
  const std::string text(value);
  const double numeric = std::strtod(text.c_str(), &end);
  if (end == text.c_str() || (end != nullptr && *end != '\0')) {
    return quader_api_failure_status(project_session(session_),
                                     "Invalid tool setting value.");
  }
  return set_setting(tool_id, name, numeric);
}

QuaderApiVoidResult QuaderToolsApi::execute_action(std::string_view tool_id,
                                                    std::string_view action_id) {
  ProjectSession &session = project_session(session_);
  if (tool_id == kPivotToolId) {
    return api_status_from_command(
        session, session.pivot().execute_align_action(action_id),
        QuaderMutationKind::Runtime, "Pivot Action");
  }
  return quader_api_failure_status(session, "Unknown tool action.");
}

QuaderApiVoidResult QuaderToolsApi::commit_active_tool_preview() {
  ProjectSession &session = project_session(session_);
  if (session.pivot().persistent_active()) {
    return api_status_from_command(session, session.pivot().apply_persistent(),
                                   QuaderMutationKind::Document,
                                   "Apply Pivot");
  }
  if (hooks_.commit_active_tool_preview) {
    return hooks_.commit_active_tool_preview();
  }
  return quader_api_failure_status(session, "No active tool preview.");
}

QuaderApiVoidResult QuaderToolsApi::cancel_active_tool_preview() {
  ProjectSession &session = project_session(session_);
  if (session.pivot().persistent_active()) {
    return api_status_from_command(session, session.pivot().cancel_persistent(),
                                   QuaderMutationKind::Runtime,
                                   "Cancel Pivot");
  }
  if (hooks_.cancel_active_tool_preview) {
    return hooks_.cancel_active_tool_preview();
  }
  return quader_api_failure_status(session, "No active tool preview.");
}

QuaderApiVoidResult QuaderToolsApi::cancel_modal_tool() {
  if (hooks_.cancel_modal_tool) {
    return hooks_.cancel_modal_tool();
  }
  ModelingCommandResult result =
      project_session(session_).modeling().cancel_active_modal_tool();
  return quader_api_void_status(project_session(session_), result.ok,
                                result.changed, std::move(result.message),
                                QuaderMutationKind::Modeling,
                                "Cancel Modal Tool");
}

std::string QuaderToolsApi::active_tool() const {
  const ProjectSession &session = project_session(session_);
  if (session.pivot().persistent_active()) {
    return std::string(kPivotToolId);
  }
  return std::string(viewport_tool_catalog_id(viewport_tool_for_modeling(
      session.modeling().tool_status().active_tool)));
}

} // namespace quader

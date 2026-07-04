////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/tool_settings_panel.hpp"

#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "core/logger.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "ui/imgui_quader_widgets.hpp"

#include <imgui.h>

#include <string>
#include <string_view>

namespace quader {
namespace {

std::string
localized_tool_settings_title(const LocalizationService &localization,
                              const ModelingToolSettingsTarget &target) {
  if (!target.action_id.empty()) {
    std::string key = "modeling.action.";
    key.append(target.action_id);
    key.append(".label");
    return localization.text(key, target.label);
  }
  if (!target.operation_id.empty()) {
    std::string key = "modeling.operation.";
    key.append(target.operation_id);
    key.append(".label");
    return localization.text(key, target.label);
  }
  if (!target.label.empty()) {
    switch (target.kind) {
    case ModelingToolSettingsTargetKind::Cut:
      return localization.text("tool_settings.target.cut", target.label);
    case ModelingToolSettingsTargetKind::Mirror:
      return localization.text("tool_settings.target.mirror", target.label);
    case ModelingToolSettingsTargetKind::Bridge:
      return localization.text("tool_settings.target.bridge", target.label);
    case ModelingToolSettingsTargetKind::EdgeBevel:
      return localization.text("tool_settings.target.edge_bevel", target.label);
    case ModelingToolSettingsTargetKind::Thicken:
      return localization.text("tool_settings.target.thicken", target.label);
    case ModelingToolSettingsTargetKind::SliceQuad:
      return localization.text("tool_settings.target.slice_quad", target.label);
    case ModelingToolSettingsTargetKind::MergeByDistance:
      return localization.text("tool_settings.target.merge_by_distance",
                               target.label);
    case ModelingToolSettingsTargetKind::None:
      break;
    }
    return target.label;
  }
  return localization.text("ui.panel.tool_settings", "Tool Settings");
}

std::string_view tool_settings_target_id(
    const ModelingToolSettingsTarget &target, std::string_view fallback) {
  if (!target.operation_id.empty()) {
    return target.operation_id;
  }
  return fallback;
}

} // namespace

void ToolSettingsPanel::draw(UIContext& context, bool& open) {
  const ModelingSessionService &modeling = context.scene.modeling;
  const ModelingToolSettingsTarget target =
      modeling.tool_settings_target();
  if (!target.active) {
    open = false;
    should_open_for_active_target_ = false;
    return;
  }
  should_open_for_active_target_ = !open;
  if (!open) {
    open = true;
  }
  draw_floating(context, open);
  should_open_for_active_target_ = false;
}

void ToolSettingsPanel::before_begin(UIContext& context, bool& open) {
    (void)open;
    if (!should_open_for_active_target_) {
      return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowPos(
            {viewport->WorkPos.x + 24.0f, viewport->WorkPos.y + 72.0f},
            ImGuiCond_Always);
}

void ToolSettingsPanel::draw_content(UIContext& context, bool& open) {
  const ModelingSessionService &modeling = context.scene.modeling;
  ModelingToolSettingsTarget target = modeling.tool_settings_target();
  const ModelingInteractionSettings settings =
      modeling.interaction_settings();
  bool setting_write_attempted = false;

  auto apply_tool_setting_update = [&](std::string_view tool_id,
                                       std::string_view name, double value) {
    setting_write_attempted = true;
    const QuaderApiVoidResult result =
        context.intent.editor.tools().set_setting(tool_id, name, value);
    if (!result.ok) {
      LOG_ERROR("Tool settings update failed: {}",
                result.message.empty() ? "Command failed." : result.message);
    }
  };

  const std::string title =
      localized_tool_settings_title(context.localization, target);
  ImGui::TextUnformatted(title.c_str());
  ImGui::Separator();

  switch (target.kind) {
  case ModelingToolSettingsTargetKind::Cut: {
    const ModelingToolStatus status = modeling.tool_status();
    int cut_mode = static_cast<int>(settings.cut_mode);
    const std::string keep_mode =
        context.localization.text("tool_settings.cut.keep_mode", "Keep Mode");
    ImGui::TextUnformatted(keep_mode.c_str());
    const std::string discard_left = context.localization.imgui_label(
        "tool_settings.cut.discard_left", "tool_settings.cut.discard_left",
        "Discard Left");
    if (ImGui::RadioButton(
            discard_left.c_str(),
            cut_mode == static_cast<int>(ModelingCutMode::DiscardLeft))) {
      cut_mode = static_cast<int>(ModelingCutMode::DiscardLeft);
      apply_tool_setting_update("cut", "operation.cut.keep_mode",
                                static_cast<double>(cut_mode));
    }
    const std::string discard_right = context.localization.imgui_label(
        "tool_settings.cut.discard_right", "tool_settings.cut.discard_right",
        "Discard Right");
    if (ImGui::RadioButton(
            discard_right.c_str(),
            cut_mode == static_cast<int>(ModelingCutMode::DiscardRight))) {
      cut_mode = static_cast<int>(ModelingCutMode::DiscardRight);
      apply_tool_setting_update("cut", "operation.cut.keep_mode",
                                static_cast<double>(cut_mode));
    }
    const std::string keep_both = context.localization.imgui_label(
        "tool_settings.cut.keep_both", "tool_settings.cut.keep_both",
        "Keep Both");
    if (ImGui::RadioButton(keep_both.c_str(),
                           cut_mode ==
                               static_cast<int>(ModelingCutMode::KeepBoth))) {
      cut_mode = static_cast<int>(ModelingCutMode::KeepBoth);
      apply_tool_setting_update("cut", "operation.cut.keep_mode",
                                static_cast<double>(cut_mode));
    }
    if (status.cut_active) {
      const std::string preview_points = context.localization.text(
          "tool_settings.cut.preview_points", "Preview points");
      ImGui::Text("%s: %d", preview_points.c_str(), status.cut_point_count);
    }
    break;
  }
    case ModelingToolSettingsTargetKind::EdgeBevel: {
      float width = static_cast<float>(settings.edge_bevel_width);
      float profile = static_cast<float>(settings.edge_bevel_profile);
      int segments = settings.edge_bevel_segments;
      ImGui::SetNextItemWidth(180.0f);
      const std::string width_label = context.localization.imgui_label(
          "operation.edge_bevel.width", "operation.edge_bevel.width", "Width");
      if (quader_slider_float(width_label.c_str(), &width, 0.0625f, 16384.0f,
                              "%.3f")) {
        apply_tool_setting_update(tool_settings_target_id(target, "edge_bevel"),
                                  "operation.edge_bevel.width", width);
      }
        ImGui::SetNextItemWidth(180.0f);
        const std::string segments_label = context.localization.imgui_label(
            "operation.edge_bevel.segments", "operation.edge_bevel.segments",
            "Segments");
        if (quader_slider_int(segments_label.c_str(), &segments, 1, 16)) {
          apply_tool_setting_update(
              tool_settings_target_id(target, "edge_bevel"),
              "operation.edge_bevel.segments", segments);
        }
        ImGui::SetNextItemWidth(180.0f);
        const std::string profile_label = context.localization.imgui_label(
            "operation.edge_bevel.profile", "operation.edge_bevel.profile",
            "Profile");
        if (quader_slider_float(profile_label.c_str(), &profile, 0.0f, 1.0f,
                                "%.2f")) {
          apply_tool_setting_update(
              tool_settings_target_id(target, "edge_bevel"),
              "operation.edge_bevel.profile", profile);
        }
        break;
    }
    case ModelingToolSettingsTargetKind::Bridge: {
      int steps = settings.bridge_steps;
      ImGui::SetNextItemWidth(180.0f);
      const std::string steps_label = context.localization.imgui_label(
          "operation.bridge.steps", "operation.bridge.steps", "Steps");
      if (quader_slider_int(steps_label.c_str(), &steps, 1, 64)) {
        apply_tool_setting_update(tool_settings_target_id(target, "bridge"),
                                  "operation.bridge.steps", steps);
      }
        break;
    }
    case ModelingToolSettingsTargetKind::Thicken: {
      float thickness = static_cast<float>(settings.thicken_thickness);
      ImGui::SetNextItemWidth(180.0f);
      const std::string distance_label = context.localization.imgui_label(
          "operation.thicken.distance", "operation.thicken.distance",
          "Distance");
      if (quader_slider_float(distance_label.c_str(), &thickness, -4.0f, 4.0f,
                              "%.3f")) {
        apply_tool_setting_update(tool_settings_target_id(target, "thicken"),
                                  "operation.thicken.thickness", thickness);
      }
        break;
    }
    case ModelingToolSettingsTargetKind::SliceQuad: {
      int x_slices = settings.slice_quad_x_slices;
      int y_slices = settings.slice_quad_y_slices;
      ImGui::SetNextItemWidth(180.0f);
      const std::string x_label = context.localization.imgui_label(
          "operation.slice_quad.x_slices", "operation.slice_quad.x_slices",
          "X");
      if (quader_slider_int(x_label.c_str(), &x_slices, 0, 64)) {
        apply_tool_setting_update(tool_settings_target_id(target, "slice_quad"),
                                  "operation.slice_quad.x_slices", x_slices);
      }
        ImGui::SetNextItemWidth(180.0f);
        const std::string y_label = context.localization.imgui_label(
            "operation.slice_quad.y_slices", "operation.slice_quad.y_slices",
            "Y");
        if (quader_slider_int(y_label.c_str(), &y_slices, 0, 64)) {
          apply_tool_setting_update(
              tool_settings_target_id(target, "slice_quad"),
              "operation.slice_quad.y_slices", y_slices);
        }
        break;
    }
    case ModelingToolSettingsTargetKind::MergeByDistance: {
      float tolerance =
          static_cast<float>(settings.merge_by_distance_tolerance);
      ImGui::SetNextItemWidth(180.0f);
      const std::string tolerance_label = context.localization.imgui_label(
          "operation.merge_by_distance.tolerance",
          "operation.merge_by_distance.tolerance", "Tolerance");
      if (quader_slider_float(tolerance_label.c_str(), &tolerance, 0.0f,
                              128.0f, "%.4f")) {
        apply_tool_setting_update(
            tool_settings_target_id(target, "merge_by_distance"),
            "operation.merge_by_distance.tolerance", tolerance);
      }
        break;
    }
    case ModelingToolSettingsTargetKind::None:
        break;
    }

    if (setting_write_attempted) {
      target = modeling.tool_settings_target();
    }
    if (!target.active) {
        open = false;
    }

    if (target.operation_preview_active) {
      ImGui::Separator();
      const std::string apply_label = context.localization.imgui_label(
          "ui.action.apply", "ui.action.apply", "Apply");
      if (ImGui::Button(apply_label.c_str())) {
        const std::string history_label = context.localization.format(
            "history.apply_operation", "Apply {operation}",
            {{"operation", title}});
        const QuaderApiVoidResult result =
            context.intent.editor.modeling().commit_preview(history_label);
        if (!result.ok) {
          LOG_ERROR("Tool preview apply failed: {}", result.message.empty()
                                                         ? "Command failed."
                                                         : result.message);
        }
        open =
            result.ok ? false : modeling.tool_settings_target().active;
      }
      ImGui::SameLine();
      const std::string cancel_label = context.localization.imgui_label(
          "ui.action.cancel", "ui.action.cancel", "Cancel");
      if (ImGui::Button(cancel_label.c_str())) {
        const QuaderApiVoidResult result =
            context.intent.editor.modeling().cancel_preview();
        if (!result.ok) {
          LOG_ERROR("Tool preview cancel failed: {}", result.message.empty()
                                                          ? "Command failed."
                                                          : result.message);
        }
        open =
            result.ok ? false : modeling.tool_settings_target().active;
      }
    }
}

} // namespace quader

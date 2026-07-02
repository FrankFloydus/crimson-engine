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

#include <imgui.h>

#include <string>

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

} // namespace

void ToolSettingsPanel::draw(UIContext& context, bool& open) {
  const ModelingToolSettingsTarget target =
      context.modeling.tool_settings_target();
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
  ModelingToolSettingsTarget target = context.modeling.tool_settings_target();
  ModelingInteractionSettings settings =
      context.modeling.interaction_settings();
  bool changed = false;

  auto apply_settings_update = [&]() {
    if (!changed) {
      return;
    }
    const ModelingCommandResult result =
        context.modeling.set_interaction_settings(settings);
    if (!result.ok) {
      LOG_ERROR("Tool settings update failed: {}",
                result.message.empty() ? "Command failed." : result.message);
    }
    changed = false;
    target = context.modeling.tool_settings_target();
  };

  const std::string title =
      localized_tool_settings_title(context.localization, target);
  ImGui::TextUnformatted(title.c_str());
  ImGui::Separator();

  switch (target.kind) {
  case ModelingToolSettingsTargetKind::Cut: {
    const ModelingToolStatus status = context.modeling.tool_status();
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
      settings.cut_mode = ModelingCutMode::DiscardLeft;
      changed = true;
    }
    const std::string discard_right = context.localization.imgui_label(
        "tool_settings.cut.discard_right", "tool_settings.cut.discard_right",
        "Discard Right");
    if (ImGui::RadioButton(
            discard_right.c_str(),
            cut_mode == static_cast<int>(ModelingCutMode::DiscardRight))) {
      cut_mode = static_cast<int>(ModelingCutMode::DiscardRight);
      settings.cut_mode = ModelingCutMode::DiscardRight;
      changed = true;
    }
    const std::string keep_both = context.localization.imgui_label(
        "tool_settings.cut.keep_both", "tool_settings.cut.keep_both",
        "Keep Both");
    if (ImGui::RadioButton(keep_both.c_str(),
                           cut_mode ==
                               static_cast<int>(ModelingCutMode::KeepBoth))) {
      cut_mode = static_cast<int>(ModelingCutMode::KeepBoth);
      settings.cut_mode = ModelingCutMode::KeepBoth;
      changed = true;
    }
    if (settings.cut_mode != static_cast<ModelingCutMode>(cut_mode)) {
      settings.cut_mode = static_cast<ModelingCutMode>(cut_mode);
      changed = true;
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
      if (ImGui::DragFloat(width_label.c_str(), &width, 0.05f, 0.0625f,
                           16384.0f, "%.3f")) {
        settings.edge_bevel_width = width;
        changed = true;
      }
        ImGui::SetNextItemWidth(180.0f);
        const std::string segments_label = context.localization.imgui_label(
            "operation.edge_bevel.segments", "operation.edge_bevel.segments",
            "Segments");
        if (ImGui::SliderInt(segments_label.c_str(), &segments, 1, 16)) {
          settings.edge_bevel_segments = segments;
          changed = true;
        }
        ImGui::SetNextItemWidth(180.0f);
        const std::string profile_label = context.localization.imgui_label(
            "operation.edge_bevel.profile", "operation.edge_bevel.profile",
            "Profile");
        if (ImGui::SliderFloat(profile_label.c_str(), &profile, 0.0f, 1.0f,
                               "%.2f")) {
          settings.edge_bevel_profile = profile;
          changed = true;
        }
        break;
    }
    case ModelingToolSettingsTargetKind::Bridge: {
      int steps = settings.bridge_steps;
      ImGui::SetNextItemWidth(180.0f);
      const std::string steps_label = context.localization.imgui_label(
          "operation.bridge.steps", "operation.bridge.steps", "Steps");
      if (ImGui::SliderInt(steps_label.c_str(), &steps, 1, 64)) {
        settings.bridge_steps = steps;
        changed = true;
      }
        break;
    }
    case ModelingToolSettingsTargetKind::Thicken: {
      float thickness = static_cast<float>(settings.thicken_thickness);
      ImGui::SetNextItemWidth(180.0f);
      const std::string distance_label = context.localization.imgui_label(
          "operation.thicken.distance", "operation.thicken.distance",
          "Distance");
      if (ImGui::DragFloat(distance_label.c_str(), &thickness, 0.01f, -4.0f,
                           4.0f, "%.3f")) {
        settings.thicken_thickness = thickness;
        changed = true;
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
      if (ImGui::SliderInt(x_label.c_str(), &x_slices, 0, 64)) {
        settings.slice_quad_x_slices = x_slices;
        changed = true;
      }
        ImGui::SetNextItemWidth(180.0f);
        const std::string y_label = context.localization.imgui_label(
            "operation.slice_quad.y_slices", "operation.slice_quad.y_slices",
            "Y");
        if (ImGui::SliderInt(y_label.c_str(), &y_slices, 0, 64)) {
          settings.slice_quad_y_slices = y_slices;
          changed = true;
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
      if (ImGui::DragFloat(tolerance_label.c_str(), &tolerance, 0.001f, 0.0f,
                           128.0f, "%.4f")) {
        settings.merge_by_distance_tolerance = tolerance;
        changed = true;
      }
        break;
    }
    case ModelingToolSettingsTargetKind::None:
        break;
    }

    apply_settings_update();
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
            context.editor.modeling().commit_preview(history_label);
        if (!result.ok) {
          LOG_ERROR("Tool preview apply failed: {}", result.message.empty()
                                                         ? "Command failed."
                                                         : result.message);
        }
        open =
            result.ok ? false : context.modeling.tool_settings_target().active;
      }
      ImGui::SameLine();
      const std::string cancel_label = context.localization.imgui_label(
          "ui.action.cancel", "ui.action.cancel", "Cancel");
      if (ImGui::Button(cancel_label.c_str())) {
        const QuaderApiVoidResult result =
            context.editor.modeling().cancel_preview();
        if (!result.ok) {
          LOG_ERROR("Tool preview cancel failed: {}", result.message.empty()
                                                          ? "Command failed."
                                                          : result.message);
        }
        open =
            result.ok ? false : context.modeling.tool_settings_target().active;
      }
    }
}

} // namespace quader

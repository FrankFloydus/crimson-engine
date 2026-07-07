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

#include <imgui.h>

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace quader {
namespace {

std::string localized_tool_settings_title(
    const LocalizationService &localization,
    const QuaderToolSettingsTarget &target) {
  if (!target.tool_id.empty()) {
    std::string key = "tool_settings.target.";
    key.append(target.tool_id);
    return localization.text(key, target.title);
  }
  if (!target.title.empty()) {
    return target.title;
  }
  return localization.text("ui.panel.tool_settings", "Tool Settings");
}

std::string localized_control_label(const LocalizationService &localization,
                                    const QuaderToolSettingControl &control) {
  return localization.imgui_label(control.id, control.id, control.label);
}

void log_tool_settings_failure(const char *action,
                               const QuaderApiVoidResult &result) {
  if (result.ok) {
    return;
  }
  LOG_ERROR("{} failed: {}", action,
            result.message.empty() ? "Command failed." : result.message);
}

bool nearly_integer(double value) {
  return std::abs(value - std::round(value)) <= 0.000001;
}

float visible_label_width(const std::string &label) {
  return ImGui::CalcTextSize(label.c_str(), nullptr, true).x;
}

float estimated_inline_control_width(
    const QuaderToolSettingControl &control, const std::string &label) {
  const ImGuiStyle &style = ImGui::GetStyle();
  switch (control.kind) {
  case QuaderToolSettingControlKind::Toggle:
    return ImGui::GetFrameHeight() + style.ItemInnerSpacing.x +
           visible_label_width(label);
  case QuaderToolSettingControlKind::Action:
    return visible_label_width(label) + style.FramePadding.x * 2.0f;
  case QuaderToolSettingControlKind::Number:
  case QuaderToolSettingControlKind::Choice:
    break;
  }
  return 0.0f;
}

bool can_inline_next_control(const QuaderToolSettingControl &current,
                             const QuaderToolSettingControl &next) {
  if (current.kind != next.kind) {
    return false;
  }
  return current.kind == QuaderToolSettingControlKind::Action ||
         current.kind == QuaderToolSettingControlKind::Toggle;
}

void maybe_same_line_for_next_control(
    const LocalizationService &localization,
    const std::vector<QuaderToolSettingControl> &controls,
    std::size_t control_index) {
  if (control_index + 1u >= controls.size() ||
      !can_inline_next_control(controls[control_index],
                               controls[control_index + 1u])) {
    return;
  }

  const QuaderToolSettingControl &next = controls[control_index + 1u];
  const std::string next_label =
      localized_control_label(localization, next);
  const float next_width = estimated_inline_control_width(next, next_label);
  if (next_width <= 0.0f) {
    return;
  }

  const ImGuiStyle &style = ImGui::GetStyle();
  const float content_right =
      ImGui::GetWindowPos().x + ImGui::GetContentRegionMax().x;
  const float next_right =
      ImGui::GetItemRectMax().x + style.ItemSpacing.x + next_width;
  if (next_right <= content_right) {
    ImGui::SameLine();
  }
}

} // namespace

void ToolSettingsPanel::draw(UIContext &context, bool &open) {
  const QuaderToolSettingsTarget target =
      context.intent.editor.tools().active_settings();
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

void ToolSettingsPanel::before_begin(UIContext &context, bool &open) {
  (void)context;
  (void)open;
  if (!should_open_for_active_target_) {
    return;
  }

  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowViewport(viewport->ID);
  ImGui::SetNextWindowPos(
      {viewport->WorkPos.x + 24.0f, viewport->WorkPos.y + 72.0f},
      ImGuiCond_Always);
}

void ToolSettingsPanel::draw_content(UIContext &context, bool &open) {
  QuaderToolSettingsTarget target =
      context.intent.editor.tools().active_settings();
  if (!target.active) {
    open = false;
    return;
  }

  const std::string title =
      localized_tool_settings_title(context.localization, target);
  ImGui::TextUnformatted(title.c_str());
  ImGui::Separator();

  for (std::size_t control_index = 0; control_index < target.controls.size();
       ++control_index) {
    const QuaderToolSettingControl &control = target.controls[control_index];
    ImGui::PushID(control.id.c_str());
    const std::string label =
        localized_control_label(context.localization, control);
    switch (control.kind) {
    case QuaderToolSettingControlKind::Toggle: {
      bool value = control.bool_value;
      if (ImGui::Checkbox(label.c_str(), &value)) {
        const QuaderApiVoidResult result =
            context.intent.editor.tools().set_setting(target.tool_id,
                                                      control.id, value);
        log_tool_settings_failure("Tool settings update", result);
      }
      break;
    }
    case QuaderToolSettingControlKind::Number: {
      ImGui::SetNextItemWidth(180.0f);
      if (nearly_integer(control.step) && nearly_integer(control.min_value) &&
          nearly_integer(control.max_value)) {
        int value = static_cast<int>(std::round(control.number_value));
        const int min_value = static_cast<int>(std::round(control.min_value));
        const int max_value = static_cast<int>(std::round(control.max_value));
        if (ImGui::SliderInt(label.c_str(), &value, min_value, max_value, "%d",
                             ImGuiSliderFlags_AlwaysClamp)) {
          const QuaderApiVoidResult result =
              context.intent.editor.tools().set_setting(
                  target.tool_id, control.id, static_cast<double>(value));
          log_tool_settings_failure("Tool settings update", result);
        }
      } else {
        float value = static_cast<float>(control.number_value);
        if (ImGui::SliderFloat(label.c_str(), &value,
                               static_cast<float>(control.min_value),
                               static_cast<float>(control.max_value), "%.4f",
                               ImGuiSliderFlags_AlwaysClamp)) {
          const QuaderApiVoidResult result =
              context.intent.editor.tools().set_setting(
                  target.tool_id, control.id, static_cast<double>(value));
          log_tool_settings_failure("Tool settings update", result);
        }
      }
      break;
    }
    case QuaderToolSettingControlKind::Choice: {
      ImGui::TextUnformatted(label.c_str());
      for (const QuaderToolSettingChoiceOption &option : control.choices) {
        const std::string option_label =
            context.localization.imgui_label(option.id, option.id,
                                             option.label);
        const bool selected =
            control.choice_value == option.id ||
            control.choice_value == std::to_string(
                                        static_cast<int>(option.value));
        if (ImGui::RadioButton(option_label.c_str(), selected)) {
          const QuaderApiVoidResult result =
              context.intent.editor.tools().set_setting(
                  target.tool_id, control.id, option.value);
          log_tool_settings_failure("Tool settings update", result);
        }
      }
      break;
    }
    case QuaderToolSettingControlKind::Action:
      if (ImGui::Button(label.c_str())) {
        const QuaderApiVoidResult result =
            context.intent.editor.tools().execute_action(target.tool_id,
                                                        control.id);
        log_tool_settings_failure("Tool settings action", result);
      }
      break;
    }
    ImGui::PopID();
    maybe_same_line_for_next_control(context.localization, target.controls,
                                     control_index);
  }

  if (target.can_apply || target.can_cancel) {
    ImGui::Separator();
  }
  if (target.can_apply) {
    const std::string apply_label = context.localization.imgui_label(
        "ui.action.apply", "ui.action.apply", "Apply");
    if (ImGui::Button(apply_label.c_str())) {
      const QuaderApiVoidResult result =
          context.intent.editor.tools().commit_active_tool_preview();
      log_tool_settings_failure("Tool preview apply", result);
      if (result.ok) {
        open = false;
      }
    }
  }
  if (target.can_apply && target.can_cancel) {
    ImGui::SameLine();
  }
  if (target.can_cancel) {
    const std::string cancel_label = context.localization.imgui_label(
        "ui.action.cancel", "ui.action.cancel", "Cancel");
    if (ImGui::Button(cancel_label.c_str())) {
      const QuaderApiVoidResult result =
          context.intent.editor.tools().cancel_active_tool_preview();
      log_tool_settings_failure("Tool preview cancel", result);
      if (result.ok) {
        open = false;
      }
    }
  }
}

} // namespace quader

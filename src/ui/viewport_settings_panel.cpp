////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/viewport_settings_panel.hpp"

#include "core/localization_service.hpp"
#include "ui/imgui_quader_widgets.hpp"
#include "ui/ui_context.hpp"
#include "ui/ui_settings.hpp"
#include "viewport/viewport_settings.hpp"

#include <imgui.h>

#include <string>

namespace quader {
namespace {

std::string_view section_stable_id(ViewportSettingsSectionId section) {
  switch (section) {
  case ViewportSettingsSectionId::Grid:
    return "grid";
  case ViewportSettingsSectionId::Selection:
    return "selection";
  case ViewportSettingsSectionId::Overlay:
    return "overlay";
  case ViewportSettingsSectionId::Vertices:
    return "vertices";
  case ViewportSettingsSectionId::Picking:
    return "picking";
  case ViewportSettingsSectionId::Gizmo:
    return "gizmo";
  case ViewportSettingsSectionId::Count:
    break;
  }
  return "unknown";
}

std::string section_label(const LocalizationService &localization,
                          const ViewportSettingsSectionDescriptor &section) {
  std::string key = "viewport.settings.section.";
  key.append(section_stable_id(section.id));
  key.append(".label");
  std::string stable_id = "viewport.settings.section.";
  stable_id.append(section_stable_id(section.id));
  return localization.imgui_label(key, stable_id, section.label);
}

std::string setting_label(const LocalizationService &localization,
                          std::string_view key, std::string_view fallback) {
  std::string localization_key = "viewport.settings.";
  localization_key.append(key);
  localization_key.append(".label");
  std::string stable_id = "viewport.settings.";
  stable_id.append(key);
  return localization.imgui_label(localization_key, stable_id, fallback);
}

void draw_locale_settings(UIContext &context) {
  LocalizationService &localization = context.localization;
  ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen;
  const std::string header = localization.imgui_label(
      "ui.settings.interface", "ui.settings.interface", "Interface");
  if (!quader_collapsing_header(header.c_str(), flags)) {
    return;
  }

  const std::string selected_locale = context.settings.locale();
  std::string preview = localization.locale_display_name(selected_locale);
  if (preview.empty()) {
    preview = selected_locale;
  }

  const std::string combo_label = localization.imgui_label(
      "ui.settings.locale", "ui.settings.locale", "Language");
  ImGui::SetNextItemWidth(220.0f);
  if (quader_begin_combo(combo_label.c_str(), preview.c_str())) {
    for (const LocaleInfo &locale : localization.available_locales()) {
      const bool selected = locale.id == selected_locale;
      const std::string item_label =
          localization.imgui_label("locale." + locale.id + ".name",
                                   "locale." + locale.id, locale.display_name);
      if (ImGui::Selectable(item_label.c_str(), selected)) {
        if (context.localization.select_locale(locale.id)) {
          context.settings.set_locale(context.localization.current_locale());
        }
        if (context.settings_changed != nullptr &&
            context.settings.locale() != selected_locale) {
          *context.settings_changed = true;
        }
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }
}

} // namespace

bool ViewportSettingsPanel::draw(UIContext& context, bool& open) {
  changed_this_frame_ = false;
  draw_floating(context, open);
  return changed_this_frame_;
}

void ViewportSettingsPanel::draw_content(UIContext& context, bool& open) {
    (void)open;

    draw_locale_settings(context);

    const std::string reset_label = context.localization.imgui_label(
        "ui.settings.viewport.reset_to_defaults",
        "ui.settings.viewport.reset_to_defaults", "Reset to Defaults");
    if (ImGui::Button(reset_label.c_str())) {
      context.viewport_settings.reset_to_defaults();
      changed_this_frame_ = true;
    }

    for (const ViewportSettingsSectionDescriptor &section :
         viewport_settings_sections()) {
      ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None;
      if (section.default_expanded) {
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
      }
      const std::string header = section_label(context.localization, section);
      if (!quader_collapsing_header(header.c_str(), flags)) {
        continue;
      }

      for (const ViewportColorSettingDescriptor &descriptor :
           viewport_color_settings()) {
        if (descriptor.section != section.id) {
          continue;
        }
        ViewportSettingColor color =
            context.viewport_settings.color(descriptor.id);
        float values[4] = {color.r, color.g, color.b, color.a};
        ImGui::SetNextItemWidth(180.0f);
        const std::string label = setting_label(
            context.localization, descriptor.key, descriptor.label);
        if (ImGui::ColorEdit4(label.c_str(), values,
                              ImGuiColorEditFlags_NoInputs |
                                  ImGuiColorEditFlags_AlphaBar |
                                  ImGuiColorEditFlags_AlphaPreviewHalf)) {
          changed_this_frame_ =
              context.viewport_settings.set_color(
                  descriptor.id, ViewportSettingColor{values[0], values[1],
                                                      values[2], values[3]}) ||
              changed_this_frame_;
        }
      }

      for (const ViewportFloatSettingDescriptor &descriptor :
           viewport_float_settings()) {
        if (descriptor.section != section.id) {
          continue;
        }
        float value = context.viewport_settings.float_value(descriptor.id);
        ImGui::SetNextItemWidth(220.0f);
        const std::string label = setting_label(
            context.localization, descriptor.key, descriptor.label);
        if (quader_slider_float(label.c_str(), &value, descriptor.minimum,
                                descriptor.maximum, "%.3f")) {
          changed_this_frame_ =
              context.viewport_settings.set_float_value(descriptor.id, value) ||
              changed_this_frame_;
        }
      }

      for (const ViewportBoolSettingDescriptor &descriptor :
           viewport_bool_settings()) {
        if (descriptor.section != section.id) {
          continue;
        }
        bool value = context.viewport_settings.bool_value(descriptor.id);
        const std::string label = setting_label(
            context.localization, descriptor.key, descriptor.label);
        if (ImGui::Checkbox(label.c_str(), &value)) {
          changed_this_frame_ =
              context.viewport_settings.set_bool_value(descriptor.id, value) ||
              changed_this_frame_;
        }
      }
    }
}

} // namespace quader

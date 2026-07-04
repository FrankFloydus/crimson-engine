////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/inspector_panel.hpp"

#include "api/quader_api_type_conversions.hpp"
#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "editor/transform/scene_transform_drag_service.hpp"
#include "core/scene_environment_catalog.hpp"
#include "scene/scene_document.hpp"
#include "scene/transform_pivot.hpp"
#include "ui/imgui_quader_widgets.hpp"
#include "ui/ui_context.hpp"
#include "ui/ui_style_tokens.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace quader {
namespace {

constexpr float kRadToDeg = 57.29577951308232f;
constexpr float kDegToRad = 0.017453292519943295f;
constexpr float kAxisTextColumnWidth = 12.0f;
constexpr float kMinAxisInputWidth = 58.0f;
constexpr float kInspectorLightRangeMin = 0.1f;
constexpr float kInspectorLightRangeSliderMax = 100.0f;

void copy_name_to_buffer(const NameComponent *name,
                         std::array<char, 128> &buffer) {
  buffer.fill('\0');
  if (!name) {
    return;
  }
  std::snprintf(buffer.data(), buffer.size(), "%s", name->name.c_str());
}

const char *selection_mode_key(ModelingSelectionMode mode) {
  switch (mode) {
  case ModelingSelectionMode::Vertex:
    return "selection.mode.vertex";
  case ModelingSelectionMode::Edge:
    return "selection.mode.edge";
  case ModelingSelectionMode::Face:
    return "selection.mode.face";
  case ModelingSelectionMode::Object:
    return "selection.mode.object";
  case ModelingSelectionMode::Group:
    return "selection.mode.group";
  }
  return "selection.mode.object";
}

const char *selection_mode_fallback(ModelingSelectionMode mode) {
  switch (mode) {
  case ModelingSelectionMode::Vertex:
    return "Vertex";
  case ModelingSelectionMode::Edge:
    return "Edge";
  case ModelingSelectionMode::Face:
    return "Face";
  case ModelingSelectionMode::Object:
    return "Object";
  case ModelingSelectionMode::Group:
    return "Group";
  }
  return "Object";
}

const char *active_selection_kind_key(ModelingActiveSelectionKind kind) {
  switch (kind) {
  case ModelingActiveSelectionKind::None:
    return "selection.active.none";
  case ModelingActiveSelectionKind::Object:
    return "selection.active.object";
  case ModelingActiveSelectionKind::Vertex:
    return "selection.active.vertex";
  case ModelingActiveSelectionKind::Edge:
    return "selection.active.edge";
  case ModelingActiveSelectionKind::Face:
    return "selection.active.face";
  case ModelingActiveSelectionKind::Light:
    return "selection.active.light";
  }
  return "selection.active.none";
}

const char *active_selection_kind_fallback(ModelingActiveSelectionKind kind) {
  switch (kind) {
  case ModelingActiveSelectionKind::None:
    return "None";
  case ModelingActiveSelectionKind::Object:
    return "Object";
  case ModelingActiveSelectionKind::Vertex:
    return "Vertex";
  case ModelingActiveSelectionKind::Edge:
    return "Edge";
  case ModelingActiveSelectionKind::Face:
    return "Face";
  case ModelingActiveSelectionKind::Light:
    return "Light";
  }
  return "None";
}

/**
 * Stores edit lifecycle flags emitted by one Inspector axis input.
 */
struct AxisDragFloatResult {
    bool changed = false;
    bool activated = false;
    bool deactivated = false;
    bool deactivated_after_edit = false;
};

AxisDragFloatResult axis_drag_float(const char *axis, ImVec4 color,
                                    float *value, bool mixed,
                                    const char *mixed_label, float speed,
                                    float min_value, float max_value,
                                    float input_width) {
  ImGui::PushID(axis);
  ImGui::PushStyleColor(ImGuiCol_Text, color);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(axis);
  ImGui::PopStyleColor();
  ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
  ImGui::SetNextItemWidth(input_width < 0.0f
                              ? input_width
                              : std::max(input_width, kMinAxisInputWidth));
  AxisDragFloatResult result;
  if (mixed) {
    std::array<char, 64> mixed_buffer{};
    std::snprintf(mixed_buffer.data(), mixed_buffer.size(), "%s", mixed_label);
    ImGui::BeginDisabled();
    ImGui::InputText("##value", mixed_buffer.data(), mixed_buffer.size(),
                     ImGuiInputTextFlags_ReadOnly);
    ImGui::EndDisabled();
  } else {
    result.changed =
        ImGui::DragFloat("##value", value, speed, min_value, max_value, "%.3f");
    result.activated = ImGui::IsItemActivated();
    result.deactivated = ImGui::IsItemDeactivated();
    result.deactivated_after_edit = ImGui::IsItemDeactivatedAfterEdit();
  }
  ImGui::PopID();
  return result;
}

AxisDragFloatResult
axis_drag_float3(const char *stable_id, const char *label, float values[3],
                 const std::array<bool, 3> &mixed, const char *mixed_label,
                 const UIStyleTokens &style, float speed,
                 float min_value = 0.0f, float max_value = 0.0f) {
  AxisDragFloatResult result;
  const ImGuiStyle &imgui_style = ImGui::GetStyle();
  const float min_axis_column_width =
      kAxisTextColumnWidth + imgui_style.ItemInnerSpacing.x +
      kMinAxisInputWidth + imgui_style.CellPadding.x * 2.0f;

  quader_property_row(stable_id, label);
  if (ImGui::BeginTable("##AxisDragFloat3", 3,
                        ImGuiTableFlags_SizingStretchProp |
                            ImGuiTableFlags_NoSavedSettings |
                            ImGuiTableFlags_NoPadOuterX)) {
    ImGui::TableSetupColumn("X", ImGuiTableColumnFlags_WidthStretch,
                            min_axis_column_width);
    ImGui::TableSetupColumn("Y", ImGuiTableColumnFlags_WidthStretch,
                            min_axis_column_width);
    ImGui::TableSetupColumn("Z", ImGuiTableColumnFlags_WidthStretch,
                            min_axis_column_width);
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    const AxisDragFloatResult x =
        axis_drag_float("X", style.axis_x, &values[0], mixed[0], mixed_label,
                        speed, min_value, max_value, -FLT_MIN);
    result.changed = result.changed || x.changed;
    result.activated = result.activated || x.activated;
    result.deactivated = result.deactivated || x.deactivated;
    result.deactivated_after_edit =
        result.deactivated_after_edit || x.deactivated_after_edit;
    ImGui::TableSetColumnIndex(1);
    const AxisDragFloatResult y =
        axis_drag_float("Y", style.axis_y, &values[1], mixed[1], mixed_label,
                        speed, min_value, max_value, -FLT_MIN);
    result.changed = result.changed || y.changed;
    result.activated = result.activated || y.activated;
    result.deactivated = result.deactivated || y.deactivated;
    result.deactivated_after_edit =
        result.deactivated_after_edit || y.deactivated_after_edit;
    ImGui::TableSetColumnIndex(2);
    const AxisDragFloatResult z =
        axis_drag_float("Z", style.axis_z, &values[2], mixed[2], mixed_label,
                        speed, min_value, max_value, -FLT_MIN);
    result.changed = result.changed || z.changed;
    result.activated = result.activated || z.activated;
    result.deactivated = result.deactivated || z.deactivated;
    result.deactivated_after_edit =
        result.deactivated_after_edit || z.deactivated_after_edit;

    ImGui::EndTable();
  }
  quader_end_property_row();
  return result;
}

bool input_text_with_left_label(const char *stable_id, const char *label,
                                char *buffer, std::size_t buffer_size,
                                ImGuiInputTextFlags flags) {
  quader_property_row(stable_id, label);
  const bool changed = ImGui::InputText("##value", buffer, buffer_size, flags);
  quader_end_property_row();
  return changed;
}

bool checkbox_with_left_label(const char *stable_id, const char *label,
                              bool *value) {
  quader_property_row(stable_id, label);
  const bool changed = ImGui::Checkbox("##value", value);
  quader_end_property_row();
  return changed;
}

void update_continuous_edit_tracker(const char *stable_id,
                                    UIContinuousEditTracker &active_edit,
                                    EntityId primary,
                                    std::span<const EntityId> selection,
                                    std::span<float> values,
                                    UIContinuousEditResult &result) {
  if (result.should_apply() || result.activated) {
    result.merge_key =
        active_edit.ensure_merge_key(stable_id, primary, selection);
  }
  if (result.activated || result.changed) {
    active_edit.stage(stable_id, primary, selection,
                      std::span<const float>(values.data(), values.size()));
  }
  if (result.deactivated_after_edit) {
    const std::vector<float> final_values = active_edit.final_values(
        stable_id, primary, selection,
        std::span<const float>(values.data(), values.size()));
    std::copy(final_values.begin(), final_values.end(), values.begin());
    active_edit.reset();
  } else if (result.deactivated) {
    active_edit.reset();
  }
}

UIContinuousEditResult drag_float_with_left_label(
    const char *stable_id, const char *label, float *value,
    UIContinuousEditTracker &active_edit, EntityId primary,
    std::span<const EntityId> selection, float speed, float min_value,
    float max_value, const char *format = "%.3f",
    ImGuiSliderFlags slider_flags = ImGuiSliderFlags_AlwaysClamp) {
  std::array<float, 1> staged{*value};
  active_edit.apply_active_values(
      stable_id, primary, selection,
      std::span<float>(staged.data(), staged.size()));
  *value = staged[0];

  UIContinuousEditResult result;
  quader_property_row(stable_id, label);
  result.changed =
      min_value < max_value
          ? quader_slider_float("##value", value, min_value, max_value, format,
                                slider_flags)
          : ImGui::DragFloat("##value", value, speed, min_value, max_value,
                             format);
  result.activated = ImGui::IsItemActivated();
  result.deactivated = ImGui::IsItemDeactivated();
  result.deactivated_after_edit = ImGui::IsItemDeactivatedAfterEdit();
  quader_end_property_row();

  staged[0] = *value;
  update_continuous_edit_tracker(stable_id, active_edit, primary, selection,
                                 std::span<float>(staged.data(), staged.size()),
                                 result);
  *value = staged[0];
  return result;
}

UIContinuousEditResult color_edit3_with_left_label(
    const char *stable_id, const char *label, float values[3],
    UIContinuousEditTracker &active_edit, EntityId primary,
    std::span<const EntityId> selection) {
  std::array<float, 3> staged{values[0], values[1], values[2]};
  active_edit.apply_active_values(
      stable_id, primary, selection,
      std::span<float>(staged.data(), staged.size()));
  values[0] = staged[0];
  values[1] = staged[1];
  values[2] = staged[2];

  UIContinuousEditResult result;
  quader_property_row(stable_id, label);
  result.changed = ImGui::ColorEdit3("##value", values,
                                     ImGuiColorEditFlags_Float |
                                         ImGuiColorEditFlags_NoInputs);
  result.activated = ImGui::IsItemActivated();
  result.deactivated = ImGui::IsItemDeactivated();
  result.deactivated_after_edit = ImGui::IsItemDeactivatedAfterEdit();
  quader_end_property_row();

  staged = {values[0], values[1], values[2]};
  update_continuous_edit_tracker(stable_id, active_edit, primary, selection,
                                 std::span<float>(staged.data(), staged.size()),
                                 result);
  values[0] = staged[0];
  values[1] = staged[1];
  values[2] = staged[2];
  return result;
}

template <typename Enum>
bool combo_with_left_label(
    const char *stable_id, const char *label, Enum *value,
    const std::vector<std::pair<Enum, std::string>> &options) {
  bool changed = false;
  quader_property_row(stable_id, label);
  const auto selected =
      std::find_if(options.begin(), options.end(), [&](const auto &option) {
        return option.first == *value;
      });
  const char *preview =
      selected == options.end() ? "" : selected->second.c_str();
  if (quader_begin_combo("##value", preview)) {
    for (const auto &[optionValue, optionLabel] : options) {
      const bool is_selected = optionValue == *value;
      if (ImGui::Selectable(optionLabel.c_str(), is_selected)) {
        *value = optionValue;
        changed = true;
      }
      if (is_selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }
  quader_end_property_row();
  return changed;
}

void draw_scene_settings_inspector(UIContext &context, EntityId entity,
                                   std::span<const EntityId> selection,
                                   UIContinuousEditTracker &active_edit,
                                   SceneSettingsComponent settings,
                                   LocalizationService &localization) {
  auto apply_settings = [&](SceneSettingsComponent next,
                            std::string merge_key = {}) {
    next.environment.intensity = std::max(next.environment.intensity, 0.0f);
    next.environment.background_intensity =
        std::max(next.environment.background_intensity, 0.0f);
    next.camera.aperture = std::max(next.camera.aperture, 0.01f);
    next.camera.shutter_speed = std::max(next.camera.shutter_speed, 0.000001f);
    next.camera.sensitivity = std::max(next.camera.sensitivity, 1.0f);
    next.camera.near_plane = std::max(next.camera.near_plane, 0.001f);
    next.camera.far_plane =
        std::max(next.camera.far_plane, next.camera.near_plane + 0.001f);
    next.post_process.ambient_occlusion_intensity =
        std::max(next.post_process.ambient_occlusion_intensity, 0.0f);
    next.post_process.ambient_occlusion_radius =
        std::max(next.post_process.ambient_occlusion_radius, 0.0f);
    next.post_process.bloom_strength =
        std::max(next.post_process.bloom_strength, 0.0f);
    (void)context.intent.editor.scene().set_scene_settings(
        entity, to_api_scene_settings(next),
        QuaderSceneEditOptions{.merge_key = std::move(merge_key)});
  };

  const std::string environment_label =
      localization.text("ui.inspector.environment", "Environment");
  if (quader_property_section("environment", environment_label.c_str())) {
    const std::string preset_label =
        localization.text("ui.inspector.environment_preset", "Preset");
    std::string environment_id = settings.environment.environment_id;
    std::vector<std::pair<std::string, std::string>> environment_options;
    for (const SceneEnvironmentDescriptor &descriptor :
         scene_environment_catalog()) {
      environment_options.emplace_back(
          std::string(descriptor.id),
          localization.text(std::string(descriptor.label_key),
                            std::string(descriptor.fallback_label)));
    }
    if (combo_with_left_label("environment_preset", preset_label.c_str(),
                              &environment_id, environment_options)) {
      settings.environment.environment_id = environment_id;
      apply_settings(settings);
    }

    bool enabled = settings.environment.enabled;
    const std::string enabled_label =
        localization.text("ui.inspector.enabled", "Enabled");
    if (checkbox_with_left_label("environment_enabled", enabled_label.c_str(),
                                 &enabled)) {
      settings.environment.enabled = enabled;
      apply_settings(settings);
    }

    bool show_background = settings.environment.show_background;
    const std::string background_label =
        localization.text("ui.inspector.background", "Background");
    if (checkbox_with_left_label("environment_background",
                                 background_label.c_str(), &show_background)) {
      settings.environment.show_background = show_background;
      apply_settings(settings);
    }

    float clear_color[3] = {
        settings.environment.clear_color.x,
        settings.environment.clear_color.y,
        settings.environment.clear_color.z,
    };
    const std::string clear_color_label =
        localization.text("ui.inspector.clear_color", "Clear Color");
    const UIContinuousEditResult clear_color_edit = color_edit3_with_left_label(
        "environment_clear_color", clear_color_label.c_str(), clear_color,
        active_edit, entity, selection);
    if (clear_color_edit.should_apply()) {
      settings.environment.clear_color = {
          std::clamp(clear_color[0], 0.0f, 1.0f),
          std::clamp(clear_color[1], 0.0f, 1.0f),
          std::clamp(clear_color[2], 0.0f, 1.0f),
      };
      apply_settings(settings, clear_color_edit.merge_key);
    }

    float intensity = settings.environment.intensity;
    const std::string intensity_label =
        localization.text("ui.inspector.intensity", "Intensity");
    const UIContinuousEditResult intensity_edit = drag_float_with_left_label(
        "environment_intensity", intensity_label.c_str(), &intensity,
        active_edit, entity, selection, 0.01f, 0.0f, 16.0f, "%.3f");
    if (intensity_edit.should_apply()) {
      settings.environment.intensity = intensity;
      apply_settings(settings, intensity_edit.merge_key);
    }

    float background_intensity = settings.environment.background_intensity;
    const std::string background_intensity_label = localization.text(
        "ui.inspector.background_intensity", "Background Intensity");
    const UIContinuousEditResult background_intensity_edit =
        drag_float_with_left_label("environment_background_intensity",
                                   background_intensity_label.c_str(),
                                   &background_intensity, active_edit, entity,
                                   selection, 0.01f, 0.0f, 16.0f, "%.3f");
    if (background_intensity_edit.should_apply()) {
      settings.environment.background_intensity = background_intensity;
      apply_settings(settings, background_intensity_edit.merge_key);
    }

    float rotation_y = settings.environment.rotation_y * kRadToDeg;
    const std::string rotation_label =
        localization.text("ui.inspector.rotation", "Rotation");
    const UIContinuousEditResult rotation_edit = drag_float_with_left_label(
        "environment_rotation", rotation_label.c_str(), &rotation_y,
        active_edit, entity, selection, 0.5f, -360.0f, 360.0f, "%.2f");
    if (rotation_edit.should_apply()) {
      settings.environment.rotation_y = rotation_y * kDegToRad;
      apply_settings(settings, rotation_edit.merge_key);
    }
  }

  const std::string camera_label =
      localization.text("ui.inspector.camera", "Camera");
  if (quader_property_section("camera", camera_label.c_str())) {
    float aperture = settings.camera.aperture;
    const std::string aperture_label =
        localization.text("ui.inspector.aperture", "Aperture");
    const UIContinuousEditResult aperture_edit = drag_float_with_left_label(
        "camera_aperture", aperture_label.c_str(), &aperture, active_edit,
        entity, selection, 0.05f, 0.01f, 128.0f, "%.2f");
    if (aperture_edit.should_apply()) {
      settings.camera.aperture = aperture;
      apply_settings(settings, aperture_edit.merge_key);
    }

    float shutter_speed = settings.camera.shutter_speed;
    const std::string shutter_label =
        localization.text("ui.inspector.shutter_speed", "Shutter");
    const UIContinuousEditResult shutter_edit = drag_float_with_left_label(
        "camera_shutter", shutter_label.c_str(), &shutter_speed, active_edit,
        entity, selection, 0.0001f, 0.000001f, 10.0f, "%.6f");
    if (shutter_edit.should_apply()) {
      settings.camera.shutter_speed = shutter_speed;
      apply_settings(settings, shutter_edit.merge_key);
    }

    float sensitivity = settings.camera.sensitivity;
    const std::string sensitivity_label =
        localization.text("ui.inspector.sensitivity", "ISO");
    const UIContinuousEditResult sensitivity_edit = drag_float_with_left_label(
        "camera_sensitivity", sensitivity_label.c_str(), &sensitivity,
        active_edit, entity, selection, 1.0f, 1.0f, 51200.0f, "%.0f");
    if (sensitivity_edit.should_apply()) {
      settings.camera.sensitivity = sensitivity;
      apply_settings(settings, sensitivity_edit.merge_key);
    }

    float near_plane = settings.camera.near_plane;
    const std::string near_label =
        localization.text("ui.inspector.near_plane", "Near");
    const UIContinuousEditResult near_edit = drag_float_with_left_label(
        "camera_near", near_label.c_str(), &near_plane, active_edit, entity,
        selection, 0.001f, 0.001f, 10000.0f, "%.4f");
    if (near_edit.should_apply()) {
      settings.camera.near_plane = near_plane;
      apply_settings(settings, near_edit.merge_key);
    }

    float far_plane = settings.camera.far_plane;
    const std::string far_label =
        localization.text("ui.inspector.far_plane", "Far");
    const UIContinuousEditResult far_edit = drag_float_with_left_label(
        "camera_far", far_label.c_str(), &far_plane, active_edit, entity,
        selection, 1.0f, 0.001f, 1000000.0f, "%.2f");
    if (far_edit.should_apply()) {
      settings.camera.far_plane = far_plane;
      apply_settings(settings, far_edit.merge_key);
    }
  }

  const std::string post_process_label =
      localization.text("ui.inspector.post_process", "Post Process");
  if (quader_property_section("post_process", post_process_label.c_str())) {
    bool enabled = settings.post_process.enabled;
    const std::string enabled_label =
        localization.text("ui.inspector.enabled", "Enabled");
    if (checkbox_with_left_label("post_process_enabled", enabled_label.c_str(),
                                 &enabled)) {
      settings.post_process.enabled = enabled;
      apply_settings(settings);
    }

    bool anti_aliasing = settings.post_process.anti_aliasing;
    const std::string aa_label =
        localization.text("ui.inspector.anti_aliasing", "Anti-Aliasing");
    if (checkbox_with_left_label("post_process_aa", aa_label.c_str(),
                                 &anti_aliasing)) {
      settings.post_process.anti_aliasing = anti_aliasing;
      apply_settings(settings);
    }

    bool dithering = settings.post_process.dithering;
    const std::string dithering_label =
        localization.text("ui.inspector.dithering", "Dithering");
    if (checkbox_with_left_label("post_process_dithering",
                                 dithering_label.c_str(), &dithering)) {
      settings.post_process.dithering = dithering;
      apply_settings(settings);
    }

    SceneToneMapping tone_mapping = settings.post_process.tone_mapping;
    const std::string tone_mapping_label =
        localization.text("ui.inspector.tone_mapping", "Tone Mapping");
    const std::vector<std::pair<SceneToneMapping, std::string>>
        tone_mapping_options{
            {SceneToneMapping::Linear,
             localization.text("ui.inspector.tone_mapping.linear", "Linear")},
            {SceneToneMapping::AcesLegacy,
             localization.text("ui.inspector.tone_mapping.aces_legacy",
                               "ACES Legacy")},
            {SceneToneMapping::Aces,
             localization.text("ui.inspector.tone_mapping.aces", "ACES")},
            {SceneToneMapping::Filmic,
             localization.text("ui.inspector.tone_mapping.filmic", "Filmic")},
            {SceneToneMapping::DisplayRange,
             localization.text("ui.inspector.tone_mapping.display_range",
                               "Display Range")},
        };
    if (combo_with_left_label("post_process_tone_mapping",
                              tone_mapping_label.c_str(), &tone_mapping,
                              tone_mapping_options)) {
      settings.post_process.tone_mapping = tone_mapping;
      apply_settings(settings);
    }

    bool ambient_occlusion = settings.post_process.ambient_occlusion;
    const std::string ao_label = localization.text(
        "ui.inspector.ambient_occlusion", "Ambient Occlusion");
    if (checkbox_with_left_label("post_process_ao", ao_label.c_str(),
                                 &ambient_occlusion)) {
      settings.post_process.ambient_occlusion = ambient_occlusion;
      apply_settings(settings);
    }
    if (settings.post_process.ambient_occlusion) {
      float ao_intensity = settings.post_process.ambient_occlusion_intensity;
      const std::string ao_intensity_label = localization.text(
          "ui.inspector.ambient_occlusion_intensity", "AO Intensity");
      const UIContinuousEditResult ao_intensity_edit =
          drag_float_with_left_label("post_process_ao_intensity",
                                     ao_intensity_label.c_str(), &ao_intensity,
                                     active_edit, entity, selection, 0.01f,
                                     0.0f, 8.0f, "%.3f");
      if (ao_intensity_edit.should_apply()) {
        settings.post_process.ambient_occlusion_intensity = ao_intensity;
        apply_settings(settings, ao_intensity_edit.merge_key);
      }

      float ao_radius = settings.post_process.ambient_occlusion_radius;
      const std::string ao_radius_label = localization.text(
          "ui.inspector.ambient_occlusion_radius", "AO Radius");
      const UIContinuousEditResult ao_radius_edit = drag_float_with_left_label(
          "post_process_ao_radius", ao_radius_label.c_str(), &ao_radius,
          active_edit, entity, selection, 0.01f, 0.0f, 10.0f, "%.3f");
      if (ao_radius_edit.should_apply()) {
        settings.post_process.ambient_occlusion_radius = ao_radius;
        apply_settings(settings, ao_radius_edit.merge_key);
      }
    }

    bool bloom = settings.post_process.bloom;
    const std::string bloom_label =
        localization.text("ui.inspector.bloom", "Bloom");
    if (checkbox_with_left_label("post_process_bloom", bloom_label.c_str(),
                                 &bloom)) {
      settings.post_process.bloom = bloom;
      apply_settings(settings);
    }
    float bloom_strength = settings.post_process.bloom_strength;
    const std::string bloom_strength_label =
        localization.text("ui.inspector.bloom_strength", "Bloom Strength");
    const UIContinuousEditResult bloom_strength_edit =
        drag_float_with_left_label("post_process_bloom_strength",
                                   bloom_strength_label.c_str(),
                                   &bloom_strength, active_edit, entity,
                                   selection, 0.01f, 0.0f, 1.0f, "%.3f");
    if (bloom_strength_edit.should_apply()) {
      settings.post_process.bloom_strength = bloom_strength;
      apply_settings(settings, bloom_strength_edit.merge_key);
    }
  }
}

/**
 * Represents a Bool Aggregate value used by the ImGui editor presentation layer.
 */
struct BoolAggregate {
  bool has_value = false;
  bool all_true = true;
  bool any_true = false;

  bool mixed() const { return any_true && !all_true; }
};

BoolAggregate aggregate_visibility(const SceneDocument &document,
                                   const std::vector<EntityId> &selected) {
  BoolAggregate result;
  for (const EntityId id : selected) {
    if (const VisibilityComponent *visibility = document.visibility(id)) {
      result.has_value = true;
      result.all_true = result.all_true && visibility->visible;
      result.any_true = result.any_true || visibility->visible;
    }
  }
  return result;
}

BoolAggregate aggregate_lock(const SceneDocument &document,
                             const std::vector<EntityId> &selected) {
  BoolAggregate result;
  for (const EntityId id : selected) {
    if (const LockComponent *lock = document.lock(id)) {
      result.has_value = true;
      result.all_true = result.all_true && lock->locked;
      result.any_true = result.any_true || lock->locked;
    }
  }
  return result;
}

bool same_float(float a, float b) { return std::fabs(a - b) <= 0.0001f; }

std::array<bool, 3> mixed_position_axes(const SceneDocument &document,
                                        const std::vector<EntityId> &selected,
                                        const TransformComponent &primary) {
  std::array<bool, 3> mixed{false, false, false};
  for (const EntityId id : selected) {
    if (const TransformComponent *transform = document.transform(id)) {
      mixed[0] =
          mixed[0] || !same_float(transform->position.x, primary.position.x);
      mixed[1] =
          mixed[1] || !same_float(transform->position.y, primary.position.y);
      mixed[2] =
          mixed[2] || !same_float(transform->position.z, primary.position.z);
    }
  }
  return mixed;
}

std::array<bool, 3> mixed_rotation_axes(const SceneDocument &document,
                                        const std::vector<EntityId> &selected,
                                        const TransformComponent &primary) {
  std::array<bool, 3> mixed{false, false, false};
  for (const EntityId id : selected) {
    if (const TransformComponent *transform = document.transform(id)) {
      mixed[0] = mixed[0] || !same_float(transform->rotation_euler.x,
                                         primary.rotation_euler.x);
      mixed[1] = mixed[1] || !same_float(transform->rotation_euler.y,
                                         primary.rotation_euler.y);
      mixed[2] = mixed[2] || !same_float(transform->rotation_euler.z,
                                         primary.rotation_euler.z);
    }
  }
  return mixed;
}

std::array<bool, 3> mixed_scale_axes(const SceneDocument &document,
                                     const std::vector<EntityId> &selected,
                                     const TransformComponent &primary) {
  std::array<bool, 3> mixed{false, false, false};
  for (const EntityId id : selected) {
    if (const TransformComponent *transform = document.transform(id)) {
      mixed[0] = mixed[0] || !same_float(transform->scale.x, primary.scale.x);
      mixed[1] = mixed[1] || !same_float(transform->scale.y, primary.scale.y);
      mixed[2] = mixed[2] || !same_float(transform->scale.z, primary.scale.z);
    }
  }
  return mixed;
}

} // namespace

float InspectorPanel::minimum_width(const UIContext& context) const {
    // UIDockPanel applies setNextWindowMinWidth(context.style.sidebarMinWidth) before opening the window.
    return context.style.sidebar_min_width;
}

ImVec2 InspectorPanel::panel_window_padding(const UIContext& context) const {
  return {context.style.window_padding.x, context.style.window_padding.y};
}

ImGuiWindowFlags InspectorPanel::window_flags(const UIContext& context) const {
  return context.style.panel_window_flags;
}

void InspectorPanel::cancel_active_transform_edit() {
  if (active_transform_preview_.preview_active()) {
    (void)active_transform_preview_.cancel();
  }
  active_transform_edit_.reset();
}

void InspectorPanel::draw_content(UIContext& context) {
    // UIDockPanel routes hover input through context.requestPanelFocus(id()) before content draws.
    const SceneDocument& document = context.scene.document;
    const ModelingSessionService& modeling = context.scene.modeling;

    const std::vector<EntityId> selected = document.selected_entities();
    const ModelingSelectionSummary selection = modeling.selection_summary();
    LocalizationService& localization = context.localization;
    const std::string mixed_label =
        localization.text("ui.inspector.mixed", "Mixed");
    const std::string mode_prefix =
        localization.text("ui.inspector.mode", "Mode");
    const std::string active_prefix =
        localization.text("ui.inspector.active", "Active");
    const std::string vertices_prefix =
        localization.text("ui.inspector.vertices", "Vertices");
    const std::string edges_prefix =
        localization.text("ui.inspector.edges", "Edges");
    const std::string faces_prefix =
        localization.text("ui.inspector.faces", "Faces");
    if (selected.empty()) {
      cancel_active_transform_edit();
      active_continuous_edit_.reset();
      if (selection.has_selection) {
        const std::string mode =
            localization.text(selection_mode_key(selection.mode),
                              selection_mode_fallback(selection.mode));
        const std::string active = localization.text(
            active_selection_kind_key(selection.active_kind),
            active_selection_kind_fallback(selection.active_kind));
        ImGui::Text("%s: %s", mode_prefix.c_str(), mode.c_str());
        ImGui::Text("%s: %s", active_prefix.c_str(), active.c_str());
        ImGui::Text("%s: %u", vertices_prefix.c_str(),
                    selection.selected_vertex_count);
        ImGui::Text("%s: %u", edges_prefix.c_str(),
                    selection.selected_edge_count);
        ImGui::Text("%s: %u", faces_prefix.c_str(),
                    selection.selected_face_count);
      } else {
        const std::string no_selection =
            localization.text("ui.inspector.no_selection", "No selection");
        ImGui::TextUnformatted(no_selection.c_str());
      }
        return;
    }

    EntityId primary = document.primary_selection();
    if (primary == kInvalidEntityId) {
      primary = selected.front();
    }
    if (active_transform_edit_ &&
        (active_transform_edit_->primary != primary ||
         active_transform_edit_->selection != selected)) {
      cancel_active_transform_edit();
    }
    active_continuous_edit_.reset_if_entity_scope_changed(
        primary, std::span<const EntityId>(selected.data(), selected.size()));
    const bool primary_is_scene_root = document.is_scene_root(primary);
    if (is_component_selection_mode(selection.mode)) {
      const std::string active = localization.text(
          active_selection_kind_key(selection.active_kind),
          active_selection_kind_fallback(selection.active_kind));
      ImGui::Text("%s: %s", active_prefix.c_str(), active.c_str());
      if (selection.selected_vertex_count > 0u) {
        ImGui::Text("%s: %u", vertices_prefix.c_str(),
                    selection.selected_vertex_count);
      }
      if (selection.selected_edge_count > 0u) {
        ImGui::Text("%s: %u", edges_prefix.c_str(),
                    selection.selected_edge_count);
      }
      if (selection.selected_face_count > 0u) {
        ImGui::Text("%s: %u", faces_prefix.c_str(),
                    selection.selected_face_count);
      }
    }

    std::vector<EntityId> editable_selected;
    editable_selected.reserve(selected.size());
    for (const EntityId id : selected) {
      if (!document.is_scene_root(id)) {
        editable_selected.push_back(id);
      }
    }

    const BoolAggregate visibility_aggregate =
        aggregate_visibility(document, editable_selected);
    const BoolAggregate lock_aggregate =
        aggregate_lock(document, editable_selected);

    if (!quader_begin_property_table("inspector_properties")) {
      return;
    }

    const bool has_general_properties =
        selected.size() == 1u || visibility_aggregate.has_value ||
        lock_aggregate.has_value;
    if (has_general_properties) {
      const std::string general_label =
          localization.text("ui.inspector.general", "General");
      if (quader_property_section("general", general_label.c_str())) {
        if (selected.size() == 1u) {
          std::array<char, 128> name_buffer;
          copy_name_to_buffer(document.name(primary), name_buffer);
          const std::string name_label =
              localization.text("ui.inspector.name", "Name");
          if (primary_is_scene_root) {
            (void)input_text_with_left_label(
                "name", name_label.c_str(), name_buffer.data(),
                name_buffer.size(), ImGuiInputTextFlags_ReadOnly);
          } else {
            if (input_text_with_left_label(
                    "name", name_label.c_str(), name_buffer.data(),
                    name_buffer.size(),
                    ImGuiInputTextFlags_EnterReturnsTrue |
                        ImGuiInputTextFlags_AutoSelectAll)) {
              (void)context.intent.editor.scene().rename(primary, name_buffer.data());
            }
          }
        }

        if (visibility_aggregate.has_value) {
          bool hidden = !visibility_aggregate.all_true;
          const std::string hidden_label =
              localization.text("ui.inspector.hidden", "Hidden");
          if (checkbox_with_left_label("hidden", hidden_label.c_str(),
                                       &hidden)) {
            std::vector<QuaderSceneVisibilityEdit> edits;
            edits.reserve(editable_selected.size());
            for (const EntityId id : editable_selected) {
              if (const VisibilityComponent *current = document.visibility(id)) {
                edits.push_back(QuaderSceneVisibilityEdit{
                    .entity = id,
                    .before = current->visible,
                    .after = !hidden,
                });
              }
            }
            (void)context.intent.editor.scene().set_visibility(
                std::span<const QuaderSceneVisibilityEdit>(edits.data(),
                                                           edits.size()));
          }
          if (visibility_aggregate.mixed()) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", mixed_label.c_str());
          }
        }

        if (lock_aggregate.has_value) {
          bool locked = lock_aggregate.all_true;
          const std::string locked_label =
              localization.text("ui.inspector.locked", "Locked");
          if (checkbox_with_left_label("locked", locked_label.c_str(),
                                       &locked)) {
            std::vector<QuaderSceneLockEdit> edits;
            edits.reserve(editable_selected.size());
            for (const EntityId id : editable_selected) {
              if (const LockComponent *current = document.lock(id)) {
                edits.push_back(QuaderSceneLockEdit{
                    .entity = id,
                    .before = current->locked,
                    .after = locked,
                });
              }
            }
            (void)context.intent.editor.scene().set_locks(
                std::span<const QuaderSceneLockEdit>(edits.data(),
                                                     edits.size()));
          }
          if (lock_aggregate.mixed()) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", mixed_label.c_str());
          }
        }
      }
    }

    const TransformComponent* transform = document.transform(primary);
    if (transform) {
      std::size_t transform_count = 0;
      for (const EntityId id : selected) {
        if (document.transform(id)) {
          ++transform_count;
        }
        }

        const std::string transform_label =
            localization.text("ui.inspector.transform", "Transform");
        if (quader_property_section("transform", transform_label.c_str())) {
          if (selected.size() > 1u) {
            const std::string spatial_entities = localization.format(
                "ui.inspector.spatial_entities", "{count} spatial entities",
                {{"count", std::to_string(transform_count)}});
            const std::string selection_label =
                localization.text("ui.inspector.selection", "Selection");
            quader_property_row("spatial_entities", selection_label.c_str());
            ImGui::TextDisabled("%s", spatial_entities.c_str());
            quader_end_property_row();
          }

          auto transform_edit_matches = [&](InspectorTransformEditKind kind) {
            return active_transform_edit_ &&
                   active_transform_edit_->kind == kind &&
                   active_transform_edit_->primary == primary &&
                   active_transform_edit_->selection == selected;
          };

          auto refresh_primary_transform = [&]() {
            transform = document.transform(primary);
          };

          auto apply_active_transform_edit_values =
              [&](InspectorTransformEditKind kind, float values[3]) {
                if (!transform_edit_matches(kind)) {
                  return;
                }
                values[0] = active_transform_edit_->values[0];
                values[1] = active_transform_edit_->values[1];
                values[2] = active_transform_edit_->values[2];
              };

          auto stage_transform_edit = [&](InspectorTransformEditKind kind,
                                          const float values[3]) {
            active_transform_edit_ = InspectorTransformEditState{
                .kind = kind,
                .primary = primary,
                .selection = selected,
                .values = {values[0], values[1], values[2]},
            };
          };

          auto begin_transform_preview = [&](InspectorTransformEditKind kind) {
            if (transform_edit_matches(kind) &&
                active_transform_preview_.preview_active()) {
              return true;
            }
            if (context.inspector_preview == nullptr) {
              return false;
            }
            SceneDocument &preview_document = context.inspector_preview->document;
            const AssetRegistry &preview_assets = context.inspector_preview->assets;

            cancel_active_transform_edit();
            refresh_primary_transform();
            if (transform == nullptr) {
              return false;
            }

            switch (kind) {
            case InspectorTransformEditKind::Position:
              return active_transform_preview_
                  .begin_move_selected_to_primary_position_preview(
                      preview_document, primary, "Set Transform");
            case InspectorTransformEditKind::Rotation:
              return active_transform_preview_
                  .begin_rotate_selected_to_primary_euler_preview(
                      preview_document, preview_assets, primary,
                      "Set Transform");
            case InspectorTransformEditKind::Scale:
              return active_transform_preview_
                  .begin_scale_selected_to_primary_scale_preview(
                      preview_document, preview_assets, primary,
                      "Set Transform");
            }
            return false;
          };

          auto final_transform_edit_values =
              [&](InspectorTransformEditKind kind, const float values[3]) {
                std::array<float, 3> result{values[0], values[1], values[2]};
                if (transform_edit_matches(kind)) {
                  result = active_transform_edit_->values;
                }
                return result;
              };

          auto preview_transform_edit = [&](InspectorTransformEditKind kind,
                                            const float values[3]) {
            if (!transform_edit_matches(kind) ||
                !active_transform_preview_.preview_active()) {
              return;
            }
            if (context.inspector_preview == nullptr) {
              return;
            }
            SceneDocument &preview_document = context.inspector_preview->document;

            DocumentCommandResult preview_result;
            switch (kind) {
            case InspectorTransformEditKind::Position:
              preview_result =
                  active_transform_preview_.preview_selected_to_primary_position(
                      preview_document, primary,
                      {values[0], values[1], values[2]});
              break;
            case InspectorTransformEditKind::Rotation:
              preview_result =
                  active_transform_preview_.preview_selected_to_primary_euler(
                      preview_document, primary,
                      {values[0] * kDegToRad, values[1] * kDegToRad,
                       values[2] * kDegToRad});
              break;
            case InspectorTransformEditKind::Scale:
              preview_result =
                  active_transform_preview_.preview_selected_to_primary_scale(
                      preview_document, primary,
                      {values[0], values[1], values[2]});
              break;
            }

            if (!preview_result.ok) {
              cancel_active_transform_edit();
            }
            refresh_primary_transform();
          };

          auto finish_transform_preview = [&]() {
            if (active_transform_preview_.preview_active()) {
              (void)active_transform_preview_.commit(context.intent.editor);
            }
            active_transform_edit_.reset();
            refresh_primary_transform();
          };

          float position[3] = {transform->position.x, transform->position.y,
                               transform->position.z};
          apply_active_transform_edit_values(
              InspectorTransformEditKind::Position, position);
          const std::array<bool, 3> position_mixed =
              mixed_position_axes(document, selected, *transform);
          const std::string position_label =
              localization.text("ui.inspector.position", "Position");
          const AxisDragFloatResult position_result = axis_drag_float3(
              "position", position_label.c_str(), position, position_mixed,
              mixed_label.c_str(), context.style, 0.05f);
          if (position_result.activated) {
            (void)begin_transform_preview(InspectorTransformEditKind::Position);
          }
          if (position_result.activated || position_result.changed) {
            stage_transform_edit(InspectorTransformEditKind::Position,
                                 position);
          }
          if (position_result.changed) {
            preview_transform_edit(InspectorTransformEditKind::Position,
                                   position);
          }
          if (position_result.deactivated_after_edit) {
            const std::array<float, 3> final_position =
                final_transform_edit_values(
                    InspectorTransformEditKind::Position, position);
            preview_transform_edit(InspectorTransformEditKind::Position,
                                   final_position.data());
            finish_transform_preview();
          } else if (position_result.deactivated) {
            cancel_active_transform_edit();
            refresh_primary_transform();
          }

          const QVec3 displayed_rotation =
              positive_euler_radians(transform->rotation_euler);
          float rotation[3] = {
              displayed_rotation.x * kRadToDeg,
              displayed_rotation.y * kRadToDeg,
              displayed_rotation.z * kRadToDeg,
          };
          apply_active_transform_edit_values(
              InspectorTransformEditKind::Rotation, rotation);
          const std::array<bool, 3> rotation_mixed =
              mixed_rotation_axes(document, selected, *transform);
          const std::string rotation_label =
              localization.text("ui.inspector.rotation", "Rotation");
          const AxisDragFloatResult rotation_result = axis_drag_float3(
              "rotation", rotation_label.c_str(), rotation, rotation_mixed,
              mixed_label.c_str(), context.style, 0.5f);
          if (rotation_result.activated) {
            (void)begin_transform_preview(InspectorTransformEditKind::Rotation);
          }
          if (rotation_result.activated || rotation_result.changed) {
            stage_transform_edit(InspectorTransformEditKind::Rotation,
                                 rotation);
          }
          if (rotation_result.changed) {
            preview_transform_edit(InspectorTransformEditKind::Rotation,
                                   rotation);
          }
          if (rotation_result.deactivated_after_edit) {
            const std::array<float, 3> final_rotation =
                final_transform_edit_values(
                    InspectorTransformEditKind::Rotation, rotation);
            preview_transform_edit(InspectorTransformEditKind::Rotation,
                                   final_rotation.data());
            finish_transform_preview();
          } else if (rotation_result.deactivated) {
            cancel_active_transform_edit();
            refresh_primary_transform();
          }

          float scale[3] = {transform->scale.x, transform->scale.y,
                            transform->scale.z};
          apply_active_transform_edit_values(InspectorTransformEditKind::Scale,
                                             scale);
          const std::array<bool, 3> scale_mixed =
              mixed_scale_axes(document, selected, *transform);
          const std::string scale_label =
              localization.text("ui.inspector.scale", "Scale");
          const AxisDragFloatResult scale_result = axis_drag_float3(
              "scale", scale_label.c_str(), scale, scale_mixed,
              mixed_label.c_str(), context.style, 0.02f, 0.001f, 1000.0f);
          if (scale_result.activated) {
            (void)begin_transform_preview(InspectorTransformEditKind::Scale);
          }
          if (scale_result.activated || scale_result.changed) {
            stage_transform_edit(InspectorTransformEditKind::Scale, scale);
          }
          if (scale_result.changed) {
            preview_transform_edit(InspectorTransformEditKind::Scale, scale);
          }
          if (scale_result.deactivated_after_edit) {
            const std::array<float, 3> final_scale =
                final_transform_edit_values(InspectorTransformEditKind::Scale,
                                            scale);
            preview_transform_edit(InspectorTransformEditKind::Scale,
                                   final_scale.data());
            finish_transform_preview();
          } else if (scale_result.deactivated) {
            cancel_active_transform_edit();
            refresh_primary_transform();
          }
        }
    }

    if (selected.size() == 1u && primary_is_scene_root) {
      if (const SceneSettingsComponent *settings =
              document.scene_settings(primary)) {
        draw_scene_settings_inspector(
            context, primary,
            std::span<const EntityId>(selected.data(), selected.size()),
            active_continuous_edit_, *settings, localization);
      }
      quader_end_property_table();
      return;
    }

    if (selected.size() == 1u) {
        if (const MeshComponent *current_mesh = document.mesh(primary)) {
          MeshComponent mesh = *current_mesh;
          auto apply_mesh_component = [&](MeshComponent next) {
          (void)context.intent.editor.scene().set_mesh_component(
              primary, to_api_mesh_component(next));
        };

        const std::string export_label =
            localization.text("ui.inspector.export", "Export");
        if (quader_property_section("export", export_label.c_str())) {
          bool editor_only = mesh.editor_only;
          const std::string editor_only_label =
              localization.text("ui.inspector.editor_only", "Editor Only");
          if (checkbox_with_left_label("editor_only", editor_only_label.c_str(),
                                       &editor_only)) {
            mesh.editor_only = editor_only;
            apply_mesh_component(mesh);
          }
        }

        const std::string lighting_label =
            localization.text("ui.inspector.lighting", "Lighting");
        if (quader_property_section("lighting", lighting_label.c_str())) {
          bool cast_shadows = mesh.cast_shadows;
          const std::string cast_shadows_label =
              localization.text("ui.inspector.cast_shadows", "Cast Shadows");
          if (checkbox_with_left_label(
                  "cast_shadows", cast_shadows_label.c_str(), &cast_shadows)) {
            mesh.cast_shadows = cast_shadows;
            apply_mesh_component(mesh);
          }

          bool double_sided = mesh.double_sided;
          const std::string double_sided_label =
              localization.text("ui.inspector.double_sided", "Double sided");
          if (checkbox_with_left_label(
                  "double_sided", double_sided_label.c_str(), &double_sided)) {
            mesh.double_sided = double_sided;
            apply_mesh_component(mesh);
          }
        }

        const std::string physics_label =
            localization.text("ui.inspector.physics", "Physics");
        if (quader_property_section("physics", physics_label.c_str())) {
          bool generate_collisions = mesh.generate_collisions;
          const std::string generate_collisions_label = localization.text(
              "ui.inspector.generate_collisions", "Generate Collisions");
          if (checkbox_with_left_label("generate_collisions",
                                       generate_collisions_label.c_str(),
                                       &generate_collisions)) {
            mesh.generate_collisions = generate_collisions;
            apply_mesh_component(mesh);
          }

          MeshCollisionType collision_type = mesh.collision_type;
          const std::string collision_type_label = localization.text(
              "ui.inspector.collision_type", "Collision Type");
          const std::vector<std::pair<MeshCollisionType, std::string>>
              collision_options{
                  {MeshCollisionType::Box,
                   localization.text("ui.inspector.collision_type.box", "Box")},
                  {MeshCollisionType::Sphere,
                   localization.text("ui.inspector.collision_type.sphere",
                                     "Sphere")},
                  {MeshCollisionType::Cylinder,
                   localization.text("ui.inspector.collision_type.cylinder",
                                     "Cylinder")},
                  {MeshCollisionType::ConvexHull,
                   localization.text("ui.inspector.collision_type.convex_hull",
                                     "Convex Hull")},
                  {MeshCollisionType::Mesh,
                   localization.text("ui.inspector.collision_type.mesh",
                                     "Mesh")},
              };
          if (combo_with_left_label("collision_type",
                                    collision_type_label.c_str(),
                                    &collision_type, collision_options)) {
            mesh.collision_type = collision_type;
            apply_mesh_component(mesh);
          }

          bool display_collision = mesh.display_collision;
          const std::string display_collision_label = localization.text(
              "ui.inspector.display_collision", "Display Collision");
          if (checkbox_with_left_label("display_collision",
                                       display_collision_label.c_str(),
                                       &display_collision)) {
            mesh.display_collision = display_collision;
            apply_mesh_component(mesh);
          }

          MeshPhysicsBodyType body_type = mesh.body_type;
          const std::string body_type_label =
              localization.text("ui.inspector.body_type", "Body type");
          const std::vector<std::pair<MeshPhysicsBodyType, std::string>>
              body_options{
                  {MeshPhysicsBodyType::StaticBody,
                   localization.text("ui.inspector.body_type.static_body",
                                     "Static Body")},
                  {MeshPhysicsBodyType::RigidBody,
                   localization.text("ui.inspector.body_type.rigid_body",
                                     "Rigid Body")},
              };
          if (combo_with_left_label("body_type", body_type_label.c_str(),
                                    &body_type, body_options)) {
            mesh.body_type = body_type;
            apply_mesh_component(mesh);
          }
        }
      }

      if (const LightComponent *current_light = document.light(primary)) {
        LightComponent light = *current_light;
        auto apply_light_component = [&](LightComponent next,
                                         std::string merge_key = {}) {
          next.range = std::max(next.range, kInspectorLightRangeMin);
          next.intensity = std::max(next.intensity, 0.0f);
          (void)context.intent.editor.scene().set_light_component(
              primary, to_api_light_component(next),
              QuaderSceneEditOptions{.merge_key = std::move(merge_key)});
        };

        const std::string light_label =
            localization.text("ui.inspector.light", "Light");
        if (quader_property_section("light", light_label.c_str())) {
          LightType type = light.type;
          const std::string type_label =
              localization.text("ui.inspector.light_type", "Type");
          const std::vector<std::pair<LightType, std::string>> type_options{
              {LightType::Directional,
               localization.text("ui.inspector.light_type.directional",
                                 "Directional")},
              {LightType::Point,
               localization.text("ui.inspector.light_type.point", "Point")},
              {LightType::Spot,
               localization.text("ui.inspector.light_type.spot", "Spot")},
          };
          if (combo_with_left_label("light_type", type_label.c_str(), &type,
                                    type_options)) {
            light.type = type;
            apply_light_component(light);
          }

          float color[3] = {light.color.x, light.color.y, light.color.z};
          const std::string color_label =
              localization.text("ui.inspector.color", "Color");
          const UIContinuousEditResult color_edit = color_edit3_with_left_label(
              "light_color", color_label.c_str(), color,
              active_continuous_edit_, primary,
              std::span<const EntityId>(selected.data(), selected.size()));
          if (color_edit.should_apply()) {
            light.color = {
                std::clamp(color[0], 0.0f, 1.0f),
                std::clamp(color[1], 0.0f, 1.0f),
                std::clamp(color[2], 0.0f, 1.0f),
            };
            apply_light_component(light, color_edit.merge_key);
          }

          float intensity = light.intensity;
          const std::string intensity_label =
              localization.text("ui.inspector.intensity", "Intensity");
          const UIContinuousEditResult intensity_edit =
              drag_float_with_left_label(
                  "light_intensity", intensity_label.c_str(), &intensity,
                  active_continuous_edit_, primary,
                  std::span<const EntityId>(selected.data(), selected.size()),
                  0.01f, 0.0f, 16.0f, "%.3f");
          if (intensity_edit.should_apply()) {
            light.intensity = intensity;
            apply_light_component(light, intensity_edit.merge_key);
          }

          if (light.type == LightType::Point || light.type == LightType::Spot) {
            float range = light.range;
            const std::string range_label =
                localization.text("ui.inspector.range", "Range");
            const UIContinuousEditResult range_edit =
                drag_float_with_left_label(
                    "light_range", range_label.c_str(), &range,
                    active_continuous_edit_, primary,
                    std::span<const EntityId>(selected.data(), selected.size()),
                    0.1f, kInspectorLightRangeMin,
                    kInspectorLightRangeSliderMax, "%.2f",
                    ImGuiSliderFlags_None);
            if (range_edit.should_apply()) {
              light.range = range;
              apply_light_component(light, range_edit.merge_key);
            }
          }

          bool cast_shadows = light.casts_shadows;
          const std::string cast_shadows_label =
              localization.text("ui.inspector.cast_shadows", "Cast Shadows");
          if (checkbox_with_left_label("light_cast_shadows",
                                       cast_shadows_label.c_str(),
                                       &cast_shadows)) {
            light.casts_shadows = cast_shadows;
            apply_light_component(light);
          }
        }
      }
    }

    if (selected.size() > 1u) {
      std::size_t mesh_count = 0;
      std::size_t light_count = 0;
      for (const EntityId id : selected) {
        mesh_count += document.mesh(id) ? 1u : 0u;
        light_count += document.light(id) ? 1u : 0u;
      }

      const std::string components_label =
          localization.text("ui.inspector.components", "Components");
      if (quader_property_section("components", components_label.c_str())) {
        const std::string meshes_label =
            localization.text("ui.inspector.meshes", "Meshes");
        const std::string lights_label =
            localization.text("ui.inspector.lights", "Lights");
        quader_property_row("meshes", meshes_label.c_str());
        ImGui::Text("%llu", static_cast<unsigned long long>(mesh_count));
        quader_end_property_row();
        quader_property_row("lights", lights_label.c_str());
        ImGui::Text("%llu", static_cast<unsigned long long>(light_count));
        quader_end_property_row();
      }
    }
    quader_end_property_table();
}

} // namespace quader

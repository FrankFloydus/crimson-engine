////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/imgui_quader_slider.hpp"

#include <algorithm>
#include <array>

#include <imgui_internal.h>

namespace quader {
namespace {

constexpr float kSliderTextTrackGap = 6.0f;
constexpr float kMinimumTrackThickness = 3.0f;
constexpr float kMinimumGrabRadius = 5.0f;

ImVec4 mix_color(const ImVec4 &base, const ImVec4 &accent, float amount) {
  const float clamped = std::clamp(amount, 0.0f, 1.0f);
  return {
      base.x + (accent.x - base.x) * clamped,
      base.y + (accent.y - base.y) * clamped,
      base.z + (accent.z - base.z) * clamped,
      base.w + (accent.w - base.w) * clamped,
  };
}

float slider_grab_radius() {
  return std::max(kMinimumGrabRadius, ImGui::GetFontSize() * 0.34f);
}

float slider_track_thickness() {
  return std::max(kMinimumTrackThickness, ImGui::GetFontSize() * 0.18f);
}

void render_slider_value_text(const ImRect &text_rect, const char *value_text) {
  const ImVec2 value_size = ImGui::CalcTextSize(value_text);
  ImGui::RenderTextClipped(text_rect.Min, text_rect.Max, value_text, nullptr,
                           &value_size, {1.0f, 0.0f});
}

void render_slider_label_text(const ImRect &text_rect, const char *label,
                              const char *label_end, float value_width) {
  if (label == label_end) {
    return;
  }

  const ImGuiStyle &style = ImGui::GetStyle();
  const float label_max_x =
      text_rect.Max.x - value_width - style.ItemInnerSpacing.x;
  if (label_max_x <= text_rect.Min.x) {
    return;
  }

  const ImVec2 label_size = ImGui::CalcTextSize(label, label_end, false);
  ImGui::RenderTextClipped(text_rect.Min, {label_max_x, text_rect.Max.y},
                           label, label_end, &label_size, {0.0f, 0.0f});
}

void render_slider_text_row(const ImRect &text_rect, const char *label,
                            const char *label_end, const char *value_text) {
  const ImVec2 value_size = ImGui::CalcTextSize(value_text);
  render_slider_label_text(text_rect, label, label_end, value_size.x);
  render_slider_value_text(text_rect, value_text);
}

ImVec4 slider_track_color(bool hovered, bool held) {
  const ImVec4 frame = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
  const ImVec4 frame_hovered = ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered);
  const ImVec4 border = ImGui::GetStyleColorVec4(ImGuiCol_Border);
  return mix_color(held || hovered ? frame_hovered : frame, border,
                   held ? 0.42f : 0.28f);
}

ImVec4 slider_accent_color(bool held) {
  return ImGui::GetStyleColorVec4(held ? ImGuiCol_SliderGrabActive
                                       : ImGuiCol_SliderGrab);
}

void render_slider_track(const ImRect &track_rect, const ImRect &grab_bb,
                         bool hovered, bool held) {
  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  const float radius = slider_grab_radius();
  const float track_thickness = slider_track_thickness();
  const float track_radius = track_thickness * 0.5f;
  const float track_y = track_rect.GetCenter().y;
  const float track_min_x = track_rect.Min.x + radius;
  const float track_max_x = std::max(track_min_x, track_rect.Max.x - radius);
  const float grab_x = std::clamp(grab_bb.GetCenter().x, track_min_x,
                                  track_max_x);

  const ImU32 track_color =
      ImGui::ColorConvertFloat4ToU32(slider_track_color(hovered, held));
  const ImVec4 accent = slider_accent_color(held);
  const ImU32 fill_color = ImGui::ColorConvertFloat4ToU32(accent);
  const ImVec2 track_min{track_min_x, track_y - track_radius};
  const ImVec2 track_max{track_max_x, track_y + track_radius};
  draw_list->AddRectFilled(track_min, track_max, track_color, track_radius);
  draw_list->AddRectFilled(track_min, {grab_x, track_max.y}, fill_color,
                           track_radius);

  const ImVec4 text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
  const ImVec4 thumb_border = mix_color(text, accent, hovered ? 0.18f : 0.28f);
  draw_list->AddCircleFilled({grab_x, track_y}, radius, fill_color, 24);
  draw_list->AddCircle({grab_x, track_y}, radius,
                       ImGui::ColorConvertFloat4ToU32(thumb_border), 24, 1.4f);
}

bool quader_theme_slider_scalar(const char *label, ImGuiDataType data_type,
                                void *value, const void *minimum,
                                const void *maximum, const char *format,
                                ImGuiSliderFlags flags) {
  ImGuiWindow *window = ImGui::GetCurrentWindow();
  if (window->SkipItems) {
    return false;
  }

  ImGuiContext &context = *GImGui;
  const ImGuiStyle &style = context.Style;
  const ImGuiID id = window->GetID(label);
  const char *label_end = ImGui::FindRenderedTextEnd(label);
  const float width = ImGui::CalcItemWidth();
  const float grab_radius = slider_grab_radius();
  const float text_height = ImGui::GetFontSize();
  const float track_height = grab_radius * 2.0f;
  const ImVec2 cursor = window->DC.CursorPos;
  const ImRect total_rect(
      cursor, {cursor.x + width,
               cursor.y + text_height + kSliderTextTrackGap + track_height});
  const float track_y = total_rect.Min.y + text_height +
                        kSliderTextTrackGap + track_height * 0.5f;
  const ImRect track_rect({total_rect.Min.x, track_y - grab_radius},
                          {total_rect.Max.x, track_y + grab_radius});

  ImGui::ItemSize(total_rect, style.FramePadding.y);
  const bool temp_input_allowed = (flags & ImGuiSliderFlags_NoInput) == 0;
  if (!ImGui::ItemAdd(total_rect, id, &track_rect,
                      temp_input_allowed ? ImGuiItemFlags_Inputable
                                         : ImGuiItemFlags_None)) {
    return false;
  }

  if (temp_input_allowed && ImGui::TempInputIsActive(id)) {
    const bool clamp_input = (flags & ImGuiSliderFlags_AlwaysClamp) != 0;
    return ImGui::TempInputScalar(total_rect, id, label, data_type, value,
                                  format, clamp_input ? minimum : nullptr,
                                  clamp_input ? maximum : nullptr);
  }

  bool hovered = false;
  bool held = false;
  (void)ImGui::ButtonBehavior(total_rect, id, &hovered, &held);

  const bool prefer_input =
      context.NavActivateId == id &&
      (context.NavActivateFlags & ImGuiActivateFlags_PreferInput) != 0;
  const bool double_clicked =
      hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
  if (temp_input_allowed && (prefer_input || double_clicked)) {
    ImGui::SetActiveID(id, window);
    ImGui::SetFocusID(id, window);
    ImGui::FocusWindow(window);
    const bool clamp_input = (flags & ImGuiSliderFlags_AlwaysClamp) != 0;
    return ImGui::TempInputScalar(total_rect, id, label, data_type, value,
                                  format, clamp_input ? minimum : nullptr,
                                  clamp_input ? maximum : nullptr);
  }

  ImRect grab_bb;
  const bool changed = ImGui::SliderBehavior(
      track_rect, id, data_type, value, minimum, maximum, format, flags,
      &grab_bb);
  if (changed) {
    ImGui::MarkItemEdited(id);
  }

  ImGui::RenderNavCursor(total_rect, id);
  std::array<char, 64> value_text{};
  ImGui::DataTypeFormatString(value_text.data(),
                              static_cast<int>(value_text.size()), data_type,
                              value, format);
  const ImRect text_rect(total_rect.Min,
                         {total_rect.Max.x, total_rect.Min.y + text_height});
  render_slider_text_row(text_rect, label, label_end, value_text.data());
  render_slider_track(track_rect, grab_bb, hovered, held);
  return changed;
}

} // namespace

bool quader_theme_slider_float(const char *label, float *value, float minimum,
                               float maximum, const char *format,
                               ImGuiSliderFlags flags) {
  return quader_theme_slider_scalar(label, ImGuiDataType_Float, value,
                                    &minimum, &maximum, format, flags);
}

bool quader_theme_slider_int(const char *label, int *value, int minimum,
                             int maximum, const char *format,
                             ImGuiSliderFlags flags) {
  return quader_theme_slider_scalar(label, ImGuiDataType_S32, value, &minimum,
                                    &maximum, format, flags);
}

} // namespace quader

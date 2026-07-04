////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/imgui_quader_widgets.hpp"

#include "ui/phosphor_icons.hpp"

#include <algorithm>
#include <cfloat>
#include <vector>

#include <imgui_internal.h>

namespace quader {
namespace {

const char *rendered_text_end(const char *label) {
  const char *end = label;
  while (end[0] != '\0') {
    if (end[0] == '#' && end[1] == '#') {
      return end;
    }
    ++end;
  }
  return end;
}

void draw_caret_text(ImDrawList &draw_list, const ImVec2 position,
                     const ImU32 color, const char *icon) {
  draw_list.AddText(position, color, icon);
}

void draw_combo_caret(ImDrawList &draw_list, const ImVec2 min,
                      const ImVec2 max) {
  const ImGuiStyle &style = ImGui::GetStyle();
  const ImVec2 icon_size = ImGui::CalcTextSize(kPhosphorCaretDown);
  const float reserved_width =
      std::max(max.y - min.y, icon_size.x + style.FramePadding.x * 2.0f);
  const float icon_x = max.x - reserved_width +
                       std::max(0.0f, (reserved_width - icon_size.x) * 0.5f);
  if (icon_x <= min.x + style.FramePadding.x) {
    return;
  }

  const float icon_y =
      min.y + std::max(0.0f, (max.y - min.y - icon_size.y) * 0.5f);
  draw_caret_text(draw_list, {icon_x, icon_y},
                  ImGui::GetColorU32(ImGuiCol_Text), kPhosphorCaretDown);
}

ImGuiComboFlags quader_combo_popup_flags(ImGuiComboFlags flags) {
  return flags & ~ImGuiComboFlags_HeightMask_;
}

ImVec4 mix_color(const ImVec4 &base, const ImVec4 &accent, float amount) {
  const float clamped = std::clamp(amount, 0.0f, 1.0f);
  return {
      base.x + (accent.x - base.x) * clamped,
      base.y + (accent.y - base.y) * clamped,
      base.z + (accent.z - base.z) * clamped,
      base.w + (accent.w - base.w) * clamped,
  };
}

float property_table_label_width(const QuaderPropertyTableOptions &options) {
  const float available_width = std::max(0.0f, ImGui::GetContentRegionAvail().x);
  const float maximum_label_width =
      std::max(options.minimum_label_column_width,
               available_width - options.minimum_value_column_width);
  return std::clamp(options.label_column_width,
                    options.minimum_label_column_width, maximum_label_width);
}

void draw_clipped_table_text(const char *label, float indent,
                             const ImVec4 &color) {
  const ImGuiStyle &style = ImGui::GetStyle();
  ImGui::AlignTextToFramePadding();
  const ImVec2 cursor = ImGui::GetCursorScreenPos();
  const float width = std::max(0.0f, ImGui::GetContentRegionAvail().x);
  const float height = ImGui::GetFrameHeight();
  ImGui::Dummy({width, height});

  const float text_y =
      cursor.y + std::max(0.0f, (height - ImGui::GetFontSize()) * 0.5f);
  const ImVec2 text_min{cursor.x + indent, text_y};
  const ImVec4 clip_rect{cursor.x, cursor.y, cursor.x + width,
                         cursor.y + height + style.FramePadding.y};
  ImGui::GetWindowDrawList()->AddText(
      ImGui::GetFont(), ImGui::GetFontSize(), text_min,
      ImGui::ColorConvertFloat4ToU32(color), label, nullptr, 0.0f,
      &clip_rect);
}

void draw_tree_expander(ImDrawList &draw_list, ImVec2 center, float size,
                        bool open, ImU32 color, ImU32 border_color) {
  const float half = size * 0.5f;
  const ImVec2 min{center.x - half, center.y - half};
  const ImVec2 max{center.x + half, center.y + half};
  draw_list.AddRect(min, max, border_color, 1.0f);
  draw_list.AddLine({min.x + 2.0f, center.y}, {max.x - 2.0f, center.y}, color,
                    1.0f);
  if (!open) {
    draw_list.AddLine({center.x, min.y + 2.0f}, {center.x, max.y - 2.0f},
                      color, 1.0f);
  }
}

float property_tree_box_size() {
  return std::max(9.0f, ImGui::GetFontSize() * 0.62f);
}

float property_tree_label_indent() {
  const ImGuiStyle &style = ImGui::GetStyle();
  return style.FramePadding.x + property_tree_box_size() +
         style.ItemInnerSpacing.x;
}

float property_tree_guide_x() {
  const ImGuiStyle &style = ImGui::GetStyle();
  return style.FramePadding.x + property_tree_box_size() * 0.5f;
}

void draw_tree_child_guides(ImDrawList &draw_list, ImVec2 cell_min,
                            float row_height, bool has_next_sibling) {
  const ImU32 guide_color = ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.85f);
  const float guide_x = cell_min.x + property_tree_guide_x();
  const float center_y = cell_min.y + row_height * 0.5f;
  const float label_x = cell_min.x + property_tree_label_indent();
  const float connector_x =
      std::max(guide_x,
               label_x - ImGui::GetStyle().ItemInnerSpacing.x * 0.5f);
  const float line_top = cell_min.y - 1.0f;
  const float line_bottom =
      has_next_sibling ? cell_min.y + row_height + 1.0f : center_y;
  draw_list.AddLine({guide_x, line_top}, {guide_x, line_bottom}, guide_color,
                    1.0f);
  draw_list.AddLine({guide_x, center_y}, {connector_x, center_y}, guide_color,
                    1.0f);
}

/**
 * Stores a deferred property-tree guide segment for the next visible child row.
 */
struct PendingPropertyChildGuide {
  bool active = false;
  ImDrawList *draw_list = nullptr;
  ImVec2 cell_min{};
  ImVec2 clip_min{};
  ImVec2 clip_max{};
  float row_height = 0.0f;
};

std::vector<PendingPropertyChildGuide> &property_table_guide_stack() {
  static std::vector<PendingPropertyChildGuide> stack;
  return stack;
}

PendingPropertyChildGuide *current_property_child_guide() {
  std::vector<PendingPropertyChildGuide> &stack = property_table_guide_stack();
  return stack.empty() ? nullptr : &stack.back();
}

void flush_pending_property_child_guide(bool has_next_sibling) {
  PendingPropertyChildGuide *pending = current_property_child_guide();
  if (pending == nullptr || !pending->active || pending->draw_list == nullptr) {
    return;
  }

  pending->draw_list->PushClipRect(pending->clip_min, pending->clip_max, true);
  draw_tree_child_guides(*pending->draw_list, pending->cell_min,
                         pending->row_height, has_next_sibling);
  pending->draw_list->PopClipRect();
  pending->active = false;
}

void queue_property_child_guide(ImVec2 cell_min, float row_height) {
  PendingPropertyChildGuide *pending = current_property_child_guide();
  if (pending == nullptr) {
    return;
  }

  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  pending->active = true;
  pending->draw_list = draw_list;
  pending->cell_min = cell_min;
  pending->clip_min = draw_list->GetClipRectMin();
  pending->clip_max = draw_list->GetClipRectMax();
  pending->row_height = row_height;
}

void activate_slider_temp_input_on_double_click(const char *label,
                                                ImGuiSliderFlags flags) {
  if ((flags & ImGuiSliderFlags_NoInput) != 0) {
    return;
  }

  ImGuiContext &context = *GImGui;
  ImGuiWindow *window = ImGui::GetCurrentWindow();
  if (window == nullptr || window->SkipItems) {
    return;
  }

  const ImGuiID id = window->GetID(label);
  const char *label_end = ImGui::FindRenderedTextEnd(label);
  const ImVec2 label_size = ImGui::CalcTextSize(label, label_end, false);
  const float width = ImGui::CalcItemWidth();
  const ImVec2 cursor = window->DC.CursorPos;
  const ImRect frame_rect(
      cursor,
      ImVec2(cursor.x + width,
             cursor.y + label_size.y + context.Style.FramePadding.y * 2.0f));
  if (!ImGui::IsMouseHoveringRect(frame_rect.Min, frame_rect.Max) ||
      !ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) ||
      !ImGui::TestKeyOwner(ImGuiKey_MouseLeft, id)) {
    return;
  }

  ImGui::SetKeyOwner(ImGuiKey_MouseLeft, id);
  context.NavActivateId = id;
  context.NavActivateFlags = ImGuiActivateFlags_PreferInput;
}

} // namespace

bool quader_begin_combo(const char *label, const char *preview_value,
                        ImGuiComboFlags flags, bool close_on_repeated_click) {
  ImGuiContext &context = *GImGui;
  ImGuiWindow *window = ImGui::GetCurrentWindow();

  const ImGuiNextWindowDataFlags next_window_flags =
      context.NextWindowData.HasFlags;
  context.NextWindowData.ClearFlags();
  if (window->SkipItems) {
    return false;
  }

  const ImGuiStyle &style = context.Style;
  const ImGuiID id = window->GetID(label);
  const char *label_end = rendered_text_end(label);
  const ImVec2 label_size = ImGui::CalcTextSize(label, label_end, false);
  const float width = ImGui::CalcItemWidth();
  const ImVec2 cursor = window->DC.CursorPos;
  const ImRect frame_rect(
      cursor, ImVec2(cursor.x + width,
                     cursor.y + label_size.y + style.FramePadding.y * 2.0f));
  const ImRect total_rect(
      frame_rect.Min,
      ImVec2(frame_rect.Max.x + (label_size.x > 0.0f
                                     ? style.ItemInnerSpacing.x + label_size.x
                                     : 0.0f),
             frame_rect.Max.y));
  ImGui::ItemSize(total_rect, style.FramePadding.y);
  if (!ImGui::ItemAdd(total_rect, id, &frame_rect)) {
    return false;
  }

  bool hovered = false;
  bool held = false;
  const bool pressed = ImGui::ButtonBehavior(frame_rect, id, &hovered, &held);
  const ImGuiID popup_id = ImHashStr("##ComboPopup", 0, id);
  const ImGuiID was_open_storage_id = ImHashStr("##QuaderComboWasOpen", 0, id);
  const bool was_open_last_frame =
      window->StateStorage.GetInt(was_open_storage_id, 0) != 0;
  bool popup_open = ImGui::IsPopupOpen(popup_id, ImGuiPopupFlags_None);
  if (pressed) {
    if (popup_open && close_on_repeated_click) {
      ImGui::ClosePopupToLevel(context.BeginPopupStack.Size, true);
      popup_open = false;
    } else if (!popup_open &&
               !(close_on_repeated_click && was_open_last_frame)) {
      ImGui::OpenPopupEx(popup_id, ImGuiPopupFlags_None);
      popup_open = true;
    }
  }
  window->StateStorage.SetInt(was_open_storage_id, popup_open ? 1 : 0);

  const ImU32 frame_color = ImGui::GetColorU32(
      (popup_open || hovered) ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
  ImGui::RenderNavCursor(frame_rect, id);
  window->DrawList->AddRectFilled(frame_rect.Min, frame_rect.Max, frame_color,
                                  style.FrameRounding);
  ImGui::RenderFrameBorder(frame_rect.Min, frame_rect.Max, style.FrameRounding);

  const ImVec2 icon_size = ImGui::CalcTextSize(kPhosphorCaretDown);
  const float reserved_width = std::max(
      frame_rect.GetHeight(), icon_size.x + style.FramePadding.x * 2.0f);
  if (preview_value != nullptr) {
    ImGui::RenderTextClipped(
        ImVec2(frame_rect.Min.x + style.FramePadding.x,
               frame_rect.Min.y + style.FramePadding.y),
        ImVec2(std::max(frame_rect.Min.x, frame_rect.Max.x - reserved_width),
               frame_rect.Max.y),
        preview_value, nullptr, nullptr);
  }
  draw_combo_caret(*window->DrawList, frame_rect.Min, frame_rect.Max);

  if (label_size.x > 0.0f) {
    ImGui::RenderText(ImVec2(frame_rect.Max.x + style.ItemInnerSpacing.x,
                             frame_rect.Min.y + style.FramePadding.y),
                      label, label_end, false);
  }

  if (!popup_open) {
    return false;
  }

  context.NextWindowData.HasFlags = next_window_flags;
  const bool popup_visible = ImGui::BeginComboPopup(
      popup_id, frame_rect, quader_combo_popup_flags(flags));
  window->StateStorage.SetInt(was_open_storage_id, popup_visible ? 1 : 0);
  return popup_visible;
}

bool quader_collapsing_header(const char *label, ImGuiTreeNodeFlags flags) {
  ImGuiStorage *storage = ImGui::GetStateStorage();
  const ImGuiID id = ImGui::GetID(label);
  const bool default_open = (flags & ImGuiTreeNodeFlags_DefaultOpen) != 0;
  bool open = storage->GetInt(id, default_open ? 1 : 0) != 0;

  const ImGuiStyle &style = ImGui::GetStyle();
  const float height = ImGui::GetFrameHeight();
  const float width = std::max(ImGui::GetContentRegionAvail().x, height);
  if (ImGui::InvisibleButton(label, {width, height})) {
    open = !open;
    storage->SetInt(id, open ? 1 : 0);
  }

  const bool hovered = ImGui::IsItemHovered();
  const bool active = ImGui::IsItemActive();
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  const ImU32 background = ImGui::GetColorU32(
      active ? ImGuiCol_HeaderActive
             : (hovered ? ImGuiCol_HeaderHovered : ImGuiCol_Header));
  draw_list->AddRectFilled(min, max, background, style.FrameRounding);

  const char *caret_icon = open ? kPhosphorCaretDown : kPhosphorCaretRight;
  const ImVec2 icon_size = ImGui::CalcTextSize(caret_icon);
  const float icon_x = min.x + style.FramePadding.x;
  const float icon_y = min.y + std::max(0.0f, (height - icon_size.y) * 0.5f);
  draw_caret_text(*draw_list, {icon_x, icon_y},
                  ImGui::GetColorU32(ImGuiCol_Text), caret_icon);

  const char *text_end = rendered_text_end(label);
  const float text_x = min.x + style.FramePadding.x + height;
  const float text_y =
      min.y + std::max(0.0f, (height - ImGui::GetFontSize()) * 0.5f);
  draw_list->AddText({text_x, text_y}, ImGui::GetColorU32(ImGuiCol_Text), label,
                     text_end);

  return open;
}

bool quader_begin_property_table(const char *stable_id,
                                 QuaderPropertyTableOptions options) {
  ImGui::PushID(stable_id);

  const ImGuiStyle &style = ImGui::GetStyle();
  const ImVec4 window_bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
  const ImVec4 frame_bg = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
  const ImVec4 header_bg = ImGui::GetStyleColorVec4(ImGuiCol_Header);
  ImVec4 row_bg = mix_color(window_bg, frame_bg, 0.72f);
  ImVec4 row_bg_alt = mix_color(window_bg, header_bg, 0.96f);
  row_bg.w = 1.0f;
  row_bg_alt.w = 1.0f;

  ImGui::PushStyleColor(ImGuiCol_TableRowBg, row_bg);
  ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, row_bg_alt);
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,
                      ImVec2(style.FramePadding.x, style.FramePadding.y));

  constexpr ImGuiTableFlags kPropertyTableFlags =
      ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable |
      ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
      ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_NoSavedSettings |
      ImGuiTableFlags_NoPadOuterX;
  if (!ImGui::BeginTable("##PropertyTable", 2, kPropertyTableFlags)) {
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    ImGui::PopID();
    return false;
  }
  property_table_guide_stack().push_back({});

  ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed,
                          property_table_label_width(options));
  ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.0f);
  return true;
}

bool quader_property_section(const char *stable_id, const char *label,
                             ImGuiTreeNodeFlags flags) {
  flush_pending_property_child_guide(false);
  ImGui::PushID(stable_id);
  ImGuiStorage *storage = ImGui::GetStateStorage();
  const ImGuiID id = ImGui::GetID("##PropertySection");
  const bool default_open = (flags & ImGuiTreeNodeFlags_DefaultOpen) != 0;
  bool open = storage->GetInt(id, default_open ? 1 : 0) != 0;

  ImGui::TableNextRow();
  ImGui::TableSetColumnIndex(0);
  const ImGuiStyle &style = ImGui::GetStyle();
  const ImVec2 cell_min = ImGui::GetCursorScreenPos();
  const float cell_width = std::max(0.0f, ImGui::GetContentRegionAvail().x);
  const float row_height = ImGui::GetFrameHeight();
  if (ImGui::InvisibleButton("##toggle", {cell_width, row_height})) {
    open = !open;
    storage->SetInt(id, open ? 1 : 0);
  }

  const bool hovered = ImGui::IsItemHovered();
  const bool active = ImGui::IsItemActive();
  if (active || hovered) {
    const ImU32 interaction_color = ImGui::GetColorU32(
        active ? ImGuiCol_HeaderActive : ImGuiCol_HeaderHovered);
    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, interaction_color);
    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, interaction_color);
  }

  const float box_size = property_tree_box_size();
  const ImVec2 box_center{
      cell_min.x + style.FramePadding.x + box_size * 0.5f,
      cell_min.y + row_height * 0.5f,
  };
  draw_tree_expander(*ImGui::GetWindowDrawList(), box_center, box_size, open,
                     ImGui::GetColorU32(ImGuiCol_Text),
                     ImGui::GetColorU32(ImGuiCol_Border));

  const ImVec4 text_color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
  const ImVec2 text_cursor = ImGui::GetItemRectMin();
  ImGui::SetCursorScreenPos(text_cursor);
  draw_clipped_table_text(label, property_tree_label_indent(), text_color);

  ImGui::TableSetColumnIndex(1);
  ImGui::Dummy({0.0f, row_height});
  ImGui::PopID();
  return open;
}

void quader_property_row(const char *stable_id, const char *label) {
  flush_pending_property_child_guide(true);
  ImGui::PushID(stable_id);
  ImGui::TableNextRow();
  ImGui::TableSetColumnIndex(0);
  const ImVec2 cell_min = ImGui::GetCursorScreenPos();
  draw_tree_child_guides(*ImGui::GetWindowDrawList(), cell_min,
                         ImGui::GetFrameHeight(), false);
  queue_property_child_guide(cell_min, ImGui::GetFrameHeight());
  draw_clipped_table_text(label, property_tree_label_indent(),
                          ImGui::GetStyleColorVec4(ImGuiCol_Text));
  ImGui::TableSetColumnIndex(1);
  ImGui::SetNextItemWidth(-FLT_MIN);
}

void quader_end_property_row() { ImGui::PopID(); }

void quader_end_property_table() {
  flush_pending_property_child_guide(false);
  if (!property_table_guide_stack().empty()) {
    property_table_guide_stack().pop_back();
  }
  ImGui::EndTable();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(2);
  ImGui::PopID();
}

bool quader_slider_float(const char *label, float *value, float minimum,
                         float maximum, const char *format,
                         ImGuiSliderFlags flags) {
  activate_slider_temp_input_on_double_click(label, flags);
  return ImGui::SliderFloat(label, value, minimum, maximum, format, flags);
}

bool quader_slider_int(const char *label, int *value, int minimum, int maximum,
                       const char *format, ImGuiSliderFlags flags) {
  activate_slider_temp_input_on_double_click(label, flags);
  return ImGui::SliderInt(label, value, minimum, maximum, format, flags);
}

} // namespace quader

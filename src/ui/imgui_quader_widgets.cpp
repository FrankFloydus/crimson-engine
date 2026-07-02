////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/imgui_quader_widgets.hpp"

#include "ui/phosphor_icons.hpp"

#include <algorithm>

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

  const ImVec2 icon_size = ImGui::CalcTextSize(kPhosphorCaretRight);
  const float icon_x = min.x + style.FramePadding.x;
  const float icon_y = min.y + std::max(0.0f, (height - icon_size.y) * 0.5f);
  draw_caret_text(*draw_list, {icon_x, icon_y},
                  ImGui::GetColorU32(ImGuiCol_Text), kPhosphorCaretRight);

  const char *text_end = rendered_text_end(label);
  const float text_x = min.x + style.FramePadding.x + height;
  const float text_y =
      min.y + std::max(0.0f, (height - ImGui::GetFontSize()) * 0.5f);
  draw_list->AddText({text_x, text_y}, ImGui::GetColorU32(ImGuiCol_Text), label,
                     text_end);

  return open;
}

} // namespace quader

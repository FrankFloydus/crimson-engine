////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/imgui_popup_utils.hpp"

#include <imgui_internal.h>

#include <string_view>

namespace quader {
namespace {

bool screen_point_inside(ImVec2 point, ImVec2 min, ImVec2 max) {
  return point.x >= min.x && point.x <= max.x && point.y >= min.y &&
         point.y <= max.y;
}

bool any_mouse_button_clicked() {
  return ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
         ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
         ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
}

bool open_combo_popup_exists(const ImGuiContext &context) {
  for (const ImGuiPopupData &popup : context.OpenPopupStack) {
    if (popup.Window == nullptr || popup.Window->Name == nullptr) {
      continue;
    }
    if (std::string_view(popup.Window->Name).starts_with("##Combo_")) {
      return true;
    }
  }
  return false;
}

} // namespace

bool imgui_open_popup_contains_screen_point(ImVec2 screen_point) {
  ImGuiContext *context = ImGui::GetCurrentContext();
  if (context == nullptr) {
    return false;
  }

  for (const ImGuiPopupData &popup : context->OpenPopupStack) {
    if (popup.Window != nullptr && popup.Window->WasActive &&
        popup.Window->Rect().Contains(screen_point)) {
      return true;
    }
  }
  return false;
}

bool imgui_close_popup_for_pass_through_click(ImVec2 pass_through_min,
                                              ImVec2 pass_through_max) {
  if (!any_mouse_button_clicked()) {
    return false;
  }

  ImGuiContext *context = ImGui::GetCurrentContext();
  if (context == nullptr || context->OpenPopupStack.empty()) {
    return false;
  }

  const ImVec2 mouse = ImGui::GetMousePos();
  if (!screen_point_inside(mouse, pass_through_min, pass_through_max) ||
      imgui_open_popup_contains_screen_point(mouse)) {
    return false;
  }

  ImGui::ClosePopupsExceptModals();
  return true;
}

bool imgui_close_combo_popup_for_click_inside(ImVec2 target_min,
                                              ImVec2 target_max,
                                              ImVec2 exclusion_min,
                                              ImVec2 exclusion_max) {
  if (!any_mouse_button_clicked()) {
    return false;
  }

  ImGuiContext *context = ImGui::GetCurrentContext();
  if (context == nullptr || context->OpenPopupStack.empty()) {
    return false;
  }
  if (!open_combo_popup_exists(*context)) {
    return false;
  }

  const ImVec2 mouse = ImGui::GetMousePos();
  if (!screen_point_inside(mouse, target_min, target_max) ||
      screen_point_inside(mouse, exclusion_min, exclusion_max) ||
      imgui_open_popup_contains_screen_point(mouse)) {
    return false;
  }

  ImGui::ClosePopupsExceptModals();
  return true;
}

} // namespace quader

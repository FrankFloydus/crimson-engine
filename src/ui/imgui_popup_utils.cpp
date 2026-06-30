////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/imgui_popup_utils.hpp"

#include <imgui_internal.h>

namespace quader {
namespace {

bool screenPointInside(ImVec2 point, ImVec2 min, ImVec2 max) {
    return point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y;
}

bool anyMouseButtonClicked() {
    return ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
            ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
            ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
}

} // namespace

bool imguiOpenPopupContainsScreenPoint(ImVec2 screenPoint) {
    ImGuiContext* context = ImGui::GetCurrentContext();
    if (context == nullptr) {
        return false;
    }

    for (const ImGuiPopupData& popup : context->OpenPopupStack) {
        if (popup.Window != nullptr && popup.Window->WasActive && popup.Window->Rect().Contains(screenPoint)) {
            return true;
        }
    }
    return false;
}

bool imguiClosePopupForPassThroughClick(ImVec2 passThroughMin, ImVec2 passThroughMax) {
    if (!anyMouseButtonClicked()) {
        return false;
    }

    ImGuiContext* context = ImGui::GetCurrentContext();
    if (context == nullptr || context->OpenPopupStack.empty()) {
        return false;
    }

    const ImVec2 mouse = ImGui::GetMousePos();
    if (!screenPointInside(mouse, passThroughMin, passThroughMax) || imguiOpenPopupContainsScreenPoint(mouse)) {
        return false;
    }

    ImGui::ClosePopupsExceptModals();
    return true;
}

bool imguiClosePopupForClickOutside(ImVec2 exclusionMin, ImVec2 exclusionMax) {
    if (!anyMouseButtonClicked()) {
        return false;
    }

    ImGuiContext* context = ImGui::GetCurrentContext();
    if (context == nullptr || context->OpenPopupStack.empty()) {
        return false;
    }

    const ImVec2 mouse = ImGui::GetMousePos();
    if (screenPointInside(mouse, exclusionMin, exclusionMax) || imguiOpenPopupContainsScreenPoint(mouse)) {
        return false;
    }

    ImGui::ClosePopupsExceptModals();
    return true;
}

} // namespace quader

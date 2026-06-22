////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/ViewportFocusController.h"

#include "ui/UIContext.h"

#include <cmath>

#include <imgui.h>

namespace quader {

bool ViewportFocusController::inputActive() const {
    const ImGuiIO& io = ImGui::GetIO();
    return ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
            ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
            ImGui::IsMouseClicked(ImGuiMouseButton_Middle) ||
            io.MouseWheel != 0.0f ||
            (ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                    (std::abs(io.MouseDelta.x) > 0.0f || std::abs(io.MouseDelta.y) > 0.0f));
}

ViewportFocusState ViewportFocusController::resolve(bool focusHovered, const UIContext& uiContext, UIPanelId panelId) const {
    ViewportFocusState state;
    state.requestedThisFrame = focusHovered && inputActive();
    if (state.requestedThisFrame) {
        uiContext.requestPanelFocus(panelId);
        ImGui::SetWindowFocus();
    }

    const bool appFocused =
            uiContext.focusedPanel == panelId ||
            (uiContext.focusedPanelStorage != nullptr && *uiContext.focusedPanelStorage == panelId);
    state.focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || appFocused || state.requestedThisFrame;
    return state;
}

} // namespace quader

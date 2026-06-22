////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <imgui.h>

#include <algorithm>
#include <limits>

namespace quader {

inline float uiFillWidthAtLeast(float minimumWidth) {
    return std::max(ImGui::GetContentRegionAvail().x, minimumWidth);
}

inline void setNextItemFillWidthAtLeast(float minimumWidth) {
    ImGui::SetNextItemWidth(uiFillWidthAtLeast(minimumWidth));
}

inline void setNextWindowMinWidth(float minimumWidth) {
    ImGui::SetNextWindowSizeConstraints(
            {minimumWidth, 0.0f},
            {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()});
}

inline bool panelFocusInputActive() {
    const ImGuiIO& io = ImGui::GetIO();
    return ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
            ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
            ImGui::IsMouseClicked(ImGuiMouseButton_Middle) ||
            io.MouseWheel != 0.0f;
}

} // namespace quader

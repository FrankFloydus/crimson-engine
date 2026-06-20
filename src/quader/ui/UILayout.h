#pragma once

#include <imgui.h>

#include <algorithm>

namespace quader {

inline float uiFillWidthAtLeast(float minimumWidth) {
    return std::max(ImGui::GetContentRegionAvail().x, minimumWidth);
}

inline void setNextItemFillWidthAtLeast(float minimumWidth) {
    ImGui::SetNextItemWidth(uiFillWidthAtLeast(minimumWidth));
}

} // namespace quader

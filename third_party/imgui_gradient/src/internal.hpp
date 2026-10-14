#pragma once
#include <imgui.h>

namespace ImGG { namespace internal {

inline auto line_height() -> float
{
    return ImGui::GetFrameHeight();
}

inline auto button_size() -> ImVec2
{
    return ImVec2{line_height(), line_height()};
}

inline auto gradient_position(float x_offset) -> ImVec2
{
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    return ImVec2{cursor.x + x_offset, cursor.y};
}

inline auto border_color() -> ImU32
{
    return ImGui::GetColorU32(ImGuiCol_Border);
}

}} // namespace ImGG::internal

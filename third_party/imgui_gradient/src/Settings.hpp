#pragma once
#include <imgui.h>
#include <functional>
#include "Flags.hpp"
#include "internal.hpp"
#include "MarkId.hpp"
#include "ColorRGBA.hpp"
#include <limits>

namespace ImGG {
class Gradient;

inline auto fully_opaque(ImU32 col) -> ImU32
{
    return col | 0xFF000000;
}

struct Settings {
    float gradient_width{500.f};
    float gradient_height{40.f}; // Must be strictly positive
    float horizontal_margin{10.f};

    /// Subdivide display preview rectangles to approximate the display mapping
    /// of Crimson's linear HDR RGB interpolation with independent straight alpha.
    /// This helps reduce the visual difference in the preview inside ImGui.
    /// Increasing the value of `gradient_subdivisions` will increase fidelity but decrease performance.
    /// Set it to 0 do disable the subdivisions and use a single ImGui's AddRectFilledMultiColor.
    int gradient_subdivisions{3};

    /// Distance under the gradient bar to delete a mark by dragging it down.
    /// This behaviour can also be disabled with the Flag::NoDragDowntoDelete.
    float distance_to_delete_mark_by_dragging_down{80.f};

    ImGG::Flags flags{ImGG::Flag::None};

    ImGuiColorEditFlags color_edit_flags{ImGuiColorEditFlags_None};

    /// Controls how the new mark color is chosen.
    /// If true, the new mark color will be a random color,
    /// otherwise it will be the one that the gradient already has at the new mark position.
    bool should_use_a_random_color_for_the_new_marks{false};

    // Crimson integration hooks. Default values retain unrestricted upstream
    // editing; hosts can constrain their authored format without replacing UI.
    size_t minimum_mark_count{0};
    size_t maximum_mark_count{std::numeric_limits<size_t>::max()};
    std::function<float(MarkId, float)> constrain_mark_position;
    std::function<void(Gradient&)> reset_gradient;
    // Optional host property widgets; absent callbacks retain the library UI.
    std::function<bool(MarkId,float&,float width)> position_widget;
    std::function<bool(const Gradient&)> reset_button_widget;
    // Authored colors and interpolation remain canonical linear HDR. These
    // hooks affect only display swatches and the selected mark's popup editor.
    std::function<ColorRGBA(const ColorRGBA&)> display_color;
    std::function<bool(ColorRGBA&,bool& interaction_active)> color_picker_widget;
    // Optional transformed-canvas integration. Popup origins are converted at
    // opening; only their contents are submitted in the host's normal space.
    std::function<ImVec2(ImVec2)> popup_position;
    std::function<void()> begin_popup_context,end_popup_context;

    std::function<bool()> plus_button_widget = []() {
        return ImGui::Button("+", internal::button_size());
    };
    std::function<bool()> minus_button_widget = []() {
        return ImGui::Button("-", internal::button_size());
    };

    ImU32 mark_color                  = fully_opaque(ImGui::GetColorU32(ImGuiCol_Button));
    ImU32 mark_hovered_color          = fully_opaque(ImGui::GetColorU32(ImGuiCol_ButtonHovered));
    ImU32 mark_selected_color         = fully_opaque(ImGui::GetColorU32(ImGuiCol_ButtonActive));
    ImU32 mark_selected_hovered_color = fully_opaque(ImGui::GetColorU32(ImGuiCol_ButtonActive));
};

} // namespace ImGG

module;

#include <imgui.h>

#include <optional>
#include <span>
#include <string_view>

module quader.ui.outliner_panel;

import quader.ui.controls;

namespace quader {

bool OutlinerPanel::is_open() const noexcept
{
    return open_;
}

void OutlinerPanel::set_open(bool open) noexcept
{
    open_ = open;
}

OutlinerPanelResult OutlinerPanel::draw(const SceneOutlinerPanelInput& input)
{
    OutlinerPanelResult result;
    if (!open_) {
        return result;
    }

    const WindowScope window("Outliner", &open_);
    if (!window) {
        return result;
    }

    if (input.rows.empty()) {
        ImGui::TextDisabled("No entities.");
        return result;
    }

    for (const SceneOutlinerRow& row : input.rows) {
        const IdScope entity_id(static_cast<int>(row.id.value));
        const bool selected = input.selected_entity.has_value() && *input.selected_entity == row.id;
        const std::string_view label = row.display_name;
        if (ImGui::Selectable(label.data(), selected)) {
            result.selected_entity = row.id;
        }
    }

    return result;
}

} // namespace quader

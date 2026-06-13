module;

#include <imgui.h>

#include <optional>
#include <span>
#include <string_view>

module quader.ui.outliner_panel;

import quader.ui.controls;

namespace quader {
namespace {

[[nodiscard]] const char* entity_fallback_name(const SceneEntity& entity) noexcept
{
    if (entity.mesh.has_value()) {
        return entity.mesh->mesh_kind == SceneMeshKind::plane ? "Plane" : "Cube";
    }

    if (entity.directional_light.has_value()) {
        return "Directional Light";
    }

    if (entity.point_light.has_value()) {
        return "Point Light";
    }

    return "Entity";
}

[[nodiscard]] std::string_view display_name_for(const SceneEntity& entity) noexcept
{
    if (!entity.name.empty()) {
        return entity.name;
    }

    return entity_fallback_name(entity);
}

} // namespace

bool OutlinerPanel::is_open() const noexcept
{
    return open_;
}

void OutlinerPanel::set_open(bool open) noexcept
{
    open_ = open;
}

OutlinerPanelResult OutlinerPanel::draw(
    std::span<const SceneEntity> entities,
    std::optional<SceneEntityId> selected_entity)
{
    OutlinerPanelResult result;
    if (!open_) {
        return result;
    }

    const WindowScope window("Outliner", &open_);
    if (!window) {
        return result;
    }

    if (entities.empty()) {
        ImGui::TextDisabled("No entities.");
        return result;
    }

    for (const SceneEntity& entity : entities) {
        const IdScope entity_id(static_cast<int>(entity.id.value));
        const bool selected = selected_entity.has_value() && *selected_entity == entity.id;
        const std::string_view label = display_name_for(entity);
        if (ImGui::Selectable(label.data(), selected)) {
            result.selected_entity = entity.id;
        }
    }

    return result;
}

} // namespace quader

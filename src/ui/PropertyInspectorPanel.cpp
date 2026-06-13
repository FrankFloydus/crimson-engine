module;

#include <imgui.h>

#include <optional>
#include <string_view>

module quader.ui.property_inspector_panel;

import quader.ui.component_editors;
import quader.ui.controls;
import quader.ui.style;

namespace quader {

PropertyInspectorPanelResult draw_property_inspector_panel(const PropertyInspectorPanelInput& input)
{
    set_initial_window_bounds(inspector_ui_style().window);

    PropertyInspectorPanelResult result;
    const WindowScope window("Property Inspector");
    if (!window) {
        return result;
    }

    if (!input.entity.has_value()) {
        ImGui::TextDisabled("No entity selected.");
        return result;
    }

    const PropertyInspectorPanelEntity& entity = *input.entity;
    result.inspected_entity = entity.id;

    ImGui::TextUnformatted(entity.name.data(), entity.name.data() + entity.name.size());
    ImGui::Separator();

    if (entity.transform.has_value()) {
        TransformComponent edited_transform = *entity.transform;
        const TransformComponentEditorResult transform_result = draw_transform_component(edited_transform);
        if (transform_result.transform_changed) {
            result.changed_transform = edited_transform;
        }
    }

    if (entity.material.has_value()) {
        PbrMaterialSettings edited_material = *entity.material;
        const MaterialComponentEditorResult material_result =
            draw_material_component(edited_material, input.material_texture_picker_busy, input.renderer);
        if (material_result.settings_changed) {
            result.changed_material = edited_material;
        }
        result.requested_texture_pick = material_result.requested_texture_pick;
        result.requested_texture_clear = material_result.requested_texture_clear;
    }

    if (entity.directional_light.has_value()) {
        DirectionalLightSettings edited_light = *entity.directional_light;
        const DirectionalLightComponentEditorResult light_result =
            draw_directional_light_component(edited_light, input.renderer);
        if (light_result.settings_changed) {
            result.changed_directional_light = edited_light;
        }
    }

    if (entity.point_light.has_value()) {
        PointLightComponent edited_light = *entity.point_light;
        const PointLightComponentEditorResult light_result = draw_point_light_component(edited_light, input.renderer);
        if (light_result.settings_changed) {
            result.changed_point_light = edited_light;
        }
    }

    return result;
}

} // namespace quader

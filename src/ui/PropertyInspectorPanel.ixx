module;

#include <optional>
#include <string>
#include <string_view>

export module quader.ui.property_inspector_panel;

export import quader.scene.scene;

export namespace quader {

struct PropertyInspectorPanelEntity {
    SceneEntityId id;
    std::string name;
    const TransformComponent* transform = nullptr;
    const MaterialComponent* material = nullptr;
    const DirectionalLightComponent* directional_light = nullptr;
    const PointLightComponent* point_light = nullptr;
};

struct PropertyInspectorPanelInput {
    std::optional<PropertyInspectorPanelEntity> entity;
    bool material_texture_picker_busy = false;
};

struct PropertyInspectorPanelResult {
    std::optional<SceneEntityId> inspected_entity;
    std::optional<TransformComponent> changed_transform;
    std::optional<PbrMaterialSettings> changed_material;
    std::optional<PbrMaterialTextureSlot> requested_texture_pick;
    std::optional<PbrMaterialTextureSlot> requested_texture_clear;
    std::optional<DirectionalLightSettings> changed_directional_light;
    std::optional<PointLightComponent> changed_point_light;
};

[[nodiscard]] PropertyInspectorPanelResult draw_property_inspector_panel(
    const PropertyInspectorPanelInput& input);

} // namespace quader

module;

#include <optional>

export module quader.ui.component_editors;

export import quader.render.types;
export import quader.scene.scene;

export namespace quader {

struct MaterialComponentEditorResult {
    bool settings_changed = false;
    std::optional<PbrMaterialTextureSlot> requested_texture_pick;
    std::optional<PbrMaterialTextureSlot> requested_texture_clear;
};

struct TransformComponentEditorResult {
    bool transform_changed = false;
};

struct DirectionalLightComponentEditorResult {
    bool settings_changed = false;
};

struct PointLightComponentEditorResult {
    bool settings_changed = false;
};

[[nodiscard]] TransformComponentEditorResult draw_transform_component(SceneTransform& transform);

[[nodiscard]] MaterialComponentEditorResult draw_material_component(
    PbrMaterialSettings& settings,
    bool picker_busy);

[[nodiscard]] DirectionalLightComponentEditorResult draw_directional_light_component(
    DirectionalLightSettings& settings);

[[nodiscard]] PointLightComponentEditorResult draw_point_light_component(PointLightComponent& light);

} // namespace quader

module;

#include <optional>
#include <span>
#include <string>

export module quader.ui_shell.editor_panel_contracts;

export import quader.render.driver_startup_plan;
export import quader.scene.scene;

export namespace quader {

enum class CreateSceneItemKind {
    cube,
    plane,
    directional_light,
    point_light,
};

struct EditorMainMenuInput {
    RenderDriverKind active_driver = RenderDriverKind::vulkan;
    RenderDriverSelection saved_selection;
    bool restart_required = false;
};

struct EditorMainMenuResult {
    std::optional<CreateSceneItemKind> requested_create;
    std::optional<RenderDriverKind> requested_render_driver;
};

struct SceneOutlinerRow {
    SceneEntityId id;
    std::string display_name;
};

struct SceneOutlinerPanelInput {
    std::span<const SceneOutlinerRow> rows;
    std::optional<SceneEntityId> selected_entity;
};

struct OutlinerPanelResult {
    std::optional<SceneEntityId> selected_entity;
};

struct PropertyInspectorPanelEntity {
    SceneEntityId id;
    std::string name;
    std::optional<TransformComponent> transform;
    std::optional<PbrMaterialSettings> material;
    std::optional<DirectionalLightSettings> directional_light;
    std::optional<PointLightComponent> point_light;
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

struct ScenePropertiesPanelFrameInput {
    SceneOutlinerPanelInput outliner;
    PropertyInspectorPanelInput property_inspector;
};

struct ScenePropertiesPanelFrameResult {
    OutlinerPanelResult outliner;
    PropertyInspectorPanelResult property_inspector;
};

} // namespace quader

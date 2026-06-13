module;

#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "diagnostics/LogMacros.h"

module quader.app.scene_properties_panel_controller;

import quader.diagnostics.logger;

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

bool ScenePropertiesPanelController::begin_frame(
    Scene& scene,
    IImageFileDialogService& image_file_dialog,
    EditorSelectionModel& selection)
{
    bool render_state_changed = selection.validate_against(scene);
    render_state_changed |= consume_texture_picker_result_if_available(scene, image_file_dialog);
    return render_state_changed;
}

ScenePropertiesPanelFrameInput ScenePropertiesPanelController::build_frame_input(
    const Scene& scene,
    const IImageFileDialogService& image_file_dialog,
    const EditorSelectionModel& selection,
    const RendererCapabilities& renderer)
{
    rebuild_outliner_rows(scene);

    return ScenePropertiesPanelFrameInput {
        .outliner = SceneOutlinerPanelInput {
            .rows = outliner_rows_,
            .selected_entity = selection.selected_entity(),
        },
        .property_inspector = make_property_inspector_input(scene, image_file_dialog, selection, renderer),
    };
}

PropertyInspectorPanelInput ScenePropertiesPanelController::make_property_inspector_input(
    const Scene& scene,
    const IImageFileDialogService& image_file_dialog,
    const EditorSelectionModel& selection,
    const RendererCapabilities& renderer) const
{
    PropertyInspectorPanelInput input {
        .renderer = renderer,
        .material_texture_picker_busy = image_file_dialog.has_pending_request(),
    };

    const std::optional<SceneEntityId> selected_entity = selection.selected_entity();
    if (!selected_entity.has_value()) {
        return input;
    }

    const std::optional<SceneEntity> entity = scene.entity(*selected_entity);
    if (!entity.has_value()) {
        return input;
    }

    input.entity = PropertyInspectorPanelEntity {
        .id = entity->id,
        .name = entity->name,
    };

    if (const TransformComponent* transform = scene.transform(entity->id); transform != nullptr) {
        input.entity->transform = *transform;
    }

    if (const MaterialComponent* material = scene.material(entity->id); material != nullptr) {
        input.entity->material = material->material;
    }

    if (const DirectionalLightComponent* light = scene.directional_light(entity->id); light != nullptr) {
        input.entity->directional_light = light->settings;
    }

    if (const PointLightComponent* light = scene.point_light(entity->id); light != nullptr) {
        input.entity->point_light = *light;
    }

    return input;
}

bool ScenePropertiesPanelController::apply_frame_result(
    const ScenePropertiesPanelFrameResult& result,
    Scene& scene,
    IImageFileDialogService& image_file_dialog,
    EditorSelectionModel& selection)
{
    bool render_state_changed = apply_outliner_result(result.outliner, selection);
    render_state_changed |= apply_property_inspector_result(result.property_inspector, scene, image_file_dialog);
    return render_state_changed;
}

bool ScenePropertiesPanelController::apply_create_request(
    CreateSceneItemKind create_kind,
    Scene& scene,
    EditorSelectionModel& selection)
{
    switch (create_kind) {
    case CreateSceneItemKind::cube: {
        const SceneEntityId created_id = scene.create_cube();
        selection.select_entity(created_id);
        return true;
    }
    case CreateSceneItemKind::plane: {
        const SceneEntityId created_id = scene.create_plane();
        selection.select_entity(created_id);
        return true;
    }
    case CreateSceneItemKind::directional_light: {
        const SceneEntityId created_id = scene.create_directional_light();
        selection.select_entity(created_id);
        return true;
    }
    case CreateSceneItemKind::point_light: {
        const SceneEntityId created_id = scene.create_point_light();
        selection.select_entity(created_id);
        return true;
    }
    }

    return false;
}

void ScenePropertiesPanelController::rebuild_outliner_rows(const Scene& scene)
{
    const std::span<const SceneEntity> entities = scene.entities();
    outliner_rows_.clear();
    outliner_rows_.reserve(entities.size());

    for (const SceneEntity& entity : entities) {
        outliner_rows_.push_back(SceneOutlinerRow {
            .id = entity.id,
            .display_name = std::string(display_name_for(entity)),
        });
    }
}

bool ScenePropertiesPanelController::apply_outliner_result(
    const OutlinerPanelResult& result,
    EditorSelectionModel& selection)
{
    if (result.selected_entity.has_value()) {
        return selection.select_entity(*result.selected_entity);
    }

    return false;
}

bool ScenePropertiesPanelController::apply_property_inspector_result(
    const PropertyInspectorPanelResult& result,
    Scene& scene,
    IImageFileDialogService& image_file_dialog)
{
    if (!result.inspected_entity.has_value()) {
        return false;
    }

    const SceneEntityId entity_id = *result.inspected_entity;
    bool render_state_changed = false;

    if (result.changed_transform.has_value() && scene.set_transform(entity_id, *result.changed_transform)) {
        render_state_changed = true;
    }

    if (result.changed_material.has_value() && scene.set_material(entity_id, *result.changed_material)) {
        render_state_changed = true;
    }

    if (result.requested_texture_clear.has_value() &&
        scene.clear_material_texture(entity_id, *result.requested_texture_clear)) {
        render_state_changed = true;
    }

    if (result.changed_directional_light.has_value() &&
        scene.set_directional_light(entity_id, *result.changed_directional_light)) {
        render_state_changed = true;
    }

    if (result.changed_point_light.has_value() && scene.set_point_light(entity_id, *result.changed_point_light)) {
        render_state_changed = true;
    }

    if (result.requested_texture_pick.has_value() && !image_file_dialog.has_pending_request()) {
        pending_material_texture_pick_ = PendingMaterialTexturePick {
            .entity_id = entity_id,
            .slot = *result.requested_texture_pick,
        };
        image_file_dialog.request_image_file();
    }

    return render_state_changed;
}

bool ScenePropertiesPanelController::consume_texture_picker_result_if_available(
    Scene& scene,
    IImageFileDialogService& image_file_dialog)
{
    std::optional<ImageFileDialogResult> result = image_file_dialog.try_consume_result();
    if (!result.has_value()) {
        return false;
    }

    const std::optional<PendingMaterialTexturePick> pending_pick = pending_material_texture_pick_;
    pending_material_texture_pick_.reset();

    switch (result->status) {
    case ImageFileDialogStatus::selected: {
        if (!pending_pick.has_value()) {
            QUADER_LOG_WARN("Image texture picker returned a path without a pending material slot.");
            return false;
        }

        const MaterialComponent* material = static_cast<const Scene&>(scene).material(pending_pick->entity_id);
        if (material == nullptr) {
            QUADER_LOG_WARN("Image texture picker returned a path, but the selected entity no longer has a material.");
            return false;
        }

        return scene.set_material_texture_source(pending_pick->entity_id, pending_pick->slot, result->path);
    }
    case ImageFileDialogStatus::failed:
        if (result->message.empty()) {
            QUADER_LOG_ERROR("Native image file dialog failed.");
        } else {
            QUADER_LOG_ERROR("Native image file dialog failed: {}", result->message);
        }
        return false;
    case ImageFileDialogStatus::canceled:
        return false;
    }

    return false;
}

} // namespace quader

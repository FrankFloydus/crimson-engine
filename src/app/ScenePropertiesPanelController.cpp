module;

#include <optional>

#include "diagnostics/LogMacros.h"

module quader.app.scene_properties_panel_controller;

import quader.diagnostics.logger;
import quader.ui.inspector_menu;
import quader.ui.property_inspector_panel;

namespace quader {
namespace {

[[nodiscard]] PropertyInspectorPanelInput make_property_inspector_input(
    const Scene& scene,
    const NativeFileDialogService& native_file_dialog,
    const EditorSelectionModel& selection)
{
    PropertyInspectorPanelInput input {
        .material_texture_picker_busy = native_file_dialog.has_pending_request(),
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
        .transform = scene.transform(entity->id),
        .material = scene.material(entity->id),
        .directional_light = scene.directional_light(entity->id),
        .point_light = scene.point_light(entity->id),
    };

    return input;
}

} // namespace

bool ScenePropertiesPanelController::render_frame(
    Scene& scene,
    NativeFileDialogService& native_file_dialog,
    EditorSelectionModel& selection)
{
    bool render_state_changed = selection.validate_against(scene);
    render_state_changed |= consume_texture_picker_result_if_available(scene, native_file_dialog);
    render_state_changed |= render_create_menu(scene, selection);
    render_state_changed |= render_outliner(scene, selection);
    render_state_changed |= render_property_inspector(scene, native_file_dialog, selection);
    return render_state_changed;
}

bool ScenePropertiesPanelController::render_create_menu(Scene& scene, EditorSelectionModel& selection)
{
    const InspectorCreateMenuResult result = draw_inspector_create_menu();
    if (!result.requested_create.has_value()) {
        return false;
    }

    switch (*result.requested_create) {
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

bool ScenePropertiesPanelController::render_outliner(const Scene& scene, EditorSelectionModel& selection)
{
    const OutlinerPanelResult result = outliner_panel_.draw(scene.entities(), selection.selected_entity());
    if (result.selected_entity.has_value()) {
        return selection.select_entity(*result.selected_entity);
    }

    return false;
}

bool ScenePropertiesPanelController::render_property_inspector(
    Scene& scene,
    NativeFileDialogService& native_file_dialog,
    EditorSelectionModel& selection)
{
    const PropertyInspectorPanelInput input = make_property_inspector_input(scene, native_file_dialog, selection);
    const PropertyInspectorPanelResult result = draw_property_inspector_panel(input);
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

    if (result.requested_texture_pick.has_value() && !native_file_dialog.has_pending_request()) {
        pending_material_texture_pick_ = PendingMaterialTexturePick {
            .entity_id = entity_id,
            .slot = *result.requested_texture_pick,
        };
        native_file_dialog.request_image_file();
    }

    return render_state_changed;
}

bool ScenePropertiesPanelController::consume_texture_picker_result_if_available(
    Scene& scene,
    NativeFileDialogService& native_file_dialog)
{
    std::optional<NativeFileDialogResult> result = native_file_dialog.try_consume_result();
    if (!result.has_value()) {
        return false;
    }

    const std::optional<PendingMaterialTexturePick> pending_pick = pending_material_texture_pick_;
    pending_material_texture_pick_.reset();

    switch (result->status) {
    case NativeFileDialogStatus::selected: {
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
    case NativeFileDialogStatus::failed:
        if (result->message.empty()) {
            QUADER_LOG_ERROR("Native image file dialog failed.");
        } else {
            QUADER_LOG_ERROR("Native image file dialog failed: {}", result->message);
        }
        return false;
    case NativeFileDialogStatus::canceled:
        return false;
    }

    return false;
}

} // namespace quader

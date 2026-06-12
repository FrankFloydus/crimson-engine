module;

#include <imgui.h>

#include <optional>
#include <span>

#include "diagnostics/LogMacros.h"

module quader.app.scene_properties_panel_controller;

import quader.diagnostics.logger;
import quader.ui.component_editors;
import quader.ui.inspector_menu;
import quader.ui.style;

namespace quader {

void ScenePropertiesPanelController::select_entity(SceneEntityId entity_id)
{
    selected_entity_ = entity_id.is_valid() ? std::optional<SceneEntityId>(entity_id) : std::nullopt;
}

bool ScenePropertiesPanelController::render_frame(
    Scene& scene,
    NativeFileDialogService& native_file_dialog)
{
    if (selected_entity_.has_value() && !scene.contains(*selected_entity_)) {
        selected_entity_.reset();
    }

    bool render_state_changed = false;
    render_state_changed |= consume_texture_picker_result_if_available(scene, native_file_dialog);
    render_state_changed |= render_create_menu(scene);
    render_outliner(scene);
    render_state_changed |= render_property_inspector(scene, native_file_dialog);
    return render_state_changed;
}

bool ScenePropertiesPanelController::render_create_menu(Scene& scene)
{
    const InspectorCreateMenuResult result = draw_inspector_create_menu();
    if (!result.requested_create.has_value()) {
        return false;
    }

    switch (*result.requested_create) {
    case CreateSceneItemKind::cube: {
        const SceneEntityId created_id = scene.create_cube();
        selected_entity_ = created_id;
        return true;
    }
    case CreateSceneItemKind::plane: {
        const SceneEntityId created_id = scene.create_plane();
        selected_entity_ = created_id;
        return true;
    }
    case CreateSceneItemKind::directional_light: {
        const SceneEntityId created_id = scene.create_directional_light();
        selected_entity_ = created_id;
        return true;
    }
    case CreateSceneItemKind::point_light: {
        const SceneEntityId created_id = scene.create_point_light();
        selected_entity_ = created_id;
        return true;
    }
    }

    return false;
}

void ScenePropertiesPanelController::render_outliner(const Scene& scene)
{
    const OutlinerPanelResult result = outliner_panel_.draw(scene.entities(), selected_entity_);
    if (result.selected_entity.has_value()) {
        selected_entity_ = *result.selected_entity;
    }
}

bool ScenePropertiesPanelController::render_property_inspector(
    Scene& scene,
    NativeFileDialogService& native_file_dialog)
{
    const InspectorUiStyle style = inspector_ui_style();
    set_initial_window_bounds(style.window);

    bool render_state_changed = false;
    if (!ImGui::Begin("Property Inspector")) {
        ImGui::End();
        return false;
    }

    if (!selected_entity_.has_value()) {
        ImGui::TextDisabled("No entity selected.");
        ImGui::End();
        return false;
    }

    const std::optional<SceneEntity> entity = scene.entity(*selected_entity_);
    if (!entity.has_value()) {
        ImGui::TextDisabled("No entity selected.");
        ImGui::End();
        return false;
    }

    ImGui::TextUnformatted(entity->name.data(), entity->name.data() + entity->name.size());
    ImGui::Separator();

    if (TransformComponent* transform = scene.transform(entity->id)) {
        const TransformComponentEditorResult transform_result = draw_transform_component(*transform);
        render_state_changed |= transform_result.transform_changed;
    }

    if (MaterialComponent* material = scene.material(entity->id)) {
        const MaterialComponentEditorResult material_result =
            draw_material_component(material->material, native_file_dialog.has_pending_request());

        render_state_changed |= material_result.settings_changed;

        if (material_result.requested_texture_clear.has_value()) {
            material_texture_reference(material->material, *material_result.requested_texture_clear).clear();
            render_state_changed = true;
        }

        if (material_result.requested_texture_pick.has_value() && !native_file_dialog.has_pending_request()) {
            pending_material_texture_pick_ = PendingMaterialTexturePick {
                .entity_id = entity->id,
                .slot = *material_result.requested_texture_pick,
            };
            native_file_dialog.request_image_file();
        }
    }

    if (SceneDirectionalLightComponent* light = scene.directional_light(entity->id)) {
        const DirectionalLightComponentEditorResult light_result =
            draw_directional_light_component(light->settings);
        render_state_changed |= light_result.settings_changed;
    }

    if (PointLightComponent* light = scene.point_light(entity->id)) {
        const PointLightComponentEditorResult light_result = draw_point_light_component(*light);
        render_state_changed |= light_result.settings_changed;
    }

    ImGui::End();
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

        MaterialComponent* material = scene.material(pending_pick->entity_id);
        if (material == nullptr) {
            QUADER_LOG_WARN("Image texture picker returned a path, but the selected entity no longer has a material.");
            return false;
        }

        material_texture_reference(material->material, pending_pick->slot).source_path = result->path;
        return true;
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

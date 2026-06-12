module;

#include <optional>

#include "diagnostics/LogMacros.h"

module quader.app.scene_properties_panel_controller;

import quader.diagnostics.logger;
import quader.ui.light_panel;
import quader.ui.material_panel;

namespace quader {

bool ScenePropertiesPanelController::render_frame(
    Scene& scene,
    NativeFileDialogService& native_file_dialog)
{
    bool render_state_changed = false;
    render_state_changed |= consume_texture_picker_result_if_available(scene, native_file_dialog);
    render_state_changed |= render_material_panel(scene, native_file_dialog);
    render_state_changed |= render_light_panel(scene);
    return render_state_changed;
}

bool ScenePropertiesPanelController::render_material_panel(
    Scene& scene,
    NativeFileDialogService& native_file_dialog)
{
    SceneMeshComponent* mesh = first_mesh_component(scene);
    if (mesh == nullptr) {
        return false;
    }

    bool render_state_changed = false;
    const MaterialPanelResult result =
        render_material_settings_panel(mesh->material, native_file_dialog.has_pending_request());

    if (result.settings_changed) {
        render_state_changed = true;
    }

    if (result.requested_texture_clear.has_value()) {
        material_texture_reference(mesh->material, *result.requested_texture_clear).clear();
        render_state_changed = true;
    }

    if (result.requested_texture_pick.has_value() && !native_file_dialog.has_pending_request()) {
        pending_material_texture_slot_ = *result.requested_texture_pick;
        native_file_dialog.request_image_file();
    }

    return render_state_changed;
}

bool ScenePropertiesPanelController::render_light_panel(Scene& scene)
{
    if (SceneDirectionalLightComponent* light = first_directional_light_component(scene)) {
        return render_directional_light_panel(light->settings);
    }

    return false;
}

bool ScenePropertiesPanelController::consume_texture_picker_result_if_available(
    Scene& scene,
    NativeFileDialogService& native_file_dialog)
{
    std::optional<NativeFileDialogResult> result = native_file_dialog.try_consume_result();
    if (!result.has_value()) {
        return false;
    }

    const std::optional<PbrMaterialTextureSlot> pending_slot = pending_material_texture_slot_;
    pending_material_texture_slot_.reset();

    switch (result->status) {
    case NativeFileDialogStatus::selected: {
        if (!pending_slot.has_value()) {
            QUADER_LOG_WARN("Image texture picker returned a path without a pending material slot.");
            return false;
        }

        SceneMeshComponent* mesh = first_mesh_component(scene);
        if (mesh == nullptr) {
            QUADER_LOG_WARN("Image texture picker returned a path, but the scene has no mesh material to update.");
            return false;
        }

        material_texture_reference(mesh->material, *pending_slot).source_path = result->path;
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

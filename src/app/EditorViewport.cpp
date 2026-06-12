module;

#include <imgui.h>

#include "diagnostics/LogMacros.h"

module quader.app.editor_viewport;

import quader.diagnostics.logger;
import quader.ui.material_panel;
import quader.ui.viewport_panels;

namespace quader {
namespace {

bool is_viewport_movement_key_down(const Input& input)
{
    return input.is_key_down(AppKey::w) ||
        input.is_key_down(AppKey::a) ||
        input.is_key_down(AppKey::s) ||
        input.is_key_down(AppKey::d) ||
        input.is_key_down(AppKey::q) ||
        input.is_key_down(AppKey::e);
}

bool render_extent_changed(RenderExtent left, RenderExtent right)
{
    return left.width != right.width || left.height != right.height;
}

EditorCameraNavigationInput editor_camera_navigation_input_from(
    const Input& input,
    bool keyboard_navigation_active,
    bool mouse_navigation_active)
{
    if (!keyboard_navigation_active && !mouse_navigation_active) {
        return {};
    }

    EditorCameraNavigationInput navigation_input;
    if (keyboard_navigation_active || mouse_navigation_active) {
        navigation_input.move_forward = input.is_key_down(AppKey::w);
        navigation_input.move_backward = input.is_key_down(AppKey::s);
        navigation_input.move_left = input.is_key_down(AppKey::a);
        navigation_input.move_right = input.is_key_down(AppKey::d);
        navigation_input.move_up = input.is_key_down(AppKey::e);
        navigation_input.move_down = input.is_key_down(AppKey::q);
        navigation_input.boost =
            input.is_key_down(AppKey::left_shift) || input.is_key_down(AppKey::right_shift);
    }

    navigation_input.look_active = mouse_navigation_active;
    navigation_input.look_delta = mouse_navigation_active ? input.mouse_delta() : MouseDelta {};
    return navigation_input;
}

bool imgui_text_input_is_active()
{
    return ImGui::GetIO().WantTextInput;
}

} // namespace

void EditorViewport::render_frame(
    ISceneViewportRenderer& renderer,
    ImGuiLayer& imgui,
    Scene& scene,
    NativeFileDialogService& native_file_dialog,
    const Input& input,
    float delta_seconds)
{
    consume_texture_picker_result_if_available(scene, native_file_dialog);

    render_material_panel(scene, native_file_dialog);

    if (SceneDirectionalLightComponent* light = first_directional_light_component(scene)) {
        if (render_directional_light_panel(light->settings)) {
            viewport_dirty_ = true;
        }
    }

    request_viewport_frame_if_needed(renderer, scene);
    upload_completed_viewport_frame_if_available(renderer, imgui);

    const ViewportPanelResult viewport_panel = render_scene_viewport_panel(
        imgui.has_viewport_texture() ? imgui.viewport_texture_id() : ImTextureID {},
        displayed_extent_.is_valid() ? displayed_extent_ : viewport_extent_);

    if (viewport_panel.requested_extent.is_valid() &&
        render_extent_changed(viewport_extent_, viewport_panel.requested_extent)) {
        viewport_extent_ = viewport_panel.requested_extent;
        viewport_dirty_ = true;
    }

    if (!input.window_has_keyboard_focus() || !input.window_has_mouse_focus()) {
        viewport_mouse_navigation_active_ = false;
    }

    if (input.was_mouse_button_pressed(AppMouseButton::right) && viewport_panel.image_hovered) {
        viewport_mouse_navigation_active_ = true;
    }
    if (input.was_mouse_button_released(AppMouseButton::right) ||
        !input.is_mouse_button_down(AppMouseButton::right)) {
        viewport_mouse_navigation_active_ = false;
    }

    viewport_keyboard_focus_ =
        viewport_panel.image_hovered &&
        !imgui_text_input_is_active() &&
        is_viewport_movement_key_down(input);

    update_editor_camera(input, delta_seconds);
}

bool EditorViewport::wants_relative_mouse_mode() const noexcept
{
    return viewport_mouse_navigation_active_;
}

void EditorViewport::update_editor_camera(const Input& input, float delta_seconds)
{
    const EditorCameraNavigationInput navigation_input = editor_camera_navigation_input_from(
        input,
        viewport_keyboard_focus_ || viewport_mouse_navigation_active_,
        viewport_mouse_navigation_active_);

    if (editor_camera_.update_from_navigation_input(navigation_input, delta_seconds)) {
        viewport_dirty_ = true;
    }
}

void EditorViewport::render_material_panel(Scene& scene, NativeFileDialogService& native_file_dialog)
{
    SceneMeshComponent* mesh = first_mesh_component(scene);
    if (mesh == nullptr) {
        return;
    }

    const MaterialPanelResult result =
        render_material_settings_panel(mesh->material, native_file_dialog.has_pending_request());

    if (result.settings_changed) {
        viewport_dirty_ = true;
    }

    if (result.requested_texture_clear.has_value()) {
        material_texture_reference(mesh->material, *result.requested_texture_clear).clear();
        viewport_dirty_ = true;
    }

    if (result.requested_texture_pick.has_value() && !native_file_dialog.has_pending_request()) {
        pending_material_texture_slot_ = *result.requested_texture_pick;
        native_file_dialog.request_image_file();
    }
}

void EditorViewport::consume_texture_picker_result_if_available(
    Scene& scene,
    NativeFileDialogService& native_file_dialog)
{
    std::optional<NativeFileDialogResult> result = native_file_dialog.try_consume_result();
    if (!result.has_value()) {
        return;
    }

    const std::optional<PbrMaterialTextureSlot> pending_slot = pending_material_texture_slot_;
    pending_material_texture_slot_.reset();

    switch (result->status) {
    case NativeFileDialogStatus::selected: {
        if (!pending_slot.has_value()) {
            QUADER_LOG_WARN("Image texture picker returned a path without a pending material slot.");
            return;
        }

        SceneMeshComponent* mesh = first_mesh_component(scene);
        if (mesh == nullptr) {
            QUADER_LOG_WARN("Image texture picker returned a path, but the scene has no mesh material to update.");
            return;
        }

        material_texture_reference(mesh->material, *pending_slot).source_path = result->path;
        viewport_dirty_ = true;
        return;
    }
    case NativeFileDialogStatus::failed:
        if (result->message.empty()) {
            QUADER_LOG_ERROR("Native image file dialog failed.");
        } else {
            QUADER_LOG_ERROR("Native image file dialog failed: {}", result->message);
        }
        return;
    case NativeFileDialogStatus::canceled:
        return;
    }
}

void EditorViewport::request_viewport_frame_if_needed(ISceneViewportRenderer& renderer, const Scene& scene)
{
    if (!viewport_dirty_ || viewport_request_pending_) {
        return;
    }

    viewport_request_pending_ =
        renderer.request_scene_viewport_frame(scene, editor_camera_.render_camera(), viewport_extent_);
    viewport_dirty_ = !viewport_request_pending_;
}

void EditorViewport::upload_completed_viewport_frame_if_available(ISceneViewportRenderer& renderer, ImGuiLayer& imgui)
{
    const RenderViewportFrame* viewport_frame = renderer.try_consume_scene_viewport_frame();
    if (viewport_frame == nullptr) {
        return;
    }

    viewport_request_pending_ = false;
    if (viewport_frame->has_pixels()) {
        imgui.upload_viewport_texture(
            viewport_frame->extent.width,
            viewport_frame->extent.height,
            viewport_frame->rgba.data());
        displayed_extent_ = viewport_frame->extent;
    } else {
        viewport_dirty_ = true;
    }
}

} // namespace quader

module;

#include <imgui.h>

module quader.app.editor_viewport;

import quader.ui.viewport_panels;

namespace quader {

void EditorViewport::render_frame(
    ISceneViewportRenderer& renderer,
    ImGuiLayer& imgui,
    Scene& scene,
    const Input& input,
    float delta_seconds)
{
    update_editor_camera(input, delta_seconds);

    if (SceneMeshComponent* mesh = first_mesh_component(scene)) {
        if (render_material_settings_panel(mesh->material)) {
            viewport_dirty_ = true;
        }
    }

    if (SceneDirectionalLightComponent* light = first_directional_light_component(scene)) {
        if (render_directional_light_panel(light->settings)) {
            viewport_dirty_ = true;
        }
    }

    request_viewport_frame_if_needed(renderer, scene);
    upload_completed_viewport_frame_if_available(renderer, imgui);

    render_scene_viewport_panel(
        scene.title(),
        imgui.has_viewport_texture() ? imgui.viewport_texture_id() : ImTextureID {},
        displayed_extent_.is_valid() ? displayed_extent_ : viewport_extent_);
}

void EditorViewport::update_editor_camera(const Input& input, float delta_seconds)
{
    if (editor_camera_.update_from_input(input, delta_seconds)) {
        viewport_dirty_ = true;
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

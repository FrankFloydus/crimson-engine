module;

#include <imgui.h>

module quader.app.preview_controller;

import quader.ui.preview_panels;

namespace quader {

void PreviewController::render_frame(IDebugPreviewRenderer& renderer, ImGuiLayer& imgui)
{
    load_renderer_settings_once(renderer);

    if (render_material_settings_panel(material_settings_)) {
        renderer.set_material_settings(material_settings_);
        preview_dirty_ = true;
    }

    if (render_directional_light_panel(light_settings_)) {
        renderer.set_directional_light_settings(light_settings_);
        preview_dirty_ = true;
    }

    request_preview_if_needed(renderer);
    upload_completed_preview_if_available(renderer, imgui);

    render_debug_preview_panel(
        imgui.has_preview_texture() ? imgui.preview_texture_id() : ImTextureID {},
        displayed_extent_.is_valid() ? displayed_extent_ : preview_extent_);
}

void PreviewController::load_renderer_settings_once(IDebugPreviewRenderer& renderer)
{
    if (settings_loaded_) {
        return;
    }

    material_settings_ = renderer.material_settings();
    light_settings_ = renderer.directional_light_settings();
    settings_loaded_ = true;
    preview_dirty_ = true;
}

void PreviewController::request_preview_if_needed(IDebugPreviewRenderer& renderer)
{
    if (!preview_dirty_ || preview_request_pending_) {
        return;
    }

    preview_request_pending_ = renderer.request_debug_preview(preview_extent_);
    preview_dirty_ = !preview_request_pending_;
}

void PreviewController::upload_completed_preview_if_available(IDebugPreviewRenderer& renderer, ImGuiLayer& imgui)
{
    const RenderPreviewFrame* preview_frame = renderer.try_consume_debug_preview_frame();
    if (preview_frame == nullptr) {
        return;
    }

    preview_request_pending_ = false;
    if (preview_frame->has_pixels()) {
        imgui.upload_preview_texture(
            preview_frame->extent.width,
            preview_frame->extent.height,
            preview_frame->rgba.data());
        displayed_extent_ = preview_frame->extent;
    } else {
        preview_dirty_ = true;
    }
}

} // namespace quader

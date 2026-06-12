module;

export module quader.app.preview_controller;

import quader.render.debug_preview_renderer;
import quader.ui.imgui_layer;

export namespace quader {

class PreviewController {
public:
    void render_frame(IDebugPreviewRenderer& renderer, ImGuiLayer& imgui);

private:
    void load_renderer_settings_once(IDebugPreviewRenderer& renderer);
    void request_preview_if_needed(IDebugPreviewRenderer& renderer);
    void upload_completed_preview_if_available(IDebugPreviewRenderer& renderer, ImGuiLayer& imgui);

    RenderExtent preview_extent_ { 720, 420 };
    RenderExtent displayed_extent_;
    PbrMaterialSettings material_settings_;
    DirectionalLightSettings light_settings_;
    bool settings_loaded_ = false;
    bool preview_dirty_ = true;
    bool preview_request_pending_ = false;
};

} // namespace quader

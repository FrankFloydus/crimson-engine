module;

#include <filament/Engine.h>

#include <memory>

export module quader.filament.render_engine;

import quader.filament.preview_scene;
export import quader.render.debug_preview_renderer;
export import quader.render.engine;

export namespace quader {

class FilamentRenderEngine final : public IDebugPreviewRenderer {
public:
    FilamentRenderEngine();

    FilamentRenderEngine(const FilamentRenderEngine&) = delete;
    FilamentRenderEngine& operator=(const FilamentRenderEngine&) = delete;

    ~FilamentRenderEngine() override;

    [[nodiscard]] RendererStatus status() const override;
    [[nodiscard]] PbrMaterialSettings material_settings() const override;
    [[nodiscard]] DirectionalLightSettings directional_light_settings() const override;

    void set_material_settings(const PbrMaterialSettings& settings) override;
    void set_directional_light_settings(const DirectionalLightSettings& settings) override;

    [[nodiscard]] bool request_debug_preview(RenderExtent extent) override;
    [[nodiscard]] const RenderPreviewFrame* try_consume_debug_preview_frame() override;

private:
    filament::Engine* engine_ = nullptr;
    std::unique_ptr<FilamentPreviewScene> preview_scene_;
    RendererStatus status_;
    PbrMaterialSettings material_settings_;
    DirectionalLightSettings light_settings_;
    RenderPreviewFrame preview_frame_;
    bool preview_frame_available_ = false;
};

} // namespace quader

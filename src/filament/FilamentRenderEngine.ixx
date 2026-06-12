module;

#include <filament/Engine.h>

#include <memory>

export module quader.filament.render_engine;

import quader.filament.scene_viewport;
export import quader.render.engine;
export import quader.render.scene_viewport_renderer;

export namespace quader {

class FilamentRenderEngine final : public ISceneViewportRenderer {
public:
    FilamentRenderEngine();

    FilamentRenderEngine(const FilamentRenderEngine&) = delete;
    FilamentRenderEngine& operator=(const FilamentRenderEngine&) = delete;

    ~FilamentRenderEngine() override;

    [[nodiscard]] RendererStatus status() const override;

    [[nodiscard]] bool request_scene_viewport_frame(
        const Scene& scene,
        const RenderCamera& camera,
        RenderExtent extent) override;
    [[nodiscard]] const RenderViewportFrame* try_consume_scene_viewport_frame() override;

private:
    filament::Engine* engine_ = nullptr;
    std::unique_ptr<FilamentSceneViewport> scene_viewport_;
    RendererStatus status_;
    RenderViewportFrame viewport_frame_;
    bool viewport_frame_available_ = false;
};

} // namespace quader

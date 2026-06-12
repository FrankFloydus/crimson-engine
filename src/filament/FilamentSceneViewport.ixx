module;

#include <filament/Engine.h>

#include <memory>

export module quader.filament.scene_viewport;

export import quader.render.types;
export import quader.scene.scene;

export namespace quader {

class FilamentSceneViewport {
public:
    explicit FilamentSceneViewport(filament::Engine& engine);

    FilamentSceneViewport(const FilamentSceneViewport&) = delete;
    FilamentSceneViewport& operator=(const FilamentSceneViewport&) = delete;

    ~FilamentSceneViewport();

    void render_into(const Scene& scene, const RenderCamera& camera, RenderExtent extent, RenderViewportFrame& frame);

private:
    struct Resources;
    std::unique_ptr<Resources> resources_;
};

} // namespace quader

module;

#include <filament/Engine.h>

#include <memory>

export module quader.filament.scene_viewport;

export import quader.filament.viewport_vulkan_platform;
export import quader.render.types;
export import quader.render.viewport_types;

export namespace quader {

class FilamentSceneViewport {
public:
    FilamentSceneViewport(filament::Engine& engine, FilamentViewportVulkanPlatform& platform);

    FilamentSceneViewport(const FilamentSceneViewport&) = delete;
    FilamentSceneViewport& operator=(const FilamentSceneViewport&) = delete;

    ~FilamentSceneViewport();

    void render_into(const ViewportRenderRequest& request, ViewportRenderFrame& frame);

private:
    struct Resources;
    std::unique_ptr<Resources> resources_;
};

} // namespace quader

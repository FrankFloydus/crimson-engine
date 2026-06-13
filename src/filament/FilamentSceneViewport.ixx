module;

#include <filament/Engine.h>

#include <memory>
#include <optional>

export module quader.filament.scene_viewport;

export import quader.filament.viewport_vulkan_platform;
export import quader.render.opengl_texture;
export import quader.render.types;
export import quader.render.viewport_types;

export namespace quader {

class FilamentSceneViewport {
public:
    FilamentSceneViewport(filament::Engine& engine, FilamentViewportVulkanPlatform& platform);
    FilamentSceneViewport(filament::Engine& engine, IOpenGlTexture2DAllocator& texture_allocator);

    FilamentSceneViewport(const FilamentSceneViewport&) = delete;
    FilamentSceneViewport& operator=(const FilamentSceneViewport&) = delete;

    ~FilamentSceneViewport();

    void render_into(const ViewportRenderRequest& request, ViewportRenderFrame& frame);
    [[nodiscard]] std::optional<ViewportSampledGpuImage> resolve_viewport_gpu_image(
        const ViewportGpuImageHandle& handle);
    [[nodiscard]] bool has_retired_viewport_gpu_resources() const noexcept;
    void collect_retired_viewport_gpu_resources();

private:
    struct Resources;
    std::unique_ptr<Resources> resources_;
};

} // namespace quader

module;

#include <filament/Engine.h>
#include <filament/SwapChain.h>
#include <filament/View.h>

#include <memory>
#include <optional>

export module quader.filament.scene_viewport_render_targets;

export import quader.filament.viewport_vulkan_platform;
export import quader.render.opengl_texture;
export import quader.render.viewport_types;

export namespace quader {

class FilamentViewportRenderTargetResources {
public:
    FilamentViewportRenderTargetResources() = default;
    FilamentViewportRenderTargetResources(const FilamentViewportRenderTargetResources&) = delete;
    FilamentViewportRenderTargetResources& operator=(const FilamentViewportRenderTargetResources&) = delete;
    virtual ~FilamentViewportRenderTargetResources() = default;

    [[nodiscard]] virtual bool ensure_size(
        ViewportId viewport_id,
        ViewportGeneration generation,
        RenderExtent requested_extent,
        filament::View& view) = 0;
    [[nodiscard]] virtual bool has_swapchain() const = 0;
    [[nodiscard]] virtual RenderExtent current_extent() const = 0;
    [[nodiscard]] virtual filament::SwapChain* current_swapchain() const = 0;
    virtual void finish_rendered_frame(ViewportId viewport_id, ViewportRenderFrame& frame) = 0;
    [[nodiscard]] virtual std::optional<ViewportSampledGpuImage> resolve_viewport_gpu_image(
        const ViewportGpuImageHandle& handle) = 0;
    [[nodiscard]] virtual bool has_retired_viewport_gpu_resources() const noexcept = 0;
    virtual void collect_retired_viewport_gpu_resources() = 0;
};

[[nodiscard]] std::unique_ptr<FilamentViewportRenderTargetResources> create_vulkan_viewport_render_targets(
    filament::Engine& engine,
    FilamentViewportVulkanPlatform& platform);

[[nodiscard]] std::unique_ptr<FilamentViewportRenderTargetResources> create_opengl_viewport_render_targets(
    filament::Engine& engine,
    IOpenGlTexture2DAllocator& texture_allocator);

} // namespace quader

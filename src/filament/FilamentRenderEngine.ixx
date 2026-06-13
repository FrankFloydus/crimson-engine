module;
#include <memory>
#include <optional>

export module quader.filament.render_engine;

export import quader.render.engine;
export import quader.render.opengl_texture;
import quader.render.vulkan_device;
export import quader.render.viewport_renderer;

export namespace quader {

class FilamentRenderEngine final : public IViewportRenderer {
public:
    explicit FilamentRenderEngine(VulkanSharedDeviceContext shared_device_context);
    FilamentRenderEngine(
        OpenGlSharedContextHandle shared_context,
        IOpenGlTexture2DAllocator& texture_allocator);

    FilamentRenderEngine(const FilamentRenderEngine&) = delete;
    FilamentRenderEngine& operator=(const FilamentRenderEngine&) = delete;

    ~FilamentRenderEngine() override;

    [[nodiscard]] RendererStatus status() const override;
    [[nodiscard]] RendererCapabilities capabilities() const override;

    [[nodiscard]] std::optional<ViewportId> try_create_viewport() override;
    void release_viewport(ViewportId viewport_id) override;
    [[nodiscard]] bool request_viewport_frame(const ViewportRenderRequest& request) override;
    [[nodiscard]] std::optional<ViewportRenderFrame> try_consume_viewport_frame(ViewportId viewport_id) override;
    [[nodiscard]] std::optional<ViewportSampledGpuImage> resolve_viewport_gpu_image(
        const ViewportGpuImageHandle& handle) override;
    [[nodiscard]] bool has_retired_viewport_gpu_resources() const noexcept override;
    void collect_retired_viewport_gpu_resources() override;

private:
    struct Resources;

    std::unique_ptr<Resources> resources_;
    RendererStatus status_;
};

} // namespace quader

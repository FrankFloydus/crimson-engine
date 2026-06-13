module;

#include <memory>

export module quader.filament.render_engine_factory;

export import quader.render.engine_factory;

export namespace quader {

class FilamentRenderEngineFactory final : public IRenderEngineFactory {
public:
    FilamentRenderEngineFactory() noexcept;

    [[nodiscard]] RenderEngineKind engine_kind() const noexcept override;
    [[nodiscard]] std::unique_ptr<IViewportRenderer> create_vulkan_viewport_renderer(
        const VulkanSharedDeviceContext& shared_device_context) const override;
    [[nodiscard]] std::unique_ptr<IViewportRenderer> create_opengl_viewport_renderer(
        OpenGlSharedContextHandle shared_context,
        IOpenGlTexture2DAllocator& texture_allocator) const override;
};

} // namespace quader

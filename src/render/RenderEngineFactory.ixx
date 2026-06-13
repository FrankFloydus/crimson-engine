module;

#include <memory>

export module quader.render.engine_factory;

export import quader.render.opengl_texture;
export import quader.render.vulkan_device;
export import quader.render.viewport_renderer;

export namespace quader {

class IRenderEngineFactory {
public:
    IRenderEngineFactory() = default;
    IRenderEngineFactory(const IRenderEngineFactory&) = delete;
    IRenderEngineFactory& operator=(const IRenderEngineFactory&) = delete;

    virtual ~IRenderEngineFactory() = default;

    [[nodiscard]] virtual RenderEngineKind engine_kind() const noexcept = 0;
    [[nodiscard]] virtual std::unique_ptr<IViewportRenderer> create_vulkan_viewport_renderer(
        const VulkanSharedDeviceContext& shared_device_context) const = 0;
    [[nodiscard]] virtual std::unique_ptr<IViewportRenderer> create_opengl_viewport_renderer(
        OpenGlSharedContextHandle shared_context,
        IOpenGlTexture2DAllocator& texture_allocator) const = 0;
};

} // namespace quader

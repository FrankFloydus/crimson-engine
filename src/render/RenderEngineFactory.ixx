module;

#include <memory>

export module quader.render.engine_factory;

export import quader.render.opengl_texture;
import quader.render.vulkan_device;
export import quader.render.viewport_renderer;

export namespace quader {

[[nodiscard]] std::unique_ptr<IViewportRenderer> create_default_viewport_renderer(
    const VulkanSharedDeviceContext& shared_device_context);
[[nodiscard]] std::unique_ptr<IViewportRenderer> create_opengl_viewport_renderer(
    OpenGlSharedContextHandle shared_context,
    IOpenGlTexture2DAllocator& texture_allocator);

} // namespace quader

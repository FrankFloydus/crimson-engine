module;

#include <memory>

module quader.render.engine_factory;

import quader.filament.render_engine;

namespace quader {

std::unique_ptr<IViewportRenderer> create_default_viewport_renderer(
    const VulkanSharedDeviceContext& shared_device_context)
{
    return std::make_unique<FilamentRenderEngine>(shared_device_context);
}

std::unique_ptr<IViewportRenderer> create_opengl_viewport_renderer(
    OpenGlSharedContextHandle shared_context,
    IOpenGlTexture2DAllocator& texture_allocator)
{
    return std::make_unique<FilamentRenderEngine>(shared_context, texture_allocator);
}

} // namespace quader

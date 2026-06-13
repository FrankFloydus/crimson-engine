module;

#include <memory>

module quader.filament.render_engine_factory;

import quader.filament.diagnostics;
import quader.filament.render_engine;

namespace quader {

FilamentRenderEngineFactory::FilamentRenderEngineFactory() noexcept
{
    install_filament_fatal_diagnostics();
}

RenderEngineKind FilamentRenderEngineFactory::engine_kind() const noexcept
{
    return RenderEngineKind::filament;
}

std::unique_ptr<IViewportRenderer> FilamentRenderEngineFactory::create_vulkan_viewport_renderer(
    const VulkanSharedDeviceContext& shared_device_context) const
{
    return std::make_unique<FilamentRenderEngine>(shared_device_context);
}

std::unique_ptr<IViewportRenderer> FilamentRenderEngineFactory::create_opengl_viewport_renderer(
    OpenGlSharedContextHandle shared_context,
    IOpenGlTexture2DAllocator& texture_allocator) const
{
    return std::make_unique<FilamentRenderEngine>(shared_context, texture_allocator);
}

} // namespace quader

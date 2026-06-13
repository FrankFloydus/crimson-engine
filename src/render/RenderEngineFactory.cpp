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

} // namespace quader

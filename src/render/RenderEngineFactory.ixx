module;

#include <memory>

export module quader.render.engine_factory;

import quader.render.vulkan_device;
export import quader.render.viewport_renderer;

export namespace quader {

[[nodiscard]] std::unique_ptr<IViewportRenderer> create_default_viewport_renderer(
    const VulkanSharedDeviceContext& shared_device_context);

} // namespace quader

module;

#include <vector>

export module quader.platform.sdl_vulkan_presentation_surface;

export import quader.render.vulkan_presentation_surface;

import quader.platform.sdl_window;

export namespace quader {

class SdlVulkanPresentationSurface final : public IVulkanPresentationSurface {
public:
    explicit SdlVulkanPresentationSurface(const SdlWindow& window);

    [[nodiscard]] std::vector<const char*> required_instance_extensions() const override;
    [[nodiscard]] VkSurfaceKHR create_surface(
        VkInstance instance,
        const VkAllocationCallbacks* allocator) const override;
    [[nodiscard]] VulkanFramebufferExtent framebuffer_extent() const override;
    [[nodiscard]] bool is_minimized() const override;
    void wait_while_minimized() const override;

private:
    const SdlWindow& window_;
};

} // namespace quader

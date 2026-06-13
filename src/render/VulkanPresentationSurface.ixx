module;

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

export module quader.render.vulkan_presentation_surface;

export namespace quader {

struct VulkanFramebufferExtent {
    uint32_t width = 0;
    uint32_t height = 0;

    [[nodiscard]] bool has_pixels() const noexcept
    {
        return width > 0 && height > 0;
    }
};

class IVulkanPresentationSurface {
public:
    virtual ~IVulkanPresentationSurface() = default;

    [[nodiscard]] virtual std::vector<const char*> required_instance_extensions() const = 0;
    [[nodiscard]] virtual VkSurfaceKHR create_surface(
        VkInstance instance,
        const VkAllocationCallbacks* allocator) const = 0;
    [[nodiscard]] virtual VulkanFramebufferExtent framebuffer_extent() const = 0;
    [[nodiscard]] virtual bool is_minimized() const = 0;
    virtual void wait_while_minimized() const = 0;
};

} // namespace quader

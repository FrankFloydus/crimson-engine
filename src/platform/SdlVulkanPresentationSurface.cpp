module;

#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <stdexcept>
#include <vector>

module quader.platform.sdl_vulkan_presentation_surface;

namespace quader {

SdlVulkanPresentationSurface::SdlVulkanPresentationSurface(const SdlWindow& window)
    : window_(window)
{
}

std::vector<const char*> SdlVulkanPresentationSurface::required_instance_extensions() const
{
    uint32_t extension_count = 0;
    const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&extension_count);
    if (extensions == nullptr) {
        throw std::runtime_error(window_.last_error());
    }

    std::vector<const char*> result;
    result.reserve(extension_count);
    for (uint32_t index = 0; index < extension_count; ++index) {
        result.push_back(extensions[index]);
    }
    return result;
}

VkSurfaceKHR SdlVulkanPresentationSurface::create_surface(
    VkInstance instance,
    const VkAllocationCallbacks* allocator) const
{
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(window_.native_handle(), instance, allocator, &surface)) {
        throw std::runtime_error(window_.last_error());
    }
    return surface;
}

VulkanFramebufferExtent SdlVulkanPresentationSurface::framebuffer_extent() const
{
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window_.native_handle(), &width, &height);

    return VulkanFramebufferExtent {
        static_cast<uint32_t>(width > 0 ? width : 0),
        static_cast<uint32_t>(height > 0 ? height : 0),
    };
}

bool SdlVulkanPresentationSurface::is_minimized() const
{
    return (SDL_GetWindowFlags(window_.native_handle()) & SDL_WINDOW_MINIMIZED) != 0;
}

void SdlVulkanPresentationSurface::wait_while_minimized() const
{
    SDL_Delay(10);
}

} // namespace quader

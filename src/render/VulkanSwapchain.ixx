module;

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>

export module quader.render.vulkan_swapchain;

export import quader.platform.sdl_window;
export import quader.render.vulkan_device;

export namespace quader {

class VulkanSwapchain {
public:
    VulkanSwapchain(const VulkanDevice& device, const SdlWindow& window);

    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

    ~VulkanSwapchain();

    [[nodiscard]] uint32_t min_image_count() const;
    [[nodiscard]] uint32_t image_count() const;
    [[nodiscard]] VkRenderPass render_pass() const;

    void recreate_if_needed(const SdlWindow& window);
    void render_current_imgui_frame();

private:
    struct Resources;
    std::unique_ptr<Resources> resources_;
};

} // namespace quader

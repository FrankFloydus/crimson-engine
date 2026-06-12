module;

#include <vulkan/vulkan.h>

export module quader.render.vulkan_device;

export import quader.platform.sdl_window;

export namespace quader {

class VulkanDevice {
public:
    explicit VulkanDevice(const SdlWindow& window);

    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;

    ~VulkanDevice();

    [[nodiscard]] VkInstance instance() const;
    [[nodiscard]] VkSurfaceKHR surface() const;
    [[nodiscard]] VkPhysicalDevice physical_device() const;
    [[nodiscard]] VkDevice device() const;
    [[nodiscard]] uint32_t queue_family() const;
    [[nodiscard]] VkQueue queue() const;
    [[nodiscard]] VkPipelineCache pipeline_cache() const;
    [[nodiscard]] const VkAllocationCallbacks* allocator() const;

    void wait_idle() const;

private:
    void create_instance(const SdlWindow& window);
    void create_surface(const SdlWindow& window);
    void select_physical_device();
    void create_device();

    const VkAllocationCallbacks* allocator_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    uint32_t queue_family_ = UINT32_MAX;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkPipelineCache pipeline_cache_ = VK_NULL_HANDLE;
};

} // namespace quader

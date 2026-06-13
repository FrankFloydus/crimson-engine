module;

#include <vulkan/vulkan.h>

#include <cstdint>

export module quader.render.vulkan_device;

export import quader.platform.sdl_window;

export namespace quader {

enum class VulkanSharedQueueMode : uint8_t {
    dedicated_renderer_queue,
    serialized_single_queue,
};

struct VulkanQueueContext {
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t family_index = UINT32_MAX;
    uint32_t queue_index = UINT32_MAX;

    [[nodiscard]] bool is_valid() const noexcept
    {
        return queue != VK_NULL_HANDLE &&
            family_index != UINT32_MAX &&
            queue_index != UINT32_MAX;
    }
};

// Borrowed app-owned handles for renderer adapters; VulkanDevice keeps ownership.
struct VulkanSharedDeviceContext {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    const VkAllocationCallbacks* allocator = nullptr;
    VkPipelineCache pipeline_cache = VK_NULL_HANDLE;
    VulkanQueueContext app_graphics_queue;
    VulkanQueueContext renderer_graphics_queue;
    uint32_t graphics_queue_family_available_count = 0;
    uint32_t created_graphics_queue_count = 0;
    VulkanSharedQueueMode queue_mode = VulkanSharedQueueMode::serialized_single_queue;
    bool owns_instance = true;
    bool owns_device = true;
    bool debug_utils_supported = false;
    bool debug_markers_supported = false;
    bool multiview_supported = false;

    [[nodiscard]] bool is_valid() const noexcept
    {
        return instance != VK_NULL_HANDLE &&
            physical_device != VK_NULL_HANDLE &&
            device != VK_NULL_HANDLE &&
            app_graphics_queue.is_valid() &&
            renderer_graphics_queue.is_valid() &&
            created_graphics_queue_count != 0 &&
            graphics_queue_family_available_count >= created_graphics_queue_count;
    }

    [[nodiscard]] bool requires_queue_serialization() const noexcept
    {
        return queue_mode == VulkanSharedQueueMode::serialized_single_queue;
    }
};

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
    [[nodiscard]] uint32_t queue_index() const;
    [[nodiscard]] uint32_t graphics_queue_family_available_count() const;
    [[nodiscard]] uint32_t created_graphics_queue_count() const;
    [[nodiscard]] VkQueue queue() const;
    [[nodiscard]] VkPipelineCache pipeline_cache() const;
    [[nodiscard]] const VkAllocationCallbacks* allocator() const;
    [[nodiscard]] VulkanSharedQueueMode queue_mode_for_renderer() const;
    [[nodiscard]] VulkanSharedDeviceContext shared_device_context() const;

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
    uint32_t queue_family_available_count_ = 0;
    uint32_t app_queue_index_ = UINT32_MAX;
    uint32_t renderer_queue_index_ = UINT32_MAX;
    uint32_t created_graphics_queue_count_ = 0;
    VulkanSharedQueueMode queue_mode_for_renderer_ = VulkanSharedQueueMode::serialized_single_queue;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkQueue renderer_queue_ = VK_NULL_HANDLE;
    VkPipelineCache pipeline_cache_ = VK_NULL_HANDLE;
};

} // namespace quader

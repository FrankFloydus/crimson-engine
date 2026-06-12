module;

#include <imgui.h>
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>

export module quader.ui.vulkan_viewport_texture;

export import quader.render.types;
export import quader.render.vulkan_device;

export namespace quader {

class VulkanViewportTexture {
public:
    explicit VulkanViewportTexture(const VulkanDevice& device);

    VulkanViewportTexture(const VulkanViewportTexture&) = delete;
    VulkanViewportTexture& operator=(const VulkanViewportTexture&) = delete;

    ~VulkanViewportTexture();

    void upload_rgba(uint32_t width, uint32_t height, const uint8_t* rgba);

    [[nodiscard]] ImTextureID texture_id() const;
    [[nodiscard]] bool has_pixels() const;
    [[nodiscard]] RenderExtent extent() const;

private:
    uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) const;
    void create_texture(uint32_t width, uint32_t height);
    void destroy_texture();
    VkCommandPool create_upload_command_pool();
    struct UploadSlot {
        VkCommandBuffer command_buffer = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        VkBuffer staging_buffer = VK_NULL_HANDLE;
        VkDeviceMemory staging_memory = VK_NULL_HANDLE;
        VkDeviceSize staging_size = 0;
    };

    void create_upload_slots();
    void destroy_upload_slots();
    UploadSlot& acquire_upload_slot();
    void reset_upload_slot(UploadSlot& slot) const;
    void ensure_staging_buffer(UploadSlot& slot, VkDeviceSize required_size);
    void destroy_staging_buffer(UploadSlot& slot);
    void submit_upload_commands(const UploadSlot& slot) const;
    void transition_image(VkCommandBuffer command_buffer, VkImageLayout old_layout, VkImageLayout new_layout) const;
    void copy_buffer_to_image(VkCommandBuffer command_buffer, VkBuffer source, uint32_t width, uint32_t height) const;

    static constexpr uint32_t upload_slot_count_ = 3;

    const VulkanDevice& device_;
    VkCommandPool upload_command_pool_ = VK_NULL_HANDLE;
    std::array<UploadSlot, upload_slot_count_> upload_slots_;
    uint32_t next_upload_slot_ = 0;
    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkImageView image_view_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_ = VK_NULL_HANDLE;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    bool has_pixels_ = false;
};

} // namespace quader

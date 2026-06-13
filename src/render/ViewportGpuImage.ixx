module;

#include <vulkan/vulkan.h>

export module quader.render.viewport_gpu_image;

export import quader.render.viewport_types;

export namespace quader {

struct ViewportSampledGpuImage {
    ViewportGpuImageHandle handle;
    VkImageView image_view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkImageLayout image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    [[nodiscard]] bool is_valid() const noexcept
    {
        return handle.is_ready() &&
            image_view != VK_NULL_HANDLE &&
            sampler != VK_NULL_HANDLE &&
            image_layout != VK_IMAGE_LAYOUT_UNDEFINED &&
            handle.extent.is_valid();
    }
};

} // namespace quader

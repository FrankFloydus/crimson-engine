module;

#include <vulkan/vulkan.h>

#include <cstdint>

export module quader.render.viewport_gpu_image;

export import quader.render.viewport_types;

export namespace quader {

enum class ViewportSampledGpuImageBackend {
    vulkan,
    opengl,
};

struct ViewportSampledGpuImage {
    ViewportGpuImageHandle handle;
    ViewportSampledGpuImageBackend backend = ViewportSampledGpuImageBackend::vulkan;
    VkImageView image_view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkImageLayout image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    uint32_t opengl_texture_id = 0;

    [[nodiscard]] bool is_valid() const noexcept
    {
        if (!handle.is_ready() || !handle.extent.is_valid()) {
            return false;
        }

        switch (backend) {
        case ViewportSampledGpuImageBackend::vulkan:
            return image_view != VK_NULL_HANDLE &&
                sampler != VK_NULL_HANDLE &&
                image_layout != VK_IMAGE_LAYOUT_UNDEFINED;
        case ViewportSampledGpuImageBackend::opengl:
            return opengl_texture_id != 0;
        }

        return false;
    }
};

} // namespace quader

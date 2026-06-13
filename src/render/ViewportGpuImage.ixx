module;

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <type_traits>

export module quader.render.viewport_gpu_image;

export import quader.render.viewport_types;

export namespace quader {

enum class ViewportSampledGpuImageBackend {
    vulkan,
    opengl,
};

enum class ViewportTextureTokenBackend : uint8_t {
    vulkan,
    opengl,
};

struct ViewportTextureTokenPayload {
    uint64_t word0 = 0;
    uint64_t word1 = 0;
    uint64_t word2 = 0;

    [[nodiscard]] friend bool operator==(
        ViewportTextureTokenPayload left,
        ViewportTextureTokenPayload right) noexcept
    {
        return left.word0 == right.word0 &&
            left.word1 == right.word1 &&
            left.word2 == right.word2;
    }
};

struct ViewportTextureToken {
    ViewportGpuImageHandle handle;
    ViewportTextureTokenBackend backend = ViewportTextureTokenBackend::vulkan;
    ViewportTextureTokenPayload payload;

    [[nodiscard]] bool is_valid() const noexcept
    {
        if (!handle.is_ready()) {
            return false;
        }

        switch (backend) {
        case ViewportTextureTokenBackend::vulkan:
            return payload.word0 != 0 &&
                payload.word1 != 0 &&
                payload.word2 != static_cast<uint64_t>(VK_IMAGE_LAYOUT_UNDEFINED);
        case ViewportTextureTokenBackend::opengl:
            return payload.word0 != 0;
        }

        return false;
    }

    [[nodiscard]] friend bool operator==(const ViewportTextureToken& left, const ViewportTextureToken& right) noexcept
    {
        return left.handle.viewport_id == right.handle.viewport_id &&
            left.handle.generation == right.handle.generation &&
            left.handle.slot_index == right.handle.slot_index &&
            left.handle.image_generation == right.handle.image_generation &&
            left.handle.resource_id == right.handle.resource_id &&
            left.handle.extent.width == right.handle.extent.width &&
            left.handle.extent.height == right.handle.extent.height &&
            left.handle.readiness == right.handle.readiness &&
            left.backend == right.backend &&
            left.payload == right.payload;
    }
};

struct ViewportVulkanTextureTokenPayload {
    VkImageView image_view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkImageLayout image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    [[nodiscard]] bool is_valid() const noexcept
    {
        return image_view != VK_NULL_HANDLE &&
            sampler != VK_NULL_HANDLE &&
            image_layout != VK_IMAGE_LAYOUT_UNDEFINED;
    }
};

struct ViewportOpenGlTextureTokenPayload {
    uint32_t texture_id = 0;

    [[nodiscard]] bool is_valid() const noexcept
    {
        return texture_id != 0;
    }
};

template <typename Handle>
[[nodiscard]] uint64_t viewport_texture_payload_word_from_handle(Handle handle) noexcept
{
    if constexpr (std::is_pointer_v<Handle>) {
        return static_cast<uint64_t>(reinterpret_cast<std::uintptr_t>(handle));
    } else {
        return static_cast<uint64_t>(handle);
    }
}

template <typename Handle>
[[nodiscard]] Handle viewport_texture_handle_from_payload_word(uint64_t word) noexcept
{
    if constexpr (std::is_pointer_v<Handle>) {
        return reinterpret_cast<Handle>(static_cast<std::uintptr_t>(word));
    } else {
        return static_cast<Handle>(word);
    }
}

[[nodiscard]] inline ViewportTextureToken make_vulkan_viewport_texture_token(
    ViewportGpuImageHandle handle,
    ViewportVulkanTextureTokenPayload payload) noexcept
{
    return ViewportTextureToken {
        .handle = handle,
        .backend = ViewportTextureTokenBackend::vulkan,
        .payload = {
            viewport_texture_payload_word_from_handle(payload.image_view),
            viewport_texture_payload_word_from_handle(payload.sampler),
            static_cast<uint64_t>(payload.image_layout),
        },
    };
}

[[nodiscard]] inline ViewportTextureToken make_opengl_viewport_texture_token(
    ViewportGpuImageHandle handle,
    ViewportOpenGlTextureTokenPayload payload) noexcept
{
    return ViewportTextureToken {
        .handle = handle,
        .backend = ViewportTextureTokenBackend::opengl,
        .payload = {
            static_cast<uint64_t>(payload.texture_id),
            0,
            0,
        },
    };
}

[[nodiscard]] inline std::optional<ViewportVulkanTextureTokenPayload> unpack_vulkan_viewport_texture_token(
    const ViewportTextureToken& texture) noexcept
{
    if (texture.backend != ViewportTextureTokenBackend::vulkan || !texture.is_valid()) {
        return std::nullopt;
    }

    return ViewportVulkanTextureTokenPayload {
        .image_view = viewport_texture_handle_from_payload_word<VkImageView>(texture.payload.word0),
        .sampler = viewport_texture_handle_from_payload_word<VkSampler>(texture.payload.word1),
        .image_layout = static_cast<VkImageLayout>(texture.payload.word2),
    };
}

[[nodiscard]] inline std::optional<ViewportOpenGlTextureTokenPayload> unpack_opengl_viewport_texture_token(
    const ViewportTextureToken& texture) noexcept
{
    if (texture.backend != ViewportTextureTokenBackend::opengl || !texture.is_valid()) {
        return std::nullopt;
    }

    return ViewportOpenGlTextureTokenPayload {
        .texture_id = static_cast<uint32_t>(texture.payload.word0),
    };
}

struct ViewportSampledGpuImage {
    ViewportGpuImageHandle handle;
    ViewportSampledGpuImageBackend backend = ViewportSampledGpuImageBackend::vulkan;
    VkImageView image_view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkImageLayout image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    uint32_t opengl_texture_id = 0;

    [[nodiscard]] ViewportTextureToken texture_token() const noexcept
    {
        switch (backend) {
        case ViewportSampledGpuImageBackend::vulkan:
            return make_vulkan_viewport_texture_token(
                handle,
                ViewportVulkanTextureTokenPayload {
                    .image_view = image_view,
                    .sampler = sampler,
                    .image_layout = image_layout,
                });
        case ViewportSampledGpuImageBackend::opengl:
            return make_opengl_viewport_texture_token(
                handle,
                ViewportOpenGlTextureTokenPayload {
                    .texture_id = opengl_texture_id,
                });
        }

        return {};
    }

    [[nodiscard]] bool is_valid() const noexcept
    {
        return texture_token().is_valid();
    }
};

} // namespace quader

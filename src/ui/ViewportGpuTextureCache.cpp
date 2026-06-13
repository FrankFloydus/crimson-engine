module;

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

module quader.ui.viewport_gpu_texture_cache;

namespace quader {
namespace {

struct ViewportGpuTextureKey {
    ViewportId viewport_id;
    ViewportGeneration generation;
    uint64_t resource_id = 0;
    uint64_t image_generation = 0;
    RenderExtent extent;
    VkImageView image_view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkImageLayout image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    [[nodiscard]] bool operator==(const ViewportGpuTextureKey& other) const noexcept
    {
        return viewport_id == other.viewport_id &&
            generation == other.generation &&
            resource_id == other.resource_id &&
            image_generation == other.image_generation &&
            extent.width == other.extent.width &&
            extent.height == other.extent.height &&
            image_view == other.image_view &&
            sampler == other.sampler &&
            image_layout == other.image_layout;
    }
};

[[nodiscard]] ViewportGpuTextureKey key_from(const ViewportSampledGpuImage& image)
{
    return ViewportGpuTextureKey {
        .viewport_id = image.handle.viewport_id,
        .generation = image.handle.generation,
        .resource_id = image.handle.resource_id,
        .image_generation = image.handle.image_generation,
        .extent = image.handle.extent,
        .image_view = image.image_view,
        .sampler = image.sampler,
        .image_layout = image.image_layout,
    };
}

} // namespace

class ViewportGpuTextureDescriptorCache::Impl {
public:
    explicit Impl(IViewportTextureRegistry& texture_registry)
        : texture_registry_(texture_registry)
    {
    }

    ~Impl()
    {
        clear();
    }

    [[nodiscard]] bool present(const ViewportSampledGpuImage& image)
    {
        if (!image.is_valid()) {
            return false;
        }

        const ViewportGpuTextureKey key = key_from(image);
        const auto existing = descriptors_.find(image.handle.viewport_id);
        if (existing != descriptors_.end() && existing->second.key == key) {
            return true;
        }

        const uint64_t texture_id = texture_registry_.add_texture(
            image.sampler,
            image.image_view,
            image.image_layout);
        if (texture_id == 0) {
            return false;
        }

        if (existing != descriptors_.end()) {
            retire_texture(existing->second.texture_id);
            existing->second = Descriptor {
                .key = key,
                .texture_id = texture_id,
            };
            return true;
        }

        descriptors_.emplace(
            image.handle.viewport_id,
            Descriptor {
                .key = key,
                .texture_id = texture_id,
            });
        return true;
    }

    [[nodiscard]] bool has_retired_textures() const noexcept
    {
        return !retired_texture_ids_.empty();
    }

    void collect_retired_textures()
    {
        for (uint64_t texture_id : retired_texture_ids_) {
            texture_registry_.remove_texture(texture_id);
        }
        retired_texture_ids_.clear();
    }

    void remove(ViewportId viewport_id)
    {
        const auto existing = descriptors_.find(viewport_id);
        if (existing == descriptors_.end()) {
            return;
        }

        retire_texture(existing->second.texture_id);
        descriptors_.erase(existing);
    }

    void prune(std::span<const ViewportId> live_viewport_ids)
    {
        std::vector<ViewportId> dead_ids;
        for (const auto& [viewport_id, _] : descriptors_) {
            const bool live = std::find(live_viewport_ids.begin(), live_viewport_ids.end(), viewport_id) !=
                live_viewport_ids.end();
            if (!live) {
                dead_ids.push_back(viewport_id);
            }
        }

        for (ViewportId viewport_id : dead_ids) {
            remove(viewport_id);
        }
    }

    void clear()
    {
        collect_retired_textures();
        for (const auto& [_, descriptor] : descriptors_) {
            texture_registry_.remove_texture(descriptor.texture_id);
        }
        descriptors_.clear();
    }

    [[nodiscard]] ViewportTextureView texture_view(ViewportId viewport_id) const
    {
        const auto existing = descriptors_.find(viewport_id);
        if (existing == descriptors_.end()) {
            return {};
        }

        return ViewportTextureView {
            .texture_id = existing->second.texture_id,
            .extent = existing->second.key.extent,
            .has_pixels = true,
        };
    }

private:
    struct Descriptor {
        ViewportGpuTextureKey key;
        uint64_t texture_id = 0;
    };

    void retire_texture(uint64_t texture_id)
    {
        if (texture_id != 0) {
            retired_texture_ids_.push_back(texture_id);
        }
    }

    IViewportTextureRegistry& texture_registry_;
    std::unordered_map<ViewportId, Descriptor, ViewportIdHash> descriptors_;
    std::vector<uint64_t> retired_texture_ids_;
};

ViewportGpuTextureDescriptorCache::ViewportGpuTextureDescriptorCache(IViewportTextureRegistry& texture_registry)
    : impl_(std::make_unique<Impl>(texture_registry))
{
}

ViewportGpuTextureDescriptorCache::~ViewportGpuTextureDescriptorCache() = default;

bool ViewportGpuTextureDescriptorCache::present(const ViewportSampledGpuImage& image)
{
    return impl_->present(image);
}

bool ViewportGpuTextureDescriptorCache::has_retired_textures() const noexcept
{
    return impl_->has_retired_textures();
}

void ViewportGpuTextureDescriptorCache::collect_retired_textures()
{
    impl_->collect_retired_textures();
}

void ViewportGpuTextureDescriptorCache::remove(ViewportId viewport_id)
{
    impl_->remove(viewport_id);
}

void ViewportGpuTextureDescriptorCache::prune(std::span<const ViewportId> live_viewport_ids)
{
    impl_->prune(live_viewport_ids);
}

void ViewportGpuTextureDescriptorCache::clear()
{
    impl_->clear();
}

ViewportTextureView ViewportGpuTextureDescriptorCache::texture_view(ViewportId viewport_id) const
{
    return impl_->texture_view(viewport_id);
}

} // namespace quader

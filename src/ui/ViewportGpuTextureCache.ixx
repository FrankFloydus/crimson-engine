module;

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <span>

export module quader.ui.viewport_gpu_texture_cache;

export import quader.render.viewport_gpu_image;
export import quader.render.viewport_types;
export import quader.ui.viewport_surface;

export namespace quader {

class IViewportTextureRegistry {
public:
    IViewportTextureRegistry() = default;
    IViewportTextureRegistry(const IViewportTextureRegistry&) = delete;
    IViewportTextureRegistry& operator=(const IViewportTextureRegistry&) = delete;

    virtual ~IViewportTextureRegistry() = default;

    [[nodiscard]] virtual uint64_t add_texture(
        VkSampler sampler,
        VkImageView image_view,
        VkImageLayout image_layout) = 0;
    virtual void remove_texture(uint64_t texture_id) = 0;
};

class ViewportGpuTextureDescriptorCache {
public:
    explicit ViewportGpuTextureDescriptorCache(IViewportTextureRegistry& texture_registry);

    ViewportGpuTextureDescriptorCache(const ViewportGpuTextureDescriptorCache&) = delete;
    ViewportGpuTextureDescriptorCache& operator=(const ViewportGpuTextureDescriptorCache&) = delete;

    ~ViewportGpuTextureDescriptorCache();

    [[nodiscard]] bool present(const ViewportSampledGpuImage& image);
    [[nodiscard]] bool has_retired_textures() const noexcept;
    void collect_retired_textures();
    void remove(ViewportId viewport_id);
    void prune(std::span<const ViewportId> live_viewport_ids);
    void clear();

    [[nodiscard]] ViewportTextureView texture_view(ViewportId viewport_id) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace quader

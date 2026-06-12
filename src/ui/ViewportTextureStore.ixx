module;

#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>

export module quader.ui.viewport_texture_store;

import quader.ui.vulkan_viewport_texture;
export import quader.render.viewport_types;
export import quader.render.vulkan_device;
export import quader.ui.viewport_surface;

export namespace quader {

class ViewportTextureStore {
public:
    explicit ViewportTextureStore(const VulkanDevice& device);

    ViewportTextureStore(const ViewportTextureStore&) = delete;
    ViewportTextureStore& operator=(const ViewportTextureStore&) = delete;

    void upload(ViewportId viewport_id, uint32_t width, uint32_t height, const uint8_t* rgba);
    void remove(ViewportId viewport_id);
    void prune(std::span<const ViewportId> live_viewport_ids);

    [[nodiscard]] ViewportTextureView texture_view(ViewportId viewport_id) const;

private:
    const VulkanDevice& device_;
    std::unordered_map<ViewportId, std::unique_ptr<VulkanViewportTexture>, ViewportIdHash> textures_;
};

} // namespace quader

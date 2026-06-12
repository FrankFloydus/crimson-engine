module;

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

module quader.ui.viewport_texture_store;

namespace quader {

ViewportTextureStore::ViewportTextureStore(const VulkanDevice& device)
    : device_(device)
{
}

void ViewportTextureStore::upload(ViewportId viewport_id, uint32_t width, uint32_t height, const uint8_t* rgba)
{
    if (!viewport_id.is_valid() || width == 0 || height == 0 || rgba == nullptr) {
        return;
    }

    auto [entry, inserted] = textures_.try_emplace(viewport_id);
    if (inserted || entry->second == nullptr) {
        entry->second = std::make_unique<VulkanViewportTexture>(device_);
    }

    entry->second->upload_rgba(width, height, rgba);
}

void ViewportTextureStore::remove(ViewportId viewport_id)
{
    textures_.erase(viewport_id);
}

void ViewportTextureStore::prune(std::span<const ViewportId> live_viewport_ids)
{
    std::vector<ViewportId> dead_ids;
    for (const auto& [viewport_id, _] : textures_) {
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

ViewportTextureView ViewportTextureStore::texture_view(ViewportId viewport_id) const
{
    const auto entry = textures_.find(viewport_id);
    if (entry == textures_.end() || entry->second == nullptr) {
        return {};
    }

    return ViewportTextureView {
        .texture_id = entry->second->texture_id(),
        .extent = entry->second->extent(),
        .has_pixels = entry->second->has_pixels(),
    };
}

} // namespace quader

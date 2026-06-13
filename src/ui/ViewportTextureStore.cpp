module;

#include <algorithm>
#include <memory>
#include <vector>

module quader.ui.viewport_texture_store;

namespace quader {

class ViewportTextureStore::Impl {
public:
    explicit Impl(IViewportTextureRegistry& texture_registry)
        : gpu_textures_(texture_registry)
    {
    }

    [[nodiscard]] bool present_texture_token(const ViewportTextureToken& texture)
    {
        if (!texture.is_valid()) {
            return false;
        }

        return gpu_textures_.present(texture);
    }

    [[nodiscard]] bool has_retired_gpu_textures() const noexcept
    {
        return gpu_textures_.has_retired_textures();
    }

    void collect_retired_gpu_textures()
    {
        gpu_textures_.collect_retired_textures();
    }

    void remove(ViewportId viewport_id)
    {
        gpu_textures_.remove(viewport_id);
    }

    void prune(std::span<const ViewportId> live_viewport_ids)
    {
        gpu_textures_.prune(live_viewport_ids);
    }

    [[nodiscard]] ViewportTextureView texture_view(ViewportId viewport_id) const
    {
        return gpu_textures_.texture_view(viewport_id);
    }

private:
    ViewportGpuTextureDescriptorCache gpu_textures_;
};

ViewportTextureStore::ViewportTextureStore(IViewportTextureRegistry& texture_registry)
    : impl_(std::make_unique<Impl>(texture_registry))
{
}

ViewportTextureStore::~ViewportTextureStore() = default;

bool ViewportTextureStore::present_texture_token(const ViewportTextureToken& texture)
{
    return impl_->present_texture_token(texture);
}

bool ViewportTextureStore::has_retired_gpu_textures() const noexcept
{
    return impl_->has_retired_gpu_textures();
}

void ViewportTextureStore::collect_retired_gpu_textures()
{
    impl_->collect_retired_gpu_textures();
}

void ViewportTextureStore::remove(ViewportId viewport_id)
{
    impl_->remove(viewport_id);
}

void ViewportTextureStore::prune(std::span<const ViewportId> live_viewport_ids)
{
    impl_->prune(live_viewport_ids);
}

ViewportTextureView ViewportTextureStore::texture_view(ViewportId viewport_id) const
{
    return impl_->texture_view(viewport_id);
}

} // namespace quader

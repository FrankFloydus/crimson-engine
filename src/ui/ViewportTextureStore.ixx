module;

#include <memory>
#include <span>

export module quader.ui.viewport_texture_store;

export import quader.render.viewport_types;
export import quader.ui.viewport_gpu_texture_cache;
export import quader.ui_shell.editor_viewport_contracts;

export namespace quader {

class ViewportTextureStore {
public:
    explicit ViewportTextureStore(IViewportTextureRegistry& texture_registry);

    ViewportTextureStore(const ViewportTextureStore&) = delete;
    ViewportTextureStore& operator=(const ViewportTextureStore&) = delete;
    ~ViewportTextureStore();

    [[nodiscard]] bool present_gpu_image(const ViewportSampledGpuImage& image);
    [[nodiscard]] bool has_retired_gpu_textures() const noexcept;
    void collect_retired_gpu_textures();
    void remove(ViewportId viewport_id);
    void prune(std::span<const ViewportId> live_viewport_ids);

    [[nodiscard]] ViewportTextureView texture_view(ViewportId viewport_id) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace quader

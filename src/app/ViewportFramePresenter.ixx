module;

#include <span>

export module quader.app.viewport_frame_presenter;

export import quader.render.viewport_types;
export import quader.ui.viewport_surface;

export namespace quader {

class IViewportFramePresenter {
public:
    virtual ~IViewportFramePresenter() = default;

    [[nodiscard]] virtual bool present_viewport_frame(const ViewportRenderFrame& frame) = 0;
    [[nodiscard]] virtual ViewportTextureView viewport_texture(ViewportId viewport_id) const = 0;
    [[nodiscard]] virtual bool text_input_active() const = 0;
    virtual void prune_viewport_textures(std::span<const ViewportId> live_viewport_ids) = 0;
};

} // namespace quader

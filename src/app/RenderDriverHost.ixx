module;

#include <memory>

export module quader.app.render_driver_host;

export import quader.platform.sdl_window;
export import quader.render.driver_kind;
export import quader.render.status;
export import quader.render.viewport_renderer;
export import quader.app.viewport_frame_presenter;
export import quader.ui.imgui_layer;

export namespace quader {

class IRenderDriverHost {
public:
    virtual ~IRenderDriverHost() = default;

    [[nodiscard]] virtual RenderDriverKind driver_kind() const noexcept = 0;
    [[nodiscard]] virtual RendererStatus status() const = 0;
    [[nodiscard]] virtual IViewportRenderer& viewport_renderer() = 0;
    [[nodiscard]] virtual IViewportFramePresenter& viewport_frame_presenter() = 0;
    [[nodiscard]] virtual ImGuiLayer& imgui() = 0;

    virtual void begin_frame() = 0;
    virtual void end_frame() = 0;
    virtual void collect_retired_viewport_gpu_resources(bool viewport_render_requests_pending) = 0;
};

[[nodiscard]] std::unique_ptr<IRenderDriverHost> create_default_render_driver_host(
    const SdlWindow& window);

} // namespace quader

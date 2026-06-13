module;

#include <span>

export module quader.platform.sdl_imgui_runtime_driver;

export import quader.platform.sdl_event_source;
export import quader.render.driver_kind;
export import quader.render.status;
export import quader.render.viewport_renderer;
export import quader.render.viewport_types;
export import quader.ui_shell.editor_viewport_contracts;

export namespace quader {

class ISdlImGuiRuntimeDriver {
public:
    ISdlImGuiRuntimeDriver() = default;
    ISdlImGuiRuntimeDriver(const ISdlImGuiRuntimeDriver&) = delete;
    ISdlImGuiRuntimeDriver& operator=(const ISdlImGuiRuntimeDriver&) = delete;

    virtual ~ISdlImGuiRuntimeDriver() = default;

    [[nodiscard]] virtual RenderDriverKind driver_kind() const noexcept = 0;
    [[nodiscard]] virtual RendererStatus status() const = 0;
    [[nodiscard]] virtual ISdlNativeEventSink& native_event_sink() noexcept = 0;
    [[nodiscard]] virtual IViewportRenderer& viewport_renderer() noexcept = 0;

    virtual void begin_frame() = 0;
    virtual void end_frame() = 0;
    virtual void collect_retired_viewport_gpu_resources(bool viewport_render_requests_pending) = 0;

    [[nodiscard]] virtual bool present_viewport_frame(const ViewportRenderFrame& frame) = 0;
    [[nodiscard]] virtual ViewportTextureView viewport_texture(ViewportId viewport_id) const = 0;
    [[nodiscard]] virtual bool text_input_active() const = 0;
    virtual void prune_viewport_textures(std::span<const ViewportId> live_viewport_ids) = 0;
};

} // namespace quader

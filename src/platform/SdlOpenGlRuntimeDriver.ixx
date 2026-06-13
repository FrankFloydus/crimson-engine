module;

#include <memory>
#include <span>
#include <string>

export module quader.platform.sdl_opengl_runtime_driver;

export import quader.platform.sdl_imgui_runtime_driver;
export import quader.platform.sdl_window;

export namespace quader {

class SdlOpenGlRuntimeDriver final : public ISdlImGuiRuntimeDriver {
public:
    SdlOpenGlRuntimeDriver(const SdlWindow& window, std::string imgui_ini_file);

    SdlOpenGlRuntimeDriver(const SdlOpenGlRuntimeDriver&) = delete;
    SdlOpenGlRuntimeDriver& operator=(const SdlOpenGlRuntimeDriver&) = delete;

    ~SdlOpenGlRuntimeDriver() override;

    [[nodiscard]] RenderDriverKind driver_kind() const noexcept override;
    [[nodiscard]] RendererStatus status() const override;
    [[nodiscard]] ISdlNativeEventSink& native_event_sink() noexcept override;
    [[nodiscard]] IViewportRenderer& viewport_renderer() noexcept override;

    void begin_frame() override;
    void end_frame() override;
    void collect_retired_viewport_gpu_resources(bool viewport_render_requests_pending) override;

    [[nodiscard]] bool present_viewport_frame(const ViewportRenderFrame& frame) override;
    [[nodiscard]] ViewportTextureView viewport_texture(ViewportId viewport_id) const override;
    [[nodiscard]] bool text_input_active() const override;
    void prune_viewport_textures(std::span<const ViewportId> live_viewport_ids) override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace quader

module;

#include "platform/SdlForward.h"

#include <memory>
#include <span>

export module quader.ui.imgui_opengl_layer;

export import quader.platform.sdl_event_source;
export import quader.platform.sdl_opengl_context;
export import quader.platform.sdl_window;
export import quader.ui.viewport_texture_store;

export namespace quader {

class ImGuiOpenGlLayer : public ISdlNativeEventSink {
public:
    ImGuiOpenGlLayer(
        const SdlWindow& window,
        SdlOpenGlContext& context,
        const char* glsl_version = "#version 410 core");

    ImGuiOpenGlLayer(const ImGuiOpenGlLayer&) = delete;
    ImGuiOpenGlLayer& operator=(const ImGuiOpenGlLayer&) = delete;

    ~ImGuiOpenGlLayer();

    void process_sdl_event(const SDL_Event& event) override;
    void begin_frame();
    void end_frame();
    [[nodiscard]] bool present_viewport_gpu_image(const ViewportSampledGpuImage& image);
    [[nodiscard]] bool has_retired_viewport_gpu_textures() const noexcept;
    void collect_retired_viewport_gpu_textures();
    void remove_viewport_texture(ViewportId viewport_id);
    void prune_viewport_textures(std::span<const ViewportId> live_viewport_ids);

    [[nodiscard]] bool text_input_active() const;
    [[nodiscard]] ViewportTextureView viewport_texture(ViewportId viewport_id) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace quader

module;

#include "platform/SdlForward.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>

export module quader.ui.imgui_layer;

export import quader.platform.sdl_window;
import quader.platform.sdl_event_source;
import quader.render.vulkan_device;
import quader.render.vulkan_swapchain;
export import quader.render.viewport_types;
export import quader.ui.viewport_gpu_texture_cache;
export import quader.ui_shell.editor_viewport_contracts;

export namespace quader {

class ImGuiLayer : public ISdlNativeEventSink {
public:
    ImGuiLayer(
        const SdlWindow& window,
        const VulkanDevice& device,
        const VulkanSwapchain& swapchain,
        std::string imgui_ini_file);

    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    ~ImGuiLayer();

    void process_sdl_event(const SDL_Event& event) override;
    void begin_frame();
    void end_frame(VulkanSwapchain& swapchain);
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

module;

#include <SDL3/SDL_events.h>
#include <imgui.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <span>

export module quader.ui.imgui_layer;

export import quader.platform.sdl_window;
import quader.platform.sdl_event_source;
export import quader.render.vulkan_device;
export import quader.render.vulkan_swapchain;
export import quader.render.viewport_types;
export import quader.ui.viewport_surface;
import quader.ui.viewport_texture_store;

export namespace quader {

class ImGuiLayer : public ISdlNativeEventSink {
public:
    ImGuiLayer(const SdlWindow& window, const VulkanDevice& device, const VulkanSwapchain& swapchain);

    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    ~ImGuiLayer();

    void process_sdl_event(const SDL_Event& event) override;
    void begin_frame();
    void end_frame(VulkanSwapchain& swapchain);
    void upload_viewport_texture(ViewportId viewport_id, uint32_t width, uint32_t height, const uint8_t* rgba);
    void remove_viewport_texture(ViewportId viewport_id);
    void prune_viewport_textures(std::span<const ViewportId> live_viewport_ids);

    [[nodiscard]] ViewportTextureView viewport_texture(ViewportId viewport_id) const;

private:
    VkDescriptorPool create_descriptor_pool();

    const VulkanDevice& device_;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    std::unique_ptr<ViewportTextureStore> viewport_textures_;
};

} // namespace quader

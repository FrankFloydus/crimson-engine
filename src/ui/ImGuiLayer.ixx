module;

#include <SDL3/SDL_events.h>
#include <imgui.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>

export module quader.ui.imgui_layer;

export import quader.platform.sdl_window;
export import quader.render.vulkan_device;
export import quader.render.vulkan_swapchain;
import quader.ui.vulkan_preview_texture;

export namespace quader {

class ImGuiLayer {
public:
    ImGuiLayer(const SdlWindow& window, const VulkanDevice& device, const VulkanSwapchain& swapchain);

    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    ~ImGuiLayer();

    void process_event(const SDL_Event& event);
    void begin_frame();
    void end_frame(VulkanSwapchain& swapchain);
    void upload_preview_texture(uint32_t width, uint32_t height, const uint8_t* rgba);

    [[nodiscard]] ImTextureID preview_texture_id() const;
    [[nodiscard]] bool has_preview_texture() const;

private:
    VkDescriptorPool create_descriptor_pool();

    const VulkanDevice& device_;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    std::unique_ptr<VulkanPreviewTexture> preview_texture_;
};

} // namespace quader

module;

#include <memory>

export module quader.app;

import quader.app.preview_controller;
import quader.platform.sdl_window;
import quader.render.debug_preview_renderer;
import quader.render.vulkan_device;
import quader.render.vulkan_swapchain;
import quader.ui.imgui_layer;

export namespace quader {

class Application {
public:
    Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    int run();

private:
    void draw_frame();

    SdlWindow window_;
    VulkanDevice vulkan_device_;
    VulkanSwapchain swapchain_;
    std::unique_ptr<IDebugPreviewRenderer> render_engine_;
    PreviewController preview_controller_;
    ImGuiLayer imgui_;
};

} // namespace quader

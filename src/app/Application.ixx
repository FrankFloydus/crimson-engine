module;

#include <memory>

export module quader.app;

import quader.app.editor_viewport;
import quader.platform.app_event_dispatcher;
import quader.platform.input;
import quader.platform.native_file_dialog;
import quader.platform.sdl_event_source;
import quader.platform.sdl_window;
import quader.render.scene_viewport_renderer;
import quader.render.vulkan_device;
import quader.render.vulkan_swapchain;
import quader.scene.scene;
import quader.ui.editor_dockspace;
import quader.ui.imgui_layer;

export namespace quader {

class Application {
public:
    Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    int run();

private:
    void draw_frame(float delta_seconds);
    void poll_events();
    void update_relative_mouse_mode();

    SdlWindow window_;
    NativeFileDialogService native_file_dialog_;
    SdlEventSource event_source_;
    AppEventDispatcher event_dispatcher_;
    Input input_;
    VulkanDevice vulkan_device_;
    VulkanSwapchain swapchain_;
    std::unique_ptr<ISceneViewportRenderer> render_engine_;
    Scene open_scene_;
    EditorViewport scene_viewport_;
    ImGuiLayer imgui_;
};

} // namespace quader

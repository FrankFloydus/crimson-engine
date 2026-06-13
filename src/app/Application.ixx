module;

#include <memory>

export module quader.app;

import quader.app.editor_viewport;
import quader.app.editor_command_stack;
import quader.app.editor_hover;
import quader.app.editor_selection;
import quader.app.scene_properties_panel_controller;
import quader.platform.app_event_dispatcher;
import quader.platform.input;
import quader.platform.native_file_dialog;
import quader.platform.sdl_event_source;
import quader.platform.sdl_platform_context;
import quader.platform.sdl_window;
import quader.render.viewport_renderer;
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
    void collect_retired_viewport_gpu_resources();
    void poll_events();
    void update_relative_mouse_mode();

    SdlPlatformContext sdl_platform_context_;
    SdlWindow window_;
    NativeFileDialogService native_file_dialog_;
    SdlEventSource event_source_;
    AppEventDispatcher event_dispatcher_;
    Input input_;
    VulkanDevice vulkan_device_;
    VulkanSwapchain swapchain_;
    std::unique_ptr<IViewportRenderer> render_engine_;
    Scene open_scene_;
    EditorSelectionModel selection_;
    EditorHoverModel hover_;
    EditorCommandStack commands_;
    ScenePropertiesPanelController scene_properties_;
    EditorViewport scene_viewport_;
    ImGuiLayer imgui_;
};

} // namespace quader

module;

#include <memory>

export module quader.app;

export import quader.app.user_settings_paths;
import quader.app.editor_viewport;
import quader.app.editor_command_stack;
import quader.app.editor_hover;
import quader.app.editor_selection;
import quader.app.render_driver_settings;
import quader.app.editor_runtime_host;
import quader.app.scene_properties_panel_controller;
import quader.platform.app_event_dispatcher;
import quader.platform.input;
import quader.scene.scene;
import quader.ui_shell.editor_shell;

export namespace quader {

class Application {
public:
    explicit Application(UserSettingsPaths user_settings_paths);

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    int run();

private:
    void draw_frame(float delta_seconds);
    [[nodiscard]] bool apply_editor_ui_result(const EditorUiFrameResult& result);
    [[nodiscard]] EditorUiFrameInput build_editor_ui_input();
    [[nodiscard]] bool apply_main_menu_result(const EditorMainMenuResult& result);
    void save_render_driver_selection(RenderDriverKind requested_driver);
    void collect_retired_viewport_gpu_resources();
    void poll_events();
    void update_relative_mouse_mode();

    UserSettingsPaths user_settings_paths_;
    RenderDriverSettingsStore render_driver_settings_;
    RenderDriverSelection render_driver_selection_;
    bool render_driver_restart_required_ = false;
    std::unique_ptr<IEditorRuntimeHost> runtime_host_;
    AppEventDispatcher event_dispatcher_;
    Input input_;
    Scene open_scene_;
    EditorSelectionModel selection_;
    EditorHoverModel hover_;
    EditorCommandStack commands_;
    ScenePropertiesPanelController scene_properties_;
    EditorViewport scene_viewport_;
};

} // namespace quader

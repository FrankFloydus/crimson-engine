module;

export module quader.platform.sdl_editor_host_services;

export import quader.platform.app_event_dispatcher;
export import quader.platform.file_dialog;
export import quader.platform.sdl_event_source;
export import quader.platform.sdl_window;
export import quader.ui_shell.editor_shell;

import quader.platform.native_file_dialog;
import quader.platform.sdl_platform_context;
import quader.ui.imgui_editor_shell;

export namespace quader {

class SdlEditorHostServices final {
public:
    explicit SdlEditorHostServices(const SdlWindowDescriptor& window_descriptor);

    SdlEditorHostServices(const SdlEditorHostServices&) = delete;
    SdlEditorHostServices& operator=(const SdlEditorHostServices&) = delete;

    ~SdlEditorHostServices() = default;

    [[nodiscard]] SdlWindow& window() noexcept;
    [[nodiscard]] const SdlWindow& window() const noexcept;
    [[nodiscard]] IEditorShell& editor_shell() noexcept;
    [[nodiscard]] IImageFileDialogService& image_file_dialog() noexcept;

    void poll_events(AppEventDispatcher& dispatcher, ISdlNativeEventSink& native_sink);
    void set_relative_mouse_mode(bool enabled);

private:
    SdlPlatformContext platform_;
    SdlWindow window_;
    NativeFileDialogService native_file_dialog_;
    SdlEventSource event_source_;
    ImGuiEditorShell editor_shell_;
};

} // namespace quader

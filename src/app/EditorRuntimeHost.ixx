module;

#include <memory>
#include <string>

export module quader.app.editor_runtime_host;

export import quader.app.editor_input_state;
export import quader.app.viewport_frame_presenter;
export import quader.platform.app_event_dispatcher;
export import quader.platform.file_dialog;
export import quader.render.driver_kind;
export import quader.render.engine_factory;
export import quader.render.status;
export import quader.render.viewport_renderer;
export import quader.ui_shell.editor_shell;

export namespace quader {

struct EditorRuntimeHostConfig {
    std::string imgui_ini_file;
};

class IEditorRuntimeHost {
public:
    virtual ~IEditorRuntimeHost() = default;

    [[nodiscard]] virtual RenderDriverKind driver_kind() const noexcept = 0;
    [[nodiscard]] virtual RendererStatus status() const = 0;
    [[nodiscard]] virtual IEditorShell& editor_shell() = 0;
    [[nodiscard]] virtual IViewportRenderer& viewport_renderer() = 0;
    [[nodiscard]] virtual IViewportFramePresenter& viewport_frame_presenter() = 0;
    [[nodiscard]] virtual IEditorInputState& editor_input_state() = 0;
    [[nodiscard]] virtual IImageFileDialogService& image_file_dialog() = 0;

    virtual void poll_events(AppEventDispatcher& dispatcher) = 0;
    virtual void set_relative_mouse_mode(bool enabled) = 0;
    virtual void begin_frame() = 0;
    virtual void end_frame() = 0;
    virtual void collect_retired_viewport_gpu_resources(bool viewport_render_requests_pending) = 0;
};

[[nodiscard]] std::unique_ptr<IEditorRuntimeHost> create_editor_runtime_host(
    RenderDriverSelection selection,
    EditorRuntimeHostConfig config,
    const IRenderEngineFactory& render_engine_factory);

} // namespace quader

module;

export module quader.ui_shell.editor_shell;

export import quader.ui_shell.editor_panel_contracts;
export import quader.ui_shell.editor_viewport_contracts;

export namespace quader {

struct EditorUiFrameInput {
    EditorMainMenuInput main_menu;
    ScenePropertiesPanelFrameInput scene_properties;
};

struct EditorUiFrameResult {
    EditorMainMenuResult main_menu;
    ScenePropertiesPanelFrameResult scene_properties;
};

class IEditorShell {
public:
    IEditorShell() = default;
    IEditorShell(const IEditorShell&) = delete;
    IEditorShell& operator=(const IEditorShell&) = delete;
    virtual ~IEditorShell() = default;

    [[nodiscard]] virtual EditorUiFrameResult draw_frame(const EditorUiFrameInput& input) = 0;
    [[nodiscard]] virtual ViewportHostPanelResult draw_viewport_host(const ViewportHostPanelInput& input) = 0;
};

} // namespace quader

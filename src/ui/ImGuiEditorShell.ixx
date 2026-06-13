module;

export module quader.ui.imgui_editor_shell;

export import quader.ui_shell.editor_shell;

import quader.ui.outliner_panel;

export namespace quader {

class ImGuiEditorShell final : public IEditorShell {
public:
    [[nodiscard]] EditorUiFrameResult draw_frame(const EditorUiFrameInput& input) override;
    [[nodiscard]] ViewportHostPanelResult draw_viewport_host(const ViewportHostPanelInput& input) override;

private:
    OutlinerPanel outliner_panel_;
};

} // namespace quader

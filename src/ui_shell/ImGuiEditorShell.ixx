module;

export module quader.ui_shell.imgui_editor_shell;

export import quader.ui_shell.editor_shell;

import quader.ui.outliner_panel;

export namespace quader {

class ImGuiEditorShell final : public IEditorShell {
public:
    [[nodiscard]] EditorUiFrameResult draw_frame(const EditorUiFrameInput& input) override;

private:
    OutlinerPanel outliner_panel_;
};

} // namespace quader

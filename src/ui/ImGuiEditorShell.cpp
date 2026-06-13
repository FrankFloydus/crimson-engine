module;

module quader.ui.imgui_editor_shell;

import quader.ui.editor_dockspace;
import quader.ui.inspector_menu;
import quader.ui.property_inspector_panel;
import quader.ui.viewport_host_panel;

namespace quader {

EditorUiFrameResult ImGuiEditorShell::draw_frame(const EditorUiFrameInput& input)
{
    EditorUiFrameResult result;

    render_editor_dockspace();
    result.main_menu = draw_editor_main_menu(input.main_menu);
    result.scene_properties.outliner = outliner_panel_.draw(input.scene_properties.outliner);
    result.scene_properties.property_inspector =
        draw_property_inspector_panel(input.scene_properties.property_inspector);

    return result;
}

ViewportHostPanelResult ImGuiEditorShell::draw_viewport_host(const ViewportHostPanelInput& input)
{
    return draw_viewport_host_panel(input);
}

} // namespace quader

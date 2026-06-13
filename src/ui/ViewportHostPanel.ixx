module;

export module quader.ui.viewport_host_panel;

export import quader.ui_shell.editor_viewport_contracts;

export namespace quader {

[[nodiscard]] ViewportHostPanelResult draw_viewport_host_panel(const ViewportHostPanelInput& input);

} // namespace quader

module;

export module quader.ui.viewport_surface;

export import quader.ui_shell.editor_viewport_contracts;

export namespace quader {

[[nodiscard]] ViewportSurfaceResult draw_viewport_surface(const ViewportSurfaceInput& input);

} // namespace quader

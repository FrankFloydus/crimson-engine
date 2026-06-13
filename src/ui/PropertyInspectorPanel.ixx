module;

#include <optional>
#include <string>
#include <string_view>

export module quader.ui.property_inspector_panel;

export import quader.ui_shell.editor_panel_contracts;

export namespace quader {

[[nodiscard]] PropertyInspectorPanelResult draw_property_inspector_panel(
    const PropertyInspectorPanelInput& input);

} // namespace quader

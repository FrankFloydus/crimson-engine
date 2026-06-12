module;

#include <optional>

export module quader.ui.material_panel;

export import quader.render.types;

export namespace quader {

struct MaterialPanelResult {
    bool settings_changed = false;
    std::optional<PbrMaterialTextureSlot> requested_texture_pick;
    std::optional<PbrMaterialTextureSlot> requested_texture_clear;
};

[[nodiscard]] MaterialPanelResult render_material_settings_panel(
    PbrMaterialSettings& settings,
    bool picker_busy);

} // namespace quader

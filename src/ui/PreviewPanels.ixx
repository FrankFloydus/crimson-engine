module;

#include <imgui.h>

#include <cstdint>

export module quader.ui.preview_panels;

export import quader.render.types;

export namespace quader {

void render_debug_preview_panel(ImTextureID texture_id, RenderExtent extent);
bool render_material_settings_panel(PbrMaterialSettings& settings);
bool render_directional_light_panel(DirectionalLightSettings& settings);

} // namespace quader

module;

#include <imgui.h>

#include <cstdint>
#include <string_view>

export module quader.ui.viewport_panels;

export import quader.render.types;

export namespace quader {

void render_scene_viewport_panel(std::string_view scene_title, ImTextureID texture_id, RenderExtent extent);
bool render_material_settings_panel(PbrMaterialSettings& settings);
bool render_directional_light_panel(DirectionalLightSettings& settings);

} // namespace quader

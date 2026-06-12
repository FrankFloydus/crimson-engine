module;

#include <imgui.h>

#include <cstdint>

export module quader.ui.viewport_panels;

export import quader.render.types;

export namespace quader {

struct ViewportImageBounds {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct ViewportPanelResult {
    RenderExtent requested_extent;
    ViewportImageBounds image_bounds;
    bool viewport_hovered = false;
    bool viewport_focused = false;
    bool viewport_active = false;
    bool viewport_clicked = false;
    bool image_visible = false;
    bool image_hovered = false;
    bool image_focused = false;
    bool image_active = false;
    bool image_clicked = false;
    bool image_dragging = false;
};

ViewportPanelResult render_scene_viewport_panel(ImTextureID texture_id, RenderExtent extent);
bool render_directional_light_panel(DirectionalLightSettings& settings);

} // namespace quader

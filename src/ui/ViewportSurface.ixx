module;

#include <imgui.h>

export module quader.ui.viewport_surface;

export import quader.render.viewport_types;

export namespace quader {

struct ViewportTextureView {
    ImTextureID texture_id {};
    RenderExtent extent;
    bool has_pixels = false;
};

struct ViewportImageBounds {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct ViewportPerformanceOverlay {
    bool enabled = false;
    float frame_delta_seconds = 0.0f;
    bool render_request_pending = false;
    bool dirty = false;
};

struct ViewportSurfaceInput {
    ViewportId id;
    ViewportTextureView texture;
    ViewportPerformanceOverlay performance;
    const char* empty_text = "Waiting for viewport pixels.";
    bool active = false;
};

struct ViewportSurfaceResult {
    ViewportId id;
    RenderExtent requested_extent;
    ViewportImageBounds image_bounds;
    bool visible = false;
    bool hovered = false;
    bool focused = false;
    bool active = false;
    bool clicked = false;
    bool dragging = false;
};

[[nodiscard]] ViewportSurfaceResult draw_viewport_surface(const ViewportSurfaceInput& input);

} // namespace quader

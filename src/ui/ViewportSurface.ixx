module;

#include <imgui.h>

#include <optional>

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

struct ViewportSurfaceClick {
    float local_x = 0.0f;
    float local_y = 0.0f;
};

struct ViewportSurfacePointer {
    float local_x = 0.0f;
    float local_y = 0.0f;
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
    bool left_clicked = false;
    bool left_down = false;
    bool left_released = false;
    bool dragging = false;
    std::optional<ViewportSurfacePointer> pointer;
    std::optional<ViewportSurfaceClick> click;
};

[[nodiscard]] ViewportSurfaceResult draw_viewport_surface(const ViewportSurfaceInput& input);

} // namespace quader

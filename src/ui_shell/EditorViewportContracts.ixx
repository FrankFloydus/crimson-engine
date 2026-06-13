module;

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

export module quader.ui_shell.editor_viewport_contracts;

export import quader.render.capabilities;
export import quader.render.types;
export import quader.render.viewport_types;
export import quader.ui_shell.editor_viewport_layout;

export namespace quader {

enum class ViewportTextureUvOrigin {
    top_left,
    bottom_left,
};

struct ViewportTextureView {
    uint64_t texture_id = 0;
    RenderExtent extent;
    ViewportTextureUvOrigin uv_origin = ViewportTextureUvOrigin::top_left;
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

struct ViewportPaneInput {
    ViewportId id;
    std::string_view title;
    ViewportTextureView texture;
    ViewportPerformanceOverlay performance;
    ViewportCameraPreset camera_preset = ViewportCameraPreset::perspective;
    ViewportCameraPreset default_camera_preset = ViewportCameraPreset::perspective;
    bool active = false;
    bool camera_dropdown_enabled = true;
};

struct ViewportPaneResult {
    ViewportId id;
    ViewportSurfaceResult surface;
    std::optional<ViewportCameraPreset> requested_camera_preset;
    bool requested_maximize_toggle = false;
};

struct ViewportHostPanelInput {
    ViewportLayoutState layout;
    EnvironmentLightingSettings environment;
    PostProcessingSettings post_processing;
    RendererCapabilities renderer;
    std::span<const ViewportPaneInput> panes;
};

struct ViewportHostPanelResult {
    std::vector<ViewportPaneResult> panes;
    std::optional<ViewportId> active_viewport;
    ViewportLayoutState layout;
    EnvironmentLightingSettings environment;
    PostProcessingSettings post_processing;
    bool layout_changed = false;
    bool environment_changed = false;
    bool post_processing_changed = false;
};

} // namespace quader

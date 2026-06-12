module;

#include <optional>
#include <span>
#include <string_view>
#include <vector>

export module quader.ui.viewport_host_panel;

export import quader.app.viewport_layout;
export import quader.render.viewport_types;
export import quader.ui.viewport_surface;

export namespace quader {

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

struct ViewportHostPanelResult {
    std::vector<ViewportPaneResult> panes;
    std::optional<ViewportId> active_viewport;
    bool layout_changed = false;
    bool environment_changed = false;
};

[[nodiscard]] ViewportHostPanelResult draw_viewport_host_panel(
    ViewportLayoutState& layout_state,
    EnvironmentLightingSettings& environment_settings,
    std::span<const ViewportPaneInput> panes);

} // namespace quader

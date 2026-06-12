module;

#include <optional>
#include <string>
#include <string_view>

export module quader.app.viewport_instance;

export import quader.app.editor_camera;
export import quader.platform.input;
export import quader.render.viewport_types;
export import quader.scene.scene;

export namespace quader {

enum class ViewportSubjectKind {
    scene,
    preview,
};

enum class ViewportNavigationKind {
    scene_camera,
    orbit_preview,
    none,
};

struct ViewportNavigationSnapshot {
    bool window_has_keyboard_focus = true;
    bool window_has_mouse_focus = true;
    bool text_input_active = false;
    bool hovered = false;
    bool right_mouse_pressed = false;
    bool right_mouse_released = false;
    bool right_mouse_down = false;
    bool move_forward = false;
    bool move_backward = false;
    bool move_left = false;
    bool move_right = false;
    bool move_up = false;
    bool move_down = false;
    bool boost = false;
    MouseDelta mouse_delta;
    MouseDelta mouse_wheel_delta;
};

struct ViewportFrameAcceptance {
    bool accepted = false;
    bool stale = false;
};

class ViewportInstance {
public:
    [[nodiscard]] static ViewportInstance scene_viewport(
        ViewportId id,
        std::string title,
        ViewportCameraPreset preset);
    [[nodiscard]] static ViewportInstance non_interactive_scene_viewport(
        ViewportId id,
        std::string title,
        ViewportCameraPreset preset);
    [[nodiscard]] static ViewportInstance preview_viewport(ViewportId id, std::string title);

    [[nodiscard]] ViewportId id() const noexcept;
    [[nodiscard]] ViewportGeneration generation() const noexcept;
    [[nodiscard]] std::string_view title() const noexcept;
    [[nodiscard]] ViewportSubjectKind subject_kind() const noexcept;
    [[nodiscard]] ViewportCameraPreset camera_preset() const noexcept;
    [[nodiscard]] ViewportCameraPreset default_camera_preset() const noexcept;
    [[nodiscard]] RenderExtent requested_extent() const noexcept;
    [[nodiscard]] RenderExtent displayed_extent() const noexcept;
    [[nodiscard]] bool is_dirty() const noexcept;
    [[nodiscard]] bool request_pending() const noexcept;
    [[nodiscard]] bool can_capture_navigation() const noexcept;
    [[nodiscard]] bool should_start_navigation_mouse_lock(const ViewportNavigationSnapshot& input) const noexcept;
    [[nodiscard]] bool should_keep_navigation_mouse_lock(const ViewportNavigationSnapshot& input) const noexcept;

    void set_camera_preset(ViewportCameraPreset preset);
    void set_requested_extent(RenderExtent extent);
    void mark_dirty();

    [[nodiscard]] bool update_navigation(
        const ViewportNavigationSnapshot& input,
        bool navigation_mouse_locked,
        float delta_seconds);
    [[nodiscard]] std::optional<ViewportRenderRequest> try_begin_render_request(
        const Scene& scene,
        const PbrMaterialSettings& preview_material,
        DirectionalLightSettings preview_light);
    [[nodiscard]] ViewportFrameAcceptance accept_frame(const ViewportRenderFrame& frame);
    void fail_pending_request();

private:
    ViewportInstance(
        ViewportId id,
        std::string title,
        ViewportSubjectKind subject_kind,
        ViewportNavigationKind navigation_kind,
        ViewportCameraPreset preset);

    [[nodiscard]] ViewportCamera current_camera() const;
    [[nodiscard]] ViewportProjection current_projection() const;
    [[nodiscard]] bool update_scene_navigation(
        const ViewportNavigationSnapshot& input,
        bool navigation_mouse_locked,
        float delta_seconds);
    [[nodiscard]] bool update_preview_orbit_navigation(
        const ViewportNavigationSnapshot& input,
        bool navigation_mouse_locked);

    ViewportId id_;
    ViewportGeneration generation_ { 1 };
    std::string title_;
    ViewportSubjectKind subject_kind_ = ViewportSubjectKind::scene;
    ViewportNavigationKind navigation_kind_ = ViewportNavigationKind::scene_camera;
    ViewportCameraPreset camera_preset_ = ViewportCameraPreset::perspective;
    ViewportCameraPreset default_camera_preset_ = ViewportCameraPreset::perspective;
    RenderExtent requested_extent_ { 720, 420 };
    RenderExtent displayed_extent_;
    EditorCamera perspective_camera_;
    Vec3 orthographic_target_;
    float orthographic_size_ = 5.0f;
    float orbit_yaw_degrees_ = -35.0f;
    float orbit_pitch_degrees_ = 20.0f;
    float orbit_distance_ = 4.0f;
    bool dirty_ = true;
    bool request_pending_ = false;
    bool dirty_after_pending_ = false;
    uint64_t next_request_serial_ = 1;
    uint64_t pending_request_serial_ = 0;
};

} // namespace quader

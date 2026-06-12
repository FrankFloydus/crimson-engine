module;

#include <optional>
#include <vector>

export module quader.app.editor_viewport;

import quader.app.editor_selection;
import quader.app.editor_input;
import quader.app.transform_gizmo;
import quader.app.viewport_instance;
import quader.app.viewport_layout;
import quader.platform.input;
import quader.render.viewport_renderer;
import quader.scene.scene;
import quader.ui.imgui_layer;
import quader.ui.viewport_surface;

export namespace quader {

struct EditorViewportFrameResult {
    bool scene_changed = false;
};

class EditorViewport {
public:
    ~EditorViewport();

    [[nodiscard]] EditorViewportFrameResult render_frame(
        IViewportRenderer& renderer,
        ImGuiLayer& imgui,
        Scene& scene,
        const Input& input,
        EditorSelectionModel& selection,
        float delta_seconds);

    [[nodiscard]] bool wants_relative_mouse_mode() const noexcept;
    void mark_render_state_dirty();

private:
    void ensure_viewports(IViewportRenderer& renderer);
    void release_viewports();
    void append_owned_viewport_ids(std::vector<ViewportId>& viewport_ids) const;

    void render_scene_viewport_host(
        ImGuiLayer& imgui,
        Scene& scene,
        const Input& input,
        EditorSelectionModel& selection,
        float delta_seconds,
        std::vector<ViewportId>& visible_viewport_ids,
        bool& hover_payload_updated,
        EditorViewportFrameResult& frame_result);
    void update_viewport_from_surface(
        ViewportInstance& viewport,
        Scene& scene,
        const ViewportSurfaceResult& surface,
        const Input& input,
        EditorSelectionModel& selection,
        float delta_seconds,
        std::vector<ViewportId>& visible_viewport_ids,
        bool& hover_payload_updated,
        EditorViewportFrameResult& frame_result);
    void release_navigation_capture_if_hidden(const std::vector<ViewportId>& visible_viewport_ids);

    void request_visible_viewport_frames(
        IViewportRenderer& renderer,
        const Scene& scene,
        const EditorSelectionModel& selection,
        const std::vector<ViewportId>& visible_viewport_ids);
    void upload_completed_viewport_frames(IViewportRenderer& renderer, ImGuiLayer& imgui);

    [[nodiscard]] ViewportInstance* find_viewport(ViewportId viewport_id);
    [[nodiscard]] const ViewportInstance* find_viewport(ViewportId viewport_id) const;
    [[nodiscard]] ViewportPerformanceOverlay performance_overlay_for(
        const ViewportInstance& viewport,
        float delta_seconds) const;
    struct ViewportPickResult {
        std::optional<SceneEntityId> entity_id;
        std::optional<DebugDrawPickPayload> debug_payload;
    };

    [[nodiscard]] ViewportPickResult pick_from_surface(
        const Scene& scene,
        const ViewportInstance& viewport,
        const EditorSelectionModel& selection,
        const ViewportSurfaceResult& surface) const;
    void mark_scene_viewports_dirty();
    void mark_all_viewports_dirty();

    IViewportRenderer* renderer_ = nullptr;
    std::vector<ViewportInstance> scene_viewports_;
    ViewportLayoutState layout_state_;
    EditorInputResolver editor_input_;
    TransformGizmoController transform_gizmo_;
    std::optional<ViewportId> active_viewport_;
    std::optional<ViewportId> mouse_locked_viewport_;
};

} // namespace quader

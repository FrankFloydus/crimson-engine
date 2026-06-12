module;

#include <optional>
#include <vector>

export module quader.app.editor_viewport;

import quader.app.viewport_instance;
import quader.app.viewport_layout;
import quader.platform.input;
import quader.render.viewport_renderer;
import quader.scene.scene;
import quader.ui.imgui_layer;
import quader.ui.viewport_surface;

export namespace quader {

class EditorViewport {
public:
    ~EditorViewport();

    void render_frame(
        IViewportRenderer& renderer,
        ImGuiLayer& imgui,
        const Scene& scene,
        const Input& input,
        float delta_seconds);

    [[nodiscard]] bool wants_relative_mouse_mode() const noexcept;
    void mark_render_state_dirty();

private:
    void ensure_viewports(IViewportRenderer& renderer);
    void release_viewports();
    void append_owned_viewport_ids(std::vector<ViewportId>& viewport_ids) const;

    void render_scene_viewport_host(
        ImGuiLayer& imgui,
        const Input& input,
        float delta_seconds,
        std::vector<ViewportId>& visible_viewport_ids);
    void render_preview_viewport_panel(
        ImGuiLayer& imgui,
        const Input& input,
        float delta_seconds,
        std::vector<ViewportId>& visible_viewport_ids);
    void update_viewport_from_surface(
        ViewportInstance& viewport,
        const ViewportSurfaceResult& surface,
        const Input& input,
        float delta_seconds,
        std::vector<ViewportId>& visible_viewport_ids);
    void release_navigation_capture_if_hidden(const std::vector<ViewportId>& visible_viewport_ids);

    void request_visible_viewport_frames(
        IViewportRenderer& renderer,
        const Scene& scene,
        const std::vector<ViewportId>& visible_viewport_ids);
    void upload_completed_viewport_frames(IViewportRenderer& renderer, ImGuiLayer& imgui);

    [[nodiscard]] ViewportInstance* find_viewport(ViewportId viewport_id);
    [[nodiscard]] const ViewportInstance* find_viewport(ViewportId viewport_id) const;
    [[nodiscard]] ViewportPerformanceOverlay performance_overlay_for(
        const ViewportInstance& viewport,
        float delta_seconds) const;
    [[nodiscard]] PbrMaterialSettings preview_material_from_scene(const Scene& scene) const;
    [[nodiscard]] DirectionalLightSettings preview_light_from_scene(const Scene& scene) const;
    void mark_scene_viewports_dirty();
    void mark_all_viewports_dirty();

    IViewportRenderer* renderer_ = nullptr;
    std::vector<ViewportInstance> scene_viewports_;
    std::optional<ViewportInstance> preview_viewport_;
    ViewportLayoutState layout_state_;
    std::optional<ViewportId> active_viewport_;
    std::optional<ViewportId> mouse_locked_viewport_;
};

} // namespace quader

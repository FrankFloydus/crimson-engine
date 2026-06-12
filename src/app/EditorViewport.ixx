module;

export module quader.app.editor_viewport;

import quader.app.editor_camera;
import quader.platform.input;
import quader.render.scene_viewport_renderer;
import quader.scene.scene;
import quader.ui.imgui_layer;

export namespace quader {

class EditorViewport {
public:
    void render_frame(
        ISceneViewportRenderer& renderer,
        ImGuiLayer& imgui,
        Scene& scene,
        const Input& input,
        float delta_seconds);

private:
    void update_editor_camera(const Input& input, float delta_seconds);
    void request_viewport_frame_if_needed(ISceneViewportRenderer& renderer, const Scene& scene);
    void upload_completed_viewport_frame_if_available(ISceneViewportRenderer& renderer, ImGuiLayer& imgui);

    RenderExtent viewport_extent_ { 720, 420 };
    RenderExtent displayed_extent_;
    EditorCamera editor_camera_;
    bool viewport_dirty_ = true;
    bool viewport_request_pending_ = false;
};

} // namespace quader

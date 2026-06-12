module;

#include <optional>

export module quader.app.editor_viewport;

import quader.app.editor_camera;
import quader.platform.input;
import quader.platform.native_file_dialog;
import quader.render.types;
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
        NativeFileDialogService& native_file_dialog,
        const Input& input,
        float delta_seconds);

    [[nodiscard]] bool wants_relative_mouse_mode() const noexcept;

private:
    void update_editor_camera(const Input& input, float delta_seconds);
    void render_material_panel(Scene& scene, NativeFileDialogService& native_file_dialog);
    void consume_texture_picker_result_if_available(Scene& scene, NativeFileDialogService& native_file_dialog);
    void request_viewport_frame_if_needed(ISceneViewportRenderer& renderer, const Scene& scene);
    void upload_completed_viewport_frame_if_available(ISceneViewportRenderer& renderer, ImGuiLayer& imgui);

    RenderExtent viewport_extent_ { 720, 420 };
    RenderExtent displayed_extent_;
    EditorCamera editor_camera_;
    std::optional<PbrMaterialTextureSlot> pending_material_texture_slot_;
    bool viewport_dirty_ = true;
    bool viewport_request_pending_ = false;
    bool viewport_keyboard_focus_ = false;
    bool viewport_mouse_navigation_active_ = false;
};

} // namespace quader

module;

export module quader.app.editor_camera;

import quader.render.types;

export namespace quader {

struct EditorCameraLookDelta {
    float x = 0.0f;
    float y = 0.0f;
};

struct EditorCameraNavigationInput {
    bool move_forward = false;
    bool move_backward = false;
    bool move_left = false;
    bool move_right = false;
    bool boost = false;
    bool look_active = false;
    EditorCameraLookDelta look_delta {};
};

class EditorCamera {
public:
    EditorCamera();
    explicit EditorCamera(RenderCamera camera);

    void set_camera(RenderCamera camera);

    [[nodiscard]] RenderCamera render_camera() const;
    [[nodiscard]] bool update_from_navigation_input(
        const EditorCameraNavigationInput& navigation_input,
        float delta_seconds);

private:
    RenderCamera camera_;
    float movement_speed_ = 3.4f;
    float movement_boost_multiplier_ = 3.5f;
    float look_sensitivity_degrees_per_pixel_ = 0.12f;
};

} // namespace quader

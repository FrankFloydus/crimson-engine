module;

export module quader.app.editor_camera;

import quader.platform.input;
import quader.render.types;

export namespace quader {

class EditorCamera {
public:
    EditorCamera();
    explicit EditorCamera(RenderCamera camera);

    void set_camera(RenderCamera camera);

    [[nodiscard]] RenderCamera render_camera() const;
    [[nodiscard]] bool update_from_input(const Input& input, float delta_seconds);

private:
    RenderCamera camera_;
    float movement_speed_ = 3.4f;
    float movement_boost_multiplier_ = 3.5f;
    float look_sensitivity_degrees_per_pixel_ = 0.12f;
};

} // namespace quader

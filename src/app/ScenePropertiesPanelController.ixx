module;

#include <optional>

export module quader.app.scene_properties_panel_controller;

import quader.platform.native_file_dialog;
import quader.render.types;
import quader.scene.scene;

export namespace quader {

class ScenePropertiesPanelController {
public:
    [[nodiscard]] bool render_frame(Scene& scene, NativeFileDialogService& native_file_dialog);

private:
    [[nodiscard]] bool render_material_panel(Scene& scene, NativeFileDialogService& native_file_dialog);
    [[nodiscard]] bool render_light_panel(Scene& scene);
    [[nodiscard]] bool consume_texture_picker_result_if_available(
        Scene& scene,
        NativeFileDialogService& native_file_dialog);

    std::optional<PbrMaterialTextureSlot> pending_material_texture_slot_;
};

} // namespace quader

module;

#include <optional>

export module quader.app.scene_properties_panel_controller;

import quader.platform.native_file_dialog;
import quader.render.types;
import quader.scene.scene;
import quader.ui.outliner_panel;

export namespace quader {

class ScenePropertiesPanelController {
public:
    void select_entity(SceneEntityId entity_id);
    [[nodiscard]] bool render_frame(Scene& scene, NativeFileDialogService& native_file_dialog);

private:
    struct PendingMaterialTexturePick {
        SceneEntityId entity_id;
        PbrMaterialTextureSlot slot = PbrMaterialTextureSlot::albedo;
    };

    [[nodiscard]] bool render_create_menu(Scene& scene);
    void render_outliner(const Scene& scene);
    [[nodiscard]] bool render_property_inspector(Scene& scene, NativeFileDialogService& native_file_dialog);
    [[nodiscard]] bool consume_texture_picker_result_if_available(
        Scene& scene,
        NativeFileDialogService& native_file_dialog);

    std::optional<SceneEntityId> selected_entity_;
    std::optional<PendingMaterialTexturePick> pending_material_texture_pick_;
    OutlinerPanel outliner_panel_;
};

} // namespace quader

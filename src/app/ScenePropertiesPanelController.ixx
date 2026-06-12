module;

#include <optional>

export module quader.app.scene_properties_panel_controller;

import quader.app.editor_selection;
import quader.platform.native_file_dialog;
import quader.render.types;
import quader.scene.scene;
import quader.ui.outliner_panel;

export namespace quader {

class ScenePropertiesPanelController {
public:
    [[nodiscard]] bool render_frame(
        Scene& scene,
        NativeFileDialogService& native_file_dialog,
        EditorSelectionModel& selection);

private:
    struct PendingMaterialTexturePick {
        SceneEntityId entity_id;
        PbrMaterialTextureSlot slot = PbrMaterialTextureSlot::albedo;
    };

    [[nodiscard]] bool render_create_menu(Scene& scene, EditorSelectionModel& selection);
    void render_outliner(const Scene& scene, EditorSelectionModel& selection);
    [[nodiscard]] bool render_property_inspector(
        Scene& scene,
        NativeFileDialogService& native_file_dialog,
        EditorSelectionModel& selection);
    [[nodiscard]] bool consume_texture_picker_result_if_available(
        Scene& scene,
        NativeFileDialogService& native_file_dialog);

    std::optional<PendingMaterialTexturePick> pending_material_texture_pick_;
    OutlinerPanel outliner_panel_;
};

} // namespace quader

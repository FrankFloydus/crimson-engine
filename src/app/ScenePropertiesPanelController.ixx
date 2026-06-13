module;

#include <optional>
#include <vector>

export module quader.app.scene_properties_panel_controller;

import quader.app.editor_selection;
import quader.platform.native_file_dialog;
import quader.render.types;
import quader.scene.scene;
import quader.ui_shell.editor_panel_contracts;

export namespace quader {

class ScenePropertiesPanelController {
public:
    [[nodiscard]] bool begin_frame(
        Scene& scene,
        NativeFileDialogService& native_file_dialog,
        EditorSelectionModel& selection);
    [[nodiscard]] ScenePropertiesPanelFrameInput build_frame_input(
        const Scene& scene,
        const NativeFileDialogService& native_file_dialog,
        const EditorSelectionModel& selection);
    [[nodiscard]] bool apply_frame_result(
        const ScenePropertiesPanelFrameResult& result,
        Scene& scene,
        NativeFileDialogService& native_file_dialog,
        EditorSelectionModel& selection);
    [[nodiscard]] bool apply_create_request(
        CreateSceneItemKind create_kind,
        Scene& scene,
        EditorSelectionModel& selection);

private:
    struct PendingMaterialTexturePick {
        SceneEntityId entity_id;
        PbrMaterialTextureSlot slot = PbrMaterialTextureSlot::albedo;
    };

    void rebuild_outliner_rows(const Scene& scene);
    [[nodiscard]] PropertyInspectorPanelInput make_property_inspector_input(
        const Scene& scene,
        const NativeFileDialogService& native_file_dialog,
        const EditorSelectionModel& selection) const;
    [[nodiscard]] bool apply_outliner_result(
        const OutlinerPanelResult& result,
        EditorSelectionModel& selection);
    [[nodiscard]] bool apply_property_inspector_result(
        const PropertyInspectorPanelResult& result,
        Scene& scene,
        NativeFileDialogService& native_file_dialog);
    [[nodiscard]] bool consume_texture_picker_result_if_available(
        Scene& scene,
        NativeFileDialogService& native_file_dialog);

    std::optional<PendingMaterialTexturePick> pending_material_texture_pick_;
    std::vector<SceneOutlinerRow> outliner_rows_;
};

} // namespace quader

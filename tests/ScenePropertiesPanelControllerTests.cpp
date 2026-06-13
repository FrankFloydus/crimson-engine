#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <utility>

import quader.app.editor_selection;
import quader.app.scene_properties_panel_controller;
import quader.platform.file_dialog;
import quader.render.types;
import quader.scene.scene;
import quader.ui_shell.editor_panel_contracts;

namespace {

class FakeImageFileDialogService final : public quader::IImageFileDialogService {
public:
    void request_image_file() override
    {
        ++request_count_;
        pending_request_ = true;
    }

    [[nodiscard]] bool has_pending_request() const override
    {
        return pending_request_;
    }

    [[nodiscard]] std::optional<quader::ImageFileDialogResult> try_consume_result() override
    {
        if (!result_.has_value()) {
            return std::nullopt;
        }

        pending_request_ = false;
        std::optional<quader::ImageFileDialogResult> consumed = std::move(result_);
        result_.reset();
        return consumed;
    }

    void complete_with_selected_path(std::string path)
    {
        result_ = quader::ImageFileDialogResult {
            .status = quader::ImageFileDialogStatus::selected,
            .path = std::move(path),
        };
    }

    void complete_with_cancel()
    {
        result_ = quader::ImageFileDialogResult {
            .status = quader::ImageFileDialogStatus::canceled,
        };
    }

    [[nodiscard]] int request_count() const
    {
        return request_count_;
    }

private:
    int request_count_ = 0;
    bool pending_request_ = false;
    std::optional<quader::ImageFileDialogResult> result_;
};

[[nodiscard]] quader::ScenePropertiesPanelFrameResult request_texture_pick(
    quader::SceneEntityId entity_id,
    quader::PbrMaterialTextureSlot slot)
{
    return quader::ScenePropertiesPanelFrameResult {
        .property_inspector = quader::PropertyInspectorPanelResult {
            .inspected_entity = entity_id,
            .requested_texture_pick = slot,
        },
    };
}

[[nodiscard]] std::string albedo_texture_source(const quader::Scene& scene, quader::SceneEntityId entity_id)
{
    const quader::MaterialComponent* material = scene.material(entity_id);
    if (material == nullptr) {
        return {};
    }

    return material->material.albedo_texture.source_path;
}

} // namespace

TEST(ScenePropertiesPanelController, TexturePickRequestStartsDialogWhenIdle)
{
    quader::Scene scene;
    const quader::SceneEntityId cube = scene.create_cube();
    quader::EditorSelectionModel selection;
    ASSERT_TRUE(selection.select_entity(cube));
    FakeImageFileDialogService dialog;
    quader::ScenePropertiesPanelController controller;

    const bool render_state_changed = controller.apply_frame_result(
        request_texture_pick(cube, quader::PbrMaterialTextureSlot::albedo),
        scene,
        dialog,
        selection);

    EXPECT_FALSE(render_state_changed);
    EXPECT_TRUE(dialog.has_pending_request());
    EXPECT_EQ(1, dialog.request_count());
    EXPECT_TRUE(albedo_texture_source(scene, cube).empty());
}

TEST(ScenePropertiesPanelController, FrameInputReportsBusyWhileDialogIsPending)
{
    quader::Scene scene;
    const quader::SceneEntityId cube = scene.create_cube();
    quader::EditorSelectionModel selection;
    ASSERT_TRUE(selection.select_entity(cube));
    FakeImageFileDialogService dialog;
    quader::ScenePropertiesPanelController controller;
    ASSERT_FALSE(controller.apply_frame_result(
        request_texture_pick(cube, quader::PbrMaterialTextureSlot::albedo),
        scene,
        dialog,
        selection));

    const quader::ScenePropertiesPanelFrameInput input =
        controller.build_frame_input(scene, dialog, selection, quader::default_renderer_capabilities());

    EXPECT_TRUE(input.property_inspector.material_texture_picker_busy);
    ASSERT_TRUE(input.property_inspector.entity.has_value());
    EXPECT_EQ(cube, input.property_inspector.entity->id);
}

TEST(ScenePropertiesPanelController, SelectedDialogPathUpdatesPendingMaterialTexture)
{
    quader::Scene scene;
    const quader::SceneEntityId cube = scene.create_cube();
    quader::EditorSelectionModel selection;
    ASSERT_TRUE(selection.select_entity(cube));
    FakeImageFileDialogService dialog;
    quader::ScenePropertiesPanelController controller;
    ASSERT_FALSE(controller.apply_frame_result(
        request_texture_pick(cube, quader::PbrMaterialTextureSlot::albedo),
        scene,
        dialog,
        selection));
    dialog.complete_with_selected_path("textures/brick_albedo.png");

    const bool render_state_changed = controller.begin_frame(scene, dialog, selection);

    EXPECT_TRUE(render_state_changed);
    EXPECT_FALSE(dialog.has_pending_request());
    EXPECT_EQ("textures/brick_albedo.png", albedo_texture_source(scene, cube));
}

TEST(ScenePropertiesPanelController, SelectedDialogPathDoesNotChangeRenderStateWhenEntityHasNoMaterial)
{
    quader::Scene scene;
    const quader::SceneEntityId light = scene.create_directional_light();
    quader::EditorSelectionModel selection;
    ASSERT_TRUE(selection.select_entity(light));
    FakeImageFileDialogService dialog;
    quader::ScenePropertiesPanelController controller;
    ASSERT_FALSE(controller.apply_frame_result(
        request_texture_pick(light, quader::PbrMaterialTextureSlot::albedo),
        scene,
        dialog,
        selection));
    dialog.complete_with_selected_path("textures/light_albedo.png");

    const bool render_state_changed = controller.begin_frame(scene, dialog, selection);

    EXPECT_FALSE(render_state_changed);
    EXPECT_TRUE(selection.has_selection());
    EXPECT_FALSE(dialog.has_pending_request());
}

TEST(ScenePropertiesPanelController, CanceledDialogDoesNotChangeMaterialTexture)
{
    quader::Scene scene;
    const quader::SceneEntityId cube = scene.create_cube();
    quader::EditorSelectionModel selection;
    ASSERT_TRUE(selection.select_entity(cube));
    FakeImageFileDialogService dialog;
    quader::ScenePropertiesPanelController controller;
    ASSERT_FALSE(controller.apply_frame_result(
        request_texture_pick(cube, quader::PbrMaterialTextureSlot::albedo),
        scene,
        dialog,
        selection));
    dialog.complete_with_cancel();

    const bool render_state_changed = controller.begin_frame(scene, dialog, selection);

    EXPECT_FALSE(render_state_changed);
    EXPECT_FALSE(dialog.has_pending_request());
    EXPECT_TRUE(albedo_texture_source(scene, cube).empty());
}

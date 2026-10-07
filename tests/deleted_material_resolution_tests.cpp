////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "assets/default_material_catalog.hpp"
#include "editor/material/material_presentation_service.hpp"
#include "editor/render/standard_material_render_snapshot_adapter.hpp"
#include "editor/session/project_session.hpp"
#include "io/project/qmat_codec.hpp"
#include "support/project_material_fixture.hpp"

#include <gtest/gtest.h>
#include <quader/modeling/neutral/polygon/document.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace quader {
namespace {

/// Recreates old persisted identities while exercising their public consumers.
class DeletedMaterialResolutionTests : public ::testing::Test {
protected:
  void SetUp() override {
    root_ = std::filesystem::temp_directory_path() /
            ("quader-deleted-material-resolution-" +
             std::to_string(std::chrono::steady_clock::now()
                                .time_since_epoch().count()));
    ASSERT_TRUE(std::filesystem::create_directory(root_));
    project_.set_resource_root(std::filesystem::path(QUADER_SOURCE_DIR) /
                               "resources/modeling_assets");
    const auto created = editor_.project().create(root_);
    ASSERT_TRUE(created.ok()) << created.error_message();
    const auto manifest_path = project_.manifest_path();
    ASSERT_TRUE(editor_.project().close().ok());
    auto workspace = ProjectManifestStore::open(manifest_path);
    ASSERT_TRUE(workspace.ok()) << workspace.error().message;
    const std::array deleted{deleted_a_, deleted_b_};
    ASSERT_TRUE(workspace.value().manifest.mark_materials_deleted(deleted).ok());
    ASSERT_TRUE(ProjectManifestStore::save_atomic(workspace.value()).ok());
    auto live_path = ProjectRelativePath::parse_portable("Materials/live.qmat");
    ASSERT_TRUE(live_path.ok());
    ASSERT_TRUE(QmatCodec::write_atomic(
        workspace.value().file_system, live_path.value(),
        ProjectMaterialDocument{
            .id = live_, .name = "Surviving material",
            .kind = ProjectMaterialKind::Standard,
            .material_version = kStandardMaterialVersion,
            .payload = ProjectStandardMaterialDefinition{
                .base_color = {0.2f, 0.3f, 0.9f}}}).ok());
    const auto opened = editor_.project().open(manifest_path);
    ASSERT_TRUE(opened.ok()) << opened.error_message();
    if (project_.active_scene() == nullptr) {
      ASSERT_TRUE(editor_.scene_documents().new_scene().ok());
    }
  }

  void TearDown() override {
    (void)project_.close(true);
    std::error_code ignored;
    std::filesystem::remove_all(root_, ignored);
  }

  QuaderApiResult<QuaderModelingObjectRef> create_box() {
    return editor_.modeling().create_box(
        {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        "Historical material bindings");
  }

  const MaterialComponent *binding(EntityId entity) const {
    return project_.active_scene()->session->scene().material(entity);
  }

  const quader_poly::Document *polygon(EntityId entity) const {
    const auto &scene = *project_.active_scene()->session;
    const MeshComponent *mesh = scene.scene().mesh(entity);
    return mesh != nullptr
               ? scene.assets().mesh(mesh->mesh)->authored_polygon_document.get()
               : nullptr;
  }

  void expect_only_default(QuaderMaterialUsageScope scope) {
    const auto used = editor_.materials().used_materials(scope);
    ASSERT_TRUE(used.ok()) << used.error_message();
    ASSERT_EQ(used.value().size(), 1u);
    EXPECT_EQ(used.value().front(), QuaderMaterialReference::project(default_));
  }

  std::filesystem::path root_;
  ProjectSession project_;
  QuaderEditorApi editor_{project_};
  ProjectAssetId deleted_a_ = ProjectAssetId::generate();
  ProjectAssetId deleted_b_ = ProjectAssetId::generate();
  ProjectAssetId live_ = ProjectAssetId::generate();
  ProjectAssetId default_ = *ProjectAssetId::parse(kDefaultProjectMaterialId);
};

TEST_F(DeletedMaterialResolutionTests,
       RenderAndPreviewResolveDeletedIdentityToSavedDefault) {
  ProjectSessionRenderMaterialResolver resolver(project_);
  const auto expected = resolver.resolve(default_);
  const auto resolved = resolver.resolve(deleted_a_);
  ASSERT_TRUE(expected.has_value());
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(resolved->evaluation.project_material_id, default_);
  EXPECT_EQ(resolved->evaluation.identity_revision,
            expected->evaluation.identity_revision);
  EXPECT_EQ(resolved->evaluation.content_revision,
            expected->evaluation.content_revision);
  ASSERT_NE(resolved->evaluation.standard_material(), nullptr);
  EXPECT_EQ(*resolved->evaluation.standard_material(),
            *expected->evaluation.standard_material());
  EXPECT_EQ(resolved->catalog_revision, expected->catalog_revision);
  const auto default_preview = project_.material_preview(default_);
  const auto deleted_preview = project_.material_preview(deleted_a_);
  ASSERT_TRUE(default_preview.ok()) << default_preview.error().message;
  ASSERT_TRUE(deleted_preview.ok()) << deleted_preview.error().message;
  EXPECT_EQ(deleted_preview.value().encoded_bytes,
            default_preview.value().encoded_bytes);
  EXPECT_FALSE(resolver.resolve(ProjectAssetId::generate()).has_value());
  const auto presented = MaterialPresentationService::snapshot(
      project_, project_.active_scene()->session.get());
  EXPECT_TRUE(std::ranges::none_of(presented.materials, [this](const auto &item) {
    return item.reference == MaterialReference::project(deleted_a_) ||
           item.reference == MaterialReference::project(deleted_b_);
  }));
}

TEST_F(DeletedMaterialResolutionTests,
       UndoRestoresRawBindingsButUsageAndReplacementKeepDefaultSemantics) {
  const auto box = create_box();
  ASSERT_TRUE(box.ok()) << box.error_message();
  MaterialComponent old{.material = MaterialReference::project(deleted_a_)};
  old.material_slots.emplace(7u, MaterialReference::project(deleted_b_));
  ASSERT_TRUE(ProjectMaterialFixture(project_).set_material(box.value().entity, old));
  ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(editor_.selection().select_object(box.value()).ok());
  expect_only_default(QuaderMaterialUsageScope::Selection);

  const auto replaced = editor_.materials().replace_material_references(
      QuaderMaterialReference::project(default_),
      QuaderMaterialReference::project(live_),
      QuaderMaterialUsageScope::Selection);
  ASSERT_TRUE(replaced.ok()) << replaced.error_message();
  ASSERT_TRUE(replaced.changed());
  ASSERT_NE(binding(box.value().entity), nullptr);
  EXPECT_EQ(binding(box.value().entity)->material, MaterialReference::project(live_));
  EXPECT_EQ(binding(box.value().entity)->material_slots.at(7u),
            MaterialReference::project(live_));
  ASSERT_TRUE(editor_.history().undo().ok());
  EXPECT_EQ(*binding(box.value().entity), old);
  expect_only_default(QuaderMaterialUsageScope::Selection);
  ASSERT_TRUE(editor_.history().redo().ok());
  EXPECT_EQ(binding(box.value().entity)->material, MaterialReference::project(live_));

  const auto rejected = editor_.materials().replace_material_references(
      QuaderMaterialReference::project(live_),
      QuaderMaterialReference::project(deleted_a_),
      QuaderMaterialUsageScope::ActiveScene);
  EXPECT_FALSE(rejected.ok());
  EXPECT_FALSE(rejected.changed());
  EXPECT_FALSE(editor_.modeling().apply_material(
      QuaderMaterialReference::project(deleted_a_)).ok());
  EXPECT_FALSE(editor_.standard_material_documents().open(deleted_a_).ok());
}

TEST_F(DeletedMaterialResolutionTests,
       SelectedFacesBatchDistinctDeletedIdsIntoOneUndoableReplacement) {
  const auto box = create_box();
  ASSERT_TRUE(box.ok()) << box.error_message();
  const auto *document = polygon(box.value().entity);
  ASSERT_NE(document, nullptr);
  ASSERT_GE(document->faces.size(), 3u);
  const QuaderElementId first = document->faces[0].id;
  const QuaderElementId second = document->faces[1].id;
  const QuaderElementId unselected = document->faces[2].id;
  MaterialComponent old{.material = MaterialReference::project(deleted_a_)};
  old.material_slots.emplace(7u, MaterialReference::project(deleted_b_));
  ASSERT_TRUE(ProjectMaterialFixture(project_).set_material(box.value().entity, old));
  ASSERT_TRUE(editor_.modeling().mesh(box.value()).materials()
                  .assign_to_face(second, 7u).ok());
  ASSERT_TRUE(editor_.selection().select_object(box.value()).ok());
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor_.selection().select_faces(box.value(), {first, second}).ok());
  const auto before = *binding(box.value().entity);
  expect_only_default(QuaderMaterialUsageScope::Selection);
  const auto queried = editor_.modeling().project_material_for_face(
      box.value().modeling_object_id, first);
  ASSERT_TRUE(queried.ok()) << queried.error_message();
  EXPECT_EQ(queried.value().id, default_);

  const auto result = editor_.materials().replace_material_references(
      QuaderMaterialReference::project(default_),
      QuaderMaterialReference::project(live_),
      QuaderMaterialUsageScope::Selection);
  ASSERT_TRUE(result.ok()) << result.error_message();
  ASSERT_TRUE(result.changed());
  for (const auto face : {first, second}) {
    const auto *changed = quader_poly::find_face(*polygon(box.value().entity), face);
    ASSERT_NE(changed, nullptr);
    EXPECT_EQ(binding(box.value().entity)->material_for_slot(changed->material_slot),
              MaterialReference::project(live_));
  }
  const auto *untouched =
      quader_poly::find_face(*polygon(box.value().entity), unselected);
  ASSERT_NE(untouched, nullptr);
  EXPECT_EQ(binding(box.value().entity)->material_for_slot(untouched->material_slot),
            MaterialReference::project(deleted_a_));
  ASSERT_TRUE(editor_.history().undo().ok());
  EXPECT_EQ(*binding(box.value().entity), before);
  expect_only_default(QuaderMaterialUsageScope::Selection);
  ASSERT_TRUE(editor_.history().redo().ok());
  const auto used = editor_.materials().used_materials(QuaderMaterialUsageScope::Selection);
  ASSERT_TRUE(used.ok());
  ASSERT_EQ(used.value().size(), 1u);
  EXPECT_EQ(used.value().front(), QuaderMaterialReference::project(live_));
}

TEST_F(DeletedMaterialResolutionTests,
       FaceTextureCanvasResolvesUndoneDeletedBindingToSavedDefault) {
  const auto box = create_box();
  ASSERT_TRUE(box.ok()) << box.error_message();
  const auto *document = polygon(box.value().entity);
  ASSERT_NE(document, nullptr);
  ASSERT_FALSE(document->faces.empty());
  const QuaderElementId face = document->faces.front().id;
  ASSERT_TRUE(editor_.selection().select_object(box.value()).ok());
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor_.selection().select_face(box.value(), face).ok());
  const auto original_default = editor_.modeling().face_texture_canvas();
  ASSERT_EQ(original_default.material_id, default_);

  const auto opened = editor_.standard_material_documents().open(default_);
  ASSERT_TRUE(opened.ok()) << opened.error_message();
  const QuaderStandardMaterialTextureTransform transform{
      .center = {0.25F, 0.75F}, .scale = {2.0F, -3.0F},
      .offset = {0.375F, -0.5F}, .rotation_degrees = 37.0F};
  ASSERT_TRUE(editor_.standard_material_documents().set_texture_transform(transform).ok());
  ASSERT_TRUE(editor_.standard_material_documents().save().ok());
  ASSERT_TRUE(editor_.standard_material_documents().close().ok());
  const auto expected = editor_.modeling().face_texture_canvas();
  ASSERT_TRUE(expected.editable) << expected.disabled_reason;
  ASSERT_TRUE(expected.texture_available);
  EXPECT_NE(expected.uv_from_plane, original_default.uv_from_plane);
  const auto default_image = project_.material_texture_image(
      default_, StandardMaterialTextureSlot::Albedo);
  ASSERT_NE(default_image, nullptr);

  const MaterialComponent historical{.material = MaterialReference::project(deleted_a_)};
  ASSERT_TRUE(ProjectMaterialFixture(project_).set_material(box.value().entity, historical));
  const auto assigned = editor_.modeling().apply_material(
      QuaderMaterialReference::project(live_));
  ASSERT_TRUE(assigned.ok()) << assigned.error_message();
  ASSERT_TRUE(assigned.changed());
  EXPECT_EQ(editor_.modeling().face_texture_canvas().material_id, live_);
  ASSERT_TRUE(editor_.history().undo().ok());
  ASSERT_NE(binding(box.value().entity), nullptr);
  ASSERT_EQ(*binding(box.value().entity), historical);

  const auto restored = editor_.modeling().face_texture_canvas();
  EXPECT_EQ(restored.material_id, default_);
  EXPECT_EQ(restored.selected_face_count, 1U);
  EXPECT_EQ(restored.object_id, box.value().modeling_object_id);
  EXPECT_EQ(restored.face_id, face);
  EXPECT_TRUE(restored.editable) << restored.disabled_reason;
  EXPECT_EQ(restored.texture_available, expected.texture_available);
  EXPECT_FLOAT_EQ(restored.texture_extent.x, static_cast<float>(default_image->width));
  EXPECT_FLOAT_EQ(restored.texture_extent.y, static_cast<float>(default_image->height));
  EXPECT_EQ(project_.material_texture_image(restored.material_id,
                                            StandardMaterialTextureSlot::Albedo), default_image);
  EXPECT_EQ(restored.uv_from_plane, expected.uv_from_plane);
  EXPECT_DOUBLE_EQ(restored.uv_offset.x, expected.uv_offset.x);
  EXPECT_DOUBLE_EQ(restored.uv_offset.y, expected.uv_offset.y);
  EXPECT_DOUBLE_EQ(restored.repeat_width, expected.repeat_width);
  EXPECT_DOUBLE_EQ(restored.repeat_height, expected.repeat_height);
  EXPECT_EQ(*binding(box.value().entity), historical);
}

TEST_F(DeletedMaterialResolutionTests,
       RetainedOrdinaryClipboardUsesDefaultAfterPasteUndoAndRedo) {
  const auto box = create_box();
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(ProjectMaterialFixture(project_).set_material(
      box.value().entity, {.material = MaterialReference::project(deleted_a_)}));
  ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(editor_.selection().select_object(box.value()).ok());
  ASSERT_TRUE(editor_.modeling().copy_selection().ok());
  const auto pasted = editor_.modeling().paste_selection();
  ASSERT_TRUE(pasted.ok()) << pasted.error_message();
  ASSERT_TRUE(pasted.changed());
  expect_only_default(QuaderMaterialUsageScope::Selection);
  ASSERT_TRUE(editor_.history().undo().ok());
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_only_default(QuaderMaterialUsageScope::Selection);
  const auto selected = project_.active_scene()->session->scene().selected_entities();
  ASSERT_EQ(selected.size(), 1u);
  ASSERT_NE(binding(selected.front()), nullptr);
  ProjectSessionRenderMaterialResolver resolver(project_);
  const auto rendered = resolver.resolve(binding(selected.front())->material.project_asset());
  ASSERT_TRUE(rendered.has_value());
  EXPECT_EQ(rendered->evaluation.project_material_id, default_);
}

TEST_F(DeletedMaterialResolutionTests,
       RetainedInstanceClipboardCanonicalizesImportedDefinitionAndDormantSlots) {
  const auto box = create_box();
  ASSERT_TRUE(box.ok()) << box.error_message();
  MaterialComponent old{.material = MaterialReference::project(deleted_a_)};
  old.material_slots.emplace(7u, MaterialReference::project(deleted_b_));
  ASSERT_TRUE(ProjectMaterialFixture(project_).set_material(box.value().entity, old));
  ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(editor_.selection().select_object(box.value()).ok());
  ASSERT_TRUE(editor_.modeling().create_instance().ok());
  ASSERT_TRUE(editor_.modeling().copy_selection().ok());
  ASSERT_TRUE(editor_.scene_documents().new_scene().ok());
  const auto pasted = editor_.modeling().paste_selection();
  ASSERT_TRUE(pasted.ok()) << pasted.error_message();
  ASSERT_TRUE(pasted.changed());
  const auto &scene = project_.active_scene()->session->scene();
  const auto selected = scene.selected_entities();
  ASSERT_EQ(selected.size(), 1u);
  const auto *occurrence = scene.instance_occurrence(selected.front());
  ASSERT_NE(occurrence, nullptr);
  const auto members = scene.children(occurrence->definition);
  ASSERT_EQ(members.size(), 1u);
  ASSERT_NE(binding(members.front()), nullptr);
  EXPECT_EQ(binding(members.front())->material, MaterialReference::project(default_));
  EXPECT_EQ(binding(members.front())->material_slots.at(7u),
            MaterialReference::project(default_));
  ASSERT_TRUE(editor_.history().undo().ok());
  ASSERT_TRUE(editor_.history().redo().ok());
  EXPECT_EQ(binding(members.front())->material, MaterialReference::project(default_));
  EXPECT_EQ(binding(members.front())->material_slots.at(7u),
            MaterialReference::project(default_));
}

TEST_F(DeletedMaterialResolutionTests,
       ApplyHistoricalFaceMaterialAssignsCanonicalDefault) {
  const auto box = create_box();
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(ProjectMaterialFixture(project_).set_material(
      box.value().entity, {.material = MaterialReference::project(deleted_a_)}));
  const auto *document = polygon(box.value().entity);
  ASSERT_NE(document, nullptr);
  ASSERT_FALSE(document->faces.empty());
  const auto applied = editor_.modeling().apply_face_material_to_mesh(
      box.value().modeling_object_id, document->faces.front().id);
  ASSERT_TRUE(applied.ok()) << applied.error_message();
  ASSERT_TRUE(applied.changed());
  EXPECT_EQ(binding(box.value().entity)->material, MaterialReference::project(default_));
  ASSERT_TRUE(editor_.history().undo().ok());
  EXPECT_EQ(binding(box.value().entity)->material, MaterialReference::project(deleted_a_));
  expect_only_default(QuaderMaterialUsageScope::ActiveScene);
}

} // namespace
} // namespace quader

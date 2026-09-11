////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "assets/default_material_catalog.hpp"
#include "editor/session/project_session.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace quader {
namespace {

/// Removes the disposable test project after its editor and session close.
class ScopedMaterialFolderDirectory final {
public:
  ScopedMaterialFolderDirectory()
      : path_(std::filesystem::temp_directory_path() /
              ("quader-material-folder-api-" +
               std::string(ProjectAssetId::generate().str()))) {
    EXPECT_TRUE(std::filesystem::create_directories(path_ / "FolderApi"));
  }

  ~ScopedMaterialFolderDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }

  [[nodiscard]] const std::filesystem::path &path() const { return path_; }

private:
  std::filesystem::path path_;
};

ProjectRelativePath relative_path(std::string_view path) {
  auto parsed = ProjectRelativePath::parse_portable(path);
  EXPECT_TRUE(parsed.ok()) << parsed.error().message;
  return parsed.ok() ? parsed.take_value() : ProjectRelativePath{};
}

/// Exercises organization exclusively through the whole-editor public facade.
class ProjectMaterialFolderApiTests : public testing::Test {
protected:
  ProjectMaterialFolderApiTests() : editor_(project_) {}

  void SetUp() override {
    project_.set_resource_root(resource_root());
    const auto created = editor_.project().create(root());
    ASSERT_TRUE(created.ok()) << created.error_message();
    editor_.scene_documents().set_runtime_hooks({
        .confirm_dirty_scene = [](const QuaderSceneDocumentInfo &,
                                  std::string_view) {
          return QuaderDirtySceneDecision::Discard;
        },
    });
    ASSERT_TRUE(editor_.events()
                    .subscribe(quader_event::kProjectAssetsChanged,
                               [this](const QuaderEvent &event) {
                                 assets_events_.push_back(event);
                               })
                    .valid());
    ASSERT_TRUE(editor_.events()
                    .subscribe(quader_event::kActiveProjectMaterialChanged,
                               [this](const QuaderEvent &event) {
                                 active_events_.push_back(event);
                               })
                    .valid());
    ASSERT_TRUE(editor_.events()
                    .subscribe(quader_event::kStandardMaterialClosed,
                               [this](const QuaderEvent &event) {
                                 closed_material_events_.push_back(event);
                               })
                    .valid());
  }

  [[nodiscard]] static std::filesystem::path resource_root() {
    return std::filesystem::path(QUADER_SOURCE_DIR) / "resources" /
           "modeling_assets";
  }

  [[nodiscard]] std::filesystem::path root() const {
    return temporary_.path() / "FolderApi";
  }

  [[nodiscard]] QuaderProjectMaterialInfo
  import_material(std::string_view folder, std::string_view name = "wood") {
    const auto source = temporary_.path() / (std::string(name) + ".png");
    std::error_code error;
    std::filesystem::copy_file(
        resource_root() / "materials/default/default_albedo.png", source,
        std::filesystem::copy_options::overwrite_existing, error);
    EXPECT_FALSE(error) << error.message();
    const auto imported = editor_.project_assets().import_textures_as_materials(
        std::array{source}, folder);
    EXPECT_TRUE(imported.ok()) << imported.error_message();
    if (!imported.ok() || imported.value().imported.empty()) {
      return {};
    }
    EXPECT_TRUE(imported.changed());
    EXPECT_TRUE(imported.value().failed.empty());
    return imported.value().imported.front().material;
  }

  [[nodiscard]] std::optional<QuaderProjectMaterialInfo>
  material_info(ProjectAssetId id) {
    const auto materials = editor_.project_assets().materials();
    EXPECT_TRUE(materials.ok()) << materials.error_message();
    if (!materials.ok()) {
      return std::nullopt;
    }
    const auto found = std::ranges::find(materials.value(), id,
                                        &QuaderProjectMaterialInfo::id);
    return found != materials.value().end()
               ? std::optional<QuaderProjectMaterialInfo>(*found)
               : std::nullopt;
  }

  void expect_only_default_used() {
    const auto used = editor_.materials().used_materials(
        QuaderMaterialUsageScope::ActiveScene);
    ASSERT_TRUE(used.ok()) << used.error_message();
    ASSERT_EQ(used.value().size(), 1u);
    EXPECT_EQ(used.value().front(), QuaderMaterialReference::project(
                                        *ProjectAssetId::parse(
                                            kDefaultProjectMaterialId)));
  }

  ScopedMaterialFolderDirectory temporary_;
  std::vector<QuaderEvent> assets_events_;
  std::vector<QuaderEvent> active_events_;
  std::vector<QuaderEvent> closed_material_events_;
  ProjectSession project_;
  QuaderEditorApi editor_;
};

TEST_F(ProjectMaterialFolderApiTests,
       EmptyFoldersPublishRevisionsAndSurviveProjectReopening) {
  auto &assets = editor_.project_assets();
  const auto before = assets.material_presentation_revision();
  ASSERT_TRUE(before.ok());
  const auto created = assets.create_material_folder("Materials", "Dev");
  ASSERT_TRUE(created.ok()) << created.error_message();
  EXPECT_TRUE(created.changed());
  ASSERT_TRUE(assets.create_material_folder("Materials/Dev", "Empty").ok());
  const auto snapshot = assets.material_presentations();
  ASSERT_TRUE(snapshot.ok());
  EXPECT_FALSE(snapshot.changed());
  EXPECT_NE(snapshot.value().revision, before.value());
  EXPECT_NE(std::ranges::find(snapshot.value().material_folders,
                             "Materials/Dev/Empty"),
            snapshot.value().material_folders.end());
  ASSERT_EQ(assets_events_.size(), 2u);
  EXPECT_TRUE(active_events_.empty());
  EXPECT_TRUE(assets_events_.front().project_id.has_value());
  EXPECT_EQ(assets_events_.front().project_name, "FolderApi");
  const auto stable_revision = assets.material_presentation_revision();
  ASSERT_TRUE(stable_revision.ok());
  EXPECT_EQ(stable_revision.value(), snapshot.value().revision);

  const auto manifest_path = editor_.project().manifest_path();
  ASSERT_TRUE(editor_.project().close().ok());
  ASSERT_TRUE(editor_.project().open(manifest_path).ok());
  const auto reopened = assets.material_presentations();
  ASSERT_TRUE(reopened.ok());
  EXPECT_NE(std::ranges::find(reopened.value().material_folders,
                             "Materials/Dev/Empty"),
            reopened.value().material_folders.end());
}

TEST_F(ProjectMaterialFolderApiTests,
       NestedImportAndBatchMovesPublishOnceAndRetainDraftIdentity) {
  auto &assets = editor_.project_assets();
  ASSERT_TRUE(assets.create_material_folder("Materials", "Dev").ok());
  ASSERT_TRUE(assets.create_material_folder("Materials/Dev", "Wood").ok());
  ASSERT_TRUE(assets.create_material_folder("Materials", "Moved").ok());
  const auto first = import_material("Materials/Dev/Wood");
  const auto second = import_material("Materials/Dev/Wood");
  ASSERT_TRUE(first.id.valid());
  ASSERT_TRUE(second.id.valid());
  EXPECT_EQ(first.project_relative_path, "Materials/Dev/Wood/wood.qmat");
  EXPECT_EQ(second.project_relative_path, "Materials/Dev/Wood/wood_2.qmat");
  ASSERT_TRUE(assets.set_active_material(first.id).ok());
  ASSERT_TRUE(editor_.standard_material_documents().open(first.id).ok());
  ASSERT_TRUE(editor_.standard_material_documents().set_name("Dirty Wood").ok());
  const auto draft = editor_.standard_material_documents().current();
  ASSERT_TRUE(draft.ok());
  ASSERT_TRUE(draft.value().dirty);
  assets_events_.clear();
  active_events_.clear();

  const auto moved = assets.move_materials(std::array{first.id, second.id},
                                           "Materials/Moved");
  ASSERT_TRUE(moved.ok()) << moved.error_message();
  EXPECT_TRUE(moved.changed());
  EXPECT_EQ(assets_events_.size(), 1u);
  EXPECT_TRUE(active_events_.empty());
  const auto renamed = assets.rename_material_folder("Materials/Moved", "Oak");
  ASSERT_TRUE(renamed.ok()) << renamed.error_message();
  EXPECT_TRUE(renamed.changed());
  ASSERT_TRUE(assets.move_material_folder("Materials/Oak", "Materials/Dev")
                  .ok());
  EXPECT_EQ(assets_events_.size(), 3u);
  const auto first_after = material_info(first.id);
  const auto second_after = material_info(second.id);
  ASSERT_TRUE(first_after.has_value());
  ASSERT_TRUE(second_after.has_value());
  EXPECT_EQ(first_after->project_relative_path, "Materials/Dev/Oak/wood.qmat");
  EXPECT_EQ(second_after->project_relative_path,
            "Materials/Dev/Oak/wood_2.qmat");
  const auto moved_draft = editor_.standard_material_documents().current();
  ASSERT_TRUE(moved_draft.ok());
  EXPECT_EQ(moved_draft.value().session_id, draft.value().session_id);
  EXPECT_EQ(moved_draft.value().name, "Dirty Wood");
  EXPECT_GT(moved_draft.value().draft_revision, draft.value().draft_revision);
  EXPECT_TRUE(moved_draft.value().dirty);
  ASSERT_TRUE(moved_draft.value().path.has_value());
  EXPECT_EQ(moved_draft.value().path->portable_string(),
            "Materials/Dev/Oak/wood.qmat");
  EXPECT_TRUE(closed_material_events_.empty());
}

TEST_F(ProjectMaterialFolderApiTests,
       FailedAndUnchangedMovesDoNotPublishOrAdvancePresentation) {
  auto &assets = editor_.project_assets();
  ASSERT_TRUE(assets.create_material_folder("Materials", "Dev").ok());
  const auto material = import_material("Materials/Dev");
  ASSERT_TRUE(material.id.valid());
  const auto before = assets.material_presentation_revision();
  ASSERT_TRUE(before.ok());
  assets_events_.clear();

  const auto unchanged = assets.move_materials(std::array{material.id},
                                               "Materials/Dev");
  EXPECT_TRUE(unchanged.ok()) << unchanged.error_message();
  EXPECT_FALSE(unchanged.changed());
  const auto missing = assets.move_materials(
      std::array{material.id, ProjectAssetId::generate()}, "Materials");
  EXPECT_FALSE(missing.ok());
  EXPECT_FALSE(missing.changed());
  EXPECT_FALSE(missing.error_message().empty());
  const auto cycle = assets.move_material_folder("Materials/Dev", "Materials/Dev");
  EXPECT_FALSE(cycle.ok());
  EXPECT_FALSE(cycle.changed());
  const auto escape = assets.create_material_folder("Materials", "../Outside");
  EXPECT_FALSE(escape.ok());
  EXPECT_FALSE(escape.changed());
  EXPECT_TRUE(assets_events_.empty());
  const auto after = assets.material_presentation_revision();
  ASSERT_TRUE(after.ok());
  EXPECT_EQ(after.value(), before.value());
  const auto retained = material_info(material.id);
  ASSERT_TRUE(retained.has_value());
  EXPECT_EQ(retained->project_relative_path, material.project_relative_path);
}

TEST_F(ProjectMaterialFolderApiTests,
       StaleDeletionPreviewFailsWithoutPublishingAndFreshPreviewDeletes) {
  auto &assets = editor_.project_assets();
  ASSERT_TRUE(assets.create_material_folder("Materials", "Disposable").ok());
  const auto preview = assets.preview_delete_material_folder("Materials/Disposable");
  ASSERT_TRUE(preview.ok()) << preview.error_message();
  EXPECT_FALSE(preview.changed());
  ASSERT_TRUE(preview.value().complete);
  EXPECT_TRUE(preview.value().material_ids.empty());
  const auto imported = import_material("Materials/Disposable");
  ASSERT_TRUE(imported.id.valid());
  assets_events_.clear();
  const auto stale = assets.delete_material_folder("Materials/Disposable",
                                                    preview.value().revision);
  EXPECT_FALSE(stale.ok());
  EXPECT_FALSE(stale.changed());
  EXPECT_FALSE(stale.error_message().empty());
  EXPECT_TRUE(material_info(imported.id).has_value());
  EXPECT_TRUE(assets_events_.empty());

  const auto current = assets.preview_delete_material_folder("Materials/Disposable");
  ASSERT_TRUE(current.ok());
  ASSERT_TRUE(current.value().complete);
  EXPECT_NE(current.value().revision, preview.value().revision);
  ASSERT_EQ(current.value().material_ids, std::vector{imported.id});
  const auto deleted = assets.delete_material_folder("Materials/Disposable",
                                                      current.value().revision);
  ASSERT_TRUE(deleted.ok()) << deleted.error_message();
  EXPECT_TRUE(deleted.changed());
  EXPECT_EQ(assets_events_.size(), 1u);
  EXPECT_FALSE(material_info(imported.id).has_value());
  EXPECT_FALSE(std::filesystem::exists(root() / "Materials/Disposable"));
  EXPECT_TRUE(std::filesystem::exists(root() / "Textures/wood.png"));
  EXPECT_FALSE(assets.set_active_material(imported.id).ok());
}

TEST_F(ProjectMaterialFolderApiTests,
       DeletionPreservesDirtySceneEditsAndUpdatesClosedSavedScenes) {
  auto &assets = editor_.project_assets();
  auto &documents = editor_.scene_documents();
  ASSERT_TRUE(assets.create_material_folder("Materials", "Disposable").ok());
  const auto imported = import_material("Materials/Disposable");
  ASSERT_TRUE(imported.id.valid());
  ASSERT_TRUE(assets.set_active_material(imported.id).ok());
  const auto box = editor_.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}}, "Saved Name");
  ASSERT_TRUE(box.ok()) << box.error_message();
  const auto first_id = documents.active_scene_id();
  ASSERT_TRUE(first_id.has_value());
  ASSERT_TRUE(documents.save_as(*first_id, relative_path("Scenes/Used.qdr")).ok());
  ASSERT_TRUE(editor_.object(box.value()).rename("Unrelated Dirty Name").ok());
  const auto second_scene = documents.new_scene();
  ASSERT_TRUE(second_scene.ok());
  ASSERT_TRUE(editor_.modeling()
                  .create_box({.min = {-1.0f, -1.0f, -1.0f},
                               .max = {1.0f, 1.0f, 1.0f}},
                              "Closed Saved Box")
                  .ok());
  ASSERT_TRUE(documents.save_as(second_scene.value(),
                               relative_path("Scenes/Closed.qdr"))
                  .ok());
  ASSERT_TRUE(documents.close(second_scene.value()).ok());
  ASSERT_TRUE(documents.activate(*first_id).ok());
  ASSERT_TRUE(documents.active_scene()->dirty);
  const auto history_before = editor_.query().history();
  ASSERT_TRUE(editor_.standard_material_documents().open(imported.id).ok());
  ASSERT_TRUE(editor_.standard_material_documents().set_name("Discarded Draft").ok());

  const auto dependencies = assets.material_dependencies(true);
  ASSERT_TRUE(dependencies.ok()) << dependencies.error_message();
  ASSERT_TRUE(dependencies.value().complete);
  const auto has_dependency = [&](std::string_view path, bool live, bool dirty) {
    return std::ranges::any_of(dependencies.value().scenes, [&](const auto &scene) {
      return scene.project_relative_path == path && scene.live == live &&
             scene.dirty == dirty &&
             std::ranges::find(scene.material_ids, imported.id) !=
                 scene.material_ids.end();
    });
  };
  EXPECT_TRUE(has_dependency("Scenes/Used.qdr", false, false));
  EXPECT_TRUE(has_dependency("Scenes/Used.qdr", true, true));
  EXPECT_TRUE(has_dependency("Scenes/Closed.qdr", false, false));
  const auto preview = assets.preview_delete_material_folder("Materials/Disposable");
  ASSERT_TRUE(preview.ok());
  ASSERT_TRUE(preview.value().complete);
  EXPECT_TRUE(preview.value().discards_material_draft);
  EXPECT_EQ(preview.value().material_ids, std::vector{imported.id});
  EXPECT_FALSE(preview.value().affected_scenes.empty());
  assets_events_.clear();
  active_events_.clear();
  const auto deleted = assets.delete_material_folder("Materials/Disposable",
                                                      preview.value().revision);
  ASSERT_TRUE(deleted.ok()) << deleted.error_message();
  EXPECT_TRUE(deleted.changed());
  EXPECT_EQ(assets_events_.size(), 1u);
  ASSERT_EQ(active_events_.size(), 1u);
  EXPECT_EQ(active_events_.front().project_asset_id,
            ProjectAssetId::parse(kDefaultProjectMaterialId));
  EXPECT_EQ(closed_material_events_.size(), 1u);
  EXPECT_FALSE(editor_.standard_material_documents().current().ok());
  ASSERT_TRUE(documents.active_scene().has_value());
  EXPECT_TRUE(documents.active_scene()->dirty);
  const auto dirty_entity = editor_.query().entity(box.value().entity);
  ASSERT_TRUE(dirty_entity.has_value());
  EXPECT_EQ(dirty_entity->name, "Unrelated Dirty Name");
  EXPECT_EQ(editor_.query().history().undo_label, history_before.undo_label);
  expect_only_default_used();
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_only_default_used();
  ASSERT_TRUE(documents.close(*first_id).ok());
  ASSERT_TRUE(documents.open(relative_path("Scenes/Used.qdr")).ok());
  const auto entities = editor_.query().entities();
  EXPECT_NE(std::ranges::find(entities, "Saved Name", &QuaderEntitySnapshot::name),
            entities.end());
  EXPECT_EQ(std::ranges::find(entities, "Unrelated Dirty Name",
                             &QuaderEntitySnapshot::name),
            entities.end());
  expect_only_default_used();
  ASSERT_TRUE(documents.open(relative_path("Scenes/Closed.qdr")).ok());
  expect_only_default_used();
}

} // namespace
} // namespace quader

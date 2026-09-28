////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "api/quader_project_assets_api_internal_access.hpp"
#include "assets/default_material_catalog.hpp"
#include "editor/material/project_asset_dependency_service.hpp"
#include "editor/session/project_session.hpp"

#include <gtest/gtest.h>

#include <QColor>
#include <QImage>
#include <QString>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
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
    QuaderProjectAssetsApiInternalAccess::set_recycle_port(
        editor_.project_assets(), [this](std::span<const std::filesystem::path> paths) {
          const auto bin = temporary_.path() / "RecycleTest";
          std::filesystem::create_directories(bin);
          ProjectAssetRecycleResult result{.ok = true};
          for (const auto &path : paths) {
            std::error_code error;
            const auto destination = bin / std::to_string(recycled_paths_.size());
            std::filesystem::rename(path, destination, error);
            result.items.push_back({.original_path = path, .recycled = !error,
                                     .message = error.message()});
            if (error) {
              result.ok = false;
              result.message = error.message();
              break;
            }
            recycled_paths_.push_back(path);
          }
          return result;
        });
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
  std::vector<QuaderEvent> closed_material_events_;
  std::vector<std::filesystem::path> recycled_paths_;
  ProjectSession project_;
  QuaderEditorApi editor_;
};

TEST_F(ProjectMaterialFolderApiTests,
       EmptyFoldersPublishRevisionsAndSurviveProjectReopening) {
  auto &assets = editor_.project_assets();
  const auto before = assets.asset_revision();
  ASSERT_TRUE(before.ok());
  const auto created = assets.create_material_folder("Materials", "Dev");
  ASSERT_TRUE(created.ok()) << created.error_message();
  EXPECT_TRUE(created.changed());
  ASSERT_TRUE(assets.create_material_folder("Materials/Dev", "Empty").ok());
  const auto snapshot = assets.assets();
  ASSERT_TRUE(snapshot.ok());
  EXPECT_FALSE(snapshot.changed());
  EXPECT_NE(snapshot.value().revision, before.value().revision);
  EXPECT_NE(std::ranges::find(snapshot.value().folders,
                             "Materials/Dev/Empty"),
            snapshot.value().folders.end());
  ASSERT_EQ(assets_events_.size(), 2u);
  EXPECT_TRUE(assets_events_.front().project_id.has_value());
  EXPECT_EQ(assets_events_.front().project_name, "FolderApi");
  const auto stable_revision = assets.asset_revision();
  ASSERT_TRUE(stable_revision.ok());
  EXPECT_EQ(stable_revision.value().revision, snapshot.value().revision);

  const auto manifest_path = editor_.project().manifest_path();
  ASSERT_TRUE(editor_.project().close().ok());
  ASSERT_TRUE(editor_.project().open(manifest_path).ok());
  const auto reopened = assets.assets();
  ASSERT_TRUE(reopened.ok());
  EXPECT_NE(std::ranges::find(reopened.value().folders,
                             "Materials/Dev/Empty"),
            reopened.value().folders.end());
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
  ASSERT_TRUE(editor_.standard_material_documents().open(first.id).ok());
  ASSERT_TRUE(editor_.standard_material_documents().set_name("Dirty Wood").ok());
  const auto draft = editor_.standard_material_documents().current();
  ASSERT_TRUE(draft.ok());
  ASSERT_TRUE(draft.value().dirty);
  assets_events_.clear();

  const auto moved = assets.move_materials(std::array{first.id, second.id},
                                           "Materials/Moved");
  ASSERT_TRUE(moved.ok()) << moved.error_message();
  EXPECT_TRUE(moved.changed());
  EXPECT_EQ(assets_events_.size(), 1u);
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
       MaterialRenameUpdatesCatalogAndRetargetsAnOpenDirtyDraft) {
  auto &assets = editor_.project_assets();
  auto &documents = editor_.standard_material_documents();
  ASSERT_TRUE(assets.create_material_folder("Materials", "Wood").ok());
  const auto material = import_material("Materials/Wood");
  ASSERT_TRUE(material.id.valid());
  ASSERT_TRUE(documents.open(material.id).ok());
  ASSERT_TRUE(documents.set_roughness(0.37f).ok());
  const auto dirty_before = documents.current();
  ASSERT_TRUE(dirty_before.ok());
  ASSERT_TRUE(dirty_before.value().dirty);
  assets_events_.clear();

  const auto renamed = assets.rename_material(material.id, "Oak Grain");
  ASSERT_TRUE(renamed.ok()) << renamed.error_message();
  EXPECT_TRUE(renamed.changed());
  EXPECT_EQ(renamed.value().id, material.id);
  EXPECT_EQ(renamed.value().name, "Oak Grain");
  EXPECT_EQ(renamed.value().project_relative_path,
            "Materials/Wood/Oak Grain.qmat");
  EXPECT_FALSE(std::filesystem::exists(root() / material.project_relative_path));
  EXPECT_TRUE(std::filesystem::exists(root() /
                                      renamed.value().project_relative_path));
  ASSERT_EQ(assets_events_.size(), 1u);

  const auto dirty_after = documents.current();
  ASSERT_TRUE(dirty_after.ok());
  EXPECT_EQ(dirty_after.value().session_id, dirty_before.value().session_id);
  EXPECT_TRUE(dirty_after.value().dirty);
  EXPECT_FLOAT_EQ(dirty_after.value().material.roughness, 0.37f);
  EXPECT_EQ(dirty_after.value().name, "Oak Grain");
  ASSERT_TRUE(dirty_after.value().path.has_value());
  EXPECT_EQ(dirty_after.value().path->portable_string(),
            "Materials/Wood/Oak Grain.qmat");

  const auto case_only = assets.rename_material(material.id, "oak grain");
  ASSERT_TRUE(case_only.ok()) << case_only.error_message();
  EXPECT_TRUE(case_only.changed());
  EXPECT_EQ(case_only.value().id, material.id);
  EXPECT_EQ(case_only.value().name, "oak grain");
  EXPECT_EQ(case_only.value().project_relative_path,
            "Materials/Wood/oak grain.qmat");
  EXPECT_TRUE(std::filesystem::exists(root() /
                                      "Materials/Wood/oak grain.qmat"));
  ASSERT_EQ(assets_events_.size(), 2u);

  const auto unchanged = assets.rename_material(material.id, "oak grain");
  ASSERT_TRUE(unchanged.ok()) << unchanged.error_message();
  EXPECT_FALSE(unchanged.changed());
  EXPECT_EQ(assets_events_.size(), 2u);
}

TEST_F(ProjectMaterialFolderApiTests,
       DuplicateAndCopyUseDeterministicNamesAndShareTextureReferences) {
  auto &assets = editor_.project_assets();
  ASSERT_TRUE(assets.create_material_folder("Materials", "Source").ok());
  ASSERT_TRUE(assets.create_material_folder("Materials", "Target").ok());
  const auto imported = import_material("Materials/Source");
  ASSERT_TRUE(imported.id.valid());
  const auto renamed = assets.rename_material(imported.id, "Wood");
  ASSERT_TRUE(renamed.ok()) << renamed.error_message();
  const auto source_texture = assets.material_albedo_texture_info(imported.id);
  ASSERT_TRUE(source_texture.ok()) << source_texture.error_message();
  auto &documents = editor_.standard_material_documents();
  ASSERT_TRUE(documents.open(imported.id).ok());
  ASSERT_TRUE(documents.set_roughness(0.23f).ok());
  const auto source_draft = documents.current();
  ASSERT_TRUE(source_draft.ok());
  ASSERT_TRUE(source_draft.value().dirty);
  assets_events_.clear();

  const auto first_duplicate =
      assets.duplicate_materials(std::array{imported.id});
  ASSERT_TRUE(first_duplicate.ok()) << first_duplicate.error_message();
  ASSERT_TRUE(first_duplicate.changed());
  ASSERT_EQ(first_duplicate.value().size(), 1u);
  const auto &duplicate_one = first_duplicate.value().front();
  EXPECT_TRUE(duplicate_one.id.valid());
  EXPECT_NE(duplicate_one.id, imported.id);
  EXPECT_EQ(duplicate_one.name, "Wood Copy");
  EXPECT_EQ(duplicate_one.project_relative_path,
            "Materials/Source/Wood Copy.qmat");

  const auto second_duplicate =
      assets.duplicate_materials(std::array{imported.id});
  ASSERT_TRUE(second_duplicate.ok()) << second_duplicate.error_message();
  ASSERT_EQ(second_duplicate.value().size(), 1u);
  const auto &duplicate_two = second_duplicate.value().front();
  EXPECT_NE(duplicate_two.id, imported.id);
  EXPECT_NE(duplicate_two.id, duplicate_one.id);
  EXPECT_EQ(duplicate_two.name, "Wood Copy 2");
  EXPECT_EQ(duplicate_two.project_relative_path,
            "Materials/Source/Wood Copy 2.qmat");

  const auto first_copy =
      assets.copy_materials(std::array{imported.id}, "Materials/Target");
  ASSERT_TRUE(first_copy.ok()) << first_copy.error_message();
  ASSERT_EQ(first_copy.value().size(), 1u);
  const auto &copy_one = first_copy.value().front();
  EXPECT_NE(copy_one.id, imported.id);
  EXPECT_NE(copy_one.id, duplicate_one.id);
  EXPECT_NE(copy_one.id, duplicate_two.id);
  EXPECT_EQ(copy_one.name, "Wood Copy");
  EXPECT_EQ(copy_one.project_relative_path,
            "Materials/Target/Wood Copy.qmat");

  const auto second_copy =
      assets.copy_materials(std::array{imported.id}, "Materials/Target");
  ASSERT_TRUE(second_copy.ok()) << second_copy.error_message();
  ASSERT_EQ(second_copy.value().size(), 1u);
  const auto &copy_two = second_copy.value().front();
  EXPECT_NE(copy_two.id, imported.id);
  EXPECT_NE(copy_two.id, copy_one.id);
  EXPECT_EQ(copy_two.name, "Wood Copy 2");
  EXPECT_EQ(copy_two.project_relative_path,
            "Materials/Target/Wood Copy 2.qmat");

  for (const ProjectAssetId copy_id : {duplicate_one.id, duplicate_two.id,
                                       copy_one.id, copy_two.id}) {
    const auto texture = assets.material_albedo_texture_info(copy_id);
    ASSERT_TRUE(texture.ok()) << texture.error_message();
    EXPECT_EQ(texture.value().texture_id, source_texture.value().texture_id);
    EXPECT_EQ(texture.value().project_relative_path,
              source_texture.value().project_relative_path);
    EXPECT_EQ(texture.value().absolute_path,
              source_texture.value().absolute_path);
  }
  EXPECT_EQ(assets_events_.size(), 4u);

  ASSERT_TRUE(documents.close(true).ok());
  ASSERT_TRUE(documents.open(copy_one.id).ok());
  const auto copied_document = documents.current();
  ASSERT_TRUE(copied_document.ok());
  EXPECT_FALSE(copied_document.value().dirty);
  EXPECT_FLOAT_EQ(copied_document.value().material.roughness, 1.0f);
}

TEST_F(ProjectMaterialFolderApiTests,
       RenameCollisionsAndStaleBatchCopiesDoNotPartiallyPublish) {
  auto &assets = editor_.project_assets();
  ASSERT_TRUE(assets.create_material_folder("Materials", "Target").ok());
  const auto wood = import_material("Materials", "wood");
  const auto stone = import_material("Materials", "stone");
  ASSERT_TRUE(wood.id.valid());
  ASSERT_TRUE(stone.id.valid());
  const auto before = assets.material_presentation_revision();
  ASSERT_TRUE(before.ok());
  const auto materials_before = assets.materials();
  ASSERT_TRUE(materials_before.ok());
  assets_events_.clear();

  const auto collision = assets.rename_material(wood.id, "STONE");
  EXPECT_FALSE(collision.ok());
  EXPECT_FALSE(collision.changed());
  EXPECT_FALSE(collision.error_message().empty());
  const auto retained_wood = material_info(wood.id);
  const auto retained_stone = material_info(stone.id);
  ASSERT_TRUE(retained_wood.has_value());
  ASSERT_TRUE(retained_stone.has_value());
  EXPECT_EQ(retained_wood->name, wood.name);
  EXPECT_EQ(retained_wood->project_relative_path, wood.project_relative_path);
  EXPECT_EQ(retained_stone->name, stone.name);
  EXPECT_EQ(retained_stone->project_relative_path,
            stone.project_relative_path);

  const std::array duplicate_ids{wood.id, wood.id};
  const auto duplicate_batch =
      assets.copy_materials(duplicate_ids, "Materials/Target");
  EXPECT_FALSE(duplicate_batch.ok());
  EXPECT_FALSE(duplicate_batch.changed());
  EXPECT_FALSE(duplicate_batch.error_message().empty());
  EXPECT_TRUE(std::filesystem::is_empty(root() / "Materials/Target"));

  ASSERT_TRUE(std::filesystem::create_directory(root() /
                                                "Materials/External"));
  const std::array stale_ids{wood.id, stone.id};
  const auto stale = assets.copy_materials(stale_ids, "Materials/Target");
  EXPECT_FALSE(stale.ok());
  EXPECT_FALSE(stale.changed());
  EXPECT_FALSE(stale.error_message().empty());
  EXPECT_TRUE(std::filesystem::is_empty(root() / "Materials/Target"));
  EXPECT_TRUE(assets_events_.empty());
  const auto after = assets.material_presentation_revision();
  const auto materials_after = assets.materials();
  ASSERT_TRUE(after.ok());
  ASSERT_TRUE(materials_after.ok());
  EXPECT_EQ(after.value(), before.value());
  EXPECT_EQ(materials_after.value().size(), materials_before.value().size());
}

TEST_F(ProjectMaterialFolderApiTests,
       DefaultMaterialRejectsRenameButCanBeDuplicatedAndCopied) {
  auto &assets = editor_.project_assets();
  ASSERT_TRUE(assets.create_material_folder("Materials", "Target").ok());
  const auto default_id = ProjectAssetId::parse(kDefaultProjectMaterialId);
  ASSERT_TRUE(default_id.has_value());
  assets_events_.clear();

  const auto renamed = assets.rename_material(*default_id, "Renamed Default");
  EXPECT_FALSE(renamed.ok());
  EXPECT_FALSE(renamed.changed());
  EXPECT_TRUE(assets_events_.empty());

  const auto duplicated = assets.duplicate_materials(std::array{*default_id});
  ASSERT_TRUE(duplicated.ok()) << duplicated.error_message();
  ASSERT_EQ(duplicated.value().size(), 1u);
  EXPECT_NE(duplicated.value().front().id, *default_id);
  EXPECT_EQ(duplicated.value().front().name, "Default Material Copy");
  EXPECT_EQ(duplicated.value().front().project_relative_path,
            "Materials/Default Material Copy.qmat");

  const auto copied =
      assets.copy_materials(std::array{*default_id}, "Materials/Target");
  ASSERT_TRUE(copied.ok()) << copied.error_message();
  ASSERT_EQ(copied.value().size(), 1u);
  EXPECT_NE(copied.value().front().id, *default_id);
  EXPECT_NE(copied.value().front().id, duplicated.value().front().id);
  EXPECT_EQ(copied.value().front().name, "Default Material Copy");
  EXPECT_EQ(copied.value().front().project_relative_path,
            "Materials/Target/Default Material Copy.qmat");
  EXPECT_EQ(assets_events_.size(), 2u);
}

TEST_F(ProjectMaterialFolderApiTests,
       DirectoryTimestampChangesDoNotExpireSavedMaterialCopySelections) {
  auto &assets = editor_.project_assets();
  const auto material = import_material("Materials", "Timestamp");
  ASSERT_TRUE(material.id.valid());
  const auto snapshot = assets.assets();
  ASSERT_TRUE(snapshot.ok()) << snapshot.error_message();
  const auto directory = root() / "Materials";
  std::error_code error;
  const auto previous = std::filesystem::last_write_time(directory, error);
  ASSERT_FALSE(error) << error.message();
  std::filesystem::last_write_time(directory, previous + std::chrono::seconds(2), error);
  ASSERT_FALSE(error) << error.message();

  const auto copied = assets.duplicate_materials(std::array{material.id});
  ASSERT_TRUE(copied.ok()) << copied.error_message();
  ASSERT_EQ(copied.value().size(), 1u);
  EXPECT_EQ(copied.value().front().project_relative_path, "Materials/Timestamp Copy.qmat");
  EXPECT_TRUE(material_info(material.id).has_value());
}

TEST_F(ProjectMaterialFolderApiTests,
       DependencyRevisionRetainsUnchangedWorkAndMapsIncompleteInspection) {
  auto &assets = editor_.project_assets();
  auto &scenes = editor_.scene_documents();
  const auto material = import_material("Materials", "usage");
  ASSERT_TRUE(material.id.valid());

  const auto initial = assets.material_dependency_revision();
  ASSERT_TRUE(initial.ok()) << initial.error_message();
  ASSERT_TRUE(initial.value().complete);
  EXPECT_EQ(initial.value().project_id, project_.manifest()->id());
  EXPECT_EQ(initial.value().project_generation,
            project_.activation_generation());
  const auto retained_work = project_.dependencies().work_stats();
  for (int frame = 0; frame < 32; ++frame) {
    const auto retained = assets.material_dependency_revision();
    ASSERT_TRUE(retained.ok()) << retained.error_message();
    EXPECT_EQ(retained.value(), initial.value());
  }
  EXPECT_EQ(project_.dependencies().work_stats(), retained_work);

  const auto box = editor_.modeling().create_box({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                 .max = {1.0f, 1.0f, 1.0f}},
      .name = "Live Usage",
      .material = QuaderMaterialReference::project(material.id),
  });
  ASSERT_TRUE(box.ok()) << box.error_message();
  const auto live = assets.material_dependency_revision();
  ASSERT_TRUE(live.ok()) << live.error_message();
  ASSERT_TRUE(live.value().complete);
  EXPECT_GT(live.value().revision, initial.value().revision);
  EXPECT_EQ(live.value().project_id, initial.value().project_id);
  EXPECT_EQ(live.value().project_generation,
            initial.value().project_generation);
  EXPECT_EQ(project_.dependencies().work_stats().live_scene_scan_count,
            retained_work.live_scene_scan_count + 1u);
  EXPECT_EQ(project_.dependencies().work_stats().saved_scene_read_count,
            retained_work.saved_scene_read_count);

  const auto scene_id = scenes.active_scene_id();
  ASSERT_TRUE(scene_id.has_value());
  ASSERT_TRUE(
      scenes.save_as(*scene_id, relative_path("Scenes/Usage.qdr")).ok());
  const auto saved = assets.material_dependency_revision();
  ASSERT_TRUE(saved.ok()) << saved.error_message();
  ASSERT_TRUE(saved.value().complete);
  EXPECT_GT(saved.value().revision, live.value().revision);
  EXPECT_GT(project_.dependencies().work_stats().saved_scene_read_count,
            retained_work.saved_scene_read_count);
  const auto saved_graph = assets.material_dependencies();
  ASSERT_TRUE(saved_graph.ok()) << saved_graph.error_message();
  EXPECT_EQ(saved_graph.value().revision, saved.value().revision);
  EXPECT_TRUE(
      std::ranges::any_of(saved_graph.value().scenes, [&](const auto &scene) {
        return !scene.live &&
               scene.project_relative_path == "Scenes/Usage.qdr" &&
               std::ranges::find(scene.material_ids, material.id) !=
                   scene.material_ids.end();
      }));

  const auto saved_path = root() / "Scenes" / "Usage.qdr";
  {
    std::fstream stream(saved_path,
                        std::ios::binary | std::ios::in | std::ios::out);
    ASSERT_TRUE(stream.is_open());
    stream.put('X');
    ASSERT_TRUE(stream.good());
  }
  const auto incomplete_graph = assets.material_dependencies(true);
  ASSERT_TRUE(incomplete_graph.ok()) << incomplete_graph.error_message();
  EXPECT_FALSE(incomplete_graph.value().complete);
  EXPECT_FALSE(incomplete_graph.value().errors.empty());
  EXPECT_GT(incomplete_graph.value().revision, saved.value().revision);
  const auto incomplete_work = project_.dependencies().work_stats();
  const auto incomplete = assets.material_dependency_revision();
  ASSERT_TRUE(incomplete.ok()) << incomplete.error_message();
  EXPECT_EQ(incomplete.value().project_id, incomplete_graph.value().project_id);
  EXPECT_EQ(incomplete.value().project_generation,
            project_.activation_generation());
  EXPECT_EQ(incomplete.value().revision, incomplete_graph.value().revision);
  EXPECT_FALSE(incomplete.value().complete);
  EXPECT_EQ(project_.dependencies().work_stats(), incomplete_work);
}

TEST_F(ProjectMaterialFolderApiTests,
       AlbedoTextureInfoReturnsValidatedFullResolutionMetadataAndPixels) {
  const auto alpha_source = temporary_.path() / "api_alpha.png";
  QImage alpha_image(2, 1, QImage::Format_RGBA8888);
  alpha_image.setPixelColor(0, 0, QColor(10, 20, 30, 40));
  alpha_image.setPixelColor(1, 0, QColor(200, 150, 100, 255));
  ASSERT_TRUE(
      alpha_image.save(QString::fromStdWString(alpha_source.wstring()), "PNG"));
  const auto imported = editor_.project_assets().import_textures_as_materials(
      std::array{alpha_source});
  ASSERT_TRUE(imported.ok()) << imported.error_message();
  ASSERT_EQ(imported.value().imported.size(), 1u);
  const ProjectAssetId material = imported.value().imported.front().material.id;

  const auto info =
      editor_.project_assets().material_albedo_texture_info(material);
  ASSERT_TRUE(info.ok()) << info.error_message();
  EXPECT_EQ(info.value().material_id, material);
  EXPECT_TRUE(info.value().texture_id.valid());
  EXPECT_EQ(info.value().name, "api_alpha.png");
  EXPECT_EQ(info.value().project_relative_path, "Textures/api_alpha.png");
  EXPECT_EQ(info.value().absolute_path, root() / "Textures" / "api_alpha.png");
  EXPECT_TRUE(std::filesystem::is_regular_file(info.value().absolute_path));
  EXPECT_EQ(info.value().format, "PNG");
  EXPECT_EQ(info.value().encoded_size_bytes,
            std::filesystem::file_size(info.value().absolute_path));
  EXPECT_EQ(info.value().width, 2u);
  EXPECT_EQ(info.value().height, 1u);
  EXPECT_TRUE(info.value().has_alpha_channel);
  EXPECT_NE(info.value().content_revision, 0u);
  ASSERT_NE(info.value().rgba8, nullptr);
  ASSERT_EQ(info.value().rgba8->size(), 8u);
  const auto channel = [&](std::size_t offset) {
    return std::to_integer<std::uint8_t>((*info.value().rgba8)[offset]);
  };
  EXPECT_EQ(channel(0u), 10u);
  EXPECT_EQ(channel(1u), 20u);
  EXPECT_EQ(channel(2u), 30u);
  EXPECT_EQ(channel(3u), 40u);
  EXPECT_EQ(channel(4u), 200u);
  EXPECT_EQ(channel(5u), 150u);
  EXPECT_EQ(channel(6u), 100u);
  EXPECT_EQ(channel(7u), 255u);

  auto &documents = editor_.standard_material_documents();
  ASSERT_TRUE(documents.open(material).ok());
  const auto material_document = documents.current();
  ASSERT_TRUE(material_document.ok());
  ASSERT_TRUE(
      material_document.value().material.base_color_texture.has_value());
  EXPECT_EQ(material_document.value().material.base_color_texture->id,
            info.value().texture_id);
  const auto repeated =
      editor_.project_assets().material_albedo_texture_info(material);
  ASSERT_TRUE(repeated.ok()) << repeated.error_message();
  EXPECT_EQ(repeated.value().texture_id, info.value().texture_id);
  EXPECT_EQ(repeated.value().content_revision, info.value().content_revision);
  EXPECT_EQ(repeated.value().rgba8, info.value().rgba8);

  ASSERT_TRUE(documents.close().ok());
  const auto rgb_source = temporary_.path() / "api_rgb.png";
  QImage rgb_image(3, 2, QImage::Format_RGB888);
  rgb_image.fill(QColor(17, 34, 51));
  ASSERT_TRUE(
      rgb_image.save(QString::fromStdWString(rgb_source.wstring()), "PNG"));
  const auto imported_rgb =
      editor_.project_assets().import_textures_as_materials(
          std::array{rgb_source});
  ASSERT_TRUE(imported_rgb.ok()) << imported_rgb.error_message();
  ASSERT_EQ(imported_rgb.value().imported.size(), 1u);
  const auto rgb_info = editor_.project_assets().material_albedo_texture_info(
      imported_rgb.value().imported.front().material.id);
  ASSERT_TRUE(rgb_info.ok()) << rgb_info.error_message();
  EXPECT_EQ(rgb_info.value().width, 3u);
  EXPECT_EQ(rgb_info.value().height, 2u);
  EXPECT_FALSE(rgb_info.value().has_alpha_channel);
  ASSERT_NE(rgb_info.value().rgba8, nullptr);
  EXPECT_EQ(rgb_info.value().rgba8->size(), 3u * 2u * 4u);

  EXPECT_FALSE(editor_.project_assets()
                   .material_albedo_texture_info(ProjectAssetId::generate())
                   .ok());
  const auto untextured = documents.new_material();
  ASSERT_TRUE(untextured.ok()) << untextured.error_message();
  ASSERT_TRUE(documents.set_name("No Albedo").ok());
  ASSERT_TRUE(
      documents.save_as(relative_path("Materials/No Albedo.qmat")).ok());
  EXPECT_FALSE(editor_.project_assets()
                   .material_albedo_texture_info(untextured.value().id)
                   .ok());
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
  EXPECT_TRUE(project_.manifest()->is_material_deleted(imported.id));
}

TEST_F(ProjectMaterialFolderApiTests,
       DeletionPreservesDirtySceneEditsAndUpdatesClosedSavedScenes) {
  auto &assets = editor_.project_assets();
  auto &documents = editor_.scene_documents();
  ASSERT_TRUE(assets.create_material_folder("Materials", "Disposable").ok());
  const auto imported = import_material("Materials/Disposable");
  ASSERT_TRUE(imported.id.valid());
  const auto box = editor_.modeling().create_box({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                 .max = {1.0f, 1.0f, 1.0f}},
      .name = "Saved Name",
      .material = QuaderMaterialReference::project(imported.id),
  });
  ASSERT_TRUE(box.ok()) << box.error_message();
  const auto first_id = documents.active_scene_id();
  ASSERT_TRUE(first_id.has_value());
  ASSERT_TRUE(documents.save_as(*first_id, relative_path("Scenes/Used.qdr")).ok());
  ASSERT_TRUE(editor_.object(box.value()).rename("Unrelated Dirty Name").ok());
  const auto second_scene = documents.new_scene();
  ASSERT_TRUE(second_scene.ok());
  ASSERT_TRUE(editor_.modeling()
                  .create_box({
                      .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                                 .max = {1.0f, 1.0f, 1.0f}},
                      .name = "Closed Saved Box",
                      .material = QuaderMaterialReference::project(imported.id),
                  })
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
  const auto deleted = assets.delete_material_folder("Materials/Disposable",
                                                      preview.value().revision);
  ASSERT_TRUE(deleted.ok()) << deleted.error_message();
  EXPECT_TRUE(deleted.changed());
  EXPECT_EQ(assets_events_.size(), 1u);
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

TEST_F(ProjectMaterialFolderApiTests,
       LegacyMaterialOperationsShareMixedProjectFoldersAndRecycleOriginalPaths) {
  auto &assets = editor_.project_assets();
  ASSERT_TRUE(assets.create_material_folder("", "Content").ok());
  const auto imported = import_material("Materials", "MovedWood");
  ASSERT_TRUE(imported.id.valid());
  ASSERT_TRUE(assets.move_materials(std::array{imported.id}, "Content").ok());
  ASSERT_TRUE(assets.rename_material_folder("Content", "Shared").ok());
  const auto renamed = assets.rename_material(imported.id, "Oak");
  ASSERT_TRUE(renamed.ok()) << renamed.error_message();
  EXPECT_EQ(renamed.value().project_relative_path, "Shared/Oak.qmat");
  const auto duplicated = assets.duplicate_materials(std::array{imported.id});
  ASSERT_TRUE(duplicated.ok()) << duplicated.error_message();
  ASSERT_EQ(duplicated.value().size(), 1u);
  EXPECT_EQ(duplicated.value().front().project_relative_path, "Shared/Oak Copy.qmat");
  const auto copied = assets.copy_materials(std::array{imported.id}, "");
  ASSERT_TRUE(copied.ok()) << copied.error_message();
  ASSERT_EQ(copied.value().size(), 1u);
  EXPECT_EQ(copied.value().front().project_relative_path, "Oak Copy.qmat");

  assets_events_.clear();
  const auto preview = assets.preview_delete_materials(std::array{imported.id});
  ASSERT_TRUE(preview.ok());
  ASSERT_TRUE(preview.value().complete) << ::testing::PrintToString(preview.value().errors);
  EXPECT_EQ(preview.value().material_names, std::vector<std::string>{"Oak"});
  const auto deleted = assets.delete_materials(std::array{imported.id}, preview.value().revision);
  ASSERT_TRUE(deleted.ok()) << deleted.error_message();
  EXPECT_EQ(assets_events_.size(), 1u);
  EXPECT_EQ(recycled_paths_, std::vector<std::filesystem::path>{root() / "Shared/Oak.qmat"});
  EXPECT_FALSE(material_info(imported.id).has_value());
  EXPECT_TRUE(material_info(duplicated.value().front().id).has_value());
  EXPECT_TRUE(material_info(copied.value().front().id).has_value());
}

TEST_F(ProjectMaterialFolderApiTests,
       LegacyFolderDeletionCannotHideSceneOrTextureDependenciesFromItsPreview) {
  auto &assets = editor_.project_assets();
  ASSERT_TRUE(assets.create_material_folder("", "Mixed").ok());
  const auto material = assets.create_material_asset("Mixed", "Surface");
  ASSERT_TRUE(material.ok());
  const auto scene = assets.create_scene_asset("Mixed", "Level");
  ASSERT_TRUE(scene.ok());
  const auto preview = assets.preview_delete_material_folder("Mixed");
  ASSERT_TRUE(preview.ok()) << preview.error_message();
  EXPECT_FALSE(preview.value().complete);
  EXPECT_FALSE(preview.value().errors.empty());
  assets_events_.clear();
  const auto deleted = assets.delete_material_folder("Mixed", preview.value().revision);
  EXPECT_FALSE(deleted.ok());
  EXPECT_FALSE(deleted.changed());
  EXPECT_TRUE(recycled_paths_.empty());
  EXPECT_TRUE(assets_events_.empty());
  EXPECT_TRUE(std::filesystem::exists(root() / "Mixed/Surface.qmat"));
  EXPECT_TRUE(std::filesystem::exists(root() / "Mixed/Level.qdr"));
  const auto generic = assets.preview_delete_assets(std::array{std::string("Mixed")});
  ASSERT_TRUE(generic.ok());
  EXPECT_TRUE(generic.value().complete);
  EXPECT_EQ(generic.value().scene_ids, std::vector{scene.value().scene_id});
}

TEST_F(ProjectMaterialFolderApiTests,
       MissingRecyclePortCannotFallBackToPermanentMaterialDeletion) {
  auto &assets = editor_.project_assets();
  const auto imported = import_material("Materials", "KeepMe");
  ASSERT_TRUE(imported.id.valid());
  QuaderProjectAssetsApiInternalAccess::set_recycle_port(assets, {});
  const auto preview = assets.preview_delete_materials(std::array{imported.id});
  ASSERT_TRUE(preview.ok());
  ASSERT_TRUE(preview.value().complete);
  assets_events_.clear();
  const auto deleted = assets.delete_materials(std::array{imported.id}, preview.value().revision);
  EXPECT_FALSE(deleted.ok());
  EXPECT_FALSE(deleted.changed());
  EXPECT_FALSE(deleted.error_message().empty());
  EXPECT_TRUE(assets_events_.empty());
  EXPECT_TRUE(std::filesystem::exists(root() / imported.project_relative_path));
  EXPECT_TRUE(material_info(imported.id).has_value());
  EXPECT_FALSE(project_.manifest()->is_material_deleted(imported.id));
}

} // namespace
} // namespace quader

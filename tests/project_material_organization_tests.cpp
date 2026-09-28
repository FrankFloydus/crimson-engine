////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "assets/default_material_catalog.hpp"
#include "editor/material/standard_material_edit_session_service.hpp"
#include "editor/session/project_session.hpp"
#include "io/project/qmat_codec.hpp"
#include "io/qdr/qdr_atomic_write.hpp"

#include <gtest/gtest.h>

#include <QImage>
#include <QString>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace quader {
namespace {

/// Exercises organization through public editor intent against real project files.
class ProjectMaterialOrganizationTests : public testing::Test {
protected:
  ProjectMaterialOrganizationTests() : editor_(project_) {}

  void SetUp() override {
    temporary_ = std::filesystem::temp_directory_path() /
                 ("quader-material-organization-" +
                  std::string(ProjectAssetId::generate().str()));
    root_ = temporary_ / "Project";
    std::filesystem::create_directories(root_);
    project_.set_resource_root(std::filesystem::path(QUADER_SOURCE_DIR) /
                               "resources" / "modeling_assets");
    const auto created = editor_.project().create(root_);
    ASSERT_TRUE(created.ok()) << created.error_message();
  }

  void TearDown() override {
    (void)project_.close(true);
    std::error_code ignored;
    std::filesystem::remove_all(temporary_, ignored);
  }

  void folder(std::string_view parent, std::string_view name) {
    const auto created = editor_.project_assets().create_material_folder(parent,
                                                                        name);
    ASSERT_TRUE(created.ok()) << created.error_message();
    ASSERT_TRUE(created.changed());
  }

  std::filesystem::path image(std::string_view name = "oak.png") {
    const auto path = temporary_ / std::string(name);
    QImage bitmap(3, 2, QImage::Format_RGBA8888);
    bitmap.fill(0xff986c42u);
    EXPECT_TRUE(bitmap.save(QString::fromStdWString(path.wstring()), "PNG"));
    return path;
  }

  ProjectAssetId import_image(std::string_view destination = "Materials",
                        std::string_view name = "oak.png") {
    const std::array sources{image(name)};
    const auto result = editor_.project_assets().import_textures_as_materials(
        sources, destination);
    EXPECT_TRUE(result.ok()) << result.error_message();
    if (!result.ok() || result.value().imported.empty()) {
      return {};
    }
    return result.value().imported.front().material.id;
  }

  std::string material_path(ProjectAssetId id) {
    const auto *entry = project_.material_catalog().find(id);
    EXPECT_NE(entry, nullptr);
    return entry == nullptr ? std::string{} : entry->path.portable_string();
  }

  std::vector<std::string> folder_paths() {
    std::vector<std::string> folders;
    for (const auto &path : project_.material_catalog().folders()) {
      folders.push_back(path.portable_string());
    }
    return folders;
  }

  std::filesystem::path temporary_;
  std::filesystem::path root_;
  ProjectSession project_;
  QuaderEditorApi editor_;
};

TEST_F(ProjectMaterialOrganizationTests,
       EmptyFoldersPublishSortedCatalogRevisionAndSurviveReopening) {
  const auto before = project_.material_catalog().content_revision();
  folder("Materials", "Wood");
  folder("Materials", "Dev");
  folder("Materials/Dev", "Empty");
  const std::vector<std::string> expected{
      "Materials", "Materials/Dev", "Materials/Dev/Empty", "Materials/Wood",
      "Scenes", "Textures"};
  EXPECT_EQ(folder_paths(), expected);
  EXPECT_NE(project_.material_catalog().content_revision(), before);
  const auto manifest = editor_.project().manifest_path();
  ASSERT_TRUE(editor_.project().close().ok());
  ASSERT_TRUE(editor_.project().open(manifest).ok());
  EXPECT_EQ(folder_paths(), expected);
}

TEST_F(ProjectMaterialOrganizationTests,
       ImportsIntoSelectedFolderUseNumericMaterialCollisionsAndExternalTextures) {
  folder("Materials", "Dev");
  folder("Materials/Dev", "Wood");
  const auto first = import_image("Materials/Dev/Wood");
  const auto second = import_image("Materials/Dev/Wood");
  ASSERT_TRUE(first.valid());
  ASSERT_TRUE(second.valid());
  EXPECT_NE(first, second);
  EXPECT_EQ(material_path(first), "Materials/Dev/Wood/oak.qmat");
  EXPECT_EQ(material_path(second), "Materials/Dev/Wood/oak_2.qmat");
  for (auto id : {first, second}) {
    const auto *entry = project_.material_catalog().find(id);
    ASSERT_NE(entry, nullptr);
    const auto *standard = entry->material.standard_material();
    ASSERT_NE(standard, nullptr);
    ASSERT_TRUE(standard->base_color_texture.has_value());
    EXPECT_TRUE(standard->base_color_texture->project_relative_path.starts_with(
        "Textures/"));
  }
  const auto *second_standard = project_.material_catalog().find(second)->material.standard_material();
  ASSERT_NE(second_standard, nullptr);
  ASSERT_TRUE(second_standard->base_color_texture.has_value());
  const auto collision_texture = ProjectRelativePath::parse_portable(
      second_standard->base_color_texture->project_relative_path);
  ASSERT_TRUE(collision_texture.ok());
  const std::vector<std::string> expected{
      "Materials", "Materials/Dev", "Materials/Dev/Wood", "Scenes", "Textures",
      collision_texture.value().native_path().parent_path().generic_string()};
  EXPECT_EQ(folder_paths(), expected);
}

TEST_F(ProjectMaterialOrganizationTests,
       InvalidImportDestinationsDoNotPublishOrWriteSourceFiles) {
  const std::array sources{image()};
  const auto revision = project_.material_catalog().content_revision();
  const auto count = project_.material_catalog().entries().size();
  for (std::string_view folder : {".quader", "../Textures", "Materials/Missing",
                                  "Materials/../Textures", "materials",
                                  "Materials/.", "Materials//Missing"}) {
    const auto result = editor_.project_assets().import_textures_as_materials(
        sources, folder);
    EXPECT_FALSE(result.ok()) << folder;
    EXPECT_FALSE(result.changed()) << folder;
  }
  EXPECT_EQ(project_.material_catalog().content_revision(), revision);
  EXPECT_EQ(project_.material_catalog().entries().size(), count);
  EXPECT_FALSE(std::filesystem::exists(root_ / "Materials" / "oak.qmat"));
  EXPECT_FALSE(std::filesystem::exists(root_ / "Textures" / "oak.png"));
}

TEST_F(ProjectMaterialOrganizationTests,
       MaterialImportsUseAnyAuthoredProjectFolderIncludingRoot) {
  const auto root_material = import_image({}, "root.png");
  ASSERT_TRUE(root_material.valid());
  EXPECT_EQ(material_path(root_material), "root.qmat");
  const auto mixed_material = import_image("Textures", "mixed.png");
  ASSERT_TRUE(mixed_material.valid());
  EXPECT_EQ(material_path(mixed_material), "Textures/mixed.qmat");
  EXPECT_TRUE(std::filesystem::exists(root_ / "Textures/mixed.png"));
}

TEST_F(ProjectMaterialOrganizationTests,
       FolderRenameNestingAndUnnestingPreserveMaterialIdentityAndTextures) {
  folder("Materials", "Dev");
  folder("Materials", "Wood");
  folder("Materials/Wood", "Empty");
  const auto id = import_image("Materials/Wood");
  ASSERT_TRUE(id.valid());
  const auto saved = *project_.material_catalog().find(id);
  ASSERT_TRUE(editor_.project_assets().move_material_folder(
      "Materials/Wood", "Materials/Dev").ok());
  EXPECT_EQ(material_path(id), "Materials/Dev/Wood/oak.qmat");
  EXPECT_TRUE(std::filesystem::is_directory(root_ / "Materials/Dev/Wood/Empty"));
  ASSERT_TRUE(editor_.project_assets().rename_material_folder(
      "Materials/Dev/Wood", "Timber").ok());
  EXPECT_EQ(material_path(id), "Materials/Dev/Timber/oak.qmat");
  ASSERT_TRUE(editor_.project_assets().move_material_folder(
      "Materials/Dev/Timber", "Materials").ok());
  EXPECT_EQ(material_path(id), "Materials/Timber/oak.qmat");
  const auto *after = project_.material_catalog().find(id);
  ASSERT_NE(after, nullptr);
  EXPECT_EQ(after->material, saved.material);
  EXPECT_EQ(after->content_revision, saved.content_revision);
  EXPECT_EQ(after->file_size, saved.file_size);
}

TEST_F(ProjectMaterialOrganizationTests, FolderRenameCanChangeOnlyItsCasing) {
  folder("Materials", "wood");
  const auto id = import_image("Materials/wood");
  const auto renamed = editor_.project_assets().rename_material_folder(
      "Materials/wood", "Wood");
  ASSERT_TRUE(renamed.ok()) << renamed.error_message();
  EXPECT_EQ(material_path(id), "Materials/Wood/oak.qmat");
  const std::vector<std::string> expected{
      "Materials", "Materials/Wood", "Scenes", "Textures"};
  EXPECT_EQ(folder_paths(), expected);
}

TEST_F(ProjectMaterialOrganizationTests,
       BatchMoveCollisionRejectsEveryMemberAndDoesNotOverwriteMaterials) {
  folder("Materials", "A");
  folder("Materials", "B");
  folder("Materials", "Target");
  const auto first = import_image("Materials/A");
  const auto second = import_image("Materials/B");
  const std::array ids{first, second};
  const auto revision = project_.material_catalog().content_revision();
  const auto failed = editor_.project_assets().move_materials(ids,
                                                            "Materials/Target");
  EXPECT_FALSE(failed.ok());
  EXPECT_FALSE(failed.changed());
  EXPECT_EQ(material_path(first), "Materials/A/oak.qmat");
  EXPECT_EQ(material_path(second), "Materials/B/oak.qmat");
  EXPECT_EQ(project_.material_catalog().content_revision(), revision);
  EXPECT_TRUE(std::filesystem::is_empty(root_ / "Materials/Target"));
}

TEST_F(ProjectMaterialOrganizationTests,
       BatchMovesPreserveDirtyDraftPendingTextureAndMaterialIdentity) {
  folder("Materials", "Target");
  const auto first = import_image("Materials", "oak.png");
  const auto second = import_image("Materials", "stone.png");
  auto &draft = project_.standard_material_editor();
  ASSERT_TRUE(draft.open(first).ok);
  ASSERT_TRUE(draft.set_name("Unsaved name").ok);
  ASSERT_TRUE(draft.import_texture(StandardMaterialTextureSlot::Roughness,
                                   image("pending.png")).ok);
  const auto before = *draft.current();
  const std::array ids{first, second};
  const auto moved = editor_.project_assets().move_materials(ids,
                                                           "Materials/Target");
  ASSERT_TRUE(moved.ok()) << moved.error_message();
  EXPECT_EQ(material_path(first), "Materials/Target/oak.qmat");
  EXPECT_EQ(material_path(second), "Materials/Target/stone.qmat");
  ASSERT_NE(draft.current(), nullptr);
  EXPECT_EQ(draft.current()->document, before.document);
  EXPECT_EQ(draft.current()->session_id, before.session_id);
  EXPECT_EQ(draft.current()->pending_texture_sources, before.pending_texture_sources);
  EXPECT_EQ(draft.current()->source_content_revision, before.source_content_revision);
  EXPECT_TRUE(draft.current()->dirty);
  EXPECT_EQ(draft.current()->path->portable_string(), "Materials/Target/oak.qmat");
  EXPECT_GT(draft.current()->draft_revision, before.draft_revision);
  ASSERT_NE(project_.material_catalog().find(first), nullptr);
  EXPECT_EQ(project_.material_catalog().find(first)->material.id, first);
  ASSERT_TRUE(draft.save().ok);
  EXPECT_EQ(project_.material_catalog().find(first)->material.name, "Unsaved name");
  EXPECT_FALSE(std::filesystem::exists(root_ / "Materials/oak.qmat"));
}

TEST_F(ProjectMaterialOrganizationTests,
       RootDefaultCyclesInvalidNamesAndUnmanagedFolderFilesAreProtected) {
  folder("Materials", "Dev");
  folder("Materials/Dev", "Wood");
  const auto default_id = ProjectAssetId::parse(kDefaultProjectMaterialId);
  ASSERT_TRUE(default_id.has_value());
  const std::array defaults{*default_id};
  EXPECT_FALSE(editor_.project_assets().move_materials(defaults,
                                                     "Materials/Dev").ok());
  EXPECT_FALSE(editor_.project_assets().move_material_folder(
      "Materials", "Materials/Dev").ok());
  EXPECT_FALSE(editor_.project_assets().rename_material_folder(
      "Materials", "Root").ok());
  EXPECT_FALSE(editor_.project_assets().move_material_folder(
      "Materials/Dev", "Materials/Dev/Wood").ok());
  for (std::string_view name : {"..", "Bad/Name", "Folder.", "COM1", "NUL.txt"}) {
    EXPECT_FALSE(editor_.project_assets().create_material_folder(
        "Materials", name).ok()) << name;
  }
  const auto collision = editor_.project_assets().create_material_folder(
      "Materials", "dev");
  EXPECT_TRUE(collision.ok()) << collision.error_message();
  EXPECT_TRUE(std::filesystem::is_directory(root_ / "Materials/Dev"));
  EXPECT_TRUE(std::filesystem::is_directory(root_ / "Materials/dev_2"));
  std::ofstream(root_ / "Materials/Dev/Wood/notes.txt") << "Keep this file";
  const auto revision = project_.material_catalog().content_revision();
  EXPECT_FALSE(editor_.project_assets().move_material_folder(
      "Materials/Dev/Wood", "Materials").ok());
  EXPECT_TRUE(std::filesystem::exists(root_ / "Materials/Dev/Wood/notes.txt"));
  EXPECT_EQ(project_.material_catalog().content_revision(), revision);
}

TEST_F(ProjectMaterialOrganizationTests, HardLinkedMaterialCannotBeMoved) {
  folder("Materials", "Target");
  const auto id = import_image();
  std::error_code error;
  std::filesystem::create_hard_link(root_ / material_path(id),
                                   temporary_ / "alias.qmat", error);
  if (error) {
    GTEST_SKIP() << "Hard links unavailable: " << error.message();
  }
  const std::array ids{id};
  EXPECT_FALSE(editor_.project_assets().move_materials(ids,
                                                     "Materials/Target").ok());
  EXPECT_EQ(material_path(id), "Materials/oak.qmat");
}

TEST_F(ProjectMaterialOrganizationTests, AliasedDestinationRejectsImportBeforeWriting) {
  std::error_code error;
  std::filesystem::create_directory_symlink(root_ / "Textures",
                                            root_ / "Materials/Alias", error);
  if (error) {
    GTEST_SKIP() << "Directory symlinks unavailable: " << error.message();
  }
  const std::array sources{image()};
  const auto imported = editor_.project_assets().import_textures_as_materials(
      sources, "Materials/Alias");
  EXPECT_FALSE(imported.ok());
  EXPECT_FALSE(std::filesystem::exists(root_ / "Textures/oak.qmat"));
  EXPECT_FALSE(std::filesystem::exists(root_ / "Textures/oak.png"));
}

TEST_F(ProjectMaterialOrganizationTests, ExternalCatalogChangesRejectStaleMoveIntent) {
  folder("Materials", "Target");
  const auto id = import_image();
  const auto revision = project_.material_catalog().content_revision();
  std::filesystem::create_directory(root_ / "Materials/External");
  const std::array ids{id};
  const auto result = editor_.project_assets().move_materials(ids,
                                                            "Materials/Target");
  EXPECT_FALSE(result.ok());
  EXPECT_FALSE(result.changed());
  EXPECT_EQ(project_.material_catalog().content_revision(), revision);
  EXPECT_EQ(material_path(id), "Materials/oak.qmat");
  ASSERT_TRUE(editor_.project_assets().assets(true).ok());
  const auto refreshed = editor_.project_assets().move_materials(ids, "Materials/Target");
  ASSERT_TRUE(refreshed.ok()) << refreshed.error_message();
  EXPECT_EQ(material_path(id), "Materials/Target/oak.qmat");
}

TEST_F(ProjectMaterialOrganizationTests,
       ExternalMaterialEditsRejectLazySnapshotMovesUntilExplicitRefresh) {
  folder("Materials", "Target");
  const auto id = import_image();
  const auto before = *project_.material_catalog().find(id);
  auto externally_edited = before.material;
  externally_edited.standard_material()->roughness = 0.25f;
  ASSERT_TRUE(QmatCodec::write_atomic(project_.workspace()->file_system,
      before.path, externally_edited).ok());
  const std::array ids{id};
  const auto rejected = editor_.project_assets().move_materials(ids, "Materials/Target");
  EXPECT_FALSE(rejected.ok());
  EXPECT_FALSE(rejected.changed());
  ASSERT_NE(project_.material_catalog().find(id), nullptr);
  EXPECT_EQ(*project_.material_catalog().find(id), before);
  EXPECT_FALSE(std::filesystem::exists(root_ / "Materials/Target/oak.qmat"));
  const auto retained = QmatCodec::read(project_.workspace()->file_system, before.path);
  ASSERT_TRUE(retained.ok());
  EXPECT_EQ(retained.value().material, externally_edited);
  ASSERT_TRUE(editor_.project_assets().assets(true).ok());
  const auto refreshed = editor_.project_assets().move_materials(ids, "Materials/Target");
  ASSERT_TRUE(refreshed.ok()) << refreshed.error_message();
  EXPECT_EQ(project_.material_catalog().find(id)->material, externally_edited);
  EXPECT_EQ(material_path(id), "Materials/Target/oak.qmat");
}

TEST_F(ProjectMaterialOrganizationTests, NoReplaceMovePreservesCompetingDestination) {
  const auto source = temporary_ / "source.txt";
  const auto destination = temporary_ / "destination.txt";
  std::ofstream(source) << "Source";
  std::ofstream(destination) << "Destination";
  const auto result = quader_io::move_project_entry_no_replace(source,
                                                              destination);
  EXPECT_FALSE(result.ok);
  EXPECT_TRUE(std::filesystem::exists(source));
  std::ifstream stream(destination);
  std::string text;
  stream >> text;
  EXPECT_EQ(text, "Destination");
}

} // namespace
} // namespace quader

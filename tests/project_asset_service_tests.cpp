////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/assets/project_asset_service.hpp"

#include "api/quader_editor_api.hpp"
#include "assets/default_material_catalog.hpp"
#include "editor/assets/project_asset_deletion_service.hpp"
#include "editor/material/standard_material_edit_session_service.hpp"
#include "editor/session/project_session.hpp"
#include "io/project/qmat_codec.hpp"
#include "io/qdr/qdr_document_reader.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace quader {
namespace {

TEST(ProjectAssetLatencyTests, ClonedProjectOperations) {
  const char *source_path = std::getenv("QUADER_ASSET_LATENCY_PROJECT");
  if (source_path == nullptr || *source_path == '\0') {
    GTEST_SKIP() << "Set QUADER_ASSET_LATENCY_PROJECT to a read-only source project.";
  }
  const auto source = std::filesystem::u8path(source_path);
  ASSERT_TRUE(std::filesystem::is_regular_file(source));
  const auto temporary = std::filesystem::temp_directory_path() /
      ("quader-asset-latency-" + std::string(ProjectAssetId::generate().str()));
  std::filesystem::create_directories(temporary);
  /// Removes only the fresh fixture copies, including injected recycled files.
  struct Cleanup final {
    std::filesystem::path directory;
    ~Cleanup() {
      std::error_code ignored;
      std::filesystem::remove_all(directory, ignored);
    }
  } cleanup{temporary};
  for (int sample = 0; sample < 3; ++sample) {
    const auto root = temporary / std::to_string(sample) / source.parent_path().filename();
    std::filesystem::create_directories(root.parent_path());
    std::filesystem::copy(source.parent_path(), root,
        std::filesystem::copy_options::recursive |
        std::filesystem::copy_options::skip_symlinks);
    ProjectSession project(temporary / ("recent-" + std::to_string(sample) + ".json"));
    project.set_resource_root(std::filesystem::path(QUADER_SOURCE_DIR) /
                              "resources/modeling_assets");
    const auto measured = [&](std::string_view operation, auto &&action) {
      const auto started = std::chrono::steady_clock::now();
      auto result = action();
      const double milliseconds = std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - started).count();
      std::cout << "ASSET_LATENCY sample=" << sample << " operation=" << operation
                << " milliseconds=" << milliseconds << std::endl;
      return result;
    };
    const auto opened = measured("open", [&] { return project.open(root / source.filename()); });
    ASSERT_TRUE(opened.ok) << opened.message;
    auto &assets = project.project_assets();
    ASSERT_TRUE(measured("browse", [&] { return assets.snapshot(); }).ok());
    ASSERT_TRUE(measured("cached_browse", [&] { return assets.snapshot(); }).ok());
    QuaderEditorApi browser_editor(project);
    ASSERT_TRUE(measured("forced_reveal", [&] {
      return browser_editor.project_assets().assets(true);
    }).ok());
    const auto retained_work = assets.work_stats();
    const auto texture_revision = project.material_texture_revision();
    for (int reveal = 0; reveal < 5; ++reveal) {
      ASSERT_TRUE(measured("unchanged_reveal", [&] {
        return browser_editor.project_assets().assets_if_changed();
      }).ok());
    }
    EXPECT_EQ(assets.work_stats().scans, retained_work.scans);
    EXPECT_EQ(assets.work_stats().metadata_checks, retained_work.metadata_checks + 5u);
    EXPECT_EQ(project.material_texture_revision(), texture_revision);
    const auto folder = measured("create_folder", [&] {
      return assets.create_folder({}, "LatencyTarget");
    });
    ASSERT_TRUE(folder.ok) << folder.message;
    ASSERT_TRUE(measured("refresh_folder", [&] { return assets.snapshot(); }).ok());
    const auto scene = measured("create_scene", [&] {
      return assets.create_scene({}, "LatencyScene");
    });
    ASSERT_TRUE(scene.ok) << scene.message;
    ASSERT_TRUE(measured("refresh_scene", [&] { return assets.snapshot(); }).ok());
    const auto moved = measured("move_scene", [&] {
      return assets.move(scene.paths, folder.paths.front().portable_string());
    });
    ASSERT_TRUE(moved.ok) << moved.message;
    ASSERT_TRUE(measured("refresh_move", [&] { return assets.snapshot(); }).ok());
    ProjectAssetDeletionService deletion(project,
        [&](std::span<const std::filesystem::path> paths) {
          ProjectAssetRecycleResult result{.ok = true};
          const auto recycle_root = temporary / ("recycled-" + std::to_string(sample));
          std::filesystem::create_directories(recycle_root);
          for (const auto &path : paths) {
            EXPECT_TRUE(path.native().starts_with(root.native()));
            std::filesystem::rename(path, recycle_root / path.filename());
            result.items.push_back({.original_path = path, .recycled = true});
          }
          return result;
        });
    const auto preview = measured("delete_preview", [&] { return deletion.preview(moved.paths); });
    ASSERT_TRUE(preview.complete) << testing::PrintToString(preview.errors);
    const auto erased = measured("delete_scene", [&] {
      return deletion.erase(moved.paths, preview.revision);
    });
    ASSERT_TRUE(erased.ok) << erased.message;
    ASSERT_TRUE(measured("refresh_delete", [&] { return assets.snapshot(); }).ok());
    const auto material = std::ranges::find_if(project.material_catalog().entries(), [](const auto &entry) {
      return entry.material.id.str() != kDefaultProjectMaterialId;
    });
    if (material != project.material_catalog().entries().end()) {
      const std::array material_paths{material->path};
      const auto moved_material = measured("move_material", [&] {
        return assets.move(material_paths, folder.paths.front().portable_string());
      });
      ASSERT_TRUE(moved_material.ok) << moved_material.message;
      ASSERT_TRUE(measured("refresh_material_move", [&] { return assets.snapshot(); }).ok());
      QuaderEditorApi editor(project);
      ASSERT_TRUE(measured("material_projection", [&] {
        return editor.project_assets().material_presentations();
      }).ok());
      ASSERT_TRUE(measured("material_projection_warm", [&] {
        return editor.project_assets().material_presentations();
      }).ok());
      const auto &original_path = material_paths.front().portable_string();
      const auto separator = original_path.find_last_of('/');
      const auto original_folder = separator == std::string::npos
          ? std::string{} : original_path.substr(0u, separator);
      const auto returned_material = measured("move_material_back", [&] {
        return assets.move(moved_material.paths, original_folder);
      });
      ASSERT_TRUE(returned_material.ok) << returned_material.message;
      ASSERT_TRUE(measured("refresh_material_return", [&] { return assets.snapshot(); }).ok());
      ASSERT_TRUE(measured("material_projection_again", [&] {
        return editor.project_assets().material_presentations();
      }).ok());
    }
    ASSERT_TRUE(project.close(true).ok);
  }
}

/// Owns a disposable project for mixed physical asset operations.
class ProjectAssetServiceTests : public testing::Test {
protected:
  void SetUp() override {
    temporary_ = std::filesystem::temp_directory_path() /
        ("quader-asset-service-" + std::string(ProjectAssetId::generate().str()));
    std::filesystem::create_directories(temporary_ / "Project");
    project_.set_resource_root(std::filesystem::path(QUADER_SOURCE_DIR) /
                               "resources/modeling_assets");
    const auto created = project_.create(temporary_ / "Project");
    ASSERT_TRUE(created.ok) << created.message;
  }

  void TearDown() override {
    assets().set_mutation_prepare({});
    (void)project_.close(true);
    std::error_code error;
    std::filesystem::remove_all(temporary_, error);
  }

  static ProjectRelativePath path(std::string_view text) {
    const auto parsed = ProjectRelativePath::parse_portable(text);
    EXPECT_TRUE(parsed.ok()) << parsed.error().message;
    return parsed.ok() ? parsed.value() : ProjectRelativePath{};
  }

  std::filesystem::path image_source() const {
    return std::filesystem::path(QUADER_SOURCE_DIR) /
           "resources/modeling_assets/materials/default/default_albedo.png";
  }

  std::filesystem::path root() const { return temporary_ / "Project"; }
  static std::string bytes(const std::filesystem::path& file){std::ifstream input(file,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
  static void write(const std::filesystem::path& file,std::string_view value){std::filesystem::create_directories(file.parent_path());std::ofstream output(file,std::ios::binary|std::ios::trunc);output.write(value.data(),static_cast<std::streamsize>(value.size()));}


  ProjectAssetService &assets() { return project_.project_assets(); }

  std::filesystem::path recovery_journal(nlohmann::json actions,
                                         std::string_view state = "prepared") {
    std::ifstream input(project_.manifest_path(), std::ios::binary);
    const std::string manifest{std::istreambuf_iterator<char>(input), {}};
    const auto relative = project_.workspace()->file_system.relative_from_absolute(project_.manifest_path());
    EXPECT_TRUE(relative.ok());
    std::filesystem::create_directories(root() / ".quader");
    const auto destination = root() / ".quader" /
        ("asset-organization-" + std::string(ProjectAssetId::generate().str()) + ".json");
    const nlohmann::json journal{
        {"version", 1}, {"state", state},
        {"project", std::string(project_.manifest()->id().str())},
        {"manifest", relative.value().portable_string()},
        {"baseline", manifest}, {"candidate", manifest}, {"actions", std::move(actions)},
    };
    std::ofstream(destination, std::ios::binary) << journal.dump(2);
    return destination;
  }

  std::filesystem::path temporary_;
  ProjectSession project_;
};

TEST_F(ProjectAssetServiceTests, RootContainsMixedAssetsAndEmptyFolders) {
  const auto active = project_.active_scene_id();
  const auto tabs = project_.open_scenes().size();
  ASSERT_TRUE(assets().create_folder({}, "Empty").ok);
  const auto scene = assets().create_scene({}, "Lobby");
  ASSERT_TRUE(scene.ok) << scene.message;
  ASSERT_TRUE(assets().create_material({}, "Paint").ok);
  const auto texture = assets().import_textures(std::array{image_source()}, {});
  ASSERT_TRUE(texture.ok) << texture.message;
  std::ofstream(root() / "notes.txt") << "unsupported source";
  const auto &snapshot = assets().refresh();
  ASSERT_TRUE(snapshot.ok()) << snapshot.error().message;
  EXPECT_EQ(project_.active_scene_id(), active);
  EXPECT_EQ(project_.open_scenes().size(), tabs);
  const auto has = [&](std::string_view relative, ProjectAssetKind kind) {
    return std::ranges::any_of(snapshot.value().entries, [&](const auto &entry) {
      return entry.path.portable_string() == relative && entry.kind == kind;
    });
  };
  EXPECT_TRUE(has("Empty", ProjectAssetKind::Folder));
  EXPECT_TRUE(has("Lobby.qdr", ProjectAssetKind::Scene));
  EXPECT_TRUE(has("Paint.qmat", ProjectAssetKind::Material));
  EXPECT_TRUE(has(texture.paths.front().portable_string(), ProjectAssetKind::Texture));
  EXPECT_TRUE(has("notes.txt",ProjectAssetKind::Unknown));
  EXPECT_FALSE(std::ranges::any_of(snapshot.value().entries, [](const auto &entry) {
    const auto value = entry.path.portable_string();
    return value.starts_with(".quader") || value.ends_with(".qproj");
  }));
}

TEST_F(ProjectAssetServiceTests, FolderOperationsPreserveArbitraryFileBytes) {
  ASSERT_TRUE(assets().create_folder({}, "Mixed").ok);
  ASSERT_TRUE(assets().create_scene("Mixed", "Scene").ok);
  const std::string raw("native\0source\xff",14);write(root()/"Mixed/source.native",raw);
  const auto renamed=assets().rename(path("Mixed"),"Renamed");ASSERT_TRUE(renamed.ok)<<renamed.message;
  EXPECT_EQ(bytes(root()/"Renamed/source.native"),raw);EXPECT_TRUE(std::filesystem::exists(root()/"Renamed/Scene.qdr"));EXPECT_FALSE(std::filesystem::exists(root()/"Mixed"));
  ASSERT_TRUE(assets().create_folder({},"Copies").ok);const auto copied=assets().copy(std::array{path("Renamed")},"Copies");ASSERT_TRUE(copied.ok)<<copied.message;
  EXPECT_EQ(bytes(root()/"Copies/Renamed/source.native"),raw);EXPECT_EQ(bytes(root()/"Renamed/source.native"),raw);
  EXPECT_TRUE(std::filesystem::exists(root()/"Copies/Renamed/Scene.qdr"));
}

TEST_F(ProjectAssetServiceTests, AssetDestinationsStayWithinProject) {
  const auto manifest_before = ProjectManifestStore::serialize(*project_.manifest());
  EXPECT_FALSE(assets().create_folder("../Outside", "Escaped").ok);
  EXPECT_FALSE(assets().create_scene(".quader", "Recovery").ok);
  EXPECT_FALSE(assets().create_material({}, "../Escaped").ok);
  EXPECT_FALSE(assets().create_folder({}, "CON").ok);
  EXPECT_EQ(ProjectManifestStore::serialize(*project_.manifest()), manifest_before);
  EXPECT_FALSE(std::filesystem::exists(temporary_ / "Outside"));
}

TEST_F(ProjectAssetServiceTests, TextureImportAndMoveKeepOneIdentityWithoutMaterialCreation) {
  const auto material_count = project_.material_catalog().entries().size();
  const auto imported = assets().import_textures(std::array{image_source()}, {});
  ASSERT_TRUE(imported.ok) << imported.message;
  ASSERT_EQ(imported.paths.size(), 1u);
  const auto *original = project_.manifest()->find_texture(imported.paths.front());
  ASSERT_NE(original, nullptr);
  const auto id = original->id;
  ASSERT_TRUE(assets().create_folder({}, "Art").ok);
  const auto moved = assets().move(imported.paths, "Art");
  ASSERT_TRUE(moved.ok) << moved.message;
  ASSERT_EQ(moved.paths.size(), 1u);
  const auto *after = project_.manifest()->find_texture(id);
  ASSERT_NE(after, nullptr);
  EXPECT_EQ(after->path, moved.paths.front());
  EXPECT_EQ(project_.material_catalog().entries().size(), material_count);
  EXPECT_FALSE(std::filesystem::exists(root() / imported.paths.front().native_path()));
  EXPECT_TRUE(std::filesystem::exists(root() / moved.paths.front().native_path()));
}

TEST_F(ProjectAssetServiceTests, MixedFolderCopyRemapsInternalIdentities) {
  ASSERT_TRUE(assets().create_folder({}, "Bundle").ok);
  const auto texture = assets().import_textures(std::array{image_source()}, "Bundle");
  ASSERT_TRUE(texture.ok) << texture.message;
  ASSERT_TRUE(assets().create_material("Bundle", "Paint").ok);
  const auto scene = assets().create_scene("Bundle", "World");
  ASSERT_TRUE(scene.ok) << scene.message;
  const auto original_scene_id = project_.manifest()->find_scene(path("Bundle/World.qdr"))->id;
  const auto *image = project_.manifest()->find_texture(texture.paths.front());
  ASSERT_NE(image, nullptr);
  const auto original_texture_id = image->id;
  auto material = QmatCodec::read(project_.workspace()->file_system, path("Bundle/Paint.qmat"));
  ASSERT_TRUE(material.ok()) << material.error().message;
  const auto original_material_id = material.value().material.id;
  auto document = material.value().material;
  document.standard_material()->base_color_texture = ProjectMaterialTextureSlot{
      .id = original_texture_id,
      .project_relative_path = texture.paths.front().portable_string()};
  ASSERT_TRUE(QmatCodec::write_atomic(project_.workspace()->file_system,
                                       path("Bundle/Paint.qmat"), document).ok());
  auto catalog = ProjectMaterialCatalog::scan(project_.workspace()->file_system,
                                              nullptr, project_.manifest());
  ASSERT_TRUE(catalog.ok());
  ASSERT_TRUE(project_.install_material_catalog(catalog.take_value()).ok);
  QuaderEditorApi editor(project_);
  const auto opened = editor.scene_documents().open(path("Bundle/World.qdr"));
  ASSERT_TRUE(opened.ok()) << opened.error_message();
  ASSERT_TRUE(editor.modeling().create_box({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Internal Material", .material = QuaderMaterialReference::project(original_material_id),
  }).ok());
  const auto default_id = *ProjectAssetId::parse(kDefaultProjectMaterialId);
  ASSERT_TRUE(editor.modeling().create_box({
      .bounds = {.min = {3.0f, -1.0f, -1.0f}, .max = {5.0f, 1.0f, 1.0f}},
      .name = "External Material", .material = QuaderMaterialReference::project(default_id),
  }).ok());
  ASSERT_TRUE(editor.scene_documents().save(opened.value()).ok());
  const auto cloned = assets().duplicate(std::array{path("Bundle")});
  ASSERT_TRUE(cloned.ok) << cloned.message;
  ASSERT_EQ(cloned.paths.size(), 1u);
  EXPECT_EQ(cloned.paths.front(), path("Bundle_2"));
  const auto copy = QmatCodec::read(project_.workspace()->file_system,
                                    path("Bundle_2/Paint.qmat"));
  ASSERT_TRUE(copy.ok()) << copy.error().message;
  EXPECT_NE(copy.value().material.id, original_material_id);
  const auto *copy_standard = copy.value().material.standard_material();
  ASSERT_NE(copy_standard, nullptr);
  ASSERT_TRUE(copy_standard->base_color_texture.has_value());
  EXPECT_NE(copy_standard->base_color_texture->id, original_texture_id);
  EXPECT_TRUE(copy_standard->base_color_texture->project_relative_path.starts_with("Bundle_2/"));
  const auto *copied_texture = project_.manifest()->find_texture(copy_standard->base_color_texture->id);
  ASSERT_NE(copied_texture, nullptr);
  EXPECT_EQ(copied_texture->path.portable_string(),
            copy_standard->base_color_texture->project_relative_path);
  const auto *copied_scene = project_.manifest()->find_scene(path("Bundle_2/World.qdr"));
  ASSERT_NE(copied_scene, nullptr);
  EXPECT_NE(copied_scene->id, original_scene_id);
  EXPECT_EQ(project_.manifest()->find_scene(path("Bundle/World.qdr"))->id,
            original_scene_id);
  const auto bindings = QdrDocumentReader::read_project_material_usage(root() / "Bundle_2/World.qdr");
  ASSERT_TRUE(bindings.ok) << bindings.message;
  EXPECT_NE(std::ranges::find(bindings.material_ids, copy.value().material.id), bindings.material_ids.end());
  EXPECT_NE(std::ranges::find(bindings.material_ids, default_id), bindings.material_ids.end());
  EXPECT_EQ(std::ranges::find(bindings.material_ids, original_material_id), bindings.material_ids.end());
}

TEST_F(ProjectAssetServiceTests, CopyUsesSavedMaterialAndPreservesDirtyDraft) {
  const auto created = assets().create_material({}, "Saved");
  ASSERT_TRUE(created.ok) << created.message;
  const auto read = QmatCodec::read(project_.workspace()->file_system, created.paths.front());
  ASSERT_TRUE(read.ok());
  ASSERT_TRUE(project_.standard_material_editor().open(read.value().material.id).ok);
  ASSERT_TRUE(project_.standard_material_editor().set_name("Unsaved Draft").ok);
  const auto duplicated = assets().duplicate(created.paths);
  ASSERT_TRUE(duplicated.ok) << duplicated.message;
  const auto copy = QmatCodec::read(project_.workspace()->file_system, duplicated.paths.front());
  ASSERT_TRUE(copy.ok());
  EXPECT_EQ(copy.value().material.name, "Saved Copy");
  ASSERT_NE(project_.standard_material_editor().current(), nullptr);
  EXPECT_TRUE(project_.standard_material_editor().current()->dirty);
  EXPECT_EQ(project_.standard_material_editor().current()->document.name, "Unsaved Draft");
}

TEST_F(ProjectAssetServiceTests, CachedReadsAreStableAndStaleOperationsReject) {
  ASSERT_TRUE(assets().create_scene({}, "Before").ok);
  const auto snapshot = assets().refresh();
  ASSERT_TRUE(snapshot.ok());
  std::filesystem::create_directory(root() / "External");
  EXPECT_EQ(assets().snapshot().value().revision, snapshot.value().revision);
  const auto renamed = assets().rename(path("Before.qdr"), "After",
                                       snapshot.value().project_id,
                                       snapshot.value().revision);
  EXPECT_FALSE(renamed.ok);
  EXPECT_TRUE(std::filesystem::exists(root() / "Before.qdr"));
  EXPECT_FALSE(std::filesystem::exists(root() / "After.qdr"));
  EXPECT_NE(assets().refresh().value().revision, snapshot.value().revision);
}

TEST_F(ProjectAssetServiceTests, PrivateStagingFilesDoNotInvalidateAuthoredAssetRevision) {
  ASSERT_TRUE(assets().create_material("Materials", "Paint").ok);
  const auto before = assets().refresh();
  ASSERT_TRUE(before.ok()) << before.error().message;
  const auto staging = root() / "Materials/.Paint.qmat.interrupted.stage";
  std::ofstream(staging) << "private transaction bytes";
  const auto with_staging = assets().refresh();
  ASSERT_TRUE(with_staging.ok()) << with_staging.error().message;
  EXPECT_EQ(with_staging.value().revision, before.value().revision);
  std::filesystem::remove(staging);
  EXPECT_EQ(assets().refresh().value().revision, before.value().revision);
}

TEST_F(ProjectAssetServiceTests, UserDotPathsArePhysicalFilesWhileOwnedMetadataStaysPrivate) {
  std::filesystem::create_directory(root() / ".art");
  std::ofstream(root() / ".art/.palette") << "authored user data";
  std::filesystem::create_directory(root() / ".crimson");
  std::ofstream(root() / ".crimson/cache.bin") << "private owner";
  std::ofstream(root() / ".art/.palette.interrupted.stage") << "private atomic stage";
  const auto snapshot = assets().refresh();
  ASSERT_TRUE(snapshot.ok()) << snapshot.error().message;
  const auto contains = [&](std::string_view path) {
    return std::ranges::any_of(snapshot.value().entries, [&](const auto &entry) {
      return entry.path.portable_string() == path;
    });
  };
  EXPECT_TRUE(contains(".art"));
  EXPECT_TRUE(contains(".art/.palette"));
  EXPECT_FALSE(contains(".crimson"));
  EXPECT_FALSE(contains(".crimson/cache.bin"));
  EXPECT_FALSE(contains(".art/.palette.interrupted.stage"));
}

TEST_F(ProjectAssetServiceTests, SceneAndFolderOrganizationRetainsMaterialImages) {
  const auto material = *ProjectAssetId::parse(kDefaultProjectMaterialId);
  const auto image = project_.material_texture_image(material, StandardMaterialTextureSlot::Albedo);
  ASSERT_NE(image, nullptr);
  const auto revision = project_.material_texture_revision();
  ASSERT_TRUE(assets().create_folder({}, "Target").ok);
  const auto scene = assets().create_scene({}, "Source");
  ASSERT_TRUE(scene.ok) << scene.message;
  const auto moved = assets().move(scene.paths, "Target");
  ASSERT_TRUE(moved.ok) << moved.message;
  const auto scans = assets().work_stats().scans;
  const auto retained = assets().snapshot();
  ASSERT_TRUE(retained.ok());
  EXPECT_EQ(assets().work_stats().scans, scans);
  EXPECT_EQ(retained.value().revision, assets().refresh().value().revision);
  ASSERT_TRUE(assets().rename(path("Target"), "Renamed").ok);
  EXPECT_EQ(project_.material_texture_revision(), revision);
  EXPECT_EQ(project_.material_texture_image(material, StandardMaterialTextureSlot::Albedo), image);
}

TEST_F(ProjectAssetServiceTests, PassiveRefreshRetainsCatalogAndDecodedImages) {
  QuaderEditorApi editor(project_);
  const auto baseline = editor.project_assets().assets_if_changed();
  ASSERT_TRUE(baseline.ok()) << baseline.error_message();
  const auto material = *ProjectAssetId::parse(kDefaultProjectMaterialId);
  const auto image = project_.material_texture_image(material, StandardMaterialTextureSlot::Albedo);
  ASSERT_NE(image, nullptr);
  const auto texture_revision = project_.material_texture_revision();
  const auto work = assets().work_stats();
  for (int reveal = 0; reveal < 8; ++reveal) {
    const auto refreshed = editor.project_assets().assets_if_changed();
    ASSERT_TRUE(refreshed.ok()) << refreshed.error_message();
    EXPECT_EQ(refreshed.value().revision, baseline.value().revision);
    EXPECT_EQ(refreshed.value().project_generation, baseline.value().project_generation);
    EXPECT_EQ(refreshed.value().assets.size(), baseline.value().assets.size());
  }
  EXPECT_EQ(assets().work_stats().scans, work.scans);
  EXPECT_EQ(assets().work_stats().inspected_entries, work.inspected_entries);
  EXPECT_EQ(assets().work_stats().metadata_checks, work.metadata_checks + 8u);
  EXPECT_EQ(project_.material_texture_revision(), texture_revision);
  EXPECT_EQ(project_.material_texture_image(material, StandardMaterialTextureSlot::Albedo), image);
  ASSERT_TRUE(assets().create_folder({}, "Authored").ok);
  const auto authored_work = assets().work_stats();
  ASSERT_TRUE(editor.project_assets().assets_if_changed().ok());
  EXPECT_EQ(assets().work_stats().scans, authored_work.scans);
  EXPECT_EQ(project_.material_texture_revision(), texture_revision);
}

TEST_F(ProjectAssetServiceTests, PassiveDiscoveryTracksExternalFoldersAndScenesWithoutReloadingImages) {
  const auto scene = assets().create_scene({}, "Original");
  ASSERT_TRUE(scene.ok) << scene.message;
  ASSERT_TRUE(assets().refresh_if_changed().ok());
  const auto material = *ProjectAssetId::parse(kDefaultProjectMaterialId);
  const auto image = project_.material_texture_image(material, StandardMaterialTextureSlot::Albedo);
  ASSERT_NE(image, nullptr);
  const auto texture_revision = project_.material_texture_revision();
  const auto original = root() / scene.paths.front().native_path();
  ASSERT_TRUE(std::filesystem::create_directories(root() / "External/Deep"));
  ASSERT_TRUE(std::filesystem::copy_file(original, root() / "External/Deep/Copy.qdr"));
  const auto contains = [&](std::string_view relative) {
    const auto &current = assets().refresh_if_changed();
    EXPECT_TRUE(current.ok()) << current.error().message;
    return current.ok() && std::ranges::any_of(current.value().entries, [&](const auto &entry) {
      return entry.path.portable_string() == relative;
    });
  };
  EXPECT_TRUE(contains("External/Deep/Copy.qdr"));
  std::filesystem::rename(root() / "External", root() / "Moved");
  EXPECT_TRUE(contains("Moved/Deep/Copy.qdr"));
  EXPECT_FALSE(contains("External/Deep/Copy.qdr"));
  ASSERT_TRUE(std::filesystem::create_directories(root() / "External/Deep"));
  ASSERT_TRUE(std::filesystem::copy_file(original, root() / "External/Deep/Replacement.qdr"));
  EXPECT_TRUE(contains("External/Deep/Replacement.qdr"));
  ASSERT_TRUE(std::filesystem::remove(root() / "External/Deep/Replacement.qdr"));
  EXPECT_FALSE(contains("External/Deep/Replacement.qdr"));
  EXPECT_EQ(project_.material_texture_revision(), texture_revision);
  EXPECT_EQ(project_.material_texture_image(material, StandardMaterialTextureSlot::Albedo), image);
}

TEST_F(ProjectAssetServiceTests, PassiveDiscoveryTracksMaterialChangesAndExplicitRefreshReadsBytes) {
  const auto created = assets().create_material({}, "Paint1");
  ASSERT_TRUE(created.ok) << created.message;
  ASSERT_TRUE(assets().snapshot().ok());
  const auto relative = created.paths.front();
  const auto absolute = root() / relative.native_path();
  const auto read = QmatCodec::read(project_.workspace()->file_system, relative);
  ASSERT_TRUE(read.ok()) << read.error().message;
  auto document = read.value().material;
  const auto texture_revision = project_.material_texture_revision();
  document.name = "Changed Paint";
  ASSERT_TRUE(QmatCodec::write_atomic(project_.workspace()->file_system, relative, document).ok());
  ASSERT_TRUE(assets().refresh_if_changed().ok());
  ASSERT_NE(project_.material_catalog().find(document.id), nullptr);
  EXPECT_EQ(project_.material_catalog().find(document.id)->material.name, "Changed Paint");
  EXPECT_GT(project_.material_texture_revision(), texture_revision);
  const auto retained_revision = project_.material_texture_revision();
  const auto retained_work = assets().work_stats();
  ASSERT_TRUE(assets().refresh_if_changed().ok());
  EXPECT_EQ(assets().work_stats().scans, retained_work.scans);
  EXPECT_EQ(project_.material_texture_revision(), retained_revision);

  // File metadata is a passive cache hint. An explicit refresh must still
  // notice an equal-length edit whose modification time was restored.
  const auto modified = std::filesystem::last_write_time(absolute);
  const auto byte_count = std::filesystem::file_size(absolute);
  document.name = "Changed Other";
  ASSERT_TRUE(QmatCodec::write_atomic(project_.workspace()->file_system, relative, document).ok());
  ASSERT_EQ(std::filesystem::file_size(absolute), byte_count);
  std::filesystem::last_write_time(absolute, modified);
  QuaderEditorApi editor(project_);
  const auto forced = editor.project_assets().assets(true);
  ASSERT_TRUE(forced.ok()) << forced.error_message();
  EXPECT_EQ(project_.material_catalog().find(document.id)->material.name, "Changed Other");
  EXPECT_GT(project_.material_texture_revision(), retained_revision);
  const auto forced_work = assets().work_stats();
  ASSERT_TRUE(editor.project_assets().assets_if_changed().ok());
  EXPECT_EQ(assets().work_stats().scans, forced_work.scans);
}

TEST_F(ProjectAssetServiceTests, PassiveDiscoveryTracksTextureEditsAndRepairsMissingImages) {
  ASSERT_TRUE(assets().refresh_if_changed().ok());
  const auto material = *ProjectAssetId::parse(kDefaultProjectMaterialId);
  const auto image = project_.material_texture_image(material, StandardMaterialTextureSlot::Albedo);
  ASSERT_NE(image, nullptr);
  const auto absolute = root() / path(image->project_relative_path).native_path();
  const auto texture_revision = project_.material_texture_revision();
  {
    std::ofstream output(absolute, std::ios::binary | std::ios::app);
    ASSERT_TRUE(output.good());
    output.put('\0');
  }
  assets().invalidate();
  ASSERT_TRUE(assets().snapshot().ok());
  EXPECT_EQ(project_.material_texture_image(material, StandardMaterialTextureSlot::Albedo), image);
  ASSERT_TRUE(assets().refresh_if_changed().ok());
  const auto changed = project_.material_texture_image(material, StandardMaterialTextureSlot::Albedo);
  ASSERT_NE(changed, nullptr);
  EXPECT_NE(changed->content_revision, image->content_revision);
  EXPECT_GT(project_.material_texture_revision(), texture_revision);
  {
    std::ofstream output(absolute, std::ios::binary | std::ios::app);
    ASSERT_TRUE(output.good());
    output.put('\1');
  }
  ASSERT_TRUE(assets().create_folder({}, "Unrelated").ok);
  EXPECT_EQ(project_.material_texture_image(material, StandardMaterialTextureSlot::Albedo), changed);
  ASSERT_TRUE(assets().refresh_if_changed().ok());
  EXPECT_NE(project_.material_texture_image(material, StandardMaterialTextureSlot::Albedo)->content_revision,
            changed->content_revision);
  ASSERT_TRUE(std::filesystem::remove(absolute));
  ASSERT_TRUE(assets().refresh_if_changed().ok());
  EXPECT_EQ(project_.material_texture_image(material, StandardMaterialTextureSlot::Albedo), nullptr);
  ASSERT_TRUE(std::filesystem::copy_file(image_source(), absolute));
  ASSERT_TRUE(assets().refresh_if_changed().ok());
  EXPECT_NE(project_.material_texture_image(material, StandardMaterialTextureSlot::Albedo), nullptr);
}

TEST_F(ProjectAssetServiceTests, PassiveDiscoveryRefreshesUninstalledFilesSeenByRetainedRead) {
  const auto created = assets().create_material({}, "Before");
  ASSERT_TRUE(created.ok) << created.message;
  const auto relative = created.paths.front();
  const auto read = QmatCodec::read(project_.workspace()->file_system, relative);
  ASSERT_TRUE(read.ok());
  auto document = read.value().material;
  document.name = "External change";
  ASSERT_TRUE(QmatCodec::write_atomic(project_.workspace()->file_system, relative, document).ok());
  assets().invalidate();
  ASSERT_TRUE(assets().snapshot().ok());
  EXPECT_EQ(project_.material_catalog().find(document.id)->material.name, "Before");
  ASSERT_TRUE(assets().refresh_if_changed().ok());
  EXPECT_EQ(project_.material_catalog().find(document.id)->material.name, "External change");
}

TEST_F(ProjectAssetServiceTests, PassiveDiscoveryDoesNotReuseFailedOrReplacedProjectHints) {
  ASSERT_TRUE(assets().create_folder({}, "OnlyFirstProject").ok);
  const auto original = assets().snapshot();
  ASSERT_TRUE(original.ok());
  const auto unavailable = temporary_ / "TemporarilyUnavailable";
  std::filesystem::rename(root(), unavailable);
  EXPECT_FALSE(assets().refresh_if_changed().ok());
  std::filesystem::rename(unavailable, root());
  ASSERT_TRUE(assets().refresh_if_changed().ok());
  ASSERT_TRUE(project_.close(true).ok);
  const auto replacement = temporary_ / "Replacement";
  ASSERT_TRUE(std::filesystem::create_directory(replacement));
  ASSERT_TRUE(project_.create(replacement).ok);
  const auto refreshed = assets().refresh_if_changed();
  ASSERT_TRUE(refreshed.ok()) << refreshed.error().message;
  EXPECT_NE(refreshed.value().project_id, original.value().project_id);
  EXPECT_FALSE(std::ranges::any_of(refreshed.value().entries, [](const auto &entry) {
    return entry.path.portable_string() == "OnlyFirstProject";
  }));
}

TEST_F(ProjectAssetServiceTests, PassiveDiscoveryValidatesExternallyReplacedFolderAliases) {
  ASSERT_TRUE(assets().create_folder({}, "External").ok);
  ASSERT_TRUE(assets().snapshot().ok());
  const auto outside = temporary_ / "Outside";
  ASSERT_TRUE(std::filesystem::create_directory(outside));
  ASSERT_TRUE(std::filesystem::remove(root() / "External"));
  std::error_code error;
  std::filesystem::create_directory_symlink(outside, root() / "External", error);
  if (error) {
    GTEST_SKIP() << "Directory symlink creation is unavailable: " << error.message();
  }
  EXPECT_FALSE(assets().refresh_if_changed().ok());
  EXPECT_TRUE(std::filesystem::is_directory(outside));
}

TEST_F(ProjectAssetServiceTests, MixedMoveRetainsAssetsAlreadyAtDestination) {
  ASSERT_TRUE(assets().create_folder({}, "Target").ok);
  const auto resident = assets().create_scene("Target", "Resident");
  ASSERT_TRUE(resident.ok) << resident.message;
  const auto incoming = assets().create_scene({}, "Incoming");
  ASSERT_TRUE(incoming.ok) << incoming.message;
  const std::array selected{resident.paths.front(), incoming.paths.front()};
  const auto moved = assets().move(selected, "Target");
  ASSERT_TRUE(moved.ok) << moved.message;
  const auto retained = assets().snapshot();
  ASSERT_TRUE(retained.ok());
  EXPECT_NE(std::ranges::find(retained.value().entries, resident.paths.front(), &ProjectAssetEntry::path),
            retained.value().entries.end());
  EXPECT_EQ(retained.value().revision, assets().refresh().value().revision);
}

TEST_F(ProjectAssetServiceTests, MaterialMoveStillRejectsUnrelatedExternalContentChanges) {
  ASSERT_TRUE(assets().create_folder({}, "Target").ok);
  const auto selected = assets().create_material({}, "Selected");
  ASSERT_TRUE(selected.ok) << selected.message;
  const auto unrelated = assets().create_material({}, "Unrelated");
  ASSERT_TRUE(unrelated.ok) << unrelated.message;
  const auto before = assets().snapshot();
  ASSERT_TRUE(before.ok());
  auto decoded = QmatCodec::read(project_.workspace()->file_system, unrelated.paths.front());
  ASSERT_TRUE(decoded.ok());
  auto document = decoded.value().material;
  document.standard_material()->roughness = 0.17f;
  const auto encoded = QmatCodec::encode(document);
  ASSERT_TRUE(encoded.ok());
  std::ofstream(root() / unrelated.paths.front().native_path(), std::ios::binary) << encoded.value();
  const auto moved = assets().move(selected.paths, "Target", before.value().project_id,
                                   before.value().revision);
  EXPECT_FALSE(moved.ok);
  EXPECT_TRUE(std::filesystem::exists(root() / selected.paths.front().native_path()));
  EXPECT_FALSE(std::filesystem::exists(root() / "Target/Selected.qmat"));
}

TEST_F(ProjectAssetServiceTests, RegisteringExternalSceneNormalizesRetainedSharedIdentities) {
  ASSERT_TRUE(assets().create_folder({}, "Target").ok);
  const auto scene = assets().create_scene({}, "ExternalA");
  ASSERT_TRUE(scene.ok) << scene.message;
  const auto original_id = project_.manifest()->find_scene(scene.paths.front())->id;
  std::filesystem::copy_file(root() / "ExternalA.qdr", root() / "ExternalB.qdr");
  auto manifest = *project_.manifest();
  ASSERT_TRUE(manifest.unregister_scene(original_id).ok());
  ASSERT_TRUE(project_.install_asset_manifest(std::move(manifest)).ok);
  const auto before = assets().snapshot();
  ASSERT_TRUE(before.ok());
  const auto copied = std::ranges::find(before.value().entries, path("ExternalB.qdr"), &ProjectAssetEntry::path);
  ASSERT_NE(copied, before.value().entries.end());
  EXPECT_EQ(copied->scene_id, original_id);
  const auto moved = assets().move(scene.paths, "Target");
  ASSERT_TRUE(moved.ok) << moved.message;
  const auto retained = assets().snapshot();
  ASSERT_TRUE(retained.ok());
  const auto remaining = std::ranges::find(retained.value().entries, path("ExternalB.qdr"), &ProjectAssetEntry::path);
  ASSERT_NE(remaining, retained.value().entries.end());
  EXPECT_FALSE(remaining->scene_id.valid());
  EXPECT_EQ(retained.value().revision, assets().refresh().value().revision);
}

TEST_F(ProjectAssetServiceTests, RejectedCatalogPublicationRestoresMovedFiles) {
  ASSERT_TRUE(assets().create_material({}, "Before").ok);
  std::filesystem::rename(root() / "Materials/default_material.qmat",
                           root() / "Materials/.default_material.qmat.interrupted.stage");
  const auto failed = assets().rename(path("Before.qmat"), "After");
  EXPECT_FALSE(failed.ok);
  EXPECT_TRUE(std::filesystem::exists(root() / "Before.qmat"));
  EXPECT_FALSE(std::filesystem::exists(root() / "After.qmat"));
  const auto original = QmatCodec::read(project_.workspace()->file_system, path("Before.qmat"));
  ASSERT_TRUE(original.ok());
  EXPECT_EQ(original.value().material.name, "Before");
  std::filesystem::rename(root() / "Materials/.default_material.qmat.interrupted.stage",
                           root() / "Materials/default_material.qmat");
  EXPECT_TRUE(ProjectAssetService::recover_pending_operations(project_.manifest_path()).ok());
}

TEST_F(ProjectAssetServiceTests, InterruptedFolderCreationIsRecoveredWithoutChangingManifest) {
  std::filesystem::create_directory(root() / "Interrupted");
  const auto journal = recovery_journal(nlohmann::json::array({
      {{"kind", "directory"}, {"target", "Interrupted"}},
  }));
  const auto recovered = ProjectAssetService::recover_pending_operations(project_.manifest_path());
  ASSERT_TRUE(recovered.ok()) << recovered.error().message;
  EXPECT_FALSE(std::filesystem::exists(root() / "Interrupted"));
  EXPECT_FALSE(std::filesystem::exists(journal));
}

TEST_F(ProjectAssetServiceTests, RecoveryPreservesNewerFolderContent) {
  std::filesystem::create_directory(root() / "Interrupted");
  std::ofstream(root() / "Interrupted/newer.txt") << "newer content";
  const auto journal = recovery_journal(nlohmann::json::array({
      {{"kind", "directory"}, {"target", "Interrupted"}},
  }));
  const auto recovered = ProjectAssetService::recover_pending_operations(project_.manifest_path());
  EXPECT_FALSE(recovered.ok());
  EXPECT_TRUE(std::filesystem::exists(root() / "Interrupted/newer.txt"));
  EXPECT_TRUE(std::filesystem::exists(journal));
}

TEST_F(ProjectAssetServiceTests, CommittedSameManifestFolderCreationSurvivesRecovery) {
  std::filesystem::create_directory(root() / "Committed");
  const auto journal = recovery_journal(nlohmann::json::array({
      {{"kind", "directory"}, {"target", "Committed"}},
  }), "committed");
  const auto recovered = ProjectAssetService::recover_pending_operations(project_.manifest_path());
  ASSERT_TRUE(recovered.ok()) << recovered.error().message;
  EXPECT_TRUE(std::filesystem::is_directory(root() / "Committed"));
  EXPECT_FALSE(std::filesystem::exists(journal));
}

TEST_F(ProjectAssetServiceTests, InterruptedCaseOnlyFolderRenameRestoresOriginalSpelling) {
  std::filesystem::create_directory(root() / "original");
  const std::string intermediate = ".quader-asset-rename-" + std::string(ProjectAssetId::generate().str());
  // Version-one recovery fixture for an empty directory, interrupted after both
  // physical renames and before a durable commit intent was recorded.
  constexpr std::string_view kEmptyDirectorySeal = "9263904324864187710";
  const auto journal = recovery_journal(nlohmann::json::array({
      {{"kind", "move"}, {"source", "Original"}, {"target", intermediate}, {"seal", kEmptyDirectorySeal}},
      {{"kind", "move"}, {"source", intermediate}, {"target", "original"}, {"seal", kEmptyDirectorySeal}},
  }));
  const auto recovered = ProjectAssetService::recover_pending_operations(project_.manifest_path());
  ASSERT_TRUE(recovered.ok()) << recovered.error().message;
  bool original_spelling = false;
  bool renamed_spelling = false;
  for (const auto &entry : std::filesystem::directory_iterator(root())) {
    original_spelling |= entry.path().filename() == "Original";
    renamed_spelling |= entry.path().filename() == "original";
  }
  EXPECT_TRUE(original_spelling);
  EXPECT_FALSE(renamed_spelling);
  EXPECT_FALSE(std::filesystem::exists(journal));
}

} // namespace
TEST_F(ProjectAssetServiceTests, MutationHookRejectsBeforePhysicalChanges){
  write(root()/"source.native","original");ASSERT_TRUE(assets().refresh().ok());unsigned prepares=0;
  assets().set_mutation_prepare([&](const ProjectAssetMutation& mutation){++prepares;EXPECT_EQ(mutation.kind,ProjectAssetMutationKind::Move);EXPECT_EQ(mutation.paths.size(),1u);if(!mutation.paths.empty()){EXPECT_EQ(mutation.paths.front().source,path("source.native"));EXPECT_EQ(mutation.paths.front().destination,path("renamed.native"));}return ProjectIoResult<ProjectAssetMutationPreparation>::failure(ProjectIoErrorCode::InvalidWorkspaceState,"Protected by typed owner");});
  const auto renamed=assets().rename(path("source.native"),"renamed");EXPECT_FALSE(renamed.ok);EXPECT_EQ(prepares,1u);EXPECT_EQ(bytes(root()/"source.native"),"original");EXPECT_FALSE(std::filesystem::exists(root()/"renamed.native"));
}
TEST_F(ProjectAssetServiceTests, MutationMetadataCommitsWithExactMoveRoots){
  write(root()/"source.native","source");write(root()/".crimson/asset-locations.json","before");ASSERT_TRUE(assets().create_folder({},"Destination").ok);unsigned commits=0,validations=0;
  assets().set_mutation_prepare([&](const ProjectAssetMutation& mutation){EXPECT_EQ(mutation.kind,ProjectAssetMutationKind::Move);EXPECT_EQ(mutation.paths.size(),1u);if(!mutation.paths.empty()){EXPECT_EQ(mutation.paths.front().source,path("source.native"));EXPECT_EQ(mutation.paths.front().destination,path("Destination/source.native"));}ProjectAssetMutationPreparation prepared;prepared.revision=71;prepared.writes.push_back({path(".crimson/asset-locations.json"),"before","after"});prepared.validate=[&]{++validations;return ProjectIoResult<void>::success();};prepared.committed=[&]{++commits;};return ProjectIoResult<ProjectAssetMutationPreparation>::success(std::move(prepared));});
  const auto moved=assets().move(std::array{path("source.native")},"Destination");ASSERT_TRUE(moved.ok)<<moved.message;EXPECT_EQ(bytes(root()/"Destination/source.native"),"source");EXPECT_EQ(bytes(root()/".crimson/asset-locations.json"),"after");EXPECT_EQ(commits,1u);EXPECT_GE(validations,1u);
}
TEST_F(ProjectAssetServiceTests, MutationValidationFailurePreservesPhysicalAndMetadataBaselines){
  write(root()/"source.native","source");write(root()/".crimson/asset-locations.json","before");ASSERT_TRUE(assets().refresh().ok());unsigned commits=0;
  assets().set_mutation_prepare([&](const ProjectAssetMutation&){ProjectAssetMutationPreparation prepared;prepared.writes.push_back({path(".crimson/asset-locations.json"),"before","after"});prepared.validate=[](){return ProjectIoResult<void>::failure(ProjectIoErrorCode::InvalidWorkspaceState,"Owner revision changed");};prepared.committed=[&]{++commits;};return ProjectIoResult<ProjectAssetMutationPreparation>::success(std::move(prepared));});
  EXPECT_FALSE(assets().rename(path("source.native"),"renamed").ok);EXPECT_EQ(bytes(root()/"source.native"),"source");EXPECT_EQ(bytes(root()/".crimson/asset-locations.json"),"before");EXPECT_FALSE(std::filesystem::exists(root()/"renamed.native"));EXPECT_EQ(commits,0u);
}
TEST_F(ProjectAssetServiceTests, RejectedCatalogPublicationRollsBackMoveAndBothMetadataWriteKinds){
  ASSERT_TRUE(assets().create_material({},"Before").ok);write(root()/".crimson/asset-locations.json","before");const auto source=bytes(root()/"Before.qmat");unsigned commits=0;
  assets().set_mutation_prepare([&](const ProjectAssetMutation&){ProjectAssetMutationPreparation prepared;prepared.writes.push_back({path(".crimson/asset-locations.json"),"before","after"});prepared.writes.push_back({.path=path(".crimson/references.json"),.after="created",.create_new=true});prepared.committed=[&]{++commits;};return ProjectIoResult<ProjectAssetMutationPreparation>::success(std::move(prepared));});
  std::filesystem::rename(root()/"Materials/default_material.qmat",root()/"Materials/.default_material.qmat.interrupted.stage");const auto failed=assets().rename(path("Before.qmat"),"After");EXPECT_FALSE(failed.ok);EXPECT_EQ(bytes(root()/"Before.qmat"),source);EXPECT_FALSE(std::filesystem::exists(root()/"After.qmat"));EXPECT_EQ(bytes(root()/".crimson/asset-locations.json"),"before");EXPECT_FALSE(std::filesystem::exists(root()/".crimson/references.json"));EXPECT_EQ(commits,0u);std::filesystem::rename(root()/"Materials/.default_material.qmat.interrupted.stage",root()/"Materials/default_material.qmat");
}
TEST_F(ProjectAssetServiceTests, CopyDerivedIdentityRewritePreservesOriginalBytes){
  write(root()/"source.native","id=original");ASSERT_TRUE(assets().create_folder({},"Copies").ok);unsigned commits=0;
  assets().set_mutation_prepare([&](const ProjectAssetMutation& mutation){EXPECT_EQ(mutation.kind,ProjectAssetMutationKind::Copy);ProjectAssetMutationPreparation prepared;if(mutation.paths.size()!=1||!mutation.paths.front().destination)return ProjectIoResult<ProjectAssetMutationPreparation>::failure(ProjectIoErrorCode::InvalidArgument,"Missing accepted copy root");EXPECT_EQ(mutation.paths.front().source,path("source.native"));EXPECT_EQ(*mutation.paths.front().destination,path("Copies/source.native"));prepared.writes.push_back({.path=*mutation.paths.front().destination,.before="id=original",.after="id=copy",.created_by_copy=true});prepared.committed=[&]{++commits;};return ProjectIoResult<ProjectAssetMutationPreparation>::success(std::move(prepared));});
  const auto copied=assets().copy(std::array{path("source.native")},"Copies");ASSERT_TRUE(copied.ok)<<copied.message;EXPECT_EQ(bytes(root()/"source.native"),"id=original");EXPECT_EQ(bytes(root()/"Copies/source.native"),"id=copy");EXPECT_EQ(commits,1u);
}

} // namespace quader

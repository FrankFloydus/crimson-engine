////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_command_ids.hpp"
#include "api/quader_editor_api.hpp"
#include "api/quader_project_assets_api_internal_access.hpp"
#include "core/release_features.hpp"
#include "editor/assets/project_asset_deletion_service.hpp"
#include "editor/assets/project_asset_service.hpp"
#include "editor/session/project_session.hpp"
#include "io/project/qmat_codec.hpp"
#include "ui/ui_panel.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>

namespace quader::test {
namespace {

ProjectRelativePath project_path(std::string_view path) {
  auto parsed = ProjectRelativePath::parse_portable(path);
  EXPECT_TRUE(parsed.ok()) << parsed.error().message;
  return parsed.take_value();
}

/// Exercises public owner paths with retained gated files seeded through storage.
class PublicReleaseAssetBrowserTests : public testing::Test {
protected:
  PublicReleaseAssetBrowserTests() : editor_(project_) {}

  void SetUp() override {
    ASSERT_EQ(current_product_release(), kPublicRelease01);
    root_ = std::filesystem::temp_directory_path() /
            ("quader-public-asset-browser-" + std::string(ProjectAssetId::generate().str()));
    ASSERT_TRUE(std::filesystem::create_directory(root_));
    project_.set_resource_root(std::filesystem::path(QUADER_SOURCE_DIR) /
                               "resources/modeling_assets");
    const auto created = editor_.project().create(root_);
    ASSERT_TRUE(created.ok()) << created.error_message();
  }

  void TearDown() override {
    (void)project_.close();
    std::error_code ignored;
    std::filesystem::remove_all(root_, ignored);
  }

  void seed_mixed_folder() {
    ASSERT_TRUE(editor_.project_assets().create_folder("", "Mixed").ok());
    ASSERT_TRUE(editor_.project_assets().create_scene_asset("Mixed", "Visible").ok());
    // Existing on-disk assets may outlive a release gate; no public authoring
    // surface is used to manufacture material or texture data in this fixture.
    ProjectMaterialDocument material;
    material.id = ProjectAssetId::generate();
    material.name = "Retained Material";
    material.payload = ProjectStandardMaterialDefinition{};
    gated_material_ = material.id;
    const auto saved = QmatCodec::write_atomic(project_.workspace()->file_system,
        project_path("Mixed/hidden.qmat"), material);
    ASSERT_TRUE(saved.ok()) << saved.error().message;
    std::filesystem::copy_file(std::filesystem::path(QUADER_SOURCE_DIR) /
        "resources/modeling_assets/materials/default/default_albedo.png",
        root_ / "Mixed/hidden.png");
    const auto refreshed = editor_.project_assets().assets(true);
    ASSERT_TRUE(refreshed.ok()) << refreshed.error_message();
    const auto *texture = project_.manifest()->find_texture(project_path("Mixed/hidden.png"));
    ASSERT_NE(texture, nullptr);
    gated_texture_ = texture->id;
  }

  std::map<std::filesystem::path, std::string> file_state() const {
    std::map<std::filesystem::path, std::string> result;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(root_)) {
      auto key = entry.path().lexically_relative(root_);
      if (entry.is_directory()) {
        key /= ".directory";
        result.emplace(std::move(key), std::string{});
      } else {
        std::ifstream stream(entry.path(), std::ios::binary);
        EXPECT_TRUE(stream.is_open());
        result.emplace(std::move(key), std::string{
            std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()});
      }
    }
    return result;
  }

  template <typename Result> void expect_api_denied(const Result &result) {
    EXPECT_FALSE(result.ok());
    EXPECT_FALSE(result.changed());
    EXPECT_FALSE(result.error_message().empty());
  }

  void expect_owner_denied(const ProjectAssetOperationResult &result) {
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.changed);
    EXPECT_FALSE(result.message.empty());
  }

  std::filesystem::path root_;
  ProjectSession project_;
  QuaderEditorApi editor_;
  ProjectAssetId gated_material_;
  ProjectAssetId gated_texture_;
};

TEST_F(PublicReleaseAssetBrowserTests, OwnerAndFacadeCatalogsExposeScenesAndFoldersOnly) {
  ASSERT_NO_FATAL_FAILURE(seed_mixed_folder());
  ASSERT_TRUE(editor_.project_assets().create_folder("", "Empty").ok());
  const auto active = project_.active_scene_id();
  const auto scene = editor_.project_assets().create_scene_asset("", "Courtyard");
  ASSERT_TRUE(scene.ok()) << scene.error_message();
  EXPECT_EQ(project_.active_scene_id(), active);
  EXPECT_FALSE(project_.active_scene()->path.has_value());

  const auto public_catalog = editor_.project_assets().assets();
  ASSERT_TRUE(public_catalog.ok()) << public_catalog.error_message();
  EXPECT_NE(std::ranges::find(public_catalog.value().folders, "Empty"),
            public_catalog.value().folders.end());
  EXPECT_NE(std::ranges::find(public_catalog.value().assets, scene.value().scene_id,
                             &QuaderProjectAssetInfo::scene_id),
            public_catalog.value().assets.end());
  EXPECT_TRUE(std::ranges::all_of(public_catalog.value().assets, [](const auto &entry) {
    return entry.kind == QuaderProjectAssetKind::Scene ||
           entry.kind == QuaderProjectAssetKind::Folder;
  }));

  const auto &owner_catalog = project_.project_assets().refresh();
  ASSERT_TRUE(owner_catalog.ok()) << owner_catalog.error().message;
  EXPECT_TRUE(std::ranges::all_of(owner_catalog.value().entries, [](const auto &entry) {
    return entry.kind == ProjectAssetKind::Scene || entry.kind == ProjectAssetKind::Folder;
  }));
  EXPECT_TRUE(std::ranges::any_of(owner_catalog.value().entries, [&](const auto &entry) {
    return entry.kind == ProjectAssetKind::Scene && entry.scene_id == scene.value().scene_id;
  }));
  EXPECT_TRUE(std::filesystem::exists(root_ / "Mixed/hidden.qmat"));
  EXPECT_TRUE(std::filesystem::exists(root_ / "Mixed/hidden.png"));
}

TEST_F(PublicReleaseAssetBrowserTests, MaterialTextureCreationAndInspectionFailBeforeIo) {
  ASSERT_NO_FATAL_FAILURE(seed_mixed_folder());
  const auto before = file_state();
  const auto saved_manifest = ProjectManifestStore::serialize(*project_.manifest());
  const std::array unread_sources{root_ / "source-that-must-not-be-read.png"};
  expect_api_denied(editor_.project_assets().create_material_asset("", "Denied"));
  expect_api_denied(editor_.project_assets().import_textures(unread_sources, ""));
  expect_api_denied(editor_.project_assets().texture_info(gated_texture_));
  expect_api_denied(editor_.project_assets().material_thumbnail_info(gated_material_));
  expect_owner_denied(project_.project_assets().create_material("", "Denied"));
  expect_owner_denied(project_.project_assets().import_textures(unread_sources, ""));
  EXPECT_EQ(file_state(), before);
  EXPECT_EQ(ProjectManifestStore::serialize(*project_.manifest()), saved_manifest);
}

TEST_F(PublicReleaseAssetBrowserTests, HiddenAssetsAndContainingFoldersCannotBeOrganized) {
  ASSERT_NO_FATAL_FAILURE(seed_mixed_folder());
  ASSERT_TRUE(editor_.project_assets().create_folder("", "Destination").ok());
  const auto before = file_state();
  const auto active = project_.active_scene_id();
  const auto saved_manifest = ProjectManifestStore::serialize(*project_.manifest());
  for (const std::string path : {"Mixed/hidden.qmat", "Mixed/hidden.png", "Mixed"}) {
    SCOPED_TRACE(path);
    const std::array api_paths{path};
    const std::array owner_paths{project_path(path)};
    expect_api_denied(editor_.project_assets().rename_asset(path, "Denied"));
    expect_api_denied(editor_.project_assets().move_assets(api_paths, "Destination"));
    expect_api_denied(editor_.project_assets().copy_assets(api_paths, "Destination"));
    expect_api_denied(editor_.project_assets().duplicate_assets(api_paths));
    expect_owner_denied(project_.project_assets().rename(owner_paths.front(), "Denied"));
    expect_owner_denied(project_.project_assets().move(owner_paths, "Destination"));
    expect_owner_denied(project_.project_assets().copy(owner_paths, "Destination"));
    expect_owner_denied(project_.project_assets().duplicate(owner_paths));
    EXPECT_EQ(file_state(), before);
    EXPECT_EQ(ProjectManifestStore::serialize(*project_.manifest()), saved_manifest);
    EXPECT_EQ(project_.active_scene_id(), active);
  }
}

TEST_F(PublicReleaseAssetBrowserTests, HiddenAssetDeletionNeverInvokesTheRecyclePort) {
  ASSERT_NO_FATAL_FAILURE(seed_mixed_folder());
  int recycle_calls = 0;
  const ProjectAssetRecyclePort recycle = [&](std::span<const std::filesystem::path>) {
    ++recycle_calls;
    return ProjectAssetRecycleResult{.ok = false, .message = "Unexpected recycle call"};
  };
  QuaderProjectAssetsApiInternalAccess::set_recycle_port(editor_.project_assets(), recycle);
  const ProjectAssetDeletionService owner(project_, recycle);
  const auto before = file_state();
  const auto saved_manifest = ProjectManifestStore::serialize(*project_.manifest());
  for (const std::string path : {"Mixed/hidden.qmat", "Mixed/hidden.png", "Mixed"}) {
    SCOPED_TRACE(path);
    const std::array api_paths{path};
    const std::array owner_paths{project_path(path)};
    const auto preview = editor_.project_assets().preview_delete_assets(api_paths);
    ASSERT_TRUE(preview.ok()) << preview.error_message();
    EXPECT_FALSE(preview.value().complete);
    EXPECT_FALSE(preview.value().errors.empty());
    expect_api_denied(editor_.project_assets().delete_assets(api_paths, preview.value().revision));
    const auto owner_preview = owner.preview(owner_paths);
    EXPECT_FALSE(owner_preview.complete);
    EXPECT_FALSE(owner_preview.errors.empty());
    const auto denied = owner.erase(owner_paths, owner_preview.revision);
    EXPECT_FALSE(denied.ok);
    EXPECT_FALSE(denied.changed);
    EXPECT_EQ(recycle_calls, 0);
    EXPECT_EQ(file_state(), before);
    EXPECT_EQ(ProjectManifestStore::serialize(*project_.manifest()), saved_manifest);
  }
}

TEST_F(PublicReleaseAssetBrowserTests, SceneOnlyFolderCopiesAndMovesRemainAvailable) {
  ASSERT_TRUE(editor_.project_assets().create_folder("", "Maps").ok());
  ASSERT_TRUE(editor_.project_assets().create_folder("", "Destination").ok());
  const auto scene = editor_.project_assets().create_scene_asset("Maps", "Main");
  ASSERT_TRUE(scene.ok()) << scene.error_message();
  const std::array folders{std::string("Maps")};
  const auto copied = editor_.project_assets().copy_assets(folders, "Destination");
  ASSERT_TRUE(copied.ok()) << copied.error_message();
  EXPECT_TRUE(std::filesystem::exists(root_ / "Destination/Maps/Main.qdr"));
  const auto original = project_.manifest()->find_scene(project_path("Maps/Main.qdr"));
  const auto copy = project_.manifest()->find_scene(project_path("Destination/Maps/Main.qdr"));
  ASSERT_NE(original, nullptr);
  ASSERT_NE(copy, nullptr);
  EXPECT_NE(original->id, copy->id);
  const auto renamed = project_.project_assets().rename(project_path("Maps"), "SourceMaps");
  ASSERT_TRUE(renamed.ok) << renamed.message;
  const auto moved = project_.project_assets().move(
      std::array{project_path("SourceMaps")}, "Destination");
  ASSERT_TRUE(moved.ok) << moved.message;
  EXPECT_TRUE(std::filesystem::exists(root_ / "Destination/SourceMaps/Main.qdr"));
  EXPECT_FALSE(std::filesystem::exists(root_ / "SourceMaps"));
}

TEST_F(PublicReleaseAssetBrowserTests, BrowserShortcutAndPanelArePublicWhileFaceTextureIsGated) {
  EXPECT_TRUE(ui_panel_available(UIPanelId::AssetBrowser));
  EXPECT_FALSE(ui_panel_available(UIPanelId::FaceTexture));
  ASSERT_TRUE(editor_.commands().descriptor(quader_command::kUiAssetBrowser).has_value());
  EXPECT_EQ(editor_.commands().shortcut_for(quader_command::kUiAssetBrowser), "Ctrl+Space");
  const auto bindings = editor_.commands().keybinding_catalog();
  EXPECT_TRUE(std::ranges::any_of(bindings, [](const auto &entry) {
    return std::ranges::find(entry.target_ids, quader_command::kUiAssetBrowser.string()) !=
           entry.target_ids.end();
  }));
  int toggles = 0;
  int panel_calls = 0;
  editor_.ui().set_runtime_hooks({
      .set_panel_visible = [&](std::string_view, bool) {
        ++panel_calls;
        return QuaderApiVoidResult{.ok = true, .changed = true};
      },
      .toggle_asset_browser = [&] {
        ++toggles;
        return QuaderApiVoidResult{.ok = true, .changed = true};
      }});
  EXPECT_TRUE(editor_.commands().execute(quader_command::kUiAssetBrowser).ok());
  EXPECT_EQ(toggles, 1);
  EXPECT_TRUE(editor_.ui().show_panel("asset_browser").ok());
  EXPECT_EQ(panel_calls, 1);
  expect_api_denied(editor_.ui().show_panel("face_texture"));
  EXPECT_EQ(panel_calls, 1);
}

TEST_F(PublicReleaseAssetBrowserTests, IncompatibleProjectDoesNotPublishPendingOrganization) {
  const auto candidate_root = root_ / "Incompatible";
  ASSERT_TRUE(std::filesystem::create_directory(candidate_root));
  auto created = ProjectManifestStore::create_in_empty_directory(candidate_root);
  ASSERT_TRUE(created.ok()) << created.error().message;
  auto &workspace = created.value();
  workspace.manifest.require_product_release(kInternalFullRelease);
  ASSERT_TRUE(ProjectManifestStore::save_atomic(workspace).ok());
  const auto baseline = ProjectManifestStore::serialize(workspace.manifest);
  auto candidate = workspace.manifest;
  ASSERT_TRUE(candidate.mark_materials_deleted(std::array{ProjectAssetId::generate()}).ok());

  // This otherwise-valid committed journal would replace the manifest if asset
  // recovery ran before the immutable public profile checked its requirement.
  ASSERT_TRUE(std::filesystem::create_directory(candidate_root / ".quader"));
  const auto journal_path = candidate_root / ".quader" /
      ("asset-organization-" + std::string(ProjectAssetId::generate().str()) + ".json");
  const nlohmann::json journal{
      {"version", 1}, {"state", "committed"},
      {"project", std::string(candidate.id().str())},
      {"manifest", "Incompatible.qproj"}, {"baseline", baseline},
      {"candidate", ProjectManifestStore::serialize(candidate)},
      {"actions", nlohmann::json::array()},
  };
  {
    std::ofstream stream(journal_path, std::ios::binary);
    ASSERT_TRUE(stream.is_open());
    stream << journal.dump(2);
    stream.close();
    ASSERT_TRUE(stream.good());
  }
  const auto before = file_state();
  const auto active_scene = project_.active_scene_id();
  const auto active_project = project_.manifest()->id();
  const auto opened = project_.open(workspace.manifest_path);
  EXPECT_FALSE(opened.ok);
  EXPECT_FALSE(opened.changed);
  EXPECT_EQ(opened.message, product_release_unavailable_message(kInternalFullRelease));
  EXPECT_EQ(file_state(), before);
  EXPECT_EQ(project_.active_scene_id(), active_scene);
  EXPECT_EQ(project_.manifest()->id(), active_project);
}

} // namespace
} // namespace quader::test

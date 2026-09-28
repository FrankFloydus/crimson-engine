////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "api/quader_project_assets_api_internal_access.hpp"
#include "editor/session/project_session.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <QImage>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace quader {
namespace {

class ProjectAssetBrowserApiTests : public testing::Test {
protected:
  ProjectAssetBrowserApiTests() : editor_(project_) {}

  void SetUp() override {
    ASSERT_TRUE(temporary_.isValid());
    project_.set_resource_root(std::filesystem::path(QUADER_SOURCE_DIR) /
                               "resources/modeling_assets");
    root_ = std::filesystem::path(temporary_.path().toStdWString()) / "Project";
    std::filesystem::create_directory(root_);
    ASSERT_TRUE(editor_.project().create(root_).ok());
  }

  ProjectRelativePath relative(std::string_view value) {
    auto parsed = ProjectRelativePath::parse_portable(value);
    EXPECT_TRUE(parsed.ok());
    return parsed.ok() ? parsed.take_value() : ProjectRelativePath{};
  }

  QTemporaryDir temporary_;
  std::filesystem::path root_;
  ProjectSession project_;
  QuaderEditorApi editor_;
};

TEST_F(ProjectAssetBrowserApiTests, CreatingSceneAssetsLeavesCurrentTabInMemory) {
  const auto active = project_.active_scene_id();
  const auto count = project_.open_scenes().size();
  const auto first = editor_.project_assets().create_scene_asset("", "Courtyard");
  ASSERT_TRUE(first.ok()) << first.error_message();
  EXPECT_EQ(first.value().kind, QuaderProjectAssetKind::Scene);
  EXPECT_TRUE(first.value().scene_id.valid());
  EXPECT_TRUE(std::filesystem::exists(root_ / "Courtyard.qdr"));
  EXPECT_EQ(project_.active_scene_id(), active);
  EXPECT_EQ(project_.open_scenes().size(), count);
  EXPECT_FALSE(project_.active_scene()->path.has_value());
  const auto second = editor_.project_assets().create_scene_asset("", "Courtyard");
  ASSERT_TRUE(second.ok()) << second.error_message();
  EXPECT_NE(second.value().scene_id, first.value().scene_id);
  EXPECT_NE(second.value().project_relative_path, first.value().project_relative_path);
  const auto opened = editor_.scene_documents().open(relative(first.value().project_relative_path));
  ASSERT_TRUE(opened.ok()) << opened.error_message();
  EXPECT_EQ(project_.active_scene()->scene_id, first.value().scene_id);
  const auto opened_again = editor_.scene_documents().open(relative(first.value().project_relative_path));
  ASSERT_TRUE(opened_again.ok());
  EXPECT_EQ(opened_again.value(), opened.value());
}

TEST_F(ProjectAssetBrowserApiTests, MixedCatalogImportsOnlyTexturesAndRetainsRevision) {
  ASSERT_TRUE(editor_.project_assets().create_folder("", "Environment").ok());
  const auto scene = editor_.project_assets().create_scene_asset("Environment", "Main");
  ASSERT_TRUE(scene.ok()) << scene.error_message();
  const auto material = editor_.project_assets().create_material_asset("Environment", "Stone");
  ASSERT_TRUE(material.ok()) << material.error_message();
  const auto source = std::filesystem::path(QUADER_SOURCE_DIR) /
                      "resources/modeling_assets/materials/default/default_albedo.png";
  const auto before = editor_.project_assets().materials();
  ASSERT_TRUE(before.ok());
  const auto imported = editor_.project_assets().import_textures(std::array{source}, "Environment");
  ASSERT_TRUE(imported.ok()) << imported.error_message();
  ASSERT_EQ(imported.value().size(), 1u);
  EXPECT_EQ(imported.value().front().kind, QuaderProjectAssetKind::Texture);
  EXPECT_EQ(editor_.project_assets().materials().value().size(), before.value().size());
  const auto catalog = editor_.project_assets().assets();
  ASSERT_TRUE(catalog.ok()) << catalog.error_message();
  EXPECT_NE(std::ranges::find(catalog.value().folders, "Environment"), catalog.value().folders.end());
  const auto stamp = editor_.project_assets().asset_revision();
  ASSERT_TRUE(stamp.ok());
  for (int iteration = 0; iteration < 8; ++iteration) {
    const auto observed = editor_.project_assets().asset_revision();
    ASSERT_TRUE(observed.ok());
    EXPECT_EQ(observed.value(), stamp.value());
  }
  const auto image = editor_.project_assets().texture_info(imported.value().front().asset_id);
  ASSERT_TRUE(image.ok()) << image.error_message();
  EXPECT_GT(image.value().width, 0u);
  EXPECT_GT(image.value().height, 0u);
  EXPECT_EQ(image.value().texture_id, imported.value().front().asset_id);
}

TEST_F(ProjectAssetBrowserApiTests, AssetMovesPreserveAnOpenSceneAndRejectEscape) {
  const auto asset = editor_.project_assets().create_scene_asset("", "Main");
  ASSERT_TRUE(asset.ok());
  const auto opened = editor_.scene_documents().open(relative(asset.value().project_relative_path));
  ASSERT_TRUE(opened.ok());
  auto *session = project_.active_scene()->session.get();
  ASSERT_TRUE(editor_.project_assets().create_folder("", "Maps").ok());
  const auto catalog = editor_.project_assets().assets();
  ASSERT_TRUE(catalog.ok());
  const auto moved = editor_.project_assets().move_assets(
      std::array{asset.value().project_relative_path}, "Maps", catalog.value().project_id,
      catalog.value().revision);
  ASSERT_TRUE(moved.ok()) << moved.error_message();
  EXPECT_EQ(project_.active_scene()->session.get(), session);
  EXPECT_EQ(project_.active_scene()->scene_id, asset.value().scene_id);
  EXPECT_EQ(project_.active_scene()->path->portable_string(), "Maps/Main.qdr");
  const auto escaped = editor_.project_assets().move_assets(
      std::array{std::string("Maps/Main.qdr")}, "../Outside");
  EXPECT_FALSE(escaped.ok());
  EXPECT_TRUE(std::filesystem::exists(root_ / "Maps/Main.qdr"));
  EXPECT_EQ(project_.active_scene()->session.get(), session);
}

TEST_F(ProjectAssetBrowserApiTests, DeletingAnOpenAssetPublishesAfterBindingReplacement) {
  const auto asset = editor_.project_assets().create_scene_asset("", "ToDelete");
  ASSERT_TRUE(asset.ok());
  const auto opened = editor_.scene_documents().open(relative(asset.value().project_relative_path));
  ASSERT_TRUE(opened.ok());
  const auto deleted_session = opened.value();
  const auto bin = root_.parent_path() / "RecycleTest";
  std::filesystem::create_directory(bin);
  QuaderProjectAssetsApiInternalAccess::set_recycle_port(editor_.project_assets(),
      [bin](std::span<const std::filesystem::path> paths) {
        ProjectAssetRecycleResult result{.ok = true};
        for (const auto &path : paths) {
          std::filesystem::rename(path, bin / path.filename());
          result.items.push_back({.original_path = path, .recycled = true});
        }
        return result;
      });
  bool closed = false;
  const auto subscription = editor_.events().subscribe(quader_event::kSceneClosed,
      [&](const QuaderEvent &event) {
        if (event.scene_session_id == deleted_session) {
          closed = true;
          EXPECT_NE(editor_.scene_documents().active_scene_id(), deleted_session);
          EXPECT_EQ(project_.find(deleted_session), nullptr);
        }
      });
  ASSERT_TRUE(subscription.valid());
  const std::array paths{asset.value().project_relative_path};
  const auto preview = editor_.project_assets().preview_delete_assets(paths);
  ASSERT_TRUE(preview.ok()) << preview.error_message();
  ASSERT_TRUE(preview.value().complete);
  const auto removed = editor_.project_assets().delete_assets(paths, preview.value().revision);
  ASSERT_TRUE(removed.ok()) << removed.error_message();
  EXPECT_TRUE(closed);
  EXPECT_FALSE(std::filesystem::exists(root_ / "ToDelete.qdr"));
  EXPECT_TRUE(std::filesystem::exists(bin / "ToDelete.qdr"));
}

TEST_F(ProjectAssetBrowserApiTests, ExplicitRefreshLoadsExternalMaterialsAndTextureEdits) {
  const auto read_file = [](const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream), {});
  };
  const auto manifest_before = read_file(project_.manifest_path());
  const auto image_path = root_ / "loose.png";
  QImage source(3, 2, QImage::Format_RGBA8888);
  source.fill(Qt::red);
  ASSERT_TRUE(source.save(QString::fromStdWString(image_path.wstring()), "PNG"));
  auto assets = editor_.project_assets().assets(true);
  ASSERT_TRUE(assets.ok()) << assets.error_message();
  const auto entry = std::ranges::find(assets.value().assets, "loose.png",
                                      &QuaderProjectAssetInfo::project_relative_path);
  ASSERT_NE(entry, assets.value().assets.end());
  const auto texture_id = entry->asset_id;
  EXPECT_TRUE(texture_id.valid());
  auto first = editor_.project_assets().texture_info(texture_id);
  ASSERT_TRUE(first.ok()) << first.error_message();
  EXPECT_EQ(first.value().width, 3u);
  EXPECT_EQ(first.value().height, 2u);
  const auto cached = first.value().rgba8;
  EXPECT_EQ(editor_.project_assets().texture_info(texture_id).value().rgba8, cached);
  QImage replacement(5, 4, QImage::Format_RGBA8888);
  replacement.fill(Qt::green);
  ASSERT_TRUE(replacement.save(QString::fromStdWString(image_path.wstring()), "PNG"));
  const auto material_path = relative("external.qmat");
  ProjectMaterialDocument material;
  material.id = ProjectAssetId::generate();
  material.name = "External material";
  material.standard_material()->roughness = 0.31f;
  material.standard_material()->base_color_texture = ProjectMaterialTextureSlot{
      .id = texture_id, .project_relative_path = "loose.png"};
  ASSERT_TRUE(QmatCodec::write_atomic(project_.workspace()->file_system,
                                      material_path, material).ok());
  assets = editor_.project_assets().assets(true);
  ASSERT_TRUE(assets.ok()) << assets.error_message();
  const auto changed = editor_.project_assets().texture_info(texture_id);
  ASSERT_TRUE(changed.ok()) << changed.error_message();
  EXPECT_EQ(changed.value().width, 5u);
  EXPECT_EQ(changed.value().height, 4u);
  EXPECT_NE(changed.value().rgba8, cached);
  const auto sphere = editor_.project_assets().material_thumbnail_info(material.id);
  ASSERT_TRUE(sphere.ok()) << sphere.error_message();
  EXPECT_FLOAT_EQ(sphere.value().material.roughness, 0.31f);
  ASSERT_TRUE(sphere.value().textures[0].has_value());
  EXPECT_EQ(sphere.value().textures[0]->rgba8, changed.value().rgba8);
  const auto row = std::ranges::find(assets.value().assets, material.id,
                                    &QuaderProjectAssetInfo::asset_id);
  ASSERT_NE(row, assets.value().assets.end());
  EXPECT_EQ(row->content_revision, sphere.value().content_revision);
  EXPECT_EQ(read_file(project_.manifest_path()), manifest_before);
}

TEST_F(ProjectAssetBrowserApiTests, RefreshKeepsLegacyMigrationInMemory) {
  const auto path = project_.manifest_path();
  ASSERT_TRUE(editor_.project().close().ok());
  std::ifstream source(path, std::ios::binary);
  auto manifest = nlohmann::json::parse(source);
  source.close();
  manifest["version"] = 4;
  manifest.erase("textures");
  manifest.erase("deletedTextureIds");
  const std::string legacy = manifest.dump(2);
  {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << legacy;
  }
  ASSERT_TRUE(editor_.project().open(path).ok());
  QImage image(2, 2, QImage::Format_RGBA8888);
  image.fill(Qt::blue);
  ASSERT_TRUE(image.save(QString::fromStdWString((root_ / "legacy-loose.png").wstring()), "PNG"));
  const auto assets = editor_.project_assets().assets(true);
  ASSERT_TRUE(assets.ok()) << assets.error_message();
  const auto found = std::ranges::find(assets.value().assets, "legacy-loose.png",
                                      &QuaderProjectAssetInfo::project_relative_path);
  ASSERT_NE(found, assets.value().assets.end());
  EXPECT_TRUE(found->asset_id.valid());
  EXPECT_EQ(project_.manifest()->loaded_version(), 4u);
  std::ifstream unchanged(path, std::ios::binary);
  EXPECT_EQ(std::string(std::istreambuf_iterator<char>(unchanged), {}), legacy);
}

} // namespace
} // namespace quader

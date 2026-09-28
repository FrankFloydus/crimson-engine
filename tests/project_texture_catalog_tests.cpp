////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/project/project_asset_resolver.hpp"
#include "io/project/project_manifest.hpp"
#include "io/project/project_material_catalog.hpp"
#include "io/project/project_texture_catalog.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace quader;

namespace {

ProjectRelativePath asset_path(std::string_view text) {
  auto result = ProjectRelativePath::parse_portable(text);
  EXPECT_TRUE(result.ok()) << result.error().message;
  return result.take_value();
}

/// Owns a temporary project and writes source bytes without invoking UI imports.
class ProjectTextureCatalogTests : public ::testing::Test {
protected:
  void SetUp() override {
    root_ = std::filesystem::temp_directory_path() /
            ("quader-texture-catalog-" + std::string(ProjectAssetId::generate().str()));
    ASSERT_TRUE(std::filesystem::create_directory(root_));
    auto created = ProjectManifestStore::create_in_empty_directory(root_);
    ASSERT_TRUE(created.ok()) << created.error().message;
    workspace_ = created.take_value();
  }

  void TearDown() override {
    std::error_code ignored;
    std::filesystem::remove_all(root_, ignored);
  }

  void write(std::string_view path, std::string_view bytes) {
    const auto destination = root_ / asset_path(path).native_path();
    std::filesystem::create_directories(destination.parent_path());
    std::ofstream stream(destination, std::ios::binary);
    ASSERT_TRUE(stream.is_open());
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    stream.close();
    ASSERT_TRUE(stream.good());
  }

  ProjectAssetId material(std::string_view path, ProjectAssetId texture_id,
                          std::string_view texture_path,
                          ProjectTextureColorSpace color_space = ProjectTextureColorSpace::Srgb) {
    ProjectMaterialDocument material;
    material.id = ProjectAssetId::generate();
    material.name = std::string(path);
    ProjectStandardMaterialDefinition standard;
    ProjectMaterialTextureSlot slot{.id = texture_id,
                                    .project_relative_path = std::string(texture_path),
                                    .color_space = color_space};
    if (color_space == ProjectTextureColorSpace::Srgb) {
      standard.base_color_texture = slot;
    } else {
      standard.roughness_texture = slot;
    }
    material.payload = standard;
    auto encoded = QmatCodec::encode(material);
    EXPECT_TRUE(encoded.ok()) << encoded.error().message;
    write(path, encoded.value());
    return material.id;
  }

  ProjectMaterialCatalog scan(const ProjectManifest *manifest = nullptr) {
    auto catalog = ProjectMaterialCatalog::scan(workspace_.file_system, nullptr, manifest);
    EXPECT_TRUE(catalog.ok()) << catalog.error().message;
    return catalog.take_value();
  }

  std::filesystem::path root_;
  ProjectWorkspaceStorage workspace_;
};

TEST_F(ProjectTextureCatalogTests, ManifestRoundTripsAliasesAndRetiredIdentities) {
  const auto id = ProjectAssetId::generate();
  const auto alias = ProjectAssetId::generate();
  const auto retired = ProjectAssetId::generate();
  ASSERT_TRUE(workspace_.manifest.register_texture(
      {id, asset_path("Shared/image.png"), {alias}}).ok());
  ASSERT_TRUE(workspace_.manifest.mark_textures_deleted(std::array{retired}).ok());
  const auto decoded = ProjectManifestStore::deserialize(
      ProjectManifestStore::serialize(workspace_.manifest));
  ASSERT_TRUE(decoded.ok()) << decoded.error().message;
  ASSERT_NE(decoded.value().find_texture(alias), nullptr);
  EXPECT_EQ(decoded.value().find_texture(alias)->id, id);
  EXPECT_TRUE(decoded.value().is_texture_deleted(retired));
  EXPECT_EQ(decoded.value().minimum_product_release(),
            workspace_.manifest.minimum_product_release());
}

TEST_F(ProjectTextureCatalogTests, RejectsDuplicateAliasesPathsAndRetiredReuseAtomically) {
  const auto id = ProjectAssetId::generate();
  const auto alias = ProjectAssetId::generate();
  ASSERT_TRUE(workspace_.manifest.register_texture(
      {id, asset_path("shared.png"), {alias}}).ok());
  const auto before = ProjectManifestStore::serialize(workspace_.manifest);
  EXPECT_FALSE(workspace_.manifest.register_texture(
      {ProjectAssetId::generate(), asset_path("other.png"), {alias}}).ok());
  EXPECT_FALSE(workspace_.manifest.register_texture(
      {ProjectAssetId::generate(), asset_path("SHARED.PNG"), {}}).ok());
  EXPECT_EQ(ProjectManifestStore::serialize(workspace_.manifest), before);
  ASSERT_TRUE(workspace_.manifest.mark_textures_deleted(std::array{alias}).ok());
  EXPECT_EQ(workspace_.manifest.find_texture(id), nullptr);
  EXPECT_TRUE(workspace_.manifest.is_texture_deleted(id));
  EXPECT_TRUE(workspace_.manifest.is_texture_deleted(alias));
  EXPECT_FALSE(workspace_.manifest.register_texture(
      {id, asset_path("returned.png"), {}}).ok());
}

TEST_F(ProjectTextureCatalogTests, DiscoveryPreservesLegacyAliasesAndMixedColorSpaces) {
  const auto srgb_id = ProjectAssetId::generate();
  const auto linear_id = ProjectAssetId::generate();
  write("Mixed/shared.png", "encoded-image");
  const auto srgb = material("Mixed/albedo.qmat", srgb_id, "Mixed/shared.png");
  const auto linear = material("linear.qmat", linear_id, "Mixed/shared.png",
                               ProjectTextureColorSpace::Linear);
  auto catalog = scan();
  const auto discovered = ProjectTextureCatalog::discover(
      workspace_.file_system, workspace_.manifest, catalog);
  ASSERT_TRUE(discovered.ok()) << discovered.error().message;
  ASSERT_EQ(discovered.value().textures().size(), 1u);
  EXPECT_EQ(discovered.value().find_texture(srgb_id), discovered.value().find_texture(linear_id));
  EXPECT_TRUE(workspace_.manifest.textures().empty());
  const auto srgb_bytes = catalog.read_texture(workspace_.file_system, srgb,
      StandardMaterialTextureSlot::Albedo, &discovered.value());
  const auto linear_bytes = catalog.read_texture(workspace_.file_system, linear,
      StandardMaterialTextureSlot::Roughness, &discovered.value());
  ASSERT_TRUE(srgb_bytes.ok());
  ASSERT_TRUE(linear_bytes.ok());
  EXPECT_EQ(srgb_bytes.value().id, linear_bytes.value().id);
  EXPECT_EQ(srgb_bytes.value().content_revision, linear_bytes.value().content_revision);
  EXPECT_NE(srgb_bytes.value().color_space, linear_bytes.value().color_space);
}

TEST_F(ProjectTextureCatalogTests, ConflictingLegacyUuidPathsRejectsWholeCandidate) {
  const auto id = ProjectAssetId::generate();
  write("one.png", "one");
  write("two.png", "two");
  material("one.qmat", id, "one.png");
  material("two.qmat", id, "two.png");
  const auto before = ProjectManifestStore::serialize(workspace_.manifest);
  const auto catalog = scan();
  const auto discovered = ProjectTextureCatalog::discover(
      workspace_.file_system, workspace_.manifest, catalog);
  EXPECT_FALSE(discovered.ok());
  EXPECT_EQ(discovered.error().code, ProjectIoErrorCode::DuplicateAsset);
  EXPECT_EQ(ProjectManifestStore::serialize(workspace_.manifest), before);
}

TEST_F(ProjectTextureCatalogTests, DiscoversLooseImagesAndIgnoresBookkeeping) {
  write("root.png", "image");
  write("Mixed/checker.TGA", "image");
  write("Mixed/unrelated.txt", "text");
  write(".quader/recovery/hidden.png", "recovery");
  write(".git/hidden.qmat", "not-a-material");
  std::filesystem::create_directory(root_ / "Empty");
  const auto catalog = scan();
  EXPECT_TRUE(catalog.warnings().empty());
  EXPECT_TRUE(std::ranges::any_of(catalog.folders(), [](const auto &path) {
    return path.portable_string() == "Empty";
  }));
  const auto discovered = ProjectTextureCatalog::discover(
      workspace_.file_system, workspace_.manifest, catalog);
  ASSERT_TRUE(discovered.ok()) << discovered.error().message;
  ASSERT_EQ(discovered.value().textures().size(), 2u);
  EXPECT_NE(discovered.value().find_texture(asset_path("root.png")), nullptr);
  EXPECT_NE(discovered.value().find_texture(asset_path("Mixed/checker.TGA")), nullptr);
  EXPECT_EQ(discovered.value().find_texture(asset_path(".quader/recovery/hidden.png")), nullptr);
}

TEST_F(ProjectTextureCatalogTests, KnownIdentityOverridesStaleOrUnsafePathHints) {
  const auto id = ProjectAssetId::generate();
  const auto alias = ProjectAssetId::generate();
  write("Current/image.png", "new-image");
  ASSERT_TRUE(workspace_.manifest.register_texture(
      {id, asset_path("Current/image.png"), {alias}}).ok());
  const auto material_id = material("paint.qmat", alias, "Previous/image.png");
  const auto catalog = scan(&workspace_.manifest);
  ASSERT_NE(catalog.find(material_id), nullptr);
  ASSERT_TRUE(catalog.find(material_id)->material.standard_material()->base_color_texture.has_value());
  EXPECT_EQ(catalog.find(material_id)->material.standard_material()->base_color_texture->id, id);
  EXPECT_TRUE(catalog.warnings().empty());
  const ProjectAssetResolver resolver(workspace_.file_system, &workspace_.manifest);
  const auto resolved = resolver.resolve_existing(
      {alias, ProjectAssetKind::Texture, asset_path("Previous/image.png")});
  ASSERT_TRUE(resolved.ok()) << resolved.error().message;
  EXPECT_EQ(resolved.value(), root_ / "Current" / "image.png");
}

TEST_F(ProjectTextureCatalogTests, RetiredIdentityNeverFallsBackToAnExistingPath) {
  const auto id = ProjectAssetId::generate();
  write("existing.png", "original");
  material("paint.qmat", id, "existing.png");
  ASSERT_TRUE(workspace_.manifest.mark_textures_deleted(std::array{id}).ok());
  const ProjectAssetResolver resolver(workspace_.file_system, &workspace_.manifest);
  const auto resolved = resolver.resolve_existing(
      {id, ProjectAssetKind::Texture, asset_path("existing.png")});
  EXPECT_FALSE(resolved.ok());
  EXPECT_EQ(resolved.error().code, ProjectIoErrorCode::AssetNotFound);
  const auto catalog = scan(&workspace_.manifest);
  ASSERT_EQ(catalog.entries().size(), 1u);
  EXPECT_FALSE(catalog.entries().front().material.standard_material()->base_color_texture.has_value());
}

TEST_F(ProjectTextureCatalogTests, ReadOnlyLegacyMigrationPersistsOnlyOnAuthoredSave) {
  auto legacy = nlohmann::json::parse(ProjectManifestStore::serialize(workspace_.manifest));
  legacy["version"] = 4u;
  legacy.erase("textures");
  legacy.erase("deletedTextureIds");
  const std::string bytes = legacy.dump(4);
  write(workspace_.manifest_path.filename().string(), bytes);
  write("loose.png", "image");
  const auto opened = ProjectManifestStore::open(workspace_.manifest_path);
  ASSERT_TRUE(opened.ok()) << opened.error().message;
  EXPECT_EQ(opened.value().manifest.loaded_version(), 4u);
  const auto candidate = ProjectTextureCatalog::discover(
      opened.value().file_system, opened.value().manifest, scan());
  ASSERT_TRUE(candidate.ok()) << candidate.error().message;
  EXPECT_EQ(candidate.value().loaded_version(), 4u);
  {
    std::ifstream stream(workspace_.manifest_path, std::ios::binary);
    const std::string unchanged{std::istreambuf_iterator<char>(stream), {}};
    EXPECT_EQ(unchanged, bytes);
  }
  ASSERT_TRUE(ProjectManifestStore::save_atomic(workspace_.file_system,
      workspace_.manifest_path, candidate.value()).ok());
  const auto reopened = ProjectManifestStore::open(workspace_.manifest_path);
  ASSERT_TRUE(reopened.ok());
  EXPECT_EQ(reopened.value().manifest.loaded_version(), 5u);
  ASSERT_EQ(reopened.value().manifest.textures().size(), 1u);
  EXPECT_EQ(reopened.value().manifest.textures().front().id, candidate.value().textures().front().id);
}

TEST_F(ProjectTextureCatalogTests, RejectsPhysicalTextureAliases) {
  write("first.png", "image");
  std::error_code error;
  std::filesystem::create_hard_link(root_ / "first.png", root_ / "second.png", error);
  if (error) {
    GTEST_SKIP() << "Host cannot create hard links: " << error.message();
  }
  const auto discovered = ProjectTextureCatalog::discover(
      workspace_.file_system, workspace_.manifest, scan());
  EXPECT_FALSE(discovered.ok());
  EXPECT_EQ(discovered.error().code, ProjectIoErrorCode::PathCaseCollision);
}

TEST_F(ProjectTextureCatalogTests, InvalidV5CatalogsAndDowngradedMetadataAreRejected) {
  const auto id = ProjectAssetId::generate();
  ASSERT_TRUE(workspace_.manifest.register_texture(
      {id, asset_path("image.png"), {}}).ok());
  const auto valid = nlohmann::json::parse(ProjectManifestStore::serialize(workspace_.manifest));
  auto candidate = valid;
  candidate["version"] = 4u;
  EXPECT_FALSE(ProjectManifestStore::deserialize(candidate.dump()).ok());
  candidate = valid;
  candidate["textures"][0]["aliases"] = nlohmann::json::array({std::string(id.str())});
  EXPECT_FALSE(ProjectManifestStore::deserialize(candidate.dump()).ok());
  candidate = valid;
  candidate["deletedTextureIds"] = nlohmann::json::array({std::string(id.str())});
  EXPECT_FALSE(ProjectManifestStore::deserialize(candidate.dump()).ok());
  candidate = valid;
  candidate["textures"][0]["path"] = ".quader/hidden.png";
  EXPECT_FALSE(ProjectManifestStore::deserialize(candidate.dump()).ok());
}

} // namespace

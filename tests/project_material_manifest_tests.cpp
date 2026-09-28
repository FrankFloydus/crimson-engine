////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/default_material_catalog.hpp"
#include "editor/session/project_session.hpp"
#include "io/project/project_manifest.hpp"
#include "io/project/project_material_catalog.hpp"
#include "io/project/project_material_provisioner.hpp"
#include "io/project/project_material_reference_resolver.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

using namespace quader;

namespace {

std::filesystem::path source_resource_root() {
  return std::filesystem::path(QUADER_SOURCE_DIR) / "resources" /
         "modeling_assets";
}

std::filesystem::path unique_temp_directory(std::string_view label) {
  const auto tick = static_cast<std::uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());
  return std::filesystem::temp_directory_path() /
         ("quader-material-manifest-" + std::string(label) + "-" +
          std::to_string(tick));
}

/// Owns one disposable root for manifest and provisioning tests.
class ScopedTestDirectory final {
public:
  explicit ScopedTestDirectory(std::string_view label)
      : path_(unique_temp_directory(label)) {
    std::error_code error;
    std::filesystem::create_directories(path_, error);
    EXPECT_FALSE(error) << error.message();
  }

  ~ScopedTestDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }

  [[nodiscard]] std::filesystem::path
  make_empty_project(std::string_view name) const {
    const std::filesystem::path root = path_ / std::string(name);
    std::error_code error;
    std::filesystem::create_directory(root, error);
    EXPECT_FALSE(error) << error.message();
    return root;
  }

  [[nodiscard]] const std::filesystem::path &path() const noexcept {
    return path_;
  }

private:
  std::filesystem::path path_;
};

void write_bytes(const std::filesystem::path &path,
                 std::string_view bytes) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(stream.is_open()) << path.string();
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  stream.close();
  ASSERT_TRUE(stream.good());
}

std::string read_bytes(const std::filesystem::path &path) {
  std::ifstream stream(path, std::ios::binary);
  EXPECT_TRUE(stream.is_open()) << path.string();
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

ProjectWorkspaceStorage create_workspace(
    const std::filesystem::path &root) {
  ProjectIoResult<ProjectWorkspaceStorage> created =
      ProjectManifestStore::create_in_empty_directory(root);
  EXPECT_TRUE(created.ok()) << created.error().message;
  return created.take_value();
}

ProjectMaterialCatalog scan_materials(
    const ProjectFileSystem &file_system) {
  ProjectIoResult<ProjectMaterialCatalog> scanned =
      ProjectMaterialCatalog::scan(file_system);
  EXPECT_TRUE(scanned.ok()) << scanned.error().message;
  return scanned.take_value();
}

ProjectRelativePath project_path(std::string_view text) {
  ProjectIoResult<ProjectRelativePath> parsed =
      ProjectRelativePath::parse_portable(text);
  EXPECT_TRUE(parsed.ok()) << parsed.error().message;
  return parsed.take_value();
}

std::string manifest_without_active_material(const ProjectId &project_id,
                                             std::uint32_t version) {
  return "{\n"
         "  \"format\": \"quader.project\",\n"
         "  \"version\": " +
         std::to_string(version) +
         ",\n"
         "  \"project\": {\n"
         "    \"id\": \"" +
         std::string(project_id.str()) +
         "\",\n"
         "    \"name\": \"Manifest\"\n"
         "  },\n"
         "  \"scenes\": [],\n"
         "  \"workspace\": {\n"
         "    \"openScenes\": [],\n"
         "    \"activeScene\": null\n"
         "  }\n"
         "}";
}

} // namespace

TEST(ProjectMaterialManifestTests,
     ReadsV2WithoutRetiredMaterialSelection) {
  const ProjectId project_id = ProjectId::generate();
  const std::string manifest =
      manifest_without_active_material(project_id, 2u);

  ProjectIoResult<ProjectManifest> decoded =
      ProjectManifestStore::deserialize(manifest);
  ASSERT_TRUE(decoded.ok()) << decoded.error().message;

  const std::string serialized =
      ProjectManifestStore::serialize(decoded.value());
  EXPECT_NE(serialized.find("\"version\": 5"), std::string::npos);
  EXPECT_EQ(decoded.value().minimum_product_release(), kInternalFullRelease);
  EXPECT_EQ(serialized.find("\"activeMaterial\""), std::string::npos);
}

TEST(ProjectMaterialManifestTests,
     ReadsV1AndWritesV5WithoutRetiredMaterialSelection) {
  const ProjectId project_id = ProjectId::generate();
  ProjectIoResult<ProjectManifest> decoded =
      ProjectManifestStore::deserialize(
          manifest_without_active_material(project_id, 1u));
  ASSERT_TRUE(decoded.ok()) << decoded.error().message;

  const std::string serialized =
      ProjectManifestStore::serialize(decoded.value());
  EXPECT_NE(serialized.find("\"version\": 5"), std::string::npos);
  EXPECT_EQ(decoded.value().minimum_product_release(), kInternalFullRelease);
  EXPECT_EQ(serialized.find("\"activeMaterial\""), std::string::npos);
}

TEST(ProjectMaterialManifestTests,
     V5PreservesStableSortedPermanentDeletionIdentities) {
  auto created = ProjectManifest::create(ProjectId::generate(), "Manifest");
  ASSERT_TRUE(created.ok());
  ProjectManifest manifest = created.take_value();
  const ProjectAssetId first =
      *ProjectAssetId::parse("10000000-0000-4000-8000-000000000001");
  const ProjectAssetId second =
      *ProjectAssetId::parse("20000000-0000-4000-8000-000000000002");
  const ProjectAssetId third =
      *ProjectAssetId::parse("30000000-0000-4000-8000-000000000003");
  EXPECT_EQ(nlohmann::json::parse(ProjectManifestStore::serialize(manifest))
                .at("version"),
            5u);
  ASSERT_TRUE(manifest.mark_materials_deleted(
                          std::array{third, first, third})
                  .ok());
  ASSERT_TRUE(manifest.mark_materials_deleted(std::array{second, first}).ok());
  const std::string serialized = ProjectManifestStore::serialize(manifest);
  const nlohmann::json document = nlohmann::json::parse(serialized);
  EXPECT_EQ(document.at("version"), 5u);
  EXPECT_EQ(document.at("deletedMaterialIds"),
            nlohmann::json::array({std::string(first.str()),
                                   std::string(second.str()),
                                   std::string(third.str())}));

  const auto decoded = ProjectManifestStore::deserialize(serialized);
  ASSERT_TRUE(decoded.ok()) << decoded.error().message;
  ASSERT_EQ(decoded.value().deleted_material_ids().size(), 3u);
  EXPECT_TRUE(decoded.value().is_material_deleted(first));
  EXPECT_TRUE(decoded.value().is_material_deleted(second));
  EXPECT_TRUE(decoded.value().is_material_deleted(third));
  EXPECT_EQ(ProjectManifestStore::serialize(decoded.value()), serialized);
}

TEST(ProjectMaterialManifestTests,
     InvalidDeletionBatchesPreserveExistingIdentities) {
  auto created = ProjectManifest::create(ProjectId::generate(), "Manifest");
  ASSERT_TRUE(created.ok());
  ProjectManifest manifest = created.take_value();
  const ProjectAssetId deleted = ProjectAssetId::generate();
  const ProjectAssetId active = ProjectAssetId::generate();
  const ProjectAssetId default_id =
      *ProjectAssetId::parse(kDefaultProjectMaterialId);
  ASSERT_TRUE(manifest.mark_materials_deleted(std::array{deleted}).ok());
  const std::string before = ProjectManifestStore::serialize(manifest);

  const auto reserved =
      manifest.mark_materials_deleted(std::array{active, default_id});
  EXPECT_FALSE(reserved.ok());
  EXPECT_EQ(reserved.error().code, ProjectIoErrorCode::InvalidArgument);
  EXPECT_EQ(ProjectManifestStore::serialize(manifest), before);
  const auto invalid =
      manifest.mark_materials_deleted(std::array{active, ProjectAssetId{}});
  EXPECT_FALSE(invalid.ok());
  EXPECT_EQ(invalid.error().code, ProjectIoErrorCode::InvalidUuid);
  EXPECT_EQ(ProjectManifestStore::serialize(manifest), before);
}

TEST(ProjectMaterialManifestTests,
     V3RejectsMalformedReservedAndDuplicateDeletedIdentities) {
  const nlohmann::json base = nlohmann::json::parse(
      manifest_without_active_material(ProjectId::generate(), 3u));
  EXPECT_FALSE(ProjectManifestStore::deserialize(base.dump()).ok());
  const std::string id = std::string(ProjectAssetId::generate().str());
  const std::vector<nlohmann::json> invalid_values = {
      nullptr,
      id,
      nlohmann::json::object(),
      nlohmann::json::array({42}),
      nlohmann::json::array({"not-a-uuid"}),
      nlohmann::json::array({std::string(kDefaultProjectMaterialId)}),
      nlohmann::json::array({id, id}),
  };
  for (const nlohmann::json &value : invalid_values) {
    nlohmann::json document = base;
    document["deletedMaterialIds"] = value;
    const auto decoded = ProjectManifestStore::deserialize(document.dump());
    EXPECT_FALSE(decoded.ok()) << document.dump();
    EXPECT_EQ(decoded.error().code, ProjectIoErrorCode::ManifestMalformed);
  }
}

TEST(ProjectMaterialManifestTests,
     LegacyAndUnknownVersionsCannotSilentlyLosePermanentDeletionState) {
  nlohmann::json document = nlohmann::json::parse(
      manifest_without_active_material(ProjectId::generate(), 3u));
  document["deletedMaterialIds"] = nlohmann::json::array(
      {std::string(ProjectAssetId::generate().str())});
  for (const std::uint32_t version : {1u, 2u}) {
    document["version"] = version;
    const auto decoded = ProjectManifestStore::deserialize(document.dump());
    EXPECT_FALSE(decoded.ok());
    EXPECT_EQ(decoded.error().code, ProjectIoErrorCode::ManifestMalformed);
  }
  for (const std::uint64_t version : {6ull, 4'294'967'299ull}) {
    document["version"] = version;
    const auto decoded = ProjectManifestStore::deserialize(document.dump());
    EXPECT_FALSE(decoded.ok());
    EXPECT_EQ(decoded.error().code, ProjectIoErrorCode::UnsupportedFormat);
  }
}

TEST(ProjectMaterialManifestTests,
     OpeningLegacyAndV3ManifestsDoesNotRewriteTheirBytes) {
  ScopedTestDirectory temporary("read-only-open");
  const auto root = temporary.make_empty_project("Manifest");
  const auto workspace = create_workspace(root);
  for (const std::uint32_t version : {1u, 2u, 3u}) {
    nlohmann::json document = nlohmann::json::parse(
        manifest_without_active_material(workspace.manifest.id(), version));
    if (version == 3u) {
      document["deletedMaterialIds"] = nlohmann::json::array(
          {std::string(ProjectAssetId::generate().str())});
    }
    const std::string original = document.dump(4) + "\n";
    write_bytes(workspace.manifest_path, original);
    const auto opened = ProjectManifestStore::open(workspace.manifest_path);
    ASSERT_TRUE(opened.ok()) << opened.error().message;
    EXPECT_EQ(read_bytes(workspace.manifest_path), original);
    EXPECT_EQ(opened.value().manifest.deleted_material_ids().size(),
              version == 3u ? 1u : 0u);
    EXPECT_EQ(opened.value().manifest.minimum_product_release(),
              kInternalFullRelease);
  }
}

TEST(ProjectMaterialReferenceResolverTests,
     MapsDeletedIdsOnlyAndKeepsDefaultAndUnknownIdentitiesUnchanged) {
  auto created = ProjectManifest::create(ProjectId::generate(), "Manifest");
  ASSERT_TRUE(created.ok());
  ProjectManifest manifest = created.take_value();
  const ProjectAssetId deleted = ProjectAssetId::generate();
  const ProjectAssetId unknown = ProjectAssetId::generate();
  const ProjectAssetId default_id =
      *ProjectAssetId::parse(kDefaultProjectMaterialId);
  ASSERT_TRUE(manifest.mark_materials_deleted(std::array{deleted}).ok());
  EXPECT_EQ(ProjectMaterialReferenceResolver::canonical_material_id(manifest,
                                                                    deleted),
            default_id);
  EXPECT_EQ(ProjectMaterialReferenceResolver::canonical_material_id(manifest,
                                                                    unknown),
            unknown);
  EXPECT_EQ(ProjectMaterialReferenceResolver::canonical_material_id(manifest,
                                                                    default_id),
            default_id);
  EXPECT_EQ(ProjectMaterialReferenceResolver::canonical_material_id(
                manifest, ProjectAssetId{}),
            ProjectAssetId{});
}

TEST(ProjectMaterialManifestTests,
     LegacyMaterialSelectionIsValidatedAndDiscardedWithoutLosingDeletionState) {
  for (const std::uint32_t version : {2u, 3u}) {
    nlohmann::json document = nlohmann::json::parse(
        manifest_without_active_material(ProjectId::generate(), version));
    const ProjectAssetId deleted = ProjectAssetId::generate();
    if (version == 3u) {
      document["deletedMaterialIds"] =
          nlohmann::json::array({std::string(deleted.str())});
    }
    for (const nlohmann::json &legacy :
         std::vector<nlohmann::json>{nullptr, std::string(deleted.str())}) {
      document["workspace"]["activeMaterial"] = legacy;
      const auto decoded = ProjectManifestStore::deserialize(document.dump());
      ASSERT_TRUE(decoded.ok()) << decoded.error().message;
      const auto saved = nlohmann::json::parse(
          ProjectManifestStore::serialize(decoded.value()));
      EXPECT_FALSE(saved.at("workspace").contains("activeMaterial"));
      EXPECT_EQ(saved.at("version"), 5u);
      EXPECT_EQ(saved.at("minimumProductRelease"), kInternalFullRelease);
      EXPECT_EQ(decoded.value().is_material_deleted(deleted), version == 3u);
    }
    for (const nlohmann::json &invalid :
         std::vector<nlohmann::json>{42, "not-a-uuid", nlohmann::json::array()}) {
      document["workspace"]["activeMaterial"] = invalid;
      const auto decoded = ProjectManifestStore::deserialize(document.dump());
      EXPECT_FALSE(decoded.ok());
      EXPECT_EQ(decoded.error().code, ProjectIoErrorCode::ManifestMalformed);
    }
  }
}

TEST(ProjectMaterialManifestTests,
     OpeningLegacyMaterialSelectionRetainsDefaultAndOmitsSelectionOnSave) {
  ScopedTestDirectory temporary("stale-active");
  const std::filesystem::path root =
      temporary.make_empty_project("StaleActive");
  ProjectSession creator;
  creator.set_resource_root(source_resource_root());
  ASSERT_TRUE(creator.create(root).ok);
  const std::filesystem::path manifest_path = creator.manifest_path();
  ASSERT_TRUE(creator.close().ok);

  auto document = nlohmann::json::parse(read_bytes(manifest_path));
  document["workspace"]["activeMaterial"] =
      std::string(ProjectAssetId::generate().str());
  write_bytes(manifest_path, document.dump());

  ProjectSession reopened;
  reopened.set_resource_root(source_resource_root());
  const ProjectSessionResult opened = reopened.open(manifest_path);
  ASSERT_TRUE(opened.ok) << opened.message;
  const auto default_id = ProjectAssetId::parse(kDefaultProjectMaterialId);
  ASSERT_TRUE(default_id.has_value());
  EXPECT_NE(reopened.material_catalog().find(*default_id), nullptr);
  ASSERT_TRUE(reopened.close().ok);
  const auto persisted = nlohmann::json::parse(read_bytes(manifest_path));
  EXPECT_FALSE(persisted.at("workspace").contains("activeMaterial"));
  EXPECT_EQ(persisted.at("version"), 5u);
}

TEST(ProjectMaterialManifestTests,
     V4RequiresANonzeroUnsignedProductRelease) {
  auto created = ProjectManifest::create(ProjectId::generate(), "Manifest");
  ASSERT_TRUE(created.ok());
  const nlohmann::json base = nlohmann::json::parse(
      ProjectManifestStore::serialize(created.value()));
  nlohmann::json missing = base;
  missing.erase("minimumProductRelease");
  EXPECT_FALSE(ProjectManifestStore::deserialize(missing.dump()).ok());
  const std::vector<nlohmann::json> invalid_values{
      nullptr, false, 0u, -1, 1.0, "1", nlohmann::json::array(),
      nlohmann::json::object()};
  for (const nlohmann::json &value : invalid_values) {
    nlohmann::json document = base;
    document["minimumProductRelease"] = value;
    const auto decoded = ProjectManifestStore::deserialize(document.dump());
    EXPECT_FALSE(decoded.ok()) << document.dump();
    EXPECT_EQ(decoded.error().code, ProjectIoErrorCode::ManifestMalformed);
  }
  const auto invalid_hydration = ProjectManifest::hydrate(
      ProjectId::generate(), "Manifest", {}, {}, std::nullopt, {}, 0u);
  EXPECT_FALSE(invalid_hydration.ok());
}

TEST(ProjectMaterialManifestTests,
     V4PreservesProductReleaseOnReadAndOnlyRaisesItExplicitly) {
  auto created = ProjectManifest::create(ProjectId::generate(), "Manifest");
  ASSERT_TRUE(created.ok());
  EXPECT_EQ(created.value().minimum_product_release(), current_product_release());
  const nlohmann::json base = nlohmann::json::parse(
      ProjectManifestStore::serialize(created.value()));
  for (const ProductRelease release :
       {ProductRelease{1u}, ProductRelease{2u}, kInternalFullRelease}) {
    nlohmann::json document = base;
    document["minimumProductRelease"] = release;
    auto decoded = ProjectManifestStore::deserialize(document.dump());
    ASSERT_TRUE(decoded.ok()) << decoded.error().message;
    EXPECT_EQ(decoded.value().minimum_product_release(), release);
    const auto saved = nlohmann::json::parse(
        ProjectManifestStore::serialize(decoded.value()));
    EXPECT_EQ(saved.at("minimumProductRelease"), release);
  }

  auto hydrated = ProjectManifest::hydrate(
      ProjectId::generate(), "Manifest", {}, {}, std::nullopt, {}, 1u);
  ASSERT_TRUE(hydrated.ok());
  auto &manifest = hydrated.value();
  manifest.require_product_release(2u);
  EXPECT_EQ(manifest.minimum_product_release(), 2u);
  manifest.require_product_release(1u);
  EXPECT_EQ(manifest.minimum_product_release(), 2u);
  manifest.require_product_release(kInternalFullRelease);
  manifest.require_product_release(2u);
  EXPECT_EQ(manifest.minimum_product_release(), kInternalFullRelease);
}

TEST(ProjectMaterialProvisionerTests,
     BackfillIsIdempotentAndRollbackRemovesOnlyCreatedEntries) {
  ScopedTestDirectory temporary("idempotent");
  const std::filesystem::path root =
      temporary.make_empty_project("Idempotent");
  ProjectWorkspaceStorage workspace = create_workspace(root);
  const ProjectMaterialCatalog empty_catalog =
      scan_materials(workspace.file_system);

  ProjectIoResult<ProjectMaterialProvisioningChanges> first =
      ProjectMaterialProvisioner::ensure_default(
          workspace.file_system, source_resource_root(), empty_catalog);
  ASSERT_TRUE(first.ok()) << first.error().message;
  EXPECT_EQ(first.value().created_files.size(), 2u);
  EXPECT_EQ(first.value().created_directories.size(), 2u);

  const std::filesystem::path qmat =
      root / std::filesystem::path(kDefaultProjectMaterialPath);
  const std::filesystem::path albedo =
      root / std::filesystem::path(kDefaultProjectAlbedoTexturePath);
  ASSERT_TRUE(std::filesystem::is_regular_file(qmat));
  ASSERT_TRUE(std::filesystem::is_regular_file(albedo));

  const ProjectMaterialCatalog installed_catalog =
      scan_materials(workspace.file_system);
  ProjectIoResult<ProjectMaterialProvisioningChanges> second =
      ProjectMaterialProvisioner::ensure_default(
          workspace.file_system, source_resource_root(), installed_catalog);
  ASSERT_TRUE(second.ok()) << second.error().message;
  EXPECT_TRUE(second.value().empty());

  ProjectMaterialProvisioner::rollback(second.value());
  EXPECT_TRUE(std::filesystem::is_regular_file(qmat));
  EXPECT_TRUE(std::filesystem::is_regular_file(albedo));

  ProjectMaterialProvisioner::rollback(first.value());
  EXPECT_FALSE(std::filesystem::exists(qmat));
  EXPECT_FALSE(std::filesystem::exists(albedo));
  EXPECT_FALSE(std::filesystem::exists(root / "Materials"));
  EXPECT_FALSE(std::filesystem::exists(root / "Textures"));
  EXPECT_TRUE(std::filesystem::is_directory(root / "Scenes"));
  EXPECT_TRUE(std::filesystem::is_regular_file(workspace.manifest_path));
}

TEST(ProjectMaterialProvisionerTests,
     PackagedV2DefaultUsesReservedIdentityKindAndAlbedoReference) {
  const std::filesystem::path qmat =
      source_resource_root() /
      std::filesystem::path(kDefaultProjectMaterialTemplatePath);
  ProjectIoResult<ProjectMaterialDocument> decoded =
      QmatCodec::decode(read_bytes(qmat));
  ASSERT_TRUE(decoded.ok()) << decoded.error().message;
  EXPECT_EQ(decoded.value().id,
            ProjectAssetId::parse(kDefaultProjectMaterialId));
  EXPECT_EQ(decoded.value().kind, ProjectMaterialKind::Standard);
  ASSERT_NE(decoded.value().standard_material(), nullptr);
  ASSERT_TRUE(
      decoded.value().standard_material()->base_color_texture.has_value());
  EXPECT_EQ(decoded.value().standard_material()->base_color_texture->id,
            ProjectAssetId::parse(kDefaultProjectAlbedoTextureId));
  EXPECT_EQ(decoded.value()
                .standard_material()
                ->base_color_texture->project_relative_path,
            kDefaultProjectAlbedoTexturePath);
}

TEST(ProjectMaterialProvisionerTests,
     PackagedAuthoredParametersDoNotChangeTheReservedContract) {
  ScopedTestDirectory temporary("packaged-authored-payload");
  const std::filesystem::path root =
      temporary.make_empty_project("PackagedAuthoredPayload");
  ProjectWorkspaceStorage workspace = create_workspace(root);
  const ProjectMaterialCatalog catalog =
      scan_materials(workspace.file_system);

  const std::filesystem::path resource_root =
      temporary.path() / "resources";
  const std::filesystem::path resource_qmat =
      resource_root /
      std::filesystem::path(kDefaultProjectMaterialTemplatePath);
  const std::filesystem::path resource_albedo =
      resource_root /
      std::filesystem::path(kDefaultProjectAlbedoTemplatePath);
  ASSERT_TRUE(std::filesystem::create_directories(resource_qmat.parent_path()));

  ProjectIoResult<ProjectMaterialDocument> authored = QmatCodec::decode(
      read_bytes(source_resource_root() /
                 std::filesystem::path(
                     kDefaultProjectMaterialTemplatePath)));
  ASSERT_TRUE(authored.ok()) << authored.error().message;
  authored.value().name = "Packaged Authored Default";
  ASSERT_NE(authored.value().standard_material(), nullptr);
  authored.value().standard_material()->base_color = {0.2f, 0.4f, 0.6f};
  authored.value().standard_material()->roughness = 0.25f;
  ProjectIoResult<std::string> encoded =
      QmatCodec::encode(authored.value());
  ASSERT_TRUE(encoded.ok()) << encoded.error().message;
  write_bytes(resource_qmat, encoded.value());
  write_bytes(resource_albedo,
              read_bytes(source_resource_root() /
                         std::filesystem::path(
                             kDefaultProjectAlbedoTemplatePath)));

  ProjectIoResult<ProjectMaterialProvisioningChanges> provisioned =
      ProjectMaterialProvisioner::ensure_default(
          workspace.file_system, resource_root, catalog);
  ASSERT_TRUE(provisioned.ok()) << provisioned.error().message;
  EXPECT_EQ(read_bytes(root /
                       std::filesystem::path(kDefaultProjectMaterialPath)),
            encoded.value());
}

TEST(ProjectMaterialProvisionerTests,
     AcceptsEditedDefaultPayloadWithoutRestoringTemplateData) {
  ScopedTestDirectory temporary("editable-default");
  const std::filesystem::path root =
      temporary.make_empty_project("EditableDefault");
  ProjectWorkspaceStorage workspace = create_workspace(root);
  const ProjectMaterialCatalog empty_catalog =
      scan_materials(workspace.file_system);
  ASSERT_TRUE(ProjectMaterialProvisioner::ensure_default(
                  workspace.file_system, source_resource_root(),
                  empty_catalog)
                  .ok());

  const ProjectRelativePath qmat_path =
      project_path(kDefaultProjectMaterialPath);
  ProjectIoResult<QmatDecodedFile> decoded =
      QmatCodec::read(workspace.file_system, qmat_path);
  ASSERT_TRUE(decoded.ok()) << decoded.error().message;
  decoded.value().material.name = "Authored Default";
  ASSERT_NE(decoded.value().material.standard_material(), nullptr);
  decoded.value().material.standard_material()->roughness = 0.35f;
  decoded.value().material.standard_material()->base_color_texture.reset();
  ASSERT_TRUE(QmatCodec::write_atomic(workspace.file_system, qmat_path,
                                     decoded.value().material)
                  .ok());
  const std::filesystem::path qmat = root / qmat_path.native_path();
  const std::string authored_bytes = read_bytes(qmat);

  const ProjectMaterialCatalog edited_catalog =
      scan_materials(workspace.file_system);
  ProjectIoResult<ProjectMaterialProvisioningChanges> provisioned =
      ProjectMaterialProvisioner::ensure_default(
          workspace.file_system, source_resource_root(), edited_catalog);
  ASSERT_TRUE(provisioned.ok()) << provisioned.error().message;
  EXPECT_TRUE(provisioned.value().empty());
  EXPECT_EQ(read_bytes(qmat), authored_bytes);
}

TEST(ProjectMaterialProvisionerTests,
     AcceptsExistingV1DefaultWithoutOverwritingOrUpgradingIt) {
  ScopedTestDirectory temporary("legacy-default");
  const std::filesystem::path root =
      temporary.make_empty_project("LegacyDefault");
  ProjectWorkspaceStorage workspace = create_workspace(root);
  ASSERT_TRUE(std::filesystem::create_directory(root / "Materials"));
  const std::filesystem::path qmat =
      root / std::filesystem::path(kDefaultProjectMaterialPath);
  const std::string legacy =
      "{\n"
      "  \"format\": \"quader.material\",\n"
      "  \"version\": 1,\n"
      "  \"id\": \"" +
      std::string(kDefaultProjectMaterialId) +
      "\",\n"
      "  \"name\": \"Legacy Default\",\n"
      "  \"model\": \"metallicRoughness\",\n"
      "  \"parameters\": {\n"
      "    \"baseColor\": [0.2, 0.3, 0.4],\n"
      "    \"roughness\": 0.6,\n"
      "    \"metallic\": 0.1\n"
      "  },\n"
      "  \"textureSlots\": {}\n"
      "}";
  write_bytes(qmat, legacy);

  const ProjectMaterialCatalog catalog =
      scan_materials(workspace.file_system);
  ProjectIoResult<ProjectMaterialProvisioningChanges> provisioned =
      ProjectMaterialProvisioner::ensure_default(
          workspace.file_system, source_resource_root(), catalog);
  ASSERT_TRUE(provisioned.ok()) << provisioned.error().message;
  EXPECT_TRUE(provisioned.value().empty());
  EXPECT_EQ(read_bytes(qmat), legacy);
  EXPECT_FALSE(std::filesystem::exists(root / "Textures"));
}

TEST(ProjectMaterialProvisionerTests,
     CorruptReservedQmatFailsWithoutOverwriteAndRollsBackBackfill) {
  ScopedTestDirectory temporary("corrupt");
  const std::filesystem::path root =
      temporary.make_empty_project("Corrupt");
  ProjectWorkspaceStorage workspace = create_workspace(root);
  ASSERT_TRUE(std::filesystem::create_directory(root / "Materials"));
  const std::filesystem::path qmat =
      root / std::filesystem::path(kDefaultProjectMaterialPath);
  constexpr std::string_view kUserBytes = "{not-valid-qmat";
  write_bytes(qmat, kUserBytes);
  const ProjectMaterialCatalog catalog =
      scan_materials(workspace.file_system);
  ASSERT_EQ(catalog.warnings().size(), 1u);

  ProjectIoResult<ProjectMaterialProvisioningChanges> provisioned =
      ProjectMaterialProvisioner::ensure_default(
          workspace.file_system, source_resource_root(), catalog);
  EXPECT_FALSE(provisioned.ok());
  EXPECT_EQ(read_bytes(qmat), kUserBytes);
  EXPECT_FALSE(std::filesystem::exists(root / "Textures"));
}

TEST(ProjectMaterialProvisionerTests,
     MissingPackagedDefaultFailsBeforeProjectMutation) {
  ScopedTestDirectory temporary("missing-resource");
  const std::filesystem::path root =
      temporary.make_empty_project("MissingResource");
  ProjectWorkspaceStorage workspace = create_workspace(root);
  const ProjectMaterialCatalog catalog =
      scan_materials(workspace.file_system);

  ProjectIoResult<ProjectMaterialProvisioningChanges> provisioned =
      ProjectMaterialProvisioner::ensure_default(
          workspace.file_system, temporary.path() / "missing-resources",
          catalog);
  EXPECT_FALSE(provisioned.ok());
  EXPECT_FALSE(std::filesystem::exists(root / "Materials"));
  EXPECT_FALSE(std::filesystem::exists(root / "Textures"));
}

TEST(ProjectMaterialProvisionerTests,
     EditedDefaultAlbedoIsAcceptedWithoutOverwritingUserBytes) {
  ScopedTestDirectory temporary("albedo-conflict");
  const std::filesystem::path root =
      temporary.make_empty_project("AlbedoConflict");
  ProjectWorkspaceStorage workspace = create_workspace(root);
  ASSERT_TRUE(std::filesystem::create_directory(root / "Materials"));
  ASSERT_TRUE(std::filesystem::create_directory(root / "Textures"));

  const std::filesystem::path qmat =
      root / std::filesystem::path(kDefaultProjectMaterialPath);
  const std::filesystem::path albedo =
      root / std::filesystem::path(kDefaultProjectAlbedoTexturePath);
  write_bytes(qmat,
              read_bytes(source_resource_root() /
                         std::filesystem::path(
                             kDefaultProjectMaterialTemplatePath)));
  constexpr std::string_view kUserBytes = "user-albedo";
  write_bytes(albedo, kUserBytes);
  const ProjectMaterialCatalog catalog =
      scan_materials(workspace.file_system);

  ProjectIoResult<ProjectMaterialProvisioningChanges> provisioned =
      ProjectMaterialProvisioner::ensure_default(
          workspace.file_system, source_resource_root(), catalog);
  ASSERT_TRUE(provisioned.ok()) << provisioned.error().message;
  EXPECT_TRUE(provisioned.value().empty());
  EXPECT_EQ(read_bytes(albedo), kUserBytes);
}

TEST(ProjectMaterialProvisionerTests,
     ConflictingBuiltInMaterialUuidFailsBeforeBackfill) {
  ScopedTestDirectory temporary("material-conflict");
  const std::filesystem::path root =
      temporary.make_empty_project("MaterialConflict");
  ProjectWorkspaceStorage workspace = create_workspace(root);
  ASSERT_TRUE(std::filesystem::create_directory(root / "Materials"));

  std::string conflicting = read_bytes(
      source_resource_root() /
      std::filesystem::path(kDefaultProjectMaterialTemplatePath));
  const std::size_t name = conflicting.find("Default Material");
  ASSERT_NE(name, std::string::npos);
  conflicting.replace(name, std::string("Default Material").size(),
                      "Conflicting Default");
  const std::filesystem::path conflict_path =
      root / "Materials" / "conflict.qmat";
  write_bytes(conflict_path, conflicting);
  const ProjectMaterialCatalog catalog =
      scan_materials(workspace.file_system);

  ProjectIoResult<ProjectMaterialProvisioningChanges> provisioned =
      ProjectMaterialProvisioner::ensure_default(
          workspace.file_system, source_resource_root(), catalog);
  EXPECT_FALSE(provisioned.ok());
  EXPECT_EQ(read_bytes(conflict_path), conflicting);
  EXPECT_FALSE(std::filesystem::exists(root / "Textures"));
  EXPECT_FALSE(std::filesystem::exists(
      root / std::filesystem::path(kDefaultProjectMaterialPath)));
}

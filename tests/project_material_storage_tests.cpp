////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/default_material_catalog.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/session/project_session.hpp"
#include "io/project/project_manifest.hpp"
#include "io/project/project_material_catalog.hpp"
#include "io/project/qmat_codec.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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
         ("quader-project-material-" + std::string(label) + "-" +
          std::to_string(tick));
}

/// Owns one disposable project root for QMAT storage tests.
class ScopedMaterialTestDirectory final {
public:
  explicit ScopedMaterialTestDirectory(std::string_view label)
      : path_(unique_temp_directory(label)) {
    std::error_code error;
    std::filesystem::create_directories(path_, error);
    EXPECT_FALSE(error) << error.message();
  }

  ~ScopedMaterialTestDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }

  [[nodiscard]] std::filesystem::path make_empty_project(
      std::string_view name) const {
    const std::filesystem::path root = path_ / std::string(name);
    std::error_code error;
    std::filesystem::create_directory(root, error);
    EXPECT_FALSE(error) << error.message();
    return root;
  }

private:
  std::filesystem::path path_;
};

ProjectRelativePath project_path(std::string_view text) {
  ProjectIoResult<ProjectRelativePath> path =
      ProjectRelativePath::parse_portable(text);
  EXPECT_TRUE(path.ok()) << path.error().message;
  return path.take_value();
}

ProjectMaterialDocument material_definition(
    std::string_view id, std::string name,
    std::string_view texture_id = {}) {
  ProjectMaterialDocument material;
  const std::optional<ProjectAssetId> parsed_id =
      ProjectAssetId::parse(id);
  EXPECT_TRUE(parsed_id.has_value());
  material.id = *parsed_id;
  material.name = std::move(name);
  ProjectStandardMaterialDefinition standard;
  standard.base_color = {0.25f, 0.5f, 0.75f};
  standard.roughness = 0.8f;
  standard.metallic = 0.1f;
  if (!texture_id.empty()) {
    const std::optional<ProjectAssetId> parsed_texture =
        ProjectAssetId::parse(texture_id);
    EXPECT_TRUE(parsed_texture.has_value());
    standard.base_color_texture = ProjectMaterialTextureSlot{
        .id = *parsed_texture,
        .project_relative_path = "Textures/albedo.png",
        .color_space = ProjectTextureColorSpace::Srgb,
    };
  }
  material.payload = std::move(standard);
  return material;
}

ProjectStandardMaterialDefinition &
standard_material(ProjectMaterialDocument &material) {
  EXPECT_NE(material.standard_material(), nullptr);
  return *material.standard_material();
}

const ProjectStandardMaterialDefinition &
standard_material(const ProjectMaterialDocument &material) {
  EXPECT_NE(material.standard_material(), nullptr);
  return *material.standard_material();
}

void write_text(const std::filesystem::path &path, std::string_view text) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(stream.is_open()) << path.string();
  stream.write(text.data(), static_cast<std::streamsize>(text.size()));
  stream.close();
  ASSERT_TRUE(stream.good());
}

std::string read_text(const std::filesystem::path &path) {
  std::ifstream stream(path, std::ios::binary);
  EXPECT_TRUE(stream.is_open()) << path.string();
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

std::string replace_once(std::string text, std::string_view before,
                         std::string_view after) {
  const std::size_t position = text.find(before);
  EXPECT_NE(position, std::string::npos) << before;
  if (position != std::string::npos) {
    text.replace(position, before.size(), after);
  }
  return text;
}

TEST(ProjectAssetIdentity, ParsesCanonicalUuidAsCheapValueIdentity) {
  const ProjectAssetId generated = ProjectAssetId::generate();
  ASSERT_TRUE(generated.valid());
  ASSERT_EQ(generated.str().size(), 36u);
  const std::optional<ProjectAssetId> parsed =
      ProjectAssetId::parse(generated.str());
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(*parsed, generated);
  EXPECT_EQ(ProjectAssetId::from_bytes(generated.bytes()), generated);
  EXPECT_FALSE(ProjectAssetId::parse("not-a-uuid").has_value());
}

TEST(QmatCodecTests, RoundTripsDeterministicallyAndValidatesInput) {
  ProjectMaterialDocument material = material_definition(
      "64cc0de8-35da-4a84-bf30-bd5c87bc40de", "Paint",
      "b1a49a62-812b-48b2-9fc5-fd6558aef5c2");
  ProjectStandardMaterialDefinition &standard = standard_material(material);
  standard.roughness_texture = ProjectMaterialTextureSlot{
      .id = *ProjectAssetId::parse(
          "01c0aa78-ac13-48e9-ac63-019443ca92af"),
      .project_relative_path = "Textures/roughness.png",
      .color_space = ProjectTextureColorSpace::Linear,
  };
  standard.metallic_texture = ProjectMaterialTextureSlot{
      .id = *ProjectAssetId::parse(
          "35054e2f-fec6-425c-8735-c6960b5144be"),
      .project_relative_path = "Textures/metallic.png",
      .color_space = ProjectTextureColorSpace::Linear,
  };
  standard.normal_texture = ProjectMaterialTextureSlot{
      .id = *ProjectAssetId::parse(
          "bb17816e-e0ba-440a-ab3b-51364decaec8"),
      .project_relative_path = "Textures/normal.png",
      .color_space = ProjectTextureColorSpace::Linear,
  };
  standard.normal_strength = 1.5f;
  standard.normal_convention = NormalMapConvention::DirectX;
  standard.texture_transform = {
      .center = {0.25f, 0.75f},
      .scale = {2.0f, -3.0f},
      .offset = {-0.5f, 1.25f},
      .rotation_degrees = 45.0f,
  };
  ProjectIoResult<std::string> first = QmatCodec::encode(material);
  ASSERT_TRUE(first.ok()) << first.error().message;
  ProjectIoResult<ProjectMaterialDocument> decoded =
      QmatCodec::decode(first.value());
  ASSERT_TRUE(decoded.ok()) << decoded.error().message;
  EXPECT_EQ(decoded.value(), material);
  ProjectIoResult<std::string> second =
      QmatCodec::encode(decoded.value());
  ASSERT_TRUE(second.ok());
  EXPECT_EQ(second.value(), first.value());

  std::string additive = first.value();
  additive.insert(additive.find('{') + 1u, R"("futureField":42,)");
  EXPECT_TRUE(QmatCodec::decode(additive).ok());
  EXPECT_TRUE(QmatCodec::decode(replace_once(
                  first.value(), "\"roughness\": 0.8",
                  "\"futureParameter\": true,\n    \"roughness\": 0.8"))
                  .ok());
  EXPECT_TRUE(QmatCodec::decode(replace_once(
                  first.value(), "\"colorSpace\": \"srgb\"",
                  "\"futureTextureField\": 7,\n      \"colorSpace\": \"srgb\""))
                  .ok());
  EXPECT_FALSE(QmatCodec::decode(replace_once(
                   first.value(), "\"rotationDegrees\": 45.0",
                   "\"rotationDegrees\": \"bad\""))
                   .ok());

  std::string unsupported_slot = first.value();
  const std::size_t slot_position =
      unsupported_slot.find("\"baseColorTexture\"");
  ASSERT_NE(slot_position, std::string::npos);
  unsupported_slot.replace(slot_position,
                           std::string("\"baseColorTexture\"").size(),
                           "\"heightTexture\"");
  EXPECT_FALSE(QmatCodec::decode(unsupported_slot).ok());

  ProjectMaterialDocument invalid = material;
  standard_material(invalid).roughness = 2.0f;
  EXPECT_FALSE(QmatCodec::encode(invalid).ok());
}

TEST(QmatCodecTests, ReadsV1AsStandardAndWritesTypedV2) {
  constexpr std::string_view kLegacy = R"json({
  "format": "quader.material",
  "version": 1,
  "id": "64cc0de8-35da-4a84-bf30-bd5c87bc40de",
  "name": "Legacy Paint",
  "model": "metallicRoughness",
  "parameters": {
    "baseColor": [0.25, 0.5, 0.75],
    "roughness": 0.8,
    "metallic": 0.1
  },
  "textureSlots": {
    "baseColorTexture": {
      "id": "e78f11d2-7055-4563-86cb-1e67a26c9aa5",
      "path": "Textures/legacy_albedo.png",
      "colorSpace": "linear"
    },
    "roughnessTexture": {
      "id": "e43f72db-481f-4f03-99ef-5a3a995b07a7",
      "path": "Textures/legacy_roughness.png",
      "colorSpace": "srgb"
    }
  }
})json";

  ProjectIoResult<ProjectMaterialDocument> decoded =
      QmatCodec::decode(kLegacy);
  ASSERT_TRUE(decoded.ok()) << decoded.error().message;
  EXPECT_EQ(decoded.value().kind, ProjectMaterialKind::Standard);
  EXPECT_EQ(decoded.value().material_version, kStandardMaterialVersion);
  ASSERT_NE(decoded.value().standard_material(), nullptr);
  EXPECT_FLOAT_EQ(decoded.value().standard_material()->normal_strength,
                  1.0f);
  EXPECT_EQ(decoded.value().standard_material()->normal_convention,
            NormalMapConvention::OpenGL);
  EXPECT_EQ(decoded.value().standard_material()->texture_transform,
            StandardMaterialTextureTransform{});
  EXPECT_FALSE(
      decoded.value().standard_material()->metallic_texture.has_value());
  EXPECT_FALSE(
      decoded.value().standard_material()->normal_texture.has_value());
  ASSERT_TRUE(
      decoded.value().standard_material()->base_color_texture.has_value());
  EXPECT_EQ(decoded.value()
                .standard_material()
                ->base_color_texture->color_space,
            ProjectTextureColorSpace::Srgb);
  ASSERT_TRUE(
      decoded.value().standard_material()->roughness_texture.has_value());
  EXPECT_EQ(decoded.value()
                .standard_material()
                ->roughness_texture->color_space,
            ProjectTextureColorSpace::Linear);

  ProjectIoResult<std::string> upgraded =
      QmatCodec::encode(decoded.value());
  ASSERT_TRUE(upgraded.ok()) << upgraded.error().message;
  EXPECT_NE(upgraded.value().find("\"version\": 2"), std::string::npos);
  EXPECT_NE(upgraded.value().find("\"materialType\": \"standard\""),
            std::string::npos);
  EXPECT_NE(upgraded.value().find("\"normalConvention\": \"opengl\""),
            std::string::npos);
  EXPECT_NE(upgraded.value().find("\"textureTransform\""),
            std::string::npos);
}

TEST(QmatCodecTests, RejectsUnsupportedUnsafeAndUnboundedValues) {
  const ProjectMaterialDocument material = material_definition(
      "64cc0de8-35da-4a84-bf30-bd5c87bc40de", "Paint",
      "b1a49a62-812b-48b2-9fc5-fd6558aef5c2");
  ProjectIoResult<std::string> encoded = QmatCodec::encode(material);
  ASSERT_TRUE(encoded.ok()) << encoded.error().message;

  EXPECT_FALSE(QmatCodec::decode(replace_once(
                   encoded.value(), "quader.material", "another.format"))
                   .ok());
  EXPECT_FALSE(QmatCodec::decode(replace_once(
                   encoded.value(), "\"version\": 2", "\"version\": 3"))
                   .ok());
  EXPECT_FALSE(QmatCodec::decode(replace_once(
                   encoded.value(), "\"materialType\": \"standard\"",
                   "\"materialType\": \"graph\""))
                   .ok());
  EXPECT_FALSE(QmatCodec::decode(replace_once(
                   encoded.value(), "\"materialVersion\": 1",
                   "\"materialVersion\": 2"))
                   .ok());
  EXPECT_FALSE(QmatCodec::decode(replace_once(
                   encoded.value(), "metallicRoughness", "unsupportedModel"))
                   .ok());
  EXPECT_FALSE(QmatCodec::decode(replace_once(
                   encoded.value(),
                   "64cc0de8-35da-4a84-bf30-bd5c87bc40de", "not-a-uuid"))
                   .ok());
  ProjectIoResult<ProjectMaterialDocument> escaping = QmatCodec::decode(
      replace_once(encoded.value(), "Textures/albedo.png", "../outside.png"));
  ASSERT_FALSE(escaping.ok());
  EXPECT_EQ(escaping.error().code, ProjectIoErrorCode::InvalidRelativePath);

  ProjectMaterialDocument invalid = material;
  standard_material(invalid).base_color[0] =
      std::numeric_limits<float>::quiet_NaN();
  EXPECT_FALSE(QmatCodec::encode(invalid).ok());
  invalid = material;
  invalid.name.assign(257u, 'n');
  EXPECT_FALSE(QmatCodec::encode(invalid).ok());
  invalid = material;
  standard_material(invalid).base_color_texture->color_space =
      static_cast<ProjectTextureColorSpace>(255u);
  EXPECT_FALSE(QmatCodec::encode(invalid).ok());
  invalid = material;
  ProjectIoResult<ProjectRelativePath> oversized_path =
      ProjectRelativePath::parse_portable(
          std::string(32'769u, 'p') + ".png");
  ASSERT_TRUE(oversized_path.ok()) << oversized_path.error().message;
  standard_material(invalid).base_color_texture->project_relative_path =
      oversized_path.value().portable_string();
  EXPECT_FALSE(QmatCodec::encode(invalid).ok());
  invalid = material;
  standard_material(invalid).normal_strength = 16.01f;
  EXPECT_FALSE(QmatCodec::encode(invalid).ok());
  invalid = material;
  standard_material(invalid).normal_strength = -16.0f;
  EXPECT_TRUE(QmatCodec::encode(invalid).ok());
  invalid = material;
  standard_material(invalid).normal_strength = -16.01f;
  EXPECT_FALSE(QmatCodec::encode(invalid).ok());
  invalid = material;
  standard_material(invalid).texture_transform.center[0] = 1.01f;
  EXPECT_FALSE(QmatCodec::encode(invalid).ok());
  invalid = material;
  standard_material(invalid).texture_transform.scale[1] = 16.01f;
  EXPECT_FALSE(QmatCodec::encode(invalid).ok());
  invalid = material;
  standard_material(invalid).texture_transform.offset[0] = -16.01f;
  EXPECT_FALSE(QmatCodec::encode(invalid).ok());
  invalid = material;
  standard_material(invalid).texture_transform.rotation_degrees = 360.01f;
  EXPECT_FALSE(QmatCodec::encode(invalid).ok());
  invalid = material;
  standard_material(invalid).texture_transform.center[0] =
      std::numeric_limits<float>::quiet_NaN();
  EXPECT_FALSE(QmatCodec::encode(invalid).ok());
  invalid = material;
  standard_material(invalid).normal_convention =
      static_cast<NormalMapConvention>(255u);
  EXPECT_FALSE(QmatCodec::encode(invalid).ok());
  invalid = material;
  standard_material(invalid).base_color_texture->color_space =
      ProjectTextureColorSpace::Linear;
  EXPECT_FALSE(QmatCodec::encode(invalid).ok());
}

TEST(QmatCodecTests, EnforcesTheFourMiBBoundaryForMemoryAndFiles) {
  const ProjectMaterialDocument material = material_definition(
      "971e9251-731a-41a1-a3aa-9a7197cce0ac", "Boundary");
  ProjectIoResult<std::string> encoded = QmatCodec::encode(material);
  ASSERT_TRUE(encoded.ok()) << encoded.error().message;
  ASSERT_LT(encoded.value().size(), kMaximumQmatBytes);
  std::string at_limit = encoded.value();
  at_limit.append(static_cast<std::size_t>(kMaximumQmatBytes) -
                      at_limit.size(),
                  ' ');
  ASSERT_EQ(at_limit.size(), kMaximumQmatBytes);
  EXPECT_TRUE(QmatCodec::decode(at_limit).ok());
  std::string over_limit = at_limit;
  over_limit.push_back(' ');
  EXPECT_FALSE(QmatCodec::decode(over_limit).ok());

  ScopedMaterialTestDirectory temporary("qmat-size");
  const std::filesystem::path root =
      temporary.make_empty_project("QmatSize");
  ProjectIoResult<ProjectWorkspaceStorage> workspace =
      ProjectManifestStore::create_in_empty_directory(root);
  ASSERT_TRUE(workspace.ok()) << workspace.error().message;
  ASSERT_TRUE(std::filesystem::create_directory(root / "Materials"));
  const ProjectRelativePath path = project_path("Materials/boundary.qmat");
  write_text(root / path.native_path(), at_limit);
  ProjectIoResult<QmatDecodedFile> at_limit_file =
      QmatCodec::read(workspace.value().file_system, path);
  ASSERT_TRUE(at_limit_file.ok()) << at_limit_file.error().message;
  EXPECT_EQ(at_limit_file.value().file_size, kMaximumQmatBytes);
  write_text(root / path.native_path(), over_limit);
  ProjectIoResult<QmatDecodedFile> over_limit_file =
      QmatCodec::read(workspace.value().file_system, path);
  ASSERT_FALSE(over_limit_file.ok());
  EXPECT_EQ(over_limit_file.error().code,
            ProjectIoErrorCode::MaterialMalformed);
}

TEST(QmatCodecTests, AtomicWriteFailurePreservesThePreviousFile) {
#ifndef _WIN32
  GTEST_SKIP() << "The deterministic replacement lock uses Windows sharing.";
#else
  ScopedMaterialTestDirectory temporary("qmat-atomic-failure");
  const std::filesystem::path root =
      temporary.make_empty_project("QmatAtomicFailure");
  ProjectIoResult<ProjectWorkspaceStorage> workspace =
      ProjectManifestStore::create_in_empty_directory(root);
  ASSERT_TRUE(workspace.ok()) << workspace.error().message;
  ASSERT_TRUE(std::filesystem::create_directory(root / "Materials"));
  const ProjectRelativePath path = project_path("Materials/locked.qmat");
  const ProjectMaterialDocument original = material_definition(
      "a090129e-d254-44b7-bdeb-70147316be72", "Original");
  ASSERT_TRUE(QmatCodec::write_atomic(workspace.value().file_system, path,
                                     original)
                  .ok());

  const std::filesystem::path absolute = root / path.native_path();
  const HANDLE locked = ::CreateFileW(
      absolute.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL, nullptr);
  ASSERT_NE(locked, INVALID_HANDLE_VALUE);
  ProjectMaterialDocument replacement = original;
  replacement.name = "Replacement";
  const ProjectIoResult<void> rejected = QmatCodec::write_atomic(
      workspace.value().file_system, path, replacement);
  ASSERT_TRUE(::CloseHandle(locked));
  EXPECT_FALSE(rejected.ok());

  ProjectIoResult<QmatDecodedFile> restored =
      QmatCodec::read(workspace.value().file_system, path);
  ASSERT_TRUE(restored.ok()) << restored.error().message;
  EXPECT_EQ(restored.value().material, original);
  for (const std::filesystem::directory_entry &entry :
       std::filesystem::directory_iterator(root / "Materials")) {
    EXPECT_FALSE(entry.path().filename().string().ends_with(".tmp"));
  }
#endif
}

TEST(ProjectMaterialCatalogTests,
     ScansRecursivelySortsAndReturnsEncodedAlbedoPreview) {
  ScopedMaterialTestDirectory temporary("catalog");
  const std::filesystem::path root =
      temporary.make_empty_project("Catalog");
  ProjectIoResult<ProjectWorkspaceStorage> workspace =
      ProjectManifestStore::create_in_empty_directory(root);
  ASSERT_TRUE(workspace.ok()) << workspace.error().message;
  ASSERT_TRUE(std::filesystem::create_directories(root / "Materials" /
                                                   "Nested"));
  ASSERT_TRUE(std::filesystem::create_directory(root / "Textures"));
  const std::string preview_bytes = "encoded-preview";
  write_text(root / "Textures" / "albedo.png", preview_bytes);
  write_text(root / "Textures" / "roughness.png", "roughness-data");
  write_text(root / "Textures" / "metallic.png", "metallic-data");
  write_text(root / "Textures" / "normal.png", "normal-data");

  ProjectMaterialDocument zebra = material_definition(
      "7aa250c0-56a3-46e7-aaf9-44ce0d121c24", "Zebra",
      "91700eab-45cd-4343-87d6-bdf1de7b4ed9");
  standard_material(zebra).roughness_texture = ProjectMaterialTextureSlot{
      .id = ProjectAssetId::generate(),
      .project_relative_path = "Textures/roughness.png",
      .color_space = ProjectTextureColorSpace::Linear,
  };
  standard_material(zebra).metallic_texture = ProjectMaterialTextureSlot{
      .id = ProjectAssetId::generate(),
      .project_relative_path = "Textures/metallic.png",
      .color_space = ProjectTextureColorSpace::Linear,
  };
  standard_material(zebra).normal_texture = ProjectMaterialTextureSlot{
      .id = ProjectAssetId::generate(),
      .project_relative_path = "Textures/normal.png",
      .color_space = ProjectTextureColorSpace::Linear,
  };
  const ProjectMaterialDocument amber = material_definition(
      "614fac5b-b29f-4c49-89f4-0734956f3819", "Amber");
  ASSERT_TRUE(QmatCodec::write_atomic(
                  workspace.value().file_system,
                  project_path("Materials/Zebra.qmat"), zebra)
                  .ok());
  ASSERT_TRUE(QmatCodec::write_atomic(
                  workspace.value().file_system,
                  project_path("Materials/Nested/Amber.qmat"), amber)
                  .ok());
  write_text(root / "Materials" / "broken.qmat", "{broken");

  ProjectIoResult<ProjectMaterialCatalog> catalog =
      ProjectMaterialCatalog::scan(workspace.value().file_system);
  ASSERT_TRUE(catalog.ok()) << catalog.error().message;
  ASSERT_EQ(catalog.value().entries().size(), 2u);
  EXPECT_EQ(catalog.value().entries()[0].material.name, "Amber");
  EXPECT_EQ(catalog.value().entries()[1].material.name, "Zebra");
  EXPECT_EQ(catalog.value().entries()[1].file_size,
            std::filesystem::file_size(root / "Materials" / "Zebra.qmat"));
  ASSERT_EQ(catalog.value().warnings().size(), 1u);

  ProjectIoResult<ProjectMaterialPreview> preview =
      catalog.value().read_preview(workspace.value().file_system, zebra.id);
  ASSERT_TRUE(preview.ok()) << preview.error().message;
  const std::string restored(
      reinterpret_cast<const char *>(preview.value().encoded_bytes.data()),
      preview.value().encoded_bytes.size());
  EXPECT_EQ(restored, preview_bytes);
  EXPECT_NE(preview.value().content_revision, 0u);
  for (const StandardMaterialTextureSlot slot : {
           StandardMaterialTextureSlot::Albedo,
           StandardMaterialTextureSlot::Roughness,
           StandardMaterialTextureSlot::Metallic,
           StandardMaterialTextureSlot::Normal,
       }) {
    EXPECT_TRUE(catalog.value()
                    .read_texture(workspace.value().file_system, zebra.id,
                                  slot)
                    .ok());
  }
}

TEST(ProjectMaterialCatalogTests,
     RejectsDuplicateIdsCaseCollisionsAndPhysicalAliases) {
  ScopedMaterialTestDirectory temporary("catalog-collisions");

  const std::filesystem::path duplicate_root =
      temporary.make_empty_project("DuplicateIds");
  ProjectIoResult<ProjectWorkspaceStorage> duplicate_workspace =
      ProjectManifestStore::create_in_empty_directory(duplicate_root);
  ASSERT_TRUE(duplicate_workspace.ok())
      << duplicate_workspace.error().message;
  ASSERT_TRUE(
      std::filesystem::create_directory(duplicate_root / "Materials"));
  const ProjectMaterialDocument first = material_definition(
      "386dbcd5-16b8-45a0-8191-1298783924a6", "First");
  ProjectMaterialDocument second = first;
  second.name = "Second";
  ASSERT_TRUE(QmatCodec::write_atomic(
                  duplicate_workspace.value().file_system,
                  project_path("Materials/first.qmat"), first)
                  .ok());
  ASSERT_TRUE(QmatCodec::write_atomic(
                  duplicate_workspace.value().file_system,
                  project_path("Materials/second.qmat"), second)
                  .ok());
  ProjectIoResult<ProjectMaterialCatalog> duplicate_catalog =
      ProjectMaterialCatalog::scan(
          duplicate_workspace.value().file_system);
  ASSERT_FALSE(duplicate_catalog.ok());
  EXPECT_EQ(duplicate_catalog.error().code,
            ProjectIoErrorCode::DuplicateAsset);

  const std::filesystem::path case_root =
      temporary.make_empty_project("CaseCollision");
  ProjectIoResult<ProjectWorkspaceStorage> case_workspace =
      ProjectManifestStore::create_in_empty_directory(case_root);
  ASSERT_TRUE(case_workspace.ok()) << case_workspace.error().message;
  ASSERT_TRUE(std::filesystem::create_directory(case_root / "materials"));
  ProjectIoResult<ProjectMaterialCatalog> case_catalog =
      ProjectMaterialCatalog::scan(case_workspace.value().file_system);
  ASSERT_FALSE(case_catalog.ok());
  EXPECT_EQ(case_catalog.error().code,
            ProjectIoErrorCode::PathCaseCollision);

  const std::filesystem::path alias_root =
      temporary.make_empty_project("PhysicalAlias");
  ProjectIoResult<ProjectWorkspaceStorage> alias_workspace =
      ProjectManifestStore::create_in_empty_directory(alias_root);
  ASSERT_TRUE(alias_workspace.ok()) << alias_workspace.error().message;
  ASSERT_TRUE(std::filesystem::create_directory(alias_root / "Materials"));
  const ProjectRelativePath source_path =
      project_path("Materials/source.qmat");
  ASSERT_TRUE(QmatCodec::write_atomic(alias_workspace.value().file_system,
                                     source_path, first)
                  .ok());
  std::error_code alias_error;
  std::filesystem::create_hard_link(
      alias_root / source_path.native_path(),
      alias_root / "Materials" / "alias.qmat", alias_error);
  ASSERT_FALSE(alias_error) << alias_error.message();
  ProjectIoResult<ProjectMaterialCatalog> alias_catalog =
      ProjectMaterialCatalog::scan(alias_workspace.value().file_system);
  ASSERT_FALSE(alias_catalog.ok());
  EXPECT_EQ(alias_catalog.error().code,
            ProjectIoErrorCode::PathCaseCollision);
}

TEST(ProjectMaterialCatalogTests,
     MissingAlbedoWarnsButKeepsTheMaterialForSwatchFallback) {
  ScopedMaterialTestDirectory temporary("catalog-fallback");
  const std::filesystem::path root =
      temporary.make_empty_project("CatalogFallback");
  ProjectIoResult<ProjectWorkspaceStorage> workspace =
      ProjectManifestStore::create_in_empty_directory(root);
  ASSERT_TRUE(workspace.ok()) << workspace.error().message;
  ASSERT_TRUE(std::filesystem::create_directory(root / "Materials"));
  const ProjectMaterialDocument material = material_definition(
      "7bd0a9bb-c9ed-4056-92c7-788fd1ef841e", "Missing Preview",
      "e70c0a21-3bb0-46d2-bb64-97502554419e");
  ASSERT_TRUE(QmatCodec::write_atomic(
                  workspace.value().file_system,
                  project_path("Materials/missing_preview.qmat"), material)
                  .ok());

  ProjectIoResult<ProjectMaterialCatalog> catalog =
      ProjectMaterialCatalog::scan(workspace.value().file_system);
  ASSERT_TRUE(catalog.ok()) << catalog.error().message;
  ASSERT_EQ(catalog.value().entries().size(), 1u);
  EXPECT_EQ(standard_material(
                catalog.value().entries().front().material)
                .base_color,
            standard_material(material).base_color);
  ASSERT_EQ(catalog.value().warnings().size(), 1u);
  EXPECT_FALSE(catalog.value()
                   .read_preview(workspace.value().file_system, material.id)
                   .ok());
}

TEST(ProjectMaterialCatalogTests,
     EqualNamesUseTheProjectRelativePathAsDeterministicTieBreaker) {
  ScopedMaterialTestDirectory temporary("catalog-order");
  const std::filesystem::path root =
      temporary.make_empty_project("CatalogOrder");
  ProjectIoResult<ProjectWorkspaceStorage> workspace =
      ProjectManifestStore::create_in_empty_directory(root);
  ASSERT_TRUE(workspace.ok()) << workspace.error().message;
  ASSERT_TRUE(std::filesystem::create_directories(root / "Materials" /
                                                   "Nested"));
  const ProjectMaterialDocument root_material = material_definition(
      "6decb960-5ad5-4da4-8ce3-a91f3e5e8de5", "Shared Name");
  const ProjectMaterialDocument nested_material = material_definition(
      "ac8a2d57-2520-4c85-b185-51920d6f35a5", "Shared Name");
  ASSERT_TRUE(QmatCodec::write_atomic(
                  workspace.value().file_system,
                  project_path("Materials/shared.qmat"), root_material)
                  .ok());
  ASSERT_TRUE(QmatCodec::write_atomic(
                  workspace.value().file_system,
                  project_path("Materials/Nested/shared.qmat"),
                  nested_material)
                  .ok());

  ProjectIoResult<ProjectMaterialCatalog> catalog =
      ProjectMaterialCatalog::scan(workspace.value().file_system);
  ASSERT_TRUE(catalog.ok()) << catalog.error().message;
  ASSERT_EQ(catalog.value().entries().size(), 2u);
  EXPECT_EQ(catalog.value().entries()[0].path.portable_string(),
            "Materials/Nested/shared.qmat");
  EXPECT_EQ(catalog.value().entries()[1].path.portable_string(),
            "Materials/shared.qmat");
}

TEST(ProjectMaterialWorkspaceTests,
     CreatesDefaultFilesAndPersistsActiveMaterialInManifestV2) {
  ScopedMaterialTestDirectory temporary("create");
  const std::filesystem::path root =
      temporary.make_empty_project("MaterialProject");
  ProjectSession project;
  project.set_resource_root(source_resource_root());
  const ProjectSessionResult created = project.create(root);
  ASSERT_TRUE(created.ok) << created.message;
  EXPECT_TRUE(
      std::filesystem::is_regular_file(root / "Materials" /
                                      "default_material.qmat"));
  EXPECT_TRUE(std::filesystem::is_regular_file(
      root / "Textures" / "default_albedo.png"));
  EXPECT_TRUE(std::filesystem::is_directory(root / "Scenes"));
  EXPECT_TRUE(std::filesystem::is_regular_file(
      root / "MaterialProject.qproj"));
  EXPECT_EQ(std::distance(
                std::filesystem::directory_iterator(root / "Materials"),
                std::filesystem::directory_iterator{}),
            1);
  EXPECT_EQ(std::distance(
                std::filesystem::directory_iterator(root / "Textures"),
                std::filesystem::directory_iterator{}),
            1);
  ASSERT_EQ(project.material_catalog().entries().size(), 1u);
  ASSERT_TRUE(project.active_material_id().has_value());
  EXPECT_EQ(project.active_material_id()->str(),
            kDefaultProjectMaterialId);
  ASSERT_NE(project.manifest(), nullptr);
  ASSERT_TRUE(project.manifest()->active_material_id().has_value());
  EXPECT_EQ(project.manifest()->active_material_id(),
            project.active_material_id());

  const std::string manifest_text = read_text(project.manifest_path());
  EXPECT_NE(manifest_text.find("\"version\": 2"), std::string::npos);
  EXPECT_NE(manifest_text.find("\"activeMaterial\""),
            std::string::npos);

  const std::string packaged_albedo = read_text(
      source_resource_root() /
      std::filesystem::path(kDefaultProjectAlbedoTemplatePath));
  EXPECT_EQ(read_text(root / "Textures" / "default_albedo.png"),
            packaged_albedo);
  ProjectIoResult<ProjectFileSystem> file_system =
      ProjectFileSystem::open(root);
  ASSERT_TRUE(file_system.ok()) << file_system.error().message;
  ProjectIoResult<QmatDecodedFile> default_qmat = QmatCodec::read(
      file_system.value(), project_path(kDefaultProjectMaterialPath));
  ASSERT_TRUE(default_qmat.ok()) << default_qmat.error().message;
  EXPECT_EQ(default_qmat.value().material.id,
            ProjectAssetId::parse(kDefaultProjectMaterialId));
  ASSERT_EQ(default_qmat.value().material.kind,
            ProjectMaterialKind::Standard);
  ASSERT_TRUE(standard_material(default_qmat.value().material)
                  .base_color_texture.has_value());
  EXPECT_EQ(standard_material(default_qmat.value().material)
                .base_color_texture->id,
            ProjectAssetId::parse(kDefaultProjectAlbedoTextureId));
  EXPECT_FALSE(standard_material(default_qmat.value().material)
                   .roughness_texture.has_value());
  EXPECT_EQ(standard_material(default_qmat.value().material)
                .texture_transform,
            StandardMaterialTextureTransform{});
}

TEST(ProjectMaterialWorkspaceTests,
     OpensV1ManifestBackfillsAssetsAndMigratesToV2) {
  ScopedMaterialTestDirectory temporary("migration");
  const std::filesystem::path root =
      temporary.make_empty_project("Legacy");
  ProjectIoResult<ProjectWorkspaceStorage> created =
      ProjectManifestStore::create_in_empty_directory(root);
  ASSERT_TRUE(created.ok()) << created.error().message;
  const std::string legacy_manifest =
      "{\n"
      "  \"format\": \"quader.project\",\n"
      "  \"version\": 1,\n"
      "  \"project\": {\n"
      "    \"id\": \"" +
      std::string(created.value().manifest.id().str()) +
      "\",\n"
      "    \"name\": \"Legacy\"\n"
      "  },\n"
      "  \"scenes\": [],\n"
      "  \"workspace\": {\n"
      "    \"openScenes\": [],\n"
      "    \"activeScene\": null\n"
      "  }\n"
      "}";
  write_text(created.value().manifest_path, legacy_manifest);

  ProjectSession project;
  project.set_resource_root(source_resource_root());
  const ProjectSessionResult opened =
      project.open(created.value().manifest_path);
  ASSERT_TRUE(opened.ok) << opened.message;
  ASSERT_TRUE(project.active_material_id().has_value());
  EXPECT_EQ(project.active_material_id()->str(),
            kDefaultProjectMaterialId);
  EXPECT_TRUE(std::filesystem::is_regular_file(
      root / "Materials" / "default_material.qmat"));
  EXPECT_TRUE(std::filesystem::is_regular_file(
      root / "Textures" / "default_albedo.png"));
  EXPECT_NE(read_text(created.value().manifest_path)
                .find("\"version\": 2"),
            std::string::npos);
}

TEST(ProjectMaterialWorkspaceTests,
     ActiveMaterialChangeIsManifestTransactionalAndRestorable) {
  ScopedMaterialTestDirectory temporary("active");
  const std::filesystem::path root =
      temporary.make_empty_project("ActiveMaterial");
  ProjectSession project;
  project.set_resource_root(source_resource_root());
  ASSERT_TRUE(project.create(root).ok);
  ASSERT_TRUE(project.close().ok);

  ProjectIoResult<ProjectWorkspaceStorage> workspace =
      ProjectManifestStore::open(root / "ActiveMaterial.qproj");
  ASSERT_TRUE(workspace.ok()) << workspace.error().message;
  const ProjectMaterialDocument alternate = material_definition(
      "80e21b14-60fd-4855-8b4f-1877ce16b0f0", "Alternate");
  ASSERT_TRUE(QmatCodec::write_atomic(
                  workspace.value().file_system,
                  project_path("Materials/alternate.qmat"), alternate)
                  .ok());
  ASSERT_TRUE(project.open(workspace.value().manifest_path).ok);
  ASSERT_EQ(project.active_material_id(),
            ProjectAssetId::parse(kDefaultProjectMaterialId));
  EXPECT_FALSE(project.has_dirty_scenes());

  const std::filesystem::path manifest = project.manifest_path();
  std::filesystem::path backup = manifest;
  backup += ".backup";
  std::error_code error;
  std::filesystem::rename(manifest, backup, error);
  ASSERT_FALSE(error) << error.message();
  ASSERT_TRUE(std::filesystem::create_directory(manifest, error));
  ASSERT_FALSE(error) << error.message();
  const ProjectSessionResult rejected =
      project.set_active_material(alternate.id);
  EXPECT_FALSE(rejected.ok);
  EXPECT_EQ(project.active_material_id(),
            ProjectAssetId::parse(kDefaultProjectMaterialId));
  std::filesystem::remove(manifest, error);
  ASSERT_FALSE(error) << error.message();
  std::filesystem::rename(backup, manifest, error);
  ASSERT_FALSE(error) << error.message();

  const ProjectSessionResult selected =
      project.set_active_material(alternate.id);
  ASSERT_TRUE(selected.ok) << selected.message;
  EXPECT_TRUE(selected.changed);
  EXPECT_EQ(project.active_material_id(), alternate.id);
  EXPECT_FALSE(project.has_dirty_scenes());

  ProjectSession reopened;
  reopened.set_resource_root(source_resource_root());
  ASSERT_TRUE(reopened.open(manifest).ok);
  EXPECT_EQ(reopened.active_material_id(), alternate.id);
}

TEST(ProjectMaterialWorkspaceTests,
     BackfillNeverOverwritesReservedConflictsAndRollsBackNewFiles) {
  ScopedMaterialTestDirectory temporary("collision");
  const std::filesystem::path root =
      temporary.make_empty_project("Collision");
  ProjectIoResult<ProjectWorkspaceStorage> workspace =
      ProjectManifestStore::create_in_empty_directory(root);
  ASSERT_TRUE(workspace.ok()) << workspace.error().message;
  ASSERT_TRUE(std::filesystem::create_directory(root / "Materials"));
  const std::string user_data = "user-owned";
  write_text(root / "Materials" / "default_material.qmat", user_data);

  ProjectSession project;
  project.set_resource_root(source_resource_root());
  const ProjectSessionResult opened =
      project.open(workspace.value().manifest_path);
  EXPECT_FALSE(opened.ok);
  EXPECT_FALSE(project.has_project());
  EXPECT_EQ(read_text(root / "Materials" / "default_material.qmat"),
            user_data);
  EXPECT_FALSE(std::filesystem::exists(root / "Textures"));
}

TEST(ProjectMaterialWorkspaceTests,
     CorruptDefaultFailureDoesNotReplaceTheOpenWorkspace) {
  ScopedMaterialTestDirectory temporary("workspace-preservation");
  const std::filesystem::path current_root =
      temporary.make_empty_project("CurrentWorkspace");
  ProjectSession project;
  project.set_resource_root(source_resource_root());
  ASSERT_TRUE(project.create(current_root).ok);
  const std::filesystem::path current_manifest = project.manifest_path();
  const std::optional<SceneSessionId> current_scene =
      project.active_scene_id();
  ASSERT_TRUE(current_scene.has_value());

  const std::filesystem::path corrupt_root =
      temporary.make_empty_project("CorruptWorkspace");
  ProjectIoResult<ProjectWorkspaceStorage> corrupt =
      ProjectManifestStore::create_in_empty_directory(corrupt_root);
  ASSERT_TRUE(corrupt.ok()) << corrupt.error().message;
  ASSERT_TRUE(std::filesystem::create_directory(corrupt_root / "Materials"));
  const std::string user_bytes = "not-a-qmat";
  write_text(corrupt_root / "Materials" / "default_material.qmat",
             user_bytes);

  const ProjectSessionResult rejected =
      project.open(corrupt.value().manifest_path);
  EXPECT_FALSE(rejected.ok);
  EXPECT_TRUE(project.has_project());
  EXPECT_EQ(project.manifest_path(), current_manifest);
  EXPECT_EQ(project.active_scene_id(), current_scene);
  EXPECT_EQ(read_text(corrupt_root / "Materials" /
                      "default_material.qmat"),
            user_bytes);
  EXPECT_FALSE(std::filesystem::exists(corrupt_root / "Textures"));
}

TEST(ProjectMaterialWorkspaceTests,
     FailedCreateValidationPreservesTheCurrentScenePreview) {
  ScopedMaterialTestDirectory temporary("failed-create-preview");
  const std::filesystem::path current_root =
      temporary.make_empty_project("CurrentWorkspace");
  ProjectSession project;
  project.set_resource_root(source_resource_root());
  ASSERT_TRUE(project.create(current_root).ok);
  ASSERT_NE(project.active_scene(), nullptr);
  ASSERT_NE(project.active_scene()->session, nullptr);

  ModelingSessionService &modeling =
      project.active_scene()->session->modeling();
  const ModelingBoxToolEvent start{
      .screen_position = {300.0f, 240.0f},
      .ray = {.origin = {0.0f, 4.0f, 0.0f},
              .direction = {0.0f, -1.0f, 0.0f}},
      .grid_size = 1.0,
  };
  ASSERT_TRUE(modeling.activate_box_tool().ok);
  ASSERT_TRUE(modeling.begin_box_footprint(start).ok);
  const ModelingBoxPreview preview_before = modeling.box_preview();
  ASSERT_TRUE(preview_before.valid);

  const std::filesystem::path rejected_root =
      temporary.make_empty_project("NotEmpty");
  write_text(rejected_root / "user-file.txt", "keep");
  const ProjectSessionResult rejected = project.create(rejected_root);

  EXPECT_FALSE(rejected.ok);
  EXPECT_EQ(project.root(), current_root);
  const ModelingBoxPreview preview_after = modeling.box_preview();
  EXPECT_TRUE(preview_after.valid);
  EXPECT_EQ(preview_after.drag_state, preview_before.drag_state);
  EXPECT_EQ(preview_after.corner_count, preview_before.corner_count);
  EXPECT_DOUBLE_EQ(preview_after.distance, preview_before.distance);
}

} // namespace

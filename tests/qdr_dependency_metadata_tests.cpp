////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/qdr/qdr_document_reader.hpp"
#include "io/qdr/qdr_scene_binary_codec.hpp"

#include "qdr_scene_generated.h"

#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <quader/modeling/neutral/polygon/document.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace quader {
namespace {

namespace fb = ::quader::qdr::fb;

[[nodiscard]] ProjectAssetId dependency_material_id(unsigned index) {
  const std::array<std::string_view, 4> ids{
      "aaaaaaaa-0000-4000-8000-000000000001",
      "aaaaaaaa-0000-4000-8000-000000000002",
      "aaaaaaaa-0000-4000-8000-000000000003",
      "aaaaaaaa-0000-4000-8000-000000000004"};
  return ProjectAssetId::parse(ids.at(index)).value();
}

[[nodiscard]] QdrProjectWriteSnapshot dependency_snapshot() {
  QdrProjectWriteSnapshot snapshot;
  snapshot.project_id = 81u;
  snapshot.project_name = "Dependency metadata";
  QdrProjectEntityWriteSnapshot entity;
  entity.id = 11u;
  entity.editor_object_id = 21u;
  entity.name = "Hidden locked material owner";
  entity.visibility.visible = false;
  entity.selectable.selectable = false;
  entity.material = MaterialComponent{
      .material = MaterialReference::project(dependency_material_id(0u))};
  entity.material->material_slots.emplace(
      999u, MaterialReference::project(dependency_material_id(1u)));
  snapshot.entities.push_back(entity);
  snapshot.root_entities = {entity.id};
  return snapshot;
}

[[nodiscard]] std::vector<std::byte> wrap_metadata(
    flatbuffers::FlatBufferBuilder &builder,
    flatbuffers::Offset<fb::ScenePayload> payload,
    std::uint16_t schema_version = 14u) {
  fb::FinishScenePayloadBuffer(builder, payload);
  const auto *begin =
      reinterpret_cast<const std::byte *>(builder.GetBufferPointer());
  return quader_io::build_qdr_binary_container(
      dependency_material_id(3u).bytes(),
      {{.type = quader_io::QdrBinaryChunkType::SceneMetadata,
        .schema_version = schema_version,
        .bytes = std::vector<std::byte>(begin, begin + builder.GetSize())}});
}

TEST(QdrDependencyMetadataTests, FindsPrimaryAndDormantBindingsWithoutTable) {
  const auto bytes = encode_qdr_scene_binary(dependency_snapshot()).bytes;
  const auto result =
      QdrDocumentReader::deserialize_project_dependencies(bytes);
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.scene_name, "Dependency metadata");
  EXPECT_TRUE(result.project_asset_dependencies.empty());
  EXPECT_EQ(result.material_ids,
            (std::vector<ProjectAssetId>{dependency_material_id(0u),
                                         dependency_material_id(1u)}));
  EXPECT_EQ(result.source_byte_count, bytes.size());
  EXPECT_EQ(result.source_content_hash,
            quader_io::qdr_binary_content_hash(bytes));
}

TEST(QdrDependencyMetadataTests, UnionsTableWithInstanceDefinitionBindings) {
  auto snapshot = dependency_snapshot();
  snapshot.project_asset_dependencies.push_back(
      {.stable_id = dependency_material_id(2u),
       .kind = ProjectAssetKind::Material,
       .path = ProjectRelativePath::parse_portable("Materials/Table.qmat")
                   .value()});
  auto child = snapshot.entities.front();
  child.id = 12u;
  child.editor_object_id = 22u;
  child.parent = 11u;
  snapshot.entities.front().instance_definition = InstanceDefinitionComponent{};
  snapshot.entities.push_back(child);
  const auto result = QdrDocumentReader::deserialize_project_dependencies(
      encode_qdr_scene_binary(snapshot).bytes);
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.material_ids,
            (std::vector<ProjectAssetId>{dependency_material_id(0u),
                                         dependency_material_id(1u),
                                         dependency_material_id(2u)}));
  ASSERT_EQ(result.project_asset_dependencies.size(), 1u);
  EXPECT_EQ(result.project_asset_dependencies.front().path.portable_string(),
            "Materials/Table.qmat");
}

TEST(QdrDependencyMetadataTests, DoesNotDecodeOrCompileAuthoredGeometry) {
  auto snapshot = dependency_snapshot();
  auto authored = std::make_shared<quader_poly::Document>();
  authored->next_vertex_id = 4u;
  authored->next_face_id = 2u;
  authored->vertices = {{.id = 1u, .position = {0.0f, 0.0f, 0.0f}},
                        {.id = 2u, .position = {1.0f, 0.0f, 0.0f}},
                        {.id = 3u, .position = {0.0f, 1.0f, 0.0f}}};
  quader_poly::Face face;
  face.id = 1u;
  face.vertices = {1u, 2u, 3u};
  face.uvs = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}};
  authored->faces.push_back(face);
  snapshot.meshes.push_back(
      {.id = 41u,
       .name = "Authored triangle",
       .has_authored_polygon_document = true,
       .geometry = std::make_shared<MeshGeometryPayload>(),
       .authored_polygon_document = authored});
  snapshot.entities.front().kind = EntityKind::Mesh;
  snapshot.entities.front().mesh = MeshComponent{.mesh = 41u};
  const auto result = QdrDocumentReader::deserialize_project_dependencies(
      encode_qdr_scene_binary(snapshot).bytes);
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stats.logical_geometry_count, 1u);
  EXPECT_EQ(result.stats.unique_geometry_decoded_count, 0u);
  EXPECT_EQ(result.stats.unique_geometry_compiled_count, 0u);
  EXPECT_EQ(result.stats.geometry_decode_ms, 0.0);
  EXPECT_EQ(result.stats.geometry_compile_ms, 0.0);
  EXPECT_EQ(result.material_ids.size(), 2u);
}

TEST(QdrDependencyMetadataTests, IncludesLegacyObjectProjectReference) {
  flatbuffers::FlatBufferBuilder builder;
  const auto entity = fb::CreateEntity(builder, 11u, {}, 21u);
  const auto object = fb::CreateObjectRecord(
      builder, 21u, 11u, 0u, 0u, -1, {}, true, 0u,
      builder.CreateString(dependency_material_id(0u).str()));
  const std::array entities{entity};
  const std::array objects{object};
  const auto payload = fb::CreateScenePayload(
      builder, 81u, {}, {}, {}, {},
      builder.CreateVector(entities.data(), entities.size()),
      builder.CreateVector(objects.data(), objects.size()));
  const auto result = QdrDocumentReader::deserialize_project_dependencies(
      wrap_metadata(builder, payload, 3u));
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.material_ids,
            std::vector<ProjectAssetId>{dependency_material_id(0u)});
}

TEST(QdrDependencyMetadataTests, RejectsAmbiguousAndMalformedProjectReferences) {
  for (const bool ambiguous : {false, true}) {
    SCOPED_TRACE(ambiguous);
    flatbuffers::FlatBufferBuilder builder;
    const auto material = fb::CreateMaterialComponent(
        builder, ambiguous ? 17u : 0u, {},
        builder.CreateString(ambiguous ? dependency_material_id(0u).str()
                                       : std::string_view("invalid UUID")));
    const auto entity = fb::CreateEntity(builder, 11u, {}, 21u, {},
                                         fb::EntityKind::Generic, 0u, {}, {},
                                         {}, {}, {}, material);
    const std::array entities{entity};
    const auto payload = fb::CreateScenePayload(
        builder, 81u, {}, {}, {}, {},
        builder.CreateVector(entities.data(), entities.size()));
    const auto result = QdrDocumentReader::deserialize_project_dependencies(
        wrap_metadata(builder, payload));
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.message.find("invalid or ambiguous"), std::string::npos);
  }
}

TEST(QdrDependencyMetadataTests, RejectsMissingEmbeddedMaterialInDormantSlot) {
  auto snapshot = dependency_snapshot();
  snapshot.entities.front().material->material_slots.emplace(500u, 789u);
  const auto result = QdrDocumentReader::deserialize_project_dependencies(
      encode_qdr_scene_binary(snapshot).bytes);
  EXPECT_FALSE(result.ok);
  EXPECT_NE(result.message.find("missing material asset"), std::string::npos);
}

TEST(QdrDependencyMetadataTests, RejectsDamagedContainerAndUnsupportedSchema) {
  const auto bytes = encode_qdr_scene_binary(dependency_snapshot()).bytes;
  auto damaged = bytes;
  damaged.front() ^= std::byte{0x01};
  EXPECT_FALSE(QdrDocumentReader::deserialize_project_dependencies(damaged).ok);

  const auto container = quader_io::read_qdr_binary_container(bytes);
  ASSERT_TRUE(container.ok) << container.message;
  ASSERT_EQ(container.chunks.size(), 1u);
  const auto metadata = container.chunks.front().bytes();
  auto corrupt_metadata = std::vector<std::byte>(metadata.begin(), metadata.end());
  corrupt_metadata.front() ^= std::byte{0x80};
  const auto invalid_flatbuffer = quader_io::build_qdr_binary_container(
      container.scene_uuid,
      {{.type = quader_io::QdrBinaryChunkType::SceneMetadata,
        .schema_version = 14u,
        .bytes = corrupt_metadata}});
  EXPECT_FALSE(
      QdrDocumentReader::deserialize_project_dependencies(invalid_flatbuffer).ok);
  const auto future_schema = quader_io::build_qdr_binary_container(
      container.scene_uuid,
      {{.type = quader_io::QdrBinaryChunkType::SceneMetadata,
        .schema_version = 65535u,
        .bytes = std::vector<std::byte>(metadata.begin(), metadata.end())}});
  EXPECT_FALSE(QdrDocumentReader::deserialize_project_dependencies(future_schema)
                   .ok);
}

TEST(QdrDependencyMetadataTests, ReadsExactDiskFingerprintAndReportsSourcePath) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const auto path = std::filesystem::path(temporary.path().toStdWString()) /
                    "metadata.qdr";
  const auto bytes = encode_qdr_scene_binary(dependency_snapshot()).bytes;
  {
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    ASSERT_TRUE(stream.good());
  }
  const auto read = QdrDocumentReader::read_project_dependencies(path);
  ASSERT_TRUE(read.ok) << read.message;
  EXPECT_EQ(read.source_byte_count, bytes.size());
  EXPECT_EQ(read.source_content_hash, quader_io::qdr_binary_content_hash(bytes));
  const auto absent = QdrDocumentReader::read_project_dependencies(
      path.parent_path() / "absent.qdr");
  ASSERT_FALSE(absent.ok);
  ASSERT_FALSE(absent.diagnostics.empty());
  EXPECT_EQ(absent.diagnostics.front().source_path,
            path.parent_path() / "absent.qdr");
}

TEST(QdrDependencyMetadataTests, RejectsMissingParentsAndHierarchyCycles) {
  for (const EntityId parent : {EntityId{11u}, EntityId{12u}, EntityId{999u}}) {
    SCOPED_TRACE(parent);
    auto snapshot = dependency_snapshot();
    auto child = snapshot.entities.front();
    child.id = 12u;
    child.editor_object_id = 22u;
    child.parent = 11u;
    snapshot.entities.front().parent = parent;
    snapshot.root_entities.clear();
    snapshot.entities.push_back(child);
    const auto bytes = encode_qdr_scene_binary(snapshot).bytes;
    const auto canonical =
        QdrDocumentReader::deserialize_project_document(bytes);
    ASSERT_FALSE(canonical.ok) << canonical.message;
    const auto metadata =
        QdrDocumentReader::deserialize_project_dependencies(bytes);
    EXPECT_FALSE(metadata.ok);
    EXPECT_FALSE(metadata.message.empty());
  }
}

TEST(QdrDependencyMetadataTests, PreservesParentlessObjectsAndMissingLegacyIdentitySupport) {
  auto snapshot = dependency_snapshot();
  snapshot.entities.front().editor_object_id = kInvalidEditorObjectId;
  const auto bytes = encode_qdr_scene_binary(snapshot).bytes;
  const auto canonical = QdrDocumentReader::deserialize_project_document(bytes);
  ASSERT_TRUE(canonical.ok) << canonical.message;
  EXPECT_EQ(canonical.document.scene().root_entities(), (std::vector<EntityId>{11u}));
  EXPECT_EQ(canonical.document.scene().entities().size(), 1u);
  EXPECT_TRUE(canonical.document.validate_object_identity_invariants().ok());
  const auto metadata =
      QdrDocumentReader::deserialize_project_dependencies(bytes);
  ASSERT_TRUE(metadata.ok) << metadata.message;
  EXPECT_EQ(metadata.material_ids.size(), 2u);
}

TEST(QdrDependencyMetadataTests, RejectsMalformedDuplicateAndParentedLegacyWrappers) {
  for (const unsigned scenario : {0u, 1u, 2u}) {
    SCOPED_TRACE(scenario);
    flatbuffers::FlatBufferBuilder builder;
    const auto environment = fb::CreateSceneEnvironmentSettings(builder);
    const auto camera = fb::CreateSceneCameraSettings(builder);
    const auto post_process = fb::CreateScenePostProcessSettings(builder);
    const auto settings = fb::CreateSceneSettingsComponent(builder, environment, camera, post_process);
    std::vector<flatbuffers::Offset<fb::Entity>> entities;
    entities.push_back(fb::CreateEntity(
        builder, 11u, {}, 21u, {}, fb::EntityKind::Empty,
        scenario == 2u ? 12u : 0u, {}, {}, {}, {}, {}, {}, {},
        true, scenario == 0u ? flatbuffers::Offset<fb::SceneSettingsComponent>{} : settings));
    if (scenario == 1u || scenario == 2u) {
      entities.push_back(fb::CreateEntity(
          builder, 12u, {}, 22u, {}, fb::EntityKind::Empty, 0u,
          {}, {}, {}, {}, {}, {}, {}, scenario == 1u,
          scenario == 1u ? settings : flatbuffers::Offset<fb::SceneSettingsComponent>{}));
    }
    const auto payload = fb::CreateScenePayload(
        builder, 81u, {}, {}, {}, {}, builder.CreateVector(entities));
    const auto bytes = wrap_metadata(builder, payload);
    const auto canonical = QdrDocumentReader::deserialize_project_document(bytes);
    ASSERT_FALSE(canonical.ok) << canonical.message;
    const auto metadata = QdrDocumentReader::deserialize_project_dependencies(bytes);
    EXPECT_FALSE(metadata.ok);
  }
}

TEST(QdrDependencyMetadataTests, MigratesLegacySettingsWrapperWithoutRedundantMarker) {
  flatbuffers::FlatBufferBuilder builder;
  const auto environment = fb::CreateSceneEnvironmentSettings(builder);
  const auto camera = fb::CreateSceneCameraSettings(builder);
  const auto post_process = fb::CreateScenePostProcessSettings(builder);
  const auto settings = fb::CreateSceneSettingsComponent(builder, environment, camera, post_process);
  const auto wrapper = fb::CreateEntity(
      builder, 11u, {}, 21u, {}, fb::EntityKind::Generic, 0u,
      {}, {}, {}, {}, {}, {}, {}, false, settings);
  const std::array entities{wrapper};
  const auto payload = fb::CreateScenePayload(builder, 81u, {}, {}, {}, {},
      builder.CreateVector(entities.data(), entities.size()));
  const auto bytes = wrap_metadata(builder, payload);
  const auto canonical = QdrDocumentReader::deserialize_project_document(bytes);
  ASSERT_TRUE(canonical.ok) << canonical.message;
  EXPECT_TRUE(canonical.document.scene().entities().empty());
  EXPECT_EQ(canonical.document.scene().scene_settings().post_process.tone_mapping,
            SceneToneMapping::Aces);
  const auto metadata = QdrDocumentReader::deserialize_project_dependencies(bytes);
  EXPECT_TRUE(metadata.ok) << metadata.message;
}

TEST(QdrDependencyMetadataTests, RejectsInvalidAndDuplicateEntityIdentities) {
  for (const unsigned scenario : {0u, 1u, 2u}) {
    SCOPED_TRACE(scenario);
    auto snapshot = dependency_snapshot();
    snapshot.entities.front().uuid =
        scenario == 0u ? "invalid UUID"
                       : std::string(dependency_material_id(0u).str());
    if (scenario != 0u) {
      auto other = snapshot.entities.front();
      other.id = 12u;
      if (scenario == 1u) {
        other.editor_object_id = 22u;
      } else {
        other.uuid = dependency_material_id(1u).str();
      }
      snapshot.entities.push_back(other);
      snapshot.root_entities.push_back(other.id);
    }
    const auto bytes = encode_qdr_scene_binary(snapshot).bytes;
    const auto canonical =
        QdrDocumentReader::deserialize_project_document(bytes);
    ASSERT_FALSE(canonical.ok) << canonical.message;
    const auto metadata =
        QdrDocumentReader::deserialize_project_dependencies(bytes);
    EXPECT_FALSE(metadata.ok);
  }
}

} // namespace
} // namespace quader

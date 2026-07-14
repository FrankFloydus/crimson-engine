////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/mesh_asset.hpp"
#include "io/qdr/qdr_binary_container.hpp"
#include "io/qdr/qdr_document_reader.hpp"
#include "io/qdr/qdr_document_writer.hpp"
#include "io/qdr/qdr_project_write_snapshot.hpp"
#include "io/qdr/qdr_scene_binary_codec.hpp"
#include "scene/document/scene_content_document.hpp"

#include <gtest/gtest.h>
#include <mesh/polygon/document.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace quader {
namespace {

[[nodiscard]] std::array<std::byte, 16> test_uuid() {
  std::array<std::byte, 16> uuid{};
  for (std::size_t index = 0; index < uuid.size(); ++index) {
    uuid[index] = static_cast<std::byte>(index + 1u);
  }
  return uuid;
}

[[nodiscard]] MeshAsset triangle_mesh(std::string name) {
  MeshAsset mesh;
  mesh.name = std::move(name);
  mesh.vertices = {
      {.position = {0.0f, 0.0f, 0.0f},
       .normal = {0.0f, 0.0f, 1.0f},
       .uv = {0.0f, 0.0f}},
      {.position = {1.0f, 0.0f, 0.0f},
       .normal = {0.0f, 0.0f, 1.0f},
       .uv = {1.0f, 0.0f}},
      {.position = {0.0f, 1.0f, 0.0f},
       .normal = {0.0f, 0.0f, 1.0f},
       .uv = {0.0f, 1.0f}},
  };
  mesh.indices = {0u, 1u, 2u};
  mesh.primitives = {{.index_offset = 0u,
                      .index_count = 3u,
                      .material_slot = 0u}};
  return mesh;
}

[[nodiscard]] MeshAsset authored_triangle_mesh(std::string name,
                                               float x_offset) {
  MeshAsset mesh = triangle_mesh(std::move(name));
  for (MeshVertex &vertex : mesh.vertices) {
    vertex.position.x += x_offset;
  }
  auto document = std::make_shared<quader_poly::Document>();
  document->next_vertex_id = 4u;
  document->next_face_id = 2u;
  document->vertices = {
      {.id = 1u, .position = {x_offset, 0.0f, 0.0f}},
      {.id = 2u, .position = {x_offset + 1.0f, 0.0f, 0.0f}},
      {.id = 3u, .position = {x_offset, 1.0f, 0.0f}},
  };
  quader_poly::Face face;
  face.id = 1u;
  face.vertices = {1u, 2u, 3u};
  face.uvs = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}};
  document->faces.push_back(std::move(face));
  mesh.has_authored_polygon_document = true;
  mesh.authored_polygon_document = std::move(document);
  return mesh;
}

[[nodiscard]] SceneContentDocument simple_scene() {
  SceneContentDocument document(SceneContentDocumentRecord{
      .id = 42u,
      .name = "Binary Scene",
  });
  (void)document.assets().add_mesh(10u, triangle_mesh("Triangle"));
  const EntityId entity = document.scene().create_entity_with_id(
      100u, "Triangle Entity", EntityKind::Mesh);
  (void)document.scene().set_mesh(entity, MeshComponent{.mesh = 10u});
  return document;
}

[[nodiscard]] std::vector<quader_io::QdrBinaryChunkInput>
copy_chunks(const quader_io::QdrBinaryContainerReadResult &container) {
  std::vector<quader_io::QdrBinaryChunkInput> chunks;
  chunks.reserve(container.chunks.size());
  for (const quader_io::QdrBinaryChunk &chunk : container.chunks) {
    chunks.push_back({
        .type = static_cast<quader_io::QdrBinaryChunkType>(chunk.type),
        .schema_version = chunk.schema_version,
        .flags = chunk.flags,
        .id = chunk.id,
        .content_hash = chunk.content_hash,
        .bytes = {chunk.bytes().begin(), chunk.bytes().end()},
        .allow_compression = false,
    });
  }
  return chunks;
}

[[nodiscard]] quader_io::QdrBinaryContainerReadResult
parse_written_scene(const SceneContentDocument &document) {
  std::vector<std::byte> bytes =
      QdrDocumentWriter::serialize_project_document_bytes(
          qdr_project_write_snapshot_from_document(document), test_uuid());
  return quader_io::read_qdr_binary_container(std::move(bytes));
}

TEST(QdrBinarySceneCodecTests, RoundTripPreservesExplicitUuidAndSceneData) {
  SceneContentDocument document = simple_scene();
  const EntityId root = document.scene().scene_root();
  ASSERT_NE(root, kInvalidEntityId);
  ASSERT_TRUE(document.scene().set_selected(root, true));

  const std::vector<std::byte> bytes =
      QdrDocumentWriter::serialize_project_document_bytes(
          qdr_project_write_snapshot_from_document(document), test_uuid());
  QdrProjectReadResult read =
      QdrDocumentReader::deserialize_project_document(bytes);

  ASSERT_TRUE(read.ok) << read.message;
  EXPECT_EQ(read.scene_uuid, test_uuid());
  EXPECT_EQ(read.stats.file_bytes, bytes.size());
  EXPECT_EQ(read.stats.logical_geometry_count, 1u);
  EXPECT_EQ(read.stats.unique_geometry_decoded_count, 1u);
  EXPECT_EQ(read.stats.unique_geometry_compiled_count, 0u);
  EXPECT_TRUE(read.project_asset_dependencies.empty());
  EXPECT_GE(read.stats.container_access_and_verification_ms, 0.0);
  EXPECT_GE(read.stats.hydration_ms, 0.0);
  EXPECT_EQ(read.document.id(), 42u);
  EXPECT_EQ(read.document.name(), "Binary Scene");
  EXPECT_NE(read.document.assets().mesh(10u), nullptr);
  EXPECT_TRUE(read.document.scene().selected_entities().empty())
      << "selection is transient and must not be stored in QDR";
}

TEST(QdrBinarySceneCodecTests,
     PreservesFutureProjectAssetDependencyReferences) {
  const ProjectIoResult<ProjectRelativePath> path =
      ProjectRelativePath::parse_portable("Materials/wood.qmat");
  ASSERT_TRUE(path.ok()) << path.error().message;
  QdrProjectWriteSnapshot snapshot =
      qdr_project_write_snapshot_from_document(simple_scene());
  snapshot.project_asset_dependencies.push_back({
      .stable_id = "85fdfec9-795e-4afe-bf74-13555ee79cb4",
      .kind = ProjectAssetKind::Material,
      .path = path.value(),
  });

  const std::vector<std::byte> bytes =
      QdrDocumentWriter::serialize_project_document_bytes(snapshot,
                                                          test_uuid());
  const QdrProjectReadResult read =
      QdrDocumentReader::deserialize_project_document(bytes);
  ASSERT_TRUE(read.ok) << read.message;
  ASSERT_EQ(read.project_asset_dependencies.size(), 1u);
  EXPECT_EQ(read.project_asset_dependencies.front().stable_id,
            "85fdfec9-795e-4afe-bf74-13555ee79cb4");
  EXPECT_EQ(read.project_asset_dependencies.front().kind,
            ProjectAssetKind::Material);
  EXPECT_EQ(read.project_asset_dependencies.front().path.portable_string(),
            "Materials/wood.qmat");
}

TEST(QdrBinarySceneCodecTests, NativeReaderRejectsLegacyJsonDirectly) {
  constexpr std::string_view kLegacy =
      R"({"format":"quader.qdr","version":4,"document":{}})";
  const QdrProjectReadResult read =
      QdrDocumentReader::deserialize_project_document(
          std::as_bytes(std::span(kLegacy.data(), kLegacy.size())));

  ASSERT_FALSE(read.ok);
  ASSERT_FALSE(read.diagnostics.empty());
  EXPECT_EQ(read.diagnostics.front().code,
            "qdr.binary.unsupported_format");
  EXPECT_NE(read.message.find("Legacy JSON"), std::string::npos);
}

TEST(QdrBinarySceneCodecTests, UnknownNoncriticalChunksAreSkipped) {
  SceneContentDocument document = simple_scene();
  quader_io::QdrBinaryContainerReadResult parsed =
      parse_written_scene(document);
  ASSERT_TRUE(parsed.ok) << parsed.message;
  std::vector<quader_io::QdrBinaryChunkInput> chunks = copy_chunks(parsed);
  const std::vector<std::byte> extension = {
      std::byte{0x10}, std::byte{0x20}, std::byte{0x30}};
  chunks.push_back({
      .type = static_cast<quader_io::QdrBinaryChunkType>(0x80000001u),
      .schema_version = 1u,
      .flags = quader_io::QdrBinaryChunkFlags::None,
      .id = 99u,
      .content_hash = quader_io::qdr_binary_content_hash(extension),
      .bytes = extension,
  });

  const std::vector<std::byte> bytes = quader_io::build_qdr_binary_container(
      test_uuid(), std::move(chunks));
  const QdrProjectReadResult read =
      QdrDocumentReader::deserialize_project_document(bytes);
  ASSERT_TRUE(read.ok) << read.message;
  EXPECT_EQ(read.document.name(), "Binary Scene");
}

TEST(QdrBinarySceneCodecTests, UnknownCriticalChunksRejectTheScene) {
  SceneContentDocument document = simple_scene();
  quader_io::QdrBinaryContainerReadResult parsed =
      parse_written_scene(document);
  ASSERT_TRUE(parsed.ok) << parsed.message;
  std::vector<quader_io::QdrBinaryChunkInput> chunks = copy_chunks(parsed);
  const std::vector<std::byte> extension = {std::byte{0x7f}};
  chunks.push_back({
      .type = static_cast<quader_io::QdrBinaryChunkType>(0x80000002u),
      .schema_version = 1u,
      .flags = quader_io::QdrBinaryChunkFlags::Critical,
      .id = 100u,
      .content_hash = quader_io::qdr_binary_content_hash(extension),
      .bytes = extension,
  });

  const std::vector<std::byte> bytes = quader_io::build_qdr_binary_container(
      test_uuid(), std::move(chunks));
  const QdrProjectReadResult read =
      QdrDocumentReader::deserialize_project_document(bytes);
  ASSERT_FALSE(read.ok);
  EXPECT_NE(read.message.find("unknown critical"), std::string::npos);
}

TEST(QdrBinarySceneCodecTests,
     LeavesUnreferencedCompressedGeometryUndecoded) {
  SceneContentDocument document = simple_scene();
  quader_io::QdrBinaryContainerReadResult parsed =
      parse_written_scene(document);
  ASSERT_TRUE(parsed.ok) << parsed.message;
  std::vector<quader_io::QdrBinaryChunkInput> chunks = copy_chunks(parsed);
  std::vector<std::byte> unreferenced(70u * 1024u, std::byte{0});
  chunks.push_back({
      .type = quader_io::QdrBinaryChunkType::Geometry,
      .schema_version = 1u,
      .flags = quader_io::QdrBinaryChunkFlags::Critical,
      .id = 999u,
      .content_hash = quader_io::qdr_binary_content_hash(unreferenced),
      .bytes = std::move(unreferenced),
      .allow_compression = true,
  });

  quader_io::QdrBinaryContainerReadResult extended =
      quader_io::read_qdr_binary_container(
          quader_io::build_qdr_binary_container(test_uuid(),
                                                std::move(chunks)));
  ASSERT_TRUE(extended.ok) << extended.message;
  const auto extra = std::ranges::find_if(
      extended.chunks, [](const quader_io::QdrBinaryChunk &chunk) {
        return chunk.type == static_cast<std::uint32_t>(
                                 quader_io::QdrBinaryChunkType::Geometry) &&
               chunk.id == 999u;
      });
  ASSERT_NE(extra, extended.chunks.end());
  ASSERT_EQ(extra->codec, quader_io::QdrBinaryChunkCodec::Zstd);
  EXPECT_FALSE(extra->was_decompressed());

  QdrSceneBinaryDecodeResult decoded =
      decode_qdr_scene_binary_with_stats(extended);
  EXPECT_EQ(decoded.document.name(), "Binary Scene");
  EXPECT_EQ(decoded.stats.unique_geometry_decoded_count, 1u);
  EXPECT_FALSE(extra->was_decompressed());
}

TEST(QdrBinarySceneCodecTests,
     KeepsLogicalUnreferencedMeshDeferredUntilFirstAccess) {
  SceneContentDocument document(
      SceneContentDocumentRecord{.id = 84u, .name = "Lazy Geometry"});
  ASSERT_EQ(document.assets().add_mesh(
                10u, authored_triangle_mesh("Referenced", 0.0f)),
            10u);
  ASSERT_EQ(document.assets().add_mesh(
                20u, authored_triangle_mesh("Deferred", 3.0f)),
            20u);
  const EntityId entity = document.scene().create_entity_with_id(
      100u, "Referenced Entity", EntityKind::Mesh);
  ASSERT_NE(entity, kInvalidEntityId);
  document.scene().set_mesh(entity, MeshComponent{.mesh = 10u});
  ASSERT_NE(document.scene().mesh(entity), nullptr);

  const QdrProjectReadResult read =
      QdrDocumentReader::deserialize_project_document(
          QdrDocumentWriter::serialize_project_document_bytes(
              qdr_project_write_snapshot_from_document(document),
              test_uuid()));
  ASSERT_TRUE(read.ok) << read.message;
  EXPECT_EQ(read.stats.logical_geometry_count, 2u);
  EXPECT_EQ(read.stats.unique_geometry_decoded_count, 1u);
  EXPECT_EQ(read.stats.unique_geometry_compiled_count, 1u);
  EXPECT_TRUE(read.document.assets().mesh_materialized(10u));
  EXPECT_FALSE(read.document.assets().mesh_materialized(20u));

  const std::optional<MeshAssetDescriptor> descriptor =
      read.document.assets().mesh_descriptor(20u);
  ASSERT_TRUE(descriptor.has_value());
  EXPECT_EQ(descriptor->name, "Deferred");
  EXPECT_TRUE(descriptor->has_authored_polygon_document);
  EXPECT_FALSE(descriptor->materialized);
  const std::uint64_t registry_revision = read.document.assets().revision();
  const MeshAssetRevision mesh_revision =
      read.document.assets().mesh_revision(20u);

  const MeshAsset *deferred = read.document.assets().mesh(20u);
  ASSERT_NE(deferred, nullptr);
  EXPECT_TRUE(deferred->has_authored_polygon_document);
  ASSERT_TRUE(deferred->authored_polygon_document);
  ASSERT_FALSE(deferred->authored_polygon_document->vertices.empty());
  EXPECT_FLOAT_EQ(
      deferred->authored_polygon_document->vertices.front().position.x,
      3.0f);
  EXPECT_TRUE(read.document.assets().mesh_materialized(20u));
  EXPECT_EQ(read.document.assets().revision(), registry_revision);
  EXPECT_EQ(read.document.assets().mesh_revision(20u), mesh_revision);
}

TEST(QdrBinarySceneCodecTests, RejectsUnsupportedMetadataSchema) {
  SceneContentDocument document = simple_scene();
  quader_io::QdrBinaryContainerReadResult parsed =
      parse_written_scene(document);
  ASSERT_TRUE(parsed.ok) << parsed.message;
  std::vector<quader_io::QdrBinaryChunkInput> chunks = copy_chunks(parsed);
  ASSERT_FALSE(chunks.empty());
  chunks.front().schema_version = 2u;

  const QdrProjectReadResult read =
      QdrDocumentReader::deserialize_project_document(
          quader_io::build_qdr_binary_container(test_uuid(),
                                                std::move(chunks)));
  ASSERT_FALSE(read.ok);
  EXPECT_NE(read.message.find("unsupported schema"), std::string::npos);
}

TEST(QdrBinarySceneCodecTests, RejectsMalformedMetadataFlatBuffer) {
  const std::vector<std::byte> malformed = {
      std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}};
  std::vector<quader_io::QdrBinaryChunkInput> chunks;
  chunks.push_back({
      .type = quader_io::QdrBinaryChunkType::SceneMetadata,
      .schema_version = 1u,
      .flags = quader_io::QdrBinaryChunkFlags::Critical,
      .id = 0u,
      .content_hash = quader_io::qdr_binary_content_hash(malformed),
      .bytes = malformed,
  });

  const QdrProjectReadResult read =
      QdrDocumentReader::deserialize_project_document(
          quader_io::build_qdr_binary_container(test_uuid(),
                                                std::move(chunks)));
  ASSERT_FALSE(read.ok);
  EXPECT_NE(read.message.find("FlatBuffer is invalid"), std::string::npos);
}

TEST(QdrBinarySceneCodecTests, DeduplicatesEquivalentGeometryPayloads) {
  SceneContentDocument document(SceneContentDocumentRecord{
      .id = 7u, .name = "Dedup"});
  (void)document.assets().add_mesh(10u, triangle_mesh("First"));
  (void)document.assets().add_mesh(11u, triangle_mesh("Second"));
  const EntityId first = document.scene().create_entity_with_id(
      100u, "First Entity", EntityKind::Mesh);
  const EntityId second = document.scene().create_entity_with_id(
      101u, "Second Entity", EntityKind::Mesh);
  ASSERT_NE(first, kInvalidEntityId);
  ASSERT_NE(second, kInvalidEntityId);
  document.scene().set_mesh(first, MeshComponent{.mesh = 10u});
  document.scene().set_mesh(second, MeshComponent{.mesh = 11u});
  const QdrSceneBinaryEncodeResult encoded = encode_qdr_scene_binary(
      qdr_project_write_snapshot_from_document(document), test_uuid());

  EXPECT_EQ(encoded.logical_geometry_count, 2u);
  EXPECT_EQ(encoded.unique_geometry_count, 1u);
  QdrProjectReadResult read =
      QdrDocumentReader::deserialize_project_document(encoded.bytes);
  ASSERT_TRUE(read.ok) << read.message;
  EXPECT_EQ(read.stats.unique_geometry_decoded_count, 1u);
  EXPECT_NE(read.document.assets().mesh(10u), nullptr);
  EXPECT_NE(read.document.assets().mesh(11u), nullptr);
}

TEST(QdrBinarySceneCodecTests, ProducesDeterministicSameBuildBytes) {
  const SceneContentDocument document = simple_scene();
  const QdrProjectWriteSnapshot snapshot =
      qdr_project_write_snapshot_from_document(document);

  const std::vector<std::byte> first =
      QdrDocumentWriter::serialize_project_document_bytes(snapshot,
                                                          test_uuid());
  const std::vector<std::byte> second =
      QdrDocumentWriter::serialize_project_document_bytes(snapshot,
                                                          test_uuid());
  EXPECT_EQ(first, second);
}

TEST(QdrBinarySceneCodecTests,
     MutationSplitsSemanticallyDeduplicatedGeometry) {
  SceneContentDocument document(
      SceneContentDocumentRecord{.id = 9u, .name = "Copy on write"});
  (void)document.assets().add_mesh(10u, triangle_mesh("First"));
  (void)document.assets().add_mesh(11u, triangle_mesh("Second"));
  const QdrSceneBinaryEncodeResult deduplicated = encode_qdr_scene_binary(
      qdr_project_write_snapshot_from_document(document), test_uuid());
  ASSERT_EQ(deduplicated.unique_geometry_count, 1u);

  ASSERT_TRUE(document.assets().update_mesh(11u, [](MeshAsset &mesh) {
    mesh.vertices.front().position.x = 3.0f;
  }));
  const QdrSceneBinaryEncodeResult split = encode_qdr_scene_binary(
      qdr_project_write_snapshot_from_document(document), test_uuid());
  EXPECT_EQ(split.unique_geometry_count, 2u);

  const QdrProjectReadResult read =
      QdrDocumentReader::deserialize_project_document(split.bytes);
  ASSERT_TRUE(read.ok) << read.message;
  const MeshAsset *first = read.document.assets().mesh(10u);
  const MeshAsset *second = read.document.assets().mesh(11u);
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  ASSERT_FALSE(first->vertices.empty());
  ASSERT_FALSE(second->vertices.empty());
  EXPECT_FLOAT_EQ(first->vertices.front().position.x, 0.0f);
  EXPECT_FLOAT_EQ(second->vertices.front().position.x, 3.0f);
}

TEST(QdrBinarySceneCodecTests, CompressesLargeRepetitiveGeometryAdaptively) {
  SceneContentDocument document(SceneContentDocumentRecord{
      .id = 8u, .name = "Compression"});
  MeshAsset mesh;
  mesh.name = "Large";
  mesh.vertices.resize(6000u);
  for (std::size_t index = 0; index < mesh.vertices.size(); ++index) {
    mesh.vertices[index].normal = {0.0f, 1.0f, 0.0f};
    mesh.vertices[index].rgba = 0xffffffffu;
  }
  (void)document.assets().add_mesh(20u, std::move(mesh));

  const QdrSceneBinaryEncodeResult encoded = encode_qdr_scene_binary(
      qdr_project_write_snapshot_from_document(document), test_uuid());
  EXPECT_EQ(encoded.logical_geometry_count, 1u);
  EXPECT_EQ(encoded.unique_geometry_count, 1u);
  EXPECT_EQ(encoded.compressed_geometry_count, 1u);
  EXPECT_GT(encoded.uncompressed_geometry_bytes,
            encoded.stored_geometry_bytes);
  EXPECT_GE(encoded.geometry_encode_ms, 0.0);
  EXPECT_GE(encoded.geometry_deduplication_ms, 0.0);
  EXPECT_GE(encoded.metadata_encode_ms, 0.0);
  EXPECT_GE(encoded.container_build_and_compression_ms, 0.0);

  quader_io::QdrBinaryContainerReadResult parsed =
      quader_io::read_qdr_binary_container(encoded.bytes);
  ASSERT_TRUE(parsed.ok) << parsed.message;
  EXPECT_EQ(parsed.compressed_chunk_count, 1u);
  EXPECT_EQ(parsed.decompressed_bytes, 0u);
  EXPECT_GE(parsed.decompression_ms, 0.0);
  const auto geometry = std::ranges::find_if(
      parsed.chunks, [](const quader_io::QdrBinaryChunk &chunk) {
        return chunk.type == static_cast<std::uint32_t>(
                                 quader_io::QdrBinaryChunkType::Geometry);
      });
  ASSERT_NE(geometry, parsed.chunks.end());
  EXPECT_EQ(geometry->codec, quader_io::QdrBinaryChunkCodec::Zstd);
  EXPECT_LT(geometry->stored_size, geometry->raw_size * 9u / 10u);
  EXPECT_FALSE(geometry->was_decompressed());
  EXPECT_FALSE(geometry->bytes().empty());
  EXPECT_TRUE(geometry->was_decompressed());
  EXPECT_GE(geometry->decompression_time_ms(), 0.0);
}

} // namespace
} // namespace quader

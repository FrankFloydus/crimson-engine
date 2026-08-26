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
#include <quader/modeling/neutral/polygon/document.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_set>
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
  MeshGeometryPayload &geometry = mesh.edit_geometry();
  geometry.vertices = {
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
  geometry.indices = {0u, 1u, 2u};
  geometry.primitives = {{.index_offset = 0u,
                          .index_count = 3u,
                          .material_slot = 0u}};
  return mesh;
}

[[nodiscard]] MeshAsset authored_triangle_mesh(std::string name,
                                               float x_offset) {
  MeshAsset mesh = triangle_mesh(std::move(name));
  for (MeshVertex &vertex : mesh.edit_geometry().vertices) {
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
  mesh.replace_authored_polygon_document(std::move(*document));
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

[[nodiscard]] QdrProjectWriteSnapshot
required_write_snapshot(const SceneContentDocument &document) {
  QdrProjectWriteSnapshotResult result =
      qdr_project_write_snapshot_from_document(document);
  if (!result.ok()) {
    throw std::runtime_error(result.error().message);
  }
  return result.take_value();
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
          required_write_snapshot(document), test_uuid());
  return quader_io::read_qdr_binary_container(std::move(bytes));
}

TEST(QdrBinarySceneCodecTests, RoundTripPreservesExplicitUuidAndSceneData) {
  SceneContentDocument document = simple_scene();
  const EntityId root = document.scene().scene_root();
  ASSERT_NE(root, kInvalidEntityId);
  ASSERT_TRUE(document.scene().set_selected(root, true));

  const std::vector<std::byte> bytes =
      QdrDocumentWriter::serialize_project_document_bytes(
          required_write_snapshot(document), test_uuid());
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
     SchemaFiveRoundTripPreservesViewportCameraPose) {
  QdrProjectWriteSnapshot snapshot =
      required_write_snapshot(simple_scene());
  snapshot.viewport_camera_pose = QdrSceneViewportCameraPose{
      .eye = {8.0f, 6.0f, 4.0f},
      .target = {1.0f, 2.0f, 3.0f},
      .up = {-0.2f, 0.95f, -0.1f},
  };

  const QdrProjectReadResult read =
      QdrDocumentReader::deserialize_project_document(
          QdrDocumentWriter::serialize_project_document_bytes(
              snapshot, test_uuid()));

  ASSERT_TRUE(read.ok) << read.message;
  ASSERT_TRUE(read.viewport_camera_pose.has_value());
  EXPECT_FLOAT_EQ(read.viewport_camera_pose->eye.x,
                  snapshot.viewport_camera_pose->eye.x);
  EXPECT_FLOAT_EQ(read.viewport_camera_pose->eye.y,
                  snapshot.viewport_camera_pose->eye.y);
  EXPECT_FLOAT_EQ(read.viewport_camera_pose->eye.z,
                  snapshot.viewport_camera_pose->eye.z);
  EXPECT_FLOAT_EQ(read.viewport_camera_pose->target.x,
                  snapshot.viewport_camera_pose->target.x);
  EXPECT_FLOAT_EQ(read.viewport_camera_pose->target.y,
                  snapshot.viewport_camera_pose->target.y);
  EXPECT_FLOAT_EQ(read.viewport_camera_pose->target.z,
                  snapshot.viewport_camera_pose->target.z);
  EXPECT_FLOAT_EQ(read.viewport_camera_pose->up.x,
                  snapshot.viewport_camera_pose->up.x);
  EXPECT_FLOAT_EQ(read.viewport_camera_pose->up.y,
                  snapshot.viewport_camera_pose->up.y);
  EXPECT_FLOAT_EQ(read.viewport_camera_pose->up.z,
                  snapshot.viewport_camera_pose->up.z);
}

TEST(QdrBinarySceneCodecTests,
     SchemaFourIgnoresAppendedViewportCameraPose) {
  QdrProjectWriteSnapshot snapshot =
      required_write_snapshot(simple_scene());
  snapshot.viewport_camera_pose = QdrSceneViewportCameraPose{
      .eye = {8.0f, 6.0f, 4.0f},
      .target = {1.0f, 2.0f, 3.0f},
      .up = {-0.2f, 0.95f, -0.1f},
  };
  const quader_io::QdrBinaryContainerReadResult parsed =
      quader_io::read_qdr_binary_container(
          QdrDocumentWriter::serialize_project_document_bytes(
              snapshot, test_uuid()));
  ASSERT_TRUE(parsed.ok) << parsed.message;
  std::vector<quader_io::QdrBinaryChunkInput> chunks = copy_chunks(parsed);
  const auto metadata = std::ranges::find_if(
      chunks, [](const quader_io::QdrBinaryChunkInput &chunk) {
        return chunk.type == quader_io::QdrBinaryChunkType::SceneMetadata;
      });
  ASSERT_NE(metadata, chunks.end());
  metadata->schema_version = 4u;

  const QdrProjectReadResult read =
      QdrDocumentReader::deserialize_project_document(
          quader_io::build_qdr_binary_container(test_uuid(),
                                                std::move(chunks)));

  ASSERT_TRUE(read.ok) << read.message;
  EXPECT_FALSE(read.viewport_camera_pose.has_value());
}

TEST(QdrBinarySceneCodecTests,
     SchemaSixRoundTripPreservesModelingObjectLocalFrameVersion) {
  SceneContentDocument document = simple_scene();
  ASSERT_TRUE(document.scene().set_modeling_object(
      100u, ModelingObjectComponent{
                .object_id = 42,
                .material_slot = 3u,
                .sdk_owned = true,
                .local_frame_version =
                    kCanonicalModelingObjectLocalFrameVersion,
            }));

  const quader_io::QdrBinaryContainerReadResult parsed =
      parse_written_scene(document);
  ASSERT_TRUE(parsed.ok) << parsed.message;
  const auto metadata = std::ranges::find_if(
      parsed.chunks, [](const quader_io::QdrBinaryChunk &chunk) {
        return chunk.type == static_cast<std::uint32_t>(
                                 quader_io::QdrBinaryChunkType::SceneMetadata);
      });
  ASSERT_NE(metadata, parsed.chunks.end());
  EXPECT_EQ(metadata->schema_version, 6u);

  const QdrProjectReadResult read =
      QdrDocumentReader::deserialize_project_document(
          QdrDocumentWriter::serialize_project_document_bytes(
              required_write_snapshot(document), test_uuid()));
  ASSERT_TRUE(read.ok) << read.message;
  const ModelingObjectComponent *modeling_object =
      read.document.scene().modeling_object(100u);
  ASSERT_NE(modeling_object, nullptr);
  EXPECT_EQ(modeling_object->local_frame_version,
            kCanonicalModelingObjectLocalFrameVersion);
}

TEST(QdrBinarySceneCodecTests,
     SchemaFiveTreatsAppendedModelingObjectFrameAsLegacyUnknown) {
  SceneContentDocument document = simple_scene();
  ASSERT_TRUE(document.scene().set_modeling_object(
      100u, ModelingObjectComponent{
                .object_id = 42,
                .sdk_owned = true,
                .local_frame_version =
                    kCanonicalModelingObjectLocalFrameVersion,
            }));
  const quader_io::QdrBinaryContainerReadResult parsed =
      parse_written_scene(document);
  ASSERT_TRUE(parsed.ok) << parsed.message;
  std::vector<quader_io::QdrBinaryChunkInput> chunks = copy_chunks(parsed);
  const auto metadata = std::ranges::find_if(
      chunks, [](const quader_io::QdrBinaryChunkInput &chunk) {
        return chunk.type == quader_io::QdrBinaryChunkType::SceneMetadata;
      });
  ASSERT_NE(metadata, chunks.end());
  metadata->schema_version = 5u;

  const QdrProjectReadResult read =
      QdrDocumentReader::deserialize_project_document(
          quader_io::build_qdr_binary_container(test_uuid(),
                                                std::move(chunks)));
  ASSERT_TRUE(read.ok) << read.message;
  const ModelingObjectComponent *modeling_object =
      read.document.scene().modeling_object(100u);
  ASSERT_NE(modeling_object, nullptr);
  EXPECT_EQ(modeling_object->local_frame_version,
            kLegacyUnknownModelingObjectLocalFrameVersion);
}

TEST(QdrBinarySceneCodecTests, RejectsInvalidViewportCameraPoses) {
  for (const QdrSceneViewportCameraPose invalid : {
           QdrSceneViewportCameraPose{
               .eye = {1.0f, 2.0f, 3.0f},
               .target = {1.0f, 2.0f, 3.0f},
               .up = {0.0f, 1.0f, 0.0f},
           },
           QdrSceneViewportCameraPose{
               .eye = {0.0f, 0.0f, 3.0f},
               .target = {0.0f, 0.0f, 0.0f},
               .up = {0.0f, 0.0f, 1.0f},
           },
           QdrSceneViewportCameraPose{
               .eye = {std::numeric_limits<float>::quiet_NaN(), 0.0f, 3.0f},
               .target = {0.0f, 0.0f, 0.0f},
               .up = {0.0f, 1.0f, 0.0f},
           },
       }) {
    QdrProjectWriteSnapshot snapshot =
        required_write_snapshot(simple_scene());
    snapshot.viewport_camera_pose = invalid;
    EXPECT_THROW(
        (void)QdrDocumentWriter::serialize_project_document_bytes(
            snapshot, test_uuid()),
        std::runtime_error);
  }
}

TEST(QdrBinarySceneCodecTests,
     RejectsStructurallyValidFilesWithInvalidViewportCameraPoses) {
  QdrProjectWriteSnapshot snapshot =
      required_write_snapshot(simple_scene());
  snapshot.viewport_camera_pose = QdrSceneViewportCameraPose{
      .eye = {8.0f, 6.0f, 4.0f},
      .target = {1.0f, 2.0f, 3.0f},
      .up = {0.0f, 1.0f, 0.0f},
  };
  const quader_io::QdrBinaryContainerReadResult parsed =
      quader_io::read_qdr_binary_container(
          QdrDocumentWriter::serialize_project_document_bytes(
              snapshot, test_uuid()));
  ASSERT_TRUE(parsed.ok) << parsed.message;
  std::vector<quader_io::QdrBinaryChunkInput> chunks = copy_chunks(parsed);
  const auto metadata = std::ranges::find_if(
      chunks, [](const quader_io::QdrBinaryChunkInput &chunk) {
        return chunk.type == quader_io::QdrBinaryChunkType::SceneMetadata;
  });
  ASSERT_NE(metadata, chunks.end());

  const std::array<float, 3> stored_eye = {8.0f, 6.0f, 4.0f};
  const std::span<const std::byte> stored_eye_bytes =
      std::as_bytes(std::span(stored_eye));
  const auto eye = std::search(
      metadata->bytes.begin(), metadata->bytes.end(),
      stored_eye_bytes.begin(), stored_eye_bytes.end());
  ASSERT_NE(eye, metadata->bytes.end());
  EXPECT_EQ(std::search(std::next(eye), metadata->bytes.end(),
                        stored_eye_bytes.begin(), stored_eye_bytes.end()),
            metadata->bytes.end())
      << "the test camera eye must be a unique serialized byte pattern";
  const float invalid = std::numeric_limits<float>::quiet_NaN();
  std::memcpy(std::addressof(*eye), std::addressof(invalid),
              sizeof(invalid));
  metadata->content_hash =
      quader_io::qdr_binary_content_hash(metadata->bytes);

  const QdrProjectReadResult read =
      QdrDocumentReader::deserialize_project_document(
          quader_io::build_qdr_binary_container(test_uuid(),
                                                std::move(chunks)));
  ASSERT_FALSE(read.ok);
  EXPECT_NE(read.message.find("invalid viewport camera pose"),
            std::string::npos);
}

TEST(QdrBinarySceneCodecTests,
     CurrentSchemaRoundTripPreservesLayersMembershipVisibilityColorAndLock) {
  SceneContentDocument document = simple_scene();
  SceneDocument &scene = document.scene();
  const SceneLayerId layer = scene.create_layer_with_id(
      17u, "Characters", false, {0.2f, 0.4f, 0.8f}, true);
  ASSERT_EQ(layer, 17u);
  ASSERT_TRUE(scene.set_entity_layer(100u, layer));

  const QdrProjectReadResult read =
      QdrDocumentReader::deserialize_project_document(
          QdrDocumentWriter::serialize_project_document_bytes(
              required_write_snapshot(document),
              test_uuid()));

  ASSERT_TRUE(read.ok) << read.message;
  const SceneLayerRecord *decoded = read.document.scene().layer(layer);
  ASSERT_NE(decoded, nullptr);
  EXPECT_EQ(decoded->name, "Characters");
  EXPECT_FALSE(decoded->visible);
  EXPECT_FLOAT_EQ(decoded->wireframe_color.x, 0.2f);
  EXPECT_FLOAT_EQ(decoded->wireframe_color.y, 0.4f);
  EXPECT_FLOAT_EQ(decoded->wireframe_color.z, 0.8f);
  EXPECT_TRUE(decoded->locked);
  EXPECT_EQ(read.document.scene().entity_layer(100u), layer);
  EXPECT_FALSE(read.document.scene().is_effectively_visible(100u));
  ASSERT_NE(read.document.scene().lock(100u), nullptr);
  EXPECT_FALSE(read.document.scene().lock(100u)->locked);
  EXPECT_TRUE(read.document.scene().is_effectively_locked(100u));
}

TEST(QdrBinarySceneCodecTests, LayerEncodingIsDeterministicByLayerId) {
  QdrProjectWriteSnapshot ordered =
      required_write_snapshot(simple_scene());
  ordered.layers = {
      {.id = 2u,
       .name = "Second",
       .visible = true,
       .wireframe_color = {0.8f, 0.4f, 0.2f},
       .locked = true},
      {.id = 1u,
       .name = "First",
       .visible = false,
       .wireframe_color = {0.1f, 0.2f, 0.3f},
       .locked = false},
  };
  QdrProjectWriteSnapshot reversed = ordered;
  std::reverse(reversed.layers.begin(), reversed.layers.end());

  EXPECT_EQ(encode_qdr_scene_binary(ordered, test_uuid()).bytes,
            encode_qdr_scene_binary(reversed, test_uuid()).bytes);
}

TEST(QdrBinarySceneCodecTests,
     RoundTripPreservesSceneLocalInstanceDefinitionAndOccurrence) {
  SceneContentDocument document = simple_scene();
  SceneDocument &scene = document.scene();
  const EntityId definition = scene.create_entity_with_id(
      200u, "Instance Definition", EntityKind::Generic);
  const EntityId member =
      scene.create_entity_with_id(201u, "Definition Member", EntityKind::Mesh);
  ASSERT_NE(definition, kInvalidEntityId);
  ASSERT_NE(member, kInvalidEntityId);
  scene.set_mesh(member, MeshComponent{.mesh = 10u});
  ASSERT_TRUE(scene.set_parent(member, definition, false));
  ASSERT_TRUE(scene.set_instance_definition(definition));

  const EntityId occurrence = scene.create_entity_with_id(
      202u, "Instance Occurrence", EntityKind::Instance);
  ASSERT_NE(occurrence, kInvalidEntityId);
  scene.set_transform(
      occurrence,
      TransformComponent{.position = {3.0f, 2.0f, 1.0f},
                         .rotation_euler = {0.0f, 0.5f, 0.0f},
                         .scale = {2.0f, 2.0f, 2.0f}});
  ASSERT_TRUE(scene.set_instance_occurrence(
      occurrence, InstanceOccurrenceComponent{.definition = definition}));

  const QdrProjectReadResult read =
      QdrDocumentReader::deserialize_project_document(
          QdrDocumentWriter::serialize_project_document_bytes(
              required_write_snapshot(document),
              test_uuid()));
  ASSERT_TRUE(read.ok) << read.message;
  EXPECT_TRUE(
      read.document.scene().is_instance_definition_entity(definition));
  EXPECT_EQ(read.document.scene().parent(member), definition);
  const InstanceOccurrenceComponent *decoded_occurrence =
      read.document.scene().instance_occurrence(occurrence);
  ASSERT_NE(decoded_occurrence, nullptr);
  EXPECT_EQ(decoded_occurrence->definition, definition);
  EXPECT_EQ(read.document.scene().kind(occurrence), EntityKind::Instance);
  ASSERT_NE(read.document.scene().transform(occurrence), nullptr);
  EXPECT_FLOAT_EQ(
      read.document.scene().transform(occurrence)->position.x, 3.0f);
}

TEST(QdrBinarySceneCodecTests, ReadsSchemasOneThroughThreeWithoutLayers) {
  const quader_io::QdrBinaryContainerReadResult parsed =
      parse_written_scene(simple_scene());
  ASSERT_TRUE(parsed.ok) << parsed.message;
  for (const std::uint16_t schema_version : {1u, 2u, 3u}) {
    std::vector<quader_io::QdrBinaryChunkInput> chunks = copy_chunks(parsed);
    const auto metadata = std::ranges::find_if(
        chunks, [](const quader_io::QdrBinaryChunkInput &chunk) {
          return chunk.type == quader_io::QdrBinaryChunkType::SceneMetadata;
        });
    ASSERT_NE(metadata, chunks.end());
    metadata->schema_version = schema_version;
    const QdrProjectReadResult read =
        QdrDocumentReader::deserialize_project_document(
            quader_io::build_qdr_binary_container(test_uuid(),
                                                  std::move(chunks)));
    ASSERT_TRUE(read.ok) << "schema " << schema_version << ": "
                         << read.message;
    EXPECT_EQ(read.document.assets().mesh_count(), 1u);
  }
}

TEST(QdrBinarySceneCodecTests,
     SchemaThreeIgnoresAppendedLayerDataAndLeavesObjectsUnassigned) {
  SceneContentDocument document = simple_scene();
  ASSERT_EQ(document.scene().create_layer_with_id(
                8u, "Legacy Hidden", false, {0.3f, 0.5f, 0.7f}),
            8u);
  ASSERT_TRUE(document.scene().set_entity_layer(100u, 8u));
  const quader_io::QdrBinaryContainerReadResult parsed =
      parse_written_scene(document);
  ASSERT_TRUE(parsed.ok) << parsed.message;
  std::vector<quader_io::QdrBinaryChunkInput> chunks = copy_chunks(parsed);
  const auto metadata = std::ranges::find_if(
      chunks, [](const quader_io::QdrBinaryChunkInput &chunk) {
        return chunk.type == quader_io::QdrBinaryChunkType::SceneMetadata;
      });
  ASSERT_NE(metadata, chunks.end());
  metadata->schema_version = 3u;

  const QdrProjectReadResult read =
      QdrDocumentReader::deserialize_project_document(
          quader_io::build_qdr_binary_container(test_uuid(),
                                                std::move(chunks)));
  ASSERT_TRUE(read.ok) << read.message;
  EXPECT_TRUE(read.document.scene().layers().empty());
  EXPECT_EQ(read.document.scene().entity_layer(100u),
            kInvalidSceneLayerId);
  EXPECT_TRUE(read.document.scene().is_effectively_visible(100u));
}

TEST(QdrBinarySceneCodecTests, RejectsInvalidSceneLayerRecords) {
  for (const QdrProjectSceneLayerWriteSnapshot invalid_layer : {
           QdrProjectSceneLayerWriteSnapshot{
               .id = 0u, .name = "Zero", .wireframe_color = {0.1f, 0.2f, 0.3f}},
           QdrProjectSceneLayerWriteSnapshot{
               .id = 4u, .name = "", .wireframe_color = {0.1f, 0.2f, 0.3f}},
           QdrProjectSceneLayerWriteSnapshot{
               .id = 5u,
               .name = "Bad Color",
               .wireframe_color = {
                   std::numeric_limits<float>::quiet_NaN(), 0.2f, 0.3f}},
       }) {
    QdrProjectWriteSnapshot snapshot =
        required_write_snapshot(simple_scene());
    snapshot.layers = {invalid_layer};
    const QdrProjectReadResult read =
        QdrDocumentReader::deserialize_project_document(
            QdrDocumentWriter::serialize_project_document_bytes(
                snapshot, test_uuid()));
    EXPECT_FALSE(read.ok);
    EXPECT_NE(read.message.find("invalid or duplicate scene layer"),
              std::string::npos);
  }

  QdrProjectWriteSnapshot duplicate =
      required_write_snapshot(simple_scene());
  duplicate.layers = {
      {.id = 3u, .name = "Duplicate"},
      {.id = 3u, .name = "Duplicate Id"},
  };
  const QdrProjectReadResult read =
      QdrDocumentReader::deserialize_project_document(
          QdrDocumentWriter::serialize_project_document_bytes(duplicate,
                                                               test_uuid()));
  EXPECT_FALSE(read.ok);
  EXPECT_NE(read.message.find("invalid or duplicate scene layer"),
            std::string::npos);

  duplicate.layers = {
      {.id = 3u, .name = "Same Name"},
      {.id = 4u, .name = "Same Name"},
  };
  const QdrProjectReadResult duplicate_name_read =
      QdrDocumentReader::deserialize_project_document(
          QdrDocumentWriter::serialize_project_document_bytes(duplicate,
                                                               test_uuid()));
  EXPECT_FALSE(duplicate_name_read.ok);
  EXPECT_NE(duplicate_name_read.message.find(
                "invalid or duplicate scene layer"),
            std::string::npos);
}

TEST(QdrBinarySceneCodecTests,
     RejectsDanglingAndForbiddenSceneLayerMemberships) {
  QdrProjectWriteSnapshot dangling =
      required_write_snapshot(simple_scene());
  const auto member = std::ranges::find_if(
      dangling.entities, [](const QdrProjectEntityWriteSnapshot &entity) {
        return entity.id == 100u;
      });
  ASSERT_NE(member, dangling.entities.end());
  member->layer_id = 404u;
  QdrProjectReadResult read =
      QdrDocumentReader::deserialize_project_document(
          QdrDocumentWriter::serialize_project_document_bytes(dangling,
                                                               test_uuid()));
  EXPECT_FALSE(read.ok);
  EXPECT_NE(read.message.find("layer memberships"), std::string::npos);

  QdrProjectWriteSnapshot forbidden =
      required_write_snapshot(simple_scene());
  forbidden.layers = {{.id = 7u, .name = "Forbidden"}};
  const auto root = std::ranges::find_if(
      forbidden.entities, [](const QdrProjectEntityWriteSnapshot &entity) {
        return entity.scene_root;
      });
  ASSERT_NE(root, forbidden.entities.end());
  root->layer_id = 7u;
  read = QdrDocumentReader::deserialize_project_document(
      QdrDocumentWriter::serialize_project_document_bytes(forbidden,
                                                           test_uuid()));
  EXPECT_FALSE(read.ok);
  EXPECT_NE(read.message.find("layer memberships"), std::string::npos);
}

TEST(QdrBinarySceneCodecTests, RejectsOccurrenceWithMissingDefinition) {
  QdrProjectWriteSnapshot snapshot =
      required_write_snapshot(simple_scene());
  snapshot.entities.push_back(QdrProjectEntityWriteSnapshot{
      .id = 300u,
      .name = "Broken Occurrence",
      .kind = EntityKind::Instance,
      .instance_occurrence =
          InstanceOccurrenceComponent{.definition = 999u},
  });
  const QdrProjectReadResult read =
      QdrDocumentReader::deserialize_project_document(
          QdrDocumentWriter::serialize_project_document_bytes(snapshot,
                                                               test_uuid()));
  ASSERT_FALSE(read.ok);
  EXPECT_NE(read.message.find("invalid instance occurrence"),
            std::string::npos);
}

TEST(QdrBinarySceneCodecTests, RejectsEmptyInstanceDefinition) {
  QdrProjectWriteSnapshot snapshot =
      required_write_snapshot(simple_scene());
  snapshot.entities.push_back(QdrProjectEntityWriteSnapshot{
      .id = 301u,
      .name = "Empty Definition",
      .kind = EntityKind::Generic,
      .instance_definition = InstanceDefinitionComponent{},
  });
  const QdrProjectReadResult read =
      QdrDocumentReader::deserialize_project_document(
          QdrDocumentWriter::serialize_project_document_bytes(snapshot,
                                                               test_uuid()));
  ASSERT_FALSE(read.ok);
  EXPECT_NE(read.message.find("invalid or empty instance definition"),
            std::string::npos);
}

TEST(QdrBinarySceneCodecTests,
     RejectsDualInstanceMarkersAndOccurrenceWithNonInstanceKind) {
  {
    QdrProjectWriteSnapshot snapshot =
        required_write_snapshot(simple_scene());
    snapshot.entities.push_back(QdrProjectEntityWriteSnapshot{
        .id = 302u,
        .name = "Dual Instance Marker",
        .kind = EntityKind::Instance,
        .instance_definition = InstanceDefinitionComponent{},
        .instance_occurrence =
            InstanceOccurrenceComponent{.definition = 302u},
    });
    const QdrProjectReadResult read =
        QdrDocumentReader::deserialize_project_document(
            QdrDocumentWriter::serialize_project_document_bytes(snapshot,
                                                                 test_uuid()));
    ASSERT_FALSE(read.ok);
    EXPECT_NE(read.message.find("both an instance definition"),
              std::string::npos);
  }

  {
    QdrProjectWriteSnapshot snapshot =
        required_write_snapshot(simple_scene());
    snapshot.entities.push_back(QdrProjectEntityWriteSnapshot{
        .id = 303u,
        .name = "Wrong Kind Occurrence",
        .kind = EntityKind::Mesh,
        .instance_occurrence =
            InstanceOccurrenceComponent{.definition = 999u},
    });
    const QdrProjectReadResult read =
        QdrDocumentReader::deserialize_project_document(
            QdrDocumentWriter::serialize_project_document_bytes(snapshot,
                                                                 test_uuid()));
    ASSERT_FALSE(read.ok);
    EXPECT_NE(read.message.find("non-instance entity kind"),
              std::string::npos);
  }
}

TEST(QdrBinarySceneCodecTests,
     RejectsNestedDefinitionsAndOccurrenceRootsWithChildren) {
  {
    QdrProjectWriteSnapshot snapshot =
        required_write_snapshot(simple_scene());
    snapshot.entities.push_back(QdrProjectEntityWriteSnapshot{
        .id = 310u,
        .name = "Outer Definition",
        .kind = EntityKind::Empty,
        .instance_definition = InstanceDefinitionComponent{},
    });
    snapshot.entities.push_back(QdrProjectEntityWriteSnapshot{
        .id = 311u,
        .name = "Nested Definition",
        .kind = EntityKind::Empty,
        .parent = 310u,
        .instance_definition = InstanceDefinitionComponent{},
    });
    snapshot.entities.push_back(QdrProjectEntityWriteSnapshot{
        .id = 312u,
        .name = "Nested Member",
        .kind = EntityKind::Mesh,
        .parent = 311u,
        .mesh = MeshComponent{.mesh = 10u},
    });
    const QdrProjectReadResult read =
        QdrDocumentReader::deserialize_project_document(
            QdrDocumentWriter::serialize_project_document_bytes(snapshot,
                                                                 test_uuid()));
    ASSERT_FALSE(read.ok);
    EXPECT_NE(read.message.find("definitions cannot be nested"),
              std::string::npos);
  }

  {
    QdrProjectWriteSnapshot snapshot =
        required_write_snapshot(simple_scene());
    snapshot.entities.push_back(QdrProjectEntityWriteSnapshot{
        .id = 320u,
        .name = "Leaf Definition",
        .kind = EntityKind::Empty,
        .instance_definition = InstanceDefinitionComponent{},
    });
    snapshot.entities.push_back(QdrProjectEntityWriteSnapshot{
        .id = 321u,
        .name = "Leaf Member",
        .kind = EntityKind::Mesh,
        .parent = 320u,
        .mesh = MeshComponent{.mesh = 10u},
    });
    snapshot.entities.push_back(QdrProjectEntityWriteSnapshot{
        .id = 322u,
        .name = "Nonleaf Occurrence",
        .kind = EntityKind::Instance,
        .instance_occurrence =
            InstanceOccurrenceComponent{.definition = 320u},
    });
    snapshot.entities.push_back(QdrProjectEntityWriteSnapshot{
        .id = 323u,
        .name = "Illegal Occurrence Child",
        .kind = EntityKind::Empty,
        .parent = 322u,
    });
    const QdrProjectReadResult read =
        QdrDocumentReader::deserialize_project_document(
            QdrDocumentWriter::serialize_project_document_bytes(snapshot,
                                                                 test_uuid()));
    ASSERT_FALSE(read.ok);
    EXPECT_NE(read.message.find("invalid instance occurrence"),
              std::string::npos);
  }
}

TEST(QdrBinarySceneCodecTests,
     PreservesFutureProjectAssetDependencyReferences) {
  const ProjectIoResult<ProjectRelativePath> path =
      ProjectRelativePath::parse_portable("Materials/wood.qmat");
  ASSERT_TRUE(path.ok()) << path.error().message;
  QdrProjectWriteSnapshot snapshot =
      required_write_snapshot(simple_scene());
  const std::optional<ProjectAssetId> dependency_id =
      ProjectAssetId::parse(
          "85fdfec9-795e-4afe-bf74-13555ee79cb4");
  ASSERT_TRUE(dependency_id.has_value());
  snapshot.project_asset_dependencies.push_back({
      .stable_id = *dependency_id,
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
            *dependency_id);
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
              required_write_snapshot(document),
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

  QdrProjectWriteSnapshotResult deferred_snapshot =
      qdr_project_write_snapshot_from_document(read.document);
  ASSERT_TRUE(deferred_snapshot.ok()) << deferred_snapshot.error().message;
  EXPECT_FALSE(read.document.assets().mesh_materialized(20u));
  ASSERT_EQ(deferred_snapshot.value().meshes.size(), 2u);
  const auto deferred_write = std::ranges::find_if(
      deferred_snapshot.value().meshes,
      [](const QdrProjectMeshAssetWriteSnapshot &mesh) {
        return mesh.id == 20u;
      });
  ASSERT_NE(deferred_write, deferred_snapshot.value().meshes.end());
  EXPECT_TRUE(deferred_write->encoded_geometry);
  EXPECT_FALSE(deferred_write->geometry);

  const QdrProjectReadResult resaved =
      QdrDocumentReader::deserialize_project_document(
          QdrDocumentWriter::serialize_project_document_bytes(
              deferred_snapshot.value(), test_uuid()));
  ASSERT_TRUE(resaved.ok) << resaved.message;
  EXPECT_EQ(resaved.document.assets().mesh_count(), 2u);
  EXPECT_TRUE(resaved.document.assets().has_mesh(20u));
  EXPECT_FALSE(resaved.document.assets().mesh_materialized(20u));

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

TEST(QdrBinarySceneCodecTests,
     FailedDeferredEncodedReadFailsSnapshotWithoutOmittingOrHydratingAsset) {
  SceneContentDocument document(
      SceneContentDocumentRecord{.id = 85u, .name = "Broken Deferred"});
  std::atomic_int materialize_calls = 0;
  std::atomic_int encoded_calls = 0;
  ASSERT_EQ(
      document.assets().add_deferred_mesh(
          91u, "Broken", true, false,
          [&materialize_calls]() {
            ++materialize_calls;
            return triangle_mesh("Should not hydrate");
          },
          [&encoded_calls]() {
            ++encoded_calls;
            return std::make_shared<const std::vector<std::byte>>();
          }),
      91u);

  const QdrProjectWriteSnapshotResult snapshot =
      qdr_project_write_snapshot_from_document(document);

  EXPECT_FALSE(snapshot.ok());
  EXPECT_NE(snapshot.error().message.find("encoded geometry is empty"),
            std::string::npos);
  EXPECT_EQ(encoded_calls.load(), 1);
  EXPECT_EQ(materialize_calls.load(), 0);
  EXPECT_TRUE(document.assets().has_mesh(91u));
  EXPECT_FALSE(document.assets().mesh_materialized(91u));
}

TEST(QdrBinarySceneCodecTests, AssetRegistryDeclaresOneOwnerThread) {
  AssetRegistry registry;
  EXPECT_TRUE(registry.on_owner_thread());

  std::atomic_bool worker_is_owner = true;
  std::thread worker([&registry, &worker_is_owner]() {
    worker_is_owner.store(registry.on_owner_thread());
  });
  worker.join();

  EXPECT_FALSE(worker_is_owner.load());
  EXPECT_TRUE(registry.on_owner_thread());
}

TEST(QdrBinarySceneCodecTests, RejectsUnsupportedMetadataSchema) {
  SceneContentDocument document = simple_scene();
  quader_io::QdrBinaryContainerReadResult parsed =
      parse_written_scene(document);
  ASSERT_TRUE(parsed.ok) << parsed.message;
  std::vector<quader_io::QdrBinaryChunkInput> chunks = copy_chunks(parsed);
  ASSERT_FALSE(chunks.empty());
  chunks.front().schema_version = 7u;

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
      required_write_snapshot(document), test_uuid());

  EXPECT_EQ(encoded.logical_geometry_count, 2u);
  EXPECT_EQ(encoded.geometry_encode_count, 1u);
  EXPECT_EQ(encoded.unique_geometry_count, 1u);
  QdrProjectReadResult read =
      QdrDocumentReader::deserialize_project_document(encoded.bytes);
  ASSERT_TRUE(read.ok) << read.message;
  EXPECT_EQ(read.stats.unique_geometry_decoded_count, 1u);
  EXPECT_NE(read.document.assets().mesh(10u), nullptr);
  EXPECT_NE(read.document.assets().mesh(11u), nullptr);
  EXPECT_EQ(read.document.assets().mesh(10u)->geometry_token(),
            read.document.assets().mesh(11u)->geometry_token());
}

TEST(QdrBinarySceneCodecTests,
     ReusesOnePinnedGeometryForTwoHundredLogicalAssetsAcrossRoundTrip) {
  SceneContentDocument document(
      SceneContentDocumentRecord{.id = 77u, .name = "Pinned geometry"});
  MeshAsset prototype = authored_triangle_mesh("Prototype", 0.0f);
  for (std::uint64_t index = 0u; index < 200u; ++index) {
    const AssetId asset_id = 1000u + index;
    MeshAsset logical = prototype;
    logical.name = "Logical " + std::to_string(index);
    ASSERT_EQ(document.assets().add_mesh(asset_id, std::move(logical)),
              asset_id);
    const EntityId entity = document.scene().create_entity_with_id(
        2000u + index, "Occurrence " + std::to_string(index),
        EntityKind::Mesh);
    ASSERT_NE(entity, kInvalidEntityId);
    document.scene().set_mesh(entity, MeshComponent{.mesh = asset_id});
  }

  const QdrSceneBinaryEncodeResult encoded = encode_qdr_scene_binary(
      required_write_snapshot(document), test_uuid());
  ASSERT_EQ(encoded.logical_geometry_count, 200u);
  EXPECT_EQ(encoded.geometry_encode_count, 1u);
  EXPECT_EQ(encoded.unique_geometry_count, 1u);

  QdrProjectReadResult read =
      QdrDocumentReader::deserialize_project_document(encoded.bytes);
  ASSERT_TRUE(read.ok) << read.message;
  ASSERT_EQ(read.document.assets().mesh_count(), 200u);
  std::unordered_set<std::uintptr_t> geometry_tokens;
  std::unordered_set<const quader_poly::Document *> authored_tokens;
  for (std::uint64_t index = 0u; index < 200u; ++index) {
    const MeshAsset *mesh = read.document.assets().mesh(1000u + index);
    ASSERT_NE(mesh, nullptr);
    geometry_tokens.insert(mesh->geometry_token());
    authored_tokens.insert(mesh->authored_polygon_document.get());
  }
  EXPECT_EQ(geometry_tokens.size(), 1u);
  EXPECT_EQ(authored_tokens.size(), 1u);

  ASSERT_TRUE(read.document.assets().update_mesh(1000u, [](MeshAsset &mesh) {
    mesh.edit_geometry().vertices.front().position.x = 4.0f;
    mesh.edit_authored_polygon_document().vertices.front().position.x = 4.0f;
  }));
  geometry_tokens.clear();
  authored_tokens.clear();
  for (std::uint64_t index = 0u; index < 200u; ++index) {
    const MeshAsset *mesh = read.document.assets().mesh(1000u + index);
    ASSERT_NE(mesh, nullptr);
    geometry_tokens.insert(mesh->geometry_token());
    authored_tokens.insert(mesh->authored_polygon_document.get());
  }
  EXPECT_EQ(geometry_tokens.size(), 2u);
  EXPECT_EQ(authored_tokens.size(), 2u);
  EXPECT_FLOAT_EQ(read.document.assets()
                      .mesh(1001u)
                      ->geometry()
                      .vertices.front()
                      .position.x,
                  0.0f);
}

TEST(QdrBinarySceneCodecTests, ProducesDeterministicSameBuildBytes) {
  const SceneContentDocument document = simple_scene();
  const QdrProjectWriteSnapshot snapshot =
      required_write_snapshot(document);

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
      required_write_snapshot(document), test_uuid());
  ASSERT_EQ(deduplicated.unique_geometry_count, 1u);

  ASSERT_TRUE(document.assets().update_mesh(11u, [](MeshAsset &mesh) {
    mesh.edit_geometry().vertices.front().position.x = 3.0f;
  }));
  const QdrSceneBinaryEncodeResult split = encode_qdr_scene_binary(
      required_write_snapshot(document), test_uuid());
  EXPECT_EQ(split.unique_geometry_count, 2u);

  const QdrProjectReadResult read =
      QdrDocumentReader::deserialize_project_document(split.bytes);
  ASSERT_TRUE(read.ok) << read.message;
  const MeshAsset *first = read.document.assets().mesh(10u);
  const MeshAsset *second = read.document.assets().mesh(11u);
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  ASSERT_FALSE(first->geometry().vertices.empty());
  ASSERT_FALSE(second->geometry().vertices.empty());
  EXPECT_FLOAT_EQ(first->geometry().vertices.front().position.x, 0.0f);
  EXPECT_FLOAT_EQ(second->geometry().vertices.front().position.x, 3.0f);
}

TEST(QdrBinarySceneCodecTests, CompressesLargeRepetitiveGeometryAdaptively) {
  SceneContentDocument document(SceneContentDocumentRecord{
      .id = 8u, .name = "Compression"});
  MeshAsset mesh;
  mesh.name = "Large";
  MeshGeometryPayload &geometry_payload = mesh.edit_geometry();
  geometry_payload.vertices.resize(6000u);
  for (std::size_t index = 0; index < geometry_payload.vertices.size();
       ++index) {
    geometry_payload.vertices[index].normal = {0.0f, 1.0f, 0.0f};
    geometry_payload.vertices[index].rgba = 0xffffffffu;
  }
  (void)document.assets().add_mesh(20u, std::move(mesh));

  const QdrSceneBinaryEncodeResult encoded = encode_qdr_scene_binary(
      required_write_snapshot(document), test_uuid());
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

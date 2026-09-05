////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/mesh_asset.hpp"
#include "editor/projection/mesh_projection_service.hpp"
#include "io/mesh/obj_writer.hpp"
#include "io/qdr/qdr_binary_container.hpp"
#include "io/qdr/qdr_document_reader.hpp"
#include "io/qdr/qdr_document_writer.hpp"
#include "io/qdr/qdr_project_write_snapshot.hpp"
#include "io/qdr/qdr_scene_binary_codec.hpp"
#include "scene/document/scene_content_document.hpp"

#include "qdr_scene_generated.h"

#include <gtest/gtest.h>
#include <quader/modeling/curves/curve_document.hpp>
#include <quader/modeling/neutral/polygon/document.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader {
namespace {

namespace qdr_fb = ::quader::qdr::fb;

#ifndef QUADER_SOURCE_DIR
#define QUADER_SOURCE_DIR "."
#endif

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

[[nodiscard]] CurveAsset authored_curve_asset(bool closed = false) {
  const std::array<modeling::CurvePointSpec, 3> points{{
      {.position = {0.0f, 1.0f, 2.0f}, .outgoing_handle = {1.0f, 2.0f, 0.0f}},
      {.position = {4.0f, 3.0f, 1.0f},
       .incoming_handle = {-2.0f, 1.0f, 0.0f},
       .outgoing_handle = {2.0f, -1.0f, 1.0f}},
      {.position = {8.0f, 2.0f, 0.0f}},
  }};
  modeling::CurveDocument document =
      modeling::CurveDocument::make_bezier(points, closed).or_throw();
  // A removed point leaves durable allocation history that must not be reused.
  EXPECT_TRUE(document.append_point({.position = {9.0f, 2.0f, 0.0f}})
                  .or_throw().changed);
  EXPECT_TRUE(document.remove_point(document.points().back().id)
                  .or_throw().changed);
  CurveAsset curve;
  curve.name = "Authored Path";
  curve.document =
      std::make_shared<const modeling::CurveDocument>(std::move(document));
  return curve;
}

[[nodiscard]] SceneContentDocument scene_with_curve(bool closed = false) {
  SceneContentDocument document = simple_scene();
  const AssetId curve_id =
      document.assets().add_curve(20u, authored_curve_asset(closed));
  if (curve_id == kInvalidAssetId) {
    throw std::runtime_error("Could not create the test curve asset.");
  }
  const EntityId entity = document.scene().create_entity_with_id(
      200u, "Curve Entity", EntityKind::Curve);
  if (!document.scene().set_curve(entity, {.curve = curve_id})) {
    throw std::runtime_error("Could not bind the test curve entity.");
  }
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

// Erase an appended optional field while retaining the legacy packed layout.
// FlatBuffers permits unreachable bytes, so this simulates an older producer
// without depending on build-generated schema headers in the test target.
void erase_flatbuffer_root_field(std::vector<std::byte> &bytes,
                                 std::size_t field_index) {
  ASSERT_GE(bytes.size(), sizeof(std::uint32_t));
  std::uint32_t root_offset = 0;
  std::memcpy(&root_offset, bytes.data(), sizeof(root_offset));
  ASSERT_LE(root_offset + sizeof(std::int32_t), bytes.size());
  std::int32_t vtable_distance = 0;
  std::memcpy(&vtable_distance, bytes.data() + root_offset,
              sizeof(vtable_distance));
  const std::int64_t vtable_offset =
      static_cast<std::int64_t>(root_offset) - vtable_distance;
  ASSERT_GE(vtable_offset, 0);
  const std::size_t field_offset = 4u + field_index * sizeof(std::uint16_t);
  ASSERT_LE(static_cast<std::size_t>(vtable_offset) + field_offset +
                sizeof(std::uint16_t),
            bytes.size());
  std::uint16_t vtable_size = 0;
  std::memcpy(&vtable_size, bytes.data() + vtable_offset, sizeof(vtable_size));
  ASSERT_GE(vtable_size, field_offset + sizeof(std::uint16_t));
  const std::uint16_t absent = 0;
  std::memcpy(bytes.data() + vtable_offset + field_offset, &absent,
              sizeof(absent));
}

template <typename Scalar>
void overwrite_flatbuffer_table_scalar(std::vector<std::byte> &bytes,
                                       std::size_t table_offset,
                                       std::size_t field_index,
                                       Scalar value) {
  static_assert(std::is_trivially_copyable_v<Scalar>);
  ASSERT_LE(table_offset + sizeof(std::int32_t), bytes.size());
  std::int32_t vtable_distance = 0;
  std::memcpy(&vtable_distance, bytes.data() + table_offset,
              sizeof(vtable_distance));
  const std::int64_t vtable_offset =
      static_cast<std::int64_t>(table_offset) - vtable_distance;
  ASSERT_GE(vtable_offset, 0);
  const std::size_t vtable_entry =
      4u + field_index * sizeof(std::uint16_t);
  ASSERT_LE(static_cast<std::size_t>(vtable_offset) + vtable_entry +
                sizeof(std::uint16_t),
            bytes.size());
  std::uint16_t vtable_size = 0;
  std::memcpy(&vtable_size, bytes.data() + vtable_offset,
              sizeof(vtable_size));
  ASSERT_GE(vtable_size, vtable_entry + sizeof(std::uint16_t));
  std::uint16_t field_offset = 0;
  std::memcpy(&field_offset,
              bytes.data() + vtable_offset + vtable_entry,
              sizeof(field_offset));
  ASSERT_NE(field_offset, 0u);
  ASSERT_LE(table_offset + field_offset + sizeof(Scalar), bytes.size());
  std::memcpy(bytes.data() + table_offset + field_offset, &value,
              sizeof(value));
}

[[nodiscard]] quader_io::QdrBinaryContainerReadResult
parse_written_scene(const SceneContentDocument &document) {
  std::vector<std::byte> bytes =
      QdrDocumentWriter::serialize_project_document_bytes(
          required_write_snapshot(document), test_uuid());
  return quader_io::read_qdr_binary_container(std::move(bytes));
}

[[nodiscard]] bool root_flatbuffer_field_is_present(
    std::span<const std::byte> bytes, std::size_t field_index) {
  if (bytes.size() < sizeof(std::uint32_t)) {
    return false;
  }
  std::uint32_t root_offset = 0u;
  std::memcpy(&root_offset, bytes.data(), sizeof(root_offset));
  if (root_offset + sizeof(std::int32_t) > bytes.size()) {
    return false;
  }
  std::int32_t vtable_distance = 0;
  std::memcpy(&vtable_distance, bytes.data() + root_offset,
              sizeof(vtable_distance));
  const std::int64_t vtable_offset =
      static_cast<std::int64_t>(root_offset) - vtable_distance;
  const std::size_t entry_offset =
      4u + field_index * sizeof(std::uint16_t);
  if (vtable_offset < 0 ||
      static_cast<std::size_t>(vtable_offset) + entry_offset +
              sizeof(std::uint16_t) >
          bytes.size()) {
    return false;
  }
  std::uint16_t vtable_size = 0u;
  std::memcpy(&vtable_size, bytes.data() + vtable_offset,
              sizeof(vtable_size));
  if (vtable_size < entry_offset + sizeof(std::uint16_t)) {
    return false;
  }
  std::uint16_t field_offset = 0u;
  std::memcpy(&field_offset,
              bytes.data() + vtable_offset + entry_offset,
              sizeof(field_offset));
  return field_offset != 0u;
}

[[nodiscard]] std::vector<std::byte> legacy_object_record_fixture(
    EditorObjectId entity_object_id, EditorObjectId legacy_object_id,
    bool legacy_visible) {
  flatbuffers::FlatBufferBuilder builder;
  const auto transform = qdr_fb::CreateTransformComponent(
      builder, nullptr, nullptr, nullptr, false);
  const auto visibility =
      qdr_fb::CreateVisibilityComponent(builder, true, true);
  const auto entity = qdr_fb::CreateEntity(
      builder, 100u, {}, entity_object_id,
      builder.CreateString("Canonical Entity"),
      qdr_fb::EntityKind::Generic, 0u, transform, visibility);
  const auto legacy = qdr_fb::CreateObjectRecord(
      builder, legacy_object_id, 100u, 0u, 0u, -1,
      builder.CreateString("Retired Authored Name"), legacy_visible, 7u);
  const std::array entities{entity};
  const std::array objects{legacy};
  const auto root = qdr_fb::CreateScenePayload(
      builder, 91u, builder.CreateString("Legacy Object Fixture"), {}, {}, {},
      builder.CreateVector(entities.data(), entities.size()),
      builder.CreateVector(objects.data(), objects.size()));
  qdr_fb::FinishScenePayloadBuffer(builder, root);
  const auto *begin =
      reinterpret_cast<const std::byte *>(builder.GetBufferPointer());
  std::vector<std::byte> metadata(begin, begin + builder.GetSize());
  return quader_io::build_qdr_binary_container(
      test_uuid(),
      {{.type = quader_io::QdrBinaryChunkType::SceneMetadata,
        .schema_version = 3u,
        .flags = quader_io::QdrBinaryChunkFlags::Critical,
        .id = 0u,
        .content_hash = quader_io::qdr_binary_content_hash(metadata),
        .bytes = std::move(metadata),
      .allow_compression = false}});
}

[[nodiscard]] std::vector<std::byte>
historical_legacy_object_record_fixture() {
  const std::filesystem::path path =
      std::filesystem::path(QUADER_SOURCE_DIR) / "tests" / "fixtures" /
      "qdr" / "legacy_object_record_schema3.qdr";
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  if (!stream) {
    throw std::runtime_error("Could not open historical QDR fixture: " +
                             path.string());
  }
  const std::streampos end = stream.tellg();
  if (end <= 0) {
    throw std::runtime_error("Historical QDR fixture is empty: " +
                             path.string());
  }
  std::vector<std::byte> bytes(static_cast<std::size_t>(end));
  stream.seekg(0, std::ios::beg);
  if (!stream.read(reinterpret_cast<char *>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()))) {
    throw std::runtime_error("Could not read historical QDR fixture: " +
                             path.string());
  }
  return bytes;
}

[[nodiscard]] std::vector<std::byte> legacy_csr_geometry_fixture() {
  flatbuffers::FlatBufferBuilder builder;
  const std::vector<qdr_fb::PolygonVertex> vertices{
      {10u, qdr_fb::Vec3(0.0f, 0.0f, 0.0f)},
      {20u, qdr_fb::Vec3(1.0f, 0.0f, 0.0f)},
      {30u, qdr_fb::Vec3(0.0f, 1.0f, 0.0f)},
  };
  const std::vector<std::uint32_t> face_ids{9u};
  const std::vector<std::uint32_t> offsets{0u, 3u};
  const std::vector<std::uint32_t> face_vertices{10u, 20u, 30u};
  const std::vector<qdr_fb::Vec2> face_uvs{
      {0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}};
  const std::vector<std::uint32_t> material_slots{4u};
  const std::vector<qdr_fb::SurfaceShading> shading{
      qdr_fb::SurfaceShading::Authored};
  const std::vector<qdr_fb::PolygonEdge> hard_edges{{10u, 20u}};
  const auto root = qdr_fb::CreateGeometryPayloadDirect(
      builder, qdr_fb::GeometryKind::AuthoredPolygon, 31u, 10u, &vertices,
      &face_ids, &offsets, &face_vertices, &offsets, &face_uvs,
      &material_slots, &shading, &hard_edges);
  builder.Finish(root, "QDRG");
  const auto *begin =
      reinterpret_cast<const std::byte *>(builder.GetBufferPointer());
  return {begin, begin + builder.GetSize()};
}

[[nodiscard]] quader_poly::Document legacy_csr_topology() {
  quader_poly::Document document;
  document.next_vertex_id = 31u;
  document.next_face_id = 10u;
  document.vertices = {
      {.id = 10u, .position = {0.0f, 0.0f, 0.0f}},
      {.id = 20u, .position = {1.0f, 0.0f, 0.0f}},
      {.id = 30u, .position = {0.0f, 1.0f, 0.0f}},
  };
  document.faces = {{
      .id = 9u,
      .vertices = {10u, 20u, 30u},
      .uvs = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}},
      .material_slot = 4u,
      .normal_shading = quader_poly::SurfaceShading::Authored,
  }};
  document.hard_normal_edges = {quader_poly::make_edge(10u, 20u)};
  return document;
}

void expect_polygon_topology_exact(const quader_poly::Document &actual,
                                   const quader_poly::Document &expected) {
  EXPECT_EQ(actual.next_vertex_id, expected.next_vertex_id);
  EXPECT_EQ(actual.next_face_id, expected.next_face_id);
  ASSERT_EQ(actual.vertices.size(), expected.vertices.size());
  for (std::size_t index = 0u; index < expected.vertices.size(); ++index) {
    EXPECT_EQ(actual.vertices[index].id, expected.vertices[index].id);
    EXPECT_FLOAT_EQ(actual.vertices[index].position.x,
                    expected.vertices[index].position.x);
    EXPECT_FLOAT_EQ(actual.vertices[index].position.y,
                    expected.vertices[index].position.y);
    EXPECT_FLOAT_EQ(actual.vertices[index].position.z,
                    expected.vertices[index].position.z);
  }
  ASSERT_EQ(actual.faces.size(), expected.faces.size());
  for (std::size_t face_index = 0u; face_index < expected.faces.size();
       ++face_index) {
    const quader_poly::Face &actual_face = actual.faces[face_index];
    const quader_poly::Face &expected_face = expected.faces[face_index];
    EXPECT_EQ(actual_face.id, expected_face.id);
    EXPECT_EQ(actual_face.vertices, expected_face.vertices);
    ASSERT_EQ(actual_face.uvs.size(), expected_face.uvs.size());
    for (std::size_t uv_index = 0u; uv_index < expected_face.uvs.size();
         ++uv_index) {
      EXPECT_FLOAT_EQ(actual_face.uvs[uv_index].x,
                      expected_face.uvs[uv_index].x);
      EXPECT_FLOAT_EQ(actual_face.uvs[uv_index].y,
                      expected_face.uvs[uv_index].y);
    }
    EXPECT_EQ(actual_face.material_slot, expected_face.material_slot);
    EXPECT_EQ(actual_face.normal_shading, expected_face.normal_shading);
    EXPECT_EQ(actual_face.texture_transform, expected_face.texture_transform);
  }
  EXPECT_EQ(actual.hard_normal_edges, expected.hard_normal_edges);
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
     CurrentWriterOmitsLegacyObjectRecordsAndKeepsEntityIdentityExact) {
  SceneContentDocument document = simple_scene();
  const EntityId entity = 100u;
  const EditorObjectId object_id =
      document.scene().editor_object_id(entity);
  const quader_io::QdrBinaryContainerReadResult container =
      parse_written_scene(document);
  ASSERT_TRUE(container.ok) << container.message;
  const auto metadata = std::ranges::find_if(
      container.chunks, [](const quader_io::QdrBinaryChunk &chunk) {
        return chunk.type == static_cast<std::uint32_t>(
                                 quader_io::QdrBinaryChunkType::SceneMetadata);
      });
  ASSERT_NE(metadata, container.chunks.end());
  // ScenePayload.objects is field 6. Keeping it absent proves the retired
  // projection is no longer serialized by new writers.
  EXPECT_FALSE(root_flatbuffer_field_is_present(metadata->bytes(), 6u));
  flatbuffers::Verifier verifier(
      reinterpret_cast<const std::uint8_t *>(metadata->bytes().data()),
      metadata->bytes().size());
  ASSERT_TRUE(qdr_fb::VerifyScenePayloadBuffer(verifier));
  const qdr_fb::ScenePayload *payload = qdr_fb::GetScenePayload(
      metadata->bytes().data());
  ASSERT_NE(payload, nullptr);
  EXPECT_TRUE(payload->objects() == nullptr ||
              payload->objects()->size() == 0u);

  const QdrProjectReadResult read =
      QdrDocumentReader::deserialize_project_document(
          quader_io::build_qdr_binary_container(
              container.scene_uuid, copy_chunks(container)));
  ASSERT_TRUE(read.ok) << read.message;
  EXPECT_EQ(read.document.scene().editor_object_id(entity), object_id);
  EXPECT_EQ(read.document.scene().entity_for_editor_object_id(object_id),
            entity);
}

TEST(QdrBinarySceneCodecTests,
     RichEntityStateRoundTripPreservesEveryDurableComponent) {
  SceneContentDocument document = simple_scene();
  const EntityId entity = 100u;
  const AssetId material = document.assets().add_material(
      20u, make_default_quader_material("Rich Material"));
  ASSERT_EQ(material, 20u);
  ASSERT_EQ(document.scene().create_layer_with_id(
                7u, "Rich Layer", false, {0.2f, 0.4f, 0.6f}, true),
            7u);
  ASSERT_TRUE(document.scene().set_transform(
      entity, TransformComponent{
                  .position = {3.0f, 4.0f, 5.0f},
                  .rotation_euler = {0.1f, 0.2f, 0.3f},
                  .scale = {1.5f, 2.0f, 2.5f},
                  .custom_pivot = true,
              }));
  ASSERT_TRUE(document.scene().set_visible(entity, false));
  ASSERT_TRUE(document.scene().set_selectable(entity, false));
  ASSERT_TRUE(document.scene().set_locked(entity, true));
  ASSERT_TRUE(document.scene().set_modeling_object(
      entity, ModelingObjectComponent{
                  .object_id = 42,
                  .material_slot = 3u,
                  .sdk_owned = true,
                  .local_frame_version = 6u,
              }));
  const MeshComponent mesh{
      .mesh = 10u,
      .editor_only = true,
      .cast_shadows = false,
      .double_sided = true,
      .generate_collisions = false,
      .collision_type = MeshCollisionType::ConvexHull,
      .display_collision = true,
      .body_type = MeshPhysicsBodyType::RigidBody,
  };
  ASSERT_TRUE(document.scene().set_mesh(entity, mesh));
  const MaterialComponent materials{
      .material = material,
      .material_slots = {{3u, material}},
  };
  ASSERT_TRUE(document.scene().set_material(entity, materials));
  TextureMappingComponent mapping;
  mapping.sampling_from_local.values[12] = 8.0f;
  mapping.sampling_from_local.values[13] = -2.0f;
  ASSERT_TRUE(document.scene().set_texture_mapping(entity, mapping));
  ASSERT_TRUE(document.scene().set_entity_layer(entity, 7u));

  const QdrProjectReadResult read =
      QdrDocumentReader::deserialize_project_document(
          QdrDocumentWriter::serialize_project_document_bytes(
              required_write_snapshot(document), test_uuid()));
  ASSERT_TRUE(read.ok) << read.message;
  const SceneDocument &scene = read.document.scene();
  ASSERT_NE(scene.transform(entity), nullptr);
  EXPECT_FLOAT_EQ(scene.transform(entity)->position.x, 3.0f);
  EXPECT_FLOAT_EQ(scene.transform(entity)->position.y, 4.0f);
  EXPECT_FLOAT_EQ(scene.transform(entity)->position.z, 5.0f);
  EXPECT_FLOAT_EQ(scene.transform(entity)->rotation_euler.x, 0.1f);
  EXPECT_FLOAT_EQ(scene.transform(entity)->rotation_euler.y, 0.2f);
  EXPECT_FLOAT_EQ(scene.transform(entity)->rotation_euler.z, 0.3f);
  EXPECT_FLOAT_EQ(scene.transform(entity)->scale.x, 1.5f);
  EXPECT_FLOAT_EQ(scene.transform(entity)->scale.y, 2.0f);
  EXPECT_FLOAT_EQ(scene.transform(entity)->scale.z, 2.5f);
  EXPECT_TRUE(scene.transform(entity)->custom_pivot);
  EXPECT_FALSE(scene.visibility(entity)->visible);
  EXPECT_FALSE(scene.visibility(entity)->selectable);
  EXPECT_TRUE(scene.lock(entity)->locked);
  EXPECT_EQ(scene.modeling_object(entity)->object_id, 42);
  EXPECT_EQ(scene.modeling_object(entity)->material_slot, 3u);
  EXPECT_TRUE(scene.modeling_object(entity)->sdk_owned);
  EXPECT_EQ(scene.modeling_object(entity)->local_frame_version, 6u);
  EXPECT_EQ(*scene.mesh(entity), mesh);
  EXPECT_EQ(*scene.material(entity), materials);
  EXPECT_EQ(*scene.texture_mapping(entity), mapping);
  EXPECT_EQ(scene.entity_layer(entity), 7u);
}

TEST(QdrBinarySceneCodecTests,
     SlotOnlyEmbeddedMaterialsRoundTripWithExactSlotIds) {
  SceneContentDocument document = simple_scene();
  ASSERT_EQ(document.assets().add_material(
                20u, make_default_quader_material("First Face Material")),
            20u);
  ASSERT_EQ(document.assets().add_material(
                21u, make_default_quader_material("Second Face Material")),
            21u);
  const MaterialComponent expected{
      .material_slots = {{7u, 20u}, {42u, 21u}},
  };
  ASSERT_TRUE(document.scene().set_material(100u, expected));
  auto bytes = QdrDocumentWriter::serialize_project_document_bytes(
      required_write_snapshot(document), test_uuid());
  for (int pass = 0; pass < 2; ++pass) {
    SCOPED_TRACE(pass);
    const QdrProjectReadResult read =
        QdrDocumentReader::deserialize_project_document(bytes);
    ASSERT_TRUE(read.ok) << read.message;
    const MaterialComponent *material = read.document.scene().material(100u);
    ASSERT_NE(material, nullptr);
    EXPECT_EQ(*material, expected);
    EXPECT_FALSE(material->material_for_slot(0u).valid());
    EXPECT_TRUE(read.document.validate_asset_references().ok());
    bytes = QdrDocumentWriter::serialize_project_document_bytes(
        required_write_snapshot(read.document), test_uuid());
  }
}

TEST(QdrBinarySceneCodecTests,
     SlotOnlyMaterialsStillRejectMissingAndInvalidEmbeddedReferences) {
  SceneContentDocument document = simple_scene();
  ASSERT_EQ(document.assets().add_material(
                20u, make_default_quader_material("Face Material")),
            20u);
  ASSERT_TRUE(document.scene().set_material(
      100u, MaterialComponent{.material_slots = {{7u, 20u}}}));
  const auto bytes = QdrDocumentWriter::serialize_project_document_bytes(
      required_write_snapshot(document), test_uuid());
  const auto container = quader_io::read_qdr_binary_container(bytes);
  ASSERT_TRUE(container.ok) << container.message;

  for (const AssetId invalid_material : {kInvalidAssetId, AssetId{999u}}) {
    SCOPED_TRACE(invalid_material);
    auto chunks = copy_chunks(container);
    auto metadata = std::ranges::find_if(chunks, [](const auto &chunk) {
      return chunk.type == quader_io::QdrBinaryChunkType::SceneMetadata;
    });
    ASSERT_NE(metadata, chunks.end());
    const auto *payload = qdr_fb::GetScenePayload(metadata->bytes.data());
    const qdr_fb::MaterialSlotBinding *slot = nullptr;
    for (const qdr_fb::Entity *entity : *payload->entities()) {
      if (entity->id() == 100u && entity->material() != nullptr) {
        slot = entity->material()->material_slots()->Get(0u);
      }
    }
    ASSERT_NE(slot, nullptr);
    const auto offset = reinterpret_cast<const std::byte *>(slot) -
                        metadata->bytes.data();
    const qdr_fb::MaterialSlotBinding invalid_slot(slot->slot(),
                                                  invalid_material);
    std::memcpy(metadata->bytes.data() + offset, &invalid_slot,
                sizeof(invalid_slot));
    metadata->content_hash =
        quader_io::qdr_binary_content_hash(metadata->bytes);

    const QdrProjectReadResult read =
        QdrDocumentReader::deserialize_project_document(
            quader_io::build_qdr_binary_container(test_uuid(),
                                                  std::move(chunks)));
    EXPECT_FALSE(read.ok);
    EXPECT_NE(read.message.find(
                  "entity material slot references a missing material asset"),
              std::string::npos)
        << read.message;
  }
}

TEST(QdrBinarySceneCodecTests,
     LegacyObjectRecordOnlySuppliesMissingIdentityWhileEntityStateWins) {
  const std::vector<std::byte> fixture =
      historical_legacy_object_record_fixture();
  ASSERT_EQ(fixture.size(), 640u);
  EXPECT_EQ(quader_io::qdr_binary_content_hash(fixture),
            0xaeebe2803723d011ull)
      << "The historical fixture must remain byte-for-byte immutable.";
  const QdrProjectReadResult read =
      QdrDocumentReader::deserialize_project_document(fixture);
  ASSERT_TRUE(read.ok) << read.message;
  EXPECT_EQ(read.document.id(), 91u);
  EXPECT_EQ(read.document.name(), "Legacy Object Fixture");
  EXPECT_EQ(read.document.scene().editor_object_id(100u), 77u);
  EXPECT_EQ(read.document.scene().parent(100u), 1u);
  const VisibilityComponent *visibility =
      read.document.scene().visibility(100u);
  ASSERT_NE(visibility, nullptr);
  EXPECT_TRUE(visibility->visible);
  EXPECT_FALSE(visibility->selectable);
  ASSERT_NE(read.document.scene().lock(100u), nullptr);
  EXPECT_TRUE(read.document.scene().lock(100u)->locked);
  ASSERT_NE(read.document.scene().transform(100u), nullptr);
  EXPECT_FLOAT_EQ(read.document.scene().transform(100u)->position.x, 3.0f);
  EXPECT_FLOAT_EQ(read.document.scene().transform(100u)->position.y, 4.0f);
  EXPECT_FLOAT_EQ(read.document.scene().transform(100u)->position.z, 5.0f);
  EXPECT_TRUE(read.document.scene().transform(100u)->custom_pivot);
  EXPECT_EQ(read.document.scene().name(100u)->name, "Canonical Entity");
}

TEST(QdrBinarySceneCodecTests,
     LegacyObjectRecordConflictHasDeterministicDiagnostic) {
  const QdrProjectReadResult read =
      QdrDocumentReader::deserialize_project_document(
          legacy_object_record_fixture(55u, 77u, true));
  ASSERT_FALSE(read.ok);
  EXPECT_NE(read.message.find(
                "legacy object record id conflicts with its canonical scene "
                "entity"),
            std::string::npos);
}

TEST(QdrBinarySceneCodecTests,
     LegacySchemasOneThroughThreeCsrAuthoredGeometryRemainReadable) {
  for (const std::uint16_t schema_version : {1u, 2u, 3u}) {
    SceneContentDocument document = simple_scene();
    ASSERT_TRUE(document.assets().replace_mesh(
        10u, authored_triangle_mesh("Legacy CSR", 0.0f)));
    const quader_io::QdrBinaryContainerReadResult parsed =
        parse_written_scene(document);
    ASSERT_TRUE(parsed.ok) << parsed.message;
    std::vector<quader_io::QdrBinaryChunkInput> chunks = copy_chunks(parsed);
    for (quader_io::QdrBinaryChunkInput &chunk : chunks) {
      if (chunk.type == quader_io::QdrBinaryChunkType::SceneMetadata) {
        chunk.schema_version = schema_version;
      } else if (chunk.type == quader_io::QdrBinaryChunkType::Geometry) {
        chunk.bytes = legacy_csr_geometry_fixture();
        chunk.content_hash = quader_io::qdr_binary_content_hash(chunk.bytes);
      }
    }

    const QdrProjectReadResult read =
        QdrDocumentReader::deserialize_project_document(
            quader_io::build_qdr_binary_container(test_uuid(),
                                                  std::move(chunks)));
    ASSERT_TRUE(read.ok) << "schema " << schema_version << ": "
                         << read.message;
    const MeshAsset *mesh = read.document.assets().mesh(10u);
    ASSERT_NE(mesh, nullptr);
    ASSERT_TRUE(mesh->authored_polygon_document);
    const quader_poly::Document &topology = *mesh->authored_polygon_document;
    ASSERT_EQ(topology.vertices.size(), 3u);
    EXPECT_EQ(topology.next_vertex_id, 31u);
    ASSERT_EQ(topology.faces.size(), 1u);
    EXPECT_EQ(topology.faces.front().id, 9u);
    EXPECT_EQ(topology.faces.front().vertices,
              (std::vector<quader_poly::ElementId>{10u, 20u, 30u}));
    EXPECT_EQ(topology.faces.front().material_slot, 4u);
    EXPECT_EQ(topology.faces.front().normal_shading,
              quader_poly::SurfaceShading::Authored);
    EXPECT_EQ(topology.hard_normal_edges,
              (std::vector<quader_poly::Edge>{
                  quader_poly::make_edge(10u, 20u)}));
    EXPECT_FALSE(mesh->geometry().indices.empty());
  }
}

TEST(QdrBinarySceneCodecTests,
     CurrentSchemaRejectsAuthoredGeometryWithoutSdkPayload) {
  SceneContentDocument document = simple_scene();
  ASSERT_TRUE(document.assets().replace_mesh(
      10u, authored_triangle_mesh("Missing SDK Payload", 0.0f)));
  const quader_io::QdrBinaryContainerReadResult parsed =
      parse_written_scene(document);
  ASSERT_TRUE(parsed.ok) << parsed.message;
  std::vector<quader_io::QdrBinaryChunkInput> chunks = copy_chunks(parsed);
  for (quader_io::QdrBinaryChunkInput &chunk : chunks) {
    if (chunk.type == quader_io::QdrBinaryChunkType::Geometry) {
      chunk.bytes = legacy_csr_geometry_fixture();
      chunk.content_hash = quader_io::qdr_binary_content_hash(chunk.bytes);
    }
  }

  const QdrProjectReadResult read =
      QdrDocumentReader::deserialize_project_document(
          quader_io::build_qdr_binary_container(test_uuid(),
                                                std::move(chunks)));
  ASSERT_FALSE(read.ok);
  EXPECT_NE(read.message.find("missing its SDK topology payload"),
            std::string::npos);
}

TEST(QdrBinarySceneCodecTests,
     SchemasFourAndSevenAcceptSdkAuthoredGeometryPayload) {
  for (const std::uint16_t schema_version : {4u, 7u}) {
    SceneContentDocument document = simple_scene();
    ASSERT_TRUE(document.assets().replace_mesh(
        10u, authored_triangle_mesh("SDK Authored", 0.0f)));
    const quader_io::QdrBinaryContainerReadResult parsed =
        parse_written_scene(document);
    ASSERT_TRUE(parsed.ok) << parsed.message;
    std::vector<quader_io::QdrBinaryChunkInput> chunks = copy_chunks(parsed);
    for (quader_io::QdrBinaryChunkInput &chunk : chunks) {
      if (chunk.type == quader_io::QdrBinaryChunkType::SceneMetadata) {
        chunk.schema_version = schema_version;
      }
    }

    const QdrProjectReadResult read =
        QdrDocumentReader::deserialize_project_document(
            quader_io::build_qdr_binary_container(test_uuid(),
                                                  std::move(chunks)));
    ASSERT_TRUE(read.ok) << "schema " << schema_version << ": "
                         << read.message;
    const MeshAsset *mesh = read.document.assets().mesh(10u);
    ASSERT_NE(mesh, nullptr);
    EXPECT_TRUE(mesh->authored_polygon_document);
  }
}

TEST(QdrBinarySceneCodecTests,
     UnreferencedLegacyCsrGeometryResavesAsCanonicalSdkTopology) {
  SceneContentDocument source(
      SceneContentDocumentRecord{.id = 92u, .name = "Legacy Deferred"});
  ASSERT_EQ(source.assets().add_mesh(
                10u, authored_triangle_mesh("Referenced", 0.0f)),
            10u);
  ASSERT_EQ(source.assets().add_mesh(
                20u, authored_triangle_mesh("Legacy Deferred", 3.0f)),
            20u);
  const EntityId entity = source.scene().create_entity_with_id(
      100u, "Referenced Entity", EntityKind::Mesh);
  ASSERT_NE(entity, kInvalidEntityId);
  ASSERT_TRUE(source.scene().set_mesh(entity, MeshComponent{.mesh = 10u}));

  const quader_io::QdrBinaryContainerReadResult current =
      parse_written_scene(source);
  ASSERT_TRUE(current.ok) << current.message;
  std::vector<quader_io::QdrBinaryChunkInput> legacy_chunks =
      copy_chunks(current);
  std::uint64_t legacy_geometry_id = 0u;
  for (quader_io::QdrBinaryChunkInput &chunk : legacy_chunks) {
    if (chunk.type != quader_io::QdrBinaryChunkType::SceneMetadata) {
      continue;
    }
    chunk.schema_version = 3u;
    flatbuffers::Verifier verifier(
        reinterpret_cast<const std::uint8_t *>(chunk.bytes.data()),
        chunk.bytes.size());
    ASSERT_TRUE(qdr_fb::VerifyScenePayloadBuffer(verifier));
    const qdr_fb::ScenePayload *payload =
        qdr_fb::GetScenePayload(chunk.bytes.data());
    ASSERT_NE(payload, nullptr);
    ASSERT_NE(payload->meshes(), nullptr);
    for (const qdr_fb::MeshAsset *mesh : *payload->meshes()) {
      if (mesh->id() == 20u) {
        legacy_geometry_id = mesh->geometry_chunk_id();
        break;
      }
    }
  }
  ASSERT_NE(legacy_geometry_id, 0u);
  for (quader_io::QdrBinaryChunkInput &chunk : legacy_chunks) {
    if (chunk.type == quader_io::QdrBinaryChunkType::Geometry &&
        chunk.id == legacy_geometry_id) {
      chunk.bytes = legacy_csr_geometry_fixture();
      chunk.content_hash = quader_io::qdr_binary_content_hash(chunk.bytes);
    }
  }

  QdrProjectReadResult legacy =
      QdrDocumentReader::deserialize_project_document(
          quader_io::build_qdr_binary_container(test_uuid(),
                                                std::move(legacy_chunks)));
  ASSERT_TRUE(legacy.ok) << legacy.message;
  EXPECT_FALSE(legacy.document.assets().mesh_materialized(20u));

  QdrProjectWriteSnapshotResult snapshot =
      qdr_project_write_snapshot_from_document(legacy.document);
  ASSERT_TRUE(snapshot.ok()) << snapshot.error().message;
  EXPECT_TRUE(legacy.document.assets().mesh_materialized(20u));
  const auto legacy_mesh_snapshot = std::ranges::find_if(
      snapshot.value().meshes,
      [](const QdrProjectMeshAssetWriteSnapshot &mesh) {
        return mesh.id == 20u;
      });
  ASSERT_NE(legacy_mesh_snapshot, snapshot.value().meshes.end());
  EXPECT_FALSE(legacy_mesh_snapshot->encoded_geometry);
  EXPECT_TRUE(legacy_mesh_snapshot->geometry);
  EXPECT_TRUE(legacy_mesh_snapshot->authored_polygon_document);

  const std::vector<std::byte> canonical_bytes =
      QdrDocumentWriter::serialize_project_document_bytes(snapshot.value(),
                                                          test_uuid());
  const quader_io::QdrBinaryContainerReadResult canonical =
      quader_io::read_qdr_binary_container(canonical_bytes);
  ASSERT_TRUE(canonical.ok) << canonical.message;
  std::uint64_t canonical_geometry_id = 0u;
  for (const quader_io::QdrBinaryChunk &chunk : canonical.chunks) {
    if (chunk.type != static_cast<std::uint32_t>(
                          quader_io::QdrBinaryChunkType::SceneMetadata)) {
      continue;
    }
    EXPECT_EQ(chunk.schema_version, 9u);
    flatbuffers::Verifier verifier(
        reinterpret_cast<const std::uint8_t *>(chunk.bytes().data()),
        chunk.bytes().size());
    ASSERT_TRUE(qdr_fb::VerifyScenePayloadBuffer(verifier));
    const qdr_fb::ScenePayload *payload =
        qdr_fb::GetScenePayload(chunk.bytes().data());
    ASSERT_NE(payload, nullptr);
    ASSERT_NE(payload->meshes(), nullptr);
    for (const qdr_fb::MeshAsset *mesh : *payload->meshes()) {
      if (mesh->id() == 20u) {
        canonical_geometry_id = mesh->geometry_chunk_id();
        break;
      }
    }
  }
  ASSERT_NE(canonical_geometry_id, 0u);

  const auto canonical_geometry = std::ranges::find_if(
      canonical.chunks, [canonical_geometry_id](
                            const quader_io::QdrBinaryChunk &chunk) {
        return chunk.type == static_cast<std::uint32_t>(
                                 quader_io::QdrBinaryChunkType::Geometry) &&
               chunk.id == canonical_geometry_id;
      });
  ASSERT_NE(canonical_geometry, canonical.chunks.end());
  flatbuffers::Verifier geometry_verifier(
      reinterpret_cast<const std::uint8_t *>(
          canonical_geometry->bytes().data()),
      canonical_geometry->bytes().size());
  ASSERT_TRUE(
      geometry_verifier.VerifyBuffer<qdr_fb::GeometryPayload>("QDRG"));
  const qdr_fb::GeometryPayload *geometry =
      flatbuffers::GetRoot<qdr_fb::GeometryPayload>(
          canonical_geometry->bytes().data());
  ASSERT_NE(geometry, nullptr);
  EXPECT_EQ(geometry->kind(), qdr_fb::GeometryKind::AuthoredPolygon);
  ASSERT_NE(geometry->authored_payload(), nullptr);
  EXPECT_GT(geometry->authored_payload()->size(), 0u);
  const auto is_empty = [](const auto *values) {
    return values == nullptr || values->size() == 0u;
  };
  EXPECT_EQ(geometry->next_vertex_id(), 1u);
  EXPECT_EQ(geometry->next_face_id(), 1u);
  EXPECT_TRUE(is_empty(geometry->polygon_vertices()));
  EXPECT_TRUE(is_empty(geometry->face_ids()));
  EXPECT_TRUE(is_empty(geometry->face_vertex_offsets()));
  EXPECT_TRUE(is_empty(geometry->face_vertices()));
  EXPECT_TRUE(is_empty(geometry->face_uv_offsets()));
  EXPECT_TRUE(is_empty(geometry->face_uvs()));
  EXPECT_TRUE(is_empty(geometry->face_material_slots()));
  EXPECT_TRUE(is_empty(geometry->face_shading()));
  EXPECT_TRUE(is_empty(geometry->hard_normal_edges()));

  const QdrProjectReadResult reloaded =
      QdrDocumentReader::deserialize_project_document(canonical_bytes);
  ASSERT_TRUE(reloaded.ok) << reloaded.message;
  const MeshAsset *reloaded_mesh = reloaded.document.assets().mesh(20u);
  ASSERT_NE(reloaded_mesh, nullptr);
  ASSERT_TRUE(reloaded_mesh->authored_polygon_document);
  expect_polygon_topology_exact(*reloaded_mesh->authored_polygon_document,
                                legacy_csr_topology());
}

TEST(QdrBinarySceneCodecTests,
     SchemaNinePreservesAuthoredCurvesSolidifySettingsAndStableIds) {
  for (const bool closed : {false, true}) {
    SceneContentDocument document = scene_with_curve(closed);
    ASSERT_TRUE(document.scene().set_curve(
        200u, {.curve = 20u,
               .solidify = true,
               .solidify_resolution = 24,
               .solidify_radius = 1.75F}));
    ASSERT_TRUE(document.scene().set_transform(
        200u, TransformComponent{.position = {10.0f, -2.0f, 8.0f},
                                  .scale = {2.0f, 1.0f, 3.0f}}));
    ASSERT_TRUE(document.scene().set_modeling_object(
        200u, {.object_id = 81, .sdk_owned = true,
               .local_frame_version = kCanonicalModelingObjectLocalFrameVersion}));
    const auto before = modeling::serialize_curve_document(
        *document.assets().curve(20u)->document).or_throw();
    const auto bytes = QdrDocumentWriter::serialize_project_document_bytes(
        required_write_snapshot(document), test_uuid());
    const auto read = QdrDocumentReader::deserialize_project_document(bytes);
    ASSERT_TRUE(read.ok) << read.message;
    EXPECT_EQ(read.document.assets().mesh_count(), 1u);
    EXPECT_EQ(read.document.assets().curve_count(), 1u);
    EXPECT_EQ(read.document.scene().kind(200u), EntityKind::Curve);
    ASSERT_NE(read.document.scene().curve(200u), nullptr);
    EXPECT_EQ(read.document.scene().curve(200u)->curve, 20u);
    EXPECT_TRUE(read.document.scene().curve(200u)->solidify);
    EXPECT_EQ(read.document.scene().curve(200u)->solidify_resolution, 24);
    EXPECT_FLOAT_EQ(read.document.scene().curve(200u)->solidify_radius,
                    1.75F);
    EXPECT_EQ(read.document.scene().uuid(200u), document.scene().uuid(200u));
    EXPECT_EQ(read.document.scene().modeling_object(200u)->object_id, 81);
    EXPECT_FLOAT_EQ(read.document.scene().transform(200u)->position.x, 10.0f);
    EXPECT_FLOAT_EQ(read.document.scene().transform(200u)->scale.z, 3.0f);
    const CurveAsset *curve = read.document.assets().curve(20u);
    ASSERT_NE(curve, nullptr);
    ASSERT_TRUE(curve->document);
    EXPECT_EQ(curve->name, "Authored Path");
    EXPECT_EQ(curve->document->closed(), closed);
    EXPECT_EQ(modeling::serialize_curve_document(*curve->document).or_throw(),
              before);
    EXPECT_TRUE(read.document.validate_asset_references().ok());
    EXPECT_TRUE(read.document.validate_object_identity_invariants().ok());
  }
}

TEST(QdrBinarySceneCodecTests,
     SchemaEightCurveBindingsUseSolidifyDefaults) {
  SceneContentDocument document = scene_with_curve();
  ASSERT_TRUE(document.scene().set_curve(
      200u, {.curve = 20u,
             .solidify = true,
             .solidify_resolution = 64,
             .solidify_radius = 7.5F}));
  const auto parsed = parse_written_scene(document);
  ASSERT_TRUE(parsed.ok) << parsed.message;
  auto chunks = copy_chunks(parsed);
  auto metadata = std::ranges::find_if(chunks, [](const auto &chunk) {
    return chunk.type == quader_io::QdrBinaryChunkType::SceneMetadata;
  });
  ASSERT_NE(metadata, chunks.end());
  metadata->schema_version = 8u;

  const auto read = QdrDocumentReader::deserialize_project_document(
      quader_io::build_qdr_binary_container(test_uuid(), std::move(chunks)));
  ASSERT_TRUE(read.ok) << read.message;
  const CurveComponent *curve = read.document.scene().curve(200u);
  ASSERT_NE(curve, nullptr);
  EXPECT_EQ(curve->curve, 20u);
  EXPECT_FALSE(curve->solidify);
  EXPECT_EQ(curve->solidify_resolution, kCurveSolidifyDefaultResolution);
  EXPECT_FLOAT_EQ(curve->solidify_radius, kCurveSolidifyDefaultRadius);
}

TEST(QdrBinarySceneCodecTests, CurveWriteSnapshotPinsControlsBeforeLaterEdits) {
  SceneContentDocument document = scene_with_curve();
  const auto snapshot = required_write_snapshot(document);
  const auto original = snapshot.curves.front().document;
  auto edited = std::make_shared<modeling::CurveDocument>(*original);
  ASSERT_TRUE(edited->set_point(edited->points().front().id,
                               {.position = {-30.0f, 0.0f, 0.0f}}).ok());
  CurveAsset replacement = *document.assets().curve(20u);
  replacement.document = edited;
  ASSERT_TRUE(document.assets().replace_curve(20u, std::move(replacement)));
  const auto read = QdrDocumentReader::deserialize_project_document(
      QdrDocumentWriter::serialize_project_document_bytes(snapshot, test_uuid()));
  ASSERT_TRUE(read.ok) << read.message;
  ASSERT_NE(read.document.assets().curve(20u), nullptr);
  EXPECT_TRUE(read.document.assets().curve(20u)->document->has_same_geometry(*original));
  EXPECT_FALSE(read.document.assets().curve(20u)->document->has_same_geometry(*edited));
}

TEST(QdrBinarySceneCodecTests, RejectsMissingOrInconsistentCurveBindingsOnWrite) {
  SceneContentDocument document = scene_with_curve();
  auto snapshot = required_write_snapshot(document);
  const auto serialize_invalid_snapshot = [&snapshot]() {
    const auto bytes = QdrDocumentWriter::serialize_project_document_bytes(
        snapshot, test_uuid());
    ADD_FAILURE() << "Invalid curve snapshot serialized " << bytes.size()
                  << " bytes instead of being rejected.";
  };
  auto entity = std::ranges::find_if(snapshot.entities,
      [](const auto &value) { return value.id == 200u; });
  ASSERT_NE(entity, snapshot.entities.end());
  entity->curve->solidify_resolution = kCurveSolidifyMinimumResolution - 1;
  EXPECT_THROW(serialize_invalid_snapshot(), std::runtime_error);
  entity->curve->solidify_resolution = kCurveSolidifyDefaultResolution;
  entity->curve->solidify_radius =
      (std::numeric_limits<float>::quiet_NaN)();
  EXPECT_THROW(serialize_invalid_snapshot(), std::runtime_error);
  entity->curve->solidify_radius = kCurveSolidifyDefaultRadius;
  entity->curve->curve = 404u;
  EXPECT_THROW(serialize_invalid_snapshot(), std::runtime_error);
  entity->curve = CurveComponent{.curve = 20u};
  entity->mesh = MeshComponent{.mesh = 10u};
  EXPECT_THROW(serialize_invalid_snapshot(), std::runtime_error);
  entity->mesh.reset();
  entity->curve.reset();
  EXPECT_THROW(serialize_invalid_snapshot(), std::runtime_error);
  ASSERT_TRUE(document.assets().remove_curve(20u));
  EXPECT_FALSE(qdr_project_write_snapshot_from_document(document).ok());
}

TEST(QdrBinarySceneCodecTests, RejectsInvalidStoredCurveSolidifySettings) {
  SceneContentDocument document = scene_with_curve();
  ASSERT_TRUE(document.scene().set_curve(
      200u, {.curve = 20u,
             .solidify = true,
             .solidify_resolution = 24,
             .solidify_radius = 1.5F}));
  const auto parsed = parse_written_scene(document);
  ASSERT_TRUE(parsed.ok) << parsed.message;
  auto chunks = copy_chunks(parsed);
  auto metadata = std::ranges::find_if(chunks, [](const auto &chunk) {
    return chunk.type == quader_io::QdrBinaryChunkType::SceneMetadata;
  });
  ASSERT_NE(metadata, chunks.end());
  const qdr_fb::ScenePayload *payload =
      qdr_fb::GetScenePayload(metadata->bytes.data());
  ASSERT_NE(payload, nullptr);
  ASSERT_NE(payload->entities(), nullptr);
  const qdr_fb::CurveComponent *stored_curve = nullptr;
  for (const qdr_fb::Entity *stored : *payload->entities()) {
    if (stored != nullptr && stored->id() == 200u) {
      stored_curve = stored->curve();
      break;
    }
  }
  ASSERT_NE(stored_curve, nullptr);
  const auto *table_bytes =
      reinterpret_cast<const std::byte *>(stored_curve);
  const std::size_t table_offset =
      static_cast<std::size_t>(table_bytes - metadata->bytes.data());
  overwrite_flatbuffer_table_scalar<std::int32_t>(
      metadata->bytes, table_offset, 2u,
      kCurveSolidifyMinimumResolution - 1);
  metadata->content_hash = 0u;

  const auto read = QdrDocumentReader::deserialize_project_document(
      quader_io::build_qdr_binary_container(test_uuid(), std::move(chunks)));
  EXPECT_FALSE(read.ok);
  EXPECT_NE(read.message.find("invalid curve component"), std::string::npos);
}

TEST(QdrBinarySceneCodecTests, RejectsCorruptAuthoredCurveInsteadOfDroppingIt) {
  SceneContentDocument document = scene_with_curve();
  const auto encoded_curve = modeling::serialize_curve_document(
      *document.assets().curve(20u)->document).or_throw();
  const auto parsed = parse_written_scene(document);
  ASSERT_TRUE(parsed.ok) << parsed.message;
  auto chunks = copy_chunks(parsed);
  auto metadata = std::ranges::find_if(chunks, [](const auto &chunk) {
    return chunk.type == quader_io::QdrBinaryChunkType::SceneMetadata;
  });
  ASSERT_NE(metadata, chunks.end());
  auto encoded_start = std::search(metadata->bytes.begin(), metadata->bytes.end(),
                                   encoded_curve.begin(), encoded_curve.end());
  ASSERT_NE(encoded_start, metadata->bytes.end());
  *encoded_start = std::byte{0};
  metadata->content_hash = 0u;
  const auto read = QdrDocumentReader::deserialize_project_document(
      quader_io::build_qdr_binary_container(test_uuid(), std::move(chunks)));
  EXPECT_FALSE(read.ok);
  EXPECT_NE(read.message.find("curve asset is invalid"), std::string::npos);
}

TEST(QdrBinarySceneCodecTests, RejectsCurveBindingWithoutStoredAssetVector) {
  const auto parsed = parse_written_scene(scene_with_curve());
  ASSERT_TRUE(parsed.ok) << parsed.message;
  auto chunks = copy_chunks(parsed);
  auto metadata = std::ranges::find_if(chunks, [](const auto &chunk) {
    return chunk.type == quader_io::QdrBinaryChunkType::SceneMetadata;
  });
  ASSERT_NE(metadata, chunks.end());
  erase_flatbuffer_root_field(metadata->bytes, 10u);
  metadata->content_hash = 0u;
  const auto read = QdrDocumentReader::deserialize_project_document(
      quader_io::build_qdr_binary_container(test_uuid(), std::move(chunks)));
  EXPECT_FALSE(read.ok);
  EXPECT_NE(read.message.find("missing curve asset"), std::string::npos);
}

TEST(QdrBinarySceneCodecTests, SchemaSevenStillReadsMeshesButRejectsCurvePayloads) {
  for (const bool includes_curve : {false, true}) {
    const auto parsed = parse_written_scene(
        includes_curve ? scene_with_curve() : simple_scene());
    ASSERT_TRUE(parsed.ok) << parsed.message;
    auto chunks = copy_chunks(parsed);
    auto metadata = std::ranges::find_if(chunks, [](const auto &chunk) {
      return chunk.type == quader_io::QdrBinaryChunkType::SceneMetadata;
    });
    ASSERT_NE(metadata, chunks.end());
    metadata->schema_version = 7u;
    const auto read = QdrDocumentReader::deserialize_project_document(
        quader_io::build_qdr_binary_container(test_uuid(), std::move(chunks)));
    EXPECT_EQ(read.ok, !includes_curve) << read.message;
    if (!includes_curve) {
      EXPECT_EQ(read.document.assets().mesh_count(), 1u);
      EXPECT_EQ(read.document.assets().curve_count(), 0u);
    }
  }
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
  EXPECT_EQ(metadata->schema_version, 9u);

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

  TextureMappingComponent member_mapping;
  member_mapping.sampling_from_local.values[0] = 2.0f;
  member_mapping.sampling_from_local.values[13] = -0.25f;
  TextureMappingComponent occurrence_mapping;
  occurrence_mapping.sampling_from_local.values[12] = 3.0f;
  ASSERT_TRUE(scene.set_texture_mapping(member, member_mapping));
  ASSERT_TRUE(scene.set_texture_mapping(occurrence, occurrence_mapping));
  ASSERT_TRUE(scene.set_texture_mapping(100u, member_mapping));

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
  for (const EntityId entity : std::array<EntityId, 2>{100u, member}) {
    ASSERT_NE(read.document.scene().texture_mapping(entity), nullptr);
    EXPECT_EQ(*read.document.scene().texture_mapping(entity), member_mapping);
  }
  ASSERT_NE(read.document.scene().texture_mapping(occurrence), nullptr);
  EXPECT_EQ(*read.document.scene().texture_mapping(occurrence),
            occurrence_mapping);
}

TEST(QdrBinarySceneCodecTests, SchemaSixIgnoresAppendedTextureMapping) {
  SceneContentDocument document = simple_scene();
  TextureMappingComponent mapping;
  mapping.sampling_from_local.values[0] = 2.0f;
  ASSERT_TRUE(document.scene().set_texture_mapping(100u, mapping));
  const auto parsed = parse_written_scene(document);
  ASSERT_TRUE(parsed.ok) << parsed.message;
  auto chunks = copy_chunks(parsed);
  const auto metadata = std::ranges::find_if(chunks, [](const auto &chunk) {
    return chunk.type == quader_io::QdrBinaryChunkType::SceneMetadata;
  });
  ASSERT_NE(metadata, chunks.end());
  metadata->schema_version = 6u;
  const QdrProjectReadResult read =
      QdrDocumentReader::deserialize_project_document(
          quader_io::build_qdr_binary_container(test_uuid(), std::move(chunks)));
  ASSERT_TRUE(read.ok) << read.message;
  EXPECT_EQ(read.document.scene().texture_mapping(100u), nullptr);
}

TEST(QdrBinarySceneCodecTests, RejectsInvalidTextureMappingSnapshots) {
  for (const float invalid : {0.0f, std::numeric_limits<float>::infinity()}) {
    auto snapshot = required_write_snapshot(simple_scene());
    const auto entity = std::ranges::find_if(snapshot.entities, [](const auto &v) {
      return v.id == 100u;
    });
    ASSERT_NE(entity, snapshot.entities.end());
    TextureMappingComponent mapping;
    mapping.sampling_from_local.values[0] = invalid;
    entity->texture_mapping = mapping;
    EXPECT_THROW((void)QdrDocumentWriter::serialize_project_document_bytes(
                     snapshot, test_uuid()),
                 std::runtime_error);
  }
}

TEST(QdrBinarySceneCodecTests, RoundTripPreservesFaceSettingsAndCompiledCarriers) {
  SceneContentDocument document = simple_scene();
  MeshAsset authored = authored_triangle_mesh("Textured Triangle", 0.0f);
  quader_poly::Document topology = *authored.authored_polygon_document;
  topology.faces.front().texture_transform = {
      .scale = {2.0f, -0.5f},
      .shift_uv = {0.25f, -0.125f},
      .rotation_degrees = 30.0f,
  };
  authored = MeshProjectionService::compile_polygon_document_mesh(topology);
  authored.replace_authored_polygon_document(topology);
  ASSERT_TRUE(document.assets().replace_mesh(10u, authored));

  MeshAsset compiled = triangle_mesh("Compiled Texture Carrier");
  for (MeshVertex &vertex : compiled.edit_geometry().vertices) {
    vertex.uv_carrier = {2.0f, -1.0f, 0.25f, 3.0f};
    vertex.uv_geometric_normal = {0.0f, 0.0f, 1.0f};
  }
  ASSERT_EQ(document.assets().add_mesh(11u, compiled), 11u);
  const QdrProjectReadResult read =
      QdrDocumentReader::deserialize_project_document(
          QdrDocumentWriter::serialize_project_document_bytes(
              required_write_snapshot(document), test_uuid()));
  ASSERT_TRUE(read.ok) << read.message;
  const MeshAsset *loaded = read.document.assets().mesh(10u);
  ASSERT_NE(loaded, nullptr);
  ASSERT_TRUE(loaded->authored_polygon_document);
  const auto &settings =
      loaded->authored_polygon_document->faces.front().texture_transform;
  EXPECT_FLOAT_EQ(settings.scale.x, 2.0f);
  EXPECT_FLOAT_EQ(settings.scale.y, -0.5f);
  EXPECT_FLOAT_EQ(settings.shift_uv.x, 0.25f);
  EXPECT_FLOAT_EQ(settings.shift_uv.y, -0.125f);
  EXPECT_FLOAT_EQ(settings.rotation_degrees, 30.0f);
  ASSERT_EQ(loaded->geometry().vertices.size(), authored.geometry().vertices.size());
  for (std::size_t i = 0; i < loaded->geometry().vertices.size(); ++i) {
    EXPECT_EQ(loaded->geometry().vertices[i].uv_carrier,
              authored.geometry().vertices[i].uv_carrier);
    EXPECT_FLOAT_EQ(loaded->geometry().vertices[i].uv.x,
                    authored.geometry().vertices[i].uv.x);
    EXPECT_FLOAT_EQ(loaded->geometry().vertices[i].uv.y,
                    authored.geometry().vertices[i].uv.y);
  }
  loaded = read.document.assets().mesh(11u);
  ASSERT_NE(loaded, nullptr);
  for (const MeshVertex &vertex : loaded->geometry().vertices) {
    EXPECT_EQ(vertex.uv_carrier, (std::array<float, 4>{2.0f, -1.0f, 0.25f, 3.0f}));
    EXPECT_FLOAT_EQ(vertex.uv_geometric_normal.z, 1.0f);
  }
}

TEST(QdrBinarySceneCodecTests,
     LegacyCompiledMeshRebuildsCarriersForUnlockedSamplingAndObjExport) {
  const auto parsed = parse_written_scene(simple_scene());
  ASSERT_TRUE(parsed.ok) << parsed.message;
  auto chunks = copy_chunks(parsed);
  for (auto &chunk : chunks) {
    if (chunk.type == quader_io::QdrBinaryChunkType::SceneMetadata) {
      chunk.schema_version = 6u;
    } else if (chunk.type == quader_io::QdrBinaryChunkType::Geometry) {
      // GeometryPayload.mesh_texture_mapping is the appended field 16.
      erase_flatbuffer_root_field(chunk.bytes, 16u);
      chunk.content_hash = quader_io::qdr_binary_content_hash(chunk.bytes);
    }
  }
  const auto read = QdrDocumentReader::deserialize_project_document(
      quader_io::build_qdr_binary_container(test_uuid(), std::move(chunks)));
  ASSERT_TRUE(read.ok) << read.message;
  const MeshAsset *loaded = read.document.assets().mesh(10u);
  ASSERT_NE(loaded, nullptr);
  EXPECT_FALSE(loaded->has_authored_polygon_document);
  ASSERT_EQ(loaded->geometry().vertices.size(), 3u);
  for (const MeshVertex &vertex : loaded->geometry().vertices) {
    EXPECT_NE(vertex.uv_carrier, (std::array<float, 4>{}));
    EXPECT_FLOAT_EQ(vertex.uv_geometric_normal.z, 1.0f);
  }
  EXPECT_FLOAT_EQ(loaded->geometry().vertices[1].uv.x, 1.0f);

  MeshAsset exported = *loaded;
  QMat4 sampling;
  sampling.values[0] = 2.0f;
  sampling.values[5] = 3.0f;
  sampling.values[12] = 1.0f;
  MeshProjectionService::apply_mesh_texture_mapping(exported, sampling);
  const std::string obj = ObjMeshWriter::serialize_mesh(exported, "Legacy");
  EXPECT_NE(obj.find("\nvt 1 0\n"), std::string::npos);
  EXPECT_NE(obj.find("\nvt 3 0\n"), std::string::npos);
  EXPECT_NE(obj.find("\nvt 1 3\n"), std::string::npos);
  EXPECT_NE(obj.find("\nv 1 0 0\n"), std::string::npos);
  EXPECT_FLOAT_EQ(loaded->geometry().vertices[1].uv.x, 1.0f);
}

TEST(QdrBinarySceneCodecTests,
     CompiledChartReconstructionSplitsConflictingCornersWithoutChangingShading) {
  MeshAsset mesh = triangle_mesh("Shared corners");
  auto &geometry = mesh.edit_geometry();
  geometry.vertices.push_back({.position = {0.0f, 0.0f, 1.0f},
                               .normal = {0.0f, 0.0f, 1.0f},
                               .uv = {1.0f, 0.0f}});
  geometry.indices = {0u, 1u, 2u, 0u, 2u, 3u};
  geometry.primitives.front().index_count = 6u;
  ASSERT_TRUE(MeshProjectionService::initialize_compiled_texture_mapping(mesh));
  const auto &projected = mesh.geometry();
  ASSERT_EQ(projected.indices.size(), 6u);
  EXPECT_NE(projected.indices[0], projected.indices[3]);
  EXPECT_NE(projected.indices[2], projected.indices[4]);
  EXPECT_FLOAT_EQ(projected.vertices[projected.indices[0]].uv_geometric_normal.z,
                  1.0f);
  EXPECT_FLOAT_EQ(projected.vertices[projected.indices[3]].uv_geometric_normal.x,
                  1.0f);
  for (const MeshVertex &vertex : projected.vertices) {
    EXPECT_FLOAT_EQ(vertex.normal.z, 1.0f);
  }
  EXPECT_FALSE(mesh.has_authored_polygon_document);
}

TEST(QdrBinarySceneCodecTests,
     CompiledChartReconstructionPreservesEmptyAndDegeneratePayloads) {
  MeshAsset empty;
  ASSERT_TRUE(MeshProjectionService::initialize_compiled_texture_mapping(empty));
  EXPECT_TRUE(empty.geometry().vertices.empty());
  EXPECT_TRUE(empty.geometry().indices.empty());

  MeshAsset mesh = triangle_mesh("Degenerate");
  auto &geometry = mesh.edit_geometry();
  for (MeshVertex &vertex : geometry.vertices) {
    vertex.position = {};
  }
  geometry.indices = {0u, 1u, 2u, 0u, 1u};
  geometry.primitives.front().index_count = 5u;
  const auto indices = geometry.indices;
  const auto primitives = geometry.primitives;
  ASSERT_TRUE(MeshProjectionService::initialize_compiled_texture_mapping(mesh));
  EXPECT_EQ(mesh.geometry().indices, indices);
  EXPECT_EQ(mesh.geometry().primitives, primitives);
  for (const MeshVertex &vertex : mesh.geometry().vertices) {
    EXPECT_EQ(vertex.uv_carrier, (std::array<float, 4>{}));
    EXPECT_FLOAT_EQ(vertex.normal.z, 1.0f);
  }
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
  ASSERT_EQ(document.assets().add_mesh(
                30u, triangle_mesh("Compiled Deferred")),
            30u);
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
  EXPECT_EQ(read.stats.logical_geometry_count, 3u);
  EXPECT_EQ(read.stats.unique_geometry_decoded_count, 1u);
  EXPECT_EQ(read.stats.unique_geometry_compiled_count, 1u);
  EXPECT_TRUE(read.document.assets().mesh_materialized(10u));
  EXPECT_FALSE(read.document.assets().mesh_materialized(20u));
  EXPECT_FALSE(read.document.assets().mesh_materialized(30u));

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
  ASSERT_EQ(deferred_snapshot.value().meshes.size(), 3u);
  const auto deferred_write = std::ranges::find_if(
      deferred_snapshot.value().meshes,
      [](const QdrProjectMeshAssetWriteSnapshot &mesh) {
        return mesh.id == 20u;
      });
  ASSERT_NE(deferred_write, deferred_snapshot.value().meshes.end());
  EXPECT_TRUE(deferred_write->encoded_geometry);
  EXPECT_FALSE(deferred_write->geometry);
  const auto compiled_deferred_write = std::ranges::find_if(
      deferred_snapshot.value().meshes,
      [](const QdrProjectMeshAssetWriteSnapshot &mesh) {
        return mesh.id == 30u;
      });
  ASSERT_NE(compiled_deferred_write, deferred_snapshot.value().meshes.end());
  EXPECT_TRUE(compiled_deferred_write->encoded_geometry);
  EXPECT_FALSE(compiled_deferred_write->geometry);
  EXPECT_FALSE(read.document.assets().mesh_materialized(30u));

  const QdrProjectReadResult resaved =
      QdrDocumentReader::deserialize_project_document(
          QdrDocumentWriter::serialize_project_document_bytes(
              deferred_snapshot.value(), test_uuid()));
  ASSERT_TRUE(resaved.ok) << resaved.message;
  EXPECT_EQ(resaved.document.assets().mesh_count(), 3u);
  EXPECT_TRUE(resaved.document.assets().has_mesh(20u));
  EXPECT_TRUE(resaved.document.assets().has_mesh(30u));
  EXPECT_FALSE(resaved.document.assets().mesh_materialized(20u));
  EXPECT_FALSE(resaved.document.assets().mesh_materialized(30u));

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
  chunks.front().schema_version = 10u;

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

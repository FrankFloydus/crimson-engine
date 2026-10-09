////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/qdr/qdr_binary_container.hpp"
#include "io/qdr/qdr_document_reader.hpp"
#include "io/qdr/qdr_document_writer.hpp"
#include "scene/document/scene_content_document.hpp"

#include "qdr_scene_generated.h"
#include "qdr_scene_usage_schema10_generated.h"

#include <quader/modeling/io/qdr_document.hpp>
#include <quader/modeling/curves/curve_document.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace quader {
namespace {

namespace current = ::quader::qdr::fb;
namespace retired = ::quader::qdr::usage_schema10::fb;

constexpr const char *kUsedPrimary = "11111111-1111-4111-8111-111111111111";
constexpr const char *kUsedSlot = "22222222-2222-4222-8222-222222222222";
constexpr const char *kDormant = "33333333-3333-4333-8333-333333333333";

struct CurrentUsageWire {
  using GeometryPayloadBuilder = current::GeometryPayloadBuilder;
  using GeometryKind = current::GeometryKind;
  using MaterialComponentBuilder = current::MaterialComponentBuilder;
  using Slot = current::ProjectMaterialSlotBinding;
  using SlotBuilder = current::ProjectMaterialSlotBindingBuilder;
  using MeshAsset = current::MeshAsset;
  using MeshAssetBuilder = current::MeshAssetBuilder;
  using MeshComponentBuilder = current::MeshComponentBuilder;
  using Entity = current::Entity;
  using EntityBuilder = current::EntityBuilder;
  using EntityKind = current::EntityKind;
  using PayloadBuilder = current::ScenePayloadBuilder;
};

struct RetiredUsageWire {
  using GeometryPayloadBuilder = retired::GeometryPayloadBuilder;
  using GeometryKind = retired::GeometryKind;
  using MaterialComponentBuilder = retired::MaterialComponentBuilder;
  using Slot = retired::ProjectMaterialSlotBinding;
  using SlotBuilder = retired::ProjectMaterialSlotBindingBuilder;
  using MeshAsset = retired::MeshAsset;
  using MeshAssetBuilder = retired::MeshAssetBuilder;
  using MeshComponentBuilder = retired::MeshComponentBuilder;
  using Entity = retired::Entity;
  using EntityBuilder = retired::EntityBuilder;
  using EntityKind = retired::EntityKind;
  using PayloadBuilder = retired::ScenePayloadBuilder;
};

[[nodiscard]] std::vector<std::byte>
builder_bytes(const flatbuffers::FlatBufferBuilder &builder) {
  const auto *begin = reinterpret_cast<const std::byte *>(builder.GetBufferPointer());
  return {begin, begin + builder.GetSize()};
}

[[nodiscard]] std::vector<std::byte> authored_bytes(bool corrupt = false) {
  auto mesh = modeling::PolygonDocument::make_box({}).or_throw();
  (void)mesh.assign_face_material_slot(mesh.face_ids().front(), 1u).or_throw();
  auto bytes = modeling::serialize_qdr_authored_topology(mesh).or_throw();
  if (corrupt) {
    bytes.front() = std::byte{0};
  }
  return bytes;
}

template <typename Wire>
[[nodiscard]] std::vector<std::byte> authored_geometry(bool corrupt) {
  flatbuffers::FlatBufferBuilder builder;
  const auto bytes = authored_bytes(corrupt);
  const auto payload = builder.CreateVector(
      reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size());
  typename Wire::GeometryPayloadBuilder geometry(builder);
  geometry.add_kind(Wire::GeometryKind::AuthoredPolygon);
  geometry.add_authored_payload(payload);
  const auto root = geometry.Finish();
  builder.Finish(root, "QDRG");
  return builder_bytes(builder);
}

[[nodiscard]] std::vector<std::byte> compiled_geometry() {
  flatbuffers::FlatBufferBuilder builder;
  const std::array vertices{
      current::MeshVertex({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}, 0u),
      current::MeshVertex({1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}, 0u),
      current::MeshVertex({0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}, 0u)};
  const std::array<std::uint32_t, 3> indices{0u, 1u, 2u};
  const std::array primitives{current::MeshPrimitive(0u, 3u, 1u),
                              current::MeshPrimitive(3u, 0u, 2u)};
  const auto stored_vertices = builder.CreateVectorOfStructs(vertices.data(), vertices.size());
  const auto stored_indices = builder.CreateVector(indices.data(), indices.size());
  const auto stored_primitives = builder.CreateVectorOfStructs(primitives.data(), primitives.size());
  current::GeometryPayloadBuilder geometry(builder);
  geometry.add_mesh_vertices(stored_vertices);
  geometry.add_mesh_indices(stored_indices);
  geometry.add_mesh_primitives(stored_primitives);
  const auto root = geometry.Finish();
  builder.Finish(root, "QDRG");
  return builder_bytes(builder);
}

template <typename Wire, bool Retired>
[[nodiscard]] std::vector<std::byte>
usage_fixture(bool corrupt_authored = false, bool missing_mesh = false,
              bool compiled = false) {
  flatbuffers::FlatBufferBuilder builder;
  std::vector<flatbuffers::Offset<typename Wire::Slot>> slots;
  for (const auto &[slot, id] : std::array{
           std::pair{1u, kUsedSlot}, std::pair{2u, kDormant}}) {
    const auto material_id = builder.CreateString(id);
    typename Wire::SlotBuilder binding(builder);
    binding.add_slot(slot);
    binding.add_project_material_id(material_id);
    slots.push_back(binding.Finish());
  }
  const auto stored_slots = builder.CreateVector(slots);
  const auto primary = builder.CreateString(kUsedPrimary);
  typename Wire::MaterialComponentBuilder material(builder);
  material.add_project_material_id(primary);
  material.add_project_material_slots(stored_slots);
  const auto material_component = material.Finish();
  std::vector<flatbuffers::Offset<typename Wire::MeshAsset>> meshes;
  std::vector<flatbuffers::Offset<typename Wire::Entity>> entities;
  std::array<std::uint64_t, 2> root_ids{100u, 101u};
  for (std::uint64_t index = 0u; index < 2u; ++index) {
    typename Wire::MeshAssetBuilder mesh(builder);
    mesh.add_id(10u + index);
    mesh.add_geometry_chunk_id(1u);
    mesh.add_has_authored_polygon_document(!compiled);
    meshes.push_back(mesh.Finish());
    typename Wire::MeshComponentBuilder component(builder);
    component.add_mesh(missing_mesh ? 999u : 10u + index);
    const auto mesh_component = component.Finish();
    typename Wire::EntityBuilder entity(builder);
    entity.add_id(root_ids[index]);
    entity.add_editor_object_id(200u + index);
    entity.add_kind(Wire::EntityKind::Mesh);
    entity.add_mesh(mesh_component);
    entity.add_material(material_component);
    entities.push_back(entity.Finish());
  }
  const auto stored_meshes = builder.CreateVector(meshes);
  const auto stored_entities = builder.CreateVector(entities);
  const auto stored_roots = builder.CreateVector(root_ids.data(), root_ids.size());
  const auto scene_name = builder.CreateString("Usage fixture");
  typename Wire::PayloadBuilder payload(builder);
  payload.add_scene_id(17u);
  payload.add_scene_name(scene_name);
  payload.add_meshes(stored_meshes);
  payload.add_entities(stored_entities);
  if constexpr (Retired) {
    payload.add_root_entities(stored_roots);
  }
  const auto root = payload.Finish();
  builder.Finish(root, "QDRS");
  auto metadata = builder_bytes(builder);
  auto geometry = compiled ? compiled_geometry() : authored_geometry<Wire>(corrupt_authored);
  std::array<std::byte, 16> uuid{};
  uuid.front() = std::byte{1};
  return quader_io::build_qdr_binary_container(uuid, {
      {.type = quader_io::QdrBinaryChunkType::SceneMetadata,
       .schema_version = Retired ? 10u : 14u,
       .flags = quader_io::QdrBinaryChunkFlags::Critical,
       .id = 0u, .content_hash = quader_io::qdr_binary_content_hash(metadata),
       .bytes = std::move(metadata), .allow_compression = false},
      {.type = quader_io::QdrBinaryChunkType::Geometry,
       .schema_version = Retired ? 2u : 1u,
       .flags = quader_io::QdrBinaryChunkFlags::Critical,
       .id = 1u, .content_hash = quader_io::qdr_binary_content_hash(geometry),
       .bytes = std::move(geometry), .allow_compression = false}});
}

TEST(QdrMaterialUsageTests, CurrentAuthoredUsageExcludesDormantSlotsWithoutCompiling) {
  const auto read = QdrDocumentReader::deserialize_project_material_usage(
      usage_fixture<CurrentUsageWire, false>());
  ASSERT_TRUE(read.ok) << read.message;
  EXPECT_TRUE(read.complete);
  EXPECT_EQ(read.material_ids, (std::vector<ProjectAssetId>{
      *ProjectAssetId::parse(kUsedPrimary), *ProjectAssetId::parse(kUsedSlot)}));
  EXPECT_EQ(read.stats.logical_geometry_count, 2u);
  EXPECT_EQ(read.stats.unique_geometry_decoded_count, 1u);
  EXPECT_EQ(read.stats.unique_geometry_compiled_count, 0u);
  EXPECT_EQ(read.stats.geometry_compile_ms, 0.0);
}

TEST(QdrMaterialUsageTests, RetiredDialectIsCompleteOnlyForUsage) {
  const auto bytes = usage_fixture<RetiredUsageWire, true>();
  const auto usage = QdrDocumentReader::deserialize_project_material_usage(bytes);
  ASSERT_TRUE(usage.ok) << usage.message;
  EXPECT_TRUE(usage.complete);
  EXPECT_EQ(usage.material_ids, (std::vector<ProjectAssetId>{
      *ProjectAssetId::parse(kUsedPrimary), *ProjectAssetId::parse(kUsedSlot)}));
  EXPECT_EQ(usage.stats.unique_geometry_decoded_count, 1u);
  EXPECT_EQ(usage.stats.unique_geometry_compiled_count, 0u);
  EXPECT_FALSE(QdrDocumentReader::deserialize_project_document(bytes).ok);
  EXPECT_FALSE(QdrDocumentReader::deserialize_project_dependencies(bytes).ok);
}

TEST(QdrMaterialUsageTests, CompiledUsageCountsOnlyNonemptyPrimitives) {
  const auto read = QdrDocumentReader::deserialize_project_material_usage(
      usage_fixture<CurrentUsageWire, false>(false, false, true));
  ASSERT_TRUE(read.ok) << read.message;
  EXPECT_EQ(read.material_ids, (std::vector<ProjectAssetId>{
      *ProjectAssetId::parse(kUsedSlot)}));
  EXPECT_EQ(read.stats.unique_geometry_compiled_count, 0u);
}

TEST(QdrMaterialUsageTests, DeferredGeometryInspectionReturnsActualSlots) {
  const auto read = QdrDocumentReader::deserialize_mesh_material_slots(
      authored_geometry<CurrentUsageWire>(false));
  ASSERT_TRUE(read.ok) << read.message;
  EXPECT_EQ(read.material_slots, (std::vector<std::uint32_t>{0u, 1u}));
  EXPECT_FALSE(QdrDocumentReader::deserialize_mesh_material_slots(
      authored_geometry<CurrentUsageWire>(true)).ok);
}

TEST(QdrMaterialUsageTests, CorruptTopologyAndDanglingReferencesAreNotComplete) {
  for (const auto &bytes : {
           usage_fixture<CurrentUsageWire, false>(true),
           usage_fixture<RetiredUsageWire, true>(true),
           usage_fixture<CurrentUsageWire, false>(false, true),
           usage_fixture<RetiredUsageWire, true>(false, true)}) {
    const auto read = QdrDocumentReader::deserialize_project_material_usage(bytes);
    EXPECT_FALSE(read.ok);
    EXPECT_FALSE(read.complete);
    EXPECT_FALSE(read.message.empty());
    EXPECT_TRUE(read.material_ids.empty());
  }
}

TEST(QdrMaterialUsageTests, ContainerChecksumFailureCannotProduceUsage) {
  auto bytes = usage_fixture<RetiredUsageWire, true>();
  const auto geometry = authored_geometry<RetiredUsageWire>(false);
  const auto stored = std::search(bytes.begin(), bytes.end(),
                                   geometry.begin(), geometry.end());
  ASSERT_NE(stored, bytes.end());
  *(stored + 4) ^= std::byte{1};
  const auto read = QdrDocumentReader::deserialize_project_material_usage(bytes);
  EXPECT_FALSE(read.ok);
  EXPECT_FALSE(read.complete);
  EXPECT_TRUE(read.material_ids.empty());
}

TEST(QdrMaterialUsageTests, HiddenLockedSolidCurvesCountWithoutDormantBindings) {
  SceneContentDocument document({.id = 7u, .name = "Curve usage"});
  const std::array<modeling::CurvePointSpec, 2> points{{
      {.position = {0.0f, 0.0f, 0.0f}}, {.position = {1.0f, 0.0f, 0.0f}}}};
  CurveAsset curve;
  curve.name = "Path";
  curve.document = std::make_shared<const modeling::CurveDocument>(
      modeling::CurveDocument::make_bezier(points, false).or_throw());
  ASSERT_EQ(document.assets().add_curve(10u, std::move(curve)), 10u);
  const EntityId entity = document.scene().create_entity_with_id(100u, "Solid path", EntityKind::Curve);
  ASSERT_TRUE(document.scene().set_curve(entity, {.curve = 10u, .solidify = true}));
  ASSERT_TRUE(document.scene().set_material(entity, {
      .material = MaterialReference::project(*ProjectAssetId::parse(kUsedPrimary)),
      .material_slots = {{2u, MaterialReference::project(*ProjectAssetId::parse(kDormant))}}}));
  ASSERT_TRUE(document.scene().set_selectable(entity, false));
  ASSERT_TRUE(document.scene().set_visible(entity, false));
  auto read = QdrDocumentReader::deserialize_project_material_usage(
      QdrDocumentWriter::serialize_project_document_bytes(document));
  ASSERT_TRUE(read.ok) << read.message;
  EXPECT_EQ(read.material_ids, (std::vector<ProjectAssetId>{*ProjectAssetId::parse(kUsedPrimary)}));
  ASSERT_TRUE(document.scene().set_curve(entity, {.curve = 10u, .solidify = false}));
  read = QdrDocumentReader::deserialize_project_material_usage(
      QdrDocumentWriter::serialize_project_document_bytes(document));
  ASSERT_TRUE(read.ok) << read.message;
  EXPECT_TRUE(read.material_ids.empty());
}

} // namespace
} // namespace quader

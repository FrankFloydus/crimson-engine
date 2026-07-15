////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include <quader/modeling/modeling.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace quader::modeling;

static_assert(std::is_same_v<
              decltype(serialize_qdr_document(
                  std::declval<const QdrDocumentDto &>())),
              Result<std::vector<std::byte>>>);
static_assert(std::is_same_v<
              decltype(deserialize_qdr_document(
                  std::declval<std::span<const std::byte>>())),
              Result<QdrDocumentDto>>);
static_assert(std::is_same_v<
              decltype(serialize_qdr_authored_topology(
                  std::declval<const PolygonDocument &>())),
              Result<std::vector<std::byte>>>);
static_assert(std::is_same_v<
              decltype(deserialize_qdr_authored_topology(
                  std::declval<std::span<const std::byte>>())),
              Result<PolygonDocument>>);
static_assert(std::is_same_v<
              decltype(std::declval<const IoApi &>().serialize_qdr()),
              std::vector<std::byte>>);
static_assert(std::is_same_v<
              decltype(std::declval<const CheckedIoApi &>().serialize_qdr()),
              Result<std::vector<std::byte>>>);

/**
 * Stores SDK mesh bounds for deterministic golden checks.
 */
struct SdkMeshBounds {
  Vec3 min{};
  Vec3 max{};
};

[[nodiscard]] std::optional<SdkMeshBounds>
mesh_bounds(const MeshPayload &mesh) {
  if (mesh.vertices.empty()) {
    return std::nullopt;
  }

  SdkMeshBounds bounds{
      .min = mesh.vertices.front().position,
      .max = mesh.vertices.front().position,
  };
  for (const MeshVertexPayload &vertex : mesh.vertices) {
    bounds.min.x = std::min(bounds.min.x, vertex.position.x);
    bounds.min.y = std::min(bounds.min.y, vertex.position.y);
    bounds.min.z = std::min(bounds.min.z, vertex.position.z);
    bounds.max.x = std::max(bounds.max.x, vertex.position.x);
    bounds.max.y = std::max(bounds.max.y, vertex.position.y);
    bounds.max.z = std::max(bounds.max.z, vertex.position.z);
  }
  return bounds;
}

void expect_vec3_near(Vec3 actual, Vec3 expected, float epsilon = 0.0001F) {
  EXPECT_NEAR(actual.x, expected.x, epsilon);
  EXPECT_NEAR(actual.y, expected.y, epsilon);
  EXPECT_NEAR(actual.z, expected.z, epsilon);
}

void expect_reversed_face_winding(const PolygonDocument &regular,
                                  const PolygonDocument &flipped) {
  Result<AuthoredPolygonPayload> regular_payload = regular.authored_payload();
  ASSERT_TRUE(regular_payload.ok()) << regular_payload.error_message();
  Result<AuthoredPolygonPayload> flipped_payload = flipped.authored_payload();
  ASSERT_TRUE(flipped_payload.ok()) << flipped_payload.error_message();
  ASSERT_EQ(regular_payload.value().faces.size(),
            flipped_payload.value().faces.size());

  for (std::size_t index = 0; index < regular_payload.value().faces.size();
       ++index) {
    const std::vector<VertexId> expected(
        regular_payload.value().faces[index].vertices.rbegin(),
        regular_payload.value().faces[index].vertices.rend());
    EXPECT_EQ(flipped_payload.value().faces[index].vertices, expected);
  }
}

[[nodiscard]] bool vec3_near(Vec3 actual, Vec3 expected,
                             float epsilon = 0.0001F) {
  return std::abs(actual.x - expected.x) <= epsilon &&
         std::abs(actual.y - expected.y) <= epsilon &&
         std::abs(actual.z - expected.z) <= epsilon;
}

[[nodiscard]] std::optional<Vec3>
authored_position(const AuthoredPolygonPayload &payload, VertexId vertex) {
  for (std::size_t index = 0; index < payload.vertices.size(); ++index) {
    if (payload.vertices[index] == vertex) {
      return payload.positions[index];
    }
  }
  return std::nullopt;
}

void write_u32_le(std::vector<std::byte> &bytes, std::size_t offset,
                  std::uint32_t value) {
  ASSERT_LE(offset + 4U, bytes.size());
  for (unsigned int index = 0; index < 4; ++index) {
    bytes[offset + index] =
        std::byte{static_cast<std::uint8_t>(value >> (index * 8U))};
  }
}

[[nodiscard]] std::vector<std::byte> byte_string(std::string_view value) {
  std::vector<std::byte> bytes;
  bytes.reserve(value.size());
  for (const char item : value) {
    bytes.push_back(std::byte{static_cast<std::uint8_t>(item)});
  }
  return bytes;
}

[[nodiscard]] bool has_magic(std::span<const std::byte> bytes,
                             std::string_view magic) {
  if (bytes.size() < magic.size()) {
    return false;
  }
  for (std::size_t index = 0; index < magic.size(); ++index) {
    if (bytes[index] !=
        std::byte{static_cast<std::uint8_t>(magic[index])}) {
      return false;
    }
  }
  return true;
}

TEST(ModelingSdkConsumerTests,
     BinaryQdrPreservesMetadataGenerationsAndPolygonStateDeterministically) {
  Result<PolygonDocument> polygon_result = PolygonDocument::make_box({});
  ASSERT_TRUE(polygon_result.ok()) << polygon_result.error_message();
  PolygonDocument polygon = std::move(polygon_result).value();
  ASSERT_FALSE(polygon.face_ids().empty());
  ASSERT_FALSE(polygon.edge_ids().empty());
  ASSERT_TRUE(polygon.select_face(polygon.face_ids().front()).ok());
  ASSERT_TRUE(polygon.shade_selected_faces_smooth().ok());
  ASSERT_TRUE(polygon.select_edge(polygon.edge_ids().front()).ok());
  ASSERT_TRUE(polygon.harden_selected_edge_normals().ok());
  ASSERT_TRUE(polygon.select_edge(polygon.edge_ids().back()).ok());

  std::string material_name{"Mat\0erial", 9};
  material_name.append("\xC3\xA9", 2);
  const std::string object_name{"Mesh\0A", 6};
  const MaterialId material_id = make_id<MaterialTag>(17U, 3U);
  const ObjectId object_id = make_id<ObjectTag>(41U, 9U);

  QdrDocumentDto document;
  document.materials.push_back({.id = material_id, .name = material_name});
  document.objects.push_back({
      .id = object_id,
      .material = material_id,
      .selected = true,
      .name = object_name,
      .document = polygon,
  });
  document.active_object = object_id;
  document.selection_mode = QdrSelectionMode::Edge;
  document.content_revision = 101U;
  document.selection_revision = 202U;

  Result<std::vector<std::byte>> first = serialize_qdr_document(document);
  ASSERT_TRUE(first.ok()) << first.error_message();
  Result<std::vector<std::byte>> second = serialize_qdr_document(document);
  ASSERT_TRUE(second.ok()) << second.error_message();
  EXPECT_EQ(first.value(), second.value());
  EXPECT_TRUE(has_magic(first.value(), "QDRM"));
  EXPECT_FALSE(has_magic(first.value(), "{"));

  Result<QdrDocumentDto> decoded = deserialize_qdr_document(first.value());
  ASSERT_TRUE(decoded.ok()) << decoded.error_message();
  ASSERT_EQ(decoded.value().materials.size(), 1U);
  ASSERT_EQ(decoded.value().objects.size(), 1U);
  EXPECT_EQ(decoded.value().materials.front().id, material_id);
  EXPECT_EQ(decoded.value().materials.front().name, material_name);
  EXPECT_EQ(decoded.value().objects.front().id, object_id);
  EXPECT_EQ(decoded.value().objects.front().material, material_id);
  EXPECT_TRUE(decoded.value().objects.front().selected);
  EXPECT_EQ(decoded.value().objects.front().name, object_name);
  EXPECT_EQ(decoded.value().active_object, object_id);
  EXPECT_EQ(decoded.value().selection_mode, QdrSelectionMode::Edge);
  EXPECT_EQ(decoded.value().content_revision, 101U);
  EXPECT_EQ(decoded.value().selection_revision, 202U);

  const PolygonDocument &decoded_polygon = decoded.value().objects.front().document;
  EXPECT_EQ(decoded_polygon.vertex_count(), polygon.vertex_count());
  EXPECT_EQ(decoded_polygon.face_count(), polygon.face_count());
  EXPECT_EQ(decoded_polygon.content_revision(), polygon.content_revision());
  EXPECT_EQ(decoded_polygon.selection_revision(), polygon.selection_revision());
  const PolygonSelectionSnapshot selection = decoded_polygon.selection();
  EXPECT_EQ(selection.kind, SelectionKind::Edge);
  ASSERT_TRUE(selection.has_active);
  EXPECT_EQ(selection.active_edge, polygon.selection().active_edge);
  EXPECT_EQ(selection.edges, polygon.selection().edges);

  Result<std::vector<std::byte>> canonical =
      serialize_qdr_document(decoded.value());
  ASSERT_TRUE(canonical.ok()) << canonical.error_message();
  EXPECT_EQ(canonical.value(), first.value());
}

TEST(ModelingSdkConsumerTests,
     AuthoredTopologyPayloadIsSelectionNeutralAndRoundTripsDeterministically) {
  Result<PolygonDocument> polygon_result = PolygonDocument::make_box({});
  ASSERT_TRUE(polygon_result.ok()) << polygon_result.error_message();
  PolygonDocument polygon = std::move(polygon_result).value();
  ASSERT_TRUE(polygon.select_edge(polygon.edge_ids().front()).ok());

  Result<std::vector<std::byte>> before =
      serialize_qdr_authored_topology(polygon);
  ASSERT_TRUE(before.ok()) << before.error_message();
  ASSERT_TRUE(has_magic(before.value(), "QGEO"));

  ASSERT_TRUE(polygon.select_edge(polygon.edge_ids().back()).ok());
  Result<std::vector<std::byte>> after_selection =
      serialize_qdr_authored_topology(polygon);
  ASSERT_TRUE(after_selection.ok()) << after_selection.error_message();
  EXPECT_EQ(after_selection.value(), before.value());

  Result<PolygonDocument> decoded =
      deserialize_qdr_authored_topology(before.value());
  ASSERT_TRUE(decoded.ok()) << decoded.error_message();
  EXPECT_TRUE(decoded.value().selection().vertices.empty());
  EXPECT_TRUE(decoded.value().selection().edges.empty());
  EXPECT_TRUE(decoded.value().selection().faces.empty());
  EXPECT_FALSE(decoded.value().selection().has_active);
  EXPECT_EQ(decoded.value().content_revision(), 1U);
  EXPECT_EQ(decoded.value().selection_revision(), 0U);

  Result<std::vector<std::byte>> reencoded =
      serialize_qdr_authored_topology(decoded.value());
  ASSERT_TRUE(reencoded.ok()) << reencoded.error_message();
  EXPECT_EQ(reencoded.value(), before.value());

  ASSERT_TRUE(decoded.value().select_all_vertices().ok());
  ASSERT_TRUE(decoded.value().translate_selection({1.0F, 0.0F, 0.0F}).ok());
  Result<std::vector<std::byte>> changed =
      serialize_qdr_authored_topology(decoded.value());
  ASSERT_TRUE(changed.ok()) << changed.error_message();
  EXPECT_NE(changed.value(), before.value());
}

TEST(ModelingSdkConsumerTests,
     BinaryQdrSupportsEmptyDocumentsAndEmptyAuthoredTopology) {
  const QdrDocumentDto empty_document;
  Result<std::vector<std::byte>> encoded_document =
      serialize_qdr_document(empty_document);
  ASSERT_TRUE(encoded_document.ok()) << encoded_document.error_message();
  EXPECT_TRUE(has_magic(encoded_document.value(), "QDRM"));
  Result<QdrDocumentDto> decoded_document =
      deserialize_qdr_document(encoded_document.value());
  ASSERT_TRUE(decoded_document.ok()) << decoded_document.error_message();
  EXPECT_TRUE(decoded_document.value().objects.empty());
  EXPECT_TRUE(decoded_document.value().materials.empty());

  const PolygonDocument empty_polygon;
  Result<std::vector<std::byte>> encoded_topology =
      serialize_qdr_authored_topology(empty_polygon);
  ASSERT_TRUE(encoded_topology.ok()) << encoded_topology.error_message();
  Result<PolygonDocument> decoded_topology =
      deserialize_qdr_authored_topology(encoded_topology.value());
  ASSERT_TRUE(decoded_topology.ok()) << decoded_topology.error_message();
  EXPECT_EQ(decoded_topology.value().vertex_count(), 0U);
  EXPECT_EQ(decoded_topology.value().face_count(), 0U);
}

TEST(ModelingSdkConsumerTests,
     BinaryQdrRejectsLegacyTextTruncationBadVersionsAndMalformedPackedArrays) {
  Result<QdrDocumentDto> legacy = deserialize_qdr_document(
      byte_string(R"json({"format":"quader.qdr","version":3})json"));
  ASSERT_FALSE(legacy.ok());
  ASSERT_FALSE(legacy.error().diagnostics.empty());
  EXPECT_EQ(legacy.error().diagnostics.front().code,
            "qdr.binary.unsupported_format");

  Result<PolygonDocument> polygon_result = PolygonDocument::make_box({});
  ASSERT_TRUE(polygon_result.ok()) << polygon_result.error_message();
  Result<std::vector<std::byte>> topology =
      serialize_qdr_authored_topology(polygon_result.value());
  ASSERT_TRUE(topology.ok()) << topology.error_message();
  ASSERT_GE(topology.value().size(), 56U);
  EXPECT_EQ(std::to_integer<std::uint8_t>(topology.value()[4]), 1U);
  EXPECT_EQ(std::to_integer<std::uint8_t>(topology.value()[5]), 0U);
  EXPECT_EQ(std::to_integer<std::uint8_t>(topology.value()[8]), 56U);

  for (std::size_t size = 0; size < topology.value().size(); ++size) {
    SCOPED_TRACE(size);
    EXPECT_FALSE(deserialize_qdr_authored_topology(
                     std::span<const std::byte>{topology.value().data(), size})
                     .ok());
  }

  std::vector<std::byte> bad_version = topology.value();
  write_u32_le(bad_version, 4, 2U);
  EXPECT_FALSE(deserialize_qdr_authored_topology(bad_version).ok());

  std::vector<std::byte> huge_vertex_count = topology.value();
  write_u32_le(huge_vertex_count, 32,
               std::numeric_limits<std::uint32_t>::max());
  EXPECT_FALSE(deserialize_qdr_authored_topology(huge_vertex_count).ok());

  std::vector<std::byte> nonfinite_position = topology.value();
  const std::size_t positions_offset =
      56U + polygon_result.value().vertex_count() * 4U;
  write_u32_le(nonfinite_position, positions_offset, 0x7FC00000U);
  EXPECT_FALSE(deserialize_qdr_authored_topology(nonfinite_position).ok());

  std::vector<std::byte> bad_face_offsets = topology.value();
  const std::size_t face_offsets_offset =
      56U + polygon_result.value().vertex_count() * 16U +
      polygon_result.value().face_count() * 4U;
  write_u32_le(bad_face_offsets, face_offsets_offset + 4U, 0U);
  EXPECT_FALSE(deserialize_qdr_authored_topology(bad_face_offsets).ok());

  std::vector<std::byte> trailing = topology.value();
  trailing.push_back(std::byte{0});
  EXPECT_FALSE(deserialize_qdr_authored_topology(trailing).ok());

  Result<std::vector<std::byte>> empty_document =
      serialize_qdr_document(QdrDocumentDto{});
  ASSERT_TRUE(empty_document.ok()) << empty_document.error_message();
  std::vector<std::byte> bad_checksum = empty_document.value();
  bad_checksum[48] ^= std::byte{1};
  EXPECT_FALSE(deserialize_qdr_document(bad_checksum).ok());

  std::uint32_t random_state = 0xC0DEC0DEU;
  for (std::size_t attempt = 0; attempt < 64; ++attempt) {
    std::vector<std::byte> random_bytes(attempt);
    for (std::byte &value : random_bytes) {
      random_state = random_state * 1664525U + 1013904223U;
      value = std::byte{static_cast<std::uint8_t>(random_state >> 24U)};
    }
    EXPECT_FALSE(deserialize_qdr_document(random_bytes).ok());
  }
}

TEST(ModelingSdkConsumerTests,
     BinaryQdrRejectsMismatchedStableIdGenerationsAndDuplicateIds) {
  Result<PolygonDocument> polygon = PolygonDocument::make_box({});
  ASSERT_TRUE(polygon.ok()) << polygon.error_message();

  QdrDocumentDto mismatched_material;
  mismatched_material.materials.push_back(
      {.id = make_id<MaterialTag>(4U, 2U), .name = "Material"});
  mismatched_material.objects.push_back({
      .id = make_id<ObjectTag>(1U, 5U),
      .material = make_id<MaterialTag>(4U, 3U),
      .name = "Object",
      .document = polygon.value(),
  });
  mismatched_material.active_object = mismatched_material.objects.front().id;
  EXPECT_FALSE(serialize_qdr_document(mismatched_material).ok());

  QdrDocumentDto duplicate_objects;
  duplicate_objects.objects.push_back({
      .id = make_id<ObjectTag>(7U, 1U),
      .name = "First",
      .document = polygon.value(),
  });
  duplicate_objects.objects.push_back({
      .id = make_id<ObjectTag>(7U, 2U),
      .name = "Second",
      .document = polygon.value(),
  });
  EXPECT_FALSE(serialize_qdr_document(duplicate_objects).ok());
}

TEST(ModelingSdkConsumerTests,
     UmbrellaHeaderRunsStandaloneDocumentOperationAndQdrRoundTrip) {
  Result<PolygonDocument> document_result =
      PolygonDocument::make_box({.min = {-1.0F, -1.0F, -1.0F},
                                 .max = {1.0F, 1.0F, 1.0F}});
  ASSERT_TRUE(document_result.ok()) << document_result.error_message();

  PolygonDocument document = std::move(document_result).value();
  ASSERT_EQ(document.face_count(), 6U);
  ASSERT_TRUE(document.select_all_faces().ok());

  const std::uint64_t before_revision = document.content_revision();
  Result<OperationResult> translate =
      document.translate_selection({1.0F, 0.0F, 0.0F});
  ASSERT_TRUE(translate.ok()) << translate.error_message();
  EXPECT_TRUE(translate.value().changed);
  EXPECT_GT(document.content_revision(), before_revision);

  Result<MeshPayload> mesh = document.compile_mesh();
  ASSERT_TRUE(mesh.ok()) << mesh.error_message();
  EXPECT_FALSE(mesh.value().vertices.empty());
  EXPECT_FALSE(mesh.value().indices.empty());

  Result<std::vector<std::byte>> qdr =
      serialize_single_document_as_qdr(document);
  ASSERT_TRUE(qdr.ok()) << qdr.error_message();
  Result<QdrDocumentDto> round_trip = deserialize_qdr_document(qdr.value());
  ASSERT_TRUE(round_trip.ok()) << round_trip.error_message();
  ASSERT_EQ(round_trip.value().objects.size(), 1U);
  EXPECT_EQ(round_trip.value().objects.front().document.face_count(), 6U);

  Result<std::string> obj = serialize_obj_mesh(mesh.value(), "SDK Mesh");
  ASSERT_TRUE(obj.ok()) << obj.error_message();
  EXPECT_NE(obj.value().find("o SDK_Mesh"), std::string::npos);
}

TEST(ModelingSdkConsumerTests,
     PolygonCopiesShareTopologyUntilOneCopyMutatesGeometry) {
  Result<PolygonDocument> source_result = PolygonDocument::make_box({});
  ASSERT_TRUE(source_result.ok()) << source_result.error_message();
  PolygonDocument source = std::move(source_result).value();
  PolygonDocument copy = source;
  const PolygonContentIdentity shared_identity = source.content_identity();

  ASSERT_TRUE(shared_identity.valid());
  EXPECT_TRUE(source.shares_topology_with(copy));
  EXPECT_EQ(copy.content_identity(), shared_identity);
  PolygonDocument no_op = source;
  Result<OperationResult> no_op_translate =
      no_op.translate_selection({1.0F, 0.0F, 0.0F});
  ASSERT_TRUE(no_op_translate.ok()) << no_op_translate.error_message();
  EXPECT_FALSE(no_op_translate.value().changed);
  EXPECT_TRUE(source.shares_topology_with(no_op));
  EXPECT_EQ(no_op.content_identity(), shared_identity);

  ASSERT_TRUE(copy.select_face(copy.face_ids().front()).ok());
  EXPECT_TRUE(source.shares_topology_with(copy));
  EXPECT_EQ(copy.content_identity(), shared_identity);
  EXPECT_TRUE(source.selection().faces.empty());
  EXPECT_EQ(copy.selection().faces.size(), 1U);

  Result<OperationResult> translated =
      copy.translate_selection({2.0F, 0.0F, 0.0F});
  ASSERT_TRUE(translated.ok()) << translated.error_message();
  ASSERT_TRUE(translated.value().changed);
  EXPECT_FALSE(source.shares_topology_with(copy));
  EXPECT_EQ(source.content_identity(), shared_identity);
  EXPECT_NE(copy.content_identity(), shared_identity);
  EXPECT_EQ(source.vertex_count(), copy.vertex_count());

  PolygonDocument assigned;
  assigned = source;
  EXPECT_TRUE(assigned.shares_topology_with(source));
  ASSERT_TRUE(assigned.select_face(assigned.face_ids().back()).ok());
  EXPECT_TRUE(assigned.shares_topology_with(source));
  EXPECT_TRUE(source.selection().faces.empty());
}

TEST(ModelingSdkConsumerTests, GoldenBoxMeshFixtureStaysDeterministic) {
  Result<PolygonDocument> document_result =
      PolygonDocument::make_box({.min = {0.0F, 0.0F, 0.0F},
                                 .max = {2.0F, 3.0F, 4.0F}});
  ASSERT_TRUE(document_result.ok()) << document_result.error_message();
  PolygonDocument document = std::move(document_result).value();

  EXPECT_EQ(document.vertex_count(), 8U);
  EXPECT_EQ(document.face_count(), 6U);
  ASSERT_TRUE(document.validate().ok());

  Result<MeshPayload> mesh = document.compile_mesh();
  ASSERT_TRUE(mesh.ok()) << mesh.error_message();
  EXPECT_EQ(mesh.value().indices.size(), 36U);
  ASSERT_FALSE(mesh.value().primitives.empty());
  EXPECT_EQ(mesh.value().primitives.front().index_offset, 0U);
  std::uint32_t covered_indices = 0;
  for (const MeshPrimitivePayload &primitive : mesh.value().primitives) {
    covered_indices += primitive.index_count;
  }
  EXPECT_EQ(covered_indices, 36U);

  const std::optional<SdkMeshBounds> bounds = mesh_bounds(mesh.value());
  ASSERT_TRUE(bounds.has_value());
  expect_vec3_near(bounds->min, {0.0F, 0.0F, 0.0F});
  expect_vec3_near(bounds->max, {2.0F, 3.0F, 4.0F});

  Result<std::vector<std::byte>> qdr =
      serialize_single_document_as_qdr(document);
  ASSERT_TRUE(qdr.ok()) << qdr.error_message();
  Result<QdrDocumentDto> round_trip = deserialize_qdr_document(qdr.value());
  ASSERT_TRUE(round_trip.ok()) << round_trip.error_message();
  ASSERT_EQ(round_trip.value().objects.size(), 1U);
  EXPECT_EQ(round_trip.value().objects.front().document.vertex_count(), 8U);
  EXPECT_EQ(round_trip.value().objects.front().document.face_count(), 6U);
}

TEST(ModelingSdkConsumerTests,
     BoxAggregateInitializationRetainsThePreSubdivisionFieldOrder) {
  const MaterialId material = make_id<MaterialTag>(11U);
  const BoxSpec spec{{-1.0F, -2.0F, -3.0F},
                     {1.0F, 2.0F, 3.0F}, material};
  const BoxOptions options{"Legacy Box",
                           {-1.0F, -2.0F, -3.0F},
                           {1.0F, 2.0F, 3.0F}, material};

  EXPECT_EQ(spec.material, material);
  EXPECT_EQ(options.material, material);
  EXPECT_EQ(spec.x_subdivisions, 1);
  EXPECT_EQ(options.x_subdivisions, 1);
}

TEST(ModelingSdkConsumerTests,
     PrimitiveFactoriesHonorDensityCapsHemisphereMaterialAndWinding) {
  const MaterialId material = make_id<MaterialTag>(7U);

  Result<PolygonDocument> box_result = PolygonDocument::make_box({
      .min = {0.0F, 0.0F, 0.0F},
      .max = {2.0F, 3.0F, 4.0F},
      .material = material,
      .x_subdivisions = 2,
      .y_subdivisions = 3,
      .z_subdivisions = 4,
  });
  ASSERT_TRUE(box_result.ok()) << box_result.error_message();
  PolygonDocument box = std::move(box_result).value();
  EXPECT_EQ(box.vertex_count(), 54U);
  EXPECT_EQ(box.face_count(), 52U);
  Result<MeshPayload> box_mesh = box.compile_mesh();
  ASSERT_TRUE(box_mesh.ok()) << box_mesh.error_message();
  const std::optional<SdkMeshBounds> box_bounds = mesh_bounds(box_mesh.value());
  ASSERT_TRUE(box_bounds.has_value());
  expect_vec3_near(box_bounds->min, {0.0F, 0.0F, 0.0F});
  expect_vec3_near(box_bounds->max, {2.0F, 3.0F, 4.0F});

  Result<AuthoredPolygonPayload> box_payload = box.authored_payload();
  ASSERT_TRUE(box_payload.ok()) << box_payload.error_message();
  EXPECT_TRUE(std::ranges::all_of(
      box_payload.value().faces,
      [material](const AuthoredPolygonFacePayload &face) {
        return face.material == material;
      }));

  Result<PolygonDocument> flipped_box = PolygonDocument::make_box({
      .min = {0.0F, 0.0F, 0.0F},
      .max = {2.0F, 3.0F, 4.0F},
      .material = material,
      .x_subdivisions = 2,
      .y_subdivisions = 3,
      .z_subdivisions = 4,
      .flip_faces = true,
  });
  ASSERT_TRUE(flipped_box.ok()) << flipped_box.error_message();
  expect_reversed_face_winding(box, flipped_box.value());

  Result<PolygonDocument> cylinder_result = PolygonDocument::make_cylinder({
      .width = 4.0F,
      .height = 6.0F,
      .depth = 2.0F,
      .side_count = 8,
      .capped = true,
      .material = material,
  });
  ASSERT_TRUE(cylinder_result.ok()) << cylinder_result.error_message();
  PolygonDocument cylinder = std::move(cylinder_result).value();
  EXPECT_EQ(cylinder.vertex_count(), 16U);
  EXPECT_EQ(cylinder.face_count(), 10U);
  Result<MeshPayload> cylinder_mesh = cylinder.compile_mesh();
  ASSERT_TRUE(cylinder_mesh.ok()) << cylinder_mesh.error_message();
  const std::optional<SdkMeshBounds> cylinder_bounds =
      mesh_bounds(cylinder_mesh.value());
  ASSERT_TRUE(cylinder_bounds.has_value());
  expect_vec3_near(cylinder_bounds->min, {-2.0F, -3.0F, -1.0F});
  expect_vec3_near(cylinder_bounds->max, {2.0F, 3.0F, 1.0F});

  Result<PolygonDocument> open_cylinder = PolygonDocument::make_cylinder({
      .width = 4.0F,
      .height = 6.0F,
      .depth = 2.0F,
      .side_count = 8,
      .capped = false,
      .material = material,
  });
  ASSERT_TRUE(open_cylinder.ok()) << open_cylinder.error_message();
  EXPECT_EQ(open_cylinder.value().face_count(), 8U);

  Result<PolygonDocument> flipped_cylinder =
      PolygonDocument::make_cylinder({
          .width = 4.0F,
          .height = 6.0F,
          .depth = 2.0F,
          .side_count = 8,
          .capped = true,
          .flip_faces = true,
          .material = material,
      });
  ASSERT_TRUE(flipped_cylinder.ok()) << flipped_cylinder.error_message();
  expect_reversed_face_winding(cylinder, flipped_cylinder.value());

  Result<PolygonDocument> sphere_result = PolygonDocument::make_sphere({
      .width = 4.0F,
      .height = 6.0F,
      .depth = 2.0F,
      .radial_segments = 8,
      .rings = 4,
      .material = material,
  });
  ASSERT_TRUE(sphere_result.ok()) << sphere_result.error_message();
  PolygonDocument sphere = std::move(sphere_result).value();
  EXPECT_EQ(sphere.vertex_count(), 26U);
  EXPECT_EQ(sphere.face_count(), 32U);
  Result<MeshPayload> sphere_mesh = sphere.compile_mesh();
  ASSERT_TRUE(sphere_mesh.ok()) << sphere_mesh.error_message();
  const std::optional<SdkMeshBounds> sphere_bounds =
      mesh_bounds(sphere_mesh.value());
  ASSERT_TRUE(sphere_bounds.has_value());
  expect_vec3_near(sphere_bounds->min, {-2.0F, -3.0F, -1.0F});
  expect_vec3_near(sphere_bounds->max, {2.0F, 3.0F, 1.0F});

  Result<PolygonDocument> hemisphere = PolygonDocument::make_sphere({
      .width = 4.0F,
      .height = 6.0F,
      .depth = 2.0F,
      .radial_segments = 8,
      .rings = 4,
      .hemisphere = true,
      .material = material,
  });
  ASSERT_TRUE(hemisphere.ok()) << hemisphere.error_message();
  EXPECT_EQ(hemisphere.value().vertex_count(), 33U);
  EXPECT_EQ(hemisphere.value().face_count(), 33U);

  Result<PolygonDocument> flipped_sphere = PolygonDocument::make_sphere({
      .width = 4.0F,
      .height = 6.0F,
      .depth = 2.0F,
      .radial_segments = 8,
      .rings = 4,
      .flip_faces = true,
      .material = material,
  });
  ASSERT_TRUE(flipped_sphere.ok()) << flipped_sphere.error_message();
  expect_reversed_face_winding(sphere, flipped_sphere.value());
}

TEST(ModelingSdkConsumerTests,
     ModelingApiCreatesEveryPrimitiveAndRejectsInvalidSettingsAtomically) {
  ModelingApi quader = ModelingApi::create();

  MeshHandle box = quader.create_box({
      .name = "Subdivided Box",
      .x_subdivisions = 2,
      .y_subdivisions = 3,
      .z_subdivisions = 4,
      .flip_faces = true,
  });
  MeshHandle cylinder = quader.create_cylinder({
      .name = "Open Cylinder",
      .width = 4.0F,
      .height = 6.0F,
      .depth = 2.0F,
      .side_count = 8,
      .capped = false,
  });
  MeshHandle sphere = quader.create_sphere({
      .name = "Hemisphere",
      .width = 4.0F,
      .height = 6.0F,
      .depth = 2.0F,
      .radial_segments = 8,
      .rings = 4,
      .hemisphere = true,
      .flip_faces = true,
  });

  ASSERT_TRUE(box.valid());
  ASSERT_TRUE(cylinder.valid());
  ASSERT_TRUE(sphere.valid());
  EXPECT_EQ(box.summary().name, "Subdivided Box");
  EXPECT_EQ(cylinder.summary().name, "Open Cylinder");
  EXPECT_EQ(sphere.summary().name, "Hemisphere");
  EXPECT_EQ(box.payloads().authored_polygon().faces.size(), 52U);
  EXPECT_EQ(cylinder.payloads().authored_polygon().faces.size(), 8U);
  EXPECT_EQ(sphere.payloads().authored_polygon().faces.size(), 33U);

  const std::size_t before_invalid = quader.mesh_summaries().size();
  Result<MeshHandle> invalid_cylinder =
      quader.checked().create_cylinder({.side_count = 2});
  ASSERT_FALSE(invalid_cylinder.ok());
  EXPECT_EQ(invalid_cylinder.error().code, ErrorCode::InvalidArgument);
  Result<MeshHandle> invalid_sphere =
      quader.checked().create_sphere({.rings = 1});
  ASSERT_FALSE(invalid_sphere.ok());
  EXPECT_EQ(invalid_sphere.error().code, ErrorCode::InvalidArgument);
  Result<MeshHandle> invalid_box =
      quader.checked().create_box({.x_subdivisions = 0});
  ASSERT_FALSE(invalid_box.ok());
  EXPECT_EQ(invalid_box.error().code, ErrorCode::InvalidArgument);
  EXPECT_EQ(quader.mesh_summaries().size(), before_invalid);
}

TEST(ModelingSdkConsumerTests, RejectsMalformedBoundsAndStaleIds) {
  Result<PolygonDocument> invalid_box =
      PolygonDocument::make_box({.min = {1.0F, 0.0F, 0.0F},
                                 .max = {1.0F, 1.0F, 1.0F}});
  ASSERT_FALSE(invalid_box.ok());
  EXPECT_EQ(invalid_box.error().code, ErrorCode::InvalidArgument);

  Result<PolygonDocument> document_result = PolygonDocument::make_box({});
  ASSERT_TRUE(document_result.ok()) << document_result.error_message();
  PolygonDocument document = std::move(document_result).value();

  Result<OperationResult> missing_face =
      document.select_face(make_id<FaceTag>(999U));
  ASSERT_FALSE(missing_face.ok());
  EXPECT_EQ(missing_face.error().code, ErrorCode::InvalidId);

  const std::vector<FaceId> faces = document.face_ids();
  ASSERT_FALSE(faces.empty());
  Result<OperationResult> stale_face =
      document.select_face(make_id<FaceTag>(faces.front().index, 2U));
  ASSERT_FALSE(stale_face.ok());
  EXPECT_EQ(stale_face.error().code, ErrorCode::StaleId);

  const std::vector<VertexId> vertices = document.vertex_ids();
  ASSERT_GE(vertices.size(), 2U);
  Result<OperationResult> stale_edge = document.select_edge(make_edge_key(
      make_id<VertexTag>(vertices[0].index, 2U), vertices[1]));
  ASSERT_FALSE(stale_edge.ok());
  EXPECT_EQ(stale_edge.error().code, ErrorCode::StaleId);
}

TEST(ModelingSdkConsumerTests,
     DeterministicSeededRoundTripsPreserveTranslatedBoxes) {
  std::uint32_t state = 0x12345678U;
  const auto next_unit = [&state]() {
    state = state * 1664525U + 1013904223U;
    return static_cast<float>((state >> 8U) & 0xffU) / 255.0F;
  };

  for (int iteration = 0; iteration < 16; ++iteration) {
    const Vec3 min{
        next_unit() * 2.0F,
        next_unit() * 2.0F,
        next_unit() * 2.0F,
    };
    const Vec3 size{
        0.25F + next_unit() * 3.0F,
        0.25F + next_unit() * 3.0F,
        0.25F + next_unit() * 3.0F,
    };
    Result<PolygonDocument> document_result =
        PolygonDocument::make_box({.min = min,
                                   .max = {min.x + size.x, min.y + size.y,
                                           min.z + size.z}});
    ASSERT_TRUE(document_result.ok()) << document_result.error_message();
    PolygonDocument document = std::move(document_result).value();
    ASSERT_TRUE(document.select_all_faces().ok());

    const Vec3 delta{
        next_unit() - 0.5F,
        next_unit() - 0.5F,
        next_unit() - 0.5F,
    };
    Result<OperationResult> translate = document.translate_selection(delta);
    ASSERT_TRUE(translate.ok()) << translate.error_message();
    ASSERT_TRUE(document.validate().ok());

    Result<MeshPayload> mesh = document.compile_mesh();
    ASSERT_TRUE(mesh.ok()) << mesh.error_message();
    ASSERT_FALSE(mesh.value().vertices.empty());
    ASSERT_FALSE(mesh.value().indices.empty());

    Result<std::vector<std::byte>> qdr =
        serialize_single_document_as_qdr(document);
    ASSERT_TRUE(qdr.ok()) << qdr.error_message();
    Result<QdrDocumentDto> round_trip = deserialize_qdr_document(qdr.value());
    ASSERT_TRUE(round_trip.ok()) << round_trip.error_message();
    ASSERT_EQ(round_trip.value().objects.size(), 1U);
    EXPECT_EQ(round_trip.value().objects.front().document.face_count(),
              document.face_count());
    EXPECT_TRUE(round_trip.value().objects.front().document.validate().ok());

    Result<std::string> obj = serialize_obj_mesh(mesh.value(), "Seeded Box");
    ASSERT_TRUE(obj.ok()) << obj.error_message();
    EXPECT_NE(obj.value().find("v "), std::string::npos);
  }
}

TEST(ModelingSdkConsumerTests, SessionSelectionUndoRedoUsesSdkTypesOnly) {
  Result<PolygonDocument> document_result = PolygonDocument::make_box({});
  ASSERT_TRUE(document_result.ok()) << document_result.error_message();

  ModelingSession session;
  Result<ObjectId> object_id =
      session.add_document(std::move(document_result).value(), "Box");
  ASSERT_TRUE(object_id.ok()) << object_id.error_message();

  const PolygonDocument *document = session.document(object_id.value());
  ASSERT_NE(document, nullptr);
  const std::vector<FaceId> faces = document->face_ids();
  ASSERT_FALSE(faces.empty());

  ASSERT_TRUE(session.select_face(object_id.value(), faces.front()).ok());
  Result<OperationResult> translate =
      session.translate_selection({0.0F, 2.0F, 0.0F});
  ASSERT_TRUE(translate.ok()) << translate.error_message();
  EXPECT_TRUE(translate.value().changed);
  EXPECT_TRUE(session.can_undo());

  Result<OperationResult> undo = session.undo();
  ASSERT_TRUE(undo.ok()) << undo.error_message();
  EXPECT_TRUE(session.can_redo());

  Result<OperationResult> redo = session.redo();
  ASSERT_TRUE(redo.ok()) << redo.error_message();
}

TEST(ModelingSdkConsumerTests,
     SessionFailedFaceSelectionPreservesHistoryAndChangeCursor) {
  Result<PolygonDocument> document_result = PolygonDocument::make_box({});
  ASSERT_TRUE(document_result.ok()) << document_result.error_message();

  ModelingSession session;
  Result<ObjectId> object =
      session.add_document(std::move(document_result).value(), "Box");
  ASSERT_TRUE(object.ok()) << object.error_message();
  ASSERT_TRUE(session.rename_object(object.value(), "Renamed").ok());
  ASSERT_TRUE(session.undo().ok());
  ASSERT_TRUE(session.can_redo());

  const std::size_t undo_depth = session.undo_depth();
  const std::uint64_t content_revision = session.content_revision();
  const std::uint64_t selection_revision = session.selection_revision();
  Result<OperationResult> failed =
      session.select_face(object.value(), make_id<FaceTag>(999999U));
  ASSERT_FALSE(failed.ok());

  EXPECT_EQ(session.undo_depth(), undo_depth);
  EXPECT_TRUE(session.can_redo());
  EXPECT_EQ(session.content_revision(), content_revision);
  EXPECT_EQ(session.selection_revision(), selection_revision);
  ASSERT_EQ(session.objects().size(), 1U);
  EXPECT_FALSE(session.objects().front().selected);
  EXPECT_TRUE(session.changes_since(content_revision).objects.empty());
}

TEST(ModelingSdkConsumerTests,
     SessionUnchangedTranslatePreservesHistoryAndRedo) {
  Result<PolygonDocument> document_result = PolygonDocument::make_box({});
  ASSERT_TRUE(document_result.ok()) << document_result.error_message();

  ModelingSession session;
  Result<ObjectId> object =
      session.add_document(std::move(document_result).value(), "Box");
  ASSERT_TRUE(object.ok()) << object.error_message();
  ASSERT_TRUE(session.select_object(object.value()).ok());
  ASSERT_TRUE(session.rename_object(object.value(), "Renamed").ok());
  ASSERT_TRUE(session.undo().ok());
  ASSERT_TRUE(session.can_redo());

  const std::size_t undo_depth = session.undo_depth();
  const std::uint64_t content_revision = session.content_revision();
  Result<OperationResult> unchanged =
      session.translate_selection({1.0F, 0.0F, 0.0F});
  ASSERT_TRUE(unchanged.ok()) << unchanged.error_message();
  EXPECT_FALSE(unchanged.value().changed);
  EXPECT_EQ(session.undo_depth(), undo_depth);
  EXPECT_TRUE(session.can_redo());
  EXPECT_EQ(session.content_revision(), content_revision);
  EXPECT_TRUE(session.changes_since(content_revision).objects.empty());
}

TEST(ModelingSdkConsumerTests,
     SessionUnchangedAndFailedCommitsDoNotSelectOrPublish) {
  Result<PolygonDocument> document_result = PolygonDocument::make_box({});
  ASSERT_TRUE(document_result.ok()) << document_result.error_message();

  ModelingSession session;
  Result<ObjectId> object =
      session.add_document(std::move(document_result).value(), "Box");
  ASSERT_TRUE(object.ok()) << object.error_message();
  const std::size_t undo_depth = session.undo_depth();
  const std::uint64_t content_revision = session.content_revision();

  Result<PolygonDocument> unchanged_document =
      session.document_copy(object.value());
  ASSERT_TRUE(unchanged_document.ok()) << unchanged_document.error_message();
  OperationResult unchanged_receipt;
  Result<OperationResult> unchanged = session.commit_document(
      object.value(), std::move(unchanged_document).value(),
      unchanged_receipt);
  ASSERT_TRUE(unchanged.ok()) << unchanged.error_message();
  EXPECT_FALSE(unchanged.value().changed);

  Result<PolygonDocument> failed_document = session.document_copy(object.value());
  ASSERT_TRUE(failed_document.ok()) << failed_document.error_message();
  OperationResult failed_receipt;
  failed_receipt.success = false;
  failed_receipt.changed = true;
  failed_receipt.error_code = ErrorCode::ValidationFailed;
  Result<OperationResult> failed = session.commit_document(
      object.value(), std::move(failed_document).value(), failed_receipt);
  ASSERT_TRUE(failed.ok()) << failed.error_message();
  EXPECT_FALSE(failed.value().success);

  EXPECT_EQ(session.undo_depth(), undo_depth);
  EXPECT_EQ(session.content_revision(), content_revision);
  ASSERT_EQ(session.objects().size(), 1U);
  EXPECT_FALSE(session.objects().front().selected);
  EXPECT_TRUE(session.changes_since(content_revision).objects.empty());
}

TEST(ModelingSdkConsumerTests,
     SessionIndependentDocumentReplacementPublishesSelectionChange) {
  Result<PolygonDocument> original_result = PolygonDocument::make_box({});
  Result<PolygonDocument> replacement_result = PolygonDocument::make_box({});
  ASSERT_TRUE(original_result.ok()) << original_result.error_message();
  ASSERT_TRUE(replacement_result.ok()) << replacement_result.error_message();

  PolygonDocument original = std::move(original_result).value();
  PolygonDocument replacement = std::move(replacement_result).value();
  const std::vector<FaceId> original_faces = original.face_ids();
  const std::vector<FaceId> replacement_faces = replacement.face_ids();
  ASSERT_GE(original_faces.size(), 2U);
  ASSERT_GE(replacement_faces.size(), 2U);
  ASSERT_TRUE(original.select_face(original_faces[0]).ok());
  ASSERT_TRUE(replacement.select_face(replacement_faces[1]).ok());
  ASSERT_EQ(original.selection_revision(), replacement.selection_revision());

  ModelingSession session;
  Result<ObjectId> object =
      session.add_document(std::move(original), "Original");
  ASSERT_TRUE(object.ok()) << object.error_message();
  ASSERT_TRUE(session.select_object(object.value()).ok());
  const PolygonDocument *before = session.document(object.value());
  ASSERT_NE(before, nullptr);
  ASSERT_EQ(before->selection_revision(), replacement.selection_revision());

  const std::uint64_t content_revision = session.content_revision();
  const std::uint64_t selection_revision = session.selection_revision();
  OperationResult receipt;
  receipt.changed = true;
  receipt.dirty.geometry = true;
  Result<OperationResult> committed = session.commit_document(
      object.value(), std::move(replacement), receipt);
  ASSERT_TRUE(committed.ok()) << committed.error_message();

  const SessionChangeSnapshot changes =
      session.changes_since(content_revision);
  const auto change = std::ranges::find_if(
      changes.objects, [&](const SessionObjectChange &candidate) {
        return candidate.id == object.value();
      });
  ASSERT_NE(change, changes.objects.end());
  EXPECT_TRUE(change->dirty.geometry);
  EXPECT_TRUE(change->dirty.selection);
  EXPECT_TRUE(change->dirty.overlays);
  EXPECT_GT(session.selection_revision(), selection_revision);

  const PolygonDocument *after = session.document(object.value());
  ASSERT_NE(after, nullptr);
  const PolygonSelectionSnapshot selected = after->selection();
  ASSERT_EQ(selected.faces.size(), 1U);
  EXPECT_EQ(selected.faces.front(), replacement_faces[1]);
}

TEST(ModelingSdkConsumerTests,
     SessionActiveObjectOnlyChangeIsUndoableAndPublished) {
  Result<PolygonDocument> first_document = PolygonDocument::make_box({});
  Result<PolygonDocument> second_document = PolygonDocument::make_box({});
  ASSERT_TRUE(first_document.ok()) << first_document.error_message();
  ASSERT_TRUE(second_document.ok()) << second_document.error_message();

  ModelingSession session;
  Result<ObjectId> first =
      session.add_document(std::move(first_document).value(), "First");
  Result<ObjectId> second =
      session.add_document(std::move(second_document).value(), "Second");
  ASSERT_TRUE(first.ok()) << first.error_message();
  ASSERT_TRUE(second.ok()) << second.error_message();
  const std::array<ObjectId, 2> both{first.value(), second.value()};
  ASSERT_TRUE(
      session.apply_object_selection(both, SelectionEdit::Replace).ok());

  const std::size_t undo_depth = session.undo_depth();
  const std::uint64_t content_revision = session.content_revision();
  const std::uint64_t selection_revision = session.selection_revision();
  const std::array<ObjectId, 1> only_second{second.value()};
  Result<OperationResult> changed =
      session.apply_object_selection(only_second, SelectionEdit::Add);
  ASSERT_TRUE(changed.ok()) << changed.error_message();
  EXPECT_TRUE(changed.value().changed);
  EXPECT_EQ(session.undo_depth(), undo_depth + 1U);
  EXPECT_GT(session.content_revision(), content_revision);
  EXPECT_GT(session.selection_revision(), selection_revision);

  const SessionChangeSnapshot changes =
      session.changes_since(content_revision);
  const auto first_change = std::ranges::find_if(
      changes.objects, [&](const SessionObjectChange &change) {
        return change.id == first.value();
      });
  const auto second_change = std::ranges::find_if(
      changes.objects, [&](const SessionObjectChange &change) {
        return change.id == second.value();
      });
  ASSERT_NE(first_change, changes.objects.end());
  ASSERT_NE(second_change, changes.objects.end());
  EXPECT_TRUE(first_change->dirty.selection);
  EXPECT_TRUE(first_change->dirty.overlays);
  EXPECT_TRUE(second_change->dirty.selection);
  EXPECT_TRUE(second_change->dirty.overlays);
  EXPECT_TRUE(session.undo().ok());
}

TEST(ModelingSdkConsumerTests, SessionPreviewCommitAndCancelStayHeadless) {
  Result<PolygonDocument> document_result = PolygonDocument::make_box({});
  ASSERT_TRUE(document_result.ok()) << document_result.error_message();

  ModelingSession session;
  Result<ObjectId> object_id =
      session.add_document(std::move(document_result).value(), "Preview Box");
  ASSERT_TRUE(object_id.ok()) << object_id.error_message();

  const PolygonDocument *document = session.document(object_id.value());
  ASSERT_NE(document, nullptr);
  const std::vector<FaceId> faces = document->face_ids();
  ASSERT_FALSE(faces.empty());
  ASSERT_TRUE(session.select_face(object_id.value(), faces.front()).ok());

  document = session.document(object_id.value());
  ASSERT_NE(document, nullptr);
  const std::uint64_t committed_revision = document->content_revision();

  Result<OperationResult> begin =
      session.begin_translate_preview(object_id.value(), {0.0F, 1.0F, 0.0F});
  ASSERT_TRUE(begin.ok()) << begin.error_message();
  EXPECT_TRUE(session.preview_active());
  EXPECT_TRUE(begin.value().dirty.overlays);

  const PreviewTransactionSummary summary = session.preview_summary();
  EXPECT_TRUE(summary.active);
  EXPECT_EQ(summary.object_id, object_id.value());
  EXPECT_EQ(summary.operation_id, "modeling.translate_selection");
  EXPECT_GT(summary.content_revision, committed_revision);

  Result<ToolPreviewPayload> preview_payload = session.preview_payload();
  ASSERT_TRUE(preview_payload.ok()) << preview_payload.error_message();
  EXPECT_TRUE(preview_payload.value().valid);
  EXPECT_FALSE(preview_payload.value().mesh.vertices.empty());
  EXPECT_FALSE(preview_payload.value().overlay.records.empty());

  document = session.document(object_id.value());
  ASSERT_NE(document, nullptr);
  EXPECT_EQ(document->content_revision(), committed_revision);

  Result<OperationResult> update =
      session.update_translate_preview({0.0F, 2.0F, 0.0F});
  ASSERT_TRUE(update.ok()) << update.error_message();
  EXPECT_EQ(session.preview_summary().delta.y, 2.0F);

  Result<OperationResult> commit = session.commit_preview();
  ASSERT_TRUE(commit.ok()) << commit.error_message();
  EXPECT_FALSE(session.preview_active());
  document = session.document(object_id.value());
  ASSERT_NE(document, nullptr);
  EXPECT_GT(document->content_revision(), committed_revision);
  EXPECT_TRUE(session.can_undo());

  const std::uint64_t committed_preview_revision = document->content_revision();
  ASSERT_TRUE(session.undo().ok());
  ASSERT_TRUE(session.begin_translate_preview(object_id.value(),
                                             {1.0F, 0.0F, 0.0F})
                  .ok());
  ASSERT_TRUE(session.cancel_preview().ok());
  EXPECT_FALSE(session.preview_active());
  document = session.document(object_id.value());
  ASSERT_NE(document, nullptr);
  EXPECT_LT(document->content_revision(), committed_preview_revision);
}

TEST(ModelingSdkConsumerTests,
     ModelingApiCreatesSelectsBevelsAndSerializesWithoutManualValueUnwrap) {
  ModelingApi quader = ModelingApi::create();

  MeshHandle mesh = quader.create_box({.name = "Box"});
  ASSERT_TRUE(mesh.valid());
  EXPECT_EQ(mesh.summary().name, "Box");

  EdgeSelection edges = mesh.edges().all();
  ASSERT_FALSE(edges.empty());
  OperationReceipt selected = edges.replace();
  ASSERT_TRUE(selected.success);

  PreviewHandle bevel = edges.bevel({.width = 0.05F, .segments = 1});
  OperationReceipt bevel_receipt = bevel.commit("Bevel Box");
  ASSERT_TRUE(bevel_receipt.success) << bevel_receipt.error.message;
  EXPECT_TRUE(bevel_receipt.changed);

  MeshPayload mesh_payload = mesh.payloads().compile_mesh();
  EXPECT_FALSE(mesh_payload.vertices.empty());
  EXPECT_FALSE(mesh_payload.indices.empty());

  std::vector<std::byte> qdr = quader.io().serialize_qdr(mesh.id());
  ASSERT_FALSE(qdr.empty());
  MeshHandle imported = quader.io().deserialize_qdr_object(qdr, "Imported");
  ASSERT_TRUE(imported.valid());
  EXPECT_EQ(imported.summary().name, "Imported");
}

TEST(ModelingSdkConsumerTests, ModelingSelectionContractIsSharedByTypedSelections) {
  ModelingApi quader = ModelingApi::create();
  MeshHandle mesh = quader.create_box({.name = "Selection Box"});
  ASSERT_TRUE(mesh.valid());

  FaceSelection material_faces = mesh.faces().by_material_slot(0);
  ASSERT_EQ(material_faces.kind(), SelectionKind::Face);
  ASSERT_FALSE(material_faces.empty());
  EXPECT_TRUE(material_faces.is_face_selection());
  EXPECT_FALSE(material_faces.is_edge_selection());

  OperationReceipt selected = material_faces.replace();
  ASSERT_TRUE(selected.success);
  EXPECT_EQ(mesh.faces().selected().count(), material_faces.count());

  OperationReceipt smooth = mesh.faces().selected().shade_smooth();
  ASSERT_TRUE(smooth.success);
  EXPECT_TRUE(smooth.changed);
}

TEST(ModelingSdkConsumerTests, ModelingCheckedAndStoreLastErrorPoliciesWork) {
  ModelingApi throwing_api = ModelingApi::create();
  Result<MeshHandle> checked =
      throwing_api.checked().create_box({.name = "Checked"});
  ASSERT_TRUE(checked.ok()) << checked.error_message();
  EXPECT_TRUE(checked.or_throw().valid());

  ModelingApi stored = ModelingApi::create({
      .error_policy = ErrorPolicy::StoreLastError,
  });
  MeshHandle invalid = stored.create_box({.min = {1.0F, 0.0F, 0.0F},
                                          .max = {1.0F, 1.0F, 1.0F}});
  EXPECT_FALSE(invalid.valid());
  ASSERT_NE(stored.last_error(), nullptr);
  EXPECT_EQ(stored.last_error()->code, ErrorCode::InvalidArgument);
}

TEST(ModelingSdkConsumerTests,
     CheckedIoReportsCurrentStoreLastErrorSerializationFailures) {
  ModelingApi quader = ModelingApi::create({
      .error_policy = ErrorPolicy::StoreLastError,
  });
  MeshHandle live = quader.create_box({.name = "Live"});
  ASSERT_TRUE(live.valid());

  const ObjectId invalid = make_id<ObjectTag>(999999U);
  Result<std::vector<std::byte>> invalid_qdr =
      quader.checked().io().serialize_qdr(invalid);
  ASSERT_FALSE(invalid_qdr.ok());
  EXPECT_EQ(invalid_qdr.error().code, ErrorCode::InvalidId);
  ASSERT_NE(quader.last_error(), nullptr);
  EXPECT_EQ(quader.last_error()->code, ErrorCode::InvalidId);

  Result<std::vector<std::byte>> valid_qdr =
      quader.checked().io().serialize_qdr(live.id());
  ASSERT_TRUE(valid_qdr.ok()) << valid_qdr.error_message();
  EXPECT_FALSE(valid_qdr.value().empty());
  ASSERT_NE(quader.last_error(), nullptr);
  EXPECT_EQ(quader.last_error()->code, ErrorCode::InvalidId);

  MeshHandle stale = quader.create_box({.name = "Stale"});
  ASSERT_TRUE(stale.valid());
  const ObjectId stale_id = stale.id();
  ASSERT_TRUE(stale.destroy().success);
  Result<std::vector<std::byte>> stale_qdr =
      quader.checked().io().serialize_qdr(stale_id);
  ASSERT_FALSE(stale_qdr.ok());
  EXPECT_EQ(stale_qdr.error().code, ErrorCode::InvalidId);

  Result<std::string> stale_obj =
      quader.checked().io().serialize_obj(stale_id);
  ASSERT_FALSE(stale_obj.ok());
  EXPECT_NE(stale_obj.error().code, ErrorCode::Ok);
  ASSERT_NE(quader.last_error(), nullptr);
  EXPECT_EQ(stale_obj.error().code, quader.last_error()->code);
}

TEST(ModelingSdkConsumerTests,
     CheckedIoRejectsMalformedDocumentUnderStoreLastError) {
  ModelingApi quader = ModelingApi::create({
      .error_policy = ErrorPolicy::StoreLastError,
  });
  const std::array<std::byte, 4> malformed{
      std::byte{0xff},
      std::byte{0x00},
      std::byte{0x7f},
      std::byte{0x01},
  };

  Result<std::vector<MeshHandle>> imported =
      quader.checked().io().deserialize_qdr_document(malformed);
  ASSERT_FALSE(imported.ok());
  EXPECT_NE(imported.error().code, ErrorCode::Ok);
  ASSERT_NE(quader.last_error(), nullptr);
  EXPECT_EQ(quader.last_error()->code, imported.error().code);
}

TEST(ModelingSdkConsumerTests,
     CheckedPreviewOperationsReportNewErrorsAndIgnoreStaleErrors) {
  ModelingApi empty = ModelingApi::create({
      .error_policy = ErrorPolicy::StoreLastError,
  });
  Result<PreviewHandle> merge =
      empty.checked().operations().merge_by_distance();
  ASSERT_FALSE(merge.ok());
  EXPECT_EQ(merge.error().code, ErrorCode::UnsupportedOperation);
  Result<PreviewHandle> bevel = empty.checked().operations().bevel_edges();
  ASSERT_FALSE(bevel.ok());
  EXPECT_EQ(bevel.error().code, ErrorCode::UnsupportedOperation);
  Result<PreviewHandle> thicken =
      empty.checked().operations().thicken_faces();
  ASSERT_FALSE(thicken.ok());
  EXPECT_EQ(thicken.error().code, ErrorCode::UnsupportedOperation);

  ModelingApi quader = ModelingApi::create({
      .error_policy = ErrorPolicy::StoreLastError,
  });
  MeshHandle mesh = quader.create_box({.name = "Preview"});
  ASSERT_TRUE(mesh.valid());
  ASSERT_TRUE(mesh.select().replace().success);
  ASSERT_TRUE(mesh.edges().all().replace().success);

  Result<std::vector<std::byte>> prior_failure =
      quader.checked().io().serialize_qdr(make_id<ObjectTag>(999999U));
  ASSERT_FALSE(prior_failure.ok());
  ASSERT_NE(quader.last_error(), nullptr);
  const Error prior_error = *quader.last_error();

  Result<PreviewHandle> valid_preview =
      quader.checked().operations().bevel_edges();
  ASSERT_TRUE(valid_preview.ok()) << valid_preview.error_message();
  EXPECT_TRUE(valid_preview.value().active());
  ASSERT_NE(quader.last_error(), nullptr);
  EXPECT_EQ(quader.last_error()->code, prior_error.code);
  EXPECT_EQ(quader.last_error()->message, prior_error.message);
}

TEST(ModelingSdkConsumerTests,
     ModelingApiObjectLifecycleUsesValueHandlesAndUndoRedo) {
  ModelingApi quader = ModelingApi::create();
  MeshHandle mesh = quader.create_box({.name = "Source"});
  ASSERT_TRUE(mesh.valid());

  OperationReceipt renamed = mesh.rename("Renamed Source");
  ASSERT_TRUE(renamed.success);
  EXPECT_EQ(mesh.summary().name, "Renamed Source");

  MeshHandle duplicate = mesh.duplicate({.offset = {2.0F, 0.0F, 0.0F}});
  ASSERT_TRUE(duplicate.valid());
  EXPECT_EQ(quader.mesh_summaries().size(), 2U);

  const MeshPayload duplicate_mesh = duplicate.payloads().compile_mesh();
  const std::optional<SdkMeshBounds> duplicate_bounds =
      mesh_bounds(duplicate_mesh);
  ASSERT_TRUE(duplicate_bounds.has_value());
  expect_vec3_near(duplicate_bounds->min, {1.0F, -1.0F, -1.0F});
  expect_vec3_near(duplicate_bounds->max, {3.0F, 1.0F, 1.0F});

  OperationReceipt destroyed = duplicate.destroy();
  ASSERT_TRUE(destroyed.success);
  EXPECT_FALSE(duplicate.valid());
  EXPECT_EQ(quader.mesh_summaries().size(), 1U);

  ASSERT_TRUE(quader.can_undo());
  OperationReceipt undone = quader.undo();
  ASSERT_TRUE(undone.success);
  EXPECT_TRUE(duplicate.valid());
  EXPECT_EQ(quader.mesh_summaries().size(), 2U);
}

TEST(ModelingSdkConsumerTests,
     ModelingApiUndoBranchDoesNotRecycleObjectIdentity) {
  ModelingApi quader = ModelingApi::create();
  MeshHandle original = quader.create_box({.name = "Original"});
  ASSERT_TRUE(original.valid());
  const ObjectId original_id = original.id();

  OperationReceipt undone = quader.undo();
  ASSERT_TRUE(undone.success) << undone.error.message;
  EXPECT_FALSE(original.valid());

  MeshHandle replacement = quader.create_box({.name = "Replacement"});
  ASSERT_TRUE(replacement.valid());
  EXPECT_NE(replacement.id(), original_id);
  EXPECT_FALSE(original.valid());
}

TEST(ModelingSdkConsumerTests,
     ModelingApiRedoRestoresOriginalObjectIdentity) {
  ModelingApi quader = ModelingApi::create();
  MeshHandle original = quader.create_box({.name = "Original"});
  ASSERT_TRUE(original.valid());
  const ObjectId original_id = original.id();

  OperationReceipt undone = quader.undo();
  ASSERT_TRUE(undone.success) << undone.error.message;
  EXPECT_FALSE(original.valid());
  ASSERT_TRUE(quader.can_redo());

  OperationReceipt redone = quader.redo();
  ASSERT_TRUE(redone.success) << redone.error.message;
  EXPECT_TRUE(original.valid());
  const std::vector<MeshSummary> summaries = quader.mesh_summaries();
  ASSERT_EQ(summaries.size(), 1U);
  EXPECT_EQ(summaries.front().id, original_id);
}

TEST(ModelingSdkConsumerTests,
     MeshSyncIncludesObjectsAddedAfterHighDocumentRevisions) {
  ModelingApi quader = ModelingApi::create();
  MeshHandle first = quader.create_box({.name = "First"});
  ASSERT_TRUE(first.valid());

  MeshSyncSnapshot initial = quader.mesh_sync().changes_since(0);
  ASSERT_EQ(initial.objects.size(), 1U);
  std::uint64_t cursor = initial.content_revision;

  ASSERT_TRUE(first.faces().all().replace().success);
  for (int edit = 0; edit < 5; ++edit) {
    ASSERT_TRUE(first.transform().translate({0.1F, 0.0F, 0.0F}).success);
  }
  MeshSyncSnapshot edited = quader.mesh_sync().changes_since(cursor);
  ASSERT_GT(edited.content_revision, cursor);
  cursor = edited.content_revision;

  MeshHandle late = quader.create_box({.name = "Late"});
  ASSERT_TRUE(late.valid());
  MeshSyncSnapshot added = quader.mesh_sync().changes_since(cursor);
  EXPECT_GT(added.content_revision, cursor);
  const auto late_change =
      std::ranges::find_if(added.objects, [&](const ObjectMeshDelta &change) {
        return change.object == late.id();
      });
  ASSERT_NE(late_change, added.objects.end());
  EXPECT_FALSE(late_change->deleted);
  EXPECT_FALSE(late_change->mesh.vertices.empty());
}

TEST(ModelingSdkConsumerTests,
     MeshSyncDeletionEmitsTombstoneAndKeepsCursorMonotonic) {
  ModelingApi quader = ModelingApi::create();
  MeshHandle mesh = quader.create_box({.name = "Delete Me"});
  ASSERT_TRUE(mesh.valid());

  const MeshSyncSnapshot created = quader.mesh_sync().changes_since(0);
  ASSERT_GT(created.content_revision, 0U);
  ASSERT_TRUE(mesh.destroy().success);

  const MeshSyncSnapshot deleted =
      quader.mesh_sync().changes_since(created.content_revision);
  EXPECT_GT(deleted.content_revision, created.content_revision);
  const auto tombstone = std::ranges::find_if(
      deleted.objects, [&](const ObjectMeshDelta &change) {
        return change.object == mesh.id();
      });
  ASSERT_NE(tombstone, deleted.objects.end());
  EXPECT_TRUE(tombstone->deleted);
  EXPECT_TRUE(tombstone->mesh.vertices.empty());
  EXPECT_EQ(quader.revisions().content, deleted.content_revision);

  const MeshSyncSnapshot unchanged =
      quader.mesh_sync().changes_since(deleted.content_revision);
  EXPECT_EQ(unchanged.content_revision, deleted.content_revision);
  EXPECT_TRUE(unchanged.objects.empty());
}

TEST(ModelingSdkConsumerTests,
     MeshSyncAndRenderPublishUndoRedoTransitions) {
  ModelingApi quader = ModelingApi::create();
  MeshHandle mesh = quader.create_box({.name = "Undo Redo"});
  ASSERT_TRUE(mesh.valid());
  const ObjectId object_id = mesh.id();

  const MeshSyncSnapshot created = quader.mesh_sync().changes_since(0);
  const RenderSnapshotPayload initial = quader.render().scene_snapshot();
  ASSERT_EQ(created.content_revision, initial.scene_revision);
  ASSERT_TRUE(mesh.destroy().success);

  const MeshSyncSnapshot removed =
      quader.mesh_sync().changes_since(created.content_revision);
  ASSERT_GT(removed.content_revision, created.content_revision);
  ASSERT_EQ(removed.objects.size(), 1U);
  EXPECT_EQ(removed.objects.front().object, object_id);
  EXPECT_TRUE(removed.objects.front().deleted);

  ASSERT_TRUE(quader.undo().success);
  const MeshSyncSnapshot restored =
      quader.mesh_sync().changes_since(removed.content_revision);
  ASSERT_GT(restored.content_revision, removed.content_revision);
  ASSERT_EQ(restored.objects.size(), 1U);
  EXPECT_EQ(restored.objects.front().object, object_id);
  EXPECT_FALSE(restored.objects.front().deleted);
  EXPECT_FALSE(restored.objects.front().mesh.vertices.empty());

  const RenderSnapshotPayload rendered_undo =
      quader.render().scene_snapshot({
          .only_if_changed = true,
          .previous_revision = initial.scene_revision,
      });
  EXPECT_EQ(rendered_undo.scene_revision, restored.content_revision);
  EXPECT_FALSE(rendered_undo.meshes.empty());

  const RenderSnapshotPayload unchanged =
      quader.render().scene_snapshot({
          .only_if_changed = true,
          .previous_revision = rendered_undo.scene_revision,
      });
  EXPECT_EQ(unchanged.scene_revision, rendered_undo.scene_revision);
  EXPECT_TRUE(unchanged.meshes.empty());
  EXPECT_TRUE(unchanged.materials.empty());
  EXPECT_TRUE(unchanged.overlays.empty());

  ASSERT_TRUE(quader.redo().success);
  const MeshSyncSnapshot removed_again =
      quader.mesh_sync().changes_since(restored.content_revision);
  ASSERT_GT(removed_again.content_revision, restored.content_revision);
  ASSERT_EQ(removed_again.objects.size(), 1U);
  EXPECT_EQ(removed_again.objects.front().object, object_id);
  EXPECT_TRUE(removed_again.objects.front().deleted);

  const RenderSnapshotPayload rendered_redo =
      quader.render().scene_snapshot({
          .only_if_changed = true,
          .previous_revision = rendered_undo.scene_revision,
      });
  EXPECT_EQ(rendered_redo.scene_revision,
            removed_again.content_revision);
  EXPECT_GT(rendered_redo.scene_revision, rendered_undo.scene_revision);
  EXPECT_TRUE(rendered_redo.meshes.empty());
  EXPECT_FALSE(rendered_redo.materials.empty());
}

TEST(ModelingSdkConsumerTests,
     ModelingApiPreviewPayloadsMaterializeBeforeCommit) {
  ModelingApi quader = ModelingApi::create();
  MeshHandle mesh = quader.create_box({.name = "Preview Source"});
  ASSERT_TRUE(mesh.valid());

  EdgeSelection edges = mesh.edges().all();
  ASSERT_FALSE(edges.empty());

  PreviewHandle preview = edges.bevel({.width = 0.05F, .segments = 1});
  ASSERT_TRUE(preview.active());
  OperationReceipt updated =
      preview.update(EdgeBevelOptions{.width = 0.08F, .segments = 1});
  ASSERT_TRUE(updated.success) << updated.error.message;
  EXPECT_TRUE(updated.dirty.overlays);

  ToolPreviewPayload payload = preview.payload();
  EXPECT_TRUE(payload.valid);
  EXPECT_FALSE(payload.mesh.vertices.empty());
  EXPECT_FALSE(payload.overlay.records.empty());

  OperationReceipt canceled = preview.cancel();
  ASSERT_TRUE(canceled.success);
  EXPECT_FALSE(preview.active());

  PreviewHandle committed_preview =
      mesh.edges().all().bevel({.width = 0.05F, .segments = 1});
  OperationReceipt committed = committed_preview.commit("Commit Preview");
  ASSERT_TRUE(committed.success) << committed.error.message;
  EXPECT_TRUE(committed.changed);
}

TEST(ModelingSdkConsumerTests,
     ModelingApiToolsRenderAndCheckedDiagnosticsExposePublicSurface) {
  ModelingApi quader = ModelingApi::create({
      .error_policy = ErrorPolicy::StoreLastError,
  });
  EXPECT_EQ(quader.tools().active_tool(), "select");

  BoxTool box = quader.tools().activate_box_tool({.name = "Tool Box"});
  MeshHandle mesh = box.commit();
  ASSERT_TRUE(mesh.valid());
  EXPECT_EQ(quader.tools().active_tool(), "box");

  ASSERT_TRUE(mesh.faces().all().replace().success);
  TransformTool move = quader.tools().activate_move_tool();
  ASSERT_TRUE(move.begin({0.0F, 1.0F, 0.0F}).success);
  ASSERT_TRUE(quader.preview().active());
  ASSERT_TRUE(move.update({0.0F, 2.0F, 0.0F}).success);
  ASSERT_TRUE(move.commit("Move Tool").success);
  EXPECT_FALSE(quader.preview().active());

  ASSERT_TRUE(mesh.faces().all().replace().success);
  TransformTool scale = quader.tools().activate_scale_tool();
  ASSERT_TRUE(scale.begin({2.0F, 1.0F, 1.0F}).success);
  ASSERT_TRUE(scale.update({2.0F, 1.0F, 1.0F}).success);
  ASSERT_TRUE(scale.commit("Scale Tool").success);
  const std::optional<SdkMeshBounds> scaled_bounds =
      mesh_bounds(mesh.payloads().compile_mesh());
  ASSERT_TRUE(scaled_bounds.has_value());
  expect_vec3_near(scaled_bounds->min, {-2.0F, 1.0F, -1.0F});
  expect_vec3_near(scaled_bounds->max, {2.0F, 3.0F, 1.0F});

  RenderSnapshotPayload snapshot = quader.render().scene_snapshot();
  EXPECT_FALSE(snapshot.meshes.empty());
  EXPECT_FALSE(snapshot.overlays.empty());
  EXPECT_FALSE(snapshot.materials.empty());

  Result<OperationReceipt> repeat =
      quader.checked().operations().repeat_last_action();
  ASSERT_FALSE(repeat.ok());
  EXPECT_EQ(repeat.error().code, ErrorCode::InvalidArgument);
}

TEST(ModelingSdkConsumerTests,
     ModelingApiSelectionInvertAndMirrorUsePortableOperations) {
  ModelingApi quader = ModelingApi::create();
  MeshHandle first = quader.create_box({.name = "First"});
  MeshHandle second = quader.create_box({.name = "Second"});
  ASSERT_TRUE(first.valid());
  ASSERT_TRUE(second.valid());

  ASSERT_TRUE(first.select().replace().success);
  OperationReceipt inverted_objects = quader.selection().invert();
  ASSERT_TRUE(inverted_objects.success);
  const std::vector<MeshHandle> selected = quader.selected_meshes();
  ASSERT_EQ(selected.size(), 1U);
  EXPECT_EQ(selected.front().id(), second.id());

  const FaceSelection all_second_faces = second.faces().all();
  const std::vector<FaceId> one_face_id{all_second_faces.faces().front()};
  FaceSelection one_face = second.faces().only(one_face_id);
  ASSERT_TRUE(one_face.replace().success);
  OperationReceipt inverted_faces = quader.selection().invert();
  ASSERT_TRUE(inverted_faces.success);
  EXPECT_EQ(second.faces().selected().count(), second.faces().all().count() - 1U);

  ASSERT_TRUE(second.transform().translate({2.0F, 0.0F, 0.0F}).success);
  const MeshPayload before_mirror = second.payloads().compile_mesh();
  const std::optional<SdkMeshBounds> before_bounds = mesh_bounds(before_mirror);
  ASSERT_TRUE(before_bounds.has_value());
  expect_vec3_near(before_bounds->min, {1.0F, -1.0F, -1.0F});
  expect_vec3_near(before_bounds->max, {3.0F, 1.0F, 1.0F});

  OperationReceipt mirrored = quader.operations().mirror_selection(Axis::X);
  ASSERT_TRUE(mirrored.success) << mirrored.error.message;
  const MeshPayload after_mirror = second.payloads().compile_mesh();
  const std::optional<SdkMeshBounds> after_bounds = mesh_bounds(after_mirror);
  ASSERT_TRUE(after_bounds.has_value());
  expect_vec3_near(after_bounds->min, {-3.0F, -1.0F, -1.0F});
  expect_vec3_near(after_bounds->max, {-1.0F, 1.0F, 1.0F});
}

TEST(ModelingSdkConsumerTests,
     ModelingApiSelectionConsumesHostResolvedTargets) {
  ModelingApi quader = ModelingApi::create();
  MeshHandle first = quader.create_box({.name = "Pick First"});
  MeshHandle second = quader.create_box({.name = "Pick Second"});
  ASSERT_TRUE(first.valid());
  ASSERT_TRUE(second.valid());

  SelectionUnion first_target;
  first_target.kind = SelectionKind::Object;
  first_target.object = first.id();
  first_target.objects = {first.id()};

  PickRequest pick_request;
  pick_request.resolved = first_target;
  PickResult pick = quader.selection().pick(pick_request);
  ASSERT_TRUE(pick.hit);
  EXPECT_EQ(pick.selection.objects.front(), first.id());

  PickSelectRequest pick_select;
  pick_select.pick = pick_request;
  OperationReceipt picked = quader.selection().pick_select(pick_select);
  ASSERT_TRUE(picked.success) << picked.error.message;
  ASSERT_EQ(quader.selected_meshes().size(), 1U);
  EXPECT_EQ(quader.selected_meshes().front().id(), first.id());

  SelectionUnion second_target;
  second_target.kind = SelectionKind::Object;
  second_target.object = second.id();
  second_target.objects = {second.id()};

  BoxSelectionRequest box;
  box.resolved = second_target;
  OperationReceipt box_preview = quader.selection().preview_box(box);
  ASSERT_TRUE(box_preview.success) << box_preview.error.message;
  OperationReceipt box_commit = quader.selection().commit_box(box);
  ASSERT_TRUE(box_commit.success) << box_commit.error.message;
  ASSERT_EQ(quader.selected_meshes().size(), 1U);
  EXPECT_EQ(quader.selected_meshes().front().id(), second.id());

  HoverRequest hover;
  hover.resolved = first_target;
  HoverResult hover_result = quader.selection().preview_hover(hover);
  ASSERT_TRUE(hover_result.hit);
  OperationReceipt hover_applied =
      quader.selection().apply_hover(hover_result.target);
  ASSERT_TRUE(hover_applied.success) << hover_applied.error.message;
  ASSERT_EQ(quader.selected_meshes().size(), 1U);
  EXPECT_EQ(quader.selected_meshes().front().id(), first.id());
}

TEST(ModelingSdkConsumerTests,
     ModelingBatchCommitsReadableWorkflowAsSingleUndoStep) {
  ModelingApi quader = ModelingApi::create();

  BatchResult batch = quader.batch("Create and bevel")
                          .create_box({.name = "Batch Body"})
                          .as("body")
                          .with_mesh("body", [](MeshHandle mesh) {
                            OperationReceipt bevel =
                                mesh.edges()
                                    .all()
                                    .bevel({.width = 0.05F, .segments = 1})
                                    .commit("Batch Bevel");
                            ASSERT_TRUE(bevel.success) << bevel.error.message;
                          })
                          .commit();

  ASSERT_TRUE(batch.success()) << batch.receipt.error.message;
  ASSERT_EQ(batch.step_receipts.size(), 2U);
  MeshHandle body = batch.mesh("body");
  ASSERT_TRUE(body.valid());
  EXPECT_EQ(body.summary().name, "Batch Body");
  EXPECT_EQ(quader.mesh_summaries().size(), 1U);
  EXPECT_TRUE(quader.can_undo());

  OperationReceipt undone = quader.undo();
  ASSERT_TRUE(undone.success);
  EXPECT_FALSE(body.valid());
  EXPECT_TRUE(quader.mesh_summaries().empty());
}

TEST(ModelingSdkConsumerTests,
     ModelingBatchValidatesAliasesBeforeMutatingSession) {
  ModelingApi quader = ModelingApi::create();

  BatchResult batch = quader.batch("Invalid alias")
                          .create_box({.name = "Body"})
                          .as("body")
                          .with_mesh("missing", [](MeshHandle) {})
                          .commit();

  EXPECT_FALSE(batch.success());
  EXPECT_EQ(batch.receipt.error.code, ErrorCode::InvalidArgument);
  EXPECT_TRUE(quader.mesh_summaries().empty());
  EXPECT_FALSE(quader.can_undo());
}

TEST(ModelingSdkConsumerTests,
     ModelingBatchRollsBackWhenAStepOperationFails) {
  ModelingApi quader = ModelingApi::create();

  BatchResult batch = quader.batch("Rollback")
                          .create_box({.name = "Rollback Body"})
                          .as("body")
                          .with_mesh("body", [](MeshHandle mesh) {
                            const std::array<EdgeKey, 1> invalid_edges{};
                            static_cast<void>(
                                mesh.edges().only(invalid_edges).replace());
                          })
                          .commit();

  EXPECT_FALSE(batch.success());
  EXPECT_EQ(batch.receipt.error.code, ErrorCode::InvalidId);
  ASSERT_EQ(batch.step_receipts.size(), 2U);
  EXPECT_TRUE(batch.step_receipts.front().success);
  EXPECT_FALSE(batch.step_receipts.back().success);
  EXPECT_EQ(batch.step_receipts.back().error.code, ErrorCode::InvalidId);
  EXPECT_TRUE(quader.mesh_summaries().empty());
  EXPECT_FALSE(quader.can_undo());
}

TEST(ModelingSdkConsumerTests,
     ModelingBatchStoreLastErrorRollsBackFailedCallback) {
  ModelingApi quader = ModelingApi::create({
      .error_policy = ErrorPolicy::StoreLastError,
  });

  BatchResult batch = quader.batch("StoreLastError rollback")
                          .create_box({.name = "Rollback Body"})
                          .as("body")
                          .with_mesh("body", [](MeshHandle mesh) {
                            const std::array<EdgeKey, 1> invalid_edges{};
                            static_cast<void>(
                                mesh.edges().only(invalid_edges).replace());
                          })
                          .commit();

  EXPECT_FALSE(batch.success());
  EXPECT_EQ(batch.receipt.error.code, ErrorCode::InvalidId);
  ASSERT_EQ(batch.step_receipts.size(), 2U);
  EXPECT_TRUE(batch.step_receipts.front().success);
  EXPECT_FALSE(batch.step_receipts.back().success);
  EXPECT_EQ(batch.step_receipts.back().error.code, ErrorCode::InvalidId);
  EXPECT_FALSE(batch.mesh("body").valid());
  EXPECT_TRUE(quader.mesh_summaries().empty());
  EXPECT_FALSE(quader.can_undo());
  ASSERT_NE(quader.last_error(), nullptr);
  EXPECT_EQ(quader.last_error()->code, ErrorCode::InvalidId);
}

TEST(ModelingSdkConsumerTests,
     ModelingBatchInvalidCreateHasPolicyIndependentFailureReceipt) {
  const BoxOptions invalid{
      .name = "Invalid Box",
      .min = {1.0F, 0.0F, 0.0F},
      .max = {1.0F, 1.0F, 1.0F},
  };

  ModelingApi throwing = ModelingApi::create();
  BatchResult throwing_batch = throwing.batch("Throwing invalid create")
                                   .create_box(invalid)
                                   .as("body")
                                   .commit();

  ModelingApi stored = ModelingApi::create({
      .error_policy = ErrorPolicy::StoreLastError,
  });
  BatchResult stored_batch = stored.batch("Stored invalid create")
                                 .create_box(invalid)
                                 .as("body")
                                 .commit();

  EXPECT_FALSE(throwing_batch.success());
  EXPECT_FALSE(stored_batch.success());
  EXPECT_EQ(throwing_batch.receipt.error.code, ErrorCode::InvalidArgument);
  EXPECT_EQ(stored_batch.receipt.error.code, ErrorCode::InvalidArgument);
  EXPECT_EQ(stored_batch.receipt.error.message,
            throwing_batch.receipt.error.message);

  ASSERT_EQ(throwing_batch.step_receipts.size(), 1U);
  ASSERT_EQ(stored_batch.step_receipts.size(), 1U);
  EXPECT_FALSE(throwing_batch.step_receipts.front().success);
  EXPECT_FALSE(stored_batch.step_receipts.front().success);
  EXPECT_EQ(throwing_batch.step_receipts.front().error.code,
            ErrorCode::InvalidArgument);
  EXPECT_EQ(stored_batch.step_receipts.front().error.code,
            ErrorCode::InvalidArgument);
  EXPECT_EQ(stored_batch.step_receipts.front().error.message,
            throwing_batch.step_receipts.front().error.message);

  EXPECT_TRUE(throwing.mesh_summaries().empty());
  EXPECT_TRUE(stored.mesh_summaries().empty());
  EXPECT_FALSE(throwing.can_undo());
  EXPECT_FALSE(stored.can_undo());
  ASSERT_NE(stored.last_error(), nullptr);
  EXPECT_EQ(stored.last_error()->code, ErrorCode::InvalidArgument);
}

TEST(ModelingSdkConsumerTests,
     ModelingCommandSettingsCarryTypedOperationPayloads) {
  ModelingApi quader = ModelingApi::create();
  MeshHandle mesh = quader.create_box({.name = "Settings Box"});
  ASSERT_TRUE(mesh.valid());
  ASSERT_TRUE(mesh.faces().all().replace().success);

  OperationReceipt translated = quader.commands().execute(
      "translate_selection", {.delta = {3.0F, 0.0F, 0.0F}});
  ASSERT_TRUE(translated.success) << translated.error.message;
  const std::optional<SdkMeshBounds> translated_bounds =
      mesh_bounds(mesh.payloads().compile_mesh());
  ASSERT_TRUE(translated_bounds.has_value());
  expect_vec3_near(translated_bounds->min, {2.0F, -1.0F, -1.0F});
  expect_vec3_near(translated_bounds->max, {4.0F, 1.0F, 1.0F});

  OperationReceipt scaled = quader.commands().execute(
      "scale_selection",
      {.scale = {.scale = {2.0F, 1.0F, 1.0F}, .pivot = {3.0F, 0.0F, 0.0F}}});
  ASSERT_TRUE(scaled.success) << scaled.error.message;
  const std::optional<SdkMeshBounds> scaled_bounds =
      mesh_bounds(mesh.payloads().compile_mesh());
  ASSERT_TRUE(scaled_bounds.has_value());
  expect_vec3_near(scaled_bounds->min, {1.0F, -1.0F, -1.0F});
  expect_vec3_near(scaled_bounds->max, {5.0F, 1.0F, 1.0F});
}

TEST(ModelingSdkConsumerTests,
     ModelingApiCopiesAndPastesSelectedMeshesAsOneUndoableOperation) {
  ModelingApi quader = ModelingApi::create();
  MeshHandle source = quader.create_box({.name = "Clipboard Source"});
  ASSERT_TRUE(source.valid());
  ASSERT_TRUE(source.select().replace().success);

  OperationReceipt copied = quader.operations().copy_selection();
  ASSERT_TRUE(copied.success) << copied.error.message;
  EXPECT_FALSE(copied.changed);

  OperationReceipt pasted =
      quader.operations().paste_selection({.offset = {2.0F, 0.0F, 0.0F}});
  ASSERT_TRUE(pasted.success) << pasted.error.message;
  EXPECT_TRUE(pasted.changed);
  ASSERT_EQ(quader.mesh_summaries().size(), 2U);

  const std::vector<MeshHandle> selected = quader.selected_meshes();
  ASSERT_EQ(selected.size(), 1U);
  EXPECT_NE(selected.front().id(), source.id());
  EXPECT_EQ(selected.front().summary().name, "Clipboard Source");

  const std::optional<SdkMeshBounds> pasted_bounds =
      mesh_bounds(selected.front().payloads().compile_mesh());
  ASSERT_TRUE(pasted_bounds.has_value());
  expect_vec3_near(pasted_bounds->min, {1.0F, -1.0F, -1.0F});
  expect_vec3_near(pasted_bounds->max, {3.0F, 1.0F, 1.0F});

  OperationReceipt undone = quader.undo();
  ASSERT_TRUE(undone.success) << undone.error.message;
  EXPECT_EQ(quader.mesh_summaries().size(), 1U);
  EXPECT_TRUE(source.valid());
}

TEST(ModelingSdkConsumerTests,
     ModelingApiRepeatsLastCommandDispatchedOperation) {
  ModelingApi quader = ModelingApi::create();
  MeshHandle mesh = quader.create_box({.name = "Repeat Source"});
  ASSERT_TRUE(mesh.valid());
  ASSERT_TRUE(mesh.faces().all().replace().success);

  OperationReceipt moved = quader.commands().execute(
      "translate_selection", {.delta = {1.0F, 0.0F, 0.0F}});
  ASSERT_TRUE(moved.success) << moved.error.message;

  OperationReceipt repeated = quader.operations().repeat_last_action();
  ASSERT_TRUE(repeated.success) << repeated.error.message;

  const std::optional<SdkMeshBounds> bounds =
      mesh_bounds(mesh.payloads().compile_mesh());
  ASSERT_TRUE(bounds.has_value());
  expect_vec3_near(bounds->min, {1.0F, -1.0F, -1.0F});
  expect_vec3_near(bounds->max, {3.0F, 1.0F, 1.0F});
}

TEST(ModelingSdkConsumerTests,
     ModelingCommandDispatchCommitsPreviewCapableOperations) {
  ModelingApi quader = ModelingApi::create();
  MeshHandle mesh = quader.create_box({.name = "Command Bevel Source"});
  ASSERT_TRUE(mesh.valid());

  const AuthoredPolygonPayload before = mesh.payloads().authored_polygon();
  ASSERT_TRUE(mesh.edges().all().replace().success);

  OperationReceipt beveled = quader.commands().execute(
      "bevel_edges", {.edge_bevel = {.width = 0.05F, .segments = 1}});
  ASSERT_TRUE(beveled.success) << beveled.error.message;
  EXPECT_TRUE(beveled.changed);

  const AuthoredPolygonPayload after = mesh.payloads().authored_polygon();
  EXPECT_GT(after.vertices.size(), before.vertices.size());
}

TEST(ModelingSdkConsumerTests,
     ModelingApiKnifeSegmentUsesResolvedTopologyTargets) {
  ModelingApi quader = ModelingApi::create();
  MeshHandle mesh = quader.create_box({.name = "Knife Source"});
  ASSERT_TRUE(mesh.valid());

  const AuthoredPolygonPayload before = mesh.payloads().authored_polygon();
  ASSERT_FALSE(before.faces.empty());
  const AuthoredPolygonFacePayload &face = before.faces.front();
  ASSERT_GE(face.vertices.size(), 4U);
  const std::array<FaceId, 1> faces{face.id};
  ASSERT_TRUE(mesh.faces().only(faces).replace().success);

  KnifeTarget from;
  from.kind = KnifeTargetKind::ExistingVertex;
  from.vertex = face.vertices[0];
  KnifeTarget to;
  to.kind = KnifeTargetKind::ExistingVertex;
  to.vertex = face.vertices[2];
  OperationReceipt cut =
      quader.operations().knife_segment({.from = from, .to = to});
  ASSERT_TRUE(cut.success) << cut.error.message;
  EXPECT_TRUE(cut.changed);

  const AuthoredPolygonPayload after = mesh.payloads().authored_polygon();
  EXPECT_GT(after.faces.size(), before.faces.size());
}

TEST(ModelingSdkConsumerTests,
     ModelingApiKnifeToolCommitsResolvedTargetSegments) {
  ModelingApi quader = ModelingApi::create();
  MeshHandle mesh = quader.create_box({.name = "Knife Tool Source"});
  ASSERT_TRUE(mesh.valid());

  const AuthoredPolygonPayload before = mesh.payloads().authored_polygon();
  ASSERT_FALSE(before.faces.empty());
  const AuthoredPolygonFacePayload &face = before.faces.front();
  ASSERT_GE(face.vertices.size(), 4U);
  const std::array<FaceId, 1> faces{face.id};
  ASSERT_TRUE(mesh.faces().only(faces).replace().success);

  KnifeTarget from;
  from.kind = KnifeTargetKind::ExistingVertex;
  from.vertex = face.vertices[0];
  KnifeTarget to;
  to.kind = KnifeTargetKind::ExistingVertex;
  to.vertex = face.vertices[2];

  KnifeTool knife = quader.tools().activate_knife_tool();
  OperationReceipt cut = knife.add_segment(from, to).commit();
  ASSERT_TRUE(cut.success) << cut.error.message;
  EXPECT_TRUE(cut.changed);

  const AuthoredPolygonPayload after = mesh.payloads().authored_polygon();
  EXPECT_GT(after.faces.size(), before.faces.size());
}

TEST(ModelingSdkConsumerTests, ModelingApiPolyToolCreatesSingleFaceMesh) {
  ModelingApi quader = ModelingApi::create();

  PolyTool poly = quader.tools().activate_poly_tool();
  OperationReceipt created =
      poly.add_point({0.0F, 0.0F, 0.0F})
          .add_point({2.0F, 0.0F, 0.0F})
          .add_point({2.0F, 0.0F, 2.0F})
          .add_point({0.0F, 0.0F, 2.0F})
          .commit();
  ASSERT_TRUE(created.success) << created.error.message;
  EXPECT_TRUE(created.changed);

  ASSERT_EQ(quader.mesh_summaries().size(), 1U);
  const std::vector<MeshHandle> selected = quader.selected_meshes();
  ASSERT_EQ(selected.size(), 1U);
  const AuthoredPolygonPayload payload = selected.front().payloads().authored_polygon();
  EXPECT_EQ(payload.vertices.size(), 4U);
  EXPECT_EQ(payload.faces.size(), 1U);
}

TEST(ModelingSdkConsumerTests, ModelingApiPlaneCutKeepsBothHalves) {
  ModelingApi quader = ModelingApi::create();
  MeshHandle mesh = quader.create_box({.name = "Cut Box"});
  ASSERT_TRUE(mesh.valid());
  ASSERT_TRUE(mesh.select().replace().success);

  OperationReceipt cut = quader.operations().plane_cut({
      .a = {0.0F, -2.0F, 0.0F},
      .b = {0.0F, 2.0F, 0.0F},
      .c = {0.0F, 0.0F, 2.0F},
      .keep = CutKeepMode::KeepBoth,
  });
  ASSERT_TRUE(cut.success) << cut.error.message;
  EXPECT_TRUE(cut.changed);
  EXPECT_EQ(quader.mesh_summaries().size(), 2U);

  OperationReceipt undone = quader.undo();
  ASSERT_TRUE(undone.success) << undone.error.message;
  EXPECT_EQ(quader.mesh_summaries().size(), 1U);
  EXPECT_TRUE(mesh.valid());
}

TEST(ModelingSdkConsumerTests,
     ModelingApiCombinesMeshObjectsIntoTheHandleSurvivor) {
  ModelingApi quader = ModelingApi::create();
  MeshHandle first = quader.create_box({.name = "First"});
  MeshHandle second = quader.create_box({
      .name = "Second",
      .min = {2.0F, -1.0F, -1.0F},
      .max = {4.0F, 1.0F, 1.0F},
  });
  ASSERT_TRUE(first.valid());
  ASSERT_TRUE(second.valid());

  const std::array<MeshHandle, 1> sources{second};
  OperationReceipt combined = first.combine_with(sources);
  ASSERT_TRUE(combined.success) << combined.error.message;
  EXPECT_TRUE(combined.changed);
  EXPECT_TRUE(first.valid());
  EXPECT_FALSE(second.valid());
  ASSERT_EQ(quader.mesh_summaries().size(), 1U);
  EXPECT_EQ(quader.mesh_summaries().front().id, first.id());

  const std::optional<SdkMeshBounds> combined_bounds =
      mesh_bounds(first.payloads().compile_mesh());
  ASSERT_TRUE(combined_bounds.has_value());
  expect_vec3_near(combined_bounds->min, {-1.0F, -1.0F, -1.0F});
  expect_vec3_near(combined_bounds->max, {4.0F, 1.0F, 1.0F});

  OperationReceipt undone = quader.undo();
  ASSERT_TRUE(undone.success) << undone.error.message;
  EXPECT_TRUE(first.valid());
  EXPECT_TRUE(second.valid());
  EXPECT_EQ(quader.mesh_summaries().size(), 2U);
}

TEST(ModelingSdkConsumerTests,
     ModelingApiSnapsSelectedEdgesToTheActiveEdge) {
  ModelingApi quader = ModelingApi::create();
  MeshHandle mesh = quader.create_box({.name = "Snap Box"});
  ASSERT_TRUE(mesh.valid());

  const AuthoredPolygonPayload before = mesh.payloads().authored_polygon();
  ASSERT_FALSE(before.faces.empty());
  const AuthoredPolygonFacePayload &face = before.faces.front();
  ASSERT_GE(face.vertices.size(), 4U);
  const EdgeKey active = make_edge_key(face.vertices[0], face.vertices[1]);
  const EdgeKey source = make_edge_key(face.vertices[2], face.vertices[3]);
  const std::array<EdgeKey, 2> edges{active, source};

  OperationReceipt snapped = mesh.edges().only(edges).snap_to_active();
  ASSERT_TRUE(snapped.success) << snapped.error.message;
  EXPECT_TRUE(snapped.changed);

  const AuthoredPolygonPayload after = mesh.payloads().authored_polygon();
  const std::optional<Vec3> active_a = authored_position(after, active.a);
  const std::optional<Vec3> active_b = authored_position(after, active.b);
  const std::optional<Vec3> source_a = authored_position(after, source.a);
  const std::optional<Vec3> source_b = authored_position(after, source.b);
  ASSERT_TRUE(active_a.has_value());
  ASSERT_TRUE(active_b.has_value());
  ASSERT_TRUE(source_a.has_value());
  ASSERT_TRUE(source_b.has_value());
  const bool direct = vec3_near(*source_a, *active_a) &&
                      vec3_near(*source_b, *active_b);
  const bool flipped = vec3_near(*source_a, *active_b) &&
                       vec3_near(*source_b, *active_a);
  EXPECT_TRUE(direct || flipped);
}

TEST(ModelingSdkConsumerTests,
     SessionChangeJournalPreservesDirectDirtyDomains) {
  ModelingApi quader = ModelingApi::create();
  MeshHandle mesh = quader.create_box({.name = "Dirty Domains"});
  ASSERT_TRUE(mesh.valid());

  const MeshSyncSnapshot created = quader.mesh_sync().changes_since(0);
  ASSERT_GT(created.content_revision, 0U);
  const RenderSnapshotPayload rendered = quader.render().scene_snapshot();
  ASSERT_EQ(rendered.meshes.size(), 1U);
  ASSERT_FALSE(rendered.meshes.front().vertices.empty());

  OperationReceipt selected = mesh.faces().all().replace();
  ASSERT_TRUE(selected.success) << selected.error.message;
  ASSERT_TRUE(selected.changed);
  const MeshSyncSnapshot selection_change =
      quader.mesh_sync().changes_since(created.content_revision);
  ASSERT_EQ(selection_change.objects.size(), 1U);
  EXPECT_TRUE(selection_change.objects.front().dirty.selection);
  EXPECT_FALSE(selection_change.objects.front().dirty.topology);
  EXPECT_FALSE(selection_change.objects.front().dirty.geometry);
  EXPECT_FALSE(selection_change.objects.front().dirty.materials);
  EXPECT_TRUE(selection_change.objects.front().mesh.vertices.empty());
  EXPECT_TRUE(selection_change.objects.front().mesh.indices.empty());

  const RenderSnapshotPayload selection_render =
      quader.render().scene_snapshot({
          .only_if_changed = true,
          .previous_revision = rendered.scene_revision,
      });
  EXPECT_GT(selection_render.scene_revision, rendered.scene_revision);
  ASSERT_EQ(selection_render.meshes.size(), 1U);
  EXPECT_EQ(selection_render.meshes.front().content_revision,
            rendered.meshes.front().content_revision);
  EXPECT_EQ(selection_render.meshes.front().vertices.size(),
            rendered.meshes.front().vertices.size());
  EXPECT_EQ(selection_render.meshes.front().indices.size(),
            rendered.meshes.front().indices.size());

  OperationReceipt assigned = mesh.materials().assign_slot(7U);
  ASSERT_TRUE(assigned.success) << assigned.error.message;
  ASSERT_TRUE(assigned.changed);
  ASSERT_TRUE(assigned.dirty.materials);
  EXPECT_FALSE(assigned.dirty.topology);
  const MeshSyncSnapshot material_change =
      quader.mesh_sync().changes_since(selection_change.content_revision);
  ASSERT_EQ(material_change.objects.size(), 1U);
  EXPECT_EQ(material_change.objects.front().dirty.topology,
            assigned.dirty.topology);
  EXPECT_EQ(material_change.objects.front().dirty.geometry,
            assigned.dirty.geometry);
  EXPECT_EQ(material_change.objects.front().dirty.selection,
            assigned.dirty.selection);
  EXPECT_TRUE(material_change.objects.front().dirty.materials);
  EXPECT_FALSE(material_change.objects.front().mesh.vertices.empty());
}

TEST(ModelingSdkConsumerTests,
     SessionPreviewCommitPublishesExactDirtyAndUndoIsConservative) {
  Result<PolygonDocument> document = PolygonDocument::make_box({});
  ASSERT_TRUE(document.ok()) << document.error_message();
  ModelingSession session;
  Result<ObjectId> object =
      session.add_document(std::move(document).value(), "Preview Dirty");
  ASSERT_TRUE(object.ok()) << object.error_message();
  const PolygonDocument *live = session.document(object.value());
  ASSERT_NE(live, nullptr);
  const std::vector<FaceId> faces = live->face_ids();
  ASSERT_FALSE(faces.empty());
  ASSERT_TRUE(session.select_face(object.value(), faces.front()).ok());
  const std::uint64_t cursor = session.content_revision();

  Result<OperationResult> began =
      session.begin_translate_preview(object.value(), {0.0F, 1.0F, 0.0F});
  ASSERT_TRUE(began.ok()) << began.error_message();
  const std::uint64_t first_preview_revision =
      session.preview_summary().content_revision;
  ASSERT_GT(first_preview_revision, 0U);
  Result<OperationResult> updated =
      session.update_translate_preview({0.0F, 2.0F, 0.0F});
  ASSERT_TRUE(updated.ok()) << updated.error_message();
  EXPECT_GT(session.preview_summary().content_revision,
            first_preview_revision);

  Result<OperationResult> committed = session.commit_preview();
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  EXPECT_TRUE(committed.value().dirty.geometry);
  EXPECT_FALSE(committed.value().dirty.topology);
  EXPECT_FALSE(committed.value().dirty.materials);
  const SessionChangeSnapshot committed_change = session.changes_since(cursor);
  ASSERT_EQ(committed_change.objects.size(), 1U);
  EXPECT_TRUE(committed_change.objects.front().dirty.geometry);
  EXPECT_FALSE(committed_change.objects.front().dirty.topology);
  EXPECT_FALSE(committed_change.objects.front().dirty.materials);

  const std::uint64_t committed_cursor = session.content_revision();
  ASSERT_TRUE(session.undo().ok());
  const SessionChangeSnapshot undone = session.changes_since(committed_cursor);
  ASSERT_EQ(undone.objects.size(), 1U);
  EXPECT_TRUE(undone.objects.front().dirty.topology);
  EXPECT_TRUE(undone.objects.front().dirty.geometry);
  EXPECT_TRUE(undone.objects.front().dirty.materials);
}

TEST(ModelingSdkConsumerTests,
     PreviewPayloadCursorIsMonotonicAndGatesMeshAndOverlayTogether) {
  ModelingApi quader = ModelingApi::create();
  MeshHandle mesh = quader.create_box({.name = "Preview Cursor"});
  ASSERT_TRUE(mesh.valid());
  ASSERT_TRUE(mesh.faces().all().replace().success);

  PreviewHandle preview = mesh.transform().preview();
  ASSERT_TRUE(preview.active());
  const std::uint64_t first_revision = preview.summary().content_revision;
  ASSERT_GT(first_revision, 0U);
  const PayloadRequest initial_request{
      .only_if_changed = true,
      .previous_revision = first_revision - 1U,
  };
  const MeshPayload first_mesh = preview.mesh_payload(initial_request);
  const SemanticOverlayPayload first_overlay =
      preview.overlay_payload(initial_request);
  EXPECT_EQ(first_mesh.content_revision, first_revision);
  EXPECT_FALSE(first_mesh.vertices.empty());
  EXPECT_FALSE(first_overlay.records.empty());

  const PayloadRequest unchanged_request{
      .only_if_changed = true,
      .previous_revision = first_revision,
  };
  EXPECT_TRUE(preview.mesh_payload(unchanged_request).vertices.empty());
  EXPECT_TRUE(preview.overlay_payload(unchanged_request).records.empty());

  TransformOptions moved;
  moved.transform.origin = {2.0F, 0.0F, 0.0F};
  OperationReceipt updated = preview.update(moved);
  ASSERT_TRUE(updated.success) << updated.error.message;
  const std::uint64_t second_revision = preview.summary().content_revision;
  EXPECT_GT(second_revision, first_revision);
  const PayloadRequest updated_request{
      .only_if_changed = true,
      .previous_revision = first_revision,
  };
  const MeshPayload second_mesh = preview.mesh_payload(updated_request);
  EXPECT_EQ(second_mesh.content_revision, second_revision);
  EXPECT_FALSE(second_mesh.vertices.empty());
  EXPECT_FALSE(preview.overlay_payload(updated_request).records.empty());

  ASSERT_TRUE(preview.cancel().success);
  PreviewHandle next_preview = mesh.transform().preview();
  EXPECT_GT(next_preview.summary().content_revision, second_revision);
  EXPECT_TRUE(next_preview.cancel().success);
}

TEST(ModelingSdkConsumerTests,
     ModelingBatchRollsBackStandardAndUnknownCallbackExceptions) {
  ModelingApi standard = ModelingApi::create();
  BatchResult standard_failure =
      standard.batch("Standard callback exception")
          .create_box({.name = "Transient Standard"})
          .as("body")
          .with_mesh("body", [](MeshHandle mesh) {
            static_cast<void>(mesh.faces().all().replace());
            throw std::runtime_error("callback exploded");
          })
          .commit();
  EXPECT_FALSE(standard_failure.success());
  EXPECT_EQ(standard_failure.receipt.error.code, ErrorCode::InternalError);
  EXPECT_NE(standard_failure.receipt.error.message.find("callback exploded"),
            std::string::npos);
  ASSERT_EQ(standard_failure.step_receipts.size(), 2U);
  EXPECT_FALSE(standard_failure.step_receipts.back().success);
  EXPECT_TRUE(standard.mesh_summaries().empty());
  EXPECT_FALSE(standard.can_undo());

  ModelingApi unknown = ModelingApi::create();
  BatchResult unknown_failure =
      unknown.batch("Unknown callback exception")
          .create_box({.name = "Transient Unknown"})
          .as("body")
          .with_mesh("body", [](MeshHandle mesh) {
            static_cast<void>(mesh.faces().all().replace());
            throw 17;
          })
          .commit();
  EXPECT_FALSE(unknown_failure.success());
  EXPECT_EQ(unknown_failure.receipt.error.code, ErrorCode::InternalError);
  EXPECT_NE(unknown_failure.receipt.error.message.find("unknown exception"),
            std::string::npos);
  EXPECT_TRUE(unknown.mesh_summaries().empty());
  EXPECT_FALSE(unknown.can_undo());
}

} // namespace

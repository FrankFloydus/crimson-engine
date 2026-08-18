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
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using namespace quader::modeling;

/**
 * Exercises Result storage without requiring a default-constructible value.
 */
struct NonDefaultResultValue {
  explicit NonDefaultResultValue(int initial) : value(initial) {}
  int value = 0;
};

static_assert(!std::is_default_constructible_v<NonDefaultResultValue>);
static_assert(
    std::is_default_constructible_v<Result<NonDefaultResultValue>>);

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

[[nodiscard]] PolygonSelectionSnapshot single_face_selection(FaceId face) {
  return {
      .kind = SelectionKind::Face,
      .faces = {face},
      .has_active = true,
      .active_face = face,
  };
}

void expect_session_face_selection(const ModelingSession &session,
                                   ObjectId object,
                                   std::optional<FaceId> expected_face) {
  Result<PolygonDocument> document = session.document_copy(object);
  ASSERT_TRUE(document.ok()) << document.error_message();
  const PolygonSelectionSnapshot selection = document.value().selection();
  if (!expected_face.has_value()) {
    EXPECT_TRUE(selection.vertices.empty());
    EXPECT_TRUE(selection.edges.empty());
    EXPECT_TRUE(selection.faces.empty());
    EXPECT_FALSE(selection.has_active);
    return;
  }

  EXPECT_EQ(selection.kind, SelectionKind::Face);
  ASSERT_EQ(selection.faces.size(), 1U);
  EXPECT_EQ(selection.faces.front(), *expected_face);
  EXPECT_TRUE(selection.has_active);
  EXPECT_EQ(selection.active_face, *expected_face);
}

void expect_session_object_selected(const ModelingSession &session,
                                    ObjectId object, bool expected) {
  const std::vector<SessionObjectSummary> objects = session.objects();
  const auto found = std::ranges::find_if(
      objects, [&](const SessionObjectSummary &candidate) {
        return candidate.id == object;
      });
  ASSERT_NE(found, objects.end());
  EXPECT_EQ(found->selected, expected);
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
     PositionChangingOperationsRestoreProceduralFaceUvs) {
  for (int operation = 0; operation < 3; ++operation) {
    SCOPED_TRACE(operation);
    Result<PolygonDocument> box = PolygonDocument::make_box({});
    ASSERT_TRUE(box.ok()) << box.error_message();
    Result<AuthoredTopology> authored = box.value().authored_topology();
    ASSERT_TRUE(authored.ok()) << authored.error_message();
    for (AuthoredTopologyFace &face : authored.value().faces) {
      face.uvs.resize(face.vertices.size());
      for (std::size_t index = 0u; index < face.uvs.size(); ++index) {
        face.uvs[index] = {static_cast<float>(index),
                           static_cast<float>(face.id.index)};
      }
    }
    Result<PolygonDocument> imported =
        PolygonDocument::from_authored_topology(authored.value());
    ASSERT_TRUE(imported.ok()) << imported.error_message();
    PolygonDocument document = std::move(imported).value();
    const VertexId vertex = document.vertex_ids().front();

    Result<OperationResult> changed;
    if (operation == 0) {
      ASSERT_TRUE(document.select_vertex(vertex).ok());
      changed = document.translate_selection({0.5f, 0.25f, 0.0f});
    } else if (operation == 1) {
      ASSERT_TRUE(document.select_vertex(vertex).ok());
      Transform3 transform;
      transform.origin = {0.5f, 0.25f, 0.0f};
      changed = document.transform_selection(transform);
    } else {
      const Result<AuthoredTopology> before = document.authored_topology();
      ASSERT_TRUE(before.ok()) << before.error_message();
      const auto found = std::ranges::find(
          before.value().vertices, vertex,
          &AuthoredTopologyVertex::id);
      ASSERT_NE(found, before.value().vertices.end());
      const std::array<VertexPositionUpdate, 1> updates{VertexPositionUpdate{
          .vertex = vertex,
          .position = {found->position.x + 0.5f, found->position.y + 0.25f,
                       found->position.z},
      }};
      changed = document.set_vertex_positions(updates);
    }
    ASSERT_TRUE(changed.ok()) << changed.error_message();
    ASSERT_TRUE(changed.value().changed);

    Result<AuthoredTopology> result = document.authored_topology();
    ASSERT_TRUE(result.ok()) << result.error_message();
    EXPECT_TRUE(std::ranges::all_of(
        result.value().faces,
        [](const AuthoredTopologyFace &face) { return face.uvs.empty(); }));
  }
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

TEST(ModelingSdkConsumerTests,
     DenseAssignAllFaceMaterialSlotUsesOneMutationAndPreservesSelection) {
  Result<PolygonDocument> document_result = PolygonDocument::make_sphere(
      {.radial_segments = 96, .rings = 64});
  ASSERT_TRUE(document_result.ok()) << document_result.error_message();
  PolygonDocument document = std::move(document_result).value();
  const std::vector<FaceId> faces = document.face_ids();
  ASSERT_GT(faces.size(), 1000U);
  const std::array selected{faces[1], faces[3]};
  ASSERT_TRUE(document.select_faces(selected).ok());
  ASSERT_TRUE(document.assign_face_material_slot(faces.back(), 9U).ok());

  const PolygonSelectionSnapshot selection_before = document.selection();
  const std::uint64_t selection_revision_before =
      document.selection_revision();
  const std::uint64_t content_revision_before = document.content_revision();
  Result<OperationResult> assigned = document.assign_all_face_material_slot(4U);
  ASSERT_TRUE(assigned.ok()) << assigned.error_message();
  EXPECT_TRUE(assigned.value().changed);
  EXPECT_TRUE(assigned.value().dirty.materials);
  EXPECT_EQ(document.content_revision(), content_revision_before + 1U);
  EXPECT_EQ(document.selection_revision(), selection_revision_before);
  const PolygonSelectionSnapshot selection_after = document.selection();
  EXPECT_EQ(selection_after.kind, selection_before.kind);
  EXPECT_EQ(selection_after.vertices, selection_before.vertices);
  EXPECT_EQ(selection_after.edges, selection_before.edges);
  EXPECT_EQ(selection_after.faces, selection_before.faces);
  EXPECT_EQ(selection_after.has_active, selection_before.has_active);
  EXPECT_EQ(selection_after.active_vertex, selection_before.active_vertex);
  EXPECT_EQ(selection_after.active_edge, selection_before.active_edge);
  EXPECT_EQ(selection_after.active_face, selection_before.active_face);
  EXPECT_EQ(document.face_ids_by_material_slot(4U), faces);

  const PolygonContentIdentity normalized_identity = document.content_identity();
  const std::uint64_t normalized_revision = document.content_revision();
  Result<OperationResult> unchanged =
      document.assign_all_face_material_slot(4U);
  ASSERT_TRUE(unchanged.ok()) << unchanged.error_message();
  EXPECT_FALSE(unchanged.value().changed);
  EXPECT_FALSE(unchanged.value().dirty.materials);
  EXPECT_EQ(document.content_identity(), normalized_identity);
  EXPECT_EQ(document.content_revision(), normalized_revision);
  EXPECT_EQ(document.selection_revision(), selection_revision_before);
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
     OrientedBoxFactoryPreservesEightCornersAndRejectsInvalidInput) {
  const std::array<Vec3, 8> corners{{
      {-2.0F, -1.0F, -1.0F},
      {0.0F, -1.0F, -3.0F},
      {2.0F, -1.0F, -1.0F},
      {0.0F, -1.0F, 1.0F},
      {-2.0F, 2.0F, -1.0F},
      {0.0F, 2.0F, -3.0F},
      {2.0F, 2.0F, -1.0F},
      {0.0F, 2.0F, 1.0F},
  }};

  Result<PolygonDocument> result =
      PolygonDocument::make_box_from_corners(corners);
  ASSERT_TRUE(result.ok()) << result.error_message();
  EXPECT_EQ(result.value().vertex_count(), 8U);
  EXPECT_EQ(result.value().face_count(), 6U);
  Result<AuthoredPolygonPayload> payload = result.value().authored_payload();
  ASSERT_TRUE(payload.ok()) << payload.error_message();
  for (const Vec3 expected : corners) {
    EXPECT_TRUE(std::any_of(
        payload.value().positions.begin(), payload.value().positions.end(),
        [expected](const Vec3 &position) {
          return position.x == expected.x && position.y == expected.y &&
                 position.z == expected.z;
        }));
  }

  const std::array<Vec3, 7> too_few{};
  Result<PolygonDocument> wrong_count =
      PolygonDocument::make_box_from_corners(too_few);
  EXPECT_FALSE(wrong_count.ok());

  const std::array<Vec3, 8> degenerate{};
  Result<PolygonDocument> zero_volume =
      PolygonDocument::make_box_from_corners(degenerate);
  EXPECT_FALSE(zero_volume.ok());
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

  Result<PolygonDocument> cone_result = PolygonDocument::make_cone({
      .width = 4.0F,
      .height = 6.0F,
      .depth = 2.0F,
      .side_count = 8,
      .capped = true,
      .material = material,
  });
  ASSERT_TRUE(cone_result.ok()) << cone_result.error_message();
  PolygonDocument cone = std::move(cone_result).value();
  EXPECT_EQ(cone.vertex_count(), 9U);
  EXPECT_EQ(cone.face_count(), 9U);
  Result<MeshPayload> cone_mesh = cone.compile_mesh();
  ASSERT_TRUE(cone_mesh.ok()) << cone_mesh.error_message();
  const std::optional<SdkMeshBounds> cone_bounds =
      mesh_bounds(cone_mesh.value());
  ASSERT_TRUE(cone_bounds.has_value());
  expect_vec3_near(cone_bounds->min, {-2.0F, -3.0F, -1.0F});
  expect_vec3_near(cone_bounds->max, {2.0F, 3.0F, 1.0F});

  Result<PolygonDocument> open_cone = PolygonDocument::make_cone({
      .width = 4.0F,
      .height = 6.0F,
      .depth = 2.0F,
      .side_count = 8,
      .capped = false,
      .material = material,
  });
  ASSERT_TRUE(open_cone.ok()) << open_cone.error_message();
  EXPECT_EQ(open_cone.value().face_count(), 8U);

  Result<PolygonDocument> flipped_cone = PolygonDocument::make_cone({
      .width = 4.0F,
      .height = 6.0F,
      .depth = 2.0F,
      .side_count = 8,
      .capped = true,
      .flip_faces = true,
      .material = material,
  });
  ASSERT_TRUE(flipped_cone.ok()) << flipped_cone.error_message();
  expect_reversed_face_winding(cone, flipped_cone.value());

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

  Result<PolygonDocument> torus_result = PolygonDocument::make_torus({
      .width = 4.0F,
      .height = 1.5F,
      .depth = 2.0F,
      .major_segments = 8,
      .minor_segments = 4,
      .tube_radius_ratio = 0.25F,
      .material = material,
  });
  ASSERT_TRUE(torus_result.ok()) << torus_result.error_message();
  PolygonDocument torus = std::move(torus_result).value();
  EXPECT_EQ(torus.vertex_count(), 32U);
  EXPECT_EQ(torus.face_count(), 32U);
  Result<MeshPayload> torus_mesh = torus.compile_mesh();
  ASSERT_TRUE(torus_mesh.ok()) << torus_mesh.error_message();
  const std::optional<SdkMeshBounds> torus_bounds =
      mesh_bounds(torus_mesh.value());
  ASSERT_TRUE(torus_bounds.has_value());
  expect_vec3_near(torus_bounds->min, {-2.0F, -0.75F, -1.0F});
  expect_vec3_near(torus_bounds->max, {2.0F, 0.75F, 1.0F});
  Result<AuthoredPolygonPayload> torus_payload = torus.authored_payload();
  ASSERT_TRUE(torus_payload.ok()) << torus_payload.error_message();
  EXPECT_TRUE(std::ranges::all_of(
      torus_payload.value().faces,
      [material](const AuthoredPolygonFacePayload &face) {
        return face.material == material;
      }));

  Result<PolygonDocument> flipped_torus = PolygonDocument::make_torus({
      .width = 4.0F,
      .height = 1.5F,
      .depth = 2.0F,
      .major_segments = 8,
      .minor_segments = 4,
      .tube_radius_ratio = 0.25F,
      .flip_faces = true,
      .material = material,
  });
  ASSERT_TRUE(flipped_torus.ok()) << flipped_torus.error_message();
  expect_reversed_face_winding(torus, flipped_torus.value());
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
  MeshHandle cone = quader.create_cone({
      .name = "Open Cone",
      .side_count = 8,
      .capped = false,
  });
  MeshHandle torus = quader.create_torus({
      .name = "Detailed Torus",
      .major_segments = 8,
      .minor_segments = 4,
      .tube_radius_ratio = 0.25F,
  });

  ASSERT_TRUE(box.valid());
  ASSERT_TRUE(cylinder.valid());
  ASSERT_TRUE(sphere.valid());
  ASSERT_TRUE(cone.valid());
  ASSERT_TRUE(torus.valid());
  EXPECT_EQ(box.summary().name, "Subdivided Box");
  EXPECT_EQ(cylinder.summary().name, "Open Cylinder");
  EXPECT_EQ(sphere.summary().name, "Hemisphere");
  EXPECT_EQ(cone.summary().name, "Open Cone");
  EXPECT_EQ(torus.summary().name, "Detailed Torus");
  EXPECT_EQ(box.payloads().authored_polygon().faces.size(), 52U);
  EXPECT_EQ(cylinder.payloads().authored_polygon().faces.size(), 8U);
  EXPECT_EQ(sphere.payloads().authored_polygon().faces.size(), 33U);
  EXPECT_EQ(cone.payloads().authored_polygon().faces.size(), 8U);
  EXPECT_EQ(torus.payloads().authored_polygon().faces.size(), 32U);

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
  Result<MeshHandle> invalid_cone =
      quader.checked().create_cone({.side_count = 2});
  ASSERT_FALSE(invalid_cone.ok());
  EXPECT_EQ(invalid_cone.error().code, ErrorCode::InvalidArgument);
  Result<MeshHandle> invalid_torus_segments =
      quader.checked().create_torus({.minor_segments = 2});
  ASSERT_FALSE(invalid_torus_segments.ok());
  EXPECT_EQ(invalid_torus_segments.error().code, ErrorCode::InvalidArgument);
  Result<MeshHandle> invalid_torus_ratio =
      quader.checked().create_torus({.tube_radius_ratio = 0.5F});
  ASSERT_FALSE(invalid_torus_ratio.ok());
  EXPECT_EQ(invalid_torus_ratio.error().code, ErrorCode::InvalidArgument);
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
     SessionHydrationAndHostHistoryRestorePublishLinearBatches) {
  constexpr std::size_t kDocumentCount = 2048U;
  Result<PolygonDocument> template_document = PolygonDocument::make_box({});
  ASSERT_TRUE(template_document.ok()) << template_document.error_message();

  std::vector<SessionWorkspaceDocument> documents;
  documents.reserve(kDocumentCount);
  for (std::size_t index = 0; index < kDocumentCount; ++index) {
    documents.push_back({
        .document = template_document.value(),
        .name = "Hydrated " + std::to_string(index),
        .selected = index == 17U,
    });
  }

  ModelingSession session;
  Result<std::vector<ObjectId>> hydrated =
      session.replace_workspace_preserving_clipboard(std::move(documents));
  ASSERT_TRUE(hydrated.ok()) << hydrated.error_message();
  ASSERT_EQ(hydrated.value().size(), kDocumentCount);
  EXPECT_EQ(session.objects().size(), kDocumentCount);
  EXPECT_EQ(session.content_revision(), 1U);
  EXPECT_EQ(session.undo_depth(), 0U);
  EXPECT_FALSE(session.can_undo());
  EXPECT_TRUE(session.objects()[17U].selected);
  EXPECT_EQ(session.changes_since(0U).objects.size(), kDocumentCount);

  Result<PolygonDocument> replacement =
      session.document_copy(hydrated.value()[17U]);
  ASSERT_TRUE(replacement.ok()) << replacement.error_message();
  ASSERT_TRUE(replacement.value().select_all_faces().ok());
  Result<OperationResult> translated =
      replacement.value().translate_selection({0.0F, 2.0F, 0.0F});
  ASSERT_TRUE(translated.ok()) << translated.error_message();
  ASSERT_TRUE(translated.value().changed);

  const std::uint64_t before_restore = session.content_revision();
  std::vector<SessionDocumentHistoryRestore> restores;
  restores.push_back({
      .object = hydrated.value()[17U],
      .document = std::move(replacement).value(),
      .dirty = {.geometry = true, .overlays = true},
  });
  Result<OperationResult> restored =
      session.restore_documents_as_history_transition(std::move(restores));
  ASSERT_TRUE(restored.ok()) << restored.error_message();
  EXPECT_TRUE(restored.value().changed);
  EXPECT_EQ(session.content_revision(), before_restore + 1U);
  EXPECT_EQ(session.undo_depth(), 0U);
  const SessionChangeSnapshot changes =
      session.changes_since(before_restore);
  ASSERT_EQ(changes.objects.size(), 1U);
  EXPECT_EQ(changes.objects.front().id, hydrated.value()[17U]);
}

TEST(ModelingSdkConsumerTests,
     SessionBridgesTwoDocumentsInSurvivorSpaceAtomically) {
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

  Result<PolygonDocument> first_copy = session.document_copy(first.value());
  Result<PolygonDocument> second_copy = session.document_copy(second.value());
  ASSERT_TRUE(first_copy.ok()) << first_copy.error_message();
  ASSERT_TRUE(second_copy.ok()) << second_copy.error_message();
  ASSERT_FALSE(first_copy.value().face_ids().empty());
  ASSERT_FALSE(second_copy.value().face_ids().empty());
  ASSERT_TRUE(
      session.select_face(first.value(), first_copy.value().face_ids().front())
          .ok());
  ASSERT_TRUE(
      session.select_face(second.value(), second_copy.value().face_ids().front())
          .ok());

  Result<OperationResult> bridged = session.bridge_objects(
      first.value(), second.value(), Transform3{.origin = {4.0F, 0.0F, 0.0F}},
      SelectionKind::Face, 3);
  ASSERT_TRUE(bridged.ok()) << bridged.error_message();
  ASSERT_TRUE(bridged.value().success) << bridged.value().message;
  EXPECT_TRUE(bridged.value().changed);
  EXPECT_TRUE(bridged.value().dirty.topology);
  ASSERT_EQ(session.objects().size(), 1U);
  EXPECT_EQ(session.objects().front().id, first.value());

  Result<PolygonDocument> survivor = session.document_copy(first.value());
  ASSERT_TRUE(survivor.ok()) << survivor.error_message();
  Result<MeshPayload> mesh = survivor.value().compile_mesh();
  ASSERT_TRUE(mesh.ok()) << mesh.error_message();
  const std::optional<SdkMeshBounds> bounds = mesh_bounds(mesh.value());
  ASSERT_TRUE(bounds.has_value());
  EXPECT_NEAR(bounds->min.x, -1.0F, 0.0001F);
  EXPECT_NEAR(bounds->max.x, 5.0F, 0.0001F);

  ASSERT_TRUE(session.undo().ok());
  EXPECT_EQ(session.objects().size(), 2U);
  EXPECT_TRUE(session.document_copy(second.value()).ok());
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
     SessionComponentSelectionBatchIsAtomicAndOneUndoStep) {
  Result<PolygonDocument> first_document = PolygonDocument::make_box({});
  Result<PolygonDocument> second_document = PolygonDocument::make_box({});
  ASSERT_TRUE(first_document.ok()) << first_document.error_message();
  ASSERT_TRUE(second_document.ok()) << second_document.error_message();
  ASSERT_GE(first_document.value().face_ids().size(), 2U);
  ASSERT_GE(second_document.value().face_ids().size(), 2U);
  const FaceId first_face = first_document.value().face_ids()[0];
  const FaceId second_face = second_document.value().face_ids()[1];

  ModelingSession session;
  Result<ObjectId> first =
      session.add_document(std::move(first_document).value(), "First");
  Result<ObjectId> second =
      session.add_document(std::move(second_document).value(), "Second");
  ASSERT_TRUE(first.ok()) << first.error_message();
  ASSERT_TRUE(second.ok()) << second.error_message();

  const std::array<SessionComponentSelectionUpdate, 2> updates{{
      {.object = first.value(),
       .selection = single_face_selection(first_face)},
      {.object = second.value(),
       .selection = single_face_selection(second_face)},
  }};
  const std::array<ObjectId, 2> selected_objects{first.value(),
                                                 second.value()};
  const std::size_t undo_depth = session.undo_depth();
  const std::uint64_t content_revision = session.content_revision();
  const std::uint64_t selection_revision = session.selection_revision();

  Result<OperationResult> committed = session.commit_component_selection_batch(
      updates, selected_objects, second.value());
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  EXPECT_TRUE(committed.value().success) << committed.value().message;
  EXPECT_TRUE(committed.value().changed);
  EXPECT_TRUE(committed.value().dirty.selection);
  EXPECT_TRUE(committed.value().dirty.overlays);
  EXPECT_EQ(session.undo_depth(), undo_depth + 1U);
  EXPECT_GT(session.content_revision(), content_revision);
  EXPECT_GT(session.selection_revision(), selection_revision);
  expect_session_face_selection(session, first.value(), first_face);
  expect_session_face_selection(session, second.value(), second_face);
  expect_session_object_selected(session, first.value(), true);
  expect_session_object_selected(session, second.value(), true);

  const SessionChangeSnapshot changes =
      session.changes_since(content_revision);
  ASSERT_EQ(changes.objects.size(), 2U);
  for (const ObjectId object : selected_objects) {
    const auto change = std::ranges::find_if(
        changes.objects, [&](const SessionObjectChange &candidate) {
          return candidate.id == object;
        });
    ASSERT_NE(change, changes.objects.end());
    EXPECT_TRUE(change->dirty.selection);
    EXPECT_TRUE(change->dirty.overlays);
    EXPECT_FALSE(change->dirty.topology);
    EXPECT_FALSE(change->dirty.geometry);
  }

  Result<OperationResult> undo = session.undo();
  ASSERT_TRUE(undo.ok()) << undo.error_message();
  EXPECT_EQ(session.undo_depth(), undo_depth);
  EXPECT_TRUE(session.can_redo());
  expect_session_face_selection(session, first.value(), std::nullopt);
  expect_session_face_selection(session, second.value(), std::nullopt);
  expect_session_object_selected(session, first.value(), false);
  expect_session_object_selected(session, second.value(), false);

  Result<OperationResult> redo = session.redo();
  ASSERT_TRUE(redo.ok()) << redo.error_message();
  EXPECT_EQ(session.undo_depth(), undo_depth + 1U);
  expect_session_face_selection(session, first.value(), first_face);
  expect_session_face_selection(session, second.value(), second_face);
  expect_session_object_selected(session, first.value(), true);
  expect_session_object_selected(session, second.value(), true);
}

TEST(ModelingSdkConsumerTests,
     SessionDenseObjectDuplicationPublishesOneAtomicTransition) {
  constexpr std::size_t kSourceCount = 64u;
  Result<PolygonDocument> template_document = PolygonDocument::make_box({});
  ASSERT_TRUE(template_document.ok()) << template_document.error_message();

  ModelingSession session;
  std::vector<ObjectId> sources;
  sources.reserve(kSourceCount);
  for (std::size_t index = 0u; index < kSourceCount; ++index) {
    Result<ObjectId> added = session.add_document(
        template_document.value(), "Dense duplicate source");
    ASSERT_TRUE(added.ok()) << added.error_message();
    sources.push_back(added.value());
  }

  const std::size_t undo_depth = session.undo_depth();
  const std::uint64_t content_revision = session.content_revision();
  Result<std::vector<ObjectId>> duplicated =
      session.duplicate_objects(sources);
  ASSERT_TRUE(duplicated.ok()) << duplicated.error_message();
  ASSERT_EQ(duplicated.value().size(), kSourceCount);
  EXPECT_EQ(session.undo_depth(), undo_depth + 1u);
  EXPECT_EQ(session.content_revision(), content_revision + 1u);

  const std::vector<SessionObjectSummary> after_duplicate = session.objects();
  ASSERT_EQ(after_duplicate.size(), kSourceCount * 2u);
  for (const ObjectId duplicate : duplicated.value()) {
    const auto found = std::ranges::find_if(
        after_duplicate, [&](const SessionObjectSummary &candidate) {
          return candidate.id == duplicate;
        });
    ASSERT_NE(found, after_duplicate.end());
    EXPECT_TRUE(found->selected);
  }
  for (const ObjectId source : sources) {
    const auto found = std::ranges::find_if(
        after_duplicate, [&](const SessionObjectSummary &candidate) {
          return candidate.id == source;
        });
    ASSERT_NE(found, after_duplicate.end());
    EXPECT_FALSE(found->selected);
  }

  Result<OperationResult> undo = session.undo();
  ASSERT_TRUE(undo.ok()) << undo.error_message();
  EXPECT_EQ(session.objects().size(), kSourceCount);
  EXPECT_EQ(session.undo_depth(), undo_depth);

  Result<OperationResult> redo = session.redo();
  ASSERT_TRUE(redo.ok()) << redo.error_message();
  EXPECT_EQ(session.objects().size(), kSourceCount * 2u);
  EXPECT_EQ(session.undo_depth(), undo_depth + 1u);
}

TEST(ModelingSdkConsumerTests,
     SessionUnchangedComponentSelectionBatchPreservesRedoHistory) {
  Result<PolygonDocument> first_document = PolygonDocument::make_box({});
  Result<PolygonDocument> second_document = PolygonDocument::make_box({});
  ASSERT_TRUE(first_document.ok()) << first_document.error_message();
  ASSERT_TRUE(second_document.ok()) << second_document.error_message();
  ASSERT_FALSE(first_document.value().face_ids().empty());
  ASSERT_FALSE(second_document.value().face_ids().empty());
  const FaceId first_face = first_document.value().face_ids().front();
  const FaceId second_face = second_document.value().face_ids().back();

  ModelingSession session;
  Result<ObjectId> first =
      session.add_document(std::move(first_document).value(), "First");
  Result<ObjectId> second =
      session.add_document(std::move(second_document).value(), "Second");
  ASSERT_TRUE(first.ok()) << first.error_message();
  ASSERT_TRUE(second.ok()) << second.error_message();
  const std::array<SessionComponentSelectionUpdate, 2> updates{{
      {.object = first.value(),
       .selection = single_face_selection(first_face)},
      {.object = second.value(),
       .selection = single_face_selection(second_face)},
  }};
  const std::array<ObjectId, 2> selected_objects{first.value(),
                                                 second.value()};
  ASSERT_TRUE(session
                  .commit_component_selection_batch(updates, selected_objects,
                                                    second.value())
                  .ok());
  ASSERT_TRUE(session.rename_object(first.value(), "Renamed").ok());
  ASSERT_TRUE(session.undo().ok());
  ASSERT_TRUE(session.can_redo());
  expect_session_face_selection(session, first.value(), first_face);
  expect_session_face_selection(session, second.value(), second_face);
  expect_session_object_selected(session, first.value(), true);
  expect_session_object_selected(session, second.value(), true);

  const std::size_t undo_depth = session.undo_depth();
  const std::uint64_t content_revision = session.content_revision();
  const std::uint64_t selection_revision = session.selection_revision();
  Result<OperationResult> unchanged = session.commit_component_selection_batch(
      updates, selected_objects, second.value());
  ASSERT_TRUE(unchanged.ok()) << unchanged.error_message();
  EXPECT_TRUE(unchanged.value().success) << unchanged.value().message;
  EXPECT_FALSE(unchanged.value().changed);
  EXPECT_FALSE(unchanged.value().dirty.selection);
  EXPECT_FALSE(unchanged.value().dirty.overlays);
  EXPECT_EQ(session.undo_depth(), undo_depth);
  EXPECT_TRUE(session.can_redo());
  EXPECT_EQ(session.content_revision(), content_revision);
  EXPECT_EQ(session.selection_revision(), selection_revision);
  EXPECT_TRUE(session.changes_since(content_revision).objects.empty());

  Result<OperationResult> redo = session.redo();
  ASSERT_TRUE(redo.ok()) << redo.error_message();
  const std::vector<SessionObjectSummary> objects = session.objects();
  const auto renamed = std::ranges::find_if(
      objects, [&](const SessionObjectSummary &candidate) {
        return candidate.id == first.value();
      });
  ASSERT_NE(renamed, objects.end());
  EXPECT_EQ(renamed->name, "Renamed");
}

TEST(ModelingSdkConsumerTests,
     SessionInvalidAndDuplicateComponentSelectionBatchesAreAtomic) {
  Result<PolygonDocument> first_document = PolygonDocument::make_box({});
  Result<PolygonDocument> second_document = PolygonDocument::make_box({});
  ASSERT_TRUE(first_document.ok()) << first_document.error_message();
  ASSERT_TRUE(second_document.ok()) << second_document.error_message();
  ASSERT_FALSE(first_document.value().face_ids().empty());
  ASSERT_FALSE(second_document.value().face_ids().empty());
  const FaceId first_face = first_document.value().face_ids().front();
  const FaceId second_face = second_document.value().face_ids().front();

  ModelingSession session;
  Result<ObjectId> first =
      session.add_document(std::move(first_document).value(), "First");
  Result<ObjectId> second =
      session.add_document(std::move(second_document).value(), "Second");
  ASSERT_TRUE(first.ok()) << first.error_message();
  ASSERT_TRUE(second.ok()) << second.error_message();
  ASSERT_TRUE(session.rename_object(first.value(), "Renamed").ok());
  ASSERT_TRUE(session.undo().ok());
  ASSERT_TRUE(session.can_redo());

  const std::array<ObjectId, 2> selected_objects{first.value(),
                                                 second.value()};
  const std::size_t undo_depth = session.undo_depth();
  const std::uint64_t content_revision = session.content_revision();
  const std::uint64_t selection_revision = session.selection_revision();
  const std::array<SessionComponentSelectionUpdate, 2> invalid_updates{{
      {.object = first.value(),
       .selection = single_face_selection(first_face)},
      {.object = second.value(),
       .selection = single_face_selection(make_id<FaceTag>(999999U))},
  }};

  Result<OperationResult> invalid = session.commit_component_selection_batch(
      invalid_updates, selected_objects, second.value());
  EXPECT_FALSE(invalid.ok());
  EXPECT_EQ(session.undo_depth(), undo_depth);
  EXPECT_TRUE(session.can_redo());
  EXPECT_EQ(session.content_revision(), content_revision);
  EXPECT_EQ(session.selection_revision(), selection_revision);
  EXPECT_TRUE(session.changes_since(content_revision).objects.empty());
  expect_session_face_selection(session, first.value(), std::nullopt);
  expect_session_face_selection(session, second.value(), std::nullopt);
  expect_session_object_selected(session, first.value(), false);
  expect_session_object_selected(session, second.value(), false);

  PolygonSelectionSnapshot noncanonical_selection =
      single_face_selection(first_face);
  noncanonical_selection.has_active = false;
  noncanonical_selection.active_face = {};
  const std::array<SessionComponentSelectionUpdate, 1>
      noncanonical_updates{{
          {.object = first.value(),
           .selection = std::move(noncanonical_selection)},
      }};
  Result<OperationResult> noncanonical =
      session.commit_component_selection_batch(
          noncanonical_updates, selected_objects, second.value());
  ASSERT_FALSE(noncanonical.ok());
  EXPECT_EQ(noncanonical.error().code, ErrorCode::InvalidArgument);
  EXPECT_EQ(session.undo_depth(), undo_depth);
  EXPECT_TRUE(session.can_redo());
  EXPECT_EQ(session.content_revision(), content_revision);
  EXPECT_EQ(session.selection_revision(), selection_revision);

  const std::array<SessionComponentSelectionUpdate, 2> duplicate_updates{{
      {.object = first.value(),
       .selection = single_face_selection(first_face)},
      {.object = first.value(),
       .selection = single_face_selection(second_face)},
  }};
  Result<OperationResult> duplicate = session.commit_component_selection_batch(
      duplicate_updates, selected_objects, second.value());
  ASSERT_FALSE(duplicate.ok());
  EXPECT_EQ(duplicate.error().code, ErrorCode::InvalidArgument);
  EXPECT_EQ(session.undo_depth(), undo_depth);
  EXPECT_TRUE(session.can_redo());
  EXPECT_EQ(session.content_revision(), content_revision);
  EXPECT_EQ(session.selection_revision(), selection_revision);
  EXPECT_TRUE(session.changes_since(content_revision).objects.empty());
  expect_session_face_selection(session, first.value(), std::nullopt);
  expect_session_face_selection(session, second.value(), std::nullopt);
  expect_session_object_selected(session, first.value(), false);
  expect_session_object_selected(session, second.value(), false);
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
  EXPECT_TRUE(std::ranges::all_of(
      preview_payload.value().overlay.records,
      [](const SemanticOverlayRecord &record) {
        return record.kind == SemanticOverlayKind::Wire;
      }));

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
     SemanticOverlayScalePreservesEveryFaceAndCornerInAuthoredOrder) {
  constexpr int kSubdivisions = 128;
  Result<PolygonDocument> document_result = PolygonDocument::make_box({
      .x_subdivisions = kSubdivisions,
      .y_subdivisions = kSubdivisions,
      .z_subdivisions = kSubdivisions,
  });
  ASSERT_TRUE(document_result.ok()) << document_result.error_message();

  Result<AuthoredPolygonPayload> authored_result =
      document_result.value().authored_payload();
  ASSERT_TRUE(authored_result.ok()) << authored_result.error_message();
  AuthoredPolygonPayload authored = std::move(authored_result).value();
  ASSERT_GT(authored.vertices.size(), 50'000U);
  ASSERT_GT(authored.faces.size(), 50'000U);

  ModelingSession session;
  Result<ObjectId> object =
      session.add_document(std::move(document_result).value(), "Overlay Scale");
  ASSERT_TRUE(object.ok()) << object.error_message();
  Result<SemanticOverlayPayload> overlay_result =
      session.semantic_overlay(object.value());
  ASSERT_TRUE(overlay_result.ok()) << overlay_result.error_message();
  const SemanticOverlayPayload &overlay = overlay_result.value();

  ASSERT_EQ(overlay.records.size(), authored.faces.size());
  EXPECT_EQ(overlay.selection_revision, 0U);

  const auto vertex_key = [](VertexId id) {
    return (static_cast<std::uint64_t>(id.generation) << 32U) |
           static_cast<std::uint64_t>(id.index);
  };
  std::unordered_map<std::uint64_t, Vec3> positions;
  positions.reserve(authored.vertices.size());
  for (std::size_t index = 0; index < authored.vertices.size(); ++index) {
    positions.emplace(vertex_key(authored.vertices[index]),
                      authored.positions[index]);
  }

  std::size_t expected_point_count = 0;
  std::size_t observed_point_count = 0;
  std::size_t mismatched_records = 0;
  std::size_t mismatched_points = 0;
  for (std::size_t face_index = 0; face_index < authored.faces.size();
       ++face_index) {
    const AuthoredPolygonFacePayload &face = authored.faces[face_index];
    const SemanticOverlayRecord &record = overlay.records[face_index];
    expected_point_count += face.vertices.size() + 1U;
    observed_point_count += record.points.size();
    if (record.kind != SemanticOverlayKind::Wire ||
        record.points.size() != face.vertices.size() + 1U) {
      ++mismatched_records;
      continue;
    }
    for (std::size_t corner = 0; corner < face.vertices.size(); ++corner) {
      const auto position = positions.find(vertex_key(face.vertices[corner]));
      if (position == positions.end() ||
          !vec3_near(record.points[corner], position->second)) {
        ++mismatched_points;
      }
    }
    if (!vec3_near(record.points.back(), record.points.front())) {
      ++mismatched_points;
    }
  }

  EXPECT_EQ(observed_point_count, expected_point_count);
  EXPECT_EQ(mismatched_records, 0U);
  EXPECT_EQ(mismatched_points, 0U);
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
  const std::vector<ToolDescriptor> tool_catalog = quader.tools().catalog();
  EXPECT_EQ(std::count_if(tool_catalog.begin(), tool_catalog.end(),
                          [](const ToolDescriptor &tool) {
                            return tool.id == "extend";
                          }),
            0);

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

  BatchResult batch = quader.batch("Create and translate")
                          .create_box({.name = "Batch Body"})
                          .as("body")
                          .with_mesh("body", [](MeshHandle mesh) {
                            OperationReceipt selected =
                                mesh.faces().all().replace();
                            ASSERT_TRUE(selected.success)
                                << selected.error.message;
                            OperationReceipt translated =
                                mesh.transform().translate(
                                    {0.0F, 0.25F, 0.0F});
                            ASSERT_TRUE(translated.success)
                                << translated.error.message;
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
  const AuthoredPolygonPayload survivor_before =
      first.payloads().authored_polygon();
  std::vector<FaceId> survivor_faces_before;
  survivor_faces_before.reserve(survivor_before.faces.size());
  for (const AuthoredPolygonFacePayload &face : survivor_before.faces) {
    survivor_faces_before.push_back(face.id);
  }

  const std::array<MeshHandle, 1> sources{second};
  OperationReceipt combined = first.combine_with(sources);
  ASSERT_TRUE(combined.success) << combined.error.message;
  EXPECT_TRUE(combined.changed);
  EXPECT_TRUE(first.valid());
  EXPECT_FALSE(second.valid());
  ASSERT_EQ(quader.mesh_summaries().size(), 1U);
  EXPECT_EQ(quader.mesh_summaries().front().id, first.id());
  ASSERT_FALSE(combined.created.faces.empty());
  for (FaceId created : combined.created.faces) {
    EXPECT_EQ(std::ranges::find(survivor_faces_before, created),
              survivor_faces_before.end());
  }

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
  EXPECT_GT(first_overlay.selection_revision, 0U);
  EXPECT_TRUE(std::ranges::all_of(
      first_overlay.records, [](const SemanticOverlayRecord &record) {
        return record.kind == SemanticOverlayKind::ToolPreview;
      }));

  const ToolPreviewPayload first_combined = preview.payload(initial_request);
  EXPECT_TRUE(first_combined.valid);
  EXPECT_EQ(first_combined.mesh.content_revision, first_revision);
  EXPECT_EQ(first_combined.mesh.vertices.size(), first_mesh.vertices.size());
  EXPECT_EQ(first_combined.overlay.records.size(),
            first_overlay.records.size());
  EXPECT_EQ(first_combined.overlay.selection_revision,
            first_overlay.selection_revision);
  EXPECT_TRUE(std::ranges::all_of(
      first_combined.overlay.records,
      [](const SemanticOverlayRecord &record) {
        return record.kind == SemanticOverlayKind::ToolPreview;
      }));

  const PayloadRequest unchanged_request{
      .only_if_changed = true,
      .previous_revision = first_revision,
  };
  EXPECT_TRUE(preview.mesh_payload(unchanged_request).vertices.empty());
  EXPECT_TRUE(preview.overlay_payload(unchanged_request).records.empty());
  EXPECT_FALSE(preview.payload(unchanged_request).valid);

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
  const SemanticOverlayPayload second_overlay =
      preview.overlay_payload(updated_request);
  EXPECT_FALSE(second_overlay.records.empty());
  EXPECT_TRUE(std::ranges::all_of(
      second_overlay.records, [](const SemanticOverlayRecord &record) {
        return record.kind == SemanticOverlayKind::ToolPreview;
      }));

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

TEST(ModelingSdkConsumerTests,
     SessionHistorySnapshotRestorePublishesOneMonotonicTransition) {
  ModelingSession session;
  Result<PolygonDocument> first_document = PolygonDocument::make_box({});
  ASSERT_TRUE(first_document.ok()) << first_document.error_message();
  Result<ObjectId> first =
      session.add_document(std::move(first_document).value(), "First");
  ASSERT_TRUE(first.ok()) << first.error_message();
  ASSERT_TRUE(session.select_object(first.value()).ok());
  const ModelingSession snapshot = session;

  Result<PolygonDocument> second_document = PolygonDocument::make_box({});
  ASSERT_TRUE(second_document.ok()) << second_document.error_message();
  Result<ObjectId> second =
      session.add_document(std::move(second_document).value(), "Second");
  ASSERT_TRUE(second.ok()) << second.error_message();
  ASSERT_TRUE(session.select_object(second.value()).ok());
  const std::uint64_t content_before_restore = session.content_revision();
  const std::uint64_t selection_before_restore = session.selection_revision();

  session.restore_snapshot_as_history_transition(snapshot);
  EXPECT_GT(session.content_revision(), content_before_restore);
  EXPECT_GT(session.selection_revision(), selection_before_restore);
  EXPECT_TRUE(session.document_copy(first.value()).ok());
  EXPECT_FALSE(session.document_copy(second.value()).ok());

  Result<PolygonDocument> replacement_document = PolygonDocument::make_box({});
  ASSERT_TRUE(replacement_document.ok())
      << replacement_document.error_message();
  Result<ObjectId> replacement = session.add_document(
      std::move(replacement_document).value(), "Replacement");
  ASSERT_TRUE(replacement.ok()) << replacement.error_message();
  EXPECT_NE(replacement.value(), second.value());
}

TEST(ModelingSdkConsumerTests,
     SessionCurrentWorkspaceCheckpointDropsDeepHistoryAndRetainsHighWater) {
  constexpr std::size_t kRenameHistoryDepth = 512U;
  constexpr std::size_t kRetiredObjectCount = 32U;
  Result<PolygonDocument> template_document = PolygonDocument::make_box({});
  ASSERT_TRUE(template_document.ok()) << template_document.error_message();

  ModelingSession session;
  Result<ObjectId> primary =
      session.add_document(template_document.value(), "Checkpoint Primary");
  ASSERT_TRUE(primary.ok()) << primary.error_message();
  ASSERT_TRUE(session.select_object(primary.value()).ok());
  Result<OperationResult> copied = session.copy_selection_to_clipboard();
  ASSERT_TRUE(copied.ok()) << copied.error_message();
  ASSERT_TRUE(session.clipboard_available());

  ObjectId last_allocated = primary.value();
  for (std::size_t index = 0; index < kRetiredObjectCount; ++index) {
    Result<ObjectId> transient = session.add_document(
        template_document.value(), "Retired " + std::to_string(index));
    ASSERT_TRUE(transient.ok()) << transient.error_message();
    last_allocated = transient.value();
    Result<OperationResult> removed =
        session.remove_object(transient.value());
    ASSERT_TRUE(removed.ok()) << removed.error_message();
    ASSERT_TRUE(removed.value().changed);
  }
  for (std::size_t index = 0; index < kRenameHistoryDepth; ++index) {
    Result<OperationResult> renamed = session.rename_object(
        primary.value(), "Checkpoint " + std::to_string(index));
    ASSERT_TRUE(renamed.ok()) << renamed.error_message();
    ASSERT_TRUE(renamed.value().changed);
  }

  Result<PolygonDocument> edited = session.document_copy(primary.value());
  ASSERT_TRUE(edited.ok()) << edited.error_message();
  const FaceId source_face = edited.value().face_ids().front();
  ASSERT_TRUE(session.select_face(primary.value(), source_face).ok());
  edited = session.document_copy(primary.value());
  ASSERT_TRUE(edited.ok()) << edited.error_message();
  Result<OperationResult> extruded =
      edited.value().extrude_selected_faces(0.25F);
  ASSERT_TRUE(extruded.ok()) << extruded.error_message();
  ASSERT_TRUE(extruded.value().changed);
  ASSERT_FALSE(extruded.value().created.faces.empty());
  const FaceId abandoned_face = extruded.value().created.faces.front();
  OperationResult extrude_receipt = extruded.value();
  Result<OperationResult> committed = session.commit_document(
      primary.value(), std::move(edited).value(),
      std::move(extrude_receipt));
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  ASSERT_TRUE(session.undo().ok());
  ASSERT_TRUE(session.can_redo());

  Result<OperationResult> preview =
      session.begin_translate_preview(primary.value(), {0.5F, 0.0F, 0.0F});
  ASSERT_TRUE(preview.ok()) << preview.error_message();
  Result<ToolPreviewPayload> preview_payload = session.preview_payload();
  ASSERT_TRUE(preview_payload.ok()) << preview_payload.error_message();
  ASSERT_TRUE(preview_payload.value().valid);
  const PreviewId allocated_preview = session.preview_summary().id;
  ASSERT_TRUE(allocated_preview.valid());

  const std::vector<SessionObjectSummary> expected_objects =
      session.objects();
  const std::uint64_t expected_content_revision = session.content_revision();
  const std::uint64_t expected_selection_revision =
      session.selection_revision();
  ASSERT_GT(session.undo_depth(), kRenameHistoryDepth);

  ModelingSession checkpoint = session.current_workspace_checkpoint();
  EXPECT_TRUE(session.preview_active());
  EXPECT_TRUE(session.can_redo());
  EXPECT_EQ(checkpoint.undo_depth(), 0U);
  EXPECT_FALSE(checkpoint.can_undo());
  EXPECT_FALSE(checkpoint.can_redo());
  EXPECT_FALSE(checkpoint.preview_active());
  EXPECT_FALSE(checkpoint.preview_payload().ok());
  EXPECT_TRUE(checkpoint.clipboard_available());
  EXPECT_EQ(checkpoint.clipboard_object_count(), 1U);
  EXPECT_EQ(checkpoint.content_revision(), expected_content_revision);
  EXPECT_EQ(checkpoint.selection_revision(), expected_selection_revision);

  const std::vector<SessionObjectSummary> checkpoint_objects =
      checkpoint.objects();
  ASSERT_EQ(checkpoint_objects.size(), expected_objects.size());
  for (std::size_t index = 0; index < expected_objects.size(); ++index) {
    EXPECT_EQ(checkpoint_objects[index].id, expected_objects[index].id);
    EXPECT_EQ(checkpoint_objects[index].name, expected_objects[index].name);
    EXPECT_EQ(checkpoint_objects[index].selected,
              expected_objects[index].selected);
    EXPECT_EQ(checkpoint_objects[index].content_revision,
              expected_objects[index].content_revision);
    EXPECT_EQ(checkpoint_objects[index].selection_revision,
              expected_objects[index].selection_revision);
  }

  for (std::size_t index = 0; index < 32U; ++index) {
    checkpoint = checkpoint.current_workspace_checkpoint();
    EXPECT_EQ(checkpoint.undo_depth(), 0U);
    EXPECT_FALSE(checkpoint.can_redo());
  }

  ModelingSession restored;
  Result<ObjectId> discarded = restored.add_document(
      template_document.value(), "Discarded Restore Target");
  ASSERT_TRUE(discarded.ok()) << discarded.error_message();
  restored.restore_snapshot_as_history_transition(checkpoint);
  EXPECT_EQ(restored.undo_depth(), 0U);
  EXPECT_FALSE(restored.can_undo());
  EXPECT_FALSE(restored.can_redo());
  EXPECT_FALSE(restored.preview_active());
  ASSERT_EQ(restored.objects().size(), expected_objects.size());
  EXPECT_EQ(restored.objects().front().id, primary.value());
  EXPECT_EQ(restored.objects().front().name, expected_objects.front().name);

  Result<OperationResult> next_preview =
      restored.begin_translate_preview(primary.value(),
                                       {0.25F, 0.0F, 0.0F});
  ASSERT_TRUE(next_preview.ok()) << next_preview.error_message();
  EXPECT_GT(restored.preview_summary().id.index, allocated_preview.index);
  ASSERT_TRUE(restored.cancel_preview().ok());

  Result<ObjectId> replacement = restored.add_document(
      template_document.value(), "Post-checkpoint Object");
  ASSERT_TRUE(replacement.ok()) << replacement.error_message();
  EXPECT_GT(replacement.value().index, last_allocated.index);

  Result<PolygonDocument> topology_branch =
      restored.document_copy(primary.value());
  ASSERT_TRUE(topology_branch.ok()) << topology_branch.error_message();
  Result<AuthoredTopology> topology =
      topology_branch.value().authored_topology();
  ASSERT_TRUE(topology.ok()) << topology.error_message();
  EXPECT_GT(topology.value().next_face_id, abandoned_face.index);
  ASSERT_TRUE(topology_branch.value().select_face(source_face).ok());
  Result<OperationResult> branch_extruded =
      topology_branch.value().extrude_selected_faces(0.5F);
  ASSERT_TRUE(branch_extruded.ok()) << branch_extruded.error_message();
  EXPECT_EQ(std::ranges::find(branch_extruded.value().created.faces,
                              abandoned_face),
            branch_extruded.value().created.faces.end());
  OperationResult branch_receipt = branch_extruded.value();
  Result<OperationResult> branch_committed = restored.commit_document(
      primary.value(), std::move(topology_branch).value(),
      std::move(branch_receipt));
  ASSERT_TRUE(branch_committed.ok()) << branch_committed.error_message();
}

TEST(ModelingSdkConsumerTests,
     ResultArmsAndTypedSelectionsRejectContradictoryStates) {
  Result<NonDefaultResultValue> uninitialized;
  ASSERT_FALSE(uninitialized.ok());
  EXPECT_EQ(uninitialized.error().code, ErrorCode::InternalError);
  EXPECT_THROW(static_cast<void>(uninitialized.value()), std::logic_error);

  Result<NonDefaultResultValue> success =
      Result<NonDefaultResultValue>::success(NonDefaultResultValue{42});
  ASSERT_TRUE(success.ok());
  EXPECT_EQ(success.value().value, 42);
  EXPECT_THROW(static_cast<void>(success.error()), std::logic_error);

  Result<NonDefaultResultValue> normalized_failure =
      Result<NonDefaultResultValue>::failure(Error{});
  ASSERT_FALSE(normalized_failure.ok());
  EXPECT_EQ(normalized_failure.error().code, ErrorCode::InternalError);

  EXPECT_EQ(ObjectSelection{}.kind(), SelectionKind::Object);
  EXPECT_EQ(VertexSelection{}.kind(), SelectionKind::Vertex);
  EXPECT_EQ(EdgeSelection{}.kind(), SelectionKind::Edge);
  EXPECT_EQ(FaceSelection{}.kind(), SelectionKind::Face);

  SelectionUnion wrong_kind;
  wrong_kind.kind = SelectionKind::Face;
  EXPECT_THROW(
      VertexSelection(std::shared_ptr<ModelingApiContext>{}, wrong_kind),
      std::invalid_argument);

  SelectionUnion mixed_payload;
  mixed_payload.kind = SelectionKind::Vertex;
  mixed_payload.faces.push_back(make_id<FaceTag>(1U));
  EXPECT_THROW(
      Selection(std::shared_ptr<ModelingApiContext>{}, mixed_payload),
      std::invalid_argument);

  ModelingApi quader = ModelingApi::create();
  MeshHandle mesh = quader.create_box({.name = "Receipt"});
  ASSERT_TRUE(mesh.valid());
  OperationReceipt unchanged = mesh.rename("Receipt");
  EXPECT_TRUE(unchanged.success);
  EXPECT_FALSE(unchanged.changed);
  EXPECT_EQ(unchanged.error.code, ErrorCode::Ok);
  EXPECT_TRUE(unchanged.error.message.empty());
}

TEST(ModelingSdkConsumerTests,
     TopologyIdentityNeverAliasesAcrossDeletionUndoBranchAndQdr) {
  Result<PolygonDocument> direct_result = PolygonDocument::make_box({});
  ASSERT_TRUE(direct_result.ok()) << direct_result.error_message();
  PolygonDocument direct = std::move(direct_result).value();
  const FaceId deleted_face = direct.face_ids().front();
  ASSERT_TRUE(direct.select_face(deleted_face).ok());
  Result<OperationResult> deleted = direct.delete_selection();
  ASSERT_TRUE(deleted.ok()) << deleted.error_message();
  ASSERT_TRUE(deleted.value().changed);

  Result<OperationResult> stale_deleted = direct.select_face(deleted_face);
  ASSERT_FALSE(stale_deleted.ok());
  EXPECT_EQ(stale_deleted.error().code, ErrorCode::StaleId);
  Result<AuthoredTopology> direct_topology = direct.authored_topology();
  ASSERT_TRUE(direct_topology.ok()) << direct_topology.error_message();
  Result<OperationResult> never_issued = direct.select_face(
      make_id<FaceTag>(direct_topology.value().next_face_id + 100U));
  ASSERT_FALSE(never_issued.ok());
  EXPECT_EQ(never_issued.error().code, ErrorCode::InvalidId);

  Result<std::vector<std::byte>> encoded =
      serialize_qdr_authored_topology(direct);
  ASSERT_TRUE(encoded.ok()) << encoded.error_message();
  Result<PolygonDocument> decoded =
      deserialize_qdr_authored_topology(encoded.value());
  ASSERT_TRUE(decoded.ok()) << decoded.error_message();
  Result<OperationResult> stale_after_qdr =
      decoded.value().select_face(deleted_face);
  ASSERT_FALSE(stale_after_qdr.ok());
  EXPECT_EQ(stale_after_qdr.error().code, ErrorCode::StaleId);

  Result<PolygonDocument> edge_result = PolygonDocument::make_box({});
  ASSERT_TRUE(edge_result.ok()) << edge_result.error_message();
  PolygonDocument edge_document = std::move(edge_result).value();
  const EdgeKey removed_edge = edge_document.edge_ids().front();
  ASSERT_TRUE(edge_document.select_all_faces().ok());
  ASSERT_TRUE(edge_document.delete_selection().ok());
  Result<OperationResult> stale_edge =
      edge_document.select_edge(removed_edge);
  ASSERT_FALSE(stale_edge.ok());
  EXPECT_EQ(stale_edge.error().code, ErrorCode::StaleId);
  Result<AuthoredTopology> empty_topology = edge_document.authored_topology();
  ASSERT_TRUE(empty_topology.ok()) << empty_topology.error_message();
  const VertexId never_a =
      make_id<VertexTag>(empty_topology.value().next_vertex_id + 10U);
  const VertexId never_b =
      make_id<VertexTag>(empty_topology.value().next_vertex_id + 11U);
  Result<OperationResult> invalid_edge =
      edge_document.select_edge(make_edge_key(never_a, never_b));
  ASSERT_FALSE(invalid_edge.ok());
  EXPECT_EQ(invalid_edge.error().code, ErrorCode::InvalidId);

  Result<PolygonDocument> copy_source_result = PolygonDocument::make_box({});
  ASSERT_TRUE(copy_source_result.ok()) << copy_source_result.error_message();
  PolygonDocument copy_source = std::move(copy_source_result).value();
  PolygonDocument independent_copy = copy_source;
  const FaceId copy_face = copy_source.face_ids().front();
  ASSERT_TRUE(copy_source.select_face(copy_face).ok());
  ASSERT_TRUE(independent_copy.select_face(copy_face).ok());
  Result<OperationResult> source_extrude =
      copy_source.extrude_selected_faces(0.2F);
  Result<OperationResult> copy_extrude =
      independent_copy.extrude_selected_faces(0.4F);
  ASSERT_TRUE(source_extrude.ok()) << source_extrude.error_message();
  ASSERT_TRUE(copy_extrude.ok()) << copy_extrude.error_message();
  EXPECT_EQ(source_extrude.value().created.faces,
            copy_extrude.value().created.faces);

  Result<PolygonDocument> initial_result = PolygonDocument::make_box({});
  ASSERT_TRUE(initial_result.ok()) << initial_result.error_message();
  ModelingSession session;
  Result<ObjectId> object =
      session.add_document(std::move(initial_result).value(), "Identity Box");
  ASSERT_TRUE(object.ok()) << object.error_message();
  Result<PolygonDocument> stale_copy = session.document_copy(object.value());
  ASSERT_TRUE(stale_copy.ok()) << stale_copy.error_message();

  Result<PolygonDocument> future = session.document_copy(object.value());
  ASSERT_TRUE(future.ok()) << future.error_message();
  const FaceId source_face = future.value().face_ids().front();
  ASSERT_TRUE(future.value().select_face(source_face).ok());
  Result<OperationResult> future_edit =
      future.value().extrude_selected_faces(0.25F);
  ASSERT_TRUE(future_edit.ok()) << future_edit.error_message();
  ASSERT_FALSE(future_edit.value().created.faces.empty());
  const FaceId issued_on_abandoned_branch =
      future_edit.value().created.faces.front();
  OperationResult future_receipt = future_edit.value();
  ASSERT_TRUE(session.commit_document(object.value(),
                                      std::move(future).value(),
                                      std::move(future_receipt))
                  .ok());
  OperationResult stale_commit_receipt;
  stale_commit_receipt.changed = true;
  stale_commit_receipt.dirty.topology = true;
  Result<OperationResult> stale_commit = session.commit_document(
      object.value(), std::move(stale_copy).value(), stale_commit_receipt);
  ASSERT_FALSE(stale_commit.ok());
  EXPECT_EQ(stale_commit.error().code, ErrorCode::StaleId);
  ASSERT_TRUE(session.undo().ok());

  Result<PolygonDocument> branch = session.document_copy(object.value());
  ASSERT_TRUE(branch.ok()) << branch.error_message();
  ASSERT_TRUE(branch.value().select_face(source_face).ok());
  Result<OperationResult> branch_edit =
      branch.value().extrude_selected_faces(0.5F);
  ASSERT_TRUE(branch_edit.ok()) << branch_edit.error_message();
  ASSERT_FALSE(branch_edit.value().created.faces.empty());
  EXPECT_EQ(std::ranges::find(branch_edit.value().created.faces,
                              issued_on_abandoned_branch),
            branch_edit.value().created.faces.end());
  OperationResult branch_receipt = branch_edit.value();
  ASSERT_TRUE(session.commit_document(object.value(),
                                      std::move(branch).value(),
                                      std::move(branch_receipt))
                  .ok());

  Result<OperationResult> abandoned_id =
      session.select_face(object.value(), issued_on_abandoned_branch);
  ASSERT_FALSE(abandoned_id.ok());
  EXPECT_EQ(abandoned_id.error().code, ErrorCode::StaleId);
}

TEST(ModelingSdkConsumerTests,
     PublicAuthoredTopologyRoundTripPreservesExactPortablePayload) {
  Result<PolygonDocument> document_result = PolygonDocument::make_box({});
  ASSERT_TRUE(document_result.ok()) << document_result.error_message();
  PolygonDocument document = std::move(document_result).value();
  const FaceId removed = document.face_ids().front();
  ASSERT_TRUE(document.select_face(removed).ok());
  ASSERT_TRUE(document.delete_selection().ok());

  Result<AuthoredTopology> exported = document.authored_topology();
  ASSERT_TRUE(exported.ok()) << exported.error_message();
  Result<PolygonDocument> imported =
      PolygonDocument::from_authored_topology(exported.value());
  ASSERT_TRUE(imported.ok()) << imported.error_message();
  Result<AuthoredTopology> reexported = imported.value().authored_topology();
  ASSERT_TRUE(reexported.ok()) << reexported.error_message();
  EXPECT_EQ(reexported.value().next_vertex_id,
            exported.value().next_vertex_id);
  EXPECT_EQ(reexported.value().next_face_id, exported.value().next_face_id);
  EXPECT_EQ(reexported.value().vertices.size(), exported.value().vertices.size());
  EXPECT_EQ(reexported.value().faces.size(), exported.value().faces.size());
  EXPECT_EQ(reexported.value().hard_normal_edges,
            exported.value().hard_normal_edges);

  Result<std::vector<std::byte>> original_bytes =
      serialize_qdr_authored_topology(document);
  Result<std::vector<std::byte>> imported_bytes =
      serialize_qdr_authored_topology(imported.value());
  ASSERT_TRUE(original_bytes.ok()) << original_bytes.error_message();
  ASSERT_TRUE(imported_bytes.ok()) << imported_bytes.error_message();
  EXPECT_EQ(imported_bytes.value(), original_bytes.value());
}

TEST(ModelingSdkConsumerTests,
     PreviewHandlesShareOneRegistryAndReleaseExactlyOnce) {
  ModelingApi quader = ModelingApi::create({
      .error_policy = ErrorPolicy::StoreLastError,
  });
  MeshHandle mesh = quader.create_box({.name = "Preview Registry"});
  ASSERT_TRUE(mesh.valid());
  ASSERT_TRUE(mesh.faces().all().replace().success);

  PreviewHandle first = mesh.transform().preview();
  ASSERT_TRUE(first.active());
  ASSERT_TRUE(first.id().valid());
  PreviewHandle copied = first;
  EXPECT_EQ(copied.id(), first.id());
  EXPECT_TRUE(copied.active());
  EXPECT_TRUE(quader.preview().active());
  EXPECT_EQ(quader.preview().summary().id, first.id());

  PreviewHandle rejected = mesh.transform().preview();
  EXPECT_FALSE(rejected.active());
  EXPECT_FALSE(rejected.id().valid());
  ASSERT_NE(quader.last_error(), nullptr);
  EXPECT_EQ(quader.last_error()->code, ErrorCode::InvalidArgument);

  OperationReceipt blocked_undo = quader.undo();
  EXPECT_FALSE(blocked_undo.success);
  EXPECT_TRUE(first.active());
  ASSERT_TRUE(copied.cancel().success);
  EXPECT_FALSE(first.active());
  EXPECT_FALSE(quader.preview().active());
  EXPECT_FALSE(first.cancel().success);

  quader.clear_error();
  PreviewId released_id{};
  {
    PreviewHandle owner = mesh.transform().preview();
    ASSERT_TRUE(owner.active());
    released_id = owner.id();
    {
      PreviewHandle second_owner = owner;
      EXPECT_TRUE(second_owner.active());
    }
    EXPECT_TRUE(owner.active());
  }
  EXPECT_FALSE(quader.preview().active());

  PreviewHandle next = mesh.transform().preview();
  ASSERT_TRUE(next.active());
  EXPECT_NE(next.id(), released_id);
  EXPECT_TRUE(next.cancel().success);

  quader.clear_error();
  TransformTool move = quader.tools().activate_move_tool();
  OperationReceipt direct_begin = move.begin({1.0F, 0.0F, 0.0F});
  ASSERT_TRUE(direct_begin.success) << direct_begin.error.message;
  ASSERT_TRUE(quader.preview().active());
  PreviewHandle blocked_by_direct_preview = mesh.transform().preview();
  EXPECT_FALSE(blocked_by_direct_preview.active());
  EXPECT_TRUE(quader.preview().cancel().success);
  EXPECT_FALSE(quader.preview().active());
}

TEST(ModelingSdkConsumerTests,
     InvalidFacadesAndOwnedSnapshotsStaySafeAcrossSessionMutation) {
  Result<PolygonDocument> unbound_document = PolygonDocument::make_box({});
  ASSERT_TRUE(unbound_document.ok()) << unbound_document.error_message();
  CheckedModelingApi unbound;
  Result<MeshHandle> unbound_add = unbound.add_document(
      std::move(unbound_document).value(), "Unbound");
  ASSERT_FALSE(unbound_add.ok());
  EXPECT_EQ(unbound_add.error().code, ErrorCode::InvalidArgument);

  MeshHandle invalid_mesh;
  EXPECT_FALSE(invalid_mesh.valid());
  EXPECT_FALSE(invalid_mesh.id().valid());
  EXPECT_TRUE(invalid_mesh.summary().name.empty());
  EXPECT_THROW(static_cast<void>(invalid_mesh.rename("No owner")),
               ModelingException);

  Result<PolygonDocument> document = PolygonDocument::make_box({});
  ASSERT_TRUE(document.ok()) << document.error_message();
  ModelingSession session;
  Result<ObjectId> object =
      session.add_document(std::move(document).value(), "Snapshot");
  ASSERT_TRUE(object.ok()) << object.error_message();
  Result<std::shared_ptr<const PolygonDocument>> snapshot =
      session.document_snapshot(object.value());
  ASSERT_TRUE(snapshot.ok()) << snapshot.error_message();
  const std::vector<FaceId> retained_faces = snapshot.value()->face_ids();
  ASSERT_FALSE(retained_faces.empty());

  ASSERT_TRUE(session.remove_object(object.value()).ok());
  Result<PolygonDocument> replacement = PolygonDocument::make_box({});
  ASSERT_TRUE(replacement.ok()) << replacement.error_message();
  ASSERT_TRUE(
      session.add_document(std::move(replacement).value(), "Replacement").ok());
  ASSERT_TRUE(session.undo().ok());
  EXPECT_EQ(snapshot.value()->face_ids(), retained_faces);
  EXPECT_TRUE(snapshot.value()->validate().ok());
}

TEST(ModelingSdkConsumerTests,
     SnapshotLifetimeRequestsAreOwnedOrExplicitlyUnsupported) {
  ModelingApi quader = ModelingApi::create({
      .error_policy = ErrorPolicy::StoreLastError,
  });
  MeshHandle mesh = quader.create_box({.name = "Lifetime"});
  ASSERT_TRUE(mesh.valid());

  MeshPayload owned = mesh.payloads().compile_mesh({
      .lifetime = SnapshotLifetime::Owned,
  });
  ASSERT_FALSE(owned.vertices.empty());
  const std::size_t owned_vertex_count = owned.vertices.size();
  RenderSnapshotPayload owned_scene = quader.render().scene_snapshot();
  EXPECT_EQ(owned_scene.lifetime, SnapshotLifetime::Owned);
  ASSERT_FALSE(owned_scene.meshes.empty());

  MeshPayload pinned = mesh.payloads().compile_mesh({
      .lifetime = SnapshotLifetime::Pinned,
  });
  EXPECT_TRUE(pinned.vertices.empty());
  ASSERT_NE(quader.last_error(), nullptr);
  EXPECT_EQ(quader.last_error()->code, ErrorCode::UnsupportedOperation);

  quader.clear_error();
  MeshCompileResult borrowed = mesh.payloads().compile_mesh_if_changed(
      mesh.revisions().content,
      {.lifetime = SnapshotLifetime::BorrowedSameFrame});
  EXPECT_FALSE(borrowed.changed);
  EXPECT_FALSE(borrowed.reused_previous);
  ASSERT_NE(quader.last_error(), nullptr);
  EXPECT_EQ(quader.last_error()->code, ErrorCode::UnsupportedOperation);

  quader.clear_error();
  RenderSnapshotPayload unsupported_scene = quader.render().scene_snapshot({
      .lifetime = SnapshotLifetime::Pinned,
  });
  EXPECT_EQ(unsupported_scene.lifetime, SnapshotLifetime::Owned);
  EXPECT_TRUE(unsupported_scene.meshes.empty());
  ASSERT_NE(quader.last_error(), nullptr);
  EXPECT_EQ(quader.last_error()->code, ErrorCode::UnsupportedOperation);

  ASSERT_TRUE(mesh.faces().all().replace().success);
  PreviewHandle preview = mesh.transform().preview();
  ASSERT_TRUE(preview.active());
  quader.clear_error();
  ToolPreviewPayload unsupported_preview = preview.payload({
      .lifetime = SnapshotLifetime::BorrowedSameFrame,
  });
  EXPECT_FALSE(unsupported_preview.valid);
  ASSERT_NE(quader.last_error(), nullptr);
  EXPECT_EQ(quader.last_error()->code, ErrorCode::UnsupportedOperation);
  ASSERT_TRUE(preview.cancel().success);

  ASSERT_TRUE(mesh.destroy().success);
  EXPECT_EQ(owned.vertices.size(), owned_vertex_count);
  EXPECT_FALSE(owned.indices.empty());
  EXPECT_FALSE(owned_scene.meshes.front().vertices.empty());
}

TEST(ModelingSdkConsumerTests,
     CheckedBatchReceiptsAreAtomicAndGuardReentrantMutation) {
  ModelingApi successful = ModelingApi::create({
      .error_policy = ErrorPolicy::StoreLastError,
  });
  BatchResult no_op =
      successful.batch("Checked no-op")
          .create_box({.name = "Body"})
          .as("body")
          .with_mesh_checked("body", [](MeshHandle mesh) {
            return mesh.rename("Body");
          })
          .commit();
  ASSERT_TRUE(no_op.success()) << no_op.receipt.error.message;
  ASSERT_EQ(no_op.step_receipts.size(), 2U);
  EXPECT_TRUE(no_op.step_receipts.front().changed);
  EXPECT_FALSE(no_op.step_receipts.back().changed);
  EXPECT_EQ(successful.mesh_summaries().size(), 1U);
  EXPECT_EQ(successful.revisions().content, no_op.receipt.revisions.content);

  ModelingApi mismatch = ModelingApi::create({
      .error_policy = ErrorPolicy::StoreLastError,
  });
  BatchResult mismatched =
      mismatch.batch("Mismatched receipt")
          .create_box({.name = "Transient"})
          .as("body")
          .with_mesh_checked("body", [](MeshHandle) {
            OperationReceipt receipt;
            receipt.changed = true;
            receipt.dirty.topology = true;
            return receipt;
          })
          .commit();
  EXPECT_FALSE(mismatched.success());
  EXPECT_TRUE(mismatched.meshes.empty());
  EXPECT_TRUE(mismatch.mesh_summaries().empty());
  EXPECT_FALSE(mismatch.can_undo());

  ModelingApi reentrant = ModelingApi::create({
      .error_policy = ErrorPolicy::StoreLastError,
  });
  BatchResult blocked =
      reentrant.batch("Blocked undo")
          .create_box({.name = "Transient"})
          .as("body")
          .with_mesh_checked("body", [&reentrant](MeshHandle) {
            return reentrant.undo();
          })
          .commit();
  EXPECT_FALSE(blocked.success());
  EXPECT_TRUE(blocked.meshes.empty());
  EXPECT_TRUE(reentrant.mesh_summaries().empty());
  EXPECT_FALSE(reentrant.can_undo());

  reentrant.clear_error();
  BatchResult unrelated =
      reentrant.batch("Blocked unrelated create")
          .create_box({.name = "Scheduled"})
          .as("scheduled")
          .with_mesh_checked("scheduled", [&reentrant](MeshHandle) {
            static_cast<void>(
                reentrant.create_box({.name = "Unscheduled"}));
            return OperationReceipt{};
          })
          .commit();
  EXPECT_FALSE(unrelated.success());
  EXPECT_TRUE(unrelated.meshes.empty());
  EXPECT_TRUE(reentrant.mesh_summaries().empty());
  EXPECT_FALSE(reentrant.can_undo());

  reentrant.clear_error();
  BatchResult nested =
      reentrant.batch("Blocked nested batch")
          .create_box({.name = "Outer"})
          .as("outer")
          .with_mesh_checked("outer", [&reentrant](MeshHandle) {
            return reentrant.batch("Nested")
                .create_box({.name = "Inner"})
                .as("inner")
                .commit()
                .receipt;
          })
          .commit();
  EXPECT_FALSE(nested.success());
  EXPECT_TRUE(nested.meshes.empty());
  EXPECT_TRUE(reentrant.mesh_summaries().empty());
  EXPECT_FALSE(reentrant.can_undo());

  BatchResult recovered =
      reentrant.batch("Recovered")
          .create_box({.name = "Permanent"})
          .as("permanent")
          .commit();
  EXPECT_TRUE(recovered.success()) << recovered.receipt.error.message;
  EXPECT_EQ(reentrant.mesh_summaries().size(), 1U);
}

TEST(ModelingSdkConsumerTests,
     UnsupportedSdkPlaceholdersAreDeprecatedAndNotAdvertised) {
  ModelingApi quader = ModelingApi::create({
      .error_policy = ErrorPolicy::StoreLastError,
  });
  const std::vector<ToolDescriptor> tools = quader.tools().catalog();
  EXPECT_EQ(std::ranges::find_if(tools, [](const ToolDescriptor &tool) {
              return tool.id == "pivot";
            }),
            tools.end());

  PivotTool pivot = quader.tools().activate_pivot_tool();
  OperationReceipt pivot_result = pivot.commit();
  EXPECT_FALSE(pivot_result.success);
  EXPECT_EQ(pivot_result.error.code, ErrorCode::UnsupportedOperation);

  ProfilingApi profiling = quader.profiling();
  Result<ProfilingReport> report = profiling.report();
  ASSERT_FALSE(report.ok());
  EXPECT_EQ(report.error().code, ErrorCode::UnsupportedOperation);
}

} // namespace

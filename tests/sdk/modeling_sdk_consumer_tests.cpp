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

[[nodiscard]] std::size_t
authored_open_edge_count(const AuthoredPolygonPayload &payload) {
  std::vector<EdgeKey> edges;
  for (const AuthoredPolygonFacePayload &face : payload.faces) {
    for (std::size_t index = 0; index < face.vertices.size(); ++index) {
      edges.push_back(make_edge_key(
          face.vertices[index],
          face.vertices[(index + 1U) % face.vertices.size()]));
    }
  }
  std::ranges::sort(edges);

  std::size_t open_edges = 0;
  for (std::size_t first = 0; first < edges.size();) {
    std::size_t after = first + 1U;
    while (after < edges.size() && edges[after] == edges[first]) {
      ++after;
    }
    if (after - first == 1U) {
      ++open_edges;
    }
    first = after;
  }
  return open_edges;
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
      face.texture_transform = {
          .scale = {2.0F, 0.5F}, .shift_uv = {0.25F, -0.5F},
          .rotation_degrees = 35.0F,
      };
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
    for (const auto &face : result.value().faces) {
      EXPECT_EQ(face.texture_transform, authored.value().faces.front().texture_transform);
    }
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

TEST(ModelingSdkConsumerTests,
     PolygonSelectionPreservesExplicitAndPreviousActiveComponents) {
  Result<PolygonDocument> document_result = PolygonDocument::make_box({});
  ASSERT_TRUE(document_result.ok()) << document_result.error_message();
  PolygonDocument document = std::move(document_result).value();

  const std::vector<VertexId> vertices = document.vertex_ids();
  const std::vector<EdgeKey> edges = document.edge_ids();
  const std::vector<FaceId> faces = document.face_ids();
  ASSERT_GE(vertices.size(), 4U);
  ASSERT_GE(edges.size(), 3U);
  ASSERT_GE(faces.size(), 4U);

  const PolygonSelectionSnapshot vertex_replacement{
      .kind = SelectionKind::Vertex,
      .vertices = {vertices[0], vertices[1], vertices[2]},
      .has_active = true,
      .active_vertex = vertices[2],
  };
  ASSERT_TRUE(document
                  .apply_selection(vertex_replacement, SelectionEdit::Replace)
                  .ok());
  EXPECT_EQ(document.selection().vertices, vertex_replacement.vertices);
  EXPECT_EQ(document.selection().active_vertex, vertices[2]);

  const PolygonSelectionSnapshot edge_replacement{
      .kind = SelectionKind::Edge,
      .edges = {edges[0], edges[1], edges[2]},
      .has_active = true,
      .active_edge = edges[2],
  };
  ASSERT_TRUE(document
                  .apply_selection(edge_replacement, SelectionEdit::Replace)
                  .ok());
  EXPECT_EQ(document.selection().edges, edge_replacement.edges);
  EXPECT_EQ(document.selection().active_edge, edges[2]);

  const PolygonSelectionSnapshot face_replacement{
      .kind = SelectionKind::Face,
      .faces = {faces[0], faces[1]},
      .has_active = true,
      .active_face = faces[1],
  };
  ASSERT_TRUE(document
                  .apply_selection(face_replacement, SelectionEdit::Replace)
                  .ok());
  EXPECT_EQ(document.selection().faces, face_replacement.faces);
  EXPECT_EQ(document.selection().active_face, faces[1]);

  const PolygonSelectionSnapshot add_face{
      .kind = SelectionKind::Face,
      .faces = {faces[2]},
      .has_active = true,
      .active_face = faces[2],
  };
  ASSERT_TRUE(document.apply_selection(add_face, SelectionEdit::Add).ok());
  EXPECT_EQ(document.selection().active_face, faces[2]);

  const PolygonSelectionSnapshot remove_non_active{
      .kind = SelectionKind::Face,
      .faces = {faces[0]},
      .has_active = true,
      .active_face = faces[0],
  };
  ASSERT_TRUE(
      document.apply_selection(remove_non_active, SelectionEdit::Remove).ok());
  EXPECT_EQ(document.selection().active_face, faces[2]);

  const PolygonSelectionSnapshot toggle_active{
      .kind = SelectionKind::Face,
      .faces = {faces[2]},
      .has_active = true,
      .active_face = faces[2],
  };
  ASSERT_TRUE(
      document.apply_selection(toggle_active, SelectionEdit::Toggle).ok());
  ASSERT_EQ(document.selection().faces.size(), 1U);
  EXPECT_EQ(document.selection().active_face, faces[1]);

  const PolygonSelectionSnapshot before_invalid = document.selection();
  const std::uint64_t revision_before_invalid = document.selection_revision();
  const PolygonSelectionSnapshot invalid_replacement{
      .kind = SelectionKind::Face,
      .faces = {faces[0], faces[1]},
      .has_active = true,
      .active_face = faces[3],
  };
  const Result<OperationResult> invalid = document.apply_selection(
      invalid_replacement, SelectionEdit::Replace);
  ASSERT_FALSE(invalid.ok());
  EXPECT_EQ(invalid.error().code, ErrorCode::InvalidArgument);
  EXPECT_EQ(document.selection().faces, before_invalid.faces);
  EXPECT_EQ(document.selection().active_face, before_invalid.active_face);
  EXPECT_EQ(document.selection_revision(), revision_before_invalid);
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
     SessionComponentSelectionHistoryPreservesNonFrontActiveFace) {
  Result<PolygonDocument> document_result = PolygonDocument::make_box({});
  ASSERT_TRUE(document_result.ok()) << document_result.error_message();
  const std::vector<FaceId> faces = document_result.value().face_ids();
  ASSERT_GE(faces.size(), 3U);

  ModelingSession session;
  Result<ObjectId> object =
      session.add_document(std::move(document_result).value(), "Box");
  ASSERT_TRUE(object.ok()) << object.error_message();

  const PolygonSelectionSnapshot replacement{
      .kind = SelectionKind::Face,
      .faces = {faces[0], faces[1]},
      .has_active = true,
      .active_face = faces[1],
  };
  const std::array<SessionComponentSelectionUpdate, 1> updates{{
      {.object = object.value(), .selection = replacement},
  }};
  const std::array<ObjectId, 1> selected_objects{object.value()};

  PolygonSelectionSnapshot invalid_replacement = replacement;
  invalid_replacement.active_face = faces[2];
  const std::array<SessionComponentSelectionUpdate, 1> invalid_updates{{
      {.object = object.value(), .selection = invalid_replacement},
  }};
  const std::size_t undo_depth_before = session.undo_depth();
  const std::uint64_t selection_revision_before =
      session.selection_revision();
  Result<OperationResult> invalid = session.commit_component_selection_batch(
      invalid_updates, selected_objects, object.value());
  ASSERT_FALSE(invalid.ok());
  EXPECT_EQ(invalid.error().code, ErrorCode::InvalidArgument);
  EXPECT_EQ(session.undo_depth(), undo_depth_before);
  EXPECT_EQ(session.selection_revision(), selection_revision_before);
  Result<PolygonDocument> selected = session.document_copy(object.value());
  ASSERT_TRUE(selected.ok()) << selected.error_message();
  EXPECT_TRUE(selected.value().selection().faces.empty());

  Result<OperationResult> committed = session.commit_component_selection_batch(
      updates, selected_objects, object.value());
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  selected = session.document_copy(object.value());
  ASSERT_TRUE(selected.ok()) << selected.error_message();
  EXPECT_EQ(selected.value().selection().faces, replacement.faces);
  EXPECT_EQ(selected.value().selection().active_face, faces[1]);

  ASSERT_TRUE(session.undo().ok());
  selected = session.document_copy(object.value());
  ASSERT_TRUE(selected.ok()) << selected.error_message();
  EXPECT_TRUE(selected.value().selection().faces.empty());
  EXPECT_FALSE(selected.value().selection().has_active);

  ASSERT_TRUE(session.redo().ok());
  selected = session.document_copy(object.value());
  ASSERT_TRUE(selected.ok()) << selected.error_message();
  EXPECT_EQ(selected.value().selection().faces, replacement.faces);
  EXPECT_EQ(selected.value().selection().active_face, faces[1]);
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

TEST(ModelingSdkConsumerTests,
     PolyToolFaceElementExtrudeKeepsTheStartingFaceClosed) {
  ModelingApi quader = ModelingApi::create();

  OperationReceipt created =
      quader.tools()
          .activate_poly_tool()
          .add_point({0.0F, 0.0F, 0.0F})
          .add_point({2.0F, 0.0F, 0.0F})
          .add_point({2.0F, 0.0F, 2.0F})
          .add_point({0.0F, 0.0F, 2.0F})
          .commit();
  ASSERT_TRUE(created.success) << created.error.message;

  const std::vector<MeshHandle> selected = quader.selected_meshes();
  ASSERT_EQ(selected.size(), 1U);
  Result<std::vector<std::byte>> encoded =
      quader.checked().io().serialize_qdr(selected.front().id());
  ASSERT_TRUE(encoded.ok()) << encoded.error_message();
  Result<QdrDocumentDto> decoded =
      deserialize_qdr_document(encoded.value());
  ASSERT_TRUE(decoded.ok()) << decoded.error_message();
  ASSERT_EQ(decoded.value().objects.size(), 1U);

  PolygonDocument document =
      std::move(decoded).value().objects.front().document;
  const std::vector<FaceId> source_faces = document.face_ids();
  ASSERT_EQ(source_faces.size(), 1U);
  Result<OperationResult> face_selected =
      document.select_face(source_faces.front());
  ASSERT_TRUE(face_selected.ok()) << face_selected.error_message();

  Result<OperationResult> extruded =
      document.extrude_selected_elements({0.0F, 1.0F, 0.0F});
  ASSERT_TRUE(extruded.ok()) << extruded.error_message();
  ASSERT_TRUE(extruded.value().changed);

  Result<AuthoredPolygonPayload> payload = document.authored_payload();
  ASSERT_TRUE(payload.ok()) << payload.error_message();
  EXPECT_EQ(payload.value().vertices.size(), 8U);
  EXPECT_EQ(payload.value().faces.size(), 6U);
  EXPECT_EQ(authored_open_edge_count(payload.value()), 0U);
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
     FaceTexturePatchesSetAbsoluteAxesWithoutTouchingOtherPropertiesOrSelection) {
  auto created = PolygonDocument::make_box({});
  ASSERT_TRUE(created.ok()) << created.error_message();
  PolygonDocument document = std::move(created).value();
  const auto ids = document.face_ids();
  const std::array<FaceId, 2> selected{ids[0], ids[1]};
  ASSERT_TRUE(document.select_faces(selected).ok());
  const PolygonDocument original = document;
  const std::array<FaceTextureTransformUpdate, 2> initial{
      FaceTextureTransformUpdate{.face = ids[0], .scale_y = 2.0F,
                                 .shift_x = 0.25F, .rotation_degrees = 30.0F},
      FaceTextureTransformUpdate{.face = ids[1], .scale_y = 4.0F,
                                 .shift_y = -0.5F, .rotation_degrees = -15.0F},
  };
  const auto first = document.set_face_texture_transforms(initial);
  ASSERT_TRUE(first.ok()) << first.error_message();
  ASSERT_TRUE(first.value().changed);
  EXPECT_TRUE(first.value().dirty.geometry);
  EXPECT_FALSE(first.value().dirty.topology);
  EXPECT_FALSE(first.value().dirty.materials);
  EXPECT_FALSE(first.value().dirty.selection);
  EXPECT_EQ(document.selection_revision(), original.selection_revision());
  EXPECT_TRUE(document.shares_selection_with(original));
  EXPECT_TRUE(document.has_same_geometry(original));
  const auto settings = document.face_texture_settings(selected);
  ASSERT_TRUE(settings.ok()) << settings.error_message();
  ASSERT_EQ(settings.value().size(), 2U);
  EXPECT_EQ(settings.value()[0].texture_transform.scale.x, 1.0F);
  EXPECT_EQ(settings.value()[1].texture_transform.scale.x, 1.0F);
  EXPECT_EQ(settings.value()[0].texture_transform.scale.y, 2.0F);
  EXPECT_EQ(settings.value()[1].texture_transform.scale.y, 4.0F);

  const std::array<FaceTextureTransformUpdate, 2> x_updates{
      FaceTextureTransformUpdate{.face = ids[0], .scale_x = 5.0F},
      FaceTextureTransformUpdate{.face = ids[1], .scale_x = 5.0F},
  };
  ASSERT_TRUE(document.set_face_texture_transforms(x_updates).ok());
  const std::array<FaceTextureTransformUpdate, 2> y_updates{
      FaceTextureTransformUpdate{.face = ids[0], .scale_y = 0.5F},
      FaceTextureTransformUpdate{.face = ids[1], .scale_y = 0.5F},
  };
  ASSERT_TRUE(document.set_face_texture_transforms(y_updates).ok());
  const auto final = document.face_texture_settings(selected);
  ASSERT_TRUE(final.ok()) << final.error_message();
  for (const auto &face : final.value()) {
    EXPECT_EQ(face.texture_transform.scale.x, 5.0F);
    EXPECT_EQ(face.texture_transform.scale.y, 0.5F);
  }
  EXPECT_EQ(final.value()[0].texture_transform.shift_uv.x, 0.25F);
  EXPECT_EQ(final.value()[1].texture_transform.shift_uv.y, -0.5F);
  EXPECT_EQ(final.value()[0].texture_transform.rotation_degrees, 30.0F);
  EXPECT_EQ(final.value()[1].texture_transform.rotation_degrees, -15.0F);
  const auto topology = document.authored_topology();
  ASSERT_TRUE(topology.ok());
  for (const auto &face : topology.value().faces) {
    EXPECT_TRUE(face.uvs.empty());
  }
  const auto identity = document.content_identity();
  const auto revision = document.content_revision();
  const auto noop = document.set_face_texture_transforms(y_updates);
  ASSERT_TRUE(noop.ok());
  EXPECT_FALSE(noop.value().changed);
  EXPECT_EQ(document.content_identity(), identity);
  EXPECT_EQ(document.content_revision(), revision);
}

[[nodiscard]] Result<PolygonDocument> make_texture_action_face(
    std::span<const Vec3> positions, std::span<const Vec2> uvs = {},
    FaceTextureTransform transform = {}) {
  auto created = PolygonDocument::make_face(positions, make_id<MaterialTag>(7U));
  if (!created.ok()) {
    return created;
  }
  auto topology = created.value().authored_topology();
  if (!topology.ok()) {
    return Result<PolygonDocument>::failure(topology.error());
  }
  auto &face = topology.value().faces.front();
  face.uvs.assign(uvs.begin(), uvs.end());
  face.texture_transform = transform;
  auto imported = PolygonDocument::from_authored_topology(topology.value());
  if (!imported.ok()) {
    return imported;
  }
  const auto selected = imported.value().select_all_faces();
  if (!selected.ok()) {
    return Result<PolygonDocument>::failure(selected.error());
  }
  return imported;
}

[[nodiscard]] FaceTextureActionTarget texture_action_target(
    const PolygonDocument &document, std::uint64_t key = 1U,
    std::uint32_t width = 64U, std::uint32_t height = 64U) {
  FaceTextureActionTarget target{.key = key, .document = &document};
  for (const FaceId face : document.face_ids()) {
    target.faces.push_back({.face = face, .texture_width = width,
                            .texture_height = height});
  }
  return target;
}

[[nodiscard]] const PreparedFaceTextureDocument *prepared_texture_document(
    const PreparedFaceTextureAction &prepared, std::uint64_t key = 1U) {
  const auto found = std::ranges::find(
      prepared.documents, key, &PreparedFaceTextureDocument::key);
  return found == prepared.documents.end() ? nullptr : &*found;
}

[[nodiscard]] Vec2 expected_texture_uv(
    Vec2 base, const FaceTextureTransform &transform) {
  constexpr double kDegreesToRadians = 0.017453292519943295;
  const double angle = transform.rotation_degrees * kDegreesToRadians;
  const double cosine = std::cos(angle);
  const double sine = std::sin(angle);
  return {
      static_cast<float>((base.x * cosine - base.y * sine) /
                            transform.scale.x +
                        transform.shift_uv.x),
      static_cast<float>((base.x * sine + base.y * cosine) /
                            transform.scale.y +
                        transform.shift_uv.y),
  };
}

void expect_vec2_near(Vec2 actual, Vec2 expected, float epsilon = 0.0001F) {
  EXPECT_NEAR(actual.x, expected.x, epsilon);
  EXPECT_NEAR(actual.y, expected.y, epsilon);
}

void expect_compiled_texture_uvs(const PolygonDocument &document,
                                std::span<const VertexId> vertices,
                                std::span<const Vec2> expected) {
  ASSERT_EQ(vertices.size(), expected.size());
  const auto compiled = document.compile_mesh();
  ASSERT_TRUE(compiled.ok()) << compiled.error_message();
  std::vector<bool> found(vertices.size());
  for (const auto &vertex : compiled.value().vertices) {
    const auto source = std::ranges::find(vertices, vertex.source_vertex);
    ASSERT_NE(source, vertices.end());
    const auto index = static_cast<std::size_t>(source - vertices.begin());
    found[index] = true;
    expect_vec2_near(vertex.uv0, expected[index]);
  }
  EXPECT_TRUE(std::ranges::all_of(found, [](bool present) { return present; }));
}

[[nodiscard]] FaceTextureTransferFace texture_transfer_face(
    const PolygonDocument &document, std::uint64_t key = 1U) {
  return {.key = key, .document = &document, .face = document.face_ids().front()};
}

[[nodiscard]] Vec2 transfer_sheared_uv(float x, float y) {
  return {0.3F + 0.4F * x + 0.2F * y, -0.7F + 0.1F * x - 0.6F * y};
}

TEST(ModelingSdkConsumerTests,
     FaceTextureTransferRotatesAroundTheSeamAndCopiesAttributesOnlyOnTarget) {
  constexpr std::array<Vec3, 4> kSourcePoints{{
      {0.0F, 0.0F, 0.0F}, {4.0F, 0.0F, 0.0F},
      {4.0F, 3.0F, 0.0F}, {0.0F, 3.0F, 0.0F},
  }};
  std::array<Vec2, 4> source_uvs;
  for (std::size_t corner = 0U; corner < source_uvs.size(); ++corner) {
    source_uvs[corner] = transfer_sheared_uv(kSourcePoints[corner].x,
                                           kSourcePoints[corner].y);
  }
  const FaceTextureTransform settings{
      .scale = {-2.0F, 0.5F}, .shift_uv = {0.31F, -0.37F},
      .rotation_degrees = 37.0F};
  auto source = make_texture_action_face(kSourcePoints, source_uvs, settings);
  ASSERT_TRUE(source.ok());
  const auto source_before = serialize_qdr_authored_topology(source.value());
  ASSERT_TRUE(source_before.ok());
  const auto source_identity = source.value().content_identity();
  const auto source_revision = source.value().content_revision();
  for (const double degrees : {90.0, 37.0, -53.0, 179.999, 0.001,
                               180.0 - 1.0e-10, 1.0e-10}) {
    SCOPED_TRACE(degrees);
    const double angle = degrees * 0.017453292519943295;
    const float x = static_cast<float>(2.0 * std::cos(angle));
    const float z = static_cast<float>(-2.0 * std::sin(angle));
    const std::array<Vec3, 4> target_points{{
        {4.0F, 0.0F, 0.0F}, {4.0F + x, 0.0F, z},
        {4.0F + x, 3.0F, z}, {4.0F, 3.0F, 0.0F},
    }};
    auto target = make_texture_action_face(target_points);
    ASSERT_TRUE(target.ok());
    const auto target_before = serialize_qdr_authored_topology(target.value());
    const auto original_topology = target.value().authored_topology();
    ASSERT_TRUE(target_before.ok());
    ASSERT_TRUE(original_topology.ok());
    const auto target_identity = target.value().content_identity();
    const auto result = prepare_face_texture_transfer(
        texture_transfer_face(source.value()), texture_transfer_face(target.value(), 2U), 42U);
    ASSERT_TRUE(result.ok()) << result.error_message();
    ASSERT_TRUE(result.value().changed);
    ASSERT_EQ(result.value().documents.size(), 1U);
    const auto *prepared = prepared_texture_document(result.value(), 2U);
    ASSERT_NE(prepared, nullptr);
    EXPECT_TRUE(prepared->document.has_same_geometry(target.value()));
    EXPECT_TRUE(prepared->document.shares_selection_with(target.value()));
    EXPECT_EQ(prepared->document.selection_revision(), target.value().selection_revision());
    EXPECT_EQ(prepared->document.vertex_ids(), target.value().vertex_ids());
    EXPECT_EQ(prepared->document.face_ids(), target.value().face_ids());
    EXPECT_TRUE(prepared->receipt.dirty.geometry);
    EXPECT_TRUE(prepared->receipt.dirty.materials);
    EXPECT_FALSE(prepared->receipt.dirty.topology);
    EXPECT_FALSE(prepared->receipt.dirty.selection);
    EXPECT_EQ(prepared->receipt.modified.faces, target.value().face_ids());
    const auto topology = prepared->document.authored_topology();
    ASSERT_TRUE(topology.ok());
    EXPECT_EQ(topology.value().faces.front().texture_transform, settings);
    EXPECT_EQ(topology.value().faces.front().material_slot, 42U);
    const std::array<Vec2, 4> expected{{
        expected_texture_uv(transfer_sheared_uv(4.0F, 0.0F), settings),
        expected_texture_uv(transfer_sheared_uv(6.0F, 0.0F), settings),
        expected_texture_uv(transfer_sheared_uv(6.0F, 3.0F), settings),
        expected_texture_uv(transfer_sheared_uv(4.0F, 3.0F), settings),
    }};
    expect_compiled_texture_uvs(prepared->document,
        original_topology.value().faces.front().vertices, expected);
    const auto repeated = prepare_face_texture_transfer(texture_transfer_face(source.value()),
        texture_transfer_face(prepared->document, 2U), 42U);
    ASSERT_TRUE(repeated.ok()) << repeated.error_message();
    EXPECT_FALSE(repeated.value().changed);
    EXPECT_FALSE(repeated.value().documents.front().receipt.changed);
    EXPECT_TRUE(repeated.value().documents.front().document.shares_topology_with(prepared->document));
    EXPECT_EQ(target.value().content_identity(), target_identity);
    EXPECT_EQ(serialize_qdr_authored_topology(target.value()).value(), target_before.value());
  }
  EXPECT_EQ(source.value().content_identity(), source_identity);
  EXPECT_EQ(source.value().content_revision(), source_revision);
  EXPECT_EQ(serialize_qdr_authored_topology(source.value()).value(), source_before.value());
}

TEST(ModelingSdkConsumerTests,
     FaceTextureTransferKeepsWorldPhaseOnParallelAndOpposedPlanes) {
  constexpr std::array<Vec3, 4> kSourcePoints{{
      {0.0F, 0.0F, 0.0F}, {4.0F, 0.0F, 0.0F},
      {4.0F, 3.0F, 0.0F}, {0.0F, 3.0F, 0.0F},
  }};
  std::array<Vec2, 4> uvs;
  for (std::size_t corner = 0U; corner < uvs.size(); ++corner) {
    uvs[corner] = transfer_sheared_uv(kSourcePoints[corner].x, kSourcePoints[corner].y);
  }
  auto source = make_texture_action_face(kSourcePoints, uvs);
  ASSERT_TRUE(source.ok());
  for (const bool opposed : {false, true}) {
    SCOPED_TRACE(opposed);
    std::array<Vec3, 4> points{{
        {6.0F, 1.0F, 5.0F}, {8.0F, 1.0F, 5.0F},
        {8.0F, 3.0F, 5.0F}, {6.0F, 3.0F, 5.0F},
    }};
    if (opposed) {
      std::ranges::reverse(points);
    }
    auto target = make_texture_action_face(points);
    ASSERT_TRUE(target.ok());
    const auto result = prepare_face_texture_transfer(
        texture_transfer_face(source.value()), texture_transfer_face(target.value()), 6U);
    ASSERT_TRUE(result.ok()) << result.error_message();
    ASSERT_TRUE(result.value().changed);
    const auto compiled = result.value().documents.front().document.compile_mesh();
    ASSERT_TRUE(compiled.ok());
    for (const auto &vertex : compiled.value().vertices) {
      expect_vec2_near(vertex.uv0, transfer_sheared_uv(vertex.position.x, vertex.position.y));
    }
  }
}

TEST(ModelingSdkConsumerTests,
     FaceTextureTransferPreservesTransformedMirroredAndDistantOccurrenceSampling) {
  constexpr std::array<Vec3, 4> kPoints{{
      {0.0F, 0.0F, 0.0F}, {4.0F, 0.0F, 0.0F},
      {4.0F, 3.0F, 0.0F}, {0.0F, 3.0F, 0.0F},
  }};
  std::array<Vec2, 4> uvs;
  for (std::size_t corner = 0U; corner < uvs.size(); ++corner) {
    uvs[corner] = transfer_sheared_uv(kPoints[corner].x, kPoints[corner].y);
  }
  const FaceTextureTransform settings{
      .scale = {-2.0F, 0.5F}, .shift_uv = {0.31F, -0.37F}, .rotation_degrees = 37.0F};
  auto source = make_texture_action_face(kPoints, uvs, settings);
  auto target = make_texture_action_face(kPoints);
  ASSERT_TRUE(source.ok());
  ASSERT_TRUE(target.ok());
  for (const float source_direction : {-1.0F, 1.0F}) {
    for (const float target_direction : {-1.0F, 1.0F}) {
      for (const bool distant : {false, true}) {
        SCOPED_TRACE(source_direction);
        SCOPED_TRACE(target_direction);
        SCOPED_TRACE(distant);
        const Vec3 origin = distant ? Vec3{1.0e8F, 1.0e8F, 1.0e8F}
                                    : Vec3{5.0F, 7.0F, 11.0F};
        auto from = texture_transfer_face(source.value());
        from.world_from_local = {
            .x_axis = {0.0F, 0.0F, 2.0F * source_direction},
            .y_axis = {0.0F, 3.0F, 0.0F}, .z_axis = {-1.0F, 0.0F, 0.0F},
            .origin = origin};
        from.sampling_from_local = {
            .x_axis = {-2.0F, 0.0F, 0.0F}, .y_axis = {0.5F, 3.0F, 0.0F},
            .z_axis = {0.0F, 0.0F, 1.0F}, .origin = {1.0F, -2.0F, 1.0F}};
        auto to = texture_transfer_face(target.value(), 2U);
        to.world_from_local = {
            .x_axis = {1.5F * target_direction, 0.0F, 0.0F},
            .y_axis = {0.0F, 2.0F, 0.0F}, .z_axis = {0.0F, 0.0F, 1.0F},
            .origin = {origin.x, origin.y, origin.z + 8.0F * source_direction}};
        to.sampling_from_local = {
            .x_axis = {-1.5F, 0.0F, 0.0F}, .y_axis = {0.25F, 2.0F, 0.0F},
            .z_axis = {0.0F, 0.0F, 1.0F}, .origin = {3.0F, -4.0F, 1.0F}};
        const auto result = prepare_face_texture_transfer(from, to, 6U);
        ASSERT_TRUE(result.ok()) << result.error_message();
        ASSERT_TRUE(result.value().changed);
        const auto &prepared = result.value().documents.front().document;
        const auto compiled = prepared.compile_mesh();
        ASSERT_TRUE(compiled.ok());
        for (const auto &vertex : compiled.value().vertices) {
          // The target's reflected sampling flips its canonical +Z chart:
          // q(Cp)-q(p)=(x/4-y/8-1.5, 2-y/2).
          const Vec2 delta{0.25F * vertex.position.x - 0.125F * vertex.position.y - 1.5F,
                            2.0F - 0.5F * vertex.position.y};
          const Vec2 effective{
              vertex.uv0.x + vertex.uv_carrier[0] * delta.x + vertex.uv_carrier[1] * delta.y,
              vertex.uv0.y + vertex.uv_carrier[2] * delta.x + vertex.uv_carrier[3] * delta.y};
          const float unfolded_x = 4.0F + 0.75F * vertex.position.x;
          const float unfolded_y = (2.0F / 3.0F) * vertex.position.y;
          const auto expected = expected_texture_uv(transfer_sheared_uv(
              2.0F * unfolded_x - 0.5F * unfolded_y - 1.0F,
              3.0F * unfolded_y - 2.0F), settings);
          expect_vec2_near(effective, expected);
        }
        to.document = &prepared;
        const auto repeated = prepare_face_texture_transfer(from, to, 6U);
        ASSERT_TRUE(repeated.ok()) << repeated.error_message();
        EXPECT_FALSE(repeated.value().changed);
      }
    }
  }
}

TEST(ModelingSdkConsumerTests,
     FaceTextureTransferDoesNotHideQuarterTileChangesBehindLargeOffsets) {
  constexpr std::array<Vec3, 4> kPoints{{
      {0.0F, 0.0F, 0.0F}, {2.0F, 0.0F, 0.0F},
      {2.0F, 2.0F, 0.0F}, {0.0F, 2.0F, 0.0F},
  }};
  constexpr std::array<Vec2, 4> kUvs{{
      {1.0e6F, 0.0F}, {1.0e6F + 1.0F, 0.0F},
      {1.0e6F + 1.0F, 1.0F}, {1.0e6F, 1.0F},
  }};
  auto shifted = kUvs;
  for (auto &uv : shifted) {
    uv.x += 0.25F;
  }
  auto source = make_texture_action_face(kPoints, kUvs);
  auto target = make_texture_action_face(kPoints, shifted);
  ASSERT_TRUE(source.ok());
  ASSERT_TRUE(target.ok());
  const auto result = prepare_face_texture_transfer(texture_transfer_face(source.value()),
      texture_transfer_face(target.value()), 6U);
  ASSERT_TRUE(result.ok()) << result.error_message();
  ASSERT_TRUE(result.value().changed);
  const auto &prepared = result.value().documents.front();
  EXPECT_FALSE(prepared.receipt.dirty.materials);
  expect_compiled_texture_uvs(prepared.document, target.value().vertex_ids(), kUvs);
  const auto repeated = prepare_face_texture_transfer(texture_transfer_face(source.value()),
      texture_transfer_face(prepared.document), 6U);
  ASSERT_TRUE(repeated.ok());
  EXPECT_FALSE(repeated.value().changed);
}

TEST(ModelingSdkConsumerTests,
     FaceTextureTransferSupportsConstantAndRankOneAffineMappings) {
  constexpr std::array<Vec3, 4> kSourcePoints{{
      {0.0F, 0.0F, 0.0F}, {2.0F, 0.0F, 0.0F},
      {2.0F, 2.0F, 0.0F}, {0.0F, 2.0F, 0.0F},
  }};
  constexpr std::array<Vec3, 4> kTargetPoints{{
      {2.0F, 0.0F, 0.0F}, {2.0F, 0.0F, -2.0F},
      {2.0F, 2.0F, -2.0F}, {2.0F, 2.0F, 0.0F},
  }};
  for (const bool constant : {false, true}) {
    SCOPED_TRACE(constant);
    const auto mapping = [&](float x, float y) -> Vec2 {
      const float u = constant ? 0.3F : 0.3F + 0.5F * x - 0.2F * y;
      return {u, -0.7F + 2.0F * u};
    };
    std::array<Vec2, 4> uvs;
    for (std::size_t corner = 0U; corner < uvs.size(); ++corner) {
      uvs[corner] = mapping(kSourcePoints[corner].x, kSourcePoints[corner].y);
    }
    auto source = make_texture_action_face(kSourcePoints, uvs);
    auto target = make_texture_action_face(kTargetPoints);
    ASSERT_TRUE(source.ok());
    ASSERT_TRUE(target.ok());
    const auto result = prepare_face_texture_transfer(texture_transfer_face(source.value()),
        texture_transfer_face(target.value()), 6U);
    ASSERT_TRUE(result.ok()) << result.error_message();
    const auto compiled = result.value().documents.front().document.compile_mesh();
    ASSERT_TRUE(compiled.ok());
    for (const auto &vertex : compiled.value().vertices) {
      expect_vec2_near(vertex.uv0, mapping(2.0F - vertex.position.z, vertex.position.y));
    }
  }
}

TEST(ModelingSdkConsumerTests,
     FaceTextureTransferRejectsNonAffineAndNonplanarInputsAtomically) {
  constexpr std::array<Vec3, 4> kPoints{{
      {0.0F, 0.0F, 0.0F}, {2.0F, 0.0F, 0.0F},
      {2.0F, 2.0F, 0.0F}, {0.0F, 2.0F, 0.0F},
  }};
  constexpr std::array<Vec2, 4> kUvs{{
      {0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F},
  }};
  for (int invalid = 0; invalid < 4; ++invalid) {
    SCOPED_TRACE(invalid);
    auto source_points = kPoints;
    auto target_points = kPoints;
    auto source_uvs = kUvs;
    if (invalid < 2) {
      source_uvs[2].x += 0.5F;
      if (invalid == 1) {
        for (auto &uv : source_uvs) {
          uv.x += 1.0e6F;
        }
      }
    } else if (invalid == 2) {
      source_points[2].z = 0.25F;
    } else {
      target_points[2].z = 0.25F;
    }
    auto source = make_texture_action_face(source_points, source_uvs);
    auto target = make_texture_action_face(target_points);
    ASSERT_TRUE(source.ok()) << source.error_message();
    ASSERT_TRUE(target.ok()) << target.error_message();
    const auto source_before = serialize_qdr_authored_topology(source.value());
    const auto target_before = serialize_qdr_authored_topology(target.value());
    ASSERT_TRUE(source_before.ok());
    ASSERT_TRUE(target_before.ok());
    const auto source_identity = source.value().content_identity();
    const auto target_identity = target.value().content_identity();
    const auto result = prepare_face_texture_transfer(texture_transfer_face(source.value()),
        texture_transfer_face(target.value()), 42U);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    EXPECT_EQ(source.value().content_identity(), source_identity);
    EXPECT_EQ(target.value().content_identity(), target_identity);
    EXPECT_EQ(serialize_qdr_authored_topology(source.value()).value(), source_before.value());
    EXPECT_EQ(serialize_qdr_authored_topology(target.value()).value(), target_before.value());
  }
}

TEST(ModelingSdkConsumerTests,
     FaceTextureTransferRejectsStaleInvalidAndCollapsedMappingsWithoutPublication) {
  constexpr std::array<Vec3, 4> kPoints{{
      {0.0F, 0.0F, 0.0F}, {2.0F, 0.0F, 0.0F},
      {2.0F, 2.0F, 0.0F}, {0.0F, 2.0F, 0.0F},
  }};
  auto source = make_texture_action_face(kPoints);
  auto target = make_texture_action_face(kPoints);
  ASSERT_TRUE(source.ok());
  ASSERT_TRUE(target.ok());
  const auto source_identity = source.value().content_identity();
  const auto target_identity = target.value().content_identity();
  const auto target_revision = target.value().content_revision();
  for (int invalid = 0; invalid < 7; ++invalid) {
    SCOPED_TRACE(invalid);
    auto from = texture_transfer_face(source.value());
    auto to = texture_transfer_face(target.value(), 2U);
    switch (invalid) {
    case 0: from.document = nullptr; break;
    case 1: from.face = make_id<FaceTag>(from.face.index, 2U); break;
    case 2: to.face = make_id<FaceTag>(999U); break;
    case 3: from.world_from_local.origin.y = std::numeric_limits<float>::infinity(); break;
    case 4: to.world_from_local.z_axis = {}; break;
    case 5: to.sampling_from_local.z_axis = {}; break;
    case 6: to.world_from_local.origin.x = 1.0e10F; break;
    }
    const auto result = prepare_face_texture_transfer(from, to, 42U);
    ASSERT_FALSE(result.ok());
    EXPECT_FALSE(result.error_message().empty());
    EXPECT_EQ(source.value().content_identity(), source_identity);
    EXPECT_EQ(target.value().content_identity(), target_identity);
    EXPECT_EQ(target.value().content_revision(), target_revision);
  }
}

TEST(ModelingSdkConsumerTests,
     FaceTextureTransferRetainsNarrowRepresentableTargetFootprints) {
  constexpr std::array<Vec3, 4> kPoints{{
      {0.0F, 0.0F, 0.0F}, {2.0F, 0.0F, 0.0F},
      {2.0F, 2.0F, 0.0F}, {0.0F, 2.0F, 0.0F},
  }};
  auto source = make_texture_action_face(kPoints);
  auto target = make_texture_action_face(kPoints);
  ASSERT_TRUE(source.ok());
  ASSERT_TRUE(target.ok());
  auto to = texture_transfer_face(target.value(), 2U);
  to.world_from_local.y_axis = {0.0F, 1.0e-7F, 0.0F};
  const auto result = prepare_face_texture_transfer(
      texture_transfer_face(source.value()), to, 6U);
  ASSERT_TRUE(result.ok()) << result.error_message();
  ASSERT_TRUE(result.value().changed);
  const auto compiled = result.value().documents.front().document.compile_mesh();
  ASSERT_TRUE(compiled.ok());
  for (const auto &vertex : compiled.value().vertices) {
    EXPECT_FLOAT_EQ(vertex.uv0.x, vertex.position.x * 0.5F);
    EXPECT_NEAR(vertex.uv0.y, vertex.position.y * -0.5e-7F, 1.0e-12F);
  }
}

TEST(ModelingSdkConsumerTests,
     FaceTextureTransferSelfAndRepeatAreNoopsButMaterialOnlyTransferChanges) {
  constexpr std::array<Vec3, 4> kPoints{{
      {0.0F, 0.0F, 0.0F}, {2.0F, 0.0F, 0.0F},
      {2.0F, 2.0F, 0.0F}, {0.0F, 2.0F, 0.0F},
  }};
  auto source = make_texture_action_face(kPoints);
  ASSERT_TRUE(source.ok());
  const auto face = texture_transfer_face(source.value());
  const auto noop = prepare_face_texture_transfer(face, face, 6U);
  ASSERT_TRUE(noop.ok()) << noop.error_message();
  EXPECT_FALSE(noop.value().changed);
  ASSERT_EQ(noop.value().documents.size(), 1U);
  const auto &unchanged = noop.value().documents.front();
  EXPECT_FALSE(unchanged.receipt.changed);
  EXPECT_TRUE(unchanged.document.shares_topology_with(source.value()));
  EXPECT_EQ(unchanged.document.content_revision(), source.value().content_revision());
  const auto material = prepare_face_texture_transfer(face, face, 42U);
  ASSERT_TRUE(material.ok()) << material.error_message();
  ASSERT_TRUE(material.value().changed);
  const auto &prepared = material.value().documents.front();
  EXPECT_TRUE(prepared.receipt.dirty.materials);
  EXPECT_FALSE(prepared.receipt.dirty.selection);
  EXPECT_TRUE(prepared.document.has_same_geometry(source.value()));
  const auto topology = prepared.document.authored_topology();
  ASSERT_TRUE(topology.ok());
  EXPECT_EQ(topology.value().faces.front().material_slot, 42U);
  EXPECT_TRUE(topology.value().faces.front().uvs.empty());
}

TEST(ModelingSdkConsumerTests,
     FaceTextureActionsKeepIncrementalEditsRelativeAndRetainEveryBaseCorner) {
  constexpr std::array<Vec3, 4> kPositions{{
      {0.0F, 0.0F, 0.0F}, {4.0F, 0.0F, 0.0F},
      {4.0F, 3.0F, 0.0F}, {0.0F, 3.0F, 0.0F},
  }};
  // This quad deliberately has non-affine corner UVs, not a planar chart.
  constexpr std::array<Vec2, 4> kAuthored{{
      {0.2F, 0.1F}, {1.8F, 0.35F}, {2.1F, 1.7F}, {0.4F, 1.2F},
  }};
  const FaceTextureTransform initial{
      .scale = {-2.0F, 0.5F}, .shift_uv = {0.25F, -0.375F},
      .rotation_degrees = 30.0F,
  };
  constexpr std::array<FaceTextureAction, 12> kActions{
      FaceTextureAction::ScaleXUp, FaceTextureAction::ScaleXDown,
      FaceTextureAction::ScaleYUp, FaceTextureAction::ScaleYDown,
      FaceTextureAction::ShiftLeft, FaceTextureAction::ShiftRight,
      FaceTextureAction::ShiftUp, FaceTextureAction::ShiftDown,
      FaceTextureAction::RotateClockwise,
      FaceTextureAction::RotateCounterClockwise,
      FaceTextureAction::FlipTextureHorizontal,
      FaceTextureAction::FlipTextureVertical,
  };
  for (const bool authored_uvs : {false, true}) {
    SCOPED_TRACE(authored_uvs);
    auto created = make_texture_action_face(
        kPositions, authored_uvs ? std::span<const Vec2>(kAuthored)
                                : std::span<const Vec2>{},
        initial);
    ASSERT_TRUE(created.ok()) << created.error_message();
    const PolygonDocument &original = created.value();
    const auto topology = original.authored_topology();
    ASSERT_TRUE(topology.ok());
    const auto source_identity = original.content_identity();
    const auto source_revision = original.content_revision();
    for (const FaceTextureAction action : kActions) {
      SCOPED_TRACE(static_cast<int>(action));
      FaceTextureTransform expected = initial;
      switch (action) {
      case FaceTextureAction::ScaleXUp: expected.scale.x = -2.1F; break;
      case FaceTextureAction::ScaleXDown: expected.scale.x = -1.9F; break;
      case FaceTextureAction::ScaleYUp: expected.scale.y = 0.6F; break;
      case FaceTextureAction::ScaleYDown: expected.scale.y = 0.4F; break;
      case FaceTextureAction::ShiftLeft: expected.shift_uv.x += 1.0F / 128.0F; break;
      case FaceTextureAction::ShiftRight: expected.shift_uv.x -= 1.0F / 128.0F; break;
      case FaceTextureAction::ShiftUp: expected.shift_uv.y += 1.0F / 32.0F; break;
      case FaceTextureAction::ShiftDown: expected.shift_uv.y -= 1.0F / 32.0F; break;
      case FaceTextureAction::RotateClockwise: expected.rotation_degrees = 52.5F; break;
      case FaceTextureAction::RotateCounterClockwise: expected.rotation_degrees = 7.5F; break;
      case FaceTextureAction::FlipTextureHorizontal: expected.scale.x = 2.0F; break;
      case FaceTextureAction::FlipTextureVertical: expected.scale.y = -0.5F; break;
      default: FAIL() << "Unexpected incremental action";
      }
      const std::array targets{texture_action_target(original, 1U, 128U, 32U)};
      const auto result = prepare_face_texture_action(
          targets, {.action = action, .treat_as_one = true,
                    .rotation_degrees = 22.5});
      ASSERT_TRUE(result.ok()) << result.error_message();
      ASSERT_TRUE(result.value().changed);
      const auto *prepared = prepared_texture_document(result.value());
      ASSERT_NE(prepared, nullptr);
      EXPECT_TRUE(prepared->receipt.changed);
      EXPECT_TRUE(prepared->receipt.dirty.geometry);
      EXPECT_FALSE(prepared->receipt.dirty.topology);
      EXPECT_FALSE(prepared->receipt.dirty.selection);
      EXPECT_FALSE(prepared->receipt.dirty.materials);
      EXPECT_TRUE(prepared->document.has_same_geometry(original));
      EXPECT_TRUE(prepared->document.shares_selection_with(original));
      EXPECT_EQ(prepared->document.selection_revision(), original.selection_revision());
      const auto after = prepared->document.authored_topology();
      ASSERT_TRUE(after.ok());
      const auto &face = after.value().faces.front();
      EXPECT_EQ(face.texture_transform, expected);
      ASSERT_EQ(face.uvs.size(), topology.value().faces.front().uvs.size());
      for (std::size_t corner = 0; corner < face.uvs.size(); ++corner) {
        EXPECT_EQ(face.uvs[corner].x, kAuthored[corner].x);
        EXPECT_EQ(face.uvs[corner].y, kAuthored[corner].y);
      }
      std::array<Vec2, 4> expected_uvs{};
      for (std::size_t corner = 0; corner < kPositions.size(); ++corner) {
        const Vec2 base = authored_uvs
                              ? kAuthored[corner]
                              : Vec2{kPositions[corner].x * 0.5F,
                                     -kPositions[corner].y * 0.5F};
        expected_uvs[corner] = expected_texture_uv(base, expected);
      }
      expect_compiled_texture_uvs(prepared->document, face.vertices, expected_uvs);
      EXPECT_EQ(original.content_identity(), source_identity);
      EXPECT_EQ(original.content_revision(), source_revision);
    }
  }
}

TEST(ModelingSdkConsumerTests,
     FaceTextureActionsRejectSignedScaleZeroCrossingsWithoutPartialCandidates) {
  constexpr std::array<Vec3, 3> kPoints{{
      {0.0F, 0.0F, 0.0F}, {2.0F, 0.0F, 0.0F}, {0.0F, 2.0F, 0.0F},
  }};
  auto first = make_texture_action_face(kPoints);
  ASSERT_TRUE(first.ok());
  const auto first_identity = first.value().content_identity();
  for (const float scale : {0.1F, 0.05F, -0.1F, -0.05F}) {
    for (const FaceTextureAction action : {FaceTextureAction::ScaleXDown,
                                           FaceTextureAction::ScaleYDown}) {
      SCOPED_TRACE(scale);
      SCOPED_TRACE(static_cast<int>(action));
      auto second = make_texture_action_face(kPoints, {}, {.scale = {scale, scale}});
      ASSERT_TRUE(second.ok());
      const auto second_identity = second.value().content_identity();
      const auto second_revision = second.value().content_revision();
      const auto before = serialize_qdr_authored_topology(second.value());
      ASSERT_TRUE(before.ok());
      const std::array targets{texture_action_target(first.value(), 1U),
                               texture_action_target(second.value(), 2U)};
      const auto result = prepare_face_texture_action(targets, {.action = action});
      ASSERT_FALSE(result.ok());
      EXPECT_FALSE(result.error_message().empty());
      EXPECT_EQ(first.value().content_identity(), first_identity);
      EXPECT_EQ(second.value().content_identity(), second_identity);
      EXPECT_EQ(second.value().content_revision(), second_revision);
      const auto after = serialize_qdr_authored_topology(second.value());
      ASSERT_TRUE(after.ok());
      EXPECT_EQ(after.value(), before.value());
    }
  }
}

TEST(ModelingSdkConsumerTests,
     FaceTextureActionsMoveTheImageOnePixelInTheNamedDirectionPerMaterial) {
  constexpr std::array<Vec3, 3> kPoints{{
      {0.0F, 0.0F, 0.0F}, {4.0F, 0.0F, 0.0F}, {0.0F, 4.0F, 0.0F},
  }};
  auto first = make_texture_action_face(kPoints);
  auto second = make_texture_action_face(kPoints);
  auto fallback = make_texture_action_face(kPoints);
  ASSERT_TRUE(first.ok());
  ASSERT_TRUE(second.ok());
  ASSERT_TRUE(fallback.ok());
  const std::array targets{
      texture_action_target(first.value(), 1U, 128U, 32U),
      texture_action_target(second.value(), 2U, 32U, 256U),
      texture_action_target(fallback.value(), 3U),
  };
  /// Pairs each image-shift action with its expected visible motion direction.
  struct ShiftCase {
    FaceTextureAction action;
    float horizontal;
    float vertical;
  };
  constexpr std::array kCases{
      ShiftCase{FaceTextureAction::ShiftLeft, -1.0F, 0.0F},
      ShiftCase{FaceTextureAction::ShiftRight, 1.0F, 0.0F},
      ShiftCase{FaceTextureAction::ShiftUp, 0.0F, 1.0F},
      ShiftCase{FaceTextureAction::ShiftDown, 0.0F, -1.0F},
  };
  for (const auto &shift : kCases) {
    SCOPED_TRACE(static_cast<int>(shift.action));
    const auto result = prepare_face_texture_action(
        targets, {.action = shift.action, .treat_as_one = true});
    ASSERT_TRUE(result.ok()) << result.error_message();
    for (const auto &target : targets) {
      const auto *prepared = prepared_texture_document(result.value(), target.key);
      ASSERT_NE(prepared, nullptr);
      const auto settings = prepared->document.face_texture_settings(
          prepared->document.face_ids());
      ASSERT_TRUE(settings.ok());
      const auto &transform = settings.value().front().texture_transform;
      const float width = static_cast<float>(target.faces.front().texture_width);
      const float height = static_cast<float>(target.faces.front().texture_height);
      // On a +Z wall q=(x/2,-y/2), preserve the marker's sampled UV while
      // moving its image location by one pixel in world X/up-Y directions.
      const Vec2 marker_before{0.75F, 0.625F};
      const Vec2 marker_after{marker_before.x + 2.0F * shift.horizontal / width,
                              marker_before.y + 2.0F * shift.vertical / height};
      expect_vec2_near(
          {marker_after.x * 0.5F + transform.shift_uv.x,
           -marker_after.y * 0.5F + transform.shift_uv.y},
          {marker_before.x * 0.5F, -marker_before.y * 0.5F});
      EXPECT_EQ(transform.scale.x, 1.0F);
      EXPECT_EQ(transform.scale.y, 1.0F);
      EXPECT_EQ(transform.rotation_degrees, 0.0F);
    }
  }
}

TEST(ModelingSdkConsumerTests,
     FaceTextureAlignGridUsesWorldDensityAndSolvesMirroredOccurrenceCompensation) {
  constexpr std::array<Vec3, 4> kPoints{{
      {0.0F, 0.0F, 0.0F}, {4.0F, 0.0F, 0.0F},
      {4.0F, 3.0F, 0.0F}, {0.0F, 3.0F, 0.0F},
  }};
  constexpr std::array<Vec2, 4> kAuthored{{
      {0.2F, 0.1F}, {1.8F, 0.35F}, {2.1F, 1.7F}, {0.4F, 1.2F},
  }};
  auto created = make_texture_action_face(
      kPoints, kAuthored, {.scale = {-2.0F, 0.5F}, .shift_uv = {0.25F, -0.5F},
                         .rotation_degrees = 37.0F});
  ASSERT_TRUE(created.ok()) << created.error_message();
  for (const float world_direction : {-1.0F, 1.0F}) {
    for (const bool compensated : {false, true}) {
      SCOPED_TRACE(world_direction);
      SCOPED_TRACE(compensated);
      auto target = texture_action_target(created.value());
      target.world_from_local = {
          .x_axis = {0.0F, 0.0F, 2.0F * world_direction},
          .y_axis = {0.0F, 3.0F, 0.0F},
          .z_axis = {-1.0F, 0.0F, 0.0F}, .origin = {5.0F, 7.0F, 11.0F},
      };
      if (compensated) {
        target.sampling_from_local = {
            .x_axis = {-1.5F, 0.0F, 0.0F}, .y_axis = {0.0F, 2.0F, 0.0F},
            .z_axis = {0.0F, 0.0F, 1.0F}, .origin = {3.0F, -4.0F, 1.0F},
        };
      }
      const auto result = prepare_face_texture_action(
          std::span<const FaceTextureActionTarget>(&target, 1U),
          {.action = FaceTextureAction::AlignToGrid});
      ASSERT_TRUE(result.ok()) << result.error_message();
      const auto *prepared = prepared_texture_document(result.value());
      ASSERT_NE(prepared, nullptr);
      EXPECT_TRUE(prepared->document.has_same_geometry(created.value()));
      const auto settings = prepared->document.face_texture_settings(
          prepared->document.face_ids());
      ASSERT_TRUE(settings.ok());
      EXPECT_EQ(settings.value().front().texture_transform, FaceTextureTransform{});
      const auto compiled = prepared->document.compile_mesh();
      ASSERT_TRUE(compiled.ok());
      for (const auto &vertex : compiled.value().vertices) {
        Vec2 effective = vertex.uv0;
        if (compensated) {
          // The mirrored XY sampling transform flips the geometric normal
          // to -Z: q(Cp)=(-(3-1.5x)/2, -(2y-4)/2).
          const Vec2 delta{0.25F * vertex.position.x - 1.5F,
                            2.0F - 0.5F * vertex.position.y};
          effective.x += vertex.uv_carrier[0] * delta.x +
                         vertex.uv_carrier[1] * delta.y;
          effective.y += vertex.uv_carrier[2] * delta.x +
                         vertex.uv_carrier[3] * delta.y;
        }
        // World +X/-X faces use signed Z and negative Y at .5 tiles/unit.
        expect_vec2_near(effective,
                         {world_direction * 5.5F + vertex.position.x,
                          -3.5F - 1.5F * vertex.position.y});
      }
    }
  }
}

TEST(ModelingSdkConsumerTests,
     FaceTextureAlignFaceProjectsOntoTheGeometricPlaneAndRetainsSettings) {
  constexpr std::array<Vec3, 4> kPoints{{
      {2.0F, 4.0F, 3.0F}, {4.4F, 4.0F, 6.2F},
      {4.4F, 7.0F, 6.2F}, {2.0F, 7.0F, 3.0F},
  }};
  constexpr std::array<Vec2, 4> kAuthored{{
      {0.2F, 0.1F}, {1.8F, 0.35F}, {2.1F, 1.7F}, {0.4F, 1.2F},
  }};
  const FaceTextureTransform settings{
      .scale = {2.0F, -0.5F}, .shift_uv = {0.2F, -0.3F},
      .rotation_degrees = 90.0F,
  };
  auto created = make_texture_action_face(kPoints, kAuthored, settings);
  ASSERT_TRUE(created.ok()) << created.error_message();
  const std::array targets{texture_action_target(created.value())};
  const auto result = prepare_face_texture_action(
      targets, {.action = FaceTextureAction::AlignToFace});
  ASSERT_TRUE(result.ok()) << result.error_message();
  const auto *prepared = prepared_texture_document(result.value());
  ASSERT_NE(prepared, nullptr);
  const auto topology = prepared->document.authored_topology();
  ASSERT_TRUE(topology.ok());
  EXPECT_EQ(topology.value().faces.front().texture_transform, settings);
  EXPECT_TRUE(prepared->document.has_same_geometry(created.value()));
  // The face's unit horizontal tangent is (.6,0,.8), anchored at corner 0;
  // its geometric vertical chart axis is -Y, independent of texture rotation.
  constexpr std::array<Vec2, 4> kFaceProjection{{
      {0.0F, 0.0F}, {2.0F, 0.0F}, {2.0F, -1.5F}, {0.0F, -1.5F},
  }};
  std::array<Vec2, 4> expected{};
  for (std::size_t corner = 0; corner < expected.size(); ++corner) {
    expected[corner] = expected_texture_uv(kFaceProjection[corner], settings);
  }
  expect_compiled_texture_uvs(prepared->document,
                              topology.value().faces.front().vertices, expected);
}

TEST(ModelingSdkConsumerTests,
     FaceTextureAlignViewUsesCapturedWorldCameraAxesAndRetainsSettings) {
  constexpr std::array<Vec3, 4> kPoints{{
      {0.0F, 0.0F, 0.0F}, {4.0F, 0.0F, 0.0F},
      {4.0F, 3.0F, 0.0F}, {0.0F, 3.0F, 0.0F},
  }};
  const FaceTextureTransform settings{
      .scale = {-2.0F, 0.5F}, .shift_uv = {0.2F, -0.3F},
      .rotation_degrees = 90.0F,
  };
  auto created = make_texture_action_face(kPoints, {}, settings);
  ASSERT_TRUE(created.ok());
  auto target = texture_action_target(created.value());
  target.world_from_local = {
      .x_axis = {0.0F, 0.0F, 2.0F}, .y_axis = {0.0F, 1.5F, 0.0F},
      .z_axis = {-1.0F, 0.0F, 0.0F}, .origin = {7.0F, 3.0F, -5.0F},
  };
  const auto result = prepare_face_texture_action(
      std::span<const FaceTextureActionTarget>(&target, 1U),
      {.action = FaceTextureAction::AlignToView,
       .view_right = {0.0F, 0.0F, 1.0F}, .view_up = {0.0F, 1.0F, 0.0F}});
  ASSERT_TRUE(result.ok()) << result.error_message();
  const auto *prepared = prepared_texture_document(result.value());
  ASSERT_NE(prepared, nullptr);
  const auto topology = prepared->document.authored_topology();
  ASSERT_TRUE(topology.ok());
  EXPECT_EQ(topology.value().faces.front().texture_transform, settings);
  EXPECT_TRUE(prepared->document.has_same_geometry(created.value()));
  std::array<Vec2, 4> expected{};
  for (std::size_t corner = 0; corner < kPoints.size(); ++corner) {
    expected[corner] = expected_texture_uv(
        {-2.5F + kPoints[corner].x, -1.5F - 0.75F * kPoints[corner].y}, settings);
  }
  expect_compiled_texture_uvs(prepared->document,
                              topology.value().faces.front().vertices, expected);
}

TEST(ModelingSdkConsumerTests,
     FaceTextureFitAndJustifyUseCurrentCornerBoundsWithoutChangingOtherAxes) {
  constexpr std::array<Vec3, 4> kPoints{{
      {0.0F, 0.0F, 0.0F}, {4.0F, 0.0F, 0.0F},
      {4.0F, 3.0F, 0.0F}, {0.0F, 3.0F, 0.0F},
  }};
  constexpr std::array<Vec2, 4> kAuthored{{
      {0.2F, 0.1F}, {1.8F, 0.35F}, {2.1F, 1.7F}, {0.4F, 1.2F},
  }};
  const FaceTextureTransform settings{
      .scale = {-2.0F, 0.5F}, .shift_uv = {0.25F, -0.375F},
      .rotation_degrees = 30.0F,
  };
  constexpr std::array kActions{
      FaceTextureAction::FitBoth, FaceTextureAction::FitX,
      FaceTextureAction::FitY, FaceTextureAction::JustifyLeft,
      FaceTextureAction::JustifyRight, FaceTextureAction::JustifyTop,
      FaceTextureAction::JustifyBottom, FaceTextureAction::JustifyCenter,
  };
  for (const bool authored_uvs : {false, true}) {
    SCOPED_TRACE(authored_uvs);
    auto created = make_texture_action_face(
        kPoints, authored_uvs ? std::span<const Vec2>(kAuthored)
                             : std::span<const Vec2>{}, settings);
    ASSERT_TRUE(created.ok());
    const std::array targets{texture_action_target(created.value())};
    std::array<Vec2, 4> before{};
    for (std::size_t corner = 0; corner < kPoints.size(); ++corner) {
      before[corner] = expected_texture_uv(
          authored_uvs ? kAuthored[corner]
                       : Vec2{kPoints[corner].x * 0.5F, -kPoints[corner].y * 0.5F},
          settings);
    }
    Vec2 minimum = before.front();
    Vec2 maximum = before.front();
    for (const Vec2 uv : before) {
      minimum.x = std::min(minimum.x, uv.x);
      minimum.y = std::min(minimum.y, uv.y);
      maximum.x = std::max(maximum.x, uv.x);
      maximum.y = std::max(maximum.y, uv.y);
    }
    for (const FaceTextureAction action : kActions) {
      SCOPED_TRACE(static_cast<int>(action));
      auto expected = before;
      for (auto &uv : expected) {
        switch (action) {
        case FaceTextureAction::FitBoth:
          uv = {(uv.x - minimum.x) / (maximum.x - minimum.x),
                (uv.y - minimum.y) / (maximum.y - minimum.y)};
          break;
        case FaceTextureAction::FitX:
          uv.x = (uv.x - minimum.x) / (maximum.x - minimum.x);
          break;
        case FaceTextureAction::FitY:
          uv.y = (uv.y - minimum.y) / (maximum.y - minimum.y);
          break;
        case FaceTextureAction::JustifyLeft: uv.x -= minimum.x; break;
        case FaceTextureAction::JustifyRight: uv.x += 1.0F - maximum.x; break;
        case FaceTextureAction::JustifyTop: uv.y -= minimum.y; break;
        case FaceTextureAction::JustifyBottom: uv.y += 1.0F - maximum.y; break;
        case FaceTextureAction::JustifyCenter:
          uv.x += 0.5F - (minimum.x + maximum.x) * 0.5F;
          uv.y += 0.5F - (minimum.y + maximum.y) * 0.5F;
          break;
        default: FAIL() << "Unexpected fit/justify action";
        }
      }
      const auto result = prepare_face_texture_action(targets, {.action = action});
      ASSERT_TRUE(result.ok()) << result.error_message();
      const auto *prepared = prepared_texture_document(result.value());
      ASSERT_NE(prepared, nullptr);
      const auto topology = prepared->document.authored_topology();
      ASSERT_TRUE(topology.ok());
      expect_compiled_texture_uvs(prepared->document,
                                  topology.value().faces.front().vertices, expected);
      EXPECT_TRUE(prepared->document.has_same_geometry(created.value()));
      if (action == FaceTextureAction::JustifyLeft ||
          action == FaceTextureAction::JustifyRight ||
          action == FaceTextureAction::JustifyTop ||
          action == FaceTextureAction::JustifyBottom ||
          action == FaceTextureAction::JustifyCenter) {
        const auto &after = topology.value().faces.front().texture_transform;
        EXPECT_EQ(after.scale.x, settings.scale.x);
        EXPECT_EQ(after.scale.y, settings.scale.y);
        EXPECT_EQ(after.rotation_degrees, settings.rotation_degrees);
      }
    }
  }
}

TEST(ModelingSdkConsumerTests,
     FaceTextureFitAndJustifyNoOpsRetainExactDocumentIdentityAndSelection) {
  constexpr std::array<Vec3, 4> kPoints{{
      {0.0F, 0.0F, 0.0F}, {4.0F, 0.0F, 0.0F},
      {4.0F, 3.0F, 0.0F}, {0.0F, 3.0F, 0.0F},
  }};
  constexpr std::array<Vec2, 4> kAuthored{{
      {0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F},
  }};
  auto created = make_texture_action_face(kPoints, kAuthored);
  ASSERT_TRUE(created.ok());
  const std::array targets{texture_action_target(created.value())};
  for (const FaceTextureAction action : {
           FaceTextureAction::FitBoth, FaceTextureAction::FitX,
           FaceTextureAction::FitY, FaceTextureAction::JustifyLeft,
           FaceTextureAction::JustifyRight, FaceTextureAction::JustifyTop,
           FaceTextureAction::JustifyBottom, FaceTextureAction::JustifyCenter}) {
    SCOPED_TRACE(static_cast<int>(action));
    const auto result = prepare_face_texture_action(targets, {.action = action});
    ASSERT_TRUE(result.ok()) << result.error_message();
    EXPECT_FALSE(result.value().changed);
    const auto *prepared = prepared_texture_document(result.value());
    ASSERT_NE(prepared, nullptr);
    EXPECT_FALSE(prepared->receipt.changed);
    EXPECT_EQ(prepared->document.content_identity(), created.value().content_identity());
    EXPECT_EQ(prepared->document.content_revision(), created.value().content_revision());
    EXPECT_EQ(prepared->document.selection_revision(), created.value().selection_revision());
    EXPECT_TRUE(prepared->document.shares_selection_with(created.value()));
  }
}

TEST(ModelingSdkConsumerTests,
     FaceTextureGridFitAndJustifyBecomeExactNoOpsAfterRotatedAuthoredEdits) {
  constexpr std::array<Vec3, 4> kPoints{{
      {0.0F, 0.0F, 0.0F}, {4.0F, 0.0F, 0.0F},
      {4.0F, 3.0F, 0.0F}, {0.0F, 3.0F, 0.0F},
  }};
  constexpr std::array<Vec2, 4> kAuthored{{
      {0.2F, 0.1F}, {1.8F, 0.35F}, {2.1F, 1.7F}, {0.4F, 1.2F},
  }};
  auto created = make_texture_action_face(
      kPoints, kAuthored, {.scale = {-2.3F, 0.7F}, .shift_uv = {0.25F, -0.375F},
                         .rotation_degrees = 37.0F});
  ASSERT_TRUE(created.ok());
  const std::array targets{texture_action_target(created.value())};
  for (const FaceTextureAction action : {
           FaceTextureAction::AlignToGrid, FaceTextureAction::FitBoth,
           FaceTextureAction::FitX, FaceTextureAction::FitY,
           FaceTextureAction::JustifyLeft, FaceTextureAction::JustifyRight,
           FaceTextureAction::JustifyTop, FaceTextureAction::JustifyBottom,
           FaceTextureAction::JustifyCenter}) {
    SCOPED_TRACE(static_cast<int>(action));
    const auto first = prepare_face_texture_action(targets, {.action = action});
    ASSERT_TRUE(first.ok()) << first.error_message();
    ASSERT_TRUE(first.value().changed);
    const auto *prepared = prepared_texture_document(first.value());
    ASSERT_NE(prepared, nullptr);
    const std::array repeated_targets{texture_action_target(prepared->document)};
    const auto repeated = prepare_face_texture_action(repeated_targets, {.action = action});
    ASSERT_TRUE(repeated.ok()) << repeated.error_message();
    EXPECT_FALSE(repeated.value().changed);
    const auto *unchanged = prepared_texture_document(repeated.value());
    ASSERT_NE(unchanged, nullptr);
    EXPECT_FALSE(unchanged->receipt.changed);
    EXPECT_EQ(unchanged->document.content_identity(), prepared->document.content_identity());
    EXPECT_EQ(unchanged->document.content_revision(), prepared->document.content_revision());
    EXPECT_TRUE(unchanged->document.shares_selection_with(prepared->document));
  }
}

TEST(ModelingSdkConsumerTests,
     FaceTextureGroupedFitSpansSeparateWallObjectsAndGapsUsingOneChart) {
  constexpr std::array<Vec3, 4> kPoints{{
      {0.0F, 0.0F, 0.0F}, {2.0F, 0.0F, 0.0F},
      {2.0F, 2.0F, 0.0F}, {0.0F, 2.0F, 0.0F},
  }};
  constexpr std::array<Vec2, 4> kFirstUvs{{
      {0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F},
  }};
  constexpr std::array<Vec2, 4> kSecondUvs{{
      {2.0F, 4.0F}, {6.0F, 4.0F}, {6.0F, 8.0F}, {2.0F, 8.0F},
  }};
  auto first = make_texture_action_face(kPoints, kFirstUvs);
  auto second = make_texture_action_face(kPoints, kSecondUvs);
  ASSERT_TRUE(first.ok());
  ASSERT_TRUE(second.ok());
  std::array targets{texture_action_target(first.value(), 10U),
                     texture_action_target(second.value(), 20U)};
  targets[0].world_from_local.origin = {4.0F, 0.0F, 0.0F};
  targets[1].world_from_local.origin = {10.0F, 0.0F, 0.0F};
  for (const bool grouped : {false, true}) {
    for (const FaceTextureAction action : {FaceTextureAction::FitBoth,
                                           FaceTextureAction::FitX,
                                           FaceTextureAction::FitY}) {
      SCOPED_TRACE(grouped);
      SCOPED_TRACE(static_cast<int>(action));
      const auto result = prepare_face_texture_action(
          targets, {.action = action, .treat_as_one = grouped,
                    .active_target_key = 10U,
                    .active_face = first.value().face_ids().front()});
      ASSERT_TRUE(result.ok()) << result.error_message();
      for (std::size_t index = 0; index < targets.size(); ++index) {
        const auto *prepared = prepared_texture_document(result.value(), targets[index].key);
        ASSERT_NE(prepared, nullptr);
        const auto topology = prepared->document.authored_topology();
        ASSERT_TRUE(topology.ok());
        std::array<Vec2, 4> expected = index == 0U ? kFirstUvs : kSecondUvs;
        for (std::size_t corner = 0; corner < kPoints.size(); ++corner) {
          if (action != FaceTextureAction::FitY) {
            expected[corner].x = grouped
                ? (kPoints[corner].x + targets[index].world_from_local.origin.x - 4.0F) / 8.0F
                : kPoints[corner].x * 0.5F;
          }
          if (action != FaceTextureAction::FitX) {
            expected[corner].y = kPoints[corner].y * 0.5F;
          }
        }
        expect_compiled_texture_uvs(prepared->document,
                                    topology.value().faces.front().vertices, expected);
        EXPECT_TRUE(prepared->document.has_same_geometry(*targets[index].document));
      }
      auto repeated_targets = targets;
      for (auto &target : repeated_targets) {
        const auto *prepared = prepared_texture_document(result.value(), target.key);
        ASSERT_NE(prepared, nullptr);
        target.document = &prepared->document;
      }
      const auto repeated = prepare_face_texture_action(
          repeated_targets, {.action = action, .treat_as_one = grouped,
                             .active_target_key = 10U,
                             .active_face = first.value().face_ids().front()});
      ASSERT_TRUE(repeated.ok()) << repeated.error_message();
      EXPECT_FALSE(repeated.value().changed);
      for (const auto &target : repeated_targets) {
        const auto *unchanged = prepared_texture_document(repeated.value(), target.key);
        ASSERT_NE(unchanged, nullptr);
        EXPECT_EQ(unchanged->document.content_identity(), target.document->content_identity());
      }
    }
  }
}

TEST(ModelingSdkConsumerTests,
     FaceTextureGroupedJustifyUsesCombinedBoundsWithoutFittingOrTouchingOtherAxis) {
  constexpr std::array<Vec3, 4> kPoints{{
      {0.0F, 0.0F, 0.0F}, {2.0F, 0.0F, 0.0F},
      {2.0F, 3.0F, 0.0F}, {0.0F, 3.0F, 0.0F},
  }};
  constexpr std::array<Vec2, 4> kFirstUvs{{
      {0.25F, 0.375F}, {1.25F, 0.375F},
      {1.25F, 1.875F}, {0.25F, 1.875F},
  }};
  constexpr std::array<Vec2, 4> kSecondUvs{{
      {2.0F, 4.0F}, {6.0F, 4.0F}, {6.0F, 10.0F}, {2.0F, 10.0F},
  }};
  auto first = make_texture_action_face(kPoints, kFirstUvs);
  auto second = make_texture_action_face(kPoints, kSecondUvs);
  ASSERT_TRUE(first.ok());
  ASSERT_TRUE(second.ok());
  std::array targets{texture_action_target(first.value(), 10U),
                     texture_action_target(second.value(), 20U)};
  targets[0].world_from_local.origin = {4.0F, 0.0F, 0.0F};
  targets[1].world_from_local.origin = {10.0F, 0.0F, 0.0F};
  /// Describes the axes and offsets expected from shared-chart justification.
  struct JustifyCase {
    FaceTextureAction action;
    bool horizontal;
    bool vertical;
    Vec2 offset;
  };
  constexpr std::array kCases{
      JustifyCase{FaceTextureAction::JustifyLeft, true, false, {-0.25F, 0.0F}},
      JustifyCase{FaceTextureAction::JustifyRight, true, false, {-3.25F, 0.0F}},
      JustifyCase{FaceTextureAction::JustifyTop, false, true, {0.0F, -0.375F}},
      JustifyCase{FaceTextureAction::JustifyBottom, false, true, {0.0F, -0.875F}},
      JustifyCase{FaceTextureAction::JustifyCenter, true, true, {-1.75F, -0.625F}},
  };
  for (const auto &test : kCases) {
    SCOPED_TRACE(static_cast<int>(test.action));
    const auto result = prepare_face_texture_action(
        targets, {.action = test.action, .treat_as_one = true,
                  .active_target_key = 10U,
                  .active_face = first.value().face_ids().front()});
    ASSERT_TRUE(result.ok()) << result.error_message();
    for (std::size_t index = 0; index < targets.size(); ++index) {
      const auto *prepared = prepared_texture_document(result.value(), targets[index].key);
      ASSERT_NE(prepared, nullptr);
      const auto topology = prepared->document.authored_topology();
      ASSERT_TRUE(topology.ok());
      std::array<Vec2, 4> expected = index == 0U ? kFirstUvs : kSecondUvs;
      for (std::size_t corner = 0; corner < kPoints.size(); ++corner) {
        if (test.horizontal) {
          expected[corner].x = 0.25F +
              (kPoints[corner].x + targets[index].world_from_local.origin.x - 4.0F) * 0.5F +
              test.offset.x;
        }
        if (test.vertical) {
          expected[corner].y = 0.375F + kPoints[corner].y * 0.5F + test.offset.y;
        }
      }
      expect_compiled_texture_uvs(prepared->document,
                                  topology.value().faces.front().vertices, expected);
    }
  }
}

TEST(ModelingSdkConsumerTests,
     FaceTextureGroupedFitHonorsActiveReferenceAndDeterministicTargetFallback) {
  constexpr std::array<Vec3, 4> kPoints{{
      {0.0F, 0.0F, 0.0F}, {2.0F, 0.0F, 0.0F},
      {2.0F, 2.0F, 0.0F}, {0.0F, 2.0F, 0.0F},
  }};
  constexpr std::array<Vec2, 4> kFirstUvs{{
      {0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F},
  }};
  constexpr std::array<Vec2, 4> kReverseUvs{{
      {1.0F, 0.0F}, {0.0F, 0.0F}, {0.0F, 1.0F}, {1.0F, 1.0F},
  }};
  auto first = make_texture_action_face(kPoints, kFirstUvs);
  auto second = make_texture_action_face(kPoints, kReverseUvs);
  ASSERT_TRUE(first.ok());
  ASSERT_TRUE(second.ok());
  for (const bool reverse_targets : {false, true}) {
    std::array targets{texture_action_target(first.value(), 10U),
                       texture_action_target(second.value(), 20U)};
    targets[1].world_from_local.origin = {4.0F, 0.0F, 0.0F};
    if (reverse_targets) {
      std::ranges::reverse(targets);
    }
    for (const std::uint64_t active : {0U, 20U, 999U}) {
      SCOPED_TRACE(reverse_targets);
      SCOPED_TRACE(active);
      const auto result = prepare_face_texture_action(
          targets, {.action = FaceTextureAction::FitBoth, .treat_as_one = true,
                    .active_target_key = active,
                    .active_face = second.value().face_ids().front()});
      ASSERT_TRUE(result.ok()) << result.error_message();
      for (const auto &target : targets) {
        const auto *prepared = prepared_texture_document(result.value(), target.key);
        ASSERT_NE(prepared, nullptr);
        const auto topology = prepared->document.authored_topology();
        ASSERT_TRUE(topology.ok());
        std::array<Vec2, 4> expected{};
        for (std::size_t corner = 0; corner < kPoints.size(); ++corner) {
          const float world_x = kPoints[corner].x + target.world_from_local.origin.x;
          expected[corner] = {active == 20U ? 1.0F - world_x / 6.0F : world_x / 6.0F,
                              kPoints[corner].y * 0.5F};
        }
        expect_compiled_texture_uvs(prepared->document,
                                    topology.value().faces.front().vertices, expected);
      }
    }
  }
}

TEST(ModelingSdkConsumerTests,
     FaceTextureGroupedProjectionAcceptsDepthOffsetsButRejectsCollapsedFaces) {
  constexpr std::array<Vec3, 4> kWall{{
      {0.0F, 0.0F, 0.0F}, {2.0F, 0.0F, 0.0F},
      {2.0F, 2.0F, 0.0F}, {0.0F, 2.0F, 0.0F},
  }};
  constexpr std::array<Vec3, 4> kPerpendicular{{
      {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 2.0F},
      {0.0F, 2.0F, 2.0F}, {0.0F, 2.0F, 0.0F},
  }};
  constexpr std::array<Vec2, 4> kUvs{{
      {0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F},
  }};
  auto first = make_texture_action_face(kWall, kUvs);
  auto depth_offset = make_texture_action_face(kWall, kUvs);
  auto collapsed = make_texture_action_face(kPerpendicular, kUvs);
  ASSERT_TRUE(first.ok());
  ASSERT_TRUE(depth_offset.ok());
  ASSERT_TRUE(collapsed.ok());
  std::array targets{texture_action_target(first.value(), 1U),
                     texture_action_target(depth_offset.value(), 2U)};
  targets[1].world_from_local.origin = {4.0F, 0.0F, 5.0F};
  const FaceTextureActionOptions options{
      .action = FaceTextureAction::FitBoth, .treat_as_one = true,
      .active_target_key = 1U, .active_face = first.value().face_ids().front(),
  };
  const auto accepted = prepare_face_texture_action(targets, options);
  ASSERT_TRUE(accepted.ok()) << accepted.error_message();
  for (const auto &target : targets) {
    const auto *prepared = prepared_texture_document(accepted.value(), target.key);
    ASSERT_NE(prepared, nullptr);
    const auto topology = prepared->document.authored_topology();
    ASSERT_TRUE(topology.ok());
    std::array<Vec2, 4> expected{};
    for (std::size_t corner = 0; corner < kWall.size(); ++corner) {
      expected[corner] = {(kWall[corner].x + target.world_from_local.origin.x) / 6.0F,
                          kWall[corner].y * 0.5F};
    }
    expect_compiled_texture_uvs(prepared->document,
                                topology.value().faces.front().vertices, expected);
  }
  targets[1] = texture_action_target(collapsed.value(), 2U);
  targets[1].world_from_local.origin = {4.0F, 0.0F, 0.0F};
  const auto first_identity = first.value().content_identity();
  const auto second_identity = collapsed.value().content_identity();
  const auto rejected = prepare_face_texture_action(targets, options);
  ASSERT_FALSE(rejected.ok());
  EXPECT_FALSE(rejected.error_message().empty());
  EXPECT_EQ(first.value().content_identity(), first_identity);
  EXPECT_EQ(collapsed.value().content_identity(), second_identity);
  const auto separate = prepare_face_texture_action(
      targets, {.action = FaceTextureAction::FitBoth});
  EXPECT_TRUE(separate.ok()) << separate.error_message();
}

TEST(ModelingSdkConsumerTests,
     FaceTextureGeometryFlipsCarryUvsAndUseWorldPlaneAxesInsteadOfTextureRotation) {
  constexpr std::array<Vec3, 4> kPoints{{
      {0.0F, 0.0F, 0.0F}, {4.0F, 0.0F, 0.0F},
      {3.0F, 2.0F, 0.0F}, {1.0F, 3.0F, 0.0F},
  }};
  constexpr std::array<Vec2, 4> kAuthored{{
      {0.2F, 0.1F}, {1.8F, 0.35F}, {2.1F, 1.7F}, {0.4F, 1.2F},
  }};
  const FaceTextureTransform settings{
      .scale = {2.0F, -0.5F}, .shift_uv = {0.2F, -0.3F},
      .rotation_degrees = 37.0F,
  };
  for (const bool authored_uvs : {false, true}) {
    auto created = make_texture_action_face(
        kPoints, authored_uvs ? std::span<const Vec2>(kAuthored)
                             : std::span<const Vec2>{}, settings);
    ASSERT_TRUE(created.ok());
    const auto before = created.value().authored_topology();
    ASSERT_TRUE(before.ok());
    for (const bool turned_object : {false, true}) {
      auto target = texture_action_target(created.value());
      if (turned_object) {
        target.world_from_local = {
            .x_axis = {0.0F, 2.0F, 0.0F}, .y_axis = {-3.0F, 0.0F, 0.0F},
            .z_axis = {0.0F, 0.0F, 1.0F}, .origin = {4.0F, 7.0F, 0.0F},
        };
      }
      for (const FaceTextureAction action : {FaceTextureAction::FlipFaceHorizontal,
                                             FaceTextureAction::FlipFaceVertical}) {
        SCOPED_TRACE(authored_uvs);
        SCOPED_TRACE(turned_object);
        SCOPED_TRACE(static_cast<int>(action));
        const auto result = prepare_face_texture_action(
            std::span<const FaceTextureActionTarget>(&target, 1U), {.action = action});
        ASSERT_TRUE(result.ok()) << result.error_message();
        ASSERT_TRUE(result.value().changed);
        const auto *prepared = prepared_texture_document(result.value());
        ASSERT_NE(prepared, nullptr);
        const auto topology = prepared->document.authored_topology();
        ASSERT_TRUE(topology.ok());
        EXPECT_FALSE(prepared->document.has_same_geometry(created.value()));
        EXPECT_EQ(prepared->document.vertex_ids(), created.value().vertex_ids());
        EXPECT_EQ(prepared->document.face_ids(), created.value().face_ids());
        EXPECT_EQ(prepared->document.selection().faces, created.value().selection().faces);
        EXPECT_EQ(topology.value().faces.front().texture_transform, settings);
        EXPECT_EQ(topology.value().faces.front().material_slot,
                  before.value().faces.front().material_slot);
        expect_reversed_face_winding(created.value(), prepared->document);
        const bool flip_local_x =
            (action == FaceTextureAction::FlipFaceHorizontal) != turned_object;
        std::array<Vec2, 4> expected_uvs{};
        for (std::size_t corner = 0; corner < kPoints.size(); ++corner) {
          const VertexId vertex = before.value().faces.front().vertices[corner];
          const auto found = std::ranges::find(topology.value().vertices, vertex,
                                                &AuthoredTopologyVertex::id);
          ASSERT_NE(found, topology.value().vertices.end());
          expect_vec3_near(found->position,
                            {flip_local_x ? 4.0F - kPoints[corner].x : kPoints[corner].x,
                             flip_local_x ? kPoints[corner].y : 3.0F - kPoints[corner].y,
                             0.0F});
          expected_uvs[corner] = expected_texture_uv(
              authored_uvs ? kAuthored[corner]
                           : Vec2{kPoints[corner].x * 0.5F, -kPoints[corner].y * 0.5F},
              settings);
        }
        expect_compiled_texture_uvs(prepared->document,
                                    before.value().faces.front().vertices, expected_uvs);
        const auto mesh = prepared->document.compile_mesh();
        ASSERT_TRUE(mesh.ok());
        for (const auto &vertex : mesh.value().vertices) {
          expect_vec3_near(vertex.normal, {0.0F, 0.0F, 1.0F});
        }
        EXPECT_TRUE(prepared->document.validate().ok());
      }
    }
  }
}

[[nodiscard]] Result<PolygonDocument> make_connected_texture_wall() {
  AuthoredTopology topology;
  topology.vertices = {
      {make_id<VertexTag>(1U), {0.0F, 0.0F, 0.0F}},
      {make_id<VertexTag>(2U), {2.0F, 0.0F, 0.0F}},
      {make_id<VertexTag>(3U), {2.0F, 2.0F, 0.0F}},
      {make_id<VertexTag>(4U), {0.0F, 2.0F, 0.0F}},
      {make_id<VertexTag>(5U), {4.0F, 0.0F, 0.0F}},
      {make_id<VertexTag>(6U), {4.0F, 2.0F, 0.0F}},
  };
  topology.faces = {
      {.id = make_id<FaceTag>(1U),
       .vertices = {make_id<VertexTag>(1U), make_id<VertexTag>(2U),
                    make_id<VertexTag>(3U), make_id<VertexTag>(4U)},
       .material_slot = 3U},
      {.id = make_id<FaceTag>(2U),
       .vertices = {make_id<VertexTag>(2U), make_id<VertexTag>(5U),
                    make_id<VertexTag>(6U), make_id<VertexTag>(3U)},
       .material_slot = 7U},
  };
  topology.hard_normal_edges = {
      make_edge_key(make_id<VertexTag>(2U), make_id<VertexTag>(3U))};
  topology.next_vertex_id = 7U;
  topology.next_face_id = 3U;
  return PolygonDocument::from_authored_topology(topology);
}

TEST(ModelingSdkConsumerTests,
     FaceTextureGroupedReferenceFallsBackToSmallestFaceRatherThanStorageOrder) {
  auto wall = make_connected_texture_wall();
  ASSERT_TRUE(wall.ok());
  auto topology = wall.value().authored_topology();
  ASSERT_TRUE(topology.ok());
  topology.value().faces[0].uvs = {
      {1.0F, 0.0F}, {0.0F, 0.0F}, {0.0F, 1.0F}, {1.0F, 1.0F}};
  topology.value().faces[1].uvs = {
      {0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}};
  std::ranges::reverse(topology.value().faces);
  auto imported = PolygonDocument::from_authored_topology(topology.value());
  ASSERT_TRUE(imported.ok());
  auto target = texture_action_target(imported.value());
  target.faces = {{.face = make_id<FaceTag>(2U)}, {.face = make_id<FaceTag>(1U)}};
  for (const bool active_second : {false, true}) {
    SCOPED_TRACE(active_second);
    const auto result = prepare_face_texture_action(
        std::span<const FaceTextureActionTarget>(&target, 1U),
        {.action = FaceTextureAction::FitBoth, .treat_as_one = true,
         .active_target_key = 1U,
         .active_face = active_second ? make_id<FaceTag>(2U) : FaceId{}});
    ASSERT_TRUE(result.ok()) << result.error_message();
    const auto *prepared = prepared_texture_document(result.value());
    ASSERT_NE(prepared, nullptr);
    std::vector<VertexId> vertices;
    std::vector<Vec2> expected;
    for (const auto &vertex : topology.value().vertices) {
      vertices.push_back(vertex.id);
      expected.push_back({active_second ? vertex.position.x * 0.25F
                                         : 1.0F - vertex.position.x * 0.25F,
                           vertex.position.y * 0.5F});
    }
    expect_compiled_texture_uvs(prepared->document, vertices, expected);
  }
}

TEST(ModelingSdkConsumerTests,
     FaceTextureGeometryFlipsRejectConflictingSharedVerticesAndPreserveConnectedGroups) {
  auto created = make_connected_texture_wall();
  ASSERT_TRUE(created.ok()) << created.error_message();
  ASSERT_TRUE(created.value().select_all_faces().ok());
  const auto before = created.value().authored_topology();
  ASSERT_TRUE(before.ok());
  const auto identity = created.value().content_identity();
  const std::array targets{texture_action_target(created.value())};
  const auto conflicting = prepare_face_texture_action(
      targets, {.action = FaceTextureAction::FlipFaceHorizontal});
  ASSERT_FALSE(conflicting.ok());
  EXPECT_FALSE(conflicting.error_message().empty());
  EXPECT_EQ(created.value().content_identity(), identity);

  const auto grouped = prepare_face_texture_action(
      targets, {.action = FaceTextureAction::FlipFaceHorizontal, .treat_as_one = true});
  ASSERT_TRUE(grouped.ok()) << grouped.error_message();
  const auto *prepared = prepared_texture_document(grouped.value());
  ASSERT_NE(prepared, nullptr);
  EXPECT_EQ(prepared->document.vertex_count(), 6U);
  EXPECT_EQ(prepared->document.face_count(), 2U);
  EXPECT_EQ(prepared->document.edge_ids().size(), 7U);
  EXPECT_EQ(prepared->document.hard_edge_ids(), created.value().hard_edge_ids());
  EXPECT_EQ(prepared->document.selection().faces, created.value().selection().faces);
  EXPECT_TRUE(prepared->document.validate().ok());
  expect_reversed_face_winding(created.value(), prepared->document);
  const auto after = prepared->document.authored_topology();
  ASSERT_TRUE(after.ok());
  std::vector<VertexId> vertices;
  std::vector<Vec2> expected_uvs;
  for (const auto &source : before.value().vertices) {
    const auto found = std::ranges::find(after.value().vertices, source.id,
                                          &AuthoredTopologyVertex::id);
    ASSERT_NE(found, after.value().vertices.end());
    expect_vec3_near(found->position,
                      {4.0F - source.position.x, source.position.y, source.position.z});
    vertices.push_back(source.id);
    expected_uvs.push_back({source.position.x * 0.5F, -source.position.y * 0.5F});
  }
  expect_compiled_texture_uvs(prepared->document, vertices, expected_uvs);
  for (std::size_t face = 0; face < after.value().faces.size(); ++face) {
    EXPECT_EQ(after.value().faces[face].material_slot, before.value().faces[face].material_slot);
  }

  auto partial = targets;
  partial.front().faces.resize(1U);
  const auto invalid_winding = prepare_face_texture_action(
      partial, {.action = FaceTextureAction::FlipFaceHorizontal});
  ASSERT_FALSE(invalid_winding.ok());
  EXPECT_FALSE(invalid_winding.error_message().empty());
  EXPECT_EQ(created.value().content_identity(), identity);
}

TEST(ModelingSdkConsumerTests,
     FaceTextureGeometryFlipRejectsPartialClosedMeshesButAcceptsWholeConnectedSurface) {
  auto created = PolygonDocument::make_box({});
  ASSERT_TRUE(created.ok());
  ASSERT_TRUE(created.value().select_all_faces().ok());
  const auto facing = created.value().face_ids_by_normal({0.0F, 0.0F, 1.0F});
  ASSERT_EQ(facing.size(), 1U);
  auto target = texture_action_target(created.value());
  target.faces = {{.face = facing.front()}};
  const auto identity = created.value().content_identity();
  const auto partial = prepare_face_texture_action(
      std::span<const FaceTextureActionTarget>(&target, 1U),
      {.action = FaceTextureAction::FlipFaceHorizontal});
  ASSERT_FALSE(partial.ok());
  EXPECT_EQ(created.value().content_identity(), identity);
  target = texture_action_target(created.value());
  const auto complete = prepare_face_texture_action(
      std::span<const FaceTextureActionTarget>(&target, 1U),
      {.action = FaceTextureAction::FlipFaceHorizontal, .treat_as_one = true,
       .active_target_key = 1U, .active_face = facing.front()});
  ASSERT_TRUE(complete.ok()) << complete.error_message();
  const auto *prepared = prepared_texture_document(complete.value());
  ASSERT_NE(prepared, nullptr);
  EXPECT_EQ(prepared->document.vertex_ids(), created.value().vertex_ids());
  EXPECT_EQ(prepared->document.face_ids(), created.value().face_ids());
  EXPECT_EQ(prepared->document.selection().faces, created.value().selection().faces);
  EXPECT_TRUE(prepared->document.validate().ok());
  expect_reversed_face_winding(created.value(), prepared->document);
  const auto payload = prepared->document.authored_payload();
  ASSERT_TRUE(payload.ok());
  EXPECT_EQ(authored_open_edge_count(payload.value()), 0U);
}

TEST(ModelingSdkConsumerTests,
     FaceTextureSharedVertexConflictsRemainInvalidFarFromTheWorldOrigin) {
  for (const float origin : {0.0F, 1000000.0F}) {
    SCOPED_TRACE(origin);
    AuthoredTopology topology;
    topology.vertices = {
        {make_id<VertexTag>(1U), {origin + 1.0F, 1.0F, 0.0F}},
        {make_id<VertexTag>(2U), {origin + 2.0F, 0.0F, 0.0F}},
        {make_id<VertexTag>(3U), {origin + 0.875F, 0.0F, 0.0F}},
        {make_id<VertexTag>(4U), {origin + 2.125F, 2.0F, 0.0F}},
    };
    topology.faces = {
        {.id = make_id<FaceTag>(1U),
         .vertices = {make_id<VertexTag>(2U), make_id<VertexTag>(1U),
                      make_id<VertexTag>(3U)}},
        {.id = make_id<FaceTag>(2U),
         .vertices = {make_id<VertexTag>(1U), make_id<VertexTag>(2U),
                      make_id<VertexTag>(4U)}},
    };
    topology.next_vertex_id = 5U;
    topology.next_face_id = 3U;
    auto document = PolygonDocument::from_authored_topology(topology);
    ASSERT_TRUE(document.ok()) << document.error_message();
    const auto identity = document.value().content_identity();
    const std::array targets{texture_action_target(document.value())};
    // The two reflections disagree by .25 units at both shared endpoints;
    // a large world-coordinate magnitude must not make them "near enough".
    const auto result = prepare_face_texture_action(
        targets, {.action = FaceTextureAction::FlipFaceHorizontal});
    ASSERT_FALSE(result.ok());
    EXPECT_FALSE(result.error_message().empty());
    EXPECT_EQ(document.value().content_identity(), identity);
  }
}

TEST(ModelingSdkConsumerTests,
     FaceTextureGeometryFlipLeavesAnUnselectedNeighborAtAFixedVertexUntouched) {
  AuthoredTopology topology;
  topology.vertices = {
      {make_id<VertexTag>(1U), {-1.0F, 0.0F, 0.0F}},
      {make_id<VertexTag>(2U), {1.0F, 0.0F, 0.0F}},
      {make_id<VertexTag>(3U), {0.0F, 1.0F, 0.0F}},
      {make_id<VertexTag>(4U), {0.0F, 2.0F, 0.0F}},
      {make_id<VertexTag>(5U), {0.0F, 1.0F, 1.0F}},
  };
  topology.faces = {
      {.id = make_id<FaceTag>(1U),
       .vertices = {make_id<VertexTag>(1U), make_id<VertexTag>(2U),
                    make_id<VertexTag>(3U)}, .material_slot = 3U},
      {.id = make_id<FaceTag>(2U),
       .vertices = {make_id<VertexTag>(3U), make_id<VertexTag>(4U),
                    make_id<VertexTag>(5U)}, .material_slot = 9U,
       .shading = AuthoredSurfaceShading::Smooth},
  };
  topology.next_vertex_id = 6U;
  topology.next_face_id = 3U;
  auto document = PolygonDocument::from_authored_topology(topology);
  ASSERT_TRUE(document.ok()) << document.error_message();
  ASSERT_TRUE(document.value().select_face(make_id<FaceTag>(1U)).ok());
  auto target = texture_action_target(document.value());
  target.faces = {{.face = make_id<FaceTag>(1U)}};
  const auto result = prepare_face_texture_action(
      std::span<const FaceTextureActionTarget>(&target, 1U),
      {.action = FaceTextureAction::FlipFaceHorizontal});
  ASSERT_TRUE(result.ok()) << result.error_message();
  const auto *prepared = prepared_texture_document(result.value());
  ASSERT_NE(prepared, nullptr);
  const auto after = prepared->document.authored_topology();
  ASSERT_TRUE(after.ok());
  const auto neighbor = std::ranges::find(after.value().faces, make_id<FaceTag>(2U),
                                          &AuthoredTopologyFace::id);
  ASSERT_NE(neighbor, after.value().faces.end());
  EXPECT_TRUE(neighbor->uvs.empty());
  EXPECT_EQ(neighbor->vertices, topology.faces[1].vertices);
  EXPECT_EQ(neighbor->material_slot, topology.faces[1].material_slot);
  EXPECT_EQ(neighbor->shading, topology.faces[1].shading);
  EXPECT_EQ(neighbor->texture_transform, topology.faces[1].texture_transform);
  for (const VertexId id : neighbor->vertices) {
    const auto source = std::ranges::find(topology.vertices, id, &AuthoredTopologyVertex::id);
    const auto current = std::ranges::find(after.value().vertices, id,
                                            &AuthoredTopologyVertex::id);
    ASSERT_NE(source, topology.vertices.end());
    ASSERT_NE(current, after.value().vertices.end());
    expect_vec3_near(current->position, source->position, 0.0F);
  }
  EXPECT_EQ(prepared->receipt.affected.faces,
            (std::vector<FaceId>{make_id<FaceTag>(1U)}));
  EXPECT_EQ(prepared->receipt.affected.vertices,
            (std::vector<VertexId>{make_id<VertexTag>(1U), make_id<VertexTag>(2U)}));
  EXPECT_EQ(prepared->document.selection().faces, document.value().selection().faces);
}

TEST(ModelingSdkConsumerTests,
     FaceTextureActionsRejectInvalidTargetBatchesWithoutChangingAnySource) {
  constexpr std::array<Vec3, 3> kPoints{{
      {0.0F, 0.0F, 0.0F}, {4.0F, 0.0F, 0.0F}, {0.0F, 4.0F, 0.0F},
  }};
  auto first = make_texture_action_face(kPoints);
  auto second = make_texture_action_face(kPoints);
  ASSERT_TRUE(first.ok());
  ASSERT_TRUE(second.ok());
  const auto first_identity = first.value().content_identity();
  const auto second_identity = second.value().content_identity();
  const auto first_revision = first.value().content_revision();
  const auto second_revision = second.value().content_revision();
  for (int invalid_case = 0; invalid_case < 10; ++invalid_case) {
    SCOPED_TRACE(invalid_case);
    std::array targets{texture_action_target(first.value(), 1U),
                       texture_action_target(second.value(), 2U)};
    FaceTextureActionOptions options{.action = FaceTextureAction::ShiftLeft};
    switch (invalid_case) {
    case 0: targets[1].document = nullptr; break;
    case 1: targets[1].key = 1U; break;
    case 2: targets[1].faces.push_back(targets[1].faces.front()); break;
    case 3: targets[1].faces.front().face.generation = 2U; break;
    case 4:
      targets[1].world_from_local.origin.x = std::numeric_limits<float>::quiet_NaN();
      break;
    case 5: targets[1].world_from_local.z_axis = {}; break;
    case 6:
      targets[1].sampling_from_local.origin.y = std::numeric_limits<float>::infinity();
      break;
    case 7: targets[1].sampling_from_local.z_axis = {}; break;
    case 8: options.action = static_cast<FaceTextureAction>(999); break;
    case 9:
      options.action = FaceTextureAction::RotateClockwise;
      options.rotation_degrees = std::numeric_limits<double>::quiet_NaN();
      break;
    default: FAIL() << "Unexpected invalid target case";
    }
    const auto result = prepare_face_texture_action(targets, options);
    ASSERT_FALSE(result.ok());
    EXPECT_FALSE(result.error_message().empty());
    EXPECT_EQ(first.value().content_identity(), first_identity);
    EXPECT_EQ(second.value().content_identity(), second_identity);
    EXPECT_EQ(first.value().content_revision(), first_revision);
    EXPECT_EQ(second.value().content_revision(), second_revision);
  }
}

TEST(ModelingSdkConsumerTests,
     FaceTexturePreparedActionBatchPreservesSelectionAndExactUndoRedo) {
  constexpr std::array<Vec3, 4> kPoints{{
      {0.0F, 0.0F, 0.0F}, {4.0F, 0.0F, 0.0F},
      {3.0F, 2.0F, 0.0F}, {1.0F, 3.0F, 0.0F},
  }};
  constexpr std::array<Vec2, 4> kAuthored{{
      {0.2F, 0.1F}, {1.8F, 0.35F}, {2.1F, 1.7F}, {0.4F, 1.2F},
  }};
  ModelingSession session;
  std::vector<ObjectId> objects;
  for (const char *name : {"First", "Second"}) {
    auto created = make_texture_action_face(kPoints, kAuthored);
    ASSERT_TRUE(created.ok());
    const auto added = session.add_document(std::move(created).value(), name);
    ASSERT_TRUE(added.ok());
    objects.push_back(added.value());
  }
  ASSERT_TRUE(session.apply_object_selection(objects).ok());
  std::vector<PolygonDocument> originals;
  std::vector<std::vector<std::byte>> before;
  for (const ObjectId object : objects) {
    auto document = session.document_copy(object);
    ASSERT_TRUE(document.ok());
    auto bytes = serialize_qdr_authored_topology(document.value());
    ASSERT_TRUE(bytes.ok());
    before.push_back(std::move(bytes).value());
    originals.push_back(std::move(document).value());
  }
  std::vector<FaceTextureActionTarget> targets;
  for (std::size_t index = 0; index < objects.size(); ++index) {
    targets.push_back(texture_action_target(originals[index], objects[index].index));
  }
  const auto prepared = prepare_face_texture_action(
      targets, {.action = FaceTextureAction::FlipFaceHorizontal});
  ASSERT_TRUE(prepared.ok()) << prepared.error_message();
  ASSERT_TRUE(prepared.value().changed);
  std::vector<SessionDocumentUpdate> updates;
  std::vector<std::vector<std::byte>> after;
  for (const ObjectId object : objects) {
    const auto *replacement = prepared_texture_document(prepared.value(), object.index);
    ASSERT_NE(replacement, nullptr);
    const auto bytes = serialize_qdr_authored_topology(replacement->document);
    ASSERT_TRUE(bytes.ok());
    after.push_back(bytes.value());
    updates.push_back({.object = object, .document = replacement->document,
                       .receipt = replacement->receipt});
  }
  const auto revision = session.content_revision();
  const auto committed = session.commit_documents_batch(std::move(updates), true);
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  EXPECT_EQ(session.content_revision(), revision + 1U);
  for (const ObjectId object : objects) {
    expect_session_object_selected(session, object, true);
  }
  ASSERT_TRUE(session.undo().ok());
  for (std::size_t index = 0; index < objects.size(); ++index) {
    const auto document = session.document_copy(objects[index]);
    ASSERT_TRUE(document.ok());
    const auto bytes = serialize_qdr_authored_topology(document.value());
    ASSERT_TRUE(bytes.ok());
    EXPECT_EQ(bytes.value(), before[index]);
    EXPECT_EQ(document.value().selection().faces, originals[index].selection().faces);
    expect_session_object_selected(session, objects[index], true);
  }
  ASSERT_TRUE(session.redo().ok());
  for (std::size_t index = 0; index < objects.size(); ++index) {
    const auto document = session.document_copy(objects[index]);
    ASSERT_TRUE(document.ok());
    const auto bytes = serialize_qdr_authored_topology(document.value());
    ASSERT_TRUE(bytes.ok());
    EXPECT_EQ(bytes.value(), after[index]);
    EXPECT_EQ(document.value().selection().faces, originals[index].selection().faces);
    expect_session_object_selected(session, objects[index], true);
  }
}

TEST(ModelingSdkConsumerTests,
     FaceTextureUpdatesRejectInvalidBatchesWithoutPublishingPartialChanges) {
  auto created = PolygonDocument::make_box({});
  ASSERT_TRUE(created.ok());
  auto document = std::move(created).value();
  const auto ids = document.face_ids();
  const auto before = serialize_qdr_authored_topology(document);
  ASSERT_TRUE(before.ok());
  const auto identity = document.content_identity();
  const auto revision = document.content_revision();
  for (const float invalid : {0.0F, -0.0F,
                             std::numeric_limits<float>::infinity(),
                             std::numeric_limits<float>::quiet_NaN()}) {
    const std::array<FaceTextureTransformUpdate, 2> updates{
        FaceTextureTransformUpdate{.face = ids[0], .scale_x = 2.0F},
        FaceTextureTransformUpdate{.face = ids[1], .scale_y = invalid},
    };
    EXPECT_FALSE(document.set_face_texture_transforms(updates).ok());
    EXPECT_EQ(document.content_identity(), identity);
    EXPECT_EQ(document.content_revision(), revision);
  }
  const std::array<FaceTextureTransformUpdate, 2> duplicates{
      FaceTextureTransformUpdate{.face = ids[0], .scale_x = 2.0F},
      FaceTextureTransformUpdate{.face = ids[0], .scale_x = 3.0F},
  };
  EXPECT_FALSE(document.set_face_texture_transforms(duplicates).ok());
  const std::array<FaceTextureTransformUpdate, 1> stale{
      FaceTextureTransformUpdate{.face = make_id<FaceTag>(ids[0].index, 2U),
                                 .scale_x = 2.0F},
  };
  EXPECT_FALSE(document.set_face_texture_transforms(stale).ok());
  const auto after = serialize_qdr_authored_topology(document);
  ASSERT_TRUE(after.ok());
  EXPECT_EQ(after.value(), before.value());
}

TEST(ModelingSdkConsumerTests,
     Qgeo11PreservesTextureSettingsAndReadsLegacy10IdentityDefaults) {
  auto created = PolygonDocument::make_box({});
  ASSERT_TRUE(created.ok());
  auto document = std::move(created).value();
  const auto ids = document.face_ids();
  const std::array<FaceTextureTransformUpdate, 1> updates{
      FaceTextureTransformUpdate{.face = ids.front(), .scale_x = -2.0F,
                                 .scale_y = 0.5F, .shift_x = 0.125F,
                                 .shift_y = -0.25F, .rotation_degrees = 67.5F},
  };
  ASSERT_TRUE(document.set_face_texture_transforms(updates).ok());
  const auto encoded = serialize_qdr_authored_topology(document);
  ASSERT_TRUE(encoded.ok()) << encoded.error_message();
  EXPECT_EQ(encoded.value()[6], std::byte{1});
  const auto decoded = deserialize_qdr_authored_topology(encoded.value());
  ASSERT_TRUE(decoded.ok()) << decoded.error_message();
  const auto original = document.face_texture_settings(ids);
  const auto restored = decoded.value().face_texture_settings(ids);
  ASSERT_TRUE(original.ok());
  ASSERT_TRUE(restored.ok());
  for (std::size_t index = 0U; index < ids.size(); ++index) {
    EXPECT_EQ(restored.value()[index].texture_transform,
              original.value()[index].texture_transform);
  }
  const auto reencoded = serialize_qdr_authored_topology(decoded.value());
  ASSERT_TRUE(reencoded.ok());
  EXPECT_EQ(reencoded.value(), encoded.value());

  // Version 1.0 ended after the hard-edge array and stored no face transforms.
  auto legacy = encoded.value();
  legacy.resize(legacy.size() - ids.size() * 20U);
  legacy[6] = std::byte{0};
  write_u32_le(legacy, 16U, static_cast<std::uint32_t>(legacy.size()));
  write_u32_le(legacy, 20U, 0U);
  const auto migrated = deserialize_qdr_authored_topology(legacy);
  ASSERT_TRUE(migrated.ok()) << migrated.error_message();
  const auto defaults = migrated.value().face_texture_settings(ids);
  ASSERT_TRUE(defaults.ok());
  for (const auto &face : defaults.value()) {
    EXPECT_EQ(face.texture_transform, FaceTextureTransform{});
  }

  auto invalid = encoded.value();
  write_u32_le(invalid, invalid.size() - ids.size() * 20U, 0U);
  EXPECT_FALSE(deserialize_qdr_authored_topology(invalid).ok());
  auto unsupported = encoded.value();
  unsupported[6] = std::byte{2};
  EXPECT_FALSE(deserialize_qdr_authored_topology(unsupported).ok());
}

TEST(ModelingSdkConsumerTests,
     FaceTextureDocumentBatchPreservesObjectSelectionAndReplaysUndoRedo) {
  ModelingSession session;
  auto first = PolygonDocument::make_box({});
  auto second = PolygonDocument::make_box({});
  ASSERT_TRUE(first.ok());
  ASSERT_TRUE(second.ok());
  const auto first_id = session.add_document(std::move(first).value(), "First");
  const auto second_id = session.add_document(std::move(second).value(), "Second");
  ASSERT_TRUE(first_id.ok());
  ASSERT_TRUE(second_id.ok());
  const std::array<ObjectId, 2> objects{first_id.value(), second_id.value()};
  ASSERT_TRUE(session.apply_object_selection(objects).ok());
  std::vector<SessionDocumentUpdate> updates;
  for (const auto object : objects) {
    auto document = session.document_copy(object);
    ASSERT_TRUE(document.ok());
    const std::array<FaceTextureTransformUpdate, 1> edit{
        FaceTextureTransformUpdate{.face = document.value().face_ids().front(),
                                   .scale_x = 3.0F, .rotation_degrees = 20.0F},
    };
    const auto receipt = document.value().set_face_texture_transforms(edit);
    ASSERT_TRUE(receipt.ok());
    updates.push_back({.object = object, .document = std::move(document).value(),
                       .receipt = receipt.value()});
  }
  const auto before_revision = session.content_revision();
  const auto committed = session.commit_documents_batch(std::move(updates), true);
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  EXPECT_EQ(session.content_revision(), before_revision + 1U);
  for (const auto object : objects) {
    expect_session_object_selected(session, object, true);
  }
  ASSERT_TRUE(session.undo().ok());
  for (const auto object : objects) {
    const auto document = session.document_copy(object);
    ASSERT_TRUE(document.ok());
    const auto settings = document.value().face_texture_settings(document.value().face_ids());
    ASSERT_TRUE(settings.ok());
    EXPECT_EQ(settings.value().front().texture_transform, FaceTextureTransform{});
    expect_session_object_selected(session, object, true);
  }
  ASSERT_TRUE(session.redo().ok());
  for (const auto object : objects) {
    const auto document = session.document_copy(object);
    ASSERT_TRUE(document.ok());
    const auto settings = document.value().face_texture_settings(document.value().face_ids());
    ASSERT_TRUE(settings.ok());
    EXPECT_EQ(settings.value().front().texture_transform.scale.x, 3.0F);
    EXPECT_EQ(settings.value().front().texture_transform.rotation_degrees, 20.0F);
  }
}

/// Stores a public authored strip and its ordered longitudinal vertex rails.
struct UvPeelStripFixture {
  AuthoredTopology topology;
  std::vector<std::vector<VertexId>> rails;
};

[[nodiscard]] UvPeelStripFixture make_uv_peel_strip(
    const std::vector<std::vector<Vec3>> &rail_positions, bool closed,
    bool authored_uvs = false, FaceTextureTransform texture_transform = {}) {
  UvPeelStripFixture fixture;
  if (rail_positions.size() < 2U || rail_positions.front().size() < 2U) {
    return fixture;
  }
  const std::size_t station_count = rail_positions.front().size();
  for (const auto &rail : rail_positions) {
    if (rail.size() != station_count) {
      return {};
    }
  }
  fixture.rails.resize(rail_positions.size());
  for (std::size_t rail = 0U; rail < rail_positions.size(); ++rail) {
    for (std::size_t station = 0U; station < station_count; ++station) {
      const VertexId id = make_id<VertexTag>(
          static_cast<std::uint32_t>(fixture.topology.vertices.size() + 1U));
      fixture.topology.vertices.push_back({id, rail_positions[rail][station]});
      fixture.rails[rail].push_back(id);
    }
  }
  const std::size_t segment_count = closed ? station_count : station_count - 1U;
  for (std::size_t rail = 0U; rail + 1U < fixture.rails.size(); ++rail) {
    for (std::size_t segment = 0U; segment < segment_count; ++segment) {
      const std::size_t next = (segment + 1U) % station_count;
      AuthoredTopologyFace face{
          .id = make_id<FaceTag>(
              static_cast<std::uint32_t>(fixture.topology.faces.size() + 1U)),
          .vertices = {fixture.rails[rail][segment],
                       fixture.rails[rail + 1U][segment],
                       fixture.rails[rail + 1U][next],
                       fixture.rails[rail][next]},
          .material_slot = static_cast<std::uint32_t>(rail * 7U + segment + 1U),
          .texture_transform = texture_transform,
      };
      if (authored_uvs) {
        const float seed = static_cast<float>(face.id.index) * 0.17F;
        face.uvs = {{seed + 0.2F, -seed + 0.1F},
                    {seed + 1.3F, -seed + 0.25F},
                    {seed + 1.5F, -seed + 2.1F},
                    {seed - 0.2F, -seed + 1.7F}};
      }
      fixture.topology.faces.push_back(std::move(face));
    }
  }
  fixture.topology.next_vertex_id =
      static_cast<std::uint32_t>(fixture.topology.vertices.size() + 1U);
  fixture.topology.next_face_id =
      static_cast<std::uint32_t>(fixture.topology.faces.size() + 1U);
  return fixture;
}

[[nodiscard]] std::vector<EdgeKey>
uv_peel_rail_edges(std::span<const VertexId> rail, bool closed) {
  std::vector<EdgeKey> edges;
  if (rail.size() < 2U) {
    return edges;
  }
  edges.reserve(closed ? rail.size() : rail.size() - 1U);
  for (std::size_t index = 0U; index + 1U < rail.size(); ++index) {
    edges.push_back(make_edge_key(rail[index], rail[index + 1U]));
  }
  if (closed) {
    edges.push_back(make_edge_key(rail.back(), rail.front()));
  }
  return edges;
}

[[nodiscard]] UvPeelTarget uv_peel_target(
    const PolygonDocument &document, std::uint64_t key,
    std::vector<EdgeKey> edges, std::optional<EdgeKey> active = std::nullopt,
    Transform3 world_from_local = {}, Transform3 sampling_from_local = {}) {
  return {.key = key, .document = &document, .edges = std::move(edges),
          .active_edge = active, .world_from_local = world_from_local,
          .sampling_from_local = sampling_from_local};
}

[[nodiscard]] const PreparedUvPeelDocument *prepared_uv_peel_document(
    const PreparedUvPeel &prepared, std::uint64_t key) {
  const auto found = std::ranges::find(
      prepared.documents, key, &PreparedUvPeelDocument::key);
  return found == prepared.documents.end() ? nullptr : &*found;
}

[[nodiscard]] const AuthoredTopologyFace *uv_peel_face(
    const AuthoredTopology &topology, FaceId id) {
  const auto found =
      std::ranges::find(topology.faces, id, &AuthoredTopologyFace::id);
  return found == topology.faces.end() ? nullptr : &*found;
}

[[nodiscard]] std::optional<Vec3>
uv_peel_position(const AuthoredTopology &topology, VertexId id) {
  const auto found =
      std::ranges::find(topology.vertices, id, &AuthoredTopologyVertex::id);
  return found == topology.vertices.end()
             ? std::nullopt
             : std::optional<Vec3>{found->position};
}

[[nodiscard]] std::optional<Vec2>
uv_peel_effective_corner(const AuthoredTopologyFace &face, VertexId id) {
  if (face.uvs.size() != face.vertices.size()) {
    return std::nullopt;
  }
  const auto found = std::ranges::find(face.vertices, id);
  if (found == face.vertices.end()) {
    return std::nullopt;
  }
  const std::size_t corner =
      static_cast<std::size_t>(found - face.vertices.begin());
  return expected_texture_uv(face.uvs[corner], face.texture_transform);
}

[[nodiscard]] std::vector<Vec2>
uv_peel_effective_vertex_samples(const AuthoredTopology &topology,
                                 VertexId id) {
  std::vector<Vec2> samples;
  for (const auto &face : topology.faces) {
    if (const auto uv = uv_peel_effective_corner(face, id)) {
      samples.push_back(*uv);
    }
  }
  return samples;
}

[[nodiscard]] Vec3 uv_peel_transform_point(const Transform3 &transform,
                                           Vec3 point) {
  return {
      transform.origin.x + transform.x_axis.x * point.x +
          transform.y_axis.x * point.y + transform.z_axis.x * point.z,
      transform.origin.y + transform.x_axis.y * point.x +
          transform.y_axis.y * point.y + transform.z_axis.y * point.z,
      transform.origin.z + transform.x_axis.z * point.x +
          transform.y_axis.z * point.y + transform.z_axis.z * point.z,
  };
}

[[nodiscard]] float uv_peel_distance(Vec3 first, Vec3 second) {
  const double x = static_cast<double>(second.x) - first.x;
  const double y = static_cast<double>(second.y) - first.y;
  const double z = static_cast<double>(second.z) - first.z;
  return static_cast<float>(std::sqrt(x * x + y * y + z * z));
}

void expect_uv_peel_guide(const AuthoredTopology &topology,
                          std::span<const VertexId> guide,
                          const Transform3 &world_from_local,
                          UvPeelAxis axis, float repeat = 1.0F,
                          float tolerance = 0.002F) {
  ASSERT_GE(guide.size(), 2U);
  std::vector<Vec2> samples;
  samples.reserve(guide.size());
  for (const VertexId id : guide) {
    const auto at_vertex = uv_peel_effective_vertex_samples(topology, id);
    ASSERT_FALSE(at_vertex.empty());
    for (const Vec2 uv : at_vertex) {
      expect_vec2_near(uv, at_vertex.front(), tolerance);
    }
    samples.push_back(at_vertex.front());
  }
  const auto along = [axis](Vec2 value) {
    return axis == UvPeelAxis::U ? value.x : value.y;
  };
  const auto across = [axis](Vec2 value) {
    return axis == UvPeelAxis::U ? value.y : value.x;
  };
  Transform3 linear_world = world_from_local;
  linear_world.origin = {};
  for (std::size_t index = 0U; index + 1U < guide.size(); ++index) {
    const auto first = uv_peel_position(topology, guide[index]);
    const auto second = uv_peel_position(topology, guide[index + 1U]);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    const float expected = 0.5F * repeat * uv_peel_distance(
        uv_peel_transform_point(linear_world, *first),
        uv_peel_transform_point(linear_world, *second));
    EXPECT_NEAR(along(samples[index + 1U]) - along(samples[index]),
                expected, tolerance);
    EXPECT_NEAR(across(samples[index + 1U]), across(samples[index]), tolerance);
  }
}

void expect_uv_peel_strip_station_metric(
    const AuthoredTopology &topology,
    const std::vector<std::vector<VertexId>> &rails, std::size_t reference,
    const Transform3 &world_from_local, bool preserve_cross_edge_widths = true) {
  ASSERT_LT(reference, rails.size());
  ASSERT_FALSE(rails[reference].empty());
  for (const auto &rail : rails) {
    ASSERT_EQ(rail.size(), rails[reference].size());
  }
  Transform3 linear_world = world_from_local;
  linear_world.origin = {};
  float reference_u = 0.0F;
  for (std::size_t station = 0U; station < rails[reference].size(); ++station) {
    SCOPED_TRACE(station);
    if (station != 0U) {
      const auto first = uv_peel_position(topology, rails[reference][station - 1U]);
      const auto second = uv_peel_position(topology, rails[reference][station]);
      ASSERT_TRUE(first.has_value());
      ASSERT_TRUE(second.has_value());
      reference_u += 0.5F * uv_peel_distance(
          uv_peel_transform_point(linear_world, *first),
          uv_peel_transform_point(linear_world, *second));
    }
    std::optional<Vec2> previous;
    for (std::size_t rail = 0U; rail < rails.size(); ++rail) {
      SCOPED_TRACE(rail);
      const auto samples =
          uv_peel_effective_vertex_samples(topology, rails[rail][station]);
      ASSERT_FALSE(samples.empty());
      for (const Vec2 uv : samples) {
        EXPECT_NEAR(uv.x, reference_u, 2.0e-5F);
        expect_vec2_near(uv, samples.front(), 2.0e-5F);
      }
      if (previous.has_value() && preserve_cross_edge_widths) {
        const auto first = uv_peel_position(topology, rails[rail - 1U][station]);
        const auto second = uv_peel_position(topology, rails[rail][station]);
        ASSERT_TRUE(first.has_value());
        ASSERT_TRUE(second.has_value());
        const float expected_width = 0.5F * uv_peel_distance(
            uv_peel_transform_point(linear_world, *first),
            uv_peel_transform_point(linear_world, *second));
        EXPECT_NEAR(std::abs(samples.front().y - previous->y), expected_width,
                    2.0e-5F);
      }
      previous = samples.front();
    }
  }
}

void expect_uv_peel_preserves_document(const PolygonDocument &source,
                                       const PreparedUvPeelDocument &prepared) {
  EXPECT_TRUE(prepared.document.has_same_geometry(source));
  EXPECT_EQ(prepared.document.vertex_ids(), source.vertex_ids());
  EXPECT_EQ(prepared.document.edge_ids(), source.edge_ids());
  EXPECT_EQ(prepared.document.face_ids(), source.face_ids());
  EXPECT_EQ(prepared.document.hard_edge_ids(), source.hard_edge_ids());
  EXPECT_EQ(prepared.document.selection_revision(), source.selection_revision());
  EXPECT_TRUE(prepared.document.shares_selection_with(source));
  const auto before = source.authored_topology();
  const auto after = prepared.document.authored_topology();
  ASSERT_TRUE(before.ok());
  ASSERT_TRUE(after.ok());
  ASSERT_EQ(before.value().faces.size(), after.value().faces.size());
  for (std::size_t index = 0U; index < before.value().faces.size(); ++index) {
    EXPECT_EQ(after.value().faces[index].id, before.value().faces[index].id);
    EXPECT_EQ(after.value().faces[index].vertices,
              before.value().faces[index].vertices);
    EXPECT_EQ(after.value().faces[index].material_slot,
              before.value().faces[index].material_slot);
    EXPECT_EQ(after.value().faces[index].shading,
              before.value().faces[index].shading);
    EXPECT_EQ(after.value().faces[index].texture_transform,
              before.value().faces[index].texture_transform);
  }
  EXPECT_TRUE(prepared.receipt.changed);
  EXPECT_TRUE(prepared.receipt.dirty.geometry);
  EXPECT_FALSE(prepared.receipt.dirty.topology);
  EXPECT_FALSE(prepared.receipt.dirty.materials);
  EXPECT_FALSE(prepared.receipt.dirty.selection);
}

void expect_uv_peel_relation(const PolygonDocument &source,
                             const PolygonDocument &transformed,
                             float uu, float uv, float vu, float vv,
                             Vec2 offset, float tolerance = 0.002F) {
  const auto before = source.authored_topology();
  const auto after = transformed.authored_topology();
  ASSERT_TRUE(before.ok());
  ASSERT_TRUE(after.ok());
  for (const auto &face : before.value().faces) {
    const auto *changed = uv_peel_face(after.value(), face.id);
    ASSERT_NE(changed, nullptr);
    if (face.uvs.empty()) {
      EXPECT_TRUE(changed->uvs.empty());
      continue;
    }
    ASSERT_EQ(face.vertices.size(), changed->vertices.size());
    for (const VertexId vertex : face.vertices) {
      const auto first = uv_peel_effective_corner(face, vertex);
      const auto second = uv_peel_effective_corner(*changed, vertex);
      ASSERT_TRUE(first.has_value());
      ASSERT_TRUE(second.has_value());
      expect_vec2_near(*second,
                       {uu * first->x + uv * first->y + offset.x,
                        vu * first->x + vv * first->y + offset.y},
                       tolerance);
    }
  }
}

[[nodiscard]] UvPeelStripFixture make_uv_peel_flat_strip(
    bool authored_uvs = false, FaceTextureTransform transform = {}) {
  std::vector<std::vector<Vec3>> rails;
  for (const float y : {2.0F, 1.0F, 0.0F}) {
    rails.push_back({{0.0F, y, 0.0F}, {2.0F, y, 0.0F}, {4.0F, y, 0.0F}});
  }
  return make_uv_peel_strip(rails, false, authored_uvs, transform);
}

[[nodiscard]] UvPeelStripFixture make_uv_peel_curved_strip(
    bool authored_uvs = false, FaceTextureTransform transform = {}) {
  constexpr float kQuarterArcStep = 0.3141592653589793F;
  std::vector<std::vector<Vec3>> rails;
  for (const float radius : {2.0F, 3.0F, 4.0F}) {
    std::vector<Vec3> points;
    for (int station = 0; station < 6; ++station) {
      const float angle = static_cast<float>(station) * kQuarterArcStep;
      points.push_back({radius * std::cos(angle), radius * std::sin(angle), 0.0F});
    }
    rails.push_back(std::move(points));
  }
  return make_uv_peel_strip(rails, false, authored_uvs, transform);
}

void expect_uv_peel_unfolded_faces(const AuthoredTopology &topology,
                                  double minimum_area = 0.00001) {
  std::optional<double> orientation;
  for (const auto &face : topology.faces) {
    if (face.uvs.empty()) {
      continue;
    }
    ASSERT_EQ(face.uvs.size(), face.vertices.size());
    double area = 0.0;
    for (std::size_t index = 0U; index < face.vertices.size(); ++index) {
      const Vec2 first = expected_texture_uv(face.uvs[index], face.texture_transform);
      const Vec2 second = expected_texture_uv(
          face.uvs[(index + 1U) % face.uvs.size()], face.texture_transform);
      ASSERT_TRUE(std::isfinite(first.x));
      ASSERT_TRUE(std::isfinite(first.y));
      area += static_cast<double>(first.x) * second.y -
              static_cast<double>(first.y) * second.x;
    }
    EXPECT_GT(std::abs(area), minimum_area);
    if (orientation.has_value()) {
      EXPECT_GT(area * *orientation, 0.0);
    } else {
      orientation = area;
    }
  }
  EXPECT_TRUE(orientation.has_value());
}

TEST(ModelingSdkConsumerTests,
     UvPeelStraightensCurvedRoadGuidesAtPhysicalDensityWithoutAProjectionShortcut) {
  const FaceTextureTransform transform{
      .scale = {-2.0F, 0.5F}, .shift_uv = {0.25F, -0.75F},
      .rotation_degrees = 30.0F,
  };
  for (const bool authored : {false, true}) {
    SCOPED_TRACE(authored);
    const auto fixture = make_uv_peel_curved_strip(authored, transform);
    auto document = PolygonDocument::from_authored_topology(fixture.topology);
    ASSERT_TRUE(document.ok()) << document.error_message();
    const auto edges = uv_peel_rail_edges(fixture.rails[1], false);
    ASSERT_TRUE(document.value().select_edges(edges).ok());
    const std::array targets{uv_peel_target(document.value(), 11U, edges, edges.front())};
    const auto original_identity = document.value().content_identity();
    const auto result = prepare_uv_peel(targets, {});
    ASSERT_TRUE(result.ok()) << result.error_message();
    ASSERT_TRUE(result.value().changed);
    const auto *prepared = prepared_uv_peel_document(result.value(), 11U);
    ASSERT_NE(prepared, nullptr);
    expect_uv_peel_preserves_document(document.value(), *prepared);
    const auto topology = prepared->document.authored_topology();
    ASSERT_TRUE(topology.ok());
    expect_uv_peel_guide(topology.value(), fixture.rails[1], {}, UvPeelAxis::U);
    expect_uv_peel_strip_station_metric(topology.value(), fixture.rails, 1U, {});
    for (const auto &rail : fixture.rails) {
      const auto start = uv_peel_effective_vertex_samples(topology.value(), rail.front());
      ASSERT_FALSE(start.empty());
      for (const VertexId vertex : rail) {
        const auto samples = uv_peel_effective_vertex_samples(topology.value(), vertex);
        ASSERT_FALSE(samples.empty());
        EXPECT_NEAR(samples.front().y, start.front().y, 2.0e-5F);
      }
    }
    expect_uv_peel_unfolded_faces(topology.value());
    const auto first = uv_peel_position(topology.value(), fixture.rails[1].front());
    const auto last = uv_peel_position(topology.value(), fixture.rails[1].back());
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(last.has_value());
    // A world-axis planar projection changes V along this curved guide;
    // peeling must instead keep every incident corner on a straight UV line.
    EXPECT_GT(std::abs(last->y - first->y) * 0.5F, 1.0F);
    EXPECT_EQ(document.value().content_identity(), original_identity);
  }
}

TEST(ModelingSdkConsumerTests,
     UvPeelSupportsParallelGuidesWithoutCuttingTheirSharedFaceCorners) {
  const auto fixture = make_uv_peel_curved_strip();
  auto document = PolygonDocument::from_authored_topology(fixture.topology);
  ASSERT_TRUE(document.ok());
  std::vector<EdgeKey> edges;
  for (const auto &rail : fixture.rails) {
    const auto rail_edges = uv_peel_rail_edges(rail, false);
    edges.insert(edges.end(), rail_edges.begin(), rail_edges.end());
  }
  const auto reference = make_edge_key(fixture.rails[1][0], fixture.rails[1][1]);
  const std::array targets{uv_peel_target(document.value(), 1U, edges, reference)};
  const auto result = prepare_uv_peel(targets, {});
  ASSERT_TRUE(result.ok()) << result.error_message();
  const auto *prepared = prepared_uv_peel_document(result.value(), 1U);
  ASSERT_NE(prepared, nullptr);
  expect_uv_peel_preserves_document(document.value(), *prepared);
  const auto topology = prepared->document.authored_topology();
  ASSERT_TRUE(topology.ok());
  expect_uv_peel_guide(topology.value(), fixture.rails[1], {}, UvPeelAxis::U);
  expect_uv_peel_strip_station_metric(topology.value(), fixture.rails, 1U, {});
  for (const auto &rail : fixture.rails) {
    std::optional<Vec2> previous;
    for (const VertexId vertex : rail) {
      const auto samples = uv_peel_effective_vertex_samples(topology.value(), vertex);
      ASSERT_FALSE(samples.empty());
      for (const Vec2 uv : samples) {
        expect_vec2_near(uv, samples.front(), 0.002F);
      }
      if (previous.has_value()) {
        EXPECT_NEAR(samples.front().y, previous->y, 0.002F);
        EXPECT_GT(samples.front().x, previous->x);
      }
      previous = samples.front();
    }
  }
  const auto inner = uv_peel_effective_vertex_samples(topology.value(), fixture.rails[0][0]);
  const auto outer = uv_peel_effective_vertex_samples(topology.value(), fixture.rails[2][0]);
  ASSERT_FALSE(inner.empty());
  ASSERT_FALSE(outer.empty());
  EXPECT_GT(std::abs(inner.front().y - outer.front().y), 0.25F);
  expect_uv_peel_unfolded_faces(topology.value());
}

TEST(ModelingSdkConsumerTests,
     UvPeelFollowsBentTwistedRoadArclengthUnderTheObjectWorldTransform) {
  constexpr std::array<Vec3, 5> kCenters{{
      {0.0F, 0.0F, 0.0F}, {2.0F, 0.0F, 0.0F}, {4.0F, 0.0F, 1.0F},
      {6.0F, 0.0F, 2.0F}, {8.0F, 0.0F, 2.0F},
  }};
  constexpr std::array<Vec3, 5> kWidths{{
      {0.0F, 1.0F, 0.0F}, {0.0F, 0.98F, 0.2F}, {0.0F, 0.866F, 0.5F},
      {0.0F, 0.7F, 0.7F}, {0.0F, 0.5F, 0.866F},
  }};
  std::vector<std::vector<Vec3>> rails;
  for (const float side : {-1.0F, 0.0F, 1.0F}) {
    std::vector<Vec3> points;
    for (std::size_t index = 0U; index < kCenters.size(); ++index) {
      points.push_back({kCenters[index].x + side * kWidths[index].x,
                        kCenters[index].y + side * kWidths[index].y,
                        kCenters[index].z + side * kWidths[index].z});
    }
    rails.push_back(std::move(points));
  }
  const auto fixture = make_uv_peel_strip(rails, false);
  auto document = PolygonDocument::from_authored_topology(fixture.topology);
  ASSERT_TRUE(document.ok()) << document.error_message();
  const auto edges = uv_peel_rail_edges(fixture.rails[1], false);
  const Transform3 world{
      .x_axis = {0.0F, 2.0F, 0.0F}, .y_axis = {-1.5F, 0.0F, 0.0F},
      .z_axis = {0.0F, 0.0F, 0.75F}, .origin = {11.0F, -7.0F, 3.0F},
  };
  const std::array targets{
      uv_peel_target(document.value(), 1U, edges, edges.front(), world)};
  const auto result = prepare_uv_peel(targets, {});
  ASSERT_TRUE(result.ok()) << result.error_message();
  const auto *prepared = prepared_uv_peel_document(result.value(), 1U);
  ASSERT_NE(prepared, nullptr);
  expect_uv_peel_preserves_document(document.value(), *prepared);
  const auto topology = prepared->document.authored_topology();
  ASSERT_TRUE(topology.ok());
  expect_uv_peel_guide(topology.value(), fixture.rails[1], world, UvPeelAxis::U);
  expect_uv_peel_strip_station_metric(topology.value(), fixture.rails, 1U, world);
  expect_uv_peel_unfolded_faces(topology.value());
}

TEST(ModelingSdkConsumerTests,
     UvPeelStructuredRoadMappingIsInvariantToCyclicAuthoredCornerOrder) {
  const auto fixture = make_uv_peel_curved_strip(
      true, {.scale = {-1.5F, 0.75F}, .shift_uv = {0.2F, -0.3F},
             .rotation_degrees = 37.0F});
  auto source = PolygonDocument::from_authored_topology(fixture.topology);
  ASSERT_TRUE(source.ok());
  const auto edges = uv_peel_rail_edges(fixture.rails[1], false);
  const Transform3 world{
      .x_axis = {0.0F, 1.5F, 0.25F}, .y_axis = {-2.0F, 0.0F, 0.5F},
      .z_axis = {0.0F, 0.0F, 1.0F}, .origin = {17.0F, -31.0F, 11.0F}};
  const std::array source_targets{
      uv_peel_target(source.value(), 1U, edges, edges.front(), world)};
  const auto baseline = prepare_uv_peel(source_targets, {});
  ASSERT_TRUE(baseline.ok()) << baseline.error_message();
  const auto *baseline_document = prepared_uv_peel_document(baseline.value(), 1U);
  ASSERT_NE(baseline_document, nullptr);
  const auto baseline_topology = baseline_document->document.authored_topology();
  ASSERT_TRUE(baseline_topology.ok());
  expect_uv_peel_strip_station_metric(baseline_topology.value(), fixture.rails,
                                      1U, world);
  for (std::uint32_t rotation = 1U; rotation < 4U; ++rotation) {
    SCOPED_TRACE(rotation);
    auto rotated_topology = fixture.topology;
    for (auto &face : rotated_topology.faces) {
      const auto offset = static_cast<std::ptrdiff_t>(
          (face.id.index + rotation) % face.vertices.size());
      std::rotate(face.vertices.begin(), face.vertices.begin() + offset,
                   face.vertices.end());
      std::rotate(face.uvs.begin(), face.uvs.begin() + offset, face.uvs.end());
    }
    auto rotated = PolygonDocument::from_authored_topology(rotated_topology);
    ASSERT_TRUE(rotated.ok()) << rotated.error_message();
    const std::array targets{
        uv_peel_target(rotated.value(), 1U, edges, edges.front(), world)};
    const auto result = prepare_uv_peel(targets, {});
    ASSERT_TRUE(result.ok()) << result.error_message();
    const auto *prepared = prepared_uv_peel_document(result.value(), 1U);
    ASSERT_NE(prepared, nullptr);
    expect_uv_peel_preserves_document(rotated.value(), *prepared);
    expect_uv_peel_relation(baseline_document->document, prepared->document,
                             1.0F, 0.0F, 0.0F, 1.0F, {}, 2.0e-5F);
  }
}

TEST(ModelingSdkConsumerTests,
     UvPeelVariableRoadWidthsRemainPhysicalUnlessExplicitGuidesConstrainThem) {
  constexpr std::array<float, 6> kInnerWidths{{1.0F, 0.75F, 0.5F, 1.0F, 1.25F, 0.875F}};
  constexpr std::array<float, 6> kOuterWidths{{0.5F, 0.75F, 1.25F, 1.0F, 0.75F, 1.5F}};
  auto fixture = make_uv_peel_curved_strip();
  for (std::size_t station = 0U; station < kInnerWidths.size(); ++station) {
    auto &inner = fixture.topology.vertices[station].position;
    auto &outer = fixture.topology.vertices[2U * kInnerWidths.size() + station].position;
    const float inner_scale = (3.0F - kInnerWidths[station]) / 2.0F;
    const float outer_scale = (3.0F + kOuterWidths[station]) / 4.0F;
    inner.x *= inner_scale;
    inner.y *= inner_scale;
    outer.x *= outer_scale;
    outer.y *= outer_scale;
  }
  auto document = PolygonDocument::from_authored_topology(fixture.topology);
  ASSERT_TRUE(document.ok()) << document.error_message();
  const auto center_edges = uv_peel_rail_edges(fixture.rails[1], false);
  for (const bool explicit_guides : {false, true}) {
    SCOPED_TRACE(explicit_guides);
    auto edges = center_edges;
    if (explicit_guides) {
      for (const std::size_t rail : {0U, 2U}) {
        const auto additions = uv_peel_rail_edges(fixture.rails[rail], false);
        edges.insert(edges.end(), additions.begin(), additions.end());
      }
    }
    const std::array targets{
        uv_peel_target(document.value(), 1U, edges, center_edges.front())};
    const auto result = prepare_uv_peel(targets, {});
    ASSERT_TRUE(result.ok()) << result.error_message();
    const auto *prepared = prepared_uv_peel_document(result.value(), 1U);
    ASSERT_NE(prepared, nullptr);
    expect_uv_peel_preserves_document(document.value(), *prepared);
    const auto topology = prepared->document.authored_topology();
    ASSERT_TRUE(topology.ok());
    expect_uv_peel_strip_station_metric(topology.value(), fixture.rails, 1U, {},
                                        !explicit_guides);
    expect_uv_peel_unfolded_faces(topology.value());
    if (explicit_guides) {
      for (const auto &rail : fixture.rails) {
        const auto start = uv_peel_effective_vertex_samples(topology.value(), rail.front());
        ASSERT_FALSE(start.empty());
        for (const VertexId vertex : rail) {
          const auto samples = uv_peel_effective_vertex_samples(topology.value(), vertex);
          ASSERT_FALSE(samples.empty());
          EXPECT_NEAR(samples.front().y, start.front().y, 2.0e-5F);
        }
      }
    }
  }
}

TEST(ModelingSdkConsumerTests,
     UvPeelRetainsConformalFallbackForTriangleAndMixedFacePatches) {
  constexpr std::array<Vec3, 4> kQuad{{
      {0.0F, 0.0F, 0.0F}, {2.0F, 0.0F, 0.0F},
      {2.0F, 1.0F, 0.0F}, {0.0F, 1.0F, 0.0F}}};
  const auto quad = make_texture_action_face(kQuad);
  ASSERT_TRUE(quad.ok());
  const auto authored = quad.value().authored_topology();
  ASSERT_TRUE(authored.ok());
  for (const bool triangulated : {false, true}) {
    SCOPED_TRACE(triangulated);
    auto topology = authored.value();
    topology.vertices.push_back({make_id<VertexTag>(5U), {3.0F, 0.5F, 0.0F}});
    topology.faces.push_back({
        .id = make_id<FaceTag>(2U),
        .vertices = {make_id<VertexTag>(3U), make_id<VertexTag>(2U),
                     make_id<VertexTag>(5U)}});
    topology.next_vertex_id = 6U;
    topology.next_face_id = 3U;
    if (triangulated) {
      topology.faces.front().vertices = {
          make_id<VertexTag>(1U), make_id<VertexTag>(2U), make_id<VertexTag>(3U)};
      topology.faces.push_back({
          .id = make_id<FaceTag>(3U),
          .vertices = {make_id<VertexTag>(1U), make_id<VertexTag>(3U),
                       make_id<VertexTag>(4U)}});
      topology.next_face_id = 4U;
    }
    auto document = PolygonDocument::from_authored_topology(topology);
    ASSERT_TRUE(document.ok()) << document.error_message();
    const std::array guide{make_id<VertexTag>(2U), make_id<VertexTag>(3U)};
    const EdgeKey edge = make_edge_key(guide[0], guide[1]);
    const std::array targets{uv_peel_target(document.value(), 1U, {edge}, edge)};
    const auto result = prepare_uv_peel(targets, {});
    ASSERT_TRUE(result.ok()) << result.error_message();
    const auto *prepared = prepared_uv_peel_document(result.value(), 1U);
    ASSERT_NE(prepared, nullptr);
    expect_uv_peel_preserves_document(document.value(), *prepared);
    const auto after = prepared->document.authored_topology();
    ASSERT_TRUE(after.ok());
    expect_uv_peel_guide(after.value(), guide, {}, UvPeelAxis::U);
    expect_uv_peel_unfolded_faces(after.value());
    if (triangulated) {
      const auto *outside = uv_peel_face(after.value(), make_id<FaceTag>(3U));
      ASSERT_NE(outside, nullptr);
      EXPECT_TRUE(outside->uvs.empty());
    }
  }
}

TEST(ModelingSdkConsumerTests,
     UvPeelAxisRepeatsOffsetsAndNormalizedModeComposeAfterTheGuidedChart) {
  const auto fixture = make_uv_peel_flat_strip(
      true, {.scale = {-2.0F, 0.5F}, .shift_uv = {0.25F, -0.75F},
             .rotation_degrees = 30.0F});
  auto document = PolygonDocument::from_authored_topology(fixture.topology);
  ASSERT_TRUE(document.ok());
  const auto edges = uv_peel_rail_edges(fixture.rails[1], false);
  const std::array targets{uv_peel_target(document.value(), 1U, edges, edges.front())};
  const auto baseline = prepare_uv_peel(targets, {});
  ASSERT_TRUE(baseline.ok()) << baseline.error_message();
  const auto *baseline_document = prepared_uv_peel_document(baseline.value(), 1U);
  ASSERT_NE(baseline_document, nullptr);
  const auto vertical = prepare_uv_peel(targets, {.axis = UvPeelAxis::V});
  ASSERT_TRUE(vertical.ok()) << vertical.error_message();
  const auto *vertical_document = prepared_uv_peel_document(vertical.value(), 1U);
  ASSERT_NE(vertical_document, nullptr);
  expect_uv_peel_relation(baseline_document->document, vertical_document->document,
                           0.0F, -1.0F, 1.0F, 0.0F, {});
  const auto repeated = prepare_uv_peel(
      targets, {.u_repeat = 2.0, .v_repeat = 3.0,
                .u_offset = 0.25, .v_offset = -0.4});
  ASSERT_TRUE(repeated.ok()) << repeated.error_message();
  const auto *repeated_document = prepared_uv_peel_document(repeated.value(), 1U);
  ASSERT_NE(repeated_document, nullptr);
  expect_uv_peel_relation(baseline_document->document, repeated_document->document,
                           2.0F, 0.0F, 0.0F, 3.0F, {0.25F, -0.4F});
  for (const UvPeelAxis axis : {UvPeelAxis::U, UvPeelAxis::V}) {
    const auto normalized = prepare_uv_peel(
        targets, {.axis = axis, .world_space = false,
                  .u_repeat = 2.0, .v_repeat = 3.0,
                  .u_offset = 0.25, .v_offset = -0.4});
    ASSERT_TRUE(normalized.ok()) << normalized.error_message();
    const auto *normalized_document = prepared_uv_peel_document(normalized.value(), 1U);
    ASSERT_NE(normalized_document, nullptr);
    expect_uv_peel_preserves_document(document.value(), *normalized_document);
    const auto topology = normalized_document->document.authored_topology();
    ASSERT_TRUE(topology.ok());
    Vec2 minimum{std::numeric_limits<float>::infinity(),
                  std::numeric_limits<float>::infinity()};
    Vec2 maximum{-std::numeric_limits<float>::infinity(),
                  -std::numeric_limits<float>::infinity()};
    for (const auto &face : topology.value().faces) {
      for (const VertexId vertex : face.vertices) {
        const auto uv = uv_peel_effective_corner(face, vertex);
        ASSERT_TRUE(uv.has_value());
        minimum.x = std::min(minimum.x, uv->x);
        minimum.y = std::min(minimum.y, uv->y);
        maximum.x = std::max(maximum.x, uv->x);
        maximum.y = std::max(maximum.y, uv->y);
      }
    }
    expect_vec2_near(minimum, {0.25F, -0.4F}, 0.002F);
    expect_vec2_near(maximum, {2.25F, 2.6F}, 0.002F);
  }

  constexpr double kTinyRepeat = 1.0e-8;
  auto collapsed_fixture = make_uv_peel_flat_strip(true);
  for (auto &face : collapsed_fixture.topology.faces) {
    face.uvs.assign(face.vertices.size(), Vec2{});
  }
  auto collapsed =
      PolygonDocument::from_authored_topology(collapsed_fixture.topology);
  ASSERT_TRUE(collapsed.ok());
  const UvPeelOptions tiny_options{
      .world_space = false, .u_repeat = kTinyRepeat, .v_repeat = kTinyRepeat};
  const std::array tiny_targets{
      uv_peel_target(collapsed.value(), 1U, edges, edges.front())};
  const auto tiny = prepare_uv_peel(tiny_targets, tiny_options);
  ASSERT_TRUE(tiny.ok()) << tiny.error_message();
  ASSERT_TRUE(tiny.value().changed);
  const auto *tiny_document = prepared_uv_peel_document(tiny.value(), 1U);
  ASSERT_NE(tiny_document, nullptr);
  expect_uv_peel_preserves_document(collapsed.value(), *tiny_document);
  const auto tiny_topology = tiny_document->document.authored_topology();
  ASSERT_TRUE(tiny_topology.ok());
  expect_uv_peel_unfolded_faces(tiny_topology.value(), 1.0e-18);
  Vec2 tiny_minimum{std::numeric_limits<float>::infinity(),
                    std::numeric_limits<float>::infinity()};
  Vec2 tiny_maximum{-std::numeric_limits<float>::infinity(),
                    -std::numeric_limits<float>::infinity()};
  for (const auto &face : tiny_topology.value().faces) {
    for (const Vec2 uv : face.uvs) {
      tiny_minimum.x = std::min(tiny_minimum.x, uv.x);
      tiny_minimum.y = std::min(tiny_minimum.y, uv.y);
      tiny_maximum.x = std::max(tiny_maximum.x, uv.x);
      tiny_maximum.y = std::max(tiny_maximum.y, uv.y);
    }
  }
  expect_vec2_near(tiny_minimum, {}, 1.0e-12F);
  expect_vec2_near(tiny_maximum,
                    {static_cast<float>(kTinyRepeat),
                     static_cast<float>(kTinyRepeat)},
                    1.0e-12F);
  const std::array tiny_again_targets{
      uv_peel_target(tiny_document->document, 1U, edges, edges.front())};
  const auto tiny_again = prepare_uv_peel(tiny_again_targets, tiny_options);
  ASSERT_TRUE(tiny_again.ok()) << tiny_again.error_message();
  EXPECT_FALSE(tiny_again.value().changed);
  const auto *unchanged = prepared_uv_peel_document(tiny_again.value(), 1U);
  ASSERT_NE(unchanged, nullptr);
  EXPECT_FALSE(unchanged->receipt.changed);
  EXPECT_EQ(unchanged->document.content_identity(),
            tiny_document->document.content_identity());
  EXPECT_EQ(unchanged->document.content_revision(),
            tiny_document->document.content_revision());
}

[[nodiscard]] AuthoredTopology close_uv_peel_strip(
    const AuthoredTopology &surface, float depth) {
  AuthoredTopology solid = surface;
  const std::uint32_t vertex_offset = surface.next_vertex_id - 1U;
  std::vector<EdgeKey> edges;
  for (const auto &face : surface.faces) {
    for (std::size_t index = 0U; index < face.vertices.size(); ++index) {
      edges.push_back(make_edge_key(face.vertices[index],
                                    face.vertices[(index + 1U) % face.vertices.size()]));
    }
  }
  for (const auto &vertex : surface.vertices) {
    solid.vertices.push_back({make_id<VertexTag>(vertex.id.index + vertex_offset),
                               {vertex.position.x, vertex.position.y,
                                vertex.position.z - depth}});
  }
  for (const auto &face : surface.faces) {
    AuthoredTopologyFace bottom = face;
    bottom.id = make_id<FaceTag>(solid.next_face_id++);
    bottom.vertices.clear();
    bottom.uvs.clear();
    for (auto vertex = face.vertices.rbegin(); vertex != face.vertices.rend(); ++vertex) {
      bottom.vertices.push_back(make_id<VertexTag>(vertex->index + vertex_offset));
    }
    solid.faces.push_back(std::move(bottom));
  }
  for (const auto &face : surface.faces) {
    for (std::size_t index = 0U; index < face.vertices.size(); ++index) {
      const VertexId first = face.vertices[index];
      const VertexId second = face.vertices[(index + 1U) % face.vertices.size()];
      if (std::ranges::count(edges, make_edge_key(first, second)) != 1) {
        continue;
      }
      solid.faces.push_back({
          .id = make_id<FaceTag>(solid.next_face_id++),
          .vertices = {second, first,
                       make_id<VertexTag>(first.index + vertex_offset),
                       make_id<VertexTag>(second.index + vertex_offset)},
          .material_slot = 99U,
      });
    }
  }
  solid.next_vertex_id += vertex_offset;
  return solid;
}

TEST(ModelingSdkConsumerTests,
     UvPeelTouchesOnlyIncidentRoadFacesOnAClosedSolid) {
  const auto fixture = make_uv_peel_curved_strip();
  const auto solid = close_uv_peel_strip(fixture.topology, 0.5F);
  auto document = PolygonDocument::from_authored_topology(solid);
  ASSERT_TRUE(document.ok()) << document.error_message();
  const auto original_payload = document.value().authored_payload();
  ASSERT_TRUE(original_payload.ok());
  EXPECT_EQ(authored_open_edge_count(original_payload.value()), 0U);
  const auto edges = uv_peel_rail_edges(fixture.rails[1], false);
  ASSERT_TRUE(document.value().select_edges(edges).ok());
  const std::array targets{uv_peel_target(document.value(), 1U, edges, edges.front())};
  const auto result = prepare_uv_peel(targets, {});
  ASSERT_TRUE(result.ok()) << result.error_message();
  const auto *prepared = prepared_uv_peel_document(result.value(), 1U);
  ASSERT_NE(prepared, nullptr);
  expect_uv_peel_preserves_document(document.value(), *prepared);
  const auto after = prepared->document.authored_topology();
  ASSERT_TRUE(after.ok());
  for (const auto &face : solid.faces) {
    const auto *changed = uv_peel_face(after.value(), face.id);
    ASSERT_NE(changed, nullptr);
    if (uv_peel_face(fixture.topology, face.id) != nullptr) {
      EXPECT_EQ(changed->uvs.size(), changed->vertices.size());
    } else {
      EXPECT_TRUE(changed->uvs.empty());
      EXPECT_EQ(changed->vertices, face.vertices);
      EXPECT_EQ(changed->material_slot, face.material_slot);
      EXPECT_EQ(changed->texture_transform, face.texture_transform);
      EXPECT_EQ(std::ranges::find(prepared->receipt.affected.faces, face.id),
                prepared->receipt.affected.faces.end());
    }
  }
  expect_uv_peel_guide(after.value(), fixture.rails[1], {}, UvPeelAxis::U);
}

[[nodiscard]] UvPeelStripFixture make_uv_peel_tube() {
  constexpr int kSides = 8;
  constexpr double kTau = 6.2831853071795864769;
  std::vector<std::vector<Vec3>> rails;
  for (const float y : {-2.0F, 2.0F}) {
    std::vector<Vec3> ring;
    for (int side = 0; side < kSides; ++side) {
      const double angle = kTau * side / kSides;
      ring.push_back({static_cast<float>(2.0 * std::cos(angle)), y,
                      static_cast<float>(2.0 * std::sin(angle))});
    }
    rails.push_back(std::move(ring));
  }
  return make_uv_peel_strip(rails, true);
}

TEST(ModelingSdkConsumerTests,
     UvPeelClosedTubeGuideCutsOnlyUvCornersAtOneTransverseSeam) {
  const auto fixture = make_uv_peel_tube();
  auto document = PolygonDocument::from_authored_topology(fixture.topology);
  ASSERT_TRUE(document.ok()) << document.error_message();
  const auto edges = uv_peel_rail_edges(fixture.rails[0], true);
  const std::array targets{uv_peel_target(document.value(), 1U, edges, edges.front())};
  const auto result = prepare_uv_peel(targets, {});
  ASSERT_TRUE(result.ok()) << result.error_message();
  const auto *prepared = prepared_uv_peel_document(result.value(), 1U);
  ASSERT_NE(prepared, nullptr);
  expect_uv_peel_preserves_document(document.value(), *prepared);
  const auto topology = prepared->document.authored_topology();
  ASSERT_TRUE(topology.ok());
  float circumference = 0.0F;
  std::optional<float> across;
  for (const EdgeKey edge : edges) {
    const auto first_position = uv_peel_position(topology.value(), edge.a);
    const auto second_position = uv_peel_position(topology.value(), edge.b);
    ASSERT_TRUE(first_position.has_value());
    ASSERT_TRUE(second_position.has_value());
    const float segment = uv_peel_distance(*first_position, *second_position);
    circumference += segment;
    std::size_t incident_faces = 0U;
    for (const auto &face : topology.value().faces) {
      const auto first = uv_peel_effective_corner(face, edge.a);
      const auto second = uv_peel_effective_corner(face, edge.b);
      if (!first.has_value() || !second.has_value()) {
        continue;
      }
      ++incident_faces;
      EXPECT_NEAR(std::abs(second->x - first->x), segment * 0.5F, 0.002F);
      EXPECT_NEAR(first->y, second->y, 0.002F);
      if (across.has_value()) {
        EXPECT_NEAR(first->y, *across, 0.002F);
      } else {
        across = first->y;
      }
    }
    EXPECT_EQ(incident_faces, 1U);
  }
  std::size_t seam_vertices = 0U;
  for (const VertexId vertex : fixture.rails[0]) {
    const auto samples = uv_peel_effective_vertex_samples(topology.value(), vertex);
    ASSERT_EQ(samples.size(), 2U);
    EXPECT_NEAR(samples[0].y, samples[1].y, 0.002F);
    if (std::abs(samples[0].x - samples[1].x) > 0.002F) {
      ++seam_vertices;
      EXPECT_EQ(vertex, fixture.rails[0].front());
      EXPECT_NEAR(std::abs(samples[0].x - samples[1].x), circumference * 0.5F, 0.002F);
    }
  }
  EXPECT_EQ(seam_vertices, 1U);
  expect_uv_peel_unfolded_faces(topology.value());
  const std::array again_targets{
      uv_peel_target(prepared->document, 1U, edges, edges.front())};
  const auto again = prepare_uv_peel(again_targets, {});
  ASSERT_TRUE(again.ok()) << again.error_message();
  EXPECT_FALSE(again.value().changed);
  const auto *unchanged = prepared_uv_peel_document(again.value(), 1U);
  ASSERT_NE(unchanged, nullptr);
  EXPECT_FALSE(unchanged->receipt.changed);
  EXPECT_EQ(unchanged->document.content_identity(),
            prepared->document.content_identity());
  EXPECT_EQ(unchanged->document.content_revision(),
            prepared->document.content_revision());
}

TEST(ModelingSdkConsumerTests,
     UvPeelOpenTubeGuideKeepsBothIncidentSidesContinuousAndOtherFacesUntouched) {
  const auto fixture = make_uv_peel_tube();
  auto document = PolygonDocument::from_authored_topology(fixture.topology);
  ASSERT_TRUE(document.ok());
  const std::array guide{fixture.rails[0].front(), fixture.rails[1].front()};
  const EdgeKey edge = make_edge_key(guide[0], guide[1]);
  const std::array targets{uv_peel_target(document.value(), 1U, {edge}, edge)};
  const auto result = prepare_uv_peel(targets, {});
  ASSERT_TRUE(result.ok()) << result.error_message();
  const auto *prepared = prepared_uv_peel_document(result.value(), 1U);
  ASSERT_NE(prepared, nullptr);
  expect_uv_peel_preserves_document(document.value(), *prepared);
  const auto topology = prepared->document.authored_topology();
  ASSERT_TRUE(topology.ok());
  std::size_t changed_faces = 0U;
  for (const auto &face : topology.value().faces) {
    const bool first = std::ranges::find(face.vertices, guide[0]) != face.vertices.end();
    const bool second = std::ranges::find(face.vertices, guide[1]) != face.vertices.end();
    if (first && second) {
      ++changed_faces;
      EXPECT_EQ(face.uvs.size(), face.vertices.size());
    } else {
      EXPECT_TRUE(face.uvs.empty());
    }
  }
  EXPECT_EQ(changed_faces, 2U);
  expect_uv_peel_guide(topology.value(), guide, {}, UvPeelAxis::U);
}

TEST(ModelingSdkConsumerTests,
     UvPeelPreservesOccurrenceCompensationAndBecomesAnExactNoOpOnRepeat) {
  const auto fixture = make_uv_peel_flat_strip(
      true, {.scale = {-2.0F, 0.5F}, .shift_uv = {0.25F, -0.75F},
             .rotation_degrees = 30.0F});
  auto document = PolygonDocument::from_authored_topology(fixture.topology);
  ASSERT_TRUE(document.ok());
  const auto edges = uv_peel_rail_edges(fixture.rails[1], false);
  const Transform3 world{
      .x_axis = {0.0F, 0.0F, 2.0F}, .y_axis = {0.0F, 1.5F, 0.0F},
      .z_axis = {-1.0F, 0.0F, 0.0F}, .origin = {7.0F, 3.0F, -5.0F},
  };
  const std::array baseline_targets{
      uv_peel_target(document.value(), 1U, edges, edges.front(), world)};
  const auto baseline = prepare_uv_peel(baseline_targets, {});
  ASSERT_TRUE(baseline.ok()) << baseline.error_message();
  const auto *baseline_document = prepared_uv_peel_document(baseline.value(), 1U);
  ASSERT_NE(baseline_document, nullptr);
  const auto desired = baseline_document->document.authored_topology();
  ASSERT_TRUE(desired.ok());
  for (const bool mirrored : {false, true}) {
    SCOPED_TRACE(mirrored);
    const Transform3 sampling{
        .x_axis = {mirrored ? -2.0F : 2.0F, 0.0F, 0.0F},
        .y_axis = {0.0F, 0.5F, 0.0F}, .z_axis = {0.0F, 0.0F, 1.0F},
        .origin = {3.0F, -2.0F, 0.0F},
    };
    const std::array targets{
        uv_peel_target(document.value(), 1U, edges, edges.front(), world, sampling)};
    const auto result = prepare_uv_peel(targets, {});
    ASSERT_TRUE(result.ok()) << result.error_message();
    const auto *prepared = prepared_uv_peel_document(result.value(), 1U);
    ASSERT_NE(prepared, nullptr);
    expect_uv_peel_preserves_document(document.value(), *prepared);
    const auto compiled = prepared->document.compile_mesh();
    ASSERT_TRUE(compiled.ok());
    for (const auto &vertex : compiled.value().vertices) {
      const auto samples = uv_peel_effective_vertex_samples(desired.value(), vertex.source_vertex);
      ASSERT_FALSE(samples.empty());
      for (const Vec2 sample : samples) {
        expect_vec2_near(sample, samples.front(), 0.002F);
      }
      // The geometric chart is (+x/2,-y/2). A mirrored C flips its normal
      // to -Z, changing the sign of the translated U correction only.
      const Vec2 delta{vertex.position.x * 0.5F + (mirrored ? -1.5F : 1.5F),
                        vertex.position.y * 0.25F + 1.0F};
      const Vec2 effective{
          vertex.uv0.x + vertex.uv_carrier[0] * delta.x + vertex.uv_carrier[1] * delta.y,
          vertex.uv0.y + vertex.uv_carrier[2] * delta.x + vertex.uv_carrier[3] * delta.y};
      expect_vec2_near(effective, samples.front(), 0.002F);
    }
    const std::array again_targets{
        uv_peel_target(prepared->document, 1U, edges, edges.front(), world, sampling)};
    const auto again = prepare_uv_peel(again_targets, {});
    ASSERT_TRUE(again.ok()) << again.error_message();
    EXPECT_FALSE(again.value().changed);
    const auto *unchanged = prepared_uv_peel_document(again.value(), 1U);
    ASSERT_NE(unchanged, nullptr);
    EXPECT_FALSE(unchanged->receipt.changed);
    EXPECT_EQ(unchanged->document.content_identity(), prepared->document.content_identity());
    EXPECT_EQ(unchanged->document.content_revision(), prepared->document.content_revision());
    EXPECT_TRUE(unchanged->document.shares_selection_with(prepared->document));
  }
}

TEST(ModelingSdkConsumerTests,
     UvPeelThinChartsPreservePerAxisDensityEditsAndRepeatExactly) {
  for (const UvPeelAxis axis : {UvPeelAxis::U, UvPeelAxis::V}) {
    SCOPED_TRACE(axis == UvPeelAxis::U ? "U" : "V");
    std::vector<std::vector<Vec3>> rails;
    for (const float y : {0.001F, 0.0F, -0.001F}) {
      rails.push_back({{0.0F, y, 0.0F}, {2048.0F, y, 0.0F},
                       {4096.0F, y, 0.0F}});
    }
    auto fixture = make_uv_peel_strip(rails, false, true);
    for (auto &face : fixture.topology.faces) {
      for (std::size_t corner = 0U; corner < face.vertices.size(); ++corner) {
        const auto position = uv_peel_position(fixture.topology, face.vertices[corner]);
        ASSERT_TRUE(position.has_value());
        face.uvs[corner] = axis == UvPeelAxis::U
                               ? Vec2{0.5F * position->x, 0.25F * position->y}
                               : Vec2{-0.25F * position->y, 0.5F * position->x};
      }
    }
    auto document = PolygonDocument::from_authored_topology(fixture.topology);
    ASSERT_TRUE(document.ok()) << document.error_message();
    const auto edges = uv_peel_rail_edges(fixture.rails[1], false);
    ASSERT_TRUE(document.value().select_edges(edges).ok());
    const UvPeelOptions options{.axis = axis};
    const std::array targets{
        uv_peel_target(document.value(), 1U, edges, edges.front())};
    const auto result = prepare_uv_peel(targets, options);
    ASSERT_TRUE(result.ok()) << result.error_message();
    ASSERT_TRUE(result.value().changed);
    const auto *prepared = prepared_uv_peel_document(result.value(), 1U);
    ASSERT_NE(prepared, nullptr);
    expect_uv_peel_preserves_document(document.value(), *prepared);
    const auto topology = prepared->document.authored_topology();
    ASSERT_TRUE(topology.ok());
    for (const auto &face : topology.value().faces) {
      for (const VertexId vertex : face.vertices) {
        const auto position = uv_peel_position(topology.value(), vertex);
        const auto uv = uv_peel_effective_corner(face, vertex);
        ASSERT_TRUE(position.has_value());
        ASSERT_TRUE(uv.has_value());
        const float along = axis == UvPeelAxis::U ? uv->x : uv->y;
        const float across = axis == UvPeelAxis::U ? uv->y : -uv->x;
        EXPECT_NEAR(along, 0.5F * position->x, 1.0e-4F);
        EXPECT_NEAR(across, 0.5F * position->y, 1.0e-9F);
      }
    }
    const std::array again_targets{
        uv_peel_target(prepared->document, 1U, edges, edges.front())};
    const auto again = prepare_uv_peel(again_targets, options);
    ASSERT_TRUE(again.ok()) << again.error_message();
    EXPECT_FALSE(again.value().changed);
    const auto *unchanged = prepared_uv_peel_document(again.value(), 1U);
    ASSERT_NE(unchanged, nullptr);
    EXPECT_FALSE(unchanged->receipt.changed);
    EXPECT_EQ(unchanged->document.content_identity(),
              prepared->document.content_identity());
    EXPECT_EQ(unchanged->document.content_revision(),
              prepared->document.content_revision());
    EXPECT_TRUE(unchanged->document.shares_selection_with(prepared->document));
  }
}

TEST(ModelingSdkConsumerTests,
     UvPeelReferenceSelectionAndLargeWorldTranslationDoNotDependOnInputOrder) {
  const auto fixture = make_uv_peel_curved_strip();
  auto document = PolygonDocument::from_authored_topology(fixture.topology);
  ASSERT_TRUE(document.ok());
  std::vector<EdgeKey> edges;
  for (auto rail = fixture.rails.rbegin(); rail != fixture.rails.rend(); ++rail) {
    const auto additions = uv_peel_rail_edges(*rail, false);
    edges.insert(edges.end(), additions.begin(), additions.end());
  }
  const EdgeKey inner = make_edge_key(fixture.rails[0][0], fixture.rails[0][1]);
  const EdgeKey outer = make_edge_key(fixture.rails[2][0], fixture.rails[2][1]);
  const std::array fallback_targets{uv_peel_target(document.value(), 1U, edges)};
  const auto fallback = prepare_uv_peel(fallback_targets, {});
  ASSERT_TRUE(fallback.ok()) << fallback.error_message();
  const auto *fallback_document = prepared_uv_peel_document(fallback.value(), 1U);
  ASSERT_NE(fallback_document, nullptr);
  const auto fallback_topology = fallback_document->document.authored_topology();
  ASSERT_TRUE(fallback_topology.ok());
  expect_uv_peel_guide(fallback_topology.value(), fixture.rails[0], {}, UvPeelAxis::U);
  std::ranges::reverse(edges);
  const std::array explicit_targets{uv_peel_target(document.value(), 1U, edges, inner)};
  const auto explicit_reference = prepare_uv_peel(explicit_targets, {});
  ASSERT_TRUE(explicit_reference.ok()) << explicit_reference.error_message();
  const auto *explicit_document = prepared_uv_peel_document(explicit_reference.value(), 1U);
  ASSERT_NE(explicit_document, nullptr);
  const auto fallback_bytes = serialize_qdr_authored_topology(fallback_document->document);
  const auto explicit_bytes = serialize_qdr_authored_topology(explicit_document->document);
  ASSERT_TRUE(fallback_bytes.ok());
  ASSERT_TRUE(explicit_bytes.ok());
  EXPECT_EQ(explicit_bytes.value(), fallback_bytes.value());

  const std::array outer_targets{uv_peel_target(document.value(), 1U, edges, outer)};
  const auto outer_reference = prepare_uv_peel(outer_targets, {});
  ASSERT_TRUE(outer_reference.ok()) << outer_reference.error_message();
  const auto *outer_document = prepared_uv_peel_document(outer_reference.value(), 1U);
  ASSERT_NE(outer_document, nullptr);
  const auto outer_topology = outer_document->document.authored_topology();
  ASSERT_TRUE(outer_topology.ok());
  expect_uv_peel_guide(outer_topology.value(), fixture.rails[2], {}, UvPeelAxis::U);

  const Transform3 translated{.origin = {1000000.0F, -1000000.0F, 300000.0F}};
  const std::array translated_targets{
      uv_peel_target(document.value(), 1U, edges, outer, translated)};
  const auto translated_result = prepare_uv_peel(translated_targets, {});
  ASSERT_TRUE(translated_result.ok()) << translated_result.error_message();
  const auto *translated_document = prepared_uv_peel_document(translated_result.value(), 1U);
  ASSERT_NE(translated_document, nullptr);
  expect_uv_peel_relation(outer_document->document, translated_document->document,
                           1.0F, 0.0F, 0.0F, 1.0F, {}, 0.002F);
}

TEST(ModelingSdkConsumerTests,
     UvPeelNormalizesDisconnectedIncidentComponentsIndependently) {
  UvPeelStripFixture fixture = make_uv_peel_flat_strip();
  const UvPeelStripFixture second = make_uv_peel_flat_strip();
  const std::uint32_t vertex_offset = fixture.topology.next_vertex_id - 1U;
  const std::uint32_t face_offset = fixture.topology.next_face_id - 1U;
  for (const auto &vertex : second.topology.vertices) {
    fixture.topology.vertices.push_back({
        make_id<VertexTag>(vertex.id.index + vertex_offset),
        {vertex.position.x * 1.5F + 10.0F, vertex.position.y, vertex.position.z}});
  }
  for (const auto &face : second.topology.faces) {
    AuthoredTopologyFace copy = face;
    copy.id = make_id<FaceTag>(face.id.index + face_offset);
    for (auto &vertex : copy.vertices) {
      vertex = make_id<VertexTag>(vertex.index + vertex_offset);
    }
    fixture.topology.faces.push_back(std::move(copy));
  }
  for (const auto &rail : second.rails) {
    std::vector<VertexId> copied;
    for (const VertexId vertex : rail) {
      copied.push_back(make_id<VertexTag>(vertex.index + vertex_offset));
    }
    fixture.rails.push_back(std::move(copied));
  }
  fixture.topology.next_vertex_id += second.topology.next_vertex_id - 1U;
  fixture.topology.next_face_id += second.topology.next_face_id - 1U;
  auto document = PolygonDocument::from_authored_topology(fixture.topology);
  ASSERT_TRUE(document.ok());
  auto edges = uv_peel_rail_edges(fixture.rails[1], false);
  const auto additional = uv_peel_rail_edges(fixture.rails[4], false);
  edges.insert(edges.end(), additional.begin(), additional.end());
  const std::array targets{uv_peel_target(document.value(), 1U, edges, edges.front())};
  const auto result = prepare_uv_peel(targets, {.world_space = false});
  ASSERT_TRUE(result.ok()) << result.error_message();
  const auto *prepared = prepared_uv_peel_document(result.value(), 1U);
  ASSERT_NE(prepared, nullptr);
  expect_uv_peel_preserves_document(document.value(), *prepared);
  const auto topology = prepared->document.authored_topology();
  ASSERT_TRUE(topology.ok());
  for (const bool second_component : {false, true}) {
    Vec2 minimum{std::numeric_limits<float>::infinity(),
                  std::numeric_limits<float>::infinity()};
    Vec2 maximum{-std::numeric_limits<float>::infinity(),
                  -std::numeric_limits<float>::infinity()};
    for (const auto &face : topology.value().faces) {
      if ((face.id.index > face_offset) != second_component) {
        continue;
      }
      for (const VertexId vertex : face.vertices) {
        const auto uv = uv_peel_effective_corner(face, vertex);
        ASSERT_TRUE(uv.has_value());
        minimum.x = std::min(minimum.x, uv->x);
        minimum.y = std::min(minimum.y, uv->y);
        maximum.x = std::max(maximum.x, uv->x);
        maximum.y = std::max(maximum.y, uv->y);
      }
    }
    expect_vec2_near(minimum, {0.0F, 0.0F}, 0.002F);
    expect_vec2_near(maximum, {1.0F, 1.0F}, 0.002F);
  }
}

TEST(ModelingSdkConsumerTests,
     UvPeelMultipleObjectsUseTheirWorldDensityAndPrepareDeterministically) {
  const auto fixture = make_uv_peel_flat_strip();
  auto first = PolygonDocument::from_authored_topology(fixture.topology);
  auto second = PolygonDocument::from_authored_topology(fixture.topology);
  ASSERT_TRUE(first.ok());
  ASSERT_TRUE(second.ok());
  const auto edges = uv_peel_rail_edges(fixture.rails[1], false);
  const Transform3 second_world{
      .x_axis = {0.0F, 2.0F, 0.0F}, .y_axis = {-1.0F, 0.0F, 0.0F},
      .z_axis = {0.0F, 0.0F, 1.0F}, .origin = {17.0F, -3.0F, 0.0F},
  };
  std::array targets{
      uv_peel_target(first.value(), 30U, edges, edges.front()),
      uv_peel_target(second.value(), 10U, edges, edges.front(), second_world)};
  const auto baseline = prepare_uv_peel(targets, {});
  ASSERT_TRUE(baseline.ok()) << baseline.error_message();
  std::ranges::reverse(targets);
  for (auto &target : targets) {
    std::ranges::reverse(target.edges);
  }
  const auto reordered = prepare_uv_peel(targets, {});
  ASSERT_TRUE(reordered.ok()) << reordered.error_message();
  for (const auto &target : targets) {
    const auto *before = prepared_uv_peel_document(baseline.value(), target.key);
    const auto *after = prepared_uv_peel_document(reordered.value(), target.key);
    ASSERT_NE(before, nullptr);
    ASSERT_NE(after, nullptr);
    expect_uv_peel_preserves_document(*target.document, *after);
    const auto topology = after->document.authored_topology();
    ASSERT_TRUE(topology.ok());
    expect_uv_peel_guide(topology.value(), fixture.rails[1], target.world_from_local,
                          UvPeelAxis::U);
    const auto before_bytes = serialize_qdr_authored_topology(before->document);
    const auto after_bytes = serialize_qdr_authored_topology(after->document);
    ASSERT_TRUE(before_bytes.ok());
    ASSERT_TRUE(after_bytes.ok());
    EXPECT_EQ(after_bytes.value(), before_bytes.value());
  }
}

TEST(ModelingSdkConsumerTests,
     UvPeelOptionsRejectInvalidFactorsOffsetsAndAxesWithoutChangingValidValues) {
  const UvPeelOptions valid{.axis = UvPeelAxis::V, .world_space = false,
                           .u_repeat = 2.5, .v_repeat = 0.125,
                           .u_offset = -0.75, .v_offset = 3.125};
  const auto accepted = validate_uv_peel_options(valid);
  ASSERT_TRUE(accepted.ok()) << accepted.error_message();
  EXPECT_EQ(accepted.value().axis, valid.axis);
  EXPECT_EQ(accepted.value().world_space, valid.world_space);
  EXPECT_EQ(accepted.value().u_repeat, valid.u_repeat);
  EXPECT_EQ(accepted.value().v_repeat, valid.v_repeat);
  EXPECT_EQ(accepted.value().u_offset, valid.u_offset);
  EXPECT_EQ(accepted.value().v_offset, valid.v_offset);
  for (const double invalid : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                               std::numeric_limits<double>::quiet_NaN()}) {
    auto options = valid;
    options.u_repeat = invalid;
    EXPECT_FALSE(validate_uv_peel_options(options).ok());
    options = valid;
    options.v_repeat = invalid;
    EXPECT_FALSE(validate_uv_peel_options(options).ok());
  }
  for (const double invalid : {std::numeric_limits<double>::infinity(),
                               std::numeric_limits<double>::quiet_NaN()}) {
    auto options = valid;
    options.u_offset = invalid;
    EXPECT_FALSE(validate_uv_peel_options(options).ok());
    options = valid;
    options.v_offset = invalid;
    EXPECT_FALSE(validate_uv_peel_options(options).ok());
  }
  auto options = valid;
  options.axis = static_cast<UvPeelAxis>(99);
  EXPECT_FALSE(validate_uv_peel_options(options).ok());
}

TEST(ModelingSdkConsumerTests,
     UvPeelRejectsInvalidOrBranchedBatchesWithoutPublishingAnyObject) {
  const auto fixture = make_uv_peel_flat_strip();
  auto first = PolygonDocument::from_authored_topology(fixture.topology);
  auto second = PolygonDocument::from_authored_topology(fixture.topology);
  ASSERT_TRUE(first.ok());
  ASSERT_TRUE(second.ok());
  const auto edges = uv_peel_rail_edges(fixture.rails[1], false);
  const auto first_identity = first.value().content_identity();
  const auto second_identity = second.value().content_identity();
  const auto first_revision = first.value().content_revision();
  const auto second_revision = second.value().content_revision();
  const auto first_before = serialize_qdr_authored_topology(first.value());
  const auto second_before = serialize_qdr_authored_topology(second.value());
  ASSERT_TRUE(first_before.ok());
  ASSERT_TRUE(second_before.ok());
  for (int invalid_case = 0; invalid_case < 13; ++invalid_case) {
    SCOPED_TRACE(invalid_case);
    std::array targets{
        uv_peel_target(first.value(), 1U, edges, edges.front()),
        uv_peel_target(second.value(), 2U, edges, edges.front())};
    UvPeelOptions options;
    switch (invalid_case) {
    case 0: targets[1].document = nullptr; break;
    case 1: targets[1].edges.clear(); break;
    case 2: targets[1].key = 1U; break;
    case 3: targets[1].edges.front().a.generation = 2U; break;
    case 4:
      targets[1].edges = {make_edge_key(fixture.topology.faces.front().vertices[0],
                                       fixture.topology.faces.front().vertices[2])};
      break;
    case 5:
      targets[1].edges = {EdgeKey{fixture.rails[1][0], fixture.rails[1][0]}};
      break;
    case 6: targets[1].world_from_local.x_axis = {}; break;
    case 7: targets[1].sampling_from_local.z_axis = {}; break;
    case 8:
      targets[1].world_from_local.origin.x = std::numeric_limits<float>::quiet_NaN();
      break;
    case 9: options.u_repeat = 0.0; break;
    case 10: options.u_repeat = 1.0e300; break;
    case 11: options.v_offset = 1.0e300; break;
    case 12:
      targets[1].edges.push_back(make_edge_key(fixture.rails[0][1], fixture.rails[1][1]));
      targets[1].edges.push_back(make_edge_key(fixture.rails[1][1], fixture.rails[2][1]));
      break;
    default: FAIL() << "Unexpected invalid UV peel case";
    }
    const auto result = prepare_uv_peel(targets, options);
    ASSERT_FALSE(result.ok());
    EXPECT_FALSE(result.error_message().empty());
    EXPECT_EQ(first.value().content_identity(), first_identity);
    EXPECT_EQ(second.value().content_identity(), second_identity);
    EXPECT_EQ(first.value().content_revision(), first_revision);
    EXPECT_EQ(second.value().content_revision(), second_revision);
    const auto first_after = serialize_qdr_authored_topology(first.value());
    const auto second_after = serialize_qdr_authored_topology(second.value());
    ASSERT_TRUE(first_after.ok());
    ASSERT_TRUE(second_after.ok());
    EXPECT_EQ(first_after.value(), first_before.value());
    EXPECT_EQ(second_after.value(), second_before.value());
  }
}

TEST(ModelingSdkConsumerTests,
     UvPeelRejectsPinchedConnectedPatchesButAcceptsSeparatePointTouchingPatches) {
  auto pinched = make_uv_peel_strip(
      {{{0.0F, 0.0F, 0.0F}, {2.0F, 0.0F, 0.0F},
        {2.0F, 2.0F, 0.0F}, {0.0F, 2.0F, 2.0F}},
       {{-1.0F, -1.0F, 0.0F}, {3.0F, -1.0F, 0.0F},
        {3.0F, 2.0F, 1.0F}, {0.0F, 0.0F, 0.0F}}}, false);
  const VertexId removed = pinched.rails[1].back();
  const VertexId shared = pinched.rails[0].front();
  for (auto &face : pinched.topology.faces) {
    std::ranges::replace(face.vertices, removed, shared);
  }
  pinched.topology.vertices.pop_back();
  auto invalid_document = PolygonDocument::from_authored_topology(pinched.topology);
  ASSERT_TRUE(invalid_document.ok()) << invalid_document.error_message();
  const auto identity = invalid_document.value().content_identity();
  const auto edges = uv_peel_rail_edges(pinched.rails[0], false);
  const std::array invalid_targets{
      uv_peel_target(invalid_document.value(), 1U, edges, edges.front())};
  const auto rejected = prepare_uv_peel(invalid_targets, {});
  ASSERT_FALSE(rejected.ok());
  EXPECT_FALSE(rejected.error_message().empty());
  EXPECT_EQ(invalid_document.value().content_identity(), identity);

  AuthoredTopology disconnected;
  disconnected.vertices = {
      {make_id<VertexTag>(1U), {0.0F, 0.0F, 0.0F}},
      {make_id<VertexTag>(2U), {2.0F, 0.0F, 0.0F}},
      {make_id<VertexTag>(3U), {2.0F, 2.0F, 0.0F}},
      {make_id<VertexTag>(4U), {0.0F, 2.0F, 0.0F}},
      {make_id<VertexTag>(5U), {-2.0F, 0.0F, 0.0F}},
      {make_id<VertexTag>(6U), {-2.0F, -2.0F, 0.0F}},
      {make_id<VertexTag>(7U), {0.0F, -2.0F, 0.0F}},
  };
  disconnected.faces = {
      {.id = make_id<FaceTag>(1U),
       .vertices = {make_id<VertexTag>(1U), make_id<VertexTag>(2U),
                    make_id<VertexTag>(3U), make_id<VertexTag>(4U)}},
      {.id = make_id<FaceTag>(2U),
       .vertices = {make_id<VertexTag>(1U), make_id<VertexTag>(5U),
                    make_id<VertexTag>(6U), make_id<VertexTag>(7U)}},
  };
  disconnected.next_vertex_id = 8U;
  disconnected.next_face_id = 3U;
  auto valid_document = PolygonDocument::from_authored_topology(disconnected);
  ASSERT_TRUE(valid_document.ok());
  const std::array valid_targets{uv_peel_target(
      valid_document.value(), 1U,
      {make_edge_key(make_id<VertexTag>(1U), make_id<VertexTag>(2U)),
       make_edge_key(make_id<VertexTag>(1U), make_id<VertexTag>(5U))})};
  const auto accepted = prepare_uv_peel(valid_targets, {});
  ASSERT_TRUE(accepted.ok()) << accepted.error_message();
  const auto *prepared = prepared_uv_peel_document(accepted.value(), 1U);
  ASSERT_NE(prepared, nullptr);
  expect_uv_peel_preserves_document(valid_document.value(), *prepared);
  const auto after = prepared->document.authored_topology();
  ASSERT_TRUE(after.ok());
  for (const auto &face : after.value().faces) {
    const auto first = uv_peel_effective_corner(face, face.vertices[0]);
    const auto second = uv_peel_effective_corner(face, face.vertices[1]);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_NEAR(second->x - first->x, 1.0F, 0.002F);
    EXPECT_NEAR(second->y, first->y, 0.002F);
  }
}

TEST(ModelingSdkConsumerTests,
     UvPeelRejectsClosedDiskGuidesAndBoundarylessPatchesAtomically) {
  constexpr std::array<Vec3, 4> kSquare{{
      {0.0F, 0.0F, 0.0F}, {2.0F, 0.0F, 0.0F},
      {2.0F, 2.0F, 0.0F}, {0.0F, 2.0F, 0.0F}}};
  auto disk = make_texture_action_face(kSquare);
  ASSERT_TRUE(disk.ok());
  const auto disk_identity = disk.value().content_identity();
  const std::array disk_targets{
      uv_peel_target(disk.value(), 1U, disk.value().edge_ids())};
  const auto disk_result = prepare_uv_peel(disk_targets, {});
  ASSERT_FALSE(disk_result.ok());
  EXPECT_EQ(disk.value().content_identity(), disk_identity);

  const auto disk_topology = disk.value().authored_topology();
  ASSERT_TRUE(disk_topology.ok());
  const auto open_edges =
      uv_peel_rail_edges(disk_topology.value().faces.front().vertices, false);
  ASSERT_EQ(open_edges.size(), 3U);
  const auto disk_revision = disk.value().content_revision();
  const auto disk_selection_revision = disk.value().selection_revision();
  const auto disk_before = serialize_qdr_authored_topology(disk.value());
  ASSERT_TRUE(disk_before.ok());
  const std::array collapsed_guide_targets{
      uv_peel_target(disk.value(), 1U, open_edges, open_edges.front())};
  const auto collapsed_guide = prepare_uv_peel(collapsed_guide_targets, {});
  ASSERT_FALSE(collapsed_guide.ok());
  EXPECT_FALSE(collapsed_guide.error_message().empty());
  EXPECT_EQ(disk.value().content_identity(), disk_identity);
  EXPECT_EQ(disk.value().content_revision(), disk_revision);
  EXPECT_EQ(disk.value().selection_revision(), disk_selection_revision);

  const std::array one_edge_targets{
      uv_peel_target(disk.value(), 1U, {open_edges.front()}, open_edges.front())};
  const auto representable = prepare_uv_peel(one_edge_targets, {});
  ASSERT_TRUE(representable.ok()) << representable.error_message();
  const auto collapsed_float = prepare_uv_peel(
      one_edge_targets, {.u_offset = 1.0e8, .v_offset = 1.0e8});
  ASSERT_FALSE(collapsed_float.ok());
  EXPECT_FALSE(collapsed_float.error_message().empty());
  EXPECT_EQ(disk.value().content_identity(), disk_identity);
  EXPECT_EQ(disk.value().content_revision(), disk_revision);
  EXPECT_EQ(disk.value().selection_revision(), disk_selection_revision);
  const auto disk_after = serialize_qdr_authored_topology(disk.value());
  ASSERT_TRUE(disk_after.ok());
  EXPECT_EQ(disk_after.value(), disk_before.value());

  AuthoredTopology tetrahedron;
  tetrahedron.vertices = {
      {make_id<VertexTag>(1U), {0.0F, 0.0F, 0.0F}},
      {make_id<VertexTag>(2U), {2.0F, 0.0F, 0.0F}},
      {make_id<VertexTag>(3U), {0.0F, 2.0F, 0.0F}},
      {make_id<VertexTag>(4U), {0.0F, 0.0F, 2.0F}}};
  tetrahedron.faces = {
      {.id = make_id<FaceTag>(1U), .vertices = {
          make_id<VertexTag>(1U), make_id<VertexTag>(3U), make_id<VertexTag>(2U)}},
      {.id = make_id<FaceTag>(2U), .vertices = {
          make_id<VertexTag>(1U), make_id<VertexTag>(2U), make_id<VertexTag>(4U)}},
      {.id = make_id<FaceTag>(3U), .vertices = {
          make_id<VertexTag>(1U), make_id<VertexTag>(4U), make_id<VertexTag>(3U)}},
      {.id = make_id<FaceTag>(4U), .vertices = {
          make_id<VertexTag>(2U), make_id<VertexTag>(3U), make_id<VertexTag>(4U)}},
  };
  tetrahedron.next_vertex_id = 5U;
  tetrahedron.next_face_id = 5U;
  auto closed = PolygonDocument::from_authored_topology(tetrahedron);
  ASSERT_TRUE(closed.ok());
  const auto closed_identity = closed.value().content_identity();
  const std::array closed_targets{uv_peel_target(
      closed.value(), 1U, uv_peel_rail_edges(tetrahedron.faces[0].vertices, true))};
  const auto closed_result = prepare_uv_peel(closed_targets, {});
  ASSERT_FALSE(closed_result.ok());
  EXPECT_FALSE(closed_result.error_message().empty());
  EXPECT_EQ(closed.value().content_identity(), closed_identity);

  // The central 200-degree pair is straightened to 180 degrees. Its two free
  // 100-degree wedges then overlap despite individually consistent winding.
  constexpr double kHundredDegrees = 1.7453292519943295769;
  AuthoredTopology fan;
  const VertexId center = make_id<VertexTag>(1U);
  fan.vertices.push_back({center, {}});
  for (std::uint32_t outer = 0U; outer < 5U; ++outer) {
    const double angle = static_cast<double>(outer) * kHundredDegrees;
    fan.vertices.push_back({
        make_id<VertexTag>(outer + 2U),
        {static_cast<float>(std::cos(angle)),
         static_cast<float>(std::sin(angle)), 0.0F}});
  }
  for (std::uint32_t wedge = 0U; wedge < 4U; ++wedge) {
    fan.faces.push_back({
        .id = make_id<FaceTag>(wedge + 1U),
        .vertices = {center, make_id<VertexTag>(wedge + 2U),
                     make_id<VertexTag>(wedge + 3U)}});
    const Vec3 first = fan.vertices[wedge + 1U].position;
    const Vec3 second = fan.vertices[wedge + 2U].position;
    EXPECT_GT(first.x * second.y - first.y * second.x, 0.98F);
  }
  fan.next_vertex_id = 7U;
  fan.next_face_id = 5U;
  auto overlapping = PolygonDocument::from_authored_topology(fan);
  ASSERT_TRUE(overlapping.ok()) << overlapping.error_message();
  const std::vector fan_edges{
      make_edge_key(center, make_id<VertexTag>(3U)),
      make_edge_key(center, make_id<VertexTag>(5U))};
  ASSERT_TRUE(overlapping.value().select_edges(fan_edges).ok());
  const auto fan_identity = overlapping.value().content_identity();
  const auto fan_revision = overlapping.value().content_revision();
  const auto fan_selection_revision = overlapping.value().selection_revision();
  const auto fan_before = serialize_qdr_authored_topology(overlapping.value());
  ASSERT_TRUE(fan_before.ok());
  const std::array fan_targets{uv_peel_target(
      overlapping.value(), 1U, fan_edges, fan_edges.front())};
  const auto fan_result = prepare_uv_peel(fan_targets, {});
  ASSERT_FALSE(fan_result.ok());
  EXPECT_NE(fan_result.error_message().find("overlap"), std::string::npos)
      << fan_result.error_message();
  EXPECT_EQ(overlapping.value().content_identity(), fan_identity);
  EXPECT_EQ(overlapping.value().content_revision(), fan_revision);
  EXPECT_EQ(overlapping.value().selection_revision(), fan_selection_revision);
  EXPECT_EQ(overlapping.value().selection().edges, fan_edges);
  const auto fan_after = serialize_qdr_authored_topology(overlapping.value());
  ASSERT_TRUE(fan_after.ok());
  EXPECT_EQ(fan_after.value(), fan_before.value());
}

TEST(ModelingSdkConsumerTests,
     UvPeelPreparedBatchReplaysExactAuthoredUvsAndSelectionsThroughSdkHistory) {
  const auto fixture = make_uv_peel_curved_strip(
      true, {.scale = {-2.0F, 0.5F}, .shift_uv = {0.25F, -0.75F},
             .rotation_degrees = 30.0F});
  const auto edges = uv_peel_rail_edges(fixture.rails[1], false);
  ModelingSession session;
  std::vector<ObjectId> objects;
  for (const char *name : {"First peeled road", "Second peeled road"}) {
    auto document = PolygonDocument::from_authored_topology(fixture.topology);
    ASSERT_TRUE(document.ok());
    ASSERT_TRUE(document.value().select_edges(edges).ok());
    const auto added = session.add_document(std::move(document).value(), name);
    ASSERT_TRUE(added.ok());
    objects.push_back(added.value());
  }
  ASSERT_TRUE(session.apply_object_selection(objects).ok());
  std::vector<PolygonDocument> originals;
  std::vector<std::vector<std::byte>> before;
  for (const ObjectId object : objects) {
    auto document = session.document_copy(object);
    ASSERT_TRUE(document.ok());
    auto encoded = serialize_qdr_authored_topology(document.value());
    ASSERT_TRUE(encoded.ok());
    before.push_back(std::move(encoded).value());
    originals.push_back(std::move(document).value());
  }
  std::vector<UvPeelTarget> targets;
  for (std::size_t index = 0U; index < objects.size(); ++index) {
    targets.push_back(uv_peel_target(originals[index], objects[index].index,
                                      edges, edges.front()));
  }
  const auto prepared = prepare_uv_peel(targets, {.u_repeat = 2.0, .v_offset = 0.125});
  ASSERT_TRUE(prepared.ok()) << prepared.error_message();
  ASSERT_TRUE(prepared.value().changed);
  std::vector<SessionDocumentUpdate> updates;
  std::vector<std::vector<std::byte>> after;
  for (const ObjectId object : objects) {
    const auto *replacement = prepared_uv_peel_document(prepared.value(), object.index);
    ASSERT_NE(replacement, nullptr);
    const auto encoded = serialize_qdr_authored_topology(replacement->document);
    ASSERT_TRUE(encoded.ok());
    after.push_back(encoded.value());
    updates.push_back({.object = object, .document = replacement->document,
                       .receipt = replacement->receipt});
  }
  const auto revision = session.content_revision();
  const auto committed = session.commit_documents_batch(std::move(updates), true);
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  EXPECT_EQ(session.content_revision(), revision + 1U);
  ASSERT_TRUE(session.undo().ok());
  for (std::size_t index = 0U; index < objects.size(); ++index) {
    const auto document = session.document_copy(objects[index]);
    ASSERT_TRUE(document.ok());
    const auto encoded = serialize_qdr_authored_topology(document.value());
    ASSERT_TRUE(encoded.ok());
    EXPECT_EQ(encoded.value(), before[index]);
    EXPECT_EQ(document.value().selection().edges, originals[index].selection().edges);
    expect_session_object_selected(session, objects[index], true);
  }
  ASSERT_TRUE(session.redo().ok());
  for (std::size_t index = 0U; index < objects.size(); ++index) {
    const auto document = session.document_copy(objects[index]);
    ASSERT_TRUE(document.ok());
    const auto encoded = serialize_qdr_authored_topology(document.value());
    ASSERT_TRUE(encoded.ok());
    EXPECT_EQ(encoded.value(), after[index]);
    EXPECT_EQ(document.value().selection().edges, originals[index].selection().edges);
    expect_session_object_selected(session, objects[index], true);
  }
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

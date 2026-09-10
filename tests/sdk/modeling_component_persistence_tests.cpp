////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include <quader/modeling/modeling.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

using namespace quader::modeling;

AuthoredTopology decorated_box() {
  const auto document = PolygonDocument::make_box({}).or_throw();
  auto topology = document.authored_topology().or_throw();
  for (std::size_t index = 0; index < topology.faces.size(); ++index) {
    auto &face = topology.faces[index];
    face.material_slot = static_cast<std::uint32_t>(index + 1U);
    face.shading = index % 2U == 0U ? AuthoredSurfaceShading::Smooth
                                  : AuthoredSurfaceShading::Flat;
    face.texture_transform = {.scale = {-2.0F, 0.5F},
                              .shift_uv = {0.125F, -0.25F},
                              .rotation_degrees = 45.0F};
    face.uvs.clear();
    for (std::size_t corner = 0; corner < face.vertices.size(); ++corner) {
      face.uvs.push_back({static_cast<float>(corner) * 0.25F,
                         static_cast<float>(index) * 0.125F});
    }
  }
  const auto edges = document.edge_ids();
  for (std::size_t index = 0; index < edges.size(); ++index) {
    (index % 2U == 0U ? topology.hard_normal_edges
                      : topology.soft_normal_edges)
        .push_back(edges[index]);
  }
  std::ranges::sort(topology.hard_normal_edges);
  std::ranges::sort(topology.soft_normal_edges);
  // Preserve retired allocation space as well as the live component IDs.
  topology.next_vertex_id += 19U;
  topology.next_face_id += 23U;
  return topology;
}

void expect_exact_topology(const AuthoredTopology &expected,
                           const PolygonDocument &actual_document) {
  const auto exported = actual_document.authored_topology();
  ASSERT_TRUE(exported.ok()) << exported.error_message();
  const auto &actual = exported.value();
  EXPECT_EQ(actual.next_vertex_id, expected.next_vertex_id);
  EXPECT_EQ(actual.next_face_id, expected.next_face_id);
  EXPECT_EQ(actual.hard_normal_edges, expected.hard_normal_edges);
  EXPECT_EQ(actual.soft_normal_edges, expected.soft_normal_edges);
  ASSERT_EQ(actual.vertices.size(), expected.vertices.size());
  ASSERT_EQ(actual.faces.size(), expected.faces.size());
  for (std::size_t index = 0; index < expected.vertices.size(); ++index) {
    SCOPED_TRACE(index);
    EXPECT_EQ(actual.vertices[index].id, expected.vertices[index].id);
    EXPECT_EQ(actual.vertices[index].position.x, expected.vertices[index].position.x);
    EXPECT_EQ(actual.vertices[index].position.y, expected.vertices[index].position.y);
    EXPECT_EQ(actual.vertices[index].position.z, expected.vertices[index].position.z);
  }
  for (std::size_t index = 0; index < expected.faces.size(); ++index) {
    SCOPED_TRACE(index);
    const auto &expected_face = expected.faces[index];
    const auto &actual_face = actual.faces[index];
    EXPECT_EQ(actual_face.id, expected_face.id);
    EXPECT_EQ(actual_face.vertices, expected_face.vertices);
    EXPECT_EQ(actual_face.material_slot, expected_face.material_slot);
    EXPECT_EQ(actual_face.shading, expected_face.shading);
    EXPECT_EQ(actual_face.texture_transform, expected_face.texture_transform);
    ASSERT_EQ(actual_face.uvs.size(), expected_face.uvs.size());
    for (std::size_t corner = 0; corner < expected_face.uvs.size(); ++corner) {
      EXPECT_EQ(actual_face.uvs[corner].x, expected_face.uvs[corner].x);
      EXPECT_EQ(actual_face.uvs[corner].y, expected_face.uvs[corner].y);
    }
  }
}

void expect_collapsed_round_trip(const PolygonDocument &document) {
  const auto expected = document.authored_topology();
  ASSERT_TRUE(expected.ok()) << expected.error_message();
  const auto validation = document.validate();
  ASSERT_FALSE(validation.ok());
  EXPECT_EQ(validation.error().code, ErrorCode::ValidationFailed);

  const auto geometry_bytes = serialize_qdr_authored_topology(document);
  ASSERT_TRUE(geometry_bytes.ok()) << geometry_bytes.error_message();
  const auto geometry = deserialize_qdr_authored_topology(geometry_bytes.value());
  ASSERT_TRUE(geometry.ok()) << geometry.error_message();
  expect_exact_topology(expected.value(), geometry.value());
  EXPECT_FALSE(geometry.value().validate().ok());
  const auto geometry_reencoded = serialize_qdr_authored_topology(geometry.value());
  ASSERT_TRUE(geometry_reencoded.ok()) << geometry_reencoded.error_message();
  EXPECT_EQ(geometry_reencoded.value(), geometry_bytes.value());

  const auto scene_bytes = serialize_single_document_as_qdr(document);
  ASSERT_TRUE(scene_bytes.ok()) << scene_bytes.error_message();
  const auto scene = deserialize_qdr_document(scene_bytes.value());
  ASSERT_TRUE(scene.ok()) << scene.error_message();
  ASSERT_EQ(scene.value().objects.size(), 1U);
  const auto &restored = scene.value().objects.front().document;
  expect_exact_topology(expected.value(), restored);
  EXPECT_FALSE(restored.validate().ok());
  EXPECT_EQ(restored.selection().faces, document.selection().faces);
  EXPECT_EQ(restored.selection().active_face, document.selection().active_face);
  const auto scene_reencoded = serialize_qdr_document(scene.value());
  ASSERT_TRUE(scene_reencoded.ok()) << scene_reencoded.error_message();
  EXPECT_EQ(scene_reencoded.value(), scene_bytes.value());
}

void write_u32_le(std::vector<std::byte> &bytes, std::size_t offset,
                   std::uint32_t value) {
  for (std::size_t index = 0; index < 4U; ++index) {
    bytes[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xFFU);
  }
}

TEST(ModelingComponentPersistenceTests,
     TranslatingBoxFaceOntoOppositeFaceRetainsCollapsedAuthoredGeometry) {
  const auto topology = decorated_box();
  auto imported = PolygonDocument::from_authored_topology(topology);
  ASSERT_TRUE(imported.ok()) << imported.error_message();
  auto &document = imported.value();
  const auto top_face = std::ranges::find_if(topology.faces, [&](const auto &face) {
    return std::ranges::all_of(face.vertices, [&](VertexId id) {
      const auto vertex = std::ranges::find(topology.vertices, id,
                                           &AuthoredTopologyVertex::id);
      return vertex != topology.vertices.end() && vertex->position.y == 1.0F;
    });
  });
  ASSERT_NE(top_face, topology.faces.end());
  ASSERT_TRUE(document.select_face(top_face->id).ok());
  const auto moved = document.translate_selection({0.0F, -2.0F, 0.0F});
  ASSERT_TRUE(moved.ok()) << moved.error_message();
  ASSERT_TRUE(moved.value().changed);
  const auto after = document.authored_topology();
  ASSERT_TRUE(after.ok());
  ASSERT_EQ(after.value().vertices.size(), topology.vertices.size());
  for (const auto &vertex : after.value().vertices) {
    EXPECT_EQ(vertex.position.y, -1.0F);
  }
  expect_collapsed_round_trip(document);
}

TEST(ModelingComponentPersistenceTests,
     ImportedZeroScaleRetainsCoincidentVerticesFacesAndAuthoredMetadata) {
  auto topology = decorated_box();
  for (auto &vertex : topology.vertices) {
    vertex.position = {2.0F, -3.0F, 4.0F};
  }
  const auto imported = PolygonDocument::from_authored_topology(topology);
  ASSERT_TRUE(imported.ok()) << imported.error_message();
  expect_exact_topology(topology, imported.value());
  expect_collapsed_round_trip(imported.value());
}

TEST(ModelingComponentPersistenceTests,
     ImportedSingleAxisCollapseRetainsTopologyWithoutAutomaticCleanup) {
  auto topology = decorated_box();
  for (auto &vertex : topology.vertices) {
    vertex.position.z = 0.0F;
  }
  const auto imported = PolygonDocument::from_authored_topology(topology);
  ASSERT_TRUE(imported.ok()) << imported.error_message();
  expect_exact_topology(topology, imported.value());
  expect_collapsed_round_trip(imported.value());
}

TEST(ModelingComponentPersistenceTests,
     AuthoredImportStillRejectsMalformedFaceIdentityAndArity) {
  const auto source = decorated_box();
  for (int corruption = 0; corruption < 5; ++corruption) {
    SCOPED_TRACE(corruption);
    auto topology = source;
    auto &face = topology.faces.front();
    if (corruption == 0) {
      face.vertices.resize(2U);
      face.uvs.resize(2U);
    } else if (corruption == 1) {
      face.vertices.back() = face.vertices.front();
    } else if (corruption == 2) {
      face.vertices.front() = make_id<VertexTag>(topology.next_vertex_id - 1U);
    } else if (corruption == 3) {
      topology.vertices.back().id = topology.vertices.front().id;
    } else {
      topology.faces.back().id = topology.faces.front().id;
    }
    const auto imported = PolygonDocument::from_authored_topology(topology);
    EXPECT_FALSE(imported.ok());
  }
}

TEST(ModelingComponentPersistenceTests,
     AuthoredImportStillRejectsNonfinitePositionsAndUvs) {
  for (const float value : {std::numeric_limits<float>::infinity(),
                            -std::numeric_limits<float>::infinity(),
                            std::numeric_limits<float>::quiet_NaN()}) {
    auto topology = decorated_box();
    topology.vertices.front().position.x = value;
    EXPECT_FALSE(PolygonDocument::from_authored_topology(topology).ok());
    topology = decorated_box();
    topology.faces.front().uvs.front().y = value;
    EXPECT_FALSE(PolygonDocument::from_authored_topology(topology).ok());
  }
}

TEST(ModelingComponentPersistenceTests,
     AuthoredDecoderStillRejectsMalformedCornersAndNonfiniteCoordinates) {
  auto topology = decorated_box();
  for (auto &vertex : topology.vertices) {
    vertex.position = {};
  }
  const auto imported = PolygonDocument::from_authored_topology(topology);
  ASSERT_TRUE(imported.ok()) << imported.error_message();
  const auto encoded = serialize_qdr_authored_topology(imported.value());
  ASSERT_TRUE(encoded.ok()) << encoded.error_message();
  // QGEO 1.x stores IDs, positions, face IDs, offsets, then corner vertex IDs.
  constexpr std::size_t kHeaderSize = 56U;
  ASSERT_GE(encoded.value().size(), kHeaderSize);
  EXPECT_EQ(encoded.value()[8], static_cast<std::byte>(kHeaderSize));
  const std::size_t positions_offset = kHeaderSize + topology.vertices.size() * 4U;
  const std::size_t offsets_offset =
      kHeaderSize + topology.vertices.size() * 16U + topology.faces.size() * 4U;
  const std::size_t corners_offset = offsets_offset + (topology.faces.size() + 1U) * 4U;
  for (int corruption = 0; corruption < 4; ++corruption) {
    SCOPED_TRACE(corruption);
    auto bytes = encoded.value();
    if (corruption == 0) {
      write_u32_le(bytes, positions_offset, 0x7F800000U);
    } else if (corruption == 1) {
      write_u32_le(bytes, offsets_offset + 4U, 2U);
    } else if (corruption == 2) {
      write_u32_le(bytes, corners_offset + 4U,
                   topology.faces.front().vertices.front().index);
    } else {
      write_u32_le(bytes, corners_offset, topology.next_vertex_id - 1U);
    }
    EXPECT_FALSE(deserialize_qdr_authored_topology(bytes).ok());
  }
}

} // namespace

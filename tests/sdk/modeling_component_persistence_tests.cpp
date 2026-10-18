////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include <quader/modeling/modeling.hpp>

#include <algorithm>
#include <bit>
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

AuthoredTopology decorated_nonmanifold_fan() {
  const auto vertex_id = [](std::uint32_t index) {
    return make_id<VertexTag>(index);
  };
  AuthoredTopology topology;
  topology.next_vertex_id = 140U;
  topology.next_face_id = 90U;
  topology.vertices = {
      {vertex_id(10U), {0.0F, 0.0F, 0.0F}},
      {vertex_id(20U), {0.0F, 0.0F, 1.0F}},
      {vertex_id(30U), {1.0F, 0.0F, 1.0F}},
      {vertex_id(40U), {1.0F, 0.0F, 0.0F}},
      {vertex_id(50U), {0.0F, 1.0F, 1.0F}},
      {vertex_id(60U), {0.0F, 1.0F, 0.0F}},
      {vertex_id(70U), {-1.0F, 0.0F, 1.0F}},
      {vertex_id(80U), {-1.0F, 0.0F, 0.0F}},
  };
  topology.faces = {
      {.id = make_id<FaceTag>(11U),
       .vertices = {vertex_id(10U), vertex_id(20U), vertex_id(30U), vertex_id(40U)}},
      {.id = make_id<FaceTag>(31U),
       .vertices = {vertex_id(10U), vertex_id(20U), vertex_id(50U), vertex_id(60U)}},
      {.id = make_id<FaceTag>(42U),
       .vertices = {vertex_id(10U), vertex_id(20U), vertex_id(70U), vertex_id(80U)}},
  };
  for (std::size_t index = 0U; index < topology.faces.size(); ++index) {
    auto &face = topology.faces[index];
    face.material_slot = static_cast<std::uint32_t>(index + 5U);
    face.shading = index == 0U ? AuthoredSurfaceShading::Smooth
                   : index == 1U ? AuthoredSurfaceShading::Flat
                                 : AuthoredSurfaceShading::Authored;
    face.texture_transform = {.scale = {-2.0F, 0.5F},
                              .shift_uv = {0.125F, -0.25F},
                              .rotation_degrees = 45.0F};
    face.uvs = {{0.0F, static_cast<float>(index)},
                {0.25F, static_cast<float>(index)},
                {0.25F, static_cast<float>(index) + 0.5F},
                {0.0F, static_cast<float>(index) + 0.5F}};
  }
  topology.hard_normal_edges = {make_edge_key(vertex_id(10U), vertex_id(20U))};
  topology.soft_normal_edges = {make_edge_key(vertex_id(20U), vertex_id(50U))};
  return topology;
}

void expect_exact_topology(const AuthoredTopology &expected,
                           const PolygonDocument &actual_document) {
  const auto expect_float_bits = [](float actual, float expected_value) {
    EXPECT_EQ(std::bit_cast<std::uint32_t>(actual),
              std::bit_cast<std::uint32_t>(expected_value));
  };
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
    expect_float_bits(actual.vertices[index].position.x, expected.vertices[index].position.x);
    expect_float_bits(actual.vertices[index].position.y, expected.vertices[index].position.y);
    expect_float_bits(actual.vertices[index].position.z, expected.vertices[index].position.z);
  }
  for (std::size_t index = 0; index < expected.faces.size(); ++index) {
    SCOPED_TRACE(index);
    const auto &expected_face = expected.faces[index];
    const auto &actual_face = actual.faces[index];
    EXPECT_EQ(actual_face.id, expected_face.id);
    EXPECT_EQ(actual_face.vertices, expected_face.vertices);
    EXPECT_EQ(actual_face.material_slot, expected_face.material_slot);
    EXPECT_EQ(actual_face.shading, expected_face.shading);
    expect_float_bits(actual_face.texture_transform.scale.x,
                      expected_face.texture_transform.scale.x);
    expect_float_bits(actual_face.texture_transform.scale.y,
                      expected_face.texture_transform.scale.y);
    expect_float_bits(actual_face.texture_transform.shift_uv.x,
                      expected_face.texture_transform.shift_uv.x);
    expect_float_bits(actual_face.texture_transform.shift_uv.y,
                      expected_face.texture_transform.shift_uv.y);
    expect_float_bits(actual_face.texture_transform.rotation_degrees,
                      expected_face.texture_transform.rotation_degrees);
    ASSERT_EQ(actual_face.uvs.size(), expected_face.uvs.size());
    for (std::size_t corner = 0; corner < expected_face.uvs.size(); ++corner) {
      expect_float_bits(actual_face.uvs[corner].x, expected_face.uvs[corner].x);
      expect_float_bits(actual_face.uvs[corner].y, expected_face.uvs[corner].y);
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
     Qgeo13KeepsAutomaticSmoothAndExplicitFlatDistinct) {
  auto topology = decorated_box();
  topology.faces[2].shading = AuthoredSurfaceShading::Authored;
  const auto document = PolygonDocument::from_authored_topology(topology);
  ASSERT_TRUE(document.ok()) << document.error_message();
  const auto encoded = serialize_qdr_authored_topology(document.value());
  ASSERT_TRUE(encoded.ok()) << encoded.error_message();
  EXPECT_EQ(encoded.value()[6], std::byte{3});
  const auto decoded = deserialize_qdr_authored_topology(encoded.value());
  ASSERT_TRUE(decoded.ok()) << decoded.error_message();
  expect_exact_topology(topology, decoded.value());
  EXPECT_EQ(serialize_qdr_authored_topology(decoded.value()).or_throw(),
            encoded.value());
}

TEST(ModelingComponentPersistenceTests,
     Pre13FaceDefaultsMigrateAfterLegacySoftEdgesWithoutInventingOverrides) {
  const auto vertex = [](std::uint32_t id) { return make_id<VertexTag>(id); };
  const EdgeKey smooth_shared{vertex(2), vertex(3)};
  const EdgeKey flat_shared{vertex(4), vertex(5)};
  const EdgeKey hard{vertex(1), vertex(2)};
  AuthoredTopology topology;
  topology.next_vertex_id = 7;
  topology.next_face_id = 5;
  topology.vertices = {
      {vertex(1), {0.0F, 0.0F, 0.0F}},
      {vertex(2), {1.0F, 0.0F, 0.0F}},
      {vertex(3), {0.0F, 1.0F, 0.0F}},
      {vertex(4), {1.0F, 1.0F, 0.0F}},
      {vertex(5), {2.0F, 0.0F, 0.0F}},
      {vertex(6), {2.0F, 1.0F, 0.0F}},
  };
  topology.faces = {
      {.id = make_id<FaceTag>(1), .vertices = {vertex(1), vertex(2), vertex(3)},
       .shading = AuthoredSurfaceShading::Smooth},
      {.id = make_id<FaceTag>(2), .vertices = {vertex(2), vertex(4), vertex(3)},
       .shading = AuthoredSurfaceShading::Smooth},
      {.id = make_id<FaceTag>(3), .vertices = {vertex(2), vertex(5), vertex(4)},
       .shading = AuthoredSurfaceShading::Flat},
      {.id = make_id<FaceTag>(4), .vertices = {vertex(5), vertex(6), vertex(4)},
       .shading = AuthoredSurfaceShading::Flat},
  };
  topology.hard_normal_edges = {hard};
  for (const unsigned minor : {0U, 1U, 2U}) {
    SCOPED_TRACE(minor);
    topology.soft_normal_edges = minor == 2U ? std::vector<EdgeKey>{flat_shared}
                                             : std::vector<EdgeKey>{};
    const auto document = PolygonDocument::from_authored_topology(topology);
    ASSERT_TRUE(document.ok()) << document.error_message();
    auto encoded = serialize_qdr_authored_topology(document.value()).or_throw();
    encoded[6] = static_cast<std::byte>(minor);
    if (minor == 0U) {
      encoded.resize(encoded.size() - topology.faces.size() * 20U);
      write_u32_le(encoded, 16U, static_cast<std::uint32_t>(encoded.size()));
    }
    const auto decoded = deserialize_qdr_authored_topology(encoded);
    ASSERT_TRUE(decoded.ok()) << decoded.error_message();
    auto expected = topology;
    expected.faces[2].shading = AuthoredSurfaceShading::Authored;
    expected.faces[3].shading = AuthoredSurfaceShading::Authored;
    expected.soft_normal_edges = {minor == 2U ? flat_shared : smooth_shared};
    expect_exact_topology(expected, decoded.value());
    const auto canonical = serialize_qdr_authored_topology(decoded.value());
    ASSERT_TRUE(canonical.ok()) << canonical.error_message();
    EXPECT_EQ(canonical.value()[6], std::byte{3});
    const auto reloaded = deserialize_qdr_authored_topology(canonical.value());
    ASSERT_TRUE(reloaded.ok()) << reloaded.error_message();
    expect_exact_topology(expected, reloaded.value());
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
     PersistedNonmanifoldFanPreservesTopologyAttributesAndStrictDiagnostics) {
  const auto topology = decorated_nonmanifold_fan();
  EXPECT_FALSE(PolygonDocument::from_authored_topology(topology).ok());
  auto imported = PolygonDocument::from_persisted_topology(topology);
  ASSERT_TRUE(imported.ok()) << imported.error_message();
  auto &document = imported.value();
  expect_exact_topology(topology, document);
  const auto strict_validation = document.validate();
  ASSERT_FALSE(strict_validation.ok());
  EXPECT_TRUE(std::ranges::any_of(
      strict_validation.error().diagnostics, [](const Diagnostic &diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Error &&
               diagnostic.message == "Edge 10-20 is referenced by 3 faces.";
      }));
  ASSERT_TRUE(document.select_face(make_id<FaceTag>(31U)).ok());

  const auto geometry_bytes = serialize_qdr_authored_topology(document);
  ASSERT_TRUE(geometry_bytes.ok()) << geometry_bytes.error_message();
  const auto geometry = deserialize_qdr_authored_topology(geometry_bytes.value());
  ASSERT_TRUE(geometry.ok()) << geometry.error_message();
  expect_exact_topology(topology, geometry.value());
  EXPECT_FALSE(geometry.value().validate().ok());
  EXPECT_EQ(serialize_qdr_authored_topology(geometry.value()).or_throw(),
            geometry_bytes.value());

  const auto scene_bytes = serialize_single_document_as_qdr(document);
  ASSERT_TRUE(scene_bytes.ok()) << scene_bytes.error_message();
  const auto scene = deserialize_qdr_document(scene_bytes.value());
  ASSERT_TRUE(scene.ok()) << scene.error_message();
  ASSERT_EQ(scene.value().objects.size(), 1U);
  const auto &restored = scene.value().objects.front().document;
  expect_exact_topology(topology, restored);
  EXPECT_FALSE(restored.validate().ok());
  EXPECT_EQ(restored.selection().faces, document.selection().faces);
  EXPECT_EQ(restored.selection().active_face, document.selection().active_face);
  EXPECT_EQ(serialize_qdr_document(scene.value()).or_throw(), scene_bytes.value());
}

TEST(ModelingComponentPersistenceTests,
     ValidatedAuthoredImportRejectsMalformedCornersUvsAndNormalEdges) {
  for (int corruption = 0; corruption < 6; ++corruption) {
    SCOPED_TRACE(corruption);
    auto topology = decorated_box();
    auto &face = topology.faces.front();
    if (corruption == 0) {
      face.vertices.resize(2U);
      face.uvs.resize(2U);
    } else if (corruption == 1) {
      face.vertices.back() = face.vertices.front();
    } else if (corruption == 2) {
      face.vertices.back() = make_id<VertexTag>(130U);
    } else if (corruption == 3) {
      face.uvs.pop_back();
    } else if (corruption == 4) {
      topology.hard_normal_edges.push_back(make_edge_key(
          topology.vertices.front().id,
          make_id<VertexTag>(topology.next_vertex_id - 1U)));
    } else {
      topology.soft_normal_edges.push_back(topology.hard_normal_edges.front());
    }
    EXPECT_FALSE(PolygonDocument::from_authored_topology(topology).ok());
  }
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
     AuthoredDecoderRetainsMalformedCornersAndNonfiniteCoordinates) {
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
    const auto decoded = deserialize_qdr_authored_topology(bytes);
    ASSERT_TRUE(decoded.ok()) << decoded.error_message();
    EXPECT_EQ(serialize_qdr_authored_topology(decoded.value()).or_throw(), bytes);
  }
}

TEST(ModelingComponentPersistenceTests,
     PersistedTopologyRetainsMalformedRecordsVerbatimThroughGeometryAndSceneIo) {
  for (int malformed_case = 0; malformed_case < 15; ++malformed_case) {
    SCOPED_TRACE(malformed_case);
    auto topology = decorated_box();
    auto &face = topology.faces.front();
    switch (malformed_case) {
    case 0:
      face.vertices.resize(2U);
      face.uvs.resize(2U);
      break;
    case 1:
      face.vertices.back() = face.vertices.front();
      break;
    case 2:
      face.vertices.back() = make_id<VertexTag>(topology.next_vertex_id + 20U);
      break;
    case 3:
      face.uvs.pop_back();
      break;
    case 4: {
      const EdgeKey reversed{topology.hard_normal_edges.front().b,
                             topology.hard_normal_edges.front().a};
      topology.hard_normal_edges = {reversed, reversed};
      break;
    }
    case 5:
      topology.soft_normal_edges.push_back(topology.hard_normal_edges.front());
      break;
    case 6:
      topology.hard_normal_edges.push_back(
          {make_id<VertexTag>(0U), make_id<VertexTag>(200U)});
      break;
    case 7:
      topology.vertices.back().id = topology.vertices.front().id;
      break;
    case 8:
      topology.faces.back().id = topology.faces.front().id;
      break;
    case 9:
      topology.vertices.front().id = make_id<VertexTag>(0U);
      face.id = make_id<FaceTag>(0U);
      break;
    case 10:
      topology.next_vertex_id = 0U;
      topology.next_face_id = 1U;
      break;
    case 11:
      topology.vertices.front().position.x = std::bit_cast<float>(0x7FC12345U);
      topology.vertices.front().position.y = std::numeric_limits<float>::infinity();
      face.uvs.front().y = std::bit_cast<float>(0xFFC54321U);
      face.texture_transform.scale.x = 0.0F;
      face.texture_transform.shift_uv.x = -std::numeric_limits<float>::infinity();
      break;
    case 12:
      face.shading = static_cast<AuthoredSurfaceShading>(255U);
      break;
    case 13:
      face.vertices.clear();
      face.uvs.clear();
      break;
    case 14:
      for (auto &vertex : topology.vertices) vertex.position = {};
      break;
    }
    const auto stored = PolygonDocument::from_persisted_topology(topology);
    ASSERT_TRUE(stored.ok()) << stored.error_message();
    expect_exact_topology(topology, stored.value());
    const auto encoded = serialize_qdr_authored_topology(stored.value());
    ASSERT_TRUE(encoded.ok()) << encoded.error_message();
    const auto decoded = deserialize_qdr_authored_topology(encoded.value());
    ASSERT_TRUE(decoded.ok()) << decoded.error_message();
    expect_exact_topology(topology, decoded.value());
    EXPECT_EQ(serialize_qdr_authored_topology(decoded.value()).or_throw(),
              encoded.value());

    const auto scene_bytes = serialize_single_document_as_qdr(stored.value());
    ASSERT_TRUE(scene_bytes.ok()) << scene_bytes.error_message();
    const auto scene = deserialize_qdr_document(scene_bytes.value());
    ASSERT_TRUE(scene.ok()) << scene.error_message();
    ASSERT_EQ(scene.value().objects.size(), 1U);
    expect_exact_topology(topology, scene.value().objects.front().document);
    EXPECT_EQ(serialize_qdr_document(scene.value()).or_throw(), scene_bytes.value());
  }
}

TEST(ModelingComponentPersistenceTests,
     PersistedTopologyRejectsUnrepresentableTypedGenerations) {
  for (int identity_case = 0; identity_case < 4; ++identity_case) {
    SCOPED_TRACE(identity_case);
    auto topology = decorated_box();
    if (identity_case == 0) topology.vertices.front().id.generation = 2U;
    else if (identity_case == 1) topology.faces.front().id.generation = 2U;
    else if (identity_case == 2) topology.faces.front().vertices.front().generation = 2U;
    else topology.hard_normal_edges.front().a.generation = 2U;
    EXPECT_FALSE(PolygonDocument::from_persisted_topology(topology).ok());
  }
}

TEST(ModelingComponentPersistenceTests,
     AuthoredDecoderStillRejectsBrokenBinaryCsrBoundsAndCoverage) {
  const auto document = PolygonDocument::make_box({}).or_throw();
  const auto encoded = serialize_qdr_authored_topology(document).or_throw();
  const std::size_t offsets_offset =
      56U + document.vertex_count() * 16U + document.face_count() * 4U;
  const std::uint32_t corner_count = 24U;
  for (int malformed_case = 0; malformed_case < 4; ++malformed_case) {
    SCOPED_TRACE(malformed_case);
    auto bytes = encoded;
    if (malformed_case == 0) write_u32_le(bytes, offsets_offset, 1U);
    else if (malformed_case == 1) {
      write_u32_le(bytes, offsets_offset + document.face_count() * 4U,
                   corner_count - 1U);
    } else if (malformed_case == 2) {
      write_u32_le(bytes, offsets_offset + 4U, corner_count + 1U);
    } else {
      write_u32_le(bytes, offsets_offset + 4U, 5U);
      write_u32_le(bytes, offsets_offset + 8U, 4U);
    }
    EXPECT_FALSE(deserialize_qdr_authored_topology(bytes).ok());
  }
}

} // namespace

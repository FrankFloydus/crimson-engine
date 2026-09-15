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
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <limits>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace {

using namespace quader::modeling;

/** Disconnected faces distinguish edited, untouched and procedural charts. */
AuthoredTopology mixed_chart_topology() {
  AuthoredTopology topology;
  for (std::uint32_t face_index = 0; face_index < 3U; ++face_index) {
    const std::uint32_t first = face_index * 4U + 1U;
    const float offset = static_cast<float>(face_index) * 8.0F;
    const auto a = make_id<VertexTag>(first);
    const auto b = make_id<VertexTag>(first + 1U);
    const auto c = make_id<VertexTag>(first + 2U);
    const auto d = make_id<VertexTag>(first + 3U);
    topology.vertices.insert(topology.vertices.end(),
        {{a, {offset, 0.0F, 0.0F}}, {b, {offset + 4.0F, 0.0F, 0.0F}},
         {c, {offset + 4.0F, 2.0F, 0.0F}}, {d, {offset, 2.0F, 0.0F}}});
    AuthoredTopologyFace face;
    face.id = make_id<FaceTag>(face_index + 1U);
    face.vertices = {a, b, c, d};
    if (face_index != 2U) {
      face.uvs = {{0.1F, 0.2F}, {3.2F, -0.4F}, {1.7F, 2.8F}, {-0.6F, 1.3F}};
      face.texture_transform = {.scale = {-0.25F, 0.7F},
                                 .shift_uv = {0.3F, -0.9F},
                                 .rotation_degrees = 37.0F};
    }
    topology.faces.push_back(std::move(face));
  }
  topology.next_vertex_id = 13U;
  topology.next_face_id = 4U;
  return topology;
}

/** Compares stored corners exactly, including the absence of authored UVs. */
void expect_face_charts(const AuthoredTopology& source, const PolygonDocument& document) {
  const auto result = document.authored_topology();
  ASSERT_TRUE(result.ok()) << result.error_message();
  ASSERT_EQ(result.value().faces.size(), source.faces.size());
  for (const auto& face : result.value().faces) {
    const auto original = std::ranges::find(source.faces, face.id, &AuthoredTopologyFace::id);
    ASSERT_NE(original, source.faces.end());
    EXPECT_EQ(face.texture_transform, original->texture_transform);
    ASSERT_EQ(face.uvs.size(), original->uvs.size());
    for (std::size_t corner = 0; corner < face.uvs.size(); ++corner) {
      EXPECT_EQ(face.uvs[corner].x, original->uvs[corner].x);
      EXPECT_EQ(face.uvs[corner].y, original->uvs[corner].y);
    }
  }
}

/** Samples the actual compiled triangles for charts without quad correction. */
std::optional<Vec2> sample_triangle_uv(const MeshPayload& mesh, Vec3 point) {
  const auto dot = [](Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
  const auto subtract = [](Vec3 a, Vec3 b) { return Vec3{a.x - b.x, a.y - b.y, a.z - b.z}; };
  for (std::size_t index = 0; index + 2U < mesh.indices.size(); index += 3U) {
    const auto& a = mesh.vertices[mesh.indices[index]];
    const auto& b = mesh.vertices[mesh.indices[index + 1U]];
    const auto& c = mesh.vertices[mesh.indices[index + 2U]];
    const Vec3 ab = subtract(b.position, a.position);
    const Vec3 ac = subtract(c.position, a.position);
    const Vec3 ap = subtract(point, a.position);
    const float bb = dot(ab, ab);
    const float cc = dot(ac, ac);
    const float bc = dot(ab, ac);
    const float determinant = bb * cc - bc * bc;
    if (std::abs(determinant) < 0.000001F) {
      continue;
    }
    const float pb = dot(ap, ab);
    const float pc = dot(ap, ac);
    const float wb = (pb * cc - pc * bc) / determinant;
    const float wc = (pc * bb - pb * bc) / determinant;
    const float wa = 1.0F - wb - wc;
    if (wa >= -0.000001F && wb >= -0.000001F && wc >= -0.000001F) {
      return Vec2{a.uv0.x * wa + b.uv0.x * wb + c.uv0.x * wc,
                  a.uv0.y * wa + b.uv0.y * wb + c.uv0.y * wc};
    }
  }
  return std::nullopt;
}

/** Canonical triangle identities expose a changed interior interpolation. */
std::set<std::array<std::uint32_t, 3>> triangle_vertex_ids(const MeshPayload& mesh) {
  std::set<std::array<std::uint32_t, 3>> triangles;
  for (std::size_t index = 0; index + 2U < mesh.indices.size(); index += 3U) {
    std::array ids{mesh.vertices[mesh.indices[index]].source_vertex.index,
        mesh.vertices[mesh.indices[index + 1U]].source_vertex.index,
        mesh.vertices[mesh.indices[index + 2U]].source_vertex.index};
    std::ranges::sort(ids);
    triangles.insert(ids);
  }
  return triangles;
}

/** Tests winding changes against interior samples and per-corner identities. */
void expect_winding_change_preserves_chart(const AuthoredTopology& topology, bool mirror,
                                          Vec3 mirror_normal = {1.0F, 0.0F, 0.0F}) {
  auto document = PolygonDocument::from_authored_topology(topology).or_throw();
  ASSERT_TRUE(document.select_all_faces().ok());
  const auto before = document.compile_mesh().or_throw();
  const auto original_bytes = serialize_qdr_authored_topology(document).or_throw();
  const auto result = mirror ? document.reflect_across_plane({}, mirror_normal)
                             : document.flip_selected_face_normals();
  ASSERT_TRUE(result.ok()) << result.error_message();
  ASSERT_TRUE(result.value().changed);
  const auto after = document.compile_mesh().or_throw();
  EXPECT_EQ(triangle_vertex_ids(after), triangle_vertex_ids(before));
  const auto authored = document.authored_topology().or_throw();
  ASSERT_EQ(authored.faces.size(), topology.faces.size());
  for (std::size_t index = 0; index < authored.faces.size(); ++index) {
    const auto& face = authored.faces[index];
    const auto& original = topology.faces[index];
    EXPECT_EQ(face.id, original.id);
    if (face.vertices.size() == 4U) {
      EXPECT_EQ(face.vertices.front(), original.vertices.front());
    }
    EXPECT_EQ(face.material_slot, original.material_slot);
    EXPECT_EQ(face.texture_transform, original.texture_transform);
    for (std::size_t corner = 0; corner < face.vertices.size(); ++corner) {
      const auto found = std::ranges::find(original.vertices, face.vertices[corner]);
      ASSERT_NE(found, original.vertices.end());
      const auto source = static_cast<std::size_t>(found - original.vertices.begin());
      EXPECT_EQ(face.uvs[corner].x, original.uvs[source].x);
      EXPECT_EQ(face.uvs[corner].y, original.uvs[source].y);
    }
  }
  // Interior points distinguish a UV-preserving reverse from merely retaining
  // the same corner attributes on a different triangle diagonal.
  for (std::size_t index = 0; index + 2U < before.indices.size(); index += 3U) {
    Vec3 point{};
    Vec3 transformed_point{};
    for (std::size_t corner = 0; corner < 3U; ++corner) {
      const auto& vertex = before.vertices[before.indices[index + corner]];
      const auto position = vertex.position;
      point.x += position.x / 3.0F;
      point.y += position.y / 3.0F;
      point.z += position.z / 3.0F;
      const auto transformed = std::ranges::find(after.vertices, vertex.source_vertex,
                                                &MeshVertexPayload::source_vertex);
      ASSERT_NE(transformed, after.vertices.end());
      transformed_point.x += transformed->position.x / 3.0F;
      transformed_point.y += transformed->position.y / 3.0F;
      transformed_point.z += transformed->position.z / 3.0F;
    }
    const auto old_uv = sample_triangle_uv(before, point);
    const auto new_uv = sample_triangle_uv(after, transformed_point);
    ASSERT_TRUE(old_uv.has_value());
    ASSERT_TRUE(new_uv.has_value());
    EXPECT_NEAR(old_uv->x, new_uv->x, 0.00001F);
    EXPECT_NEAR(old_uv->y, new_uv->y, 0.00001F);
  }
  const auto restored = mirror ? document.reflect_across_plane({}, mirror_normal)
                               : document.flip_selected_face_normals();
  ASSERT_TRUE(restored.ok()) << restored.error_message();
  if (!mirror || mirror_normal.z == 0.0F || mirror_normal.x == 0.0F) {
    EXPECT_EQ(serialize_qdr_authored_topology(document).or_throw(), original_bytes);
  }
}

TEST(UvPreservationFoundationTests, ComponentPositionEditsRetainMixedDisconnectedCharts) {
  for (int operation = 0; operation < 3; ++operation) {
    SCOPED_TRACE(operation);
    const auto topology = mixed_chart_topology();
    auto document = PolygonDocument::from_authored_topology(topology).or_throw();
    const auto vertex = make_id<VertexTag>(1U);
    ASSERT_TRUE(document.select_vertex(vertex).ok());
    Result<OperationResult> result;
    if (operation == 0) {
      result = document.translate_selection({0.25F, 0.0F, 0.0F});
    } else if (operation == 1) {
      Transform3 transform;
      transform.origin = {0.25F, 0.0F, 0.0F};
      result = document.transform_selection(transform);
    } else {
      const std::array updates{VertexPositionUpdate{vertex, {0.25F, 0.0F, 0.0F}}};
      result = document.set_vertex_positions(updates);
    }
    ASSERT_TRUE(result.ok()) << result.error_message();
    ASSERT_TRUE(result.value().changed);
    expect_face_charts(topology, document);
  }
}

TEST(UvPreservationFoundationTests, IdentityPositionEditsPreservePublicationAndRevision) {
  auto document = PolygonDocument::from_authored_topology(mixed_chart_topology()).or_throw();
  const auto vertex = make_id<VertexTag>(1U);
  ASSERT_TRUE(document.select_vertex(vertex).ok());
  const auto before = document;
  const auto bytes = serialize_qdr_authored_topology(document).or_throw();
  const std::array updates{VertexPositionUpdate{vertex, {0.0F, 0.0F, 0.0F}}};
  for (const auto& result : {document.translate_selection({}),
                            document.transform_selection({}),
                            document.set_vertex_positions(updates)}) {
    ASSERT_TRUE(result.ok()) << result.error_message();
    EXPECT_FALSE(result.value().changed);
  }
  EXPECT_EQ(document.content_identity(), before.content_identity());
  EXPECT_EQ(document.content_revision(), before.content_revision());
  EXPECT_EQ(document.selection_revision(), before.selection_revision());
  EXPECT_EQ(serialize_qdr_authored_topology(document).or_throw(), bytes);
}

TEST(UvPreservationFoundationTests, TinyRepresentableMoveStillPublishesAndPreservesCharts) {
  const auto topology = mixed_chart_topology();
  auto document = PolygonDocument::from_authored_topology(topology).or_throw();
  ASSERT_TRUE(document.select_vertex(make_id<VertexTag>(1U)).ok());
  const auto before = document.content_identity();
  const auto result = document.translate_selection({0.0000001F, 0.0F, 0.0F});
  ASSERT_TRUE(result.ok()) << result.error_message();
  EXPECT_TRUE(result.value().changed);
  EXPECT_NE(document.content_identity(), before);
  expect_face_charts(topology, document);
}

TEST(UvPreservationFoundationTests, FailedAbsoluteUpdateLeavesAllGeometryAndChartsUntouched) {
  auto document = PolygonDocument::from_authored_topology(mixed_chart_topology()).or_throw();
  const auto before = document;
  const auto bytes = serialize_qdr_authored_topology(document).or_throw();
  const std::array updates{
      VertexPositionUpdate{make_id<VertexTag>(1U), {0.5F, 0.0F, 0.0F}},
      VertexPositionUpdate{make_id<VertexTag>(999U), {1.0F, 0.0F, 0.0F}}};
  EXPECT_FALSE(document.set_vertex_positions(updates).ok());
  EXPECT_EQ(document.content_identity(), before.content_identity());
  EXPECT_EQ(document.content_revision(), before.content_revision());
  EXPECT_EQ(serialize_qdr_authored_topology(document).or_throw(), bytes);
}

TEST(UvPreservationFoundationTests, EdgeSnapKeepsEditedAndUnrelatedCharts) {
  const auto topology = mixed_chart_topology();
  auto document = PolygonDocument::from_authored_topology(topology).or_throw();
  const auto moved = make_edge_key(make_id<VertexTag>(1U), make_id<VertexTag>(2U));
  const auto target = make_edge_key(make_id<VertexTag>(5U), make_id<VertexTag>(6U));
  ASSERT_TRUE(document.apply_selection({.kind = SelectionKind::Edge,
      .edges = {moved, target}, .has_active = true, .active_edge = target}).ok());
  const auto result = document.snap_selected_edges_to_active();
  ASSERT_TRUE(result.ok()) << result.error_message();
  ASSERT_TRUE(result.value().changed);
  expect_face_charts(topology, document);
}

TEST(UvPreservationFoundationTests, RadialAlignRetainsAuthoredAndProceduralCharts) {
  const auto topology = mixed_chart_topology();
  auto document = PolygonDocument::from_authored_topology(topology).or_throw();
  ASSERT_TRUE(document.select_face(make_id<FaceTag>(1U)).ok());
  const auto result = document.radial_align_selection();
  ASSERT_TRUE(result.ok()) << result.error_message();
  ASSERT_TRUE(result.value().changed);
  expect_face_charts(topology, document);
}

TEST(UvPreservationFoundationTests, VertexMergeRetainsSurvivingNonaffineCornerValues) {
  auto topology = mixed_chart_topology();
  auto document = PolygonDocument::from_authored_topology(topology).or_throw();
  const auto source = make_id<VertexTag>(2U);
  const auto target = make_id<VertexTag>(1U);
  ASSERT_TRUE(document.apply_selection({.kind = SelectionKind::Vertex,
      .vertices = {source, target}, .has_active = true, .active_vertex = target}).ok());
  const auto result = document.merge_selected_vertices_to_active();
  ASSERT_TRUE(result.ok()) << result.error_message();
  ASSERT_TRUE(result.value().changed);
  const auto after = document.authored_topology().or_throw();
  ASSERT_EQ(after.faces.size(), topology.faces.size());
  for (const auto& face : after.faces) {
    const auto original = std::ranges::find(topology.faces, face.id, &AuthoredTopologyFace::id);
    ASSERT_NE(original, topology.faces.end());
    EXPECT_EQ(face.texture_transform, original->texture_transform);
    if (original->uvs.empty()) {
      EXPECT_TRUE(face.uvs.empty());
      continue;
    }
    ASSERT_EQ(face.uvs.size(), face.vertices.size());
    for (std::size_t corner = 0; corner < face.vertices.size(); ++corner) {
      const auto original_corner = std::ranges::find(original->vertices, face.vertices[corner]);
      ASSERT_NE(original_corner, original->vertices.end());
      const auto index = static_cast<std::size_t>(original_corner - original->vertices.begin());
      EXPECT_EQ(face.uvs[corner].x, original->uvs[index].x);
      EXPECT_EQ(face.uvs[corner].y, original->uvs[index].y);
    }
  }
}

TEST(UvPreservationFoundationTests, NonadjacentVertexWeldKeepsSeparateSourceCornersAtTheSurvivor) {
  AuthoredTopology topology;
  const std::array positions{Vec3{0.0F, 0.0F, 0.0F}, Vec3{2.0F, 0.0F, 0.0F},
      Vec3{3.0F, 1.0F, 0.0F}, Vec3{2.0F, 2.0F, 0.0F},
      Vec3{0.0F, 2.0F, 0.0F}, Vec3{-1.0F, 1.0F, 0.0F}};
  AuthoredTopologyFace face;
  face.id = make_id<FaceTag>(1U);
  for (std::uint32_t index = 0; index < positions.size(); ++index) {
    const auto vertex = make_id<VertexTag>(index + 1U);
    topology.vertices.push_back({vertex, positions[index]});
    face.vertices.push_back(vertex);
    face.uvs.push_back({static_cast<float>(index * index), static_cast<float>(index + 10U)});
  }
  topology.faces.push_back(face);
  topology.next_vertex_id = 7U;
  topology.next_face_id = 2U;
  auto document = PolygonDocument::from_authored_topology(topology).or_throw();
  const auto target = make_id<VertexTag>(1U);
  ASSERT_TRUE(document.apply_selection({.kind = SelectionKind::Vertex,
      .vertices = {make_id<VertexTag>(4U), target},
      .has_active = true, .active_vertex = target}).ok());
  const auto result = document.merge_selected_vertices_to_active();
  ASSERT_TRUE(result.ok()) << result.error_message();
  ASSERT_TRUE(result.value().changed);
  const auto after = document.authored_topology().or_throw();
  ASSERT_EQ(after.faces.size(), 2U);
  std::vector<float> survivor_u;
  for (const auto& repaired : after.faces) {
    ASSERT_EQ(repaired.vertices.size(), repaired.uvs.size());
    for (std::size_t corner = 0; corner < repaired.vertices.size(); ++corner) {
      if (repaired.vertices[corner] == target) {
        survivor_u.push_back(repaired.uvs[corner].x);
      } else {
        const std::size_t original = repaired.vertices[corner].index - 1U;
        EXPECT_EQ(repaired.uvs[corner].x, face.uvs[original].x);
        EXPECT_EQ(repaired.uvs[corner].y, face.uvs[original].y);
      }
    }
  }
  std::ranges::sort(survivor_u);
  EXPECT_EQ(survivor_u, (std::vector<float>{0.0F, 9.0F}));
}

TEST(UvPreservationFoundationTests, NormalFlipAndReflectionRetainFoldedQuadInteriorUvs) {
  AuthoredTopology topology;
  topology.vertices = {{make_id<VertexTag>(1U), {0.0F, 0.0F, 0.0F}},
      {make_id<VertexTag>(2U), {1.0F, 0.0F, 0.0F}},
      {make_id<VertexTag>(3U), {1.0F, 1.0F, 0.0F}},
      {make_id<VertexTag>(4U), {0.0F, 1.0F, 0.0F}}};
  topology.faces = {{.id = make_id<FaceTag>(1U),
      .vertices = {make_id<VertexTag>(1U), make_id<VertexTag>(2U),
                   make_id<VertexTag>(3U), make_id<VertexTag>(4U)},
      .uvs = {{0.0F, 0.0F}, {0.6F, 0.0F}, {0.0F, 0.8F}, {0.6F, 0.8F}},
      .material_slot = 7U}};
  topology.next_vertex_id = 5U;
  topology.next_face_id = 2U;
  for (const bool mirror : {false, true}) {
    SCOPED_TRACE(mirror);
    expect_winding_change_preserves_chart(topology, mirror);
  }
}

TEST(UvPreservationFoundationTests, NormalFlipAndReflectionRetainNonaffineNgonInteriorUvs) {
  for (const bool concave : {false, true}) {
    SCOPED_TRACE(concave);
    AuthoredTopology topology;
    const std::array points{Vec3{0.0F, 0.0F, 0.0F}, Vec3{3.0F, 0.0F, 0.0F},
        Vec3{3.0F, 2.0F, 0.0F}, Vec3{1.0F, concave ? 1.0F : 3.0F, 0.0F},
        Vec3{0.0F, 2.0F, 0.0F}};
    AuthoredTopologyFace face;
    face.id = make_id<FaceTag>(1U);
    face.material_slot = 3U;
    for (std::uint32_t index = 0; index < points.size(); ++index) {
      const auto vertex = make_id<VertexTag>(index + 1U);
      topology.vertices.push_back({vertex, points[index]});
      face.vertices.push_back(vertex);
      face.uvs.push_back({static_cast<float>(index * index) * 0.1F,
                         static_cast<float>((index * 3U) % 4U) * 0.2F});
    }
    topology.faces.push_back(face);
    topology.next_vertex_id = 6U;
    topology.next_face_id = 2U;
    for (const bool mirror : {false, true}) {
      SCOPED_TRACE(mirror);
      expect_winding_change_preserves_chart(topology, mirror);
    }
    expect_winding_change_preserves_chart(topology, true, {0.0F, 0.0F, 1.0F});
    expect_winding_change_preserves_chart(topology, true, {1.0F, 0.0F, 1.0F});
  }
}

TEST(UvPreservationFoundationTests, ReflectionKeepsCollapsedAuthoredNgonCorners) {
  AuthoredTopology topology;
  AuthoredTopologyFace face;
  face.id = make_id<FaceTag>(1U);
  for (std::uint32_t index = 0; index < 5U; ++index) {
    const auto id = make_id<VertexTag>(index + 1U);
    topology.vertices.push_back({id, {static_cast<float>(index), 0.0F, 0.0F}});
    face.vertices.push_back(id);
    face.uvs.push_back({static_cast<float>(index * index), static_cast<float>(index % 2U)});
  }
  topology.faces.push_back(face);
  topology.next_vertex_id = 6U;
  topology.next_face_id = 2U;
  auto document = PolygonDocument::from_authored_topology(topology).or_throw();
  const auto reflected = document.reflect_across_plane({}, {1.0F, 0.0F, 0.0F});
  ASSERT_TRUE(reflected.ok()) << reflected.error_message();
  ASSERT_TRUE(reflected.value().changed);
  const auto result = document.authored_topology().or_throw();
  ASSERT_EQ(result.faces.size(), 1U);
  for (std::size_t corner = 0; corner < result.faces.front().vertices.size(); ++corner) {
    const auto id = result.faces.front().vertices[corner];
    const auto original = std::ranges::find(face.vertices, id);
    ASSERT_NE(original, face.vertices.end());
    const auto index = static_cast<std::size_t>(original - face.vertices.begin());
    EXPECT_FLOAT_EQ(result.faces.front().uvs[corner].x, face.uvs[index].x);
    EXPECT_FLOAT_EQ(result.faces.front().uvs[corner].y, face.uvs[index].y);
  }
  for (std::size_t index = 0; index < result.vertices.size(); ++index) {
    EXPECT_FLOAT_EQ(result.vertices[index].position.x, -topology.vertices[index].position.x);
  }
}

TEST(UvPreservationFoundationTests, ReflectionRejectsNonfiniteResultWithoutChangingGeometryOrUvs) {
  auto document = PolygonDocument::from_authored_topology(mixed_chart_topology()).or_throw();
  const auto original = serialize_qdr_authored_topology(document).or_throw();
  const auto result = document.reflect_across_plane(
      {std::numeric_limits<float>::max(), 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F});
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(serialize_qdr_authored_topology(document).or_throw(), original);
}

} // namespace

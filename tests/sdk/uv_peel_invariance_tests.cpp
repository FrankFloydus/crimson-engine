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
#include <utility>
#include <vector>

namespace {

using namespace quader::modeling;

/// Provides unequal longitudinal and transverse lengths around a closed sleeve.
AuthoredTopology tapered_sleeve() {
  constexpr std::array<float, 3> kLongitudinal{0.0F, 2.0F, 5.0F};
  constexpr std::array<float, 3> kRadius{1.0F, 1.5F, 2.0F};
  constexpr std::array<Vec2, 4> kProfile{
      Vec2{-1.0F, -2.0F}, Vec2{1.0F, -2.0F},
      Vec2{1.0F, 2.0F}, Vec2{-1.0F, 2.0F}};
  AuthoredTopology topology;
  const auto vertex_id = [](std::size_t rail, std::size_t station) {
    return make_id<VertexTag>(static_cast<std::uint32_t>(rail * 3U + station + 1U));
  };
  for (std::size_t rail = 0U; rail < kProfile.size(); ++rail) {
    for (std::size_t station = 0U; station < kLongitudinal.size(); ++station) {
      topology.vertices.push_back({vertex_id(rail, station),
          {kLongitudinal[station], kRadius[station] * kProfile[rail].x,
           kRadius[station] * kProfile[rail].y}});
    }
    for (std::size_t station = 0U; station + 1U < kLongitudinal.size(); ++station) {
      const std::size_t next_rail = (rail + 1U) % kProfile.size();
      topology.faces.push_back({
          .id = make_id<FaceTag>(static_cast<std::uint32_t>(rail * 2U + station + 1U)),
          .vertices = {vertex_id(rail, station), vertex_id(rail, station + 1U),
                       vertex_id(next_rail, station + 1U), vertex_id(next_rail, station)},
          .uvs = {{0.15F, 0.3F}, {1.2F, 0.1F}, {1.5F, 1.4F}, {-0.1F, 0.9F}},
          .material_slot = static_cast<std::uint32_t>(rail + 1U)});
    }
  }
  topology.next_vertex_id = 13U;
  topology.next_face_id = 9U;
  return topology;
}

/// Selects either a longitudinal rail or the closed central circumference.
std::vector<EdgeKey> sleeve_guide(bool closed) {
  const std::vector<std::uint32_t> indices = closed
      ? std::vector<std::uint32_t>{2U, 5U, 8U, 11U, 2U}
      : std::vector<std::uint32_t>{1U, 2U, 3U};
  std::vector<EdgeKey> edges;
  for (std::size_t index = 1U; index < indices.size(); ++index) {
    edges.push_back(make_edge_key(make_id<VertexTag>(indices[index - 1U]),
                                  make_id<VertexTag>(indices[index])));
  }
  return edges;
}

/// Compares authored corner identities independently of face/corner storage order.
void expect_corner_relation(const AuthoredTopology &reference,
                            const AuthoredTopology &candidate,
                            float gain, Vec2 offset) {
  ASSERT_EQ(reference.faces.size(), candidate.faces.size());
  for (const auto &face : reference.faces) {
    const auto found = std::ranges::find(candidate.faces, face.id, &AuthoredTopologyFace::id);
    ASSERT_NE(found, candidate.faces.end());
    EXPECT_EQ(found->material_slot, face.material_slot);
    ASSERT_EQ(found->uvs.size(), face.uvs.size());
    for (std::size_t corner = 0U; corner < face.vertices.size(); ++corner) {
      const auto vertex = std::ranges::find(found->vertices, face.vertices[corner]);
      ASSERT_NE(vertex, found->vertices.end());
      const auto destination = static_cast<std::size_t>(vertex - found->vertices.begin());
      EXPECT_NEAR(found->uvs[destination].x,
                  offset.x + gain * (face.uvs[corner].x - offset.x), 0.0002F);
      EXPECT_NEAR(found->uvs[destination].y,
                  offset.y + gain * (face.uvs[corner].y - offset.y), 0.0002F);
    }
  }
}

TEST(UvPeelInvarianceTests, CyclicProfilesPreserveCornerSeamsAcrossStoragePermutations) {
  const auto topology = tapered_sleeve();
  auto permuted = topology;
  std::ranges::reverse(permuted.vertices);
  std::ranges::reverse(permuted.faces);
  for (auto &face : permuted.faces) {
    const auto rotation = face.id.index % 4U;
    std::rotate(face.vertices.begin(), face.vertices.begin() + rotation, face.vertices.end());
    std::rotate(face.uvs.begin(), face.uvs.begin() + rotation, face.uvs.end());
  }
  const auto original = PolygonDocument::from_authored_topology(topology).or_throw();
  const auto reordered = PolygonDocument::from_authored_topology(permuted).or_throw();
  for (const bool closed : {false, true}) {
    for (const bool world_space : {false, true}) {
      for (const auto axis : {UvPeelAxis::U, UvPeelAxis::V}) {
        SCOPED_TRACE(::testing::Message() << closed << ", " << world_space << ", "
                                        << static_cast<int>(axis));
        const UvPeelOptions options{.axis = axis, .world_space = world_space,
            .u_repeat = 1.5, .v_repeat = 0.75, .u_offset = 0.125, .v_offset = -0.25};
        auto edges = sleeve_guide(closed);
        const auto active = edges.front();
        const auto baseline = prepare_uv_peel(std::array{UvPeelTarget{
            .key = 1U, .document = &original, .edges = edges, .active_edge = active}}, options);
        ASSERT_TRUE(baseline.ok()) << baseline.error_message();
        std::ranges::reverse(edges);
        const auto candidate = prepare_uv_peel(std::array{UvPeelTarget{
            .key = 1U, .document = &reordered, .edges = edges, .active_edge = active}}, options);
        ASSERT_TRUE(candidate.ok()) << candidate.error_message();
        const auto &baseline_document = baseline.value().documents.front().document;
        const auto &candidate_document = candidate.value().documents.front().document;
        EXPECT_TRUE(candidate_document.has_same_geometry(reordered));
        EXPECT_TRUE(candidate_document.shares_selection_with(reordered));
        expect_corner_relation(baseline_document.authored_topology().or_throw(),
                               candidate_document.authored_topology().or_throw(), 1.0F, {});
        const auto repeated = prepare_uv_peel(std::array{UvPeelTarget{
            .key = 1U, .document = &candidate_document, .edges = edges,
            .active_edge = active}}, options);
        ASSERT_TRUE(repeated.ok()) << repeated.error_message();
        EXPECT_FALSE(repeated.value().changed);
        EXPECT_EQ(serialize_qdr_authored_topology(candidate_document).or_throw(),
                  serialize_qdr_authored_topology(
                      repeated.value().documents.front().document).or_throw());
        if (!closed) {
          std::vector<Vec2> seam_corners;
          const auto candidate_topology = candidate_document.authored_topology().or_throw();
          for (const auto &face : candidate_topology.faces) {
            for (std::size_t corner = 0U; corner < face.vertices.size(); ++corner) {
              if (face.vertices[corner] == make_id<VertexTag>(1U)) {
                seam_corners.push_back(face.uvs[corner]);
              }
            }
          }
          ASSERT_EQ(seam_corners.size(), 2U);
          const float separation = axis == UvPeelAxis::U
              ? seam_corners[0].y - seam_corners[1].y
              : seam_corners[0].x - seam_corners[1].x;
          EXPECT_GT(std::abs(separation), 0.25F);
        }
      }
    }
  }
}

TEST(UvPeelInvarianceTests, UniformMetricScalingAffectsWorldDensityButPreservesFit) {
  const auto document = PolygonDocument::from_authored_topology(tapered_sleeve()).or_throw();
  const Transform3 scaled{.x_axis = {0.0F, 3.0F, 0.0F},
      .y_axis = {0.0F, 0.0F, -3.0F}, .z_axis = {3.0F, 0.0F, 0.0F},
      .origin = {1.0e12F, -1.0e12F, 1.0e12F}};
  const auto before = serialize_qdr_authored_topology(document).or_throw();
  for (const bool closed : {false, true}) {
    const auto edges = sleeve_guide(closed);
    for (const bool world_space : {false, true}) {
      for (const auto axis : {UvPeelAxis::U, UvPeelAxis::V}) {
        SCOPED_TRACE(::testing::Message() << closed << ", " << world_space << ", "
                                        << static_cast<int>(axis));
        const UvPeelOptions options{.axis = axis, .world_space = world_space,
            .u_repeat = 0.5, .v_repeat = 2.0, .u_offset = -0.25, .v_offset = 0.125};
        const auto baseline = prepare_uv_peel(std::array{UvPeelTarget{
            .key = 1U, .document = &document, .edges = edges,
            .active_edge = edges.front()}}, options);
        ASSERT_TRUE(baseline.ok()) << baseline.error_message();
        const auto candidate = prepare_uv_peel(std::array{UvPeelTarget{
            .key = 1U, .document = &document, .edges = edges,
            .active_edge = edges.front(), .world_from_local = scaled}}, options);
        ASSERT_TRUE(candidate.ok()) << candidate.error_message();
        expect_corner_relation(
            baseline.value().documents.front().document.authored_topology().or_throw(),
            candidate.value().documents.front().document.authored_topology().or_throw(),
            world_space ? 3.0F : 1.0F, {-0.25F, 0.125F});
      }
    }
  }
  EXPECT_EQ(serialize_qdr_authored_topology(document).or_throw(), before);
}

} // namespace

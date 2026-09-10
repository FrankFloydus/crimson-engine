////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/modeling.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <utility>
#include <vector>

namespace quader::modeling {
namespace {

[[nodiscard]] Vec3 subtract(Vec3 left, Vec3 right) {
  return {left.x - right.x, left.y - right.y, left.z - right.z};
}

[[nodiscard]] Vec3 add(Vec3 left, Vec3 right) {
  return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] Vec3 scale(Vec3 value, double scalar) {
  return {static_cast<float>(value.x * scalar),
          static_cast<float>(value.y * scalar),
          static_cast<float>(value.z * scalar)};
}

[[nodiscard]] Vec3 cross_product(Vec3 left, Vec3 right) {
  return {left.y * right.z - left.z * right.y,
          left.z * right.x - left.x * right.z,
          left.x * right.y - left.y * right.x};
}

[[nodiscard]] double dot_product(Vec3 left, Vec3 right) {
  return static_cast<double>(left.x) * right.x +
         static_cast<double>(left.y) * right.y +
         static_cast<double>(left.z) * right.z;
}

[[nodiscard]] double vector_length(Vec3 value) {
  return std::sqrt(dot_product(value, value));
}

[[nodiscard]] Vec3 normalized_vector(Vec3 value) {
  return scale(value, 1.0 / vector_length(value));
}

[[nodiscard]] Vec3 rotate_vector(Vec3 value, Vec3 axis, double radians) {
  const double cosine = std::cos(radians);
  const double sine = std::sin(radians);
  return add(add(scale(value, cosine), scale(cross_product(axis, value), sine)),
             scale(axis, dot_product(axis, value) * (1.0 - cosine)));
}

[[nodiscard]] Vec3 transport_vector(Vec3 normal, Vec3 from_tangent,
                                    Vec3 to_tangent) {
  const double cosine =
      std::clamp(dot_product(from_tangent, to_tangent), -1.0, 1.0);
  const Vec3 axis = cross_product(from_tangent, to_tangent);
  const double sine = vector_length(axis);
  Vec3 transported = normal;
  if (sine > 1.0e-8) {
    transported = rotate_vector(normal, scale(axis, 1.0 / sine),
                                std::atan2(sine, cosine));
  }
  return normalized_vector(subtract(
      transported, scale(to_tangent, dot_product(transported, to_tangent))));
}

void expect_vec_near(Vec3 actual, Vec3 expected, double tolerance = 1.0e-5) {
  EXPECT_NEAR(actual.x, expected.x, tolerance);
  EXPECT_NEAR(actual.y, expected.y, tolerance);
  EXPECT_NEAR(actual.z, expected.z, tolerance);
}

[[nodiscard]] bool same_position(Vec3 left, Vec3 right) {
  return left.x == right.x && left.y == right.y && left.z == right.z;
}

[[nodiscard]] std::size_t unique_ring_count(
    const CurveDocument &document, CurveSamplingOptions sampling) {
  const auto sampled = document.sample(sampling);
  EXPECT_TRUE(sampled.ok()) << sampled.error_message();
  if (!sampled.ok()) {
    return 0U;
  }
  std::vector<Vec3> positions;
  for (const CurveSample &sample : sampled.value()) {
    if (positions.empty() || !same_position(positions.back(), sample.position)) {
      positions.push_back(sample.position);
    }
  }
  if (document.closed() && positions.size() > 1U &&
      same_position(positions.front(), positions.back())) {
    positions.pop_back();
  }
  return positions.size();
}

[[nodiscard]] std::map<std::pair<std::uint32_t, std::uint32_t>, std::size_t>
edge_use_counts(const AuthoredTopology &topology) {
  std::map<std::pair<std::uint32_t, std::uint32_t>, std::size_t> counts;
  for (const AuthoredTopologyFace &face : topology.faces) {
    for (std::size_t corner = 0U; corner < face.vertices.size(); ++corner) {
      std::uint32_t first = face.vertices[corner].index;
      std::uint32_t second =
          face.vertices[(corner + 1U) % face.vertices.size()].index;
      if (second < first) {
        std::swap(first, second);
      }
      ++counts[{first, second}];
    }
  }
  return counts;
}

void expect_payload_equal(const MeshPayload &actual,
                          const MeshPayload &expected) {
  ASSERT_EQ(actual.vertices.size(), expected.vertices.size());
  EXPECT_EQ(actual.indices, expected.indices);
  ASSERT_EQ(actual.primitives.size(), expected.primitives.size());
  EXPECT_EQ(actual.content_revision, expected.content_revision);
  for (std::size_t index = 0U; index < actual.vertices.size(); ++index) {
    const MeshVertexPayload &left = actual.vertices[index];
    const MeshVertexPayload &right = expected.vertices[index];
    EXPECT_FLOAT_EQ(left.position.x, right.position.x);
    EXPECT_FLOAT_EQ(left.position.y, right.position.y);
    EXPECT_FLOAT_EQ(left.position.z, right.position.z);
    EXPECT_FLOAT_EQ(left.normal.x, right.normal.x);
    EXPECT_FLOAT_EQ(left.normal.y, right.normal.y);
    EXPECT_FLOAT_EQ(left.normal.z, right.normal.z);
    EXPECT_FLOAT_EQ(left.uv0.x, right.uv0.x);
    EXPECT_FLOAT_EQ(left.uv0.y, right.uv0.y);
    EXPECT_EQ(left.color, right.color);
    EXPECT_EQ(left.source_vertex, right.source_vertex);
    EXPECT_EQ(left.uv_carrier, right.uv_carrier);
    EXPECT_FLOAT_EQ(left.uv_geometric_normal.x,
                    right.uv_geometric_normal.x);
    EXPECT_FLOAT_EQ(left.uv_geometric_normal.y,
                    right.uv_geometric_normal.y);
    EXPECT_FLOAT_EQ(left.uv_geometric_normal.z,
                    right.uv_geometric_normal.z);
  }
  for (std::size_t index = 0U; index < actual.primitives.size(); ++index) {
    EXPECT_EQ(actual.primitives[index].index_offset,
              expected.primitives[index].index_offset);
    EXPECT_EQ(actual.primitives[index].index_count,
              expected.primitives[index].index_count);
    EXPECT_EQ(actual.primitives[index].material,
              expected.primitives[index].material);
  }
}

[[nodiscard]] CurveDocument straight_curve() {
  const std::array points{
      CurvePointSpec{.position = {0.0F, 0.0F, 0.0F},
                     .outgoing_handle = {2.0F / 3.0F, 0.0F, 0.0F}},
      CurvePointSpec{.position = {2.0F, 0.0F, 0.0F},
                     .incoming_handle = {-2.0F / 3.0F, 0.0F, 0.0F}},
  };
  return CurveDocument::make_bezier(points).or_throw();
}

[[nodiscard]] CurveDocument nonplanar_loop() {
  const std::array positions{
      Vec3{2.0F, 0.0F, 0.0F}, Vec3{1.0F, 2.0F, 1.5F},
      Vec3{-1.0F, 1.0F, 0.2F}, Vec3{-2.0F, -1.0F, 2.0F},
      Vec3{0.0F, -2.0F, -1.0F},
  };
  std::array<CurvePointSpec, 5U> points;
  for (std::size_t index = 0U; index < points.size(); ++index) {
    const Vec3 previous =
        positions[index == 0U ? positions.size() - 1U : index - 1U];
    const Vec3 next = positions[(index + 1U) % positions.size()];
    const Vec3 handle{(next.x - previous.x) / 6.0F,
                      (next.y - previous.y) / 6.0F,
                      (next.z - previous.z) / 6.0F};
    points[index] = {
        .position = positions[index],
        .incoming_handle = {-handle.x, -handle.y, -handle.z},
        .outgoing_handle = handle,
    };
  }
  return CurveDocument::make_bezier(points, true).or_throw();
}

TEST(ModelingCurveTubeTests,
     StraightOpenTubeHasExactUncappedBuffersNormalsUvsAndWinding) {
  const CurveDocument document = straight_curve();
  const auto built = tessellate_curve_tube(
      document, {.radius = 0.5, .radial_resolution = 4U,
                 .sampling = {.tolerance = 0.001}});
  ASSERT_TRUE(built.ok()) << built.error_message();
  const MeshPayload &mesh = built.value();
  ASSERT_EQ(mesh.vertices.size(), 10U);
  ASSERT_EQ(mesh.indices.size(), 24U);
  ASSERT_EQ(mesh.primitives.size(), 1U);
  EXPECT_EQ(mesh.primitives.front().index_offset, 0U);
  EXPECT_EQ(mesh.primitives.front().index_count, 24U);
  EXPECT_FALSE(mesh.primitives.front().material.valid());
  EXPECT_EQ(mesh.content_revision, document.content_revision());

  const std::array expected_ring{
      Vec3{0.0F, 0.5F, 0.0F}, Vec3{0.0F, 0.0F, 0.5F},
      Vec3{0.0F, -0.5F, 0.0F}, Vec3{0.0F, 0.0F, -0.5F},
      Vec3{0.0F, 0.5F, 0.0F},
  };
  for (std::size_t radial = 0U; radial < expected_ring.size(); ++radial) {
    expect_vec_near(mesh.vertices[radial].position, expected_ring[radial]);
    EXPECT_NEAR(vector_length(mesh.vertices[radial].normal), 1.0, 1.0e-6);
    EXPECT_FLOAT_EQ(mesh.vertices[radial].uv0.x, 0.0F);
    EXPECT_FLOAT_EQ(mesh.vertices[radial].uv0.y,
                    static_cast<float>(radial) / 4.0F);
    expect_vec_near(mesh.vertices[radial].normal,
                    mesh.vertices[radial].uv_geometric_normal);
    Vec3 translated = mesh.vertices[radial + 5U].position;
    translated.x -= 2.0F;
    expect_vec_near(translated, expected_ring[radial]);
    EXPECT_FLOAT_EQ(mesh.vertices[radial + 5U].uv0.x, 1.0F);
  }

  for (std::size_t triangle = 0U; triangle < mesh.indices.size();
       triangle += 3U) {
    const MeshVertexPayload &first = mesh.vertices[mesh.indices[triangle]];
    const MeshVertexPayload &second =
        mesh.vertices[mesh.indices[triangle + 1U]];
    const MeshVertexPayload &third =
        mesh.vertices[mesh.indices[triangle + 2U]];
    const Vec3 geometric = cross_product(
        subtract(second.position, first.position),
        subtract(third.position, first.position));
    EXPECT_GT(dot_product(geometric, first.normal), 0.0);
  }
}

TEST(ModelingCurveTubeTests,
     EditableOpenTubeHasWeldedRadialSeamSmoothUncappedQuads) {
  const CurveTubeOptions options{
      .radius = 0.5,
      .radial_resolution = 4U,
      .sampling = {.tolerance = 0.001},
  };
  const auto built = build_curve_tube_mesh(straight_curve(), options);
  ASSERT_TRUE(built.ok()) << built.error_message();
  EXPECT_EQ(built.value().document.vertex_count(), 8U);
  EXPECT_EQ(built.value().document.face_count(), 4U);
  const auto validation = built.value().document.validate();
  ASSERT_TRUE(validation.ok()) << validation.error_message();
  EXPECT_TRUE(validation.value().success);

  const auto authored = built.value().document.authored_topology();
  ASSERT_TRUE(authored.ok()) << authored.error_message();
  const AuthoredTopology &topology = authored.value();
  EXPECT_EQ(topology.next_vertex_id, 9U);
  EXPECT_EQ(topology.next_face_id, 5U);
  EXPECT_TRUE(topology.hard_normal_edges.empty());
  ASSERT_EQ(topology.faces.size(), 4U);
  for (const AuthoredTopologyFace &face : topology.faces) {
    EXPECT_EQ(face.vertices.size(), 4U);
    EXPECT_EQ(face.uvs.size(), 4U);
    EXPECT_EQ(face.material_slot, 0U);
    EXPECT_EQ(face.shading, AuthoredSurfaceShading::Authored);
  }

  // The radial UV seam uses the same authored vertices at V=0 and V=1.
  const AuthoredTopologyFace &seam = topology.faces.back();
  ASSERT_EQ(seam.vertices.size(), 4U);
  EXPECT_EQ(seam.vertices[0], make_id<VertexTag>(4U));
  EXPECT_EQ(seam.vertices[1], make_id<VertexTag>(1U));
  EXPECT_EQ(seam.vertices[2], make_id<VertexTag>(5U));
  EXPECT_EQ(seam.vertices[3], make_id<VertexTag>(8U));
  ASSERT_EQ(seam.uvs.size(), 4U);
  EXPECT_FLOAT_EQ(seam.uvs[0].y, 0.75F);
  EXPECT_FLOAT_EQ(seam.uvs[1].y, 1.0F);
  EXPECT_FLOAT_EQ(seam.uvs[2].y, 1.0F);
  EXPECT_FLOAT_EQ(seam.uvs[3].y, 0.75F);

  const auto edge_counts = edge_use_counts(topology);
  EXPECT_EQ(std::count_if(edge_counts.begin(), edge_counts.end(),
                          [](const auto &entry) {
                            return entry.second == 1U;
                          }),
            8);
  EXPECT_TRUE(std::all_of(edge_counts.begin(), edge_counts.end(),
                          [](const auto &entry) {
                            return entry.second == 1U || entry.second == 2U;
                          }));

  const Vec3 &first = topology.vertices[0U].position;
  const Vec3 &second = topology.vertices[1U].position;
  const Vec3 &third = topology.vertices[5U].position;
  const Vec3 outward = subtract(first, {first.x, 0.0F, 0.0F});
  EXPECT_GT(dot_product(cross_product(subtract(second, first),
                                      subtract(third, first)),
                        outward),
            0.0);
}

TEST(ModelingCurveTubeTests,
     ClosedNonplanarCurvePublishesContinuousUvSeamAndCorrectedFrames) {
  const CurveDocument document = nonplanar_loop();
  const CurveTubeOptions options{
      .radius = 0.15,
      .radial_resolution = 8U,
      .sampling = {.tolerance = 0.01},
  };
  const auto built = tessellate_curve_tube(document, options);
  ASSERT_TRUE(built.ok()) << built.error_message();
  const MeshPayload &mesh = built.value();
  const std::size_t unique_rings =
      unique_ring_count(document, options.sampling);
  ASSERT_GT(unique_rings, document.point_count());
  const std::size_t stride = options.radial_resolution + 1U;
  const std::size_t published_rings = unique_rings + 1U;
  EXPECT_EQ(mesh.vertices.size(), published_rings * stride);
  EXPECT_EQ(mesh.indices.size(),
            unique_rings * options.radial_resolution * 6U);

  for (std::size_t ring = 0U; ring < published_rings; ++ring) {
    const MeshVertexPayload &radial_start = mesh.vertices[ring * stride];
    const MeshVertexPayload &radial_seam =
        mesh.vertices[ring * stride + options.radial_resolution];
    expect_vec_near(radial_start.position, radial_seam.position, 0.0);
    expect_vec_near(radial_start.normal, radial_seam.normal, 0.0);
    EXPECT_FLOAT_EQ(radial_start.uv0.y, 0.0F);
    EXPECT_FLOAT_EQ(radial_seam.uv0.y, 1.0F);
    EXPECT_NEAR(vector_length(radial_start.normal), 1.0, 2.0e-6);
    if (ring > 0U) {
      EXPECT_GE(radial_start.uv0.x,
                mesh.vertices[(ring - 1U) * stride].uv0.x);
    }
  }

  const std::size_t closing_offset =
      (unique_rings - 1U) * options.radial_resolution * 6U;
  const std::size_t last_unique_ring_offset = (unique_rings - 1U) * stride;
  const std::size_t longitudinal_seam_offset = unique_rings * stride;
  for (std::uint32_t radial = 0U;
       radial <= options.radial_resolution; ++radial) {
    const MeshVertexPayload &start = mesh.vertices[radial];
    const MeshVertexPayload &seam =
        mesh.vertices[longitudinal_seam_offset + radial];
    expect_vec_near(start.position, seam.position, 0.0);
    expect_vec_near(start.normal, seam.normal, 0.0);
    expect_vec_near(start.uv_geometric_normal, seam.uv_geometric_normal, 0.0);
    EXPECT_FLOAT_EQ(start.uv0.x, 0.0F);
    EXPECT_FLOAT_EQ(seam.uv0.x, 1.0F);
    EXPECT_FLOAT_EQ(start.uv0.y, seam.uv0.y);
  }

  // Every closing-strip triangle now references the duplicated U=1 ring.
  // No corner may wrap to the authored start ring's U=0.
  for (std::uint32_t radial = 0U;
       radial < options.radial_resolution; ++radial) {
    const std::size_t offset = closing_offset + radial * 6U;
    const auto last = static_cast<std::uint32_t>(
        last_unique_ring_offset + radial);
    const auto seam = static_cast<std::uint32_t>(
        longitudinal_seam_offset + radial);
    const std::array expected{
        last, last + 1U, seam, last + 1U, seam + 1U, seam,
    };
    for (std::size_t corner = 0U; corner < expected.size(); ++corner) {
      ASSERT_LT(offset + corner, mesh.indices.size());
      const std::uint32_t vertex_index = mesh.indices[offset + corner];
      EXPECT_EQ(vertex_index, expected[corner]);
      EXPECT_GT(mesh.vertices[vertex_index].uv0.x, 0.0F);
      EXPECT_LE(mesh.vertices[vertex_index].uv0.x, 1.0F);
    }
  }

  // Residual twist is distributed around the loop, including the closing
  // edge, so corresponding radial normals never acquire a seam phase flip.
  for (std::size_t ring = 0U; ring < unique_rings; ++ring) {
    const std::size_t next = (ring + 1U) % unique_rings;
    const Vec3 current_normal = mesh.vertices[ring * stride].normal;
    const Vec3 next_normal = mesh.vertices[next * stride].normal;
    EXPECT_GT(dot_product(current_normal, next_normal), 0.0);
  }

  // Recover each published frame from its zero- and quarter-turn radial
  // normals. Parallel transport followed by the next frame's roll must have a
  // constant twist per unit length, including the closing edge. This fails if
  // the loop's nonzero residual holonomy is left as a seam jump.
  std::vector<Vec3> centers(unique_rings);
  std::vector<Vec3> tangents(unique_rings);
  for (std::size_t ring = 0U; ring < unique_rings; ++ring) {
    for (std::uint32_t radial = 0U;
         radial < options.radial_resolution; ++radial) {
      centers[ring] = add(
          centers[ring], mesh.vertices[ring * stride + radial].position);
    }
    centers[ring] = scale(
        centers[ring], 1.0 / options.radial_resolution);
    const Vec3 normal = mesh.vertices[ring * stride].normal;
    const Vec3 binormal =
        mesh.vertices[ring * stride + options.radial_resolution / 4U].normal;
    tangents[ring] = normalized_vector(cross_product(normal, binormal));
  }
  double minimum_twist_rate = std::numeric_limits<double>::infinity();
  double maximum_twist_rate = -std::numeric_limits<double>::infinity();
  for (std::size_t ring = 0U; ring < unique_rings; ++ring) {
    const std::size_t next = (ring + 1U) % unique_rings;
    const Vec3 normal = mesh.vertices[ring * stride].normal;
    const Vec3 next_normal = mesh.vertices[next * stride].normal;
    const Vec3 transported =
        transport_vector(normal, tangents[ring], tangents[next]);
    const double twist = std::atan2(
        dot_product(tangents[next], cross_product(transported, next_normal)),
        std::clamp(dot_product(transported, next_normal), -1.0, 1.0));
    const double edge_length =
        vector_length(subtract(centers[next], centers[ring]));
    ASSERT_GT(edge_length, 0.0);
    const double rate = twist / edge_length;
    minimum_twist_rate = std::min(minimum_twist_rate, rate);
    maximum_twist_rate = std::max(maximum_twist_rate, rate);
  }
  EXPECT_GT(std::max(std::abs(minimum_twist_rate),
                     std::abs(maximum_twist_rate)),
            1.0e-4);
  EXPECT_NEAR(maximum_twist_rate, minimum_twist_rate, 2.0e-4);

  for (std::size_t triangle = 0U; triangle < mesh.indices.size();
       triangle += 3U) {
    const MeshVertexPayload &first = mesh.vertices[mesh.indices[triangle]];
    const MeshVertexPayload &second =
        mesh.vertices[mesh.indices[triangle + 1U]];
    const MeshVertexPayload &third =
        mesh.vertices[mesh.indices[triangle + 2U]];
    const Vec3 geometric = cross_product(
        subtract(second.position, first.position),
        subtract(third.position, first.position));
    const Vec3 smooth_normal{
        first.normal.x + second.normal.x + third.normal.x,
        first.normal.y + second.normal.y + third.normal.y,
        first.normal.z + second.normal.z + third.normal.z,
    };
    EXPECT_GT(dot_product(geometric, smooth_normal), 0.0);
  }
}

TEST(ModelingCurveTubeTests,
     EditableClosedTubeWeldsBothSeamsWithPerCornerUvs) {
  const CurveDocument document = nonplanar_loop();
  const CurveTubeOptions options{
      .radius = 0.15,
      .radial_resolution = 8U,
      .sampling = {.tolerance = 0.01},
  };
  const auto built = build_curve_tube_mesh(document, options);
  ASSERT_TRUE(built.ok()) << built.error_message();
  const std::size_t rings = unique_ring_count(document, options.sampling);
  EXPECT_EQ(built.value().document.vertex_count(),
            rings * options.radial_resolution);
  EXPECT_EQ(built.value().document.face_count(),
            rings * options.radial_resolution);

  const auto authored = built.value().document.authored_topology();
  ASSERT_TRUE(authored.ok()) << authored.error_message();
  const AuthoredTopology &topology = authored.value();
  const auto edge_counts = edge_use_counts(topology);
  ASSERT_FALSE(edge_counts.empty());
  EXPECT_TRUE(std::all_of(edge_counts.begin(), edge_counts.end(),
                          [](const auto &entry) {
                            return entry.second == 2U;
                          }));

  const std::size_t closing_face_index =
      (rings - 1U) * options.radial_resolution;
  ASSERT_LT(closing_face_index, topology.faces.size());
  const AuthoredTopologyFace &closing = topology.faces[closing_face_index];
  const std::uint32_t last_ring_first = static_cast<std::uint32_t>(
      (rings - 1U) * options.radial_resolution + 1U);
  EXPECT_EQ(closing.vertices[0], make_id<VertexTag>(last_ring_first));
  EXPECT_EQ(closing.vertices[1], make_id<VertexTag>(last_ring_first + 1U));
  EXPECT_EQ(closing.vertices[2], make_id<VertexTag>(2U));
  EXPECT_EQ(closing.vertices[3], make_id<VertexTag>(1U));
  EXPECT_GT(closing.uvs[0].x, 0.0F);
  EXPECT_GT(closing.uvs[1].x, 0.0F);
  EXPECT_FLOAT_EQ(closing.uvs[2].x, 1.0F);
  EXPECT_FLOAT_EQ(closing.uvs[3].x, 1.0F);
  EXPECT_FLOAT_EQ(topology.faces.front().uvs.front().x, 0.0F);

  EXPECT_TRUE(std::all_of(topology.faces.begin(), topology.faces.end(),
                          [](const AuthoredTopologyFace &face) {
                            return face.material_slot == 0U &&
                                   face.shading ==
                                       AuthoredSurfaceShading::Authored;
                          }));
}

TEST(ModelingCurveTubeTests,
     EditableFactoryPreservesLegacyRendererPayloadExactly) {
  const CurveDocument document = nonplanar_loop();
  const CurveTubeOptions options{
      .radius = 0.2,
      .radial_resolution = 7U,
      .sampling = {.tolerance = 0.02},
  };
  const auto editable = build_curve_tube_mesh(document, options);
  const auto legacy = tessellate_curve_tube(document, options);
  ASSERT_TRUE(editable.ok()) << editable.error_message();
  ASSERT_TRUE(legacy.ok()) << legacy.error_message();
  expect_payload_equal(editable.value().payload, legacy.value());
}

TEST(ModelingCurveTubeTests,
     ConsecutiveRepeatedAnchorsProduceOnlyUniqueTubeRings) {
  const std::array points{
      CurvePointSpec{.position = {0.0F, 0.0F, 0.0F}},
      CurvePointSpec{.position = {0.0F, 0.0F, 0.0F},
                     .outgoing_handle = {1.0F, 0.0F, 0.0F}},
      CurvePointSpec{.position = {3.0F, 0.0F, 0.0F},
                     .incoming_handle = {-1.0F, 0.0F, 0.0F}},
  };
  const CurveDocument document = CurveDocument::make_bezier(points).or_throw();
  const CurveTubeOptions options{.radius = 0.25,
                                 .radial_resolution = 3U};
  const auto built = tessellate_curve_tube(document, options);
  ASSERT_TRUE(built.ok()) << built.error_message();
  const std::size_t rings = unique_ring_count(document, options.sampling);
  EXPECT_EQ(rings, 2U);
  EXPECT_EQ(built.value().vertices.size(), 8U);
  EXPECT_EQ(built.value().indices.size(), 18U);
}

TEST(ModelingCurveTubeTests, AuthoredCornerUsesDeterministicBisectorFrame) {
  const std::array points{
      CurvePointSpec{.position = {0.0F, 0.0F, 0.0F}},
      CurvePointSpec{.position = {1.0F, 0.0F, 0.0F}},
      CurvePointSpec{.position = {1.0F, 1.0F, 0.0F}},
  };
  const CurveDocument document = CurveDocument::make_bezier(points).or_throw();
  const CurveTubeOptions options{.radius = 0.1,
                                 .radial_resolution = 4U};
  const auto first = tessellate_curve_tube(document, options);
  const auto second = tessellate_curve_tube(document, options);
  ASSERT_TRUE(first.ok()) << first.error_message();
  ASSERT_TRUE(second.ok()) << second.error_message();
  ASSERT_EQ(first.value().vertices.size(), 15U);
  EXPECT_EQ(first.value().indices.size(), 48U);
  EXPECT_EQ(first.value().indices, second.value().indices);
  ASSERT_EQ(first.value().vertices.size(), second.value().vertices.size());
  for (std::size_t index = 0U; index < first.value().vertices.size(); ++index) {
    const MeshVertexPayload &left = first.value().vertices[index];
    const MeshVertexPayload &right = second.value().vertices[index];
    EXPECT_FLOAT_EQ(left.position.x, right.position.x);
    EXPECT_FLOAT_EQ(left.position.y, right.position.y);
    EXPECT_FLOAT_EQ(left.position.z, right.position.z);
    EXPECT_FLOAT_EQ(left.normal.x, right.normal.x);
    EXPECT_FLOAT_EQ(left.normal.y, right.normal.y);
    EXPECT_FLOAT_EQ(left.normal.z, right.normal.z);
    EXPECT_FLOAT_EQ(left.uv0.x, right.uv0.x);
    EXPECT_FLOAT_EQ(left.uv0.y, right.uv0.y);
  }
  constexpr float kInverseSqrtTwo = 0.7071067811865475F;
  const MeshVertexPayload &corner = first.value().vertices[5U];
  expect_vec_near(corner.normal,
                  {-kInverseSqrtTwo, kInverseSqrtTwo, 0.0F}, 2.0e-6);
  EXPECT_NEAR(vector_length(corner.normal), 1.0, 2.0e-6);
}

TEST(ModelingCurveTubeTests, CurvedTubeKeepsSmoothUnitRadialNormals) {
  const std::array points{
      CurvePointSpec{.position = {0.0F, 0.0F, 0.0F},
                     .outgoing_handle = {0.0F, 3.0F, 0.0F}},
      CurvePointSpec{.position = {4.0F, 0.0F, 1.0F},
                     .incoming_handle = {0.0F, 3.0F, 0.0F}},
  };
  const CurveDocument document = CurveDocument::make_bezier(points).or_throw();
  const CurveTubeOptions options{
      .radius = 0.2,
      .radial_resolution = 12U,
      .sampling = {.tolerance = 0.005},
  };
  const auto built = tessellate_curve_tube(document, options);
  ASSERT_TRUE(built.ok()) << built.error_message();
  const std::size_t rings = unique_ring_count(document, options.sampling);
  ASSERT_GT(rings, 2U);
  const std::size_t stride = options.radial_resolution + 1U;
  ASSERT_EQ(built.value().vertices.size(), rings * stride);
  for (std::size_t ring = 0U; ring < rings; ++ring) {
    Vec3 center{};
    for (std::uint32_t radial = 0U;
         radial < options.radial_resolution; ++radial) {
      const Vec3 position =
          built.value().vertices[ring * stride + radial].position;
      center.x += position.x;
      center.y += position.y;
      center.z += position.z;
    }
    center.x /= static_cast<float>(options.radial_resolution);
    center.y /= static_cast<float>(options.radial_resolution);
    center.z /= static_cast<float>(options.radial_resolution);
    for (std::uint32_t radial = 0U;
         radial < options.radial_resolution; ++radial) {
      const MeshVertexPayload &vertex =
          built.value().vertices[ring * stride + radial];
      EXPECT_NEAR(vector_length(vertex.normal), 1.0, 2.0e-6);
      EXPECT_NEAR(vector_length(subtract(vertex.position, center)),
                  options.radius, 2.0e-5);
      EXPECT_GT(dot_product(subtract(vertex.position, center), vertex.normal),
                0.0);
    }
  }
}

TEST(ModelingCurveTubeTests, InvalidSettingsReturnStructuredErrors) {
  const CurveDocument document = straight_curve();
  for (const double radius : {
           std::numeric_limits<double>::quiet_NaN(),
           std::numeric_limits<double>::infinity(), 0.0009, 100000.1}) {
    const auto result =
        tessellate_curve_tube(document, {.radius = radius});
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);
  }
  for (const std::uint32_t resolution : {2U, 129U}) {
    const auto result = tessellate_curve_tube(
        document, {.radial_resolution = resolution});
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);
    const auto editable = build_curve_tube_mesh(
        document, {.radial_resolution = resolution});
    ASSERT_FALSE(editable.ok());
    EXPECT_EQ(editable.error().code, result.error().code);
    EXPECT_EQ(editable.error_message(), result.error_message());
  }
  const auto invalid_sampling = tessellate_curve_tube(
      document, {.sampling = {.tolerance = 0.0}});
  ASSERT_FALSE(invalid_sampling.ok());
  EXPECT_EQ(invalid_sampling.error().code, ErrorCode::InvalidArgument);

  EXPECT_TRUE(tessellate_curve_tube(
                  document, {.radius = 0.001, .radial_resolution = 3U})
                  .ok());
  EXPECT_TRUE(tessellate_curve_tube(
                  document,
                  {.radius = 100000.0, .radial_resolution = 128U})
                  .ok());
}

TEST(ModelingCurveTubeTests,
     EmptySinglePointAndCoincidentPathsFailDeterministically) {
  CurveDocument point;
  ASSERT_TRUE(point.append_point({.position = {1.0F, 2.0F, 3.0F}}).ok());
  const std::array coincident{
      CurvePointSpec{.position = {2.0F, 3.0F, 4.0F}},
      CurvePointSpec{.position = {2.0F, 3.0F, 4.0F}},
      CurvePointSpec{.position = {2.0F, 3.0F, 4.0F}},
  };
  const CurveDocument coincident_document =
      CurveDocument::make_bezier(coincident).or_throw();
  const CurveDocument empty;
  const std::array<const CurveDocument *, 3U> candidates{
      &empty, &point, &coincident_document};
  for (const CurveDocument *candidate : candidates) {
    const auto first = tessellate_curve_tube(*candidate);
    const auto second = tessellate_curve_tube(*candidate);
    ASSERT_FALSE(first.ok());
    ASSERT_FALSE(second.ok());
    EXPECT_EQ(first.error().code, ErrorCode::ValidationFailed);
    EXPECT_EQ(second.error().code, first.error().code);
    EXPECT_EQ(second.error_message(), first.error_message());
    EXPECT_TRUE(first.value_or({}).vertices.empty());
  }
}

TEST(ModelingCurveTubeTests, PracticalOutputBudgetFailsBeforeMeshAllocation) {
  constexpr std::size_t kRingCount = 31'008U;
  std::vector<CurvePointSpec> points;
  points.reserve(kRingCount);
  for (std::size_t index = 0U; index < kRingCount; ++index) {
    points.push_back(
        {.position = {static_cast<float>(index), 0.0F, 0.0F}});
  }
  const CurveDocument document = CurveDocument::make_bezier(points).or_throw();
  const CurveContentIdentity identity = document.content_identity();
  const auto first = tessellate_curve_tube(
      document, {.radial_resolution = 128U,
                 .sampling = {.max_samples = 100'000U}});
  const auto second = tessellate_curve_tube(
      document, {.radial_resolution = 128U,
                 .sampling = {.max_samples = 100'000U}});
  const auto editable = build_curve_tube_mesh(
      document, {.radial_resolution = 128U,
                 .sampling = {.max_samples = 100'000U}});
  ASSERT_FALSE(first.ok());
  ASSERT_FALSE(second.ok());
  ASSERT_FALSE(editable.ok());
  EXPECT_EQ(first.error().code, ErrorCode::ValidationFailed);
  EXPECT_EQ(first.error_message(),
            "Curve tube exceeds the practical derived-mesh budget.");
  EXPECT_EQ(second.error_message(), first.error_message());
  EXPECT_EQ(editable.error_message(), first.error_message());
  EXPECT_EQ(document.content_identity(), identity);
}

} // namespace
} // namespace quader::modeling

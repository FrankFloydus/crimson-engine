////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/mesh_quad_uv_interpolation.hpp"

#include <quader/modeling/neutral/geometry/quad_uv_interpolation.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace {

using QVec2 = quader_geometry::QVec2f;
using QVec3 = quader_geometry::QVec3f;
using Points = std::array<QVec3, 4>;
using Uvs = std::array<QVec2, 4>;

constexpr Points kSquare{QVec3{0, 0, 0}, QVec3{1, 0, 0},
                         QVec3{1, 1, 0}, QVec3{0, 1, 0}};
constexpr Uvs kNonaffineUvs{QVec2{0, 0}, QVec2{1, 0},
                           QVec2{1.2F, 1}, QVec2{0, 1}};
constexpr std::array<std::array<std::uint32_t, 6>, 2> kDiagonals{
    std::array<std::uint32_t, 6>{0, 1, 2, 0, 2, 3},
    std::array<std::uint32_t, 6>{0, 1, 3, 1, 2, 3}};

std::array<quader::MeshVertex, 4> mesh_vertices(const Points &points,
                                              const Uvs &uvs) {
  std::array<quader::MeshVertex, 4> vertices{};
  for (std::size_t index = 0; index < vertices.size(); ++index) {
    vertices[index].position = {points[index].x, points[index].y, points[index].z};
    vertices[index].uv = {uvs[index].x, uvs[index].y};
    vertices[index].source_vertex_id = static_cast<std::uint32_t>(index + 1U);
  }
  return vertices;
}

void expect_bilinear_samples(const Points &points, const Uvs &uvs) {
  const auto shared = quader_geometry::build_quad_uv_interpolation(points, uvs);
  ASSERT_TRUE(shared.has_value());
  const auto vertices = mesh_vertices(points, uvs);
  const std::array<quader::MeshPrimitiveSpan, 1> primitives{
      quader::MeshPrimitiveSpan{0, 6, 0}};
  for (const auto &indices : kDiagonals) {
    const auto render = quader::build_mesh_quad_uv_interpolations(
        vertices, indices, primitives);
    ASSERT_EQ(render.size(), 4U);
    ASSERT_TRUE(std::ranges::all_of(render, [](const auto &corner) {
      return corner.enabled;
    }));
    for (float s : {0.0F, 0.17F, 0.5F, 0.83F, 1.0F}) {
      for (float t : {0.0F, 0.23F, 0.5F, 0.79F, 1.0F}) {
        // A forward four-corner interpolation supplies the expected image and
        // world location without depending on the production inverse solve.
        const std::array<float, 4> weights{
            (1 - s) * (1 - t), s * (1 - t), s * t, (1 - s) * t};
        QVec3 point{};
        QVec2 expected{};
        for (std::size_t corner = 0; corner < 4U; ++corner) {
          point = point + points[corner] * weights[corner];
          expected = expected + uvs[corner] * weights[corner];
        }
        bool sampled = false;
        for (std::size_t offset = 0; offset < indices.size(); offset += 3U) {
          const auto a = indices[offset];
          const auto b = indices[offset + 1U];
          const auto c = indices[offset + 2U];
          const auto ab = points[b] - points[a];
          const auto ac = points[c] - points[a];
          const auto ap = point - points[a];
          const double determinant = static_cast<double>(ab.x) * ac.y -
                                     static_cast<double>(ab.y) * ac.x;
          ASSERT_GT(std::abs(determinant), 0.0);
          const float wb = static_cast<float>((static_cast<double>(ap.x) * ac.y -
                                               static_cast<double>(ap.y) * ac.x) /
                                              determinant);
          const float wc = static_cast<float>((static_cast<double>(ab.x) * ap.y -
                                               static_cast<double>(ab.y) * ap.x) /
                                              determinant);
          const std::array<float, 3> triangle_weights{1 - wb - wc, wb, wc};
          if (std::ranges::any_of(triangle_weights, [](float weight) {
                return weight < -0.000002F;
              })) {
            continue;
          }
          QVec2 triangle_uv{};
          QVec3 shared_coordinate{};
          quader::MeshQuadUvInterpolation render_sample = render[a];
          render_sample.coordinates = {};
          for (std::size_t index = 0; index < 3U; ++index) {
            const auto corner = indices[offset + index];
            const float weight = triangle_weights[index];
            triangle_uv = triangle_uv + uvs[corner] * weight;
            shared_coordinate = shared_coordinate +
                                shared->coordinates[corner] * weight;
            render_sample.coordinates.x += render[corner].coordinates.x * weight;
            render_sample.coordinates.y += render[corner].coordinates.y * weight;
            render_sample.coordinates.z += render[corner].coordinates.z * weight;
          }
          const auto actual = quader_geometry::evaluate_quad_uv_interpolation(
              triangle_uv, shared_coordinate, shared->geometry_warp,
              shared->uv_correction);
          const auto rendered = quader::evaluate_mesh_quad_uv_interpolation(
              {triangle_uv.x, triangle_uv.y}, render_sample);
          EXPECT_NEAR(actual.x, expected.x, 0.000003F);
          EXPECT_NEAR(actual.y, expected.y, 0.000003F);
          EXPECT_NEAR(rendered.x, expected.x, 0.000003F);
          EXPECT_NEAR(rendered.y, expected.y, 0.000003F);
          sampled = true;
        }
        EXPECT_TRUE(sampled);
      }
    }
  }
}

void expect_shared_and_render_fallback(const Points &points, const Uvs &uvs) {
  EXPECT_FALSE(quader_geometry::build_quad_uv_interpolation(points, uvs).has_value());
  const auto vertices = mesh_vertices(points, uvs);
  const std::array<quader::MeshPrimitiveSpan, 1> primitives{
      quader::MeshPrimitiveSpan{0, 6, 0}};
  for (const auto &indices : kDiagonals) {
    const auto render = quader::build_mesh_quad_uv_interpolations(
        vertices, indices, primitives);
    ASSERT_EQ(render.size(), 4U);
    for (const auto &corner : render) {
      EXPECT_FALSE(corner.enabled);
    }
  }
}

} // namespace

TEST(MeshQuadUvInterpolationTests, SharedAndRenderSamplersPreserveBilinearInterior) {
  expect_bilinear_samples(kSquare, kNonaffineUvs);
  // In particular, the source center is (0.55, 0.5), not either
  // triangle-diagonal interpolation value (0.5 or 0.6, 0.5).
  const auto quad = quader_geometry::build_quad_uv_interpolation(kSquare, kNonaffineUvs);
  ASSERT_TRUE(quad.has_value());
  const auto actual = quader_geometry::evaluate_quad_uv_interpolation(
      {0.6F, 0.5F}, {0.5F, 0.5F, 0.5F}, quad->geometry_warp,
      quad->uv_correction);
  EXPECT_NEAR(actual.x, 0.55F, 0.000001F);
  EXPECT_NEAR(actual.y, 0.5F, 0.000001F);
}

TEST(MeshQuadUvInterpolationTests, SharedAndRenderSamplersAgreeAfterOriginChanges) {
  Points points{QVec3{0, 0, 0}, QVec3{2, 0, 0},
                QVec3{1.5F, 1.25F, 0}, QVec3{-0.25F, 1, 0}};
  Uvs uvs = kNonaffineUvs;
  for (std::size_t origin = 0; origin < 4U; ++origin) {
    expect_bilinear_samples(points, uvs);
    std::rotate(points.begin(), points.begin() + 1, points.end());
    std::rotate(uvs.begin(), uvs.begin() + 1, uvs.end());
  }
  std::reverse(points.begin(), points.end());
  std::reverse(uvs.begin(), uvs.end());
  expect_bilinear_samples(points, uvs);
}

TEST(MeshQuadUvInterpolationTests, SharedAndRenderSamplersKeepFallbackEligibility) {
  expect_shared_and_render_fallback(
      kSquare, Uvs{QVec2{0, 0}, QVec2{1, 0}, QVec2{1, 1}, QVec2{0, 1}});
  expect_shared_and_render_fallback(
      kSquare, Uvs{QVec2{0, 0}, QVec2{1, 0}, QVec2{0.2F, 0.2F}, QVec2{0, 1}});
  for (const QVec3 changed_corner : {QVec3{0.2F, 0.2F, 0},
                                     QVec3{-1, 1, 0}, QVec3{1, 1, 0.1F},
                                     QVec3{0.001F, 1, 0}}) {
    Points points = kSquare;
    points[2] = changed_corner;
    expect_shared_and_render_fallback(points, kNonaffineUvs);
  }
  Uvs invalid = kNonaffineUvs;
  invalid[2].x = std::numeric_limits<float>::quiet_NaN();
  expect_shared_and_render_fallback(kSquare, invalid);
}

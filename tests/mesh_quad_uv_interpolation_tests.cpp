////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/mesh_quad_uv_interpolation.hpp"
#include "render/render_signature_builder.hpp"
#include "render/scene_render_extraction.hpp"
#include "support/stairs_smooth_uv_experiment.hpp"

#include <quader/modeling/neutral/geometry/quad_uv_interpolation.hpp>
#include <quader/modeling/neutral/polygon/document_texture.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_uv_helpers.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <vector>

namespace {

using QVec2 = quader_geometry::QVec2f;
using QVec3 = quader_geometry::QVec3f;
using Points = std::array<QVec3, 4>;
using Uvs = std::array<QVec2, 4>;
using SmoothPoint = quader::test::SmoothUvPoint;
using SmoothBoundary = std::vector<quader::test::SmoothUvBoundaryVertex>;

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

TEST(StairsSmoothUvExperimentTests, PreservesAffineMapsAndBoundaryValuesInEitherWinding) {
  SmoothBoundary boundary{{{0, 0}, {4, -3}}, {{1, 0}, {6, -4}},
                          {{2, 0}, {8, -5}}, {{2, 1}, {11, 0}},
                          {{1, 1}, {9, 1}}, {{0, 1}, {7, 2}}};
  for (int winding = 0; winding < 2; ++winding) {
    const quader::test::StairsSmoothUvExperiment map(boundary);
    ASSERT_TRUE(map.valid());
    for (const auto &corner : boundary) {
      const auto uv = map.evaluate_mean_value(corner.position);
      ASSERT_TRUE(uv.has_value());
      EXPECT_DOUBLE_EQ(uv->x, corner.uv.x);
      EXPECT_DOUBLE_EQ(uv->y, corner.uv.y);
    }
    for (const SmoothPoint point : {SmoothPoint{0.1, 0.2}, {1, 0.5},
                                     {1.8, 0.7}, {0.5, 0}, {2, 0.25}}) {
      const auto uv = map.evaluate_mean_value(point);
      ASSERT_TRUE(uv.has_value());
      EXPECT_NEAR(uv->x, 4 + 2 * point.x + 3 * point.y, 1e-12);
      EXPECT_NEAR(uv->y, -3 - point.x + 5 * point.y, 1e-12);
    }
    EXPECT_FALSE(map.evaluate_mean_value({2.1, 0.5}).has_value());
    std::reverse(boundary.begin(), boundary.end());
  }
}

TEST(StairsSmoothUvExperimentTests, SmoothsTheDerivativeAcrossAnInternalPanelJoin) {
  // Two adjacent rectangular panels with different top-edge density have a
  // unit derivative jump at their shared edge under independent bilinear maps.
  // The experiment evaluates their unchanged outer boundary as one domain.
  const quader::test::StairsSmoothUvExperiment map(SmoothBoundary{
      {{0, 0}, {0, 0}}, {{1, 0}, {1, 0}}, {{2, 0}, {2, 0}},
      {{2, 1}, {2, 1}}, {{1, 1}, {1, 2}}, {{0, 1}, {0, 1}}});
  ASSERT_TRUE(map.valid());
  constexpr double kOffset = 0.00001;
  const auto left = map.evaluate_mean_value({1 - kOffset, 0.5});
  const auto center = map.evaluate_mean_value({1, 0.5});
  const auto right = map.evaluate_mean_value({1 + kOffset, 0.5});
  ASSERT_TRUE(left.has_value());
  ASSERT_TRUE(center.has_value());
  ASSERT_TRUE(right.has_value());
  EXPECT_NEAR((center->x - left->x) / kOffset, 1, 1e-8);
  EXPECT_NEAR((right->x - center->x) / kOffset, 1, 1e-8);
  EXPECT_NEAR((center->y - left->y) / kOffset,
              (right->y - center->y) / kOffset, 0.0001);
  // Smoothing the field changes an internal constraint; the experiment must
  // not claim that an omitted pin at this point remains the bilinear value 1.
  EXPECT_GT(std::abs(center->y - 1), 0.05);
}

TEST(StairsSmoothUvExperimentTests, HandlesConcavityAndRejectsMalformedDomains) {
  SmoothBoundary boundary{{{0, 0}, {0, 0}}, {{2, 0}, {2, 0}},
                          {{2, 1}, {2, 1}}, {{1, 1}, {1, 1}},
                          {{1, 2}, {1, 2}}, {{0, 2}, {0, 2}}};
  const quader::test::StairsSmoothUvExperiment map(boundary);
  ASSERT_TRUE(map.valid());
  for (const SmoothPoint point : {SmoothPoint{0.5, 1.5}, {1.5, 0.5}, {1, 1}}) {
    const auto uv = map.evaluate_mean_value(point);
    ASSERT_TRUE(uv.has_value());
    EXPECT_NEAR(uv->x, point.x, 1e-12);
    EXPECT_NEAR(uv->y, point.y, 1e-12);
  }
  EXPECT_FALSE(map.evaluate_mean_value({1.5, 1.5}).has_value());
  boundary[1].uv.x = std::numeric_limits<double>::infinity();
  EXPECT_FALSE(quader::test::StairsSmoothUvExperiment(boundary).valid());
  EXPECT_FALSE(quader::test::StairsSmoothUvExperiment(SmoothBoundary{
      {{0, 0}, {}}, {{1, 1}, {}}, {{0, 1}, {}}, {{1, 0}, {}}}).valid());
}

TEST(StairsSmoothUvExperimentTests, CircularReferenceKeepsRadiusAndUnwrapsTheAngle) {
  const quader::test::PolarUvReference reference{
      .center = {3, -2}, .reference_angle = std::numbers::pi,
      .reference_radius = 1.3, .angular_scale = 2, .radial_scale = 3,
      .offset = {4, 5}};
  for (const double delta : {-0.2, 0.0, 0.2}) {
    const double angle = std::numbers::pi + delta;
    const auto uv = quader::test::evaluate_polar(
        {3 + 1.8 * std::cos(angle), -2 + 1.8 * std::sin(angle)}, reference);
    ASSERT_TRUE(uv.has_value());
    EXPECT_NEAR(uv->x, 4 + 2 * delta, 1e-12);
    EXPECT_NEAR(uv->y, 6.5, 1e-12);
  }
  EXPECT_FALSE(quader::test::evaluate_polar(reference.center, reference).has_value());
}

namespace {

quader_poly::CircularUvInterpolation circular_test_mapping() {
  return {.axis_u = {1, 0, 0}, .axis_v = {0, 1, 0},
          .uv_from_polar = {0.7F, 1.3F, -0.2F, 0.9F}};
}

quader::QVec2 circular_test_uv(quader::QVec3 point) {
  const double radius = std::hypot(double(point.x), double(point.y));
  const double angle = std::atan2(double(point.y), double(point.x));
  return {float(2.0 + 0.7 * radius + 1.3 * angle + 0.4 * point.x - 0.2 * point.y),
          float(-1.0 - 0.2 * radius + 0.9 * angle - 0.1 * point.x + 0.3 * point.y)};
}

std::array<quader::MeshVertex, 4> circular_test_vertices() {
  std::array<quader::MeshVertex, 4> vertices;
  const float cosine = std::cos(0.4F);
  const float sine = std::sin(0.4F);
  const std::array<quader::QVec3, 4> points{{
      {cosine, -sine, 0}, {2 * cosine, -2 * sine, 0},
      {2 * cosine, 2 * sine, 0}, {cosine, sine, 0}}};
  for (std::size_t index = 0; index < vertices.size(); ++index) {
    vertices[index].position = points[index];
    vertices[index].uv = circular_test_uv(points[index]);
    vertices[index].source_vertex_id = static_cast<std::uint32_t>(index + 1U);
    vertices[index].circular_uv_index = 1U;
  }
  return vertices;
}

} // namespace

TEST(MeshQuadUvInterpolationTests, CircularMappingMatchesAnalyticFieldAcrossBothDiagonals) {
  const auto vertices = circular_test_vertices();
  const std::array mappings{circular_test_mapping()};
  const std::array<quader::MeshPrimitiveSpan, 1> primitives{{{0, 6, 0}}};
  for (const auto &indices : kDiagonals) {
    const auto render = quader::build_mesh_quad_uv_interpolations(vertices, indices, primitives, {}, mappings);
    ASSERT_EQ(render.size(), vertices.size());
    for (const auto &corner : render) {
      EXPECT_TRUE(corner.circular);
      EXPECT_FALSE(corner.enabled);
    }
    for (std::size_t triangle = 0; triangle < indices.size(); triangle += 3U) {
      for (const auto weights : {std::array{0.2F, 0.3F, 0.5F}, std::array{0.5F, 0.5F, 0.0F},
                                 std::array{1.0F, 0.0F, 0.0F}}) {
        auto interpolated = render[indices[triangle]];
        interpolated.circular_coordinates = {};
        quader::QVec3 position{};
        quader::QVec2 triangle_uv{};
        for (std::size_t corner = 0; corner < weights.size(); ++corner) {
          const auto index = indices[triangle + corner];
          position += vertices[index].position * weights[corner];
          triangle_uv += vertices[index].uv * weights[corner];
          for (std::size_t axis = 0; axis < 4U; ++axis) {
            interpolated.circular_coordinates[axis] += render[index].circular_coordinates[axis] * weights[corner];
          }
        }
        const auto actual = quader::evaluate_mesh_quad_uv_interpolation(triangle_uv, interpolated);
        const auto expected = circular_test_uv(position);
        EXPECT_NEAR(actual.x, expected.x, 0.000001F);
        EXPECT_NEAR(actual.y, expected.y, 0.000001F);
      }
    }
  }
}

TEST(MeshQuadUvInterpolationTests, CircularCpuSamplerIncludesCurvedBoundaryAndFaceTransform) {
  const auto vertices = circular_test_vertices();
  quader_poly::Document document;
  quader_poly::Face face;
  face.id = 1;
  face.circular_uv_interpolation = circular_test_mapping();
  face.texture_transform = {.scale = {2.0F, -0.8F}, .shift_uv = {7, -4}, .rotation_degrees = 35};
  for (const auto &vertex : vertices) {
    const auto id = static_cast<quader_poly::ElementId>(vertex.source_vertex_id);
    document.vertices.push_back({id, vertex.position});
    face.vertices.push_back(id);
    face.uvs.push_back(vertex.uv);
  }
  document.faces.push_back(face);
  const quader_poly::document_internal::FaceUvSampler sampler(document, document.faces.front());
  EXPECT_FALSE(sampler.has_quad_interpolation());
  for (const auto point : {(vertices[1].position + vertices[2].position) * 0.5F,
                           (vertices[0].position + vertices[2].position) * 0.5F,
                           vertices[1].position}) {
    const auto actual = sampler.sample(point);
    ASSERT_TRUE(actual.has_value());
    const auto expected = circular_test_uv(point);
    EXPECT_NEAR(actual->x, expected.x, 0.000002F);
    EXPECT_NEAR(actual->y, expected.y, 0.000002F);
  }
}

TEST(MeshQuadUvInterpolationTests, CircularFramesUseEffectivePositionsAndRejectInvalidData) {
  auto vertices = circular_test_vertices();
  const std::array<quader::MeshPrimitiveSpan, 1> primitives{{{0, 6, 0}}};
  auto mapping = circular_test_mapping();
  mapping.center = {5, -7, 2};
  mapping.axis_u = {0.5F, 0, 0};
  mapping.axis_v = {0, 2, 0};
  const std::array mappings{mapping};
  std::array<quader::QVec3, 4> positions;
  for (std::size_t index = 0; index < positions.size(); ++index) {
    positions[index] = {5 + 2 * vertices[index].position.x,
                        -7 + 0.5F * vertices[index].position.y, 2};
  }
  const auto render = quader::build_mesh_quad_uv_interpolations(vertices, kDiagonals[0], primitives, positions, mappings);
  for (std::size_t index = 0; index < positions.size(); ++index) {
    ASSERT_TRUE(render[index].circular);
    EXPECT_NEAR(render[index].circular_coordinates[0], vertices[index].position.x, 0.000001F);
    EXPECT_NEAR(render[index].circular_coordinates[1], vertices[index].position.y, 0.000001F);
  }
  positions[0] = mapping.center;
  const auto collapsed = quader::build_mesh_quad_uv_interpolations(vertices, kDiagonals[0], primitives, positions, mappings);
  for (const auto &corner : collapsed) EXPECT_FALSE(corner.circular);
  mapping.axis_v = mapping.axis_u;
  EXPECT_FALSE(quader_poly::valid_circular_uv_interpolation(mapping));
  mapping = circular_test_mapping();
  mapping.uv_from_polar[0] = std::numeric_limits<float>::infinity();
  EXPECT_FALSE(quader_poly::valid_circular_uv_interpolation(mapping));
  vertices[0].circular_uv_index = 2U;
  const auto absent = quader::build_mesh_quad_uv_interpolations(vertices, kDiagonals[0], primitives, {}, mappings);
  EXPECT_FALSE(absent[0].circular);
}

TEST(MeshQuadUvInterpolationTests, CircularDescriptorChangesInvalidateRenderSignatures) {
  const auto vertices = circular_test_vertices();
  quader::MeshAsset mesh;
  mesh.edit_geometry().vertices.assign(vertices.begin(), vertices.end());
  mesh.edit_geometry().indices.assign(kDiagonals[0].begin(), kDiagonals[0].end());
  mesh.edit_geometry().primitives = {{0, 6, 0}};
  mesh.edit_geometry().circular_uv_interpolations = {circular_test_mapping()};
  const auto first = quader::RenderSignatureBuilder::mesh_asset_signature(0, mesh);
  const auto snapshot = quader::render_mesh_asset_snapshot(1, mesh);
  ASSERT_EQ(snapshot.circular_uv_interpolations.size(), 1U);
  EXPECT_EQ(snapshot.circular_uv_interpolations.front(), circular_test_mapping());
  mesh.edit_geometry().circular_uv_interpolations.front().uv_from_polar[1] += 0.5F;
  EXPECT_NE(quader::RenderSignatureBuilder::mesh_asset_signature(0, mesh), first);
  mesh.edit_geometry().circular_uv_interpolations = {circular_test_mapping()};
  mesh.edit_geometry().vertices.front().circular_uv_index = 0U;
  EXPECT_NE(quader::RenderSignatureBuilder::mesh_asset_signature(0, mesh), first);
}

TEST(MeshQuadUvInterpolationTests, MixedCircularTriangleDescriptorsFallBackAsCompleteCharts) {
  auto vertices = circular_test_vertices();
  vertices[0].circular_uv_index = 2U;
  vertices[1].circular_uv_index = 0U;
  const std::array mappings{circular_test_mapping(), circular_test_mapping()};
  const std::array<quader::MeshPrimitiveSpan, 1> primitives{{{0, 6, 0}}};
  const auto render = quader::build_mesh_quad_uv_interpolations(
      vertices, kDiagonals[0], primitives, {}, mappings);
  for (const auto &corner : render) {
    EXPECT_FALSE(corner.circular);
    EXPECT_FALSE(corner.enabled);
  }
}

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
#include <span>
#include <utility>

namespace {

using namespace quader::modeling;

/** Builds one selected face through the public authored-topology contract. */
PolygonDocument precision_face(std::span<const Vec3> positions,
                               std::span<const Vec2> uvs = {},
                               FaceTextureTransform settings = {}) {
  auto document = PolygonDocument::make_face(positions, make_id<MaterialTag>(7U)).or_throw();
  auto topology = document.authored_topology().or_throw();
  topology.faces.front().uvs.assign(uvs.begin(), uvs.end());
  topology.faces.front().texture_transform = settings;
  document = PolygonDocument::from_authored_topology(topology).or_throw();
  (void)document.select_all_faces().or_throw();
  return document;
}

/** Evaluates the public render payload with a known constant projection delta. */
Vec2 translated_uv(const MeshVertexPayload& vertex, Vec2 delta) {
  return {vertex.uv0.x + vertex.uv_carrier[0] * delta.x +
                            vertex.uv_carrier[1] * delta.y,
          vertex.uv0.y + vertex.uv_carrier[2] * delta.x +
                            vertex.uv_carrier[3] * delta.y};
}

/** Applies the documented texture settings to an independently defined chart. */
Vec2 transformed_uv(Vec2 base, const FaceTextureTransform& settings) {
  const double angle = settings.rotation_degrees * 0.017453292519943295;
  const double cosine = std::cos(angle);
  const double sine = std::sin(angle);
  return {static_cast<float>((base.x * cosine - base.y * sine) / settings.scale.x +
                             settings.shift_uv.x),
          static_cast<float>((base.x * sine + base.y * cosine) / settings.scale.y +
                             settings.shift_uv.y)};
}

/** A full-rank sheared chart makes lost rotation, sign, or density observable. */
Vec2 strip_uv(float developed_x, float y) {
  return {0.0375F + 0.07F * developed_x + 0.02F * y,
          -0.0625F + 0.015F * developed_x - 0.09F * y};
}

/** Keeps each transfer on the same translated displayed occurrence. */
FaceTextureTransferFace transfer_face(const PolygonDocument& document,
                                      std::uint64_t key, Vec3 translation) {
  FaceTextureTransferFace result{
      .key = key, .document = &document, .face = document.face_ids().front()};
  result.world_from_local.origin = translation;
  result.sampling_from_local.origin = translation;
  return result;
}

constexpr std::array<Vec3, 4> kNarrowPoints{{
    {0.0F, 0.0F, 0.0F}, {1024.0F, 0.0F, 0.0F},
    {1024.0F, 0.001024F, 0.0F}, {0.0F, 0.001024F, 0.0F}}};
constexpr std::array<Vec2, 4> kNarrowGeneratedUvs{{
    {0.0F, 0.0F}, {512.0F, 0.0F},
    {512.0F, -0.000512F}, {0.0F, -0.000512F}}};

TEST(UvPrecisionReliabilityTests,
     AuthoringGeneratedUvsOnNarrowLocalGeometryRetainsTheSamplingCarrier) {
  const auto procedural = precision_face(kNarrowPoints);
  const auto authored = precision_face(kNarrowPoints, kNarrowGeneratedUvs);
  const auto before = procedural.compile_mesh().or_throw();
  const auto after = authored.compile_mesh().or_throw();
  ASSERT_FALSE(before.vertices.empty());
  ASSERT_EQ(before.vertices.size(), after.vertices.size());
  constexpr Vec2 kTranslationDelta{2.5F, -0.125F};
  for (const auto& vertex : after.vertices) {
    const auto original = std::ranges::find(
        before.vertices, vertex.source_vertex, &MeshVertexPayload::source_vertex);
    ASSERT_NE(original, before.vertices.end());
    for (std::size_t coefficient = 0U; coefficient < vertex.uv_carrier.size();
         ++coefficient) {
      EXPECT_NEAR(vertex.uv_carrier[coefficient], original->uv_carrier[coefficient],
                  1.0e-6F);
    }
    EXPECT_NEAR(vertex.uv_carrier[0], 1.0F, 1.0e-6F);
    EXPECT_NEAR(vertex.uv_carrier[3], 1.0F, 1.0e-6F);
    const auto actual = translated_uv(vertex, kTranslationDelta);
    const auto expected = translated_uv(*original, kTranslationDelta);
    EXPECT_FLOAT_EQ(actual.x, expected.x);
    EXPECT_FLOAT_EQ(actual.y, expected.y);
  }
}

TEST(UvPrecisionReliabilityTests,
     ExplicitGeneratedTriangleChartRetainsExactCarrierAndTranslatedEvaluation) {
  constexpr std::array<Vec3, 3> kPoints{{
      {0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}}};
  constexpr std::array<Vec2, 3> kGeneratedUvs{{
      {0.0F, 0.0F}, {0.5F, 0.0F}, {0.0F, -0.5F}}};
  const auto procedural = precision_face(kPoints).compile_mesh().or_throw();
  const auto authored = precision_face(kPoints, kGeneratedUvs).compile_mesh().or_throw();
  ASSERT_FALSE(procedural.vertices.empty());
  ASSERT_EQ(authored.vertices.size(), procedural.vertices.size());
  for (const auto& vertex : authored.vertices) {
    const auto original = std::ranges::find(
        procedural.vertices, vertex.source_vertex, &MeshVertexPayload::source_vertex);
    ASSERT_NE(original, procedural.vertices.end());
    EXPECT_EQ(vertex.uv_carrier, original->uv_carrier);
    for (const Vec2 delta : {Vec2{0.0F, 2048.0F}, Vec2{-4096.0F, 0.0F}}) {
      const auto actual = translated_uv(vertex, delta);
      const auto expected = translated_uv(*original, delta);
      EXPECT_EQ(actual.x, expected.x);
      EXPECT_EQ(actual.y, expected.y);
    }
  }
}

TEST(UvPrecisionReliabilityTests,
     NarrowAuthoredFacesSupportTranslatedAlignmentAndSubsequentTransfer) {
  const auto source = precision_face(kNarrowPoints, kNarrowGeneratedUvs);
  constexpr Vec3 kTranslation{5.0F, 0.25F, -2.0F};
  FaceTextureActionTarget request{
      .key = 1U, .document = &source,
      .faces = {{.face = source.face_ids().front()}}};
  request.world_from_local.origin = kTranslation;
  request.sampling_from_local.origin = kTranslation;
  const auto aligned = prepare_face_texture_action(std::array{request},
      {.action = FaceTextureAction::AlignToGrid});
  ASSERT_TRUE(aligned.ok()) << aligned.error_message();
  ASSERT_TRUE(aligned.value().changed);
  const auto& aligned_document = aligned.value().documents.front().document;
  EXPECT_TRUE(aligned_document.has_same_geometry(source));
  auto target = precision_face(kNarrowPoints, {}, {.scale = {2.0F, 2.0F}});
  auto from = transfer_face(aligned_document, 1U, kTranslation);
  auto to = transfer_face(target, 2U, kTranslation);
  const auto transferred = prepare_face_texture_transfer(from, to, 6U);
  ASSERT_TRUE(transferred.ok()) << transferred.error_message();
  ASSERT_TRUE(transferred.value().changed);
  const auto& transferred_document = transferred.value().documents.front().document;
  for (const auto* document : {&aligned_document, &transferred_document}) {
    const auto mesh = document->compile_mesh().or_throw();
    ASSERT_FALSE(mesh.vertices.empty());
    for (const auto& vertex : mesh.vertices) {
      // Stored UVs near -1 quantize the narrow span; its carrier must still
      // retain the density and reproduce the much stricter corner values.
      EXPECT_NEAR(vertex.uv_carrier[0], 8.0F, 1.0e-3F);
      EXPECT_NEAR(vertex.uv_carrier[3], 8.0F, 1.0e-3F);
      const auto effective = translated_uv(vertex, {2.5F, -0.125F});
      EXPECT_NEAR(effective.x, 4.0F * (vertex.position.x + kTranslation.x), 1.0e-4F);
      EXPECT_NEAR(effective.y, -4.0F * (vertex.position.y + kTranslation.y), 1.0e-6F);
    }
  }
  request.document = &aligned_document;
  const auto aligned_again = prepare_face_texture_action(std::array{request},
      {.action = FaceTextureAction::AlignToGrid});
  ASSERT_TRUE(aligned_again.ok()) << aligned_again.error_message();
  EXPECT_FALSE(aligned_again.value().changed);
  EXPECT_EQ(aligned_again.value().documents.front().document.content_identity(),
            aligned_document.content_identity());
  to.document = &transferred_document;
  const auto transferred_again = prepare_face_texture_transfer(from, to, 6U);
  ASSERT_TRUE(transferred_again.ok()) << transferred_again.error_message();
  EXPECT_FALSE(transferred_again.value().changed);
  EXPECT_EQ(transferred_again.value().documents.front().document.content_identity(),
            transferred_document.content_identity());
}

TEST(UvPrecisionReliabilityTests,
     ContinuedSignedRotatedChartsRemainUsableAcrossTranslatedPlaneAndSlopeChains) {
  constexpr std::array<Vec3, 4> kSourcePoints{{
      {0.0F, 0.0F, 0.0F}, {4.0F, 0.0F, 0.0F},
      {4.0F, 3.0F, 0.0F}, {0.0F, 3.0F, 0.0F}}};
  constexpr std::array kSettings{
      FaceTextureTransform{.scale = {-0.125F, 0.375F},
          .shift_uv = {0.31F, -0.37F}, .rotation_degrees = 37.0F},
      FaceTextureTransform{.scale = {0.35F, -0.2F},
          .shift_uv = {-0.27F, 0.19F}, .rotation_degrees = -23.0F}};
  constexpr std::array kTranslations{
      Vec3{}, Vec3{5.0F, 0.5F, -21.5F}, Vec3{-31.25F, 7.75F, 13.5F}};
  struct WrapStep {
    double degrees;
    float developed_width;
  };
  // Every panel has two units of developed width, including the obtuse
  // 90 -> -20 turn; a continued chart must not accumulate density loss.
  constexpr std::array<WrapStep, 4> kSteps{{
      {37.0, 2.0F}, {90.0, 2.0F},
      {-20.0, 2.0F}, {0.0, 2.0F}}};
  std::array<Vec2, 4> source_uvs;
  for (std::size_t corner = 0U; corner < source_uvs.size(); ++corner) {
    source_uvs[corner] = strip_uv(kSourcePoints[corner].x, kSourcePoints[corner].y);
  }
  for (const auto settings : kSettings) {
    SCOPED_TRACE(settings.rotation_degrees);
    for (const auto translation : kTranslations) {
      SCOPED_TRACE(translation.x);
      auto current = precision_face(kSourcePoints, source_uvs, settings);
      Vec3 seam{4.0F, 0.0F, 0.0F};
      float developed_start = 4.0F;
      for (const auto &[degrees, developed_width] : kSteps) {
        SCOPED_TRACE(degrees);
        const double angle = degrees * 0.017453292519943295;
        const Vec3 end{seam.x + static_cast<float>(2.0 * std::cos(angle)), 0.0F,
                       seam.z - static_cast<float>(2.0 * std::sin(angle))};
        const std::array<Vec3, 4> points{{
            seam, end, {end.x, 3.0F, end.z}, {seam.x, 3.0F, seam.z}}};
        const auto target = precision_face(points);
        const auto source_bytes = serialize_qdr_authored_topology(current).or_throw();
        const auto from = transfer_face(current, 1U, translation);
        auto to = transfer_face(target, 2U, translation);
        const auto result = prepare_face_texture_transfer(from, to, 6U);
        ASSERT_TRUE(result.ok()) << result.error_message();
        ASSERT_TRUE(result.value().changed);
        const auto& prepared = result.value().documents.front().document;
        EXPECT_TRUE(prepared.has_same_geometry(target));
        EXPECT_EQ(prepared.authored_topology().or_throw().faces.front().texture_transform,
                  settings);
        EXPECT_EQ(serialize_qdr_authored_topology(current).or_throw(), source_bytes);
        // These vertical strip panels use the +Z or signed X canonical chart.
        const Vec2 delta = std::abs(std::sin(angle)) > std::abs(std::cos(angle))
            ? Vec2{static_cast<float>(-std::copysign(0.5, std::sin(angle))) * translation.z,
                   -0.5F * translation.y}
            : Vec2{0.5F * translation.x, -0.5F * translation.y};
        const auto mesh = prepared.compile_mesh().or_throw();
        const auto ids = target.vertex_ids();
        ASSERT_EQ(ids.size(), points.size());
        for (const auto& vertex : mesh.vertices) {
          const auto found = std::ranges::find(ids, vertex.source_vertex);
          ASSERT_NE(found, ids.end());
          const auto corner = static_cast<std::size_t>(found - ids.begin());
          const float developed_x = developed_start +
              ((corner == 1U || corner == 2U) ? developed_width : 0.0F);
          const auto expected = transformed_uv(
              strip_uv(developed_x + translation.x, points[corner].y + translation.y),
              settings);
          const auto actual = translated_uv(vertex, delta);
          EXPECT_NEAR(actual.x, expected.x, 1.0e-4F);
          EXPECT_NEAR(actual.y, expected.y, 1.0e-4F);
        }
        const auto identity = prepared.content_identity();
        const auto revision = prepared.content_revision();
        const auto bytes = serialize_qdr_authored_topology(prepared).or_throw();
        to.document = &prepared;
        const auto repeated = prepare_face_texture_transfer(from, to, 6U);
        ASSERT_TRUE(repeated.ok()) << repeated.error_message();
        EXPECT_FALSE(repeated.value().changed);
        const auto& repeated_document = repeated.value().documents.front().document;
        EXPECT_EQ(repeated_document.content_identity(), identity);
        EXPECT_EQ(repeated_document.content_revision(), revision);
        EXPECT_EQ(serialize_qdr_authored_topology(repeated_document).or_throw(), bytes);
        current = prepared;
        seam = end;
        developed_start += developed_width;
      }
    }
  }
}

TEST(UvPrecisionReliabilityTests,
     NearIdentitySamplingAtLargeLocalCoordinatesPreservesSubtexelContinuation) {
  constexpr float kOriginX = 1.0e6F;
  constexpr float kOriginY = 524288.0F;
  constexpr std::array<Vec3, 4> kLocalPoints{{
      {0.0F, 0.0F, 0.0F}, {4.0F, 0.0F, 0.0F},
      {4.0F, 3.0F, 0.0F}, {0.0F, 3.0F, 0.0F}}};
  std::array<Vec2, 4> source_uvs;
  for (std::size_t corner = 0U; corner < kLocalPoints.size(); ++corner) {
    source_uvs[corner] = strip_uv(kLocalPoints[corner].x, kLocalPoints[corner].y);
  }
  // Import the distant authored state directly. The strict face factory's
  // absolute-position area diagnostic is unrelated to this UV evaluation test.
  auto source_topology = precision_face(kLocalPoints, source_uvs)
                             .authored_topology().or_throw();
  for (auto& vertex : source_topology.vertices) {
    vertex.position.x += kOriginX;
    vertex.position.y += kOriginY;
  }
  const auto imported = PolygonDocument::from_authored_topology(source_topology);
  ASSERT_TRUE(imported.ok()) << imported.error_message();
  const auto source = imported.value();
  const auto target = precision_face(kLocalPoints);
  for (const float direction : {-1.0F, 1.0F}) {
    SCOPED_TRACE(direction);
    auto from = transfer_face(source, 1U, {});
    from.sampling_from_local = {
        .x_axis = {1.0F + direction * 0x1p-20F, 0.0F, 0.0F},
        .y_axis = {direction * 0x1p-21F, 1.0F - direction * 0x1p-20F, 0.0F},
        .origin = {-1.2F, 0.65F, 0.0F}};
    auto to = transfer_face(target, 2U, {});
    to.world_from_local.origin = {kOriginX, kOriginY, 0.0F};
    const auto result = prepare_face_texture_transfer(from, to, 6U);
    ASSERT_TRUE(result.ok()) << result.error_message();
    ASSERT_TRUE(result.value().changed);
    const auto& prepared = result.value().documents.front().document;
    const auto mesh = prepared.compile_mesh().or_throw();
    ASSERT_FALSE(mesh.vertices.empty());
    for (const auto& vertex : mesh.vertices) {
      const double source_x = static_cast<double>(kOriginX) + vertex.position.x;
      const double source_y = static_cast<double>(kOriginY) + vertex.position.y;
      const double displacement_x =
          (static_cast<double>(from.sampling_from_local.x_axis.x) - 1.0) * source_x +
          static_cast<double>(from.sampling_from_local.y_axis.x) * source_y +
          from.sampling_from_local.origin.x;
      const double displacement_y =
          (static_cast<double>(from.sampling_from_local.y_axis.y) - 1.0) * source_y +
          from.sampling_from_local.origin.y;
      const auto expected = strip_uv(
          static_cast<float>(vertex.position.x + displacement_x),
          static_cast<float>(vertex.position.y + displacement_y));
      EXPECT_NEAR(vertex.uv0.x, expected.x, 2.0e-6F);
      EXPECT_NEAR(vertex.uv0.y, expected.y, 2.0e-6F);
    }
    to.document = &prepared;
    const auto repeated = prepare_face_texture_transfer(from, to, 6U);
    ASSERT_TRUE(repeated.ok()) << repeated.error_message();
    EXPECT_FALSE(repeated.value().changed);
    EXPECT_EQ(repeated.value().documents.front().document.content_identity(),
              prepared.content_identity());
  }
}

TEST(UvPrecisionReliabilityTests,
     ObliqueNarrowAffineSourceRetainsItsCornersWhenTransferredToAnotherDocument) {
  constexpr float kWidth = 0x1p-18F;
  // Every binary coordinate lies exactly on x + y + z = 0. Projecting the
  // distant corners in float must not turn this affine chart into a warped one.
  constexpr std::array<Vec3, 4> kPoints{{
      {0.0F, 0.0F, 0.0F}, {4.0F, 4.0F, -8.0F},
      {4.0F, 4.0F + kWidth, -8.0F - kWidth}, {0.0F, kWidth, -kWidth}}};
  constexpr std::array<Vec2, 4> kUvs{{
      {0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}}};
  const auto source = precision_face(kPoints, kUvs);
  const auto target = PolygonDocument::make_face(kPoints, make_id<MaterialTag>(13U)).or_throw();
  const auto source_identity = source.content_identity();
  const auto source_revision = source.content_revision();
  const auto source_bytes = serialize_qdr_authored_topology(source).or_throw();
  const auto target_bytes = serialize_qdr_authored_topology(target).or_throw();
  const auto from = transfer_face(source, 1U, {});
  auto to = transfer_face(target, 2U, {});
  const auto result = prepare_face_texture_transfer(from, to, 6U);
  ASSERT_TRUE(result.ok()) << result.error_message();
  ASSERT_TRUE(result.value().changed);
  const auto& prepared = result.value().documents.front().document;
  EXPECT_TRUE(prepared.has_same_geometry(target));
  EXPECT_EQ(prepared.authored_topology().or_throw().faces.front().material_slot, 6U);
  const auto mesh = prepared.compile_mesh().or_throw();
  ASSERT_FALSE(mesh.vertices.empty());
  const auto ids = target.vertex_ids();
  ASSERT_EQ(ids.size(), kUvs.size());
  for (const auto& vertex : mesh.vertices) {
    const auto found = std::ranges::find(ids, vertex.source_vertex);
    ASSERT_NE(found, ids.end());
    const auto corner = static_cast<std::size_t>(found - ids.begin());
    EXPECT_NEAR(vertex.uv0.x, kUvs[corner].x, 2.0e-6F);
    EXPECT_NEAR(vertex.uv0.y, kUvs[corner].y, 2.0e-6F);
  }
  const auto prepared_bytes = serialize_qdr_authored_topology(prepared).or_throw();
  to.document = &prepared;
  const auto repeated = prepare_face_texture_transfer(from, to, 6U);
  ASSERT_TRUE(repeated.ok()) << repeated.error_message();
  EXPECT_FALSE(repeated.value().changed);
  const auto& repeated_document = repeated.value().documents.front().document;
  EXPECT_EQ(repeated_document.content_identity(), prepared.content_identity());
  EXPECT_EQ(repeated_document.content_revision(), prepared.content_revision());
  EXPECT_EQ(serialize_qdr_authored_topology(repeated_document).or_throw(), prepared_bytes);
  EXPECT_EQ(source.content_identity(), source_identity);
  EXPECT_EQ(source.content_revision(), source_revision);
  EXPECT_EQ(serialize_qdr_authored_topology(source).or_throw(), source_bytes);
  EXPECT_EQ(serialize_qdr_authored_topology(target).or_throw(), target_bytes);
}

} // namespace

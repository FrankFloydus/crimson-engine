////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include <quader/modeling/modeling.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace quader::modeling;

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

  Result<std::string> qdr = serialize_single_document_as_qdr(document);
  ASSERT_TRUE(qdr.ok()) << qdr.error_message();
  Result<QdrDocumentDto> round_trip = deserialize_qdr_document(qdr.value());
  ASSERT_TRUE(round_trip.ok()) << round_trip.error_message();
  ASSERT_EQ(round_trip.value().objects.size(), 1U);
  EXPECT_EQ(round_trip.value().objects.front().document.face_count(), 6U);

  Result<std::string> obj = serialize_obj_mesh(mesh.value(), "SDK Mesh");
  ASSERT_TRUE(obj.ok()) << obj.error_message();
  EXPECT_NE(obj.value().find("o SDK_Mesh"), std::string::npos);
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

  Result<std::string> qdr = serialize_single_document_as_qdr(document);
  ASSERT_TRUE(qdr.ok()) << qdr.error_message();
  Result<QdrDocumentDto> round_trip = deserialize_qdr_document(qdr.value());
  ASSERT_TRUE(round_trip.ok()) << round_trip.error_message();
  ASSERT_EQ(round_trip.value().objects.size(), 1U);
  EXPECT_EQ(round_trip.value().objects.front().document.vertex_count(), 8U);
  EXPECT_EQ(round_trip.value().objects.front().document.face_count(), 6U);
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

    Result<std::string> qdr = serialize_single_document_as_qdr(document);
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

} // namespace

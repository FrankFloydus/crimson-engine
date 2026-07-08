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

  std::string qdr = quader.io().serialize_qdr(mesh.id());
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

  BatchResult batch = quader.batch("Create and bevel")
                          .create_box({.name = "Batch Body"})
                          .as("body")
                          .with_mesh("body", [](MeshHandle mesh) {
                            OperationReceipt bevel =
                                mesh.edges()
                                    .all()
                                    .bevel({.width = 0.05F, .segments = 1})
                                    .commit("Batch Bevel");
                            ASSERT_TRUE(bevel.success) << bevel.error.message;
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
  EXPECT_TRUE(quader.mesh_summaries().empty());
  EXPECT_FALSE(quader.can_undo());
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

  const std::array<MeshHandle, 1> sources{second};
  OperationReceipt combined = first.combine_with(sources);
  ASSERT_TRUE(combined.success) << combined.error.message;
  EXPECT_TRUE(combined.changed);
  EXPECT_TRUE(first.valid());
  EXPECT_FALSE(second.valid());
  ASSERT_EQ(quader.mesh_summaries().size(), 1U);
  EXPECT_EQ(quader.mesh_summaries().front().id, first.id());

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

} // namespace

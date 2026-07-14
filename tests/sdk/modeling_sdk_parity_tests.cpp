////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "api/quader_editor_api.hpp"

#include <quader/modeling/modeling.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace {

/**
 * Stores comparable mesh bounds for native and SDK parity checks.
 */
struct MeshBounds {
  quader::QVec3 min{
      std::numeric_limits<float>::max(),
      std::numeric_limits<float>::max(),
      std::numeric_limits<float>::max(),
  };
  quader::QVec3 max{
      std::numeric_limits<float>::lowest(),
      std::numeric_limits<float>::lowest(),
      std::numeric_limits<float>::lowest(),
  };
};

[[nodiscard]] quader::QVec3 to_native_vec3(quader::modeling::Vec3 value) {
  return quader::QVec3{value.x, value.y, value.z};
}

void include_point(MeshBounds &bounds, quader::QVec3 point) {
  bounds.min.x = std::min(bounds.min.x, point.x);
  bounds.min.y = std::min(bounds.min.y, point.y);
  bounds.min.z = std::min(bounds.min.z, point.z);
  bounds.max.x = std::max(bounds.max.x, point.x);
  bounds.max.y = std::max(bounds.max.y, point.y);
  bounds.max.z = std::max(bounds.max.z, point.z);
}

[[nodiscard]] std::optional<MeshBounds>
mesh_bounds(const quader::QuaderMeshAsset &mesh) {
  if (mesh.vertices.empty()) {
    return std::nullopt;
  }

  MeshBounds bounds;
  for (const quader::QuaderMeshVertex &vertex : mesh.vertices) {
    include_point(bounds, vertex.position);
  }
  return bounds;
}

[[nodiscard]] std::optional<MeshBounds>
mesh_bounds(const quader::modeling::MeshPayload &mesh) {
  if (mesh.vertices.empty()) {
    return std::nullopt;
  }

  MeshBounds bounds;
  for (const quader::modeling::MeshVertexPayload &vertex : mesh.vertices) {
    include_point(bounds, to_native_vec3(vertex.position));
  }
  return bounds;
}

void expect_bounds_near(const MeshBounds &actual, const MeshBounds &expected,
                        float epsilon = 0.0001F) {
  EXPECT_NEAR(actual.min.x, expected.min.x, epsilon);
  EXPECT_NEAR(actual.min.y, expected.min.y, epsilon);
  EXPECT_NEAR(actual.min.z, expected.min.z, epsilon);
  EXPECT_NEAR(actual.max.x, expected.max.x, epsilon);
  EXPECT_NEAR(actual.max.y, expected.max.y, epsilon);
  EXPECT_NEAR(actual.max.z, expected.max.z, epsilon);
}

[[nodiscard]] std::filesystem::path unique_temp_qdr_path() {
  const auto stamp =
      std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("quader_modeling_sdk_parity_" + std::to_string(stamp) + ".qdr");
}

} // namespace

TEST(ModelingSdkParityTests,
     NativeApiAndSdkSessionMatchTranslatedBoxTranscript) {
  quader::QuaderStandaloneEditor native_editor =
      quader::create_standalone_editor();
  quader::QuaderEditorApi &editor = native_editor.editor();
  quader::QuaderApi &api = editor.api();

  quader::QuaderCreateBoxArgs args;
  args.bounds = {
      .min = {-1.0F, -1.0F, -1.0F},
      .max = {1.0F, 1.0F, 1.0F},
  };
  args.name = "Parity Box";

  const quader::QuaderApiResult<quader::QuaderModelingObjectRef>
      native_object = api.modeling().create_box(args);
  ASSERT_TRUE(native_object.ok()) << native_object.error_message();
  ASSERT_TRUE(api.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(api.selection().select_face(native_object.value(), 1).ok());
  ASSERT_TRUE(api.modeling().move_selection({1.0F, 0.0F, 0.0F}).ok());

  const std::optional<quader::QuaderMeshAsset> native_mesh =
      api.query().compiled_object_mesh(native_object.value());
  ASSERT_TRUE(native_mesh.has_value());
  const std::optional<MeshBounds> native_bounds = mesh_bounds(*native_mesh);
  ASSERT_TRUE(native_bounds.has_value());

  using namespace quader::modeling;
  Result<PolygonDocument> sdk_document = PolygonDocument::make_box({
      .min = {-1.0F, -1.0F, -1.0F},
      .max = {1.0F, 1.0F, 1.0F},
  });
  ASSERT_TRUE(sdk_document.ok()) << sdk_document.error_message();

  ModelingSession sdk_session;
  Result<ObjectId> sdk_object =
      sdk_session.add_document(std::move(sdk_document).value(), "Parity Box");
  ASSERT_TRUE(sdk_object.ok()) << sdk_object.error_message();
  ASSERT_TRUE(
      sdk_session.select_face(sdk_object.value(), make_id<FaceTag>(1)).ok());
  ASSERT_TRUE(sdk_session.translate_selection({1.0F, 0.0F, 0.0F}).ok());

  const PolygonDocument *sdk_committed_document =
      sdk_session.document(sdk_object.value());
  ASSERT_NE(sdk_committed_document, nullptr);
  Result<MeshPayload> sdk_mesh = sdk_committed_document->compile_mesh();
  ASSERT_TRUE(sdk_mesh.ok()) << sdk_mesh.error_message();
  const std::optional<MeshBounds> sdk_bounds = mesh_bounds(sdk_mesh.value());
  ASSERT_TRUE(sdk_bounds.has_value());

  EXPECT_EQ(native_mesh->vertices.size(), sdk_mesh.value().vertices.size());
  EXPECT_EQ(native_mesh->indices.size(), sdk_mesh.value().indices.size());
  EXPECT_EQ(native_mesh->primitives.size(), sdk_mesh.value().primitives.size());
  expect_bounds_near(*native_bounds, *sdk_bounds);

  const std::filesystem::path native_qdr = unique_temp_qdr_path();
  std::filesystem::remove(native_qdr);
  const quader::QuaderApiVoidResult saved =
      editor.scene_documents().save_as_native_path(
          *editor.scene_documents().active_scene_id(), native_qdr);
  ASSERT_TRUE(saved.ok()) << saved.error_message();
  EXPECT_TRUE(std::filesystem::exists(native_qdr));
  std::filesystem::remove(native_qdr);

  Result<std::vector<std::byte>> sdk_qdr =
      serialize_single_document_as_qdr(*sdk_committed_document);
  ASSERT_TRUE(sdk_qdr.ok()) << sdk_qdr.error_message();
  Result<QdrDocumentDto> sdk_round_trip =
      deserialize_qdr_document(sdk_qdr.value());
  ASSERT_TRUE(sdk_round_trip.ok()) << sdk_round_trip.error_message();
  ASSERT_EQ(sdk_round_trip.value().objects.size(), 1U);
  EXPECT_EQ(sdk_round_trip.value().objects.front().document.face_count(),
            sdk_committed_document->face_count());
}

TEST(ModelingSdkParityTests,
     NativeApiAndSdkPreviewCommitCancelTranscriptMatchBounds) {
  quader::QuaderStandaloneEditor native_editor =
      quader::create_standalone_editor();
  quader::QuaderEditorApi &editor = native_editor.editor();
  quader::QuaderApi &api = editor.api();

  quader::QuaderCreateBoxArgs args;
  args.bounds = {
      .min = {-1.0F, -1.0F, -1.0F},
      .max = {1.0F, 1.0F, 1.0F},
  };
  args.name = "Preview Parity Box";

  const quader::QuaderApiResult<quader::QuaderModelingObjectRef>
      native_object = api.modeling().create_box(args);
  ASSERT_TRUE(native_object.ok()) << native_object.error_message();
  ASSERT_TRUE(api.selection().select_object(native_object.value()).ok());
  ASSERT_TRUE(api.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(api.selection().select_face(native_object.value(), 1).ok());

  const std::optional<quader::QuaderMeshAsset> native_before_mesh =
      api.query().compiled_object_mesh(native_object.value());
  ASSERT_TRUE(native_before_mesh.has_value());
  const std::optional<MeshBounds> native_before =
      mesh_bounds(*native_before_mesh);
  ASSERT_TRUE(native_before.has_value());

  ASSERT_TRUE(api.modeling().begin_transform_preview(false).ok());
  ASSERT_TRUE(api.modeling().update_transform_translate({0.0F, 2.0F, 0.0F})
                  .ok());
  ASSERT_TRUE(api.modeling().commit_transform_preview("Preview parity").ok());

  const std::optional<quader::QuaderMeshAsset> native_committed_mesh =
      api.query().compiled_object_mesh(native_object.value());
  ASSERT_TRUE(native_committed_mesh.has_value());
  const std::optional<MeshBounds> native_committed =
      mesh_bounds(*native_committed_mesh);
  ASSERT_TRUE(native_committed.has_value());

  using namespace quader::modeling;
  Result<PolygonDocument> sdk_document = PolygonDocument::make_box({
      .min = {-1.0F, -1.0F, -1.0F},
      .max = {1.0F, 1.0F, 1.0F},
  });
  ASSERT_TRUE(sdk_document.ok()) << sdk_document.error_message();

  ModelingSession sdk_session;
  Result<ObjectId> sdk_object = sdk_session.add_document(
      std::move(sdk_document).value(), "Preview Parity Box");
  ASSERT_TRUE(sdk_object.ok()) << sdk_object.error_message();
  ASSERT_TRUE(sdk_session.select_object(sdk_object.value()).ok());
  ASSERT_TRUE(
      sdk_session.select_face(sdk_object.value(), make_id<FaceTag>(1)).ok());

  Result<OperationResult> begin =
      sdk_session.begin_translate_preview(sdk_object.value(),
                                          {0.0F, 2.0F, 0.0F});
  ASSERT_TRUE(begin.ok()) << begin.error_message();
  ASSERT_TRUE(sdk_session.preview_active());
  Result<ToolPreviewPayload> preview = sdk_session.preview_payload();
  ASSERT_TRUE(preview.ok()) << preview.error_message();
  EXPECT_TRUE(preview.value().valid);
  ASSERT_TRUE(sdk_session.commit_preview().ok());

  const PolygonDocument *sdk_committed_document =
      sdk_session.document(sdk_object.value());
  ASSERT_NE(sdk_committed_document, nullptr);
  Result<MeshPayload> sdk_committed_mesh =
      sdk_committed_document->compile_mesh();
  ASSERT_TRUE(sdk_committed_mesh.ok()) << sdk_committed_mesh.error_message();
  const std::optional<MeshBounds> sdk_committed =
      mesh_bounds(sdk_committed_mesh.value());
  ASSERT_TRUE(sdk_committed.has_value());

  expect_bounds_near(*native_committed, *sdk_committed);

  ASSERT_TRUE(api.modeling().begin_transform_preview(false).ok());
  ASSERT_TRUE(api.modeling().update_transform_translate({0.0F, 5.0F, 0.0F})
                  .ok());
  ASSERT_TRUE(api.modeling().cancel_transform_preview().ok());
  const std::optional<quader::QuaderMeshAsset> native_after_cancel_mesh =
      api.query().compiled_object_mesh(native_object.value());
  ASSERT_TRUE(native_after_cancel_mesh.has_value());
  const std::optional<MeshBounds> native_after_cancel =
      mesh_bounds(*native_after_cancel_mesh);
  ASSERT_TRUE(native_after_cancel.has_value());
  expect_bounds_near(*native_after_cancel, *native_committed);

  ASSERT_TRUE(sdk_session.begin_translate_preview(sdk_object.value(),
                                                  {0.0F, 5.0F, 0.0F})
                  .ok());
  ASSERT_TRUE(sdk_session.cancel_preview().ok());
  const PolygonDocument *sdk_after_cancel_document =
      sdk_session.document(sdk_object.value());
  ASSERT_NE(sdk_after_cancel_document, nullptr);
  Result<MeshPayload> sdk_after_cancel_mesh =
      sdk_after_cancel_document->compile_mesh();
  ASSERT_TRUE(sdk_after_cancel_mesh.ok())
      << sdk_after_cancel_mesh.error_message();
  const std::optional<MeshBounds> sdk_after_cancel =
      mesh_bounds(sdk_after_cancel_mesh.value());
  ASSERT_TRUE(sdk_after_cancel.has_value());
  expect_bounds_near(*sdk_after_cancel, *sdk_committed);
}

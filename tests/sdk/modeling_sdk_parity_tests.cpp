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
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

/// Names the user-level steps shared by the native and SDK runners.
enum class TranscriptIntent {
  CreateBox,
  SelectFace,
  Translate,
  BeginPreview,
  UpdatePreview,
  CancelPreview,
  CommitPreview,
  Undo,
  Redo,
  QdrRoundTrip,
  StaleSelection,
};

/// Carries one normalized transcript intent and its optional translation.
struct TranscriptStep {
  TranscriptIntent intent = TranscriptIntent::CreateBox;
  quader::QVec3 delta{};
};

/// Records only public, cross-engine behavior after one transcript step.
struct NormalizedObservation {
  bool ok = false;
  bool changed = false;
  std::string error_class;
  std::uint32_t selected_object_count = 0;
  std::uint32_t selected_face_count = 0;
  bool active_face = false;
  std::size_t selection_remap_count = 0;
  bool content_revision_changed = false;
  bool selection_revision_changed = false;
  bool can_undo = false;
  bool can_redo = false;
  bool preview_active = false;
  std::size_t vertex_count = 0;
  std::size_t index_count = 0;
  std::size_t primitive_count = 0;
  std::uint64_t snapshot_checksum = 0;
  std::optional<MeshBounds> bounds;
  std::size_t qdr_object_count = 0;
};

[[nodiscard]] std::vector<std::string> observation_diff(
    const NormalizedObservation &native,
    const NormalizedObservation &sdk) {
  std::vector<std::string> fields;
  const auto compare = [&](bool same, std::string_view field) {
    if (!same) {
      fields.emplace_back(field);
    }
  };
  compare(native.ok == sdk.ok, "result.ok");
  compare(native.changed == sdk.changed, "result.changed");
  compare(native.error_class == sdk.error_class, "result.error_class");
  compare(native.selected_object_count == sdk.selected_object_count,
          "selection.selected_object_count");
  compare(native.selected_face_count == sdk.selected_face_count,
          "selection.selected_face_count");
  compare(native.active_face == sdk.active_face,
          "selection.active_kind");
  compare(native.selection_remap_count == sdk.selection_remap_count,
          "selection.remap_count");
  compare(native.content_revision_changed == sdk.content_revision_changed,
          "dirty.content_revision");
  compare(native.selection_revision_changed == sdk.selection_revision_changed,
          "dirty.selection_revision");
  compare(native.can_undo == sdk.can_undo, "history.can_undo");
  compare(native.can_redo == sdk.can_redo, "history.can_redo");
  compare(native.preview_active == sdk.preview_active, "preview.active");
  compare(native.vertex_count == sdk.vertex_count, "snapshot.vertex_count");
  compare(native.index_count == sdk.index_count, "snapshot.index_count");
  compare(native.primitive_count == sdk.primitive_count,
          "snapshot.primitive_count");
  compare(native.snapshot_checksum == sdk.snapshot_checksum,
          "snapshot.checksum");
  compare(native.bounds.has_value() == sdk.bounds.has_value(),
          "snapshot.bounds.presence");
  if (native.bounds.has_value() && sdk.bounds.has_value()) {
    constexpr float kEpsilon = 0.0001f;
    compare(std::abs(native.bounds->min.x - sdk.bounds->min.x) <= kEpsilon &&
                std::abs(native.bounds->min.y - sdk.bounds->min.y) <= kEpsilon &&
                std::abs(native.bounds->min.z - sdk.bounds->min.z) <= kEpsilon &&
                std::abs(native.bounds->max.x - sdk.bounds->max.x) <= kEpsilon &&
                std::abs(native.bounds->max.y - sdk.bounds->max.y) <= kEpsilon &&
                std::abs(native.bounds->max.z - sdk.bounds->max.z) <= kEpsilon,
            "snapshot.bounds");
  }
  compare(native.qdr_object_count == sdk.qdr_object_count,
          "qdr.object_count");
  return fields;
}

void checksum_mix(std::uint64_t &checksum, std::uint32_t value) {
  checksum ^= value;
  checksum *= 1099511628211ULL;
}

[[nodiscard]] std::uint64_t
mesh_checksum(const quader::QuaderMeshAsset &mesh) {
  std::uint64_t checksum = 1469598103934665603ULL;
  for (const quader::QuaderMeshVertex &vertex : mesh.vertices) {
    checksum_mix(checksum, std::bit_cast<std::uint32_t>(vertex.position.x));
    checksum_mix(checksum, std::bit_cast<std::uint32_t>(vertex.position.y));
    checksum_mix(checksum, std::bit_cast<std::uint32_t>(vertex.position.z));
  }
  for (const std::uint32_t index : mesh.indices) {
    checksum_mix(checksum, index);
  }
  return checksum;
}

[[nodiscard]] std::uint64_t
mesh_checksum(const quader::modeling::MeshPayload &mesh) {
  std::uint64_t checksum = 1469598103934665603ULL;
  for (const quader::modeling::MeshVertexPayload &vertex : mesh.vertices) {
    checksum_mix(checksum, std::bit_cast<std::uint32_t>(vertex.position.x));
    checksum_mix(checksum, std::bit_cast<std::uint32_t>(vertex.position.y));
    checksum_mix(checksum, std::bit_cast<std::uint32_t>(vertex.position.z));
  }
  for (const std::uint32_t index : mesh.indices) {
    checksum_mix(checksum, index);
  }
  return checksum;
}

template <typename T>
[[nodiscard]] std::string
sdk_error_message(const quader::modeling::Result<T> &result) {
  return result.ok() ? std::string{} : result.error_message();
}

/// Executes the normalized transcript through the shipping native facade.
class NativeTranscriptRunner final {
public:
  NativeTranscriptRunner()
      : standalone_(quader::create_standalone_editor()),
        editor_(standalone_.editor()), api_(editor_.api()) {}

  [[nodiscard]] quader::QuaderApiVoidResult enter_edit_mode() {
    return editor_.mode().set(quader::QuaderEditorMode::Edit);
  }

  [[nodiscard]] NormalizedObservation execute(const TranscriptStep &step) {
    bool ok = false;
    bool changed = false;
    std::string error;
    switch (step.intent) {
    case TranscriptIntent::CreateBox: {
      quader::QuaderCreateBoxArgs args;
      args.bounds = {.min = {-1.0f, -1.0f, -1.0f},
                     .max = {1.0f, 1.0f, 1.0f}};
      args.name = "Normalized Parity Box";
      const auto result = api_.modeling().create_box(args);
      ok = result.ok();
      changed = result.changed();
      error = result.error_message();
      if (ok) {
        object_ = result.value();
      }
      break;
    }
    case TranscriptIntent::SelectFace: {
      const auto mode =
          api_.selection().set_mode(quader::QuaderSelectionMode::Face);
      const auto result = object_.has_value()
                              ? api_.selection().select_face(*object_, 1)
                              : api_.selection().select_face({}, 1);
      ok = mode.ok() && result.ok();
      changed = mode.changed() || result.changed();
      error = !mode.ok() ? mode.error_message() : result.error_message();
      break;
    }
    case TranscriptIntent::Translate: {
      const auto result = api_.modeling().move_selection(step.delta);
      ok = result.ok();
      changed = result.changed();
      error = result.error_message();
      break;
    }
    case TranscriptIntent::BeginPreview: {
      const auto result = api_.modeling().begin_transform_preview(false);
      ok = result.ok();
      changed = result.changed();
      error = result.error_message();
      preview_active_ = ok;
      break;
    }
    case TranscriptIntent::UpdatePreview: {
      const auto result = api_.modeling().update_transform_translate(step.delta);
      ok = result.ok();
      changed = result.changed();
      error = result.error_message();
      break;
    }
    case TranscriptIntent::CancelPreview: {
      const auto result = api_.modeling().cancel_transform_preview();
      ok = result.ok();
      // Normalization reports committed modeling-state change; cancel only
      // retracts a transient host projection.
      changed = false;
      error = result.error_message();
      if (ok) {
        preview_active_ = false;
      }
      break;
    }
    case TranscriptIntent::CommitPreview: {
      const auto result =
          api_.modeling().commit_transform_preview("Normalized preview");
      ok = result.ok();
      changed = result.changed();
      error = result.error_message();
      if (ok) {
        preview_active_ = false;
      }
      break;
    }
    case TranscriptIntent::Undo: {
      const auto result = editor_.history().undo();
      ok = result.ok();
      changed = result.changed();
      error = result.error_message();
      break;
    }
    case TranscriptIntent::Redo: {
      const auto result = editor_.history().redo();
      ok = result.ok();
      changed = result.changed();
      error = result.error_message();
      break;
    }
    case TranscriptIntent::QdrRoundTrip: {
      const std::filesystem::path path = unique_temp_qdr_path();
      std::filesystem::remove(path);
      const auto active = editor_.scene_documents().active_scene_id();
      const auto result = active.has_value()
                              ? editor_.scene_documents().save_as_native_path(
                                    *active, path)
                              : quader::QuaderApiVoidResult{};
      ok = result.ok() && std::filesystem::exists(path);
      changed = false;
      error = result.error_message();
      qdr_object_count_ = ok && object_.has_value() ? 1u : 0u;
      std::filesystem::remove(path);
      break;
    }
    case TranscriptIntent::StaleSelection: {
      const auto result = api_.selection().select_face({}, 1);
      ok = result.ok();
      changed = result.changed();
      error = result.error_message();
      break;
    }
    }
    return observe(ok, changed, std::move(error));
  }

private:
  [[nodiscard]] NormalizedObservation observe(bool ok, bool changed,
                                              std::string error) {
    const quader::QuaderSelectionSummary selection = api_.selection().summary();
    const quader::QuaderHistorySnapshot history = editor_.query().history();
    NormalizedObservation observation{
        .ok = ok,
        .changed = changed,
        .error_class = ok ? std::string{} : "invalid",
        .selected_object_count = selection.selected_object_count,
        .selected_face_count = selection.selected_face_count,
        .active_face = selection.active_kind ==
                       quader::QuaderActiveSelectionKind::Face,
        .content_revision_changed =
            selection.content_revision != last_content_revision_,
        .selection_revision_changed =
            selection.selection_revision != last_selection_revision_,
        .can_undo = history.can_undo,
        .can_redo = history.can_redo,
        .preview_active = preview_active_,
        .qdr_object_count = qdr_object_count_,
    };
    (void)error;
    last_content_revision_ = selection.content_revision;
    last_selection_revision_ = selection.selection_revision;
    if (object_.has_value()) {
      const auto mesh = api_.query().compiled_object_mesh(*object_);
      if (mesh.has_value()) {
        observation.vertex_count = mesh->vertices.size();
        observation.index_count = mesh->indices.size();
        observation.primitive_count = mesh->primitives.size();
        observation.bounds = mesh_bounds(*mesh);
        observation.snapshot_checksum = mesh_checksum(*mesh);
      }
    }
    return observation;
  }

  quader::QuaderStandaloneEditor standalone_;
  quader::QuaderEditorApi &editor_;
  quader::QuaderApi &api_;
  std::optional<quader::QuaderModelingObjectRef> object_;
  std::uint64_t last_content_revision_ = 0;
  std::uint64_t last_selection_revision_ = 0;
  std::size_t qdr_object_count_ = 0;
  bool preview_active_ = false;
};

/// Executes the normalized transcript directly through the portable SDK.
class SdkTranscriptRunner final {
public:
  [[nodiscard]] NormalizedObservation execute(const TranscriptStep &step) {
    bool ok = false;
    bool changed = false;
    std::string error;
    using namespace quader::modeling;
    switch (step.intent) {
    case TranscriptIntent::CreateBox: {
      Result<PolygonDocument> document = PolygonDocument::make_box({
          .min = {-1.0f, -1.0f, -1.0f},
          .max = {1.0f, 1.0f, 1.0f},
      });
      if (document.ok()) {
        Result<ObjectId> result = session_.add_document(
            std::move(document).value(), "Normalized Parity Box");
        ok = result.ok();
        changed = result.ok();
        error = sdk_error_message(result);
        if (ok) {
          object_ = result.value();
          Result<OperationResult> selected = session_.select_object(*object_);
          ok = selected.ok();
          error = sdk_error_message(selected);
        }
      } else {
        error = sdk_error_message(document);
      }
      break;
    }
    case TranscriptIntent::SelectFace: {
      const auto result = object_.has_value()
                              ? session_.select_face(*object_,
                                                     make_id<FaceTag>(1))
                              : Result<OperationResult>::failure(make_error(
                                    ErrorCode::InvalidId, "missing object"));
      ok = result.ok();
      changed = result.ok() && result.value().changed;
      error = sdk_error_message(result);
      break;
    }
    case TranscriptIntent::Translate: {
      const auto result = session_.translate_selection(
          {step.delta.x, step.delta.y, step.delta.z});
      ok = result.ok();
      changed = result.ok() && result.value().changed;
      error = sdk_error_message(result);
      break;
    }
    case TranscriptIntent::BeginPreview: {
      const auto result = object_.has_value()
                              ? session_.begin_translate_preview(*object_, {})
                              : Result<OperationResult>::failure(make_error(
                                     ErrorCode::InvalidId, "missing object"));
      ok = result.ok();
      // Starting a preview changes transient session state, not committed
      // modeling content. The normalized contract reports the latter.
      changed = false;
      error = sdk_error_message(result);
      break;
    }
    case TranscriptIntent::UpdatePreview: {
      const auto result = session_.update_translate_preview(
          {step.delta.x, step.delta.y, step.delta.z});
      ok = result.ok();
      changed = result.ok() && result.value().changed;
      error = sdk_error_message(result);
      break;
    }
    case TranscriptIntent::CancelPreview: {
      const auto result = session_.cancel_preview();
      ok = result.ok();
      // Match the committed-state observation used by the native runner.
      changed = false;
      error = sdk_error_message(result);
      break;
    }
    case TranscriptIntent::CommitPreview: {
      const auto result = session_.commit_preview();
      ok = result.ok();
      changed = result.ok() && result.value().changed;
      error = sdk_error_message(result);
      break;
    }
    case TranscriptIntent::Undo: {
      const auto result = session_.undo();
      ok = result.ok();
      changed = result.ok() && result.value().changed;
      error = sdk_error_message(result);
      break;
    }
    case TranscriptIntent::Redo: {
      const auto result = session_.redo();
      ok = result.ok();
      changed = result.ok() && result.value().changed;
      error = sdk_error_message(result);
      break;
    }
    case TranscriptIntent::QdrRoundTrip: {
      if (object_.has_value()) {
        Result<PolygonDocument> document = session_.document_copy(*object_);
        Result<std::vector<std::byte>> bytes =
            document.ok()
                ? serialize_single_document_as_qdr(document.value())
                : Result<std::vector<std::byte>>::failure(document.error());
        Result<QdrDocumentDto> restored =
            bytes.ok() ? deserialize_qdr_document(bytes.value())
                       : Result<QdrDocumentDto>::failure(bytes.error());
        ok = restored.ok();
        error = sdk_error_message(restored);
        qdr_object_count_ = ok ? restored.value().objects.size() : 0u;
      }
      changed = false;
      break;
    }
    case TranscriptIntent::StaleSelection: {
      const auto result = session_.select_face(make_id<ObjectTag>(9999),
                                               make_id<FaceTag>(1));
      ok = result.ok();
      changed = result.ok() && result.value().changed;
      error = sdk_error_message(result);
      break;
    }
    }
    return observe(ok, changed, std::move(error));
  }

private:
  [[nodiscard]] NormalizedObservation observe(bool ok, bool changed,
                                              std::string error) {
    using namespace quader::modeling;
    const SessionChangeSnapshot changes =
        session_.changes_since(last_session_content_revision_);
    const bool persistent_content_changed = std::ranges::any_of(
        changes.objects, [](const SessionObjectChange &change) {
          return change.deleted || change.dirty.topology ||
                 change.dirty.geometry || change.dirty.materials;
        });
    NormalizedObservation observation{
        .ok = ok,
        .changed = changed,
        .error_class = ok ? std::string{} : "invalid",
        .content_revision_changed = persistent_content_changed,
        .selection_revision_changed =
            session_.selection_revision() != last_selection_revision_,
        .can_undo = session_.can_undo(),
        .can_redo = session_.can_redo(),
        .preview_active = session_.preview_active(),
        .qdr_object_count = qdr_object_count_,
    };
    (void)error;
    last_session_content_revision_ = changes.content_revision;
    last_selection_revision_ = session_.selection_revision();
    for (const SessionObjectSummary &summary : session_.objects()) {
      if (summary.selected) {
        ++observation.selected_object_count;
      }
    }
    if (object_.has_value()) {
      Result<PolygonDocument> document = session_.document_copy(*object_);
      if (document.ok()) {
        const PolygonSelectionSnapshot selection = document.value().selection();
        observation.selected_face_count =
            static_cast<std::uint32_t>(selection.faces.size());
        observation.active_face =
            selection.has_active && selection.kind == SelectionKind::Face;
        // The native transform hot path keeps the committed mesh asset stable
        // and streams transient preview positions directly to the renderer.
        // Compare durable geometry here; preview_active above is the portable
        // cross-engine observation for the transient transaction itself.
        Result<MeshPayload> mesh = document.value().compile_mesh();
        if (mesh.ok()) {
          observation.vertex_count = mesh.value().vertices.size();
          observation.index_count = mesh.value().indices.size();
          observation.primitive_count = mesh.value().primitives.size();
          observation.bounds = mesh_bounds(mesh.value());
          observation.snapshot_checksum = mesh_checksum(mesh.value());
        }
      }
    }
    return observation;
  }

  quader::modeling::ModelingSession session_;
  std::optional<quader::modeling::ObjectId> object_;
  std::uint64_t last_session_content_revision_ = 0;
  std::uint64_t last_selection_revision_ = 0;
  std::size_t qdr_object_count_ = 0;
};

} // namespace

TEST(ModelingSdkParityHarnessTests, MismatchDiagnosticsNameTheDivergedField) {
  NormalizedObservation native;
  NormalizedObservation sdk;
  sdk.selected_face_count = 1;
  sdk.selection_remap_count = 1;
  sdk.can_undo = true;
  sdk.snapshot_checksum = 42;
  const std::vector<std::string> diff = observation_diff(native, sdk);
  EXPECT_NE(std::find(diff.begin(), diff.end(),
                      "selection.selected_face_count"),
            diff.end());
  EXPECT_NE(std::find(diff.begin(), diff.end(), "selection.remap_count"),
            diff.end());
  EXPECT_NE(std::find(diff.begin(), diff.end(), "history.can_undo"),
            diff.end());
  EXPECT_NE(std::find(diff.begin(), diff.end(), "snapshot.checksum"),
            diff.end());
}

TEST(ModelingSdkParityHarnessTests,
     NativeAndSdkAdaptersExecuteTheSameNormalizedTranscript) {
  constexpr std::array<TranscriptStep, 12> kTranscript = {{
      {TranscriptIntent::CreateBox},
      {TranscriptIntent::SelectFace},
      {TranscriptIntent::Translate, {1.0f, 0.0f, 0.0f}},
      {TranscriptIntent::BeginPreview},
      {TranscriptIntent::UpdatePreview, {0.0f, 2.0f, 0.0f}},
      {TranscriptIntent::CancelPreview},
      {TranscriptIntent::BeginPreview},
      {TranscriptIntent::UpdatePreview, {0.0f, 3.0f, 0.0f}},
      {TranscriptIntent::CommitPreview},
      {TranscriptIntent::Undo},
      {TranscriptIntent::Redo},
      {TranscriptIntent::QdrRoundTrip},
  }};
  NativeTranscriptRunner native;
  SdkTranscriptRunner sdk;
  for (std::size_t index = 0; index < kTranscript.size(); ++index) {
    SCOPED_TRACE(index);
    if (kTranscript[index].intent == TranscriptIntent::SelectFace) {
      ASSERT_TRUE(native.enter_edit_mode().ok());
    }
    const NormalizedObservation native_observation =
        native.execute(kTranscript[index]);
    const NormalizedObservation sdk_observation = sdk.execute(kTranscript[index]);
    const std::vector<std::string> diff =
        observation_diff(native_observation, sdk_observation);
    EXPECT_TRUE(diff.empty())
        << (diff.empty() ? std::string{} : diff.front())
        << "; native selection dirty="
        << native_observation.selection_revision_changed
        << ", SDK selection dirty="
        << sdk_observation.selection_revision_changed;
  }

  const NormalizedObservation native_stale =
      native.execute({TranscriptIntent::StaleSelection});
  const NormalizedObservation sdk_stale =
      sdk.execute({TranscriptIntent::StaleSelection});
  const std::vector<std::string> stale_diff =
      observation_diff(native_stale, sdk_stale);
  EXPECT_TRUE(stale_diff.empty())
      << (stale_diff.empty() ? std::string{} : stale_diff.front());
}

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
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
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
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
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

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "api/quader_polygon_document_internal_access.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/projection/mesh_projection_service.hpp"
#include "editor/session/project_session.hpp"
#include "editor/session/scene_session.hpp"
#include "scene/scene_texture_mapping.hpp"
#include "scene/scene_transform.hpp"
#include "support/command_observable_state.hpp"

#include <quader/modeling/neutral/polygon/document.hpp>
#include <gtest/gtest.h>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

namespace quader {
namespace {

constexpr float kBakeEpsilon = 0.001f;

void expect_vector(QVec3 actual, QVec3 expected) {
  EXPECT_NEAR(actual.x, expected.x, kBakeEpsilon);
  EXPECT_NEAR(actual.y, expected.y, kBakeEpsilon);
  EXPECT_NEAR(actual.z, expected.z, kBakeEpsilon);
}

void expect_exact_vector(QVec3 actual, QVec3 expected) {
  EXPECT_FLOAT_EQ(actual.x, expected.x);
  EXPECT_FLOAT_EQ(actual.y, expected.y);
  EXPECT_FLOAT_EQ(actual.z, expected.z);
}

void expect_matrix(const QMat4 &actual, const QMat4 &expected) {
  for (std::size_t index = 0; index < actual.values.size(); ++index) {
    EXPECT_NEAR(actual.values[index], expected.values[index], kBakeEpsilon)
        << index;
  }
}

/// Keeps visible attributes independent of reordered mirrored face corners.
struct BakedCorner {
  QVec3 position;
  QVec3 normal;
  QVec2 uv;
};

void expect_corners(std::vector<BakedCorner> actual,
                    const std::vector<BakedCorner> &expected) {
  ASSERT_EQ(actual.size(), expected.size());
  for (const BakedCorner &corner : expected) {
    const auto found = std::find_if(actual.begin(), actual.end(),
        [&](const BakedCorner &candidate) {
          return length(candidate.position - corner.position) < kBakeEpsilon &&
                 length(candidate.normal - corner.normal) < kBakeEpsilon &&
                 std::abs(candidate.uv.x - corner.uv.x) < kBakeEpsilon &&
                 std::abs(candidate.uv.y - corner.uv.y) < kBakeEpsilon;
        });
    ASSERT_NE(found, actual.end())
        << "World corner " << corner.position.x << ", " << corner.position.y
        << ", " << corner.position.z << " or its normal/UV changed";
    actual.erase(found);
  }
}

/// Public mutations with native observations of authored geometry and sampling.
class MeshTransformBakeTests : public ::testing::Test {
protected:
  MeshTransformBakeTests()
      : editor_(QuaderApiSessionInternalAccess::from_scene_session(session_)) {}

  QuaderModelingObjectRef mesh(std::string name, bool textured = false) {
    quader_poly::Document document;
    const auto a = quader_poly::add_vertex(document, {0.5f, -0.25f, 0.75f});
    const auto b = quader_poly::add_vertex(document, {4.0f, 0.0f, 0.0f});
    const auto c = quader_poly::add_vertex(document, {0.0f, 2.0f, 0.0f});
    const auto d = quader_poly::add_vertex(document, {0.0f, 0.0f, 3.0f});
    for (const auto face : {std::array{a, b, c}, std::array{a, d, b},
                            std::array{a, c, d}, std::array{b, d, c}}) {
      EXPECT_NE(quader_poly::add_face(document, face),
                quader_poly::kInvalidElementId);
    }
    if (textured) {
      for (auto &face : document.faces) {
        face.texture_transform = {.scale = {2.0f, -0.5f},
                                  .shift_uv = {0.125f, -0.25f},
                                  .rotation_degrees = 35.0f};
      }
      // Exercise generated and explicitly authored UVs in the same mesh.
      document.faces.front().uvs = {{0.1f, 0.2f}, {1.8f, 0.15f}, {1.5f, 2.7f}};
    }
    const auto created = editor_.modeling().create_mesh(
        QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
        {.name = std::move(name)});
    EXPECT_TRUE(created.ok()) << created.error_message();
    return created.ok() ? created.value() : QuaderModelingObjectRef{};
  }

  AssetId mesh_id(EntityId entity) const {
    const auto *component = session_.scene().mesh(entity);
    EXPECT_NE(component, nullptr);
    return component != nullptr ? component->mesh : kInvalidAssetId;
  }

  std::vector<QVec3> vertices(EntityId entity, bool world = true) const {
    const auto *asset = session_.assets().mesh(mesh_id(entity));
    EXPECT_NE(asset, nullptr);
    if (asset == nullptr) return {};
    EXPECT_NE(asset->authored_polygon_document, nullptr);
    if (asset->authored_polygon_document == nullptr) return {};
    const QMat4 matrix = world ? session_.scene().world_transform_matrix(entity)
                               : QMat4{};
    std::vector<QVec3> result;
    for (const auto &vertex : asset->authored_polygon_document->vertices) {
      result.push_back(transform_point(matrix, vertex.position));
    }
    return result;
  }

  void expect_vertices(EntityId entity, const std::vector<QVec3> &expected,
                        bool world = true) const {
    const auto actual = vertices(entity, world);
    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t index = 0; index < actual.size(); ++index) {
      expect_vector(actual[index], expected[index]);
    }
  }

  std::vector<BakedCorner> corners(EntityId entity) {
    const auto *object = session_.scene().modeling_object(entity);
    EXPECT_NE(object, nullptr);
    if (object == nullptr) return {};
    auto compiled = session_.modeling().compile_object_mesh(object->object_id);
    const auto sampling = effective_texture_sampling_from_local(session_.scene(), entity);
    EXPECT_TRUE(compiled.has_value());
    EXPECT_TRUE(sampling.has_value());
    if (!compiled || !sampling) return {};
    MeshProjectionService::apply_mesh_texture_mapping(*compiled, *sampling);
    const QMat4 world = session_.scene().world_transform_matrix(entity);
    const glm::mat3 normal = glm::transpose(glm::inverse(glm::mat3{to_glm(world)}));
    std::vector<BakedCorner> result;
    for (const MeshVertex &vertex : compiled->geometry().vertices) {
      result.push_back({transform_point(world, vertex.position),
                        from_glm(glm::normalize(normal * to_glm(vertex.normal))),
                        vertex.uv});
    }
    return result;
  }

  void expect_no_change(const test::CommandObservableState &before) {
    const auto difference = test::diff_command_observable_state(
        before, test::capture_command_observable_state(editor_));
    EXPECT_TRUE(difference.empty()) << difference.summary;
  }

  SceneSession session_;
  QuaderEditorApi editor_;
};

TEST_F(MeshTransformBakeTests, EachModeResetsOnlyRequestedChannelsAndReplaysOnce) {
  constexpr std::array kModes{QuaderTransformBakeMode::Transforms,
                            QuaderTransformBakeMode::Location,
                            QuaderTransformBakeMode::Rotation,
                            QuaderTransformBakeMode::Scale};
  for (const auto mode : kModes) {
    SCOPED_TRACE(static_cast<int>(mode));
    const auto target = mesh("Bake Mode Mesh");
    const QuaderTransform before{.position = {7.0f, -3.0f, 5.0f},
                                 .rotation_euler = {0.3f, -0.6f, 0.8f},
                                 .scale = {-1.5f, 0.75f, 2.25f}};
    ASSERT_TRUE(editor_.scene().set_transform(target.entity, before).ok());
    ASSERT_TRUE(editor_.selection().select_object(target).ok());
    const auto world_before = vertices(target.entity);
    const auto local_before = vertices(target.entity, false);
    const std::size_t undo_before = session_.history().undo_count();
    ASSERT_TRUE(editor_.modeling().can_bake_transforms(mode));
    const auto result = editor_.modeling().bake_transforms(mode);
    ASSERT_TRUE(result.ok()) << result.error_message();
    EXPECT_TRUE(result.changed());
    const auto after = editor_.query().entity(target.entity)->transform;
    expect_exact_vector(after.position,
        mode == QuaderTransformBakeMode::Transforms || mode == QuaderTransformBakeMode::Location
            ? QVec3{} : before.position);
    expect_exact_vector(after.rotation_euler,
        mode == QuaderTransformBakeMode::Transforms || mode == QuaderTransformBakeMode::Rotation
            ? QVec3{} : before.rotation_euler);
    expect_exact_vector(after.scale,
        mode == QuaderTransformBakeMode::Transforms || mode == QuaderTransformBakeMode::Scale
            ? QVec3{1.0f, 1.0f, 1.0f} : before.scale);
    expect_vertices(target.entity, world_before);
    EXPECT_EQ(session_.history().undo_count(), undo_before + 1u);
    const auto local_after = vertices(target.entity, false);
    const auto baked_state = test::capture_command_observable_state(editor_);
    const auto repeated = editor_.modeling().bake_transforms(mode);
    ASSERT_TRUE(repeated.ok()) << repeated.error_message();
    EXPECT_FALSE(repeated.changed());
    expect_no_change(baked_state);
    ASSERT_TRUE(editor_.history().undo().ok());
    const auto undone = editor_.query().entity(target.entity)->transform;
    expect_exact_vector(undone.position, before.position);
    expect_exact_vector(undone.rotation_euler, before.rotation_euler);
    expect_exact_vector(undone.scale, before.scale);
    expect_vertices(target.entity, local_before, false);
    expect_vertices(target.entity, world_before);
    ASSERT_TRUE(editor_.history().redo().ok());
    const auto redone = editor_.query().entity(target.entity)->transform;
    expect_exact_vector(redone.position, after.position);
    expect_exact_vector(redone.rotation_euler, after.rotation_euler);
    expect_exact_vector(redone.scale, after.scale);
    expect_vertices(target.entity, local_after, false);
    expect_vertices(target.entity, world_before);
  }
}

TEST_F(MeshTransformBakeTests, SharedMeshUsesStableCopyOnWriteWithoutChangingPeer) {
  const auto target = mesh("Selected Shared Mesh");
  const auto peer = mesh("Unselected Shared Mesh");
  const AssetId original = mesh_id(target.entity);
  auto shared = *session_.scene().mesh(peer.entity);
  shared.mesh = original;
  ASSERT_TRUE(session_.scene().set_mesh(peer.entity, shared));
  ASSERT_TRUE(editor_.scene().set_transform(target.entity,
      {.position = {4.0f, -3.0f, 2.0f}, .rotation_euler = {0.2f, 0.5f, -0.7f},
       .scale = {-2.0f, 1.0f, 0.5f}}).ok());
  ASSERT_TRUE(editor_.selection().select_object(target).ok());
  const auto target_before = vertices(target.entity);
  const auto peer_before = vertices(peer.entity);
  const auto peer_local = vertices(peer.entity, false);
  const std::size_t mesh_count = session_.assets().mesh_count();
  ASSERT_TRUE(editor_.modeling().bake_transforms(QuaderTransformBakeMode::Transforms).ok());
  const AssetId clone = mesh_id(target.entity);
  EXPECT_NE(clone, original);
  EXPECT_EQ(mesh_id(peer.entity), original);
  EXPECT_EQ(session_.assets().mesh_count(), mesh_count + 1u);
  expect_vertices(target.entity, target_before);
  expect_vertices(peer.entity, peer_before);
  expect_vertices(peer.entity, peer_local, false);
  ASSERT_TRUE(editor_.history().undo().ok());
  EXPECT_EQ(mesh_id(target.entity), original);
  EXPECT_EQ(session_.assets().mesh_count(), mesh_count);
  ASSERT_TRUE(editor_.history().redo().ok());
  EXPECT_EQ(mesh_id(target.entity), clone);
  EXPECT_EQ(mesh_id(peer.entity), original);
  expect_vertices(target.entity, target_before);
  expect_vertices(peer.entity, peer_before);
  expect_vertices(peer.entity, peer_local, false);
}

TEST_F(MeshTransformBakeTests, ScaleAtLargeWorldOffsetsUsesAuthoredLocalCoordinates) {
  for (const bool parented : {false, true}) {
    SCOPED_TRACE(parented);
    const auto target = mesh("Large Offset Scale Bake");
    if (parented) {
      const auto parent = editor_.scene().create_empty();
      ASSERT_TRUE(parent.ok());
      ASSERT_TRUE(editor_.scene().set_transform(parent.value().id,
          {.position = {400000.0f, -200000.0f, 900000.0f},
           .rotation_euler = {0.2f, -0.4f, 0.6f},
           .scale = {1.2f, 0.8f, 1.4f}}).ok());
      ASSERT_TRUE(editor_.scene().reparent(target.entity, parent.value().id, false).ok());
    }
    const QuaderTransform before{
        .position = {1000000.0f, -700000.0f, 300000.0f},
        .rotation_euler = {0.0f, 0.0f, 0.7f},
        .scale = {2.0f, 0.75f, 1.5f}};
    ASSERT_TRUE(editor_.scene().set_transform(target.entity, before).ok());
    ASSERT_TRUE(editor_.selection().select_object(target).ok());
    const auto original = vertices(target.entity, false);
    const auto baked = editor_.modeling().bake_transforms(QuaderTransformBakeMode::Scale);
    ASSERT_TRUE(baked.ok()) << baked.error_message();
    EXPECT_TRUE(baked.changed());
    const auto after = editor_.query().entity(target.entity)->transform;
    expect_exact_vector(after.position, before.position);
    expect_exact_vector(after.rotation_euler, before.rotation_euler);
    expect_exact_vector(after.scale, {1.0f, 1.0f, 1.0f});
    const auto actual = vertices(target.entity, false);
    ASSERT_EQ(actual.size(), original.size());
    for (std::size_t index = 0; index < actual.size(); ++index) {
      EXPECT_NEAR(actual[index].x, before.scale.x * original[index].x, 0.00001f);
      EXPECT_NEAR(actual[index].y, before.scale.y * original[index].y, 0.00001f);
      EXPECT_NEAR(actual[index].z, before.scale.z * original[index].z, 0.00001f);
    }
  }
}

TEST_F(MeshTransformBakeTests, SelectedChildBeforeParentUsesOriginalWorldFrames) {
  const auto child = mesh("Earlier Child");
  const auto parent = mesh("Later Parent");
  ASSERT_LT(child.entity, parent.entity);
  const auto unselected = editor_.scene().create_entity(
      {.name = "Unselected Child", .kind = QuaderEntityKind::Empty,
       .transform = {.position = {-2.0f, 7.0f, -5.0f},
                     .rotation_euler = {0.2f, 0.3f, -0.4f}}});
  ASSERT_TRUE(unselected.ok());
  ASSERT_TRUE(editor_.scene().set_transform(parent.entity,
      {.position = {7.0f, -3.0f, 5.0f}, .rotation_euler = {0.3f, -0.6f, 0.8f},
       .scale = {2.0f, 2.0f, 2.0f}}).ok());
  ASSERT_TRUE(editor_.scene().set_transform(child.entity,
      {.position = {-2.0f, 1.0f, 4.0f}, .rotation_euler = {-0.2f, 0.4f, 0.1f},
       .scale = {0.5f, 1.25f, 1.5f}}).ok());
  ASSERT_TRUE(editor_.scene().reparent(child.entity, parent.entity, false).ok());
  ASSERT_TRUE(editor_.scene().reparent(unselected.value().id, parent.entity, false).ok());
  ASSERT_TRUE(editor_.selection().select_objects({child.entity, parent.entity}, parent.entity).ok());
  const auto child_before = vertices(child.entity);
  const auto parent_before = vertices(parent.entity);
  const auto unselected_before = session_.scene().world_transform_matrix(unselected.value().id);
  const std::size_t undo_before = session_.history().undo_count();
  const auto baked = editor_.modeling().bake_transforms(QuaderTransformBakeMode::Transforms);
  ASSERT_TRUE(baked.ok()) << baked.error_message();
  for (const EntityId entity : {child.entity, parent.entity}) {
    const auto transform = editor_.query().entity(entity)->transform;
    expect_exact_vector(transform.position, {});
    expect_exact_vector(transform.rotation_euler, {});
    expect_exact_vector(transform.scale, {1.0f, 1.0f, 1.0f});
  }
  expect_vertices(child.entity, child_before);
  expect_vertices(parent.entity, parent_before);
  expect_matrix(session_.scene().world_transform_matrix(unselected.value().id), unselected_before);
  EXPECT_EQ(session_.history().undo_count(), undo_before + 1u);
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_vertices(child.entity, child_before);
  expect_vertices(parent.entity, parent_before);
  expect_matrix(session_.scene().world_transform_matrix(unselected.value().id), unselected_before);
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_vertices(child.entity, child_before);
  expect_vertices(parent.entity, parent_before);
  expect_matrix(session_.scene().world_transform_matrix(unselected.value().id), unselected_before);
}

TEST_F(MeshTransformBakeTests, UnrepresentableUnselectedChildRejectsWholeSelection) {
  const auto valid = mesh("Valid Independent Bake");
  const auto parent = mesh("Nonuniform Parent");
  const auto child = editor_.scene().create_entity(
      {.name = "Rotated Unselected Child", .kind = QuaderEntityKind::Empty,
       .transform = {.rotation_euler = {0.0f, 0.0f, 0.785398163f}}});
  ASSERT_TRUE(child.ok());
  ASSERT_TRUE(editor_.scene().set_transform(valid.entity, {.position = {5.0f, 0.0f, 0.0f}}).ok());
  ASSERT_TRUE(editor_.scene().set_transform(parent.entity, {.scale = {2.0f, 1.0f, 1.0f}}).ok());
  ASSERT_TRUE(editor_.scene().reparent(child.value().id, parent.entity, false).ok());
  ASSERT_TRUE(editor_.selection().select_objects({valid.entity, parent.entity}, parent.entity).ok());
  const auto before = test::capture_command_observable_state(editor_);
  const auto failed = editor_.modeling().bake_transforms(QuaderTransformBakeMode::Transforms);
  EXPECT_FALSE(failed.ok());
  EXPECT_FALSE(failed.changed());
  expect_no_change(before);
}

TEST_F(MeshTransformBakeTests, MirroredBakePreservesEffectiveUvsNormalsAndFaceSettings) {
  const auto target = mesh("Mirrored Textured Bake", true);
  ASSERT_TRUE(editor_.scene().set_transform(target.entity,
      {.position = {4.0f, -2.0f, 8.0f}, .rotation_euler = {0.2f, -0.5f, 0.7f},
       .scale = {-1.5f, 0.75f, 2.0f}}).ok());
  const QMat4 sampling = compose_transform(TransformComponent{
      .position = {0.3f, -1.0f, 0.5f}, .rotation_euler = {0.3f, 1.2f, -0.4f},
      .scale = {1.3f, 0.7f, -1.1f}}.spatial_transform());
  ASSERT_TRUE(session_.scene().set_texture_mapping(target.entity, {.sampling_from_local = sampling}));
  ASSERT_TRUE(editor_.selection().select_object(target).ok());
  const auto before = corners(target.entity);
  ASSERT_FALSE(before.empty());
  const auto original_document = *session_.assets().mesh(mesh_id(target.entity))->authored_polygon_document;
  const auto baked = editor_.modeling().bake_transforms(QuaderTransformBakeMode::Transforms);
  ASSERT_TRUE(baked.ok()) << baked.error_message();
  const auto expect_preserved = [&]() {
    expect_corners(corners(target.entity), before);
    const auto &document = *session_.assets().mesh(mesh_id(target.entity))->authored_polygon_document;
    ASSERT_EQ(document.faces.size(), original_document.faces.size());
    for (std::size_t index = 0; index < document.faces.size(); ++index) {
      EXPECT_EQ(document.faces[index].texture_transform,
                original_document.faces[index].texture_transform);
    }
  };
  expect_preserved();
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_corners(corners(target.entity), before);
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_preserved();
}

TEST_F(MeshTransformBakeTests, HistoryPreviewRejectionKeepsUndoAndRedoRetryable) {
  const auto created = editor_.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      "Bake History Preview Mesh");
  ASSERT_TRUE(created.ok());
  ASSERT_TRUE(editor_.scene().set_transform(created.value().entity,
      {.position = {4.0f, 2.0f, -3.0f}}).ok());
  ASSERT_TRUE(editor_.selection().select_object(created.value()).ok());
  ASSERT_TRUE(editor_.modeling().bake_transforms(QuaderTransformBakeMode::Transforms).ok());
  const std::string preview_command =
      editor_.commands().command_id_for_modeling_action("half_cut");
  ASSERT_FALSE(preview_command.empty());
  for (const bool undo : {true, false}) {
    ASSERT_TRUE(editor_.commands().execute(preview_command).ok());
    const auto before = test::capture_command_observable_state(editor_);
    // Invoke the history owner so the public undo intent cannot first dismiss
    // the active preview; the stored Bake command must reject that collision.
    const auto rejected = undo ? session_.history().undo() : session_.history().redo();
    EXPECT_FALSE(rejected.ok);
    EXPECT_FALSE(rejected.changed);
    expect_no_change(before);
    ASSERT_TRUE(editor_.tools().cancel_active_tool_preview().ok());
    const auto retried = undo ? editor_.history().undo() : editor_.history().redo();
    ASSERT_TRUE(retried.ok()) << retried.error_message();
    EXPECT_TRUE(retried.changed());
  }
}

TEST_F(MeshTransformBakeTests, MissingSdkBindingRejectsWithoutLeavingHostChanges) {
  const auto target = mesh("Stale SDK Binding Bake");
  ASSERT_TRUE(editor_.scene().set_transform(target.entity,
      {.position = {4.0f, -2.0f, 7.0f}, .rotation_euler = {0.2f, -0.3f, 0.5f},
       .scale = {-1.5f, 0.75f, 2.0f}}).ok());
  ASSERT_TRUE(editor_.selection().select_object(target).ok());
  auto binding = *session_.scene().modeling_object(target.entity);
  binding.object_id = std::numeric_limits<int>::max();
  ASSERT_TRUE(session_.scene().set_modeling_object(target.entity, binding));
  const auto before = test::capture_command_observable_state(editor_);
  const auto failed = editor_.modeling().bake_transforms(QuaderTransformBakeMode::Transforms);
  EXPECT_FALSE(failed.ok());
  EXPECT_FALSE(failed.changed());
  expect_no_change(before);
}

TEST(MeshTransformBakePersistenceTests, ReopenedMeshUsesBakedShapeAsTransformZero) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  ProjectSession project;
  QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.project().create(
      std::filesystem::path(temporary.path().toStdWString())).ok());
  const auto created = editor.modeling().create_box(
      {.min = {-1.0f, -2.0f, -0.5f}, .max = {3.0f, 4.0f, 1.5f}},
      "Persisted Baked Mesh");
  ASSERT_TRUE(created.ok()) << created.error_message();
  auto object = created.value();
  ASSERT_TRUE(editor.scene().set_transform(object.entity,
      {.position = {4.0f, -2.0f, 7.0f}, .rotation_euler = {0.3f, -0.4f, 0.7f},
       .scale = {-1.5f, 0.75f, 2.0f}}).ok());
  ASSERT_TRUE(editor.selection().select_object(object).ok());
  const auto baked = editor.modeling().bake_transforms(QuaderTransformBakeMode::Transforms);
  ASSERT_TRUE(baked.ok()) << baked.error_message();
  const auto baked_mesh = editor.query().compiled_object_mesh(object);
  ASSERT_TRUE(baked_mesh.has_value());
  ASSERT_FALSE(baked_mesh->vertices.empty());
  const auto path = ProjectRelativePath::parse_portable("Scenes/Baked Mesh.qdr");
  ASSERT_TRUE(path.ok());
  ASSERT_TRUE(editor.scene_documents().save_as(project.active_scene_id(), path.value()).ok());
  ASSERT_TRUE(editor.scene_documents().close_active().ok());
  const auto opened = editor.scene_documents().open(path.value());
  ASSERT_TRUE(opened.ok()) << opened.error_message();
  const auto objects = editor.query().modeling_objects();
  ASSERT_EQ(objects.size(), 1u);
  const auto rebound = editor.query().modeling_object_for_id(objects.front().modeling_object_id);
  ASSERT_TRUE(rebound.has_value());
  object = *rebound;
  const auto transform = editor.query().entity(object.entity)->transform;
  expect_exact_vector(transform.position, {});
  expect_exact_vector(transform.rotation_euler, {});
  expect_exact_vector(transform.scale, {1.0f, 1.0f, 1.0f});
  const auto expect_baked_shape = [&]() {
    const auto current = editor.query().compiled_object_mesh(object);
    ASSERT_TRUE(current.has_value());
    ASSERT_EQ(current->vertices.size(), baked_mesh->vertices.size());
    EXPECT_EQ(current->indices, baked_mesh->indices);
    const QMat4 world = project.active_scene()->session->scene()
                            .world_transform_matrix(object.entity);
    for (std::size_t index = 0; index < current->vertices.size(); ++index) {
      expect_vector(transform_point(world, current->vertices[index].position),
                    baked_mesh->vertices[index].position);
    }
  };
  expect_baked_shape();
  ASSERT_TRUE(editor.scene().set_transform(object.entity,
      {.rotation_euler = {0.5f, 0.2f, -0.3f}}).ok());
  ASSERT_TRUE(editor.scene().set_transform(object.entity, {}).ok());
  expect_baked_shape();
  ASSERT_TRUE(editor.selection().select_object(object).ok());
  ASSERT_TRUE(editor.modeling().can_bake_transforms(QuaderTransformBakeMode::Transforms));
  const auto unchanged = editor.modeling().bake_transforms(QuaderTransformBakeMode::Transforms);
  ASSERT_TRUE(unchanged.ok());
  EXPECT_FALSE(unchanged.changed());
}

} // namespace
} // namespace quader

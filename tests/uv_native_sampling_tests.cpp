////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "api/quader_polygon_document_internal_access.hpp"
#include "assets/asset_registry.hpp"
#include "assets/mesh_asset_payload.hpp"
#include "assets/texture_asset.hpp"
#include "editor/instances/scene_instance_service.hpp"
#include "editor/modeling/runtime/modeling_object_runtime_map.hpp"
#include "editor/modeling/services/modeling_mesh_sync_service.hpp"
#include "editor/modeling/services/modeling_sdk_projection_adapter.hpp"
#include "editor/projection/mesh_projection_service.hpp"
#include "editor/session/scene_session.hpp"
#include "render/mesh_quad_uv_interpolation.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_texture_mapping.hpp"
#include "scene/scene_transform.hpp"

#include <quader/modeling/neutral/polygon/document.hpp>
#include <quader/modeling/modeling.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace quader {
namespace {

TEST(FaceTextureCanvasProjectionTests, BatchedChartsPreserveIndividualFailuresAndInputOrder) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingObjectRuntimeMap object_map;
  ModelingMeshSyncService mesh_sync;
  ModelingSdkProjectionAdapter projection(scene, assets, object_map, mesh_sync);
  const auto first = projection.create_box({.bounds = {.min = {}, .max = {128, 64, 32}}});
  const auto second = projection.create_box({.bounds = {.min = {}, .max = {64, 32, 16}}});
  ASSERT_TRUE(first.ok) << first.message;
  ASSERT_TRUE(second.ok) << second.message;
  const auto first_world = scene.world_transform_matrix(first.object.entity);
  const auto second_world = scene.world_transform_matrix(second.object.entity);
  const auto scalar = [&](int object, modeling::FaceId face, const QMat4 &world) {
    return projection.face_texture_canvas({
        .modeling_object_id = object, .face = face,
        .world_from_local = world, .sampling_from_local = world});
  };
  const auto first_chart = scalar(first.object.modeling_object_id, {1, 1}, first_world);
  const auto next_chart = scalar(first.object.modeling_object_id, {2, 1}, first_world);
  const auto other_chart = scalar(second.object.modeling_object_id, {1, 1}, second_world);
  ASSERT_TRUE(first_chart.ok()) << first_chart.error_message();
  ASSERT_TRUE(next_chart.ok()) << next_chart.error_message();
  ASSERT_TRUE(other_chart.ok()) << other_chart.error_message();
  const std::array targets{
      ModelingFaceTextureActionTarget{
          .modeling_object_id = first.object.modeling_object_id,
          .world_from_local = first_world, .sampling_from_local = first_world,
          .faces = {{.face = {1, 1}}, {.face = {9999, 1}}, {.face = {2, 1}}}},
      ModelingFaceTextureActionTarget{
          .modeling_object_id = -123, .faces = {{.face = {1, 1}}}},
      ModelingFaceTextureActionTarget{
          .modeling_object_id = second.object.modeling_object_id,
          .world_from_local = second_world, .sampling_from_local = second_world,
          .faces = {{.face = {1, 1}}}},
      ModelingFaceTextureActionTarget{
          .modeling_object_id = first.object.modeling_object_id,
          .world_from_local = first_world, .sampling_from_local = first_world,
          .faces = {{.face = {2, 1}}}}};
  const auto before = projection.stats();
  const auto charts = projection.face_texture_canvases(targets);
  const auto after = projection.stats();
  ASSERT_EQ(charts.size(), 6u);
  EXPECT_EQ(after.face_texture_canvas_document_query_count -
                before.face_texture_canvas_document_query_count, 1u);
  EXPECT_EQ(after.face_texture_canvas_document_copy_count -
                before.face_texture_canvas_document_copy_count, 2u);
  EXPECT_EQ(after.face_texture_canvas_build_count - before.face_texture_canvas_build_count, 6u);
  const auto expect_chart = [](const auto &actual, const auto &expected) {
    ASSERT_TRUE(actual.ok()) << actual.error_message();
    EXPECT_EQ(actual.value().outline, expected.value().outline);
    EXPECT_EQ(actual.value().map, expected.value().map);
    EXPECT_EQ(actual.value().mapping_usable, expected.value().mapping_usable);
    EXPECT_EQ(actual.value().disabled_reason, expected.value().disabled_reason);
  };
  expect_chart(charts[0], first_chart);
  ASSERT_FALSE(charts[1].ok());
  EXPECT_EQ(charts[1].error().code, modeling::ErrorCode::StaleId);
  expect_chart(charts[2], next_chart);
  ASSERT_FALSE(charts[3].ok());
  EXPECT_EQ(charts[3].error().code, modeling::ErrorCode::InvalidId);
  expect_chart(charts[4], other_chart);
  expect_chart(charts[5], next_chart);
  std::size_t visited = 0;
  projection.visit_face_texture_canvases(targets,
      [&](std::size_t object_index, std::size_t face_index, auto chart) {
        EXPECT_EQ(object_index, 0u);
        EXPECT_EQ(face_index, visited);
        if (visited == 0u) {
          expect_chart(chart, first_chart);
        } else {
          EXPECT_FALSE(chart.ok());
        }
        return ++visited < 2u;
      });
  EXPECT_EQ(visited, 2u);
  EXPECT_EQ(projection.stats().face_texture_canvas_build_count -
                after.face_texture_canvas_build_count, 2u);
}

/// Keeps the observable rendered corner attributes independent of face IDs.
struct DisplayedUvCorner {
  QVec3 position;
  QVec2 uv;
};

quader_poly::Document uv_test_wall(float x, bool authored = true) {
  quader_poly::Document document;
  const std::array ids{
      quader_poly::add_vertex(document, {x, 0, 0}),
      quader_poly::add_vertex(document, {x + 3, 0, 0}),
      quader_poly::add_vertex(document, {x + 3, 2, 0}),
      quader_poly::add_vertex(document, {x, 2, 0})};
  (void)quader_poly::add_face(document, ids);
  if (authored) {
    document.faces.front().uvs = {{0.1f, 0.2f}, {1.8f, 0.15f},
                                 {1.5f, 2.7f}, {0.2f, 1.1f}};
  }
  document.faces.front().texture_transform = {
      .scale = {-0.7f, 1.3f}, .shift_uv = {0.23f, -0.17f},
      .rotation_degrees = 37.0f};
  return document;
}

void expect_uv_matrix(const QMat4 &actual, const QMat4 &expected) {
  for (std::size_t index = 0; index < actual.values.size(); ++index) {
    EXPECT_NEAR(actual.values[index], expected.values[index], 0.0003f) << index;
  }
}

void expect_displayed_corners(std::vector<DisplayedUvCorner> actual,
                              const std::vector<DisplayedUvCorner> &expected) {
  ASSERT_EQ(actual.size(), expected.size());
  for (const auto &corner : expected) {
    const auto found = std::ranges::find_if(actual, [&](const auto &candidate) {
      return std::abs(candidate.position.x - corner.position.x) < 0.001f &&
             std::abs(candidate.position.y - corner.position.y) < 0.001f &&
             std::abs(candidate.position.z - corner.position.z) < 0.001f &&
             std::abs(candidate.uv.x - corner.uv.x) < 0.001f &&
             std::abs(candidate.uv.y - corner.uv.y) < 0.001f;
    });
    ASSERT_NE(found, actual.end())
        << "Missing displayed UV " << corner.uv.x << ", " << corner.uv.y
        << " at " << corner.position.x << ", " << corner.position.y
        << ", " << corner.position.z;
    actual.erase(found);
  }
}

/// Exercises the public modeling/history routes with captured render payloads.
class UvNativeSamplingTests : public ::testing::Test {
protected:
  UvNativeSamplingTests()
      : editor_(QuaderApiSessionInternalAccess::from_scene_session(session_)) {}

  QuaderModelingObjectRef wall(std::string name, float x = 0,
                              bool authored = true) {
    const auto created = editor_.modeling().create_mesh(
        QuaderPolygonDocumentInternalAccess::from_backend(
            uv_test_wall(x, authored)), {.name = std::move(name)});
    EXPECT_TRUE(created.ok()) << created.error_message();
    return created.ok() ? created.value() : QuaderModelingObjectRef{};
  }

  std::vector<DisplayedUvCorner> displayed(EntityId entity,
                                          EntityId occurrence = kInvalidEntityId) {
    const auto *object = session_.scene().modeling_object(entity);
    EXPECT_NE(object, nullptr);
    if (object == nullptr) return {};
    auto mesh = session_.modeling().compile_object_mesh(object->object_id);
    const auto sampling = effective_texture_sampling_from_local(
        session_.scene(), entity, occurrence);
    EXPECT_TRUE(mesh.has_value());
    EXPECT_TRUE(sampling.has_value());
    if (!mesh.has_value() || !sampling.has_value()) return {};
    MeshProjectionService::apply_mesh_texture_mapping(*mesh, *sampling);
    QMat4 world = session_.scene().world_transform_matrix(entity);
    if (occurrence != kInvalidEntityId) {
      const auto *instance = session_.scene().instance_occurrence(occurrence);
      EXPECT_NE(instance, nullptr);
      if (instance == nullptr) return {};
      const auto inverse = try_scene_matrix_inverse(
          session_.scene().world_transform_matrix(instance->definition));
      EXPECT_TRUE(inverse.has_value());
      if (!inverse.has_value()) return {};
      world = scene_transform_multiply(
          session_.scene().world_transform_matrix(occurrence),
          scene_transform_multiply(*inverse, world));
    }
    std::vector<DisplayedUvCorner> result;
    for (const auto &vertex : mesh->geometry().vertices) {
      result.push_back({transform_point(world, vertex.position), vertex.uv});
    }
    return result;
  }

  void select_sources(EntityId source, EntityId survivor) {
    ASSERT_TRUE(editor_.modeling().set_selection_mode(QuaderSelectionMode::Object).ok());
    const std::array sources{source, survivor};
    ASSERT_TRUE(editor_.selection().select_objects(sources, survivor).ok());
    session_.history().clear();
  }

  void texture_extent(EntityId entity, std::uint32_t width, std::uint32_t height) {
    TextureAsset texture;
    texture.name = "UV Axis Resolution";
    texture.width = width;
    texture.height = height;
    texture.generated = true;
    const AssetId texture_id = session_.assets().add_texture(std::move(texture));
    ASSERT_NE(texture_id, kInvalidAssetId);
    MaterialDocument material;
    material.texture_slots.emplace(material_texture_slot::kBaseColor, texture_id);
    const AssetId material_id = session_.assets().add_material(std::move(material));
    ASSERT_NE(material_id, kInvalidAssetId);
    ASSERT_TRUE(session_.scene().set_material(entity, {.material = material_id}));
  }

  QuaderApiVoidResult rotate_numeric(double value, std::string merge_key = {}) {
    return editor_.modeling().set_face_texture_setting({
        .field = QuaderFaceTextureField::Rotate, .value = value,
        .selection_token = editor_.modeling().face_texture_settings().selection_token,
        .merge_key = std::move(merge_key)});
  }

  std::optional<QVec2> linear_uv_at(EntityId entity, QVec3 world_point) {
    const auto *object = session_.scene().modeling_object(entity);
    EXPECT_NE(object, nullptr);
    if (object == nullptr) return std::nullopt;
    auto mesh = session_.modeling().compile_object_mesh(object->object_id);
    const auto sampling = effective_texture_sampling_from_local(session_.scene(), entity);
    EXPECT_TRUE(mesh.has_value());
    EXPECT_TRUE(sampling.has_value());
    if (!mesh.has_value() || !sampling.has_value()) return std::nullopt;
    MeshProjectionService::apply_mesh_texture_mapping(*mesh, *sampling);
    const auto &geometry = mesh->geometry();
    const auto quads = build_mesh_quad_uv_interpolations(
        geometry.vertices, geometry.indices, geometry.primitives);
    const auto world = session_.scene().world_transform_matrix(entity);
    for (std::size_t index = 0; index + 2U < geometry.indices.size(); index += 3U) {
      const std::array indices{geometry.indices[index], geometry.indices[index + 1U],
                               geometry.indices[index + 2U]};
      const auto &a = geometry.vertices[indices[0]];
      const auto &b = geometry.vertices[indices[1]];
      const auto &c = geometry.vertices[indices[2]];
      const auto origin = transform_point(world, a.position);
      const auto ab = transform_point(world, b.position) - origin;
      const auto ac = transform_point(world, c.position) - origin;
      const auto point = world_point - origin;
      const float aa = dot(ab, ab);
      const float bb = dot(ac, ac);
      const float mixed = dot(ab, ac);
      const float determinant = aa * bb - mixed * mixed;
      if (determinant <= 0.000001F) continue;
      const float wb = (dot(point, ab) * bb - dot(point, ac) * mixed) / determinant;
      const float wc = (dot(point, ac) * aa - dot(point, ab) * mixed) / determinant;
      const float wa = 1.0F - wb - wc;
      if (wa < -0.0001F || wb < -0.0001F || wc < -0.0001F ||
          length_squared(point - ab * wb - ac * wc) > 0.000001F) continue;
      for (const auto corner : indices) EXPECT_FALSE(quads[corner].enabled);
      return a.uv * wa + b.uv * wb + c.uv * wc;
    }
    return std::nullopt;
  }

  SceneSession session_;
  QuaderEditorApi editor_;
};

TEST_F(UvNativeSamplingTests, CombineIdentityRetainsNonAffineAuthoredChartsAndUndo) {
  const auto source = wall("Source", 0);
  const auto survivor = wall("Survivor", 5);
  const auto source_mesh = session_.modeling().compile_object_mesh(source.modeling_object_id);
  const auto survivor_mesh = session_.modeling().compile_object_mesh(survivor.modeling_object_id);
  ASSERT_TRUE(source_mesh.has_value());
  ASSERT_TRUE(survivor_mesh.has_value());
  auto expected = displayed(survivor.entity);
  const auto source_corners = displayed(source.entity);
  expected.insert(expected.end(), source_corners.begin(), source_corners.end());
  select_sources(source.entity, survivor.entity);
  const auto combined = editor_.modeling().combine_meshes();
  ASSERT_TRUE(combined.ok()) << combined.error_message();
  ASSERT_TRUE(combined.changed());
  EXPECT_FALSE(session_.scene().exists(source.entity));
  EXPECT_EQ(session_.history().undo_count(), 1U);
  expect_displayed_corners(displayed(survivor.entity), expected);
  const auto committed = session_.modeling().compile_object_mesh(survivor.modeling_object_id);
  ASSERT_TRUE(committed.has_value());
  ASSERT_EQ(committed->authored_polygon_document->faces.size(), 2U);
  for (const auto &face : committed->authored_polygon_document->faces) {
    EXPECT_EQ(face.uvs.size(), 4U);
    EXPECT_EQ(face.texture_transform,
              source_mesh->authored_polygon_document->faces.front().texture_transform);
  }
  ASSERT_TRUE(editor_.history().undo().ok());
  ASSERT_TRUE(session_.scene().exists(source.entity));
  expect_displayed_corners(displayed(source.entity), source_corners);
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_displayed_corners(displayed(survivor.entity), expected);
}

TEST_F(UvNativeSamplingTests, CombinePerpendicularCubeFacesKeepsCompleteBoundaryAndUndoRedo) {
  auto document = quader_poly::make_cube({});
  quader_poly::ElementId top = quader_poly::kInvalidElementId;
  quader_poly::ElementId side = quader_poly::kInvalidElementId;
  for (auto &face : document.faces) {
    const auto normal = quader_poly::face_normal(document, face);
    if (normal.y > 0.9F) top = face.id;
    if (normal.x > 0.9F) side = face.id;
    for (std::size_t index = 0; index < face.vertices.size(); ++index) {
      face.uvs.push_back({static_cast<float>(face.id * 3U + index),
                          static_cast<float>(index * index)});
    }
  }
  ASSERT_NE(top, quader_poly::kInvalidElementId);
  ASSERT_NE(side, quader_poly::kInvalidElementId);
  const auto created = editor_.modeling().create_mesh(
      QuaderPolygonDocumentInternalAccess::from_backend(document),
      {.name = "Perpendicular face charts"});
  ASSERT_TRUE(created.ok()) << created.error_message();
  const auto object = created.value();
  ASSERT_TRUE(editor_.modeling().set_selection_mode(QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor_.selection().select_face(object, top).ok());
  ASSERT_TRUE(editor_.selection().select_face(object, side, QuaderSelectionEdit::Add).ok());
  const auto snapshot = [&] {
    const auto mesh = session_.modeling().compile_object_mesh(object.modeling_object_id);
    EXPECT_TRUE(mesh.has_value());
    const auto sdk = MeshProjectionService::sdk_document_from_polygon_document(
        *mesh->authored_polygon_document).or_throw();
    return modeling::serialize_qdr_authored_topology(sdk).or_throw();
  };
  const auto original = snapshot();
  const auto original_displayed = displayed(object.entity);
  const auto selection = editor_.modeling().selection_summary();
  session_.history().clear();
  const auto descriptor = editor_.commands().descriptor(quader_command::kModelingCombineFaces);
  ASSERT_TRUE(descriptor.has_value());
  EXPECT_TRUE(descriptor->enabled);
  const auto context_actions = editor_.modeling().available_action_set({
      .surface = QuaderActionSurface::ViewportContextMenu,
      .include_disabled = false,
      .include_hidden = false});
  EXPECT_TRUE(std::ranges::any_of(context_actions.panels, [](const auto &panel) {
    return std::ranges::any_of(panel.actions, [](const QuaderActionItem &action) {
      return action.action_id == "combine_faces";
    });
  }));
  const auto combined = editor_.commands().execute(quader_command::kModelingCombineFaces);
  ASSERT_TRUE(combined.ok()) << combined.error_message();
  ASSERT_TRUE(combined.changed());
  const auto committed = snapshot();
  EXPECT_NE(committed, original);
  const auto committed_selection = editor_.modeling().selection_summary();
  EXPECT_EQ(committed_selection.mode, QuaderSelectionMode::Face);
  EXPECT_EQ(committed_selection.selected_face_count, 1U);
  EXPECT_GT(committed_selection.selection_revision, selection.selection_revision);
  EXPECT_GT(committed_selection.content_revision, selection.content_revision);
  const auto mesh = session_.modeling().compile_object_mesh(object.modeling_object_id);
  ASSERT_TRUE(mesh.has_value());
  ASSERT_EQ(mesh->authored_polygon_document->faces.size(), 5U);
  const auto *merged = quader_poly::find_face(
      *mesh->authored_polygon_document,
      static_cast<quader_poly::ElementId>(committed_selection.active_face_id));
  ASSERT_NE(merged, nullptr);
  ASSERT_EQ(merged->vertices.size(), 6U);
  ASSERT_EQ(merged->uvs.size(), merged->vertices.size());
  for (const auto uv : merged->uvs) {
    EXPECT_TRUE(std::isfinite(uv.x));
    EXPECT_TRUE(std::isfinite(uv.y));
  }
  const auto &geometry = mesh->geometry();
  ASSERT_EQ(geometry.primitives.size(), 5U);
  const auto primitive = std::ranges::find_if(geometry.primitives, [](const auto &span) {
    return span.index_count == 12U;
  });
  ASSERT_NE(primitive, geometry.primitives.end());
  std::map<std::pair<quader_poly::ElementId, quader_poly::ElementId>, int> edge_uses;
  for (std::size_t triangle = primitive->index_offset;
       triangle < primitive->index_offset + primitive->index_count; triangle += 3U) {
    for (std::size_t corner = 0; corner < 3U; ++corner) {
      const auto from = geometry.vertices[geometry.indices[triangle + corner]].source_vertex_id;
      const auto to = geometry.vertices[geometry.indices[triangle + (corner + 1U) % 3U]].source_vertex_id;
      ++edge_uses[std::minmax(from, to)];
    }
  }
  for (std::size_t corner = 0; corner < merged->vertices.size(); ++corner) {
    const auto from = merged->vertices[corner];
    const auto to = merged->vertices[(corner + 1U) % merged->vertices.size()];
    const auto key = std::minmax(from, to);
    EXPECT_EQ(edge_uses[key], 1) << "Missing compiled boundary " << from << "," << to;
    edge_uses.erase(key);
  }
  for (const auto &edge : edge_uses) EXPECT_EQ(edge.second, 2);
  const auto committed_displayed = displayed(object.entity);
  EXPECT_EQ(session_.history().undo_count(), 1U);

  ASSERT_TRUE(editor_.history().undo().ok());
  EXPECT_EQ(snapshot(), original);
  expect_displayed_corners(displayed(object.entity), original_displayed);
  const auto restored_selection = editor_.modeling().selection_summary();
  EXPECT_EQ(restored_selection.active_face_id, selection.active_face_id);
  EXPECT_EQ(restored_selection.selected_face_count, selection.selected_face_count);
  ASSERT_TRUE(editor_.history().redo().ok());
  EXPECT_EQ(snapshot(), committed);
  expect_displayed_corners(displayed(object.entity), committed_displayed);
  const auto redone_selection = editor_.modeling().selection_summary();
  EXPECT_EQ(redone_selection.active_face_id, committed_selection.active_face_id);
  EXPECT_EQ(redone_selection.selected_face_count, 1U);
  const auto redone_mesh = session_.modeling().compile_object_mesh(object.modeling_object_id);
  ASSERT_TRUE(redone_mesh.has_value());
  EXPECT_TRUE(mesh_geometry_payload_equal(redone_mesh->geometry(), geometry));
}

TEST_F(UvNativeSamplingTests, CombineCoplanarFacesRemovesUvSeamAndPreservesUndoRedo) {
  quader_poly::Document document;
  const auto a = quader_poly::add_vertex(document, {0, 0, 0});
  const auto b = quader_poly::add_vertex(document, {1, 0, 0});
  const auto c = quader_poly::add_vertex(document, {2, 0, 0});
  const auto d = quader_poly::add_vertex(document, {0, 1, 0});
  const auto e = quader_poly::add_vertex(document, {1, 1, 0});
  const auto f = quader_poly::add_vertex(document, {2, 1, 0});
  const auto first = quader_poly::add_face(document, std::array{a, b, e, d});
  const auto second = quader_poly::add_face(document, std::array{b, c, f, e});
  document.faces[0].uvs = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
  document.faces[1].uvs = {{4, 0}, {5, 0}, {5, 1}, {4, 1}};
  const auto created = editor_.modeling().create_mesh(
      QuaderPolygonDocumentInternalAccess::from_backend(document),
      {.name = "Coplanar faces with a UV seam"});
  ASSERT_TRUE(created.ok()) << created.error_message();
  const auto object = created.value();
  ASSERT_TRUE(editor_.modeling().set_selection_mode(QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor_.selection().select_faces(
      object, {static_cast<QuaderElementId>(first), static_cast<QuaderElementId>(second)}).ok());
  const auto snapshot = [&] {
    const auto mesh = session_.modeling().compile_object_mesh(object.modeling_object_id);
    EXPECT_TRUE(mesh.has_value());
    const auto sdk = MeshProjectionService::sdk_document_from_polygon_document(
        *mesh->authored_polygon_document).or_throw();
    return modeling::serialize_qdr_authored_topology(sdk).or_throw();
  };
  const auto original = snapshot();
  session_.history().clear();
  const auto descriptor = editor_.commands().descriptor(quader_command::kModelingCombineFaces);
  ASSERT_TRUE(descriptor.has_value());
  EXPECT_TRUE(descriptor->enabled);
  const auto combined = editor_.commands().execute(quader_command::kModelingCombineFaces);
  ASSERT_TRUE(combined.ok()) << combined.error_message();
  ASSERT_TRUE(combined.changed());
  const auto committed = snapshot();
  EXPECT_NE(committed, original);
  const auto mesh = session_.modeling().compile_object_mesh(object.modeling_object_id);
  ASSERT_TRUE(mesh.has_value());
  ASSERT_EQ(mesh->authored_polygon_document->faces.size(), 1U);
  const auto &merged = mesh->authored_polygon_document->faces.front();
  EXPECT_EQ(merged.uvs.size(), merged.vertices.size());
  EXPECT_EQ(session_.history().undo_count(), 1U);
  ASSERT_TRUE(editor_.history().undo().ok());
  EXPECT_EQ(snapshot(), original);
  ASSERT_TRUE(editor_.history().redo().ok());
  EXPECT_EQ(snapshot(), committed);
}

TEST_F(UvNativeSamplingTests, RebaseKeepsFiniteCornersWhenSamplingCompensationLosesPrecision) {
  const auto document = uv_test_wall(0.0F);
  const auto sdk = MeshProjectionService::sdk_document_from_polygon_document(document).or_throw();
  QMat4 destination_sampling;
  destination_sampling.values[12] = 1.0e20F;
  const auto rebased = MeshProjectionService::rebase_sdk_document_texture_mapping(
      document, sdk, {}, destination_sampling, false);
  ASSERT_TRUE(rebased.ok()) << rebased.error_message();
  const auto result = rebased.value().authored_topology().or_throw();
  ASSERT_EQ(result.faces.size(), 1U);
  ASSERT_EQ(result.faces.front().uvs.size(), document.faces.front().uvs.size());
  for (std::size_t index = 0; index < result.faces.front().uvs.size(); ++index) {
    EXPECT_FLOAT_EQ(result.faces.front().uvs[index].x, document.faces.front().uvs[index].x);
    EXPECT_FLOAT_EQ(result.faces.front().uvs[index].y, document.faces.front().uvs[index].y);
  }
  destination_sampling.values[0] = 0.0F;
  EXPECT_FALSE(MeshProjectionService::rebase_sdk_document_texture_mapping(
      document, sdk, {}, destination_sampling, false).ok());
}

TEST_F(UvNativeSamplingTests, ChangedNgonTriangulationKeepsStoredCornersAfterGeometryEdits) {
  quader_poly::Document source;
  std::vector<quader_poly::ElementId> corners;
  constexpr std::array<QVec3, 5> kPoints{{
      {0, 0, 0}, {4, 0, 0}, {5, 3, 0}, {2, 5, 0}, {-1, 3, 0}}};
  for (const auto point : kPoints) corners.push_back(quader_poly::add_vertex(source, point));
  ASSERT_NE(quader_poly::add_face(source, corners), quader_poly::kInvalidElementId);
  source.faces.front().uvs = {{0, 0}, {1, 0}, {3, 2}, {0, 1}, {2, 3}};
  const auto before = MeshProjectionService::sdk_document_from_polygon_document(source).or_throw();
  const auto triangle_ids = [](const modeling::PolygonDocument &document) {
    std::vector<std::array<std::uint32_t, 3>> triangles;
    const auto mesh = document.compile_mesh().or_throw();
    for (std::size_t index = 0; index + 2U < mesh.indices.size(); index += 3U) {
      std::array ids{mesh.vertices[mesh.indices[index]].source_vertex.index,
          mesh.vertices[mesh.indices[index + 1U]].source_vertex.index,
          mesh.vertices[mesh.indices[index + 2U]].source_vertex.index};
      std::ranges::sort(ids);
      triangles.push_back(ids);
    }
    std::ranges::sort(triangles);
    return triangles;
  };
  const auto original_triangles = triangle_ids(before);
  std::size_t changed_triangulations = 0U;
  for (std::size_t moved = 0; moved < kPoints.size(); ++moved) {
    SCOPED_TRACE(moved);
    auto transformed = before;
    const std::array updates{modeling::VertexPositionUpdate{
        modeling::make_id<modeling::VertexTag>(corners[moved]), {2.0F, 2.5F, 0.0F}}};
    ASSERT_TRUE(transformed.set_vertex_positions(updates).ok());
    ASSERT_TRUE(transformed.validate().ok());
    const auto preserved = MeshProjectionService::preserve_transformed_sdk_document_uv_layout(
        before, transformed, true);
    ASSERT_TRUE(preserved.ok()) << preserved.error_message();
    const auto result = transformed.authored_topology().or_throw();
    ASSERT_EQ(result.faces.size(), 1U);
    const auto &face = result.faces.front();
    for (std::size_t corner = 0; corner < face.vertices.size(); ++corner) {
      const auto original = std::ranges::find(corners, face.vertices[corner].index);
      ASSERT_NE(original, corners.end());
      const auto index = static_cast<std::size_t>(original - corners.begin());
      EXPECT_FLOAT_EQ(face.uvs[corner].x, source.faces.front().uvs[index].x);
      EXPECT_FLOAT_EQ(face.uvs[corner].y, source.faces.front().uvs[index].y);
    }
    changed_triangulations += triangle_ids(transformed) != original_triangles ? 1U : 0U;
  }
  EXPECT_GT(changed_triangulations, 0U);
}

TEST_F(UvNativeSamplingTests, CombineRebasesSourceAndSurvivorSamplingIncludingMirrors) {
  for (const bool mirrored : {false, true}) {
    SCOPED_TRACE(mirrored);
    const auto source = wall("Mapped Source");
    const auto survivor = wall("Mapped Survivor", 7, false);
    ASSERT_TRUE(editor_.scene().set_transform(source.entity,
        {.position = {3, 2, -1}, .rotation_euler = {0.2f, 0.35f, 0.6f},
         .scale = {mirrored ? -1.5f : 1.5f, 0.8f, 1.2f}}).ok());
    ASSERT_TRUE(editor_.scene().set_transform(survivor.entity,
        {.position = {-2, 1, 4}, .rotation_euler = {-0.1f, 0.2f, -0.3f},
         .scale = {0.7f, 1.3f, 1.1f}}).ok());
    const QMat4 source_sampling = compose_transform(QTransform{
        .position = {0.4f, -0.7f, 0.1f}, .scale = {1.2f, 0.6f, 0.9f}});
    const QMat4 survivor_sampling = compose_transform(QTransform{
        .position = {-0.2f, 0.5f, 0.3f}, .scale = {0.8f, 1.1f, 1.4f}});
    ASSERT_TRUE(session_.scene().set_texture_mapping(source.entity, {source_sampling}));
    ASSERT_TRUE(session_.scene().set_texture_mapping(survivor.entity, {survivor_sampling}));
    auto expected = displayed(survivor.entity);
    const auto source_corners = displayed(source.entity);
    expected.insert(expected.end(), source_corners.begin(), source_corners.end());
    select_sources(source.entity, survivor.entity);
    const auto result = editor_.modeling().combine_meshes();
    ASSERT_TRUE(result.ok()) << result.error_message();
    expect_displayed_corners(displayed(survivor.entity), expected);
    EXPECT_EQ(session_.scene().texture_mapping(survivor.entity)->sampling_from_local.values,
              survivor_sampling.values);
    ASSERT_TRUE(editor_.history().undo().ok());
    expect_displayed_corners(displayed(source.entity), source_corners);
    ASSERT_TRUE(editor_.history().redo().ok());
    expect_displayed_corners(displayed(survivor.entity), expected);
  }
}

TEST_F(UvNativeSamplingTests, MirroredCombinePreservesFoldedChartInteriorAndUndoRedo) {
  auto document = uv_test_wall(0.0F);
  document.faces.front().texture_transform = {};
  // This folded chart deliberately uses the renderer's linear triangle fallback.
  document.faces.front().uvs = {{0.0F, 0.0F}, {0.6F, 0.0F},
                               {0.0F, 0.8F}, {0.6F, 0.8F}};
  const auto created = editor_.modeling().create_mesh(
      QuaderPolygonDocumentInternalAccess::from_backend(document),
      {.name = "Folded UV source"});
  ASSERT_TRUE(created.ok()) << created.error_message();
  const auto source = created.value();
  const auto survivor = wall("Folded UV survivor", 5.0F);
  ASSERT_TRUE(editor_.scene().set_transform(source.entity,
      {.scale = {-1.0F, 1.0F, 1.0F}}).ok());
  ASSERT_TRUE(session_.scene().set_texture_mapping(source.entity, {QMat4{}}));
  const QVec3 sample_position{-1.5F, 1.0F, 0.0F};
  const auto before = linear_uv_at(source.entity, sample_position);
  ASSERT_TRUE(before.has_value());
  EXPECT_FLOAT_EQ(before->x, 0.0F);
  EXPECT_FLOAT_EQ(before->y, 0.4F);
  select_sources(source.entity, survivor.entity);

  const auto combined = editor_.modeling().combine_meshes();

  ASSERT_TRUE(combined.ok()) << combined.error_message();
  ASSERT_TRUE(combined.changed());
  EXPECT_EQ(session_.history().undo_count(), 1U);
  const auto after = linear_uv_at(survivor.entity, sample_position);
  ASSERT_TRUE(after.has_value());
  EXPECT_FLOAT_EQ(after->x, before->x);
  EXPECT_FLOAT_EQ(after->y, before->y);
  ASSERT_TRUE(editor_.history().undo().ok());
  const auto undone = linear_uv_at(source.entity, sample_position);
  ASSERT_TRUE(undone.has_value());
  EXPECT_FLOAT_EQ(undone->x, before->x);
  EXPECT_FLOAT_EQ(undone->y, before->y);
  ASSERT_TRUE(editor_.history().redo().ok());
  const auto redone = linear_uv_at(survivor.entity, sample_position);
  ASSERT_TRUE(redone.has_value());
  EXPECT_FLOAT_EQ(redone->x, before->x);
  EXPECT_FLOAT_EQ(redone->y, before->y);
}

TEST_F(UvNativeSamplingTests, MirroredCombinePreservesConvexAndConcaveNgonUvInteriors) {
  const std::array<std::vector<QVec3>, 2> loops{
      std::vector<QVec3>{{0, 0, 0}, {3, 0, 0}, {4, 1, 0},
                        {3, 3, 0}, {1, 4, 0}, {0, 2, 0}},
      std::vector<QVec3>{{0, 0, 0}, {4, 0, 0}, {4, 1, 0}, {2, 1, 0},
                        {2, 3, 0}, {1, 4, 0}, {0, 3, 0}, {0, 1, 0}}};
  const std::array<QuaderTransform, 4> reflections{
      QuaderTransform{.scale = {-1.0F, 1.0F, 1.0F}},
      QuaderTransform{.scale = {1.0F, -1.0F, 1.0F}},
      QuaderTransform{.scale = {1.0F, 1.0F, -1.0F}},
      QuaderTransform{.position = {-2.0F, 1.0F, 3.0F},
                       .rotation_euler = {0.2F, -0.4F, 0.3F},
                       .scale = {1.0F, 1.0F, -1.0F}}};
  for (const auto &loop : loops) {
    SCOPED_TRACE(loop.size());
    for (std::size_t reflection = 0; reflection < reflections.size(); ++reflection) {
      SCOPED_TRACE(reflection);
      quader_poly::Document document;
      std::vector<quader_poly::ElementId> corners;
      for (const QVec3 position : loop) {
        corners.push_back(quader_poly::add_vertex(document, position));
      }
      ASSERT_NE(quader_poly::add_face(document, corners), quader_poly::kInvalidElementId);
      for (std::size_t index = 0; index < loop.size(); ++index) {
        document.faces.front().uvs.push_back(
            {0.31F * static_cast<float>(index % 3U),
             0.27F * static_cast<float>((index * index) % 7U)});
      }
      const auto created = editor_.modeling().create_mesh(
          QuaderPolygonDocumentInternalAccess::from_backend(document),
          {.name = "Mirrored ngon UV source"});
      ASSERT_TRUE(created.ok()) << created.error_message();
      const auto source = created.value();
      const auto survivor = wall("Mirrored ngon UV survivor", 6.0F);
      ASSERT_TRUE(editor_.scene().set_transform(source.entity, reflections[reflection]).ok());
      ASSERT_TRUE(session_.scene().set_texture_mapping(source.entity, {QMat4{}}));
      const auto before_mesh = session_.modeling().compile_object_mesh(source.modeling_object_id);
      ASSERT_TRUE(before_mesh.has_value());
      const auto world = session_.scene().world_transform_matrix(source.entity);
      const auto &geometry = before_mesh->geometry();
      std::vector<QVec3> samples;
      std::vector<QVec2> expected;
      // Sample every actual render triangle away from its diagonals. Corner
      // equality alone cannot detect a triangulation change on an authored n-gon.
      for (std::size_t index = 0; index + 2 < geometry.indices.size(); index += 3) {
        const auto a = geometry.vertices[geometry.indices[index]].position;
        const auto b = geometry.vertices[geometry.indices[index + 1]].position;
        const auto c = geometry.vertices[geometry.indices[index + 2]].position;
        const auto position = transform_point(world, a * 0.2F + b * 0.35F + c * 0.45F);
        const auto uv = linear_uv_at(source.entity, position);
        ASSERT_TRUE(uv.has_value());
        samples.push_back(position);
        expected.push_back(*uv);
      }
      ASSERT_EQ(samples.size(), loop.size() - 2U);
      select_sources(source.entity, survivor.entity);
      const auto combined = editor_.modeling().combine_meshes();
      ASSERT_TRUE(combined.ok()) << combined.error_message();
      EXPECT_EQ(session_.history().undo_count(), 1U);
      const auto expect_samples = [&](EntityId entity) {
        for (std::size_t index = 0; index < samples.size(); ++index) {
          const auto uv = linear_uv_at(entity, samples[index]);
          ASSERT_TRUE(uv.has_value());
          EXPECT_NEAR(uv->x, expected[index].x, 0.00001F);
          EXPECT_NEAR(uv->y, expected[index].y, 0.00001F);
        }
      };
      expect_samples(survivor.entity);
      ASSERT_TRUE(editor_.history().undo().ok());
      expect_samples(source.entity);
      ASSERT_TRUE(editor_.history().redo().ok());
      expect_samples(survivor.entity);
    }
  }
}

TEST_F(UvNativeSamplingTests,
       CombinePreservesNgonUvInteriorsWhenPositiveDeterminantChangesProjectionWinding) {
  const std::array<QVec3, 6> loop{{
      {0, 0, 0}, {3, 0, 0}, {4, 1, 0},
      {3, 3, 0}, {1, 4, 0}, {0, 2, 0}}};
  const std::array<QuaderTransform, 2> placements{
      QuaderTransform{.scale = {1.0F, -1.0F, -1.0F}},
      QuaderTransform{.rotation_euler = {std::acos(-1.0F), 0.0F, 0.0F}}};
  for (std::size_t placement = 0; placement < placements.size(); ++placement) {
    SCOPED_TRACE(placement);
    quader_poly::Document document;
    std::vector<quader_poly::ElementId> corners;
    for (const QVec3 position : loop) {
      corners.push_back(quader_poly::add_vertex(document, position));
    }
    ASSERT_NE(quader_poly::add_face(document, corners), quader_poly::kInvalidElementId);
    for (std::size_t index = 0; index < loop.size(); ++index) {
      document.faces.front().uvs.push_back(
          {0.31F * static_cast<float>(index % 3U),
           0.27F * static_cast<float>((index * index) % 7U)});
    }
    const auto created = editor_.modeling().create_mesh(
        QuaderPolygonDocumentInternalAccess::from_backend(document),
        {.name = "Rotated ngon UV source"});
    ASSERT_TRUE(created.ok()) << created.error_message();
    const auto source = created.value();
    const auto survivor = wall("Rotated ngon UV survivor", 6.0F);
    ASSERT_TRUE(editor_.scene().set_transform(source.entity, placements[placement]).ok());
    ASSERT_TRUE(session_.scene().set_texture_mapping(source.entity, {QMat4{}}));
    ASSERT_TRUE(session_.scene().set_texture_mapping(survivor.entity, {QMat4{}}));
    const auto before_mesh = session_.modeling().compile_object_mesh(source.modeling_object_id);
    ASSERT_TRUE(before_mesh.has_value());
    const auto world = session_.scene().world_transform_matrix(source.entity);
    const auto &geometry = before_mesh->geometry();
    std::vector<QVec3> samples;
    std::vector<QVec2> expected;
    // Both placements preserve 3D winding, but reverse the XY projection.
    // The visible chart must survive any resulting change of Earcut traversal.
    for (std::size_t index = 0; index + 2U < geometry.indices.size(); index += 3U) {
      const auto a = geometry.vertices[geometry.indices[index]].position;
      const auto b = geometry.vertices[geometry.indices[index + 1U]].position;
      const auto c = geometry.vertices[geometry.indices[index + 2U]].position;
      const auto position = transform_point(world, a * 0.2F + b * 0.35F + c * 0.45F);
      const auto uv = linear_uv_at(source.entity, position);
      ASSERT_TRUE(uv.has_value());
      samples.push_back(position);
      expected.push_back(*uv);
    }
    ASSERT_EQ(samples.size(), loop.size() - 2U);
    select_sources(source.entity, survivor.entity);
    const auto combined = editor_.modeling().combine_meshes();
    ASSERT_TRUE(combined.ok()) << combined.error_message();
    EXPECT_TRUE(combined.changed());
    EXPECT_EQ(session_.history().undo_count(), 1U);
    const auto expect_samples = [&](EntityId entity) {
      for (std::size_t index = 0; index < samples.size(); ++index) {
        const auto uv = linear_uv_at(entity, samples[index]);
        ASSERT_TRUE(uv.has_value());
        EXPECT_NEAR(uv->x, expected[index].x, 0.00001F);
        EXPECT_NEAR(uv->y, expected[index].y, 0.00001F);
      }
    };
    expect_samples(survivor.entity);
    ASSERT_TRUE(editor_.history().undo().ok());
    expect_samples(source.entity);
    ASSERT_TRUE(editor_.history().redo().ok());
    expect_samples(survivor.entity);
  }
}

TEST_F(UvNativeSamplingTests, CombineUsesActiveInstanceOccurrenceSampling) {
  const auto source = wall("Instance Source");
  const auto survivor = wall("Instance Survivor", 5);
  ASSERT_TRUE(editor_.scene().set_transform(source.entity,
      {.position = {2, 0, 1}, .rotation_euler = {0, 0.2f, 0.4f}}).ok());
  ASSERT_TRUE(session_.scene().set_texture_mapping(source.entity,
      {compose_transform(QTransform{.position = {0.3f, -0.8f, 0}})}));
  select_sources(source.entity, survivor.entity);
  ASSERT_TRUE(editor_.modeling().create_instance_from_selection().ok());
  const EntityId occurrence = session_.scene().primary_selection();
  ASSERT_NE(session_.scene().instance_occurrence(occurrence), nullptr);
  ASSERT_TRUE(editor_.scene().set_transform(occurrence,
      {.position = {9, 4, -3}, .rotation_euler = {0.1f, -0.25f, 0.7f}}).ok());
  ASSERT_TRUE(session_.scene().set_texture_mapping(occurrence,
      {compose_transform(QTransform{.position = {1, -2, 0.2f},
                                    .scale = {0.8f, 1.2f, 1.1f}})}));
  ASSERT_TRUE(editor_.modeling().enter_instance_edit(occurrence).ok());
  auto expected = displayed(survivor.entity, occurrence);
  const auto source_corners = displayed(source.entity, occurrence);
  expected.insert(expected.end(), source_corners.begin(), source_corners.end());
  select_sources(source.entity, survivor.entity);
  const auto combined = editor_.modeling().combine_meshes();
  ASSERT_TRUE(combined.ok()) << combined.error_message();
  expect_displayed_corners(displayed(survivor.entity, occurrence), expected);
  EXPECT_EQ(session_.history().undo_count(), 1U);
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_displayed_corners(displayed(source.entity, occurrence), source_corners);
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_displayed_corners(displayed(survivor.entity, occurrence), expected);
}

TEST_F(UvNativeSamplingTests, CombinePublicationFailureRestoresChartsAndSceneRevisions) {
  const auto source = wall("Rollback Source");
  const auto survivor = wall("Rollback Survivor", 5);
  ASSERT_TRUE(editor_.scene().set_transform(source.entity,
      {.position = {2, 3, -1}}).ok());
  ASSERT_TRUE(session_.scene().set_texture_mapping(source.entity,
      {compose_transform(QTransform{.position = {0.4f, -0.3f, 0}})}));
  const AssetId material = session_.assets().add_material({});
  ASSERT_NE(material, kInvalidAssetId);
  ASSERT_TRUE(session_.scene().set_material(source.entity, {.material = material}));
  select_sources(source.entity, survivor.entity);
  const auto source_before = displayed(source.entity);
  const auto survivor_before = displayed(survivor.entity);
  const auto entities = session_.scene().entities();
  const auto assets = session_.assets().mesh_ids();
  const auto selected = session_.scene().selected_entities();
  const auto scene_revision = session_.scene().revision();
  const auto asset_revision = session_.assets().revision();
  for (const auto point : {ModelingSdkProjectionFailurePoint::AfterSdkMutation,
                           ModelingSdkProjectionFailurePoint::CommittedAssetReplace,
                           ModelingSdkProjectionFailurePoint::CommittedSceneNotify,
                           ModelingSdkProjectionFailurePoint::BeforeCommit}) {
    SCOPED_TRACE(static_cast<int>(point));
    session_.modeling().set_modeling_sdk_projection_failure_for_tests(point);
    const auto failed = editor_.modeling().combine_meshes();
    session_.modeling().set_modeling_sdk_projection_failure_for_tests(
        ModelingSdkProjectionFailurePoint::None);
    EXPECT_FALSE(failed.ok());
    EXPECT_FALSE(failed.changed());
    EXPECT_EQ(session_.scene().entities(), entities);
    EXPECT_EQ(session_.scene().selected_entities(), selected);
    EXPECT_EQ(session_.assets().mesh_ids(), assets);
    EXPECT_EQ(session_.scene().revision(), scene_revision);
    EXPECT_EQ(session_.assets().revision(), asset_revision);
    EXPECT_EQ(session_.scene().material(survivor.entity), nullptr);
    ASSERT_NE(session_.scene().material(source.entity), nullptr);
    EXPECT_EQ(session_.scene().material(source.entity)->material, material);
    EXPECT_EQ(session_.history().undo_count(), 0U);
    expect_displayed_corners(displayed(source.entity), source_before);
    expect_displayed_corners(displayed(survivor.entity), survivor_before);
  }
}

TEST_F(UvNativeSamplingTests, CombineMergesMaterialTablesWithoutChangingFallbackFaces) {
  const auto source = wall("Material Source");
  const auto survivor = wall("Material Survivor", 5);
  const AssetId source_material = session_.assets().add_material({});
  const AssetId target_material = session_.assets().add_material({});
  ASSERT_NE(source_material, kInvalidAssetId);
  ASSERT_NE(target_material, kInvalidAssetId);
  ASSERT_TRUE(session_.scene().set_material(source.entity,
      {.material = source_material, .material_slots = {{1U, source_material}}}));
  ASSERT_TRUE(session_.scene().set_material(survivor.entity, {.material = target_material}));
  ASSERT_TRUE(editor_.modeling().assign_material_slot_to_face(source, 1, 1U).ok());
  // Slot one is intentionally unbound on the survivor and must keep fallback.
  ASSERT_TRUE(editor_.modeling().assign_material_slot_to_face(survivor, 1, 1U).ok());
  ASSERT_TRUE(session_.scene().set_texture_mapping(source.entity,
      {compose_transform(QTransform{.position = {0.4f, -0.3f, 0}})}));
  const auto survivor_before = *session_.scene().material(survivor.entity);
  auto expected = displayed(survivor.entity);
  const auto source_corners = displayed(source.entity);
  expected.insert(expected.end(), source_corners.begin(), source_corners.end());
  select_sources(source.entity, survivor.entity);
  const auto combined = editor_.modeling().combine_meshes();
  ASSERT_TRUE(combined.ok()) << combined.error_message();
  const auto mesh = session_.modeling().compile_object_mesh(survivor.modeling_object_id);
  ASSERT_TRUE(mesh.has_value());
  ASSERT_EQ(mesh->authored_polygon_document->faces.size(), 2U);
  const auto *materials = session_.scene().material(survivor.entity);
  ASSERT_NE(materials, nullptr);
  EXPECT_EQ(materials->material_for_slot(mesh->authored_polygon_document->faces[0].material_slot),
            MaterialReference{target_material});
  EXPECT_EQ(materials->material_for_slot(mesh->authored_polygon_document->faces[1].material_slot),
            MaterialReference{source_material});
  expect_displayed_corners(displayed(survivor.entity), expected);
  ASSERT_TRUE(editor_.history().undo().ok());
  ASSERT_NE(session_.scene().material(survivor.entity), nullptr);
  EXPECT_EQ(*session_.scene().material(survivor.entity), survivor_before);
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_displayed_corners(displayed(survivor.entity), expected);
}

TEST_F(UvNativeSamplingTests, OrdinaryArraysHonorTextureLocksAndRecordMappingsForRedo) {
  for (const bool radial : {false, true}) {
    for (const bool lock : {false, true}) {
      for (const bool scale_lock : {false, true}) {
        SCOPED_TRACE(::testing::Message() << radial << "/" << lock << "/" << scale_lock);
        const auto source = wall("Array Mapping Source");
        ASSERT_TRUE(editor_.scene().set_transform(source.entity,
            {.position = {4, 1, 2}, .rotation_euler = {0.2f, 0.3f, -0.1f},
             .scale = {1.3f, 0.8f, 1.1f}}).ok());
        const QMat4 initial_sampling = compose_transform(QTransform{
            .position = {0.4f, -0.7f, 0.3f}, .scale = {0.7f, 1.2f, 0.9f}});
        ASSERT_TRUE(session_.scene().set_texture_mapping(source.entity, {initial_sampling}));
        TextureLockSettings settings{lock, scale_lock};
        session_.modeling().set_texture_lock_settings_provider([&] { return settings; });
        const auto source_world = session_.scene().world_transform_matrix(source.entity);
        const auto source_inverse = try_scene_matrix_inverse(source_world);
        ASSERT_TRUE(source_inverse.has_value());
        ASSERT_TRUE(editor_.selection().select_object(source).ok());
        session_.history().clear();
        std::vector<QuaderModelingObjectRef> elements;
        if (radial) {
          const auto created = editor_.modeling().create_radial_array({
              .sources = {source}, .center_world = {-1, 0, -2},
              .plane_normal_world = {0, 1, 0}, .degrees = 150,
              .copies = 3, .repeats = 2, .repeat_spacing = 2});
          ASSERT_TRUE(created.ok()) << created.error_message();
          elements = created.value().source_arrays.front().elements;
        } else {
          const auto created = editor_.modeling().create_linear_array({
              .source = source, .endpoint_world = {12, -3, 5}, .count = 3});
          ASSERT_TRUE(created.ok()) << created.error_message();
          elements = created.value().elements;
        }
        EXPECT_EQ(session_.history().undo_count(), 1U);
        std::vector<QMat4> recorded;
        for (const auto &element : elements) {
          const auto sampling = effective_texture_sampling_from_local(session_.scene(), element.entity);
          ASSERT_TRUE(sampling.has_value());
          const QMat4 expected = lock ? initial_sampling : scene_transform_multiply(
              scene_transform_multiply(initial_sampling, *source_inverse),
              session_.scene().world_transform_matrix(element.entity));
          expect_uv_matrix(*sampling, expected);
          recorded.push_back(*sampling);
        }
        settings = {!lock, !scale_lock};
        ASSERT_TRUE(editor_.history().undo().ok());
        ASSERT_TRUE(editor_.history().redo().ok());
        for (std::size_t index = 0; index < elements.size(); ++index) {
          const auto sampling = effective_texture_sampling_from_local(session_.scene(), elements[index].entity);
          ASSERT_TRUE(sampling.has_value());
          EXPECT_EQ(sampling->values, recorded[index].values);
        }
        session_.modeling().set_texture_lock_settings_provider({});
      }
    }
  }
}

TEST_F(UvNativeSamplingTests, RectangularNumericRotationMatchesActionAndContinuousHistory) {
  const auto source = wall("Rectangular Numeric Rotation");
  texture_extent(source.entity, 512, 128);
  ASSERT_TRUE(session_.scene().set_texture_mapping(source.entity,
      {compose_transform(QTransform{.position = {0.4f, -0.3f, 0},
                                    .scale = {0.8f, 1.2f, 1}})}));
  ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor_.selection().select_face(source, 1).ok());
  const auto before = displayed(source.entity);
  const double initial_angle = editor_.modeling().face_texture_settings().rotation_degrees;
  ASSERT_DOUBLE_EQ(initial_angle, 37.0);
  session_.history().clear();
  ASSERT_TRUE(rotate_numeric(initial_angle + 10, "rectangular.rotation").ok());
  ASSERT_TRUE(rotate_numeric(initial_angle + 15, "rectangular.rotation").ok());
  EXPECT_EQ(session_.history().undo_count(), 1U);
  EXPECT_EQ(editor_.modeling().face_texture_settings().rotation_degrees, initial_angle + 15);
  const auto numeric = displayed(source.entity);
  ASSERT_EQ(numeric.size(), before.size());
  // Image-axis distances include the retained signed face scales. A pure
  // rotation preserves this metric even for a nonaffine authored quad.
  const auto physical_squared = [](QVec2 a, QVec2 b) {
    const double u = (static_cast<double>(a.x) - b.x) * 512.0 * -0.7;
    const double v = (static_cast<double>(a.y) - b.y) * 128.0 * 1.3;
    return u * u + v * v;
  };
  for (std::size_t index = 1; index < before.size(); ++index) {
    const double expected = physical_squared(before[index].uv, before[0].uv);
    EXPECT_NEAR(physical_squared(numeric[index].uv, numeric[0].uv), expected,
                std::max(0.001, expected * 0.00001));
  }
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_displayed_corners(displayed(source.entity), before);
  EXPECT_EQ(editor_.modeling().face_texture_settings().rotation_degrees, initial_angle);
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_displayed_corners(displayed(source.entity), numeric);
  ASSERT_TRUE(editor_.history().undo().ok());
  const auto action = editor_.modeling().apply_face_texture_action({
      .action = QuaderFaceTextureAction::RotateCounterClockwise,
      .selection_token = editor_.modeling().face_texture_settings().selection_token});
  ASSERT_TRUE(action.ok()) << action.error_message();
  expect_displayed_corners(displayed(source.entity), numeric);
  session_.history().clear();
  const auto unchanged = rotate_numeric(
      editor_.modeling().face_texture_settings().rotation_degrees);
  ASSERT_TRUE(unchanged.ok()) << unchanged.error_message();
  EXPECT_FALSE(unchanged.changed());
  EXPECT_EQ(session_.history().undo_count(), 0U);
  ASSERT_TRUE(rotate_numeric(-15).ok());
  EXPECT_EQ(editor_.modeling().face_texture_settings().rotation_degrees, -15);
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_displayed_corners(displayed(source.entity), numeric);
}

TEST_F(UvNativeSamplingTests, MixedMaterialNumericRotationKeepsDimensionsAndPriorRepeatAction) {
  const auto wide = wall("Wide UV Image");
  const auto tall = wall("Tall UV Image", 6);
  texture_extent(wide.entity, 512, 128);
  texture_extent(tall.entity, 128, 512);
  ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor_.selection().select_face(wide, 1).ok());
  ASSERT_TRUE(editor_.selection().select_face(tall, 1, QuaderSelectionEdit::Add).ok());
  ASSERT_TRUE(editor_.modeling().apply_face_texture_action({
      .action = QuaderFaceTextureAction::ShiftRight,
      .selection_token = editor_.modeling().face_texture_settings().selection_token}).ok());
  session_.history().clear();
  const double initial_angle = editor_.modeling().face_texture_settings().rotation_degrees;
  ASSERT_TRUE(rotate_numeric(initial_angle + 15).ok());
  const auto wide_numeric = displayed(wide.entity);
  const auto tall_numeric = displayed(tall.entity);
  ASSERT_TRUE(editor_.history().undo().ok());
  ASSERT_TRUE(editor_.modeling().apply_face_texture_action({
      .action = QuaderFaceTextureAction::RotateCounterClockwise,
      .selection_token = editor_.modeling().face_texture_settings().selection_token}).ok());
  expect_displayed_corners(displayed(wide.entity), wide_numeric);
  expect_displayed_corners(displayed(tall.entity), tall_numeric);

  // Seed a distinct repeatable action, then edit the number. Numeric editing
  // must keep the prior repeat owner and must not become a discrete rotation.
  ASSERT_TRUE(editor_.modeling().apply_face_texture_action({
      .action = QuaderFaceTextureAction::ShiftRight,
      .selection_token = editor_.modeling().face_texture_settings().selection_token}).ok());
  ASSERT_TRUE(rotate_numeric(30).ok());
  const auto current_wide = session_.modeling().compile_object_mesh(wide.modeling_object_id);
  const auto current_tall = session_.modeling().compile_object_mesh(tall.modeling_object_id);
  ASSERT_TRUE(current_wide.has_value());
  ASSERT_TRUE(current_tall.has_value());
  const auto wide_settings = current_wide->authored_polygon_document->faces.front().texture_transform;
  const auto tall_settings = current_tall->authored_polygon_document->faces.front().texture_transform;
  ASSERT_TRUE(editor_.modeling().repeat_last_action().ok());
  const auto repeated_wide = session_.modeling().compile_object_mesh(wide.modeling_object_id);
  const auto repeated_tall = session_.modeling().compile_object_mesh(tall.modeling_object_id);
  ASSERT_TRUE(repeated_wide.has_value());
  ASSERT_TRUE(repeated_tall.has_value());
  const auto wide_after = repeated_wide->authored_polygon_document->faces.front().texture_transform;
  const auto tall_after = repeated_tall->authored_polygon_document->faces.front().texture_transform;
  EXPECT_FLOAT_EQ(wide_after.rotation_degrees, wide_settings.rotation_degrees);
  EXPECT_FLOAT_EQ(tall_after.rotation_degrees, tall_settings.rotation_degrees);
  EXPECT_NEAR(wide_after.shift_uv.x, wide_settings.shift_uv.x - 1.0f / 512.0f, 0.000001f);
  EXPECT_NEAR(tall_after.shift_uv.x, tall_settings.shift_uv.x - 1.0f / 128.0f, 0.000001f);
}

quader_poly::Document translated_projection_fixture(
    quader_poly::SurfaceShading shading, bool authored_uvs) {
  quader_poly::Document document;
  const std::array points{
      QVec3{0, 0, 0}, QVec3{2, 0, 0}, QVec3{2, 2, 0},
      QVec3{0, 2, 0}, QVec3{4, 0, 1}, QVec3{4, 2, 1}};
  std::array<quader_poly::ElementId, 6> vertices;
  for (std::size_t index = 0u; index < points.size(); ++index) {
    vertices[index] = quader_poly::add_vertex(document, points[index]);
  }
  (void)quader_poly::add_face(
      document, std::array{vertices[0], vertices[1], vertices[2], vertices[3]});
  (void)quader_poly::add_face(
      document, std::array{vertices[1], vertices[4], vertices[5], vertices[2]});
  for (auto &face : document.faces) {
    face.normal_shading = shading;
    face.texture_transform = {
        .scale = {-0.7f, 1.3f}, .shift_uv = {0.23f, -0.17f},
        .rotation_degrees = 37.0f};
    if (authored_uvs) {
      face.uvs = {{0.1f, 0.2f}, {1.8f, 0.15f}, {1.5f, 2.7f}, {0.2f, 1.1f}};
    }
  }
  return document;
}

void expect_canonical_projection(const MeshGeometryPayload &actual,
                                 const MeshGeometryPayload &expected) {
  ASSERT_EQ(actual.vertices.size(), expected.vertices.size());
  ASSERT_EQ(actual.indices, expected.indices);
  ASSERT_EQ(actual.primitives, expected.primitives);
  for (std::size_t index = 0u; index < actual.vertices.size(); ++index) {
    SCOPED_TRACE(index);
    const MeshVertex &left = actual.vertices[index];
    const MeshVertex &right = expected.vertices[index];
    for (const auto pair : std::array{
             std::pair{left.position, right.position},
             std::pair{left.normal, right.normal},
             std::pair{left.uv_geometric_normal, right.uv_geometric_normal}}) {
      EXPECT_EQ(pair.first.x, pair.second.x);
      EXPECT_EQ(pair.first.y, pair.second.y);
      EXPECT_EQ(pair.first.z, pair.second.z);
    }
    EXPECT_EQ(left.uv.x, right.uv.x);
    EXPECT_EQ(left.uv.y, right.uv.y);
    for (std::size_t component = 0u; component < left.uv_carrier.size(); ++component) {
      EXPECT_EQ(left.uv_carrier[component], right.uv_carrier[component]);
    }
    EXPECT_EQ(left.source_vertex_id, right.source_vertex_id);
    EXPECT_EQ(left.rgba, right.rgba);
  }
}

TEST(MeshProjectionPositionTests,
     WholeMeshTranslationMatchesCanonicalNormalsAndBothUvPolicies) {
  for (const auto shading : std::array{
           quader_poly::SurfaceShading::Authored,
           quader_poly::SurfaceShading::Smooth,
           quader_poly::SurfaceShading::Flat}) {
    for (const bool authored_uvs : {false, true}) {
      SCOPED_TRACE(static_cast<int>(shading));
      SCOPED_TRACE(authored_uvs);
      const auto source = translated_projection_fixture(shading, authored_uvs);
      const auto before = MeshProjectionService::sdk_document_from_polygon_document(
                              source).or_throw();
      const auto initial = MeshProjectionService::compile_sdk_document_mesh(before);
      ASSERT_TRUE(initial.has_value());
      auto moved = source;
      for (auto &vertex : moved.vertices) {
        vertex.position += QVec3{4.0f, -2.0f, 8.0f};
      }
      const auto after = MeshProjectionService::sdk_document_from_polygon_document(
                             moved).or_throw();
      const auto stats_before = MeshProjectionService::position_update_stats();
      const auto retained =
          MeshProjectionService::update_sdk_document_mesh_positions(after, *initial);
      const auto canonical = MeshProjectionService::compile_sdk_document_mesh(after);
      ASSERT_TRUE(retained.has_value());
      ASSERT_TRUE(canonical.has_value());
      expect_canonical_projection(retained->geometry(), canonical->geometry());
      const auto restored =
          MeshProjectionService::update_sdk_document_mesh_positions(before, *retained);
      ASSERT_TRUE(restored.has_value());
      expect_canonical_projection(restored->geometry(), initial->geometry());
      const auto stats_after = MeshProjectionService::position_update_stats();
      const bool flat = shading == quader_poly::SurfaceShading::Flat;
      EXPECT_EQ(stats_after.retained_translation_count -
                    stats_before.retained_translation_count, flat ? 0U : 2U);
      EXPECT_EQ(stats_after.retained_flat_face_count -
                    stats_before.retained_flat_face_count, flat ? 2U : 0U);
      EXPECT_EQ(stats_after.full_compile_count, stats_before.full_compile_count);
    }
  }
}

TEST(MeshProjectionPositionTests,
     RetainedCoordinateStreamsMatchCanonicalAcrossEditsAndPinnedRestore) {
  for (const auto shading : {quader_poly::SurfaceShading::Authored,
                            quader_poly::SurfaceShading::Flat}) {
    for (const bool authored_uvs : {false, true}) {
      SCOPED_TRACE(static_cast<int>(shading));
      SCOPED_TRACE(authored_uvs);
      const auto source = translated_projection_fixture(shading, authored_uvs);
      auto document = MeshProjectionService::sdk_document_from_polygon_document(source).or_throw();
      ASSERT_TRUE(document.select_vertex(modeling::make_id<modeling::VertexTag>(
          source.vertices[2].id)).ok());
      const auto prepared = document.prepare_selection_transform().or_throw();
      const auto baseline = MeshProjectionService::compile_sdk_document_mesh(document);
      ASSERT_TRUE(baseline);
      const std::array movable{source.vertices[2].id};
      const auto neighborhood = MeshProjectionService::prepare_position_update_plan(*baseline, movable);
      const auto plan = MeshProjectionService::prepare_retained_position_projection(
          document, *baseline, neighborhood.get());
      ASSERT_TRUE(plan);
      const auto baseline_geometry = baseline->geometry_payload();
      auto previous_document = document;
      std::shared_ptr<const MeshGeometryProjection> previous;
      std::shared_ptr<const MeshGeometryProjection> pinned;
      std::optional<MeshAsset> pinned_canonical;
      const auto stats_before = MeshProjectionService::position_update_stats();
      for (const auto delta : {modeling::Vec3{0.25F, 0.125F, 0.75F},
                               modeling::Vec3{-2.5F, -1.5F, 0.0F}, modeling::Vec3{},
                               modeling::Vec3{0.5F, -0.25F, 1.0F}}) {
        auto evaluated = prepared.evaluate_translation(delta).or_throw();
        const auto work_before = prepared.work_stats();
        const auto projection = MeshProjectionService::project_retained_mesh_positions(
            evaluated.document, previous_document, *plan, previous);
        ASSERT_TRUE(projection);
        EXPECT_EQ(prepared.work_stats().topology_materialization_count,
                  work_before.topology_materialization_count);
        EXPECT_EQ(projection->source_geometry, baseline_geometry);
        ASSERT_TRUE(projection->positions);
        EXPECT_EQ(projection->positions->size(), baseline_geometry->vertices.size());
        const auto canonical = MeshProjectionService::compile_sdk_document_mesh(evaluated.document);
        ASSERT_TRUE(canonical);
        expect_canonical_projection(*projection->materialized_geometry(), canonical->geometry());
        EXPECT_EQ(projection->materialized_geometry(), projection->materialized_geometry());
        const auto adopted = MeshProjectionService::materialize_retained_mesh_projection(
            evaluated.document, *baseline, *projection);
        EXPECT_EQ(adopted.geometry_payload(), projection->materialized_geometry());
        ASSERT_TRUE(adopted.authored_polygon_document);
        ASSERT_TRUE(canonical->authored_polygon_document);
        EXPECT_TRUE(mesh_asset_payload_detail::same_polygon_document(
            *adopted.authored_polygon_document, *canonical->authored_polygon_document));
        MeshAsset sibling_source = *baseline;
        sibling_source.name = "Independent retained participant";
        const auto sibling = MeshProjectionService::materialize_retained_mesh_projection(
            evaluated.document, sibling_source, *projection, adopted.authored_polygon_document);
        EXPECT_EQ(sibling.authored_polygon_document, adopted.authored_polygon_document);
        EXPECT_EQ(sibling.name, sibling_source.name);
        EXPECT_EQ(sibling.geometry_payload(), adopted.geometry_payload());
        // Equal render geometry does not prove equal authored identity cursors.
        auto different_identity = std::make_shared<quader_poly::Document>(
            *adopted.authored_polygon_document);
        ++different_identity->next_vertex_id;
        ++different_identity->next_face_id;
        const auto independent = MeshProjectionService::materialize_retained_mesh_projection(
            evaluated.document, sibling_source, *projection, different_identity);
        EXPECT_NE(independent.authored_polygon_document, different_identity);
        ASSERT_TRUE(independent.authored_polygon_document);
        EXPECT_TRUE(mesh_asset_payload_detail::same_polygon_document(
            *independent.authored_polygon_document, *canonical->authored_polygon_document));
        EXPECT_EQ(independent.name, sibling_source.name);
        EXPECT_EQ(MeshProjectionService::project_retained_mesh_positions(
            evaluated.document, evaluated.document, *plan, projection), projection);
        if (!pinned) {
          pinned = projection;
          pinned_canonical = canonical;
        }
        expect_canonical_projection(*pinned->materialized_geometry(), pinned_canonical->geometry());
        EXPECT_EQ(baseline->geometry_payload(), baseline_geometry);
        previous_document = std::move(evaluated.document);
        previous = projection;
      }
      EXPECT_EQ(MeshProjectionService::position_update_stats().retained_stream_projection_count -
                    stats_before.retained_stream_projection_count, 4U);
      const auto rejected = prepared.evaluate_translation(
          {std::numeric_limits<float>::infinity(), 0, 0});
      EXPECT_FALSE(rejected.ok());
      expect_canonical_projection(*pinned->materialized_geometry(), pinned_canonical->geometry());
    }
  }
}

TEST(MeshProjectionPositionTests,
     PartialComponentAndReflectedUpdatesKeepCanonicalDerivedGeometry) {
  for (const bool authored_uvs : {false, true}) {
    const auto source = translated_projection_fixture(
        quader_poly::SurfaceShading::Authored, authored_uvs);
    const auto before = MeshProjectionService::sdk_document_from_polygon_document(
                            source).or_throw();
    const auto initial = MeshProjectionService::compile_sdk_document_mesh(before);
    ASSERT_TRUE(initial.has_value());
    for (const bool reflection : {false, true}) {
      SCOPED_TRACE(authored_uvs);
      SCOPED_TRACE(reflection);
      auto changed = source;
      if (reflection) {
        for (auto &vertex : changed.vertices) vertex.position.x = -vertex.position.x;
      } else {
        changed.vertices[2].position += QVec3{0.5f, 0.25f, 1.0f};
      }
      const auto after = MeshProjectionService::sdk_document_from_polygon_document(
                             changed).or_throw();
      const auto stats_before = MeshProjectionService::position_update_stats();
      const auto retained =
          MeshProjectionService::update_sdk_document_mesh_positions(after, *initial);
      const auto canonical = MeshProjectionService::compile_sdk_document_mesh(after);
      ASSERT_TRUE(retained.has_value());
      ASSERT_TRUE(canonical.has_value());
      expect_canonical_projection(retained->geometry(), canonical->geometry());
      const auto stats_after = MeshProjectionService::position_update_stats();
      EXPECT_EQ(stats_after.full_compile_count - stats_before.full_compile_count, 1U);
      EXPECT_EQ(stats_after.retained_translation_count,
                stats_before.retained_translation_count);
    }
  }
}

TEST(MeshProjectionPositionTests,
     RoundedTranslationAndCollinearFaceFallbackMatchCanonical) {
  for (const bool collinear_corner : {false, true}) {
    SCOPED_TRACE(collinear_corner);
    auto source = translated_projection_fixture(
        quader_poly::SurfaceShading::Authored, false);
    if (collinear_corner) {
      const auto midpoint = quader_poly::add_vertex(source, {1.0f, 0.0f, 0.0f});
      source.faces.front().vertices.insert(
          source.faces.front().vertices.begin() + 1, midpoint);
    }
    const auto before = MeshProjectionService::sdk_document_from_polygon_document(
                            source).or_throw();
    const auto initial = MeshProjectionService::compile_sdk_document_mesh(before);
    ASSERT_TRUE(initial.has_value());
    auto moved = source;
    for (auto &vertex : moved.vertices) {
      vertex.position.x += collinear_corner ? 4.0f : 0.025f;
    }
    const auto after = MeshProjectionService::sdk_document_from_polygon_document(
                           moved).or_throw();
    const auto stats_before = MeshProjectionService::position_update_stats();
    const auto retained =
        MeshProjectionService::update_sdk_document_mesh_positions(after, *initial);
    const auto canonical = MeshProjectionService::compile_sdk_document_mesh(after);
    ASSERT_TRUE(retained.has_value());
    ASSERT_TRUE(canonical.has_value());
    expect_canonical_projection(retained->geometry(), canonical->geometry());
    const auto stats_after = MeshProjectionService::position_update_stats();
    EXPECT_EQ(stats_after.full_compile_count - stats_before.full_compile_count, 1U);
    EXPECT_EQ(stats_after.retained_translation_count,
              stats_before.retained_translation_count);
  }
}

void append_remote_projection_faces(quader_poly::Document &document) {
  for (int index = 0; index < 6; ++index) {
    const float x = 20.0f + 4.0f * static_cast<float>(index);
    const std::array vertices{
        quader_poly::add_vertex(document, {x, 0.0f, 0.0f}),
        quader_poly::add_vertex(document, {x + 2.0f, 0.0f, 0.0f}),
        quader_poly::add_vertex(document, {x + 2.0f, 2.0f, 0.0f}),
        quader_poly::add_vertex(document, {x, 2.0f, 0.0f})};
    (void)quader_poly::add_face(document, vertices);
  }
}

TEST(MeshProjectionPositionTests,
     RetainedCoordinateNeighborhoodPreservesCreasesAndRejectsAnotherTopology) {
  auto source = translated_projection_fixture(quader_poly::SurfaceShading::Authored, true);
  append_remote_projection_faces(source);
  auto document = MeshProjectionService::sdk_document_from_polygon_document(source).or_throw();
  const std::array selected{modeling::make_id<modeling::VertexTag>(source.vertices[4].id),
                            modeling::make_id<modeling::VertexTag>(source.vertices[5].id)};
  ASSERT_TRUE(document.select_vertices(selected).ok());
  const auto prepared = document.prepare_selection_transform().or_throw();
  const auto baseline = MeshProjectionService::compile_sdk_document_mesh(document);
  ASSERT_TRUE(baseline);
  const std::array movable{source.vertices[4].id, source.vertices[5].id};
  const auto neighborhood = MeshProjectionService::prepare_position_update_plan(*baseline, movable);
  const auto plan = MeshProjectionService::prepare_retained_position_projection(
      document, *baseline, neighborhood.get());
  ASSERT_TRUE(plan);
  auto previous_document = document;
  std::shared_ptr<const MeshGeometryProjection> previous;
  const auto stats_before = MeshProjectionService::position_update_stats();
  for (const float angle : {30.0F, 50.0F, 39.99F, 40.0F, 140.0F, 0.0F}) {
    SCOPED_TRACE(angle);
    const float radians = angle * 3.14159265358979323846F / 180.0F;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    auto evaluated = prepared.evaluate_transform({
        .x_axis = {cosine, 0, sine}, .y_axis = {0, 1, 0},
        .z_axis = {-sine, 0, cosine}, .origin = {2 - 2 * cosine, 0, -2 * sine}}).or_throw();
    const auto projection = MeshProjectionService::project_retained_mesh_positions(
        evaluated.document, previous_document, *plan, previous);
    ASSERT_TRUE(projection);
    const auto canonical = MeshProjectionService::compile_sdk_document_mesh(evaluated.document);
    ASSERT_TRUE(canonical);
    expect_canonical_projection(*projection->materialized_geometry(), canonical->geometry());
    previous_document = std::move(evaluated.document);
    previous = projection;
  }
  const auto stats_after = MeshProjectionService::position_update_stats();
  EXPECT_EQ(stats_after.retained_partial_count - stats_before.retained_partial_count, 6U);
  EXPECT_EQ(stats_after.full_compile_count, stats_before.full_compile_count);
  auto unrelated = MeshProjectionService::sdk_document_from_polygon_document(source).or_throw();
  EXPECT_FALSE(MeshProjectionService::project_retained_mesh_positions(
      unrelated, previous_document, *plan, previous));
  expect_canonical_projection(*previous->materialized_geometry(), baseline->geometry());
}

TEST(MeshProjectionPositionTests,
     PartialNormalNeighborhoodMatchesCanonicalAcrossCreasesAndOverrides) {
  for (const bool authored_uvs : {false, true}) {
    for (const bool flat_neighbor : {false, true}) {
      for (int edge_policy = 0; edge_policy < 3; ++edge_policy) {
        SCOPED_TRACE(authored_uvs);
        SCOPED_TRACE(flat_neighbor);
        SCOPED_TRACE(edge_policy);
        auto source = translated_projection_fixture(
            quader_poly::SurfaceShading::Authored, authored_uvs);
        if (flat_neighbor) {
          source.faces.front().normal_shading = quader_poly::SurfaceShading::Flat;
        }
        const auto shared_edge = quader_poly::make_edge(
            source.faces.front().vertices[1], source.faces.front().vertices[2]);
        if (edge_policy == 1) source.hard_normal_edges.push_back(shared_edge);
        if (edge_policy == 2) source.soft_normal_edges.push_back(shared_edge);
        append_remote_projection_faces(source);
        const auto before = MeshProjectionService::sdk_document_from_polygon_document(
                                source).or_throw();
        auto retained = MeshProjectionService::compile_sdk_document_mesh(before);
        ASSERT_TRUE(retained.has_value());
        auto planned = *retained;
        const std::array movable_vertex_ids{
            source.vertices[4].id, source.vertices[5].id};
        const auto plan_stats_before = MeshProjectionService::position_update_stats();
        const auto plan = MeshProjectionService::prepare_position_update_plan(
            planned, movable_vertex_ids);
        ASSERT_TRUE(plan);
        const auto plan_stats = MeshProjectionService::position_update_stats();
        EXPECT_EQ(plan_stats.partial_plan_build_count -
                      plan_stats_before.partial_plan_build_count, 1U);
        EXPECT_GT(plan_stats.partial_plan_face_visit_count,
                  plan_stats_before.partial_plan_face_visit_count);
        for (const float angle : {30.0f, 50.0f, 39.99f, 40.0f, 0.0f, 140.0f}) {
          SCOPED_TRACE(angle);
          auto changed = source;
          const float radians = angle * 3.14159265358979323846f / 180.0f;
          for (const std::size_t vertex_index : {4U, 5U}) {
            changed.vertices[vertex_index].position.x = 2.0f + 2.0f * std::cos(radians);
            changed.vertices[vertex_index].position.z = 2.0f * std::sin(radians);
          }
          const auto after = MeshProjectionService::sdk_document_from_polygon_document(
                                 changed).or_throw();
          const auto stats_before = MeshProjectionService::position_update_stats();
          retained = MeshProjectionService::update_sdk_document_mesh_positions(
              after, *retained);
          const auto canonical = MeshProjectionService::compile_sdk_document_mesh(after);
          ASSERT_TRUE(retained.has_value());
          ASSERT_TRUE(canonical.has_value());
          expect_canonical_projection(retained->geometry(), canonical->geometry());
          const auto stats_after = MeshProjectionService::position_update_stats();
          EXPECT_EQ(stats_after.full_compile_count, stats_before.full_compile_count);
          EXPECT_EQ(stats_after.retained_partial_count -
                        stats_before.retained_partial_count, 1U);
          EXPECT_EQ(stats_after.partial_compiled_face_count -
                        stats_before.partial_compiled_face_count, 2U);
          const auto planned_update =
              MeshProjectionService::update_sdk_document_mesh_positions(
                  after, planned, plan.get());
          ASSERT_TRUE(planned_update.has_value());
          expect_canonical_projection(planned_update->geometry(), canonical->geometry());
          planned = *planned_update;
          const auto planned_stats = MeshProjectionService::position_update_stats();
          EXPECT_EQ(planned_stats.partial_plan_build_count, stats_after.partial_plan_build_count);
          EXPECT_EQ(planned_stats.partial_plan_face_visit_count,
                    stats_after.partial_plan_face_visit_count);
          EXPECT_EQ(planned_stats.partial_plan_reuse_count -
                        stats_after.partial_plan_reuse_count, 1U);
          EXPECT_EQ(planned_stats.partial_plan_fallback_count,
                    stats_after.partial_plan_fallback_count);
          EXPECT_EQ(planned_stats.full_compile_count, stats_after.full_compile_count);
        }
      }
    }
  }
}

TEST(MeshProjectionPositionTests,
     PartialDeformationsMatchCanonicalTriangulationUvsAndWinding) {
  for (const bool authored_uvs : {false, true}) {
    auto source = translated_projection_fixture(
        quader_poly::SurfaceShading::Authored, authored_uvs);
    append_remote_projection_faces(source);
    const auto before = MeshProjectionService::sdk_document_from_polygon_document(
                            source).or_throw();
    const auto initial = MeshProjectionService::compile_sdk_document_mesh(before);
    ASSERT_TRUE(initial.has_value());
    for (int deformation = 0; deformation < 4; ++deformation) {
      SCOPED_TRACE(authored_uvs);
      SCOPED_TRACE(deformation);
      auto changed = source;
      if (deformation == 0) {
        changed.vertices[2].position = {-0.5f, 0.5f, 0.0f};
      } else {
        for (const std::size_t vertex_index : {4U, 5U}) {
          auto &position = changed.vertices[vertex_index].position;
          if (deformation == 1) {
            position = {position.z, position.y, -position.x};
          } else if (deformation == 2) {
            position.x *= 0.25f;
            position.y *= 1.5f;
            position.z *= 2.0f;
          } else {
            position.x = -position.x;
          }
        }
      }
      const auto after = MeshProjectionService::sdk_document_from_polygon_document(
                             changed).or_throw();
      const std::vector<std::uint32_t> movable_vertex_ids = deformation == 0
          ? std::vector<std::uint32_t>{source.vertices[2].id}
          : std::vector<std::uint32_t>{source.vertices[4].id, source.vertices[5].id};
      const auto plan = MeshProjectionService::prepare_position_update_plan(
          *initial, movable_vertex_ids);
      ASSERT_TRUE(plan);
      const auto stats_before = MeshProjectionService::position_update_stats();
      const auto retained =
          MeshProjectionService::update_sdk_document_mesh_positions(after, *initial);
      const auto canonical = MeshProjectionService::compile_sdk_document_mesh(after);
      ASSERT_TRUE(retained.has_value());
      ASSERT_TRUE(canonical.has_value());
      expect_canonical_projection(retained->geometry(), canonical->geometry());
      EXPECT_EQ(MeshProjectionService::position_update_stats().full_compile_count,
                stats_before.full_compile_count);
      if (deformation == 0) {
        EXPECT_NE(retained->geometry().indices, initial->geometry().indices);
      }
      const auto restored =
          MeshProjectionService::update_sdk_document_mesh_positions(before, *retained);
      ASSERT_TRUE(restored.has_value());
      expect_canonical_projection(restored->geometry(), initial->geometry());
      const auto plan_stats = MeshProjectionService::position_update_stats();
      const auto planned = MeshProjectionService::update_sdk_document_mesh_positions(
          after, *initial, plan.get());
      ASSERT_TRUE(planned.has_value());
      expect_canonical_projection(planned->geometry(), canonical->geometry());
      const auto planned_restore =
          MeshProjectionService::update_sdk_document_mesh_positions(
              before, *planned, plan.get());
      ASSERT_TRUE(planned_restore.has_value());
      expect_canonical_projection(planned_restore->geometry(), initial->geometry());
      const auto planned_stats = MeshProjectionService::position_update_stats();
      EXPECT_EQ(planned_stats.partial_plan_build_count, plan_stats.partial_plan_build_count);
      EXPECT_EQ(planned_stats.partial_plan_face_visit_count,
                plan_stats.partial_plan_face_visit_count);
      EXPECT_EQ(planned_stats.partial_plan_reuse_count - plan_stats.partial_plan_reuse_count, 2U);
      EXPECT_EQ(planned_stats.partial_plan_fallback_count, plan_stats.partial_plan_fallback_count);
      EXPECT_EQ(planned_stats.full_compile_count, plan_stats.full_compile_count);
    }
  }
}

TEST(MeshProjectionPositionTests,
     RetainedPositionPlanReusesMovableSupersetAcrossAbsoluteUpdatesAndRestore) {
  auto source = translated_projection_fixture(
      quader_poly::SurfaceShading::Authored, true);
  append_remote_projection_faces(source);
  auto current = MeshProjectionService::compile_polygon_document_mesh(source);
  current.replace_authored_polygon_document(source);
  const std::array movable_vertex_ids{
      source.vertices[4].id, source.vertices[5].id};
  const auto stats_before = MeshProjectionService::position_update_stats();
  const auto plan = MeshProjectionService::prepare_position_update_plan(
      current, movable_vertex_ids);
  ASSERT_TRUE(plan);
  const auto plan_stats = MeshProjectionService::position_update_stats();
  EXPECT_EQ(plan_stats.partial_plan_build_count - stats_before.partial_plan_build_count, 1U);
  EXPECT_GT(plan_stats.partial_plan_face_visit_count, stats_before.partial_plan_face_visit_count);
  const std::array gestures{
      std::array{QVec3{0.5f, 0.0f, 0.75f}, QVec3{0.5f, 0.0f, 0.75f}},
      std::array{QVec3{0.75f, 0.0f, 1.25f}, QVec3{0.5f, 0.0f, 0.75f}},
      std::array{QVec3{0.75f, 0.0f, 1.25f}, QVec3{}},
      std::array{QVec3{}, QVec3{}},
      std::array{QVec3{-0.25f, 0.0f, -0.5f}, QVec3{0.0f, 0.25f, 0.75f}}};
  for (std::size_t gesture_index = 0U; gesture_index < gestures.size(); ++gesture_index) {
    SCOPED_TRACE(gesture_index);
    auto changed = source;
    changed.vertices[4].position += gestures[gesture_index][0];
    changed.vertices[5].position += gestures[gesture_index][1];
    const auto pinned = current.geometry_payload();
    const auto pinned_content = pinned->content_id();
    const auto expected_current = MeshProjectionService::compile_polygon_document_mesh(
        *current.authored_polygon_document);
    const auto update_stats = MeshProjectionService::position_update_stats();
    auto projected = MeshProjectionService::update_polygon_document_mesh_positions(
        changed, current, plan.get());
    const auto canonical = MeshProjectionService::compile_polygon_document_mesh(changed);
    expect_canonical_projection(projected.geometry(), canonical.geometry());
    EXPECT_EQ(current.geometry_payload(), pinned);
    EXPECT_EQ(pinned->content_id(), pinned_content);
    expect_canonical_projection(*pinned, expected_current.geometry());
    const auto reused_stats = MeshProjectionService::position_update_stats();
    EXPECT_EQ(reused_stats.partial_plan_build_count, plan_stats.partial_plan_build_count);
    EXPECT_EQ(reused_stats.partial_plan_face_visit_count, plan_stats.partial_plan_face_visit_count);
    EXPECT_EQ(reused_stats.partial_plan_reuse_count - update_stats.partial_plan_reuse_count, 1U);
    EXPECT_EQ(reused_stats.partial_plan_fallback_count, plan_stats.partial_plan_fallback_count);
    EXPECT_EQ(reused_stats.full_compile_count, plan_stats.full_compile_count);
    current = std::move(projected);
  }
}

TEST(MeshProjectionPositionTests,
     FullNeighborhoodPositionPlanRetainsPreparationAcrossCanonicalFallbacks) {
  const auto source = translated_projection_fixture(
      quader_poly::SurfaceShading::Authored, true);
  auto current = MeshProjectionService::compile_polygon_document_mesh(source);
  current.replace_authored_polygon_document(source);
  const std::array movable_vertex_ids{source.vertices[4].id, source.vertices[5].id};
  const auto stats_before = MeshProjectionService::position_update_stats();
  const auto plan = MeshProjectionService::prepare_position_update_plan(current, movable_vertex_ids);
  ASSERT_TRUE(plan);
  const auto plan_stats = MeshProjectionService::position_update_stats();
  EXPECT_EQ(plan_stats.partial_plan_build_count - stats_before.partial_plan_build_count, 1U);
  EXPECT_GT(plan_stats.partial_plan_face_visit_count, stats_before.partial_plan_face_visit_count);
  for (const float height : {0.5f, -0.5f, 1.0f}) {
    SCOPED_TRACE(height);
    auto changed = source;
    changed.vertices[4].position.z = height;
    changed.vertices[5].position.z = height;
    const auto update_stats = MeshProjectionService::position_update_stats();
    auto projected = MeshProjectionService::update_polygon_document_mesh_positions(
        changed, current, plan.get());
    const auto canonical = MeshProjectionService::compile_polygon_document_mesh(changed);
    expect_canonical_projection(projected.geometry(), canonical.geometry());
    const auto fallback_stats = MeshProjectionService::position_update_stats();
    EXPECT_EQ(fallback_stats.partial_plan_build_count, plan_stats.partial_plan_build_count);
    EXPECT_EQ(fallback_stats.partial_plan_face_visit_count, plan_stats.partial_plan_face_visit_count);
    EXPECT_EQ(fallback_stats.partial_plan_fallback_count - update_stats.partial_plan_fallback_count, 1U);
    EXPECT_EQ(fallback_stats.full_compile_count - update_stats.full_compile_count, 1U);
    current = std::move(projected);
  }
}

TEST(MeshProjectionPositionTests,
     IncompatiblePositionPlanFallsBackWithoutMutatingPinnedSourceOrPlan) {
  enum class PlanInvalidation {
    AuthoredUv,
    Shading,
    Crease,
    AuthoredLayout,
    CompiledLayout,
    OutsideMovableCandidate,
    OutsideMovableCurrent,
  };
  auto source = translated_projection_fixture(
      quader_poly::SurfaceShading::Authored, true);
  append_remote_projection_faces(source);
  auto initial = MeshProjectionService::compile_polygon_document_mesh(source);
  initial.replace_authored_polygon_document(source);
  const std::array movable_vertex_ids{
      source.vertices[4].id, source.vertices[5].id};
  const auto plan = MeshProjectionService::prepare_position_update_plan(
      initial, movable_vertex_ids);
  ASSERT_TRUE(plan);
  const auto plan_stats = MeshProjectionService::position_update_stats();
  for (const auto invalidation : std::array{
           PlanInvalidation::AuthoredUv, PlanInvalidation::Shading,
           PlanInvalidation::Crease, PlanInvalidation::AuthoredLayout,
           PlanInvalidation::CompiledLayout, PlanInvalidation::OutsideMovableCandidate,
           PlanInvalidation::OutsideMovableCurrent}) {
    SCOPED_TRACE(static_cast<int>(invalidation));
    auto changed = source;
    changed.vertices[4].position.z += 0.5f;
    auto current = initial;
    switch (invalidation) {
    case PlanInvalidation::AuthoredUv:
      changed.faces.front().uvs.front().x += 0.125f;
      break;
    case PlanInvalidation::Shading:
      changed.faces.front().normal_shading = quader_poly::SurfaceShading::Flat;
      break;
    case PlanInvalidation::Crease:
      changed.hard_normal_edges.push_back(quader_poly::make_edge(
          changed.faces.front().vertices[1], changed.faces.front().vertices[2]));
      break;
    case PlanInvalidation::AuthoredLayout:
      std::rotate(changed.faces.front().vertices.begin(),
                  changed.faces.front().vertices.begin() + 1,
                  changed.faces.front().vertices.end());
      break;
    case PlanInvalidation::CompiledLayout:
      current.edit_geometry().vertices.front().source_vertex_id = source.vertices.back().id;
      break;
    case PlanInvalidation::OutsideMovableCandidate:
      changed.vertices.back().position.z += 0.5f;
      break;
    case PlanInvalidation::OutsideMovableCurrent: {
      auto prior = source;
      prior.vertices.back().position.z += 0.5f;
      current = MeshProjectionService::compile_polygon_document_mesh(prior);
      current.replace_authored_polygon_document(std::move(prior));
      break;
    }
    }
    const auto pinned = current.geometry_payload();
    const auto pinned_copy = *pinned;
    const auto authored_pin = current.authored_polygon_document;
    const auto update_stats = MeshProjectionService::position_update_stats();
    const auto rejected_plan_projection =
        MeshProjectionService::update_polygon_document_mesh_positions(changed, current, plan.get());
    const auto canonical = MeshProjectionService::compile_polygon_document_mesh(changed);
    expect_canonical_projection(rejected_plan_projection.geometry(), canonical.geometry());
    EXPECT_EQ(current.geometry_payload(), pinned);
    EXPECT_EQ(current.authored_polygon_document, authored_pin);
    expect_canonical_projection(*pinned, pinned_copy);
    const auto fallback_stats = MeshProjectionService::position_update_stats();
    EXPECT_EQ(fallback_stats.partial_plan_fallback_count - update_stats.partial_plan_fallback_count, 1U);
    EXPECT_EQ(fallback_stats.full_compile_count - update_stats.full_compile_count, 1U);
    EXPECT_EQ(fallback_stats.partial_plan_reuse_count, update_stats.partial_plan_reuse_count);

    auto compatible = source;
    compatible.vertices[4].position.z += 0.75f;
    const auto recovered = MeshProjectionService::update_polygon_document_mesh_positions(
        compatible, initial, plan.get());
    const auto canonical_recovered = MeshProjectionService::compile_polygon_document_mesh(compatible);
    expect_canonical_projection(recovered.geometry(), canonical_recovered.geometry());
    const auto recovered_stats = MeshProjectionService::position_update_stats();
    EXPECT_EQ(recovered_stats.partial_plan_build_count, plan_stats.partial_plan_build_count);
    EXPECT_EQ(recovered_stats.partial_plan_face_visit_count, plan_stats.partial_plan_face_visit_count);
    EXPECT_EQ(recovered_stats.partial_plan_reuse_count - fallback_stats.partial_plan_reuse_count, 1U);
    EXPECT_EQ(recovered_stats.partial_plan_fallback_count, fallback_stats.partial_plan_fallback_count);
    EXPECT_EQ(recovered_stats.full_compile_count, fallback_stats.full_compile_count);
  }
}

TEST(MeshProjectionPositionTests,
     PartialNormalNeighborhoodPreservesCollapsedAndNonmanifoldIncidence) {
  auto source = translated_projection_fixture(
      quader_poly::SurfaceShading::Authored, false);
  const auto shared_a = source.faces.front().vertices[1];
  const auto shared_b = source.faces.front().vertices[2];
  const auto collapsed_vertex =
      quader_poly::add_vertex(source, {2.0f, 1.0f, 0.0f});
  (void)quader_poly::add_face(
      source, std::array{shared_a, shared_b, collapsed_vertex});
  const auto disconnected_a =
      quader_poly::add_vertex(source, {1.0f, -2.0f, 0.0f});
  const auto disconnected_b =
      quader_poly::add_vertex(source, {3.0f, -2.0f, 1.0f});
  (void)quader_poly::add_face(
      source, std::array{shared_a, disconnected_a, disconnected_b});
  source.soft_normal_edges.push_back(quader_poly::make_edge(shared_a, shared_b));
  append_remote_projection_faces(source);
  // SDK import rejects nonmanifold edges. Exercise the native compiler's
  // defensive incidence handling directly without weakening that validation.
  auto retained = MeshProjectionService::compile_polygon_document_mesh(source);
  retained.replace_authored_polygon_document(source);
  auto planned = retained;
  const std::array movable_vertex_ids{
      source.vertices[4].id, source.vertices[5].id, source.vertices[6].id};
  const auto plan = MeshProjectionService::prepare_position_update_plan(
      planned, movable_vertex_ids);
  ASSERT_TRUE(plan);
  for (const float height : {0.5f, -0.5f, 0.0f}) {
    SCOPED_TRACE(height);
    auto changed = source;
    changed.vertices[6].position.z = height;
    changed.vertices[4].position.z = 0.5f;
    changed.vertices[5].position.z = 0.5f;
    const auto stats_before = MeshProjectionService::position_update_stats();
    retained = MeshProjectionService::update_polygon_document_mesh_positions(
        changed, retained);
    const auto canonical = MeshProjectionService::compile_polygon_document_mesh(changed);
    expect_canonical_projection(retained.geometry(), canonical.geometry());
    const auto stats_after = MeshProjectionService::position_update_stats();
    EXPECT_EQ(stats_after.full_compile_count, stats_before.full_compile_count);
    EXPECT_EQ(stats_after.retained_partial_count -
                  stats_before.retained_partial_count, 1U);
    EXPECT_EQ(stats_after.partial_compiled_face_count -
                  stats_before.partial_compiled_face_count, 4U);
    planned = MeshProjectionService::update_polygon_document_mesh_positions(
        changed, planned, plan.get());
    expect_canonical_projection(planned.geometry(), canonical.geometry());
    const auto planned_stats = MeshProjectionService::position_update_stats();
    EXPECT_EQ(planned_stats.partial_plan_build_count, stats_after.partial_plan_build_count);
    EXPECT_EQ(planned_stats.partial_plan_face_visit_count,
              stats_after.partial_plan_face_visit_count);
    EXPECT_EQ(planned_stats.partial_plan_reuse_count - stats_after.partial_plan_reuse_count, 1U);
    EXPECT_EQ(planned_stats.partial_plan_fallback_count, stats_after.partial_plan_fallback_count);
    EXPECT_EQ(planned_stats.full_compile_count, stats_after.full_compile_count);
  }
}

TEST(MeshProjectionPositionTests,
     PartialNgonDeformationAndCollapsedLayoutUseCanonicalProjection) {
  quader_poly::Document source;
  std::vector<quader_poly::ElementId> vertices;
  for (const QVec3 position : std::array{
           QVec3{0.0f, 0.0f, 0.0f}, QVec3{3.0f, 0.0f, 0.0f},
           QVec3{4.0f, 2.0f, 0.0f}, QVec3{2.0f, 4.0f, 0.0f},
           QVec3{0.0f, 2.0f, 0.0f}}) {
    vertices.push_back(quader_poly::add_vertex(source, position));
  }
  (void)quader_poly::add_face(source, vertices);
  source.faces.front().uvs = {
      {0.0f, 0.0f}, {2.0f, 0.1f}, {3.0f, 2.0f}, {1.0f, 3.0f}, {-0.2f, 1.0f}};
  append_remote_projection_faces(source);
  const auto before = MeshProjectionService::sdk_document_from_polygon_document(
                          source).or_throw();
  const auto initial = MeshProjectionService::compile_sdk_document_mesh(before);
  ASSERT_TRUE(initial.has_value());
  const auto pinned = initial->geometry_payload();
  const auto pinned_copy = *pinned;
  const auto plan = MeshProjectionService::prepare_position_update_plan(*initial, vertices);
  ASSERT_TRUE(plan);
  for (const bool collapse : {false, true}) {
    SCOPED_TRACE(collapse);
    auto changed = source;
    if (collapse) {
      for (std::size_t index = 0U; index < vertices.size(); ++index) {
        changed.vertices[index].position = {static_cast<float>(index), 0.0f, 0.0f};
      }
    } else {
      changed.vertices[2].position = {0.5f, 1.0f, 0.25f};
    }
    const auto after = MeshProjectionService::sdk_document_from_polygon_document(
                           changed).or_throw();
    const auto stats_before = MeshProjectionService::position_update_stats();
    const auto retained =
        MeshProjectionService::update_sdk_document_mesh_positions(after, *initial);
    const auto canonical = MeshProjectionService::compile_sdk_document_mesh(after);
    ASSERT_TRUE(retained.has_value());
    ASSERT_TRUE(canonical.has_value());
    expect_canonical_projection(retained->geometry(), canonical->geometry());
    if (collapse) {
      EXPECT_LT(canonical->geometry().primitives.size(),
                initial->geometry().primitives.size());
      EXPECT_EQ(MeshProjectionService::position_update_stats().full_compile_count -
                    stats_before.full_compile_count, 1U);
    }
    const auto restored =
        MeshProjectionService::update_sdk_document_mesh_positions(before, *retained);
    ASSERT_TRUE(restored.has_value());
    expect_canonical_projection(restored->geometry(), initial->geometry());
    const auto update_stats = MeshProjectionService::position_update_stats();
    const auto planned = MeshProjectionService::update_sdk_document_mesh_positions(
        after, *initial, plan.get());
    ASSERT_TRUE(planned.has_value());
    expect_canonical_projection(planned->geometry(), canonical->geometry());
    const auto planned_stats = MeshProjectionService::position_update_stats();
    EXPECT_EQ(planned_stats.partial_plan_build_count, update_stats.partial_plan_build_count);
    EXPECT_EQ(planned_stats.partial_plan_face_visit_count, update_stats.partial_plan_face_visit_count);
    EXPECT_EQ(planned_stats.partial_plan_fallback_count - update_stats.partial_plan_fallback_count,
              collapse ? 1U : 0U);
    EXPECT_EQ(planned_stats.full_compile_count - update_stats.full_compile_count, collapse ? 1U : 0U);
    const auto planned_restore = MeshProjectionService::update_sdk_document_mesh_positions(
        before, *planned, plan.get());
    ASSERT_TRUE(planned_restore.has_value());
    expect_canonical_projection(planned_restore->geometry(), initial->geometry());
    const auto recovered = MeshProjectionService::update_sdk_document_mesh_positions(
        before, *initial, plan.get());
    ASSERT_TRUE(recovered.has_value());
    expect_canonical_projection(recovered->geometry(), pinned_copy);
    EXPECT_EQ(initial->geometry_payload(), pinned);
    expect_canonical_projection(*pinned, pinned_copy);
  }
}

TEST(MeshProjectionPositionTests,
     RejectedSdkCandidateLeavesPinnedGeometryAndPositionPlanReusable) {
  auto source = translated_projection_fixture(
      quader_poly::SurfaceShading::Authored, true);
  append_remote_projection_faces(source);
  const auto before = MeshProjectionService::sdk_document_from_polygon_document(source).or_throw();
  const auto initial = MeshProjectionService::compile_sdk_document_mesh(before);
  ASSERT_TRUE(initial.has_value());
  const auto pinned = initial->geometry_payload();
  const auto pinned_copy = *pinned;
  const std::array movable_vertex_ids{source.vertices[4].id, source.vertices[5].id};
  const auto plan = MeshProjectionService::prepare_position_update_plan(*initial, movable_vertex_ids);
  ASSERT_TRUE(plan);
  const auto plan_stats = MeshProjectionService::position_update_stats();
  auto incompatible = source;
  incompatible.faces.pop_back();
  const auto incompatible_sdk =
      MeshProjectionService::sdk_document_from_polygon_document(incompatible).or_throw();
  EXPECT_FALSE(MeshProjectionService::update_sdk_document_mesh_positions(
      incompatible_sdk, *initial, plan.get()).has_value());
  EXPECT_EQ(initial->geometry_payload(), pinned);
  expect_canonical_projection(*pinned, pinned_copy);
  auto compatible = source;
  compatible.vertices[4].position.z += 0.5f;
  const auto compatible_sdk =
      MeshProjectionService::sdk_document_from_polygon_document(compatible).or_throw();
  const auto recovered = MeshProjectionService::update_sdk_document_mesh_positions(
      compatible_sdk, *initial, plan.get());
  const auto canonical = MeshProjectionService::compile_sdk_document_mesh(compatible_sdk);
  ASSERT_TRUE(recovered.has_value());
  ASSERT_TRUE(canonical.has_value());
  expect_canonical_projection(recovered->geometry(), canonical->geometry());
  const auto recovered_stats = MeshProjectionService::position_update_stats();
  EXPECT_EQ(recovered_stats.partial_plan_build_count, plan_stats.partial_plan_build_count);
  EXPECT_EQ(recovered_stats.partial_plan_face_visit_count, plan_stats.partial_plan_face_visit_count);
  EXPECT_EQ(recovered_stats.partial_plan_reuse_count - plan_stats.partial_plan_reuse_count, 1U);
  EXPECT_EQ(initial->geometry_payload(), pinned);
  expect_canonical_projection(*pinned, pinned_copy);
}

TEST(MeshProjectionPositionTests,
     ExportedDocumentProjectionPreservesAuthoredPinAndCanonicalFallback) {
  auto source = translated_projection_fixture(
      quader_poly::SurfaceShading::Authored, true);
  append_remote_projection_faces(source);
  const auto before = MeshProjectionService::sdk_document_from_polygon_document(
                          source).or_throw();
  const auto initial = MeshProjectionService::compile_sdk_document_mesh(before);
  ASSERT_TRUE(initial.has_value());
  for (const bool changed_texture : {false, true}) {
    SCOPED_TRACE(changed_texture);
    auto changed = source;
    changed.vertices[4].position.z = 0.5f;
    if (changed_texture) changed.faces.front().uvs.front().x += 0.125f;
    const auto after = MeshProjectionService::sdk_document_from_polygon_document(
                           changed).or_throw();
    auto exported = MeshProjectionService::polygon_document_from_sdk_document(after);
    ASSERT_TRUE(exported.has_value());
    const auto stats_before = MeshProjectionService::position_update_stats();
    const auto projected =
        MeshProjectionService::update_polygon_document_mesh_positions(
            std::move(*exported), *initial);
    const auto canonical = MeshProjectionService::compile_sdk_document_mesh(after);
    ASSERT_TRUE(canonical.has_value());
    expect_canonical_projection(projected.geometry(), canonical->geometry());
    EXPECT_TRUE(projected.has_stable_cpu_topology);
    EXPECT_TRUE(projected.has_authored_polygon_document);
    ASSERT_TRUE(projected.authored_polygon_document);
    EXPECT_NE(projected.authored_polygon_document, initial->authored_polygon_document);
    EXPECT_EQ(projected.authored_polygon_document->vertices[4].position.z, 0.5f);
    EXPECT_EQ(projected.authored_polygon_document->faces.front().uvs.front().x,
              changed.faces.front().uvs.front().x);
    const auto stats_after = MeshProjectionService::position_update_stats();
    EXPECT_EQ(stats_after.full_compile_count - stats_before.full_compile_count,
              changed_texture ? 1U : 0U);
    EXPECT_EQ(stats_after.retained_partial_count - stats_before.retained_partial_count,
              changed_texture ? 0U : 1U);
  }
  const auto original_canonical = MeshProjectionService::compile_sdk_document_mesh(before);
  ASSERT_TRUE(original_canonical.has_value());
  expect_canonical_projection(initial->geometry(), original_canonical->geometry());
}

} // namespace
} // namespace quader

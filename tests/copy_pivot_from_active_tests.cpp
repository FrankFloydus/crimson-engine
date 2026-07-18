////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "api/quader_polygon_document_internal_access.hpp"
#include "editor/modeling/modeling_types.hpp"
#include "editor/pivot/pivot_tool_service.hpp"
#include "editor/session/scene_session.hpp"
#include "math/glm_bridge.hpp"
#include "quader/modeling/neutral/polygon/document_model.hpp"
#include "scene/scene_bounds.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr float kEpsilon = 0.002f;

/// Owns an isolated native editor session for copy-pivot behavior tests.
struct CopyPivotHarness {
  quader::SceneSession session;
  quader::QuaderEditorApi editor;

  CopyPivotHarness()
      : editor(quader::QuaderApiSessionInternalAccess::from_scene_session(
            session)) {}
};

quader_poly::Document asymmetric_document(float size_multiplier = 1.0f) {
  quader_poly::Document document;
  const quader_poly::ElementId origin =
      quader_poly::add_vertex(document, {});
  const quader_poly::ElementId x = quader_poly::add_vertex(
      document, {4.0f * size_multiplier, 0.0f, 0.0f});
  const quader_poly::ElementId y = quader_poly::add_vertex(
      document, {0.0f, 2.0f * size_multiplier, 0.0f});
  const quader_poly::ElementId z = quader_poly::add_vertex(
      document, {0.0f, 0.0f, 3.0f * size_multiplier});
  const std::array first_face{origin, x, y};
  const std::array second_face{origin, z, x};
  EXPECT_NE(quader_poly::add_face(document, first_face),
            quader_poly::kInvalidElementId);
  EXPECT_NE(quader_poly::add_face(document, second_face),
            quader_poly::kInvalidElementId);
  return document;
}

quader::QuaderModelingObjectRef
create_asymmetric_mesh(CopyPivotHarness &harness, std::string name,
                       float size_multiplier = 1.0f) {
  const quader::QuaderApiResult<quader::QuaderModelingObjectRef> created =
      harness.editor.modeling().create_mesh(
          quader::QuaderPolygonDocumentInternalAccess::from_backend(
              asymmetric_document(size_multiplier)),
          {.name = std::move(name)});
  EXPECT_TRUE(created.ok()) << created.error_message();
  return created.value();
}

quader::QuaderModelingObjectRef create_box(CopyPivotHarness &harness,
                                            std::string name) {
  const quader::QuaderApiResult<quader::QuaderModelingObjectRef> created =
      harness.editor.modeling().create_box(
          {.min = {-1.0f, -1.0f, -1.0f},
           .max = {1.0f, 1.0f, 1.0f}},
          std::move(name));
  EXPECT_TRUE(created.ok()) << created.error_message();
  return created.value();
}

quader::QuaderModelingObjectRef
create_box_with_bounds(CopyPivotHarness &harness, std::string name,
                       quader::QVec3 min, quader::QVec3 max) {
  const quader::QuaderApiResult<quader::QuaderModelingObjectRef> created =
      harness.editor.modeling().create_box(
          {.min = min, .max = max}, std::move(name));
  EXPECT_TRUE(created.ok()) << created.error_message();
  return created.value();
}

quader::EntityId create_plain_cpu_mesh(CopyPivotHarness &harness,
                                       std::string name) {
  quader::MeshAsset mesh;
  mesh.name = name + " Mesh";
  quader::MeshGeometryPayload &geometry = mesh.edit_geometry();
  geometry.vertices = {
      {.position = {}, .normal = {0.0f, 1.0f, 0.0f}},
      {.position = {4.0f, 0.0f, 0.0f}, .normal = {0.0f, 1.0f, 0.0f}},
      {.position = {0.0f, 2.0f, 0.0f}, .normal = {0.0f, 1.0f, 0.0f}},
      {.position = {0.0f, 0.0f, 3.0f}, .normal = {0.0f, 1.0f, 0.0f}},
  };
  geometry.indices = {0u, 1u, 2u, 0u, 3u, 1u};
  const quader::AssetId mesh_id =
      harness.session.assets().add_mesh(std::move(mesh));
  const quader::EntityId entity = harness.session.scene().create_entity(
      std::move(name), quader::EntityKind::Mesh);
  harness.session.scene().set_mesh(entity,
                                   quader::MeshComponent{.mesh = mesh_id});
  return entity;
}

void set_transform(CopyPivotHarness &harness, quader::EntityId entity,
                   quader::TransformComponent transform) {
  harness.session.scene().set_transform(entity, transform);
}

quader::QVec3 world_origin(const CopyPivotHarness &harness,
                           quader::EntityId entity) {
  return quader::transform_point(
      harness.session.scene().world_transform_matrix(entity), {});
}

void expect_vec3_near(quader::QVec3 actual, quader::QVec3 expected,
                      float epsilon = kEpsilon) {
  EXPECT_NEAR(actual.x, expected.x, epsilon);
  EXPECT_NEAR(actual.y, expected.y, epsilon);
  EXPECT_NEAR(actual.z, expected.z, epsilon);
}

void expect_matrix_near(const quader::QMat4 &actual,
                        const quader::QMat4 &expected) {
  for (std::size_t index = 0; index < actual.values.size(); ++index) {
    SCOPED_TRACE(index);
    EXPECT_NEAR(actual.values[index], expected.values[index], kEpsilon);
  }
}

void expect_transform_near(const quader::TransformComponent &actual,
                           const quader::TransformComponent &expected) {
  expect_vec3_near(actual.position, expected.position);
  expect_vec3_near(actual.rotation_euler, expected.rotation_euler);
  expect_vec3_near(actual.scale, expected.scale);
  EXPECT_EQ(actual.custom_pivot, expected.custom_pivot);
}

std::vector<quader::QVec3>
world_vertices(const CopyPivotHarness &harness, quader::EntityId entity) {
  const quader::MeshComponent *component =
      harness.session.scene().mesh(entity);
  EXPECT_NE(component, nullptr);
  if (component == nullptr) {
    return {};
  }
  const quader::MeshAsset *mesh =
      harness.session.assets().mesh(component->mesh);
  EXPECT_NE(mesh, nullptr);
  if (mesh == nullptr) {
    return {};
  }

  const quader::QMat4 world =
      harness.session.scene().world_transform_matrix(entity);
  std::vector<quader::QVec3> result;
  result.reserve(mesh->geometry().vertices.size());
  for (const quader::MeshVertex &vertex : mesh->geometry().vertices) {
    result.push_back(quader::transform_point(world, vertex.position));
  }
  return result;
}

std::vector<quader::QVec3>
authored_world_vertices(const CopyPivotHarness &harness,
                        quader::EntityId entity) {
  const quader::MeshComponent *component =
      harness.session.scene().mesh(entity);
  EXPECT_NE(component, nullptr);
  if (component == nullptr) {
    return {};
  }
  const quader::MeshAsset *mesh =
      harness.session.assets().mesh(component->mesh);
  EXPECT_NE(mesh, nullptr);
  if (mesh == nullptr || mesh->authored_polygon_document == nullptr) {
    return {};
  }

  const quader::QMat4 world =
      harness.session.scene().world_transform_matrix(entity);
  std::vector<quader::QVec3> result;
  result.reserve(mesh->authored_polygon_document->vertices.size());
  for (const quader_poly::Vertex &vertex :
       mesh->authored_polygon_document->vertices) {
    result.push_back(quader::transform_point(world, vertex.position));
  }
  return result;
}

void expect_vertices_near(const std::vector<quader::QVec3> &actual,
                          const std::vector<quader::QVec3> &expected) {
  ASSERT_EQ(actual.size(), expected.size());
  for (std::size_t index = 0; index < actual.size(); ++index) {
    SCOPED_TRACE(index);
    expect_vec3_near(actual[index], expected[index]);
  }
}

quader::SceneBounds bounds(const CopyPivotHarness &harness,
                           quader::EntityId entity) {
  const std::optional<quader::SceneBounds> result =
      quader::entity_world_bounds(harness.session.scene(),
                                  harness.session.assets(), entity);
  EXPECT_TRUE(result.has_value());
  return result.value_or(quader::SceneBounds{});
}

void expect_bounds_near(const quader::SceneBounds &actual,
                        const quader::SceneBounds &expected) {
  expect_vec3_near(actual.min, expected.min);
  expect_vec3_near(actual.max, expected.max);
}

quader::QVec3 local_mean(const quader::MeshAsset &mesh) {
  quader::QVec3 sum;
  std::size_t count = 0u;
  if (mesh.authored_polygon_document != nullptr &&
      !mesh.authored_polygon_document->vertices.empty()) {
    for (const quader_poly::Vertex &vertex :
         mesh.authored_polygon_document->vertices) {
      sum += vertex.position;
    }
    count = mesh.authored_polygon_document->vertices.size();
  } else {
    for (const quader::MeshVertex &vertex : mesh.geometry().vertices) {
      sum += vertex.position;
    }
    count = mesh.geometry().vertices.size();
  }
  EXPECT_GT(count, 0u);
  return count == 0u ? quader::QVec3{}
                     : sum / static_cast<float>(count);
}

quader::QVec3 hybrid_center(const CopyPivotHarness &harness,
                            quader::EntityId entity) {
  const quader::MeshComponent *component =
      harness.session.scene().mesh(entity);
  EXPECT_NE(component, nullptr);
  if (component == nullptr) {
    return {};
  }
  const quader::MeshAsset *mesh =
      harness.session.assets().mesh(component->mesh);
  EXPECT_NE(mesh, nullptr);
  if (mesh == nullptr) {
    return {};
  }
  const quader::QMat4 world =
      harness.session.scene().world_transform_matrix(entity);
  const quader::QVec3 mean_world =
      quader::transform_point(world, local_mean(*mesh));
  const std::optional<quader::SceneBounds> world_bounds =
      quader::world_bounds_from_local_mesh_bounds(
          world, quader::local_mesh_bounds(*mesh));
  EXPECT_TRUE(world_bounds.has_value());
  return world_bounds.has_value()
             ? (quader::bounds_center(*world_bounds) + mean_world) * 0.5f
             : quader::QVec3{};
}

quader::QMat4 rotation_only(const quader::TransformComponent &transform) {
  return quader::compose_transform(quader::QTransform{
      .rotation = quader::quat_from_euler_radians(transform.rotation_euler),
  });
}

quader::QVec3 expected_top_level_target_pivot(
    const CopyPivotHarness &harness, quader::EntityId source,
    quader::EntityId target) {
  const quader::TransformComponent &source_transform =
      *harness.session.scene().transform(source);
  const quader::TransformComponent &target_transform =
      *harness.session.scene().transform(target);
  const quader::QVec3 source_pivot =
      source_transform.custom_pivot
          ? world_origin(harness, source)
          : quader::bounds_center(bounds(harness, source));
  const quader::QVec3 source_offset =
      source_pivot - hybrid_center(harness, source);
  const quader::QVec3 source_local_offset = quader::transform_vector(
      quader::inverse(rotation_only(source_transform)), source_offset);
  return hybrid_center(harness, target) +
         quader::transform_vector(rotation_only(target_transform),
                                  source_local_offset);
}

quader::AssetId mesh_id(const CopyPivotHarness &harness,
                        quader::EntityId entity) {
  const quader::MeshComponent *mesh = harness.session.scene().mesh(entity);
  EXPECT_NE(mesh, nullptr);
  return mesh == nullptr ? quader::kInvalidAssetId : mesh->mesh;
}

const quader::QuaderActionPanel *
find_panel(const quader::QuaderActionSet &actions, std::string_view panel_id) {
  const auto found = std::find_if(
      actions.panels.begin(), actions.panels.end(),
      [&](const quader::QuaderActionPanel &panel) {
        return panel.panel_id == panel_id;
      });
  return found == actions.panels.end() ? nullptr : &*found;
}

const quader::QuaderActionItem *
find_action(const quader::QuaderActionPanel &panel,
            std::string_view action_id) {
  const auto found = std::find_if(
      panel.actions.begin(), panel.actions.end(),
      [&](const quader::QuaderActionItem &action) {
        return action.action_id == action_id;
      });
  return found == panel.actions.end() ? nullptr : &*found;
}

std::optional<quader::QuaderCommandDescriptor>
command_descriptor(const CopyPivotHarness &harness) {
  const std::vector<quader::QuaderCommandDescriptor> commands =
      harness.editor.commands().available();
  const auto found = std::find_if(
      commands.begin(), commands.end(),
      [](const quader::QuaderCommandDescriptor &command) {
        return command.id == quader::quader_command::kModelingCopyPivotFromActive
                                 .string();
      });
  return found == commands.end()
             ? std::nullopt
             : std::optional<quader::QuaderCommandDescriptor>{*found};
}

TEST(CopyPivotFromActiveTests,
     AppliedLowerLeftPivotTransfersToCongruentTargetLowerLeft) {
  CopyPivotHarness harness;
  const quader::QuaderModelingObjectRef source = create_box_with_bounds(
      harness, "Lower Left Source", {2.0f, 1.0f, -4.0f},
      {4.0f, 3.0f, -2.0f});
  const quader::QuaderModelingObjectRef target = create_box_with_bounds(
      harness, "Lower Left Target", {-8.0f, 5.0f, 1.0f},
      {-6.0f, 7.0f, 3.0f});

  ASSERT_TRUE(harness.editor.selection().select_object(source).ok());
  const std::optional<quader::QVec3> source_move_pivot =
      harness.editor.query().selection_transform_pivot();
  ASSERT_TRUE(source_move_pivot.has_value());
  expect_vec3_near(*source_move_pivot, {3.0f, 2.0f, -3.0f});
  ASSERT_TRUE(
      harness.editor.tools().activate(quader::ViewportToolId::Pivot).ok());
  expect_vec3_near(harness.session.pivot().handle_state().world_position,
                   *source_move_pivot);
  ASSERT_TRUE(harness.editor.tools()
                  .execute_action("pivot", "pivot.align.bottom_left_front")
                  .ok());
  ASSERT_TRUE(harness.editor.tools().commit_active_tool_preview().ok());

  const quader::SceneBounds source_bounds = bounds(harness, source.entity);
  expect_vec3_near(world_origin(harness, source.entity), source_bounds.min);
  ASSERT_TRUE(harness.session.scene().transform(source.entity)->custom_pivot);

  const quader::SceneBounds target_bounds_before =
      bounds(harness, target.entity);
  const std::vector<quader::QVec3> source_vertices_before =
      world_vertices(harness, source.entity);
  const std::vector<quader::QVec3> target_vertices_before =
      world_vertices(harness, target.entity);
  const std::vector<quader::QVec3> target_authored_vertices_before =
      authored_world_vertices(harness, target.entity);
  ASSERT_FALSE(target_authored_vertices_before.empty());
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({source.entity, target.entity}, source.entity)
                  .ok());
  harness.session.history().clear();

  const quader::QuaderApiVoidResult copied =
      harness.editor.modeling().copy_pivot_from_active();
  ASSERT_TRUE(copied.ok()) << copied.error_message();
  ASSERT_TRUE(copied.changed());
  EXPECT_TRUE(harness.session.scene().transform(target.entity)->custom_pivot);
  expect_vec3_near(world_origin(harness, target.entity),
                   target_bounds_before.min);
  expect_vertices_near(world_vertices(harness, source.entity),
                       source_vertices_before);
  expect_vertices_near(world_vertices(harness, target.entity),
                       target_vertices_before);
  expect_vertices_near(authored_world_vertices(harness, target.entity),
                       target_authored_vertices_before);
  expect_bounds_near(bounds(harness, target.entity), target_bounds_before);
  EXPECT_EQ(harness.session.scene().primary_selection(), source.entity);
  ASSERT_NE(harness.session.history().undo_label(), nullptr);
  EXPECT_EQ(*harness.session.history().undo_label(),
            "Copy Pivot From Active");

  ASSERT_TRUE(harness.editor.history().undo().ok());
  EXPECT_FALSE(harness.session.scene().transform(target.entity)->custom_pivot);
  expect_vertices_near(world_vertices(harness, target.entity),
                       target_vertices_before);
  expect_vertices_near(authored_world_vertices(harness, target.entity),
                       target_authored_vertices_before);
  expect_bounds_near(bounds(harness, target.entity), target_bounds_before);
  ASSERT_TRUE(harness.editor.history().redo().ok());
  expect_vec3_near(world_origin(harness, target.entity),
                   target_bounds_before.min);
  expect_vertices_near(world_vertices(harness, target.entity),
                       target_vertices_before);
  expect_vertices_near(authored_world_vertices(harness, target.entity),
                       target_authored_vertices_before);
}

TEST(CopyPivotFromActiveTests,
     DefaultSourceWithChildCopiesThePivotShownByTransformGizmo) {
  CopyPivotHarness harness;
  const quader::QuaderModelingObjectRef source =
      create_box(harness, "Hierarchy Pivot Source");
  const quader::QuaderModelingObjectRef source_child =
      create_box(harness, "Off-Center Source Child");
  const quader::QuaderModelingObjectRef target =
      create_box(harness, "Hierarchy Pivot Target");
  set_transform(harness, source_child.entity, {.position = {10.0f, 0.0f, 0.0f}});
  set_transform(harness, target.entity, {.position = {30.0f, 0.0f, 0.0f}});
  ASSERT_TRUE(harness.editor.scene()
                  .reparent(source_child.entity, source.entity, false)
                  .ok());

  ASSERT_TRUE(harness.editor.selection().select_object(source).ok());
  const std::optional<quader::QVec3> displayed_source_pivot =
      harness.editor.query().selection_transform_pivot();
  ASSERT_TRUE(displayed_source_pivot.has_value());
  expect_vec3_near(*displayed_source_pivot, {5.0f, 0.0f, 0.0f});
  expect_vec3_near(quader::bounds_center(bounds(harness, source.entity)), {});

  const quader::QMat4 source_child_world_before =
      harness.session.scene().world_transform_matrix(source_child.entity);
  const std::vector<quader::QVec3> source_vertices_before =
      world_vertices(harness, source.entity);
  const std::vector<quader::QVec3> target_vertices_before =
      world_vertices(harness, target.entity);
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({source.entity, target.entity}, source.entity)
                  .ok());
  harness.session.history().clear();

  const quader::QuaderApiVoidResult copied =
      harness.editor.modeling().copy_pivot_from_active();
  ASSERT_TRUE(copied.ok()) << copied.error_message();
  ASSERT_TRUE(copied.changed());
  expect_vec3_near(world_origin(harness, target.entity),
                   {35.0f, 0.0f, 0.0f});
  EXPECT_TRUE(harness.session.scene().transform(target.entity)->custom_pivot);
  expect_vertices_near(world_vertices(harness, source.entity),
                       source_vertices_before);
  expect_vertices_near(world_vertices(harness, target.entity),
                       target_vertices_before);
  expect_matrix_near(
      harness.session.scene().world_transform_matrix(source_child.entity),
      source_child_world_before);

  ASSERT_TRUE(harness.editor.history().undo().ok());
  EXPECT_FALSE(harness.session.scene().transform(target.entity)->custom_pivot);
  expect_vertices_near(world_vertices(harness, target.entity),
                       target_vertices_before);
  ASSERT_TRUE(harness.editor.history().redo().ok());
  expect_vec3_near(world_origin(harness, target.entity),
                   {35.0f, 0.0f, 0.0f});
}

TEST(CopyPivotFromActiveTests,
     DefaultTargetWithChildIsComparedAgainstItsDisplayedHierarchyPivot) {
  CopyPivotHarness harness;
  const quader::QuaderModelingObjectRef source =
      create_box(harness, "Centered Source");
  const quader::QuaderModelingObjectRef target =
      create_box(harness, "Child-Bearing Target");
  const quader::QuaderModelingObjectRef target_child =
      create_box(harness, "Off-Center Target Child");
  set_transform(harness, target.entity, {.position = {30.0f, 0.0f, 0.0f}});
  set_transform(harness, target_child.entity, {.position = {10.0f, 0.0f, 0.0f}});
  ASSERT_TRUE(harness.editor.scene()
                  .reparent(target_child.entity, target.entity, false)
                  .ok());

  ASSERT_TRUE(harness.editor.selection().select_object(target).ok());
  const std::optional<quader::QVec3> displayed_target_pivot =
      harness.editor.query().selection_transform_pivot();
  ASSERT_TRUE(displayed_target_pivot.has_value());
  expect_vec3_near(*displayed_target_pivot, {35.0f, 0.0f, 0.0f});
  expect_vec3_near(quader::bounds_center(bounds(harness, target.entity)),
                   {30.0f, 0.0f, 0.0f});

  const quader::QMat4 target_child_world_before =
      harness.session.scene().world_transform_matrix(target_child.entity);
  const std::vector<quader::QVec3> target_vertices_before =
      world_vertices(harness, target.entity);
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({source.entity, target.entity}, source.entity)
                  .ok());
  harness.session.history().clear();

  const quader::QuaderApiVoidResult copied =
      harness.editor.modeling().copy_pivot_from_active();
  ASSERT_TRUE(copied.ok()) << copied.error_message();
  ASSERT_TRUE(copied.changed());
  expect_vec3_near(world_origin(harness, target.entity),
                   {30.0f, 0.0f, 0.0f});
  EXPECT_TRUE(harness.session.scene().transform(target.entity)->custom_pivot);
  expect_vertices_near(world_vertices(harness, target.entity),
                       target_vertices_before);
  expect_matrix_near(
      harness.session.scene().world_transform_matrix(target_child.entity),
      target_child_world_before);
  ASSERT_NE(harness.session.history().undo_label(), nullptr);
  EXPECT_EQ(*harness.session.history().undo_label(),
            "Copy Pivot From Active");

  ASSERT_TRUE(harness.editor.history().undo().ok());
  EXPECT_FALSE(harness.session.scene().transform(target.entity)->custom_pivot);
  expect_matrix_near(
      harness.session.scene().world_transform_matrix(target_child.entity),
      target_child_world_before);
}

TEST(CopyPivotFromActiveTests,
     UsesAuthoredHybridCenterVisibleBoundsPivotAndRotationOnlyTransfer) {
  CopyPivotHarness harness;
  const quader::QuaderModelingObjectRef source =
      create_asymmetric_mesh(harness, "Asymmetric Source");
  const quader::QuaderModelingObjectRef target =
      create_asymmetric_mesh(harness, "Scaled Target", 1.8f);
  set_transform(harness, source.entity,
                {.position = {3.0f, -2.0f, 5.0f},
                 .rotation_euler = {0.3f, -0.45f, 0.2f},
                 .scale = {2.0f, 0.75f, 1.5f}});
  set_transform(harness, target.entity,
                {.position = {-7.0f, 4.0f, -3.0f},
                 .rotation_euler = {-0.25f, 0.6f, -0.35f},
                 .scale = {0.5f, 2.25f, 0.8f}});
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({source.entity, target.entity}, source.entity)
                  .ok());

  ASSERT_TRUE(harness.session.assets().update_mesh(
      mesh_id(harness, source.entity), [](quader::MeshAsset &mesh) {
        ASSERT_FALSE(mesh.geometry().vertices.empty());
        mesh.edit_geometry().vertices.front().position = {1.0f, 1.0f, 1.0f};
      }));

  const quader::MeshAsset *source_mesh =
      harness.session.assets().mesh(mesh_id(harness, source.entity));
  ASSERT_NE(source_mesh, nullptr);
  ASSERT_NE(source_mesh->authored_polygon_document, nullptr);
  ASSERT_EQ(source_mesh->authored_polygon_document->vertices.size(), 4u);
  ASSERT_FALSE(harness.session.scene().transform(source.entity)->custom_pivot);
  quader::QVec3 cpu_sum;
  for (const quader::MeshVertex &vertex : source_mesh->geometry().vertices) {
    cpu_sum += vertex.position;
  }
  const quader::QVec3 cpu_mean =
      cpu_sum / static_cast<float>(source_mesh->geometry().vertices.size());
  EXPECT_GT(quader::length(cpu_mean - local_mean(*source_mesh)), 0.1f);

  const quader::QVec3 source_visible_pivot =
      quader::bounds_center(bounds(harness, source.entity));
  EXPECT_GT(quader::length(source_visible_pivot -
                           world_origin(harness, source.entity)),
            0.1f);
  const quader::QVec3 target_hybrid = hybrid_center(harness, target.entity);
  const quader::QVec3 source_offset =
      source_visible_pivot - hybrid_center(harness, source.entity);
  const quader::QVec3 expected =
      expected_top_level_target_pivot(harness, source.entity, target.entity);
  EXPECT_NEAR(quader::length(expected - target_hybrid),
              quader::length(source_offset), kEpsilon);
  EXPECT_GT(quader::length(expected - world_origin(harness, target.entity)),
            0.1f);

  const quader::TransformComponent source_before =
      *harness.session.scene().transform(source.entity);
  const quader::TransformComponent target_before =
      *harness.session.scene().transform(target.entity);
  const std::vector<quader::QVec3> source_vertices_before =
      world_vertices(harness, source.entity);
  const std::vector<quader::QVec3> target_vertices_before =
      world_vertices(harness, target.entity);
  const quader::SceneBounds target_bounds_before =
      bounds(harness, target.entity);
  const std::vector<quader::EntityId> selected_before =
      harness.session.scene().selected_entities();
  harness.session.history().clear();

  const quader::QuaderApiVoidResult copied =
      harness.editor.modeling().copy_pivot_from_active();
  ASSERT_TRUE(copied.ok()) << copied.error_message();
  ASSERT_TRUE(copied.changed());
  expect_vec3_near(world_origin(harness, target.entity), expected);
  EXPECT_TRUE(harness.session.scene().transform(target.entity)->custom_pivot);
  expect_transform_near(*harness.session.scene().transform(source.entity),
                        source_before);
  expect_vertices_near(world_vertices(harness, source.entity),
                       source_vertices_before);
  expect_vertices_near(world_vertices(harness, target.entity),
                       target_vertices_before);
  expect_bounds_near(bounds(harness, target.entity), target_bounds_before);
  EXPECT_EQ(harness.session.scene().selected_entities(), selected_before);
  EXPECT_EQ(harness.session.scene().primary_selection(), source.entity);
  ASSERT_NE(harness.session.history().undo_label(), nullptr);
  EXPECT_EQ(*harness.session.history().undo_label(),
            "Copy Pivot From Active");

  const quader::QuaderApiVoidResult unchanged =
      harness.editor.modeling().copy_pivot_from_active();
  ASSERT_TRUE(unchanged.ok()) << unchanged.error_message();
  EXPECT_FALSE(unchanged.changed());

  ASSERT_TRUE(harness.editor.history().undo().ok());
  expect_transform_near(*harness.session.scene().transform(target.entity),
                        target_before);
  expect_vertices_near(world_vertices(harness, target.entity),
                       target_vertices_before);
  EXPECT_FALSE(harness.editor.history().can_undo());
  ASSERT_TRUE(harness.editor.history().redo().ok());
  expect_vec3_near(world_origin(harness, target.entity), expected);
  expect_vertices_near(world_vertices(harness, target.entity),
                       target_vertices_before);
}

TEST(CopyPivotFromActiveTests,
     CopiesCustomSourcePivotToMultipleTargetsAndIgnoresNonMeshSelection) {
  CopyPivotHarness harness;
  const quader::QuaderModelingObjectRef source =
      create_asymmetric_mesh(harness, "Custom Pivot Source");
  const quader::QuaderModelingObjectRef first =
      create_asymmetric_mesh(harness, "First Target", 0.75f);
  const quader::QuaderModelingObjectRef second =
      create_asymmetric_mesh(harness, "Second Target", 1.4f);
  const quader::QuaderApiResult<quader::QuaderEntityRef> empty =
      harness.editor.scene().create_entity(
          {.name = "Ignored Empty", .kind = quader::QuaderEntityKind::Empty});
  ASSERT_TRUE(empty.ok()) << empty.error_message();

  set_transform(harness, source.entity,
                {.position = {6.0f, 2.0f, -4.0f},
                 .rotation_euler = {0.15f, 0.4f, -0.25f},
                 .scale = {1.75f, 0.6f, 1.1f},
                 .custom_pivot = true});
  set_transform(harness, first.entity,
                {.position = {-2.0f, -5.0f, 3.0f},
                 .rotation_euler = {-0.2f, 0.1f, 0.55f},
                 .scale = {0.9f, 1.8f, 0.7f}});
  set_transform(harness, second.entity,
                {.position = {9.0f, -1.0f, 2.0f},
                 .rotation_euler = {0.35f, -0.5f, 0.1f},
                 .scale = {2.2f, 0.8f, 1.3f},
                 .custom_pivot = true});
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({source.entity, first.entity, second.entity,
                                   empty.value().id},
                                  source.entity)
                  .ok());
  ASSERT_TRUE(harness.session.scene().set_locked(source.entity, true));

  const quader::QVec3 first_expected = expected_top_level_target_pivot(
      harness, source.entity, first.entity);
  const quader::QVec3 second_expected = expected_top_level_target_pivot(
      harness, source.entity, second.entity);
  const quader::QVec3 first_origin_before = world_origin(harness, first.entity);
  const quader::QVec3 second_origin_before =
      world_origin(harness, second.entity);
  const std::vector<quader::QVec3> first_vertices_before =
      world_vertices(harness, first.entity);
  const std::vector<quader::QVec3> second_vertices_before =
      world_vertices(harness, second.entity);
  const std::vector<quader::EntityId> selected_before =
      harness.session.scene().selected_entities();
  harness.session.history().clear();

  ASSERT_TRUE(harness.editor.modeling().can_copy_pivot_from_active());
  const quader::QuaderApiVoidResult copied = harness.editor.commands().execute(
      quader::quader_command::kModelingCopyPivotFromActive);
  ASSERT_TRUE(copied.ok()) << copied.error_message();
  ASSERT_TRUE(copied.changed());
  expect_vec3_near(world_origin(harness, first.entity), first_expected);
  expect_vec3_near(world_origin(harness, second.entity), second_expected);
  expect_vertices_near(world_vertices(harness, first.entity),
                       first_vertices_before);
  expect_vertices_near(world_vertices(harness, second.entity),
                       second_vertices_before);
  EXPECT_EQ(harness.session.scene().selected_entities(), selected_before);
  EXPECT_EQ(harness.session.scene().primary_selection(), source.entity);
  EXPECT_TRUE(harness.session.scene().is_effectively_locked(source.entity));
  ASSERT_NE(harness.session.history().undo_label(), nullptr);
  EXPECT_EQ(*harness.session.history().undo_label(),
            "Copy Pivot From Active");

  ASSERT_TRUE(harness.editor.history().undo().ok());
  expect_vec3_near(world_origin(harness, first.entity), first_origin_before);
  expect_vec3_near(world_origin(harness, second.entity), second_origin_before);
  EXPECT_EQ(harness.session.scene().selected_entities(), selected_before);
  ASSERT_TRUE(harness.editor.history().redo().ok());
  expect_vec3_near(world_origin(harness, first.entity), first_expected);
  expect_vec3_near(world_origin(harness, second.entity), second_expected);
  EXPECT_EQ(harness.session.scene().selected_entities(), selected_before);
}

TEST(CopyPivotFromActiveTests,
     PlainSceneMeshSourceFallsBackToStableCpuVertices) {
  CopyPivotHarness harness;
  const quader::EntityId source =
      create_plain_cpu_mesh(harness, "Plain CPU Source");
  const quader::QuaderModelingObjectRef target =
      create_asymmetric_mesh(harness, "Authored Target", 1.25f);
  set_transform(harness, source,
                {.position = {4.0f, -3.0f, 2.0f},
                 .rotation_euler = {0.25f, -0.35f, 0.15f},
                 .scale = {1.5f, 0.7f, 1.2f}});
  set_transform(harness, target.entity,
                {.position = {-8.0f, 5.0f, -1.0f},
                 .rotation_euler = {-0.2f, 0.45f, -0.1f},
                 .scale = {0.8f, 1.6f, 0.9f}});
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({source, target.entity}, source)
                  .ok());

  const quader::MeshAsset *source_mesh =
      harness.session.assets().mesh(mesh_id(harness, source));
  ASSERT_NE(source_mesh, nullptr);
  EXPECT_EQ(source_mesh->authored_polygon_document, nullptr);
  EXPECT_TRUE(source_mesh->has_stable_cpu_topology);
  const quader::QVec3 expected =
      expected_top_level_target_pivot(harness, source, target.entity);
  const std::vector<quader::QVec3> target_vertices_before =
      world_vertices(harness, target.entity);
  harness.session.history().clear();

  ASSERT_TRUE(harness.editor.modeling().can_copy_pivot_from_active());
  const quader::QuaderApiVoidResult copied =
      harness.editor.modeling().copy_pivot_from_active();
  ASSERT_TRUE(copied.ok()) << copied.error_message();
  ASSERT_TRUE(copied.changed());
  expect_vec3_near(world_origin(harness, target.entity), expected);
  expect_vertices_near(world_vertices(harness, target.entity),
                       target_vertices_before);
  EXPECT_EQ(harness.session.scene().primary_selection(), source);
}

TEST(CopyPivotFromActiveTests,
     MatchingNonCustomVisibleBoundsPivotIsUnchangedWithoutHistory) {
  CopyPivotHarness harness;
  const quader::QuaderModelingObjectRef source =
      create_asymmetric_mesh(harness, "Matching Source");
  const quader::QuaderModelingObjectRef target =
      create_asymmetric_mesh(harness, "Matching Target");
  const quader::QVec3 shared_rotation{0.2f, -0.35f, 0.45f};
  const quader::QVec3 shared_scale{1.6f, 0.75f, 1.25f};
  set_transform(harness, source.entity,
                {.position = {3.0f, -4.0f, 2.0f},
                 .rotation_euler = shared_rotation,
                 .scale = shared_scale});
  set_transform(harness, target.entity,
                {.position = {-8.0f, 5.0f, -3.0f},
                 .rotation_euler = shared_rotation,
                 .scale = shared_scale});
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({source.entity, target.entity}, source.entity)
                  .ok());

  ASSERT_FALSE(harness.session.scene().transform(target.entity)->custom_pivot);
  const quader::QVec3 expected =
      expected_top_level_target_pivot(harness, source.entity, target.entity);
  const quader::QVec3 effective_target_pivot =
      quader::bounds_center(bounds(harness, target.entity));
  expect_vec3_near(expected, effective_target_pivot);
  EXPECT_GT(quader::length(effective_target_pivot -
                           world_origin(harness, target.entity)),
            0.1f);

  const quader::TransformComponent target_before =
      *harness.session.scene().transform(target.entity);
  const quader::AssetId target_mesh_before =
      mesh_id(harness, target.entity);
  const std::vector<quader::QVec3> target_vertices_before =
      world_vertices(harness, target.entity);
  const std::size_t mesh_count_before = harness.session.assets().mesh_count();
  harness.session.history().clear();

  ASSERT_TRUE(harness.editor.modeling().can_copy_pivot_from_active());
  const quader::QuaderApiVoidResult copied =
      harness.editor.modeling().copy_pivot_from_active();
  ASSERT_TRUE(copied.ok()) << copied.error_message();
  EXPECT_FALSE(copied.changed());
  EXPECT_FALSE(harness.editor.history().can_undo());
  EXPECT_FALSE(harness.editor.history().can_redo());
  expect_transform_near(*harness.session.scene().transform(target.entity),
                        target_before);
  EXPECT_FALSE(harness.session.scene().transform(target.entity)->custom_pivot);
  EXPECT_EQ(mesh_id(harness, target.entity), target_mesh_before);
  EXPECT_EQ(harness.session.assets().mesh_count(), mesh_count_before);
  expect_vertices_near(world_vertices(harness, target.entity),
                       target_vertices_before);
}

TEST(CopyPivotFromActiveTests,
     MixedUnchangedAndChangedTargetsOnlyRebaseChangedTarget) {
  CopyPivotHarness harness;
  const quader::QuaderModelingObjectRef source =
      create_asymmetric_mesh(harness, "Mixed Source");
  const quader::QuaderModelingObjectRef unchanged =
      create_asymmetric_mesh(harness, "Mixed Unchanged Target");
  const quader::QuaderModelingObjectRef changed =
      create_asymmetric_mesh(harness, "Mixed Changed Target", 1.4f);
  const quader::QVec3 shared_rotation{0.2f, -0.35f, 0.45f};
  const quader::QVec3 shared_scale{1.6f, 0.75f, 1.25f};
  set_transform(harness, source.entity,
                {.position = {3.0f, -4.0f, 2.0f},
                 .rotation_euler = shared_rotation,
                 .scale = shared_scale});
  set_transform(harness, unchanged.entity,
                {.position = {-8.0f, 5.0f, -3.0f},
                 .rotation_euler = shared_rotation,
                 .scale = shared_scale});
  set_transform(harness, changed.entity,
                {.position = {9.0f, 2.0f, -6.0f},
                 .rotation_euler = {-0.3f, 0.1f, -0.25f},
                 .scale = {0.9f, 1.3f, 0.8f}});
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects(
                      {source.entity, unchanged.entity, changed.entity},
                      source.entity)
                  .ok());

  const quader::TransformComponent unchanged_before =
      *harness.session.scene().transform(unchanged.entity);
  const quader::AssetId unchanged_mesh_before =
      mesh_id(harness, unchanged.entity);
  const std::vector<quader::QVec3> unchanged_vertices_before =
      world_vertices(harness, unchanged.entity);
  const quader::QVec3 expected_changed = expected_top_level_target_pivot(
      harness, source.entity, changed.entity);
  const quader::QVec3 changed_origin_before =
      world_origin(harness, changed.entity);
  harness.session.history().clear();

  const quader::QuaderApiVoidResult copied =
      harness.editor.modeling().copy_pivot_from_active();
  ASSERT_TRUE(copied.ok()) << copied.error_message();
  ASSERT_TRUE(copied.changed());
  expect_transform_near(*harness.session.scene().transform(unchanged.entity),
                        unchanged_before);
  EXPECT_FALSE(
      harness.session.scene().transform(unchanged.entity)->custom_pivot);
  EXPECT_EQ(mesh_id(harness, unchanged.entity), unchanged_mesh_before);
  expect_vertices_near(world_vertices(harness, unchanged.entity),
                       unchanged_vertices_before);
  expect_vec3_near(world_origin(harness, changed.entity), expected_changed);
  EXPECT_TRUE(harness.session.scene().transform(changed.entity)->custom_pivot);
  ASSERT_NE(harness.session.history().undo_label(), nullptr);
  EXPECT_EQ(*harness.session.history().undo_label(),
            "Copy Pivot From Active");

  ASSERT_TRUE(harness.editor.history().undo().ok());
  expect_vec3_near(world_origin(harness, changed.entity),
                   changed_origin_before);
  expect_transform_near(*harness.session.scene().transform(unchanged.entity),
                        unchanged_before);
  EXPECT_FALSE(harness.editor.history().can_undo());
}

TEST(CopyPivotFromActiveTests,
     MinimumNonuniformScalesStillUseTheRotationOnlyBasis) {
  CopyPivotHarness harness;
  const quader::QuaderModelingObjectRef source =
      create_asymmetric_mesh(harness, "Minimum Scale Source");
  const quader::QuaderModelingObjectRef target =
      create_asymmetric_mesh(harness, "Minimum Scale Target", 1.25f);
  set_transform(harness, source.entity,
                {.position = {3.0f, -4.0f, 2.0f},
                 .rotation_euler = {0.2f, -0.35f, 0.45f},
                 .scale = {0.001f, 0.0015f, 0.002f},
                 .custom_pivot = true});
  set_transform(harness, target.entity,
                {.position = {-8.0f, 5.0f, -3.0f},
                 .rotation_euler = {-0.3f, 0.1f, -0.25f},
                 .scale = {0.002f, 0.001f, 0.003f}});
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({source.entity, target.entity},
                                  source.entity)
                  .ok());

  const quader::QVec3 expected =
      expected_top_level_target_pivot(harness, source.entity, target.entity);
  const quader::QVec3 origin_before = world_origin(harness, target.entity);
  const std::vector<quader::QVec3> vertices_before =
      world_vertices(harness, target.entity);
  harness.session.history().clear();

  ASSERT_TRUE(harness.editor.modeling().can_copy_pivot_from_active());
  const quader::QuaderApiVoidResult copied =
      harness.editor.modeling().copy_pivot_from_active();
  ASSERT_TRUE(copied.ok()) << copied.error_message();
  ASSERT_TRUE(copied.changed());
  EXPECT_GT(quader::length(expected - origin_before), 0.0001f);
  expect_vec3_near(world_origin(harness, target.entity), expected, 0.00001f);
  expect_vertices_near(world_vertices(harness, target.entity),
                       vertices_before);
}

TEST(CopyPivotFromActiveTests,
     SharedTargetMeshUsesStableCloneOnWriteAcrossUndoAndRedo) {
  CopyPivotHarness harness;
  const quader::QuaderModelingObjectRef source =
      create_asymmetric_mesh(harness, "Clone Source");
  const quader::QuaderModelingObjectRef target =
      create_box(harness, "Clone Target");
  const quader::QuaderModelingObjectRef unselected_sharer =
      create_box(harness, "Unselected Sharer");
  set_transform(harness, source.entity,
                {.position = {5.0f, 3.0f, -2.0f},
                 .rotation_euler = {0.2f, 0.3f, -0.4f},
                 .scale = {1.2f, 0.8f, 1.5f},
                 .custom_pivot = true});
  set_transform(harness, target.entity,
                {.position = {-4.0f, 2.0f, 1.0f},
                 .rotation_euler = {-0.1f, 0.25f, 0.2f},
                 .scale = {0.75f, 1.4f, 0.9f}});
  set_transform(harness, unselected_sharer.entity,
                {.position = {11.0f, -6.0f, 4.0f}});

  const quader::AssetId shared_mesh = mesh_id(harness, target.entity);
  quader::MeshComponent shared_component =
      *harness.session.scene().mesh(unselected_sharer.entity);
  shared_component.mesh = shared_mesh;
  harness.session.scene().set_mesh(unselected_sharer.entity, shared_component);
  ASSERT_EQ(mesh_id(harness, unselected_sharer.entity), shared_mesh);
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({source.entity, target.entity}, source.entity)
                  .ok());

  const std::size_t mesh_count_before = harness.session.assets().mesh_count();
  const quader::QVec3 target_origin_before =
      world_origin(harness, target.entity);
  const std::vector<quader::QVec3> target_vertices_before =
      world_vertices(harness, target.entity);
  const std::vector<quader::QVec3> sharer_vertices_before =
      world_vertices(harness, unselected_sharer.entity);
  harness.session.history().clear();

  const quader::QuaderApiVoidResult copied =
      harness.editor.modeling().copy_pivot_from_active();
  ASSERT_TRUE(copied.ok()) << copied.error_message();
  ASSERT_TRUE(copied.changed());
  const quader::AssetId cloned_mesh = mesh_id(harness, target.entity);
  EXPECT_NE(cloned_mesh, shared_mesh);
  EXPECT_EQ(mesh_id(harness, unselected_sharer.entity), shared_mesh);
  EXPECT_EQ(harness.session.assets().mesh_count(), mesh_count_before + 1u);
  expect_vertices_near(world_vertices(harness, target.entity),
                       target_vertices_before);
  expect_vertices_near(world_vertices(harness, unselected_sharer.entity),
                       sharer_vertices_before);

  ASSERT_TRUE(harness.editor.history().undo().ok());
  EXPECT_EQ(mesh_id(harness, target.entity), shared_mesh);
  EXPECT_EQ(mesh_id(harness, unselected_sharer.entity), shared_mesh);
  EXPECT_EQ(harness.session.assets().mesh_count(), mesh_count_before);
  expect_vec3_near(world_origin(harness, target.entity), target_origin_before);
  expect_vertices_near(world_vertices(harness, target.entity),
                       target_vertices_before);
  expect_vertices_near(world_vertices(harness, unselected_sharer.entity),
                       sharer_vertices_before);

  ASSERT_TRUE(harness.editor.history().redo().ok());
  EXPECT_EQ(mesh_id(harness, target.entity), cloned_mesh);
  EXPECT_EQ(mesh_id(harness, unselected_sharer.entity), shared_mesh);
  EXPECT_EQ(harness.session.assets().mesh_count(), mesh_count_before + 1u);
  expect_vertices_near(world_vertices(harness, target.entity),
                       target_vertices_before);
  expect_vertices_near(world_vertices(harness, unselected_sharer.entity),
                       sharer_vertices_before);
}

TEST(CopyPivotFromActiveTests,
     ParentAndChildTargetsPreserveWorldGeometryThroughOneUndoStep) {
  CopyPivotHarness harness;
  const quader::QuaderModelingObjectRef source =
      create_asymmetric_mesh(harness, "Hierarchy Source");
  const quader::QuaderModelingObjectRef child =
      create_asymmetric_mesh(harness, "Earlier Child", 0.6f);
  const quader::QuaderModelingObjectRef parent =
      create_asymmetric_mesh(harness, "Later Parent", 1.3f);
  const quader::QuaderApiResult<quader::QuaderEntityRef> nonselected_child =
      harness.editor.scene().create_entity({
          .name = "Nonselected Direct Child",
          .kind = quader::QuaderEntityKind::Empty,
          .transform = {.position = {-2.0f, 7.0f, -5.0f},
                        .rotation_euler = {0.3f, -0.1f, 0.2f},
                        .scale = {0.7f, 1.4f, 0.9f}},
      });
  ASSERT_TRUE(nonselected_child.ok()) << nonselected_child.error_message();
  ASSERT_LT(child.entity, parent.entity);
  set_transform(harness, source.entity,
                {.position = {8.0f, 1.0f, -3.0f},
                 .custom_pivot = true});
  set_transform(harness, child.entity,
                {.position = {-5.0f, 6.0f, 2.0f},
                 .rotation_euler = {-0.2f, 0.15f, 0.1f},
                 .scale = {0.8f, 1.25f, 0.95f}});
  set_transform(harness, parent.entity,
                {.position = {3.0f, -4.0f, 1.0f},
                 .rotation_euler = {0.1f, -0.25f, 0.2f},
                 .scale = {1.15f, 0.9f, 1.1f}});
  ASSERT_TRUE(harness.editor.scene()
                  .reparent(child.entity, parent.entity, true)
                  .ok());
  ASSERT_TRUE(harness.editor.scene()
                  .reparent(nonselected_child.value().id, parent.entity, true)
                  .ok());
  ASSERT_EQ(harness.session.scene().parent(child.entity), parent.entity);
  ASSERT_EQ(harness.session.scene().parent(nonselected_child.value().id),
            parent.entity);

  const quader::AssetId shared_mesh = mesh_id(harness, parent.entity);
  quader::MeshComponent child_component =
      *harness.session.scene().mesh(child.entity);
  child_component.mesh = shared_mesh;
  harness.session.scene().set_mesh(child.entity, child_component);
  ASSERT_EQ(mesh_id(harness, child.entity), shared_mesh);
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({source.entity, child.entity, parent.entity},
                                  source.entity)
                  .ok());

  const quader::QVec3 child_origin_before =
      world_origin(harness, child.entity);
  const quader::QVec3 parent_origin_before =
      world_origin(harness, parent.entity);
  const std::vector<quader::QVec3> child_vertices_before =
      world_vertices(harness, child.entity);
  const std::vector<quader::QVec3> parent_vertices_before =
      world_vertices(harness, parent.entity);
  const quader::QMat4 nonselected_child_world_before =
      harness.session.scene().world_transform_matrix(
          nonselected_child.value().id);
  const std::size_t mesh_count_before = harness.session.assets().mesh_count();
  const std::vector<quader::EntityId> selected_before =
      harness.session.scene().selected_entities();
  EXPECT_EQ(std::find(selected_before.begin(), selected_before.end(),
                      nonselected_child.value().id),
            selected_before.end());
  harness.session.history().clear();

  const quader::QuaderApiVoidResult copied =
      harness.editor.modeling().copy_pivot_from_active();
  ASSERT_TRUE(copied.ok()) << copied.error_message();
  ASSERT_TRUE(copied.changed());
  EXPECT_GT(quader::length(world_origin(harness, child.entity) -
                           child_origin_before),
            0.1f);
  EXPECT_GT(quader::length(world_origin(harness, parent.entity) -
                           parent_origin_before),
            0.1f);
  EXPECT_TRUE(harness.session.scene().transform(child.entity)->custom_pivot);
  EXPECT_TRUE(harness.session.scene().transform(parent.entity)->custom_pivot);
  const quader::AssetId cloned_parent_mesh =
      mesh_id(harness, parent.entity);
  EXPECT_NE(cloned_parent_mesh, shared_mesh);
  EXPECT_EQ(mesh_id(harness, child.entity), shared_mesh);
  EXPECT_EQ(harness.session.assets().mesh_count(), mesh_count_before + 1u);
  expect_vertices_near(world_vertices(harness, child.entity),
                       child_vertices_before);
  expect_vertices_near(world_vertices(harness, parent.entity),
                       parent_vertices_before);
  expect_matrix_near(harness.session.scene().world_transform_matrix(
                         nonselected_child.value().id),
                     nonselected_child_world_before);
  EXPECT_EQ(harness.session.scene().selected_entities(), selected_before);

  ASSERT_TRUE(harness.editor.history().undo().ok());
  expect_vec3_near(world_origin(harness, child.entity), child_origin_before);
  expect_vec3_near(world_origin(harness, parent.entity), parent_origin_before);
  expect_vertices_near(world_vertices(harness, child.entity),
                       child_vertices_before);
  expect_vertices_near(world_vertices(harness, parent.entity),
                       parent_vertices_before);
  expect_matrix_near(harness.session.scene().world_transform_matrix(
                         nonselected_child.value().id),
                     nonselected_child_world_before);
  EXPECT_EQ(mesh_id(harness, parent.entity), shared_mesh);
  EXPECT_EQ(mesh_id(harness, child.entity), shared_mesh);
  EXPECT_EQ(harness.session.assets().mesh_count(), mesh_count_before);
  EXPECT_EQ(harness.session.scene().parent(child.entity), parent.entity);
  EXPECT_FALSE(harness.editor.history().can_undo());

  ASSERT_TRUE(harness.editor.history().redo().ok());
  EXPECT_EQ(mesh_id(harness, parent.entity), cloned_parent_mesh);
  EXPECT_EQ(mesh_id(harness, child.entity), shared_mesh);
  EXPECT_EQ(harness.session.assets().mesh_count(), mesh_count_before + 1u);
  expect_vertices_near(world_vertices(harness, child.entity),
                       child_vertices_before);
  expect_vertices_near(world_vertices(harness, parent.entity),
                       parent_vertices_before);
  expect_matrix_near(harness.session.scene().world_transform_matrix(
                         nonselected_child.value().id),
                     nonselected_child_world_before);
  EXPECT_EQ(harness.session.scene().selected_entities(), selected_before);
}

TEST(CopyPivotFromActiveTests,
     MirroredNonuniformParentUsesBlenderWorldRotationAndPreservesDescendants) {
  CopyPivotHarness harness;
  const quader::QuaderModelingObjectRef source =
      create_asymmetric_mesh(harness, "Mirrored Hierarchy Source");
  const quader::QuaderModelingObjectRef target =
      create_asymmetric_mesh(harness, "Mirrored Hierarchy Target", 0.9f);
  const quader::QuaderApiResult<quader::QuaderEntityRef> parent =
      harness.editor.scene().create_entity({
          .name = "Mirrored Nonuniform Parent",
          .kind = quader::QuaderEntityKind::Empty,
          .transform = {.position = {-6.0f, 2.0f, 5.0f},
                        .rotation_euler = {0.4f, -0.2f, 0.3f},
                        .scale = {-1.5f, 0.65f, 2.2f}},
      });
  const quader::QuaderApiResult<quader::QuaderEntityRef> direct_child =
      harness.editor.scene().create_entity({
          .name = "Preserved Direct Child",
          .kind = quader::QuaderEntityKind::Empty,
          .transform = {.position = {1.1f, -0.7f, 0.4f},
                        .rotation_euler = {0.2f, 0.1f, -0.3f},
                        .scale = {0.8f, 1.2f, 0.6f}},
      });
  const quader::QuaderApiResult<quader::QuaderEntityRef> grandchild =
      harness.editor.scene().create_entity({
          .name = "Preserved Grandchild",
          .kind = quader::QuaderEntityKind::Empty,
          .transform = {.position = {-0.5f, 1.4f, 2.0f},
                        .rotation_euler = {-0.15f, 0.25f, 0.45f},
                        .scale = {1.1f, 0.7f, 1.3f}},
      });
  ASSERT_TRUE(parent.ok()) << parent.error_message();
  ASSERT_TRUE(direct_child.ok()) << direct_child.error_message();
  ASSERT_TRUE(grandchild.ok()) << grandchild.error_message();

  set_transform(harness, source.entity,
                {.position = {4.0f, -3.0f, 2.0f},
                 .rotation_euler = {0.25f, -0.4f, 0.35f},
                 .scale = {1.2f, 0.7f, 1.5f},
                 .custom_pivot = true});
  set_transform(harness, target.entity,
                {.position = {2.5f, -1.0f, 3.0f},
                 .rotation_euler = {-0.35f, 0.5f, -0.15f},
                 .scale = {0.8f, 1.3f, 0.55f}});
  ASSERT_TRUE(
      harness.editor.scene().reparent(target.entity, parent.value().id, false)
          .ok());
  ASSERT_TRUE(harness.editor.scene()
                  .reparent(direct_child.value().id, target.entity, false)
                  .ok());
  ASSERT_TRUE(harness.editor.scene()
                  .reparent(grandchild.value().id, direct_child.value().id,
                            false)
                  .ok());
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({source.entity, target.entity}, source.entity)
                  .ok());

  // Oracle generated by Blender 4.2 from the exact TRS hierarchy above using
  // matrix_world.decompose().rotation and the add-on's dual-center formula.
  constexpr quader::QVec3 kBlenderExpectedPivot{
      -14.002666473f, -1.897130013f, 9.366803169f};
  const quader::QMat4 parent_world_before =
      harness.session.scene().world_transform_matrix(parent.value().id);
  const quader::QMat4 direct_child_world_before =
      harness.session.scene().world_transform_matrix(direct_child.value().id);
  const quader::QMat4 grandchild_world_before =
      harness.session.scene().world_transform_matrix(grandchild.value().id);
  const std::vector<quader::QVec3> source_vertices_before =
      world_vertices(harness, source.entity);
  const std::vector<quader::QVec3> target_vertices_before =
      world_vertices(harness, target.entity);
  const quader::SceneBounds target_bounds_before =
      bounds(harness, target.entity);
  const std::vector<quader::EntityId> selection_before =
      harness.session.scene().selected_entities();
  harness.session.history().clear();

  const quader::QuaderApiVoidResult copied =
      harness.editor.modeling().copy_pivot_from_active();
  ASSERT_TRUE(copied.ok()) << copied.error_message();
  ASSERT_TRUE(copied.changed());
  expect_vec3_near(world_origin(harness, target.entity),
                   kBlenderExpectedPivot);
  EXPECT_TRUE(harness.session.scene().transform(target.entity)->custom_pivot);
  expect_vertices_near(world_vertices(harness, source.entity),
                       source_vertices_before);
  expect_vertices_near(world_vertices(harness, target.entity),
                       target_vertices_before);
  expect_bounds_near(bounds(harness, target.entity), target_bounds_before);
  expect_matrix_near(
      harness.session.scene().world_transform_matrix(parent.value().id),
      parent_world_before);
  expect_matrix_near(harness.session.scene().world_transform_matrix(
                         direct_child.value().id),
                     direct_child_world_before);
  expect_matrix_near(harness.session.scene().world_transform_matrix(
                         grandchild.value().id),
                     grandchild_world_before);
  EXPECT_EQ(harness.session.scene().selected_entities(), selection_before);

  ASSERT_TRUE(harness.editor.history().undo().ok());
  expect_vertices_near(world_vertices(harness, target.entity),
                       target_vertices_before);
  expect_bounds_near(bounds(harness, target.entity), target_bounds_before);
  expect_matrix_near(harness.session.scene().world_transform_matrix(
                         direct_child.value().id),
                     direct_child_world_before);
  expect_matrix_near(harness.session.scene().world_transform_matrix(
                         grandchild.value().id),
                     grandchild_world_before);

  ASSERT_TRUE(harness.editor.history().redo().ok());
  expect_vec3_near(world_origin(harness, target.entity),
                   kBlenderExpectedPivot);
  expect_vertices_near(world_vertices(harness, target.entity),
                       target_vertices_before);
  expect_matrix_near(harness.session.scene().world_transform_matrix(
                         direct_child.value().id),
                     direct_child_world_before);
  expect_matrix_near(harness.session.scene().world_transform_matrix(
                         grandchild.value().id),
                     grandchild_world_before);
}

/**
 * Enumerates atomic-rejection scenarios shared by the invalid-selection test.
 */
enum class InvalidCopyCase {
  HiddenTarget,
  LockedTarget,
  MissingTargetMesh,
  UnstableTargetTopology,
  HiddenSource,
  MissingSourceMesh,
  UnstableCpuOnlySource,
  SingularSourceTransform,
  SingularTargetTransform,
  ComponentMode,
};

void run_invalid_copy_case(InvalidCopyCase copy_case) {
  CopyPivotHarness harness;
  const quader::QuaderModelingObjectRef source =
      create_asymmetric_mesh(harness, "Invalid Case Source");
  const quader::QuaderModelingObjectRef valid_target =
      create_asymmetric_mesh(harness, "Valid Earlier Target", 0.8f);
  const quader::QuaderModelingObjectRef invalid_target =
      create_asymmetric_mesh(harness, "Invalid Later Target", 1.2f);
  set_transform(harness, source.entity,
                {.position = {7.0f, 3.0f, -2.0f},
                 .rotation_euler = {0.2f, -0.3f, 0.1f},
                 .custom_pivot = true});
  set_transform(harness, valid_target.entity,
                {.position = {-6.0f, 1.0f, 4.0f}});
  set_transform(harness, invalid_target.entity,
                {.position = {2.0f, -5.0f, 3.0f}});
  ASSERT_LT(valid_target.entity, invalid_target.entity);
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({source.entity, valid_target.entity,
                                   invalid_target.entity},
                                  source.entity)
                  .ok());

  switch (copy_case) {
  case InvalidCopyCase::HiddenTarget:
    ASSERT_TRUE(
        harness.session.scene().set_visible(invalid_target.entity, false));
    break;
  case InvalidCopyCase::LockedTarget:
    ASSERT_TRUE(
        harness.session.scene().set_locked(invalid_target.entity, true));
    break;
  case InvalidCopyCase::MissingTargetMesh: {
    quader::MeshComponent missing =
        *harness.session.scene().mesh(invalid_target.entity);
    missing.mesh = std::numeric_limits<quader::AssetId>::max();
    harness.session.scene().set_mesh(invalid_target.entity, missing);
    break;
  }
  case InvalidCopyCase::UnstableTargetTopology: {
    const quader::AssetId unstable_mesh =
        mesh_id(harness, invalid_target.entity);
    ASSERT_TRUE(harness.session.assets().update_mesh(
        unstable_mesh, [](quader::MeshAsset &mesh) {
          mesh.has_stable_cpu_topology = false;
        }));
    break;
  }
  case InvalidCopyCase::HiddenSource:
    ASSERT_TRUE(harness.session.scene().set_visible(source.entity, false));
    break;
  case InvalidCopyCase::MissingSourceMesh: {
    quader::MeshComponent missing =
        *harness.session.scene().mesh(source.entity);
    missing.mesh = std::numeric_limits<quader::AssetId>::max();
    harness.session.scene().set_mesh(source.entity, missing);
    break;
  }
  case InvalidCopyCase::UnstableCpuOnlySource: {
    const quader::AssetId unstable_mesh = mesh_id(harness, source.entity);
    ASSERT_TRUE(harness.session.assets().update_mesh(
        unstable_mesh, [](quader::MeshAsset &mesh) {
          mesh.authored_polygon_document.reset();
          mesh.has_authored_polygon_document = false;
          mesh.has_stable_cpu_topology = false;
        }));
    break;
  }
  case InvalidCopyCase::SingularSourceTransform: {
    quader::TransformComponent singular =
        *harness.session.scene().transform(source.entity);
    singular.scale.x = 0.0f;
    set_transform(harness, source.entity, singular);
    break;
  }
  case InvalidCopyCase::SingularTargetTransform: {
    quader::TransformComponent singular =
        *harness.session.scene().transform(invalid_target.entity);
    singular.scale.y = 0.0f;
    set_transform(harness, invalid_target.entity, singular);
    break;
  }
  case InvalidCopyCase::ComponentMode:
    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Vertex)
                    .ok());
    break;
  }

  const quader::TransformComponent valid_transform_before =
      *harness.session.scene().transform(valid_target.entity);
  const quader::AssetId valid_mesh_before =
      mesh_id(harness, valid_target.entity);
  const std::vector<quader::QVec3> valid_vertices_before =
      world_vertices(harness, valid_target.entity);
  const std::size_t mesh_count_before = harness.session.assets().mesh_count();
  const std::vector<quader::EntityId> selected_before =
      harness.session.scene().selected_entities();
  harness.session.history().clear();

  EXPECT_FALSE(harness.editor.modeling().can_copy_pivot_from_active());
  const quader::QuaderApiVoidResult result =
      harness.editor.modeling().copy_pivot_from_active();
  EXPECT_FALSE(result.ok());
  EXPECT_FALSE(result.changed());
  EXPECT_FALSE(result.error_message().empty());
  EXPECT_FALSE(harness.editor.history().can_undo());
  EXPECT_FALSE(harness.editor.history().can_redo());
  expect_transform_near(
      *harness.session.scene().transform(valid_target.entity),
      valid_transform_before);
  EXPECT_EQ(mesh_id(harness, valid_target.entity), valid_mesh_before);
  expect_vertices_near(world_vertices(harness, valid_target.entity),
                       valid_vertices_before);
  EXPECT_EQ(harness.session.assets().mesh_count(), mesh_count_before);
  EXPECT_EQ(harness.session.scene().selected_entities(), selected_before);
  EXPECT_EQ(harness.session.scene().primary_selection(), source.entity);
}

TEST(CopyPivotFromActiveTests,
     InvalidSourcesModesAndTargetsFailWithoutPartialMutationOrHistory) {
  constexpr std::array kCases{
      InvalidCopyCase::HiddenTarget,
      InvalidCopyCase::LockedTarget,
      InvalidCopyCase::MissingTargetMesh,
      InvalidCopyCase::UnstableTargetTopology,
      InvalidCopyCase::HiddenSource,
      InvalidCopyCase::MissingSourceMesh,
      InvalidCopyCase::UnstableCpuOnlySource,
      InvalidCopyCase::SingularSourceTransform,
      InvalidCopyCase::SingularTargetTransform,
      InvalidCopyCase::ComponentMode,
  };
  for (const InvalidCopyCase copy_case : kCases) {
    SCOPED_TRACE(static_cast<int>(copy_case));
    run_invalid_copy_case(copy_case);
  }
}

TEST(CopyPivotFromActiveTests,
     ActiveModelingPreviewBlocksCopyWithoutDiscardingThePreview) {
  CopyPivotHarness harness;
  const quader::QuaderModelingObjectRef source =
      create_asymmetric_mesh(harness, "Preview Guard Source");
  const quader::QuaderModelingObjectRef target =
      create_asymmetric_mesh(harness, "Preview Guard Target");
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({source.entity, target.entity}, source.entity)
                  .ok());
  harness.session.history().clear();

  const std::string preview_command =
      harness.editor.commands().command_id_for_modeling_action("half_cut");
  ASSERT_FALSE(preview_command.empty());
  const quader::QuaderApiVoidResult preview =
      harness.editor.commands().execute(preview_command);
  ASSERT_TRUE(preview.ok()) << preview.error_message();
  ASSERT_TRUE(harness.editor.modeling().can_cancel_active_tool_or_preview());
  const quader::QuaderToolSettingsTarget settings_before =
      harness.editor.tools().active_settings();
  ASSERT_TRUE(settings_before.active);
  ASSERT_TRUE(settings_before.preview_active);
  EXPECT_EQ(settings_before.tool_id, "half_cut");
  const std::uint64_t scene_revision_before =
      harness.session.scene().revision();
  const std::uint64_t asset_revision_before =
      harness.session.assets().revision();
  const quader::QuaderHistorySnapshot history_before =
      harness.editor.query().history();

  EXPECT_FALSE(harness.editor.modeling().can_copy_pivot_from_active());
  std::optional<quader::QuaderCommandDescriptor> command =
      command_descriptor(harness);
  ASSERT_TRUE(command.has_value());
  EXPECT_FALSE(command->enabled);
  const quader::QuaderActionSet operations =
      harness.editor.modeling().available_action_set({
          .surface = quader::QuaderActionSurface::OperationsPanel,
          .include_disabled = true,
      });
  const quader::QuaderActionPanel *object_panel =
      find_panel(operations, "object_operations");
  ASSERT_NE(object_panel, nullptr);
  const quader::QuaderActionItem *action =
      find_action(*object_panel, "copy_pivot_from_active");
  ASSERT_NE(action, nullptr);
  EXPECT_EQ(action->visibility, quader::QuaderActionVisibility::Disabled);
  const quader::QuaderApiVoidResult copied =
      harness.editor.modeling().copy_pivot_from_active();
  EXPECT_FALSE(copied.ok());
  EXPECT_FALSE(copied.changed());
  EXPECT_EQ(harness.session.scene().revision(), scene_revision_before);
  EXPECT_EQ(harness.session.assets().revision(), asset_revision_before);
  EXPECT_EQ(harness.editor.query().history().undo_label,
            history_before.undo_label);
  const quader::QuaderToolSettingsTarget settings_after =
      harness.editor.tools().active_settings();
  EXPECT_TRUE(settings_after.active);
  EXPECT_TRUE(settings_after.preview_active);
  EXPECT_EQ(settings_after.tool_id, "half_cut");

  ASSERT_TRUE(harness.editor.tools().cancel_active_tool_preview().ok());
}

TEST(CopyPivotFromActiveTests,
     TypedCommandAndNativeActionsExposeExactAvailabilityAndOrdering) {
  CopyPivotHarness harness;
  const quader::QuaderModelingObjectRef source =
      create_asymmetric_mesh(harness, "Action Source");
  ASSERT_TRUE(harness.editor.selection().select_object(source).ok());

  std::optional<quader::QuaderCommandDescriptor> command =
      command_descriptor(harness);
  ASSERT_TRUE(command.has_value());
  EXPECT_EQ(command->label, "Copy Pivot From Active");
  EXPECT_TRUE(command->shortcut.empty());
  EXPECT_FALSE(command->enabled);

  quader::QuaderActionSet operations =
      harness.editor.modeling().available_action_set({
          .surface = quader::QuaderActionSurface::OperationsPanel,
          .include_disabled = true,
      });
  const quader::QuaderActionPanel *object_panel =
      find_panel(operations, "object_operations");
  ASSERT_NE(object_panel, nullptr);
  ASSERT_FALSE(object_panel->actions.empty());
  const quader::QuaderActionItem &disabled = object_panel->actions.back();
  EXPECT_EQ(disabled.action_id, "copy_pivot_from_active");
  EXPECT_TRUE(disabled.operation_id.empty());
  EXPECT_EQ(disabled.label, "Copy Pivot From Active");
  EXPECT_EQ(disabled.kind, quader::QuaderActionKind::Operation);
  EXPECT_EQ(disabled.visibility, quader::QuaderActionVisibility::Disabled);
  EXPECT_EQ(disabled.disabled_reason_key,
            "modeling.action.disabled.copy_pivot_from_active_selection");

  const quader::QuaderModelingObjectRef target =
      create_asymmetric_mesh(harness, "Action Target");
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({source.entity, target.entity}, source.entity)
                  .ok());
  command = command_descriptor(harness);
  ASSERT_TRUE(command.has_value());
  EXPECT_TRUE(command->enabled);
  EXPECT_TRUE(harness.editor.modeling().can_copy_pivot_from_active());

  operations = harness.editor.modeling().available_action_set({
      .surface = quader::QuaderActionSurface::OperationsPanel,
      .include_disabled = true,
  });
  object_panel = find_panel(operations, "object_operations");
  ASSERT_NE(object_panel, nullptr);
  ASSERT_FALSE(object_panel->actions.empty());
  EXPECT_EQ(object_panel->actions.back().action_id,
            "copy_pivot_from_active");
  EXPECT_EQ(object_panel->actions.back().visibility,
            quader::QuaderActionVisibility::Enabled);

  const quader::QuaderActionSet context =
      harness.editor.modeling().available_action_set({
          .surface = quader::QuaderActionSurface::ViewportContextMenu,
          .include_disabled = true,
      });
  const quader::QuaderActionPanel *context_panel =
      find_panel(context, "viewport_context_menu");
  ASSERT_NE(context_panel, nullptr);
  const auto copy = std::find_if(
      context_panel->actions.begin(), context_panel->actions.end(),
      [](const quader::QuaderActionItem &action) {
        return action.action_id == "copy_pivot_from_active";
      });
  ASSERT_NE(copy, context_panel->actions.end());
  ASSERT_NE(std::next(copy), context_panel->actions.end());
  EXPECT_TRUE(std::next(copy)->action_id == "delete_selection" ||
              std::next(copy)->action_id == "duplicate_meshes");

  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Vertex)
                  .ok());
  EXPECT_FALSE(harness.editor.modeling().can_copy_pivot_from_active());
  command = command_descriptor(harness);
  ASSERT_TRUE(command.has_value());
  EXPECT_FALSE(command->enabled);
  operations = harness.editor.modeling().available_action_set({
      .surface = quader::QuaderActionSurface::OperationsPanel,
      .include_disabled = true,
      .include_hidden = true,
  });
  object_panel = find_panel(operations, "object_operations");
  ASSERT_NE(object_panel, nullptr);
  const quader::QuaderActionItem *hidden =
      find_action(*object_panel, "copy_pivot_from_active");
  ASSERT_NE(hidden, nullptr);
  EXPECT_EQ(hidden->visibility, quader::QuaderActionVisibility::Hidden);
}

} // namespace

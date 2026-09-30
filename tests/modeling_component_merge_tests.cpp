////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "api/quader_polygon_document_internal_access.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/session/project_session.hpp"
#include "math/glm_bridge.hpp"
#include "support/command_observable_state.hpp"

#include <quader/modeling/neutral/polygon/document.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace quader {
namespace {

bool same_position(QVec3 left, QVec3 right) {
  return left.x == right.x && left.y == right.y && left.z == right.z;
}

std::vector<QuaderElementId> vertices_at(const quader_poly::Document &document,
                                       QVec3 position) {
  std::vector<QuaderElementId> vertices;
  for (const auto &vertex : document.vertices) {
    if (same_position(vertex.position, position)) {
      vertices.push_back(static_cast<QuaderElementId>(vertex.id));
    }
  }
  return vertices;
}

std::vector<quader_poly::Edge> edges_at(const quader_poly::Document &document,
                                     QVec3 first, QVec3 second) {
  std::vector<quader_poly::Edge> edges;
  for (const auto &face : document.faces) {
    for (std::size_t index = 0; index < face.vertices.size(); ++index) {
      const auto edge = quader_poly::make_edge(
          face.vertices[index], face.vertices[(index + 1U) % face.vertices.size()]);
      const auto *a = quader_poly::find_vertex(document, edge.a);
      const auto *b = quader_poly::find_vertex(document, edge.b);
      if (a != nullptr && b != nullptr &&
          ((same_position(a->position, first) && same_position(b->position, second)) ||
           (same_position(a->position, second) && same_position(b->position, first))) &&
          std::ranges::find(edges, edge) == edges.end()) {
        edges.push_back(edge);
      }
    }
  }
  return edges;
}

quader_poly::Document saved_insert_edge_loop_source() {
  // Authored topology from SEL-20260930-B0193489, with its original sparse IDs.
  // Both end caps contain a collinear corner opposite the selected quad strip.
  quader_poly::Document document;
  document.vertices = {
      {1, {-12.5F, -0.5F, 14.5F}}, {2, {-0.5F, -0.5F, 14.5F}},
      {3, {-0.5F, 17.5F, 14.5F}}, {4, {-12.5F, 17.5F, 14.5F}},
      {5, {-12.5F, -0.5F, -14.5F}}, {6, {-12.5F, 17.5F, -14.5F}},
      {7, {-0.5F, 17.5F, -14.5F}}, {8, {-0.5F, -0.5F, -14.5F}},
      {9, {-12.5F, 17.5F, 4.5F}}, {10, {-12.5F, -0.5F, 4.5F}},
      {11, {-12.5F, -0.5F, -3.5F}}, {12, {-12.5F, 17.5F, -3.5F}},
      {13, {-12.5F, 11.5F, 14.5F}}, {14, {-12.5F, 11.5F, 4.5F}},
      {15, {-12.5F, 11.5F, -3.5F}}, {16, {-12.5F, 11.5F, -14.5F}},
      {89, {-32.5F, 11.5F, 4.5F}}, {90, {-32.5F, -0.5F, 4.5F}},
      {91, {-32.5F, -0.5F, -3.5F}}, {92, {-32.5F, 11.5F, -3.5F}},
      {223, {-12.5F, -0.5F, 26.499998092651367F}},
      {224, {-12.5F, 11.5F, 26.499998092651367F}},
      {225, {-12.5F, 17.5F, 26.499998092651367F}},
      {226, {-0.5F, 17.5F, 26.499998092651367F}},
      {227, {-0.5F, -0.5F, 26.499998092651367F}},
      {228, {-12.5F, -0.5F, -71.5F}}, {229, {-0.5F, -0.5F, -71.5F}},
      {230, {-0.5F, 17.5F, -71.5F}}, {231, {-12.5F, 17.5F, -71.5F}},
      {232, {-12.5F, 11.5F, -71.5F}}, {233, {-0.5F, -0.5F, -57.5F}},
      {234, {-0.5F, 17.5F, -57.5F}}, {235, {-0.5F, 17.5F, -51.5F}},
      {236, {-0.5F, -0.5F, -51.5F}},
  };
  // Saved faces have empty UV charts, material slot zero, authored shading and
  // identity texture transforms, matching the neutral face defaults.
  document.faces = {
      {.id = 1, .vertices = {223, 224, 225, 226, 227}},
      {.id = 2, .vertices = {228, 229, 230, 231, 232}},
      {.id = 4, .vertices = {13, 1, 10, 14}},
      {.id = 9, .vertices = {14, 9, 4, 13}},
      {.id = 7, .vertices = {15, 11, 5, 16}},
      {.id = 10, .vertices = {16, 6, 12, 15}},
      {.id = 8, .vertices = {89, 90, 91, 92}},
      {.id = 11, .vertices = {15, 12, 9, 14}},
      {.id = 5, .vertices = {6, 7, 3, 4, 9, 12}},
      {.id = 6, .vertices = {5, 11, 10, 1, 2, 8}},
      {.id = 84, .vertices = {14, 10, 90, 89}},
      {.id = 85, .vertices = {10, 11, 91, 90}},
      {.id = 86, .vertices = {11, 15, 92, 91}},
      {.id = 87, .vertices = {15, 14, 89, 92}},
      {.id = 218, .vertices = {1, 13, 224, 223}},
      {.id = 219, .vertices = {13, 4, 225, 224}},
      {.id = 220, .vertices = {4, 3, 226, 225}},
      {.id = 222, .vertices = {2, 1, 223, 227}},
      {.id = 223, .vertices = {5, 8, 236, 233, 229, 228}},
      {.id = 225, .vertices = {7, 6, 231, 230, 234, 235}},
      {.id = 226, .vertices = {6, 16, 232, 231}},
      {.id = 227, .vertices = {16, 5, 228, 232}},
      {.id = 228, .vertices = {229, 233, 234, 230}},
      {.id = 224, .vertices = {233, 236, 235, 234}},
      {.id = 229, .vertices = {236, 8, 7, 235}},
      {.id = 3, .vertices = {8, 2, 3, 7}},
      {.id = 221, .vertices = {2, 227, 226, 3}},
  };
  document.next_vertex_id = 249;
  document.next_face_id = 240;
  return document;
}

std::map<std::pair<quader_poly::ElementId, quader_poly::ElementId>, std::size_t>
authored_edge_incidence(const quader_poly::Document &document) {
  std::map<std::pair<quader_poly::ElementId, quader_poly::ElementId>, std::size_t> incidence;
  for (const auto &face : document.faces) {
    for (std::size_t index = 0; index < face.vertices.size(); ++index) {
      const auto edge = quader_poly::make_edge(
          face.vertices[index], face.vertices[(index + 1U) % face.vertices.size()]);
      ++incidence[{edge.a, edge.b}];
    }
  }
  return incidence;
}

class ModelingComponentMergeTests : public ::testing::Test {
protected:
  ModelingComponentMergeTests()
      : root_(std::filesystem::temp_directory_path() /
              ("quader-component-merge-" + std::string(ProjectId::generate().str()))),
        editor_(project_) {}

  void SetUp() override {
    std::filesystem::create_directory(root_);
    const auto created = editor_.project().create(root_);
    ASSERT_TRUE(created.ok()) << created.error_message();
    const auto box = editor_.modeling().create_box(
        {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}},
        "Merge Source");
    ASSERT_TRUE(box.ok()) << box.error_message();
    source_ = box.value();
    survivor_ = source_;
  }

  void TearDown() override {
    std::error_code ignored;
    std::filesystem::remove_all(root_, ignored);
  }

  quader_poly::Document topology(QuaderModelingObjectRef object) const {
    const auto live = editor_.query().modeling_object_for_id(object.modeling_object_id);
    EXPECT_TRUE(live.has_value());
    if (!live.has_value()) {
      return {};
    }
    const auto *mesh = project_.active_scene()->session->assets().mesh(live->mesh);
    EXPECT_NE(mesh, nullptr);
    if (mesh == nullptr || !mesh->authored_polygon_document) {
      ADD_FAILURE() << "Expected published authored topology.";
      return {};
    }
    return *mesh->authored_polygon_document;
  }

  std::size_t vertex_count() const {
    std::size_t count = 0U;
    for (const auto &object : editor_.query().modeling_objects()) {
      if (object.mesh == kInvalidAssetId) {
        continue;
      }
      const auto live = editor_.query().modeling_object_for_id(object.modeling_object_id);
      if (live.has_value()) {
        count += topology(*live).vertices.size();
      }
    }
    return count;
  }

  void split_positive_x_face(bool extract) {
    const auto document = topology(source_);
    const auto face = std::ranges::find_if(document.faces, [&](const auto &candidate) {
      return std::ranges::all_of(candidate.vertices, [&](const auto vertex) {
        return quader_poly::find_vertex(document, vertex)->position.x == 1.0F;
      });
    });
    ASSERT_NE(face, document.faces.end());
    ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Face).ok());
    ASSERT_TRUE(editor_.selection().select_face(
        source_, static_cast<QuaderElementId>(face->id)).ok());
    const auto split = editor_.commands().execute(
        extract ? quader_command::kModelingExtractFaces : quader_command::kModelingDetachFaces);
    ASSERT_TRUE(split.ok()) << split.error_message();
    ASSERT_TRUE(split.changed());
    ASSERT_EQ(editor_.query().modeling_objects().size(), extract ? 2U : 1U);
    if (extract) {
      const auto selected = editor_.selection().summary();
      const auto active = editor_.query().modeling_object_for_id(selected.active_object_id);
      ASSERT_TRUE(active.has_value());
      ASSERT_NE(active->modeling_object_id, source_.modeling_object_id);
      survivor_ = *active;
    }
    EXPECT_EQ(vertex_count(), 12U);
  }

  void select_boundary_vertices(bool extract) {
    const auto source_vertices = vertices_at(topology(source_), {1.0F, 1.0F, 1.0F});
    ASSERT_EQ(source_vertices.size(), extract ? 1U : 2U);
    ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Vertex).ok());
    ASSERT_TRUE(editor_.selection().clear().ok());
    ASSERT_EQ(editor_.selection().summary().selected_vertex_count, 0U);
    ASSERT_TRUE(editor_.selection().select_vertex(source_, source_vertices.front()).ok());
    if (extract) {
      const auto target_vertices = vertices_at(topology(survivor_), {1.0F, 1.0F, 1.0F});
      ASSERT_EQ(target_vertices.size(), 1U);
      ASSERT_TRUE(editor_.selection().select_vertex(
          survivor_, target_vertices.front(), QuaderSelectionEdit::Add).ok());
    } else {
      ASSERT_TRUE(editor_.selection().select_vertex(
          source_, source_vertices.back(), QuaderSelectionEdit::Add).ok());
    }
    ASSERT_EQ(editor_.selection().summary().selected_vertex_count, 2U);
    const auto command = editor_.commands().descriptor(quader_command::kModelingMergeByDistance);
    ASSERT_TRUE(command.has_value());
    EXPECT_TRUE(command->enabled);
  }

  void select_boundary_edges(bool extract) {
    const auto source_edges = edges_at(topology(source_), {1.0F, -1.0F, 1.0F},
                                      {1.0F, 1.0F, 1.0F});
    ASSERT_EQ(source_edges.size(), extract ? 1U : 2U);
    ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Edge).ok());
    ASSERT_TRUE(editor_.selection().clear().ok());
    ASSERT_EQ(editor_.selection().summary().selected_edge_count, 0U);
    ASSERT_TRUE(editor_.selection().select_edge(
        source_, static_cast<QuaderElementId>(source_edges.front().a),
        static_cast<QuaderElementId>(source_edges.front().b)).ok());
    const auto target_edges = extract
        ? edges_at(topology(survivor_), {1.0F, -1.0F, 1.0F}, {1.0F, 1.0F, 1.0F})
        : source_edges;
    ASSERT_EQ(target_edges.size(), extract ? 1U : 2U);
    ASSERT_TRUE(editor_.selection().select_edge(
        survivor_, static_cast<QuaderElementId>(target_edges.back().a),
        static_cast<QuaderElementId>(target_edges.back().b), QuaderSelectionEdit::Add).ok());
    ASSERT_EQ(editor_.selection().summary().selected_edge_count, 2U);
    const auto command = editor_.commands().descriptor(quader_command::kModelingMergeEdges);
    ASSERT_TRUE(command.has_value());
    EXPECT_TRUE(command->enabled);
    const auto operations = editor_.modeling().instant_operations();
    const auto merge = std::ranges::find(operations, std::string("merge_edges"),
                                       &QuaderInstantOperationDescriptor::id);
    ASSERT_NE(merge, operations.end());
    EXPECT_TRUE(merge->enabled);
  }

  void expect_semantics(const test::CommandObservableState &expected,
                        bool include_history = false) const {
    using Domain = test::CommandObservableDomain;
    const auto actual = test::capture_command_observable_state(editor_);
    EXPECT_EQ(actual.domain(Domain::Geometry), expected.domain(Domain::Geometry));
    EXPECT_EQ(actual.domain(Domain::Selection), expected.domain(Domain::Selection))
        << test::diff_command_observable_state(
               expected, actual, test::command_observable_domain_mask(Domain::Selection)).summary;
    if (include_history) {
      EXPECT_EQ(actual.domain(Domain::History), expected.domain(Domain::History));
    }
  }

  void expect_component_operation_availability(
      QuaderCommandId command, std::string_view action_id, bool enabled) const {
    const auto descriptor = editor_.commands().descriptor(command);
    ASSERT_TRUE(descriptor.has_value());
    EXPECT_EQ(descriptor->enabled, enabled);
    const auto operations = editor_.modeling().instant_operations();
    const auto operation = std::ranges::find(
        operations, action_id, &QuaderInstantOperationDescriptor::id);
    ASSERT_NE(operation, operations.end());
    EXPECT_EQ(operation->enabled, enabled);
    const auto actions = editor_.modeling().available_actions({.include_hidden = true});
    const auto action = std::ranges::find(actions, action_id, &QuaderActionItem::action_id);
    ASSERT_NE(action, actions.end());
    EXPECT_EQ(action->visibility, enabled ? QuaderActionVisibility::Enabled
                                         : QuaderActionVisibility::Disabled);
    if (!enabled) {
      EXPECT_EQ(action->disabled_reason_key, "modeling.action.disabled.single_object");
    }
  }

  void exercise_distance_preview(bool extract) {
    split_positive_x_face(extract);
    ASSERT_FALSE(HasFatalFailure());
    select_boundary_vertices(extract);
    ASSERT_FALSE(HasFatalFailure());
    const auto before = test::capture_command_observable_state(editor_);
    const auto begin = editor_.modeling().begin_preview("merge_by_distance");
    ASSERT_TRUE(begin.ok()) << begin.error_message();
    EXPECT_TRUE(begin.changed());
    EXPECT_EQ(vertex_count(), 11U);
    QuaderPreviewSettings settings;
    settings.interaction.merge_by_distance_tolerance = 0.0;
    const auto update = editor_.modeling().update_preview(settings);
    ASSERT_TRUE(update.ok()) << update.error_message();
    EXPECT_EQ(vertex_count(), 11U);
    const auto cancel = editor_.modeling().cancel_preview();
    ASSERT_TRUE(cancel.ok()) << cancel.error_message();
    EXPECT_EQ(editor_.query().modeling_objects().size(), extract ? 2U : 1U);
    expect_semantics(before, true);

    ASSERT_TRUE(editor_.modeling().begin_preview("merge_by_distance").ok());
    const auto commit = editor_.modeling().commit_preview("Merge by Distance");
    ASSERT_TRUE(commit.ok()) << commit.error_message();
    EXPECT_TRUE(commit.changed());
    EXPECT_EQ(editor_.query().modeling_objects().size(), 1U);
    EXPECT_EQ(vertex_count(), 11U);
    EXPECT_EQ(topology(survivor_).faces.size(), 6U);
    EXPECT_EQ(editor_.selection().summary().selected_vertex_count, 1U);
    const auto after = test::capture_command_observable_state(editor_);
    ASSERT_TRUE(editor_.history().undo().ok());
    EXPECT_EQ(editor_.query().modeling_objects().size(), extract ? 2U : 1U);
    expect_semantics(before);
    ASSERT_TRUE(editor_.history().redo().ok());
    EXPECT_EQ(editor_.query().modeling_objects().size(), 1U);
    expect_semantics(after);
  }

  void exercise_edge_merge(bool extract) {
    split_positive_x_face(extract);
    ASSERT_FALSE(HasFatalFailure());
    select_boundary_edges(extract);
    ASSERT_FALSE(HasFatalFailure());
    const auto before = test::capture_command_observable_state(editor_);
    const auto merged = editor_.commands().execute(quader_command::kModelingMergeEdges);
    ASSERT_TRUE(merged.ok()) << merged.error_message();
    EXPECT_TRUE(merged.changed());
    EXPECT_EQ(editor_.query().modeling_objects().size(), 1U);
    EXPECT_EQ(vertex_count(), 10U);
    EXPECT_EQ(topology(survivor_).faces.size(), 6U);
    EXPECT_EQ(editor_.selection().summary().selected_edge_count, 1U);
    const auto after = test::capture_command_observable_state(editor_);
    ASSERT_TRUE(editor_.history().undo().ok());
    EXPECT_EQ(editor_.query().modeling_objects().size(), extract ? 2U : 1U);
    expect_semantics(before);
    ASSERT_TRUE(editor_.history().redo().ok());
    expect_semantics(after);
  }

  void exercise_hierarchy_merge(
      bool merge_edges, std::optional<QuaderCommandId> vertex_command = {}) {
    split_positive_x_face(true);
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_TRUE(editor_.scene().reparent(survivor_.entity, source_.entity).ok());
    const auto child = editor_.scene().create_entity({
        .name = "Source Child", .parent = source_.entity,
        .transform = {.position = {3.0F, 2.0F, 0.0F}}});
    ASSERT_TRUE(child.ok()) << child.error_message();
    ASSERT_TRUE(editor_.object(source_).set_transform(
        {.position = {5.0F, 2.0F, -1.0F}, .scale = {2.0F, 3.0F, 1.0F}}).ok());
    const auto child_world_before = project_.active_scene()->session->scene().world_transform_matrix(child.value().id);
    const auto survivor_world_before = project_.active_scene()->session->scene().world_transform_matrix(survivor_.entity);
    if (merge_edges) {
      select_boundary_edges(true);
    } else {
      select_boundary_vertices(true);
    }
    ASSERT_FALSE(HasFatalFailure());
    const auto before = test::capture_command_observable_state(editor_);
    if (merge_edges || vertex_command.has_value()) {
      const auto merged = editor_.commands().execute(
          vertex_command.value_or(quader_command::kModelingMergeEdges));
      ASSERT_TRUE(merged.ok()) << merged.error_message();
    } else {
      ASSERT_TRUE(editor_.modeling().begin_preview("merge_by_distance").ok());
      const auto commit = editor_.modeling().commit_preview("Merge by Distance");
      ASSERT_TRUE(commit.ok()) << commit.error_message();
    }
    EXPECT_FALSE(editor_.query().entity(source_.entity).has_value());
    const auto child_after = editor_.query().entity(child.value().id);
    const auto survivor_after = editor_.query().entity(survivor_.entity);
    ASSERT_TRUE(child_after.has_value());
    ASSERT_TRUE(survivor_after.has_value());
    EXPECT_NE(child_after->parent, source_.entity);
    EXPECT_NE(survivor_after->parent, source_.entity);
    EXPECT_EQ(project_.active_scene()->session->scene().world_transform_matrix(child.value().id).values,
              child_world_before.values);
    EXPECT_EQ(project_.active_scene()->session->scene().world_transform_matrix(survivor_.entity).values,
              survivor_world_before.values);
    ASSERT_TRUE(editor_.history().undo().ok());
    const auto restored_child = editor_.query().entity(child.value().id);
    const auto restored_survivor = editor_.query().entity(survivor_.entity);
    ASSERT_TRUE(restored_child.has_value());
    ASSERT_TRUE(restored_survivor.has_value());
    EXPECT_EQ(restored_child->parent, source_.entity);
    EXPECT_EQ(restored_survivor->parent, source_.entity);
    expect_semantics(before);
    ASSERT_TRUE(editor_.history().redo().ok());
    EXPECT_TRUE(editor_.query().entity(child.value().id).has_value());
    EXPECT_TRUE(editor_.query().entity(survivor_.entity).has_value());
  }

  QuaderModelingObjectRef create_quad(float x, std::string name) {
    quader_poly::Document document;
    const std::vector<quader_poly::ElementId> vertices{
        quader_poly::add_vertex(document, {x, 0.0F, 0.0F}),
        quader_poly::add_vertex(document, {x + 2.0F, 0.0F, 0.0F}),
        quader_poly::add_vertex(document, {x + 2.0F, 2.0F, 0.0F}),
        quader_poly::add_vertex(document, {x, 2.0F, 0.0F}),
    };
    EXPECT_NE(quader_poly::add_face(document, vertices),
              quader_poly::kInvalidElementId);
    const auto created = editor_.modeling().create_mesh(
        QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
        {.name = std::move(name)});
    EXPECT_TRUE(created.ok()) << created.error_message();
    return created.ok() ? created.value() : QuaderModelingObjectRef{};
  }

  void retain_component_object_scope(
      QuaderModelingObjectRef componentless,
      std::initializer_list<EntityId> entities, QuaderModelingObjectRef active,
      QuaderComponentKind kind) {
    ASSERT_TRUE(editor_.selection().select_components(
        {.object = componentless, .kind = kind,
         .edit = QuaderSelectionEdit::Replace}).ok());
    ASSERT_TRUE(editor_.selection().select_objects(entities, active.entity).ok());
    ASSERT_TRUE(editor_.selection().select_components(
        {.object = componentless, .kind = kind,
         .edit = QuaderSelectionEdit::Add}).ok());
    if (active.entity != componentless.entity) {
      ASSERT_TRUE(editor_.selection().select_components(
          {.object = active, .kind = kind,
           .edit = QuaderSelectionEdit::Add}).ok());
    }
    const auto summary = editor_.selection().summary();
    ASSERT_EQ(summary.selected_object_count, entities.size());
    ASSERT_EQ(summary.active_object_id, active.modeling_object_id);
  }

  void exercise_three_object_vertex_merge(bool at_center) {
    const auto second = editor_.modeling().create_box(
        {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}},
        "Second Merge Source");
    const auto third = editor_.modeling().create_box(
        {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}},
        "Active Merge Source");
    ASSERT_TRUE(second.ok()) << second.error_message();
    ASSERT_TRUE(third.ok()) << third.error_message();
    survivor_ = third.value();
    ASSERT_TRUE(editor_.object(source_).set_transform(
        {.position = {-5.0F, 1.0F, 0.0F}, .scale = {2.0F, 1.0F, 1.0F}}).ok());
    ASSERT_TRUE(editor_.object(second.value()).set_transform(
        {.position = {4.0F, -2.0F, 1.0F}, .rotation_euler = {0.0F, 0.0F, 90.0F},
         .scale = {2.0F, 1.0F, 3.0F}}).ok());
    ASSERT_TRUE(editor_.object(survivor_).set_transform(
        {.position = {2.0F, 5.0F, -1.0F}, .scale = {-1.0F, 2.0F, 1.0F}}).ok());
    const QVec3 first_local{1.0F, 1.0F, 1.0F};
    const QVec3 second_local{-1.0F, 1.0F, 1.0F};
    const QVec3 third_local{1.0F, -1.0F, 1.0F};
    const auto &scene = project_.active_scene()->session->scene();
    const QVec3 first_world = transform_point(
        scene.world_transform_matrix(source_.entity), first_local);
    const QVec3 second_world = transform_point(
        scene.world_transform_matrix(second.value().entity), second_local);
    const QVec3 third_world = transform_point(
        scene.world_transform_matrix(survivor_.entity), third_local);
    const QVec3 expected = at_center
        ? QVec3{(first_world.x + second_world.x + third_world.x) / 3.0F,
                (first_world.y + second_world.y + third_world.y) / 3.0F,
                (first_world.z + second_world.z + third_world.z) / 3.0F}
        : third_world;
    ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Vertex).ok());
    ASSERT_TRUE(editor_.selection().select_vertex(
        source_, vertices_at(topology(source_), first_local).front()).ok());
    ASSERT_TRUE(editor_.selection().select_vertex(
        second.value(), vertices_at(topology(second.value()), second_local).front(),
        QuaderSelectionEdit::Add).ok());
    ASSERT_TRUE(editor_.selection().select_vertex(
        survivor_, vertices_at(topology(survivor_), third_local).front(),
        QuaderSelectionEdit::Add).ok());
    const auto command = at_center ? quader_command::kModelingMergeVerticesAtCenter
                                   : quader_command::kModelingMergeVertices;
    const auto available = editor_.commands().descriptor(command);
    ASSERT_TRUE(available.has_value());
    ASSERT_TRUE(available->enabled);
    const auto before = test::capture_command_observable_state(editor_);
    const auto merged = editor_.commands().execute(command);
    ASSERT_TRUE(merged.ok()) << merged.error_message();
    ASSERT_TRUE(merged.changed());
    ASSERT_EQ(editor_.query().modeling_objects().size(), 1U);
    EXPECT_EQ(vertex_count(), 22U);
    const auto selected = editor_.selection().summary();
    EXPECT_EQ(selected.active_object_id, survivor_.modeling_object_id);
    EXPECT_EQ(selected.selected_vertex_count, 1U);
    const auto document = topology(survivor_);
    const auto *vertex = quader_poly::find_vertex(document, selected.active_vertex_id);
    ASSERT_NE(vertex, nullptr);
    const QVec3 actual = transform_point(
        scene.world_transform_matrix(survivor_.entity), vertex->position);
    EXPECT_NEAR(actual.x, expected.x, 0.0001F);
    EXPECT_NEAR(actual.y, expected.y, 0.0001F);
    EXPECT_NEAR(actual.z, expected.z, 0.0001F);
    const auto after = test::capture_command_observable_state(editor_);
    ASSERT_TRUE(editor_.history().undo().ok());
    EXPECT_EQ(editor_.query().modeling_objects().size(), 3U);
    expect_semantics(before);
    ASSERT_TRUE(editor_.history().redo().ok());
    expect_semantics(after);
  }

  std::filesystem::path root_;
  ProjectSession project_;
  QuaderEditorApi editor_;
  QuaderModelingObjectRef source_;
  QuaderModelingObjectRef survivor_;
};

TEST_F(ModelingComponentMergeTests, DetachedCoincidentVerticesPreviewCancelCommitAndHistory) {
  exercise_distance_preview(false);
}

TEST_F(ModelingComponentMergeTests, ExtractedCoincidentVerticesPreviewCancelCommitAndHistory) {
  exercise_distance_preview(true);
}

TEST_F(ModelingComponentMergeTests, DetachedCoincidentBoundaryEdgesMergeAndUndoTogether) {
  exercise_edge_merge(false);
}

TEST_F(ModelingComponentMergeTests, ExtractedCoincidentBoundaryEdgesMergeAndUndoTogether) {
  exercise_edge_merge(true);
}

TEST_F(ModelingComponentMergeTests, DistancePreviewCanEnterAndLeaveCrossObjectMergeWithoutHistory) {
  split_positive_x_face(true);
  ASSERT_FALSE(HasFatalFailure());
  ASSERT_TRUE(editor_.object(survivor_).set_transform({.position = {0.5F, 0.0F, 0.0F}}).ok());
  select_boundary_vertices(true);
  ASSERT_FALSE(HasFatalFailure());
  const auto before = test::capture_command_observable_state(editor_);
  const auto begin = editor_.modeling().begin_preview("merge_by_distance");
  ASSERT_TRUE(begin.ok()) << begin.error_message();
  EXPECT_FALSE(begin.changed());
  EXPECT_EQ(vertex_count(), 12U);

  QuaderPreviewSettings settings;
  settings.interaction.merge_by_distance_tolerance = 0.75;
  const auto hit = editor_.modeling().update_preview(settings);
  ASSERT_TRUE(hit.ok()) << hit.error_message();
  EXPECT_TRUE(hit.changed());
  EXPECT_EQ(vertex_count(), 11U);

  settings.interaction.merge_by_distance_tolerance = 0.125;
  const auto no_hit = editor_.modeling().update_preview(settings);
  ASSERT_TRUE(no_hit.ok()) << no_hit.error_message();
  EXPECT_EQ(vertex_count(), 12U);
  const auto commit = editor_.modeling().commit_preview("Merge by Distance");
  ASSERT_TRUE(commit.ok()) << commit.error_message();
  EXPECT_FALSE(commit.changed());
  EXPECT_EQ(editor_.query().modeling_objects().size(), 2U);
  expect_semantics(before, true);
}

TEST_F(ModelingComponentMergeTests, ExtractedMergePreservesReflectedSourceWorldGeometry) {
  split_positive_x_face(true);
  ASSERT_FALSE(HasFatalFailure());
  ASSERT_TRUE(editor_.object(source_).set_transform(
      {.position = {2.0F, 0.0F, 0.0F}, .scale = {-1.0F, 1.0F, 1.0F}}).ok());
  select_boundary_vertices(true);
  ASSERT_FALSE(HasFatalFailure());
  const auto before = test::capture_command_observable_state(editor_);
  const auto begin = editor_.modeling().begin_preview("merge_by_distance");
  ASSERT_TRUE(begin.ok()) << begin.error_message();
  const auto commit = editor_.modeling().commit_preview("Merge by Distance");
  ASSERT_TRUE(commit.ok()) << commit.error_message();
  ASSERT_EQ(editor_.query().modeling_objects().size(), 1U);
  const auto document = topology(survivor_);
  ASSERT_EQ(document.vertices.size(), 11U);
  EXPECT_EQ(std::ranges::min(document.vertices, {}, [](const auto &vertex) {
    return vertex.position.x;
  }).position.x, 1.0F);
  EXPECT_EQ(std::ranges::max(document.vertices, {}, [](const auto &vertex) {
    return vertex.position.x;
  }).position.x, 3.0F);
  for (const auto &face : document.faces) {
    ASSERT_GE(face.vertices.size(), 3U);
    const auto a = quader_poly::find_vertex(document, face.vertices[0])->position;
    const auto b = quader_poly::find_vertex(document, face.vertices[1])->position;
    const auto c = quader_poly::find_vertex(document, face.vertices[2])->position;
    const QVec3 normal{(b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y),
                       (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z),
                       (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)};
    if (std::ranges::all_of(face.vertices, [&](const auto id) {
          return quader_poly::find_vertex(document, id)->position.x == 3.0F;
        })) {
      EXPECT_GT(normal.x, 0.0F);
    }
  }
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_semantics(before);
}

TEST_F(ModelingComponentMergeTests, StructuralMergePreservesSourceChildrenAndNestedSurvivor) {
  exercise_hierarchy_merge(false);
}

TEST_F(ModelingComponentMergeTests, InstantEdgeMergePreservesSourceChildrenAndNestedSurvivor) {
  exercise_hierarchy_merge(true);
}

TEST_F(ModelingComponentMergeTests, ActiveVertexMergePreservesSourceChildrenAndNestedSurvivor) {
  exercise_hierarchy_merge(false, quader_command::kModelingMergeVerticesToActive);
}

TEST_F(ModelingComponentMergeTests, CenterVertexMergePreservesSourceChildrenAndNestedSurvivor) {
  exercise_hierarchy_merge(false, quader_command::kModelingMergeVerticesToCenter);
}

TEST_F(ModelingComponentMergeTests, ThreeObjectActiveMergeUsesGlobalActiveWorldPosition) {
  exercise_three_object_vertex_merge(false);
}

TEST_F(ModelingComponentMergeTests, ThreeObjectCenterMergeUsesWorldCentroid) {
  exercise_three_object_vertex_merge(true);
}

TEST_F(ModelingComponentMergeTests, VertexMergeFailuresPreserveAuthoredSelectionAndHistory) {
  split_positive_x_face(true);
  ASSERT_FALSE(HasFatalFailure());
  select_boundary_vertices(true);
  ASSERT_FALSE(HasFatalFailure());
  const auto before = test::capture_command_observable_state(editor_);
  auto &modeling = project_.active_scene()->session->modeling();
  for (const auto command : {quader_command::kModelingMergeVerticesToActive,
                             quader_command::kModelingMergeVerticesToCenter}) {
    SCOPED_TRACE(command.string());
    for (const auto point : {ModelingSdkProjectionFailurePoint::AfterSdkMutation,
                             ModelingSdkProjectionFailurePoint::BeforeCommit}) {
      SCOPED_TRACE(static_cast<int>(point));
      modeling.set_modeling_sdk_projection_failure_for_tests(point);
      const auto failed = editor_.commands().execute(command);
      modeling.set_modeling_sdk_projection_failure_for_tests(
          ModelingSdkProjectionFailurePoint::None);
      EXPECT_FALSE(failed.ok());
      EXPECT_FALSE(failed.changed());
      EXPECT_FALSE(modeling.operation_preview_active());
      EXPECT_EQ(editor_.query().modeling_objects().size(), 2U);
      expect_semantics(before, true);
    }
  }
}

TEST_F(ModelingComponentMergeTests, EmptyGlobalActiveOwnerRejectsActiveMergeAndAllowsCenter) {
  split_positive_x_face(true);
  ASSERT_FALSE(HasFatalFailure());
  select_boundary_vertices(true);
  ASSERT_FALSE(HasFatalFailure());
  const auto unrelated = create_quad(8.0F, "Componentless Active Mesh");
  retain_component_object_scope(
      unrelated, {source_.entity, survivor_.entity, unrelated.entity}, unrelated,
      QuaderComponentKind::Vertex);
  ASSERT_FALSE(HasFatalFailure());
  ASSERT_EQ(editor_.selection().summary().mode, QuaderSelectionMode::Vertex);
  EXPECT_EQ(editor_.selection().summary().active_kind, QuaderActiveSelectionKind::None);
  const auto active = editor_.commands().descriptor(
      quader_command::kModelingMergeVerticesToActive);
  const auto center = editor_.commands().descriptor(
      quader_command::kModelingMergeVerticesToCenter);
  ASSERT_TRUE(active.has_value());
  ASSERT_TRUE(center.has_value());
  EXPECT_FALSE(active->enabled);
  EXPECT_TRUE(center->enabled);
  const auto before = test::capture_command_observable_state(editor_);
  const auto rejected = editor_.commands().execute(
      quader_command::kModelingMergeVerticesToActive);
  EXPECT_FALSE(rejected.ok());
  EXPECT_FALSE(rejected.changed());
  expect_semantics(before, true);
  const auto merged = editor_.commands().execute(
      quader_command::kModelingMergeVerticesToCenter);
  ASSERT_TRUE(merged.ok()) << merged.error_message();
  EXPECT_TRUE(merged.changed());
  EXPECT_EQ(editor_.query().modeling_objects().size(), 2U);
  EXPECT_EQ(topology(unrelated).vertices.size(), 4U);
  EXPECT_EQ(topology(unrelated).faces.size(), 1U);
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_semantics(before);
}

TEST_F(ModelingComponentMergeTests, VertexMergeRebasesEachSourcesMaterialAndReplaysHistory) {
  split_positive_x_face(true);
  ASSERT_FALSE(HasFatalFailure());
  const auto red = editor_.assets().add_material(
      QuaderMaterialArgs{.name = "Vertex Source Red", .base_color = {1.0F, 0.0F, 0.0F}});
  const auto blue = editor_.assets().add_material(
      QuaderMaterialArgs{.name = "Vertex Survivor Blue", .base_color = {0.0F, 0.0F, 1.0F}});
  ASSERT_TRUE(red.ok()) << red.error_message();
  ASSERT_TRUE(blue.ok()) << blue.error_message();
  ASSERT_TRUE(editor_.object(source_).assign_material(red.value().id).ok());
  ASSERT_TRUE(editor_.object(survivor_).assign_material(blue.value().id).ok());
  const auto &scene = project_.active_scene()->session->scene();
  const auto source_reference = scene.material(source_.entity)->material_for_slot(0U);
  const auto survivor_reference = scene.material(survivor_.entity)->material_for_slot(0U);
  select_boundary_vertices(true);
  ASSERT_FALSE(HasFatalFailure());
  const auto before = test::capture_command_observable_state(editor_);
  for (const auto command : {quader_command::kModelingMergeVerticesToActive,
                             quader_command::kModelingMergeVerticesToCenter}) {
    SCOPED_TRACE(command.string());
    const auto merged = editor_.commands().execute(command);
    ASSERT_TRUE(merged.ok()) << merged.error_message();
    const auto document = topology(survivor_);
    const auto *material = scene.material(survivor_.entity);
    ASSERT_NE(material, nullptr);
    std::size_t source_faces = 0U;
    std::size_t survivor_faces = 0U;
    for (const auto &face : document.faces) {
      const auto resolved = material->material_for_slot(face.material_slot);
      source_faces += resolved == source_reference;
      survivor_faces += resolved == survivor_reference;
    }
    EXPECT_EQ(source_faces, 5U);
    EXPECT_EQ(survivor_faces, 1U);
    const auto after = test::capture_command_observable_state(editor_);
    ASSERT_TRUE(editor_.history().undo().ok());
    expect_semantics(before);
    ASSERT_TRUE(editor_.history().redo().ok());
    expect_semantics(after);
    ASSERT_TRUE(editor_.history().undo().ok());
  }
}

TEST_F(ModelingComponentMergeTests, BoundaryBridgeAvailabilityAcceptsOneEdgeFromEachMesh) {
  const auto first = create_quad(0.0F, "First Bridge Quad");
  const auto second = create_quad(4.0F, "Second Bridge Quad");
  ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Edge).ok());
  const auto first_face = topology(first).faces.front();
  const auto second_face = topology(second).faces.front();
  ASSERT_TRUE(editor_.selection().select_edge(
      first, first_face.vertices[1], first_face.vertices[2]).ok());
  ASSERT_TRUE(editor_.selection().select_edge(
      second, second_face.vertices[3], second_face.vertices[0],
      QuaderSelectionEdit::Add).ok());
  const auto available = editor_.commands().descriptor(
      quader_command::kModelingBridgeEdges);
  ASSERT_TRUE(available.has_value());
  EXPECT_TRUE(available->enabled);
  const auto before = test::capture_command_observable_state(editor_);
  const auto bridged = editor_.commands().execute(quader_command::kModelingBridgeEdges);
  ASSERT_TRUE(bridged.ok()) << bridged.error_message();
  EXPECT_TRUE(bridged.changed());
  EXPECT_EQ(editor_.query().modeling_objects().size(), 2U);
  EXPECT_EQ(topology(second).faces.size(), 3U);
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_semantics(before);
}

TEST_F(ModelingComponentMergeTests, BridgeRejectsOneInteriorEdgeFromEachMeshWithoutMutation) {
  const auto second = editor_.modeling().create_box(
      {.min = {3.0F, -1.0F, -1.0F}, .max = {5.0F, 1.0F, 1.0F}},
      "Interior Bridge Source");
  ASSERT_TRUE(second.ok()) << second.error_message();
  ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Edge).ok());
  const auto first_face = topology(source_).faces.front();
  const auto second_face = topology(second.value()).faces.front();
  ASSERT_TRUE(editor_.selection().select_edge(
      source_, first_face.vertices[0], first_face.vertices[1]).ok());
  ASSERT_TRUE(editor_.selection().select_edge(
      second.value(), second_face.vertices[0], second_face.vertices[1],
      QuaderSelectionEdit::Add).ok());
  const auto available = editor_.commands().descriptor(quader_command::kModelingBridgeEdges);
  ASSERT_TRUE(available.has_value());
  EXPECT_FALSE(available->enabled);
  const auto before = test::capture_command_observable_state(editor_);
  const auto failed = editor_.commands().execute(quader_command::kModelingBridgeEdges);
  EXPECT_FALSE(failed.ok());
  EXPECT_FALSE(failed.changed());
  expect_semantics(before, true);
}

TEST_F(ModelingComponentMergeTests, PolygonReadSnapshotPinsAcceptedPreviewAndRejectsSuppressedSource) {
  split_positive_x_face(true);
  ASSERT_FALSE(HasFatalFailure());
  select_boundary_vertices(true);
  ASSERT_FALSE(HasFatalFailure());
  const auto &modeling = project_.active_scene()->session->modeling();
  const auto committed = modeling.polygon_document_read_snapshot(survivor_.modeling_object_id);
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  EXPECT_FALSE(committed.value().preview);
  EXPECT_EQ(committed.value().document.view().vertices.size(), 4U);
  ASSERT_TRUE(editor_.modeling().begin_preview("merge_by_distance").ok());
  const auto accepted = modeling.polygon_document_read_snapshot(survivor_.modeling_object_id);
  ASSERT_TRUE(accepted.ok()) << accepted.error_message();
  EXPECT_TRUE(accepted.value().preview);
  EXPECT_TRUE(accepted.value().object.valid());
  EXPECT_EQ(accepted.value().document.view().vertices.size(), 11U);
  EXPECT_EQ(committed.value().document.view().vertices.size(), 4U);
  const auto suppressed = modeling.polygon_document_read_snapshot(source_.modeling_object_id);
  EXPECT_FALSE(suppressed.ok());
  const auto source_baseline = modeling.polygon_document_read_snapshot(
      source_.modeling_object_id, false);
  ASSERT_TRUE(source_baseline.ok()) << source_baseline.error_message();
  EXPECT_FALSE(source_baseline.value().preview);
  EXPECT_EQ(source_baseline.value().document.view().vertices.size(), 8U);
  ASSERT_TRUE(editor_.modeling().cancel_preview().ok());
  EXPECT_EQ(accepted.value().document.view().vertices.size(), 11U);
  EXPECT_EQ(committed.value().document.view().vertices.size(), 4U);
}

TEST_F(ModelingComponentMergeTests, ConnectBatchesApplicableMeshesAndPreservesComponentlessObject) {
  const auto first = create_quad(0.0F, "First Connect Quad");
  const auto second = create_quad(4.0F, "Second Connect Quad");
  ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Vertex).ok());
  const auto first_face = topology(first).faces.front();
  const auto second_face = topology(second).faces.front();
  ASSERT_TRUE(editor_.selection().select_vertices(
      first, {static_cast<QuaderElementId>(first_face.vertices[0]),
              static_cast<QuaderElementId>(first_face.vertices[2])}).ok());
  ASSERT_TRUE(editor_.selection().select_vertices(
      second, {static_cast<QuaderElementId>(second_face.vertices[0]),
               static_cast<QuaderElementId>(second_face.vertices[2])},
      QuaderSelectionEdit::Add).ok());
  retain_component_object_scope(
      source_, {source_.entity, first.entity, second.entity}, second,
      QuaderComponentKind::Vertex);
  ASSERT_FALSE(HasFatalFailure());
  ASSERT_EQ(editor_.selection().summary().mode, QuaderSelectionMode::Vertex);
  const auto source_before = topology(source_);
  const auto before = test::capture_command_observable_state(editor_);
  const auto connected = editor_.commands().execute(quader_command::kModelingConnectVertices);
  ASSERT_TRUE(connected.ok()) << connected.error_message();
  EXPECT_TRUE(connected.changed());
  EXPECT_EQ(topology(first).faces.size(), 2U);
  EXPECT_EQ(topology(second).faces.size(), 2U);
  EXPECT_EQ(topology(source_).vertices.size(), source_before.vertices.size());
  EXPECT_EQ(topology(source_).faces.size(), source_before.faces.size());
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_semantics(before);
  ASSERT_TRUE(editor_.history().redo().ok());
  EXPECT_EQ(topology(first).faces.size(), 2U);
  EXPECT_EQ(topology(second).faces.size(), 2U);
}

TEST_F(ModelingComponentMergeTests, ConnectRejectsInvalidApplicableMeshBeforePublishingValidMesh) {
  const auto first = create_quad(0.0F, "Valid Connect Quad");
  const auto second = create_quad(4.0F, "Adjacent Connect Quad");
  ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Vertex).ok());
  const auto first_face = topology(first).faces.front();
  const auto second_face = topology(second).faces.front();
  ASSERT_TRUE(editor_.selection().select_vertices(
      first, {static_cast<QuaderElementId>(first_face.vertices[0]),
              static_cast<QuaderElementId>(first_face.vertices[2])}).ok());
  ASSERT_TRUE(editor_.selection().select_vertices(
      second, {static_cast<QuaderElementId>(second_face.vertices[0]),
               static_cast<QuaderElementId>(second_face.vertices[1])},
      QuaderSelectionEdit::Add).ok());
  const auto before = test::capture_command_observable_state(editor_);
  const auto failed = editor_.commands().execute(quader_command::kModelingConnectVertices);
  EXPECT_FALSE(failed.ok());
  EXPECT_FALSE(failed.changed());
  EXPECT_EQ(topology(first).faces.size(), 1U);
  EXPECT_EQ(topology(second).faces.size(), 1U);
  expect_semantics(before, true);
}

TEST_F(ModelingComponentMergeTests, SplitInteriorEdgeIgnoresComponentlessMeshAndReplaysHistory) {
  const auto unrelated = create_quad(5.0F, "Unselected Split Topology");
  ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Edge).ok());
  const auto face = topology(source_).faces.front();
  ASSERT_TRUE(editor_.selection().select_edge(
      source_, face.vertices[0], face.vertices[1]).ok());
  retain_component_object_scope(
      unrelated, {source_.entity, unrelated.entity}, source_,
      QuaderComponentKind::Edge);
  ASSERT_FALSE(HasFatalFailure());
  ASSERT_EQ(editor_.selection().summary().mode, QuaderSelectionMode::Edge);
  const auto before = test::capture_command_observable_state(editor_);
  const auto split = editor_.commands().execute(quader_command::kModelingSplitEdges);
  ASSERT_TRUE(split.ok()) << split.error_message();
  EXPECT_TRUE(split.changed());
  EXPECT_EQ(topology(source_).vertices.size(), 10U);
  EXPECT_EQ(topology(source_).faces.size(), 6U);
  EXPECT_EQ(editor_.selection().summary().selected_edge_count, 2U);
  EXPECT_EQ(topology(unrelated).vertices.size(), 4U);
  EXPECT_EQ(topology(unrelated).faces.size(), 1U);
  const auto after = test::capture_command_observable_state(editor_);
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_semantics(before);
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_semantics(after);
}

TEST_F(ModelingComponentMergeTests, EdgeConnectAndDissolveRebuildOnlyComponentOwnerAndReplayHistory) {
  const auto quad = create_quad(0.0F, "Edge Support Cut");
  ASSERT_TRUE(editor_.selection().select_face(
      source_, topology(source_).faces.front().id).ok());
  const auto face = topology(quad).faces.front();
  ASSERT_TRUE(editor_.selection().select_edge(
      quad, face.vertices[0], face.vertices[1], QuaderSelectionEdit::Add).ok());
  ASSERT_TRUE(editor_.selection().select_edge(
      quad, face.vertices[2], face.vertices[3], QuaderSelectionEdit::Add).ok());
  ASSERT_EQ(editor_.selection().summary().mode, QuaderSelectionMode::Edge);
  ASSERT_EQ(editor_.selection().summary().selected_object_count, 2U);
  ASSERT_EQ(editor_.selection().summary().selected_edge_count, 2U);
  ASSERT_EQ(editor_.selection().summary().selected_face_count, 1U);
  expect_component_operation_availability(
      quader_command::kModelingConnectEdges, "connect_edges", true);
  ASSERT_FALSE(HasFatalFailure());
  const auto before_connect = test::capture_command_observable_state(editor_);
  const auto connected = editor_.commands().execute(quader_command::kModelingConnectEdges);
  ASSERT_TRUE(connected.ok()) << connected.error_message();
  EXPECT_TRUE(connected.changed());
  EXPECT_EQ(topology(quad).vertices.size(), 6U);
  EXPECT_EQ(topology(quad).faces.size(), 2U);
  EXPECT_EQ(editor_.selection().summary().selected_edge_count, 1U);
  EXPECT_EQ(topology(source_).vertices.size(), 8U);
  EXPECT_EQ(topology(source_).faces.size(), 6U);
  const auto before_dissolve = test::capture_command_observable_state(editor_);
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_semantics(before_connect);
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_semantics(before_dissolve);
  ASSERT_EQ(editor_.selection().summary().selected_object_count, 2U);
  expect_component_operation_availability(
      quader_command::kModelingDissolveEdges, "dissolve_edges", true);
  ASSERT_FALSE(HasFatalFailure());
  const auto dissolved = editor_.commands().execute(quader_command::kModelingDissolveEdges);
  ASSERT_TRUE(dissolved.ok()) << dissolved.error_message();
  EXPECT_TRUE(dissolved.changed());
  EXPECT_EQ(topology(quad).vertices.size(), 4U);
  EXPECT_EQ(topology(quad).faces.size(), 1U);
  const auto after_dissolve = test::capture_command_observable_state(editor_);
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_semantics(before_dissolve);
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_semantics(after_dissolve);
}

TEST_F(ModelingComponentMergeTests, EdgeConnectAndDissolveRejectTwoComponentParticipants) {
  const auto first = create_quad(0.0F, "First edge participant");
  const auto second = create_quad(5.0F, "Second edge participant");
  for (const auto object : {first, second}) {
    const auto face = topology(object).faces.front();
    ASSERT_TRUE(editor_.selection().select_edge(
        object, face.vertices[0], face.vertices[1],
        object.entity == first.entity ? QuaderSelectionEdit::Replace
                                     : QuaderSelectionEdit::Add).ok());
    ASSERT_TRUE(editor_.selection().select_edge(
        object, face.vertices[2], face.vertices[3], QuaderSelectionEdit::Add).ok());
  }
  ASSERT_EQ(editor_.selection().summary().selected_object_count, 2U);
  ASSERT_EQ(editor_.selection().summary().selected_edge_count, 4U);
  expect_component_operation_availability(
      quader_command::kModelingConnectEdges, "connect_edges", false);
  ASSERT_FALSE(HasFatalFailure());
  const auto before_connect = test::capture_command_observable_state(editor_);
  const auto rejected_connect = editor_.commands().execute(quader_command::kModelingConnectEdges);
  EXPECT_FALSE(rejected_connect.ok());
  EXPECT_FALSE(rejected_connect.changed());
  expect_semantics(before_connect, true);

  std::vector<quader_poly::Edge> interior_edges;
  for (const auto object : {first, second}) {
    const auto face = topology(object).faces.front();
    ASSERT_TRUE(editor_.selection().select_edge(
        object, face.vertices[0], face.vertices[1]).ok());
    ASSERT_TRUE(editor_.selection().select_edge(
        object, face.vertices[2], face.vertices[3], QuaderSelectionEdit::Add).ok());
    const auto connected = editor_.commands().execute(quader_command::kModelingConnectEdges);
    ASSERT_TRUE(connected.ok()) << connected.error_message();
    ASSERT_TRUE(connected.changed());
    const auto incidence = authored_edge_incidence(topology(object));
    const auto interior = std::ranges::find_if(incidence, [](const auto &entry) {
      return entry.second == 2U;
    });
    ASSERT_NE(interior, incidence.end());
    interior_edges.push_back(quader_poly::make_edge(
        interior->first.first, interior->first.second));
  }
  ASSERT_TRUE(editor_.selection().select_edge(
      first, interior_edges[0].a, interior_edges[0].b).ok());
  ASSERT_TRUE(editor_.selection().select_edge(
      second, interior_edges[1].a, interior_edges[1].b, QuaderSelectionEdit::Add).ok());
  ASSERT_EQ(editor_.selection().summary().selected_object_count, 2U);
  ASSERT_EQ(editor_.selection().summary().selected_edge_count, 2U);
  expect_component_operation_availability(
      quader_command::kModelingDissolveEdges, "dissolve_edges", false);
  ASSERT_FALSE(HasFatalFailure());
  const auto before_dissolve = test::capture_command_observable_state(editor_);
  const auto rejected_dissolve = editor_.commands().execute(quader_command::kModelingDissolveEdges);
  EXPECT_FALSE(rejected_dissolve.ok());
  EXPECT_FALSE(rejected_dissolve.changed());
  expect_semantics(before_dissolve, true);
}

TEST_F(ModelingComponentMergeTests, DissolveInlineVertexPreservesComponentlessMeshAndHistory) {
  quader_poly::Document document;
  const auto left = quader_poly::add_vertex(document, {-1.0F, 0.0F, 0.0F});
  const auto middle = quader_poly::add_vertex(document, {0.0F, 0.0F, 0.0F});
  const auto right = quader_poly::add_vertex(document, {1.0F, 0.0F, 0.0F});
  const auto top = quader_poly::add_vertex(document, {0.0F, 1.0F, 0.0F});
  const std::vector<quader_poly::ElementId> vertices{left, middle, right, top};
  ASSERT_NE(quader_poly::add_face(document, vertices), quader_poly::kInvalidElementId);
  const auto created = editor_.modeling().create_mesh(
      QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
      {.name = "Inline Vertex Face"});
  ASSERT_TRUE(created.ok()) << created.error_message();
  ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Vertex).ok());
  ASSERT_TRUE(editor_.selection().select_vertex(created.value(), middle).ok());
  retain_component_object_scope(
      source_, {source_.entity, created.value().entity}, created.value(),
      QuaderComponentKind::Vertex);
  ASSERT_FALSE(HasFatalFailure());
  ASSERT_EQ(editor_.selection().summary().mode, QuaderSelectionMode::Vertex);
  const auto before = test::capture_command_observable_state(editor_);
  const auto dissolved = editor_.commands().execute(quader_command::kModelingDissolveVertices);
  ASSERT_TRUE(dissolved.ok()) << dissolved.error_message();
  EXPECT_TRUE(dissolved.changed());
  EXPECT_EQ(topology(created.value()).vertices.size(), 3U);
  EXPECT_EQ(topology(created.value()).faces.size(), 1U);
  EXPECT_EQ(topology(created.value()).faces.front().vertices.size(), 3U);
  EXPECT_EQ(topology(source_).vertices.size(), 8U);
  EXPECT_EQ(topology(source_).faces.size(), 6U);
  const auto after = test::capture_command_observable_state(editor_);
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_semantics(before);
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_semantics(after);
}

TEST_F(ModelingComponentMergeTests, ExtractedMergeRetainsEachObjectsResolvedFaceMaterials) {
  split_positive_x_face(true);
  ASSERT_FALSE(HasFatalFailure());
  const auto source_material = editor_.assets().add_material(
      QuaderMaterialArgs{.name = "Source Red", .base_color = {1.0F, 0.0F, 0.0F}});
  const auto survivor_material = editor_.assets().add_material(
      QuaderMaterialArgs{.name = "Survivor Blue", .base_color = {0.0F, 0.0F, 1.0F}});
  ASSERT_TRUE(source_material.ok()) << source_material.error_message();
  ASSERT_TRUE(survivor_material.ok()) << survivor_material.error_message();
  ASSERT_TRUE(editor_.object(source_).assign_material(source_material.value().id).ok());
  ASSERT_TRUE(editor_.object(survivor_).assign_material(survivor_material.value().id).ok());
  const auto &scene = project_.active_scene()->session->scene();
  ASSERT_NE(scene.material(source_.entity), nullptr);
  ASSERT_NE(scene.material(survivor_.entity), nullptr);
  const auto source_reference = scene.material(source_.entity)->material_for_slot(0U);
  const auto survivor_reference = scene.material(survivor_.entity)->material_for_slot(0U);
  ASSERT_NE(source_reference, survivor_reference);
  select_boundary_edges(true);
  ASSERT_FALSE(HasFatalFailure());
  const auto before = test::capture_command_observable_state(editor_);
  const auto merged = editor_.commands().execute(quader_command::kModelingMergeEdges);
  ASSERT_TRUE(merged.ok()) << merged.error_message();
  const auto document = topology(survivor_);
  const auto *material = scene.material(survivor_.entity);
  ASSERT_NE(material, nullptr);
  std::size_t source_faces = 0U;
  std::size_t survivor_faces = 0U;
  for (const auto &face : document.faces) {
    const auto resolved = material->material_for_slot(face.material_slot);
    source_faces += resolved == source_reference;
    survivor_faces += resolved == survivor_reference;
  }
  EXPECT_EQ(source_faces, 5U);
  EXPECT_EQ(survivor_faces, 1U);
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_semantics(before);
  EXPECT_EQ(scene.material(source_.entity)->material_for_slot(0U), source_reference);
  EXPECT_EQ(scene.material(survivor_.entity)->material_for_slot(0U), survivor_reference);
}

TEST_F(ModelingComponentMergeTests, InstantCrossObjectFailuresPreserveSelectionAndLeaveNoPreview) {
  split_positive_x_face(true);
  ASSERT_FALSE(HasFatalFailure());
  select_boundary_edges(true);
  ASSERT_FALSE(HasFatalFailure());
  const auto before = test::capture_command_observable_state(editor_);
  auto &modeling = project_.active_scene()->session->modeling();
  for (const auto point : {ModelingSdkProjectionFailurePoint::AfterSdkMutation,
                           ModelingSdkProjectionFailurePoint::BeforeCommit}) {
    SCOPED_TRACE(static_cast<int>(point));
    modeling.set_modeling_sdk_projection_failure_for_tests(point);
    const auto failed = editor_.commands().execute(quader_command::kModelingMergeEdges);
    modeling.set_modeling_sdk_projection_failure_for_tests(ModelingSdkProjectionFailurePoint::None);
    EXPECT_FALSE(failed.ok());
    EXPECT_FALSE(failed.changed());
    EXPECT_FALSE(modeling.operation_preview_active());
    EXPECT_EQ(editor_.query().modeling_objects().size(), 2U);
    expect_semantics(before, true);
  }
  const auto retried = editor_.commands().execute(quader_command::kModelingMergeEdges);
  ASSERT_TRUE(retried.ok()) << retried.error_message();
  EXPECT_TRUE(retried.changed());
  EXPECT_FALSE(modeling.operation_preview_active());
  EXPECT_EQ(editor_.query().modeling_objects().size(), 1U);
}

TEST_F(ModelingComponentMergeTests, SameDirectionEdgeWeldRejectsWithoutCombiningObjectsOrChangingHistory) {
  const auto create_triangle = [&](QVec3 first, QVec3 second, QVec3 third) {
    quader_poly::Document document;
    const std::vector<quader_poly::ElementId> vertices{
        quader_poly::add_vertex(document, first),
        quader_poly::add_vertex(document, second),
        quader_poly::add_vertex(document, third)};
    EXPECT_NE(quader_poly::add_face(document, vertices), quader_poly::kInvalidElementId);
    return editor_.modeling().create_mesh(
        QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
        {.name = "Winding rejection triangle"});
  };
  const auto target = create_triangle({-1.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F});
  const auto source = create_triangle({-1.0F, 2.0F, 0.0F}, {1.0F, 2.0F, 0.0F}, {0.0F, -1.0F, 0.0F});
  ASSERT_TRUE(target.ok()) << target.error_message();
  ASSERT_TRUE(source.ok()) << source.error_message();
  const auto child = editor_.scene().create_entity({.name = "Retained child", .parent = source.value().entity});
  ASSERT_TRUE(child.ok()) << child.error_message();
  ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Edge).ok());
  ASSERT_TRUE(editor_.selection().select_edge(source.value(), 1U, 2U).ok());
  ASSERT_TRUE(editor_.selection().select_edge(target.value(), 1U, 2U, QuaderSelectionEdit::Add).ok());
  const auto before = test::capture_command_observable_state(editor_);

  const auto merged = editor_.commands().execute(quader_command::kModelingMergeEdges);

  EXPECT_FALSE(merged.ok());
  EXPECT_FALSE(merged.changed());
  EXPECT_FALSE(merged.error_message().empty());
  EXPECT_EQ(editor_.query().modeling_objects().size(), 3U);
  const auto retained_child = editor_.query().entity(child.value().id);
  ASSERT_TRUE(retained_child.has_value());
  EXPECT_EQ(retained_child->parent, source.value().entity);
  EXPECT_FALSE(project_.active_scene()->session->modeling().operation_preview_active());
  expect_semantics(before, true);
}

TEST_F(ModelingComponentMergeTests, FailedDistanceUpdateAndCommitRetainAcceptedPreviewAndCancellation) {
  split_positive_x_face(true);
  ASSERT_FALSE(HasFatalFailure());
  select_boundary_vertices(true);
  ASSERT_FALSE(HasFatalFailure());
  const auto before = test::capture_command_observable_state(editor_);
  ASSERT_TRUE(editor_.modeling().begin_preview("merge_by_distance").ok());
  const auto accepted = test::capture_command_observable_state(editor_);
  auto &modeling = project_.active_scene()->session->modeling();
  QuaderPreviewSettings settings;
  settings.interaction.merge_by_distance_tolerance = 0.0;
  modeling.set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::AfterSdkMutation);
  const auto failed_update = editor_.modeling().update_preview(settings);
  modeling.set_modeling_sdk_projection_failure_for_tests(ModelingSdkProjectionFailurePoint::None);
  EXPECT_FALSE(failed_update.ok());
  EXPECT_FALSE(failed_update.changed());
  EXPECT_TRUE(modeling.operation_preview_active());
  expect_semantics(accepted, true);

  modeling.set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::BeforeCommit);
  const auto failed_commit = editor_.modeling().commit_preview("Merge by Distance");
  modeling.set_modeling_sdk_projection_failure_for_tests(ModelingSdkProjectionFailurePoint::None);
  EXPECT_FALSE(failed_commit.ok());
  EXPECT_FALSE(failed_commit.changed());
  EXPECT_TRUE(modeling.operation_preview_active());
  expect_semantics(accepted, true);
  const auto canceled = editor_.modeling().cancel_preview();
  ASSERT_TRUE(canceled.ok()) << canceled.error_message();
  EXPECT_FALSE(modeling.operation_preview_active());
  EXPECT_EQ(editor_.query().modeling_objects().size(), 2U);
  expect_semantics(before, true);
}

TEST_F(ModelingComponentMergeTests, InsertEdgeLoopSplicesNgonCapsAndReplaysOneNativeHistoryStep) {
  const auto source_document = saved_insert_edge_loop_source();
  const auto created = editor_.modeling().create_mesh(
      QuaderPolygonDocumentInternalAccess::from_backend(source_document),
      {.name = "Saved capped quad strip"});
  ASSERT_TRUE(created.ok()) << created.error_message();
  const auto object = created.value();
  ASSERT_TRUE(editor_.object(object).set_transform(
      {.position = {-7.5F, 0.5F, -10.5F}}).ok());
  ASSERT_TRUE(editor_.selection().select_faces(object, {221, 3, 229, 224, 228}).ok());
  ASSERT_TRUE(editor_.tools().activate(ViewportToolId::InsertEdgeLoop).ok());
  auto &session = *project_.active_scene()->session;
  auto &modeling = session.modeling();
  const auto history_count = session.history().undo_count();
  const auto before = test::capture_command_observable_state(editor_);
  const auto original_mesh = editor_.query().compiled_object_mesh(object);
  ASSERT_TRUE(original_mesh.has_value());
  ASSERT_TRUE(original_mesh->has_authored_polygon_document);
  const auto original = topology(object);
  ASSERT_EQ(original.vertices.size(), 34U);
  ASSERT_EQ(original.faces.size(), 27U);
  const auto original_incidence = authored_edge_incidence(original);
  ASSERT_EQ(original_incidence.size(), 59U);
  for (const auto &[edge, count] : original_incidence) {
    SCOPED_TRACE(std::to_string(edge.first) + ":" + std::to_string(edge.second));
    ASSERT_EQ(count, 2U);
  }

  ModelingToolRayEvent event{
      .ray = {.origin = {-9.0F, 8.0F, -62.0F}, .direction = {1.0F, 0.0F, 0.0F}},
      .camera = {.orthographic = true,
                 .origin = {-9.0F, 8.0F, -62.0F},
                 .x_axis = {0.0F, 0.0F, -1.0F},
                 .y_axis = {0.0F, 1.0F, 0.0F},
                 .z_axis = {-1.0F, 0.0F, 0.0F},
                 .viewport_size = {800.0F, 600.0F}},
      .snap_enabled = false,
  };
  const auto hover = modeling.update_insert_edge_loop_hover(event);
  ASSERT_TRUE(hover.ok) << hover.message;
  ASSERT_TRUE(modeling.tool_status().insert_edge_loop_has_preview);
  const auto first_drag = modeling.begin_insert_edge_loop_drag(event);
  ASSERT_TRUE(first_drag.ok) << first_drag.message;
  ASSERT_TRUE(modeling.tool_status().insert_edge_loop_dragging);
  // The ray follows vertex 236 -> 235 at 11/18 of its bottom-to-top length.
  // The canonical edge reverses that orientation, so its factor is 7/18.
  event.ray.origin.y = 11.0F;
  const auto updated = modeling.update_insert_edge_loop_drag(event);
  ASSERT_TRUE(updated.ok) << updated.message;
  ASSERT_TRUE(modeling.tool_status().insert_edge_loop_has_preview);
  EXPECT_EQ(topology(object).vertices.size(), 34U);
  EXPECT_EQ(session.history().undo_count(), history_count);
  const auto canceled = editor_.tools().cancel_modal_tool();
  ASSERT_TRUE(canceled.ok()) << canceled.error_message();
  EXPECT_FALSE(modeling.tool_status().insert_edge_loop_has_preview);
  EXPECT_FALSE(modeling.tool_status().insert_edge_loop_dragging);
  EXPECT_EQ(editor_.selection().hover_summary().target.kind,
            QuaderActiveSelectionKind::Edge);
  // Cancel returns to Select and refreshes the edge under the pointer. Leaving
  // the viewport clears that transient hover before comparing authored selection.
  ASSERT_TRUE(editor_.selection().clear_hover().ok());
  EXPECT_EQ(editor_.selection().hover_summary().target.kind,
            QuaderActiveSelectionKind::None);
  {
    SCOPED_TRACE("Canceled first InsertEdgeLoop drag");
    expect_semantics(before, true);
  }
  const auto canceled_mesh = editor_.query().compiled_object_mesh(object);
  ASSERT_TRUE(canceled_mesh.has_value());
  EXPECT_EQ(canceled_mesh->revision, original_mesh->revision);

  ASSERT_TRUE(editor_.tools().activate(ViewportToolId::InsertEdgeLoop).ok());
  const auto restarted = modeling.begin_insert_edge_loop_drag(event);
  ASSERT_TRUE(restarted.ok) << restarted.message;
  ASSERT_TRUE(modeling.tool_status().insert_edge_loop_has_preview);
  const auto committed = editor_.tools().commit_modeling_tool(ViewportToolId::InsertEdgeLoop);
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  ASSERT_TRUE(committed.changed());
  EXPECT_FALSE(modeling.tool_status().insert_edge_loop_has_preview);
  EXPECT_FALSE(modeling.tool_status().insert_edge_loop_dragging);
  EXPECT_EQ(session.history().undo_count(), history_count + 1U);

  const auto expect_published_cut = [&] {
    const auto document = topology(object);
    ASSERT_EQ(document.vertices.size(), 40U);
    ASSERT_EQ(document.faces.size(), 32U);
    const auto incidence = authored_edge_incidence(document);
    EXPECT_EQ(incidence.size(), 70U);
    for (const auto &[edge, count] : incidence) {
      SCOPED_TRACE(std::to_string(edge.first) + ":" + std::to_string(edge.second));
      EXPECT_EQ(count, 2U);
    }
    EXPECT_FALSE(incidence.contains({226, 227}));
    EXPECT_FALSE(incidence.contains({229, 230}));
    const auto mesh = editor_.query().compiled_object_mesh(object);
    ASSERT_TRUE(mesh.has_value());
    EXPECT_TRUE(mesh->has_authored_polygon_document);
    EXPECT_GT(mesh->indices.size(), original_mesh->indices.size());
    const auto world = session.scene().world_transform_matrix(object.entity);
    for (const auto cap : {quader_poly::ElementId{1}, quader_poly::ElementId{2}}) {
      SCOPED_TRACE(cap);
      const auto *face = quader_poly::find_face(document, cap);
      ASSERT_NE(face, nullptr);
      ASSERT_EQ(face->vertices.size(), 6U);
      const float local_z = cap == 1 ? 26.499998092651367F : -71.5F;
      const auto endpoint = std::ranges::find_if(document.vertices, [&](const auto &vertex) {
        return std::abs(vertex.position.x + 0.5F) < 0.0001F &&
               std::abs(vertex.position.y - 10.5F) < 0.0001F &&
               std::abs(vertex.position.z - local_z) < 0.0001F;
      });
      ASSERT_NE(endpoint, document.vertices.end());
      EXPECT_NE(std::ranges::find(face->vertices, endpoint->id), face->vertices.end());
      const auto position = transform_point(world, endpoint->position);
      EXPECT_NEAR(position.x, -8.0F, 0.0001F);
      EXPECT_NEAR(position.y, 11.0F, 0.0001F);
      EXPECT_NEAR(position.z, local_z - 10.5F, 0.0001F);
      EXPECT_TRUE(std::ranges::any_of(mesh->vertices, [&](const auto &vertex) {
        const auto projected_position = transform_point(world, vertex.position);
        return std::abs(projected_position.x - position.x) < 0.0001F &&
               std::abs(projected_position.y - position.y) < 0.0001F &&
               std::abs(projected_position.z - position.z) < 0.0001F;
      }));
    }
    const auto *unrelated_face = quader_poly::find_face(document, 8);
    ASSERT_NE(unrelated_face, nullptr);
    EXPECT_EQ(unrelated_face->vertices, quader_poly::find_face(original, 8)->vertices);
    EXPECT_EQ(unrelated_face->material_slot, 0U);
    EXPECT_EQ(unrelated_face->normal_shading, quader_poly::SurfaceShading::Authored);
    const auto *unrelated_vertex = quader_poly::find_vertex(document, 89);
    ASSERT_NE(unrelated_vertex, nullptr);
    EXPECT_TRUE(same_position(unrelated_vertex->position,
                             quader_poly::find_vertex(original, 89)->position));
    EXPECT_EQ(topology(source_).vertices.size(), 8U);
    EXPECT_EQ(topology(source_).faces.size(), 6U);
  };
  expect_published_cut();
  ASSERT_FALSE(HasFatalFailure());
  const auto accepted = test::capture_command_observable_state(editor_);
  const auto empty_commit = editor_.tools().commit_modeling_tool(ViewportToolId::InsertEdgeLoop);
  EXPECT_FALSE(empty_commit.changed());
  EXPECT_EQ(session.history().undo_count(), history_count + 1U);
  ASSERT_TRUE(editor_.history().undo().ok());
  EXPECT_EQ(session.history().undo_count(), history_count);
  EXPECT_EQ(session.history().redo_count(), 1U);
  {
    SCOPED_TRACE("Undone InsertEdgeLoop commit");
    expect_semantics(before);
  }
  EXPECT_EQ(topology(object).vertices.size(), 34U);
  EXPECT_EQ(authored_edge_incidence(topology(object)), original_incidence);
  const auto undone_mesh = editor_.query().compiled_object_mesh(object);
  ASSERT_TRUE(undone_mesh.has_value());
  EXPECT_EQ(undone_mesh->indices.size(), original_mesh->indices.size());
  ASSERT_TRUE(editor_.history().redo().ok());
  EXPECT_EQ(session.history().undo_count(), history_count + 1U);
  EXPECT_EQ(session.history().redo_count(), 0U);
  {
    SCOPED_TRACE("Redone InsertEdgeLoop commit");
    expect_semantics(accepted);
  }
  expect_published_cut();
}

} // namespace
} // namespace quader

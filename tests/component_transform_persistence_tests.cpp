////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "api/quader_polygon_document_internal_access.hpp"
#include "assets/mesh_asset_payload.hpp"
#include "assets/mesh_geometry_projection.hpp"
#include "editor/session/project_session.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/projection/mesh_projection_service.hpp"
#include "editor/render/quader_render_api_runtime_access.hpp"
#include "render/scene_render_bridge.hpp"
#include "render/scene_render_extraction.hpp"
#include "scene/scene_transform.hpp"
#include "support/command_observable_state.hpp"

#include <quader/modeling/neutral/polygon/document.hpp>
#include <quader/modeling/neutral/polygon/document_validation.hpp>

#include <gtest/gtest.h>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace quader {
namespace {

QuaderTransformDragBeginRequest component_move_request() {
  return {
      .tool = QuaderTransformTool::Move, .axis = QuaderTransformAxis::X,
      .screen_position = {100, 100}, .screen_pivot = {100, 100},
      .screen_tip = {120, 100}, .has_world_direction = true,
      .world_direction = {1, 0, 0},
      .ray = {.origin = {0, 0, 5}, .direction = {0, 0, -1}},
      .camera = {.orthographic = true, .origin = {0, 0, 5},
                 .x_axis = {1, 0, 0}, .y_axis = {0, 1, 0}, .z_axis = {0, 0, 1},
                 .viewport_size = {800, 600}, .orthographic_size = 8},
      .vertex_snap_radius = 0, .grid_snap_enabled = false};
}

void expect_same_authored_topology(const quader_poly::Document &actual,
                                  const quader_poly::Document &expected) {
  EXPECT_EQ(actual.next_vertex_id, expected.next_vertex_id);
  EXPECT_EQ(actual.next_face_id, expected.next_face_id);
  EXPECT_EQ(actual.hard_normal_edges, expected.hard_normal_edges);
  EXPECT_EQ(actual.soft_normal_edges, expected.soft_normal_edges);
  ASSERT_EQ(actual.vertices.size(), expected.vertices.size());
  ASSERT_EQ(actual.faces.size(), expected.faces.size());
  for (std::size_t index = 0; index < actual.vertices.size(); ++index) {
    const auto &left = actual.vertices[index];
    const auto &right = expected.vertices[index];
    EXPECT_EQ(left.id, right.id);
    EXPECT_EQ(left.position.x, right.position.x);
    EXPECT_EQ(left.position.y, right.position.y);
    EXPECT_EQ(left.position.z, right.position.z);
  }
  for (std::size_t index = 0; index < actual.faces.size(); ++index) {
    const auto &left = actual.faces[index];
    const auto &right = expected.faces[index];
    EXPECT_EQ(left.id, right.id);
    EXPECT_EQ(left.vertices, right.vertices);
    EXPECT_EQ(left.material_slot, right.material_slot);
    EXPECT_EQ(left.normal_shading, right.normal_shading);
    EXPECT_EQ(left.texture_transform, right.texture_transform);
    ASSERT_EQ(left.uvs.size(), right.uvs.size());
    for (std::size_t corner = 0; corner < left.uvs.size(); ++corner) {
      EXPECT_EQ(left.uvs[corner].x, right.uvs[corner].x);
      EXPECT_EQ(left.uvs[corner].y, right.uvs[corner].y);
    }
  }
}

void expect_restored_extrusion_topology(const quader_poly::Document &actual,
                                       const quader_poly::Document &expected,
                                       const quader_poly::Document &allocation_floor) {
  EXPECT_GE(actual.next_vertex_id, allocation_floor.next_vertex_id);
  EXPECT_GE(actual.next_face_id, allocation_floor.next_face_id);
  auto restored = expected;
  // Undo restores authored elements while retaining this record's allocated IDs.
  restored.next_vertex_id = actual.next_vertex_id;
  restored.next_face_id = actual.next_face_id;
  expect_same_authored_topology(actual, restored);
}

template <typename Preview>
std::shared_ptr<const MeshGeometryPayload>
effective_preview_geometry(const Preview &preview) {
  return preview.geometry_projection
      ? preview.geometry_projection->materialized_geometry()
      : preview.mesh.geometry_payload();
}

void expect_preview_matches_document(const ModelingToolPreviewMesh &preview,
                                     const quader_poly::Document &expected) {
  const auto document =
      MeshProjectionService::sdk_document_from_polygon_document(expected);
  ASSERT_TRUE(document.ok()) << document.error_message();
  const auto canonical =
      MeshProjectionService::compile_sdk_document_mesh(document.value());
  ASSERT_TRUE(canonical.has_value());
  const auto geometry = effective_preview_geometry(preview);
  ASSERT_NE(geometry, nullptr);
  EXPECT_TRUE(mesh_geometry_payload_equal(*geometry, canonical->geometry()));
  if (preview.dynamic_position_payload) {
    ASSERT_EQ(preview.dynamic_position_payload->size(), geometry->vertices.size());
    for (std::size_t index = 0U; index < geometry->vertices.size(); ++index) {
      const auto &position = (*preview.dynamic_position_payload)[index];
      EXPECT_EQ(position.x, geometry->vertices[index].position.x);
      EXPECT_EQ(position.y, geometry->vertices[index].position.y);
      EXPECT_EQ(position.z, geometry->vertices[index].position.z);
    }
  }
}

class ComponentTransformPersistenceTests : public ::testing::Test {
protected:
  ComponentTransformPersistenceTests()
      : root_(std::filesystem::temp_directory_path() /
              ("quader-component-persistence-" +
               std::string(ProjectId::generate().str()))),
        editor_(project_) {}

  void SetUp() override {
    std::filesystem::create_directory(root_);
    const auto created = editor_.project().create(root_);
    ASSERT_TRUE(created.ok()) << created.error_message();
    const auto box = editor_.modeling().create_box(
        {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        "Editable Box");
    ASSERT_TRUE(box.ok()) << box.error_message();
    object_ = box.value();
  }

  void TearDown() override {
    std::error_code ignored;
    std::filesystem::remove_all(root_, ignored);
  }

  quader_poly::Document topology() const {
    const auto objects = editor_.query().modeling_objects();
    EXPECT_EQ(objects.size(), 1U);
    if (objects.size() != 1U) {
      return {};
    }
    const auto *mesh =
        project_.active_scene()->session->assets().mesh(objects[0].mesh);
    EXPECT_NE(mesh, nullptr);
    if (mesh == nullptr || !mesh->authored_polygon_document) {
      ADD_FAILURE() << "The authored polygon document must survive projection.";
      return {};
    }
    return *mesh->authored_polygon_document;
  }

  void enter_edit_for_all_meshes() {
    if (editor_.mode().current() == QuaderEditorMode::Object) {
      ASSERT_TRUE(editor_.selection().select_all().ok());
      ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Edit).ok());
    }
  }

  void select_positive_x_face() {
    const auto document = topology();
    const auto face = std::ranges::find_if(document.faces, [&](const auto &face) {
      return std::ranges::all_of(face.vertices, [&](const auto id) {
        return quader_poly::find_vertex(document, id)->position.x == 1.0f;
      });
    });
    ASSERT_NE(face, document.faces.end());
    enter_edit_for_all_meshes();
    ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Face).ok());
    ASSERT_TRUE(editor_.selection()
                    .select_face(object_, static_cast<QuaderElementId>(face->id))
                    .ok());
  }

  void verify_history_and_roundtrip(const quader_poly::Document &before,
                                   const quader_poly::Document &after) {
    const auto selection = editor_.selection().summary();
    ASSERT_TRUE(editor_.history().undo().ok());
    expect_same_authored_topology(topology(), before);
    ASSERT_TRUE(editor_.history().redo().ok());
    expect_same_authored_topology(topology(), after);
    const auto restored_selection = editor_.selection().summary();
    EXPECT_EQ(restored_selection.mode, selection.mode);
    EXPECT_EQ(restored_selection.selected_face_count, selection.selected_face_count);
    EXPECT_EQ(restored_selection.selected_edge_count, selection.selected_edge_count);
    EXPECT_EQ(restored_selection.selected_vertex_count, selection.selected_vertex_count);
    EXPECT_EQ(restored_selection.active_face_id, selection.active_face_id);

    const auto path = ProjectRelativePath::parse_portable("Scenes/Component Edit.qdr");
    ASSERT_TRUE(path.ok());
    const auto saved = editor_.scene_documents().save_as(
        project_.active_scene_id(), path.value());
    ASSERT_TRUE(saved.ok()) << saved.error_message();
    EXPECT_FALSE(editor_.scene_documents().active_scene()->dirty);
    ASSERT_TRUE(editor_.scene_documents().close_active().ok());
    const auto opened = editor_.scene_documents().open(path.value());
    ASSERT_TRUE(opened.ok()) << opened.error_message();
    expect_same_authored_topology(topology(), after);
    const auto objects = editor_.query().modeling_objects();
    ASSERT_EQ(objects.size(), 1U);
    const auto rebound = editor_.query().modeling_object_for_id(objects[0].modeling_object_id);
    ASSERT_TRUE(rebound.has_value());
    object_ = *rebound;
    // Rehydration must retain an editable SDK binding even with no visible triangles.
    enter_edit_for_all_meshes();
    ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Vertex).ok());
    ASSERT_TRUE(editor_.selection()
                    .select_vertex(object_, static_cast<QuaderElementId>(after.vertices[0].id))
                    .ok());
    ASSERT_TRUE(editor_.modeling().move_selection({0.25f, 0.0f, 0.0f}).ok());
    ASSERT_TRUE(editor_.history().undo().ok());
    expect_same_authored_topology(topology(), after);
    const auto saved_again = editor_.scene_documents().save_active();
    ASSERT_TRUE(saved_again.ok()) << saved_again.error_message();
    ASSERT_TRUE(editor_.scene_documents().close_active().ok());
    ASSERT_TRUE(editor_.scene_documents().open(path.value()).ok());
    expect_same_authored_topology(topology(), after);
  }

  std::filesystem::path root_;
  ProjectSession project_;
  QuaderEditorApi editor_;
  QuaderModelingObjectRef object_;
};

TEST_F(ComponentTransformPersistenceTests, FaceMovedOntoOppositeFaceRemainsSaveable) {
  select_positive_x_face();
  const auto before = topology();
  const auto moved = editor_.modeling().move_selection({-2.0f, 0.0f, 0.0f});
  ASSERT_TRUE(moved.ok()) << moved.error_message();
  ASSERT_TRUE(moved.changed());
  const auto after = topology();
  ASSERT_EQ(after.faces.size(), 6U);
  EXPECT_FALSE(quader_poly::validate_polygon_document(after).ok());
  verify_history_and_roundtrip(before, after);
}

class ComponentNudgePersistenceTests
    : public ComponentTransformPersistenceTests,
      public ::testing::WithParamInterface<QuaderSelectionMode> {};

TEST_P(ComponentNudgePersistenceTests,
       WorldMoveAndSharedLocalRotationRetainParticipantFramesAndSparseHistory) {
  const auto second = editor_.modeling().create_box(
      {.min = {-1, -1, -1}, .max = {1, 1, 1}}, "Second nudge participant");
  ASSERT_TRUE(second.ok()) << second.error_message();
  const std::array objects{object_, second.value()};
  ASSERT_TRUE(editor_.scene().set_transform(objects[0].entity,
      {.position = {10, 20, -5}, .rotation_euler = {0.2F, 0.4F, 0.6F},
       .scale = {2, 0.5F, 3}}).ok());
  ASSERT_TRUE(editor_.scene().set_transform(objects[1].entity,
      {.position = {-4, 2, 7}, .rotation_euler = {-0.2F, -0.6F, 0.8F},
       .scale = {-1, 2, 0.75F}}).ok());
  auto &session = *project_.active_scene()->session;
  TransformComponent sheared = *session.scene().transform(objects[1].entity);
  sheared.parenting_offset.values[4] = 0.35F;
  ASSERT_TRUE(session.scene().set_transform(objects[1].entity, sheared));
  const auto authored = [&](QuaderModelingObjectRef object) {
    const auto *binding = session.scene().mesh(object.entity);
    const auto *mesh = binding ? session.assets().mesh(binding->mesh) : nullptr;
    EXPECT_TRUE(mesh && mesh->authored_polygon_document);
    return mesh && mesh->authored_polygon_document
        ? *mesh->authored_polygon_document : quader_poly::Document{};
  };
  const std::array before{authored(objects[0]), authored(objects[1])};
  const std::array worlds{session.scene().world_transform_matrix(objects[0].entity),
                          session.scene().world_transform_matrix(objects[1].entity)};
  enter_edit_for_all_meshes();
  ASSERT_TRUE(editor_.selection().set_mode(GetParam()).ok());
  ASSERT_TRUE(editor_.selection().select_all().ok());
  ASSERT_TRUE(editor_.tools().activate(ViewportToolId::Scale).ok());
  ASSERT_TRUE(editor_.modeling().can_nudge_selection());
  const auto availability_builds = session.modeling().nudge_selection_eligibility_build_count();
  const auto projection_before = session.modeling().modeling_sdk_projection_stats_for_tests();
  for (int query = 0; query < 24; ++query) {
    EXPECT_TRUE(editor_.modeling().can_nudge_selection());
  }
  EXPECT_EQ(session.modeling().nudge_selection_eligibility_build_count(), availability_builds);
  EXPECT_EQ(session.modeling().modeling_sdk_projection_stats_for_tests().selection_topology_build_count,
            projection_before.selection_topology_build_count);
  const auto undo_before = session.history().undo_count();
  const auto selection_before = editor_.selection().summary();
  const QuaderResolvedNudgeRequest move{
      .tool = QuaderTransformTool::Move, .axis = QuaderTransformAxis::X,
      .move_delta = {1.5F, -0.5F, 2}};
  const auto moved = editor_.modeling().nudge_selection(move);
  ASSERT_TRUE(moved.ok()) << moved.error_message();
  ASSERT_TRUE(moved.changed());
  EXPECT_EQ(session.history().undo_count(), undo_before + 1U);
  const std::array after_move{authored(objects[0]), authored(objects[1])};
  for (std::size_t index = 0; index < objects.size(); ++index) {
    ASSERT_EQ(after_move[index].vertices.size(), before[index].vertices.size());
    for (std::size_t vertex = 0; vertex < before[index].vertices.size(); ++vertex) {
      const QVec3 actual = transform_point(worlds[index], after_move[index].vertices[vertex].position);
      const QVec3 expected = transform_point(worlds[index], before[index].vertices[vertex].position) + move.move_delta;
      EXPECT_NEAR(actual.x, expected.x, 0.00002F);
      EXPECT_NEAR(actual.y, expected.y, 0.00002F);
      EXPECT_NEAR(actual.z, expected.z, 0.00002F);
    }
  }

  const QuaderResolvedNudgeRequest rotate{
      .tool = QuaderTransformTool::Rotate, .axis = QuaderTransformAxis::X,
      .space = QuaderTransformSpace::Local,
      .orientation = {.x_axis = {0, 1, 0}, .y_axis = {-1, 0, 0}, .z_axis = {0, 0, 1}},
      .pivot = {3, -2, 4}, .rotate_angle_degrees = 45};
  const auto expect_rotated_world = [&](int count) {
    const double angle = count * std::acos(-1.0) / 4.0;
    const float cosine = static_cast<float>(std::cos(angle));
    const float sine = static_cast<float>(std::sin(angle));
    for (std::size_t index = 0; index < objects.size(); ++index) {
      const auto actual_document = authored(objects[index]);
      ASSERT_EQ(actual_document.vertices.size(), after_move[index].vertices.size());
      EXPECT_EQ(session.scene().world_transform_matrix(objects[index].entity).values,
                worlds[index].values);
      for (std::size_t vertex = 0; vertex < actual_document.vertices.size(); ++vertex) {
        const QVec3 offset = transform_point(worlds[index], after_move[index].vertices[vertex].position) - rotate.pivot;
        const QVec3 expected = rotate.pivot + QVec3{
            cosine * offset.x + sine * offset.z, offset.y,
            -sine * offset.x + cosine * offset.z};
        const QVec3 actual = transform_point(worlds[index], actual_document.vertices[vertex].position);
        EXPECT_NEAR(actual.x, expected.x, 0.00005F);
        EXPECT_NEAR(actual.y, expected.y, 0.00005F);
        EXPECT_NEAR(actual.z, expected.z, 0.00005F);
      }
    }
  };
  const auto rotated = editor_.modeling().nudge_selection(rotate);
  ASSERT_TRUE(rotated.ok()) << rotated.error_message();
  ASSERT_TRUE(rotated.changed());
  EXPECT_EQ(session.history().undo_count(), undo_before + 2U);
  expect_rotated_world(1);
  const std::array after_rotate{authored(objects[0]), authored(objects[1])};
  const auto repeated = editor_.history().repeat_last_action();
  ASSERT_TRUE(repeated.ok()) << repeated.error_message();
  ASSERT_TRUE(repeated.changed());
  EXPECT_EQ(session.history().undo_count(), undo_before + 3U);
  expect_rotated_world(2);
  EXPECT_EQ(editor_.tools().active_tool(), "scale");
  EXPECT_FALSE(session.modeling().transient_transform_preview_active());
  EXPECT_TRUE(session.modeling().tool_preview_meshes().empty());
  const auto selection_after = editor_.selection().summary();
  EXPECT_EQ(selection_after.mode, selection_before.mode);
  EXPECT_EQ(selection_after.selected_vertex_count, selection_before.selected_vertex_count);
  EXPECT_EQ(selection_after.selected_edge_count, selection_before.selected_edge_count);
  EXPECT_EQ(selection_after.selected_face_count, selection_before.selected_face_count);
  ASSERT_TRUE(editor_.history().undo().ok());
  for (std::size_t index = 0; index < objects.size(); ++index) {
    expect_same_authored_topology(authored(objects[index]), after_rotate[index]);
  }
  ASSERT_TRUE(editor_.history().undo().ok());
  for (std::size_t index = 0; index < objects.size(); ++index) {
    expect_same_authored_topology(authored(objects[index]), after_move[index]);
  }
  ASSERT_TRUE(editor_.history().undo().ok());
  for (std::size_t index = 0; index < objects.size(); ++index) {
    expect_same_authored_topology(authored(objects[index]), before[index]);
  }
  ASSERT_TRUE(editor_.history().redo().ok());
  ASSERT_TRUE(editor_.history().redo().ok());
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_rotated_world(2);
}

INSTANTIATE_TEST_SUITE_P(ComponentModes, ComponentNudgePersistenceTests,
                        ::testing::Values(QuaderSelectionMode::Vertex,
                                          QuaderSelectionMode::Edge,
                                          QuaderSelectionMode::Face));

TEST_F(ComponentTransformPersistenceTests,
       RejectedNudgeCollapsingFacePreservesRedoRepeatAndCommittedProjection) {
  select_positive_x_face();
  const auto before = topology();
  auto &session = *project_.active_scene()->session;
  const QuaderResolvedNudgeRequest accepted{
      .tool = QuaderTransformTool::Move, .axis = QuaderTransformAxis::X,
      .move_delta = {0.25F, 0, 0}};
  ASSERT_TRUE(editor_.modeling().nudge_selection(accepted).ok());
  const auto after = topology();
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_same_authored_topology(topology(), before);
  const auto undo_before = session.history().undo_count();
  const auto redo_before = session.history().redo_count();
  const auto selection_before = editor_.selection().summary();
  const auto content_before = session.modeling().content_revision();
  const auto rejected = editor_.modeling().nudge_selection({
      .tool = QuaderTransformTool::Move, .axis = QuaderTransformAxis::X,
      .move_delta = {-2, 0, 0}});
  EXPECT_FALSE(rejected.ok());
  EXPECT_FALSE(rejected.changed());
  EXPECT_FALSE(session.modeling().transform_drag_active());
  EXPECT_FALSE(session.modeling().transient_transform_preview_active());
  EXPECT_TRUE(session.modeling().tool_preview_meshes().empty());
  EXPECT_EQ(session.history().undo_count(), undo_before);
  EXPECT_EQ(session.history().redo_count(), redo_before);
  EXPECT_EQ(session.modeling().content_revision(), content_before);
  EXPECT_EQ(editor_.selection().summary().active_face_id, selection_before.active_face_id);
  expect_same_authored_topology(topology(), before);
  const auto repeated = editor_.history().repeat_last_action();
  ASSERT_TRUE(repeated.ok()) << repeated.error_message();
  ASSERT_TRUE(repeated.changed());
  expect_same_authored_topology(topology(), after);
  EXPECT_EQ(session.history().undo_count(), undo_before + 1U);
}

class ComponentExtrudeNudgeTests
    : public ComponentTransformPersistenceTests,
      public ::testing::WithParamInterface<std::tuple<QuaderSelectionMode, QuaderTransformTool>> {
protected:
  void SetUp() override {
    ComponentTransformPersistenceTests::SetUp();
    if (HasFatalFailure()) return;
    ASSERT_TRUE(editor_.scene().destroy(object_.entity).ok());
    quader_poly::Document document;
    const std::array vertices{
        quader_poly::add_vertex(document, {1, -1, -1}),
        quader_poly::add_vertex(document, {1, 1, -1}),
        quader_poly::add_vertex(document, {1, 1, 1}),
        quader_poly::add_vertex(document, {1, -1, 1})};
    const auto face = quader_poly::add_face(document, vertices);
    const auto created = editor_.modeling().create_mesh(
        QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
        {.name = "Extrusion Nudge Quad"});
    ASSERT_TRUE(created.ok()) << created.error_message();
    object_ = created.value();
    ASSERT_TRUE(editor_.scene().set_transform(object_.entity,
        {.position = {10, -3, 4}, .rotation_euler = {0.2F, 0.4F, 0.6F},
         .scale = {2, 0.5F, 1.5F}}).ok());
    enter_edit_for_all_meshes();
    const auto mode = std::get<0>(GetParam());
    ASSERT_TRUE(editor_.selection().set_mode(mode).ok());
    if (mode == QuaderSelectionMode::Face) {
      ASSERT_TRUE(editor_.selection().select_face(object_, static_cast<QuaderElementId>(face)).ok());
    } else {
      ASSERT_TRUE(editor_.selection().select_edge(object_,
          static_cast<QuaderElementId>(vertices[0]),
          static_cast<QuaderElementId>(vertices[1])).ok());
    }
    ASSERT_TRUE(editor_.tools().activate(ViewportToolId::Scale).ok());
  }

  QuaderResolvedNudgeRequest request() const {
    const auto tool = std::get<1>(GetParam());
    return {.tool = tool, .axis = QuaderTransformAxis::Z,
            .pivot = {8, -4, 2},
            .move_delta = tool == QuaderTransformTool::Move ? QVec3{0.75F, 0.25F, -0.5F} : QVec3{},
            .rotate_angle_degrees = tool == QuaderTransformTool::Rotate ? 30.0 : 0.0,
            .duplicate_selected = true};
  }

  quader_poly::Selection component_selection() const {
    const auto exported = project_.active_scene()->session->modeling().export_selection();
    EXPECT_EQ(exported.objects.size(), 1U);
    return exported.objects.size() == 1U ? exported.objects.front().selection
                                         : quader_poly::Selection{};
  }

  static auto selection_state(const quader_poly::Selection &selection) {
    return std::tuple{selection.mode, selection.vertices, selection.edges,
        selection.faces, selection.has_active, selection.active_kind,
        selection.active_vertex, selection.active_edge, selection.active_face};
  }

  auto selection_state() const {
    return selection_state(component_selection());
  }

  void expect_fresh_repeat(const quader_poly::Document &before,
                           const quader_poly::Document &accepted,
                           const quader_poly::Selection &accepted_selection) const {
    auto normalized = topology();
    ASSERT_EQ(normalized.vertices.size(), accepted.vertices.size());
    ASSERT_EQ(normalized.faces.size(), accepted.faces.size());
    EXPECT_GT(normalized.next_vertex_id, accepted.next_vertex_id);
    EXPECT_GT(normalized.next_face_id, accepted.next_face_id);
    std::unordered_map<quader_poly::ElementId, quader_poly::ElementId> vertex_ids;
    std::unordered_map<quader_poly::ElementId, quader_poly::ElementId> face_ids;
    for (auto &vertex : normalized.vertices) {
      const auto match = std::ranges::find_if(accepted.vertices, [&](const auto &candidate) {
        return candidate.position.x == vertex.position.x &&
               candidate.position.y == vertex.position.y &&
               candidate.position.z == vertex.position.z;
      });
      ASSERT_NE(match, accepted.vertices.end());
      if (quader_poly::find_vertex(before, match->id) != nullptr) {
        EXPECT_EQ(vertex.id, match->id);
      } else {
        EXPECT_GE(vertex.id, accepted.next_vertex_id);
        EXPECT_EQ(quader_poly::find_vertex(accepted, vertex.id), nullptr);
      }
      ASSERT_TRUE(vertex_ids.emplace(vertex.id, match->id).second);
      vertex.id = match->id;
    }
    for (auto &face : normalized.faces) {
      for (auto &id : face.vertices) id = vertex_ids.at(id);
      const auto match = std::ranges::find_if(accepted.faces, [&](const auto &candidate) {
        return candidate.vertices == face.vertices;
      });
      ASSERT_NE(match, accepted.faces.end());
      if (quader_poly::find_face(before, match->id) != nullptr) {
        EXPECT_EQ(face.id, match->id);
      } else {
        EXPECT_GE(face.id, accepted.next_face_id);
        EXPECT_EQ(quader_poly::find_face(accepted, face.id), nullptr);
      }
      ASSERT_TRUE(face_ids.emplace(face.id, match->id).second);
      face.id = match->id;
    }
    const auto remap_edge = [&](quader_poly::Edge edge) {
      return quader_poly::Edge{vertex_ids.at(edge.a), vertex_ids.at(edge.b)};
    };
    for (auto &edge : normalized.hard_normal_edges) edge = remap_edge(edge);
    for (auto &edge : normalized.soft_normal_edges) edge = remap_edge(edge);
    normalized.next_vertex_id = accepted.next_vertex_id;
    normalized.next_face_id = accepted.next_face_id;
    auto expected = accepted;
    const auto by_id = [](const auto &left, const auto &right) { return left.id < right.id; };
    std::ranges::sort(normalized.vertices, by_id);
    std::ranges::sort(expected.vertices, by_id);
    std::ranges::sort(normalized.faces, by_id);
    std::ranges::sort(expected.faces, by_id);
    expect_same_authored_topology(normalized, expected);

    auto selection = component_selection();
    for (auto &id : selection.vertices) id = vertex_ids.at(id);
    for (auto &edge : selection.edges) edge = remap_edge(edge);
    for (auto &id : selection.faces) id = face_ids.at(id);
    if (selection.active_vertex != quader_poly::kInvalidElementId) {
      selection.active_vertex = vertex_ids.at(selection.active_vertex);
    }
    if (selection.active_edge.a != quader_poly::kInvalidElementId) {
      selection.active_edge = remap_edge(selection.active_edge);
    }
    if (selection.active_face != quader_poly::kInvalidElementId) {
      selection.active_face = face_ids.at(selection.active_face);
    }
    EXPECT_EQ(selection_state(selection), selection_state(accepted_selection));
  }

  std::vector<QVec3> selected_world_positions() const {
    const auto document = topology();
    const auto selection = component_selection();
    std::vector<quader_poly::ElementId> vertices;
    if (std::get<0>(GetParam()) == QuaderSelectionMode::Face) {
      EXPECT_EQ(selection.faces.size(), 1U);
      if (selection.faces.size() == 1U) {
        const auto *face = quader_poly::find_face(document, selection.faces.front());
        EXPECT_NE(face, nullptr);
        if (face != nullptr) vertices = face->vertices;
      }
    } else {
      EXPECT_EQ(selection.edges.size(), 1U);
      if (selection.edges.size() == 1U) {
        vertices = {selection.edges.front().a, selection.edges.front().b};
      }
    }
    const auto world = project_.active_scene()->session->scene().world_transform_matrix(object_.entity);
    std::vector<QVec3> positions;
    for (const auto id : vertices) {
      const auto *vertex = quader_poly::find_vertex(document, id);
      EXPECT_NE(vertex, nullptr);
      if (vertex != nullptr) positions.push_back(transform_point(world, vertex->position));
    }
    return positions;
  }

  void expect_one_extrusion(const quader_poly::Document &before,
                            const std::vector<QVec3> &selected_before) const {
    const auto after = topology();
    const bool face = std::get<0>(GetParam()) == QuaderSelectionMode::Face;
    EXPECT_EQ(after.vertices.size(), before.vertices.size() + (face ? 4U : 2U));
    EXPECT_EQ(after.faces.size(), before.faces.size() + (face ? 4U : 1U));
    for (const auto &vertex : before.vertices) {
      const auto *retained = quader_poly::find_vertex(after, vertex.id);
      ASSERT_NE(retained, nullptr);
      EXPECT_FLOAT_EQ(retained->position.x, vertex.position.x);
      EXPECT_FLOAT_EQ(retained->position.y, vertex.position.y);
      EXPECT_FLOAT_EQ(retained->position.z, vertex.position.z);
    }
    const auto actual = selected_world_positions();
    ASSERT_EQ(actual.size(), selected_before.size());
    std::vector<bool> matched(actual.size(), false);
    const auto resolved = request();
    const float cosine = static_cast<float>(std::cos(std::acos(-1.0) / 6.0));
    const float sine = 0.5F;
    for (const auto point : selected_before) {
      const QVec3 offset = point - resolved.pivot;
      const QVec3 expected = resolved.tool == QuaderTransformTool::Move
          ? point + resolved.move_delta
          : resolved.pivot + QVec3{cosine * offset.x - sine * offset.y,
                                  sine * offset.x + cosine * offset.y, offset.z};
      bool found = false;
      for (std::size_t index = 0; index < actual.size(); ++index) {
        if (!matched[index] && std::abs(actual[index].x - expected.x) < 0.0001F &&
            std::abs(actual[index].y - expected.y) < 0.0001F &&
            std::abs(actual[index].z - expected.z) < 0.0001F) {
          matched[index] = true;
          found = true;
          break;
        }
      }
      EXPECT_TRUE(found) << "Missing transformed extrusion vertex at "
                        << expected.x << ", " << expected.y << ", " << expected.z;
    }
  }
};

TEST_P(ComponentExtrudeNudgeTests, AppliesFullTransformOnceAndRepeatsWithExactTopologyHistory) {
  auto &session = *project_.active_scene()->session;
  const auto before = topology();
  const auto selection_before = selection_state();
  const auto positions_before = selected_world_positions();
  const auto world_before = session.scene().world_transform_matrix(object_.entity);
  const auto undo_count = session.history().undo_count();
  const auto nudged = editor_.modeling().nudge_selection(request());
  ASSERT_TRUE(nudged.ok()) << nudged.error_message();
  EXPECT_TRUE(nudged.changed());
  EXPECT_EQ(session.history().undo_count(), undo_count + 1U);
  expect_one_extrusion(before, positions_before);
  const auto once = topology();
  const auto selection_once = selection_state();
  const auto positions_once = selected_world_positions();
  const auto repeated = editor_.history().repeat_last_action();
  ASSERT_TRUE(repeated.ok()) << repeated.error_message();
  EXPECT_TRUE(repeated.changed());
  EXPECT_EQ(session.history().undo_count(), undo_count + 2U);
  expect_one_extrusion(once, positions_once);
  const auto twice = topology();
  const auto selection_twice = selection_state();
  EXPECT_EQ(session.scene().world_transform_matrix(object_.entity).values, world_before.values);
  EXPECT_EQ(editor_.tools().active_tool(), "scale");
  EXPECT_FALSE(session.modeling().transform_drag_active());
  EXPECT_TRUE(session.modeling().tool_preview_meshes().empty());
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_restored_extrusion_topology(topology(), once, twice);
  EXPECT_EQ(selection_state(), selection_once);
  ASSERT_TRUE(editor_.history().undo().ok());
  EXPECT_EQ(session.history().undo_count(), undo_count);
  expect_restored_extrusion_topology(topology(), before, once);
  EXPECT_EQ(selection_state(), selection_before);
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_same_authored_topology(topology(), once);
  EXPECT_EQ(selection_state(), selection_once);
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_same_authored_topology(topology(), twice);
  EXPECT_EQ(selection_state(), selection_twice);

  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Object).ok());
  const auto object_state = test::capture_command_observable_state(editor_);
  const auto wrong_mode = editor_.history().repeat_last_action();
  EXPECT_FALSE(wrong_mode.ok());
  EXPECT_FALSE(wrong_mode.changed());
  EXPECT_EQ(test::capture_command_observable_state(editor_), object_state);
}

TEST_P(ComponentExtrudeNudgeTests, FailureAndPendingDraftPreserveTopologySelectionRedoAndRepeat) {
  auto &session = *project_.active_scene()->session;
  const auto before = topology();
  const auto selection_before = selection_state();
  ASSERT_TRUE(editor_.modeling().nudge_selection(request()).ok());
  const auto accepted = topology();
  const auto accepted_selection = component_selection();
  ASSERT_TRUE(editor_.history().undo().ok());
  const auto undone = topology();
  expect_restored_extrusion_topology(undone, before, accepted);
  const auto undo_count = session.history().undo_count();
  const auto before_failure = test::capture_command_observable_state(editor_);
  session.modeling().set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::AfterSdkMutation);
  const auto rejected = editor_.modeling().nudge_selection(request());
  session.modeling().set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::None);
  EXPECT_FALSE(rejected.ok());
  EXPECT_FALSE(rejected.changed());
  EXPECT_EQ(test::capture_command_observable_state(editor_), before_failure);
  expect_same_authored_topology(topology(), undone);
  EXPECT_EQ(selection_state(), selection_before);
  EXPECT_TRUE(editor_.history().can_redo());

  ASSERT_TRUE(editor_.modeling().begin_transform_preview().ok());
  ASSERT_TRUE(editor_.modeling().update_transform_translate({0.125F, 0, 0}).ok());
  const auto pending = test::capture_command_observable_state(editor_);
  const auto blocked = editor_.modeling().nudge_selection(request());
  EXPECT_FALSE(blocked.ok());
  EXPECT_FALSE(blocked.changed());
  EXPECT_EQ(test::capture_command_observable_state(editor_), pending);
  ASSERT_TRUE(editor_.modeling().cancel_transform_preview().ok());
  expect_same_authored_topology(topology(), undone);
  EXPECT_EQ(selection_state(), selection_before);
  EXPECT_EQ(session.history().undo_count(), undo_count);
  EXPECT_TRUE(editor_.history().can_redo());
  const auto repeated = editor_.history().repeat_last_action();
  ASSERT_TRUE(repeated.ok()) << repeated.error_message();
  EXPECT_TRUE(repeated.changed());
  EXPECT_EQ(session.history().undo_count(), undo_count + 1U);
  EXPECT_FALSE(editor_.history().can_redo());
  expect_fresh_repeat(before, accepted, accepted_selection);
  const auto branched = topology();
  const auto branched_selection = selection_state();
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_restored_extrusion_topology(topology(), before, branched);
  EXPECT_EQ(selection_state(), selection_before);
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_same_authored_topology(topology(), branched);
  EXPECT_EQ(selection_state(), branched_selection);
}

INSTANTIATE_TEST_SUITE_P(FaceAndBoundaryEdge, ComponentExtrudeNudgeTests,
    ::testing::Combine(::testing::Values(QuaderSelectionMode::Face, QuaderSelectionMode::Edge),
                       ::testing::Values(QuaderTransformTool::Move, QuaderTransformTool::Rotate)));

TEST_F(ComponentTransformPersistenceTests, InvalidVertexExtrusionGridPreservesRedoAndPreviousRepeat) {
  const auto before = topology();
  enter_edit_for_all_meshes();
  ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Vertex).ok());
  ASSERT_TRUE(editor_.selection().select_vertex(object_,
      static_cast<QuaderElementId>(before.vertices.front().id)).ok());
  ASSERT_TRUE(editor_.modeling().nudge_selection({
      .axis = QuaderTransformAxis::X, .move_delta = {0.25F, 0, 0}}).ok());
  const auto accepted = topology();
  ASSERT_TRUE(editor_.history().undo().ok());
  const auto state = test::capture_command_observable_state(editor_);
  for (const double grid : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
                            std::numeric_limits<double>::max()}) {
    SCOPED_TRACE(grid);
    const auto rejected = editor_.modeling().nudge_selection({
        .axis = QuaderTransformAxis::Y, .move_delta = {0, 0.5F, 0},
        .duplicate_selected = true, .extrusion_grid_size = grid});
    EXPECT_FALSE(rejected.ok());
    EXPECT_FALSE(rejected.changed());
    EXPECT_EQ(test::capture_command_observable_state(editor_), state);
    EXPECT_TRUE(editor_.history().can_redo());
  }
  ASSERT_TRUE(editor_.history().repeat_last_action().ok());
  expect_same_authored_topology(topology(), accepted);
}

class VertexExtrudeNudgeTests : public ComponentTransformPersistenceTests {
protected:
  using VertexSelectionState = std::pair<std::vector<quader_poly::ElementId>, QuaderElementId>;

  VertexSelectionState vertex_selection() const {
    const auto exported = project_.active_scene()->session->modeling().export_selection();
    EXPECT_EQ(exported.objects.size(), 1U);
    return {exported.objects.size() == 1U ? exported.objects.front().selection.vertices
                                         : std::vector<quader_poly::ElementId>{},
            editor_.selection().summary().active_vertex_id};
  }

  void select_cube_tip() {
    const auto document = topology();
    const auto vertex = std::ranges::find_if(document.vertices, [](const auto &candidate) {
      return candidate.position.x == 1.0F && candidate.position.y == 1.0F && candidate.position.z == 1.0F;
    });
    ASSERT_NE(vertex, document.vertices.end());
    tip_ = vertex->id;
    enter_edit_for_all_meshes();
    ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Vertex).ok());
    ASSERT_TRUE(editor_.selection().select_vertex(object_, static_cast<QuaderElementId>(tip_)).ok());
    ASSERT_TRUE(editor_.tools().activate(ViewportToolId::Scale).ok());
  }

  void install_document(quader_poly::Document document) {
    ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Object).ok());
    ASSERT_TRUE(editor_.scene().destroy(object_.entity).ok());
    const auto created = editor_.modeling().create_mesh(
        QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
        {.name = "Vertex Nudge Surface"});
    ASSERT_TRUE(created.ok()) << created.error_message();
    object_ = created.value();
    enter_edit_for_all_meshes();
    ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Vertex).ok());
  }

  void expect_position(const quader_poly::Document &document, quader_poly::ElementId id,
                        QVec3 expected) const {
    const auto *vertex = quader_poly::find_vertex(document, id);
    ASSERT_NE(vertex, nullptr);
    EXPECT_NEAR(vertex->position.x, expected.x, 0.00001F);
    EXPECT_NEAR(vertex->position.y, expected.y, 0.00001F);
    EXPECT_NEAR(vertex->position.z, expected.z, 0.00001F);
  }

  void expect_history(const quader_poly::Document &before, const quader_poly::Document &after,
                       const VertexSelectionState &selection_before,
                       const VertexSelectionState &selection_after) {
    ASSERT_TRUE(editor_.history().undo().ok());
    expect_restored_extrusion_topology(topology(), before, after);
    EXPECT_EQ(vertex_selection(), selection_before);
    ASSERT_TRUE(editor_.history().redo().ok());
    expect_same_authored_topology(topology(), after);
    EXPECT_EQ(vertex_selection(), selection_after);
  }

  quader_poly::ElementId tip_ = quader_poly::kInvalidElementId;
};

TEST_F(VertexExtrudeNudgeTests, RotationAboutInteriorTipStillCreatesSurfaceRingAndHistory) {
  select_cube_tip();
  const auto before = topology();
  const auto selection = vertex_selection();
  auto &session = *project_.active_scene()->session;
  const auto undo_count = session.history().undo_count();
  ASSERT_TRUE(editor_.modeling().can_nudge_selection(true));
  const auto result = editor_.modeling().nudge_selection({
      .tool = QuaderTransformTool::Rotate, .axis = QuaderTransformAxis::Z,
      .pivot = {1, 1, 1}, .rotate_angle_degrees = 45.0,
      .duplicate_selected = true, .extrusion_grid_size = 0.25});
  ASSERT_TRUE(result.ok()) << result.error_message();
  EXPECT_TRUE(result.changed());
  const auto after = topology();
  EXPECT_EQ(after.vertices.size(), 11U);
  EXPECT_EQ(after.faces.size(), 9U);
  EXPECT_EQ(session.history().undo_count(), undo_count + 1U);
  EXPECT_EQ(vertex_selection(), selection);
  expect_position(after, tip_, {1, 1, 1});
  for (const QVec3 expected : {QVec3{0.75F, 1, 1}, QVec3{1, 0.75F, 1}, QVec3{1, 1, 0.75F}}) {
    EXPECT_TRUE(std::ranges::any_of(after.vertices, [&](const auto &vertex) {
      return std::abs(vertex.position.x - expected.x) < 0.00001F &&
             std::abs(vertex.position.y - expected.y) < 0.00001F &&
             std::abs(vertex.position.z - expected.z) < 0.00001F;
    }));
  }
  EXPECT_EQ(editor_.tools().active_tool(), "scale");
  EXPECT_FALSE(session.modeling().transform_drag_active());
  EXPECT_TRUE(session.modeling().tool_preview_meshes().empty());
  expect_history(before, after, selection, selection);
}

TEST_F(VertexExtrudeNudgeTests, InteriorMoveAndRepeatRetainRawGridIndependentOfDisplacement) {
  select_cube_tip();
  const auto before = topology();
  const auto selection = vertex_selection();
  auto &session = *project_.active_scene()->session;
  const auto undo_count = session.history().undo_count();
  const QuaderResolvedNudgeRequest request{
      .axis = QuaderTransformAxis::All, .move_delta = {0.75F, 0.5F, 1},
      .duplicate_selected = true, .extrusion_grid_size = 0.25};
  const auto moved = editor_.modeling().nudge_selection(request);
  ASSERT_TRUE(moved.ok()) << moved.error_message();
  EXPECT_TRUE(moved.changed());
  const auto once = topology();
  EXPECT_EQ(once.vertices.size(), 11U);
  EXPECT_EQ(once.faces.size(), 9U);
  EXPECT_EQ(vertex_selection(), selection);
  expect_position(once, tip_, {1.75F, 1.5F, 2});
  for (const auto &vertex : once.vertices) {
    if (quader_poly::find_vertex(before, vertex.id) == nullptr) {
      const QVec3 offset = vertex.position - QVec3{1, 1, 1};
      EXPECT_NEAR(offset.x * offset.x + offset.y * offset.y + offset.z * offset.z, 0.0625F, 0.00001F);
    }
  }
  const auto repeated = editor_.history().repeat_last_action();
  ASSERT_TRUE(repeated.ok()) << repeated.error_message();
  EXPECT_TRUE(repeated.changed());
  const auto twice = topology();
  EXPECT_EQ(twice.vertices.size(), 14U);
  EXPECT_EQ(twice.faces.size(), 12U);
  EXPECT_EQ(session.history().undo_count(), undo_count + 2U);
  EXPECT_EQ(vertex_selection(), selection);
  expect_position(twice, tip_, {2.5F, 2, 3});
  for (const auto &vertex : twice.vertices) {
    if (quader_poly::find_vertex(once, vertex.id) == nullptr) {
      const QVec3 offset = vertex.position - QVec3{1.75F, 1.5F, 2};
      EXPECT_NEAR(offset.x * offset.x + offset.y * offset.y + offset.z * offset.z, 0.0625F, 0.00001F);
    }
  }
  expect_history(once, twice, selection, selection);
  ASSERT_TRUE(editor_.history().undo().ok());
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_restored_extrusion_topology(topology(), before, once);
  EXPECT_EQ(vertex_selection(), selection);
}

TEST_F(VertexExtrudeNudgeTests, BoundaryCornerAndChainExtendSeamWithoutGridSetback) {
  for (const bool chain : {false, true}) {
    SCOPED_TRACE(chain);
    quader_poly::Document document;
    const std::array vertices{
        quader_poly::add_vertex(document, {0, 0, 0}),
        quader_poly::add_vertex(document, {4, 0, 0}),
        quader_poly::add_vertex(document, {4, 4, 0}),
        quader_poly::add_vertex(document, {0, 4, 0})};
    (void)quader_poly::add_face(document, vertices);
    install_document(std::move(document));
    const std::vector<QuaderElementId> selected = chain
        ? std::vector<QuaderElementId>{static_cast<QuaderElementId>(vertices[0]), static_cast<QuaderElementId>(vertices[1])}
        : std::vector<QuaderElementId>{static_cast<QuaderElementId>(vertices[0])};
    ASSERT_TRUE(editor_.selection().select_components({
        .object = object_, .kind = QuaderComponentKind::Vertex, .vertices = selected,
        .active = {.object = object_, .kind = QuaderComponentKind::Vertex,
                   .vertex = static_cast<QuaderElementId>(vertices[0])}}).ok());
    const auto before = topology();
    const auto selection_before = vertex_selection();
    const auto result = editor_.modeling().nudge_selection({
        .axis = QuaderTransformAxis::Z, .move_delta = {0, 0, 1},
        .duplicate_selected = true, .extrusion_grid_size = 64.0});
    ASSERT_TRUE(result.ok()) << result.error_message();
    EXPECT_TRUE(result.changed());
    const auto after = topology();
    EXPECT_EQ(after.vertices.size(), chain ? 6U : 5U);
    EXPECT_EQ(after.faces.size(), 2U);
    const auto selection_after = vertex_selection();
    EXPECT_EQ(selection_after.first.size(), selected.size());
    EXPECT_NE(selection_after.second, selection_before.second);
    expect_position(after, static_cast<quader_poly::ElementId>(selection_after.second), {0, 0, 1});
    for (const auto &vertex : before.vertices) expect_position(after, vertex.id, vertex.position);
    for (const auto id : selection_after.first) {
      EXPECT_EQ(quader_poly::find_vertex(before, id), nullptr);
      const auto *vertex = quader_poly::find_vertex(after, id);
      ASSERT_NE(vertex, nullptr);
      EXPECT_FLOAT_EQ(vertex->position.y, 0.0F);
      EXPECT_FLOAT_EQ(vertex->position.z, 1.0F);
      EXPECT_TRUE(vertex->position.x == 0.0F || (chain && vertex->position.x == 4.0F));
    }
    expect_history(before, after, selection_before, selection_after);
  }
}

TEST_F(VertexExtrudeNudgeTests, IsolatedPointKeepsIdentityAndTopologyForModifierTransforms) {
  quader_poly::Document document;
  const std::array vertices{
      quader_poly::add_vertex(document, {0, 0, 0}),
      quader_poly::add_vertex(document, {4, 0, 0}),
      quader_poly::add_vertex(document, {4, 4, 0}),
      quader_poly::add_vertex(document, {0, 4, 0})};
  (void)quader_poly::add_face(document, vertices);
  const auto isolated = quader_poly::add_vertex(document, {8, 2, 3});
  install_document(std::move(document));
  ASSERT_TRUE(editor_.selection().select_vertex(object_, static_cast<QuaderElementId>(isolated)).ok());
  const auto before = topology();
  const auto selection = vertex_selection();
  for (const auto tool : {QuaderTransformTool::Move, QuaderTransformTool::Rotate}) {
    SCOPED_TRACE(static_cast<int>(tool));
    const auto result = editor_.modeling().nudge_selection({
        .tool = tool, .axis = QuaderTransformAxis::Z,
        .move_delta = tool == QuaderTransformTool::Move ? QVec3{0, 0, 0.5F} : QVec3{},
        .rotate_angle_degrees = tool == QuaderTransformTool::Rotate ? 90.0 : 0.0,
        .duplicate_selected = true, .extrusion_grid_size = 0.25});
    ASSERT_TRUE(result.ok()) << result.error_message();
    EXPECT_TRUE(result.changed());
    const auto after = topology();
    EXPECT_EQ(after.vertices.size(), before.vertices.size());
    EXPECT_EQ(after.faces.size(), before.faces.size());
    EXPECT_EQ(after.next_vertex_id, before.next_vertex_id);
    EXPECT_EQ(after.next_face_id, before.next_face_id);
    EXPECT_EQ(vertex_selection(), selection);
    expect_position(after, isolated, tool == QuaderTransformTool::Move ? QVec3{8, 2, 3.5F} : QVec3{-2, 8, 3});
    for (const auto &vertex : before.vertices) {
      if (vertex.id != isolated) expect_position(after, vertex.id, vertex.position);
    }
    expect_history(before, after, selection, selection);
    ASSERT_TRUE(editor_.history().undo().ok());
  }
}

class ComponentZeroScalePersistenceTests
    : public ComponentTransformPersistenceTests,
      public ::testing::WithParamInterface<QuaderSelectionMode> {};

TEST_P(ComponentZeroScalePersistenceTests, RetainsAllAuthoredFaces) {
    const auto mode = GetParam();
    enter_edit_for_all_meshes();
    ASSERT_TRUE(editor_.selection().set_mode(mode).ok());
    if (mode == QuaderSelectionMode::Face) {
      ASSERT_TRUE(editor_.selection().select_face(object_, 1).ok());
    } else if (mode == QuaderSelectionMode::Edge) {
      const auto before = topology();
      const auto &face = before.faces[0];
      ASSERT_TRUE(editor_.selection().select_edge(
          object_, static_cast<QuaderElementId>(face.vertices[0]),
          static_cast<QuaderElementId>(face.vertices[1])).ok());
    } else {
      ASSERT_TRUE(editor_.selection().select_vertex(object_, 1).ok());
    }
    ASSERT_TRUE(editor_.selection().select_all().ok());
    const auto before = topology();
    const auto scaled = editor_.modeling().scale_selection({0.0f, 0.0f, 0.0f});
    ASSERT_TRUE(scaled.ok()) << scaled.error_message();
    ASSERT_TRUE(scaled.changed());
    const auto after = topology();
    ASSERT_EQ(after.vertices.size(), 8U);
    ASSERT_EQ(after.faces.size(), 6U);
    EXPECT_FALSE(quader_poly::validate_polygon_document(after).ok());
    verify_history_and_roundtrip(before, after);
}

INSTANTIATE_TEST_SUITE_P(ComponentModes, ComponentZeroScalePersistenceTests,
                        ::testing::Values(QuaderSelectionMode::Face,
                                          QuaderSelectionMode::Edge,
                                          QuaderSelectionMode::Vertex));

class ComponentGridSnapPersistenceTests
    : public ComponentTransformPersistenceTests,
      public ::testing::WithParamInterface<std::tuple<QuaderSelectionMode, float>> {};

TEST_P(ComponentGridSnapPersistenceTests,
       OffGridAnchorCatchesUpWithoutChangingOffsetsAndRetainsHistory) {
  const auto [mode, direction] = GetParam();
  enter_edit_for_all_meshes();
  ASSERT_TRUE(editor_.selection().set_mode(mode).ok());
  ASSERT_TRUE(editor_.selection().select_all().ok());
  const float anchor = direction * 6.0F;
  ASSERT_TRUE(editor_.modeling().move_selection({anchor, 0, 0}).ok());
  const auto before = topology();
  auto &session = *project_.active_scene()->session;
  const auto history_count = session.history().undo_count();
  auto begin = component_move_request();
  begin.pivot = {anchor, 0, 0};
  begin.ray.origin.x = anchor;
  begin.grid_size = 8.0;
  begin.grid_snap_enabled = true;
  const auto drag_to = [&](float raw_delta) {
    return editor_.modeling().update_transform_drag({
        .screen_position = {100.0F + raw_delta * 100.0F, 100.0F},
        .ray = {.origin = {anchor + raw_delta, 0, 5}, .direction = {0, 0, -1}},
        .camera = begin.camera, .grid_size = 8.0,
        .vertex_snap_radius = 0, .grid_snap_enabled = true});
  };
  const auto expected_document = [&](float delta) {
    auto expected = before;
    for (auto &vertex : expected.vertices) vertex.position.x += delta;
    return expected;
  };

  ASSERT_TRUE(editor_.modeling().begin_transform_drag(begin).ok());
  const auto stationary = drag_to(0.0F);
  ASSERT_TRUE(stationary.ok()) << stationary.error_message();
  EXPECT_FLOAT_EQ(stationary.value().move_delta.x, 0.0F);
  const auto clicked = editor_.modeling().commit_transform_drag();
  ASSERT_TRUE(clicked.ok()) << clicked.error_message();
  EXPECT_FALSE(clicked.changed());
  EXPECT_EQ(session.history().undo_count(), history_count);
  expect_same_authored_topology(topology(), before);

  for (const bool cancel : {true, false}) {
    SCOPED_TRACE(cancel);
    ASSERT_TRUE(editor_.modeling().begin_transform_drag(begin).ok());
    // Each sample starts from the original off-grid anchor, even after the
    // previous candidate has already caught up to the neighboring grid line.
    for (const auto [raw_delta, snapped_delta] :
         {std::pair{1.0F, 2.0F}, std::pair{7.0F, 10.0F}}) {
      const auto moved = drag_to(direction * raw_delta);
      ASSERT_TRUE(moved.ok()) << moved.error_message();
      EXPECT_FLOAT_EQ(moved.value().move_delta.x, direction * snapped_delta);
      EXPECT_FLOAT_EQ(moved.value().move_delta.y, 0.0F);
      EXPECT_FLOAT_EQ(moved.value().move_delta.z, 0.0F);
      const auto previews = session.modeling().tool_preview_meshes();
      ASSERT_EQ(previews.size(), 1U);
      expect_preview_matches_document(previews.front(),
          expected_document(direction * snapped_delta));
      expect_same_authored_topology(topology(), before);
      EXPECT_EQ(session.history().undo_count(), history_count);
    }
    if (cancel) {
      const auto returned = drag_to(0.0F);
      ASSERT_TRUE(returned.ok()) << returned.error_message();
      EXPECT_FLOAT_EQ(returned.value().move_delta.x, 0.0F);
      const auto previews = session.modeling().tool_preview_meshes();
      if (!previews.empty()) {
        ASSERT_EQ(previews.size(), 1U);
        expect_preview_matches_document(previews.front(), before);
      }
      ASSERT_TRUE(drag_to(direction).ok());
      ASSERT_TRUE(editor_.modeling().cancel_transform_drag().ok());
      EXPECT_EQ(session.history().undo_count(), history_count);
      expect_same_authored_topology(topology(), before);
    } else {
      ASSERT_TRUE(editor_.modeling().commit_transform_drag().ok());
      EXPECT_EQ(session.history().undo_count(), history_count + 1U);
      const auto after = expected_document(direction * 10.0F);
      expect_same_authored_topology(topology(), after);
      verify_history_and_roundtrip(before, after);
    }
  }
}

INSTANTIATE_TEST_SUITE_P(ComponentModesAndDirections, ComponentGridSnapPersistenceTests,
                        ::testing::Combine(
                            ::testing::Values(QuaderSelectionMode::Face,
                                              QuaderSelectionMode::Edge,
                                              QuaderSelectionMode::Vertex),
                            ::testing::Values(-1.0F, 1.0F)));

TEST_F(ComponentTransformPersistenceTests,
       ComponentScaleCapturesSelectedBoundsInReflectedShearedFrame) {
  const auto unit_box = topology();
  const auto first = std::ranges::find_if(unit_box.vertices, [](const auto &vertex) {
    return vertex.position.x == -1.0F && vertex.position.y == 1.0F &&
           vertex.position.z == 1.0F;
  });
  const auto second = std::ranges::find_if(unit_box.vertices, [](const auto &vertex) {
    return vertex.position.x == 1.0F && vertex.position.y == -1.0F &&
           vertex.position.z == -1.0F;
  });
  ASSERT_NE(first, unit_box.vertices.end());
  ASSERT_NE(second, unit_box.vertices.end());
  enter_edit_for_all_meshes();
  ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Vertex).ok());
  ASSERT_TRUE(editor_.selection().select_all().ok());
  const QuaderTransform3 orientation{
      .x_axis = {-1, 0, 0}, .y_axis = {0.5F, 1, 0},
      .z_axis = {0.25F, 0.5F, 1}};
  ASSERT_TRUE(editor_.modeling().transform_selection({
      .x_axis = {-4, 0, 0}, .y_axis = orientation.y_axis,
      .z_axis = orientation.z_axis, .origin = {-5, 0, 0}}).ok());
  ASSERT_TRUE(editor_.selection().select_components({
      .object = object_, .kind = QuaderComponentKind::Vertex,
      .vertices = {static_cast<QuaderElementId>(first->id),
                   static_cast<QuaderElementId>(second->id)},
      .edit = QuaderSelectionEdit::Replace}).ok());
  const auto before = topology();
  auto &session = *project_.active_scene()->session;
  const auto history_count = session.history().undo_count();
  auto begin = component_move_request();
  begin.tool = QuaderTransformTool::Scale;
  begin.orientation = orientation;
  begin.pivot = {-5, 0, 0};
  begin.world_direction = {-1, 0, 0};
  begin.grid_size = 8.0;
  begin.grid_snap_enabled = true;
  ASSERT_TRUE(editor_.modeling().begin_transform_drag(begin).ok());
  const auto expected_document = [&](float factor) {
    auto expected = before;
    for (auto &vertex : expected.vertices) {
      if (vertex.id != first->id && vertex.id != second->id) continue;
      // The inverse of the authored reflected/sheared frame gives logical
      // x = -world.x + world.y/2. Selected endpoints are exactly 1 and 9;
      // inverse-projecting their world AABB would instead give -0.5 and 10.5.
      const float logical_x = -vertex.position.x + vertex.position.y * 0.5F;
      vertex.position.x -= (factor - 1.0F) * (logical_x - 5.0F);
    }
    return expected;
  };
  for (const auto [pointer_delta, expected_factor] : {
           std::pair{19.2F, 1.25F}, std::pair{-19.2F, 0.75F},
           std::pair{12.0F, 1.0F}, std::pair{-12.0F, 1.0F},
           std::pair{0.0F, 1.0F}, std::pair{19.2F, 1.25F}}) {
    SCOPED_TRACE(pointer_delta);
    const auto scaled = editor_.modeling().update_transform_drag({
        .screen_position = {100.0F + pointer_delta, 100},
        .ray = begin.ray, .camera = begin.camera, .grid_size = 8.0,
        .vertex_snap_radius = 0, .grid_snap_enabled = true});
    ASSERT_TRUE(scaled.ok()) << scaled.error_message();
    EXPECT_FLOAT_EQ(scaled.value().scale.x, expected_factor);
    EXPECT_FLOAT_EQ(scaled.value().scale.y, 1.0F);
    EXPECT_FLOAT_EQ(scaled.value().scale.z, 1.0F);
    const auto previews = session.modeling().tool_preview_meshes();
    if (!previews.empty()) {
      ASSERT_EQ(previews.size(), 1U);
      expect_preview_matches_document(previews.front(), expected_document(expected_factor));
    } else {
      EXPECT_FLOAT_EQ(expected_factor, 1.0F);
    }
    expect_same_authored_topology(topology(), before);
    EXPECT_EQ(session.history().undo_count(), history_count);
  }
  ASSERT_TRUE(editor_.modeling().commit_transform_drag().ok());
  const auto after = expected_document(1.25F);
  expect_same_authored_topology(topology(), after);
  EXPECT_EQ(session.history().undo_count(), history_count + 1U);
  verify_history_and_roundtrip(before, after);
}

TEST_F(ComponentTransformPersistenceTests,
       UniformInsetRetainsGridQuantizationWhileOrdinaryScaleRemainsFree) {
  select_positive_x_face();
  const auto before = topology();
  auto &session = *project_.active_scene()->session;
  const auto history_count = session.history().undo_count();
  for (const auto operation : {
           QuaderTransformOperation::Transform, QuaderTransformOperation::Inset}) {
    SCOPED_TRACE(static_cast<int>(operation));
    auto begin = component_move_request();
    begin.tool = QuaderTransformTool::Scale;
    begin.axis = QuaderTransformAxis::All;
    begin.operation = operation;
    begin.pivot = {1, 0, 0};
    begin.grid_size = 0.5;
    begin.grid_snap_enabled = true;
    const auto started = editor_.modeling().begin_transform_drag(begin);
    ASSERT_TRUE(started.ok()) << started.error_message();
    for (const bool grid_enabled : {true, false, true}) {
      const auto scaled = editor_.modeling().update_transform_drag({
          .screen_position = {80.8F, 100},
          .ray = begin.ray, .camera = begin.camera, .grid_size = 0.5,
          .vertex_snap_radius = 0, .grid_snap_enabled = grid_enabled});
      ASSERT_TRUE(scaled.ok()) << scaled.error_message();
      const float expected = operation == QuaderTransformOperation::Inset && grid_enabled
          ? 0.75F : 0.8F;
      EXPECT_NEAR(scaled.value().scale.x, expected, 0.0001F);
      EXPECT_NEAR(scaled.value().scale.y, expected, 0.0001F);
      EXPECT_NEAR(scaled.value().scale.z, expected, 0.0001F);
      EXPECT_EQ(scaled.value().operation, operation);
      if (operation == QuaderTransformOperation::Transform) {
        EXPECT_FALSE(session.modeling().tool_preview_meshes().empty());
        expect_same_authored_topology(topology(), before);
      } else {
        // Inset publishes its topology draft into the authored projection;
        // cancellation restores the baseline after all grid updates below.
        const auto preview = topology();
        ASSERT_EQ(preview.vertices.size(), before.vertices.size() + 4U);
        EXPECT_EQ(preview.faces.size(), before.faces.size() + 4U);
        std::size_t inset_vertices = 0U;
        for (const auto &vertex : preview.vertices) {
          const auto *original = quader_poly::find_vertex(before, vertex.id);
          if (original != nullptr) {
            EXPECT_FLOAT_EQ(vertex.position.x, original->position.x);
            EXPECT_FLOAT_EQ(vertex.position.y, original->position.y);
            EXPECT_FLOAT_EQ(vertex.position.z, original->position.z);
          } else {
            ++inset_vertices;
            EXPECT_NEAR(vertex.position.x, 1.0F, 0.0001F);
            EXPECT_NEAR(std::abs(vertex.position.y), expected, 0.0001F);
            EXPECT_NEAR(std::abs(vertex.position.z), expected, 0.0001F);
          }
        }
        EXPECT_EQ(inset_vertices, 4U);
      }
      EXPECT_EQ(session.history().undo_count(), history_count);
    }
    ASSERT_TRUE(editor_.modeling().cancel_transform_drag().ok());
    expect_same_authored_topology(topology(), before);
    EXPECT_EQ(session.history().undo_count(), history_count);
  }
}

TEST_F(ComponentTransformPersistenceTests, OrdinaryFaceMoveRetainsItsTopologyThroughSaveAndHistory) {
  select_positive_x_face();
  const auto before = topology();
  ASSERT_TRUE(editor_.modeling().move_selection({0.5f, 0.0f, 0.0f}).ok());
  const auto after = topology();
  EXPECT_TRUE(quader_poly::validate_polygon_document(after).ok());
  verify_history_and_roundtrip(before, after);
}

TEST_F(ComponentTransformPersistenceTests, CommitReusesTheExactAcceptedPreviewGeometry) {
  select_positive_x_face();
  const auto before = topology();
  auto document = MeshProjectionService::sdk_document_from_polygon_document(before);
  ASSERT_TRUE(document.ok());
  ASSERT_TRUE(document.value().select_face(modeling::make_id<modeling::FaceTag>(
      static_cast<std::uint32_t>(editor_.selection().summary().active_face_id))).ok());
  ASSERT_TRUE(document.value().translate_selection({0.5F, 0.25F, 0.0F}).ok());
  const auto canonical = MeshProjectionService::compile_sdk_document_mesh(document.value());
  ASSERT_TRUE(canonical.has_value());
  ASSERT_NE(canonical->authored_polygon_document, nullptr);
  ASSERT_TRUE(editor_.modeling().begin_transform_preview().ok());
  ASSERT_TRUE(editor_.modeling().update_transform_translate({0.5F, 0.25F, 0.0F}).ok());
  auto &session = *project_.active_scene()->session;
  const auto previews = session.modeling().tool_preview_meshes();
  ASSERT_EQ(previews.size(), 1U);
  // Compare the published streams without warming the combined geometry cache.
  // Commit must adopt this accepted publication even when no getter flattened it.
  const auto projection = previews.front().geometry_projection;
  ASSERT_NE(projection, nullptr);
  ASSERT_NE(projection->positions, nullptr);
  const auto attributes = projection->vertices();
  ASSERT_EQ(attributes.size(), canonical->geometry().vertices.size());
  ASSERT_EQ(projection->positions->size(), attributes.size());
  for (std::size_t index = 0U; index < attributes.size(); ++index) {
    auto vertex = attributes[index];
    vertex.position = (*projection->positions)[index];
    EXPECT_TRUE(mesh_asset_payload_detail::same_vertex(
        vertex, canonical->geometry().vertices[index])) << index;
  }
  EXPECT_TRUE(std::ranges::equal(projection->indices(), canonical->geometry().indices));
  EXPECT_TRUE(std::ranges::equal(projection->primitives(), canonical->geometry().primitives));
  const auto compilations = session.modeling().modeling_sdk_projection_stats_for_tests()
                                .mesh_compile_count;
  ASSERT_TRUE(editor_.modeling().commit_transform_preview().ok());
  const auto *binding = session.scene().mesh(object_.entity);
  ASSERT_NE(binding, nullptr);
  const auto *committed = session.assets().mesh(binding->mesh);
  ASSERT_NE(committed, nullptr);
  const auto candidate = projection->materialized_geometry();
  ASSERT_NE(candidate, nullptr);
  EXPECT_EQ(committed->geometry_payload(), candidate);
  EXPECT_TRUE(mesh_geometry_payload_equal(committed->geometry(), canonical->geometry()));
  EXPECT_EQ(session.modeling().modeling_sdk_projection_stats_for_tests().mesh_compile_count,
            compilations);
  expect_same_authored_topology(topology(), *canonical->authored_polygon_document);
  verify_history_and_roundtrip(before, topology());
}

TEST_F(ComponentTransformPersistenceTests, AffinePreviewAndCommitShareCanonicalSdkArithmetic) {
  select_positive_x_face();
  const auto before = topology();
  const auto selected_face = editor_.selection().summary().active_face_id;
  auto document = MeshProjectionService::sdk_document_from_polygon_document(before);
  ASSERT_TRUE(document.ok());
  ASSERT_TRUE(document.value().select_face(
      modeling::make_id<modeling::FaceTag>(static_cast<std::uint32_t>(selected_face))).ok());
  const QuaderTransform3 transform{
      .x_axis = {1.0F, 0.0F, 0.0F}, .y_axis = {0.03F, 1.0F, 0.0F},
      .z_axis = {0.77F, 0.0F, 1.0F}, .origin = {0.03F, 0.0F, 0.0F}};
  ASSERT_TRUE(document.value().transform_selection({
      .x_axis = {1.0F, 0.0F, 0.0F}, .y_axis = {0.03F, 1.0F, 0.0F},
      .z_axis = {0.77F, 0.0F, 1.0F}, .origin = {0.03F, 0.0F, 0.0F}}).ok());
  const auto canonical = MeshProjectionService::compile_sdk_document_mesh(document.value());
  ASSERT_TRUE(canonical.has_value());
  ASSERT_NE(canonical->authored_polygon_document, nullptr);
  ASSERT_TRUE(editor_.modeling().begin_transform_preview().ok());
  ASSERT_TRUE(editor_.modeling().update_transform_matrix(transform, {}).ok());
  auto &session = *project_.active_scene()->session;
  const auto previews = session.modeling().tool_preview_meshes();
  ASSERT_EQ(previews.size(), 1U);
  const auto candidate = effective_preview_geometry(previews.front());
  ASSERT_NE(candidate, nullptr);
  EXPECT_TRUE(mesh_geometry_payload_equal(*candidate, canonical->geometry()));
  const auto compilations = session.modeling().modeling_sdk_projection_stats_for_tests()
                                .mesh_compile_count;
  ASSERT_TRUE(editor_.modeling().commit_transform_preview().ok());
  const auto *binding = session.scene().mesh(object_.entity);
  ASSERT_NE(binding, nullptr);
  const auto *committed = session.assets().mesh(binding->mesh);
  ASSERT_NE(committed, nullptr);
  EXPECT_TRUE(mesh_geometry_payload_equal(committed->geometry(), canonical->geometry()));
  EXPECT_EQ(session.modeling().modeling_sdk_projection_stats_for_tests().mesh_compile_count,
            compilations);
  EXPECT_EQ(committed->geometry_payload(), candidate);
  expect_same_authored_topology(topology(), *canonical->authored_polygon_document);
  verify_history_and_roundtrip(before, topology());
}

TEST_F(ComponentTransformPersistenceTests, SparseDragHistoryRestoresCanonicalAcceptedPositions) {
  select_positive_x_face();
  const auto before = topology();
  const QuaderTransformDragBeginRequest begin{
      .tool = QuaderTransformTool::Rotate, .axis = QuaderTransformAxis::X,
      .pivot = {1.0F, 0.03F, 0.07F}, .screen_position = {120, 100},
      .screen_pivot = {100, 100}, .screen_tip = {120, 100},
      .has_world_direction = true, .world_direction = {1, 0, 0},
      .ray = {.origin = {10, 0, 0}, .direction = {-1, 0, 0}},
      .camera = {.orthographic = true, .origin = {10, 0, 0},
                 .x_axis = {0, 0, -1}, .y_axis = {0, 1, 0}, .z_axis = {1, 0, 0},
                 .viewport_size = {800, 600}, .orthographic_size = 8},
      .vertex_snap_radius = 0, .grid_snap_enabled = false, .rotation_snap_degrees = 0};
  auto &modeling = project_.active_scene()->session->modeling();
  for (const QVec2 position : {QVec2{117, 113}, QVec2{113, 121}, QVec2{81, 117}}) {
    ASSERT_TRUE(editor_.modeling().begin_transform_drag(begin).ok());
    ASSERT_TRUE(editor_.modeling().update_transform_drag({
        .screen_position = position, .ray = begin.ray, .camera = begin.camera,
        .vertex_snap_radius = 0, .grid_snap_enabled = false,
        .rotation_snap_degrees = 0}).ok());
    const auto compilations = modeling.modeling_sdk_projection_stats_for_tests().mesh_compile_count;
    ASSERT_TRUE(editor_.modeling().commit_transform_drag().ok());
    EXPECT_EQ(modeling.modeling_sdk_projection_stats_for_tests().mesh_compile_count,
              compilations);
    const auto accepted = topology();
    ASSERT_TRUE(editor_.history().undo().ok());
    expect_same_authored_topology(topology(), before);
    ASSERT_TRUE(editor_.history().redo().ok());
    expect_same_authored_topology(topology(), accepted);
    ASSERT_TRUE(editor_.history().undo().ok());
  }
}

TEST_F(ComponentTransformPersistenceTests, SparseDragHistoryOmitsSdkUnchangedParticipants) {
  enter_edit_for_all_meshes();
  ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Vertex).ok());
  const auto original = topology();
  const auto first_vertex = std::ranges::find_if(original.vertices, [](const auto &vertex) {
    return vertex.position.x == 1.0F && vertex.position.y == 1.0F && vertex.position.z == 1.0F;
  });
  ASSERT_NE(first_vertex, original.vertices.end());
  const auto first_id = static_cast<QuaderElementId>(first_vertex->id);
  ASSERT_TRUE(editor_.selection().select_vertex(object_, first_id).ok());
  ASSERT_TRUE(editor_.modeling().move_selection({-1.0F, 0.0F, 0.0F}).ok());
  const auto first_before = topology();
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Object).ok());
  const auto created = editor_.modeling().create_box(
      {.min = {2, 0, 0}, .max = {4, 1, 1}}, "Changed history participant");
  ASSERT_TRUE(created.ok());
  const auto second = created.value();
  auto &session = *project_.active_scene()->session;
  const auto document = [&](QuaderModelingObjectRef object) {
    const auto *binding = session.scene().mesh(object.entity);
    const auto *mesh = binding != nullptr ? session.assets().mesh(binding->mesh) : nullptr;
    EXPECT_NE(mesh, nullptr);
    EXPECT_TRUE(mesh != nullptr && mesh->authored_polygon_document != nullptr);
    return mesh != nullptr && mesh->authored_polygon_document
        ? *mesh->authored_polygon_document : quader_poly::Document{};
  };
  const auto second_before = document(second);
  const auto second_vertex = std::ranges::find_if(second_before.vertices, [](const auto &vertex) {
    return vertex.position.x == 2.0F && vertex.position.y == 1.0F && vertex.position.z == 0.0F;
  });
  ASSERT_NE(second_vertex, second_before.vertices.end());
  enter_edit_for_all_meshes();
  ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Vertex).ok());
  ASSERT_TRUE(editor_.selection().select_vertex(object_, first_id).ok());
  ASSERT_TRUE(editor_.selection().select_components({
      .object = second, .kind = QuaderComponentKind::Vertex,
      .vertices = {static_cast<QuaderElementId>(second_vertex->id)},
      .edit = QuaderSelectionEdit::Add}).ok());
  ASSERT_EQ(editor_.selection().summary().selected_object_count, 2U);
  const auto begun = editor_.modeling().begin_transform_drag({
      .tool = QuaderTransformTool::Move, .axis = QuaderTransformAxis::X,
      .screen_position = {100, 100}, .screen_pivot = {100, 100},
      .screen_tip = {120, 100}, .has_world_direction = true, .world_direction = {1, 0, 0},
      .ray = {.origin = {0, 0, 5}, .direction = {0, 0, -1}},
      .camera = {.orthographic = true, .origin = {0, 0, 5},
                 .x_axis = {1, 0, 0}, .y_axis = {0, 1, 0}, .z_axis = {0, 0, 1},
                 .viewport_size = {800, 600}, .orthographic_size = 8},
      .vertex_snap_radius = 0, .grid_snap_enabled = false});
  ASSERT_TRUE(begun.ok()) << begun.error_message();
  ASSERT_TRUE(begun.value().preview_driven);
  // The public affine update keeps the drag's sparse history owner active.
  // The canonical SDK arithmetic leaves (0,1,1) fixed despite cancellation.
  // Both live presentation and sparse history must preserve that unchanged participant.
  ASSERT_TRUE(editor_.modeling().update_transform_matrix({
      .x_axis = {1, 0, 0}, .y_axis = {1, 1, 0}, .z_axis = {-1, 0, 1},
      .origin = {1.0e-8F, 0, 0}}, {}).ok());
  const auto previews = session.modeling().tool_preview_meshes();
  const auto first_preview = std::ranges::find(previews, object_.entity,
                                               &ModelingToolPreviewMesh::source_entity);
  ASSERT_NE(first_preview, previews.end());
  expect_preview_matches_document(*first_preview, first_before);
  ASSERT_TRUE(editor_.modeling().commit_transform_drag().ok());
  expect_same_authored_topology(document(object_), first_before);
  const auto second_after = document(second);
  EXPECT_FALSE(mesh_asset_payload_detail::same_polygon_document(second_before, second_after));
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_same_authored_topology(document(object_), first_before);
  expect_same_authored_topology(document(second), second_before);
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_same_authored_topology(document(object_), first_before);
  expect_same_authored_topology(document(second), second_after);
}

TEST_F(ComponentTransformPersistenceTests,
       MoveDragPreservesIdenticalLocalGeometryAtDifferentWorldOrigins) {
  const auto before = topology();
  const auto selected_id = static_cast<QuaderElementId>(before.vertices.front().id);
  std::vector<QuaderModelingObjectRef> objects{object_};
  for (int index = 0; index < 2; ++index) {
    const auto created = editor_.modeling().create_box(
        {.min = {-1, -1, -1}, .max = {1, 1, 1}}, "Translated edit participant");
    ASSERT_TRUE(created.ok()) << created.error_message();
    objects.push_back(created.value());
  }
  constexpr std::array<float, 3u> kOrigins{0.0F, 1000.0F, 1000000.0F};
  for (std::size_t index = 0u; index < objects.size(); ++index) {
    ASSERT_TRUE(editor_.scene().set_transform(objects[index].entity,
        {.position = {kOrigins[index], -kOrigins[index], kOrigins[index]}}).ok());
  }
  enter_edit_for_all_meshes();
  ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Vertex).ok());
  for (std::size_t index = 0u; index < objects.size(); ++index) {
    ASSERT_TRUE(editor_.selection().select_components({
        .object = objects[index], .kind = QuaderComponentKind::Vertex,
        .vertices = {selected_id},
        .edit = index == 0u ? QuaderSelectionEdit::Replace : QuaderSelectionEdit::Add}).ok());
  }
  const auto request = component_move_request();
  ASSERT_TRUE(editor_.modeling().begin_transform_drag(request).ok());
  const auto moved = editor_.modeling().update_transform_drag({
      .screen_position = {103, 100},
      .ray = {.origin = {0.03F, 0, 5}, .direction = {0, 0, -1}},
      .camera = request.camera, .vertex_snap_radius = 0, .grid_snap_enabled = false});
  ASSERT_TRUE(moved.ok()) << moved.error_message();
  ASSERT_NE(moved.value().move_delta.x, 0.0F);
  const auto previews = project_.active_scene()->session->modeling().tool_preview_meshes();
  ASSERT_EQ(previews.size(), objects.size());
  auto expected = before;
  auto *selected_vertex = quader_poly::find_vertex(expected, selected_id);
  ASSERT_NE(selected_vertex, nullptr);
  selected_vertex->position = selected_vertex->position + moved.value().move_delta;
  const auto candidate = effective_preview_geometry(previews.front());
  ASSERT_NE(candidate, nullptr);
  for (const auto &preview : previews) {
    expect_preview_matches_document(preview, expected);
    EXPECT_EQ(effective_preview_geometry(preview), candidate);
  }
  ASSERT_TRUE(editor_.modeling().cancel_transform_drag().ok());
}

TEST_F(ComponentTransformPersistenceTests,
       MoveRepeatPreservesEachRotatedScaledObjectLocalDisplacement) {
  const auto before = topology();
  const auto selected_id = static_cast<QuaderElementId>(before.vertices.front().id);
  const auto created = editor_.modeling().create_box(
      {.min = {-1, -1, -1}, .max = {1, 1, 1}}, "Rotated edit participant");
  ASSERT_TRUE(created.ok()) << created.error_message();
  const std::array objects{object_, created.value()};
  ASSERT_TRUE(editor_.scene().set_transform(objects[0].entity,
      {.position = {1000, 0, 0}}).ok());
  ASSERT_TRUE(editor_.scene().set_transform(objects[1].entity,
      {.position = {1000000, 0, 0}, .rotation_euler = {0.2F, 0.4F, 1.1F},
       .scale = {2.0F, 0.5F, 3.0F}}).ok());
  enter_edit_for_all_meshes();
  ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Vertex).ok());
  for (std::size_t index = 0u; index < objects.size(); ++index) {
    ASSERT_TRUE(editor_.selection().select_components({
        .object = objects[index], .kind = QuaderComponentKind::Vertex,
        .vertices = {selected_id},
        .edit = index == 0u ? QuaderSelectionEdit::Replace : QuaderSelectionEdit::Add}).ok());
  }
  const auto request = component_move_request();
  ASSERT_TRUE(editor_.modeling().begin_transform_drag(request).ok());
  const auto moved = editor_.modeling().update_transform_drag({
      .screen_position = {112.5F, 100},
      .ray = {.origin = {0.125F, 0, 5}, .direction = {0, 0, -1}},
      .camera = request.camera, .vertex_snap_radius = 0, .grid_snap_enabled = false});
  ASSERT_TRUE(moved.ok()) << moved.error_message();
  ASSERT_GT(moved.value().move_delta.x, 0.0F);
  auto &session = *project_.active_scene()->session;
  const auto previews = session.modeling().tool_preview_meshes();
  ASSERT_EQ(previews.size(), objects.size());
  std::vector<quader_poly::Document> first_documents;
  std::vector<quader_poly::Document> repeated_documents;
  for (const auto object : objects) {
    const auto preview = std::ranges::find(previews, object.entity,
                                            &ModelingToolPreviewMesh::source_entity);
    ASSERT_NE(preview, previews.end());
    const auto world = session.scene().world_transform_matrix(object.entity);
    const auto inverse_world = try_scene_matrix_inverse(world);
    ASSERT_TRUE(inverse_world.has_value());
    const auto local_delta = transform_vector(*inverse_world, moved.value().move_delta);
    auto canonical = MeshProjectionService::sdk_document_from_polygon_document(before);
    ASSERT_TRUE(canonical.ok());
    ASSERT_TRUE(canonical.value().select_vertex(
        modeling::make_id<modeling::VertexTag>(static_cast<std::uint32_t>(selected_id))).ok());
    ASSERT_TRUE(canonical.value().transform_selection(
        {.origin = {local_delta.x, local_delta.y, local_delta.z}}).ok());
    const auto first_compiled = MeshProjectionService::compile_sdk_document_mesh(canonical.value());
    ASSERT_TRUE(first_compiled.has_value());
    ASSERT_NE(first_compiled->authored_polygon_document, nullptr);
    first_documents.push_back(*first_compiled->authored_polygon_document);
    const auto geometry = effective_preview_geometry(*preview);
    ASSERT_NE(geometry, nullptr);
    EXPECT_TRUE(mesh_geometry_payload_equal(*geometry, first_compiled->geometry()));
    const auto *selected = quader_poly::find_vertex(first_documents.back(), selected_id);
    ASSERT_NE(selected, nullptr);
    const auto displacement = transform_vector(world, selected->position - before.vertices.front().position);
    EXPECT_NEAR(displacement.x, moved.value().move_delta.x, 0.00001F);
    EXPECT_NEAR(displacement.y, moved.value().move_delta.y, 0.00001F);
    EXPECT_NEAR(displacement.z, moved.value().move_delta.z, 0.00001F);
    ASSERT_TRUE(canonical.value().transform_selection(
        {.origin = {local_delta.x, local_delta.y, local_delta.z}}).ok());
    const auto compiled = MeshProjectionService::compile_sdk_document_mesh(canonical.value());
    ASSERT_TRUE(compiled.has_value());
    ASSERT_NE(compiled->authored_polygon_document, nullptr);
    repeated_documents.push_back(*compiled->authored_polygon_document);
  }
  ASSERT_TRUE(editor_.modeling().commit_transform_drag().ok());
  const auto repeated = editor_.modeling().repeat_last_action();
  ASSERT_TRUE(repeated.ok()) << repeated.error_message();
  for (std::size_t index = 0u; index < objects.size(); ++index) {
    const auto *binding = session.scene().mesh(objects[index].entity);
    ASSERT_NE(binding, nullptr);
    const auto *mesh = session.assets().mesh(binding->mesh);
    ASSERT_TRUE(mesh != nullptr && mesh->authored_polygon_document != nullptr);
    expect_same_authored_topology(*mesh->authored_polygon_document, repeated_documents[index]);
  }
  ASSERT_TRUE(editor_.history().undo().ok());
  for (std::size_t index = 0u; index < objects.size(); ++index) {
    const auto *binding = session.scene().mesh(objects[index].entity);
    ASSERT_NE(binding, nullptr);
    const auto *mesh = session.assets().mesh(binding->mesh);
    ASSERT_TRUE(mesh != nullptr && mesh->authored_polygon_document != nullptr);
    expect_same_authored_topology(*mesh->authored_polygon_document, first_documents[index]);
  }
}

TEST_F(ComponentTransformPersistenceTests, EquivalentAffineParticipantsShareOnlyDerivedGeometry) {
  auto &session = *project_.active_scene()->session;
  std::vector<QuaderModelingObjectRef> objects{object_};
  for (int index = 0; index < 2; ++index) {
    const auto created = editor_.modeling().create_box(
        {.min = {-1, -1, -1}, .max = {1, 1, 1}}, "Equivalent edit participant");
    ASSERT_TRUE(created.ok()) << created.error_message();
    objects.push_back(created.value());
  }
  const auto authored = [&](QuaderModelingObjectRef object) {
    const auto *binding = session.scene().mesh(object.entity);
    const auto *mesh = binding ? session.assets().mesh(binding->mesh) : nullptr;
    EXPECT_TRUE(mesh && mesh->authored_polygon_document);
    return mesh && mesh->authored_polygon_document
        ? *mesh->authored_polygon_document : quader_poly::Document{};
  };
  const ModelingRay pick_ray{.origin = {0, 0, 10}, .direction = {0, 0, -1}};
  const auto expect_family_pick_work = [&](std::uint64_t family_count,
                                           std::string_view stage) {
    SCOPED_TRACE(stage);
    const auto before_builds = session.modeling().local_face_geometry_build_count();
    const auto distance = session.modeling().pick_mesh_surface_distance(pick_ray);
    ASSERT_TRUE(distance.has_value());
    EXPECT_DOUBLE_EQ(*distance, 9.0);
    const auto after_builds = session.modeling().local_face_geometry_build_count();
    EXPECT_LE(after_builds - before_builds, family_count);
    EXPECT_EQ(session.modeling().pick_mesh_surface_distance(pick_ray), distance);
    EXPECT_EQ(session.modeling().local_face_geometry_build_count(), after_builds);
  };
  std::vector<quader_poly::Document> before;
  std::vector<quader_poly::ElementId> selected;
  enter_edit_for_all_meshes();
  ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Vertex).ok());
  for (std::size_t index = 0U; index < objects.size(); ++index) {
    before.push_back(authored(objects[index]));
    ASSERT_GE(before.back().vertices.size(), 2U);
    selected.push_back(before.back().vertices[index == 2U ? 1U : 0U].id);
    ASSERT_TRUE(editor_.selection().select_components({
        .object = objects[index], .kind = QuaderComponentKind::Vertex,
        .vertices = {static_cast<QuaderElementId>(selected.back())},
        .edit = index == 0U ? QuaderSelectionEdit::Replace : QuaderSelectionEdit::Add}).ok());
  }
  ASSERT_TRUE(editor_.modeling().begin_transform_preview().ok());
  std::vector<ModelingToolPreviewMesh> pinned;
  for (const float amount : {0.125F, 0.25F, 0.0F, 0.375F}) {
    ASSERT_TRUE(editor_.modeling().update_transform_translate({amount, 0, 0}).ok());
    const auto previews = session.modeling().tool_preview_meshes();
    ASSERT_EQ(previews.size(), objects.size());
    const auto find_preview = [&](QuaderModelingObjectRef object) {
      return std::ranges::find(previews, object.entity, &ModelingToolPreviewMesh::source_entity);
    };
    const auto first = find_preview(objects[0]);
    const auto second = find_preview(objects[1]);
    const auto distinct = find_preview(objects[2]);
    ASSERT_NE(first, previews.end());
    ASSERT_NE(second, previews.end());
    ASSERT_NE(distinct, previews.end());
    EXPECT_EQ(effective_preview_geometry(*first), effective_preview_geometry(*second));
    if (amount != 0.0F) {
      EXPECT_NE(effective_preview_geometry(*first), effective_preview_geometry(*distinct));
    }
    for (std::size_t index = 0U; index < objects.size(); ++index) {
      auto expected = before[index];
      quader_poly::find_vertex(expected, selected[index])->position.x += amount;
      const auto preview = find_preview(objects[index]);
      ASSERT_NE(preview, previews.end());
      expect_preview_matches_document(*preview, expected);
      expect_same_authored_topology(authored(objects[index]), before[index]);
    }
    if (pinned.empty()) pinned = previews;
    for (const auto &preview : pinned) {
      const auto object = std::ranges::find(objects, preview.source_entity,
                                           &QuaderModelingObjectRef::entity);
      ASSERT_NE(object, objects.end());
      const auto index = static_cast<std::size_t>(object - objects.begin());
      auto expected = before[index];
      quader_poly::find_vertex(expected, selected[index])->position.x += 0.125F;
      expect_preview_matches_document(preview, expected);
    }
  }
  ASSERT_TRUE(editor_.modeling().commit_transform_preview().ok());
  std::vector<quader_poly::Document> accepted;
  for (std::size_t index = 0U; index < objects.size(); ++index) {
    auto expected = before[index];
    quader_poly::find_vertex(expected, selected[index])->position.x += 0.375F;
    accepted.push_back(authored(objects[index]));
    expect_same_authored_topology(accepted.back(), expected);
  }
  // Two distinct accepted shapes must cost at most two local triangulations,
  // regardless of the independent objects and SDK documents presenting them.
  expect_family_pick_work(2U, "Accepted equivalent transform families");
  std::vector<std::shared_ptr<const quader_poly::Document>> accepted_pins;
  for (const auto object : objects) {
    const auto *binding = session.scene().mesh(object.entity);
    ASSERT_NE(binding, nullptr);
    const auto *mesh = session.assets().mesh(binding->mesh);
    ASSERT_NE(mesh, nullptr);
    accepted_pins.push_back(mesh->authored_polygon_document);
  }
  ASSERT_TRUE(editor_.history().undo().ok());
  for (std::size_t index = 0U; index < objects.size(); ++index) {
    expect_same_authored_topology(authored(objects[index]), before[index]);
  }
  EXPECT_EQ(session.modeling().pick_mesh_surface_distance(pick_ray), 9.0);
  ASSERT_TRUE(editor_.history().redo().ok());
  for (std::size_t index = 0U; index < objects.size(); ++index) {
    expect_same_authored_topology(authored(objects[index]), accepted[index]);
  }
  expect_family_pick_work(2U, "Replayed equivalent transform families");
  ASSERT_TRUE(editor_.modeling().begin_transform_preview().ok());
  ASSERT_TRUE(editor_.modeling().update_transform_translate({0.5F, 0, 0}).ok());
  ASSERT_TRUE(editor_.modeling().cancel_transform_preview().ok());
  for (std::size_t index = 0U; index < objects.size(); ++index) {
    expect_same_authored_topology(authored(objects[index]), accepted[index]);
  }
  for (const auto &preview : pinned) {
    const auto object = std::ranges::find(objects, preview.source_entity,
                                         &QuaderModelingObjectRef::entity);
    ASSERT_NE(object, objects.end());
    const auto index = static_cast<std::size_t>(object - objects.begin());
    auto expected = before[index];
    quader_poly::find_vertex(expected, selected[index])->position.x += 0.125F;
    expect_preview_matches_document(preview, expected);
  }
  ASSERT_TRUE(editor_.selection().select_vertex(objects[0],
      static_cast<QuaderElementId>(selected[0])).ok());
  ASSERT_TRUE(editor_.modeling().begin_transform_preview().ok());
  ASSERT_TRUE(editor_.modeling().update_transform_translate({0.125F, 0, 0}).ok());
  ASSERT_TRUE(editor_.modeling().commit_transform_preview().ok());
  auto independent = accepted[0];
  quader_poly::find_vertex(independent, selected[0])->position.x += 0.125F;
  expect_same_authored_topology(authored(objects[0]), independent);
  for (std::size_t index = 0U; index < objects.size(); ++index) {
    ASSERT_NE(accepted_pins[index], nullptr);
    expect_same_authored_topology(*accepted_pins[index], accepted[index]);
    if (index != 0U) {
      expect_same_authored_topology(authored(objects[index]), accepted[index]);
    }
  }
  expect_family_pick_work(3U, "Independently edited participant");
  ASSERT_TRUE(editor_.history().undo().ok());
  for (std::size_t index = 0U; index < objects.size(); ++index) {
    expect_same_authored_topology(authored(objects[index]), accepted[index]);
  }
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_same_authored_topology(authored(objects[0]), independent);
  for (std::size_t index = 1U; index < objects.size(); ++index) {
    expect_same_authored_topology(authored(objects[index]), accepted[index]);
  }
  expect_family_pick_work(3U, "Replayed independent participant edit");
}

TEST_F(ComponentTransformPersistenceTests, ParallelAffineRejectionPreservesEveryAcceptedParticipant) {
  auto &session = *project_.active_scene()->session;
  std::vector<QuaderModelingObjectRef> objects{object_};
  constexpr std::size_t kParticipantCount = 72U;
  for (std::size_t index = 1U; index < kParticipantCount; ++index) {
    const auto created = editor_.modeling().create_box(
        {.min = {index + 1U == kParticipantCount ? 0.0F : -1.0F, -1, -1},
         .max = {1, 1, 1}}, "Affine batch participant");
    ASSERT_TRUE(created.ok()) << created.error_message();
    objects.push_back(created.value());
  }
  const auto authored = [&](QuaderModelingObjectRef object) {
    const auto *binding = session.scene().mesh(object.entity);
    const auto *mesh = binding != nullptr ? session.assets().mesh(binding->mesh) : nullptr;
    EXPECT_TRUE(mesh != nullptr && mesh->authored_polygon_document != nullptr);
    return mesh != nullptr && mesh->authored_polygon_document
               ? *mesh->authored_polygon_document : quader_poly::Document{};
  };
  std::vector<quader_poly::Document> before;
  enter_edit_for_all_meshes();
  ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Face).ok());
  for (const auto object : objects) {
    before.push_back(authored(object));
    const auto &document = before.back();
    const auto face = std::ranges::find_if(document.faces, [&](const auto &candidate) {
      return std::ranges::all_of(candidate.vertices, [&](const auto id) {
        return quader_poly::find_vertex(document, id)->position.x == 1.0F;
      });
    });
    ASSERT_NE(face, document.faces.end());
    ASSERT_TRUE(editor_.selection().select_components({
        .object = object, .kind = QuaderComponentKind::Face,
        .faces = {static_cast<QuaderElementId>(face->id)},
        .edit = before.size() == 1U ? QuaderSelectionEdit::Replace
                                    : QuaderSelectionEdit::Add}).ok());
  }
  ASSERT_EQ(editor_.selection().summary().selected_object_count, kParticipantCount);
  ASSERT_TRUE(editor_.modeling().begin_transform_preview().ok());
  ASSERT_TRUE(editor_.modeling().update_transform_translate({0.2F, 0, 0}).ok());
  const auto accepted = session.modeling().tool_preview_meshes();
  ASSERT_EQ(accepted.size(), kParticipantCount);
  auto expected = before;
  for (auto &document : expected) {
    for (auto &vertex : document.vertices) {
      if (vertex.position.x == 1.0F) vertex.position.x += 0.2F;
    }
  }
  const auto rejected = editor_.modeling().update_transform_matrix({
      .x_axis = {0, 0, 0}, .y_axis = {0, 1, 0}, .z_axis = {0, 0, 1}}, {});
  EXPECT_FALSE(rejected.ok());
  const auto retained = session.modeling().tool_preview_meshes();
  ASSERT_EQ(retained.size(), accepted.size());
  for (std::size_t index = 0U; index < accepted.size(); ++index) {
    EXPECT_EQ(retained[index].source_entity, accepted[index].source_entity);
    EXPECT_EQ(retained[index].position_revision, accepted[index].position_revision);
    EXPECT_EQ(retained[index].geometry_projection, accepted[index].geometry_projection);
    EXPECT_EQ(effective_preview_geometry(retained[index]),
              effective_preview_geometry(accepted[index]));
    EXPECT_EQ(retained[index].dynamic_position_payload, accepted[index].dynamic_position_payload);
    ASSERT_EQ(retained[index].source_entity, objects[index].entity);
    expect_preview_matches_document(retained[index], expected[index]);
    expect_same_authored_topology(authored(objects[index]), before[index]);
  }
  const auto unchanged = editor_.modeling().update_transform_translate({0.2F, 0, 0});
  ASSERT_TRUE(unchanged.ok()) << unchanged.error_message();
  EXPECT_FALSE(unchanged.changed());
  ASSERT_TRUE(editor_.modeling().commit_transform_preview().ok());
  for (std::size_t index = 0U; index < objects.size(); ++index) {
    expect_same_authored_topology(authored(objects[index]), expected[index]);
  }
  ASSERT_TRUE(editor_.history().undo().ok());
  for (std::size_t index = 0U; index < objects.size(); ++index) {
    expect_same_authored_topology(authored(objects[index]), before[index]);
  }
  ASSERT_TRUE(editor_.history().redo().ok());
  for (std::size_t index = 0U; index < objects.size(); ++index) {
    expect_same_authored_topology(authored(objects[index]), expected[index]);
  }
}

TEST_F(ComponentTransformPersistenceTests, AffineOverflowPreservesAcceptedPreviewAndCanRecover) {
  const auto original = topology();
  const auto first_vertex = std::ranges::find_if(original.vertices, [](const auto &vertex) {
    return vertex.position.x == 1.0F;
  });
  ASSERT_NE(first_vertex, original.vertices.end());
  enter_edit_for_all_meshes();
  ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Vertex).ok());
  ASSERT_TRUE(editor_.selection().select_vertex(
      object_, static_cast<QuaderElementId>(first_vertex->id)).ok());
  ASSERT_TRUE(editor_.modeling().move_selection({-1, 0, 0}).ok());
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Object).ok());
  const auto created = editor_.modeling().create_box(
      {.min = {0, 0, 0}, .max = {2, 1, 1}}, "Overflow participant");
  ASSERT_TRUE(created.ok());
  auto &session = *project_.active_scene()->session;
  enter_edit_for_all_meshes();
  ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Vertex).ok());
  for (const auto object : {object_, created.value()}) {
    const auto *binding = session.scene().mesh(object.entity);
    ASSERT_NE(binding, nullptr);
    const auto *mesh = session.assets().mesh(binding->mesh);
    ASSERT_NE(mesh, nullptr);
    ASSERT_NE(mesh->authored_polygon_document, nullptr);
    const auto &vertices = mesh->authored_polygon_document->vertices;
    const auto selected = std::ranges::find_if(vertices, [&](const auto &vertex) {
      return vertex.position.x == (object.entity == object_.entity ? 0.0F : 2.0F);
    });
    ASSERT_NE(selected, vertices.end());
    ASSERT_TRUE(editor_.selection().select_components({
        .object = object, .kind = QuaderComponentKind::Vertex,
        .vertices = {static_cast<QuaderElementId>(selected->id)},
        .edit = object.entity == object_.entity ? QuaderSelectionEdit::Replace
                                                : QuaderSelectionEdit::Add}).ok());
  }
  ASSERT_TRUE(editor_.modeling().begin_transform_preview().ok());
  ASSERT_TRUE(editor_.modeling().update_transform_translate({0.2F, 0, 0}).ok());
  const auto accepted = session.modeling().tool_preview_meshes();
  const auto rejected = editor_.modeling().update_transform_matrix({
      .x_axis = {std::numeric_limits<float>::max(), 0, 0},
      .y_axis = {0, 1, 0}, .z_axis = {0, 0, 1}}, {});
  EXPECT_FALSE(rejected.ok());
  EXPECT_EQ(rejected.error_message(), "Vertex position updates must contain finite coordinates.");
  const auto retained = session.modeling().tool_preview_meshes();
  ASSERT_EQ(retained.size(), accepted.size());
  for (std::size_t index = 0U; index < accepted.size(); ++index) {
    EXPECT_EQ(retained[index].position_revision, accepted[index].position_revision);
    EXPECT_EQ(retained[index].geometry_projection, accepted[index].geometry_projection);
    EXPECT_EQ(effective_preview_geometry(retained[index]),
              effective_preview_geometry(accepted[index]));
    EXPECT_EQ(retained[index].dynamic_position_payload, accepted[index].dynamic_position_payload);
  }
  ASSERT_TRUE(editor_.modeling().update_transform_translate({0.4F, 0, 0}).ok());
  ASSERT_TRUE(editor_.modeling().cancel_transform_preview().ok());
}

/// Retains the production coordinator's submitted component geometry without
/// substituting GPU timings for this CPU workflow measurement.
class ComponentDragSubmission final : public ViewportOverlaySelectionFrameProvider,
                                      public ViewportOverlaySubmissionTarget {
public:
  explicit ComponentDragSubmission(SceneSession &session) : session_(session) {}
  ViewportOverlayFrame build_selection_overlay_frame() const override {
    ++builds;
    return session_.modeling().selection_overlay_frame();
  }
  ViewportOverlayCameraUpdate update_overlay_cameras(
      std::size_t, std::uint8_t, ViewportOverlayCamera) override {
    return {true, true};
  }
  void submit_persistent_overlay(std::size_t, std::uint8_t,
      ViewportOverlayFrame frame, ViewportOverlayCamera) override {
    submitted = std::move(frame);
    ++submissions;
  }
  void submit_dynamic_overlay(std::size_t, std::uint8_t,
      ViewportOverlayFrame, ViewportOverlayCamera) override {}
  ViewportOverlayFrame submitted;
  mutable std::size_t builds = 0U;
  std::size_t submissions = 0U;

private:
  SceneSession &session_;
};

TEST_F(ComponentTransformPersistenceTests, AffinePreviewRetainsSemanticProjectionAcrossChangedSamples) {
  auto &session = *project_.active_scene()->session;
  auto &modeling = session.modeling();
  for (const auto mode : {QuaderSelectionMode::Vertex, QuaderSelectionMode::Edge,
                          QuaderSelectionMode::Face}) {
    SCOPED_TRACE(static_cast<int>(mode));
    ASSERT_TRUE(editor_.selection().clear().ok());
    enter_edit_for_all_meshes();
    ASSERT_TRUE(editor_.selection().set_mode(mode).ok());
    const auto document = topology();
    const auto &face = document.faces.front();
    const auto select = [&] {
      if (mode == QuaderSelectionMode::Vertex) {
        return editor_.selection().select_vertex(object_,
            static_cast<QuaderElementId>(face.vertices.front()));
      }
      if (mode == QuaderSelectionMode::Edge) {
        return editor_.selection().select_edge(object_,
            static_cast<QuaderElementId>(face.vertices[0]),
            static_cast<QuaderElementId>(face.vertices[1]));
      }
      return editor_.selection().select_face(object_, static_cast<QuaderElementId>(face.id));
    };
    ASSERT_TRUE(select().ok());
    const auto context = mode == QuaderSelectionMode::Vertex
        ? ViewportOverlaySelectionContext::Vertex : mode == QuaderSelectionMode::Edge
        ? ViewportOverlaySelectionContext::Edge : ViewportOverlaySelectionContext::Face;
    ComponentDragSubmission target(session);
    ViewportOverlaySubmissionCoordinator coordinator;
    SceneRenderBridge bridge;
    std::vector<RenderToolPreviewMeshRecord> published;
    const QuaderRenderApiRuntimeAccess::ToolPreviewScenePublisher publisher =
        [&](RenderToolPreviewSceneSnapshot records) {
          published = std::move(records.meshes);
          return ToolPreviewSubmissionResult::Published;
        };
    const auto submit = [&] {
      EXPECT_TRUE(QuaderRenderApiRuntimeAccess::sync_tool_previews(
          editor_.render(), bridge, {}, publisher).ok());
      return coordinator.submit({
          .viewport_index = 0U, .layer_mask = 1U,
          .selection_source = {
              .source_identity = reinterpret_cast<std::uintptr_t>(&modeling),
              .content_revision = modeling.content_revision(),
              .selection_revision = modeling.selection_revision(),
              .scene_revision = session.scene().revision(),
              .asset_revision = session.assets().revision(),
              .overlay_signature = modeling.selection_overlay_signature(),
              .selection_context = context},
          .selection_frame_provider = &target,
          .scene_selection_mode = RenderOverlaySelectionMode::Component,
          .style_selection_context = context, .overlays_visible = true,
          .selection_dimensions_visible = false}, ViewportSettings::defaults(), target);
    };
    ASSERT_TRUE(submit().persistent_submitted);
    ASSERT_TRUE(editor_.modeling().begin_transform_preview().ok());
    ASSERT_TRUE(editor_.modeling().update_transform_translate({0.125F, 0.0F, 0.0F}).ok());
    ASSERT_TRUE(submit().persistent_submitted);
    const auto builds = target.builds;
    const auto submissions = target.submissions;
    const auto projections = modeling.modeling_sdk_projection_stats_for_tests()
                                 .native_selection_projection_build_count;
    for (const float amount : {0.25F, 0.375F, 0.0F}) {
      SCOPED_TRACE(amount);
      const auto previous_revision = modeling.transient_transform_preview_revision();
      const auto updated = editor_.modeling().update_transform_translate({amount, 0.0F, 0.0F});
      ASSERT_TRUE(updated.ok()) << updated.error_message();
      // Changed describes the candidate relative to its gesture base. Returning
      // to zero still uploads positions while leaving no edit to commit.
      EXPECT_EQ(updated.changed(), amount != 0.0F);
      const auto submission = submit();
      EXPECT_FALSE(submission.selection_source_refreshed);
      EXPECT_FALSE(submission.persistent_submitted);
      EXPECT_EQ(target.builds, builds);
      EXPECT_EQ(target.submissions, submissions);
      EXPECT_EQ(modeling.modeling_sdk_projection_stats_for_tests()
                    .native_selection_projection_build_count, projections);
      EXPECT_GT(modeling.transient_transform_preview_revision(), previous_revision);
      ASSERT_EQ(published.size(), 1U);
      const auto &preview = published.front();
      ASSERT_NE(preview.dynamic_position_payload, nullptr);
      const auto geometry = effective_preview_geometry(preview);
      ASSERT_NE(geometry, nullptr);
      ASSERT_EQ(preview.dynamic_position_payload->size(), geometry->vertices.size());
      for (std::size_t index = 0U; index < geometry->vertices.size(); ++index) {
        const auto &vertex = geometry->vertices[index];
        const auto original = std::ranges::find(document.vertices, vertex.source_vertex_id,
                                                &quader_poly::Vertex::id);
        ASSERT_NE(original, document.vertices.end());
        const bool selected = mode == QuaderSelectionMode::Vertex
            ? original->id == face.vertices.front()
            : mode == QuaderSelectionMode::Edge
            ? original->id == face.vertices[0] || original->id == face.vertices[1]
            : std::ranges::find(face.vertices, original->id) != face.vertices.end();
        const auto position = (*preview.dynamic_position_payload)[index];
        EXPECT_EQ(position.x, original->position.x + (selected ? amount : 0.0F));
        EXPECT_EQ(position.y, original->position.y);
        EXPECT_EQ(position.z, original->position.z);
        EXPECT_EQ(vertex.position.x, position.x);
        EXPECT_EQ(vertex.position.y, position.y);
        EXPECT_EQ(vertex.position.z, position.z);
      }
      expect_same_authored_topology(topology(), document);
      if (amount == 0.375F) {
        const auto accepted_revision = modeling.transient_transform_preview_revision();
        const auto accepted_positions = preview.dynamic_position_payload;
        EXPECT_FALSE(editor_.modeling().update_transform_translate(
            {std::numeric_limits<float>::infinity(), 0.0F, 0.0F}).ok());
        EXPECT_FALSE(submit().persistent_submitted);
        EXPECT_EQ(modeling.transient_transform_preview_revision(), accepted_revision);
        ASSERT_EQ(published.size(), 1U);
        EXPECT_EQ(published.front().dynamic_position_payload, accepted_positions);
      }
    }
    ASSERT_TRUE(editor_.modeling().cancel_transform_preview().ok());
    EXPECT_TRUE(submit().persistent_submitted);
    EXPECT_TRUE(published.empty());
    expect_same_authored_topology(topology(), document);
    ASSERT_TRUE(editor_.modeling().begin_transform_preview().ok());
    ASSERT_TRUE(editor_.modeling().update_transform_translate({0.125F, 0.0F, 0.0F}).ok());
    EXPECT_TRUE(submit().persistent_submitted);
    ASSERT_TRUE(editor_.modeling().commit_transform_preview().ok());
    EXPECT_TRUE(submit().persistent_submitted);
    ASSERT_TRUE(editor_.history().undo().ok());
    EXPECT_TRUE(submit().persistent_submitted);
    ASSERT_TRUE(editor_.selection().clear().ok());
    EXPECT_TRUE(submit().persistent_submitted);
    ASSERT_TRUE(select().ok());
    EXPECT_TRUE(submit().persistent_submitted);
    EXPECT_FALSE(submit().persistent_submitted);
  }
}

TEST(ComponentTransformPerformanceTests, ClonedLargeSceneComponentDragSubmission) {
  const char *source = std::getenv("QUADER_TEST_LARGE_SELECTION_PROJECT");
  if (source == nullptr || *source == '\0') {
    GTEST_SKIP() << "Set QUADER_TEST_LARGE_SELECTION_PROJECT to measure a cloned project.";
  }
  const char *backfaces = std::getenv("QUADER_TEST_COMPONENT_DRAG_BACKFACES");
  const bool select_backfaces = backfaces == nullptr || std::string_view(backfaces) != "0";
  const std::filesystem::path source_project = std::filesystem::u8path(source);
  ASSERT_TRUE(std::filesystem::is_regular_file(source_project));
  const char *source_scene = std::getenv("QUADER_TEST_LARGE_SELECTION_SCENE");
  const auto original_scene = source_scene != nullptr && *source_scene != '\0'
      ? std::filesystem::u8path(source_scene)
      : source_project.parent_path() / "Scenes" / "one.qdr";
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const std::filesystem::path root(temporary.path().toStdWString());
  const auto project_root = root / source_project.parent_path().filename();
  std::error_code error;
  std::filesystem::copy(source_project.parent_path(), project_root,
      std::filesystem::copy_options::recursive |
          std::filesystem::copy_options::skip_symlinks, error);
  ASSERT_FALSE(error) << error.message();
  const auto relative_scene = std::filesystem::relative(
      original_scene, source_project.parent_path(), error);
  ASSERT_FALSE(error) << error.message();
  ProjectSession project(root / "recent-scenes.json");
  project.set_resource_root(std::filesystem::path(QUADER_SOURCE_DIR) /
      "resources/modeling_assets");
  QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.project().open(project_root / source_project.filename()).ok());
  ASSERT_TRUE(editor.scene_documents().open_native_path(project_root / relative_scene).ok());
  auto &session = *project.active_scene()->session;
  auto &modeling = session.modeling();
  ASSERT_GE(modeling.modeling_sdk_object_count_for_tests(), 6'000u);
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(editor.selection().select_all().ok());
  const auto bounds = modeling.selection_bounds();
  ASSERT_TRUE(bounds.has_value());
  const QVec3 extent = bounds->max - bounds->min;
  const QVec3 center = (bounds->min + bounds->max) * 0.5f;
  const float framing_size = std::max({extent.x, extent.y, extent.z, 1.0f});
  SceneRenderBridge bridge;
  std::vector<RenderToolPreviewMeshRecord> published;
  const QuaderRenderApiRuntimeAccess::ToolPreviewScenePublisher publisher =
      [&](RenderToolPreviewSceneSnapshot records) {
        published = std::move(records.meshes);
        return ToolPreviewSubmissionResult::Published;
      };
  const QuaderViewportCamera camera{
      .orthographic = true, .origin = {center.x, center.y, bounds->max.z + framing_size},
      .x_axis = {1.0f, 0.0f, 0.0f}, .y_axis = {0.0f, 1.0f, 0.0f},
      .z_axis = {0.0f, 0.0f, 1.0f}, .viewport_size = {100.0f, 100.0f},
      .orthographic_size = framing_size * 1.1 + 1.0, .fov_degrees = 60.0};
  const QuaderRay ray{.origin = camera.origin, .direction = {0.0f, 0.0f, -1.0f}};
  const auto elapsed = [](std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
  };
  ASSERT_TRUE(editor.mode().set(QuaderEditorMode::Edit).ok());
  for (const auto mode : {QuaderSelectionMode::Vertex, QuaderSelectionMode::Edge,
                          QuaderSelectionMode::Face}) {
    SCOPED_TRACE(static_cast<int>(mode));
    ASSERT_TRUE(editor.selection().set_mode(mode).ok());
    ASSERT_TRUE(editor.selection().clear().ok());
    const auto selected_components = editor.selection().box_select({
        .camera = camera, .start = camera.viewport_size, .end = {},
        .edit = QuaderSelectionEdit::Replace, .rule = QuaderBoxSelectionRule::Crossing,
        .select_backfaces = select_backfaces});
    ASSERT_TRUE(selected_components.ok()) << selected_components.error_message();
    const auto selected = editor.selection().summary();
    ASSERT_TRUE(selected.has_selection);
    ASSERT_GE(selected.selected_object_count, select_backfaces ? 6'000U : 3'000U);
    ASSERT_GT(mode == QuaderSelectionMode::Vertex ? selected.selected_vertex_count
        : mode == QuaderSelectionMode::Edge ? selected.selected_edge_count
                                           : selected.selected_face_count, 0u);
    const auto context = mode == QuaderSelectionMode::Vertex
        ? ViewportOverlaySelectionContext::Vertex : mode == QuaderSelectionMode::Edge
        ? ViewportOverlaySelectionContext::Edge : ViewportOverlaySelectionContext::Face;
    ComponentDragSubmission target(session);
    ViewportOverlaySubmissionCoordinator coordinator;
    auto render_snapshot = build_render_scene_snapshot(session.scene(), session.assets());
    const auto submit = [&] {
      return coordinator.submit({
          .viewport_index = 0u, .layer_mask = 1u,
          .selection_source = {
              .source_identity = reinterpret_cast<std::uintptr_t>(&modeling),
              .content_revision = modeling.content_revision(),
              .selection_revision = modeling.selection_revision(),
              .scene_revision = session.scene().revision(),
              .asset_revision = session.assets().revision(),
              .overlay_signature = modeling.selection_overlay_signature(),
              .selection_context = context},
          .selection_frame_provider = &target,
          .scene_selection_mode = RenderOverlaySelectionMode::Component,
          .style_selection_context = context, .overlays_visible = true,
          .selection_dimensions_visible = false}, ViewportSettings::defaults(), target);
    };
    (void)submit();
    auto start = std::chrono::steady_clock::now();
    const auto begun = editor.modeling().begin_transform_drag({
        .tool = QuaderTransformTool::Move, .operation = QuaderTransformOperation::Transform,
        .axis = QuaderTransformAxis::X, .screen_position = {}, .screen_pivot = {},
        .screen_tip = {10.0f, 0.0f}, .has_world_direction = true,
        .world_direction = {1.0f, 0.0f, 0.0f}, .ray = ray, .camera = camera,
        .grid_size = 1.0, .vertex_snap_radius = 0.0, .grid_snap_enabled = false});
    const double begin_ms = elapsed(start);
    ASSERT_TRUE(begun.ok()) << begun.error_message();
    ASSERT_TRUE(begun.value().active);
    ASSERT_TRUE(begun.value().preview_driven);
    std::cout << "COMPONENT_DRAG mode=" << static_cast<int>(mode)
              << " backfaces=" << select_backfaces
              << " phase=begin ms=" << begin_ms
              << " objects=" << selected.selected_object_count
              << " vertices=" << selected.selected_vertex_count
              << " edges=" << selected.selected_edge_count
              << " faces=" << selected.selected_face_count << std::endl;
    const auto scene_revision = session.scene().revision();
    const auto asset_revision = session.assets().revision();
    for (int sample = 0; sample < 5; ++sample) {
      const QuaderTransformDragUpdateRequest request{
          .screen_position = {0.25f * static_cast<float>(sample + 1), 0.0f},
          .ray = ray, .camera = camera, .grid_size = 1.0,
          .vertex_snap_radius = 0.0, .grid_snap_enabled = false};
      const auto projection_before = MeshProjectionService::position_update_stats();
      start = std::chrono::steady_clock::now();
      const auto updated = editor.modeling().update_transform_drag(request);
      const double update_ms = elapsed(start);
      const auto projection_after = MeshProjectionService::position_update_stats();
      ASSERT_TRUE(updated.ok()) << updated.error_message();
      ASSERT_TRUE(updated.changed());
      start = std::chrono::steady_clock::now();
      ASSERT_TRUE(QuaderRenderApiRuntimeAccess::sync_tool_previews(
          editor.render(), bridge, {}, publisher).ok());
      const double preview_extraction_ms = elapsed(start);
      ASSERT_FALSE(published.empty());
      start = std::chrono::steady_clock::now();
      (void)submit();
      const double overlay_submission_ms = elapsed(start);
      EXPECT_FALSE(target.submitted.lines.empty());
      EXPECT_TRUE(render_scene_snapshot_matches_sources(
          render_snapshot, session.scene(), session.assets()));
      EXPECT_EQ(session.scene().revision(), scene_revision);
      EXPECT_EQ(session.assets().revision(), asset_revision);
      const auto revision = modeling.transient_transform_preview_revision();
      const auto work = modeling.tool_preview_work_stats();
      start = std::chrono::steady_clock::now();
      const auto repeated = editor.modeling().update_transform_drag(request);
      ASSERT_TRUE(repeated.ok()) << repeated.error_message();
      EXPECT_FALSE(repeated.changed());
      ASSERT_TRUE(QuaderRenderApiRuntimeAccess::sync_tool_previews(
          editor.render(), bridge, {}, publisher).ok());
      EXPECT_FALSE(submit().persistent_submitted);
      const double unchanged_ms = elapsed(start);
      EXPECT_EQ(modeling.transient_transform_preview_revision(), revision);
      EXPECT_EQ(modeling.tool_preview_work_stats().mesh_extraction_count,
                work.mesh_extraction_count);
      std::cout << "COMPONENT_DRAG mode=" << static_cast<int>(mode)
                << " backfaces=" << select_backfaces
                << " sample=" << sample << " update_ms=" << update_ms
                << " preview_extraction_ms=" << preview_extraction_ms
                << " overlay_submission_ms=" << overlay_submission_ms
                << " unchanged_ms=" << unchanged_ms
                << " preview_meshes=" << published.size()
                << " retained_translation=" << projection_after.retained_translation_count -
                    projection_before.retained_translation_count
                << " retained_partial=" << projection_after.retained_partial_count -
                    projection_before.retained_partial_count
                << " partial_compiled_faces=" << projection_after.partial_compiled_face_count -
                    projection_before.partial_compiled_face_count
                << " retained_flat=" << projection_after.retained_flat_face_count -
                    projection_before.retained_flat_face_count
                << " full_compile=" << projection_after.full_compile_count -
                    projection_before.full_compile_count << std::endl;
    }
    const auto candidate_count = published.size();
    start = std::chrono::steady_clock::now();
    const auto committed = editor.modeling().commit_transform_drag();
    const double commit_ms = elapsed(start);
    ASSERT_TRUE(committed.ok()) << committed.error_message();
    EXPECT_TRUE(committed.changed());
    std::size_t retained_committed_geometry = 0U;
    // Inspect accepted pins after timing commit, keeping lazy materialization
    // inside the workflow being measured instead of warming it in the test.
    for (const auto &preview : published) {
      const auto geometry = effective_preview_geometry(preview);
      const auto *binding = session.scene().mesh(preview.source_entity);
      const auto *mesh = binding != nullptr ? session.assets().mesh(binding->mesh) : nullptr;
      if (mesh != nullptr && mesh->geometry_payload() == geometry) {
        ++retained_committed_geometry;
      }
    }
    ASSERT_TRUE(QuaderRenderApiRuntimeAccess::sync_tool_previews(
        editor.render(), bridge, {}, publisher).ok());
    EXPECT_TRUE(published.empty());
    ASSERT_TRUE(editor.history().undo().ok());
    std::cout << "COMPONENT_DRAG mode=" << static_cast<int>(mode)
              << " backfaces=" << select_backfaces
              << " phase=commit ms=" << commit_ms
              << " retained_geometry=" << retained_committed_geometry
              << " candidates=" << candidate_count << std::endl;
  }
}

} // namespace
} // namespace quader

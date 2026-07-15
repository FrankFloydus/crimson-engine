////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace qec = quader_editor_core;

namespace {

quader_poly::Document translated_cube(quader::QVec3 offset) {
  quader_poly::Document document = quader_poly::make_cube({});
  for (quader_poly::Vertex &vertex : document.vertices) {
    vertex.position += offset;
  }
  return document;
}

std::size_t vertex_count_on_axis_plane(const quader_poly::Document &document,
                                       qec::HalfCutAxis axis, float value) {
  return static_cast<std::size_t>(
      std::count_if(document.vertices.begin(), document.vertices.end(),
                    [axis, value](const quader_poly::Vertex &vertex) {
                      const float coordinate =
                          axis == qec::HalfCutAxis::X   ? vertex.position.x
                          : axis == qec::HalfCutAxis::Y ? vertex.position.y
                                                        : vertex.position.z;
                      return qdr_test::nearly_equal(coordinate, value);
                    }));
}

void expect_documents_equal(const quader_poly::Document &actual,
                            const quader_poly::Document &expected) {
  ASSERT_EQ(actual.vertices.size(), expected.vertices.size());
  for (std::size_t index = 0; index < expected.vertices.size(); ++index) {
    EXPECT_EQ(actual.vertices[index].id, expected.vertices[index].id);
    EXPECT_FLOAT_EQ(actual.vertices[index].position.x,
                    expected.vertices[index].position.x);
    EXPECT_FLOAT_EQ(actual.vertices[index].position.y,
                    expected.vertices[index].position.y);
    EXPECT_FLOAT_EQ(actual.vertices[index].position.z,
                    expected.vertices[index].position.z);
  }

  ASSERT_EQ(actual.faces.size(), expected.faces.size());
  for (std::size_t index = 0; index < expected.faces.size(); ++index) {
    const quader_poly::Face &actual_face = actual.faces[index];
    const quader_poly::Face &expected_face = expected.faces[index];
    EXPECT_EQ(actual_face.id, expected_face.id);
    EXPECT_EQ(actual_face.vertices, expected_face.vertices);
    ASSERT_EQ(actual_face.uvs.size(), expected_face.uvs.size());
    for (std::size_t uv_index = 0; uv_index < expected_face.uvs.size();
         ++uv_index) {
      EXPECT_FLOAT_EQ(actual_face.uvs[uv_index].x,
                      expected_face.uvs[uv_index].x);
      EXPECT_FLOAT_EQ(actual_face.uvs[uv_index].y,
                      expected_face.uvs[uv_index].y);
    }
    EXPECT_EQ(actual_face.material_slot, expected_face.material_slot);
    EXPECT_EQ(actual_face.normal_shading, expected_face.normal_shading);
  }

  EXPECT_EQ(actual.hard_normal_edges, expected.hard_normal_edges);
  EXPECT_EQ(actual.next_vertex_id, expected.next_vertex_id);
  EXPECT_EQ(actual.next_face_id, expected.next_face_id);
}

void expect_selections_equal(const quader_poly::Selection &actual,
                             const quader_poly::Selection &expected) {
  EXPECT_EQ(actual.mode, expected.mode);
  EXPECT_EQ(actual.vertices, expected.vertices);
  EXPECT_EQ(actual.edges, expected.edges);
  EXPECT_EQ(actual.faces, expected.faces);
  EXPECT_EQ(actual.has_active, expected.has_active);
  EXPECT_EQ(actual.active_kind, expected.active_kind);
  EXPECT_EQ(actual.active_vertex, expected.active_vertex);
  EXPECT_EQ(actual.active_edge, expected.active_edge);
  EXPECT_EQ(actual.active_face, expected.active_face);
}

void expect_objects_equal(const std::vector<qec::PolygonObject> &actual,
                          const std::vector<qec::PolygonObject> &expected) {
  ASSERT_EQ(actual.size(), expected.size());
  for (std::size_t index = 0; index < expected.size(); ++index) {
    EXPECT_EQ(actual[index].id, expected[index].id);
    EXPECT_EQ(actual[index].material_id, expected[index].material_id);
    EXPECT_EQ(actual[index].selected, expected[index].selected);
    expect_documents_equal(actual[index].document, expected[index].document);
    expect_selections_equal(actual[index].selection, expected[index].selection);
    EXPECT_EQ(actual[index].component_edit_target,
              expected[index].component_edit_target);
  }
}

void expect_session_semantics_equal(const qec::SessionState &actual,
                                    const qec::SessionState &expected) {
  expect_objects_equal(actual.objects, expected.objects);
  EXPECT_EQ(actual.lights.size(), expected.lights.size());
  EXPECT_EQ(actual.active_object_id, expected.active_object_id);
  EXPECT_EQ(actual.active_light_id, expected.active_light_id);
  EXPECT_EQ(actual.selection_mode, expected.selection_mode);
  EXPECT_EQ(actual.dirty, expected.dirty);
}

} // namespace

TEST(QdrModelingRuntimeOperationHalfCutTests,
     DefaultsAndInvalidAxesNormalizeToX) {
  const qec::HalfCutOperationSchema schema = qec::half_cut_operation_schema();
  const qec::HalfCutOperationSettings defaults;

  EXPECT_EQ(defaults.axis, qec::HalfCutAxis::X);
  EXPECT_FALSE(defaults.separate_parts);
  EXPECT_TRUE(defaults.fill_parts);
  EXPECT_EQ(schema.axis.default_value, static_cast<int>(qec::HalfCutAxis::X));
  EXPECT_EQ(schema.separate_parts.default_value, defaults.separate_parts);
  EXPECT_EQ(schema.fill_parts.default_value, defaults.fill_parts);

  const qec::HalfCutOperationSettings below_range =
      qec::normalize_half_cut_operation_settings(
          {.axis = static_cast<qec::HalfCutAxis>(-1),
           .separate_parts = true,
           .fill_parts = false});
  const qec::HalfCutOperationSettings above_range =
      qec::normalize_half_cut_operation_settings(
          {.axis = static_cast<qec::HalfCutAxis>(99)});

  EXPECT_EQ(below_range.axis, qec::HalfCutAxis::X);
  EXPECT_TRUE(below_range.separate_parts);
  EXPECT_FALSE(below_range.fill_parts);
  EXPECT_EQ(above_range.axis, qec::HalfCutAxis::X);

  qec::EditorSession session({
      qdr_test::make_object(10, translated_cube({4.0F, 6.0F, 8.0F})),
  });
  const qec::CommandResult result = session.half_cut_selected_objects(
      {.axis = static_cast<qec::HalfCutAxis>(99),
       .separate_parts = false});

  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_GE(vertex_count_on_axis_plane(session.objects().front().document,
                                       qec::HalfCutAxis::X, 4.0F),
            4U);
}

TEST(QdrModelingRuntimeOperationHalfCutTests,
     InPlaceCutUsesEachMeshLocalBoundsCenterAndPreservesObjects) {
  qec::EditorSession session({
      qdr_test::make_object(10, translated_cube({4.0F, 0.0F, 0.0F})),
      qdr_test::make_object(20, translated_cube({0.0F, 6.0F, 0.0F})),
  });
  const std::size_t first_vertices_before =
      session.objects()[0].document.vertices.size();
  const std::size_t second_vertices_before =
      session.objects()[1].document.vertices.size();

  const qec::CommandResult result = session.half_cut_selected_objects(
      {.axis = qec::HalfCutAxis::Y, .separate_parts = false});

  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_TRUE(result.changed);
  ASSERT_EQ(session.objects().size(), 2U);
  EXPECT_EQ(session.selection_mode(), qec::SelectionMode::Object);
  EXPECT_EQ(session.selected_object_ids().size(), 2U);
  EXPECT_GT(session.objects()[0].document.vertices.size(),
            first_vertices_before);
  EXPECT_GT(session.objects()[1].document.vertices.size(),
            second_vertices_before);
  EXPECT_GE(vertex_count_on_axis_plane(session.objects()[0].document,
                                       qec::HalfCutAxis::Y, 0.0F),
            4U);
  EXPECT_GE(vertex_count_on_axis_plane(session.objects()[1].document,
                                       qec::HalfCutAxis::Y, 6.0F),
            4U);
}

TEST(QdrModelingRuntimeOperationHalfCutTests,
     MultiMeshFailureLeavesEverySourceUntouched) {
  quader_poly::Document cube = translated_cube({3.0F, 0.0F, 0.0F});
  const std::size_t vertex_count_before = cube.vertices.size();
  const std::size_t face_count_before = cube.faces.size();
  qec::EditorSession session({
      qdr_test::make_object(1, std::move(cube)),
      qdr_test::make_object(2, {}),
  });

  const qec::CommandResult result = session.half_cut_selected_objects({});

  EXPECT_FALSE(result.ok);
  EXPECT_FALSE(result.changed);
  ASSERT_EQ(session.objects().size(), 2U);
  EXPECT_EQ(session.objects()[0].document.vertices.size(), vertex_count_before);
  EXPECT_EQ(session.objects()[0].document.faces.size(), face_count_before);
}

TEST(QdrModelingRuntimeOperationHalfCutTests,
     PreviewAxisUpdatesRebuildFromBaseWithoutAccumulatingCuts) {
  qec::EditorSession session({
      qdr_test::make_object(10, translated_cube({4.0F, 6.0F, 8.0F})),
  });

  const qec::CommandResult begin = session.begin_half_cut_preview(
      {.axis = qec::HalfCutAxis::X, .separate_parts = false});
  ASSERT_TRUE(begin.ok) << begin.message;
  EXPECT_TRUE(session.preview_transaction_status().active);
  EXPECT_EQ(session.preview_transaction_status().kind,
            qec::PreviewTransactionKind::HalfCut);
  EXPECT_EQ(session.objects().size(), 1U);
  const std::size_t cut_vertex_count =
      session.objects().front().document.vertices.size();
  const std::size_t cut_face_count =
      session.objects().front().document.faces.size();
  EXPECT_GE(vertex_count_on_axis_plane(session.objects().front().document,
                                       qec::HalfCutAxis::X, 4.0F),
            4U);
  EXPECT_EQ(vertex_count_on_axis_plane(session.objects().front().document,
                                       qec::HalfCutAxis::Y, 6.0F),
            0U);
  EXPECT_EQ(vertex_count_on_axis_plane(session.objects().front().document,
                                       qec::HalfCutAxis::Z, 8.0F),
            0U);

  const qec::CommandResult update_y = session.update_half_cut_preview(
      {.axis = qec::HalfCutAxis::Y, .separate_parts = false});
  ASSERT_TRUE(update_y.ok) << update_y.message;
  EXPECT_EQ(session.objects().front().document.vertices.size(),
            cut_vertex_count);
  EXPECT_EQ(session.objects().front().document.faces.size(), cut_face_count);
  EXPECT_EQ(vertex_count_on_axis_plane(session.objects().front().document,
                                       qec::HalfCutAxis::X, 4.0F),
            0U);
  EXPECT_GE(vertex_count_on_axis_plane(session.objects().front().document,
                                       qec::HalfCutAxis::Y, 6.0F),
            4U);
  EXPECT_EQ(vertex_count_on_axis_plane(session.objects().front().document,
                                       qec::HalfCutAxis::Z, 8.0F),
            0U);

  const qec::CommandResult update_z = session.update_half_cut_preview(
      {.axis = qec::HalfCutAxis::Z, .separate_parts = false});
  ASSERT_TRUE(update_z.ok) << update_z.message;
  EXPECT_EQ(session.objects().front().document.vertices.size(),
            cut_vertex_count);
  EXPECT_EQ(session.objects().front().document.faces.size(), cut_face_count);
  EXPECT_EQ(vertex_count_on_axis_plane(session.objects().front().document,
                                       qec::HalfCutAxis::X, 4.0F),
            0U);
  EXPECT_EQ(vertex_count_on_axis_plane(session.objects().front().document,
                                       qec::HalfCutAxis::Y, 6.0F),
            0U);
  EXPECT_GE(vertex_count_on_axis_plane(session.objects().front().document,
                                       qec::HalfCutAxis::Z, 8.0F),
            4U);
  EXPECT_FALSE(session.history_status().can_undo);

  const qec::PreviewTransactionFinishResult cancel =
      session.cancel_preview_transaction(qec::PreviewTransactionKind::HalfCut);
  EXPECT_TRUE(cancel.restored);
}

TEST(QdrModelingRuntimeOperationHalfCutTests,
     FillPartsIsIgnoredWhenPartsRemainInPlace) {
  const qec::PolygonObject source =
      qdr_test::make_object(10, translated_cube({4.0F, 6.0F, 8.0F}));
  qec::EditorSession filled({source});
  qec::EditorSession unfilled({source});

  const qec::CommandResult filled_result =
      filled.half_cut_selected_objects({.axis = qec::HalfCutAxis::Y,
                                        .separate_parts = false,
                                        .fill_parts = true});
  const qec::CommandResult unfilled_result =
      unfilled.half_cut_selected_objects({.axis = qec::HalfCutAxis::Y,
                                          .separate_parts = false,
                                          .fill_parts = false});

  ASSERT_TRUE(filled_result.ok) << filled_result.message;
  ASSERT_TRUE(unfilled_result.ok) << unfilled_result.message;
  ASSERT_TRUE(filled_result.changed);
  ASSERT_TRUE(unfilled_result.changed);
  ASSERT_EQ(filled.objects().size(), 1U);
  ASSERT_EQ(unfilled.objects().size(), 1U);
  expect_documents_equal(filled.objects().front().document,
                         unfilled.objects().front().document);
  EXPECT_TRUE(
      qdr_test::every_edge_is_closed(filled.objects().front().document));
}

TEST(QdrModelingRuntimeOperationHalfCutTests,
     MultiMeshSeparatedPreviewCancelRestoresSemanticsAndRetiresIds) {
  qec::PolygonObject first =
      qdr_test::make_object(10, translated_cube({4.0F, 0.0F, 0.0F}));
  qec::PolygonObject second =
      qdr_test::make_object(20, translated_cube({0.0F, 6.0F, 0.0F}));
  first.material_id = 3;
  second.material_id = 7;
  qec::EditorSession session({std::move(first), std::move(second)});
  session.set_selected_object_ids({10, 20}, 20);
  const qec::SessionState before = session.state();
  const qec::HistoryStatus history_before = session.history_status();

  const qec::CommandResult begin =
      session.begin_half_cut_preview({.axis = qec::HalfCutAxis::Y,
                                      .separate_parts = true,
                                      .fill_parts = false});
  ASSERT_TRUE(begin.ok) << begin.message;
  ASSERT_EQ(session.objects().size(), 4U);
  EXPECT_GT(session.next_object_id(), before.next_object_id);
  EXPECT_EQ(session.selected_object_ids().size(), 4U);
  for (const qec::PolygonObject &object : session.objects()) {
    EXPECT_GT(qdr_test::open_edge_count(object.document), 0U);
  }

  const qec::CommandResult update =
      session.update_half_cut_preview({.axis = qec::HalfCutAxis::Z,
                                       .separate_parts = true,
                                       .fill_parts = true});
  ASSERT_TRUE(update.ok) << update.message;
  ASSERT_EQ(session.objects().size(), 4U);
  const qec::SessionState preview = session.state();

  const qec::PreviewTransactionFinishResult cancel =
      session.cancel_preview_transaction(qec::PreviewTransactionKind::HalfCut);
  EXPECT_TRUE(cancel.before.active);
  EXPECT_TRUE(cancel.restored);
  const qec::SessionState after = session.state();
  expect_session_semantics_equal(after, before);
  EXPECT_EQ(after.next_object_id, preview.next_object_id);
  EXPECT_EQ(after.next_light_id, before.next_light_id);
  EXPECT_GT(after.content_revision, preview.content_revision);
  EXPECT_GT(after.selection_revision, preview.selection_revision);
  const qec::HistoryStatus history_after = session.history_status();
  EXPECT_EQ(history_after.can_undo, history_before.can_undo);
  EXPECT_EQ(history_after.can_redo, history_before.can_redo);
  EXPECT_EQ(history_after.undo_count, history_before.undo_count);
  EXPECT_EQ(history_after.redo_count, history_before.redo_count);
}

TEST(QdrModelingRuntimeOperationHalfCutTests,
     MultiMeshFilledSeparatedPreviewCommitsAsOneUndoableOperation) {
  qec::EditorSession session({
      qdr_test::make_object(10, translated_cube({4.0F, 0.0F, 0.0F})),
      qdr_test::make_object(20, translated_cube({0.0F, 6.0F, 0.0F})),
  });
  session.set_selected_object_ids({10, 20}, 20);
  const qec::SessionState before = session.state();

  const qec::CommandResult begin = session.begin_half_cut_preview({
      .axis = qec::HalfCutAxis::X,
      .separate_parts = true,
      .fill_parts = true,
  });
  ASSERT_TRUE(begin.ok) << begin.message;
  ASSERT_EQ(session.objects().size(), 4U);
  EXPECT_EQ(session.selected_object_ids().size(), 4U);
  for (const qec::PolygonObject &object : session.objects()) {
    EXPECT_TRUE(qdr_test::every_edge_is_closed(object.document));
  }

  const qec::PreviewTransactionFinishResult commit =
      session.commit_preview_transaction(qec::PreviewTransactionKind::HalfCut);
  EXPECT_TRUE(commit.before.active);
  EXPECT_FALSE(commit.restored);
  EXPECT_TRUE(session.history_status().can_undo);
  EXPECT_EQ(session.history_status().undo_count, 1U);
  EXPECT_EQ(session.history_status().redo_count, 0U);
  ASSERT_EQ(session.objects().size(), 4U);
  const qec::SessionState committed = session.state();

  EXPECT_TRUE(session.undo().applied);
  const qec::SessionState undone = session.state();
  expect_session_semantics_equal(undone, before);
  EXPECT_EQ(undone.next_object_id, committed.next_object_id);
  EXPECT_EQ(undone.next_light_id, before.next_light_id);
  EXPECT_GT(undone.content_revision, committed.content_revision);
  EXPECT_GT(undone.selection_revision, committed.selection_revision);
  EXPECT_EQ(session.history_status().undo_count, 0U);
  EXPECT_EQ(session.history_status().redo_count, 1U);

  EXPECT_TRUE(session.redo().applied);
  const qec::SessionState redone = session.state();
  expect_session_semantics_equal(redone, committed);
  EXPECT_EQ(redone.next_object_id, committed.next_object_id);
  EXPECT_EQ(redone.next_light_id, committed.next_light_id);
  EXPECT_GT(redone.content_revision, undone.content_revision);
  EXPECT_GT(redone.selection_revision, undone.selection_revision);
  EXPECT_EQ(session.objects().size(), 4U);
  EXPECT_EQ(session.history_status().undo_count, 1U);
  EXPECT_EQ(session.history_status().redo_count, 0U);
}

TEST(QdrModelingRuntimeOperationHalfCutTests,
     RequiresObjectModeAndAtLeastOneSelectedMesh) {
  qec::EditorSession component_mode = qdr_test::selected_cube_session();
  const quader_poly::Document component_source =
      component_mode.objects().front().document;
  component_mode.set_selection_mode(qec::SelectionMode::Face);

  const qec::CommandResult component_result =
      component_mode.half_cut_selected_objects({});

  EXPECT_FALSE(component_result.ok);
  EXPECT_FALSE(component_result.changed);
  ASSERT_EQ(component_mode.objects().size(), 1U);
  expect_documents_equal(component_mode.objects().front().document,
                         component_source);

  qec::EditorSession no_selection(
      {qdr_test::make_object(10, translated_cube({4.0F, 0.0F, 0.0F}), false)});
  const quader_poly::Document unselected_source =
      no_selection.objects().front().document;

  const qec::CommandResult no_selection_result =
      no_selection.half_cut_selected_objects({});

  EXPECT_FALSE(no_selection_result.ok);
  EXPECT_FALSE(no_selection_result.changed);
  ASSERT_EQ(no_selection.objects().size(), 1U);
  expect_documents_equal(no_selection.objects().front().document,
                         unselected_source);
}

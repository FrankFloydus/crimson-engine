////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_editor_api.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace quader::test::curve {

inline QuaderCreateCurveArgs example_curve_args() {
  return {.points = {{.position = {0.0F, 0.0F, 0.0F},
                      .outgoing_handle = {0.5F, 0.0F, 0.0F}},
                     {.position = {2.0F, 1.0F, 0.0F},
                      .incoming_handle = {-0.5F, 0.0F, 0.0F},
                      .outgoing_handle = {0.5F, 0.0F, 0.0F}},
                     {.position = {4.0F, 0.0F, 0.0F},
                      .incoming_handle = {-0.5F, 0.0F, 0.0F}}},
          .name = "Editable Curve"};
}

inline modeling::CurvePointSpec
point_spec(const modeling::CurveControlPoint &point) {
  return {.position = point.position,
          .incoming_handle = point.incoming_handle,
          .outgoing_handle = point.outgoing_handle};
}

inline modeling::CurveDocument
with_middle_height(modeling::CurveDocument document, float height) {
  EXPECT_EQ(document.point_count(), 3U);
  if (document.point_count() != 3U) {
    return document;
  }
  const auto point = document.points()[1];
  auto value = point_spec(point);
  value.position.y = height;
  const auto changed = document.set_point(point.id, value);
  EXPECT_TRUE(changed.ok()) << changed.error_message();
  return document;
}

inline void expect_same_revisions(const QuaderRevisionStamp &actual,
                                  const QuaderRevisionStamp &expected) {
  EXPECT_EQ(actual.project_revision, expected.project_revision);
  EXPECT_EQ(actual.scene_revision, expected.scene_revision);
  EXPECT_EQ(actual.scene_layer_revision, expected.scene_layer_revision);
  EXPECT_EQ(actual.asset_revision, expected.asset_revision);
  EXPECT_EQ(actual.modeling_content_revision, expected.modeling_content_revision);
  EXPECT_EQ(actual.modeling_selection_revision,
            expected.modeling_selection_revision);
}

inline void expect_same_history(const QuaderHistorySnapshot &actual,
                                const QuaderHistorySnapshot &expected) {
  EXPECT_EQ(actual.can_undo, expected.can_undo);
  EXPECT_EQ(actual.can_redo, expected.can_redo);
  EXPECT_EQ(actual.undo_label, expected.undo_label);
  EXPECT_EQ(actual.redo_label, expected.redo_label);
}

inline void expect_same_mesh_geometry(const QuaderMeshAsset &actual,
                                      const QuaderMeshAsset &expected) {
  EXPECT_EQ(actual.indices, expected.indices);
  ASSERT_EQ(actual.vertices.size(), expected.vertices.size());
  for (std::size_t index = 0; index < actual.vertices.size(); ++index) {
    EXPECT_FLOAT_EQ(actual.vertices[index].position.x,
                    expected.vertices[index].position.x);
    EXPECT_FLOAT_EQ(actual.vertices[index].position.y,
                    expected.vertices[index].position.y);
    EXPECT_FLOAT_EQ(actual.vertices[index].position.z,
                    expected.vertices[index].position.z);
  }
}

inline QuaderModelingObjectRef create_curve(QuaderEditorApi &editor) {
  const auto created = editor.modeling().create_curve(example_curve_args());
  EXPECT_TRUE(created.ok()) << created.error_message();
  return created.value();
}

inline QuaderModelingObjectRef create_box(QuaderEditorApi &editor) {
  const auto created = editor.modeling().create_box(
      QuaderBounds{.min = {-1.0F, -1.0F, -1.0F},
                   .max = {1.0F, 1.0F, 1.0F}},
      "Companion Mesh");
  EXPECT_TRUE(created.ok()) << created.error_message();
  return created.value();
}

inline modeling::CurveDocument read_curve(QuaderEditorApi &editor,
                                          QuaderModelingObjectRef object,
                                          bool include_preview = true) {
  const auto snapshot = editor.modeling().curve_snapshot(object, include_preview);
  EXPECT_TRUE(snapshot.ok()) << snapshot.error_message();
  return snapshot.value().document;
}

inline void expect_curve(QuaderEditorApi &editor, QuaderModelingObjectRef object,
                          const modeling::CurveDocument &expected,
                          bool include_preview = true) {
  const auto snapshot = editor.modeling().curve_snapshot(object, include_preview);
  ASSERT_TRUE(snapshot.ok()) << snapshot.error_message();
  EXPECT_TRUE(snapshot.value().document.has_same_geometry(expected));
  const auto entity = editor.query().entity(object.entity);
  ASSERT_TRUE(entity.has_value());
  EXPECT_EQ(entity->kind, QuaderEntityKind::Curve);
  EXPECT_TRUE(entity->has_curve);
  EXPECT_FALSE(entity->has_mesh);
  EXPECT_EQ(entity->modeling_object_id, object.modeling_object_id);
}

inline std::vector<EntityId> selected_entities(const QuaderEditorApi &editor) {
  std::vector<EntityId> selected;
  for (const auto &entity : editor.query().entities()) {
    if (entity.selected) {
      selected.push_back(entity.id);
    }
  }
  std::sort(selected.begin(), selected.end());
  return selected;
}

inline EntityId active_entity(const QuaderEditorApi &editor) {
  for (const auto &entity : editor.query().entities()) {
    if (entity.active) {
      return entity.id;
    }
  }
  return kInvalidEntityId;
}

} // namespace quader::test::curve

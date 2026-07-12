////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "ui/qt_viewport_delete_popup.hpp"

#include <array>
#include <string_view>

namespace {

using quader::ModelingSelectionMode;
using quader::ModelingSelectionSummary;
using quader::QtViewportDeletePopupAction;
using quader::QtViewportDeletePopupKind;
using quader::qt_viewport_delete_popup_actions;
using quader::qt_viewport_delete_popup_kind;

const QtViewportDeletePopupAction &find_action(
    const std::array<QtViewportDeletePopupAction, 5> &actions,
    std::string_view id) {
  for (const QtViewportDeletePopupAction &action : actions) {
    if (action.id == id) {
      return action;
    }
  }
  ADD_FAILURE() << "Delete popup action not found: " << id;
  return actions.front();
}

} // namespace

TEST(QtViewportDeletePopupTests,
     ObjectGroupAndSelectedSceneRootUseConfirmation) {
  EXPECT_EQ(qt_viewport_delete_popup_kind(ModelingSelectionMode::Object,
                                          false),
            QtViewportDeletePopupKind::ObjectConfirmation);
  EXPECT_EQ(qt_viewport_delete_popup_kind(ModelingSelectionMode::Group, false),
            QtViewportDeletePopupKind::ObjectConfirmation);
  EXPECT_EQ(qt_viewport_delete_popup_kind(ModelingSelectionMode::Vertex, true),
            QtViewportDeletePopupKind::ObjectConfirmation);

  EXPECT_EQ(qt_viewport_delete_popup_kind(ModelingSelectionMode::Vertex,
                                          false),
            QtViewportDeletePopupKind::ComponentMenu);
  EXPECT_EQ(qt_viewport_delete_popup_kind(ModelingSelectionMode::Edge, false),
            QtViewportDeletePopupKind::ComponentMenu);
  EXPECT_EQ(qt_viewport_delete_popup_kind(ModelingSelectionMode::Face, false),
            QtViewportDeletePopupKind::ComponentMenu);
}

TEST(QtViewportDeletePopupTests,
     VertexModeEnablesDeleteAndDissolveVertexCommandsOnly) {
  const auto actions = qt_viewport_delete_popup_actions(
      {.mode = ModelingSelectionMode::Vertex, .selected_vertex_count = 2U});

  EXPECT_TRUE(find_action(actions, "delete_vertices").enabled);
  EXPECT_TRUE(find_action(actions, "dissolve_vertices").enabled);
  EXPECT_FALSE(find_action(actions, "delete_face").enabled);
  EXPECT_FALSE(find_action(actions, "delete_edges").enabled);
  EXPECT_FALSE(find_action(actions, "dissolve_edges").enabled);
  EXPECT_EQ(find_action(actions, "delete_vertices").command_id.string(),
            quader::quader_command::kDeleteSelection.string());
  EXPECT_EQ(find_action(actions, "dissolve_vertices").command_id.string(),
            quader::quader_command::kModelingDissolveVertices.string());
}

TEST(QtViewportDeletePopupTests,
     EdgeModeEnablesDeleteAndDissolveEdgeCommandsOnly) {
  const auto actions = qt_viewport_delete_popup_actions(
      {.mode = ModelingSelectionMode::Edge, .selected_edge_count = 3U});

  EXPECT_TRUE(find_action(actions, "delete_edges").enabled);
  EXPECT_TRUE(find_action(actions, "dissolve_edges").enabled);
  EXPECT_FALSE(find_action(actions, "delete_vertices").enabled);
  EXPECT_FALSE(find_action(actions, "dissolve_vertices").enabled);
  EXPECT_FALSE(find_action(actions, "delete_face").enabled);
  EXPECT_EQ(find_action(actions, "delete_edges").command_id.string(),
            quader::quader_command::kDeleteSelection.string());
  EXPECT_EQ(find_action(actions, "dissolve_edges").command_id.string(),
            quader::quader_command::kModelingDissolveEdges.string());
}

TEST(QtViewportDeletePopupTests, FaceModeEnablesDeleteFaceOnly) {
  const auto actions = qt_viewport_delete_popup_actions(
      {.mode = ModelingSelectionMode::Face, .selected_face_count = 1U});

  EXPECT_TRUE(find_action(actions, "delete_face").enabled);
  EXPECT_FALSE(find_action(actions, "delete_vertices").enabled);
  EXPECT_FALSE(find_action(actions, "dissolve_vertices").enabled);
  EXPECT_FALSE(find_action(actions, "delete_edges").enabled);
  EXPECT_FALSE(find_action(actions, "dissolve_edges").enabled);
  EXPECT_EQ(find_action(actions, "delete_face").command_id.string(),
            quader::quader_command::kDeleteSelection.string());
}

TEST(QtViewportDeletePopupTests, EmptyComponentSelectionDisablesEveryAction) {
  const auto actions = qt_viewport_delete_popup_actions(
      {.mode = ModelingSelectionMode::Vertex});
  for (const QtViewportDeletePopupAction &action : actions) {
    EXPECT_FALSE(action.enabled) << action.id;
  }
}

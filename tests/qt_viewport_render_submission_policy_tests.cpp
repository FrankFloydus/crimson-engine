////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "ui/qt_viewport_render_submission_policy.hpp"

#include <array>
#include <vector>

namespace {

using quader::ViewportPaneId;
using quader::ViewportPanePlacement;

constexpr std::array<ViewportPanePlacement, 4> kQuadPlacements{{
    {.pane_id = ViewportPaneId::Perspective},
    {.pane_id = ViewportPaneId::Top},
    {.pane_id = ViewportPaneId::Front},
    {.pane_id = ViewportPaneId::Side},
}};

TEST(QtViewportRenderSubmissionPolicyTests,
     NavigationSubmitsOnlyTheInteractionPane) {
  std::vector<ViewportPaneId> submitted_panes;

  const std::size_t submitted = quader::qt_submit_viewport_render_frames(
      kQuadPlacements, true, ViewportPaneId::Front,
      [&submitted_panes](const ViewportPanePlacement &placement) {
        submitted_panes.push_back(placement.pane_id);
        return true;
      });

  EXPECT_EQ(submitted, 1u);
  EXPECT_EQ(submitted_panes,
            std::vector<ViewportPaneId>{ViewportPaneId::Front});
}

TEST(QtViewportRenderSubmissionPolicyTests,
     NavigationReleaseResumesEveryPaneInOriginalOrder) {
  std::vector<ViewportPaneId> submitted_panes;

  const std::size_t submitted = quader::qt_submit_viewport_render_frames(
      kQuadPlacements, false, ViewportPaneId::Front,
      [&submitted_panes](const ViewportPanePlacement &placement) {
        submitted_panes.push_back(placement.pane_id);
        return true;
      });

  EXPECT_EQ(submitted, kQuadPlacements.size());
  EXPECT_EQ(submitted_panes,
            (std::vector<ViewportPaneId>{
                ViewportPaneId::Perspective, ViewportPaneId::Top,
                ViewportPaneId::Front, ViewportPaneId::Side}));
}

TEST(QtViewportRenderSubmissionPolicyTests,
     FailedNavigationSubmissionDoesNotFallThroughToStaticPanes) {
  std::vector<ViewportPaneId> attempted_panes;

  const std::size_t submitted = quader::qt_submit_viewport_render_frames(
      kQuadPlacements, true, ViewportPaneId::Side,
      [&attempted_panes](const ViewportPanePlacement &placement) {
        attempted_panes.push_back(placement.pane_id);
        return false;
      });

  EXPECT_EQ(submitted, 0u);
  EXPECT_EQ(attempted_panes,
            std::vector<ViewportPaneId>{ViewportPaneId::Side});
}

TEST(QtViewportRenderSubmissionPolicyTests,
     MissingInteractionPaneFallsBackToEveryActivePane) {
  constexpr std::array<ViewportPanePlacement, 2> kActivePlacements{{
      {.pane_id = ViewportPaneId::Top},
      {.pane_id = ViewportPaneId::Perspective},
  }};
  std::vector<ViewportPaneId> submitted_panes;

  const std::size_t submitted = quader::qt_submit_viewport_render_frames(
      kActivePlacements, true, ViewportPaneId::Side,
      [&submitted_panes](const ViewportPanePlacement &placement) {
        submitted_panes.push_back(placement.pane_id);
        return true;
      });

  EXPECT_EQ(submitted, kActivePlacements.size());
  EXPECT_EQ(submitted_panes,
            (std::vector<ViewportPaneId>{ViewportPaneId::Top,
                                         ViewportPaneId::Perspective}));
}

} // namespace

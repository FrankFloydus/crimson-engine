////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "ui/qt_command_event_filter.hpp"

namespace {

TEST(QtCommandEventFilterTests,
     DispatchContextPreservesActiveToolAndSelectionMode) {
  const quader::ViewportToolState state =
      quader::qt_command_dispatch_tool_state(
          []() {
            return quader::ViewportToolState{
                .active_tool = quader::ViewportToolId::Box,
                .selection_mode = quader::ViewportSelectionMode::Face,
                .grid_preset = 2,
            };
          },
          9);

  EXPECT_EQ(state.active_tool, quader::ViewportToolId::Box);
  EXPECT_EQ(state.selection_mode, quader::ViewportSelectionMode::Face);
  EXPECT_EQ(state.grid_preset, 9);
}

TEST(QtCommandEventFilterTests, DispatchContextHasCanonicalFallback) {
  const quader::ViewportToolState state =
      quader::qt_command_dispatch_tool_state({}, 4);

  EXPECT_EQ(state.active_tool, quader::ViewportToolId::Select);
  EXPECT_EQ(state.selection_mode, quader::ViewportSelectionMode::Object);
  EXPECT_EQ(state.grid_preset, 4);
}

} // namespace

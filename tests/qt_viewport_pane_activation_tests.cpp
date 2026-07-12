////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "ui/qt_viewport_pane_activation.hpp"

#include <array>
#include <cstddef>
#include <optional>

namespace {

/**
 * Models the visibility and geometry state of one embedded native pane.
 */
struct FakeNativePane {
  bool visible = false;
  bool geometry_synchronized_after_activation = false;
  std::size_t geometry_sync_count = 0u;
};

TEST(QtViewportPaneActivationTests,
     SingleToFourMakesEveryPanePresentableWithoutSplitMutation) {
  std::array<FakeNativePane, 4> panes{};
  const auto apply = [&panes](const std::array<bool, 4> &active) {
    quader::qt_apply_viewport_pane_activation(
        active,
        [&panes](std::size_t index, bool visible) {
          panes[index].visible = visible;
        },
        [&panes](std::size_t index) {
          ++panes[index].geometry_sync_count;
          panes[index].geometry_synchronized_after_activation =
              panes[index].visible;
        });
  };
  std::optional<std::array<bool, 4>> displayed;
  const auto refresh = [&apply, &displayed](
                           const std::array<bool, 4> &active) {
    if (displayed.has_value() && *displayed == active) {
      return;
    }
    displayed = active;
    apply(active);
  };

  refresh({true, false, false, false});
  EXPECT_TRUE(panes[0].geometry_synchronized_after_activation);
  for (std::size_t index = 1; index < panes.size(); ++index) {
    EXPECT_FALSE(panes[index].visible);
    EXPECT_EQ(panes[index].geometry_sync_count, 0u);
  }

  refresh({true, true, true, true});
  for (const FakeNativePane &pane : panes) {
    EXPECT_TRUE(pane.visible);
    EXPECT_TRUE(pane.geometry_synchronized_after_activation);
    EXPECT_GT(pane.geometry_sync_count, 0u);
  }

  const std::array<std::size_t, 4> before_cached_frame{
      panes[0].geometry_sync_count, panes[1].geometry_sync_count,
      panes[2].geometry_sync_count, panes[3].geometry_sync_count};
  refresh({true, true, true, true});
  // The unchanged layout takes the cached no-op path without a splitter
  // mutation. The one activation pass already left every pane presentable.
  for (std::size_t index = 0; index < panes.size(); ++index) {
    EXPECT_TRUE(panes[index].geometry_synchronized_after_activation);
    EXPECT_EQ(panes[index].geometry_sync_count, before_cached_frame[index]);
  }
}

} // namespace

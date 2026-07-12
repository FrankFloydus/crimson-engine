////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "ui/ui_panel_manager.hpp"

namespace quader {
namespace {

TEST(UIPanelManagerTests, ExplicitVisibilityTransitionIgnoresPhysicalNoise) {
  const UIPanelVisibilityTransition unchanged_visible =
      ui_panel_visibility_transition(true, true);
  EXPECT_TRUE(unchanged_visible.visible);
  EXPECT_FALSE(unchanged_visible.changed);

  const UIPanelVisibilityTransition hidden =
      ui_panel_visibility_transition(true, false);
  EXPECT_FALSE(hidden.visible);
  EXPECT_TRUE(hidden.changed);

  const UIPanelVisibilityTransition unchanged_hidden =
      ui_panel_visibility_transition(hidden.visible, false);
  EXPECT_FALSE(unchanged_hidden.visible);
  EXPECT_FALSE(unchanged_hidden.changed);

  const UIPanelVisibilityTransition restored =
      ui_panel_visibility_transition(unchanged_hidden.visible, true);
  EXPECT_TRUE(restored.visible);
  EXPECT_TRUE(restored.changed);
}

} // namespace
} // namespace quader

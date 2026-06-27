////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <editor/interactions/interaction_api.hpp>

TEST(QdrModelingRuntimeToolViewportNavigationTests, ViewportNavigationStateTracksOrbitPanAndFlyOwnership) {
	quader_editor_core::ViewportNavigationState state;
	EXPECT_FALSE(quader_editor_core::viewport_navigation_is_active(state));

	quader_editor_core::begin_viewport_navigation(state, false);
	EXPECT_TRUE(quader_editor_core::viewport_navigation_is_orbit(state));
	EXPECT_FALSE(quader_editor_core::viewport_navigation_is_pan(state));
	EXPECT_TRUE(quader_editor_core::viewport_navigation_is_active(state));

	quader_editor_core::begin_viewport_navigation(state, true);
	EXPECT_TRUE(quader_editor_core::viewport_navigation_is_pan(state));
	EXPECT_FALSE(quader_editor_core::viewport_navigation_is_orbit(state));

	quader_editor_core::begin_viewport_fly_navigation(state, 77, "main");
	EXPECT_TRUE(quader_editor_core::viewport_navigation_is_fly(state));
	EXPECT_TRUE(quader_editor_core::viewport_navigation_is_fly_camera(state, 77));
	EXPECT_FALSE(quader_editor_core::viewport_navigation_is_fly_camera(state, 78));

	quader_editor_core::end_viewport_navigation(state);
	EXPECT_FALSE(quader_editor_core::viewport_navigation_is_active(state));
}

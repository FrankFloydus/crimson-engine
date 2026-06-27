////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <catch2/catch_test_macros.hpp>

#include <editor/interactions/interaction_api.h>

TEST_CASE("viewport navigation state tracks orbit pan and fly ownership", "[editor-core][tool][navigation]")
{
	quader_editor_core::ViewportNavigationState state;
	CHECK_FALSE(quader_editor_core::viewport_navigation_is_active(state));

	quader_editor_core::begin_viewport_navigation(state, false);
	CHECK(quader_editor_core::viewport_navigation_is_orbit(state));
	CHECK_FALSE(quader_editor_core::viewport_navigation_is_pan(state));
	CHECK(quader_editor_core::viewport_navigation_is_active(state));

	quader_editor_core::begin_viewport_navigation(state, true);
	CHECK(quader_editor_core::viewport_navigation_is_pan(state));
	CHECK_FALSE(quader_editor_core::viewport_navigation_is_orbit(state));

	quader_editor_core::begin_viewport_fly_navigation(state, 77, "main");
	CHECK(quader_editor_core::viewport_navigation_is_fly(state));
	CHECK(quader_editor_core::viewport_navigation_is_fly_camera(state, 77));
	CHECK_FALSE(quader_editor_core::viewport_navigation_is_fly_camera(state, 78));

	quader_editor_core::end_viewport_navigation(state);
	CHECK_FALSE(quader_editor_core::viewport_navigation_is_active(state));
}

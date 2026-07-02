////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <editor/modeling/runtime/tool_constants.hpp>
#include <editor/metadata/host_contract.hpp>
#include <viewport/viewport_layout.hpp>

#include <string_view>

namespace qhc = quader_host_contract;
namespace qec = quader_editor_core;

namespace {

bool same_text(std::string_view left, std::string_view right)
{
	return left == right;
}

const qhc::HostViewportViewDescriptor& require_view(std::string_view id)
{
	for (const qhc::HostViewportViewDescriptor& descriptor : qhc::viewport_view_catalog()) {
		if (descriptor.id == id) {
			return descriptor;
		}
	}
	ADD_FAILURE() << "Missing host viewport view descriptor";
        static const qhc::HostViewportViewDescriptor kMissingDescriptor{};
        return kMissingDescriptor;
}

const qhc::HostGridPresetDescriptor& require_grid(std::string_view shortcut)
{
	for (const qhc::HostGridPresetDescriptor& descriptor : qhc::grid_preset_catalog()) {
		if (descriptor.shortcut == shortcut) {
			return descriptor;
		}
	}
	ADD_FAILURE() << "Missing host grid preset descriptor";
        static const qhc::HostGridPresetDescriptor kMissingDescriptor{};
        return kMissingDescriptor;
}

} // namespace

TEST(QdrEditorMetadataViewportCatalogTests, HostContractViewportDefaultsExposeCurrentHostGlueConstants) {
	const qhc::HostViewportDefaultsDescriptor defaults = qhc::viewport_defaults();

        EXPECT_TRUE(defaults.map_units_per_world_unit ==
                    qec::kDefaultMapUnitsPerWorldUnit);
        EXPECT_TRUE(defaults.default_grid_map_units == 32.0);
	EXPECT_TRUE(defaults.default_orthographic_size == 24.0);
	EXPECT_TRUE(defaults.focus_origin_distance == 64.0);
	EXPECT_TRUE(defaults.default_fov_degrees == 60.0);
        EXPECT_TRUE(defaults.orbit_radians_per_pixel ==
                    qec::kNavigationOrbitRadiansPerPixel);
        EXPECT_TRUE(defaults.fly_look_radians_per_pixel ==
                    qec::kNavigationFlyLookRadiansPerPixel);
        EXPECT_TRUE(defaults.fly_base_speed == qec::kNavigationFlyBaseSpeed);
        EXPECT_TRUE(defaults.fly_fast_multiplier ==
                    qec::kNavigationFlyFastMultiplier);
        EXPECT_TRUE(defaults.fly_slow_multiplier ==
                    qec::kNavigationFlySlowMultiplier);
        EXPECT_TRUE(defaults.selection_box_drag_threshold_pixels ==
                    qec::kSelectionBoxDefaultDragThreshold);
}

TEST(QdrEditorMetadataViewportCatalogTests, HostContractGridPresetCatalogIsExplicitAndMapUnitBased) {
	const std::span<const qhc::HostGridPresetDescriptor> presets = qhc::grid_preset_catalog();
	EXPECT_TRUE(presets.size() == 10U);

	EXPECT_TRUE(same_text(presets.front().shortcut, "F1"));
	EXPECT_TRUE(same_text(presets.back().shortcut, "F10"));
	EXPECT_TRUE(require_grid("F1").map_units == 1.0);
	EXPECT_TRUE(require_grid("F6").map_units == 32.0);
	EXPECT_TRUE(require_grid("F10").map_units == 512.0);
	EXPECT_TRUE(require_grid("F6").default_preset);
	EXPECT_FALSE(require_grid("F5").default_preset);
        EXPECT_TRUE(require_grid("F6").world_units ==
                    32.0 / qec::kDefaultMapUnitsPerWorldUnit);
}

TEST(QdrEditorMetadataViewportCatalogTests, HostContractViewportViewCatalogDescribesCanonicalInitialCameras) {
	const std::span<const qhc::HostViewportViewDescriptor> views = qhc::viewport_view_catalog();
	EXPECT_TRUE(views.size() == 7U);

	const qhc::HostViewportViewDescriptor& perspective = require_view("perspective");
	EXPECT_TRUE(same_text(perspective.label, "Perspective"));
	EXPECT_FALSE(perspective.orthographic);
	EXPECT_TRUE(perspective.position.x == 6.0);
	EXPECT_TRUE(perspective.position.y == 5.0);
	EXPECT_TRUE(perspective.position.z == 8.0);

	const qhc::HostViewportViewDescriptor& top = require_view("top");
	EXPECT_TRUE(top.orthographic);
	EXPECT_TRUE(top.position.y == qhc::viewport_defaults().focus_origin_distance);
	EXPECT_TRUE(top.up.z == -1.0);

        const qhc::HostViewportViewDescriptor &side = require_view(
            quader::viewport_view_stable_id(quader::ViewportView::Side));
        EXPECT_TRUE(same_text(side.label, quader::viewport_view_label(
                                              quader::ViewportView::Side)));
        EXPECT_TRUE(side.orthographic);
	EXPECT_TRUE(side.position.x == qhc::viewport_defaults().focus_origin_distance);
	EXPECT_TRUE(side.up.y == 1.0);
}

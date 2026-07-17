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

#include <array>
#include <cstddef>
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

        constexpr std::array<double, 10> kExpectedMapUnits{
            1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 64.0, 128.0, 256.0,
            512.0};
        for (std::size_t index = 0; index < presets.size(); ++index) {
          EXPECT_TRUE(presets[index].shortcut.empty());
          EXPECT_TRUE(presets[index].map_units == kExpectedMapUnits[index]);
          EXPECT_EQ(presets[index].default_preset, index == 5U);
        }
        EXPECT_TRUE(presets[5].world_units ==
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

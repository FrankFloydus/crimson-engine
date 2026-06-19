#include <catch2/catch_test_macros.hpp>

#include <quader_editor_core/tool_constants.h>
#include <quader_host_contract/host_contract.h>

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
	FAIL("Missing host viewport view descriptor");
}

const qhc::HostGridPresetDescriptor& require_grid(std::string_view shortcut)
{
	for (const qhc::HostGridPresetDescriptor& descriptor : qhc::grid_preset_catalog()) {
		if (descriptor.shortcut == shortcut) {
			return descriptor;
		}
	}
	FAIL("Missing host grid preset descriptor");
}

} // namespace

TEST_CASE("host contract viewport defaults expose current host glue constants", "[host-contract]")
{
	const qhc::HostViewportDefaultsDescriptor defaults = qhc::viewport_defaults();

	CHECK(defaults.map_units_per_world_unit == qec::default_map_units_per_world_unit);
	CHECK(defaults.default_grid_map_units == 32.0);
	CHECK(defaults.default_orthographic_size == 24.0);
	CHECK(defaults.focus_origin_distance == 64.0);
	CHECK(defaults.default_fov_degrees == 60.0);
	CHECK(defaults.orbit_radians_per_pixel == qec::navigation_orbit_radians_per_pixel);
	CHECK(defaults.fly_look_radians_per_pixel == qec::navigation_fly_look_radians_per_pixel);
	CHECK(defaults.fly_base_speed == qec::navigation_fly_base_speed);
	CHECK(defaults.fly_fast_multiplier == qec::navigation_fly_fast_multiplier);
	CHECK(defaults.fly_slow_multiplier == qec::navigation_fly_slow_multiplier);
	CHECK(defaults.selection_box_drag_threshold_pixels == qec::selection_box_default_drag_threshold);
}

TEST_CASE("host contract grid preset catalog is explicit and map-unit based", "[host-contract]")
{
	const std::span<const qhc::HostGridPresetDescriptor> presets = qhc::grid_preset_catalog();
	REQUIRE(presets.size() == 10U);

	CHECK(same_text(presets.front().shortcut, "F1"));
	CHECK(same_text(presets.back().shortcut, "F10"));
	CHECK(require_grid("F1").map_units == 1.0);
	CHECK(require_grid("F6").map_units == 32.0);
	CHECK(require_grid("F10").map_units == 512.0);
	CHECK(require_grid("F6").default_preset);
	CHECK_FALSE(require_grid("F5").default_preset);
	CHECK(require_grid("F6").world_units == 32.0 / qec::default_map_units_per_world_unit);
}

TEST_CASE("host contract viewport view catalog describes canonical initial cameras", "[host-contract]")
{
	const std::span<const qhc::HostViewportViewDescriptor> views = qhc::viewport_view_catalog();
	REQUIRE(views.size() == 7U);

	const qhc::HostViewportViewDescriptor& perspective = require_view("perspective");
	CHECK(same_text(perspective.label, "Perspective"));
	CHECK_FALSE(perspective.orthographic);
	CHECK(perspective.position.x == 6.0);
	CHECK(perspective.position.y == 5.0);
	CHECK(perspective.position.z == 8.0);

	const qhc::HostViewportViewDescriptor& top = require_view("top");
	CHECK(top.orthographic);
	CHECK(top.position.y == qhc::viewport_defaults().focus_origin_distance);
	CHECK(top.up.z == -1.0);

	const qhc::HostViewportViewDescriptor& side = require_view("side");
	CHECK(same_text(side.label, "Right"));
	CHECK(side.orthographic);
	CHECK(side.position.x == qhc::viewport_defaults().focus_origin_distance);
	CHECK(side.up.y == 1.0);
}

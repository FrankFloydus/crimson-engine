////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <catch2/catch_test_macros.hpp>

#include <editor/modeling/operations/operation_schema.h>
#include <editor/interactions/cut_tool.h>
#include <editor/metadata/host_contract.h>

#include <cmath>
#include <span>
#include <string_view>

namespace qhc = quader_host_contract;
namespace qec = quader_editor_core;

namespace {

bool same_text(std::string_view left, std::string_view right)
{
	return left == right;
}

bool close_to(double left, double right)
{
	return std::abs(left - right) <= 0.000001;
}

const qhc::HostPreferenceDescriptor& require_preference(std::string_view id)
{
	const qhc::HostPreferenceDescriptor *descriptor = qhc::find_preference_descriptor(id);
	REQUIRE(descriptor != nullptr);
	return *descriptor;
}

const qhc::HostPreferenceGroupDescriptor& require_group(std::string_view id)
{
	const qhc::HostPreferenceGroupDescriptor *descriptor = qhc::find_preference_group_descriptor(id);
	REQUIRE(descriptor != nullptr);
	return *descriptor;
}

bool group_contains(const qhc::HostPreferenceGroupDescriptor& group, std::string_view preference_id)
{
	for (std::string_view id : group.preference_ids) {
		if (id == preference_id) {
			return true;
		}
	}
	return false;
}

void check_color(const qhc::HostColorDescriptor& color, double r, double g, double b, double a)
{
	CHECK(close_to(color.r, r));
	CHECK(close_to(color.g, g));
	CHECK(close_to(color.b, b));
	CHECK(close_to(color.a, a));
}

void check_float_preference(
		const qhc::HostPreferenceDescriptor& preference,
		double default_value,
		double minimum,
		double maximum,
		std::string_view group_id)
{
	CHECK(preference.value_kind == qhc::HostPreferenceValueKind::floating);
	CHECK(close_to(preference.default_number, default_value));
	CHECK(close_to(preference.minimum_number, minimum));
	CHECK(close_to(preference.maximum_number, maximum));
	CHECK(close_to(preference.number_step, 0.0));
	CHECK(same_text(preference.group_id, group_id));
	CHECK(preference.hard_limits);
}

} // namespace

TEST_CASE("host contract preference catalog exposes viewport setting defaults as engine-neutral descriptors", "[host-contract]")
{
	const std::span<const qhc::HostPreferenceDescriptor> preferences = qhc::preference_catalog();
	REQUIRE(preferences.size() == 45U);

	const qhc::HostPreferenceDescriptor& background = require_preference("viewport.scene.background_color");
	CHECK(background.scope == qhc::HostPreferenceScope::viewport_style);
	CHECK(background.value_kind == qhc::HostPreferenceValueKind::color_rgba);
	CHECK(same_text(background.label, "Background"));
	CHECK(same_text(background.group_id, "viewport.scene"));
	check_color(background.default_color, 13.0 / 255.0, 13.0 / 255.0, 13.0 / 255.0, 1.0);

	const qhc::HostPreferenceDescriptor& selection_wire = require_preference("viewport.selection.wire_color");
	CHECK(selection_wire.value_kind == qhc::HostPreferenceValueKind::color_rgba);
	CHECK(same_text(selection_wire.related_overlay_layer_id, "selected_face_edge"));
	check_color(selection_wire.default_color, 1.0, 211.0 / 255.0, 31.0 / 255.0, 1.0);

	const qhc::HostPreferenceDescriptor& diagnostic_edge =
		require_preference("viewport.overlay.diagnostic_edge_color");
	CHECK(diagnostic_edge.value_kind == qhc::HostPreferenceValueKind::color_rgba);
	CHECK(same_text(diagnostic_edge.related_overlay_layer_id, "diagnostic_edge"));
	check_color(diagnostic_edge.default_color, 1.0, 0.0, 0.0, 1.0);

	check_float_preference(require_preference("viewport.grid.minor_line_size"), 0.32, 0.25, 8.0, "viewport.grid");
	const qhc::HostPreferenceDescriptor& diagnostic_edge_line =
		require_preference("viewport.overlay.diagnostic_edge_line_size");
	check_float_preference(diagnostic_edge_line, 4.0, 0.25, 8.0, "viewport.overlay");
	CHECK(same_text(diagnostic_edge_line.related_overlay_layer_id, "diagnostic_edge"));
	check_float_preference(require_preference("viewport.vertices.vertex_size"), 7.0, 2.0, 32.0, "viewport.vertices");

	const qhc::HostPreferenceDescriptor& pick_vertex = require_preference("viewport.picking.vertex_radius");
	check_float_preference(pick_vertex, 0.3, 0.001, 2.0, "viewport.picking");
	CHECK(pick_vertex.scope == qhc::HostPreferenceScope::viewport_picking);

	const qhc::HostPreferenceDescriptor& gizmo_size = require_preference("viewport.gizmo.size");
	check_float_preference(gizmo_size, 1.0, 0.5, 2.0, "viewport.gizmo");
	CHECK(gizmo_size.scope == qhc::HostPreferenceScope::viewport_interaction);
}

TEST_CASE("host contract operation preferences link back to editor-core operation option schemas", "[host-contract]")
{
	const qec::EdgeBevelOperationSchema edge_bevel = qec::edge_bevel_operation_schema();
	const qhc::HostPreferenceDescriptor& width = require_preference("operation.edge_bevel.width");
	CHECK(width.scope == qhc::HostPreferenceScope::operation);
	CHECK(width.value_kind == qhc::HostPreferenceValueKind::floating);
	CHECK(same_text(width.related_operation_id, edge_bevel.operation_id));
	CHECK(same_text(width.related_option_id, edge_bevel.width.id));
	CHECK(same_text(width.label, edge_bevel.width.label));
	CHECK(close_to(width.default_number, edge_bevel.width.default_value));
	CHECK(close_to(width.minimum_number, edge_bevel.width.minimum));
	CHECK(close_to(width.maximum_number, edge_bevel.width.maximum));
	CHECK(close_to(width.number_step, edge_bevel.width.step));

	const qec::ThickenOperationSchema thicken = qec::thicken_operation_schema();
	const qhc::HostPreferenceDescriptor& from_center = require_preference("operation.thicken.from_center");
	CHECK(from_center.value_kind == qhc::HostPreferenceValueKind::boolean);
	CHECK(same_text(from_center.related_operation_id, thicken.operation_id));
	CHECK(same_text(from_center.related_option_id, thicken.from_center.id));
	CHECK(from_center.default_bool == thicken.from_center.default_value);
	CHECK(from_center.visible == thicken.from_center.visible);

	const qec::BridgeOperationSchema bridge = qec::bridge_operation_schema();
	const qhc::HostPreferenceDescriptor& steps = require_preference("operation.bridge.steps");
	CHECK(steps.value_kind == qhc::HostPreferenceValueKind::integer);
	CHECK(same_text(steps.related_operation_id, bridge.operation_id));
	CHECK(same_text(steps.related_option_id, bridge.steps.id));
	CHECK(steps.default_integer == bridge.steps.default_value);
	CHECK(steps.minimum_integer == bridge.steps.minimum);
	CHECK(steps.maximum_integer == bridge.steps.maximum);
	CHECK(steps.integer_step == bridge.steps.step);

	const qhc::HostPreferenceDescriptor& cut_mode = require_preference("operation.cut.keep_mode");
	CHECK(cut_mode.value_kind == qhc::HostPreferenceValueKind::choice);
	CHECK(same_text(cut_mode.group_id, "operation.cut"));
	CHECK(cut_mode.default_integer == qec::cut_mode_id(qec::CutMode::keep_both));
	REQUIRE(cut_mode.choices.size() == 3U);
	CHECK(same_text(cut_mode.choices[0].id, "keep_back"));
	CHECK(same_text(cut_mode.choices[0].label, "Discard Left"));
	CHECK(cut_mode.choices[0].value == qec::cut_mode_id(qec::CutMode::keep_back));
	CHECK_FALSE(cut_mode.choices[0].default_choice);
	CHECK(same_text(cut_mode.choices[1].id, "keep_front"));
	CHECK(same_text(cut_mode.choices[1].label, "Discard Right"));
	CHECK(cut_mode.choices[1].value == qec::cut_mode_id(qec::CutMode::keep_front));
	CHECK_FALSE(cut_mode.choices[1].default_choice);
	CHECK(same_text(cut_mode.choices[2].id, "keep_both"));
	CHECK(same_text(cut_mode.choices[2].label, "Keep Both"));
	CHECK(cut_mode.choices[2].value == qec::cut_mode_id(qec::CutMode::keep_both));
	CHECK(cut_mode.choices[2].default_choice);
}

TEST_CASE("host contract preference groups can drive custom engine settings panes", "[host-contract]")
{
	const std::span<const qhc::HostPreferenceGroupDescriptor> groups = qhc::preference_group_catalog();
	REQUIRE(groups.size() == 13U);

	const qhc::HostPreferenceGroupDescriptor& cut = require_group("operation.cut");
	CHECK(cut.scope == qhc::HostPreferenceScope::operation);
	REQUIRE(cut.preference_ids.size() == 1U);
	CHECK(group_contains(cut, "operation.cut.keep_mode"));

	const qhc::HostPreferenceGroupDescriptor& grid = require_group("viewport.grid");
	CHECK(same_text(grid.label, "Grid"));
	CHECK(grid.scope == qhc::HostPreferenceScope::viewport_style);
	REQUIRE(grid.preference_ids.size() == 8U);
	CHECK(group_contains(grid, "viewport.grid.minor_color"));
	CHECK(group_contains(grid, "viewport.grid.axis_line_size"));

	const qhc::HostPreferenceGroupDescriptor& overlay = require_group("viewport.overlay");
	CHECK(same_text(overlay.label, "Overlay"));
	CHECK(overlay.scope == qhc::HostPreferenceScope::viewport_style);
	REQUIRE(overlay.preference_ids.size() == 11U);
	CHECK(group_contains(overlay, "viewport.overlay.diagnostic_edge_color"));
	CHECK(group_contains(overlay, "viewport.overlay.diagnostic_edge_line_size"));

	const qhc::HostPreferenceGroupDescriptor& picking = require_group("viewport.picking");
	CHECK(picking.scope == qhc::HostPreferenceScope::viewport_picking);
	REQUIRE(picking.preference_ids.size() == 2U);
	CHECK(group_contains(picking, "viewport.picking.vertex_radius"));
	CHECK(group_contains(picking, "viewport.picking.edge_radius"));

	for (const qhc::HostPreferenceGroupDescriptor& group : groups) {
		for (std::string_view preference_id : group.preference_ids) {
			const qhc::HostPreferenceDescriptor *preference = qhc::find_preference_descriptor(preference_id);
			REQUIRE(preference != nullptr);
			CHECK(same_text(preference->group_id, group.id));
		}
	}
}

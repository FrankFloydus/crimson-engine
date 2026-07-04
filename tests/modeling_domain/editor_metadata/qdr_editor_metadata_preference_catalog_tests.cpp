////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <editor/modeling/operations/operation_schema.hpp>
#include <editor/interactions/cut_tool.hpp>
#include <editor/metadata/host_contract.hpp>

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
	EXPECT_TRUE(descriptor != nullptr);
	return *descriptor;
}

const qhc::HostPreferenceGroupDescriptor& require_group(std::string_view id)
{
	const qhc::HostPreferenceGroupDescriptor *descriptor = qhc::find_preference_group_descriptor(id);
	EXPECT_TRUE(descriptor != nullptr);
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
	EXPECT_TRUE(close_to(color.r, r));
	EXPECT_TRUE(close_to(color.g, g));
	EXPECT_TRUE(close_to(color.b, b));
	EXPECT_TRUE(close_to(color.a, a));
}

void check_float_preference(
		const qhc::HostPreferenceDescriptor& preference,
		double default_value,
		double minimum,
		double maximum,
		std::string_view group_id)
{
  EXPECT_TRUE(preference.value_kind == qhc::HostPreferenceValueKind::Floating);
  EXPECT_TRUE(close_to(preference.default_number, default_value));
  EXPECT_TRUE(close_to(preference.minimum_number, minimum));
  EXPECT_TRUE(close_to(preference.maximum_number, maximum));
  EXPECT_TRUE(close_to(preference.number_step, 0.0));
  EXPECT_TRUE(same_text(preference.group_id, group_id));
  EXPECT_TRUE(preference.hard_limits);
}

} // namespace

TEST(QdrEditorMetadataPreferenceCatalogTests, HostContractPreferenceCatalogExposesViewportSettingDefaultsAsEngineNeutralDescriptors) {
	const std::span<const qhc::HostPreferenceDescriptor> preferences = qhc::preference_catalog();
	EXPECT_TRUE(preferences.size() == 45U);

	const qhc::HostPreferenceDescriptor& background = require_preference("viewport.scene.background_color");
        EXPECT_TRUE(background.scope ==
                    qhc::HostPreferenceScope::ViewportStyle);
        EXPECT_TRUE(background.value_kind ==
                    qhc::HostPreferenceValueKind::ColorRgba);
        EXPECT_TRUE(same_text(background.label, "Background"));
	EXPECT_TRUE(same_text(background.group_id, "viewport.scene"));
	check_color(background.default_color, 2.0 / 255.0, 2.0 / 255.0, 2.0 / 255.0, 1.0);

	const qhc::HostPreferenceDescriptor& grid_minor = require_preference("viewport.grid.minor_color");
        EXPECT_TRUE(grid_minor.value_kind ==
                    qhc::HostPreferenceValueKind::ColorRgba);
        EXPECT_TRUE(same_text(grid_minor.group_id, "viewport.grid"));
	check_color(grid_minor.default_color, 150.0 / 255.0, 150.0 / 255.0, 150.0 / 255.0, 1.0);

	const qhc::HostPreferenceDescriptor& grid_major = require_preference("viewport.grid.major_color");
        EXPECT_TRUE(grid_major.value_kind ==
                    qhc::HostPreferenceValueKind::ColorRgba);
        EXPECT_TRUE(same_text(grid_major.group_id, "viewport.grid"));
	check_color(grid_major.default_color, 210.0 / 255.0, 210.0 / 255.0, 210.0 / 255.0, 1.0);

	const qhc::HostPreferenceDescriptor& selection_wire = require_preference("viewport.selection.wire_color");
        EXPECT_TRUE(selection_wire.value_kind ==
                    qhc::HostPreferenceValueKind::ColorRgba);
        EXPECT_TRUE(same_text(selection_wire.related_overlay_layer_id, "selected_face_edge"));
	check_color(selection_wire.default_color, 1.0, 211.0 / 255.0, 31.0 / 255.0, 1.0);

	const qhc::HostPreferenceDescriptor& diagnostic_edge =
		require_preference("viewport.overlay.diagnostic_edge_color");
        EXPECT_TRUE(diagnostic_edge.value_kind ==
                    qhc::HostPreferenceValueKind::ColorRgba);
        EXPECT_TRUE(same_text(diagnostic_edge.related_overlay_layer_id, "diagnostic_edge"));
	check_color(diagnostic_edge.default_color, 1.0, 0.0, 0.0, 1.0);

	check_float_preference(require_preference("viewport.grid.minor_line_size"), 0.325, 0.25, 8.0, "viewport.grid");
	check_float_preference(require_preference("viewport.grid.major_line_size"), 0.250, 0.25, 8.0, "viewport.grid");
	check_float_preference(require_preference("viewport.grid.axis_line_size"), 1.0, 0.25, 8.0, "viewport.grid");
	const qhc::HostPreferenceDescriptor& diagnostic_edge_line =
		require_preference("viewport.overlay.diagnostic_edge_line_size");
	check_float_preference(diagnostic_edge_line, 4.0, 0.25, 8.0, "viewport.overlay");
	EXPECT_TRUE(same_text(diagnostic_edge_line.related_overlay_layer_id, "diagnostic_edge"));
	check_float_preference(require_preference("viewport.vertices.vertex_size"), 7.0, 2.0, 32.0, "viewport.vertices");

	const qhc::HostPreferenceDescriptor& pick_vertex = require_preference("viewport.picking.vertex_radius");
	check_float_preference(pick_vertex, 0.3, 0.001, 2.0, "viewport.picking");
        EXPECT_TRUE(pick_vertex.scope ==
                    qhc::HostPreferenceScope::ViewportPicking);

        const qhc::HostPreferenceDescriptor& gizmo_size = require_preference("viewport.gizmo.size");
	check_float_preference(gizmo_size, 1.0, 0.5, 2.0, "viewport.gizmo");
        EXPECT_TRUE(gizmo_size.scope ==
                    qhc::HostPreferenceScope::ViewportInteraction);
}

TEST(QdrEditorMetadataPreferenceCatalogTests, HostContractOperationPreferencesLinkBackToEditorCoreOperationOptionSchemas) {
	const qec::EdgeBevelOperationSchema edge_bevel = qec::edge_bevel_operation_schema();
	const qhc::HostPreferenceDescriptor& width = require_preference("operation.edge_bevel.width");
        EXPECT_TRUE(width.scope == qhc::HostPreferenceScope::Operation);
        EXPECT_TRUE(width.value_kind == qhc::HostPreferenceValueKind::Floating);
        EXPECT_TRUE(same_text(width.related_operation_id, edge_bevel.operation_id));
	EXPECT_TRUE(same_text(width.related_option_id, edge_bevel.width.id));
	EXPECT_TRUE(same_text(width.label, edge_bevel.width.label));
	EXPECT_TRUE(close_to(width.default_number, edge_bevel.width.default_value));
	EXPECT_TRUE(close_to(width.minimum_number, edge_bevel.width.minimum));
	EXPECT_TRUE(close_to(width.maximum_number, edge_bevel.width.maximum));
	EXPECT_TRUE(close_to(width.number_step, edge_bevel.width.step));

	const qec::ThickenOperationSchema thicken = qec::thicken_operation_schema();
	const qhc::HostPreferenceDescriptor& from_center = require_preference("operation.thicken.from_center");
        EXPECT_TRUE(from_center.value_kind ==
                    qhc::HostPreferenceValueKind::Boolean);
        EXPECT_TRUE(same_text(from_center.related_operation_id, thicken.operation_id));
	EXPECT_TRUE(same_text(from_center.related_option_id, thicken.from_center.id));
	EXPECT_TRUE(from_center.default_bool == thicken.from_center.default_value);
	EXPECT_TRUE(from_center.visible == thicken.from_center.visible);

	const qec::BridgeOperationSchema bridge = qec::bridge_operation_schema();
	const qhc::HostPreferenceDescriptor& steps = require_preference("operation.bridge.steps");
        EXPECT_TRUE(steps.value_kind == qhc::HostPreferenceValueKind::Integer);
        EXPECT_TRUE(same_text(steps.related_operation_id, bridge.operation_id));
	EXPECT_TRUE(same_text(steps.related_option_id, bridge.steps.id));
	EXPECT_TRUE(steps.default_integer == bridge.steps.default_value);
	EXPECT_TRUE(steps.minimum_integer == bridge.steps.minimum);
	EXPECT_TRUE(steps.maximum_integer == bridge.steps.maximum);
	EXPECT_TRUE(steps.integer_step == bridge.steps.step);

	const qhc::HostPreferenceDescriptor& cut_mode = require_preference("operation.cut.keep_mode");
        EXPECT_TRUE(cut_mode.value_kind ==
                    qhc::HostPreferenceValueKind::Choice);
        EXPECT_TRUE(same_text(cut_mode.group_id, "operation.cut"));
        EXPECT_TRUE(cut_mode.default_integer ==
                    qec::cut_mode_id(qec::CutMode::KeepBoth));
        EXPECT_TRUE(cut_mode.choices.size() == 3U);
	EXPECT_TRUE(same_text(cut_mode.choices[0].id, "keep_back"));
	EXPECT_TRUE(same_text(cut_mode.choices[0].label, "Discard Left"));
        EXPECT_TRUE(cut_mode.choices[0].value ==
                    qec::cut_mode_id(qec::CutMode::KeepBack));
        EXPECT_FALSE(cut_mode.choices[0].default_choice);
	EXPECT_TRUE(same_text(cut_mode.choices[1].id, "keep_front"));
	EXPECT_TRUE(same_text(cut_mode.choices[1].label, "Discard Right"));
        EXPECT_TRUE(cut_mode.choices[1].value ==
                    qec::cut_mode_id(qec::CutMode::KeepFront));
        EXPECT_FALSE(cut_mode.choices[1].default_choice);
	EXPECT_TRUE(same_text(cut_mode.choices[2].id, "keep_both"));
	EXPECT_TRUE(same_text(cut_mode.choices[2].label, "Keep Both"));
        EXPECT_TRUE(cut_mode.choices[2].value ==
                    qec::cut_mode_id(qec::CutMode::KeepBoth));
        EXPECT_TRUE(cut_mode.choices[2].default_choice);
}

TEST(QdrEditorMetadataPreferenceCatalogTests, HostContractPreferenceGroupsCanDriveCustomEngineSettingsPanes) {
	const std::span<const qhc::HostPreferenceGroupDescriptor> groups = qhc::preference_group_catalog();
	EXPECT_TRUE(groups.size() == 13U);

	const qhc::HostPreferenceGroupDescriptor& cut = require_group("operation.cut");
        EXPECT_TRUE(cut.scope == qhc::HostPreferenceScope::Operation);
        EXPECT_TRUE(cut.preference_ids.size() == 1U);
	EXPECT_TRUE(group_contains(cut, "operation.cut.keep_mode"));

	const qhc::HostPreferenceGroupDescriptor& grid = require_group("viewport.grid");
	EXPECT_TRUE(same_text(grid.label, "Grid"));
        EXPECT_TRUE(grid.scope == qhc::HostPreferenceScope::ViewportStyle);
        EXPECT_TRUE(grid.preference_ids.size() == 8U);
	EXPECT_TRUE(group_contains(grid, "viewport.grid.minor_color"));
	EXPECT_TRUE(group_contains(grid, "viewport.grid.axis_line_size"));

	const qhc::HostPreferenceGroupDescriptor& overlay = require_group("viewport.overlay");
	EXPECT_TRUE(same_text(overlay.label, "Overlay"));
        EXPECT_TRUE(overlay.scope == qhc::HostPreferenceScope::ViewportStyle);
        EXPECT_TRUE(overlay.preference_ids.size() == 11U);
	EXPECT_TRUE(group_contains(overlay, "viewport.overlay.diagnostic_edge_color"));
	EXPECT_TRUE(group_contains(overlay, "viewport.overlay.diagnostic_edge_line_size"));

	const qhc::HostPreferenceGroupDescriptor& picking = require_group("viewport.picking");
        EXPECT_TRUE(picking.scope == qhc::HostPreferenceScope::ViewportPicking);
        EXPECT_TRUE(picking.preference_ids.size() == 2U);
	EXPECT_TRUE(group_contains(picking, "viewport.picking.vertex_radius"));
	EXPECT_TRUE(group_contains(picking, "viewport.picking.edge_radius"));

	for (const qhc::HostPreferenceGroupDescriptor& group : groups) {
		for (std::string_view preference_id : group.preference_ids) {
			const qhc::HostPreferenceDescriptor *preference = qhc::find_preference_descriptor(preference_id);
			EXPECT_TRUE(preference != nullptr);
			EXPECT_TRUE(same_text(preference->group_id, group.id));
		}
	}
}

TEST(QdrEditorMetadataPreferenceCatalogTests, OperationPreferenceGroupsCoverSchemaOwnedOptionDescriptorCatalog) {
	for (const qec::EditorOperationOptionDescriptor& schema : qec::operation_option_descriptor_catalog()) {
		const qhc::HostPreferenceDescriptor& preference = require_preference(schema.preference_id);
		EXPECT_TRUE(same_text(preference.group_id, schema.preference_group_id));
		EXPECT_TRUE(same_text(preference.related_operation_id, schema.operation_id));
		EXPECT_TRUE(same_text(preference.related_option_id, schema.option_id));

		const qhc::HostPreferenceGroupDescriptor& group = require_group(schema.preference_group_id);
		EXPECT_TRUE(same_text(group.label, schema.operation_label));
                EXPECT_TRUE(group.scope == qhc::HostPreferenceScope::Operation);
                EXPECT_TRUE(group_contains(group, schema.preference_id));
	}
}

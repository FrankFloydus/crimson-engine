////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <catch2/catch_test_macros.hpp>

#include <editor/modeling/runtime/session.h>
#include <editor/metadata/host_contract.h>

#include <array>
#include <string_view>

namespace qhc = quader_host_contract;
namespace qec = quader_editor_core;

namespace {

bool same_text(std::string_view left, std::string_view right)
{
	return left == right;
}

std::size_t layer_id_count(std::span<const std::string_view> ids, std::string_view id)
{
	std::size_t count = 0;
	for (const std::string_view candidate : ids) {
		if (candidate == id) {
			++count;
		}
	}
	return count;
}

bool catalog_contains_layer_id(std::span<const qhc::HostOverlayLayerDescriptor> layers, std::string_view id)
{
	for (const qhc::HostOverlayLayerDescriptor& layer : layers) {
		if (layer.id == id) {
			return true;
		}
	}
	return false;
}

const qhc::HostOverlayLayerDescriptor& require_layer(qec::SelectionOverlayLayer layer)
{
	const qhc::HostOverlayLayerDescriptor *descriptor =
		qhc::find_overlay_layer_descriptor(static_cast<int>(layer));
	REQUIRE(descriptor != nullptr);
	return *descriptor;
}

} // namespace

TEST_CASE("host contract overlay layer catalog mirrors stable editor-core layer indices", "[host-contract]")
{
	const std::span<const qhc::HostOverlayLayerDescriptor> layers = qhc::overlay_layer_catalog();
	REQUIRE(layers.size() == 15U);

	CHECK(require_layer(qec::SelectionOverlayLayer::selected_visible_face).stable_index == 0);
	CHECK(require_layer(qec::SelectionOverlayLayer::selected_face_edge).stable_index == 1);
	CHECK(require_layer(qec::SelectionOverlayLayer::unselected_vertex).stable_index == 2);
	CHECK(require_layer(qec::SelectionOverlayLayer::selected_vertex).stable_index == 3);
	CHECK(require_layer(qec::SelectionOverlayLayer::hover_face).stable_index == 4);
	CHECK(require_layer(qec::SelectionOverlayLayer::hover_face_edge).stable_index == 5);
	CHECK(require_layer(qec::SelectionOverlayLayer::hover_edge).stable_index == 6);
	CHECK(require_layer(qec::SelectionOverlayLayer::hover_vertex).stable_index == 7);
	CHECK(require_layer(qec::SelectionOverlayLayer::selected_edge).stable_index == 8);
	CHECK(require_layer(qec::SelectionOverlayLayer::selected_xray_face).stable_index == 9);
	CHECK(require_layer(qec::SelectionOverlayLayer::selected_face_mask).stable_index == 10);
	CHECK(require_layer(qec::SelectionOverlayLayer::source_wire).stable_index == 11);
	CHECK(require_layer(qec::SelectionOverlayLayer::open_edge).stable_index == 12);
	CHECK(require_layer(qec::SelectionOverlayLayer::snap_target_vertex).stable_index == 13);
	CHECK(require_layer(qec::SelectionOverlayLayer::diagnostic_edge).stable_index == 14);
}

TEST_CASE("host contract overlay layer catalog states render-depth semantics", "[host-contract]")
{
	const qhc::HostOverlayLayerDescriptor& source_wire = require_layer(qec::SelectionOverlayLayer::source_wire);
	CHECK(same_text(source_wire.id, "source_wire"));
	CHECK(source_wire.lines);
	CHECK_FALSE(source_wire.line_draws_on_top);
	CHECK(source_wire.depth_tested_by_default);

	const qhc::HostOverlayLayerDescriptor& open_edge = require_layer(qec::SelectionOverlayLayer::open_edge);
	CHECK(open_edge.lines);
	CHECK_FALSE(open_edge.line_draws_on_top);
	CHECK(open_edge.depth_tested_by_default);

	const qhc::HostOverlayLayerDescriptor& selected_face_edge = require_layer(qec::SelectionOverlayLayer::selected_face_edge);
	CHECK(selected_face_edge.lines);
	CHECK(selected_face_edge.line_draws_on_top);
	CHECK_FALSE(selected_face_edge.depth_tested_by_default);

	const qhc::HostOverlayLayerDescriptor& selected_edge = require_layer(qec::SelectionOverlayLayer::selected_edge);
	CHECK(selected_edge.lines);
	CHECK(selected_edge.line_draws_on_top);
	CHECK_FALSE(selected_edge.depth_tested_by_default);

	const qhc::HostOverlayLayerDescriptor& selected_vertex = require_layer(qec::SelectionOverlayLayer::selected_vertex);
	CHECK(selected_vertex.points);
	CHECK(selected_vertex.point_draws_on_top);
	CHECK_FALSE(selected_vertex.depth_tested_by_default);

	const qhc::HostOverlayLayerDescriptor& hover_vertex = require_layer(qec::SelectionOverlayLayer::hover_vertex);
	CHECK(hover_vertex.points);
	CHECK(hover_vertex.point_draws_on_top);
	CHECK_FALSE(hover_vertex.depth_tested_by_default);

	const qhc::HostOverlayLayerDescriptor& snap_target_vertex = require_layer(qec::SelectionOverlayLayer::snap_target_vertex);
	CHECK(same_text(snap_target_vertex.id, "snap_target_vertex"));
	CHECK(snap_target_vertex.points);
	CHECK(snap_target_vertex.point_draws_on_top);
	CHECK_FALSE(snap_target_vertex.depth_tested_by_default);

	const qhc::HostOverlayLayerDescriptor& diagnostic_edge = require_layer(qec::SelectionOverlayLayer::diagnostic_edge);
	CHECK(same_text(diagnostic_edge.id, "diagnostic_edge"));
	CHECK(diagnostic_edge.lines);
	CHECK(diagnostic_edge.line_draws_on_top);
	CHECK_FALSE(diagnostic_edge.depth_tested_by_default);

	const qhc::HostOverlayLayerDescriptor& hover_face = require_layer(qec::SelectionOverlayLayer::hover_face);
	CHECK(hover_face.triangles);
	CHECK(hover_face.depth_tested_by_default);
}

TEST_CASE("host contract overlay presentation exposes the native layer draw order", "[host-contract]")
{
	const std::span<const std::string_view> ids = qhc::overlay_presentation().draw_order_layer_ids;
	static constexpr std::array<std::string_view, 15> expected {{
		"selected_visible_face",
		"selected_face_mask",
		"source_wire",
		"selected_xray_face",
		"hover_face",
		"open_edge",
		"diagnostic_edge",
		"selected_face_edge",
		"selected_edge",
		"hover_face_edge",
		"hover_edge",
		"unselected_vertex",
		"selected_vertex",
		"hover_vertex",
		"snap_target_vertex",
	}};

	REQUIRE(ids.size() == expected.size());
	for (std::size_t index = 0; index < expected.size(); ++index) {
		CHECK(ids[index] == expected[index]);
	}
}

TEST_CASE("host contract overlay draw order covers each catalog layer exactly once", "[host-contract]")
{
	const std::span<const qhc::HostOverlayLayerDescriptor> layers = qhc::overlay_layer_catalog();
	const std::span<const std::string_view> ids = qhc::overlay_presentation().draw_order_layer_ids;

	REQUIRE(ids.size() == layers.size());
	for (const qhc::HostOverlayLayerDescriptor& layer : layers) {
		CHECK(layer.stable_index >= 0);
		CHECK(layer_id_count(ids, layer.id) == 1U);
	}
	for (const std::string_view id : ids) {
		CHECK(catalog_contains_layer_id(layers, id));
	}
}

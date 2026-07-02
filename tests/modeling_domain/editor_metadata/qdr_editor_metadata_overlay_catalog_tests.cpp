////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <editor/modeling/runtime/session.hpp>
#include <editor/metadata/host_contract.hpp>

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
	EXPECT_TRUE(descriptor != nullptr);
	return *descriptor;
}

} // namespace

TEST(QdrEditorMetadataOverlayCatalogTests, HostContractOverlayLayerCatalogMirrorsStableEditorCoreLayerIndices) {
	const std::span<const qhc::HostOverlayLayerDescriptor> layers = qhc::overlay_layer_catalog();
	EXPECT_TRUE(layers.size() == 15U);

        EXPECT_TRUE(
            require_layer(qec::SelectionOverlayLayer::SelectedVisibleFace)
                .stable_index == 0);
        EXPECT_TRUE(require_layer(qec::SelectionOverlayLayer::SelectedFaceEdge)
                        .stable_index == 1);
        EXPECT_TRUE(require_layer(qec::SelectionOverlayLayer::UnselectedVertex)
                        .stable_index == 2);
        EXPECT_TRUE(require_layer(qec::SelectionOverlayLayer::SelectedVertex)
                        .stable_index == 3);
        EXPECT_TRUE(
            require_layer(qec::SelectionOverlayLayer::HoverFace).stable_index ==
            4);
        EXPECT_TRUE(require_layer(qec::SelectionOverlayLayer::HoverFaceEdge)
                        .stable_index == 5);
        EXPECT_TRUE(
            require_layer(qec::SelectionOverlayLayer::HoverEdge).stable_index ==
            6);
        EXPECT_TRUE(require_layer(qec::SelectionOverlayLayer::HoverVertex)
                        .stable_index == 7);
        EXPECT_TRUE(require_layer(qec::SelectionOverlayLayer::SelectedEdge)
                        .stable_index == 8);
        EXPECT_TRUE(require_layer(qec::SelectionOverlayLayer::SelectedXrayFace)
                        .stable_index == 9);
        EXPECT_TRUE(require_layer(qec::SelectionOverlayLayer::SelectedFaceMask)
                        .stable_index == 10);
        EXPECT_TRUE(require_layer(qec::SelectionOverlayLayer::SourceWire)
                        .stable_index == 11);
        EXPECT_TRUE(
            require_layer(qec::SelectionOverlayLayer::OpenEdge).stable_index ==
            12);
        EXPECT_TRUE(require_layer(qec::SelectionOverlayLayer::SnapTargetVertex)
                        .stable_index == 13);
        EXPECT_TRUE(require_layer(qec::SelectionOverlayLayer::DiagnosticEdge)
                        .stable_index == 14);
}

TEST(QdrEditorMetadataOverlayCatalogTests, HostContractOverlayLayerCatalogStatesRenderDepthSemantics) {
  const qhc::HostOverlayLayerDescriptor &source_wire =
      require_layer(qec::SelectionOverlayLayer::SourceWire);
  EXPECT_TRUE(same_text(source_wire.id, "source_wire"));
  EXPECT_TRUE(source_wire.lines);
  EXPECT_TRUE(source_wire.line_draws_on_top);
  EXPECT_FALSE(source_wire.depth_tested_by_default);

  const qhc::HostOverlayLayerDescriptor &open_edge =
      require_layer(qec::SelectionOverlayLayer::OpenEdge);
  EXPECT_TRUE(open_edge.lines);
  EXPECT_FALSE(open_edge.line_draws_on_top);
  EXPECT_TRUE(open_edge.depth_tested_by_default);

  const qhc::HostOverlayLayerDescriptor &selected_face_edge =
      require_layer(qec::SelectionOverlayLayer::SelectedFaceEdge);
  EXPECT_TRUE(selected_face_edge.lines);
  EXPECT_TRUE(selected_face_edge.line_draws_on_top);
  EXPECT_FALSE(selected_face_edge.depth_tested_by_default);

  const qhc::HostOverlayLayerDescriptor &selected_edge =
      require_layer(qec::SelectionOverlayLayer::SelectedEdge);
  EXPECT_TRUE(selected_edge.lines);
  EXPECT_TRUE(selected_edge.line_draws_on_top);
  EXPECT_FALSE(selected_edge.depth_tested_by_default);

  const qhc::HostOverlayLayerDescriptor &hover_face_edge =
      require_layer(qec::SelectionOverlayLayer::HoverFaceEdge);
  EXPECT_TRUE(hover_face_edge.lines);
  EXPECT_TRUE(hover_face_edge.line_draws_on_top);
  EXPECT_FALSE(hover_face_edge.depth_tested_by_default);

  const qhc::HostOverlayLayerDescriptor &hover_edge =
      require_layer(qec::SelectionOverlayLayer::HoverEdge);
  EXPECT_TRUE(hover_edge.lines);
  EXPECT_TRUE(hover_edge.line_draws_on_top);
  EXPECT_FALSE(hover_edge.depth_tested_by_default);

  const qhc::HostOverlayLayerDescriptor &selected_vertex =
      require_layer(qec::SelectionOverlayLayer::SelectedVertex);
  EXPECT_TRUE(selected_vertex.points);
  EXPECT_TRUE(selected_vertex.point_draws_on_top);
  EXPECT_FALSE(selected_vertex.depth_tested_by_default);

  const qhc::HostOverlayLayerDescriptor &hover_vertex =
      require_layer(qec::SelectionOverlayLayer::HoverVertex);
  EXPECT_TRUE(hover_vertex.points);
  EXPECT_TRUE(hover_vertex.point_draws_on_top);
  EXPECT_FALSE(hover_vertex.depth_tested_by_default);

  const qhc::HostOverlayLayerDescriptor &snap_target_vertex =
      require_layer(qec::SelectionOverlayLayer::SnapTargetVertex);
  EXPECT_TRUE(same_text(snap_target_vertex.id, "snap_target_vertex"));
  EXPECT_TRUE(snap_target_vertex.points);
  EXPECT_TRUE(snap_target_vertex.point_draws_on_top);
  EXPECT_FALSE(snap_target_vertex.depth_tested_by_default);

  const qhc::HostOverlayLayerDescriptor &diagnostic_edge =
      require_layer(qec::SelectionOverlayLayer::DiagnosticEdge);
  EXPECT_TRUE(same_text(diagnostic_edge.id, "diagnostic_edge"));
  EXPECT_TRUE(diagnostic_edge.lines);
  EXPECT_TRUE(diagnostic_edge.line_draws_on_top);
  EXPECT_FALSE(diagnostic_edge.depth_tested_by_default);

  const qhc::HostOverlayLayerDescriptor &hover_face =
      require_layer(qec::SelectionOverlayLayer::HoverFace);
  EXPECT_TRUE(hover_face.triangles);
  EXPECT_TRUE(hover_face.depth_tested_by_default);
}

TEST(QdrEditorMetadataOverlayCatalogTests, HostContractOverlayPresentationExposesTheNativeLayerDrawOrder) {
	const std::span<const std::string_view> ids = qhc::overlay_presentation().draw_order_layer_ids;
        static constexpr std::array<std::string_view, 15> kExpected{{
            "selected_visible_face",
            "selected_face_mask",
            "selected_xray_face",
            "hover_face",
            "source_wire",
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

        EXPECT_TRUE(ids.size() == kExpected.size());
        for (std::size_t index = 0; index < kExpected.size(); ++index) {
          EXPECT_TRUE(ids[index] == kExpected[index]);
        }
}

TEST(QdrEditorMetadataOverlayCatalogTests, HostContractOverlayDrawOrderCoversEachCatalogLayerExactlyOnce) {
	const std::span<const qhc::HostOverlayLayerDescriptor> layers = qhc::overlay_layer_catalog();
	const std::span<const std::string_view> ids = qhc::overlay_presentation().draw_order_layer_ids;

	EXPECT_TRUE(ids.size() == layers.size());
	for (const qhc::HostOverlayLayerDescriptor& layer : layers) {
		EXPECT_TRUE(layer.stable_index >= 0);
		EXPECT_TRUE(layer_id_count(ids, layer.id) == 1U);
	}
	for (const std::string_view id : ids) {
		EXPECT_TRUE(catalog_contains_layer_id(layers, id));
	}
}

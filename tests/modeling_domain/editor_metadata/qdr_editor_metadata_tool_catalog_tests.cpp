////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <editor/interactions/tool_identity.hpp>
#include <editor/modeling/runtime/tool_registry.hpp>
#include <editor/metadata/host_contract.hpp>

#include <string_view>

namespace qhc = quader_host_contract;
namespace qec = quader_editor_core;

namespace {

bool same_text(std::string_view left, std::string_view right)
{
	return left == right;
}

const qhc::HostToolDescriptor& require_tool(std::string_view id)
{
	const qhc::HostToolDescriptor *descriptor = qhc::find_tool_descriptor(id);
	EXPECT_TRUE(descriptor != nullptr);
	return *descriptor;
}

} // namespace

TEST(QdrEditorMetadataToolCatalogTests, HostContractToolCatalogCoversEditorCoreToolRegistryIdentity) {
	const std::span<const qhc::HostToolDescriptor> descriptors = qhc::tool_catalog();
	const std::span<const qec::EditorTool* const> tools = qec::tool_registry().tools();

	EXPECT_GE(descriptors.size(), tools.size());

	for (const qec::EditorTool *tool : tools) {
		EXPECT_TRUE(tool != nullptr);
		const bool primitive_footprint = same_text(tool->id(), "box");
		const std::string_view host_id =
			primitive_footprint ? "create_primitive" : tool->id();
		const qhc::HostToolDescriptor& descriptor = require_tool(host_id);
		EXPECT_TRUE(same_text(descriptor.id, host_id));
		if (!primitive_footprint) {
			EXPECT_TRUE(same_text(descriptor.label, tool->label()));
		}
	}
}

TEST(QdrEditorMetadataToolCatalogTests, HostContractToolCatalogExposesHostGlueHintsWithoutExecutableSurface) {
	EXPECT_TRUE(require_tool("select").primary_shortcut.empty());
	EXPECT_TRUE(same_text(require_tool("lasso_select").primary_shortcut, "Shift+Q"));
	EXPECT_EQ(require_tool("paint_select").primary_shortcut, "Ctrl+Q");
	EXPECT_TRUE(same_text(require_tool("move").primary_shortcut, "W"));
	EXPECT_TRUE(same_text(require_tool("rotate").primary_shortcut, "R"));
	EXPECT_TRUE(same_text(require_tool("scale").primary_shortcut, "S"));
	EXPECT_TRUE(same_text(
		require_tool("create_primitive").primary_shortcut, ""));
	EXPECT_TRUE(same_text(require_tool("insert_edge_loop").primary_shortcut, "Shift+R"));
	EXPECT_TRUE(same_text(require_tool("knife").primary_shortcut, "C"));
	EXPECT_TRUE(same_text(require_tool("cut").primary_shortcut, ""));
	EXPECT_TRUE(same_text(require_tool("mirror").primary_shortcut, "M"));
	EXPECT_TRUE(same_text(require_tool("poly").primary_shortcut, ""));

	EXPECT_TRUE(require_tool("move").transform_tool);
	EXPECT_TRUE(require_tool("extrude").transform_tool);
	EXPECT_TRUE(require_tool("rotate").transform_tool);
	EXPECT_TRUE(require_tool("scale").transform_tool);
	EXPECT_FALSE(require_tool("select").transform_tool);
	EXPECT_FALSE(require_tool("lasso_select").transform_tool);
	EXPECT_FALSE(require_tool("lasso_select").modal_tool);
	EXPECT_FALSE(require_tool("paint_select").transform_tool);
	EXPECT_FALSE(require_tool("paint_select").modal_tool);

	EXPECT_TRUE(require_tool("create_primitive").modal_tool);
	EXPECT_TRUE(require_tool("insert_edge_loop").modal_tool);
	EXPECT_TRUE(require_tool("knife").modal_tool);
	EXPECT_TRUE(require_tool("cut").modal_tool);
	EXPECT_TRUE(require_tool("mirror").modal_tool);
	EXPECT_TRUE(require_tool("poly").modal_tool);
	EXPECT_FALSE(require_tool("move").modal_tool);
}

TEST(QdrEditorMetadataToolCatalogTests, HostContractTransformAxisCatalogMirrorsEditorCoreAxisIdentifiers) {
	const std::span<const qhc::HostTransformAxisDescriptor> axes = qhc::transform_axis_catalog();
	EXPECT_TRUE(axes.size() == 7U);

        EXPECT_TRUE(same_text(axes[0].id, qec::axis_id(qec::TransformAxis::X)));
        EXPECT_TRUE(same_text(axes[1].id, qec::axis_id(qec::TransformAxis::Y)));
        EXPECT_TRUE(same_text(axes[2].id, qec::axis_id(qec::TransformAxis::Z)));
        EXPECT_TRUE(
            same_text(axes[3].id, qec::axis_id(qec::TransformAxis::Xy)));
        EXPECT_TRUE(
            same_text(axes[4].id, qec::axis_id(qec::TransformAxis::Xz)));
        EXPECT_TRUE(
            same_text(axes[5].id, qec::axis_id(qec::TransformAxis::Yz)));
        EXPECT_TRUE(
            same_text(axes[6].id, qec::axis_id(qec::TransformAxis::All)));

        EXPECT_FALSE(axes[0].planar);
	EXPECT_TRUE(axes[3].planar);
	EXPECT_TRUE(axes[4].planar);
	EXPECT_TRUE(axes[5].planar);
}

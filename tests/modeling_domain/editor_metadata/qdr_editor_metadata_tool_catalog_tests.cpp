////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <catch2/catch_test_macros.hpp>

#include <editor/interactions/tool_identity.h>
#include <editor/modeling/runtime/tool_registry.h>
#include <editor/metadata/host_contract.h>

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
	REQUIRE(descriptor != nullptr);
	return *descriptor;
}

} // namespace

TEST_CASE("host contract tool catalog mirrors editor-core tool registry identity", "[host-contract]")
{
	const std::span<const qhc::HostToolDescriptor> descriptors = qhc::tool_catalog();
	const std::span<const qec::EditorTool* const> tools = qec::tool_registry().tools();

	REQUIRE(descriptors.size() == tools.size());

	for (const qec::EditorTool *tool : tools) {
		REQUIRE(tool != nullptr);
		const qhc::HostToolDescriptor& descriptor = require_tool(tool->id());
		CHECK(same_text(descriptor.id, tool->id()));
		CHECK(same_text(descriptor.label, tool->label()));
	}
}

TEST_CASE("host contract tool catalog exposes host glue hints without executable surface", "[host-contract]")
{
	CHECK(same_text(require_tool("select").primary_shortcut, "Q"));
	CHECK(same_text(require_tool("move").primary_shortcut, "W"));
	CHECK(same_text(require_tool("rotate").primary_shortcut, "R"));
	CHECK(same_text(require_tool("scale").primary_shortcut, "S"));
	CHECK(same_text(require_tool("box").primary_shortcut, "B"));
	CHECK(same_text(require_tool("insert_edge_loop").primary_shortcut, "Shift+R"));
	CHECK(same_text(require_tool("knife").primary_shortcut, "C"));
	CHECK(same_text(require_tool("cut").primary_shortcut, "Shift+C"));
	CHECK(same_text(require_tool("mirror").primary_shortcut, ""));
	CHECK(same_text(require_tool("poly").primary_shortcut, "P"));

	CHECK(require_tool("move").transform_tool);
	CHECK(require_tool("extend").transform_tool);
	CHECK(require_tool("extrude").transform_tool);
	CHECK(require_tool("rotate").transform_tool);
	CHECK(require_tool("scale").transform_tool);
	CHECK_FALSE(require_tool("select").transform_tool);

	CHECK(require_tool("box").modal_tool);
	CHECK(require_tool("insert_edge_loop").modal_tool);
	CHECK(require_tool("knife").modal_tool);
	CHECK(require_tool("cut").modal_tool);
	CHECK(require_tool("mirror").modal_tool);
	CHECK(require_tool("poly").modal_tool);
	CHECK_FALSE(require_tool("move").modal_tool);
}

TEST_CASE("host contract transform axis catalog mirrors editor-core axis identifiers", "[host-contract]")
{
	const std::span<const qhc::HostTransformAxisDescriptor> axes = qhc::transform_axis_catalog();
	REQUIRE(axes.size() == 7U);

	CHECK(same_text(axes[0].id, qec::axis_id(qec::TransformAxis::x)));
	CHECK(same_text(axes[1].id, qec::axis_id(qec::TransformAxis::y)));
	CHECK(same_text(axes[2].id, qec::axis_id(qec::TransformAxis::z)));
	CHECK(same_text(axes[3].id, qec::axis_id(qec::TransformAxis::xy)));
	CHECK(same_text(axes[4].id, qec::axis_id(qec::TransformAxis::xz)));
	CHECK(same_text(axes[5].id, qec::axis_id(qec::TransformAxis::yz)));
	CHECK(same_text(axes[6].id, qec::axis_id(qec::TransformAxis::all)));

	CHECK_FALSE(axes[0].planar);
	CHECK(axes[3].planar);
	CHECK(axes[4].planar);
	CHECK(axes[5].planar);
}

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <editor/interactions/interaction_api.hpp>
#include <editor/metadata/host_contract.hpp>
#include <quader/modeling/modeling.hpp>

#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace qec = quader_editor_core;
namespace qhc = quader_host_contract;

namespace {

bool catalog_has_operation(std::string_view id)
{
	return qhc::find_operation_descriptor(id) != nullptr;
}

bool catalog_has_tool(std::string_view id)
{
	return qhc::find_tool_descriptor(id) != nullptr;
}

qec::ViewportCameraState front_camera()
{
	qec::ViewportCameraState camera;
        camera.projection = qec::CameraProjection::Perspective;
        camera.transform.origin = { 0.0F, 0.0F, 6.0F };
	camera.transform.x_axis = { 1.0F, 0.0F, 0.0F };
	camera.transform.y_axis = { 0.0F, 1.0F, 0.0F };
	camera.transform.z_axis = { 0.0F, 0.0F, 1.0F };
	camera.viewport_size = { 800.0F, 600.0F };
	camera.fov_degrees = 60.0;
	return camera;
}

void require_compilable_mesh(const quader::modeling::MeshHandle &mesh)
{
	ASSERT_TRUE(mesh.valid());
	const quader::modeling::MeshPayload payload = mesh.payloads().compile_mesh();
	EXPECT_FALSE(payload.vertices.empty());
	EXPECT_FALSE(payload.indices.empty());
	EXPECT_FALSE(payload.primitives.empty());
}

std::string read_text_file(const char *path)
{
	std::ifstream input(path, std::ios::binary);
	EXPECT_TRUE(input.good());
	return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
}

} // namespace

TEST(QdrCustomEngineModelingRuntimeSmokeTests, CustomEngineConsumerCanRunAModelingSessionThroughAppOwnedModelingDomain) {
	EXPECT_TRUE(catalog_has_operation("duplicate_meshes"));
	EXPECT_TRUE(catalog_has_tool("move"));
	EXPECT_FALSE(qhc::ui_placement_catalog().empty());
	EXPECT_FALSE(qhc::overlay_layer_catalog().empty());
	EXPECT_FALSE(qhc::material_recipe_catalog().empty());

	quader::modeling::ModelingApi modeling =
		quader::modeling::ModelingApi::create();
	quader::modeling::MeshHandle source =
		modeling.create_box({ .name = "Custom Engine Box" });
	ASSERT_TRUE(source.valid());
	quader::modeling::MeshHandle duplicate = source.duplicate({
		.offset = { 2.0F, 0.0F, 0.0F },
	});
	ASSERT_TRUE(duplicate.valid());
	EXPECT_EQ(modeling.mesh_summaries().size(), 2U);
	require_compilable_mesh(source);
	require_compilable_mesh(duplicate);

	qec::TransformGizmoFrameInput gizmo_input;
        gizmo_input.tool = qec::ViewportInteractionTool::Move;
	gizmo_input.has_selection = true;
	gizmo_input.has_pivot = true;
	gizmo_input.pivot = { 0.0F, 0.0F, 0.0F };
	gizmo_input.camera = front_camera();

	const qec::TransformGizmoFrame gizmo = qec::build_transform_gizmo_frame(gizmo_input);
	EXPECT_TRUE(gizmo.info.ok);
	EXPECT_FALSE(gizmo.axes.empty());
	EXPECT_FALSE(gizmo.planes.empty());

}

#ifdef QUADER_CUSTOM_ENGINE_SMOKE_SOURCE_PATH
TEST(QdrCustomEngineModelingRuntimeSmokeTests, CustomEngineSmokeTestStaysOffLegacyAdapterSurfaces) {
	const std::string source = read_text_file(QUADER_CUSTOM_ENGINE_SMOKE_SOURCE_PATH);
	const std::string bridge_name = std::string("qdr_") + "un" + "ity";
	const std::string managed_import = std::string("Dll") + "Import";
	const std::string session_export_prefix = std::string("qdr_") + "session_";

	EXPECT_TRUE(source.find(bridge_name) == std::string::npos);
	EXPECT_TRUE(source.find(managed_import) == std::string::npos);
	EXPECT_TRUE(source.find(std::string("native") + "/include") == std::string::npos);
	EXPECT_TRUE(source.find(std::string("native") + "\\include") == std::string::npos);
	EXPECT_TRUE(source.find(session_export_prefix) == std::string::npos);
}
#endif

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <editor/modeling/operations/operation_command.hpp>
#include <editor/modeling/runtime/session.hpp>
#include <editor/interactions/interaction_api.hpp>
#include <editor/metadata/host_contract.hpp>
#include <io/qdr/qdr_runtime_io_compat.hpp>
#include <mesh/polygon/document.hpp>

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
	camera.projection = qec::CameraProjection::perspective;
	camera.transform.origin = { 0.0F, 0.0F, 6.0F };
	camera.transform.x_axis = { 1.0F, 0.0F, 0.0F };
	camera.transform.y_axis = { 0.0F, 1.0F, 0.0F };
	camera.transform.z_axis = { 0.0F, 0.0F, 1.0F };
	camera.viewport_size = { 800.0F, 600.0F };
	camera.fov_degrees = 60.0;
	return camera;
}

void require_compilable_meshes(const qec::EditorSession& session)
{
	EXPECT_FALSE(session.objects().empty());
	for (const qec::PolygonObject& object : session.objects()) {
		const quader_poly::CompiledMesh mesh = quader_poly::compile_document(object.document);
		EXPECT_FALSE(mesh.vertices.empty());
		EXPECT_FALSE(mesh.indices.empty());
		EXPECT_FALSE(mesh.primitives.empty());
	}
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

	qec::EditorSession session;
	const qec::CommandResult create = session.create_box_from_bounds(
		{ -1.0F, -1.0F, -1.0F },
		{ 1.0F, 1.0F, 1.0F });
	EXPECT_TRUE(create.ok);
	EXPECT_TRUE(create.changed);
	EXPECT_TRUE(session.has_selection());
	EXPECT_TRUE(session.objects().size() == 1U);

	qec::DuplicateMeshesOperationSettings duplicate_settings;
	duplicate_settings.explicit_offset = { { 2.0F, 0.0F, 0.0F }, true };

	qec::EditorOperationCommand duplicate_command;
	duplicate_command.operation_id = qec::EditorOperationId::duplicate_meshes;
	duplicate_command.phase = qec::EditorOperationCommandPhase::execute;
	duplicate_command.settings = duplicate_settings;

	const qec::EditorOperationCommandResult duplicate = qec::execute_command(session, duplicate_command);
	EXPECT_TRUE(duplicate.command.ok);
	EXPECT_TRUE(duplicate.command.changed);
	EXPECT_TRUE(session.has_selection());
	EXPECT_TRUE(session.objects().size() == 2U);
	require_compilable_meshes(session);

	qec::SelectionHoverState hover;
	const qec::SelectionOverlayFrame overlay =
		qec::build_selection_overlay_frame(session.objects(), session.selection_mode(), hover);
	EXPECT_FALSE(overlay.triangles.empty());
	EXPECT_FALSE(overlay.lines.empty());

	const std::optional<quader::QVec3> pivot = session.selection_center();
	EXPECT_TRUE(pivot.has_value());

	qec::TransformGizmoFrameInput gizmo_input;
	gizmo_input.tool = qec::ViewportInteractionTool::move;
	gizmo_input.has_selection = session.has_selection();
	gizmo_input.has_pivot = true;
	gizmo_input.pivot = *pivot;
	gizmo_input.camera = front_camera();

	const qec::TransformGizmoFrame gizmo = qec::build_transform_gizmo_frame(gizmo_input);
	EXPECT_TRUE(gizmo.info.ok);
	EXPECT_FALSE(gizmo.axes.empty());
	EXPECT_FALSE(gizmo.planes.empty());

	const std::string serialized = quader_io::serialize_session_state(session.state());
	EXPECT_TRUE(serialized.find("\"format\": \"quader.qdr\"") != std::string::npos);

	const quader_io::ReadResult read = quader_io::deserialize_session_state(serialized);
	EXPECT_TRUE(read.ok);

	const qec::EditorSession loaded(read.state);
	EXPECT_TRUE(loaded.objects().size() == session.objects().size());
	require_compilable_meshes(loaded);
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

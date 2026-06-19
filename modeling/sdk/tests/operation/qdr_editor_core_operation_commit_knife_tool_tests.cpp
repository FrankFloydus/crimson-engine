#include "editor_operation_test_helpers.h"

using namespace quader_poly;

namespace {

std::vector<KnifePointTarget> quad_knife_points(const qdr_test::QuadFixture& fixture)
{
	return {
		{
			KnifePointTargetKind::existing_edge,
			invalid_element_id,
			make_edge(fixture.bottom_left, fixture.bottom_right),
			0.5F,
			fixture.face,
			{},
		},
		{
			KnifePointTargetKind::face_point,
			invalid_element_id,
			{},
			0.5F,
			fixture.face,
			{ 0.0F, 0.0F, 0.0F },
		},
		{
			KnifePointTargetKind::existing_edge,
			invalid_element_id,
			make_edge(fixture.top_right, fixture.top_left),
			0.5F,
			fixture.face,
			{},
		},
	};
}

} // namespace

TEST_CASE("commit knife tool applies the captured stroke through editor history", "[operation][commit_knife_tool]")
{
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	quader_editor_core::PolygonObject object = qdr_test::make_object(1, fixture.document);
	quader_editor_core::EditorSession session({ object });
	quader_editor_core::KnifeToolState state;
	state.active = true;
	state.has_active_stroke = true;
	state.target_object_id = 1;
	state.points = quad_knife_points(fixture);
	state.point_positions = {
		{ 0.0F, -1.0F, 0.0F },
		{ 0.0F, 0.0F, 0.0F },
		{ 0.0F, 1.0F, 0.0F },
	};
	state.segments = {
		{ 0U, 1U },
		{ 1U, 2U },
	};
	const std::size_t face_count_before = session.objects().front().document.faces.size();

	const quader_editor_core::CommandResult result = session.commit_knife_tool(state);

	REQUIRE(result.ok);
	REQUIRE(result.changed);
	REQUIRE(session.objects().front().document.faces.size() > face_count_before);
	REQUIRE(session.history_status().can_undo);
	REQUIRE_FALSE(quader_editor_core::knife_tool_preview(state).active);
	REQUIRE(state.points.empty());
	REQUIRE(state.segments.empty());
	REQUIRE(session.undo().applied);
	REQUIRE(session.objects().front().document.faces.size() == face_count_before);
}

TEST_CASE("commit knife tool rejects incomplete or targetless state predictably", "[operation][commit_knife_tool]")
{
	SECTION("empty stroke resets as unchanged success")
	{
		quader_editor_core::EditorSession session;
		session.reset_to_default_scene();
		quader_editor_core::KnifeToolState state;
		state.active = true;

		const quader_editor_core::CommandResult result = session.commit_knife_tool(state);

		REQUIRE(result.ok);
		REQUIRE_FALSE(result.changed);
		REQUIRE_FALSE(quader_editor_core::knife_tool_preview(state).active);
	}

	SECTION("segments without a target keep the state inspectable and invalid")
	{
		quader_editor_core::EditorSession session;
		session.reset_to_default_scene();
		quader_editor_core::KnifeToolState state;
		state.active = true;
		state.segments = { { 0U, 1U } };
		state.target_object_id = -1;

		const quader_editor_core::CommandResult result = session.commit_knife_tool(state);

		REQUIRE_FALSE(result.ok);
		REQUIRE_FALSE(result.changed);
		REQUIRE(state.status == quader_editor_core::KnifeToolStatus::invalid_target);
		REQUIRE(quader_editor_core::knife_tool_preview(state).active);
	}
}

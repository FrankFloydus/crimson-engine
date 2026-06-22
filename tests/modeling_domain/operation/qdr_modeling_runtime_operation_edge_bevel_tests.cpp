////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.h"

#include <editor/modeling/operations/operation_schema.h>

#include <limits>

using namespace quader_poly;

TEST_CASE("edge bevel operation schema normalizes non-finite and out-of-range settings", "[operation][edge_bevel]")
{
	const quader_editor_core::EdgeBevelOperationSchema bevel = quader_editor_core::edge_bevel_operation_schema();

	REQUIRE(bevel.id == quader_editor_core::EditorOperationId::edge_bevel);
	REQUIRE(std::string(bevel.operation_id) == "edge_bevel");
	REQUIRE(std::string(bevel.width.id) == "width");
	REQUIRE(std::string(bevel.profile.id) == "profile");
	REQUIRE(std::string(bevel.segments.id) == "segments");
	REQUIRE(bevel.width.default_value == Catch::Approx(1.0));
	REQUIRE(bevel.width.minimum == Catch::Approx(0.0625));
	REQUIRE(bevel.width.maximum == Catch::Approx(16384.0));
	REQUIRE(bevel.width.step == Catch::Approx(1.0));
	REQUIRE(bevel.profile.default_value == Catch::Approx(0.5));
	REQUIRE(bevel.segments.default_value == 1);
	REQUIRE(bevel.segments.maximum == 16);
	REQUIRE(bevel.default_profile_type == quader_poly::BevelProfileType::offset);

	quader_editor_core::EdgeBevelOperationSettings settings;
	settings.width = std::numeric_limits<double>::quiet_NaN();
	settings.profile = std::numeric_limits<double>::infinity();
	settings.profile_type = -100;
	settings.segments = -2;
	const quader_editor_core::EdgeBevelOperationSettings normalized =
		quader_editor_core::normalize_edge_bevel_operation_settings(settings);

	REQUIRE(normalized.width == Catch::Approx(bevel.width.default_value));
	REQUIRE(normalized.profile == Catch::Approx(bevel.profile.default_value));
	REQUIRE(normalized.segments == bevel.segments.minimum);
	REQUIRE(normalized.profile_type == static_cast<int>(quader_poly::BevelProfileType::offset));

	const quader_editor_core::EdgeBevelOperationSettings high =
		quader_editor_core::normalize_edge_bevel_operation_settings({ 20000.0, 2.0, 999, 999 });
	REQUIRE(high.width == Catch::Approx(bevel.width.maximum));
	REQUIRE(high.profile == Catch::Approx(bevel.profile.maximum));
	REQUIRE(high.segments == bevel.segments.maximum);
}

TEST_CASE("edge bevel operation previews from base topology and commits through history", "[operation][edge_bevel]")
{
	SECTION("edge bevel preview rebuilds from base and commits to history")
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		quader_editor_core::PolygonObject& object = session.objects().front();
		const Edge edge = document_edges(object.document).front();
		session.set_component_selection(object.id, qdr_test::edge_selection({ edge }, edge), quader_editor_core::SelectionMode::edge);
		const std::size_t face_count_before = object.document.faces.size();

		EdgeBevelSettings settings;
		settings.width = 0.1F;
		settings.segments = 2;
		REQUIRE(session.begin_edge_bevel_preview(settings).changed);
		REQUIRE(session.preview_transaction_status().kind == quader_editor_core::PreviewTransactionKind::edge_bevel);
		settings.width = 0.2F;
		REQUIRE(session.update_edge_bevel_preview(settings).changed);
		REQUIRE(session.objects().front().document.faces.size() > face_count_before);
		const quader_editor_core::PreviewTransactionFinishResult commit =
			session.commit_preview_transaction(quader_editor_core::PreviewTransactionKind::edge_bevel);
		REQUIRE(commit.before.changed);
		REQUIRE(session.history_status().can_undo);
	}

	SECTION("schema settings clamp invalid bevel ranges for preview")
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		quader_editor_core::PolygonObject& object = session.objects().front();
		const Edge edge = document_edges(object.document).front();
		session.set_component_selection(object.id, qdr_test::edge_selection({ edge }, edge), quader_editor_core::SelectionMode::edge);

		quader_editor_core::EdgeBevelOperationSettings settings;
		settings.width = -10.0;
		settings.profile = 20.0;
		settings.segments = 1000;
		const quader_editor_core::CommandResult begin = session.begin_edge_bevel_preview(settings);

		REQUIRE(begin.ok);
		REQUIRE(begin.changed);
		REQUIRE(session.preview_transaction_status().active);
		REQUIRE(session.preview_transaction_status().kind == quader_editor_core::PreviewTransactionKind::edge_bevel);
	}

	SECTION("preview without an edge selection fails and clears the transaction")
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();

		const quader_editor_core::CommandResult begin =
			session.begin_edge_bevel_preview(quader_poly::EdgeBevelSettings {});

		REQUIRE_FALSE(begin.ok);
		REQUIRE_FALSE(begin.changed);
		REQUIRE_FALSE(session.preview_transaction_status().active);
	}
}

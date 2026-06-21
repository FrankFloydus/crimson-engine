#include "editor_operation_test_helpers.h"

#include <quader_editor_core/operation_schema.h>

#include <algorithm>
#include <limits>

namespace {

const quader_editor_core::PolygonObject* find_object(const quader_editor_core::EditorSession& session, int object_id)
{
	const auto& objects = session.objects();
	const auto it = std::find_if(objects.begin(), objects.end(), [object_id](const quader_editor_core::PolygonObject& object) {
		return object.id == object_id;
	});
	return it == objects.end() ? nullptr : &*it;
}

quader_poly::Vec3 first_vertex_position(const quader_poly::Document& document)
{
	REQUIRE_FALSE(document.vertices.empty());
	return document.vertices.front().position;
}

quader_poly::ElementId first_vertex_id(const quader_poly::Document& document)
{
	REQUIRE_FALSE(document.vertices.empty());
	return document.vertices.front().id;
}

quader_poly::Vec3 vertex_position(const quader_poly::Document& document, quader_poly::ElementId vertex_id)
{
	const quader_poly::Vertex* vertex = quader_poly::find_vertex(document, vertex_id);
	REQUIRE(vertex != nullptr);
	return vertex->position;
}

quader_poly::Vec3 averaged_face_normal_for_vertex(const quader_poly::Document& document, quader_poly::ElementId vertex_id)
{
	quader_poly::Vec3 normal {};
	for (const quader_poly::Face& face : document.faces) {
		if (std::find(face.vertices.begin(), face.vertices.end(), vertex_id) != face.vertices.end()) {
			normal += quader_poly::face_normal(document, face);
		}
	}
	return quader_poly::normalize_or_zero(normal);
}

} // namespace

TEST_CASE("thicken operation schema keeps soft thickness limits and hidden center toggle", "[operation][thicken]")
{
	const quader_editor_core::ThickenOperationSchema thicken = quader_editor_core::thicken_operation_schema();

	REQUIRE(thicken.id == quader_editor_core::EditorOperationId::thicken);
	REQUIRE(std::string(thicken.operation_id) == "thicken");
	REQUIRE(std::string(thicken.thickness.id) == "thickness");
	REQUIRE(std::string(thicken.from_center.id) == "from_center");
	REQUIRE(thicken.thickness.default_value == Catch::Approx(0.25));
	REQUIRE(thicken.thickness.minimum == Catch::Approx(-4.0));
	REQUIRE(thicken.thickness.maximum == Catch::Approx(4.0));
	REQUIRE_FALSE(thicken.thickness.hard_limits);
	REQUIRE_FALSE(thicken.from_center.visible);

	quader_editor_core::ThickenOperationSettings settings;
	settings.thickness = 100.0;
	settings.from_center = true;
	const quader_editor_core::ThickenOperationSettings normalized =
		quader_editor_core::normalize_thicken_operation_settings(settings);
	REQUIRE(normalized.thickness == Catch::Approx(100.0));
	REQUIRE(normalized.from_center);

	settings.thickness = -std::numeric_limits<double>::infinity();
	const quader_editor_core::ThickenOperationSettings invalid =
		quader_editor_core::normalize_thicken_operation_settings(settings);
	REQUIRE(invalid.thickness == Catch::Approx(thicken.thickness.default_value));
}

TEST_CASE("thicken operation previews generated topology transactionally", "[operation][thicken]")
{
	SECTION("object mode creates a separate outer hull without mutating the source mesh")
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		REQUIRE(session.selection_mode() == quader_editor_core::SelectionMode::object);
		REQUIRE(session.selected_object_ids().size() == 1U);
		const int source_id = session.selected_object_ids().front();
		const int next_id_before = session.next_object_id();
		const std::size_t object_count_before = session.objects().size();
		const std::size_t vertex_count_before = session.objects().front().document.vertices.size();
		const std::size_t face_count_before = session.objects().front().document.faces.size();
		const quader_poly::ElementId source_vertex_id = first_vertex_id(session.objects().front().document);
		const quader_poly::Vec3 source_position_before = first_vertex_position(session.objects().front().document);
		const quader_poly::Vec3 source_normal = averaged_face_normal_for_vertex(session.objects().front().document, source_vertex_id);
		REQUIRE(quader_poly::length_squared(source_normal) > 0.0F);

		REQUIRE(session.begin_thicken_preview(0.25F, false).changed);

		REQUIRE(session.preview_transaction_status().kind == quader_editor_core::PreviewTransactionKind::thicken);
		REQUIRE(session.selection_mode() == quader_editor_core::SelectionMode::object);
		REQUIRE(session.objects().size() == object_count_before + 1U);
		REQUIRE(session.selected_object_ids().size() == 1U);
		const int hull_id = session.selected_object_ids().front();
		REQUIRE(hull_id != source_id);
		REQUIRE(hull_id == next_id_before);
		const quader_editor_core::PolygonObject* source = find_object(session, source_id);
		const quader_editor_core::PolygonObject* hull = find_object(session, hull_id);
		REQUIRE(source != nullptr);
		REQUIRE(hull != nullptr);
		REQUIRE_FALSE(source->selected);
		REQUIRE(hull->selected);
		REQUIRE(source->document.vertices.size() == vertex_count_before);
		REQUIRE(source->document.faces.size() == face_count_before);
		REQUIRE(hull->document.vertices.size() == vertex_count_before);
		REQUIRE(hull->document.faces.size() == face_count_before);
		REQUIRE(quader_poly::length(vertex_position(source->document, source_vertex_id) - source_position_before) == Catch::Approx(0.0F));
		const quader_poly::Vec3 displacement = vertex_position(hull->document, source_vertex_id) - source_position_before;
		REQUIRE(quader_poly::length(displacement) == Catch::Approx(0.25F));
		REQUIRE(quader_poly::dot(displacement, source_normal) == Catch::Approx(-0.25F));

		const quader_editor_core::PreviewTransactionFinishResult commit =
			session.commit_preview_transaction(quader_editor_core::PreviewTransactionKind::thicken);
		REQUIRE(commit.before.changed);
		REQUIRE(session.objects().size() == object_count_before + 1U);
		REQUIRE(session.history_status().can_undo);
		REQUIRE(session.undo().applied);
		REQUIRE(session.selection_mode() == quader_editor_core::SelectionMode::object);
		REQUIRE(session.objects().size() == object_count_before);
		REQUIRE(session.selected_object_ids().size() == 1U);
		REQUIRE(session.selected_object_ids().front() == source_id);
	}

	SECTION("object mode cancel and update rebuild one outer hull from the preview base")
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const std::size_t object_count_before = session.objects().size();
		const quader_poly::ElementId source_vertex_id = first_vertex_id(session.objects().front().document);
		const quader_poly::Vec3 source_position_before = first_vertex_position(session.objects().front().document);
		const quader_poly::Vec3 source_normal = averaged_face_normal_for_vertex(session.objects().front().document, source_vertex_id);
		REQUIRE(quader_poly::length_squared(source_normal) > 0.0F);
		const std::size_t face_count_before = session.objects().front().document.faces.size();

		REQUIRE(session.begin_thicken_preview(0.25F, false).changed);
		REQUIRE(session.objects().size() == object_count_before + 1U);
		const quader_editor_core::PolygonObject* first_hull = find_object(session, session.selected_object_ids().front());
		REQUIRE(first_hull != nullptr);
		quader_poly::Vec3 displacement = vertex_position(first_hull->document, source_vertex_id) - source_position_before;
		REQUIRE(quader_poly::length(displacement) == Catch::Approx(0.25F));
		REQUIRE(quader_poly::dot(displacement, source_normal) == Catch::Approx(-0.25F));
		REQUIRE(session.update_thicken_preview(0.5F, false).changed);
		REQUIRE(session.objects().size() == object_count_before + 1U);
		const quader_editor_core::PolygonObject* updated_hull = find_object(session, session.selected_object_ids().front());
		REQUIRE(updated_hull != nullptr);
		displacement = vertex_position(updated_hull->document, source_vertex_id) - source_position_before;
		REQUIRE(quader_poly::length(displacement) == Catch::Approx(0.5F));
		REQUIRE(quader_poly::dot(displacement, source_normal) == Catch::Approx(-0.5F));

		const quader_editor_core::PreviewTransactionFinishResult cancel =
			session.cancel_preview_transaction(quader_editor_core::PreviewTransactionKind::thicken);

		REQUIRE(cancel.restored);
		REQUIRE(session.objects().size() == object_count_before);
		REQUIRE(session.objects().front().document.faces.size() == face_count_before);
		REQUIRE_FALSE(session.history_status().can_undo);
	}

	SECTION("commit records generated topology and undo restores the base")
	{
		quader_editor_core::PolygonObject object = qdr_test::make_object(1, qdr_test::make_quad_document().document);
		quader_editor_core::EditorSession session({ object });
		session.set_component_selection(1, qdr_test::face_selection({ session.objects().front().document.faces.front().id }), quader_editor_core::SelectionMode::face);
		const std::size_t face_count_before = session.objects().front().document.faces.size();

		REQUIRE(session.begin_thicken_preview(0.25F, false).changed);
		REQUIRE(session.preview_transaction_status().kind == quader_editor_core::PreviewTransactionKind::thicken);
		REQUIRE(session.objects().front().document.faces.size() > face_count_before);
		const quader_editor_core::PreviewTransactionFinishResult commit =
			session.commit_preview_transaction(quader_editor_core::PreviewTransactionKind::thicken);
		REQUIRE(commit.before.changed);
		REQUIRE(session.history_status().can_undo);
		REQUIRE(session.undo().applied);
		REQUIRE(session.objects().front().document.faces.size() == face_count_before);
	}

	SECTION("zero thickness preview stays active but unchanged until commit restores base")
	{
		quader_editor_core::PolygonObject object = qdr_test::make_object(1, qdr_test::make_quad_document().document);
		quader_editor_core::EditorSession session({ object });
		session.set_component_selection(1, qdr_test::face_selection({ session.objects().front().document.faces.front().id }), quader_editor_core::SelectionMode::face);
		const std::size_t face_count_before = session.objects().front().document.faces.size();

		const quader_editor_core::CommandResult begin = session.begin_thicken_preview(0.0F, false);
		const quader_editor_core::PreviewTransactionFinishResult commit =
			session.commit_preview_transaction(quader_editor_core::PreviewTransactionKind::thicken);

		REQUIRE(begin.ok);
		REQUIRE_FALSE(begin.changed);
		REQUIRE(commit.restored);
		REQUIRE_FALSE(session.history_status().can_undo);
		REQUIRE(session.objects().front().document.faces.size() == face_count_before);
	}

	SECTION("schema settings preserve soft thickness values for preview")
	{
		quader_editor_core::PolygonObject object = qdr_test::make_object(1, qdr_test::make_quad_document().document);
		quader_editor_core::EditorSession session({ object });
		session.set_component_selection(1, qdr_test::face_selection({ session.objects().front().document.faces.front().id }), quader_editor_core::SelectionMode::face);

		quader_editor_core::ThickenOperationSettings settings;
		settings.thickness = -0.25;
		settings.from_center = true;
		const quader_editor_core::CommandResult begin = session.begin_thicken_preview(settings);

		REQUIRE(begin.ok);
		REQUIRE(begin.changed);
		REQUIRE(session.preview_transaction_status().active);
		REQUIRE(session.preview_transaction_status().kind == quader_editor_core::PreviewTransactionKind::thicken);
	}

	SECTION("cancel and update rebuild from the preview base")
	{
		quader_editor_core::PolygonObject object = qdr_test::make_object(1, qdr_test::make_quad_document().document);
		quader_editor_core::EditorSession session({ object });
		session.set_component_selection(
			1,
			qdr_test::face_selection({ session.objects().front().document.faces.front().id }, session.objects().front().document.faces.front().id),
			quader_editor_core::SelectionMode::face);
		const std::size_t face_count_before = session.objects().front().document.faces.size();

		const quader_editor_core::CommandResult begin = session.begin_thicken_preview(0.25F, true);

		REQUIRE(begin.ok);
		REQUIRE(begin.changed);
		REQUIRE(session.preview_transaction_status().kind == quader_editor_core::PreviewTransactionKind::thicken);
		REQUIRE(session.objects().front().document.faces.size() > face_count_before);
		REQUIRE(session.update_thicken_preview(0.5F, true).changed);
		REQUIRE(session.objects().front().document.faces.size() > face_count_before);

		const quader_editor_core::PreviewTransactionFinishResult cancel =
			session.cancel_preview_transaction(quader_editor_core::PreviewTransactionKind::thicken);

		REQUIRE(cancel.restored);
		REQUIRE(session.objects().front().document.faces.size() == face_count_before);
		REQUIRE_FALSE(session.history_status().can_undo);

		REQUIRE(session.begin_thicken_preview(0.25F, true).changed);
		const quader_editor_core::PreviewTransactionFinishResult commit =
			session.commit_preview_transaction(quader_editor_core::PreviewTransactionKind::thicken);

		REQUIRE(commit.before.changed);
		REQUIRE_FALSE(commit.restored);
		REQUIRE(session.objects().front().document.faces.size() > face_count_before);
		REQUIRE(session.history_status().undo_count == 1U);

		const quader_editor_core::HistoryRestoreResult undo = session.undo();
		REQUIRE(undo.applied);
		REQUIRE(session.objects().front().document.faces.size() == face_count_before);
	}
}

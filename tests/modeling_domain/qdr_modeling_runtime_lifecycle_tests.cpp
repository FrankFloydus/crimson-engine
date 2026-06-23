////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.h"

#include <editor/modeling/runtime/session.h>
#include <mesh/polygon/document.h>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <optional>
#include <vector>

namespace {

bool near(float left, float right)
{
	return std::abs(left - right) <= 0.0001F;
}

bool vec_near(quader::QVec3 left, quader::QVec3 right)
{
	return near(left.x, right.x) && near(left.y, right.y) && near(left.z, right.z);
}

void default_scene_reset_restores_stable_runtime_defaults()
{
	quader_editor_core::EditorSession session;
	require_test(session.create_box_from_bounds({ -1.0F, -1.0F, -1.0F }, { 1.0F, 1.0F, 1.0F }).changed,
		"runtime reset fixture should create transient content");
	session.clear_history();
	session.reset_to_default_scene();

	require_test(session.objects().size() == 1, "default scene should contain one starter object");
	require_test(session.objects().front().id == 1, "default scene should restart object ids at 1");
	require_test(session.next_object_id() == 2, "default scene should reserve the next object id");
	require_test(session.active_object_id() < 0, "default scene should not activate an object");
	require_test(session.selection_mode() == quader_editor_core::SelectionMode::object, "default scene should use object selection mode");
	require_test(session.dirty(), "default scene reset should mark the runtime content dirty");
	require_test(session.state().content_revision == 1, "default scene reset should restore content revision baseline");
	require_test(session.state().selection_revision == 0, "default scene reset should restore selection revision baseline");
	require_test(!session.history_status().can_undo && !session.history_status().can_redo, "default scene reset should clear undo and redo history");
}

void mark_clean_tracks_content_changes_not_selection_only_changes()
{
	quader_editor_core::EditorSession session;
	session.reset_to_default_scene();
	session.mark_clean();
	require_test(!session.dirty(), "mark_clean should clear runtime dirty state");

	const std::uint64_t selection_revision = session.state().selection_revision;
	session.set_selected_object_ids({ 1 }, 1);
	require_test(!session.dirty(), "selection-only changes should not dirty saved content");
	require_test(session.state().selection_revision > selection_revision, "selection-only changes should still advance selection revision");

	require_test(session.create_box_from_bounds({ 4.0F, 0.0F, 0.0F }, { 5.0F, 1.0F, 1.0F }).changed,
		"content edit should create a second object");
	require_test(session.dirty(), "content edit should dirty runtime content");
	session.mark_clean();
	require_test(!session.dirty(), "mark_clean should clear dirty state after content edit");
}

void history_restore_bumps_revisions_and_preserves_redo()
{
	quader_editor_core::EditorSession session;
	const quader_editor_core::CommandResult applied = session.apply_history_command([](quader_editor_core::EditorSession& editor_session) {
		return editor_session.create_box_from_bounds({ -1.0F, -1.0F, -1.0F }, { 1.0F, 1.0F, 1.0F });
	});
	require_test(applied.ok && applied.changed, "history fixture should apply a tracked content edit");
	require_test(session.history_status().can_undo && session.history_status().undo_count == 1, "tracked content edit should create undo history");
	const std::uint64_t created_content_revision = session.state().content_revision;
	const std::uint64_t created_selection_revision = session.state().selection_revision;

	const quader_editor_core::HistoryRestoreResult undo = session.undo();
	require_test(undo.applied && undo.status.can_redo, "undo should apply and expose redo");
	require_test(session.objects().empty(), "undo should restore the pre-create empty runtime");
	require_test(session.state().content_revision > created_content_revision, "undo restore should advance content revision");
	require_test(session.state().selection_revision > created_selection_revision, "undo restore should advance selection revision");

	const std::uint64_t undone_content_revision = session.state().content_revision;
	const std::uint64_t undone_selection_revision = session.state().selection_revision;
	const quader_editor_core::HistoryRestoreResult redo = session.redo();
	require_test(redo.applied && redo.status.can_undo, "redo should apply and expose undo");
	require_test(session.objects().size() == 1, "redo should restore the created object");
	require_test(session.state().content_revision > undone_content_revision, "redo restore should advance content revision");
	require_test(session.state().selection_revision > undone_selection_revision, "redo restore should advance selection revision");
}

void set_objects_normalizes_next_id_and_active_selection()
{
	quader_editor_core::PolygonObject first;
	first.id = 5;
	first.selected = true;
	first.document = quader_poly::make_cube({});

	quader_editor_core::PolygonObject second;
	second.id = 9;
	second.selected = false;
	second.document = quader_poly::make_cube({});

	quader_editor_core::EditorSession session;
	session.set_objects(std::vector<quader_editor_core::PolygonObject> { first, second });
	require_test(session.objects().size() == 2, "set_objects should store imported objects");
	require_test(session.next_object_id() == 10, "set_objects should normalize next object id after imported ids");
	require_test(session.selected_object_ids().size() == 1 && session.selected_object_ids().front() == 5,
		"set_objects should preserve imported selected object ids");
	require_test(session.active_object_id() < 0, "set_objects should not invent an active id when imported state has none");
	require_test(session.dirty(), "set_objects should dirty runtime content");
	require_test(session.state().content_revision == 1 && session.state().selection_revision == 1,
		"set_objects should advance both content and selection revisions");
}

void light_entities_track_selection_content_and_history()
{
	quader_editor_core::EditorSession session;
	session.reset_to_default_scene();
	session.mark_clean();

	const quader_editor_core::CommandResult create = session.apply_history_command([](quader_editor_core::EditorSession& editor_session) {
		return editor_session.create_light(
			quader_editor_core::LightType::spot,
			{ 0.0F, 4.0F, 0.0F },
			{ 0.0F, -1.0F, 0.0F });
	});
	require_test(create.ok && create.changed, "tracked light create should apply");
	require_test(session.lights().size() == 1, "light create should append a light entity");
	require_test(session.lights().front().id == 1, "light create should allocate the first light id");
	require_test(session.next_light_id() == 2, "light create should reserve the next light id");
	require_test(session.active_light_id() == 1 && session.selected_light_ids() == std::vector<int> { 1 },
		"light create should select and activate the new light");
	require_test(session.dirty(), "light create should dirty saved content");
	require_test(session.history_status().can_undo, "tracked light create should create undo history");

	const quader_editor_core::HistoryRestoreResult undo_create = session.undo();
	require_test(undo_create.applied && session.lights().empty(), "undo should remove a created light");
	const quader_editor_core::HistoryRestoreResult redo_create = session.redo();
	require_test(redo_create.applied && session.lights().size() == 1, "redo should restore a created light");

	session.mark_clean();
	const quader_editor_core::CommandResult edit = session.apply_history_command([](quader_editor_core::EditorSession& editor_session) {
		return editor_session.set_light_float(1, quader_editor_core::LightFloatProperty::range, 20.0F);
	});
	require_test(edit.ok && edit.changed, "tracked light property edit should apply");
	require_test(session.lights().front().range == 20.0F, "light property edit should update range");
	require_test(session.dirty(), "light property edit should dirty saved content");
	require_test(session.undo().applied && session.lights().front().range == 10.0F, "undo should restore light range");
	require_test(session.redo().applied && session.lights().front().range == 20.0F, "redo should reapply light range");

	const quader_editor_core::CommandResult delete_selected = session.apply_history_command([](quader_editor_core::EditorSession& editor_session) {
		return editor_session.delete_selected();
	});
	require_test(delete_selected.ok && delete_selected.changed, "object-mode delete should delete selected lights");
	require_test(session.lights().empty(), "selected light should be removed by delete_selected");
	require_test(session.undo().applied && session.lights().size() == 1, "undo should restore deleted lights");
}

void selecting_a_light_clears_mesh_selection()
{
	quader_editor_core::EditorSession session;
	session.reset_to_default_scene();
	require_test(session.create_light(quader_editor_core::LightType::point, { 1.0F, 2.0F, 3.0F }, { 0.0F, -1.0F, 0.0F }).changed,
		"fixture should create a light");
	session.set_selected_object_ids({ 1 }, 1);
	require_test(session.selected_object_ids() == std::vector<int> { 1 }, "fixture should select the starter mesh");

	const quader_editor_core::CommandResult select_light = session.select_light(1);
	require_test(select_light.ok && select_light.changed, "select_light should select an existing light");
	require_test(session.selected_object_ids().empty(), "select_light should clear mesh object selection");
	require_test(session.selected_light_ids() == std::vector<int> { 1 }, "select_light should select the target light");
}

void selected_lights_supply_transform_gizmo_center_and_bounds()
{
	quader_editor_core::EditorSession session;
	require_test(session.create_light(
		quader_editor_core::LightType::point,
		{ 3.0F, 4.0F, 5.0F },
		{ 0.0F, -1.0F, 0.0F }).changed,
		"fixture should create a selected light");

	const std::optional<quader::QVec3> center = session.selection_center();
	require_test(center.has_value(), "selected light should provide a transform center");
	require_test(vec_near(*center, { 3.0F, 4.0F, 5.0F }), "selected light center should use the light position");

	const quader_editor_core::Bounds3 selection_bounds = session.selection_bounds();
	require_test(!selection_bounds.empty, "selected light should provide selection bounds");
	require_test(vec_near(selection_bounds.min, { 3.0F, 4.0F, 5.0F }), "selected light bounds min should use the light position");
	require_test(vec_near(selection_bounds.max, { 3.0F, 4.0F, 5.0F }), "selected light bounds max should use the light position");

	const quader_editor_core::Bounds3 document_bounds = session.document_bounds();
	require_test(!document_bounds.empty, "document bounds should include light entities");
	require_test(vec_near(document_bounds.min, { 3.0F, 4.0F, 5.0F }), "light-only document bounds min should use the light position");
	require_test(vec_near(document_bounds.max, { 3.0F, 4.0F, 5.0F }), "light-only document bounds max should use the light position");
}

void selected_lights_move_and_rotate_with_transform_operations()
{
	quader_editor_core::EditorSession session;
	require_test(session.create_light(
		quader_editor_core::LightType::spot,
		{ 1.0F, 0.0F, 0.0F },
		{ 0.0F, -1.0F, 0.0F }).changed,
		"fixture should create a selected spot light");

	require_test(session.translate_selected({ 2.0F, 3.0F, 4.0F }).changed, "translate_selected should move selected lights");
	require_test(vec_near(session.lights().front().position, { 3.0F, 3.0F, 4.0F }), "selected light position should translate");

	quader_poly::Transform3 rotate_z;
	rotate_z.x_axis = { 0.0F, 1.0F, 0.0F };
	rotate_z.y_axis = { -1.0F, 0.0F, 0.0F };
	rotate_z.z_axis = { 0.0F, 0.0F, 1.0F };
	require_test(session.transform_selected(rotate_z, { 0.0F, 0.0F, 0.0F }).changed, "transform_selected should rotate selected lights");
	require_test(vec_near(session.lights().front().position, { -3.0F, 3.0F, 4.0F }), "selected light position should rotate around the pivot");
	require_test(vec_near(session.lights().front().direction, { 1.0F, 0.0F, 0.0F }), "spot light direction should rotate with the selected light");
}

void transform_preview_commit_and_history_apply_to_selected_lights()
{
	quader_editor_core::EditorSession session;
	const quader_editor_core::CommandResult create = session.apply_history_command([](quader_editor_core::EditorSession& editor_session) {
		return editor_session.create_light(
			quader_editor_core::LightType::point,
			{ 0.0F, 1.0F, 0.0F },
			{ 0.0F, -1.0F, 0.0F });
	});
	require_test(create.ok && create.changed, "fixture should create a tracked selected light");

	require_test(session.begin_transform_preview(true).ok, "selected light transform preview should start even when duplicate modifier is pressed");
	require_test(session.update_transform_translate_preview({ 5.0F, 0.0F, 0.0F }).changed, "light transform preview should move the selected light");
	require_test(vec_near(session.lights().front().position, { 5.0F, 1.0F, 0.0F }), "preview update should mutate the selected light position");
	const quader_editor_core::PreviewTransactionFinishResult commit =
		session.commit_preview_transaction(quader_editor_core::PreviewTransactionKind::transform);
	require_test(commit.before.active && commit.before.changed, "light transform preview commit should finish an active changed preview");
	require_test(session.undo().applied, "undo should restore committed light transform preview");
	require_test(vec_near(session.lights().front().position, { 0.0F, 1.0F, 0.0F }), "undo should restore the light position");
	require_test(session.redo().applied, "redo should reapply committed light transform preview");
	require_test(vec_near(session.lights().front().position, { 5.0F, 1.0F, 0.0F }), "redo should reapply the light position");
}

} // namespace

TEST_CASE("qdr_editor_core_runtime_lifecycle_tests", "[editor-core-runtime-lifecycle]")
{
	default_scene_reset_restores_stable_runtime_defaults();
	mark_clean_tracks_content_changes_not_selection_only_changes();
	history_restore_bumps_revisions_and_preserves_redo();
	set_objects_normalizes_next_id_and_active_selection();
	light_entities_track_selection_content_and_history();
	selecting_a_light_clears_mesh_selection();
	selected_lights_supply_transform_gizmo_center_and_bounds();
	selected_lights_move_and_rotate_with_transform_operations();
	transform_preview_commit_and_history_apply_to_selected_lights();
}

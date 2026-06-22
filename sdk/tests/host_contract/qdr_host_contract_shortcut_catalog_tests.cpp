#include <catch2/catch_test_macros.hpp>

#include <quader_host_contract/host_contract.h>

#include <set>
#include <string>
#include <string_view>

namespace qhc = quader_host_contract;

namespace {

bool same_text(std::string_view left, std::string_view right)
{
	return left == right;
}

const qhc::HostShortcutDescriptor& require_shortcut(std::string_view id)
{
	const qhc::HostShortcutDescriptor *match = nullptr;
	for (const qhc::HostShortcutDescriptor& descriptor : qhc::shortcut_catalog()) {
		if (descriptor.id == id) {
			match = &descriptor;
			break;
		}
	}
	REQUIRE(match != nullptr);
	return *match;
}

bool has_interaction_kind(std::string_view id, qhc::HostInteractionKind kind)
{
	const qhc::HostInteractionDescriptor *descriptor = qhc::find_interaction_descriptor(id);
	return descriptor != nullptr && descriptor->kind == kind;
}

std::string shortcut_stroke_key(const qhc::HostShortcutDescriptor& shortcut)
{
	std::string key;
	key += std::to_string(static_cast<int>(shortcut.context));
	key += "|";
	key += shortcut.key;
	key += "|";
	key += shortcut.primary_modifier ? "primary" : "plain";
	key += "|";
	key += shortcut.alt ? "alt" : "no_alt";
	key += "|";
	key += shortcut.shift ? "shift" : "no_shift";
	return key;
}

} // namespace

TEST_CASE("host contract selection mode catalog exposes stable host ids", "[host-contract]")
{
	const std::span<const qhc::HostSelectionModeDescriptor> modes = qhc::selection_mode_catalog();
	REQUIRE(modes.size() == 5U);

	const qhc::HostSelectionModeDescriptor *vertex = qhc::find_selection_mode_descriptor("vertex");
	REQUIRE(vertex != nullptr);
	CHECK(vertex->mode == qhc::HostSelectionMode::vertex);
	CHECK(same_text(vertex->label, "Vertex"));

	const qhc::HostSelectionModeDescriptor *edge = qhc::find_selection_mode_descriptor("edge");
	REQUIRE(edge != nullptr);
	CHECK(edge->mode == qhc::HostSelectionMode::edge);

	const qhc::HostSelectionModeDescriptor *face = qhc::find_selection_mode_descriptor("face");
	REQUIRE(face != nullptr);
	CHECK(face->mode == qhc::HostSelectionMode::face);

	const qhc::HostSelectionModeDescriptor *object = qhc::find_selection_mode_descriptor("object");
	REQUIRE(object != nullptr);
	CHECK(object->mode == qhc::HostSelectionMode::object);

	const qhc::HostSelectionModeDescriptor *group = qhc::find_selection_mode_descriptor("group");
	REQUIRE(group != nullptr);
	CHECK(group->mode == qhc::HostSelectionMode::group);
	CHECK(same_text(group->label, "Group"));
}

TEST_CASE("host contract shortcut targets resolve to descriptor-only catalogs", "[host-contract]")
{
	const std::span<const qhc::HostShortcutDescriptor> shortcuts = qhc::shortcut_catalog();
	REQUIRE(shortcuts.size() == 44U);

	for (const qhc::HostShortcutDescriptor& shortcut : shortcuts) {
		CHECK_FALSE(shortcut.id.empty());
		CHECK_FALSE(shortcut.key.empty());
		CHECK_FALSE(shortcut.target_id.empty());
		CHECK_FALSE(shortcut.alt);
		CHECK(shortcut.registration_policy == qhc::HostShortcutRegistrationPolicy::quader_local);

		switch (shortcut.target_kind) {
			case qhc::HostShortcutTargetKind::action:
				CHECK(qhc::find_action_descriptor(shortcut.target_id) != nullptr);
				break;
			case qhc::HostShortcutTargetKind::tool:
				CHECK(qhc::find_tool_descriptor(shortcut.target_id) != nullptr);
				break;
			case qhc::HostShortcutTargetKind::selection_mode:
				CHECK(qhc::find_selection_mode_descriptor(shortcut.target_id) != nullptr);
				break;
			case qhc::HostShortcutTargetKind::editor_action:
				CHECK(has_interaction_kind(shortcut.target_id, qhc::HostInteractionKind::editor_action));
				break;
			case qhc::HostShortcutTargetKind::viewport_action:
				CHECK(has_interaction_kind(shortcut.target_id, qhc::HostInteractionKind::viewport_action));
				break;
		}
	}
}

TEST_CASE("host contract shortcut catalog includes active material assignment", "[host-contract]")
{
	const qhc::HostShortcutDescriptor& apply_active_material = require_shortcut("apply_active_material");
	CHECK(apply_active_material.context == qhc::HostShortcutContext::viewport);
	CHECK(same_text(apply_active_material.key, "T"));
	CHECK_FALSE(apply_active_material.primary_modifier);
	CHECK(apply_active_material.shift);
	CHECK(apply_active_material.target_kind == qhc::HostShortcutTargetKind::action);
	CHECK(same_text(apply_active_material.target_id, "apply_active_material"));
	CHECK(apply_active_material.registration_policy == qhc::HostShortcutRegistrationPolicy::quader_local);
	CHECK(qhc::find_action_descriptor("apply_active_material") != nullptr);
}

TEST_CASE("host contract shortcut catalog has no duplicate default strokes", "[host-contract]")
{
	std::set<std::string> strokes;
	for (const qhc::HostShortcutDescriptor& shortcut : qhc::shortcut_catalog()) {
		const std::string key = shortcut_stroke_key(shortcut);
		CHECK(strokes.insert(key).second);
	}
}

TEST_CASE("host contract shortcut catalog preserves editor and viewport key meanings", "[host-contract]")
{
	const qhc::HostShortcutDescriptor& undo = require_shortcut("undo");
	CHECK(undo.context == qhc::HostShortcutContext::global);
	CHECK(same_text(undo.key, "Z"));
	CHECK(undo.primary_modifier);
	CHECK_FALSE(undo.shift);
	CHECK(undo.target_kind == qhc::HostShortcutTargetKind::editor_action);
	CHECK(same_text(undo.target_id, "undo"));

	const qhc::HostShortcutDescriptor& redo_shift_z = require_shortcut("redo_shift_z");
	CHECK(same_text(redo_shift_z.key, "Z"));
	CHECK(redo_shift_z.primary_modifier);
	CHECK(redo_shift_z.shift);
	CHECK(same_text(redo_shift_z.target_id, "redo"));

	const qhc::HostShortcutDescriptor& new_document = require_shortcut("new_document");
	CHECK(new_document.context == qhc::HostShortcutContext::global);
	CHECK(same_text(new_document.key, "N"));
	CHECK(new_document.primary_modifier);
	CHECK(new_document.registration_policy == qhc::HostShortcutRegistrationPolicy::quader_local);

	const qhc::HostShortcutDescriptor& open_document = require_shortcut("open_document");
	CHECK(same_text(open_document.key, "O"));
	CHECK(open_document.primary_modifier);
	CHECK(open_document.registration_policy == qhc::HostShortcutRegistrationPolicy::quader_local);

	const qhc::HostShortcutDescriptor& save_document = require_shortcut("save_document");
	CHECK(same_text(save_document.key, "S"));
	CHECK(save_document.primary_modifier);
	CHECK(save_document.registration_policy == qhc::HostShortcutRegistrationPolicy::quader_local);

	const qhc::HostShortcutDescriptor& delete_key = require_shortcut("delete");
	CHECK(same_text(delete_key.key, "Delete"));
	CHECK_FALSE(delete_key.primary_modifier);
	CHECK(same_text(delete_key.target_id, "delete"));

	const qhc::HostShortcutDescriptor& delete_backspace = require_shortcut("delete_backspace");
	CHECK(same_text(delete_backspace.key, "Backspace"));
	CHECK_FALSE(delete_backspace.primary_modifier);
	CHECK(same_text(delete_backspace.target_id, "delete"));

	const qhc::HostShortcutDescriptor& merge = require_shortcut("merge_vertices");
	CHECK(merge.context == qhc::HostShortcutContext::viewport);
	CHECK(same_text(merge.key, "M"));
	CHECK_FALSE(merge.shift);
	CHECK(merge.target_kind == qhc::HostShortcutTargetKind::action);
	CHECK(same_text(merge.target_id, "merge_vertices"));
	CHECK_FALSE(merge.alternate);

	const qhc::HostShortcutDescriptor& merge_center = require_shortcut("merge_vertices_at_center");
	CHECK(same_text(merge_center.key, "M"));
	CHECK(merge_center.shift);
	CHECK(same_text(merge_center.target_id, "merge_vertices_at_center"));
	CHECK(merge_center.alternate);

	const qhc::HostShortcutDescriptor& flip_mesh_normals = require_shortcut("flip_mesh_normals");
	CHECK(flip_mesh_normals.context == qhc::HostShortcutContext::viewport);
	CHECK(same_text(flip_mesh_normals.key, "F"));
	CHECK_FALSE(flip_mesh_normals.primary_modifier);
	CHECK_FALSE(flip_mesh_normals.shift);
	CHECK(flip_mesh_normals.target_kind == qhc::HostShortcutTargetKind::action);
	CHECK(same_text(flip_mesh_normals.target_id, "invert_mesh"));
}

TEST_CASE("host contract shortcut catalog keeps overloaded R shortcuts unambiguous", "[host-contract]")
{
	const qhc::HostShortcutDescriptor& repeat = require_shortcut("repeat_last_action");
	CHECK(repeat.context == qhc::HostShortcutContext::global);
	CHECK(same_text(repeat.key, "R"));
	CHECK(repeat.primary_modifier);
	CHECK_FALSE(repeat.shift);
	CHECK(repeat.target_kind == qhc::HostShortcutTargetKind::editor_action);
	CHECK(same_text(repeat.target_id, "repeat_last_action"));

	const qhc::HostShortcutDescriptor& rotate = require_shortcut("rotate_tool");
	CHECK(rotate.context == qhc::HostShortcutContext::viewport);
	CHECK(same_text(rotate.key, "R"));
	CHECK_FALSE(rotate.primary_modifier);
	CHECK_FALSE(rotate.shift);
	CHECK(rotate.target_kind == qhc::HostShortcutTargetKind::tool);
	CHECK(same_text(rotate.target_id, "rotate"));

	const qhc::HostShortcutDescriptor& insert_edge_loop = require_shortcut("insert_edge_loop_tool");
	CHECK(insert_edge_loop.context == qhc::HostShortcutContext::viewport);
	CHECK(same_text(insert_edge_loop.key, "R"));
	CHECK_FALSE(insert_edge_loop.primary_modifier);
	CHECK(insert_edge_loop.shift);
	CHECK(insert_edge_loop.target_kind == qhc::HostShortcutTargetKind::action);
	CHECK(same_text(insert_edge_loop.target_id, "insert_edge_loop_tool"));

	const qhc::HostShortcutDescriptor& cut = require_shortcut("cut_tool");
	CHECK(cut.context == qhc::HostShortcutContext::viewport);
	CHECK(same_text(cut.key, "C"));
	CHECK_FALSE(cut.primary_modifier);
	CHECK(cut.shift);
	CHECK(cut.target_kind == qhc::HostShortcutTargetKind::action);
	CHECK(same_text(cut.target_id, "cut_tool"));

	const qhc::HostShortcutDescriptor& mirror_x = require_shortcut("mirror_axis_x");
	CHECK(same_text(mirror_x.key, "X"));
	CHECK(mirror_x.shift);
	CHECK(mirror_x.target_kind == qhc::HostShortcutTargetKind::viewport_action);
	CHECK(same_text(mirror_x.target_id, "mirror_axis_x"));

	CHECK(same_text(require_shortcut("mirror_axis_y").key, "Y"));
	CHECK(same_text(require_shortcut("mirror_axis_z").key, "Z"));
	CHECK(same_text(require_shortcut("flip_horizontal").key, "H"));
	CHECK(same_text(require_shortcut("flip_vertical").key, "V"));
}

TEST_CASE("host contract shortcut catalog includes viewport selection mode keys", "[host-contract]")
{
	CHECK(same_text(require_shortcut("vertex_selection").key, "1"));
	CHECK(same_text(require_shortcut("vertex_selection").target_id, "vertex"));
	CHECK(same_text(require_shortcut("vertex_selection_keypad").key, "Keypad1"));
	CHECK(same_text(require_shortcut("vertex_selection_keypad").target_id, "vertex"));

	CHECK(same_text(require_shortcut("edge_selection").key, "2"));
	CHECK(same_text(require_shortcut("edge_selection").target_id, "edge"));
	CHECK(same_text(require_shortcut("edge_selection_keypad").key, "Keypad2"));
	CHECK(same_text(require_shortcut("edge_selection_keypad").target_id, "edge"));

	CHECK(same_text(require_shortcut("face_selection").key, "3"));
	CHECK(same_text(require_shortcut("face_selection").target_id, "face"));
	CHECK(same_text(require_shortcut("face_selection_keypad").key, "Keypad3"));
	CHECK(same_text(require_shortcut("face_selection_keypad").target_id, "face"));

	CHECK(same_text(require_shortcut("object_selection").key, "4"));
	CHECK(same_text(require_shortcut("object_selection").target_id, "object"));
	CHECK(same_text(require_shortcut("object_selection_keypad").key, "Keypad4"));
	CHECK(same_text(require_shortcut("object_selection_keypad").target_id, "object"));

	CHECK(same_text(require_shortcut("group_selection").key, "5"));
	CHECK(same_text(require_shortcut("group_selection").target_id, "group"));
	CHECK(same_text(require_shortcut("group_selection_keypad").key, "Keypad5"));
	CHECK(same_text(require_shortcut("group_selection_keypad").target_id, "group"));
}

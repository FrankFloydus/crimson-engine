////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <editor/metadata/host_contract.hpp>

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
	EXPECT_TRUE(match != nullptr);
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

TEST(QdrEditorMetadataShortcutCatalogTests, HostContractSelectionModeCatalogExposesStableHostIds) {
	const std::span<const qhc::HostSelectionModeDescriptor> modes = qhc::selection_mode_catalog();
	EXPECT_TRUE(modes.size() == 5U);

	const qhc::HostSelectionModeDescriptor *vertex = qhc::find_selection_mode_descriptor("vertex");
	EXPECT_TRUE(vertex != nullptr);
	EXPECT_TRUE(vertex->mode == qhc::HostSelectionMode::vertex);
	EXPECT_TRUE(same_text(vertex->label, "Vertex"));

	const qhc::HostSelectionModeDescriptor *edge = qhc::find_selection_mode_descriptor("edge");
	EXPECT_TRUE(edge != nullptr);
	EXPECT_TRUE(edge->mode == qhc::HostSelectionMode::edge);

	const qhc::HostSelectionModeDescriptor *face = qhc::find_selection_mode_descriptor("face");
	EXPECT_TRUE(face != nullptr);
	EXPECT_TRUE(face->mode == qhc::HostSelectionMode::face);

	const qhc::HostSelectionModeDescriptor *object = qhc::find_selection_mode_descriptor("object");
	EXPECT_TRUE(object != nullptr);
	EXPECT_TRUE(object->mode == qhc::HostSelectionMode::object);

	const qhc::HostSelectionModeDescriptor *group = qhc::find_selection_mode_descriptor("group");
	EXPECT_TRUE(group != nullptr);
	EXPECT_TRUE(group->mode == qhc::HostSelectionMode::group);
	EXPECT_TRUE(same_text(group->label, "Group"));
}

TEST(QdrEditorMetadataShortcutCatalogTests, HostContractShortcutTargetsResolveToDescriptorOnlyCatalogs) {
	const std::span<const qhc::HostShortcutDescriptor> shortcuts = qhc::shortcut_catalog();
	EXPECT_TRUE(shortcuts.size() == 44U);

	for (const qhc::HostShortcutDescriptor& shortcut : shortcuts) {
		EXPECT_FALSE(shortcut.id.empty());
		EXPECT_FALSE(shortcut.key.empty());
		EXPECT_FALSE(shortcut.target_id.empty());
		EXPECT_FALSE(shortcut.alt);
		EXPECT_TRUE(shortcut.registration_policy == qhc::HostShortcutRegistrationPolicy::quader_local);

		switch (shortcut.target_kind) {
			case qhc::HostShortcutTargetKind::action:
				EXPECT_TRUE(qhc::find_action_descriptor(shortcut.target_id) != nullptr);
				break;
			case qhc::HostShortcutTargetKind::tool:
				EXPECT_TRUE(qhc::find_tool_descriptor(shortcut.target_id) != nullptr);
				break;
			case qhc::HostShortcutTargetKind::selection_mode:
				EXPECT_TRUE(qhc::find_selection_mode_descriptor(shortcut.target_id) != nullptr);
				break;
			case qhc::HostShortcutTargetKind::editor_action:
				EXPECT_TRUE(has_interaction_kind(shortcut.target_id, qhc::HostInteractionKind::editor_action));
				break;
			case qhc::HostShortcutTargetKind::viewport_action:
				EXPECT_TRUE(has_interaction_kind(shortcut.target_id, qhc::HostInteractionKind::viewport_action));
				break;
		}
	}
}

TEST(QdrEditorMetadataShortcutCatalogTests, HostContractShortcutCatalogIncludesActiveMaterialAssignment) {
	const qhc::HostShortcutDescriptor& apply_active_material = require_shortcut("apply_active_material");
	EXPECT_TRUE(apply_active_material.context == qhc::HostShortcutContext::viewport);
	EXPECT_TRUE(same_text(apply_active_material.key, "T"));
	EXPECT_FALSE(apply_active_material.primary_modifier);
	EXPECT_TRUE(apply_active_material.shift);
	EXPECT_TRUE(apply_active_material.target_kind == qhc::HostShortcutTargetKind::action);
	EXPECT_TRUE(same_text(apply_active_material.target_id, "apply_active_material"));
	EXPECT_TRUE(apply_active_material.registration_policy == qhc::HostShortcutRegistrationPolicy::quader_local);
	EXPECT_TRUE(qhc::find_action_descriptor("apply_active_material") != nullptr);
}

TEST(QdrEditorMetadataShortcutCatalogTests, HostContractShortcutCatalogHasNoDuplicateDefaultStrokes) {
	std::set<std::string> strokes;
	for (const qhc::HostShortcutDescriptor& shortcut : qhc::shortcut_catalog()) {
		const std::string key = shortcut_stroke_key(shortcut);
		EXPECT_TRUE(strokes.insert(key).second);
	}
}

TEST(QdrEditorMetadataShortcutCatalogTests, HostContractShortcutCatalogPreservesEditorAndViewportKeyMeanings) {
	const qhc::HostShortcutDescriptor& undo = require_shortcut("undo");
	EXPECT_TRUE(undo.context == qhc::HostShortcutContext::global);
	EXPECT_TRUE(same_text(undo.key, "Z"));
	EXPECT_TRUE(undo.primary_modifier);
	EXPECT_FALSE(undo.shift);
	EXPECT_TRUE(undo.target_kind == qhc::HostShortcutTargetKind::editor_action);
	EXPECT_TRUE(same_text(undo.target_id, "undo"));

	const qhc::HostShortcutDescriptor& redo_shift_z = require_shortcut("redo_shift_z");
	EXPECT_TRUE(same_text(redo_shift_z.key, "Z"));
	EXPECT_TRUE(redo_shift_z.primary_modifier);
	EXPECT_TRUE(redo_shift_z.shift);
	EXPECT_TRUE(same_text(redo_shift_z.target_id, "redo"));

	const qhc::HostShortcutDescriptor& new_document = require_shortcut("new_document");
	EXPECT_TRUE(new_document.context == qhc::HostShortcutContext::global);
	EXPECT_TRUE(same_text(new_document.key, "N"));
	EXPECT_TRUE(new_document.primary_modifier);
	EXPECT_TRUE(new_document.registration_policy == qhc::HostShortcutRegistrationPolicy::quader_local);

	const qhc::HostShortcutDescriptor& open_document = require_shortcut("open_document");
	EXPECT_TRUE(same_text(open_document.key, "O"));
	EXPECT_TRUE(open_document.primary_modifier);
	EXPECT_TRUE(open_document.registration_policy == qhc::HostShortcutRegistrationPolicy::quader_local);

	const qhc::HostShortcutDescriptor& save_document = require_shortcut("save_document");
	EXPECT_TRUE(same_text(save_document.key, "S"));
	EXPECT_TRUE(save_document.primary_modifier);
	EXPECT_TRUE(save_document.registration_policy == qhc::HostShortcutRegistrationPolicy::quader_local);

	const qhc::HostShortcutDescriptor& delete_key = require_shortcut("delete");
	EXPECT_TRUE(same_text(delete_key.key, "Delete"));
	EXPECT_FALSE(delete_key.primary_modifier);
	EXPECT_TRUE(same_text(delete_key.target_id, "delete"));

	const qhc::HostShortcutDescriptor& delete_backspace = require_shortcut("delete_backspace");
	EXPECT_TRUE(same_text(delete_backspace.key, "Backspace"));
	EXPECT_FALSE(delete_backspace.primary_modifier);
	EXPECT_TRUE(same_text(delete_backspace.target_id, "delete"));

	const qhc::HostShortcutDescriptor& merge = require_shortcut("merge_vertices");
	EXPECT_TRUE(merge.context == qhc::HostShortcutContext::viewport);
	EXPECT_TRUE(same_text(merge.key, "M"));
	EXPECT_FALSE(merge.shift);
	EXPECT_TRUE(merge.target_kind == qhc::HostShortcutTargetKind::action);
	EXPECT_TRUE(same_text(merge.target_id, "merge_vertices"));
	EXPECT_FALSE(merge.alternate);

	const qhc::HostShortcutDescriptor& merge_center = require_shortcut("merge_vertices_at_center");
	EXPECT_TRUE(same_text(merge_center.key, "M"));
	EXPECT_TRUE(merge_center.shift);
	EXPECT_TRUE(same_text(merge_center.target_id, "merge_vertices_at_center"));
	EXPECT_TRUE(merge_center.alternate);

	const qhc::HostShortcutDescriptor& flip_mesh_normals = require_shortcut("flip_mesh_normals");
	EXPECT_TRUE(flip_mesh_normals.context == qhc::HostShortcutContext::viewport);
	EXPECT_TRUE(same_text(flip_mesh_normals.key, "F"));
	EXPECT_FALSE(flip_mesh_normals.primary_modifier);
	EXPECT_FALSE(flip_mesh_normals.shift);
	EXPECT_TRUE(flip_mesh_normals.target_kind == qhc::HostShortcutTargetKind::action);
	EXPECT_TRUE(same_text(flip_mesh_normals.target_id, "invert_mesh"));
}

TEST(QdrEditorMetadataShortcutCatalogTests, HostContractShortcutCatalogKeepsOverloadedRShortcutsUnambiguous) {
	const qhc::HostShortcutDescriptor& repeat = require_shortcut("repeat_last_action");
	EXPECT_TRUE(repeat.context == qhc::HostShortcutContext::global);
	EXPECT_TRUE(same_text(repeat.key, "R"));
	EXPECT_TRUE(repeat.primary_modifier);
	EXPECT_FALSE(repeat.shift);
	EXPECT_TRUE(repeat.target_kind == qhc::HostShortcutTargetKind::editor_action);
	EXPECT_TRUE(same_text(repeat.target_id, "repeat_last_action"));

	const qhc::HostShortcutDescriptor& rotate = require_shortcut("rotate_tool");
	EXPECT_TRUE(rotate.context == qhc::HostShortcutContext::viewport);
	EXPECT_TRUE(same_text(rotate.key, "R"));
	EXPECT_FALSE(rotate.primary_modifier);
	EXPECT_FALSE(rotate.shift);
	EXPECT_TRUE(rotate.target_kind == qhc::HostShortcutTargetKind::tool);
	EXPECT_TRUE(same_text(rotate.target_id, "rotate"));

	const qhc::HostShortcutDescriptor& insert_edge_loop = require_shortcut("insert_edge_loop_tool");
	EXPECT_TRUE(insert_edge_loop.context == qhc::HostShortcutContext::viewport);
	EXPECT_TRUE(same_text(insert_edge_loop.key, "R"));
	EXPECT_FALSE(insert_edge_loop.primary_modifier);
	EXPECT_TRUE(insert_edge_loop.shift);
	EXPECT_TRUE(insert_edge_loop.target_kind == qhc::HostShortcutTargetKind::action);
	EXPECT_TRUE(same_text(insert_edge_loop.target_id, "insert_edge_loop_tool"));

	const qhc::HostShortcutDescriptor& cut = require_shortcut("cut_tool");
	EXPECT_TRUE(cut.context == qhc::HostShortcutContext::viewport);
	EXPECT_TRUE(same_text(cut.key, "C"));
	EXPECT_FALSE(cut.primary_modifier);
	EXPECT_TRUE(cut.shift);
	EXPECT_TRUE(cut.target_kind == qhc::HostShortcutTargetKind::action);
	EXPECT_TRUE(same_text(cut.target_id, "cut_tool"));

	const qhc::HostShortcutDescriptor& mirror_x = require_shortcut("mirror_axis_x");
	EXPECT_TRUE(same_text(mirror_x.key, "X"));
	EXPECT_TRUE(mirror_x.shift);
	EXPECT_TRUE(mirror_x.target_kind == qhc::HostShortcutTargetKind::viewport_action);
	EXPECT_TRUE(same_text(mirror_x.target_id, "mirror_axis_x"));

	EXPECT_TRUE(same_text(require_shortcut("mirror_axis_y").key, "Y"));
	EXPECT_TRUE(same_text(require_shortcut("mirror_axis_z").key, "Z"));
	EXPECT_TRUE(same_text(require_shortcut("flip_horizontal").key, "H"));
	EXPECT_TRUE(same_text(require_shortcut("flip_vertical").key, "V"));
}

TEST(QdrEditorMetadataShortcutCatalogTests, HostContractShortcutCatalogIncludesViewportSelectionModeKeys) {
	EXPECT_TRUE(same_text(require_shortcut("vertex_selection").key, "1"));
	EXPECT_TRUE(same_text(require_shortcut("vertex_selection").target_id, "vertex"));
	EXPECT_TRUE(same_text(require_shortcut("vertex_selection_keypad").key, "Keypad1"));
	EXPECT_TRUE(same_text(require_shortcut("vertex_selection_keypad").target_id, "vertex"));

	EXPECT_TRUE(same_text(require_shortcut("edge_selection").key, "2"));
	EXPECT_TRUE(same_text(require_shortcut("edge_selection").target_id, "edge"));
	EXPECT_TRUE(same_text(require_shortcut("edge_selection_keypad").key, "Keypad2"));
	EXPECT_TRUE(same_text(require_shortcut("edge_selection_keypad").target_id, "edge"));

	EXPECT_TRUE(same_text(require_shortcut("face_selection").key, "3"));
	EXPECT_TRUE(same_text(require_shortcut("face_selection").target_id, "face"));
	EXPECT_TRUE(same_text(require_shortcut("face_selection_keypad").key, "Keypad3"));
	EXPECT_TRUE(same_text(require_shortcut("face_selection_keypad").target_id, "face"));

	EXPECT_TRUE(same_text(require_shortcut("object_selection").key, "4"));
	EXPECT_TRUE(same_text(require_shortcut("object_selection").target_id, "object"));
	EXPECT_TRUE(same_text(require_shortcut("object_selection_keypad").key, "Keypad4"));
	EXPECT_TRUE(same_text(require_shortcut("object_selection_keypad").target_id, "object"));

	EXPECT_TRUE(same_text(require_shortcut("group_selection").key, "5"));
	EXPECT_TRUE(same_text(require_shortcut("group_selection").target_id, "group"));
	EXPECT_TRUE(same_text(require_shortcut("group_selection_keypad").key, "Keypad5"));
	EXPECT_TRUE(same_text(require_shortcut("group_selection_keypad").target_id, "group"));
}

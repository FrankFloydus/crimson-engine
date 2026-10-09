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

bool has_shortcut(std::string_view id)
{
	for (const qhc::HostShortcutDescriptor& descriptor : qhc::shortcut_catalog()) {
		if (descriptor.id == id) {
			return true;
		}
	}
	return false;
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
        EXPECT_TRUE(vertex->mode == qhc::HostSelectionMode::Vertex);
        EXPECT_TRUE(same_text(vertex->label, "Vertex"));

	const qhc::HostSelectionModeDescriptor *edge = qhc::find_selection_mode_descriptor("edge");
	EXPECT_TRUE(edge != nullptr);
        EXPECT_TRUE(edge->mode == qhc::HostSelectionMode::Edge);

        const qhc::HostSelectionModeDescriptor *face = qhc::find_selection_mode_descriptor("face");
	EXPECT_TRUE(face != nullptr);
        EXPECT_TRUE(face->mode == qhc::HostSelectionMode::Face);

        const qhc::HostSelectionModeDescriptor *object = qhc::find_selection_mode_descriptor("object");
	EXPECT_TRUE(object != nullptr);
        EXPECT_TRUE(object->mode == qhc::HostSelectionMode::Object);

        const qhc::HostSelectionModeDescriptor *group = qhc::find_selection_mode_descriptor("group");
	EXPECT_TRUE(group != nullptr);
        EXPECT_TRUE(group->mode == qhc::HostSelectionMode::Group);
        EXPECT_TRUE(same_text(group->label, "Group"));
}

TEST(QdrEditorMetadataShortcutCatalogTests, HostContractShortcutTargetsResolveToDescriptorOnlyCatalogs) {
	const std::span<const qhc::HostShortcutDescriptor> shortcuts = qhc::shortcut_catalog();
	EXPECT_EQ(shortcuts.size(), 38U);
	EXPECT_FALSE(has_shortcut("select_tool"));

	for (const qhc::HostShortcutDescriptor& shortcut : shortcuts) {
		EXPECT_FALSE(shortcut.id.empty());
		EXPECT_FALSE(shortcut.key.empty());
		EXPECT_FALSE(shortcut.target_id.empty());
		EXPECT_EQ(shortcut.alt, shortcut.id == "save_scene_as");
                EXPECT_TRUE(shortcut.registration_policy ==
                            qhc::HostShortcutRegistrationPolicy::QuaderLocal);

                switch (shortcut.target_kind) {
                case qhc::HostShortcutTargetKind::Action:
                  EXPECT_TRUE(qhc::find_action_descriptor(shortcut.target_id) !=
                              nullptr);
                  break;
                case qhc::HostShortcutTargetKind::Tool:
                  EXPECT_TRUE(qhc::find_tool_descriptor(shortcut.target_id) !=
                              nullptr);
                  break;
                case qhc::HostShortcutTargetKind::SelectionMode:
                  EXPECT_TRUE(qhc::find_selection_mode_descriptor(
                                  shortcut.target_id) != nullptr);
                  break;
                case qhc::HostShortcutTargetKind::EditorAction:
                  EXPECT_TRUE(has_interaction_kind(
                      shortcut.target_id,
                      qhc::HostInteractionKind::EditorAction));
                  break;
                case qhc::HostShortcutTargetKind::ViewportAction:
                  EXPECT_TRUE(has_interaction_kind(
                      shortcut.target_id,
                      qhc::HostInteractionKind::ViewportAction));
                  break;
		}
	}
}

TEST(QdrEditorMetadataShortcutCatalogTests,
     HostContractShortcutCatalogRetiresImplicitMaterialAssignment) {
  for (const auto &shortcut : qhc::shortcut_catalog()) {
    EXPECT_FALSE(same_text(shortcut.id, "apply_active_material"));
    EXPECT_FALSE(same_text(shortcut.target_id, "apply_active_material"));
  }
  EXPECT_EQ(qhc::find_action_descriptor("apply_active_material"), nullptr);
}

TEST(QdrEditorMetadataShortcutCatalogTests, HostContractShortcutCatalogHasNoDuplicateDefaultStrokes) {
	std::set<std::string> strokes;
	for (const qhc::HostShortcutDescriptor& shortcut : qhc::shortcut_catalog()) {
		const std::string key = shortcut_stroke_key(shortcut);
		EXPECT_TRUE(strokes.insert(key).second);
	}
}

TEST(QdrEditorMetadataShortcutCatalogTests, HostContractShortcutCatalogPreservesEditorAndViewportKeyMeanings) {
  const auto &paint = require_shortcut("paint_select_tool");
  EXPECT_EQ(paint.context, qhc::HostShortcutContext::Viewport);
  EXPECT_EQ(paint.key, "Q");
  EXPECT_TRUE(paint.primary_modifier);
  EXPECT_FALSE(paint.alt);
  EXPECT_FALSE(paint.shift);
  EXPECT_EQ(paint.target_kind, qhc::HostShortcutTargetKind::Tool);
  EXPECT_EQ(paint.target_id, "paint_select");
	const qhc::HostShortcutDescriptor& undo = require_shortcut("undo");
        EXPECT_TRUE(undo.context == qhc::HostShortcutContext::Global);
        EXPECT_TRUE(same_text(undo.key, "Z"));
	EXPECT_TRUE(undo.primary_modifier);
	EXPECT_FALSE(undo.shift);
        EXPECT_TRUE(undo.target_kind ==
                    qhc::HostShortcutTargetKind::EditorAction);
        EXPECT_TRUE(same_text(undo.target_id, "undo"));

	const qhc::HostShortcutDescriptor& redo_shift_z = require_shortcut("redo_shift_z");
	EXPECT_TRUE(same_text(redo_shift_z.key, "Z"));
	EXPECT_TRUE(redo_shift_z.primary_modifier);
	EXPECT_TRUE(redo_shift_z.shift);
	EXPECT_TRUE(same_text(redo_shift_z.target_id, "redo"));
	EXPECT_FALSE(has_shortcut("redo_y"));

	const qhc::HostShortcutDescriptor& new_scene = require_shortcut("new_scene");
        EXPECT_TRUE(new_scene.context == qhc::HostShortcutContext::Global);
        EXPECT_TRUE(same_text(new_scene.key, "N"));
	EXPECT_TRUE(new_scene.primary_modifier);
        EXPECT_TRUE(new_scene.registration_policy ==
                    qhc::HostShortcutRegistrationPolicy::QuaderLocal);

        const qhc::HostShortcutDescriptor& open_scene = require_shortcut("open_scene");
	EXPECT_TRUE(same_text(open_scene.key, "O"));
	EXPECT_TRUE(open_scene.primary_modifier);
        EXPECT_TRUE(open_scene.registration_policy ==
                    qhc::HostShortcutRegistrationPolicy::QuaderLocal);

        const qhc::HostShortcutDescriptor& save_scene = require_shortcut("save_scene");
	EXPECT_TRUE(same_text(save_scene.key, "S"));
	EXPECT_TRUE(save_scene.primary_modifier);
	EXPECT_FALSE(save_scene.alt);
	EXPECT_FALSE(save_scene.shift);
        EXPECT_TRUE(save_scene.registration_policy ==
                    qhc::HostShortcutRegistrationPolicy::QuaderLocal);

        const qhc::HostShortcutDescriptor& save_scene_as = require_shortcut("save_scene_as");
	EXPECT_TRUE(same_text(save_scene_as.key, "S"));
	EXPECT_TRUE(save_scene_as.primary_modifier);
	EXPECT_TRUE(save_scene_as.alt);
	EXPECT_FALSE(save_scene_as.shift);
	EXPECT_TRUE(same_text(save_scene_as.target_id, "save_scene_as"));
        EXPECT_TRUE(save_scene_as.registration_policy ==
                    qhc::HostShortcutRegistrationPolicy::QuaderLocal);

        const qhc::HostShortcutDescriptor& save_all_scenes = require_shortcut("save_all_scenes");
	EXPECT_TRUE(same_text(save_all_scenes.key, "S"));
	EXPECT_TRUE(save_all_scenes.primary_modifier);
	EXPECT_FALSE(save_all_scenes.alt);
	EXPECT_TRUE(save_all_scenes.shift);
	EXPECT_TRUE(same_text(save_all_scenes.target_id, "save_all_scenes"));

        const qhc::HostShortcutDescriptor& close_scene = require_shortcut("close_scene");
	EXPECT_TRUE(same_text(close_scene.key, "W"));
	EXPECT_TRUE(close_scene.primary_modifier);
	EXPECT_FALSE(close_scene.shift);

        const qhc::HostShortcutDescriptor& next_scene = require_shortcut("next_scene");
	EXPECT_TRUE(same_text(next_scene.key, "Tab"));
	EXPECT_TRUE(next_scene.primary_modifier);
	EXPECT_FALSE(next_scene.shift);

        const qhc::HostShortcutDescriptor& previous_scene = require_shortcut("previous_scene");
	EXPECT_TRUE(same_text(previous_scene.key, "Tab"));
	EXPECT_TRUE(previous_scene.primary_modifier);
	EXPECT_TRUE(previous_scene.shift);

        const qhc::HostShortcutDescriptor& delete_key = require_shortcut("delete");
	EXPECT_TRUE(same_text(delete_key.key, "Delete"));
	EXPECT_FALSE(delete_key.primary_modifier);
	EXPECT_TRUE(same_text(delete_key.target_id, "delete"));

	EXPECT_FALSE(has_shortcut("delete_backspace"));

	const qhc::HostShortcutDescriptor& merge = require_shortcut("merge_vertices");
        EXPECT_TRUE(merge.context == qhc::HostShortcutContext::Viewport);
        EXPECT_TRUE(same_text(merge.key, "M"));
	EXPECT_FALSE(merge.shift);
        EXPECT_TRUE(merge.target_kind == qhc::HostShortcutTargetKind::Action);
        EXPECT_TRUE(same_text(merge.target_id, "merge_vertices"));
	EXPECT_FALSE(merge.alternate);

	const qhc::HostShortcutDescriptor& merge_center = require_shortcut("merge_vertices_at_center");
	EXPECT_TRUE(same_text(merge_center.key, "M"));
	EXPECT_TRUE(merge_center.shift);
	EXPECT_TRUE(same_text(merge_center.target_id, "merge_vertices_at_center"));
	EXPECT_TRUE(merge_center.alternate);

	const qhc::HostShortcutDescriptor& flip_mesh_normals = require_shortcut("flip_mesh_normals");
        EXPECT_TRUE(flip_mesh_normals.context ==
                    qhc::HostShortcutContext::Viewport);
        EXPECT_TRUE(same_text(flip_mesh_normals.key, "F"));
	EXPECT_FALSE(flip_mesh_normals.primary_modifier);
	EXPECT_TRUE(flip_mesh_normals.shift);
        EXPECT_TRUE(flip_mesh_normals.target_kind ==
                    qhc::HostShortcutTargetKind::Action);
        EXPECT_TRUE(same_text(flip_mesh_normals.target_id, "invert_mesh"));
}

TEST(QdrEditorMetadataShortcutCatalogTests, HostContractShortcutCatalogKeepsOverloadedRShortcutsUnambiguous) {
	const qhc::HostShortcutDescriptor& repeat = require_shortcut("repeat_last_action");
        EXPECT_TRUE(repeat.context == qhc::HostShortcutContext::Global);
        EXPECT_TRUE(same_text(repeat.key, "R"));
	EXPECT_TRUE(repeat.primary_modifier);
	EXPECT_FALSE(repeat.shift);
        EXPECT_TRUE(repeat.target_kind ==
                    qhc::HostShortcutTargetKind::EditorAction);
        EXPECT_TRUE(same_text(repeat.target_id, "repeat_last_action"));

	const qhc::HostShortcutDescriptor& rotate = require_shortcut("rotate_tool");
        EXPECT_TRUE(rotate.context == qhc::HostShortcutContext::Viewport);
        EXPECT_TRUE(same_text(rotate.key, "R"));
	EXPECT_FALSE(rotate.primary_modifier);
	EXPECT_FALSE(rotate.shift);
        EXPECT_TRUE(rotate.target_kind == qhc::HostShortcutTargetKind::Tool);
        EXPECT_TRUE(same_text(rotate.target_id, "rotate"));

	const qhc::HostShortcutDescriptor& insert_edge_loop = require_shortcut("insert_edge_loop_tool");
        EXPECT_TRUE(insert_edge_loop.context ==
                    qhc::HostShortcutContext::Viewport);
        EXPECT_TRUE(same_text(insert_edge_loop.key, "R"));
	EXPECT_FALSE(insert_edge_loop.primary_modifier);
	EXPECT_TRUE(insert_edge_loop.shift);
        EXPECT_TRUE(insert_edge_loop.target_kind ==
                    qhc::HostShortcutTargetKind::Action);
        EXPECT_TRUE(same_text(insert_edge_loop.target_id, "insert_edge_loop_tool"));

	// Shape commands own the defaults; generic native tool activation is unbound.
	EXPECT_FALSE(has_shortcut("create_primitive_tool"));

	EXPECT_FALSE(has_shortcut("cut_tool"));

	const qhc::HostShortcutDescriptor& mirror_x = require_shortcut("mirror_axis_x");
	EXPECT_TRUE(same_text(mirror_x.key, "X"));
	EXPECT_TRUE(mirror_x.shift);
        EXPECT_TRUE(mirror_x.target_kind ==
                    qhc::HostShortcutTargetKind::ViewportAction);
        EXPECT_TRUE(same_text(mirror_x.target_id, "mirror_axis_x"));

	EXPECT_TRUE(same_text(require_shortcut("mirror_axis_y").key, "Y"));
	EXPECT_TRUE(same_text(require_shortcut("mirror_axis_z").key, "Z"));
	const qhc::HostShortcutDescriptor& focus_selection =
		require_shortcut("focus_selection");
	EXPECT_TRUE(same_text(focus_selection.key, "F"));
	EXPECT_FALSE(focus_selection.shift);
	EXPECT_TRUE(same_text(focus_selection.target_id, "focus_selection"));
	const qhc::HostShortcutDescriptor& flip_horizontal =
		require_shortcut("flip_horizontal");
	EXPECT_TRUE(same_text(flip_horizontal.key, "H"));
	EXPECT_TRUE(flip_horizontal.shift);
	EXPECT_TRUE(same_text(require_shortcut("flip_vertical").key, "V"));
}

TEST(QdrEditorMetadataShortcutCatalogTests, HostContractShortcutCatalogIncludesViewportSelectionModeKeys) {
	EXPECT_TRUE(same_text(require_shortcut("vertex_selection").key, "1"));
	EXPECT_TRUE(same_text(require_shortcut("vertex_selection").target_id, "vertex"));
	EXPECT_FALSE(has_shortcut("vertex_selection_keypad"));

	EXPECT_TRUE(same_text(require_shortcut("edge_selection").key, "2"));
	EXPECT_TRUE(same_text(require_shortcut("edge_selection").target_id, "edge"));
	EXPECT_FALSE(has_shortcut("edge_selection_keypad"));

	EXPECT_TRUE(same_text(require_shortcut("face_selection").key, "3"));
	EXPECT_TRUE(same_text(require_shortcut("face_selection").target_id, "face"));
	EXPECT_FALSE(has_shortcut("face_selection_keypad"));

	EXPECT_FALSE(has_shortcut("object_selection"));
	EXPECT_FALSE(has_shortcut("object_selection_keypad"));

	EXPECT_FALSE(has_shortcut("group_selection"));
	EXPECT_FALSE(has_shortcut("group_selection_keypad"));
}

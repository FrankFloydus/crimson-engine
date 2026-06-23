////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/metadata/host_contract.h>

#include <editor/modeling/operations/operation_registry.h>
#include <editor/modeling/operations/operation_schema.h>
#include <editor/modeling/runtime/session.h>
#include <editor/interactions/cut_tool.h>
#include <editor/interactions/tool_identity.h>
#include <editor/modeling/runtime/tool_constants.h>
#include <editor/modeling/runtime/tool_registry.h>

#include <array>
#include <vector>

namespace quader_host_contract {
namespace {

HostOptionDescriptor int_option(const quader_editor_core::IntOperationOptionSchema& schema)
{
	HostOptionDescriptor option;
	option.id = schema.id;
	option.label = schema.label;
	option.value_kind = HostOptionValueKind::integer;
	option.default_integer = schema.default_value;
	option.minimum_integer = schema.minimum;
	option.maximum_integer = schema.maximum;
	option.integer_step = schema.step;
	option.hard_limits = schema.hard_limits;
	return option;
}

HostOptionDescriptor float_option(const quader_editor_core::FloatOperationOptionSchema& schema)
{
	HostOptionDescriptor option;
	option.id = schema.id;
	option.label = schema.label;
	option.value_kind = HostOptionValueKind::floating;
	option.default_number = schema.default_value;
	option.minimum_number = schema.minimum;
	option.maximum_number = schema.maximum;
	option.number_step = schema.step;
	option.hard_limits = schema.hard_limits;
	return option;
}

HostOptionDescriptor bool_option(const quader_editor_core::BoolOperationOptionSchema& schema)
{
	HostOptionDescriptor option;
	option.id = schema.id;
	option.label = schema.label;
	option.value_kind = HostOptionValueKind::boolean;
	option.default_bool = schema.default_value;
	option.visible = schema.visible;
	option.hard_limits = false;
	return option;
}

std::span<const HostOptionDescriptor> options_for_operation(std::string_view id)
{
	static const std::array<HostOptionDescriptor, 1> bridge_options {{
		int_option(quader_editor_core::bridge_operation_schema().steps),
	}};
	static const std::array<HostOptionDescriptor, 3> edge_bevel_options {{
		float_option(quader_editor_core::edge_bevel_operation_schema().width),
		float_option(quader_editor_core::edge_bevel_operation_schema().profile),
		int_option(quader_editor_core::edge_bevel_operation_schema().segments),
	}};
	static const std::array<HostOptionDescriptor, 2> thicken_options {{
		float_option(quader_editor_core::thicken_operation_schema().thickness),
		bool_option(quader_editor_core::thicken_operation_schema().from_center),
	}};
	static const std::array<HostOptionDescriptor, 2> slice_quad_options {{
		int_option(quader_editor_core::slice_quad_operation_schema().x_slices),
		int_option(quader_editor_core::slice_quad_operation_schema().y_slices),
	}};
	static const std::array<HostOptionDescriptor, 1> merge_by_distance_options {{
		float_option(quader_editor_core::merge_by_distance_operation_schema().tolerance),
	}};

	if (id == quader_editor_core::bridge_operation_schema().operation_id) {
		return bridge_options;
	}
	if (id == quader_editor_core::edge_bevel_operation_schema().operation_id) {
		return edge_bevel_options;
	}
	if (id == quader_editor_core::thicken_operation_schema().operation_id) {
		return thicken_options;
	}
	if (id == quader_editor_core::slice_quad_operation_schema().operation_id) {
		return slice_quad_options;
	}
	if (id == quader_editor_core::merge_by_distance_operation_schema().operation_id) {
		return merge_by_distance_options;
	}
	return {};
}

std::vector<HostOperationDescriptor> build_operation_catalog()
{
	std::vector<HostOperationDescriptor> descriptors;
	const std::span<const quader_editor_core::EditorOperationRegistryEntry> entries =
		quader_editor_core::operation_registry().entries();
	descriptors.reserve(entries.size());
	for (const quader_editor_core::EditorOperationRegistryEntry& entry : entries) {
		descriptors.push_back({
			entry.id,
			entry.operation->label(),
			entry.operation->supports_preview(),
			options_for_operation(entry.id),
		});
	}
	return descriptors;
}

HostActionAvailabilityDescriptor action_availability(
		HostSelectionMode selection_mode,
		std::uint32_t minimum_selected_vertices,
		std::uint32_t minimum_selected_edges,
		HostActiveSelectionKind required_active_kind,
		bool requires_single_object,
		std::uint32_t exact_selected_edges = 0,
		std::uint32_t minimum_selected_faces = 0,
		std::uint32_t minimum_selected_objects = 0)
{
	return {
		selection_mode,
		minimum_selected_vertices,
		minimum_selected_edges,
		exact_selected_edges,
		minimum_selected_faces,
		minimum_selected_objects,
		required_active_kind,
		requires_single_object,
	};
}

HostActionDescriptor operation_action(
		std::string_view id,
		std::string_view label,
		std::string_view operation_id,
		HostActionAvailabilityDescriptor availability,
		HostPreviewKind preview_kind = HostPreviewKind::none,
		bool show_in_operations_panel = true)
{
	return {
		id,
		label,
		HostActionKind::operation,
		operation_id,
		{},
		preview_kind,
		show_in_operations_panel,
		availability,
	};
}

HostActionDescriptor tool_action(
		std::string_view id,
		std::string_view label,
		std::string_view tool_id,
		HostActionAvailabilityDescriptor availability,
		bool show_in_operations_panel = false)
{
	return {
		id,
		label,
		HostActionKind::tool,
		{},
		tool_id,
		HostPreviewKind::none,
		show_in_operations_panel,
		availability,
	};
}

std::span<const HostActionDescriptor> action_descriptors()
{
	static const std::array<HostActionDescriptor, 42> descriptors {{
		operation_action("invert_mesh", "Flip Mesh Normals", "flip_face_normals",
			action_availability(HostSelectionMode::object, 0, 0, HostActiveSelectionKind::none, false, 0, 0, 1)),
		operation_action("shade_smooth_mesh", "Shade Smooth", "shade_faces_smooth",
			action_availability(HostSelectionMode::object, 0, 0, HostActiveSelectionKind::none, false, 0, 0, 1)),
		operation_action("shade_flat_mesh", "Shade Flat", "shade_faces_flat",
			action_availability(HostSelectionMode::object, 0, 0, HostActiveSelectionKind::none, false, 0, 0, 1)),
		operation_action("combine_mesh", "Combine Mesh", "combine_meshes",
			action_availability(HostSelectionMode::object, 0, 0, HostActiveSelectionKind::none, false, 0, 0, 2)),
		operation_action("create_outer_hull", "Create Outer Hull", "thicken",
			action_availability(HostSelectionMode::object, 0, 0, HostActiveSelectionKind::none, false, 0, 0, 1), HostPreviewKind::thicken),
		operation_action("snap_vertices_to_active", "Snap to Vertex", "snap_vertices_to_active",
			action_availability(HostSelectionMode::vertex, 2, 0, HostActiveSelectionKind::vertex, false)),
		operation_action("merge_vertices", "Merge", "merge_vertices_to_active",
			action_availability(HostSelectionMode::vertex, 2, 0, HostActiveSelectionKind::vertex, false)),
		operation_action("merge_vertices_at_center", "Merge at Center", "merge_vertices_to_center",
			action_availability(HostSelectionMode::vertex, 2, 0, HostActiveSelectionKind::vertex, false)),
		operation_action("merge_by_distance", "Merge by Distance", "merge_by_distance",
			action_availability(HostSelectionMode::vertex, 2, 0, HostActiveSelectionKind::none, false), HostPreviewKind::merge_by_distance),
		operation_action("bevel_vertices", "Bevel Vertex", "bevel_vertices",
			action_availability(HostSelectionMode::vertex, 1, 0, HostActiveSelectionKind::none, false)),
		operation_action("connect_vertices", "Connect", "connect_vertices",
			action_availability(HostSelectionMode::vertex, 2, 0, HostActiveSelectionKind::none, true)),
		operation_action("dissolve_vertices", "Dissolve Vertex", "dissolve_vertices",
			action_availability(HostSelectionMode::vertex, 1, 0, HostActiveSelectionKind::none, false)),
		operation_action("radial_align_vertices", "Radial Align", "radial_align",
			action_availability(HostSelectionMode::vertex, 3, 0, HostActiveSelectionKind::none, false)),
		operation_action("snap_edges_to_active", "Snap to Edge", "snap_edges_to_active",
			action_availability(HostSelectionMode::edge, 0, 2, HostActiveSelectionKind::edge, false)),
		operation_action("connect_edges", "Connect Edges", "connect_edges",
			action_availability(HostSelectionMode::edge, 0, 2, HostActiveSelectionKind::none, true)),
		operation_action("split_edges", "Edge Split", "split_edges",
			action_availability(HostSelectionMode::edge, 0, 1, HostActiveSelectionKind::none, false)),
		operation_action("harden_edge_normals", "Hard Normal", "harden_edge_normals",
			action_availability(HostSelectionMode::edge, 0, 1, HostActiveSelectionKind::none, false)),
		operation_action("soften_edge_normals", "Soft Normal", "soften_edge_normals",
			action_availability(HostSelectionMode::edge, 0, 1, HostActiveSelectionKind::none, false)),
		tool_action("insert_edge_loop_tool", "Insert Edge Loop", "insert_edge_loop",
			action_availability(HostSelectionMode::edge, 0, 0, HostActiveSelectionKind::none, false)),
		tool_action("knife_tool", "Knife", "knife",
			action_availability(HostSelectionMode::edge, 0, 0, HostActiveSelectionKind::none, false)),
		tool_action("cut_tool", "Cut", "cut",
			action_availability(HostSelectionMode::object, 0, 0, HostActiveSelectionKind::none, false)),
		tool_action("mirror_tool", "Mirror", "mirror",
			action_availability(HostSelectionMode::object, 0, 0, HostActiveSelectionKind::none, false, 0, 0, 1)),
		tool_action("poly_tool", "Poly", "poly",
			action_availability(HostSelectionMode::object, 0, 0, HostActiveSelectionKind::none, false)),
		operation_action("apply_active_material", "Apply Active Material", "assign_material",
			action_availability(HostSelectionMode::face, 0, 0, HostActiveSelectionKind::none, false, 0, 1),
			HostPreviewKind::none,
			false),
		operation_action("bevel_edges", "Bevel Edges", "edge_bevel",
			action_availability(HostSelectionMode::edge, 0, 1, HostActiveSelectionKind::none, false), HostPreviewKind::edge_bevel),
		operation_action("bridge_edges", "Bridge", "bridge",
			action_availability(HostSelectionMode::edge, 0, 2, HostActiveSelectionKind::none, false)),
		operation_action("interpolated_bridge_edges", "Interpolated Bridge", "bridge",
			action_availability(HostSelectionMode::edge, 0, 2, HostActiveSelectionKind::none, false), HostPreviewKind::bridge),
		operation_action("dissolve_edges", "Dissolve", "dissolve_edges",
			action_availability(HostSelectionMode::edge, 0, 1, HostActiveSelectionKind::none, true)),
		operation_action("merge_edges", "Merge Edges", "merge_edges",
			action_availability(HostSelectionMode::edge, 0, 2, HostActiveSelectionKind::none, true)),
		operation_action("collapse_edges", "Collapse", "collapse_edges",
			action_availability(HostSelectionMode::edge, 0, 1, HostActiveSelectionKind::none, true)),
		operation_action("fill_hole", "Fill Hole", "fill_hole",
			action_availability(HostSelectionMode::edge, 0, 3, HostActiveSelectionKind::none, true)),
		operation_action("radial_align_edges", "Radial Align", "radial_align",
			action_availability(HostSelectionMode::edge, 0, 2, HostActiveSelectionKind::none, false)),
		operation_action("bridge_faces", "Bridge Faces", "bridge",
			action_availability(HostSelectionMode::face, 0, 0, HostActiveSelectionKind::none, false, 0, 2)),
		operation_action("interpolated_bridge_faces", "Interpolated Bridge", "bridge",
			action_availability(HostSelectionMode::face, 0, 0, HostActiveSelectionKind::none, false, 0, 2), HostPreviewKind::bridge),
		operation_action("invert_faces", "Invert Face", "flip_face_normals",
			action_availability(HostSelectionMode::face, 0, 0, HostActiveSelectionKind::none, false, 0, 1)),
		operation_action("combine_faces", "Combine Faces", "combine_faces",
			action_availability(HostSelectionMode::face, 0, 0, HostActiveSelectionKind::none, true, 0, 2)),
		operation_action("collapse_faces", "Collapse Faces", "collapse_faces",
			action_availability(HostSelectionMode::face, 0, 0, HostActiveSelectionKind::none, false, 0, 1)),
		operation_action("radial_align_faces", "Radial Align", "radial_align",
			action_availability(HostSelectionMode::face, 0, 0, HostActiveSelectionKind::none, false, 0, 1)),
		operation_action("slice_quad", "Slice Quad", "slice_quad",
			action_availability(HostSelectionMode::face, 0, 0, HostActiveSelectionKind::none, false, 0, 1), HostPreviewKind::slice_quad),
		operation_action("thicken_faces", "Thicken Faces", "thicken",
			action_availability(HostSelectionMode::face, 0, 0, HostActiveSelectionKind::none, false, 0, 1), HostPreviewKind::thicken),
		operation_action("extract_faces", "Extract Faces", "extract_faces",
			action_availability(HostSelectionMode::face, 0, 0, HostActiveSelectionKind::none, true, 0, 1)),
		operation_action("detach_faces", "Detach Faces", "detach_faces",
			action_availability(HostSelectionMode::face, 0, 0, HostActiveSelectionKind::none, false, 0, 1)),
	}};
	return descriptors;
}

std::span<const HostOperationPanelDescriptor> operation_panel_descriptors()
{
	static constexpr std::array<std::string_view, 5> object_actions {{
		"invert_mesh",
		"shade_smooth_mesh",
		"shade_flat_mesh",
		"combine_mesh",
		"create_outer_hull",
	}};
	static constexpr std::array<std::string_view, 8> vertex_actions {{
		"snap_vertices_to_active",
		"merge_vertices",
		"merge_vertices_at_center",
		"merge_by_distance",
		"bevel_vertices",
		"connect_vertices",
		"dissolve_vertices",
		"radial_align_vertices",
	}};
	static constexpr std::array<std::string_view, 13> edge_actions {{
		"snap_edges_to_active",
		"connect_edges",
		"split_edges",
		"harden_edge_normals",
		"soften_edge_normals",
		"bevel_edges",
		"bridge_edges",
		"interpolated_bridge_edges",
		"dissolve_edges",
		"merge_edges",
		"collapse_edges",
		"fill_hole",
		"radial_align_edges",
	}};
	static constexpr std::array<std::string_view, 10> face_actions {{
		"bridge_faces",
		"interpolated_bridge_faces",
		"invert_faces",
		"combine_faces",
		"collapse_faces",
		"radial_align_faces",
		"slice_quad",
		"thicken_faces",
		"extract_faces",
		"detach_faces",
	}};
	static constexpr std::array<HostOperationPanelDescriptor, 4> descriptors {{
		{ "object_operations", "Object Operations", HostSelectionMode::object, object_actions, true },
		{ "vertex_operations", "Vertex Operations", HostSelectionMode::vertex, vertex_actions, false },
		{ "edge_operations", "Edge Operations", HostSelectionMode::edge, edge_actions, false },
		{ "face_operations", "Face Operations", HostSelectionMode::face, face_actions, false },
	}};
	return descriptors;
}

std::span<const HostUiSurfaceDescriptor> ui_surface_descriptors()
{
	static constexpr std::array<HostUiSurfaceDescriptor, 4> descriptors {{
		{ "main_menu", "Main Menu", HostUiSurfaceKind::menu, false, false },
		{ "operations_top_bar", "Operations Top Bar", HostUiSurfaceKind::toolbar, false, true },
		{ "viewport_tool_toolbar", "Viewport Tool Toolbar", HostUiSurfaceKind::toolbar, true, false },
		{ "tool_settings_panel", "Tool Settings Panel", HostUiSurfaceKind::panel, true, false },
	}};
	return descriptors;
}

std::span<const HostUiGroupDescriptor> ui_group_descriptors()
{
	static constexpr std::array<HostUiGroupDescriptor, 8> descriptors {{
		{ "editor", "Editor" },
		{ "file", "File" },
		{ "viewport_layout", "Viewport Layout" },
		{ "panels", "Panels" },
		{ "selection_operations", "Selection Operations" },
		{ "transform_tools", "Transform Tools" },
		{ "modal_modeling_tools", "Modal Modeling Tools" },
		{ "operation_settings", "Operation Settings" },
	}};
	return descriptors;
}

std::span<const HostUiPlacementDescriptor> ui_placement_descriptors()
{
	static constexpr std::array<HostUiPlacementDescriptor, 31> descriptors {{
		{ "menu_open_editor", "main_menu", "editor", "Open Editor", HostUiPlacementTargetKind::editor_action, "open_editor", 0, false },
		{ "menu_new_document", "main_menu", "file", "New Document", HostUiPlacementTargetKind::editor_action, "new_document", 100, false },
		{ "menu_open_document", "main_menu", "file", "Open...", HostUiPlacementTargetKind::editor_action, "open_document", 110, false },
		{ "menu_save_document", "main_menu", "file", "Save", HostUiPlacementTargetKind::editor_action, "save_document", 120, false },
		{ "menu_save_as_document", "main_menu", "file", "Save As...", HostUiPlacementTargetKind::editor_action, "save_as_document", 130, false },
		{ "menu_layout_single_view", "main_menu", "viewport_layout", "Single View", HostUiPlacementTargetKind::viewport_action, "layout_single_view", 200, false },
		{ "menu_layout_two_view_horizontal", "main_menu", "viewport_layout", "Two-View Horizontal", HostUiPlacementTargetKind::viewport_action, "layout_two_view_horizontal", 210, false },
		{ "menu_layout_two_view_vertical", "main_menu", "viewport_layout", "Two-View Vertical", HostUiPlacementTargetKind::viewport_action, "layout_two_view_vertical", 220, false },
		{ "menu_layout_three_view_primary_left", "main_menu", "viewport_layout", "Three-View Primary Left", HostUiPlacementTargetKind::viewport_action, "layout_three_view_primary_left", 230, false },
		{ "menu_layout_three_view_primary_right", "main_menu", "viewport_layout", "Three-View Primary Right", HostUiPlacementTargetKind::viewport_action, "layout_three_view_primary_right", 240, false },
		{ "menu_layout_three_view_primary_top", "main_menu", "viewport_layout", "Three-View Primary Top", HostUiPlacementTargetKind::viewport_action, "layout_three_view_primary_top", 250, false },
		{ "menu_layout_three_view_primary_bottom", "main_menu", "viewport_layout", "Three-View Primary Bottom", HostUiPlacementTargetKind::viewport_action, "layout_three_view_primary_bottom", 260, false },
		{ "menu_layout_four_view_quad", "main_menu", "viewport_layout", "Four-View Quad", HostUiPlacementTargetKind::viewport_action, "layout_four_view_quad", 270, false },
		{ "menu_viewport_settings_panel", "main_menu", "panels", "Viewport Settings", HostUiPlacementTargetKind::panel, "viewport_settings", 300, false },
		{ "operations_object_panel", "operations_top_bar", "selection_operations", "Object Operations", HostUiPlacementTargetKind::panel, "object_operations", 0, false },
		{ "operations_vertex_panel", "operations_top_bar", "selection_operations", "Vertex Operations", HostUiPlacementTargetKind::panel, "vertex_operations", 10, false },
		{ "operations_edge_panel", "operations_top_bar", "selection_operations", "Edge Operations", HostUiPlacementTargetKind::panel, "edge_operations", 20, false },
		{ "operations_face_panel", "operations_top_bar", "selection_operations", "Face Operations", HostUiPlacementTargetKind::panel, "face_operations", 30, false },
		{ "toolbar_move_tool", "viewport_tool_toolbar", "transform_tools", "Move", HostUiPlacementTargetKind::tool, "move", 0, false },
		{ "toolbar_rotate_tool", "viewport_tool_toolbar", "transform_tools", "Rotate", HostUiPlacementTargetKind::tool, "rotate", 10, false },
		{ "toolbar_scale_tool", "viewport_tool_toolbar", "transform_tools", "Scale", HostUiPlacementTargetKind::tool, "scale", 20, false },
		{ "toolbar_knife_tool", "viewport_tool_toolbar", "modal_modeling_tools", "Knife", HostUiPlacementTargetKind::tool, "knife", 30, false },
		{ "toolbar_cut_tool", "viewport_tool_toolbar", "modal_modeling_tools", "Cut", HostUiPlacementTargetKind::tool, "cut", 40, false },
		{ "toolbar_mirror_tool", "viewport_tool_toolbar", "modal_modeling_tools", "Mirror", HostUiPlacementTargetKind::tool, "mirror", 50, false },
		{ "toolbar_poly_tool", "viewport_tool_toolbar", "modal_modeling_tools", "Poly", HostUiPlacementTargetKind::tool, "poly", 60, false },
		{ "settings_interpolated_bridge", "tool_settings_panel", "operation_settings", "Interpolated Bridge", HostUiPlacementTargetKind::preference_group, "operation.bridge", 0, false, HostUiVisibilityCondition::bridge_preview_active },
		{ "settings_edge_bevel", "tool_settings_panel", "operation_settings", "Bevel Edges", HostUiPlacementTargetKind::preference_group, "operation.edge_bevel", 10, false, HostUiVisibilityCondition::edge_bevel_preview_active },
		{ "settings_thicken", "tool_settings_panel", "operation_settings", "Thicken", HostUiPlacementTargetKind::preference_group, "operation.thicken", 20, false, HostUiVisibilityCondition::thicken_preview_active },
		{ "settings_slice_quad", "tool_settings_panel", "operation_settings", "Slice Quad", HostUiPlacementTargetKind::preference_group, "operation.slice_quad", 30, false, HostUiVisibilityCondition::slice_quad_preview_active },
		{ "settings_merge_by_distance", "tool_settings_panel", "operation_settings", "Merge by Distance", HostUiPlacementTargetKind::preference_group, "operation.merge_by_distance", 40, false, HostUiVisibilityCondition::merge_by_distance_preview_active },
		{ "settings_cut_keep_mode", "tool_settings_panel", "operation_settings", "Keep Mode", HostUiPlacementTargetKind::preference_group, "operation.cut", 50, false, HostUiVisibilityCondition::cut_tool_active },
	}};
	return descriptors;
}

std::span<const HostInteractionDescriptor> interaction_descriptors()
{
	static constexpr std::array<HostInteractionDescriptor, 38> descriptors {{
		{ "open_editor", "Open Editor", HostInteractionKind::editor_action },
		{ "new_document", "New Document", HostInteractionKind::editor_action },
		{ "open_document", "Open Document", HostInteractionKind::editor_action },
		{ "save_document", "Save Document", HostInteractionKind::editor_action },
		{ "save_as_document", "Save Document As", HostInteractionKind::editor_action },
		{ "undo", "Undo", HostInteractionKind::editor_action },
		{ "redo", "Redo", HostInteractionKind::editor_action },
		{ "select_all", "Select All", HostInteractionKind::editor_action },
		{ "copy_meshes", "Copy Meshes", HostInteractionKind::editor_action },
		{ "duplicate_meshes", "Duplicate Meshes", HostInteractionKind::editor_action },
		{ "apply_active_material", "Apply Active Material", HostInteractionKind::editor_action },
		{ "invert_selection", "Invert Selection", HostInteractionKind::editor_action },
		{ "repeat_last_action", "Repeat Last Action", HostInteractionKind::editor_action },
		{ "paste_meshes", "Paste Meshes", HostInteractionKind::editor_action },
		{ "delete", "Delete", HostInteractionKind::editor_action },
		{ "layout_single_view", "Single View", HostInteractionKind::viewport_action },
		{ "layout_two_view_horizontal", "Two-View Horizontal", HostInteractionKind::viewport_action },
		{ "layout_two_view_vertical", "Two-View Vertical", HostInteractionKind::viewport_action },
		{ "layout_three_view_primary_left", "Three-View Primary Left", HostInteractionKind::viewport_action },
		{ "layout_three_view_primary_right", "Three-View Primary Right", HostInteractionKind::viewport_action },
		{ "layout_three_view_primary_top", "Three-View Primary Top", HostInteractionKind::viewport_action },
		{ "layout_three_view_primary_bottom", "Three-View Primary Bottom", HostInteractionKind::viewport_action },
		{ "layout_four_view_quad", "Four-View Quad", HostInteractionKind::viewport_action },
		{ "focus_all", "Focus All", HostInteractionKind::viewport_action },
		{ "focus_origin", "Focus Origin", HostInteractionKind::viewport_action },
		{ "mirror_axis_x", "Mirror Across X QAxis", HostInteractionKind::viewport_action },
		{ "mirror_axis_y", "Mirror Across Y QAxis", HostInteractionKind::viewport_action },
		{ "mirror_axis_z", "Mirror Across Z QAxis", HostInteractionKind::viewport_action },
		{ "flip_horizontal", "Flip Horizontally", HostInteractionKind::viewport_action },
		{ "flip_vertical", "Flip Vertically", HostInteractionKind::viewport_action },
		{ "zoom", "Zoom", HostInteractionKind::viewport_action },
		{ "fly_or_orthographic_pan", "Fly Or Orthographic Pan", HostInteractionKind::viewport_action },
		{ "orbit_or_pan", "Orbit Or Pan", HostInteractionKind::viewport_action },
		{ "toggle_focused_pane_maximized", "Toggle Focused Pane Maximized", HostInteractionKind::viewport_action },
		{ "active_preview_pointer_block", "Active Preview Pointer Block", HostInteractionKind::preview_action },
		{ "active_preview_commit_or_cancel", "Active Preview Commit Or Cancel", HostInteractionKind::preview_action },
		{ "active_modal_tool_precedence", "Active Modal Tool Precedence", HostInteractionKind::input_policy },
		{ "modifier_hover_refresh", "Modifier Hover Refresh", HostInteractionKind::input_policy },
	}};
	return descriptors;
}

std::span<const HostToolGroupDescriptor> tool_group_descriptors()
{
	static constexpr std::array<std::string_view, 3> transform_tools {{
		"move",
		"rotate",
		"scale",
	}};
	static constexpr std::array<std::string_view, 6> modal_modeling_tools {{
		"box",
		"insert_edge_loop",
		"knife",
		"cut",
		"mirror",
		"poly",
	}};
	static constexpr std::array<HostToolGroupDescriptor, 2> descriptors {{
		{ "transform_tools", "Transform Tools", transform_tools },
		{ "modal_modeling_tools", "Modal Modeling Tools", modal_modeling_tools },
	}};
	return descriptors;
}

std::span<const HostPanelDescriptor> panel_descriptors()
{
	static constexpr std::array<std::string_view, 8> viewport_settings_groups {{
		"viewport.scene",
		"viewport.grid",
		"viewport.selection",
		"viewport.overlay",
		"viewport.vertices",
		"viewport.measurements",
		"viewport.picking",
		"viewport.gizmo",
	}};
	static constexpr std::array<HostPanelDescriptor, 1> descriptors {{
		{ "viewport_settings", "Viewport Settings", viewport_settings_groups },
	}};
	return descriptors;
}

std::string_view shortcut_for_tool(std::string_view id)
{
	if (id == "select") {
		return "Q";
	}
	if (id == "move") {
		return "W";
	}
	if (id == "rotate") {
		return "R";
	}
	if (id == "scale") {
		return "S";
	}
	if (id == "box") {
		return "B";
	}
	if (id == "insert_edge_loop") {
		return "Shift+R";
	}
	if (id == "knife") {
		return "C";
	}
	if (id == "cut") {
		return "Shift+C";
	}
	if (id == "poly") {
		return "P";
	}
	return {};
}

bool is_transform_tool(quader_editor_core::ViewportInteractionTool tool)
{
	return tool == quader_editor_core::ViewportInteractionTool::move ||
		tool == quader_editor_core::ViewportInteractionTool::extend ||
		tool == quader_editor_core::ViewportInteractionTool::extrude ||
		tool == quader_editor_core::ViewportInteractionTool::rotate ||
		tool == quader_editor_core::ViewportInteractionTool::scale;
}

bool is_modal_tool(quader_editor_core::ViewportInteractionTool tool)
{
	return tool == quader_editor_core::ViewportInteractionTool::box ||
		tool == quader_editor_core::ViewportInteractionTool::insert_edge_loop ||
		tool == quader_editor_core::ViewportInteractionTool::knife ||
		tool == quader_editor_core::ViewportInteractionTool::cut ||
		tool == quader_editor_core::ViewportInteractionTool::mirror ||
		tool == quader_editor_core::ViewportInteractionTool::poly;
}

std::vector<HostToolDescriptor> build_tool_catalog()
{
	std::vector<HostToolDescriptor> descriptors;
	const std::span<const quader_editor_core::EditorTool* const> tools =
		quader_editor_core::tool_registry().tools();
	descriptors.reserve(tools.size());
	for (const quader_editor_core::EditorTool *tool : tools) {
		if (tool == nullptr) {
			continue;
		}
		descriptors.push_back({
			tool->id(),
			tool->label(),
			shortcut_for_tool(tool->id()),
			is_transform_tool(tool->tool_id()),
			is_modal_tool(tool->tool_id()),
		});
	}
	return descriptors;
}

HostShortcutDescriptor shortcut(
		std::string_view id,
		HostShortcutContext context,
		std::string_view key,
		bool primary_modifier,
		bool alt,
		bool shift,
		HostShortcutTargetKind target_kind,
		std::string_view target_id,
		bool alternate = false,
		HostShortcutRegistrationPolicy registration_policy = HostShortcutRegistrationPolicy::quader_local)
{
	return {
		id,
		context,
		key,
		primary_modifier,
		alt,
		shift,
		target_kind,
		target_id,
		alternate,
		registration_policy,
	};
}

std::span<const HostShortcutDescriptor> shortcut_descriptors()
{
	static const std::array<HostShortcutDescriptor, 44> descriptors {{
		shortcut("undo", HostShortcutContext::global, "Z", true, false, false,
			HostShortcutTargetKind::editor_action, "undo"),
		shortcut("redo_shift_z", HostShortcutContext::global, "Z", true, false, true,
			HostShortcutTargetKind::editor_action, "redo"),
		shortcut("redo_y", HostShortcutContext::global, "Y", true, false, false,
			HostShortcutTargetKind::editor_action, "redo"),
		shortcut("new_document", HostShortcutContext::global, "N", true, false, false,
			HostShortcutTargetKind::editor_action, "new_document"),
		shortcut("open_document", HostShortcutContext::global, "O", true, false, false,
			HostShortcutTargetKind::editor_action, "open_document"),
		shortcut("save_document", HostShortcutContext::global, "S", true, false, false,
			HostShortcutTargetKind::editor_action, "save_document"),
		shortcut("select_all", HostShortcutContext::global, "A", true, false, false,
			HostShortcutTargetKind::editor_action, "select_all"),
		shortcut("copy_meshes", HostShortcutContext::global, "C", true, false, false,
			HostShortcutTargetKind::editor_action, "copy_meshes"),
		shortcut("duplicate_meshes", HostShortcutContext::global, "D", true, false, false,
			HostShortcutTargetKind::editor_action, "duplicate_meshes"),
		shortcut("invert_selection", HostShortcutContext::global, "I", true, false, false,
			HostShortcutTargetKind::editor_action, "invert_selection"),
		shortcut("repeat_last_action", HostShortcutContext::global, "R", true, false, false,
			HostShortcutTargetKind::editor_action, "repeat_last_action"),
		shortcut("paste_meshes", HostShortcutContext::global, "V", true, false, false,
			HostShortcutTargetKind::editor_action, "paste_meshes"),
		shortcut("delete", HostShortcutContext::global, "Delete", false, false, false,
			HostShortcutTargetKind::editor_action, "delete"),
		shortcut("delete_backspace", HostShortcutContext::global, "Backspace", false, false, false,
			HostShortcutTargetKind::editor_action, "delete"),

		shortcut("merge_vertices", HostShortcutContext::viewport, "M", false, false, false,
			HostShortcutTargetKind::action, "merge_vertices"),
		shortcut("merge_vertices_at_center", HostShortcutContext::viewport, "M", false, false, true,
			HostShortcutTargetKind::action, "merge_vertices_at_center", true),
		shortcut("apply_active_material", HostShortcutContext::viewport, "T", false, false, true,
			HostShortcutTargetKind::action, "apply_active_material"),
		shortcut("focus_all_home", HostShortcutContext::viewport, "Home", false, false, false,
			HostShortcutTargetKind::viewport_action, "focus_all"),
		shortcut("focus_origin", HostShortcutContext::viewport, "Home", false, false, true,
			HostShortcutTargetKind::viewport_action, "focus_origin"),
		shortcut("insert_edge_loop_tool", HostShortcutContext::viewport, "R", false, false, true,
			HostShortcutTargetKind::action, "insert_edge_loop_tool"),
		shortcut("cut_tool", HostShortcutContext::viewport, "C", false, false, true,
			HostShortcutTargetKind::action, "cut_tool"),
		shortcut("mirror_axis_x", HostShortcutContext::viewport, "X", false, false, true,
			HostShortcutTargetKind::viewport_action, "mirror_axis_x"),
		shortcut("mirror_axis_y", HostShortcutContext::viewport, "Y", false, false, true,
			HostShortcutTargetKind::viewport_action, "mirror_axis_y"),
		shortcut("mirror_axis_z", HostShortcutContext::viewport, "Z", false, false, true,
			HostShortcutTargetKind::viewport_action, "mirror_axis_z"),
		shortcut("flip_horizontal", HostShortcutContext::viewport, "H", false, false, true,
			HostShortcutTargetKind::viewport_action, "flip_horizontal"),
		shortcut("flip_vertical", HostShortcutContext::viewport, "V", false, false, true,
			HostShortcutTargetKind::viewport_action, "flip_vertical"),
		shortcut("box_tool", HostShortcutContext::viewport, "B", false, false, false,
			HostShortcutTargetKind::tool, "box"),
		shortcut("select_tool", HostShortcutContext::viewport, "Q", false, false, false,
			HostShortcutTargetKind::tool, "select"),
		shortcut("knife_tool", HostShortcutContext::viewport, "C", false, false, false,
			HostShortcutTargetKind::action, "knife_tool"),
		shortcut("poly_tool", HostShortcutContext::viewport, "P", false, false, false,
			HostShortcutTargetKind::action, "poly_tool"),
		shortcut("flip_mesh_normals", HostShortcutContext::viewport, "F", false, false, false,
			HostShortcutTargetKind::action, "invert_mesh"),
		shortcut("move_tool", HostShortcutContext::viewport, "W", false, false, false,
			HostShortcutTargetKind::tool, "move"),
		shortcut("rotate_tool", HostShortcutContext::viewport, "R", false, false, false,
			HostShortcutTargetKind::tool, "rotate"),
		shortcut("scale_tool", HostShortcutContext::viewport, "S", false, false, false,
			HostShortcutTargetKind::tool, "scale"),
		shortcut("vertex_selection", HostShortcutContext::viewport, "1", false, false, false,
			HostShortcutTargetKind::selection_mode, "vertex"),
		shortcut("vertex_selection_keypad", HostShortcutContext::viewport, "Keypad1", false, false, false,
			HostShortcutTargetKind::selection_mode, "vertex"),
		shortcut("edge_selection", HostShortcutContext::viewport, "2", false, false, false,
			HostShortcutTargetKind::selection_mode, "edge"),
		shortcut("edge_selection_keypad", HostShortcutContext::viewport, "Keypad2", false, false, false,
			HostShortcutTargetKind::selection_mode, "edge"),
		shortcut("face_selection", HostShortcutContext::viewport, "3", false, false, false,
			HostShortcutTargetKind::selection_mode, "face"),
		shortcut("face_selection_keypad", HostShortcutContext::viewport, "Keypad3", false, false, false,
			HostShortcutTargetKind::selection_mode, "face"),
		shortcut("object_selection", HostShortcutContext::viewport, "4", false, false, false,
			HostShortcutTargetKind::selection_mode, "object"),
		shortcut("object_selection_keypad", HostShortcutContext::viewport, "Keypad4", false, false, false,
			HostShortcutTargetKind::selection_mode, "object"),
		shortcut("group_selection", HostShortcutContext::viewport, "5", false, false, false,
			HostShortcutTargetKind::selection_mode, "group"),
		shortcut("group_selection_keypad", HostShortcutContext::viewport, "Keypad5", false, false, false,
			HostShortcutTargetKind::selection_mode, "group"),
	}};
	return descriptors;
}

HostGestureDescriptor gesture(
		std::string_view id,
		std::string_view label,
		HostGestureTargetKind target_kind,
		std::string_view target_id,
		std::uint32_t priority,
		std::span<const std::string_view> tool_ids,
		std::span<const HostPointerInputKind> pointer_inputs,
		std::span<const std::string_view> key_inputs,
		std::span<const std::string_view> modifier_meanings,
		bool requires_active_tool,
		bool requires_active_preview,
		bool blocked_by_active_preview,
		bool suppressed_while_fly_navigation,
		bool consumes_pointer_when_active,
		bool suppresses_gizmo_and_measurements,
		bool returns_to_select_on_cancel,
		bool returns_to_select_on_success,
		bool rebuilds_mesh_on_success)
{
	return {
		id,
		label,
		target_kind,
		target_id,
		priority,
		tool_ids,
		pointer_inputs,
		key_inputs,
		modifier_meanings,
		requires_active_tool,
		requires_active_preview,
		blocked_by_active_preview,
		suppressed_while_fly_navigation,
		consumes_pointer_when_active,
		suppresses_gizmo_and_measurements,
		returns_to_select_on_cancel,
		returns_to_select_on_success,
		rebuilds_mesh_on_success,
	};
}

std::span<const HostGestureDescriptor> gesture_descriptors()
{
	static constexpr std::array<std::string_view, 6> modal_tools {{
		"box",
		"insert_edge_loop",
		"knife",
		"cut",
		"mirror",
		"poly",
	}};
	static constexpr std::array<std::string_view, 3> transform_tools {{
		"move",
		"rotate",
		"scale",
	}};
	static constexpr std::array<std::string_view, 1> selection_tool {{
		"select",
	}};
	static constexpr std::array<std::string_view, 1> box_tool {{
		"box",
	}};
	static constexpr std::array<std::string_view, 1> insert_edge_loop_tool {{
		"insert_edge_loop",
	}};
	static constexpr std::array<std::string_view, 1> knife_tool {{
		"knife",
	}};
	static constexpr std::array<std::string_view, 1> cut_tool {{
		"cut",
	}};
	static constexpr std::array<std::string_view, 1> poly_tool {{
		"poly",
	}};

	static constexpr std::array<HostPointerInputKind, 4> hover_primary_drag {{
		HostPointerInputKind::mouse_move,
		HostPointerInputKind::primary_down,
		HostPointerInputKind::primary_drag,
		HostPointerInputKind::primary_up,
	}};
	static constexpr std::array<HostPointerInputKind, 3> primary_drag {{
		HostPointerInputKind::primary_down,
		HostPointerInputKind::primary_drag,
		HostPointerInputKind::primary_up,
	}};
	static constexpr std::array<HostPointerInputKind, 3> primary_preview_block {{
		HostPointerInputKind::primary_down,
		HostPointerInputKind::primary_drag,
		HostPointerInputKind::primary_up,
	}};
	static constexpr std::array<HostPointerInputKind, 1> mouse_move {{
		HostPointerInputKind::mouse_move,
	}};
	static constexpr std::array<HostPointerInputKind, 1> scroll {{
		HostPointerInputKind::scroll,
	}};
	static constexpr std::array<HostPointerInputKind, 3> secondary_drag {{
		HostPointerInputKind::secondary_down,
		HostPointerInputKind::secondary_drag,
		HostPointerInputKind::secondary_up,
	}};
	static constexpr std::array<HostPointerInputKind, 3> middle_drag {{
		HostPointerInputKind::middle_down,
		HostPointerInputKind::middle_drag,
		HostPointerInputKind::middle_up,
	}};

	static constexpr std::array<std::string_view, 1> cancel_escape {{
		"Escape",
	}};
	static constexpr std::array<std::string_view, 4> knife_keys {{
		"Escape",
		"Space",
		"Return",
		"KeypadEnter",
	}};
	static constexpr std::array<std::string_view, 2> finalize_keys {{
		"Escape",
		"Space",
	}};
	static constexpr std::array<std::string_view, 3> cut_keys {{
		"Escape",
		"Space",
		"F",
	}};
	static constexpr std::array<std::string_view, 2> preview_keys {{
		"Escape",
		"Space",
	}};
	static constexpr std::array<std::string_view, 1> fly_keys {{
		"Escape",
	}};
	static constexpr std::array<std::string_view, 1> layout_keys {{
		"Space",
	}};

	static constexpr std::array<std::string_view, 2> snap_axis_modifiers {{
		"primary_modifier:bypass_snap",
		"shift:constrain_axis",
	}};
	static constexpr std::array<std::string_view, 4> knife_meanings {{
		"primary_modifier:bypass_snap",
		"shift:constrain_axis",
		"Space:finalize",
		"Return:finalize_and_start_new_operation",
	}};
	static constexpr std::array<std::string_view, 1> bypass_snap_modifier {{
		"primary_modifier:bypass_snap",
	}};
	static constexpr std::array<std::string_view, 2> cut_meanings {{
		"F:cycle_keep_mode",
		"Space:finalize",
	}};
	static constexpr std::array<std::string_view, 3> poly_meanings {{
		"primary_modifier:bypass_snap",
		"shift:constrain_axis",
		"Space:finalize",
	}};
	static constexpr std::array<std::string_view, 4> selection_modifiers {{
		"primary_modifier:toggle_selection",
		"shift:extend_selection",
		"platform_modifier:multi_select",
		"alt:alternate_pick",
	}};
	static constexpr std::array<std::string_view, 3> transform_modifiers {{
		"shift:duplicate_object_selection",
		"shift:extrude_or_inset_elements",
		"primary_modifier:transform_snap_modifier",
	}};
	static constexpr std::array<std::string_view, 1> scroll_modifiers {{
		"primary_modifier:zoom_to_cursor",
	}};
	static constexpr std::array<std::string_view, 2> middle_modifiers {{
		"shift:pan",
		"alt:orbit_snap_modifier",
	}};
	static const std::array<HostGestureDescriptor, 15> descriptors {{
		gesture("active_modal_tool_precedence", "Active Modal Tool Precedence",
			HostGestureTargetKind::input_policy, "active_modal_tool_precedence", 0,
			modal_tools, {}, {}, {}, true, false, false, false, true, true, false, false, false),
		gesture("box_tool_gesture", "Box Tool Gesture",
			HostGestureTargetKind::tool, "box", 10,
			box_tool, hover_primary_drag, cancel_escape, {}, true, false, false, false, true, true, false, false, true),
		gesture("insert_edge_loop_gesture", "Insert Edge Loop Gesture",
			HostGestureTargetKind::tool, "insert_edge_loop", 20,
			insert_edge_loop_tool, hover_primary_drag, cancel_escape, bypass_snap_modifier, true, false, false, false, true, true, true, false, true),
		gesture("knife_tool_gesture", "Knife Tool Gesture",
			HostGestureTargetKind::tool, "knife", 30,
			knife_tool, hover_primary_drag, knife_keys, knife_meanings, true, false, false, false, true, true, true, true, true),
		gesture("cut_tool_gesture", "Cut Tool Gesture",
			HostGestureTargetKind::tool, "cut", 40,
			cut_tool, hover_primary_drag, cut_keys, cut_meanings, true, false, false, false, true, true, true, true, true),
		gesture("poly_tool_gesture", "Poly Tool Gesture",
			HostGestureTargetKind::tool, "poly", 50,
			poly_tool, hover_primary_drag, finalize_keys, poly_meanings, true, false, false, false, true, true, true, true, true),
		gesture("active_preview_pointer_block", "Active Preview Pointer Block",
			HostGestureTargetKind::preview_action, "active_preview_pointer_block", 60,
			{}, primary_preview_block, {}, {}, false, true, false, true, true, true, false, false, false),
		gesture("transform_gizmo_drag", "Transform Gizmo Drag",
			HostGestureTargetKind::tool_group, "transform_tools", 70,
			transform_tools, primary_drag, cancel_escape, transform_modifiers, false, false, true, true, true, false, false, false, true),
		gesture("selection_pick_and_paint", "Selection Pick And Paint",
			HostGestureTargetKind::tool, "select", 80,
			selection_tool, hover_primary_drag, {}, selection_modifiers, false, false, true, true, true, false, false, false, false),
		gesture("modifier_hover_refresh", "Modifier Hover Refresh",
			HostGestureTargetKind::input_policy, "modifier_hover_refresh", 90,
			modal_tools, mouse_move, {}, snap_axis_modifiers, false, false, false, true, false, true, false, false, false),
		gesture("viewport_scroll_zoom", "Viewport Scroll Zoom",
			HostGestureTargetKind::viewport_action, "zoom", 100,
			{}, scroll, {}, scroll_modifiers, false, false, false, false, true, false, false, false, false),
		gesture("viewport_fly_or_orthographic_pan", "Viewport Fly Or Orthographic Pan",
			HostGestureTargetKind::viewport_action, "fly_or_orthographic_pan", 110,
			{}, secondary_drag, fly_keys, {}, false, false, false, false, true, false, false, false, false),
		gesture("viewport_orbit_or_pan", "Viewport Orbit Or Pan",
			HostGestureTargetKind::viewport_action, "orbit_or_pan", 120,
			{}, middle_drag, {}, middle_modifiers, false, false, false, false, true, false, false, false, false),
		gesture("active_preview_commit_or_cancel", "Active Preview Commit Or Cancel",
			HostGestureTargetKind::preview_action, "active_preview_commit_or_cancel", 130,
			{}, {}, preview_keys, {}, false, true, false, true, false, true, false, false, true),
		gesture("focused_pane_maximize_toggle", "Focused Pane Maximize Toggle",
			HostGestureTargetKind::viewport_action, "toggle_focused_pane_maximized", 140,
			{}, {}, layout_keys, {}, false, false, false, true, false, false, false, false, false),
	}};
	return descriptors;
}

constexpr int checked_overlay_layer_index(quader_editor_core::SelectionOverlayLayer layer)
{
	using Layer = quader_editor_core::SelectionOverlayLayer;
	switch (layer) {
	case Layer::selected_visible_face:
		return 0;
	case Layer::selected_face_edge:
		return 1;
	case Layer::unselected_vertex:
		return 2;
	case Layer::selected_vertex:
		return 3;
	case Layer::hover_face:
		return 4;
	case Layer::hover_face_edge:
		return 5;
	case Layer::hover_edge:
		return 6;
	case Layer::hover_vertex:
		return 7;
	case Layer::selected_edge:
		return 8;
	case Layer::selected_xray_face:
		return 9;
	case Layer::selected_face_mask:
		return 10;
	case Layer::source_wire:
		return 11;
	case Layer::open_edge:
		return 12;
	case Layer::snap_target_vertex:
		return 13;
	case Layer::diagnostic_edge:
		return 14;
	}
	return -1;
}

HostOverlayLayerDescriptor overlay_layer(
		quader_editor_core::SelectionOverlayLayer layer,
		std::string_view id,
		std::string_view label,
		bool triangles,
		bool lines,
		bool points,
		bool line_draws_on_top = false,
		bool point_draws_on_top = false)
{
	return {
		checked_overlay_layer_index(layer),
		id,
		label,
		triangles,
		lines,
		points,
		line_draws_on_top,
		point_draws_on_top,
		!(line_draws_on_top || point_draws_on_top),
	};
}

constexpr double channel_to_unit(std::uint32_t value)
{
	return static_cast<double>(value & 0xffU) / 255.0;
}

constexpr HostColorDescriptor color_rgba(double r, double g, double b, double a)
{
	return { r, g, b, a };
}

constexpr HostColorDescriptor color_from_abgr(std::uint32_t color_abgr)
{
	return {
		channel_to_unit(color_abgr),
		channel_to_unit(color_abgr >> 8U),
		channel_to_unit(color_abgr >> 16U),
		channel_to_unit(color_abgr >> 24U),
	};
}

HostPreferenceDescriptor int_preference(
		std::string_view id,
		std::string_view label,
		HostPreferenceScope scope,
		int default_value,
		int minimum,
		int maximum,
		int step,
		std::string_view group_id,
		std::string_view related_operation_id = {},
		std::string_view related_option_id = {},
		bool hard_limits = true)
{
	HostPreferenceDescriptor descriptor;
	descriptor.id = id;
	descriptor.label = label;
	descriptor.scope = scope;
	descriptor.value_kind = HostPreferenceValueKind::integer;
	descriptor.default_integer = default_value;
	descriptor.minimum_integer = minimum;
	descriptor.maximum_integer = maximum;
	descriptor.integer_step = step;
	descriptor.group_id = group_id;
	descriptor.related_operation_id = related_operation_id;
	descriptor.related_option_id = related_option_id;
	descriptor.hard_limits = hard_limits;
	return descriptor;
}

HostPreferenceDescriptor float_preference(
		std::string_view id,
		std::string_view label,
		HostPreferenceScope scope,
		double default_value,
		double minimum,
		double maximum,
		double step,
		std::string_view group_id,
		std::string_view related_operation_id = {},
		std::string_view related_option_id = {},
		std::string_view related_overlay_layer_id = {},
		bool hard_limits = true)
{
	HostPreferenceDescriptor descriptor;
	descriptor.id = id;
	descriptor.label = label;
	descriptor.scope = scope;
	descriptor.value_kind = HostPreferenceValueKind::floating;
	descriptor.default_number = default_value;
	descriptor.minimum_number = minimum;
	descriptor.maximum_number = maximum;
	descriptor.number_step = step;
	descriptor.group_id = group_id;
	descriptor.related_operation_id = related_operation_id;
	descriptor.related_option_id = related_option_id;
	descriptor.related_overlay_layer_id = related_overlay_layer_id;
	descriptor.hard_limits = hard_limits;
	return descriptor;
}

HostPreferenceDescriptor bool_preference(
		std::string_view id,
		std::string_view label,
		bool default_value,
		std::string_view group_id,
		std::string_view related_operation_id = {},
		std::string_view related_option_id = {},
		bool visible = true)
{
	HostPreferenceDescriptor descriptor;
	descriptor.id = id;
	descriptor.label = label;
	descriptor.scope = HostPreferenceScope::operation;
	descriptor.value_kind = HostPreferenceValueKind::boolean;
	descriptor.default_bool = default_value;
	descriptor.group_id = group_id;
	descriptor.related_operation_id = related_operation_id;
	descriptor.related_option_id = related_option_id;
	descriptor.visible = visible;
	descriptor.hard_limits = false;
	return descriptor;
}

HostPreferenceDescriptor color_preference(
		std::string_view id,
		std::string_view label,
		HostColorDescriptor default_value,
		std::string_view group_id,
		std::string_view related_overlay_layer_id = {})
{
	HostPreferenceDescriptor descriptor;
	descriptor.id = id;
	descriptor.label = label;
	descriptor.scope = HostPreferenceScope::viewport_style;
	descriptor.value_kind = HostPreferenceValueKind::color_rgba;
	descriptor.default_color = default_value;
	descriptor.group_id = group_id;
	descriptor.related_overlay_layer_id = related_overlay_layer_id;
	return descriptor;
}

HostPreferenceDescriptor choice_preference(
		std::string_view id,
		std::string_view label,
		std::span<const HostPreferenceChoiceDescriptor> choices,
		std::string_view group_id)
{
	HostPreferenceDescriptor descriptor;
	descriptor.id = id;
	descriptor.label = label;
	descriptor.scope = HostPreferenceScope::operation;
	descriptor.value_kind = HostPreferenceValueKind::choice;
	descriptor.choices = choices;
	for (const HostPreferenceChoiceDescriptor& choice : choices) {
		if (choice.default_choice) {
			descriptor.default_integer = choice.value;
			break;
		}
	}
	descriptor.group_id = group_id;
	descriptor.hard_limits = true;
	return descriptor;
}

HostPreferenceDescriptor operation_int_preference(
		std::string_view id,
		const quader_editor_core::IntOperationOptionSchema& schema,
		std::string_view group_id,
		std::string_view operation_id)
{
	return int_preference(
		id,
		schema.label,
		HostPreferenceScope::operation,
		schema.default_value,
		schema.minimum,
		schema.maximum,
		schema.step,
		group_id,
		operation_id,
		schema.id,
		schema.hard_limits);
}

HostPreferenceDescriptor operation_float_preference(
		std::string_view id,
		const quader_editor_core::FloatOperationOptionSchema& schema,
		std::string_view group_id,
		std::string_view operation_id)
{
	return float_preference(
		id,
		schema.label,
		HostPreferenceScope::operation,
		schema.default_value,
		schema.minimum,
		schema.maximum,
		schema.step,
		group_id,
		operation_id,
		schema.id,
		{},
		schema.hard_limits);
}

HostPreferenceDescriptor operation_bool_preference(
		std::string_view id,
		const quader_editor_core::BoolOperationOptionSchema& schema,
		std::string_view group_id,
		std::string_view operation_id)
{
	return bool_preference(id, schema.label, schema.default_value, group_id, operation_id, schema.id, schema.visible);
}

std::span<const HostPreferenceDescriptor> preference_descriptors()
{
	const quader_editor_core::BridgeOperationSchema bridge = quader_editor_core::bridge_operation_schema();
	const quader_editor_core::EdgeBevelOperationSchema edge_bevel = quader_editor_core::edge_bevel_operation_schema();
	const quader_editor_core::ThickenOperationSchema thicken = quader_editor_core::thicken_operation_schema();
	const quader_editor_core::SliceQuadOperationSchema slice_quad = quader_editor_core::slice_quad_operation_schema();
	const quader_editor_core::MergeByDistanceOperationSchema merge_by_distance = quader_editor_core::merge_by_distance_operation_schema();
	static const std::array<HostPreferenceChoiceDescriptor, 3> cut_mode_choices {{
		{ "keep_back", "Discard Left",
			quader_editor_core::cut_mode_id(quader_editor_core::CutMode::keep_back), false },
		{ "keep_front", "Discard Right",
			quader_editor_core::cut_mode_id(quader_editor_core::CutMode::keep_front), false },
		{ "keep_both", "Keep Both",
			quader_editor_core::cut_mode_id(quader_editor_core::CutMode::keep_both), true },
	}};

	static const std::array<HostPreferenceDescriptor, 49> descriptors {{
		operation_int_preference("operation.bridge.steps", bridge.steps, "operation.bridge", bridge.operation_id),
		operation_float_preference("operation.edge_bevel.width", edge_bevel.width, "operation.edge_bevel", edge_bevel.operation_id),
		operation_float_preference("operation.edge_bevel.profile", edge_bevel.profile, "operation.edge_bevel", edge_bevel.operation_id),
		operation_int_preference("operation.edge_bevel.segments", edge_bevel.segments, "operation.edge_bevel", edge_bevel.operation_id),
		operation_float_preference("operation.thicken.thickness", thicken.thickness, "operation.thicken", thicken.operation_id),
		operation_bool_preference("operation.thicken.from_center", thicken.from_center, "operation.thicken", thicken.operation_id),
		operation_int_preference("operation.slice_quad.x_slices", slice_quad.x_slices, "operation.slice_quad", slice_quad.operation_id),
		operation_int_preference("operation.slice_quad.y_slices", slice_quad.y_slices, "operation.slice_quad", slice_quad.operation_id),
		operation_float_preference("operation.merge_by_distance.tolerance", merge_by_distance.tolerance, "operation.merge_by_distance", merge_by_distance.operation_id),
		choice_preference("operation.cut.keep_mode", "Keep Mode", cut_mode_choices, "operation.cut"),

		color_preference("viewport.scene.background_color", "Background", color_rgba(13.0 / 255.0, 13.0 / 255.0, 13.0 / 255.0, 1.0), "viewport.scene"),
		color_preference("viewport.grid.minor_color", "Minor", color_rgba(0.8, 0.8, 0.8, 0.05), "viewport.grid"),
		color_preference("viewport.grid.major_color", "Major", color_rgba(0.298, 0.298, 0.298, 0.459), "viewport.grid"),
		color_preference("viewport.grid.x_axis_color", "X QAxis", color_rgba(1.0, 0.239, 0.0, 0.72), "viewport.grid"),
		color_preference("viewport.grid.y_axis_color", "Y QAxis", color_rgba(0.29, 0.58, 0.0, 0.0), "viewport.grid"),
		color_preference("viewport.grid.z_axis_color", "Z QAxis", color_rgba(0.059, 0.612, 1.0, 0.72), "viewport.grid"),
		color_preference("viewport.selection.face_color", "Face Fill", color_from_abgr(0x081facffU), "viewport.selection", "selected_visible_face"),
		color_preference("viewport.selection.wire_color", "Wire", color_from_abgr(0xff1fd3ffU), "viewport.selection", "selected_face_edge"),
		color_preference("viewport.overlay.source_wire_color", "Source Wire", color_from_abgr(0xebffe05cU), "viewport.overlay", "source_wire"),
		color_preference("viewport.overlay.open_edge_color", "Open Edge", color_from_abgr(0xff00ffffU), "viewport.overlay", "open_edge"),
		color_preference("viewport.overlay.diagnostic_edge_color", "Diagnostic Edge", color_from_abgr(0xff0000ffU), "viewport.overlay", "diagnostic_edge"),
		color_preference("viewport.overlay.hover_face_color", "Hover Face", color_from_abgr(0x161fff66U), "viewport.overlay", "hover_face"),
		color_preference("viewport.overlay.hover_wire_color", "Hover Wire", color_from_abgr(0xff00ff51U), "viewport.overlay", "hover_edge"),
		color_preference("viewport.overlay.remove_selection_face_color", "Remove Face", color_from_abgr(0x52ff6b1fU), "viewport.overlay", "hover_face"),
		color_preference("viewport.overlay.remove_selection_wire_color", "Remove Wire", color_from_abgr(0xffff6b1fU), "viewport.overlay", "hover_edge"),
		color_preference("viewport.vertices.vertex_color", "Vertex", color_from_abgr(0xffffe66bU), "viewport.vertices", "unselected_vertex"),
		color_preference("viewport.vertices.selected_vertex_color", "Selected Vertex", color_from_abgr(0xff1fadffU), "viewport.vertices", "selected_vertex"),
		color_preference("viewport.vertices.hover_vertex_color", "Hover Vertex", color_from_abgr(0xff38ff00U), "viewport.vertices", "hover_vertex"),
		color_preference("viewport.vertices.remove_selection_vertex_color", "Remove Vertex", color_from_abgr(0xffff6b1fU), "viewport.vertices", "hover_vertex"),
		color_preference("viewport.measurements.text_color", "Text", color_rgba(1.0, 1.0, 1.0, 1.0), "viewport.measurements"),
		color_preference("viewport.measurements.outline_color", "Outline", color_rgba(0.0, 0.0, 0.0, 0.95), "viewport.measurements"),

		float_preference("viewport.grid.minor_line_size", "Minor Line", HostPreferenceScope::viewport_style, 0.32, 0.25, 8.0, 0.0, "viewport.grid"),
		float_preference("viewport.grid.major_line_size", "Major Line", HostPreferenceScope::viewport_style, 0.36, 0.25, 8.0, 0.0, "viewport.grid"),
		float_preference("viewport.grid.axis_line_size", "QAxis Line", HostPreferenceScope::viewport_style, 0.65, 0.25, 8.0, 0.0, "viewport.grid"),
		float_preference("viewport.overlay.source_wire_line_size", "Source Wire Size", HostPreferenceScope::viewport_style, 1.5, 0.25, 8.0, 0.0, "viewport.overlay", {}, {}, "source_wire"),
		float_preference("viewport.selection.face_wire_line_size", "Face Wire Size", HostPreferenceScope::viewport_style, 2.5, 0.25, 8.0, 0.0, "viewport.selection", {}, {}, "selected_face_edge"),
		float_preference("viewport.selection.edge_line_size", "Edge Line Size", HostPreferenceScope::viewport_style, 2.5, 0.25, 8.0, 0.0, "viewport.selection", {}, {}, "selected_edge"),
		float_preference("viewport.overlay.hover_wire_line_size", "Hover Wire Size", HostPreferenceScope::viewport_style, 2.5, 0.25, 8.0, 0.0, "viewport.overlay", {}, {}, "hover_edge"),
		float_preference("viewport.overlay.open_edge_line_size", "Open Edge Size", HostPreferenceScope::viewport_style, 1.0, 0.25, 8.0, 0.0, "viewport.overlay", {}, {}, "open_edge"),
		float_preference("viewport.overlay.diagnostic_edge_line_size", "Diagnostic Edge Size", HostPreferenceScope::viewport_style, 4.0, 0.25, 8.0, 0.0, "viewport.overlay", {}, {}, "diagnostic_edge"),
		float_preference("viewport.vertices.vertex_size", "Vertex Size", HostPreferenceScope::viewport_style, 7.0, 2.0, 32.0, 0.0, "viewport.vertices", {}, {}, "unselected_vertex"),
		float_preference("viewport.vertices.selected_growth", "Selected Growth", HostPreferenceScope::viewport_style, 0.5, 0.0, 12.0, 0.0, "viewport.vertices", {}, {}, "selected_vertex"),
		float_preference("viewport.vertices.hover_growth", "Hover Growth", HostPreferenceScope::viewport_style, 0.5, 0.0, 12.0, 0.0, "viewport.vertices", {}, {}, "hover_vertex"),
		float_preference("viewport.vertices.outline_size", "Outline Size", HostPreferenceScope::viewport_style, 1.0, 0.0, 12.0, 0.0, "viewport.vertices"),
		float_preference("viewport.measurements.line_size", "Line Size", HostPreferenceScope::viewport_style, 2.0, 0.25, 8.0, 0.0, "viewport.measurements"),
		float_preference("viewport.measurements.text_size", "Text Size", HostPreferenceScope::viewport_style, 8.0, 8.0, 28.0, 0.0, "viewport.measurements"),
		float_preference("viewport.picking.vertex_radius", "Vertex Radius", HostPreferenceScope::viewport_picking, 0.3, 0.001, 2.0, 0.0, "viewport.picking"),
		float_preference("viewport.picking.edge_radius", "Edge Radius", HostPreferenceScope::viewport_picking, 0.2, 0.001, 2.0, 0.0, "viewport.picking"),
		float_preference("viewport.gizmo.size", "Size", HostPreferenceScope::viewport_interaction, 1.0, 0.5, 2.0, 0.0, "viewport.gizmo"),
	}};
	return descriptors;
}

std::span<const HostPreferenceGroupDescriptor> preference_group_descriptors()
{
	static constexpr std::array<std::string_view, 1> operation_bridge_preferences {{
		"operation.bridge.steps",
	}};
	static constexpr std::array<std::string_view, 3> operation_edge_bevel_preferences {{
		"operation.edge_bevel.width",
		"operation.edge_bevel.profile",
		"operation.edge_bevel.segments",
	}};
	static constexpr std::array<std::string_view, 2> operation_thicken_preferences {{
		"operation.thicken.thickness",
		"operation.thicken.from_center",
	}};
	static constexpr std::array<std::string_view, 2> operation_slice_quad_preferences {{
		"operation.slice_quad.x_slices",
		"operation.slice_quad.y_slices",
	}};
	static constexpr std::array<std::string_view, 1> operation_merge_by_distance_preferences {{
		"operation.merge_by_distance.tolerance",
	}};
	static constexpr std::array<std::string_view, 1> operation_cut_preferences {{
		"operation.cut.keep_mode",
	}};
	static constexpr std::array<std::string_view, 1> viewport_scene_preferences {{
		"viewport.scene.background_color",
	}};
	static constexpr std::array<std::string_view, 8> viewport_grid_preferences {{
		"viewport.grid.minor_color",
		"viewport.grid.major_color",
		"viewport.grid.x_axis_color",
		"viewport.grid.y_axis_color",
		"viewport.grid.z_axis_color",
		"viewport.grid.minor_line_size",
		"viewport.grid.major_line_size",
		"viewport.grid.axis_line_size",
	}};
	static constexpr std::array<std::string_view, 4> viewport_selection_preferences {{
		"viewport.selection.face_color",
		"viewport.selection.wire_color",
		"viewport.selection.face_wire_line_size",
		"viewport.selection.edge_line_size",
	}};
	static constexpr std::array<std::string_view, 11> viewport_overlay_preferences {{
		"viewport.overlay.source_wire_color",
		"viewport.overlay.open_edge_color",
		"viewport.overlay.diagnostic_edge_color",
		"viewport.overlay.hover_face_color",
		"viewport.overlay.hover_wire_color",
		"viewport.overlay.remove_selection_face_color",
		"viewport.overlay.remove_selection_wire_color",
		"viewport.overlay.source_wire_line_size",
		"viewport.overlay.hover_wire_line_size",
		"viewport.overlay.open_edge_line_size",
		"viewport.overlay.diagnostic_edge_line_size",
	}};
	static constexpr std::array<std::string_view, 8> viewport_vertex_preferences {{
		"viewport.vertices.vertex_color",
		"viewport.vertices.selected_vertex_color",
		"viewport.vertices.hover_vertex_color",
		"viewport.vertices.remove_selection_vertex_color",
		"viewport.vertices.vertex_size",
		"viewport.vertices.selected_growth",
		"viewport.vertices.hover_growth",
		"viewport.vertices.outline_size",
	}};
	static constexpr std::array<std::string_view, 4> viewport_measurement_preferences {{
		"viewport.measurements.text_color",
		"viewport.measurements.outline_color",
		"viewport.measurements.line_size",
		"viewport.measurements.text_size",
	}};
	static constexpr std::array<std::string_view, 2> viewport_picking_preferences {{
		"viewport.picking.vertex_radius",
		"viewport.picking.edge_radius",
	}};
	static constexpr std::array<std::string_view, 1> viewport_gizmo_preferences {{
		"viewport.gizmo.size",
	}};

	static const std::array<HostPreferenceGroupDescriptor, 14> descriptors {{
		{ "operation.bridge", "Bridge", HostPreferenceScope::operation, operation_bridge_preferences },
		{ "operation.edge_bevel", "Edge Bevel", HostPreferenceScope::operation, operation_edge_bevel_preferences },
		{ "operation.thicken", "Thicken", HostPreferenceScope::operation, operation_thicken_preferences },
		{ "operation.slice_quad", "Slice Quad", HostPreferenceScope::operation, operation_slice_quad_preferences },
		{ "operation.merge_by_distance", "Merge by Distance", HostPreferenceScope::operation, operation_merge_by_distance_preferences },
		{ "operation.cut", "Cut", HostPreferenceScope::operation, operation_cut_preferences },
		{ "viewport.scene", "Scene", HostPreferenceScope::viewport_style, viewport_scene_preferences },
		{ "viewport.grid", "Grid", HostPreferenceScope::viewport_style, viewport_grid_preferences },
		{ "viewport.selection", "Selection", HostPreferenceScope::viewport_style, viewport_selection_preferences },
		{ "viewport.overlay", "Overlay", HostPreferenceScope::viewport_style, viewport_overlay_preferences },
		{ "viewport.vertices", "Vertices", HostPreferenceScope::viewport_style, viewport_vertex_preferences },
		{ "viewport.measurements", "Measurements", HostPreferenceScope::viewport_style, viewport_measurement_preferences },
		{ "viewport.picking", "Picking", HostPreferenceScope::viewport_picking, viewport_picking_preferences },
		{ "viewport.gizmo", "Gizmo", HostPreferenceScope::viewport_interaction, viewport_gizmo_preferences },
	}};
	return descriptors;
}

std::span<const std::string_view> overlay_draw_order_layer_ids()
{
	static constexpr std::array<std::string_view, 15> ids {{
		"selected_visible_face",
		"selected_face_mask",
		"source_wire",
		"selected_xray_face",
		"hover_face",
		"open_edge",
		"diagnostic_edge",
		"selected_face_edge",
		"selected_edge",
		"hover_face_edge",
		"hover_edge",
		"unselected_vertex",
		"selected_vertex",
		"hover_vertex",
		"snap_target_vertex",
	}};
	return ids;
}

std::span<const std::string_view> blocking_preview_override_layer_ids()
{
	static constexpr std::array<std::string_view, 4> ids {{
		"selected_face_edge",
		"selected_edge",
		"source_wire",
		"open_edge",
	}};
	return ids;
}

std::span<const std::string_view> transient_hover_mask_writer_layer_ids()
{
	static constexpr std::array<std::string_view, 4> ids {{
		"hover_face",
		"hover_face_edge",
		"hover_edge",
		"hover_vertex",
	}};
	return ids;
}

std::span<const std::string_view> transient_hover_suppressed_layer_ids()
{
	static constexpr std::array<std::string_view, 6> ids {{
		"selected_visible_face",
		"selected_xray_face",
		"selected_face_mask",
		"selected_face_edge",
		"selected_edge",
		"selected_vertex",
	}};
	return ids;
}

std::span<const HostOverlayRenderPolicyDescriptor> overlay_render_policy_descriptors()
{
	static constexpr std::array<HostOverlayRenderPolicyDescriptor, 5> descriptors {{
		{ "depth_tested_fill", HostOverlayGeometryKind::triangles, 0.035, 0.0, false },
		{ "depth_tested_line", HostOverlayGeometryKind::lines, 0.02, 1.0, false },
		{ "depth_tested_point", HostOverlayGeometryKind::points, 0.01, 0.0, false },
		{ "draw_on_top_line", HostOverlayGeometryKind::lines, 0.0, 1.0, true },
		{ "draw_on_top_point", HostOverlayGeometryKind::points, 0.0, 0.0, true },
	}};
	return descriptors;
}

std::span<const HostMaterialTextureAssetDescriptor> material_texture_asset_descriptors()
{
	static constexpr std::array<HostMaterialTextureAssetDescriptor, 2> descriptors {{
		{ "default_albedo", "Default Albedo", HostMaterialTextureRole::base_color, "assets/materials/default/default_albedo.png" },
		{ "default_roughness", "Default Roughness", HostMaterialTextureRole::roughness, "assets/materials/default/default_roughness.png" },
	}};
	return descriptors;
}

std::span<const HostMaterialGeneratedTextureDescriptor> material_generated_texture_descriptors()
{
	static constexpr std::array<HostMaterialGeneratedTextureDescriptor, 2> descriptors {{
		{
			"default_metallic_smoothness",
			"Default Metallic Smoothness",
			HostMaterialTextureRole::metallic_smoothness,
			"default_roughness",
			true,
			HostMaterialTextureChannel::alpha,
			0.0,
			0.0,
			0.0,
			1.0,
		},
		{
			"default_packed_mask",
			"Default Packed Mask",
			HostMaterialTextureRole::mask,
			"default_roughness",
			true,
			HostMaterialTextureChannel::alpha,
			0.0,
			1.0,
			0.0,
			1.0,
		},
	}};
	return descriptors;
}

std::span<const HostMaterialScalarDescriptor> default_material_scalars()
{
	static constexpr std::array<HostMaterialScalarDescriptor, 7> descriptors {{
		{ HostMaterialScalarKind::metallic, 0.0 },
		{ HostMaterialScalarKind::smoothness, 1.0 },
		{ HostMaterialScalarKind::smoothness_texture_channel, 0.0 },
		{ HostMaterialScalarKind::smoothness_remap_min, 0.0 },
		{ HostMaterialScalarKind::smoothness_remap_max, 1.0 },
		{ HostMaterialScalarKind::ambient_occlusion_remap_min, 1.0 },
		{ HostMaterialScalarKind::ambient_occlusion_remap_max, 1.0 },
	}};
	return descriptors;
}

std::span<const HostMaterialRecipeDescriptor> material_recipe_descriptors()
{
	static constexpr std::array<std::string_view, 2> texture_assets {{
		"default_albedo",
		"default_roughness",
	}};
	static constexpr std::array<std::string_view, 2> generated_textures {{
		"default_metallic_smoothness",
		"default_packed_mask",
	}};
	static const std::array<HostMaterialRecipeDescriptor, 1> descriptors {{
		{
			"default_material",
			"Quader Default Material",
			color_rgba(1.0, 1.0, 1.0, 1.0),
			texture_assets,
			generated_textures,
			default_material_scalars(),
			{ 1.0, -1.0 },
			{ 0.0, 1.0 },
		},
	}};
	return descriptors;
}

constexpr double host_default_orthographic_size = 24.0;
constexpr double host_focus_origin_distance = 64.0;
constexpr double host_default_fov_degrees = 60.0;
constexpr double host_default_grid_map_units = 32.0;

} // namespace

std::span<const HostOperationDescriptor> operation_catalog()
{
	static const std::vector<HostOperationDescriptor> descriptors = build_operation_catalog();
	return descriptors;
}

const HostOperationDescriptor *find_operation_descriptor(std::string_view operation_id)
{
	for (const HostOperationDescriptor& descriptor : operation_catalog()) {
		if (descriptor.id == operation_id) {
			return &descriptor;
		}
	}
	return nullptr;
}

std::span<const HostActionDescriptor> action_catalog()
{
	return action_descriptors();
}

const HostActionDescriptor *find_action_descriptor(std::string_view action_id)
{
	for (const HostActionDescriptor& descriptor : action_catalog()) {
		if (descriptor.id == action_id) {
			return &descriptor;
		}
	}
	return nullptr;
}

std::span<const HostOperationPanelDescriptor> operation_panel_catalog()
{
	return operation_panel_descriptors();
}

const HostOperationPanelDescriptor *find_operation_panel_descriptor(std::string_view panel_id)
{
	for (const HostOperationPanelDescriptor& descriptor : operation_panel_catalog()) {
		if (descriptor.id == panel_id) {
			return &descriptor;
		}
	}
	return nullptr;
}

std::span<const HostUiSurfaceDescriptor> ui_surface_catalog()
{
	return ui_surface_descriptors();
}

const HostUiSurfaceDescriptor *find_ui_surface_descriptor(std::string_view surface_id)
{
	for (const HostUiSurfaceDescriptor& descriptor : ui_surface_catalog()) {
		if (descriptor.id == surface_id) {
			return &descriptor;
		}
	}
	return nullptr;
}

std::span<const HostUiPlacementDescriptor> ui_placement_catalog()
{
	return ui_placement_descriptors();
}

const HostUiPlacementDescriptor *find_ui_placement_descriptor(std::string_view placement_id)
{
	for (const HostUiPlacementDescriptor& descriptor : ui_placement_catalog()) {
		if (descriptor.id == placement_id) {
			return &descriptor;
		}
	}
	return nullptr;
}

std::span<const HostUiGroupDescriptor> ui_group_catalog()
{
	return ui_group_descriptors();
}

const HostUiGroupDescriptor *find_ui_group_descriptor(std::string_view group_id)
{
	for (const HostUiGroupDescriptor& descriptor : ui_group_catalog()) {
		if (descriptor.id == group_id) {
			return &descriptor;
		}
	}
	return nullptr;
}

std::span<const HostInteractionDescriptor> interaction_catalog()
{
	return interaction_descriptors();
}

const HostInteractionDescriptor *find_interaction_descriptor(std::string_view interaction_id)
{
	for (const HostInteractionDescriptor& descriptor : interaction_catalog()) {
		if (descriptor.id == interaction_id) {
			return &descriptor;
		}
	}
	return nullptr;
}

std::span<const HostToolGroupDescriptor> tool_group_catalog()
{
	return tool_group_descriptors();
}

const HostToolGroupDescriptor *find_tool_group_descriptor(std::string_view group_id)
{
	for (const HostToolGroupDescriptor& descriptor : tool_group_catalog()) {
		if (descriptor.id == group_id) {
			return &descriptor;
		}
	}
	return nullptr;
}

std::span<const HostPanelDescriptor> panel_catalog()
{
	return panel_descriptors();
}

const HostPanelDescriptor *find_panel_descriptor(std::string_view panel_id)
{
	for (const HostPanelDescriptor& descriptor : panel_catalog()) {
		if (descriptor.id == panel_id) {
			return &descriptor;
		}
	}
	return nullptr;
}

std::span<const HostToolDescriptor> tool_catalog()
{
	static const std::vector<HostToolDescriptor> descriptors = build_tool_catalog();
	return descriptors;
}

const HostToolDescriptor *find_tool_descriptor(std::string_view tool_id)
{
	for (const HostToolDescriptor& descriptor : tool_catalog()) {
		if (descriptor.id == tool_id) {
			return &descriptor;
		}
	}
	return nullptr;
}

std::span<const HostSelectionModeDescriptor> selection_mode_catalog()
{
	static const std::array<HostSelectionModeDescriptor, 5> descriptors {{
		{ HostSelectionMode::vertex, "vertex", "Vertex" },
		{ HostSelectionMode::edge, "edge", "Edge" },
		{ HostSelectionMode::face, "face", "Face" },
		{ HostSelectionMode::object, "object", "Object" },
		{ HostSelectionMode::group, "group", "Group" },
	}};
	return descriptors;
}

const HostSelectionModeDescriptor *find_selection_mode_descriptor(std::string_view selection_mode_id)
{
	for (const HostSelectionModeDescriptor& descriptor : selection_mode_catalog()) {
		if (descriptor.id == selection_mode_id) {
			return &descriptor;
		}
	}
	return nullptr;
}

std::span<const HostShortcutDescriptor> shortcut_catalog()
{
	return shortcut_descriptors();
}

std::span<const HostGestureDescriptor> gesture_catalog()
{
	return gesture_descriptors();
}

const HostGestureDescriptor *find_gesture_descriptor(std::string_view gesture_id)
{
	for (const HostGestureDescriptor& descriptor : gesture_catalog()) {
		if (descriptor.id == gesture_id) {
			return &descriptor;
		}
	}
	return nullptr;
}

std::span<const HostTransformAxisDescriptor> transform_axis_catalog()
{
	static const std::array<HostTransformAxisDescriptor, 7> descriptors {{
		{ quader_editor_core::axis_id(quader_editor_core::TransformAxis::x), "X", false },
		{ quader_editor_core::axis_id(quader_editor_core::TransformAxis::y), "Y", false },
		{ quader_editor_core::axis_id(quader_editor_core::TransformAxis::z), "Z", false },
		{ quader_editor_core::axis_id(quader_editor_core::TransformAxis::xy), "XY", true },
		{ quader_editor_core::axis_id(quader_editor_core::TransformAxis::xz), "XZ", true },
		{ quader_editor_core::axis_id(quader_editor_core::TransformAxis::yz), "YZ", true },
		{ quader_editor_core::axis_id(quader_editor_core::TransformAxis::all), "All", false },
	}};
	return descriptors;
}

std::span<const HostViewportViewDescriptor> viewport_view_catalog()
{
	static const std::array<HostViewportViewDescriptor, 7> descriptors {{
		{ "perspective", "Perspective", false, { 6.0, 5.0, 8.0 }, {}, { 0.0, 1.0, 0.0 } },
		{ "top", "Top", true, { 0.0, host_focus_origin_distance, 0.0 }, {}, { 0.0, 0.0, -1.0 } },
		{ "bottom", "Bottom", true, { 0.0, -host_focus_origin_distance, 0.0 }, {}, { 0.0, 0.0, 1.0 } },
		{ "front", "Front", true, { 0.0, 0.0, host_focus_origin_distance }, {}, { 0.0, 1.0, 0.0 } },
		{ "back", "Back", true, { 0.0, 0.0, -host_focus_origin_distance }, {}, { 0.0, 1.0, 0.0 } },
		{ "side", "Right", true, { host_focus_origin_distance, 0.0, 0.0 }, {}, { 0.0, 1.0, 0.0 } },
		{ "left", "Left", true, { -host_focus_origin_distance, 0.0, 0.0 }, {}, { 0.0, 1.0, 0.0 } },
	}};
	return descriptors;
}

std::span<const HostGridPresetDescriptor> grid_preset_catalog()
{
	constexpr double unit = quader_editor_core::default_map_units_per_world_unit;
	static const std::array<HostGridPresetDescriptor, 10> descriptors {{
		{ "F1", 1.0, 1.0 / unit, false },
		{ "F2", 2.0, 2.0 / unit, false },
		{ "F3", 4.0, 4.0 / unit, false },
		{ "F4", 8.0, 8.0 / unit, false },
		{ "F5", 16.0, 16.0 / unit, false },
		{ "F6", 32.0, 32.0 / unit, true },
		{ "F7", 64.0, 64.0 / unit, false },
		{ "F8", 128.0, 128.0 / unit, false },
		{ "F9", 256.0, 256.0 / unit, false },
		{ "F10", 512.0, 512.0 / unit, false },
	}};
	return descriptors;
}

HostViewportDefaultsDescriptor viewport_defaults()
{
	HostViewportDefaultsDescriptor descriptor;
	descriptor.map_units_per_world_unit = quader_editor_core::default_map_units_per_world_unit;
	descriptor.default_grid_map_units = host_default_grid_map_units;
	descriptor.default_orthographic_size = host_default_orthographic_size;
	descriptor.focus_origin_distance = host_focus_origin_distance;
	descriptor.default_fov_degrees = host_default_fov_degrees;
	descriptor.orbit_radians_per_pixel = quader_editor_core::navigation_orbit_radians_per_pixel;
	descriptor.fly_look_radians_per_pixel = quader_editor_core::navigation_fly_look_radians_per_pixel;
	descriptor.fly_base_speed = quader_editor_core::navigation_fly_base_speed;
	descriptor.fly_fast_multiplier = quader_editor_core::navigation_fly_fast_multiplier;
	descriptor.fly_slow_multiplier = quader_editor_core::navigation_fly_slow_multiplier;
	descriptor.selection_box_drag_threshold_pixels = quader_editor_core::selection_box_default_drag_threshold;
	return descriptor;
}

std::span<const HostOverlayLayerDescriptor> overlay_layer_catalog()
{
	using Layer = quader_editor_core::SelectionOverlayLayer;
	static const std::array<HostOverlayLayerDescriptor, 15> descriptors {{
		overlay_layer(Layer::selected_visible_face, "selected_visible_face", "Selected Visible Face", true, false, false),
		overlay_layer(Layer::selected_face_edge, "selected_face_edge", "Selected Face Edge", false, true, false, true),
		overlay_layer(Layer::unselected_vertex, "unselected_vertex", "Unselected Vertex", false, false, true),
		overlay_layer(Layer::selected_vertex, "selected_vertex", "Selected Vertex", false, false, true, false, true),
		overlay_layer(Layer::hover_face, "hover_face", "Hover Face", true, false, false),
		overlay_layer(Layer::hover_face_edge, "hover_face_edge", "Hover Face Edge", false, true, false),
		overlay_layer(Layer::hover_edge, "hover_edge", "Hover Edge", false, true, false),
		overlay_layer(Layer::hover_vertex, "hover_vertex", "Hover Vertex", false, false, true, false, true),
		overlay_layer(Layer::selected_edge, "selected_edge", "Selected Edge", false, true, false, true),
		overlay_layer(Layer::selected_xray_face, "selected_xray_face", "Selected Xray Face", true, false, false),
		overlay_layer(Layer::selected_face_mask, "selected_face_mask", "Selected Face Mask", true, false, false),
		overlay_layer(Layer::source_wire, "source_wire", "Source Wire", false, true, false),
		overlay_layer(Layer::open_edge, "open_edge", "Open Edge", false, true, false),
		overlay_layer(Layer::snap_target_vertex, "snap_target_vertex", "Snap Target Vertex", false, false, true, false, true),
		overlay_layer(Layer::diagnostic_edge, "diagnostic_edge", "Diagnostic Edge", false, true, false, true),
	}};
	return descriptors;
}

const HostOverlayLayerDescriptor *find_overlay_layer_descriptor(int stable_index)
{
	for (const HostOverlayLayerDescriptor& descriptor : overlay_layer_catalog()) {
		if (descriptor.stable_index == stable_index) {
			return &descriptor;
		}
	}
	return nullptr;
}

std::span<const HostPreferenceDescriptor> preference_catalog()
{
	return preference_descriptors();
}

const HostPreferenceDescriptor *find_preference_descriptor(std::string_view preference_id)
{
	for (const HostPreferenceDescriptor& descriptor : preference_catalog()) {
		if (descriptor.id == preference_id) {
			return &descriptor;
		}
	}
	return nullptr;
}

std::span<const HostPreferenceGroupDescriptor> preference_group_catalog()
{
	return preference_group_descriptors();
}

const HostPreferenceGroupDescriptor *find_preference_group_descriptor(std::string_view group_id)
{
	for (const HostPreferenceGroupDescriptor& descriptor : preference_group_catalog()) {
		if (descriptor.id == group_id) {
			return &descriptor;
		}
	}
	return nullptr;
}

HostOverlayPresentationDescriptor overlay_presentation()
{
	HostOverlayPresentationDescriptor descriptor;
	descriptor.suppress_gizmo_and_measurements_for_blocking_preview = true;
	descriptor.suppress_gizmo_and_measurements_for_modal_tool = true;
	descriptor.blocking_preview_wire_color = color_from_abgr(0xebffe05cU);
	descriptor.blocking_preview_override_layer_ids = blocking_preview_override_layer_ids();
	descriptor.draw_order_layer_ids = overlay_draw_order_layer_ids();
	descriptor.transient_hover_mask_writer_layer_ids = transient_hover_mask_writer_layer_ids();
	descriptor.transient_hover_suppressed_layer_ids = transient_hover_suppressed_layer_ids();
	descriptor.render_policies = overlay_render_policy_descriptors();
	return descriptor;
}

std::span<const HostMaterialTextureAssetDescriptor> material_texture_asset_catalog()
{
	return material_texture_asset_descriptors();
}

const HostMaterialTextureAssetDescriptor *find_material_texture_asset_descriptor(std::string_view texture_asset_id)
{
	for (const HostMaterialTextureAssetDescriptor& descriptor : material_texture_asset_catalog()) {
		if (descriptor.id == texture_asset_id) {
			return &descriptor;
		}
	}
	return nullptr;
}

std::span<const HostMaterialGeneratedTextureDescriptor> material_generated_texture_catalog()
{
	return material_generated_texture_descriptors();
}

const HostMaterialGeneratedTextureDescriptor *find_material_generated_texture_descriptor(std::string_view texture_id)
{
	for (const HostMaterialGeneratedTextureDescriptor& descriptor : material_generated_texture_catalog()) {
		if (descriptor.id == texture_id) {
			return &descriptor;
		}
	}
	return nullptr;
}

std::span<const HostMaterialRecipeDescriptor> material_recipe_catalog()
{
	return material_recipe_descriptors();
}

const HostMaterialRecipeDescriptor *find_material_recipe_descriptor(std::string_view recipe_id)
{
	for (const HostMaterialRecipeDescriptor& descriptor : material_recipe_catalog()) {
		if (descriptor.id == recipe_id) {
			return &descriptor;
		}
	}
	return nullptr;
}

} // namespace quader_host_contract

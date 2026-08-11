////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/metadata/host_contract.hpp>

#include <assets/default_material_catalog.hpp>
#include <editor/modeling/operations/operation_registry.hpp>
#include <editor/modeling/operations/operation_schema.hpp>
#include <editor/modeling/runtime/session.hpp>
#include <editor/tools/viewport_tool_descriptor_catalog.hpp>
#include <editor/interactions/cut_tool.hpp>
#include <editor/interactions/tool_identity.hpp>
#include <editor/modeling/runtime/tool_constants.hpp>
#include <core/scene_environment_catalog.hpp>
#include <viewport/viewport_layout.hpp>
#include <viewport/viewport_settings_defaults.hpp>

#include <algorithm>
#include <array>
#include <iterator>
#include <utility>
#include <vector>

namespace quader_host_contract {
namespace {

HostOptionDescriptor host_option(const quader_editor_core::EditorOperationOptionDescriptor& schema)
{
	HostOptionDescriptor option;
	option.id = schema.option_id;
	option.label = schema.label;
	option.default_integer = schema.default_integer;
	option.minimum_integer = schema.minimum_integer;
	option.maximum_integer = schema.maximum_integer;
	option.integer_step = schema.integer_step;
	option.default_number = schema.default_number;
	option.minimum_number = schema.minimum_number;
	option.maximum_number = schema.maximum_number;
	option.number_step = schema.number_step;
	option.default_bool = schema.default_bool;
	option.visible = schema.visible;
	option.hard_limits = schema.hard_limits;
	switch (schema.value_kind) {
        case quader_editor_core::EditorOperationOptionValueKind::Integer:
          option.value_kind = HostOptionValueKind::Integer;
          break;
        case quader_editor_core::EditorOperationOptionValueKind::Floating:
          option.value_kind = HostOptionValueKind::Floating;
          break;
        case quader_editor_core::EditorOperationOptionValueKind::Boolean:
          option.value_kind = HostOptionValueKind::Boolean;
          break;
	}
	return option;
}

/**
 * Groups schema-owned operation options by operation ID for host metadata projection.
 */
struct HostOperationOptions {
	std::string_view operation_id;
	std::vector<HostOptionDescriptor> options;
};

std::vector<HostOperationOptions> build_operation_options()
{
	std::vector<HostOperationOptions> groups;
	for (const quader_editor_core::EditorOperationOptionDescriptor& option :
			quader_editor_core::operation_option_descriptor_catalog()) {
		auto found = std::find_if(
			groups.begin(),
			groups.end(),
			[&](const HostOperationOptions& group) {
				return group.operation_id == option.operation_id;
			});
		if (found == groups.end()) {
			groups.push_back(HostOperationOptions{option.operation_id, {}});
			found = std::prev(groups.end());
		}
		found->options.push_back(host_option(option));
	}
	return groups;
}

std::span<const HostOptionDescriptor> options_for_operation(std::string_view id)
{
  static const std::vector<HostOperationOptions> kGroups =
      build_operation_options();
  for (const HostOperationOptions &group : kGroups) {
    if (group.operation_id == id) {
      return {group.options.data(), group.options.size()};
    }
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

HostActionDescriptor
operation_action(std::string_view id, std::string_view label,
                 std::string_view operation_id,
                 HostActionAvailabilityDescriptor availability,
                 HostPreviewKind preview_kind = HostPreviewKind::None,
                 bool show_in_operations_panel = true) {
  return {
      id, label,        HostActionKind::Operation, operation_id,
      {}, preview_kind, show_in_operations_panel,  availability,
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
      HostActionKind::Tool,
      {},
      tool_id,
      HostPreviewKind::None,
      show_in_operations_panel,
      availability,
  };
}

std::span<const HostActionDescriptor> action_descriptors()
{
  static const std::array<HostActionDescriptor, 43> kDescriptors{{
      operation_action("invert_mesh", "Flip Selected Mesh Normals", "flip_face_normals",
                       action_availability(HostSelectionMode::Object, 0, 0,
                                           HostActiveSelectionKind::None, false,
                                           0, 0, 1)),
      operation_action(
          "shade_smooth_mesh", "Set Selected Meshes to Smooth Shading", "shade_faces_smooth",
          action_availability(HostSelectionMode::Object, 0, 0,
                              HostActiveSelectionKind::None, false, 0, 0, 1)),
      operation_action("shade_flat_mesh", "Set Selected Meshes to Flat Shading", "shade_faces_flat",
                       action_availability(HostSelectionMode::Object, 0, 0,
                                           HostActiveSelectionKind::None, false,
                                           0, 0, 1)),
      operation_action("combine_mesh", "Combine Selected Meshes", "combine_meshes",
                       action_availability(HostSelectionMode::Object, 0, 0,
                                           HostActiveSelectionKind::None, false,
                                           0, 0, 2)),
      operation_action("half_cut", "Half-Cut Selected Meshes", "half_cut",
                       action_availability(HostSelectionMode::Object, 0, 0,
                                           HostActiveSelectionKind::None, false,
                                           0, 0, 1),
                       HostPreviewKind::HalfCut),
      operation_action("create_outer_hull", "Create Outer Hull from Selected Meshes", "thicken",
                       action_availability(HostSelectionMode::Object, 0, 0,
                                           HostActiveSelectionKind::None, false,
                                           0, 0, 1),
                       HostPreviewKind::Thicken),
      operation_action("snap_vertices_to_active", "Snap Selected Vertices to Active Vertex",
                       "snap_vertices_to_active",
                       action_availability(HostSelectionMode::Vertex, 2, 0,
                                           HostActiveSelectionKind::Vertex,
                                           false)),
      operation_action("merge_vertices", "Merge Selected Vertices to Active Vertex", "merge_vertices_to_active",
                       action_availability(HostSelectionMode::Vertex, 2, 0,
                                           HostActiveSelectionKind::Vertex,
                                           false)),
      operation_action("merge_vertices_at_center", "Merge Selected Vertices at Center",
                       "merge_vertices_to_center",
                       action_availability(HostSelectionMode::Vertex, 2, 0,
                                           HostActiveSelectionKind::Vertex,
                                           false)),
      operation_action(
          "merge_by_distance", "Merge Selected Vertices by Distance", "merge_by_distance",
          action_availability(HostSelectionMode::Vertex, 2, 0,
                              HostActiveSelectionKind::None, false),
          HostPreviewKind::MergeByDistance),
      operation_action("bevel_vertices", "Bevel Selected Vertices", "bevel_vertices",
                       action_availability(HostSelectionMode::Vertex, 1, 0,
                                           HostActiveSelectionKind::None,
                                           false)),
      operation_action("connect_vertices", "Connect Selected Vertices", "connect_vertices",
                       action_availability(HostSelectionMode::Vertex, 2, 0,
                                           HostActiveSelectionKind::None,
                                           true)),
      operation_action(
          "dissolve_vertices", "Dissolve Selected Vertices", "dissolve_vertices",
          action_availability(HostSelectionMode::Vertex, 1, 0,
                              HostActiveSelectionKind::None, false)),
      operation_action("radial_align_vertices", "Radially Align Selected Vertices", "radial_align",
                       action_availability(HostSelectionMode::Vertex, 3, 0,
                                           HostActiveSelectionKind::None,
                                           false)),
      operation_action(
          "snap_edges_to_active", "Snap Selected Edges to Active Edge", "snap_edges_to_active",
          action_availability(HostSelectionMode::Edge, 0, 2,
                              HostActiveSelectionKind::Edge, false)),
      operation_action("connect_edges", "Connect Selected Edges", "connect_edges",
                       action_availability(HostSelectionMode::Edge, 0, 2,
                                           HostActiveSelectionKind::None,
                                           true)),
      operation_action("split_edges", "Split Selected Edges", "split_edges",
                       action_availability(HostSelectionMode::Edge, 0, 1,
                                           HostActiveSelectionKind::None,
                                           false)),
      operation_action(
          "harden_edge_normals", "Harden Selected Edge Normals", "harden_edge_normals",
          action_availability(HostSelectionMode::Edge, 0, 1,
                              HostActiveSelectionKind::None, false)),
      operation_action(
          "soften_edge_normals", "Soften Selected Edge Normals", "soften_edge_normals",
          action_availability(HostSelectionMode::Edge, 0, 1,
                              HostActiveSelectionKind::None, false)),
      tool_action("insert_edge_loop_tool", "Insert Edge Loop Tool",
                  "insert_edge_loop",
                  action_availability(HostSelectionMode::Edge, 0, 0,
                                      HostActiveSelectionKind::None, false)),
      tool_action("knife_tool", "Knife Tool", "knife",
                  action_availability(HostSelectionMode::Edge, 0, 0,
                                      HostActiveSelectionKind::None, false)),
      tool_action("cut_tool", "Cut Tool", "cut",
                  action_availability(HostSelectionMode::Object, 0, 0,
                                      HostActiveSelectionKind::None, false)),
      tool_action("mirror_tool", "Mirror Tool", "mirror",
                  action_availability(HostSelectionMode::Object, 0, 0,
                                      HostActiveSelectionKind::None, false, 0,
                                      0, 1)),
      tool_action("poly_tool", "Polygon Tool", "poly",
                  action_availability(HostSelectionMode::Object, 0, 0,
                                      HostActiveSelectionKind::None, false)),
      operation_action(
          "apply_active_material", "Apply Active Material to Selected Faces",
          "assign_material",
          action_availability(HostSelectionMode::Face, 0, 0,
                              HostActiveSelectionKind::None, false, 0, 1),
          HostPreviewKind::None, false),
      operation_action("bevel_edges", "Bevel Selected Edges", "edge_bevel",
                       action_availability(HostSelectionMode::Edge, 0, 1,
                                           HostActiveSelectionKind::None,
                                           false),
                       HostPreviewKind::EdgeBevel),
      operation_action("bridge_edges", "Bridge Selected Edges", "bridge",
                       action_availability(HostSelectionMode::Edge, 0, 2,
                                           HostActiveSelectionKind::None,
                                           false)),
      operation_action(
          "interpolated_bridge_edges", "Bridge Selected Edges with Interpolation", "bridge",
          action_availability(HostSelectionMode::Edge, 0, 2,
                              HostActiveSelectionKind::None, false),
          HostPreviewKind::Bridge),
      operation_action("dissolve_edges", "Dissolve Selected Edges", "dissolve_edges",
                       action_availability(HostSelectionMode::Edge, 0, 1,
                                           HostActiveSelectionKind::None,
                                           true)),
      operation_action("merge_edges", "Merge Selected Edges", "merge_edges",
                       action_availability(HostSelectionMode::Edge, 0, 2,
                                           HostActiveSelectionKind::None,
                                           true)),
      operation_action("collapse_edges", "Collapse Selected Edges", "collapse_edges",
                       action_availability(HostSelectionMode::Edge, 0, 1,
                                           HostActiveSelectionKind::None,
                                           true)),
      operation_action("fill_hole", "Fill Hole from Selected Edges", "fill_hole",
                       action_availability(HostSelectionMode::Edge, 0, 3,
                                           HostActiveSelectionKind::None,
                                           true)),
      operation_action("radial_align_edges", "Radially Align Selected Edges", "radial_align",
                       action_availability(HostSelectionMode::Edge, 0, 2,
                                           HostActiveSelectionKind::None,
                                           false)),
      operation_action("bridge_faces", "Bridge Selected Faces", "bridge",
                       action_availability(HostSelectionMode::Face, 0, 0,
                                           HostActiveSelectionKind::None, false,
                                           0, 2)),
      operation_action(
          "interpolated_bridge_faces", "Bridge Selected Faces with Interpolation", "bridge",
          action_availability(HostSelectionMode::Face, 0, 0,
                              HostActiveSelectionKind::None, false, 0, 2),
          HostPreviewKind::Bridge),
      operation_action("invert_faces", "Flip Selected Face Normals", "flip_face_normals",
                       action_availability(HostSelectionMode::Face, 0, 0,
                                           HostActiveSelectionKind::None, false,
                                           0, 1)),
      operation_action("combine_faces", "Combine Selected Faces", "combine_faces",
                       action_availability(HostSelectionMode::Face, 0, 0,
                                           HostActiveSelectionKind::None, true,
                                           0, 2)),
      operation_action("collapse_faces", "Collapse Selected Faces", "collapse_faces",
                       action_availability(HostSelectionMode::Face, 0, 0,
                                           HostActiveSelectionKind::None, false,
                                           0, 1)),
      operation_action("radial_align_faces", "Radially Align Selected Faces", "radial_align",
                       action_availability(HostSelectionMode::Face, 0, 0,
                                           HostActiveSelectionKind::None, false,
                                           0, 1)),
      operation_action("slice_quad", "Slice Selected Quad Face", "slice_quad",
                       action_availability(HostSelectionMode::Face, 0, 0,
                                           HostActiveSelectionKind::None, false,
                                           0, 1),
                       HostPreviewKind::SliceQuad),
      operation_action("thicken_faces", "Thicken Selected Faces", "thicken",
                       action_availability(HostSelectionMode::Face, 0, 0,
                                           HostActiveSelectionKind::None, false,
                                           0, 1),
                       HostPreviewKind::Thicken),
      operation_action("extract_faces", "Extract Selected Faces", "extract_faces",
                       action_availability(HostSelectionMode::Face, 0, 0,
                                           HostActiveSelectionKind::None, true,
                                           0, 1)),
      operation_action("detach_faces", "Detach Selected Faces", "detach_faces",
                       action_availability(HostSelectionMode::Face, 0, 0,
                                           HostActiveSelectionKind::None, false,
                                           0, 1)),
  }};
  return kDescriptors;
}

std::span<const HostOperationPanelDescriptor> operation_panel_descriptors()
{
  static constexpr std::array<std::string_view, 6> kObjectActions{{
      "invert_mesh",
      "shade_smooth_mesh",
      "shade_flat_mesh",
      "combine_mesh",
      "half_cut",
      "create_outer_hull",
  }};
  static constexpr std::array<std::string_view, 8> kVertexActions{{
      "snap_vertices_to_active",
      "merge_vertices",
      "merge_vertices_at_center",
      "merge_by_distance",
      "bevel_vertices",
      "connect_vertices",
      "dissolve_vertices",
      "radial_align_vertices",
  }};
  static constexpr std::array<std::string_view, 13> kEdgeActions{{
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
  static constexpr std::array<std::string_view, 10> kFaceActions{{
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
  static constexpr std::array<HostOperationPanelDescriptor, 4> kDescriptors{{
      {"object_operations", "Object Operations", HostSelectionMode::Object,
       kObjectActions, true},
      {"vertex_operations", "Vertex Operations", HostSelectionMode::Vertex,
       kVertexActions, false},
      {"edge_operations", "Edge Operations", HostSelectionMode::Edge,
       kEdgeActions, false},
      {"face_operations", "Face Operations", HostSelectionMode::Face,
       kFaceActions, false},
  }};
  return kDescriptors;
}

std::span<const HostUiSurfaceDescriptor> ui_surface_descriptors()
{
  static constexpr std::array<HostUiSurfaceDescriptor, 4> kDescriptors{{
      {"main_menu", "Main Menu", HostUiSurfaceKind::Menu, false, false},
      {"operations_top_bar", "Operations Top Bar", HostUiSurfaceKind::Toolbar,
       false, true},
      {"viewport_tool_toolbar", "Viewport Tool Toolbar",
       HostUiSurfaceKind::Toolbar, true, false},
      {"tool_settings_panel", "Tool Settings Panel", HostUiSurfaceKind::Panel,
       false, false},
  }};
  return kDescriptors;
}

std::span<const HostUiGroupDescriptor> ui_group_descriptors()
{
  static constexpr std::array<HostUiGroupDescriptor, 8> kDescriptors{{
      {"editor", "Editor"},
      {"file", "File"},
      {"viewport_layout", "Viewport Layout"},
      {"panels", "Panels"},
      {"selection_operations", "Selection Operations"},
      {"transform_tools", "Transform Tools"},
      {"modal_modeling_tools", "Modal Modeling Tools"},
      {"operation_settings", "Operation Settings"},
  }};
  return kDescriptors;
}

std::string_view label_for_viewport_tool(std::string_view tool_id)
{
  const quader::ToolDescriptor *tool =
      quader::find_viewport_tool_descriptor(tool_id);
  return tool != nullptr ? tool->label : tool_id;
}

HostUiPlacementDescriptor viewport_tool_placement(
		std::string_view id,
		std::string_view group_id,
		std::string_view tool_id,
		std::uint32_t order)
{
  return {
      id,
      "viewport_tool_toolbar",
      group_id,
      label_for_viewport_tool(tool_id),
      HostUiPlacementTargetKind::Tool,
      tool_id,
      order,
      false,
  };
}

std::span<const HostUiPlacementDescriptor> ui_placement_descriptors()
{
  static const std::array<HostUiPlacementDescriptor, 36> kDescriptors{{
      {"menu_open_editor", "main_menu", "editor", "Open Editor",
       HostUiPlacementTargetKind::EditorAction, "open_editor", 0, false},
      {"menu_new_scene", "main_menu", "file", "New Scene",
       HostUiPlacementTargetKind::EditorAction, "new_scene", 100, false},
      {"menu_open_scene", "main_menu", "file", "Open Scene...",
       HostUiPlacementTargetKind::EditorAction, "open_scene", 110, false},
      {"menu_save_scene", "main_menu", "file", "Save Scene",
       HostUiPlacementTargetKind::EditorAction, "save_scene", 120, false},
      {"menu_save_scene_as", "main_menu", "file", "Save Scene As...",
       HostUiPlacementTargetKind::EditorAction, "save_scene_as", 130, false},
      {"menu_save_all_scenes", "main_menu", "file", "Save All Scenes",
       HostUiPlacementTargetKind::EditorAction, "save_all_scenes", 140,
       false},
      {"menu_close_scene", "main_menu", "file", "Close Scene",
       HostUiPlacementTargetKind::EditorAction, "close_scene", 150, false},
      {"menu_next_scene", "main_menu", "file", "Next Scene",
       HostUiPlacementTargetKind::EditorAction, "next_scene", 160, true},
      {"menu_previous_scene", "main_menu", "file", "Previous Scene",
       HostUiPlacementTargetKind::EditorAction, "previous_scene", 170, false},
      {"menu_layout_single_view", "main_menu", "viewport_layout", "Single View",
       HostUiPlacementTargetKind::ViewportAction, "layout_single_view", 200,
       false},
      {"menu_layout_two_view_horizontal", "main_menu", "viewport_layout",
       "Two-View Horizontal", HostUiPlacementTargetKind::ViewportAction,
       "layout_two_view_horizontal", 210, false},
      {"menu_layout_two_view_vertical", "main_menu", "viewport_layout",
       "Two-View Vertical", HostUiPlacementTargetKind::ViewportAction,
       "layout_two_view_vertical", 220, false},
      {"menu_layout_three_view_primary_left", "main_menu", "viewport_layout",
       "Three-View Primary Left", HostUiPlacementTargetKind::ViewportAction,
       "layout_three_view_primary_left", 230, false},
      {"menu_layout_three_view_primary_right", "main_menu", "viewport_layout",
       "Three-View Primary Right", HostUiPlacementTargetKind::ViewportAction,
       "layout_three_view_primary_right", 240, false},
      {"menu_layout_three_view_primary_top", "main_menu", "viewport_layout",
       "Three-View Primary Top", HostUiPlacementTargetKind::ViewportAction,
       "layout_three_view_primary_top", 250, false},
      {"menu_layout_three_view_primary_bottom", "main_menu", "viewport_layout",
       "Three-View Primary Bottom", HostUiPlacementTargetKind::ViewportAction,
       "layout_three_view_primary_bottom", 260, false},
      {"menu_layout_four_view_quad", "main_menu", "viewport_layout",
       "Four-View Quad", HostUiPlacementTargetKind::ViewportAction,
       "layout_four_view_quad", 270, false},
      {"menu_viewport_settings_panel", "main_menu", "panels",
       "Viewport Settings", HostUiPlacementTargetKind::Panel,
       "viewport_settings", 300, false},
      {"operations_object_panel", "operations_top_bar", "selection_operations",
       "Object Operations", HostUiPlacementTargetKind::Panel,
       "object_operations", 0, false},
      {"operations_vertex_panel", "operations_top_bar", "selection_operations",
       "Vertex Operations", HostUiPlacementTargetKind::Panel,
       "vertex_operations", 10, false},
      {"operations_edge_panel", "operations_top_bar", "selection_operations",
       "Edge Operations", HostUiPlacementTargetKind::Panel, "edge_operations",
       20, false},
      {"operations_face_panel", "operations_top_bar", "selection_operations",
       "Face Operations", HostUiPlacementTargetKind::Panel, "face_operations",
       30, false},
      viewport_tool_placement("toolbar_move_tool", "transform_tools", "move",
                              0),
      viewport_tool_placement("toolbar_rotate_tool", "transform_tools",
                              "rotate", 10),
      viewport_tool_placement("toolbar_scale_tool", "transform_tools", "scale",
                              20),
      viewport_tool_placement("toolbar_knife_tool", "modal_modeling_tools",
                              "knife", 30),
      viewport_tool_placement("toolbar_cut_tool", "modal_modeling_tools", "cut",
                              40),
      viewport_tool_placement("toolbar_mirror_tool", "modal_modeling_tools",
                              "mirror", 50),
      viewport_tool_placement("toolbar_poly_tool", "modal_modeling_tools",
                              "poly", 60),
      {"settings_interpolated_bridge", "tool_settings_panel",
       "operation_settings", "Interpolated Bridge",
       HostUiPlacementTargetKind::PreferenceGroup, "operation.bridge", 0, false,
       HostUiVisibilityCondition::BridgePreviewActive},
      {"settings_edge_bevel", "tool_settings_panel", "operation_settings",
       "Bevel Edges", HostUiPlacementTargetKind::PreferenceGroup,
       "operation.edge_bevel", 10, false,
       HostUiVisibilityCondition::EdgeBevelPreviewActive},
      {"settings_thicken", "tool_settings_panel", "operation_settings",
       "Thicken", HostUiPlacementTargetKind::PreferenceGroup,
       "operation.thicken", 20, false,
       HostUiVisibilityCondition::ThickenPreviewActive},
      {"settings_slice_quad", "tool_settings_panel", "operation_settings",
       "Slice Quad", HostUiPlacementTargetKind::PreferenceGroup,
       "operation.slice_quad", 30, false,
       HostUiVisibilityCondition::SliceQuadPreviewActive},
      {"settings_merge_by_distance", "tool_settings_panel",
       "operation_settings", "Merge by Distance",
       HostUiPlacementTargetKind::PreferenceGroup,
       "operation.merge_by_distance", 40, false,
       HostUiVisibilityCondition::MergeByDistancePreviewActive},
      {"settings_half_cut", "tool_settings_panel", "operation_settings",
       "Half-Cut", HostUiPlacementTargetKind::PreferenceGroup,
       "operation.half_cut", 50, false,
       HostUiVisibilityCondition::HalfCutPreviewActive},
      {"settings_cut_keep_mode", "tool_settings_panel", "operation_settings",
       "Keep Mode", HostUiPlacementTargetKind::PreferenceGroup, "operation.cut",
       60, false, HostUiVisibilityCondition::CutToolActive},
  }};
  return kDescriptors;
}

std::span<const HostInteractionDescriptor> interaction_descriptors()
{
  static constexpr std::array<HostInteractionDescriptor, 43> kDescriptors{{
      {"open_editor", "Open Editor", HostInteractionKind::EditorAction},
      {"new_scene", "New Scene", HostInteractionKind::EditorAction},
      {"open_scene", "Open Scene", HostInteractionKind::EditorAction},
      {"save_scene", "Save Scene", HostInteractionKind::EditorAction},
      {"save_scene_as", "Save Scene As", HostInteractionKind::EditorAction},
      {"save_all_scenes", "Save All Scenes",
       HostInteractionKind::EditorAction},
      {"close_scene", "Close Scene", HostInteractionKind::EditorAction},
      {"next_scene", "Next Scene", HostInteractionKind::EditorAction},
      {"previous_scene", "Previous Scene",
       HostInteractionKind::EditorAction},
      {"undo", "Undo", HostInteractionKind::EditorAction},
      {"redo", "Redo", HostInteractionKind::EditorAction},
      {"select_all", "Select All", HostInteractionKind::EditorAction},
      {"copy_meshes", "Copy Meshes", HostInteractionKind::EditorAction},
      {"duplicate_selection", "Duplicate Selection",
       HostInteractionKind::EditorAction},
      {"apply_active_material", "Apply Active Material",
       HostInteractionKind::EditorAction},
      {"invert_selection", "Invert Selection",
       HostInteractionKind::EditorAction},
      {"repeat_last_action", "Repeat Last Action",
       HostInteractionKind::EditorAction},
      {"paste_meshes", "Paste Meshes", HostInteractionKind::EditorAction},
      {"delete", "Delete", HostInteractionKind::EditorAction},
      {"layout_single_view", "Single View",
       HostInteractionKind::ViewportAction},
      {"layout_two_view_horizontal", "Two-View Horizontal",
       HostInteractionKind::ViewportAction},
      {"layout_two_view_vertical", "Two-View Vertical",
       HostInteractionKind::ViewportAction},
      {"layout_three_view_primary_left", "Three-View Primary Left",
       HostInteractionKind::ViewportAction},
      {"layout_three_view_primary_right", "Three-View Primary Right",
       HostInteractionKind::ViewportAction},
      {"layout_three_view_primary_top", "Three-View Primary Top",
       HostInteractionKind::ViewportAction},
      {"layout_three_view_primary_bottom", "Three-View Primary Bottom",
       HostInteractionKind::ViewportAction},
      {"layout_four_view_quad", "Four-View Quad",
       HostInteractionKind::ViewportAction},
      {"focus_all", "Focus All", HostInteractionKind::ViewportAction},
      {"focus_selection", "Focus Selection",
       HostInteractionKind::ViewportAction},
      {"focus_origin", "Focus Origin", HostInteractionKind::ViewportAction},
      {"mirror_axis_x", "Mirror Across X QAxis",
       HostInteractionKind::ViewportAction},
      {"mirror_axis_y", "Mirror Across Y QAxis",
       HostInteractionKind::ViewportAction},
      {"mirror_axis_z", "Mirror Across Z QAxis",
       HostInteractionKind::ViewportAction},
      {"flip_horizontal", "Flip Horizontally",
       HostInteractionKind::ViewportAction},
      {"flip_vertical", "Flip Vertically", HostInteractionKind::ViewportAction},
      {"zoom", "Zoom", HostInteractionKind::ViewportAction},
      {"fly_or_orthographic_pan", "Fly Or Orthographic Pan",
       HostInteractionKind::ViewportAction},
      {"orbit_or_pan", "Orbit Or Pan", HostInteractionKind::ViewportAction},
      {"toggle_focused_pane_maximized", "Toggle Focused Pane Maximized",
       HostInteractionKind::ViewportAction},
      {"active_preview_pointer_block", "Active Preview Pointer Block",
       HostInteractionKind::PreviewAction},
      {"active_preview_commit_or_cancel", "Active Preview Commit Or Cancel",
       HostInteractionKind::PreviewAction},
      {"active_modal_tool_precedence", "Active Modal Tool Precedence",
       HostInteractionKind::InputPolicy},
      {"modifier_hover_refresh", "Modifier Hover Refresh",
       HostInteractionKind::InputPolicy},
  }};
  return kDescriptors;
}

std::span<const HostToolGroupDescriptor> tool_group_descriptors()
{
  static constexpr std::array<std::string_view, 3> kTransformTools{{
      "move",
      "rotate",
      "scale",
  }};
  static constexpr std::array<std::string_view, 8> kModalModelingTools{{
      "create_primitive",
      "insert_edge_loop",
      "knife",
      "cut",
      "mirror",
      "poly",
      "linear_array",
      "radial_array",
  }};
  static constexpr std::array<HostToolGroupDescriptor, 2> kDescriptors{{
      {"transform_tools", "Transform Tools", kTransformTools},
      {"modal_modeling_tools", "Modal Modeling Tools", kModalModelingTools},
  }};
  return kDescriptors;
}

std::span<const HostPanelDescriptor> panel_descriptors()
{
  static constexpr std::array<std::string_view, 7> kViewportSettingsGroups{{
      "viewport.scene",
      "viewport.grid",
      "viewport.selection",
      "viewport.overlay",
      "viewport.vertices",
      "viewport.picking",
      "viewport.gizmo",
  }};
  static constexpr std::array<HostPanelDescriptor, 1> kDescriptors{{
      {"viewport_settings", "Viewport Settings", kViewportSettingsGroups},
  }};
  return kDescriptors;
}

bool is_transform_tool(const quader::ToolDescriptor& tool)
{
	return tool.kind == quader::ToolKind::Transform;
}

bool is_modal_tool(const quader::ToolDescriptor& tool)
{
	return tool.kind == quader::ToolKind::Modal;
}

std::vector<HostToolDescriptor> build_tool_catalog()
{
	std::vector<HostToolDescriptor> descriptors;
        const std::span<const quader::ToolDescriptor> tools =
            quader::viewport_tool_descriptors();
        descriptors.reserve(tools.size());
	for (const quader::ToolDescriptor& tool : tools) {
		descriptors.push_back({
			tool.id,
			tool.label,
			tool.shortcut,
			is_transform_tool(tool),
			is_modal_tool(tool),
		});
	}
	return descriptors;
}

HostShortcutDescriptor
shortcut(std::string_view id, HostShortcutContext context, std::string_view key,
         bool primary_modifier, bool alt, bool shift,
         HostShortcutTargetKind target_kind, std::string_view target_id,
         bool alternate = false,
         HostShortcutRegistrationPolicy registration_policy =
             HostShortcutRegistrationPolicy::QuaderLocal) {
  return {
      id,    context,     key,       primary_modifier, alt,
      shift, target_kind, target_id, alternate,        registration_policy,
  };
}

std::span<const HostShortcutDescriptor> shortcut_descriptors()
{
  static const std::array<HostShortcutDescriptor, 39> kDescriptors{{
      shortcut("undo", HostShortcutContext::Global, "Z", true, false, false,
               HostShortcutTargetKind::EditorAction, "undo"),
      shortcut("redo_shift_z", HostShortcutContext::Global, "Z", true, false,
               true, HostShortcutTargetKind::EditorAction, "redo"),
      shortcut("new_scene", HostShortcutContext::Global, "N", true, false,
               false, HostShortcutTargetKind::EditorAction, "new_scene"),
      shortcut("open_scene", HostShortcutContext::Global, "O", true, false,
               false, HostShortcutTargetKind::EditorAction, "open_scene"),
      shortcut("save_scene", HostShortcutContext::Global, "S", true, false,
               false, HostShortcutTargetKind::EditorAction, "save_scene"),
      shortcut("save_scene_as", HostShortcutContext::Global, "S", true, true,
               false, HostShortcutTargetKind::EditorAction, "save_scene_as"),
      shortcut("save_all_scenes", HostShortcutContext::Global, "S", true,
               false, true, HostShortcutTargetKind::EditorAction,
               "save_all_scenes"),
      shortcut("close_scene", HostShortcutContext::Global, "W", true, false,
               false, HostShortcutTargetKind::EditorAction, "close_scene"),
      shortcut("next_scene", HostShortcutContext::Global, "Tab", true, false,
               false, HostShortcutTargetKind::EditorAction, "next_scene"),
      shortcut("previous_scene", HostShortcutContext::Global, "Tab", true,
               false, true, HostShortcutTargetKind::EditorAction,
               "previous_scene"),
      shortcut("select_all", HostShortcutContext::Global, "A", true, false,
               false, HostShortcutTargetKind::EditorAction, "select_all"),
      shortcut("copy_meshes", HostShortcutContext::Global, "C", true, false,
               false, HostShortcutTargetKind::EditorAction, "copy_meshes"),
      shortcut("duplicate_selection", HostShortcutContext::Global, "D", true,
               false, false, HostShortcutTargetKind::EditorAction,
               "duplicate_selection"),
      shortcut("invert_selection", HostShortcutContext::Global, "I", true,
               false, false, HostShortcutTargetKind::EditorAction,
               "invert_selection"),
      shortcut("repeat_last_action", HostShortcutContext::Global, "R", true,
               false, false, HostShortcutTargetKind::EditorAction,
               "repeat_last_action"),
      shortcut("paste_meshes", HostShortcutContext::Global, "V", true, false,
               false, HostShortcutTargetKind::EditorAction, "paste_meshes"),
      shortcut("delete", HostShortcutContext::Global, "Delete", false, false,
               false, HostShortcutTargetKind::EditorAction, "delete"),

      shortcut("merge_vertices", HostShortcutContext::Viewport, "M", false,
               false, false, HostShortcutTargetKind::Action, "merge_vertices"),
      shortcut("merge_vertices_at_center", HostShortcutContext::Viewport, "M",
               false, false, true, HostShortcutTargetKind::Action,
               "merge_vertices_at_center", true),
      shortcut("apply_active_material", HostShortcutContext::Viewport, "T",
               false, false, true, HostShortcutTargetKind::Action,
               "apply_active_material"),
      shortcut("focus_all_home", HostShortcutContext::Viewport, "Home", false,
               false, false, HostShortcutTargetKind::ViewportAction,
               "focus_all"),
      shortcut("focus_origin", HostShortcutContext::Viewport, "Home", false,
               false, true, HostShortcutTargetKind::ViewportAction,
               "focus_origin"),
      shortcut("insert_edge_loop_tool", HostShortcutContext::Viewport, "R",
               false, false, true, HostShortcutTargetKind::Action,
               "insert_edge_loop_tool"),
      shortcut("mirror_axis_x", HostShortcutContext::Viewport, "X", false,
               false, true, HostShortcutTargetKind::ViewportAction,
               "mirror_axis_x"),
      shortcut("mirror_axis_y", HostShortcutContext::Viewport, "Y", false,
               false, true, HostShortcutTargetKind::ViewportAction,
               "mirror_axis_y"),
      shortcut("mirror_axis_z", HostShortcutContext::Viewport, "Z", false,
               false, true, HostShortcutTargetKind::ViewportAction,
               "mirror_axis_z"),
      shortcut("flip_vertical", HostShortcutContext::Viewport, "V", false,
               false, true, HostShortcutTargetKind::ViewportAction,
               "flip_vertical"),
      shortcut("create_primitive_tool", HostShortcutContext::Viewport, "B",
               false, false, true, HostShortcutTargetKind::Tool,
               "create_primitive"),
      shortcut("select_tool", HostShortcutContext::Viewport, "Q", false, false,
               false, HostShortcutTargetKind::Tool, "select"),
      shortcut("knife_tool", HostShortcutContext::Viewport, "C", false, false,
               false, HostShortcutTargetKind::Action, "knife_tool"),
      shortcut("flip_mesh_normals", HostShortcutContext::Viewport, "F", false,
               false, false, HostShortcutTargetKind::Action, "invert_mesh"),
      shortcut("move_tool", HostShortcutContext::Viewport, "W", false, false,
               false, HostShortcutTargetKind::Tool, "move"),
      shortcut("rotate_tool", HostShortcutContext::Viewport, "R", false, false,
               false, HostShortcutTargetKind::Tool, "rotate"),
      shortcut("scale_tool", HostShortcutContext::Viewport, "S", false, false,
               false, HostShortcutTargetKind::Tool, "scale"),
      shortcut("vertex_selection", HostShortcutContext::Viewport, "1", false,
               false, false, HostShortcutTargetKind::SelectionMode, "vertex"),
      shortcut("edge_selection", HostShortcutContext::Viewport, "2", false,
               false, false, HostShortcutTargetKind::SelectionMode, "edge"),
      shortcut("face_selection", HostShortcutContext::Viewport, "3", false,
               false, false, HostShortcutTargetKind::SelectionMode, "face"),
      shortcut("object_selection", HostShortcutContext::Viewport, "4", false,
               false, false, HostShortcutTargetKind::SelectionMode, "object"),
      shortcut("group_selection", HostShortcutContext::Viewport, "5", false,
               false, false, HostShortcutTargetKind::SelectionMode, "group"),
  }};
  return kDescriptors;
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
		bool suppresses_gizmo,
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
		suppresses_gizmo,
		returns_to_select_on_cancel,
		returns_to_select_on_success,
		rebuilds_mesh_on_success,
	};
}

std::span<const HostGestureDescriptor> gesture_descriptors()
{
  static constexpr std::array<std::string_view, 6> kModalTools{{
      "create_primitive",
      "insert_edge_loop",
      "knife",
      "cut",
      "mirror",
      "poly",
  }};
  static constexpr std::array<std::string_view, 3> kTransformTools{{
      "move",
      "rotate",
      "scale",
  }};
  static constexpr std::array<std::string_view, 1> kSelectionTool{{
      "select",
  }};
  static constexpr std::array<std::string_view, 1> kCreatePrimitiveTool{{
      "create_primitive",
  }};
  static constexpr std::array<std::string_view, 1> kInsertEdgeLoopTool{{
      "insert_edge_loop",
  }};
  static constexpr std::array<std::string_view, 1> kNifeTool{{
      "knife",
  }};
  static constexpr std::array<std::string_view, 1> kCutTool{{
      "cut",
  }};
  static constexpr std::array<std::string_view, 1> kPolyTool{{
      "poly",
  }};

  static constexpr std::array<HostPointerInputKind, 4> kHoverPrimaryDrag{{
      HostPointerInputKind::MouseMove,
      HostPointerInputKind::PrimaryDown,
      HostPointerInputKind::PrimaryDrag,
      HostPointerInputKind::PrimaryUp,
  }};
  static constexpr std::array<HostPointerInputKind, 3> kPrimaryDrag{{
      HostPointerInputKind::PrimaryDown,
      HostPointerInputKind::PrimaryDrag,
      HostPointerInputKind::PrimaryUp,
  }};
  static constexpr std::array<HostPointerInputKind, 3> kPrimaryPreviewBlock{{
      HostPointerInputKind::PrimaryDown,
      HostPointerInputKind::PrimaryDrag,
      HostPointerInputKind::PrimaryUp,
  }};
  static constexpr std::array<HostPointerInputKind, 1> kMouseMove{{
      HostPointerInputKind::MouseMove,
  }};
  static constexpr std::array<HostPointerInputKind, 1> kScroll{{
      HostPointerInputKind::Scroll,
  }};
  static constexpr std::array<HostPointerInputKind, 3> kSecondaryDrag{{
      HostPointerInputKind::SecondaryDown,
      HostPointerInputKind::SecondaryDrag,
      HostPointerInputKind::SecondaryUp,
  }};
  static constexpr std::array<HostPointerInputKind, 3> kMiddleDrag{{
      HostPointerInputKind::MiddleDown,
      HostPointerInputKind::MiddleDrag,
      HostPointerInputKind::MiddleUp,
  }};

  static constexpr std::array<std::string_view, 1> kCancelEscape{{
      "Escape",
  }};
  static constexpr std::array<std::string_view, 4> kNifeKeys{{
      "Escape",
      "Space",
      "Return",
      "KeypadEnter",
  }};
  static constexpr std::array<std::string_view, 2> kFinalizeKeys{{
      "Escape",
      "Space",
  }};
  static constexpr std::array<std::string_view, 3> kCutKeys{{
      "Escape",
      "Space",
      "F",
  }};
  static constexpr std::array<std::string_view, 2> kPreviewKeys{{
      "Escape",
      "Space",
  }};
  static constexpr std::array<std::string_view, 1> kFlyKeys{{
      "Escape",
  }};
  static constexpr std::array<std::string_view, 1> kLayoutKeys{{
      "Space",
  }};

  static constexpr std::array<std::string_view, 2> kSnapAxisModifiers{{
      "primary_modifier:bypass_snap",
      "shift:constrain_axis",
  }};
  static constexpr std::array<std::string_view, 4> kNifeMeanings{{
      "primary_modifier:bypass_snap",
      "shift:constrain_axis",
      "Space:finalize",
      "Return:finalize_and_start_new_operation",
  }};
  static constexpr std::array<std::string_view, 1> kBypassSnapModifier{{
      "primary_modifier:bypass_snap",
  }};
  static constexpr std::array<std::string_view, 2> kCutMeanings{{
      "F:cycle_keep_mode",
      "Space:finalize",
  }};
  static constexpr std::array<std::string_view, 3> kPolyMeanings{{
      "primary_modifier:bypass_snap",
      "shift:constrain_axis",
      "Space:finalize",
  }};
  static constexpr std::array<std::string_view, 4> kSelectionModifiers{{
      "primary_modifier:toggle_selection",
      "shift:extend_selection",
      "platform_modifier:multi_select",
      "alt:alternate_pick",
  }};
  static constexpr std::array<std::string_view, 3> kTransformModifiers{{
      "shift:duplicate_object_selection",
      "shift:extrude_or_inset_elements",
      "primary_modifier:transform_snap_modifier",
  }};
  static constexpr std::array<std::string_view, 1> kScrollModifiers{{
      "primary_modifier:zoom_to_cursor",
  }};
  static constexpr std::array<std::string_view, 2> kMiddleModifiers{{
      "shift:pan",
      "alt:orbit_snap_modifier",
  }};
  static const std::array<HostGestureDescriptor, 15> kDescriptors{{
      gesture("active_modal_tool_precedence", "Active Modal Tool Precedence",
              HostGestureTargetKind::InputPolicy,
              "active_modal_tool_precedence", 0, kModalTools, {}, {}, {}, true,
              false, false, false, true, true, false, false, false),
      gesture("create_primitive_gesture", "Create Primitive Gesture",
              HostGestureTargetKind::Tool, "create_primitive", 10,
              kCreatePrimitiveTool,
              kHoverPrimaryDrag, kCancelEscape, {}, true, false, false, false,
              true, true, false, false, true),
      gesture("insert_edge_loop_gesture", "Insert Edge Loop Gesture",
              HostGestureTargetKind::Tool, "insert_edge_loop", 20,
              kInsertEdgeLoopTool, kHoverPrimaryDrag, kCancelEscape,
              kBypassSnapModifier, true, false, false, false, true, true, true,
              true, true),
      gesture("knife_tool_gesture", "Knife Tool Gesture",
              HostGestureTargetKind::Tool, "knife", 30, kNifeTool,
              kHoverPrimaryDrag, kNifeKeys, kNifeMeanings, true, false, false,
              false, true, true, true, true, true),
      gesture("cut_tool_gesture", "Cut Tool Gesture",
              HostGestureTargetKind::Tool, "cut", 40, kCutTool,
              kHoverPrimaryDrag, kCutKeys, kCutMeanings, true, false, false,
              false, true, true, true, true, true),
      gesture("poly_tool_gesture", "Poly Tool Gesture",
              HostGestureTargetKind::Tool, "poly", 50, kPolyTool,
              kHoverPrimaryDrag, kFinalizeKeys, kPolyMeanings, true, false,
              false, false, true, true, true, true, true),
      gesture("active_preview_pointer_block", "Active Preview Pointer Block",
              HostGestureTargetKind::PreviewAction,
              "active_preview_pointer_block", 60, {}, kPrimaryPreviewBlock, {},
              {}, false, true, false, true, true, true, false, false, false),
      gesture("transform_gizmo_drag", "Transform Gizmo Drag",
              HostGestureTargetKind::ToolGroup, "transform_tools", 70,
              kTransformTools, kPrimaryDrag, kCancelEscape, kTransformModifiers,
              false, false, true, true, true, false, false, false, true),
      gesture("selection_pick_and_paint", "Selection Pick And Paint",
              HostGestureTargetKind::Tool, "select", 80, kSelectionTool,
              kHoverPrimaryDrag, {}, kSelectionModifiers, false, false, true,
              true, true, false, false, false, false),
      gesture("modifier_hover_refresh", "Modifier Hover Refresh",
              HostGestureTargetKind::InputPolicy, "modifier_hover_refresh", 90,
              kModalTools, kMouseMove, {}, kSnapAxisModifiers, false, false,
              false, true, false, true, false, false, false),
      gesture("viewport_scroll_zoom", "Viewport Scroll Zoom",
              HostGestureTargetKind::ViewportAction, "zoom", 100, {}, kScroll,
              {}, kScrollModifiers, false, false, false, false, true, false,
              false, false, false),
      gesture("viewport_fly_or_orthographic_pan",
              "Viewport Fly Or Orthographic Pan",
              HostGestureTargetKind::ViewportAction, "fly_or_orthographic_pan",
              110, {}, kSecondaryDrag, kFlyKeys, {}, false, false, false, false,
              true, false, false, false, false),
      gesture("viewport_orbit_or_pan", "Viewport Orbit Or Pan",
              HostGestureTargetKind::ViewportAction, "orbit_or_pan", 120, {},
              kMiddleDrag, {}, kMiddleModifiers, false, false, false, false,
              true, false, false, false, false),
      gesture("active_preview_commit_or_cancel",
              "Active Preview Commit Or Cancel",
              HostGestureTargetKind::PreviewAction,
              "active_preview_commit_or_cancel", 130, {}, {}, kPreviewKeys, {},
              false, true, false, true, false, true, false, false, true),
      gesture("focused_pane_maximize_toggle", "Focused Pane Maximize Toggle",
              HostGestureTargetKind::ViewportAction,
              "toggle_focused_pane_maximized", 140, {}, {}, kLayoutKeys, {},
              false, false, false, true, false, false, false, false, false),
  }};
  return kDescriptors;
}

constexpr int checked_overlay_layer_index(quader_editor_core::SelectionOverlayLayer layer)
{
	using Layer = quader_editor_core::SelectionOverlayLayer;
	switch (layer) {
        case Layer::SelectedVisibleFace:
          return 0;
        case Layer::SelectedFaceEdge:
          return 1;
        case Layer::UnselectedVertex:
          return 2;
        case Layer::SelectedVertex:
          return 3;
        case Layer::HoverFace:
          return 4;
        case Layer::HoverFaceEdge:
          return 5;
        case Layer::HoverEdge:
          return 6;
        case Layer::HoverVertex:
          return 7;
        case Layer::SelectedEdge:
          return 8;
        case Layer::SelectedXrayFace:
          return 9;
        case Layer::SelectedFaceMask:
          return 10;
        case Layer::SourceWire:
          return 11;
        case Layer::OpenEdge:
          return 12;
        case Layer::SnapTargetVertex:
          return 13;
        case Layer::DiagnosticEdge:
          return 14;
        case Layer::SourceWireDepthStamp:
          return -1;
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

constexpr HostColorDescriptor
color_from_viewport_setting(quader::ViewportSettingColor color) {
  return color_rgba(color.r, color.g, color.b, color.a);
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
        descriptor.value_kind = HostPreferenceValueKind::Integer;
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
        descriptor.value_kind = HostPreferenceValueKind::Floating;
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
        descriptor.scope = HostPreferenceScope::Operation;
        descriptor.value_kind = HostPreferenceValueKind::Boolean;
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
        descriptor.scope = HostPreferenceScope::ViewportStyle;
        descriptor.value_kind = HostPreferenceValueKind::ColorRgba;
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
        descriptor.scope = HostPreferenceScope::Operation;
        descriptor.value_kind = HostPreferenceValueKind::Choice;
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

HostPreferenceDescriptor operation_preference(
		const quader_editor_core::EditorOperationOptionDescriptor& schema)
{
	switch (schema.value_kind) {
        case quader_editor_core::EditorOperationOptionValueKind::Integer:
          return int_preference(schema.preference_id, schema.label,
                                HostPreferenceScope::Operation,
                                schema.default_integer, schema.minimum_integer,
                                schema.maximum_integer, schema.integer_step,
                                schema.preference_group_id, schema.operation_id,
                                schema.option_id, schema.hard_limits);
        case quader_editor_core::EditorOperationOptionValueKind::Floating:
          return float_preference(
              schema.preference_id, schema.label,
              HostPreferenceScope::Operation, schema.default_number,
              schema.minimum_number, schema.maximum_number, schema.number_step,
              schema.preference_group_id, schema.operation_id, schema.option_id,
              {}, schema.hard_limits);
        case quader_editor_core::EditorOperationOptionValueKind::Boolean:
          return bool_preference(
              schema.preference_id, schema.label, schema.default_bool,
              schema.preference_group_id, schema.operation_id, schema.option_id,
              schema.visible);
	}
	return {};
}

std::vector<HostPreferenceDescriptor> build_preference_descriptors()
{
  static const std::array<HostPreferenceChoiceDescriptor, 3> kCutModeChoices{{
      {"keep_back", "Discard Left",
       quader_editor_core::cut_mode_id(quader_editor_core::CutMode::KeepBack),
       false},
      {"keep_front", "Discard Right",
       quader_editor_core::cut_mode_id(quader_editor_core::CutMode::KeepFront),
       false},
      {"keep_both", "Keep Both",
       quader_editor_core::cut_mode_id(quader_editor_core::CutMode::KeepBoth),
       true},
  }};

  std::vector<HostPreferenceDescriptor> descriptors;
  descriptors.reserve(49);
  for (const quader_editor_core::EditorOperationOptionDescriptor &option :
       quader_editor_core::operation_option_descriptor_catalog()) {
    descriptors.push_back(operation_preference(option));
  }
  descriptors.push_back(choice_preference("operation.cut.keep_mode",
                                          "Keep Mode", kCutModeChoices,
                                          "operation.cut"));

  descriptors.push_back(color_preference(
      "viewport.scene.background_color", "Background",
      color_rgba(quader::kDefaultSceneClearColorChannel,
                 quader::kDefaultSceneClearColorChannel,
                 quader::kDefaultSceneClearColorChannel, 1.0),
      "viewport.scene"));
  descriptors.push_back(color_preference("viewport.grid.minor_color", "Minor",
                                         color_from_viewport_setting(
                                             quader::kDefaultViewportGridMinorColor),
                                         "viewport.grid"));
  descriptors.push_back(color_preference("viewport.grid.major_color", "Major",
                                         color_from_viewport_setting(
                                             quader::kDefaultViewportGridMajorColor),
                                         "viewport.grid"));
  descriptors.push_back(
      color_preference("viewport.grid.x_axis_color", "X QAxis",
                       color_from_viewport_setting(
                           quader::kDefaultViewportGridXAxisColor),
                       "viewport.grid"));
  descriptors.push_back(
      color_preference("viewport.grid.y_axis_color", "Y QAxis",
                       color_from_viewport_setting(
                           quader::kDefaultViewportGridYAxisColor),
                       "viewport.grid"));
  descriptors.push_back(
      color_preference("viewport.grid.z_axis_color", "Z QAxis",
                       color_from_viewport_setting(
                           quader::kDefaultViewportGridZAxisColor),
                       "viewport.grid"));
  descriptors.push_back(
      color_preference("viewport.selection.face_color", "Face Fill",
                       color_from_abgr(0x081facffU), "viewport.selection",
                       "selected_visible_face"));
  descriptors.push_back(color_preference(
      "viewport.selection.wire_color", "Wire", color_from_abgr(0xff1fd3ffU),
      "viewport.selection", "selected_face_edge"));
  descriptors.push_back(color_preference(
      "viewport.selection.bounding_box_wire_color", "Bounding Box Wire Color",
      color_from_abgr(0xff00e6ffU), "viewport.selection"));
  descriptors.push_back(color_preference(
      "viewport.overlay.source_wire_color", "Source Wire",
      color_from_abgr(0xebffe05cU), "viewport.overlay", "source_wire"));
  descriptors.push_back(color_preference(
      "viewport.overlay.open_edge_color", "Open Edge",
      color_from_abgr(0xff00ffffU), "viewport.overlay", "open_edge"));
  descriptors.push_back(color_preference(
      "viewport.overlay.diagnostic_edge_color", "Diagnostic Edge",
      color_from_abgr(0xff0000ffU), "viewport.overlay", "diagnostic_edge"));
  descriptors.push_back(color_preference(
      "viewport.overlay.hover_face_color", "Hover Face",
      color_from_abgr(0x161fff66U), "viewport.overlay", "hover_face"));
  descriptors.push_back(color_preference(
      "viewport.overlay.hover_wire_color", "Hover Wire",
      color_from_abgr(0xff00ff51U), "viewport.overlay", "hover_edge"));
  descriptors.push_back(color_preference(
      "viewport.overlay.remove_selection_face_color", "Remove Face",
      color_from_abgr(0x52ff6b1fU), "viewport.overlay", "hover_face"));
  descriptors.push_back(color_preference(
      "viewport.overlay.remove_selection_wire_color", "Remove Wire",
      color_from_abgr(0xffff6b1fU), "viewport.overlay", "hover_edge"));
  descriptors.push_back(color_preference(
      "viewport.vertices.vertex_color", "Vertex", color_from_abgr(0xffffe66bU),
      "viewport.vertices", "unselected_vertex"));
  descriptors.push_back(color_preference(
      "viewport.vertices.selected_vertex_color", "Selected Vertex",
      color_from_abgr(0xff1fadffU), "viewport.vertices", "selected_vertex"));
  descriptors.push_back(color_preference(
      "viewport.vertices.hover_vertex_color", "Hover Vertex",
      color_from_abgr(0xff38ff00U), "viewport.vertices", "hover_vertex"));
  descriptors.push_back(color_preference(
      "viewport.vertices.remove_selection_vertex_color", "Remove Vertex",
      color_from_abgr(0xffff6b1fU), "viewport.vertices", "hover_vertex"));
  descriptors.push_back(
      float_preference("viewport.grid.minor_line_size", "Minor Line",
                       HostPreferenceScope::ViewportStyle,
                       quader::kDefaultViewportGridMinorLineSize, 0.25, 8.0,
                       0.0,
                       "viewport.grid"));
  descriptors.push_back(
      float_preference("viewport.grid.major_line_size", "Major Line",
                       HostPreferenceScope::ViewportStyle,
                       quader::kDefaultViewportGridMajorLineSize, 0.25, 8.0,
                       0.0,
                       "viewport.grid"));
  descriptors.push_back(
      float_preference("viewport.grid.axis_line_size", "QAxis Line",
                       HostPreferenceScope::ViewportStyle,
                       quader::kDefaultViewportGridAxisLineSize, 0.25, 8.0,
                       0.0,
                       "viewport.grid"));
  descriptors.push_back(float_preference(
      "viewport.overlay.source_wire_line_size", "Source Wire Size",
      HostPreferenceScope::ViewportStyle, 1.5, 0.25, 8.0, 0.0,
      "viewport.overlay", {}, {}, "source_wire"));
  descriptors.push_back(float_preference(
      "viewport.selection.face_wire_line_size", "Face Wire Size",
      HostPreferenceScope::ViewportStyle, 2.5, 0.25, 8.0, 0.0,
      "viewport.selection", {}, {}, "selected_face_edge"));
  descriptors.push_back(
      float_preference("viewport.selection.edge_line_size", "Edge Line Size",
                       HostPreferenceScope::ViewportStyle, 2.5, 0.25, 8.0, 0.0,
                       "viewport.selection", {}, {}, "selected_edge"));
  descriptors.push_back(float_preference(
      "viewport.overlay.hover_wire_line_size", "Hover Wire Size",
      HostPreferenceScope::ViewportStyle, 2.5, 0.25, 8.0, 0.0,
      "viewport.overlay", {}, {}, "hover_edge"));
  descriptors.push_back(
      float_preference("viewport.overlay.open_edge_line_size", "Open Edge Size",
                       HostPreferenceScope::ViewportStyle, 1.0, 0.25, 8.0, 0.0,
                       "viewport.overlay", {}, {}, "open_edge"));
  descriptors.push_back(float_preference(
      "viewport.overlay.diagnostic_edge_line_size", "Diagnostic Edge Size",
      HostPreferenceScope::ViewportStyle, 4.0, 0.25, 8.0, 0.0,
      "viewport.overlay", {}, {}, "diagnostic_edge"));
  descriptors.push_back(
      float_preference("viewport.vertices.vertex_size", "Vertex Size",
                       HostPreferenceScope::ViewportStyle, 7.0, 2.0, 32.0, 0.0,
                       "viewport.vertices", {}, {}, "unselected_vertex"));
  descriptors.push_back(
      float_preference("viewport.vertices.selected_growth", "Selected Growth",
                       HostPreferenceScope::ViewportStyle, 0.5, 0.0, 12.0, 0.0,
                       "viewport.vertices", {}, {}, "selected_vertex"));
  descriptors.push_back(
      float_preference("viewport.vertices.hover_growth", "Hover Growth",
                       HostPreferenceScope::ViewportStyle, 0.5, 0.0, 12.0, 0.0,
                       "viewport.vertices", {}, {}, "hover_vertex"));
  descriptors.push_back(
      float_preference("viewport.vertices.outline_size", "Outline Size",
                       HostPreferenceScope::ViewportStyle, 1.0, 0.0, 12.0, 0.0,
                       "viewport.vertices"));
  descriptors.push_back(
      float_preference("viewport.picking.vertex_radius", "Vertex Radius",
                       HostPreferenceScope::ViewportPicking, 0.3, 0.001, 2.0,
                       0.0, "viewport.picking"));
  descriptors.push_back(
      float_preference("viewport.picking.edge_radius", "Edge Radius",
                       HostPreferenceScope::ViewportPicking, 0.2, 0.001, 2.0,
                       0.0, "viewport.picking"));
  descriptors.push_back(float_preference(
      "viewport.gizmo.size", "Size", HostPreferenceScope::ViewportInteraction,
      1.0, 0.5, 2.0, 0.0, "viewport.gizmo"));
  return descriptors;
}

std::span<const HostPreferenceDescriptor> preference_descriptors()
{
  static const std::vector<HostPreferenceDescriptor> kDescriptors =
      build_preference_descriptors();
  return {kDescriptors.data(), kDescriptors.size()};
}

/**
 * Owns stable preference ID spans while building host preference group descriptors.
 */
struct HostPreferenceGroupStorage {
	std::vector<std::vector<std::string_view>> preference_ids;
	std::vector<HostPreferenceGroupDescriptor> descriptors;
};

void add_preference_group(
		HostPreferenceGroupStorage& storage,
		std::string_view id,
		std::string_view label,
		HostPreferenceScope scope,
		std::vector<std::string_view> preference_ids)
{
	storage.preference_ids.push_back(std::move(preference_ids));
	const std::vector<std::string_view>& ids = storage.preference_ids.back();
	storage.descriptors.push_back({
		id,
		label,
		scope,
		{ids.data(), ids.size()},
	});
}

HostPreferenceGroupStorage build_preference_group_descriptors()
{
	HostPreferenceGroupStorage storage;

	/**
	 * Collects operation-scoped preference IDs before storage-backed group descriptors are emitted.
	 */
	struct OperationPreferenceGroup {
		std::string_view id;
		std::string_view label;
		std::vector<std::string_view> preference_ids;
	};
	std::vector<OperationPreferenceGroup> operation_groups;
	for (const quader_editor_core::EditorOperationOptionDescriptor& option :
			quader_editor_core::operation_option_descriptor_catalog()) {
		auto found = std::find_if(
			operation_groups.begin(),
			operation_groups.end(),
			[&](const OperationPreferenceGroup& group) {
				return group.id == option.preference_group_id;
			});
		if (found == operation_groups.end()) {
			operation_groups.push_back(OperationPreferenceGroup{option.preference_group_id, option.operation_label, {}});
			found = std::prev(operation_groups.end());
		}
		found->preference_ids.push_back(option.preference_id);
	}
	for (OperationPreferenceGroup& group : operation_groups) {
          add_preference_group(storage, group.id, group.label,
                               HostPreferenceScope::Operation,
                               std::move(group.preference_ids));
        }
        add_preference_group(storage, "operation.cut", "Cut",
                             HostPreferenceScope::Operation,
                             {"operation.cut.keep_mode"});

        static constexpr std::array<std::string_view, 1>
            kViewportScenePreferences{{
                "viewport.scene.background_color",
            }};
        static constexpr std::array<std::string_view, 8>
            kViewportGridPreferences{{
                "viewport.grid.minor_color",
                "viewport.grid.major_color",
                "viewport.grid.x_axis_color",
                "viewport.grid.y_axis_color",
                "viewport.grid.z_axis_color",
                "viewport.grid.minor_line_size",
                "viewport.grid.major_line_size",
                "viewport.grid.axis_line_size",
            }};
        static constexpr std::array<std::string_view, 5>
            kViewportSelectionPreferences{{
                "viewport.selection.face_color",
                "viewport.selection.wire_color",
                "viewport.selection.bounding_box_wire_color",
                "viewport.selection.face_wire_line_size",
                "viewport.selection.edge_line_size",
            }};
        static constexpr std::array<std::string_view, 11>
            kViewportOverlayPreferences{{
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
        static constexpr std::array<std::string_view, 8>
            kViewportVertexPreferences{{
                "viewport.vertices.vertex_color",
                "viewport.vertices.selected_vertex_color",
                "viewport.vertices.hover_vertex_color",
                "viewport.vertices.remove_selection_vertex_color",
                "viewport.vertices.vertex_size",
                "viewport.vertices.selected_growth",
                "viewport.vertices.hover_growth",
                "viewport.vertices.outline_size",
            }};
        static constexpr std::array<std::string_view, 2>
            kViewportPickingPreferences{{
                "viewport.picking.vertex_radius",
                "viewport.picking.edge_radius",
            }};
        static constexpr std::array<std::string_view, 1>
            kViewportGizmoPreferences{{
                "viewport.gizmo.size",
            }};

        add_preference_group(
            storage, "viewport.scene", "Scene",
            HostPreferenceScope::ViewportStyle,
            std::vector<std::string_view>(kViewportScenePreferences.begin(),
                                          kViewportScenePreferences.end()));
        add_preference_group(
            storage, "viewport.grid", "Grid",
            HostPreferenceScope::ViewportStyle,
            std::vector<std::string_view>(kViewportGridPreferences.begin(),
                                          kViewportGridPreferences.end()));
        add_preference_group(
            storage, "viewport.selection", "Selection",
            HostPreferenceScope::ViewportStyle,
            std::vector<std::string_view>(kViewportSelectionPreferences.begin(),
                                          kViewportSelectionPreferences.end()));
        add_preference_group(
            storage, "viewport.overlay", "Overlay",
            HostPreferenceScope::ViewportStyle,
            std::vector<std::string_view>(kViewportOverlayPreferences.begin(),
                                          kViewportOverlayPreferences.end()));
        add_preference_group(
            storage, "viewport.vertices", "Vertices",
            HostPreferenceScope::ViewportStyle,
            std::vector<std::string_view>(kViewportVertexPreferences.begin(),
                                          kViewportVertexPreferences.end()));
        add_preference_group(
            storage, "viewport.picking", "Picking",
            HostPreferenceScope::ViewportPicking,
            std::vector<std::string_view>(kViewportPickingPreferences.begin(),
                                          kViewportPickingPreferences.end()));
        add_preference_group(
            storage, "viewport.gizmo", "Gizmo",
            HostPreferenceScope::ViewportInteraction,
            std::vector<std::string_view>(kViewportGizmoPreferences.begin(),
                                          kViewportGizmoPreferences.end()));
        return storage;
}

std::span<const HostPreferenceGroupDescriptor> preference_group_descriptors()
{
  static const HostPreferenceGroupStorage kStorage =
      build_preference_group_descriptors();
  return {kStorage.descriptors.data(), kStorage.descriptors.size()};
}

std::span<const std::string_view> overlay_draw_order_layer_ids()
{
  static constexpr std::array<std::string_view, 15> kIds{{
      "selected_visible_face",
      "selected_face_mask",
      "selected_xray_face",
      "hover_face",
      "source_wire",
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
  return kIds;
}

std::span<const std::string_view> blocking_preview_override_layer_ids()
{
  static constexpr std::array<std::string_view, 4> kIds{{
      "selected_face_edge",
      "selected_edge",
      "source_wire",
      "open_edge",
  }};
  return kIds;
}

std::span<const std::string_view> transient_hover_mask_writer_layer_ids()
{
  static constexpr std::array<std::string_view, 4> kIds{{
      "hover_face",
      "hover_face_edge",
      "hover_edge",
      "hover_vertex",
  }};
  return kIds;
}

std::span<const std::string_view> transient_hover_suppressed_layer_ids()
{
  static constexpr std::array<std::string_view, 6> kIds{{
      "selected_visible_face",
      "selected_xray_face",
      "selected_face_mask",
      "selected_face_edge",
      "selected_edge",
      "selected_vertex",
  }};
  return kIds;
}

std::span<const HostOverlayRenderPolicyDescriptor> overlay_render_policy_descriptors()
{
  static constexpr std::array<HostOverlayRenderPolicyDescriptor, 5>
      kDescriptors{{
          {"depth_tested_fill", HostOverlayGeometryKind::Triangles, 0.035, 0.0,
           false},
          {"depth_tested_line", HostOverlayGeometryKind::Lines, 0.02, 1.0,
           false},
          {"depth_tested_point", HostOverlayGeometryKind::Points, 0.01, 0.0,
           false},
          {"draw_on_top_line", HostOverlayGeometryKind::Lines, 0.0, 1.0, true},
          {"draw_on_top_point", HostOverlayGeometryKind::Points, 0.0, 0.0,
           true},
      }};
  return kDescriptors;
}

std::span<const HostMaterialTextureAssetDescriptor> material_texture_asset_descriptors()
{
  static constexpr std::array<HostMaterialTextureAssetDescriptor, 2>
      kDescriptors{{
          {quader::kDefaultMaterialAlbedoTextureId,
           quader::kDefaultMaterialAlbedoTextureName,
           HostMaterialTextureRole::BaseColor,
           quader::kDefaultMaterialAlbedoTexturePackagePath},
          {quader::kDefaultMaterialRoughnessTextureId,
           quader::kDefaultMaterialRoughnessTextureName,
           HostMaterialTextureRole::Roughness,
           quader::kDefaultMaterialRoughnessTexturePackagePath},
      }};
  return kDescriptors;
}

std::span<const HostMaterialGeneratedTextureDescriptor> material_generated_texture_descriptors()
{
  static constexpr std::array<HostMaterialGeneratedTextureDescriptor, 2>
      kDescriptors{{
          {
              "default_metallic_smoothness",
              "Default Metallic Smoothness",
              HostMaterialTextureRole::MetallicSmoothness,
              "default_roughness",
              true,
              HostMaterialTextureChannel::Alpha,
              0.0,
              0.0,
              0.0,
              1.0,
          },
          {
              "default_packed_mask",
              "Default Packed Mask",
              HostMaterialTextureRole::Mask,
              "default_roughness",
              true,
              HostMaterialTextureChannel::Alpha,
              0.0,
              1.0,
              0.0,
              1.0,
          },
      }};
  return kDescriptors;
}

std::span<const HostMaterialScalarDescriptor> default_material_scalars()
{
  static constexpr std::array<HostMaterialScalarDescriptor, 7> kDescriptors{{
      {HostMaterialScalarKind::Metallic, 0.0},
      {HostMaterialScalarKind::Smoothness, 1.0},
      {HostMaterialScalarKind::SmoothnessTextureChannel, 0.0},
      {HostMaterialScalarKind::SmoothnessRemapMin, 0.0},
      {HostMaterialScalarKind::SmoothnessRemapMax, 1.0},
      {HostMaterialScalarKind::AmbientOcclusionRemapMin, 1.0},
      {HostMaterialScalarKind::AmbientOcclusionRemapMax, 1.0},
  }};
  return kDescriptors;
}

std::span<const HostMaterialRecipeDescriptor> material_recipe_descriptors()
{
  static constexpr std::array<std::string_view, 2> kTextureAssets{{
      quader::kDefaultMaterialAlbedoTextureId,
      quader::kDefaultMaterialRoughnessTextureId,
  }};
  static constexpr std::array<std::string_view, 2> kGeneratedTextures{{
      "default_metallic_smoothness",
      "default_packed_mask",
  }};
  static const std::array<HostMaterialRecipeDescriptor, 1> kDescriptors{{
      {
          "default_material",
          "Quader Default Material",
          color_rgba(1.0, 1.0, 1.0, 1.0),
          kTextureAssets,
          kGeneratedTextures,
          default_material_scalars(),
          {1.0, -1.0},
          {0.0, 1.0},
      },
  }};
  return kDescriptors;
}

constexpr double kHostDefaultOrthographicSize = 24.0;
constexpr double kHostFocusOriginDistance = 64.0;
constexpr double kHostDefaultFovDegrees = 60.0;
constexpr double kHostDefaultGridMapUnits = 32.0;

} // namespace

std::span<const HostOperationDescriptor> operation_catalog()
{
  static const std::vector<HostOperationDescriptor> kDescriptors =
      build_operation_catalog();
  return kDescriptors;
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
  static const std::vector<HostToolDescriptor> kDescriptors =
      build_tool_catalog();
  return kDescriptors;
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
  static const std::array<HostSelectionModeDescriptor, 5> kDescriptors{{
      {HostSelectionMode::Vertex, "vertex", "Vertex"},
      {HostSelectionMode::Edge, "edge", "Edge"},
      {HostSelectionMode::Face, "face", "Face"},
      {HostSelectionMode::Object, "object", "Object"},
      {HostSelectionMode::Group, "group", "Group"},
  }};
  return kDescriptors;
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
  static const std::array<HostTransformAxisDescriptor, 7> kDescriptors{{
      {quader_editor_core::axis_id(quader_editor_core::TransformAxis::X), "X",
       false},
      {quader_editor_core::axis_id(quader_editor_core::TransformAxis::Y), "Y",
       false},
      {quader_editor_core::axis_id(quader_editor_core::TransformAxis::Z), "Z",
       false},
      {quader_editor_core::axis_id(quader_editor_core::TransformAxis::Xy), "XY",
       true},
      {quader_editor_core::axis_id(quader_editor_core::TransformAxis::Xz), "XZ",
       true},
      {quader_editor_core::axis_id(quader_editor_core::TransformAxis::Yz), "YZ",
       true},
      {quader_editor_core::axis_id(quader_editor_core::TransformAxis::All),
       "All", false},
  }};
  return kDescriptors;
}

std::span<const HostViewportViewDescriptor> viewport_view_catalog()
{
  static const std::array<HostViewportViewDescriptor, 7> kDescriptors{{
      {"perspective",
       "Perspective",
       false,
       {6.0, 5.0, 8.0},
       {},
       {0.0, 1.0, 0.0}},
      {"top",
       "Top",
       true,
       {0.0, kHostFocusOriginDistance, 0.0},
       {},
       {0.0, 0.0, -1.0}},
      {"bottom",
       "Bottom",
       true,
       {0.0, -kHostFocusOriginDistance, 0.0},
       {},
       {0.0, 0.0, 1.0}},
      {"front",
       "Front",
       true,
       {0.0, 0.0, kHostFocusOriginDistance},
       {},
       {0.0, 1.0, 0.0}},
      {"back",
       "Back",
       true,
       {0.0, 0.0, -kHostFocusOriginDistance},
       {},
       {0.0, 1.0, 0.0}},
      {quader::viewport_view_stable_id(quader::ViewportView::Side),
       quader::viewport_view_label(quader::ViewportView::Side),
       true,
       {kHostFocusOriginDistance, 0.0, 0.0},
       {},
       {0.0, 1.0, 0.0}},
      {"left",
       "Left",
       true,
       {-kHostFocusOriginDistance, 0.0, 0.0},
       {},
       {0.0, 1.0, 0.0}},
  }};
  return kDescriptors;
}

std::span<const HostGridPresetDescriptor> grid_preset_catalog()
{
  constexpr double kUnit = quader_editor_core::kDefaultMapUnitsPerWorldUnit;
  static const std::array<HostGridPresetDescriptor, 10> kDescriptors{{
      {"", 1.0, 1.0 / kUnit, false},
      {"", 2.0, 2.0 / kUnit, false},
      {"", 4.0, 4.0 / kUnit, false},
      {"", 8.0, 8.0 / kUnit, false},
      {"", 16.0, 16.0 / kUnit, false},
      {"", 32.0, 32.0 / kUnit, true},
      {"", 64.0, 64.0 / kUnit, false},
      {"", 128.0, 128.0 / kUnit, false},
      {"", 256.0, 256.0 / kUnit, false},
      {"", 512.0, 512.0 / kUnit, false},
  }};
  return kDescriptors;
}

HostViewportDefaultsDescriptor viewport_defaults()
{
	HostViewportDefaultsDescriptor descriptor;
        descriptor.map_units_per_world_unit =
            quader_editor_core::kDefaultMapUnitsPerWorldUnit;
        descriptor.default_grid_map_units = kHostDefaultGridMapUnits;
        descriptor.default_orthographic_size = kHostDefaultOrthographicSize;
        descriptor.focus_origin_distance = kHostFocusOriginDistance;
        descriptor.default_fov_degrees = kHostDefaultFovDegrees;
        descriptor.orbit_radians_per_pixel =
            quader_editor_core::kNavigationOrbitRadiansPerPixel;
        descriptor.fly_look_radians_per_pixel =
            quader_editor_core::kNavigationFlyLookRadiansPerPixel;
        descriptor.fly_base_speed = quader_editor_core::kNavigationFlyBaseSpeed;
        descriptor.fly_fast_multiplier =
            quader_editor_core::kNavigationFlyFastMultiplier;
        descriptor.fly_slow_multiplier =
            quader_editor_core::kNavigationFlySlowMultiplier;
        descriptor.selection_box_drag_threshold_pixels =
            quader_editor_core::kSelectionBoxDefaultDragThreshold;
        return descriptor;
}

std::span<const HostOverlayLayerDescriptor> overlay_layer_catalog()
{
	using Layer = quader_editor_core::SelectionOverlayLayer;
        static const std::array<HostOverlayLayerDescriptor, 15> kDescriptors{{
            overlay_layer(Layer::SelectedVisibleFace, "selected_visible_face",
                          "Selected Visible Face", true, false, false),
            overlay_layer(Layer::SelectedFaceEdge, "selected_face_edge",
                          "Selected Face Edge", false, true, false, true),
            overlay_layer(Layer::UnselectedVertex, "unselected_vertex",
                          "Unselected Vertex", false, false, true),
            overlay_layer(Layer::SelectedVertex, "selected_vertex",
                          "Selected Vertex", false, false, true),
            overlay_layer(Layer::HoverFace, "hover_face", "Hover Face", true,
                          false, false),
            overlay_layer(Layer::HoverFaceEdge, "hover_face_edge",
                          "Hover Face Edge", false, true, false, true),
            overlay_layer(Layer::HoverEdge, "hover_edge", "Hover Edge", false,
                          true, false, true),
            overlay_layer(Layer::HoverVertex, "hover_vertex", "Hover Vertex",
                          false, false, true),
            overlay_layer(Layer::SelectedEdge, "selected_edge", "Selected Edge",
                          false, true, false, true),
            overlay_layer(Layer::SelectedXrayFace, "selected_xray_face",
                          "Selected Xray Face", true, false, false),
            overlay_layer(Layer::SelectedFaceMask, "selected_face_mask",
                          "Selected Face Mask", true, false, false),
            overlay_layer(Layer::SourceWire, "source_wire", "Source Wire",
                          false, true, false, true),
            overlay_layer(Layer::OpenEdge, "open_edge", "Open Edge", false,
                          true, false),
            overlay_layer(Layer::SnapTargetVertex, "snap_target_vertex",
                          "Snap Target Vertex", false, false, true, false,
                          true),
            overlay_layer(Layer::DiagnosticEdge, "diagnostic_edge",
                          "Diagnostic Edge", false, true, false, true),
        }};
        return kDescriptors;
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
	descriptor.suppress_gizmo_for_blocking_preview = true;
	descriptor.suppress_gizmo_for_modal_tool = true;
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

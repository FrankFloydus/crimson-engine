////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace quader_host_contract {

/**
 * Enumerates HostOptionValueKind values used by the editor layer.
 */
enum class HostOptionValueKind {
	integer,
	floating,
	boolean,
};

/**
 * Stores the Host Option Descriptor data contract used by the editor metadata catalog.
 */
struct HostOptionDescriptor {
	std::string_view id;
	std::string_view label;
	HostOptionValueKind value_kind = HostOptionValueKind::integer;
	int default_integer = 0;
	int minimum_integer = 0;
	int maximum_integer = 0;
	int integer_step = 1;
	double default_number = 0.0;
	double minimum_number = 0.0;
	double maximum_number = 0.0;
	double number_step = 0.01;
	bool default_bool = false;
	bool visible = true;
	bool hard_limits = true;
};

/**
 * Stores the Host Operation Descriptor data contract used by the editor metadata catalog.
 */
struct HostOperationDescriptor {
	std::string_view id;
	std::string_view label;
	bool supports_preview = false;
	std::span<const HostOptionDescriptor> options;
};

/**
 * Enumerates HostSelectionMode values used by the editor layer.
 */
enum class HostSelectionMode {
	vertex,
	edge,
	face,
	object,
	group,
};

/**
 * Enumerates HostActiveSelectionKind values used by the editor layer.
 */
enum class HostActiveSelectionKind {
	none,
	object,
	vertex,
	edge,
	face,
};

/**
 * Enumerates HostActionKind values used by the editor layer.
 */
enum class HostActionKind {
	operation,
	tool,
};

/**
 * Enumerates HostPreviewKind values used by the editor layer.
 */
enum class HostPreviewKind {
	none,
	bridge,
	edge_bevel,
	thicken,
	slice_quad,
	merge_by_distance,
};

/**
 * Stores the Host Action Availability Descriptor data contract used by the editor metadata catalog.
 */
struct HostActionAvailabilityDescriptor {
	HostSelectionMode selection_mode = HostSelectionMode::object;
	std::uint32_t minimum_selected_vertices = 0;
	std::uint32_t minimum_selected_edges = 0;
	std::uint32_t exact_selected_edges = 0;
	std::uint32_t minimum_selected_faces = 0;
	std::uint32_t minimum_selected_objects = 0;
	HostActiveSelectionKind required_active_kind = HostActiveSelectionKind::none;
	bool requires_single_object = false;
};

/**
 * Stores the Host Action Descriptor data contract used by the editor metadata catalog.
 */
struct HostActionDescriptor {
	std::string_view id;
	std::string_view label;
	HostActionKind kind = HostActionKind::operation;
	std::string_view operation_id;
	std::string_view tool_id;
	HostPreviewKind preview_kind = HostPreviewKind::none;
	bool show_in_operations_panel = true;
	HostActionAvailabilityDescriptor availability;
};

/**
 * Stores the Host Operation Panel Descriptor data contract used by the editor metadata catalog.
 */
struct HostOperationPanelDescriptor {
	std::string_view id;
	std::string_view label;
	HostSelectionMode selection_mode = HostSelectionMode::object;
	std::span<const std::string_view> action_ids;
	bool fallback_for_unknown_selection = false;
};

/**
 * Enumerates HostUiSurfaceKind values used by the editor layer.
 */
enum class HostUiSurfaceKind {
	menu,
	toolbar,
	panel,
};

/**
 * Stores the Host UI Surface Descriptor data contract used by the editor metadata catalog.
 */
struct HostUiSurfaceDescriptor {
	std::string_view id;
	std::string_view label;
	HostUiSurfaceKind kind = HostUiSurfaceKind::toolbar;
	bool floating = false;
	bool selection_contextual = false;
};

/**
 * Stores the Host UI Group Descriptor data contract used by the editor metadata catalog.
 */
struct HostUiGroupDescriptor {
	std::string_view id;
	std::string_view label;
};

/**
 * Enumerates HostUiPlacementTargetKind values used by the editor layer.
 */
enum class HostUiPlacementTargetKind {
	action,
	tool,
	selection_mode,
	editor_action,
	viewport_action,
	panel,
	preference_group,
};

/**
 * Enumerates HostUiVisibilityCondition values used by the editor layer.
 */
enum class HostUiVisibilityCondition {
	always,
	bridge_preview_active,
	edge_bevel_preview_active,
	thicken_preview_active,
	slice_quad_preview_active,
	merge_by_distance_preview_active,
	cut_tool_active,
};

/**
 * Stores the Host UI Placement Descriptor data contract used by the editor metadata catalog.
 */
struct HostUiPlacementDescriptor {
	std::string_view id;
	std::string_view surface_id;
	std::string_view group_id;
	std::string_view label;
	HostUiPlacementTargetKind target_kind = HostUiPlacementTargetKind::action;
	std::string_view target_id;
	std::uint32_t order = 0;
	bool separator_before = false;
	HostUiVisibilityCondition visibility = HostUiVisibilityCondition::always;
};

/**
 * Enumerates HostInteractionKind values used by the editor layer.
 */
enum class HostInteractionKind {
	editor_action,
	viewport_action,
	preview_action,
	input_policy,
};

/**
 * Stores the Host Interaction Descriptor data contract used by the editor metadata catalog.
 */
struct HostInteractionDescriptor {
	std::string_view id;
	std::string_view label;
	HostInteractionKind kind = HostInteractionKind::editor_action;
};

/**
 * Stores the Host Tool Group Descriptor data contract used by the editor metadata catalog.
 */
struct HostToolGroupDescriptor {
	std::string_view id;
	std::string_view label;
	std::span<const std::string_view> tool_ids;
};

/**
 * Stores the Host Panel Descriptor data contract used by the editor metadata catalog.
 */
struct HostPanelDescriptor {
	std::string_view id;
	std::string_view label;
	std::span<const std::string_view> preference_group_ids;
};

/**
 * Stores the Host Tool Descriptor data contract used by the editor metadata catalog.
 */
struct HostToolDescriptor {
	std::string_view id;
	std::string_view label;
	std::string_view primary_shortcut;
	bool transform_tool = false;
	bool modal_tool = false;
};

/**
 * Stores the Host Selection Mode Descriptor data contract used by the editor metadata catalog.
 */
struct HostSelectionModeDescriptor {
	HostSelectionMode mode = HostSelectionMode::object;
	std::string_view id;
	std::string_view label;
};

/**
 * Enumerates HostShortcutContext values used by the editor layer.
 */
enum class HostShortcutContext {
	global,
	viewport,
};

/**
 * Enumerates HostShortcutTargetKind values used by the editor layer.
 */
enum class HostShortcutTargetKind {
	action,
	tool,
	selection_mode,
	editor_action,
	viewport_action,
};

/**
 * Enumerates HostShortcutRegistrationPolicy values used by the editor layer.
 */
enum class HostShortcutRegistrationPolicy {
	quader_local,
	host_global,
};

/**
 * Stores the Host Shortcut Descriptor data contract used by the editor metadata catalog.
 */
struct HostShortcutDescriptor {
	std::string_view id;
	HostShortcutContext context = HostShortcutContext::global;
	std::string_view key;
	bool primary_modifier = false;
	bool alt = false;
	bool shift = false;
	HostShortcutTargetKind target_kind = HostShortcutTargetKind::editor_action;
	std::string_view target_id;
	bool alternate = false;
	HostShortcutRegistrationPolicy registration_policy = HostShortcutRegistrationPolicy::quader_local;
};

/**
 * Enumerates HostPointerInputKind values used by the editor layer.
 */
enum class HostPointerInputKind {
	mouse_move,
	primary_down,
	primary_drag,
	primary_up,
	secondary_down,
	secondary_drag,
	secondary_up,
	middle_down,
	middle_drag,
	middle_up,
	scroll,
};

/**
 * Enumerates HostGestureTargetKind values used by the editor layer.
 */
enum class HostGestureTargetKind {
	tool,
	tool_group,
	viewport_action,
	preview_action,
	input_policy,
};

/**
 * Stores the Host Gesture Descriptor data contract used by the editor metadata catalog.
 */
struct HostGestureDescriptor {
	std::string_view id;
	std::string_view label;
	HostGestureTargetKind target_kind = HostGestureTargetKind::input_policy;
	std::string_view target_id;
	std::uint32_t priority = 0;
	std::span<const std::string_view> tool_ids;
	std::span<const HostPointerInputKind> pointer_inputs;
	std::span<const std::string_view> key_inputs;
	std::span<const std::string_view> modifier_meanings;
	bool requires_active_tool = false;
	bool requires_active_preview = false;
	bool blocked_by_active_preview = false;
	bool suppressed_while_fly_navigation = false;
	bool consumes_pointer_when_active = false;
	bool suppresses_gizmo_and_measurements = false;
	bool returns_to_select_on_cancel = false;
	bool returns_to_select_on_success = false;
	bool rebuilds_mesh_on_success = false;
};

/**
 * Stores the Host Transform Axis Descriptor data contract used by the editor metadata catalog.
 */
struct HostTransformAxisDescriptor {
	std::string_view id;
	std::string_view label;
	bool planar = false;
};

/**
 * Stores the Host Vector3 Descriptor data contract used by the editor metadata catalog.
 */
struct HostVector3Descriptor {
	double x = 0.0;
	double y = 0.0;
	double z = 0.0;
};

/**
 * Stores the Host Viewport View Descriptor data contract used by the editor metadata catalog.
 */
struct HostViewportViewDescriptor {
	std::string_view id;
	std::string_view label;
	bool orthographic = false;
	HostVector3Descriptor position;
	HostVector3Descriptor target;
	HostVector3Descriptor up;
};

/**
 * Stores the Host Grid Preset Descriptor data contract used by the editor metadata catalog.
 */
struct HostGridPresetDescriptor {
	std::string_view shortcut;
	double map_units = 1.0;
	double world_units = 1.0;
	bool default_preset = false;
};

/**
 * Stores the Host Viewport Defaults Descriptor data contract used by the editor metadata catalog.
 */
struct HostViewportDefaultsDescriptor {
	double map_units_per_world_unit = 32.0;
	double default_grid_map_units = 32.0;
	double default_orthographic_size = 24.0;
	double focus_origin_distance = 64.0;
	double default_fov_degrees = 60.0;
	double orbit_radians_per_pixel = 0.006;
	double fly_look_radians_per_pixel = 0.004;
	double fly_base_speed = 6.0;
	double fly_fast_multiplier = 4.0;
	double fly_slow_multiplier = 0.25;
	double selection_box_drag_threshold_pixels = 4.0;
};

/**
 * Stores the Host Overlay Layer Descriptor data contract used by the editor metadata catalog.
 */
struct HostOverlayLayerDescriptor {
	int stable_index = 0;
	std::string_view id;
	std::string_view label;
	bool triangles = false;
	bool lines = false;
	bool points = false;
	bool line_draws_on_top = false;
	bool point_draws_on_top = false;
	bool depth_tested_by_default = true;
};

/**
 * Stores the Host Color Descriptor data contract used by the editor metadata catalog.
 */
struct HostColorDescriptor {
	double r = 0.0;
	double g = 0.0;
	double b = 0.0;
	double a = 1.0;
};

/**
 * Enumerates HostOverlayGeometryKind values used by the editor layer.
 */
enum class HostOverlayGeometryKind {
	triangles,
	lines,
	points,
};

/**
 * Stores the Host Overlay Render Policy Descriptor data contract used by the editor metadata catalog.
 */
struct HostOverlayRenderPolicyDescriptor {
	std::string_view id;
	HostOverlayGeometryKind geometry_kind = HostOverlayGeometryKind::lines;
	double depth_bias = 0.0;
	double feather_pixels = 0.0;
	bool draws_on_top = false;
};

/**
 * Stores the Host Overlay Presentation Descriptor data contract used by the editor metadata catalog.
 */
struct HostOverlayPresentationDescriptor {
	bool suppress_gizmo_and_measurements_for_blocking_preview = true;
	bool suppress_gizmo_and_measurements_for_modal_tool = true;
	HostColorDescriptor blocking_preview_wire_color;
	std::span<const std::string_view> blocking_preview_override_layer_ids;
	std::span<const std::string_view> draw_order_layer_ids;
	std::span<const std::string_view> transient_hover_mask_writer_layer_ids;
	std::span<const std::string_view> transient_hover_suppressed_layer_ids;
	std::span<const HostOverlayRenderPolicyDescriptor> render_policies;
};

/**
 * Enumerates HostPreferenceValueKind values used by the editor layer.
 */
enum class HostPreferenceValueKind {
	integer,
	floating,
	boolean,
	color_rgba,
	choice,
};

/**
 * Enumerates HostPreferenceScope values used by the editor layer.
 */
enum class HostPreferenceScope {
	operation,
	viewport_style,
	viewport_picking,
	viewport_interaction,
};

/**
 * Stores the Host Preference Choice Descriptor data contract used by the editor metadata catalog.
 */
struct HostPreferenceChoiceDescriptor {
	std::string_view id;
	std::string_view label;
	int value = 0;
	bool default_choice = false;
};

/**
 * Stores the Host Preference Descriptor data contract used by the editor metadata catalog.
 */
struct HostPreferenceDescriptor {
	std::string_view id;
	std::string_view label;
	HostPreferenceScope scope = HostPreferenceScope::viewport_style;
	HostPreferenceValueKind value_kind = HostPreferenceValueKind::floating;
	double default_number = 0.0;
	double minimum_number = 0.0;
	double maximum_number = 0.0;
	double number_step = 0.0;
	int default_integer = 0;
	int minimum_integer = 0;
	int maximum_integer = 0;
	int integer_step = 1;
	bool default_bool = false;
	HostColorDescriptor default_color;
	std::span<const HostPreferenceChoiceDescriptor> choices;
	std::string_view group_id;
	std::string_view related_operation_id;
	std::string_view related_option_id;
	std::string_view related_overlay_layer_id;
	bool visible = true;
	bool hard_limits = true;
};

/**
 * Stores the Host Preference Group Descriptor data contract used by the editor metadata catalog.
 */
struct HostPreferenceGroupDescriptor {
	std::string_view id;
	std::string_view label;
	HostPreferenceScope scope = HostPreferenceScope::viewport_style;
	std::span<const std::string_view> preference_ids;
};

/**
 * Enumerates HostMaterialTextureRole values used by the editor layer.
 */
enum class HostMaterialTextureRole {
	base_color,
	roughness,
	metallic_smoothness,
	mask,
};

/**
 * Enumerates HostMaterialTextureChannel values used by the editor layer.
 */
enum class HostMaterialTextureChannel {
	red,
	green,
	blue,
	alpha,
};

/**
 * Enumerates HostMaterialScalarKind values used by the editor layer.
 */
enum class HostMaterialScalarKind {
	metallic,
	smoothness,
	smoothness_texture_channel,
	smoothness_remap_min,
	smoothness_remap_max,
	ambient_occlusion_remap_min,
	ambient_occlusion_remap_max,
};

/**
 * Stores the Host Vector2 Descriptor data contract used by the editor metadata catalog.
 */
struct HostVector2Descriptor {
	double x = 0.0;
	double y = 0.0;
};

/**
 * Stores the Host Material Texture Asset Descriptor data contract used by the editor metadata catalog.
 */
struct HostMaterialTextureAssetDescriptor {
	std::string_view id;
	std::string_view label;
	HostMaterialTextureRole role = HostMaterialTextureRole::base_color;
	std::string_view package_relative_path;
};

/**
 * Stores the Host Material Generated Texture Descriptor data contract used by the editor metadata catalog.
 */
struct HostMaterialGeneratedTextureDescriptor {
	std::string_view id;
	std::string_view label;
	HostMaterialTextureRole role = HostMaterialTextureRole::metallic_smoothness;
	std::string_view source_texture_asset_id;
	bool invert_source_grayscale = false;
	HostMaterialTextureChannel target_channel = HostMaterialTextureChannel::alpha;
	double constant_red = 0.0;
	double constant_green = 0.0;
	double constant_blue = 0.0;
	double constant_alpha = 1.0;
};

/**
 * Stores the Host Material Scalar Descriptor data contract used by the editor metadata catalog.
 */
struct HostMaterialScalarDescriptor {
	HostMaterialScalarKind kind = HostMaterialScalarKind::metallic;
	double value = 0.0;
};

/**
 * Stores the Host Material Recipe Descriptor data contract used by the editor metadata catalog.
 */
struct HostMaterialRecipeDescriptor {
	std::string_view id;
	std::string_view label;
	HostColorDescriptor base_color;
	std::span<const std::string_view> texture_asset_ids;
	std::span<const std::string_view> generated_texture_ids;
	std::span<const HostMaterialScalarDescriptor> scalars;
	HostVector2Descriptor texture_scale;
	HostVector2Descriptor texture_offset;
};

[[nodiscard]] std::span<const HostOperationDescriptor> operation_catalog();
[[nodiscard]] const HostOperationDescriptor *find_operation_descriptor(std::string_view operation_id);
[[nodiscard]] std::span<const HostActionDescriptor> action_catalog();
[[nodiscard]] const HostActionDescriptor *find_action_descriptor(std::string_view action_id);
[[nodiscard]] std::span<const HostOperationPanelDescriptor> operation_panel_catalog();
[[nodiscard]] const HostOperationPanelDescriptor *find_operation_panel_descriptor(std::string_view panel_id);
[[nodiscard]] std::span<const HostUiSurfaceDescriptor> ui_surface_catalog();
[[nodiscard]] const HostUiSurfaceDescriptor *find_ui_surface_descriptor(std::string_view surface_id);
[[nodiscard]] std::span<const HostUiPlacementDescriptor> ui_placement_catalog();
[[nodiscard]] const HostUiPlacementDescriptor *find_ui_placement_descriptor(std::string_view placement_id);
[[nodiscard]] std::span<const HostUiGroupDescriptor> ui_group_catalog();
[[nodiscard]] const HostUiGroupDescriptor *find_ui_group_descriptor(std::string_view group_id);
[[nodiscard]] std::span<const HostInteractionDescriptor> interaction_catalog();
[[nodiscard]] const HostInteractionDescriptor *find_interaction_descriptor(std::string_view interaction_id);
[[nodiscard]] std::span<const HostToolGroupDescriptor> tool_group_catalog();
[[nodiscard]] const HostToolGroupDescriptor *find_tool_group_descriptor(std::string_view group_id);
[[nodiscard]] std::span<const HostPanelDescriptor> panel_catalog();
[[nodiscard]] const HostPanelDescriptor *find_panel_descriptor(std::string_view panel_id);
[[nodiscard]] std::span<const HostToolDescriptor> tool_catalog();
[[nodiscard]] const HostToolDescriptor *find_tool_descriptor(std::string_view tool_id);
[[nodiscard]] std::span<const HostSelectionModeDescriptor> selection_mode_catalog();
[[nodiscard]] const HostSelectionModeDescriptor *find_selection_mode_descriptor(std::string_view selection_mode_id);
[[nodiscard]] std::span<const HostShortcutDescriptor> shortcut_catalog();
[[nodiscard]] std::span<const HostGestureDescriptor> gesture_catalog();
[[nodiscard]] const HostGestureDescriptor *find_gesture_descriptor(std::string_view gesture_id);
[[nodiscard]] std::span<const HostTransformAxisDescriptor> transform_axis_catalog();
[[nodiscard]] std::span<const HostViewportViewDescriptor> viewport_view_catalog();
[[nodiscard]] std::span<const HostGridPresetDescriptor> grid_preset_catalog();
[[nodiscard]] HostViewportDefaultsDescriptor viewport_defaults();
[[nodiscard]] std::span<const HostOverlayLayerDescriptor> overlay_layer_catalog();
[[nodiscard]] const HostOverlayLayerDescriptor *find_overlay_layer_descriptor(int stable_index);
[[nodiscard]] std::span<const HostPreferenceDescriptor> preference_catalog();
[[nodiscard]] const HostPreferenceDescriptor *find_preference_descriptor(std::string_view preference_id);
[[nodiscard]] std::span<const HostPreferenceGroupDescriptor> preference_group_catalog();
[[nodiscard]] const HostPreferenceGroupDescriptor *find_preference_group_descriptor(std::string_view group_id);
[[nodiscard]] HostOverlayPresentationDescriptor overlay_presentation();
[[nodiscard]] std::span<const HostMaterialTextureAssetDescriptor> material_texture_asset_catalog();
[[nodiscard]] const HostMaterialTextureAssetDescriptor *find_material_texture_asset_descriptor(std::string_view texture_asset_id);
[[nodiscard]] std::span<const HostMaterialGeneratedTextureDescriptor> material_generated_texture_catalog();
[[nodiscard]] const HostMaterialGeneratedTextureDescriptor *find_material_generated_texture_descriptor(std::string_view texture_id);
[[nodiscard]] std::span<const HostMaterialRecipeDescriptor> material_recipe_catalog();
[[nodiscard]] const HostMaterialRecipeDescriptor *find_material_recipe_descriptor(std::string_view recipe_id);


} // namespace quader_host_contract

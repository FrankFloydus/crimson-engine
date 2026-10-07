////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modes/editor_mode_policy.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace quader_host_contract {

/**
 * Enumerates HostOptionValueKind values used by the editor layer.
 */
enum class HostOptionValueKind {
  Integer,
  Floating,
  Boolean,
};

/**
 * Stores the Host Option Descriptor data contract used by the editor metadata catalog.
 */
struct HostOptionDescriptor {
	std::string_view id;
	std::string_view label;
        HostOptionValueKind value_kind = HostOptionValueKind::Integer;
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
  Vertex,
  Edge,
  Face,
  Object,
  Group,
};

/**
 * Enumerates HostActiveSelectionKind values used by the editor layer.
 */
enum class HostActiveSelectionKind {
  None,
  Object,
  Vertex,
  Edge,
  Face,
};

/**
 * Enumerates HostActionKind values used by the editor layer.
 */
enum class HostActionKind {
  Operation,
  Tool,
};

/**
 * Enumerates HostPreviewKind values used by the editor layer.
 */
enum class HostPreviewKind {
  None,
  Bridge,
  EdgeBevel,
  Thicken,
  SliceQuad,
  MergeByDistance,
  HalfCut,
};

/**
 * Stores the Host Action Availability Descriptor data contract used by the editor metadata catalog.
 */
struct HostActionAvailabilityDescriptor {
  HostSelectionMode selection_mode = HostSelectionMode::Object;
  std::uint32_t minimum_selected_vertices = 0;
  std::uint32_t minimum_selected_edges = 0;
  std::uint32_t exact_selected_edges = 0;
  std::uint32_t minimum_selected_faces = 0;
  std::uint32_t minimum_selected_objects = 0;
  HostActiveSelectionKind required_active_kind = HostActiveSelectionKind::None;
  bool requires_single_object = false;
};

/**
 * Stores the Host Action Descriptor data contract used by the editor metadata catalog.
 */
struct HostActionDescriptor {
	std::string_view id;
	std::string_view label;
        HostActionKind kind = HostActionKind::Operation;
        std::string_view operation_id;
	std::string_view tool_id;
        HostPreviewKind preview_kind = HostPreviewKind::None;
        bool show_in_operations_panel = true;
	HostActionAvailabilityDescriptor availability;
        quader::EditorModeSupport supported_editor_modes = quader::EditorModeSupport::Edit;
};

/**
 * Stores the Host Operation Panel Descriptor data contract used by the editor metadata catalog.
 */
struct HostOperationPanelDescriptor {
	std::string_view id;
	std::string_view label;
        HostSelectionMode selection_mode = HostSelectionMode::Object;
        std::span<const std::string_view> action_ids;
	bool fallback_for_unknown_selection = false;
};

/**
 * Enumerates HostUiSurfaceKind values used by the editor layer.
 */
enum class HostUiSurfaceKind {
  Menu,
  Toolbar,
  Panel,
};

/**
 * Stores the Host UI Surface Descriptor data contract used by the editor metadata catalog.
 */
struct HostUiSurfaceDescriptor {
	std::string_view id;
	std::string_view label;
        HostUiSurfaceKind kind = HostUiSurfaceKind::Toolbar;
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
  Action,
  Tool,
  SelectionMode,
  EditorAction,
  ViewportAction,
  Panel,
  PreferenceGroup,
};

/**
 * Enumerates HostUiVisibilityCondition values used by the editor layer.
 */
enum class HostUiVisibilityCondition {
  Always,
  BridgePreviewActive,
  EdgeBevelPreviewActive,
  ThickenPreviewActive,
  SliceQuadPreviewActive,
  MergeByDistancePreviewActive,
  HalfCutPreviewActive,
  CutToolActive,
};

/**
 * Stores the Host UI Placement Descriptor data contract used by the editor metadata catalog.
 */
struct HostUiPlacementDescriptor {
	std::string_view id;
	std::string_view surface_id;
	std::string_view group_id;
	std::string_view label;
        HostUiPlacementTargetKind target_kind =
            HostUiPlacementTargetKind::Action;
        std::string_view target_id;
	std::uint32_t order = 0;
	bool separator_before = false;
        HostUiVisibilityCondition visibility =
            HostUiVisibilityCondition::Always;
};

/**
 * Enumerates HostInteractionKind values used by the editor layer.
 */
enum class HostInteractionKind {
  EditorAction,
  ViewportAction,
  PreviewAction,
  InputPolicy,
};

/**
 * Stores the Host Interaction Descriptor data contract used by the editor metadata catalog.
 */
struct HostInteractionDescriptor {
	std::string_view id;
	std::string_view label;
        HostInteractionKind kind = HostInteractionKind::EditorAction;
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
  HostSelectionMode mode = HostSelectionMode::Object;
  std::string_view id;
  std::string_view label;
};

/**
 * Enumerates HostShortcutContext values used by the editor layer.
 */
enum class HostShortcutContext {
  Global,
  Viewport,
};

/**
 * Enumerates HostShortcutTargetKind values used by the editor layer.
 */
enum class HostShortcutTargetKind {
  Action,
  Tool,
  SelectionMode,
  EditorAction,
  ViewportAction,
};

/**
 * Enumerates HostShortcutRegistrationPolicy values used by the editor layer.
 */
enum class HostShortcutRegistrationPolicy {
  QuaderLocal,
  HostGlobal,
};

/**
 * Stores the Host Shortcut Descriptor data contract used by the editor metadata catalog.
 */
struct HostShortcutDescriptor {
	std::string_view id;
        HostShortcutContext context = HostShortcutContext::Global;
        std::string_view key;
	bool primary_modifier = false;
	bool alt = false;
	bool shift = false;
        HostShortcutTargetKind target_kind =
            HostShortcutTargetKind::EditorAction;
        std::string_view target_id;
	bool alternate = false;
        HostShortcutRegistrationPolicy registration_policy =
            HostShortcutRegistrationPolicy::QuaderLocal;
};

/**
 * Enumerates HostPointerInputKind values used by the editor layer.
 */
enum class HostPointerInputKind {
  MouseMove,
  PrimaryDown,
  PrimaryDrag,
  PrimaryUp,
  SecondaryDown,
  SecondaryDrag,
  SecondaryUp,
  MiddleDown,
  MiddleDrag,
  MiddleUp,
  Scroll,
};

/**
 * Enumerates HostGestureTargetKind values used by the editor layer.
 */
enum class HostGestureTargetKind {
  Tool,
  ToolGroup,
  ViewportAction,
  PreviewAction,
  InputPolicy,
};

/**
 * Stores the Host Gesture Descriptor data contract used by the editor metadata catalog.
 */
struct HostGestureDescriptor {
	std::string_view id;
	std::string_view label;
        HostGestureTargetKind target_kind = HostGestureTargetKind::InputPolicy;
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
	bool suppresses_gizmo = false;
	bool returns_to_select_on_cancel = false;
	bool returns_to_select_on_success = false;
	bool rebuilds_mesh_on_success = false;
};

/**
 * Stores the Host Transform QAxis Descriptor data contract used by the editor metadata catalog.
 */
struct HostTransformAxisDescriptor {
	std::string_view id;
	std::string_view label;
	bool planar = false;
};

/**
 * Stores the Host QVec3 Descriptor data contract used by the editor metadata catalog.
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
	double fly_base_speed = 25.0;
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
  Triangles,
  Lines,
  Points,
};

/**
 * Stores the Host Overlay Render Policy Descriptor data contract used by the editor metadata catalog.
 */
struct HostOverlayRenderPolicyDescriptor {
	std::string_view id;
        HostOverlayGeometryKind geometry_kind = HostOverlayGeometryKind::Lines;
        double depth_bias = 0.0;
	double feather_pixels = 0.0;
	bool draws_on_top = false;
};

/**
 * Stores the Host Overlay Presentation Descriptor data contract used by the editor metadata catalog.
 */
struct HostOverlayPresentationDescriptor {
	bool suppress_gizmo_for_blocking_preview = true;
	bool suppress_gizmo_for_modal_tool = true;
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
  Integer,
  Floating,
  Boolean,
  ColorRgba,
  Choice,
};

/**
 * Enumerates HostPreferenceScope values used by the editor layer.
 */
enum class HostPreferenceScope {
  Operation,
  ViewportStyle,
  ViewportPicking,
  ViewportInteraction,
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
        HostPreferenceScope scope = HostPreferenceScope::ViewportStyle;
        HostPreferenceValueKind value_kind = HostPreferenceValueKind::Floating;
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
        HostPreferenceScope scope = HostPreferenceScope::ViewportStyle;
        std::span<const std::string_view> preference_ids;
};

/**
 * Enumerates HostMaterialTextureRole values used by the editor layer.
 */
enum class HostMaterialTextureRole {
  BaseColor,
  Roughness,
  MetallicSmoothness,
  Mask,
};

/**
 * Enumerates HostMaterialTextureChannel values used by the editor layer.
 */
enum class HostMaterialTextureChannel {
  Red,
  Green,
  Blue,
  Alpha,
};

/**
 * Enumerates HostMaterialScalarKind values used by the editor layer.
 */
enum class HostMaterialScalarKind {
  Metallic,
  Smoothness,
  SmoothnessTextureChannel,
  SmoothnessRemapMin,
  SmoothnessRemapMax,
  AmbientOcclusionRemapMin,
  AmbientOcclusionRemapMax,
};

/**
 * Stores the Host QVec2 Descriptor data contract used by the editor metadata catalog.
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
        HostMaterialTextureRole role = HostMaterialTextureRole::BaseColor;
        std::string_view package_relative_path;
};

/**
 * Stores the Host Material Generated Texture Descriptor data contract used by the editor metadata catalog.
 */
struct HostMaterialGeneratedTextureDescriptor {
	std::string_view id;
	std::string_view label;
        HostMaterialTextureRole role =
            HostMaterialTextureRole::MetallicSmoothness;
        std::string_view source_texture_asset_id;
	bool invert_source_grayscale = false;
        HostMaterialTextureChannel target_channel =
            HostMaterialTextureChannel::Alpha;
        double constant_red = 0.0;
	double constant_green = 0.0;
	double constant_blue = 0.0;
	double constant_alpha = 1.0;
};

/**
 * Stores the Host Material Scalar Descriptor data contract used by the editor metadata catalog.
 */
struct HostMaterialScalarDescriptor {
  HostMaterialScalarKind kind = HostMaterialScalarKind::Metallic;
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

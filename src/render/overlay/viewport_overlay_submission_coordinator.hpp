////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/overlay/selected_mesh_outline_cache.hpp"

#include "render/overlay/curve_tool_overlay_cache.hpp"
#include "render/overlay/render_overlay_snapshot.hpp"
#include "render/overlay/viewport_overlay_style_resolver.hpp"
#include "render/overlay/viewport_scene_overlay_builder.hpp"
#include "render/viewport_overlay_frame.hpp"
#include "render/render_source_stamp.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace quader {

class ViewportSettings;
struct RenderToolPreviewMeshRecord;
struct RenderToolPreviewGroupMarkerRecord;
struct RenderToolPreviewCurveRecord;
struct RenderToolPreviewLightRecord;
struct RenderToolPreviewSuppressionSnapshot;
struct RenderSceneSnapshot;

/// An array owns its dimensions channel independently of selected-object dimensions.
enum class ViewportArrayDimensionsMode { Inactive, Hidden, Visible };

/** Source-owner facts that invalidate the shared component-selection frame. */
struct ViewportOverlaySelectionSourceStamp final {
  std::uintptr_t source_identity = 0u;
  std::uint64_t content_revision = 0u;
  std::uint64_t selection_revision = 0u;
  std::uint64_t scene_revision = 0u;
  std::uint64_t asset_revision = 0u;
  std::uint64_t local_visibility_revision = 0u;
  std::uint64_t overlay_signature = 0u;
  ViewportOverlaySelectionContext selection_context =
      ViewportOverlaySelectionContext::Any;

  friend bool operator==(const ViewportOverlaySelectionSourceStamp &,
                         const ViewportOverlaySelectionSourceStamp &) =
      default;
};

/** Lazily publishes the semantic component-selection frame on cache misses. */
class ViewportOverlaySelectionFrameProvider {
public:
  virtual ~ViewportOverlaySelectionFrameProvider() = default;
  [[nodiscard]] virtual ViewportOverlayFrame
  build_selection_overlay_frame() const = 0;
};

/** Typed scene-source facts that invalidate one retained pane overlay. */
struct ViewportOverlaySceneSubmissionStamp final {
  RenderSceneBindingKey scene_binding;
  std::uint64_t scene_revision = 0u;
  std::uint64_t asset_revision = 0u;
  std::uint64_t selection_revision = 0u;
  std::uint64_t visibility_revision = 0u;
  std::uint64_t local_visibility_revision = 0u;
  std::uint64_t transform_revision = 0u;
  std::uint64_t mesh_binding_revision = 0u;
  std::uint64_t render_component_revision = 0u;

  friend bool operator==(const ViewportOverlaySceneSubmissionStamp &,
                         const ViewportOverlaySceneSubmissionStamp &) =
      default;
};

/** One owner publication of pane-local semantic tool overlay state. */
struct ViewportOverlayDynamicToolSourceStamp final {
  std::uintptr_t source_identity = 0u;
  std::uint64_t publication_revision = 0u;

  friend bool operator==(const ViewportOverlayDynamicToolSourceStamp &,
                         const ViewportOverlayDynamicToolSourceStamp &) =
      default;
};

/** Compiles pane-local semantic tool state only after its typed stamp changes. */
class ViewportOverlayDynamicToolFrameProvider {
public:
  virtual ~ViewportOverlayDynamicToolFrameProvider() = default;
  [[nodiscard]] virtual ViewportOverlayFrame
  build_dynamic_tool_overlay_frame() const = 0;
};

/** Keys one pane's retained semantic overlay independently of camera pose. */
struct ViewportOverlayPersistentSubmissionStamp final {
  std::uint64_t selection_overlay_revision = 0u;
  ViewportOverlaySceneSubmissionStamp scene;
  std::uint64_t style_signature = 0u;
  float raster_scale = 1.0f;
  RenderOverlaySelectionMode scene_selection_mode =
      RenderOverlaySelectionMode::Object;
  ViewportOverlaySelectionContext style_selection_context =
      ViewportOverlaySelectionContext::Any;
  ViewportOverlayProjection projection =
      ViewportOverlayProjection::Perspective;
  bool selection_overlay_visible = false;
  bool selection_overlays_visible = false;
  bool scene_overlay_requested = false;
  bool wireframe_pane = false;
  bool point_light_gizmos_visible = false;
  bool selection_dimension_box_visible = false;

  friend bool operator==(const ViewportOverlayPersistentSubmissionStamp &,
                         const ViewportOverlayPersistentSubmissionStamp &) =
      default;
};

/** Radius handles keep a fixed pixel size as camera pose and projection change. */
struct ViewportOverlayPointLightRadiusSubmissionStamp final {
  ViewportOverlaySceneSubmissionStamp scene;
  std::array<float, 15u> camera_values{};
  ViewportOverlayProjection projection = ViewportOverlayProjection::Perspective;
  float raster_scale = 1.0f;
  bool active = false;

  friend bool operator==(const ViewportOverlayPointLightRadiusSubmissionStamp &,
                         const ViewportOverlayPointLightRadiusSubmissionStamp &) = default;
};

/** Screen-spaced dimension dashes use only the retained bounds and pane camera. */
struct ViewportOverlaySelectionDimensionSubmissionStamp final {
  std::uint64_t bounds_revision = 0u;
  std::array<float, 18u> camera_values{};
  ViewportOverlayProjection projection = ViewportOverlayProjection::Perspective;
  float raster_scale = 1.0f;
  bool active = false;

  friend bool operator==(const ViewportOverlaySelectionDimensionSubmissionStamp &,
                         const ViewportOverlaySelectionDimensionSubmissionStamp &) = default;
};

/** Keys the small frame-volatile stream separately from retained geometry. */
struct ViewportOverlayDynamicSubmissionStamp final {
  std::uint64_t mesh_outline_revision = 0u;
  ViewportOverlayPointLightRadiusSubmissionStamp point_light_radius;
  ViewportOverlaySelectionDimensionSubmissionStamp selection_dimensions;
  ViewportOverlayDynamicToolSourceStamp dynamic_tools;
  /** Identifies one volatile modeling-overlay publication without hashing its geometry. */
  struct VolatileModelingSourceStamp final {
    std::uintptr_t source_identity = 0u;
    std::uint64_t publication_revision = 0u;

    friend bool operator==(const VolatileModelingSourceStamp &,
                           const VolatileModelingSourceStamp &) = default;
  } volatile_modeling;
  ViewportOverlaySceneSubmissionStamp scene_tools;
  CurveToolOverlaySourceStamp curve;
  std::uintptr_t tool_preview_identity = 0u;
  std::uint64_t tool_preview_revision = 0u;
  std::uint64_t style_signature = 0u;
  float raster_scale = 1.0f;
  ViewportOverlaySelectionContext style_selection_context =
      ViewportOverlaySelectionContext::Any;
  ViewportOverlayProjection projection =
      ViewportOverlayProjection::Perspective;
  bool wireframe_pane = false;

  friend bool operator==(const ViewportOverlayDynamicSubmissionStamp &,
                         const ViewportOverlayDynamicSubmissionStamp &) =
      default;
};

using ViewportOverlayVolatileModelingSourceStamp =
    ViewportOverlayDynamicSubmissionStamp::VolatileModelingSourceStamp;

/** Reports which retained backend streams still exist after a camera update. */
struct ViewportOverlayCameraUpdate final {
  bool persistent_available = false;
  bool dynamic_available = false;
};

/** Render-backend endpoint used by the headless submission policy. */
class ViewportOverlaySubmissionTarget {
public:
  virtual ~ViewportOverlaySubmissionTarget() = default;

  [[nodiscard]] virtual ViewportOverlayCameraUpdate update_overlay_cameras(
      std::size_t viewport_index, std::uint8_t layer_mask,
      ViewportOverlayCamera camera) = 0;
  virtual void submit_persistent_overlay(
      std::size_t viewport_index, std::uint8_t layer_mask,
      ViewportOverlayFrame frame, ViewportOverlayCamera camera) = 0;
  virtual void submit_dynamic_overlay(
      std::size_t viewport_index, std::uint8_t layer_mask,
      ViewportOverlayFrame frame, ViewportOverlayCamera camera) = 0;
};

/** Semantic inputs for one pane's retained and volatile overlay streams. */
struct ViewportOverlaySubmissionRequest final {
  std::size_t viewport_index = 0u;
  std::uint8_t layer_mask = 0u;
  /** Combined app interface scale and this pane's native-surface DPR. */
  float raster_scale = 1.0f;
  /// Object whose selection and hover use faint wires without face tint.
  int construction_object_id = -1;
  ViewportOverlayCamera camera;
  ViewportOverlaySelectionSourceStamp selection_source;
  const ViewportOverlaySelectionFrameProvider *selection_frame_provider =
      nullptr;
  const RenderOverlaySelectionSnapshot *scene_overlay_snapshot = nullptr;
  /// Original bounds before source-replacing previews suppress render cages.
  const RenderOverlaySelectionSnapshot *selection_dimension_scene_snapshot = nullptr;
  /// Accepted preview payloads pinned by the bridge for this submission.
  std::span<const RenderToolPreviewMeshRecord> selection_dimension_preview_meshes;
  std::span<const RenderToolPreviewGroupMarkerRecord> selection_dimension_preview_groups;
  std::span<const RenderToolPreviewCurveRecord> selection_dimension_preview_curves;
  std::span<const RenderToolPreviewLightRecord> array_dimension_preview_lights;
  /// Visible originals for additive arrays; borrowed only during submit().
  const RenderSceneSnapshot *array_dimension_scene_snapshot = nullptr;
  const RenderToolPreviewSuppressionSnapshot *selection_dimension_preview_suppression = nullptr;
  /// Borrowed persistent mesh geometry; present only for whole-mesh outline selection.
  const RenderOverlaySelectedObjectSnapshot *selected_mesh_outline_snapshot = nullptr;
  bool gpu_mesh_outlines = false;
  const ViewportOverlayFrame *volatile_modeling_overlay_frame = nullptr;
  ViewportOverlayVolatileModelingSourceStamp volatile_modeling_source;
  /// Borrowed only for submit(); render owns all retained curve presentation.
  const CurveToolPreviewState *curve_preview = nullptr;
  /// Borrowed for submit(); SceneRenderBridge retains this compound preview.
  const ViewportOverlayFrame *tool_preview_overlay_frame = nullptr;
  std::uintptr_t tool_preview_overlay_identity = 0u;
  std::uint64_t tool_preview_overlay_revision = 0u;
  ViewportOverlayDynamicToolSourceStamp dynamic_tool_source;
  const ViewportOverlayDynamicToolFrameProvider *dynamic_tool_frame_provider =
      nullptr;
  RenderOverlaySelectionMode scene_selection_mode =
      RenderOverlaySelectionMode::Object;
  ViewportOverlaySelectionContext style_selection_context =
      ViewportOverlaySelectionContext::Any;
  bool overlays_visible = false;
  bool scene_overlay_requested = false;
  bool wireframe_pane = false;
  bool point_light_gizmos_visible = false;
  bool selection_dimensions_visible = true;
  ViewportArrayDimensionsMode array_dimensions_mode = ViewportArrayDimensionsMode::Inactive;
};

/** Outcome plus the unstyled dynamic stream consumed by the Qt edge HUD. */
struct ViewportOverlaySubmissionResult final {
  /// Borrowed from the coordinator until this pane changes or is invalidated.
  const ViewportOverlayFrame *edge_hud_frame = nullptr;
  std::uint64_t dynamic_source_signature = 0u;
  std::optional<ViewportSelectionDimensionBounds> selection_dimension_bounds;
  std::uint64_t selection_dimension_signature = 0u;
  bool persistent_submitted = false;
  bool dynamic_submitted = false;
  bool persistent_recovered = false;
  bool dynamic_recovered = false;
  bool camera_only = false;
  bool selection_source_refreshed = false;
};

/** Monotonic counters for broad work performed behind retained source gates. */
struct ViewportOverlaySubmissionWorkCounters final {
  std::uint64_t mesh_outline_source_refresh_count = 0u;
  std::uint64_t mesh_outline_topology_build_count = 0u;
  std::uint64_t mesh_outline_camera_classification_count = 0u;
  // These remain zero and make accidental broad source hashing test-visible.
  std::uint64_t dynamic_source_hash_count = 0u;
  std::uint64_t dynamic_source_hashed_record_count = 0u;
  std::uint64_t dynamic_source_compose_count = 0u;
  std::uint64_t dynamic_source_composed_record_count = 0u;
  std::uint64_t curve_overlay_build_count = 0u;
  std::uint64_t point_light_radius_source_refresh_count = 0u;
  std::uint64_t point_light_radius_camera_build_count = 0u;
  std::uint64_t selection_dimension_bounds_build_count = 0u;
  std::uint64_t selection_dimension_source_record_count = 0u;
  std::uint64_t selection_dimension_frame_build_count = 0u;
  std::uint64_t selection_dimension_mapping_build_count = 0u;
  std::uint64_t selection_dimension_mapping_source_record_count = 0u;
  std::uint64_t selection_dimension_position_read_count = 0u;
  std::uint64_t array_dimension_bounds_build_count = 0u;
  std::uint64_t array_dimension_originals_build_count = 0u;
};

/**
 * Owns per-pane overlay stream composition and retained submission policy.
 *
 * Semantic inputs are renderer neutral. Curves remain borrowed until the
 * dynamic stamp changes, so unchanged frames do not copy, hash, or restyle
 * their potentially large sampled geometry.
 */
class ViewportOverlaySubmissionCoordinator final {
public:
  static constexpr std::size_t kMaxViewportSlots = 4u;

  [[nodiscard]] ViewportOverlaySubmissionResult submit(
      ViewportOverlaySubmissionRequest request,
      const ViewportSettings &settings,
      ViewportOverlaySubmissionTarget &target);

  void invalidate(std::size_t viewport_index);
  void invalidate_all();
  void set_active_viewports(std::uint8_t viewport_mask);
  [[nodiscard]] ViewportOverlaySubmissionWorkCounters work_counters() const;

private:
  [[nodiscard]] std::optional<ViewportSelectionDimensionBounds>
  component_selection_dimension_bounds(
      const ViewportOverlaySubmissionRequest &request,
      const ViewportOverlayFrame *selection,
      std::uint64_t selection_revision);

  /// Pins only source-ID layout; changing preview positions remain owner inputs.
  struct ComponentDimensionPreviewLayout final {
    int object_id = -1;
    std::shared_ptr<const MeshGeometryPayload> geometry;
  };

  /// Addresses one selected authored vertex in an accepted preview position stream.
  struct ComponentDimensionPositionBinding final {
    std::size_t preview_index = 0u;
    std::size_t vertex_index = 0u;
  };

  /// Retains selected-to-preview bindings and bounds for vertices without a preview.
  struct ComponentDimensionMapping final {
    std::uint64_t selection_revision = 0u;
    std::vector<ComponentDimensionPreviewLayout> previews;
    std::vector<ComponentDimensionPositionBinding> positions;
    std::optional<ViewportSelectionDimensionBounds> fallback_bounds;
    bool valid = false;
  };

  /** Retains shared semantic selection geometry and its publication revision. */
  struct SelectionSourceState final {
    ViewportOverlaySelectionSourceStamp stamp;
    ViewportOverlayFrame frame;
    std::uint64_t publication_revision = 0u;
    bool valid = false;
  };

  /** Bounds depend on selection publications, never on camera or paint style. */
  struct SelectionDimensionSourceState final {
    std::uint64_t selection_overlay_revision = 0u;
    ViewportOverlaySceneSubmissionStamp scene;
    std::uintptr_t tool_preview_identity = 0u;
    std::uint64_t tool_preview_revision = 0u;
    RenderOverlaySelectionMode mode = RenderOverlaySelectionMode::Object;
    ViewportOverlaySelectionContext context = ViewportOverlaySelectionContext::Any;
    std::optional<ViewportSelectionDimensionBounds> bounds;
    std::uint64_t publication_revision = 0u;
    bool valid = false;
  };

  /// Separate from selection so submitting sibling panes cannot evict array bounds.
  struct ArrayDimensionSourceState final {
    RenderSourceStamp scene;
    std::uintptr_t scene_identity = 0u;
    std::uintptr_t preview_identity = 0u;
    std::uint64_t preview_revision = 0u;
    std::vector<std::pair<EntityId, EntityId>> originals;
    std::optional<ViewportSelectionDimensionBounds> original_bounds;
    std::optional<ViewportSelectionDimensionBounds> bounds;
    std::uint64_t publication_revision = 0u;
    bool valid = false;
  };

  /** Retains per-pane persistent and dynamic overlay stream state. */
  struct PaneState final {
    ViewportOverlayPersistentSubmissionStamp persistent;
    ViewportOverlayDynamicSubmissionStamp dynamic;
    bool persistent_valid = false;
    bool dynamic_valid = false;
    ViewportOverlayFrame dynamic_source_frame;
    std::uint64_t dynamic_source_revision = 0u;
    ViewportOverlaySceneSubmissionStamp point_light_source;
    std::vector<RenderOverlayLightMarkerSnapshot> selected_point_lights;
    ViewportOverlayFrame point_light_radius_frame;
    ViewportOverlayFrame selection_dimension_frame;
    bool point_light_source_valid = false;
    bool point_light_radius_enabled = false;
  };

  std::array<PaneState, kMaxViewportSlots> panes_;
  SelectionSourceState selection_source_;
  SelectionDimensionSourceState selection_dimensions_;
  ComponentDimensionMapping component_dimensions_;
  ArrayDimensionSourceState array_dimensions_;
  CurveToolOverlayCache curve_overlay_source_;
  SelectedMeshOutlineCache mesh_outline_source_;
  ViewportOverlaySubmissionWorkCounters work_counters_;
  std::uint64_t dynamic_source_publication_revision_ = 0u;
  std::uint64_t selection_dimension_publication_revision_ = 0u;
};

} // namespace quader

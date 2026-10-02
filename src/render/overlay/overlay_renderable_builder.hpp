////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/viewport_overlay_frame.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "render/render_signature_builder.hpp"
#include "render/overlay/overlay_mesh_types.hpp"
#include "render/overlay/overlay_face_fill_composition.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace quader {

/// Retains incident surface points for camera-independent component wire depth.
/// Exact endpoint positions reject ambiguous occurrences of one authored edge.
class OverlayWireSurfaceSupportCache final {
public:
  void prepare(const ViewportOverlayFrame &frame,
               const OverlayFrameSignatures &signatures);
  void apply(ViewportOverlayLine &line) const;
  [[nodiscard]] std::uint64_t build_count() const { return build_count_; }

private:
  /// Canonical authored edge identity within one object.
  struct EdgeKey {
    int object = -1;
    int first = -1;
    int second = -1;
    bool operator==(const EdgeKey &) const = default;
  };
  /// Hashes authored identity while exact equality decides support reuse.
  struct EdgeKeyHash {
    std::size_t operator()(const EdgeKey &key) const;
  };
  /// At most two incident faces support a manifold edge; others fall back.
  struct EdgeSupport {
    QVec3 first;
    QVec3 second;
    std::array<ViewportOverlaySurfaceSupport, 2u> points;
    std::array<int, 2u> faces{-1, -1};
    std::size_t count = 0u;
    bool ambiguous = false;
  };
  std::unordered_map<EdgeKey, EdgeSupport, EdgeKeyHash> supports_;
  std::uint64_t signature_ = 0u;
  std::uint64_t build_count_ = 0u;
  bool prepared_ = false;
};

/// Reports retained source-face acceleration work independently of camera queries.
struct OverlayDepthStampPointVisibilityStats {
  /// Source-frame reconciliations, including appearance-only source changes.
  std::uint64_t build_count = 0u;
  /// Actual per-object acceleration builds and exact unchanged geometry reuse.
  std::uint64_t object_build_count = 0u;
  std::uint64_t object_reuse_count = 0u;
  std::uint64_t viewport_skip_count = 0u;
  std::uint64_t ray_query_count = 0u;
  std::uint64_t query_batch_count = 0u;
  std::uint64_t parallel_query_batch_count = 0u;
  std::uint64_t queried_point_count = 0u;
  std::size_t object_count = 0u;
  std::size_t triangle_count = 0u;
};

/// Owns source-face acceleration shared by editable point layers in one pane.
/// Camera changes repeat visibility queries without extracting or indexing faces.
class OverlayDepthStampPointVisibilityFilter final {
public:
  OverlayDepthStampPointVisibilityFilter();
  ~OverlayDepthStampPointVisibilityFilter();
  OverlayDepthStampPointVisibilityFilter(
      const OverlayDepthStampPointVisibilityFilter &) = delete;
  OverlayDepthStampPointVisibilityFilter &operator=(
      const OverlayDepthStampPointVisibilityFilter &) = delete;

  void prepare(const ViewportOverlayFrame &frame,
               const OverlayFrameSignatures &signatures,
               const ViewportOverlayCamera &camera,
               bool allow_viewport_ray_skips = true);
  void invalidate();
  [[nodiscard]] bool point_visible(const ViewportOverlayPoint &point) const;
  /// Returns frame-indexed flags; the owned span lasts until the next evaluation.
  /// Workers share immutable BVHs and join before any visibility is published.
  [[nodiscard]] std::span<const std::uint8_t> evaluate(
      const ViewportOverlayFrame &frame, const OverlayFrameSignatures &signatures,
      const ViewportOverlayCamera &camera, bool allow_viewport_ray_skips = true,
      bool allow_parallel_queries = true);
  [[nodiscard]] OverlayDepthStampPointVisibilityStats stats() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

/**
 * Enumerates OverlayDepthFunc values used by the render layer.
 */
enum class OverlayDepthFunc {
    Always,
    Equal,
    Less,
    GreaterOrEqual,
};

/**
 * Enumerates OverlayCullingMode values used by the render layer.
 */
enum class OverlayCullingMode {
    None,
    Back,
};

/**
 * Enumerates OverlayTransparencyMode values used by the render layer.
 */
enum class OverlayTransparencyMode {
    Default,
    TwoPassesOneSide,
    TwoPassesTwoSides,
};

/**
 * Stores the Overlay Render State data contract used by the render extraction and Filament resource layer.
 */
struct OverlayRenderState {
  bool depth_write = false;
  bool color_write = true;
  bool depth_culling = false;
  OverlayDepthFunc depth_func = OverlayDepthFunc::Always;
  OverlayCullingMode culling_mode = OverlayCullingMode::None;
  OverlayTransparencyMode transparency_mode = OverlayTransparencyMode::Default;
  OverlayFaceFillClass face_fill_class = OverlayFaceFillClass::None;
  OverlayFaceFillPass face_fill_pass = OverlayFaceFillPass::Color;
  OverlaySilhouettePass silhouette_pass = OverlaySilhouettePass::None;
  float polygon_offset_scale = 0.0f;
  float polygon_offset_constant = 0.0f;
  std::uint16_t blend_order = 0;
};

/**
 * Represents an Overlay Face Fill Batch value used by the render extraction and Filament resource layer.
 */
struct OverlayFaceFillBatch {
    OverlayFaceFillMesh mesh;
    OverlayRenderState state;
};

/**
 * Represents an Overlay Line Batch value used by the render extraction and Filament resource layer.
 */
struct OverlayLineBatch {
    OverlayLineMesh mesh;
    OverlayRenderState state;
    // Object silhouettes share their geometry with a subdued, complementary
    // depth pass. Component wires never acquire this companion.
    std::optional<OverlayRenderState> occluded_state;
    float occluded_alpha_multiplier =
        kOverlayOccludedSilhouetteAlphaMultiplier;
    float width_pixels = 1.0f;
    float depth_bias = 0.0f;
    float near_plane = 0.05f;
};

/**
 * Represents an Overlay Edit Wire Batch value used by the render extraction and Filament resource layer.
 */
struct OverlayEditWireBatch {
    OverlayEditWireMesh mesh;
    OverlayRenderState state;
    float width_pixels = 1.0f;
    float near_plane = 0.05f;
    float far_plane = 1000.0f;
    bool orthographic_projection = false;
};

/**
 * Represents an Overlay Point Batch value used by the render extraction and Filament resource layer.
 */
struct OverlayPointBatch {
    OverlayPointMesh mesh;
    OverlayRenderState state;
    float size_pixels = 1.0f;
    float inner_radius = 0.0f;
    float depth_bias = 0.0f;
    float near_plane = 0.05f;
    std::vector<std::size_t> visibility_point_indices;
};

/**
 * Represents an Overlay Icon Batch value used by the render extraction and Filament resource layer.
 */
struct OverlayIconBatch {
    OverlayIconMesh mesh;
    OverlayRenderState state;
    ViewportOverlayIconKind icon = ViewportOverlayIconKind::PointLight;
    float size_pixels = 1.0f;
    float depth_bias = 0.0f;
};

/**
 * Represents an Overlay Layer Build Plan value used by the render extraction and Filament resource layer.
 */
struct OverlayLayerBuildPlan {
  std::vector<OverlayFaceFillBatch> face_fill_batches;
  std::vector<OverlayLineBatch> line_batches;
  std::vector<OverlayEditWireBatch> edit_wire_batches;
  std::vector<OverlayPointBatch> point_batches;
  std::vector<OverlayIconBatch> icon_batches;
};

/**
 * Builds Overlay Renderable Builder data for the render extraction and Filament resource layer.
 */
class OverlayRenderableBuilder {
public:
  static std::uint64_t frame_signature(const ViewportOverlayFrame &frame);
  static OverlayFrameSignatures
  frame_signatures(const ViewportOverlayFrame &frame);
  static std::uint64_t camera_signature(const ViewportOverlayCamera &camera);
  static std::uint64_t layer_signature(const ViewportOverlayFrame &frame,
                                       ViewportOverlayLayer layer);
  static std::uint64_t layer_signature(const OverlayFrameSignatures &signatures,
                                       ViewportOverlayLayer layer);
  static std::uint64_t
  layer_build_signature(const ViewportOverlayFrame &frame,
                        const ViewportOverlayCamera &camera,
                        ViewportOverlayLayer layer);
  static std::uint64_t
  layer_build_signature(const OverlayFrameSignatures &signatures,
                        const ViewportOverlayCamera &camera,
                        ViewportOverlayLayer layer,
                        bool retain_point_geometry = false);
  static bool layer_build_depends_on_camera(ViewportOverlayLayer layer);
  static bool
  layer_has_renderable_primitives(const OverlayFrameSignatures &signatures,
                                  ViewportOverlayLayer layer);

  static OverlayRenderState make_state(ViewportOverlayLayer layer,
                                       ViewportOverlayGeometryKind kind,
                                       std::uint16_t blend_order);
  /// Shared presentation for expanded and retained component wire geometry.
  static OverlayEditWireBatch edit_wire_presentation(
      ViewportOverlayLayer layer, float width_pixels,
      const ViewportOverlayCamera &camera, std::uint16_t blend_order);

  static OverlayLayerBuildPlan
  build_layer(const ViewportOverlayFrame &frame,
              const ViewportOverlayCamera &camera, ViewportOverlayLayer layer,
              const OverlayFrameSignatures *signatures = nullptr,
              std::uint16_t blend_order_bias = 0u,
              OverlayDepthStampPointVisibilityFilter *point_visibility = nullptr,
              bool allow_viewport_ray_skips = true,
              bool retain_point_geometry = false,
              const OverlayWireSurfaceSupportCache *wire_surface_support = nullptr);

private:
    /**
     * Stores the Line Batch Key data contract used by the render extraction and Filament resource layer.
     */
    struct LineBatchKey {
      float width_pixels = 0.0f;
      ViewportOverlayDepthMode depth_mode =
          ViewportOverlayDepthMode::DepthTested;
      bool object_silhouette = false;
    };

    /**
     * Stores the Point Batch Key data contract used by the render extraction and Filament resource layer.
     */
    struct PointBatchKey {
      float size_pixels = 0.0f;
      ViewportOverlayDepthMode depth_mode =
          ViewportOverlayDepthMode::DepthTested;
    };

    /**
     * Stores the Icon Batch Key data contract used by the render extraction and Filament resource layer.
     */
    struct IconBatchKey {
      ViewportOverlayIconKind icon = ViewportOverlayIconKind::PointLight;
      float size_pixels = 0.0f;
    };

    static std::vector<const ViewportOverlayLine *>
    lines_for_layer(const ViewportOverlayFrame &frame,
                    ViewportOverlayLayer layer);
    static std::vector<const ViewportOverlayPoint *>
    points_for_layer(const ViewportOverlayFrame &frame,
                     ViewportOverlayLayer layer,
                     const OverlayDepthStampPointVisibilityFilter *visibility);
    static std::vector<const ViewportOverlayIcon *>
    icons_for_layer(const ViewportOverlayFrame &frame,
                    ViewportOverlayLayer layer);
    static std::vector<LineBatchKey>
    line_batch_keys(const std::vector<const ViewportOverlayLine *> &lines,
                    ViewportOverlayLayer layer);
    static void
    append_point_pass(OverlayLayerBuildPlan &plan,
                      const std::vector<const ViewportOverlayPoint *> &points,
                      const ViewportOverlayCamera &camera,
                      ViewportOverlayLayer layer, std::uint16_t &blend_order,
                      bool outline, const ViewportOverlayPoint *frame_points = nullptr);
    static void
    append_icon_pass(OverlayLayerBuildPlan &plan,
                     const std::vector<const ViewportOverlayIcon *> &icons,
                     const ViewportOverlayCamera &camera,
                     ViewportOverlayLayer layer, std::uint16_t &blend_order);
};

} // namespace quader

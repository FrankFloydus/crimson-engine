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

#include <cstdint>
#include <vector>

namespace quader {

class OverlayDepthStampVisibilityFilter;

/**
 * Enumerates OverlayDepthFunc values used by the render layer.
 */
enum class OverlayDepthFunc {
    Always,
    Equal,
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
    float width_pixels = 1.0f;
    float depth_bias = 0.0f;
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
};

/**
 * Represents an Overlay Layer Build Plan value used by the render extraction and Filament resource layer.
 */
struct OverlayLayerBuildPlan {
  std::vector<OverlayFaceFillBatch> face_fill_batches;
  std::vector<OverlayLineBatch> line_batches;
  std::vector<OverlayPointBatch> point_batches;
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
                        ViewportOverlayLayer layer);
  static bool layer_build_depends_on_camera(ViewportOverlayLayer layer);
  static bool
  layer_has_renderable_primitives(const OverlayFrameSignatures &signatures,
                                  ViewportOverlayLayer layer);

  static OverlayRenderState make_state(ViewportOverlayLayer layer,
                                       ViewportOverlayGeometryKind kind,
                                       std::uint16_t blend_order,
                                       bool depth_stamp = false,
                                       bool face_color_pass = false);

  static OverlayLayerBuildPlan
  build_layer(const ViewportOverlayFrame &frame,
              const ViewportOverlayCamera &camera, ViewportOverlayLayer layer,
              const OverlayFrameSignatures *signatures = nullptr);

private:
    /**
     * Stores the Line Batch Key data contract used by the render extraction and Filament resource layer.
     */
    struct LineBatchKey {
      float width_pixels = 0.0f;
    };

    static std::vector<const ViewportOverlayLine *>
    lines_for_layer(const ViewportOverlayFrame &frame,
                    ViewportOverlayLayer layer,
                    const ViewportOverlayCamera &camera,
                    const OverlayDepthStampVisibilityFilter *visibility);
    static std::vector<const ViewportOverlayPoint *>
    points_for_layer(const ViewportOverlayFrame &frame,
                     ViewportOverlayLayer layer,
                     const ViewportOverlayCamera &camera,
                     const OverlayDepthStampVisibilityFilter *visibility);
    static std::vector<LineBatchKey>
    line_batch_keys(const std::vector<const ViewportOverlayLine *> &lines,
                    ViewportOverlayLayer layer);
    static void
    append_point_pass(OverlayLayerBuildPlan &plan,
                      const std::vector<const ViewportOverlayPoint *> &points,
                      const ViewportOverlayCamera &camera,
                      ViewportOverlayLayer layer, std::uint16_t &blend_order,
                      bool outline);
};

} // namespace quader

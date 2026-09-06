////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/viewport_overlay_policy.hpp"

#include <cstddef>

namespace quader {

float viewport_overlay_sanitized_raster_scale(float raster_scale) {
  return viewport_sanitized_pixel_scale(raster_scale);
}

int viewport_overlay_draw_order(ViewportOverlayLayer layer) {
  for (int index = 0;
       index < static_cast<int>(kViewportOverlayDrawOrder.size()); ++index) {
    if (kViewportOverlayDrawOrder[static_cast<std::size_t>(index)] == layer) {
      return index;
    }
  }
  return static_cast<int>(kViewportOverlayDrawOrder.size());
}

bool viewport_overlay_layer_uses_depth_stamped_face_fill(
    ViewportOverlayLayer layer) {
  return layer == ViewportOverlayLayer::SelectedVisibleFace ||
         layer == ViewportOverlayLayer::SelectedXrayFace ||
         layer == ViewportOverlayLayer::SelectedFaceMask ||
         layer == ViewportOverlayLayer::HoverFace;
}

bool viewport_overlay_layer_uses_depth_only_face_fill(
    ViewportOverlayLayer layer) {
  (void)layer;
  return false;
}

bool viewport_overlay_triangle_is_two_sided(ViewportOverlayLayer layer) {
  return layer == ViewportOverlayLayer::SelectedVisibleFace ||
         layer == ViewportOverlayLayer::SelectedXrayFace ||
         layer == ViewportOverlayLayer::SelectedFaceMask ||
         layer == ViewportOverlayLayer::HoverFace ||
         layer == ViewportOverlayLayer::ToolPlane ||
         layer == ViewportOverlayLayer::ToolHandle ||
         layer == ViewportOverlayLayer::ToolPreviewFill;
}

bool viewport_overlay_line_draws_on_top(ViewportOverlayLayer layer) {
  return layer == ViewportOverlayLayer::SourceWire ||
         layer == ViewportOverlayLayer::WireframeXray ||
         layer == ViewportOverlayLayer::SelectedFaceEdge ||
         layer == ViewportOverlayLayer::SelectedEdge ||
         layer == ViewportOverlayLayer::HoverFaceEdge ||
         layer == ViewportOverlayLayer::HoverEdge ||
         layer == ViewportOverlayLayer::GroupBounds ||
         layer == ViewportOverlayLayer::DiagnosticEdge ||
         layer == ViewportOverlayLayer::ToolPlaneLine ||
         layer == ViewportOverlayLayer::ToolPreviewLine ||
         layer == ViewportOverlayLayer::ToolPreviewSeam ||
         layer == ViewportOverlayLayer::ToolHandle;
}

bool viewport_overlay_point_draws_on_top(ViewportOverlayLayer layer) {
  return layer == ViewportOverlayLayer::SnapTargetVertex ||
         layer == ViewportOverlayLayer::ToolHandle;
}

bool viewport_overlay_icon_draws_on_top(ViewportOverlayLayer layer) {
  return layer == ViewportOverlayLayer::PointLightGizmo;
}

bool viewport_overlay_component_selection_source(ViewportOverlaySource source) {
  return source == ViewportOverlaySource::ComponentSelection ||
         source == ViewportOverlaySource::ComponentHover;
}

bool viewport_overlay_line_uses_component_depth(
    const ViewportOverlayLine &line) {
  if (!viewport_overlay_component_selection_source(line.source)) {
    return false;
  }
  return line.layer == ViewportOverlayLayer::SourceWire ||
         line.layer == ViewportOverlayLayer::SelectedFaceEdge ||
         line.layer == ViewportOverlayLayer::SelectedEdge ||
         line.layer == ViewportOverlayLayer::HoverFaceEdge ||
         line.layer == ViewportOverlayLayer::HoverEdge;
}

ViewportOverlayDepthMode
viewport_overlay_depth_mode(ViewportOverlayLayer layer,
                            ViewportOverlayGeometryKind kind) {
  if (layer == ViewportOverlayLayer::ToolPlane ||
      layer == ViewportOverlayLayer::ToolHandle ||
      layer == ViewportOverlayLayer::ToolPreviewFill) {
    return ViewportOverlayDepthMode::DrawOnTop;
  }
  if (kind == ViewportOverlayGeometryKind::Lines &&
      viewport_overlay_line_draws_on_top(layer)) {
    return ViewportOverlayDepthMode::DrawOnTop;
  }
  if (kind == ViewportOverlayGeometryKind::Points &&
      viewport_overlay_point_draws_on_top(layer)) {
    return ViewportOverlayDepthMode::DrawOnTop;
  }
  if (kind == ViewportOverlayGeometryKind::Icons &&
      viewport_overlay_icon_draws_on_top(layer)) {
    return ViewportOverlayDepthMode::DrawOnTop;
  }
  return ViewportOverlayDepthMode::DepthTested;
}

ViewportOverlayDepthMode
viewport_overlay_depth_mode(const ViewportOverlayLine &line) {
  if (viewport_overlay_line_uses_component_depth(line) ||
      line.source == ViewportOverlaySource::MeshOutline) {
    return ViewportOverlayDepthMode::DepthTested;
  }
  return viewport_overlay_depth_mode(line.layer,
                                     ViewportOverlayGeometryKind::Lines);
}

ViewportOverlayDepthMode
viewport_overlay_depth_mode(const ViewportOverlayPoint &point) {
  return viewport_overlay_depth_mode(point.layer,
                                     ViewportOverlayGeometryKind::Points);
}

bool viewport_overlay_depth_tested(ViewportOverlayLayer layer,
                                   ViewportOverlayGeometryKind kind) {
  return viewport_overlay_depth_mode(layer, kind) ==
         ViewportOverlayDepthMode::DepthTested;
}

float viewport_overlay_depth_bias(ViewportOverlayDepthMode mode,
                                  ViewportOverlayGeometryKind kind) {
  if (mode != ViewportOverlayDepthMode::DepthTested) {
    return 0.0f;
  }
  switch (kind) {
  case ViewportOverlayGeometryKind::Triangles:
    return kOverlayFillDepthBias;
  case ViewportOverlayGeometryKind::Lines:
    return kOverlayLineDepthBias;
  case ViewportOverlayGeometryKind::Points:
  case ViewportOverlayGeometryKind::Icons:
    return kOverlayPointDepthBias;
  }
  return 0.0f;
}

float viewport_overlay_depth_bias(ViewportOverlayLayer layer,
                                  ViewportOverlayGeometryKind kind) {
  return viewport_overlay_depth_bias(viewport_overlay_depth_mode(layer, kind),
                                     kind);
}

bool viewport_overlay_layer_writes_transient_hover_suppression_mask(
    ViewportOverlayLayer layer) {
  return layer == ViewportOverlayLayer::HoverFace ||
         layer == ViewportOverlayLayer::HoverFaceEdge ||
         layer == ViewportOverlayLayer::HoverEdge ||
         layer == ViewportOverlayLayer::HoverVertex;
}

bool viewport_overlay_layer_is_suppressed_by_transient_hover(
    ViewportOverlayLayer layer) {
  return layer == ViewportOverlayLayer::SelectedVisibleFace ||
         layer == ViewportOverlayLayer::SelectedXrayFace ||
         layer == ViewportOverlayLayer::SelectedFaceMask ||
         layer == ViewportOverlayLayer::SelectedFaceEdge ||
         layer == ViewportOverlayLayer::SelectedEdge ||
         layer == ViewportOverlayLayer::SelectedVertex;
}

ViewportOverlayLayerPolicy
viewport_overlay_layer_policy(ViewportOverlayLayer layer) {
  return {
      .layer = layer,
      .draw_order = viewport_overlay_draw_order(layer),
      .depth_stamped_face_fill =
          viewport_overlay_layer_uses_depth_stamped_face_fill(layer),
      .writes_transient_hover_suppression_mask =
          viewport_overlay_layer_writes_transient_hover_suppression_mask(layer),
      .suppressed_by_transient_hover =
          viewport_overlay_layer_is_suppressed_by_transient_hover(layer),
  };
}

float viewport_overlay_line_width_pixels(ViewportOverlayLayer layer,
                                         float native_width_pixels) {
  if (native_width_pixels > 0.0f) {
    return native_width_pixels;
  }
  switch (layer) {
  case ViewportOverlayLayer::WireframeBase:
    return kOverlayWireframeBaseLineWidthPixels;
  case ViewportOverlayLayer::WireframeXray:
    return kOverlayWireframeXrayLineWidthPixels;
  case ViewportOverlayLayer::SourceWire:
    return kOverlaySourceWireLineWidthPixels;
  case ViewportOverlayLayer::SelectedFaceEdge:
  case ViewportOverlayLayer::SelectedEdge:
    return kOverlaySelectionWireLineWidthPixels;
  case ViewportOverlayLayer::HoverFaceEdge:
  case ViewportOverlayLayer::HoverEdge:
    return kOverlayHoverWireLineWidthPixels;
  case ViewportOverlayLayer::OpenEdge:
    return kOverlayOpenEdgeLineWidthPixels;
  case ViewportOverlayLayer::DiagnosticEdge:
    return kOverlayDiagnosticEdgeLineWidthPixels;
  case ViewportOverlayLayer::GroupBounds:
    return kOverlayGroupBoundsLineWidthPixels;
  case ViewportOverlayLayer::ToolPlaneLine:
    return kOverlayToolPlaneLineWidthPixels;
  case ViewportOverlayLayer::ToolPreviewLine:
    return kOverlayToolPreviewLineWidthPixels;
  case ViewportOverlayLayer::ToolPreviewSeam:
    return kOverlayToolPreviewSeamLineWidthPixels;
  case ViewportOverlayLayer::ToolHandle:
    return kOverlayToolHandleLineWidthPixels;
  default:
    return kOverlayWireframeBaseLineWidthPixels;
  }
}

float viewport_overlay_point_size_pixels(ViewportOverlayLayer layer,
                                         float native_size_pixels) {
  if (std::isfinite(native_size_pixels) && native_size_pixels > 0.0f) {
    return native_size_pixels;
  }
  switch (layer) {
  case ViewportOverlayLayer::UnselectedVertex:
    return kOverlayVertexSizePixels;
  case ViewportOverlayLayer::SelectedVertex:
    return kOverlayVertexSizePixels + kOverlaySelectedVertexGrowthPixels;
  case ViewportOverlayLayer::HoverVertex:
    return kOverlayVertexSizePixels + kOverlayHoverVertexGrowthPixels;
  default:
    break;
  }
  return 2.0f;
}

float viewport_overlay_point_outline_growth_pixels(ViewportOverlayLayer layer) {
  return layer == ViewportOverlayLayer::SelectedVertex ||
                 layer == ViewportOverlayLayer::HoverVertex ||
                 layer == ViewportOverlayLayer::SnapTargetVertex
             ? kOverlayVertexOutlineSizePixels
             : 0.0f;
}

bool viewport_overlay_point_has_outline(ViewportOverlayLayer layer) {
  return viewport_overlay_point_outline_growth_pixels(layer) > 0.0f;
}

bool viewport_overlay_point_is_outline_only(ViewportOverlayLayer layer) {
  return layer == ViewportOverlayLayer::SnapTargetVertex;
}

float viewport_overlay_point_inner_radius(ViewportOverlayLayer layer) {
  return layer == ViewportOverlayLayer::SnapTargetVertex ? 0.62f : 0.0f;
}

bool viewport_overlay_color_approximately(std::uint32_t a, std::uint32_t b) {
  return a == b;
}

bool viewport_overlay_is_native_remove_color(std::uint32_t color) {
  return viewport_overlay_color_approximately(color, kOverlayRemoveFaceColor) ||
         viewport_overlay_color_approximately(color, kOverlayRemoveWireColor);
}

bool viewport_overlay_is_native_selection_wire_color(std::uint32_t color) {
  return viewport_overlay_color_approximately(color,
                                              kOverlaySelectedVertexColor) ||
         viewport_overlay_color_approximately(color,
                                              kOverlaySelectionWireColor);
}

bool selected_object_face_fills_enabled_for_selection_count(
    std::size_t selected_mesh_count) {
  return selected_mesh_count > 0u;
}

std::uint32_t viewport_overlay_color_for(ViewportOverlayLayer layer,
                                         std::uint32_t original_rgba) {
  switch (layer) {
  case ViewportOverlayLayer::WireframeBase:
  case ViewportOverlayLayer::WireframeXray:
    return original_rgba;
  case ViewportOverlayLayer::SelectedVisibleFace:
  case ViewportOverlayLayer::SelectedXrayFace:
  case ViewportOverlayLayer::SelectedFaceMask:
    return kOverlaySelectionFaceColor;
  case ViewportOverlayLayer::SelectedFaceEdge:
  case ViewportOverlayLayer::SelectedEdge:
    return kOverlaySelectionWireColor;
  case ViewportOverlayLayer::SourceWire:
  case ViewportOverlayLayer::SourceWireDepthStamp:
    return viewport_overlay_is_native_selection_wire_color(original_rgba)
               ? kOverlaySelectionWireColor
               : kOverlaySourceWireColor;
  case ViewportOverlayLayer::OpenEdge:
    return kOverlayOpenEdgeColor;
  case ViewportOverlayLayer::DiagnosticEdge:
    return kOverlayDiagnosticEdgeColor;
  case ViewportOverlayLayer::GroupBounds:
    return kOverlayGroupBoundsColor;
  case ViewportOverlayLayer::HoverFace:
    return viewport_overlay_is_native_remove_color(original_rgba)
               ? kOverlayRemoveFaceColor
               : kOverlayHoverFaceColor;
  case ViewportOverlayLayer::HoverFaceEdge:
  case ViewportOverlayLayer::HoverEdge:
    return viewport_overlay_is_native_remove_color(original_rgba)
               ? kOverlayRemoveWireColor
               : kOverlayHoverWireColor;
  case ViewportOverlayLayer::UnselectedVertex:
    return kOverlayVertexColor;
  case ViewportOverlayLayer::SelectedVertex:
    return kOverlaySelectedVertexColor;
  case ViewportOverlayLayer::HoverVertex:
    return viewport_overlay_is_native_remove_color(original_rgba)
               ? kOverlayRemoveWireColor
               : kOverlayHoverVertexColor;
  case ViewportOverlayLayer::SnapTargetVertex:
    return original_rgba;
  case ViewportOverlayLayer::ToolPlane:
    return kOverlayToolPlaneColor;
  case ViewportOverlayLayer::ToolPlaneLine:
    return kOverlayToolPlaneLineColor;
  case ViewportOverlayLayer::ToolPreviewFill:
  case ViewportOverlayLayer::ToolPreviewLine:
  case ViewportOverlayLayer::ToolPreviewSeam:
    return original_rgba;
  case ViewportOverlayLayer::ToolHandle:
    return kOverlayToolHandleColor;
  }
  return original_rgba;
}

} // namespace quader

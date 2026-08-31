////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/curve_tool_overlay_cache.hpp"

#include "assets/curve_asset_payload.hpp"
#include "diagnostics/profile.hpp"
#include "math/glm_bridge.hpp"
#include "render/overlay/curve_overlay_geometry.hpp"
#include "render/viewport_overlay_policy.hpp"

namespace quader {
CurveToolOverlaySourceStamp CurveToolOverlayCache::source_stamp(
    const CurveToolPreviewState &preview) noexcept {
  return {
      .preview_revision = preview.revision,
      .document_identity = preview.document.has_value()
                               ? preview.document->content_identity().value
                               : 0u,
      .document_revision = preview.document.has_value()
                               ? preview.document->content_revision()
                               : 0u,
      .entity = preview.entity,
      .modeling_object_id = preview.modeling_object_id,
      .active = preview.active,
      .drawing = preview.drawing,
      .dragging = preview.dragging,
      .document_available = preview.document.has_value(),
  };
}

const ViewportOverlayFrame &CurveToolOverlayCache::frame(
    const CurveToolPreviewState &preview) {
  const CurveToolOverlaySourceStamp stamp = source_stamp(preview);
  if (valid_ && stamp == previous_stamp_) {
    return frame_;
  }
  QDR_PROFILE_SCOPE("Curve tool rebuild overlay");
  previous_stamp_ = stamp;
  valid_ = true;
  ++build_count_;
  ++publication_revision_;
  frame_.lines.clear();
  frame_.points.clear();
  if (!preview.active || !preview.document.has_value()) {
    samples_.reset();
    return frame_;
  }
  if (preview.drawing) {
    const auto samples = curve_sample_payload(*preview.document, samples_);
    append_curve_overlay_stroke(frame_, *samples, preview.world_transform,
        ViewportOverlayLine{
            .layer = ViewportOverlayLayer::WireframeBase,
            .color = kOverlaySelectionWireColor,
            .width_pixels = kOverlayCurveLineWidthPixels,
            .style_role = ViewportOverlayStyleRole::SelectionWire,
            .source = ViewportOverlaySource::Tool,
        });
  }
  const auto points = preview.document->points();
  frame_.points.reserve(points.size() * 3u + 1u);
  frame_.lines.reserve(frame_.lines.size() + points.size() * 2u + 1u);
  for (std::size_t index = 0; index < points.size(); ++index) {
    const auto &point = points[index];
    const QVec3 anchor = transform_point(preview.world_transform,
        curve_control_local_position(point, CurveControlKind::Anchor));
    const bool anchor_selected = preview.control_selection.contains(
        CurveControlTarget{point.id, CurveControlKind::Anchor});
    for (const CurveControlKind kind : {CurveControlKind::Anchor,
                                       CurveControlKind::IncomingHandle,
                                       CurveControlKind::OutgoingHandle}) {
      if (!curve_control_is_visible(preview, index, kind)) {
        continue;
      }
      const QVec3 world = transform_point(preview.world_transform,
          curve_control_local_position(point, kind));
      const bool selected = preview.control_selection.contains(
          CurveControlTarget{point.id, kind});
      if (kind != CurveControlKind::Anchor) {
        frame_.lines.push_back({
            .layer = ViewportOverlayLayer::ToolHandle,
            .start = anchor,
            .end = world,
            .color = anchor_selected ? kOverlaySelectedVertexColor
                                     : kOverlayToolPlaneLineColor,
            .width_pixels = kOverlayToolPlaneLineWidthPixels,
            .style_role = ViewportOverlayStyleRole::ExplicitColor,
            .source = ViewportOverlaySource::Tool,
        });
      }
      frame_.points.push_back({
          .layer = ViewportOverlayLayer::ToolHandle,
          .position = world,
          .color = selected
                       ? kOverlaySelectedVertexColor
                       : (kind == CurveControlKind::Anchor
                              ? kOverlayVertexColor
                              : kOverlayToolHandleColor),
          .size_pixels = kOverlayDrawnPlanePointSizePixels,
          .style_role = ViewportOverlayStyleRole::ExplicitColor,
          .source = ViewportOverlaySource::Tool,
      });
    }
  }
  if (preview.drawing && !preview.dragging && preview.hover_world.has_value()) {
    // Placement already resolved snapping in world space. Match Poly's hover
    // vertex presentation, including before the first anchor has been placed.
    frame_.points.push_back({
        .layer = ViewportOverlayLayer::HoverVertex,
        .position = *preview.hover_world,
        .color = kOverlayHoverVertexColor,
        .size_pixels = kOverlayDrawnPlanePointSizePixels,
        .style_role = ViewportOverlayStyleRole::HoverVertex,
        .source = ViewportOverlaySource::Tool,
    });
    if (!points.empty()) {
      frame_.lines.push_back({
          .layer = ViewportOverlayLayer::ToolPlaneLine,
          .start = transform_point(preview.world_transform,
              curve_control_local_position(points.back(), CurveControlKind::Anchor)),
          .end = *preview.hover_world,
          .color = kOverlayToolPlaneLineColor,
          .width_pixels = kOverlayToolPlaneLineWidthPixels,
          .style_role = ViewportOverlayStyleRole::ExplicitColor,
          .source = ViewportOverlaySource::Tool,
      });
    }
  }
  return frame_;
}

void CurveToolOverlayCache::reset() {
  ++publication_revision_;
  frame_ = {};
  previous_stamp_ = {};
  samples_.reset();
  valid_ = false;
}

} // namespace quader

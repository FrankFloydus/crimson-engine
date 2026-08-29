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
namespace {

bool same_hover(const std::optional<QVec3> &left,
                const std::optional<QVec3> &right) {
  return left.has_value() == right.has_value() &&
         (!left.has_value() || (left->x == right->x && left->y == right->y &&
                                left->z == right->z));
}

bool same_preview(const CurveToolPreviewState &left,
                  const CurveToolPreviewState &right) {
  if (left.revision != right.revision || left.active != right.active ||
      left.drawing != right.drawing || left.dragging != right.dragging ||
      left.entity != right.entity ||
      left.modeling_object_id != right.modeling_object_id ||
      left.world_transform.values != right.world_transform.values ||
      left.control_selection != right.control_selection ||
      !same_hover(left.hover_world, right.hover_world) ||
      left.document.has_value() != right.document.has_value()) {
    return false;
  }
  return !left.document.has_value() ||
         (left.document->content_identity() == right.document->content_identity() &&
          left.document->content_revision() == right.document->content_revision());
}

} // namespace

const ViewportOverlayFrame &CurveToolOverlayCache::frame(
    const CurveToolPreviewState &preview) {
  if (valid_ && same_preview(preview, previous_)) {
    return frame_;
  }
  QDR_PROFILE_SCOPE("Curve tool rebuild overlay");
  previous_ = preview;
  valid_ = true;
  ++build_count_;
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
  frame_ = {};
  previous_ = {};
  samples_.reset();
  build_count_ = 0;
  valid_ = false;
}

} // namespace quader

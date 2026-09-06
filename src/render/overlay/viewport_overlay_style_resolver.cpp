////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/viewport_overlay_style_resolver.hpp"

#include "core/color_space.hpp"
#include "render/render_signature_builder.hpp"
#include "render/viewport_overlay_policy.hpp"

#include <algorithm>

namespace quader {
namespace {

bool selection_context_matches(ViewportOverlaySelectionContext override_mode,
                               ViewportOverlaySelectionContext active_mode) {
  return override_mode == ViewportOverlaySelectionContext::Any ||
         override_mode == active_mode;
}

} // namespace

ViewportPersistentOverlayStyleSnapshot
ViewportOverlayStyleResolver::persistent_style_snapshot(
    const ViewportSettings &settings, float raster_scale) {
  const float resolved_raster_scale =
      viewport_overlay_sanitized_raster_scale(raster_scale);
  const float selection_edge_logical_width_pixels =
      viewport_overlay_line_width_pixels(
          ViewportOverlayLayer::SelectedEdge,
          settings.float_value(ViewportFloatSettingId::SelectionEdgeLineSize));
  ViewportPersistentOverlayStyleSnapshot snapshot{
      .raster_scale = resolved_raster_scale,
      .selection_face_color =
          setting_color(settings, ViewportColorSettingId::SelectionFace),
      .selection_wire_color =
          setting_color(settings, ViewportColorSettingId::SelectionWire),
      .selection_edge_logical_width_pixels =
          selection_edge_logical_width_pixels,
      .selection_edge_line_width_pixels =
          selection_edge_logical_width_pixels *
          resolved_raster_scale,
      .instance_edit_style = InstanceEditStyle::from_settings(settings),
  };
  RenderSignatureBuilder face_signature;
  face_signature.add_value(snapshot.selection_face_color);
  snapshot.face_signature = face_signature.value();

  RenderSignatureBuilder wire_signature;
  wire_signature.add_value(snapshot.selection_wire_color)
      .add_float(snapshot.selection_edge_logical_width_pixels)
      .add_float(snapshot.selection_edge_line_width_pixels)
      .add_float(snapshot.raster_scale);
  snapshot.wire_signature = wire_signature.value();

  const InstanceEditStyle &instance_edit_style = snapshot.instance_edit_style;
  RenderSignatureBuilder context_signature;
  context_signature.add_float(instance_edit_style.context.r)
      .add_float(instance_edit_style.context.g)
      .add_float(instance_edit_style.context.b)
      .add_float(instance_edit_style.context.a)
      .add_float(instance_edit_style.context_strength)
      .add_float(instance_edit_style.context_tint_amount)
      .add_float(instance_edit_style.context_desaturation);
  snapshot.instance_edit_context_signature = context_signature.value();
  return snapshot;
}

std::uint64_t ViewportOverlayStyleResolver::style_signature(
    const ViewportSettings &settings, float raster_scale) {
  RenderSignatureBuilder signature;
  for (std::size_t index = 0u; index < kViewportColorSettingCount; ++index) {
    const ViewportSettingColor color =
        settings.color(static_cast<ViewportColorSettingId>(index));
    signature.add_float(color.r)
        .add_float(color.g)
        .add_float(color.b)
        .add_float(color.a);
  }
  for (std::size_t index = 0u; index < kViewportFloatSettingCount; ++index) {
    signature.add_float(
        settings.float_value(static_cast<ViewportFloatSettingId>(index)));
  }
  signature.add_bool(
      settings.bool_value(ViewportBoolSettingId::BackfaceSelection));
  signature.add_float(
      viewport_overlay_sanitized_raster_scale(raster_scale));
  return signature.value();
}

ViewportOverlayFrame ViewportOverlayStyleResolver::resolve(
        ViewportOverlayFrame frame,
        const ViewportSettings& settings,
        const ViewportOverlayStyleContext& context) {
    apply(frame, settings, context);
    return frame;
}

void ViewportOverlayStyleResolver::apply(ViewportOverlayFrame& frame, const ViewportSettings& settings) {
    apply(frame, settings, {});
}

void ViewportOverlayStyleResolver::apply(
        ViewportOverlayFrame& frame,
        const ViewportSettings& settings,
        const ViewportOverlayStyleContext& context) {
  apply_from(frame, settings, context, {});
}

void ViewportOverlayStyleResolver::apply_from(
    ViewportOverlayFrame &frame, const ViewportSettings &settings,
    const ViewportOverlayStyleContext &context,
    ViewportOverlayStyleRange range) {
    range.triangle_begin = std::min(range.triangle_begin, frame.triangles.size());
    range.line_begin = std::min(range.line_begin, frame.lines.size());
    range.point_begin = std::min(range.point_begin, frame.points.size());
    range.icon_begin = std::min(range.icon_begin, frame.icons.size());
    const float raster_scale =
        viewport_overlay_sanitized_raster_scale(context.raster_scale);
    if (context.construction_object_id >= 0) {
      // Remove the records rather than making them transparent: selection and
      // hover fills also write depth masks that can obscure other overlays.
      const auto first = frame.triangles.begin() +
                         static_cast<std::ptrdiff_t>(range.triangle_begin);
      frame.triangles.erase(
          std::remove_if(first, frame.triangles.end(),
                         [&](const ViewportOverlayTriangle &triangle) {
                           const auto role = role_for(triangle);
                           return triangle.element.object_id ==
                                      context.construction_object_id &&
                                  (role == ViewportOverlayStyleRole::SelectionFace ||
                                   role == ViewportOverlayStyleRole::HoverFace ||
                                   role == ViewportOverlayStyleRole::RemoveSelectionFace);
                         }),
          frame.triangles.end());
    }
    if (context.projection == ViewportOverlayProjection::Orthographic &&
        !settings.bool_value(ViewportBoolSettingId::BackfaceSelection)) {
      // Orthographic hover is outline-only here: transparent triangles would
      // still stamp overlay depth. Perspective hover, selected faces, and tool
      // face-plane previews keep their fills regardless of the picking preference.
      const auto first = frame.triangles.begin() +
                         static_cast<std::ptrdiff_t>(range.triangle_begin);
      frame.triangles.erase(
          std::remove_if(first, frame.triangles.end(),
                         [](const ViewportOverlayTriangle &triangle) {
                           return triangle.layer ==
                                      ViewportOverlayLayer::HoverFace &&
                                  role_for(triangle) ==
                                      ViewportOverlayStyleRole::HoverFace;
                         }),
          frame.triangles.end());
    }
    for (auto it = frame.triangles.begin() +
                   static_cast<std::ptrdiff_t>(range.triangle_begin);
         it != frame.triangles.end(); ++it) {
      ViewportOverlayTriangle &triangle = *it;
      const ViewportOverlayStyleRole role = role_for(triangle);
      triangle.color = color_for(role, triangle.color, settings);
      if (const ViewportOverlayStyleOverride *override =
              override_for(role, context);
          override != nullptr) {
        if (override->color.has_value()) {
          triangle.color = *override->color;
        }
      }
    }

    for (auto it = frame.lines.begin() +
                   static_cast<std::ptrdiff_t>(range.line_begin);
         it != frame.lines.end(); ++it) {
      ViewportOverlayLine &line = *it;
      const ViewportOverlayStyleRole role = role_for(line);
      line.color = color_for(role, line.color, settings);
      float resolved_width_pixels =
          line_width_for(role, line.layer, line.width_pixels, settings);
      if (const ViewportOverlayStyleOverride *override =
              override_for(role, context);
          override != nullptr) {
        if (override->color.has_value()) {
          line.color = *override->color;
        }
        if (override->line_width_pixels.has_value()) {
          resolved_width_pixels = *override->line_width_pixels;
        }
      }
      line.width_pixels =
          viewport_overlay_line_width_pixels(line.layer,
                                             resolved_width_pixels) *
          raster_scale;
      if (context.construction_object_id >= 0 &&
          line.element.object_id == context.construction_object_id &&
          (role == ViewportOverlayStyleRole::SelectionWire ||
           role == ViewportOverlayStyleRole::SelectedSourceWire ||
           role == ViewportOverlayStyleRole::SourceWire ||
           role == ViewportOverlayStyleRole::HoverWire ||
           role == ViewportOverlayStyleRole::RemoveSelectionWire)) {
        const std::uint32_t alpha = (line.color >> 24u) / 10u;
        line.color = (line.color & 0x00ffffffu) | (alpha << 24u);
        line.width_pixels = std::min(line.width_pixels, raster_scale);
      }
    }

    for (auto it = frame.points.begin() +
                   static_cast<std::ptrdiff_t>(range.point_begin);
         it != frame.points.end(); ++it) {
      ViewportOverlayPoint &point = *it;
      const ViewportOverlayStyleRole role = role_for(point);
      point.color = color_for(role, point.color, settings);
      float resolved_size_pixels =
          point_size_for(role, point.size_pixels, settings);
      std::optional<float> resolved_outline_growth_pixels =
          point_outline_growth_for(role, point.outline_growth_pixels, settings);
      if (const ViewportOverlayStyleOverride *override =
              override_for(role, context);
          override != nullptr) {
        if (override->color.has_value()) {
          point.color = *override->color;
        }
        if (override->point_size_pixels.has_value()) {
          resolved_size_pixels = *override->point_size_pixels;
        }
        if (override->outline_growth_pixels.has_value()) {
          resolved_outline_growth_pixels = *override->outline_growth_pixels;
        }
      }
      point.size_pixels =
          viewport_overlay_point_size_pixels(point.layer,
                                             resolved_size_pixels) *
          raster_scale;
      const float outline_growth_pixels =
          resolved_outline_growth_pixels.value_or(
              viewport_overlay_point_outline_growth_pixels(point.layer));
      point.outline_growth_pixels =
          resolved_outline_growth_pixels.has_value() ||
                  outline_growth_pixels > 0.0f
              ? std::optional<float>{outline_growth_pixels * raster_scale}
              : std::nullopt;
    }

    for (auto it = frame.icons.begin() +
                   static_cast<std::ptrdiff_t>(range.icon_begin);
         it != frame.icons.end(); ++it) {
      ViewportOverlayIcon &icon = *it;
      icon.size_pixels =
          viewport_overlay_point_size_pixels(icon.layer, icon.size_pixels) *
          raster_scale;
    }
}

ViewportOverlayStyleRole ViewportOverlayStyleResolver::role_for(
    const ViewportOverlayTriangle &triangle) {
  if (triangle.style_role != ViewportOverlayStyleRole::LayerDefault) {
    return triangle.style_role;
  }
  switch (triangle.layer) {
  case ViewportOverlayLayer::SelectedVisibleFace:
  case ViewportOverlayLayer::SelectedXrayFace:
  case ViewportOverlayLayer::SelectedFaceMask:
    return ViewportOverlayStyleRole::SelectionFace;
  case ViewportOverlayLayer::HoverFace:
    return ViewportOverlayStyleRole::HoverFace;
  case ViewportOverlayLayer::ToolPlane:
    return ViewportOverlayStyleRole::ToolPlaneFill;
  case ViewportOverlayLayer::ToolPreviewFill:
    return ViewportOverlayStyleRole::ToolPreviewFill;
  default:
    return ViewportOverlayStyleRole::ExplicitColor;
  }
}

ViewportOverlayStyleRole
ViewportOverlayStyleResolver::role_for(const ViewportOverlayLine &line) {
  if (line.style_role != ViewportOverlayStyleRole::LayerDefault) {
    return line.style_role;
  }
  switch (line.layer) {
  case ViewportOverlayLayer::SourceWire:
    return ViewportOverlayStyleRole::SourceWire;
  case ViewportOverlayLayer::SelectedFaceEdge:
  case ViewportOverlayLayer::SelectedEdge:
    return ViewportOverlayStyleRole::SelectionWire;
  case ViewportOverlayLayer::HoverFaceEdge:
  case ViewportOverlayLayer::HoverEdge:
    return ViewportOverlayStyleRole::HoverWire;
  case ViewportOverlayLayer::OpenEdge:
    return ViewportOverlayStyleRole::OpenEdge;
  case ViewportOverlayLayer::DiagnosticEdge:
    return ViewportOverlayStyleRole::DiagnosticEdge;
  case ViewportOverlayLayer::GroupBounds:
    return ViewportOverlayStyleRole::GroupBounds;
  case ViewportOverlayLayer::WireframeBase:
  case ViewportOverlayLayer::WireframeXray:
    return ViewportOverlayStyleRole::WireframeBase;
  case ViewportOverlayLayer::ToolPlaneLine:
    return ViewportOverlayStyleRole::ToolPlaneLine;
  case ViewportOverlayLayer::ToolPreviewLine:
    return ViewportOverlayStyleRole::ToolPreviewLine;
  case ViewportOverlayLayer::ToolPreviewSeam:
    return ViewportOverlayStyleRole::ToolPreviewSeam;
  case ViewportOverlayLayer::ToolHandle:
    return ViewportOverlayStyleRole::ToolHandle;
  default:
    return ViewportOverlayStyleRole::ExplicitColor;
  }
}

ViewportOverlayStyleRole
ViewportOverlayStyleResolver::role_for(const ViewportOverlayPoint &point) {
  if (point.style_role != ViewportOverlayStyleRole::LayerDefault) {
    return point.style_role;
  }
  switch (point.layer) {
  case ViewportOverlayLayer::UnselectedVertex:
    return ViewportOverlayStyleRole::Vertex;
  case ViewportOverlayLayer::SelectedVertex:
    return ViewportOverlayStyleRole::SelectedVertex;
  case ViewportOverlayLayer::HoverVertex:
    return ViewportOverlayStyleRole::HoverVertex;
  case ViewportOverlayLayer::SnapTargetVertex:
    return ViewportOverlayStyleRole::SnapTargetVertex;
  case ViewportOverlayLayer::ToolHandle:
    return ViewportOverlayStyleRole::ToolHandle;
  default:
    return ViewportOverlayStyleRole::ExplicitColor;
  }
}

const ViewportOverlayStyleOverride *ViewportOverlayStyleResolver::override_for(
    ViewportOverlayStyleRole role, const ViewportOverlayStyleContext &context) {
  const ViewportOverlayStyleOverride *fallback = nullptr;
  for (auto it = context.overrides.rbegin(); it != context.overrides.rend();
       ++it) {
    if (it->role != role || !selection_context_matches(
                                it->selection_mode, context.selection_mode)) {
      continue;
    }
    if (it->selection_mode == context.selection_mode) {
      return &*it;
    }
    if (fallback == nullptr) {
      fallback = &*it;
    }
  }
  return fallback;
}

std::uint32_t
ViewportOverlayStyleResolver::color_for(ViewportOverlayStyleRole role,
                                        std::uint32_t authored_color,
                                        const ViewportSettings &settings) {
  switch (role) {
  case ViewportOverlayStyleRole::SelectionFace:
    return setting_color(settings, ViewportColorSettingId::SelectionFace);
  case ViewportOverlayStyleRole::SelectionWire:
  case ViewportOverlayStyleRole::SelectedSourceWire:
    return setting_color(settings, ViewportColorSettingId::SelectionWire);
  case ViewportOverlayStyleRole::SourceWire:
    return setting_color(settings, ViewportColorSettingId::SourceWire);
  case ViewportOverlayStyleRole::OpenEdge:
    return setting_color(settings, ViewportColorSettingId::OpenEdge);
  case ViewportOverlayStyleRole::DiagnosticEdge:
    return setting_color(settings, ViewportColorSettingId::DiagnosticEdge);
  case ViewportOverlayStyleRole::HoverFace:
    return setting_color(settings, ViewportColorSettingId::HoverFace);
  case ViewportOverlayStyleRole::HoverWire:
    return setting_color(settings, ViewportColorSettingId::HoverWire);
  case ViewportOverlayStyleRole::FacePlaneHoverFace:
    return kOverlayFacePlaneHoverFaceColor;
  case ViewportOverlayStyleRole::FacePlaneHoverWire:
    return kOverlayFacePlaneHoverWireColor;
  case ViewportOverlayStyleRole::RemoveSelectionFace:
    return setting_color(settings, ViewportColorSettingId::RemoveSelectionFace);
  case ViewportOverlayStyleRole::RemoveSelectionWire:
    return setting_color(settings, ViewportColorSettingId::RemoveSelectionWire);
  case ViewportOverlayStyleRole::Vertex:
    return setting_color(settings, ViewportColorSettingId::Vertex);
  case ViewportOverlayStyleRole::SelectedVertex:
    return setting_color(settings, ViewportColorSettingId::SelectedVertex);
  case ViewportOverlayStyleRole::HoverVertex:
    return setting_color(settings, ViewportColorSettingId::HoverVertex);
  case ViewportOverlayStyleRole::RemoveSelectionVertex:
    return setting_color(settings,
                         ViewportColorSettingId::RemoveSelectionVertex);
  case ViewportOverlayStyleRole::GroupBounds:
    return setting_color(settings, ViewportColorSettingId::BoundingBoxWire);
  case ViewportOverlayStyleRole::ToolPlaneFill:
    return kOverlayToolPlaneColor;
  case ViewportOverlayStyleRole::ToolPlaneLine:
    return kOverlayToolPlaneLineColor;
  case ViewportOverlayStyleRole::ToolHandle:
    return kOverlayToolHandleColor;
  case ViewportOverlayStyleRole::CutPreviewKeepFill:
    return kOverlayCutPreviewKeepFillColor;
  case ViewportOverlayStyleRole::CutPreviewKeepBothDiscardFill:
    return kOverlayCutPreviewKeepBothDiscardFillColor;
  case ViewportOverlayStyleRole::CutPreviewDiscardFill:
    return kOverlayCutPreviewDiscardFillColor;
  case ViewportOverlayStyleRole::CutPreviewKeepBothOutline:
    return kOverlayCutPreviewKeepBothOutlineColor;
  case ViewportOverlayStyleRole::CutPreviewDiscardOutline:
    return kOverlayCutPreviewDiscardOutlineColor;
  case ViewportOverlayStyleRole::CutPreviewSeam:
    return kOverlayCutPreviewSeamColor;
  case ViewportOverlayStyleRole::LayerDefault:
  case ViewportOverlayStyleRole::ExplicitColor:
  case ViewportOverlayStyleRole::SnapTargetVertex:
  case ViewportOverlayStyleRole::WireframeBase:
  case ViewportOverlayStyleRole::ToolPreviewFill:
  case ViewportOverlayStyleRole::ToolPreviewLine:
  case ViewportOverlayStyleRole::ToolPreviewSeam:
    return authored_color;
  }
  return authored_color;
}

float ViewportOverlayStyleResolver::line_width_for(
    ViewportOverlayStyleRole role, ViewportOverlayLayer layer,
    float authored_width_pixels, const ViewportSettings &settings) {
  switch (role) {
  case ViewportOverlayStyleRole::SourceWire:
    return settings.float_value(ViewportFloatSettingId::SourceWireLineSize);
  case ViewportOverlayStyleRole::SelectedSourceWire:
    return settings.float_value(
        ViewportFloatSettingId::SelectionFaceWireLineSize);
  case ViewportOverlayStyleRole::SelectionWire:
    return layer == ViewportOverlayLayer::SelectedFaceEdge
               ? settings.float_value(
                     ViewportFloatSettingId::SelectionFaceWireLineSize)
               : settings.float_value(
                     ViewportFloatSettingId::SelectionEdgeLineSize);
  case ViewportOverlayStyleRole::HoverWire:
  case ViewportOverlayStyleRole::RemoveSelectionWire:
  case ViewportOverlayStyleRole::FacePlaneHoverWire:
    return settings.float_value(ViewportFloatSettingId::HoverWireLineSize);
  case ViewportOverlayStyleRole::OpenEdge:
    return settings.float_value(ViewportFloatSettingId::OpenEdgeLineSize);
  case ViewportOverlayStyleRole::DiagnosticEdge:
    return settings.float_value(ViewportFloatSettingId::DiagnosticEdgeLineSize);
  case ViewportOverlayStyleRole::GroupBounds:
    return settings.float_value(
        ViewportFloatSettingId::BoundingBoxWireThickness);
  default:
    return authored_width_pixels;
  }
}

float ViewportOverlayStyleResolver::point_size_for(
    ViewportOverlayStyleRole role, float authored_size_pixels,
    const ViewportSettings &settings) {
  const float vertex_size =
      settings.float_value(ViewportFloatSettingId::VertexSize);
  switch (role) {
  case ViewportOverlayStyleRole::Vertex:
    return vertex_size;
  case ViewportOverlayStyleRole::SelectedVertex:
    return vertex_size +
           settings.float_value(ViewportFloatSettingId::SelectedVertexGrowth);
  case ViewportOverlayStyleRole::HoverVertex:
  case ViewportOverlayStyleRole::RemoveSelectionVertex:
    return vertex_size +
           settings.float_value(ViewportFloatSettingId::HoverVertexGrowth);
  default:
    return authored_size_pixels;
  }
}

std::optional<float> ViewportOverlayStyleResolver::point_outline_growth_for(
    ViewportOverlayStyleRole role,
    std::optional<float> authored_outline_growth_pixels,
    const ViewportSettings &settings) {
  switch (role) {
  case ViewportOverlayStyleRole::SelectedVertex:
  case ViewportOverlayStyleRole::HoverVertex:
  case ViewportOverlayStyleRole::RemoveSelectionVertex:
  case ViewportOverlayStyleRole::SnapTargetVertex:
    return settings.float_value(ViewportFloatSettingId::VertexOutlineSize);
  default:
    return authored_outline_growth_pixels;
  }
}

std::uint32_t
ViewportOverlayStyleResolver::setting_color_rgba(ViewportSettingColor color) {
  return static_cast<std::uint32_t>(normalized_to_byte(color.r)) |
         (static_cast<std::uint32_t>(normalized_to_byte(color.g)) << 8u) |
         (static_cast<std::uint32_t>(normalized_to_byte(color.b)) << 16u) |
         (static_cast<std::uint32_t>(normalized_to_byte(color.a)) << 24u);
}

std::uint32_t
ViewportOverlayStyleResolver::setting_color(const ViewportSettings &settings,
                                            ViewportColorSettingId id) {
  return setting_color_rgba(settings.color(id));
}

} // namespace quader

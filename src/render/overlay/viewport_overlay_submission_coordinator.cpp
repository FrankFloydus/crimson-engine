////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/viewport_overlay_submission_coordinator.hpp"

#include "diagnostics/profile.hpp"
#include "viewport/viewport_settings.hpp"

#include <utility>

namespace quader {
namespace {

void append_overlay_frame(ViewportOverlayFrame &target,
                          const ViewportOverlayFrame &source) {
  target.lines.reserve(target.lines.size() + source.lines.size());
  target.triangles.reserve(target.triangles.size() + source.triangles.size());
  target.points.reserve(target.points.size() + source.points.size());
  target.icons.reserve(target.icons.size() + source.icons.size());
  target.lines.insert(target.lines.end(), source.lines.begin(),
                      source.lines.end());
  target.triangles.insert(target.triangles.end(), source.triangles.begin(),
                          source.triangles.end());
  target.points.insert(target.points.end(), source.points.begin(),
                       source.points.end());
  target.icons.insert(target.icons.end(), source.icons.begin(),
                      source.icons.end());
  if (source.hover_edge_dimension_context.has_value()) {
    target.hover_edge_dimension_context = source.hover_edge_dimension_context;
  }
  target.hover_target_selected =
      target.hover_target_selected || source.hover_target_selected;
}

std::uint64_t overlay_record_count(const ViewportOverlayFrame &frame) {
  return static_cast<std::uint64_t>(frame.lines.size()) +
         static_cast<std::uint64_t>(frame.triangles.size()) +
         static_cast<std::uint64_t>(frame.points.size()) +
         static_cast<std::uint64_t>(frame.icons.size());
}

ViewportOverlaySceneSubmissionStamp scene_submission_stamp(
    const RenderOverlaySelectionSnapshot *snapshot,
    bool scene_overlay_requested) {
  if (!scene_overlay_requested || snapshot == nullptr) {
    return {};
  }
  return {
      .scene_binding = snapshot->scene_binding,
      .scene_revision = snapshot->scene_revision,
      .asset_revision = snapshot->asset_revision,
      .selection_revision = snapshot->selection_revision,
      .visibility_revision = snapshot->visibility_revision,
      .local_visibility_revision = snapshot->local_visibility_revision,
      .transform_revision = snapshot->transform_revision,
      .mesh_binding_revision = snapshot->mesh_binding_revision,
      .render_component_revision = snapshot->render_component_revision,
  };
}

ViewportOverlaySceneSubmissionStamp scene_tool_submission_stamp(
    const RenderOverlaySelectionSnapshot *snapshot,
    bool scene_overlay_requested) {
  ViewportOverlaySceneSubmissionStamp stamp =
      scene_submission_stamp(snapshot, scene_overlay_requested);
  // Scene transforms invalidate the persistent bounds, wire, and marker
  // stream. Pane-local tool geometry has its own typed publication source and
  // must not be resubmitted merely because that persistent stream moved.
  stamp.transform_revision = 0u;
  return stamp;
}

ViewportOverlayFrame build_persistent_frame(
    const ViewportOverlaySubmissionRequest &request,
    const ViewportSettings &settings,
    const ViewportOverlayFrame *selection_overlay_frame,
    bool selection_overlay_visible) {
  ViewportOverlayFrame frame;
  if (selection_overlay_visible && selection_overlay_frame != nullptr) {
    QDR_PROFILE_SCOPE("Viewport overlay copy cached selection frame");
    frame = *selection_overlay_frame;
  }

  const ViewportOverlayStyleRange scene_style_range{
      .triangle_begin = frame.triangles.size(),
      .line_begin = frame.lines.size(),
      .point_begin = frame.points.size(),
  };
  if (request.scene_overlay_requested &&
      request.scene_overlay_snapshot != nullptr) {
    frame = ViewportSceneOverlayBuilder::build_selection_frame(
        std::move(frame), *request.scene_overlay_snapshot,
        request.point_light_gizmos_visible, request.scene_selection_mode,
        false, request.overlays_visible);
    if (request.wireframe_pane) {
      frame = ViewportSceneOverlayBuilder::strip_wireframe_selection_fills(
          std::move(frame), request.scene_selection_mode);
    }
  }
  ViewportOverlayStyleResolver::apply_from(
      frame, settings,
      ViewportOverlayStyleContext{
          .selection_mode = request.style_selection_context,
          .projection = request.camera.projection,
      },
      scene_style_range);
  return frame;
}

} // namespace

ViewportOverlaySubmissionResult ViewportOverlaySubmissionCoordinator::submit(
    ViewportOverlaySubmissionRequest request,
    const ViewportSettings &settings,
    ViewportOverlaySubmissionTarget &target) {
  QDR_PROFILE_FUNCTION();
  ViewportOverlaySubmissionResult result;
  if (request.viewport_index >= panes_.size()) {
    return result;
  }
  const CurveToolPreviewState empty_curve_preview;
  const CurveToolPreviewState &curve_preview =
      request.curve_preview != nullptr ? *request.curve_preview
                                       : empty_curve_preview;
  const CurveToolOverlaySourceStamp curve_source_stamp =
      CurveToolOverlayCache::source_stamp(curve_preview);
  const std::uint64_t style_signature =
      ViewportOverlayStyleResolver::style_signature(settings);
  const bool selection_overlay_visible =
      request.overlays_visible && request.selection_frame_provider != nullptr;
  if (selection_overlay_visible) {
    const StyledSelectionSourceStamp source_stamp{
        .source = request.selection_source,
        .style_signature = style_signature,
    };
    if (!selection_source_.valid || selection_source_.stamp != source_stamp) {
      selection_source_.frame =
          request.selection_frame_provider->build_selection_overlay_frame();
      ViewportOverlayStyleResolver::apply(
          selection_source_.frame, settings,
          ViewportOverlayStyleContext{
              .selection_mode = request.selection_source.selection_context,
          });
      selection_source_.stamp = source_stamp;
      ++selection_source_.publication_revision;
      selection_source_.valid = true;
      result.selection_source_refreshed = true;
    }
  }
  const ViewportOverlayPersistentSubmissionStamp persistent_stamp{
      .selection_overlay_revision =
          selection_overlay_visible && selection_source_.valid
              ? selection_source_.publication_revision
              : 0u,
      .scene = scene_submission_stamp(request.scene_overlay_snapshot,
                                      request.scene_overlay_requested),
      .style_signature = style_signature,
      .scene_selection_mode = request.scene_selection_mode,
      .style_selection_context = request.style_selection_context,
      .projection = request.camera.projection,
      .selection_overlay_visible = selection_overlay_visible,
      .selection_overlays_visible = request.overlays_visible,
      .scene_overlay_requested = request.scene_overlay_requested,
      .wireframe_pane = request.wireframe_pane,
      .point_light_gizmos_visible = request.point_light_gizmos_visible,
  };
  const ViewportOverlayDynamicSubmissionStamp dynamic_stamp{
      .dynamic_tools = request.dynamic_tool_source,
      .volatile_modeling =
          request.overlays_visible
              ? request.volatile_modeling_source
              : ViewportOverlayVolatileModelingSourceStamp{},
      .scene_tools =
          scene_tool_submission_stamp(
              request.scene_overlay_snapshot,
              request.overlays_visible && request.scene_overlay_requested),
      .curve = curve_source_stamp,
      .tool_preview_identity = request.tool_preview_overlay_identity,
      .tool_preview_revision = request.tool_preview_overlay_revision,
      .style_signature = style_signature,
      .style_selection_context = request.style_selection_context,
      .projection = request.camera.projection,
  };

  PaneState &pane = panes_[request.viewport_index];
  bool persistent_changed =
      !pane.persistent_valid || pane.persistent != persistent_stamp;
  const bool dynamic_source_changed =
      !pane.dynamic_valid ||
      pane.dynamic.dynamic_tools != dynamic_stamp.dynamic_tools ||
      pane.dynamic.volatile_modeling != dynamic_stamp.volatile_modeling ||
      pane.dynamic.scene_tools != dynamic_stamp.scene_tools ||
      pane.dynamic.tool_preview_identity !=
          dynamic_stamp.tool_preview_identity ||
      pane.dynamic.tool_preview_revision !=
          dynamic_stamp.tool_preview_revision;
  bool dynamic_changed = !pane.dynamic_valid || pane.dynamic != dynamic_stamp;
  if (!persistent_changed || !dynamic_changed) {
    const ViewportOverlayCameraUpdate camera_update =
        target.update_overlay_cameras(request.viewport_index,
                                      request.layer_mask, request.camera);
    if (!persistent_changed && !camera_update.persistent_available) {
      persistent_changed = true;
      result.persistent_recovered = true;
    }
    if (!dynamic_changed && !camera_update.dynamic_available) {
      dynamic_changed = true;
      result.dynamic_recovered = true;
    }
    if (!persistent_changed && !dynamic_changed) {
      result.camera_only = true;
      result.edge_hud_frame = &pane.dynamic_source_frame;
      result.dynamic_source_signature = pane.dynamic_source_revision;
      return result;
    }
  }

  if (persistent_changed) {
    target.submit_persistent_overlay(
        request.viewport_index, request.layer_mask,
        build_persistent_frame(
            request, settings,
            selection_overlay_visible && selection_source_.valid
                ? &selection_source_.frame
                : nullptr,
            selection_overlay_visible),
        request.camera);
    pane.persistent = persistent_stamp;
    pane.persistent_valid = true;
    result.persistent_submitted = true;
  }

  if (dynamic_source_changed) {
    pane.dynamic_source_frame =
        request.dynamic_tool_frame_provider != nullptr
            ? request.dynamic_tool_frame_provider
                  ->build_dynamic_tool_overlay_frame()
            : ViewportOverlayFrame{};
    if (request.overlays_visible &&
        request.volatile_modeling_overlay_frame != nullptr) {
      append_overlay_frame(pane.dynamic_source_frame,
                           *request.volatile_modeling_overlay_frame);
    }
    if (request.overlays_visible && request.scene_overlay_requested &&
        request.scene_overlay_snapshot != nullptr) {
      append_overlay_frame(pane.dynamic_source_frame,
                           request.scene_overlay_snapshot->tool_overlays);
    }
    if (request.tool_preview_overlay_frame != nullptr) {
      append_overlay_frame(pane.dynamic_source_frame,
                           *request.tool_preview_overlay_frame);
    }
    pane.dynamic_source_revision = ++dynamic_source_publication_revision_;
    ++work_counters_.dynamic_source_compose_count;
    work_counters_.dynamic_source_composed_record_count +=
        overlay_record_count(pane.dynamic_source_frame);
  }

  if (dynamic_changed) {
    // Copy broad semantic sources only after typed source stamps differ or the
    // backend reports a lost retained stream.
    ViewportOverlayFrame dynamic_frame = pane.dynamic_source_frame;
    const ViewportOverlayFrame &curve_overlay_frame =
        curve_overlay_source_.frame(curve_preview);
    append_overlay_frame(dynamic_frame, curve_overlay_frame);
    ViewportOverlayStyleResolver::apply(
        dynamic_frame, settings,
        ViewportOverlayStyleContext{
            .selection_mode = request.style_selection_context,
            .projection = request.camera.projection,
        });
    target.submit_dynamic_overlay(request.viewport_index, request.layer_mask,
                                  std::move(dynamic_frame), request.camera);
    pane.dynamic = dynamic_stamp;
    pane.dynamic_valid = true;
    result.dynamic_submitted = true;
  }

  result.edge_hud_frame = &pane.dynamic_source_frame;
  result.dynamic_source_signature = pane.dynamic_source_revision;
  return result;
}

void ViewportOverlaySubmissionCoordinator::invalidate(
    std::size_t viewport_index) {
  if (viewport_index < panes_.size()) {
    panes_[viewport_index] = {};
  }
}

void ViewportOverlaySubmissionCoordinator::invalidate_all() {
  panes_ = {};
  selection_source_ = {};
  curve_overlay_source_.reset();
}

void ViewportOverlaySubmissionCoordinator::set_active_viewports(
    std::uint8_t viewport_mask) {
  for (std::size_t index = 0u; index < panes_.size(); ++index) {
    const std::uint8_t slot_bit = static_cast<std::uint8_t>(1u << index);
    if ((viewport_mask & slot_bit) == 0u) {
      invalidate(index);
    }
  }
}

ViewportOverlaySubmissionWorkCounters
ViewportOverlaySubmissionCoordinator::work_counters() const {
  ViewportOverlaySubmissionWorkCounters counters = work_counters_;
  counters.curve_overlay_build_count = curve_overlay_source_.build_count();
  return counters;
}

} // namespace quader

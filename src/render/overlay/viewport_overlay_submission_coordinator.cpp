////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/viewport_overlay_submission_coordinator.hpp"

#include "diagnostics/profile.hpp"
#include "render/render_signature_builder.hpp"
#include "viewport/viewport_settings.hpp"

#include <algorithm>
#include <array>
#include <unordered_set>
#include <utility>

namespace quader {
namespace {

bool uses_mesh_outlines(const ViewportOverlaySubmissionRequest &request) {
  return request.overlays_visible &&
         (request.scene_selection_mode == RenderOverlaySelectionMode::Object ||
          request.scene_selection_mode == RenderOverlaySelectionMode::Group) &&
         request.construction_object_id < 0 && request.selected_mesh_outline_snapshot != nullptr;
}

void strip_mesh_selection_wires(ViewportOverlayFrame &frame) {
  std::erase_if(frame.lines, [](const ViewportOverlayLine &line) {
    if (line.source == ViewportOverlaySource::HierarchySelection) return true;
    if (line.element.object_id < 0 ||
        (line.source != ViewportOverlaySource::Selection &&
         line.source != ViewportOverlaySource::ComponentSelection)) return false;
    const auto role = line.style_role;
    return role == ViewportOverlayStyleRole::SelectionWire ||
           role == ViewportOverlayStyleRole::SelectedSourceWire ||
           role == ViewportOverlayStyleRole::SourceWire ||
           (role == ViewportOverlayStyleRole::LayerDefault &&
            (line.layer == ViewportOverlayLayer::SourceWire ||
             line.layer == ViewportOverlayLayer::SelectedEdge));
  });
}

struct ComponentWireKey {
  std::array<int, 3u> element;
  std::array<float, 3u> start;
  std::array<float, 3u> end;

  bool operator==(const ComponentWireKey &) const = default;
};

struct ComponentWireKeyHash {
  std::size_t operator()(const ComponentWireKey &key) const {
    return static_cast<std::size_t>(RenderSignatureBuilder()
        .add_value(static_cast<std::uint32_t>(key.element[0]))
        .add_value(static_cast<std::uint32_t>(key.element[1]))
        .add_value(static_cast<std::uint32_t>(key.element[2]))
        .value());
  }
};

ComponentWireKey component_wire_key(const ViewportOverlayLine &line) {
  ComponentWireKey key{
      {line.element.object_id,
       std::min(line.element.edge_a, line.element.edge_b),
       std::max(line.element.edge_a, line.element.edge_b)},
      {line.start.x, line.start.y, line.start.z},
      {line.end.x, line.end.y, line.end.z},
  };
  if (key.end < key.start) std::swap(key.start, key.end);
  return key;
}

void strip_component_source_wires_under_selection(ViewportOverlayFrame &frame) {
  const auto has_component_edge = [](const ViewportOverlayLine &line) {
    return line.source == ViewportOverlaySource::ComponentSelection &&
           line.element.object_id > 0 && line.element.edge_a > 0 &&
           line.element.edge_b > 0 &&
           line.element.edge_a != line.element.edge_b;
  };
  std::unordered_set<ComponentWireKey, ComponentWireKeyHash> selected_edges;
  for (const ViewportOverlayLine &line : frame.lines) {
    if (has_component_edge(line) && (line.color >> 24u) == 255u &&
        (line.layer == ViewportOverlayLayer::SelectedEdge ||
         line.layer == ViewportOverlayLayer::SelectedFaceEdge)) {
      selected_edges.insert(component_wire_key(line));
    }
  }
  if (selected_edges.empty()) return;

  // Both component wires use the same depth-tested ribbon. Drawing cyan
  // beneath an opaque yellow selection contaminates its antialiased pixels,
  // especially at one pixel wide. Keep the complete semantic source frame,
  // but submit only the selected ribbon for this exact edge occurrence.
  std::erase_if(frame.lines, [&](const ViewportOverlayLine &line) {
    return line.layer == ViewportOverlayLayer::SourceWire &&
           has_component_edge(line) &&
           selected_edges.contains(component_wire_key(line));
  });
}

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
    ViewportOverlayStyleResolver::apply(
        frame, settings,
        ViewportOverlayStyleContext{
            .selection_mode = request.selection_source.selection_context,
            .projection = request.camera.projection,
            .raster_scale = request.raster_scale,
            .construction_object_id = request.construction_object_id,
        });
  }

  const ViewportOverlayStyleRange scene_style_range{
      .triangle_begin = frame.triangles.size(),
      .line_begin = frame.lines.size(),
      .point_begin = frame.points.size(),
      .icon_begin = frame.icons.size(),
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
          .raster_scale = request.raster_scale,
          .construction_object_id = request.construction_object_id,
      },
      scene_style_range);
  if (uses_mesh_outlines(request)) strip_mesh_selection_wires(frame);
  strip_component_source_wires_under_selection(frame);
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
  request.raster_scale =
      viewport_overlay_sanitized_raster_scale(request.raster_scale);
  const std::uint64_t style_signature =
      RenderSignatureBuilder(ViewportOverlayStyleResolver::style_signature(
          settings, request.raster_scale))
          .add_value(request.construction_object_id)
          .add_bool(uses_mesh_outlines(request))
          .value();
  const ViewportOverlayFrame &mesh_outline = mesh_outline_source_.frame(
      uses_mesh_outlines(request) ? request.selected_mesh_outline_snapshot : nullptr,
      request.camera, request.viewport_index,
      uses_mesh_outlines(request) && request.scene_overlay_requested
          ? request.scene_overlay_snapshot : nullptr);
  const bool selection_overlay_visible =
      request.overlays_visible && request.selection_frame_provider != nullptr;
  if (selection_overlay_visible) {
    const ViewportOverlaySelectionSourceStamp &source_stamp =
        request.selection_source;
    if (!selection_source_.valid || selection_source_.stamp != source_stamp) {
      selection_source_.frame =
          request.selection_frame_provider->build_selection_overlay_frame();
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
      .raster_scale = request.raster_scale,
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
      .mesh_outline_revision = mesh_outline_source_.revision(request.viewport_index),
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
      .raster_scale = request.raster_scale,
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
          dynamic_stamp.tool_preview_revision ||
      pane.dynamic.raster_scale != dynamic_stamp.raster_scale;
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
    if (uses_mesh_outlines(request)) strip_mesh_selection_wires(dynamic_frame);
    append_overlay_frame(dynamic_frame, mesh_outline);
    const ViewportOverlayFrame &curve_overlay_frame =
        curve_overlay_source_.frame(curve_preview);
    append_overlay_frame(dynamic_frame, curve_overlay_frame);
    ViewportOverlayStyleResolver::apply(
        dynamic_frame, settings,
        ViewportOverlayStyleContext{
            .selection_mode = request.style_selection_context,
            .projection = request.camera.projection,
            .raster_scale = request.raster_scale,
            .construction_object_id = request.construction_object_id,
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
  mesh_outline_source_.reset();
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

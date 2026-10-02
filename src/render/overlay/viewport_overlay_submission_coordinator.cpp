////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/viewport_overlay_submission_coordinator.hpp"

#include "diagnostics/profile.hpp"
#include "math/glm_bridge.hpp"
#include "render/render_signature_builder.hpp"
#include "render/render_scene_snapshot.hpp"
#include "render/overlay/overlay_camera_projector.hpp"
#include "render/overlay/viewport_overlay_dash.hpp"
#include "render/overlay/viewport_selection_dimension_geometry.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"
#include "viewport/viewport_settings.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>
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

void strip_wireframe_point_occluders(ViewportOverlayFrame &frame) {
  // Wireframe panes hide mesh surfaces, so those surfaces must not CPU-cull
  // vertex markers either. Keep marker depth testing and its distance-safe
  // shader bias intact; only remove the hidden-surface visibility metadata.
  std::erase_if(frame.triangles, [](const ViewportOverlayTriangle &triangle) {
    return triangle.layer == ViewportOverlayLayer::SourceWireDepthStamp;
  });
}

void strip_mesh_selection_wires(ViewportOverlayFrame &frame) {
  std::erase_if(frame.lines, [](const ViewportOverlayLine &line) {
    if (line.source == ViewportOverlaySource::HierarchySelection) return true;
    if (line.element.object_id < 0 ||
        (line.source != ViewportOverlaySource::Selection &&
         line.source != ViewportOverlaySource::ComponentSelection)) return false;
    const auto role = line.style_role;
    return role == ViewportOverlayStyleRole::SelectionWire ||
           role == ViewportOverlayStyleRole::ActiveSelectionWire ||
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

void include_dimension_point(std::optional<ViewportSelectionDimensionBounds> &bounds,
                             QVec3 point) {
  if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) return;
  if (!bounds) {
    bounds = ViewportSelectionDimensionBounds{point, point};
    return;
  }
  bounds->min = {std::min(bounds->min.x, point.x),
                 std::min(bounds->min.y, point.y),
                 std::min(bounds->min.z, point.z)};
  bounds->max = {std::max(bounds->max.x, point.x),
                 std::max(bounds->max.y, point.y),
                 std::max(bounds->max.z, point.z)};
}

bool same_dimension_bounds(const std::optional<ViewportSelectionDimensionBounds> &a,
                           const std::optional<ViewportSelectionDimensionBounds> &b) {
  if (a.has_value() != b.has_value()) return false;
  return !a || (a->min.x == b->min.x && a->min.y == b->min.y && a->min.z == b->min.z &&
                a->max.x == b->max.x && a->max.y == b->max.y && a->max.z == b->max.z);
}

// Clip before dash subdivision so near-plane and offscreen edges cannot create
// unbounded dash geometry. Parameters returned here remain screen-linear.
bool clip_dimension_screen_edge(QVec2 start, QVec2 end, QVec2 viewport,
                                float &start_t, float &end_t) {
  const QVec2 delta = end - start;
  start_t = 0.0f;
  end_t = 1.0f;
  const auto clip = [&](float denominator, float numerator) {
    if (denominator == 0.0f) return numerator >= 0.0f;
    const float t = numerator / denominator;
    if (denominator < 0.0f) start_t = std::max(start_t, t);
    else end_t = std::min(end_t, t);
    return start_t <= end_t;
  };
  return clip(-delta.x, start.x) && clip(delta.x, viewport.x - start.x) &&
         clip(-delta.y, start.y) && clip(delta.y, viewport.y - start.y);
}

void append_dimension_dashes(ViewportOverlayFrame &frame,
                             const OverlayCameraProjector &projector,
                             QVec3 start, QVec3 end, float raster_scale) {
  const auto &camera = projector.camera();
  const float start_depth = -projector.view_point_for(start).z;
  const float end_depth = -projector.view_point_for(end).z;
  const float depth_delta = end_depth - start_depth;
  float start_t = 0.0f;
  float end_t = 1.0f;
  if (depth_delta == 0.0f) {
    if (start_depth < camera.near_plane || start_depth > camera.far_plane) return;
  } else {
    const float near_t = (camera.near_plane - start_depth) / depth_delta;
    const float far_t = (camera.far_plane - start_depth) / depth_delta;
    start_t = std::max(start_t, std::min(near_t, far_t));
    end_t = std::min(end_t, std::max(near_t, far_t));
    if (start_t >= end_t) return;
  }
  const QVec3 delta = end - start;
  const QVec3 clipped_start = start + delta * start_t;
  const QVec3 clipped_end = start + delta * end_t;
  const auto view_start = projector.view_point_for(clipped_start);
  const auto view_end = projector.view_point_for(clipped_end);
  const auto screen_start = projector.project(view_start);
  const auto screen_end = projector.project(view_end);
  if (!screen_start.valid || !screen_end.valid ||
      !std::isfinite(screen_start.pixel.x) || !std::isfinite(screen_start.pixel.y) ||
      !std::isfinite(screen_end.pixel.x) || !std::isfinite(screen_end.pixel.y)) return;
  if (!clip_dimension_screen_edge(screen_start.pixel, screen_end.pixel,
                                  camera.viewport_size, start_t, end_t)) return;
  const QVec2 screen_delta = screen_end.pixel - screen_start.pixel;
  const QVec3 clipped_delta = clipped_end - clipped_start;
  const auto world_point = [&](float screen_t) {
    // Perspective projection is linear in reciprocal depth, not world distance.
    const float world_t = camera.projection == ViewportOverlayProjection::Perspective
        ? screen_t * view_start.z /
              ((1.0f - screen_t) * view_end.z + screen_t * view_start.z)
        : screen_t;
    return clipped_start + clipped_delta * world_t;
  };
  append_viewport_overlay_dashed_edge(
      screen_start.pixel + screen_delta * start_t,
      screen_start.pixel + screen_delta * end_t, true,
      [&](QVec2, QVec2, float dash_start, float dash_end) {
        frame.lines.push_back({
            .layer = ViewportOverlayLayer::GroupBounds,
            .start = world_point(start_t + (end_t - start_t) * dash_start),
            .end = world_point(start_t + (end_t - start_t) * dash_end),
            .style_role = ViewportOverlayStyleRole::GroupBounds,
            .source = ViewportOverlaySource::SelectionDimensions,
        });
      }, raster_scale);
}

ViewportOverlayFrame build_selection_dimension_frame(
    const ViewportSelectionDimensionBounds &bounds,
    const ViewportOverlayCamera &camera, float raster_scale) {
  ViewportOverlayFrame frame;
  const OverlayCameraProjector projector(camera);
  const auto spans = viewport_selection_dimension_spans(
      bounds, camera, viewport_host_defaults().map_units_per_world_unit);
  const std::array<float, 3u> minimum{bounds.min.x, bounds.min.y, bounds.min.z};
  const std::array<float, 3u> maximum{bounds.max.x, bounds.max.y, bounds.max.z};
  const auto same_point = [](QVec3 a, QVec3 b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
  };
  for (std::size_t axis = 0u; axis < 3u; ++axis) {
    if (minimum[axis] == maximum[axis]) continue;
    for (std::size_t corner = 0u; corner < 8u; ++corner) {
      if ((corner & (1u << axis)) != 0u) continue;
      bool duplicate = false;
      for (std::size_t fixed_axis = 0u; fixed_axis < 3u; ++fixed_axis) {
        duplicate = duplicate || (minimum[fixed_axis] == maximum[fixed_axis] &&
                                 (corner & (1u << fixed_axis)) != 0u);
      }
      if (duplicate) continue;
      const QVec3 start{(corner & 1u) ? maximum[0] : minimum[0],
                        (corner & 2u) ? maximum[1] : minimum[1],
                        (corner & 4u) ? maximum[2] : minimum[2]};
      QVec3 end = start;
      if (axis == 0u) end.x = maximum[0];
      if (axis == 1u) end.y = maximum[1];
      if (axis == 2u) end.z = maximum[2];
      if (spans[axis] &&
          ((same_point(spans[axis]->start, start) && same_point(spans[axis]->end, end)) ||
           (same_point(spans[axis]->start, end) && same_point(spans[axis]->end, start)))) {
        continue;
      }
      append_dimension_dashes(frame, projector, start, end, raster_scale);
    }
  }
  constexpr std::array kAxisColors{kOverlayToolAxisXColor, kOverlayToolAxisYColor,
                                    kOverlayToolAxisZColor};
  for (std::size_t axis = 0u; axis < spans.size(); ++axis) {
    if (!spans[axis]) continue;
    frame.lines.push_back({
        .layer = ViewportOverlayLayer::GroupBounds,
        .start = spans[axis]->start,
        .end = spans[axis]->end,
        .color = kAxisColors[axis],
        .width_pixels = kOverlaySelectionDimensionLineWidthPixels,
        .style_role = ViewportOverlayStyleRole::ExplicitColor,
        .source = ViewportOverlaySource::SelectionDimensions,
    });
  }
  return frame;
}

void strip_replaced_selection_cages(ViewportOverlayFrame &frame,
                                    bool keep_scene_group_cages) {
  std::erase_if(frame.lines, [keep_scene_group_cages](const ViewportOverlayLine &line) {
    return (!keep_scene_group_cages && line.source == ViewportOverlaySource::GroupBounds) ||
           (line.source == ViewportOverlaySource::Tool &&
            line.layer == ViewportOverlayLayer::GroupBounds &&
            line.style_role == ViewportOverlayStyleRole::GroupBounds);
  });
}

bool dimension_selection_source(ViewportOverlaySource source) {
  return source == ViewportOverlaySource::Selection ||
         source == ViewportOverlaySource::ComponentSelection;
}

constexpr auto kArrayDimensionSourceDependencies = RenderSourceDependencyMask::from({
    RenderSourceDependency::SceneBinding, RenderSourceDependency::VisibilityRevision,
    RenderSourceDependency::LocalVisibilityRevision, RenderSourceDependency::TransformRevision,
    RenderSourceDependency::MeshBindingRevision, RenderSourceDependency::RenderComponentRevision,
    RenderSourceDependency::LayerRevision, RenderSourceDependency::AssetRevision,
    RenderSourceDependency::InstanceEditScope});

std::optional<ViewportSelectionDimensionBounds> array_original_dimension_bounds(
    const ViewportOverlaySubmissionRequest &request,
    ViewportOverlaySubmissionWorkCounters &work) {
  std::optional<ViewportSelectionDimensionBounds> bounds;
  const auto *scene = request.array_dimension_scene_snapshot;
  if (scene == nullptr) return bounds;
  const auto append_mesh = [&](const RenderMeshAssetSnapshot &mesh, const QMat4 &world) {
    for (const auto &vertex : mesh.vertices) {
      include_dimension_point(bounds, transform_point(world, vertex.position));
    }
    work.selection_dimension_source_record_count += mesh.vertices.size();
  };
  for (const auto &preview : request.selection_dimension_preview_meshes) {
    if (preview.suppress_source) continue;
    const EntityId occurrence = preview.visibility_entity != kInvalidEntityId
        ? preview.visibility_entity : preview.source_entity;
    for (const auto &mesh : scene->meshes) {
      if (mesh.entity_id == preview.source_entity) append_mesh(mesh.mesh, mesh.world_transform);
    }
    for (const auto &primitive : scene->instance_primitives) {
      if (primitive.member_entity != kInvalidEntityId &&
          primitive.member_entity != preview.source_entity) continue;
      for (const auto &instance : primitive.occurrences) {
        if (instance.entity_id == occurrence) append_mesh(primitive.mesh, instance.world_transform);
      }
    }
  }
  return bounds;
}

std::optional<ViewportSelectionDimensionBounds> array_dimension_bounds(
    const ViewportOverlaySubmissionRequest &request,
    std::optional<ViewportSelectionDimensionBounds> bounds,
    ViewportOverlaySubmissionWorkCounters &work) {
  for (const auto &preview : request.selection_dimension_preview_meshes) {
    const auto &vertices = preview.mesh.geometry().vertices;
    const bool dynamic = preview.dynamic_positions && preview.dynamic_position_payload &&
        preview.dynamic_position_payload->size() == vertices.size();
    const auto append_at = [&](const QMat4 &world) {
      for (std::size_t index = 0u; index < vertices.size(); ++index) {
        include_dimension_point(bounds, transform_point(world, dynamic
            ? (*preview.dynamic_position_payload)[index] : vertices[index].position));
      }
      work.selection_dimension_source_record_count += vertices.size();
    };
    if (preview.instance_transforms.empty()) append_at(preview.transform);
    else for (const auto &world : preview.instance_transforms) append_at(world);
  }
  for (const auto &curve : request.selection_dimension_preview_curves) {
    if (!curve.selection_dimension_bounds) continue;
    include_dimension_point(bounds, curve.selection_dimension_bounds->min);
    include_dimension_point(bounds, curve.selection_dimension_bounds->max);
    ++work.selection_dimension_source_record_count;
  }
  // Lights and empties contribute their authored positions, not display-sized
  // icons, attenuation radii, or conservative rotated group cages.
  for (const auto &light : request.array_dimension_preview_lights) {
    for (const auto &world : light.instance_transforms) {
      include_dimension_point(bounds, transform_point(world, {}));
      ++work.selection_dimension_source_record_count;
    }
  }
  for (const auto &group : request.selection_dimension_preview_groups) {
    // A nondegenerate cage represents child geometry already measured above.
    // Its pivot may lie outside that geometry and is not an additional extent.
    if (group.local_min.x != group.local_max.x || group.local_min.y != group.local_max.y ||
        group.local_min.z != group.local_max.z) continue;
    for (const auto &world : group.instance_transforms) {
      include_dimension_point(bounds, transform_point(world, group.local_min));
      ++work.selection_dimension_source_record_count;
    }
  }
  return bounds;
}

std::optional<ViewportSelectionDimensionBounds> selection_dimension_bounds(
    const ViewportOverlaySubmissionRequest &request,
    const ViewportOverlayFrame *selection,
    const RenderOverlaySelectionSnapshot *scene,
    ViewportOverlaySubmissionWorkCounters &work) {
  QDR_PROFILE_SCOPE("Selection dimensions update object bounds");
  std::optional<ViewportSelectionDimensionBounds> bounds;
  std::unordered_map<const RenderToolPreviewMeshRecord *, ViewportSelectionDimensionBounds>
      preview_bounds;
  for (const RenderToolPreviewMeshRecord &preview : request.selection_dimension_preview_meshes) {
    const auto &vertices = preview.mesh.geometry().vertices;
    const bool dynamic = preview.dynamic_positions && preview.dynamic_position_payload &&
                         preview.dynamic_position_payload->size() == vertices.size();
    if (preview.suppress_source) {
      std::optional<ViewportSelectionDimensionBounds> candidate;
      const auto append_at = [&](const QMat4 &world) {
        for (std::size_t index = 0u; index < vertices.size(); ++index) {
          include_dimension_point(candidate, transform_point(world, dynamic
              ? (*preview.dynamic_position_payload)[index] : vertices[index].position));
        }
        work.selection_dimension_source_record_count += vertices.size();
      };
      if (preview.instance_transforms.empty()) append_at(preview.transform);
      else for (const QMat4 &world : preview.instance_transforms) append_at(world);
      if (candidate) preview_bounds.emplace(&preview, *candidate);
    }
  }
  const auto append_preview = [&](const RenderToolPreviewMeshRecord &preview) {
    const auto found = preview_bounds.find(&preview);
    if (found == preview_bounds.end()) return false;
    include_dimension_point(bounds, found->second.min);
    include_dimension_point(bounds, found->second.max);
    return true;
  };
  const auto append_entity_preview = [&](EntityId entity) {
    bool replaced = false;
    for (const auto &[preview, candidate] : preview_bounds) {
      if (preview->source_entity == entity || preview->visibility_entity == entity) {
        include_dimension_point(bounds, candidate.min);
        include_dimension_point(bounds, candidate.max);
        replaced = true;
      }
    }
    return replaced;
  };
  const auto leaf_preview_matches = [](const RenderToolPreviewMeshRecord &preview,
                                       EntityId entity, EntityId occurrence,
                                       EntityId definition, EntityId member) {
    if (occurrence != kInvalidEntityId) {
      return (preview.visibility_entity == occurrence ||
              preview.source_entity == occurrence ||
              preview.visibility_entity == kInvalidEntityId) &&
             (preview.source_entity == member || preview.source_entity == definition ||
              preview.source_entity == occurrence);
    }
    return preview.source_entity == entity;
  };
  const auto append_leaf = [&](EntityId entity, EntityId occurrence, EntityId definition,
                                EntityId member, const QMat4 &world,
                                std::span<const MeshVertex> vertices) {
    bool replaced = false;
    for (const auto &[preview, candidate] : preview_bounds) {
      if (leaf_preview_matches(*preview, entity, occurrence, definition, member)) {
        include_dimension_point(bounds, candidate.min);
        include_dimension_point(bounds, candidate.max);
        replaced = true;
      }
    }
    if (!replaced) {
      for (const auto &vertex : vertices) {
        include_dimension_point(bounds, transform_point(world, vertex.position));
      }
      work.selection_dimension_source_record_count += vertices.size();
    }
  };
  const auto curve_preview = [&](EntityId entity)
      -> const ViewportSelectionDimensionBounds * {
    for (const auto &preview : request.selection_dimension_preview_curves) {
      if (preview.source_entity == entity && preview.suppress_source &&
          preview.selection_dimension_bounds) {
        return &*preview.selection_dimension_bounds;
      }
    }
    return nullptr;
  };
  if (scene != nullptr) {
    for (const RenderOverlayBoundsSnapshot &selected : scene->selected_mesh_bounds) {
      bool member_preview = false;
      const auto *selected_objects = request.selected_mesh_outline_snapshot;
      if (selected_objects != nullptr && !preview_bounds.empty()) {
        for (const auto &member : selected_objects->instance_selection_fills) {
          if (member.occurrence != selected.entity) continue;
          for (const auto &[preview, candidate] : preview_bounds) {
            if (leaf_preview_matches(*preview, member.occurrence, member.occurrence,
                                      member.definition, member.member)) member_preview = true;
          }
        }
      }
      if (member_preview) {
        for (const auto &member : selected_objects->instance_selection_fills) {
          if (member.occurrence == selected.entity) {
            append_leaf(member.occurrence, member.occurrence, member.definition,
                        member.member, member.world_transform, member.vertices);
          }
        }
        continue;
      }
      if (!append_entity_preview(selected.entity)) {
        include_dimension_point(bounds, selected.min);
        include_dimension_point(bounds, selected.max);
      }
    }
    for (const RenderOverlayBoundsSnapshot &group : scene->group_bounds) {
      bool replaced = false;
      const auto *suppression = request.selection_dimension_preview_suppression;
      const bool suppressed = suppression != nullptr &&
          std::ranges::find(suppression->entities, group.entity) != suppression->entities.end();
      if (!preview_bounds.empty() || !request.selection_dimension_preview_curves.empty()) {
        bool leaf_preview = false;
        for (const auto &mesh : scene->hierarchy_selection_wires) {
          if (mesh.selection_root != group.entity) continue;
          for (const auto &[preview, candidate] : preview_bounds) {
            if (leaf_preview_matches(*preview, mesh.entity, mesh.occurrence,
                                      mesh.definition, mesh.member)) leaf_preview = true;
          }
        }
        for (const auto &curve : scene->curves) {
          if (curve.selection_root == group.entity && curve_preview(curve.entity) != nullptr) {
            leaf_preview = true;
          }
        }
        if (leaf_preview) {
          for (const auto &mesh : scene->hierarchy_selection_wires) {
            if (mesh.selection_root == group.entity) {
              append_leaf(mesh.entity, mesh.occurrence, mesh.definition, mesh.member,
                          mesh.world_transform, mesh.vertices);
            }
          }
          for (const auto &curve : scene->curves) {
            if (curve.selection_root != group.entity) continue;
            const auto *current = curve_preview(curve.entity);
            if (current == nullptr && curve.selection_dimension_bounds) {
              current = &*curve.selection_dimension_bounds;
            }
            if (current != nullptr) {
              include_dimension_point(bounds, current->min);
              include_dimension_point(bounds, current->max);
            }
          }
          replaced = true;
        }
      }
      if (!replaced && suppressed) {
        for (const auto &marker : request.selection_dimension_preview_groups) {
          if (marker.source_entity != group.entity) continue;
          for (const QMat4 &world : marker.instance_transforms) {
            for (unsigned corner = 0; corner < 8; ++corner) {
              include_dimension_point(bounds, transform_point(world, {
                  (corner & 1u) ? marker.local_max.x : marker.local_min.x,
                  (corner & 2u) ? marker.local_max.y : marker.local_min.y,
                  (corner & 4u) ? marker.local_max.z : marker.local_min.z}));
            }
            replaced = true;
          }
        }
      }
      if (!replaced) {
        include_dimension_point(bounds, group.min);
        include_dimension_point(bounds, group.max);
      }
    }
    work.selection_dimension_source_record_count +=
        scene->selected_mesh_bounds.size() + scene->group_bounds.size();
  }
  if (selection != nullptr) {
    std::unordered_set<int> replaced_objects;
    const auto append_point = [&](int object, QVec3 point) {
      if (replaced_objects.contains(object)) return;
      bool replaced = false;
      for (const auto &preview : request.selection_dimension_preview_meshes) {
        if (preview.source_object_id == object && append_preview(preview)) replaced = true;
      }
      if (replaced) {
        replaced_objects.insert(object);
        return;
      }
      include_dimension_point(bounds, point);
    };
    for (const auto &line : selection->lines) {
      if (!dimension_selection_source(line.source) ||
          (line.layer != ViewportOverlayLayer::SelectedFaceEdge &&
           line.layer != ViewportOverlayLayer::SelectedEdge)) continue;
      append_point(line.element.object_id, line.start);
      append_point(line.element.object_id, line.end);
    }
    for (const auto &triangle : selection->triangles) {
      if (!dimension_selection_source(triangle.source) ||
          (triangle.layer != ViewportOverlayLayer::SelectedVisibleFace &&
           triangle.layer != ViewportOverlayLayer::SelectedXrayFace &&
           triangle.layer != ViewportOverlayLayer::SelectedFaceMask)) continue;
      append_point(triangle.element.object_id, triangle.a);
      append_point(triangle.element.object_id, triangle.b);
      append_point(triangle.element.object_id, triangle.c);
    }
    work.selection_dimension_source_record_count +=
        selection->lines.size() + selection->triangles.size();
  }
  return bounds;
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
    bool selection_overlay_visible, bool dimension_box_visible) {
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
  if (request.wireframe_pane) strip_wireframe_point_occluders(frame);
  strip_component_source_wires_under_selection(frame);
  if (dimension_box_visible)
    strip_replaced_selection_cages(frame, request.scene_selection_mode == RenderOverlaySelectionMode::Group);
  return frame;
}

} // namespace

std::optional<ViewportSelectionDimensionBounds>
ViewportOverlaySubmissionCoordinator::component_selection_dimension_bounds(
    const ViewportOverlaySubmissionRequest &request,
    const ViewportOverlayFrame *selection, std::uint64_t selection_revision) {
  QDR_PROFILE_SCOPE("Selection dimensions update component bounds");
  const auto previews = request.selection_dimension_preview_meshes;
  const auto eligible = [](const RenderToolPreviewMeshRecord &preview) {
    return preview.source_object_id >= 0 && preview.dynamic_positions &&
           preview.dynamic_position_payload &&
           preview.dynamic_position_payload->size() ==
               preview.mesh.geometry().vertices.size();
  };
  bool mapping_matches = component_dimensions_.valid &&
      component_dimensions_.selection_revision == selection_revision &&
      component_dimensions_.previews.size() == previews.size();
  if (mapping_matches) {
    QDR_PROFILE_SCOPE("Selection dimensions validate preview mapping");
    for (std::size_t index = 0u; index < previews.size(); ++index) {
      const auto &preview = previews[index];
      auto &retained = component_dimensions_.previews[index];
      const bool dynamic = eligible(preview);
      if (retained.object_id != preview.source_object_id ||
          static_cast<bool>(retained.geometry) != dynamic) {
        mapping_matches = false;
        break;
      }
      if (!dynamic || retained.geometry.get() == &preview.mesh.geometry()) continue;
      const auto &vertices = preview.mesh.geometry().vertices;
      if (retained.geometry->vertices.size() != vertices.size() ||
          !std::equal(vertices.begin(), vertices.end(),
                      retained.geometry->vertices.begin(),
                      [](const MeshVertex &left, const MeshVertex &right) {
                        return left.source_vertex_id == right.source_vertex_id;
                      })) {
        mapping_matches = false;
        break;
      }
      retained.geometry = preview.mesh.geometry_payload();
    }
  }
  if (!mapping_matches) {
    QDR_PROFILE_SCOPE("Selection dimensions build preview mapping");
    ++work_counters_.selection_dimension_mapping_build_count;
    component_dimensions_ = {};
    component_dimensions_.selection_revision = selection_revision;
    component_dimensions_.previews.reserve(previews.size());
    struct SelectedPoint final {
      std::optional<ViewportSelectionDimensionBounds> fallback;
      std::optional<ComponentDimensionPositionBinding> binding;
    };
    std::unordered_map<std::uint64_t, SelectedPoint> selected_points;
    const auto point_key = [](int object, int vertex) {
      return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(object)) << 32u) |
             static_cast<std::uint32_t>(vertex);
    };
    const auto append_point = [&](int object, int vertex, QVec3 point) {
      if (previews.empty()) {
        include_dimension_point(component_dimensions_.fallback_bounds, point);
        return;
      }
      include_dimension_point(selected_points[point_key(object, vertex)].fallback,
                              point);
    };
    if (selection != nullptr) {
      for (const auto &line : selection->lines) {
        if (!dimension_selection_source(line.source) ||
            line.layer != ViewportOverlayLayer::SelectedFaceEdge) continue;
        append_point(line.element.object_id, line.element.edge_a, line.start);
        append_point(line.element.object_id, line.element.edge_b, line.end);
      }
      for (const auto &triangle : selection->triangles) {
        if (!dimension_selection_source(triangle.source) ||
            (triangle.layer != ViewportOverlayLayer::SelectedVisibleFace &&
             triangle.layer != ViewportOverlayLayer::SelectedXrayFace &&
             triangle.layer != ViewportOverlayLayer::SelectedFaceMask)) continue;
        append_point(triangle.element.object_id, triangle.element.vertex_id, triangle.a);
        append_point(triangle.element.object_id, triangle.element.edge_a, triangle.b);
        append_point(triangle.element.object_id, triangle.element.edge_b, triangle.c);
      }
      work_counters_.selection_dimension_source_record_count +=
          selection->lines.size() + selection->triangles.size();
    }
    for (std::size_t preview_index = 0u; preview_index < previews.size(); ++preview_index) {
      const auto &preview = previews[preview_index];
      const bool dynamic = eligible(preview);
      component_dimensions_.previews.push_back({
          .object_id = preview.source_object_id,
          .geometry = dynamic ? preview.mesh.geometry_payload() : nullptr});
      if (!dynamic) continue;
      const auto &vertices = preview.mesh.geometry().vertices;
      for (std::size_t vertex_index = 0u; vertex_index < vertices.size(); ++vertex_index) {
        const auto found = selected_points.find(point_key(preview.source_object_id,
            static_cast<int>(vertices[vertex_index].source_vertex_id)));
        if (found != selected_points.end() && !found->second.binding) {
          // Compiled corners and repeated preview records use the first match,
          // exactly as the previous object/vertex try_emplace position map did.
          found->second.binding = ComponentDimensionPositionBinding{
              .preview_index = preview_index, .vertex_index = vertex_index};
        }
      }
      work_counters_.selection_dimension_mapping_source_record_count += vertices.size();
    }
    component_dimensions_.positions.reserve(selected_points.size());
    for (const auto &[key, point] : selected_points) {
      (void)key;
      if (point.binding) {
        component_dimensions_.positions.push_back(*point.binding);
      } else if (point.fallback) {
        include_dimension_point(component_dimensions_.fallback_bounds, point.fallback->min);
        include_dimension_point(component_dimensions_.fallback_bounds, point.fallback->max);
      }
    }
    component_dimensions_.valid = true;
  }
  auto bounds = component_dimensions_.fallback_bounds;
  {
    QDR_PROFILE_SCOPE("Selection dimensions evaluate mapped positions");
    for (const auto &binding : component_dimensions_.positions) {
      const auto &preview = previews[binding.preview_index];
      include_dimension_point(bounds, transform_point(preview.transform,
          (*preview.dynamic_position_payload)[binding.vertex_index]));
    }
  }
  work_counters_.selection_dimension_position_read_count +=
      component_dimensions_.positions.size();
  work_counters_.selection_dimension_source_record_count +=
      component_dimensions_.positions.size();
  return bounds;
}

ViewportOverlaySubmissionResult ViewportOverlaySubmissionCoordinator::submit(
    ViewportOverlaySubmissionRequest request,
    const ViewportSettings &settings,
    ViewportOverlaySubmissionTarget &target) {
  QDR_PROFILE_FUNCTION();
  ViewportOverlaySubmissionResult result;
  if (request.viewport_index >= panes_.size()) {
    return result;
  }
  PaneState &pane = panes_[request.viewport_index];
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
          .add_bool(request.gpu_mesh_outlines)
          .value();
  const ViewportOverlayFrame empty_mesh_outline;
  const ViewportOverlayFrame &mesh_outline = request.gpu_mesh_outlines
      ? empty_mesh_outline : mesh_outline_source_.frame(
      uses_mesh_outlines(request) && !request.gpu_mesh_outlines
          ? request.selected_mesh_outline_snapshot : nullptr,
      request.camera, request.viewport_index,
      uses_mesh_outlines(request) && !request.gpu_mesh_outlines && request.scene_overlay_requested
          ? request.scene_overlay_snapshot : nullptr,
      viewport_overlay_line_width_pixels(ViewportOverlayLayer::SelectedEdge,
          std::max(settings.float_value(ViewportFloatSettingId::SelectionEdgeLineSize),
                   settings.float_value(ViewportFloatSettingId::ActiveSelectionLineSize))) * request.raster_scale);
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
  const bool dimensions_visible = request.overlays_visible &&
      request.array_dimensions_mode == ViewportArrayDimensionsMode::Inactive &&
      request.selection_dimensions_visible &&
      (request.scene_selection_mode != RenderOverlaySelectionMode::Component ||
       request.style_selection_context == ViewportOverlaySelectionContext::Face);
  if (dimensions_visible) {
    const RenderOverlaySelectionSnapshot *dimension_scene =
        request.scene_overlay_requested
            ? (request.selection_dimension_scene_snapshot != nullptr
                   ? request.selection_dimension_scene_snapshot : request.scene_overlay_snapshot)
            : nullptr;
    const auto dimension_scene_stamp = scene_submission_stamp(dimension_scene,
                                                              dimension_scene != nullptr);
    const std::uint64_t selection_revision = selection_overlay_visible
        ? selection_source_.publication_revision : 0u;
    if (!selection_dimensions_.valid ||
        selection_dimensions_.selection_overlay_revision != selection_revision ||
        selection_dimensions_.scene != dimension_scene_stamp ||
        selection_dimensions_.tool_preview_identity != request.tool_preview_overlay_identity ||
        selection_dimensions_.tool_preview_revision != request.tool_preview_overlay_revision ||
        selection_dimensions_.mode != request.scene_selection_mode ||
        selection_dimensions_.context != request.style_selection_context) {
      const auto *dimension_selection =
          selection_overlay_visible ? &selection_source_.frame : nullptr;
      auto bounds = request.scene_selection_mode == RenderOverlaySelectionMode::Component
          ? component_selection_dimension_bounds(request, dimension_selection,
                                                  selection_revision)
          : selection_dimension_bounds(request, dimension_selection,
                                       dimension_scene, work_counters_);
      ++work_counters_.selection_dimension_bounds_build_count;
      if (!selection_dimensions_.valid ||
          !same_dimension_bounds(selection_dimensions_.bounds, bounds)) {
        selection_dimensions_.publication_revision = ++selection_dimension_publication_revision_;
      }
      selection_dimensions_.selection_overlay_revision = selection_revision;
      selection_dimensions_.scene = dimension_scene_stamp;
      selection_dimensions_.tool_preview_identity = request.tool_preview_overlay_identity;
      selection_dimensions_.tool_preview_revision = request.tool_preview_overlay_revision;
      selection_dimensions_.mode = request.scene_selection_mode;
      selection_dimensions_.context = request.style_selection_context;
      selection_dimensions_.bounds = bounds;
      selection_dimensions_.valid = true;
    }
    result.selection_dimension_bounds = selection_dimensions_.bounds;
    result.selection_dimension_signature = selection_dimensions_.publication_revision;
  }
  if (request.overlays_visible &&
      request.array_dimensions_mode == ViewportArrayDimensionsMode::Visible) {
    const auto *scene = request.array_dimension_scene_snapshot;
    const auto scene_identity = reinterpret_cast<std::uintptr_t>(scene);
    const RenderSourceStamp scene_stamp = scene != nullptr
        ? static_cast<const RenderSourceStamp &>(*scene) : RenderSourceStamp{};
    const bool scene_changed = !array_dimensions_.valid ||
        array_dimensions_.scene_identity != scene_identity ||
        !render_source_dependencies_match(array_dimensions_.scene, scene_stamp,
                                           kArrayDimensionSourceDependencies);
    if (scene_changed ||
        array_dimensions_.preview_identity != request.tool_preview_overlay_identity ||
        array_dimensions_.preview_revision != request.tool_preview_overlay_revision) {
      bool originals_changed = scene_changed;
      std::size_t original_index = 0u;
      for (const auto &preview : request.selection_dimension_preview_meshes) {
        if (preview.suppress_source) continue;
        const auto key = std::pair{preview.source_entity, preview.visibility_entity};
        if (original_index >= array_dimensions_.originals.size() ||
            array_dimensions_.originals[original_index] != key) originals_changed = true;
        ++original_index;
      }
      originals_changed |= original_index != array_dimensions_.originals.size();
      if (originals_changed) {
        array_dimensions_.originals.clear();
        for (const auto &preview : request.selection_dimension_preview_meshes) {
          if (!preview.suppress_source) array_dimensions_.originals.emplace_back(
              preview.source_entity, preview.visibility_entity);
        }
        array_dimensions_.original_bounds = array_original_dimension_bounds(request, work_counters_);
        ++work_counters_.array_dimension_originals_build_count;
      }
      auto bounds = array_dimension_bounds(request, array_dimensions_.original_bounds,
                                           work_counters_);
      ++work_counters_.array_dimension_bounds_build_count;
      if (!array_dimensions_.valid || !same_dimension_bounds(array_dimensions_.bounds, bounds)) {
        array_dimensions_.publication_revision = ++selection_dimension_publication_revision_;
      }
      array_dimensions_.bounds = bounds;
      array_dimensions_.scene = scene_stamp;
      array_dimensions_.scene_identity = scene_identity;
      array_dimensions_.preview_identity = request.tool_preview_overlay_identity;
      array_dimensions_.preview_revision = request.tool_preview_overlay_revision;
      array_dimensions_.valid = true;
    }
    result.selection_dimension_bounds = array_dimensions_.bounds;
    result.selection_dimension_signature = array_dimensions_.publication_revision;
  }
  const bool dimension_box_visible = result.selection_dimension_bounds.has_value();
  ViewportOverlaySelectionDimensionSubmissionStamp dimension_stamp;
  if (dimension_box_visible) {
    dimension_stamp = {
        .bounds_revision = result.selection_dimension_signature,
        .camera_values = {request.camera.eye.x, request.camera.eye.y, request.camera.eye.z,
                          request.camera.right.x, request.camera.right.y, request.camera.right.z,
                          request.camera.up.x, request.camera.up.y, request.camera.up.z,
                          request.camera.forward.x, request.camera.forward.y, request.camera.forward.z,
                          request.camera.viewport_size.x, request.camera.viewport_size.y,
                          request.camera.orthographic_size, request.camera.fov_degrees,
                          request.camera.near_plane, request.camera.far_plane},
        .projection = request.camera.projection,
        .raster_scale = request.raster_scale,
        .active = true,
    };
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
      .selection_dimension_box_visible = dimension_box_visible,
  };
  const bool point_light_radius_enabled =
      request.overlays_visible && request.point_light_gizmos_visible &&
      request.scene_overlay_requested && request.scene_overlay_snapshot != nullptr;
  const ViewportOverlaySceneSubmissionStamp point_light_source =
      scene_submission_stamp(request.scene_overlay_snapshot,
                             point_light_radius_enabled);
  if (!pane.point_light_source_valid ||
      pane.point_light_source != point_light_source ||
      pane.point_light_radius_enabled != point_light_radius_enabled) {
    // Select the small camera-dependent source only when scene owners publish.
    // Camera navigation and idle frames never scan the complete light catalog.
    pane.selected_point_lights.clear();
    if (point_light_radius_enabled) {
      for (const RenderOverlayLightMarkerSnapshot &light :
           request.scene_overlay_snapshot->point_light_gizmos) {
        if (light.selected) pane.selected_point_lights.push_back(light);
      }
      ++work_counters_.point_light_radius_source_refresh_count;
    }
    pane.point_light_source = point_light_source;
    pane.point_light_source_valid = true;
    pane.point_light_radius_enabled = point_light_radius_enabled;
  }
  ViewportOverlayPointLightRadiusSubmissionStamp point_light_radius;
  if (!pane.selected_point_lights.empty()) {
    point_light_radius = {
        .scene = point_light_source,
        .camera_values = {request.camera.eye.x, request.camera.eye.y,
                          request.camera.eye.z, request.camera.right.x,
                          request.camera.right.y, request.camera.right.z,
                          request.camera.forward.x, request.camera.forward.y,
                          request.camera.forward.z, request.camera.viewport_size.x,
                          request.camera.viewport_size.y, request.camera.orthographic_size,
                          request.camera.fov_degrees, request.camera.near_plane,
                          request.camera.far_plane},
        .projection = request.camera.projection,
        .raster_scale = request.raster_scale,
        .active = true,
    };
  }
  const ViewportOverlayDynamicSubmissionStamp dynamic_stamp{
      .mesh_outline_revision = request.gpu_mesh_outlines ? 0u
          : mesh_outline_source_.revision(request.viewport_index),
      .point_light_radius = point_light_radius,
      .selection_dimensions = dimension_stamp,
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
      .wireframe_pane = request.wireframe_pane,
  };

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
            selection_overlay_visible, dimension_box_visible),
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
    if (!pane.dynamic_valid || pane.dynamic.selection_dimensions != dimension_stamp) {
      pane.selection_dimension_frame = dimension_box_visible
          ? build_selection_dimension_frame(*result.selection_dimension_bounds,
                                            request.camera, request.raster_scale)
          : ViewportOverlayFrame{};
      if (dimension_box_visible) ++work_counters_.selection_dimension_frame_build_count;
    }
    if (!pane.dynamic_valid ||
        pane.dynamic.point_light_radius != point_light_radius) {
      if (point_light_radius.active) {
        pane.point_light_radius_frame =
            ViewportSceneOverlayBuilder::build_point_light_radius_frame(
                pane.selected_point_lights, request.camera, request.raster_scale);
        ++work_counters_.point_light_radius_camera_build_count;
      } else {
        pane.point_light_radius_frame = {};
      }
    }
    // Copy broad semantic sources only after typed source stamps differ or the
    // backend reports a lost retained stream.
    ViewportOverlayFrame dynamic_frame = pane.dynamic_source_frame;
    append_overlay_frame(dynamic_frame, pane.point_light_radius_frame);
    if (uses_mesh_outlines(request)) strip_mesh_selection_wires(dynamic_frame);
    append_overlay_frame(dynamic_frame, mesh_outline);
    const ViewportOverlayFrame &curve_overlay_frame =
        curve_overlay_source_.frame(curve_preview);
    append_overlay_frame(dynamic_frame, curve_overlay_frame);
    if (dimension_box_visible)
      strip_replaced_selection_cages(dynamic_frame, request.scene_selection_mode == RenderOverlaySelectionMode::Group);
    append_overlay_frame(dynamic_frame, pane.selection_dimension_frame);
    ViewportOverlayStyleResolver::apply(
        dynamic_frame, settings,
        ViewportOverlayStyleContext{
            .selection_mode = request.style_selection_context,
            .projection = request.camera.projection,
            .raster_scale = request.raster_scale,
            .construction_object_id = request.construction_object_id,
        });
    if (request.wireframe_pane) strip_wireframe_point_occluders(dynamic_frame);
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
  selection_dimensions_ = {};
  component_dimensions_ = {};
  array_dimensions_ = {};
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
  const auto outline_stats = mesh_outline_source_.work_stats();
  counters.mesh_outline_source_refresh_count = outline_stats.source_refreshes;
  counters.mesh_outline_topology_build_count = outline_stats.topology_builds;
  counters.mesh_outline_camera_classification_count = outline_stats.camera_classifications;
  counters.curve_overlay_build_count = curve_overlay_source_.build_count();
  return counters;
}

} // namespace quader

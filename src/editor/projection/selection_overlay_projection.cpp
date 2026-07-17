////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/projection/selection_overlay_projection.hpp"

#include "diagnostics/profile.hpp"
#include "editor/modeling/runtime/modeling_object_runtime_map.hpp"
#include "math/glm_bridge.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_hierarchy_visibility.hpp"
#include "scene/scene_transform.hpp"

#include <optional>
#include <unordered_map>
#include <utility>

#include <mesh/polygon/document.hpp>

namespace quader {
namespace {

QVec3 from_runtime(quader::QVec3 value) { return {value.x, value.y, value.z}; }

ViewportOverlayLayer
from_runtime(quader_editor_core::SelectionOverlayLayer layer) {
  switch (layer) {
  case quader_editor_core::SelectionOverlayLayer::SelectedVisibleFace:
    return ViewportOverlayLayer::SelectedVisibleFace;
  case quader_editor_core::SelectionOverlayLayer::SelectedFaceEdge:
    return ViewportOverlayLayer::SelectedFaceEdge;
  case quader_editor_core::SelectionOverlayLayer::UnselectedVertex:
    return ViewportOverlayLayer::UnselectedVertex;
  case quader_editor_core::SelectionOverlayLayer::SelectedVertex:
    return ViewportOverlayLayer::SelectedVertex;
  case quader_editor_core::SelectionOverlayLayer::HoverFace:
    return ViewportOverlayLayer::HoverFace;
  case quader_editor_core::SelectionOverlayLayer::HoverFaceEdge:
    return ViewportOverlayLayer::HoverFaceEdge;
  case quader_editor_core::SelectionOverlayLayer::HoverEdge:
    return ViewportOverlayLayer::HoverEdge;
  case quader_editor_core::SelectionOverlayLayer::HoverVertex:
    return ViewportOverlayLayer::HoverVertex;
  case quader_editor_core::SelectionOverlayLayer::SelectedEdge:
    return ViewportOverlayLayer::SelectedEdge;
  case quader_editor_core::SelectionOverlayLayer::SelectedXrayFace:
    return ViewportOverlayLayer::SelectedXrayFace;
  case quader_editor_core::SelectionOverlayLayer::SelectedFaceMask:
    return ViewportOverlayLayer::SelectedFaceMask;
  case quader_editor_core::SelectionOverlayLayer::SourceWire:
    return ViewportOverlayLayer::SourceWire;
  case quader_editor_core::SelectionOverlayLayer::OpenEdge:
    return ViewportOverlayLayer::OpenEdge;
  case quader_editor_core::SelectionOverlayLayer::SnapTargetVertex:
    return ViewportOverlayLayer::SnapTargetVertex;
  case quader_editor_core::SelectionOverlayLayer::DiagnosticEdge:
    return ViewportOverlayLayer::DiagnosticEdge;
  case quader_editor_core::SelectionOverlayLayer::SourceWireDepthStamp:
    return ViewportOverlayLayer::SourceWireDepthStamp;
  }
  return ViewportOverlayLayer::SourceWire;
}

ViewportOverlayStyleRole
triangle_style_role(quader_editor_core::SelectionOverlayLayer layer,
                    std::uint32_t color_abgr) {
  if (const std::optional<quader_editor_core::SelectionOverlayStyleRole> role =
          quader_editor_core::selection_overlay_style_role_from_token(
              color_abgr)) {
    switch (*role) {
    case quader_editor_core::SelectionOverlayStyleRole::SelectionFace:
      return ViewportOverlayStyleRole::SelectionFace;
    case quader_editor_core::SelectionOverlayStyleRole::HoverFace:
      return ViewportOverlayStyleRole::HoverFace;
    case quader_editor_core::SelectionOverlayStyleRole::FacePlaneHoverFace:
      return ViewportOverlayStyleRole::FacePlaneHoverFace;
    case quader_editor_core::SelectionOverlayStyleRole::RemoveSelectionFace:
      return ViewportOverlayStyleRole::RemoveSelectionFace;
    default:
      break;
    }
  }
  switch (layer) {
  case quader_editor_core::SelectionOverlayLayer::SelectedVisibleFace:
  case quader_editor_core::SelectionOverlayLayer::SelectedXrayFace:
  case quader_editor_core::SelectionOverlayLayer::SelectedFaceMask:
    return ViewportOverlayStyleRole::SelectionFace;
  case quader_editor_core::SelectionOverlayLayer::HoverFace:
    return ViewportOverlayStyleRole::HoverFace;
  case quader_editor_core::SelectionOverlayLayer::SourceWireDepthStamp:
    return ViewportOverlayStyleRole::SourceWire;
  default:
    return ViewportOverlayStyleRole::ExplicitColor;
  }
}

ViewportOverlayStyleRole
line_style_role(quader_editor_core::SelectionOverlayLayer layer,
                std::uint32_t color_abgr) {
  if (const std::optional<quader_editor_core::SelectionOverlayStyleRole> role =
          quader_editor_core::selection_overlay_style_role_from_token(
              color_abgr)) {
    switch (*role) {
    case quader_editor_core::SelectionOverlayStyleRole::SourceWire:
      return ViewportOverlayStyleRole::SourceWire;
    case quader_editor_core::SelectionOverlayStyleRole::SelectionWire:
      return layer == quader_editor_core::SelectionOverlayLayer::SourceWire
                 ? ViewportOverlayStyleRole::SelectedSourceWire
                 : ViewportOverlayStyleRole::SelectionWire;
    case quader_editor_core::SelectionOverlayStyleRole::HoverWire:
      return ViewportOverlayStyleRole::HoverWire;
    case quader_editor_core::SelectionOverlayStyleRole::FacePlaneHoverWire:
      return ViewportOverlayStyleRole::FacePlaneHoverWire;
    case quader_editor_core::SelectionOverlayStyleRole::RemoveSelectionWire:
      return ViewportOverlayStyleRole::RemoveSelectionWire;
    case quader_editor_core::SelectionOverlayStyleRole::OpenEdge:
      return ViewportOverlayStyleRole::OpenEdge;
    case quader_editor_core::SelectionOverlayStyleRole::DiagnosticEdge:
      return ViewportOverlayStyleRole::DiagnosticEdge;
    default:
      break;
    }
  }
  switch (layer) {
  case quader_editor_core::SelectionOverlayLayer::SourceWire:
    return ViewportOverlayStyleRole::SourceWire;
  case quader_editor_core::SelectionOverlayLayer::SelectedFaceEdge:
  case quader_editor_core::SelectionOverlayLayer::SelectedEdge:
    return ViewportOverlayStyleRole::SelectionWire;
  case quader_editor_core::SelectionOverlayLayer::HoverFaceEdge:
  case quader_editor_core::SelectionOverlayLayer::HoverEdge:
    return ViewportOverlayStyleRole::HoverWire;
  case quader_editor_core::SelectionOverlayLayer::OpenEdge:
    return ViewportOverlayStyleRole::OpenEdge;
  case quader_editor_core::SelectionOverlayLayer::DiagnosticEdge:
    return ViewportOverlayStyleRole::DiagnosticEdge;
  default:
    return ViewportOverlayStyleRole::ExplicitColor;
  }
}

ViewportOverlayStyleRole
point_style_role(quader_editor_core::SelectionOverlayLayer layer,
                 std::uint32_t color_abgr) {
  if (const std::optional<quader_editor_core::SelectionOverlayStyleRole> role =
          quader_editor_core::selection_overlay_style_role_from_token(
              color_abgr)) {
    switch (*role) {
    case quader_editor_core::SelectionOverlayStyleRole::Vertex:
      return ViewportOverlayStyleRole::Vertex;
    case quader_editor_core::SelectionOverlayStyleRole::SelectedVertex:
      return ViewportOverlayStyleRole::SelectedVertex;
    case quader_editor_core::SelectionOverlayStyleRole::HoverVertex:
      return ViewportOverlayStyleRole::HoverVertex;
    case quader_editor_core::SelectionOverlayStyleRole::RemoveSelectionWire:
      return ViewportOverlayStyleRole::RemoveSelectionVertex;
    case quader_editor_core::SelectionOverlayStyleRole::SnapTargetVertex:
      return ViewportOverlayStyleRole::SnapTargetVertex;
    default:
      break;
    }
  }
  switch (layer) {
  case quader_editor_core::SelectionOverlayLayer::UnselectedVertex:
    return ViewportOverlayStyleRole::Vertex;
  case quader_editor_core::SelectionOverlayLayer::SelectedVertex:
    return ViewportOverlayStyleRole::SelectedVertex;
  case quader_editor_core::SelectionOverlayLayer::HoverVertex:
    return ViewportOverlayStyleRole::HoverVertex;
  case quader_editor_core::SelectionOverlayLayer::SnapTargetVertex:
    return ViewportOverlayStyleRole::SnapTargetVertex;
  default:
    return ViewportOverlayStyleRole::ExplicitColor;
  }
}

ViewportOverlaySource overlay_source_for_role(ViewportOverlayStyleRole role) {
  switch (role) {
  case ViewportOverlayStyleRole::HoverFace:
  case ViewportOverlayStyleRole::HoverWire:
  case ViewportOverlayStyleRole::HoverVertex:
  case ViewportOverlayStyleRole::FacePlaneHoverFace:
  case ViewportOverlayStyleRole::FacePlaneHoverWire:
  case ViewportOverlayStyleRole::RemoveSelectionFace:
  case ViewportOverlayStyleRole::RemoveSelectionWire:
  case ViewportOverlayStyleRole::RemoveSelectionVertex:
    return ViewportOverlaySource::Hover;
  case ViewportOverlayStyleRole::OpenEdge:
  case ViewportOverlayStyleRole::DiagnosticEdge:
    return ViewportOverlaySource::Diagnostic;
  case ViewportOverlayStyleRole::ToolPlaneFill:
  case ViewportOverlayStyleRole::ToolPlaneLine:
  case ViewportOverlayStyleRole::ToolHandle:
  case ViewportOverlayStyleRole::ToolPreviewFill:
  case ViewportOverlayStyleRole::ToolPreviewLine:
  case ViewportOverlayStyleRole::ToolPreviewSeam:
  case ViewportOverlayStyleRole::CutPreviewKeepFill:
  case ViewportOverlayStyleRole::CutPreviewKeepBothDiscardFill:
  case ViewportOverlayStyleRole::CutPreviewDiscardFill:
  case ViewportOverlayStyleRole::CutPreviewKeepBothOutline:
  case ViewportOverlayStyleRole::CutPreviewDiscardOutline:
  case ViewportOverlayStyleRole::CutPreviewSeam:
    return ViewportOverlaySource::Tool;
  case ViewportOverlayStyleRole::LayerDefault:
  case ViewportOverlayStyleRole::ExplicitColor:
  case ViewportOverlayStyleRole::SelectionFace:
  case ViewportOverlayStyleRole::SelectionWire:
  case ViewportOverlayStyleRole::SelectedSourceWire:
  case ViewportOverlayStyleRole::SourceWire:
  case ViewportOverlayStyleRole::Vertex:
  case ViewportOverlayStyleRole::SelectedVertex:
  case ViewportOverlayStyleRole::SnapTargetVertex:
  case ViewportOverlayStyleRole::GroupBounds:
  case ViewportOverlayStyleRole::WireframeBase:
    return ViewportOverlaySource::Selection;
  }
  return ViewportOverlaySource::Unknown;
}

bool overlay_projection_component_mode(quader_editor_core::SelectionMode mode) {
  return mode != quader_editor_core::SelectionMode::Object;
}

bool component_selection_line_layer(ViewportOverlayLayer layer) {
  return layer == ViewportOverlayLayer::SourceWire ||
         layer == ViewportOverlayLayer::SelectedFaceEdge ||
         layer == ViewportOverlayLayer::SelectedEdge;
}

bool component_hover_line_layer(ViewportOverlayLayer layer) {
  return layer == ViewportOverlayLayer::HoverFaceEdge ||
         layer == ViewportOverlayLayer::HoverEdge;
}

ViewportOverlaySource
overlay_source_for_line(ViewportOverlayLayer layer, ViewportOverlayStyleRole role,
                        quader_editor_core::SelectionMode mode) {
  if (overlay_projection_component_mode(mode)) {
    if (component_selection_line_layer(layer)) {
      return ViewportOverlaySource::ComponentSelection;
    }
    if (component_hover_line_layer(layer)) {
      return ViewportOverlaySource::ComponentHover;
    }
  }
  return overlay_source_for_role(role);
}

ViewportOverlaySource
overlay_source_for_point(ViewportOverlayLayer layer, ViewportOverlayStyleRole role,
                         quader_editor_core::SelectionMode mode) {
  if (overlay_projection_component_mode(mode) &&
      layer == ViewportOverlayLayer::SelectedVertex) {
    return ViewportOverlaySource::ComponentSelection;
  }
  if (overlay_projection_component_mode(mode) &&
      layer == ViewportOverlayLayer::HoverVertex) {
    return ViewportOverlaySource::ComponentHover;
  }
  return overlay_source_for_role(role);
}

std::uint32_t projected_color_for(ViewportOverlayLayer layer,
                                  ViewportOverlayStyleRole role,
                                  std::uint32_t authored_color) {
  switch (role) {
  case ViewportOverlayStyleRole::SelectionFace:
    return kOverlaySelectionFaceColor;
  case ViewportOverlayStyleRole::SelectionWire:
  case ViewportOverlayStyleRole::SelectedSourceWire:
    return kOverlaySelectionWireColor;
  case ViewportOverlayStyleRole::SourceWire:
    return kOverlaySourceWireColor;
  case ViewportOverlayStyleRole::Vertex:
    return kOverlayVertexColor;
  case ViewportOverlayStyleRole::SelectedVertex:
    return kOverlaySelectedVertexColor;
  case ViewportOverlayStyleRole::HoverFace:
    return kOverlayHoverFaceColor;
  case ViewportOverlayStyleRole::HoverWire:
    return kOverlayHoverWireColor;
  case ViewportOverlayStyleRole::HoverVertex:
    return kOverlayHoverVertexColor;
  case ViewportOverlayStyleRole::FacePlaneHoverFace:
    return kOverlayFacePlaneHoverFaceColor;
  case ViewportOverlayStyleRole::FacePlaneHoverWire:
    return kOverlayFacePlaneHoverWireColor;
  case ViewportOverlayStyleRole::RemoveSelectionFace:
    return kOverlayRemoveFaceColor;
  case ViewportOverlayStyleRole::RemoveSelectionWire:
  case ViewportOverlayStyleRole::RemoveSelectionVertex:
    return kOverlayRemoveWireColor;
  case ViewportOverlayStyleRole::OpenEdge:
    return kOverlayOpenEdgeColor;
  case ViewportOverlayStyleRole::DiagnosticEdge:
    return kOverlayDiagnosticEdgeColor;
  case ViewportOverlayStyleRole::SnapTargetVertex:
    return kOverlayDiagnosticEdgeColor;
  default:
    return viewport_overlay_color_for(layer, authored_color);
  }
}

ViewportOverlayElementRef runtime_element_ref(int object_id,
                                              quader_poly::ElementId vertex_id,
                                              quader_poly::Edge edge,
                                              quader_poly::ElementId face_id) {
  return {
      .object_id = object_id,
      .vertex_id = static_cast<int>(vertex_id),
      .edge_a = static_cast<int>(edge.a),
      .edge_b = static_cast<int>(edge.b),
      .face_id = static_cast<int>(face_id),
  };
}

/**
 * Caches visibility and world-transform data needed while projecting one modeling object overlay.
 */
struct ProjectionObjectState {
    bool visible = true;
    bool has_world_transform = false;
    QMat4 world_transform;
};

std::optional<QMat4> instance_edit_member_world_transform(
    const SceneDocument &scene, ModelingInstanceEditScope scope,
    EntityId member) {
  const std::optional<QMat4> definition_inverse =
      try_scene_matrix_inverse(scene.world_transform_matrix(scope.definition));
  if (!definition_inverse.has_value()) {
    return std::nullopt;
  }
  return scene_transform_multiply(
      scene.world_transform_matrix(scope.occurrence),
      scene_transform_multiply(*definition_inverse,
                               scene.world_transform_matrix(member)));
}

/**
 * Memoizes scene lookup results for object ids while building a selection overlay frame.
 */
class ProjectionObjectStateCache {
public:
    explicit ProjectionObjectStateCache(
        ModelingInstanceEditScope instance_edit_scope = {},
        std::function<bool(EntityId)> entity_visible = {})
        : instance_edit_scope_(instance_edit_scope),
          entity_visible_(std::move(entity_visible)) {
        states_.reserve(64);
    }

    [[nodiscard]] const ProjectionObjectState &
    state_for(int object_id, const ModelingObjectRuntimeMap &object_registry,
              const SceneDocument &scene) {
      if (object_id < 0) {
        last_object_id_ = object_id;
        last_state_ = &default_state_;
        return default_state_;
      }
      if (object_id == last_object_id_ && last_state_ != nullptr) {
        return *last_state_;
      }
      const auto existing = states_.find(object_id);
      if (existing != states_.end()) {
        last_object_id_ = object_id;
        last_state_ = &existing->second;
        return existing->second;
      }

      ProjectionObjectState state;
      const std::optional<EntityId> entity =
          object_registry.entity_for_modeling_object(object_id);
      if (!entity.has_value()) {
        state.visible = false;
      } else {
        const bool viewport_visible =
            !entity_visible_ || entity_visible_(*entity);
        const bool active_member =
            instance_edit_scope_.active() &&
            scene.containing_instance_definition(*entity) ==
                instance_edit_scope_.definition;
        if (active_member) {
          state.visible = viewport_visible &&
              scene.is_effectively_visible(instance_edit_scope_.occurrence) &&
              scene_hierarchy_segment_is_visible(
                  scene, *entity, instance_edit_scope_.definition);
          if (state.visible) {
            const std::optional<QMat4> world =
                instance_edit_member_world_transform(
                    scene, instance_edit_scope_, *entity);
            state.has_world_transform = world.has_value();
            if (world.has_value()) {
              state.world_transform = *world;
            }
          }
        } else {
          state.visible = viewport_visible &&
                          scene.is_effectively_visible(*entity);
          if (state.visible && scene.transform(*entity) != nullptr) {
            state.has_world_transform = true;
            state.world_transform = scene.world_transform_matrix(*entity);
          }
        }
      }
      const auto [inserted, _] = states_.emplace(object_id, state);
      last_object_id_ = object_id;
      last_state_ = &inserted->second;
      return *last_state_;
    }

private:
  ProjectionObjectState default_state_;
  std::unordered_map<int, ProjectionObjectState> states_;
  int last_object_id_ = -1;
  const ProjectionObjectState *last_state_ = nullptr;
  ModelingInstanceEditScope instance_edit_scope_;
  std::function<bool(EntityId)> entity_visible_;
};

QVec3 transform_object_point(const ProjectionObjectState &state, QVec3 point) {
  return state.has_world_transform
             ? transform_point(state.world_transform, point)
             : point;
}

} // namespace

ViewportOverlayFrame
SelectionOverlayProjection::project_selection_overlay_frame(
    const quader_editor_core::SelectionOverlayFrame &frame,
    const ModelingObjectRuntimeMap &object_registry,
    const SceneDocument &scene, quader_editor_core::SelectionMode mode,
    ModelingInstanceEditScope instance_edit_scope,
    const std::function<bool(EntityId)> &entity_visible) {
  QDR_PROFILE_FUNCTION();
  ViewportOverlayFrame result;
  result.triangles.reserve(frame.triangles.size());
  result.lines.reserve(frame.lines.size());
  result.points.reserve(frame.points.size());
  result.hover_target_selected = frame.hover_target_selected;
  ProjectionObjectStateCache object_state_cache(instance_edit_scope,
                                                 entity_visible);

  {
    QDR_PROFILE_SCOPE("Selection overlay project triangles");
    for (const quader_editor_core::SelectionOverlayTriangle &triangle :
         frame.triangles) {
      const ProjectionObjectState &object_state = object_state_cache.state_for(
          triangle.object_id, object_registry, scene);
      if (!object_state.visible) {
        continue;
      }
      const ViewportOverlayLayer layer = from_runtime(triangle.layer);
      const ViewportOverlayStyleRole role =
          triangle_style_role(triangle.layer, triangle.color_abgr);
      result.triangles.push_back(ViewportOverlayTriangle{
          .layer = layer,
          .a = transform_object_point(object_state, from_runtime(triangle.a)),
          .b = transform_object_point(object_state, from_runtime(triangle.b)),
          .c = transform_object_point(object_state, from_runtime(triangle.c)),
          .color = projected_color_for(layer, role, triangle.color_abgr),
          .style_role = role,
          .source = overlay_source_for_role(role),
          .element = runtime_element_ref(triangle.object_id,
                                         quader_poly::kInvalidElementId, {},
                                         triangle.face_id),
      });
    }
  }
  {
    QDR_PROFILE_SCOPE("Selection overlay project lines");
    for (const quader_editor_core::SelectionOverlayLine &line : frame.lines) {
      const ProjectionObjectState &object_state =
          object_state_cache.state_for(line.object_id, object_registry, scene);
      if (!object_state.visible) {
        continue;
      }
      const ViewportOverlayLayer layer = from_runtime(line.layer);
      const ViewportOverlayStyleRole role =
          line_style_role(line.layer, line.color_abgr);
      result.lines.push_back(ViewportOverlayLine{
          .layer = layer,
          .start =
              transform_object_point(object_state, from_runtime(line.start)),
          .end = transform_object_point(object_state, from_runtime(line.end)),
          .color = projected_color_for(layer, role, line.color_abgr),
          .width_pixels = line.width_pixels,
          .style_role = role,
          .source = overlay_source_for_line(layer, role, mode),
          .element = runtime_element_ref(line.object_id,
                                         quader_poly::kInvalidElementId,
                                         line.edge, line.face_id),
      });
    }
  }
  {
    QDR_PROFILE_SCOPE("Selection overlay project points");
    for (const quader_editor_core::SelectionOverlayPoint &point :
         frame.points) {
      const ProjectionObjectState &object_state =
          object_state_cache.state_for(point.object_id, object_registry, scene);
      if (!object_state.visible) {
        continue;
      }
      const ViewportOverlayLayer layer = from_runtime(point.layer);
      const ViewportOverlayStyleRole role =
          point_style_role(point.layer, point.color_abgr);
      result.points.push_back(ViewportOverlayPoint{
          .layer = layer,
          .position = transform_object_point(object_state,
                                             from_runtime(point.position)),
          .color = projected_color_for(layer, role, point.color_abgr),
          .size_pixels = point.size_pixels,
          .style_role = role,
          .source = overlay_source_for_point(layer, role, mode),
          .element = runtime_element_ref(point.object_id, point.vertex_id, {},
                                         quader_poly::kInvalidElementId),
      });
    }
  }
  return result;
}

void SelectionOverlayProjection::append_projected_overlay_lines(
    ViewportOverlayFrame &frame,
    const std::vector<quader_editor_core::SelectionOverlayLine> &lines,
    const ModelingObjectRuntimeMap &object_registry, const SceneDocument &scene,
    ViewportOverlayLayer layer, ViewportOverlayStyleRole style_role,
    ViewportOverlaySource source,
    ModelingInstanceEditScope instance_edit_scope,
    const std::function<bool(EntityId)> &entity_visible) {
  QDR_PROFILE_FUNCTION();
  frame.lines.reserve(frame.lines.size() + lines.size());
  ProjectionObjectStateCache object_state_cache(instance_edit_scope,
                                                 entity_visible);
  for (const quader_editor_core::SelectionOverlayLine &line : lines) {
    const ProjectionObjectState &object_state =
        object_state_cache.state_for(line.object_id, object_registry, scene);
    if (!object_state.visible) {
      continue;
    }
    frame.lines.push_back(ViewportOverlayLine{
        .layer = layer,
        .start = transform_object_point(object_state, from_runtime(line.start)),
        .end = transform_object_point(object_state, from_runtime(line.end)),
        .color = viewport_overlay_color_for(layer, line.color_abgr),
        .width_pixels = line.width_pixels,
        .style_role = style_role,
        .source = source,
        .element =
            runtime_element_ref(line.object_id, quader_poly::kInvalidElementId,
                                line.edge, line.face_id),
    });
  }
}

void SelectionOverlayProjection::append_projected_overlay_points(
    ViewportOverlayFrame &frame,
    const std::vector<quader_editor_core::SelectionOverlayPoint> &points,
    const ModelingObjectRuntimeMap &object_registry, const SceneDocument &scene,
    ViewportOverlayLayer layer, ViewportOverlayStyleRole style_role,
    ViewportOverlaySource source,
    ModelingInstanceEditScope instance_edit_scope,
    const std::function<bool(EntityId)> &entity_visible) {
  QDR_PROFILE_FUNCTION();
  frame.points.reserve(frame.points.size() + points.size());
  ProjectionObjectStateCache object_state_cache(instance_edit_scope,
                                                 entity_visible);
  for (const quader_editor_core::SelectionOverlayPoint &point : points) {
    const ProjectionObjectState &object_state =
        object_state_cache.state_for(point.object_id, object_registry, scene);
    if (!object_state.visible) {
      continue;
    }
    frame.points.push_back(ViewportOverlayPoint{
        .layer = layer,
        .position = transform_object_point(object_state,
                                           from_runtime(point.position)),
        .color = point.color_abgr,
        .size_pixels = point.size_pixels,
        .style_role = style_role,
        .source = source,
        .element = runtime_element_ref(point.object_id, point.vertex_id, {},
                                       quader_poly::kInvalidElementId),
    });
  }
}

void SelectionOverlayProjection::append_world_space_overlay_points(
    ViewportOverlayFrame &frame,
    const std::vector<quader_editor_core::SelectionOverlayPoint> &points) {
  frame.points.reserve(frame.points.size() + points.size());
  for (const quader_editor_core::SelectionOverlayPoint &point : points) {
    const ViewportOverlayLayer layer = from_runtime(point.layer);
    const ViewportOverlayStyleRole role =
        point_style_role(point.layer, point.color_abgr);
    frame.points.push_back(ViewportOverlayPoint{
        .layer = layer,
        .position = from_runtime(point.position),
        .color = projected_color_for(layer, role, point.color_abgr),
        .size_pixels = point.size_pixels,
        .style_role = role,
        .source = overlay_source_for_role(role),
        .element = runtime_element_ref(point.object_id, point.vertex_id, {},
                                       quader_poly::kInvalidElementId),
    });
  }
}

} // namespace quader

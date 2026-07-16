////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/services/modeling_session_service.hpp"

#include "editor/modeling/modeling_viewport_camera_adapter.hpp"

#include "assets/asset_registry.hpp"
#include "assets/mesh_asset_payload.hpp"
#include "core/i_undoable_command.hpp"
#include "diagnostics/profile.hpp"
#include "editor/commands/document_command_service.hpp"
#include "editor/commands/modeling/scene_aware_delete_selection_command.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/modeling/linear_array_layout.hpp"
#include "editor/modeling/radial_array_layout.hpp"
#include "editor/modeling/services/modeling_hierarchy_duplicate_service.hpp"
#include "editor/modeling/services/modeling_instant_operation_catalog.hpp"
#include "editor/modeling/services/modeling_operation_settings.hpp"
#include "editor/modeling/services/modeling_session_services.hpp"
#include "editor/modeling/services/modeling_transform_drag_service.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "editor/selection/selection_projection_service.hpp"
#include "editor/tools/viewport_tool_descriptor_catalog.hpp"
#include "math/glm_bridge.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_entity_naming.hpp"
#include "scene/scene_transform.hpp"
#include "scene/scene_transform_pivot.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <editor/interactions/box_tool.hpp>
#include <editor/interactions/cut_tool.hpp>
#include <editor/interactions/mirror_tool.hpp>
#include <editor/interactions/tool_math_internal.hpp>
#include <editor/interactions/tool_transform_math.hpp>
#include <editor/interactions/selection_box.hpp>
#include <editor/interactions/selection_edit.hpp>
#include <editor/interactions/tool_snapping.hpp>
#include <editor/interactions/viewport_projection.hpp>
#include <editor/metadata/host_contract.hpp>
#include <editor/modeling/operations/operation_command.hpp>
#include <editor/modeling/runtime/repeat_last_action.hpp>
#include <editor/modeling/runtime/runtime_command.hpp>
#include <editor/modeling/runtime/session.hpp>
#include <mesh/geometry/geometry.hpp>
#include <mesh/polygon/document.hpp>

namespace quader {
namespace {

quader::QVec3 to_runtime(QVec3 value) { return {value.x, value.y, value.z}; }

quader::QVec2 to_runtime(QVec2 value) { return {value.x, value.y}; }

bool transformed_duplicate_world_matches(const QMat4 &left,
                                         const QMat4 &right) {
  constexpr float kAbsoluteTolerance = 0.0001f;
  constexpr float kRelativeTolerance = 0.00001f;
  for (std::size_t index = 0; index < left.values.size(); ++index) {
    const float magnitude =
        std::max(std::abs(left.values[index]), std::abs(right.values[index]));
    if (std::abs(left.values[index] - right.values[index]) >
        kAbsoluteTolerance + kRelativeTolerance * magnitude) {
      return false;
    }
  }
  return true;
}

quader_poly::Ray to_runtime(const ModelingRay &ray) {
  return {
      .origin = to_runtime(ray.origin),
      .direction = to_runtime(normalize(ray.direction)),
  };
}

quader_poly::Transform3 to_runtime(const ModelingTransform3 &transform) {
  return {
      .x_axis = to_runtime(transform.x_axis),
      .y_axis = to_runtime(transform.y_axis),
      .z_axis = to_runtime(transform.z_axis),
      .origin = to_runtime(transform.origin),
  };
}

bool knife_pick_face_contains_edge(const quader_poly::Face &face,
                                   quader_poly::Edge edge) {
  const quader_poly::Edge normalized =
      quader_poly::make_edge(edge.a, edge.b);
  for (std::size_t index = 0; index < face.vertices.size(); ++index) {
    const quader_poly::ElementId a = face.vertices[index];
    const quader_poly::ElementId b =
        face.vertices[(index + 1U) % face.vertices.size()];
    if (quader_poly::make_edge(a, b) == normalized) {
      return true;
    }
  }
  return false;
}

bool knife_pick_face_contains_component(
    const quader_poly::Face &face, quader_poly::SelectionMode mode,
    quader_poly::ElementId vertex_id, quader_poly::Edge edge) {
  if (mode == quader_poly::SelectionMode::Vertex) {
    return std::find(face.vertices.begin(), face.vertices.end(), vertex_id) !=
           face.vertices.end();
  }
  return mode == quader_poly::SelectionMode::Edge &&
         knife_pick_face_contains_edge(face, edge);
}

quader_poly::ElementId knife_pick_context_face_id(
    const quader_poly::Document &document, quader_poly::SelectionMode mode,
    quader_poly::ElementId vertex_id, quader_poly::Edge edge,
    const quader_poly::Ray &ray,
    const std::optional<quader_editor_core::PickHitRecord> &surface_hit,
    int object_id) {
  if (surface_hit.has_value() && surface_hit->object_id == object_id) {
    const quader_poly::Face *surface_face =
        quader_poly::find_face(document, surface_hit->face_id);
    if (surface_face != nullptr && knife_pick_face_contains_component(
                                       *surface_face, mode, vertex_id, edge)) {
      return surface_face->id;
    }
  }

  quader_poly::ElementId best_face_id = quader_poly::kInvalidElementId;
  float best_facing_score = -std::numeric_limits<float>::infinity();
  for (const quader_poly::Face &face : document.faces) {
    if (!knife_pick_face_contains_component(face, mode, vertex_id, edge)) {
      continue;
    }
    const float facing_score =
        -quader::dot(quader_poly::face_normal(document, face), ray.direction);
    if (best_face_id == quader_poly::kInvalidElementId ||
        facing_score > best_facing_score) {
      best_face_id = face.id;
      best_facing_score = facing_score;
    }
  }
  return best_face_id;
}

/**
 * Carries the nearest cursor projection onto one screen-space Knife edge.
 */
struct KnifeScreenSegmentProjection {
  QVec2 closest;
  float factor = 0.0F;
  float distance_squared = std::numeric_limits<float>::max();
};

KnifeScreenSegmentProjection knife_pick_screen_segment_projection(
    QVec2 point, QVec2 a, QVec2 b) {
  const QVec2 ab = b - a;
  const float length_squared = ab.x * ab.x + ab.y * ab.y;
  const float factor = length_squared > 0.000001F
                           ? std::clamp(((point.x - a.x) * ab.x +
                                         (point.y - a.y) * ab.y) /
                                            length_squared,
                                        0.0F, 1.0F)
                           : 0.0F;
  const QVec2 closest = a + ab * factor;
  const float dx = point.x - closest.x;
  const float dy = point.y - closest.y;
  return {
      .closest = closest,
      .factor = factor,
      .distance_squared = dx * dx + dy * dy,
  };
}

float knife_world_segment_factor_from_screen_projection(
    const quader_editor_core::ViewportCameraState &camera, QVec3 a, QVec3 b,
    float screen_factor) {
  if (camera.projection ==
      quader_editor_core::CameraProjection::Orthographic) {
    return screen_factor;
  }

  QVec3 forward = camera.transform.z_axis * -1.0F;
  if (length_squared(forward) <= 0.000001F) {
    forward = {0.0F, 0.0F, -1.0F};
  } else if (!camera.affine_model_space) {
    forward = normalize(forward);
  }
  const float depth_a = dot(a - camera.transform.origin, forward);
  const float depth_b = dot(b - camera.transform.origin, forward);
  const float denominator =
      (1.0F - screen_factor) * depth_b + screen_factor * depth_a;
  if (!std::isfinite(denominator) || std::abs(denominator) <= 0.000001F) {
    return screen_factor;
  }

  // Screen interpolation is perspective-divided; recover the matching
  // parameter on the authored 3D edge instead of mixing the two metrics.
  return std::clamp(screen_factor * depth_a / denominator, 0.0F, 1.0F);
}

bool knife_screen_component_is_occluded(
    const std::vector<quader_editor_core::PolygonObject> &objects,
    int object_index, quader_poly::SelectionMode mode,
    quader_poly::ElementId vertex_id, quader_poly::Edge edge,
    float ray_distance,
    const std::optional<quader_editor_core::PickHitRecord> &surface_hit) {
  if (!surface_hit.has_value() ||
      ray_distance <= surface_hit->ray_distance + 0.001F) {
    return false;
  }
  if (surface_hit->object_index != object_index || object_index < 0 ||
      object_index >= static_cast<int>(objects.size())) {
    return true;
  }
  const quader_poly::Face *surface_face = quader_poly::find_face(
      objects[static_cast<std::size_t>(object_index)].document,
      surface_hit->face_id);
  return surface_face == nullptr || !knife_pick_face_contains_component(
                                        *surface_face, mode, vertex_id, edge);
}

/**
 * Couples one topology hit with its screen-space ranking distance.
 */
struct KnifeScreenPickCandidate {
  quader_editor_core::PickHitRecord hit;
  float screen_distance_squared = std::numeric_limits<float>::max();
};

bool knife_screen_pick_candidate_is_better(
    const KnifeScreenPickCandidate &candidate,
    const std::optional<KnifeScreenPickCandidate> &best) {
  constexpr float kComparisonEpsilon = 0.0001F;
  if (!best.has_value()) {
    return true;
  }
  if (std::abs(candidate.screen_distance_squared -
               best->screen_distance_squared) > kComparisonEpsilon) {
    return candidate.screen_distance_squared < best->screen_distance_squared;
  }
  if (std::abs(candidate.hit.ray_distance - best->hit.ray_distance) >
      kComparisonEpsilon) {
    return candidate.hit.ray_distance < best->hit.ray_distance;
  }
  return candidate.hit.object_id < best->hit.object_id;
}

std::optional<quader_editor_core::PickHitRecord>
knife_screen_space_component_pick(
    const std::vector<quader_editor_core::PolygonObject> &objects,
    const ModelingToolRayEvent &event, quader_poly::SelectionMode mode,
    double radius_pixels, int target_object_id,
    quader_poly::PickOptions face_options,
    std::span<const quader_editor_core::PickObjectBounds> object_bounds,
    std::span<const quader_editor_core::PickObjectFaceGeometry>
        face_geometry) {
  if ((mode != quader_poly::SelectionMode::Vertex &&
       mode != quader_poly::SelectionMode::Edge) ||
      !std::isfinite(radius_pixels) || radius_pixels <= 0.0) {
    return std::nullopt;
  }

  const quader_editor_core::ViewportCameraState camera =
      modeling_runtime_camera(event.camera);
  const float maximum_distance_squared =
      static_cast<float>(radius_pixels * radius_pixels);
  std::optional<KnifeScreenPickCandidate> best;

  for (int object_index = 0; object_index < static_cast<int>(objects.size());
       ++object_index) {
    const quader_editor_core::PolygonObject &object =
        objects[static_cast<std::size_t>(object_index)];
    if (target_object_id > 0 && object.id != target_object_id) {
      continue;
    }

    if (mode == quader_poly::SelectionMode::Vertex) {
      for (const quader_poly::Vertex &vertex : object.document.vertices) {
        QVec2 screen;
        if (!quader_editor_core::project_viewport_point(
                camera, vertex.position, screen)) {
          continue;
        }
        const float dx = event.screen_position.x - screen.x;
        const float dy = event.screen_position.y - screen.y;
        const float screen_distance_squared = dx * dx + dy * dy;
        if (screen_distance_squared > maximum_distance_squared) {
          continue;
        }

        // Occlusion must be measured along the candidate's own projected ray;
        // the cursor may be several pixels away while still inside tolerance.
        const quader_poly::Ray candidate_ray =
            quader_editor_core::ray_from_viewport_screen_point(camera, screen);
        const float ray_distance =
            quader::dot(vertex.position - candidate_ray.origin,
                        candidate_ray.direction);
        const std::optional<quader_editor_core::PickHitRecord>
            candidate_surface_hit = quader_editor_core::pick_nearest(
                objects,
                {.ray = candidate_ray,
                 .options = face_options,
                 .target_object_id = target_object_id},
                object_bounds, face_geometry);
        if (ray_distance < 0.0F || knife_screen_component_is_occluded(
                                      objects, object_index, mode, vertex.id,
                                      {}, ray_distance,
                                      candidate_surface_hit)) {
          continue;
        }
        const quader::QVec3 closest =
            candidate_ray.origin + candidate_ray.direction * ray_distance;
        const quader_poly::ElementId face_id = knife_pick_context_face_id(
            object.document, mode, vertex.id, {}, candidate_ray,
            candidate_surface_hit, object.id);
        const quader_poly::Face *face =
            quader_poly::find_face(object.document, face_id);
        KnifeScreenPickCandidate candidate;
        candidate.hit = {
            .hit = true,
            .object_id = object.id,
            .object_index = object_index,
            .material_id = object.material_id,
            .kind = quader_poly::ElementKind::Vertex,
            .vertex_id = vertex.id,
            .face_id = face_id,
            .local_position = vertex.position,
            .local_normal = face != nullptr
                                ? quader_poly::face_normal(object.document,
                                                           *face)
                                : QVec3{0.0F, 1.0F, 0.0F},
            .ray_distance = ray_distance,
            .pick_distance = quader::length(vertex.position - closest),
        };
        candidate.hit.signature =
            quader_editor_core::pick_hit_signature(candidate.hit);
        candidate.screen_distance_squared = screen_distance_squared;
        if (knife_screen_pick_candidate_is_better(candidate, best)) {
          best = std::move(candidate);
        }
      }
      continue;
    }

    for (const quader_poly::Edge edge :
         quader_poly::document_edges(object.document)) {
      const quader_poly::Vertex *a =
          quader_poly::find_vertex(object.document, edge.a);
      const quader_poly::Vertex *b =
          quader_poly::find_vertex(object.document, edge.b);
      if (a == nullptr || b == nullptr) {
        continue;
      }
      QVec2 screen_a;
      QVec2 screen_b;
      if (!quader_editor_core::project_viewport_point(camera, a->position,
                                                      screen_a) ||
          !quader_editor_core::project_viewport_point(camera, b->position,
                                                      screen_b)) {
        continue;
      }
      const KnifeScreenSegmentProjection screen_projection =
          knife_pick_screen_segment_projection(event.screen_position, screen_a,
                                               screen_b);
      if (screen_projection.distance_squared > maximum_distance_squared) {
        continue;
      }

      const float world_factor =
          knife_world_segment_factor_from_screen_projection(
              camera, a->position, b->position, screen_projection.factor);
      const QVec3 world_position =
          a->position + (b->position - a->position) * world_factor;
      const quader_poly::Ray candidate_ray =
          quader_editor_core::ray_from_viewport_screen_point(
              camera, screen_projection.closest);
      const float ray_distance =
          dot(world_position - candidate_ray.origin, candidate_ray.direction);
      const QVec3 closest =
          candidate_ray.origin + candidate_ray.direction * ray_distance;
      const std::optional<quader_editor_core::PickHitRecord>
          candidate_surface_hit = quader_editor_core::pick_nearest(
              objects,
              {.ray = candidate_ray,
               .options = face_options,
               .target_object_id = target_object_id},
              object_bounds, face_geometry);
      if (ray_distance < 0.0F ||
          knife_screen_component_is_occluded(
              objects, object_index, mode, quader_poly::kInvalidElementId,
              edge, ray_distance, candidate_surface_hit)) {
        continue;
      }
      const quader_poly::ElementId face_id = knife_pick_context_face_id(
          object.document, mode, quader_poly::kInvalidElementId, edge,
          candidate_ray, candidate_surface_hit, object.id);
      const quader_poly::Face *face =
          quader_poly::find_face(object.document, face_id);
      KnifeScreenPickCandidate candidate;
      candidate.hit = {
          .hit = true,
          .object_id = object.id,
          .object_index = object_index,
          .material_id = object.material_id,
          .kind = quader_poly::ElementKind::Edge,
          .edge = quader_poly::make_edge(edge.a, edge.b),
          .face_id = face_id,
          .local_position = world_position,
          .local_normal = face != nullptr
                              ? quader_poly::face_normal(object.document, *face)
                              : QVec3{0.0F, 1.0F, 0.0F},
          .ray_distance = ray_distance,
          .pick_distance = length(world_position - closest),
      };
      candidate.hit.signature =
          quader_editor_core::pick_hit_signature(candidate.hit);
      candidate.screen_distance_squared = screen_projection.distance_squared;
      if (knife_screen_pick_candidate_is_better(candidate, best)) {
        best = std::move(candidate);
      }
    }
  }

  return best.has_value()
             ? std::optional<quader_editor_core::PickHitRecord>(best->hit)
             : std::nullopt;
}

quader_editor_core::ViewportInteractionTool
to_runtime(ModelingViewportTool tool) {
  switch (tool) {
  case ModelingViewportTool::Move:
    return quader_editor_core::ViewportInteractionTool::Move;
  case ModelingViewportTool::Extend:
    return quader_editor_core::ViewportInteractionTool::Extend;
  case ModelingViewportTool::Extrude:
    return quader_editor_core::ViewportInteractionTool::Extrude;
  case ModelingViewportTool::Rotate:
    return quader_editor_core::ViewportInteractionTool::Rotate;
  case ModelingViewportTool::Scale:
    return quader_editor_core::ViewportInteractionTool::Scale;
  case ModelingViewportTool::Box:
  case ModelingViewportTool::Cylinder:
  case ModelingViewportTool::Sphere:
    return quader_editor_core::ViewportInteractionTool::Box;
  case ModelingViewportTool::InsertEdgeLoop:
    return quader_editor_core::ViewportInteractionTool::InsertEdgeLoop;
  case ModelingViewportTool::Knife:
    return quader_editor_core::ViewportInteractionTool::Knife;
  case ModelingViewportTool::Cut:
    return quader_editor_core::ViewportInteractionTool::Cut;
  case ModelingViewportTool::Poly:
    return quader_editor_core::ViewportInteractionTool::Poly;
  case ModelingViewportTool::Mirror:
    return quader_editor_core::ViewportInteractionTool::Mirror;
  case ModelingViewportTool::Select:
    return quader_editor_core::ViewportInteractionTool::Select;
  }
  return quader_editor_core::ViewportInteractionTool::Select;
}

ModelingViewportTool
from_runtime_viewport_tool(quader_editor_core::ViewportInteractionTool tool) {
  switch (tool) {
  case quader_editor_core::ViewportInteractionTool::Move:
    return ModelingViewportTool::Move;
  case quader_editor_core::ViewportInteractionTool::Extend:
    return ModelingViewportTool::Extend;
  case quader_editor_core::ViewportInteractionTool::Extrude:
    return ModelingViewportTool::Extrude;
  case quader_editor_core::ViewportInteractionTool::Rotate:
    return ModelingViewportTool::Rotate;
  case quader_editor_core::ViewportInteractionTool::Scale:
    return ModelingViewportTool::Scale;
  case quader_editor_core::ViewportInteractionTool::Box:
    return ModelingViewportTool::Box;
  case quader_editor_core::ViewportInteractionTool::InsertEdgeLoop:
    return ModelingViewportTool::InsertEdgeLoop;
  case quader_editor_core::ViewportInteractionTool::Knife:
    return ModelingViewportTool::Knife;
  case quader_editor_core::ViewportInteractionTool::Cut:
    return ModelingViewportTool::Cut;
  case quader_editor_core::ViewportInteractionTool::Poly:
    return ModelingViewportTool::Poly;
  case quader_editor_core::ViewportInteractionTool::Mirror:
    return ModelingViewportTool::Mirror;
  case quader_editor_core::ViewportInteractionTool::Select:
  case quader_editor_core::ViewportInteractionTool::Unknown:
    break;
  }
  return ModelingViewportTool::Select;
}

quader_editor_core::CutMode to_runtime(ModelingCutMode mode) {
  switch (mode) {
  case ModelingCutMode::DiscardLeft:
    return quader_editor_core::CutMode::KeepBack;
  case ModelingCutMode::DiscardRight:
    return quader_editor_core::CutMode::KeepFront;
  case ModelingCutMode::KeepBoth:
    return quader_editor_core::CutMode::KeepBoth;
  }
  return quader_editor_core::CutMode::KeepBoth;
}

ModelingCutMode from_runtime(quader_editor_core::CutMode mode) {
  switch (mode) {
  case quader_editor_core::CutMode::KeepBack:
    return ModelingCutMode::DiscardLeft;
  case quader_editor_core::CutMode::KeepFront:
    return ModelingCutMode::DiscardRight;
  case quader_editor_core::CutMode::KeepBoth:
    return ModelingCutMode::KeepBoth;
  }
  return ModelingCutMode::KeepBoth;
}

QVec3 from_runtime(quader::QVec3 value) { return {value.x, value.y, value.z}; }

ModelingTransform3 from_runtime(quader_poly::Transform3 value) {
  return {
      .x_axis = from_runtime(value.x_axis),
      .y_axis = from_runtime(value.y_axis),
      .z_axis = from_runtime(value.z_axis),
      .origin = from_runtime(value.origin),
  };
}

std::vector<ModelingSelectionExportVertex>
export_selected_vertices(const quader_poly::Document &document,
                         const quader_poly::Selection &selection) {
  std::vector<ModelingSelectionExportVertex> vertices;
  vertices.reserve(selection.vertices.size());
  for (const quader_poly::ElementId vertex_id : selection.vertices) {
    const quader_poly::Vertex *vertex =
        quader_poly::find_vertex(document, vertex_id);
    if (vertex == nullptr) {
      continue;
    }
    vertices.push_back(ModelingSelectionExportVertex{
        .id = vertex->id,
        .position = vertex->position,
    });
  }
  return vertices;
}

std::vector<ModelingSelectionExportEdge>
export_selected_edges(const quader_poly::Document &document,
                      const quader_poly::Selection &selection) {
  std::vector<ModelingSelectionExportEdge> edges;
  edges.reserve(selection.edges.size());
  for (const quader_poly::Edge edge : selection.edges) {
    ModelingSelectionExportEdge exported{
        .a = edge.a,
        .b = edge.b,
    };
    if (const quader_poly::Vertex *vertex =
            quader_poly::find_vertex(document, edge.a)) {
      exported.a_position = vertex->position;
    }
    if (const quader_poly::Vertex *vertex =
            quader_poly::find_vertex(document, edge.b)) {
      exported.b_position = vertex->position;
    }
    edges.push_back(std::move(exported));
  }
  return edges;
}

std::vector<ModelingSelectionExportFace>
export_selected_faces(const quader_poly::Document &document,
                      const quader_poly::Selection &selection) {
  std::vector<ModelingSelectionExportFace> faces;
  faces.reserve(selection.faces.size());
  for (const quader_poly::ElementId face_id : selection.faces) {
    const quader_poly::Face *face = quader_poly::find_face(document, face_id);
    if (face == nullptr) {
      continue;
    }
    faces.push_back(ModelingSelectionExportFace{
        .id = face->id,
        .vertices = face->vertices,
    });
  }
  return faces;
}

quader_editor_core::TransformAxis to_runtime(ModelingMirrorAxis axis) {
  switch (axis) {
  case ModelingMirrorAxis::X:
    return quader_editor_core::TransformAxis::X;
  case ModelingMirrorAxis::Y:
    return quader_editor_core::TransformAxis::Y;
  case ModelingMirrorAxis::Z:
    return quader_editor_core::TransformAxis::Z;
  }
  return quader_editor_core::TransformAxis::None;
}

quader_editor_core::TransformAxis to_runtime(ModelingTransformAxis axis) {
  switch (axis) {
  case ModelingTransformAxis::X:
    return quader_editor_core::TransformAxis::X;
  case ModelingTransformAxis::Y:
    return quader_editor_core::TransformAxis::Y;
  case ModelingTransformAxis::Z:
    return quader_editor_core::TransformAxis::Z;
  case ModelingTransformAxis::XY:
    return quader_editor_core::TransformAxis::Xy;
  case ModelingTransformAxis::XZ:
    return quader_editor_core::TransformAxis::Xz;
  case ModelingTransformAxis::YZ:
    return quader_editor_core::TransformAxis::Yz;
  case ModelingTransformAxis::All:
    return quader_editor_core::TransformAxis::All;
  case ModelingTransformAxis::None:
    return quader_editor_core::TransformAxis::None;
  }
  return quader_editor_core::TransformAxis::None;
}

quader_editor_core::ViewportInteractionTool
to_runtime(ModelingTransformTool tool) {
  switch (tool) {
  case ModelingTransformTool::Move:
    return quader_editor_core::ViewportInteractionTool::Move;
  case ModelingTransformTool::Rotate:
    return quader_editor_core::ViewportInteractionTool::Rotate;
  case ModelingTransformTool::Scale:
    return quader_editor_core::ViewportInteractionTool::Scale;
  }
  return quader_editor_core::ViewportInteractionTool::Unknown;
}

quader_editor_core::TransformDragOperation
to_runtime(ModelingTransformOperation operation) {
  switch (operation) {
  case ModelingTransformOperation::Transform:
    return quader_editor_core::TransformDragOperation::Transform;
  case ModelingTransformOperation::Extrude:
    return quader_editor_core::TransformDragOperation::Extrude;
  case ModelingTransformOperation::Inset:
    return quader_editor_core::TransformDragOperation::Inset;
  }
  return quader_editor_core::TransformDragOperation::Transform;
}

quader_editor_core::ViewportFlipOrientation
to_runtime(ModelingFlipOrientation orientation) {
  switch (orientation) {
  case ModelingFlipOrientation::Horizontal:
    return quader_editor_core::ViewportFlipOrientation::Horizontal;
  case ModelingFlipOrientation::Vertical:
    return quader_editor_core::ViewportFlipOrientation::Vertical;
  }
  return quader_editor_core::ViewportFlipOrientation::Horizontal;
}

ModelingHoverTarget
from_runtime(const quader_editor_core::SelectionHoverState &hover) {
  ModelingHoverTarget target;
  target.object_id = hover.object_id;
  switch (hover.kind) {
  case quader_editor_core::SelectionHoverKind::Object:
    target.kind = ModelingActiveSelectionKind::Object;
    break;
  case quader_editor_core::SelectionHoverKind::Vertex:
    target.kind = ModelingActiveSelectionKind::Vertex;
    target.vertex_id = static_cast<int>(hover.vertex_id);
    break;
  case quader_editor_core::SelectionHoverKind::Edge:
    target.kind = ModelingActiveSelectionKind::Edge;
    target.edge_a = static_cast<int>(hover.edge.a);
    target.edge_b = static_cast<int>(hover.edge.b);
    break;
  case quader_editor_core::SelectionHoverKind::Face:
    target.kind = ModelingActiveSelectionKind::Face;
    target.face_id = static_cast<int>(hover.face_id);
    break;
  case quader_editor_core::SelectionHoverKind::None:
    target.kind = ModelingActiveSelectionKind::None;
    target.object_id = -1;
    break;
  }
  return target;
}

ModelingHoverSummary
from_runtime_hover_summary(const quader_editor_core::EditorSession &session,
                           const quader_editor_core::SelectionHoverState &hover,
                           bool changed) {
  return {
      .changed = changed,
      .target = from_runtime(hover),
      .target_selected =
          quader_editor_core::selection_hover_target_matches_selection(
              session.objects(), session.selection_mode(), hover),
  };
}

ModelingBoxDragState from_runtime(quader_editor_core::BoxDragState state) {
  switch (state) {
  case quader_editor_core::BoxDragState::Idle:
    return ModelingBoxDragState::Idle;
  case quader_editor_core::BoxDragState::Footprint:
    return ModelingBoxDragState::Footprint;
  case quader_editor_core::BoxDragState::Height:
    return ModelingBoxDragState::Height;
  }
  return ModelingBoxDragState::Idle;
}

const quader_editor_core::PolygonObject *
find_object(const quader_editor_core::EditorSession &session,
            int modeling_object_id) {
  const auto &objects = session.objects();
  const auto it = std::find_if(objects.begin(), objects.end(),
                               [modeling_object_id](const auto &object) {
                                 return object.id == modeling_object_id;
                               });
  return it == objects.end() ? nullptr : &(*it);
}

quader_poly::PickOptions
pick_options_from_request(const ModelingHoverRequest &request) {
  return quader_editor_core::selection_pick_options_from_radii(
      request.vertex_radius, request.edge_radius);
}

std::optional<std::array<QVec3, 3>> face_plane_points(
    const quader_poly::Document &document, quader_poly::ElementId face_id) {
  const quader_poly::Face *face = quader_poly::find_face(document, face_id);
  if (face == nullptr || face->vertices.size() < 3) {
    return std::nullopt;
  }

  const quader_poly::Vertex *anchor =
      quader_poly::find_vertex(document, face->vertices.front());
  if (anchor == nullptr) {
    return std::nullopt;
  }

  for (std::size_t first_index = 1; first_index < face->vertices.size();
       ++first_index) {
    const quader_poly::Vertex *first =
        quader_poly::find_vertex(document, face->vertices[first_index]);
    if (first == nullptr) {
      continue;
    }
    for (std::size_t second_index = first_index + 1;
         second_index < face->vertices.size(); ++second_index) {
      const quader_poly::Vertex *second =
          quader_poly::find_vertex(document, face->vertices[second_index]);
      if (second == nullptr) {
        continue;
      }
      const QVec3 normal =
          cross(first->position - anchor->position,
                second->position - anchor->position);
      if (length_squared(normal) > 0.000001F) {
        return std::array<QVec3, 3>{anchor->position, first->position,
                                    second->position};
      }
    }
  }

  return std::nullopt;
}

quader_poly::PlaneCutRequest
plane_cut_request_from_points(const std::array<QVec3, 3> &points) {
  return {
      .first_point = points[0],
      .second_point = points[1],
      .third_point = points[2],
      .mode = quader_poly::PlaneCutMode::KeepBoth,
  };
}

ModelingBoxPreview
from_runtime(const quader_editor_core::BoxPreviewDescriptor &preview,
             const std::vector<quader::QVec3> &corners = {}) {
  ModelingBoxPreview result;
  result.ok = preview.ok;
  result.valid = preview.ok && preview.footprint.footprint.size() >= 4U &&
                 !preview.bounds.empty;
  result.drag_state = from_runtime(preview.drag_state);
  result.message = preview.message;
  result.normal = from_runtime(preview.normal);
  result.distance = preview.distance;
  result.bounds_empty = preview.bounds.empty;
  result.bounds = {
      .min = from_runtime(preview.bounds.min),
      .max = from_runtime(preview.bounds.max),
  };

  result.footprint_count = std::min<std::size_t>(
      result.footprint.size(), preview.footprint.footprint.size());
  for (std::size_t index = 0; index < result.footprint_count; ++index) {
    result.footprint[index] = from_runtime(preview.footprint.footprint[index]);
  }

  result.corner_count =
      std::min<std::size_t>(result.corners.size(), corners.size());
  for (std::size_t index = 0; index < result.corner_count; ++index) {
    result.corners[index] = from_runtime(corners[index]);
  }
  return result;
}

ModelingCommandDescriptor make_box_tool_descriptor() {
  const ToolDescriptor *tool =
      find_viewport_tool_descriptor(ModelingViewportTool::Box);
  return ModelingCommandDescriptor{
      .command = ModelingCommandId::ActivateBoxTool,
      .id = tool != nullptr ? std::string{tool->id} : std::string{},
      .label = tool != nullptr ? std::string{tool->label} : std::string{},
      .shortcut = tool != nullptr ? std::string{tool->shortcut} : std::string{},
      .viewport_scoped = tool != nullptr && tool->owns_viewport,
      .rebuilds_mesh_on_success = true,
  };
}

ModelingCommandDescriptor make_create_box_descriptor() {
  return ModelingCommandDescriptor{
      .command = ModelingCommandId::CreateBoxFromBounds,
      .id = "create_box_from_bounds",
      .label = "Create Box From Bounds",
      .shortcut = "",
      .viewport_scoped = true,
      .rebuilds_mesh_on_success = true,
  };
}

} // namespace

/**
 * Stores the private runtime session state for a Modeling Session Memento.
 */
struct ModelingSessionMemento::Impl {
  Impl(quader_editor_core::SessionState snapshot,
       std::unordered_map<int, ModelingObjectSceneIdentity>
           scene_identity_snapshot,
       std::unordered_set<AssetId> mesh_asset_id_snapshot,
       std::unordered_map<AssetId, MeshAsset> mesh_asset_snapshot)
      : state(std::move(snapshot)),
        scene_identities(std::move(scene_identity_snapshot)),
        mesh_asset_ids(std::move(mesh_asset_id_snapshot)),
        mesh_assets(std::move(mesh_asset_snapshot)) {}

  quader_editor_core::SessionState state;
  std::unordered_map<int, ModelingObjectSceneIdentity> scene_identities;
  std::unordered_set<AssetId> mesh_asset_ids;
  std::unordered_map<AssetId, MeshAsset> mesh_assets;
};

ModelingSessionMemento::ModelingSessionMemento() = default;
ModelingSessionMemento::~ModelingSessionMemento() = default;

ModelingSessionMemento::ModelingSessionMemento(std::shared_ptr<const Impl> impl)
    : impl_(std::move(impl)) {}

bool ModelingSessionMemento::valid() const { return static_cast<bool>(impl_); }

/**
 * Stores private runtime selection state for a modeling box-selection baseline.
 */
struct ModelingSelectionBoxBaseline::Impl {
  explicit Impl(quader_editor_core::RectangleSelectionState selection)
      : selection(std::move(selection)) {}

  quader_editor_core::RectangleSelectionState selection;
};

ModelingSelectionBoxBaseline::ModelingSelectionBoxBaseline() = default;
ModelingSelectionBoxBaseline::~ModelingSelectionBoxBaseline() = default;

ModelingSelectionBoxBaseline::ModelingSelectionBoxBaseline(
    std::shared_ptr<const Impl> impl)
    : impl_(std::move(impl)) {}

bool ModelingSelectionBoxBaseline::valid() const {
  return static_cast<bool>(impl_);
}

/**
 * Owns Impl behavior within the modeling application service layer.
 */
class ModelingSessionService::Impl : private ModelingTransformDragPort {
  /**
   * Captures the complete runtime-owned state that a creation projection must
   * restore when its SceneDocument or AssetRegistry publication fails.
   */
  struct CreationProjectionCheckpoint {
    quader_editor_core::EditorSession session;
    quader_editor_core::EditorRuntimeState runtime;
  };

  /// Precomputes one source's identity and representable local transforms.
  struct TransformedDuplicateSourcePlan {
    EntityId source_entity = kInvalidEntityId;
    int source_modeling_object_id = kInvalidModelingObjectId;
    ModelingObjectSceneIdentity identity;
    std::vector<TransformComponent> duplicate_local_transforms;
    std::vector<QMat4> expected_duplicate_world_transforms;
  };

  /// Returns source-grouped elements from one atomic duplicate batch.
  struct TransformedDuplicateBatchResult {
    ModelingCommandResult command;
    std::vector<std::vector<ModelingObjectRecord>> source_elements;
  };

public:
  Impl(SceneDocument &scene, AssetRegistry &assets,
       ModelingOperationPreviewCheckpointHooks preview_checkpoint_hooks)
      : scene_(scene), assets_(assets), scene_sync_service_(object_registry_),
        preview_checkpoint_hooks_(std::move(preview_checkpoint_hooks)) {
    selection_service_.set_app_selection_mode_provider(
        [this]() { return app_selection_mode_; });
  }

  [[nodiscard]] ModelingCommandDescriptor box_tool_command() const {
    return make_box_tool_descriptor();
  }

  [[nodiscard]] ModelingCommandDescriptor
  create_box_from_bounds_command() const {
    return make_create_box_descriptor();
  }

  [[nodiscard]] std::vector<ModelingObjectRecord> objects() const {
    return object_registry_.records(scene_);
  }

  [[nodiscard]] std::optional<EntityId>
  entity_for_modeling_object(int modeling_object_id) const {
    return object_registry_.entity_for_modeling_object(modeling_object_id);
  }

  [[nodiscard]] std::optional<int>
  modeling_object_for_entity(EntityId entity) const {
    return object_registry_.modeling_object_for_entity(entity);
  }

  [[nodiscard]] std::uint64_t modeling_object_map_revision() const {
    return object_registry_.revision();
  }

  [[nodiscard]] std::optional<MeshAsset>
  compile_object_mesh(int modeling_object_id) const {
    const quader_editor_core::PolygonObject *object =
        find_object(session_, modeling_object_id);
    if (object == nullptr) {
      return std::nullopt;
    }
    return mesh_sync_service_.compile_object_mesh(*object);
  }

  [[nodiscard]] std::optional<EntityId>
  pick_object_entity(const ModelingPickSelectRequest &request) const {
    const std::optional<int> object_id = selection_service_.pick_object_id(
        session_, scene_, object_registry_, request);
    if (!object_id.has_value()) {
      return std::nullopt;
    }
    if (const std::optional<EntityId> participant =
            selection_service_.scene_target_for_picking_participant(
                *object_id)) {
      return participant;
    }
    return object_registry_.entity_for_modeling_object(*object_id);
  }

  [[nodiscard]] std::vector<EntityId>
  box_pick_object_entities(const ModelingBoxSelectRequest &request) const {
    const std::vector<int> object_ids = selection_service_.box_pick_object_ids(
        session_, scene_, object_registry_, request);
    std::vector<EntityId> entities;
    entities.reserve(object_ids.size());
    for (const int object_id : object_ids) {
      std::optional<EntityId> entity =
          selection_service_.scene_target_for_picking_participant(object_id);
      if (!entity.has_value()) {
        entity = object_registry_.entity_for_modeling_object(object_id);
      }
      if (!entity.has_value() || std::find(entities.begin(), entities.end(),
                                           *entity) != entities.end()) {
        continue;
      }
      entities.push_back(*entity);
    }
    return entities;
  }

  [[nodiscard]] std::optional<QVec3> selection_center() const {
    if (const std::optional<SceneBounds> bounds = selection_bounds()) {
      return bounds_center(*bounds);
    }
    return std::nullopt;
  }

  [[nodiscard]] std::optional<SceneBounds> selection_bounds() const {
    const ModelingInstanceEditScope edit_scope =
        instance_edit_scope_provider_ ? instance_edit_scope_provider_()
                                      : ModelingInstanceEditScope{};
    if (edit_scope.occurrence != kInvalidEntityId &&
        edit_scope.definition != kInvalidEntityId) {
      std::optional<SceneBounds> bounds;
      for (const quader_editor_core::PolygonObject &object :
           session_.objects()) {
        if (!object.selected && object.selection.empty() &&
            object.id != session_.active_object_id()) {
          continue;
        }
        const std::optional<EntityId> entity =
            object_registry_.entity_for_modeling_object(object.id);
        if (!entity.has_value() ||
            scene_.containing_instance_definition(*entity) !=
                edit_scope.definition) {
          continue;
        }
        const std::optional<QMat4> world =
            modeling_object_world_transform(object.id);
        if (!world.has_value()) {
          continue;
        }
        const bool whole_object =
            session_.selection_mode() ==
            quader_editor_core::SelectionMode::Object;
        const std::vector<quader_poly::ElementId> selected_vertices =
            whole_object ? std::vector<quader_poly::ElementId>{}
                         : quader_poly::selected_vertex_ids(object.document,
                                                            object.selection);
        const auto include_vertex = [&](const quader_poly::Vertex &vertex) {
          const QVec3 point = transform_point(*world, vertex.position);
          if (!bounds.has_value()) {
            bounds = SceneBounds{.min = point, .max = point};
          } else {
            include_point(*bounds, point);
          }
        };
        if (whole_object) {
          for (const quader_poly::Vertex &vertex : object.document.vertices) {
            include_vertex(vertex);
          }
        } else {
          for (const quader_poly::ElementId vertex_id : selected_vertices) {
            if (const quader_poly::Vertex *vertex =
                    quader_poly::find_vertex(object.document, vertex_id)) {
              include_vertex(*vertex);
            }
          }
        }
      }
      return bounds;
    }
    return selection_projection_service_.world_selection_bounds(
        session_, scene_, assets_, object_registry_);
  }

  DocumentCommandResult
  translate_scene_selection_by_world_delta(QVec3 world_delta,
                                           std::string label) {
    if (quader::length_squared(world_delta) <= 0.0000001f) {
      return document_command_unchanged_result();
    }
    SceneTransformDragService placement;
    if (!placement.begin(scene_, selection_center().value_or(QVec3{}), true,
                         std::move(label),
                         scene_selection_display_from_scene())) {
      return document_command_invalid_request_result(
          "Scene placement could not begin.");
    }
    const DocumentCommandResult moved =
        placement.preview_world_translation_delta(scene_, world_delta);
    if (!moved.ok) {
      (void)placement.cancel();
      return moved;
    }
    return placement.accept();
  }

  DocumentCommandResult transform_scene_selection_by_world_delta(
      const QMat4 &world_delta, std::string label) {
    SceneTransformDragService placement;
    if (!placement.begin(scene_, selection_center().value_or(QVec3{}), true,
                         std::move(label),
                         scene_selection_display_from_scene())) {
      return document_command_invalid_request_result(
          "Scene transform could not begin.");
    }
    const DocumentCommandResult transformed =
        placement.preview_world_transform_delta(scene_, world_delta);
    if (!transformed.ok) {
      (void)placement.cancel();
      return transformed;
    }
    return placement.accept();
  }

  [[nodiscard]] ModelingSelectionMode selection_mode() const {
    return selection_service_.selection_mode(session_, app_selection_mode_);
  }

  [[nodiscard]] ModelingSelectionSummary selection_summary() const {
    return selection_service_.selection_summary(session_, scene_,
                                                app_selection_mode_);
  }

  [[nodiscard]] ModelingSelectionExport export_selection() const {
    ModelingSelectionExport exported;
    exported.summary = selection_summary();

    const std::vector<EntityId> selected_scene_entities =
        scene_.selected_entities();
    std::unordered_set<EntityId> selected_scene_entity_set;
    selected_scene_entity_set.reserve(selected_scene_entities.size());
    for (const EntityId entity : selected_scene_entities) {
      selected_scene_entity_set.insert(entity);
    }

    for (const quader_editor_core::PolygonObject &object : session_.objects()) {
      const std::optional<EntityId> entity =
          object_registry_.entity_for_modeling_object(object.id);
      const bool scene_selected =
          entity.has_value() && selected_scene_entity_set.contains(*entity);
      if (!object.selected && !scene_selected &&
          object.id != exported.summary.active_object_id) {
        continue;
      }

      ModelingSelectionExportObject object_export;
      object_export.modeling_object_id = object.id;
      object_export.entity = entity.value_or(kInvalidEntityId);
      object_export.selected = object.selected || scene_selected;
      object_export.selection = object.selection;
      object_export.selected_vertices =
          export_selected_vertices(object.document, object.selection);
      object_export.selected_edges =
          export_selected_edges(object.document, object.selection);
      object_export.selected_faces =
          export_selected_faces(object.document, object.selection);

      if (object_export.entity != kInvalidEntityId &&
          scene_.exists(object_export.entity)) {
        object_export.entity_uuid = scene_.uuid(object_export.entity);
        if (const NameComponent *name = scene_.name(object_export.entity)) {
          object_export.name = name->name;
        }
      }

      exported.objects.push_back(std::move(object_export));
    }

    return exported;
  }

  [[nodiscard]] ModelingSelectionBoxBaseline
  capture_box_selection_baseline() const {
    return ModelingSelectionBoxBaseline(
        std::make_shared<ModelingSelectionBoxBaseline::Impl>(
            selection_service_.rectangle_selection_state(session_)));
  }

  [[nodiscard]] std::uint64_t selection_overlay_signature(
      ModelingSelectionOverlayOptions options = {}) const {
    return overlay_service_.selection_overlay_signature(runtime_, options);
  }

  [[nodiscard]] ViewportOverlayFrame
  selection_overlay_frame(ModelingSelectionOverlayOptions options = {}) const {
    return overlay_service_.selection_overlay_frame(
        session_, runtime_, scene_, object_registry_, selection_service_,
        options);
  }

  [[nodiscard]] std::vector<ModelingToolPreviewMesh>
  tool_preview_meshes() const {
    return overlay_service_.tool_preview_meshes(
        session_, runtime_, scene_, object_registry_, selection_service_);
  }

  [[nodiscard]] std::uint64_t
  transformed_picking_projection_build_count() const {
    return selection_service_.transformed_picking_projection_build_count();
  }

  [[nodiscard]] std::uint64_t
  rectangle_picking_projection_build_count() const {
    return selection_service_.rectangle_picking_projection_build_count();
  }

  [[nodiscard]] std::uint64_t
  rectangle_selection_resolution_build_count() const {
    return selection_service_.rectangle_selection_resolution_build_count();
  }

  [[nodiscard]] std::uint64_t local_face_geometry_build_count() const {
    return selection_service_.local_face_geometry_build_count();
  }

  [[nodiscard]] static std::unordered_set<int> live_modeling_object_ids(
      const std::vector<quader_editor_core::PolygonObject> &objects) {
    std::unordered_set<int> ids;
    ids.reserve(objects.size());
    for (const quader_editor_core::PolygonObject &object : objects) {
      ids.insert(object.id);
    }
    return ids;
  }

  [[nodiscard]] ModelingSessionMemento capture_history_memento_for_state(
      const quader_editor_core::SessionState &state,
      std::span<const AssetId> excluded_mesh_asset_ids = {}) const {
    const std::unordered_set<int> live_ids =
        live_modeling_object_ids(state.objects);
    std::unordered_map<int, ModelingObjectSceneIdentity> scene_identities =
        object_registry_.capture_scene_identities_for_modeling_objects(
            scene_, live_ids);
    std::unordered_map<AssetId, MeshAsset> mesh_assets;
    mesh_assets.reserve(scene_identities.size());
    for (const auto &[modeling_object_id, identity] : scene_identities) {
      (void)modeling_object_id;
      if (!identity.mesh.has_value() ||
          identity.mesh->mesh == kInvalidAssetId ||
          mesh_assets.contains(identity.mesh->mesh)) {
        continue;
      }
      if (const MeshAsset *mesh = assets_.mesh(identity.mesh->mesh)) {
        mesh_assets.emplace(identity.mesh->mesh,
                            clone_mesh_asset_payload(*mesh));
      }
    }
    const std::vector<AssetId> live_mesh_asset_ids = assets_.mesh_ids();
    std::unordered_set<AssetId> captured_mesh_asset_ids(
        live_mesh_asset_ids.begin(), live_mesh_asset_ids.end());
    for (const AssetId excluded : excluded_mesh_asset_ids) {
      captured_mesh_asset_ids.erase(excluded);
    }
    return ModelingSessionMemento(
        std::make_shared<ModelingSessionMemento::Impl>(
            state, std::move(scene_identities),
            std::move(captured_mesh_asset_ids),
            std::move(mesh_assets)));
  }

  void refresh_live_scene_identities() {
    for (const quader_editor_core::PolygonObject &object : session_.objects()) {
      const std::optional<EntityId> entity =
          object_registry_.entity_for_modeling_object(object.id);
      if (entity.has_value() && scene_.exists(*entity)) {
        object_registry_.remember_scene_entity(object.id, scene_, *entity);
      }
    }
  }

  [[nodiscard]] ModelingSessionMemento capture_history_memento() const {
    return capture_history_memento_for_state(session_.state());
  }

  [[nodiscard]] ModelingSessionMemento capture_history_memento_without_objects(
      const quader_editor_core::SessionState &selection_baseline,
      std::span<const int> excluded_object_ids) const {
    quader_editor_core::SessionState state = session_.state();
    const std::unordered_set<int> excluded(excluded_object_ids.begin(),
                                           excluded_object_ids.end());
    const auto excluded_identities =
        object_registry_.capture_scene_identities_for_modeling_objects(
            scene_, excluded);
    std::vector<AssetId> excluded_mesh_asset_ids;
    excluded_mesh_asset_ids.reserve(excluded_identities.size());
    for (const auto &[modeling_object_id, identity] : excluded_identities) {
      (void)modeling_object_id;
      if (identity.mesh.has_value() &&
          identity.mesh->mesh != kInvalidAssetId) {
        excluded_mesh_asset_ids.push_back(identity.mesh->mesh);
      }
    }
    state.objects.erase(
        std::remove_if(state.objects.begin(), state.objects.end(),
                       [&excluded](const auto &object) {
                         return excluded.contains(object.id);
                       }),
        state.objects.end());

    // Primitive content is rebased onto the current document so concurrent
    // scene edits survive, while selection returns to the session boundary.
    // This keeps transient primitive identities out of later selection undo.
    state.selection_mode = selection_baseline.selection_mode;
    state.active_object_id = selection_baseline.active_object_id;
    state.active_light_id = selection_baseline.active_light_id;
    for (auto &object : state.objects) {
      const auto baseline = std::find_if(
          selection_baseline.objects.begin(), selection_baseline.objects.end(),
          [&object](const auto &candidate) {
            return candidate.id == object.id;
          });
      if (baseline != selection_baseline.objects.end()) {
        object.selected = baseline->selected;
        object.selection = baseline->selection;
        object.component_edit_target = baseline->component_edit_target;
      } else {
        object.selected = false;
        object.selection = {};
        object.component_edit_target = false;
      }
    }
    if (std::none_of(state.objects.begin(), state.objects.end(),
                     [&state](const auto &object) {
                       return object.id == state.active_object_id;
                     })) {
      state.active_object_id = -1;
    }
    for (auto &light : state.lights) {
      const auto baseline = std::find_if(
          selection_baseline.lights.begin(), selection_baseline.lights.end(),
          [&light](const auto &candidate) {
            return candidate.id == light.id;
          });
      light.selected = baseline != selection_baseline.lights.end() &&
                       baseline->selected;
    }
    if (std::none_of(state.lights.begin(), state.lights.end(),
                     [&state](const auto &light) {
                       return light.id == state.active_light_id;
                     })) {
      state.active_light_id = -1;
    }
    return capture_history_memento_for_state(state,
                                             excluded_mesh_asset_ids);
  }

  [[nodiscard]] ModelingSessionMemento
  capture_active_preview_history_memento() const {
    const std::optional<quader_editor_core::SessionState> state =
        session_.preview_transaction_history_state();
    return state.has_value() ? capture_history_memento_for_state(*state)
                             : ModelingSessionMemento();
  }

  ModelingCommandResult
  restore_history_memento(const ModelingSessionMemento &memento) {
    if (!memento.valid()) {
      return {
          .ok = false,
          .changed = false,
          .message = "Modeling history restore point is invalid.",
      };
    }

    quader_editor_core::SessionState state = memento.impl_->state;
    const quader_editor_core::SessionState &current = session_.state();
    std::vector<AssetId> displaced_mesh_assets;
    displaced_mesh_assets.reserve(current.objects.size());
    for (const quader_editor_core::PolygonObject &object : current.objects) {
      const std::optional<EntityId> entity =
          object_registry_.entity_for_modeling_object(object.id);
      if (!entity.has_value() || !scene_.exists(*entity)) {
        continue;
      }
      const MeshComponent *mesh = scene_.mesh(*entity);
      if (mesh != nullptr && mesh->mesh != kInvalidAssetId &&
          !memento.impl_->mesh_asset_ids.contains(mesh->mesh)) {
        displaced_mesh_assets.push_back(mesh->mesh);
      }
    }
    if (!mesh_sync_service_.restore_history_mesh_assets(
            assets_, memento.impl_->mesh_assets)) {
      return {
          .ok = false,
          .changed = false,
          .message = "Modeling history mesh assets could not be restored.",
      };
    }

    object_registry_.restore_scene_identities(memento.impl_->scene_identities);
    for (const auto &[modeling_object_id, identity] :
         memento.impl_->scene_identities) {
      if (identity.entity == kInvalidEntityId ||
          !scene_.exists(identity.entity)) {
        continue;
      }
      object_registry_.bind(modeling_object_id, identity.entity);
      mesh_sync_service_.apply_scene_identity_to_entity(
          scene_, identity.entity, identity);
    }
    session_.restore_history_state(std::move(state));
    session_.clear_history();
    sync_scene_objects_from_modeling(true);
    mesh_sync_service_.remove_unreferenced_history_mesh_assets(
        scene_, assets_, displaced_mesh_assets);
    object_registry_.restore_scene_identities(memento.impl_->scene_identities);
    refresh_live_scene_identities();
    sync_scene_selection_from_modeling();
    invalidate_selection_overlay_caches();
    return {
        .ok = true,
        .changed = true,
    };
  }

  ModelingCommandResult restore_history_memento_preserving_scene_selection(
      const ModelingSessionMemento &memento) {
    const std::vector<EntityId> selected = scene_.selected_entities();
    const EntityId active = scene_.primary_selection();
    ModelingCommandResult restored = restore_history_memento(memento);
    if (restored.ok) {
      restore_scene_selection(selected, active);
    }
    return restored;
  }

  void clear_transient_state_for_document_replacement() {
    if (transform_drag_active()) {
      (void)cancel_transform_drag();
    }

    const quader_editor_core::PreviewTransactionStatus preview =
        session_.preview_transaction_status();
    if (preview.active && operation_for_preview_kind(preview.kind).has_value()) {
      (void)cancel_active_operation_preview();
    }

    (void)cancel_active_modal_tool();
    (void)runtime_.discard_transient_state(session_, true);
    tool_settings_service_.clear_preview_metadata();
    scene_workspace_preview_.reset();
    invalidate_selection_overlay_caches();
  }

  void reset_from_scene() {
    // Group selection is scene-owned even though the polygon runtime represents
    // its mode as Object. Keep that runtime projection empty so reset cannot
    // introduce a second selection source of truth.
    const bool preserve_scene_owned_group_selection =
        app_selection_mode_ == ModelingSelectionMode::Group;
    if (!preserve_scene_owned_group_selection) {
      app_selection_mode_ = ModelingSelectionMode::Object;
    }

    object_registry_.clear();
    quader_editor_core::SessionState state;
    state.selection_mode = quader_editor_core::SelectionMode::Object;
    const EntityId active_entity = scene_.primary_selection();

    for (const EntityId entity : scene_.entity_tree_order()) {
      const ModelingObjectComponent *modeling_object =
          scene_.modeling_object(entity);
      const MeshComponent *mesh_component = scene_.mesh(entity);
      if (modeling_object == nullptr ||
          modeling_object->object_id == kInvalidModelingObjectId ||
          mesh_component == nullptr ||
          mesh_component->mesh == kInvalidAssetId) {
        continue;
      }

      const MeshAsset *mesh = assets_.mesh(mesh_component->mesh);
      if (mesh == nullptr || !mesh->has_authored_polygon_document ||
          !mesh->authored_polygon_document) {
        continue;
      }

      quader_editor_core::PolygonObject object;
      object.id = modeling_object->object_id;
      object.material_id = static_cast<int>(modeling_object->material_slot);
      object.selected = !preserve_scene_owned_group_selection &&
                        scene_.selection(entity) != nullptr &&
                        scene_.selection(entity)->selected;
      // AssetRegistry interns authored topology independently from logical
      // AssetId. Adopt that immutable pin so loading 200 ordinary duplicates
      // keeps one runtime topology allocation and one picking/compile key.
      object.document.share(mesh->authored_polygon_document);
      state.objects.push_back(std::move(object));
      object_registry_.bind(modeling_object->object_id, entity);
      object_registry_.remember_scene_entity(modeling_object->object_id, scene_,
                                             entity);
      if (!preserve_scene_owned_group_selection && entity == active_entity) {
        state.active_object_id = modeling_object->object_id;
      }
    }

    state.next_object_id =
        quader_editor_core::next_polygon_object_id(state.objects, 1);
    state.content_revision = state.objects.empty() ? 0U : 1U;
    state.selection_revision = 0U;
    session_.set_state(std::move(state));
    session_.mark_clean();
    session_.clear_history();
    runtime_.reset_document_state(session_);
    mesh_sync_service_.establish_loaded_scene_sync_baseline(
        session_, scene_sync_service_);
    if (!preserve_scene_owned_group_selection) {
      (void)selection_projection_service_.project_object_selection_from_scene(
          session_, scene_, object_registry_, true);
    }
    session_.mark_clean();
    invalidate_selection_overlay_caches();
  }

  void set_instance_edit_scope_provider(
      std::function<ModelingInstanceEditScope()> provider) {
    instance_edit_scope_provider_ = provider;
    selection_service_.set_instance_edit_scope_provider(std::move(provider));
  }

  void sync_object_selection_from_scene(bool force_object_mode = false) {
    QDR_PROFILE_FUNCTION();
    if (!selection_projection_service_
             .should_project_object_selection_from_scene(session_,
                                                         force_object_mode)) {
      return;
    }
    if (!scene_objects_synced_for_current_modeling_content()) {
      sync_scene_objects_from_modeling();
    }

    selection_projection_service_.project_object_selection_from_scene(
        session_, scene_, object_registry_, force_object_mode);
  }

  void promote_drawn_plane_tool_object_selection() {
    QDR_PROFILE_FUNCTION();
    if (!scene_objects_synced_for_current_modeling_content()) {
      sync_scene_objects_from_modeling();
    }

    if (selection_projection_service_.promote_object_selection_for_tool(
            session_, scene_, object_registry_)) {
      sync_scene_selection_from_modeling();
      invalidate_selection_overlay_caches();
    }
  }

  void sync_scene_selection_from_modeling() {
    QDR_PROFILE_FUNCTION();
    if (!scene_objects_synced_for_current_modeling_content() &&
        !try_sync_scene_objects_from_modeling()) {
      return;
    }
    selection_projection_service_.project_scene_selection_from_modeling(
        session_, scene_, object_registry_);
  }

  void restore_scene_selection(const std::vector<EntityId> &selected,
                               EntityId active) {
    selection_projection_service_.restore_scene_selection(scene_, selected,
                                                          active);
  }

  /**
   * Preserves the scene identity of every source mesh while Half-Cut previews
   * optionally materialize a second runtime object for each source.
   */
  struct HalfCutSceneIdentitySnapshot {
    std::vector<int> source_object_ids;
    std::unordered_set<int> object_ids_before;
    std::unordered_map<int, ModelingObjectSceneIdentity> source_identities;
  };

  [[nodiscard]] std::optional<HalfCutSceneIdentitySnapshot>
  capture_half_cut_scene_identities(
      quader_editor_core::EditorOperationId operation,
      quader_editor_core::EditorOperationCommandPhase phase) const {
    if (operation != quader_editor_core::EditorOperationId::HalfCut) {
      return std::nullopt;
    }

    std::optional<quader_editor_core::SessionState> preview_base;
    const quader_editor_core::SessionState *source_state = nullptr;
    switch (phase) {
    case quader_editor_core::EditorOperationCommandPhase::Execute:
    case quader_editor_core::EditorOperationCommandPhase::BeginPreview:
      source_state = &session_.state();
      break;
    case quader_editor_core::EditorOperationCommandPhase::UpdatePreview:
      preview_base = session_.preview_transaction_history_state(
          quader_editor_core::PreviewTransactionKind::HalfCut);
      if (preview_base.has_value()) {
        source_state = &*preview_base;
      }
      break;
    case quader_editor_core::EditorOperationCommandPhase::CommitPreview:
    case quader_editor_core::EditorOperationCommandPhase::CancelPreview:
      break;
    }
    if (source_state == nullptr) {
      return std::nullopt;
    }

    HalfCutSceneIdentitySnapshot snapshot;
    snapshot.object_ids_before.reserve(source_state->objects.size());
    for (const quader_editor_core::PolygonObject &object :
         source_state->objects) {
      snapshot.object_ids_before.insert(object.id);
      if (object.selected) {
        snapshot.source_object_ids.push_back(object.id);
      }
    }
    if (snapshot.source_object_ids.empty()) {
      return std::nullopt;
    }

    const std::unordered_set<int> source_id_set(
        snapshot.source_object_ids.begin(), snapshot.source_object_ids.end());
    snapshot.source_identities =
        object_registry_.capture_scene_identities_for_modeling_objects(
            scene_, source_id_set);
    return snapshot;
  }

  void apply_half_cut_scene_identities(
      const HalfCutSceneIdentitySnapshot &snapshot) {
    std::vector<int> created_object_ids;
    for (const quader_editor_core::PolygonObject &object : session_.objects()) {
      if (!snapshot.object_ids_before.contains(object.id)) {
        created_object_ids.push_back(object.id);
      }
    }

    const std::size_t count =
        std::min(created_object_ids.size(), snapshot.source_object_ids.size());
    for (std::size_t index = 0; index < count; ++index) {
      const auto source_identity =
          snapshot.source_identities.find(snapshot.source_object_ids[index]);
      if (source_identity == snapshot.source_identities.end()) {
        continue;
      }
      const std::optional<EntityId> created_entity =
          object_registry_.entity_for_modeling_object(created_object_ids[index]);
      if (!created_entity.has_value() || !scene_.exists(*created_entity)) {
        continue;
      }
      mesh_sync_service_.apply_scene_identity_to_duplicate_entity(
          scene_, *created_entity, source_identity->second);
      object_registry_.remember_scene_entity(created_object_ids[index], scene_,
                                             *created_entity);
    }
  }

  [[nodiscard]] std::vector<ModelingObjectSceneIdentity>
  capture_selected_duplicate_scene_identities() const {
    QDR_PROFILE_FUNCTION();
    const std::vector<int> selected_object_ids = session_.selected_object_ids();
    return object_registry_.capture_scene_identities_for_modeling_objects(
        scene_, std::span<const int>(selected_object_ids.data(),
                                     selected_object_ids.size()));
  }

  void apply_scene_identities_to_selected_duplicates(
      const std::vector<ModelingObjectSceneIdentity> &source_identities) {
    QDR_PROFILE_FUNCTION();
    if (source_identities.empty()) {
      return;
    }

    const std::vector<int> selected_duplicate_object_ids =
        session_.selected_object_ids();
    if (selected_duplicate_object_ids.size() != source_identities.size()) {
      return;
    }
    const std::unordered_set<int> duplicate_object_set(
        selected_duplicate_object_ids.begin(),
        selected_duplicate_object_ids.end());
    std::vector<int> duplicate_object_ids;
    duplicate_object_ids.reserve(selected_duplicate_object_ids.size());
    for (const quader_editor_core::PolygonObject &object : session_.objects()) {
      if (duplicate_object_set.contains(object.id)) {
        duplicate_object_ids.push_back(object.id);
      }
    }
    if (duplicate_object_ids.size() != source_identities.size()) {
      return;
    }

    for (std::size_t index = 0; index < source_identities.size(); ++index) {
      const std::optional<EntityId> duplicate =
          object_registry_.entity_for_modeling_object(
              duplicate_object_ids[index]);
      if (!duplicate.has_value() || !scene_.exists(*duplicate)) {
        continue;
      }

      mesh_sync_service_.apply_scene_identity_to_duplicate_entity(
          scene_, *duplicate, source_identities[index]);
      object_registry_.remember_scene_entity(duplicate_object_ids[index],
                                             scene_, *duplicate);
    }
  }

  ModelingCommandResult
  duplicate_selected_hierarchy(DocumentCommandService &document_commands,
                               ModelingSelectionMode result_selection_mode) {
    return hierarchy_duplicate_service_.duplicate_selected_entity_hierarchy(
        session_, scene_, object_registry_, document_commands,
        ModelingHierarchyDuplicateRuntime{
            .ensure_scene_objects_synced =
                [&]() {
                  if (!scene_objects_synced_for_current_modeling_content()) {
                    sync_scene_objects_from_modeling();
                  }
                },
            .set_selection_mode =
                [&](ModelingSelectionMode mode) {
                  return set_selection_mode(mode);
                },
            .project_object_selection_from_scene =
                [&]() { sync_object_selection_from_scene(); },
            .duplicate_selected_meshes =
                [&]() {
                  return execute_instant_operation(
                      ModelingInstantOperationId::DuplicateMeshes, false);
                },
        },
        result_selection_mode);
  }

  ModelingCommandResult duplicate_selected_hierarchy_for_group_drag(
      DocumentCommandService &document_commands) {
    return duplicate_selected_hierarchy(document_commands,
                                        ModelingSelectionMode::Group);
  }

  ModelingCommandResult duplicate_selected_entity_hierarchy(
      DocumentCommandService &document_commands) {
    return duplicate_selected_hierarchy(document_commands,
                                        ModelingSelectionMode::Object);
  }

  ModelingCommandResult set_selection_mode(ModelingSelectionMode mode) {
    bool canceled_preview = false;
    const auto absorb_cancel = [&canceled_preview](
                                   ModelingCommandResult result) {
      canceled_preview = canceled_preview || result.changed;
      return result;
    };
    if (tool_settings_target().operation_preview_active) {
      const ModelingCommandResult canceled =
          absorb_cancel(cancel_active_operation_preview());
      if (!canceled.ok) {
        return canceled;
      }
    }
    if (transform_drag_active()) {
      const ModelingCommandResult canceled =
          absorb_cancel(cancel_transform_drag());
      if (!canceled.ok) {
        return canceled;
      }
    }
    if (has_transform_preview()) {
      const ModelingCommandResult canceled =
          absorb_cancel(cancel_transform_preview());
      if (!canceled.ok) {
        return canceled;
      }
    }
    if (modal_tool_owns_viewport()) {
      const ModelingCommandResult canceled =
          absorb_cancel(cancel_active_modal_tool());
      if (!canceled.ok) {
        return canceled;
      }
    }

    if (mode == ModelingSelectionMode::Group) {
      std::vector<EntityId> retained_scene_selection;
      retained_scene_selection.reserve(scene_.selected_entities().size());
      for (const EntityId entity : scene_.selected_entities()) {
        if (scene_.instance_occurrence(entity) == nullptr &&
            !scene_.is_instance_definition_entity(entity) &&
            !scene_.is_instance_definition_member(entity)) {
          retained_scene_selection.push_back(entity);
        }
      }
      const bool removed_instance_scope_selection =
          retained_scene_selection.size() != scene_.selected_entities().size();
      if (removed_instance_scope_selection) {
        const EntityId previous_active = scene_.primary_selection();
        const EntityId retained_active =
            std::find(retained_scene_selection.begin(),
                      retained_scene_selection.end(), previous_active) !=
                    retained_scene_selection.end()
                ? previous_active
                : (retained_scene_selection.empty()
                       ? kInvalidEntityId
                       : retained_scene_selection.back());
        restore_scene_selection(retained_scene_selection, retained_active);
      }
      ModelingCommandResult result =
          selection_service_.set_group_selection_mode(session_,
                                                      app_selection_mode_);
      result.changed = result.changed || canceled_preview ||
                       removed_instance_scope_selection;
      if (result.changed) {
        invalidate_selection_overlay_caches();
      }
      return result;
    }

    app_selection_mode_ = mode;
    std::vector<EntityId> scene_only_selection;
    scene_only_selection.reserve(scene_.selected_entities().size());
    for (const EntityId entity : scene_.selected_entities()) {
      if (scene_.modeling_object(entity) == nullptr &&
          (mode == ModelingSelectionMode::Object ||
           scene_.instance_occurrence(entity) == nullptr)) {
        scene_only_selection.push_back(entity);
      }
    }
    const EntityId active_scene_only_selection =
        scene_.modeling_object(scene_.primary_selection()) == nullptr &&
                (mode == ModelingSelectionMode::Object ||
                 scene_.instance_occurrence(scene_.primary_selection()) ==
                     nullptr)
            ? scene_.primary_selection()
            : kInvalidEntityId;
    if (!scene_only_selection.empty()) {
      sync_object_selection_from_scene();
    }

    ModelingCommandResult result =
        selection_service_.set_runtime_selection_mode(session_, runtime_, mode);
    result.changed = result.changed || canceled_preview;
    invalidate_selection_overlay_caches();
    if (mode == ModelingSelectionMode::Object &&
        !scene_only_selection.empty()) {
      sync_object_selection_from_scene();
    }
    sync_scene_selection_from_modeling();
    selection_projection_service_.add_scene_selection(
        scene_, scene_only_selection, active_scene_only_selection);
    return result;
  }

  ModelingCommandResult select_all() {
    if (const std::optional<ModelingCommandResult> scoped =
            selection_service_.select_all_in_instance_edit_scope(
                session_, scene_, object_registry_, false)) {
      if (scoped->changed) {
        sync_scene_selection_from_modeling();
        invalidate_selection_overlay_caches();
      }
      return *scoped;
    }
    return execute_runtime_no_settings(
        quader_editor_core::EditorRuntimeCommandId::SelectAll, false);
  }

  ModelingCommandResult invert_selection() {
    if (const std::optional<ModelingCommandResult> scoped =
            selection_service_.select_all_in_instance_edit_scope(
                session_, scene_, object_registry_, true)) {
      if (scoped->changed) {
        sync_scene_selection_from_modeling();
        invalidate_selection_overlay_caches();
      }
      return *scoped;
    }
    return execute_runtime_no_settings(
        quader_editor_core::EditorRuntimeCommandId::InvertSelection, false);
  }

  ModelingCommandResult clear_selection() {
    return execute_runtime_no_settings(
        quader_editor_core::EditorRuntimeCommandId::ClearSelection, false);
  }

  ModelingCommandResult delete_component_selection() {
    const ModelingSelectionSummary summary = selection_summary();
    if (scene_aware_delete_uses_document_route(summary.mode)) {
      return {
          .ok = false,
          .changed = false,
          .message = "Delete selection must use the scene-aware command route.",
      };
    }
    return execute_runtime_no_settings(
        quader_editor_core::EditorRuntimeCommandId::DeleteSelection, true);
  }

  [[nodiscard]] bool has_selected_scene_aware_delete_root() const {
    return scene_aware_delete_has_selected_root(scene_);
  }

  std::unique_ptr<IUndoableCommand> create_delete_selection_command() {
    return create_scene_aware_delete_selection_command(
        scene_,
        SceneAwareDeleteRuntime{
            .capture_modeling_memento =
                [&]() { return capture_history_memento(); },
            .restore_modeling_memento =
                [&](const ModelingSessionMemento &memento) {
                  return restore_history_memento(memento);
                },
            .app_selection_mode = [&]() { return app_selection_mode_; },
            .set_app_selection_mode =
                [&](ModelingSelectionMode mode) { app_selection_mode_ = mode; },
            .delete_modeling_objects =
                [&](const std::vector<int> &modeling_object_ids) {
                  if (modeling_object_ids.empty()) {
                    return ModelingCommandResult{.ok = true, .changed = false};
                  }
                  session_.set_selection_mode(
                      quader_editor_core::SelectionMode::Object);
                  session_.set_selected_object_ids(modeling_object_ids,
                                                   modeling_object_ids.front());
                  return execute_runtime_command(
                      RuntimeToolCommandService::no_settings(
                          quader_editor_core::EditorRuntimeCommandId::
                              DeleteSelection),
                      true);
                },
            .entity_for_modeling_object =
                [&](int modeling_object_id) {
                  return object_registry_.entity_for_modeling_object(
                      modeling_object_id);
                },
            .sync_object_selection_from_scene =
                [&](bool force_object_mode) {
                  sync_object_selection_from_scene(force_object_mode);
                },
            .clear_runtime_object_selection_for_group_mode =
                [&]() {
                  session_.set_selection_mode(
                      quader_editor_core::SelectionMode::Object);
                  session_.set_selected_object_ids({}, -1);
                },
            .invalidate_selection_overlay_caches =
                [&]() { invalidate_selection_overlay_caches(); },
        });
  }

  ModelingCommandResult copy_selection() {
    ModelingCommandResult result = execute_runtime_command(
        clipboard_service_.make_copy_command(), false);
    if (result.ok) {
      clipboard_scene_identities_ =
          capture_selected_duplicate_scene_identities();
      clipboard_world_center_ = selection_center();
    }
    return result;
  }

  ModelingCommandResult paste_selection() {
    quader_editor_core::EditorRuntimeCommand command =
        clipboard_service_.make_paste_command();
    if (auto *paste_settings = std::get_if<
            quader_editor_core::PasteCopiedMeshesRuntimeCommandSettings>(
            &command.settings);
        paste_settings != nullptr && !runtime_.clipboard_objects().empty()) {
      const quader_editor_core::Bounds3 copied_bounds =
          quader_editor_core::document_bounds(runtime_.clipboard_objects());
      if (!copied_bounds.empty) {
        paste_settings->target_center = {
            (copied_bounds.min + copied_bounds.max) * 0.5f, true};
      }
    }
    const std::optional<QVec3> paste_target = selection_center();
    const std::uint64_t content_before = session_.state().content_revision;
    const quader_editor_core::EditorRuntimeCommandResult result =
        quader_editor_core::execute_runtime_command(session_, runtime_,
                                                    command);
    if (session_.state().content_revision != content_before) {
      sync_scene_objects_from_modeling();
    }
    sync_scene_selection_from_modeling();
    if (result.command.ok && result.command.changed &&
        !clipboard_scene_identities_.empty()) {
      apply_scene_identities_to_selected_duplicates(
          clipboard_scene_identities_);
      sync_scene_selection_from_modeling();
      const QVec3 paste_delta =
          paste_target.has_value() && clipboard_world_center_.has_value()
              ? *paste_target - *clipboard_world_center_
              : QVec3{1.0f, 0.0f, 1.0f};
      const DocumentCommandResult placed =
          translate_scene_selection_by_world_delta(paste_delta,
                                                   "Place Pasted Meshes");
      if (!placed.ok) {
        return {
            .ok = false,
            .changed = true,
            .message = placed.message,
        };
      }
    }
    return {
        .ok = result.command.ok,
        .changed = result.command.changed,
        .message = result.command.message,
    };
  }

  ModelingCommandResult repeat_last_action() {
    const std::uint64_t content_before = session_.state().content_revision;
    const quader_editor_core::CommandResult result =
        execute_repeat_last_action_scene_aware();
    if (session_.state().content_revision != content_before) {
      sync_scene_objects_from_modeling();
    }
    sync_scene_selection_from_modeling();
    return {
        .ok = result.ok,
        .changed = result.changed,
        .message = result.message,
    };
  }

  ModelingCommandResult mirror_selection(ModelingMirrorAxis axis) {
    quader_editor_core::MirrorAxisRuntimeCommandSettings settings;
    settings.axis = to_runtime(axis);

    quader_editor_core::EditorRuntimeCommand command;
    command.command_id = quader_editor_core::EditorRuntimeCommandId::MirrorAxis;
    command.settings = settings;

    const std::uint64_t content_before = session_.state().content_revision;
    const quader_editor_core::EditorRuntimeCommandResult result =
        quader_editor_core::execute_runtime_command(session_, runtime_,
                                                    command);
    if (session_.state().content_revision != content_before) {
      sync_scene_objects_from_modeling();
    }
    sync_scene_selection_from_modeling();
    return {
        .ok = result.command.ok,
        .changed = result.command.changed,
        .message = result.command.message,
    };
  }

  ModelingCommandResult flip_selection(ModelingFlipOrientation orientation) {
    quader_editor_core::ViewFlipRuntimeCommandSettings settings;
    settings.orientation = to_runtime(orientation);

    quader_editor_core::EditorRuntimeCommand command;
    command.command_id = quader_editor_core::EditorRuntimeCommandId::FlipView;
    command.settings = settings;

    const std::uint64_t content_before = session_.state().content_revision;
    const quader_editor_core::EditorRuntimeCommandResult result =
        quader_editor_core::execute_runtime_command(session_, runtime_,
                                                    command);
    if (session_.state().content_revision != content_before) {
      sync_scene_objects_from_modeling();
    }
    sync_scene_selection_from_modeling();
    return {
        .ok = result.command.ok,
        .changed = result.command.changed,
        .message = result.command.message,
    };
  }

  ModelingCommandResult apply_active_material(std::uint32_t material_slot) {
    quader_editor_core::AssignMaterialOperationSettings settings;
    settings.material_slot = material_slot;

    quader_editor_core::EditorOperationCommand command;
    command.operation_id =
        quader_editor_core::EditorOperationId::AssignMaterial;
    command.phase = quader_editor_core::EditorOperationCommandPhase::Execute;
    command.settings = settings;

    const std::uint64_t content_before = session_.state().content_revision;
    const quader_editor_core::EditorOperationCommandResult result =
        quader_editor_core::execute_command(session_, command);
    if (session_.state().content_revision != content_before) {
      sync_scene_objects_from_modeling();
    }
    sync_scene_selection_from_modeling();
    history_bridge_.record_operation(command, result.command);
    return {
        .ok = result.command.ok,
        .changed = result.command.changed,
        .message = result.command.message,
    };
  }

  [[nodiscard]] ModelingToolStatus tool_status() const {
    return overlay_service_.tool_status(runtime_, modal_tool_owns_viewport());
  }

  [[nodiscard]] ModelingLiveStatus live_status() const {
    const quader_editor_core::PreviewTransactionStatus preview =
        session_.preview_transaction_status();
    if (preview.active &&
        preview.kind ==
            quader_editor_core::PreviewTransactionKind::MergeByDistance) {
      return {
          .kind = ModelingLiveStatusKind::MergeByDistanceMergedVertices,
          .count = preview.merge_by_distance_removed_vertices,
      };
    }
    return {};
  }

  [[nodiscard]] ModelingToolSettingsTarget tool_settings_target() const {
    const ModelingToolSettingsTarget preview_target =
        tool_settings_service_.active_preview_target(
            session_.preview_transaction_status());
    if (preview_target.active) {
      return preview_target;
    }
    return tool_settings_service_.active_runtime_tool_target(runtime_.status());
  }

  [[nodiscard]] ModelingInteractionSettings interaction_settings() const {
    return interaction_settings_service_.settings();
  }

  ModelingCommandResult
  set_interaction_settings(const ModelingInteractionSettings &settings) {
    const ModelingToolSettingsTarget active_target = tool_settings_target();
    const ModelingInteractionSettings previous =
        interaction_settings_service_.settings();
    const ModelingInteractionSettings normalized =
        interaction_settings_service_.normalize(settings);
    const bool cut_mode_changed = normalized.cut_mode != previous.cut_mode;
    const bool changed =
        interaction_settings_service_.set_normalized(normalized);
    if (cut_mode_changed) {
      return set_cut_mode(normalized.cut_mode);
    }
    if (changed && active_target.operation_preview_active) {
      ModelingCommandResult result = update_active_operation_preview_settings();
      result.changed = result.changed || changed;
      return result;
    }
    return {.ok = true, .changed = changed};
  }

  [[nodiscard]] bool modal_tool_owns_viewport() const {
    return modal_tool_runtime_.owns_viewport(
        from_runtime_viewport_tool(runtime_.active_tool()));
  }

  ModelingCommandResult apply_active_material(MaterialReference material) {
    if (!material.valid()) {
      return {
          .ok = false,
          .changed = false,
          .message = "The active material is unavailable.",
      };
    }

    const ModelingSelectionSummary summary = selection_summary();
    if (summary.mode != ModelingSelectionMode::Object &&
        summary.mode != ModelingSelectionMode::Face) {
      return {
          .ok = false,
          .changed = false,
          .message = "Apply Material works in object or face mode.",
      };
    }

    const ModelingSelectionExport selection = export_selection();
    if (summary.mode == ModelingSelectionMode::Object &&
        summary.selected_object_count == 0u) {
      return {.ok = false,
              .changed = false,
              .message =
                  "Select one or more meshes before applying material."};
    }
    if (summary.mode == ModelingSelectionMode::Face &&
        summary.selected_face_count == 0u) {
      return {.ok = false,
              .changed = false,
              .message =
                  "Select one or more faces before applying material."};
    }

    bool component_changed = false;
    const std::uint64_t content_before = session_.state().content_revision;
    if (summary.mode == ModelingSelectionMode::Object) {
      const quader_editor_core::CommandResult assigned =
          session_.assign_material_to_selection(0u);
      if (!assigned.ok) {
        return {.ok = false,
                .changed = false,
                .message = assigned.message};
      }
      for (const ModelingSelectionExportObject &object : selection.objects) {
        if (!object.selected || object.entity == kInvalidEntityId ||
            !scene_.exists(object.entity)) {
          continue;
        }
        const MaterialComponent desired{.material = material};
        component_changed =
            mesh_sync_service_.set_entity_material(scene_, object.entity,
                                                   desired) ||
            component_changed;
      }
    } else {
      for (const ModelingSelectionExportObject &object : selection.objects) {
        if (object.selected_faces.empty() ||
            object.entity == kInvalidEntityId ||
            !scene_.exists(object.entity)) {
          continue;
        }

        MaterialComponent desired;
        if (const MaterialComponent *existing =
                scene_.material(object.entity)) {
          desired = *existing;
        }

        std::uint32_t slot = 0u;
        if (desired.material != material) {
          const auto bound = std::min_element(
              desired.material_slots.begin(), desired.material_slots.end(),
              [&](const auto &left, const auto &right) {
                const bool left_matches = left.second == material;
                const bool right_matches = right.second == material;
                if (left_matches != right_matches) {
                  return left_matches;
                }
                return left.first < right.first;
              });
          if (bound != desired.material_slots.end() &&
              bound->second == material) {
            slot = bound->first;
          } else {
            slot = 1u;
            while (desired.material_slots.contains(slot)) {
              ++slot;
            }
            desired.material_slots.emplace(slot, material);
          }
        }

        for (const ModelingSelectionExportFace &face :
             object.selected_faces) {
          const quader_editor_core::CommandResult assigned =
              session_.assign_material_to_face(
                  object.modeling_object_id, face.id, slot);
          if (!assigned.ok) {
            return {.ok = false,
                    .changed = false,
                    .message = assigned.message};
          }
        }

        component_changed =
            mesh_sync_service_.set_entity_material(scene_, object.entity,
                                                   desired) ||
            component_changed;
      }
    }

    const bool geometry_changed =
        session_.state().content_revision != content_before;
    if (geometry_changed) {
      sync_scene_objects_from_modeling();
    }
    sync_scene_selection_from_modeling();
    return {
        .ok = true,
        .changed = geometry_changed || component_changed,
    };
  }

  [[nodiscard]] bool transform_drag_active() const {
    return runtime_.transform_drag_active() ||
           transform_drag_service_.scene_preview_active();
  }

  [[nodiscard]] bool can_cancel_active_tool_or_preview() const {
    return tool_settings_target().operation_preview_active ||
           runtime_.transform_drag_active() || has_transform_preview() ||
           transform_drag_service_.scene_preview_active() ||
           modal_tool_owns_viewport();
  }

  [[nodiscard]] bool can_cancel_active_preview_for_undo() const {
    return tool_settings_target().operation_preview_active ||
           runtime_.transform_drag_active() || has_transform_preview() ||
           transform_drag_service_.scene_preview_active();
  }

  ModelingCommandResult cancel_competing_state_for_native_tool_activation() {
    bool changed = false;
    const auto cancel = [&changed](ModelingCommandResult result) {
      changed = changed || result.changed;
      result.changed = changed;
      return result;
    };

    if (tool_settings_target().operation_preview_active) {
      const ModelingCommandResult result =
          cancel(cancel_active_operation_preview());
      if (!result.ok) {
        return result;
      }
    }
    if (transform_drag_active()) {
      const ModelingCommandResult result = cancel(cancel_transform_drag());
      if (!result.ok) {
        return result;
      }
    }
    if (has_transform_preview()) {
      const ModelingCommandResult result = cancel(cancel_transform_preview());
      if (!result.ok) {
        return result;
      }
    }
    if (modal_tool_owns_viewport()) {
      const ModelingCommandResult result = cancel(cancel_active_modal_tool());
      if (!result.ok) {
        return result;
      }
    }
    return {.ok = true, .changed = changed};
  }

  [[nodiscard]] ModelingActionQuery
  effective_action_query(ModelingActionQuery query) const {
    return context_action_service_.effective_action_query(
        query, modal_tool_owns_viewport());
  }

  [[nodiscard]] ModelingActionSet
  context_action_set(ModelingActionQuery query) const {
    return context_action_service_.action_set(selection_summary(), query,
                                              modal_tool_owns_viewport());
  }

  [[nodiscard]] std::vector<ModelingActionItem>
  context_actions(ModelingActionQuery query) const {
    return context_action_service_.actions(selection_summary(), query,
                                           modal_tool_owns_viewport());
  }

  [[nodiscard]] std::optional<ModelingActionItem>
  context_action(std::string_view action_id, ModelingActionQuery query) const {
    return context_action_service_.action(selection_summary(), action_id, query,
                                          modal_tool_owns_viewport());
  }

  void
  reset_settings_for_preview_action(ModelingActionPreviewKind preview_kind) {
    interaction_settings_service_.reset_for_preview_action(preview_kind);
  }

  void clear_inactive_operation_preview_metadata() {
    if (!session_.preview_transaction_status().active) {
      tool_settings_service_.clear_preview_metadata();
      scene_workspace_preview_.reset();
    }
  }

  ModelingCommandResult execute_runtime_operation_command(
      quader_editor_core::EditorOperationId runtime_operation,
      quader_editor_core::EditorOperationCommandPhase phase,
      quader_editor_core::EditorOperationSettings settings,
      bool preserve_duplicate_scene_identity = true) {
    const bool ordinary_duplicate =
        runtime_operation ==
            quader_editor_core::EditorOperationId::DuplicateMeshes &&
        phase == quader_editor_core::EditorOperationCommandPhase::Execute &&
        preserve_duplicate_scene_identity;
    const std::vector<ModelingObjectSceneIdentity> duplicate_scene_identities =
        ordinary_duplicate ? capture_selected_duplicate_scene_identities()
                           : std::vector<ModelingObjectSceneIdentity>{};
    QVec3 duplicate_world_offset;
    if (ordinary_duplicate) {
      if (const auto *duplicate_settings =
              std::get_if<quader_editor_core::DuplicateMeshesOperationSettings>(
                  &settings)) {
        duplicate_world_offset = quader_editor_core::
            duplicate_meshes_offset_from_settings(*duplicate_settings);
      }
    }
    const bool begins_preview =
        phase == quader_editor_core::EditorOperationCommandPhase::BeginPreview;
    const bool projects_active_preview =
        phase == quader_editor_core::EditorOperationCommandPhase::UpdatePreview ||
        phase == quader_editor_core::EditorOperationCommandPhase::CommitPreview ||
        phase == quader_editor_core::EditorOperationCommandPhase::CancelPreview;
    if (begins_preview && preview_checkpoint_hooks_.begin) {
      preview_checkpoint_hooks_.begin();
    } else if (projects_active_preview &&
               preview_checkpoint_hooks_.observe_before_projection) {
      preview_checkpoint_hooks_.observe_before_projection();
    }

    RuntimeOperationExecutor::OperationCommand operation_command;
    operation_command.operation = runtime_operation;
    operation_command.phase = phase;
    operation_command.settings = std::move(settings);
    const quader_editor_core::EditorOperationCommand command =
        RuntimeOperationExecutor::make_command(operation_command);
    if (ordinary_duplicate) {
      auto *duplicate_settings =
          std::get_if<quader_editor_core::DuplicateMeshesOperationSettings>(
              &operation_command.settings);
      if (duplicate_settings != nullptr) {
        duplicate_settings->explicit_offset = {{}, true};
      }
    }

    const quader_editor_core::PreviewTransactionStatus preview_before =
        session_.preview_transaction_status();
    const std::uint64_t content_before = session_.state().content_revision;
    const std::optional<HalfCutSceneIdentitySnapshot>
        half_cut_scene_identities = capture_half_cut_scene_identities(
            runtime_operation, phase);
    if (phase == quader_editor_core::EditorOperationCommandPhase::BeginPreview) {
      scene_workspace_preview_.reset();
    }
    std::optional<SceneWorkspaceOperationState> scene_workspace =
        scene_workspace_operation_state(runtime_operation, phase);
    std::optional<SceneWorkspacePreviewState> pending_scene_workspace_preview;
    if (scene_workspace.has_value() &&
        phase == quader_editor_core::EditorOperationCommandPhase::BeginPreview) {
      pending_scene_workspace_preview =
          preview_state_from_scene_workspace(runtime_operation,
                                             *scene_workspace);
    }
    quader_editor_core::EditorOperationCommandResult result =
        scene_workspace.has_value()
            ? execute_scene_workspace_operation_command(operation_command,
                                                        *scene_workspace)
            : runtime_operation_executor_.execute(session_, operation_command);
    if (scene_workspace.has_value() &&
        (!result.command.ok || !result.command.changed) &&
        phase == quader_editor_core::EditorOperationCommandPhase::Execute) {
      session_.set_state(std::move(scene_workspace->source_state));
    }
    if (phase == quader_editor_core::EditorOperationCommandPhase::BeginPreview &&
        result.command.ok && pending_scene_workspace_preview.has_value()) {
      scene_workspace_preview_ = std::move(pending_scene_workspace_preview);
    }
    if (phase ==
            quader_editor_core::EditorOperationCommandPhase::CommitPreview ||
        phase ==
            quader_editor_core::EditorOperationCommandPhase::CancelPreview ||
        (phase == quader_editor_core::EditorOperationCommandPhase::BeginPreview &&
         !result.command.ok)) {
      scene_workspace_preview_.reset();
    }

    history_bridge_.record_operation(command, result.command);

    ModelingCommandExecutor::SyncPlan sync_plan =
        command_executor_.operation_sync_plan(
            phase, preview_before, result, content_before,
            session_.state().content_revision);
    if (runtime_operation ==
            quader_editor_core::EditorOperationId::DuplicateMeshes &&
        phase == quader_editor_core::EditorOperationCommandPhase::Execute) {
      sync_plan.force_mesh_sync = false;
    }
    if (sync_plan.invalidate_overlay) {
      invalidate_selection_overlay_caches();
    }
    if (sync_plan.sync_meshes) {
      sync_scene_objects_from_modeling(sync_plan.force_mesh_sync);
      if (result.command.ok && half_cut_scene_identities.has_value()) {
        apply_half_cut_scene_identities(*half_cut_scene_identities);
      }
    }
    if (sync_plan.sync_selection) {
      sync_scene_selection_from_modeling();
    }
    if (ordinary_duplicate && result.command.ok && result.command.changed) {
      apply_scene_identities_to_selected_duplicates(
          duplicate_scene_identities);
      sync_scene_selection_from_modeling();
      if (quader::length_squared(duplicate_world_offset) > 0.0000001f) {
        const DocumentCommandResult placed =
            translate_scene_selection_by_world_delta(duplicate_world_offset,
                                                     "Place Duplicate");
        if (!placed.ok) {
          result.command = {
              .ok = false,
              .changed = true,
              .message = placed.message,
          };
        }
      }
    }
    clear_inactive_operation_preview_metadata();
    if ((begins_preview || projects_active_preview) &&
        preview_checkpoint_hooks_.record_after_projection) {
      preview_checkpoint_hooks_.record_after_projection();
    }

    const bool finished_preview =
        result.preview_finish.has_value() &&
        result.preview_finish->before.active;
    const bool failed_to_begin_preview =
        begins_preview && !session_.preview_transaction_status().active;
    if ((finished_preview || failed_to_begin_preview) &&
        preview_checkpoint_hooks_.finish) {
      const bool restored_by_cancel =
          phase ==
              quader_editor_core::EditorOperationCommandPhase::CancelPreview &&
          result.command.ok && result.preview_finish.has_value() &&
          result.preview_finish->restored;
      preview_checkpoint_hooks_.finish(restored_by_cancel);
    }
    return command_executor_.from_runtime(result.command);
  }

  quader_poly::Transform3 runtime_transform_from_matrix(
      const QMat4 &matrix) const {
    const glm::mat4 value = to_glm(matrix);
    return {
        .x_axis = from_glm(glm::vec3{value[0]}),
        .y_axis = from_glm(glm::vec3{value[1]}),
        .z_axis = from_glm(glm::vec3{value[2]}),
        .origin = from_glm(glm::vec3{value[3]}),
    };
  }

  QMat4 world_affine_from_transform(
      const quader_poly::Transform3 &transform, QVec3 pivot) const {
    const quader_poly::Transform3 pivoted =
        quader_editor_core::pivoted_transform(transform, to_runtime(pivot));
    glm::mat4 matrix{1.0f};
    matrix[0] = glm::vec4{to_glm(pivoted.x_axis), 0.0f};
    matrix[1] = glm::vec4{to_glm(pivoted.y_axis), 0.0f};
    matrix[2] = glm::vec4{to_glm(pivoted.z_axis), 0.0f};
    matrix[3] = glm::vec4{to_glm(pivoted.origin), 1.0f};
    return from_glm(matrix);
  }

  QMat4 world_translation_affine(QVec3 delta) const {
    return from_glm(glm::translate(glm::mat4{1.0f}, to_glm(delta)));
  }

  std::vector<quader_editor_core::ObjectLocalTransform>
  local_transforms_for_selected_objects(const QMat4 &world_affine,
                                        std::string &message) const {
    std::vector<quader_editor_core::ObjectLocalTransform> transforms;
    const glm::mat4 world_delta = to_glm(world_affine);
    for (const quader_editor_core::PolygonObject &object :
         session_.objects()) {
      if (!object.selected) {
        continue;
      }
      const std::optional<QMat4> world_from_object =
          modeling_object_world_transform(object.id);
      if (!world_from_object.has_value()) {
        message = "Could not resolve selected object scene transform.";
        return {};
      }
      const glm::mat4 object_world = to_glm(*world_from_object);
      const glm::mat4 local_delta =
          glm::inverse(object_world) * world_delta * object_world;
      transforms.push_back({
          .object_id = object.id,
          .transform = runtime_transform_from_matrix(from_glm(local_delta)),
      });
    }
    if (transforms.empty()) {
      message = "Select something before repeating the last transform.";
    }
    return transforms;
  }

  std::vector<quader_editor_core::ObjectLocalOffset>
  local_offsets_for_selected_objects(QVec3 world_delta,
                                     std::string &message) const {
    const std::vector<quader_editor_core::ObjectLocalTransform> transforms =
        local_transforms_for_selected_objects(
            world_translation_affine(world_delta), message);
    std::vector<quader_editor_core::ObjectLocalOffset> offsets;
    offsets.reserve(transforms.size());
    for (const quader_editor_core::ObjectLocalTransform &transform :
         transforms) {
      offsets.push_back({
          .object_id = transform.object_id,
          .offset = transform.transform.origin,
      });
    }
    return offsets;
  }

  quader_editor_core::CommandResult commit_repeat_transform_preview_result(
      const quader_editor_core::CommandResult &update_result) {
    if (!update_result.ok) {
      (void)session_.cancel_preview_transaction(
          quader_editor_core::PreviewTransactionKind::Transform);
      return update_result;
    }
    const quader_editor_core::PreviewTransactionStatus status =
        session_.preview_transaction_status();
    const bool changed =
        status.active &&
        status.kind == quader_editor_core::PreviewTransactionKind::Transform &&
        status.changed;
    (void)session_.commit_preview_transaction(
        quader_editor_core::PreviewTransactionKind::Transform);
    return {true, changed, {}};
  }

  quader_editor_core::CommandResult execute_repeat_transform_scene_aware(
      const quader_editor_core::RepeatTransformAction &action) {
    if (!session_.has_selection()) {
      return {false, false,
              "Select something before repeating the last transform."};
    }
    if (session_.selection_mode() ==
        quader_editor_core::SelectionMode::Object) {
      bool duplicated = false;
      if (action.duplicate_selected) {
        const ModelingCommandResult duplicate = begin_transform_preview(true);
        if (!duplicate.ok) {
          return {false, duplicate.changed, duplicate.message};
        }
        duplicated = duplicate.changed;
      }

      DocumentCommandResult transformed;
      if (action.tool ==
          quader_editor_core::ViewportInteractionTool::Move) {
        transformed = translate_scene_selection_by_world_delta(
            action.move_delta, "Repeat Move");
      } else {
        const QVec3 pivot = selection_center().value_or(QVec3{});
        if (action.tool ==
            quader_editor_core::ViewportInteractionTool::Rotate) {
          const quader_poly::Transform3 rotation =
              quader_editor_core::rotation_transform(
                  action.axis,
                  action.rotate_angle_degrees * std::acos(-1.0) / 180.0);
          transformed = transform_scene_selection_by_world_delta(
              world_affine_from_transform(rotation, pivot), "Repeat Rotate");
        } else if (action.tool ==
                   quader_editor_core::ViewportInteractionTool::Scale) {
          transformed = transform_scene_selection_by_world_delta(
              world_affine_from_transform(
                  quader_editor_core::scale_transform(action.scale), pivot),
              "Repeat Scale");
        } else {
          transformed = document_command_invalid_request_result(
              "Unsupported repeat transform tool.");
        }
      }

      if (!transformed.ok) {
        if (action.duplicate_selected) {
          (void)cancel_transform_preview();
        }
        return {false, duplicated, transformed.message};
      }
      if (action.duplicate_selected) {
        const ModelingCommandResult committed = commit_transform_preview();
        if (!committed.ok) {
          return {false, true, committed.message};
        }
        duplicated = duplicated || committed.changed;
      }
      return {true, duplicated || transformed.changed, {}};
    }

    quader_editor_core::CommandResult begin_result =
        session_.begin_transform_preview(action.duplicate_selected);
    if (!begin_result.ok) {
      return begin_result;
    }

    std::string message;
    quader_editor_core::CommandResult update_result;
    if (action.tool == quader_editor_core::ViewportInteractionTool::Move) {
      const std::vector<quader_editor_core::ObjectLocalOffset> offsets =
          local_offsets_for_selected_objects(action.move_delta, message);
      if (offsets.empty()) {
        (void)session_.cancel_preview_transaction(
            quader_editor_core::PreviewTransactionKind::Transform);
        return {false, false, message};
      }
      if (action.operation ==
          quader_editor_core::TransformDragOperation::Extrude) {
        update_result = session_.update_transform_extrude_preview(offsets);
      } else {
        update_result = session_.update_transform_translate_preview(offsets,
                                                                    false);
      }
      return commit_repeat_transform_preview_result(update_result);
    }

    const QVec3 pivot = selection_center().value_or(QVec3{});
    if (action.tool == quader_editor_core::ViewportInteractionTool::Rotate) {
      const quader_poly::Transform3 world_transform =
          quader_editor_core::rotation_transform(
              action.axis,
              action.rotate_angle_degrees * std::acos(-1.0) / 180.0);
      const std::vector<quader_editor_core::ObjectLocalTransform> transforms =
          local_transforms_for_selected_objects(
              world_affine_from_transform(world_transform, pivot), message);
      if (transforms.empty()) {
        (void)session_.cancel_preview_transaction(
            quader_editor_core::PreviewTransactionKind::Transform);
        return {false, false, message};
      }
      if (action.operation ==
          quader_editor_core::TransformDragOperation::Extrude) {
        update_result = session_.update_transform_extrude_preview(transforms);
      } else {
        update_result = session_.update_transform_matrix_preview(transforms,
                                                                 false, false);
      }
      return commit_repeat_transform_preview_result(update_result);
    }

    if (action.tool == quader_editor_core::ViewportInteractionTool::Scale) {
      const quader_poly::Transform3 world_transform =
          quader_editor_core::scale_transform(action.scale);
      const std::vector<quader_editor_core::ObjectLocalTransform> transforms =
          local_transforms_for_selected_objects(
              world_affine_from_transform(world_transform, pivot), message);
      if (transforms.empty()) {
        (void)session_.cancel_preview_transaction(
            quader_editor_core::PreviewTransactionKind::Transform);
        return {false, false, message};
      }
      if (action.operation ==
          quader_editor_core::TransformDragOperation::Inset) {
        update_result = session_.update_transform_inset_preview(transforms);
      } else {
        update_result = session_.update_transform_matrix_preview(transforms,
                                                                 false, false);
      }
      return commit_repeat_transform_preview_result(update_result);
    }

    (void)session_.cancel_preview_transaction(
        quader_editor_core::PreviewTransactionKind::Transform);
    return {false, false, "Unsupported repeat transform tool."};
  }

  quader_editor_core::CommandResult execute_repeat_last_action_scene_aware() {
    quader_editor_core::RepeatLastActionState &repeat =
        history_bridge_.repeat_last_action();
    if (repeat.last_action_kind ==
        quader_editor_core::RepeatableActionKind::Transform) {
      if (!repeat.last_transform.has_value()) {
        return {false, false, "No repeatable transform is available."};
      }
      return execute_repeat_transform_scene_aware(*repeat.last_transform);
    }
    if (repeat.last_action_kind ==
            quader_editor_core::RepeatableActionKind::Operation &&
        repeat.last_operation.has_value()) {
      quader_editor_core::EditorOperationCommand command =
          *repeat.last_operation;
      command.phase = quader_editor_core::EditorOperationCommandPhase::Execute;
      const ModelingCommandResult result = execute_runtime_operation_command(
          command.operation_id, command.phase, command.settings);
      return {
          .ok = result.ok,
          .changed = result.changed,
          .message = result.message,
      };
    }
    return {false, false, "No repeatable action is available."};
  }

  /**
   * Captures normalized scene-space inputs for a runtime operation.
   */
  struct SceneWorkspaceOperationState {
    quader_editor_core::SessionState source_state;
    quader_editor_core::SessionState working_state;
    int survivor_id = -1;
    std::unordered_map<int, QMat4> world_from_object_by_id;
  };

  /**
   * Tracks the active scene-space preview participant mapping.
   */
  struct SceneWorkspacePreviewState {
    quader_editor_core::EditorOperationId operation =
        quader_editor_core::EditorOperationId::Bridge;
    int survivor_id = -1;
    std::unordered_map<int, QMat4> world_from_object_by_id;
  };

  std::optional<quader_editor_core::BridgeOperationSettings>
  bridge_operation_settings_from(
      const quader_editor_core::EditorOperationSettings &settings) const {
    const auto *bridge =
        std::get_if<quader_editor_core::BridgeOperationSettings>(&settings);
    if (bridge == nullptr) {
      return std::nullopt;
    }
    return quader_editor_core::normalize_bridge_operation_settings(*bridge);
  }

  std::vector<int> selected_object_indices(
      const quader_editor_core::SessionState &state) const {
    std::vector<int> indices;
    for (int index = 0; index < static_cast<int>(state.objects.size());
         ++index) {
      if (state.objects[index].selected) {
        indices.push_back(index);
      }
    }
    return indices;
  }

  [[nodiscard]] bool transform_flips_handedness(const QMat4 &transform) const {
    return glm::determinant(glm::mat3{to_glm(transform)}) < 0.0f;
  }

  void reverse_document_winding(quader_poly::Document &document) const {
    for (quader_poly::Face &face : document.faces) {
      std::ranges::reverse(face.vertices);
      if (face.uvs.size() == face.vertices.size()) {
        std::ranges::reverse(face.uvs);
      } else {
        face.uvs.clear();
      }
    }
  }

  std::vector<int> scene_workspace_participant_indices(
      quader_editor_core::EditorOperationId runtime_operation,
      const quader_editor_core::SessionState &state) const {
    if (runtime_operation ==
        quader_editor_core::EditorOperationId::CombineMeshes) {
      return state.selection_mode == quader_editor_core::SelectionMode::Object
                 ? selected_object_indices(state)
                 : std::vector<int>{};
    }

    if (runtime_operation != quader_editor_core::EditorOperationId::Bridge) {
      return {};
    }
    if (state.selection_mode != quader_editor_core::SelectionMode::Edge &&
        state.selection_mode != quader_editor_core::SelectionMode::Face) {
      return {};
    }

    std::vector<int> indices;
    for (int index = 0; index < static_cast<int>(state.objects.size());
         ++index) {
      const quader_editor_core::PolygonObject &object = state.objects[index];
      const bool has_bridge_selection =
          state.selection_mode == quader_editor_core::SelectionMode::Face
              ? !object.selection.faces.empty()
              : !object.selection.edges.empty();
      if (object.selected && has_bridge_selection) {
        indices.push_back(index);
      }
    }
    return indices.size() == 2U ? indices : std::vector<int>{};
  }

  int scene_workspace_survivor_index(
      const quader_editor_core::SessionState &state,
      const std::vector<int> &participant_indices) const {
    int survivor_index = participant_indices.empty()
                             ? -1
                             : participant_indices.front();
    for (const int index : participant_indices) {
      if (index >= 0 && index < static_cast<int>(state.objects.size()) &&
          state.objects[index].id == state.active_object_id) {
        survivor_index = index;
        break;
      }
    }
    return survivor_index;
  }

  [[nodiscard]] const std::unordered_map<int, QMat4> *
  captured_scene_workspace_transforms(
      quader_editor_core::EditorOperationId runtime_operation) const {
    if (!scene_workspace_preview_.has_value() ||
        scene_workspace_preview_->operation != runtime_operation) {
      return nullptr;
    }
    return &scene_workspace_preview_->world_from_object_by_id;
  }

  [[nodiscard]] std::optional<QMat4> scene_workspace_object_world_transform(
      int modeling_object_id,
      const std::unordered_map<int, QMat4> *captured_transforms) const {
    if (captured_transforms != nullptr) {
      const auto found = captured_transforms->find(modeling_object_id);
      if (found != captured_transforms->end()) {
        return found->second;
      }
    }
    return modeling_object_world_transform(modeling_object_id);
  }

  [[nodiscard]] std::optional<SceneWorkspacePreviewState>
  preview_state_from_scene_workspace(
      quader_editor_core::EditorOperationId runtime_operation,
      const SceneWorkspaceOperationState &scene_workspace) const {
    if (scene_workspace.world_from_object_by_id.empty()) {
      return std::nullopt;
    }
    return SceneWorkspacePreviewState{
        .operation = runtime_operation,
        .survivor_id = scene_workspace.survivor_id,
        .world_from_object_by_id = scene_workspace.world_from_object_by_id,
    };
  }

  std::optional<SceneWorkspaceOperationState>
  scene_workspace_operation_state(
      quader_editor_core::EditorOperationId runtime_operation,
      quader_editor_core::EditorOperationCommandPhase phase) {
    quader_editor_core::SessionState source_state;
    const std::unordered_map<int, QMat4> *captured_transforms = nullptr;
    switch (phase) {
    case quader_editor_core::EditorOperationCommandPhase::Execute:
    case quader_editor_core::EditorOperationCommandPhase::BeginPreview:
      source_state = session_.state();
      break;
    case quader_editor_core::EditorOperationCommandPhase::UpdatePreview: {
      if (runtime_operation != quader_editor_core::EditorOperationId::Bridge) {
        return std::nullopt;
      }
      const std::optional<quader_editor_core::SessionState> preview_base =
          session_.preview_transaction_history_state(
              quader_editor_core::PreviewTransactionKind::Bridge);
      if (!preview_base.has_value()) {
        return std::nullopt;
      }
      source_state = *preview_base;
      captured_transforms =
          captured_scene_workspace_transforms(runtime_operation);
      break;
    }
    case quader_editor_core::EditorOperationCommandPhase::CommitPreview:
    case quader_editor_core::EditorOperationCommandPhase::CancelPreview:
      return std::nullopt;
    }

    const std::vector<int> participant_indices =
        scene_workspace_participant_indices(runtime_operation, source_state);
    if (participant_indices.size() < 2U) {
      return std::nullopt;
    }
    const int survivor_index =
        scene_workspace_survivor_index(source_state, participant_indices);
    if (survivor_index < 0 ||
        survivor_index >= static_cast<int>(source_state.objects.size())) {
      return std::nullopt;
    }
    const int survivor_id = source_state.objects[survivor_index].id;
    const std::optional<QMat4> survivor_world =
        scene_workspace_object_world_transform(survivor_id,
                                               captured_transforms);
    if (!survivor_world.has_value()) {
      return std::nullopt;
    }

    quader_editor_core::SessionState working_state = source_state;
    std::unordered_map<int, QMat4> world_from_object_by_id;
    const glm::mat4 survivor_from_world = glm::inverse(to_glm(*survivor_world));
    bool changed = false;
    for (const int index : participant_indices) {
      if (index < 0 || index >= static_cast<int>(working_state.objects.size())) {
        continue;
      }
      quader_editor_core::PolygonObject &object =
          working_state.objects[index];
      const std::optional<QMat4> object_world =
          scene_workspace_object_world_transform(object.id,
                                                 captured_transforms);
      if (!object_world.has_value()) {
        return std::nullopt;
      }
      world_from_object_by_id.insert_or_assign(object.id, *object_world);
      if (object.id == survivor_id) {
        continue;
      }
      const QMat4 survivor_from_object =
          from_glm(survivor_from_world * to_glm(*object_world));
      for (quader_poly::Vertex &vertex : object.document.edit().vertices) {
        vertex.position =
            transform_point(survivor_from_object, vertex.position);
      }
      if (transform_flips_handedness(survivor_from_object)) {
        reverse_document_winding(object.document);
      }
      changed = true;
    }

    return changed ? std::optional<SceneWorkspaceOperationState>{
                          SceneWorkspaceOperationState{
                              .source_state = std::move(source_state),
                              .working_state = std::move(working_state),
                              .survivor_id = survivor_id,
                              .world_from_object_by_id =
                                  std::move(world_from_object_by_id),
                          }}
                   : std::nullopt;
  }

  quader_editor_core::EditorOperationCommandResult
  execute_scene_workspace_operation_command(
      const RuntimeOperationExecutor::OperationCommand &operation_command,
      const SceneWorkspaceOperationState &scene_workspace) {
    if (operation_command.operation ==
            quader_editor_core::EditorOperationId::Bridge &&
        operation_command.phase ==
            quader_editor_core::EditorOperationCommandPhase::BeginPreview) {
      const std::optional<quader_editor_core::BridgeOperationSettings>
          bridge_settings =
              bridge_operation_settings_from(operation_command.settings);
      if (!bridge_settings.has_value()) {
        return runtime_operation_executor_.execute(session_, operation_command);
      }
      return {
          session_.begin_bridge_preview_from_working_state(
              scene_workspace.source_state, scene_workspace.working_state,
              bridge_settings->steps),
          std::nullopt,
      };
    }

    if (operation_command.operation ==
            quader_editor_core::EditorOperationId::Bridge &&
        operation_command.phase ==
            quader_editor_core::EditorOperationCommandPhase::UpdatePreview) {
      const std::optional<quader_editor_core::BridgeOperationSettings>
          bridge_settings =
              bridge_operation_settings_from(operation_command.settings);
      if (!bridge_settings.has_value()) {
        return runtime_operation_executor_.execute(session_, operation_command);
      }
      return {
          session_.update_bridge_preview_from_working_state(
              scene_workspace.source_state, scene_workspace.working_state,
              bridge_settings->steps),
          std::nullopt,
      };
    }

    session_.set_state(scene_workspace.working_state);
    return RuntimeOperationExecutor::execute_with_history_state(
        session_, operation_command, scene_workspace.source_state);
  }

  ModelingCommandResult begin_operation_preview(
      const ModelingActionItem &action,
      const quader_editor_core::EditorOperation &operation) {
    if (!operation.supports_preview()) {
      return {.ok = false,
              .message = "Action does not support a live settings preview."};
    }

    reset_settings_for_preview_action(action.preview_kind);
    ModelingCommandResult result = execute_runtime_operation_command(
        operation.operation_id(),
        quader_editor_core::EditorOperationCommandPhase::BeginPreview,
        settings_for_operation(operation.operation_id(),
                               interaction_settings_service_.settings()));
    if (result.ok) {
      tool_settings_service_.set_preview_metadata(
          action.action_id, action.operation_id,
          action.label.empty()
              ? fallback_tool_settings_label(
                    tool_settings_target_kind_for_preview(
                        session_.preview_transaction_status().kind))
              : action.label);
    }
    return result;
  }

  ModelingCommandResult update_active_operation_preview_settings() {
    const quader_editor_core::PreviewTransactionStatus preview =
        session_.preview_transaction_status();
    if (!preview.active) {
      return {.ok = false, .message = "No active operation preview."};
    }
    const std::optional<quader_editor_core::EditorOperationId> operation =
        operation_for_preview_kind(preview.kind);
    if (!operation.has_value()) {
      return {.ok = false,
              .message =
                  "Active preview does not have editable operation settings."};
    }
    return execute_runtime_operation_command(
        *operation,
        quader_editor_core::EditorOperationCommandPhase::UpdatePreview,
        settings_for_operation(*operation,
                               interaction_settings_service_.settings()));
  }

  ModelingCommandResult finish_active_operation_preview(bool commit) {
    const quader_editor_core::PreviewTransactionStatus preview =
        session_.preview_transaction_status();
    if (!preview.active) {
      return {.ok = false, .message = "No active operation preview."};
    }
    const std::optional<quader_editor_core::EditorOperationId> operation =
        operation_for_preview_kind(preview.kind);
    if (!operation.has_value()) {
      return {.ok = false,
              .message =
                  "Active preview does not belong to a settings operation."};
    }
    return execute_runtime_operation_command(
        *operation,
        commit ? quader_editor_core::EditorOperationCommandPhase::CommitPreview
               : quader_editor_core::EditorOperationCommandPhase::CancelPreview,
        settings_for_operation(*operation,
                               interaction_settings_service_.settings()));
  }

  [[nodiscard]] std::optional<quader_editor_core::PreviewTransactionKind>
  editable_operation_preview_kind() const {
    const quader_editor_core::PreviewTransactionStatus preview =
        session_.preview_transaction_status();
    if (!preview.active ||
        !operation_for_preview_kind(preview.kind).has_value()) {
      return std::nullopt;
    }
    if ((preview.kind == quader_editor_core::PreviewTransactionKind::Thicken ||
         preview.kind == quader_editor_core::PreviewTransactionKind::HalfCut) &&
        session_.selection_mode() ==
            quader_editor_core::SelectionMode::Object) {
      return std::nullopt;
    }
    return preview.kind;
  }

  [[nodiscard]] bool
  restore_active_operation_preview_base_for_selection_edit() {
    const std::optional<quader_editor_core::PreviewTransactionKind>
        preview_kind = editable_operation_preview_kind();
    if (!preview_kind.has_value()) {
      return false;
    }
    const bool restored =
        session_.restore_preview_transaction_base_for_edit(*preview_kind);
    if (restored) {
      selection_service_.invalidate_picking_cache();
    }
    return restored;
  }

  void refresh_active_operation_preview_after_selection_edit(
      bool restored_preview_base, bool selection_changed) {
    if (!restored_preview_base) {
      return;
    }
    const std::optional<quader_editor_core::PreviewTransactionKind>
        preview_kind = editable_operation_preview_kind();
    if (!preview_kind.has_value()) {
      sync_scene_selection_from_modeling();
      return;
    }
    if (selection_changed) {
      (void)session_.capture_preview_transaction_base_after_edit(*preview_kind);
    }
    (void)update_active_operation_preview_settings();
  }

  ModelingCommandResult execute_context_action(std::string_view action_id) {
    if (action_id == std::string_view("delete_selection")) {
      return {.ok = false,
              .message = "Delete selection must use the command route."};
    }

    const ModelingContextActionResolution action =
        context_action_service_.resolve_executable_action(
            selection_summary(), action_id, modal_tool_owns_viewport());
    if (!action.ok) {
      return {.ok = false, .message = action.message};
    }
    for (const ModelingInstantOperationCatalogEntry &entry :
         modeling_instant_operation_catalog()) {
      if (entry.id == action.action.action_id &&
          (action.action.operation_id.empty() ||
           entry.operation_id == action.action.operation_id) &&
          action.action.preview_kind == ModelingActionPreviewKind::None) {
        return execute_instant_operation(entry.operation);
      }
    }
    if (action.action.kind != ModelingActionKind::Operation ||
        action.action.operation_id.empty()) {
      return {.ok = false,
              .message = "Action is not an executable modeling operation."};
    }

    const quader_editor_core::EditorOperation *operation =
        quader_editor_core::editor_operation_for_id(action.action.operation_id);
    if (operation == nullptr) {
      return {.ok = false,
              .message = "Action references an unknown modeling operation."};
    }

    if (action.action.preview_kind != ModelingActionPreviewKind::None) {
      return begin_operation_preview(action.action, *operation);
    }

    quader_editor_core::EditorOperationSettings settings =
        settings_for_operation(operation->operation_id(),
                               interaction_settings_service_.settings());
    if (operation->operation_id() ==
        quader_editor_core::EditorOperationId::Bridge) {
      settings = quader_editor_core::BridgeOperationSettings{.steps = 1};
    }
    return execute_runtime_operation(operation->operation_id(), settings);
  }

  ModelingCommandResult commit_active_operation_preview() {
    return finish_active_operation_preview(true);
  }

  ModelingCommandResult cancel_active_operation_preview() {
    return finish_active_operation_preview(false);
  }

  [[nodiscard]] std::vector<ModelingInstantOperationDescriptor>
  instant_operations() const {
    const ModelingSelectionSummary summary = selection_summary();
    std::vector<ModelingInstantOperationDescriptor> operations;
    for (const ModelingInstantOperationCatalogEntry &entry :
         modeling_instant_operation_catalog()) {
      operations.push_back({
          .operation = entry.operation,
          .id = std::string(entry.id),
          .label = std::string(entry.live_label),
          .enabled = modeling_instant_operation_enabled(entry, summary),
          .rebuilds_mesh_on_success = entry.rebuilds_mesh_on_success,
      });
    }
    return operations;
  }

  ModelingCommandResult execute_runtime_operation(
      quader_editor_core::EditorOperationId runtime_operation,
      quader_editor_core::EditorOperationSettings settings,
      bool preserve_duplicate_scene_identity = true) {
    return execute_runtime_operation_command(
        runtime_operation,
        quader_editor_core::EditorOperationCommandPhase::Execute,
        std::move(settings), preserve_duplicate_scene_identity);
  }

  ModelingCommandResult
  execute_instant_operation(ModelingInstantOperationId operation,
                            bool preserve_duplicate_scene_identity = true) {
    const std::optional<RuntimeOperationExecutor::OperationCommand> command =
        runtime_operation_executor_.operation_for_instant_command(
            operation, interaction_settings_service_.settings());
    if (!command.has_value()) {
      return {.ok = false,
              .message =
                  "Instant operation is not mapped to a runtime operation."};
    }

    ModelingCommandResult result = execute_runtime_operation(
        command->operation, command->settings,
        preserve_duplicate_scene_identity);
    return result;
  }

  ModelingCommandResult begin_transform_preview(bool duplicate_selected) {
    const std::vector<ModelingObjectSceneIdentity> duplicate_scene_identities =
        duplicate_selected ? capture_selected_duplicate_scene_identities()
                           : std::vector<ModelingObjectSceneIdentity>{};
    const quader_editor_core::CommandResult result =
        session_.begin_transform_preview(duplicate_selected);
    if (result.ok) {
      invalidate_selection_overlay_caches();
    }
    if (result.changed) {
      sync_scene_objects_from_modeling(true);
      sync_scene_selection_from_modeling();
      if (duplicate_selected) {
        apply_scene_identities_to_selected_duplicates(
            duplicate_scene_identities);
      }
    }
    return {
        .ok = result.ok,
        .changed = result.changed,
        .message = result.message,
    };
  }

  ModelingCommandResult update_transform_translate_preview(QVec3 delta) {
    return update_transform_preview_result(
        session_.update_transform_translate_preview(to_runtime(delta)));
  }

  ModelingCommandResult
  update_transform_matrix_preview(const ModelingTransform3 &transform,
                                  QVec3 pivot) {
    return update_transform_preview_result(
        session_.update_transform_matrix_preview(to_runtime(transform),
                                                 to_runtime(pivot)));
  }

  ModelingCommandResult
  update_transform_extrude_preview(QVec3 offset, float closed_edge_ledge_size) {
    return update_transform_preview_result(
        session_.update_transform_extrude_preview(to_runtime(offset),
                                                  closed_edge_ledge_size));
  }

  ModelingCommandResult
  update_transform_inset_preview(const ModelingTransform3 &transform,
                                 QVec3 pivot) {
    return update_transform_preview_result(
        session_.update_transform_inset_preview(to_runtime(transform),
                                                to_runtime(pivot)));
  }

  ModelingCommandResult commit_transform_preview() {
    const quader_editor_core::PreviewTransactionFinishResult finish =
        session_.commit_preview_transaction(
            quader_editor_core::PreviewTransactionKind::Transform);
    if (finish.before.active) {
      invalidate_selection_overlay_caches();
    }
    sync_scene_objects_from_modeling(finish.before.active);
    sync_scene_selection_from_modeling();
    return transform_preview_service_.finish_result(finish, false);
  }

  ModelingCommandResult cancel_transform_preview() {
    const quader_editor_core::PreviewTransactionFinishResult finish =
        session_.cancel_preview_transaction(
            quader_editor_core::PreviewTransactionKind::Transform);
    if (finish.before.active) {
      invalidate_selection_overlay_caches();
    }
    sync_scene_objects_from_modeling(finish.before.active);
    sync_scene_selection_from_modeling();
    return transform_preview_service_.finish_result(finish, true);
  }

  ModelingCommandResult
  record_repeatable_transform(const ModelingRepeatTransform &transform) {
    if (!transform.active) {
      return {.ok = false, .message = "No active transform to record."};
    }

    quader_editor_core::TransformDragState drag;
    drag.active = true;
    drag.tool = to_runtime(transform.tool);
    drag.operation = to_runtime(transform.operation);
    drag.axis = to_runtime(transform.axis);
    drag.move.active = transform.tool == ModelingTransformTool::Move;
    drag.move.axis = drag.axis;
    drag.move.preview_delta = to_runtime(transform.move_delta);
    drag.rotate.active = transform.tool == ModelingTransformTool::Rotate;
    drag.rotate.axis = drag.axis;
    drag.rotate.preview_angle_degrees = transform.rotate_angle_degrees;
    drag.scale.active = transform.tool == ModelingTransformTool::Scale;
    drag.scale.axis = drag.axis;
    drag.scale.preview_scale = to_runtime(transform.scale);

    history_bridge_.record_transform(session_, drag,
                                     transform.duplicate_selected);
    return {.ok = true};
  }

  ModelingTransformDragPreview
  begin_transform_drag(const ModelingTransformDragBeginRequest &request,
                       DocumentCommandService *document_commands = nullptr,
                       EditorHistoryService *history = nullptr) {
    return transform_drag_service_.begin_transform_drag(
        transform_drag_context(document_commands, history), request);
  }

  ModelingTransformDragPreview
  update_transform_drag(const ModelingTransformDragUpdateRequest &request) {
    return transform_drag_service_.update_transform_drag(
        transform_drag_context(), request);
  }

  ModelingTransformDragPreview
  commit_transform_drag(EditorHistoryService *history = nullptr) {
    return transform_drag_service_.commit_transform_drag(
        transform_drag_context(), history);
  }

  ModelingCommandResult cancel_transform_drag() {
    return transform_drag_service_.cancel_transform_drag(
        transform_drag_context());
  }

  ModelingSelectionSummary
  pick_select(const ModelingPickSelectRequest &request) {
    const bool restored_preview_base =
        restore_active_operation_preview_base_for_selection_edit();
    ModelingPickSelectRequest effective_request = request;
    if (restored_preview_base) {
      effective_request.preserve_selection_on_miss = true;
    }
    const ModelingSelectionApplyResult selection =
        selection_service_.apply_pick_selection(
            session_, scene_, object_registry_, effective_request);
    if (restored_preview_base) {
      refresh_active_operation_preview_after_selection_edit(
          restored_preview_base, selection.changed);
      return selection_summary();
    }
    if (selection.applied) {
      sync_scene_selection_from_modeling();
    }
    return selection_summary();
  }

  ModelingSelectionSummary box_select(const ModelingBoxSelectRequest &request) {
    const bool restored_preview_base =
        restore_active_operation_preview_base_for_selection_edit();
    return box_select_from_baseline(
        request, selection_service_.rectangle_selection_state(session_),
        restored_preview_base);
  }

  ModelingSelectionSummary box_select(
      const ModelingBoxSelectRequest &request,
      const quader_editor_core::RectangleSelectionState &baseline_selection) {
    const bool restored_preview_base =
        restore_active_operation_preview_base_for_selection_edit();
    const quader_editor_core::RectangleSelectionState effective_baseline =
        restored_preview_base
            ? selection_service_.rectangle_selection_state(session_)
            : baseline_selection;
    return box_select_from_baseline(request, effective_baseline,
                                    restored_preview_base);
  }

  ModelingSelectionSummary box_select_from_baseline(
      const ModelingBoxSelectRequest &request,
      const quader_editor_core::RectangleSelectionState &baseline_selection,
      bool restored_preview_base) {
    const ModelingSelectionApplyResult selection =
        selection_service_.apply_rectangle_selection(
            session_, scene_, object_registry_, request, baseline_selection);
    if (restored_preview_base) {
      refresh_active_operation_preview_after_selection_edit(
          restored_preview_base, selection.changed);
      return selection_summary();
    }
    if (selection.changed) {
      sync_scene_selection_from_modeling();
      invalidate_selection_overlay_caches();
    }
    return selection_summary();
  }

  ModelingCommandResult
  update_selection_hover(const ModelingHoverRequest &request) {
    const quader_editor_core::SelectionHoverUpdateResult hover =
        selection_service_.selection_hover(
            session_, scene_, object_registry_, runtime_.hover(), request);
    runtime_.apply_hover_update(hover);
    return {
        .ok = true,
        .changed = hover.changed,
    };
  }

  [[nodiscard]] ModelingHoverSummary selection_hover_summary() const {
    return from_runtime_hover_summary(session_, runtime_.hover(), false);
  }

  [[nodiscard]] ModelingHoverSummary
  preview_selection_hover(const ModelingHoverRequest &request) const {
    const quader_editor_core::SelectionHoverUpdateResult hover =
        selection_service_.selection_hover(
            session_, scene_, object_registry_, runtime_.hover(), request);
    return from_runtime_hover_summary(session_, hover.hover, hover.changed);
  }

  ModelingCommandResult clear_selection_hover() {
    quader_editor_core::EditorRuntimeCommand command;
    command.command_id = quader_editor_core::EditorRuntimeCommandId::ClearHover;
    const quader_editor_core::EditorRuntimeCommandResult result =
        quader_editor_core::execute_runtime_command(session_, runtime_,
                                                    command);
    return {
        .ok = result.command.ok,
        .changed = result.command.changed,
        .message = result.command.message,
    };
  }

  ModelingCommandResult execute_runtime_command(
      const quader_editor_core::EditorRuntimeCommand &command,
      bool may_change_content) {
    const std::uint64_t content_before = session_.state().content_revision;
    const quader_editor_core::EditorRuntimeCommandResult result =
        quader_editor_core::execute_runtime_command(session_, runtime_,
                                                    command);
    const ModelingCommandExecutor::SyncPlan sync_plan =
        command_executor_.runtime_sync_plan(may_change_content, content_before,
                                            session_.state().content_revision,
                                            result.command);
    if (sync_plan.sync_meshes) {
      sync_scene_objects_from_modeling(sync_plan.force_mesh_sync);
    }
    if (sync_plan.sync_selection) {
      sync_scene_selection_from_modeling();
    }
    if (sync_plan.invalidate_overlay) {
      invalidate_selection_overlay_caches();
    }
    return command_executor_.from_runtime(result.command);
  }

  ModelingCommandResult
  set_active_runtime_tool(quader_editor_core::ViewportInteractionTool tool) {
    const bool preserve_group_selection =
        app_selection_mode_ == ModelingSelectionMode::Group;
    const bool preserve_scene_transform_selection =
        !preserve_group_selection && !session_.has_selection() &&
        has_scene_transform_selection();
    const bool preserve_scene_selection =
        preserve_group_selection || preserve_scene_transform_selection;
    const std::vector<EntityId> selected_before =
        preserve_scene_selection ? scene_.selected_entities()
                                 : std::vector<EntityId>{};
    const EntityId active_before = preserve_scene_selection
                                       ? scene_.primary_selection()
                                       : kInvalidEntityId;

    ModelingCommandResult result =
        execute_runtime_command(RuntimeToolCommandService::set_active_tool(
                                    from_runtime_viewport_tool(tool)),
                                false);
    if (preserve_scene_selection) {
      restore_scene_selection(selected_before, active_before);
      if (preserve_group_selection) {
        app_selection_mode_ = ModelingSelectionMode::Group;
      }
      invalidate_selection_overlay_caches();
    }
    return result;
  }

  ModelingCommandResult activate_viewport_tool(ModelingViewportTool tool) {
    bool selection_mode_changed = false;
    std::string selection_mode_message;
    if (tool == ModelingViewportTool::Knife &&
        app_selection_mode_ != ModelingSelectionMode::Edge) {
      const ModelingCommandResult mode_result =
          set_selection_mode(ModelingSelectionMode::Edge);
      if (!mode_result.ok) {
        return mode_result;
      }
      selection_mode_changed = mode_result.changed;
      selection_mode_message = mode_result.message;
    }
    const bool preserve_group_selection =
        app_selection_mode_ == ModelingSelectionMode::Group;
    const bool preserve_scene_transform_selection =
        !preserve_group_selection && !session_.has_selection() &&
        has_scene_transform_selection();
    const bool preserve_scene_selection =
        preserve_group_selection || preserve_scene_transform_selection;
    const std::vector<EntityId> selected_before =
        preserve_scene_selection ? scene_.selected_entities()
                                 : std::vector<EntityId>{};
    const EntityId active_before = preserve_scene_selection
                                       ? scene_.primary_selection()
                                       : kInvalidEntityId;
    const auto finish = [&](ModelingCommandResult result) {
      if (preserve_scene_selection) {
        restore_scene_selection(selected_before, active_before);
        if (preserve_group_selection) {
          app_selection_mode_ = ModelingSelectionMode::Group;
        }
        invalidate_selection_overlay_caches();
      }
      result.changed = result.changed || selection_mode_changed;
      if (result.message.empty()) {
        result.message = selection_mode_message;
      }
      return result;
    };
    if (tool == ModelingViewportTool::Cut ||
        tool == ModelingViewportTool::Mirror) {
      promote_drawn_plane_tool_object_selection();
    }
    if (tool == ModelingViewportTool::Cut) {
      ModelingCommandResult activated = execute_runtime_command(
          RuntimeToolCommandService::activate_cut(), false);
      if (activated.ok) {
        const ModelingCommandResult mode_result =
            set_cut_mode(interaction_settings_service_.settings().cut_mode);
        activated.ok = mode_result.ok;
        activated.changed = activated.changed || mode_result.changed;
        if (!mode_result.message.empty()) {
          activated.message = mode_result.message;
        }
      }
      return finish(activated);
    }
    if (tool == ModelingViewportTool::Mirror) {
      return finish(execute_runtime_command(
          RuntimeToolCommandService::activate_mirror(), false));
    }
    return finish(set_active_runtime_tool(to_runtime(tool)));
  }

  ModelingCommandResult cancel_active_modal_tool() {
    switch (runtime_.active_tool()) {
    case quader_editor_core::ViewportInteractionTool::Box:
      return cancel_box();
    case quader_editor_core::ViewportInteractionTool::InsertEdgeLoop:
      (void)execute_runtime_command(
          RuntimeToolCommandService::cancel_insert_edge_loop(), false);
      return set_active_runtime_tool(
          quader_editor_core::ViewportInteractionTool::Select);
    case quader_editor_core::ViewportInteractionTool::Knife:
      runtime_.reset_knife_state();
      invalidate_selection_overlay_caches();
      return set_active_runtime_tool(
          quader_editor_core::ViewportInteractionTool::Select);
    case quader_editor_core::ViewportInteractionTool::Cut:
      (void)execute_runtime_command(RuntimeToolCommandService::cancel_cut(),
                                    false);
      return set_active_runtime_tool(
          quader_editor_core::ViewportInteractionTool::Select);
    case quader_editor_core::ViewportInteractionTool::Mirror:
      (void)execute_runtime_command(RuntimeToolCommandService::cancel_mirror(),
                                    false);
      return set_active_runtime_tool(
          quader_editor_core::ViewportInteractionTool::Select);
    case quader_editor_core::ViewportInteractionTool::Poly:
      (void)execute_runtime_command(RuntimeToolCommandService::cancel_poly(),
                                    false);
      return set_active_runtime_tool(
          quader_editor_core::ViewportInteractionTool::Select);
    case quader_editor_core::ViewportInteractionTool::Select:
    case quader_editor_core::ViewportInteractionTool::Move:
    case quader_editor_core::ViewportInteractionTool::Extend:
    case quader_editor_core::ViewportInteractionTool::Extrude:
    case quader_editor_core::ViewportInteractionTool::Rotate:
    case quader_editor_core::ViewportInteractionTool::Scale:
    case quader_editor_core::ViewportInteractionTool::Unknown:
      break;
    }
    return {.ok = true};
  }

  [[nodiscard]] std::optional<quader_editor_core::PickHitRecord>
  pick_tool_target(const ModelingToolRayEvent &event,
                   quader_poly::SelectionMode mode,
                   int target_object_id = -1) const {
    quader_poly::PickOptions options =
        quader_editor_core::selection_pick_options_from_radii(
            event.vertex_radius, event.edge_radius);
    options.mode = mode;
    return quader_editor_core::pick_nearest(
        cached_scene_transformed_objects_for_picking(),
        {.ray = to_runtime(event.ray),
         .options = options,
         .target_object_id = target_object_id},
        cached_pick_object_bounds(), cached_pick_face_geometry(options));
  }

  [[nodiscard]] std::optional<quader_editor_core::PickHitRecord>
  pick_box_surface(const ModelingBoxToolEvent &event) const {
    return selection_service_.pick_surface_hit(session_, scene_,
                                               object_registry_, event.ray);
  }

  [[nodiscard]] std::array<
      std::optional<quader_editor_core::PickHitRecord>, 3U>
  knife_pick_targets(const ModelingToolRayEvent &event,
                     int target_object_id = -1) const {
    constexpr std::array kModes{
        quader_poly::SelectionMode::Vertex,
        quader_poly::SelectionMode::Edge,
        quader_poly::SelectionMode::Face,
    };
    quader_poly::PickOptions options =
        quader_editor_core::selection_pick_options_from_radii(
            event.vertex_radius, event.edge_radius);
    // Filament renders ordinary meshes with back-face culling. On an
    // inside-out closed mesh the near shell is therefore invisible and the
    // user sees the farther, authored-front interior surface. Prefer that
    // rendered surface for Knife occlusion/context while retaining backfaces
    // as a fallback for isolated reversed or open faces.
    options.pick_backfaces = true;
    options.prefer_front_facing_faces = true;
    quader_poly::PickOptions face_options = options;
    face_options.mode = quader_poly::SelectionMode::Face;
    const std::vector<quader_editor_core::PolygonObject> &objects =
        cached_scene_transformed_objects_for_picking();
    const std::vector<quader_editor_core::PickObjectBounds> &bounds =
        cached_pick_object_bounds();
    const std::vector<quader_editor_core::PickObjectFaceGeometry>
        &face_geometry = cached_pick_face_geometry(face_options);
    std::array<std::optional<quader_editor_core::PickHitRecord>, kModes.size()>
        hits;
    const std::optional<quader_editor_core::PickHitRecord> surface_hit =
        quader_editor_core::pick_nearest(
            objects,
            {.ray = to_runtime(event.ray),
             .options = face_options,
             .target_object_id = target_object_id},
            bounds, face_geometry);
    const bool requires_topology_seed =
        !runtime_.knife_state().has_active_stroke;
    for (std::size_t index = 0U; index < kModes.size(); ++index) {
      if (requires_topology_seed &&
          kModes[index] == quader_poly::SelectionMode::Face) {
        continue;
      }
      if (kModes[index] == quader_poly::SelectionMode::Face) {
        hits[index] = surface_hit;
        continue;
      }
      const double screen_radius =
          kModes[index] == quader_poly::SelectionMode::Vertex
              ? event.vertex_screen_radius
              : event.edge_screen_radius;
      if (screen_radius > 0.0) {
        hits[index] = knife_screen_space_component_pick(
            objects, event, kModes[index], screen_radius, target_object_id,
            face_options, bounds, face_geometry);
        continue;
      }
      options.mode = kModes[index];
      hits[index] = quader_editor_core::pick_nearest(
          objects,
          {.ray = to_runtime(event.ray),
           .options = options,
           .target_object_id = target_object_id},
          bounds, face_geometry);
    }
    return hits;
  }

  [[nodiscard]] std::optional<quader_editor_core::PickHitRecord>
  pick_hit_in_object_space(
      const quader_editor_core::PickHitRecord &world_hit) const {
    const std::optional<QMat4> world_from_object =
        modeling_object_world_transform(world_hit.object_id);
    const std::optional<QMat4> object_from_world =
        world_from_object.has_value()
            ? try_scene_matrix_inverse(*world_from_object)
            : std::nullopt;
    if (!world_from_object.has_value() || !object_from_world.has_value()) {
      return std::nullopt;
    }
    quader_editor_core::PickHitRecord local_hit = world_hit;
    local_hit.local_position =
        transform_point(*object_from_world, world_hit.local_position);
    const QMat4 object_space_normal =
        from_glm(glm::transpose(to_glm(*world_from_object)));
    local_hit.local_normal = normalize(
        transform_vector(object_space_normal, world_hit.local_normal));
    return local_hit;
  }

  [[nodiscard]] std::optional<float> insert_edge_loop_world_factor(
      const quader_editor_core::PickHitRecord &world_hit,
      const ModelingToolRayEvent &event) const {
    const std::vector<quader_editor_core::PolygonObject> &world_objects =
        cached_scene_transformed_objects_for_picking();
    const auto object = std::find_if(
        world_objects.begin(), world_objects.end(),
        [&world_hit](const quader_editor_core::PolygonObject &candidate) {
          return candidate.id == world_hit.object_id;
        });
    if (object == world_objects.end()) {
      return std::nullopt;
    }
    const std::optional<float> raw_factor =
        quader_poly::edge_factor_from_position(
            object->document, world_hit.edge, world_hit.local_position);
    if (!raw_factor.has_value()) {
      return std::nullopt;
    }
    return quader_editor_core::snap_insert_edge_loop_preview_factor(
        object->document, world_hit.edge, *raw_factor, event.grid_size,
        event.snap_enabled);
  }

  [[nodiscard]] std::optional<float> insert_edge_loop_world_drag_factor(
      const ModelingToolRayEvent &event) const {
    const quader_editor_core::InsertEdgeLoopToolState &state =
        runtime_.insert_edge_loop_state();
    const std::vector<quader_editor_core::PolygonObject> &world_objects =
        cached_scene_transformed_objects_for_picking();
    const auto object = std::find_if(
        world_objects.begin(), world_objects.end(),
        [&state](const quader_editor_core::PolygonObject &candidate) {
          return candidate.id == state.object_id;
        });
    if (object == world_objects.end()) {
      return std::nullopt;
    }
    const std::optional<float> raw_factor =
        quader_poly::edge_factor_from_ray(object->document, state.edge,
                                          to_runtime(event.ray));
    return quader_editor_core::snap_insert_edge_loop_preview_factor(
        object->document, state.edge, raw_factor.value_or(state.factor),
        event.grid_size, event.snap_enabled);
  }

  /**
   * Stores one topology-derived straight constraint for the active knife
   * segment in world space.
   */
  struct KnifeStraightConstraint {
    QVec3 previous;
    QVec3 direction;
    QVec3 point;
    quader_poly::ElementId face_id = quader_poly::kInvalidElementId;
  };

  [[nodiscard]] static bool
  knife_face_contains_edge(const quader_poly::Face &face,
                           quader_poly::Edge edge) {
    edge = quader_poly::make_edge(edge.a, edge.b);
    for (std::size_t index = 0U; index < face.vertices.size(); ++index) {
      if (quader_poly::make_edge(
              face.vertices[index],
              face.vertices[(index + 1U) % face.vertices.size()]) == edge) {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] static bool knife_face_contains_target(
      const quader_poly::Face &face,
      const quader_poly::KnifePointTarget &target) {
    switch (target.kind) {
    case quader_poly::KnifePointTargetKind::ExistingVertex:
    case quader_poly::KnifePointTargetKind::InsertedVertex:
      return std::ranges::find(face.vertices, target.vertex_id) !=
             face.vertices.end();
    case quader_poly::KnifePointTargetKind::ExistingEdge:
      return knife_face_contains_edge(face, target.edge);
    case quader_poly::KnifePointTargetKind::FacePoint:
      return face.id == target.face_id;
    }
    return false;
  }

  [[nodiscard]] static bool
  knife_face_contains_hit(const quader_poly::Face &face,
                          const quader_editor_core::PickHitRecord &hit) {
    switch (hit.kind) {
    case quader_poly::ElementKind::Vertex:
      return std::ranges::find(face.vertices, hit.vertex_id) !=
             face.vertices.end();
    case quader_poly::ElementKind::Edge:
      return knife_face_contains_edge(face, hit.edge);
    case quader_poly::ElementKind::Face:
      return face.id == hit.face_id;
    }
    return false;
  }

  [[nodiscard]] const quader_poly::Face *knife_active_constraint_face(
      const quader_poly::Document &document,
      const quader_editor_core::PickHitRecord &hit) const {
    const quader_editor_core::KnifeToolState &knife = runtime_.knife_state();
    if (!knife.has_active_stroke ||
        knife.previous_point_index >= knife.points.size()) {
      return nullptr;
    }

    const quader_poly::KnifePointTarget &previous =
        knife.points[knife.previous_point_index];
    const quader_poly::Face *fallback = nullptr;
    for (const quader_poly::Face &face : document.faces) {
      if (!knife_face_contains_target(face, previous) ||
          !knife_face_contains_hit(face, hit)) {
        continue;
      }
      if (face.id == hit.face_id) {
        return &face;
      }
      fallback = fallback != nullptr ? fallback : &face;
    }
    return fallback;
  }

  [[nodiscard]] static std::vector<QVec3> knife_face_directions(
      const quader_poly::Document &document, const quader_poly::Face &face) {
    std::vector<QVec3> directions;
    directions.reserve(face.vertices.size());
    for (std::size_t index = 0U; index < face.vertices.size(); ++index) {
      const quader_poly::Vertex *first =
          quader_poly::find_vertex(document, face.vertices[index]);
      const quader_poly::Vertex *second = quader_poly::find_vertex(
          document, face.vertices[(index + 1U) % face.vertices.size()]);
      if (first == nullptr || second == nullptr) {
        continue;
      }
      const QVec3 direction = normalize(second->position - first->position);
      if (length_squared(direction) <= 0.000001F ||
          std::ranges::any_of(directions, [&direction](QVec3 existing) {
            return std::abs(dot(existing, direction)) >= 0.9999F;
          })) {
        continue;
      }
      directions.push_back(direction);
    }
    return directions;
  }

  [[nodiscard]] std::optional<KnifeStraightConstraint>
  knife_straight_constraint(
      const quader_editor_core::PolygonObject &object,
      const quader_editor_core::PickHitRecord &hit,
      QVec3 raw_world_position, double grid_size, bool snap_enabled) const {
    const quader_editor_core::KnifeToolState &knife = runtime_.knife_state();
    if (!knife.has_active_stroke ||
        knife.previous_point_index >= knife.point_positions.size()) {
      return std::nullopt;
    }
    const quader_poly::Face *face =
        knife_active_constraint_face(object.document, hit);
    if (face == nullptr) {
      return std::nullopt;
    }

    const std::optional<QMat4> world_from_object =
        modeling_object_world_transform(hit.object_id);
    if (!world_from_object.has_value()) {
      return std::nullopt;
    }
    const QVec3 previous = transform_point(
        *world_from_object, knife.point_positions[knife.previous_point_index]);
    const std::vector<QVec3> directions =
        knife_face_directions(object.document, *face);
    const std::optional<QVec3> constrained =
        quader_editor_core::constrain_knife_point_to_face_directions(
            previous, raw_world_position, directions, grid_size,
            snap_enabled);
    if (!constrained.has_value()) {
      return std::nullopt;
    }
    const QVec3 direction = normalize(*constrained - previous);
    if (length_squared(direction) <= 0.000001F) {
      return std::nullopt;
    }
    return KnifeStraightConstraint{
        .previous = previous,
        .direction = direction,
        .point = *constrained,
        .face_id = face->id,
    };
  }

  [[nodiscard]] static std::optional<QVec3>
  knife_straight_edge_intersection(
      const quader_poly::Document &document, quader_poly::Edge edge,
      QVec3 raw_world_position, const KnifeStraightConstraint &constraint) {
    const quader_poly::Vertex *first =
        quader_poly::find_vertex(document, edge.a);
    const quader_poly::Vertex *second =
        quader_poly::find_vertex(document, edge.b);
    if (first == nullptr || second == nullptr) {
      return std::nullopt;
    }

    const QVec3 edge_direction = second->position - first->position;
    const float edge_length = std::sqrt(length_squared(edge_direction));
    if (edge_length <= 0.000001F) {
      return std::nullopt;
    }
    const float tolerance = std::max(0.0001F, edge_length * 0.00001F);
    const float parallel_measure = length_squared(
        cross(constraint.direction, edge_direction / edge_length));
    if (parallel_measure <= 0.000001F) {
      const QVec3 from_line = raw_world_position - constraint.previous;
      const QVec3 projected =
          constraint.previous +
          constraint.direction * dot(from_line, constraint.direction);
      if (length_squared(projected - raw_world_position) >
          tolerance * tolerance) {
        return std::nullopt;
      }
      return raw_world_position;
    }

    const quader_geometry::QLineLineClosestPoints<float> intersection =
        quader_geometry::closest_points_line_line<float>(
            {quader_editor_core::tool_geometry_vec3(constraint.previous),
             quader_editor_core::tool_geometry_vec3(constraint.direction)},
            {quader_editor_core::tool_geometry_vec3(first->position),
             quader_editor_core::tool_geometry_vec3(edge_direction)},
            tolerance);
    if (!intersection.valid ||
        intersection.distance_squared > tolerance * tolerance ||
        intersection.second_parameter <= 0.01F ||
        intersection.second_parameter >= 0.99F) {
      return std::nullopt;
    }
    return QVec3{intersection.midpoint.x, intersection.midpoint.y,
                 intersection.midpoint.z};
  }

  [[nodiscard]] std::optional<quader_editor_core::PickHitRecord>
  knife_hit_with_topology_safe_resolution(
      const quader_editor_core::PickHitRecord &world_hit,
      const ModelingToolRayEvent &event) const {
    const std::optional<quader_editor_core::PickHitRecord> local_hit =
        knife_hit_in_object_space(world_hit, event);
    if (!local_hit.has_value() ||
        world_hit.kind != quader_poly::ElementKind::Face ||
        (!event.constrain_axis && !event.snap_enabled)) {
      return local_hit;
    }

    quader_poly::PickOptions face_options =
        quader_editor_core::selection_pick_options_from_radii(
            event.vertex_radius, event.edge_radius);
    face_options.mode = quader_poly::SelectionMode::Face;
    const auto &geometry = cached_pick_face_geometry(face_options);
    const auto object = std::find_if(
        geometry.begin(), geometry.end(),
        [&world_hit](const quader_editor_core::PickObjectFaceGeometry &entry) {
          return entry.object_id == world_hit.object_id;
        });
    if (object == geometry.end()) {
      return std::nullopt;
    }

    const std::optional<QMat4> world_from_object =
        modeling_object_world_transform(world_hit.object_id);
    if (!world_from_object.has_value()) {
      return std::nullopt;
    }
    const QVec3 resolved_world_position =
        transform_point(*world_from_object, local_hit->local_position);
    const bool stays_on_face =
        std::ranges::any_of(object->triangle_span(),
                            [&local_hit, resolved_world_position](
                                const quader_poly::PickFaceTriangle &triangle) {
                              return triangle.face_id == local_hit->face_id &&
                                     quader_geometry::point_in_triangle(
                                         quader_editor_core::tool_geometry_vec3(
                                             resolved_world_position),
                                         quader_geometry::QTriangle3<float>{
                                             quader_editor_core::tool_geometry_vec3(
                                                 triangle.a),
                                             quader_editor_core::tool_geometry_vec3(
                                                 triangle.b),
                                             quader_editor_core::tool_geometry_vec3(
                                                 triangle.c)},
                                         0.0001F, 0.001F);
                            });
    if (stays_on_face) {
      return local_hit;
    }
    if (event.constrain_axis) {
      return std::nullopt;
    }
    ModelingToolRayEvent unsnapped_event = event;
    unsnapped_event.snap_enabled = false;
    return knife_hit_in_object_space(world_hit, unsnapped_event);
  }

  [[nodiscard]] std::optional<quader_editor_core::PickHitRecord>
  knife_hit_in_object_space(
      const quader_editor_core::PickHitRecord &world_hit,
      const ModelingToolRayEvent &event) const {
    const std::optional<QMat4> world_from_object =
        modeling_object_world_transform(world_hit.object_id);
    const std::optional<QMat4> object_from_world =
        world_from_object.has_value()
            ? try_scene_matrix_inverse(*world_from_object)
            : std::nullopt;
    if (!world_from_object.has_value() || !object_from_world.has_value()) {
      return std::nullopt;
    }
    const std::vector<quader_editor_core::PolygonObject> &world_objects =
        cached_scene_transformed_objects_for_picking();
    const auto object = std::find_if(
        world_objects.begin(), world_objects.end(),
        [&world_hit](const quader_editor_core::PolygonObject &candidate) {
          return candidate.id == world_hit.object_id;
        });
    if (object == world_objects.end()) {
      return std::nullopt;
    }

    QVec3 resolved_world_position = world_hit.local_position;
    quader_poly::ElementId resolved_face_id = world_hit.face_id;
    quader_poly::ElementKind resolved_kind = world_hit.kind;
    quader_poly::ElementId resolved_vertex_id = world_hit.vertex_id;
    quader_poly::Edge resolved_edge = quader_poly::make_edge(
        world_hit.edge.a, world_hit.edge.b);
    if (world_hit.kind == quader_poly::ElementKind::Edge) {
      if (event.constrain_axis) {
        const std::optional<KnifeStraightConstraint> constraint =
            knife_straight_constraint(*object, world_hit,
                                      resolved_world_position,
                                      event.grid_size, event.snap_enabled);
        if (!constraint.has_value()) {
          return std::nullopt;
        }
        const std::optional<QVec3> intersection =
            knife_straight_edge_intersection(
                object->document, world_hit.edge, resolved_world_position,
                *constraint);
        if (!intersection.has_value()) {
          return std::nullopt;
        }
        resolved_world_position = *intersection;
        resolved_face_id = constraint->face_id;
      } else {
        const std::optional<QVec3> snapped =
            quader_editor_core::snap_knife_point_to_edge_grid(
                object->document, resolved_edge, resolved_world_position,
                event.grid_size, event.snap_enabled);
        if (!snapped.has_value()) {
          return std::nullopt;
        }
        resolved_world_position = *snapped;
      }
      const std::optional<float> factor = quader_poly::edge_factor_from_position(
          object->document, resolved_edge, resolved_world_position);
      const quader_poly::Vertex *a = quader_poly::find_vertex(
          object->document, resolved_edge.a);
      const quader_poly::Vertex *b = quader_poly::find_vertex(
          object->document, resolved_edge.b);
      if (!factor.has_value() || a == nullptr || b == nullptr) {
        return std::nullopt;
      }
      if (*factor <= 0.01F || *factor >= 0.99F) {
        resolved_kind = quader_poly::ElementKind::Vertex;
        resolved_vertex_id =
            *factor <= 0.01F ? resolved_edge.a : resolved_edge.b;
        const quader_poly::Vertex *endpoint =
            quader_poly::find_vertex(object->document, resolved_vertex_id);
        if (endpoint == nullptr) {
          return std::nullopt;
        }
        resolved_world_position = endpoint->position;
      } else {
        resolved_world_position =
            a->position + ((b->position - a->position) * *factor);
      }
    } else if (world_hit.kind == quader_poly::ElementKind::Vertex &&
               event.constrain_axis) {
      const std::optional<KnifeStraightConstraint> constraint =
          knife_straight_constraint(*object, world_hit,
                                    resolved_world_position,
                                    event.grid_size, event.snap_enabled);
      if (!constraint.has_value()) {
        return std::nullopt;
      }
      const float tolerance = std::max(
          0.0001F,
          std::sqrt(length_squared(resolved_world_position -
                                   constraint->previous)) *
              0.00001F);
      if (length_squared(constraint->point - resolved_world_position) >
          tolerance * tolerance) {
        return std::nullopt;
      }
      resolved_face_id = constraint->face_id;
    } else if (world_hit.kind == quader_poly::ElementKind::Face) {
      if (event.constrain_axis) {
        const std::optional<KnifeStraightConstraint> constraint =
            knife_straight_constraint(*object, world_hit,
                                      resolved_world_position,
                                      event.grid_size, event.snap_enabled);
        if (!constraint.has_value()) {
          return std::nullopt;
        }
        resolved_world_position = constraint->point;
        resolved_face_id = constraint->face_id;
      } else {
        const std::optional<QVec3> snapped =
            quader_editor_core::snap_knife_point_to_face_grid(
                object->document, resolved_face_id, resolved_world_position,
                event.grid_size, event.snap_enabled);
        if (!snapped.has_value()) {
          return std::nullopt;
        }
        resolved_world_position = *snapped;
      }
      const quader_poly::Face *face =
          quader_poly::find_face(object->document, resolved_face_id);
      if (face == nullptr || face->vertices.empty()) {
        return std::nullopt;
      }
      const quader_poly::Vertex *origin =
          quader_poly::find_vertex(object->document, face->vertices.front());
      const QVec3 normal = quader_poly::face_normal(object->document, *face);
      if (origin == nullptr || length_squared(normal) <= 0.000001F) {
        return std::nullopt;
      }
      const float normal_length_squared = length_squared(normal);
      resolved_world_position =
          resolved_world_position -
          normal * (dot(resolved_world_position - origin->position, normal) /
                    normal_length_squared);

      constexpr float kBoundaryToleranceSquared = 0.00000001F;
      for (const quader_poly::ElementId vertex_id : face->vertices) {
        const quader_poly::Vertex *vertex =
            quader_poly::find_vertex(object->document, vertex_id);
        if (vertex != nullptr &&
            length_squared(vertex->position - resolved_world_position) <=
                kBoundaryToleranceSquared) {
          resolved_kind = quader_poly::ElementKind::Vertex;
          resolved_vertex_id = vertex_id;
          resolved_world_position = vertex->position;
          break;
        }
      }
      if (resolved_kind == quader_poly::ElementKind::Face) {
        for (std::size_t index = 0U; index < face->vertices.size(); ++index) {
          const quader_poly::Edge edge = quader_poly::make_edge(
              face->vertices[index],
              face->vertices[(index + 1U) % face->vertices.size()]);
          const quader_poly::Vertex *first =
              quader_poly::find_vertex(object->document, edge.a);
          const quader_poly::Vertex *second =
              quader_poly::find_vertex(object->document, edge.b);
          const std::optional<float> factor =
              quader_poly::edge_factor_from_position(
                  object->document, edge, resolved_world_position);
          if (first == nullptr || second == nullptr || !factor.has_value()) {
            continue;
          }
          const QVec3 edge_point =
              first->position + (second->position - first->position) * *factor;
          if (length_squared(edge_point - resolved_world_position) >
              kBoundaryToleranceSquared) {
            continue;
          }
          resolved_kind = quader_poly::ElementKind::Edge;
          resolved_edge = edge;
          resolved_world_position = edge_point;
          break;
        }
      }
    }

    quader_editor_core::PickHitRecord local_hit = world_hit;
    local_hit.kind = resolved_kind;
    local_hit.vertex_id = resolved_vertex_id;
    local_hit.edge = resolved_edge;
    local_hit.face_id = resolved_face_id;
    local_hit.local_position =
        transform_point(*object_from_world, resolved_world_position);
    const QMat4 object_space_normal =
        from_glm(glm::transpose(to_glm(*world_from_object)));
    local_hit.local_normal = normalize(
        transform_vector(object_space_normal, world_hit.local_normal));
    return local_hit;
  }

  [[nodiscard]] std::optional<ModelingToolRayEvent>
  tool_event_in_object_space(const ModelingToolRayEvent &event,
                             int target_object_id) const {
    if (target_object_id <= 0) {
      return event;
    }
    const std::optional<QMat4> world_from_object =
        modeling_object_world_transform(target_object_id);
    const std::optional<QMat4> object_from_world =
        world_from_object.has_value()
            ? try_scene_matrix_inverse(*world_from_object)
            : std::nullopt;
    if (!object_from_world.has_value()) {
      return std::nullopt;
    }

    ModelingToolRayEvent local = event;
    local.ray.origin = transform_point(*object_from_world, event.ray.origin);
    local.ray.direction =
        normalize(transform_vector(*object_from_world, event.ray.direction));
    if (length_squared(local.ray.direction) <= 0.000001F) {
      return std::nullopt;
    }
    local.camera.origin =
        transform_point(*object_from_world, event.camera.origin);
    const QMat4 object_space_camera_covectors =
        from_glm(glm::transpose(to_glm(*world_from_object)));
    local.camera.affine_model_space = true;
    local.camera.x_axis = transform_vector(object_space_camera_covectors,
                                           event.camera.x_axis);
    local.camera.y_axis = transform_vector(object_space_camera_covectors,
                                           event.camera.y_axis);
    local.camera.z_axis = transform_vector(object_space_camera_covectors,
                                           event.camera.z_axis);
    return local;
  }

  ModelingCommandResult
  update_insert_edge_loop_hover(const ModelingToolRayEvent &event) {
    const std::optional<quader_editor_core::PickHitRecord> world_hit =
        pick_tool_target(event, quader_poly::SelectionMode::Edge);
    const int target_object_id = world_hit.has_value()
                                     ? world_hit->object_id
                                     : std::max(runtime_.insert_edge_loop_state()
                                                    .object_id,
                                                0);
    const std::optional<quader_editor_core::PickHitRecord> local_hit =
        world_hit.has_value() ? pick_hit_in_object_space(*world_hit)
                              : std::nullopt;
    const std::optional<float> resolved_factor =
        world_hit.has_value()
            ? insert_edge_loop_world_factor(*world_hit, event)
            : std::nullopt;
    if (world_hit.has_value() && !local_hit.has_value()) {
      return {.ok = false,
              .message = "Edge loop target transform is not invertible."};
    }
    const std::optional<ModelingToolRayEvent> local_event =
        tool_event_in_object_space(event, target_object_id);
    if (!local_event.has_value()) {
      return {.ok = false,
              .message = "Edge loop target transform is not invertible."};
    }
    return execute_runtime_command(
        RuntimeToolCommandService::update_insert_edge_loop_hover(
            *local_event, target_object_id, local_hit, resolved_factor),
        false);
  }

  ModelingCommandResult
  begin_insert_edge_loop_drag(const ModelingToolRayEvent &event) {
    const std::optional<quader_editor_core::PickHitRecord> world_hit =
        pick_tool_target(event, quader_poly::SelectionMode::Edge);
    const int target_object_id = world_hit.has_value()
                                     ? world_hit->object_id
                                     : std::max(runtime_.insert_edge_loop_state()
                                                    .object_id,
                                                0);
    const std::optional<quader_editor_core::PickHitRecord> local_hit =
        world_hit.has_value() ? pick_hit_in_object_space(*world_hit)
                              : std::nullopt;
    const std::optional<float> resolved_factor =
        world_hit.has_value()
            ? insert_edge_loop_world_factor(*world_hit, event)
            : std::nullopt;
    if (world_hit.has_value() && !local_hit.has_value()) {
      return {.ok = false,
              .message = "Edge loop target transform is not invertible."};
    }
    const std::optional<ModelingToolRayEvent> local_event =
        tool_event_in_object_space(event, target_object_id);
    if (!local_event.has_value()) {
      return {.ok = false,
              .message = "Edge loop target transform is not invertible."};
    }
    return execute_runtime_command(
        RuntimeToolCommandService::begin_insert_edge_loop_drag(
            *local_event, target_object_id, local_hit, resolved_factor),
        false);
  }

  ModelingCommandResult
  update_insert_edge_loop_drag(const ModelingToolRayEvent &event) {
    const int target_object_id = runtime_.insert_edge_loop_state().object_id;
    const std::optional<float> resolved_factor =
        insert_edge_loop_world_drag_factor(event);
    const std::optional<ModelingToolRayEvent> local_event =
        tool_event_in_object_space(event, target_object_id);
    if (!local_event.has_value()) {
      return {.ok = false,
              .message = "Edge loop target transform is not invertible."};
    }
    return execute_runtime_command(
        RuntimeToolCommandService::update_insert_edge_loop_drag(
            *local_event, target_object_id, resolved_factor, true),
        false);
  }

  ModelingCommandResult commit_insert_edge_loop_drag() {
    return execute_runtime_command(
        RuntimeToolCommandService::commit_insert_edge_loop_drag(), true);
  }

  ModelingCommandResult clear_knife_hover() {
    const std::uint64_t overlay_signature_before =
        overlay_service_.selection_overlay_signature(runtime_);
    runtime_.clear_knife_hover();
    const bool changed =
        overlay_signature_before !=
        overlay_service_.selection_overlay_signature(runtime_);
    if (changed) {
      invalidate_selection_overlay_caches();
    }
    return {.ok = true, .changed = changed};
  }

  ModelingCommandResult update_knife_hover(const ModelingToolRayEvent &event) {
    const std::uint64_t overlay_signature_before =
        overlay_service_.selection_overlay_signature(runtime_);
    auto finish = [&](bool ok, std::string message) {
      const bool changed =
          overlay_signature_before !=
          overlay_service_.selection_overlay_signature(runtime_);
      if (changed) {
        invalidate_selection_overlay_caches();
      }
      return ModelingCommandResult{
          .ok = ok,
          .changed = changed,
          .message = std::move(message),
      };
    };
    const int locked_object_id = runtime_.knife_state().target_object_id;
    const auto world_hits = knife_pick_targets(
        event, locked_object_id > 0 ? locked_object_id : -1);
    std::array<std::optional<quader_editor_core::PickHitRecord>, 3U>
        local_hits;
    std::array<std::optional<ModelingToolRayEvent>, 3U>
        local_events;
    for (std::size_t index = 0U; index < world_hits.size(); ++index) {
      if (!world_hits[index].has_value()) {
        continue;
      }
      local_events[index] =
          tool_event_in_object_space(event, world_hits[index]->object_id);
      if (!local_events[index].has_value()) {
        runtime_.clear_knife_hover();
        return finish(false, "Knife target transform is not invertible.");
      }
      local_hits[index] =
          knife_hit_with_topology_safe_resolution(*world_hits[index], event);
    }

    bool target_valid = false;
    for (std::size_t index = 0U; index < local_hits.size(); ++index) {
      if (!local_hits[index].has_value()) {
        continue;
      }
      target_valid = runtime_.update_knife_preview_from_hit(
          session_, *local_hits[index], local_events[index]->grid_size, false,
          false);
      if (target_valid) {
        break;
      }
    }
    if (!target_valid) {
      runtime_.clear_knife_hover();
    }
    return finish(true, {});
  }

  ModelingCommandResult accept_knife_point(const ModelingToolRayEvent &event) {
    const ModelingCommandResult resolved = update_knife_hover(event);
    if (!resolved.ok) {
      return resolved;
    }
    const int target_object_id = runtime_.knife_state().current_object_id;
    const std::optional<ModelingToolRayEvent> local_event =
        tool_event_in_object_space(event, target_object_id);
    if (!local_event.has_value()) {
      return {.ok = false,
              .message = "Knife target transform is not invertible."};
    }
    const bool changed =
        runtime_.accept_knife_current_point(local_event->grid_size);
    if (changed) {
      invalidate_selection_overlay_caches();
    }
    return {.ok = true, .changed = resolved.changed || changed};
  }

  ModelingCommandResult commit_knife_tool() {
    const std::uint64_t content_before = session_.state().content_revision;
    const quader_editor_core::CommandResult result =
        runtime_.commit_knife_tool(session_);
    if (session_.state().content_revision != content_before || result.changed) {
      sync_scene_objects_from_modeling(result.changed);
    }
    sync_scene_selection_from_modeling();
    invalidate_selection_overlay_caches();
    if (result.ok) {
      const ModelingCommandResult select_tool = set_active_runtime_tool(
          quader_editor_core::ViewportInteractionTool::Select);
      return {
          .ok = select_tool.ok,
          .changed = result.changed || select_tool.changed,
          .message =
              !result.message.empty() ? result.message : select_tool.message,
      };
    }
    return {
        .ok = result.ok, .changed = result.changed, .message = result.message};
  }

  ModelingCommandResult update_cut_hover(const ModelingToolRayEvent &event) {
    return execute_runtime_command(
        RuntimeToolCommandService::update_cut_hover(event), false);
  }

  ModelingCommandResult click_cut(const ModelingToolRayEvent &event) {
    return execute_runtime_command(RuntimeToolCommandService::click_cut(event),
                                   false);
  }

  ModelingCommandResult begin_cut_point_drag(int point_index) {
    return execute_runtime_command(
        RuntimeToolCommandService::begin_cut_point_drag(point_index), false);
  }

  ModelingCommandResult begin_cut_line_drag(const ModelingToolRayEvent &event) {
    return execute_runtime_command(
        RuntimeToolCommandService::begin_cut_line_drag(event), false);
  }

  ModelingCommandResult end_cut_drag() {
    return execute_runtime_command(RuntimeToolCommandService::end_cut_drag(),
                                   false);
  }

  ModelingCommandResult set_cut_mode(ModelingCutMode mode) {
    interaction_settings_service_.set_cut_mode(mode);
    return execute_runtime_command(
        RuntimeToolCommandService::set_cut_mode(mode), false);
  }

  ModelingCommandResult cycle_cut_mode() {
    return set_cut_mode(from_runtime(quader_editor_core::next_cut_mode(
        to_runtime(interaction_settings_service_.settings().cut_mode))));
  }

  ModelingCommandResult finalize_cut_tool() {
    return execute_runtime_command(RuntimeToolCommandService::finalize_cut(),
                                   true);
  }

  ModelingCommandResult update_mirror_hover(const ModelingToolRayEvent &event) {
    return execute_runtime_command(
        RuntimeToolCommandService::update_mirror_hover(event), false);
  }

  ModelingCommandResult click_mirror(const ModelingToolRayEvent &event) {
    return execute_runtime_command(
        RuntimeToolCommandService::click_mirror(event), false);
  }

  ModelingCommandResult begin_mirror_point_drag(int point_index) {
    return execute_runtime_command(
        RuntimeToolCommandService::begin_mirror_point_drag(point_index), false);
  }

  ModelingCommandResult
  begin_mirror_line_drag(const ModelingToolRayEvent &event) {
    return execute_runtime_command(
        RuntimeToolCommandService::begin_mirror_line_drag(event), false);
  }

  ModelingCommandResult end_mirror_drag() {
    return execute_runtime_command(RuntimeToolCommandService::end_mirror_drag(),
                                   false);
  }

  ModelingCommandResult finalize_mirror_tool() {
    return execute_runtime_command(RuntimeToolCommandService::finalize_mirror(),
                                   true);
  }

  ModelingCommandResult update_poly_hover(const ModelingToolRayEvent &event) {
    return execute_runtime_command(
        RuntimeToolCommandService::update_poly_hover(event), false);
  }

  ModelingCommandResult accept_poly_point() {
    return execute_runtime_command(
        RuntimeToolCommandService::accept_poly_point(), false);
  }

  ModelingCommandResult commit_poly_tool(MaterialReference material = {}) {
    const std::uint64_t content_before = session_.state().content_revision;
    const std::size_t object_count_before = session_.objects().size();
    const quader_editor_core::EditorRuntimeCommandResult runtime_result =
        quader_editor_core::execute_runtime_command(
            session_, runtime_, RuntimeToolCommandService::commit_poly());
    if (session_.state().content_revision != content_before ||
        runtime_result.command.changed) {
      sync_scene_objects_from_modeling(runtime_result.command.changed);
    }
    if (runtime_result.command.ok && runtime_result.command.changed &&
        material.valid() && session_.objects().size() > object_count_before) {
      const quader_editor_core::PolygonObject &created =
          session_.objects().back();
      const std::optional<EntityId> entity =
          object_registry_.entity_for_modeling_object(created.id);
      if (entity.has_value() && scene_.exists(*entity)) {
        (void)mesh_sync_service_.set_entity_material(
            scene_, *entity, MaterialComponent{.material = material});
        object_registry_.remember_scene_entity(created.id, scene_, *entity);
      }
    }
    sync_scene_selection_from_modeling();
    invalidate_selection_overlay_caches();
    if (runtime_result.command.ok) {
      const ModelingCommandResult select_tool = set_active_runtime_tool(
          quader_editor_core::ViewportInteractionTool::Select);
      return {
          .ok = select_tool.ok,
          .changed = runtime_result.command.changed || select_tool.changed,
          .message = !runtime_result.command.message.empty()
                         ? runtime_result.command.message
                         : select_tool.message,
      };
    }
    return {.ok = runtime_result.command.ok,
            .changed = runtime_result.command.changed,
            .message = runtime_result.command.message};
  }

  ModelingCommandResult activate_box_tool() {
    if (quader_host_contract::find_tool_descriptor("create_primitive") ==
        nullptr) {
      return {.ok = false,
              .message =
                  "Create Primitive is not present in the modeling host contract."};
    }

    return set_active_runtime_tool(
        quader_editor_core::ViewportInteractionTool::Box);
  }

  ModelingCreateBoxResult
  create_box_from_bounds(const ModelingCreateBoxRequest &request) {
    const auto finite_vector = [](QVec3 value) {
      return std::isfinite(value.x) && std::isfinite(value.y) &&
             std::isfinite(value.z);
    };
    if (!finite_vector(request.bounds.min) ||
        !finite_vector(request.bounds.max)) {
      return {
          .ok = false,
          .changed = false,
          .message = "Box bounds must be finite.",
      };
    }

    CreationProjectionCheckpoint checkpoint =
        capture_creation_projection_checkpoint();
    const std::size_t object_count_before = session_.objects().size();
    const quader_editor_core::EditorRuntimeCommandResult runtime_result =
        quader_editor_core::execute_runtime_command(
            session_, runtime_,
            RuntimeToolCommandService::create_box_from_bounds(request));
    if (!runtime_result.command.ok || !runtime_result.command.changed ||
        session_.objects().size() <= object_count_before) {
      ModelingCreateBoxResult failed{
          .ok = runtime_result.command.ok,
          .changed = runtime_result.command.changed,
          .message = runtime_result.command.message.empty()
                         ? "Modeling runtime did not create a box object."
                         : runtime_result.command.message,
      };
      restore_creation_projection_checkpoint(std::move(checkpoint));
      return failed;
    }

    ModelingCreateBoxResult created = create_scene_object_from_latest(
        request.name.empty() ? "Box" : request.name,
        runtime_result.command.message);
    if (!created.ok) {
      restore_creation_projection_checkpoint(std::move(checkpoint));
    } else if (request.material.valid()) {
      (void)mesh_sync_service_.set_entity_material(
          scene_, created.object.entity,
          MaterialComponent{.material = request.material});
      object_registry_.remember_scene_entity(created.object.modeling_object_id,
                                             scene_, created.object.entity);
    }
    return created;
  }

  ModelingBoxPreview update_box_hover(const ModelingBoxToolEvent &event) {
    return execute_box_preview_command(RuntimeToolCommandService::update_box_hover(
                                           event, pick_box_surface(event)),
                                       event.snap_enabled);
  }

  ModelingBoxPreview begin_box_footprint(const ModelingBoxToolEvent &event) {
    ModelingBoxPreview preview = execute_box_preview_command(
        RuntimeToolCommandService::begin_box_footprint(
            event, pick_box_surface(event)),
        event.snap_enabled);
    if (preview.ok) {
      selection_projection_service_.restore_scene_selection(
          scene_, std::span<const EntityId>{}, kInvalidEntityId);
    }
    return preview;
  }

  ModelingBoxPreview update_box_footprint(const ModelingBoxToolEvent &event) {
    return execute_box_preview_command(
        RuntimeToolCommandService::update_box_footprint(event),
        event.snap_enabled);
  }

  ModelingBoxPreview begin_box_height(const ModelingBoxToolEvent &event) {
    return execute_box_preview_command(
        RuntimeToolCommandService::begin_box_height(event), event.snap_enabled);
  }

  ModelingBoxPreview update_box_height(const ModelingBoxToolEvent &event) {
    return execute_box_preview_command(
        RuntimeToolCommandService::update_box_height(event),
        event.snap_enabled);
  }

  [[nodiscard]] ModelingBoxPreview box_preview(bool snap_enabled = true) const {
    return current_box_preview(snap_enabled);
  }

  ModelingCreateBoxResult commit_box(std::string name) {
    CreationProjectionCheckpoint checkpoint =
        capture_creation_projection_checkpoint();
    const std::size_t object_count_before = session_.objects().size();
    const quader_editor_core::EditorRuntimeCommandResult runtime_result =
        quader_editor_core::execute_runtime_command(
            session_, runtime_, RuntimeToolCommandService::commit_box());
    if (!runtime_result.command.ok || !runtime_result.command.changed ||
        session_.objects().size() <= object_count_before) {
      ModelingCreateBoxResult failed{
          .ok = runtime_result.command.ok,
          .changed = runtime_result.command.changed,
          .message = runtime_result.command.message.empty()
                         ? "Modeling runtime did not commit a box object."
                         : runtime_result.command.message,
      };
      restore_creation_projection_checkpoint(std::move(checkpoint));
      return failed;
    }

    ModelingCreateBoxResult created = create_scene_object_from_latest(
        name.empty() ? "Box" : name, runtime_result.command.message);
    if (!created.ok) {
      restore_creation_projection_checkpoint(std::move(checkpoint));
      return created;
    }
    const ModelingCommandResult select_tool = set_active_runtime_tool(
        quader_editor_core::ViewportInteractionTool::Select);
    created.changed = created.changed || select_tool.changed;
    if (!select_tool.ok) {
      created.ok = false;
      created.message =
          select_tool.message.empty()
              ? "Box committed, but Modeling runtime tool reset failed."
              : select_tool.message;
    }
    return created;
  }

  ModelingCommandResult cancel_box() {
    const quader_editor_core::EditorRuntimeCommandResult result =
        quader_editor_core::execute_runtime_command(
            session_, runtime_, RuntimeToolCommandService::cancel_box());
    const ModelingCommandResult select_tool = set_active_runtime_tool(
        quader_editor_core::ViewportInteractionTool::Select);
    return {
        .ok = result.command.ok && select_tool.ok,
        .changed = result.command.changed || select_tool.changed,
        .message = !result.command.message.empty() ? result.command.message
                                                   : select_tool.message,
    };
  }

  ModelingCreateBoxResult
  create_mesh(const ModelingCreateMeshRequest &request) {
    if (request.document.vertices.empty() || request.document.faces.empty()) {
      return {
          .ok = false,
          .changed = false,
          .message = "Document mesh must contain vertices and faces.",
      };
    }

    quader_editor_core::PolygonObject projection_candidate;
    projection_candidate.id = session_.next_object_id();
    projection_candidate.material_id =
        static_cast<int>(request.material_slot);
    projection_candidate.document = request.document;
    const std::optional<MeshAsset> projected_mesh =
        mesh_sync_service_.compile_object_mesh(projection_candidate);
    if (!projected_mesh.has_value()) {
      return {
          .ok = false,
          .changed = false,
          .message = "Modeling mesh projection could not be compiled.",
      };
    }
    const AssetRegistryValidationResult projection_validation =
        assets_.validate_mesh(*projected_mesh);
    if (!projection_validation.ok) {
      return {
          .ok = false,
          .changed = false,
          .message = projection_validation.message.empty()
                         ? "Modeling mesh projection is invalid."
                         : projection_validation.message,
      };
    }

    CreationProjectionCheckpoint checkpoint =
        capture_creation_projection_checkpoint();
    const std::size_t object_count_before = session_.objects().size();
    const quader_editor_core::CommandResult runtime_result =
        session_.create_object_from_document(request.document, request.name,
                                             request.material_slot);
    if (!runtime_result.ok || !runtime_result.changed ||
        session_.objects().size() <= object_count_before) {
      ModelingCreateBoxResult failed{
          .ok = runtime_result.ok,
          .changed = runtime_result.changed,
          .message = runtime_result.message.empty()
                         ? "Modeling runtime did not create a mesh object."
                         : runtime_result.message,
      };
      restore_creation_projection_checkpoint(std::move(checkpoint));
      return failed;
    }

    ModelingCreateBoxResult created = create_scene_object_from_latest(
        request.name.empty() ? "Mesh" : request.name, runtime_result.message);
    if (!created.ok) {
      restore_creation_projection_checkpoint(std::move(checkpoint));
    } else if (request.material.valid()) {
      (void)mesh_sync_service_.set_entity_material(
          scene_, created.object.entity,
          MaterialComponent{.material = request.material});
      object_registry_.remember_scene_entity(created.object.modeling_object_id,
                                             scene_, created.object.entity);
    }
    return created;
  }

  ModelingCommandResult replace_mesh_document(
      int modeling_object_id, quader_poly::Document document) {
    if (document.vertices.empty() || document.faces.empty()) {
      return {
          .ok = false,
          .changed = false,
          .message = "Document mesh must contain vertices and faces.",
      };
    }
    const quader_editor_core::PolygonObject *source =
        find_object(session_, modeling_object_id);
    if (source == nullptr) {
      return {
          .ok = false,
          .changed = false,
          .message = "Modeling object does not exist.",
      };
    }

    quader_editor_core::PolygonObject projection_candidate = *source;
    projection_candidate.document = document;
    const std::optional<MeshAsset> projected_mesh =
        mesh_sync_service_.compile_object_mesh(projection_candidate);
    if (!projected_mesh.has_value()) {
      return {
          .ok = false,
          .changed = false,
          .message = "Modeling mesh projection could not be compiled.",
      };
    }
    const AssetRegistryValidationResult projection_validation =
        assets_.validate_mesh(*projected_mesh);
    if (!projection_validation.ok) {
      return {
          .ok = false,
          .changed = false,
          .message = projection_validation.message.empty()
                         ? "Modeling mesh projection is invalid."
                         : projection_validation.message,
      };
    }

    CreationProjectionCheckpoint checkpoint =
        capture_creation_projection_checkpoint();
    const quader_editor_core::CommandResult replaced =
        session_.replace_object_document(modeling_object_id,
                                         std::move(document));
    if (!replaced.ok || !replaced.changed) {
      restore_creation_projection_checkpoint(std::move(checkpoint));
      return {
          .ok = replaced.ok,
          .changed = false,
          .message = replaced.message.empty()
                         ? "Modeling mesh document could not be replaced."
                         : replaced.message,
      };
    }
    if (!try_sync_scene_objects_from_modeling(true)) {
      restore_creation_projection_checkpoint(std::move(checkpoint));
      const bool rollback_synced = try_sync_scene_objects_from_modeling(true);
      return {
          .ok = false,
          .changed = false,
          .message = rollback_synced
                         ? "Updated primitive mesh could not be synchronized."
                         : "Updated primitive mesh and its rollback could not be synchronized.",
      };
    }
    invalidate_selection_overlay_caches();
    return {
        .ok = true,
        .changed = true,
        .message = replaced.message,
    };
  }

  TransformedDuplicateBatchResult execute_transformed_duplicate_batch(
      std::span<const TransformedDuplicateSourcePlan> source_plans,
      std::string_view operation_name, std::string success_message) {
    const auto failure = [](std::string message) {
      return TransformedDuplicateBatchResult{
          .command = {
              .ok = false,
              .changed = false,
              .message = std::move(message),
          },
      };
    };
    if (source_plans.empty() ||
        source_plans.front().duplicate_local_transforms.empty()) {
      return failure(std::string(operation_name) +
                     " needs at least one transformed copy.");
    }

    const std::size_t copies_per_source =
        source_plans.front().duplicate_local_transforms.size();
    std::vector<int> source_object_ids;
    source_object_ids.reserve(source_plans.size());
    for (const TransformedDuplicateSourcePlan &source : source_plans) {
      if (source.source_entity == kInvalidEntityId ||
          source.source_modeling_object_id == kInvalidModelingObjectId ||
          source.duplicate_local_transforms.size() != copies_per_source ||
          (!source.expected_duplicate_world_transforms.empty() &&
           source.expected_duplicate_world_transforms.size() !=
               copies_per_source)) {
        return failure(std::string(operation_name) +
                       " transformed-copy plan is invalid.");
      }
      source_object_ids.push_back(source.source_modeling_object_id);
    }

    const ModelingSessionMemento rollback = capture_history_memento();
    const auto rollback_after_failure =
        [&](std::string message) -> TransformedDuplicateBatchResult {
      const ModelingCommandResult restored = restore_history_memento(rollback);
      if (!restored.ok) {
        return TransformedDuplicateBatchResult{
            .command = {
                .ok = false,
                .changed = true,
                .message =
                    std::move(message) +
                    " The pre-operation state could not be restored: " +
                    (restored.message.empty() ? "unknown rollback failure."
                                              : restored.message),
            },
        };
      }
      return failure(std::move(message));
    };
    const quader_editor_core::DuplicateObjectsResult duplicated =
        session_.duplicate_objects(
            quader_editor_core::DuplicateObjectsRequest{
                .source_object_ids = source_object_ids,
                .copies_per_source =
                    static_cast<std::uint32_t>(copies_per_source),
                .geometry_offset = {},
                .selection_policy =
                    quader_editor_core::DuplicateObjectSelectionPolicy::
                        SourcesAndDuplicates,
            });
    const std::size_t expected_duplicate_count =
        source_plans.size() * copies_per_source;
    if (!duplicated.command.ok || !duplicated.command.changed ||
        duplicated.duplicate_object_ids.size() != expected_duplicate_count) {
      if (duplicated.command.changed) {
        return rollback_after_failure(
            duplicated.command.message.empty()
                ? std::string("Modeling runtime did not create the ") +
                      std::string(operation_name) + "."
                : duplicated.command.message);
      }
      return failure(
          duplicated.command.message.empty()
              ? std::string("Modeling runtime did not create the ") +
                    std::string(operation_name) + "."
              : duplicated.command.message);
    }
    if (!try_sync_scene_objects_from_modeling(true)) {
      return rollback_after_failure(std::string(operation_name) +
                                    " could not synchronize its mesh copies.");
    }

    const auto object_record = [&](int modeling_object_id, EntityId entity) {
      ModelingObjectRecord record;
      record.modeling_object_id = modeling_object_id;
      record.entity = entity;
      if (const MeshComponent *mesh = scene_.mesh(entity)) {
        record.mesh = mesh->mesh;
      }
      if (const NameComponent *name = scene_.name(entity)) {
        record.name = name->name;
      }
      return record;
    };

    SceneEntityNameAllocator names(scene_);
    std::vector<SceneTransformAssignment> transform_assignments;
    transform_assignments.reserve(expected_duplicate_count);
    TransformedDuplicateBatchResult result{
        .command = {
            .ok = true,
            .changed = true,
            .message = std::move(success_message),
        },
    };
    result.source_elements.reserve(source_plans.size());
    for (std::size_t source_index = 0; source_index < source_plans.size();
         ++source_index) {
      const TransformedDuplicateSourcePlan &source =
          source_plans[source_index];
      std::vector<ModelingObjectRecord> elements;
      elements.reserve(copies_per_source + 1u);
      elements.push_back(object_record(source.source_modeling_object_id,
                                       source.source_entity));
      for (std::size_t copy_index = 0; copy_index < copies_per_source;
           ++copy_index) {
        const std::size_t duplicate_index =
            source_index * copies_per_source + copy_index;
        const int object_id =
            duplicated.duplicate_object_ids[duplicate_index];
        const std::optional<EntityId> entity =
            object_registry_.entity_for_modeling_object(object_id);
        if (!entity.has_value() || !scene_.exists(*entity)) {
          return rollback_after_failure(std::string(operation_name) +
                                        " could not create a scene mesh copy.");
        }

        mesh_sync_service_.apply_scene_identity_to_duplicate_entity(
            scene_, *entity, source.identity);
        const std::string base_name =
            source.identity.name.empty() ? "Mesh" : source.identity.name;
        (void)scene_.rename_entity(*entity, names.next_name(base_name));
        transform_assignments.push_back({
            .entity = *entity,
            .transform = source.duplicate_local_transforms[copy_index],
        });
        object_registry_.remember_scene_entity(object_id, scene_, *entity);
        elements.push_back(object_record(object_id, *entity));
      }
      result.source_elements.push_back(std::move(elements));
    }

    mesh_sync_service_.apply_scene_transforms(scene_, transform_assignments);
    for (std::size_t source_index = 0; source_index < source_plans.size();
         ++source_index) {
      const TransformedDuplicateSourcePlan &source =
          source_plans[source_index];
      for (std::size_t copy_index = 0; copy_index < copies_per_source;
           ++copy_index) {
        const std::size_t duplicate_index =
            source_index * copies_per_source + copy_index;
        const int object_id =
            duplicated.duplicate_object_ids[duplicate_index];
        const std::optional<EntityId> entity =
            object_registry_.entity_for_modeling_object(object_id);
        if (!entity.has_value() ||
            (!source.expected_duplicate_world_transforms.empty() &&
             !transformed_duplicate_world_matches(
                 scene_.world_transform_matrix(*entity),
                 source.expected_duplicate_world_transforms[copy_index]))) {
          return rollback_after_failure(
              std::string(operation_name) +
              " could not preserve a generated world transform.");
        }
        object_registry_.remember_scene_entity(object_id, scene_, *entity);
      }
    }

    sync_scene_selection_from_modeling();
    invalidate_selection_overlay_caches();
    return result;
  }

  ModelingLinearArrayResult
  create_linear_array(const ModelingLinearArrayRequest &request) {
    const auto finite_vector = [](QVec3 value) {
      return std::isfinite(value.x) && std::isfinite(value.y) &&
             std::isfinite(value.z);
    };
    const auto failure = [](std::string message) {
      return ModelingLinearArrayResult{
          .ok = false,
          .changed = false,
          .message = std::move(message),
      };
    };

    if (request.sources.empty()) {
      return failure("Linear Array needs at least one source mesh.");
    }
    if (request.count < kLinearArrayLayoutMinimumCount ||
        request.count > kLinearArrayLayoutMaximumCount) {
      return failure("Linear Array count must be between 2 and 128.");
    }
    if (request.sources.size() >
        static_cast<std::size_t>(kLinearArrayLayoutMaximumElements) /
            request.count) {
      return failure("Linear Array is limited to 1024 total elements.");
    }
    if (!finite_vector(request.span_world)) {
      return failure("Linear Array span must be finite.");
    }
    if (selection_mode() != ModelingSelectionMode::Object) {
      return failure("Linear Array only works in object mode.");
    }

    std::vector<int> source_ids;
    source_ids.reserve(request.sources.size());
    std::unordered_set<EntityId> source_entities;
    source_entities.reserve(request.sources.size());
    std::unordered_set<int> source_object_ids;
    source_object_ids.reserve(request.sources.size());
    for (const ModelingArraySource &source : request.sources) {
      if (source.entity == kInvalidEntityId ||
          source.modeling_object_id == kInvalidModelingObjectId ||
          !scene_.exists(source.entity)) {
        return failure("Linear Array source mesh was not found.");
      }
      if (!source_entities.insert(source.entity).second ||
          !source_object_ids.insert(source.modeling_object_id).second) {
        return failure("Linear Array source meshes must be unique.");
      }

      const ModelingObjectComponent *modeling =
          scene_.modeling_object(source.entity);
      const MeshComponent *mesh = scene_.mesh(source.entity);
      const TransformComponent *transform = scene_.transform(source.entity);
      const std::optional<EntityId> mapped_entity =
          object_registry_.entity_for_modeling_object(
              source.modeling_object_id);
      if (modeling == nullptr || mesh == nullptr || transform == nullptr ||
          modeling->object_id != source.modeling_object_id ||
          !mapped_entity.has_value() || *mapped_entity != source.entity ||
          assets_.mesh(mesh->mesh) == nullptr) {
        return failure("Linear Array source is not a live modeling mesh.");
      }
      if (!scene_.is_effectively_visible(source.entity) ||
          scene_.is_effectively_locked(source.entity)) {
        return failure("Linear Array sources must be visible and unlocked.");
      }
      source_ids.push_back(source.modeling_object_id);
    }

    const std::vector<ModelingObjectSceneIdentity> source_identities =
        object_registry_.capture_scene_identities_for_modeling_objects(
            scene_, std::span<const int>(source_ids.data(), source_ids.size()));
    if (source_identities.size() != request.sources.size()) {
      return failure("Linear Array source scene identities were not found.");
    }

    std::vector<TransformedDuplicateSourcePlan> plans;
    plans.reserve(request.sources.size());
    for (std::size_t source_index = 0;
         source_index < request.sources.size(); ++source_index) {
      const ModelingArraySource &source = request.sources[source_index];
      const ModelingObjectSceneIdentity &identity =
          source_identities[source_index];
      if (identity.entity != source.entity) {
        return failure("Linear Array source scene identity is stale.");
      }

      const TransformComponent *source_transform =
          scene_.transform(source.entity);
      if (source_transform == nullptr) {
        return failure("Linear Array source transform was not found.");
      }
      const LinearArrayLayoutResult layout = build_linear_array_layout({
          .source_world_transform =
              scene_.world_transform_matrix(source.entity),
          .span_world = request.span_world,
          .count = request.count,
      });
      if (!layout.ok ||
          layout.generated_world_transforms.size() != request.count - 1u) {
        return failure(layout.message.empty()
                           ? "Linear Array layout could not be generated."
                           : layout.message);
      }

      const EntityId parent = scene_.parent(source.entity);
      const QMat4 parent_world =
          parent == kInvalidEntityId ? scene_transform_identity_matrix()
                                     : scene_.world_transform_matrix(parent);
      TransformedDuplicateSourcePlan prepared{
          .source_entity = source.entity,
          .source_modeling_object_id = source.modeling_object_id,
          .identity = identity,
          .expected_duplicate_world_transforms =
              layout.generated_world_transforms,
      };
      prepared.duplicate_local_transforms.reserve(
          layout.generated_world_transforms.size());
      for (const QMat4 &desired_world : layout.generated_world_transforms) {
        const std::optional<TransformComponent> local_transform =
            try_scene_local_transform_preserving_world(
                parent_world, desired_world, *source_transform);
        if (!local_transform.has_value()) {
          return failure(
              "Linear Array source parent transform is not invertible.");
        }
        const QMat4 recomposed_world = scene_transform_multiply(
            parent_world,
            compose_transform(local_transform->spatial_transform()));
        if (!transformed_duplicate_world_matches(recomposed_world,
                                                 desired_world)) {
          return failure(
              "Linear Array transform cannot be represented under its "
              "source parent without shear.");
        }
        prepared.duplicate_local_transforms.push_back(*local_transform);
      }
      plans.push_back(std::move(prepared));
    }

    TransformedDuplicateBatchResult duplicated =
        execute_transformed_duplicate_batch(plans, "Linear Array",
                                            "Created linear array.");
    ModelingLinearArrayResult result{
        .ok = duplicated.command.ok,
        .changed = duplicated.command.changed,
        .message = std::move(duplicated.command.message),
    };
    result.source_arrays.reserve(duplicated.source_elements.size());
    for (std::vector<ModelingObjectRecord> &elements :
         duplicated.source_elements) {
      ModelingLinearArraySourceResult source_result;
      source_result.source = elements.front();
      source_result.elements = std::move(elements);
      result.source_arrays.push_back(std::move(source_result));
    }
    return result;
  }

  ModelingRadialArrayResult
  create_radial_array(const ModelingRadialArrayRequest &request) {
    const auto failure = [](std::string message) {
      return ModelingRadialArrayResult{
          .ok = false,
          .changed = false,
          .message = std::move(message),
      };
    };
    const auto finite_vector = [](QVec3 value) {
      return std::isfinite(value.x) && std::isfinite(value.y) &&
             std::isfinite(value.z);
    };

    if (request.sources.empty()) {
      return failure("Radial Array needs at least one source mesh.");
    }
    if (selection_mode() != ModelingSelectionMode::Object) {
      return failure("Radial Array only works in object mode.");
    }
    if (!finite_vector(request.center_world) ||
        !finite_vector(request.plane_normal_world) ||
        length_squared(request.plane_normal_world) <= 0.000000000001f) {
      return failure("Radial Array center and plane normal must be valid.");
    }
    if (!std::isfinite(request.degrees) || request.degrees < 0.0 ||
        request.degrees > 360.0) {
      return failure("Radial Array degrees must be between 0 and 360.");
    }
    if (request.copies < kRadialArrayLayoutMinimumCopies ||
        request.copies > kRadialArrayLayoutMaximumCopies) {
      return failure("Radial Array copies must be between 2 and 128.");
    }
    if (request.repeats == 0u ||
        request.repeats >
            kRadialArrayLayoutMaximumElements / request.copies) {
      return failure("Radial Array repeat count is too large.");
    }
    if (!std::isfinite(request.repeat_spacing) ||
        request.repeat_spacing < 0.0 ||
        request.repeat_spacing > kRadialArrayLayoutMaximumRepeatSpacing) {
      return failure(
          "Radial Array repeat spacing must be between 0 and 16384.");
    }

    const std::size_t elements_per_source =
        static_cast<std::size_t>(request.copies) * request.repeats;
    if (request.sources.size() >
        static_cast<std::size_t>(kRadialArrayLayoutMaximumElements) /
            elements_per_source) {
      return failure("Radial Array is limited to 1024 total elements.");
    }

    std::vector<int> source_object_ids;
    source_object_ids.reserve(request.sources.size());
    std::unordered_set<EntityId> source_entities;
    source_entities.reserve(request.sources.size());
    std::unordered_set<int> unique_source_object_ids;
    unique_source_object_ids.reserve(request.sources.size());
    for (const ModelingArraySource &source : request.sources) {
      if (source.entity == kInvalidEntityId ||
          source.modeling_object_id == kInvalidModelingObjectId ||
          !scene_.exists(source.entity)) {
        return failure("Radial Array source mesh was not found.");
      }
      if (!source_entities.insert(source.entity).second ||
          !unique_source_object_ids.insert(source.modeling_object_id).second) {
        return failure("Radial Array source meshes must be unique.");
      }

      const ModelingObjectComponent *modeling =
          scene_.modeling_object(source.entity);
      const MeshComponent *mesh = scene_.mesh(source.entity);
      const TransformComponent *transform = scene_.transform(source.entity);
      const std::optional<EntityId> mapped_entity =
          object_registry_.entity_for_modeling_object(
              source.modeling_object_id);
      if (modeling == nullptr || mesh == nullptr || transform == nullptr ||
          modeling->object_id != source.modeling_object_id ||
          !mapped_entity.has_value() || *mapped_entity != source.entity ||
          assets_.mesh(mesh->mesh) == nullptr) {
        return failure("Radial Array source is not a live modeling mesh.");
      }
      if (!scene_.is_effectively_visible(source.entity) ||
          scene_.is_effectively_locked(source.entity)) {
        return failure("Radial Array sources must be visible and unlocked.");
      }
      source_object_ids.push_back(source.modeling_object_id);
    }

    const std::vector<ModelingObjectSceneIdentity> source_identities =
        object_registry_.capture_scene_identities_for_modeling_objects(
            scene_, std::span<const int>(source_object_ids.data(),
                                         source_object_ids.size()));
    if (source_identities.size() != request.sources.size()) {
      return failure("Radial Array source scene identities were not found.");
    }

    std::vector<TransformedDuplicateSourcePlan> prepared_sources;
    prepared_sources.reserve(request.sources.size());
    for (std::size_t source_index = 0;
         source_index < request.sources.size(); ++source_index) {
      const ModelingArraySource &source = request.sources[source_index];
      const ModelingObjectSceneIdentity &identity =
          source_identities[source_index];
      if (identity.entity != source.entity) {
        return failure("Radial Array source scene identity is stale.");
      }

      const TransformComponent *source_transform =
          scene_.transform(source.entity);
      const std::optional<QVec3> source_pivot =
          resolve_entity_transform_visible_pivot(scene_, assets_,
                                                 source.entity);
      if (source_transform == nullptr || !source_pivot.has_value()) {
        return failure("Radial Array source has no valid visible pivot.");
      }
      const QMat4 source_world = scene_.world_transform_matrix(source.entity);
      const RadialArrayLayoutResult layout = build_radial_array_layout({
          .source_world_transform = source_world,
          .source_pivot_world = *source_pivot,
          .center_world = request.center_world,
          .plane_normal_world = request.plane_normal_world,
          .degrees = request.degrees,
          .copies = request.copies,
          .repeats = request.repeats,
          .repeat_spacing = request.repeat_spacing,
      });
      if (!layout.ok ||
          layout.generated_world_transforms.size() !=
              elements_per_source - 1u) {
        return failure(layout.message.empty()
                           ? "Radial Array layout could not be generated."
                           : layout.message);
      }

      const EntityId parent = scene_.parent(source.entity);
      const QMat4 parent_world =
          parent == kInvalidEntityId ? scene_transform_identity_matrix()
                                     : scene_.world_transform_matrix(parent);
      TransformedDuplicateSourcePlan prepared{
          .source_entity = source.entity,
          .source_modeling_object_id = source.modeling_object_id,
          .identity = identity,
          .expected_duplicate_world_transforms =
              layout.generated_world_transforms,
      };
      prepared.duplicate_local_transforms.reserve(
          layout.generated_world_transforms.size());
      for (const QMat4 &desired_world :
           layout.generated_world_transforms) {
        const std::optional<TransformComponent> local_transform =
            try_scene_local_transform_preserving_world(
                parent_world, desired_world, *source_transform);
        if (!local_transform.has_value()) {
          return failure(
              "Radial Array source parent transform is not invertible.");
        }
        const QMat4 recomposed_world = scene_transform_multiply(
            parent_world,
            compose_transform(local_transform->spatial_transform()));
        if (!transformed_duplicate_world_matches(recomposed_world,
                                                 desired_world)) {
          return failure(
              "Radial Array transform cannot be represented under its "
              "source parent without shear.");
        }
        prepared.duplicate_local_transforms.push_back(*local_transform);
      }
      prepared_sources.push_back(std::move(prepared));
    }

    TransformedDuplicateBatchResult duplicated =
        execute_transformed_duplicate_batch(prepared_sources, "Radial Array",
                                            "Created radial array.");
    ModelingRadialArrayResult result{
        .ok = duplicated.command.ok,
        .changed = duplicated.command.changed,
        .message = std::move(duplicated.command.message),
    };
    result.source_arrays.reserve(duplicated.source_elements.size());
    for (std::vector<ModelingObjectRecord> &elements :
         duplicated.source_elements) {
      ModelingRadialArraySourceResult source_result;
      source_result.source = elements.front();
      source_result.elements = std::move(elements);
      result.source_arrays.push_back(std::move(source_result));
    }
    return result;
  }

  ModelingCommandResult set_object_selection(std::span<const EntityId> entities,
                                             EntityId active) {
    if (!scene_objects_synced_for_current_modeling_content()) {
      sync_scene_objects_from_modeling();
    }

    const RuntimeObjectSelectionProjectionResult projection =
        selection_projection_service_
            .project_object_selection_for_scene_entities(
                session_, entities, active, object_registry_);
    if (!projection.ok) {
      return {.ok = false,
              .message = "Selection does not contain modeling objects."};
    }

    if (!projection.changed) {
      sync_scene_selection_from_modeling();
      return {.ok = true, .changed = false};
    }

    sync_scene_selection_from_modeling();
    invalidate_selection_overlay_caches();
    return {.ok = true, .changed = true};
  }

  ModelingCommandResult
  set_component_selection(const ModelingComponentSelectionRequest &request) {
    const std::optional<EntityId> requested_entity =
        object_registry_.entity_for_modeling_object(request.modeling_object_id);
    const ModelingInstanceEditScope edit_scope =
        instance_edit_scope_provider_ ? instance_edit_scope_provider_()
                                      : ModelingInstanceEditScope{};
    if (requested_entity.has_value()) {
      const EntityId definition =
          scene_.containing_instance_definition(*requested_entity);
      const bool active_edit =
          edit_scope.occurrence != kInvalidEntityId &&
          edit_scope.definition != kInvalidEntityId;
      if ((definition != kInvalidEntityId &&
           (!active_edit || definition != edit_scope.definition)) ||
          (active_edit && definition != edit_scope.definition)) {
        return {
            .ok = false,
            .changed = false,
            .message =
                "Component selection is outside the active instance scope.",
        };
      }
    }
    const bool restored_preview_base =
        restore_active_operation_preview_base_for_selection_edit();
    auto finish_after_preview_restore =
        [&](const ModelingCommandResult &result) {
          if (restored_preview_base) {
            refresh_active_operation_preview_after_selection_edit(
                restored_preview_base, result.ok && result.changed);
          }
          return result;
        };

    const ModelingCommandResult selection =
        selection_service_.set_component_selection(session_, request);
    if (restored_preview_base) {
      return finish_after_preview_restore(selection);
    }
    if (!selection.ok) {
      return selection;
    }
    sync_scene_selection_from_modeling();
    invalidate_selection_overlay_caches();
    return selection;
  }

  ModelingCommandResult move_selection(QVec3 delta) {
    const quader_editor_core::CommandResult result =
        session_.translate_selected(to_runtime(delta));
    if (result.ok) {
      if (result.changed) {
        sync_scene_objects_from_modeling(true);
      }
      sync_scene_selection_from_modeling();
      invalidate_selection_overlay_caches();
    }
    return {
        .ok = result.ok, .changed = result.changed, .message = result.message};
  }

  ModelingCommandResult transform_selection(const ModelingTransform3 &transform,
                                            QVec3 pivot) {
    const quader_editor_core::CommandResult result =
        session_.transform_selected(to_runtime(transform), to_runtime(pivot));
    if (result.ok) {
      if (result.changed) {
        sync_scene_objects_from_modeling(true);
      }
      sync_scene_selection_from_modeling();
      invalidate_selection_overlay_caches();
    }
    return {
        .ok = result.ok, .changed = result.changed, .message = result.message};
  }

  ModelingCommandResult
  assign_material_slot_to_selection(std::uint32_t material_slot) {
    const quader_editor_core::CommandResult result =
        session_.assign_material_to_selection(material_slot);
    if (result.ok) {
      if (result.changed) {
        sync_scene_objects_from_modeling(true);
      }
      sync_scene_selection_from_modeling();
      invalidate_selection_overlay_caches();
    }
    return {
        .ok = result.ok, .changed = result.changed, .message = result.message};
  }

  ModelingCommandResult
  assign_material_slot_to_face(int modeling_object_id, int face_id,
                               std::uint32_t material_slot) {
    const quader_editor_core::CommandResult result =
        session_.assign_material_to_face(
            modeling_object_id, static_cast<quader_poly::ElementId>(face_id),
            material_slot);
    if (result.ok) {
      if (result.changed) {
        sync_scene_objects_from_modeling(true);
      }
      sync_scene_selection_from_modeling();
      invalidate_selection_overlay_caches();
    }
    return {
        .ok = result.ok, .changed = result.changed, .message = result.message};
  }

  ModelingCommandResult cut_selected_meshes_by_hovered_face_plane() {
    const quader_editor_core::SelectionHoverState hover = runtime_.hover();
    if (hover.kind != quader_editor_core::SelectionHoverKind::Face ||
        hover.object_id <= 0 ||
        hover.face_id == quader_poly::kInvalidElementId) {
      return {.ok = false,
              .message =
                  "Hover a mesh face while holding Alt before cutting."};
    }

    const std::vector<int> selected_object_ids = session_.selected_object_ids();
    if (selected_object_ids.empty()) {
      return {.ok = false,
              .message =
                  "Select one or more mesh objects before cutting."};
    }
    std::unordered_set<int> object_ids_before;
    object_ids_before.reserve(session_.objects().size());
    for (const quader_editor_core::PolygonObject &object :
         session_.objects()) {
      object_ids_before.insert(object.id);
    }
    const std::unordered_set<int> selected_object_id_set(
        selected_object_ids.begin(), selected_object_ids.end());
    const std::unordered_map<int, ModelingObjectSceneIdentity>
        source_scene_identities =
            object_registry_.capture_scene_identities_for_modeling_objects(
                scene_, selected_object_id_set);

    const std::vector<quader_editor_core::PolygonObject> &picking_objects =
        cached_scene_transformed_objects_for_picking();
    const auto hovered_object =
        std::find_if(picking_objects.begin(), picking_objects.end(),
                     [&](const quader_editor_core::PolygonObject &object) {
                       return object.id == hover.object_id;
                     });
    if (hovered_object == picking_objects.end()) {
      return {.ok = false, .message = "Hovered face mesh was not found."};
    }

    const std::optional<std::array<QVec3, 3>> world_points =
        face_plane_points(hovered_object->document, hover.face_id);
    if (!world_points.has_value()) {
      return {.ok = false,
              .message = "Hovered face does not define a valid plane."};
    }

    std::vector<quader_editor_core::ObjectPlaneCutRequest> requests;
    requests.reserve(selected_object_ids.size());
    for (const int object_id : selected_object_ids) {
      const std::optional<QMat4> world_transform =
          modeling_object_world_transform(object_id);
      if (!world_transform.has_value()) {
        return {.ok = false,
                .message = "Cut target mesh world transform was not found."};
      }
      const QMat4 object_from_world = inverse(*world_transform);
      std::array<QVec3, 3> local_points{};
      for (std::size_t index = 0; index < local_points.size(); ++index) {
        local_points[index] =
            transform_point(object_from_world, (*world_points)[index]);
      }
      requests.push_back({
          .object_id = object_id,
          .request = plane_cut_request_from_points(local_points),
      });
    }

    const quader_editor_core::CommandResult result = session_.plane_cut(requests);
    if (result.ok) {
      if (result.changed) {
        sync_scene_objects_from_modeling(true);
        std::vector<int> created_object_ids;
        for (const quader_editor_core::PolygonObject &object :
             session_.objects()) {
          if (!object_ids_before.contains(object.id)) {
            created_object_ids.push_back(object.id);
          }
        }
        for (std::size_t index = 0; index < created_object_ids.size();
             ++index) {
          if (index >= selected_object_ids.size()) {
            break;
          }
          const auto source_identity =
              source_scene_identities.find(selected_object_ids[index]);
          if (source_identity == source_scene_identities.end()) {
            continue;
          }
          const std::optional<EntityId> created_entity =
              object_registry_.entity_for_modeling_object(
                  created_object_ids[index]);
          if (!created_entity.has_value() ||
              !scene_.exists(*created_entity)) {
            continue;
          }
          mesh_sync_service_.apply_scene_identity_to_duplicate_entity(
              scene_, *created_entity, source_identity->second);
          object_registry_.remember_scene_entity(created_object_ids[index],
                                                 scene_, *created_entity);
        }
        runtime_.set_hover({});
      }
      sync_scene_selection_from_modeling();
      invalidate_selection_overlay_caches();
    }
    return {
        .ok = result.ok, .changed = result.changed, .message = result.message};
  }

  ModelingCommandResult
  set_selection_hover(const ModelingDirectHoverRequest &request) {
    quader_editor_core::SelectionHoverState hover;
    hover.object_id = request.modeling_object_id;
    hover.selected_modifier_pressed = request.selected_modifier_pressed;
    hover.face_plane_hover = request.face_plane_hover;
    hover.candidate_object_id =
        request.kind == ModelingActiveSelectionKind::Object
            ? -1
            : request.modeling_object_id;
    switch (request.kind) {
    case ModelingActiveSelectionKind::Object:
      hover.kind = quader_editor_core::SelectionHoverKind::Object;
      break;
    case ModelingActiveSelectionKind::Vertex:
      hover.kind = quader_editor_core::SelectionHoverKind::Vertex;
      hover.vertex_id = static_cast<quader_poly::ElementId>(request.vertex_id);
      break;
    case ModelingActiveSelectionKind::Edge:
      hover.kind = quader_editor_core::SelectionHoverKind::Edge;
      hover.edge = quader_poly::make_edge(
          static_cast<quader_poly::ElementId>(request.edge.first),
          static_cast<quader_poly::ElementId>(request.edge.second));
      break;
    case ModelingActiveSelectionKind::Face:
      hover.kind = quader_editor_core::SelectionHoverKind::Face;
      hover.face_id = static_cast<quader_poly::ElementId>(request.face_id);
      break;
    case ModelingActiveSelectionKind::None:
    case ModelingActiveSelectionKind::Light:
      hover = {};
      break;
    }

    const bool changed =
        !SelectionOverlayCache::hover_matches(runtime_.hover(), hover);
    runtime_.apply_hover_update(quader_editor_core::SelectionHoverUpdateResult{
        .changed = changed,
        .hover = hover,
    });
    if (changed) {
      invalidate_selection_overlay_caches();
    }
    return {.ok = true, .changed = changed};
  }

private:
  void invalidate_selection_overlay_caches() const override {
    overlay_service_.invalidate();
  }

  [[nodiscard]] bool scene_objects_synced_for_current_modeling_content() const {
    return scene_sync_service_
        .scene_objects_synced_for_current_modeling_content(session_, scene_,
                                                           assets_);
  }

  ModelingCommandResult update_transform_preview_result(
      const quader_editor_core::CommandResult &result) {
    if (result.ok) {
      invalidate_selection_overlay_caches();
      sync_scene_objects_from_modeling(true);
      sync_scene_selection_from_modeling();
    }
    return {
        .ok = result.ok,
        .changed = result.changed,
        .message = result.message,
    };
  }

  [[nodiscard]] bool has_transform_preview() const {
    return transform_preview_service_.has_transform_preview(session_);
  }

  [[nodiscard]] ModelingTransformDragServiceContext
  transform_drag_context(DocumentCommandService *document_commands = nullptr,
                         EditorHistoryService *history = nullptr) {
    return {
        .scene = scene_,
        .assets = assets_,
        .session = session_,
        .runtime = runtime_,
        .history_bridge = history_bridge_,
        .port = *this,
        .document_commands = document_commands,
        .history = history,
    };
  }

  [[nodiscard]] bool has_scene_transform_selection() const override {
    for (const EntityId entity : scene_.selected_entities()) {
      if (scene_.transform(entity) != nullptr &&
          !scene_.is_effectively_locked(entity)) {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] QMat4
  scene_selection_display_from_scene() const override {
    const ModelingInstanceEditScope edit_scope =
        instance_edit_scope_provider_ ? instance_edit_scope_provider_()
                                      : ModelingInstanceEditScope{};
    const InstanceOccurrenceComponent *occurrence =
        scene_.instance_occurrence(edit_scope.occurrence);
    const std::optional<QMat4> definition_inverse =
        occurrence != nullptr &&
                occurrence->definition == edit_scope.definition
            ? try_scene_matrix_inverse(
                  scene_.world_transform_matrix(edit_scope.definition))
            : std::nullopt;
    if (!definition_inverse.has_value()) {
      return scene_transform_identity_matrix();
    }
    return scene_transform_multiply(
        scene_.world_transform_matrix(edit_scope.occurrence),
        *definition_inverse);
  }

  [[nodiscard]] std::optional<QMat4>
  modeling_object_world_transform(int modeling_object_id) const override {
    const std::optional<EntityId> entity =
        object_registry_.entity_for_modeling_object(modeling_object_id);
    if (!entity.has_value() || !scene_.exists(*entity)) {
      return std::nullopt;
    }
    const QMat4 scene_world = scene_.world_transform_matrix(*entity);
    const ModelingInstanceEditScope edit_scope =
        instance_edit_scope_provider_ ? instance_edit_scope_provider_()
                                      : ModelingInstanceEditScope{};
    if (scene_.containing_instance_definition(*entity) !=
        edit_scope.definition) {
      return scene_world;
    }
    return scene_transform_multiply(scene_selection_display_from_scene(),
                                    scene_world);
  }

  [[nodiscard]] quader_editor_core::Bounds3
  transform_drag_bounds() const override {
    const auto to_runtime_bounds =
        [](const SceneBounds &scene_bounds) -> quader_editor_core::Bounds3 {
      return {
          .empty = false,
          .min = scene_bounds.min,
          .max = scene_bounds.max,
      };
    };

    if (const std::optional<SceneBounds> scene_bounds = selection_bounds()) {
      return to_runtime_bounds(*scene_bounds);
    }

    quader_editor_core::Bounds3 bounds = session_.selection_bounds();
    if (!bounds.empty) {
      return bounds;
    }
    if (const std::optional<SceneBounds> scene_bounds =
            selected_transform_world_bounds(scene_, assets_)) {
      return to_runtime_bounds(*scene_bounds);
    }
    return bounds;
  }

  [[nodiscard]] bool
  try_sync_scene_objects_from_modeling(bool force = false) {
    QDR_PROFILE_FUNCTION();
    return mesh_sync_service_.sync_scene_objects_from_modeling(
        session_, scene_, assets_, scene_sync_service_, force);
  }

  void sync_scene_objects_from_modeling(bool force = false) override {
    (void)try_sync_scene_objects_from_modeling(force);
  }

  [[nodiscard]] CreationProjectionCheckpoint
  capture_creation_projection_checkpoint() const {
    return {
        .session = session_,
        .runtime = runtime_.state(),
    };
  }

  void restore_creation_projection_checkpoint(
      CreationProjectionCheckpoint checkpoint) {
    session_ = std::move(checkpoint.session);
    runtime_.set_state(std::move(checkpoint.runtime));
    scene_sync_service_.invalidate_scene_objects();
    invalidate_selection_overlay_caches();
  }

  [[nodiscard]] const std::vector<quader_editor_core::PolygonObject> &
  cached_scene_transformed_objects_for_picking() const override {
    QDR_PROFILE_FUNCTION();
    return selection_service_.cached_scene_transformed_objects_for_picking(
        session_, scene_, object_registry_);
  }

  [[nodiscard]] std::vector<quader_editor_core::PolygonObject>
  scene_transformed_objects_for_picking() const {
    QDR_PROFILE_FUNCTION();
    return selection_service_.scene_transformed_objects_for_picking(
        session_, scene_, object_registry_);
  }

  [[nodiscard]] const std::vector<quader_editor_core::PickObjectBounds> &
  cached_pick_object_bounds() const override {
    QDR_PROFILE_FUNCTION();
    return selection_service_.cached_pick_object_bounds(session_, scene_,
                                                        object_registry_);
  }

  [[nodiscard]] const std::vector<quader_editor_core::PickObjectFaceGeometry> &
  cached_pick_face_geometry(
      quader_poly::PickOptions pick_options = {}) const override {
    QDR_PROFILE_FUNCTION();
    return selection_service_.cached_pick_face_geometry(
        session_, scene_, object_registry_, pick_options);
  }

  ModelingCommandResult execute_runtime_no_settings(
      quader_editor_core::EditorRuntimeCommandId command_id,
      bool may_change_content) {
    return execute_runtime_command(
        RuntimeToolCommandService::no_settings(command_id), may_change_content);
  }

  ModelingCreateBoxResult
  create_scene_object_from_latest(const std::string &name,
                                  const std::string &message) {
    ModelingCreateBoxResult created =
        mesh_sync_service_.create_scene_object_from_latest(
        session_, scene_, assets_, scene_sync_service_, name, message);
    const ModelingInstanceEditScope edit_scope =
        instance_edit_scope_provider_ ? instance_edit_scope_provider_()
                                      : ModelingInstanceEditScope{};
    if (!created.ok || !created.changed ||
        edit_scope.definition == kInvalidEntityId) {
      return created;
    }
    const auto cleanup_failed_instance_member = [&]() {
      mesh_sync_service_.rollback_created_scene_object(
          scene_, assets_, scene_sync_service_,
          created.object.modeling_object_id, created.object.entity,
          created.object.mesh);
      return ModelingCreateBoxResult{
          .ok = false,
          .changed = false,
          .message =
              "Created mesh could not be added to the active instance.",
      };
    };
    const InstanceOccurrenceComponent *occurrence =
        scene_.instance_occurrence(edit_scope.occurrence);
    const TransformComponent *created_transform =
        scene_.transform(created.object.entity);
    const std::optional<QMat4> occurrence_inverse =
        occurrence != nullptr &&
                occurrence->definition == edit_scope.definition
            ? try_scene_matrix_inverse(
                  scene_.world_transform_matrix(edit_scope.occurrence))
            : std::nullopt;
    if (!scene_.is_instance_definition_entity(edit_scope.definition) ||
        created_transform == nullptr || !occurrence_inverse.has_value()) {
      return cleanup_failed_instance_member();
    }
    const QMat4 definition_world =
        scene_.world_transform_matrix(edit_scope.definition);
    const QMat4 definition_local_world = scene_transform_multiply(
        *occurrence_inverse,
        scene_.world_transform_matrix(created.object.entity));
    const std::optional<TransformComponent> member_local =
        try_scene_local_transform_preserving_world(
            definition_world,
            scene_transform_multiply(definition_world,
                                     definition_local_world),
            *created_transform);
    if (!member_local.has_value() ||
        !mesh_sync_service_.place_created_scene_object(
            scene_, created.object.entity, edit_scope.definition,
            *member_local)) {
      return cleanup_failed_instance_member();
    }
    object_registry_.remember_scene_entity(created.object.modeling_object_id,
                                           scene_, created.object.entity);
    return created;
  }

  [[nodiscard]] ModelingBoxPreview
  current_box_preview(bool snap_enabled) const {
    const quader_editor_core::BoxPreviewDescriptor preview =
        runtime_.box_preview_descriptor(snap_enabled);
    if (preview.drag_state == quader_editor_core::BoxDragState::Idle &&
        from_runtime_viewport_tool(runtime_.active_tool()) !=
            ModelingViewportTool::Box) {
      return {
          .ok = preview.ok,
          .message = preview.message,
      };
    }
    std::vector<quader::QVec3> corners;
    if (preview.drag_state == quader_editor_core::BoxDragState::Height) {
      corners = quader_editor_core::box_preview_corners(preview);
    }
    return from_runtime(preview, corners);
  }

  ModelingBoxPreview execute_box_preview_command(
      const quader_editor_core::EditorRuntimeCommand &command,
      bool snap_enabled) {
    const quader_editor_core::EditorRuntimeCommandResult runtime_result =
        quader_editor_core::execute_runtime_command(session_, runtime_,
                                                    command);
    if (!runtime_result.command.ok) {
      return {
          .ok = false,
          .message = runtime_result.command.message,
      };
    }

    ModelingBoxPreview result = current_box_preview(snap_enabled);
    result.ok = runtime_result.command.ok;
    if (!runtime_result.command.message.empty()) {
      result.message = runtime_result.command.message;
    }
    return result;
  }

  SceneDocument &scene_;
  AssetRegistry &assets_;
  ModelingObjectRuntimeMap object_registry_;
  SceneModelingSyncService scene_sync_service_;
  ModelingMeshSyncService mesh_sync_service_;
  ModelingHierarchyDuplicateService hierarchy_duplicate_service_;
  ModelingSelectionService selection_service_;
  SelectionProjectionService selection_projection_service_;
  mutable ModelingOverlayService overlay_service_;
  ModelingCommandExecutor command_executor_;
  ModelingContextActionService context_action_service_;
  RuntimeOperationExecutor runtime_operation_executor_;
  TransformPreviewService transform_preview_service_;
  ModelingInteractionSettingsService interaction_settings_service_;
  ModelingToolSettingsService tool_settings_service_;
  ModelingTransformDragService transform_drag_service_;
  ModalToolRuntime modal_tool_runtime_;
  ModelingClipboardService clipboard_service_;
  std::vector<ModelingObjectSceneIdentity> clipboard_scene_identities_;
  std::optional<QVec3> clipboard_world_center_;
  ModelingHistoryBridge history_bridge_;
  std::optional<SceneWorkspacePreviewState> scene_workspace_preview_;
  ModelingOperationPreviewCheckpointHooks preview_checkpoint_hooks_;
  std::function<ModelingInstanceEditScope()> instance_edit_scope_provider_;
  ModelingSelectionMode app_selection_mode_ = ModelingSelectionMode::Object;

  quader_editor_core::EditorSession session_;
  quader_editor_core::EditorRuntime runtime_;
};

ModelingSessionService::ModelingSessionService(
    SceneDocument &scene, AssetRegistry &assets,
    ModelingOperationPreviewCheckpointHooks preview_checkpoint_hooks)
    : impl_(std::make_unique<Impl>(scene, assets,
                                  std::move(preview_checkpoint_hooks))) {}

ModelingSessionService::~ModelingSessionService() = default;
ModelingSessionService::ModelingSessionService(
    ModelingSessionService &&) noexcept = default;
ModelingSessionService &
ModelingSessionService::operator=(ModelingSessionService &&) noexcept = default;

ModelingCommandDescriptor ModelingSessionService::box_tool_command() const {
  return impl_->box_tool_command();
}

ModelingCommandDescriptor
ModelingSessionService::create_box_from_bounds_command() const {
  return impl_->create_box_from_bounds_command();
}

std::vector<ModelingObjectRecord> ModelingSessionService::objects() const {
  return impl_->objects();
}

std::optional<EntityId> ModelingSessionService::entity_for_modeling_object(
    int modeling_object_id) const {
  return impl_->entity_for_modeling_object(modeling_object_id);
}

std::optional<int>
ModelingSessionService::modeling_object_for_entity(EntityId entity) const {
  return impl_->modeling_object_for_entity(entity);
}

std::uint64_t ModelingSessionService::modeling_object_map_revision() const {
  return impl_->modeling_object_map_revision();
}

std::optional<MeshAsset>
ModelingSessionService::compile_object_mesh(int modeling_object_id) const {
  return impl_->compile_object_mesh(modeling_object_id);
}

std::optional<EntityId> ModelingSessionService::pick_object_entity(
    const ModelingPickSelectRequest &request) const {
  return impl_->pick_object_entity(request);
}

std::vector<EntityId> ModelingSessionService::box_pick_object_entities(
    const ModelingBoxSelectRequest &request) const {
  return impl_->box_pick_object_entities(request);
}

std::optional<QVec3> ModelingSessionService::selection_center() const {
  return impl_->selection_center();
}

std::optional<SceneBounds> ModelingSessionService::selection_bounds() const {
  return impl_->selection_bounds();
}

ModelingSelectionMode ModelingSessionService::selection_mode() const {
  return impl_->selection_mode();
}

ModelingSelectionSummary ModelingSessionService::selection_summary() const {
  return impl_->selection_summary();
}

ModelingSelectionExport ModelingSessionService::export_selection() const {
  return impl_->export_selection();
}

ModelingSelectionBoxBaseline
ModelingSessionService::capture_box_selection_baseline() const {
  return impl_->capture_box_selection_baseline();
}

std::uint64_t ModelingSessionService::selection_overlay_signature(
    ModelingSelectionOverlayOptions options) const {
  return impl_->selection_overlay_signature(options);
}

ViewportOverlayFrame ModelingSessionService::selection_overlay_frame(
    ModelingSelectionOverlayOptions options) const {
  return impl_->selection_overlay_frame(options);
}

std::vector<ModelingToolPreviewMesh>
ModelingSessionService::tool_preview_meshes() const {
  return impl_->tool_preview_meshes();
}

std::uint64_t
ModelingSessionService::transformed_picking_projection_build_count() const {
  return impl_->transformed_picking_projection_build_count();
}

std::uint64_t
ModelingSessionService::rectangle_picking_projection_build_count() const {
  return impl_->rectangle_picking_projection_build_count();
}

std::uint64_t
ModelingSessionService::rectangle_selection_resolution_build_count() const {
  return impl_->rectangle_selection_resolution_build_count();
}

std::uint64_t ModelingSessionService::local_face_geometry_build_count() const {
  return impl_->local_face_geometry_build_count();
}

ModelingSessionMemento ModelingSessionService::capture_history_memento() const {
  return impl_->capture_history_memento();
}

ModelingSessionMemento
ModelingSessionService::capture_history_memento_without_objects(
    const ModelingSessionMemento &selection_baseline,
    std::span<const int> excluded_object_ids) const {
  if (!selection_baseline.valid()) {
    return {};
  }
  return impl_->capture_history_memento_without_objects(
      selection_baseline.impl_->state, excluded_object_ids);
}

ModelingSessionMemento
ModelingSessionService::capture_active_preview_history_memento() const {
  return impl_->capture_active_preview_history_memento();
}

ModelingCommandResult ModelingSessionService::restore_history_memento(
    const ModelingSessionMemento &memento) {
  return impl_->restore_history_memento(memento);
}

ModelingCommandResult
ModelingSessionService::restore_history_memento_preserving_scene_selection(
    const ModelingSessionMemento &memento) {
  return impl_->restore_history_memento_preserving_scene_selection(memento);
}

void ModelingSessionService::clear_transient_state_for_document_replacement() {
  impl_->clear_transient_state_for_document_replacement();
}

void ModelingSessionService::reset_from_scene() { impl_->reset_from_scene(); }

void ModelingSessionService::set_instance_edit_scope_provider(
    std::function<ModelingInstanceEditScope()> provider) {
  impl_->set_instance_edit_scope_provider(std::move(provider));
}

void ModelingSessionService::project_object_selection_from_scene(
    bool force_object_mode) {
  impl_->sync_object_selection_from_scene(force_object_mode);
}

ModelingCommandResult
ModelingSessionService::duplicate_selected_hierarchy_for_group_drag(
    DocumentCommandService &document_commands) {
  return impl_->duplicate_selected_hierarchy_for_group_drag(document_commands);
}

ModelingCommandResult
ModelingSessionService::duplicate_selected_entity_hierarchy(
    DocumentCommandService &document_commands) {
  return impl_->duplicate_selected_entity_hierarchy(document_commands);
}

ModelingCommandResult
ModelingSessionService::set_selection_mode(ModelingSelectionMode mode) {
  return impl_->set_selection_mode(mode);
}

ModelingCommandResult ModelingSessionService::select_all() {
  return impl_->select_all();
}

ModelingCommandResult ModelingSessionService::invert_selection() {
  return impl_->invert_selection();
}

ModelingCommandResult ModelingSessionService::clear_selection() {
  return impl_->clear_selection();
}

std::unique_ptr<IUndoableCommand>
ModelingSessionService::create_delete_selection_command() {
  return impl_->create_delete_selection_command();
}

bool ModelingSessionService::has_selected_scene_aware_delete_root() const {
  return impl_->has_selected_scene_aware_delete_root();
}

ModelingCommandResult ModelingSessionService::delete_component_selection() {
  return impl_->delete_component_selection();
}

ModelingCommandResult ModelingSessionService::copy_selection() {
  return impl_->copy_selection();
}

ModelingCommandResult ModelingSessionService::paste_selection() {
  return impl_->paste_selection();
}

ModelingCommandResult ModelingSessionService::repeat_last_action() {
  return impl_->repeat_last_action();
}

ModelingCommandResult
ModelingSessionService::mirror_selection(ModelingMirrorAxis axis) {
  return impl_->mirror_selection(axis);
}

ModelingCommandResult
ModelingSessionService::flip_selection(ModelingFlipOrientation orientation) {
  return impl_->flip_selection(orientation);
}

ModelingCommandResult
ModelingSessionService::apply_active_material(std::uint32_t material_slot) {
  return impl_->apply_active_material(material_slot);
}

ModelingCommandResult
ModelingSessionService::apply_active_material(MaterialReference material) {
  return impl_->apply_active_material(material);
}

bool ModelingSessionService::modal_tool_owns_viewport() const {
  return impl_->modal_tool_owns_viewport();
}

bool ModelingSessionService::transform_drag_active() const {
  return impl_->transform_drag_active();
}

bool ModelingSessionService::can_cancel_active_tool_or_preview() const {
  return impl_->can_cancel_active_tool_or_preview();
}

bool ModelingSessionService::can_cancel_active_preview_for_undo() const {
  return impl_->can_cancel_active_preview_for_undo();
}

ModelingCommandResult
ModelingSessionService::cancel_competing_state_for_native_tool_activation() {
  return impl_->cancel_competing_state_for_native_tool_activation();
}

ModelingToolStatus ModelingSessionService::tool_status() const {
  return impl_->tool_status();
}

ModelingLiveStatus ModelingSessionService::live_status() const {
  return impl_->live_status();
}

ModelingToolSettingsTarget
ModelingSessionService::tool_settings_target() const {
  return impl_->tool_settings_target();
}

ModelingInteractionSettings
ModelingSessionService::interaction_settings() const {
  return impl_->interaction_settings();
}

ModelingCommandResult ModelingSessionService::set_interaction_settings(
    const ModelingInteractionSettings &settings) {
  return impl_->set_interaction_settings(settings);
}

ModelingActionSet ModelingSessionService::context_action_set(
    const ModelingActionQuery &query) const {
  return impl_->context_action_set(query);
}

std::vector<ModelingActionItem> ModelingSessionService::context_actions(
    const ModelingActionQuery &query) const {
  return impl_->context_actions(query);
}

std::optional<ModelingActionItem>
ModelingSessionService::context_action(std::string_view action_id,
                                       const ModelingActionQuery &query) const {
  return impl_->context_action(action_id, query);
}

ModelingCommandResult
ModelingSessionService::execute_context_action(std::string_view action_id) {
  return impl_->execute_context_action(action_id);
}

ModelingCommandResult
ModelingSessionService::commit_active_operation_preview() {
  return impl_->commit_active_operation_preview();
}

ModelingCommandResult
ModelingSessionService::cancel_active_operation_preview() {
  return impl_->cancel_active_operation_preview();
}

std::vector<ModelingInstantOperationDescriptor>
ModelingSessionService::instant_operations() const {
  return impl_->instant_operations();
}

ModelingCommandResult ModelingSessionService::execute_instant_operation(
    ModelingInstantOperationId operation) {
  return impl_->execute_instant_operation(operation);
}

ModelingCommandResult
ModelingSessionService::begin_transform_preview(bool duplicate_selected) {
  return impl_->begin_transform_preview(duplicate_selected);
}

ModelingCommandResult
ModelingSessionService::update_transform_translate_preview(QVec3 delta) {
  return impl_->update_transform_translate_preview(delta);
}

ModelingCommandResult ModelingSessionService::update_transform_matrix_preview(
    const ModelingTransform3 &transform, QVec3 pivot) {
  return impl_->update_transform_matrix_preview(transform, pivot);
}

ModelingCommandResult ModelingSessionService::update_transform_extrude_preview(
    QVec3 offset, float closed_edge_ledge_size) {
  return impl_->update_transform_extrude_preview(offset,
                                                 closed_edge_ledge_size);
}

ModelingCommandResult ModelingSessionService::update_transform_inset_preview(
    const ModelingTransform3 &transform, QVec3 pivot) {
  return impl_->update_transform_inset_preview(transform, pivot);
}

ModelingCommandResult ModelingSessionService::commit_transform_preview() {
  return impl_->commit_transform_preview();
}

ModelingCommandResult ModelingSessionService::cancel_transform_preview() {
  return impl_->cancel_transform_preview();
}

ModelingCommandResult ModelingSessionService::record_repeatable_transform(
    const ModelingRepeatTransform &transform) {
  return impl_->record_repeatable_transform(transform);
}

ModelingTransformDragPreview ModelingSessionService::begin_transform_drag(
    const ModelingTransformDragBeginRequest &request) {
  QDR_PROFILE_FUNCTION();
  return impl_->begin_transform_drag(request);
}

ModelingTransformDragPreview ModelingSessionService::begin_transform_drag(
    const ModelingTransformDragBeginRequest &request,
    DocumentCommandService &document_commands, EditorHistoryService &history) {
  QDR_PROFILE_FUNCTION();
  return impl_->begin_transform_drag(request, &document_commands, &history);
}

ModelingTransformDragPreview ModelingSessionService::update_transform_drag(
    const ModelingTransformDragUpdateRequest &request) {
  QDR_PROFILE_FUNCTION();
  return impl_->update_transform_drag(request);
}

ModelingTransformDragPreview ModelingSessionService::commit_transform_drag() {
  QDR_PROFILE_FUNCTION();
  return impl_->commit_transform_drag();
}

ModelingTransformDragPreview
ModelingSessionService::commit_transform_drag(EditorHistoryService &history) {
  QDR_PROFILE_FUNCTION();
  return impl_->commit_transform_drag(&history);
}

ModelingCommandResult ModelingSessionService::cancel_transform_drag() {
  QDR_PROFILE_FUNCTION();
  return impl_->cancel_transform_drag();
}

ModelingSelectionSummary
ModelingSessionService::pick_select(const ModelingPickSelectRequest &request) {
  return impl_->pick_select(request);
}

ModelingSelectionSummary
ModelingSessionService::box_select(const ModelingBoxSelectRequest &request) {
  return impl_->box_select(request);
}

ModelingSelectionSummary ModelingSessionService::box_select(
    const ModelingBoxSelectRequest &request,
    const ModelingSelectionBoxBaseline &baseline) {
  return baseline.impl_ ? impl_->box_select(request, baseline.impl_->selection)
                        : impl_->box_select(request);
}

ModelingCommandResult ModelingSessionService::update_selection_hover(
    const ModelingHoverRequest &request) {
  return impl_->update_selection_hover(request);
}

ModelingHoverSummary ModelingSessionService::selection_hover_summary() const {
  return impl_->selection_hover_summary();
}

ModelingHoverSummary ModelingSessionService::preview_selection_hover(
    const ModelingHoverRequest &request) const {
  return impl_->preview_selection_hover(request);
}

ModelingCommandResult ModelingSessionService::clear_selection_hover() {
  return impl_->clear_selection_hover();
}

ModelingCommandResult
ModelingSessionService::cut_selected_meshes_by_hovered_face_plane() {
  return impl_->cut_selected_meshes_by_hovered_face_plane();
}

ModelingCommandResult
ModelingSessionService::activate_viewport_tool(ModelingViewportTool tool) {
  return impl_->activate_viewport_tool(tool);
}

ModelingCommandResult ModelingSessionService::cancel_active_modal_tool() {
  return impl_->cancel_active_modal_tool();
}

ModelingCommandResult ModelingSessionService::update_insert_edge_loop_hover(
    const ModelingToolRayEvent &event) {
  return impl_->update_insert_edge_loop_hover(event);
}

ModelingCommandResult ModelingSessionService::begin_insert_edge_loop_drag(
    const ModelingToolRayEvent &event) {
  return impl_->begin_insert_edge_loop_drag(event);
}

ModelingCommandResult ModelingSessionService::update_insert_edge_loop_drag(
    const ModelingToolRayEvent &event) {
  return impl_->update_insert_edge_loop_drag(event);
}

ModelingCommandResult ModelingSessionService::commit_insert_edge_loop_drag() {
  return impl_->commit_insert_edge_loop_drag();
}

ModelingCommandResult
ModelingSessionService::update_knife_hover(const ModelingToolRayEvent &event) {
  return impl_->update_knife_hover(event);
}

ModelingCommandResult ModelingSessionService::clear_knife_hover() {
  return impl_->clear_knife_hover();
}

ModelingCommandResult
ModelingSessionService::accept_knife_point(const ModelingToolRayEvent &event) {
  return impl_->accept_knife_point(event);
}

ModelingCommandResult ModelingSessionService::commit_knife_tool() {
  return impl_->commit_knife_tool();
}

ModelingCommandResult
ModelingSessionService::update_cut_hover(const ModelingToolRayEvent &event) {
  return impl_->update_cut_hover(event);
}

ModelingCommandResult
ModelingSessionService::click_cut(const ModelingToolRayEvent &event) {
  return impl_->click_cut(event);
}

ModelingCommandResult
ModelingSessionService::begin_cut_point_drag(int point_index) {
  return impl_->begin_cut_point_drag(point_index);
}

ModelingCommandResult
ModelingSessionService::begin_cut_line_drag(const ModelingToolRayEvent &event) {
  return impl_->begin_cut_line_drag(event);
}

ModelingCommandResult ModelingSessionService::end_cut_drag() {
  return impl_->end_cut_drag();
}

ModelingCommandResult
ModelingSessionService::set_cut_mode(ModelingCutMode mode) {
  return impl_->set_cut_mode(mode);
}

ModelingCommandResult ModelingSessionService::cycle_cut_mode() {
  return impl_->cycle_cut_mode();
}

ModelingCommandResult ModelingSessionService::finalize_cut_tool() {
  return impl_->finalize_cut_tool();
}

ModelingCommandResult
ModelingSessionService::update_mirror_hover(const ModelingToolRayEvent &event) {
  return impl_->update_mirror_hover(event);
}

ModelingCommandResult
ModelingSessionService::click_mirror(const ModelingToolRayEvent &event) {
  return impl_->click_mirror(event);
}

ModelingCommandResult
ModelingSessionService::begin_mirror_point_drag(int point_index) {
  return impl_->begin_mirror_point_drag(point_index);
}

ModelingCommandResult ModelingSessionService::begin_mirror_line_drag(
    const ModelingToolRayEvent &event) {
  return impl_->begin_mirror_line_drag(event);
}

ModelingCommandResult ModelingSessionService::end_mirror_drag() {
  return impl_->end_mirror_drag();
}

ModelingCommandResult ModelingSessionService::finalize_mirror_tool() {
  return impl_->finalize_mirror_tool();
}

ModelingCommandResult
ModelingSessionService::update_poly_hover(const ModelingToolRayEvent &event) {
  return impl_->update_poly_hover(event);
}

ModelingCommandResult ModelingSessionService::accept_poly_point() {
  return impl_->accept_poly_point();
}

ModelingCommandResult ModelingSessionService::commit_poly_tool() {
  return impl_->commit_poly_tool();
}

ModelingCommandResult
ModelingSessionService::commit_poly_tool(MaterialReference material) {
  return impl_->commit_poly_tool(material);
}

ModelingCommandResult ModelingSessionService::activate_box_tool() {
  return impl_->activate_box_tool();
}

ModelingCreateBoxResult ModelingSessionService::create_box_from_bounds(
    const ModelingCreateBoxRequest &request) {
  return impl_->create_box_from_bounds(request);
}

ModelingBoxPreview
ModelingSessionService::update_box_hover(const ModelingBoxToolEvent &event) {
  return impl_->update_box_hover(event);
}

ModelingBoxPreview
ModelingSessionService::begin_box_footprint(const ModelingBoxToolEvent &event) {
  return impl_->begin_box_footprint(event);
}

ModelingBoxPreview ModelingSessionService::update_box_footprint(
    const ModelingBoxToolEvent &event) {
  return impl_->update_box_footprint(event);
}

ModelingBoxPreview
ModelingSessionService::begin_box_height(const ModelingBoxToolEvent &event) {
  return impl_->begin_box_height(event);
}

ModelingBoxPreview
ModelingSessionService::update_box_height(const ModelingBoxToolEvent &event) {
  return impl_->update_box_height(event);
}

ModelingBoxPreview
ModelingSessionService::box_preview(bool snap_enabled) const {
  return impl_->box_preview(snap_enabled);
}

ModelingCreateBoxResult ModelingSessionService::commit_box(std::string name) {
  return impl_->commit_box(std::move(name));
}

ModelingCommandResult ModelingSessionService::cancel_box() {
  return impl_->cancel_box();
}

ModelingCreateBoxResult
ModelingSessionService::create_mesh(const ModelingCreateMeshRequest &request) {
  return impl_->create_mesh(request);
}

ModelingCommandResult ModelingSessionService::replace_mesh_document(
    int modeling_object_id, quader_poly::Document document) {
  return impl_->replace_mesh_document(modeling_object_id,
                                      std::move(document));
}

ModelingLinearArrayResult ModelingSessionService::create_linear_array(
    const ModelingLinearArrayRequest &request) {
  return impl_->create_linear_array(request);
}

ModelingRadialArrayResult ModelingSessionService::create_radial_array(
    const ModelingRadialArrayRequest &request) {
  return impl_->create_radial_array(request);
}

ModelingCommandResult
ModelingSessionService::set_object_selection(std::span<const EntityId> entities,
                                             EntityId active) {
  return impl_->set_object_selection(entities, active);
}

ModelingCommandResult ModelingSessionService::set_component_selection(
    const ModelingComponentSelectionRequest &request) {
  return impl_->set_component_selection(request);
}

ModelingCommandResult ModelingSessionService::move_selection(QVec3 delta) {
  return impl_->move_selection(delta);
}

ModelingCommandResult
ModelingSessionService::transform_selection(const ModelingTransform3 &transform,
                                            QVec3 pivot) {
  return impl_->transform_selection(transform, pivot);
}

ModelingCommandResult ModelingSessionService::assign_material_slot_to_selection(
    std::uint32_t material_slot) {
  return impl_->assign_material_slot_to_selection(material_slot);
}

ModelingCommandResult ModelingSessionService::assign_material_slot_to_face(
    int modeling_object_id, int face_id, std::uint32_t material_slot) {
  return impl_->assign_material_slot_to_face(modeling_object_id, face_id,
                                             material_slot);
}

ModelingCommandResult ModelingSessionService::set_selection_hover(
    const ModelingDirectHoverRequest &request) {
  return impl_->set_selection_hover(request);
}

} // namespace quader

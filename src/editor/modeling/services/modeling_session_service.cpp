////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/services/face_texture_edit_context.hpp"
#include "editor/modeling/services/uv_peel_edit_context.hpp"

#include "editor/modeling/modeling_viewport_camera_adapter.hpp"

#include "assets/asset_registry.hpp"
#include "assets/mesh_asset_payload.hpp"
#include "core/i_undoable_command.hpp"
#include "diagnostics/profile.hpp"
#include "editor/commands/document_command_service.hpp"
#include "editor/commands/document/document_command_scene_helpers.hpp"
#include "editor/commands/modeling/scene_aware_delete_selection_command.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/modeling/linear_array_layout.hpp"
#include "editor/modeling/radial_array_layout.hpp"
#include "editor/modeling/services/modeling_hierarchy_duplicate_service.hpp"
#include "editor/modeling/services/modeling_instant_operation_catalog.hpp"
#include "editor/modeling/services/modeling_operation_settings.hpp"
#include "editor/modeling/services/modeling_sdk_projection_adapter.hpp"
#include "editor/modeling/services/modeling_session_services.hpp"
#include "editor/modeling/services/modeling_transform_drag_service.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "editor/modeling/picking/curve_picking_cache.hpp"
#include "editor/selection/selection_projection_service.hpp"
#include "editor/tools/viewport_tool_descriptor_catalog.hpp"
#include "editor/transform/scene_transform_drag_service.hpp"
#include "editor/commands/document/scene_texture_mapping_plan.hpp"
#include "math/glm_bridge.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_entity_naming.hpp"
#include "scene/scene_hierarchy_visibility.hpp"
#include "scene/scene_material_surface.hpp"
#include "scene/scene_transform.hpp"
#include "scene/scene_transform_pivot.hpp"
#include "scene/scene_texture_mapping.hpp"

#include <algorithm>
#include <array>
#include <bit>
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
#include <editor/interactions/move_tool.hpp>
#include <editor/interactions/rotate_tool.hpp>
#include <editor/interactions/tool_math_internal.hpp>
#include <editor/interactions/tool_transform_math.hpp>
#include <editor/interactions/selection_box.hpp>
#include <editor/interactions/selection_edit.hpp>
#include <editor/interactions/tool_snapping.hpp>
#include <editor/interactions/viewport_projection.hpp>
#include <editor/metadata/host_contract.hpp>
#include <editor/modeling/operations/operation_command.hpp>
#include <editor/modeling/runtime/component_orientation.hpp>
#include <editor/modeling/runtime/repeat_last_action.hpp>
#include <editor/modeling/runtime/runtime_command.hpp>
#include <editor/modeling/runtime/session.hpp>
#include <quader/modeling/curves/curve_tube.hpp>
#include <quader/modeling/neutral/geometry/geometry.hpp>
#include <quader/modeling/neutral/polygon/document.hpp>
#include <quader/modeling/neutral/polygon/document_selection.hpp>

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

QMat4 modeling_transform_matrix(const ModelingTransform3 &transform,
                                QVec3 pivot) {
  const QVec3 linear_pivot =
      transform.x_axis * pivot.x + transform.y_axis * pivot.y +
      transform.z_axis * pivot.z;
  const QVec3 origin = transform.origin + pivot - linear_pivot;
  glm::mat4 matrix{1.0f};
  matrix[0] = glm::vec4{to_glm(transform.x_axis), 0.0f};
  matrix[1] = glm::vec4{to_glm(transform.y_axis), 0.0f};
  matrix[2] = glm::vec4{to_glm(transform.z_axis), 0.0f};
  matrix[3] = glm::vec4{to_glm(origin), 1.0f};
  return from_glm(matrix);
}

std::optional<glm::dmat3> normalized_orientation_basis(
    const quader_poly::Transform3 &orientation) {
  constexpr double kOrientationEpsilon = 0.000001;
  glm::dmat3 basis{1.0};
  const std::array<QVec3, 3> axes{
      orientation.x_axis,
      orientation.y_axis,
      orientation.z_axis,
  };
  for (glm::length_t column = 0; column < 3; ++column) {
    const QVec3 axis = axes[static_cast<std::size_t>(column)];
    const glm::dvec3 exact_axis{axis.x, axis.y, axis.z};
    const double axis_length = glm::length(exact_axis);
    if (!std::isfinite(axis_length) || axis_length <= kOrientationEpsilon) {
      return std::nullopt;
    }
    basis[column] = exact_axis / axis_length;
  }

  const double determinant = glm::determinant(basis);
  if (!std::isfinite(determinant) ||
      std::abs(determinant) <= kOrientationEpsilon) {
    return std::nullopt;
  }
  return basis;
}

std::optional<ModelingTransform3>
modeling_orientation_from_world_matrix(const QMat4 &world_matrix) {
  if (!scene_matrix_is_finite(world_matrix)) {
    return std::nullopt;
  }

  const glm::mat4 world = to_glm(world_matrix);
  const quader_poly::Transform3 exact_world_basis{
      .x_axis = from_glm(glm::vec3{world[0]}),
      .y_axis = from_glm(glm::vec3{world[1]}),
      .z_axis = from_glm(glm::vec3{world[2]}),
  };
  const std::optional<glm::dmat3> normalized =
      normalized_orientation_basis(exact_world_basis);
  if (!normalized.has_value()) {
    return std::nullopt;
  }
  return ModelingTransform3{
      .x_axis = from_glm(glm::vec3{(*normalized)[0]}),
      .y_axis = from_glm(glm::vec3{(*normalized)[1]}),
      .z_axis = from_glm(glm::vec3{(*normalized)[2]}),
  };
}

std::optional<QVec3> remap_local_move_delta(
    QVec3 recorded_world_delta,
    const quader_poly::Transform3 &recorded_orientation,
    const ModelingTransform3 &current_orientation) {
  const std::optional<glm::dmat3> recorded_basis =
      normalized_orientation_basis(recorded_orientation);
  const std::optional<glm::dmat3> current_basis =
      normalized_orientation_basis(to_runtime(current_orientation));
  if (!recorded_basis.has_value() || !current_basis.has_value()) {
    return std::nullopt;
  }

  // Exact displayed local directions need not be orthogonal after a rotated
  // child is composed through non-uniform scale. Recover logical coordinates
  // with the full inverse rather than treating dot products as an inverse.
  const glm::dvec3 world_delta{recorded_world_delta.x,
                               recorded_world_delta.y,
                               recorded_world_delta.z};
  const glm::dvec3 logical_local_delta =
      glm::inverse(*recorded_basis) * world_delta;
  const glm::dvec3 next_world_delta =
      *current_basis * logical_local_delta;
  const QVec3 remapped{static_cast<float>(next_world_delta.x),
                       static_cast<float>(next_world_delta.y),
                       static_cast<float>(next_world_delta.z)};
  return std::isfinite(remapped.x) && std::isfinite(remapped.y) &&
                 std::isfinite(remapped.z)
             ? std::optional<QVec3>{remapped}
             : std::nullopt;
}

std::optional<ModelingTransform3> modeling_component_world_orientation(
    const quader_poly::Transform3 &local_orientation,
    const QMat4 &world_matrix) {
  constexpr float kOrientationEpsilon = 0.000001F;
  if (!scene_matrix_is_finite(world_matrix)) {
    return std::nullopt;
  }

  const glm::mat3 linear{to_glm(world_matrix)};
  const float determinant = glm::determinant(linear);
  if (!std::isfinite(determinant) ||
      std::abs(determinant) <= kOrientationEpsilon) {
    return std::nullopt;
  }

  glm::vec3 z_axis = glm::transpose(glm::inverse(linear)) *
                     to_glm(local_orientation.z_axis);
  if (determinant < 0.0F) {
    z_axis = -z_axis;
  }
  const float z_length = glm::length(z_axis);
  if (!std::isfinite(z_length) || z_length <= kOrientationEpsilon) {
    return std::nullopt;
  }
  z_axis /= z_length;

  glm::vec3 x_axis = linear * to_glm(local_orientation.x_axis);
  x_axis -= z_axis * glm::dot(x_axis, z_axis);
  float x_length = glm::length(x_axis);
  if (!std::isfinite(x_length) || x_length <= kOrientationEpsilon) {
    if (const std::optional<glm::mat3> object_basis =
            try_scene_pure_world_rotation_basis(world_matrix)) {
      x_axis = (*object_basis)[0] -
               z_axis * glm::dot((*object_basis)[0], z_axis);
      x_length = glm::length(x_axis);
    }
  }
  if (!std::isfinite(x_length) || x_length <= kOrientationEpsilon) {
    return std::nullopt;
  }
  x_axis /= x_length;

  glm::vec3 y_axis = glm::cross(z_axis, x_axis);
  const float y_length = glm::length(y_axis);
  if (!std::isfinite(y_length) || y_length <= kOrientationEpsilon) {
    return std::nullopt;
  }
  y_axis /= y_length;
  x_axis = glm::normalize(glm::cross(y_axis, z_axis));

  return ModelingTransform3{
      .x_axis = from_glm(x_axis),
      .y_axis = from_glm(y_axis),
      .z_axis = from_glm(z_axis),
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

float world_segment_factor_from_screen_projection(
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

std::optional<std::pair<float, float>> clipped_screen_segment_factors(
    const quader_editor_core::ScreenRect &rect, QVec2 a, QVec2 b) {
  const double dx = static_cast<double>(b.x - a.x);
  const double dy = static_cast<double>(b.y - a.y);
  double enter = 0.0;
  double leave = 1.0;
  const auto clip = [&](double p, double q) {
    constexpr double kParallelEpsilon = 0.0000001;
    if (std::abs(p) <= kParallelEpsilon) {
      return q >= 0.0;
    }
    const double factor = q / p;
    if (p < 0.0) {
      enter = std::max(enter, factor);
    } else {
      leave = std::min(leave, factor);
    }
    return enter <= leave;
  };
  if (!clip(-dx, static_cast<double>(a.x) - rect.min_x) ||
      !clip(dx, rect.max_x - static_cast<double>(a.x)) ||
      !clip(-dy, static_cast<double>(a.y) - rect.min_y) ||
      !clip(dy, rect.max_y - static_cast<double>(a.y))) {
    return std::nullopt;
  }
  return std::pair{static_cast<float>(enter), static_cast<float>(leave)};
}

bool screen_point_on_segment(QVec2 point, QVec2 a, QVec2 b) {
  const KnifeScreenSegmentProjection projection =
      knife_pick_screen_segment_projection(point, a, b);
  return projection.distance_squared <= 0.0001F;
}

bool screen_point_inside_polygon(QVec2 point, std::span<const QVec2> polygon) {
  if (polygon.size() < 3U) {
    return false;
  }
  bool inside = false;
  for (std::size_t index = 0; index < polygon.size(); ++index) {
    const QVec2 a = polygon[index];
    const QVec2 b = polygon[(index + 1U) % polygon.size()];
    if (screen_point_on_segment(point, a, b)) {
      return true;
    }
    const bool crosses = (a.y > point.y) != (b.y > point.y);
    if (!crosses) {
      continue;
    }
    const double x = static_cast<double>(a.x) +
                     (static_cast<double>(point.y - a.y) *
                      static_cast<double>(b.x - a.x)) /
                         static_cast<double>(b.y - a.y);
    if (static_cast<double>(point.x) < x) {
      inside = !inside;
    }
  }
  return inside;
}

class RectangleVisibilityWitnessAccumulator final {
public:
  explicit RectangleVisibilityWitnessAccumulator(
      const quader_editor_core::ScreenRect &rect)
      : rect_(rect), anchors_{
                         QVec2{static_cast<float>(rect.min_x),
                               static_cast<float>(rect.min_y)},
                         QVec2{static_cast<float>(rect.max_x),
                               static_cast<float>(rect.min_y)},
                         QVec2{static_cast<float>(rect.max_x),
                               static_cast<float>(rect.max_y)},
                         QVec2{static_cast<float>(rect.min_x),
                               static_cast<float>(rect.max_y)},
                         QVec2{static_cast<float>((rect.min_x + rect.max_x) *
                                                  0.5),
                               static_cast<float>(rect.min_y)},
                         QVec2{static_cast<float>(rect.max_x),
                               static_cast<float>((rect.min_y + rect.max_y) *
                                                  0.5)},
                         QVec2{static_cast<float>((rect.min_x + rect.max_x) *
                                                  0.5),
                               static_cast<float>(rect.max_y)},
                         QVec2{static_cast<float>(rect.min_x),
                               static_cast<float>((rect.min_y + rect.max_y) *
                                                  0.5)},
                         QVec2{static_cast<float>((rect.min_x + rect.max_x) *
                                                  0.5),
                               static_cast<float>((rect.min_y + rect.max_y) *
                                                  0.5)},
                     } {}

  void consider(QVec2 screen, QVec3 world) {
    if (!quader_editor_core::screen_rect_contains(rect_, screen)) {
      return;
    }
    const ModelingSelectionVisibilityWitness candidate{
        .screen_position = screen,
        .world_position = world,
    };
    for (std::size_t anchor_index = 0; anchor_index < anchors_.size();
         ++anchor_index) {
      const float dx = screen.x - anchors_[anchor_index].x;
      const float dy = screen.y - anchors_[anchor_index].y;
      const float distance = dx * dx + dy * dy;
      auto &nearest = nearest_[anchor_index];
      if (!nearest[0].has_value() || distance < nearest[0]->distance) {
        nearest[1] = nearest[0];
        nearest[0] = RankedWitness{candidate, distance};
      } else if (!nearest[1].has_value() ||
                 distance < nearest[1]->distance) {
        nearest[1] = RankedWitness{candidate, distance};
      }
    }
  }

  [[nodiscard]] std::span<const QVec2> anchors() const { return anchors_; }
  [[nodiscard]] const quader_editor_core::ScreenRect &rect() const {
    return rect_;
  }

  [[nodiscard]] std::vector<ModelingSelectionVisibilityWitness> finish() const {
    std::vector<ModelingSelectionVisibilityWitness> result;
    result.reserve(anchors_.size() * 2U);
    for (const auto &nearest : nearest_) {
      for (const std::optional<RankedWitness> &ranked : nearest) {
        if (!ranked.has_value()) {
          continue;
        }
        const auto duplicate = std::find_if(
            result.begin(), result.end(), [&](const auto &witness) {
              return witness == ranked->witness;
            });
        if (duplicate == result.end()) {
          result.push_back(ranked->witness);
        }
      }
    }
    return result;
  }

private:
  struct RankedWitness {
    ModelingSelectionVisibilityWitness witness;
    float distance = 0.0F;
  };

  quader_editor_core::ScreenRect rect_;
  std::array<QVec2, 9> anchors_;
  std::array<std::array<std::optional<RankedWitness>, 2>, 9> nearest_{};
};

std::optional<QVec3> face_witness_at_screen(
    const quader_editor_core::ViewportCameraState &camera,
    const quader_poly::Document &document, const quader_poly::Face &face,
    QVec2 screen) {
  const quader_poly::Ray ray =
      quader_editor_core::ray_from_viewport_screen_point(camera, screen);
  quader_poly::PickOptions options;
  options.mode = quader_poly::SelectionMode::Face;
  options.pick_backfaces = true;
  options.prefer_front_facing_faces = false;
  const std::vector<quader_poly::PickFaceTriangle> triangles =
      quader_poly::build_face_pick_geometry(document, face.id, options);
  const quader_poly::PickResult hit =
      quader_poly::pick_face_geometry(triangles, ray, options);
  return hit.hit ? std::optional<QVec3>{hit.position} : std::nullopt;
}

void collect_face_visibility_witnesses(
    const quader_editor_core::ViewportCameraState &camera,
    const quader_poly::Document &document, const quader_poly::Face &face,
    RectangleVisibilityWitnessAccumulator &witnesses) {
  std::vector<QVec2> screen_vertices;
  screen_vertices.reserve(face.vertices.size());
  bool all_projected = !face.vertices.empty();
  for (const quader_poly::ElementId id : face.vertices) {
    const quader_poly::Vertex *vertex = quader_poly::find_vertex(document, id);
    if (vertex == nullptr) {
      all_projected = false;
      screen_vertices.push_back({});
      continue;
    }
    QVec2 screen{};
    if (!quader_editor_core::project_viewport_point(camera, vertex->position,
                                                    screen)) {
      all_projected = false;
      screen_vertices.push_back({});
      continue;
    }
    screen_vertices.push_back(screen);
    witnesses.consider(screen, vertex->position);
  }

  for (std::size_t index = 0; index < face.vertices.size(); ++index) {
    const std::size_t next = (index + 1U) % face.vertices.size();
    const quader_poly::Vertex *a =
        quader_poly::find_vertex(document, face.vertices[index]);
    const quader_poly::Vertex *b =
        quader_poly::find_vertex(document, face.vertices[next]);
    if (a == nullptr || b == nullptr) {
      continue;
    }
    QVec2 screen_a{};
    QVec2 screen_b{};
    if (!quader_editor_core::project_viewport_point(camera, a->position,
                                                    screen_a) ||
        !quader_editor_core::project_viewport_point(camera, b->position,
                                                    screen_b)) {
      continue;
    }
    const auto clipped = clipped_screen_segment_factors(
        witnesses.rect(), screen_a, screen_b);
    if (!clipped.has_value()) {
      continue;
    }
    const float screen_factor =
        (clipped->first + clipped->second) * 0.5F;
    const float world_factor = world_segment_factor_from_screen_projection(
        camera, a->position, b->position, screen_factor);
    witnesses.consider(
        screen_a + (screen_b - screen_a) * screen_factor,
        a->position + (b->position - a->position) * world_factor);
  }

  if (!all_projected) {
    return;
  }
  for (const QVec2 anchor : witnesses.anchors()) {
    if (!screen_point_inside_polygon(anchor, screen_vertices)) {
      continue;
    }
    if (const std::optional<QVec3> world =
            face_witness_at_screen(camera, document, face, anchor)) {
      witnesses.consider(anchor, *world);
    }
  }
}

std::vector<ModelingSelectionVisibilityWitness>
resolved_selection_visibility_witnesses(
    std::span<const quader_editor_core::PolygonObject> picking_objects,
    int participant_id, ModelingActiveSelectionKind kind, int vertex_id,
    int edge_a, int edge_b, int face_id,
    const ModelingBoxSelectRequest &request) {
  const auto object = std::find_if(
      picking_objects.begin(), picking_objects.end(),
      [participant_id](const quader_editor_core::PolygonObject &candidate) {
        return candidate.id == participant_id;
      });
  if (object == picking_objects.end()) {
    return {};
  }
  const quader_poly::Document &document = object->document.read();
  const quader_editor_core::ViewportCameraState camera =
      modeling_runtime_camera(request.camera);
  RectangleVisibilityWitnessAccumulator witnesses(
      quader_editor_core::normalized_screen_rect(request.start, request.end));

  if (kind == ModelingActiveSelectionKind::Vertex) {
    const quader_poly::Vertex *vertex =
        quader_poly::find_vertex(document, vertex_id);
    QVec2 screen{};
    if (vertex != nullptr && quader_editor_core::project_viewport_point(
                                 camera, vertex->position, screen)) {
      witnesses.consider(screen, vertex->position);
    }
    return witnesses.finish();
  }
  if (kind == ModelingActiveSelectionKind::Edge) {
    const quader_poly::Vertex *a = quader_poly::find_vertex(document, edge_a);
    const quader_poly::Vertex *b = quader_poly::find_vertex(document, edge_b);
    QVec2 screen_a{};
    QVec2 screen_b{};
    if (a != nullptr && b != nullptr &&
        quader_editor_core::project_viewport_point(camera, a->position,
                                                   screen_a) &&
        quader_editor_core::project_viewport_point(camera, b->position,
                                                   screen_b)) {
      witnesses.consider(screen_a, a->position);
      witnesses.consider(screen_b, b->position);
      const auto clipped = clipped_screen_segment_factors(
          quader_editor_core::normalized_screen_rect(request.start,
                                                     request.end),
          screen_a, screen_b);
      if (clipped.has_value()) {
        const float screen_factor =
            (clipped->first + clipped->second) * 0.5F;
        const float world_factor =
            world_segment_factor_from_screen_projection(
                camera, a->position, b->position, screen_factor);
        witnesses.consider(
            screen_a + (screen_b - screen_a) * screen_factor,
            a->position + (b->position - a->position) * world_factor);
      }
    }
    return witnesses.finish();
  }
  if (kind == ModelingActiveSelectionKind::Face) {
    const quader_poly::Face *face = quader_poly::find_face(document, face_id);
    if (face != nullptr) {
      collect_face_visibility_witnesses(camera, document, *face, witnesses);
    }
    return witnesses.finish();
  }
  if (kind == ModelingActiveSelectionKind::Object) {
    for (const quader_poly::Face &face : document.faces) {
      collect_face_visibility_witnesses(camera, document, face, witnesses);
    }
    if (document.faces.empty()) {
      for (const quader_poly::Vertex &vertex : document.vertices) {
        QVec2 screen{};
        if (quader_editor_core::project_viewport_point(
                camera, vertex.position, screen)) {
          witnesses.consider(screen, vertex.position);
        }
      }
    }
  }
  return witnesses.finish();
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
          world_segment_factor_from_screen_projection(
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
  case ModelingViewportTool::LassoSelect:
    return quader_editor_core::ViewportInteractionTool::LassoSelect;
  }
  return quader_editor_core::ViewportInteractionTool::Select;
}

ModelingViewportTool
from_runtime_viewport_tool(quader_editor_core::ViewportInteractionTool tool) {
  switch (tool) {
  case quader_editor_core::ViewportInteractionTool::Move:
    return ModelingViewportTool::Move;
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
  case quader_editor_core::ViewportInteractionTool::LassoSelect:
    return ModelingViewportTool::LassoSelect;
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

quader_editor_core::TransformSpace to_runtime(ModelingTransformSpace space) {
  return space == ModelingTransformSpace::Local
             ? quader_editor_core::TransformSpace::Local
             : quader_editor_core::TransformSpace::Global;
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
    target.face_id = static_cast<int>(hover.face_id);
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
from_runtime_hover_summary(const ModelingSelectionProjectionSnapshot &session,
                           const quader_editor_core::SelectionHoverState &hover,
                           bool changed) {
  return {
      .changed = changed,
      .target = from_runtime(hover),
      .target_selected =
          quader_editor_core::selection_hover_target_matches_selection(
              session.objects(), session.selection_mode(), hover),
      .candidate_object_id = hover.candidate_object_id,
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
  result.footprint_size = preview.footprint.size;
  result.signed_footprint_size = preview.footprint.signed_size;
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
  explicit Impl(ModelingSdkProjectionSnapshot sdk_projection_snapshot)
      : sdk_projection(std::move(sdk_projection_snapshot)) {}

  ModelingSdkProjectionSnapshot sdk_projection;
  std::optional<DocumentSelectionState> group_selection;
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
  /// Retains one selected object's transform basis for the duration of a drag.
  struct TransformDragLocalInput {
    int modeling_object_id = kInvalidModelingObjectId;
    QMat4 world_from_object;
    QMat4 object_from_world;
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

  /// Cheap owner facts that key the retained world-space volatile overlay.
  struct VolatileOverlaySourceKey final {
    std::uint64_t content_revision = 0;
    std::uint64_t selection_revision = 0;
    std::uint64_t scene_revision = 0;
    std::uint64_t local_visibility_revision = 0;
    std::uint64_t object_map_revision = 0;
    std::uint64_t asset_revision = 0;
    std::uint64_t transform_preview_revision = 0;
    std::uint64_t runtime_overlay_revision = 0;
    ModelingSelectionMode app_selection_mode = ModelingSelectionMode::Object;
    ModelingInstanceEditScope instance_edit_scope;

    friend bool operator==(const VolatileOverlaySourceKey &,
                           const VolatileOverlaySourceKey &) = default;
  };

  /// Retains one immutable publication until one cheap source fact changes.
  struct VolatileOverlayCache final {
    bool valid = false;
    VolatileOverlaySourceKey key;
    std::shared_ptr<const ViewportOverlayFrame> frame;
    std::uint64_t publication_revision = 0u;
    std::uint64_t build_count = 0u;
    std::uint64_t reuse_count = 0u;
  };

  /// Fixed-size Knife cursor facts consumed by the volatile overlay builder.
  struct KnifeHoverOverlayKey final {
    quader_editor_core::KnifeToolStatus status =
        quader_editor_core::KnifeToolStatus::Idle;
    bool has_current = false;
    bool current_target_valid = false;
    int current_object_id = -1;
    int target_object_id = -1;
    int target_kind = 0;
    quader_poly::ElementId vertex_id = quader_poly::kInvalidElementId;
    quader_poly::ElementId edge_a = quader_poly::kInvalidElementId;
    quader_poly::ElementId edge_b = quader_poly::kInvalidElementId;
    float edge_factor = 0.0f;
    quader_poly::ElementId face_id = quader_poly::kInvalidElementId;
    float target_x = 0.0f;
    float target_y = 0.0f;
    float target_z = 0.0f;
    float current_x = 0.0f;
    float current_y = 0.0f;
    float current_z = 0.0f;
    float normal_x = 0.0f;
    float normal_y = 0.0f;
    float normal_z = 0.0f;
    std::size_t preview_line_count = 0u;
    std::size_t preview_point_count = 0u;

    friend bool operator==(const KnifeHoverOverlayKey &,
                           const KnifeHoverOverlayKey &) = default;
  };

  /// Shared host inputs for retained texture presentation and target validity.
  struct TextureSettingsCacheKey final {
    std::uint64_t content_revision = 0;
    std::uint64_t selection_revision = 0;
    std::uint64_t scene_revision = 0;
    std::uint64_t layer_revision = 0;
    std::uint64_t asset_revision = 0;
    std::uint64_t visibility_revision = 0;
    std::uint64_t extent_revision = 0;
    std::uint64_t context_revision = 0;
    ModelingInstanceEditScope instance_scope;
    bool preview_active = false;

    friend bool operator==(const TextureSettingsCacheKey &,
                           const TextureSettingsCacheKey &) = default;
  };

public:
  Impl(SceneDocument &scene, AssetRegistry &assets,
       ModelingOperationPreviewCheckpointHooks preview_checkpoint_hooks)
      : scene_(scene), assets_(assets),
        sdk_projection_adapter_(scene, assets, object_registry_,
                                mesh_sync_service_),
        preview_checkpoint_hooks_(std::move(preview_checkpoint_hooks)) {
    selection_service_.set_app_selection_mode_provider(
        [this]() { return app_selection_mode_; });
    sdk_projection_adapter_.set_modeling_object_world_transform_resolver(
        [this](int modeling_object_id) {
          return modeling_object_world_transform(modeling_object_id);
        });
  }

  ~Impl() {
    if (transform_drag_active() || structural_drag_checkpoint_active_) {
      (void)cancel_transform_drag();
    }
    if (structural_preview_transaction_.has_value()) {
      (void)cancel_transform_preview();
    }
  }

  void detach_preview_checkpoint_hooks() {
    preview_checkpoint_hooks_ = {};
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

  [[nodiscard]] std::vector<EntityId> matching_geometry_entities(
      std::span<const EntityId> candidates,
      std::span<const EntityId> references) const {
    const ModelingSelectionProjectionSnapshot &projection =
        sdk_projection_adapter_.selection_projection_snapshot();
    std::unordered_map<int, const quader_editor_core::PolygonObject *>
        objects_by_id;
    objects_by_id.reserve(projection.objects().size());
    for (const quader_editor_core::PolygonObject &object :
         projection.objects()) {
      objects_by_id.emplace(object.id, &object);
    }

    /// Describes one visible geometry member inside an atomic selection target.
    struct GeometryMemberDescriptor final {
      const quader_editor_core::PolygonObject *object = nullptr;
      QMat4 definition_relative_transform;
    };
    using GeometryTargetDescriptor =
        std::vector<GeometryMemberDescriptor>;

    const auto descriptor_for_entity =
        [&](EntityId entity) -> std::optional<GeometryTargetDescriptor> {
      if (!scene_.exists(entity) ||
          scene_.is_instance_definition_member(entity) ||
          scene_.is_instance_definition_entity(entity)) {
        return std::nullopt;
      }

      GeometryTargetDescriptor descriptor;
      if (const InstanceOccurrenceComponent *occurrence =
              scene_.instance_occurrence(entity)) {
        if (!scene_.is_instance_definition_entity(occurrence->definition)) {
          return std::nullopt;
        }
        const std::optional<QMat4> inverse_definition =
            try_scene_matrix_inverse(
                scene_.world_transform_matrix(occurrence->definition));
        if (!inverse_definition.has_value()) {
          return std::nullopt;
        }
        for (const EntityId member :
             scene_.descendants(occurrence->definition, false)) {
          // Match the occurrence's actual rendered geometry universe: only
          // members owned by this definition whose complete local hierarchy
          // segment is visible. This also prevents a nested definition, if a
          // future scene schema permits one, from being expanded twice.
          if (scene_.containing_instance_definition(member) !=
                  occurrence->definition ||
              !scene_hierarchy_segment_is_visible(
                  scene_, member, occurrence->definition) ||
              scene_.kind(member) != EntityKind::Mesh) {
            continue;
          }
          const std::optional<int> object_id =
              object_registry_.modeling_object_for_entity(member);
          if (!object_id.has_value()) {
            return std::nullopt;
          }
          const auto object = objects_by_id.find(*object_id);
          if (object == objects_by_id.end()) {
            return std::nullopt;
          }
          descriptor.push_back({
              .object = object->second,
              .definition_relative_transform = scene_transform_multiply(
                  *inverse_definition, scene_.world_transform_matrix(member)),
          });
        }
      } else {
        const std::optional<int> object_id =
            object_registry_.modeling_object_for_entity(entity);
        if (!object_id.has_value()) {
          return std::nullopt;
        }
        const auto object = objects_by_id.find(*object_id);
        if (object == objects_by_id.end()) {
          return std::nullopt;
        }
        descriptor.push_back({.object = object->second});
      }

      if (descriptor.empty()) {
        return std::nullopt;
      }
      if (descriptor.size() == 1U) {
        // A one-mesh target has no internal arrangement. Its scene or
        // definition placement is the target's outer transform and Select
        // Similar deliberately compares local geometry independently of it.
        descriptor.front().definition_relative_transform = QMat4{};
      }
      return descriptor;
    };

    const auto transforms_match = [](const QMat4 &left, const QMat4 &right) {
      const auto exact_value_key = [](float value) {
        // Equivalent definitions can acquire opposite signed zeroes while
        // composing/inverting their independently created transforms.
        return value == 0.0F ? 0U : std::bit_cast<std::uint32_t>(value);
      };
      for (std::size_t index = 0U; index < left.values.size(); ++index) {
        if (exact_value_key(left.values[index]) !=
            exact_value_key(right.values[index])) {
          return false;
        }
      }
      return true;
    };
    const auto descriptors_match =
        [&](const GeometryTargetDescriptor &left,
            const GeometryTargetDescriptor &right) {
      if (left.size() != right.size()) {
        return false;
      }
      std::vector<bool> right_matched(right.size(), false);
      for (std::size_t left_index = 0U; left_index < left.size();
           ++left_index) {
        bool found_equivalent = false;
        for (std::size_t right_index = 0U; right_index < right.size();
             ++right_index) {
          if (right_matched[right_index] ||
              !transforms_match(
                  left[left_index].definition_relative_transform,
                  right[right_index].definition_relative_transform) ||
              !quader_poly::has_same_geometry(
                  left[left_index].object->document,
                  right[right_index].object->document)) {
            continue;
          }
          right_matched[right_index] = true;
          found_equivalent = true;
          break;
        }
        if (!found_equivalent) {
          return false;
        }
      }
      return true;
    };

    std::vector<GeometryTargetDescriptor> reference_descriptors;
    reference_descriptors.reserve(references.size());
    for (const EntityId entity : references) {
      std::optional<GeometryTargetDescriptor> descriptor =
          descriptor_for_entity(entity);
      if (descriptor.has_value()) {
        reference_descriptors.push_back(std::move(*descriptor));
      }
    }
    if (reference_descriptors.empty()) {
      return {};
    }

    std::vector<EntityId> matches;
    matches.reserve(candidates.size());
    for (const EntityId entity : candidates) {
      const std::optional<GeometryTargetDescriptor> candidate =
          descriptor_for_entity(entity);
      if (!candidate.has_value()) {
        continue;
      }
      const bool matches_reference =
          std::any_of(reference_descriptors.begin(),
                      reference_descriptors.end(),
                      [&](const GeometryTargetDescriptor &reference) {
                        return descriptors_match(*candidate, reference);
                      });
      if (matches_reference) {
        matches.push_back(entity);
      }
    }
    return matches;
  }

  [[nodiscard]] std::uint64_t modeling_object_map_revision() const {
    return object_registry_.revision();
  }

  [[nodiscard]] std::optional<MeshAsset>
  compile_object_mesh(int modeling_object_id) const {
    return sdk_projection_adapter_.compile_object_mesh(modeling_object_id);
  }

  [[nodiscard]] bool sdk_selection_active() const {
    return sdk_projection_adapter_.has_selection();
  }

  [[nodiscard]] bool active_scene_selection_is_owned_outside_modeling() const {
    const EntityId active = scene_.primary_selection();
    return active != kInvalidEntityId &&
           !sdk_projection_adapter_.owns_entity(active);
  }

  [[nodiscard]] std::optional<EntityId>
  pick_object_entity(const ModelingPickSelectRequest &request) const {
    const ModelingSelectionProjectionSnapshot &projected =
        selection_projection_snapshot();
    return selection_service_.pick_object_entity(
        projected, scene_, object_registry_, request);
  }

  [[nodiscard]] std::optional<ModelingSelectionHit>
  query_selection_hit(const ModelingPickSelectRequest &request) const {
    const ModelingSelectionProjectionSnapshot &projected =
        selection_projection_snapshot();
    const std::optional<quader_editor_core::PickHitRecord> hit =
        selection_service_.pick_selection_hit(
            projected, scene_, object_registry_, request);
    if (!hit.has_value()) {
      return std::nullopt;
    }
    return selection_hit_from_record(projected, *hit);
  }

  [[nodiscard]] std::vector<ModelingSelectionHit>
  query_selection_depth_hits(const ModelingPickSelectRequest &request) const {
    const ModelingSelectionProjectionSnapshot &projected =
        selection_projection_snapshot();
    const std::vector<quader_editor_core::PickHitRecord> hits =
        selection_service_.pick_selection_depth_hits(
            projected, scene_, object_registry_, request);
    std::vector<ModelingSelectionHit> result;
    result.reserve(hits.size());
    for (const quader_editor_core::PickHitRecord &hit : hits) {
      result.push_back(selection_hit_from_record(projected, hit));
    }
    return result;
  }

  [[nodiscard]] ModelingSelectionHit selection_hit_from_record(
      const ModelingSelectionProjectionSnapshot &projected,
      const quader_editor_core::PickHitRecord &hit) const {
    std::optional<EntityId> entity =
        selection_service_.scene_target_for_picking_participant(
            hit.object_id);
    if (!entity.has_value()) {
      entity = object_registry_.entity_for_modeling_object(hit.object_id);
    }
    const std::optional<int> authored_modeling_object =
        entity.has_value()
            ? object_registry_.modeling_object_for_entity(*entity)
            : std::nullopt;
    const bool scene_entity_hit =
        app_selection_mode_ == ModelingSelectionMode::Object ||
        app_selection_mode_ == ModelingSelectionMode::Group;
    const int modeling_object_id =
        authored_modeling_object.value_or(
            scene_entity_hit ? kInvalidModelingObjectId : hit.object_id);
    QVec3 world_position = hit.local_position;
    const std::span<const quader_editor_core::PickObjectTransform> transforms =
        selection_service_.cached_local_picking_transforms(
            projected, scene_, object_registry_);
    if (hit.object_index >= 0 &&
        static_cast<std::size_t>(hit.object_index) < transforms.size() &&
        transforms[static_cast<std::size_t>(hit.object_index)].object_id ==
            hit.object_id) {
      world_position = transform_point(
          transforms[static_cast<std::size_t>(hit.object_index)]
              .world_from_object,
          hit.local_position);
    }
    ModelingSelectionHit result{
        .entity = entity.value_or(kInvalidEntityId),
        .modeling_object_id = modeling_object_id,
        .kind = app_selection_mode_ == ModelingSelectionMode::Object ||
                        app_selection_mode_ == ModelingSelectionMode::Group
                    ? ModelingActiveSelectionKind::Object
                    : ModelingActiveSelectionKind::None,
        .ray_distance = hit.ray_distance,
        .pick_distance = hit.pick_distance,
        .world_position = world_position,
        .signature = hit.signature,
    };
    if (result.kind == ModelingActiveSelectionKind::None) {
      switch (hit.kind) {
      case quader_poly::ElementKind::Vertex:
        result.kind = ModelingActiveSelectionKind::Vertex;
        result.vertex_id = static_cast<int>(hit.vertex_id);
        break;
      case quader_poly::ElementKind::Edge:
        result.kind = ModelingActiveSelectionKind::Edge;
        result.edge_a = static_cast<int>(hit.edge.a);
        result.edge_b = static_cast<int>(hit.edge.b);
        result.face_id = static_cast<int>(hit.face_id);
        break;
      case quader_poly::ElementKind::Face:
        result.kind = ModelingActiveSelectionKind::Face;
        result.face_id = static_cast<int>(hit.face_id);
        break;
      }
    }
    if (result.kind != ModelingActiveSelectionKind::Object) {
      const ModelingInstanceEditScope edit_scope =
          selection_service_.overlay_instance_edit_scope();
      result.source_occurrence =
          edit_scope.active() ? edit_scope.occurrence : result.entity;
    }
    return result;
  }

  [[nodiscard]] std::vector<EntityId>
  box_pick_object_entities(const ModelingBoxSelectRequest &request) const {
    QDR_PROFILE_SCOPE("Modeling session box pick objects");
    const ModelingSelectionProjectionSnapshot &projected = [&]()
        -> const ModelingSelectionProjectionSnapshot & {
      QDR_PROFILE_SCOPE("Box selection acquire session pick projection");
      return selection_projection_snapshot();
    }();
    const std::vector<int> object_ids = [&]() {
      QDR_PROFILE_SCOPE("Box selection test session object candidates");
      return selection_service_.box_pick_object_ids(
          projected, scene_, object_registry_, request);
    }();
    std::vector<EntityId> entities;
    {
      QDR_PROFILE_SCOPE("Box selection map session object hits");
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
    }
    const auto curves = curve_box_pick_cache_.box_pick_entities(
        scene_, assets_, request, selection_service_.viewport_visibility_revision(),
        [this](EntityId entity) {
          return selection_service_.viewport_entity_visible(entity);
        },
        selection_service_.overlay_instance_edit_scope().active());
    entities.insert(entities.end(), curves.begin(), curves.end());
    return entities;
  }

  [[nodiscard]] std::vector<ModelingSelectionHit>
  query_selection_marquee_hits(
      const ModelingBoxSelectRequest &request) const {
    const ModelingSelectionProjectionSnapshot &projected =
        selection_projection_snapshot();
    const quader_editor_core::RectangleSelectionHits hits =
        selection_service_.rectangle_selection_hits(
            projected, scene_, object_registry_, request);
    const std::vector<quader_editor_core::PolygonObject> &picking_objects =
        selection_service_.cached_scene_transformed_objects_for_picking(
            projected, scene_, object_registry_);
    std::vector<ModelingSelectionHit> result;
    result.reserve(hits.object_ids.size() + hits.faces.size() +
                   hits.edges.size() + hits.vertices.size());
    const auto append = [&](int object_id, ModelingActiveSelectionKind kind,
                            int vertex, int edge_a, int edge_b, int face) {
      std::optional<EntityId> entity =
          selection_service_.scene_target_for_picking_participant(object_id);
      if (!entity.has_value()) {
        entity = object_registry_.entity_for_modeling_object(object_id);
      }
      const std::optional<int> authored_modeling_object =
          entity.has_value()
              ? object_registry_.modeling_object_for_entity(*entity)
              : std::nullopt;
      const int resolved_object = authored_modeling_object.value_or(
          kind == ModelingActiveSelectionKind::Object
              ? kInvalidModelingObjectId
              : object_id);
      std::vector<ModelingSelectionVisibilityWitness> witnesses =
          resolved_selection_visibility_witnesses(
              picking_objects, object_id, kind, vertex, edge_a, edge_b, face,
              request);
      std::optional<QVec3> world_position =
          witnesses.empty()
              ? std::nullopt
              : std::optional<QVec3>(witnesses.front().world_position);
      result.push_back(ModelingSelectionHit{
          .entity = entity.value_or(kInvalidEntityId),
          .modeling_object_id = resolved_object,
          .kind = kind,
          .vertex_id = vertex,
          .edge_a = edge_a,
          .edge_b = edge_b,
          .face_id = face,
          .world_position = std::move(world_position),
          .visibility_witnesses = std::move(witnesses),
      });
    };
    for (const int object_id : hits.object_ids) {
      append(object_id, ModelingActiveSelectionKind::Object, -1, -1, -1,
             -1);
    }
    for (const auto &face : hits.faces) {
      append(face.object_id, ModelingActiveSelectionKind::Face, -1, -1, -1,
             face.face_index);
    }
    for (const auto &edge : hits.edges) {
      append(edge.object_id, ModelingActiveSelectionKind::Edge, -1,
             edge.a_index, edge.b_index, -1);
    }
    for (const auto &vertex : hits.vertices) {
      append(vertex.object_id, ModelingActiveSelectionKind::Vertex,
             vertex.vertex_index, -1, -1, -1);
    }
    return result;
  }

  [[nodiscard]] std::vector<ModelingSelectionHit> query_selection_lasso_hits(
      const ModelingLassoSelectRequest &request,
      std::vector<EntityId> *eligible_entities) const {
    const auto &projected = selection_projection_snapshot();
    const auto hits = selection_service_.lasso_selection_hits(
        projected, scene_, object_registry_, request);
    const auto entity_for = [&](int object_id) {
      auto entity = selection_service_.scene_target_for_picking_participant(object_id);
      return entity.has_value() ? entity : object_registry_.entity_for_modeling_object(object_id);
    };
    std::vector<int> eligible_objects;
    if (projected.selection_mode() == quader_editor_core::SelectionMode::Object &&
        (eligible_entities != nullptr || !request.partial_intersection)) {
      eligible_objects = selection_service_.lasso_eligible_object_ids(
          projected, scene_, object_registry_, request);
      if (eligible_entities != nullptr) {
        for (const int object_id : eligible_objects) {
          if (const auto entity = entity_for(object_id)) {
            eligible_entities->push_back(*entity);
          }
        }
      }
    }
    std::vector<ModelingSelectionHit> result;
    const auto append = [&](int object_id, ModelingActiveSelectionKind kind,
                            int vertex, int edge_a, int edge_b, int face) {
      const auto entity = entity_for(object_id);
      const auto authored_object = entity.has_value()
          ? object_registry_.modeling_object_for_entity(*entity) : std::nullopt;
      result.push_back({
          .entity = entity.value_or(kInvalidEntityId),
          .modeling_object_id = authored_object.value_or(
              kind == ModelingActiveSelectionKind::Object
                  ? kInvalidModelingObjectId : object_id),
          .kind = kind, .vertex_id = vertex, .edge_a = edge_a,
          .edge_b = edge_b, .face_id = face});
    };
    for (const int object_id : hits.object_ids) {
      const auto entity = entity_for(object_id);
      // A formal occurrence can project multiple geometry participants. Full
      // containment belongs to the occurrence as a whole, before group promotion.
      if (!request.partial_intersection && std::any_of(
              eligible_objects.begin(), eligible_objects.end(), [&](int other) {
                return entity_for(other) == entity &&
                    std::find(hits.object_ids.begin(), hits.object_ids.end(), other) ==
                        hits.object_ids.end();
              })) {
        continue;
      }
      append(object_id, ModelingActiveSelectionKind::Object, -1, -1, -1, -1);
    }
    for (const auto &face : hits.faces) {
      append(face.object_id, ModelingActiveSelectionKind::Face, -1, -1, -1,
             face.face_index);
    }
    for (const auto &edge : hits.edges) {
      append(edge.object_id, ModelingActiveSelectionKind::Edge, -1,
             edge.a_index, edge.b_index, -1);
    }
    for (const auto &vertex : hits.vertices) {
      append(vertex.object_id, ModelingActiveSelectionKind::Vertex,
             vertex.vertex_index, -1, -1, -1);
    }
    return result;
  }

  [[nodiscard]] std::span<const ViewportSceneCurvePickCandidate>
  scene_curve_pick_candidates() const {
    return curve_box_pick_cache_.candidates(
        scene_, assets_, selection_service_.viewport_visibility_revision(),
        [this](EntityId entity) {
          return selection_service_.viewport_entity_visible(entity);
        },
        selection_service_.overlay_instance_edit_scope().active());
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
    const bool instance_edit_active =
        edit_scope.occurrence != kInvalidEntityId &&
        edit_scope.definition != kInvalidEntityId;
    if (sdk_selection_active() && !instance_edit_active) {
      return sdk_projection_adapter_.selection_bounds();
    }
    if (instance_edit_active) {
      const ModelingSelectionProjectionSnapshot &projected =
          selection_projection_snapshot();
      std::optional<SceneBounds> bounds;
      for (const quader_editor_core::PolygonObject &object :
           projected.objects()) {
        if (!object.selected && object.selection.empty() &&
            object.id != projected.active_object_id()) {
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
            projected.selection_mode() ==
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
    const ModelingSelectionProjectionSnapshot &projected =
        selection_projection_snapshot();
    return selection_projection_service_.world_selection_bounds(
        projected, scene_, assets_, object_registry_);
  }

  DocumentCommandResult
  translate_scene_selection_by_world_delta(QVec3 world_delta,
                                           std::string label,
                                           EditorHistoryService *history =
                                               nullptr) {
    if (quader::length_squared(world_delta) <= 0.0000001f) {
      return document_command_unchanged_result();
    }
    SceneTransformDragService placement;
    if (!placement.begin(scene_, selection_center().value_or(QVec3{}), true,
                         std::move(label),
                         scene_selection_display_from_scene(), &assets_,
                         texture_lock_settings())) {
      return document_command_invalid_request_result(
          "Scene placement could not begin.");
    }
    const DocumentCommandResult moved =
        placement.preview_world_translation_delta(scene_, world_delta);
    if (!moved.ok) {
      (void)placement.cancel();
      return moved;
    }
    return history != nullptr ? placement.commit(*history)
                              : placement.accept();
  }

  DocumentCommandResult transform_scene_selection_by_world_delta(
      const QMat4 &world_delta, std::string label,
      SceneTextureTransformKind kind,
      EditorHistoryService *history = nullptr) {
    SceneTransformDragService placement;
    if (!placement.begin(scene_, selection_center().value_or(QVec3{}), true,
                         std::move(label),
                         scene_selection_display_from_scene(), &assets_,
                         texture_lock_settings())) {
      return document_command_invalid_request_result(
          "Scene transform could not begin.");
    }
    const DocumentCommandResult transformed =
        placement.preview_world_transform_delta(scene_, world_delta, kind);
    if (!transformed.ok) {
      (void)placement.cancel();
      return transformed;
    }
    return history != nullptr ? placement.commit(*history)
                              : placement.accept();
  }

  DocumentCommandResult rotate_scene_selection_by_local_delta(
      quader_editor_core::TransformAxis axis, double angle_degrees,
      std::string label, EditorHistoryService *history = nullptr) {
    SceneTransformDragService placement;
    if (!placement.begin(scene_, selection_center().value_or(QVec3{}), true,
                         std::move(label),
                         scene_selection_display_from_scene(), &assets_,
                         texture_lock_settings())) {
      return document_command_invalid_request_result(
          "Scene local rotation could not begin.");
    }
    const double angle_radians = angle_degrees * std::acos(-1.0) / 180.0;
    const DocumentCommandResult transformed =
        placement.preview_local_rotation_delta(
            scene_, quader_editor_core::rotation_axis_vector(axis) *
                        static_cast<float>(angle_radians));
    if (!transformed.ok) {
      (void)placement.cancel();
      return transformed;
    }
    return history != nullptr ? placement.commit(*history)
                              : placement.accept();
  }

  DocumentCommandResult scale_scene_selection_by_local_delta(
      QVec3 scale, std::string label,
      EditorHistoryService *history = nullptr) {
    SceneTransformDragService placement;
    if (!placement.begin(scene_, selection_center().value_or(QVec3{}), true,
                         std::move(label),
                         scene_selection_display_from_scene(), &assets_,
                         texture_lock_settings())) {
      return document_command_invalid_request_result(
          "Scene local scale could not begin.");
    }
    const DocumentCommandResult transformed =
        placement.preview_local_scale_delta(scene_, scale);
    if (!transformed.ok) {
      (void)placement.cancel();
      return transformed;
    }
    return history != nullptr ? placement.commit(*history)
                              : placement.accept();
  }

  [[nodiscard]] ModelingSelectionMode selection_mode() const {
    if (sdk_selection_active()) {
      return sdk_projection_adapter_.selection_mode();
    }
    const ModelingSelectionProjectionSnapshot &projected =
        selection_projection_snapshot();
    return selection_service_.selection_mode(projected, app_selection_mode_);
  }

  [[nodiscard]] ModelingSelectionSummary selection_summary() const {
    if (sdk_selection_active()) {
      ModelingSelectionSummary summary =
          sdk_projection_adapter_.selection_summary();
      summary.content_revision = content_revision();
      summary.selection_revision = selection_revision();
      return summary;
    }
    const ModelingSelectionProjectionSnapshot &projected =
        selection_projection_snapshot();
    return selection_service_.selection_summary(projected, scene_,
                                                app_selection_mode_);
  }

  [[nodiscard]] std::span<const EntityId>
  selected_component_entities() const {
    return sdk_projection_adapter_.selected_component_entities();
  }

  [[nodiscard]] std::uint64_t content_revision() const {
    return sdk_projection_adapter_.content_revision();
  }

  [[nodiscard]] std::uint64_t selection_revision() const {
    return sdk_projection_adapter_.selection_revision();
  }

  [[nodiscard]] TextureSettingsCacheKey texture_settings_cache_key(
      std::uint64_t context_revision, std::uint64_t extent_revision = 0) const {
    return {
        .content_revision = content_revision(),
        .selection_revision = selection_revision(),
        .scene_revision = scene_.revision(),
        .layer_revision = scene_.layer_revision(),
        .asset_revision = assets_.revision(),
        .visibility_revision = selection_service_.viewport_visibility_revision(),
        .extent_revision = extent_revision,
        .context_revision = context_revision,
        .instance_scope = instance_edit_scope_provider_
                              ? instance_edit_scope_provider_()
                              : ModelingInstanceEditScope{},
        .preview_active = sdk_projection_adapter_.preview_active() ||
                          transform_drag_active() || modal_tool_owns_viewport(),
    };
  }

  [[nodiscard]] std::uint64_t face_texture_settings_revision() const {
    const TextureSettingsCacheKey key = texture_settings_cache_key(
        face_texture_context_.revision(),
        face_texture_extent_revision_provider_
            ? face_texture_extent_revision_provider_()
            : 0);
    if (!face_texture_cache_key_.has_value() || *face_texture_cache_key_ != key) {
      face_texture_cache_key_ = key;
      ++face_texture_revision_;
    }
    return face_texture_revision_;
  }

  [[nodiscard]] QVec2 face_texture_extent(EntityId entity,
                                         std::uint32_t material_slot) const {
    constexpr QVec2 kFallback{64.0f, 64.0f};
    const MaterialComponent *component = scene_.material(entity);
    if (component == nullptr) {
      return kFallback;
    }
    const MaterialReference reference = component->material_for_slot(material_slot);
    std::optional<QVec2> extent;
    if (reference.embedded()) {
      const MaterialDocument *material = assets_.material(reference.embedded_asset());
      const std::optional<AssetId> texture_id =
          material != nullptr
              ? material_texture_slot_asset(*material,
                                             material_texture_slot::kBaseColor)
              : std::nullopt;
      const TextureAsset *texture =
          texture_id.has_value() ? assets_.texture(*texture_id) : nullptr;
      if (texture != nullptr && texture->width != 0u && texture->height != 0u) {
        extent = QVec2{static_cast<float>(texture->width),
                       static_cast<float>(texture->height)};
      }
    } else if (reference.project_owned() && face_texture_extent_provider_) {
      extent = face_texture_extent_provider_(reference);
    }
    return extent.has_value() && std::isfinite(extent->x) &&
                   std::isfinite(extent->y) && extent->x > 0.0f && extent->y > 0.0f
               ? *extent
               : kFallback;
  }

  [[nodiscard]] QVec2 face_texture_extent(
      const ModelingSelectedFaceTextureSnapshot &snapshot) const {
    if (snapshot.objects.size() != 1u ||
        snapshot.objects.front().faces.size() != 1u) {
      return {64.0f, 64.0f};
    }
    const auto &object = snapshot.objects.front();
    return face_texture_extent(object.entity, object.faces.front().material_slot);
  }

  [[nodiscard]] ModelingFaceTextureSettings face_texture_settings() const {
    const std::uint64_t revision = face_texture_settings_revision();
    if (face_texture_presented_revision_ == revision) {
      return face_texture_settings_cache_;
    }
    ModelingSelectedFaceTextureSnapshot snapshot =
        sdk_projection_adapter_.selected_face_texture_settings();
    std::string target_error;
    if (!snapshot.command.ok) {
      target_error = snapshot.command.message;
    } else {
      for (const auto &object : snapshot.objects) {
        const FaceTarget target =
            resolve_face_object_target(object.modeling_object_id);
        if (!target.command.ok) {
          target_error = target.command.message;
          break;
        }
      }
    }
    face_texture_extent_cache_ = face_texture_extent(snapshot);
    ModelingFaceTextureSettings settings = face_texture_context_.synchronize(
        snapshot, selection_revision(), face_texture_extent_cache_);
    if (!target_error.empty()) {
      settings.editable = false;
      settings.block_reason = ModelingFaceTextureBlockReason::TargetUnavailable;
      settings.disabled_reason = std::move(target_error);
    } else if (settings.selected_face_count != 0u &&
               face_texture_cache_key_->preview_active) {
      settings.editable = false;
      settings.block_reason = ModelingFaceTextureBlockReason::PreviewActive;
      settings.disabled_reason =
          "Commit or cancel the active tool or preview before editing textures.";
    }
    face_texture_selection_cache_ = std::move(snapshot);
    face_texture_settings_cache_ = std::move(settings);
    face_texture_presented_revision_ = face_texture_settings_revision();
    return face_texture_settings_cache_;
  }

  ModelingCommandResult set_face_texture_setting(
      const ModelingFaceTextureEdit &edit) {
    const ModelingFaceTextureSettings settings = face_texture_settings();
    if (!settings.editable) {
      return {.ok = false,
              .changed = false,
              .message = settings.disabled_reason.empty()
                             ? "Select one or more editable faces first."
                             : settings.disabled_reason};
    }
    std::vector<ModelingFaceTextureUpdateBatch> batches;
    const ModelingCommandResult prepared = face_texture_context_.prepare_edit(
        edit, face_texture_selection_cache_, face_texture_extent_cache_, batches);
    if (!prepared.ok) {
      return prepared;
    }
    ModelingCommandResult result =
        sdk_projection_adapter_.apply_face_texture_updates(batches);
    if (result.ok && result.changed) {
      face_texture_context_.record_applied_edit(edit);
      invalidate_selection_overlay_caches();
    }
    return result;
  }

  ModelingCommandResult apply_face_texture_action(
      const ModelingFaceTextureActionRequest &request) {
    if (modeling_face_texture_action_label(request.options.action).empty()) {
      return {.ok = false, .message = "Unknown face texture action."};
    }

    const bool whole_meshes =
        modeling_face_texture_action_is_projection(request.options.action) &&
        selection_mode() == ModelingSelectionMode::Object;
    ModelingSelectedFaceTextureSnapshot action_selection;
    if (whole_meshes) {
      if (sdk_projection_adapter_.preview_active() || transform_drag_active() ||
          modal_tool_owns_viewport()) {
        return {.ok = false,
                .changed = false,
                .message = "Commit or cancel the active tool or preview before projecting UVs."};
      }
      if (request.selection_token == 0 ||
          request.selection_token != selection_revision()) {
        return {.ok = false,
                .changed = false,
                .message = "The mesh selection changed before the UV projection."};
      }
      action_selection =
          sdk_projection_adapter_.selected_face_texture_settings(true);
      if (!action_selection.command.ok) {
        return action_selection.command;
      }
      if (action_selection.objects.empty()) {
        return {.ok = false,
                .changed = false,
                .message = "Select one or more editable meshes first."};
      }
    } else {
      const ModelingFaceTextureSettings settings = face_texture_settings();
      if (!settings.editable) {
        return {.ok = false,
                .changed = false,
                .message = settings.disabled_reason.empty()
                               ? "Select one or more editable faces first."
                               : settings.disabled_reason};
      }
      if (request.selection_token == 0 ||
          request.selection_token != settings.selection_token) {
        return {.ok = false,
                .changed = false,
                .message =
                    "The face selection changed before the texture action."};
      }
      action_selection = face_texture_selection_cache_;
    }

    const ModelingInstanceEditScope scope =
        instance_edit_scope_provider_ ? instance_edit_scope_provider_()
                                      : ModelingInstanceEditScope{};
    const ModelingSelectionSummary selection = selection_summary();
    modeling::FaceTextureActionOptions options = request.options;
    options.active_target_key = 0;
    options.active_face = {};
    std::vector<ModelingFaceTextureActionTarget> targets;
    targets.reserve(action_selection.objects.size());
    for (const auto &object : action_selection.objects) {
      const auto world = modeling_object_world_transform(object.modeling_object_id);
      const EntityId occurrence =
          scope.active() && scene_.containing_instance_definition(object.entity) ==
                                scope.definition
              ? scope.occurrence
              : kInvalidEntityId;
      const auto sampling =
          effective_texture_sampling_from_local(scene_, object.entity, occurrence);
      if (!world.has_value() || !sampling.has_value()) {
        return {.ok = false,
                .message = "A selected face has no usable texture mapping frame."};
      }
      ModelingFaceTextureActionTarget target{
          .modeling_object_id = object.modeling_object_id,
          .world_from_local = *world,
          .sampling_from_local = *sampling,
      };
      target.faces.reserve(object.faces.size());
      for (const auto &face : object.faces) {
        const QVec2 extent = face_texture_extent(object.entity, face.material_slot);
        const auto dimension = [](float value) {
          return static_cast<std::uint32_t>(std::clamp<double>(
              value, 1.0, std::numeric_limits<std::uint32_t>::max()));
        };
        target.faces.push_back({.face = face.face,
                                .texture_width = dimension(extent.x),
                                .texture_height = dimension(extent.y)});
        if (selection.active_kind == ModelingActiveSelectionKind::Face &&
            selection.active_object_id == object.modeling_object_id &&
            selection.active_face_id == static_cast<int>(face.face.index)) {
          options.active_target_key =
              static_cast<std::uint64_t>(object.modeling_object_id);
          options.active_face = face.face;
        }
      }
      targets.push_back(std::move(target));
    }
    ModelingCommandResult result =
        sdk_projection_adapter_.apply_face_texture_action(targets, options);
    if (result.ok && result.changed) {
      if (options.action == ModelingFaceTextureAction::RotateClockwise) {
        face_texture_context_.record_applied_rotation_delta(options.rotation_degrees);
      } else if (options.action == ModelingFaceTextureAction::RotateCounterClockwise) {
        face_texture_context_.record_applied_rotation_delta(-options.rotation_degrees);
      } else if (options.action == ModelingFaceTextureAction::AlignToGrid ||
                 modeling_face_texture_action_is_projection(options.action)) {
        face_texture_context_.invalidate();
      }
      auto &repeat = history_bridge_.repeat_last_action();
      repeat.last_action_kind = quader_editor_core::RepeatableActionKind::FaceTexture;
      repeat.last_face_texture = options;
      repeat.pending_preview_operation.reset();
      invalidate_selection_overlay_caches();
    }
    return result;
  }

  ModelingCommandResult transfer_face_texture_to_face(int modeling_object_id,
                                                      int face_id) {
    const ModelingFaceTextureSettings settings = face_texture_settings();
    if (!settings.editable) {
      return {.ok = false,
              .message = settings.disabled_reason.empty()
                             ? "Select exactly one editable source face first."
                             : settings.disabled_reason};
    }
    if (settings.selected_face_count != 1u ||
        face_texture_selection_cache_.objects.size() != 1u ||
        face_texture_selection_cache_.objects.front().faces.size() != 1u) {
      return {.ok = false,
              .message = "Select exactly one source face before transferring textures."};
    }

    const auto &selected = face_texture_selection_cache_.objects.front();
    const modeling::FaceId source_face = selected.faces.front().face;
    const FaceMaterialTarget source = resolve_face_material(
        selected.modeling_object_id, static_cast<int>(source_face.index));
    if (!source.command.ok) {
      return source.command;
    }
    const FaceTarget target = resolve_face_target(modeling_object_id, face_id);
    if (!target.command.ok) {
      return target.command;
    }
    if (selected.modeling_object_id == modeling_object_id &&
        source_face.index == static_cast<std::uint32_t>(face_id)) {
      return {.ok = true, .changed = false};
    }

    const ModelingInstanceEditScope scope =
        instance_edit_scope_provider_ ? instance_edit_scope_provider_()
                                      : ModelingInstanceEditScope{};
    const auto mapping_target = [&](int object_id, modeling::FaceId face,
                                    EntityId entity)
        -> std::optional<ModelingFaceTextureTransferTarget> {
      const auto world = modeling_object_world_transform(object_id);
      const EntityId occurrence =
          scope.active() && scene_.containing_instance_definition(entity) ==
                                scope.definition
              ? scope.occurrence
              : kInvalidEntityId;
      const auto sampling =
          effective_texture_sampling_from_local(scene_, entity, occurrence);
      if (!world.has_value() || !sampling.has_value()) {
        return std::nullopt;
      }
      return ModelingFaceTextureTransferTarget{
          .modeling_object_id = object_id,
          .face = face,
          .world_from_local = *world,
          .sampling_from_local = *sampling,
      };
    };
    const auto source_mapping = mapping_target(selected.modeling_object_id,
                                               source_face, source.entity);
    const auto target_mapping = mapping_target(
        modeling_object_id,
        modeling::make_id<modeling::FaceTag>(static_cast<std::uint32_t>(face_id)),
        target.entity);
    if (!source_mapping.has_value() || !target_mapping.has_value()) {
      return {.ok = false,
              .message = "A texture transfer face has no usable mapping frame."};
    }

    const MaterialComponent *existing = scene_.material(target.entity);
    MaterialComponent desired = existing != nullptr ? *existing : MaterialComponent{};
    // Keep an existing equivalent binding stable, including nonzero face slots.
    // Otherwise stage the new slot before the SDK prepares any document changes.
    const std::uint32_t slot =
        existing != nullptr &&
                existing->material_for_slot(target.material_slot) == source.material
            ? target.material_slot
            : stage_material_slot(desired, source.material);
    ModelingCommandResult result = sdk_projection_adapter_.transfer_face_texture(
        *source_mapping, *target_mapping, slot);
    if (!result.ok) {
      return result;
    }
    const bool material_changed = mesh_sync_service_.set_entity_material(
        scene_, target.entity, std::move(desired));
    result.changed = result.changed || material_changed;
    if (result.changed) {
      invalidate_selection_overlay_caches();
    }
    return result;
  }

  [[nodiscard]] bool face_texture_treat_as_one() const {
    return face_texture_context_.treat_as_one();
  }

  [[nodiscard]] std::uint64_t uv_peel_settings_revision() const {
    const TextureSettingsCacheKey key =
        texture_settings_cache_key(uv_peel_context_.revision());
    if (!uv_peel_cache_key_.has_value() || *uv_peel_cache_key_ != key) {
      uv_peel_cache_key_ = key;
      ++uv_peel_revision_;
    }
    return uv_peel_revision_;
  }

  [[nodiscard]] ModelingUvPeelSettings uv_peel_settings() const {
    const std::uint64_t revision = uv_peel_settings_revision();
    if (uv_peel_presented_revision_ == revision) {
      return uv_peel_settings_cache_;
    }
    ModelingSelectedUvPeelSnapshot snapshot =
        sdk_projection_adapter_.selected_uv_peel_edges();
    std::string target_error;
    if (!snapshot.command.ok) {
      target_error = snapshot.command.message.empty()
                         ? "A selected UV Peel mesh is unavailable."
                         : snapshot.command.message;
    } else {
      for (const auto &object : snapshot.objects) {
        const FaceTarget target =
            resolve_face_object_target(object.modeling_object_id);
        if (!target.command.ok) {
          target_error = "UV Peel: " + target.command.message;
          break;
        }
      }
    }
    ModelingUvPeelSettings settings = uv_peel_context_.synchronize(
        snapshot, content_revision(), selection_revision(),
        uv_peel_cache_key_->instance_scope);
    if (!target_error.empty()) {
      settings.editable = false;
      settings.block_reason = ModelingUvPeelBlockReason::TargetUnavailable;
      settings.disabled_reason = std::move(target_error);
    } else if (settings.selected_edge_count != 0u &&
               uv_peel_cache_key_->preview_active) {
      settings.editable = false;
      settings.block_reason = ModelingUvPeelBlockReason::PreviewActive;
      settings.disabled_reason =
          "Commit or cancel the active tool or preview before UV Peel.";
    }
    uv_peel_selection_cache_ = std::move(snapshot);
    uv_peel_settings_cache_ = std::move(settings);
    uv_peel_presented_revision_ = uv_peel_settings_revision();
    return uv_peel_settings_cache_;
  }

  ModelingCommandResult set_uv_peel_options(const ModelingUvPeelOptions &options) {
    return uv_peel_context_.set_options(options);
  }

  void invalidate_uv_peel_selection() {
    uv_peel_context_.invalidate_selection();
  }

  ModelingCommandResult peel_uv(const ModelingUvPeelRequest &request) {
    const ModelingUvPeelSettings settings = uv_peel_settings();
    if (!settings.editable) {
      return {.ok = false,
              .message = settings.disabled_reason.empty()
                             ? "Select one or more editable edge guides first."
                             : settings.disabled_reason};
    }
    if (request.selection_token == 0 ||
        request.selection_token != settings.selection_token) {
      return {.ok = false,
              .message = "The edge selection or document changed before UV Peel."};
    }
    const auto options = modeling::validate_uv_peel_options(request.options);
    if (!options.ok()) {
      return {.ok = false, .message = options.error_message()};
    }

    const ModelingInstanceEditScope scope = uv_peel_cache_key_->instance_scope;
    std::vector<ModelingUvPeelTarget> targets;
    targets.reserve(uv_peel_selection_cache_.objects.size());
    for (const auto &object : uv_peel_selection_cache_.objects) {
      const auto world = modeling_object_world_transform(object.modeling_object_id);
      const EntityId occurrence =
          scope.active() && scene_.containing_instance_definition(object.entity) ==
                                scope.definition
              ? scope.occurrence
              : kInvalidEntityId;
      const auto sampling =
          effective_texture_sampling_from_local(scene_, object.entity, occurrence);
      if (!world.has_value() || !sampling.has_value()) {
        return {.ok = false,
                .message = "A selected UV Peel mesh has no usable texture mapping frame."};
      }
      targets.push_back({
          .modeling_object_id = object.modeling_object_id,
          .edges = object.edges,
          .active_edge = object.active_edge,
          .world_from_local = *world,
          .sampling_from_local = *sampling,
      });
    }
    ModelingCommandResult result =
        sdk_projection_adapter_.apply_uv_peel(targets, options.value());
    if (result.ok && result.changed) {
      auto &repeat = history_bridge_.repeat_last_action();
      repeat.last_action_kind = quader_editor_core::RepeatableActionKind::UvPeel;
      repeat.last_uv_peel = options.value();
      repeat.pending_preview_operation.reset();
      invalidate_selection_overlay_caches();
    }
    return result;
  }

  ModelingCommandResult set_face_texture_treat_as_one(bool enabled) {
    return {.ok = true,
            .changed = face_texture_context_.set_treat_as_one(enabled)};
  }

  [[nodiscard]] double face_texture_rotation_step() const {
    return face_texture_rotation_step_provider_ ? face_texture_rotation_step_provider_()
                                               : 15.0;
  }

  void set_face_texture_rotation_step_provider(std::function<double()> provider) {
    face_texture_rotation_step_provider_ = std::move(provider);
  }

  [[nodiscard]] ModelingFaceTextureEditState
  capture_face_texture_edit_state() const {
    (void)face_texture_settings();
    return face_texture_context_.capture();
  }

  void restore_face_texture_edit_state(const ModelingFaceTextureEditState &state) {
    const auto snapshot = sdk_projection_adapter_.selected_face_texture_settings();
    face_texture_context_.restore(state, snapshot, selection_revision());
  }

  void invalidate_face_texture_edit_state() {
    face_texture_context_.invalidate();
  }

  void set_face_texture_extent_provider(
      std::function<std::optional<QVec2>(MaterialReference)> provider,
      std::function<std::uint64_t()> revision_provider) {
    face_texture_extent_provider_ = std::move(provider);
    face_texture_extent_revision_provider_ = std::move(revision_provider);
    face_texture_cache_key_.reset();
  }

  void set_texture_lock_settings_provider(
      std::function<TextureLockSettings()> provider) {
    texture_lock_settings_provider_ = std::move(provider);
  }

  [[nodiscard]] TextureLockSettings texture_lock_settings() const {
    return texture_lock_settings_provider_ ? texture_lock_settings_provider_()
                                           : TextureLockSettings{};
  }

  [[nodiscard]] ModelingSelectionExport export_selection() const {
    if (sdk_selection_active()) {
      ModelingSelectionExport exported =
          sdk_projection_adapter_.export_selection();
      exported.summary.content_revision = content_revision();
      exported.summary.selection_revision = selection_revision();
      return exported;
    }
    ModelingSelectionExport exported;
    exported.summary = selection_summary();
    const ModelingSelectionProjectionSnapshot &projected =
        selection_projection_snapshot();

    const std::vector<EntityId> selected_scene_entities =
        scene_.selected_entities();
    std::unordered_set<EntityId> selected_scene_entity_set;
    selected_scene_entity_set.reserve(selected_scene_entities.size());
    for (const EntityId entity : selected_scene_entities) {
      selected_scene_entity_set.insert(entity);
    }

    for (const quader_editor_core::PolygonObject &object :
         projected.objects()) {
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

  [[nodiscard]] ModelingSelectedVerticesSnapshot
  selected_vertices_snapshot() const {
    return sdk_projection_adapter_.selected_vertices_snapshot();
  }

  ModelingCommandResult apply_resolved_vertex_positions(
      std::span<const ModelingResolvedVertexPositionBatch> batches) {
    return sdk_projection_adapter_.apply_resolved_vertex_positions(batches);
  }

  [[nodiscard]] ModelingSelectionBoxBaseline
  capture_box_selection_baseline() const {
    QDR_PROFILE_SCOPE("Modeling session capture box selection baseline");
    const ModelingSelectionProjectionSnapshot &projected = [&]()
        -> const ModelingSelectionProjectionSnapshot & {
      QDR_PROFILE_SCOPE("Box selection acquire session baseline projection");
      return selection_projection_snapshot();
    }();
    {
      QDR_PROFILE_SCOPE("Box selection capture session runtime baseline");
      return ModelingSelectionBoxBaseline(
          std::make_shared<ModelingSelectionBoxBaseline::Impl>(
              selection_service_.rectangle_selection_state(projected)));
    }
  }

  [[nodiscard]] std::uint64_t selection_overlay_signature(
      ModelingSelectionOverlayOptions options = {}) const {
    options.suppress_persistent_selection =
        options.suppress_persistent_selection ||
        active_scene_selection_is_owned_outside_modeling();
    options.persistent_source_wire_object_id =
        persistent_tool_source_wire_object_id(selection_projection_snapshot());
    return overlay_service_.selection_overlay_signature(runtime_, options);
  }

  [[nodiscard]] std::optional<ModelingTransform3>
  selection_transform_orientation() const {
    const ModelingSelectionMode mode = selection_mode();
    if (mode == ModelingSelectionMode::Object ||
        mode == ModelingSelectionMode::Group) {
      const EntityId active = scene_.primary_selection();
      if (active == kInvalidEntityId || !scene_.exists(active)) {
        return std::nullopt;
      }
      if (const std::optional<int> object_id =
              object_registry_.modeling_object_for_entity(active)) {
        const std::optional<QMat4> display_world =
            modeling_object_world_transform(*object_id);
        return display_world.has_value()
                   ? modeling_orientation_from_world_matrix(*display_world)
                   : std::nullopt;
      }

      QMat4 display_world = scene_.world_transform_matrix(active);
      const ModelingInstanceEditScope edit_scope =
          instance_edit_scope_provider_ ? instance_edit_scope_provider_()
                                        : ModelingInstanceEditScope{};
      if (edit_scope.definition != kInvalidEntityId &&
          scene_.containing_instance_definition(active) ==
              edit_scope.definition) {
        display_world = scene_transform_multiply(
            scene_selection_display_from_scene(), display_world);
      }
      return modeling_orientation_from_world_matrix(display_world);
    }

    const ModelingSelectionProjectionSnapshot &projection =
        selection_projection_snapshot();
    const quader_editor_core::ActiveSelection active =
        projection.active_selection();
    if (!active.has_active || active.object_id < 0) {
      return std::nullopt;
    }
    const auto object = std::ranges::find_if(
        projection.objects(), [&](const quader_editor_core::PolygonObject &item) {
          return item.id == active.object_id;
        });
    if (object == projection.objects().end()) {
      return std::nullopt;
    }

    std::optional<quader_poly::Transform3> local_orientation;
    switch (active.kind) {
    case quader_editor_core::ActiveSelectionKind::Vertex:
      local_orientation =
          quader_editor_core::component_vertex_local_orientation(
              object->document.read(), active.vertex_id);
      break;
    case quader_editor_core::ActiveSelectionKind::Edge:
      local_orientation = quader_editor_core::component_edge_local_orientation(
          object->document.read(), active.edge);
      break;
    case quader_editor_core::ActiveSelectionKind::Face:
      local_orientation = quader_editor_core::component_face_local_orientation(
          object->document.read(), active.face_id);
      break;
    case quader_editor_core::ActiveSelectionKind::Object:
    case quader_editor_core::ActiveSelectionKind::None:
      return std::nullopt;
    }
    if (!local_orientation.has_value()) {
      return std::nullopt;
    }

    const std::optional<QMat4> world =
        modeling_object_world_transform(active.object_id);
    return world.has_value()
               ? modeling_component_world_orientation(*local_orientation,
                                                       *world)
               : std::nullopt;
  }

  [[nodiscard]] ViewportOverlayFrame
  selection_overlay_frame(ModelingSelectionOverlayOptions options = {}) const {
    options.suppress_persistent_selection =
        options.suppress_persistent_selection ||
        active_scene_selection_is_owned_outside_modeling();
    const ModelingSelectionProjectionSnapshot &projected =
        selection_projection_snapshot();
    options.persistent_source_wire_object_id =
        persistent_tool_source_wire_object_id(projected);
    return overlay_service_.selection_overlay_frame(
        projected, runtime_, scene_, object_registry_, selection_service_,
        sdk_projection_adapter_, options);
  }

  [[nodiscard]] ModelingVolatileOverlaySnapshot
  volatile_overlay_snapshot() const {
    const VolatileOverlaySourceKey source_key{
        .content_revision = content_revision(),
        .selection_revision = selection_revision(),
        .scene_revision = scene_.revision(),
        .local_visibility_revision =
            selection_service_.viewport_visibility_revision(),
        .object_map_revision = object_registry_.revision(),
        .asset_revision = assets_.revision(),
        .transform_preview_revision =
            sdk_projection_adapter_.workspace_transform_preview_revision(),
        .runtime_overlay_revision = volatile_overlay_runtime_revision_,
        .app_selection_mode = app_selection_mode_,
        .instance_edit_scope =
            selection_service_.overlay_instance_edit_scope(),
    };
    if (volatile_overlay_cache_.valid &&
        volatile_overlay_cache_.key == source_key &&
        volatile_overlay_cache_.frame != nullptr) {
      ++volatile_overlay_cache_.reuse_count;
      return {
          .frame = volatile_overlay_cache_.frame,
          .source = {
              .source_identity = reinterpret_cast<std::uintptr_t>(this),
              .publication_revision =
                  volatile_overlay_cache_.publication_revision,
          },
      };
    }
    const ModelingSelectionProjectionSnapshot &projected =
        selection_projection_snapshot();
    ViewportOverlayFrame frame = overlay_service_.volatile_overlay_frame(
        projected, runtime_, scene_, object_registry_, selection_service_,
        sdk_projection_adapter_,
        persistent_tool_source_wire_object_id(projected));
    volatile_overlay_cache_.key = source_key;
    volatile_overlay_cache_.frame =
        std::make_shared<const ViewportOverlayFrame>(std::move(frame));
    volatile_overlay_cache_.valid = true;
    ++volatile_overlay_cache_.publication_revision;
    ++volatile_overlay_cache_.build_count;
    return {
        .frame = volatile_overlay_cache_.frame,
        .source = {
            .source_identity = reinterpret_cast<std::uintptr_t>(this),
            .publication_revision =
                volatile_overlay_cache_.publication_revision,
        },
    };
  }

  [[nodiscard]] ViewportOverlayFrame volatile_overlay_frame() const {
    const ModelingVolatileOverlaySnapshot snapshot = volatile_overlay_snapshot();
    return snapshot.frame != nullptr ? *snapshot.frame : ViewportOverlayFrame{};
  }

  [[nodiscard]] ModelingVolatileOverlayCacheStats
  volatile_overlay_cache_stats() const {
    return {
        .build_count = volatile_overlay_cache_.build_count,
        .reuse_count = volatile_overlay_cache_.reuse_count,
    };
  }

  [[nodiscard]] std::uint64_t tool_preview_source_revision() const {
    return volatile_overlay_runtime_revision_;
  }

  [[nodiscard]] std::uint64_t tool_preview_signature() const {
    ++tool_preview_work_stats_.signature_enumeration_count;
    return overlay_service_.tool_preview_signature(runtime_);
  }

  [[nodiscard]] std::vector<ModelingToolPreviewMesh>
  tool_preview_meshes() const {
    ++tool_preview_work_stats_.mesh_extraction_count;
    return overlay_service_.tool_preview_meshes(runtime_,
                                                sdk_projection_adapter_);
  }

  [[nodiscard]] ModelingToolPreviewWorkStats
  tool_preview_work_stats() const {
    return tool_preview_work_stats_;
  }

  [[nodiscard]] bool transient_transform_preview_active() const override {
    return sdk_projection_adapter_.workspace_transform_preview_active();
  }

  [[nodiscard]] std::uint64_t transient_transform_preview_revision() const {
    return sdk_projection_adapter_.workspace_transform_preview_revision();
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
  rectangle_selection_universe_build_count() const {
    return selection_service_.rectangle_selection_universe_build_count();
  }

  [[nodiscard]] std::uint64_t
  rectangle_selection_resolution_build_count() const {
    return selection_service_.rectangle_selection_resolution_build_count();
  }

  [[nodiscard]] std::uint64_t local_face_geometry_build_count() const {
    return selection_service_.local_face_geometry_build_count();
  }

  [[nodiscard]] std::uint64_t world_pick_bounds_build_count() const {
    return selection_service_.world_pick_bounds_build_count();
  }

  [[nodiscard]] std::uint64_t mesh_snap_query_count() const {
    return mesh_snap_query_count_;
  }

  [[nodiscard]] const void *modeling_sdk_session_identity_for_tests() const {
    return sdk_projection_adapter_.session_identity();
  }

  [[nodiscard]] std::size_t modeling_sdk_object_count_for_tests() const {
    return sdk_projection_adapter_.object_count();
  }

  void set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint point) {
    sdk_projection_adapter_.set_failure_point_for_tests(point);
    volatile_overlay_cache_.valid = false;
  }

  [[nodiscard]] ModelingSdkProjectionStats
  modeling_sdk_projection_stats_for_tests() const {
    return sdk_projection_adapter_.stats();
  }

  void release_redundant_sdk_history() {
    sdk_projection_adapter_.release_redundant_sdk_history();
  }

  [[nodiscard]] ModelingSessionMemento capture_history_memento() const override {
    return capture_history_memento(false);
  }

  [[nodiscard]] ModelingSessionMemento capture_history_memento(
      bool preserve_group_selection) const {
    auto state = std::make_shared<ModelingSessionMemento::Impl>(
        sdk_projection_adapter_.capture_snapshot());
    if (preserve_group_selection && selection_mode() == ModelingSelectionMode::Group) {
      state->group_selection = document_command_capture_selection(scene_);
    }
    return ModelingSessionMemento(std::move(state));
  }

  [[nodiscard]] ModelingSessionMemento capture_history_memento_without_objects(
      const ModelingSdkProjectionSnapshot &sdk_baseline,
      std::span<const int> excluded_object_ids) const {
    (void)excluded_object_ids;
    const ModelingSdkProjectionSnapshot rebased_sdk_baseline =
        sdk_projection_adapter_.rebase_snapshot_host_projection(sdk_baseline);
    return rebased_sdk_baseline.valid()
               ? ModelingSessionMemento(
                     std::make_shared<ModelingSessionMemento::Impl>(
                         rebased_sdk_baseline))
               : ModelingSessionMemento{};
  }

  [[nodiscard]] ModelingSessionMemento
  capture_active_preview_history_memento() const {
    if (sdk_projection_adapter_.preview_active()) {
      const ModelingSdkProjectionSnapshot sdk_base =
          sdk_projection_adapter_.capture_active_preview_base_snapshot();
      return sdk_base.valid()
                 ? ModelingSessionMemento(
                       std::make_shared<ModelingSessionMemento::Impl>(sdk_base))
                 : ModelingSessionMemento{};
    }
    return {};
  }

  ModelingCommandResult restore_history_memento_impl(
      const ModelingSessionMemento &memento,
      bool defer_scene_hierarchy) {
    if (!memento.valid()) {
      return {
          .ok = false,
          .changed = false,
          .message = "Modeling history restore point is invalid.",
      };
    }

    const ModelingCommandResult sdk_restored =
        defer_scene_hierarchy
            ? sdk_projection_adapter_.restore_snapshot_deferred_scene_hierarchy(
                  memento.impl_->sdk_projection)
            : sdk_projection_adapter_.restore_snapshot(
                  memento.impl_->sdk_projection);
    if (!sdk_restored.ok) {
      return {
          .ok = false,
          .changed = sdk_restored.changed,
          .message = sdk_restored.message.empty()
                         ? "SDK modeling history state could not be restored."
                         : sdk_restored.message,
      };
    }
    if (memento.impl_->group_selection.has_value()) {
      document_command_apply_selection(scene_, *memento.impl_->group_selection);
      app_selection_mode_ = ModelingSelectionMode::Group;
    }
    face_texture_context_.invalidate();
    uv_peel_context_.invalidate_selection();
    invalidate_selection_overlay_caches();
    return {
        .ok = true,
        .changed = true,
    };
  }

  ModelingCommandResult restore_history_memento_preserving_scene_selection(
      const ModelingSessionMemento &memento) {
    if (!memento.valid()) {
      return {
          .ok = false,
          .changed = false,
          .message = "Modeling history restore point is invalid.",
      };
    }

    const ModelingCommandResult sdk_restored =
        sdk_projection_adapter_.restore_snapshot_preserving_scene_selection(
            memento.impl_->sdk_projection);
    if (!sdk_restored.ok) {
      return {
          .ok = false,
          .changed = true,
          .message = sdk_restored.message.empty()
                         ? "SDK modeling history state could not be restored."
                         : sdk_restored.message,
      };
    }
    if (memento.impl_->group_selection.has_value()) {
      app_selection_mode_ = ModelingSelectionMode::Group;
    }
    face_texture_context_.invalidate();
    uv_peel_context_.invalidate_selection();
    invalidate_selection_overlay_caches();
    return {
        .ok = true,
        .changed = true,
    };
  }

  ModelingCommandResult
  restore_history_memento(const ModelingSessionMemento &memento) {
    return restore_history_memento_impl(memento, false);
  }

  ModelingCommandResult restore_history_memento_deferred_scene_hierarchy(
      const ModelingSessionMemento &memento) {
    return restore_history_memento_impl(memento, true);
  }

  void clear_transient_state_for_document_replacement() {
    face_texture_context_.invalidate();
    uv_peel_context_.invalidate_selection();
    if (transform_drag_active()) {
      (void)cancel_transform_drag();
    }
    if (sdk_projection_adapter_.preview_active() ||
        structural_preview_transaction_.has_value()) {
      (void)cancel_transform_preview();
    }

    (void)cancel_active_modal_tool();
    (void)runtime_.discard_transient_state();
    tool_settings_service_.clear_preview_metadata();
    invalidate_selection_overlay_caches();
  }

  void reset_from_scene() {
    face_texture_context_.invalidate();
    uv_peel_context_.invalidate_selection();
    // Group selection is scene-owned even though the polygon runtime represents
    // its mode as Object. Keep that runtime projection empty so reset cannot
    // introduce a second selection source of truth.
    const bool preserve_scene_owned_group_selection =
        app_selection_mode_ == ModelingSelectionMode::Group;
    if (!preserve_scene_owned_group_selection) {
      app_selection_mode_ = ModelingSelectionMode::Object;
    }

    sdk_projection_adapter_.reset_from_scene();
    if (preserve_scene_owned_group_selection) {
      (void)sdk_projection_adapter_.set_selection_mode(
          ModelingSelectionMode::Group);
    }
    runtime_.reset_document_state();
    // reset_from_scene hydrates SDK workspace selection and the active object
    // directly from the same SceneDocument snapshot. Re-projecting it here
    // would duplicate the owner transition and introduce a second failure
    // policy into document replacement.
    invalidate_selection_overlay_caches();
  }

  [[nodiscard]] ModelingCommandResult
  restore_mesh_documents_from_scene(std::span<const EntityId> entities) {
    ModelingCommandResult restored =
        sdk_projection_adapter_.restore_mesh_documents_from_scene(entities);
    if (restored.ok) {
      runtime_.reset_document_state();
      invalidate_selection_overlay_caches();
    }
    return restored;
  }

  void set_instance_edit_scope_provider(
      std::function<ModelingInstanceEditScope()> provider) {
    instance_edit_scope_provider_ = provider;
    selection_service_.set_instance_edit_scope_provider(std::move(provider));
    volatile_overlay_cache_.valid = false;
  }

  [[nodiscard]] ModelingInstanceEditScope instance_edit_scope() const {
    return instance_edit_scope_provider_ ? instance_edit_scope_provider_()
                                        : ModelingInstanceEditScope{};
  }

  void set_viewport_visibility_filter(
      std::function<bool(EntityId)> filter,
      std::function<std::uint64_t()> revision_provider,
      std::function<bool()> active_provider) {
    selection_service_.set_viewport_visibility_filter(
        std::move(filter), std::move(revision_provider),
        std::move(active_provider));
    volatile_overlay_cache_.valid = false;
  }

  [[nodiscard]] ModelingCommandResult
  sync_object_selection_from_scene(bool force_object_mode = false) {
    QDR_PROFILE_FUNCTION();
    std::vector<EntityId> sdk_selected;
    for (const EntityId entity : scene_.selected_entities()) {
      if (sdk_projection_adapter_.owns_entity(entity)) {
        sdk_selected.push_back(entity);
      }
    }
    const EntityId scene_active = scene_.primary_selection();
    ModelingCommandResult projected =
        sdk_projection_adapter_.project_object_selection_from_scene(
            sdk_selected,
            sdk_projection_adapter_.owns_entity(scene_active)
                ? scene_active
                : (sdk_selected.empty() ? kInvalidEntityId
                                        : sdk_selected.front()));
    if (projected.ok && (force_object_mode || !sdk_selected.empty())) {
      app_selection_mode_ = ModelingSelectionMode::Object;
    }
    if (projected.ok && projected.changed) {
      invalidate_selection_overlay_caches();
    }
    return projected;
  }

  [[nodiscard]] ModelingCommandResult
  clear_runtime_object_selection_for_scene_owned_selection() {
    QDR_PROFILE_FUNCTION();
    ModelingCommandResult cleared =
        sdk_projection_adapter_.clear_object_selection_preserving_mode();
    if (cleared.changed) {
      invalidate_selection_overlay_caches();
    }
    return cleared;
  }

  ModelingCommandResult restore_history_memento_exact(
      const ModelingSessionMemento &memento,
      bool preserve_scene_selection = false,
      bool defer_scene_hierarchy = false) {
    if (!memento.valid()) {
      return {
          .ok = false,
          .changed = false,
          .message = "Modeling history restore point is invalid.",
      };
    }

    ModelingCommandResult sdk_restored;
    if (preserve_scene_selection) {
      sdk_restored =
          sdk_projection_adapter_.restore_snapshot_exact_preserving_scene_selection(
              memento.impl_->sdk_projection);
    } else if (defer_scene_hierarchy) {
      sdk_restored =
          sdk_projection_adapter_.restore_snapshot_exact_deferred_scene_hierarchy(
              memento.impl_->sdk_projection);
    } else {
      sdk_restored = sdk_projection_adapter_.restore_snapshot_exact(
          memento.impl_->sdk_projection);
    }
    if (!sdk_restored.ok) {
      return {
          .ok = false,
          .changed = sdk_restored.changed,
          .message = sdk_restored.message.empty()
                         ? "SDK modeling history state could not be restored "
                           "exactly."
                         : sdk_restored.message,
      };
    }
    if (!preserve_scene_selection &&
        memento.impl_->group_selection.has_value()) {
      document_command_apply_selection(scene_, *memento.impl_->group_selection);
    }
    if (memento.impl_->group_selection.has_value()) {
      app_selection_mode_ = ModelingSelectionMode::Group;
    }
    face_texture_context_.invalidate();
    uv_peel_context_.invalidate_selection();
    invalidate_selection_overlay_caches();
    return {.ok = true, .changed = true};
  }

  void promote_drawn_plane_tool_object_selection() {
    QDR_PROFILE_FUNCTION();
    const ModelingCommandResult promoted =
        sdk_projection_adapter_.set_selection_mode(
            ModelingSelectionMode::Object);
    if (promoted.ok) {
      app_selection_mode_ = ModelingSelectionMode::Object;
      sdk_projection_adapter_.add_selection_to_scene();
      invalidate_selection_overlay_caches();
    }
  }

  void sync_scene_selection_from_modeling() {
    QDR_PROFILE_FUNCTION();
    std::vector<EntityId> selected =
        sdk_projection_adapter_.selected_entities();
    for (const EntityId entity : scene_.selected_entities()) {
      if (!sdk_projection_adapter_.owns_entity(entity) &&
          std::find(selected.begin(), selected.end(), entity) ==
              selected.end()) {
        selected.push_back(entity);
      }
    }
    EntityId active = sdk_projection_adapter_.active_selected_entity();
    if (active == kInvalidEntityId &&
        !sdk_projection_adapter_.owns_entity(scene_.primary_selection())) {
      active = scene_.primary_selection();
    }
    selection_projection_service_.restore_scene_selection(scene_, selected,
                                                          active);
  }

  void refresh_transform_drag_projection() override {
    refresh_transform_drag_projection_cache();
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
  [[nodiscard]] std::vector<ModelingObjectSceneIdentity>
  capture_selected_duplicate_scene_identities() const {
    QDR_PROFILE_FUNCTION();
    std::vector<int> selected_object_ids;
    const ModelingSelectionExport selection =
        sdk_projection_adapter_.export_selection();
    for (const ModelingSelectionExportObject &object : selection.objects) {
      if (object.selected) {
        selected_object_ids.push_back(object.modeling_object_id);
      }
    }
    return object_registry_.capture_scene_identities_for_modeling_objects(
        scene_, std::span<const int>(selected_object_ids.data(),
                                     selected_object_ids.size()));
  }

  ModelingHierarchyDuplicateResult duplicate_entity_hierarchy(
      std::span<const EntityId> roots,
      DocumentCommandService &document_commands,
      ModelingSelectionMode result_selection_mode,
      bool independent_mesh_assets = false) {
    const ModelingSelectionProjectionSnapshot &projection =
        selection_projection_snapshot();
    return hierarchy_duplicate_service_.duplicate_entity_hierarchy(
        projection, scene_, object_registry_, document_commands,
        ModelingHierarchyDuplicateRuntime{
            .ensure_scene_objects_synced = []() {},
            .selection_mode = [&]() { return selection_mode(); },
            .set_selection_mode =
                [&](ModelingSelectionMode mode) {
                  return set_selection_mode(mode);
                },
            .duplicate_modeling_objects =
                [&](std::span<const EntityId> source_meshes) {
                  std::vector<int> source_object_ids;
                  source_object_ids.reserve(source_meshes.size());
                  for (const EntityId entity : source_meshes) {
                    const std::optional<int> object_id =
                        object_registry_.modeling_object_for_entity(entity);
                    if (!object_id.has_value()) {
                      return ModelingHierarchyDuplicateBatchResult{
                          .command =
                              {.ok = false,
                               .changed = false,
                               .message = "Hierarchy mesh has no modeling "
                                          "object identity."},
                      };
                    }
                    source_object_ids.push_back(*object_id);
                  }
                  ModelingSdkDuplicateBatchResult duplicated =
                      sdk_projection_adapter_
                          .duplicate_objects(source_object_ids, 1U, false,
                                             !independent_mesh_assets);
                  ModelingHierarchyDuplicateBatchResult result{
                      .command = std::move(duplicated.command),
                  };
                  result.duplicate_entities.reserve(
                      duplicated.duplicates.size());
                  for (const ModelingObjectRecord &record :
                       duplicated.duplicates) {
                    result.duplicate_entities.push_back(record.entity);
                  }
                  return result;
                },
            .detach_duplicate_mesh = [&](EntityId entity) {
              return mesh_sync_service_.parent_projected_entity(
                  scene_, entity, kInvalidEntityId);
            },
        },
        roots, result_selection_mode);
  }

  ModelingCommandResult
  duplicate_selected_hierarchy(DocumentCommandService &document_commands,
                               ModelingSelectionMode result_selection_mode,
                               bool independent_mesh_assets = false) {
    const std::vector<EntityId> roots =
        document_command_selected_root_entities(scene_);
    return duplicate_entity_hierarchy(roots, document_commands,
                                      result_selection_mode,
                                      independent_mesh_assets);
  }

  ModelingCommandResult
  duplicate_selected_entity_hierarchy_with_independent_meshes(
      DocumentCommandService &document_commands) {
    return duplicate_selected_hierarchy(document_commands,
                                        ModelingSelectionMode::Object, true);
  }

  ModelingCommandResult set_selection_mode(ModelingSelectionMode mode) {
    bool canceled_preview = false;
    const auto absorb_cancel = [&canceled_preview](
                                   ModelingCommandResult result) {
      canceled_preview = canceled_preview || result.changed;
      return result;
    };
    if (operation_preview_active()) {
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
          sdk_projection_adapter_.set_selection_mode(mode);
      if (result.ok) {
        app_selection_mode_ = mode;
      }
      result.changed = result.changed || canceled_preview ||
                       removed_instance_scope_selection;
      if (result.changed) {
        invalidate_selection_overlay_caches();
      }
      return result;
    }

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
      ModelingCommandResult synchronized = sync_object_selection_from_scene();
      if (!synchronized.ok) {
        synchronized.changed = synchronized.changed || canceled_preview;
        return synchronized;
      }
    }

    ModelingCommandResult result =
        sdk_projection_adapter_.set_selection_mode(mode);
    if (result.ok) {
      app_selection_mode_ = mode;
    }
    result.changed = result.changed || canceled_preview;
    invalidate_selection_overlay_caches();
    if (mode == ModelingSelectionMode::Object &&
        !scene_only_selection.empty()) {
      ModelingCommandResult synchronized = sync_object_selection_from_scene();
      if (!synchronized.ok) {
        synchronized.changed = synchronized.changed || result.changed;
        return synchronized;
      }
    }
    sync_scene_selection_from_modeling();
    selection_projection_service_.add_scene_selection(
        scene_, scene_only_selection, active_scene_only_selection);
    return result;
  }

  ModelingCommandResult select_all() {
    const ModelingSelectionProjectionSnapshot &projection =
        selection_projection_snapshot();
    if (const std::optional<ModelingSelectionProjectionCommandResult> scoped =
            selection_service_.select_all_in_constrained_component_scope(
                projection, scene_, object_registry_,
                runtime_.hover().candidate_object_id)) {
      if (!scoped->command.ok) {
        return scoped->command;
      }
      ModelingCommandResult published =
          sdk_projection_adapter_.apply_selection_projection_checked(
              scoped->projection);
      if (published.ok && published.changed) {
        invalidate_selection_overlay_caches();
      }
      if (published.message.empty()) {
        published.message = scoped->command.message;
      }
      return published;
    }
    ModelingCommandResult result = sdk_projection_adapter_.select_all();
    if (result.ok && result.changed) {
      invalidate_selection_overlay_caches();
    }
    return result;
  }

  ModelingCommandResult invert_selection() {
    if (selection_mode_uses_scene_document(app_selection_mode_)) {
      return {
          .ok = false,
          .changed = false,
          .message = "Object and Group selection inversion must be routed "
                     "through EditorSelectionService.",
      };
    }

    const ModelingSelectionProjectionSnapshot &projection =
        selection_projection_snapshot();
    if (projection.selection_mode() ==
        quader_editor_core::SelectionMode::Object) {
      return {
          .ok = false,
          .changed = false,
          .message =
              "Component selection inversion requires a component mode.",
      };
    }
    const std::vector<ModelingComponentSelectionUniverse> universes =
        selection_service_.component_selection_universes(
            projection, scene_, object_registry_);
    ModelingCommandResult result =
        sdk_projection_adapter_.invert_selection(universes);
    if (result.ok && result.changed) {
      invalidate_selection_overlay_caches();
    }
    return result;
  }

  ModelingCommandResult clear_selection() {
    ModelingCommandResult result = sdk_projection_adapter_.clear_selection();
    if (result.ok && result.changed) {
      invalidate_selection_overlay_caches();
    }
    return result;
  }

  ModelingCommandResult apply_component_selection_command(
      ModelingComponentSelectionCommand command) {
    const ModelingSelectionProjectionSnapshot &projection =
        selection_projection_snapshot();
    const ModelingSelectionProjectionApplyResult applied =
        selection_service_.apply_component_selection_command(
            projection, scene_, object_registry_, command);
    if (!applied.applied) {
      return {
          .ok = false,
          .changed = false,
          .message = "The component selection command could not be applied.",
      };
    }
    if (applied.changed) {
      ModelingCommandResult published =
          sdk_projection_adapter_.apply_selection_projection_checked(
              applied.projection);
      if (!published.ok) {
        return published;
      }
      if (published.changed) {
        invalidate_selection_overlay_caches();
      }
      return published;
    }
    return {
        .ok = true,
        .changed = applied.changed,
        .message = applied.changed ? "Selection updated."
                                   : "Selection unchanged.",
    };
  }

  ModelingCommandResult
  select_mesh_ngons(std::span<const int> modeling_object_ids) {
    ModelingCommandResult result =
        sdk_projection_adapter_.select_mesh_ngons(modeling_object_ids);
    if (result.ok) {
      app_selection_mode_ = ModelingSelectionMode::Face;
      invalidate_selection_overlay_caches();
    }
    return result;
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
    return execute_runtime_operation(
        quader_editor_core::EditorOperationId::DeleteSelection,
        quader_editor_core::SelectionOperationSettings{});
  }

  [[nodiscard]] bool has_selected_scene_aware_delete_root() const {
    return scene_aware_delete_has_selected_root(scene_);
  }

  SceneAwareDeleteRuntime scene_aware_delete_runtime() {
    return SceneAwareDeleteRuntime{
            .capture_modeling_memento =
                [&]() { return capture_history_memento(); },
            .restore_modeling_memento =
                [&](const ModelingSessionMemento &memento) {
                  return restore_history_memento_deferred_scene_hierarchy(
                      memento);
                },
            .restore_modeling_memento_exact =
                [&](const ModelingSessionMemento &memento) {
                  return restore_history_memento_exact(memento, false, true);
                },
            .app_selection_mode = [&]() { return app_selection_mode_; },
            .set_app_selection_mode =
                [&](ModelingSelectionMode mode) { app_selection_mode_ = mode; },
            .delete_modeling_objects =
                [&](const std::vector<int> &modeling_object_ids) {
                  return sdk_projection_adapter_.remove_objects(
                      modeling_object_ids);
                },
            .destroy_scene_entities =
                [&](const std::vector<EntityId> &entities) {
                  return scene_.destroy_entities(entities);
                },
            .entity_for_modeling_object =
                [&](int modeling_object_id) {
                  return object_registry_.entity_for_modeling_object(
                      modeling_object_id);
                },
            .sync_object_selection_from_scene =
                [&](bool force_object_mode) {
                  return sync_object_selection_from_scene(force_object_mode);
                },
            .clear_runtime_object_selection_for_group_mode =
                [&]() {
                  return clear_runtime_object_selection_for_scene_owned_selection();
                },
            .invalidate_selection_overlay_caches =
                [&]() { invalidate_selection_overlay_caches(); },
        };
  }

  std::unique_ptr<IUndoableCommand> create_delete_selection_command() {
    return create_scene_aware_delete_selection_command(
        scene_, scene_aware_delete_runtime());
  }

  std::unique_ptr<IUndoableCommand>
  create_destroy_entities_command(std::vector<EntityId> roots,
                                  std::string label) {
    return create_scene_aware_destroy_entities_command(
        scene_, scene_aware_delete_runtime(), std::move(roots),
        std::move(label));
  }

  ModelingCommandResult copy_selection() {
    ModelingSdkClipboardCopyResult copied =
        sdk_projection_adapter_.copy_selection_to_clipboard();
    if (copied.command.ok) {
      clipboard_scene_identities_ =
          object_registry_.capture_scene_identities_for_modeling_objects(
              scene_, copied.source_modeling_object_ids);
      clipboard_world_center_ = selection_center();
    }
    return copied.command;
  }

  ModelingCommandResult paste_selection() {
    const std::optional<QVec3> paste_target = selection_center();
    const ModelingSessionMemento rollback = capture_history_memento();
    const auto rollback_failure = [&](std::string message) {
      const ModelingCommandResult restored = restore_history_memento(rollback);
      return ModelingCommandResult{
          .ok = false,
          .changed = !restored.ok,
          .message = restored.ok
                         ? std::move(message)
                         : std::move(message) +
                               " The pre-paste workspace could not be "
                               "restored.",
      };
    };
    ModelingSdkDuplicateBatchResult pasted =
        sdk_projection_adapter_.paste_clipboard(clipboard_scene_identities_);
    if (!pasted.command.ok || !pasted.command.changed) {
      return pasted.command;
    }
    if (!clipboard_scene_identities_.empty()) {
      const QVec3 paste_delta =
          paste_target.has_value() && clipboard_world_center_.has_value()
              ? *paste_target - *clipboard_world_center_
              : QVec3{1.0f, 0.0f, 1.0f};
      const DocumentCommandResult placed =
          translate_scene_selection_by_world_delta(paste_delta,
                                                   "Place Pasted Meshes");
      if (!placed.ok) {
        return rollback_failure(placed.message);
      }
    }
    invalidate_selection_overlay_caches();
    return {
        .ok = pasted.command.ok,
        .changed = true,
        .message = pasted.command.message,
    };
  }

  ModelingCommandResult repeat_last_action() {
    const quader_editor_core::CommandResult result =
        execute_repeat_last_action_scene_aware();
    if (result.changed) {
      invalidate_selection_overlay_caches();
    }
    return {
        .ok = result.ok,
        .changed = result.changed,
        .message = result.message,
    };
  }

  void record_repeatable_structural_duplicate() {
    const std::optional<quader_editor_core::EditorOperationCommand> command =
        modeling_instant_operation_command(
            ModelingInstantOperationId::DuplicateMeshes,
            interaction_settings_service_.settings());
    if (!command.has_value()) {
      return;
    }
    history_bridge_.record_operation(
        *command,
        {.ok = true, .changed = true, .message = "Selection duplicated."});
  }

  [[nodiscard]] bool repeat_last_action_uses_structural_duplicate() const {
    const quader_editor_core::RepeatLastActionState &repeat =
        history_bridge_.repeat_last_action();
    const bool structural_operation =
        repeat.last_action_kind ==
            quader_editor_core::RepeatableActionKind::Operation &&
        repeat.last_operation.has_value() &&
        repeat.last_operation->operation_id ==
            quader_editor_core::EditorOperationId::DuplicateMeshes;
    const bool structural_transform =
        repeat.last_action_kind ==
            quader_editor_core::RepeatableActionKind::Transform &&
        repeat.last_transform.has_value() &&
        repeat.last_transform->duplicate_selected;
    if (!structural_operation && !structural_transform) {
      return false;
    }
    const ModelingSelectionMode mode = selection_mode();
    return mode == ModelingSelectionMode::Object ||
           mode == ModelingSelectionMode::Group;
  }

  ModelingCommandResult repeat_last_structural_transform(
      EditorTransaction &transaction, EditorHistoryService &history,
      const ModelingStructuralDuplicate &structural_duplicate) {
    quader_editor_core::RepeatLastActionState &repeat =
        history_bridge_.repeat_last_action();
    if (!repeat_last_action_uses_structural_duplicate()) {
      return {
          .ok = false,
          .changed = false,
          .message = "No structural duplicate action is available.",
      };
    }

    const quader_editor_core::RepeatLastActionState repeated_action = repeat;
    if (repeat.last_action_kind ==
            quader_editor_core::RepeatableActionKind::Operation &&
        repeat.last_operation.has_value() &&
        repeat.last_operation->operation_id ==
            quader_editor_core::EditorOperationId::DuplicateMeshes) {
      const ModelingStructuralDuplicateResult duplicated =
          structural_duplicate(transaction, selection_mode(),
                               "Repeat Last Action");
      repeat = repeated_action;
      if (duplicated.changed) {
        invalidate_selection_overlay_caches();
      }
      return {
          .ok = duplicated.ok,
          .changed = duplicated.changed,
          .message = duplicated.message,
      };
    }
    const quader_editor_core::RepeatTransformAction transform_action =
        *repeat.last_transform;
    const quader_editor_core::CommandResult result =
        execute_repeat_transform_scene_aware(
            transform_action, &transaction, &history, &structural_duplicate);
    repeat = repeated_action;
    if (result.changed) {
      invalidate_selection_overlay_caches();
    }
    return {
        .ok = result.ok,
        .changed = result.changed,
        .message = result.message,
    };
  }

  [[nodiscard]] bool has_face_texture_repeat() const {
    const auto &repeat = history_bridge_.repeat_last_action();
    return repeat.last_action_kind ==
               quader_editor_core::RepeatableActionKind::FaceTexture &&
           repeat.last_face_texture.has_value();
  }

  ModelingCommandResult mirror_selection(ModelingMirrorAxis axis) {
    QVec3 normal;
    switch (axis) {
    case ModelingMirrorAxis::X:
      normal = {1.0F, 0.0F, 0.0F};
      break;
    case ModelingMirrorAxis::Y:
      normal = {0.0F, 1.0F, 0.0F};
      break;
    case ModelingMirrorAxis::Z:
      normal = {0.0F, 0.0F, 1.0F};
      break;
    }
    ModelingCommandResult result =
        sdk_projection_adapter_.mirror_selected_objects_across_plane({},
                                                                     normal);
    if (result.ok && result.changed) {
      invalidate_selection_overlay_caches();
    }
    return result;
  }

  ModelingCommandResult flip_selection(ModelingFlipOrientation orientation) {
    ModelingCommandResult result =
        sdk_projection_adapter_.flip_selected_objects(orientation);
    if (result.ok && result.changed) {
      invalidate_selection_overlay_caches();
    }
    return result;
  }

  ModelingCommandResult apply_active_material(std::uint32_t material_slot) {
    if (selection_mode() == ModelingSelectionMode::Group) {
      const MaterialSurfaceTargets targets = selected_material_surface_targets();
      if (!targets.command.ok) {
        return targets.command;
      }
      return sdk_projection_adapter_.assign_material_slot_to_objects(
          targets.mesh_objects, material_slot, true);
    }
    quader_editor_core::AssignMaterialOperationSettings settings;
    settings.material_slot = material_slot;
    return execute_runtime_operation(
        quader_editor_core::EditorOperationId::AssignMaterial, settings);
  }

  [[nodiscard]] ModelingToolStatus tool_status() const {
    return overlay_service_.tool_status(runtime_, modal_tool_owns_viewport());
  }

  [[nodiscard]] ModelingLiveStatus live_status() const {
    if (sdk_projection_adapter_.active_preview_operation() ==
        quader_editor_core::EditorOperationId::MergeByDistance) {
      return {
          .kind = ModelingLiveStatusKind::MergeByDistanceMergedVertices,
          .count = static_cast<std::uint32_t>(std::min<std::size_t>(
              sdk_projection_adapter_.active_preview_deleted_vertex_count(),
              std::numeric_limits<std::uint32_t>::max())),
      };
    }
    return {};
  }

  [[nodiscard]] bool operation_preview_active() const {
    const std::optional<quader_editor_core::EditorOperationId> operation =
        sdk_projection_adapter_.active_preview_operation();
    return operation.has_value() &&
           quader_editor_core::preview_transaction_kind_for_operation(
               *operation)
               .has_value();
  }

  [[nodiscard]] ModelingToolSettingsTarget tool_settings_target() const {
    if (const std::optional<quader_editor_core::EditorOperationId> operation =
            sdk_projection_adapter_.active_preview_operation()) {
      if (const std::optional<quader_editor_core::PreviewTransactionKind> kind =
              quader_editor_core::preview_transaction_kind_for_operation(
                  *operation)) {
        return tool_settings_service_.active_preview_target(*kind);
      }
    }
    return tool_settings_service_.active_runtime_tool_target(runtime_.status());
  }

  [[nodiscard]] std::uint64_t tool_settings_revision() const {
    const std::optional<quader_editor_core::EditorOperationId>
        preview_operation = sdk_projection_adapter_.active_preview_operation();
    const bool cut_active = runtime_.status().cut_active;
    const auto active_tool = runtime_.status().active_tool;
    const std::uint64_t interaction_revision =
        interaction_settings_service_.revision();
    const std::uint64_t metadata_revision = tool_settings_service_.revision();
    if (!tool_settings_revision_valid_ ||
        tool_settings_preview_operation_ != preview_operation ||
        tool_settings_cut_active_ != cut_active ||
        tool_settings_active_tool_ != active_tool ||
        tool_settings_interaction_revision_ != interaction_revision ||
        tool_settings_metadata_revision_ != metadata_revision) {
      tool_settings_preview_operation_ = preview_operation;
      tool_settings_cut_active_ = cut_active;
      tool_settings_active_tool_ = active_tool;
      tool_settings_interaction_revision_ = interaction_revision;
      tool_settings_metadata_revision_ = metadata_revision;
      tool_settings_revision_valid_ = true;
      ++tool_settings_revision_;
    }
    return tool_settings_revision_;
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
    const bool changed = normalized != previous;
    if (!changed) {
      return {.ok = true, .changed = false};
    }
    if (cut_mode_changed) {
      (void)interaction_settings_service_.set_normalized(normalized);
      return set_cut_mode(normalized.cut_mode);
    }
    if (active_target.operation_preview_active) {
      // Preview settings are a candidate until their SDK projection succeeds.
      // This keeps both the values and their revision on the prior committed
      // state when an in-place update or structural preview rebuild fails.
      ModelingCommandResult result =
          update_active_operation_preview_settings(normalized);
      if (!result.ok) {
        return result;
      }
      const bool settings_changed =
          interaction_settings_service_.set_normalized(normalized);
      result.changed = result.changed || settings_changed;
      return result;
    }
    return {.ok = true,
            .changed =
                interaction_settings_service_.set_normalized(normalized)};
  }

  ModelingCommandResult reset_active_tool_settings() {
    const ModelingToolSettingsTarget active_target = tool_settings_target();
    if (!active_target.active) {
      return {
          .ok = false,
          .changed = false,
          .message = "No active tool settings.",
      };
    }
    if (active_target.kind == ModelingToolSettingsTargetKind::Mirror ||
        active_target.kind == ModelingToolSettingsTargetKind::None) {
      return {
          .ok = false,
          .changed = false,
          .message = "The active tool has no resettable settings.",
      };
    }
    if (active_target.kind == ModelingToolSettingsTargetKind::Cut) {
      const ModelingInteractionSettings defaults =
          interaction_settings_service_.normalize(
              ModelingInteractionSettings{});
      if (interaction_settings_service_.settings().cut_mode ==
          defaults.cut_mode) {
        return {.ok = true, .changed = false};
      }
      return set_cut_mode(defaults.cut_mode);
    }

    const bool changed =
        interaction_settings_service_.reset_for_target(active_target.kind);
    if (!changed) {
      return {.ok = true, .changed = false};
    }
    if (!active_target.operation_preview_active) {
      return {.ok = true, .changed = true};
    }
    ModelingCommandResult result = update_active_operation_preview_settings(
        interaction_settings_service_.settings());
    result.changed = result.changed || changed;
    return result;
  }

  [[nodiscard]] bool modal_tool_owns_viewport() const {
    return modal_tool_runtime_.owns_viewport(
        from_runtime_viewport_tool(runtime_.active_tool()));
  }

  /// Validates and stages a live editable face without retaining pointers
  /// across a modeling mutation.
  struct FaceTarget final {
    ModelingCommandResult command;
    EntityId entity = kInvalidEntityId;
    std::uint32_t material_slot = 0u;
  };

  struct FaceMaterialTarget final {
    ModelingCommandResult command;
    EntityId entity = kInvalidEntityId;
    MaterialReference material;
  };

  [[nodiscard]] FaceTarget resolve_face_object_target(int modeling_object_id) const {
    const auto rejected = [](std::string message) {
      return FaceTarget{
          .command = {
              .ok = false,
              .changed = false,
              .message = std::move(message),
          },
      };
    };
    if (modeling_object_id <= 0 ||
        !sdk_projection_adapter_.owns_object(modeling_object_id)) {
      return rejected("The material target mesh is not live.");
    }

    const std::optional<EntityId> entity =
        object_registry_.entity_for_modeling_object(modeling_object_id);
    if (!entity.has_value() || !scene_.exists(*entity) ||
        scene_.kind(*entity) != EntityKind::Mesh) {
      return rejected("The material target mesh is stale.");
    }

    const ModelingInstanceEditScope edit_scope =
        instance_edit_scope_provider_ ? instance_edit_scope_provider_()
                                      : ModelingInstanceEditScope{};
    const EntityId definition =
        scene_.containing_instance_definition(*entity);
    if (edit_scope.active()) {
      if (!scene_.exists(edit_scope.occurrence) ||
          !scene_.exists(edit_scope.definition) ||
          definition != edit_scope.definition ||
          *entity == edit_scope.definition) {
        return rejected(
            "The material target is outside the active instance edit scope.");
      }
      if (!scene_hierarchy_segment_is_selectable(
              scene_, *entity, edit_scope.definition) ||
          !scene_.is_effectively_visible(edit_scope.occurrence) ||
          scene_.is_effectively_locked(edit_scope.occurrence) ||
          (scene_.visibility(edit_scope.occurrence) != nullptr &&
           !scene_.visibility(edit_scope.occurrence)->selectable) ||
          !selection_service_.viewport_entity_visible(*entity) ||
          !selection_service_.viewport_entity_visible(edit_scope.occurrence)) {
        return rejected("The material target mesh is hidden or locked.");
      }
    } else {
      if (definition != kInvalidEntityId) {
        return rejected(
            "The face belongs to an atomic instance occurrence; enter "
            "instance edit mode before editing its material.");
      }
      const VisibilityComponent *visibility = scene_.visibility(*entity);
      if (!scene_.is_effectively_visible(*entity) ||
          scene_.is_effectively_locked(*entity) ||
          (visibility != nullptr && !visibility->selectable) ||
          !selection_service_.viewport_entity_visible(*entity)) {
        return rejected("The material target mesh is hidden or locked.");
      }
    }

    return {
        .command = {.ok = true, .changed = false},
        .entity = *entity,
    };
  }

  [[nodiscard]] FaceTarget resolve_face_target(int modeling_object_id,
                                               int face_id) const {
    FaceTarget target = resolve_face_object_target(modeling_object_id);
    if (!target.command.ok) {
      return target;
    }
    const auto rejected = [](std::string message) {
      return FaceTarget{.command = {.ok = false,
                                    .changed = false,
                                    .message = std::move(message)}};
    };
    const ModelingSelectionProjectionSnapshot &projection =
        selection_projection_snapshot();
    const auto object = std::find_if(
        projection.objects().begin(), projection.objects().end(),
        [modeling_object_id](const quader_editor_core::PolygonObject &item) {
          return item.id == modeling_object_id;
        });
    if (object == projection.objects().end()) {
      return rejected("The material target mesh is stale.");
    }
    const quader_poly::Face *face = quader_poly::find_face(
        object->document, static_cast<quader_poly::ElementId>(face_id));
    if (face == nullptr) {
      return rejected("The material target face is not live.");
    }

    target.material_slot = face->material_slot;
    return target;
  }

  [[nodiscard]] FaceMaterialTarget
  resolve_face_material(int modeling_object_id, int face_id) const {
    const FaceTarget target =
        resolve_face_target(modeling_object_id, face_id);
    if (!target.command.ok) {
      return {.command = target.command, .entity = target.entity};
    }

    const MaterialComponent *component = scene_.material(target.entity);
    if (component == nullptr) {
      return {
          .command = {
              .ok = false,
              .changed = false,
              .message = "The target face has no material.",
          },
          .entity = target.entity,
      };
    }
    const MaterialReference reference =
        component->material_for_slot(target.material_slot);
    if (!reference.valid()) {
      return {
          .command = {
              .ok = false,
              .changed = false,
              .message = "The target face material is unavailable.",
          },
          .entity = target.entity,
      };
    }
    return {
        .command = {.ok = true, .changed = false},
        .entity = target.entity,
        .material = reference,
    };
  }

  static std::uint32_t stage_material_slot(MaterialComponent &component,
                                           MaterialReference material) {
    if (component.material_for_slot(0u) == material) {
      return 0u;
    }
    const auto bound = std::min_element(
        component.material_slots.begin(), component.material_slots.end(),
        [&](const auto &left, const auto &right) {
          const bool left_matches = left.second == material;
          const bool right_matches = right.second == material;
          if (left_matches != right_matches) {
            return left_matches;
          }
          return left.first < right.first;
        });
    if (bound != component.material_slots.end() &&
        bound->second == material) {
      return bound->first;
    }
    std::uint32_t slot = 1u;
    while (component.material_slots.contains(slot)) {
      ++slot;
    }
    component.material_slots.emplace(slot, material);
    return slot;
  }

  /// Pins every renderable material surface selected by Object or Group mode.
  /// Mesh topology remains SDK-owned; solidified Curves need only their
  /// scene-owned primary material component updated.
  struct MaterialSurfaceTargets final {
    ModelingCommandResult command{.ok = true};
    std::vector<int> mesh_objects;
    std::vector<EntityId> solid_curves;
    bool preserve_scene_selection = false;

    [[nodiscard]] bool empty() const noexcept {
      return mesh_objects.empty() && solid_curves.empty();
    }
  };

  [[nodiscard]] static MaterialSurfaceTargets
  rejected_material_surface_targets(std::string message) {
    return MaterialSurfaceTargets{
        .command = {.ok = false, .message = std::move(message)}};
  }

  [[nodiscard]] ModelingCommandResult append_material_surface_target(
      EntityId entity, bool group_target,
      const ModelingInstanceEditScope &edit_scope,
      std::unordered_set<int> &visited_meshes,
      std::unordered_set<EntityId> &visited_curves,
      MaterialSurfaceTargets &result) const {
    const auto rejected = [](std::string message) {
      return ModelingCommandResult{
          .ok = false, .changed = false, .message = std::move(message)};
    };

    if (!scene_.exists(entity)) {
      return rejected("A selected material surface is not live.");
    }
    const CurveComponent *curve = scene_.curve(entity);
    if (curve != nullptr && curve->solidify &&
        !scene_entity_has_solid_curve_surface(scene_, entity)) {
      return rejected("A selected solidified Curve is invalid.");
    }
    if (!scene_entity_has_material_surface(scene_, entity)) {
      return {.ok = true, .changed = false};
    }

    const bool is_curve = scene_entity_has_solid_curve_surface(scene_, entity);
    const std::string_view target_name =
        is_curve ? "solidified Curve" : "mesh";
    if (scene_.is_effectively_locked(entity)) {
      return rejected("A selected " + std::string(target_name) +
                      " material surface is locked.");
    }

    const EntityId definition = scene_.containing_instance_definition(entity);
    if (!group_target && edit_scope.active()) {
      if (is_curve || definition != edit_scope.definition ||
          !scene_.exists(edit_scope.occurrence) ||
          !scene_hierarchy_segment_is_selectable(
              scene_, entity, edit_scope.definition) ||
          scene_.is_effectively_locked(edit_scope.occurrence) ||
          (scene_.visibility(edit_scope.occurrence) != nullptr &&
           !scene_.visibility(edit_scope.occurrence)->selectable)) {
        return rejected("A selected material surface is outside the active "
                        "instance edit scope or read-only.");
      }
    } else {
      if (!group_target && definition != kInvalidEntityId) {
        return rejected("Enter instance edit before applying material to a "
                        "definition mesh.");
      }
      if (!scene_hierarchy_segment_is_selectable_ignoring_visibility(scene_,
                                                                      entity)) {
        return rejected("A selected " + std::string(target_name) +
                        " material surface is read-only.");
      }
    }

    if (scene_.instance_occurrence(entity) != nullptr ||
        scene_.is_instance_definition_entity(entity) ||
        (is_curve && scene_.is_instance_definition_member(entity))) {
      return rejected("A selected material surface is not directly editable.");
    }
    const ModelingObjectComponent *object = scene_.modeling_object(entity);
    if (object == nullptr || object->object_id <= 0 ||
        !sdk_projection_adapter_.owns_object(object->object_id)) {
      return rejected("A selected " + std::string(target_name) +
                      " material surface is not editable.");
    }
    if (is_curve) {
      if (visited_curves.insert(entity).second) {
        result.solid_curves.push_back(entity);
      }
    } else if (visited_meshes.insert(object->object_id).second) {
      result.mesh_objects.push_back(object->object_id);
    }
    return {.ok = true, .changed = false};
  }

  [[nodiscard]] MaterialSurfaceTargets selected_material_surface_targets() const {
    if (sdk_projection_adapter_.preview_active() || transform_drag_active() ||
        modal_tool_owns_viewport()) {
      return rejected_material_surface_targets(
          "Commit or cancel the active tool or preview before applying material.");
    }

    const ModelingSelectionMode mode = selection_mode();
    if (mode != ModelingSelectionMode::Object &&
        mode != ModelingSelectionMode::Group) {
      return rejected_material_surface_targets(
          "Apply Material works in object, group, or face mode.");
    }
    const ModelingInstanceEditScope edit_scope =
        instance_edit_scope_provider_ ? instance_edit_scope_provider_()
                                      : ModelingInstanceEditScope{};
    if (mode == ModelingSelectionMode::Group && edit_scope.active()) {
      return rejected_material_surface_targets(
          "Select meshes in Object mode when applying material inside an instance.");
    }

    MaterialSurfaceTargets result;
    result.preserve_scene_selection = mode == ModelingSelectionMode::Group;
    std::unordered_set<int> visited_meshes;
    std::unordered_set<EntityId> visited_curves;

    if (mode == ModelingSelectionMode::Object) {
      const ModelingSelectionExport selection = export_selection();
      for (const ModelingSelectionExportObject &object : selection.objects) {
        if (!object.selected || object.entity == kInvalidEntityId) {
          continue;
        }
        const ModelingCommandResult appended = append_material_surface_target(
            object.entity, false, edit_scope, visited_meshes, visited_curves,
            result);
        if (!appended.ok) {
          return rejected_material_surface_targets(appended.message);
        }
      }
      if (result.empty()) {
        return rejected_material_surface_targets(
            "Select one or more meshes or solidified Curves before applying "
            "material.");
      }
      return result;
    }

    const std::vector<EntityId> selected = scene_.selected_entities();
    if (selected.empty()) {
      return rejected_material_surface_targets(
          "Select one or more groups containing material surfaces first.");
    }
    // The shared root helper omits locked roots, so validate the entire selected
    // set first rather than silently applying only to its unlocked remainder.
    for (const EntityId group : selected) {
      if (!scene_.exists(group) || scene_.is_scene_root(group) ||
          scene_.kind(group) != EntityKind::Empty ||
          scene_.is_instance_definition_entity(group) ||
          scene_.is_instance_definition_member(group)) {
        return rejected_material_surface_targets(
            "Apply Material in Group mode requires ordinary scene groups.");
      }
      if (scene_.is_effectively_locked(group)) {
        return rejected_material_surface_targets(
            "A selected material group is locked.");
      }
      if (!scene_hierarchy_segment_is_selectable_ignoring_visibility(scene_,
                                                                      group)) {
        return rejected_material_surface_targets(
            "A selected material group is read-only.");
      }
    }

    for (const EntityId root : document_command_selected_root_entities(scene_)) {
      for (const EntityId entity : scene_.descendants(root, true)) {
        if (scene_.instance_occurrence(entity) != nullptr ||
            scene_.is_instance_definition_entity(entity) ||
            scene_.is_instance_definition_member(entity)) {
          return rejected_material_surface_targets(
              "Enter instance edit and select its meshes before applying material.");
        }
        const ModelingCommandResult appended = append_material_surface_target(
            entity, true, edit_scope, visited_meshes, visited_curves, result);
        if (!appended.ok) {
          return rejected_material_surface_targets(appended.message);
        }
      }
    }
    if (result.empty()) {
      return rejected_material_surface_targets(
          "The selected groups contain no editable meshes or solidified Curves.");
    }
    return result;
  }

  ModelingCommandResult normalize_whole_object_materials(
      std::span<const int> modeling_object_ids, MaterialReference material,
      bool preserve_scene_selection = false) {
    if (!material.valid()) {
      return {
          .ok = false,
          .changed = false,
          .message = "The material is unavailable.",
      };
    }
    if (modeling_object_ids.empty()) {
      return {
          .ok = false,
          .changed = false,
          .message = "Select one or more meshes before applying material.",
      };
    }

    const ModelingSelectionProjectionSnapshot &projection =
        selection_projection_snapshot();
    std::vector<int> normalized_object_ids;
    std::vector<EntityId> entities;
    std::unordered_set<int> staged_object_ids;
    normalized_object_ids.reserve(modeling_object_ids.size());
    entities.reserve(modeling_object_ids.size());
    staged_object_ids.reserve(modeling_object_ids.size());
    for (const int modeling_object_id : modeling_object_ids) {
      if (!staged_object_ids.insert(modeling_object_id).second) {
        continue;
      }
      const std::optional<EntityId> entity =
          object_registry_.entity_for_modeling_object(modeling_object_id);
      const auto object = std::find_if(
          projection.objects().begin(), projection.objects().end(),
          [modeling_object_id](const quader_editor_core::PolygonObject &item) {
            return item.id == modeling_object_id;
          });
      if (!entity.has_value() || !scene_.exists(*entity) ||
          object == projection.objects().end() ||
          object->document.faces.empty()) {
        return {
            .ok = false,
            .changed = false,
            .message = "A material target mesh is not live.",
          };
      }
      normalized_object_ids.push_back(modeling_object_id);
      entities.push_back(*entity);
    }

    ModelingCommandResult modeling_result =
        sdk_projection_adapter_.assign_material_slot_to_objects(
            normalized_object_ids, 0u, preserve_scene_selection);
    if (!modeling_result.ok) {
      return modeling_result;
    }
    bool component_changed = false;
    for (const EntityId entity : entities) {
      component_changed =
          mesh_sync_service_.set_entity_material(
              scene_, entity, MaterialComponent{.material = material}) ||
          component_changed;
    }
    modeling_result.changed = modeling_result.changed || component_changed;
    return modeling_result;
  }

  ModelingCommandResult apply_material_to_surfaces(
      const MaterialSurfaceTargets &targets, MaterialReference material) {
    if (!targets.command.ok) {
      return targets.command;
    }
    if (!material.valid()) {
      return {.ok = false,
              .changed = false,
              .message = "The active material is unavailable."};
    }

    ModelingCommandResult result{.ok = true};
    if (!targets.mesh_objects.empty()) {
      result = normalize_whole_object_materials(
          targets.mesh_objects, material, targets.preserve_scene_selection);
      if (!result.ok) {
        return result;
      }
    }

    bool curve_changed = false;
    for (const EntityId entity : targets.solid_curves) {
      const MaterialComponent desired{.material = material};
      const MaterialComponent *current = scene_.material(entity);
      if (current != nullptr && *current == desired) {
        continue;
      }
      if (!mesh_sync_service_.set_entity_material(scene_, entity, desired)) {
        return {.ok = false,
                .changed = result.changed || curve_changed,
                .message =
                    "A solidified Curve material could not be applied."};
      }
      curve_changed = true;
    }
    result.changed = result.changed || curve_changed;
    return result;
  }

  [[nodiscard]] ModelingCommandResult
  active_material_selection_status(bool material_reference_available) const {
    const ModelingSelectionMode mode = selection_mode();
    if (mode != ModelingSelectionMode::Object &&
        mode != ModelingSelectionMode::Group &&
        mode != ModelingSelectionMode::Face) {
      return {.ok = false,
              .changed = false,
              .message =
                  "Apply Material works in object, group, or face mode."};
    }
    if (mode == ModelingSelectionMode::Face) {
      return selection_summary().selected_face_count != 0u
                 ? ModelingCommandResult{.ok = true}
                 : ModelingCommandResult{
                       .ok = false,
                       .changed = false,
                       .message =
                           "Select one or more faces before applying material."};
    }
    const MaterialSurfaceTargets targets = selected_material_surface_targets();
    if (!targets.command.ok) {
      return targets.command;
    }
    if (!material_reference_available && !targets.solid_curves.empty()) {
      return {.ok = false,
              .changed = false,
              .message =
                  "Choose an active material before applying it to a "
                  "solidified Curve."};
    }
    return targets.command;
  }

  [[nodiscard]] ModelingProjectMaterialQueryResult
  project_material_for_face(int modeling_object_id, int face_id) const {
    FaceMaterialTarget target =
        resolve_face_material(modeling_object_id, face_id);
    if (target.command.ok && !target.material.project_owned()) {
      target.command = {
          .ok = false,
          .changed = false,
          .message = "The target face does not use a project material.",
      };
    }
    return {
        .command = std::move(target.command),
        .material = target.material.project_asset(),
    };
  }

  ModelingCommandResult apply_face_material_to_mesh(int modeling_object_id,
                                                    int face_id) {
    const FaceMaterialTarget target =
        resolve_face_material(modeling_object_id, face_id);
    if (!target.command.ok) {
      return target.command;
    }
    const std::array object_ids{modeling_object_id};
    return normalize_whole_object_materials(object_ids, target.material);
  }

  ModelingCommandResult apply_material_to_mesh_at_face(
      int modeling_object_id, int face_id, MaterialReference material) {
    if (!material.valid()) {
      return {
          .ok = false,
          .changed = false,
          .message = "The active material is unavailable.",
      };
    }
    const FaceTarget target = resolve_face_target(modeling_object_id, face_id);
    if (!target.command.ok) {
      return target.command;
    }
    const std::array object_ids{modeling_object_id};
    return normalize_whole_object_materials(object_ids, material);
  }

  ModelingCommandResult apply_material_to_face(
      int modeling_object_id, int face_id, MaterialReference material) {
    if (!material.valid()) {
      return {
          .ok = false,
          .changed = false,
          .message = "The active material is unavailable.",
      };
    }
    const FaceTarget target = resolve_face_target(modeling_object_id, face_id);
    if (!target.command.ok) {
      return target.command;
    }

    const MaterialComponent *existing = scene_.material(target.entity);
    MaterialComponent desired;
    if (existing != nullptr) {
      desired = *existing;
    }
    const std::uint32_t slot = stage_material_slot(desired, material);
    ModelingCommandResult modeling_result =
        sdk_projection_adapter_.assign_material_slot_to_face(
            modeling_object_id, face_id, slot);
    if (!modeling_result.ok) {
      return modeling_result;
    }
    const bool component_changed = mesh_sync_service_.set_entity_material(
        scene_, target.entity, std::move(desired));
    modeling_result.changed = modeling_result.changed || component_changed;
    return modeling_result;
  }

  ModelingCommandResult replace_material_references_on_faces(
      std::span<const ModelingFaceMaterialReplacementTarget> targets,
      MaterialReference source, MaterialReference replacement) {
    if (!source.valid() || !replacement.valid()) {
      return {
          .ok = false,
          .changed = false,
          .message = "Material references are unavailable.",
      };
    }
    if (source == replacement || targets.empty()) {
      return {.ok = true, .changed = false};
    }

    struct MaterialProjection final {
      EntityId entity = kInvalidEntityId;
      MaterialComponent material;
      std::uint32_t destination_slot = 0u;
    };
    std::vector<MaterialProjection> projections;
    std::unordered_map<EntityId, std::size_t> projection_by_entity;
    std::vector<ModelingMaterialSlotAssignment> assignments;
    projections.reserve(targets.size());
    projection_by_entity.reserve(targets.size());
    assignments.reserve(targets.size());

    for (const ModelingFaceMaterialReplacementTarget &requested : targets) {
      const FaceTarget target =
          resolve_face_target(requested.modeling_object_id, requested.face_id);
      if (!target.command.ok) {
        return target.command;
      }
      const MaterialComponent *existing = scene_.material(target.entity);
      if (existing == nullptr ||
          existing->material_for_slot(target.material_slot) != source) {
        continue;
      }

      std::size_t projection_index = 0u;
      const auto found = projection_by_entity.find(target.entity);
      if (found == projection_by_entity.end()) {
        MaterialComponent desired = *existing;
        const std::uint32_t destination_slot =
            stage_material_slot(desired, replacement);
        projection_index = projections.size();
        projection_by_entity.emplace(target.entity, projection_index);
        projections.push_back({
            .entity = target.entity,
            .material = std::move(desired),
            .destination_slot = destination_slot,
        });
      } else {
        projection_index = found->second;
      }
      assignments.push_back({
          .modeling_object_id = requested.modeling_object_id,
          .face_id = requested.face_id,
          .material_slot = projections[projection_index].destination_slot,
      });
    }

    if (assignments.empty()) {
      return {.ok = true, .changed = false};
    }
    ModelingCommandResult modeling_result =
        sdk_projection_adapter_.assign_material_slots_to_faces(assignments);
    if (!modeling_result.ok) {
      return modeling_result;
    }
    bool component_changed = false;
    for (MaterialProjection &projection : projections) {
      const MaterialComponent *current = scene_.material(projection.entity);
      if (current != nullptr && *current == projection.material) {
        continue;
      }
      if (!mesh_sync_service_.set_entity_material(
              scene_, projection.entity, std::move(projection.material))) {
        return {
            .ok = false,
            .changed = modeling_result.changed,
            .message = "A face material binding could not be replaced.",
        };
      }
      component_changed = true;
    }
    modeling_result.changed = modeling_result.changed || component_changed;
    return modeling_result;
  }

  ModelingCommandResult apply_active_material(MaterialReference material) {
    if (!material.valid()) {
      return {
          .ok = false,
          .changed = false,
          .message = "The active material is unavailable.",
      };
    }

    if (selection_mode() == ModelingSelectionMode::Group) {
      const MaterialSurfaceTargets targets = selected_material_surface_targets();
      return apply_material_to_surfaces(targets, material);
    }

    const ModelingSelectionSummary summary = selection_summary();
    if (summary.mode != ModelingSelectionMode::Object &&
        summary.mode != ModelingSelectionMode::Face) {
      return {
          .ok = false,
          .changed = false,
          .message = "Apply Material works in object, group, or face mode.",
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

    if (summary.mode == ModelingSelectionMode::Object) {
      return apply_material_to_surfaces(selected_material_surface_targets(),
                                        material);
    }

    bool component_changed = false;
    std::vector<ModelingMaterialSlotAssignment> assignments;
    /// Stages one host material component until SDK topology mutation succeeds.
    struct MaterialProjection {
      EntityId entity = kInvalidEntityId;
      MaterialComponent material;
    };
    std::vector<MaterialProjection> projections;
    for (const ModelingSelectionExportObject &object : selection.objects) {
      if (object.selected_faces.empty() ||
          object.entity == kInvalidEntityId ||
          !scene_.exists(object.entity)) {
        continue;
      }

      MaterialComponent desired;
      if (const MaterialComponent *existing = scene_.material(object.entity)) {
        desired = *existing;
      }

      const std::uint32_t slot = stage_material_slot(desired, material);

      for (const ModelingSelectionExportFace &face : object.selected_faces) {
        assignments.push_back({
            .modeling_object_id = object.modeling_object_id,
            .face_id = static_cast<int>(face.id),
            .material_slot = slot,
        });
      }
      projections.push_back(
          {.entity = object.entity, .material = std::move(desired)});
    }

    ModelingCommandResult modeling_result =
        sdk_projection_adapter_.assign_material_slots_to_faces(assignments);
    if (!modeling_result.ok) {
      return modeling_result;
    }
    for (MaterialProjection &projection : projections) {
      component_changed = mesh_sync_service_.set_entity_material(
                              scene_, projection.entity,
                              std::move(projection.material)) ||
                          component_changed;
    }

    return {
        .ok = true,
        .changed = modeling_result.changed || component_changed,
        .message = std::move(modeling_result.message),
    };
  }

  [[nodiscard]] bool transform_drag_active() const {
    return runtime_.transform_drag_active() ||
           transform_drag_service_.scene_preview_active();
  }

  [[nodiscard]] bool can_cancel_active_tool_or_preview() const {
    return operation_preview_active() ||
           runtime_.transform_drag_active() || has_transform_preview() ||
           transform_drag_service_.scene_preview_active() ||
           structural_transform_preview_active() ||
           structural_drag_checkpoint_active_ ||
           modal_tool_owns_viewport();
  }

  [[nodiscard]] bool can_cancel_active_preview_for_undo() const {
    return operation_preview_active() ||
           runtime_.transform_drag_active() || has_transform_preview() ||
           transform_drag_service_.scene_preview_active() ||
           structural_transform_preview_active() ||
           structural_drag_checkpoint_active_;
  }

  ModelingCommandResult cancel_competing_state_for_native_tool_activation() {
    bool changed = false;
    const auto cancel = [&changed](ModelingCommandResult result) {
      changed = changed || result.changed;
      result.changed = changed;
      return result;
    };

    if (operation_preview_active()) {
      const ModelingCommandResult result =
          cancel(cancel_active_operation_preview());
      if (!result.ok) {
        return result;
      }
    }
    if (transform_drag_active() || structural_drag_checkpoint_active_) {
      const ModelingCommandResult result = cancel(cancel_transform_drag());
      if (!result.ok) {
        return result;
      }
    }
    if (structural_transform_preview_active()) {
      const ModelingCommandResult result = cancel(cancel_transform_preview());
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
    if (!sdk_projection_adapter_.preview_active()) {
      tool_settings_service_.clear_preview_metadata();
    }
  }

  ModelingCommandResult execute_runtime_operation_command(
      quader_editor_core::EditorOperationId runtime_operation,
      quader_editor_core::EditorOperationCommandPhase phase,
      quader_editor_core::EditorOperationSettings settings,
      bool preserve_duplicate_scene_identity = true) {
    using OperationId = quader_editor_core::EditorOperationId;
    using OperationPhase = quader_editor_core::EditorOperationCommandPhase;

    if (!quader_editor_core::command_settings_match_operation(
            runtime_operation, settings)) {
      return {.ok = false,
              .changed = false,
              .message =
                  "Operation command settings do not match the operation id."};
    }

    const bool begins_preview = phase == OperationPhase::BeginPreview;
    const bool projects_active_preview =
        phase == OperationPhase::UpdatePreview ||
        phase == OperationPhase::CommitPreview ||
        phase == OperationPhase::CancelPreview;
    if (begins_preview &&
        (structural_transform_preview_active() ||
         structural_drag_checkpoint_active_)) {
      return {
          .ok = false,
          .changed = false,
          .message =
              "Finish the active structural transform before starting an "
              "operation preview.",
      };
    }
    if (begins_preview && preview_checkpoint_hooks_.begin) {
      preview_checkpoint_hooks_.begin();
    } else if (projects_active_preview &&
               preview_checkpoint_hooks_.observe_before_projection) {
      preview_checkpoint_hooks_.observe_before_projection();
    }

    const quader_editor_core::EditorOperationCommand command{
        .operation_id = runtime_operation,
        .phase = phase,
        .settings = settings,
    };
    quader_editor_core::EditorOperationSettings sdk_settings = settings;
    std::optional<QVec3> duplicate_world_offset;
    if (runtime_operation == OperationId::DuplicateMeshes &&
        phase == OperationPhase::Execute &&
        preserve_duplicate_scene_identity) {
      if (auto *duplicate =
              std::get_if<quader_editor_core::DuplicateMeshesOperationSettings>(
                  &sdk_settings)) {
        duplicate_world_offset =
            quader_editor_core::duplicate_meshes_offset_from_settings(
                *duplicate);
        duplicate->explicit_offset = {{}, true};
      }
    }

    ModelingCommandResult result;
    switch (phase) {
    case OperationPhase::Execute:
      result = sdk_projection_adapter_.execute_operation(runtime_operation,
                                                         sdk_settings);
      break;
    case OperationPhase::BeginPreview:
      result = sdk_projection_adapter_.begin_operation_preview(
          runtime_operation, settings);
      break;
    case OperationPhase::UpdatePreview:
      result = sdk_projection_adapter_.update_operation_preview(
          runtime_operation, settings);
      break;
    case OperationPhase::CommitPreview:
      result = sdk_projection_adapter_.commit_operation_preview();
      break;
    case OperationPhase::CancelPreview:
      result = sdk_projection_adapter_.cancel_operation_preview();
      break;
    }

    if (result.ok && result.changed && duplicate_world_offset.has_value() &&
        quader::length_squared(*duplicate_world_offset) > 0.0000001F) {
      const DocumentCommandResult placed =
          translate_scene_selection_by_world_delta(*duplicate_world_offset,
                                                   "Place Duplicate");
      if (!placed.ok) {
        result = {
            .ok = false,
            .changed = true,
            .message = placed.message,
        };
      }
    }

    history_bridge_.record_operation(
        command,
        {.ok = result.ok,
         .changed = result.changed,
         .message = result.message});
    if (result.ok) {
      sdk_projection_adapter_.add_selection_to_scene();
      if ((begins_preview || phase == OperationPhase::UpdatePreview) &&
          sdk_projection_adapter_.preview_active()) {
        const bool acknowledged =
            sdk_projection_adapter_
                .acknowledge_active_preview_host_publication();
        if (!acknowledged) {
          result = {
              .ok = false,
              .changed = true,
              .message = "Operation preview host publication could not be "
                         "acknowledged.",
          };
        }
      }
      invalidate_selection_overlay_caches();
    }
    clear_inactive_operation_preview_metadata();
    if ((begins_preview || projects_active_preview) &&
        preview_checkpoint_hooks_.record_after_projection) {
      preview_checkpoint_hooks_.record_after_projection();
    }
    const bool finished_preview =
        projects_active_preview &&
        (phase == OperationPhase::CommitPreview ||
         phase == OperationPhase::CancelPreview) &&
        result.ok && !sdk_projection_adapter_.preview_active();
    const bool failed_to_begin_preview =
        begins_preview && !sdk_projection_adapter_.preview_active();
    if ((finished_preview || failed_to_begin_preview) &&
        preview_checkpoint_hooks_.finish) {
      preview_checkpoint_hooks_.finish(
          phase == OperationPhase::CancelPreview && result.ok);
    }
    return result;
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

  void refresh_transform_drag_projection_cache() {
    ModelingSelectionProjectionSnapshot projection =
        selection_projection_snapshot();
    if (transform_drag_projection_cache_ == nullptr) {
      transform_drag_projection_cache_ =
          std::make_unique<ModelingSelectionProjectionSnapshot>(
              std::move(projection));
      return;
    }
    // Keep the pointee address stable: an in-flight drag context borrows it.
    *transform_drag_projection_cache_ = std::move(projection);
  }

  void clear_transform_drag_caches() {
    transform_drag_projection_cache_.reset();
    transform_drag_local_inputs_.clear();
    transform_drag_preview_transforms_.clear();
  }

  [[nodiscard]] bool
  initialize_transform_drag_local_inputs(std::string &message) {
    QDR_PROFILE_SCOPE("Modeling transform drag cache local inputs");
    transform_drag_local_inputs_.clear();
    transform_drag_preview_transforms_.clear();
    if (transform_drag_projection_cache_ == nullptr) {
      refresh_transform_drag_projection_cache();
    }
    const ModelingSelectionProjectionSnapshot &projection =
        *transform_drag_projection_cache_;
    transform_drag_local_inputs_.reserve(projection.objects().size());
    for (const quader_editor_core::PolygonObject &object :
         projection.objects()) {
      if (!object.selected) {
        continue;
      }
      const std::optional<QMat4> world_from_object =
          modeling_object_world_transform(object.id);
      const std::optional<QMat4> object_from_world =
          world_from_object.has_value()
              ? try_scene_matrix_inverse(*world_from_object)
              : std::nullopt;
      if (!world_from_object.has_value() || !object_from_world.has_value()) {
        transform_drag_local_inputs_.clear();
        message = "Could not resolve selected object scene transform.";
        return false;
      }
      transform_drag_local_inputs_.push_back({
          .modeling_object_id = object.id,
          .world_from_object = *world_from_object,
          .object_from_world = *object_from_world,
      });
    }
    if (transform_drag_local_inputs_.empty()) {
      message = "Select something before transforming.";
      return false;
    }
    transform_drag_preview_transforms_.reserve(
        transform_drag_local_inputs_.size());
    for (const TransformDragLocalInput &input :
         transform_drag_local_inputs_) {
      transform_drag_preview_transforms_.push_back({
          .modeling_object_id = input.modeling_object_id,
      });
    }
    return true;
  }

  [[nodiscard]] bool update_transform_drag_local_transforms(
      const QMat4 &world_affine, std::string &message) {
    if (transform_drag_local_inputs_.empty() &&
        !initialize_transform_drag_local_inputs(message)) {
      return false;
    }
    const glm::mat4 world_delta = to_glm(world_affine);
    for (std::size_t index = 0;
         index < transform_drag_local_inputs_.size(); ++index) {
      const TransformDragLocalInput &input =
          transform_drag_local_inputs_[index];
      const glm::mat4 local_delta =
          to_glm(input.object_from_world) * world_delta *
          to_glm(input.world_from_object);
      transform_drag_preview_transforms_[index].transform = from_runtime(
          runtime_transform_from_matrix(from_glm(local_delta)));
    }
    return true;
  }

  std::vector<quader_editor_core::ObjectLocalTransform>
  local_transforms_for_selected_objects(const QMat4 &world_affine,
                                        std::string &message) const {
    std::vector<quader_editor_core::ObjectLocalTransform> transforms;
    const glm::mat4 world_delta = to_glm(world_affine);
    const ModelingSelectionProjectionSnapshot &projection =
        selection_projection_snapshot();
    for (const quader_editor_core::PolygonObject &object :
         projection.objects()) {
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

  quader_editor_core::CommandResult execute_repeat_transform_scene_aware(
      const quader_editor_core::RepeatTransformAction &action,
      EditorTransaction *transaction = nullptr,
      EditorHistoryService *history = nullptr,
      const ModelingStructuralDuplicate *structural_duplicate =
          nullptr) {
    const ModelingSelectionMode current_mode = selection_mode();
    const bool scene_selection_mode =
        current_mode == ModelingSelectionMode::Object ||
        current_mode == ModelingSelectionMode::Group;
    if ((scene_selection_mode && scene_.selected_entities().empty()) ||
        (!scene_selection_mode && !sdk_selection_active())) {
      return {false, false,
              "Select something before repeating the last transform."};
    }
    QVec3 replay_move_delta = action.move_delta;
    if (action.tool == quader_editor_core::ViewportInteractionTool::Move &&
        action.space == quader_editor_core::TransformSpace::Local) {
      const std::optional<ModelingTransform3> current_orientation =
          selection_transform_orientation();
      const std::optional<QVec3> remapped =
          current_orientation.has_value()
              ? remap_local_move_delta(action.move_delta, action.orientation,
                                       *current_orientation)
              : std::nullopt;
      if (!remapped.has_value()) {
        return {false, false,
                "Could not resolve the active local transform orientation."};
      }
      replay_move_delta = *remapped;
    }
    if (scene_selection_mode) {
      bool duplicated = false;
      if (action.duplicate_selected) {
        if (transaction == nullptr || structural_duplicate == nullptr) {
          return {
              false,
              false,
              "Object and Group duplicate repeat requires the structural "
              "entity lifecycle.",
          };
        }
        const ModelingCommandResult duplicate = (*structural_duplicate)(
            *transaction, current_mode, "Repeat Last Action");
        if (!duplicate.ok) {
          return {false, duplicate.changed, duplicate.message};
        }
        duplicated = duplicate.changed;
      }

      DocumentCommandResult transformed;
      if (action.tool ==
          quader_editor_core::ViewportInteractionTool::Move) {
        transformed = translate_scene_selection_by_world_delta(
            replay_move_delta, "Repeat Move", history);
      } else {
        if (action.tool ==
            quader_editor_core::ViewportInteractionTool::Rotate) {
          if (action.space == quader_editor_core::TransformSpace::Local) {
            transformed = rotate_scene_selection_by_local_delta(
                action.axis, action.rotate_angle_degrees, "Repeat Rotate",
                history);
          } else {
            const quader_poly::Transform3 rotation =
                action.axis == quader_editor_core::TransformAxis::All
                    ? action.trackball_rotation
                    : quader_editor_core::rotation_transform(
                          action.axis, action.rotate_angle_degrees *
                                           std::acos(-1.0) / 180.0);
            transformed = transform_scene_selection_by_world_delta(
                world_affine_from_transform(rotation, action.pivot),
                "Repeat Rotate", SceneTextureTransformKind::Rotation,
                history);
          }
        } else if (action.tool ==
                   quader_editor_core::ViewportInteractionTool::Scale) {
          transformed = action.space == quader_editor_core::TransformSpace::Local
                            ? scale_scene_selection_by_local_delta(
                                  action.scale, "Repeat Scale", history)
                            : transform_scene_selection_by_world_delta(
                                  world_affine_from_transform(
                                      quader_editor_core::scale_transform(
                                          action.scale),
                                      action.pivot),
                                  "Repeat Scale",
                                  SceneTextureTransformKind::Scale, history);
        } else {
          transformed = document_command_invalid_request_result(
              "Unsupported repeat transform tool.");
        }
      }

      if (!transformed.ok) {
        return {false, duplicated, transformed.message};
      }
      return {true, duplicated || transformed.changed, {}};
    }

    if (action.tool == quader_editor_core::ViewportInteractionTool::Move) {
      const ModelingCommandResult begun =
          begin_transform_preview(action.duplicate_selected);
      if (!begun.ok) {
        return {begun.ok, begun.changed, begun.message};
      }

      std::string message;
      const std::vector<quader_editor_core::ObjectLocalOffset> offsets =
          local_offsets_for_selected_objects(replay_move_delta, message);
      if (offsets.empty()) {
        (void)cancel_transform_preview();
        return {false, false, message};
      }
      ModelingCommandResult update;
      if (action.operation ==
          quader_editor_core::TransformDragOperation::Extrude) {
        update = update_transform_extrude_preview(offsets.front().offset, 0.0F);
      } else {
        update = update_transform_translate_preview(offsets.front().offset);
      }
      if (!update.ok) {
        (void)cancel_transform_preview();
        return {false, update.changed, update.message};
      }
      const ModelingCommandResult committed =
          commit_transform_preview(action.move_auto_merge_vertices);
      return {committed.ok, committed.changed, committed.message};
    }

    if (action.tool != quader_editor_core::ViewportInteractionTool::Rotate &&
        action.tool != quader_editor_core::ViewportInteractionTool::Scale) {
      return {false, false, "Unsupported repeat transform tool."};
    }

    // Re-enter the same per-object preview path used by the original drag.
    // This preserves the recorded world pivot, converts the complete affine
    // into each selected object's local basis, and retains TransformExtrude
    // for Shift+Rotate rather than reducing the rotation to an offset.
    const ModelingCommandResult begun = begin_portable_transform_preview(
        action.duplicate_selected,
        action.operation ==
                quader_editor_core::TransformDragOperation::Transform &&
            !action.duplicate_selected);
    if (!begun.ok) {
      clear_transform_drag_caches();
      return {begun.ok, begun.changed, begun.message};
    }

    quader_editor_core::TransformDragState replay_drag;
    replay_drag.active = true;
    replay_drag.tool = action.tool;
    replay_drag.operation = action.operation;
    replay_drag.axis = action.axis;
    replay_drag.space = action.space;
    replay_drag.orientation = action.orientation;
    replay_drag.pivot = action.pivot;
    replay_drag.rotate.active =
        action.tool == quader_editor_core::ViewportInteractionTool::Rotate;
    replay_drag.rotate.axis = action.axis;
    replay_drag.rotate.orientation = action.orientation;
    replay_drag.rotate.preview_angle_degrees = action.rotate_angle_degrees;
    replay_drag.rotate.trackball_rotation = action.trackball_rotation;
    replay_drag.scale.active =
        action.tool == quader_editor_core::ViewportInteractionTool::Scale;
    replay_drag.scale.axis = action.axis;
    replay_drag.scale.orientation = action.orientation;
    replay_drag.scale.preview_scale = action.scale;

    const ModelingCommandResult update =
        update_portable_transform_preview(replay_drag, 0.0);
    if (!update.ok) {
      (void)cancel_transform_preview();
      clear_transform_drag_caches();
      return {false, update.changed, update.message};
    }
    const ModelingCommandResult committed = commit_transform_preview();
    clear_transform_drag_caches();
    return {committed.ok, committed.changed, committed.message};
  }

  quader_editor_core::CommandResult execute_repeat_last_action_scene_aware() {
    quader_editor_core::RepeatLastActionState &repeat =
        history_bridge_.repeat_last_action();
    if (repeat.last_action_kind ==
        quader_editor_core::RepeatableActionKind::UvPeel) {
      if (!repeat.last_uv_peel.has_value()) {
        return {false, false, "No repeatable UV Peel action is available."};
      }
      const quader_editor_core::RepeatLastActionState repeated_action = repeat;
      const ModelingCommandResult result = peel_uv({
          .options = *repeat.last_uv_peel,
          .selection_token = uv_peel_settings().selection_token,
      });
      repeat = repeated_action;
      return {.ok = result.ok, .changed = result.changed, .message = result.message};
    }
    if (repeat.last_action_kind ==
        quader_editor_core::RepeatableActionKind::FaceTexture) {
      if (!repeat.last_face_texture.has_value()) {
        return {false, false, "No repeatable texture action is available."};
      }
      const quader_editor_core::RepeatLastActionState repeated_action = repeat;
      const std::uint64_t selection_token =
          modeling_face_texture_action_is_projection(
              repeat.last_face_texture->action) &&
                  selection_mode() == ModelingSelectionMode::Object
              ? selection_revision()
              : face_texture_settings().selection_token;
      const ModelingCommandResult result = apply_face_texture_action({
          .options = *repeat.last_face_texture,
          .selection_token = selection_token,
      });
      repeat = repeated_action;
      return {.ok = result.ok, .changed = result.changed, .message = result.message};
    }
    if (repeat.last_action_kind ==
        quader_editor_core::RepeatableActionKind::Transform) {
      if (!repeat.last_transform.has_value()) {
        return {false, false, "No repeatable transform is available."};
      }
      // Repeat execution can record another action while replaying. Restore
      // the complete descriptor so repeated invocations remain identical.
      const quader_editor_core::RepeatLastActionState repeated_action = repeat;
      const quader_editor_core::RepeatTransformAction transform_action =
          *repeat.last_transform;
      const quader_editor_core::CommandResult result =
          execute_repeat_transform_scene_aware(transform_action);
      repeat = repeated_action;
      return result;
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

  ModelingCommandResult begin_operation_preview(
      const ModelingActionItem &action,
      const quader_editor_core::EditorOperation &operation) {
    if (structural_transform_preview_active() ||
        structural_drag_checkpoint_active_) {
      return {
          .ok = false,
          .changed = false,
          .message =
              "Finish the active structural transform before starting an "
              "operation preview.",
      };
    }
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
      const std::optional<quader_editor_core::PreviewTransactionKind>
          preview_kind =
              quader_editor_core::preview_transaction_kind_for_operation(
                  operation.operation_id());
      tool_settings_service_.set_preview_metadata(
          action.action_id, action.operation_id,
          action.label.empty()
              ? fallback_tool_settings_label(tool_settings_target_kind_for_preview(
                    preview_kind.value_or(
                        quader_editor_core::PreviewTransactionKind::Bridge)))
              : action.label);
    }
    return result;
  }

  ModelingCommandResult update_active_operation_preview_settings(
      const ModelingInteractionSettings &settings) {
    const std::optional<quader_editor_core::EditorOperationId> operation =
        sdk_projection_adapter_.active_preview_operation();
    if (!operation.has_value()) {
      return {.ok = false, .message = "No active operation preview."};
    }
    return execute_runtime_operation_command(
        *operation,
        quader_editor_core::EditorOperationCommandPhase::UpdatePreview,
        settings_for_operation(*operation, settings));
  }

  ModelingCommandResult finish_active_operation_preview(bool commit) {
    const std::optional<quader_editor_core::EditorOperationId> operation =
        sdk_projection_adapter_.active_preview_operation();
    if (!operation.has_value()) {
      return {.ok = false, .message = "No active operation preview."};
    }
    return execute_runtime_operation_command(
        *operation,
        commit ? quader_editor_core::EditorOperationCommandPhase::CommitPreview
               : quader_editor_core::EditorOperationCommandPhase::CancelPreview,
        settings_for_operation(*operation,
                               interaction_settings_service_.settings()));
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
    const std::optional<quader_editor_core::EditorOperationCommand> command =
        modeling_instant_operation_command(
            operation, interaction_settings_service_.settings());
    if (!command.has_value()) {
      return {.ok = false,
              .message =
                  "Instant operation is not mapped to a runtime operation."};
    }

    ModelingCommandResult result = execute_runtime_operation(
        command->operation_id, command->settings,
        preserve_duplicate_scene_identity);
    return result;
  }

  ModelingCommandResult cancel_structural_transform_preview_after_failure(
      std::string message) {
    const ModelingCommandResult rollback = cancel_transform_preview();
    if (!rollback.ok) {
      if (!message.empty()) {
        message += "; ";
      }
      message += "rollback failed: ";
      message += rollback.message.empty() ? "unknown structural failure"
                                          : rollback.message;
    }
    return {
        .ok = false,
        .changed = !rollback.ok,
        .message = std::move(message),
    };
  }

  void begin_preview_content_checkpoint(bool &active) {
    if (active) {
      return;
    }
    if (preview_checkpoint_hooks_.begin) {
      preview_checkpoint_hooks_.begin();
    }
    active = true;
  }

  void observe_preview_content_checkpoint(bool active) {
    if (active && preview_checkpoint_hooks_.observe_before_projection) {
      preview_checkpoint_hooks_.observe_before_projection();
    }
  }

  void record_preview_content_checkpoint(bool active) {
    if (active && preview_checkpoint_hooks_.record_after_projection) {
      preview_checkpoint_hooks_.record_after_projection();
    }
  }

  void finish_preview_content_checkpoint(bool &active,
                                         bool restored_by_cancel) {
    if (!active) {
      return;
    }
    if (preview_checkpoint_hooks_.finish) {
      preview_checkpoint_hooks_.finish(restored_by_cancel);
    }
    active = false;
  }

  ModelingCommandResult begin_transform_preview(bool duplicate_selected) {
    if (structural_transform_preview_active() ||
        structural_drag_checkpoint_active_) {
      return {
          .ok = false,
          .changed = false,
          .message =
              "Finish the active structural transform before starting "
              "another transform preview.",
      };
    }
    const ModelingSelectionMode mode = selection_mode();
    if (duplicate_selected &&
        (mode == ModelingSelectionMode::Object ||
         mode == ModelingSelectionMode::Group)) {
      return {
          .ok = false,
          .changed = false,
          .message =
              "Object and Group duplicate previews require the structural "
              "entity lifecycle.",
      };
    }
    ModelingCommandResult result =
        sdk_projection_adapter_.begin_translate_preview(duplicate_selected);
    if (result.ok) {
      invalidate_selection_overlay_caches();
    }
    return result;
  }

  ModelingCommandResult begin_transform_preview(
      bool duplicate_selected,
      const ModelingStructuralDuplicate &structural_duplicate,
      EditorHistoryService &history) {
    const ModelingSelectionMode mode = selection_mode();
    const bool structural_duplicate_requested =
        duplicate_selected &&
        (mode == ModelingSelectionMode::Object ||
         mode == ModelingSelectionMode::Group);
    if (!structural_duplicate_requested) {
      return begin_transform_preview(duplicate_selected);
    }
    if (structural_preview_transaction_.has_value()) {
      return {
          .ok = false,
          .changed = false,
          .message = "A structural transform preview is already active.",
      };
    }
    if (transform_drag_active()) {
      return {
          .ok = false,
          .changed = false,
          .message =
              "Finish the active transform drag before starting a structural "
              "transform preview.",
      };
    }
    if (operation_preview_active() || has_transform_preview() ||
        modal_tool_owns_viewport()) {
      return {
          .ok = false,
          .changed = false,
          .message =
              "Finish the active modeling tool or preview before starting a "
              "structural transform preview.",
      };
    }
    if (!structural_duplicate) {
      return {
          .ok = false,
          .changed = false,
          .message = "Structural duplicate callback is missing.",
      };
    }

    structural_preview_transaction_.emplace(
        history.begin_transaction("Commit Transform Preview"));
    if (!structural_preview_transaction_->active()) {
      structural_preview_transaction_.reset();
      return {
          .ok = false,
          .changed = false,
          .message =
              "Cannot begin a structural transform preview while another "
              "editor transaction is active.",
      };
    }
    structural_preview_history_ = &history;
    begin_preview_content_checkpoint(structural_preview_checkpoint_active_);

    ModelingStructuralDuplicateResult duplicated = structural_duplicate(
        *structural_preview_transaction_, mode, "Commit Transform Preview");
    record_preview_content_checkpoint(structural_preview_checkpoint_active_);
    if (!duplicated.ok || !duplicated.changed) {
      bool restored = !duplicated.changed;
      if (structural_preview_transaction_->active()) {
        observe_preview_content_checkpoint(
            structural_preview_checkpoint_active_);
        const CommandResult rollback =
            structural_preview_transaction_->cancel();
        record_preview_content_checkpoint(
            structural_preview_checkpoint_active_);
        restored = rollback.ok;
        if (!rollback.ok) {
          duplicated.ok = false;
          duplicated.changed = true;
          if (!duplicated.message.empty()) {
            duplicated.message += "; ";
          }
          duplicated.message += "structural preview rollback failed: ";
          duplicated.message += rollback.message.empty()
                                    ? "unknown transaction failure"
                                    : rollback.message;
        } else {
          duplicated.changed = false;
        }
      }
      structural_preview_transaction_.reset();
      structural_preview_duplicate_ = {};
      structural_preview_repeat_transform_.reset();
      structural_preview_history_ = nullptr;
      finish_preview_content_checkpoint(structural_preview_checkpoint_active_,
                                        restored);
      return duplicated;
    }
    if (duplicated.roots.empty()) {
      structural_preview_duplicate_ = std::move(duplicated);
      return cancel_structural_transform_preview_after_failure(
          "Structural duplicate returned no root mapping.");
    }

    structural_preview_duplicate_ = std::move(duplicated);
    const TextureLockSettings texture_locks =
        texture_lock_settings_provider_ ? texture_lock_settings_provider_()
                                        : TextureLockSettings{};
    if (!structural_preview_scene_transform_.begin(
            scene_, {}, true, "Commit Transform Preview", {}, &assets_,
            texture_locks)) {
      return cancel_structural_transform_preview_after_failure(
          "Structural transform preview could not capture its duplicated "
          "roots.");
    }
    structural_preview_repeat_transform_ = ModelingRepeatTransform{
        .active = true,
        .tool = ModelingTransformTool::Move,
        .operation = ModelingTransformOperation::Transform,
        .space = ModelingTransformSpace::Global,
        .duplicate_selected = true,
    };
    invalidate_selection_overlay_caches();
    return {
        .ok = true,
        .changed = true,
        .message = "Structural transform preview started.",
    };
  }

  [[nodiscard]] bool structural_transform_preview_active() const {
    return structural_preview_transaction_.has_value() &&
           structural_preview_transaction_->active();
  }

  ModelingCommandResult
  commit_structural_transform_preview(std::string label = {}) {
    if (!structural_transform_preview_active()) {
      return {
          .ok = false,
          .changed = false,
          .message = "No structural transform preview is active.",
      };
    }
    if (structural_preview_history_ == nullptr ||
        !structural_preview_scene_transform_.preview_active()) {
      return cancel_structural_transform_preview_after_failure(
          "Structural transform preview state is incomplete.");
    }
    if (label.empty()) {
      label = "Commit Transform Preview";
    }
    structural_preview_transaction_->set_label(label);
    observe_preview_content_checkpoint(structural_preview_checkpoint_active_);
    const DocumentCommandResult preview_commit =
        structural_preview_scene_transform_.commit(*structural_preview_history_);
    record_preview_content_checkpoint(structural_preview_checkpoint_active_);
    if (!preview_commit.ok) {
      return cancel_structural_transform_preview_after_failure(
          preview_commit.message.empty()
              ? "Structural transform preview could not be committed."
              : preview_commit.message);
    }
    const CommandResult transaction_commit =
        structural_preview_transaction_->commit();
    if (transaction_commit.ok &&
        structural_preview_repeat_transform_.has_value()) {
      (void)record_repeatable_transform(
          *structural_preview_repeat_transform_);
    }
    structural_preview_transaction_.reset();
    structural_preview_duplicate_ = {};
    structural_preview_repeat_transform_.reset();
    structural_preview_history_ = nullptr;
    record_preview_content_checkpoint(structural_preview_checkpoint_active_);
    finish_preview_content_checkpoint(structural_preview_checkpoint_active_,
                                      false);
    if (transaction_commit.ok) {
      invalidate_selection_overlay_caches();
    }
    return {
        .ok = transaction_commit.ok,
        .changed = !transaction_commit.ok || preview_commit.changed ||
                   transaction_commit.changed,
        .message = transaction_commit.message,
    };
  }

  ModelingCommandResult update_transform_translate_preview(QVec3 delta) {
    if (structural_transform_preview_active()) {
      observe_preview_content_checkpoint(structural_preview_checkpoint_active_);
      const DocumentCommandResult result =
          structural_preview_scene_transform_.preview_world_translation_delta(
              scene_, delta);
      record_preview_content_checkpoint(structural_preview_checkpoint_active_);
      if (result.ok) {
        structural_preview_repeat_transform_ = ModelingRepeatTransform{
            .active = true,
            .tool = ModelingTransformTool::Move,
            .operation = ModelingTransformOperation::Transform,
            .space = ModelingTransformSpace::Global,
            .duplicate_selected = true,
            .move_delta = delta,
        };
        invalidate_selection_overlay_caches();
      }
      return {
          .ok = result.ok,
          .changed = result.changed,
          .message = result.message,
      };
    }
    ModelingCommandResult result =
        sdk_projection_adapter_.update_translate_preview(delta);
    if (result.ok) {
      invalidate_selection_overlay_caches();
    }
    return result;
  }

  ModelingCommandResult
  update_transform_matrix_preview(const ModelingTransform3 &transform,
                                  QVec3 pivot) {
    if (structural_transform_preview_active()) {
      observe_preview_content_checkpoint(structural_preview_checkpoint_active_);
      const DocumentCommandResult result =
          structural_preview_scene_transform_.preview_world_transform_delta(
              scene_, modeling_transform_matrix(transform, pivot));
      record_preview_content_checkpoint(structural_preview_checkpoint_active_);
      if (result.ok) {
        // The matrix API does not carry the semantic rotate/scale tool and
        // axis needed by Repeat Last Action. Do not retain an earlier
        // translation descriptor after this matrix supersedes the preview.
        structural_preview_repeat_transform_.reset();
        invalidate_selection_overlay_caches();
      }
      return {
          .ok = result.ok,
          .changed = result.changed,
          .message = result.message,
      };
    }
    ModelingCommandResult result =
        sdk_projection_adapter_.update_transform_preview(transform, pivot);
    if (result.ok) {
      invalidate_selection_overlay_caches();
    }
    return result;
  }

  ModelingCommandResult
  update_transform_extrude_preview(QVec3 offset, float closed_edge_ledge_size) {
    if (structural_transform_preview_active()) {
      return {
          .ok = false,
          .changed = false,
          .message =
              "Extrude preview is unavailable for structural selections.",
      };
    }
    ModelingCommandResult result =
        sdk_projection_adapter_.update_extrude_preview(
            offset, closed_edge_ledge_size);
    if (result.ok) {
      invalidate_selection_overlay_caches();
    }
    return result;
  }

  ModelingCommandResult
  update_transform_inset_preview(const ModelingTransform3 &transform,
                                 QVec3 pivot) {
    if (structural_transform_preview_active()) {
      return {
          .ok = false,
          .changed = false,
          .message =
              "Inset preview is unavailable for structural selections.",
      };
    }
    ModelingCommandResult result =
        sdk_projection_adapter_.update_inset_preview(transform, pivot);
    if (result.ok) {
      invalidate_selection_overlay_caches();
    }
    return result;
  }

  ModelingCommandResult
  commit_transform_preview(bool auto_merge_vertices = false) {
    if (structural_transform_preview_active()) {
      return commit_structural_transform_preview();
    }
    ModelingCommandResult result =
        sdk_projection_adapter_.commit_translate_preview(auto_merge_vertices);
    if (result.ok) {
      invalidate_selection_overlay_caches();
    }
    return result;
  }

  ModelingCommandResult cancel_transform_preview() {
    if (!structural_preview_transaction_.has_value()) {
      ModelingCommandResult result =
          sdk_projection_adapter_.cancel_translate_preview();
      if (result.ok) {
        invalidate_selection_overlay_caches();
        if (result.changed &&
            preview_checkpoint_hooks_.rollback_without_checkpoint) {
          preview_checkpoint_hooks_.rollback_without_checkpoint();
        }
      }
      return result;
    }

    observe_preview_content_checkpoint(structural_preview_checkpoint_active_);
    DocumentCommandResult preview_cancel = document_command_unchanged_result();
    if (structural_preview_scene_transform_.preview_active()) {
      preview_cancel = structural_preview_scene_transform_.cancel();
    }
    CommandResult transaction_cancel = command_unchanged_result();
    if (structural_preview_transaction_->active()) {
      transaction_cancel = structural_preview_transaction_->cancel();
    }
    const bool ok = preview_cancel.ok && transaction_cancel.ok;
    std::string message;
    if (!preview_cancel.ok) {
      message = preview_cancel.message.empty()
                    ? "Scene transform preview cancellation failed."
                    : preview_cancel.message;
    }
    if (!transaction_cancel.ok) {
      if (!message.empty()) {
        message += "; ";
      }
      message += "structural transaction cancellation failed: ";
      message += transaction_cancel.message.empty()
                     ? "unknown transaction failure"
                     : transaction_cancel.message;
    }
    const bool changed = preview_cancel.changed || transaction_cancel.changed ||
                         !structural_preview_duplicate_.roots.empty();
    structural_preview_transaction_.reset();
    structural_preview_duplicate_ = {};
    structural_preview_repeat_transform_.reset();
    structural_preview_history_ = nullptr;
    record_preview_content_checkpoint(structural_preview_checkpoint_active_);
    finish_preview_content_checkpoint(structural_preview_checkpoint_active_,
                                      ok);
    if (ok) {
      invalidate_selection_overlay_caches();
    }
    return {
        .ok = ok,
        .changed = changed,
        .message = std::move(message),
    };
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
    drag.space = to_runtime(transform.space);
    drag.orientation = to_runtime(transform.orientation);
    drag.pivot = to_runtime(transform.pivot);
    drag.move.active = transform.tool == ModelingTransformTool::Move;
    drag.move.axis = drag.axis;
    drag.move.orientation = drag.orientation;
    drag.move.preview_delta = to_runtime(transform.move_delta);
    drag.move_auto_merge_vertices = transform.move_auto_merge_vertices;
    drag.rotate.active = transform.tool == ModelingTransformTool::Rotate;
    drag.rotate.axis = drag.axis;
    drag.rotate.orientation = drag.orientation;
    drag.rotate.preview_angle_degrees = transform.rotate_angle_degrees;
    drag.scale.active = transform.tool == ModelingTransformTool::Scale;
    drag.scale.axis = drag.axis;
    drag.scale.orientation = drag.orientation;
    drag.scale.preview_scale = to_runtime(transform.scale);

    const ModelingSelectionProjectionSnapshot &projection =
        selection_projection_snapshot();
    history_bridge_.record_transform(projection, drag,
                                     transform.duplicate_selected);
    return {.ok = true};
  }

  ModelingTransformDragPreview
  begin_transform_drag(const ModelingTransformDragBeginRequest &request,
                       DocumentCommandService *document_commands = nullptr,
                       const ModelingStructuralDuplicate
                           *structural_duplicate = nullptr,
                       EditorHistoryService *history = nullptr) {
    if (structural_drag_checkpoint_active_) {
      return {
          .ok = false,
          .active = true,
          .changed = false,
          .message =
              "Finish the active structural transform drag before starting "
              "another drag.",
      };
    }
    if (structural_transform_preview_active()) {
      return {
          .ok = false,
          .active = false,
          .changed = false,
          .message =
              "Finish the active structural transform preview before "
              "starting a transform drag.",
      };
    }
    const ModelingSelectionMode mode = selection_mode();
    const bool structural_duplicate_requested =
        request.duplicate_selected && structural_duplicate != nullptr &&
        history != nullptr &&
        (mode == ModelingSelectionMode::Object ||
         mode == ModelingSelectionMode::Group);
    if (structural_duplicate_requested &&
        (transform_drag_active() || operation_preview_active() ||
         has_transform_preview() || modal_tool_owns_viewport())) {
      return {
          .ok = false,
          .active = false,
          .changed = false,
          .message =
              "Finish the active modeling tool or preview before starting a "
              "structural transform drag.",
      };
    }
    if (structural_duplicate_requested) {
      begin_preview_content_checkpoint(structural_drag_checkpoint_active_);
    }
    const bool drag_was_active = transform_drag_active();
    refresh_transform_drag_projection_cache();
    ModelingTransformDragPreview result =
        transform_drag_service_.begin_transform_drag(
        transform_drag_context(document_commands, structural_duplicate,
                               history),
        request);
    if (structural_duplicate_requested) {
      record_preview_content_checkpoint(structural_drag_checkpoint_active_);
      if (!result.active) {
        finish_preview_content_checkpoint(structural_drag_checkpoint_active_,
                                          !result.changed);
      }
    }
    if (!runtime_.transform_drag_active() &&
        !sdk_projection_adapter_.preview_active()) {
      clear_transform_drag_caches();
    }
    if (result.changed || drag_was_active != result.active) {
      invalidate_volatile_overlay_cache();
    }
    return result;
  }

  ModelingTransformDragPreview
  update_transform_drag(const ModelingTransformDragUpdateRequest &request) {
    const bool drag_was_active = transform_drag_active();
    observe_preview_content_checkpoint(structural_drag_checkpoint_active_);
    ModelingTransformDragPreview result =
        transform_drag_service_.update_transform_drag(
        transform_drag_context(), request);
    record_preview_content_checkpoint(structural_drag_checkpoint_active_);
    if (structural_drag_checkpoint_active_ && !result.active) {
      finish_preview_content_checkpoint(structural_drag_checkpoint_active_,
                                        !result.changed);
    }
    if (!runtime_.transform_drag_active() &&
        !sdk_projection_adapter_.preview_active()) {
      clear_transform_drag_caches();
    }
    if (result.changed || drag_was_active != result.active) {
      invalidate_volatile_overlay_cache();
    }
    return result;
  }

  ModelingTransformDragPreview
  commit_transform_drag(EditorHistoryService *history = nullptr) {
    const bool drag_was_active = transform_drag_active();
    observe_preview_content_checkpoint(structural_drag_checkpoint_active_);
    ModelingTransformDragPreview result =
        transform_drag_service_.commit_transform_drag(
        transform_drag_context(), history);
    record_preview_content_checkpoint(structural_drag_checkpoint_active_);
    finish_preview_content_checkpoint(
        structural_drag_checkpoint_active_, !result.ok && !result.changed);
    clear_transform_drag_caches();
    if (drag_was_active || result.changed) {
      invalidate_volatile_overlay_cache();
    }
    return result;
  }

  ModelingCommandResult cancel_transform_drag() {
    const bool drag_was_active = transform_drag_active();
    const bool had_checkpoint = structural_drag_checkpoint_active_;
    observe_preview_content_checkpoint(structural_drag_checkpoint_active_);
    ModelingCommandResult result =
        transform_drag_service_.cancel_transform_drag(
        transform_drag_context());
    record_preview_content_checkpoint(structural_drag_checkpoint_active_);
    finish_preview_content_checkpoint(structural_drag_checkpoint_active_,
                                      result.ok);
    if (!had_checkpoint && result.ok && result.changed &&
        preview_checkpoint_hooks_.rollback_without_checkpoint) {
      preview_checkpoint_hooks_.rollback_without_checkpoint();
    }
    clear_transform_drag_caches();
    if (drag_was_active || result.changed) {
      invalidate_volatile_overlay_cache();
    }
    return result;
  }

  ModelingSelectionSummary
  pick_select(const ModelingPickSelectRequest &request) {
    const ModelingSelectionSummary before = selection_summary();
    ModelingSelectionSummary summary =
        sdk_projection_adapter_.pick_select(request, selection_service_);
    summary.content_revision = content_revision();
    summary.selection_revision = selection_revision();
    if (summary.selection_revision != before.selection_revision) {
      invalidate_selection_overlay_caches();
    }
    return summary;
  }

  ModelingSelectionSummary box_select(const ModelingBoxSelectRequest &request) {
    QDR_PROFILE_SCOPE("Modeling session box selection");
    const ModelingSelectionSummary before = selection_summary();
    ModelingSelectionSummary summary = [&]() {
      QDR_PROFILE_SCOPE("Modeling session box select SDK projection");
      return sdk_projection_adapter_.box_select(request, selection_service_);
    }();
    summary.content_revision = content_revision();
    summary.selection_revision = selection_revision();
    if (summary.selection_revision != before.selection_revision) {
      QDR_PROFILE_SCOPE("Modeling session box select invalidate overlays");
      invalidate_selection_overlay_caches();
    }
    return summary;
  }

  ModelingSelectionSummary box_select(
      const ModelingBoxSelectRequest &request,
      const quader_editor_core::RectangleSelectionState &baseline_selection) {
    QDR_PROFILE_SCOPE("Modeling session box selection from baseline");
    const ModelingSelectionSummary before = selection_summary();
    ModelingSelectionSummary summary = [&]() {
      QDR_PROFILE_SCOPE("Modeling session box select SDK projection");
      return sdk_projection_adapter_.box_select(
          request, baseline_selection, selection_service_);
    }();
    summary.content_revision = content_revision();
    summary.selection_revision = selection_revision();
    if (summary.selection_revision != before.selection_revision) {
      QDR_PROFILE_SCOPE("Modeling session box select invalidate overlays");
      invalidate_selection_overlay_caches();
    }
    return summary;
  }

  ModelingCommandResult
  update_selection_hover(const ModelingHoverRequest &request) {
    const ModelingSelectionProjectionSnapshot &projected =
        selection_projection_snapshot();
    const quader_editor_core::SelectionHoverUpdateResult hover =
        selection_service_.selection_hover(
            projected, scene_, object_registry_, runtime_.hover(), request);
    runtime_.apply_hover_update(hover);
    if (hover.changed) {
      overlay_service_.invalidate_composed_frame();
      invalidate_volatile_overlay_cache();
    }
    return {
        .ok = true,
        .changed = hover.changed,
    };
  }

  [[nodiscard]] ModelingHoverSummary selection_hover_summary() const {
    const ModelingSelectionProjectionSnapshot &projected =
        selection_projection_snapshot();
    return from_runtime_hover_summary(projected, runtime_.hover(), false);
  }

  [[nodiscard]] ModelingHoverSummary
  preview_selection_hover(const ModelingHoverRequest &request) const {
    const ModelingSelectionProjectionSnapshot &projected =
        selection_projection_snapshot();
    const quader_editor_core::SelectionHoverUpdateResult hover =
        selection_service_.selection_hover(
            projected, scene_, object_registry_, runtime_.hover(), request);
    return from_runtime_hover_summary(projected, hover.hover, hover.changed);
  }

  ModelingCommandResult clear_selection_hover() {
    quader_editor_core::EditorRuntimeCommand command;
    command.command_id = quader_editor_core::EditorRuntimeCommandId::ClearHover;
    const quader_editor_core::EditorRuntimeCommandResult result =
        quader_editor_core::execute_runtime_command(runtime_selection_view(),
                                                    runtime_, command);
    if (result.command.changed) {
      overlay_service_.invalidate_composed_frame();
      invalidate_volatile_overlay_cache();
    }
    return {
        .ok = result.command.ok,
        .changed = result.command.changed,
        .message = result.command.message,
    };
  }

  ModelingCommandResult execute_runtime_command(
      const quader_editor_core::EditorRuntimeCommand &command,
      bool may_change_content) {
    if (may_change_content) {
      return {.ok = false,
              .message =
                  "Retired native runtime commands cannot mutate modeling "
                  "content."};
    }
    const quader_editor_core::EditorRuntimeCommandResult result =
        quader_editor_core::execute_runtime_command(runtime_selection_view(),
                                                    runtime_, command);
    if (result.clear_selection_requested) {
      ModelingCommandResult cleared = clear_selection();
      if (!cleared.ok) {
        return cleared;
      }
      cleared.changed = cleared.changed || result.command.changed;
      return cleared;
    }
    if (result.command.changed) {
      invalidate_selection_overlay_caches();
    }
    return command_executor_.from_runtime(result.command);
  }

  ModelingCommandResult
  set_active_runtime_tool(quader_editor_core::ViewportInteractionTool tool) {
    const bool preserve_group_selection =
        app_selection_mode_ == ModelingSelectionMode::Group;
    const bool preserve_scene_transform_selection =
        !preserve_group_selection && !sdk_selection_active() &&
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
    std::optional<ModelingSelectionMode> required_selection_mode;
    if (tool == ModelingViewportTool::InsertEdgeLoop) {
      required_selection_mode = ModelingSelectionMode::Edge;
    } else if (tool == ModelingViewportTool::Mirror) {
      required_selection_mode = ModelingSelectionMode::Object;
    }
    if (required_selection_mode.has_value() &&
        app_selection_mode_ != *required_selection_mode) {
      const ModelingCommandResult mode_result =
          set_selection_mode(*required_selection_mode);
      if (!mode_result.ok) {
        return mode_result;
      }
      selection_mode_changed = mode_result.changed;
      selection_mode_message = mode_result.message;
    }
    const bool preserve_group_selection =
        app_selection_mode_ == ModelingSelectionMode::Group;
    const bool preserve_scene_transform_selection =
        !preserve_group_selection && !sdk_selection_active() &&
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
    case quader_editor_core::ViewportInteractionTool::LassoSelect:
    case quader_editor_core::ViewportInteractionTool::Move:
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
  pick_scene_surface(const ModelingRay &ray) const {
    const ModelingSelectionProjectionSnapshot &projection =
        selection_projection_snapshot();
    return selection_service_.pick_surface_hit(projection, scene_,
                                               object_registry_, ray);
  }

  [[nodiscard]] std::optional<double>
  pick_mesh_surface_distance(const ModelingRay &ray) const {
    const auto &projection = sdk_projection_adapter_.selection_projection_snapshot();
    const auto hit = selection_service_.pick_surface_hit(
        projection, scene_, object_registry_, ray);
    return hit.has_value() ? std::optional<double>{hit->ray_distance} : std::nullopt;
  }

  [[nodiscard]] ModelingMeshSnapQueryResult
  pick_mesh_snap_query(const ModelingRay &ray, double vertex_radius,
                       bool exclude_current_selection) const {
    if (!std::isfinite(vertex_radius) || vertex_radius <= 0.0) {
      return {};
    }

    ++mesh_snap_query_count_;
    const quader_poly::PickOptions options =
        quader_editor_core::move_vertex_snap_pick_options(vertex_radius);
    const auto &projection =
        sdk_projection_adapter_.selection_projection_snapshot();
    const std::vector<quader_editor_core::PolygonObject> &objects =
        selection_service_.cached_local_picking_objects(
            projection, scene_, object_registry_);
    const quader_editor_core::MoveVertexSnapQueryResult query =
        quader_editor_core::resolve_move_vertex_snap_query(
            objects, projection.selection_mode(), to_runtime(ray),
            vertex_radius,
            selection_service_.cached_pick_object_bounds(
                projection, scene_, object_registry_),
            selection_service_.cached_local_pick_face_geometry(
                projection, scene_, object_registry_, options),
            selection_service_.cached_local_picking_transforms(
                projection, scene_, object_registry_),
            exclude_current_selection, &mesh_snap_picking_scratch_);

    ModelingMeshSnapQueryResult result{
        .nearest_surface_ray_distance =
            query.nearest_surface_ray_distance,
    };
    if (query.vertex_target.has_value()) {
      result.vertex_target = ModelingMeshVertexSnapTarget{
          .modeling_object_id = query.vertex_target->object_id,
          .vertex_id = query.vertex_target->vertex_id,
          .world_position = query.vertex_target->position,
          .ray_distance = query.vertex_target->ray_distance,
          .pick_distance = query.vertex_target->pick_distance,
      };
    }
    return result;
  }

  [[nodiscard]] std::optional<ModelingMeshVertexSnapTarget>
  pick_mesh_vertex_snap_target(const ModelingRay &ray, double vertex_radius,
                               bool exclude_current_selection,
                               std::optional<double>
                                   maximum_ray_distance) const {
    if (!std::isfinite(vertex_radius) || vertex_radius <= 0.0 ||
        (maximum_ray_distance.has_value() &&
         (!std::isfinite(*maximum_ray_distance) ||
          *maximum_ray_distance < 0.0))) {
      return std::nullopt;
    }

    ++mesh_snap_query_count_;
    const auto &projection =
        sdk_projection_adapter_.selection_projection_snapshot();
    const std::vector<quader_editor_core::PolygonObject> &objects =
        selection_service_.cached_local_picking_objects(
            projection, scene_, object_registry_);
    const std::optional<quader_editor_core::MoveVertexSnapTarget> target =
        quader_editor_core::resolve_move_vertex_snap_candidate(
            objects, projection.selection_mode(), to_runtime(ray),
            vertex_radius,
            selection_service_.cached_pick_object_bounds(
                projection, scene_, object_registry_),
            selection_service_.cached_local_picking_transforms(
                projection, scene_, object_registry_),
            exclude_current_selection, maximum_ray_distance,
            &mesh_snap_picking_scratch_);
    if (!target.has_value()) {
      return std::nullopt;
    }
    return ModelingMeshVertexSnapTarget{
        .modeling_object_id = target->object_id,
        .vertex_id = target->vertex_id,
        .world_position = target->position,
        .ray_distance = target->ray_distance,
        .pick_distance = target->pick_distance,
    };
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
    const quader_editor_core::InsertEdgeLoopToolState state =
        runtime_.insert_edge_loop_state();
    if (!state.dragging || state.preview_lines.empty()) {
      return {.ok = false,
              .message = "Select an edge to insert an edge loop."};
    }

    ModelingCommandResult result = sdk_projection_adapter_.insert_edge_loop(
        state.object_id, state.edge.a, state.edge.b, state.factor);
    if (!result.ok) {
      runtime_.set_insert_edge_loop_dragging(false);
      if (result.message.empty()) {
        result.message = "Could not insert edge loop.";
      }
      return result;
    }

    runtime_.reset_insert_edge_loop_state();
    const ModelingCommandResult selected = set_active_runtime_tool(
        quader_editor_core::ViewportInteractionTool::Select);
    runtime_.clear_hover();
    runtime_.clear_gizmo_hover();
    runtime_.clear_transform_drag_state();
    invalidate_selection_overlay_caches();
    result.ok = result.ok && selected.ok;
    result.changed = result.changed || selected.changed;
    if (result.message.empty()) {
      result.message = selected.message;
    }
    return result;
  }

  [[nodiscard]] static KnifeHoverOverlayKey knife_hover_overlay_key(
      const quader_editor_core::KnifeToolState &state) {
    return {
        .status = state.status,
        .has_current = state.has_current,
        .current_target_valid = state.current_target_valid,
        .current_object_id = state.current_object_id,
        .target_object_id = state.target_object_id,
        .target_kind = static_cast<int>(state.current_target.kind),
        .vertex_id = state.current_target.vertex_id,
        .edge_a = state.current_target.edge.a,
        .edge_b = state.current_target.edge.b,
        .edge_factor = state.current_target.edge_factor,
        .face_id = state.current_target.face_id,
        .target_x = state.current_target.position.x,
        .target_y = state.current_target.position.y,
        .target_z = state.current_target.position.z,
        .current_x = state.current_point.x,
        .current_y = state.current_point.y,
        .current_z = state.current_point.z,
        .normal_x = state.face_normal.x,
        .normal_y = state.face_normal.y,
        .normal_z = state.face_normal.z,
        .preview_line_count = state.preview_lines.size(),
        .preview_point_count = state.preview_points.size(),
    };
  }

  ModelingCommandResult clear_knife_hover() {
    const KnifeHoverOverlayKey overlay_before =
        knife_hover_overlay_key(runtime_.knife_state());
    runtime_.clear_knife_hover();
    const bool changed =
        overlay_before != knife_hover_overlay_key(runtime_.knife_state());
    if (changed) {
      overlay_service_.invalidate_composed_frame();
      invalidate_volatile_overlay_cache();
    }
    return {.ok = true, .changed = changed};
  }

  ModelingCommandResult update_knife_hover(const ModelingToolRayEvent &event) {
    const KnifeHoverOverlayKey overlay_before =
        knife_hover_overlay_key(runtime_.knife_state());
    auto finish = [&](bool ok, std::string message) {
      const bool changed =
          overlay_before != knife_hover_overlay_key(runtime_.knife_state());
      if (changed) {
        overlay_service_.invalidate_composed_frame();
        invalidate_volatile_overlay_cache();
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
    const ModelingSelectionProjectionSnapshot &projection =
        selection_projection_snapshot();
    for (std::size_t index = 0U; index < local_hits.size(); ++index) {
      if (!local_hits[index].has_value()) {
        continue;
      }
      target_valid = runtime_.update_knife_preview_from_hit(
          projection.object_records(), *local_hits[index],
          local_events[index]->grid_size, false, false);
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
    const int previous_target_object_id =
        runtime_.knife_state().target_object_id;
    const bool changed =
        runtime_.accept_knife_current_point(local_event->grid_size);
    if (changed) {
      if (runtime_.knife_state().target_object_id !=
          previous_target_object_id) {
        invalidate_selection_overlay_caches();
      } else {
        overlay_service_.invalidate_composed_frame();
        invalidate_volatile_overlay_cache();
      }
    }
    return {.ok = true, .changed = resolved.changed || changed};
  }

  ModelingCommandResult commit_knife_tool() {
    const quader_editor_core::KnifeToolState state = runtime_.knife_state();
    if (state.segments.empty()) {
      runtime_.reset_knife_state();
      invalidate_selection_overlay_caches();
      return {.ok = true};
    }
    if (state.target_object_id <= 0) {
      return {.ok = false, .message = "Knife needs a locked target mesh."};
    }

    const auto target_kind = [](quader_poly::KnifePointTargetKind kind) {
      switch (kind) {
      case quader_poly::KnifePointTargetKind::ExistingVertex:
        return ModelingSdkKnifeTargetKind::ExistingVertex;
      case quader_poly::KnifePointTargetKind::ExistingEdge:
        return ModelingSdkKnifeTargetKind::ExistingEdge;
      case quader_poly::KnifePointTargetKind::InsertedVertex:
        return ModelingSdkKnifeTargetKind::InsertedVertex;
      case quader_poly::KnifePointTargetKind::FacePoint:
        return ModelingSdkKnifeTargetKind::FacePoint;
      }
      return ModelingSdkKnifeTargetKind::ExistingVertex;
    };
    std::vector<ModelingSdkKnifeTarget> points;
    points.reserve(state.points.size());
    for (const quader_poly::KnifePointTarget &point : state.points) {
      points.push_back({
          .kind = target_kind(point.kind),
          .vertex_id = static_cast<int>(point.vertex_id),
          .edge_a = static_cast<int>(point.edge.a),
          .edge_b = static_cast<int>(point.edge.b),
          .edge_factor = point.edge_factor,
          .face_id = static_cast<int>(point.face_id),
          .position = point.position,
      });
    }
    std::vector<ModelingSdkKnifeStrokeSegment> segments;
    segments.reserve(state.segments.size());
    for (const quader_poly::KnifeStrokeSegment segment : state.segments) {
      segments.push_back({
          .first_point = static_cast<std::uint32_t>(segment.first_point),
          .second_point = static_cast<std::uint32_t>(segment.second_point),
      });
    }

    ModelingCommandResult result = sdk_projection_adapter_.knife_stroke(
        state.target_object_id, points, segments);
    invalidate_selection_overlay_caches();
    if (result.ok) {
      runtime_.reset_knife_state();
      const ModelingCommandResult select_tool = set_active_runtime_tool(
          quader_editor_core::ViewportInteractionTool::Select);
      return {
          .ok = select_tool.ok,
          .changed = result.changed || select_tool.changed,
          .message =
              !result.message.empty() ? result.message : select_tool.message,
      };
    }
    quader_editor_core::KnifeToolState invalid = state;
    invalid.status = quader_editor_core::KnifeToolStatus::InvalidTarget;
    runtime_.set_knife_state(std::move(invalid));
    return result;
  }

  ModelingCommandResult update_cut_hover(const ModelingToolRayEvent &event) {
    const quader_editor_core::EditorRuntimeCommand command =
        RuntimeToolCommandService::update_cut_hover(event);
    const quader_editor_core::EditorRuntimeCommandResult result =
        quader_editor_core::execute_cut_hover_runtime_command(
            runtime_selection_view(), runtime_, command);
    if (result.command.changed) {
      const quader_editor_core::CutToolState &cut = runtime_.cut_state();
      const bool inert_hover_over_retained_preview =
          !event.dragging && quader_editor_core::cut_can_preview(cut) &&
          cut.preview_valid && !quader_editor_core::cut_is_dragging(cut);
      if (!inert_hover_over_retained_preview) {
        invalidate_selection_overlay_caches();
      }
    }
    return command_executor_.from_runtime(result.command);
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
    const quader_editor_core::CutToolState &cut = runtime_.cut_state();
    const std::vector<QVec3> world_points = [&]() {
      std::vector<QVec3> result;
      for (quader::QVec3 point : quader_editor_core::cut_plane_points(cut)) {
        result.push_back(from_runtime(point));
      }
      return result;
    }();
    if (world_points.size() < 3U || cut.target_object_ids.empty()) {
      return {.ok = false,
              .changed = false,
              .message = "Cut needs a valid plane and selected meshes."};
    }
    std::vector<ModelingSdkPlaneCutRequest> requests;
    requests.reserve(cut.target_object_ids.size());
    for (int object_id : cut.target_object_ids) {
      const std::optional<QMat4> world =
          modeling_object_world_transform(object_id);
      if (!world.has_value()) {
        return {.ok = false,
                .changed = false,
                .message = "Cut target mesh world transform was not found."};
      }
      const QMat4 local_from_world = inverse(*world);
      requests.push_back({
          .modeling_object_id = object_id,
          .local_plane_points = {
              transform_point(local_from_world, world_points[0]),
              transform_point(local_from_world, world_points[1]),
              transform_point(local_from_world, world_points[2]),
          },
      });
    }
    ModelingCommandResult result = sdk_projection_adapter_.plane_cut(
        requests, from_runtime(cut.mode), true);
    if (result.ok) {
      runtime_.reset_cut_state();
      const ModelingCommandResult selected = set_active_runtime_tool(
          quader_editor_core::ViewportInteractionTool::Select);
      result.ok = selected.ok;
      result.changed = result.changed || selected.changed;
      if (result.message.empty()) {
        result.message = selected.message;
      }
      invalidate_selection_overlay_caches();
    }
    return result;
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
    const quader_editor_core::MirrorToolState &mirror = runtime_.mirror_state();
    const std::vector<quader::QVec3> runtime_points =
        quader_editor_core::mirror_plane_points(mirror);
    if (runtime_points.size() < 3U || mirror.target_object_ids.empty()) {
      return {.ok = false,
              .changed = false,
              .message = "Mirror needs a valid plane and selected meshes."};
    }
    std::vector<ModelingSdkPlaneCutRequest> requests;
    requests.reserve(mirror.target_object_ids.size());
    for (int object_id : mirror.target_object_ids) {
      const std::optional<QMat4> world =
          modeling_object_world_transform(object_id);
      if (!world.has_value()) {
        return {.ok = false,
                .changed = false,
                .message =
                    "Mirror target mesh world transform was not found."};
      }
      const QMat4 local_from_world = inverse(*world);
      requests.push_back({
          .modeling_object_id = object_id,
          .local_plane_points = {
              transform_point(local_from_world,
                              from_runtime(runtime_points[0])),
              transform_point(local_from_world,
                              from_runtime(runtime_points[1])),
              transform_point(local_from_world,
                              from_runtime(runtime_points[2])),
          },
      });
    }
    ModelingCommandResult result =
        sdk_projection_adapter_.mirror_objects_across_planes(requests);
    if (result.ok) {
      runtime_.reset_mirror_state();
      const ModelingCommandResult selected = set_active_runtime_tool(
          quader_editor_core::ViewportInteractionTool::Select);
      result.ok = selected.ok;
      result.changed = result.changed || selected.changed;
      if (result.message.empty()) {
        result.message = selected.message;
      }
      invalidate_selection_overlay_caches();
    }
    return result;
  }

  ModelingCommandResult update_poly_hover(const ModelingToolRayEvent &event) {
    const std::optional<quader_editor_core::PickHitRecord> surface_hit =
        runtime_.poly_state().points.empty()
            ? pick_scene_surface(event.ray)
            : std::nullopt;
    return execute_runtime_command(
        RuntimeToolCommandService::update_poly_hover(
            event, surface_hit),
        false);
  }

  ModelingCommandResult accept_poly_point(const ModelingToolRayEvent &event) {
    const ModelingCommandResult resolved = update_poly_hover(event);
    if (!resolved.ok) {
      return resolved;
    }
    ModelingCommandResult accepted = execute_runtime_command(
        RuntimeToolCommandService::accept_poly_point(), false);
    accepted.changed = accepted.changed || resolved.changed;
    return accepted;
  }

  [[nodiscard]] ModelingSdkProjectionAdapter::CreationFinalizer
  sdk_creation_finalizer(bool world_texture_projection) {
    const ModelingInstanceEditScope edit_scope =
        instance_edit_scope_provider_ ? instance_edit_scope_provider_()
                                      : ModelingInstanceEditScope{};
    return [this, edit_scope, world_texture_projection](
               const ModelingObjectRecord &created) {
      if (edit_scope.definition == kInvalidEntityId) {
        return true;
      }
      // Capture the construction chart before the entity is placed in the
      // definition. Its displayed occurrence may already have a lock correction.
      const std::optional<QMat4> desired_sampling = world_texture_projection
          ? effective_texture_sampling_from_local(scene_, created.entity)
          : std::nullopt;
      if (world_texture_projection && !desired_sampling.has_value()) {
        return false;
      }
      const InstanceOccurrenceComponent *occurrence =
          scene_.instance_occurrence(edit_scope.occurrence);
      const TransformComponent *created_transform =
          scene_.transform(created.entity);
      const std::optional<QMat4> occurrence_inverse =
          occurrence != nullptr &&
                  occurrence->definition == edit_scope.definition
              ? try_scene_matrix_inverse(
                    scene_.world_transform_matrix(edit_scope.occurrence))
              : std::nullopt;
      if (!scene_.is_instance_definition_entity(edit_scope.definition) ||
          created_transform == nullptr || !occurrence_inverse.has_value()) {
        return false;
      }
      const QMat4 definition_world =
          scene_.world_transform_matrix(edit_scope.definition);
      const QMat4 definition_local_world = scene_transform_multiply(
          *occurrence_inverse,
          scene_.world_transform_matrix(created.entity));
      const std::optional<TransformComponent> member_local =
          try_scene_local_transform_preserving_world(
              definition_world,
              scene_transform_multiply(definition_world,
                                       definition_local_world),
              *created_transform);
      if (!member_local.has_value() ||
          !mesh_sync_service_.place_created_scene_object(
              scene_, created.entity, edit_scope.definition, *member_local)) {
        return false;
      }
      if (!desired_sampling.has_value()) {
        return true;
      }
      const std::optional<TextureMappingComponent> member_mapping =
          texture_mapping_for_effective_sampling(
              scene_, created.entity, *desired_sampling, edit_scope.occurrence);
      return member_mapping.has_value() &&
             mesh_sync_service_.set_projected_texture_mapping(
                 scene_, created.entity, *member_mapping);
    };
  }

  ModelingCommandResult commit_poly_tool(MaterialReference material = {}) {
    const quader_editor_core::PolyToolState state = runtime_.poly_state();
    if (state.points.size() < 3U) {
      return {.ok = false,
              .message = "Poly needs at least three vertices."};
    }
    ModelingCreateBoxResult created = sdk_projection_adapter_.create_face(
        state.points, state.face_normal, "Poly", material,
        sdk_creation_finalizer(true));
    invalidate_selection_overlay_caches();
    if (created.ok) {
      runtime_.reset_poly_state();
      const ModelingCommandResult select_tool = set_active_runtime_tool(
          quader_editor_core::ViewportInteractionTool::Select);
      return {
          .ok = select_tool.ok,
          .changed = created.changed || select_tool.changed,
          .message = !created.message.empty()
                         ? created.message
                         : select_tool.message,
      };
    }
    return {.ok = created.ok,
            .changed = created.changed,
            .message = created.message};
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
    ModelingCreateBoxResult created =
        sdk_projection_adapter_.create_box(request, sdk_creation_finalizer(true));
    if (created.ok) {
      invalidate_selection_overlay_caches();
    }
    return created;
  }

  ModelingBoxPreview update_box_hover(const ModelingBoxToolEvent &event) {
    return execute_box_preview_command(RuntimeToolCommandService::update_box_hover(
                                           event, pick_scene_surface(event.ray)),
                                       event.snap_enabled);
  }

  ModelingBoxPreview begin_box_footprint(const ModelingBoxToolEvent &event) {
    ModelingBoxPreview preview = execute_box_preview_command(
        RuntimeToolCommandService::begin_box_footprint(
            event, pick_scene_surface(event.ray)),
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

  [[nodiscard]] ModelingBoxDragState box_drag_state() const {
    return from_runtime(runtime_.box_state().drag_state);
  }

  [[nodiscard]] ModelingBoxPreview box_preview(bool snap_enabled = true) const {
    return current_box_preview(snap_enabled);
  }

  ModelingCreateBoxResult commit_box(std::string name) {
    const ModelingBoxPreview preview = current_box_preview(true);
    if (!preview.ok || !preview.valid || preview.bounds_empty) {
      return {
          .ok = false,
          .changed = false,
          .message = preview.message.empty()
                         ? "Box preview is not ready to commit."
                         : preview.message,
      };
    }
    ModelingCreateBoxResult created =
        sdk_projection_adapter_.create_box_from_corners(
            preview.corners, name.empty() ? std::string_view{"Box"} : name,
            {}, sdk_creation_finalizer(true));
    if (!created.ok) {
      return created;
    }
    invalidate_selection_overlay_caches();
    const ModelingCommandResult reset_tool = cancel_box();
    created.changed = created.changed || reset_tool.changed;
    if (!reset_tool.ok) {
      created.ok = false;
      created.message =
          reset_tool.message.empty()
              ? "Box committed, but the native box tool could not reset."
              : reset_tool.message;
    }
    return created;
  }

  ModelingCommandResult cancel_box() {
    const ModelingCommandResult result = execute_runtime_command(
        RuntimeToolCommandService::cancel_box(), false);
    const ModelingCommandResult select_tool = set_active_runtime_tool(
        quader_editor_core::ViewportInteractionTool::Select);
    return {
        .ok = result.ok && select_tool.ok,
        .changed = result.changed || select_tool.changed,
        .message = !result.message.empty() ? result.message
                                           : select_tool.message,
    };
  }

  ModelingCreateBoxResult
  create_mesh(const ModelingCreateMeshRequest &request) {
    ModelingCreateBoxResult created =
        sdk_projection_adapter_.create_mesh(
            request, sdk_creation_finalizer(request.world_texture_projection));
    if (created.ok) {
      invalidate_selection_overlay_caches();
    }
    return created;
  }

  [[nodiscard]] bool curve_target_is_editable(int modeling_object_id) const {
    const auto entity = object_registry_.entity_for_modeling_object(modeling_object_id);
    if (!entity.has_value() || !scene_.exists(*entity) ||
        scene_.curve(*entity) == nullptr ||
        scene_.is_effectively_locked(*entity) ||
        !scene_hierarchy_segment_is_selectable_ignoring_visibility(scene_, *entity) ||
        scene_.is_instance_definition_member(*entity) ||
        scene_.is_instance_definition_entity(*entity)) {
      return false;
    }
    return sdk_projection_adapter_.owns_object(modeling_object_id);
  }

  ModelingCreateBoxResult create_curve(modeling::CurveDocument document,
                                       std::string name) {
    const ModelingInstanceEditScope scope = instance_edit_scope_provider_
        ? instance_edit_scope_provider_() : ModelingInstanceEditScope{};
    if (scope.active()) {
      return {.ok = false,
              .message = "Exit instance editing before creating a curve."};
    }
    auto created = sdk_projection_adapter_.create_curve(std::move(document), name);
    if (created.ok && created.changed) {
      app_selection_mode_ = ModelingSelectionMode::Object;
      invalidate_selection_overlay_caches();
    }
    return created;
  }

  ModelingCreateBoxResult apply_curve_solidify(int modeling_object_id) {
    const auto rejected = [](std::string message) {
      return ModelingCreateBoxResult{
          .ok = false, .changed = false, .message = std::move(message)};
    };
    if (sdk_projection_adapter_.preview_active() || transform_drag_active() ||
        structural_drag_checkpoint_active_ ||
        structural_transform_preview_active() || modal_tool_owns_viewport()) {
      return rejected(
          "Commit or cancel the active tool, drag, or preview before applying "
          "Curve Solidify.");
    }
    const ModelingInstanceEditScope scope = instance_edit_scope_provider_
        ? instance_edit_scope_provider_()
        : ModelingInstanceEditScope{};
    if (scope.active()) {
      return rejected("Exit instance editing before applying Curve Solidify.");
    }
    if (selection_mode() != ModelingSelectionMode::Object ||
        !curve_target_is_editable(modeling_object_id)) {
      return rejected("Select one editable solidified Curve in Object mode.");
    }

    const std::optional<EntityId> source =
        object_registry_.entity_for_modeling_object(modeling_object_id);
    const std::vector<EntityId> selected = scene_.selected_entities();
    if (!source.has_value() || selected.size() != 1U ||
        selected.front() != *source) {
      return rejected("Select only the Curve to apply Solidify.");
    }

    const CurveComponent *current_curve = scene_.curve(*source);
    const ModelingObjectComponent *current_object =
        scene_.modeling_object(*source);
    const VisibilityComponent *current_visibility = scene_.visibility(*source);
    const TransformComponent *current_transform = scene_.transform(*source);
    const NameComponent *current_name = scene_.name(*source);
    const EntityId parent = scene_.parent(*source);
    if (scene_.kind(*source) != EntityKind::Curve ||
        scene_.mesh(*source) != nullptr || current_curve == nullptr ||
        !curve_component_is_valid(*current_curve) || !current_curve->solidify ||
        assets_.curve(current_curve->curve) == nullptr ||
        current_object == nullptr ||
        current_object->object_id != modeling_object_id ||
        !current_object->sdk_owned || !scene_.is_effectively_visible(*source) ||
        (current_visibility != nullptr && !current_visibility->selectable) ||
        !try_scene_matrix_inverse(scene_.world_transform_matrix(*source))
             .has_value()) {
      return rejected("Enable valid Solidify settings on the selected Curve.");
    }
    if (current_transform == nullptr || current_name == nullptr ||
        !scene_transform_is_finite(*current_transform) ||
        scene_.uuid(*source).empty() || parent == kInvalidEntityId ||
        !scene_.exists(parent)) {
      return rejected("The selected Curve placement is invalid.");
    }

    const std::optional<modeling::CurveDocument> curve_document =
        sdk_projection_adapter_.curve_document(modeling_object_id, false);
    if (!curve_document.has_value()) {
      return rejected("The selected Curve document is unavailable.");
    }
    auto tube = modeling::build_curve_tube_mesh(
        *curve_document,
        {.radius = static_cast<double>(current_curve->solidify_radius),
         .radial_resolution =
             static_cast<std::uint32_t>(current_curve->solidify_resolution)});
    if (!tube.ok()) {
      return rejected(tube.error_message().empty()
                          ? "Curve Solidify could not produce an editable mesh."
                          : tube.error_message());
    }

    const CurveComponent source_curve = *current_curve;
    CurveComponent retained_curve = source_curve;
    retained_curve.solidify = false;
    const TransformComponent source_transform = *current_transform;
    const SceneLayerId source_layer = scene_.entity_layer(*source);
    const std::string source_uuid = scene_.uuid(*source);
    const std::string baked_base_name = current_name->name.empty()
                                            ? "Curve Mesh"
                                            : current_name->name + " Mesh";
    const std::string baked_name = SceneEntityNaming::next_name(
        scene_, baked_base_name);
    std::optional<MaterialComponent> source_material_component;
    if (const MaterialComponent *material = scene_.material(*source)) {
      source_material_component = *material;
    }
    const MaterialReference source_material =
        source_material_component.has_value() ? source_material_component->material
                                              : MaterialReference{};

    modeling::CurveTubeMesh baked = std::move(tube).value();
    ModelingCreateBoxResult created =
        sdk_projection_adapter_.create_mesh_document(
            std::move(baked.document), baked_name, source_material,
            [this, source = *source, source_uuid, source_curve,
             retained_curve, parent, source_transform, source_material_component,
             source_layer](const ModelingObjectRecord &created_object) {
              if (!scene_.exists(source) || scene_.uuid(source) != source_uuid) {
                return false;
              }
              const CurveComponent *live_curve = scene_.curve(source);
              if (live_curve == nullptr || *live_curve != source_curve) {
                return false;
              }
              if (!mesh_sync_service_.place_created_scene_object(
                      scene_, created_object.entity, parent, source_transform) ||
                  !scene_.set_entity_layer(created_object.entity,
                                           source_layer) ||
                  (source_material_component.has_value() &&
                   !mesh_sync_service_.set_projected_material(
                       scene_, created_object.entity,
                       &*source_material_component)) ||
                  !scene_.set_curve(source, retained_curve)) {
                return false;
              }
              return true;
            });
    if (created.ok) {
      app_selection_mode_ = ModelingSelectionMode::Object;
      invalidate_selection_overlay_caches();
      created.message = "Applied Curve Solidify.";
    }
    return created;
  }

  [[nodiscard]] std::optional<modeling::CurveDocument> curve_document(
      int modeling_object_id, bool include_preview) const {
    return sdk_projection_adapter_.curve_document(modeling_object_id, include_preview);
  }

  ModelingCommandResult replace_curve_document(int modeling_object_id,
                                                modeling::CurveDocument document) {
    if (!curve_target_is_editable(modeling_object_id)) {
      return {.ok = false, .message = "Curve is not editable."};
    }
    auto result = sdk_projection_adapter_.replace_curve_document(
        modeling_object_id, std::move(document));
    if (result.ok && result.changed) {
      invalidate_selection_overlay_caches();
    }
    return result;
  }

  ModelingCommandResult begin_curve_document_preview(int modeling_object_id) {
    if (!curve_target_is_editable(modeling_object_id) || transform_drag_active()) {
      return {.ok = false, .message = "Curve is not editable."};
    }
    auto result = sdk_projection_adapter_.begin_curve_document_preview(modeling_object_id);
    if (result.ok) {
      invalidate_selection_overlay_caches();
    }
    return result;
  }

  ModelingCommandResult update_curve_document_preview(modeling::CurveDocument document) {
    if (!curve_target_is_editable(
            sdk_projection_adapter_.curve_document_preview_object_id())) {
      return {.ok = false, .message = "Curve is not editable."};
    }
    // Curve previews publish through their own curve-overlay revision. They do
    // not change polygon topology or selection, so keep the retained polygon
    // selection overlay hot throughout pointer sampling.
    return sdk_projection_adapter_.update_curve_document_preview(
        std::move(document));
  }

  [[nodiscard]] bool curve_document_preview_active() const {
    return sdk_projection_adapter_.curve_document_preview_active();
  }

  ModelingCommandResult commit_curve_document_preview() {
    if (!curve_document_preview_active() || !curve_target_is_editable(
            sdk_projection_adapter_.curve_document_preview_object_id())) {
      return {.ok = false, .message = "No editable curve preview is active."};
    }
    return commit_transform_preview();
  }

  ModelingCommandResult cancel_curve_document_preview() {
    if (!curve_document_preview_active()) {
      return {.ok = true, .changed = false};
    }
    return cancel_transform_preview();
  }

  ModelingCommandResult replace_mesh_document(
      int modeling_object_id, quader_poly::Document document,
      bool preserve_selection) {
    ModelingCommandResult result = sdk_projection_adapter_.replace_mesh_document(
        modeling_object_id, document, preserve_selection);
    if (result.ok) {
      invalidate_selection_overlay_caches();
    }
    return result;
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
    ModelingSdkDuplicateBatchResult duplicated =
        sdk_projection_adapter_.duplicate_objects(source_object_ids,
                                                  copies_per_source, true);
    const std::size_t expected_duplicate_count =
        source_plans.size() * copies_per_source;
    if (!duplicated.command.ok || !duplicated.command.changed ||
        duplicated.duplicates.size() != expected_duplicate_count) {
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
        const ModelingObjectRecord &duplicate =
            duplicated.duplicates[duplicate_index];
        const int object_id = duplicate.modeling_object_id;
        const EntityId entity = duplicate.entity;
        if (entity == kInvalidEntityId || !scene_.exists(entity)) {
          return rollback_after_failure(std::string(operation_name) +
                                        " could not create a scene mesh copy.");
        }

        if (!mesh_sync_service_.apply_scene_identity_to_duplicate_entity(
                scene_, entity, source.identity)) {
          return rollback_after_failure(std::string(operation_name) +
                                        " could not apply scene identity.");
        }
        const std::string base_name =
            source.identity.name.empty() ? "Mesh" : source.identity.name;
        if (!scene_.rename_entity(entity, names.next_name(base_name))) {
          return rollback_after_failure(std::string(operation_name) +
                                        " could not name a scene copy.");
        }
        transform_assignments.push_back({
            .entity = entity,
            .transform = source.duplicate_local_transforms[copy_index],
        });
        object_registry_.remember_scene_entity(object_id, scene_, entity);
        elements.push_back(object_record(object_id, entity));
      }
      result.source_elements.push_back(std::move(elements));
    }

    if (!mesh_sync_service_.apply_scene_transforms(scene_,
                                                   transform_assignments)) {
      return rollback_after_failure(std::string(operation_name) +
                                    " could not publish scene transforms.");
    }
    for (std::size_t source_index = 0; source_index < source_plans.size();
         ++source_index) {
      const TransformedDuplicateSourcePlan &source =
          source_plans[source_index];
      for (std::size_t copy_index = 0; copy_index < copies_per_source;
           ++copy_index) {
        const std::size_t duplicate_index =
            source_index * copies_per_source + copy_index;
        const ModelingObjectRecord &duplicate =
            duplicated.duplicates[duplicate_index];
        const int object_id = duplicate.modeling_object_id;
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
      if (identity.id != source.entity) {
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
      if (identity.id != source.entity) {
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
    std::vector<EntityId> sdk_entities;
    std::vector<EntityId> host_entities;
    sdk_entities.reserve(entities.size());
    host_entities.reserve(entities.size());
    for (EntityId entity : entities) {
      (sdk_projection_adapter_.owns_entity(entity) ? sdk_entities
                                                   : host_entities)
          .push_back(entity);
    }
    const EntityId sdk_active = sdk_projection_adapter_.owns_entity(active)
                                    ? active
                                    : (sdk_entities.empty()
                                           ? kInvalidEntityId
                                           : sdk_entities.front());
    ModelingCommandResult result =
        sdk_projection_adapter_.set_object_selection(sdk_entities,
                                                     sdk_active);
    if (result.ok && !host_entities.empty()) {
      result.changed = selection_projection_service_.add_scene_selection(
                           scene_, host_entities,
                           sdk_projection_adapter_.owns_entity(active)
                               ? sdk_active
                               : active) ||
                       result.changed;
    }
    if (result.ok && result.changed) {
      invalidate_selection_overlay_caches();
    }
    return result;
  }

  ModelingCommandResult
  set_component_selection(const ModelingComponentSelectionRequest &request) {
    const std::array one{request};
    return set_component_selections(one);
  }

  ModelingCommandResult apply_resolved_component_selection(
      const ModelingResolvedComponentSelectionRequest &request) {
    const ModelingSelectionProjectionSnapshot &projection =
        selection_projection_snapshot();
    const auto object_it = std::find_if(
        projection.objects().begin(), projection.objects().end(),
        [&](const quader_editor_core::PolygonObject &object) {
          return object.id == request.modeling_object_id;
        });
    if (object_it == projection.objects().end()) {
      return {.ok = false,
              .message = "Resolved component owner is no longer live."};
    }
    const bool kind_matches_mode =
        (request.mode == ModelingSelectionMode::Vertex &&
         request.kind == ModelingActiveSelectionKind::Vertex) ||
        (request.mode == ModelingSelectionMode::Edge &&
         request.kind == ModelingActiveSelectionKind::Edge) ||
        (request.mode == ModelingSelectionMode::Face &&
         request.kind == ModelingActiveSelectionKind::Face);
    if (!kind_matches_mode || request.mode != app_selection_mode_) {
      return {.ok = false,
              .message = "Resolved component target does not match the current selection mode."};
    }

    const quader_poly::Document &document = object_it->document.read();
    const quader_poly::Selection &selected = object_it->selection;
    bool target_live = false;
    bool target_selected = false;
    switch (request.kind) {
    case ModelingActiveSelectionKind::Vertex:
      target_live = quader_poly::find_vertex(document, request.vertex) != nullptr;
      target_selected =
          target_live && quader_poly::selection_contains(selected,
                                                         request.vertex);
      break;
    case ModelingActiveSelectionKind::Edge: {
      const quader_poly::Edge edge = quader_poly::make_edge(
          request.edge.first, request.edge.second);
      target_live = quader_poly::find_vertex(document, edge.a) != nullptr &&
                    quader_poly::find_vertex(document, edge.b) != nullptr;
      target_selected =
          target_live && quader_poly::selection_contains(selected, edge);
      break;
    }
    case ModelingActiveSelectionKind::Face:
      target_live = quader_poly::find_face(document, request.face) != nullptr;
      target_selected = target_live &&
                        quader_poly::selection_contains_face(selected,
                                                            request.face);
      break;
    case ModelingActiveSelectionKind::None:
    case ModelingActiveSelectionKind::Object:
    case ModelingActiveSelectionKind::Light:
      break;
    }
    if (!target_live) {
      return {.ok = false,
              .message = "Resolved component target is no longer live."};
    }

    const bool edge_traversal =
        request.kind == ModelingActiveSelectionKind::Edge &&
        request.edge_traversal != ModelingEdgeSelectionTraversal::None;
    const bool expands_hit = request.click_count > 1 || edge_traversal;
    if (!expands_hit && request.edit == ModelingSelectionEdit::Replace &&
        request.preserve_selection_on_selected_hit && target_selected) {
      return {.ok = true};
    }
    if (!expands_hit && request.edit == ModelingSelectionEdit::Replace &&
        request.preserve_existing_component_selection_on_hit &&
        ModelingSelectionService::has_component_selection(
            selection_summary())) {
      return {.ok = true};
    }

    ModelingComponentSelectionRequest component{
        .modeling_object_id = request.modeling_object_id,
        .mode = request.mode,
        .edit = edge_traversal && request.edit == ModelingSelectionEdit::Toggle
                    ? (target_selected ? ModelingSelectionEdit::Remove
                                       : ModelingSelectionEdit::Add)
                    : request.edit,
    };
    if (request.kind == ModelingActiveSelectionKind::Vertex) {
      if (request.click_count > 1) {
        component.vertices.reserve(document.vertices.size());
        for (const quader_poly::Vertex &vertex : document.vertices) {
          component.vertices.push_back(vertex.id);
        }
      } else {
        component.vertices.push_back(request.vertex);
      }
      component.active_kind = ModelingActiveSelectionKind::Vertex;
      component.active_vertex = component.vertices.empty()
                                    ? request.vertex
                                    : component.vertices.back();
    } else if (request.kind == ModelingActiveSelectionKind::Edge) {
      if (expands_hit) {
        const quader_poly::Edge seed = quader_poly::make_edge(
            request.edge.first, request.edge.second);
        std::vector<quader_poly::Edge> edges =
            request.edge_traversal == ModelingEdgeSelectionTraversal::Ring
                ? quader_poly::edge_ring_edges(document, seed)
                : quader_poly::edge_loop_edges(document, seed, request.face);
        if (request.edge_traversal ==
                ModelingEdgeSelectionTraversal::Loop &&
            request.edit == ModelingSelectionEdit::Replace &&
            target_selected &&
            std::ranges::all_of(edges, [&](quader_poly::Edge edge) {
              return quader_poly::selection_contains(selected, edge);
            })) {
          const std::vector<quader_poly::Edge> boundary =
              quader_poly::edge_boundary_component_edges(document, seed);
          if (!boundary.empty() &&
              !std::ranges::all_of(boundary, [&](quader_poly::Edge edge) {
                return quader_poly::selection_contains(selected, edge);
              })) {
            edges = boundary;
          }
        }
        component.edges.reserve(edges.size());
        for (const quader_poly::Edge edge : edges) {
          component.edges.emplace_back(edge.a, edge.b);
        }
      } else {
        component.edges.push_back(request.edge);
      }
      if (component.edges.empty()) {
        return {.ok = false,
                .message = "Resolved edge loop has no selectable edges."};
      }
      component.active_kind = ModelingActiveSelectionKind::Edge;
      component.active_edge = request.edge;
    } else {
      if (request.click_count > 1) {
        component.faces.reserve(document.faces.size());
        for (const quader_poly::Face &face : document.faces) {
          component.faces.push_back(face.id);
        }
      } else {
        component.faces.push_back(request.face);
      }
      component.active_kind = ModelingActiveSelectionKind::Face;
      component.active_face = component.faces.empty()
                                  ? request.face
                                  : component.faces.back();
    }
    return set_component_selection(component);
  }

  [[nodiscard]] bool resolved_component_body_target_eligible(
      int modeling_object_id, EntityId source_occurrence) const {
    const std::optional<EntityId> entity =
        object_registry_.entity_for_modeling_object(modeling_object_id);
    if (!entity.has_value() || !scene_.exists(*entity)) {
      return false;
    }
    const EntityId definition =
        scene_.containing_instance_definition(*entity);
    const ModelingInstanceEditScope edit_scope =
        instance_edit_scope_provider_ ? instance_edit_scope_provider_()
                                      : ModelingInstanceEditScope{};
    const bool active_instance_edit =
        edit_scope.occurrence != kInvalidEntityId &&
        edit_scope.definition != kInvalidEntityId;
    if (definition != kInvalidEntityId) {
      return active_instance_edit && definition == edit_scope.definition &&
             source_occurrence == edit_scope.occurrence;
    }
    return !active_instance_edit && source_occurrence == *entity;
  }

  ModelingCommandResult apply_resolved_component_body_selection(
      const ModelingResolvedComponentBodySelectionRequest &request) {
    const ModelingSelectionProjectionSnapshot &projection =
        selection_projection_snapshot();
    const auto object_it = std::find_if(
        projection.objects().begin(), projection.objects().end(),
        [&](const quader_editor_core::PolygonObject &object) {
          return object.id == request.modeling_object_id;
        });
    if (object_it == projection.objects().end()) {
      return {.ok = false,
              .message = "Resolved component body owner is no longer live."};
    }
    if (request.mode == ModelingSelectionMode::Object ||
        request.mode == ModelingSelectionMode::Group ||
        request.mode != app_selection_mode_) {
      return {
          .ok = false,
          .message =
              "Resolved component body does not match the current selection mode.",
      };
    }
    if (!resolved_component_body_target_eligible(
            request.modeling_object_id, request.source_occurrence)) {
      return {
          .ok = false,
          .message =
              "Resolved component body is outside the active edit scope.",
      };
    }
    if (request.edit == ModelingSelectionEdit::Remove ||
        request.edit == ModelingSelectionEdit::Toggle) {
      return {.ok = true, .changed = false};
    }
    if (request.preserve_existing_component_selection_on_hit &&
        ModelingSelectionService::has_component_selection(
            selection_summary())) {
      return {.ok = true, .changed = false};
    }
    if (request.preserve_selection_on_selected_hit &&
        (object_it->selected || object_it->component_edit_target)) {
      return {.ok = true, .changed = false};
    }

    ModelingComponentSelectionRequest component{
        .modeling_object_id = request.modeling_object_id,
        .mode = request.mode,
        .edit = request.edit,
        .retain_component_edit_target = true,
    };
    return set_component_selection(component);
  }

  ModelingCommandResult set_component_selections(
      std::span<const ModelingComponentSelectionRequest> requests) {
    const ModelingInstanceEditScope edit_scope =
        instance_edit_scope_provider_ ? instance_edit_scope_provider_()
                                      : ModelingInstanceEditScope{};
    for (const ModelingComponentSelectionRequest &request : requests) {
      const std::optional<EntityId> requested_entity =
          object_registry_.entity_for_modeling_object(
              request.modeling_object_id);
      if (!requested_entity.has_value()) {
        continue;
      }
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
    ModelingCommandResult result =
        sdk_projection_adapter_.set_component_selections(requests);
    if (result.ok && !requests.empty()) {
      app_selection_mode_ = requests.back().mode;
      invalidate_selection_overlay_caches();
    }
    return result;
  }

  ModelingCommandResult apply_resolved_component_selections(
      std::span<const ModelingComponentSelectionRequest> requests,
      ModelingSelectionEdit edit,
      const quader_editor_core::RectangleSelectionState &baseline) {
    const ModelingSelectionMode current_mode = selection_mode();
    if (baseline.mode != selection_projection_snapshot().selection_mode()) {
      return {.ok = false,
              .message = "Resolved component marquee baseline is stale."};
    }

    const ModelingInstanceEditScope edit_scope =
        instance_edit_scope_provider_ ? instance_edit_scope_provider_()
                                      : ModelingInstanceEditScope{};
    quader_editor_core::RectangleSelectionHits hits;
    for (const ModelingComponentSelectionRequest &request : requests) {
      if (request.mode != current_mode || request.edit != edit) {
        return {.ok = false,
                .message = "Resolved component marquee target is stale."};
      }
      const std::optional<EntityId> requested_entity =
          object_registry_.entity_for_modeling_object(
              request.modeling_object_id);
      if (!requested_entity.has_value()) {
        return {.ok = false,
                .message = "Resolved component owner is no longer live."};
      }
      const EntityId definition =
          scene_.containing_instance_definition(*requested_entity);
      const bool active_edit =
          edit_scope.occurrence != kInvalidEntityId &&
          edit_scope.definition != kInvalidEntityId;
      if ((definition != kInvalidEntityId &&
           (!active_edit || definition != edit_scope.definition)) ||
          (active_edit && definition != edit_scope.definition)) {
        return {.ok = false,
                .message =
                    "Component selection is outside the active instance scope."};
      }
      for (const int vertex : request.vertices) {
        hits.vertices.push_back({request.modeling_object_id, vertex});
      }
      for (const auto [a, b] : request.edges) {
        hits.edges.push_back({request.modeling_object_id, a, b});
      }
      for (const int face : request.faces) {
        hits.faces.push_back({request.modeling_object_id, face, {}});
      }
    }

    const ModelingSelectionProjectionSnapshot &projection =
        selection_projection_snapshot();
    const ModelingSelectionProjectionApplyResult applied =
        selection_service_.apply_resolved_rectangle_selection(
            projection, scene_, object_registry_, edit, baseline, hits);
    if (!applied.changed) {
      return {.ok = true, .changed = false};
    }
    ModelingCommandResult result =
        sdk_projection_adapter_.apply_selection_projection_checked(
            applied.projection);
    if (result.ok) {
      app_selection_mode_ = current_mode;
      invalidate_selection_overlay_caches();
    }
    return result;
  }

  ModelingCommandResult move_selection(QVec3 delta) {
    ModelingCommandResult result =
        sdk_projection_adapter_.move_selection(delta);
    if (result.ok) {
      invalidate_selection_overlay_caches();
    }
    return result;
  }

  ModelingCommandResult transform_selection(const ModelingTransform3 &transform,
                                            QVec3 pivot) {
    ModelingCommandResult result =
        sdk_projection_adapter_.transform_selection(transform, pivot);
    if (result.ok) {
      invalidate_selection_overlay_caches();
    }
    return result;
  }

  ModelingCommandResult
  assign_material_slot_to_selection(std::uint32_t material_slot) {
    ModelingCommandResult result =
        sdk_projection_adapter_.assign_material_slot_to_selection(
            material_slot);
    if (result.ok) {
      invalidate_selection_overlay_caches();
    }
    return result;
  }

  ModelingCommandResult
  assign_material_slot_to_face(int modeling_object_id, int face_id,
                               std::uint32_t material_slot) {
    ModelingCommandResult result =
        sdk_projection_adapter_.assign_material_slot_to_face(
            modeling_object_id, face_id, material_slot);
    if (result.ok) {
      invalidate_selection_overlay_caches();
    }
    return result;
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

    std::vector<int> selected_object_ids;
    for (const ModelingSelectionExportObject &object :
         sdk_projection_adapter_.export_selection().objects) {
      if (object.selected) {
        selected_object_ids.push_back(object.modeling_object_id);
      }
    }
    if (selected_object_ids.empty()) {
      return {.ok = false,
              .message =
                  "Select one or more mesh objects before cutting."};
    }
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

    std::vector<ModelingSdkPlaneCutRequest> requests;
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
          .modeling_object_id = object_id,
          .local_plane_points = local_points,
      });
    }

    ModelingCommandResult result = sdk_projection_adapter_.plane_cut(
        requests, ModelingCutMode::KeepBoth, false);
    if (result.ok) {
      if (result.changed) {
        runtime_.set_hover({});
      }
      invalidate_selection_overlay_caches();
    }
    return result;
  }

  ModelingCommandResult
  set_selection_hover(const ModelingDirectHoverRequest &request) {
    quader_editor_core::SelectionHoverState hover;
    hover.object_id = request.modeling_object_id;
    hover.selected_modifier_pressed = request.selected_modifier_pressed;
    hover.face_plane_hover = request.face_plane_hover;
    const bool component_hover =
        request.kind == ModelingActiveSelectionKind::Vertex ||
        request.kind == ModelingActiveSelectionKind::Edge ||
        request.kind == ModelingActiveSelectionKind::Face;
    hover.candidate_object_id =
        request.candidate_object_id > 0
            ? request.candidate_object_id
            : (component_hover ? request.modeling_object_id : -1);
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
      hover.face_id = static_cast<quader_poly::ElementId>(request.face_id);
      break;
    case ModelingActiveSelectionKind::Face:
      hover.kind = quader_editor_core::SelectionHoverKind::Face;
      hover.face_id = static_cast<quader_poly::ElementId>(request.face_id);
      break;
    case ModelingActiveSelectionKind::None:
    case ModelingActiveSelectionKind::Light: {
      const int candidate_object_id = hover.candidate_object_id;
      hover = {};
      hover.candidate_object_id = candidate_object_id;
      break;
    }
    }

    const bool changed =
        !SelectionOverlayCache::hover_matches(runtime_.hover(), hover);
    runtime_.apply_hover_update(quader_editor_core::SelectionHoverUpdateResult{
        .changed = changed,
        .hover = hover,
    });
    if (changed) {
      QDR_PROFILE_SCOPE("Selection hover invalidate composed overlay only");
      overlay_service_.invalidate_composed_frame();
      invalidate_volatile_overlay_cache();
    }
    return {.ok = true, .changed = changed};
  }

private:
  [[nodiscard]] int persistent_tool_source_wire_object_id(
      const ModelingSelectionProjectionSnapshot &projected) const {
    const quader_editor_core::KnifeToolState &knife = runtime_.knife_state();
    if (runtime_.active_tool() !=
            quader_editor_core::ViewportInteractionTool::Knife ||
        !knife.active) {
      return -1;
    }
    return knife.target_object_id > 0
               ? knife.target_object_id
               : projected.active_object_id();
  }

  /** Borrows the cached native projection for read-only selection queries. */
  [[nodiscard]] const ModelingSelectionProjectionSnapshot &
  selection_projection_snapshot() const {
    return sdk_projection_adapter_.selection_projection_snapshot();
  }

  /** Borrows only the neutral facts required by retained tool policy. */
  [[nodiscard]] quader_editor_core::EditorRuntimeSelectionView
  runtime_selection_view() const {
    const ModelingSelectionProjectionSnapshot &selection =
        selection_projection_snapshot();
    return {
        .object_records = &selection.object_records(),
        .mode = selection.selection_mode(),
        .active_object_id = selection.active_object_id(),
    };
  }

  void invalidate_selection_overlay_caches() const override {
    overlay_service_.invalidate();
    invalidate_volatile_overlay_cache();
  }

  void invalidate_volatile_overlay_cache() const {
    volatile_overlay_cache_.valid = false;
    ++volatile_overlay_runtime_revision_;
  }

  [[nodiscard]] bool scene_objects_synced_for_current_modeling_content() const {
    return true;
  }

  [[nodiscard]] bool has_transform_preview() const {
    return sdk_projection_adapter_.preview_active();
  }

  [[nodiscard]] ModelingTransformDragServiceContext
  transform_drag_context(DocumentCommandService *document_commands = nullptr,
                         const ModelingStructuralDuplicate
                             *structural_duplicate = nullptr,
                         EditorHistoryService *history = nullptr) {
    if (transform_drag_projection_cache_ == nullptr) {
      refresh_transform_drag_projection_cache();
    }
    return {
        .scene = scene_,
        .assets = assets_,
        .session = *transform_drag_projection_cache_,
        .runtime = runtime_,
        .port = *this,
        .document_commands = document_commands,
        .structural_duplicate = structural_duplicate,
        .history = history,
        .move_auto_merge_vertices =
            interaction_settings().move_auto_merge_vertices,
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

  [[nodiscard]] bool has_portable_modeling_selection() const override {
    const ModelingSelectionMode mode = sdk_projection_adapter_.selection_mode();
    return sdk_selection_active() && mode != ModelingSelectionMode::Object &&
           mode != ModelingSelectionMode::Group;
  }

  [[nodiscard]] ModelingSelectionMode
  portable_modeling_selection_mode() const override {
    return sdk_projection_adapter_.selection_mode();
  }

  [[nodiscard]] bool portable_modeling_preview_active() const override {
    return sdk_projection_adapter_.preview_active();
  }

  ModelingCommandResult begin_portable_transform_preview(
      bool duplicate_selected, bool topology_stable_transform,
      std::span<const ModelingStructuralEntityClone> structural_duplicates =
          {}) override {
    ModelingCommandResult result =
        sdk_projection_adapter_.begin_translate_preview(
            duplicate_selected, topology_stable_transform,
            structural_duplicates);
    if (!result.ok) {
      transform_drag_local_inputs_.clear();
      transform_drag_preview_transforms_.clear();
      return result;
    }

    // Duplication may replace the selected SDK objects. Refresh the stable
    // borrowed session in place, then resolve every world/local basis once.
    refresh_transform_drag_projection_cache();
    std::string message;
    if (!initialize_transform_drag_local_inputs(message)) {
      (void)sdk_projection_adapter_.cancel_translate_preview();
      return {
          .ok = false,
          .changed = false,
          .message = std::move(message),
      };
    }
    return result;
  }

  ModelingCommandResult
  update_portable_transform_preview(
      const quader_editor_core::TransformDragState &drag,
      double grid_size) override {
    QMat4 world_affine;
    ModelingSdkTransformPreviewKind kind =
        ModelingSdkTransformPreviewKind::Transform;
    float closed_edge_ledge_size = 0.0F;
    if (drag.tool == quader_editor_core::ViewportInteractionTool::Move) {
      world_affine = world_translation_affine(
          from_runtime(drag.move.preview_delta));
      if (drag.operation ==
          quader_editor_core::TransformDragOperation::Extrude) {
        kind = ModelingSdkTransformPreviewKind::ExtrudeOffset;
        closed_edge_ledge_size =
            static_cast<float>(std::max(grid_size, 0.0001));
      }
    } else if (drag.tool ==
               quader_editor_core::ViewportInteractionTool::Rotate) {
      const quader_poly::Transform3 transform =
          quader_editor_core::rotate_preview_transform(drag.rotate);
      world_affine = world_affine_from_transform(
          transform, from_runtime(drag.pivot));
      if (drag.operation ==
          quader_editor_core::TransformDragOperation::Extrude) {
        kind = ModelingSdkTransformPreviewKind::TransformExtrude;
      }
    } else if (drag.tool ==
               quader_editor_core::ViewportInteractionTool::Scale) {
      world_affine = world_affine_from_transform(
          quader_editor_core::oriented_scale_transform(
              drag.scale.preview_scale, drag.orientation),
          from_runtime(drag.pivot));
      if (drag.operation ==
          quader_editor_core::TransformDragOperation::Inset) {
        kind = ModelingSdkTransformPreviewKind::Inset;
      }
    } else {
      return {.ok = false,
              .changed = false,
              .message = "Portable transform preview tool is unsupported."};
    }

    std::string message;
    if (!update_transform_drag_local_transforms(world_affine, message)) {
      return {.ok = false, .changed = false, .message = std::move(message)};
    }
    return sdk_projection_adapter_.update_object_transform_preview(
        transform_drag_preview_transforms_, kind, closed_edge_ledge_size);
  }

  ModelingCommandResult commit_portable_translate_preview() override {
    return sdk_projection_adapter_.commit_translate_preview(
        runtime_.transform_drag_state().move_auto_merge_vertices);
  }

  [[nodiscard]] bool
  portable_transform_supports_sparse_history() const override {
    return sdk_projection_adapter_.transform_preview_supports_sparse_history();
  }

  ModelingCommandResult cancel_portable_translate_preview() override {
    ModelingCommandResult result =
        sdk_projection_adapter_.cancel_translate_preview();
    if (result.ok && result.changed &&
        preview_checkpoint_hooks_.rollback_without_checkpoint) {
      preview_checkpoint_hooks_.rollback_without_checkpoint();
    }
    return result;
  }

  [[nodiscard]] ModelingSdkTransformHistoryDelta
  take_committed_transform_history_delta() override {
    return sdk_projection_adapter_.take_committed_transform_history_delta();
  }

  ModelingCommandResult apply_transform_history_delta(
      const ModelingSdkTransformHistoryDelta &delta,
      bool use_after) override {
    return sdk_projection_adapter_.apply_transform_history_delta(delta,
                                                                 use_after);
  }

  void record_portable_transform(
      const quader_editor_core::TransformDragState &drag,
      bool duplicate_selected) override {
    const ModelingSelectionProjectionSnapshot &projection =
        selection_projection_snapshot();
    history_bridge_.record_transform(projection, drag, duplicate_selected);
  }

  [[nodiscard]] std::uint64_t modeling_content_revision() const override {
    return content_revision();
  }

  [[nodiscard]] std::uint64_t modeling_selection_revision() const override {
    return selection_revision();
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

    if (const std::optional<SceneBounds> scene_bounds =
            selected_transform_world_bounds(scene_, assets_)) {
      return to_runtime_bounds(*scene_bounds);
    }
    return {};
  }

  [[nodiscard]] const std::vector<quader_editor_core::PolygonObject> &
  cached_scene_transformed_objects_for_picking() const override {
    QDR_PROFILE_FUNCTION();
    const ModelingSelectionProjectionSnapshot &projected =
        selection_projection_snapshot();
    return selection_service_.cached_scene_transformed_objects_for_picking(
        projected, scene_, object_registry_);
  }

  [[nodiscard]] const std::vector<quader_editor_core::PolygonObject> &
  cached_local_objects_for_picking() const override {
    QDR_PROFILE_FUNCTION();
    const ModelingSelectionProjectionSnapshot &projected =
        selection_projection_snapshot();
    return selection_service_.cached_local_picking_objects(
        projected, scene_, object_registry_);
  }

  [[nodiscard]] std::vector<quader_editor_core::PolygonObject>
  scene_transformed_objects_for_picking() const {
    QDR_PROFILE_FUNCTION();
    const ModelingSelectionProjectionSnapshot &projected =
        selection_projection_snapshot();
    return selection_service_.scene_transformed_objects_for_picking(
        projected, scene_, object_registry_);
  }

  [[nodiscard]] const std::vector<quader_editor_core::PickObjectBounds> &
  cached_pick_object_bounds() const override {
    QDR_PROFILE_FUNCTION();
    const ModelingSelectionProjectionSnapshot &projected =
        selection_projection_snapshot();
    return selection_service_.cached_pick_object_bounds(
        projected, scene_, object_registry_);
  }

  [[nodiscard]] const std::vector<quader_editor_core::PickObjectFaceGeometry> &
  cached_pick_face_geometry(
      quader_poly::PickOptions pick_options = {}) const override {
    QDR_PROFILE_FUNCTION();
    const ModelingSelectionProjectionSnapshot &projected =
        selection_projection_snapshot();
    return selection_service_.cached_pick_face_geometry(
        projected, scene_, object_registry_, pick_options);
  }

  [[nodiscard]] const std::vector<
      quader_editor_core::PickObjectFaceGeometry> &
  cached_local_pick_face_geometry(
      quader_poly::PickOptions pick_options = {}) const override {
    QDR_PROFILE_FUNCTION();
    const ModelingSelectionProjectionSnapshot &projected =
        selection_projection_snapshot();
    return selection_service_.cached_local_pick_face_geometry(
        projected, scene_, object_registry_, pick_options);
  }

  [[nodiscard]] std::span<const quader_editor_core::PickObjectTransform>
  cached_local_picking_transforms() const override {
    QDR_PROFILE_FUNCTION();
    const ModelingSelectionProjectionSnapshot &projected =
        selection_projection_snapshot();
    return selection_service_.cached_local_picking_transforms(
        projected, scene_, object_registry_);
  }

  ModelingCommandResult execute_runtime_no_settings(
      quader_editor_core::EditorRuntimeCommandId command_id,
      bool may_change_content) {
    return execute_runtime_command(
        RuntimeToolCommandService::no_settings(command_id), may_change_content);
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
        quader_editor_core::execute_runtime_command(runtime_selection_view(),
                                                    runtime_, command);
    if (!runtime_result.command.ok) {
      return {
          .ok = false,
          .message = runtime_result.command.message,
      };
    }
    if (runtime_result.command.changed) {
      invalidate_selection_overlay_caches();
    }
    if (runtime_result.clear_selection_requested) {
      const ModelingCommandResult cleared = clear_selection();
      if (!cleared.ok) {
        return {
            .ok = false,
            .message = cleared.message,
        };
      }
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
  ModelingMeshSyncService mesh_sync_service_;
  ModelingSdkProjectionAdapter sdk_projection_adapter_;
  mutable FaceTextureEditContext face_texture_context_;
  mutable std::optional<TextureSettingsCacheKey> face_texture_cache_key_;
  mutable std::uint64_t face_texture_revision_ = 0;
  mutable std::uint64_t face_texture_presented_revision_ = 0;
  mutable ModelingSelectedFaceTextureSnapshot face_texture_selection_cache_;
  mutable ModelingFaceTextureSettings face_texture_settings_cache_;
  mutable QVec2 face_texture_extent_cache_{64.0f, 64.0f};
  mutable UvPeelEditContext uv_peel_context_;
  mutable std::optional<TextureSettingsCacheKey> uv_peel_cache_key_;
  mutable std::uint64_t uv_peel_revision_ = 0;
  mutable std::uint64_t uv_peel_presented_revision_ = 0;
  mutable ModelingSelectedUvPeelSnapshot uv_peel_selection_cache_;
  mutable ModelingUvPeelSettings uv_peel_settings_cache_;
  std::function<std::optional<QVec2>(MaterialReference)> face_texture_extent_provider_;
  std::function<std::uint64_t()> face_texture_extent_revision_provider_;
  std::function<double()> face_texture_rotation_step_provider_;
  std::function<TextureLockSettings()> texture_lock_settings_provider_;
  ModelingHierarchyDuplicateService hierarchy_duplicate_service_;
  ModelingSelectionService selection_service_;
  mutable std::uint64_t mesh_snap_query_count_ = 0;
  mutable quader_editor_core::PickAllWithSurfaceScratch
      mesh_snap_picking_scratch_;
  mutable ViewportCurvePickCache curve_box_pick_cache_;
  SelectionProjectionService selection_projection_service_;
  mutable ModelingOverlayService overlay_service_;
  mutable VolatileOverlayCache volatile_overlay_cache_;
  mutable std::uint64_t volatile_overlay_runtime_revision_ = 0u;
  mutable ModelingToolPreviewWorkStats tool_preview_work_stats_;
  ModelingCommandExecutor command_executor_;
  ModelingContextActionService context_action_service_;
  ModelingInteractionSettingsService interaction_settings_service_;
  ModelingToolSettingsService tool_settings_service_;
  ModelingTransformDragService transform_drag_service_;
  SceneTransformDragService structural_preview_scene_transform_;
  std::optional<EditorTransaction> structural_preview_transaction_;
  ModelingStructuralDuplicateResult structural_preview_duplicate_;
  std::optional<ModelingRepeatTransform>
      structural_preview_repeat_transform_;
  EditorHistoryService *structural_preview_history_ = nullptr;
  bool structural_preview_checkpoint_active_ = false;
  bool structural_drag_checkpoint_active_ = false;
  std::unique_ptr<ModelingSelectionProjectionSnapshot>
      transform_drag_projection_cache_;
  std::vector<TransformDragLocalInput> transform_drag_local_inputs_;
  std::vector<ModelingSdkObjectTransformPreview>
      transform_drag_preview_transforms_;
  ModalToolRuntime modal_tool_runtime_;
  std::vector<ModelingObjectSceneIdentity> clipboard_scene_identities_;
  std::optional<QVec3> clipboard_world_center_;
  ModelingHistoryBridge history_bridge_;
  ModelingOperationPreviewCheckpointHooks preview_checkpoint_hooks_;
  std::function<ModelingInstanceEditScope()> instance_edit_scope_provider_;
  ModelingSelectionMode app_selection_mode_ = ModelingSelectionMode::Object;

  mutable std::optional<quader_editor_core::EditorOperationId>
      tool_settings_preview_operation_;
  mutable std::uint64_t tool_settings_interaction_revision_ = 0;
  mutable std::uint64_t tool_settings_metadata_revision_ = 0;
  mutable std::uint64_t tool_settings_revision_ = 0;
  mutable bool tool_settings_cut_active_ = false;
  mutable quader_editor_core::ViewportInteractionTool
      tool_settings_active_tool_ =
          quader_editor_core::ViewportInteractionTool::Unknown;
  mutable bool tool_settings_revision_valid_ = false;

  quader_editor_core::EditorRuntime runtime_;
};

ModelingSessionService::ModelingSessionService(
    SceneDocument &scene, AssetRegistry &assets,
    ModelingOperationPreviewCheckpointHooks preview_checkpoint_hooks)
    : impl_(std::make_unique<Impl>(scene, assets,
                                  std::move(preview_checkpoint_hooks))) {}

ModelingSessionService::~ModelingSessionService() = default;
void ModelingSessionService::detach_preview_checkpoint_hooks() {
  impl_->detach_preview_checkpoint_hooks();
}
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

std::vector<EntityId> ModelingSessionService::matching_geometry_entities(
    std::span<const EntityId> candidates,
    std::span<const EntityId> references) const {
  return impl_->matching_geometry_entities(candidates, references);
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

std::optional<ModelingSelectionHit>
ModelingSessionService::query_selection_hit(
    const ModelingPickSelectRequest &request) const {
  return impl_->query_selection_hit(request);
}

std::vector<ModelingSelectionHit>
ModelingSessionService::query_selection_depth_hits(
    const ModelingPickSelectRequest &request) const {
  return impl_->query_selection_depth_hits(request);
}

std::optional<double> ModelingSessionService::pick_mesh_surface_distance(
    const ModelingRay &ray) const {
  return impl_->pick_mesh_surface_distance(ray);
}

ModelingMeshSnapQueryResult ModelingSessionService::pick_mesh_snap_query(
    const ModelingRay &ray, double vertex_radius,
    bool exclude_current_selection) const {
  return impl_->pick_mesh_snap_query(ray, vertex_radius,
                                     exclude_current_selection);
}

std::optional<ModelingMeshVertexSnapTarget>
ModelingSessionService::pick_mesh_vertex_snap_target(
    const ModelingRay &ray, double vertex_radius,
    bool exclude_current_selection,
    std::optional<double> maximum_ray_distance) const {
  return impl_->pick_mesh_vertex_snap_target(
      ray, vertex_radius, exclude_current_selection, maximum_ray_distance);
}

std::vector<EntityId> ModelingSessionService::box_pick_object_entities(
    const ModelingBoxSelectRequest &request) const {
  QDR_PROFILE_SCOPE("Modeling session box pick entry");
  return impl_->box_pick_object_entities(request);
}

std::vector<ModelingSelectionHit>
ModelingSessionService::query_selection_marquee_hits(
    const ModelingBoxSelectRequest &request) const {
  return impl_->query_selection_marquee_hits(request);
}

std::vector<ModelingSelectionHit> ModelingSessionService::query_selection_lasso_hits(
    const ModelingLassoSelectRequest &request,
    std::vector<EntityId> *eligible_entities) const {
  return impl_->query_selection_lasso_hits(request, eligible_entities);
}

std::span<const ViewportSceneCurvePickCandidate>
ModelingSessionService::scene_curve_pick_candidates() const {
  return impl_->scene_curve_pick_candidates();
}

std::optional<QVec3> ModelingSessionService::selection_center() const {
  return impl_->selection_center();
}

std::optional<ModelingTransform3>
ModelingSessionService::selection_transform_orientation() const {
  return impl_->selection_transform_orientation();
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

std::span<const EntityId>
ModelingSessionService::selected_component_entities() const {
  return impl_->selected_component_entities();
}

std::uint64_t ModelingSessionService::content_revision() const {
  return impl_->content_revision();
}

std::uint64_t ModelingSessionService::selection_revision() const {
  return impl_->selection_revision();
}

std::uint64_t ModelingSessionService::face_texture_settings_revision() const {
  return impl_->face_texture_settings_revision();
}

ModelingFaceTextureSettings ModelingSessionService::face_texture_settings() const {
  return impl_->face_texture_settings();
}

ModelingCommandResult ModelingSessionService::set_face_texture_setting(
    const ModelingFaceTextureEdit &edit) {
  return impl_->set_face_texture_setting(edit);
}

ModelingCommandResult ModelingSessionService::apply_face_texture_action(
    const ModelingFaceTextureActionRequest &request) {
  return impl_->apply_face_texture_action(request);
}

ModelingCommandResult ModelingSessionService::transfer_face_texture_to_face(
    int modeling_object_id, int face_id) {
  return impl_->transfer_face_texture_to_face(modeling_object_id, face_id);
}

std::uint64_t ModelingSessionService::uv_peel_settings_revision() const {
  return impl_->uv_peel_settings_revision();
}

ModelingUvPeelSettings ModelingSessionService::uv_peel_settings() const {
  return impl_->uv_peel_settings();
}

ModelingCommandResult ModelingSessionService::set_uv_peel_options(
    const ModelingUvPeelOptions &options) {
  return impl_->set_uv_peel_options(options);
}

ModelingCommandResult ModelingSessionService::peel_uv(
    const ModelingUvPeelRequest &request) {
  return impl_->peel_uv(request);
}

void ModelingSessionService::invalidate_uv_peel_selection() {
  impl_->invalidate_uv_peel_selection();
}

bool ModelingSessionService::face_texture_treat_as_one() const {
  return impl_->face_texture_treat_as_one();
}

ModelingCommandResult ModelingSessionService::set_face_texture_treat_as_one(
    bool enabled) {
  return impl_->set_face_texture_treat_as_one(enabled);
}

double ModelingSessionService::face_texture_rotation_step() const {
  return impl_->face_texture_rotation_step();
}

void ModelingSessionService::set_face_texture_rotation_step_provider(
    std::function<double()> provider) {
  impl_->set_face_texture_rotation_step_provider(std::move(provider));
}

ModelingFaceTextureEditState
ModelingSessionService::capture_face_texture_edit_state() const {
  return impl_->capture_face_texture_edit_state();
}

void ModelingSessionService::restore_face_texture_edit_state(
    const ModelingFaceTextureEditState &state) {
  impl_->restore_face_texture_edit_state(state);
}

void ModelingSessionService::invalidate_face_texture_edit_state() {
  impl_->invalidate_face_texture_edit_state();
}

void ModelingSessionService::set_face_texture_extent_provider(
    std::function<std::optional<QVec2>(MaterialReference)> provider,
    std::function<std::uint64_t()> revision_provider) {
  impl_->set_face_texture_extent_provider(std::move(provider),
                                         std::move(revision_provider));
}

void ModelingSessionService::set_texture_lock_settings_provider(
    std::function<TextureLockSettings()> provider) {
  impl_->set_texture_lock_settings_provider(std::move(provider));
}

ModelingSelectionExport ModelingSessionService::export_selection() const {
  return impl_->export_selection();
}

ModelingSelectedVerticesSnapshot
ModelingSessionService::selected_vertices_snapshot() const {
  return impl_->selected_vertices_snapshot();
}

ModelingSelectionBoxBaseline
ModelingSessionService::capture_box_selection_baseline() const {
  QDR_PROFILE_SCOPE("Modeling session box baseline entry");
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

ModelingVolatileOverlaySnapshot
ModelingSessionService::volatile_overlay_snapshot() const {
  return impl_->volatile_overlay_snapshot();
}

ViewportOverlayFrame ModelingSessionService::volatile_overlay_frame() const {
  return impl_->volatile_overlay_frame();
}

ModelingVolatileOverlayCacheStats
ModelingSessionService::volatile_overlay_cache_stats() const {
  return impl_->volatile_overlay_cache_stats();
}

std::uint64_t ModelingSessionService::tool_preview_source_revision() const {
  return impl_->tool_preview_source_revision();
}

std::uint64_t ModelingSessionService::tool_preview_signature() const {
  return impl_->tool_preview_signature();
}

std::vector<ModelingToolPreviewMesh>
ModelingSessionService::tool_preview_meshes() const {
  return impl_->tool_preview_meshes();
}

ModelingToolPreviewWorkStats
ModelingSessionService::tool_preview_work_stats() const {
  return impl_->tool_preview_work_stats();
}

bool ModelingSessionService::transient_transform_preview_active() const {
  return impl_->transient_transform_preview_active();
}

std::uint64_t
ModelingSessionService::transient_transform_preview_revision() const {
  return impl_->transient_transform_preview_revision();
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
ModelingSessionService::rectangle_selection_universe_build_count() const {
  return impl_->rectangle_selection_universe_build_count();
}

std::uint64_t
ModelingSessionService::rectangle_selection_resolution_build_count() const {
  return impl_->rectangle_selection_resolution_build_count();
}

std::uint64_t ModelingSessionService::local_face_geometry_build_count() const {
  return impl_->local_face_geometry_build_count();
}

std::uint64_t ModelingSessionService::world_pick_bounds_build_count() const {
  return impl_->world_pick_bounds_build_count();
}

std::uint64_t ModelingSessionService::mesh_snap_query_count() const {
  return impl_->mesh_snap_query_count();
}

const void *
ModelingSessionService::modeling_sdk_session_identity_for_tests() const {
  return impl_->modeling_sdk_session_identity_for_tests();
}

std::size_t ModelingSessionService::modeling_sdk_object_count_for_tests() const {
  return impl_->modeling_sdk_object_count_for_tests();
}

void ModelingSessionService::set_modeling_sdk_projection_failure_for_tests(
    ModelingSdkProjectionFailurePoint point) {
  impl_->set_modeling_sdk_projection_failure_for_tests(point);
}

ModelingSdkProjectionStats
ModelingSessionService::modeling_sdk_projection_stats_for_tests() const {
  return impl_->modeling_sdk_projection_stats_for_tests();
}

void ModelingSessionService::release_redundant_sdk_history() {
  impl_->release_redundant_sdk_history();
}

ModelingSessionMemento ModelingSessionService::capture_history_memento(
    bool preserve_group_selection) const {
  return impl_->capture_history_memento(preserve_group_selection);
}

ModelingSessionMemento
ModelingSessionService::capture_history_memento_without_objects(
    const ModelingSessionMemento &selection_baseline,
    std::span<const int> excluded_object_ids) const {
  if (!selection_baseline.valid()) {
    return {};
  }
  return impl_->capture_history_memento_without_objects(
      selection_baseline.impl_->sdk_projection, excluded_object_ids);
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

ModelingCommandResult
ModelingSessionService::restore_history_memento_exact_for_failure_compensation(
    const ModelingSessionMemento &memento) {
  return impl_->restore_history_memento_exact(memento, true);
}

void ModelingSessionService::clear_transient_state_for_document_replacement() {
  impl_->clear_transient_state_for_document_replacement();
}

void ModelingSessionService::reset_from_scene() { impl_->reset_from_scene(); }

ModelingCommandResult
ModelingSessionService::restore_mesh_documents_from_scene(
    std::span<const EntityId> entities) {
  return impl_->restore_mesh_documents_from_scene(entities);
}

ModelingInstanceEditScope ModelingSessionService::instance_edit_scope() const {
  return impl_->instance_edit_scope();
}

void ModelingSessionService::set_instance_edit_scope_provider(
    std::function<ModelingInstanceEditScope()> provider) {
  impl_->set_instance_edit_scope_provider(std::move(provider));
}

ModelingCommandResult ModelingSessionService::project_object_selection_from_scene(
    bool force_object_mode) {
  return impl_->sync_object_selection_from_scene(force_object_mode);
}

ModelingCommandResult ModelingSessionService::
    clear_runtime_object_selection_for_scene_owned_selection() {
  return impl_->clear_runtime_object_selection_for_scene_owned_selection();
}

ModelingCommandResult
ModelingSessionService::
    duplicate_selected_entity_hierarchy_with_independent_meshes(
        DocumentCommandService &document_commands) {
  return impl_->duplicate_selected_entity_hierarchy_with_independent_meshes(
      document_commands);
}

ModelingHierarchyDuplicateResult
ModelingSessionService::duplicate_entity_hierarchy(
    std::span<const EntityId> roots,
    DocumentCommandService &document_commands,
    ModelingSelectionMode result_selection_mode,
    bool independent_mesh_assets) {
  return impl_->duplicate_entity_hierarchy(
      roots, document_commands, result_selection_mode,
      independent_mesh_assets);
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

ModelingCommandResult ModelingSessionService::apply_component_selection_command(
    ModelingComponentSelectionCommand command) {
  return impl_->apply_component_selection_command(command);
}

ModelingCommandResult ModelingSessionService::select_mesh_ngons(
    std::span<const int> modeling_object_ids) {
  return impl_->select_mesh_ngons(modeling_object_ids);
}

std::unique_ptr<IUndoableCommand>
ModelingSessionService::create_delete_selection_command() {
  return impl_->create_delete_selection_command();
}

std::unique_ptr<IUndoableCommand>
ModelingSessionService::create_destroy_entities_command(
    std::vector<EntityId> roots, std::string label) {
  return impl_->create_destroy_entities_command(std::move(roots),
                                                std::move(label));
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

void ModelingSessionService::record_repeatable_structural_duplicate() {
  impl_->record_repeatable_structural_duplicate();
}

bool ModelingSessionService::
    repeat_last_action_uses_structural_duplicate() const {
  return impl_->repeat_last_action_uses_structural_duplicate();
}

ModelingCommandResult ModelingSessionService::repeat_last_structural_transform(
    EditorTransaction &transaction, EditorHistoryService &history,
    const ModelingStructuralDuplicate &structural_duplicate) {
  return impl_->repeat_last_structural_transform(transaction, history,
                                                 structural_duplicate);
}

bool ModelingSessionService::has_face_texture_repeat() const {
  return impl_->has_face_texture_repeat();
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
ModelingSessionService::active_material_selection_status(
    bool material_reference_available) const {
  return impl_->active_material_selection_status(material_reference_available);
}

ModelingCommandResult
ModelingSessionService::apply_active_material(std::uint32_t material_slot) {
  return impl_->apply_active_material(material_slot);
}

ModelingCommandResult
ModelingSessionService::apply_active_material(MaterialReference material) {
  return impl_->apply_active_material(material);
}

ModelingCommandResult ModelingSessionService::apply_material_to_mesh_at_face(
    int modeling_object_id, int face_id, MaterialReference material) {
  return impl_->apply_material_to_mesh_at_face(modeling_object_id, face_id,
                                               material);
}

ModelingCommandResult ModelingSessionService::apply_material_to_face(
    int modeling_object_id, int face_id, MaterialReference material) {
  return impl_->apply_material_to_face(modeling_object_id, face_id, material);
}

ModelingCommandResult ModelingSessionService::replace_material_references_on_faces(
    std::span<const ModelingFaceMaterialReplacementTarget> targets,
    MaterialReference source, MaterialReference replacement) {
  return impl_->replace_material_references_on_faces(targets, source,
                                                     replacement);
}

ModelingProjectMaterialQueryResult
ModelingSessionService::project_material_for_face(int modeling_object_id,
                                                  int face_id) const {
  return impl_->project_material_for_face(modeling_object_id, face_id);
}

ModelingCommandResult ModelingSessionService::apply_face_material_to_mesh(
    int modeling_object_id, int face_id) {
  return impl_->apply_face_material_to_mesh(modeling_object_id, face_id);
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

bool ModelingSessionService::operation_preview_active() const {
  return impl_->operation_preview_active();
}

ModelingToolSettingsTarget
ModelingSessionService::tool_settings_target() const {
  return impl_->tool_settings_target();
}

std::uint64_t ModelingSessionService::tool_settings_revision() const {
  return impl_->tool_settings_revision();
}

ModelingInteractionSettings
ModelingSessionService::interaction_settings() const {
  return impl_->interaction_settings();
}

ModelingCommandResult ModelingSessionService::set_interaction_settings(
    const ModelingInteractionSettings &settings) {
  return impl_->set_interaction_settings(settings);
}

ModelingCommandResult ModelingSessionService::reset_active_tool_settings() {
  return impl_->reset_active_tool_settings();
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

ModelingCommandResult ModelingSessionService::begin_transform_preview(
    bool duplicate_selected,
    const ModelingStructuralDuplicate &structural_duplicate,
    EditorHistoryService &history) {
  return impl_->begin_transform_preview(duplicate_selected,
                                        structural_duplicate, history);
}

bool ModelingSessionService::structural_transform_preview_active() const {
  return impl_->structural_transform_preview_active();
}

ModelingCommandResult
ModelingSessionService::commit_structural_transform_preview(
    std::string label) {
  return impl_->commit_structural_transform_preview(std::move(label));
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
    DocumentCommandService &document_commands,
    const ModelingStructuralDuplicate &structural_duplicate,
    EditorHistoryService &history) {
  QDR_PROFILE_FUNCTION();
  return impl_->begin_transform_drag(request, &document_commands,
                                     &structural_duplicate, &history);
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
  QDR_PROFILE_SCOPE("Modeling session box select entry");
  return impl_->box_select(request);
}

ModelingSelectionSummary ModelingSessionService::box_select(
    const ModelingBoxSelectRequest &request,
    const ModelingSelectionBoxBaseline &baseline) {
  QDR_PROFILE_SCOPE("Modeling session baseline box select entry");
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

void ModelingSessionService::set_viewport_visibility_filter(
    std::function<bool(EntityId)> filter,
    std::function<std::uint64_t()> revision_provider,
    std::function<bool()> active_provider) {
  impl_->set_viewport_visibility_filter(std::move(filter),
                                        std::move(revision_provider),
                                        std::move(active_provider));
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

ModelingCommandResult ModelingSessionService::accept_poly_point(
    const ModelingToolRayEvent &event) {
  return impl_->accept_poly_point(event);
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

ModelingBoxDragState ModelingSessionService::box_drag_state() const {
  return impl_->box_drag_state();
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

ModelingCreateBoxResult ModelingSessionService::create_curve(
    modeling::CurveDocument document, std::string name) {
  return impl_->create_curve(std::move(document), std::move(name));
}

ModelingCreateBoxResult
ModelingSessionService::apply_curve_solidify(int modeling_object_id) {
  return impl_->apply_curve_solidify(modeling_object_id);
}

std::optional<modeling::CurveDocument> ModelingSessionService::curve_document(
    int modeling_object_id, bool include_preview) const {
  return impl_->curve_document(modeling_object_id, include_preview);
}

ModelingCommandResult ModelingSessionService::replace_curve_document(
    int modeling_object_id, modeling::CurveDocument document) {
  return impl_->replace_curve_document(modeling_object_id, std::move(document));
}

ModelingCommandResult ModelingSessionService::begin_curve_document_preview(
    int modeling_object_id) {
  return impl_->begin_curve_document_preview(modeling_object_id);
}

ModelingCommandResult ModelingSessionService::update_curve_document_preview(
    modeling::CurveDocument document) {
  return impl_->update_curve_document_preview(std::move(document));
}

ModelingCommandResult ModelingSessionService::commit_curve_document_preview() {
  return impl_->commit_curve_document_preview();
}

ModelingCommandResult ModelingSessionService::cancel_curve_document_preview() {
  return impl_->cancel_curve_document_preview();
}

bool ModelingSessionService::curve_document_preview_active() const {
  return impl_->curve_document_preview_active();
}

ModelingCommandResult ModelingSessionService::replace_mesh_document(
    int modeling_object_id, quader_poly::Document document,
    bool preserve_selection) {
  return impl_->replace_mesh_document(modeling_object_id,
                                      std::move(document), preserve_selection);
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

ModelingCommandResult ModelingSessionService::apply_resolved_component_selection(
    const ModelingResolvedComponentSelectionRequest &request) {
  return impl_->apply_resolved_component_selection(request);
}

bool ModelingSessionService::resolved_component_body_target_eligible(
    int modeling_object_id, EntityId source_occurrence) const {
  return impl_->resolved_component_body_target_eligible(modeling_object_id,
                                                        source_occurrence);
}

ModelingCommandResult
ModelingSessionService::apply_resolved_component_body_selection(
    const ModelingResolvedComponentBodySelectionRequest &request) {
  return impl_->apply_resolved_component_body_selection(request);
}

ModelingCommandResult ModelingSessionService::set_component_selections(
    std::span<const ModelingComponentSelectionRequest> requests) {
  return impl_->set_component_selections(requests);
}

ModelingCommandResult ModelingSessionService::apply_resolved_component_selections(
    std::span<const ModelingComponentSelectionRequest> requests,
    ModelingSelectionEdit edit,
    const ModelingSelectionBoxBaseline &baseline) {
  if (!baseline.impl_) {
    return {.ok = false,
            .message = "Resolved component marquee baseline is unavailable."};
  }
  return impl_->apply_resolved_component_selections(
      requests, edit, baseline.impl_->selection);
}

ModelingCommandResult ModelingSessionService::move_selection(QVec3 delta) {
  return impl_->move_selection(delta);
}

ModelingCommandResult
ModelingSessionService::transform_selection(const ModelingTransform3 &transform,
                                            QVec3 pivot) {
  return impl_->transform_selection(transform, pivot);
}

ModelingCommandResult ModelingSessionService::apply_resolved_vertex_positions(
    std::span<const ModelingResolvedVertexPositionBatch> batches) {
  return impl_->apply_resolved_vertex_positions(batches);
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

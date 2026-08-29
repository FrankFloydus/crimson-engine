////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/services/modeling_session_services.hpp"

#include "editor/modeling/modeling_viewport_camera_adapter.hpp"
#include "editor/selection/scene_selection_summary.hpp"

#include "assets/asset_registry.hpp"
#include "diagnostics/profile.hpp"
#include "editor/modeling/services/modeling_instant_operation_catalog.hpp"
#include "editor/modeling/services/modeling_operation_settings.hpp"
#include "editor/modeling/services/modeling_sdk_projection_adapter.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "editor/projection/cut_preview_overlay_projection.hpp"
#include "editor/projection/drawn_plane_overlay_projection.hpp"
#include "editor/projection/picking_cache_projection.hpp"
#include "editor/projection/selection_overlay_projection.hpp"
#include "editor/projection/tool_preview_mesh_projection.hpp"
#include "editor/selection/box_selection_rules.hpp"
#include "editor/tools/viewport_tool_descriptor_catalog.hpp"
#include "math/glm_bridge.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "scene/scene_document.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
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
#include <editor/interactions/selection_box.hpp>
#include <editor/interactions/transform_drag.hpp>
#include <editor/interactions/viewport_projection.hpp>
#include <editor/modeling/runtime/runtime_command.hpp>
#include <quader/modeling/neutral/polygon/document.hpp>
#include <quader/modeling/neutral/polygon/document_selection.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_picking_helpers.hpp>

namespace quader {
namespace {

ModelingCutMode modeling_cut_mode(quader_editor_core::CutMode mode) {
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

bool same_vector(QVec2 left, QVec2 right) {
  return left.x == right.x && left.y == right.y;
}

bool same_vector(QVec3 left, QVec3 right) {
  return left.x == right.x && left.y == right.y && left.z == right.z;
}

bool same_matrix(const QMat4 &left, const QMat4 &right) {
  return left.values == right.values;
}

quader_poly::Ray rectangle_local_pick_ray(
    const quader_editor_core::ViewportCameraState &camera,
    QVec2 screen_position,
    const quader_editor_core::PickObjectTransform *transform) {
  const quader_poly::Ray world_ray =
      quader_editor_core::ray_from_viewport_screen_point(camera,
                                                         screen_position);
  if (transform == nullptr) {
    return world_ray;
  }
  return {
      .origin = transform_point(transform->object_from_world,
                                world_ray.origin),
      .direction = quader::normalize_or_zero(transform_vector(
          transform->object_from_world, world_ray.direction)),
  };
}

quader_poly::Transform3 polygon_transform_from_matrix(const QMat4 &matrix) {
  const auto &m = matrix.values;
  return {
      .x_axis = {m[0], m[1], m[2]},
      .y_axis = {m[4], m[5], m[6]},
      .z_axis = {m[8], m[9], m[10]},
      .origin = {m[12], m[13], m[14]},
  };
}

bool same_rectangle_face(const quader_editor_core::RectangleSelectionFace &left,
                         const quader_editor_core::RectangleSelectionFace &right) {
  return left.object_id == right.object_id &&
         left.face_index == right.face_index &&
         left.vertex_indices == right.vertex_indices;
}

bool same_rectangle_edge(const quader_editor_core::RectangleSelectionEdge &left,
                         const quader_editor_core::RectangleSelectionEdge &right) {
  return left.object_id == right.object_id && left.a_index == right.a_index &&
         left.b_index == right.b_index;
}

bool same_rectangle_vertex(
    const quader_editor_core::RectangleSelectionVertex &left,
    const quader_editor_core::RectangleSelectionVertex &right) {
  return left.object_id == right.object_id &&
         left.vertex_index == right.vertex_index;
}

template <typename Value, typename Equal>
bool same_rectangle_values(const std::vector<Value> &left,
                           const std::vector<Value> &right, Equal equal) {
  return left.size() == right.size() &&
         std::equal(left.begin(), left.end(), right.begin(), equal);
}

bool same_rectangle_selection_state(
    const quader_editor_core::RectangleSelectionState &left,
    const quader_editor_core::RectangleSelectionState &right) {
  return left.mode == right.mode &&
         left.active_object_id == right.active_object_id &&
         left.active_face_index == right.active_face_index &&
         left.selected_object_ids == right.selected_object_ids &&
         same_rectangle_values(left.selected_faces, right.selected_faces,
                               same_rectangle_face) &&
         same_rectangle_values(left.selected_edges, right.selected_edges,
                               same_rectangle_edge) &&
         left.active_edge_vertex_indices == right.active_edge_vertex_indices &&
         left.selected_edge_full_object_ids ==
             right.selected_edge_full_object_ids &&
         same_rectangle_values(left.selected_vertices, right.selected_vertices,
                               same_rectangle_vertex) &&
         left.active_vertex_indices == right.active_vertex_indices &&
         left.selected_vertex_full_object_ids ==
             right.selected_vertex_full_object_ids;
}

bool same_rectangle_hits(
    const quader_editor_core::RectangleSelectionHits &left,
    const quader_editor_core::RectangleSelectionHits &right) {
  return left.object_ids == right.object_ids &&
         same_rectangle_values(left.faces, right.faces, same_rectangle_face) &&
         same_rectangle_values(left.edges, right.edges, same_rectangle_edge) &&
         same_rectangle_values(left.vertices, right.vertices,
                               same_rectangle_vertex);
}

void append_overlay_frame(
    quader_editor_core::SelectionOverlayFrame &target,
    const quader_editor_core::SelectionOverlayFrame &source) {
  target.triangles.reserve(target.triangles.size() + source.triangles.size());
  target.lines.reserve(target.lines.size() + source.lines.size());
  target.points.reserve(target.points.size() + source.points.size());
  target.triangles.insert(target.triangles.end(), source.triangles.begin(),
                          source.triangles.end());
  target.lines.insert(target.lines.end(), source.lines.begin(),
                      source.lines.end());
  target.points.insert(target.points.end(), source.points.begin(),
                       source.points.end());
  target.hover_target_selected =
      target.hover_target_selected || source.hover_target_selected;
}

bool persistent_object_selection_face_fill_triangle(
    const quader_editor_core::SelectionOverlayTriangle &triangle) {
  return triangle.layer ==
             quader_editor_core::SelectionOverlayLayer::SelectedVisibleFace ||
         triangle.layer ==
             quader_editor_core::SelectionOverlayLayer::SelectedXrayFace ||
         triangle.layer ==
             quader_editor_core::SelectionOverlayLayer::SelectedFaceMask;
}

void remove_persistent_object_face_fill_triangles(
    quader_editor_core::SelectionOverlayFrame &frame) {
  frame.triangles.erase(
      std::remove_if(frame.triangles.begin(), frame.triangles.end(),
                     persistent_object_selection_face_fill_triangle),
      frame.triangles.end());
}

bool edges_match_unordered(quader_poly::Edge left, quader_poly::Edge right) {
  return (left.a == right.a && left.b == right.b) ||
         (left.a == right.b && left.b == right.a);
}

bool hover_frame_suppresses_triangle(
    bool suppress_base_under_hover,
    const quader_editor_core::SelectionOverlayFrame &hover_frame,
    const quader_editor_core::SelectionOverlayTriangle &triangle) {
  if (!suppress_base_under_hover) {
    return false;
  }
  if (triangle.layer !=
          quader_editor_core::SelectionOverlayLayer::SelectedVisibleFace &&
      triangle.layer !=
          quader_editor_core::SelectionOverlayLayer::SelectedXrayFace &&
      triangle.layer !=
          quader_editor_core::SelectionOverlayLayer::SelectedFaceMask) {
    return false;
  }
  return std::any_of(
      hover_frame.triangles.begin(), hover_frame.triangles.end(),
      [&](const quader_editor_core::SelectionOverlayTriangle &hover_triangle) {
        return hover_triangle.layer ==
                   quader_editor_core::SelectionOverlayLayer::HoverFace &&
               hover_triangle.object_id == triangle.object_id &&
               hover_triangle.face_id == triangle.face_id;
      });
}

bool hover_frame_suppresses_line(
    bool suppress_base_under_hover,
    const quader_editor_core::SelectionOverlayFrame &hover_frame,
    const quader_editor_core::SelectionOverlayLine &line) {
  if (!suppress_base_under_hover) {
    return false;
  }
  if (line.layer !=
          quader_editor_core::SelectionOverlayLayer::SelectedFaceEdge &&
      line.layer != quader_editor_core::SelectionOverlayLayer::SelectedEdge) {
    return false;
  }
  return std::any_of(
      hover_frame.lines.begin(), hover_frame.lines.end(),
      [&](const quader_editor_core::SelectionOverlayLine &hover_line) {
        if (hover_line.object_id != line.object_id) {
          return false;
        }
        if (hover_line.layer ==
            quader_editor_core::SelectionOverlayLayer::HoverFaceEdge) {
          return line.face_id == hover_line.face_id &&
                 edges_match_unordered(line.edge, hover_line.edge);
        }
        return hover_line.layer ==
                   quader_editor_core::SelectionOverlayLayer::HoverEdge &&
               edges_match_unordered(line.edge, hover_line.edge);
      });
}

bool hover_frame_suppresses_point(
    bool suppress_base_under_hover,
    const quader_editor_core::SelectionOverlayFrame &hover_frame,
    const quader_editor_core::SelectionOverlayPoint &point) {
  if (!suppress_base_under_hover ||
      point.layer !=
          quader_editor_core::SelectionOverlayLayer::SelectedVertex) {
    return false;
  }
  return std::any_of(
      hover_frame.points.begin(), hover_frame.points.end(),
      [&](const quader_editor_core::SelectionOverlayPoint &hover_point) {
        return hover_point.layer ==
                   quader_editor_core::SelectionOverlayLayer::HoverVertex &&
               hover_point.object_id == point.object_id &&
               hover_point.vertex_id == point.vertex_id;
      });
}

void append_selected_hover_filtered_base_frame(
    quader_editor_core::SelectionOverlayFrame &target,
    const quader_editor_core::SelectionOverlayFrame &base,
    const quader_editor_core::SelectionOverlayFrame &hover_frame,
    bool suppress_base_under_hover) {
  target.triangles.reserve(target.triangles.size() + base.triangles.size());
  target.lines.reserve(target.lines.size() + base.lines.size());
  target.points.reserve(target.points.size() + base.points.size());
  for (const quader_editor_core::SelectionOverlayTriangle &triangle :
       base.triangles) {
    if (!hover_frame_suppresses_triangle(suppress_base_under_hover, hover_frame,
                                         triangle)) {
      target.triangles.push_back(triangle);
    }
  }
  for (const quader_editor_core::SelectionOverlayLine &line : base.lines) {
    if (!hover_frame_suppresses_line(suppress_base_under_hover, hover_frame,
                                     line)) {
      target.lines.push_back(line);
    }
  }
  for (const quader_editor_core::SelectionOverlayPoint &point : base.points) {
    if (!hover_frame_suppresses_point(suppress_base_under_hover, hover_frame,
                                      point)) {
      target.points.push_back(point);
    }
  }
}

const quader_editor_core::PolygonObject *find_polygon_object_by_id(
    const std::vector<quader_editor_core::PolygonObject> &objects,
    int object_id) {
  const auto found = std::find_if(
      objects.begin(), objects.end(),
      [object_id](const quader_editor_core::PolygonObject &object) {
        return object.id == object_id;
      });
  return found == objects.end() ? nullptr : &*found;
}

bool has_complete_source_wire_for_object(
    const quader_editor_core::SelectionOverlayFrame &frame, int object_id) {
  const bool has_wire = std::any_of(
      frame.lines.begin(), frame.lines.end(),
      [object_id](const quader_editor_core::SelectionOverlayLine &line) {
        return line.object_id == object_id &&
               line.layer ==
                   quader_editor_core::SelectionOverlayLayer::SourceWire;
      });
  const bool has_depth_stamp = std::any_of(
      frame.triangles.begin(), frame.triangles.end(),
      [object_id](
          const quader_editor_core::SelectionOverlayTriangle &triangle) {
        return triangle.object_id == object_id &&
               triangle.layer == quader_editor_core::SelectionOverlayLayer::
                                     SourceWireDepthStamp;
      });
  return has_wire && has_depth_stamp;
}

bool component_hover_replaces_fallback_active_source_wire(
    const quader_editor_core::EditorSession &session,
    const quader_editor_core::SelectionHoverState &hover) {
  const quader_editor_core::SelectionMode mode = session.selection_mode();
  if (mode == quader_editor_core::SelectionMode::Object ||
      hover.candidate_object_id <= 0) {
    return false;
  }
  const int active_object_id = session.active_object_id();
  if (active_object_id <= 0 || active_object_id == hover.candidate_object_id) {
    return false;
  }
  const quader_editor_core::PolygonObject *active_object =
      find_polygon_object_by_id(session.objects(), active_object_id);
  return active_object != nullptr && active_object->selected &&
         !active_object->component_edit_target &&
         !quader_editor_core::component_selection_has_any_components(
             active_object->selection);
}

void remove_source_wire_for_object(
    quader_editor_core::SelectionOverlayFrame &frame, int object_id) {
  frame.lines.erase(
      std::remove_if(
          frame.lines.begin(), frame.lines.end(),
          [object_id](const quader_editor_core::SelectionOverlayLine &line) {
            return line.object_id == object_id &&
                   line.layer ==
                       quader_editor_core::SelectionOverlayLayer::SourceWire;
          }),
      frame.lines.end());
  frame.triangles.erase(
      std::remove_if(
          frame.triangles.begin(), frame.triangles.end(),
          [object_id](
              const quader_editor_core::SelectionOverlayTriangle &triangle) {
            return triangle.object_id == object_id &&
                   triangle.layer == quader_editor_core::SelectionOverlayLayer::
                                         SourceWireDepthStamp;
          }),
      frame.triangles.end());
}

void append_viewport_overlay_frame(ViewportOverlayFrame &target,
                                   const ViewportOverlayFrame &source) {
  target.triangles.reserve(target.triangles.size() + source.triangles.size());
  target.lines.reserve(target.lines.size() + source.lines.size());
  target.points.reserve(target.points.size() + source.points.size());
  target.icons.reserve(target.icons.size() + source.icons.size());
  target.triangles.insert(target.triangles.end(), source.triangles.begin(),
                          source.triangles.end());
  target.lines.insert(target.lines.end(), source.lines.begin(),
                      source.lines.end());
  target.points.insert(target.points.end(), source.points.begin(),
                       source.points.end());
  target.icons.insert(target.icons.end(), source.icons.begin(),
                      source.icons.end());
  target.hover_target_selected =
      target.hover_target_selected || source.hover_target_selected;
}

[[nodiscard]] bool same_overlay_document_layout(
    const quader_poly::Document &left, const quader_poly::Document &right) {
  if (left.vertices.size() != right.vertices.size() ||
      left.faces.size() != right.faces.size()) {
    return false;
  }
  for (std::size_t index = 0U; index < left.vertices.size(); ++index) {
    const quader_poly::Vertex &left_vertex = left.vertices[index];
    const quader_poly::Vertex &right_vertex = right.vertices[index];
    if (left_vertex.id != right_vertex.id ||
        left_vertex.position.x != right_vertex.position.x ||
        left_vertex.position.y != right_vertex.position.y ||
        left_vertex.position.z != right_vertex.position.z) {
      return false;
    }
  }
  for (std::size_t index = 0U; index < left.faces.size(); ++index) {
    if (left.faces[index].id != right.faces[index].id ||
        left.faces[index].vertices != right.faces[index].vertices) {
      return false;
    }
  }
  return true;
}

void set_overlay_frame_object_id(
    quader_editor_core::SelectionOverlayFrame &frame, int object_id) {
  for (quader_editor_core::SelectionOverlayTriangle &triangle :
       frame.triangles) {
    triangle.object_id = object_id;
  }
  for (quader_editor_core::SelectionOverlayLine &line : frame.lines) {
    line.object_id = object_id;
  }
  for (quader_editor_core::SelectionOverlayPoint &point : frame.points) {
    point.object_id = object_id;
  }
}

bool mirror_has_mesh_preview(const quader_editor_core::MirrorToolState &mirror) {
  return quader_editor_core::mirror_is_active(mirror) &&
         quader_editor_core::mirror_can_preview(mirror) && mirror.preview_valid;
}

void mix_overlay_signature_hash(std::uint64_t &hash, std::uint64_t value) {
  hash ^= value;
  hash *= 1099511628211ULL;
}

void mix_overlay_signature_hash(std::uint64_t &hash, bool value) {
  mix_overlay_signature_hash(hash, value ? 1ULL : 0ULL);
}

void mix_overlay_signature_hash(std::uint64_t &hash, int value) {
  mix_overlay_signature_hash(
      hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(value)));
}

void mix_overlay_signature_hash(std::uint64_t &hash, double value) {
  std::uint64_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value));
  std::memcpy(&bits, &value, sizeof(value));
  mix_overlay_signature_hash(hash, bits);
}

void mix_overlay_signature_hash(std::uint64_t &hash, quader::QVec3 value) {
  mix_overlay_signature_hash(hash, static_cast<double>(value.x));
  mix_overlay_signature_hash(hash, static_cast<double>(value.y));
  mix_overlay_signature_hash(hash, static_cast<double>(value.z));
}

void mix_knife_target_signature_hash(
    std::uint64_t &hash, const quader_poly::KnifePointTarget &target) {
  mix_overlay_signature_hash(hash, static_cast<int>(target.kind));
  mix_overlay_signature_hash(hash, static_cast<int>(target.vertex_id));
  mix_overlay_signature_hash(hash, static_cast<int>(target.edge.a));
  mix_overlay_signature_hash(hash, static_cast<int>(target.edge.b));
  mix_overlay_signature_hash(hash, static_cast<double>(target.edge_factor));
  mix_overlay_signature_hash(hash, static_cast<int>(target.face_id));
  mix_overlay_signature_hash(hash, target.position);
}

QVec3 from_runtime(quader::QVec3 value) { return {value.x, value.y, value.z}; }

ModelingCutMode modeling_cut_mode_from_runtime(
    quader_editor_core::CutMode mode) {
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

ModelingViewportTool modeling_viewport_tool_from_runtime(
    quader_editor_core::ViewportInteractionTool runtime_tool) {
  const ToolDescriptor *tool = find_viewport_tool_descriptor(
      quader_editor_core::viewport_interaction_tool_id(runtime_tool));
  return tool == nullptr ? ModelingViewportTool::Select
                         : tool->modeling_tool;
}

quader::QVec3 to_runtime_vector(QVec3 value) {
  return {value.x, value.y, value.z};
}

quader_poly::Ray to_runtime_ray(const ModelingRay &ray) {
  return {
      .origin = to_runtime_vector(ray.origin),
      .direction = to_runtime_vector(normalize(ray.direction)),
  };
}

quader_poly::PickOptions
pick_options_from_request(const ModelingPickSelectRequest &request) {
  return quader_editor_core::selection_pick_options_from_radii(
      request.vertex_radius, request.edge_radius, request.select_backfaces);
}

quader_editor_core::SelectionEditOperation
to_runtime_selection_edit(ModelingSelectionEdit edit) {
  switch (edit) {
  case ModelingSelectionEdit::Add:
    return quader_editor_core::SelectionEditOperation::Add;
  case ModelingSelectionEdit::Remove:
    return quader_editor_core::SelectionEditOperation::Remove;
  case ModelingSelectionEdit::Toggle:
    return quader_editor_core::SelectionEditOperation::Toggle;
  case ModelingSelectionEdit::Replace:
    return quader_editor_core::SelectionEditOperation::Replace;
  }
  return quader_editor_core::SelectionEditOperation::Replace;
}

ModelingActiveSelectionKind
modeling_active_selection_kind_from_runtime(
    quader_editor_core::ActiveSelectionKind kind) {
  switch (kind) {
  case quader_editor_core::ActiveSelectionKind::None:
    return ModelingActiveSelectionKind::None;
  case quader_editor_core::ActiveSelectionKind::Object:
    return ModelingActiveSelectionKind::Object;
  case quader_editor_core::ActiveSelectionKind::Vertex:
    return ModelingActiveSelectionKind::Vertex;
  case quader_editor_core::ActiveSelectionKind::Edge:
    return ModelingActiveSelectionKind::Edge;
  case quader_editor_core::ActiveSelectionKind::Face:
    return ModelingActiveSelectionKind::Face;
  }
  return ModelingActiveSelectionKind::None;
}

/**
 * Stores a validated polygon component selection edit before it is committed into runtime selection state.
 */
struct ComponentSelectionEditResult {
  bool ok = true;
  quader_poly::Selection selection;
  std::string_view message;
};

bool polygon_edge_exists(const quader_poly::Document &document,
                         quader_poly::Edge edge) {
  if (edge.a == quader_poly::kInvalidElementId ||
      edge.b == quader_poly::kInvalidElementId || edge.a == edge.b) {
    return false;
  }
  for (const quader_poly::Face &face : document.faces) {
    if (face.vertices.size() < 2U) {
      continue;
    }
    for (std::size_t index = 0; index < face.vertices.size(); ++index) {
      const quader_poly::ElementId a = face.vertices[index];
      const quader_poly::ElementId b =
          face.vertices[(index + 1U) % face.vertices.size()];
      if ((a == edge.a && b == edge.b) || (a == edge.b && b == edge.a)) {
        return true;
      }
    }
  }
  return false;
}

template <typename Element>
void apply_component_selection_value(std::vector<Element> &values,
                                     Element value,
                                     ModelingSelectionEdit edit) {
  const auto found = std::find(values.begin(), values.end(), value);
  const bool selected = found != values.end();
  if (edit == ModelingSelectionEdit::Remove ||
      (edit == ModelingSelectionEdit::Toggle && selected)) {
    if (selected) {
      values.erase(found);
    }
    return;
  }
  if (!selected) {
    values.push_back(value);
  }
}

ComponentSelectionEditResult validated_component_selection_edit(
    const quader_editor_core::PolygonObject &object,
    const ModelingComponentSelectionRequest &request,
    quader_editor_core::SelectionMode mode,
    const quader_poly::Selection *current_selection) {
  ComponentSelectionEditResult result;
  if (current_selection != nullptr) {
    result.selection = *current_selection;
  }
  result.selection.mode = quader_editor_core::component_selection_mode(mode);

  for (const int vertex_id : request.vertices) {
    if (vertex_id <= 0) {
      continue;
    }
    const auto id = static_cast<quader_poly::ElementId>(vertex_id);
    if (quader_poly::find_vertex(object.document, id) == nullptr) {
      return {.ok = false, .message = "Selection vertex was not found."};
    }
    apply_component_selection_value(result.selection.vertices, id,
                                    request.edit);
  }

  for (const std::pair<int, int> &edge_ids : request.edges) {
    if (edge_ids.first <= 0 || edge_ids.second <= 0) {
      continue;
    }
    const quader_poly::Edge edge = quader_poly::make_edge(
        static_cast<quader_poly::ElementId>(edge_ids.first),
        static_cast<quader_poly::ElementId>(edge_ids.second));
    if (!polygon_edge_exists(object.document, edge)) {
      return {.ok = false, .message = "Selection edge was not found."};
    }
    apply_component_selection_value(result.selection.edges, edge,
                                    request.edit);
  }

  for (const int face_id : request.faces) {
    if (face_id <= 0) {
      continue;
    }
    const auto id = static_cast<quader_poly::ElementId>(face_id);
    if (quader_poly::find_face(object.document, id) == nullptr) {
      return {.ok = false, .message = "Selection face was not found."};
    }
    apply_component_selection_value(result.selection.faces, id, request.edit);
  }

  result.selection.has_active = false;
  if (request.active_kind == ModelingActiveSelectionKind::Vertex &&
      request.active_vertex > 0) {
    const auto active =
        static_cast<quader_poly::ElementId>(request.active_vertex);
    if (quader_poly::find_vertex(object.document, active) == nullptr) {
      return {.ok = false, .message = "Active selection vertex was not found."};
    }
    if (std::find(result.selection.vertices.begin(),
                  result.selection.vertices.end(),
                  active) != result.selection.vertices.end()) {
      result.selection.has_active = true;
      result.selection.active_kind = quader_poly::ElementKind::Vertex;
      result.selection.active_vertex = active;
    }
  } else if (request.active_kind == ModelingActiveSelectionKind::Edge &&
             request.active_edge.first > 0 && request.active_edge.second > 0) {
    const quader_poly::Edge active = quader_poly::make_edge(
        static_cast<quader_poly::ElementId>(request.active_edge.first),
        static_cast<quader_poly::ElementId>(request.active_edge.second));
    if (!polygon_edge_exists(object.document, active)) {
      return {.ok = false, .message = "Active selection edge was not found."};
    }
    if (std::find(result.selection.edges.begin(), result.selection.edges.end(),
                  active) != result.selection.edges.end()) {
      result.selection.has_active = true;
      result.selection.active_kind = quader_poly::ElementKind::Edge;
      result.selection.active_edge = active;
    }
  } else if (request.active_kind == ModelingActiveSelectionKind::Face &&
             request.active_face > 0) {
    const auto active =
        static_cast<quader_poly::ElementId>(request.active_face);
    if (quader_poly::find_face(object.document, active) == nullptr) {
      return {.ok = false, .message = "Active selection face was not found."};
    }
    if (std::find(result.selection.faces.begin(), result.selection.faces.end(),
                  active) != result.selection.faces.end()) {
      result.selection.has_active = true;
      result.selection.active_kind = quader_poly::ElementKind::Face;
      result.selection.active_face = active;
    }
  }

  return result;
}

quader_editor_core::PolygonObject *
find_modeling_object(quader_editor_core::EditorSession &session,
                     int modeling_object_id) {
  auto &objects = session.objects();
  const auto it = std::find_if(objects.begin(), objects.end(),
                               [modeling_object_id](const auto &object) {
                                 return object.id == modeling_object_id;
                               });
  return it == objects.end() ? nullptr : &(*it);
}

quader_editor_core::RectangleSelectionMatch
rectangle_selection_match(ModelingBoxSelectionRule rule, QVec2 start,
                          QVec2 end) {
  switch (resolve_box_selection_rule(rule, start, end)) {
  case ModelingBoxSelectionRule::Contain:
    return quader_editor_core::RectangleSelectionMatch::Contain;
  case ModelingBoxSelectionRule::Crossing:
    return quader_editor_core::RectangleSelectionMatch::Crossing;
  case ModelingBoxSelectionRule::DragDirection:
    break;
  }
  return quader_editor_core::RectangleSelectionMatch::Crossing;
}

quader_editor_core::RectangleSelectionState rectangle_selection_state_for(
    std::span<const quader_editor_core::PolygonObject> objects,
    quader_editor_core::SelectionMode mode, int active_object_id) {
  quader_editor_core::RectangleSelectionState selection;
  selection.mode = mode;
  selection.active_object_id = active_object_id;
  for (const quader_editor_core::PolygonObject &object : objects) {
    if (!object.selected) {
      continue;
    }
    if (selection.mode == quader_editor_core::SelectionMode::Object) {
      selection.selected_object_ids.push_back(object.id);
      continue;
    }
    if (selection.mode == quader_editor_core::SelectionMode::Face) {
      for (const quader_poly::ElementId face_id : object.selection.faces) {
        const quader_poly::Face *face =
            quader_poly::find_face(object.document, face_id);
        std::vector<int> vertex_ids;
        if (face != nullptr) {
          vertex_ids.reserve(face->vertices.size());
          for (const quader_poly::ElementId vertex_id : face->vertices) {
            vertex_ids.push_back(static_cast<int>(vertex_id));
          }
        }
        selection.selected_faces.push_back(
            {object.id, static_cast<int>(face_id), std::move(vertex_ids)});
      }
      if (object.id == selection.active_object_id &&
          object.selection.has_active &&
          object.selection.active_kind == quader_poly::ElementKind::Face) {
        selection.active_face_index =
            static_cast<int>(object.selection.active_face);
      }
    } else if (selection.mode == quader_editor_core::SelectionMode::Edge) {
      for (const quader_poly::Edge edge : object.selection.edges) {
        selection.selected_edges.push_back({
            object.id,
            static_cast<int>(edge.a),
            static_cast<int>(edge.b),
        });
      }
      if (object.id == selection.active_object_id &&
          object.selection.has_active &&
          object.selection.active_kind == quader_poly::ElementKind::Edge) {
        selection.active_edge_vertex_indices = {
            static_cast<int>(object.selection.active_edge.a),
            static_cast<int>(object.selection.active_edge.b),
        };
      }
    } else if (selection.mode == quader_editor_core::SelectionMode::Vertex) {
      for (const quader_poly::ElementId vertex_id : object.selection.vertices) {
        selection.selected_vertices.push_back(
            {object.id, static_cast<int>(vertex_id)});
      }
      if (object.id == selection.active_object_id &&
          object.selection.has_active &&
          object.selection.active_kind == quader_poly::ElementKind::Vertex) {
        selection.active_vertex_indices = {
            static_cast<int>(object.selection.active_vertex)};
      }
    }
  }
  return selection;
}

void bake_picking_object_world_transform(
    quader_editor_core::PolygonObject &object,
    const quader_editor_core::PickObjectTransform &transform) {
  for (quader_poly::Vertex &vertex : object.document.edit().vertices) {
    vertex.position = transform_point(transform.world_from_object,
                                      vertex.position);
  }
}

} // namespace

bool ModelingSelectionService::has_component_selection(
    const ModelingSelectionSummary &summary) {
  return summary.selected_vertex_count > 0 || summary.selected_edge_count > 0 ||
         summary.selected_face_count > 0;
}

ModelingSelectionMode ModelingSelectionService::selection_mode(
    const quader_editor_core::EditorSession &session,
    ModelingSelectionMode app_selection_mode) const {
  if (app_selection_mode == ModelingSelectionMode::Group) {
    return ModelingSelectionMode::Group;
  }
  return runtime_selection_mode_to_app(session.selection_mode());
}

ModelingSelectionSummary ModelingSelectionService::selection_summary(
    const quader_editor_core::EditorSession &session,
    const SceneDocument &scene, ModelingSelectionMode app_selection_mode) const {
  if (app_selection_mode == ModelingSelectionMode::Group) {
    return make_scene_selection_summary(scene, ModelingSelectionMode::Group,
                                        session.state().content_revision);
  }

  const quader_editor_core::ActiveSelection active =
      session.active_selection();
  const std::vector<int> selected_objects = session.selected_object_ids();
  std::uint32_t selected_vertex_count = 0;
  std::uint32_t selected_edge_count = 0;
  std::uint32_t selected_face_count = 0;
  for (const quader_editor_core::PolygonObject &object : session.objects()) {
    if (!object.selected) {
      continue;
    }
    selected_vertex_count +=
        static_cast<std::uint32_t>(object.selection.vertices.size());
    selected_edge_count +=
        static_cast<std::uint32_t>(object.selection.edges.size());
    selected_face_count +=
        static_cast<std::uint32_t>(object.selection.faces.size());
  }

  return ModelingSelectionSummary{
      .mode = runtime_selection_mode_to_app(session.selection_mode()),
      .has_selection = session.has_selection(),
      .active_kind = modeling_active_selection_kind_from_runtime(active.kind),
      .active_object_id = active.object_id,
      .active_vertex_id = static_cast<int>(active.vertex_id),
      .active_edge_a = static_cast<int>(active.edge.a),
      .active_edge_b = static_cast<int>(active.edge.b),
      .active_face_id = static_cast<int>(active.face_id),
      .selected_object_count =
          static_cast<std::uint32_t>(selected_objects.size()),
      .selected_vertex_count = selected_vertex_count,
      .selected_edge_count = selected_edge_count,
      .selected_face_count = selected_face_count,
      .content_revision = session.state().content_revision,
      .selection_revision = session.state().selection_revision,
  };
}

void ModelingSelectionService::set_instance_edit_scope_provider(
    std::function<ModelingInstanceEditScope()> provider) {
  instance_edit_scope_provider_ = std::move(provider);
  invalidate_picking_cache();
}

void ModelingSelectionService::set_app_selection_mode_provider(
    std::function<ModelingSelectionMode()> provider) {
  app_selection_mode_provider_ = std::move(provider);
  invalidate_picking_cache();
}

void ModelingSelectionService::set_viewport_visibility_filter(
    std::function<bool(EntityId)> filter,
    std::function<std::uint64_t()> revision_provider,
    std::function<bool()> active_provider) {
  viewport_visibility_filter_ = std::move(filter);
  viewport_visibility_revision_provider_ = std::move(revision_provider);
  viewport_visibility_active_provider_ = std::move(active_provider);
  invalidate_picking_cache();
}

bool ModelingSelectionService::viewport_entity_visible(
    EntityId entity) const {
  return !viewport_visibility_filter_ || viewport_visibility_filter_(entity);
}

ModelingInstanceEditScope
ModelingSelectionService::instance_edit_scope() const {
  return instance_edit_scope_provider_ ? instance_edit_scope_provider_()
                                       : ModelingInstanceEditScope{};
}

ModelingSelectionMode ModelingSelectionService::app_selection_mode() const {
  return app_selection_mode_provider_ ? app_selection_mode_provider_()
                                      : ModelingSelectionMode::Object;
}

std::uint64_t
ModelingSelectionService::viewport_visibility_revision() const {
  return viewport_visibility_revision_provider_
             ? viewport_visibility_revision_provider_()
             : 0u;
}

bool ModelingSelectionService::viewport_visibility_active() const {
  return viewport_visibility_active_provider_ &&
         viewport_visibility_active_provider_();
}

int ModelingSelectionService::instance_participant_id(
    EntityId occurrence,
    const quader_editor_core::EditorSession &session) const {
  const auto runtime_id_in_use = [&](int candidate) {
    return std::any_of(session.objects().begin(), session.objects().end(),
                       [candidate](const auto &object) {
                         return object.id == candidate;
                       });
  };
  if (const auto found =
          instance_participant_id_by_occurrence_.find(occurrence);
      found != instance_participant_id_by_occurrence_.end() &&
      !runtime_id_in_use(found->second)) {
    return found->second;
  } else if (found != instance_participant_id_by_occurrence_.end()) {
    occurrence_by_instance_participant_id_.erase(found->second);
    instance_participant_id_by_occurrence_.erase(found);
  }

  while (next_instance_participant_id_ > 0 &&
         (runtime_id_in_use(next_instance_participant_id_) ||
          occurrence_by_instance_participant_id_.contains(
              next_instance_participant_id_))) {
    --next_instance_participant_id_;
  }
  if (next_instance_participant_id_ <= 0) {
    return -1;
  }
  const int participant_id = next_instance_participant_id_--;
  instance_participant_id_by_occurrence_.insert_or_assign(occurrence,
                                                           participant_id);
  occurrence_by_instance_participant_id_.insert_or_assign(participant_id,
                                                           occurrence);
  return participant_id;
}

std::optional<EntityId>
ModelingSelectionService::scene_target_for_picking_participant(
    int participant_id) const {
  const auto found =
      picking_cache_.scene_target_by_participant.find(participant_id);
  return found == picking_cache_.scene_target_by_participant.end()
             ? std::nullopt
             : std::optional<EntityId>{found->second};
}

void ModelingSelectionService::invalidate_picking_cache() const {
  picking_cache_ = {};
  reset_depth_pick_cycle();
  rectangle_projection_cache_ = {};
  rectangle_universe_cache_ = {};
  rectangle_resolution_cache_ = {};
}

void ModelingSelectionService::reset_depth_pick_cycle() const {
  quader_editor_core::reset_pick_cycle(depth_pick_cycle_);
  depth_pick_context_signature_.clear();
  depth_pick_origin_ = {};
}

std::uint64_t
ModelingSelectionService::transformed_picking_projection_build_count() const {
  return transformed_picking_projection_build_count_;
}

std::uint64_t
ModelingSelectionService::rectangle_picking_projection_build_count() const {
  return rectangle_picking_projection_build_count_;
}

std::uint64_t
ModelingSelectionService::rectangle_selection_universe_build_count() const {
  return rectangle_selection_universe_build_count_;
}

std::uint64_t
ModelingSelectionService::rectangle_selection_resolution_build_count() const {
  return rectangle_selection_resolution_build_count_;
}

std::uint64_t ModelingSelectionService::local_face_geometry_build_count()
    const {
  return local_face_geometry_build_count_;
}

std::uint64_t ModelingSelectionService::world_pick_bounds_build_count() const {
  return world_pick_bounds_build_count_;
}

const std::vector<quader_editor_core::PolygonObject> &
ModelingSelectionService::cached_local_picking_objects(
    const quader_editor_core::EditorSession &session,
    const SceneDocument &scene,
    const ModelingObjectRuntimeMap &object_registry) const {
  const std::uint64_t content_revision = session.state().content_revision;
  const std::uint64_t selection_revision = session.state().selection_revision;
  const std::uint64_t scene_hierarchy_revision =
      scene.hierarchy_presentation_revision();
  const std::uint64_t scene_selection_revision = scene.selection_revision();
  const std::uint64_t scene_transform_revision = scene.transform_revision();
  const std::uint64_t scene_visibility_revision = scene.visibility_revision();
  const std::uint64_t scene_layer_revision = scene.layer_revision();
  const std::uint64_t scene_mesh_binding_revision =
      scene.mesh_binding_revision();
  const std::uint64_t object_map_revision = object_registry.revision();
  const std::uint64_t local_visibility_revision =
      viewport_visibility_revision();
  const ModelingInstanceEditScope edit_scope = instance_edit_scope();
  const ModelingSelectionMode current_app_selection_mode =
      app_selection_mode();
  const ModelingSelectionMode participant_mode =
      current_app_selection_mode == ModelingSelectionMode::Object
          ? ModelingSelectionMode::Object
          : ModelingSelectionMode::Group;
  const bool modeling_content_changed =
      !picking_cache_.objects_valid ||
      picking_cache_.content_revision != content_revision;
  const bool scene_hierarchy_changed =
      !picking_cache_.objects_valid ||
      picking_cache_.scene_hierarchy_revision != scene_hierarchy_revision;
  const bool local_participants_changed =
      modeling_content_changed || scene_hierarchy_changed ||
      picking_cache_.scene_visibility_revision != scene_visibility_revision ||
      picking_cache_.scene_layer_revision != scene_layer_revision ||
      picking_cache_.scene_mesh_binding_revision !=
          scene_mesh_binding_revision ||
      picking_cache_.viewport_visibility_revision !=
          local_visibility_revision ||
      picking_cache_.object_map_revision != object_map_revision ||
      picking_cache_.instance_edit_scope != edit_scope ||
      picking_cache_.app_selection_mode != participant_mode;
  const bool geometry_changed =
      modeling_content_changed || scene_hierarchy_changed ||
      picking_cache_.scene_transform_revision != scene_transform_revision ||
      picking_cache_.scene_visibility_revision != scene_visibility_revision ||
      picking_cache_.scene_layer_revision != scene_layer_revision ||
      picking_cache_.scene_mesh_binding_revision !=
          scene_mesh_binding_revision ||
      picking_cache_.viewport_visibility_revision !=
          local_visibility_revision ||
      picking_cache_.object_map_revision != object_map_revision ||
      picking_cache_.instance_edit_scope != edit_scope ||
      picking_cache_.app_selection_mode != participant_mode;
  if (!geometry_changed &&
      picking_cache_.selection_revision == selection_revision &&
      picking_cache_.scene_selection_revision == scene_selection_revision) {
    return picking_cache_.objects;
  }

  picking_cache_.world_objects_valid = false;

  if (geometry_changed) {
    if (modeling_content_changed) {
      picking_cache_.shared_local_bounds.clear();
    }
    if (local_participants_changed) {
      picking_cache_.local_face_geometry_valid = false;
    }
    ++transformed_picking_projection_build_count_;
    ModelingPickingProjection projection =
        PickingCacheProjection::scene_transformed_objects_for_picking(
            session.objects(), scene, object_registry, edit_scope,
            participant_mode,
            [&](EntityId occurrence) {
              return instance_participant_id(occurrence, session);
            },
            viewport_visibility_filter_);
    picking_cache_.objects = std::move(projection.objects);
    picking_cache_.object_transforms =
        std::move(projection.object_transforms);
    picking_cache_.scene_target_by_participant =
        std::move(projection.scene_target_by_participant);
  } else {
    std::unordered_map<int, const quader_editor_core::PolygonObject *>
        runtime_objects_by_id;
    runtime_objects_by_id.reserve(session.objects().size());
    for (const quader_editor_core::PolygonObject &object : session.objects()) {
      runtime_objects_by_id.emplace(object.id, &object);
    }
    for (quader_editor_core::PolygonObject &cached : picking_cache_.objects) {
      if (const auto participant =
              picking_cache_.scene_target_by_participant.find(cached.id);
          participant !=
          picking_cache_.scene_target_by_participant.end()) {
        cached.selected = scene.selection(participant->second) != nullptr &&
                          scene.selection(participant->second)->selected;
        cached.component_edit_target = false;
        cached.selection = {};
        continue;
      }
      const auto runtime = runtime_objects_by_id.find(cached.id);
      if (runtime == runtime_objects_by_id.end()) {
        continue;
      }
      cached.selected = runtime->second->selected;
      cached.component_edit_target = runtime->second->component_edit_target;
      cached.selection = runtime->second->selection;
    }
  }
  picking_cache_.content_revision = content_revision;
  picking_cache_.selection_revision = selection_revision;
  picking_cache_.scene_hierarchy_revision = scene_hierarchy_revision;
  picking_cache_.scene_selection_revision = scene_selection_revision;
  picking_cache_.scene_transform_revision = scene_transform_revision;
  picking_cache_.scene_visibility_revision = scene_visibility_revision;
  picking_cache_.scene_layer_revision = scene_layer_revision;
  picking_cache_.scene_mesh_binding_revision = scene_mesh_binding_revision;
  picking_cache_.viewport_visibility_revision = local_visibility_revision;
  picking_cache_.object_map_revision = object_map_revision;
  picking_cache_.instance_edit_scope = edit_scope;
  picking_cache_.app_selection_mode = participant_mode;
  picking_cache_.objects_valid = true;
  if (geometry_changed) {
    picking_cache_.bounds_valid = false;
    picking_cache_.face_geometry_valid = false;
  }
  return picking_cache_.objects;
}

const std::vector<quader_editor_core::PolygonObject> &
ModelingSelectionService::cached_scene_transformed_objects_for_picking(
    const quader_editor_core::EditorSession &session,
    const SceneDocument &scene,
    const ModelingObjectRuntimeMap &object_registry) const {
  const std::vector<quader_editor_core::PolygonObject> &local_objects =
      cached_local_picking_objects(session, scene, object_registry);
  if (picking_cache_.world_objects_valid) {
    return picking_cache_.world_objects;
  }
  picking_cache_.world_objects = local_objects;
  const std::size_t participant_count = std::min(
      picking_cache_.world_objects.size(),
      picking_cache_.object_transforms.size());
  for (std::size_t index = 0; index < participant_count; ++index) {
    bake_picking_object_world_transform(
        picking_cache_.world_objects[index],
        picking_cache_.object_transforms[index]);
  }
  picking_cache_.world_objects_valid = true;
  return picking_cache_.world_objects;
}

std::span<const quader_editor_core::PickObjectTransform>
ModelingSelectionService::cached_local_picking_transforms(
    const quader_editor_core::EditorSession &session,
    const SceneDocument &scene,
    const ModelingObjectRuntimeMap &object_map) const {
  (void)cached_local_picking_objects(session, scene, object_map);
  return picking_cache_.object_transforms;
}

std::vector<quader_editor_core::PolygonObject>
ModelingSelectionService::scene_transformed_objects_for_picking(
    const quader_editor_core::EditorSession &session,
    const SceneDocument &scene,
    const ModelingObjectRuntimeMap &object_registry) const {
  return cached_scene_transformed_objects_for_picking(session, scene,
                                                      object_registry);
}

const std::vector<quader_editor_core::PickObjectBounds> &
ModelingSelectionService::cached_pick_object_bounds(
    const quader_editor_core::EditorSession &session,
    const SceneDocument &scene,
    const ModelingObjectRuntimeMap &object_registry) const {
  QDR_PROFILE_FUNCTION();
  const std::vector<quader_editor_core::PolygonObject> &objects =
      cached_local_picking_objects(session, scene, object_registry);
  if (picking_cache_.bounds_valid) {
    return picking_cache_.bounds;
  }

  {
    QDR_PROFILE_SCOPE("Modeling refresh world pick bounds");
    bool layout_matches = picking_cache_.bounds.size() == objects.size() &&
                          picking_cache_.bounds_entries.size() ==
                              objects.size();
    for (std::size_t index = 0; layout_matches && index < objects.size();
         ++index) {
      layout_matches =
          picking_cache_.bounds_entries[index].object_id == objects[index].id;
    }
    picking_cache_.bounds.resize(objects.size());
    picking_cache_.bounds_entries.resize(objects.size());
    for (std::size_t index = 0; index < objects.size(); ++index) {
      const quader_editor_core::PolygonObject &object = objects[index];
      const std::uintptr_t storage = object.document.storage_token();
      auto local = picking_cache_.shared_local_bounds.find(storage);
      if (local == picking_cache_.shared_local_bounds.end()) {
        const std::uint64_t signature =
            PickingCacheProjection::local_face_geometry_signature(object);
        ModelingSharedPickBoundsCacheEntry replacement{
            .geometry_signature = signature,
            .bounds = PickingCacheProjection::build_object_pick_bounds(object),
        };
        replacement.bounds.object_id = -1;
        local = picking_cache_.shared_local_bounds
                    .insert_or_assign(storage, std::move(replacement))
                    .first;
      }

      const quader_editor_core::PickObjectTransform *object_transform =
          nullptr;
      if (index < picking_cache_.object_transforms.size() &&
          picking_cache_.object_transforms[index].object_id == object.id) {
        object_transform = &picking_cache_.object_transforms[index];
      } else {
        const auto matching_transform = std::find_if(
            picking_cache_.object_transforms.begin(),
            picking_cache_.object_transforms.end(),
            [&object](const auto &candidate) {
              return candidate.object_id == object.id;
            });
        if (matching_transform != picking_cache_.object_transforms.end()) {
          object_transform = &*matching_transform;
        }
      }
      ModelingPickBoundsCacheEntry next_entry{
          .object_id = object.id,
          .topology_storage = storage,
          .geometry_signature = local->second.geometry_signature,
          .has_world_transform = object_transform != nullptr,
      };
      if (object_transform != nullptr) {
        next_entry.world_from_object = object_transform->world_from_object;
      }

      bool reused = layout_matches;
      if (reused) {
        const ModelingPickBoundsCacheEntry &previous_entry =
            picking_cache_.bounds_entries[index];
        const bool transform_matches =
            previous_entry.has_world_transform ==
                next_entry.has_world_transform &&
            (!next_entry.has_world_transform ||
             same_matrix(previous_entry.world_from_object,
                         next_entry.world_from_object));
        if (previous_entry.topology_storage == next_entry.topology_storage &&
            previous_entry.geometry_signature ==
                next_entry.geometry_signature &&
            transform_matches) {
          reused = true;
        } else {
          reused = false;
        }
      }

      if (!reused && object_transform != nullptr) {
        picking_cache_.bounds[index] =
            PickingCacheProjection::transform_object_pick_bounds(
                local->second.bounds, object.id,
                object_transform->world_from_object);
        ++world_pick_bounds_build_count_;
      } else if (!reused) {
        quader_editor_core::PickObjectBounds bounds = local->second.bounds;
        bounds.object_id = object.id;
        picking_cache_.bounds[index] = bounds;
        ++world_pick_bounds_build_count_;
      }
      picking_cache_.bounds_entries[index] = std::move(next_entry);
    }
  }
  picking_cache_.bounds_valid = true;
  return picking_cache_.bounds;
}

const std::vector<quader_editor_core::PickObjectFaceGeometry> &
ModelingSelectionService::cached_pick_face_geometry(
    const quader_editor_core::EditorSession &session,
    const SceneDocument &scene, const ModelingObjectRuntimeMap &object_registry,
    quader_poly::PickOptions pick_options) const {
  const std::vector<quader_editor_core::PolygonObject> &objects =
      cached_scene_transformed_objects_for_picking(session, scene,
                                                   object_registry);
  if (picking_cache_.face_geometry_valid) {
    bool signatures_match =
        picking_cache_.face_geometry_entries.size() == objects.size();
    for (std::size_t index = 0; signatures_match && index < objects.size();
         ++index) {
      signatures_match =
          picking_cache_.face_geometry_entries[index].object_id ==
              objects[index].id &&
          picking_cache_.face_geometry_entries[index].geometry_signature ==
              PickingCacheProjection::object_face_geometry_signature(
                  objects[index], pick_options);
    }
    if (signatures_match) {
      return picking_cache_.face_geometry;
    }
  }

  std::vector<ModelingPickFaceGeometryCacheEntry> previous_entries =
      std::move(picking_cache_.face_geometry_entries);
  std::vector<quader_editor_core::PickObjectFaceGeometry> previous_geometry =
      std::move(picking_cache_.face_geometry);
  std::vector<bool> previous_entry_used(previous_entries.size(), false);
  std::vector<ModelingPickFaceGeometryCacheEntry> next_entries;
  std::vector<quader_editor_core::PickObjectFaceGeometry> next_geometry;
  next_entries.reserve(objects.size());
  next_geometry.reserve(objects.size());

  for (std::size_t object_index = 0; object_index < objects.size();
       ++object_index) {
    const quader_editor_core::PolygonObject &object = objects[object_index];
    const std::uint64_t signature =
        PickingCacheProjection::object_face_geometry_signature(object,
                                                               pick_options);
    std::optional<std::size_t> reusable;
    if (object_index < previous_entries.size() &&
        !previous_entry_used[object_index] &&
        previous_entries[object_index].object_id == object.id &&
        previous_entries[object_index].geometry_signature == signature) {
      reusable = object_index;
    } else {
      for (std::size_t previous_index = 0;
           previous_index < previous_entries.size(); ++previous_index) {
        if (!previous_entry_used[previous_index] &&
            previous_entries[previous_index].object_id == object.id &&
            previous_entries[previous_index].geometry_signature == signature) {
          reusable = previous_index;
          break;
        }
      }
    }
    if (reusable.has_value()) {
      previous_entry_used[*reusable] = true;
      next_geometry.push_back(std::move(previous_geometry[*reusable]));
    } else {
      next_geometry.push_back({
          object.id,
          quader_poly::build_face_pick_geometry(object.document, pick_options),
      });
    }
    next_entries.push_back({
        .object_id = object.id,
        .geometry_signature = signature,
    });
  }

  picking_cache_.face_geometry = std::move(next_geometry);
  picking_cache_.face_geometry_entries = std::move(next_entries);
  picking_cache_.face_geometry_valid = true;
  return picking_cache_.face_geometry;
}

const std::vector<quader_editor_core::PickObjectFaceGeometry> &
ModelingSelectionService::cached_local_pick_face_geometry(
    const quader_editor_core::EditorSession &session,
    const SceneDocument &scene, const ModelingObjectRuntimeMap &object_registry,
    quader_poly::PickOptions pick_options) const {
  const std::vector<quader_editor_core::PolygonObject> &objects =
      cached_local_picking_objects(session, scene, object_registry);
  if (picking_cache_.local_face_geometry_valid &&
      picking_cache_.local_face_geometry_entries.size() == objects.size() &&
      picking_cache_.local_face_geometry_has_hidden_material_slot ==
          pick_options.has_hidden_material_slot &&
      picking_cache_.local_face_geometry_hidden_material_slot ==
          pick_options.hidden_material_slot) {
    return picking_cache_.local_face_geometry;
  }

  std::vector<ModelingPickFaceGeometryCacheEntry> next_entries;
  std::vector<quader_editor_core::PickObjectFaceGeometry> next_geometry;
  std::unordered_map<std::uintptr_t, std::uint64_t> signature_by_storage;
  next_entries.reserve(objects.size());
  next_geometry.reserve(objects.size());
  signature_by_storage.reserve(objects.size());
  for (const quader_editor_core::PolygonObject &object : objects) {
    const std::uintptr_t storage = object.document.storage_token();
    const auto [signature_entry, inserted] =
        signature_by_storage.try_emplace(storage, 0u);
    if (inserted) {
      signature_entry->second =
          PickingCacheProjection::local_face_geometry_signature(
              object, pick_options);
    }
    const std::uint64_t signature = signature_entry->second;
    auto shared = std::find_if(
        picking_cache_.shared_local_face_geometry.begin(),
        picking_cache_.shared_local_face_geometry.end(),
        [storage, signature](
            const ModelingSharedPickFaceGeometryCacheEntry &candidate) {
          return candidate.topology_storage == storage &&
                 candidate.geometry_signature == signature;
        });
    if (shared == picking_cache_.shared_local_face_geometry.end()) {
      ModelingSharedPickFaceGeometryCacheEntry built{
          .topology_storage = storage,
          .geometry_signature = signature,
          .triangles = std::make_shared<
              const std::vector<quader_poly::PickFaceTriangle>>(
              quader_poly::build_face_pick_geometry(object.document,
                                                    pick_options)),
      };
      ++local_face_geometry_build_count_;
      picking_cache_.shared_local_face_geometry.push_back(std::move(built));
      shared = std::prev(picking_cache_.shared_local_face_geometry.end());
    }
    next_entries.push_back({
        .object_id = object.id,
        .topology_storage = storage,
        .geometry_signature = signature,
    });
    next_geometry.push_back({
        .object_id = object.id,
        .shared_triangles = shared->triangles,
    });
  }
  picking_cache_.local_face_geometry_entries = std::move(next_entries);
  picking_cache_.local_face_geometry = std::move(next_geometry);
  picking_cache_.local_face_geometry_valid = true;
  picking_cache_.local_face_geometry_has_hidden_material_slot =
      pick_options.has_hidden_material_slot;
  picking_cache_.local_face_geometry_hidden_material_slot =
      pick_options.hidden_material_slot;
  return picking_cache_.local_face_geometry;
}

void ModelingSelectionService::synchronize_depth_pick_cycle(
    const quader_editor_core::EditorSession &session,
    const SceneDocument &scene, const ModelingObjectRuntimeMap &object_map,
    const ModelingPickSelectRequest &request) const {
  const ModelingSelectionMode current_mode =
      selection_mode(session, app_selection_mode());
  if (current_mode != ModelingSelectionMode::Object ||
      request.click_count == 0 || request.alt_pressed) {
    reset_depth_pick_cycle();
    return;
  }
  if (!depth_pick_cycle_.active && !request.depth_cycle_requested(current_mode)) {
    return;
  }
  // Selection revisions intentionally do not participate: the first ordinary
  // click in a subsequent double-click must not restart the depth stack.
  std::string signature;
  signature.reserve(160U);
  const auto append = [&signature](const auto &value) {
    signature.append(reinterpret_cast<const char *>(&value), sizeof(value));
  };
  append(session.state().content_revision);
  append(scene.content_revision());
  append(scene.transform_revision());
  append(scene.visibility_revision());
  append(scene.layer_revision());
  append(scene.mesh_binding_revision());
  append(object_map.revision());
  append(viewport_visibility_revision());
  const ModelingInstanceEditScope scope = instance_edit_scope();
  append(scope.occurrence);
  append(scope.definition);
  append(app_selection_mode());
  append(session.selection_mode());
  append(request.ray.direction.x);
  append(request.ray.direction.y);
  append(request.ray.direction.z);
  append(request.vertex_radius);
  append(request.edge_radius);
  append(request.select_backfaces);
  const double origin_tolerance =
      std::isfinite(request.cycle_origin_tolerance) &&
              request.cycle_origin_tolerance > 0.0
          ? request.cycle_origin_tolerance
          : 0.0;
  append(origin_tolerance);
  const double dx =
      static_cast<double>(request.ray.origin.x) - depth_pick_origin_.x;
  const double dy =
      static_cast<double>(request.ray.origin.y) - depth_pick_origin_.y;
  const double dz =
      static_cast<double>(request.ray.origin.z) - depth_pick_origin_.z;
  const double origin_distance_squared = dx * dx + dy * dy + dz * dz;
  if (signature != depth_pick_context_signature_ ||
      !std::isfinite(origin_distance_squared) ||
      origin_distance_squared > origin_tolerance * origin_tolerance) {
    reset_depth_pick_cycle();
    depth_pick_context_signature_ = std::move(signature);
    // Keep the anchor fixed until this cycle ends, rather than accumulating
    // small movements that eventually walk the cursor to a different stack.
    depth_pick_origin_ = request.ray.origin;
  }
}

ModelingSelectionApplyResult ModelingSelectionService::apply_pick_selection(
    quader_editor_core::EditorSession &session, const SceneDocument &scene,
    const ModelingObjectRuntimeMap &object_registry,
    const ModelingPickSelectRequest &request) const {
  synchronize_depth_pick_cycle(session, scene, object_registry, request);
  const bool cycle_depth = request.depth_cycle_requested(
      selection_mode(session, app_selection_mode()));
  const std::uint64_t selection_before = session.state().selection_revision;
  const std::vector<quader_editor_core::PolygonObject> &cached_objects =
      cached_local_picking_objects(session, scene, object_registry);
  const quader_poly::PickOptions pick_options =
      pick_options_from_request(request);
  const std::vector<quader_editor_core::PickObjectBounds> &pick_bounds =
      cached_pick_object_bounds(session, scene, object_registry);
  const std::vector<quader_editor_core::PickObjectFaceGeometry>
      &pick_face_geometry = cached_local_pick_face_geometry(
          session, scene, object_registry, pick_options);
  const std::span<const quader_editor_core::PickObjectTransform>
      object_transforms = cached_local_picking_transforms(
          session, scene, object_registry);
  const quader_editor_core::ApplyPickSelectionResult result =
      quader_editor_core::apply_pick_selection(
          session,
          std::vector<quader_editor_core::PolygonObject>(cached_objects.begin(),
                                                         cached_objects.end()),
          quader_editor_core::ApplyPickSelectionRequest{
              .operation =
                  quader_editor_core::selection_edit_operation_from_modifiers(
                      request.control_pressed ||
                          (cycle_depth && request.command_pressed),
                      request.shift_pressed),
              .ray = to_runtime_ray(request.ray),
              .pick_options = pick_options,
              .click_count = request.click_count,
              .edge_loop_select = request.alt_pressed,
              .preserve_existing_component_selection_on_hit =
                  request.preserve_existing_component_selection_on_hit,
              .preserve_selection_on_selected_hit =
                  request.preserve_selection_on_selected_hit,
              .preserve_selection_on_miss = request.preserve_selection_on_miss,
              .object_bounds = pick_bounds,
              .face_geometry = pick_face_geometry,
              .object_transforms = object_transforms,
              .cycle_depth = cycle_depth,
              .depth_cycle = &depth_pick_cycle_,
          });
  return {
      .applied = result.applied,
      .changed = session.state().selection_revision != selection_before,
  };
}

std::optional<int> ModelingSelectionService::pick_object_id(
    const quader_editor_core::EditorSession &session,
    const SceneDocument &scene, const ModelingObjectRuntimeMap &object_registry,
    const ModelingPickSelectRequest &request) const {
  synchronize_depth_pick_cycle(session, scene, object_registry, request);
  const std::vector<quader_editor_core::PolygonObject> &cached_objects =
      cached_local_picking_objects(session, scene, object_registry);
  quader_poly::PickOptions pick_options = pick_options_from_request(request);
  pick_options.mode = quader_poly::SelectionMode::Face;
  const std::optional<quader_editor_core::PickHitRecord> hit =
      quader_editor_core::pick_nearest(
          cached_objects,
          quader_editor_core::PickRequest{
              .ray = to_runtime_ray(request.ray),
              .options = pick_options,
          },
          cached_pick_object_bounds(session, scene, object_registry),
          cached_local_pick_face_geometry(session, scene, object_registry,
                                          pick_options),
          cached_local_picking_transforms(session, scene, object_registry));
  if (!hit || hit->object_id <= 0) {
    return std::nullopt;
  }
  return hit->object_id;
}

std::optional<EntityId> ModelingSelectionService::pick_object_entity(
    const quader_editor_core::EditorSession &session,
    const SceneDocument &scene, const ModelingObjectRuntimeMap &object_registry,
    const ModelingPickSelectRequest &request) const {
  const auto entity_for_participant = [&](int object_id) {
    std::optional<EntityId> entity =
        scene_target_for_picking_participant(object_id);
    if (!entity.has_value()) {
      entity = object_registry.entity_for_modeling_object(object_id);
    }
    return entity;
  };
  if (!request.depth_cycle_requested(
          selection_mode(session, app_selection_mode()))) {
    const std::optional<int> object_id =
        pick_object_id(session, scene, object_registry, request);
    return object_id.has_value() ? entity_for_participant(*object_id)
                                 : std::nullopt;
  }

  synchronize_depth_pick_cycle(session, scene, object_registry, request);
  const std::vector<quader_editor_core::PolygonObject> &objects =
      cached_local_picking_objects(session, scene, object_registry);
  quader_poly::PickOptions options = pick_options_from_request(request);
  options.mode = quader_poly::SelectionMode::Face;
  // Include authored boundaries when an open mesh is edge-on to this ray.
  // Ordinary component picking retains its facing and occlusion policy.
  const std::vector<quader_editor_core::PickHitRecord> hits =
      quader_editor_core::pick_mesh_depth_hits(
          objects,
          quader_editor_core::PickRequest{.ray = to_runtime_ray(request.ray),
                                          .options = options},
          cached_pick_object_bounds(session, scene, object_registry),
          cached_local_pick_face_geometry(session, scene, object_registry,
                                          options),
          cached_local_picking_transforms(session, scene, object_registry));
  std::vector<EntityId> candidates;
  std::unordered_set<EntityId> membership;
  const quader_editor_core::SelectionEditOperation operation =
      quader_editor_core::selection_edit_operation_from_modifiers(
          request.control_pressed || request.command_pressed,
          request.shift_pressed);
  std::string signature = "scene:";
  signature += std::to_string(static_cast<int>(app_selection_mode()));
  signature += ":" + std::to_string(static_cast<int>(operation));
  int ordinary_candidate = -1;
  std::optional<EntityId> ordinary_target;
  for (const quader_editor_core::PickHitRecord &hit : hits) {
    const std::optional<EntityId> entity = entity_for_participant(hit.object_id);
    if (!entity.has_value()) {
      continue;
    }
    const EntityId target = *entity;
    if (!ordinary_target.has_value() && target != kInvalidEntityId &&
        hit.kind == quader_poly::ElementKind::Face) {
      ordinary_target = target;
    }
    if (target == kInvalidEntityId || !membership.insert(target).second) {
      continue;
    }
    candidates.push_back(target);
    signature += "|" + std::to_string(target);
  }
  // The first click can remain deferred, so the previous primary is not this
  // gesture's front target. Seed every fresh cycle from the ordinary surface;
  // an existing same-operation cursor still advances normally.
  if (ordinary_target.has_value()) {
    const auto ordinary =
        std::find(candidates.begin(), candidates.end(), *ordinary_target);
    if (ordinary != candidates.end()) {
      ordinary_candidate = static_cast<int>(ordinary - candidates.begin());
    }
  }
  const quader_editor_core::PickCycleStep step =
      quader_editor_core::advance_depth_pick_cycle(
          depth_pick_cycle_, std::move(signature),
          static_cast<int>(candidates.size()), ordinary_candidate);
  return step.active ? std::optional<EntityId>(candidates[step.index])
                     : std::nullopt;
}

std::optional<quader_editor_core::PickHitRecord>
ModelingSelectionService::pick_surface_hit(
    const quader_editor_core::EditorSession &session,
    const SceneDocument &scene, const ModelingObjectRuntimeMap &object_registry,
    const ModelingRay &ray) const {
  const std::vector<quader_editor_core::PolygonObject> &cached_objects =
      cached_local_picking_objects(session, scene, object_registry);
  quader_poly::PickOptions pick_options =
      quader_editor_core::selection_pick_options_from_radii(0.0, 0.0);
  pick_options.mode = quader_poly::SelectionMode::Face;
  return quader_editor_core::pick_nearest(
      cached_objects,
      quader_editor_core::PickRequest{
          .ray = to_runtime_ray(ray),
          .options = pick_options,
      },
      cached_pick_object_bounds(session, scene, object_registry),
      cached_local_pick_face_geometry(session, scene, object_registry,
                                      pick_options),
      cached_local_picking_transforms(session, scene, object_registry));
}

quader_editor_core::SelectionHoverUpdateResult
ModelingSelectionService::selection_hover(
    const quader_editor_core::EditorSession &session,
    const SceneDocument &scene, const ModelingObjectRuntimeMap &object_map,
    const quader_editor_core::SelectionHoverState &current_hover,
  const ModelingHoverRequest &request) const {
  const quader_poly::PickOptions pick_options =
      quader_editor_core::selection_pick_options_from_radii(
          request.vertex_radius, request.edge_radius,
          request.select_backfaces);
  return quader_editor_core::update_selection_hover(
      cached_local_picking_objects(session, scene, object_map),
      session.selection_mode(), current_hover, to_runtime_ray(request.ray),
      request.selected_modifier_pressed, pick_options,
      cached_pick_object_bounds(session, scene, object_map),
      cached_local_pick_face_geometry(session, scene, object_map,
                                      pick_options),
      request.face_plane_hover,
      cached_local_picking_transforms(session, scene, object_map));
}

quader_editor_core::RectangleSelectionState
ModelingSelectionService::rectangle_selection_state(
    const quader_editor_core::EditorSession &session) const {
  return rectangle_selection_state_for(session.objects(),
                                       session.selection_mode(),
                                       session.active_object_id());
}

std::optional<ModelingCommandResult>
ModelingSelectionService::select_all_in_constrained_component_scope(
    quader_editor_core::EditorSession &session, const SceneDocument &scene,
    const ModelingObjectRuntimeMap &object_map, bool invert,
    int sticky_component_object_id) const {
  const bool instance_scope_active = instance_edit_scope().active();
  const bool viewport_scope_active = viewport_visibility_active();
  if (session.selection_mode() == quader_editor_core::SelectionMode::Object ||
      app_selection_mode() == ModelingSelectionMode::Group) {
    return std::nullopt;
  }
  const bool sticky_scope_requested =
      !invert && sticky_component_object_id > 0;
  if (!instance_scope_active && !viewport_scope_active &&
      !sticky_scope_requested) {
    return std::nullopt;
  }

  const std::vector<quader_editor_core::PolygonObject> &picking_objects =
      cached_local_picking_objects(session, scene, object_map);
  const bool sticky_scope_active =
      sticky_scope_requested &&
      std::ranges::any_of(
          picking_objects,
          [sticky_component_object_id](
              const quader_editor_core::PolygonObject &object) {
            return object.id == sticky_component_object_id;
          });
  if (!instance_scope_active && !viewport_scope_active &&
      !sticky_scope_active) {
    return std::nullopt;
  }

  // Local visibility and instance edit deliberately restrict the whole
  // component universe. Ordinary sticky selection only validates its added
  // target through the pickable universe; existing component-selected meshes
  // keep the same full-session scope used by the SDK fallback.
  std::vector<quader_editor_core::PolygonObject> scoped_objects =
      instance_scope_active || viewport_scope_active
          ? picking_objects
          : session.objects();
  int active_object_id = session.active_object_id();
  std::vector<std::size_t> target_indices;
  target_indices.reserve(scoped_objects.size());
  for (std::size_t index = 0; index < scoped_objects.size(); ++index) {
    if (quader_editor_core::component_selection_has_mode_components(
            scoped_objects[index].selection, session.selection_mode()) ||
        (sticky_scope_active &&
         scoped_objects[index].id == sticky_component_object_id)) {
      target_indices.push_back(index);
    }
  }
  if (target_indices.empty()) {
    return ModelingCommandResult{.ok = true,
                                 .changed = false,
                                 .message = "Selection unchanged."};
  }

  for (const std::size_t index : target_indices) {
    quader_editor_core::PolygonObject &object = scoped_objects[index];
    quader_poly::Selection next_selection;
    next_selection.mode = quader_editor_core::component_selection_mode(
        session.selection_mode());
    switch (session.selection_mode()) {
    case quader_editor_core::SelectionMode::Edge:
      for (const quader_poly::Edge edge : cached_document_edges(object)) {
        if (!invert ||
            !quader_poly::selection_contains(object.selection, edge)) {
          next_selection.edges.push_back(edge);
        }
      }
      if (!next_selection.edges.empty()) {
        next_selection.has_active = true;
        next_selection.active_kind = quader_poly::ElementKind::Edge;
        next_selection.active_edge = next_selection.edges.back();
      }
      break;
    case quader_editor_core::SelectionMode::Face:
      for (const quader_poly::Face &face : object.document.faces) {
        if (!invert || !quader_poly::selection_contains_face(object.selection,
                                                              face.id)) {
          next_selection.faces.push_back(face.id);
        }
      }
      if (!next_selection.faces.empty()) {
        next_selection.has_active = true;
        next_selection.active_kind = quader_poly::ElementKind::Face;
        next_selection.active_face = next_selection.faces.back();
      }
      break;
    case quader_editor_core::SelectionMode::Vertex:
    case quader_editor_core::SelectionMode::Object:
      for (const quader_poly::Vertex &vertex : object.document.vertices) {
        if (!invert ||
            !quader_poly::selection_contains(object.selection, vertex.id)) {
          next_selection.vertices.push_back(vertex.id);
        }
      }
      if (!next_selection.vertices.empty()) {
        next_selection.has_active = true;
        next_selection.active_kind = quader_poly::ElementKind::Vertex;
        next_selection.active_vertex = next_selection.vertices.back();
      }
      break;
    }
    object.selection = std::move(next_selection);
    object.selected = !object.selection.empty();
    object.component_edit_target = object.selected;
    if (object.selected) {
      active_object_id = object.id;
    }
  }
  quader_editor_core::normalize_active_object_id(scoped_objects,
                                                  active_object_id);

  const quader_editor_core::RectangleSelectionState scoped_selection =
      rectangle_selection_state_for(scoped_objects, session.selection_mode(),
                                    active_object_id);
  const bool changed = apply_rectangle_selection(session, scoped_selection);
  return ModelingCommandResult{
      .ok = true,
      .changed = changed,
      .message = changed ? (invert ? "Visible selection inverted."
                                   : "Components selected.")
                         : "Selection unchanged.",
  };
}

ModelingSelectionApplyResult
ModelingSelectionService::apply_component_selection_command(
    quader_editor_core::EditorSession &session, const SceneDocument &scene,
    const ModelingObjectRuntimeMap &object_map,
    ModelingComponentSelectionCommand command) const {
  using NativeMode = quader_editor_core::SelectionMode;
  const NativeMode mode = session.selection_mode();
  const bool edge_command =
      command == ModelingComponentSelectionCommand::SelectEdgeDirection ||
      command == ModelingComponentSelectionCommand::SelectEdgeLoops ||
      command == ModelingComponentSelectionCommand::SelectEdgeRing;
  const bool face_command =
      command ==
          ModelingComponentSelectionCommand::SelectFacesWithSameMaterial ||
      command == ModelingComponentSelectionCommand::SelectCoplanarFaces ||
      command == ModelingComponentSelectionCommand::SelectFaceNormal;
  if ((edge_command && mode != NativeMode::Edge) ||
      (face_command && mode != NativeMode::Face) ||
      mode == NativeMode::Object) {
    return {.applied = false, .changed = false};
  }

  const bool comparison_space_command =
      command == ModelingComponentSelectionCommand::SelectEdgeDirection;
  const bool plane_command =
      command == ModelingComponentSelectionCommand::SelectCoplanarFaces;
  const bool normal_command =
      command == ModelingComponentSelectionCommand::SelectFaceNormal;
  std::vector<quader_editor_core::PolygonObject> scoped_objects =
      comparison_space_command
          ? scene_transformed_objects_for_picking(session, scene, object_map)
          : cached_local_picking_objects(session, scene, object_map);
  const std::span<const quader_editor_core::PickObjectTransform>
      object_transforms = (plane_command || normal_command)
                              ? cached_local_picking_transforms(
                                    session, scene, object_map)
                              : std::span<const quader_editor_core::
                                                PickObjectTransform>{};
  const auto comparison_transform_for =
      [&](std::size_t index,
          const quader_editor_core::PolygonObject &object) {
        if (index < object_transforms.size() &&
            object_transforms[index].object_id == object.id) {
          return polygon_transform_from_matrix(
              object_transforms[index].world_from_object);
        }
        const auto found = std::ranges::find(
            object_transforms, object.id,
            &quader_editor_core::PickObjectTransform::object_id);
        return found == object_transforms.end()
                   ? quader_poly::Transform3{}
                   : polygon_transform_from_matrix(found->world_from_object);
      };
  const auto entity_for_object = [&](int object_id) {
    std::optional<EntityId> entity =
        scene_target_for_picking_participant(object_id);
    if (!entity.has_value()) {
      entity = object_map.entity_for_modeling_object(object_id);
    }
    return entity;
  };
  std::vector<std::size_t> target_indices;
  target_indices.reserve(scoped_objects.size());
  for (std::size_t index = 0; index < scoped_objects.size(); ++index) {
    const quader_editor_core::PolygonObject &object = scoped_objects[index];
    const std::optional<EntityId> entity = entity_for_object(object.id);
    const SelectionComponent *scene_selection =
        entity.has_value() ? scene.selection(*entity) : nullptr;
    if ((scene_selection != nullptr && scene_selection->selected) ||
        object.selected || object.component_edit_target ||
        !object.selection.empty()) {
      target_indices.push_back(index);
    }
  }
  if (target_indices.empty()) {
    return {.applied = true, .changed = false};
  }

  quader_poly::SelectionMode portable_mode =
      quader_poly::SelectionMode::Vertex;
  if (mode == NativeMode::Edge) {
    portable_mode = quader_poly::SelectionMode::Edge;
  } else if (mode == NativeMode::Face) {
    portable_mode = quader_poly::SelectionMode::Face;
  }
  for (const std::size_t index : target_indices) {
    quader_poly::Selection &selection = scoped_objects[index].selection;
    if (selection.mode != portable_mode) {
      quader_poly::set_selection_mode(selection, portable_mode);
    }
  }

  const quader_poly::Transform3 identity;
  bool selection_changed = false;
  if (command == ModelingComponentSelectionCommand::SelectEdgeDirection) {
    std::vector<QVec3> seed_directions;
    for (const std::size_t index : target_indices) {
      std::vector<QVec3> directions =
          quader_poly::selected_edge_directions_in_comparison_space(
              scoped_objects[index].document,
              scoped_objects[index].selection, identity);
      seed_directions.insert(seed_directions.end(), directions.begin(),
                             directions.end());
    }
    for (const std::size_t index : target_indices) {
      selection_changed =
          quader_poly::add_edges_matching_directions(
              scoped_objects[index].document,
              scoped_objects[index].selection, identity, seed_directions) ||
          selection_changed;
    }
  } else if (command ==
             ModelingComponentSelectionCommand::SelectCoplanarFaces) {
    std::vector<quader_poly::SelectionComparisonPlane> seed_planes;
    for (const std::size_t index : target_indices) {
      std::vector<quader_poly::SelectionComparisonPlane> planes =
          quader_poly::selected_face_planes_in_comparison_space(
              scoped_objects[index].document,
              scoped_objects[index].selection,
              comparison_transform_for(index, scoped_objects[index]));
      seed_planes.insert(seed_planes.end(), planes.begin(), planes.end());
    }
    for (const std::size_t index : target_indices) {
      selection_changed =
          quader_poly::add_faces_coplanar_with_planes(
              scoped_objects[index].document,
              scoped_objects[index].selection,
              comparison_transform_for(index, scoped_objects[index]),
              seed_planes) ||
          selection_changed;
    }
  } else if (command ==
             ModelingComponentSelectionCommand::SelectFaceNormal) {
    std::vector<QVec3> seed_normals;
    for (const std::size_t index : target_indices) {
      const quader_poly::Transform3 comparison_from_document =
          comparison_transform_for(index, scoped_objects[index]);
      std::vector<QVec3> normals =
          quader_poly::selected_face_normals_in_comparison_space(
              scoped_objects[index].document,
              scoped_objects[index].selection,
              comparison_from_document);
      seed_normals.insert(seed_normals.end(), normals.begin(), normals.end());
    }
    for (const std::size_t index : target_indices) {
      const quader_poly::Transform3 comparison_from_document =
          comparison_transform_for(index, scoped_objects[index]);
      selection_changed =
          quader_poly::add_faces_matching_normals(
              scoped_objects[index].document,
              scoped_objects[index].selection,
              comparison_from_document, seed_normals) ||
          selection_changed;
    }
  } else if (command ==
             ModelingComponentSelectionCommand::SelectFacesWithSameMaterial) {
    std::vector<MaterialReference> seed_materials;
    for (const std::size_t index : target_indices) {
      const quader_editor_core::PolygonObject &object = scoped_objects[index];
      const std::optional<EntityId> entity = entity_for_object(object.id);
      const MaterialComponent *material =
          entity.has_value() ? scene.material(*entity) : nullptr;
      if (material == nullptr) {
        continue;
      }
      for (const quader_poly::ElementId face_id : object.selection.faces) {
        const quader_poly::Face *face =
            quader_poly::find_face(object.document, face_id);
        if (face == nullptr) {
          continue;
        }
        const MaterialReference reference =
            material->material_for_slot(face->material_slot);
        if (reference.valid() &&
            std::find(seed_materials.begin(), seed_materials.end(),
                      reference) == seed_materials.end()) {
          seed_materials.push_back(reference);
        }
      }
    }
    for (const std::size_t index : target_indices) {
      quader_editor_core::PolygonObject &object = scoped_objects[index];
      const std::optional<EntityId> entity = entity_for_object(object.id);
      const MaterialComponent *material =
          entity.has_value() ? scene.material(*entity) : nullptr;
      if (material == nullptr) {
        continue;
      }
      std::vector<std::uint32_t> matching_slots;
      for (const quader_poly::Face &face : object.document.faces) {
        const MaterialReference reference =
            material->material_for_slot(face.material_slot);
        if (reference.valid() &&
            std::find(seed_materials.begin(), seed_materials.end(),
                      reference) != seed_materials.end() &&
            std::find(matching_slots.begin(), matching_slots.end(),
                      face.material_slot) == matching_slots.end()) {
          matching_slots.push_back(face.material_slot);
        }
      }
      selection_changed =
          quader_poly::add_faces_with_material_slots(
              object.document, object.selection, matching_slots) ||
          selection_changed;
    }
  } else {
    quader_poly::ComponentSelectionCommand portable_command =
        quader_poly::ComponentSelectionCommand::Grow;
    switch (command) {
    case ModelingComponentSelectionCommand::SelectEdgeLoops:
      portable_command = quader_poly::ComponentSelectionCommand::SelectLoops;
      break;
    case ModelingComponentSelectionCommand::SelectEdgeRing:
      portable_command =
          quader_poly::ComponentSelectionCommand::SelectEdgeRing;
      break;
    case ModelingComponentSelectionCommand::Grow:
      portable_command = quader_poly::ComponentSelectionCommand::Grow;
      break;
    case ModelingComponentSelectionCommand::Shrink:
      portable_command = quader_poly::ComponentSelectionCommand::Shrink;
      break;
    case ModelingComponentSelectionCommand::SelectEdgeDirection:
    case ModelingComponentSelectionCommand::SelectFacesWithSameMaterial:
    case ModelingComponentSelectionCommand::SelectCoplanarFaces:
    case ModelingComponentSelectionCommand::SelectFaceNormal:
      break;
    }
    for (const std::size_t index : target_indices) {
      selection_changed =
          quader_poly::apply_component_selection_command(
              scoped_objects[index].document,
              scoped_objects[index].selection, portable_command) ||
          selection_changed;
    }
  }

  if (!selection_changed) {
    return {.applied = true, .changed = false};
  }

  std::unordered_map<int, const quader_editor_core::PolygonObject *>
      changed_objects;
  changed_objects.reserve(target_indices.size());
  for (const std::size_t index : target_indices) {
    changed_objects.emplace(scoped_objects[index].id, &scoped_objects[index]);
  }

  bool projection_changed = false;
  for (quader_editor_core::PolygonObject &object : session.state().objects) {
    const auto found = changed_objects.find(object.id);
    if (found == changed_objects.end()) {
      continue;
    }
    const quader_poly::Selection &next_selection = found->second->selection;
    const bool next_selected = !next_selection.empty();
    projection_changed = projection_changed ||
                         object.selected != next_selected ||
                         object.component_edit_target != next_selected ||
                         !selection_matches(object.selection, next_selection);
    object.selection = next_selection;
    object.selected = next_selected;
    object.component_edit_target = next_selected;
  }
  const int active_before = session.state().active_object_id;
  quader_editor_core::normalize_active_object_id(
      session.state().objects, session.state().active_object_id);
  projection_changed = projection_changed ||
                       session.state().active_object_id != active_before;
  return {.applied = true, .changed = projection_changed};
}

const std::vector<quader_poly::Edge> &
ModelingSelectionService::cached_document_edges(
    const quader_editor_core::PolygonObject &object) const {
  const std::shared_ptr<const quader_poly::Document> topology =
      object.document.topology_payload();
  const std::uintptr_t storage =
      reinterpret_cast<std::uintptr_t>(topology.get());
  const auto cached = picking_cache_.shared_local_edges.find(storage);
  if (cached != picking_cache_.shared_local_edges.end() &&
      cached->second.topology.get() == topology.get()) {
    return *cached->second.edges;
  }

  auto edges = std::make_shared<const std::vector<quader_poly::Edge>>(
      quader_poly::document_edges(*topology));
  const auto [entry, inserted] =
      picking_cache_.shared_local_edges.insert_or_assign(
          storage, ModelingSharedPickEdgeGeometryCacheEntry{
                       .topology = topology,
                       .edges = std::move(edges),
                   });
  (void)inserted;
  return *entry->second.edges;
}

const quader_editor_core::RectangleSelectionUniverse &
ModelingSelectionService::rectangle_selection_universe(
    const quader_editor_core::EditorSession &session,
    const SceneDocument &scene,
    const ModelingObjectRuntimeMap &object_map,
    quader_editor_core::SelectionMode selection_mode) const {
  QDR_PROFILE_SCOPE("Box selection rectangle universe");
  const std::uint64_t content_revision = session.state().content_revision;
  const std::uint64_t scene_hierarchy_revision =
      scene.hierarchy_presentation_revision();
  const std::uint64_t scene_visibility_revision = scene.visibility_revision();
  const std::uint64_t scene_layer_revision = scene.layer_revision();
  const std::uint64_t scene_mesh_binding_revision =
      scene.mesh_binding_revision();
  const std::uint64_t object_map_revision = object_map.revision();
  const std::uint64_t local_visibility_revision =
      viewport_visibility_revision();
  const ModelingInstanceEditScope edit_scope = instance_edit_scope();
  if (rectangle_universe_cache_.valid &&
      rectangle_universe_cache_.content_revision == content_revision &&
      rectangle_universe_cache_.scene_hierarchy_revision ==
          scene_hierarchy_revision &&
      rectangle_universe_cache_.scene_visibility_revision ==
          scene_visibility_revision &&
      rectangle_universe_cache_.scene_layer_revision == scene_layer_revision &&
      rectangle_universe_cache_.scene_mesh_binding_revision ==
          scene_mesh_binding_revision &&
      rectangle_universe_cache_.object_map_revision == object_map_revision &&
      rectangle_universe_cache_.viewport_visibility_revision ==
          local_visibility_revision &&
      rectangle_universe_cache_.instance_edit_scope == edit_scope &&
      rectangle_universe_cache_.selection_mode == selection_mode) {
    QDR_PROFILE_SCOPE("Box selection rectangle universe cache hit");
    return rectangle_universe_cache_.universe;
  }

  quader_editor_core::RectangleSelectionUniverse universe;
  {
    QDR_PROFILE_SCOPE("Box selection build rectangle universe cache miss");
    ++rectangle_selection_universe_build_count_;
    const std::vector<quader_editor_core::PolygonObject> &picking_objects =
        cached_local_picking_objects(session, scene, object_map);
    for (const quader_editor_core::PolygonObject &object : picking_objects) {
      universe.object_ids.push_back(object.id);
      if (selection_mode == quader_editor_core::SelectionMode::Face) {
        for (const quader_poly::Face &face : object.document.faces) {
          std::vector<int> vertex_ids;
          vertex_ids.reserve(face.vertices.size());
          for (const quader_poly::ElementId vertex_id : face.vertices) {
            vertex_ids.push_back(static_cast<int>(vertex_id));
          }
          universe.faces.push_back(
              {object.id, static_cast<int>(face.id), std::move(vertex_ids)});
        }
      } else if (selection_mode == quader_editor_core::SelectionMode::Edge) {
        for (const quader_poly::Edge edge : cached_document_edges(object)) {
          universe.edges.push_back(
              {object.id, static_cast<int>(edge.a), static_cast<int>(edge.b)});
        }
      } else if (selection_mode ==
                 quader_editor_core::SelectionMode::Vertex) {
        for (const quader_poly::Vertex &vertex : object.document.vertices) {
          universe.vertices.push_back(
              {object.id, static_cast<int>(vertex.id)});
        }
      }
    }
  }
  rectangle_universe_cache_.valid = true;
  rectangle_universe_cache_.content_revision = content_revision;
  rectangle_universe_cache_.scene_hierarchy_revision =
      scene_hierarchy_revision;
  rectangle_universe_cache_.scene_visibility_revision =
      scene_visibility_revision;
  rectangle_universe_cache_.scene_layer_revision = scene_layer_revision;
  rectangle_universe_cache_.scene_mesh_binding_revision =
      scene_mesh_binding_revision;
  rectangle_universe_cache_.object_map_revision = object_map_revision;
  rectangle_universe_cache_.viewport_visibility_revision =
      local_visibility_revision;
  rectangle_universe_cache_.instance_edit_scope = edit_scope;
  rectangle_universe_cache_.selection_mode = selection_mode;
  rectangle_universe_cache_.universe = std::move(universe);
  return rectangle_universe_cache_.universe;
}

quader_editor_core::RectangleProjectedHitsRequest
ModelingSelectionService::rectangle_projected_hits_request(
    const std::vector<quader_editor_core::PolygonObject> &picking_objects,
    std::span<const quader_editor_core::PickObjectTransform>
        object_transforms,
    quader_editor_core::SelectionMode selection_mode,
    const ModelingBoxSelectRequest &request) const {
  QDR_PROFILE_SCOPE("Box selection project candidates");
  const quader_editor_core::ViewportCameraState camera =
      modeling_runtime_camera(request.camera);
  quader_editor_core::RectangleProjectedHitsRequest hits_request;
  hits_request.mode = selection_mode;
  hits_request.match =
      rectangle_selection_match(request.rule, request.start, request.end);
  hits_request.rect =
      quader_editor_core::normalized_screen_rect(request.start, request.end);
  hits_request.select_backfaces = request.select_backfaces;

  if (selection_mode == quader_editor_core::SelectionMode::Object) {
    hits_request.objects.reserve(picking_objects.size());
  } else if (selection_mode == quader_editor_core::SelectionMode::Face) {
    std::size_t face_count = 0U;
    for (const quader_editor_core::PolygonObject &object : picking_objects) {
      face_count += object.document.faces.size();
    }
    hits_request.faces.reserve(face_count);
  } else if (selection_mode == quader_editor_core::SelectionMode::Edge) {
    std::size_t edge_capacity = 0U;
    for (const quader_editor_core::PolygonObject &object : picking_objects) {
      for (const quader_poly::Face &face : object.document.faces) {
        edge_capacity += face.vertices.size();
      }
    }
    hits_request.edges.reserve(edge_capacity);
  } else {
    std::size_t vertex_count = 0U;
    for (const quader_editor_core::PolygonObject &object : picking_objects) {
      vertex_count += object.document.vertices.size();
    }
    hits_request.vertices.reserve(vertex_count);
  }

  std::vector<QVec2> projected_vertices;
  std::vector<std::uint8_t> projected_vertex_valid;
  std::vector<QVec3> world_vertices;
  std::vector<std::uint8_t> world_vertex_valid;
  for (std::size_t object_index = 0;
       object_index < picking_objects.size(); ++object_index) {
    const quader_editor_core::PolygonObject &object =
        picking_objects[object_index];
    const quader_editor_core::PickObjectTransform *object_transform =
        object_index < object_transforms.size() &&
                object_transforms[object_index].object_id == object.id
            ? &object_transforms[object_index]
            : nullptr;
    quader_poly::PickOptions facing_options;
    facing_options.reverse_face_winding =
        object_transform != nullptr &&
        quader_editor_core::pick_object_transform_reverses_face_winding(
            *object_transform);
    std::unique_ptr<quader_poly::document_internal::DocumentPickFacts>
        pick_facts;
    if (selection_mode != quader_editor_core::SelectionMode::Object) {
      pick_facts = std::make_unique<
          quader_poly::document_internal::DocumentPickFacts>(
          object.document, facing_options);
    }
    std::size_t projected_vertex_capacity =
        static_cast<std::size_t>(object.document.read().next_vertex_id);
    for (const quader_poly::Vertex &vertex : object.document.vertices) {
      projected_vertex_capacity =
          std::max(projected_vertex_capacity,
                   static_cast<std::size_t>(vertex.id) + 1U);
    }
    projected_vertices.resize(projected_vertex_capacity);
    projected_vertex_valid.assign(projected_vertex_capacity, 0U);
    world_vertices.resize(projected_vertex_capacity);
    world_vertex_valid.assign(projected_vertex_capacity, 0U);
    const auto projected_position =
        [&projected_vertices,
         &projected_vertex_valid](quader_poly::ElementId vertex_id)
        -> const QVec2 * {
      const std::size_t index = static_cast<std::size_t>(vertex_id);
      return index < projected_vertex_valid.size() &&
                     projected_vertex_valid[index] != 0U
                 ? &projected_vertices[index]
                 : nullptr;
    };
    const auto cached_world_position =
        [&world_vertices,
         &world_vertex_valid](quader_poly::ElementId vertex_id)
        -> const QVec3 * {
      const std::size_t index = static_cast<std::size_t>(vertex_id);
      return index < world_vertex_valid.size() &&
                     world_vertex_valid[index] != 0U
                 ? &world_vertices[index]
                 : nullptr;
    };
    quader_editor_core::RectangleProjectedObject projected_object{
        .object_id = object.id,
    };
    if (selection_mode == quader_editor_core::SelectionMode::Object) {
      projected_object.screen_points.reserve(object.document.vertices.size());
      projected_object.screen_polygons.reserve(object.document.faces.size());
    }
    for (const quader_poly::Vertex &vertex : object.document.vertices) {
      const QVec3 world_position =
          object_transform == nullptr
              ? vertex.position
              : transform_point(object_transform->world_from_object,
                                vertex.position);
      const std::size_t vertex_index = static_cast<std::size_t>(vertex.id);
      world_vertices[vertex_index] = world_position;
      world_vertex_valid[vertex_index] = 1U;
      QVec2 screen;
      if (!quader_editor_core::project_viewport_point(camera, world_position,
                                                      screen)) {
        continue;
      }
      projected_vertices[vertex_index] = screen;
      projected_vertex_valid[vertex_index] = 1U;
      if (selection_mode == quader_editor_core::SelectionMode::Object) {
        include_projected_object_point(projected_object, screen);
      }
    }

    if (selection_mode == quader_editor_core::SelectionMode::Object &&
        projected_object.has_screen_bounds) {
      projected_object.has_screen_center = true;
      projected_object.screen_center = {
          (projected_object.screen_min.x + projected_object.screen_max.x) *
              0.5f,
          (projected_object.screen_min.y + projected_object.screen_max.y) *
              0.5f,
      };
    }

    if (selection_mode == quader_editor_core::SelectionMode::Object) {
      for (const quader_poly::Face &face : object.document.faces) {
        std::vector<QVec2> screen_vertices;
        screen_vertices.reserve(face.vertices.size());
        bool all_projected = !face.vertices.empty();
        for (const quader_poly::ElementId vertex_id : face.vertices) {
          const QVec2 *found = projected_position(vertex_id);
          if (found == nullptr) {
            all_projected = false;
            break;
          }
          screen_vertices.push_back(*found);
        }
        if (all_projected) {
          projected_object.screen_polygons.push_back(
              std::move(screen_vertices));
        } else if (face.vertices.size() >= 2U) {
          for (std::size_t vertex_index = 0;
               vertex_index < face.vertices.size(); ++vertex_index) {
            const QVec2 *a = projected_position(face.vertices[vertex_index]);
            const QVec2 *b = projected_position(
                face.vertices[(vertex_index + 1U) % face.vertices.size()]);
            if (a != nullptr && b != nullptr) {
              projected_object.screen_segments.push_back({*a, *b});
            }
          }
        }
      }
      hits_request.objects.push_back(std::move(projected_object));
      continue;
    }

    if (selection_mode == quader_editor_core::SelectionMode::Face) {
      for (const quader_poly::Face &face : object.document.faces) {
        quader_editor_core::RectangleProjectedFace projected_face;
        projected_face.object_id = object.id;
        projected_face.face_index = static_cast<int>(face.id);
        projected_face.vertex_indices.reserve(face.vertices.size());
        projected_face.screen_vertices.reserve(face.vertices.size());
        QVec2 center{};
        QVec3 world_center{};
        std::size_t world_center_count = 0U;
        bool all_projected = !face.vertices.empty();
        for (const quader_poly::ElementId vertex_id : face.vertices) {
          projected_face.vertex_indices.push_back(
              static_cast<int>(vertex_id));
          const QVec2 *found = projected_position(vertex_id);
          if (const QVec3 *world = cached_world_position(vertex_id)) {
            world_center = world_center + *world;
            ++world_center_count;
          }
          if (found == nullptr) {
            all_projected = false;
            continue;
          }
          center = center + *found;
          projected_face.screen_vertices.push_back(*found);
          if (!projected_face.has_screen_bounds) {
            projected_face.has_screen_bounds = true;
            projected_face.screen_min = *found;
            projected_face.screen_max = *found;
          } else {
            projected_face.screen_min.x =
                std::min(projected_face.screen_min.x, found->x);
            projected_face.screen_min.y =
                std::min(projected_face.screen_min.y, found->y);
            projected_face.screen_max.x =
                std::max(projected_face.screen_max.x, found->x);
            projected_face.screen_max.y =
                std::max(projected_face.screen_max.y, found->y);
          }
        }
        if (all_projected && !projected_face.screen_vertices.empty()) {
          projected_face.has_screen_position = true;
          projected_face.screen_position = center / static_cast<float>(
                                                      projected_face
                                                          .screen_vertices
                                                          .size());
        } else {
          projected_face.screen_vertices.clear();
          projected_face.has_screen_bounds = false;
        }
        if (world_center_count > 0U) {
          QVec2 facing_screen;
          world_center = world_center /
                         static_cast<float>(world_center_count);
          if (quader_editor_core::project_viewport_point(
                  camera, world_center, facing_screen)) {
            projected_face.front_facing =
                pick_facts->face_is_front_facing_pickable(
                    face.id,
                    rectangle_local_pick_ray(camera, facing_screen,
                                             object_transform));
          } else {
            projected_face.front_facing = false;
          }
        } else {
          projected_face.front_facing = false;
        }
        hits_request.faces.push_back(std::move(projected_face));
      }
      continue;
    }

    if (selection_mode == quader_editor_core::SelectionMode::Edge) {
      for (const quader_poly::Edge edge : cached_document_edges(object)) {
        quader_editor_core::RectangleProjectedEdge projected_edge;
        projected_edge.object_id = object.id;
        projected_edge.a_index = static_cast<int>(edge.a);
        projected_edge.b_index = static_cast<int>(edge.b);
        const QVec2 *a = projected_position(edge.a);
        const QVec2 *b = projected_position(edge.b);
        if (a != nullptr && b != nullptr) {
          projected_edge.has_screen_start = true;
          projected_edge.screen_start = *a;
          projected_edge.has_screen_end = true;
          projected_edge.screen_end = *b;
          projected_edge.has_screen_position = true;
          projected_edge.screen_position = (*a + *b) * 0.5f;
        }
        const QVec3 *world_a = cached_world_position(edge.a);
        const QVec3 *world_b = cached_world_position(edge.b);
        if (world_a != nullptr && world_b != nullptr) {
          QVec2 facing_screen;
          if (quader_editor_core::project_viewport_point(
                  camera, (*world_a + *world_b) * 0.5F, facing_screen)) {
            projected_edge.front_facing =
                !pick_facts->edge_has_incident_face(edge) ||
                pick_facts->edge_has_front_facing_pickable_face(
                    edge,
                    rectangle_local_pick_ray(camera, facing_screen,
                                             object_transform));
          } else {
            projected_edge.front_facing = false;
          }
        } else {
          projected_edge.front_facing = false;
        }
        hits_request.edges.push_back(projected_edge);
      }
      continue;
    }

    for (const quader_poly::Vertex &vertex : object.document.vertices) {
      quader_editor_core::RectangleProjectedVertex projected_vertex;
      projected_vertex.object_id = object.id;
      projected_vertex.vertex_index = static_cast<int>(vertex.id);
      const QVec2 *found = projected_position(vertex.id);
      if (found != nullptr) {
        projected_vertex.has_screen_position = true;
        projected_vertex.screen_position = *found;
        projected_vertex.front_facing =
            !pick_facts->vertex_has_incident_face(vertex.id) ||
            pick_facts->vertex_has_front_facing_pickable_face(
                vertex.id,
                rectangle_local_pick_ray(camera, *found, object_transform));
      } else {
        projected_vertex.front_facing = false;
      }
      hits_request.vertices.push_back(projected_vertex);
    }
  }
  return hits_request;
}

const quader_editor_core::RectangleProjectedHitsRequest &
ModelingSelectionService::cached_rectangle_projected_hits_request(
    const quader_editor_core::EditorSession &session,
    const SceneDocument &scene, const ModelingObjectRuntimeMap &object_map,
    quader_editor_core::SelectionMode selection_mode,
    const ModelingBoxSelectRequest &request) const {
  QDR_PROFILE_SCOPE("Box selection projected candidates cache");
  const std::uint64_t content_revision = session.state().content_revision;
  const std::uint64_t scene_hierarchy_revision =
      scene.hierarchy_presentation_revision();
  const std::uint64_t scene_transform_revision = scene.transform_revision();
  const std::uint64_t scene_visibility_revision = scene.visibility_revision();
  const std::uint64_t scene_layer_revision = scene.layer_revision();
  const std::uint64_t scene_mesh_binding_revision =
      scene.mesh_binding_revision();
  const std::uint64_t object_map_revision = object_map.revision();
  const std::uint64_t local_visibility_revision =
      viewport_visibility_revision();
  const ModelingInstanceEditScope edit_scope = instance_edit_scope();
  const ModelingSelectionMode current_app_selection_mode =
      app_selection_mode();
  const ModelingSelectionMode participant_mode =
      current_app_selection_mode == ModelingSelectionMode::Object
          ? ModelingSelectionMode::Object
          : ModelingSelectionMode::Group;
  const bool rebuild =
      !rectangle_projection_cache_.valid ||
      rectangle_projection_cache_.content_revision != content_revision ||
      rectangle_projection_cache_.scene_hierarchy_revision !=
          scene_hierarchy_revision ||
      rectangle_projection_cache_.scene_transform_revision !=
          scene_transform_revision ||
      rectangle_projection_cache_.scene_visibility_revision !=
          scene_visibility_revision ||
      rectangle_projection_cache_.scene_layer_revision !=
          scene_layer_revision ||
      rectangle_projection_cache_.scene_mesh_binding_revision !=
          scene_mesh_binding_revision ||
      rectangle_projection_cache_.object_map_revision != object_map_revision ||
      rectangle_projection_cache_.viewport_visibility_revision !=
          local_visibility_revision ||
      rectangle_projection_cache_.instance_edit_scope != edit_scope ||
      rectangle_projection_cache_.app_selection_mode !=
          participant_mode ||
      rectangle_projection_cache_.selection_mode != selection_mode ||
      !modeling_viewport_camera_matches(rectangle_projection_cache_.camera,
                            request.camera);
  if (rebuild) {
    QDR_PROFILE_SCOPE("Box selection build projected candidates cache miss");
    const std::vector<quader_editor_core::PolygonObject> &picking_objects =
        cached_local_picking_objects(session, scene, object_map);
    rectangle_projection_cache_.request = rectangle_projected_hits_request(
        picking_objects,
        cached_local_picking_transforms(session, scene, object_map),
        selection_mode, request);
    rectangle_projection_cache_.valid = true;
    rectangle_projection_cache_.content_revision = content_revision;
    rectangle_projection_cache_.scene_hierarchy_revision =
        scene_hierarchy_revision;
    rectangle_projection_cache_.scene_transform_revision =
        scene_transform_revision;
    rectangle_projection_cache_.scene_visibility_revision =
        scene_visibility_revision;
    rectangle_projection_cache_.scene_layer_revision = scene_layer_revision;
    rectangle_projection_cache_.scene_mesh_binding_revision =
        scene_mesh_binding_revision;
    rectangle_projection_cache_.object_map_revision = object_map_revision;
    rectangle_projection_cache_.viewport_visibility_revision =
        local_visibility_revision;
    rectangle_projection_cache_.instance_edit_scope = edit_scope;
    rectangle_projection_cache_.app_selection_mode =
        participant_mode;
    rectangle_projection_cache_.selection_mode = selection_mode;
    rectangle_projection_cache_.camera = request.camera;
    ++rectangle_picking_projection_build_count_;
  } else {
    QDR_PROFILE_SCOPE("Box selection projected candidates cache hit");
  }
  rectangle_projection_cache_.request.match =
      rectangle_selection_match(request.rule, request.start, request.end);
  rectangle_projection_cache_.request.rect =
      quader_editor_core::normalized_screen_rect(request.start, request.end);
  rectangle_projection_cache_.request.select_backfaces =
      request.select_backfaces;
  return rectangle_projection_cache_.request;
}

std::vector<int> ModelingSelectionService::box_pick_object_ids(
    const quader_editor_core::EditorSession &session,
    const SceneDocument &scene, const ModelingObjectRuntimeMap &object_registry,
    const ModelingBoxSelectRequest &request) const {
  QDR_PROFILE_SCOPE("Box selection resolve object hits");
  const quader_editor_core::RectangleSelectionHits hits =
      quader_editor_core::rectangle_hits_from_projected_candidates(
          cached_rectangle_projected_hits_request(
              session, scene, object_registry,
              quader_editor_core::SelectionMode::Object, request));
  return hits.object_ids;
}

bool ModelingSelectionService::apply_rectangle_selection(
    quader_editor_core::EditorSession &session,
    const quader_editor_core::RectangleSelectionState &selection) const {
  QDR_PROFILE_SCOPE("Box selection publish runtime selection");
  quader_editor_core::SessionState &state = session.state();
  bool changed = state.selection_mode != selection.mode ||
                 state.active_object_id != selection.active_object_id;

  std::unordered_set<int> selected_objects(
      selection.selected_object_ids.begin(),
      selection.selected_object_ids.end());
  std::unordered_map<int, std::vector<int>> selected_faces_by_object;
  std::unordered_map<int, std::vector<quader_poly::Edge>>
      selected_edges_by_object;
  std::unordered_map<int, std::vector<int>> selected_vertices_by_object;
  if (selection.mode == quader_editor_core::SelectionMode::Face) {
    for (const quader_editor_core::RectangleSelectionFace face :
         selection.selected_faces) {
      selected_faces_by_object[face.object_id].push_back(face.face_index);
    }
  } else if (selection.mode == quader_editor_core::SelectionMode::Edge) {
    std::unordered_set<int> full_object_ids(
        selection.selected_edge_full_object_ids.begin(),
        selection.selected_edge_full_object_ids.end());
    for (const quader_editor_core::PolygonObject &object : state.objects) {
      if (full_object_ids.find(object.id) == full_object_ids.end()) {
        continue;
      }
      selected_edges_by_object[object.id] = cached_document_edges(object);
    }
    for (const quader_editor_core::RectangleSelectionEdge edge :
         selection.selected_edges) {
      selected_edges_by_object[edge.object_id].push_back(quader_poly::make_edge(
          static_cast<quader_poly::ElementId>(edge.a_index),
          static_cast<quader_poly::ElementId>(edge.b_index)));
    }
  } else if (selection.mode == quader_editor_core::SelectionMode::Vertex) {
    std::unordered_set<int> full_object_ids(
        selection.selected_vertex_full_object_ids.begin(),
        selection.selected_vertex_full_object_ids.end());
    for (const quader_editor_core::PolygonObject &object : state.objects) {
      if (full_object_ids.find(object.id) == full_object_ids.end()) {
        continue;
      }
      std::vector<int> &vertices = selected_vertices_by_object[object.id];
      vertices.reserve(object.document.vertices.size());
      for (const quader_poly::Vertex &vertex : object.document.vertices) {
        vertices.push_back(static_cast<int>(vertex.id));
      }
    }
    for (const quader_editor_core::RectangleSelectionVertex vertex :
         selection.selected_vertices) {
      selected_vertices_by_object[vertex.object_id].push_back(
          vertex.vertex_index);
    }
  }

  for (quader_editor_core::PolygonObject &object : state.objects) {
    bool next_selected = false;
    quader_poly::Selection next_selection;
    if (selection.mode == quader_editor_core::SelectionMode::Object) {
      next_selected =
          selected_objects.find(object.id) != selected_objects.end();
    } else {
      next_selection.mode =
          quader_editor_core::component_selection_mode(selection.mode);
      if (selection.mode == quader_editor_core::SelectionMode::Face) {
        const auto found = selected_faces_by_object.find(object.id);
        if (found != selected_faces_by_object.end()) {
          for (const int face_id : found->second) {
            next_selection.faces.push_back(
                static_cast<quader_poly::ElementId>(face_id));
          }
        }
        if (object.id == selection.active_object_id &&
            selection.active_face_index >= 0 &&
            std::find(next_selection.faces.begin(), next_selection.faces.end(),
                      static_cast<quader_poly::ElementId>(
                          selection.active_face_index)) !=
                next_selection.faces.end()) {
          next_selection.has_active = true;
          next_selection.active_kind = quader_poly::ElementKind::Face;
          next_selection.active_face =
              static_cast<quader_poly::ElementId>(selection.active_face_index);
        }
      } else if (selection.mode == quader_editor_core::SelectionMode::Edge) {
        const auto found = selected_edges_by_object.find(object.id);
        if (found != selected_edges_by_object.end()) {
          next_selection.edges = found->second;
        }
        if (object.id == selection.active_object_id &&
            !next_selection.edges.empty()) {
          quader_poly::Edge active = next_selection.edges.back();
          if (selection.active_edge_vertex_indices.size() >= 2U &&
              selection.active_edge_vertex_indices[0] >= 0 &&
              selection.active_edge_vertex_indices[1] >= 0) {
            const quader_poly::Edge requested_active = quader_poly::make_edge(
                static_cast<quader_poly::ElementId>(
                    selection.active_edge_vertex_indices[0]),
                static_cast<quader_poly::ElementId>(
                    selection.active_edge_vertex_indices[1]));
            if (std::find(next_selection.edges.begin(),
                          next_selection.edges.end(), requested_active) !=
                next_selection.edges.end()) {
              active = requested_active;
            }
          }
          next_selection.has_active = true;
          next_selection.active_kind = quader_poly::ElementKind::Edge;
          next_selection.active_edge = active;
        }
      } else if (selection.mode == quader_editor_core::SelectionMode::Vertex) {
        const auto found = selected_vertices_by_object.find(object.id);
        if (found != selected_vertices_by_object.end()) {
          for (const int vertex_id : found->second) {
            next_selection.vertices.push_back(
                static_cast<quader_poly::ElementId>(vertex_id));
          }
        }
        if (object.id == selection.active_object_id &&
            !next_selection.vertices.empty()) {
          quader_poly::ElementId active = next_selection.vertices.back();
          if (!selection.active_vertex_indices.empty() &&
              selection.active_vertex_indices.front() >= 0) {
            const quader_poly::ElementId requested_active =
                static_cast<quader_poly::ElementId>(
                    selection.active_vertex_indices.front());
            if (std::find(next_selection.vertices.begin(),
                          next_selection.vertices.end(), requested_active) !=
                next_selection.vertices.end()) {
              active = requested_active;
            }
          }
          next_selection.has_active = true;
          next_selection.active_kind = quader_poly::ElementKind::Vertex;
          next_selection.active_vertex = active;
        }
      }
      next_selected = !next_selection.empty();
    }

    const bool next_component_edit_target =
        selection.mode != quader_editor_core::SelectionMode::Object &&
        next_selected;
    changed = changed || object.selected != next_selected ||
              object.component_edit_target != next_component_edit_target ||
              !selection_matches(object.selection, next_selection);
    object.selected = next_selected;
    object.component_edit_target = next_component_edit_target;
    object.selection = std::move(next_selection);
  }

  if (!changed) {
    return false;
  }
  state.selection_mode = selection.mode;
  state.active_object_id = selection.active_object_id;
  quader_editor_core::normalize_active_object_id(state.objects,
                                                 state.active_object_id);
  state.selection_revision =
      quader_editor_core::advance_session_revision(state.selection_revision);
  return true;
}

ModelingSelectionApplyResult ModelingSelectionService::apply_rectangle_selection(
    quader_editor_core::EditorSession &session, const SceneDocument &scene,
    const ModelingObjectRuntimeMap &object_registry,
    const ModelingBoxSelectRequest &request,
    const quader_editor_core::RectangleSelectionState &baseline) const {
  QDR_PROFILE_SCOPE("Box selection resolve component selection");
  quader_editor_core::RectangleSelectionRequest rectangle_request;
  rectangle_request.current_selection = baseline;
  const quader_editor_core::SelectionEditOperation operation =
      to_runtime_selection_edit(request.edit);
  rectangle_request.operation = operation;
  rectangle_request.universe_view =
      &rectangle_selection_universe(session, scene, object_registry,
                                    session.selection_mode());
  rectangle_request.hits =
      quader_editor_core::rectangle_hits_from_projected_candidates(
          cached_rectangle_projected_hits_request(
              session, scene, object_registry, session.selection_mode(),
              request));

  if (rectangle_resolution_cache_.valid &&
      rectangle_resolution_cache_.projected_candidates_revision ==
          rectangle_picking_projection_build_count_ &&
      rectangle_resolution_cache_.content_revision ==
          session.state().content_revision &&
      rectangle_resolution_cache_.operation == operation &&
      same_rectangle_selection_state(rectangle_resolution_cache_.baseline,
                                     baseline) &&
      same_rectangle_hits(rectangle_resolution_cache_.hits,
                          rectangle_request.hits)) {
    QDR_PROFILE_SCOPE("Box selection resolution cache hit");
    return {};
  }

  ++rectangle_selection_resolution_build_count_;
  const quader_editor_core::RectangleSelectionResult result = [&]() {
    QDR_PROFILE_SCOPE("Box selection execute selection policy");
    return quader_editor_core::apply_rectangle_selection(rectangle_request);
  }();
  const std::uint64_t selection_before = session.state().selection_revision;
  const bool applied = [&]() {
    QDR_PROFILE_SCOPE("Box selection apply resolved state");
    return apply_rectangle_selection(session, result.selection);
  }();
  rectangle_resolution_cache_.valid = true;
  rectangle_resolution_cache_.projected_candidates_revision =
      rectangle_picking_projection_build_count_;
  rectangle_resolution_cache_.content_revision =
      session.state().content_revision;
  rectangle_resolution_cache_.operation = operation;
  rectangle_resolution_cache_.baseline = baseline;
  rectangle_resolution_cache_.hits = rectangle_request.hits;
  return {
      .applied = applied,
      .changed = session.state().selection_revision != selection_before,
  };
}

ModelingCommandResult ModelingSelectionService::set_group_selection_mode(
    quader_editor_core::EditorSession &session,
    ModelingSelectionMode &app_selection_mode) const {
  const quader_editor_core::SelectionMode runtime_mode =
      app_selection_mode_to_runtime(ModelingSelectionMode::Group);
  const std::vector<int> selected_object_ids = session.selected_object_ids();
  const bool had_runtime_object_selection =
      !selected_object_ids.empty() || session.active_object_id() > 0;
  const bool changed = app_selection_mode != ModelingSelectionMode::Group ||
                       session.selection_mode() != runtime_mode ||
                       had_runtime_object_selection;
  app_selection_mode = ModelingSelectionMode::Group;
  if (session.selection_mode() != runtime_mode) {
    session.set_selection_mode(runtime_mode);
  }
  if (had_runtime_object_selection) {
    session.set_selected_object_ids({}, -1);
  }
  return {
      .ok = true,
      .changed = changed,
      .message = changed ? "Selection mode set to Group."
                         : "Selection mode already Group.",
  };
}

ModelingCommandResult ModelingSelectionService::set_component_selection(
    quader_editor_core::EditorSession &session,
    const ModelingComponentSelectionRequest &request) const {
  quader_editor_core::PolygonObject *object =
      find_modeling_object(session, request.modeling_object_id);
  if (object == nullptr) {
    return {.ok = false, .message = "Selection target mesh was not found."};
  }

  const quader_editor_core::SelectionMode mode =
      app_selection_mode_to_runtime(request.mode);
  const quader_poly::Selection *current_selection = nullptr;
  if (request.edit != ModelingSelectionEdit::Replace &&
      session.selection_mode() == mode && object->selected) {
    current_selection = &object->selection;
  }
  ComponentSelectionEditResult selection_edit =
      validated_component_selection_edit(*object, request, mode,
                                         current_selection);
  if (!selection_edit.ok) {
    return {.ok = false, .message = std::string(selection_edit.message)};
  }

  const std::uint64_t selection_before = session.state().selection_revision;
  session.set_component_selection(request.modeling_object_id,
                                  std::move(selection_edit.selection), mode);
  return {
      .ok = true,
      .changed = session.state().selection_revision != selection_before,
  };
}

bool ModelingSelectionService::selection_matches(
    const quader_poly::Selection &left, const quader_poly::Selection &right) {
  return quader_editor_core::polygon_selection_matches(left, right);
}

SceneModelingSyncService::SceneModelingSyncService(
    ModelingObjectRuntimeMap &object_map)
    : object_map_(object_map) {}

ModelingObjectRuntimeMap &SceneModelingSyncService::object_map() {
  return object_map_;
}

const ModelingObjectRuntimeMap &SceneModelingSyncService::object_map() const {
  return object_map_;
}

bool SceneModelingSyncService::scene_objects_sync_valid() const {
  return scene_objects_sync_valid_;
}

std::uint64_t
SceneModelingSyncService::synced_modeling_content_revision() const {
  return synced_modeling_content_revision_;
}

void SceneModelingSyncService::invalidate_scene_objects() {
  scene_objects_sync_valid_ = false;
  projected_mesh_signatures_by_object_.clear();
  projected_topology_storage_by_object_.clear();
}

void SceneModelingSyncService::mark_scene_objects_synced(
    std::uint64_t content_revision) {
  synced_modeling_content_revision_ = content_revision;
  scene_objects_sync_valid_ = true;
}

void SceneModelingSyncService::remember_projected_mesh_signature(
    int modeling_object_id, std::uint64_t signature,
    std::uintptr_t topology_storage) {
  projected_mesh_signatures_by_object_[modeling_object_id] = signature;
  projected_topology_storage_by_object_[modeling_object_id] =
      topology_storage;
}

void SceneModelingSyncService::forget_projected_mesh_signature(
    int modeling_object_id) {
  projected_mesh_signatures_by_object_.erase(modeling_object_id);
  projected_topology_storage_by_object_.erase(modeling_object_id);
}

bool SceneModelingSyncService::projected_mesh_signature_known(
    int modeling_object_id) const {
  return projected_mesh_signatures_by_object_.contains(modeling_object_id);
}

bool SceneModelingSyncService::projected_mesh_signature_matches(
    int modeling_object_id, std::uint64_t signature) const {
  const auto found =
      projected_mesh_signatures_by_object_.find(modeling_object_id);
  return found != projected_mesh_signatures_by_object_.end() &&
         found->second == signature;
}

bool SceneModelingSyncService::projected_topology_storage_matches(
    int modeling_object_id, std::uintptr_t topology_storage) const {
  const auto found =
      projected_topology_storage_by_object_.find(modeling_object_id);
  return found != projected_topology_storage_by_object_.end() &&
         found->second == topology_storage;
}

bool SceneModelingSyncService::
    scene_objects_synced_for_current_modeling_content(
        const quader_editor_core::EditorSession &session,
        const SceneDocument &scene, const AssetRegistry &assets) const {
  if (!scene_objects_sync_valid_ ||
      synced_modeling_content_revision_ != session.state().content_revision) {
    return false;
  }
  // The workspace map also contains SDK-owned objects. This legacy sync owner
  // validates only its own runtime participants and must not treat the SDK
  // adapter's workspace-scoped bindings as stale duplicates.
  for (const quader_editor_core::PolygonObject &object : session.objects()) {
    const std::optional<EntityId> entity =
        object_map_.entity_for_modeling_object(object.id);
    if (!entity.has_value() || !scene.exists(*entity)) {
      return false;
    }
    const ModelingObjectComponent *modeling_object =
        scene.modeling_object(*entity);
    const MeshComponent *mesh = scene.mesh(*entity);
    if (modeling_object == nullptr || mesh == nullptr ||
        scene.kind(*entity) != EntityKind::Mesh ||
        modeling_object->object_id != object.id ||
        modeling_object->material_slot !=
            static_cast<std::uint32_t>(object.material_id) ||
        mesh->mesh == kInvalidAssetId || assets.mesh(mesh->mesh) == nullptr ||
        !projected_mesh_signature_known(object.id) ||
        !projected_topology_storage_matches(
            object.id, object.document.storage_token())) {
      return false;
    }
  }
  return true;
}

ModelingToolStatus ModelingOverlayService::tool_status(
    const quader_editor_core::EditorRuntime &runtime,
    bool modal_tool_owns_viewport) const {
  ModelingToolStatus status;
  const quader_editor_core::EditorRuntimeStatus runtime_status =
      runtime.status();
  status.active_tool =
      modeling_viewport_tool_from_runtime(runtime_status.active_tool);
  status.modal_tool_owns_viewport = modal_tool_owns_viewport;
  status.insert_edge_loop_active = runtime_status.insert_edge_loop_active;
  status.insert_edge_loop_dragging = runtime_status.insert_edge_loop_dragging;
  status.insert_edge_loop_has_preview =
      runtime_status.insert_edge_loop_has_preview;
  status.knife_active = runtime_status.knife_active;
  status.knife_has_current = runtime_status.knife_has_current;
  status.knife_can_commit = runtime_status.knife_can_commit;
  status.cut_active = runtime_status.cut_active;
  status.cut_can_preview = runtime_status.cut_can_preview;
  status.cut_preview_valid = runtime_status.cut_preview_valid;

  const quader_editor_core::CutToolState &cut = runtime.cut_state();
  const std::vector<quader::QVec3> cut_overlay_points =
      quader_editor_core::cut_overlay_points(
          cut, runtime.cut_hover().position, runtime.cut_hover().valid);
  status.cut_dragging = quader_editor_core::cut_is_dragging(cut);
  status.cut_dragging_new_point = cut.dragging_new_point;
  status.cut_dragging_line = cut.dragging_line;
  status.cut_dragging_point_index = cut.dragging_point_index;
  status.cut_point_count = static_cast<int>(std::min<std::size_t>(
      cut_overlay_points.size(), status.cut_points.size()));
  status.cut_captured_point_count = static_cast<int>(
      std::min<std::size_t>(cut.points.size(), status.cut_points.size()));
  status.cut_has_line = cut_overlay_points.size() >= 2U;
  for (int index = 0; index < status.cut_point_count; ++index) {
    status.cut_points[static_cast<std::size_t>(index)] =
        from_runtime(cut_overlay_points[static_cast<std::size_t>(index)]);
  }
  status.cut_mode = modeling_cut_mode_from_runtime(cut.mode);

  status.mirror_active = runtime_status.mirror_active;
  status.mirror_can_preview = runtime_status.mirror_can_preview;
  status.mirror_preview_valid = runtime_status.mirror_preview_valid;
  const quader_editor_core::MirrorToolState &mirror = runtime.mirror_state();
  const std::vector<quader::QVec3> mirror_overlay_points =
      quader_editor_core::mirror_overlay_points(
          mirror, runtime.mirror_hover().position,
          runtime.mirror_hover().valid);
  status.mirror_dragging = quader_editor_core::mirror_is_dragging(mirror);
  status.mirror_dragging_new_point = mirror.plane.dragging_new_point;
  status.mirror_dragging_line = mirror.plane.dragging_line;
  status.mirror_dragging_point_index = mirror.plane.dragging_point_index;
  status.mirror_point_count = static_cast<int>(std::min<std::size_t>(
      mirror_overlay_points.size(), status.mirror_points.size()));
  status.mirror_captured_point_count =
      static_cast<int>(std::min<std::size_t>(mirror.plane.points.size(),
                                             status.mirror_points.size()));
  status.mirror_has_line = mirror_overlay_points.size() >= 2U;
  for (int index = 0; index < status.mirror_point_count; ++index) {
    status.mirror_points[static_cast<std::size_t>(index)] =
        from_runtime(mirror_overlay_points[static_cast<std::size_t>(index)]);
  }

  status.poly_active = runtime_status.poly_active;
  status.poly_closed = runtime_status.poly_closed;
  status.poly_has_current = runtime_status.poly_has_current;
  status.poly_can_commit = runtime_status.poly_can_commit;
  status.poly_point_count = runtime_status.poly_point_count;
  return status;
}

bool ModelingOverlayService::has_modal_overlay(const ModelingToolStatus &status) {
  return status.insert_edge_loop_has_preview || status.knife_has_current ||
         status.cut_has_line || status.mirror_has_line ||
         status.poly_has_current;
}

void ModelingOverlayService::invalidate() const {
  selection_overlay_cache_.invalidate();
  overlay_base_cache_.valid = false;
  projected_overlay_base_cache_.valid = false;
}

void ModelingOverlayService::invalidate_composed_frame() const {
  selection_overlay_cache_.invalidate();
}

std::uint64_t ModelingOverlayService::selection_overlay_signature(
    const quader_editor_core::EditorRuntime &runtime,
    ModelingSelectionOverlayOptions options) const {
  // The retained selection frame depends only on persistent selection
  // presentation. Hover and snap state live in the volatile overlay stream;
  // only a tool's stable topology context participates here.
  std::uint64_t hash = 1469598103934665603ULL;
  mix_overlay_signature_hash(hash,
                             options.suppress_persistent_object_selection);
  mix_overlay_signature_hash(hash, options.suppress_persistent_selection);
  (void)runtime;
  mix_overlay_signature_hash(hash, options.persistent_source_wire_object_id);
  return hash;
}

std::uint64_t ModelingOverlayService::tool_preview_signature(
    const quader_editor_core::EditorRuntime &runtime) const {
  std::uint64_t hash = 1469598103934665603ULL;
  const quader_editor_core::BoxToolState &box = runtime.box_state();
  const bool box_has_mesh =
      box.drag_state == quader_editor_core::BoxDragState::Height;
  mix_overlay_signature_hash(hash, box_has_mesh);
  if (box_has_mesh) {
    // The descriptor allocates footprint geometry. Hash its scalar inputs
    // before the render submission gate instead of constructing it here.
    mix_overlay_signature_hash(hash, box.start_point);
    mix_overlay_signature_hash(hash, box.end_point);
    mix_overlay_signature_hash(hash, box.plane.axis_u);
    mix_overlay_signature_hash(hash, box.plane.axis_v);
    mix_overlay_signature_hash(hash, box.plane.normal);
    mix_overlay_signature_hash(hash, box.height_distance);
    mix_overlay_signature_hash(hash, runtime.box_grid_size());
  }

  const quader_editor_core::MirrorToolState &mirror = runtime.mirror_state();
  const bool mirror_has_mesh = mirror_has_mesh_preview(mirror);
  mix_overlay_signature_hash(hash, mirror_has_mesh);
  if (mirror_has_mesh) {
    mix_overlay_signature_hash(
        hash, static_cast<std::uint64_t>(mirror.target_object_ids.size()));
    for (const int object_id : mirror.target_object_ids) {
      mix_overlay_signature_hash(hash, object_id);
    }
    mix_overlay_signature_hash(
        hash, static_cast<std::uint64_t>(mirror.plane.points.size()));
    for (const QVec3 point : mirror.plane.points) {
      mix_overlay_signature_hash(hash, point);
    }
    mix_overlay_signature_hash(hash, mirror.plane.face_normal);
  }
  return hash;
}

std::uint64_t ModelingOverlayService::volatile_overlay_signature(
    const quader_editor_core::EditorRuntime &runtime) const {
  std::uint64_t hash = 1469598103934665603ULL;
  mix_overlay_signature_hash(hash, static_cast<int>(runtime.active_tool()));

  const quader_editor_core::SelectionHoverState &hover = runtime.hover();
  mix_overlay_signature_hash(hash, hover.object_id);
  mix_overlay_signature_hash(hash, static_cast<int>(hover.kind));
  mix_overlay_signature_hash(hash, static_cast<int>(hover.vertex_id));
  mix_overlay_signature_hash(hash, static_cast<int>(hover.edge.a));
  mix_overlay_signature_hash(hash, static_cast<int>(hover.edge.b));
  mix_overlay_signature_hash(hash, static_cast<int>(hover.face_id));
  mix_overlay_signature_hash(hash, hover.selected_modifier_pressed);
  mix_overlay_signature_hash(hash, hover.face_plane_hover);
  mix_overlay_signature_hash(hash, hover.candidate_object_id);

  const quader_editor_core::KnifeToolState &knife = runtime.knife_state();
  mix_overlay_signature_hash(hash, knife.active);
  mix_overlay_signature_hash(hash, knife.has_active_stroke);
  mix_overlay_signature_hash(hash, knife.has_current);
  mix_overlay_signature_hash(hash, knife.current_target_valid);
  mix_overlay_signature_hash(hash, knife.current_object_id);
  mix_overlay_signature_hash(hash, knife.target_object_id);
  mix_knife_target_signature_hash(hash, knife.current_target);
  mix_overlay_signature_hash(hash, knife.current_point);
  mix_overlay_signature_hash(hash, knife.face_normal);
  mix_overlay_signature_hash(
      hash, static_cast<std::uint64_t>(knife.active_stroke_start));
  mix_overlay_signature_hash(
      hash, static_cast<std::uint64_t>(knife.previous_point_index));
  mix_overlay_signature_hash(hash, static_cast<int>(knife.points.size()));
  for (const quader_poly::KnifePointTarget &point : knife.points) {
    mix_knife_target_signature_hash(hash, point);
  }
  mix_overlay_signature_hash(hash,
                             static_cast<int>(knife.point_positions.size()));
  for (const quader::QVec3 &position : knife.point_positions) {
    mix_overlay_signature_hash(hash, position);
  }
  mix_overlay_signature_hash(hash, static_cast<int>(knife.segments.size()));
  for (const quader_poly::KnifeStrokeSegment &segment : knife.segments) {
    mix_overlay_signature_hash(
        hash, static_cast<std::uint64_t>(segment.first_point));
    mix_overlay_signature_hash(
        hash, static_cast<std::uint64_t>(segment.second_point));
  }
  mix_overlay_signature_hash(hash, static_cast<int>(knife.status));
  mix_overlay_signature_hash(hash,
                             static_cast<int>(knife.preview_points.size()));
  for (const quader_editor_core::SelectionOverlayPoint &point :
       knife.preview_points) {
    mix_overlay_signature_hash(hash, static_cast<int>(point.layer));
    mix_overlay_signature_hash(hash, point.position);
    mix_overlay_signature_hash(hash,
                               static_cast<std::uint64_t>(point.color_abgr));
    mix_overlay_signature_hash(hash, static_cast<double>(point.size_pixels));
    mix_overlay_signature_hash(hash, point.object_id);
    mix_overlay_signature_hash(hash, static_cast<int>(point.vertex_id));
  }
  mix_overlay_signature_hash(hash,
                             static_cast<int>(knife.preview_lines.size()));
  for (const quader_editor_core::SelectionOverlayLine &line :
       knife.preview_lines) {
    mix_overlay_signature_hash(hash, static_cast<int>(line.layer));
    mix_overlay_signature_hash(hash, line.start);
    mix_overlay_signature_hash(hash, line.end);
    mix_overlay_signature_hash(hash,
                               static_cast<std::uint64_t>(line.color_abgr));
    mix_overlay_signature_hash(hash, static_cast<double>(line.width_pixels));
    mix_overlay_signature_hash(hash, line.object_id);
    mix_overlay_signature_hash(hash, static_cast<int>(line.face_id));
    mix_overlay_signature_hash(hash, static_cast<int>(line.edge.a));
    mix_overlay_signature_hash(hash, static_cast<int>(line.edge.b));
  }

  const quader_editor_core::TransformDragState &drag =
      runtime.transform_drag_state();
  mix_overlay_signature_hash(hash, drag.active);
  mix_overlay_signature_hash(hash, static_cast<int>(drag.tool));
  mix_overlay_signature_hash(hash, static_cast<int>(drag.operation));
  mix_overlay_signature_hash(hash, static_cast<int>(drag.axis));
  mix_overlay_signature_hash(hash, drag.pivot);
  mix_overlay_signature_hash(hash, drag.scene_transform_preview_eligible);
  mix_overlay_signature_hash(hash, static_cast<int>(drag.snap_target.kind));
  mix_overlay_signature_hash(hash, drag.snap_target.object_id);
  mix_overlay_signature_hash(hash,
                             static_cast<int>(drag.snap_target.vertex_id));
  mix_overlay_signature_hash(hash, drag.snap_target.position);
  if (drag.active) {
    const std::vector<quader_editor_core::SelectionOverlayPoint> snap_points =
        quader_editor_core::transform_drag_snap_overlay_points(drag);
    mix_overlay_signature_hash(hash, static_cast<int>(snap_points.size()));
    for (const quader_editor_core::SelectionOverlayPoint &point : snap_points) {
      mix_overlay_signature_hash(hash, static_cast<int>(point.layer));
      mix_overlay_signature_hash(hash, point.position);
      mix_overlay_signature_hash(hash,
                                 static_cast<std::uint64_t>(point.color_abgr));
      mix_overlay_signature_hash(hash, static_cast<double>(point.size_pixels));
      mix_overlay_signature_hash(hash, point.object_id);
      mix_overlay_signature_hash(hash, static_cast<int>(point.vertex_id));
    }
  }

  const quader_editor_core::CutToolState &cut = runtime.cut_state();
  const quader_editor_core::CutHoverState &cut_hover = runtime.cut_hover();
  mix_overlay_signature_hash(hash, quader_editor_core::cut_is_active(cut));
  mix_overlay_signature_hash(hash, static_cast<int>(cut.points.size()));
  for (const quader::QVec3 &point : cut.points) {
    mix_overlay_signature_hash(hash, point);
  }
  mix_overlay_signature_hash(hash,
                             static_cast<int>(cut.target_object_ids.size()));
  for (const int object_id : cut.target_object_ids) {
    mix_overlay_signature_hash(hash, object_id);
  }
  mix_overlay_signature_hash(hash, cut.source_object_id);
  mix_overlay_signature_hash(hash, cut.face_normal);
  mix_overlay_signature_hash(hash, static_cast<int>(cut.mode));
  mix_overlay_signature_hash(hash, cut.dragging_point_index);
  mix_overlay_signature_hash(hash, cut.dragging_new_point);
  mix_overlay_signature_hash(hash, cut.dragging_line);
  mix_overlay_signature_hash(hash, cut.drag_line_anchor);
  mix_overlay_signature_hash(hash, cut.preview_valid);
  mix_overlay_signature_hash(hash, cut.preview_counts.target_count);
  mix_overlay_signature_hash(hash, cut.preview_counts.split_count);
  mix_overlay_signature_hash(hash, cut.preview_counts.removed_count);
  // DrawnPlaneToolLogic stops presenting hover once both Cut points exist.
  // Excluding that inert state keeps a valid split preview reusable while the
  // pointer moves. Point and drag fields above still invalidate live edits.
  if (cut.points.size() < 2U) {
    mix_overlay_signature_hash(hash, cut_hover.valid);
    mix_overlay_signature_hash(hash, cut_hover.object_id);
    mix_overlay_signature_hash(hash, cut_hover.position);
  }

  const quader_editor_core::MirrorToolState &mirror = runtime.mirror_state();
  const quader_editor_core::MirrorHoverState &mirror_hover =
      runtime.mirror_hover();
  mix_overlay_signature_hash(hash,
                             quader_editor_core::mirror_is_active(mirror));
  mix_overlay_signature_hash(hash,
                             static_cast<int>(mirror.plane.points.size()));
  for (const quader::QVec3 &point : mirror.plane.points) {
    mix_overlay_signature_hash(hash, point);
  }
  mix_overlay_signature_hash(hash, mirror.plane.face_normal);
  mix_overlay_signature_hash(hash, mirror.plane.dragging_point_index);
  mix_overlay_signature_hash(hash, mirror.plane.dragging_new_point);
  mix_overlay_signature_hash(hash, mirror.plane.dragging_line);
  mix_overlay_signature_hash(hash, mirror.plane.drag_line_anchor);
  mix_overlay_signature_hash(hash,
                             static_cast<int>(mirror.target_object_ids.size()));
  for (const int object_id : mirror.target_object_ids) {
    mix_overlay_signature_hash(hash, object_id);
  }
  mix_overlay_signature_hash(hash, mirror.preview_valid);
  mix_overlay_signature_hash(hash, mirror.preview_counts.target_count);
  mix_overlay_signature_hash(hash, mirror.preview_counts.mirrored_count);
  mix_overlay_signature_hash(hash, mirror_hover.valid);
  mix_overlay_signature_hash(hash, mirror_hover.object_id);
  mix_overlay_signature_hash(hash, mirror_hover.position);
  return hash;
}

void SelectionOverlayCache::invalidate() const {
    valid_ = false;
}

bool SelectionOverlayCache::matches(
    std::uint64_t content_revision, std::uint64_t selection_revision,
    std::uint64_t scene_revision,
    std::uint64_t local_visibility_revision,
    quader_editor_core::SelectionMode selection_mode,
    std::uint64_t overlay_signature,
    const quader_editor_core::SelectionHoverState &hover) const {
  return valid_ && content_revision_ == content_revision &&
         selection_revision_ == selection_revision &&
         scene_revision_ == scene_revision &&
         local_visibility_revision_ == local_visibility_revision &&
         selection_mode_ == selection_mode &&
         overlay_signature_ == overlay_signature &&
         hover_matches(hover_, hover);
}

const ViewportOverlayFrame& SelectionOverlayCache::frame() const {
    return frame_;
}

void SelectionOverlayCache::store(
    ViewportOverlayFrame frame, std::uint64_t content_revision,
    std::uint64_t selection_revision, std::uint64_t scene_revision,
    std::uint64_t local_visibility_revision,
    quader_editor_core::SelectionMode selection_mode,
    std::uint64_t overlay_signature,
    const quader_editor_core::SelectionHoverState &hover) const {
  frame_ = std::move(frame);
  content_revision_ = content_revision;
  selection_revision_ = selection_revision;
  scene_revision_ = scene_revision;
  local_visibility_revision_ = local_visibility_revision;
  selection_mode_ = selection_mode;
  overlay_signature_ = overlay_signature;
  hover_ = hover;
  valid_ = true;
}

bool SelectionOverlayCache::hover_matches(
    const quader_editor_core::SelectionHoverState &left,
    const quader_editor_core::SelectionHoverState &right) {
  return left.object_id == right.object_id && left.kind == right.kind &&
         left.vertex_id == right.vertex_id && left.edge == right.edge &&
         left.face_id == right.face_id &&
         left.selected_modifier_pressed == right.selected_modifier_pressed &&
         left.face_plane_hover == right.face_plane_hover &&
         left.candidate_object_id == right.candidate_object_id;
}

bool ModelingOverlayService::selection_frame_matches(
    std::uint64_t content_revision, std::uint64_t selection_revision,
    std::uint64_t scene_revision,
    std::uint64_t local_visibility_revision,
    quader_editor_core::SelectionMode selection_mode,
    std::uint64_t overlay_signature,
    const quader_editor_core::SelectionHoverState &hover) const {
  return selection_overlay_cache_.matches(content_revision, selection_revision,
                                          scene_revision,
                                          local_visibility_revision,
                                          selection_mode,
                                          overlay_signature, hover);
}

const ViewportOverlayFrame &ModelingOverlayService::selection_frame() const {
  return selection_overlay_cache_.frame();
}

void ModelingOverlayService::store_selection_frame(
    ViewportOverlayFrame frame, std::uint64_t content_revision,
    std::uint64_t selection_revision, std::uint64_t scene_revision,
    std::uint64_t local_visibility_revision,
    quader_editor_core::SelectionMode selection_mode,
    std::uint64_t overlay_signature,
    const quader_editor_core::SelectionHoverState &hover) const {
  selection_overlay_cache_.store(std::move(frame), content_revision,
                                 selection_revision, scene_revision,
                                 local_visibility_revision,
                                 selection_mode, overlay_signature, hover);
}

const quader_editor_core::SelectionOverlayFrame &
ModelingOverlayService::selection_overlay_base_frame(
    const quader_editor_core::EditorSession &session,
    ModelingSelectionOverlayOptions options) const {
  const std::uint64_t content_revision = session.state().content_revision;
  const std::uint64_t selection_revision = session.state().selection_revision;
  const int active_object_id = session.active_object_id();
  const quader_editor_core::SelectionMode selection_mode =
      session.selection_mode();
  if (overlay_base_cache_.valid &&
      overlay_base_cache_.content_revision == content_revision &&
      overlay_base_cache_.selection_revision == selection_revision &&
      overlay_base_cache_.active_object_id == active_object_id &&
      overlay_base_cache_.persistent_source_wire_object_id ==
          options.persistent_source_wire_object_id &&
      overlay_base_cache_.selection_mode == selection_mode &&
      overlay_base_cache_.suppress_persistent_object_selection ==
          options.suppress_persistent_object_selection &&
      overlay_base_cache_.suppress_persistent_selection ==
          options.suppress_persistent_selection) {
    return overlay_base_cache_.frame;
  }

  const bool previous_suppressed_persistent_object_selection =
      overlay_base_cache_.suppress_persistent_object_selection;
  const bool previous_suppressed_persistent_selection =
      overlay_base_cache_.suppress_persistent_selection;
  std::vector<OverlayBaseObjectCacheEntry> previous_entries =
      std::move(overlay_base_cache_.object_entries);
  std::unordered_map<int, std::size_t> previous_entry_by_object_id;
  previous_entry_by_object_id.reserve(previous_entries.size());
  for (std::size_t index = 0; index < previous_entries.size(); ++index) {
    previous_entry_by_object_id.emplace(previous_entries[index].object_id,
                                        index);
  }
  OverlayBaseCache next;
  next.valid = true;
  next.content_revision = content_revision;
  next.selection_revision = selection_revision;
  next.active_object_id = active_object_id;
  next.persistent_source_wire_object_id =
      options.persistent_source_wire_object_id;
  next.selection_mode = selection_mode;
  next.suppress_persistent_object_selection =
      options.suppress_persistent_object_selection;
  next.suppress_persistent_selection = options.suppress_persistent_selection;
  next.object_entries.reserve(session.objects().size());
  std::unordered_multimap<std::uint64_t, std::size_t>
      current_entry_by_geometry_signature;
  current_entry_by_geometry_signature.reserve(session.objects().size());
  std::vector<const quader_editor_core::PolygonObject *>
      current_entry_source_objects;
  current_entry_source_objects.reserve(session.objects().size());

  const quader_editor_core::SelectionHoverState empty_hover;
  const bool has_component_source_targets =
      selection_mode != quader_editor_core::SelectionMode::Object &&
      std::any_of(
          session.objects().begin(), session.objects().end(),
          [](const quader_editor_core::PolygonObject &object) {
            return object.selected && object.id > 0 &&
                   (object.component_edit_target ||
                    quader_editor_core::component_selection_has_any_components(
                        object.selection));
          });
  std::size_t aggregate_triangle_count = 0U;
  std::size_t aggregate_line_count = 0U;
  std::size_t aggregate_point_count = 0U;
  {
    QDR_PROFILE_SCOPE("Selection overlay assemble semantic object entries");
    for (const quader_editor_core::PolygonObject &object : session.objects()) {
      const std::uint64_t signature =
          PickingCacheProjection::local_face_geometry_signature(object);
      const auto reusable = previous_entry_by_object_id.find(object.id);
      const bool active_source_wire_object =
          selection_mode != quader_editor_core::SelectionMode::Object &&
          object.id > 0 && object.selected &&
           (object.component_edit_target ||
            quader_editor_core::component_selection_has_any_components(
                object.selection) ||
           (!has_component_source_targets && object.id == active_object_id));
      const bool tool_source_wire_object =
          object.id > 0 &&
          object.id == options.persistent_source_wire_object_id;

      OverlayBaseObjectCacheEntry entry;
      if (reusable != previous_entry_by_object_id.end() &&
          previous_entries[reusable->second].geometry_signature == signature &&
          previous_entries[reusable->second].selection_mode == selection_mode &&
          previous_entries[reusable->second].selected == object.selected &&
          previous_entries[reusable->second].component_edit_target ==
              object.component_edit_target &&
          previous_entries[reusable->second].active_source_wire_object ==
              active_source_wire_object &&
          previous_entries[reusable->second].tool_source_wire_object ==
              tool_source_wire_object &&
          previous_suppressed_persistent_object_selection ==
              options.suppress_persistent_object_selection &&
          previous_suppressed_persistent_selection ==
              options.suppress_persistent_selection &&
          ModelingSelectionService::selection_matches(
              previous_entries[reusable->second].selection, object.selection)) {
        entry = std::move(previous_entries[reusable->second]);
      } else {
        const OverlayBaseObjectCacheEntry *template_entry = nullptr;
        const auto [template_first, template_last] =
            current_entry_by_geometry_signature.equal_range(signature);
        for (auto candidate = template_first; candidate != template_last;
             ++candidate) {
          const std::size_t candidate_index = candidate->second;
          const OverlayBaseObjectCacheEntry &candidate_entry =
              next.object_entries[candidate_index];
          const quader_editor_core::PolygonObject &candidate_object =
              *current_entry_source_objects[candidate_index];
          if (candidate_entry.selection_mode == selection_mode &&
              candidate_entry.selected == object.selected &&
              candidate_entry.component_edit_target ==
                  object.component_edit_target &&
              candidate_entry.active_source_wire_object ==
                  active_source_wire_object &&
              candidate_entry.tool_source_wire_object ==
                  tool_source_wire_object &&
              ModelingSelectionService::selection_matches(
                  candidate_entry.selection, object.selection) &&
              same_overlay_document_layout(candidate_object.document,
                                           object.document)) {
            template_entry = &candidate_entry;
            break;
          }
        }

        if (template_entry != nullptr) {
          QDR_PROFILE_SCOPE(
              "Selection overlay clone identical object template");
          entry = *template_entry;
          entry.object_id = object.id;
          entry.selection = object.selection;
          set_overlay_frame_object_id(entry.frame, object.id);
        } else {
          entry.object_id = object.id;
          entry.geometry_signature = signature;
          entry.selection_mode = selection_mode;
          entry.selected = object.selected;
          entry.component_edit_target = object.component_edit_target;
          entry.active_source_wire_object = active_source_wire_object;
          entry.tool_source_wire_object = tool_source_wire_object;
          entry.selection = object.selection;
          if (options.suppress_persistent_selection) {
            entry.frame = {};
          } else if (options.suppress_persistent_object_selection &&
                     selection_mode ==
                         quader_editor_core::SelectionMode::Object &&
                     object.selected) {
            entry.frame =
                quader_editor_core::build_object_selection_wire_overlay_frame(
                    object, {});
          } else {
            entry.frame = quader_editor_core::build_selection_overlay_frame(
                std::span<const quader_editor_core::PolygonObject>(&object, 1U),
                selection_mode, active_source_wire_object ? object.id : -1,
                empty_hover, {});
          }
        }
        if (selection_mode != quader_editor_core::SelectionMode::Object &&
            !active_source_wire_object && !tool_source_wire_object) {
          remove_source_wire_for_object(entry.frame, object.id);
        }
        if (tool_source_wire_object &&
            !has_complete_source_wire_for_object(entry.frame, object.id)) {
          remove_source_wire_for_object(entry.frame, object.id);
          append_overlay_frame(
              entry.frame,
              quader_editor_core::build_source_wire_overlay_frame(object, {}));
        }
      }
      if (options.suppress_persistent_object_selection &&
          selection_mode == quader_editor_core::SelectionMode::Object &&
          object.selected) {
        remove_persistent_object_face_fill_triangles(entry.frame);
      }
      aggregate_triangle_count += entry.frame.triangles.size();
      aggregate_line_count += entry.frame.lines.size();
      aggregate_point_count += entry.frame.points.size();
      const std::size_t current_entry_index = next.object_entries.size();
      next.object_entries.push_back(std::move(entry));
      current_entry_source_objects.push_back(&object);
      current_entry_by_geometry_signature.emplace(signature,
                                                  current_entry_index);
    }
  }

  {
    QDR_PROFILE_SCOPE("Selection overlay assemble semantic aggregate");
    // Reserve the complete aggregate once. Reserving current-size + source-size
    // for every object makes a many-object component overlay quadratic because
    // each append can reallocate and copy the entire accumulated frame.
    next.frame.triangles.reserve(aggregate_triangle_count);
    next.frame.lines.reserve(aggregate_line_count);
    next.frame.points.reserve(aggregate_point_count);
    for (const OverlayBaseObjectCacheEntry &entry : next.object_entries) {
      append_overlay_frame(next.frame, entry.frame);
    }
  }
  {
    QDR_PROFILE_SCOPE("Selection overlay release previous object entries");
    previous_entries.clear();
  }
  {
    QDR_PROFILE_SCOPE("Selection overlay publish semantic base");
    overlay_base_cache_ = std::move(next);
  }
  return overlay_base_cache_.frame;
}

quader_editor_core::SelectionOverlayFrame
ModelingOverlayService::selection_overlay_current_frame(
    const quader_editor_core::EditorSession &session,
    const quader_editor_core::EditorRuntime &runtime,
    const quader_editor_core::SelectionHoverState &hover,
    ModelingSelectionOverlayOptions options) const {
  const quader_editor_core::SelectionOverlayFrame &base =
      selection_overlay_base_frame(session, options);
  quader_editor_core::SelectionOverlayFrame hover_frame =
      quader_editor_core::build_selection_hover_overlay_frame(
          session.objects(), session.selection_mode(), hover, {});
  if (options.persistent_source_wire_object_id > 0) {
    remove_source_wire_for_object(
        hover_frame, options.persistent_source_wire_object_id);
  }
  const auto append_runtime_tool_overlay =
      [&](quader_editor_core::SelectionOverlayFrame &frame) {
        const quader_editor_core::InsertEdgeLoopToolState &insert_loop =
            runtime.insert_edge_loop_state();
        frame.lines.insert(frame.lines.end(), insert_loop.preview_lines.begin(),
                           insert_loop.preview_lines.end());

        const quader_editor_core::PolyToolState& poly = runtime.poly_state();
        frame.lines.insert(frame.lines.end(), poly.preview_lines.begin(), poly.preview_lines.end());
        frame.points.insert(frame.points.end(), poly.preview_points.begin(), poly.preview_points.end());
      };
  const bool suppress_base_under_hover =
      quader_editor_core::selection_hover_target_is_selected(
          session.objects(), session.selection_mode(), hover);
  if (suppress_base_under_hover) {
    quader_editor_core::SelectionOverlayFrame frame;
    frame.hover_target_selected = hover_frame.hover_target_selected;
    append_selected_hover_filtered_base_frame(frame, base, hover_frame,
                                              suppress_base_under_hover);
    append_overlay_frame(frame, hover_frame);
    append_runtime_tool_overlay(frame);
    return frame;
  }

  quader_editor_core::SelectionOverlayFrame frame = base;
  if (component_hover_replaces_fallback_active_source_wire(session, hover) &&
      session.active_object_id() !=
          options.persistent_source_wire_object_id) {
    remove_source_wire_for_object(frame, session.active_object_id());
  }
  append_overlay_frame(frame, hover_frame);
  append_runtime_tool_overlay(frame);
  return frame;
}

ViewportOverlayFrame ModelingOverlayService::selection_overlay_frame(
    const quader_editor_core::EditorSession &session,
    const quader_editor_core::EditorRuntime &runtime,
    const SceneDocument &scene, const ModelingObjectRuntimeMap &object_registry,
    const ModelingSelectionService &selection_service,
    const ModelingSdkProjectionAdapter &sdk_projection,
    ModelingSelectionOverlayOptions options) const {
  const quader_editor_core::SelectionHoverState persistent_hover;
  (void)sdk_projection;
  const std::uint64_t content_revision = session.state().content_revision;
  const std::uint64_t selection_revision = session.state().selection_revision;
  const std::uint64_t scene_revision = scene.revision();
  const std::uint64_t local_visibility_revision =
      selection_service.viewport_visibility_revision();
  const quader_editor_core::SelectionMode selection_mode =
      session.selection_mode();
  const ModelingInstanceEditScope instance_edit_scope =
      selection_service.overlay_instance_edit_scope();
  const std::uint64_t object_map_revision = object_registry.revision();
  std::uint64_t overlay_signature =
      selection_overlay_signature(runtime, options);
  mix_overlay_signature_hash(overlay_signature, object_map_revision);
  mix_overlay_signature_hash(
      overlay_signature,
      static_cast<std::uint64_t>(instance_edit_scope.occurrence));
  mix_overlay_signature_hash(
      overlay_signature,
      static_cast<std::uint64_t>(instance_edit_scope.definition));
  if (selection_frame_matches(content_revision, selection_revision,
                              scene_revision, local_visibility_revision,
                              selection_mode, overlay_signature,
                              persistent_hover)) {
    QDR_PROFILE_SCOPE("Selection overlay copy cached result");
    return selection_frame();
  }

  const std::function<bool(EntityId)> entity_visible =
      [&selection_service](EntityId entity) {
        return selection_service.viewport_entity_visible(entity);
      };
  const quader_editor_core::SelectionOverlayFrame &base_frame = [&]()
      -> const quader_editor_core::SelectionOverlayFrame & {
    QDR_PROFILE_SCOPE("Selection overlay acquire semantic base");
    return selection_overlay_base_frame(session, options);
  }();
  if (!projected_overlay_base_cache_.valid ||
      projected_overlay_base_cache_.content_revision != content_revision ||
      projected_overlay_base_cache_.selection_revision != selection_revision ||
      projected_overlay_base_cache_.scene_revision != scene_revision ||
      projected_overlay_base_cache_.local_visibility_revision !=
          local_visibility_revision ||
      projected_overlay_base_cache_.object_map_revision !=
          object_map_revision ||
      projected_overlay_base_cache_.selection_mode != selection_mode ||
      projected_overlay_base_cache_.instance_edit_scope !=
          instance_edit_scope ||
      projected_overlay_base_cache_.suppress_persistent_object_selection !=
          options.suppress_persistent_object_selection ||
      projected_overlay_base_cache_.suppress_persistent_selection !=
          options.suppress_persistent_selection ||
      projected_overlay_base_cache_.persistent_source_wire_object_id !=
          options.persistent_source_wire_object_id) {
    QDR_PROFILE_SCOPE("Selection overlay project persistent base");
    projected_overlay_base_cache_.frame =
        SelectionOverlayProjection::project_selection_overlay_frame(
            base_frame, object_registry, scene, selection_mode,
            instance_edit_scope, entity_visible);
    projected_overlay_base_cache_.content_revision = content_revision;
    projected_overlay_base_cache_.selection_revision = selection_revision;
    projected_overlay_base_cache_.scene_revision = scene_revision;
    projected_overlay_base_cache_.local_visibility_revision =
        local_visibility_revision;
    projected_overlay_base_cache_.object_map_revision = object_map_revision;
    projected_overlay_base_cache_.selection_mode = selection_mode;
    projected_overlay_base_cache_.instance_edit_scope = instance_edit_scope;
    projected_overlay_base_cache_.suppress_persistent_object_selection =
        options.suppress_persistent_object_selection;
    projected_overlay_base_cache_.suppress_persistent_selection =
        options.suppress_persistent_selection;
    projected_overlay_base_cache_.persistent_source_wire_object_id =
        options.persistent_source_wire_object_id;
    projected_overlay_base_cache_.valid = true;
  }

  {
    QDR_PROFILE_SCOPE("Selection overlay store persistent frame");
    store_selection_frame(projected_overlay_base_cache_.frame,
                          content_revision, selection_revision,
                          scene_revision, local_visibility_revision,
                          selection_mode, overlay_signature,
                          persistent_hover);
  }
  return selection_frame();
}

ViewportOverlayFrame ModelingOverlayService::volatile_overlay_frame(
    const quader_editor_core::EditorSession &session,
    const quader_editor_core::EditorRuntime &runtime,
    const SceneDocument &scene, const ModelingObjectRuntimeMap &object_registry,
    const ModelingSelectionService &selection_service,
    const ModelingSdkProjectionAdapter &sdk_projection,
    int persistent_source_wire_object_id) const {
  QDR_PROFILE_FUNCTION();
  const quader_editor_core::SelectionMode selection_mode =
      session.selection_mode();
  const ModelingInstanceEditScope instance_edit_scope =
      selection_service.overlay_instance_edit_scope();
  const std::function<bool(EntityId)> entity_visible =
      [&selection_service](EntityId entity) {
        return selection_service.viewport_entity_visible(entity);
      };

  ViewportOverlayFrame frame;
  const quader_editor_core::SelectionHoverState &hover = runtime.hover();
  if (hover.kind != quader_editor_core::SelectionHoverKind::None ||
      hover.candidate_object_id > 0) {
    QDR_PROFILE_SCOPE("Volatile overlay project hover frame");
    quader_editor_core::SelectionOverlayFrame hover_frame;
    const auto append_hover_object = [&](int object_id) {
      const quader_editor_core::PolygonObject *object =
          find_polygon_object_by_id(session.objects(), object_id);
      if (object == nullptr) {
        return;
      }
      append_overlay_frame(
          hover_frame,
          quader_editor_core::build_selection_hover_overlay_frame(
              std::span<const quader_editor_core::PolygonObject>(object, 1U),
              selection_mode, hover, {}));
    };
    append_hover_object(hover.candidate_object_id);
    if (hover.object_id != hover.candidate_object_id) {
      append_hover_object(hover.object_id);
    }
    if (persistent_source_wire_object_id > 0) {
      remove_source_wire_for_object(hover_frame,
                                    persistent_source_wire_object_id);
    }
    frame = SelectionOverlayProjection::project_selection_overlay_frame(
        hover_frame, object_registry, scene, selection_mode,
        instance_edit_scope, entity_visible);
  }

  quader_editor_core::SelectionOverlayFrame runtime_tool_frame;
  const quader_editor_core::InsertEdgeLoopToolState &insert_loop =
      runtime.insert_edge_loop_state();
  runtime_tool_frame.lines.insert(runtime_tool_frame.lines.end(),
                                  insert_loop.preview_lines.begin(),
                                  insert_loop.preview_lines.end());
  const quader_editor_core::PolyToolState &poly = runtime.poly_state();
  runtime_tool_frame.lines.insert(runtime_tool_frame.lines.end(),
                                  poly.preview_lines.begin(),
                                  poly.preview_lines.end());
  runtime_tool_frame.points.insert(runtime_tool_frame.points.end(),
                                   poly.preview_points.begin(),
                                   poly.preview_points.end());
  if (!runtime_tool_frame.lines.empty() ||
      !runtime_tool_frame.points.empty()) {
    append_viewport_overlay_frame(
        frame, SelectionOverlayProjection::project_selection_overlay_frame(
                   runtime_tool_frame, object_registry, scene, selection_mode,
                   instance_edit_scope, entity_visible));
  }
  SelectionOverlayProjection::append_projected_overlay_points(
      frame, runtime.knife_state().preview_points, object_registry, scene,
      ViewportOverlayLayer::ToolHandle,
      ViewportOverlayStyleRole::ExplicitColor, ViewportOverlaySource::Tool,
      instance_edit_scope, entity_visible);
  SelectionOverlayProjection::append_projected_overlay_lines(
      frame, runtime.knife_state().preview_lines, object_registry, scene,
      ViewportOverlayLayer::ToolPreviewLine,
      ViewportOverlayStyleRole::ToolPreviewLine, ViewportOverlaySource::Tool,
      instance_edit_scope, entity_visible);
  if (runtime.transform_drag_active()) {
    SelectionOverlayProjection::append_world_space_overlay_points(
        frame, quader_editor_core::transform_drag_snap_overlay_points(
                   runtime.transform_drag_state()));
  }
  const quader_editor_core::CutToolState &cut = runtime.cut_state();
  if (quader_editor_core::cut_is_active(cut)) {
    const std::vector<quader::QVec3> cut_points =
        quader_editor_core::cut_overlay_points(
            cut, runtime.cut_hover().position, runtime.cut_hover().valid);
    if (quader_editor_core::cut_can_preview(cut) && cut.preview_valid) {
      const std::vector<quader::QVec3> plane_points =
          quader_editor_core::cut_plane_points(cut);
      ModelingSdkPlaneCutPreviewResult sdk_preview;
      if (plane_points.size() >= 3U) {
        sdk_preview = sdk_projection.preview_plane_cut(
            cut.target_object_ids,
            {plane_points[0], plane_points[1], plane_points[2]},
            modeling_cut_mode(cut.mode));
      }
      const CutPreviewOverlayProjection::PreviewTriangleMesh keep_mesh =
          CutPreviewOverlayProjection::mesh_from_previews(
              sdk_preview.kept_meshes);
      const CutPreviewOverlayProjection::PreviewTriangleMesh discard_mesh =
          CutPreviewOverlayProjection::mesh_from_previews(
              sdk_preview.discarded_meshes);

      if (cut.mode == quader_editor_core::CutMode::KeepBoth) {
        CutPreviewOverlayProjection::append_mesh_fill(
            frame, keep_mesh, kOverlayCutPreviewKeepFillColor,
            ViewportOverlayStyleRole::CutPreviewKeepFill);
        CutPreviewOverlayProjection::append_mesh_fill(
            frame, discard_mesh, kOverlayCutPreviewKeepBothDiscardFillColor,
            ViewportOverlayStyleRole::CutPreviewKeepBothDiscardFill);
      } else {
        CutPreviewOverlayProjection::append_mesh_fill(
            frame, discard_mesh, kOverlayCutPreviewDiscardFillColor,
            ViewportOverlayStyleRole::CutPreviewDiscardFill);
      }

      const CutPreviewOverlayProjection::PreviewTriangleMesh &line_source =
          !discard_mesh.empty() ? discard_mesh : keep_mesh;
      if (!line_source.empty() && cut_points.size() >= 2U) {
        const std::uint32_t outline_color =
            cut.mode == quader_editor_core::CutMode::KeepBoth
                ? kOverlayCutPreviewKeepBothOutlineColor
                : kOverlayCutPreviewDiscardOutlineColor;
        CutPreviewOverlayProjection::append_result_lines(
            frame, line_source, cut_points[0], cut.face_normal, outline_color);
      }
    }
    DrawnPlaneOverlayProjection::append(
        frame, cut_points,
        DrawnPlaneOverlayProjection::support_point(cut_points, cut.face_normal),
        cut_points.size() >= 2U);
  }
  const quader_editor_core::MirrorToolState &mirror = runtime.mirror_state();
  if (quader_editor_core::mirror_is_active(mirror)) {
    const std::vector<quader::QVec3> mirror_points =
        quader_editor_core::mirror_overlay_points(
            mirror, runtime.mirror_hover().position,
            runtime.mirror_hover().valid);
    DrawnPlaneOverlayProjection::append(
        frame, mirror_points,
        DrawnPlaneOverlayProjection::support_point(mirror_points,
                                                   mirror.plane.face_normal),
        mirror_points.size() >= 2U);
  }
  return frame;
}

std::vector<ModelingToolPreviewMesh>
ModelingOverlayService::tool_preview_meshes(
    const quader_editor_core::EditorRuntime &runtime,
    const ModelingSdkProjectionAdapter &sdk_projection) const {
  std::vector<ModelingToolPreviewMesh> preview_meshes =
      sdk_projection.workspace_transform_preview_meshes();
  if (const std::optional<MeshAsset> box_preview =
          ToolPreviewMeshProjection::box_preview_mesh_from_descriptor(
              runtime.box_preview_descriptor(true))) {
    preview_meshes.push_back(ModelingToolPreviewMesh{
        .source_entity = kInvalidEntityId,
        .material = {},
        .mesh = *box_preview,
    });
  }
  const quader_editor_core::MirrorToolState &mirror = runtime.mirror_state();
  if (!mirror_has_mesh_preview(mirror)) {
    return preview_meshes;
  }
  const std::vector<quader::QVec3> plane_points =
      quader_editor_core::mirror_plane_points(mirror);
  if (plane_points.size() < 3U) {
    return preview_meshes;
  }
  ModelingSdkMirrorPreviewResult mirror_preview = sdk_projection.preview_mirror(
      mirror.target_object_ids,
      {plane_points[0], plane_points[1], plane_points[2]});
  std::vector<ModelingToolPreviewMesh> &mirror_meshes = mirror_preview.meshes;
  preview_meshes.reserve(preview_meshes.size() + mirror_meshes.size());
  preview_meshes.insert(preview_meshes.end(),
                        std::make_move_iterator(mirror_meshes.begin()),
                        std::make_move_iterator(mirror_meshes.end()));
  return preview_meshes;
}

ModelingCommandResult ModelingCommandExecutor::runtime_unavailable() {
  return {.ok = false,
          .message = "Modeling runtime is not available in this build."};
}

ModelingCommandResult ModelingCommandExecutor::from_runtime(
    const quader_editor_core::CommandResult &result) {
  return {
      .ok = result.ok,
      .changed = result.changed,
      .message = result.message,
  };
}

ModelingCommandExecutor::SyncPlan ModelingCommandExecutor::runtime_sync_plan(
    bool may_change_content, std::uint64_t content_before,
    std::uint64_t content_after,
    const quader_editor_core::CommandResult &result) {
  SyncPlan plan;
  plan.sync_meshes =
      may_change_content && (content_after != content_before || result.changed);
  plan.force_mesh_sync = result.changed;
  plan.sync_selection = true;
  plan.invalidate_overlay = result.changed;
  return plan;
}

quader_editor_core::RepeatLastActionState &
ModelingHistoryBridge::repeat_last_action() {
  return repeat_last_action_;
}

const quader_editor_core::RepeatLastActionState &
ModelingHistoryBridge::repeat_last_action() const {
  return repeat_last_action_;
}

void ModelingHistoryBridge::record_operation(
    const quader_editor_core::EditorOperationCommand &command,
    const quader_editor_core::CommandResult &result) {
  quader_editor_core::record_repeatable_operation(repeat_last_action_, command,
                                                  result);
}

void ModelingHistoryBridge::record_transform(
    const quader_editor_core::EditorSession &session,
    const quader_editor_core::TransformDragState &drag,
    bool duplicate_selected) {
  quader_editor_core::record_repeatable_transform(repeat_last_action_, session,
                                                  drag, duplicate_selected);
}

ModelingInteractionSettingsService::ModelingInteractionSettingsService()
    : settings_(normalize(ModelingInteractionSettings{})) {}

ModelingInteractionSettings ModelingInteractionSettingsService::settings() const {
    return settings_;
}

ModelingInteractionSettings ModelingInteractionSettingsService::normalize(
        const ModelingInteractionSettings& settings) const {
    ModelingInteractionSettings normalized = settings;
    normalized.bridge_steps =
        quader_editor_core::normalize_bridge_operation_settings(
            quader_editor_core::BridgeOperationSettings{
                .steps = normalized.bridge_steps})
            .steps;
    const quader_editor_core::EdgeBevelOperationSettings edge_bevel =
        quader_editor_core::normalize_edge_bevel_operation_settings({
            .width = normalized.edge_bevel_width,
            .profile = normalized.edge_bevel_profile,
            .segments = normalized.edge_bevel_segments,
        });
    normalized.edge_bevel_width = edge_bevel.width;
    normalized.edge_bevel_profile = edge_bevel.profile;
    normalized.edge_bevel_segments = edge_bevel.segments;
    const quader_editor_core::ThickenOperationSettings thicken =
        quader_editor_core::normalize_thicken_operation_settings({
            .thickness = normalized.thicken_thickness,
            .from_center = normalized.thicken_from_center,
        });
    normalized.thicken_thickness = thicken.thickness;
    normalized.thicken_from_center = thicken.from_center;
    const quader_editor_core::SliceQuadOperationSettings slice =
        quader_editor_core::normalize_slice_quad_operation_settings({
            .x_slices = normalized.slice_quad_x_slices,
            .y_slices = normalized.slice_quad_y_slices,
        });
    normalized.slice_quad_x_slices = slice.x_slices;
    normalized.slice_quad_y_slices = slice.y_slices;
    normalized.merge_by_distance_tolerance =
        quader_editor_core::normalize_merge_by_distance_operation_settings(
            quader_editor_core::MergeByDistanceOperationSettings{
                .tolerance = normalized.merge_by_distance_tolerance})
            .tolerance;
    const quader_editor_core::HalfCutOperationSettings half_cut =
        quader_editor_core::normalize_half_cut_operation_settings({
            .axis = static_cast<quader_editor_core::HalfCutAxis>(
                normalized.half_cut_axis),
            .separate_parts = normalized.half_cut_separate_parts,
            .fill_parts = normalized.half_cut_fill_parts,
        });
    normalized.half_cut_axis =
        static_cast<ModelingHalfCutAxis>(half_cut.axis);
    normalized.half_cut_separate_parts = half_cut.separate_parts;
    normalized.half_cut_fill_parts = half_cut.fill_parts;
    return normalized;
}

bool ModelingInteractionSettingsService::set_normalized(
    ModelingInteractionSettings settings) {
  settings = normalize(settings);
  const bool changed = !(settings == settings_);
  settings_ = settings;
  if (changed) {
    ++revision_;
  }
  return changed;
}

bool ModelingInteractionSettingsService::reset_for_target(
    ModelingToolSettingsTargetKind target_kind) {
  const ModelingInteractionSettings before = settings_;
  const ModelingInteractionSettings defaults =
      normalize(ModelingInteractionSettings{});
  switch (target_kind) {
  case ModelingToolSettingsTargetKind::Cut:
    settings_.cut_mode = defaults.cut_mode;
    break;
  case ModelingToolSettingsTargetKind::Bridge:
    settings_.bridge_steps = defaults.bridge_steps;
    break;
  case ModelingToolSettingsTargetKind::EdgeBevel:
    settings_.edge_bevel_width = defaults.edge_bevel_width;
    settings_.edge_bevel_profile = defaults.edge_bevel_profile;
    settings_.edge_bevel_segments = defaults.edge_bevel_segments;
    break;
  case ModelingToolSettingsTargetKind::Thicken:
    settings_.thicken_thickness = defaults.thicken_thickness;
    settings_.thicken_from_center = defaults.thicken_from_center;
    break;
  case ModelingToolSettingsTargetKind::SliceQuad:
    settings_.slice_quad_x_slices = defaults.slice_quad_x_slices;
    settings_.slice_quad_y_slices = defaults.slice_quad_y_slices;
    break;
  case ModelingToolSettingsTargetKind::MergeByDistance:
    settings_.merge_by_distance_tolerance =
        defaults.merge_by_distance_tolerance;
    break;
  case ModelingToolSettingsTargetKind::HalfCut:
    settings_.half_cut_axis = defaults.half_cut_axis;
    settings_.half_cut_separate_parts = defaults.half_cut_separate_parts;
    settings_.half_cut_fill_parts = defaults.half_cut_fill_parts;
    break;
  case ModelingToolSettingsTargetKind::Mirror:
  case ModelingToolSettingsTargetKind::None:
    return false;
  }
  if (settings_ == before) {
    return false;
  }
  ++revision_;
  return true;
}

void ModelingInteractionSettingsService::reset_for_preview_action(
    ModelingActionPreviewKind preview_kind) {
  switch (preview_kind) {
  case ModelingActionPreviewKind::EdgeBevel:
    (void)reset_for_target(ModelingToolSettingsTargetKind::EdgeBevel);
    return;
  case ModelingActionPreviewKind::Thicken:
    (void)reset_for_target(ModelingToolSettingsTargetKind::Thicken);
    return;
  case ModelingActionPreviewKind::MergeByDistance:
    (void)reset_for_target(ModelingToolSettingsTargetKind::MergeByDistance);
    return;
  case ModelingActionPreviewKind::HalfCut:
    (void)reset_for_target(ModelingToolSettingsTargetKind::HalfCut);
    return;
  case ModelingActionPreviewKind::Bridge:
  case ModelingActionPreviewKind::SliceQuad:
  case ModelingActionPreviewKind::None:
    break;
  }
}

void ModelingInteractionSettingsService::set_cut_mode(ModelingCutMode mode) {
  if (settings_.cut_mode == mode) {
    return;
  }
  settings_.cut_mode = mode;
  ++revision_;
}

void ModelingInteractionSettingsService::cycle_cut_mode_without_runtime() {
  settings_.cut_mode = settings_.cut_mode == ModelingCutMode::DiscardLeft
                           ? ModelingCutMode::DiscardRight
                       : settings_.cut_mode == ModelingCutMode::DiscardRight
                           ? ModelingCutMode::KeepBoth
                           : ModelingCutMode::DiscardLeft;
  ++revision_;
}

const std::string &
ModelingToolSettingsService::active_preview_action_id() const {
  return active_preview_action_id_;
}

const std::string &
ModelingToolSettingsService::active_preview_operation_id() const {
  return active_preview_operation_id_;
}

const std::string &ModelingToolSettingsService::active_preview_label() const {
  return active_preview_label_;
}

void ModelingToolSettingsService::set_preview_metadata(std::string action_id,
                                                       std::string operation_id,
                                                       std::string label) {
  if (active_preview_action_id_ == action_id &&
      active_preview_operation_id_ == operation_id &&
      active_preview_label_ == label) {
    return;
  }
  active_preview_action_id_ = std::move(action_id);
  active_preview_operation_id_ = std::move(operation_id);
  active_preview_label_ = std::move(label);
  ++revision_;
}

void ModelingToolSettingsService::clear_preview_metadata() {
  if (active_preview_action_id_.empty() &&
      active_preview_operation_id_.empty() && active_preview_label_.empty()) {
    return;
  }
  active_preview_action_id_.clear();
  active_preview_operation_id_.clear();
  active_preview_label_.clear();
  ++revision_;
}

ModelingToolSettingsTarget
ModelingToolSettingsService::operation_preview_target(
    ModelingToolSettingsTargetKind kind, std::string_view fallback_label,
    std::string_view fallback_operation_id) const {
  ModelingToolSettingsTarget target;
  target.kind = kind;
  target.active = true;
  target.operation_preview_active = true;
  target.label = active_preview_label_.empty() ? std::string(fallback_label)
                                               : active_preview_label_;
  target.action_id = active_preview_action_id_;
  target.operation_id = active_preview_operation_id_.empty()
                            ? std::string(fallback_operation_id)
                            : active_preview_operation_id_;
  return target;
}

ModelingToolSettingsTarget ModelingToolSettingsService::active_preview_target(
    quader_editor_core::PreviewTransactionKind kind) const {
  const ModelingToolSettingsTargetKind preview_kind =
      tool_settings_target_kind_for_preview(kind);
  if (preview_kind == ModelingToolSettingsTargetKind::None) {
    return {};
  }

  std::string fallback_operation_id;
  if (const std::optional<quader_editor_core::EditorOperationId> operation =
          operation_for_preview_kind(kind)) {
    if (const quader_editor_core::EditorOperation *schema =
            quader_editor_core::editor_operation_for_id(*operation)) {
      fallback_operation_id = std::string(schema->id());
    }
  }
  return operation_preview_target(preview_kind,
                                  fallback_tool_settings_label(preview_kind),
                                  fallback_operation_id);
}

ModelingToolSettingsTarget
ModelingToolSettingsService::active_runtime_tool_target(
    const quader_editor_core::EditorRuntimeStatus &runtime_status) const {
  ModelingToolSettingsTarget target;
  if (runtime_status.cut_active) {
    target.kind = ModelingToolSettingsTargetKind::Cut;
    target.active = true;
    target.label =
        fallback_tool_settings_label(ModelingToolSettingsTargetKind::Cut);
    return target;
  }
  return target;
}

ModelingActionQuery ModelingContextActionService::effective_action_query(
    ModelingActionQuery query, bool modal_tool_owns_viewport) const {
  if (query.suppress_when_modal_tool_owns_viewport) {
    query.modal_tool_owns_viewport =
        query.modal_tool_owns_viewport || modal_tool_owns_viewport;
  }
  return query;
}

ModelingActionSet ModelingContextActionService::action_set(
    const ModelingSelectionSummary &summary, ModelingActionQuery query,
    bool modal_tool_owns_viewport) const {
  return ModelingActionCatalog::action_set_for(
      summary, effective_action_query(query, modal_tool_owns_viewport));
}

std::vector<ModelingActionItem>
ModelingContextActionService::actions(const ModelingSelectionSummary &summary,
                                      ModelingActionQuery query,
                                      bool modal_tool_owns_viewport) const {
  return ModelingActionCatalog::actions_for(
      summary, effective_action_query(query, modal_tool_owns_viewport));
}

std::optional<ModelingActionItem> ModelingContextActionService::action(
    const ModelingSelectionSummary &summary, std::string_view action_id,
    ModelingActionQuery query, bool modal_tool_owns_viewport) const {
  query = effective_action_query(query, modal_tool_owns_viewport);
  if (query.modal_tool_owns_viewport) {
    return std::nullopt;
  }

  std::optional<ModelingActionItem> item =
      ModelingActionCatalog::action_for(summary, action_id);
  if (!item.has_value()) {
    return std::nullopt;
  }
  if (item->visibility == ModelingActionVisibility::Hidden &&
      !query.include_hidden) {
    return std::nullopt;
  }
  if (item->visibility == ModelingActionVisibility::Disabled &&
      !query.include_disabled) {
    return std::nullopt;
  }
  return item;
}

ModelingContextActionResolution
ModelingContextActionService::resolve_executable_action(
    const ModelingSelectionSummary &summary, std::string_view action_id,
    bool modal_tool_owns_viewport) const {
  ModelingContextActionResolution resolution;
  if (modal_tool_owns_viewport) {
    resolution.message = "Viewport is owned by an active modal tool.";
    return resolution;
  }

  ModelingActionQuery query;
  query.surface = ModelingActionSurface::ViewportContextMenu;
  query.modal_tool_owns_viewport = false;
  query.include_disabled = true;
  query.include_hidden = false;

  const std::optional<ModelingActionItem> action =
      this->action(summary, action_id, query, modal_tool_owns_viewport);
  if (!action.has_value()) {
    resolution.message = "Action is not available in the current mode.";
    return resolution;
  }
  if (action->visibility != ModelingActionVisibility::Enabled) {
    resolution.action = *action;
    resolution.message = action->disabled_reason.empty()
                             ? "Action is disabled for the current selection."
                             : action->disabled_reason;
    return resolution;
  }

  resolution.ok = true;
  resolution.action = *action;
  return resolution;
}

} // namespace quader

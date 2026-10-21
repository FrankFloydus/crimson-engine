////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/services/modeling_lamina_overlay_service.hpp"

#include "assets/mesh_geometry_projection.hpp"
#include "diagnostics/profile.hpp"
#include "editor/modeling/services/modeling_sdk_projection_adapter.hpp"
#include "editor/modeling/services/modeling_selection_service.hpp"
#include "editor/projection/selection_overlay_projection.hpp"
#include "editor/selection/modeling_selection_projection_snapshot.hpp"
#include "math/glm_bridge.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "scene/scene_document.hpp"

#include <quader/modeling/neutral/geometry/geometry.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace quader {
namespace {

using Point3 = quader_geometry::QVec3d;
using Point2 = quader_geometry::QVec2d;
using Triangle = quader_geometry::QTriangleIndices;

Point3 point3(QVec3 point) { return {point.x, point.y, point.z}; }
double coordinate(Point3 point, std::size_t axis) {
  return axis == 0u ? point.x : axis == 1u ? point.y : point.z;
}
Point3 difference(Point3 left, Point3 right) {
  return {left.x - right.x, left.y - right.y, left.z - right.z};
}
double dot3(Point3 left, Point3 right) {
  return left.x * right.x + left.y * right.y + left.z * right.z;
}
Point3 cross3(Point3 left, Point3 right) {
  return {left.y * right.z - left.z * right.y,
          left.z * right.x - left.x * right.z,
          left.x * right.y - left.y * right.x};
}
bool finite(QVec3 point) {
  return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

struct FaceGeometry final {
  int object_id = -1;
  quader_poly::ElementId face_id = quader_poly::kInvalidElementId;
  std::vector<quader_poly::ElementId> vertex_ids;
  std::vector<QVec3> positions;
  std::vector<Triangle> triangles;
  Point3 minimum;
  Point3 maximum;
  Point3 plane_origin;
  Point3 normal;
  double extent = 0.0;
};

std::vector<Triangle> triangulate_face(std::span<const QVec3> positions) {
  if (positions.size() < 3u) return {};
  const auto origin = point3(positions.front());
  Point3 normal;
  for (std::size_t index = 1u; index + 1u < positions.size(); ++index) {
    const auto product = cross3(difference(point3(positions[index]), origin),
        difference(point3(positions[index + 1u]), origin));
    normal.x += product.x;
    normal.y += product.y;
    normal.z += product.z;
  }
  if (!(dot3(normal, normal) > 0.0)) return {};
  std::vector<Point2> projected;
  projected.reserve(positions.size());
  double extent = 0.0;
  for (const auto position : positions) {
    if (!finite(position)) return {};
    const auto relative = difference(point3(position), origin);
    const auto point = quader_geometry::project_dominant_axis(relative, normal);
    projected.push_back(point);
    extent = std::max({extent, std::abs(point.x), std::abs(point.y)});
  }
  if (!(extent > 0.0)) return {};
  // Normalize only this query's projected coordinates. Document vertices and
  // overlay output retain the exact accepted world positions.
  for (auto &point : projected) {
    point.x /= extent;
    point.y /= extent;
  }
  return quader_geometry::triangulate_projected_polygon(
      std::span<const Point2>(projected), 1.0e-10);
}

void update_bounds(FaceGeometry &face) {
  face.minimum = face.maximum = point3(face.positions.front());
  for (const auto position : face.positions) {
    face.minimum.x = std::min(face.minimum.x, static_cast<double>(position.x));
    face.minimum.y = std::min(face.minimum.y, static_cast<double>(position.y));
    face.minimum.z = std::min(face.minimum.z, static_cast<double>(position.z));
    face.maximum.x = std::max(face.maximum.x, static_cast<double>(position.x));
    face.maximum.y = std::max(face.maximum.y, static_cast<double>(position.y));
    face.maximum.z = std::max(face.maximum.z, static_cast<double>(position.z));
  }
  face.extent = std::max({face.maximum.x - face.minimum.x,
                          face.maximum.y - face.minimum.y,
                          face.maximum.z - face.minimum.z});
}

bool prepare_face_plane(FaceGeometry &face) {
  double largest_area_squared = 0.0;
  for (const auto &triangle : face.triangles) {
    const auto origin = point3(face.positions[triangle[0]]);
    const auto normal = cross3(difference(point3(face.positions[triangle[1]]), origin),
        difference(point3(face.positions[triangle[2]]), origin));
    const double area_squared = dot3(normal, normal);
    if (area_squared > largest_area_squared) {
      largest_area_squared = area_squared;
      face.plane_origin = origin;
      face.normal = normal;
    }
  }
  if (!(largest_area_squared > 0.0)) return false;
  const double inverse_length = 1.0 / std::sqrt(largest_area_squared);
  face.normal.x *= inverse_length;
  face.normal.y *= inverse_length;
  face.normal.z *= inverse_length;
  const double plane_tolerance = face.extent * 2.0e-7;
  // A triangulation supplies render surfaces for a warped polygon too. Lamina
  // classification requires every authored corner to share one support plane.
  return std::ranges::all_of(face.positions, [&](QVec3 position) {
    return std::abs(dot3(face.normal,
        difference(point3(position), face.plane_origin))) <= plane_tolerance;
  });
}

bool bounds_intersect(const FaceGeometry &first, const FaceGeometry &second) {
  return first.minimum.x <= second.maximum.x && second.minimum.x <= first.maximum.x &&
         first.minimum.y <= second.maximum.y && second.minimum.y <= first.maximum.y &&
         first.minimum.z <= second.maximum.z && second.minimum.z <= first.maximum.z;
}

double orientation(Point2 first, Point2 second, Point2 third) {
  return (second.x - first.x) * (third.y - first.y) -
         (second.y - first.y) * (third.x - first.x);
}

bool triangle_interiors_overlap(const std::array<Point2, 3u> &first,
                                const std::array<Point2, 3u> &second) {
  const auto separates = [](const auto &boundary, const auto &other) {
    const double winding = orientation(boundary[0], boundary[1], boundary[2]);
    if (winding == 0.0) return true;
    const double sign = winding > 0.0 ? 1.0 : -1.0;
    for (std::size_t edge = 0u; edge < 3u; ++edge) {
      const auto a = boundary[edge];
      const auto b = boundary[(edge + 1u) % 3u];
      bool positive_interior = false;
      for (const auto point : other) {
        const double first_product = (b.x - a.x) * (point.y - a.y);
        const double second_product = (b.y - a.y) * (point.x - a.x);
        const double roundoff = 64.0 * std::numeric_limits<double>::epsilon() *
            (std::abs(first_product) + std::abs(second_product));
        positive_interior |= sign * (first_product - second_product) > roundoff;
      }
      // A boundary shared by two faces has no strictly interior sample on
      // one side. This excludes both edge and vertex contact.
      if (!positive_interior) return true;
    }
    return false;
  };
  return !separates(first, second) && !separates(second, first);
}

bool faces_coplanar(const FaceGeometry &first, const FaceGeometry &second) {
  if (std::abs(dot3(first.normal, second.normal)) < 1.0 - 1.0e-12) return false;
  const double plane_tolerance = std::max(first.extent, second.extent) * 2.0e-7;
  for (const auto position : first.positions) {
    if (std::abs(dot3(second.normal,
        difference(point3(position), second.plane_origin))) > plane_tolerance) return false;
  }
  for (const auto position : second.positions) {
    if (std::abs(dot3(first.normal,
        difference(point3(position), first.plane_origin))) > plane_tolerance) return false;
  }
  return true;
}

struct ClipPoint final {
  Point2 projected;
  Point3 world;
};

Point2 project_face_point(Point3 position, const FaceGeometry &plane) {
  return quader_geometry::project_dominant_axis(
      difference(position, plane.plane_origin), plane.normal);
}

Point2 project_face_point(QVec3 position, const FaceGeometry &plane) {
  return project_face_point(point3(position), plane);
}

ClipPoint interpolate(ClipPoint first, ClipPoint second, double fraction) {
  if (fraction <= 0.0) return first;
  if (fraction >= 1.0) return second;
  return {{first.projected.x + (second.projected.x - first.projected.x) * fraction,
           first.projected.y + (second.projected.y - first.projected.y) * fraction},
          {first.world.x + (second.world.x - first.world.x) * fraction,
           first.world.y + (second.world.y - first.world.y) * fraction,
           first.world.z + (second.world.z - first.world.z) * fraction}};
}

double orientation_error(Point2 first, Point2 second, Point2 point) {
  return 64.0 * std::numeric_limits<double>::epsilon() *
      (std::abs((second.x - first.x) * (point.y - first.y)) +
       std::abs((second.y - first.y) * (point.x - first.x)));
}

std::vector<ClipPoint> clip_triangle(std::array<ClipPoint, 3u> first,
                                   const std::array<Point2, 3u> &second) {
  const double sign = orientation(second[0], second[1], second[2]) > 0.0 ? 1.0 : -1.0;
  std::vector<ClipPoint> polygon(first.begin(), first.end());
  std::vector<ClipPoint> clipped;
  clipped.reserve(8u);
  for (std::size_t edge = 0u; edge < 3u && !polygon.empty(); ++edge) {
    clipped.clear();
    const auto a = second[edge];
    const auto b = second[(edge + 1u) % 3u];
    auto previous = polygon.back();
    double previous_distance = sign * orientation(a, b, previous.projected);
    bool previous_inside = previous_distance >= -orientation_error(a, b, previous.projected);
    for (const auto current : polygon) {
      const double current_distance = sign * orientation(a, b, current.projected);
      const bool current_inside = current_distance >= -orientation_error(a, b, current.projected);
      if (current_inside != previous_inside) {
        const double denominator = previous_distance - current_distance;
        if (denominator != 0.0) {
          clipped.push_back(interpolate(previous, current,
              std::clamp(previous_distance / denominator, 0.0, 1.0)));
        }
      }
      if (current_inside) clipped.push_back(current);
      previous = current;
      previous_distance = current_distance;
      previous_inside = current_inside;
    }
    polygon.swap(clipped);
  }
  return polygon;
}

QVec3 render_point(Point3 point) {
  return {static_cast<float>(point.x), static_cast<float>(point.y), static_cast<float>(point.z)};
}

bool append_overlap_fill(ViewportOverlayFrame &frame, const FaceGeometry &first,
                         const FaceGeometry &second,
                         std::vector<std::vector<ClipPoint>> &patches) {
  if (!faces_coplanar(first, second)) return false;
  bool overlap = false;
  for (const auto &first_triangle : first.triangles) {
    std::array<Point2, 3u> projected_a;
    std::array<ClipPoint, 3u> clip_a;
    for (std::size_t corner = 0u; corner < 3u; ++corner) {
      const auto world = first.positions[first_triangle[corner]];
      projected_a[corner] = project_face_point(world, first);
      clip_a[corner] = {projected_a[corner], point3(world)};
    }
    for (const auto &second_triangle : second.triangles) {
      std::array<Point2, 3u> projected_b;
      for (std::size_t corner = 0u; corner < 3u; ++corner) {
        projected_b[corner] = project_face_point(second.positions[second_triangle[corner]], first);
      }
      if (!triangle_interiors_overlap(projected_a, projected_b)) continue;
      auto polygon = clip_triangle(clip_a, projected_b);
      bool positive_patch = false;
      for (std::size_t corner = 1u; corner + 1u < polygon.size(); ++corner) {
        if (std::abs(orientation(polygon[0].projected, polygon[corner].projected,
                polygon[corner + 1u].projected)) <=
            orientation_error(polygon[0].projected, polygon[corner].projected,
                polygon[corner + 1u].projected)) continue;
        overlap = true;
        positive_patch = true;
        // Both authored identities own the same geometric patch. The shared
        // diagnostic fill pass consumes coincident coverage once on the GPU.
        for (const auto *face : {&first, &second}) {
          frame.triangles.push_back({.layer = ViewportOverlayLayer::DiagnosticFace,
              .a = render_point(polygon[0].world), .b = render_point(polygon[corner].world),
              .c = render_point(polygon[corner + 1u].world), .color = kOverlayLaminaFaceColor,
              .style_role = ViewportOverlayStyleRole::LaminaFace,
              .source = ViewportOverlaySource::Diagnostic,
              .element = {.object_id = face->object_id,
                  .face_id = static_cast<int>(face->face_id)}});
        }
      }
      if (positive_patch) patches.push_back(std::move(polygon));
    }
  }
  return overlap;
}

using EdgeInterval = std::pair<double, double>;
using FaceEdgeIntervals = std::vector<std::vector<EdgeInterval>>;

std::optional<EdgeInterval> clip_edge(Point2 start, Point2 end,
                                    std::span<const Point2> polygon) {
  const double winding = quader_geometry::polygon_signed_area(polygon);
  if (winding == 0.0) return std::nullopt;
  const double sign = winding > 0.0 ? 1.0 : -1.0;
  double begin = 0.0;
  double finish = 1.0;
  for (std::size_t edge = 0u; edge < polygon.size(); ++edge) {
    const auto a = polygon[edge];
    const auto b = polygon[(edge + 1u) % polygon.size()];
    const double start_distance = sign * orientation(a, b, start);
    const double end_distance = sign * orientation(a, b, end);
    const bool start_outside = start_distance < -orientation_error(a, b, start);
    const bool end_outside = end_distance < -orientation_error(a, b, end);
    if (start_outside && end_outside) return std::nullopt;
    if (start_outside != end_outside) {
      const double fraction = std::clamp(start_distance / (start_distance - end_distance), 0.0, 1.0);
      if (start_outside) begin = std::max(begin, fraction);
      else finish = std::min(finish, fraction);
    }
    if (finish <= begin) return std::nullopt;
  }
  return EdgeInterval{begin, finish};
}

void append_edge_intervals(const FaceGeometry &face,
                           std::span<const std::vector<ClipPoint>> patches,
                           FaceEdgeIntervals &intervals) {
  if (intervals.empty()) intervals.resize(face.positions.size());
  for (const auto &patch : patches) {
    std::vector<Point2> projected;
    projected.reserve(patch.size());
    for (const auto &point : patch) projected.push_back(project_face_point(point.world, face));
    for (std::size_t corner = 0u; corner < face.positions.size(); ++corner) {
      const auto start = project_face_point(face.positions[corner], face);
      const auto end = project_face_point(face.positions[(corner + 1u) % face.positions.size()], face);
      if (const auto interval = clip_edge(start, end, projected))
        intervals[corner].push_back(*interval);
    }
  }
}

void append_overlap_edges(ViewportOverlayFrame &frame, const FaceGeometry &face,
                          FaceEdgeIntervals &intervals) {
  for (std::size_t corner = 0u; corner < intervals.size(); ++corner) {
    auto &parts = intervals[corner];
    if (parts.empty()) continue;
    std::ranges::sort(parts);
    const auto start = face.positions[corner];
    const auto end = face.positions[(corner + 1u) % face.positions.size()];
    const ClipPoint first{{}, point3(start)}, second{{}, point3(end)};
    const auto emit = [&](EdgeInterval interval) {
      const auto a = render_point(interpolate(first, second, interval.first).world);
      const auto b = render_point(interpolate(first, second, interval.second).world);
      if (a.x == b.x && a.y == b.y && a.z == b.z) return;
      frame.lines.push_back({.layer = ViewportOverlayLayer::DiagnosticEdge,
          .start = a, .end = b, .color = kOverlayLaminaWireColor,
          .width_pixels = kOverlayLaminaWireLineWidthPixels,
          .style_role = ViewportOverlayStyleRole::LaminaWire,
          .source = ViewportOverlaySource::Diagnostic,
          // These are effective world coordinates, without renderer bindings.
          .element = {.object_id = face.object_id,
              .face_id = static_cast<int>(face.face_id)}});
    };
    auto merged = parts.front();
    for (std::size_t index = 1u; index < parts.size(); ++index) {
      constexpr double kParameterRoundoff = 128.0 * std::numeric_limits<double>::epsilon();
      if (parts[index].first <= merged.second + kParameterRoundoff)
        merged.second = std::max(merged.second, parts[index].second);
      else {
        emit(merged);
        merged = parts[index];
      }
    }
    emit(merged);
  }
}

struct CandidateCoordinates final {
  std::shared_ptr<const std::vector<QVec3>> positions;
  std::shared_ptr<const MeshGeometryProjection> projection;
  std::shared_ptr<const MeshGeometryPayload> geometry;
  std::span<const MeshVertex> vertices;
  QMat4 transform;
  std::unordered_map<quader_poly::ElementId, QVec3> by_vertex;
};

CandidateCoordinates preview_coordinates(const ModelingToolPreviewMesh &preview) {
  CandidateCoordinates candidate;
  candidate.projection = preview.geometry_projection ? preview.geometry_projection
                                                     : preview.mesh.geometry_projection();
  candidate.positions = preview.dynamic_position_payload ? preview.dynamic_position_payload
      : candidate.projection ? candidate.projection->positions : nullptr;
  candidate.geometry = candidate.projection ? candidate.projection->source_geometry
                                           : preview.mesh.geometry_payload();
  candidate.transform = preview.transform;
  const auto vertices = candidate.projection ? candidate.projection->vertices()
                                             : preview.mesh.geometry_vertices();
  candidate.vertices = vertices;
  if (!candidate.positions || candidate.positions->size() != vertices.size()) return candidate;
  candidate.by_vertex.reserve(vertices.size());
  for (std::size_t index = 0u; index < vertices.size(); ++index) {
    candidate.by_vertex.try_emplace(vertices[index].source_vertex_id,
                                    (*candidate.positions)[index]);
  }
  return candidate;
}

} // namespace

struct ModelingLaminaOverlayService::Impl final {
  struct SourceKey final {
    const SceneDocument *scene = nullptr;
    std::uint64_t content = 0u;
    std::uint64_t transforms = 0u;
    std::uint64_t visibility = 0u;
    std::uint64_t hierarchy = 0u;
    std::uint64_t layers = 0u;
    std::uint64_t bindings = 0u;
    std::uint64_t object_map = 0u;
    std::uint64_t viewport_visibility = 0u;
    std::uint64_t preview_revision = 0u;
    std::uint64_t cut_revision = 0u;
    ModelingInstanceEditScope scope;
    bool preview_active = false;
    friend bool operator==(const SourceKey &, const SourceKey &) = default;
  };
  struct ObjectGeometry final {
    quader_editor_core::CowPolygonDocument document;
    std::vector<FaceGeometry> local_faces;
    std::vector<FaceGeometry> world_faces;
    std::shared_ptr<const std::vector<QVec3>> positions;
    std::shared_ptr<const MeshGeometryPayload> geometry;
    std::shared_ptr<const std::vector<MeshVertex>> attributes;
    std::span<const MeshVertex> vertices;
    QMat4 world;
    bool valid = false;
  };
  std::optional<SourceKey> key;
  std::unordered_map<int, ObjectGeometry> objects;
  ViewportOverlayFrame frame;
  ModelingLaminaOverlayStats work;

  void rebuild_local(ObjectGeometry &retained,
                     const quader_editor_core::PolygonObject &object) {
    ++work.local_geometry_build_count;
    retained.document = object.document;
    retained.local_faces.clear();
    retained.valid = false;
    const auto source = object.document.read_view();
    std::unordered_map<quader_poly::ElementId, QVec3> vertices;
    vertices.reserve(source.vertices.size());
    for (const auto &vertex : source.vertices) vertices.emplace(vertex.id, vertex.position);
    for (const auto &face : source.faces) {
      FaceGeometry local;
      local.object_id = object.id;
      local.face_id = face.id;
      local.vertex_ids = face.vertices;
      for (const auto id : face.vertices) {
        const auto found = vertices.find(id);
        if (found == vertices.end()) break;
        local.positions.push_back(found->second);
      }
      if (local.positions.size() != face.vertices.size()) continue;
      local.triangles = triangulate_face(local.positions);
      if (!local.triangles.empty()) retained.local_faces.push_back(std::move(local));
    }
  }
};

ModelingLaminaOverlayService::ModelingLaminaOverlayService() : impl_(std::make_unique<Impl>()) {}
ModelingLaminaOverlayService::~ModelingLaminaOverlayService() = default;

const ViewportOverlayFrame &ModelingLaminaOverlayService::update(
    const ModelingSelectionProjectionSnapshot &snapshot, const SceneDocument &scene,
    const ModelingObjectRuntimeMap &object_map, const ModelingSelectionService &selection,
    const ModelingSdkProjectionAdapter &projection) {
  const Impl::SourceKey key{
      .scene = &scene, .content = snapshot.content_revision(),
      .transforms = scene.transform_revision(), .visibility = scene.visibility_revision(),
      .hierarchy = scene.hierarchy_presentation_revision(), .layers = scene.layer_revision(),
      .bindings = scene.mesh_binding_revision(), .object_map = object_map.revision(),
      .viewport_visibility = selection.viewport_visibility_revision(),
      .preview_revision = projection.workspace_transform_preview_revision(),
      .cut_revision = projection.plane_cut_preview_revision(),
      .scope = selection.overlay_instance_edit_scope(),
      .preview_active = projection.workspace_transform_preview_active()};
  if (impl_->key && *impl_->key == key) return impl_->frame;
  QDR_PROFILE_SCOPE("Modeling lamina diagnostic update");
  ++impl_->work.build_count;
  std::unordered_map<int, CandidateCoordinates> candidates;
  if (key.preview_active) {
    ++impl_->work.preview_extraction_count;
    for (const auto &preview : projection.workspace_transform_preview_meshes())
      candidates.try_emplace(preview.source_object_id, preview_coordinates(preview));
  } else if (const auto accepted = projection.accepted_overlay_coordinates()) {
    ++impl_->work.preview_extraction_count;
    for (const auto &preview : accepted->meshes)
      candidates.try_emplace(preview.source_object_id, preview_coordinates(preview));
  }
  const auto visible = [&selection](EntityId entity) {
    return selection.viewport_entity_visible(entity);
  };
  std::unordered_set<int> retained_ids;
  for (const auto &object : snapshot.objects()) {
    const auto world = SelectionOverlayProjection::visible_object_world_transform(
        object.id, object_map, scene, key.scope, visible);
    if (!world) continue;
    retained_ids.insert(object.id);
    auto [entry, inserted] = impl_->objects.try_emplace(object.id);
    auto &retained = entry->second;
    if (inserted || !retained.document.shares_storage_with(object.document))
      impl_->rebuild_local(retained, object);
    const auto candidate = candidates.find(object.id);
    const auto *coordinates = candidate == candidates.end() ? nullptr : &candidate->second;
    const auto positions = coordinates != nullptr ? coordinates->positions : nullptr;
    const auto vertices = coordinates != nullptr ? coordinates->vertices
                                                 : std::span<const MeshVertex>{};
    const auto placement = coordinates != nullptr ? coordinates->transform : *world;
    if (retained.valid && retained.positions == positions &&
        retained.vertices.data() == vertices.data() &&
        retained.vertices.size() == vertices.size() &&
        retained.world.values == placement.values) continue;
    retained.world_faces.clear();
    for (const auto &local : retained.local_faces) {
      FaceGeometry face = local;
      for (std::size_t corner = 0u; corner < face.positions.size(); ++corner) {
        if (coordinates != nullptr) {
          const auto point = coordinates->by_vertex.find(face.vertex_ids[corner]);
          if (point != coordinates->by_vertex.end()) face.positions[corner] = point->second;
        }
        face.positions[corner] = transform_point(placement, face.positions[corner]);
      }
      if (!std::ranges::all_of(face.positions, finite)) continue;
      if (coordinates != nullptr) face.triangles = triangulate_face(face.positions);
      if (face.triangles.empty()) continue;
      update_bounds(face);
      if (!prepare_face_plane(face)) continue;
      retained.world_faces.push_back(std::move(face));
      ++impl_->work.world_face_build_count;
    }
    retained.positions = positions;
    retained.geometry = coordinates != nullptr ? coordinates->geometry : nullptr;
    retained.attributes = coordinates != nullptr && coordinates->projection
        ? coordinates->projection->vertex_attributes : nullptr;
    retained.vertices = vertices;
    retained.world = placement;
    retained.valid = true;
  }
  std::erase_if(impl_->objects, [&](const auto &entry) {
    return !retained_ids.contains(entry.first);
  });
  std::vector<const FaceGeometry *> faces;
  for (const auto &[id, object] : impl_->objects)
    for (const auto &face : object.world_faces) faces.push_back(&face);
  std::size_t sweep_axis = 0u;
  if (!faces.empty()) {
    std::array<double, 3u> minimum_centers;
    std::array<double, 3u> maximum_centers;
    for (std::size_t axis = 0u; axis < 3u; ++axis) {
      minimum_centers[axis] = maximum_centers[axis] =
          (coordinate(faces.front()->minimum, axis) +
           coordinate(faces.front()->maximum, axis)) * 0.5;
    }
    for (const auto *face : faces) {
      for (std::size_t axis = 0u; axis < 3u; ++axis) {
        const double center = (coordinate(face->minimum, axis) +
                               coordinate(face->maximum, axis)) * 0.5;
        minimum_centers[axis] = std::min(minimum_centers[axis], center);
        maximum_centers[axis] = std::max(maximum_centers[axis], center);
      }
    }
    for (std::size_t axis = 1u; axis < 3u; ++axis) {
      if (maximum_centers[axis] - minimum_centers[axis] >
          maximum_centers[sweep_axis] - minimum_centers[sweep_axis])
        sweep_axis = axis;
    }
  }
  std::ranges::sort(faces, [sweep_axis](const auto *left, const auto *right) {
    if (coordinate(left->minimum, sweep_axis) != coordinate(right->minimum, sweep_axis))
      return coordinate(left->minimum, sweep_axis) < coordinate(right->minimum, sweep_axis);
    if (left->object_id != right->object_id) return left->object_id < right->object_id;
    return left->face_id < right->face_id;
  });
  ViewportOverlayFrame next_frame;
  std::vector<FaceEdgeIntervals> edge_intervals(faces.size());
  for (std::size_t first = 0u; first < faces.size(); ++first) {
    for (std::size_t second = first + 1u; second < faces.size(); ++second) {
      ++impl_->work.broadphase_comparison_count;
      if (coordinate(faces[second]->minimum, sweep_axis) >
          coordinate(faces[first]->maximum, sweep_axis)) break;
      if (!bounds_intersect(*faces[first], *faces[second])) continue;
      ++impl_->work.candidate_pair_count;
      std::vector<std::vector<ClipPoint>> patches;
      if (append_overlap_fill(next_frame, *faces[first], *faces[second], patches)) {
        // Closed face domains can have an unrelated edge-only contact in
        // addition to area overlap. Only the positive-area patches own red wire.
        append_edge_intervals(*faces[first], patches, edge_intervals[first]);
        append_edge_intervals(*faces[second], patches, edge_intervals[second]);
      }
    }
  }
  for (std::size_t index = 0u; index < faces.size(); ++index) {
    append_overlap_edges(next_frame, *faces[index], edge_intervals[index]);
  }
  impl_->frame = std::move(next_frame);
  impl_->key = key;
  return impl_->frame;
}

ModelingLaminaOverlayStats ModelingLaminaOverlayService::stats() const { return impl_->work; }
std::uint64_t ModelingLaminaOverlayService::build_count() const { return impl_->work.build_count; }

} // namespace quader

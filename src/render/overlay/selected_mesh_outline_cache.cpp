////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/selected_mesh_outline_cache.hpp"

#include "math/glm_bridge.hpp"
#include "render/overlay/overlay_camera_projector.hpp"
#include "render/viewport_overlay_policy.hpp"

#include <quader/modeling/neutral/geometry/bvh.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <utility>

namespace quader {
namespace {

bool finite(QVec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

std::array<float, 3> position_key(QVec3 value) { return {value.x, value.y, value.z}; }

/// Welds identical positions across UV and normal splits without merging nearby vertices.
struct PositionHash {
  std::size_t operator()(const std::array<float, 3> &key) const noexcept {
    std::size_t hash = 0;
    for (float component : key) {
      hash ^= std::hash<float>{}(component) + 0x9e3779b9u + (hash << 6u) + (hash >> 2u);
    }
    return hash;
  }
};

/// Stores camera-independent adjacency for one immutable mesh payload.
struct OutlineTopology {
  /// A welded edge and the number of nondegenerate triangles incident to it.
  struct Edge {
    QVec3 start, end;
    std::array<QVec3, 2> inside;
    std::uint32_t count = 0;
  };
  /// A triangle plane and its three indices into the shared edge list.
  struct Face {
    QVec3 normal, center;
    std::array<std::size_t, 3> edges;
    std::array<QVec3, 3> points;
  };
  std::shared_ptr<const MeshGeometryPayload> payload;
  std::vector<Edge> edges;
  std::vector<Face> faces;
  std::array<QVec3, 8> bounds;
};

std::shared_ptr<OutlineTopology> build_topology(
    std::shared_ptr<const MeshGeometryPayload> payload,
    std::span<const MeshVertex> vertices, std::span<const std::uint32_t> indices) {
  auto topology = std::make_shared<OutlineTopology>();
  topology->payload = std::move(payload);
  std::unordered_map<std::array<float, 3>, std::uint32_t, PositionHash> positions;
  std::unordered_map<std::uint64_t, std::size_t> edges;
  positions.reserve(vertices.size());
  edges.reserve(indices.size());
  topology->faces.reserve(indices.size() / 3);
  glm::vec3 minimum(std::numeric_limits<float>::infinity()), maximum(-minimum);
  for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
    if (indices[i] >= vertices.size() || indices[i + 1] >= vertices.size() ||
        indices[i + 2] >= vertices.size()) continue;
    const std::array<QVec3, 3> points{vertices[indices[i]].position,
        vertices[indices[i + 1]].position, vertices[indices[i + 2]].position};
    if (!std::all_of(points.begin(), points.end(), finite)) continue;
    const QVec3 normal = cross(points[1] - points[0], points[2] - points[0]);
    const float magnitude = length(normal);
    if (!std::isfinite(magnitude) || magnitude <= 0) continue;
    for (const auto point : points) {
      minimum = glm::min(minimum, to_glm(point));
      maximum = glm::max(maximum, to_glm(point));
    }
    OutlineTopology::Face face{normal / magnitude, (points[0] + points[1] + points[2]) / 3.0f, {}, points};
    std::array<std::uint32_t, 3> welded;
    for (std::size_t j = 0; j < 3; ++j) {
      welded[j] = positions.try_emplace(position_key(points[j]),
          static_cast<std::uint32_t>(positions.size())).first->second;
    }
    for (std::size_t j = 0; j < 3; ++j) {
      const std::size_t next = (j + 1) % 3;
      const auto a = std::min(welded[j], welded[next]);
      const auto b = std::max(welded[j], welded[next]);
      const std::uint64_t key = (static_cast<std::uint64_t>(a) << 32u) | b;
      const auto [entry, inserted] = edges.try_emplace(key, topology->edges.size());
      if (inserted) topology->edges.push_back({points[j], points[next], {}, 0});
      face.edges[j] = entry->second;
      auto &edge = topology->edges[entry->second];
      edge.inside[std::min(edge.count, 1u)] = face.center;
      ++edge.count;
    }
    topology->faces.push_back(face);
  }
  for (unsigned corner = 0; corner < topology->bounds.size(); ++corner) {
    topology->bounds[corner] = {
        corner & 1 ? maximum.x : minimum.x, corner & 2 ? maximum.y : minimum.y,
        corner & 4 ? maximum.z : minimum.z};
  }
  return topology;
}

/// Rejects only whole occurrences beyond one padded viewport side, before perspective division.
struct OutlineFrustum {
  std::array<glm::dvec4, 4> sides;

  OutlineFrustum(const ViewportOverlayCamera &input, float width_pixels) {
    const auto camera = OverlayCameraProjector::sanitized_camera(input);
    // The shader extends square caps along both the line and its normal.
    const double padding = std::sqrt(2.0) *
        (std::max(1.0, static_cast<double>(width_pixels)) + kOverlayLineFeatherPixels) * 0.5;
    const double aspect = camera.viewport_size.x / static_cast<double>(camera.viewport_size.y);
    const bool perspective = camera.projection == ViewportOverlayProjection::Perspective;
    const double half_height = perspective
        ? std::tan(camera.fov_degrees * 3.14159265358979323846 / 360.0)
        : std::max(0.0001, camera.orthographic_size * 0.5);
    const double horizontal = half_height * aspect * (1 + 2 * padding / camera.viewport_size.x);
    const double vertical = half_height * (1 + 2 * padding / camera.viewport_size.y);
    const glm::dvec3 eye(to_glm(camera.eye)), right(to_glm(camera.right));
    const glm::dvec3 up(to_glm(camera.up)), forward(to_glm(camera.forward));
    for (std::size_t index = 0; index < sides.size(); ++index) {
      const double extent = index < 2 ? horizontal : vertical;
      const glm::dvec3 axis = (index < 2 ? right : up) * (index % 2 == 0 ? 1.0 : -1.0);
      const glm::dvec3 normal = axis + (perspective ? forward * extent : glm::dvec3(0));
      sides[index] = {normal, -glm::dot(normal, eye) + (perspective ? 0 : extent)};
    }
  }

  bool intersects(const std::array<QVec3, 8> &bounds) const {
    unsigned outside = 15;
    for (const auto corner : bounds) {
      if (!finite(corner)) return true;
      unsigned corner_outside = 0;
      for (std::size_t index = 0; index < sides.size(); ++index) {
        const auto &plane = sides[index];
        const auto normal = glm::dvec3(plane);
        const glm::dvec3 point(to_glm(corner));
        const double distance = glm::dot(normal, point) + plane.w;
        const double tolerance = (glm::dot(glm::abs(normal), glm::abs(point)) +
            std::abs(plane.w) + 1) * 1e-7;
        if (!std::isfinite(distance)) return true;
        if (distance < -tolerance) corner_outside |= 1u << index;
      }
      outside &= corner_outside;
      if (outside == 0) return true;
    }
    return outside == 0;
  }
};

bool same_lines(const ViewportOverlayFrame &a, const ViewportOverlayFrame &b) {
  return a.lines.size() == b.lines.size() && std::equal(a.lines.begin(), a.lines.end(),
      b.lines.begin(), [](const auto &left, const auto &right) {
        return position_key(left.start) == position_key(right.start) &&
               position_key(left.end) == position_key(right.end) &&
               left.style_role == right.style_role;
      });
}

double cross_2d(glm::dvec2 a, glm::dvec2 b) { return a.x * b.y - a.y * b.x; }

/// Projects into an unscaled view plane; FOV, DPI and orthographic panning do not change coverage.
struct OutlineProjection {
  bool perspective;
  double near_plane;
  glm::dvec3 eye, right, up, forward;

  explicit OutlineProjection(const ViewportOverlayCamera &camera)
      : perspective(camera.projection == ViewportOverlayProjection::Perspective),
        near_plane(std::max(0.0001, static_cast<double>(camera.near_plane))),
        eye(perspective ? glm::dvec3(to_glm(camera.eye)) : glm::dvec3(0)),
        forward(to_glm(camera.forward)) {
    if (glm::length(forward) < 0.000001) forward = {0, 0, -1};
    forward = glm::normalize(forward);
    const glm::dvec3 reference = std::abs(forward.y) < 0.9
        ? glm::dvec3(0, 1, 0) : glm::dvec3(1, 0, 0);
    right = glm::normalize(glm::cross(forward, reference));
    up = glm::cross(right, forward);
  }

  glm::dvec3 view(QVec3 point) const {
    const glm::dvec3 offset = glm::dvec3(to_glm(point)) - eye;
    return {glm::dot(offset, right), glm::dot(offset, up), glm::dot(offset, forward)};
  }
  glm::dvec2 project(glm::dvec3 point) const {
    return glm::dvec2(point) / (perspective ? point.z : 1.0);
  }
};

/// Clips silhouette candidates against the union of projected selected triangles.
/// The shared bounds BVH limits coverage queries to triangles crossed by each candidate.
struct OutlineCoverage {
  /// A counterclockwise projected triangle, including near-plane-clipped triangles.
  struct Triangle { std::array<glm::dvec2, 3> points; };
  /// A covered interval along a projected candidate segment.
  struct Interval { double begin, end; };
  std::vector<Triangle> triangles;
  std::vector<quader_geometry::QAabb3<double>> bounds;
  std::vector<quader_geometry::QVec3d> centers;
  std::vector<std::size_t> indices;
  quader_geometry::QBoundsBvh<double> bvh;
  quader_geometry::QBvhTraversalScratch scratch;
  std::vector<Interval> covered;

  void clear() {
    triangles.clear();
    bounds.clear();
    centers.clear();
    indices.clear();
  }

  void append_triangle(const std::array<glm::dvec3, 3> &points,
                       const OutlineProjection &projection) {
    std::array<glm::dvec3, 4> clipped;
    std::size_t count = 0;
    for (std::size_t i = 0; i < 3; ++i) {
      const auto a = points[i], b = points[(i + 1) % 3];
      const bool a_inside = !projection.perspective || a.z >= projection.near_plane;
      const bool b_inside = !projection.perspective || b.z >= projection.near_plane;
      if (a_inside) clipped[count++] = a;
      if (a_inside != b_inside) {
        clipped[count++] = glm::mix(a, b, (projection.near_plane - a.z) / (b.z - a.z));
      }
    }
    for (std::size_t i = 1; i + 1 < count; ++i) {
      Triangle triangle{{projection.project(clipped[0]), projection.project(clipped[i]),
                         projection.project(clipped[i + 1])}};
      const double area = cross_2d(triangle.points[1] - triangle.points[0],
                                   triangle.points[2] - triangle.points[0]);
      if (!std::isfinite(area) || std::abs(area) < 1e-18) continue;
      if (area < 0) std::swap(triangle.points[1], triangle.points[2]);
      glm::dvec2 minimum = triangle.points[0], maximum = minimum;
      for (const auto point : triangle.points) {
        minimum = glm::min(minimum, point);
        maximum = glm::max(maximum, point);
      }
      const double tolerance = std::max({1.0, glm::length(minimum), glm::length(maximum)}) * 1e-10;
      indices.push_back(triangles.size());
      triangles.push_back(triangle);
      bounds.push_back({{minimum.x - tolerance, minimum.y - tolerance, -1},
                        {maximum.x + tolerance, maximum.y + tolerance, 1}});
      centers.push_back({(minimum.x + maximum.x) * 0.5, (minimum.y + maximum.y) * 0.5, 0});
    }
  }

  void build() { (void)bvh.build(bounds, centers, indices); }

  /// Retains the exact interval union so traversal can stop once no outline remains.
  bool add_covered_interval(Interval interval) {
    if (interval.begin == 0 && interval.end == 1) {
      covered.assign(1, interval);
      return false;
    }
    const auto first = std::lower_bound(covered.begin(), covered.end(), interval.begin,
        [](const Interval &existing, double begin) { return existing.end < begin; });
    auto last = first;
    while (last != covered.end() && last->begin <= interval.end) {
      interval.begin = std::min(interval.begin, last->begin);
      interval.end = std::max(interval.end, last->end);
      ++last;
    }
    if (first == last) {
      covered.insert(first, interval);
    } else {
      *first = interval;
      covered.erase(first + 1, last);
    }
    return covered.size() != 1 || covered.front().begin != 0 || covered.front().end != 1;
  }

  void find_covered(glm::dvec2 start, glm::dvec2 end, glm::dvec2 outside) {
    covered.clear();
    const glm::dvec2 direction = end - start;
    const double tolerance = std::max({1.0, glm::length(start), glm::length(end)}) * 1e-10;
    (void)bvh.for_each_ray_candidate(
        {{start.x, start.y, 0}, {direction.x, direction.y, 0}},
        {.min_distance = 0, .max_distance = 1, .intersection_epsilon = 1e-14}, scratch,
        [&](std::size_t index, std::size_t, const auto &, double &) {
          Interval interval{0, 1};
          const auto &triangle = triangles[index];
          for (std::size_t i = 0; i < 3; ++i) {
            const auto a = triangle.points[i];
            const auto side = triangle.points[(i + 1) % 3] - a;
            const double side_length = glm::length(side);
            if (side_length <= tolerance) return true;
            double from = cross_2d(side, start - a) / side_length;
            double to = cross_2d(side, end - a) / side_length;
            if (std::abs(from) <= tolerance) from = 0;
            if (std::abs(to) <= tolerance) to = 0;
            if (from == 0 && to == 0) {
              // Coincident boundaries cancel only when this triangle occupies
              // the candidate's exterior. Same-side boundaries remain outlines.
              if (cross_2d(side, outside) <= 0) return true;
            } else if (from < 0 && to < 0) {
              return true;
            } else if (from < 0) {
              interval.begin = std::max(interval.begin, from / (from - to));
            } else if (to < 0) {
              interval.end = std::min(interval.end, from / (from - to));
            }
            if (interval.begin >= interval.end - 1e-10) return true;
          }
          return add_covered_interval(interval);
        });
  }
};

void append_union_outline(ViewportOverlayFrame &frame, OutlineCoverage &coverage,
                          const OutlineProjection &projection,
                          QVec3 start, QVec3 end, const std::array<QVec3, 2> &inside,
                          ViewportOverlayStyleRole style_role) {
  glm::dvec3 view_start = projection.view(start), view_end = projection.view(end);
  if (projection.perspective) {
    const bool start_inside = view_start.z >= projection.near_plane;
    const bool end_inside = view_end.z >= projection.near_plane;
    if (!start_inside && !end_inside) return;
    if (start_inside != end_inside) {
      const double t = (projection.near_plane - view_start.z) / (view_end.z - view_start.z);
      const auto clipped_view = glm::mix(view_start, view_end, t);
      const QVec3 clipped_world = start + (end - start) * static_cast<float>(t);
      if (!start_inside) { view_start = clipped_view; start = clipped_world; }
      else { view_end = clipped_view; end = clipped_world; }
    }
  }
  const auto a = projection.project(view_start), b = projection.project(view_end);
  const auto direction = b - a;
  if (glm::length(direction) < 1e-12) return;
  double inside_side = 0;
  for (const auto point : inside) {
    const auto view_inside = projection.view(point);
    inside_side = cross_2d(direction, glm::dvec2(view_inside) -
        a * (projection.perspective ? view_inside.z : 1.0));
    if (std::abs(inside_side) > 1e-15) break;
  }
  if (std::abs(inside_side) <= 1e-15) return;
  const auto outside = glm::normalize(glm::dvec2(-direction.y, direction.x)) *
      (inside_side > 0 ? -1.0 : 1.0);
  coverage.find_covered(a, b, outside);
  const auto world_point = [&](double t) {
    if (projection.perspective) {
      t = t * view_start.z / ((1 - t) * view_end.z + t * view_start.z);
    }
    return start + (end - start) * static_cast<float>(t);
  };
  const auto append = [&](double begin, double finish) {
    if (finish <= begin + 1e-9) return;
    frame.lines.push_back({.layer = ViewportOverlayLayer::SelectedEdge,
        .start = world_point(begin), .end = world_point(finish), .color = kOverlaySelectionWireColor,
        .width_pixels = 0, .style_role = style_role,
        .source = ViewportOverlaySource::MeshOutline});
  };
  double begin = 0;
  for (const auto interval : coverage.covered) {
    append(begin, interval.begin);
    begin = std::max(begin, interval.end);
  }
  append(begin, 1);
}

/// Removes ordinary fragments coincident with active edges, including partial overlaps.
/// World-space tests preserve the existing depth relationship of projected overlaps.
struct ActiveOutlineMerge {
  std::vector<quader_geometry::QAabb3<double>> bounds;
  std::vector<quader_geometry::QVec3d> centers;
  std::vector<std::size_t> indices;
  quader_geometry::QBoundsBvh<double> bvh;
  quader_geometry::QBvhTraversalScratch scratch;
  std::vector<OutlineCoverage::Interval> covered;
  std::vector<ViewportOverlayLine> merged;

  void merge(std::vector<ViewportOverlayLine> &lines) {
    bounds.clear();
    centers.clear();
    indices.clear();
    const auto is_active = [](const auto &line) {
      return line.style_role == ViewportOverlayStyleRole::ActiveSelectionWire;
    };
    const auto active_count = std::count_if(lines.begin(), lines.end(), is_active);
    if (active_count == 0 || static_cast<std::size_t>(active_count) == lines.size()) return;
    // The BVH keeps the caller's source-line index alongside each compact bound.
    for (std::size_t i = 0; i < lines.size(); ++i) {
      const auto &line = lines[i];
      if (!is_active(line)) continue;
      const glm::dvec3 a(to_glm(line.start)), b(to_glm(line.end));
      const double tolerance = std::max(1.0, glm::length(b - a)) * 1e-7;
      const auto low = glm::min(a, b) - tolerance, high = glm::max(a, b) + tolerance;
      const auto center = (a + b) * 0.5;
      bounds.push_back({{low.x, low.y, low.z}, {high.x, high.y, high.z}});
      centers.push_back({center.x, center.y, center.z});
      indices.push_back(i);
    }
    (void)bvh.build(bounds, centers, indices);
    merged.clear();
    for (const auto &line : lines) {
      if (is_active(line)) { merged.push_back(line); continue; }
      const glm::dvec3 start(to_glm(line.start)), end(to_glm(line.end));
      const auto direction = end - start;
      const double squared_length = glm::dot(direction, direction);
      if (squared_length <= 0) continue;
      const double tolerance = std::max(1.0, std::sqrt(squared_length)) * 1e-7;
      covered.clear();
      (void)bvh.for_each_ray_candidate(
          {{start.x, start.y, start.z}, {direction.x, direction.y, direction.z}},
          {.min_distance = 0, .max_distance = 1, .intersection_epsilon = 1e-14}, scratch,
          [&](std::size_t index, std::size_t, const auto &, double &) {
            const auto &active = lines[index];
            const glm::dvec3 a = glm::dvec3(to_glm(active.start)) - start;
            const glm::dvec3 b = glm::dvec3(to_glm(active.end)) - start;
            const double from = glm::dot(a, direction) / squared_length;
            const double to = glm::dot(b, direction) / squared_length;
            if (glm::length(a - from * direction) > tolerance ||
                glm::length(b - to * direction) > tolerance) return true;
            const double begin = std::max(0.0, std::min(from, to));
            const double finish = std::min(1.0, std::max(from, to));
            if (finish > begin + 1e-9) covered.push_back({begin, finish});
            return true;
          });
      std::sort(covered.begin(), covered.end(), [](const auto &a, const auto &b) {
        return a.begin < b.begin;
      });
      const auto append = [&](double begin, double finish) {
        if (finish <= begin + 1e-9) return;
        auto fragment = line;
        const QVec3 delta = line.end - line.start;
        fragment.start = line.start + delta * static_cast<float>(begin);
        fragment.end = line.start + delta * static_cast<float>(finish);
        merged.push_back(fragment);
      };
      double begin = 0;
      for (const auto interval : covered) {
        append(begin, interval.begin);
        begin = std::max(begin, interval.end);
      }
      append(begin, 1);
    }
    lines.swap(merged);
  }
};

} // namespace

/// Pins selected occurrences and their shared topology, with an independent view gate per pane.
struct SelectedMeshOutlineCache::Impl {
  /// Couples immutable topology to an occurrence's local/world placement.
  struct Occurrence {
    std::shared_ptr<OutlineTopology> topology;
    QMat4 world, local;
    bool active_selected = false;
    std::array<QVec3, 8> bounds;
  };
  /// Framing changes need silhouette classification only when visible membership changes.
  struct View {
    ViewportOverlayProjection projection = ViewportOverlayProjection::Perspective;
    std::array<float, 3> direction_or_eye{};
    std::array<float, 3> forward{};
    float near_plane = 0;
    friend bool operator==(const View &, const View &) = default;
  };
  /// Retains output and scratch capacity until the source or relevant camera facts change.
  struct Pane {
    View view;
    std::array<float, 11> framing{};
    std::uint64_t source_revision = 0, revision = 0;
    bool valid = false;
    ViewportOverlayFrame frame, next;
    std::vector<std::uint8_t> facing;
    std::vector<std::size_t> visible_occurrences, next_visible_occurrences;
    std::array<OutlineCoverage, 2> coverage;
    ActiveOutlineMerge active_merge;
  };
  const RenderOverlaySelectedObjectSnapshot *source = nullptr;
  const RenderOverlaySelectionSnapshot *hierarchy_source = nullptr;
  RenderSourceStamp stamp;
  RenderSourceStamp hierarchy_stamp;
  bool source_valid = false, visible = false;
  std::uint64_t source_revision = 0;
  std::unordered_map<const MeshGeometryPayload *, std::shared_ptr<OutlineTopology>> topologies;
  std::vector<Occurrence> occurrences;
  std::array<Pane, 4> panes;
  SelectedMeshOutlineWorkStats stats;

  void prepare(const RenderOverlaySelectedObjectSnapshot *snapshot,
               const RenderOverlaySelectionSnapshot *hierarchy) {
    const RenderSourceStamp next_stamp = snapshot ? static_cast<const RenderSourceStamp &>(*snapshot)
                                                : RenderSourceStamp{};
    const RenderSourceStamp next_hierarchy_stamp = hierarchy
        ? static_cast<const RenderSourceStamp &>(*hierarchy) : RenderSourceStamp{};
    const bool next_visible = snapshot && snapshot->visible;
    if (source_valid && source == snapshot && stamp == next_stamp && visible == next_visible &&
        hierarchy_source == hierarchy && hierarchy_stamp == next_hierarchy_stamp) return;
    const bool same_scene = source_valid && source == snapshot && stamp.scene_binding == next_stamp.scene_binding &&
        hierarchy_stamp.scene_binding == next_hierarchy_stamp.scene_binding;
    if (!same_scene) topologies.clear();
    std::unordered_map<const MeshGeometryPayload *, std::shared_ptr<OutlineTopology>> retained;
    occurrences.clear();
    const auto append = [&](const auto &mesh) {
      if (mesh.vertices.empty() || mesh.indices.size() < 3) return;
      auto &topology = retained[mesh.geometry_payload.get()];
      if (!topology || !mesh.geometry_payload) {
        const auto found = topologies.find(mesh.geometry_payload.get());
        if (mesh.geometry_payload && found != topologies.end()) {
          topology = found->second;
        } else {
          topology = build_topology(mesh.geometry_payload, mesh.vertices, mesh.indices);
          ++stats.topology_builds;
        }
      }
      occurrences.push_back({topology, mesh.world_transform, inverse(mesh.world_transform),
                             mesh.active_selected});
      auto &occurrence = occurrences.back();
      for (std::size_t index = 0; index < topology->bounds.size(); ++index) {
        occurrence.bounds[index] = transform_point(mesh.world_transform, topology->bounds[index]);
      }
    };
    if (next_visible) {
      for (const auto &mesh : snapshot->meshes) append(mesh);
      for (const auto &mesh : snapshot->instance_selection_fills) append(mesh);
    }
    if (hierarchy) {
      for (const auto &mesh : hierarchy->hierarchy_selection_wires) append(mesh);
    }
    topologies = std::move(retained);
    source = snapshot;
    stamp = next_stamp;
    hierarchy_source = hierarchy;
    hierarchy_stamp = next_hierarchy_stamp;
    visible = next_visible;
    source_valid = true;
    ++source_revision;
    ++stats.source_refreshes;
  }
};

SelectedMeshOutlineCache::SelectedMeshOutlineCache() : impl_(std::make_unique<Impl>()) {}
SelectedMeshOutlineCache::~SelectedMeshOutlineCache() = default;
SelectedMeshOutlineCache::SelectedMeshOutlineCache(SelectedMeshOutlineCache &&) noexcept = default;
SelectedMeshOutlineCache &SelectedMeshOutlineCache::operator=(SelectedMeshOutlineCache &&) noexcept = default;

const ViewportOverlayFrame &SelectedMeshOutlineCache::frame(
    const RenderOverlaySelectedObjectSnapshot *snapshot,
    const ViewportOverlayCamera &camera, std::size_t pane_index,
    const RenderOverlaySelectionSnapshot *hierarchy, float outline_width_pixels) {
  impl_->prepare(snapshot, hierarchy);
  auto &pane = impl_->panes.at(pane_index);
  const Impl::View view{camera.projection, position_key(
      camera.projection == ViewportOverlayProjection::Orthographic ? camera.forward : camera.eye),
      position_key(camera.forward),
      camera.projection == ViewportOverlayProjection::Perspective ? camera.near_plane : 0};
  const std::array<float, 11> framing{camera.eye.x, camera.eye.y, camera.eye.z,
      camera.right.x, camera.right.y, camera.right.z, camera.viewport_size.x,
      camera.viewport_size.y, camera.orthographic_size, camera.fov_degrees, outline_width_pixels};
  const bool same_view = pane.valid && pane.source_revision == impl_->source_revision && pane.view == view;
  if (same_view && pane.framing == framing) return pane.frame;
  const OutlineFrustum frustum(camera, outline_width_pixels);
  pane.next_visible_occurrences.clear();
  for (std::size_t index = 0; index < impl_->occurrences.size(); ++index) {
    if (frustum.intersects(impl_->occurrences[index].bounds)) pane.next_visible_occurrences.push_back(index);
  }
  ++impl_->stats.visibility_classifications;
  impl_->stats.occurrence_bounds_tests += impl_->occurrences.size();
  pane.framing = framing;
  if (same_view && pane.visible_occurrences == pane.next_visible_occurrences) return pane.frame;
  pane.visible_occurrences.swap(pane.next_visible_occurrences);
  pane.next.lines.clear();
  const OutlineProjection projection(camera);
  for (auto &coverage : pane.coverage) coverage.clear();
  for (const auto index : pane.visible_occurrences) {
    const auto &occurrence = impl_->occurrences[index];
    auto &coverage = pane.coverage[occurrence.active_selected ? 1 : 0];
    for (const auto &face : occurrence.topology->faces) {
      std::array<glm::dvec3, 3> points;
      for (std::size_t i = 0; i < points.size(); ++i) {
        points[i] = projection.view(transform_point(occurrence.world, face.points[i]));
      }
      coverage.append_triangle(points, projection);
    }
  }
  for (auto &coverage : pane.coverage) coverage.build();
  for (const auto occurrence_index : pane.visible_occurrences) {
    const auto &occurrence = impl_->occurrences[occurrence_index];
    const QVec3 local_view = camera.projection == ViewportOverlayProjection::Orthographic
        ? transform_vector(occurrence.local, camera.forward * -1.0f)
        : transform_point(occurrence.local, camera.eye);
    if (!finite(local_view)) continue;
    const auto &topology = *occurrence.topology;
    pane.facing.assign(topology.edges.size(), 0);
    for (const auto &face : topology.faces) {
      const QVec3 direction = camera.projection == ViewportOverlayProjection::Orthographic
          ? local_view : local_view - face.center;
      const float side = dot(face.normal, direction);
      const float tolerance = length(direction) * 0.000001f;
      const std::uint8_t bit = side > tolerance ? 1 : side < -tolerance ? 2 : 4;
      for (const auto edge : face.edges) pane.facing[edge] |= bit;
    }
    for (std::size_t index = 0; index < topology.edges.size(); ++index) {
      const auto &edge = topology.edges[index];
      const auto sides = pane.facing[index];
      if (edge.count != 1 && sides != 3 && sides != 5 && sides != 6 && sides != 7) continue;
      const QVec3 start = transform_point(occurrence.world, edge.start);
      const QVec3 end = transform_point(occurrence.world, edge.end);
      if (!finite(start) || !finite(end)) continue;
      const std::array<QVec3, 2> inside{
          transform_point(occurrence.world, edge.inside[0]),
          transform_point(occurrence.world, edge.inside[edge.count > 1 ? 1 : 0])};
      append_union_outline(pane.next, pane.coverage[occurrence.active_selected ? 1 : 0],
          projection, start, end, inside, occurrence.active_selected
              ? ViewportOverlayStyleRole::ActiveSelectionWire
              : ViewportOverlayStyleRole::SelectionWire);
    }
  }
  pane.active_merge.merge(pane.next.lines);
  for (auto &line : pane.next.lines) {
    if (position_key(line.end) < position_key(line.start)) std::swap(line.start, line.end);
  }
  const auto line_key = [](const ViewportOverlayLine &line) {
    return std::array<float, 6>{line.start.x, line.start.y, line.start.z,
                               line.end.x, line.end.y, line.end.z};
  };
  std::sort(pane.next.lines.begin(), pane.next.lines.end(), [&](const auto &a, const auto &b) {
    if (line_key(a) == line_key(b)) {
      return a.style_role == ViewportOverlayStyleRole::ActiveSelectionWire &&
             b.style_role != ViewportOverlayStyleRole::ActiveSelectionWire;
    }
    return line_key(a) < line_key(b);
  });
  const auto duplicate_begin = std::unique(pane.next.lines.begin(), pane.next.lines.end(),
      [&](const auto &a, const auto &b) { return line_key(a) == line_key(b); });
  pane.next.lines.erase(duplicate_begin, pane.next.lines.end());
  if (!same_lines(pane.frame, pane.next)) {
    std::swap(pane.frame, pane.next);
    ++pane.revision;
  }
  pane.source_revision = impl_->source_revision;
  pane.view = view;
  pane.valid = true;
  ++impl_->stats.camera_classifications;
  return pane.frame;
}

std::uint64_t SelectedMeshOutlineCache::revision(std::size_t pane) const { return impl_->panes.at(pane).revision; }
SelectedMeshOutlineWorkStats SelectedMeshOutlineCache::work_stats() const { return impl_->stats; }
void SelectedMeshOutlineCache::reset() {
  impl_->source_valid = false;
  impl_->topologies.clear();
  impl_->occurrences.clear();
  for (auto &pane : impl_->panes) pane.valid = false;
}

} // namespace quader

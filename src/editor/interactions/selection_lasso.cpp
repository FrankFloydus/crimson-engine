////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/interactions/selection_lasso.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace quader_editor_core {
namespace {

constexpr double kTolerance = 1.0e-7;

double cross(quader::QVec2 a, quader::QVec2 b) {
  return static_cast<double>(a.x) * b.y - static_cast<double>(a.y) * b.x;
}

quader::QVec2 at(quader::QVec2 a, quader::QVec2 b, double t) {
  return {static_cast<float>(std::lerp(static_cast<double>(a.x), b.x, t)),
          static_cast<float>(std::lerp(static_cast<double>(a.y), b.y, t))};
}

bool on_segment(quader::QVec2 p, quader::QVec2 a, quader::QVec2 b) {
  const double scale = std::max(1.0, std::hypot(
      static_cast<double>(b.x) - a.x, static_cast<double>(b.y) - a.y));
  return std::abs(cross(b - a, p - a)) <= kTolerance * scale &&
         p.x >= std::min(a.x, b.x) - kTolerance &&
         p.x <= std::max(a.x, b.x) + kTolerance &&
         p.y >= std::min(a.y, b.y) - kTolerance &&
         p.y <= std::max(a.y, b.y) + kTolerance;
}

bool contains(std::span<const quader::QVec2> polygon, quader::QVec2 p) {
  bool inside = false;
  for (std::size_t i = 0; i < polygon.size(); ++i) {
    const auto a = polygon[i];
    const auto b = polygon[(i + 1) % polygon.size()];
    if (on_segment(p, a, b)) {
      return true;
    }
    if ((a.y > p.y) != (b.y > p.y) &&
        p.x < static_cast<double>(b.x - a.x) * (p.y - a.y) /
                      (b.y - a.y) + a.x) {
      inside = !inside;
    }
  }
  return inside;
}

void intersection_parameters(quader::QVec2 a, quader::QVec2 b,
                             quader::QVec2 c, quader::QVec2 d,
                             std::vector<double> &parameters) {
  const auto ab = b - a;
  const auto cd = d - c;
  const double denominator = cross(ab, cd);
  if (std::abs(denominator) > kTolerance) {
    const double t = cross(c - a, cd) / denominator;
    const double u = cross(c - a, ab) / denominator;
    if (t >= -kTolerance && t <= 1.0 + kTolerance &&
        u >= -kTolerance && u <= 1.0 + kTolerance) {
      parameters.push_back(std::clamp(t, 0.0, 1.0));
    }
    return;
  }
  if (std::abs(cross(c - a, ab)) > kTolerance) {
    return;
  }
  const double squared_length =
      static_cast<double>(ab.x) * ab.x + static_cast<double>(ab.y) * ab.y;
  if (squared_length <= kTolerance * kTolerance) {
    if (on_segment(a, c, d)) {
      parameters.push_back(0.0);
    }
    return;
  }
  for (const auto point : {c, d}) {
    const double t = (static_cast<double>(point.x - a.x) * ab.x +
                      static_cast<double>(point.y - a.y) * ab.y) /
                     squared_length;
    if (t >= -kTolerance && t <= 1.0 + kTolerance) {
      parameters.push_back(std::clamp(t, 0.0, 1.0));
    }
  }
}

void sort_unique(std::vector<double> &values) {
  std::sort(values.begin(), values.end());
  values.erase(std::unique(values.begin(), values.end(),
                           [](double a, double b) {
                             return std::abs(a - b) <= kTolerance;
                           }), values.end());
}

} // namespace

SelectionLassoRegion::SelectionLassoRegion(
    std::span<const quader::QVec2> points) {
  points_.reserve(points.size());
  for (const auto point : points) {
    if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
      continue;
    }
    if (points_.empty() || point.x != points_.back().x ||
        point.y != points_.back().y) {
      points_.push_back(point);
    }
  }
  if (points_.size() < 3) {
    return;
  }
  // Every even-odd arrangement cell spans a strip between vertex/intersection
  // heights. One witness in every finite exterior gap detects holes completely
  // enclosed by a candidate whose own boundary never leaves the lasso.
  std::vector<double> heights;
  heights.reserve(points_.size());
  std::vector<double> intersections;
  intersections.reserve(2);
  for (std::size_t i = 0; i < points_.size(); ++i) {
    heights.push_back(points_[i].y);
    for (std::size_t j = i + 1; j < points_.size(); ++j) {
      intersections.clear();
      intersection_parameters(points_[i], points_[(i + 1) % points_.size()],
                              points_[j], points_[(j + 1) % points_.size()],
                              intersections);
      for (double t : intersections) {
        heights.push_back(std::lerp(static_cast<double>(points_[i].y),
            points_[(i + 1) % points_.size()].y, t));
      }
    }
  }
  sort_unique(heights);
  for (std::size_t strip = 1; strip < heights.size(); ++strip) {
    const double y = std::midpoint(heights[strip - 1], heights[strip]);
    std::vector<double> crossings;
    for (std::size_t i = 0; i < points_.size(); ++i) {
      const auto a = points_[i];
      const auto b = points_[(i + 1) % points_.size()];
      if ((a.y > y) != (b.y > y)) {
        crossings.push_back(a.x + (y - a.y) * (b.x - a.x) / (b.y - a.y));
      }
    }
    std::sort(crossings.begin(), crossings.end());
    for (std::size_t i = 1; i < crossings.size(); i += 2) {
      has_interior_ = has_interior_ || crossings[i] - crossings[i - 1] > kTolerance;
    }
    for (std::size_t i = 2; i < crossings.size(); i += 2) {
      if (crossings[i] - crossings[i - 1] > kTolerance) {
        exterior_witnesses_.push_back({
            static_cast<float>(std::midpoint(crossings[i - 1], crossings[i])),
            static_cast<float>(y)});
      }
    }
  }
}

bool SelectionLassoRegion::valid() const noexcept { return has_interior_; }

bool SelectionLassoRegion::contains_point(quader::QVec2 point) const {
  return valid() && contains(points_, point);
}

bool SelectionLassoRegion::contains_segment(quader::QVec2 start,
                                            quader::QVec2 end) const {
  if (!contains_point(start) || !contains_point(end)) {
    return false;
  }
  std::vector<double> parameters{0.0, 1.0};
  for (std::size_t i = 0; i < points_.size(); ++i) {
    intersection_parameters(start, end, points_[i],
                            points_[(i + 1) % points_.size()], parameters);
  }
  sort_unique(parameters);
  for (std::size_t i = 1; i < parameters.size(); ++i) {
    if (!contains_point(at(start, end,
                            std::midpoint(parameters[i - 1], parameters[i])))) {
      return false;
    }
  }
  return true;
}

bool SelectionLassoRegion::intersects_segment(quader::QVec2 start,
                                              quader::QVec2 end) const {
  if (!valid()) {
    return false;
  }
  if (contains_point(start) || contains_point(end)) {
    return true;
  }
  std::vector<double> parameters;
  for (std::size_t i = 0; i < points_.size(); ++i) {
    intersection_parameters(start, end, points_[i],
                            points_[(i + 1) % points_.size()], parameters);
    if (!parameters.empty()) {
      return true;
    }
  }
  return false;
}

bool SelectionLassoRegion::contains_polygon(
    std::span<const quader::QVec2> polygon) const {
  if (polygon.empty() || !valid()) {
    return false;
  }
  for (std::size_t i = 0; i < polygon.size(); ++i) {
    if (!contains_segment(polygon[i], polygon[(i + 1) % polygon.size()])) {
      return false;
    }
  }
  return std::none_of(exterior_witnesses_.begin(), exterior_witnesses_.end(),
                      [&](quader::QVec2 point) { return contains(polygon, point); });
}

bool SelectionLassoRegion::intersects_polygon(
    std::span<const quader::QVec2> polygon) const {
  if (polygon.empty() || !valid()) {
    return false;
  }
  for (std::size_t i = 0; i < polygon.size(); ++i) {
    if (intersects_segment(polygon[i], polygon[(i + 1) % polygon.size()])) {
      return true;
    }
  }
  return std::any_of(points_.begin(), points_.end(),
                     [&](quader::QVec2 point) { return contains(polygon, point); });
}

bool SelectionLassoRegion::matches_object(
    const RectangleProjectedObject &object, bool partial_intersection) const {
  if (!partial_intersection && !object.complete_geometry) {
    return false;
  }
  bool has_geometry = false;
  for (const auto point : object.screen_points) {
    has_geometry = true;
    if (contains_point(point) == partial_intersection) {
      return partial_intersection;
    }
  }
  for (const auto &segment : object.screen_segments) {
    has_geometry = true;
    const bool matches = partial_intersection
        ? intersects_segment(segment.start, segment.end)
        : contains_segment(segment.start, segment.end);
    if (matches == partial_intersection) {
      return partial_intersection;
    }
  }
  for (const auto &polygon : object.screen_polygons) {
    has_geometry = true;
    const bool matches = partial_intersection ? intersects_polygon(polygon)
                                               : contains_polygon(polygon);
    if (matches == partial_intersection) {
      return partial_intersection;
    }
  }
  if (has_geometry) {
    return !partial_intersection;
  }
  if (object.has_screen_bounds) {
    const quader::QVec2 bounds[]{object.screen_min,
        {object.screen_max.x, object.screen_min.y}, object.screen_max,
        {object.screen_min.x, object.screen_max.y}};
    return partial_intersection ? intersects_polygon(bounds)
                                : contains_polygon(bounds);
  }
  return object.has_screen_center && contains_point(object.screen_center);
}

RectangleSelectionHits lasso_hits_from_projected_candidates(
    const RectangleProjectedHitsRequest &candidates,
    const SelectionLassoRegion &region, bool partial_intersection) {
  RectangleSelectionHits hits;
  if (candidates.mode == SelectionMode::Object) {
    for (const auto &object : candidates.objects) {
      if (object.object_id >= 0 && region.matches_object(object, partial_intersection)) {
        hits.object_ids.push_back(object.object_id);
      }
    }
  } else if (candidates.mode == SelectionMode::Face) {
    for (const auto &face : candidates.faces) {
      if (face.object_id >= 0 && face.face_index >= 0 &&
          (candidates.select_backfaces || face.front_facing) &&
          (partial_intersection || face.complete_geometry) &&
          (partial_intersection ? region.intersects_polygon(face.screen_vertices)
                                : region.contains_polygon(face.screen_vertices))) {
        hits.faces.push_back({face.object_id, face.face_index, {}});
      }
    }
  } else if (candidates.mode == SelectionMode::Edge) {
    for (const auto &edge : candidates.edges) {
      if (edge.object_id >= 0 && edge.a_index >= 0 && edge.b_index >= 0 &&
          (candidates.select_backfaces || edge.front_facing) &&
          (partial_intersection || edge.complete_geometry) &&
          edge.has_screen_start && edge.has_screen_end &&
          (partial_intersection
               ? region.intersects_segment(edge.screen_start, edge.screen_end)
               : region.contains_segment(edge.screen_start, edge.screen_end))) {
        hits.edges.push_back({edge.object_id, edge.a_index, edge.b_index});
      }
    }
  } else if (candidates.mode == SelectionMode::Vertex) {
    for (const auto &vertex : candidates.vertices) {
      if (vertex.object_id >= 0 && vertex.vertex_index >= 0 &&
          (candidates.select_backfaces || vertex.front_facing) &&
          vertex.has_screen_position && region.contains_point(vertex.screen_position)) {
        hits.vertices.push_back({vertex.object_id, vertex.vertex_index});
      }
    }
  }
  return hits;
}

std::vector<quader::QVec2> project_lasso_polygon(
    const ViewportCameraState &camera, std::span<const quader::QVec3> vertices,
    double near_plane, double far_plane) {
  const auto forward = quader::normalize(camera.transform.z_axis * -1.0f);
  const auto depth = [&](quader::QVec3 point) {
    return static_cast<double>(quader::dot(point - camera.transform.origin, forward));
  };
  std::vector<quader::QVec3> clipped(vertices.begin(), vertices.end());
  for (int plane = 0; plane != 2 && !clipped.empty(); ++plane) {
    const double limit = plane == 0 ? std::max(near_plane, 1.0e-6) : far_plane;
    std::vector<quader::QVec3> next;
    for (std::size_t i = 0; i < clipped.size(); ++i) {
      const auto a = clipped[i];
      const auto b = clipped[(i + 1) % clipped.size()];
      const double da = depth(a);
      const double db = depth(b);
      const bool inside_a = plane == 0 ? da >= limit : da <= limit;
      const bool inside_b = plane == 0 ? db >= limit : db <= limit;
      if (inside_a) {
        next.push_back(a);
      }
      if (inside_a != inside_b) {
        next.push_back(a + (b - a) * static_cast<float>((limit - da) / (db - da)));
      }
    }
    clipped = std::move(next);
  }
  std::vector<quader::QVec2> result;
  result.reserve(clipped.size());
  for (const auto world : clipped) {
    quader::QVec2 screen;
    if (!project_viewport_point(camera, world, screen)) {
      return {};
    }
    result.push_back(screen);
  }
  return result;
}

} // namespace quader_editor_core

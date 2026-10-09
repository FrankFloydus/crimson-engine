////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/neutral/geometry/geometry.hpp>
#include <quader/modeling/neutral/geometry/polygon_triangulation.hpp>

#include <mapbox/earcut.hpp>

#include <map>
#include <set>

namespace quader_geometry::detail {

[[nodiscard]] double triangle_projected_area_abs(QVec2d a, QVec2d b,
                                                 QVec2d c) {
  const std::array points{a, b, c};
  return std::abs(
      polygon_signed_area<double>(std::span<const QVec2d>(points)));
}

} // namespace quader_geometry::detail

namespace quader_geometry {

QClosestSegmentPoint2<double>
closest_point_on_segment(QVec2d point, QVec2d segment_a, QVec2d segment_b,
                         double epsilon) {
  return closest_point_on_segment<double>(point, segment_a, segment_b,
                                          epsilon);
}

double point_segment_distance_squared(QVec2d point, QVec2d segment_a,
                                      QVec2d segment_b, double epsilon) {
  return point_segment_distance_squared<double>(point, segment_a, segment_b,
                                                epsilon);
}

QClosestSegmentPoint3<double>
closest_point_on_segment(QVec3d point, const QSegment3<double> &segment,
                         double epsilon) {
  return closest_point_on_segment<double>(point, segment, epsilon);
}

double point_segment_distance_squared(QVec3d point,
                                      const QSegment3<double> &segment,
                                      double epsilon) {
  return point_segment_distance_squared<double>(point, segment, epsilon);
}

QClassifiedSegmentIntersection2<double>
intersect_segments_2d(QVec2d a, QVec2d b, QVec2d c, QVec2d d,
                      double epsilon) {
  return detail::classify_segments_2d_local<double>(a, b, c, d, epsilon);
}

std::vector<QTriangleIndices>
triangulate_projected_polygon(std::span<const QVec2d> points, double epsilon,
                              double coverage_tolerance_ratio) {
  if (points.size() < 3U) {
    return {};
  }

  const double polygon_area = std::abs(polygon_signed_area<double>(points));
  if (polygon_area <= epsilon) {
    return {};
  }

  using EarcutPoint = std::array<double, 2>;
  std::vector<std::vector<EarcutPoint>> polygon;
  polygon.emplace_back();
  polygon.front().reserve(points.size());
  for (const QVec2d point : points) {
    polygon.front().push_back({point.x, point.y});
  }

  const std::vector<std::uint32_t> indices =
      mapbox::earcut<std::uint32_t>(polygon);
  if (indices.size() < 3U || indices.size() % 3U != 0U) {
    return {};
  }

  std::vector<QTriangleIndices> triangles;
  triangles.reserve(indices.size() / 3U);
  double covered_area = 0.0;
  for (std::size_t index = 0; index < indices.size(); index += 3U) {
    const std::uint32_t a = indices[index];
    const std::uint32_t b = indices[index + 1U];
    const std::uint32_t c = indices[index + 2U];
    if (a >= points.size() || b >= points.size() || c >= points.size()) {
      return {};
    }
    if (a == b || a == c || b == c) {
      continue;
    }

    const double triangle_area =
        detail::triangle_projected_area_abs(points[a], points[b], points[c]);
    if (triangle_area <= epsilon) {
      continue;
    }

    triangles.push_back({a, b, c});
    covered_area += triangle_area;
  }

  const double coverage_tolerance =
      std::max(epsilon, polygon_area * std::max(0.0, coverage_tolerance_ratio));
  if (triangles.empty() || covered_area + coverage_tolerance < polygon_area) {
    return {};
  }

  return triangles;
}

std::vector<QTriangleIndices> triangulate_projected_polygon_rings(
    std::span<const std::vector<QVec2d>> rings, double epsilon,
    double coverage_tolerance_ratio) {
  if (rings.empty() || !std::isfinite(epsilon) || epsilon < 0.0 ||
      !std::isfinite(coverage_tolerance_ratio) || coverage_tolerance_ratio < 0.0) {
    return {};
  }
  using IndexEdge = std::pair<std::size_t, std::size_t>;
  const auto edge_key = [](std::size_t first, std::size_t second) {
    return IndexEdge{std::min(first, second), std::max(first, second)};
  };
  std::vector<QVec2d> points;
  std::vector<std::vector<std::array<double, 2>>> polygon;
  std::vector<IndexEdge> directed_boundary;
  std::set<IndexEdge> boundary;
  double expected_area = 0.0;
  for (std::size_t ring_index = 0; ring_index < rings.size(); ++ring_index) {
    const auto& ring = rings[ring_index];
    if (ring.size() < 3U) {
      return {};
    }
    const double area = std::abs(polygon_signed_area<double>(ring));
    if (!std::isfinite(area) || area <= epsilon) {
      return {};
    }
    expected_area += ring_index == 0U ? area : -area;
    if (ring_index != 0U && !point_in_or_on_polygon_2d<double>(ring.front(), rings.front(), epsilon)) {
      return {};
    }
    polygon.emplace_back();
    const std::size_t offset = points.size();
    for (std::size_t index = 0; index < ring.size(); ++index) {
      const auto point = ring[index];
      if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
          length_squared(point - ring[(index + 1U) % ring.size()]) <= epsilon * epsilon) {
        return {};
      }
      points.push_back(point);
      polygon.back().push_back({point.x, point.y});
      const auto first = offset + index;
      const auto second = offset + (index + 1U) % ring.size();
      directed_boundary.emplace_back(first, second);
      boundary.insert(edge_key(first, second));
    }
  }
  if (expected_area <= epsilon || points.size() > std::numeric_limits<std::uint32_t>::max()) {
    return {};
  }
  for (std::size_t first = 0; first < directed_boundary.size(); ++first) {
    const auto [a, b] = directed_boundary[first];
    for (std::size_t second = first + 1U; second < directed_boundary.size(); ++second) {
      const auto [c, d] = directed_boundary[second];
      if (a == c || a == d || b == c || b == d) {
        continue;
      }
      if (segments_intersect_2d<double>(points[a], points[b], points[c], points[d], epsilon)) {
        return {};
      }
    }
  }
  for (std::size_t first = 1U; first < rings.size(); ++first) {
    for (std::size_t second = 1U; second < rings.size(); ++second) {
      if (first != second && point_in_or_on_polygon_2d<double>(rings[first].front(), rings[second], epsilon)) {
        return {};
      }
    }
  }
  const auto indices = mapbox::earcut<std::uint32_t>(polygon);
  if (indices.empty() || indices.size() % 3U != 0U) {
    return {};
  }
  std::vector<QTriangleIndices> triangles;
  for (std::size_t index = 0; index < indices.size(); index += 3U) {
    const auto a = indices[index];
    const auto b = indices[index + 1U];
    const auto c = indices[index + 2U];
    if (a >= points.size() || b >= points.size() || c >= points.size()) {
      return {};
    }
    if (detail::triangle_projected_area_abs(points[a], points[b], points[c]) > epsilon) {
      triangles.push_back({a, b, c});
    }
  }
  const auto incidence = [&] {
    std::map<IndexEdge, unsigned> counts;
    for (const auto triangle : triangles) {
      ++counts[edge_key(triangle[0], triangle[1])];
      ++counts[edge_key(triangle[1], triangle[2])];
      ++counts[edge_key(triangle[2], triangle[0])];
    }
    return counts;
  };
  // Straight corners can be omitted from a boundary or a hole bridge even
  // when another triangle already uses that corner. Split every containing
  // triangle edge so both sides retain the same authored adjacency.
  for (std::size_t vertex = 0; vertex < points.size(); ++vertex) {
    bool represented = false;
    for (std::size_t index = 0; index < triangles.size(); ++index) {
      const auto triangle = triangles[index];
      if (triangle[0] == vertex || triangle[1] == vertex || triangle[2] == vertex) {
        represented = true;
        continue;
      }
      const auto corners = triangle;
      for (std::size_t edge = 0; edge < corners.size(); ++edge) {
        const auto first = corners[edge];
        const auto second = corners[(edge + 1U) % corners.size()];
        const auto opposite = corners[(edge + 2U) % corners.size()];
        if (point_on_segment_2d<double>(points[vertex], points[first], points[second], epsilon)) {
          const auto inserted = static_cast<std::uint32_t>(vertex);
          triangles[index] = {first, inserted, opposite};
          triangles.push_back({inserted, second, opposite});
          represented = true;
          break;
        }
      }
    }
    if (!represented) {
      return {};
    }
  }
  const auto counts = incidence();
  for (const auto edge : boundary) {
    const auto found = counts.find(edge);
    if (found == counts.end() || found->second != 1U) {
      return {};
    }
  }
  for (const auto& [edge, count] : counts) {
    if (count != (boundary.contains(edge) ? 1U : 2U)) {
      return {};
    }
  }
  double covered_area = 0.0;
  for (const auto triangle : triangles) {
    const auto a = points[triangle[0]];
    const auto b = points[triangle[1]];
    const auto c = points[triangle[2]];
    const double area = detail::triangle_projected_area_abs(a, b, c);
    if (area <= epsilon) {
      return {};
    }
    const auto center = (a + b + c) * (1.0 / 3.0);
    if (!point_in_or_on_polygon_2d<double>(center, rings.front(), epsilon)) {
      return {};
    }
    for (std::size_t hole = 1; hole < rings.size(); ++hole) {
      if (point_in_or_on_polygon_2d<double>(center, rings[hole], epsilon)) {
        return {};
      }
    }
    covered_area += area;
  }
  if (std::abs(covered_area - expected_area) >
      std::max(epsilon, expected_area * coverage_tolerance_ratio)) {
    return {};
  }
  return triangles;
}

QRayPlaneHit<float> intersect_ray_plane(const QRay3<float> &ray,
                                        const QPlane3<float> &plane,
                                        float epsilon) {
  const QRayPlaneHit<double> hit = intersect_ray_plane<double>(
      QRay3<double>{ray}, QPlane3<double>{plane},
      static_cast<double>(epsilon));
  return {
      hit.hit,
      static_cast<float>(hit.distance),
      QBasicVec3<float>{hit.point},
  };
}

QRayTriangleHit<double>
intersect_ray_triangle(const QRay3<double> &ray,
                       const QTriangle3<double> &triangle, double epsilon) {
  return intersect_ray_triangle<double>(ray, triangle, epsilon);
}

QRayAabbHit<double> intersect_ray_aabb(const QRay3<double> &ray,
                                       const QAabb3<double> &aabb,
                                       double epsilon) {
  return intersect_ray_aabb<double>(ray, aabb, epsilon);
}

QClosestRayPoint<double> closest_point_on_ray(QVec3d point,
                                              const QRay3<double> &ray,
                                              double epsilon) {
  return closest_point_on_ray<double>(point, ray, epsilon);
}

double point_ray_distance_squared(QVec3d point, const QRay3<double> &ray,
                                  double epsilon) {
  return point_ray_distance_squared<double>(point, ray, epsilon);
}

QSegmentRayClosestPoints<double>
closest_points_segment_ray(const QSegment3<double> &segment,
                           const QRay3<double> &ray, double epsilon) {
  return closest_points_segment_ray<double>(segment, ray, epsilon);
}

double segment_ray_distance_squared(const QSegment3<double> &segment,
                                    const QRay3<double> &ray,
                                    double epsilon) {
  return segment_ray_distance_squared<double>(segment, ray, epsilon);
}

QSegmentSegmentClosestPoints<double>
closest_points_segment_segment(const QSegment3<double> &first,
                               const QSegment3<double> &second,
                               double epsilon) {
  return closest_points_segment_segment<double>(first, second, epsilon);
}

double segment_segment_distance_squared(const QSegment3<double> &first,
                                        const QSegment3<double> &second,
                                        double epsilon) {
  return segment_segment_distance_squared<double>(first, second, epsilon);
}

QLineLineClosestPoints<double>
closest_points_line_line(const QLine3<double> &first,
                         const QLine3<double> &second, double epsilon) {
  return closest_points_line_line<double>(first, second, epsilon);
}

double line_line_distance_squared(const QLine3<double> &first,
                                  const QLine3<double> &second,
                                  double epsilon) {
  return line_line_distance_squared<double>(first, second, epsilon);
}

} // namespace quader_geometry

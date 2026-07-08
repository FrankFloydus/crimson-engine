////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/geometry/geometry.hpp>

#include <mapbox/earcut.hpp>

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

QRayPlaneHit<float> intersect_ray_plane(const QRay3<float> &ray,
                                        const QPlane3<float> &plane,
                                        float epsilon) {
  return intersect_ray_plane<float>(ray, plane, epsilon);
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

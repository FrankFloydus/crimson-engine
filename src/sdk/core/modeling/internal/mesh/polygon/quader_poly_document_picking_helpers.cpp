////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/internal/quader_poly_document_picking_helpers.hpp>

#include <mesh/polygon/internal/quader_poly_document_topology_helpers.hpp>

#include <mesh/geometry/geometry.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <exception>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace quader_poly::document_internal {

bool ray_intersects_triangle(
    const Ray& ray,
    const quader::QVec3& a,
    const quader::QVec3& b,
    const quader::QVec3& c,
    float& distance,
    quader::QVec3& position)
{
    const quader_geometry::QRay3<float> geometry_pick_ray = geometry_ray(ray);
    const quader_geometry::QTriangle3<float> triangle { geometry_vec3(a), geometry_vec3(b), geometry_vec3(c) };
    const float picking_epsilon = std::nextafter(kEpsilon, 0.0F);
    const quader_geometry::QRayBoundedTriangleQueryOptions<float> options {
        0.0F,
        std::numeric_limits<float>::infinity(),
        picking_epsilon,
    };
    const quader_geometry::QRayBoundedTriangleHit<float> hit = quader_geometry::intersect_ray_bounded_triangle<float>(
        geometry_pick_ray,
        triangle,
        quader_geometry::triangle_aabb(triangle),
        options);

    const quader_geometry::QRayTriangleHit<float> triangle_hit =
        hit.hit ? hit.triangle_hit : quader_geometry::intersect_ray_triangle<float>(geometry_pick_ray, triangle, picking_epsilon);
    if (!triangle_hit.hit) {
        return false;
    }

    distance = triangle_hit.distance;
    position = poly_vec3(triangle_hit.point);
    return true;
}

float point_ray_distance(const Ray& ray, const quader::QVec3& point, float& ray_distance)
{
    const quader::QVec3 offset = point - ray.origin;
    ray_distance = dot(offset, ray.direction);
    if (ray_distance < 0.0F) {
        return std::numeric_limits<float>::max();
    }

    const quader_geometry::QClosestRayPoint<float> closest =
        quader_geometry::closest_point_on_ray<float>(
            geometry_vec3(point), geometry_ray(ray), kEpsilon);
    if (!closest.valid) {
        return length(point - ray.origin);
    }
    ray_distance = closest.ray_distance;
    return std::sqrt(closest.distance_squared);
}

float segment_ray_distance(const Ray& ray, const quader::QVec3& a, const quader::QVec3& b, float& ray_distance, quader::QVec3& position)
{
    const quader::QVec3 segment = b - a;
    const float segment_length_squared = length_squared(segment);
    if (segment_length_squared <= kEpsilon) {
      position = a;
      return point_ray_distance(ray, a, ray_distance);
    }

    const quader_geometry::QSegmentRayClosestPoints<float> closest =
        quader_geometry::closest_points_segment_ray<float>(
            {geometry_vec3(a), geometry_vec3(b)}, geometry_ray(ray), kEpsilon);
    if (!closest.valid) {
        ray_distance = 0.0F;
        position = a;
        return length(ray.origin - a);
    }

    ray_distance = closest.ray_distance;
    position = poly_vec3(closest.segment_point);
    return std::sqrt(closest.distance_squared);
}

} // namespace quader_poly::document_internal

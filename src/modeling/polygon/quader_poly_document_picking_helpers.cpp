////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <modeling/polygon/internal/quader_poly_document_picking_helpers.h>

#include <modeling/polygon/internal/quader_poly_document_topology_helpers.h>

#include <geometry/geometry.h>

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
    const Vec3& a,
    const Vec3& b,
    const Vec3& c,
    float& distance,
    Vec3& position)
{
    const quader_geometry::Ray3<float> geometry_pick_ray = geometry_ray(ray);
    const quader_geometry::Triangle3<float> triangle { geometry_vec3(a), geometry_vec3(b), geometry_vec3(c) };
    const float picking_epsilon = std::nextafter(epsilon, 0.0F);
    const quader_geometry::RayBoundedTriangleQueryOptions<float> options {
        0.0F,
        std::numeric_limits<float>::infinity(),
        picking_epsilon,
    };
    const quader_geometry::RayBoundedTriangleHit<float> hit = quader_geometry::intersect_ray_bounded_triangle<float>(
        geometry_pick_ray,
        triangle,
        quader_geometry::triangle_aabb(triangle),
        options);

    const quader_geometry::RayTriangleHit<float> triangle_hit =
        hit.hit ? hit.triangle_hit : quader_geometry::intersect_ray_triangle<float>(geometry_pick_ray, triangle, picking_epsilon);
    if (!triangle_hit.hit) {
        return false;
    }

    distance = triangle_hit.distance;
    position = poly_vec3(triangle_hit.point);
    return true;
}

float point_ray_distance(const Ray& ray, const Vec3& point, float& ray_distance)
{
    const Vec3 offset = point - ray.origin;
    ray_distance = dot(offset, ray.direction);
    if (ray_distance < 0.0F) {
        return std::numeric_limits<float>::max();
    }

    const quader_geometry::ClosestRayPoint<float> closest =
        quader_geometry::closest_point_on_ray<float>(geometry_vec3(point), geometry_ray(ray), epsilon);
    if (!closest.valid) {
        return length(point - ray.origin);
    }
    ray_distance = closest.ray_distance;
    return std::sqrt(closest.distance_squared);
}

float segment_ray_distance(const Ray& ray, const Vec3& a, const Vec3& b, float& ray_distance, Vec3& position)
{
    const Vec3 segment = b - a;
    const float segment_length_squared = length_squared(segment);
    if (segment_length_squared <= epsilon) {
        position = a;
        return point_ray_distance(ray, a, ray_distance);
    }

    const quader_geometry::SegmentRayClosestPoints<float> closest =
        quader_geometry::closest_points_segment_ray<float>(
            { geometry_vec3(a), geometry_vec3(b) },
            geometry_ray(ray),
            epsilon);
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

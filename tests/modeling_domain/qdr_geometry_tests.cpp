////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <mesh/geometry/geometry.hpp>

#include <array>
#include <cmath>
#include <vector>

namespace {

bool nearly_equal(double left, double right, double epsilon = 0.0001)
{
	return std::abs(left - right) <= epsilon;
}

bool vec3_nearly_equal(quader_geometry::QVec3d left, quader_geometry::QVec3d right, double epsilon = 0.0001)
{
	return nearly_equal(left.x, right.x, epsilon) &&
		nearly_equal(left.y, right.y, epsilon) &&
		nearly_equal(left.z, right.z, epsilon);
}

bool vec2_nearly_equal(quader_geometry::QVec2d left, quader_geometry::QVec2d right, double epsilon = 0.0001)
{
	return nearly_equal(left.x, right.x, epsilon) &&
		nearly_equal(left.y, right.y, epsilon);
}

double triangle_area_abs(const std::vector<quader_geometry::QVec2d>& points, quader_geometry::QTriangleIndices triangle)
{
	const std::array triangle_points {
		points[triangle[0]],
		points[triangle[1]],
		points[triangle[2]],
	};
	return std::abs(quader_geometry::polygon_signed_area<double>(std::span<const quader_geometry::QVec2d>(triangle_points)));
}

void ray_plane_and_projection_are_stable()
{
	const quader_geometry::QPlane3<double> plane =
		quader_geometry::plane_from_point_normal(quader_geometry::QVec3d { 0.0, 2.0, 0.0 }, quader_geometry::QVec3d { 0.0, 4.0, 0.0 });
	const quader_geometry::QRay3<double> ray { { 8.0, 6.0, 24.0 }, { 0.0, -1.0, 0.0 } };
	const quader_geometry::QRayPlaneHit<double> hit = quader_geometry::intersect_ray_plane(ray, plane);

	require_test(hit.hit, "ray should intersect the normalized plane");
	require_test(nearly_equal(hit.distance, 4.0), "ray-plane distance should be measured along the ray");
	require_test(vec3_nearly_equal(hit.point, { 8.0, 2.0, 24.0 }), "ray-plane hit point should lie on the plane");
	require_test(vec3_nearly_equal(quader_geometry::project_point_to_plane({ 3.0, 9.0, -2.0 }, plane), { 3.0, 2.0, -2.0 }),
		"projecting a point to a plane should remove the normal offset");
        require_test(quader_geometry::plane_side({3.0, 2.0, -2.0}, plane) ==
                         quader_geometry::QPlaneSide::On,
                     "plane side helper should classify points within "
                     "tolerance as on-plane");
        require_test(quader_geometry::planes_nearly_equal(plane, { { 0.0, 2.0, 0.0 }, 4.0 }),
		"plane equality helper should normalize plane coefficients before comparing");
}

void plane_basis_helpers_are_stable()
{
	const quader_geometry::QPlaneBasis3<double> basis = quader_geometry::make_plane_basis<double>(
		{ 0.0, 2.0, 0.0 },
		{ 0.0, 10.0, 0.0 },
		{ 1.0, 0.00000001, 0.0 });

	require_test(basis.valid, "plane basis should accept a nonzero normal");
	require_test(nearly_equal(quader_geometry::length(basis.normal), 1.0), "plane basis normal should be normalized");
	require_test(nearly_equal(quader_geometry::dot(basis.normal, basis.u), 0.0), "plane basis u should lie on the plane");
	require_test(nearly_equal(quader_geometry::dot(basis.normal, basis.v), 0.0), "plane basis v should lie on the plane");
	require_test(nearly_equal(quader_geometry::dot(basis.u, basis.v), 0.0), "plane basis axes should be orthogonal");

	const quader_geometry::QVec3d point = quader_geometry::plane_point_from_coordinates(basis, 3.0, -4.0);
	const quader_geometry::QPlaneCoordinates3<double> coordinates = quader_geometry::plane_coordinates(point + basis.normal * 5.0, basis);
	require_test(coordinates.valid, "plane coordinate projection should accept a valid basis");
	require_test(nearly_equal(coordinates.u, 3.0), "plane coordinate projection should recover the u coordinate");
	require_test(nearly_equal(coordinates.v, -4.0), "plane coordinate projection should recover the v coordinate");
	require_test(nearly_equal(coordinates.signed_distance, 5.0), "plane coordinate projection should preserve signed distance");
	require_test(vec3_nearly_equal(coordinates.projected_point, point), "plane coordinate projection should return the projected point");

	const quader_geometry::QPlaneBasis3<double> fallback_basis = quader_geometry::make_plane_basis<double>(
		{ 0.0, 0.0, 0.0 },
		{ 0.0, 0.0, 1.0 },
		{ 0.0, 0.0, 12.0 });
	require_test(fallback_basis.valid, "plane basis should recover when the preferred axis is parallel to the normal");
	require_test(!quader_geometry::make_plane_basis<double>({ 0.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 }).valid,
		"plane basis should reject a degenerate normal");
}

void three_plane_intersection_is_stable()
{
	const quader_geometry::QThreePlaneIntersection<double> hit = quader_geometry::intersect_three_planes<double>(
		{ { 1.0, 0.0, 0.0 }, 1.0 },
		{ { 0.0, 1.0, 0.0 }, 2.0 },
		{ { 0.0, 0.0, 1.0 }, 3.0 });

	require_test(hit.hit, "three independent planes should intersect in one point");
	require_test(vec3_nearly_equal(hit.point, { 1.0, 2.0, 3.0 }), "three-plane intersection point should satisfy all planes");

	const quader_geometry::QThreePlaneIntersection<double> parallel = quader_geometry::intersect_three_planes<double>(
		{ { 1.0, 0.0, 0.0 }, 1.0 },
		{ { 2.0, 0.0, 0.0 }, 4.0 },
		{ { 0.0, 1.0, 0.0 }, 2.0 });
	require_test(!parallel.hit, "three-plane intersection should reject dependent plane systems");
}

void line_and_segment_plane_queries_are_classified()
{
	const quader_geometry::QPlane3<double> plane = quader_geometry::plane_from_point_normal<double>(
		{ 0.0, 2.0, 0.0 },
		{ 0.0, 1.0, 0.0 });

	const quader_geometry::QLinePlaneHit<double> line_hit = quader_geometry::intersect_line_plane<double>(
		{ { 5.0, 8.0, -3.0 }, { 0.0, -2.0, 0.0 } },
		plane);
        require_test(line_hit.type ==
                         quader_geometry::QPlaneIntersectionType::Point,
                     "line-plane query should report point intersections");
        require_test(nearly_equal(line_hit.parameter, 3.0), "line-plane parameter should be measured along the line direction");
	require_test(vec3_nearly_equal(line_hit.point, { 5.0, 2.0, -3.0 }), "line-plane query should reconstruct the hit point");

	const quader_geometry::QLinePlaneHit<double> line_behind = quader_geometry::intersect_line_plane<double>(
		{ { 5.0, 8.0, -3.0 }, { 0.0, 2.0, 0.0 } },
		plane);
        require_test(
            line_behind.type ==
                    quader_geometry::QPlaneIntersectionType::Point &&
                nearly_equal(line_behind.parameter, -3.0),
            "line-plane query should keep negative parameters for true lines");

        const quader_geometry::QLinePlaneHit<double> coplanar_line = quader_geometry::intersect_line_plane<double>(
		{ { 0.0, 2.0, 0.0 }, { 1.0, 0.0, 0.0 } },
		plane);
        require_test(
            coplanar_line.type ==
                quader_geometry::QPlaneIntersectionType::Coplanar,
            "line-plane query should distinguish coplanar lines from misses");
        require_test(quader_geometry::intersect_line_plane<double>(
                         {{0.0, 5.0, 0.0}, {1.0, 0.0, 0.0}}, plane)
                             .type ==
                         quader_geometry::QPlaneIntersectionType::None,
                     "line-plane query should reject parallel offset lines");

        const quader_geometry::QSegmentPlaneHit<double> segment_hit = quader_geometry::intersect_segment_plane<double>(
		{ { 0.0, 5.0, 0.0 }, { 0.0, -1.0, 0.0 } },
		plane);
        require_test(segment_hit.type ==
                         quader_geometry::QPlaneIntersectionType::Point,
                     "segment-plane query should report crossings");
        require_test(nearly_equal(segment_hit.segment_factor, 0.5), "segment-plane query should report the crossing factor");
	require_test(vec3_nearly_equal(segment_hit.point, { 0.0, 2.0, 0.0 }), "segment-plane query should reconstruct the crossing point");

        require_test(
            quader_geometry::intersect_segment_plane<double>(
                {{0.0, 2.0, 0.0}, {4.0, 2.0, 0.0}}, plane)
                    .type == quader_geometry::QPlaneIntersectionType::Coplanar,
            "segment-plane query should distinguish coplanar segments");
        require_test(
            quader_geometry::intersect_segment_plane<double>(
                {{0.0, 4.0, 0.0}, {4.0, 4.0, 0.0}}, plane)
                    .type == quader_geometry::QPlaneIntersectionType::None,
            "segment-plane query should reject parallel offset segments");
        require_test(quader_geometry::intersect_segment_plane<double>(
                         {{0.0, 2.0, 0.0}, {0.0, 2.0, 0.0}}, plane)
                             .type ==
                         quader_geometry::QPlaneIntersectionType::Point,
                     "segment-plane query should classify a degenerate "
                     "on-plane segment as a point");
}

void ray_triangle_and_aabb_are_stable()
{
	const quader_geometry::QRay3<double> ray { { 0.25, -0.25, 2.0 }, { 0.0, 0.0, -1.0 } };
	const quader_geometry::QTriangle3<double> triangle { { -1.0, -1.0, 0.0 }, { 1.0, -1.0, 0.0 }, { 1.0, 1.0, 0.0 } };
	const quader_geometry::QRayTriangleHit<double> triangle_hit = quader_geometry::intersect_ray_triangle(ray, triangle);

	require_test(triangle_hit.hit, "ray should hit the triangle");
	require_test(nearly_equal(triangle_hit.distance, 2.0), "ray-triangle distance should be measured along the ray");
	require_test(vec3_nearly_equal(triangle_hit.point, { 0.25, -0.25, 0.0 }), "ray-triangle hit point should be reconstructed");
	require_test(nearly_equal(triangle_hit.u + triangle_hit.v + triangle_hit.w, 1.0), "ray-triangle barycentric coordinates should sum to one");

	const quader_geometry::QRayAabbHit<double> bounds_hit =
		quader_geometry::intersect_ray_aabb(ray, { { -1.0, -1.0, -0.5 }, { 1.0, 1.0, 0.5 } });
	require_test(bounds_hit.hit, "ray should hit the aligned box");
	require_test(nearly_equal(bounds_hit.min_distance, 1.5), "ray-AABB entry distance should be stable");
}

void bounded_triangle_ray_queries_are_stable()
{
	const quader_geometry::QRay3<double> ray { { 0.0, 0.0, 5.0 }, { 0.0, 0.0, -1.0 } };
	const std::array<quader_geometry::QTriangle3<double>, 2> triangles {
		quader_geometry::QTriangle3<double> { { -1.0, -1.0, 3.0 }, { 1.0, -1.0, 3.0 }, { 0.0, 1.0, 3.0 } },
		quader_geometry::QTriangle3<double> { { -1.0, -1.0, 1.0 }, { 1.0, -1.0, 1.0 }, { 0.0, 1.0, 1.0 } },
	};
	const std::array<quader_geometry::QAabb3d, 2> bounds {
		quader_geometry::triangle_aabb(triangles[0]),
		quader_geometry::triangle_aabb(triangles[1]),
	};

	const quader_geometry::QRayBoundedTriangleHit<double> nearest = quader_geometry::intersect_ray_bounded_triangles<double>(
		ray,
		std::span<const quader_geometry::QTriangle3<double>>(triangles),
		std::span<const quader_geometry::QAabb3d>(bounds));
	require_test(nearest.hit, "bounded triangle query should find a visible triangle");
	require_test(nearest.triangle_index == 0U, "bounded triangle query should return the closest hit index");
	require_test(nearly_equal(nearest.triangle_hit.distance, 2.0), "bounded triangle query should preserve triangle hit distance");
	require_test(nearly_equal(nearest.bounds_hit.min_distance, 2.0), "bounded triangle query should preserve AABB entry distance");

	quader_geometry::QRayBoundedTriangleQueryOptions<double> skip_near;
	skip_near.min_distance = 2.5;
	const quader_geometry::QRayBoundedTriangleHit<double> farther = quader_geometry::intersect_ray_bounded_triangles<double>(
		ray,
		std::span<const quader_geometry::QTriangle3<double>>(triangles),
		std::span<const quader_geometry::QAabb3d>(bounds),
		skip_near);
	require_test(farther.hit && farther.triangle_index == 1U, "bounded triangle query should honor minimum distance windows");
	require_test(nearly_equal(farther.triangle_hit.distance, 4.0), "bounded triangle query should return the next valid hit after the distance window");

	const quader_geometry::QAabb3d shifted_bounds { { 10.0, 10.0, 2.5 }, { 11.0, 11.0, 3.5 } };
	require_test(!quader_geometry::intersect_ray_bounded_triangle(ray, triangles[0], shifted_bounds).hit,
		"bounded triangle query should let the AABB reject a triangle hit");
	require_test(!quader_geometry::intersect_ray_bounded_triangles<double>(
					 ray,
					 std::span<const quader_geometry::QTriangle3<double>>(triangles),
					 std::span<const quader_geometry::QAabb3d>(bounds).first(1U))
					 .hit,
		"bounded triangle query should reject mismatched triangle and AABB spans");

	const quader_geometry::QRay3<float> float_ray { { 0.0F, 0.0F, 5.0F }, { 0.0F, 0.0F, -1.0F } };
	const quader_geometry::QTriangle3<float> float_triangle {
		{ -1.0F, -1.0F, 3.0F },
		{ 1.0F, -1.0F, 3.0F },
		{ 0.0F, 1.0F, 3.0F },
	};
	require_test(quader_geometry::intersect_ray_bounded_triangle(float_ray, float_triangle, quader_geometry::triangle_aabb(float_triangle)).hit,
		"float bounded triangle query should use the template fallback path");
}

void triangle_predicates_are_stable()
{
	const quader_geometry::QTriangle3<double> triangle { { -1.0, -1.0, 0.0 }, { 2.0, -1.0, 0.0 }, { -1.0, 3.0, 0.0 } };
	require_test(nearly_equal(quader_geometry::triangle_area(triangle), 6.0), "triangle area should be half the cross-product length");
	require_test(vec3_nearly_equal(quader_geometry::triangle_unit_normal(triangle), { 0.0, 0.0, 1.0 }),
		"triangle unit normal should preserve winding");

	const quader_geometry::Barycentric3<double> coordinates =
		quader_geometry::barycentric_coordinates({ 0.0, 0.0, 0.0 }, triangle);
	require_test(coordinates.valid, "triangle barycentric coordinates should be valid for nondegenerate triangles");
	require_test(nearly_equal(coordinates.u + coordinates.v + coordinates.w, 1.0), "triangle barycentric coordinates should sum to one");
	require_test(quader_geometry::point_in_triangle<double>({ 0.0, 0.0, 0.0 }, triangle), "point-in-triangle should accept interior points");
	require_test(quader_geometry::point_in_triangle<double>({ -1.0, 1.0, 0.0 }, triangle), "point-in-triangle should accept edge points");
	require_test(!quader_geometry::point_in_triangle<double>({ 0.0, 0.0, 0.01 }, triangle, 0.000001, 0.000001),
		"point-in-triangle should reject off-plane points beyond tolerance");
	require_test(!quader_geometry::point_in_triangle<double>({ 4.0, 4.0, 0.0 }, triangle), "point-in-triangle should reject outside points");

	const quader_geometry::QAabb3d triangle_bounds = quader_geometry::triangle_aabb(triangle);
	require_test(vec3_nearly_equal(triangle_bounds.min, { -1.0, -1.0, 0.0 }), "triangle AABB should include component minima");
	require_test(vec3_nearly_equal(triangle_bounds.max, { 2.0, 3.0, 0.0 }), "triangle AABB should include component maxima");

	const quader_geometry::QTriangle3<double> degenerate { { 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 2.0, 0.0, 0.0 } };
	require_test(nearly_equal(quader_geometry::triangle_area(degenerate), 0.0), "degenerate triangle area should be zero");
	require_test(!quader_geometry::barycentric_coordinates({ 1.0, 0.0, 0.0 }, degenerate).valid,
		"degenerate triangle barycentric coordinates should be invalid");
	require_test(!quader_geometry::point_in_triangle<double>({ 1.0, 0.0, 0.0 }, degenerate),
		"point-in-triangle should reject degenerate triangles");
}

void aabb_helpers_are_stable()
{
	quader_geometry::QAabb3d bounds = quader_geometry::empty_aabb3<double>();
	require_test(!quader_geometry::aabb_is_valid(bounds), "empty AABB should start invalid");

	quader_geometry::aabb_include(bounds, quader_geometry::QVec3d { 2.0, -1.0, 5.0 });
	quader_geometry::aabb_include(bounds, quader_geometry::QVec3d { -4.0, 3.0, 1.0 });
	require_test(quader_geometry::aabb_is_valid(bounds), "AABB should become valid after including points");
	require_test(vec3_nearly_equal(quader_geometry::aabb_size(bounds), { 6.0, 4.0, 4.0 }), "AABB size should span min to max");
	require_test(vec3_nearly_equal(quader_geometry::aabb_center(bounds), { -1.0, 1.0, 3.0 }), "AABB center should average min and max");
	require_test(quader_geometry::aabb_contains_point(bounds, { 0.0, 0.0, 2.0 }), "AABB should contain interior points");
	require_test(!quader_geometry::aabb_contains_point(bounds, { 0.0, 4.0, 2.0 }), "AABB should reject exterior points");

	const quader_geometry::QAabb3d expanded = quader_geometry::aabb_expanded(bounds, 0.5);
	require_test(vec3_nearly_equal(expanded.min, { -4.5, -1.5, 0.5 }), "AABB expansion should move min outward");
	require_test(vec3_nearly_equal(expanded.max, { 2.5, 3.5, 5.5 }), "AABB expansion should move max outward");

	const quader_geometry::QAabb3d overlapping { { 2.0, 3.0, 5.0 }, { 6.0, 4.0, 7.0 } };
	const quader_geometry::QAabb3d separated { { 3.0, 3.0, 3.0 }, { 4.0, 4.0, 4.0 } };
	require_test(quader_geometry::aabb_overlaps(bounds, overlapping), "AABB overlap should include touching faces");
	require_test(!quader_geometry::aabb_overlaps(bounds, separated), "AABB overlap should reject separated boxes");
}

void aabb_spatial_helpers_are_stable()
{
	const std::array<quader_geometry::QVec3d, 4> points {
		quader_geometry::QVec3d { 2.0, -1.0, 5.0 },
		quader_geometry::QVec3d { -4.0, 3.0, 1.0 },
		quader_geometry::QVec3d { 8.0, 2.0, -2.0 },
		quader_geometry::QVec3d { 1.0, 9.0, 4.0 },
	};
	const quader_geometry::QAabb3d bounds =
		quader_geometry::aabb_from_points<double>(std::span<const quader_geometry::QVec3d>(points));
	require_test(vec3_nearly_equal(bounds.min, { -4.0, -1.0, -2.0 }), "AABB from points should preserve component minima");
	require_test(vec3_nearly_equal(bounds.max, { 8.0, 9.0, 5.0 }), "AABB from points should preserve component maxima");
        require_test(quader_geometry::aabb_longest_axis(bounds) ==
                         quader_geometry::QAxis::X,
                     "AABB longest axis should choose the widest extent");
        require_test(vec3_nearly_equal(quader_geometry::aabb_closest_point(bounds, { 20.0, 4.0, -4.0 }), { 8.0, 4.0, -2.0 }),
		"AABB closest point should clamp each component");
	require_test(nearly_equal(quader_geometry::aabb_distance_squared_to_point(bounds, { 20.0, 4.0, -4.0 }), 148.0),
		"AABB distance should measure squared distance to the clamped point");
	const std::span<const quader_geometry::QVec3d> empty_points;
	require_test(!quader_geometry::aabb_is_valid(quader_geometry::aabb_from_points<double>(empty_points)),
		"AABB from an empty span should stay invalid");
}

void rect_helpers_are_stable()
{
	quader_geometry::QRect2d rect = quader_geometry::empty_rect2<double>();
	require_test(!quader_geometry::rect_is_valid(rect), "empty rect should start invalid");

	quader_geometry::rect_include(rect, quader_geometry::QVec2d { 4.0, -2.0 });
	quader_geometry::rect_include(rect, quader_geometry::QVec2d { -2.0, 6.0 });
	require_test(quader_geometry::rect_is_valid(rect), "rect should become valid after including points");
	require_test(vec2_nearly_equal(quader_geometry::rect_size(rect), { 6.0, 8.0 }), "rect size should span min to max");
	require_test(vec2_nearly_equal(quader_geometry::rect_center(rect), { 1.0, 2.0 }), "rect center should average min and max");
	require_test(quader_geometry::rect_contains_point(rect, { 0.0, 0.0 }), "rect should contain interior points");
	require_test(!quader_geometry::rect_contains_point(rect, { 5.0, 0.0 }), "rect should reject exterior points");

	const quader_geometry::QRect2d expanded = quader_geometry::rect_expanded(rect, 1.0);
	require_test(vec2_nearly_equal(expanded.min, { -3.0, -3.0 }), "rect expansion should move min outward");
	require_test(vec2_nearly_equal(expanded.max, { 5.0, 7.0 }), "rect expansion should move max outward");

	const quader_geometry::QRect2d overlapping { { -3.0, 6.0 }, { -2.0, 8.0 } };
	const quader_geometry::QRect2d separated { { 5.0, 0.0 }, { 6.0, 1.0 } };
	require_test(quader_geometry::rect_overlaps(rect, overlapping), "rect overlap should include touching edges");
	require_test(!quader_geometry::rect_overlaps(rect, separated), "rect overlap should reject separated rectangles");
}

void ray_distance_queries_are_stable()
{
	const quader_geometry::QRay3<double> ray { { 0.25, -1.0, 2.0 }, { 0.0, 0.0, -1.0 } };
	const quader_geometry::QClosestRayPoint<double> point = quader_geometry::closest_point_on_ray({ 0.25, -1.0, 0.0 }, ray);
	require_test(point.valid && nearly_equal(point.ray_distance, 2.0) && nearly_equal(point.distance_squared, 0.0),
		"closest point on ray should report zero distance for a point on the ray");

	const quader_geometry::QSegment3<double> segment { { -1.0, -1.0, 0.0 }, { 1.0, -1.0, 0.0 } };
	const quader_geometry::QSegmentRayClosestPoints<double> closest = quader_geometry::closest_points_segment_ray(segment, ray);
	require_test(closest.valid, "segment-ray closest point query should produce a candidate");
	require_test(nearly_equal(closest.segment_factor, 0.625), "segment-ray closest point should preserve the current edge factor convention");
	require_test(nearly_equal(closest.ray_distance, 2.0), "segment-ray closest point should preserve ray depth");
	require_test(vec3_nearly_equal(closest.segment_point, { 0.25, -1.0, 0.0 }), "segment closest point should lie on the edge");
}

void point_segment_3d_queries_are_stable()
{
	const quader_geometry::QSegment3<double> segment { { -2.0, 0.0, 0.0 }, { 2.0, 0.0, 0.0 } };
	const quader_geometry::QClosestSegmentPoint3<double> closest =
		quader_geometry::closest_point_on_segment({ 0.5, 3.0, 0.0 }, segment);
	require_test(closest.valid, "3D point-segment query should produce a closest point");
	require_test(nearly_equal(closest.segment_factor, 0.625), "3D point-segment query should preserve segment factor");
	require_test(vec3_nearly_equal(closest.point, { 0.5, 0.0, 0.0 }), "3D point-segment query should project onto the segment");
	require_test(nearly_equal(closest.distance_squared, 9.0), "3D point-segment query should report squared distance");

	const quader_geometry::QClosestSegmentPoint3<double> clamped =
		quader_geometry::closest_point_on_segment({ 8.0, 3.0, 0.0 }, segment);
	require_test(nearly_equal(clamped.segment_factor, 1.0), "3D point-segment query should clamp after the end point");
	require_test(vec3_nearly_equal(clamped.point, { 2.0, 0.0, 0.0 }), "3D point-segment query should return the endpoint after clamping");

	const quader_geometry::QSegment3<double> degenerate { { 1.0, 2.0, 3.0 }, { 1.0, 2.0, 3.0 } };
	const quader_geometry::QClosestSegmentPoint3<double> degenerate_closest =
		quader_geometry::closest_point_on_segment({ 1.0, 4.0, 3.0 }, degenerate);
	require_test(degenerate_closest.valid && nearly_equal(degenerate_closest.segment_factor, 0.0),
		"3D point-segment query should treat degenerate segments as one point");
	require_test(nearly_equal(quader_geometry::point_segment_distance_squared({ 1.0, 4.0, 3.0 }, degenerate), 4.0),
		"3D point-segment distance should handle degenerate segments");
}

void segment_segment_3d_queries_are_stable()
{
	const quader_geometry::QSegmentSegmentClosestPoints<double> crossing = quader_geometry::closest_points_segment_segment(
		{ { -1.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 } },
		{ { 0.0, -1.0, 0.0 }, { 0.0, 1.0, 0.0 } });
	require_test(crossing.valid && crossing.intersects, "segment-segment closest query should report ordinary crossings");
	require_test(nearly_equal(crossing.first_factor, 0.5) && nearly_equal(crossing.second_factor, 0.5),
		"segment-segment closest query should report crossing factors");
	require_test(vec3_nearly_equal(crossing.midpoint, { 0.0, 0.0, 0.0 }), "segment-segment closest midpoint should be the crossing point");

	const quader_geometry::QSegmentSegmentClosestPoints<double> near_parallel = quader_geometry::closest_points_segment_segment(
		{ { 0.0, 0.0, 0.0 }, { 10.0, 0.0, 0.0 } },
		{ { 0.0, 1.0, 0.000000001 }, { 10.0, 1.0, 0.000000002 } });
	require_test(near_parallel.valid && !near_parallel.intersects, "near-parallel separated segments should still produce a closest pair");
	require_test(nearly_equal(near_parallel.distance_squared, 1.0), "near-parallel segment distance should remain stable");

	const quader_geometry::QSegmentSegmentClosestPoints<double> degenerate_endpoint = quader_geometry::closest_points_segment_segment(
		{ { 2.0, 0.0, 0.0 }, { 2.0, 0.0, 0.0 } },
		{ { 0.0, 0.0, 0.0 }, { 4.0, 0.0, 0.0 } });
	require_test(degenerate_endpoint.valid && degenerate_endpoint.intersects, "degenerate endpoint segment should be measured against the other segment");
	require_test(nearly_equal(degenerate_endpoint.first_factor, 0.0) && nearly_equal(degenerate_endpoint.second_factor, 0.5),
		"degenerate endpoint segment should preserve endpoint and target factors");
	require_test(nearly_equal(quader_geometry::segment_segment_distance_squared(
								 { { 2.0, 0.0, 0.0 }, { 2.0, 0.0, 0.0 } },
								 { { 0.0, 0.0, 0.0 }, { 4.0, 0.0, 0.0 } }),
					 0.0),
		"segment-segment distance helper should share closest-query behavior");
}

void segment_factor_projection_queries_are_stable()
{
	const quader_geometry::QSegment3<double> target { { 0.0, 0.0, 0.0 }, { 10.0, 0.0, 0.0 } };
	quader_geometry::QSegmentSegmentFactorOptions<double> options;
	options.min_factor = 0.01;
	options.max_factor = 0.99;

	const quader_geometry::QSegmentFactorClosestPoint3<double> crossing =
		quader_geometry::closest_factor_on_segment_to_segment(target, { { 7.0, -1.0, 0.0 }, { 7.0, 1.0, 0.0 } }, options);
	require_test(crossing.valid && nearly_equal(crossing.segment_factor, 0.7),
		"segment factor projection should preserve interior finite crossings");
	require_test(vec3_nearly_equal(crossing.point, { 7.0, 0.0, 0.0 }),
		"segment factor projection should report the closest target point");

	const quader_geometry::QSegmentFactorClosestPoint3<double> boundary =
		quader_geometry::closest_factor_on_segment_to_segment(target, { { 7.0, 2.0, 0.0 }, { 7.0, 3.0, 0.0 } }, options);
	require_test(boundary.valid && nearly_equal(boundary.segment_factor, 0.7),
		"segment factor projection should use the query supporting line after endpoint closest points");

	const quader_geometry::QSegmentFactorClosestPoint3<double> near_parallel =
		quader_geometry::closest_factor_on_segment_to_segment(target, { { 2.0, 1.0, 0.0 }, { 8.0, 1.0, 0.0 } }, options);
	require_test(near_parallel.valid && nearly_equal(near_parallel.segment_factor, 0.5),
		"segment factor projection should use the query midpoint for near-parallel segments");

	const quader_geometry::QSegmentFactorClosestPoint3<double> point_query =
		quader_geometry::closest_factor_on_segment_to_segment(target, { { 3.0, 1.0, 0.0 }, { 3.0, 1.0, 0.0 } }, options);
	require_test(point_query.valid && nearly_equal(point_query.segment_factor, 0.3),
		"segment factor projection should handle degenerate query segments as points");

	const quader_geometry::QSegmentFactorClosestPoint3<double> clamped =
		quader_geometry::closest_factor_on_segment_to_segment(target, { { -2.0, 1.0, 0.0 }, { -2.0, 1.0, 0.0 } }, options);
	require_test(clamped.valid && nearly_equal(clamped.segment_factor, 0.01),
		"segment factor projection should respect caller factor limits");

	const quader_geometry::QSegmentFactorClosestPoint3<double> degenerate_target =
		quader_geometry::closest_factor_on_segment_to_segment(
			{ { 1.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 } },
			{ { 2.0, 0.0, 0.0 }, { 3.0, 0.0, 0.0 } },
			options);
	require_test(!degenerate_target.valid, "segment factor projection should reject degenerate target segments");
}

void gte_backed_double_ray_queries_are_stable()
{
	const quader_geometry::QRay3<double> side_ray { { 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 } };
	const quader_geometry::QClosestRayPoint<double> side_point = quader_geometry::closest_point_on_ray({ 3.0, 2.0, 0.0 }, side_ray);
	require_test(side_point.valid, "double point-ray query should produce a closest point");
	require_test(nearly_equal(side_point.ray_distance, 3.0), "double point-ray query should preserve ray parameter");
	require_test(vec3_nearly_equal(side_point.point, { 3.0, 0.0, 0.0 }), "double point-ray query should project onto the ray");
	require_test(nearly_equal(quader_geometry::point_ray_distance_squared({ 3.0, 2.0, 0.0 }, side_ray), 4.0),
		"double point-ray distance should report squared distance");

	const quader_geometry::QRayAabbHit<double> inside_hit =
		quader_geometry::intersect_ray_aabb(side_ray, { { -1.0, -1.0, -1.0 }, { 1.0, 1.0, 1.0 } });
	require_test(inside_hit.hit, "double ray-AABB query should hit when starting inside");
	require_test(nearly_equal(inside_hit.min_distance, 0.0), "double ray-AABB query should clamp inside entry to the ray start");
	require_test(nearly_equal(inside_hit.max_distance, 1.0), "double ray-AABB query should preserve exit distance");
	require_test(!quader_geometry::intersect_ray_aabb(side_ray, { { -4.0, 3.0, -1.0 }, { -2.0, 4.0, 1.0 } }).hit,
		"double ray-AABB query should reject misses");

	const quader_geometry::QSegment3<double> behind_segment { { -3.0, 2.0, 0.0 }, { -2.0, 2.0, 0.0 } };
	const quader_geometry::QSegmentRayClosestPoints<double> behind = quader_geometry::closest_points_segment_ray(behind_segment, side_ray);
	require_test(behind.valid, "double segment-ray query should produce a closest point");
	require_test(nearly_equal(behind.ray_distance, 0.0), "double segment-ray query should clamp closest ray point to the ray start");
	require_test(nearly_equal(behind.segment_factor, 1.0), "double segment-ray query should preserve closest segment endpoint");
	require_test(nearly_equal(quader_geometry::segment_ray_distance_squared(behind_segment, side_ray), 8.0),
		"double segment-ray distance should report squared distance");
}

void float_template_ray_queries_remain_stable()
{
	const quader_geometry::QRay3<float> ray { { 0.0F, 0.0F, 2.0F }, { 0.0F, 0.0F, -1.0F } };
	const quader_geometry::QRayAabbHit<float> bounds_hit =
		quader_geometry::intersect_ray_aabb(ray, { { -1.0F, -1.0F, -0.5F }, { 1.0F, 1.0F, 0.5F } });
	require_test(bounds_hit.hit && nearly_equal(bounds_hit.min_distance, 1.5), "float template ray-AABB query should keep slab behavior");

	const quader_geometry::QRay3<float> side_ray { { 0.0F, 0.0F, 0.0F }, { 1.0F, 0.0F, 0.0F } };
	const quader_geometry::QAabb3<float> inverted_bounds { { 1.0F, -1.0F, -1.0F }, { -1.0F, 1.0F, 1.0F } };
	require_test(!quader_geometry::intersect_ray_aabb<float>(side_ray, inverted_bounds).hit,
		"float template ray-AABB query should reject invalid AABBs before slab testing");

	const quader_geometry::QClosestRayPoint<float> point = quader_geometry::closest_point_on_ray({ 0.0F, 2.0F, 0.0F }, ray);
	require_test(point.valid && nearly_equal(point.ray_distance, 2.0) && nearly_equal(point.distance_squared, 4.0),
		"float template point-ray query should keep local distance behavior");

	const quader_geometry::QSegment3<float> segment { { -1.0F, 0.0F, 0.0F }, { 1.0F, 0.0F, 0.0F } };
	const quader_geometry::QSegmentRayClosestPoints<float> closest = quader_geometry::closest_points_segment_ray(segment, ray);
	require_test(closest.valid && nearly_equal(closest.segment_factor, 0.5) && nearly_equal(closest.ray_distance, 2.0),
		"float template segment-ray query should keep local closest-point behavior");
}

void float_ray_plane_query_promotes_to_double()
{
	const quader_geometry::QRay3<float> ray {
		{ -16794.263671875F, 662792.0F, -285979.90625F },
		{ -7.735326290130615F, -3.6271681785583496F, -7.772265911102295F },
	};
	const quader_geometry::QPlane3<float> plane {
		{ 6.390786170959473F, 8.432144165039062F, -5.03233528137207F },
		6920459.0F,
	};

	const quader_geometry::QRayPlaneHit<float> hit = quader_geometry::intersect_ray_plane(ray, plane, 0.000001F);
	require_test(hit.hit, "float ray-plane wrapper should report the forward hit");
	require_test(nearly_equal(hit.distance, 2.858177661895752),
		"float ray-plane wrapper should promote large dot products to double before computing distance");
	require_test(
		nearly_equal(
			quader_geometry::signed_distance_to_plane(
				static_cast<quader_geometry::QVec3d>(hit.point),
				static_cast<quader_geometry::QPlane3<double>>(plane)),
			0.0,
			0.5),
		"float ray-plane wrapper should return a point that remains close to the source plane after float storage");
}

void projection_and_area_are_stable()
{
	const quader_geometry::QAxis axis = quader_geometry::dominant_axis(quader_geometry::QVec3d { 0.1, 0.2, 0.9 });
        require_test(
            axis == quader_geometry::QAxis::Z,
            "dominant axis should choose the largest normal component");
        require_test(quader_geometry::project_dominant_axis(quader_geometry::QVec3d { 2.0, 3.0, 4.0 }, axis).x == 2.0,
		"dominant-axis projection should drop the requested axis");

	const std::vector<quader_geometry::QVec2d> square {
		{ -1.0, -1.0 },
		{ 1.0, -1.0 },
		{ 1.0, 1.0 },
		{ -1.0, 1.0 },
	};
	require_test(nearly_equal(quader_geometry::polygon_signed_area<double>(square), 4.0), "polygon signed area should preserve winding");
	require_test(nearly_equal(quader_geometry::triangle_signed_area<double>({ 0.0, 0.0 }, { 3.0, 0.0 }, { 0.0, 4.0 }), 6.0),
		"2D triangle signed area should preserve winding");
	require_test(nearly_equal(quader_geometry::triangle_area_abs<double>({ 0.0, 0.0 }, { 0.0, 4.0 }, { 3.0, 0.0 }), 6.0),
		"2D triangle absolute area should ignore winding");
}

void dominant_axis_unprojection_is_stable()
{
	const quader_geometry::QPlane3<double> plane { { 2.0, 3.0, 5.0 }, 31.0 };
	const quader_geometry::QVec3d expected { 1.0, 3.0, 4.0 };

        const quader_geometry::QDominantAxisUnprojectResult<double> dropped_x =
            quader_geometry::unproject_dominant_axis_point_to_plane<double>(
                {expected.y, expected.z}, plane, quader_geometry::QAxis::X);
        const quader_geometry::QDominantAxisUnprojectResult<double> dropped_y =
            quader_geometry::unproject_dominant_axis_point_to_plane<double>(
                {expected.x, expected.z}, plane, quader_geometry::QAxis::Y);
        const quader_geometry::QDominantAxisUnprojectResult<double> dropped_z =
            quader_geometry::unproject_dominant_axis_point_to_plane<double>(
                {expected.x, expected.y}, plane, quader_geometry::QAxis::Z);

        require_test(dropped_x.valid && vec3_nearly_equal(dropped_x.point, expected), "unprojection should restore the dropped x coordinate");
	require_test(dropped_y.valid && vec3_nearly_equal(dropped_y.point, expected), "unprojection should restore the dropped y coordinate");
	require_test(dropped_z.valid && vec3_nearly_equal(dropped_z.point, expected), "unprojection should restore the dropped z coordinate");
        require_test(
            !quader_geometry::unproject_dominant_axis_point_to_plane<double>(
                 {0.0, 0.0}, {{0.0, 1.0, 0.0}, 0.0}, quader_geometry::QAxis::X)
                 .valid,
            "unprojection should reject a dropped axis with no plane "
            "coefficient");
}

void polygon_predicates_are_stable()
{
	const std::vector<quader_geometry::QVec2d> polygon {
		{ 0.0, 0.0 },
		{ 3.0, 0.0 },
		{ 3.0, 1.0 },
		{ 1.0, 1.0 },
		{ 1.0, 3.0 },
		{ 0.0, 3.0 },
	};

	require_test(quader_geometry::point_in_or_on_polygon_2d<double>({ 0.5, 2.5 }, polygon), "point inside concave polygon should be accepted");
	require_test(quader_geometry::point_in_or_on_polygon_2d<double>({ 1.0, 2.0 }, polygon), "point on polygon boundary should be accepted");
	require_test(!quader_geometry::point_in_or_on_polygon_2d<double>({ 2.0, 2.0 }, polygon), "point in concave notch should be outside");
}

void segment_predicates_are_stable()
{
	require_test(
		quader_geometry::segments_intersect_2d<double>({ 0.0, 0.0 }, { 2.0, 2.0 }, { 0.0, 2.0 }, { 2.0, 0.0 }),
		"crossing segments should intersect");
	require_test(
		quader_geometry::segments_intersect_2d<double>({ 0.0, 0.0 }, { 2.0, 0.0 }, { 2.0, 0.0 }, { 2.0, 2.0 }),
		"segments that touch at an endpoint should intersect");
	require_test(
		quader_geometry::point_on_segment_2d<double>({ 1.0, 0.0 }, { 0.0, 0.0 }, { 2.0, 0.0 }),
		"point on segment should be accepted");
	require_test(
		!quader_geometry::segments_intersect_2d<double>({ 0.0, 0.0 }, { 1.0, 0.0 }, { 2.0, 0.0 }, { 3.0, 0.0 }),
		"separated collinear segments should not intersect");
}

void proper_segment_intersection_is_endpoint_exclusive()
{
	const quader_geometry::QSegmentIntersection2<double> crossing = quader_geometry::proper_segment_intersection_2d<double>(
		{ 0.0, 0.0 }, { 2.0, 2.0 }, { 0.0, 2.0 }, { 2.0, 0.0 });
	require_test(crossing.hit, "proper segment intersection should accept interior crossings");
	require_test(nearly_equal(crossing.first_factor, 0.5) && nearly_equal(crossing.second_factor, 0.5),
		"proper segment intersection should report both segment factors");
	require_test(vec2_nearly_equal(crossing.point, { 1.0, 1.0 }), "proper segment intersection should report the crossing point");

	require_test(!quader_geometry::proper_segment_intersection_2d<double>({ 0.0, 0.0 }, { 2.0, 0.0 }, { 2.0, 0.0 }, { 2.0, 2.0 }).hit,
		"proper segment intersection should exclude endpoint-only touches");
	require_test(!quader_geometry::proper_segment_intersection_2d<double>({ 0.0, 0.0 }, { 2.0, 0.0 }, { 0.0, 1.0 }, { 2.0, 1.0 }).hit,
		"proper segment intersection should reject parallel segments");
}

void classified_segment_intersections_are_stable()
{
	const quader_geometry::QClassifiedSegmentIntersection2<double> crossing = quader_geometry::intersect_segments_2d(
		{ 0.0, 0.0 }, { 2.0, 2.0 }, { 0.0, 2.0 }, { 2.0, 0.0 });
        require_test(crossing.hit &&
                         crossing.kind ==
                             quader_geometry::QSegmentIntersectionKind2::Point,
                     "classified segment intersection should report ordinary "
                     "crossings as points");
        require_test(crossing.point_count == 1, "classified point intersections should report one point");
	require_test(nearly_equal(crossing.first_factors[0], 0.5) && nearly_equal(crossing.second_factors[0], 0.5),
		"classified point intersections should preserve both factors");
	require_test(vec2_nearly_equal(crossing.points[0], { 1.0, 1.0 }), "classified point intersections should preserve the crossing point");

	const quader_geometry::QClassifiedSegmentIntersection2<double> overlap = quader_geometry::intersect_segments_2d(
		{ 0.0, 0.0 }, { 4.0, 0.0 }, { 1.0, 0.0 }, { 3.0, 0.0 });
        require_test(
            overlap.hit &&
                overlap.kind ==
                    quader_geometry::QSegmentIntersectionKind2::Overlap,
            "classified segment intersection should distinguish overlaps from "
            "point hits");
        require_test(overlap.point_count == 2, "classified overlaps should report two endpoints");
	require_test(nearly_equal(overlap.first_factors[0], 0.25) && nearly_equal(overlap.first_factors[1], 0.75),
		"classified overlaps should preserve first-segment factors");
	require_test(nearly_equal(overlap.second_factors[0], 0.0) && nearly_equal(overlap.second_factors[1], 1.0),
		"classified overlaps should preserve second-segment factors");
	require_test(vec2_nearly_equal(overlap.points[0], { 1.0, 0.0 }) && vec2_nearly_equal(overlap.points[1], { 3.0, 0.0 }),
		"classified overlaps should preserve overlap endpoints");

	const quader_geometry::QClassifiedSegmentIntersection2<double> degenerate_endpoint = quader_geometry::intersect_segments_2d(
		{ 2.0, 0.0 }, { 2.0, 0.0 }, { 0.0, 0.0 }, { 4.0, 0.0 });
        require_test(degenerate_endpoint.hit &&
                         degenerate_endpoint.kind ==
                             quader_geometry::QSegmentIntersectionKind2::Point,
                     "classified segment intersection should handle degenerate "
                     "endpoint segments");
        require_test(nearly_equal(degenerate_endpoint.first_factors[0], 0.0) && nearly_equal(degenerate_endpoint.second_factors[0], 0.5),
		"classified degenerate endpoint intersections should preserve factors");
	require_test(vec2_nearly_equal(degenerate_endpoint.points[0], { 2.0, 0.0 }),
		"classified degenerate endpoint intersections should preserve the shared point");

	const quader_geometry::QClassifiedSegmentIntersection2<double> near_parallel_miss = quader_geometry::intersect_segments_2d(
		{ 0.0, 0.0 }, { 10.0, 0.0000001 }, { 0.0, 1.0 }, { 10.0, 1.0000001 }, 0.000000001);
        require_test(!near_parallel_miss.hit &&
                         near_parallel_miss.kind ==
                             quader_geometry::QSegmentIntersectionKind2::None,
                     "classified segment intersection should reject "
                     "near-parallel separated segments");

        const quader_geometry::QClassifiedSegmentIntersection2<float> float_crossing = quader_geometry::intersect_segments_2d<float>(
		{ 0.0F, 0.0F }, { 2.0F, 0.0F }, { 1.0F, -1.0F }, { 1.0F, 1.0F });
        require_test(float_crossing.hit &&
                         float_crossing.kind ==
                             quader_geometry::QSegmentIntersectionKind2::Point,
                     "float classified segment intersection should use the "
                     "template fallback");
}

void line_line_closest_points_are_stable()
{
	const quader_geometry::QLine3<double> first { { 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 } };
	const quader_geometry::QLine3<double> second { { 0.0, 2.0, 2.0 }, { 0.0, 1.0, 0.0 } };
	const quader_geometry::QLineLineClosestPoints<double> skew = quader_geometry::closest_points_line_line(first, second);

	require_test(skew.valid && !skew.intersects, "line-line closest query should accept skew lines without reporting intersection");
	require_test(vec3_nearly_equal(skew.first_point, { 0.0, 0.0, 0.0 }), "line-line closest query should report the first closest point");
	require_test(vec3_nearly_equal(skew.second_point, { 0.0, 0.0, 2.0 }), "line-line closest query should report the second closest point");
	require_test(vec3_nearly_equal(skew.midpoint, { 0.0, 0.0, 1.0 }), "line-line closest query should report the closest-pair midpoint");
	require_test(nearly_equal(skew.distance_squared, 4.0), "line-line closest query should report squared separation");

	const quader_geometry::QLineLineClosestPoints<double> crossing = quader_geometry::closest_points_line_line(
		first, { { 2.0, -1.0, 0.0 }, { 0.0, 1.0, 0.0 } });
	require_test(crossing.valid && crossing.intersects, "line-line closest query should flag true intersections");
	require_test(vec3_nearly_equal(crossing.midpoint, { 2.0, 0.0, 0.0 }), "line-line closest midpoint should equal the intersection point");
}

void projected_polygon_triangulation_is_stable()
{
	const std::vector<quader_geometry::QVec2d> concave_polygon {
		{ 0.0, 0.0 },
		{ 3.0, 0.0 },
		{ 3.0, 1.0 },
		{ 1.0, 1.0 },
		{ 1.0, 3.0 },
		{ 0.0, 3.0 },
	};

	const std::vector<quader_geometry::QTriangleIndices> triangles =
		quader_geometry::triangulate_projected_polygon(std::span<const quader_geometry::QVec2d>(concave_polygon));
	require_test(triangles.size() == concave_polygon.size() - 2U, "concave polygon should triangulate to n - 2 triangles");

	double covered_area = 0.0;
	for (const quader_geometry::QTriangleIndices triangle : triangles) {
		require_test(triangle[0] < concave_polygon.size() && triangle[1] < concave_polygon.size() && triangle[2] < concave_polygon.size(),
			"triangulation indices should refer to polygon vertices");
		require_test(triangle[0] != triangle[1] && triangle[0] != triangle[2] && triangle[1] != triangle[2],
			"triangulation should not emit duplicate triangle vertices");
		covered_area += triangle_area_abs(concave_polygon, triangle);
	}
	require_test(nearly_equal(covered_area, std::abs(quader_geometry::polygon_signed_area<double>(concave_polygon))),
		"triangulation should cover the projected polygon area");

	const std::vector<quader_geometry::QVec2d> degenerate_polygon {
		{ 0.0, 0.0 },
		{ 1.0, 0.0 },
		{ 2.0, 0.0 },
		{ 3.0, 0.0 },
	};
	require_test(quader_geometry::triangulate_projected_polygon(std::span<const quader_geometry::QVec2d>(degenerate_polygon)).empty(),
		"degenerate projected polygon should not triangulate");
}

} // namespace

TEST(QdrGeometryTests, QdrGeometryHelpersRemainStable) {
	ray_plane_and_projection_are_stable();
	plane_basis_helpers_are_stable();
	three_plane_intersection_is_stable();
	line_and_segment_plane_queries_are_classified();
	ray_triangle_and_aabb_are_stable();
	bounded_triangle_ray_queries_are_stable();
	triangle_predicates_are_stable();
	aabb_helpers_are_stable();
	aabb_spatial_helpers_are_stable();
	rect_helpers_are_stable();
	ray_distance_queries_are_stable();
	point_segment_3d_queries_are_stable();
	segment_segment_3d_queries_are_stable();
	segment_factor_projection_queries_are_stable();
	gte_backed_double_ray_queries_are_stable();
	float_template_ray_queries_remain_stable();
	float_ray_plane_query_promotes_to_double();
	projection_and_area_are_stable();
	dominant_axis_unprojection_is_stable();
	polygon_predicates_are_stable();
	segment_predicates_are_stable();
	proper_segment_intersection_is_endpoint_exclusive();
	classified_segment_intersections_are_stable();
	line_line_closest_points_are_stable();
	projected_polygon_triangulation_is_stable();
}

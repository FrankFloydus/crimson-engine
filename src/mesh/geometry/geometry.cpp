////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/geometry/geometry.hpp>

#include <Mathematics/AlignedBox.h>
#include <Mathematics/DistLineLine.h>
#include <Mathematics/DistPointSegment.h>
#include <Mathematics/DistPointRay.h>
#include <Mathematics/DistRaySegment.h>
#include <Mathematics/DistSegmentSegment.h>
#include <Mathematics/IntrRay3AlignedBox3.h>
#include <Mathematics/IntrRay3Triangle3.h>
#include <Mathematics/IntrSegment2Segment2.h>
#include <Mathematics/Segment.h>
#include <Mathematics/Triangle.h>
#include <Mathematics/Vector2.h>
#include <Mathematics/Vector3.h>

#include <mapbox/earcut.hpp>

namespace quader_geometry::detail {

/**
 * Enumerates QMathBackend values used by the geometry layer.
 */
enum class QMathBackend {
  Local,
  Gte,
};

[[nodiscard]] constexpr QMathBackend active_math_backend()
{
  return QMathBackend::Gte;
}

static_assert(active_math_backend() == QMathBackend::Gte);

[[nodiscard]] gte::Vector2<double> to_gte(QVec2d value)
{
	return { value.x, value.y };
}

[[nodiscard]] gte::Vector3<double> to_gte(QVec3d value)
{
	return { value.x, value.y, value.z };
}

[[nodiscard]] QVec2d from_gte(gte::Vector2<double> value)
{
	return { value[0], value[1] };
}

[[nodiscard]] QVec3d from_gte(gte::Vector3<double> value)
{
	return { value[0], value[1], value[2] };
}

[[nodiscard]] gte::Ray3<double> to_gte(const QRay3<double>& ray)
{
	return { to_gte(ray.origin), to_gte(ray.direction) };
}

[[nodiscard]] gte::Line3<double> to_gte(const QLine3<double>& line)
{
	return { to_gte(line.origin), to_gte(line.direction) };
}

[[nodiscard]] gte::Segment2<double> to_gte_segment(QVec2d segment_a, QVec2d segment_b)
{
	return { to_gte(segment_a), to_gte(segment_b) };
}

[[nodiscard]] gte::Segment3<double> to_gte(const QSegment3<double>& segment)
{
	return { to_gte(segment.a), to_gte(segment.b) };
}

[[nodiscard]] gte::Triangle3<double> to_gte(const QTriangle3<double>& triangle)
{
	return { to_gte(triangle.a), to_gte(triangle.b), to_gte(triangle.c) };
}

[[nodiscard]] gte::AlignedBox3<double> to_gte(const QAabb3<double>& aabb)
{
	return { to_gte(aabb.min), to_gte(aabb.max) };
}

static_assert(sizeof(gte::Vector3<double>) > 0);
static_assert(sizeof(gte::DCPQuery<double, gte::Vector2<double>, gte::Segment2<double>>) > 0);
static_assert(sizeof(gte::DCPQuery<double, gte::Vector3<double>, gte::Segment3<double>>) > 0);
static_assert(sizeof(gte::DCPQuery<double, gte::Line3<double>, gte::Line3<double>>) > 0);
static_assert(sizeof(gte::DCPQuery<double, gte::Vector3<double>, gte::Ray3<double>>) > 0);
static_assert(sizeof(gte::DCPQuery<double, gte::Ray3<double>, gte::Segment3<double>>) > 0);
static_assert(sizeof(gte::DCPQuery<double, gte::Segment3<double>, gte::Segment3<double>>) > 0);
static_assert(sizeof(gte::FIQuery<double, gte::Ray3<double>, gte::Triangle3<double>>) > 0);
static_assert(sizeof(gte::FIQuery<double, gte::Ray3<double>, gte::AlignedBox3<double>>) > 0);
static_assert(sizeof(gte::FIQuery<double, gte::Segment2<double>, gte::Segment2<double>>) > 0);

[[nodiscard]] double triangle_projected_area_abs(QVec2d a, QVec2d b, QVec2d c)
{
	const std::array points { a, b, c };
	return std::abs(polygon_signed_area<double>(std::span<const QVec2d>(points)));
}

} // namespace quader_geometry::detail

namespace quader_geometry {

QClosestSegmentPoint2<double> closest_point_on_segment(QVec2d point, QVec2d segment_a, QVec2d segment_b, double epsilon)
{
	const QVec2d segment_direction = segment_b - segment_a;
	if (length_squared(segment_direction) <= epsilon * epsilon) {
		return { true, 0.0, segment_a, length_squared(point - segment_a) };
	}

	gte::DCPQuery<double, gte::Vector2<double>, gte::Segment2<double>> query;
	const auto result = query(detail::to_gte(point), detail::to_gte_segment(segment_a, segment_b));
	return { true, result.parameter, detail::from_gte(result.closest[1]), result.sqrDistance };
}

double point_segment_distance_squared(QVec2d point, QVec2d segment_a, QVec2d segment_b, double epsilon)
{
	return closest_point_on_segment(point, segment_a, segment_b, epsilon).distance_squared;
}

QClosestSegmentPoint3<double> closest_point_on_segment(QVec3d point, const QSegment3<double>& segment, double epsilon)
{
	const QVec3d segment_direction = segment.b - segment.a;
	if (length_squared(segment_direction) <= epsilon * epsilon) {
		return { true, 0.0, segment.a, length_squared(point - segment.a) };
	}

	gte::DCPQuery<double, gte::Vector3<double>, gte::Segment3<double>> query;
	const auto result = query(detail::to_gte(point), detail::to_gte(segment));
	return { true, result.parameter, detail::from_gte(result.closest[1]), result.sqrDistance };
}

double point_segment_distance_squared(QVec3d point, const QSegment3<double>& segment, double epsilon)
{
	return closest_point_on_segment(point, segment, epsilon).distance_squared;
}

QClassifiedSegmentIntersection2<double> intersect_segments_2d(
	QVec2d a, QVec2d b, QVec2d c, QVec2d d, double epsilon)
{
	const QVec2d first_direction = b - a;
	const QVec2d second_direction = d - c;
	if (length_squared(first_direction) <= epsilon * epsilon ||
			length_squared(second_direction) <= epsilon * epsilon) {
		return detail::classify_segments_2d_local(a, b, c, d, epsilon);
	}

	gte::FIQuery<double, gte::Segment2<double>, gte::Segment2<double>> query;
	const auto result = query.Exact(detail::to_gte_segment(a, b), detail::to_gte_segment(c, d));
	if (!result.intersect || result.numIntersections <= 0) {
		return detail::classify_segments_2d_local(a, b, c, d, epsilon);
	}

	const auto point = [&](int index) {
		return detail::from_gte(result.point[static_cast<std::size_t>(index)]);
	};

	if (result.numIntersections == 1 ||
			length_squared(point(0) - point(1)) <= epsilon * epsilon ||
			std::abs(result.segment0Parameter[0] - result.segment0Parameter[1]) <= epsilon) {
		return detail::make_segment_intersection_point_2d<double>(
			result.segment0Parameter[0],
			result.segment1Parameter[0],
			point(0));
	}

	QClassifiedSegmentIntersection2<double> classified;
	classified.hit = true;
        classified.kind = QSegmentIntersectionKind2::Overlap;
        classified.point_count = 2;
	classified.first_factors[0] = std::clamp(result.segment0Parameter[0], 0.0, 1.0);
	classified.first_factors[1] = std::clamp(result.segment0Parameter[1], 0.0, 1.0);
	classified.second_factors[0] = std::clamp(result.segment1Parameter[0], 0.0, 1.0);
	classified.second_factors[1] = std::clamp(result.segment1Parameter[1], 0.0, 1.0);
	classified.points[0] = point(0);
	classified.points[1] = point(1);
	return classified;
}

std::vector<QTriangleIndices> triangulate_projected_polygon(std::span<const QVec2d> points, double epsilon, double coverage_tolerance_ratio)
{
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
		polygon.front().push_back({ point.x, point.y });
	}

	const std::vector<std::uint32_t> indices = mapbox::earcut<std::uint32_t>(polygon);
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

		const double triangle_area = detail::triangle_projected_area_abs(points[a], points[b], points[c]);
		if (triangle_area <= epsilon) {
			continue;
		}

		triangles.push_back({ a, b, c });
		covered_area += triangle_area;
	}

	const double coverage_tolerance = std::max(epsilon, polygon_area * std::max(0.0, coverage_tolerance_ratio));
	if (triangles.empty() || covered_area + coverage_tolerance < polygon_area) {
		return {};
	}

	return triangles;
}

QRayPlaneHit<float> intersect_ray_plane(const QRay3<float>& ray, const QPlane3<float>& plane, float epsilon)
{
	const QRayPlaneHit<double> hit =
		intersect_ray_plane<double>(QRay3<double>(ray), QPlane3<double>(plane), static_cast<double>(epsilon));
	if (!hit.hit) {
		return {};
	}
	return { true, static_cast<float>(hit.distance), static_cast<QVec3f>(hit.point) };
}

QRayTriangleHit<double> intersect_ray_triangle(const QRay3<double>& ray, const QTriangle3<double>& triangle, double epsilon)
{
	const QVec3d edge1 = triangle.b - triangle.a;
	const QVec3d edge2 = triangle.c - triangle.a;
	const double determinant = dot(edge1, cross(ray.direction, edge2));
	if (std::abs(determinant) <= epsilon) {
		return {};
	}

	gte::FIQuery<double, gte::Ray3<double>, gte::Triangle3<double>> query;
	const auto result = query(detail::to_gte(ray), detail::to_gte(triangle));
	if (!result.intersect) {
		return {};
	}

	return {
		true,
		result.parameter,
		result.triangleBary[1],
		result.triangleBary[2],
		result.triangleBary[0],
		detail::from_gte(result.point),
	};
}

QRayAabbHit<double> intersect_ray_aabb(const QRay3<double>& ray, const QAabb3<double>& aabb, double epsilon)
{
	if (!aabb_is_valid(aabb)) {
		return {};
	}

	if (length_squared(ray.direction) <= epsilon * epsilon) {
		if (!aabb_contains_point(aabb, ray.origin)) {
			return {};
		}
		return { true, 0.0, std::numeric_limits<double>::infinity(), ray.origin };
	}

	gte::FIQuery<double, gte::Ray3<double>, gte::AlignedBox3<double>> query;
	const auto result = query(detail::to_gte(ray), detail::to_gte(aabb));
	if (!result.intersect) {
		return {};
	}

	return {
		true,
		result.parameter[0],
		result.parameter[1],
		detail::from_gte(result.point[0]),
	};
}

QClosestRayPoint<double> closest_point_on_ray(QVec3d point, const QRay3<double>& ray, double epsilon)
{
	if (length_squared(ray.direction) <= epsilon * epsilon) {
		return {};
	}

	gte::DCPQuery<double, gte::Vector3<double>, gte::Ray3<double>> query;
	const auto result = query(detail::to_gte(point), detail::to_gte(ray));
	return { true, result.parameter, detail::from_gte(result.closest[1]), result.sqrDistance };
}

double point_ray_distance_squared(QVec3d point, const QRay3<double>& ray, double epsilon)
{
	const QClosestRayPoint<double> closest = closest_point_on_ray(point, ray, epsilon);
	return closest.valid ? closest.distance_squared : std::numeric_limits<double>::infinity();
}

QSegmentRayClosestPoints<double> closest_points_segment_ray(const QSegment3<double>& segment, const QRay3<double>& ray, double epsilon)
{
	const QVec3d segment_direction = segment.b - segment.a;
	const double segment_length_squared = length_squared(segment_direction);
	if (length_squared(ray.direction) <= epsilon * epsilon) {
		return {};
	}

	if (segment_length_squared <= epsilon * epsilon) {
		const QClosestRayPoint<double> closest = closest_point_on_ray(segment.a, ray, epsilon);
		if (!closest.valid) {
			return {};
		}
		return { true, 0.0, closest.ray_distance, segment.a, closest.point, closest.distance_squared };
	}

	const auto candidate_for = [&](double segment_factor, double ray_distance) {
		QSegmentRayClosestPoints<double> candidate;
		candidate.valid = true;
		candidate.segment_factor = std::clamp(segment_factor, 0.0, 1.0);
		candidate.ray_distance = std::max(0.0, ray_distance);
		candidate.segment_point = segment.a + segment_direction * candidate.segment_factor;
		candidate.ray_point = ray.origin + ray.direction * candidate.ray_distance;
		candidate.distance_squared = length_squared(candidate.segment_point - candidate.ray_point);
		return candidate;
	};

	QSegmentRayClosestPoints<double> best = candidate_for(0.0, 0.0);
	const auto keep_best = [&](QSegmentRayClosestPoints<double> candidate) {
		if (!best.valid || candidate.distance_squared < best.distance_squared) {
			best = candidate;
		}
	};

	const double ray_length_squared = length_squared(ray.direction);
	best = candidate_for(0.0, dot(segment.a - ray.origin, ray.direction) / ray_length_squared);
	keep_best(candidate_for(1.0, dot(segment.b - ray.origin, ray.direction) / ray_length_squared));
	keep_best(candidate_for(dot(ray.origin - segment.a, segment_direction) / segment_length_squared, 0.0));

	gte::DCPQuery<double, gte::Ray3<double>, gte::Segment3<double>> query;
	const auto result = query(detail::to_gte(ray), detail::to_gte(segment));
	keep_best(candidate_for(result.parameter[1], result.parameter[0]));

	return best;
}

double segment_ray_distance_squared(const QSegment3<double>& segment, const QRay3<double>& ray, double epsilon)
{
	const QSegmentRayClosestPoints<double> closest = closest_points_segment_ray(segment, ray, epsilon);
	return closest.valid ? closest.distance_squared : std::numeric_limits<double>::infinity();
}

QSegmentSegmentClosestPoints<double> closest_points_segment_segment(
	const QSegment3<double>& first, const QSegment3<double>& second, double epsilon)
{
	const QVec3d first_direction = first.b - first.a;
	const QVec3d second_direction = second.b - second.a;
	if (length_squared(first_direction) <= epsilon * epsilon ||
			length_squared(second_direction) <= epsilon * epsilon) {
		return closest_points_segment_segment<double>(first, second, epsilon);
	}

	gte::DCPQuery<double, gte::Segment3<double>, gte::Segment3<double>> query;
	const auto result = query(detail::to_gte(first), detail::to_gte(second));
	const QVec3d first_point = detail::from_gte(result.closest[0]);
	const QVec3d second_point = detail::from_gte(result.closest[1]);
	return {
		true,
		result.sqrDistance <= epsilon * epsilon,
		result.parameter[0],
		result.parameter[1],
		first_point,
		second_point,
		(first_point + second_point) * 0.5,
		result.sqrDistance,
	};
}

double segment_segment_distance_squared(const QSegment3<double>& first, const QSegment3<double>& second, double epsilon)
{
	const QSegmentSegmentClosestPoints<double> closest = closest_points_segment_segment(first, second, epsilon);
	return closest.valid ? closest.distance_squared : std::numeric_limits<double>::infinity();
}

QLineLineClosestPoints<double> closest_points_line_line(const QLine3<double>& first, const QLine3<double>& second, double epsilon)
{
	if (length_squared(first.direction) <= epsilon * epsilon || length_squared(second.direction) <= epsilon * epsilon) {
		return {};
	}

	gte::DCPQuery<double, gte::Line3<double>, gte::Line3<double>> query;
	const auto result = query(detail::to_gte(first), detail::to_gte(second));
	const QVec3d first_point = detail::from_gte(result.closest[0]);
	const QVec3d second_point = detail::from_gte(result.closest[1]);
	return {
		true,
		result.sqrDistance <= epsilon * epsilon,
		result.parameter[0],
		result.parameter[1],
		first_point,
		second_point,
		(first_point + second_point) * 0.5,
		result.sqrDistance,
	};
}

double line_line_distance_squared(const QLine3<double>& first, const QLine3<double>& second, double epsilon)
{
	const QLineLineClosestPoints<double> closest = closest_points_line_line(first, second, epsilon);
	return closest.valid ? closest.distance_squared : std::numeric_limits<double>::infinity();
}

} // namespace quader_geometry

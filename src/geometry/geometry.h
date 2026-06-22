////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <limits>
#include <span>
#include <type_traits>
#include <vector>

namespace quader_geometry {

/**
 * Enumerates Axis values used by the geometry layer.
 */
enum class Axis {
	x,
	y,
	z,
};

template<typename T>
/**
 * Represents a Basic Vec2 value used by the geometry predicate and projection layer.
 */
struct BasicVec2 {
	static_assert(std::is_floating_point_v<T>);

	T x = T(0);
	T y = T(0);

	constexpr BasicVec2() = default;
	constexpr BasicVec2(T x_value, T y_value)
		: x(x_value)
		, y(y_value)
	{
	}

	template<typename U>
	explicit constexpr BasicVec2(const BasicVec2<U>& value)
		: x(static_cast<T>(value.x))
		, y(static_cast<T>(value.y))
	{
	}

	template<typename U>
		requires requires(U value) {
			value.x;
			value.y;
		}
	explicit constexpr BasicVec2(const U& value)
		: x(static_cast<T>(value.x))
		, y(static_cast<T>(value.y))
	{
	}
};

template<typename T>
/**
 * Represents a Basic Vec3 value used by the geometry predicate and projection layer.
 */
struct BasicVec3 {
	static_assert(std::is_floating_point_v<T>);

	T x = T(0);
	T y = T(0);
	T z = T(0);

	constexpr BasicVec3() = default;
	constexpr BasicVec3(T x_value, T y_value, T z_value)
		: x(x_value)
		, y(y_value)
		, z(z_value)
	{
	}

	template<typename U>
	explicit constexpr BasicVec3(const BasicVec3<U>& value)
		: x(static_cast<T>(value.x))
		, y(static_cast<T>(value.y))
		, z(static_cast<T>(value.z))
	{
	}

	template<typename U>
		requires requires(U value) {
			value.x;
			value.y;
			value.z;
		}
	explicit constexpr BasicVec3(const U& value)
		: x(static_cast<T>(value.x))
		, y(static_cast<T>(value.y))
		, z(static_cast<T>(value.z))
	{
	}
};

using Vec2 = BasicVec2<float>;
using Vec3 = BasicVec3<float>;
using Vec2f = BasicVec2<float>;
using Vec3f = BasicVec3<float>;
using Vec2d = BasicVec2<double>;
using Vec3d = BasicVec3<double>;
using TriangleIndices = std::array<std::uint32_t, 3>;

template<typename T>
/**
 * Represents a Ray3 value used by the geometry predicate and projection layer.
 */
struct Ray3 {
	BasicVec3<T> origin;
	BasicVec3<T> direction { T(0), T(0), T(1) };

	constexpr Ray3() = default;
	constexpr Ray3(BasicVec3<T> origin_value, BasicVec3<T> direction_value)
		: origin(origin_value)
		, direction(direction_value)
	{
	}

	template<typename U>
	explicit constexpr Ray3(const Ray3<U>& value)
		: origin(value.origin)
		, direction(value.direction)
	{
	}
};

template<typename T>
/**
 * Represents a Line3 value used by the geometry predicate and projection layer.
 */
struct Line3 {
	BasicVec3<T> origin;
	BasicVec3<T> direction { T(0), T(0), T(1) };

	constexpr Line3() = default;
	constexpr Line3(BasicVec3<T> origin_value, BasicVec3<T> direction_value)
		: origin(origin_value)
		, direction(direction_value)
	{
	}

	template<typename U>
	explicit constexpr Line3(const Line3<U>& value)
		: origin(static_cast<BasicVec3<T>>(value.origin))
		, direction(static_cast<BasicVec3<T>>(value.direction))
	{
	}
};

template<typename T>
/**
 * Represents a Plane3 value used by the geometry predicate and projection layer.
 */
struct Plane3 {
	BasicVec3<T> normal { T(0), T(1), T(0) };
	T d = T(0);

	constexpr Plane3() = default;
	constexpr Plane3(BasicVec3<T> normal_value, T d_value)
		: normal(normal_value)
		, d(d_value)
	{
	}

	template<typename U>
	explicit constexpr Plane3(const Plane3<U>& value)
		: normal(value.normal)
		, d(static_cast<T>(value.d))
	{
	}
};

template<typename T>
/**
 * Represents a Segment3 value used by the geometry predicate and projection layer.
 */
struct Segment3 {
	BasicVec3<T> a;
	BasicVec3<T> b;

	constexpr Segment3() = default;
	constexpr Segment3(BasicVec3<T> a_value, BasicVec3<T> b_value)
		: a(a_value)
		, b(b_value)
	{
	}

	template<typename U>
	explicit constexpr Segment3(const Segment3<U>& value)
		: a(value.a)
		, b(value.b)
	{
	}
};

template<typename T>
/**
 * Represents a Triangle3 value used by the geometry predicate and projection layer.
 */
struct Triangle3 {
	BasicVec3<T> a;
	BasicVec3<T> b;
	BasicVec3<T> c;

	constexpr Triangle3() = default;
	constexpr Triangle3(BasicVec3<T> a_value, BasicVec3<T> b_value, BasicVec3<T> c_value)
		: a(a_value)
		, b(b_value)
		, c(c_value)
	{
	}

	template<typename U>
	explicit constexpr Triangle3(const Triangle3<U>& value)
		: a(value.a)
		, b(value.b)
		, c(value.c)
	{
	}
};

template<typename T>
/**
 * Represents an Aabb3 value used by the geometry predicate and projection layer.
 */
struct Aabb3 {
	BasicVec3<T> min;
	BasicVec3<T> max;

	constexpr Aabb3() = default;
	constexpr Aabb3(BasicVec3<T> min_value, BasicVec3<T> max_value)
		: min(min_value)
		, max(max_value)
	{
	}

	template<typename U>
	explicit constexpr Aabb3(const Aabb3<U>& value)
		: min(value.min)
		, max(value.max)
	{
	}
};

template<typename T>
/**
 * Represents a Rect2 value used by the geometry predicate and projection layer.
 */
struct Rect2 {
	BasicVec2<T> min;
	BasicVec2<T> max;

	constexpr Rect2() = default;
	constexpr Rect2(BasicVec2<T> min_value, BasicVec2<T> max_value)
		: min(min_value)
		, max(max_value)
	{
	}

	template<typename U>
	explicit constexpr Rect2(const Rect2<U>& value)
		: min(value.min)
		, max(value.max)
	{
	}
};

using Aabb3f = Aabb3<float>;
using Aabb3d = Aabb3<double>;
using Rect2f = Rect2<float>;
using Rect2d = Rect2<double>;

/**
 * Enumerates PlaneSide values used by the geometry layer.
 */
enum class PlaneSide {
	negative,
	on,
	positive,
};

/**
 * Enumerates PlaneIntersectionType values used by the geometry layer.
 */
enum class PlaneIntersectionType {
	none,
	point,
	coplanar,
};

/**
 * Enumerates SegmentIntersectionKind2 values used by the geometry layer.
 */
enum class SegmentIntersectionKind2 {
	none,
	point,
	overlap,
};

template<typename T>
/**
 * Represents a Plane Basis3 value used by the geometry predicate and projection layer.
 */
struct PlaneBasis3 {
	bool valid = false;
	BasicVec3<T> origin;
	BasicVec3<T> normal;
	BasicVec3<T> u;
	BasicVec3<T> v;
};

template<typename T>
/**
 * Represents a Plane Coordinates3 value used by the geometry predicate and projection layer.
 */
struct PlaneCoordinates3 {
	bool valid = false;
	T u = T(0);
	T v = T(0);
	T signed_distance = T(0);
	BasicVec3<T> projected_point;
};

template<typename T>
/**
 * Represents a Ray Plane Hit value used by the geometry predicate and projection layer.
 */
struct RayPlaneHit {
	bool hit = false;
	T distance = T(0);
	BasicVec3<T> point;
};

template<typename T>
/**
 * Represents a Line Plane Hit value used by the geometry predicate and projection layer.
 */
struct LinePlaneHit {
	PlaneIntersectionType type = PlaneIntersectionType::none;
	T parameter = T(0);
	BasicVec3<T> point;
};

template<typename T>
/**
 * Represents a Segment Plane Hit value used by the geometry predicate and projection layer.
 */
struct SegmentPlaneHit {
	PlaneIntersectionType type = PlaneIntersectionType::none;
	T segment_factor = T(0);
	BasicVec3<T> point;
};

template<typename T>
/**
 * Represents a Three Plane Intersection value used by the geometry predicate and projection layer.
 */
struct ThreePlaneIntersection {
	bool hit = false;
	BasicVec3<T> point;
};

template<typename T>
/**
 * Represents a Segment Intersection2 value used by the geometry predicate and projection layer.
 */
struct SegmentIntersection2 {
	bool hit = false;
	T first_factor = T(0);
	T second_factor = T(0);
	BasicVec2<T> point;
};

template<typename T>
/**
 * Represents a Classified Segment Intersection2 value used by the geometry predicate and projection layer.
 */
struct ClassifiedSegmentIntersection2 {
	bool hit = false;
	SegmentIntersectionKind2 kind = SegmentIntersectionKind2::none;
	std::uint8_t point_count = 0;
	std::array<T, 2> first_factors {};
	std::array<T, 2> second_factors {};
	std::array<BasicVec2<T>, 2> points {};
};

template<typename T>
/**
 * Stores the Dominant Axis Unproject Result data contract used by the geometry predicate and projection layer.
 */
struct DominantAxisUnprojectResult {
	bool valid = false;
	BasicVec3<T> point;
};

template<typename T>
/**
 * Represents a Ray Triangle Hit value used by the geometry predicate and projection layer.
 */
struct RayTriangleHit {
	bool hit = false;
	T distance = T(0);
	T u = T(0);
	T v = T(0);
	T w = T(0);
	BasicVec3<T> point;
};

template<typename T>
/**
 * Represents a Ray Aabb Hit value used by the geometry predicate and projection layer.
 */
struct RayAabbHit {
	bool hit = false;
	T min_distance = T(0);
	T max_distance = T(0);
	BasicVec3<T> point;
};

template<typename T>
/**
 * Stores the Ray Bounded Triangle Query Options data contract used by the geometry predicate and projection layer.
 */
struct RayBoundedTriangleQueryOptions {
	T min_distance = T(0);
	T max_distance = std::numeric_limits<T>::infinity();
	T epsilon = T(0.000001);
};

template<typename T>
/**
 * Represents a Ray Bounded Triangle Hit value used by the geometry predicate and projection layer.
 */
struct RayBoundedTriangleHit {
	bool hit = false;
	std::size_t triangle_index = std::numeric_limits<std::size_t>::max();
	RayAabbHit<T> bounds_hit;
	RayTriangleHit<T> triangle_hit;
};

template<typename T>
/**
 * Represents a Barycentric3 value used by the geometry predicate and projection layer.
 */
struct Barycentric3 {
	bool valid = false;
	T u = T(0);
	T v = T(0);
	T w = T(0);
};

template<typename T>
/**
 * Represents a Closest Ray Point value used by the geometry predicate and projection layer.
 */
struct ClosestRayPoint {
	bool valid = false;
	T ray_distance = T(0);
	BasicVec3<T> point;
	T distance_squared = T(0);
};

template<typename T>
/**
 * Represents a Closest Segment Point2 value used by the geometry predicate and projection layer.
 */
struct ClosestSegmentPoint2 {
	bool valid = false;
	T segment_factor = T(0);
	BasicVec2<T> point;
	T distance_squared = T(0);
};

template<typename T>
/**
 * Represents a Closest Segment Point3 value used by the geometry predicate and projection layer.
 */
struct ClosestSegmentPoint3 {
	bool valid = false;
	T segment_factor = T(0);
	BasicVec3<T> point;
	T distance_squared = T(0);
};

template<typename T>
/**
 * Represents a Segment Ray Closest Points value used by the geometry predicate and projection layer.
 */
struct SegmentRayClosestPoints {
	bool valid = false;
	T segment_factor = T(0);
	T ray_distance = T(0);
	BasicVec3<T> segment_point;
	BasicVec3<T> ray_point;
	T distance_squared = T(0);
};

template<typename T>
/**
 * Represents a Segment Segment Closest Points value used by the geometry predicate and projection layer.
 */
struct SegmentSegmentClosestPoints {
	bool valid = false;
	bool intersects = false;
	T first_factor = T(0);
	T second_factor = T(0);
	BasicVec3<T> first_point;
	BasicVec3<T> second_point;
	BasicVec3<T> midpoint;
	T distance_squared = T(0);
};

template<typename T>
/**
 * Represents a Segment Factor Closest Point3 value used by the geometry predicate and projection layer.
 */
struct SegmentFactorClosestPoint3 {
	bool valid = false;
	T segment_factor = T(0);
	BasicVec3<T> point;
};

template<typename T>
/**
 * Stores the Segment Segment Factor Options data contract used by the geometry predicate and projection layer.
 */
struct SegmentSegmentFactorOptions {
	T length_squared_epsilon = T(0.000000000001);
	T closest_epsilon = T(0.000001);
	T parameter_epsilon = T(0.000001);
	T min_factor = T(0);
	T max_factor = T(1);
};

template<typename T>
/**
 * Represents a Line Line Closest Points value used by the geometry predicate and projection layer.
 */
struct LineLineClosestPoints {
	bool valid = false;
	bool intersects = false;
	T first_parameter = T(0);
	T second_parameter = T(0);
	BasicVec3<T> first_point;
	BasicVec3<T> second_point;
	BasicVec3<T> midpoint;
	T distance_squared = T(0);
};

template<typename T>
[[nodiscard]] constexpr BasicVec2<T> operator+(BasicVec2<T> left, BasicVec2<T> right)
{
	return { left.x + right.x, left.y + right.y };
}

template<typename T>
[[nodiscard]] constexpr BasicVec2<T> operator-(BasicVec2<T> left, BasicVec2<T> right)
{
	return { left.x - right.x, left.y - right.y };
}

template<typename T>
[[nodiscard]] constexpr BasicVec2<T> operator-(BasicVec2<T> value)
{
	return { -value.x, -value.y };
}

template<typename T>
[[nodiscard]] constexpr BasicVec2<T> operator*(BasicVec2<T> value, T scalar)
{
	return { value.x * scalar, value.y * scalar };
}

template<typename T>
[[nodiscard]] constexpr BasicVec2<T> operator/(BasicVec2<T> value, T scalar)
{
	return { value.x / scalar, value.y / scalar };
}

template<typename T>
[[nodiscard]] constexpr BasicVec3<T> operator+(BasicVec3<T> left, BasicVec3<T> right)
{
	return { left.x + right.x, left.y + right.y, left.z + right.z };
}

template<typename T>
[[nodiscard]] constexpr BasicVec3<T> operator-(BasicVec3<T> left, BasicVec3<T> right)
{
	return { left.x - right.x, left.y - right.y, left.z - right.z };
}

template<typename T>
[[nodiscard]] constexpr BasicVec3<T> operator-(BasicVec3<T> value)
{
	return { -value.x, -value.y, -value.z };
}

template<typename T>
[[nodiscard]] constexpr BasicVec3<T> operator*(BasicVec3<T> value, T scalar)
{
	return { value.x * scalar, value.y * scalar, value.z * scalar };
}

template<typename T>
[[nodiscard]] constexpr BasicVec3<T> operator/(BasicVec3<T> value, T scalar)
{
	return { value.x / scalar, value.y / scalar, value.z / scalar };
}

template<typename T>
[[nodiscard]] constexpr BasicVec2<T> component_min(BasicVec2<T> left, BasicVec2<T> right)
{
	return { std::min(left.x, right.x), std::min(left.y, right.y) };
}

template<typename T>
[[nodiscard]] constexpr BasicVec3<T> component_min(BasicVec3<T> left, BasicVec3<T> right)
{
	return { std::min(left.x, right.x), std::min(left.y, right.y), std::min(left.z, right.z) };
}

template<typename T>
[[nodiscard]] constexpr BasicVec2<T> component_max(BasicVec2<T> left, BasicVec2<T> right)
{
	return { std::max(left.x, right.x), std::max(left.y, right.y) };
}

template<typename T>
[[nodiscard]] constexpr BasicVec3<T> component_max(BasicVec3<T> left, BasicVec3<T> right)
{
	return { std::max(left.x, right.x), std::max(left.y, right.y), std::max(left.z, right.z) };
}

template<typename T>
[[nodiscard]] constexpr Aabb3<T> empty_aabb3()
{
	const T infinity = std::numeric_limits<T>::infinity();
	return { { infinity, infinity, infinity }, { -infinity, -infinity, -infinity } };
}

template<typename T>
[[nodiscard]] constexpr bool aabb_is_valid(const Aabb3<T>& aabb)
{
	return aabb.min.x <= aabb.max.x && aabb.min.y <= aabb.max.y && aabb.min.z <= aabb.max.z;
}

template<typename T>
constexpr void aabb_include(Aabb3<T>& aabb, BasicVec3<T> point)
{
	aabb.min = component_min(aabb.min, point);
	aabb.max = component_max(aabb.max, point);
}

template<typename T>
constexpr void aabb_include(Aabb3<T>& aabb, const Aabb3<T>& included)
{
	if (!aabb_is_valid(included)) {
		return;
	}
	aabb_include(aabb, included.min);
	aabb_include(aabb, included.max);
}

template<typename T>
[[nodiscard]] constexpr Aabb3<T> aabb_including(Aabb3<T> aabb, BasicVec3<T> point)
{
	aabb_include(aabb, point);
	return aabb;
}

template<typename T>
[[nodiscard]] constexpr Aabb3<T> aabb_including(Aabb3<T> aabb, const Aabb3<T>& included)
{
	aabb_include(aabb, included);
	return aabb;
}

template<typename T>
[[nodiscard]] constexpr Aabb3<T> aabb_expanded(const Aabb3<T>& aabb, T amount)
{
	if (!aabb_is_valid(aabb)) {
		return aabb;
	}
	const BasicVec3<T> delta { amount, amount, amount };
	return { aabb.min - delta, aabb.max + delta };
}

template<typename T>
[[nodiscard]] constexpr BasicVec3<T> aabb_size(const Aabb3<T>& aabb)
{
	return aabb_is_valid(aabb) ? aabb.max - aabb.min : BasicVec3<T> {};
}

template<typename T>
[[nodiscard]] constexpr BasicVec3<T> aabb_center(const Aabb3<T>& aabb)
{
	return aabb_is_valid(aabb) ? (aabb.min + aabb.max) * T(0.5) : BasicVec3<T> {};
}

template<typename T>
[[nodiscard]] Aabb3<T> aabb_from_points(std::span<const BasicVec3<T>> points)
{
	Aabb3<T> bounds = empty_aabb3<T>();
	for (const BasicVec3<T> point : points) {
		aabb_include(bounds, point);
	}
	return bounds;
}

template<typename T>
[[nodiscard]] constexpr bool aabb_contains_point(const Aabb3<T>& aabb, BasicVec3<T> point)
{
	return aabb_is_valid(aabb) &&
		point.x >= aabb.min.x && point.x <= aabb.max.x &&
		point.y >= aabb.min.y && point.y <= aabb.max.y &&
		point.z >= aabb.min.z && point.z <= aabb.max.z;
}

template<typename T>
[[nodiscard]] constexpr bool aabb_overlaps(const Aabb3<T>& left, const Aabb3<T>& right)
{
	return aabb_is_valid(left) && aabb_is_valid(right) &&
		left.min.x <= right.max.x && left.max.x >= right.min.x &&
		left.min.y <= right.max.y && left.max.y >= right.min.y &&
		left.min.z <= right.max.z && left.max.z >= right.min.z;
}

template<typename T>
[[nodiscard]] constexpr BasicVec3<T> aabb_closest_point(const Aabb3<T>& aabb, BasicVec3<T> point)
{
	if (!aabb_is_valid(aabb)) {
		return {};
	}
	return {
		std::clamp(point.x, aabb.min.x, aabb.max.x),
		std::clamp(point.y, aabb.min.y, aabb.max.y),
		std::clamp(point.z, aabb.min.z, aabb.max.z),
	};
}

template<typename T>
[[nodiscard]] constexpr T aabb_distance_squared_to_point(const Aabb3<T>& aabb, BasicVec3<T> point)
{
	if (!aabb_is_valid(aabb)) {
		return std::numeric_limits<T>::infinity();
	}
	return length_squared(point - aabb_closest_point(aabb, point));
}

template<typename T>
[[nodiscard]] Axis aabb_longest_axis(const Aabb3<T>& aabb)
{
	const BasicVec3<T> size = aabb_size(aabb);
	const T ax = std::abs(size.x);
	const T ay = std::abs(size.y);
	const T az = std::abs(size.z);
	if (ax >= ay && ax >= az) {
		return Axis::x;
	}
	if (ay >= az) {
		return Axis::y;
	}
	return Axis::z;
}

template<typename T>
[[nodiscard]] constexpr Rect2<T> empty_rect2()
{
	const T infinity = std::numeric_limits<T>::infinity();
	return { { infinity, infinity }, { -infinity, -infinity } };
}

template<typename T>
[[nodiscard]] constexpr bool rect_is_valid(const Rect2<T>& rect)
{
	return rect.min.x <= rect.max.x && rect.min.y <= rect.max.y;
}

template<typename T>
constexpr void rect_include(Rect2<T>& rect, BasicVec2<T> point)
{
	rect.min = component_min(rect.min, point);
	rect.max = component_max(rect.max, point);
}

template<typename T>
constexpr void rect_include(Rect2<T>& rect, const Rect2<T>& included)
{
	if (!rect_is_valid(included)) {
		return;
	}
	rect_include(rect, included.min);
	rect_include(rect, included.max);
}

template<typename T>
[[nodiscard]] constexpr Rect2<T> rect_including(Rect2<T> rect, BasicVec2<T> point)
{
	rect_include(rect, point);
	return rect;
}

template<typename T>
[[nodiscard]] constexpr Rect2<T> rect_including(Rect2<T> rect, const Rect2<T>& included)
{
	rect_include(rect, included);
	return rect;
}

template<typename T>
[[nodiscard]] constexpr Rect2<T> rect_expanded(const Rect2<T>& rect, T amount)
{
	if (!rect_is_valid(rect)) {
		return rect;
	}
	const BasicVec2<T> delta { amount, amount };
	return { rect.min - delta, rect.max + delta };
}

template<typename T>
[[nodiscard]] constexpr BasicVec2<T> rect_size(const Rect2<T>& rect)
{
	return rect_is_valid(rect) ? rect.max - rect.min : BasicVec2<T> {};
}

template<typename T>
[[nodiscard]] constexpr BasicVec2<T> rect_center(const Rect2<T>& rect)
{
	return rect_is_valid(rect) ? (rect.min + rect.max) * T(0.5) : BasicVec2<T> {};
}

template<typename T>
[[nodiscard]] constexpr bool rect_contains_point(const Rect2<T>& rect, BasicVec2<T> point)
{
	return rect_is_valid(rect) &&
		point.x >= rect.min.x && point.x <= rect.max.x &&
		point.y >= rect.min.y && point.y <= rect.max.y;
}

template<typename T>
[[nodiscard]] constexpr bool rect_overlaps(const Rect2<T>& left, const Rect2<T>& right)
{
	return rect_is_valid(left) && rect_is_valid(right) &&
		left.min.x <= right.max.x && left.max.x >= right.min.x &&
		left.min.y <= right.max.y && left.max.y >= right.min.y;
}

template<typename T>
[[nodiscard]] constexpr T dot(BasicVec2<T> left, BasicVec2<T> right)
{
	return left.x * right.x + left.y * right.y;
}

template<typename T>
[[nodiscard]] constexpr T dot(BasicVec3<T> left, BasicVec3<T> right)
{
	return left.x * right.x + left.y * right.y + left.z * right.z;
}

template<typename T>
[[nodiscard]] constexpr BasicVec3<T> cross(BasicVec3<T> left, BasicVec3<T> right)
{
	return {
		left.y * right.z - left.z * right.y,
		left.z * right.x - left.x * right.z,
		left.x * right.y - left.y * right.x,
	};
}

template<typename T>
[[nodiscard]] constexpr T length_squared(BasicVec2<T> value)
{
	return dot(value, value);
}

template<typename T>
[[nodiscard]] constexpr T length_squared(BasicVec3<T> value)
{
	return dot(value, value);
}

template<typename T>
[[nodiscard]] T length(BasicVec2<T> value)
{
	return std::sqrt(length_squared(value));
}

template<typename T>
[[nodiscard]] T length(BasicVec3<T> value)
{
	return std::sqrt(length_squared(value));
}

template<typename T>
[[nodiscard]] BasicVec2<T> normalize_or_zero(BasicVec2<T> value, T epsilon = std::numeric_limits<T>::epsilon())
{
	const T squared = length_squared(value);
	if (squared <= epsilon * epsilon) {
		return {};
	}
	return value / std::sqrt(squared);
}

template<typename T>
[[nodiscard]] BasicVec3<T> normalize_or_zero(BasicVec3<T> value, T epsilon = std::numeric_limits<T>::epsilon())
{
	const T squared = length_squared(value);
	if (squared <= epsilon * epsilon) {
		return {};
	}
	return value / std::sqrt(squared);
}

template<typename T>
[[nodiscard]] Plane3<T> plane_from_point_normal(BasicVec3<T> point, BasicVec3<T> normal, T epsilon = std::numeric_limits<T>::epsilon())
{
	const BasicVec3<T> normalized = normalize_or_zero(normal, epsilon);
	return { normalized, dot(normalized, point) };
}

template<typename T>
[[nodiscard]] Plane3<T> normalize_plane(const Plane3<T>& plane, T epsilon = std::numeric_limits<T>::epsilon())
{
	const T squared = length_squared(plane.normal);
	if (squared <= epsilon * epsilon) {
		return { {}, T(0) };
	}
	const T inverse_length = T(1) / std::sqrt(squared);
	return { plane.normal * inverse_length, plane.d * inverse_length };
}

template<typename T>
[[nodiscard]] T signed_distance_to_plane(BasicVec3<T> point, const Plane3<T>& plane)
{
	return dot(plane.normal, point) - plane.d;
}

template<typename T>
[[nodiscard]] PlaneSide plane_side(BasicVec3<T> point, const Plane3<T>& plane, T epsilon = T(0.000001))
{
	const T distance = signed_distance_to_plane(point, plane);
	if (distance > epsilon) {
		return PlaneSide::positive;
	}
	if (distance < -epsilon) {
		return PlaneSide::negative;
	}
	return PlaneSide::on;
}

template<typename T>
[[nodiscard]] bool planes_nearly_equal(const Plane3<T>& left, const Plane3<T>& right, T epsilon = T(0.000001))
{
	const Plane3<T> normalized_left = normalize_plane(left, epsilon);
	const Plane3<T> normalized_right = normalize_plane(right, epsilon);
	return length_squared(normalized_left.normal - normalized_right.normal) <= epsilon * epsilon &&
		std::abs(normalized_left.d - normalized_right.d) <= epsilon;
}

template<typename T>
[[nodiscard]] BasicVec3<T> project_point_to_plane(BasicVec3<T> point, const Plane3<T>& plane)
{
	return point - plane.normal * signed_distance_to_plane(point, plane);
}

template<typename T>
[[nodiscard]] PlaneBasis3<T> make_plane_basis(
	BasicVec3<T> origin, BasicVec3<T> normal, BasicVec3<T> preferred_u = {}, T epsilon = T(0.000001))
{
	const BasicVec3<T> normalized_normal = normalize_or_zero(normal, epsilon);
	if (length_squared(normalized_normal) <= epsilon * epsilon) {
		return {};
	}

	BasicVec3<T> candidate_u = preferred_u - normalized_normal * dot(preferred_u, normalized_normal);
	if (length_squared(candidate_u) <= epsilon * epsilon) {
		const T ax = std::abs(normalized_normal.x);
		const T ay = std::abs(normalized_normal.y);
		const T az = std::abs(normalized_normal.z);
		BasicVec3<T> reference { T(1), T(0), T(0) };
		if (ay <= ax && ay <= az) {
			reference = { T(0), T(1), T(0) };
		} else if (az <= ax && az <= ay) {
			reference = { T(0), T(0), T(1) };
		}
		candidate_u = reference - normalized_normal * dot(reference, normalized_normal);
	}

	const BasicVec3<T> u = normalize_or_zero(candidate_u, epsilon);
	if (length_squared(u) <= epsilon * epsilon) {
		return {};
	}
	const BasicVec3<T> v = normalize_or_zero(cross(normalized_normal, u), epsilon);
	if (length_squared(v) <= epsilon * epsilon) {
		return {};
	}
	return { true, origin, normalized_normal, u, v };
}

template<typename T>
[[nodiscard]] PlaneBasis3<T> make_plane_basis(const Plane3<T>& plane, BasicVec3<T> preferred_u = {}, T epsilon = T(0.000001))
{
	const Plane3<T> normalized = normalize_plane(plane, epsilon);
	if (length_squared(normalized.normal) <= epsilon * epsilon) {
		return {};
	}
	return make_plane_basis(normalized.normal * normalized.d, normalized.normal, preferred_u, epsilon);
}

template<typename T>
[[nodiscard]] PlaneCoordinates3<T> plane_coordinates(BasicVec3<T> point, const PlaneBasis3<T>& basis)
{
	if (!basis.valid) {
		return {};
	}
	const BasicVec3<T> delta = point - basis.origin;
	const T signed_distance = dot(delta, basis.normal);
	return {
		true,
		dot(delta, basis.u),
		dot(delta, basis.v),
		signed_distance,
		point - basis.normal * signed_distance,
	};
}

template<typename T>
[[nodiscard]] BasicVec3<T> plane_point_from_coordinates(const PlaneBasis3<T>& basis, T u, T v)
{
	return basis.origin + basis.u * u + basis.v * v;
}

template<typename T>
[[nodiscard]] Axis dominant_axis(BasicVec3<T> normal)
{
	const T ax = std::abs(normal.x);
	const T ay = std::abs(normal.y);
	const T az = std::abs(normal.z);
	if (ax >= ay && ax >= az) {
		return Axis::x;
	}
	if (ay >= az) {
		return Axis::y;
	}
	return Axis::z;
}

template<typename T>
[[nodiscard]] BasicVec2<T> project_dominant_axis(BasicVec3<T> point, Axis dropped_axis)
{
	if (dropped_axis == Axis::x) {
		return { point.y, point.z };
	}
	if (dropped_axis == Axis::y) {
		return { point.x, point.z };
	}
	return { point.x, point.y };
}

template<typename T>
[[nodiscard]] BasicVec2<T> project_dominant_axis(BasicVec3<T> point, BasicVec3<T> normal)
{
	return project_dominant_axis(point, dominant_axis(normal));
}

template<typename T>
[[nodiscard]] DominantAxisUnprojectResult<T> unproject_dominant_axis_point_to_plane(
	BasicVec2<T> point, const Plane3<T>& plane, Axis dropped_axis, T epsilon = T(0.000001))
{
	if (dropped_axis == Axis::x) {
		if (std::abs(plane.normal.x) <= epsilon) {
			return {};
		}
		return { true, { (plane.d - plane.normal.y * point.x - plane.normal.z * point.y) / plane.normal.x, point.x, point.y } };
	}
	if (dropped_axis == Axis::y) {
		if (std::abs(plane.normal.y) <= epsilon) {
			return {};
		}
		return { true, { point.x, (plane.d - plane.normal.x * point.x - plane.normal.z * point.y) / plane.normal.y, point.y } };
	}
	if (std::abs(plane.normal.z) <= epsilon) {
		return {};
	}
	return { true, { point.x, point.y, (plane.d - plane.normal.x * point.x - plane.normal.y * point.y) / plane.normal.z } };
}

template<typename T>
[[nodiscard]] ClosestSegmentPoint2<T> closest_point_on_segment(BasicVec2<T> point, BasicVec2<T> segment_a, BasicVec2<T> segment_b,
	T epsilon = T(0.000001))
{
	const BasicVec2<T> segment_direction = segment_b - segment_a;
	const T segment_length_squared = length_squared(segment_direction);
	if (segment_length_squared <= epsilon * epsilon) {
		return { true, T(0), segment_a, length_squared(point - segment_a) };
	}

	const T segment_factor = std::clamp(dot(point - segment_a, segment_direction) / segment_length_squared, T(0), T(1));
	const BasicVec2<T> closest = segment_a + segment_direction * segment_factor;
	return { true, segment_factor, closest, length_squared(point - closest) };
}

template<typename T>
[[nodiscard]] T point_segment_distance_squared(BasicVec2<T> point, BasicVec2<T> segment_a, BasicVec2<T> segment_b, T epsilon = T(0.000001))
{
	const ClosestSegmentPoint2<T> closest = closest_point_on_segment(point, segment_a, segment_b, epsilon);
	return closest.valid ? closest.distance_squared : std::numeric_limits<T>::infinity();
}

[[nodiscard]] ClosestSegmentPoint2<double> closest_point_on_segment(Vec2d point, Vec2d segment_a, Vec2d segment_b,
	double epsilon = 0.000001);
[[nodiscard]] double point_segment_distance_squared(Vec2d point, Vec2d segment_a, Vec2d segment_b, double epsilon = 0.000001);

[[nodiscard]] ClosestSegmentPoint3<double> closest_point_on_segment(Vec3d point, const Segment3<double>& segment,
	double epsilon = 0.000001);
[[nodiscard]] double point_segment_distance_squared(Vec3d point, const Segment3<double>& segment, double epsilon = 0.000001);

template<typename T>
[[nodiscard]] ClosestSegmentPoint3<T> closest_point_on_segment(BasicVec3<T> point, const Segment3<T>& segment,
	T epsilon = T(0.000001))
{
	const BasicVec3<T> segment_direction = segment.b - segment.a;
	const T segment_length_squared = length_squared(segment_direction);
	if (segment_length_squared <= epsilon * epsilon) {
		return { true, T(0), segment.a, length_squared(point - segment.a) };
	}

	const T segment_factor = std::clamp(dot(point - segment.a, segment_direction) / segment_length_squared, T(0), T(1));
	const BasicVec3<T> closest = segment.a + segment_direction * segment_factor;
	return { true, segment_factor, closest, length_squared(point - closest) };
}

template<typename T>
[[nodiscard]] T point_segment_distance_squared(BasicVec3<T> point, const Segment3<T>& segment, T epsilon = T(0.000001))
{
	const ClosestSegmentPoint3<T> closest = closest_point_on_segment(point, segment, epsilon);
	return closest.valid ? closest.distance_squared : std::numeric_limits<T>::infinity();
}

namespace detail {

template<typename T>
[[nodiscard]] constexpr T cross_2d(BasicVec2<T> left, BasicVec2<T> right)
{
	return left.x * right.y - left.y * right.x;
}

template<typename T>
[[nodiscard]] constexpr T signed_area_twice_2d(BasicVec2<T> a, BasicVec2<T> b, BasicVec2<T> c)
{
	return ((b.x - a.x) * (c.y - a.y)) - ((b.y - a.y) * (c.x - a.x));
}

template<typename T>
[[nodiscard]] constexpr T clamped_unit_factor(T value)
{
	return std::clamp(value, T(0), T(1));
}

template<typename T>
[[nodiscard]] T segment_factor_2d(BasicVec2<T> point, BasicVec2<T> segment_a, BasicVec2<T> segment_b, T epsilon)
{
	const BasicVec2<T> segment_direction = segment_b - segment_a;
	const T segment_length_squared = length_squared(segment_direction);
	if (segment_length_squared <= epsilon * epsilon) {
		return T(0);
	}
	return clamped_unit_factor(dot(point - segment_a, segment_direction) / segment_length_squared);
}

template<typename T>
[[nodiscard]] bool point_on_segment_2d_local(BasicVec2<T> point, BasicVec2<T> segment_a, BasicVec2<T> segment_b, T epsilon)
{
	if (std::abs(signed_area_twice_2d(segment_a, segment_b, point)) > epsilon) {
		return false;
	}
	return point.x >= std::min(segment_a.x, segment_b.x) - epsilon &&
		point.x <= std::max(segment_a.x, segment_b.x) + epsilon &&
		point.y >= std::min(segment_a.y, segment_b.y) - epsilon &&
		point.y <= std::max(segment_a.y, segment_b.y) + epsilon;
}

template<typename T>
[[nodiscard]] ClassifiedSegmentIntersection2<T> make_segment_intersection_point_2d(
	T first_factor, T second_factor, BasicVec2<T> point)
{
	ClassifiedSegmentIntersection2<T> result;
	result.hit = true;
	result.kind = SegmentIntersectionKind2::point;
	result.point_count = 1;
	result.first_factors[0] = clamped_unit_factor(first_factor);
	result.first_factors[1] = result.first_factors[0];
	result.second_factors[0] = clamped_unit_factor(second_factor);
	result.second_factors[1] = result.second_factors[0];
	result.points[0] = point;
	result.points[1] = point;
	return result;
}

template<typename T>
[[nodiscard]] ClassifiedSegmentIntersection2<T> classify_segments_2d_local(
	BasicVec2<T> a, BasicVec2<T> b, BasicVec2<T> c, BasicVec2<T> d, T epsilon)
{
	const BasicVec2<T> first_direction = b - a;
	const BasicVec2<T> second_direction = d - c;
	const T first_length_squared = length_squared(first_direction);
	const T second_length_squared = length_squared(second_direction);
	const bool first_degenerate = first_length_squared <= epsilon * epsilon;
	const bool second_degenerate = second_length_squared <= epsilon * epsilon;

	if (first_degenerate && second_degenerate) {
		if (length_squared(a - c) <= epsilon * epsilon) {
			return make_segment_intersection_point_2d<T>(T(0), T(0), (a + c) * T(0.5));
		}
		return {};
	}

	if (first_degenerate) {
		if (!point_on_segment_2d_local(a, c, d, epsilon)) {
			return {};
		}
		return make_segment_intersection_point_2d<T>(T(0), segment_factor_2d(a, c, d, epsilon), a);
	}

	if (second_degenerate) {
		if (!point_on_segment_2d_local(c, a, b, epsilon)) {
			return {};
		}
		return make_segment_intersection_point_2d<T>(segment_factor_2d(c, a, b, epsilon), T(0), c);
	}

	const T denominator = cross_2d(first_direction, second_direction);
	const BasicVec2<T> delta = c - a;
	if (std::abs(denominator) > epsilon) {
		const T first_factor = cross_2d(delta, second_direction) / denominator;
		const T second_factor = cross_2d(delta, first_direction) / denominator;
		if (first_factor < -epsilon || first_factor > T(1) + epsilon ||
				second_factor < -epsilon || second_factor > T(1) + epsilon) {
			return {};
		}
		const T clamped_first_factor = clamped_unit_factor(first_factor);
		return make_segment_intersection_point_2d<T>(
			clamped_first_factor,
			clamped_unit_factor(second_factor),
			a + first_direction * clamped_first_factor);
	}

	if (std::abs(cross_2d(delta, first_direction)) > epsilon) {
		return {};
	}

	T second_start_on_first = dot(c - a, first_direction) / first_length_squared;
	T second_end_on_first = dot(d - a, first_direction) / first_length_squared;
	if (second_start_on_first > second_end_on_first) {
		std::swap(second_start_on_first, second_end_on_first);
	}

	const T overlap_start = std::max(T(0), second_start_on_first);
	const T overlap_end = std::min(T(1), second_end_on_first);
	if (overlap_start > overlap_end + epsilon) {
		return {};
	}

	if (std::abs(overlap_start - overlap_end) <= epsilon) {
		const T first_factor = clamped_unit_factor((overlap_start + overlap_end) * T(0.5));
		const BasicVec2<T> point = a + first_direction * first_factor;
		return make_segment_intersection_point_2d<T>(first_factor, segment_factor_2d(point, c, d, epsilon), point);
	}

	ClassifiedSegmentIntersection2<T> result;
	result.hit = true;
	result.kind = SegmentIntersectionKind2::overlap;
	result.point_count = 2;
	result.first_factors[0] = clamped_unit_factor(overlap_start);
	result.first_factors[1] = clamped_unit_factor(overlap_end);
	result.points[0] = a + first_direction * result.first_factors[0];
	result.points[1] = a + first_direction * result.first_factors[1];
	result.second_factors[0] = segment_factor_2d(result.points[0], c, d, epsilon);
	result.second_factors[1] = segment_factor_2d(result.points[1], c, d, epsilon);
	return result;
}

} // namespace detail

template<typename T>
[[nodiscard]] constexpr T triangle_signed_area(BasicVec2<T> a, BasicVec2<T> b, BasicVec2<T> c)
{
	return detail::signed_area_twice_2d(a, b, c) * T(0.5);
}

template<typename T>
[[nodiscard]] T triangle_area_abs(BasicVec2<T> a, BasicVec2<T> b, BasicVec2<T> c)
{
	return std::abs(triangle_signed_area(a, b, c));
}

template<typename T>
[[nodiscard]] bool point_on_segment_2d(BasicVec2<T> point, BasicVec2<T> segment_a, BasicVec2<T> segment_b, T epsilon = T(0.000001))
{
	if (std::abs(detail::signed_area_twice_2d(segment_a, segment_b, point)) > epsilon) {
		return false;
	}
	return point.x >= std::min(segment_a.x, segment_b.x) - epsilon &&
		point.x <= std::max(segment_a.x, segment_b.x) + epsilon &&
		point.y >= std::min(segment_a.y, segment_b.y) - epsilon &&
		point.y <= std::max(segment_a.y, segment_b.y) + epsilon;
}

template<typename T>
[[nodiscard]] bool segments_intersect_2d(BasicVec2<T> a, BasicVec2<T> b, BasicVec2<T> c, BasicVec2<T> d, T epsilon = T(0.000001))
{
	const T ab_c = detail::signed_area_twice_2d(a, b, c);
	const T ab_d = detail::signed_area_twice_2d(a, b, d);
	const T cd_a = detail::signed_area_twice_2d(c, d, a);
	const T cd_b = detail::signed_area_twice_2d(c, d, b);

	if (((ab_c > epsilon && ab_d < -epsilon) || (ab_c < -epsilon && ab_d > epsilon)) &&
		((cd_a > epsilon && cd_b < -epsilon) || (cd_a < -epsilon && cd_b > epsilon))) {
		return true;
	}

	return point_on_segment_2d(c, a, b, epsilon) ||
		point_on_segment_2d(d, a, b, epsilon) ||
		point_on_segment_2d(a, c, d, epsilon) ||
		point_on_segment_2d(b, c, d, epsilon);
}

[[nodiscard]] ClassifiedSegmentIntersection2<double> intersect_segments_2d(
	Vec2d a, Vec2d b, Vec2d c, Vec2d d, double epsilon = 0.000001);

template<typename T>
[[nodiscard]] ClassifiedSegmentIntersection2<T> intersect_segments_2d(
	BasicVec2<T> a, BasicVec2<T> b, BasicVec2<T> c, BasicVec2<T> d, T epsilon = T(0.000001))
{
	return detail::classify_segments_2d_local(a, b, c, d, epsilon);
}

template<typename T>
[[nodiscard]] SegmentIntersection2<T> proper_segment_intersection_2d(
	BasicVec2<T> a, BasicVec2<T> b, BasicVec2<T> c, BasicVec2<T> d, T epsilon = T(0.000001))
{
	const BasicVec2<T> first_direction = b - a;
	const BasicVec2<T> second_direction = d - c;
	const T denominator = detail::cross_2d(first_direction, second_direction);
	if (std::abs(denominator) <= epsilon) {
		return {};
	}

	const BasicVec2<T> delta = c - a;
	const T first_factor = detail::cross_2d(delta, second_direction) / denominator;
	const T second_factor = detail::cross_2d(delta, first_direction) / denominator;
	if (first_factor <= epsilon || first_factor >= T(1) - epsilon ||
			second_factor <= epsilon || second_factor >= T(1) - epsilon) {
		return {};
	}

	return { true, first_factor, second_factor, a + first_direction * first_factor };
}

template<typename T>
[[nodiscard]] bool point_in_or_on_polygon_2d(BasicVec2<T> point, std::span<const BasicVec2<T>> polygon, T epsilon = T(0.000001))
{
	if (polygon.size() < 3U) {
		return false;
	}

	bool inside = false;
	for (std::size_t index = 0, previous = polygon.size() - 1U; index < polygon.size(); previous = index++) {
		const BasicVec2<T> a = polygon[previous];
		const BasicVec2<T> b = polygon[index];
		if (point_on_segment_2d(point, a, b, epsilon)) {
			return true;
		}
		const bool crosses = ((a.y > point.y) != (b.y > point.y)) &&
			(point.x < ((b.x - a.x) * (point.y - a.y) / (b.y - a.y)) + a.x);
		if (crosses) {
			inside = !inside;
		}
	}
	return inside;
}

template<typename T>
[[nodiscard]] T polygon_signed_area(std::span<const BasicVec2<T>> points)
{
	if (points.size() < 3) {
		return T(0);
	}
	T area = T(0);
	for (std::size_t index = 0; index < points.size(); ++index) {
		const BasicVec2<T> current = points[index];
		const BasicVec2<T> next = points[(index + 1U) % points.size()];
		area += current.x * next.y - next.x * current.y;
	}
	return area * T(0.5);
}

[[nodiscard]] std::vector<TriangleIndices> triangulate_projected_polygon(
	std::span<const Vec2d> points,
	double epsilon = 0.000001,
	double coverage_tolerance_ratio = 0.001);

template<typename T>
[[nodiscard]] std::vector<TriangleIndices> triangulate_projected_polygon(
	std::span<const BasicVec2<T>> points,
	double epsilon = 0.000001,
	double coverage_tolerance_ratio = 0.001)
{
	std::vector<Vec2d> double_points;
	double_points.reserve(points.size());
	for (const BasicVec2<T> point : points) {
		double_points.push_back(Vec2d { static_cast<double>(point.x), static_cast<double>(point.y) });
	}
	return triangulate_projected_polygon(std::span<const Vec2d>(double_points), epsilon, coverage_tolerance_ratio);
}

template<typename T>
[[nodiscard]] ThreePlaneIntersection<T> intersect_three_planes(
	const Plane3<T>& first, const Plane3<T>& second, const Plane3<T>& third, T epsilon = T(0.000001))
{
	const BasicVec3<T> second_cross_third = cross(second.normal, third.normal);
	const T denominator = dot(first.normal, second_cross_third);
	if (std::abs(denominator) <= epsilon) {
		return {};
	}

	const BasicVec3<T> point =
		(second_cross_third * first.d +
			cross(third.normal, first.normal) * second.d +
			cross(first.normal, second.normal) * third.d) /
		denominator;
	return { true, point };
}

template<typename T>
[[nodiscard]] RayPlaneHit<T> intersect_ray_plane(const Ray3<T>& ray, const Plane3<T>& plane, T epsilon = T(0.000001))
{
	const T denominator = dot(plane.normal, ray.direction);
	if (std::abs(denominator) <= epsilon) {
		return {};
	}

	const T distance = (plane.d - dot(plane.normal, ray.origin)) / denominator;
	if (distance < T(0)) {
		return {};
	}

	return { true, distance, ray.origin + ray.direction * distance };
}

[[nodiscard]] RayPlaneHit<float> intersect_ray_plane(
	const Ray3<float>& ray, const Plane3<float>& plane, float epsilon = 0.000001F);

template<typename T>
[[nodiscard]] LinePlaneHit<T> intersect_line_plane(const Line3<T>& line, const Plane3<T>& plane, T epsilon = T(0.000001))
{
	const Plane3<T> normalized_plane = normalize_plane(plane, epsilon);
	if (length_squared(normalized_plane.normal) <= epsilon * epsilon ||
			length_squared(line.direction) <= epsilon * epsilon) {
		return {};
	}

	const T denominator = dot(normalized_plane.normal, line.direction);
	const T origin_distance = signed_distance_to_plane(line.origin, normalized_plane);
	if (std::abs(denominator) <= epsilon) {
		return {
			std::abs(origin_distance) <= epsilon ? PlaneIntersectionType::coplanar : PlaneIntersectionType::none,
			T(0),
			line.origin,
		};
	}

	const T parameter = -origin_distance / denominator;
	return { PlaneIntersectionType::point, parameter, line.origin + line.direction * parameter };
}

template<typename T>
[[nodiscard]] SegmentPlaneHit<T> intersect_segment_plane(const Segment3<T>& segment, const Plane3<T>& plane, T epsilon = T(0.000001))
{
	const Plane3<T> normalized_plane = normalize_plane(plane, epsilon);
	if (length_squared(normalized_plane.normal) <= epsilon * epsilon) {
		return {};
	}

	const T first_distance = signed_distance_to_plane(segment.a, normalized_plane);
	const T second_distance = signed_distance_to_plane(segment.b, normalized_plane);
	const bool first_on_plane = std::abs(first_distance) <= epsilon;
	const bool second_on_plane = std::abs(second_distance) <= epsilon;
	const BasicVec3<T> segment_direction = segment.b - segment.a;
	if (length_squared(segment_direction) <= epsilon * epsilon) {
		return first_on_plane ? SegmentPlaneHit<T> { PlaneIntersectionType::point, T(0), segment.a } : SegmentPlaneHit<T> {};
	}
	if (first_on_plane && second_on_plane) {
		return { PlaneIntersectionType::coplanar, T(0), segment.a };
	}
	if (first_on_plane) {
		return { PlaneIntersectionType::point, T(0), segment.a };
	}
	if (second_on_plane) {
		return { PlaneIntersectionType::point, T(1), segment.b };
	}
	if ((first_distance > T(0) && second_distance > T(0)) ||
			(first_distance < T(0) && second_distance < T(0))) {
		return {};
	}

	const T segment_factor = first_distance / (first_distance - second_distance);
	return { PlaneIntersectionType::point, segment_factor, segment.a + segment_direction * segment_factor };
}

template<typename T>
[[nodiscard]] BasicVec3<T> triangle_area_vector(const Triangle3<T>& triangle)
{
	return cross(triangle.b - triangle.a, triangle.c - triangle.a) * T(0.5);
}

template<typename T>
[[nodiscard]] T triangle_area(const Triangle3<T>& triangle)
{
	return length(triangle_area_vector(triangle));
}

template<typename T>
[[nodiscard]] BasicVec3<T> triangle_unit_normal(const Triangle3<T>& triangle, T epsilon = T(0.000001))
{
	return normalize_or_zero(cross(triangle.b - triangle.a, triangle.c - triangle.a), epsilon);
}

template<typename T>
[[nodiscard]] Aabb3<T> triangle_aabb(const Triangle3<T>& triangle)
{
	Aabb3<T> bounds = empty_aabb3<T>();
	aabb_include(bounds, triangle.a);
	aabb_include(bounds, triangle.b);
	aabb_include(bounds, triangle.c);
	return bounds;
}

template<typename T>
[[nodiscard]] Barycentric3<T> barycentric_coordinates(BasicVec3<T> point, const Triangle3<T>& triangle, T epsilon = T(0.000001))
{
	const BasicVec3<T> v0 = triangle.b - triangle.a;
	const BasicVec3<T> v1 = triangle.c - triangle.a;
	const BasicVec3<T> v2 = point - triangle.a;
	const T d00 = dot(v0, v0);
	const T d01 = dot(v0, v1);
	const T d11 = dot(v1, v1);
	const T d20 = dot(v2, v0);
	const T d21 = dot(v2, v1);
	const T denominator = d00 * d11 - d01 * d01;
	if (std::abs(denominator) <= epsilon * epsilon) {
		return {};
	}

	const T v = (d11 * d20 - d01 * d21) / denominator;
	const T w = (d00 * d21 - d01 * d20) / denominator;
	const T u = T(1) - v - w;
	return { true, u, v, w };
}

template<typename T>
[[nodiscard]] bool point_in_triangle(
	BasicVec3<T> point, const Triangle3<T>& triangle, T barycentric_epsilon = T(0.000001), T plane_epsilon = T(0.000001))
{
	const BasicVec3<T> normal = triangle_unit_normal(triangle, plane_epsilon);
	if (length_squared(normal) <= plane_epsilon * plane_epsilon) {
		return false;
	}
	const Plane3<T> plane = plane_from_point_normal(triangle.a, normal, plane_epsilon);
	if (std::abs(signed_distance_to_plane(point, plane)) > plane_epsilon) {
		return false;
	}

	const Barycentric3<T> coordinates = barycentric_coordinates(point, triangle, barycentric_epsilon);
	return coordinates.valid &&
		coordinates.u >= -barycentric_epsilon &&
		coordinates.v >= -barycentric_epsilon &&
		coordinates.w >= -barycentric_epsilon &&
		coordinates.u <= T(1) + barycentric_epsilon &&
		coordinates.v <= T(1) + barycentric_epsilon &&
		coordinates.w <= T(1) + barycentric_epsilon;
}

template<typename T>
[[nodiscard]] RayTriangleHit<T> intersect_ray_triangle(const Ray3<T>& ray, const Triangle3<T>& triangle, T epsilon = T(0.000001))
{
	const BasicVec3<T> edge1 = triangle.b - triangle.a;
	const BasicVec3<T> edge2 = triangle.c - triangle.a;
	const BasicVec3<T> pvec = cross(ray.direction, edge2);
	const T determinant = dot(edge1, pvec);
	if (std::abs(determinant) <= epsilon) {
		return {};
	}

	const T inverse_determinant = T(1) / determinant;
	const BasicVec3<T> tvec = ray.origin - triangle.a;
	const T u = dot(tvec, pvec) * inverse_determinant;
	if (u < T(0) || u > T(1)) {
		return {};
	}

	const BasicVec3<T> qvec = cross(tvec, edge1);
	const T v = dot(ray.direction, qvec) * inverse_determinant;
	if (v < T(0) || u + v > T(1)) {
		return {};
	}

	const T distance = dot(edge2, qvec) * inverse_determinant;
	if (distance < T(0)) {
		return {};
	}

	return { true, distance, u, v, T(1) - u - v, ray.origin + ray.direction * distance };
}

[[nodiscard]] RayTriangleHit<double> intersect_ray_triangle(const Ray3<double>& ray, const Triangle3<double>& triangle,
	double epsilon = 0.000001);
[[nodiscard]] RayAabbHit<double> intersect_ray_aabb(const Ray3<double>& ray, const Aabb3<double>& aabb,
	double epsilon = 0.000001);

template<typename T>
[[nodiscard]] RayAabbHit<T> intersect_ray_aabb(const Ray3<T>& ray, const Aabb3<T>& aabb, T epsilon = T(0.000001))
{
	if (!aabb_is_valid(aabb)) {
		return {};
	}

	T t_min = T(0);
	T t_max = std::numeric_limits<T>::infinity();

	const auto update_axis = [&](T origin, T direction, T min_value, T max_value) {
		if (std::abs(direction) <= epsilon) {
			return origin >= min_value && origin <= max_value;
		}
		T near_distance = (min_value - origin) / direction;
		T far_distance = (max_value - origin) / direction;
		if (near_distance > far_distance) {
			std::swap(near_distance, far_distance);
		}
		t_min = std::max(t_min, near_distance);
		t_max = std::min(t_max, far_distance);
		return t_min <= t_max;
	};

	if (!update_axis(ray.origin.x, ray.direction.x, aabb.min.x, aabb.max.x) ||
			!update_axis(ray.origin.y, ray.direction.y, aabb.min.y, aabb.max.y) ||
			!update_axis(ray.origin.z, ray.direction.z, aabb.min.z, aabb.max.z)) {
		return {};
	}

	return { true, t_min, t_max, ray.origin + ray.direction * t_min };
}

template<typename T>
[[nodiscard]] RayBoundedTriangleHit<T> intersect_ray_bounded_triangle(
	const Ray3<T>& ray,
	const Triangle3<T>& triangle,
	const Aabb3<T>& bounds,
	RayBoundedTriangleQueryOptions<T> options = {})
{
	if (options.max_distance < options.min_distance) {
		return {};
	}

	const RayAabbHit<T> bounds_hit = intersect_ray_aabb(ray, bounds, options.epsilon);
	if (!bounds_hit.hit ||
			bounds_hit.min_distance > options.max_distance ||
			bounds_hit.max_distance < options.min_distance) {
		return {};
	}

	const RayTriangleHit<T> triangle_hit = intersect_ray_triangle(ray, triangle, options.epsilon);
	if (!triangle_hit.hit ||
			triangle_hit.distance < options.min_distance ||
			triangle_hit.distance > options.max_distance) {
		return {};
	}

	return { true, 0U, bounds_hit, triangle_hit };
}

template<typename T>
[[nodiscard]] RayBoundedTriangleHit<T> intersect_ray_bounded_triangles(
	const Ray3<T>& ray,
	std::span<const Triangle3<T>> triangles,
	std::span<const Aabb3<T>> bounds,
	RayBoundedTriangleQueryOptions<T> options = {})
{
	if (triangles.size() != bounds.size()) {
		return {};
	}

	RayBoundedTriangleHit<T> best;
	for (std::size_t index = 0; index < triangles.size(); ++index) {
		RayBoundedTriangleQueryOptions<T> candidate_options = options;
		if (best.hit) {
			candidate_options.max_distance = std::min(candidate_options.max_distance, best.triangle_hit.distance);
		}
		RayBoundedTriangleHit<T> candidate =
			intersect_ray_bounded_triangle(ray, triangles[index], bounds[index], candidate_options);
		if (!candidate.hit) {
			continue;
		}
		candidate.triangle_index = index;
		if (!best.hit || candidate.triangle_hit.distance < best.triangle_hit.distance) {
			best = candidate;
		}
	}
	return best;
}

[[nodiscard]] ClosestRayPoint<double> closest_point_on_ray(Vec3d point, const Ray3<double>& ray, double epsilon = 0.000001);
[[nodiscard]] double point_ray_distance_squared(Vec3d point, const Ray3<double>& ray, double epsilon = 0.000001);

template<typename T>
[[nodiscard]] ClosestRayPoint<T> closest_point_on_ray(BasicVec3<T> point, const Ray3<T>& ray, T epsilon = T(0.000001))
{
	const T direction_length_squared = length_squared(ray.direction);
	if (direction_length_squared <= epsilon * epsilon) {
		return {};
	}

	const T distance = std::max(T(0), dot(point - ray.origin, ray.direction) / direction_length_squared);
	const BasicVec3<T> closest = ray.origin + ray.direction * distance;
	return { true, distance, closest, length_squared(point - closest) };
}

template<typename T>
[[nodiscard]] T point_ray_distance_squared(BasicVec3<T> point, const Ray3<T>& ray, T epsilon = T(0.000001))
{
	const ClosestRayPoint<T> closest = closest_point_on_ray(point, ray, epsilon);
	return closest.valid ? closest.distance_squared : std::numeric_limits<T>::infinity();
}

[[nodiscard]] SegmentRayClosestPoints<double> closest_points_segment_ray(const Segment3<double>& segment, const Ray3<double>& ray,
	double epsilon = 0.000001);
[[nodiscard]] double segment_ray_distance_squared(const Segment3<double>& segment, const Ray3<double>& ray, double epsilon = 0.000001);

[[nodiscard]] SegmentSegmentClosestPoints<double> closest_points_segment_segment(
	const Segment3<double>& first, const Segment3<double>& second, double epsilon = 0.000001);
[[nodiscard]] double segment_segment_distance_squared(
	const Segment3<double>& first, const Segment3<double>& second, double epsilon = 0.000001);

template<typename T>
[[nodiscard]] SegmentRayClosestPoints<T> closest_points_segment_ray(const Segment3<T>& segment, const Ray3<T>& ray, T epsilon = T(0.000001))
{
	const BasicVec3<T> segment_direction = segment.b - segment.a;
	const T segment_length_squared = length_squared(segment_direction);
	const T ray_length_squared = length_squared(ray.direction);
	if (ray_length_squared <= epsilon * epsilon) {
		return {};
	}

	const auto candidate_for = [&](T segment_factor, T ray_distance) {
		SegmentRayClosestPoints<T> candidate;
		candidate.valid = true;
		candidate.segment_factor = std::clamp(segment_factor, T(0), T(1));
		candidate.ray_distance = std::max(T(0), ray_distance);
		candidate.segment_point = segment.a + segment_direction * candidate.segment_factor;
		candidate.ray_point = ray.origin + ray.direction * candidate.ray_distance;
		candidate.distance_squared = length_squared(candidate.segment_point - candidate.ray_point);
		return candidate;
	};

	SegmentRayClosestPoints<T> best = candidate_for(T(0), dot(segment.a - ray.origin, ray.direction) / ray_length_squared);
	const auto keep_best = [&](SegmentRayClosestPoints<T> candidate) {
		if (candidate.distance_squared < best.distance_squared) {
			best = candidate;
		}
	};

	keep_best(candidate_for(T(1), dot(segment.b - ray.origin, ray.direction) / ray_length_squared));

	if (segment_length_squared <= epsilon * epsilon) {
		return best;
	}

	keep_best(candidate_for(dot(ray.origin - segment.a, segment_direction) / segment_length_squared, T(0)));

	const BasicVec3<T> w0 = segment.a - ray.origin;
	const T a = segment_length_squared;
	const T b = dot(segment_direction, ray.direction);
	const T c = ray_length_squared;
	const T d = dot(segment_direction, w0);
	const T e = dot(ray.direction, w0);
	const T denominator = a * c - b * b;
	if (std::abs(denominator) > epsilon * epsilon) {
		const T segment_factor = (b * e - c * d) / denominator;
		const T ray_distance = (a * e - b * d) / denominator;
		if (segment_factor >= T(0) && segment_factor <= T(1) && ray_distance >= T(0)) {
			keep_best(candidate_for(segment_factor, ray_distance));
		}
	}

	return best;
}

template<typename T>
[[nodiscard]] T segment_ray_distance_squared(const Segment3<T>& segment, const Ray3<T>& ray, T epsilon = T(0.000001))
{
	const SegmentRayClosestPoints<T> closest = closest_points_segment_ray(segment, ray, epsilon);
	return closest.valid ? closest.distance_squared : std::numeric_limits<T>::infinity();
}

template<typename T>
[[nodiscard]] SegmentSegmentClosestPoints<T> closest_points_segment_segment(
	const Segment3<T>& first, const Segment3<T>& second, T epsilon = T(0.000001))
{
	const BasicVec3<T> first_direction = first.b - first.a;
	const BasicVec3<T> second_direction = second.b - second.a;
	const T first_length_squared = length_squared(first_direction);
	const T second_length_squared = length_squared(second_direction);

	const auto candidate_for = [&](T first_factor, T second_factor) {
		SegmentSegmentClosestPoints<T> candidate;
		candidate.valid = true;
		candidate.first_factor = std::clamp(first_factor, T(0), T(1));
		candidate.second_factor = std::clamp(second_factor, T(0), T(1));
		candidate.first_point = first.a + first_direction * candidate.first_factor;
		candidate.second_point = second.a + second_direction * candidate.second_factor;
		candidate.midpoint = (candidate.first_point + candidate.second_point) * T(0.5);
		candidate.distance_squared = length_squared(candidate.first_point - candidate.second_point);
		candidate.intersects = candidate.distance_squared <= epsilon * epsilon;
		return candidate;
	};

	if (first_length_squared <= epsilon * epsilon && second_length_squared <= epsilon * epsilon) {
		return candidate_for(T(0), T(0));
	}
	if (first_length_squared <= epsilon * epsilon) {
		const ClosestSegmentPoint3<T> closest = closest_point_on_segment(first.a, second, epsilon);
		return closest.valid ? candidate_for(T(0), closest.segment_factor) : SegmentSegmentClosestPoints<T> {};
	}
	if (second_length_squared <= epsilon * epsilon) {
		const ClosestSegmentPoint3<T> closest = closest_point_on_segment(second.a, first, epsilon);
		return closest.valid ? candidate_for(closest.segment_factor, T(0)) : SegmentSegmentClosestPoints<T> {};
	}

	SegmentSegmentClosestPoints<T> best = candidate_for(T(0), closest_point_on_segment(first.a, second, epsilon).segment_factor);
	const auto keep_best = [&](SegmentSegmentClosestPoints<T> candidate) {
		if (!best.valid || candidate.distance_squared < best.distance_squared) {
			best = candidate;
		}
	};

	keep_best(candidate_for(T(1), closest_point_on_segment(first.b, second, epsilon).segment_factor));
	keep_best(candidate_for(closest_point_on_segment(second.a, first, epsilon).segment_factor, T(0)));
	keep_best(candidate_for(closest_point_on_segment(second.b, first, epsilon).segment_factor, T(1)));

	const BasicVec3<T> delta = first.a - second.a;
	const T direction_dot = dot(first_direction, second_direction);
	const T first_delta_dot = dot(first_direction, delta);
	const T second_delta_dot = dot(second_direction, delta);
	const T denominator = first_length_squared * second_length_squared - direction_dot * direction_dot;
	if (std::abs(denominator) > epsilon * epsilon) {
		const T first_factor = (direction_dot * second_delta_dot - second_length_squared * first_delta_dot) / denominator;
		const T second_factor = (first_length_squared * second_delta_dot - direction_dot * first_delta_dot) / denominator;
		if (first_factor >= T(0) && first_factor <= T(1) && second_factor >= T(0) && second_factor <= T(1)) {
			keep_best(candidate_for(first_factor, second_factor));
		}
	}

	return best;
}

template<typename T>
[[nodiscard]] T segment_segment_distance_squared(const Segment3<T>& first, const Segment3<T>& second, T epsilon = T(0.000001))
{
	const SegmentSegmentClosestPoints<T> closest = closest_points_segment_segment(first, second, epsilon);
	return closest.valid ? closest.distance_squared : std::numeric_limits<T>::infinity();
}

[[nodiscard]] LineLineClosestPoints<double> closest_points_line_line(const Line3<double>& first, const Line3<double>& second,
	double epsilon = 0.000001);
[[nodiscard]] double line_line_distance_squared(const Line3<double>& first, const Line3<double>& second, double epsilon = 0.000001);

template<typename T>
[[nodiscard]] LineLineClosestPoints<T> closest_points_line_line(const Line3<T>& first, const Line3<T>& second, T epsilon = T(0.000001))
{
	const T first_length_squared = length_squared(first.direction);
	const T second_length_squared = length_squared(second.direction);
	if (first_length_squared <= epsilon * epsilon || second_length_squared <= epsilon * epsilon) {
		return {};
	}

	const BasicVec3<T> delta = first.origin - second.origin;
	const T direction_dot = dot(first.direction, second.direction);
	const T first_delta_dot = dot(first.direction, delta);
	const T second_delta_dot = dot(second.direction, delta);
	const T denominator = first_length_squared * second_length_squared - direction_dot * direction_dot;

	T first_parameter = T(0);
	T second_parameter = second_delta_dot / second_length_squared;
	if (std::abs(denominator) > epsilon * epsilon) {
		first_parameter = (direction_dot * second_delta_dot - second_length_squared * first_delta_dot) / denominator;
		second_parameter = (first_length_squared * second_delta_dot - direction_dot * first_delta_dot) / denominator;
	}

	const BasicVec3<T> first_point = first.origin + first.direction * first_parameter;
	const BasicVec3<T> second_point = second.origin + second.direction * second_parameter;
	const T distance_squared = length_squared(first_point - second_point);
	return {
		true,
		distance_squared <= epsilon * epsilon,
		first_parameter,
		second_parameter,
		first_point,
		second_point,
		(first_point + second_point) * T(0.5),
		distance_squared,
	};
}

template<typename T>
[[nodiscard]] T line_line_distance_squared(const Line3<T>& first, const Line3<T>& second, T epsilon = T(0.000001))
{
	const LineLineClosestPoints<T> closest = closest_points_line_line(first, second, epsilon);
	return closest.valid ? closest.distance_squared : std::numeric_limits<T>::infinity();
}

template<typename T>
[[nodiscard]] SegmentFactorClosestPoint3<T> closest_factor_on_segment_to_segment(
	const Segment3<T>& target, const Segment3<T>& segment, SegmentSegmentFactorOptions<T> options = {})
{
	const BasicVec3<T> target_direction = target.b - target.a;
	const BasicVec3<T> segment_direction = segment.b - segment.a;
	const T target_length_squared = length_squared(target_direction);
	const T segment_length_squared = length_squared(segment_direction);
	const T min_factor = std::min(options.min_factor, options.max_factor);
	const T max_factor = std::max(options.min_factor, options.max_factor);

	const auto result_for = [&](T factor) {
		SegmentFactorClosestPoint3<T> result;
		result.valid = true;
		result.segment_factor = std::clamp(factor, min_factor, max_factor);
		result.point = target.a + target_direction * result.segment_factor;
		return result;
	};

	if (target_length_squared <= options.length_squared_epsilon) {
		return {};
	}
	if (segment_length_squared <= options.length_squared_epsilon) {
		return result_for(dot(segment.a - target.a, target_direction) / target_length_squared);
	}

	const T denominator = length_squared(cross(target_direction, segment_direction));
	if (denominator <= options.length_squared_epsilon) {
		const BasicVec3<T> midpoint = (segment.a + segment.b) * T(0.5);
		return result_for(dot(midpoint - target.a, target_direction) / target_length_squared);
	}

	const SegmentSegmentClosestPoints<T> finite_closest = closest_points_segment_segment(target, segment, options.closest_epsilon);
	if (finite_closest.valid &&
			finite_closest.second_factor > options.parameter_epsilon &&
			finite_closest.second_factor < T(1) - options.parameter_epsilon) {
		return result_for(finite_closest.first_factor);
	}

	const LineLineClosestPoints<T> closest = closest_points_line_line(
		Line3<T> { target.a, target_direction },
		Line3<T> { segment.a, segment_direction },
		options.closest_epsilon);
	return closest.valid ? result_for(closest.first_parameter) : SegmentFactorClosestPoint3<T> {};
}

} // namespace quader_geometry

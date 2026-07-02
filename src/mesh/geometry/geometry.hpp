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
 * Enumerates QAxis values used by the geometry layer.
 */
enum class QAxis {
  X,
  Y,
  Z,
};

template<typename T>
/**
 * Represents a Basic Vec2 value used by the geometry predicate and projection layer.
 */
struct QBasicVec2 {
	static_assert(std::is_floating_point_v<T>);

	T x = T(0);
	T y = T(0);

	constexpr QBasicVec2() = default;
	constexpr QBasicVec2(T x_value, T y_value)
		: x(x_value)
		, y(y_value)
	{
	}

	template<typename U>
	explicit constexpr QBasicVec2(const QBasicVec2<U>& value)
		: x(static_cast<T>(value.x))
		, y(static_cast<T>(value.y))
	{
	}

	template<typename U>
		requires requires(U value) {
			value.x;
			value.y;
		}
	explicit constexpr QBasicVec2(const U& value)
		: x(static_cast<T>(value.x))
		, y(static_cast<T>(value.y))
	{
	}
};

template<typename T>
/**
 * Represents a Basic Vec3 value used by the geometry predicate and projection layer.
 */
struct QBasicVec3 {
	static_assert(std::is_floating_point_v<T>);

	T x = T(0);
	T y = T(0);
	T z = T(0);

	constexpr QBasicVec3() = default;
	constexpr QBasicVec3(T x_value, T y_value, T z_value)
		: x(x_value)
		, y(y_value)
		, z(z_value)
	{
	}

	template<typename U>
	explicit constexpr QBasicVec3(const QBasicVec3<U>& value)
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
	explicit constexpr QBasicVec3(const U& value)
		: x(static_cast<T>(value.x))
		, y(static_cast<T>(value.y))
		, z(static_cast<T>(value.z))
	{
	}
};

using QVec2f = QBasicVec2<float>;
using QVec3f = QBasicVec3<float>;
using QVec2d = QBasicVec2<double>;
using QVec3d = QBasicVec3<double>;
using QTriangleIndices = std::array<std::uint32_t, 3>;

template<typename T>
/**
 * Represents a QRay3 value used by the geometry predicate and projection layer.
 */
struct QRay3 {
	QBasicVec3<T> origin;
	QBasicVec3<T> direction { T(0), T(0), T(1) };

	constexpr QRay3() = default;
	constexpr QRay3(QBasicVec3<T> origin_value, QBasicVec3<T> direction_value)
		: origin(origin_value)
		, direction(direction_value)
	{
	}

	template<typename U>
	explicit constexpr QRay3(const QRay3<U>& value)
		: origin(value.origin)
		, direction(value.direction)
	{
	}
};

template<typename T>
/**
 * Represents a QLine3 value used by the geometry predicate and projection layer.
 */
struct QLine3 {
	QBasicVec3<T> origin;
	QBasicVec3<T> direction { T(0), T(0), T(1) };

	constexpr QLine3() = default;
	constexpr QLine3(QBasicVec3<T> origin_value, QBasicVec3<T> direction_value)
		: origin(origin_value)
		, direction(direction_value)
	{
	}

	template<typename U>
	explicit constexpr QLine3(const QLine3<U>& value)
		: origin(static_cast<QBasicVec3<T>>(value.origin))
		, direction(static_cast<QBasicVec3<T>>(value.direction))
	{
	}
};

template<typename T>
/**
 * Represents a QPlane3 value used by the geometry predicate and projection layer.
 */
struct QPlane3 {
	QBasicVec3<T> normal { T(0), T(1), T(0) };
	T d = T(0);

	constexpr QPlane3() = default;
	constexpr QPlane3(QBasicVec3<T> normal_value, T d_value)
		: normal(normal_value)
		, d(d_value)
	{
	}

	template<typename U>
	explicit constexpr QPlane3(const QPlane3<U>& value)
		: normal(value.normal)
		, d(static_cast<T>(value.d))
	{
	}
};

template<typename T>
/**
 * Represents a QSegment3 value used by the geometry predicate and projection layer.
 */
struct QSegment3 {
	QBasicVec3<T> a;
	QBasicVec3<T> b;

	constexpr QSegment3() = default;
	constexpr QSegment3(QBasicVec3<T> a_value, QBasicVec3<T> b_value)
		: a(a_value)
		, b(b_value)
	{
	}

	template<typename U>
	explicit constexpr QSegment3(const QSegment3<U>& value)
		: a(value.a)
		, b(value.b)
	{
	}
};

template<typename T>
/**
 * Represents a QTriangle3 value used by the geometry predicate and projection layer.
 */
struct QTriangle3 {
	QBasicVec3<T> a;
	QBasicVec3<T> b;
	QBasicVec3<T> c;

	constexpr QTriangle3() = default;
	constexpr QTriangle3(QBasicVec3<T> a_value, QBasicVec3<T> b_value, QBasicVec3<T> c_value)
		: a(a_value)
		, b(b_value)
		, c(c_value)
	{
	}

	template<typename U>
	explicit constexpr QTriangle3(const QTriangle3<U>& value)
		: a(value.a)
		, b(value.b)
		, c(value.c)
	{
	}
};

template<typename T>
/**
 * Represents an QAabb3 value used by the geometry predicate and projection layer.
 */
struct QAabb3 {
	QBasicVec3<T> min;
	QBasicVec3<T> max;

	constexpr QAabb3() = default;
	constexpr QAabb3(QBasicVec3<T> min_value, QBasicVec3<T> max_value)
		: min(min_value)
		, max(max_value)
	{
	}

	template<typename U>
	explicit constexpr QAabb3(const QAabb3<U>& value)
		: min(value.min)
		, max(value.max)
	{
	}
};

template<typename T>
/**
 * Represents a QRect2 value used by the geometry predicate and projection layer.
 */
struct QRect2 {
	QBasicVec2<T> min;
	QBasicVec2<T> max;

	constexpr QRect2() = default;
	constexpr QRect2(QBasicVec2<T> min_value, QBasicVec2<T> max_value)
		: min(min_value)
		, max(max_value)
	{
	}

	template<typename U>
	explicit constexpr QRect2(const QRect2<U>& value)
		: min(value.min)
		, max(value.max)
	{
	}
};

using QAabb3f = QAabb3<float>;
using QAabb3d = QAabb3<double>;
using QRect2f = QRect2<float>;
using QRect2d = QRect2<double>;

/**
 * Enumerates QPlaneSide values used by the geometry layer.
 */
enum class QPlaneSide {
  Negative,
  On,
  Positive,
};

/**
 * Enumerates QPlaneIntersectionType values used by the geometry layer.
 */
enum class QPlaneIntersectionType {
  None,
  Point,
  Coplanar,
};

/**
 * Enumerates QSegmentIntersectionKind2 values used by the geometry layer.
 */
enum class QSegmentIntersectionKind2 {
  None,
  Point,
  Overlap,
};

template<typename T>
/**
 * Represents a Plane Basis3 value used by the geometry predicate and projection layer.
 */
struct QPlaneBasis3 {
	bool valid = false;
	QBasicVec3<T> origin;
	QBasicVec3<T> normal;
	QBasicVec3<T> u;
	QBasicVec3<T> v;
};

template<typename T>
/**
 * Represents a Plane Coordinates3 value used by the geometry predicate and projection layer.
 */
struct QPlaneCoordinates3 {
	bool valid = false;
	T u = T(0);
	T v = T(0);
	T signed_distance = T(0);
	QBasicVec3<T> projected_point;
};

template<typename T>
/**
 * Represents a Ray Plane Hit value used by the geometry predicate and projection layer.
 */
struct QRayPlaneHit {
	bool hit = false;
	T distance = T(0);
	QBasicVec3<T> point;
};

template<typename T>
/**
 * Represents a Line Plane Hit value used by the geometry predicate and projection layer.
 */
struct QLinePlaneHit {
  QPlaneIntersectionType type = QPlaneIntersectionType::None;
  T parameter = T(0);
  QBasicVec3<T> point;
};

template<typename T>
/**
 * Represents a Segment Plane Hit value used by the geometry predicate and projection layer.
 */
struct QSegmentPlaneHit {
  QPlaneIntersectionType type = QPlaneIntersectionType::None;
  T segment_factor = T(0);
  QBasicVec3<T> point;
};

template<typename T>
/**
 * Represents a Three Plane Intersection value used by the geometry predicate and projection layer.
 */
struct QThreePlaneIntersection {
	bool hit = false;
	QBasicVec3<T> point;
};

template<typename T>
/**
 * Represents a Segment Intersection2 value used by the geometry predicate and projection layer.
 */
struct QSegmentIntersection2 {
	bool hit = false;
	T first_factor = T(0);
	T second_factor = T(0);
	QBasicVec2<T> point;
};

template<typename T>
/**
 * Represents a Classified Segment Intersection2 value used by the geometry predicate and projection layer.
 */
struct QClassifiedSegmentIntersection2 {
	bool hit = false;
        QSegmentIntersectionKind2 kind = QSegmentIntersectionKind2::None;
        std::uint8_t point_count = 0;
	std::array<T, 2> first_factors {};
	std::array<T, 2> second_factors {};
	std::array<QBasicVec2<T>, 2> points {};
};

template<typename T>
/**
 * Stores the Dominant QAxis Unproject Result data contract used by the geometry predicate and projection layer.
 */
struct QDominantAxisUnprojectResult {
	bool valid = false;
	QBasicVec3<T> point;
};

template<typename T>
/**
 * Represents a Ray Triangle Hit value used by the geometry predicate and projection layer.
 */
struct QRayTriangleHit {
	bool hit = false;
	T distance = T(0);
	T u = T(0);
	T v = T(0);
	T w = T(0);
	QBasicVec3<T> point;
};

template<typename T>
/**
 * Represents a Ray Aabb Hit value used by the geometry predicate and projection layer.
 */
struct QRayAabbHit {
	bool hit = false;
	T min_distance = T(0);
	T max_distance = T(0);
	QBasicVec3<T> point;
};

template<typename T>
/**
 * Stores the Ray Bounded Triangle Query Options data contract used by the geometry predicate and projection layer.
 */
struct QRayBoundedTriangleQueryOptions {
	T min_distance = T(0);
	T max_distance = std::numeric_limits<T>::infinity();
	T epsilon = T(0.000001);
};

template<typename T>
/**
 * Represents a Ray Bounded Triangle Hit value used by the geometry predicate and projection layer.
 */
struct QRayBoundedTriangleHit {
	bool hit = false;
	std::size_t triangle_index = std::numeric_limits<std::size_t>::max();
	QRayAabbHit<T> bounds_hit;
	QRayTriangleHit<T> triangle_hit;
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
struct QClosestRayPoint {
	bool valid = false;
	T ray_distance = T(0);
	QBasicVec3<T> point;
	T distance_squared = T(0);
};

template<typename T>
/**
 * Represents a Closest Segment Point2 value used by the geometry predicate and projection layer.
 */
struct QClosestSegmentPoint2 {
	bool valid = false;
	T segment_factor = T(0);
	QBasicVec2<T> point;
	T distance_squared = T(0);
};

template<typename T>
/**
 * Represents a Closest Segment Point3 value used by the geometry predicate and projection layer.
 */
struct QClosestSegmentPoint3 {
	bool valid = false;
	T segment_factor = T(0);
	QBasicVec3<T> point;
	T distance_squared = T(0);
};

template<typename T>
/**
 * Represents a Segment Ray Closest Points value used by the geometry predicate and projection layer.
 */
struct QSegmentRayClosestPoints {
	bool valid = false;
	T segment_factor = T(0);
	T ray_distance = T(0);
	QBasicVec3<T> segment_point;
	QBasicVec3<T> ray_point;
	T distance_squared = T(0);
};

template<typename T>
/**
 * Represents a Segment Segment Closest Points value used by the geometry predicate and projection layer.
 */
struct QSegmentSegmentClosestPoints {
	bool valid = false;
	bool intersects = false;
	T first_factor = T(0);
	T second_factor = T(0);
	QBasicVec3<T> first_point;
	QBasicVec3<T> second_point;
	QBasicVec3<T> midpoint;
	T distance_squared = T(0);
};

template<typename T>
/**
 * Represents a Segment Factor Closest Point3 value used by the geometry predicate and projection layer.
 */
struct QSegmentFactorClosestPoint3 {
	bool valid = false;
	T segment_factor = T(0);
	QBasicVec3<T> point;
};

template<typename T>
/**
 * Stores the Segment Segment Factor Options data contract used by the geometry predicate and projection layer.
 */
struct QSegmentSegmentFactorOptions {
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
struct QLineLineClosestPoints {
	bool valid = false;
	bool intersects = false;
	T first_parameter = T(0);
	T second_parameter = T(0);
	QBasicVec3<T> first_point;
	QBasicVec3<T> second_point;
	QBasicVec3<T> midpoint;
	T distance_squared = T(0);
};

template<typename T>
[[nodiscard]] constexpr QBasicVec2<T> operator+(QBasicVec2<T> left, QBasicVec2<T> right)
{
	return { left.x + right.x, left.y + right.y };
}

template<typename T>
[[nodiscard]] constexpr QBasicVec2<T> operator-(QBasicVec2<T> left, QBasicVec2<T> right)
{
	return { left.x - right.x, left.y - right.y };
}

template<typename T>
[[nodiscard]] constexpr QBasicVec2<T> operator-(QBasicVec2<T> value)
{
	return { -value.x, -value.y };
}

template<typename T>
[[nodiscard]] constexpr QBasicVec2<T> operator*(QBasicVec2<T> value, T scalar)
{
	return { value.x * scalar, value.y * scalar };
}

template<typename T>
[[nodiscard]] constexpr QBasicVec2<T> operator/(QBasicVec2<T> value, T scalar)
{
	return { value.x / scalar, value.y / scalar };
}

template<typename T>
[[nodiscard]] constexpr QBasicVec3<T> operator+(QBasicVec3<T> left, QBasicVec3<T> right)
{
	return { left.x + right.x, left.y + right.y, left.z + right.z };
}

template<typename T>
[[nodiscard]] constexpr QBasicVec3<T> operator-(QBasicVec3<T> left, QBasicVec3<T> right)
{
	return { left.x - right.x, left.y - right.y, left.z - right.z };
}

template<typename T>
[[nodiscard]] constexpr QBasicVec3<T> operator-(QBasicVec3<T> value)
{
	return { -value.x, -value.y, -value.z };
}

template<typename T>
[[nodiscard]] constexpr QBasicVec3<T> operator*(QBasicVec3<T> value, T scalar)
{
	return { value.x * scalar, value.y * scalar, value.z * scalar };
}

template<typename T>
[[nodiscard]] constexpr QBasicVec3<T> operator/(QBasicVec3<T> value, T scalar)
{
	return { value.x / scalar, value.y / scalar, value.z / scalar };
}

template<typename T>
[[nodiscard]] constexpr QBasicVec2<T> component_min(QBasicVec2<T> left, QBasicVec2<T> right)
{
	return { std::min(left.x, right.x), std::min(left.y, right.y) };
}

template<typename T>
[[nodiscard]] constexpr QBasicVec3<T> component_min(QBasicVec3<T> left, QBasicVec3<T> right)
{
	return { std::min(left.x, right.x), std::min(left.y, right.y), std::min(left.z, right.z) };
}

template<typename T>
[[nodiscard]] constexpr QBasicVec2<T> component_max(QBasicVec2<T> left, QBasicVec2<T> right)
{
	return { std::max(left.x, right.x), std::max(left.y, right.y) };
}

template<typename T>
[[nodiscard]] constexpr QBasicVec3<T> component_max(QBasicVec3<T> left, QBasicVec3<T> right)
{
	return { std::max(left.x, right.x), std::max(left.y, right.y), std::max(left.z, right.z) };
}

template<typename T>
[[nodiscard]] constexpr QAabb3<T> empty_aabb3()
{
	const T infinity = std::numeric_limits<T>::infinity();
	return { { infinity, infinity, infinity }, { -infinity, -infinity, -infinity } };
}

template<typename T>
[[nodiscard]] constexpr bool aabb_is_valid(const QAabb3<T>& aabb)
{
	return aabb.min.x <= aabb.max.x && aabb.min.y <= aabb.max.y && aabb.min.z <= aabb.max.z;
}

template<typename T>
constexpr void aabb_include(QAabb3<T>& aabb, QBasicVec3<T> point)
{
	aabb.min = component_min(aabb.min, point);
	aabb.max = component_max(aabb.max, point);
}

template<typename T>
constexpr void aabb_include(QAabb3<T>& aabb, const QAabb3<T>& included)
{
	if (!aabb_is_valid(included)) {
		return;
	}
	aabb_include(aabb, included.min);
	aabb_include(aabb, included.max);
}

template<typename T>
[[nodiscard]] constexpr QAabb3<T> aabb_including(QAabb3<T> aabb, QBasicVec3<T> point)
{
	aabb_include(aabb, point);
	return aabb;
}

template<typename T>
[[nodiscard]] constexpr QAabb3<T> aabb_including(QAabb3<T> aabb, const QAabb3<T>& included)
{
	aabb_include(aabb, included);
	return aabb;
}

template<typename T>
[[nodiscard]] constexpr QAabb3<T> aabb_expanded(const QAabb3<T>& aabb, T amount)
{
	if (!aabb_is_valid(aabb)) {
		return aabb;
	}
	const QBasicVec3<T> delta { amount, amount, amount };
	return { aabb.min - delta, aabb.max + delta };
}

template<typename T>
[[nodiscard]] constexpr QBasicVec3<T> aabb_size(const QAabb3<T>& aabb)
{
	return aabb_is_valid(aabb) ? aabb.max - aabb.min : QBasicVec3<T> {};
}

template<typename T>
[[nodiscard]] constexpr QBasicVec3<T> aabb_center(const QAabb3<T>& aabb)
{
	return aabb_is_valid(aabb) ? (aabb.min + aabb.max) * T(0.5) : QBasicVec3<T> {};
}

template<typename T>
[[nodiscard]] QAabb3<T> aabb_from_points(std::span<const QBasicVec3<T>> points)
{
	QAabb3<T> bounds = empty_aabb3<T>();
	for (const QBasicVec3<T> point : points) {
		aabb_include(bounds, point);
	}
	return bounds;
}

template<typename T>
[[nodiscard]] constexpr bool aabb_contains_point(const QAabb3<T>& aabb, QBasicVec3<T> point)
{
	return aabb_is_valid(aabb) &&
		point.x >= aabb.min.x && point.x <= aabb.max.x &&
		point.y >= aabb.min.y && point.y <= aabb.max.y &&
		point.z >= aabb.min.z && point.z <= aabb.max.z;
}

template<typename T>
[[nodiscard]] constexpr bool aabb_overlaps(const QAabb3<T>& left, const QAabb3<T>& right)
{
	return aabb_is_valid(left) && aabb_is_valid(right) &&
		left.min.x <= right.max.x && left.max.x >= right.min.x &&
		left.min.y <= right.max.y && left.max.y >= right.min.y &&
		left.min.z <= right.max.z && left.max.z >= right.min.z;
}

template<typename T>
[[nodiscard]] constexpr QBasicVec3<T> aabb_closest_point(const QAabb3<T>& aabb, QBasicVec3<T> point)
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
[[nodiscard]] constexpr T aabb_distance_squared_to_point(const QAabb3<T>& aabb, QBasicVec3<T> point)
{
	if (!aabb_is_valid(aabb)) {
		return std::numeric_limits<T>::infinity();
	}
	return length_squared(point - aabb_closest_point(aabb, point));
}

template<typename T>
[[nodiscard]] QAxis aabb_longest_axis(const QAabb3<T>& aabb)
{
	const QBasicVec3<T> size = aabb_size(aabb);
	const T ax = std::abs(size.x);
	const T ay = std::abs(size.y);
	const T az = std::abs(size.z);
	if (ax >= ay && ax >= az) {
          return QAxis::X;
        }
	if (ay >= az) {
          return QAxis::Y;
        }
        return QAxis::Z;
}

template<typename T>
[[nodiscard]] constexpr QRect2<T> empty_rect2()
{
	const T infinity = std::numeric_limits<T>::infinity();
	return { { infinity, infinity }, { -infinity, -infinity } };
}

template<typename T>
[[nodiscard]] constexpr bool rect_is_valid(const QRect2<T>& rect)
{
	return rect.min.x <= rect.max.x && rect.min.y <= rect.max.y;
}

template<typename T>
constexpr void rect_include(QRect2<T>& rect, QBasicVec2<T> point)
{
	rect.min = component_min(rect.min, point);
	rect.max = component_max(rect.max, point);
}

template<typename T>
constexpr void rect_include(QRect2<T>& rect, const QRect2<T>& included)
{
	if (!rect_is_valid(included)) {
		return;
	}
	rect_include(rect, included.min);
	rect_include(rect, included.max);
}

template<typename T>
[[nodiscard]] constexpr QRect2<T> rect_including(QRect2<T> rect, QBasicVec2<T> point)
{
	rect_include(rect, point);
	return rect;
}

template<typename T>
[[nodiscard]] constexpr QRect2<T> rect_including(QRect2<T> rect, const QRect2<T>& included)
{
	rect_include(rect, included);
	return rect;
}

template<typename T>
[[nodiscard]] constexpr QRect2<T> rect_expanded(const QRect2<T>& rect, T amount)
{
	if (!rect_is_valid(rect)) {
		return rect;
	}
	const QBasicVec2<T> delta { amount, amount };
	return { rect.min - delta, rect.max + delta };
}

template<typename T>
[[nodiscard]] constexpr QBasicVec2<T> rect_size(const QRect2<T>& rect)
{
	return rect_is_valid(rect) ? rect.max - rect.min : QBasicVec2<T> {};
}

template<typename T>
[[nodiscard]] constexpr QBasicVec2<T> rect_center(const QRect2<T>& rect)
{
	return rect_is_valid(rect) ? (rect.min + rect.max) * T(0.5) : QBasicVec2<T> {};
}

template<typename T>
[[nodiscard]] constexpr bool rect_contains_point(const QRect2<T>& rect, QBasicVec2<T> point)
{
	return rect_is_valid(rect) &&
		point.x >= rect.min.x && point.x <= rect.max.x &&
		point.y >= rect.min.y && point.y <= rect.max.y;
}

template<typename T>
[[nodiscard]] constexpr bool rect_overlaps(const QRect2<T>& left, const QRect2<T>& right)
{
	return rect_is_valid(left) && rect_is_valid(right) &&
		left.min.x <= right.max.x && left.max.x >= right.min.x &&
		left.min.y <= right.max.y && left.max.y >= right.min.y;
}

template<typename T>
[[nodiscard]] constexpr T dot(QBasicVec2<T> left, QBasicVec2<T> right)
{
	return left.x * right.x + left.y * right.y;
}

template<typename T>
[[nodiscard]] constexpr T dot(QBasicVec3<T> left, QBasicVec3<T> right)
{
	return left.x * right.x + left.y * right.y + left.z * right.z;
}

template<typename T>
[[nodiscard]] constexpr QBasicVec3<T> cross(QBasicVec3<T> left, QBasicVec3<T> right)
{
	return {
		left.y * right.z - left.z * right.y,
		left.z * right.x - left.x * right.z,
		left.x * right.y - left.y * right.x,
	};
}

template<typename T>
[[nodiscard]] constexpr T length_squared(QBasicVec2<T> value)
{
	return dot(value, value);
}

template<typename T>
[[nodiscard]] constexpr T length_squared(QBasicVec3<T> value)
{
	return dot(value, value);
}

template<typename T>
[[nodiscard]] T length(QBasicVec2<T> value)
{
	return std::sqrt(length_squared(value));
}

template<typename T>
[[nodiscard]] T length(QBasicVec3<T> value)
{
	return std::sqrt(length_squared(value));
}

template<typename T>
[[nodiscard]] QBasicVec2<T> normalize_or_zero(QBasicVec2<T> value, T epsilon = std::numeric_limits<T>::epsilon())
{
	const T squared = length_squared(value);
	if (squared <= epsilon * epsilon) {
		return {};
	}
	return value / std::sqrt(squared);
}

template<typename T>
[[nodiscard]] QBasicVec3<T> normalize_or_zero(QBasicVec3<T> value, T epsilon = std::numeric_limits<T>::epsilon())
{
	const T squared = length_squared(value);
	if (squared <= epsilon * epsilon) {
		return {};
	}
	return value / std::sqrt(squared);
}

template<typename T>
[[nodiscard]] QPlane3<T> plane_from_point_normal(QBasicVec3<T> point, QBasicVec3<T> normal, T epsilon = std::numeric_limits<T>::epsilon())
{
	const QBasicVec3<T> normalized = normalize_or_zero(normal, epsilon);
	return { normalized, dot(normalized, point) };
}

template<typename T>
[[nodiscard]] QPlane3<T> normalize_plane(const QPlane3<T>& plane, T epsilon = std::numeric_limits<T>::epsilon())
{
	const T squared = length_squared(plane.normal);
	if (squared <= epsilon * epsilon) {
		return { {}, T(0) };
	}
	const T inverse_length = T(1) / std::sqrt(squared);
	return { plane.normal * inverse_length, plane.d * inverse_length };
}

template<typename T>
[[nodiscard]] T signed_distance_to_plane(QBasicVec3<T> point, const QPlane3<T>& plane)
{
	return dot(plane.normal, point) - plane.d;
}

template<typename T>
[[nodiscard]] QPlaneSide plane_side(QBasicVec3<T> point, const QPlane3<T>& plane, T epsilon = T(0.000001))
{
	const T distance = signed_distance_to_plane(point, plane);
	if (distance > epsilon) {
          return QPlaneSide::Positive;
        }
	if (distance < -epsilon) {
          return QPlaneSide::Negative;
        }
        return QPlaneSide::On;
}

template<typename T>
[[nodiscard]] bool planes_nearly_equal(const QPlane3<T>& left, const QPlane3<T>& right, T epsilon = T(0.000001))
{
	const QPlane3<T> normalized_left = normalize_plane(left, epsilon);
	const QPlane3<T> normalized_right = normalize_plane(right, epsilon);
	return length_squared(normalized_left.normal - normalized_right.normal) <= epsilon * epsilon &&
		std::abs(normalized_left.d - normalized_right.d) <= epsilon;
}

template<typename T>
[[nodiscard]] QBasicVec3<T> project_point_to_plane(QBasicVec3<T> point, const QPlane3<T>& plane)
{
	return point - plane.normal * signed_distance_to_plane(point, plane);
}

template<typename T>
[[nodiscard]] QPlaneBasis3<T> make_plane_basis(
	QBasicVec3<T> origin, QBasicVec3<T> normal, QBasicVec3<T> preferred_u = {}, T epsilon = T(0.000001))
{
	const QBasicVec3<T> normalized_normal = normalize_or_zero(normal, epsilon);
	if (length_squared(normalized_normal) <= epsilon * epsilon) {
		return {};
	}

	QBasicVec3<T> candidate_u = preferred_u - normalized_normal * dot(preferred_u, normalized_normal);
	if (length_squared(candidate_u) <= epsilon * epsilon) {
		const T ax = std::abs(normalized_normal.x);
		const T ay = std::abs(normalized_normal.y);
		const T az = std::abs(normalized_normal.z);
		QBasicVec3<T> reference { T(1), T(0), T(0) };
		if (ay <= ax && ay <= az) {
			reference = { T(0), T(1), T(0) };
		} else if (az <= ax && az <= ay) {
			reference = { T(0), T(0), T(1) };
		}
		candidate_u = reference - normalized_normal * dot(reference, normalized_normal);
	}

	const QBasicVec3<T> u = normalize_or_zero(candidate_u, epsilon);
	if (length_squared(u) <= epsilon * epsilon) {
		return {};
	}
	const QBasicVec3<T> v = normalize_or_zero(cross(normalized_normal, u), epsilon);
	if (length_squared(v) <= epsilon * epsilon) {
		return {};
	}
	return { true, origin, normalized_normal, u, v };
}

template<typename T>
[[nodiscard]] QPlaneBasis3<T> make_plane_basis(const QPlane3<T>& plane, QBasicVec3<T> preferred_u = {}, T epsilon = T(0.000001))
{
	const QPlane3<T> normalized = normalize_plane(plane, epsilon);
	if (length_squared(normalized.normal) <= epsilon * epsilon) {
		return {};
	}
	return make_plane_basis(normalized.normal * normalized.d, normalized.normal, preferred_u, epsilon);
}

template<typename T>
[[nodiscard]] QPlaneCoordinates3<T> plane_coordinates(QBasicVec3<T> point, const QPlaneBasis3<T>& basis)
{
	if (!basis.valid) {
		return {};
	}
	const QBasicVec3<T> delta = point - basis.origin;
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
[[nodiscard]] QBasicVec3<T> plane_point_from_coordinates(const QPlaneBasis3<T>& basis, T u, T v)
{
	return basis.origin + basis.u * u + basis.v * v;
}

template<typename T>
[[nodiscard]] QAxis dominant_axis(QBasicVec3<T> normal)
{
	const T ax = std::abs(normal.x);
	const T ay = std::abs(normal.y);
	const T az = std::abs(normal.z);
	if (ax >= ay && ax >= az) {
          return QAxis::X;
        }
	if (ay >= az) {
          return QAxis::Y;
        }
        return QAxis::Z;
}

template<typename T>
[[nodiscard]] QBasicVec2<T> project_dominant_axis(QBasicVec3<T> point, QAxis dropped_axis)
{
  if (dropped_axis == QAxis::X) {
    return {point.y, point.z};
  }
  if (dropped_axis == QAxis::Y) {
    return {point.x, point.z};
  }
        return { point.x, point.y };
}

template<typename T>
[[nodiscard]] QBasicVec2<T> project_dominant_axis(QBasicVec3<T> point, QBasicVec3<T> normal)
{
	return project_dominant_axis(point, dominant_axis(normal));
}

template<typename T>
[[nodiscard]] QDominantAxisUnprojectResult<T> unproject_dominant_axis_point_to_plane(
	QBasicVec2<T> point, const QPlane3<T>& plane, QAxis dropped_axis, T epsilon = T(0.000001))
{
  if (dropped_axis == QAxis::X) {
    if (std::abs(plane.normal.x) <= epsilon) {
      return {};
    }
    return {true,
            {(plane.d - plane.normal.y * point.x - plane.normal.z * point.y) /
                 plane.normal.x,
             point.x, point.y}};
  }
  if (dropped_axis == QAxis::Y) {
    if (std::abs(plane.normal.y) <= epsilon) {
      return {};
    }
    return {true,
            {point.x,
             (plane.d - plane.normal.x * point.x - plane.normal.z * point.y) /
                 plane.normal.y,
             point.y}};
  }
        if (std::abs(plane.normal.z) <= epsilon) {
		return {};
	}
	return { true, { point.x, point.y, (plane.d - plane.normal.x * point.x - plane.normal.y * point.y) / plane.normal.z } };
}

template<typename T>
[[nodiscard]] QClosestSegmentPoint2<T> closest_point_on_segment(QBasicVec2<T> point, QBasicVec2<T> segment_a, QBasicVec2<T> segment_b,
	T epsilon = T(0.000001))
{
	const QBasicVec2<T> segment_direction = segment_b - segment_a;
	const T segment_length_squared = length_squared(segment_direction);
	if (segment_length_squared <= epsilon * epsilon) {
		return { true, T(0), segment_a, length_squared(point - segment_a) };
	}

	const T segment_factor = std::clamp(dot(point - segment_a, segment_direction) / segment_length_squared, T(0), T(1));
	const QBasicVec2<T> closest = segment_a + segment_direction * segment_factor;
	return { true, segment_factor, closest, length_squared(point - closest) };
}

template<typename T>
[[nodiscard]] T point_segment_distance_squared(QBasicVec2<T> point, QBasicVec2<T> segment_a, QBasicVec2<T> segment_b, T epsilon = T(0.000001))
{
	const QClosestSegmentPoint2<T> closest = closest_point_on_segment(point, segment_a, segment_b, epsilon);
	return closest.valid ? closest.distance_squared : std::numeric_limits<T>::infinity();
}

[[nodiscard]] QClosestSegmentPoint2<double> closest_point_on_segment(QVec2d point, QVec2d segment_a, QVec2d segment_b,
	double epsilon = 0.000001);
[[nodiscard]] double point_segment_distance_squared(QVec2d point, QVec2d segment_a, QVec2d segment_b, double epsilon = 0.000001);

[[nodiscard]] QClosestSegmentPoint3<double> closest_point_on_segment(QVec3d point, const QSegment3<double>& segment,
	double epsilon = 0.000001);
[[nodiscard]] double point_segment_distance_squared(QVec3d point, const QSegment3<double>& segment, double epsilon = 0.000001);

template<typename T>
[[nodiscard]] QClosestSegmentPoint3<T> closest_point_on_segment(QBasicVec3<T> point, const QSegment3<T>& segment,
	T epsilon = T(0.000001))
{
	const QBasicVec3<T> segment_direction = segment.b - segment.a;
	const T segment_length_squared = length_squared(segment_direction);
	if (segment_length_squared <= epsilon * epsilon) {
		return { true, T(0), segment.a, length_squared(point - segment.a) };
	}

	const T segment_factor = std::clamp(dot(point - segment.a, segment_direction) / segment_length_squared, T(0), T(1));
	const QBasicVec3<T> closest = segment.a + segment_direction * segment_factor;
	return { true, segment_factor, closest, length_squared(point - closest) };
}

template<typename T>
[[nodiscard]] T point_segment_distance_squared(QBasicVec3<T> point, const QSegment3<T>& segment, T epsilon = T(0.000001))
{
	const QClosestSegmentPoint3<T> closest = closest_point_on_segment(point, segment, epsilon);
	return closest.valid ? closest.distance_squared : std::numeric_limits<T>::infinity();
}

namespace detail {

template<typename T>
[[nodiscard]] constexpr T cross_2d(QBasicVec2<T> left, QBasicVec2<T> right)
{
	return left.x * right.y - left.y * right.x;
}

template<typename T>
[[nodiscard]] constexpr T signed_area_twice_2d(QBasicVec2<T> a, QBasicVec2<T> b, QBasicVec2<T> c)
{
	return ((b.x - a.x) * (c.y - a.y)) - ((b.y - a.y) * (c.x - a.x));
}

template<typename T>
[[nodiscard]] constexpr T clamped_unit_factor(T value)
{
	return std::clamp(value, T(0), T(1));
}

template<typename T>
[[nodiscard]] T segment_factor_2d(QBasicVec2<T> point, QBasicVec2<T> segment_a, QBasicVec2<T> segment_b, T epsilon)
{
	const QBasicVec2<T> segment_direction = segment_b - segment_a;
	const T segment_length_squared = length_squared(segment_direction);
	if (segment_length_squared <= epsilon * epsilon) {
		return T(0);
	}
	return clamped_unit_factor(dot(point - segment_a, segment_direction) / segment_length_squared);
}

template<typename T>
[[nodiscard]] bool point_on_segment_2d_local(QBasicVec2<T> point, QBasicVec2<T> segment_a, QBasicVec2<T> segment_b, T epsilon)
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
[[nodiscard]] QClassifiedSegmentIntersection2<T> make_segment_intersection_point_2d(
	T first_factor, T second_factor, QBasicVec2<T> point)
{
	QClassifiedSegmentIntersection2<T> result;
	result.hit = true;
        result.kind = QSegmentIntersectionKind2::Point;
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
[[nodiscard]] QClassifiedSegmentIntersection2<T> classify_segments_2d_local(
	QBasicVec2<T> a, QBasicVec2<T> b, QBasicVec2<T> c, QBasicVec2<T> d, T epsilon)
{
	const QBasicVec2<T> first_direction = b - a;
	const QBasicVec2<T> second_direction = d - c;
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
	const QBasicVec2<T> delta = c - a;
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
		const QBasicVec2<T> point = a + first_direction * first_factor;
		return make_segment_intersection_point_2d<T>(first_factor, segment_factor_2d(point, c, d, epsilon), point);
	}

	QClassifiedSegmentIntersection2<T> result;
	result.hit = true;
        result.kind = QSegmentIntersectionKind2::Overlap;
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
[[nodiscard]] constexpr T triangle_signed_area(QBasicVec2<T> a, QBasicVec2<T> b, QBasicVec2<T> c)
{
	return detail::signed_area_twice_2d(a, b, c) * T(0.5);
}

template<typename T>
[[nodiscard]] T triangle_area_abs(QBasicVec2<T> a, QBasicVec2<T> b, QBasicVec2<T> c)
{
	return std::abs(triangle_signed_area(a, b, c));
}

template<typename T>
[[nodiscard]] bool point_on_segment_2d(QBasicVec2<T> point, QBasicVec2<T> segment_a, QBasicVec2<T> segment_b, T epsilon = T(0.000001))
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
[[nodiscard]] bool segments_intersect_2d(QBasicVec2<T> a, QBasicVec2<T> b, QBasicVec2<T> c, QBasicVec2<T> d, T epsilon = T(0.000001))
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

[[nodiscard]] QClassifiedSegmentIntersection2<double> intersect_segments_2d(
	QVec2d a, QVec2d b, QVec2d c, QVec2d d, double epsilon = 0.000001);

template<typename T>
[[nodiscard]] QClassifiedSegmentIntersection2<T> intersect_segments_2d(
	QBasicVec2<T> a, QBasicVec2<T> b, QBasicVec2<T> c, QBasicVec2<T> d, T epsilon = T(0.000001))
{
	return detail::classify_segments_2d_local(a, b, c, d, epsilon);
}

template<typename T>
[[nodiscard]] QSegmentIntersection2<T> proper_segment_intersection_2d(
	QBasicVec2<T> a, QBasicVec2<T> b, QBasicVec2<T> c, QBasicVec2<T> d, T epsilon = T(0.000001))
{
	const QBasicVec2<T> first_direction = b - a;
	const QBasicVec2<T> second_direction = d - c;
	const T denominator = detail::cross_2d(first_direction, second_direction);
	if (std::abs(denominator) <= epsilon) {
		return {};
	}

	const QBasicVec2<T> delta = c - a;
	const T first_factor = detail::cross_2d(delta, second_direction) / denominator;
	const T second_factor = detail::cross_2d(delta, first_direction) / denominator;
	if (first_factor <= epsilon || first_factor >= T(1) - epsilon ||
			second_factor <= epsilon || second_factor >= T(1) - epsilon) {
		return {};
	}

	return { true, first_factor, second_factor, a + first_direction * first_factor };
}

template<typename T>
[[nodiscard]] bool point_in_or_on_polygon_2d(QBasicVec2<T> point, std::span<const QBasicVec2<T>> polygon, T epsilon = T(0.000001))
{
	if (polygon.size() < 3U) {
		return false;
	}

	bool inside = false;
	for (std::size_t index = 0, previous = polygon.size() - 1U; index < polygon.size(); previous = index++) {
		const QBasicVec2<T> a = polygon[previous];
		const QBasicVec2<T> b = polygon[index];
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
[[nodiscard]] T polygon_signed_area(std::span<const QBasicVec2<T>> points)
{
	if (points.size() < 3) {
		return T(0);
	}
	T area = T(0);
	for (std::size_t index = 0; index < points.size(); ++index) {
		const QBasicVec2<T> current = points[index];
		const QBasicVec2<T> next = points[(index + 1U) % points.size()];
		area += current.x * next.y - next.x * current.y;
	}
	return area * T(0.5);
}

[[nodiscard]] std::vector<QTriangleIndices> triangulate_projected_polygon(
	std::span<const QVec2d> points,
	double epsilon = 0.000001,
	double coverage_tolerance_ratio = 0.001);

template<typename T>
[[nodiscard]] std::vector<QTriangleIndices> triangulate_projected_polygon(
	std::span<const QBasicVec2<T>> points,
	double epsilon = 0.000001,
	double coverage_tolerance_ratio = 0.001)
{
	std::vector<QVec2d> double_points;
	double_points.reserve(points.size());
	for (const QBasicVec2<T> point : points) {
		double_points.push_back(QVec2d { static_cast<double>(point.x), static_cast<double>(point.y) });
	}
	return triangulate_projected_polygon(std::span<const QVec2d>(double_points), epsilon, coverage_tolerance_ratio);
}

template<typename T>
[[nodiscard]] QThreePlaneIntersection<T> intersect_three_planes(
	const QPlane3<T>& first, const QPlane3<T>& second, const QPlane3<T>& third, T epsilon = T(0.000001))
{
	const QBasicVec3<T> second_cross_third = cross(second.normal, third.normal);
	const T denominator = dot(first.normal, second_cross_third);
	if (std::abs(denominator) <= epsilon) {
		return {};
	}

	const QBasicVec3<T> point =
		(second_cross_third * first.d +
			cross(third.normal, first.normal) * second.d +
			cross(first.normal, second.normal) * third.d) /
		denominator;
	return { true, point };
}

template<typename T>
[[nodiscard]] QRayPlaneHit<T> intersect_ray_plane(const QRay3<T>& ray, const QPlane3<T>& plane, T epsilon = T(0.000001))
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

[[nodiscard]] QRayPlaneHit<float> intersect_ray_plane(
	const QRay3<float>& ray, const QPlane3<float>& plane, float epsilon = 0.000001F);

template<typename T>
[[nodiscard]] QLinePlaneHit<T> intersect_line_plane(const QLine3<T>& line, const QPlane3<T>& plane, T epsilon = T(0.000001))
{
	const QPlane3<T> normalized_plane = normalize_plane(plane, epsilon);
	if (length_squared(normalized_plane.normal) <= epsilon * epsilon ||
			length_squared(line.direction) <= epsilon * epsilon) {
		return {};
	}

	const T denominator = dot(normalized_plane.normal, line.direction);
	const T origin_distance = signed_distance_to_plane(line.origin, normalized_plane);
	if (std::abs(denominator) <= epsilon) {
          return {
              std::abs(origin_distance) <= epsilon
                  ? QPlaneIntersectionType::Coplanar
                  : QPlaneIntersectionType::None,
              T(0),
              line.origin,
          };
        }

	const T parameter = -origin_distance / denominator;
        return {QPlaneIntersectionType::Point, parameter,
                line.origin + line.direction * parameter};
}

template<typename T>
[[nodiscard]] QSegmentPlaneHit<T> intersect_segment_plane(const QSegment3<T>& segment, const QPlane3<T>& plane, T epsilon = T(0.000001))
{
	const QPlane3<T> normalized_plane = normalize_plane(plane, epsilon);
	if (length_squared(normalized_plane.normal) <= epsilon * epsilon) {
		return {};
	}

	const T first_distance = signed_distance_to_plane(segment.a, normalized_plane);
	const T second_distance = signed_distance_to_plane(segment.b, normalized_plane);
	const bool first_on_plane = std::abs(first_distance) <= epsilon;
	const bool second_on_plane = std::abs(second_distance) <= epsilon;
	const QBasicVec3<T> segment_direction = segment.b - segment.a;
	if (length_squared(segment_direction) <= epsilon * epsilon) {
          return first_on_plane
                     ? QSegmentPlaneHit<T>{QPlaneIntersectionType::Point, T(0),
                                           segment.a}
                     : QSegmentPlaneHit<T>{};
        }
	if (first_on_plane && second_on_plane) {
          return {QPlaneIntersectionType::Coplanar, T(0), segment.a};
        }
	if (first_on_plane) {
          return {QPlaneIntersectionType::Point, T(0), segment.a};
        }
	if (second_on_plane) {
          return {QPlaneIntersectionType::Point, T(1), segment.b};
        }
	if ((first_distance > T(0) && second_distance > T(0)) ||
			(first_distance < T(0) && second_distance < T(0))) {
		return {};
	}

	const T segment_factor = first_distance / (first_distance - second_distance);
        return {QPlaneIntersectionType::Point, segment_factor,
                segment.a + segment_direction * segment_factor};
}

template<typename T>
[[nodiscard]] QBasicVec3<T> triangle_area_vector(const QTriangle3<T>& triangle)
{
	return cross(triangle.b - triangle.a, triangle.c - triangle.a) * T(0.5);
}

template<typename T>
[[nodiscard]] T triangle_area(const QTriangle3<T>& triangle)
{
	return length(triangle_area_vector(triangle));
}

template<typename T>
[[nodiscard]] QBasicVec3<T> triangle_unit_normal(const QTriangle3<T>& triangle, T epsilon = T(0.000001))
{
	return normalize_or_zero(cross(triangle.b - triangle.a, triangle.c - triangle.a), epsilon);
}

template<typename T>
[[nodiscard]] QAabb3<T> triangle_aabb(const QTriangle3<T>& triangle)
{
	QAabb3<T> bounds = empty_aabb3<T>();
	aabb_include(bounds, triangle.a);
	aabb_include(bounds, triangle.b);
	aabb_include(bounds, triangle.c);
	return bounds;
}

template<typename T>
[[nodiscard]] Barycentric3<T> barycentric_coordinates(QBasicVec3<T> point, const QTriangle3<T>& triangle, T epsilon = T(0.000001))
{
	const QBasicVec3<T> v0 = triangle.b - triangle.a;
	const QBasicVec3<T> v1 = triangle.c - triangle.a;
	const QBasicVec3<T> v2 = point - triangle.a;
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
	QBasicVec3<T> point, const QTriangle3<T>& triangle, T barycentric_epsilon = T(0.000001), T plane_epsilon = T(0.000001))
{
	const QBasicVec3<T> normal = triangle_unit_normal(triangle, plane_epsilon);
	if (length_squared(normal) <= plane_epsilon * plane_epsilon) {
		return false;
	}
	const QPlane3<T> plane = plane_from_point_normal(triangle.a, normal, plane_epsilon);
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
[[nodiscard]] QRayTriangleHit<T> intersect_ray_triangle(const QRay3<T>& ray, const QTriangle3<T>& triangle, T epsilon = T(0.000001))
{
	const QBasicVec3<T> edge1 = triangle.b - triangle.a;
	const QBasicVec3<T> edge2 = triangle.c - triangle.a;
	const QBasicVec3<T> pvec = cross(ray.direction, edge2);
	const T determinant = dot(edge1, pvec);
	if (std::abs(determinant) <= epsilon) {
		return {};
	}

	const T inverse_determinant = T(1) / determinant;
	const QBasicVec3<T> tvec = ray.origin - triangle.a;
	const T u = dot(tvec, pvec) * inverse_determinant;
	if (u < T(0) || u > T(1)) {
		return {};
	}

	const QBasicVec3<T> qvec = cross(tvec, edge1);
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

[[nodiscard]] QRayTriangleHit<double> intersect_ray_triangle(const QRay3<double>& ray, const QTriangle3<double>& triangle,
	double epsilon = 0.000001);
[[nodiscard]] QRayAabbHit<double> intersect_ray_aabb(const QRay3<double>& ray, const QAabb3<double>& aabb,
	double epsilon = 0.000001);

template<typename T>
[[nodiscard]] QRayAabbHit<T> intersect_ray_aabb(const QRay3<T>& ray, const QAabb3<T>& aabb, T epsilon = T(0.000001))
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
[[nodiscard]] QRayBoundedTriangleHit<T> intersect_ray_bounded_triangle(
	const QRay3<T>& ray,
	const QTriangle3<T>& triangle,
	const QAabb3<T>& bounds,
	QRayBoundedTriangleQueryOptions<T> options = {})
{
	if (options.max_distance < options.min_distance) {
		return {};
	}

	const QRayAabbHit<T> bounds_hit = intersect_ray_aabb(ray, bounds, options.epsilon);
	if (!bounds_hit.hit ||
			bounds_hit.min_distance > options.max_distance ||
			bounds_hit.max_distance < options.min_distance) {
		return {};
	}

	const QRayTriangleHit<T> triangle_hit = intersect_ray_triangle(ray, triangle, options.epsilon);
	if (!triangle_hit.hit ||
			triangle_hit.distance < options.min_distance ||
			triangle_hit.distance > options.max_distance) {
		return {};
	}

	return { true, 0U, bounds_hit, triangle_hit };
}

template<typename T>
[[nodiscard]] QRayBoundedTriangleHit<T> intersect_ray_bounded_triangles(
	const QRay3<T>& ray,
	std::span<const QTriangle3<T>> triangles,
	std::span<const QAabb3<T>> bounds,
	QRayBoundedTriangleQueryOptions<T> options = {})
{
	if (triangles.size() != bounds.size()) {
		return {};
	}

	QRayBoundedTriangleHit<T> best;
	for (std::size_t index = 0; index < triangles.size(); ++index) {
		QRayBoundedTriangleQueryOptions<T> candidate_options = options;
		if (best.hit) {
			candidate_options.max_distance = std::min(candidate_options.max_distance, best.triangle_hit.distance);
		}
		QRayBoundedTriangleHit<T> candidate =
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

[[nodiscard]] QClosestRayPoint<double> closest_point_on_ray(QVec3d point, const QRay3<double>& ray, double epsilon = 0.000001);
[[nodiscard]] double point_ray_distance_squared(QVec3d point, const QRay3<double>& ray, double epsilon = 0.000001);

template<typename T>
[[nodiscard]] QClosestRayPoint<T> closest_point_on_ray(QBasicVec3<T> point, const QRay3<T>& ray, T epsilon = T(0.000001))
{
	const T direction_length_squared = length_squared(ray.direction);
	if (direction_length_squared <= epsilon * epsilon) {
		return {};
	}

	const T distance = std::max(T(0), dot(point - ray.origin, ray.direction) / direction_length_squared);
	const QBasicVec3<T> closest = ray.origin + ray.direction * distance;
	return { true, distance, closest, length_squared(point - closest) };
}

template<typename T>
[[nodiscard]] T point_ray_distance_squared(QBasicVec3<T> point, const QRay3<T>& ray, T epsilon = T(0.000001))
{
	const QClosestRayPoint<T> closest = closest_point_on_ray(point, ray, epsilon);
	return closest.valid ? closest.distance_squared : std::numeric_limits<T>::infinity();
}

[[nodiscard]] QSegmentRayClosestPoints<double> closest_points_segment_ray(const QSegment3<double>& segment, const QRay3<double>& ray,
	double epsilon = 0.000001);
[[nodiscard]] double segment_ray_distance_squared(const QSegment3<double>& segment, const QRay3<double>& ray, double epsilon = 0.000001);

[[nodiscard]] QSegmentSegmentClosestPoints<double> closest_points_segment_segment(
	const QSegment3<double>& first, const QSegment3<double>& second, double epsilon = 0.000001);
[[nodiscard]] double segment_segment_distance_squared(
	const QSegment3<double>& first, const QSegment3<double>& second, double epsilon = 0.000001);

template<typename T>
[[nodiscard]] QSegmentRayClosestPoints<T> closest_points_segment_ray(const QSegment3<T>& segment, const QRay3<T>& ray, T epsilon = T(0.000001))
{
	const QBasicVec3<T> segment_direction = segment.b - segment.a;
	const T segment_length_squared = length_squared(segment_direction);
	const T ray_length_squared = length_squared(ray.direction);
	if (ray_length_squared <= epsilon * epsilon) {
		return {};
	}

	const auto candidate_for = [&](T segment_factor, T ray_distance) {
		QSegmentRayClosestPoints<T> candidate;
		candidate.valid = true;
		candidate.segment_factor = std::clamp(segment_factor, T(0), T(1));
		candidate.ray_distance = std::max(T(0), ray_distance);
		candidate.segment_point = segment.a + segment_direction * candidate.segment_factor;
		candidate.ray_point = ray.origin + ray.direction * candidate.ray_distance;
		candidate.distance_squared = length_squared(candidate.segment_point - candidate.ray_point);
		return candidate;
	};

	QSegmentRayClosestPoints<T> best = candidate_for(T(0), dot(segment.a - ray.origin, ray.direction) / ray_length_squared);
	const auto keep_best = [&](QSegmentRayClosestPoints<T> candidate) {
		if (candidate.distance_squared < best.distance_squared) {
			best = candidate;
		}
	};

	keep_best(candidate_for(T(1), dot(segment.b - ray.origin, ray.direction) / ray_length_squared));

	if (segment_length_squared <= epsilon * epsilon) {
		return best;
	}

	keep_best(candidate_for(dot(ray.origin - segment.a, segment_direction) / segment_length_squared, T(0)));

	const QBasicVec3<T> w0 = segment.a - ray.origin;
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
[[nodiscard]] T segment_ray_distance_squared(const QSegment3<T>& segment, const QRay3<T>& ray, T epsilon = T(0.000001))
{
	const QSegmentRayClosestPoints<T> closest = closest_points_segment_ray(segment, ray, epsilon);
	return closest.valid ? closest.distance_squared : std::numeric_limits<T>::infinity();
}

template<typename T>
[[nodiscard]] QSegmentSegmentClosestPoints<T> closest_points_segment_segment(
	const QSegment3<T>& first, const QSegment3<T>& second, T epsilon = T(0.000001))
{
	const QBasicVec3<T> first_direction = first.b - first.a;
	const QBasicVec3<T> second_direction = second.b - second.a;
	const T first_length_squared = length_squared(first_direction);
	const T second_length_squared = length_squared(second_direction);

	const auto candidate_for = [&](T first_factor, T second_factor) {
		QSegmentSegmentClosestPoints<T> candidate;
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
		const QClosestSegmentPoint3<T> closest = closest_point_on_segment(first.a, second, epsilon);
		return closest.valid ? candidate_for(T(0), closest.segment_factor) : QSegmentSegmentClosestPoints<T> {};
	}
	if (second_length_squared <= epsilon * epsilon) {
		const QClosestSegmentPoint3<T> closest = closest_point_on_segment(second.a, first, epsilon);
		return closest.valid ? candidate_for(closest.segment_factor, T(0)) : QSegmentSegmentClosestPoints<T> {};
	}

	QSegmentSegmentClosestPoints<T> best = candidate_for(T(0), closest_point_on_segment(first.a, second, epsilon).segment_factor);
	const auto keep_best = [&](QSegmentSegmentClosestPoints<T> candidate) {
		if (!best.valid || candidate.distance_squared < best.distance_squared) {
			best = candidate;
		}
	};

	keep_best(candidate_for(T(1), closest_point_on_segment(first.b, second, epsilon).segment_factor));
	keep_best(candidate_for(closest_point_on_segment(second.a, first, epsilon).segment_factor, T(0)));
	keep_best(candidate_for(closest_point_on_segment(second.b, first, epsilon).segment_factor, T(1)));

	const QBasicVec3<T> delta = first.a - second.a;
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
[[nodiscard]] T segment_segment_distance_squared(const QSegment3<T>& first, const QSegment3<T>& second, T epsilon = T(0.000001))
{
	const QSegmentSegmentClosestPoints<T> closest = closest_points_segment_segment(first, second, epsilon);
	return closest.valid ? closest.distance_squared : std::numeric_limits<T>::infinity();
}

[[nodiscard]] QLineLineClosestPoints<double> closest_points_line_line(const QLine3<double>& first, const QLine3<double>& second,
	double epsilon = 0.000001);
[[nodiscard]] double line_line_distance_squared(const QLine3<double>& first, const QLine3<double>& second, double epsilon = 0.000001);

template<typename T>
[[nodiscard]] QLineLineClosestPoints<T> closest_points_line_line(const QLine3<T>& first, const QLine3<T>& second, T epsilon = T(0.000001))
{
	const T first_length_squared = length_squared(first.direction);
	const T second_length_squared = length_squared(second.direction);
	if (first_length_squared <= epsilon * epsilon || second_length_squared <= epsilon * epsilon) {
		return {};
	}

	const QBasicVec3<T> delta = first.origin - second.origin;
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

	const QBasicVec3<T> first_point = first.origin + first.direction * first_parameter;
	const QBasicVec3<T> second_point = second.origin + second.direction * second_parameter;
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
[[nodiscard]] T line_line_distance_squared(const QLine3<T>& first, const QLine3<T>& second, T epsilon = T(0.000001))
{
	const QLineLineClosestPoints<T> closest = closest_points_line_line(first, second, epsilon);
	return closest.valid ? closest.distance_squared : std::numeric_limits<T>::infinity();
}

template<typename T>
[[nodiscard]] QSegmentFactorClosestPoint3<T> closest_factor_on_segment_to_segment(
	const QSegment3<T>& target, const QSegment3<T>& segment, QSegmentSegmentFactorOptions<T> options = {})
{
	const QBasicVec3<T> target_direction = target.b - target.a;
	const QBasicVec3<T> segment_direction = segment.b - segment.a;
	const T target_length_squared = length_squared(target_direction);
	const T segment_length_squared = length_squared(segment_direction);
	const T min_factor = std::min(options.min_factor, options.max_factor);
	const T max_factor = std::max(options.min_factor, options.max_factor);

	const auto result_for = [&](T factor) {
		QSegmentFactorClosestPoint3<T> result;
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
		const QBasicVec3<T> midpoint = (segment.a + segment.b) * T(0.5);
		return result_for(dot(midpoint - target.a, target_direction) / target_length_squared);
	}

	const QSegmentSegmentClosestPoints<T> finite_closest = closest_points_segment_segment(target, segment, options.closest_epsilon);
	if (finite_closest.valid &&
			finite_closest.second_factor > options.parameter_epsilon &&
			finite_closest.second_factor < T(1) - options.parameter_epsilon) {
		return result_for(finite_closest.first_factor);
	}

	const QLineLineClosestPoints<T> closest = closest_points_line_line(
		QLine3<T> { target.a, target_direction },
		QLine3<T> { segment.a, segment_direction },
		options.closest_epsilon);
	return closest.valid ? result_for(closest.first_parameter) : QSegmentFactorClosestPoint3<T> {};
}

} // namespace quader_geometry

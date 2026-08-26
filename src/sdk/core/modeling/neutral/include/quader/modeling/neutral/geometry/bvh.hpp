////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/neutral/geometry/geometry.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace quader_geometry {

/**
 * Stores a reusable traversal stack shared by neutral bounds and triangle BVH queries.
 */
class QBvhTraversalScratch {
public:
	void clear()
	{
		node_stack_.clear();
	}

	void reserve(std::size_t capacity)
	{
		node_stack_.reserve(capacity);
	}

	[[nodiscard]] std::size_t capacity() const
	{
		return node_stack_.capacity();
	}

private:
	template<typename>
	friend class QBoundsBvh;

	std::vector<std::size_t> node_stack_;
};

using QTriangleBvhTraversalScratch = QBvhTraversalScratch;

/**
 * Stores the bounded ray interval used while traversing a neutral BVH.
 */
template<typename T>
/// Stores the bounded ray interval used while traversing a neutral BVH.
struct QBvhRayQueryOptions {
	T min_distance = T(0);
	T max_distance = std::numeric_limits<T>::infinity();
	T intersection_epsilon = T(0.000001);
};

namespace detail {

template<typename T>
[[nodiscard]] bool bvh_coordinate_is_finite(QBasicVec3<T> value)
{
	return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

template<typename T>
[[nodiscard]] T bvh_axis_value(QBasicVec3<T> value, QAxis axis)
{
	switch (axis) {
	case QAxis::X:
		return value.x;
	case QAxis::Y:
		return value.y;
	case QAxis::Z:
		return value.z;
	}
	return value.z;
}

template<typename T>
[[nodiscard]] bool bvh_centroid_less(QBasicVec3<T> left, QBasicVec3<T> right, QAxis primary_axis)
{
	const std::array<QAxis, 3> axes = primary_axis == QAxis::X
		? std::array { QAxis::X, QAxis::Y, QAxis::Z }
		: primary_axis == QAxis::Y
			? std::array { QAxis::Y, QAxis::Z, QAxis::X }
			: std::array { QAxis::Z, QAxis::X, QAxis::Y };
	for (const QAxis axis : axes) {
		const T left_value = bvh_axis_value(left, axis);
		const T right_value = bvh_axis_value(right, axis);
		if (left_value < right_value) {
			return true;
		}
		if (left_value > right_value) {
			return false;
		}
	}
	return false;
}

template<typename T>
[[nodiscard]] bool bvh_options_are_valid(const QBvhRayQueryOptions<T>& options)
{
	return !std::isnan(options.min_distance) &&
		!std::isnan(options.max_distance) &&
		options.max_distance >= options.min_distance;
}

} // namespace detail

/**
 * Owns a deterministic median-split index over caller-supplied primitive bounds.
 */
template<typename T>
/// Owns a deterministic median-split index over caller primitive bounds.
class QBoundsBvh {
public:
	static_assert(std::is_floating_point_v<T>);

	void clear()
	{
		primitives_.clear();
		order_.clear();
		nodes_.clear();
	}

	[[nodiscard]] bool build(
		std::span<const QAabb3<T>> bounds,
		std::span<const QBasicVec3<T>> centroids,
		std::span<const std::size_t> primitive_indices,
		std::size_t leaf_primitive_count = 8U)
	{
		clear();
		if (bounds.size() != centroids.size() ||
				bounds.size() != primitive_indices.size() ||
				leaf_primitive_count == 0U) {
			return false;
		}

		for (std::size_t index = 0; index < bounds.size(); ++index) {
			if (!aabb_is_valid(bounds[index]) ||
					!detail::bvh_coordinate_is_finite(bounds[index].min) ||
					!detail::bvh_coordinate_is_finite(bounds[index].max) ||
					!detail::bvh_coordinate_is_finite(centroids[index])) {
				clear();
				return false;
			}
		}

		leaf_primitive_count_ = leaf_primitive_count;
		primitives_.reserve(bounds.size());
		for (std::size_t index = 0; index < bounds.size(); ++index) {
			primitives_.push_back({ bounds[index], centroids[index], primitive_indices[index], index });
		}
		order_.resize(primitives_.size());
		std::iota(order_.begin(), order_.end(), std::size_t(0));
		if (order_.empty()) {
			return true;
		}
		nodes_.reserve((order_.size() * 2U) - 1U);
		(void)build_node(0U, order_.size());
		return true;
	}

	[[nodiscard]] bool build(
		std::span<const QAabb3<T>> bounds,
		std::span<const std::size_t> primitive_indices,
		std::size_t leaf_primitive_count = 8U)
	{
		std::vector<QBasicVec3<T>> centroids;
		centroids.reserve(bounds.size());
		for (const QAabb3<T>& primitive_bounds : bounds) {
			centroids.push_back(aabb_center(primitive_bounds));
		}
		return build(bounds, centroids, primitive_indices, leaf_primitive_count);
	}

	[[nodiscard]] bool build(
		std::span<const QAabb3<T>> bounds,
		std::size_t leaf_primitive_count = 8U)
	{
		std::vector<std::size_t> primitive_indices(bounds.size());
		std::iota(primitive_indices.begin(), primitive_indices.end(), std::size_t(0));
		return build(bounds, primitive_indices, leaf_primitive_count);
	}

	[[nodiscard]] bool empty() const
	{
		return nodes_.empty();
	}

	[[nodiscard]] std::size_t primitive_count() const
	{
		return primitives_.size();
	}

	[[nodiscard]] std::size_t node_count() const
	{
		return nodes_.size();
	}

	[[nodiscard]] std::size_t required_traversal_capacity() const
	{
		return nodes_.size();
	}

	/**
	 * Visits ray-overlapping primitive bounds in deterministic near-first order.
	 *
	 * The visitor receives the caller primitive index, the primitive bounds hit,
	 * and a mutable maximum distance. Returning false stops traversal. Reducing
	 * the maximum distance lets nearest-hit callers prune queued nodes.
	 */
	template<typename Visitor>
	[[nodiscard]] bool for_each_ray_candidate(
		const QRay3<T>& ray,
		QBvhRayQueryOptions<T> options,
		QBvhTraversalScratch& scratch,
		Visitor&& visitor) const
	{
		scratch.clear();
		if (empty() || !detail::bvh_options_are_valid(options)) {
			return true;
		}
		options.intersection_epsilon = std::max(options.intersection_epsilon, T(0));
		scratch.reserve(required_traversal_capacity());
		scratch.node_stack_.push_back(0U);
		T traversal_max_distance = options.max_distance;

		while (!scratch.node_stack_.empty()) {
			const std::size_t node_index = scratch.node_stack_.back();
			scratch.node_stack_.pop_back();
			const Node& node = nodes_[node_index];
			const QRayAabbHit<T> node_hit = intersect_ray_aabb<T>(ray, node.bounds, options.intersection_epsilon);
			if (!bounds_hit_overlaps_query(node_hit, options.min_distance, traversal_max_distance)) {
				continue;
			}

			if (node.count != 0U) {
				for (std::size_t offset = 0; offset < node.count; ++offset) {
					const Primitive& primitive = primitives_[order_[node.start + offset]];
					const QRayAabbHit<T> primitive_hit =
						intersect_ray_aabb<T>(ray, primitive.bounds, options.intersection_epsilon);
					if (!bounds_hit_overlaps_query(primitive_hit, options.min_distance, traversal_max_distance)) {
						continue;
					}
					if (!visitor(
							primitive.primitive_index,
							primitive.source_index,
							primitive_hit,
							traversal_max_distance)) {
						return false;
					}
					traversal_max_distance = std::min(traversal_max_distance, options.max_distance);
				}
				continue;
			}

			const ChildHit left = child_hit(ray, node.left, options, traversal_max_distance);
			const ChildHit right = child_hit(ray, node.right, options, traversal_max_distance);
			if (left.hit && right.hit) {
				const bool left_is_near = left.min_distance < right.min_distance ||
					(left.min_distance == right.min_distance && node.left < node.right);
				scratch.node_stack_.push_back(left_is_near ? node.right : node.left);
				scratch.node_stack_.push_back(left_is_near ? node.left : node.right);
			} else if (left.hit) {
				scratch.node_stack_.push_back(node.left);
			} else if (right.hit) {
				scratch.node_stack_.push_back(node.right);
			}
		}
		return true;
	}

private:
	static constexpr std::size_t kInvalidNode = std::numeric_limits<std::size_t>::max();

	/// Stores one caller primitive and its deterministic build metadata.
	struct Primitive {
		QAabb3<T> bounds;
		QBasicVec3<T> centroid;
		std::size_t primitive_index = 0U;
		std::size_t source_index = 0U;
	};

	/// Stores one immutable bounds-hierarchy node.
	struct Node {
		QAabb3<T> bounds = empty_aabb3<T>();
		std::size_t start = 0U;
		std::size_t count = 0U;
		std::size_t left = kInvalidNode;
		std::size_t right = kInvalidNode;
	};

	/// Carries the near distance used to order two child nodes.
	struct ChildHit {
		bool hit = false;
		T min_distance = T(0);
	};

	[[nodiscard]] static bool bounds_hit_overlaps_query(
		const QRayAabbHit<T>& hit,
		T min_distance,
		T max_distance)
	{
		return hit.hit && hit.min_distance <= max_distance && hit.max_distance >= min_distance;
	}

	[[nodiscard]] ChildHit child_hit(
		const QRay3<T>& ray,
		std::size_t node_index,
		const QBvhRayQueryOptions<T>& options,
		T max_distance) const
	{
		if (node_index == kInvalidNode) {
			return {};
		}
		const QRayAabbHit<T> hit =
			intersect_ray_aabb<T>(ray, nodes_[node_index].bounds, options.intersection_epsilon);
		return { bounds_hit_overlaps_query(hit, options.min_distance, max_distance), hit.min_distance };
	}

	[[nodiscard]] std::size_t build_node(std::size_t start, std::size_t count)
	{
		const std::size_t node_index = nodes_.size();
		QAabb3<T> bounds = empty_aabb3<T>();
		QAabb3<T> centroid_bounds = empty_aabb3<T>();
		for (std::size_t offset = 0; offset < count; ++offset) {
			const Primitive& primitive = primitives_[order_[start + offset]];
			aabb_include(bounds, primitive.bounds);
			aabb_include(centroid_bounds, primitive.centroid);
		}
		nodes_.push_back({});
		if (count <= leaf_primitive_count_) {
			nodes_[node_index] = { bounds, start, count, kInvalidNode, kInvalidNode };
			return node_index;
		}

		const QAxis axis = aabb_longest_axis(centroid_bounds);
		std::sort(
			order_.begin() + static_cast<std::ptrdiff_t>(start),
			order_.begin() + static_cast<std::ptrdiff_t>(start + count),
			[this, axis](std::size_t left_index, std::size_t right_index) {
				const Primitive& left = primitives_[left_index];
				const Primitive& right = primitives_[right_index];
				if (detail::bvh_centroid_less(left.centroid, right.centroid, axis)) {
					return true;
				}
				if (detail::bvh_centroid_less(right.centroid, left.centroid, axis)) {
					return false;
				}
				if (left.primitive_index != right.primitive_index) {
					return left.primitive_index < right.primitive_index;
				}
				return left.source_index < right.source_index;
			});
		const std::size_t left_count = count / 2U;
		const std::size_t left = build_node(start, left_count);
		const std::size_t right = build_node(start + left_count, count - left_count);
		nodes_[node_index] = { bounds, 0U, 0U, left, right };
		return node_index;
	}

	std::vector<Primitive> primitives_;
	std::vector<std::size_t> order_;
	std::vector<Node> nodes_;
	std::size_t leaf_primitive_count_ = 8U;
};

/**
 * Stores triangle-query-specific ray interval and stable tie behavior.
 */
template<typename T>
/// Stores triangle-query ray intervals and stable tie behavior.
struct QTriangleBvhQueryOptions : QBvhRayQueryOptions<T> {
	T tie_tolerance = T(0.000001);
};

/**
 * Stores deterministic triangle BVH construction policy.
 */
template<typename T>
/// Stores deterministic triangle BVH construction policy.
struct QTriangleBvhBuildOptions {
	std::size_t leaf_primitive_count = 8U;
	T degenerate_area_epsilon = T(0.000001);
	T bounds_padding = T(0);
};

/**
 * Represents a nearest hit in a triangle BVH.
 */
template<typename T>
/// Represents one deterministic nearest hit in a triangle BVH.
struct QTriangleBvhHit {
	bool hit = false;
	std::size_t primitive_index = std::numeric_limits<std::size_t>::max();
	std::size_t triangle_index = std::numeric_limits<std::size_t>::max();
	QRayTriangleHit<T> triangle_hit;
};

namespace detail {

/// Accepts every triangle when the caller supplies no primitive filter.
struct QTriangleBvhAcceptAll {
	template<typename T>
	[[nodiscard]] bool operator()(std::size_t, const QTriangle3<T>&) const
	{
		return true;
	}
};

} // namespace detail

/**
 * Owns a deterministic triangle BLAS built over the shared neutral bounds BVH.
 */
template<typename T>
/// Owns a deterministic triangle BLAS built over the neutral bounds BVH.
class QTriangleBvh {
public:
	static_assert(std::is_floating_point_v<T>);

	void clear()
	{
		triangles_.clear();
		source_triangle_indices_.clear();
		bounds_bvh_.clear();
	}

	[[nodiscard]] bool build(
		std::span<const QTriangle3<T>> triangles,
		std::span<const std::size_t> primitive_indices,
		QTriangleBvhBuildOptions<T> options)
	{
		clear();
		if (triangles.size() != primitive_indices.size() ||
				options.leaf_primitive_count == 0U ||
				!std::isfinite(options.degenerate_area_epsilon) ||
				options.degenerate_area_epsilon < T(0) ||
				!std::isfinite(options.bounds_padding) ||
				options.bounds_padding < T(0)) {
			return false;
		}
		std::vector<QAabb3<T>> bounds;
		std::vector<QBasicVec3<T>> centroids;
		std::vector<std::size_t> filtered_primitive_indices;
		triangles_.reserve(triangles.size());
		source_triangle_indices_.reserve(triangles.size());
		bounds.reserve(triangles.size());
		centroids.reserve(triangles.size());
		filtered_primitive_indices.reserve(triangles.size());
		for (std::size_t index = 0; index < triangles.size(); ++index) {
			const QTriangle3<T>& triangle = triangles[index];
			if (!detail::bvh_coordinate_is_finite(triangle.a) ||
					!detail::bvh_coordinate_is_finite(triangle.b) ||
					!detail::bvh_coordinate_is_finite(triangle.c)) {
				clear();
				return false;
			}
			if (triangle_area(triangle) <= options.degenerate_area_epsilon) {
				continue;
			}
			const QAabb3<T> triangle_bounds =
				aabb_expanded(triangle_aabb(triangle), options.bounds_padding);
			triangles_.push_back(triangle);
			source_triangle_indices_.push_back(index);
			bounds.push_back(triangle_bounds);
			centroids.push_back((triangle.a + triangle.b + triangle.c) / T(3));
			filtered_primitive_indices.push_back(primitive_indices[index]);
		}
		if (!bounds_bvh_.build(
				bounds,
				centroids,
				filtered_primitive_indices,
				options.leaf_primitive_count)) {
			clear();
			return false;
		}
		return true;
	}

	[[nodiscard]] bool build(
		std::span<const QTriangle3<T>> triangles,
		std::span<const std::size_t> primitive_indices,
		std::size_t leaf_primitive_count = 8U)
	{
		QTriangleBvhBuildOptions<T> options;
		options.leaf_primitive_count = leaf_primitive_count;
		return build(triangles, primitive_indices, options);
	}

	[[nodiscard]] bool build(
		std::span<const QTriangle3<T>> triangles,
		std::size_t leaf_primitive_count = 8U)
	{
		std::vector<std::size_t> primitive_indices(triangles.size());
		std::iota(primitive_indices.begin(), primitive_indices.end(), std::size_t(0));
		return build(triangles, primitive_indices, leaf_primitive_count);
	}

	[[nodiscard]] bool empty() const
	{
		return bounds_bvh_.empty();
	}

	[[nodiscard]] std::size_t primitive_count() const
	{
		return bounds_bvh_.primitive_count();
	}

	[[nodiscard]] std::size_t node_count() const
	{
		return bounds_bvh_.node_count();
	}

	[[nodiscard]] std::size_t required_traversal_capacity() const
	{
		return bounds_bvh_.required_traversal_capacity();
	}

	template<typename PrimitiveFilter>
	[[nodiscard]] QTriangleBvhHit<T> nearest_hit(
		const QRay3<T>& ray,
		QTriangleBvhQueryOptions<T> options,
		QBvhTraversalScratch& scratch,
		PrimitiveFilter&& primitive_filter) const
	{
		QTriangleBvhHit<T> best;
		if (options.max_distance < options.min_distance) {
			scratch.clear();
			return best;
		}
		const T tie_tolerance = std::isfinite(options.tie_tolerance)
			? std::max(options.tie_tolerance, T(0))
			: T(0);
		QBvhRayQueryOptions<T> bounds_options;
		bounds_options.min_distance = options.min_distance;
		bounds_options.max_distance = options.max_distance;
		bounds_options.intersection_epsilon = options.intersection_epsilon;
		(void)bounds_bvh_.for_each_ray_candidate(
			ray,
			bounds_options,
			scratch,
			[this, &ray, &options, &primitive_filter, &best, tie_tolerance](
				std::size_t primitive_index,
				std::size_t triangle_index,
				const QRayAabbHit<T>&,
				T& traversal_max_distance) {
				const QTriangle3<T>& triangle = triangles_[triangle_index];
				if (!primitive_filter(primitive_index, triangle)) {
					return true;
				}
				const T intersection_epsilon = std::max(options.intersection_epsilon, T(0));
				const QRayTriangleHit<T> hit = intersect_ray_triangle<T>(ray, triangle, intersection_epsilon);
				if (!hit.hit || hit.distance < options.min_distance || hit.distance > options.max_distance) {
					return true;
				}
				const bool distance_is_better = !best.hit || hit.distance < best.triangle_hit.distance - tie_tolerance;
				const bool distance_is_tied = best.hit && std::abs(hit.distance - best.triangle_hit.distance) <= tie_tolerance;
				const bool stable_index_is_better = distance_is_tied &&
					(primitive_index < best.primitive_index ||
						(primitive_index == best.primitive_index &&
							source_triangle_indices_[triangle_index] < best.triangle_index));
				if (distance_is_better || stable_index_is_better) {
					best = { true, primitive_index, source_triangle_indices_[triangle_index], hit };
				}
				if (best.hit) {
					traversal_max_distance = std::min(
						options.max_distance,
						best.triangle_hit.distance + tie_tolerance);
				}
				return true;
			});
		return best;
	}

	[[nodiscard]] QTriangleBvhHit<T> nearest_hit(
		const QRay3<T>& ray,
		QTriangleBvhQueryOptions<T> options,
		QBvhTraversalScratch& scratch) const
	{
		return nearest_hit(ray, options, scratch, detail::QTriangleBvhAcceptAll {});
	}

	[[nodiscard]] QTriangleBvhHit<T> nearest_hit(
		const QRay3<T>& ray,
		QBvhTraversalScratch& scratch) const
	{
		return nearest_hit(ray, QTriangleBvhQueryOptions<T> {}, scratch);
	}

	template<typename PrimitiveFilter>
	[[nodiscard]] bool any_hit(
		const QRay3<T>& ray,
		QTriangleBvhQueryOptions<T> options,
		QBvhTraversalScratch& scratch,
		PrimitiveFilter&& primitive_filter) const
	{
		if (options.max_distance < options.min_distance) {
			scratch.clear();
			return false;
		}
		bool hit_found = false;
		QBvhRayQueryOptions<T> bounds_options;
		bounds_options.min_distance = options.min_distance;
		bounds_options.max_distance = options.max_distance;
		bounds_options.intersection_epsilon = options.intersection_epsilon;
		(void)bounds_bvh_.for_each_ray_candidate(
			ray,
			bounds_options,
			scratch,
			[this, &ray, &options, &primitive_filter, &hit_found](
				std::size_t primitive_index,
				std::size_t triangle_index,
				const QRayAabbHit<T>&,
				T&) {
				const QTriangle3<T>& triangle = triangles_[triangle_index];
				if (!primitive_filter(primitive_index, triangle)) {
					return true;
				}
				const T intersection_epsilon = std::max(options.intersection_epsilon, T(0));
				const QRayTriangleHit<T> hit = intersect_ray_triangle<T>(ray, triangle, intersection_epsilon);
				hit_found = hit.hit && hit.distance >= options.min_distance && hit.distance <= options.max_distance;
				return !hit_found;
			});
		return hit_found;
	}

	[[nodiscard]] bool any_hit(
		const QRay3<T>& ray,
		QTriangleBvhQueryOptions<T> options,
		QBvhTraversalScratch& scratch) const
	{
		return any_hit(ray, options, scratch, detail::QTriangleBvhAcceptAll {});
	}

	[[nodiscard]] bool any_hit(
		const QRay3<T>& ray,
		QBvhTraversalScratch& scratch) const
	{
		return any_hit(ray, QTriangleBvhQueryOptions<T> {}, scratch);
	}

private:
	std::vector<QTriangle3<T>> triangles_;
	std::vector<std::size_t> source_triangle_indices_;
	QBoundsBvh<T> bounds_bvh_;
};

} // namespace quader_geometry

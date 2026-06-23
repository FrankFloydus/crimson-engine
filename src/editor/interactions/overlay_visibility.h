////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <mesh/polygon/document.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace quader_editor_core {

struct PolygonObject;
struct SelectionOverlayLine;
enum class SelectionOverlayLayer;
struct ViewportCameraState;

/**
 * Represents an Overlay Visibility Bounds value used by the modal modeling interaction layer.
 */
struct OverlayVisibilityBounds {
	bool empty = true;
	quader::QVec3 min;
	quader::QVec3 max;
};

/**
 * Represents an Overlay Visibility Triangle value used by the modal modeling interaction layer.
 */
struct OverlayVisibilityTriangle {
	std::uint32_t a = 0;
	std::uint32_t b = 0;
	std::uint32_t c = 0;
	quader::QVec3 normal;
	quader::QVec3 centroid;
	OverlayVisibilityBounds bounds;
};

/**
 * Represents an Overlay Visibility Node value used by the modal modeling interaction layer.
 */
struct OverlayVisibilityNode {
	OverlayVisibilityBounds bounds;
	std::size_t start = 0;
	std::size_t count = 0;
	int left = -1;
	int right = -1;
};

/**
 * Owns Overlay Visibility Occluder behavior within the modal modeling interaction layer.
 */
class OverlayVisibilityOccluder {
public:
	void clear();
	void set_mesh(std::span<const quader::QVec3> vertices, std::span<const std::uint32_t> indices);
	void set_objects(std::span<const PolygonObject> objects);
	[[nodiscard]] bool empty() const;
	[[nodiscard]] bool line_segment_is_visible(quader::QVec3 a, quader::QVec3 b, const ViewportCameraState& camera) const;
	[[nodiscard]] bool line_is_visible(const SelectionOverlayLine& line, const ViewportCameraState& camera) const;
	void append_visible_lines(std::vector<SelectionOverlayLine>& output, std::span<const SelectionOverlayLine> lines, const ViewportCameraState& camera) const;
	[[nodiscard]] std::vector<SelectionOverlayLine> visible_lines(std::span<const SelectionOverlayLine> lines, const ViewportCameraState& camera) const;
	[[nodiscard]] static bool line_layer_uses_visibility(SelectionOverlayLayer layer);

private:
	void build_triangles(std::span<const quader::QVec3> vertices, std::span<const std::uint32_t> indices);
	int build_node(std::size_t start, std::size_t count);
	[[nodiscard]] bool point_is_visible(quader::QVec3 point, const ViewportCameraState& camera) const;
	[[nodiscard]] bool hits_before_target(quader_poly::Ray ray, double target_distance, double tolerance) const;

	std::vector<quader::QVec3> vertices_;
	std::vector<OverlayVisibilityTriangle> triangles_;
	std::vector<std::size_t> order_;
	std::vector<OverlayVisibilityNode> nodes_;
	mutable std::vector<int> stack_;
};

[[nodiscard]] bool selection_overlay_lines_use_visibility(std::span<const SelectionOverlayLine> lines);

} // namespace quader_editor_core

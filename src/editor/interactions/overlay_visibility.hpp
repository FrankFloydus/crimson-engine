////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <editor/modeling/runtime/selection_types.hpp>
#include <quader/modeling/neutral/geometry/bvh.hpp>
#include <quader/modeling/neutral/polygon/document.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace quader_editor_core {

struct PolygonObject;
struct ViewportCameraState;

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
	[[nodiscard]] bool point_is_visible(quader::QVec3 point, const ViewportCameraState& camera) const;
	[[nodiscard]] bool hits_before_target(quader_poly::Ray ray, double target_distance, double tolerance) const;

	std::vector<quader_geometry::QVec3d> triangle_normals_;
	quader_geometry::QTriangleBvh<double> triangle_bvh_;
	mutable quader_geometry::QBvhTraversalScratch traversal_scratch_;
};

[[nodiscard]] bool selection_overlay_lines_use_visibility(std::span<const SelectionOverlayLine> lines);

} // namespace quader_editor_core

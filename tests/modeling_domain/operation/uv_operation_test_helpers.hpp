////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "operation_test_helpers.hpp"

#include <algorithm>
#include <limits>

namespace qdr_test {

/**
 * Represents an UV Bounds value used by the automated test suite.
 */
struct UvBounds {
	float min_u = std::numeric_limits<float>::max();
	float max_u = std::numeric_limits<float>::lowest();
	float min_v = std::numeric_limits<float>::max();
	float max_v = std::numeric_limits<float>::lowest();
};

inline UvBounds uv_bounds(const quader_poly::Face& face)
{
	UvBounds bounds;
	for (const quader::QVec2& uv : face.uvs) {
		bounds.min_u = std::min(bounds.min_u, uv.x);
		bounds.max_u = std::max(bounds.max_u, uv.x);
		bounds.min_v = std::min(bounds.min_v, uv.y);
		bounds.max_v = std::max(bounds.max_v, uv.y);
	}
	return bounds;
}

inline void assign_offset_uvs(quader_poly::Face& face)
{
	face.uvs = {
		{ 2.0F, 3.0F },
		{ 4.0F, 3.0F },
		{ 4.0F, 7.0F },
		{ 2.0F, 7.0F },
	};
}

} // namespace qdr_test

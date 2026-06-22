////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <modeling/polygon/document.h>

namespace quader_poly::document_internal {

bool ray_intersects_triangle( const Ray& ray, const Vec3& a, const Vec3& b, const Vec3& c, float& distance, Vec3& position);
float point_ray_distance(const Ray& ray, const Vec3& point, float& ray_distance);
float segment_ray_distance(const Ray& ray, const Vec3& a, const Vec3& b, float& ray_distance, Vec3& position);
} // namespace quader_poly::document_internal

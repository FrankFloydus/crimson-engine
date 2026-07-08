////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <mesh/polygon/document.hpp>

namespace quader_poly::document_internal {

bool ray_intersects_triangle( const Ray& ray, const quader::QVec3& a, const quader::QVec3& b, const quader::QVec3& c, float& distance, quader::QVec3& position);
float point_ray_distance(const Ray& ray, const quader::QVec3& point, float& ray_distance);
float segment_ray_distance(const Ray& ray, const quader::QVec3& a, const quader::QVec3& b, float& ray_distance, quader::QVec3& position);
} // namespace quader_poly::document_internal

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

namespace quader::modeling {

/**
 * SDK-owned two-dimensional vector value.
 */
struct Vec2 {
  float x = 0.0F;
  float y = 0.0F;
};

/**
 * SDK-owned three-dimensional vector value.
 */
struct Vec3 {
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
};

/**
 * Renderer-neutral affine transform axes and origin.
 */
struct Transform3 {
  Vec3 x_axis{1.0F, 0.0F, 0.0F};
  Vec3 y_axis{0.0F, 1.0F, 0.0F};
  Vec3 z_axis{0.0F, 0.0F, 1.0F};
  Vec3 origin{};
};

/**
 * SDK ray value used by portable picking and tool contracts.
 */
struct Ray {
  Vec3 origin{};
  Vec3 direction{0.0F, 0.0F, 1.0F};
};

} // namespace quader::modeling

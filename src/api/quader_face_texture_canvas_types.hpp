////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace quader {

/// UV phase stays double precision even for faces far from the sampling origin.
struct QuaderFaceTextureUvOffset final {
  double x = 0.0;
  double y = 0.0;
};

/// Immutable planar-face presentation. Coordinates are map units, with Y up;
/// the row-major affine map produces the renderer's effective normalized UVs.
struct QuaderFaceTextureCanvas final {
  std::size_t selected_face_count = 0;
  bool editable = false;
  std::string disabled_reason;
  std::uint64_t selection_token = 0;
  QuaderModelingObjectId object_id = kInvalidQuaderModelingObjectId;
  QuaderElementId face_id = kInvalidQuaderElementId;
  std::vector<QVec2> outline;
  std::array<double, 4> uv_from_plane{1.0, 0.0, 0.0, 1.0};
  QuaderFaceTextureUvOffset uv_offset;
  ProjectAssetId material_id;
  AssetId embedded_material = kInvalidAssetId;
  QVec2 texture_extent{64.0f, 64.0f};
  bool texture_available = false;
  double repeat_width = 0.0;
  double repeat_height = 0.0;
  /// Signed U-axis orientation in the upright face plane, in [-180, 180).
  double rotation_degrees = 0.0;
  bool gesture_active = false;
};

/// Selects background translation, an image-axis repeat dimension, or pivot rotation.
enum class QuaderFaceTextureGestureKind {
  Translate,
  ScaleWidth,
  ScaleHeight,
  Rotate,
};

/// Captures the pointer and opposite-edge/central pivot in face-plane units.
struct QuaderFaceTextureGestureBegin final {
  QuaderFaceTextureGestureKind kind = QuaderFaceTextureGestureKind::Translate;
  std::uint64_t selection_token = 0;
  QVec2 start;
  QVec2 pivot;
};

/// Absolute pointer intent is always evaluated against the gesture baseline.
struct QuaderFaceTextureGestureUpdate final {
  QVec2 current;
  bool precision = false;
  bool snap = false;
};

} // namespace quader

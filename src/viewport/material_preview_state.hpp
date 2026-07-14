////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/types.hpp"

#include <cstdint>

namespace quader {

/**
 * Identifies one retained test mesh in the Standard Material preview.
 */
enum class MaterialPreviewMeshKind : std::uint8_t {
  Sphere,
  Box,
  RoundedBox,
  Cylinder,
  Plane,
};

/**
 * Selects the isolated presentation path used by the material preview.
 */
enum class MaterialPreviewShadingMode : std::uint8_t {
  Wireframe,
  Shaded,
  Rendered,
};

/**
 * Material previews deliberately expose no orthographic projection state.
 */
enum class MaterialPreviewProjection : std::uint8_t {
  Perspective,
};

/**
 * Carries already-routed semantic input for one material-preview update.
 * UI backends are responsible for mapping their primary-button drag to the
 * tumble action before constructing this frame.
 */
struct MaterialPreviewInteractionFrame {
  QVec2 tumble_delta_pixels;
  float zoom_wheel_steps = 0.0f;
  bool tumble_active = false;
  bool reset_requested = false;
};

/**
 * Describes which preview state transitions were accepted from an input frame.
 */
struct MaterialPreviewInteractionResult {
  bool changed = false;
  bool tumbled = false;
  bool zoomed = false;
  bool reset = false;
};

/**
 * Owns renderer-neutral state for the isolated Standard Material preview.
 */
class MaterialPreviewState final {
public:
  static constexpr float kDefaultCameraDistance = 3.2f;
  static constexpr float kMinimumCameraDistance = 1.5f;
  static constexpr float kMaximumCameraDistance = 8.0f;
  static constexpr float kTumbleRadiansPerPixel = 0.008f;
  static constexpr float kMaximumPitchRadians = 1.56079632679f;

  [[nodiscard]] MaterialPreviewMeshKind mesh_kind() const noexcept {
    return mesh_kind_;
  }
  [[nodiscard]] MaterialPreviewShadingMode shading_mode() const noexcept {
    return shading_mode_;
  }
  [[nodiscard]] constexpr MaterialPreviewProjection projection() const
      noexcept {
    return MaterialPreviewProjection::Perspective;
  }
  [[nodiscard]] float yaw_radians() const noexcept { return yaw_radians_; }
  [[nodiscard]] float pitch_radians() const noexcept {
    return pitch_radians_;
  }
  [[nodiscard]] float camera_distance() const noexcept {
    return camera_distance_;
  }
  [[nodiscard]] QQuat object_rotation() const noexcept;
  [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

  bool set_mesh_kind(MaterialPreviewMeshKind mesh_kind) noexcept;
  bool set_shading_mode(MaterialPreviewShadingMode shading_mode) noexcept;
  bool reset_view() noexcept;

private:
  friend class MaterialPreviewInputRouter;

  bool tumble(QVec2 delta_pixels) noexcept;
  bool zoom(float wheel_steps) noexcept;
  void mark_changed() noexcept;

  MaterialPreviewMeshKind mesh_kind_ = MaterialPreviewMeshKind::Sphere;
  MaterialPreviewShadingMode shading_mode_ =
      MaterialPreviewShadingMode::Rendered;
  float yaw_radians_ = 0.0f;
  float pitch_radians_ = 0.0f;
  float camera_distance_ = kDefaultCameraDistance;
  std::uint64_t revision_ = 1u;
};

/**
 * Routes semantic tumble, zoom, and reset input into preview-owned state.
 */
class MaterialPreviewInputRouter final {
public:
  [[nodiscard]] MaterialPreviewInteractionResult
  route(MaterialPreviewState &state,
        const MaterialPreviewInteractionFrame &input) const noexcept;
};

} // namespace quader

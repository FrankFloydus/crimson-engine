////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "viewport/viewport_settings.hpp"

namespace quader {

/**
 * Resolves renderer-owned instance-edit presentation without changing scene or
 * material state. Colors are authored sRGB; material binding converts the
 * context color to linear RGB before the shared shader applies it.
 */
struct InstanceEditStyle {
  ViewportSettingColor background;
  ViewportSettingColor context;
  float background_blend = 0.0f;
  float context_strength = 0.0f;
  float context_tint_amount = 0.0f;
  float context_desaturation = 0.0f;
  float grid_fade = 0.0f;

  [[nodiscard]] static InstanceEditStyle
  from_settings(const ViewportSettings &settings);

  /** Blends the configured background RGB, weighted by its alpha, while
   * preserving the original clear alpha. */
  [[nodiscard]] ViewportSettingColor
  resolve_background_color(ViewportSettingColor source, bool active) const;

  /** Applies the shared shader's linear RGB policy to an authored sRGB color.
   * Context alpha is the target opacity multiplier, not replacement alpha. */
  [[nodiscard]] ViewportSettingColor
  resolve_context_color(ViewportSettingColor source, bool dimmed) const;

  /** Fades grid and axis opacity without changing their authored RGB. */
  [[nodiscard]] ViewportSettingColor
  resolve_grid_color(ViewportSettingColor source, bool active) const;
};

} // namespace quader

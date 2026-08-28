////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/instance_edit_style.hpp"

#include "core/color_space.hpp"

#include <algorithm>
#include <cmath>

namespace quader {

InstanceEditStyle
InstanceEditStyle::from_settings(const ViewportSettings &settings) {
  const auto unit_value = [&](ViewportFloatSettingId id) {
    return std::clamp(settings.float_value(id), 0.0f, 1.0f);
  };
  return {
      .background = settings.color(ViewportColorSettingId::InstanceEditBackground),
      .context = settings.color(ViewportColorSettingId::InstanceEditContext),
      .background_blend =
          unit_value(ViewportFloatSettingId::InstanceEditBackgroundBlend),
      .context_strength =
          unit_value(ViewportFloatSettingId::InstanceEditContextStrength),
      .context_tint_amount =
          unit_value(ViewportFloatSettingId::InstanceEditContextTintAmount),
      .context_desaturation =
          unit_value(ViewportFloatSettingId::InstanceEditContextDesaturation),
      .grid_fade = unit_value(ViewportFloatSettingId::InstanceEditGridFade),
  };
}

ViewportSettingColor InstanceEditStyle::resolve_background_color(
    ViewportSettingColor source, bool active) const {
  const float amount = active ? std::clamp(background_blend, 0.0f, 1.0f) *
                                    std::clamp(background.a, 0.0f, 1.0f)
                              : 0.0f;
  if (amount == 0.0f) {
    return source;
  }
  return {std::lerp(source.r, background.r, amount),
          std::lerp(source.g, background.g, amount),
          std::lerp(source.b, background.b, amount), source.a};
}

ViewportSettingColor InstanceEditStyle::resolve_grid_color(
    ViewportSettingColor source, bool active) const {
  if (active) {
    source.a *= 1.0f - std::clamp(grid_fade, 0.0f, 1.0f);
  }
  return source;
}

ViewportSettingColor InstanceEditStyle::resolve_context_color(
    ViewportSettingColor source, bool dimmed) const {
  const float amount =
      dimmed ? std::clamp(context_strength, 0.0f, 1.0f) : 0.0f;
  if (amount == 0.0f) {
    return source;
  }
  // Keep the linear RGB formula aligned with instance_edit.glsl. The semantic
  // overlay fallback supplies authored sRGB, unlike the shader's linear inputs.
  const float source_r = srgb_to_linear(source.r);
  const float source_g = srgb_to_linear(source.g);
  const float source_b = srgb_to_linear(source.b);
  const float luminance =
      source_r * 0.2126f + source_g * 0.7152f + source_b * 0.0722f;
  const float desaturation = std::clamp(context_desaturation, 0.0f, 1.0f);
  const float tint_amount = std::clamp(context_tint_amount, 0.0f, 1.0f);
  const auto channel = [&](float value, float tint) {
    const float target = std::lerp(std::lerp(value, luminance, desaturation),
                                   srgb_to_linear(tint), tint_amount);
    return linear_to_srgb(std::lerp(value, target, amount));
  };
  return {
      channel(source_r, context.r),
      channel(source_g, context.g),
      channel(source_b, context.b),
      source.a * std::lerp(1.0f, std::clamp(context.a, 0.0f, 1.0f), amount),
  };
}

} // namespace quader

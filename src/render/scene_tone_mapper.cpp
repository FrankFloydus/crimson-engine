////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/scene_tone_mapper.hpp"

#include <math/vec3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

// The AgX matrices and allenwp curve below are adapted from Godot Engine (MIT).
// Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md).
// Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.
// See thirdparty/licenses/godot-MIT.txt and thirdparty/godot-agx-PROVENANCE.md.
// The port uses Godot 4.7's SDR defaults, not Blender's full AgX display transform.

namespace quader {
namespace {

using Color = std::array<double, 3>;
using Matrix = std::array<Color, 3>;

// D65 linear-light primary conversions. Godot expects Rec.709, while Filament
// passes and receives Rec.2020. Transfer functions belong to ColorGrading.
constexpr Matrix kRec2020ToRec709 = {{
    {1.660491002108435, -0.587641138788550, -0.072849863319885},
    {-0.124550474521591, 1.132899897125960, -0.008349422604369},
    {-0.018150763354905, -0.100578898008007, 1.118729661362912},
}};
constexpr Matrix kRec709ToRec2020 = {{
    {0.627403895934699, 0.329283038377883, 0.043313065687418},
    {0.069097289358232, 0.919540395075459, 0.011362315566309},
    {0.016391438875150, 0.088013307877226, 0.895595253247624},
}};

// Godot's GLSL constructors are column-major; these C++ constants are rows.
constexpr Matrix kGodotAgxInset = {{
    {0.544814746488245, 0.373787398372697, 0.0813978551390581},
    {0.140416948464053, 0.754137554567394, 0.105445496968552},
    {0.0888104196149096, 0.178871756420858, 0.732317823964232},
}};
constexpr Matrix kGodotAgxOutset = {{
    {1.96488741169489, -0.855988495690215, -0.108898916004672},
    {-0.299313364904742, 1.32639796461980, -0.0270845997150571},
    {-0.164352742528393, -0.238183969428088, 1.40253671195648},
}};

/// Applies a row-major primary transform with headroom for finite HDR float input.
[[nodiscard]] Color transform(const Matrix &matrix, const Color &color) noexcept {
  Color result{};
  for (std::size_t row = 0; row < result.size(); ++row) {
    result[row] = matrix[row][0] * color[0] + matrix[row][1] * color[1] +
                  matrix[row][2] * color[2];
  }
  return result;
}

/// Keeps invalid diagnostic inputs from producing an unusable color-grading LUT.
[[nodiscard]] double finite_channel(float channel) noexcept {
  if (std::isnan(channel)) {
    return 0.0;
  }
  constexpr double maximum = std::numeric_limits<float>::max();
  return std::clamp(static_cast<double>(channel), -maximum, maximum);
}

class GodotAgxToneMapper final : public filament::ToneMapper {
public:
  GodotAgxToneMapper() noexcept {
    const double crossover_power = std::pow(kCrossover, kContrast);
    toe_a_ = (1.0 / kCrossover - 1.0) * crossover_power;
    const double slope_denominator = crossover_power + toe_a_;
    slope_ = kContrast * std::pow(kCrossover, kContrast - 1.0) * toe_a_ /
             (slope_denominator * slope_denominator);
    width_ = (kWhite - kCrossover) * (kWhite - kCrossover) /
             (kOutputMaximum - kCrossover) * slope_;
  }

  filament::math::float3 operator()(filament::math::float3 input) const noexcept override {
    Color color = transform(kRec2020ToRec709,
                            {finite_channel(input.r), finite_channel(input.g),
                             finite_channel(input.b)});
    for (double &channel : color) {
      channel = std::max(0.0, channel);
    }
    color = transform(kGodotAgxInset, color);
    for (double &channel : color) {
      channel = std::min(kOutputMaximum, curve(channel));
    }
    color = transform(kGodotAgxOutset, color);
    color = transform(kRec709ToRec2020, color);
    // Preserve the outset's negative channels for Filament's later gamut clamp.
    return {static_cast<float>(color[0]), static_cast<float>(color[1]),
            static_cast<float>(color[2])};
  }

private:
  /// Godot's linear-domain toe/shoulder preserves 18% gray and clips at 16.29.
  [[nodiscard]] double curve(double value) const noexcept {
    if (value < kCrossover) {
      const double toe = std::pow(value, kContrast);
      return toe / (toe + toe_a_);
    }
    const double shoulder = value - kCrossover;
    const double scaled = slope_ * shoulder;
    return scaled * (1.0 + shoulder / width_) /
               (1.0 + scaled / (kOutputMaximum - kCrossover)) +
           kCrossover;
  }

  static constexpr double kCrossover = 0.18;
  static constexpr double kContrast = 1.25;
  static constexpr double kWhite = 16.29;
  static constexpr double kOutputMaximum = 1.0;
  double toe_a_ = 0.0;
  double slope_ = 0.0;
  double width_ = 0.0;
};

} // namespace

const filament::ToneMapper &scene_tone_mapper(SceneToneMapping tone_mapping) noexcept {
  switch (tone_mapping) {
  case SceneToneMapping::Linear: {
    static const filament::LinearToneMapper mapper;
    return mapper;
  }
  case SceneToneMapping::Filmic: {
    static const filament::FilmicToneMapper mapper;
    return mapper;
  }
  case SceneToneMapping::Agx: {
    static const GodotAgxToneMapper mapper;
    return mapper;
  }
  case SceneToneMapping::AcesLegacy:
  case SceneToneMapping::Aces:
  case SceneToneMapping::DisplayRange:
    break;
  }
  static const filament::ACESToneMapper mapper;
  return mapper;
}

} // namespace quader

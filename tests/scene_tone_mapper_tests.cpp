////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/scene_tone_mapper.hpp"

#include <gtest/gtest.h>
#include <math/vec3.h>

#include <array>
#include <cmath>
#include <limits>

namespace quader {
namespace {

using filament::math::float3;

/// Compares reference RGB values without hiding channel or color-space errors.
void expect_color_near(float3 actual, float3 expected, float tolerance = 0.00001f) {
  EXPECT_NEAR(actual.r, expected.r, tolerance);
  EXPECT_NEAR(actual.g, expected.g, tolerance);
  EXPECT_NEAR(actual.b, expected.b, tolerance);
}

TEST(SceneToneMapperTests, AgxMatchesGodotSdrReferenceColors) {
  struct Reference {
    float3 linear_rec2020_input;
    float3 linear_rec2020_output;
  };
  // Godot 4.7 SDR defaults: contrast 1.25, white 16.29, output maximum 1.
  // Reference inputs are Rec.709 white, primaries, warm HDR and bright cyan,
  // transformed to Filament's Rec.2020 boundary. Outputs are fixed reference
  // evaluations of the upstream shader, without applying an sRGB transfer.
  constexpr std::array kReferences = {
      Reference{{1.0f, 1.0f, 1.0f},
                {0.596110849f, 0.596110849f, 0.596110849f}},
      Reference{{0.627403896f, 0.069097289f, 0.016391439f},
                {0.475960722f, 0.097352015f, 0.027073759f}},
      Reference{{0.329283038f, 0.919540395f, 0.088013308f},
                {0.322387091f, 0.554790704f, 0.117977329f}},
      Reference{{0.043313066f, 0.011362316f, 0.895595253f},
                {0.065154582f, 0.097942672f, 0.623521103f}},
      Reference{{8.538352399f, 3.593469815f, 0.908534817f},
                {0.949444101f, 0.857692011f, 0.710170975f}},
      Reference{{37.259610407f, 93.090271064f, 98.360856112f},
                {1.0f, 1.0f, 1.0f}},
  };
  const auto &mapper = scene_tone_mapper(SceneToneMapping::Agx);
  for (const auto &reference : kReferences) {
    expect_color_near(mapper(reference.linear_rec2020_input),
                      reference.linear_rec2020_output);
  }
}

TEST(SceneToneMapperTests, AgxPreservesBlackMiddleGrayAndReferenceWhite) {
  const auto &mapper = scene_tone_mapper(SceneToneMapping::Agx);
  expect_color_near(mapper({0.0f, 0.0f, 0.0f}), {0.0f, 0.0f, 0.0f});
  expect_color_near(mapper({0.18f, 0.18f, 0.18f}), {0.18f, 0.18f, 0.18f});
  expect_color_near(mapper({16.29f, 16.29f, 16.29f}), {1.0f, 1.0f, 1.0f});
  EXPECT_FALSE(mapper.isOneDimensional());
  EXPECT_FALSE(mapper.isLDR());
}

TEST(SceneToneMapperTests, AgxCompressesNeutralHdrMonotonically) {
  const auto &mapper = scene_tone_mapper(SceneToneMapping::Agx);
  constexpr std::array kSamples = {0.0f,    0.0001f, 0.01f, 0.17999f, 0.18f,
                                  0.18001f, 0.5f,    1.0f,  4.0f,     16.29f,
                                  1000.0f};
  float previous = 0.0f;
  for (float sample : kSamples) {
    const float3 actual = mapper({sample, sample, sample});
    EXPECT_GE(actual.r, previous);
    EXPECT_LE(actual.r, 1.0f);
    expect_color_near(actual, {actual.r, actual.r, actual.r});
    previous = actual.r;
  }
}

TEST(SceneToneMapperTests, AgxHandlesExtremeAndNegativeInputs) {
  const auto &mapper = scene_tone_mapper(SceneToneMapping::Agx);
  expect_color_near(mapper({-1.0f, -1.0f, -1.0f}), {0.0f, 0.0f, 0.0f});
  const float maximum = std::numeric_limits<float>::max();
  expect_color_near(mapper({maximum, maximum, maximum}), {1.0f, 1.0f, 1.0f});
  const float infinity = std::numeric_limits<float>::infinity();
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float3 actual = mapper({infinity, nan, -infinity});
  EXPECT_TRUE(std::isfinite(actual.r));
  EXPECT_TRUE(std::isfinite(actual.g));
  EXPECT_TRUE(std::isfinite(actual.b));
}

TEST(SceneToneMapperTests, RetiredAndUnknownModesUseModernAces) {
  const filament::ACESToneMapper expected_mapper;
  constexpr std::array kModes = {
      SceneToneMapping::Aces,
      SceneToneMapping::AcesLegacy,
      SceneToneMapping::DisplayRange,
      static_cast<SceneToneMapping>(255),
  };
  constexpr std::array kSamples = {float3{0.18f, 0.18f, 0.18f},
                                  float3{2.0f, 0.2f, 0.05f},
                                  float3{20.0f, 10.0f, 1.0f}};
  for (SceneToneMapping mode : kModes) {
    const auto &mapper = scene_tone_mapper(mode);
    EXPECT_EQ(&mapper, &scene_tone_mapper(SceneToneMapping::Aces));
    for (float3 sample : kSamples) {
      expect_color_near(mapper(sample), expected_mapper(sample));
    }
  }
}

} // namespace
} // namespace quader

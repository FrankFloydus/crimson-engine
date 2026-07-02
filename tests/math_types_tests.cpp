////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "math/glm_bridge.hpp"
#include "math/types.hpp"
#include "support/guardrail_file_scan.hpp"

#include <gtest/gtest.h>

#include <string>

using namespace quader;

namespace {

using quader::test::guardrail::read_source_file;

void expect_q_vec3_near(QVec3 actual, QVec3 expected) {
  EXPECT_NEAR(actual.x, expected.x, 0.001f);
  EXPECT_NEAR(actual.y, expected.y, 0.001f);
  EXPECT_NEAR(actual.z, expected.z, 0.001f);
}

} // namespace

TEST(MathTypesTests, ValueHeadersDoNotIncludeGlmBridge) {
  EXPECT_EQ(read_source_file("src/math/types.hpp").find("glm"),
            std::string::npos);
  EXPECT_EQ(read_source_file("src/math/types.hpp").find("glm"),
            std::string::npos);
  EXPECT_EQ(read_source_file("src/math/types/qvec2.hpp").find("glm"),
            std::string::npos);
  EXPECT_EQ(read_source_file("src/math/types/qvec3.hpp").find("glm"),
            std::string::npos);
  EXPECT_EQ(read_source_file("src/math/types/qquat.hpp").find("glm"),
            std::string::npos);
  EXPECT_EQ(read_source_file("src/math/types/qmat4.hpp").find("glm"),
            std::string::npos);
  EXPECT_EQ(read_source_file("src/math/types/qtransform.hpp").find("glm"),
            std::string::npos);
  EXPECT_NE(read_source_file("src/math/glm_bridge.hpp").find("glm"),
            std::string::npos);
}

TEST(MathTypesTests, QUmbrellaOnlyIncludesSplitVectorHeaders) {
  const std::string vec_umbrella = read_source_file("src/math/types.hpp");

  EXPECT_NE(vec_umbrella.find("#include \"math/types/qvec2.hpp\""),
            std::string::npos);
  EXPECT_NE(vec_umbrella.find("#include \"math/types/qvec3.hpp\""),
            std::string::npos);
  EXPECT_EQ(vec_umbrella.find("struct QVec2"), std::string::npos);
  EXPECT_EQ(vec_umbrella.find("struct QVec3"), std::string::npos);
}

TEST(MathTypesTests, PolygonDocumentUsesSharedVectorTypes) {
  const std::string polygon_document =
      read_source_file("src/mesh/polygon/document.hpp");

  EXPECT_NE(polygon_document.find("quader::QVec2"), std::string::npos);
  EXPECT_NE(polygon_document.find("quader::QVec3"), std::string::npos);
  EXPECT_EQ(polygon_document.find("struct QVec2"), std::string::npos);
  EXPECT_EQ(polygon_document.find("struct QVec3"), std::string::npos);
}

TEST(MathTypesTests, QuaternionFromEulerMatchesExpectedAxisRotations) {
  constexpr float kHalfPi = 1.57079632679489661923f;

  expect_q_vec3_near(transform_vector(compose_transform(QTransform{
                                          .rotation = quat_from_euler_radians(
                                              {kHalfPi, 0.0f, 0.0f})}),
                                      {0.0f, 1.0f, 0.0f}),
                     {0.0f, 0.0f, 1.0f});
  expect_q_vec3_near(transform_vector(compose_transform(QTransform{
                                          .rotation = quat_from_euler_radians(
                                              {0.0f, kHalfPi, 0.0f})}),
                                      {0.0f, 0.0f, 1.0f}),
                     {1.0f, 0.0f, 0.0f});
  expect_q_vec3_near(transform_vector(compose_transform(QTransform{
                                          .rotation = quat_from_euler_radians(
                                              {0.0f, 0.0f, kHalfPi})}),
                                      {1.0f, 0.0f, 0.0f}),
                     {0.0f, 1.0f, 0.0f});
}

TEST(MathTypesTests, GlmBridgePreservesColumnMajorMat4Storage) {
  const QMat4 transform = compose_transform(QTransform{
      .position = {3.0f, 4.0f, 5.0f},
      .rotation = quat_from_euler_radians({0.0f, 0.0f, 0.0f}),
      .scale = {2.0f, 3.0f, 4.0f},
  });

  expect_q_vec3_near(transform_point(transform, {1.0f, 1.0f, 1.0f}),
                     {5.0f, 7.0f, 9.0f});
  expect_q_vec3_near(transform_point(inverse(transform), {5.0f, 7.0f, 9.0f}),
                     {1.0f, 1.0f, 1.0f});
}

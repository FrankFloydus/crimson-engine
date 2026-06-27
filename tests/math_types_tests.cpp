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

using quader::test::guardrail::readSourceFile;

void expectQVec3Near(QVec3 actual, QVec3 expected) {
    EXPECT_NEAR(actual.x, expected.x, 0.001f);
    EXPECT_NEAR(actual.y, expected.y, 0.001f);
    EXPECT_NEAR(actual.z, expected.z, 0.001f);
}

} // namespace

TEST(MathTypesTests, ValueHeadersDoNotIncludeGlmBridge) {
    EXPECT_EQ(readSourceFile("src/math/types.hpp").find("glm"), std::string::npos);
    EXPECT_EQ(readSourceFile("src/math/types.hpp").find("glm"), std::string::npos);
    EXPECT_EQ(readSourceFile("src/math/types/q_vec2.hpp").find("glm"), std::string::npos);
    EXPECT_EQ(readSourceFile("src/math/types/q_vec3.hpp").find("glm"), std::string::npos);
    EXPECT_EQ(readSourceFile("src/math/types/q_quat.hpp").find("glm"), std::string::npos);
    EXPECT_EQ(readSourceFile("src/math/types/q_mat4.hpp").find("glm"), std::string::npos);
    EXPECT_EQ(readSourceFile("src/math/types/q_transform.hpp").find("glm"), std::string::npos);
    EXPECT_NE(readSourceFile("src/math/glm_bridge.hpp").find("glm"), std::string::npos);
}

TEST(MathTypesTests, QUmbrellaOnlyIncludesSplitVectorHeaders) {
    const std::string vecUmbrella = readSourceFile("src/math/types.hpp");

    EXPECT_NE(vecUmbrella.find("#include \"math/types/q_vec2.hpp\""), std::string::npos);
    EXPECT_NE(vecUmbrella.find("#include \"math/types/q_vec3.hpp\""), std::string::npos);
    EXPECT_EQ(vecUmbrella.find("struct QVec2"), std::string::npos);
    EXPECT_EQ(vecUmbrella.find("struct QVec3"), std::string::npos);
}

TEST(MathTypesTests, PolygonDocumentUsesSharedVectorTypes) {
    const std::string polygonDocument = readSourceFile("src/mesh/polygon/document.hpp");

    EXPECT_NE(polygonDocument.find("quader::QVec2"), std::string::npos);
    EXPECT_NE(polygonDocument.find("quader::QVec3"), std::string::npos);
    EXPECT_EQ(polygonDocument.find("struct QVec2"), std::string::npos);
    EXPECT_EQ(polygonDocument.find("struct QVec3"), std::string::npos);
}

TEST(MathTypesTests, QuaternionFromEulerMatchesExpectedAxisRotations) {
    constexpr float HalfPi = 1.57079632679489661923f;

    expectQVec3Near(transformVector(composeTransform(QTransform{.rotation = quatFromEulerRadians({HalfPi, 0.0f, 0.0f})}),
                           {0.0f, 1.0f, 0.0f}),
            {0.0f, 0.0f, 1.0f});
    expectQVec3Near(transformVector(composeTransform(QTransform{.rotation = quatFromEulerRadians({0.0f, HalfPi, 0.0f})}),
                           {0.0f, 0.0f, 1.0f}),
            {1.0f, 0.0f, 0.0f});
    expectQVec3Near(transformVector(composeTransform(QTransform{.rotation = quatFromEulerRadians({0.0f, 0.0f, HalfPi})}),
                           {1.0f, 0.0f, 0.0f}),
            {0.0f, 1.0f, 0.0f});
}

TEST(MathTypesTests, GlmBridgePreservesColumnMajorMat4Storage) {
    const QMat4 transform = composeTransform(QTransform{
            .position = {3.0f, 4.0f, 5.0f},
            .rotation = quatFromEulerRadians({0.0f, 0.0f, 0.0f}),
            .scale = {2.0f, 3.0f, 4.0f},
    });

    expectQVec3Near(transformPoint(transform, {1.0f, 1.0f, 1.0f}), {5.0f, 7.0f, 9.0f});
    expectQVec3Near(transformPoint(inverse(transform), {5.0f, 7.0f, 9.0f}), {1.0f, 1.0f, 1.0f});
}

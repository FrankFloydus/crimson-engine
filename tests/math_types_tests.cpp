////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "math/GlmBridge.h"
#include "math/Types.h"
#include "support/GuardrailFileScan.h"

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
    EXPECT_EQ(readSourceFile("src/math/Types.h").find("glm"), std::string::npos);
    EXPECT_EQ(readSourceFile("src/math/Types.h").find("glm"), std::string::npos);
    EXPECT_EQ(readSourceFile("src/math/types/QVec2.h").find("glm"), std::string::npos);
    EXPECT_EQ(readSourceFile("src/math/types/QVec3.h").find("glm"), std::string::npos);
    EXPECT_EQ(readSourceFile("src/math/types/QQuat.h").find("glm"), std::string::npos);
    EXPECT_EQ(readSourceFile("src/math/types/QMat4.h").find("glm"), std::string::npos);
    EXPECT_EQ(readSourceFile("src/math/types/QTransform.h").find("glm"), std::string::npos);
    EXPECT_NE(readSourceFile("src/math/GlmBridge.h").find("glm"), std::string::npos);
}

TEST(MathTypesTests, QUmbrellaOnlyIncludesSplitVectorHeaders) {
    const std::string vecUmbrella = readSourceFile("src/math/Types.h");

    EXPECT_NE(vecUmbrella.find("#include \"math/types/QVec2.h\""), std::string::npos);
    EXPECT_NE(vecUmbrella.find("#include \"math/types/QVec3.h\""), std::string::npos);
    EXPECT_EQ(vecUmbrella.find("struct QVec2"), std::string::npos);
    EXPECT_EQ(vecUmbrella.find("struct QVec3"), std::string::npos);
}

TEST(MathTypesTests, PolygonDocumentUsesSharedVectorTypes) {
    const std::string polygonDocument = readSourceFile("src/mesh/polygon/document.h");

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

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "support/BehaviorExpect.h"
#include "support/BehaviorScenario.h"

#include <gtest/gtest.h>

TEST(ProgrammaticBehaviorScenarioTests, ShiftDuplicatePreviewUsesAbsoluteDeltasAndKeepsDuplicatePickable) {
#if !QUADER_TEST_HAS_PROGRAMMATIC_API
    GTEST_SKIP() << "QuaderApi tests are disabled until the production API target is linked into quader_tests.";
#else
    auto s = quader::test::BehaviorScenario::newProject();

    s.given()
            .unitBox("source", "Shift Duplicate Source")
            .selected("source");

    s.when()
            .beginTransformPreview(true)
            .translatePreview({1.0f, 0.0f, 0.0f});

    const quader::test::BehaviorSnapshot firstMove = s.snapshot("first move");

    s.when()
            .translatePreview({3.0f, 0.0f, 0.0f})
            .commitTransformPreview();

    s.then().command().okChanged();
    s.then().scene()
            .entityCount(2)
            .selectedAliasIs("source.copy")
            .selectedBoundsCenterNear({3.0f, 0.0f, 0.0f});

    s.then().snapshot(firstMove)
            .selectedBoundsCenterNear({1.0f, 0.0f, 0.0f});

    s.when().pick({3.0f, 0.0f, 5.0f});
    s.then().selection()
            .objectCount(1)
            .activeAlias("source.copy");
#endif
}

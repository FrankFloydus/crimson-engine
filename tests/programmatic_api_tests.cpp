////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "support/BehaviorScenario.h"

#include <gtest/gtest.h>

TEST(ProgrammaticApiTests, QuaderApiCreatesBoxAndSynchronizesSnapshots) {
#if !QUADER_TEST_HAS_PROGRAMMATIC_API
    GTEST_SKIP() << "QuaderApi tests are disabled until the production API target is linked into quader_tests.";
#else
    quader::ProjectSession session;
    quader::QuaderApi api(session);

    quader::QuaderCreateBoxArgs args;
    args.bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}};
    args.name = "API Box";

    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> created = api.modeling().createBox(args);

    ASSERT_TRUE(created.ok) << created.message;
    EXPECT_TRUE(created.changed);
    EXPECT_NE(created.value.entity, quader::InvalidEntityId);
    EXPECT_NE(created.value.modelingObjectId, quader::InvalidModelingObjectId);
    EXPECT_NE(created.value.mesh, quader::InvalidAssetId);

    const quader::ModelingSelectionSummary selection = api.selection().summary();
    EXPECT_EQ(session.scene().entities().size(), 1u);
    EXPECT_EQ(api.modeling().objects().size(), 1u);
    EXPECT_EQ(session.assets().meshCount(), 1u);
    EXPECT_TRUE(selection.hasSelection);
    EXPECT_EQ(selection.selectedObjectCount, 1u);
    EXPECT_EQ(selection.activeObjectId, created.value.modelingObjectId);
#endif
}

TEST(ProgrammaticApiTests, InvalidToolIdReturnsFailedUnchangedStatus) {
#if !QUADER_TEST_HAS_PROGRAMMATIC_API
    GTEST_SKIP() << "QuaderApi tests are disabled until the production API target is linked into quader_tests.";
#else
    quader::ProjectSession session;
    quader::QuaderApi api(session);

    const quader::QuaderApiStatus result = api.modeling().executeTool("__missing_tool__");

    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.changed);
    EXPECT_FALSE(result.message.empty());
    EXPECT_EQ(result.mutationKind, quader::QuaderMutationKind::None);
#endif
}

TEST(ProgrammaticApiTests, DocumentNoOpAndFailureReportNoChangedMutationKind) {
#if !QUADER_TEST_HAS_PROGRAMMATIC_API
    GTEST_SKIP() << "QuaderApi tests are disabled until the production API target is linked into quader_tests.";
#else
    quader::ProjectSession session;
    quader::QuaderApi api(session);

    quader::QuaderCreateEntityArgs args;
    args.name = "Document Entity";

    const quader::QuaderApiResult<quader::QuaderEntityRef> created = api.scene().createEntity(args);
    ASSERT_TRUE(created.ok) << created.message;
    ASSERT_TRUE(created.changed);
    EXPECT_EQ(created.mutationKind, quader::QuaderMutationKind::Document);

    const quader::QuaderApiVoidResult unchanged = api.scene().rename(created.value.id, args.name);
    EXPECT_TRUE(unchanged.ok) << unchanged.message;
    EXPECT_FALSE(unchanged.changed);
    EXPECT_EQ(unchanged.mutationKind, quader::QuaderMutationKind::None);

    const quader::QuaderApiVoidResult failed = api.scene().rename(quader::InvalidEntityId, "Missing");
    EXPECT_FALSE(failed.ok);
    EXPECT_FALSE(failed.changed);
    EXPECT_EQ(failed.mutationKind, quader::QuaderMutationKind::None);
#endif
}

TEST(ProgrammaticApiTests, ModelingNoOpAndFailureReportNoChangedMutationKind) {
#if !QUADER_TEST_HAS_PROGRAMMATIC_API
    GTEST_SKIP() << "QuaderApi tests are disabled until the production API target is linked into quader_tests.";
#else
    quader::ProjectSession session;
    quader::QuaderApi api(session);

    const quader::QuaderApiVoidResult unchanged = api.modeling().updatePreview({});
    EXPECT_TRUE(unchanged.ok) << unchanged.message;
    EXPECT_FALSE(unchanged.changed);
    EXPECT_EQ(unchanged.mutationKind, quader::QuaderMutationKind::None);

    const quader::QuaderApiVoidResult failed = api.modeling().executeTool("__missing_tool__");
    EXPECT_FALSE(failed.ok);
    EXPECT_FALSE(failed.changed);
    EXPECT_EQ(failed.mutationKind, quader::QuaderMutationKind::None);
#endif
}

TEST(ProgrammaticApiTests, EmptyHistoryFailureReportsNoChangedMutationKind) {
#if !QUADER_TEST_HAS_PROGRAMMATIC_API
    GTEST_SKIP() << "QuaderApi tests are disabled until the production API target is linked into quader_tests.";
#else
    quader::ProjectSession session;
    quader::QuaderApi api(session);

    const quader::QuaderApiVoidResult result = api.history().undo();

    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.changed);
    EXPECT_EQ(result.mutationKind, quader::QuaderMutationKind::None);
#endif
}

TEST(ProgrammaticApiTests, ScriptRunsThroughPublicApiAndReturnsFinalSnapshot) {
#if !QUADER_TEST_HAS_PROGRAMMATIC_API
    GTEST_SKIP() << "QuaderApi tests are disabled until the production API target is linked into quader_tests.";
#else
    quader::ProjectSession session;
    quader::QuaderApi api(session);

    quader::ModelingBounds bounds;
    bounds.min = {-1.0f, -1.0f, -1.0f};
    bounds.max = {1.0f, 1.0f, 1.0f};

    const quader::QuaderScriptResult result = api.script("scripted box")
            .createBox("box", bounds, "Script Box")
            .select("box")
            .moveSelection({2.0f, 0.0f, 0.0f})
            .assignMaterialSlot(1)
            .selectVertex("box", 1)
            .run();

    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.steps.size(), 5u);
    ASSERT_TRUE(result.objects.contains("box"));
    EXPECT_EQ(result.finalSnapshot.entities.size(), 1u);
    EXPECT_TRUE(result.finalSnapshot.selection.hasSelection);
    EXPECT_EQ(result.finalSnapshot.selection.selectedVertexCount, 1u);
#endif
}

TEST(ProgrammaticApiTests, ScriptStopsAtMissingAliasAndReportsStep) {
#if !QUADER_TEST_HAS_PROGRAMMATIC_API
    GTEST_SKIP() << "QuaderApi tests are disabled until the production API target is linked into quader_tests.";
#else
    quader::ProjectSession session;
    quader::QuaderApi api(session);

    const quader::QuaderScriptResult result = api.script("bad script")
            .select("missing")
            .moveSelection({2.0f, 0.0f, 0.0f})
            .run();

    EXPECT_FALSE(result.ok);
    ASSERT_EQ(result.steps.size(), 1u);
    EXPECT_EQ(result.steps.front().label, "select(missing)");
    EXPECT_FALSE(result.steps.front().status.ok);
    EXPECT_FALSE(result.steps.front().status.changed);
    EXPECT_NE(result.steps.front().status.message.find("missing"), std::string::npos);
    EXPECT_TRUE(result.finalSnapshot.entities.empty());
#endif
}

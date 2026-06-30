////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <vector>

namespace {

/**
 * Owns a public standalone editor and exposes its whole-editor API for programmatic API tests.
 */
struct ProgrammaticApiHarness {
    quader::QuaderStandaloneEditor standalone;
    quader::QuaderApi& api;

    ProgrammaticApiHarness()
        : standalone(quader::createStandaloneEditor())
        , api(standalone.editor().api()) {}
};

std::size_t userEntityCount(const std::vector<quader::QuaderEntitySnapshot>& entities) {
    std::size_t count = 0;
    for (const quader::QuaderEntitySnapshot& entity : entities) {
        if (!entity.isSceneRoot) {
            ++count;
        }
    }
    return count;
}

std::size_t userEntityCount(const quader::QuaderApi& api) {
    return userEntityCount(api.query().entities());
}

bool hasSceneRoot(const quader::QuaderApi& api) {
    const std::vector<quader::QuaderEntitySnapshot> entities = api.query().entities();
    return std::any_of(entities.begin(), entities.end(), [](const quader::QuaderEntitySnapshot& entity) {
        return entity.isSceneRoot;
    });
}

} // namespace

TEST(ProgrammaticApiTests, QuaderApiCreatesBoxAndSynchronizesSnapshots) {
    ProgrammaticApiHarness harness;
    quader::QuaderApi& api = harness.api;

    quader::QuaderCreateBoxArgs args;
    args.bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}};
    args.name = "API Box";

    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> created = api.modeling().createBox(args);

    ASSERT_TRUE(created.ok) << created.message;
    EXPECT_TRUE(created.changed);
    EXPECT_NE(created.value.entity, quader::InvalidEntityId);
    EXPECT_NE(created.value.modelingObjectId, quader::InvalidQuaderModelingObjectId);
    EXPECT_NE(created.value.mesh, quader::InvalidAssetId);

    const quader::QuaderSelectionSummary selection = api.selection().summary();
    EXPECT_TRUE(hasSceneRoot(api));
    EXPECT_EQ(userEntityCount(api), 1u);
    EXPECT_EQ(api.modeling().objects().size(), 1u);
    EXPECT_EQ(api.query().assets().meshCount, 1u);
    EXPECT_TRUE(selection.hasSelection);
    EXPECT_EQ(selection.selectedObjectCount, 1u);
    EXPECT_EQ(selection.activeObjectId, created.value.modelingObjectId);
}

TEST(ProgrammaticApiTests, InvalidToolIdReturnsFailedUnchangedStatus) {
    ProgrammaticApiHarness harness;
    quader::QuaderApi& api = harness.api;

    const quader::QuaderApiStatus result = api.modeling().executeTool("__missing_tool__");

    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.changed);
    EXPECT_FALSE(result.message.empty());
    EXPECT_EQ(result.mutationKind, quader::QuaderMutationKind::None);
}

TEST(ProgrammaticApiTests, DocumentNoOpAndFailureReportNoChangedMutationKind) {
    ProgrammaticApiHarness harness;
    quader::QuaderApi& api = harness.api;

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
}

TEST(ProgrammaticApiTests, ModelingNoOpAndFailureReportNoChangedMutationKind) {
    ProgrammaticApiHarness harness;
    quader::QuaderApi& api = harness.api;

    const quader::QuaderApiVoidResult unchanged = api.modeling().updatePreview({});
    EXPECT_TRUE(unchanged.ok) << unchanged.message;
    EXPECT_FALSE(unchanged.changed);
    EXPECT_EQ(unchanged.mutationKind, quader::QuaderMutationKind::None);

    const quader::QuaderApiVoidResult failed = api.modeling().executeTool("__missing_tool__");
    EXPECT_FALSE(failed.ok);
    EXPECT_FALSE(failed.changed);
    EXPECT_EQ(failed.mutationKind, quader::QuaderMutationKind::None);
}

TEST(ProgrammaticApiTests, EmptyHistoryFailureReportsNoChangedMutationKind) {
    ProgrammaticApiHarness harness;
    quader::QuaderApi& api = harness.api;

    const quader::QuaderApiVoidResult result = api.history().undo();

    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.changed);
    EXPECT_EQ(result.mutationKind, quader::QuaderMutationKind::None);
}

TEST(ProgrammaticApiTests, ScriptRunsThroughPublicApiAndReturnsFinalSnapshot) {
    ProgrammaticApiHarness harness;
    quader::QuaderApi& api = harness.api;

    quader::QuaderBounds bounds;
    bounds.min = {-1.0f, -1.0f, -1.0f};
    bounds.max = {1.0f, 1.0f, 1.0f};

    const quader::QuaderScriptResult result = api.script("scripted box")
            .createBox("box", bounds, "Script Box")
            .select("box")
            .moveSelection({2.0f, 0.0f, 0.0f})
            .assignMaterialSlot(1)
            .selectVertex("box", 1)
            .run();

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(result.steps.size(), 5u);
    ASSERT_TRUE(result.objects.contains("box"));
    EXPECT_EQ(userEntityCount(result.finalSnapshot.entities), 1u);
    EXPECT_TRUE(result.finalSnapshot.selection.hasSelection);
    EXPECT_EQ(result.finalSnapshot.selection.selectedVertexCount, 1u);
    ASSERT_TRUE(api.history().canUndo());
    EXPECT_EQ(api.history().snapshot().undoLabel, "scripted box");

    const quader::QuaderApiVoidResult undone = api.history().undo();
    ASSERT_TRUE(undone.ok) << undone.message;
    EXPECT_EQ(userEntityCount(api), 0u);
    EXPECT_TRUE(api.history().canRedo());

    const quader::QuaderApiVoidResult redone = api.history().redo();
    ASSERT_TRUE(redone.ok) << redone.message;
    EXPECT_EQ(userEntityCount(api), 1u);
    EXPECT_EQ(api.selection().summary().selectedVertexCount, 1u);
}

TEST(ProgrammaticApiTests, ScriptRecordsDocumentOwnedSelectionTransformsInOneHistoryEntry) {
    ProgrammaticApiHarness harness;
    quader::QuaderApi& api = harness.api;

    quader::QuaderCreatePointLightArgs args;
    args.name = "Script Light";
    args.transform.position = {3.0f, 0.0f, 0.0f};
    args.selectCreated = true;
    const quader::QuaderApiResult<quader::QuaderEntityRef> light = api.scene().createPointLight(args);
    ASSERT_TRUE(light.ok) << light.message;

    const quader::QuaderScriptResult result = api.script("script moves light")
            .moveSelection({2.0f, 0.0f, 0.0f})
            .run();
    ASSERT_TRUE(result.ok()) << result.errorMessage();
    EXPECT_TRUE(result.changed());
    EXPECT_EQ(api.history().snapshot().undoLabel, "script moves light");

    std::optional<quader::QuaderEntitySnapshot> snapshot = api.query().entity(light.value.id);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_FLOAT_EQ(snapshot->transform.position.x, 5.0f);

    const quader::QuaderApiVoidResult undone = api.history().undo();
    ASSERT_TRUE(undone.ok) << undone.message;
    snapshot = api.query().entity(light.value.id);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_FLOAT_EQ(snapshot->transform.position.x, 3.0f);
    EXPECT_EQ(api.history().snapshot().redoLabel, "script moves light");

    const quader::QuaderApiVoidResult redone = api.history().redo();
    ASSERT_TRUE(redone.ok) << redone.message;
    snapshot = api.query().entity(light.value.id);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_FLOAT_EQ(snapshot->transform.position.x, 5.0f);
}

TEST(ProgrammaticApiTests, ScriptRejectsHistoryStepsInsideActiveTransaction) {
    ProgrammaticApiHarness harness;
    quader::QuaderApi& api = harness.api;

    const quader::QuaderScriptResult result = api.script("script history step")
            .undo()
            .run();

    EXPECT_FALSE(result.ok());
    ASSERT_EQ(result.steps.size(), 1u);
    EXPECT_FALSE(result.steps.front().status.ok);
    EXPECT_NE(result.steps.front().status.message.find("History operations are not supported"), std::string::npos);
    EXPECT_FALSE(api.history().canUndo());
}

TEST(ProgrammaticApiTests, ScriptStopsAtMissingAliasAndReportsStep) {
    ProgrammaticApiHarness harness;
    quader::QuaderApi& api = harness.api;

    const quader::QuaderScriptResult result = api.script("bad script")
            .select("missing")
            .moveSelection({2.0f, 0.0f, 0.0f})
            .run();

    EXPECT_FALSE(result.ok());
    ASSERT_EQ(result.steps.size(), 1u);
    EXPECT_EQ(result.steps.front().label, "select(missing)");
    EXPECT_FALSE(result.steps.front().status.ok);
    EXPECT_FALSE(result.steps.front().status.changed);
    EXPECT_NE(result.steps.front().status.message.find("missing"), std::string::npos);
    EXPECT_EQ(userEntityCount(result.finalSnapshot.entities), 0u);
}

TEST(ProgrammaticApiTests, ScriptAggregatesChangedStatusAcrossLaterNoOp) {
    ProgrammaticApiHarness harness;
    quader::QuaderApi& api = harness.api;

    const quader::QuaderScriptResult result = api.script("changed then noop")
            .createBox("box", {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}}, "Script Box")
            .setSelectionMode(quader::QuaderSelectionMode::Object)
            .run();

    ASSERT_TRUE(result.ok()) << result.errorMessage();
    ASSERT_EQ(result.steps.size(), 2u);
    EXPECT_TRUE(result.changed());
    EXPECT_NE(result.mutationKind, quader::QuaderMutationKind::None);
    EXPECT_FALSE(result.finalSnapshot.entities.empty());
}

TEST(ProgrammaticApiTests, ScriptRollsBackChangedStepsAcrossLaterFailure) {
    ProgrammaticApiHarness harness;
    quader::QuaderApi& api = harness.api;

    const quader::QuaderScriptResult result = api.script("changed then missing alias")
            .createBox("box", {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}}, "Script Box")
            .setSelectionMode(quader::QuaderSelectionMode::Face)
            .select("missing")
            .run();

    EXPECT_FALSE(result.ok());
    ASSERT_EQ(result.steps.size(), 3u);
    EXPECT_FALSE(result.changed());
    EXPECT_EQ(result.mutationKind, quader::QuaderMutationKind::None);
    EXPECT_NE(result.errorMessage().find("missing"), std::string::npos);
    EXPECT_EQ(userEntityCount(result.finalSnapshot.entities), 0u);
    EXPECT_TRUE(result.objects.empty());
    EXPECT_FALSE(api.history().canUndo());
}

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
        : standalone(quader::create_standalone_editor()),
          api(standalone.editor().api()) {}
};

std::size_t
user_entity_count(const std::vector<quader::QuaderEntitySnapshot> &entities) {
  return entities.size();
}

std::size_t user_entity_count(const quader::QuaderApi &api) {
  return user_entity_count(api.query().entities());
}


} // namespace

TEST(ProgrammaticApiTests, QuaderApiCreatesBoxAndSynchronizesSnapshots) {
    ProgrammaticApiHarness harness;
    quader::QuaderApi& api = harness.api;
    EXPECT_TRUE(api.query().entities().empty());

    quader::QuaderCreateBoxArgs args;
    args.bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}};
    args.name = "API Box";

    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> created =
        api.modeling().create_box(args);

    ASSERT_TRUE(created.ok) << created.message;
    EXPECT_TRUE(created.changed);
    EXPECT_NE(created.value.entity, quader::kInvalidEntityId);
    EXPECT_NE(created.value.modeling_object_id,
              quader::kInvalidQuaderModelingObjectId);
    EXPECT_NE(created.value.mesh, quader::kInvalidAssetId);

    const quader::QuaderSelectionSummary selection = api.selection().summary();
    const auto created_entity = api.query().entity(created.value.entity);
    ASSERT_TRUE(created_entity.has_value());
    EXPECT_EQ(created_entity->parent, quader::kInvalidEntityId);
    EXPECT_EQ(user_entity_count(api), 1u);
    EXPECT_EQ(api.modeling().objects().size(), 1u);
    EXPECT_EQ(api.query().assets().mesh_count, 1u);
    EXPECT_TRUE(selection.has_selection);
    EXPECT_EQ(selection.selected_object_count, 1u);
    EXPECT_EQ(selection.active_object_id, created.value.modeling_object_id);
}

TEST(ProgrammaticApiTests, InvalidToolIdReturnsFailedUnchangedStatus) {
    ProgrammaticApiHarness harness;
    quader::QuaderApi& api = harness.api;

    const quader::QuaderApiStatus result =
        api.modeling().execute_tool("__missing_tool__");

    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.changed);
    EXPECT_FALSE(result.message.empty());
    EXPECT_EQ(result.mutation_kind, quader::QuaderMutationKind::None);
}

TEST(ProgrammaticApiTests, DocumentNoOpAndFailureReportNoChangedMutationKind) {
    ProgrammaticApiHarness harness;
    quader::QuaderApi& api = harness.api;

    quader::QuaderCreateEntityArgs args;
    args.name = "Document Entity";

    const quader::QuaderApiResult<quader::QuaderEntityRef> created =
        api.scene().create_entity(args);
    ASSERT_TRUE(created.ok) << created.message;
    ASSERT_TRUE(created.changed);
    EXPECT_EQ(created.mutation_kind, quader::QuaderMutationKind::Document);

    const quader::QuaderApiVoidResult unchanged = api.scene().rename(created.value.id, args.name);
    EXPECT_TRUE(unchanged.ok) << unchanged.message;
    EXPECT_FALSE(unchanged.changed);
    EXPECT_EQ(unchanged.mutation_kind, quader::QuaderMutationKind::None);

    const quader::QuaderApiVoidResult failed =
        api.scene().rename(quader::kInvalidEntityId, "Missing");
    EXPECT_FALSE(failed.ok);
    EXPECT_FALSE(failed.changed);
    EXPECT_EQ(failed.mutation_kind, quader::QuaderMutationKind::None);
}

TEST(ProgrammaticApiTests,
     CreateEmptySelectsUniqueRootEntityAndUndoRedoRestoresSelectionAndIdentity) {
    ProgrammaticApiHarness harness;
    quader::QuaderApi& api = harness.api;

    quader::QuaderCreateEntityArgs baseline_args;
    baseline_args.name = "Prior Selection";
    baseline_args.select_created = true;
    const auto baseline = api.scene().create_entity(baseline_args);
    ASSERT_TRUE(baseline.ok) << baseline.message;
    const auto baseline_snapshot = api.query().entity(baseline.value.id);
    ASSERT_TRUE(baseline_snapshot.has_value());
    ASSERT_TRUE(baseline_snapshot->selected);

    const auto first = api.scene().create_empty();
    ASSERT_TRUE(first.ok) << first.message;
    ASSERT_TRUE(first.changed);
    const auto first_snapshot = api.query().entity(first.value.id);
    ASSERT_TRUE(first_snapshot.has_value());
    EXPECT_EQ(first_snapshot->name, "Empty");
    EXPECT_EQ(first_snapshot->kind, quader::QuaderEntityKind::Empty);
    EXPECT_TRUE(first_snapshot->selected);
    EXPECT_TRUE(first_snapshot->active);
    EXPECT_FALSE(first_snapshot->has_mesh);
    EXPECT_FALSE(first_snapshot->has_curve);
    EXPECT_FALSE(first_snapshot->has_light);
    EXPECT_FLOAT_EQ(first_snapshot->transform.position.x, 0.0f);
    EXPECT_FLOAT_EQ(first_snapshot->transform.position.y, 0.0f);
    EXPECT_FLOAT_EQ(first_snapshot->transform.position.z, 0.0f);
    EXPECT_FLOAT_EQ(first_snapshot->transform.rotation_euler.x, 0.0f);
    EXPECT_FLOAT_EQ(first_snapshot->transform.rotation_euler.y, 0.0f);
    EXPECT_FLOAT_EQ(first_snapshot->transform.rotation_euler.z, 0.0f);
    EXPECT_FLOAT_EQ(first_snapshot->transform.scale.x, 1.0f);
    EXPECT_FLOAT_EQ(first_snapshot->transform.scale.y, 1.0f);
    EXPECT_FLOAT_EQ(first_snapshot->transform.scale.z, 1.0f);
    EXPECT_EQ(first_snapshot->parent, quader::kInvalidEntityId);
    const std::string first_uuid = first_snapshot->uuid;
    const auto first_editor_object_id = first_snapshot->editor_object_id;

    ASSERT_TRUE(api.history().undo().ok());
    EXPECT_FALSE(api.query().entity(first.value.id).has_value());
    const auto restored_baseline = api.query().entity(baseline.value.id);
    ASSERT_TRUE(restored_baseline.has_value());
    EXPECT_TRUE(restored_baseline->selected);
    EXPECT_TRUE(restored_baseline->active);

    ASSERT_TRUE(api.history().redo().ok());
    const auto redone_first = api.query().entity(first.value.id);
    ASSERT_TRUE(redone_first.has_value());
    EXPECT_EQ(redone_first->uuid, first_uuid);
    EXPECT_EQ(redone_first->editor_object_id, first_editor_object_id);
    EXPECT_TRUE(redone_first->selected);
    EXPECT_TRUE(redone_first->active);

    const auto second = api.scene().create_empty();
    ASSERT_TRUE(second.ok) << second.message;
    const auto second_snapshot = api.query().entity(second.value.id);
    ASSERT_TRUE(second_snapshot.has_value());
    EXPECT_EQ(second_snapshot->name, "Empty_1");
    EXPECT_TRUE(second_snapshot->selected);
    EXPECT_TRUE(second_snapshot->active);
    const std::string second_uuid = second_snapshot->uuid;
    const auto second_editor_object_id = second_snapshot->editor_object_id;

    ASSERT_TRUE(api.history().undo().ok());
    EXPECT_FALSE(api.query().entity(second.value.id).has_value());
    const auto restored_first = api.query().entity(first.value.id);
    ASSERT_TRUE(restored_first.has_value());
    EXPECT_TRUE(restored_first->selected);
    EXPECT_TRUE(restored_first->active);

    ASSERT_TRUE(api.history().redo().ok());
    const auto redone_second = api.query().entity(second.value.id);
    ASSERT_TRUE(redone_second.has_value());
    EXPECT_EQ(redone_second->name, "Empty_1");
    EXPECT_EQ(redone_second->uuid, second_uuid);
    EXPECT_EQ(redone_second->editor_object_id, second_editor_object_id);
    EXPECT_TRUE(redone_second->selected);
    EXPECT_TRUE(redone_second->active);
}

TEST(ProgrammaticApiTests, ModelingNoOpAndFailureReportNoChangedMutationKind) {
    ProgrammaticApiHarness harness;
    quader::QuaderApi& api = harness.api;

    const quader::QuaderApiVoidResult unchanged =
        api.modeling().update_preview({});
    EXPECT_TRUE(unchanged.ok) << unchanged.message;
    EXPECT_FALSE(unchanged.changed);
    EXPECT_EQ(unchanged.mutation_kind, quader::QuaderMutationKind::None);

    const quader::QuaderApiVoidResult failed =
        api.modeling().execute_tool("__missing_tool__");
    EXPECT_FALSE(failed.ok);
    EXPECT_FALSE(failed.changed);
    EXPECT_EQ(failed.mutation_kind, quader::QuaderMutationKind::None);
}

TEST(ProgrammaticApiTests, EmptyHistoryFailureReportsNoChangedMutationKind) {
    ProgrammaticApiHarness harness;
    quader::QuaderApi& api = harness.api;

    const quader::QuaderApiVoidResult result = api.history().undo();

    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.changed);
    EXPECT_EQ(result.mutation_kind, quader::QuaderMutationKind::None);
}

TEST(ProgrammaticApiTests, ScriptRunsThroughPublicApiAndReturnsFinalSnapshot) {
    ProgrammaticApiHarness harness;
    quader::QuaderApi& api = harness.api;

    quader::QuaderBounds bounds;
    bounds.min = {-1.0f, -1.0f, -1.0f};
    bounds.max = {1.0f, 1.0f, 1.0f};

    const quader::QuaderScriptResult result =
        api.script("scripted box")
            .create_box("box", bounds, "Script Box")
            .select("box")
            .move_selection({2.0f, 0.0f, 0.0f})
            .assign_material_slot(1)
            .select_vertex("box", 1)
            .run();

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(result.steps.size(), 5u);
    ASSERT_TRUE(result.objects.contains("box"));
    EXPECT_EQ(user_entity_count(result.final_snapshot.entities), 1u);
    EXPECT_TRUE(result.final_snapshot.selection.has_selection);
    EXPECT_EQ(result.final_snapshot.selection.selected_vertex_count, 1u);
    ASSERT_TRUE(api.history().can_undo());
    EXPECT_EQ(api.history().snapshot().undo_label, "scripted box");

    const quader::QuaderApiVoidResult undone = api.history().undo();
    ASSERT_TRUE(undone.ok) << undone.message;
    EXPECT_EQ(user_entity_count(api), 0u);
    EXPECT_TRUE(api.history().can_redo());

    const quader::QuaderApiVoidResult redone = api.history().redo();
    ASSERT_TRUE(redone.ok) << redone.message;
    EXPECT_EQ(user_entity_count(api), 1u);
    EXPECT_EQ(api.selection().summary().selected_vertex_count, 1u);
}

TEST(ProgrammaticApiTests, ScriptRecordsDocumentOwnedSelectionTransformsInOneHistoryEntry) {
    ProgrammaticApiHarness harness;
    quader::QuaderApi& api = harness.api;

    quader::QuaderCreatePointLightArgs args;
    args.name = "Script Light";
    args.transform.position = {3.0f, 0.0f, 0.0f};
    args.select_created = true;
    const quader::QuaderApiResult<quader::QuaderEntityRef> light =
        api.scene().create_point_light(args);
    ASSERT_TRUE(light.ok) << light.message;

    const quader::QuaderScriptResult result =
        api.script("script moves light")
            .move_selection({2.0f, 0.0f, 0.0f})
            .run();
    ASSERT_TRUE(result.ok()) << result.error_message();
    EXPECT_TRUE(result.changed());
    EXPECT_EQ(api.history().snapshot().undo_label, "script moves light");

    std::optional<quader::QuaderEntitySnapshot> snapshot = api.query().entity(light.value.id);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_FLOAT_EQ(snapshot->transform.position.x, 5.0f);

    const quader::QuaderApiVoidResult undone = api.history().undo();
    ASSERT_TRUE(undone.ok) << undone.message;
    snapshot = api.query().entity(light.value.id);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_FLOAT_EQ(snapshot->transform.position.x, 3.0f);
    EXPECT_EQ(api.history().snapshot().redo_label, "script moves light");

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
    EXPECT_FALSE(api.history().can_undo());
}

TEST(ProgrammaticApiTests, ScriptStopsAtMissingAliasAndReportsStep) {
    ProgrammaticApiHarness harness;
    quader::QuaderApi& api = harness.api;

    const quader::QuaderScriptResult result =
        api.script("bad script")
            .select("missing")
            .move_selection({2.0f, 0.0f, 0.0f})
            .run();

    EXPECT_FALSE(result.ok());
    ASSERT_EQ(result.steps.size(), 1u);
    EXPECT_EQ(result.steps.front().label, "select(missing)");
    EXPECT_FALSE(result.steps.front().status.ok);
    EXPECT_FALSE(result.steps.front().status.changed);
    EXPECT_NE(result.steps.front().status.message.find("missing"), std::string::npos);
    EXPECT_EQ(user_entity_count(result.final_snapshot.entities), 0u);
}

TEST(ProgrammaticApiTests, ScriptAggregatesChangedStatusAcrossLaterNoOp) {
    ProgrammaticApiHarness harness;
    quader::QuaderApi& api = harness.api;

    const quader::QuaderScriptResult result =
        api.script("changed then noop")
            .create_box(
                "box",
                {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
                "Script Box")
            .set_selection_mode(quader::QuaderSelectionMode::Object)
            .run();

    ASSERT_TRUE(result.ok()) << result.error_message();
    ASSERT_EQ(result.steps.size(), 2u);
    EXPECT_TRUE(result.changed());
    EXPECT_NE(result.mutation_kind, quader::QuaderMutationKind::None);
    EXPECT_FALSE(result.final_snapshot.entities.empty());
}

TEST(ProgrammaticApiTests, ScriptRollsBackChangedStepsAcrossLaterFailure) {
    ProgrammaticApiHarness harness;
    quader::QuaderApi& api = harness.api;

    const quader::QuaderScriptResult result =
        api.script("changed then missing alias")
            .create_box(
                "box",
                {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
                "Script Box")
            .set_selection_mode(quader::QuaderSelectionMode::Face)
            .select("missing")
            .run();

    EXPECT_FALSE(result.ok());
    ASSERT_EQ(result.steps.size(), 3u);
    EXPECT_FALSE(result.changed());
    EXPECT_EQ(result.mutation_kind, quader::QuaderMutationKind::None);
    EXPECT_NE(result.error_message().find("missing"), std::string::npos);
    EXPECT_EQ(user_entity_count(result.final_snapshot.entities), 0u);
    EXPECT_TRUE(result.objects.empty());
    EXPECT_FALSE(api.history().can_undo());
}

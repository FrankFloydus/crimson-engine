////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/QuaderEditorApi.h"
#include "scene/document/ProjectSession.h"
#include "editor/commands/ModelingCommandToolCatalog.h"
#include "editor/selection/EditorSelectionService.h"
#include "editor/modeling/services/ModelingSessionService.h"
#include "editor/tools/ViewportToolCatalog.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

namespace {

std::optional<quader::QuaderEntitySnapshot> entityNamed(
        const quader::QuaderEditorApi& editor,
        std::string_view name) {
    for (const quader::QuaderEntitySnapshot& snapshot : editor.query().entities()) {
        if (snapshot.name == name) {
            return snapshot;
        }
    }
    return std::nullopt;
}

std::unordered_set<std::string> availableCommandIds(const quader::QuaderEditorApi& editor) {
    std::unordered_set<std::string> result;
    for (const quader::QuaderCommandDescriptor& command : editor.commands().available()) {
        result.insert(command.id);
    }
    return result;
}

std::string commandIdForContextAction(const quader::ModelingActionItem& action) {
    if (action.actionId == "delete_selection") {
        return std::string(quader::QuaderCommand::DeleteSelection.string());
    }
    if (action.actionId == "duplicate_meshes") {
        return std::string(quader::QuaderCommand::DuplicateSelection.string());
    }
    if (action.kind == quader::ModelingActionKind::Tool) {
        return "tools.activate." + action.toolId;
    }
    return "modeling.action." + action.actionId;
}

/// Owns an isolated editor session for command routing tests that need fresh
/// history, selection, and scene state per assertion path.
struct CommandTestHarness {
    quader::ProjectSession session;
    quader::QuaderEditorApi editor;

    CommandTestHarness()
        : session()
        , editor(session) {}
};

quader::QuaderModelingObjectRef createSelectedBox(quader::QuaderEditorApi& editor, std::string name = "Command Box") {
    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> created =
            editor.modeling().createBox(
                    quader::ModelingBounds{.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
                    std::move(name));
    EXPECT_TRUE(created.ok()) << created.errorMessage();
    EXPECT_TRUE(editor.selection().selectObject(created.value()).ok());
    return created.value();
}

void setupSelectionForMode(
        quader::QuaderEditorApi& editor,
        quader::ModelingSelectionMode mode) {
    const quader::QuaderModelingObjectRef object = createSelectedBox(editor);
    ASSERT_TRUE(editor.selection().setMode(mode).ok());
    if (mode == quader::ModelingSelectionMode::Object) {
        return;
    }
    ASSERT_TRUE(editor.selection().selectAll().ok());
    if (mode == quader::ModelingSelectionMode::Face) {
        ASSERT_TRUE(editor.selection().selectFace(object, 0).ok());
    } else if (mode == quader::ModelingSelectionMode::Edge) {
        ASSERT_TRUE(editor.selection().selectAll().ok());
    } else if (mode == quader::ModelingSelectionMode::Vertex) {
        ASSERT_TRUE(editor.selection().selectAll().ok());
    }
}

bool knownNonSuccessCommandMessage(std::string_view message) {
    return message != "Unknown command id." &&
            message != "Command tool is not registered.";
}

} // namespace

TEST(EditorApiTests, AggregatesDocumentApisAndCommandRouting) {
    quader::ProjectSession session;
    quader::QuaderEditorApi editor(session);

    EXPECT_FALSE(editor.project().dirty());
    EXPECT_EQ(editor.project().metadata().name, "Untitled Project");

    const std::vector<quader::QuaderCommandDescriptor> commands = editor.commands().available();
    EXPECT_NE(std::find_if(commands.begin(), commands.end(), [](const quader::QuaderCommandDescriptor& command) {
                  return command.id == "edit.undo";
              }),
            commands.end());
    EXPECT_EQ(editor.commands().shortcutFor(quader::QuaderCommand::EditUndo), "Ctrl+Z");
    EXPECT_EQ(editor.commands().shortcutFor("history.undo"), "Ctrl+Z");
    EXPECT_EQ(editor.commands().shortcutFor("modeling.action.delete_selection"), "");
    EXPECT_FALSE(editor.commands().execute("__missing_command__").ok());
    EXPECT_FALSE(editor.commands().execute("__missing_command__"));
    EXPECT_EQ(editor.commands().execute("__missing_command__").errorMessage(), "Unknown command id.");
    EXPECT_EQ(
            editor.commands().execute("modeling.action.__missing_action__").errorMessage(),
            "Unknown command id.");

    quader::QuaderCreateBoxArgs args;
    args.bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}};
    args.name = "Editor API Box";

    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> created = editor.modeling().createBox(args);
    ASSERT_TRUE(created.ok()) << created.errorMessage();
    EXPECT_TRUE(created.changed());
    EXPECT_NE(created.value().entity, quader::InvalidEntityId);
    EXPECT_EQ(editor.query().entities().size(), 1u);

    const quader::QuaderApiVoidResult undo = editor.commands().execute(quader::QuaderCommand::HistoryUndo);
    EXPECT_TRUE(undo.ok()) << undo.errorMessage();
    EXPECT_TRUE(undo.changed());
}

TEST(EditorApiTests, LegacyModelingActionCommandIdsRouteThroughCommandRegistry) {
    quader::ProjectSession session;
    quader::QuaderEditorApi editor(session);

    quader::QuaderCreateBoxArgs args;
    args.bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}};
    args.name = "Legacy Action Box";

    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> created = editor.modeling().createBox(args);
    ASSERT_TRUE(created.ok()) << created.errorMessage();
    ASSERT_TRUE(editor.selection().selectObject(created.value()).ok());

    const quader::QuaderApiVoidResult duplicate =
            editor.commands().execute("modeling.action.duplicate_meshes");
    ASSERT_TRUE(duplicate.ok()) << duplicate.errorMessage();
    EXPECT_TRUE(duplicate.changed());
    EXPECT_EQ(editor.query().entities().size(), 2u);

    const quader::QuaderApiVoidResult undo = editor.commands().execute("history.undo");
    ASSERT_TRUE(undo.ok()) << undo.errorMessage();
    EXPECT_EQ(editor.query().entities().size(), 1u);

    const quader::QuaderApiVoidResult deleteSelection =
            editor.commands().execute("modeling.action.delete_selection");
    ASSERT_TRUE(deleteSelection.ok()) << deleteSelection.errorMessage();
    EXPECT_TRUE(deleteSelection.changed());
    EXPECT_TRUE(editor.query().entities().empty());
}

TEST(EditorApiTests, CommandRegistryCoversEveryAppOperationAndViewportTool) {
    quader::ProjectSession session;
    quader::QuaderEditorApi editor(session);
    const std::unordered_set<std::string> ids = availableCommandIds(editor);

    for (std::string_view neutral : {
                 quader::QuaderCommand::EditUndo.string(),
                 quader::QuaderCommand::EditRedo.string(),
                 quader::QuaderCommand::EditRepeatLastAction.string(),
                 quader::QuaderCommand::EditClearSelection.string(),
                 quader::QuaderCommand::EditSelectAll.string(),
                 quader::QuaderCommand::EditInvertSelection.string(),
                 quader::QuaderCommand::DeleteSelection.string(),
                 quader::QuaderCommand::CopySelection.string(),
                 quader::QuaderCommand::PasteSelection.string(),
                 quader::QuaderCommand::DuplicateSelection.string(),
                 quader::QuaderCommand::GroupSelection.string(),
                 quader::QuaderCommand::UngroupSelection.string(),
                 quader::QuaderCommand::ApplyMaterial.string(),
                 quader::QuaderCommand::ViewportFrameSelection.string(),
                 quader::QuaderCommand::ViewportFocusOrigin.string(),
         }) {
        EXPECT_TRUE(ids.contains(std::string(neutral))) << neutral;
    }

    const std::unordered_set<std::string_view> neutralModelingTools{
            "delete_selection",
            "copy_selection",
            "paste_selection",
            "duplicate_meshes",
            "repeat_last_action",
            "assign_material",
            "apply_active_material",
    };
    for (const quader::CommandTool* tool : quader::modelingCommandTools()) {
        ASSERT_NE(tool, nullptr);
        const std::string legacy = "modeling.action." + std::string(tool->id());
        EXPECT_NE(editor.commands().execute(legacy).errorMessage(), "Unknown command id.") << legacy;
        if (!neutralModelingTools.contains(tool->id())) {
            const std::string command = "modeling." + std::string(tool->id());
            EXPECT_TRUE(ids.contains(command)) << command;
            EXPECT_NE(editor.commands().execute(command).errorMessage(), "Unknown command id.") << command;
        }
    }

    for (const quader::ViewportTool* tool : quader::viewportToolCatalog()) {
        ASSERT_NE(tool, nullptr);
        const std::string command = "tools.activate." + std::string(tool->id());
        EXPECT_TRUE(ids.contains(command)) << command;
        EXPECT_NE(editor.commands().execute(command).errorMessage(), "Unknown command id.") << command;
    }
}

TEST(EditorApiTests, EnabledContextMenuOperationsExecuteThroughCommandRegistry) {
    int routedActionCount = 0;
    int successfulActionCount = 0;
    for (const quader::ModelingSelectionMode mode : {
                 quader::ModelingSelectionMode::Object,
                 quader::ModelingSelectionMode::Vertex,
                 quader::ModelingSelectionMode::Edge,
                 quader::ModelingSelectionMode::Face,
         }) {
        CommandTestHarness listHarness;
        setupSelectionForMode(listHarness.editor, mode);
        const std::vector<quader::ModelingActionItem> actions =
                listHarness.editor.modeling().availableActions({
                        .surface = quader::ModelingActionSurface::ViewportContextMenu,
                        .includeDisabled = false,
                        .includeHidden = false,
                });

        for (const quader::ModelingActionItem& action : actions) {
            SCOPED_TRACE(action.actionId);
            CommandTestHarness executeHarness;
            setupSelectionForMode(executeHarness.editor, mode);
            const std::string commandId = commandIdForContextAction(action);
            const quader::QuaderApiVoidResult result = executeHarness.editor.commands().execute(commandId);
            ++routedActionCount;
            EXPECT_TRUE(result.ok() || knownNonSuccessCommandMessage(result.errorMessage()))
                    << commandId << ": " << result.errorMessage();
            if (!result.ok()) {
                continue;
            }
            ++successfulActionCount;
            EXPECT_TRUE(result.changed() || executeHarness.editor.query().history().canUndo ||
                    executeHarness.editor.query().toolStatus().cutActive ||
                    executeHarness.editor.query().toolStatus().mirrorActive)
                    << commandId;
        }
    }
    EXPECT_GT(routedActionCount, 0);
    EXPECT_GT(successfulActionCount, 0);
}

TEST(EditorApiTests, DeleteCommandAliasesRemoveSameObjectAndExposeSameUndoLabel) {
    for (const std::string_view commandId : {
                 quader::QuaderCommand::DeleteSelection.string(),
                 std::string_view{"modeling.delete_selection"},
                 std::string_view{"modeling.action.delete_selection"},
         }) {
        CommandTestHarness harness;
        const quader::QuaderModelingObjectRef first = createSelectedBox(harness.editor, "Delete Alias First");
        const quader::QuaderApiResult<quader::QuaderModelingObjectRef> second =
                harness.editor.modeling().createBox(
                        quader::ModelingBounds{.min = {4.0f, -1.0f, -1.0f}, .max = {6.0f, 1.0f, 1.0f}},
                        "Delete Alias Second");
        ASSERT_TRUE(second.ok()) << second.errorMessage();
        ASSERT_TRUE(harness.editor.selection().selectObject(first).ok());

        const quader::QuaderApiVoidResult deleted = harness.editor.commands().execute(commandId);
        ASSERT_TRUE(deleted.ok()) << commandId << ": " << deleted.errorMessage();
        EXPECT_TRUE(deleted.changed());
        EXPECT_FALSE(harness.editor.query().entity(first.entity).has_value());
        EXPECT_TRUE(harness.editor.query().entity(second.value().entity).has_value());
        EXPECT_EQ(harness.editor.query().history().undoLabel, "Delete Selection");

        const quader::QuaderApiVoidResult undo = harness.editor.history().undo();
        ASSERT_TRUE(undo.ok()) << undo.errorMessage();
        EXPECT_TRUE(harness.editor.query().entity(first.entity).has_value());
        EXPECT_TRUE(harness.editor.query().entity(second.value().entity).has_value());
    }
}

TEST(EditorApiTests, RightClickPickOnUnselectedObjectChangesSelectionBeforeContextDelete) {
    CommandTestHarness harness;
    const quader::QuaderModelingObjectRef left = createSelectedBox(harness.editor, "Context Left");
    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> right =
            harness.editor.modeling().createBox(
                    quader::ModelingBounds{.min = {4.0f, -1.0f, -1.0f}, .max = {6.0f, 1.0f, 1.0f}},
                    "Context Right");
    ASSERT_TRUE(right.ok()) << right.errorMessage();
    ASSERT_TRUE(harness.editor.selection().selectObject(left).ok());

    quader::ModelingPickSelectRequest rightClickPick;
    rightClickPick.ray = {
            .origin = {5.0f, 0.0f, 5.0f},
            .direction = {0.0f, 0.0f, -1.0f},
    };
    rightClickPick.preserveSelectionOnMiss = true;
    rightClickPick.preserveSelectionOnSelectedHit = true;
    const quader::QuaderApiResult<quader::ModelingSelectionSummary> picked =
            harness.editor.selection().pickSelect(rightClickPick);
    ASSERT_TRUE(picked.ok()) << picked.errorMessage();
    EXPECT_EQ(picked.value().activeObjectId, right.value().modelingObjectId);

    const quader::QuaderApiVoidResult deleted = harness.editor.commands().execute(quader::QuaderCommand::DeleteSelection);
    ASSERT_TRUE(deleted.ok()) << deleted.errorMessage();
    EXPECT_TRUE(harness.editor.query().entity(left.entity).has_value());
    EXPECT_FALSE(harness.editor.query().entity(right.value().entity).has_value());
}

TEST(EditorApiTests, RightClickPickOnUnselectedFaceChangesComponentSelectionBeforeContextDelete) {
    CommandTestHarness harness;
    const quader::QuaderModelingObjectRef left = createSelectedBox(harness.editor, "Context Face Left");
    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> right =
            harness.editor.modeling().createBox(
                    quader::ModelingBounds{.min = {4.0f, -1.0f, -1.0f}, .max = {6.0f, 1.0f, 1.0f}},
                    "Context Face Right");
    ASSERT_TRUE(right.ok()) << right.errorMessage();
    ASSERT_TRUE(harness.editor.selection().setMode(quader::ModelingSelectionMode::Face).ok());
    ASSERT_TRUE(harness.editor.selection().selectFace(left, 0).ok());
    const std::optional<quader::MeshAsset> rightBefore = harness.editor.query().compiledObjectMesh(right.value());
    ASSERT_TRUE(rightBefore.has_value());

    quader::ModelingPickSelectRequest rightClickPick;
    rightClickPick.ray = {
            .origin = {5.0f, 0.0f, 5.0f},
            .direction = {0.0f, 0.0f, -1.0f},
    };
    rightClickPick.preserveSelectionOnMiss = true;
    rightClickPick.preserveSelectionOnSelectedHit = true;
    const quader::QuaderApiResult<quader::ModelingSelectionSummary> picked =
            harness.editor.selection().pickSelect(rightClickPick);
    ASSERT_TRUE(picked.ok()) << picked.errorMessage();
    EXPECT_EQ(picked.value().mode, quader::ModelingSelectionMode::Face);
    EXPECT_EQ(picked.value().activeObjectId, right.value().modelingObjectId);
    EXPECT_EQ(picked.value().selectedFaceCount, 1u);

    const quader::QuaderApiVoidResult deleted = harness.editor.commands().execute(quader::QuaderCommand::DeleteSelection);
    ASSERT_TRUE(deleted.ok()) << deleted.errorMessage();
    EXPECT_TRUE(harness.editor.query().entity(left.entity).has_value());
    EXPECT_TRUE(harness.editor.query().entity(right.value().entity).has_value());
    const std::optional<quader::MeshAsset> rightAfter = harness.editor.query().compiledObjectMesh(right.value());
    ASSERT_TRUE(rightAfter.has_value());
    EXPECT_LT(rightAfter->indices.size(), rightBefore->indices.size());
}

TEST(EditorApiTests, GroupModeRightClickPickSelectsGroupBeforeContextDelete) {
    CommandTestHarness harness;
    const quader::QuaderModelingObjectRef left = createSelectedBox(harness.editor, "Context Group Left");
    const quader::QuaderApiResult<quader::QuaderEntityRef> leftGroup =
            harness.editor.scene().groupSelection("Context Group A");
    ASSERT_TRUE(leftGroup.ok()) << leftGroup.errorMessage();

    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> right =
            harness.editor.modeling().createBox(
                    quader::ModelingBounds{.min = {4.0f, -1.0f, -1.0f}, .max = {6.0f, 1.0f, 1.0f}},
                    "Context Group Right");
    ASSERT_TRUE(right.ok()) << right.errorMessage();
    ASSERT_TRUE(harness.editor.selection().selectObject(right.value()).ok());
    const quader::QuaderApiResult<quader::QuaderEntityRef> rightGroup =
            harness.editor.scene().groupSelection("Context Group B");
    ASSERT_TRUE(rightGroup.ok()) << rightGroup.errorMessage();

    ASSERT_TRUE(harness.editor.selection().setMode(quader::ModelingSelectionMode::Group).ok());
    ASSERT_TRUE(harness.editor.selection().selectObject(leftGroup.value().id).ok());

    quader::ModelingPickSelectRequest rightClickPick;
    rightClickPick.ray = {
            .origin = {5.0f, 0.0f, 5.0f},
            .direction = {0.0f, 0.0f, -1.0f},
    };
    rightClickPick.preserveSelectionOnMiss = true;
    rightClickPick.preserveSelectionOnSelectedHit = true;
    const quader::QuaderApiResult<quader::ModelingSelectionSummary> picked =
            harness.editor.selection().pickSelect(rightClickPick);
    ASSERT_TRUE(picked.ok()) << picked.errorMessage();
    const quader::EditorSelectionState pickedState = harness.session.selection().state();
    EXPECT_EQ(pickedState.target.kind, quader::EditorSelectionTargetKind::Group);
    EXPECT_EQ(pickedState.target.entity, rightGroup.value().id);

    const quader::QuaderApiVoidResult deleted = harness.editor.commands().execute(quader::QuaderCommand::DeleteSelection);
    ASSERT_TRUE(deleted.ok()) << deleted.errorMessage();
    EXPECT_TRUE(harness.editor.query().entity(leftGroup.value().id).has_value());
    EXPECT_TRUE(harness.editor.query().entity(left.entity).has_value());
    EXPECT_FALSE(harness.editor.query().entity(rightGroup.value().id).has_value());
    EXPECT_FALSE(harness.editor.query().entity(right.value().entity).has_value());
}

TEST(EditorApiTests, ViewportHandlesUseWorkspaceWithoutRuntimeHooks) {
    quader::ProjectSession session;
    quader::QuaderEditorApi editor(session);

    EXPECT_TRUE(editor.viewports().setLayout(quader::ViewportLayoutId::FourQuad).ok());
    EXPECT_EQ(editor.viewports().layout(), quader::ViewportLayoutId::FourQuad);
    EXPECT_EQ(editor.viewports().workspace().activePanePlacements(false).size(), 4u);

    EXPECT_TRUE(editor.viewports().setGridPreset(4).ok());
    EXPECT_EQ(editor.viewports().gridPreset(), 4);

    auto viewport = editor.viewports().main();
    EXPECT_TRUE(viewport.setShadingMode(quader::QuaderShadingMode::Wireframe).ok());
    EXPECT_EQ(editor.viewports().shadingMode(quader::ViewportPaneId::Perspective), quader::QuaderShadingMode::Wireframe);

    EXPECT_TRUE(viewport.setView(quader::ViewportView::Top).ok());
    EXPECT_EQ(editor.viewports().view(quader::ViewportPaneId::Perspective), quader::ViewportView::Top);

    EXPECT_TRUE(viewport.camera().focusOrigin().ok());
    EXPECT_FALSE(viewport.camera().frameSelection().ok());
}

TEST(EditorApiTests, ObjectHandlesSelectionConveniencesAndSceneBatchesUsePublicApi) {
    quader::ProjectSession session;
    quader::QuaderEditorApi editor(session);

    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> created =
            editor.modeling().createBox(
                    quader::ModelingBounds{.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
                    "Handle Box");
    ASSERT_TRUE(created.ok()) << created.errorMessage();

    quader::QuaderObjectHandle box = editor.object(created.value());
    EXPECT_TRUE(box.rename("Public Handle Box").ok());
    EXPECT_TRUE(box.setVisible(false).ok());
    EXPECT_TRUE(box.setLocked(true).ok());
    EXPECT_TRUE(box.setLocked(false).ok());
    EXPECT_TRUE(editor.selection().selectObject(created.value().entity).ok());
    EXPECT_TRUE(editor.selection().selectObjects({created.value()}, quader::QuaderSelectionEdit::Replace).ok());
    EXPECT_TRUE(editor.selection().selectVertices(created.value(), {0, 1}).ok());

    const quader::QuaderApiResult<quader::QuaderEntityRef> duplicate = box.duplicate();
    ASSERT_TRUE(duplicate.ok()) << duplicate.errorMessage();
    EXPECT_NE(duplicate.value().id, quader::InvalidEntityId);

    quader::QuaderCreateEntityArgs firstArgs;
    firstArgs.name = "First";
    const quader::QuaderApiResult<quader::QuaderEntityRef> first = editor.scene().createEntity(firstArgs);
    ASSERT_TRUE(first.ok()) << first.errorMessage();
    EXPECT_NE(first.value().editorObjectId, quader::InvalidEditorObjectId);
    const std::optional<quader::QuaderEntitySnapshot> firstSnapshot = editor.query().entity(first.value().id);
    ASSERT_TRUE(firstSnapshot.has_value());
    EXPECT_EQ(firstSnapshot->editorObjectId, first.value().editorObjectId);

    quader::QuaderCreateEntityArgs secondArgs;
    secondArgs.name = "Second";
    const quader::QuaderApiResult<quader::QuaderEntityRef> second = editor.scene().createEntity(secondArgs);
    ASSERT_TRUE(second.ok()) << second.errorMessage();

    quader::TransformComponent moved;
    moved.position = {3.0f, 0.0f, 0.0f};
    const quader::QuaderApiVoidResult batch = editor.scene().batch("Arrange")
            .rename(first.value().id, "Renamed First")
            .setTransform(first.value().id, moved)
            .setVisible(second.value().id, false)
            .setLocked(second.value().id, true)
            .commit();
    ASSERT_TRUE(batch.ok()) << batch.errorMessage();
    EXPECT_TRUE(batch.changed());

    ASSERT_NE(session.scene().name(first.value().id), nullptr);
    EXPECT_EQ(session.scene().name(first.value().id)->name, "Renamed First");
    ASSERT_NE(session.scene().transform(first.value().id), nullptr);
    EXPECT_EQ(session.scene().transform(first.value().id)->position.x, 3.0f);
    ASSERT_NE(session.scene().visibility(second.value().id), nullptr);
    EXPECT_FALSE(session.scene().visibility(second.value().id)->visible);
    ASSERT_NE(session.scene().lock(second.value().id), nullptr);
    EXPECT_TRUE(session.scene().lock(second.value().id)->locked);

    EXPECT_TRUE(editor.history().undo().ok());
    EXPECT_EQ(session.scene().name(first.value().id)->name, "First");
    EXPECT_TRUE(session.scene().visibility(second.value().id)->visible);
    EXPECT_FALSE(session.scene().lock(second.value().id)->locked);
}

TEST(EditorApiTests, SelectionApiDelegatesTypedTargetsToEditorSelectionService) {
    quader::ProjectSession session;
    quader::QuaderEditorApi editor(session);

    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> first =
            editor.modeling().createBox(
                    quader::ModelingBounds{.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
                    "Selection First");
    ASSERT_TRUE(first.ok()) << first.errorMessage();
    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> second =
            editor.modeling().createBox(
                    quader::ModelingBounds{.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
                    "Selection Second");
    ASSERT_TRUE(second.ok()) << second.errorMessage();

    ASSERT_TRUE(editor.selection().selectObject(first.value()).ok());
    quader::EditorSelectionState state = session.selection().state();
    EXPECT_EQ(state.target.kind, quader::EditorSelectionTargetKind::Object);
    EXPECT_EQ(state.target.entity, first.value().entity);
    EXPECT_EQ(state.target.modelingObjectId, first.value().modelingObjectId);

    ASSERT_TRUE(editor.selection().selectFace(first.value(), 1).ok());
    state = session.selection().state();
    EXPECT_EQ(state.target.kind, quader::EditorSelectionTargetKind::Face);
    EXPECT_EQ(state.target.entity, first.value().entity);
    EXPECT_EQ(state.target.face, 1);

    ASSERT_TRUE(editor.selection().setMode(quader::ModelingSelectionMode::Object).ok());
    ASSERT_TRUE(editor.selection().selectObjects({first.value().entity, second.value().entity}, second.value().entity).ok());
    const quader::QuaderApiResult<quader::QuaderEntityRef> group = editor.scene().groupSelection("Selection Group");
    ASSERT_TRUE(group.ok()) << group.errorMessage();
    ASSERT_TRUE(editor.selection().setMode(quader::ModelingSelectionMode::Group).ok());
    state = session.selection().state();
    EXPECT_EQ(state.target.kind, quader::EditorSelectionTargetKind::Group);
    EXPECT_EQ(state.target.entity, group.value().id);
    EXPECT_EQ(state.mode, quader::ModelingSelectionMode::Group);
}

TEST(EditorApiTests, MixedModelingAndSceneHistoryReplaysChronologicallyThroughPublicHistory) {
    quader::ProjectSession session;
    quader::QuaderEditorApi editor(session);

    quader::QuaderCreateBoxArgs args;
    args.bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}};
    args.name = "Chronology Box";
    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> created = editor.modeling().createBox(args);
    ASSERT_TRUE(created.ok()) << created.errorMessage();

    const quader::QuaderApiVoidResult renamed =
            editor.scene().rename(created.value().entity, "Chronology Box Renamed");
    ASSERT_TRUE(renamed.ok()) << renamed.errorMessage();
    EXPECT_TRUE(entityNamed(editor, "Chronology Box Renamed").has_value());

    ASSERT_TRUE(editor.history().undo().ok());
    EXPECT_TRUE(entityNamed(editor, "Chronology Box").has_value());
    EXPECT_FALSE(entityNamed(editor, "Chronology Box Renamed").has_value());

    ASSERT_TRUE(editor.history().undo().ok());
    EXPECT_FALSE(entityNamed(editor, "Chronology Box").has_value());
    EXPECT_FALSE(entityNamed(editor, "Chronology Box Renamed").has_value());

    ASSERT_TRUE(editor.history().redo().ok());
    EXPECT_TRUE(entityNamed(editor, "Chronology Box").has_value());
    EXPECT_FALSE(entityNamed(editor, "Chronology Box Renamed").has_value());

    ASSERT_TRUE(editor.history().redo().ok());
    EXPECT_TRUE(entityNamed(editor, "Chronology Box Renamed").has_value());
    EXPECT_FALSE(entityNamed(editor, "Chronology Box").has_value());
}

TEST(EditorApiTests, AppCommandAliasesAndDirectOperationsShareChronologicalUndoStack) {
    quader::ProjectSession session;
    quader::QuaderEditorApi editor(session);

    const quader::QuaderModelingObjectRef original = createSelectedBox(editor, "Chronology Command Box");

    const quader::QuaderApiVoidResult duplicated = editor.commands().execute("modeling.action.duplicate_meshes");
    ASSERT_TRUE(duplicated.ok()) << duplicated.errorMessage();
    ASSERT_EQ(editor.query().entities().size(), 2u);
    EXPECT_EQ(editor.query().history().undoLabel, "Duplicate Meshes");

    quader::TransformComponent moved;
    moved.position = {3.0f, 0.0f, 0.0f};
    const quader::QuaderApiVoidResult transformed = editor.scene().setTransform(original.entity, moved);
    ASSERT_TRUE(transformed.ok()) << transformed.errorMessage();
    ASSERT_NE(session.scene().transform(original.entity), nullptr);
    EXPECT_NEAR(session.scene().transform(original.entity)->position.x, 3.0f, 0.001f);
    EXPECT_EQ(editor.query().history().undoLabel, "Set Transform");

    const std::vector<quader::EntityId> selectedBeforeDelete = session.scene().selectedEntities();
    ASSERT_FALSE(selectedBeforeDelete.empty());
    const quader::QuaderApiVoidResult deleted = editor.commands().execute(quader::QuaderCommand::DeleteSelection);
    ASSERT_TRUE(deleted.ok()) << deleted.errorMessage();
    EXPECT_EQ(editor.query().history().undoLabel, "Delete Selection");

    ASSERT_TRUE(editor.history().undo().ok());
    EXPECT_EQ(editor.query().entities().size(), 2u);
    EXPECT_EQ(editor.query().history().redoLabel, "Delete Selection");

    ASSERT_TRUE(editor.history().undo().ok());
    ASSERT_NE(session.scene().transform(original.entity), nullptr);
    EXPECT_NEAR(session.scene().transform(original.entity)->position.x, 0.0f, 0.001f);
    EXPECT_EQ(editor.query().history().redoLabel, "Set Transform");

    ASSERT_TRUE(editor.history().undo().ok());
    EXPECT_EQ(editor.query().entities().size(), 1u);
    EXPECT_EQ(editor.query().history().redoLabel, "Duplicate Meshes");

    ASSERT_TRUE(editor.history().redo().ok());
    EXPECT_EQ(editor.query().entities().size(), 2u);
    ASSERT_TRUE(editor.history().redo().ok());
    ASSERT_NE(session.scene().transform(original.entity), nullptr);
    EXPECT_NEAR(session.scene().transform(original.entity)->position.x, 3.0f, 0.001f);
    ASSERT_TRUE(editor.history().redo().ok());
    EXPECT_LT(editor.query().entities().size(), 2u);
}

TEST(EditorApiTests, PublicNoopOperationsPreserveRedoUntilChangedAppCommand) {
    quader::ProjectSession session;
    quader::QuaderEditorApi editor(session);

    const quader::QuaderModelingObjectRef box = createSelectedBox(editor, "Redo Noop Box");
    const quader::QuaderApiVoidResult renamed = editor.scene().rename(box.entity, "Redo Noop Renamed");
    ASSERT_TRUE(renamed.ok()) << renamed.errorMessage();
    ASSERT_TRUE(editor.history().undo().ok());
    EXPECT_TRUE(editor.history().canRedo());

    const quader::QuaderApiVoidResult noOpRename = editor.scene().rename(box.entity, "Redo Noop Box");
    ASSERT_TRUE(noOpRename.ok()) << noOpRename.errorMessage();
    EXPECT_FALSE(noOpRename.changed());
    EXPECT_TRUE(editor.history().canRedo());
    EXPECT_EQ(editor.query().history().redoLabel, "Rename Entity");

    const quader::QuaderApiVoidResult duplicate = editor.commands().execute(quader::QuaderCommand::DuplicateSelection);
    ASSERT_TRUE(duplicate.ok()) << duplicate.errorMessage();
    EXPECT_TRUE(duplicate.changed());
    EXPECT_FALSE(editor.history().canRedo());
}

TEST(EditorApiTests, PublicHistoryIgnoresUntrackedModelingSessionHistory) {
    quader::ProjectSession session;
    quader::QuaderEditorApi editor(session);

    const auto hidden = session.modeling().createBoxFromBounds({
            .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
            .name = "Hidden Modeling Box",
    });
    ASSERT_TRUE(hidden.ok) << hidden.message;
    ASSERT_TRUE(entityNamed(editor, "Hidden Modeling Box").has_value());

    EXPECT_FALSE(editor.history().canUndo());
    EXPECT_FALSE(editor.history().canRedo());

    const quader::QuaderApiVoidResult directUndo = editor.history().undo();
    EXPECT_FALSE(directUndo.ok());
    EXPECT_FALSE(directUndo.changed());
    EXPECT_EQ(directUndo.errorMessage(), "No command to undo");
    EXPECT_TRUE(entityNamed(editor, "Hidden Modeling Box").has_value());

    const quader::QuaderApiVoidResult commandUndo = editor.commands().execute(quader::QuaderCommand::HistoryUndo);
    EXPECT_FALSE(commandUndo.ok());
    EXPECT_FALSE(commandUndo.changed());
    EXPECT_EQ(commandUndo.errorMessage(), "Command is disabled.");
    EXPECT_TRUE(entityNamed(editor, "Hidden Modeling Box").has_value());
}

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/QuaderEditorApi.h"
#include "document/ProjectSession.h"
#include "editor/selection/EditorSelectionService.h"
#include "modeling/services/ModelingSessionService.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string_view>

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
    ASSERT_TRUE(editor.scene().select({first.value().entity, second.value().entity}, second.value().entity).ok());
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

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/QuaderEditorApi.h"
#include "editor/modeling/services/ModelingSessionService.h"
#include "scene/document/ProjectSession.h"
#include "viewport/HostViewportContractAdapter.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

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

std::optional<quader::QuaderEntitySnapshot> entityById(
        const quader::QuaderEditorApi& editor,
        quader::EntityId id) {
    return editor.query().entity(id);
}

std::vector<quader::EntityId> selectedEntities(const quader::QuaderEditorApi& editor) {
    std::vector<quader::EntityId> selected;
    for (const quader::QuaderEntitySnapshot& snapshot : editor.query().entities()) {
        if (snapshot.selected) {
            selected.push_back(snapshot.id);
        }
    }
    std::sort(selected.begin(), selected.end());
    return selected;
}

std::unordered_set<std::string> availableCommandIds(const quader::QuaderEditorApi& editor) {
    std::unordered_set<std::string> result;
    for (const quader::QuaderCommandDescriptor& command : editor.commands().available()) {
        result.insert(command.id);
    }
    return result;
}

std::string commandIdForContextAction(
        const quader::QuaderEditorApi& editor,
        const quader::ModelingActionItem& action) {
    if (action.kind == quader::ModelingActionKind::Tool) {
        return editor.commands().commandIdForToolActivation(action.toolId);
    }
    return editor.commands().commandIdForModelingAction(action.actionId);
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

std::filesystem::path keybindingTestPath(std::string_view name) {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
            ("quader-keybindings-" + std::string(name) + ".json");
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    return path;
}

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    std::string text;
    std::string line;
    while (std::getline(file, line)) {
        text += line;
        text += '\n';
    }
    return text;
}

bool sameViewportShortcut(const quader::ViewportShortcut& left, const quader::ViewportShortcut& right) {
    return left.kind == right.kind &&
            left.context == right.context &&
            left.tool == right.tool &&
            left.selectionMode == right.selectionMode &&
            left.mirrorAxis == right.mirrorAxis &&
            left.gridPreset == right.gridPreset;
}

bool hasKeybinding(
        const std::vector<quader::QuaderKeybindingRule>& rules,
        std::string_view targetId,
        std::string_view shortcut) {
    return std::any_of(rules.begin(), rules.end(), [&](const quader::QuaderKeybindingRule& rule) {
        return rule.targetId == targetId && rule.shortcut == shortcut;
    });
}

} // namespace

TEST(EditorApiTests, AggregatesDocumentApisAndCommandRouting) {
    quader::ProjectSession session;
    quader::QuaderEditorApi editor(session);
    ASSERT_TRUE(editor.commands().setKeybindingStoragePath(keybindingTestPath("aggregate")).ok());

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
    EXPECT_EQ(
            editor.commands().commandIdForModelingAction("delete_selection"),
            std::string(quader::QuaderCommand::DeleteSelection.string()));
    EXPECT_EQ(
            editor.commands().commandIdForModelingAction("invert_mesh"),
            std::string(quader::QuaderCommand::ModelingInvertMesh.string()));
    EXPECT_EQ(editor.commands().commandIdForModelingAction("bridge_edges"), "modeling.bridge_edges");
    EXPECT_EQ(editor.commands().commandIdForToolActivation("knife"), "tools.activate.knife");
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

TEST(EditorApiTests, PreferencesPersistAndResolveCustomCommandShortcuts) {
    const std::filesystem::path path = keybindingTestPath("command");
    quader::ProjectSession session;
    quader::QuaderEditorApi editor(session);

    ASSERT_TRUE(editor.commands().setKeybindingStoragePath(path).ok());
    const quader::QuaderApiVoidResult updated =
            editor.preferences().setShortcut(quader::QuaderCommand::EditUndo.string(), "Ctrl+Alt+Z");
    ASSERT_TRUE(updated.ok()) << updated.errorMessage();
    EXPECT_TRUE(updated.changed());
    EXPECT_EQ(editor.commands().shortcutFor(quader::QuaderCommand::EditUndo), "Ctrl+Alt+Z");
    EXPECT_EQ(editor.commands().shortcutFor("history.undo"), "Ctrl+Alt+Z");
    EXPECT_NE(readTextFile(path).find("\"key\": \"Ctrl+Alt+Z\""), std::string::npos);

    quader::ProjectSession reloadedSession;
    quader::QuaderEditorApi reloaded(reloadedSession);
    ASSERT_TRUE(reloaded.commands().setKeybindingStoragePath(path).ok());
    EXPECT_EQ(reloaded.commands().shortcutFor(quader::QuaderCommand::EditUndo), "Ctrl+Alt+Z");

    ASSERT_TRUE(reloaded.preferences().resetShortcut(quader::QuaderCommand::EditUndo.string()).ok());
    EXPECT_EQ(reloaded.commands().shortcutFor(quader::QuaderCommand::EditUndo), "Ctrl+Z");
}

TEST(EditorApiTests, KeybindingConflictsAndRemovalSupportRebindingUi) {
    const std::filesystem::path path = keybindingTestPath("conflicts");
    quader::ProjectSession session;
    quader::QuaderEditorApi editor(session);

    ASSERT_TRUE(editor.commands().setKeybindingStoragePath(path).ok());
    ASSERT_TRUE(editor.preferences().setShortcut(quader::QuaderCommand::EditUndo.string(), "Ctrl+Alt+Z").ok());

    const std::vector<quader::QuaderKeybindingConflict> conflicts =
            editor.commands().conflictsForShortcut("Ctrl+Alt+Z");
    ASSERT_EQ(conflicts.size(), 1u);
    EXPECT_EQ(conflicts.front().targetId, quader::QuaderCommand::EditUndo.string());
    EXPECT_EQ(conflicts.front().source, "user");

    ASSERT_TRUE(editor.preferences().removeShortcut(quader::QuaderCommand::EditUndo.string()).ok());
    EXPECT_EQ(editor.commands().shortcutFor(quader::QuaderCommand::EditUndo), "");
    EXPECT_NE(readTextFile(path).find("\"command\": \"-edit.undo\""), std::string::npos);
}

TEST(EditorApiTests, ViewportKeybindingsResolveContextAndUserOverrides) {
    const std::filesystem::path path = keybindingTestPath("viewport");
    quader::ProjectSession session;
    quader::QuaderEditorApi editor(session);
    ASSERT_TRUE(editor.commands().setKeybindingStoragePath(path).ok());

    quader::ViewportToolState state;
    quader::ViewportShortcut shortcut;
    EXPECT_EQ(editor.commands().shortcutFor("viewport.selection_mode.face"), "Keypad3");
    ASSERT_TRUE(editor.commands().resolveViewportShortcut(
            quader::ViewportKey::Number3,
            {},
            state,
            true,
            false,
            shortcut));
    EXPECT_EQ(shortcut.kind, quader::ViewportShortcutKind::SetSelectionMode);
    EXPECT_EQ(shortcut.selectionMode, quader::ViewportSelectionMode::Face);

    ASSERT_TRUE(editor.preferences().setShortcut("viewport.selection_mode.face", "Ctrl+3").ok());
    EXPECT_EQ(editor.commands().shortcutFor("viewport.selection_mode.face"), "Ctrl+3");
    shortcut = {};
    EXPECT_FALSE(editor.commands().resolveViewportShortcut(
            quader::ViewportKey::Number3,
            {},
            state,
            true,
            false,
            shortcut));
    ASSERT_TRUE(editor.commands().resolveViewportShortcut(
            quader::ViewportKey::Number3,
            {.primary = true},
            state,
            true,
            false,
            shortcut));
    EXPECT_EQ(shortcut.kind, quader::ViewportShortcutKind::SetSelectionMode);
    EXPECT_EQ(shortcut.selectionMode, quader::ViewportSelectionMode::Face);
    ASSERT_TRUE(editor.preferences().resetShortcut("viewport.selection_mode.face").ok());
    EXPECT_EQ(editor.commands().shortcutFor("viewport.selection_mode.face"), "Keypad3");

    ASSERT_TRUE(editor.preferences().setShortcut(quader::QuaderCommand::EditUndo.string(), "Ctrl+U").ok());
    const quader::QuaderViewportKeybindingDispatch dispatch = editor.commands().dispatchViewportKeybinding(
            quader::ViewportKey::U,
            {.primary = true},
            state,
            true,
            false);
    EXPECT_EQ(dispatch.kind, quader::QuaderViewportKeybindingDispatch::Kind::ViewportShortcut);
    EXPECT_EQ(dispatch.shortcut.kind, quader::ViewportShortcutKind::Undo);
    EXPECT_EQ(dispatch.shortcut.context, quader::ViewportShortcutContext::Global);

    shortcut = {};
    EXPECT_FALSE(editor.commands().resolveViewportShortcut(
            quader::ViewportKey::Number3,
            {.primary = true},
            state,
            true,
            true,
            shortcut));
}

TEST(EditorApiTests, InputBindingsSupportPunctuationAndMouseChords) {
    const std::filesystem::path path = keybindingTestPath("input-chords");
    quader::ProjectSession session;
    quader::QuaderEditorApi editor(session);
    ASSERT_TRUE(editor.commands().setKeybindingStoragePath(path).ok());

    quader::ViewportToolState state;
    ASSERT_TRUE(editor.preferences().setShortcut(quader::QuaderCommand::EditUndo.string(), "Ctrl+Comma").ok());
    EXPECT_EQ(editor.commands().shortcutFor(quader::QuaderCommand::EditUndo), "Ctrl+Comma");

    const quader::QuaderViewportKeybindingDispatch commaDispatch =
            editor.commands().dispatchViewportInputBinding(
                    quader::QuaderInputChord{
                            .primary = true,
                            .device = quader::QuaderInputDevice::Keyboard,
                            .key = quader::QuaderKeyCode::Comma,
                    },
                    state,
                    true,
                    false);
    EXPECT_EQ(commaDispatch.kind, quader::QuaderViewportKeybindingDispatch::Kind::ViewportShortcut);
    EXPECT_EQ(commaDispatch.shortcut.kind, quader::ViewportShortcutKind::Undo);

    ASSERT_TRUE(editor.preferences().setShortcut(quader::QuaderCommand::EditRedo.string(), "Mouse4").ok());
    EXPECT_EQ(editor.commands().shortcutFor(quader::QuaderCommand::EditRedo), "Mouse4");

    const quader::QuaderViewportKeybindingDispatch mouseDispatch =
            editor.commands().dispatchViewportInputBinding(
                    quader::QuaderInputChord{
                            .device = quader::QuaderInputDevice::Mouse,
                            .mouseButton = quader::QuaderMouseButton::Back,
                    },
                    state,
                    true,
                    false);
    EXPECT_EQ(mouseDispatch.kind, quader::QuaderViewportKeybindingDispatch::Kind::ViewportShortcut);
    EXPECT_EQ(mouseDispatch.shortcut.kind, quader::ViewportShortcutKind::Redo);
}

TEST(EditorApiTests, ViewportNavigationActionsUseMouseInputBindings) {
    const std::filesystem::path path = keybindingTestPath("navigation-bindings");
    quader::ProjectSession session;
    quader::QuaderEditorApi editor(session);
    ASSERT_TRUE(editor.commands().setKeybindingStoragePath(path).ok());

    quader::ViewportToolState state;
    quader::ViewportInputActionKind action = quader::ViewportInputActionKind::None;
    ASSERT_TRUE(editor.commands().resolveViewportInputAction(
            *quader::quaderInputParseChord("MiddleMouseDrag"),
            state,
            true,
            false,
            action));
    EXPECT_EQ(action, quader::ViewportInputActionKind::Orbit);
    EXPECT_EQ(editor.commands().shortcutFor("viewport.navigation.pan"), "Shift+MiddleMouseDrag");
    EXPECT_EQ(editor.commands().shortcutFor("viewport.navigation.reset_fly_speed"), "MiddleMouse");

    action = quader::ViewportInputActionKind::None;
    ASSERT_TRUE(editor.commands().resolveViewportInputAction(
            *quader::quaderInputParseChord("MiddleMouse"),
            state,
            true,
            false,
            action));
    EXPECT_EQ(action, quader::ViewportInputActionKind::ResetFlySpeed);

    ASSERT_TRUE(editor.preferences().setShortcut("viewport.navigation.orbit", "Alt+LeftMouseDrag").ok());
    EXPECT_EQ(editor.commands().shortcutFor("viewport.navigation.orbit"), "Alt+LeftMouseDrag");

    action = quader::ViewportInputActionKind::None;
    EXPECT_FALSE(editor.commands().resolveViewportInputAction(
            *quader::quaderInputParseChord("MiddleMouseDrag"),
            state,
            true,
            false,
            action));
    ASSERT_TRUE(editor.commands().resolveViewportInputAction(
            *quader::quaderInputParseChord("Alt+LeftMouseDrag"),
            state,
            true,
            false,
            action));
    EXPECT_EQ(action, quader::ViewportInputActionKind::Orbit);
}

TEST(EditorApiTests, ViewportHostShortcutCatalogDrivesCommandKeybindings) {
    const std::filesystem::path path = keybindingTestPath("viewport-host-catalog");
    quader::ProjectSession session;
    quader::QuaderEditorApi editor(session);
    ASSERT_TRUE(editor.commands().setKeybindingStoragePath(path).ok());

    const std::vector<quader::QuaderKeybindingRule> rules = editor.commands().keybindings();
    int checked = 0;
    for (const quader::ViewportShortcutBindingDescriptor& descriptor : quader::viewportShortcutBindingCatalog()) {
        quader::ViewportShortcut contractShortcut;
        if (!quader::viewportShortcutFromContractTarget(descriptor.targetId, contractShortcut)) {
            continue;
        }

        const std::optional<std::string> commandTarget =
                quader::viewportCommandTargetIdForContractTarget(descriptor.targetId);
        ASSERT_TRUE(commandTarget.has_value()) << descriptor.targetId;
        EXPECT_TRUE(hasKeybinding(rules, *commandTarget, descriptor.shortcut))
                << descriptor.targetId << " -> " << *commandTarget << " / " << descriptor.shortcut;

        quader::ViewportShortcut commandShortcut;
        ASSERT_TRUE(quader::viewportShortcutForCommandTargetId(*commandTarget, commandShortcut))
                << *commandTarget;
        EXPECT_TRUE(sameViewportShortcut(contractShortcut, commandShortcut))
                << descriptor.targetId << " -> " << *commandTarget;
        ++checked;
    }

    EXPECT_GT(checked, 0);
}

TEST(EditorApiTests, ViewportNavigationActionCatalogDrivesDefaultsAndResolution) {
    const std::filesystem::path path = keybindingTestPath("viewport-action-catalog");
    quader::ProjectSession session;
    quader::QuaderEditorApi editor(session);
    ASSERT_TRUE(editor.commands().setKeybindingStoragePath(path).ok());

    const std::vector<quader::QuaderKeybindingRule> rules = editor.commands().keybindings();
    quader::ViewportToolState state;
    for (const quader::ViewportInputActionBindingDescriptor& descriptor : quader::viewportInputActionBindingCatalog()) {
        EXPECT_TRUE(hasKeybinding(rules, descriptor.targetId, descriptor.shortcut))
                << descriptor.targetId << " / " << descriptor.shortcut;

        quader::ViewportInputActionKind catalogAction = quader::ViewportInputActionKind::None;
        ASSERT_TRUE(quader::viewportInputActionForTargetId(descriptor.targetId, catalogAction))
                << descriptor.targetId;
        EXPECT_EQ(catalogAction, descriptor.action);

        const std::optional<std::vector<quader::QuaderInputChord>> parsed =
                quader::quaderInputParseShortcut(descriptor.shortcut);
        ASSERT_TRUE(parsed.has_value()) << descriptor.shortcut;
        ASSERT_EQ(parsed->size(), 1u) << descriptor.shortcut;

        quader::ViewportInputActionKind resolvedAction = quader::ViewportInputActionKind::None;
        ASSERT_TRUE(editor.commands().resolveViewportInputAction(
                parsed->front(),
                state,
                true,
                false,
                resolvedAction))
                << descriptor.targetId;
        EXPECT_EQ(resolvedAction, descriptor.action) << descriptor.targetId;
    }
}

TEST(EditorApiTests, ViewportKeybindingContextsSeparateNavigationFromToolShortcuts) {
    const std::filesystem::path path = keybindingTestPath("navigation-contexts");
    quader::ProjectSession session;
    quader::QuaderEditorApi editor(session);
    ASSERT_TRUE(editor.commands().setKeybindingStoragePath(path).ok());

    quader::ViewportToolState state;
    const quader::QuaderViewportKeybindingDispatch toolDispatch =
            editor.commands().dispatchViewportKeybinding(
                    quader::ViewportKey::W,
                    {},
                    state,
                    true,
                    false);
    ASSERT_EQ(toolDispatch.kind, quader::QuaderViewportKeybindingDispatch::Kind::ViewportShortcut);
    EXPECT_EQ(toolDispatch.shortcut.kind, quader::ViewportShortcutKind::ActivateTool);
    EXPECT_EQ(toolDispatch.shortcut.tool, quader::ViewportToolId::Move);

    const quader::QuaderViewportKeybindingDispatch navigationDispatch =
            editor.commands().dispatchViewportKeybinding(
                    quader::ViewportKey::W,
                    {},
                    state,
                    true,
                    false,
                    quader::QuaderViewportKeybindingContext{
                            .navigationActive = true,
                            .flyNavigationActive = true,
                    });
    EXPECT_EQ(navigationDispatch.kind, quader::QuaderViewportKeybindingDispatch::Kind::NoMatch);

    quader::ViewportInputActionKind action = quader::ViewportInputActionKind::None;
    ASSERT_TRUE(editor.commands().resolveViewportInputAction(
            *quader::quaderInputParseChord("RightMouseDrag"),
            state,
            true,
            false,
            action,
            quader::QuaderViewportKeybindingContext{
                    .navigationActive = true,
                    .flyNavigationActive = true,
            }));
    EXPECT_EQ(action, quader::ViewportInputActionKind::Fly);
}

TEST(EditorApiTests, ProjectDirtyStateAggregatesProjectSceneAssetAndModelingContentRevisions) {
    quader::ProjectSession session;
    quader::QuaderEditorApi editor(session);

    EXPECT_FALSE(editor.project().dirty());

    session.document().rename("Dirty Project");
    EXPECT_TRUE(editor.project().dirty());
    session.markClean();
    EXPECT_FALSE(editor.project().dirty());

    quader::QuaderCreateEntityArgs entityArgs;
    entityArgs.name = "Dirty Entity";
    const quader::QuaderApiResult<quader::QuaderEntityRef> entity =
            editor.scene().createEntity(entityArgs);
    ASSERT_TRUE(entity.ok()) << entity.errorMessage();
    EXPECT_TRUE(editor.project().dirty());
    session.markClean();
    EXPECT_FALSE(editor.project().dirty());

    quader::TransformComponent moved;
    moved.position = {1.0f, 2.0f, 3.0f};
    const quader::QuaderApiVoidResult transformed =
            editor.scene().setTransform(entity.value().id, moved);
    ASSERT_TRUE(transformed.ok()) << transformed.errorMessage();
    EXPECT_TRUE(transformed.changed());
    EXPECT_TRUE(editor.project().dirty());
    session.markClean();
    EXPECT_FALSE(editor.project().dirty());

    const quader::QuaderApiVoidResult hidden = editor.scene().setVisible(entity.value().id, false);
    ASSERT_TRUE(hidden.ok()) << hidden.errorMessage();
    EXPECT_TRUE(hidden.changed());
    EXPECT_TRUE(editor.project().dirty());
    session.markClean();
    EXPECT_FALSE(editor.project().dirty());

    const quader::QuaderApiVoidResult locked = editor.scene().setLocked(entity.value().id, true);
    ASSERT_TRUE(locked.ok()) << locked.errorMessage();
    EXPECT_TRUE(locked.changed());
    EXPECT_TRUE(editor.project().dirty());
    session.markClean();
    EXPECT_FALSE(editor.project().dirty());

    quader::QuaderMaterialArgs materialArgs;
    materialArgs.name = "Dirty Material";
    materialArgs.baseColor = {0.8f, 0.7f, 0.6f};
    const quader::QuaderApiResult<quader::QuaderAssetRef> material =
            editor.assets().addMaterial(materialArgs);
    ASSERT_TRUE(material.ok()) << material.errorMessage();
    EXPECT_TRUE(editor.project().dirty());
    session.markClean();
    EXPECT_FALSE(editor.project().dirty());

    const quader::QuaderApiVoidResult assigned =
            editor.assets().assignMaterialToEntity(entity.value().id, material.value().id);
    ASSERT_TRUE(assigned.ok()) << assigned.errorMessage();
    EXPECT_TRUE(assigned.changed());
    EXPECT_TRUE(editor.project().dirty());
    session.markClean();
    EXPECT_FALSE(editor.project().dirty());

    const quader::QuaderApiVoidResult recolored =
            editor.assets().setMaterialBaseColor(material.value().id, {0.1f, 0.2f, 0.3f});
    ASSERT_TRUE(recolored.ok()) << recolored.errorMessage();
    EXPECT_TRUE(recolored.changed());
    EXPECT_TRUE(editor.project().dirty());
    session.markClean();
    EXPECT_FALSE(editor.project().dirty());

    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> box =
            editor.modeling().createBox(
                    quader::ModelingBounds{.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
                    "Dirty Modeling Box");
    ASSERT_TRUE(box.ok()) << box.errorMessage();
    ASSERT_TRUE(editor.selection().selectObject(box.value()).ok());
    session.markClean();
    EXPECT_FALSE(editor.project().dirty());

    const quader::QuaderApiVoidResult movedSelection =
            editor.modeling().moveSelection({0.25f, 0.0f, 0.0f});
    ASSERT_TRUE(movedSelection.ok()) << movedSelection.errorMessage();
    EXPECT_TRUE(movedSelection.changed());
    EXPECT_TRUE(editor.project().dirty());
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
                 quader::QuaderCommand::ModelingDissolveEdges.string(),
                 quader::QuaderCommand::ModelingDissolveVertices.string(),
                 quader::QuaderCommand::ModelingCombineMeshes.string(),
                 quader::QuaderCommand::ModelingMergeVerticesToActive.string(),
                 quader::QuaderCommand::ModelingMergeVerticesToCenter.string(),
                 quader::QuaderCommand::ModelingMirrorSelectionX.string(),
                 quader::QuaderCommand::ModelingMirrorSelectionY.string(),
                 quader::QuaderCommand::ModelingMirrorSelectionZ.string(),
                 quader::QuaderCommand::ModelingInvertMesh.string(),
                 quader::QuaderCommand::ModelingFlipHorizontal.string(),
                 quader::QuaderCommand::ModelingFlipVertical.string(),
                  quader::QuaderCommand::ToolsActivateSelect.string(),
                  quader::QuaderCommand::ToolsActivateMove.string(),
                 quader::QuaderCommand::ToolsActivateExtend.string(),
                  quader::QuaderCommand::ToolsActivateRotate.string(),
                  quader::QuaderCommand::ToolsActivateScale.string(),
                  quader::QuaderCommand::ToolsActivateBox.string(),
                  quader::QuaderCommand::ToolsActivateExtrude.string(),
                  quader::QuaderCommand::ToolsActivateInsertEdgeLoop.string(),
                  quader::QuaderCommand::ToolsActivateKnife.string(),
                  quader::QuaderCommand::ToolsActivateCut.string(),
                  quader::QuaderCommand::ToolsActivatePoly.string(),
                  quader::QuaderCommand::ToolsActivateMirror.string(),
         }) {
        EXPECT_TRUE(ids.contains(std::string(neutral))) << neutral;
    }

    for (const quader::ModelingSelectionMode mode : {
                 quader::ModelingSelectionMode::Object,
                 quader::ModelingSelectionMode::Vertex,
                 quader::ModelingSelectionMode::Edge,
                 quader::ModelingSelectionMode::Face,
         }) {
        CommandTestHarness harness;
        setupSelectionForMode(harness.editor, mode);
        for (const quader::ModelingActionSurface surface : {
                     quader::ModelingActionSurface::OperationsPanel,
                     quader::ModelingActionSurface::ViewportContextMenu,
             }) {
            const std::vector<quader::ModelingActionItem> actions =
                    harness.editor.modeling().availableActions({
                            .surface = surface,
                            .includeDisabled = true,
                            .includeHidden = true,
                    });
            for (const quader::ModelingActionItem& action : actions) {
                SCOPED_TRACE(action.actionId);
                const std::string legacy = "modeling.action." + action.actionId;
                EXPECT_NE(harness.editor.commands().execute(legacy).errorMessage(), "Unknown command id.") << legacy;
                const std::string command = commandIdForContextAction(harness.editor, action);
                EXPECT_FALSE(command.empty()) << action.actionId;
                EXPECT_NE(harness.editor.commands().execute(command).errorMessage(), "Unknown command id.") << command;
            }
        }
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
            const std::string commandId = commandIdForContextAction(executeHarness.editor, action);
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
    const std::optional<quader::QuaderEntitySnapshot> pickedGroup =
            entityById(harness.editor, rightGroup.value().id);
    ASSERT_TRUE(pickedGroup.has_value());
    EXPECT_TRUE(pickedGroup->active);

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

    const quader::QuaderApiVoidResult unsupportedLayout =
            editor.viewports().setLayout(quader::ViewportLayoutId::FourQuad);
    EXPECT_FALSE(unsupportedLayout.ok());
    EXPECT_EQ(editor.viewports().layout(), quader::ViewportLayoutId::Single);
    EXPECT_EQ(editor.viewports().activePaneCount(), 1u);

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

    std::optional<quader::QuaderEntitySnapshot> firstAfterBatch = entityById(editor, first.value().id);
    ASSERT_TRUE(firstAfterBatch.has_value());
    EXPECT_EQ(firstAfterBatch->name, "Renamed First");
    EXPECT_EQ(firstAfterBatch->transform.position.x, 3.0f);
    std::optional<quader::QuaderEntitySnapshot> secondAfterBatch = entityById(editor, second.value().id);
    ASSERT_TRUE(secondAfterBatch.has_value());
    EXPECT_FALSE(secondAfterBatch->visible);
    EXPECT_TRUE(secondAfterBatch->locked);

    EXPECT_TRUE(editor.history().undo().ok());
    firstAfterBatch = entityById(editor, first.value().id);
    ASSERT_TRUE(firstAfterBatch.has_value());
    EXPECT_EQ(firstAfterBatch->name, "First");
    secondAfterBatch = entityById(editor, second.value().id);
    ASSERT_TRUE(secondAfterBatch.has_value());
    EXPECT_TRUE(secondAfterBatch->visible);
    EXPECT_FALSE(secondAfterBatch->locked);
}

TEST(EditorApiTests, SceneBatchUsesPendingStateForRepeatedEntityEdits) {
    quader::ProjectSession session;
    quader::QuaderEditorApi editor(session);

    quader::QuaderCreateEntityArgs args;
    args.name = "Repeated Batch Entity";
    const quader::QuaderApiResult<quader::QuaderEntityRef> entity = editor.scene().createEntity(args);
    ASSERT_TRUE(entity.ok()) << entity.errorMessage();

    quader::TransformComponent firstMove;
    firstMove.position = {1.0f, 0.0f, 0.0f};
    quader::TransformComponent secondMove;
    secondMove.position = {2.0f, 0.0f, 0.0f};

    const quader::QuaderApiVoidResult batch = editor.scene().batch("Repeated Batch")
            .setVisible(entity.value().id, false)
            .setVisible(entity.value().id, true)
            .setLocked(entity.value().id, true)
            .setLocked(entity.value().id, false)
            .setTransform(entity.value().id, firstMove)
            .setTransform(entity.value().id, secondMove)
            .commit();
    ASSERT_TRUE(batch.ok()) << batch.errorMessage();
    EXPECT_TRUE(batch.changed());

    std::optional<quader::QuaderEntitySnapshot> snapshot = entityById(editor, entity.value().id);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_TRUE(snapshot->visible);
    EXPECT_FALSE(snapshot->locked);
    EXPECT_EQ(snapshot->transform.position.x, 2.0f);

    ASSERT_TRUE(editor.history().undo().ok());
    snapshot = entityById(editor, entity.value().id);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_TRUE(snapshot->visible);
    EXPECT_FALSE(snapshot->locked);
    EXPECT_EQ(snapshot->transform.position.x, 0.0f);

    ASSERT_TRUE(editor.history().redo().ok());
    snapshot = entityById(editor, entity.value().id);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_TRUE(snapshot->visible);
    EXPECT_FALSE(snapshot->locked);
    EXPECT_EQ(snapshot->transform.position.x, 2.0f);
}

TEST(EditorApiTests, SelectionApiExposesTypedTargetsThroughPublicQueries) {
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
    quader::ModelingSelectionSummary summary = editor.modeling().selectionSummary();
    EXPECT_EQ(summary.mode, quader::ModelingSelectionMode::Object);
    EXPECT_EQ(summary.activeObjectId, first.value().modelingObjectId);
    const std::optional<quader::QuaderEntitySnapshot> firstSnapshot = entityById(editor, first.value().entity);
    ASSERT_TRUE(firstSnapshot.has_value());
    EXPECT_TRUE(firstSnapshot->active);

    ASSERT_TRUE(editor.selection().selectFace(first.value(), 1).ok());
    summary = editor.modeling().selectionSummary();
    EXPECT_EQ(summary.mode, quader::ModelingSelectionMode::Face);
    EXPECT_EQ(summary.activeObjectId, first.value().modelingObjectId);
    EXPECT_EQ(summary.activeFaceId, 1);
    EXPECT_EQ(summary.selectedFaceCount, 1u);

    ASSERT_TRUE(editor.selection().setMode(quader::ModelingSelectionMode::Object).ok());
    ASSERT_TRUE(editor.selection().selectObjects({first.value().entity, second.value().entity}, second.value().entity).ok());
    const quader::QuaderApiResult<quader::QuaderEntityRef> group = editor.scene().groupSelection("Selection Group");
    ASSERT_TRUE(group.ok()) << group.errorMessage();
    ASSERT_TRUE(editor.selection().setMode(quader::ModelingSelectionMode::Group).ok());
    summary = editor.modeling().selectionSummary();
    EXPECT_EQ(summary.mode, quader::ModelingSelectionMode::Group);
    const std::optional<quader::QuaderEntitySnapshot> groupSnapshot = entityById(editor, group.value().id);
    ASSERT_TRUE(groupSnapshot.has_value());
    EXPECT_TRUE(groupSnapshot->active);
    EXPECT_TRUE(groupSnapshot->selected);
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
    std::optional<quader::QuaderEntitySnapshot> originalSnapshot = entityById(editor, original.entity);
    ASSERT_TRUE(originalSnapshot.has_value());
    EXPECT_NEAR(originalSnapshot->transform.position.x, 3.0f, 0.001f);
    EXPECT_EQ(editor.query().history().undoLabel, "Set Transform");

    const std::vector<quader::EntityId> selectedBeforeDelete = selectedEntities(editor);
    ASSERT_FALSE(selectedBeforeDelete.empty());
    const quader::QuaderApiVoidResult deleted = editor.commands().execute(quader::QuaderCommand::DeleteSelection);
    ASSERT_TRUE(deleted.ok()) << deleted.errorMessage();
    EXPECT_EQ(editor.query().history().undoLabel, "Delete Selection");

    ASSERT_TRUE(editor.history().undo().ok());
    EXPECT_EQ(editor.query().entities().size(), 2u);
    EXPECT_EQ(editor.query().history().redoLabel, "Delete Selection");

    ASSERT_TRUE(editor.history().undo().ok());
    originalSnapshot = entityById(editor, original.entity);
    ASSERT_TRUE(originalSnapshot.has_value());
    EXPECT_NEAR(originalSnapshot->transform.position.x, 0.0f, 0.001f);
    EXPECT_EQ(editor.query().history().redoLabel, "Set Transform");

    ASSERT_TRUE(editor.history().undo().ok());
    EXPECT_EQ(editor.query().entities().size(), 1u);
    EXPECT_EQ(editor.query().history().redoLabel, "Duplicate Meshes");

    ASSERT_TRUE(editor.history().redo().ok());
    EXPECT_EQ(editor.query().entities().size(), 2u);
    ASSERT_TRUE(editor.history().redo().ok());
    originalSnapshot = entityById(editor, original.entity);
    ASSERT_TRUE(originalSnapshot.has_value());
    EXPECT_NEAR(originalSnapshot->transform.position.x, 3.0f, 0.001f);
    ASSERT_TRUE(editor.history().redo().ok());
    EXPECT_LT(editor.query().entities().size(), 2u);
}

TEST(EditorApiTests, DuplicateMeshesHistoryRestoresSceneOnlyDuplicateState) {
    quader::ProjectSession session;
    quader::QuaderEditorApi editor(session);

    quader::QuaderCreateEntityArgs parentArgs;
    parentArgs.name = "Duplicate State Parent";
    parentArgs.kind = quader::QuaderEntityKind::Empty;
    const quader::QuaderApiResult<quader::QuaderEntityRef> parent = editor.scene().createEntity(parentArgs);
    ASSERT_TRUE(parent.ok()) << parent.errorMessage();

    const quader::QuaderModelingObjectRef source = createSelectedBox(editor, "Duplicate State Source");
    ASSERT_TRUE(editor.scene().reparent(source.entity, parent.value().id, false).ok());

    quader::TransformComponent transform;
    transform.position = {2.0f, 3.0f, 4.0f};
    transform.rotationEuler = {0.25f, 0.5f, 0.75f};
    ASSERT_TRUE(editor.scene().setTransform(source.entity, transform).ok());
    ASSERT_TRUE(editor.scene().setVisible(source.entity, false).ok());
    ASSERT_TRUE(editor.scene().setLocked(source.entity, true).ok());

    const quader::QuaderApiResult<quader::QuaderAssetRef> material =
            editor.assets().addMaterial(quader::QuaderMaterialArgs{.name = "Duplicate State Material"});
    ASSERT_TRUE(material.ok()) << material.errorMessage();
    ASSERT_TRUE(editor.assets().assignMaterialToEntity(source.entity, material.value().id).ok());

    ASSERT_TRUE(editor.selection().selectObject(source).ok());
    const quader::QuaderApiVoidResult duplicated =
            editor.commands().execute(quader::QuaderCommand::DuplicateSelection);
    ASSERT_TRUE(duplicated.ok()) << duplicated.errorMessage();

    std::vector<quader::EntityId> selected = selectedEntities(editor);
    ASSERT_EQ(selected.size(), 1u);
    const quader::EntityId duplicateEntity = selected.front();
    EXPECT_NE(duplicateEntity, source.entity);
    std::optional<quader::QuaderEntitySnapshot> duplicateSnapshot = entityById(editor, duplicateEntity);
    ASSERT_TRUE(duplicateSnapshot.has_value());
    EXPECT_EQ(duplicateSnapshot->parent, parent.value().id);
    EXPECT_NEAR(duplicateSnapshot->transform.position.x, 2.0f, 0.001f);
    EXPECT_NEAR(duplicateSnapshot->transform.rotationEuler.z, 0.75f, 0.001f);
    EXPECT_FALSE(duplicateSnapshot->visible);
    EXPECT_TRUE(duplicateSnapshot->locked);
    EXPECT_TRUE(duplicateSnapshot->hasMaterial);
    EXPECT_EQ(duplicateSnapshot->material, material.value().id);
    EXPECT_TRUE(duplicateSnapshot->hasMesh);
    EXPECT_NE(duplicateSnapshot->mesh, source.mesh);

    ASSERT_TRUE(editor.history().undo().ok());
    EXPECT_FALSE(entityById(editor, duplicateEntity).has_value());

    ASSERT_TRUE(editor.history().redo().ok());
    duplicateSnapshot = entityById(editor, duplicateEntity);
    ASSERT_TRUE(duplicateSnapshot.has_value());
    EXPECT_EQ(duplicateSnapshot->parent, parent.value().id);
    EXPECT_NEAR(duplicateSnapshot->transform.position.x, 2.0f, 0.001f);
    EXPECT_NEAR(duplicateSnapshot->transform.rotationEuler.z, 0.75f, 0.001f);
    EXPECT_FALSE(duplicateSnapshot->visible);
    EXPECT_TRUE(duplicateSnapshot->locked);
    EXPECT_TRUE(duplicateSnapshot->hasMaterial);
    EXPECT_EQ(duplicateSnapshot->material, material.value().id);
    EXPECT_TRUE(duplicateSnapshot->hasMesh);
    EXPECT_NE(duplicateSnapshot->mesh, source.mesh);
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

TEST(EditorApiTests, PublicAssetMutationsRejectDanglingReferences) {
    quader::ProjectSession session;
    quader::QuaderEditorApi editor(session);

    quader::MaterialDocument invalidMaterial;
    invalidMaterial.name = "Dangling Texture Material";
    invalidMaterial.textureSlots.emplace(quader::MaterialTextureSlot::BaseColor, 9000u);
    const quader::QuaderApiResult<quader::QuaderAssetRef> added =
            editor.assets().addMaterial(invalidMaterial);
    EXPECT_FALSE(added.ok());
    EXPECT_EQ(added.errorMessage(), "Texture asset does not exist");

    const quader::QuaderApiResult<quader::QuaderAssetRef> validMaterial =
            editor.assets().addMaterial(quader::QuaderMaterialArgs{.name = "Valid Material"});
    ASSERT_TRUE(validMaterial.ok()) << validMaterial.errorMessage();
    const quader::QuaderApiVoidResult replaced =
            editor.assets().replaceMaterial(validMaterial.value().id, invalidMaterial);
    EXPECT_FALSE(replaced.ok());
    EXPECT_EQ(replaced.errorMessage(), "Texture asset does not exist");

    const quader::QuaderModelingObjectRef box = createSelectedBox(editor, "Dangling Mesh Component Box");
    quader::MeshComponent invalidMesh;
    invalidMesh.mesh = 9100u;
    const quader::QuaderApiVoidResult meshResult =
            editor.scene().setMeshComponent(box.entity, invalidMesh);
    EXPECT_FALSE(meshResult.ok());
    EXPECT_EQ(meshResult.errorMessage(), "Mesh asset does not exist");
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
    EXPECT_EQ(commandUndo.revisions.sceneRevision, editor.revisions().sceneRevision);
    EXPECT_EQ(commandUndo.revisions.modelingContentRevision, editor.revisions().modelingContentRevision);
    EXPECT_TRUE(entityNamed(editor, "Hidden Modeling Box").has_value());

    const quader::QuaderApiVoidResult unknownTool = editor.tools().activate("__missing_tool__");
    EXPECT_FALSE(unknownTool.ok());
    EXPECT_EQ(unknownTool.errorMessage(), "Unknown viewport tool.");
    EXPECT_EQ(unknownTool.revisions.sceneRevision, editor.revisions().sceneRevision);
    EXPECT_EQ(unknownTool.revisions.modelingContentRevision, editor.revisions().modelingContentRevision);
}

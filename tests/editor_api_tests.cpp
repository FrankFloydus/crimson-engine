////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "core/public_command_targets.hpp"
#include "editor/input/quader_keybinding_service.hpp"
#include "editor/input/viewport_input_binding_adapter.hpp"
#include "support/behavior_scenario.hpp"
#include "ui/viewport_shortcut_controller.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
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

std::optional<quader::QuaderCommandDescriptor> commandDescriptor(
        const quader::QuaderEditorApi& editor,
        quader::QuaderCommandId commandId) {
    const std::vector<quader::QuaderCommandDescriptor> commands = editor.commands().available();
    const auto found = std::find_if(commands.begin(), commands.end(), [&](const quader::QuaderCommandDescriptor& command) {
        return command.id == commandId.string();
    });
    return found == commands.end() ? std::nullopt : std::optional<quader::QuaderCommandDescriptor>{*found};
}

std::unordered_set<std::string> typedCommandIds() {
    std::unordered_set<std::string> ids;
    for (const quader::QuaderCommandId commandId : quader::QuaderCommand::typedCommandIds()) {
        ids.emplace(commandId.string());
    }
    return ids;
}

std::string commandIdForContextAction(
        const quader::QuaderEditorApi& editor,
        const quader::QuaderActionItem& action) {
    if (action.kind == quader::QuaderActionKind::Tool) {
        return editor.commands().commandIdForToolActivation(action.toolId);
    }
    return editor.commands().commandIdForModelingAction(action.actionId);
}

const quader::QuaderActionPanel* findActionPanel(
        const quader::QuaderActionSet& actionSet,
        std::string_view panelId) {
    const auto found = std::find_if(actionSet.panels.begin(), actionSet.panels.end(), [&](const auto& panel) {
        return panel.panelId == panelId;
    });
    return found == actionSet.panels.end() ? nullptr : &(*found);
}

const quader::QuaderActionItem* findActionItem(
        const quader::QuaderActionPanel& panel,
        std::string_view actionId) {
    const auto found = std::find_if(panel.actions.begin(), panel.actions.end(), [&](const auto& action) {
        return action.actionId == actionId;
    });
    return found == panel.actions.end() ? nullptr : &(*found);
}

/// Owns an isolated editor session for command routing tests that need fresh
/// history, selection, and scene state per assertion path.
struct CommandTestHarness {
    quader::QuaderStandaloneEditor standalone;
    quader::QuaderEditorApi& editor;

    CommandTestHarness()
        : standalone(quader::createStandaloneEditor())
        , editor(standalone.editor()) {}
};

quader::QuaderModelingObjectRef createSelectedBox(quader::QuaderEditorApi& editor, std::string name = "Command Box") {
    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> created =
            editor.modeling().createBox(
                    quader::QuaderBounds{.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
                    std::move(name));
    EXPECT_TRUE(created.ok()) << created.errorMessage();
    EXPECT_TRUE(editor.selection().selectObject(created.value()).ok());
    return created.value();
}

/// Captures material and texture ids used by undo/redo preservation regressions.
struct TexturedMaterialRefs {
    quader::AssetId material = quader::InvalidAssetId;
    quader::AssetId baseColorTexture = quader::InvalidAssetId;
    quader::AssetId roughnessTexture = quader::InvalidAssetId;
};

TexturedMaterialRefs createTexturedMaterial(quader::QuaderEditorApi& editor, std::string name) {
    quader::QuaderTextureAsset baseColor;
    baseColor.name = name + " Base";
    baseColor.usage = quader::QuaderTextureUsage::BaseColor;
    baseColor.width = 8;
    baseColor.height = 8;
    baseColor.generated = true;
    const quader::QuaderApiResult<quader::QuaderAssetRef> baseColorTexture =
            editor.assets().addTexture(std::move(baseColor));
    if (!baseColorTexture.ok()) {
        ADD_FAILURE() << baseColorTexture.errorMessage();
        return {};
    }

    quader::QuaderTextureAsset roughness;
    roughness.name = name + " Roughness";
    roughness.usage = quader::QuaderTextureUsage::Roughness;
    roughness.colorSpace = quader::QuaderTextureColorSpace::Linear;
    roughness.width = 4;
    roughness.height = 4;
    roughness.generated = true;
    const quader::QuaderApiResult<quader::QuaderAssetRef> roughnessTexture =
            editor.assets().addTexture(std::move(roughness));
    if (!roughnessTexture.ok()) {
        ADD_FAILURE() << roughnessTexture.errorMessage();
        return {};
    }

    quader::QuaderMaterialAsset material;
    material.name = std::move(name);
    material.textureSlots.emplace("baseColorTexture", baseColorTexture.value().id);
    material.textureSlots.emplace("roughnessTexture", roughnessTexture.value().id);
    const quader::QuaderApiResult<quader::QuaderAssetRef> materialRef =
            editor.assets().addMaterial(std::move(material));
    if (!materialRef.ok()) {
        ADD_FAILURE() << materialRef.errorMessage();
        return {};
    }

    return {
            .material = materialRef.value().id,
            .baseColorTexture = baseColorTexture.value().id,
            .roughnessTexture = roughnessTexture.value().id,
    };
}

void expectEntityTextured(
        const quader::QuaderEditorApi& editor,
        quader::EntityId entity,
        const TexturedMaterialRefs& refs) {
    const std::optional<quader::QuaderEntitySnapshot> snapshot = editor.query().entity(entity);
    ASSERT_TRUE(snapshot.has_value());
    ASSERT_TRUE(snapshot->hasMaterial);
    EXPECT_EQ(snapshot->material, refs.material);

    const std::optional<quader::QuaderMaterialAsset> material = editor.query().material(refs.material);
    ASSERT_TRUE(material.has_value());
    ASSERT_TRUE(material->textureSlots.contains("baseColorTexture"));
    ASSERT_TRUE(material->textureSlots.contains("roughnessTexture"));
    EXPECT_EQ(material->textureSlots.at("baseColorTexture"), refs.baseColorTexture);
    EXPECT_EQ(material->textureSlots.at("roughnessTexture"), refs.roughnessTexture);

    const std::optional<quader::QuaderTextureAsset> baseColor = editor.query().texture(refs.baseColorTexture);
    ASSERT_TRUE(baseColor.has_value());
    EXPECT_EQ(baseColor->usage, quader::QuaderTextureUsage::BaseColor);
    EXPECT_EQ(baseColor->colorSpace, quader::QuaderTextureColorSpace::Srgb);

    const std::optional<quader::QuaderTextureAsset> roughness = editor.query().texture(refs.roughnessTexture);
    ASSERT_TRUE(roughness.has_value());
    EXPECT_EQ(roughness->usage, quader::QuaderTextureUsage::Roughness);
    EXPECT_EQ(roughness->colorSpace, quader::QuaderTextureColorSpace::Linear);
}

quader::QuaderViewportCamera apiOrthographicCamera() {
    return {
            .orthographic = true,
            .origin = {0.0f, 0.0f, 10.0f},
            .xAxis = {1.0f, 0.0f, 0.0f},
            .yAxis = {0.0f, 1.0f, 0.0f},
            .zAxis = {0.0f, 0.0f, 1.0f},
            .viewportSize = {100.0f, 100.0f},
            .orthographicSize = 100.0,
            .fovDegrees = 60.0,
    };
}

void setupSelectionForMode(
        quader::QuaderEditorApi& editor,
        quader::QuaderSelectionMode mode) {
    const quader::QuaderModelingObjectRef object = createSelectedBox(editor);
    ASSERT_TRUE(editor.selection().setMode(mode).ok());
    if (mode == quader::QuaderSelectionMode::Object) {
        return;
    }
    ASSERT_TRUE(editor.selection().selectAll().ok());
    if (mode == quader::QuaderSelectionMode::Face) {
        ASSERT_TRUE(editor.selection().selectFace(object, 0).ok());
    } else if (mode == quader::QuaderSelectionMode::Edge) {
        ASSERT_TRUE(editor.selection().selectAll().ok());
    } else if (mode == quader::QuaderSelectionMode::Vertex) {
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

quader::QuaderMeshAsset triangleMesh(std::string name) {
    quader::QuaderMeshAsset mesh;
    mesh.name = std::move(name);
    mesh.vertices = {
            {.position = {0.0f, 0.0f, 0.0f}, .normal = {0.0f, 1.0f, 0.0f}, .uv = {0.0f, 0.0f}},
            {.position = {1.0f, 0.0f, 0.0f}, .normal = {0.0f, 1.0f, 0.0f}, .uv = {1.0f, 0.0f}},
            {.position = {0.0f, 0.0f, 1.0f}, .normal = {0.0f, 1.0f, 0.0f}, .uv = {0.0f, 1.0f}},
    };
    mesh.indices = {0, 1, 2};
    return mesh;
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

void expectSameApiStatus(
        const quader::QuaderApiVoidResult& actual,
        const quader::QuaderApiVoidResult& expected) {
    EXPECT_EQ(actual.ok(), expected.ok());
    EXPECT_EQ(actual.changed(), expected.changed());
    EXPECT_EQ(actual.errorMessage(), expected.errorMessage());
    EXPECT_EQ(actual.mutationKind, expected.mutationKind);
    EXPECT_EQ(actual.revisions.projectRevision, expected.revisions.projectRevision);
    EXPECT_EQ(actual.revisions.sceneRevision, expected.revisions.sceneRevision);
    EXPECT_EQ(actual.revisions.assetRevision, expected.revisions.assetRevision);
    EXPECT_EQ(actual.revisions.modelingContentRevision, expected.revisions.modelingContentRevision);
    EXPECT_EQ(actual.revisions.modelingSelectionRevision, expected.revisions.modelingSelectionRevision);
}

} // namespace

TEST(EditorApiTests, AggregatesDocumentApisAndCommandRouting) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;
    ASSERT_TRUE(editor.commands().setKeybindingStoragePath(keybindingTestPath("aggregate")).ok());

    EXPECT_FALSE(editor.project().dirty());
    EXPECT_EQ(editor.project().metadata().name, "Untitled Project");

    const std::vector<quader::QuaderCommandDescriptor> commands = editor.commands().available();
    EXPECT_NE(std::find_if(commands.begin(), commands.end(), [](const quader::QuaderCommandDescriptor& command) {
                  return command.id == quader::QuaderCommand::EditUndo.string();
              }),
            commands.end());
    const std::optional<quader::QuaderCommandDescriptor> duplicate =
            commandDescriptor(editor, quader::QuaderCommand::DuplicateSelection);
    ASSERT_TRUE(duplicate.has_value());
    EXPECT_FALSE(duplicate->enabled);
    const std::optional<quader::QuaderCommandDescriptor> applyMaterial =
            commandDescriptor(editor, quader::QuaderCommand::ApplyMaterial);
    ASSERT_TRUE(applyMaterial.has_value());
    EXPECT_FALSE(applyMaterial->enabled);
    EXPECT_TRUE(commandDescriptor(editor, quader::QuaderCommand::ProjectNew).has_value());
    EXPECT_TRUE(commandDescriptor(editor, quader::QuaderCommand::ProjectOpen).has_value());
    EXPECT_TRUE(commandDescriptor(editor, quader::QuaderCommand::ProjectSave).has_value());
    EXPECT_TRUE(commandDescriptor(editor, quader::QuaderCommand::ProjectSaveAs).has_value());
    EXPECT_EQ(editor.commands().shortcutFor(quader::QuaderCommand::EditUndo), "Ctrl+Z");
    EXPECT_EQ(editor.commands().shortcutFor(quader::QuaderCommand::ProjectNew), "Ctrl+N");
    EXPECT_EQ(editor.commands().shortcutFor(quader::QuaderCommand::ProjectOpen), "Ctrl+O");
    EXPECT_EQ(editor.commands().shortcutFor(quader::QuaderCommand::ProjectSave), "Ctrl+S");
    EXPECT_EQ(editor.commands().shortcutFor(quader::QuaderCommand::ProjectSaveAs), "");
    EXPECT_EQ(editor.commands().shortcutFor(quader::QuaderCommand::ViewportFrameSelection), "Home");
    EXPECT_EQ(editor.commands().shortcutFor(quader::QuaderCommand::ViewportFocusOrigin), "Shift+Home");
    EXPECT_EQ(editor.commands().shortcutFor(quader::QuaderCommand::ViewportSpawnWindow), "F12");
    EXPECT_EQ(editor.commands().shortcutFor(quader::QuaderCommand::ViewportViewPerspective), "P");
    EXPECT_EQ(editor.commands().shortcutFor(quader::QuaderCommand::ModelingApplyActivePreview), "Space");
    EXPECT_EQ(editor.commands().shortcutFor(quader::QuaderCommand::ModelingCancelActivePreview), "Escape");
    EXPECT_EQ(editor.commands().shortcutFor(quader::QuaderCommand::ToolsActivatePoly), "");
    EXPECT_EQ(editor.commands().shortcutFor(quader::QuaderCommand::EditUndo), "Ctrl+Z");
    EXPECT_EQ(
            editor.commands().commandIdForModelingAction("delete_selection"),
            std::string(quader::QuaderCommand::DeleteSelection.string()));
    EXPECT_EQ(editor.commands().shortcutFor(editor.commands().commandIdForModelingAction("delete_selection")), "Backspace");
    EXPECT_EQ(
            editor.commands().commandIdForModelingAction("invert_mesh"),
            std::string(quader::QuaderCommand::ModelingInvertMesh.string()));
    EXPECT_EQ(
            editor.commands().commandIdForModelingAction("bridge_edges"),
            quader::quaderModelingCommandTargetId("bridge_edges"));
    EXPECT_EQ(
            editor.commands().commandIdForToolActivation("knife"),
            quader::quaderToolActivationCommandTargetId("knife"));
    EXPECT_FALSE(editor.commands().execute("__missing_command__").ok());
    EXPECT_FALSE(editor.commands().execute("__missing_command__"));
    EXPECT_EQ(editor.commands().execute("__missing_command__").errorMessage(), "Unknown command id.");
    EXPECT_EQ(
            editor.commands().execute("modeling.__missing_action__").errorMessage(),
            "Unknown command id.");
    EXPECT_EQ(
            editor.commands().execute(quader::quaderToolActivationCommandTargetId("__missing_tool__")).errorMessage(),
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

TEST(EditorApiTests, ProjectDocumentCommandsRouteThroughProjectApi) {
    auto compareProjectCommand = [](quader::QuaderCommandId commandId, auto directCall) {
        CommandTestHarness commandHarness;
        quader::QuaderEditorApi& commandEditor = commandHarness.editor;
        CommandTestHarness directHarness;
        quader::QuaderEditorApi& directEditor = directHarness.editor;

        const quader::QuaderApiVoidResult commandResult = commandEditor.commands().execute(commandId);
        const quader::QuaderApiVoidResult directResult = directCall(directEditor.project());
        expectSameApiStatus(commandResult, directResult);
    };

    compareProjectCommand(quader::QuaderCommand::ProjectNew, [](quader::QuaderProjectApi& project) {
        return project.newProject();
    });
    compareProjectCommand(quader::QuaderCommand::ProjectOpen, [](quader::QuaderProjectApi& project) {
        return project.open({});
    });
    compareProjectCommand(quader::QuaderCommand::ProjectSave, [](quader::QuaderProjectApi& project) {
        return project.save();
    });
    compareProjectCommand(quader::QuaderCommand::ProjectSaveAs, [](quader::QuaderProjectApi& project) {
        return project.saveAs({});
    });
}

TEST(EditorApiTests, PublicAssetApiOwnsStandaloneMeshReplacementPolicy) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    const quader::QuaderApiResult<quader::QuaderAssetRef> standaloneMesh =
            editor.assets().addStandaloneMesh(triangleMesh("Standalone"));
    ASSERT_TRUE(standaloneMesh.ok()) << standaloneMesh.errorMessage();

    quader::QuaderMeshAsset replacement = triangleMesh("Replacement");
    replacement.vertices.push_back({.position = {0.0f, 1.0f, 0.0f}, .normal = {0.0f, 1.0f, 0.0f}, .uv = {0.0f, 0.0f}});
    const quader::QuaderApiVoidResult replaced =
            editor.assets().replaceStandaloneMesh(standaloneMesh.value().id, replacement);
    ASSERT_TRUE(replaced.ok()) << replaced.errorMessage();
    EXPECT_TRUE(replaced.changed());
    const std::optional<quader::QuaderMeshSnapshot> meshSnapshot = editor.query().mesh(standaloneMesh.value().id);
    ASSERT_TRUE(meshSnapshot.has_value());
    EXPECT_EQ(meshSnapshot->vertexCount, replacement.vertices.size());

    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> modelingObject =
            editor.modeling().createBox(
                    quader::QuaderBounds{.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
                    "Modeling Owned");
    ASSERT_TRUE(modelingObject.ok()) << modelingObject.errorMessage();

    const quader::QuaderApiVoidResult apiRejected =
            editor.assets().replaceStandaloneMesh(modelingObject.value().mesh, triangleMesh("Rejected"));
    EXPECT_FALSE(apiRejected.ok());
    EXPECT_NE(apiRejected.errorMessage().find("modeling object"), std::string::npos);
}

TEST(EditorApiTests, SceneCommandsRejectModelingOwnedEntityStructuralAndMeshMutations) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    const quader::QuaderModelingObjectRef box = createSelectedBox(editor, "Modeling Owned Guard");
    const quader::QuaderApiResult<quader::QuaderAssetRef> replacementMesh =
            editor.assets().addStandaloneMesh(triangleMesh("Standalone Replacement"));
    ASSERT_TRUE(replacementMesh.ok()) << replacementMesh.errorMessage();

    const quader::QuaderApiVoidResult destroyed = editor.scene().destroy(box.entity);
    EXPECT_FALSE(destroyed.ok());
    EXPECT_NE(destroyed.errorMessage().find("modeling object"), std::string::npos);
    EXPECT_TRUE(editor.query().entity(box.entity).has_value());
    EXPECT_TRUE(editor.object(box).valid());

    const quader::QuaderApiResult<quader::QuaderEntityRef> duplicated = editor.object(box).duplicate();
    EXPECT_FALSE(duplicated.ok());
    EXPECT_NE(duplicated.errorMessage().find("modeling object"), std::string::npos);

    const quader::QuaderApiVoidResult replaced =
            editor.scene().setMeshComponent(box.entity, quader::QuaderMeshComponent{.mesh = replacementMesh.value().id});
    EXPECT_FALSE(replaced.ok());
    EXPECT_NE(replaced.errorMessage().find("modeling object"), std::string::npos);
    const std::optional<quader::QuaderEntitySnapshot> snapshot = editor.query().entity(box.entity);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_TRUE(snapshot->hasMesh);
    EXPECT_EQ(snapshot->mesh, box.mesh);
}

TEST(EditorApiTests, PreferencesPersistAndResolveCustomCommandShortcuts) {
    const std::filesystem::path path = keybindingTestPath("command");
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    ASSERT_TRUE(editor.commands().setKeybindingStoragePath(path).ok());
    const quader::QuaderApiVoidResult updated =
            editor.preferences().setShortcut(quader::QuaderCommand::EditUndo, "Ctrl+Alt+Z");
    ASSERT_TRUE(updated.ok()) << updated.errorMessage();
    EXPECT_TRUE(updated.changed());
    EXPECT_EQ(editor.commands().shortcutFor(quader::QuaderCommand::EditUndo), "Ctrl+Alt+Z");
    EXPECT_EQ(editor.commands().shortcutFor(quader::QuaderCommand::EditUndo), "Ctrl+Alt+Z");
    EXPECT_NE(readTextFile(path).find("\"key\": \"Ctrl+Alt+Z\""), std::string::npos);

    CommandTestHarness reloadedHarness;
    quader::QuaderEditorApi& reloaded = reloadedHarness.editor;
    ASSERT_TRUE(reloaded.commands().setKeybindingStoragePath(path).ok());
    EXPECT_EQ(reloaded.commands().shortcutFor(quader::QuaderCommand::EditUndo), "Ctrl+Alt+Z");

    ASSERT_TRUE(reloaded.preferences().resetShortcut(quader::QuaderCommand::EditUndo).ok());
    EXPECT_EQ(reloaded.commands().shortcutFor(quader::QuaderCommand::EditUndo), "Ctrl+Z");
}

TEST(EditorApiTests, KeybindingConflictsAndRemovalSupportRebindingUi) {
    const std::filesystem::path path = keybindingTestPath("conflicts");
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    ASSERT_TRUE(editor.commands().setKeybindingStoragePath(path).ok());
    ASSERT_TRUE(editor.preferences().setShortcut(quader::QuaderCommand::EditUndo, "Ctrl+Alt+Z").ok());

    const std::vector<quader::QuaderKeybindingConflict> conflicts =
            editor.commands().conflictsForShortcut("Ctrl+Alt+Z");
    ASSERT_EQ(conflicts.size(), 1u);
    EXPECT_EQ(conflicts.front().targetId, quader::QuaderCommand::EditUndo.string());
    EXPECT_EQ(conflicts.front().source, "user");

    ASSERT_TRUE(editor.preferences().removeShortcut(quader::QuaderCommand::EditUndo).ok());
    EXPECT_EQ(editor.commands().shortcutFor(quader::QuaderCommand::EditUndo), "");
    const std::string removedUndoCommand =
            "\"command\": \"-" + std::string(quader::QuaderCommand::EditUndo.string()) + "\"";
    EXPECT_NE(readTextFile(path).find(removedUndoCommand), std::string::npos);
}

TEST(EditorApiTests, ShortcutMutationsReportNoOpChangedStatus) {
    const std::filesystem::path path = keybindingTestPath("shortcut-noops");
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    ASSERT_TRUE(editor.commands().setKeybindingStoragePath(path).ok());

    const quader::QuaderApiVoidResult sameDefault =
            editor.preferences().setShortcut(quader::QuaderCommand::EditUndo, "Ctrl+Z");
    ASSERT_TRUE(sameDefault.ok()) << sameDefault.errorMessage();
    EXPECT_FALSE(sameDefault.changed());

    const quader::QuaderApiVoidResult custom =
            editor.preferences().setShortcut(quader::QuaderCommand::EditUndo, "Ctrl+Alt+Z");
    ASSERT_TRUE(custom.ok()) << custom.errorMessage();
    EXPECT_TRUE(custom.changed());

    const quader::QuaderApiVoidResult sameCustom =
            editor.preferences().setShortcut(quader::QuaderCommand::EditUndo, "Ctrl+Alt+Z");
    ASSERT_TRUE(sameCustom.ok()) << sameCustom.errorMessage();
    EXPECT_FALSE(sameCustom.changed());

    const quader::QuaderApiVoidResult removed =
            editor.preferences().removeShortcut(quader::QuaderCommand::EditUndo);
    ASSERT_TRUE(removed.ok()) << removed.errorMessage();
    EXPECT_TRUE(removed.changed());

    const quader::QuaderApiVoidResult removedAgain =
            editor.preferences().removeShortcut(quader::QuaderCommand::EditUndo);
    ASSERT_TRUE(removedAgain.ok()) << removedAgain.errorMessage();
    EXPECT_FALSE(removedAgain.changed());

    const quader::QuaderApiVoidResult reset =
            editor.preferences().resetShortcut(quader::QuaderCommand::EditUndo);
    ASSERT_TRUE(reset.ok()) << reset.errorMessage();
    EXPECT_TRUE(reset.changed());

    const quader::QuaderApiVoidResult resetAgain =
            editor.preferences().resetShortcut(quader::QuaderCommand::EditUndo);
    ASSERT_TRUE(resetAgain.ok()) << resetAgain.errorMessage();
    EXPECT_FALSE(resetAgain.changed());
}

TEST(EditorApiTests, KeybindingConflictsUseSemanticWhenOverlap) {
    quader::QuaderKeybindingService service;
    service.registerTarget("navigation.any");
    service.registerTarget("navigation.fly");
    service.registerTarget("tool.move");
    service.registerTarget("tool.box");

    ASSERT_TRUE(service.addDefaultKeybinding("navigation.any", "F", "viewportFocus && navigation.active"));
    ASSERT_TRUE(service.addDefaultKeybinding("navigation.fly", "F", "viewportFocus && navigation.flyActive"));
    ASSERT_TRUE(service.addDefaultKeybinding("tool.move", "G", "activeTool.move"));
    ASSERT_TRUE(service.addDefaultKeybinding("tool.box", "G", "activeTool.box"));

    const std::vector<quader::QuaderKeybindingConflict> flyConflicts =
            service.conflictsFor("F", "viewportFocus && navigation.flyActive");
    EXPECT_NE(std::find_if(flyConflicts.begin(), flyConflicts.end(), [](const auto& conflict) {
                  return conflict.targetId == "navigation.any";
              }),
            flyConflicts.end());
    EXPECT_NE(std::find_if(flyConflicts.begin(), flyConflicts.end(), [](const auto& conflict) {
                  return conflict.targetId == "navigation.fly";
              }),
            flyConflicts.end());

    const std::vector<quader::QuaderKeybindingConflict> moveConflicts =
            service.conflictsFor("G", "activeTool.move");
    ASSERT_EQ(moveConflicts.size(), 1u);
    EXPECT_EQ(moveConflicts.front().targetId, "tool.move");
}

TEST(EditorApiTests, PersistedKeybindingsRejectUnknownTargets) {
    const std::filesystem::path unknownPath = keybindingTestPath("unknown-target");
    {
        std::ofstream file(unknownPath);
        file << "[{\"command\":\"missing.command\",\"key\":\"Ctrl+M\"}]\n";
    }

    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;
    const quader::QuaderApiVoidResult loaded = editor.commands().setKeybindingStoragePath(unknownPath);
    EXPECT_FALSE(loaded.ok());
    EXPECT_NE(loaded.errorMessage().find("unknown command id"), std::string::npos);

    const std::filesystem::path unknownRemovalPath = keybindingTestPath("unknown-removal-target");
    {
        std::ofstream file(unknownRemovalPath);
        file << "[{\"command\":\"-missing.command\"}]\n";
    }

    CommandTestHarness removalHarness;
    quader::QuaderEditorApi& removalEditor = removalHarness.editor;
    const quader::QuaderApiVoidResult loadedRemoval =
            removalEditor.commands().setKeybindingStoragePath(unknownRemovalPath);
    EXPECT_FALSE(loadedRemoval.ok());
    EXPECT_NE(loadedRemoval.errorMessage().find("unknown command id"), std::string::npos);
}

TEST(EditorApiTests, ViewportKeybindingsResolveContextAndUserOverrides) {
    const std::filesystem::path path = keybindingTestPath("viewport");
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;
    ASSERT_TRUE(editor.commands().setKeybindingStoragePath(path).ok());

    quader::ViewportToolState state;
    quader::ViewportShortcut shortcut;
    const std::string faceSelectionTarget = quader::quaderViewportSelectionModeTargetId("face");
    EXPECT_EQ(editor.commands().shortcutFor(faceSelectionTarget), "Keypad3");
    ASSERT_TRUE(editor.commands().resolveViewportShortcut(
            quader::ViewportKey::Number3,
            {},
            state,
            true,
            false,
            shortcut));
    EXPECT_EQ(shortcut.kind, quader::ViewportShortcutKind::SetSelectionMode);
    EXPECT_EQ(shortcut.selectionMode, quader::ViewportSelectionMode::Face);

    ASSERT_TRUE(editor.preferences().setShortcut(faceSelectionTarget, "Ctrl+3").ok());
    EXPECT_EQ(editor.commands().shortcutFor(faceSelectionTarget), "Ctrl+3");
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
    ASSERT_TRUE(editor.preferences().resetShortcut(faceSelectionTarget).ok());
    EXPECT_EQ(editor.commands().shortcutFor(faceSelectionTarget), "Keypad3");

    ASSERT_TRUE(editor.preferences().setShortcut(quader::QuaderCommand::EditUndo, "Ctrl+U").ok());
    const quader::QuaderViewportKeybindingDispatch dispatch = editor.commands().dispatchViewportKeybinding(
            quader::ViewportKey::U,
            {.primary = true},
            state,
            true,
            false);
    EXPECT_EQ(dispatch.kind, quader::QuaderViewportKeybindingDispatch::Kind::Command);
    EXPECT_EQ(dispatch.commandId, quader::QuaderCommand::EditUndo.string());

    shortcut = {};
    EXPECT_FALSE(editor.commands().resolveViewportShortcut(
            quader::ViewportKey::Number3,
            {.primary = true},
            state,
            true,
            true,
            shortcut));
}

TEST(EditorApiTests, ViewportSelectionModeShortcutsResolveAsRouterIntents) {
    /// Maps one viewport shortcut mode to the public modeling selection mode it should apply.
    struct ModeCase {
        quader::ViewportSelectionMode viewportMode;
    };
    constexpr std::array cases{
            ModeCase{quader::ViewportSelectionMode::Vertex},
            ModeCase{quader::ViewportSelectionMode::Edge},
            ModeCase{quader::ViewportSelectionMode::Face},
            ModeCase{quader::ViewportSelectionMode::Object},
            ModeCase{quader::ViewportSelectionMode::Group},
    };

    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;
    const quader::ViewportShortcutController controller;

    for (const ModeCase& mode : cases) {
        SCOPED_TRACE(static_cast<int>(mode.viewportMode));
        const std::string target =
                quader::viewportSelectionModeTargetId(quader::viewportSelectionModeId(mode.viewportMode));
        const std::optional<std::vector<quader::QuaderInputChord>> parsed =
                quader::quaderInputParseShortcut(editor.commands().shortcutFor(target));
        ASSERT_TRUE(parsed.has_value()) << target;
        ASSERT_EQ(parsed->size(), 1u) << target;

        const quader::QuaderViewportKeybindingDispatch dispatch =
                editor.commands().dispatchViewportInputBinding(parsed->front(), {}, true, false);
        ASSERT_EQ(dispatch.kind, quader::QuaderViewportKeybindingDispatch::Kind::ViewportShortcut);
        EXPECT_EQ(dispatch.shortcut.kind, quader::ViewportShortcutKind::SetSelectionMode);
        EXPECT_EQ(dispatch.shortcut.selectionMode, mode.viewportMode);

        quader::ViewportInputResult input;
        input.shortcutHandled = true;
        input.selectionModeChanged = true;
        input.shortcut = dispatch.shortcut;

        const quader::ViewportShortcutControllerResult result =
                controller.handleModelingShortcut(input, editor);

        EXPECT_FALSE(result.commandExecuted);
        EXPECT_FALSE(result.invalidateHover);
        EXPECT_FALSE(result.markOverlayDirty);
        EXPECT_EQ(editor.selection().summary().mode, quader::QuaderSelectionMode::Object);
    }
}

TEST(EditorApiTests, InputBindingsSupportPunctuationAndMouseChords) {
    const std::filesystem::path path = keybindingTestPath("input-chords");
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;
    ASSERT_TRUE(editor.commands().setKeybindingStoragePath(path).ok());

    quader::ViewportToolState state;
    ASSERT_TRUE(editor.preferences().setShortcut(quader::QuaderCommand::EditUndo, "Ctrl+Comma").ok());
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
    EXPECT_EQ(commaDispatch.kind, quader::QuaderViewportKeybindingDispatch::Kind::Command);
    EXPECT_EQ(commaDispatch.commandId, quader::QuaderCommand::EditUndo.string());

    ASSERT_TRUE(editor.preferences().setShortcut(quader::QuaderCommand::EditRedo, "Mouse4").ok());
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
    EXPECT_EQ(mouseDispatch.kind, quader::QuaderViewportKeybindingDispatch::Kind::Command);
    EXPECT_EQ(mouseDispatch.commandId, quader::QuaderCommand::EditRedo.string());
}

TEST(EditorApiTests, ViewportNavigationActionsUseMouseInputBindings) {
    const std::filesystem::path path = keybindingTestPath("navigation-bindings");
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;
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

    action = quader::ViewportInputActionKind::None;
    EXPECT_FALSE(editor.commands().resolveViewportInputAction(
            *quader::quaderInputParseChord("Escape"),
            state,
            true,
            false,
            action));
    ASSERT_TRUE(editor.commands().resolveViewportInputAction(
            *quader::quaderInputParseChord("Escape"),
            state,
            true,
            false,
            action,
            quader::QuaderViewportKeybindingContext{.navigationActive = true}));
    EXPECT_EQ(action, quader::ViewportInputActionKind::CancelNavigation);

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

TEST(EditorApiTests, ViewportKeyChordConversionUsesSharedInputAdapter) {
    const std::optional<quader::QuaderInputChord> moveChord =
            quader::quaderInputChordForViewportKey(quader::ViewportKey::W, {});
    ASSERT_TRUE(moveChord.has_value());
    EXPECT_EQ(moveChord->device, quader::QuaderInputDevice::Keyboard);
    EXPECT_EQ(moveChord->key, quader::QuaderKeyCode::W);
    EXPECT_EQ(quader::viewportKeyForQuaderKeyCode(moveChord->key), quader::ViewportKey::W);

    const std::optional<quader::QuaderInputChord> primaryDelete =
            quader::quaderInputChordForViewportKey(
                    quader::ViewportKey::Delete,
                    quader::ViewportModifiers{.primary = true});
    ASSERT_TRUE(primaryDelete.has_value());
    EXPECT_TRUE(primaryDelete->primary);
    EXPECT_EQ(primaryDelete->key, quader::QuaderKeyCode::Delete);

    EXPECT_EQ(quader::viewportKeyForQuaderKeyCode(quader::QuaderKeyCode::Numpad1), quader::ViewportKey::Number1);
    EXPECT_EQ(quader::viewportKeyForQuaderKeyCode(quader::QuaderKeyCode::NumpadEnter), quader::ViewportKey::Enter);
    EXPECT_EQ(quader::quaderKeyCodeForViewportKey(quader::ViewportKey::F12), quader::QuaderKeyCode::F12);
}

TEST(EditorApiTests, ViewportHostShortcutCatalogDrivesCommandKeybindings) {
    const std::filesystem::path path = keybindingTestPath("viewport-host-catalog");
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;
    ASSERT_TRUE(editor.commands().setKeybindingStoragePath(path).ok());

    const std::vector<quader::QuaderKeybindingRule> rules = editor.commands().keybindings();
    quader::ViewportToolState state;
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

        const std::optional<std::vector<quader::QuaderInputChord>> parsed =
                quader::quaderInputParseShortcut(descriptor.shortcut);
        ASSERT_TRUE(parsed.has_value()) << descriptor.shortcut;
        ASSERT_EQ(parsed->size(), 1u) << descriptor.shortcut;

        const quader::QuaderViewportKeybindingDispatch dispatch =
                editor.commands().dispatchViewportInputBinding(parsed->front(), state, true, false);
        if (quader::viewportTargetRequiresRouterDispatch(*commandTarget)) {
            ASSERT_EQ(dispatch.kind, quader::QuaderViewportKeybindingDispatch::Kind::ViewportShortcut)
                    << descriptor.targetId << " -> " << *commandTarget;
            EXPECT_TRUE(sameViewportShortcut(contractShortcut, dispatch.shortcut))
                    << descriptor.targetId << " -> " << *commandTarget;
        } else {
            EXPECT_EQ(dispatch.kind, quader::QuaderViewportKeybindingDispatch::Kind::Command)
                    << descriptor.targetId << " -> " << *commandTarget;
            EXPECT_EQ(dispatch.commandId, *commandTarget)
                    << descriptor.targetId << " -> " << *commandTarget;
            quader::ViewportShortcut routerShortcut;
            EXPECT_FALSE(quader::viewportRouterShortcutForTargetId(*commandTarget, routerShortcut))
                    << descriptor.targetId << " -> " << *commandTarget;
        }
        ++checked;
    }

    EXPECT_GT(checked, 0);

    const std::optional<std::vector<quader::QuaderInputChord>> perspective =
            quader::quaderInputParseShortcut("P");
    ASSERT_TRUE(perspective.has_value());
    ASSERT_EQ(perspective->size(), 1u);
    const quader::QuaderViewportKeybindingDispatch perspectiveDispatch =
            editor.commands().dispatchViewportInputBinding(perspective->front(), state, true, false);
    ASSERT_EQ(perspectiveDispatch.kind, quader::QuaderViewportKeybindingDispatch::Kind::ViewportShortcut);
    EXPECT_EQ(perspectiveDispatch.shortcut.kind, quader::ViewportShortcutKind::SetView);
    EXPECT_EQ(perspectiveDispatch.shortcut.view, quader::ViewportView::Perspective);

    EXPECT_TRUE(hasKeybinding(rules, quader::QuaderCommand::ModelingApplyActivePreview.string(), "Space"));
    EXPECT_TRUE(hasKeybinding(rules, quader::QuaderCommand::ModelingCancelActivePreview.string(), "Escape"));

    const std::optional<std::vector<quader::QuaderInputChord>> space =
            quader::quaderInputParseShortcut("Space");
    ASSERT_TRUE(space.has_value());
    ASSERT_EQ(space->size(), 1u);
    const quader::QuaderViewportKeybindingDispatch spaceWithoutPreview =
            editor.commands().dispatchViewportInputBinding(space->front(), state, true, false);
    ASSERT_EQ(spaceWithoutPreview.kind, quader::QuaderViewportKeybindingDispatch::Kind::ViewportShortcut);
    EXPECT_EQ(spaceWithoutPreview.shortcut.kind, quader::ViewportShortcutKind::ToggleMaximized);
    const quader::QuaderViewportKeybindingDispatch spaceWithPreview =
            editor.commands().dispatchViewportInputBinding(
                    space->front(),
                    state,
                    true,
                    false,
                    quader::QuaderViewportKeybindingContext{.operationPreviewActive = true});
    EXPECT_EQ(spaceWithPreview.kind, quader::QuaderViewportKeybindingDispatch::Kind::Command);
    EXPECT_EQ(spaceWithPreview.commandId, quader::QuaderCommand::ModelingApplyActivePreview.string());

    const std::optional<std::vector<quader::QuaderInputChord>> escape =
            quader::quaderInputParseShortcut("Escape");
    ASSERT_TRUE(escape.has_value());
    ASSERT_EQ(escape->size(), 1u);
    const quader::QuaderViewportKeybindingDispatch escapeWithoutPreview =
            editor.commands().dispatchViewportInputBinding(escape->front(), state, true, false);
    ASSERT_EQ(escapeWithoutPreview.kind, quader::QuaderViewportKeybindingDispatch::Kind::ViewportShortcut);
    EXPECT_EQ(escapeWithoutPreview.shortcut.kind, quader::ViewportShortcutKind::CancelActiveTool);
    const quader::QuaderViewportKeybindingDispatch escapeWithPreview =
            editor.commands().dispatchViewportInputBinding(
                    escape->front(),
                    state,
                    true,
                    false,
                    quader::QuaderViewportKeybindingContext{.operationPreviewActive = true});
    EXPECT_EQ(escapeWithPreview.kind, quader::QuaderViewportKeybindingDispatch::Kind::Command);
    EXPECT_EQ(escapeWithPreview.commandId, quader::QuaderCommand::ModelingCancelActivePreview.string());
}

TEST(EditorApiTests, DefaultKeybindingProjectionDoesNotDuplicateRuleTuples) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    std::unordered_set<std::string> seen;
    for (const quader::QuaderKeybindingRule& rule : editor.commands().keybindings()) {
        const std::string key = rule.targetId + "|" + rule.shortcut + "|" + rule.when + "|" + rule.source;
        EXPECT_TRUE(seen.insert(key).second) << key;
    }
}

TEST(EditorApiTests, ViewportNavigationActionCatalogDrivesDefaultsAndResolution) {
    const std::filesystem::path path = keybindingTestPath("viewport-action-catalog");
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;
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
        const bool navigationOnlyAction = descriptor.when.find("!navigation.active") == std::string_view::npos &&
                descriptor.when.find("navigation.active") != std::string_view::npos;
        const bool flyOnlyAction = descriptor.when.find("navigation.flyActive") != std::string_view::npos;
        const bool modalOnlyAction = descriptor.when.find("modalTool.active") != std::string_view::npos;
        quader::QuaderViewportKeybindingContext inputContext;
        if (navigationOnlyAction) {
            inputContext.navigationActive = true;
        }
        if (flyOnlyAction) {
            inputContext.navigationActive = true;
            inputContext.flyNavigationActive = true;
        }
        if (modalOnlyAction) {
            inputContext.modalToolActive = true;
        }
        ASSERT_TRUE(editor.commands().resolveViewportInputAction(
                parsed->front(),
                state,
                true,
                false,
                resolvedAction,
                inputContext))
                << descriptor.targetId;
        EXPECT_EQ(resolvedAction, descriptor.action) << descriptor.targetId;
    }
}

TEST(EditorApiTests, ViewportModalToolGesturesUseKeybindingTargets) {
    const std::filesystem::path path = keybindingTestPath("modal-tool-bindings");
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;
    ASSERT_TRUE(editor.commands().setKeybindingStoragePath(path).ok());

    quader::ViewportToolState state;
    const quader::QuaderViewportKeybindingContext modalContext{.modalToolActive = true};
    quader::ViewportInputActionKind action = quader::ViewportInputActionKind::None;
    ASSERT_TRUE(editor.commands().resolveViewportInputAction(
            *quader::quaderInputParseChord("Space"),
            state,
            true,
            false,
            action,
            modalContext));
    EXPECT_EQ(action, quader::ViewportInputActionKind::ModalToolFinalize);

    action = quader::ViewportInputActionKind::None;
    ASSERT_TRUE(editor.commands().resolveViewportInputAction(
            *quader::quaderInputParseChord("Enter"),
            state,
            true,
            false,
            action,
            modalContext));
    EXPECT_EQ(action, quader::ViewportInputActionKind::ModalToolFinalize);

    action = quader::ViewportInputActionKind::None;
    ASSERT_TRUE(editor.commands().resolveViewportInputAction(
            *quader::quaderInputParseChord("F"),
            state,
            true,
            false,
            action,
            modalContext));
    EXPECT_EQ(action, quader::ViewportInputActionKind::ModalToolModeCycle);

    action = quader::ViewportInputActionKind::None;
    EXPECT_FALSE(editor.commands().resolveViewportInputAction(
            *quader::quaderInputParseChord("Space"),
            state,
            true,
            false,
            action));

    ASSERT_TRUE(editor.preferences().setShortcut("viewport.modal_tool.finalize", "Ctrl+Enter").ok());
    action = quader::ViewportInputActionKind::None;
    EXPECT_FALSE(editor.commands().resolveViewportInputAction(
            *quader::quaderInputParseChord("Space"),
            state,
            true,
            false,
            action,
            modalContext));
    action = quader::ViewportInputActionKind::None;
    ASSERT_TRUE(editor.commands().resolveViewportInputAction(
            *quader::quaderInputParseChord("Ctrl+Enter"),
            state,
            true,
            false,
            action,
            modalContext));
    EXPECT_EQ(action, quader::ViewportInputActionKind::ModalToolFinalize);

    ASSERT_TRUE(editor.preferences().setShortcut("viewport.modal_tool.mode_cycle", "Shift+F").ok());
    action = quader::ViewportInputActionKind::None;
    EXPECT_FALSE(editor.commands().resolveViewportInputAction(
            *quader::quaderInputParseChord("F"),
            state,
            true,
            false,
            action,
            modalContext));
    action = quader::ViewportInputActionKind::None;
    ASSERT_TRUE(editor.commands().resolveViewportInputAction(
            *quader::quaderInputParseChord("Shift+F"),
            state,
            true,
            false,
            action,
            modalContext));
    EXPECT_EQ(action, quader::ViewportInputActionKind::ModalToolModeCycle);
}

TEST(EditorApiTests, ViewportKeybindingContextsSeparateNavigationFromToolShortcuts) {
    const std::filesystem::path path = keybindingTestPath("navigation-contexts");
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;
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

    action = quader::ViewportInputActionKind::None;
    ASSERT_TRUE(editor.commands().resolveViewportInputAction(
            *quader::quaderInputParseChord("W"),
            state,
            true,
            false,
            action,
            quader::QuaderViewportKeybindingContext{
                    .navigationActive = true,
                    .flyNavigationActive = true,
            }));
    EXPECT_EQ(action, quader::ViewportInputActionKind::FlyForward);

    ASSERT_TRUE(editor.preferences().setShortcut("viewport.navigation.fly_forward", "Up").ok());
    action = quader::ViewportInputActionKind::None;
    EXPECT_FALSE(editor.commands().resolveViewportInputAction(
            *quader::quaderInputParseChord("W"),
            state,
            true,
            false,
            action,
            quader::QuaderViewportKeybindingContext{
                    .navigationActive = true,
                    .flyNavigationActive = true,
            }));
    ASSERT_TRUE(editor.commands().resolveViewportInputAction(
            *quader::quaderInputParseChord("Up"),
            state,
            true,
            false,
            action,
            quader::QuaderViewportKeybindingContext{
                    .navigationActive = true,
                    .flyNavigationActive = true,
            }));
    EXPECT_EQ(action, quader::ViewportInputActionKind::FlyForward);
}

TEST(EditorApiTests, PublicMutationsMarkStandaloneProjectDirty) {
    auto expectDirtyAfter = [](auto mutate) {
        CommandTestHarness harness;
        quader::QuaderEditorApi& editor = harness.editor;
        EXPECT_FALSE(editor.project().dirty());
        mutate(editor);
        EXPECT_TRUE(editor.project().dirty());
    };

    expectDirtyAfter([](quader::QuaderEditorApi& editor) {
        quader::QuaderCreateEntityArgs entityArgs;
        entityArgs.name = "Dirty Entity";
        const quader::QuaderApiResult<quader::QuaderEntityRef> entity =
                editor.scene().createEntity(entityArgs);
        ASSERT_TRUE(entity.ok()) << entity.errorMessage();
    });
    expectDirtyAfter([](quader::QuaderEditorApi& editor) {
        quader::QuaderMaterialArgs materialArgs;
        materialArgs.name = "Dirty Material";
        materialArgs.baseColor = {0.8f, 0.7f, 0.6f};
        const quader::QuaderApiResult<quader::QuaderAssetRef> material =
                editor.assets().addMaterial(materialArgs);
        ASSERT_TRUE(material.ok()) << material.errorMessage();
    });
    expectDirtyAfter([](quader::QuaderEditorApi& editor) {
        const quader::QuaderApiResult<quader::QuaderModelingObjectRef> box =
                editor.modeling().createBox(
                        quader::QuaderBounds{.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
                        "Dirty Modeling Box");
        ASSERT_TRUE(box.ok()) << box.errorMessage();
    });
}

TEST(EditorApiTests, RetiredModelingActionCommandAliasesDoNotRouteThroughCommandRegistry) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    EXPECT_EQ(
            editor.commands().commandIdForModelingAction("duplicate_meshes"),
            std::string(quader::QuaderCommand::DuplicateSelection.string()));
    EXPECT_EQ(
            editor.commands().execute("modeling.action.duplicate_meshes").errorMessage(),
            "Unknown command id.");
    EXPECT_EQ(
            editor.commands().execute("modeling.duplicate_meshes").errorMessage(),
            "Unknown command id.");
    for (const std::string_view retiredCommandId : {
                 std::string_view{"modeling.action.delete_selection"},
                 std::string_view{"modeling.delete_selection"},
                 std::string_view{"history.undo"},
                 std::string_view{"history.redo"},
                 std::string_view{"history.repeat_last_action"},
                 std::string_view{"selection.clear"},
                 std::string_view{"selection.select_all"},
                 std::string_view{"selection.invert"},
                 std::string_view{"scene.group_selection"},
                 std::string_view{"scene.ungroup_selection"},
         }) {
        EXPECT_EQ(editor.commands().execute(retiredCommandId).errorMessage(), "Unknown command id.")
                << retiredCommandId;
    }
}

TEST(EditorApiTests, CommandRegistryCoversEveryAppOperationAndViewportTool) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;
    const std::unordered_set<std::string> ids = availableCommandIds(editor);
    const std::unordered_set<std::string> typedIds = typedCommandIds();

    for (const std::string& id : ids) {
        EXPECT_TRUE(typedIds.contains(id)) << id;
    }

    for (const std::string& typedId : typedIds) {
        EXPECT_TRUE(ids.contains(typedId)) << typedId;
    }

    for (const quader::QuaderSelectionMode mode : {
                 quader::QuaderSelectionMode::Object,
                 quader::QuaderSelectionMode::Vertex,
                 quader::QuaderSelectionMode::Edge,
                 quader::QuaderSelectionMode::Face,
         }) {
        CommandTestHarness harness;
        setupSelectionForMode(harness.editor, mode);
        for (const quader::QuaderActionSurface surface : {
                     quader::QuaderActionSurface::OperationsPanel,
                     quader::QuaderActionSurface::ViewportContextMenu,
             }) {
            const std::vector<quader::QuaderActionItem> actions =
                    harness.editor.modeling().availableActions({
                            .surface = surface,
                            .includeDisabled = true,
                            .includeHidden = true,
                    });
            for (const quader::QuaderActionItem& action : actions) {
                SCOPED_TRACE(action.actionId);
                const std::string command = commandIdForContextAction(harness.editor, action);
                EXPECT_FALSE(command.empty()) << action.actionId;
                CommandTestHarness commandRouteHarness;
                EXPECT_NE(commandRouteHarness.editor.commands().execute(command).errorMessage(), "Unknown command id.")
                        << command;
            }
        }
    }
}

TEST(EditorApiTests, ModelingActionSetExposesActiveOperationPanelForSelectionModeShelf) {
    const quader::QuaderActionQuery query{
            .surface = quader::QuaderActionSurface::OperationsPanel,
            .includeDisabled = true,
            .includeHidden = false,
    };

    CommandTestHarness objectHarness;
    setupSelectionForMode(objectHarness.editor, quader::QuaderSelectionMode::Object);
    const quader::QuaderActionSet objectActions = objectHarness.editor.modeling().availableActionSet(query);
    ASSERT_EQ(objectActions.panels.size(), 1u);
    const quader::QuaderActionPanel* objectPanel = findActionPanel(objectActions, "object_operations");
    ASSERT_NE(objectPanel, nullptr);
    EXPECT_EQ(objectPanel->visibility, quader::QuaderActionVisibility::Enabled);
    ASSERT_FALSE(objectPanel->actions.empty());
    EXPECT_EQ(objectPanel->actions.front().actionId, "invert_mesh");
    const quader::QuaderActionItem* combine = findActionItem(*objectPanel, "combine_mesh");
    ASSERT_NE(combine, nullptr);
    EXPECT_EQ(combine->visibility, quader::QuaderActionVisibility::Disabled);

    CommandTestHarness faceHarness;
    setupSelectionForMode(faceHarness.editor, quader::QuaderSelectionMode::Face);
    const quader::QuaderActionSet faceActions = faceHarness.editor.modeling().availableActionSet(query);
    ASSERT_EQ(faceActions.panels.size(), 1u);
    const quader::QuaderActionPanel* facePanel = findActionPanel(faceActions, "face_operations");
    ASSERT_NE(facePanel, nullptr);
    EXPECT_EQ(facePanel->visibility, quader::QuaderActionVisibility::Enabled);
    ASSERT_FALSE(facePanel->actions.empty());
    EXPECT_EQ(facePanel->actions.front().actionId, "bridge_faces");
    const quader::QuaderActionItem* thicken = findActionItem(*facePanel, "thicken_faces");
    ASSERT_NE(thicken, nullptr);
    EXPECT_EQ(thicken->operationId, "thicken");

    CommandTestHarness groupHarness;
    createSelectedBox(groupHarness.editor);
    ASSERT_TRUE(groupHarness.editor.selection().setMode(quader::QuaderSelectionMode::Group).ok());
    EXPECT_TRUE(groupHarness.editor.modeling().availableActionSet(query).panels.empty());
}

TEST(EditorApiTests, EnabledContextMenuOperationsExecuteThroughCommandRegistry) {
    int routedActionCount = 0;
    int successfulActionCount = 0;
    for (const quader::QuaderSelectionMode mode : {
                 quader::QuaderSelectionMode::Object,
                 quader::QuaderSelectionMode::Vertex,
                 quader::QuaderSelectionMode::Edge,
                 quader::QuaderSelectionMode::Face,
         }) {
        CommandTestHarness listHarness;
        setupSelectionForMode(listHarness.editor, mode);
        const std::vector<quader::QuaderActionItem> actions =
                listHarness.editor.modeling().availableActions({
                        .surface = quader::QuaderActionSurface::ViewportContextMenu,
                        .includeDisabled = false,
                        .includeHidden = false,
                });

        for (const quader::QuaderActionItem& action : actions) {
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

TEST(EditorApiTests, ActivePreviewCommandsApplyAndCancelSliceQuadWithoutUi) {
    {
        CommandTestHarness harness;
        const quader::QuaderModelingObjectRef box = createSelectedBox(harness.editor, "Slice Apply Command Box");
        ASSERT_TRUE(harness.editor.selection().setMode(quader::QuaderSelectionMode::Face).ok());
        quader::QuaderPickSelectRequest pickFace;
        pickFace.ray = {
                .origin = {0.0f, 0.0f, 5.0f},
                .direction = {0.0f, 0.0f, -1.0f},
        };
        const quader::QuaderApiResult<quader::QuaderSelectionSummary> picked =
                harness.editor.selection().pickSelect(pickFace);
        ASSERT_TRUE(picked.ok()) << picked.errorMessage();
        ASSERT_EQ(picked.value().selectedFaceCount, 1u);
        const std::optional<quader::QuaderMeshSnapshot> before = harness.editor.query().mesh(box.mesh);
        ASSERT_TRUE(before.has_value());

        const quader::QuaderApiVoidResult preview =
                harness.editor.commands().execute(harness.editor.commands().commandIdForModelingAction("slice_quad"));
        ASSERT_TRUE(preview.ok()) << preview.errorMessage();
        EXPECT_TRUE(preview.changed());
        const std::optional<quader::QuaderMeshSnapshot> previewMesh = harness.editor.query().mesh(box.mesh);
        ASSERT_TRUE(previewMesh.has_value());
        EXPECT_GT(previewMesh->vertexCount, before->vertexCount);
        EXPECT_GT(previewMesh->primitiveCount, before->primitiveCount);

        quader::QuaderPickSelectRequest missPick;
        missPick.ray = {
                .origin = {100.0f, 100.0f, 100.0f},
                .direction = {1.0f, 0.0f, 0.0f},
        };
        const quader::QuaderApiResult<quader::QuaderSelectionSummary> missed =
                harness.editor.selection().pickSelect(missPick);
        ASSERT_TRUE(missed.ok()) << missed.errorMessage();
        EXPECT_EQ(missed.value().selectedFaceCount, 4u);
        const std::optional<quader::QuaderMeshSnapshot> previewAfterMiss = harness.editor.query().mesh(box.mesh);
        ASSERT_TRUE(previewAfterMiss.has_value());
        EXPECT_EQ(previewAfterMiss->vertexCount, previewMesh->vertexCount);
        EXPECT_EQ(previewAfterMiss->primitiveCount, previewMesh->primitiveCount);

        const quader::QuaderApiVoidResult apply =
                harness.editor.commands().execute(quader::QuaderCommand::ModelingApplyActivePreview);
        ASSERT_TRUE(apply.ok()) << apply.errorMessage();
        EXPECT_TRUE(apply.changed());
        EXPECT_EQ(harness.editor.query().history().undoLabel, "Apply Tool Preview");
        const std::optional<quader::QuaderMeshSnapshot> committed = harness.editor.query().mesh(box.mesh);
        ASSERT_TRUE(committed.has_value());
        EXPECT_EQ(committed->vertexCount, previewMesh->vertexCount);
        EXPECT_EQ(committed->primitiveCount, previewMesh->primitiveCount);
        EXPECT_FALSE(harness.editor.commands().execute(quader::QuaderCommand::ModelingApplyActivePreview).ok());
    }

    {
        CommandTestHarness harness;
        const quader::QuaderModelingObjectRef box = createSelectedBox(harness.editor, "Slice Cancel Command Box");
        ASSERT_TRUE(harness.editor.selection().setMode(quader::QuaderSelectionMode::Face).ok());
        quader::QuaderPickSelectRequest pickFace;
        pickFace.ray = {
                .origin = {0.0f, 0.0f, 5.0f},
                .direction = {0.0f, 0.0f, -1.0f},
        };
        const quader::QuaderApiResult<quader::QuaderSelectionSummary> picked =
                harness.editor.selection().pickSelect(pickFace);
        ASSERT_TRUE(picked.ok()) << picked.errorMessage();
        ASSERT_EQ(picked.value().selectedFaceCount, 1u);
        const std::optional<quader::QuaderMeshSnapshot> before = harness.editor.query().mesh(box.mesh);
        ASSERT_TRUE(before.has_value());

        const quader::QuaderApiVoidResult preview =
                harness.editor.commands().execute(harness.editor.commands().commandIdForModelingAction("slice_quad"));
        ASSERT_TRUE(preview.ok()) << preview.errorMessage();
        const std::optional<quader::QuaderMeshSnapshot> previewMesh = harness.editor.query().mesh(box.mesh);
        ASSERT_TRUE(previewMesh.has_value());
        EXPECT_GT(previewMesh->vertexCount, before->vertexCount);

        const quader::QuaderApiVoidResult cancel =
                harness.editor.commands().execute(quader::QuaderCommand::ModelingCancelActivePreview);
        ASSERT_TRUE(cancel.ok()) << cancel.errorMessage();
        EXPECT_TRUE(cancel.changed());
        const std::optional<quader::QuaderMeshSnapshot> restored = harness.editor.query().mesh(box.mesh);
        ASSERT_TRUE(restored.has_value());
        EXPECT_EQ(restored->vertexCount, before->vertexCount);
        EXPECT_EQ(restored->primitiveCount, before->primitiveCount);
        EXPECT_FALSE(harness.editor.commands().execute(quader::QuaderCommand::ModelingCancelActivePreview).ok());
    }
}

TEST(EditorApiTests, HistoryUndoCancelsActiveBevelPreviewBeforeDocumentHistory) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;
    const quader::QuaderModelingObjectRef box = createSelectedBox(editor, "Undo Bevel Preview Box");
    ASSERT_TRUE(editor.selection().setMode(quader::QuaderSelectionMode::Edge).ok());
    ASSERT_TRUE(editor.selection().selectAll().ok());
    ASSERT_GT(editor.selection().summary().selectedEdgeCount, 0u);

    const std::optional<quader::QuaderMeshSnapshot> before = editor.query().mesh(box.mesh);
    ASSERT_TRUE(before.has_value());
    const quader::QuaderHistorySnapshot historyBeforePreview = editor.query().history();

    const quader::QuaderApiVoidResult preview =
            editor.commands().execute(editor.commands().commandIdForModelingAction("bevel_edges"));
    ASSERT_TRUE(preview.ok()) << preview.errorMessage();
    EXPECT_TRUE(preview.changed());
    EXPECT_TRUE(editor.modeling().canCancelActiveToolOrPreview());
    const std::optional<quader::QuaderMeshSnapshot> previewMesh = editor.query().mesh(box.mesh);
    ASSERT_TRUE(previewMesh.has_value());
    EXPECT_GT(previewMesh->primitiveCount, before->primitiveCount);

    const quader::QuaderApiVoidResult undo = editor.commands().execute(quader::QuaderCommand::HistoryUndo);
    ASSERT_TRUE(undo.ok()) << undo.errorMessage();
    EXPECT_TRUE(undo.changed());
    EXPECT_FALSE(editor.modeling().canCancelActiveToolOrPreview());
    EXPECT_TRUE(entityNamed(editor, "Undo Bevel Preview Box").has_value());
    EXPECT_EQ(editor.query().history().undoLabel, historyBeforePreview.undoLabel);

    const std::optional<quader::QuaderMeshSnapshot> restored = editor.query().mesh(box.mesh);
    ASSERT_TRUE(restored.has_value());
    EXPECT_EQ(restored->vertexCount, before->vertexCount);
    EXPECT_EQ(restored->primitiveCount, before->primitiveCount);
}

TEST(EditorApiTests, HistoryUndoAfterSliceQuadFinalizeRestoresPreOperationMeshAndSelection) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;
    const quader::QuaderModelingObjectRef box = createSelectedBox(editor, "Undo Slice Quad Box");
    ASSERT_TRUE(editor.selection().setMode(quader::QuaderSelectionMode::Face).ok());
    ASSERT_TRUE(editor.selection().selectFace(box, 1).ok());
    ASSERT_EQ(editor.selection().summary().selectedFaceCount, 1u);

    const std::optional<quader::QuaderMeshSnapshot> before = editor.query().mesh(box.mesh);
    ASSERT_TRUE(before.has_value());
    const quader::QuaderHistorySnapshot historyBeforePreview = editor.query().history();

    const quader::QuaderApiVoidResult preview =
            editor.commands().execute(editor.commands().commandIdForModelingAction("slice_quad"));
    ASSERT_TRUE(preview.ok()) << preview.errorMessage();
    const std::optional<quader::QuaderMeshSnapshot> previewMesh = editor.query().mesh(box.mesh);
    ASSERT_TRUE(previewMesh.has_value());
    EXPECT_GT(previewMesh->vertexCount, before->vertexCount);
    EXPECT_GT(previewMesh->primitiveCount, before->primitiveCount);

    const quader::QuaderApiVoidResult apply =
            editor.commands().execute(quader::QuaderCommand::ModelingApplyActivePreview);
    ASSERT_TRUE(apply.ok()) << apply.errorMessage();
    EXPECT_EQ(editor.query().history().undoLabel, "Apply Tool Preview");
    const quader::QuaderSelectionSummary committedSelection = editor.selection().summary();
    EXPECT_GT(committedSelection.selectedFaceCount, 1u);

    const quader::QuaderApiVoidResult undo = editor.commands().execute(quader::QuaderCommand::HistoryUndo);
    ASSERT_TRUE(undo.ok()) << undo.errorMessage();
    EXPECT_TRUE(undo.changed());
    EXPECT_TRUE(entityNamed(editor, "Undo Slice Quad Box").has_value());
    EXPECT_EQ(editor.selection().summary().selectedFaceCount, 1u);
    EXPECT_EQ(editor.query().history().undoLabel, historyBeforePreview.undoLabel);

    const std::optional<quader::QuaderMeshSnapshot> restored = editor.query().mesh(box.mesh);
    ASSERT_TRUE(restored.has_value());
    EXPECT_EQ(restored->vertexCount, before->vertexCount);
    EXPECT_EQ(restored->primitiveCount, before->primitiveCount);
}

TEST(EditorApiTests, MixedUndoRedoPreviewStackPreservesTexturedMaterial) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;
    const quader::QuaderModelingObjectRef box = createSelectedBox(editor, "Mixed Stack Textured Box");
    const TexturedMaterialRefs refs = createTexturedMaterial(editor, "Mixed Stack Material");
    ASSERT_NE(refs.material, quader::InvalidAssetId);

    const quader::QuaderApiVoidResult assigned = editor.assets().assignMaterialToEntity(box.entity, refs.material);
    ASSERT_TRUE(assigned.ok()) << assigned.errorMessage();
    EXPECT_TRUE(assigned.changed());
    expectEntityTextured(editor, box.entity, refs);

    const std::optional<quader::QuaderMeshSnapshot> original = editor.query().mesh(box.mesh);
    ASSERT_TRUE(original.has_value());

    ASSERT_TRUE(editor.selection().setMode(quader::QuaderSelectionMode::Edge).ok());
    ASSERT_TRUE(editor.selection().selectAll().ok());
    const quader::QuaderApiVoidResult bevelPreview =
            editor.commands().execute(editor.commands().commandIdForModelingAction("bevel_edges"));
    ASSERT_TRUE(bevelPreview.ok()) << bevelPreview.errorMessage();
    EXPECT_TRUE(editor.modeling().canCancelActiveToolOrPreview());
    const std::optional<quader::QuaderMeshSnapshot> bevelPreviewMesh = editor.query().mesh(box.mesh);
    ASSERT_TRUE(bevelPreviewMesh.has_value());
    EXPECT_GT(bevelPreviewMesh->primitiveCount, original->primitiveCount);

    const quader::QuaderApiVoidResult cancelPreviewViaUndo =
            editor.commands().execute(quader::QuaderCommand::HistoryUndo);
    ASSERT_TRUE(cancelPreviewViaUndo.ok()) << cancelPreviewViaUndo.errorMessage();
    EXPECT_TRUE(cancelPreviewViaUndo.changed());
    EXPECT_FALSE(editor.modeling().canCancelActiveToolOrPreview());
    const std::optional<quader::QuaderMeshSnapshot> afterPreviewCancel = editor.query().mesh(box.mesh);
    ASSERT_TRUE(afterPreviewCancel.has_value());
    EXPECT_EQ(afterPreviewCancel->vertexCount, original->vertexCount);
    EXPECT_EQ(afterPreviewCancel->primitiveCount, original->primitiveCount);
    expectEntityTextured(editor, box.entity, refs);

    const quader::QuaderApiVoidResult undoMaterial = editor.commands().execute(quader::QuaderCommand::HistoryUndo);
    ASSERT_TRUE(undoMaterial.ok()) << undoMaterial.errorMessage();
    EXPECT_TRUE(undoMaterial.changed());
    const std::optional<quader::QuaderEntitySnapshot> unassigned = editor.query().entity(box.entity);
    ASSERT_TRUE(unassigned.has_value());
    EXPECT_FALSE(unassigned->hasMaterial);

    const quader::QuaderApiVoidResult redoMaterial = editor.commands().execute(quader::QuaderCommand::HistoryRedo);
    ASSERT_TRUE(redoMaterial.ok()) << redoMaterial.errorMessage();
    EXPECT_TRUE(redoMaterial.changed());
    expectEntityTextured(editor, box.entity, refs);

    ASSERT_TRUE(editor.selection().setMode(quader::QuaderSelectionMode::Face).ok());
    ASSERT_TRUE(editor.selection().clear().ok());
    ASSERT_TRUE(editor.selection().selectFace(box, 1).ok());
    ASSERT_EQ(editor.selection().summary().selectedFaceCount, 1u);
    const std::optional<quader::QuaderMeshSnapshot> beforeSlice = editor.query().mesh(box.mesh);
    ASSERT_TRUE(beforeSlice.has_value());

    const quader::QuaderApiVoidResult slicePreview =
            editor.commands().execute(editor.commands().commandIdForModelingAction("slice_quad"));
    ASSERT_TRUE(slicePreview.ok()) << slicePreview.errorMessage();
    const quader::QuaderApiVoidResult applySlice =
            editor.commands().execute(quader::QuaderCommand::ModelingApplyActivePreview);
    ASSERT_TRUE(applySlice.ok()) << applySlice.errorMessage();
    const std::optional<quader::QuaderMeshSnapshot> sliced = editor.query().mesh(box.mesh);
    ASSERT_TRUE(sliced.has_value());
    EXPECT_GT(sliced->vertexCount, beforeSlice->vertexCount);
    EXPECT_GT(sliced->primitiveCount, beforeSlice->primitiveCount);
    EXPECT_GT(editor.selection().summary().selectedFaceCount, 1u);
    expectEntityTextured(editor, box.entity, refs);

    const quader::QuaderApiVoidResult undoSlice = editor.commands().execute(quader::QuaderCommand::HistoryUndo);
    ASSERT_TRUE(undoSlice.ok()) << undoSlice.errorMessage();
    EXPECT_TRUE(undoSlice.changed());
    const std::optional<quader::QuaderMeshSnapshot> afterSliceUndo = editor.query().mesh(box.mesh);
    ASSERT_TRUE(afterSliceUndo.has_value());
    EXPECT_EQ(afterSliceUndo->vertexCount, beforeSlice->vertexCount);
    EXPECT_EQ(afterSliceUndo->primitiveCount, beforeSlice->primitiveCount);
    EXPECT_EQ(editor.selection().summary().selectedFaceCount, 1u);
    expectEntityTextured(editor, box.entity, refs);

    const quader::QuaderApiVoidResult redoSlice = editor.commands().execute(quader::QuaderCommand::HistoryRedo);
    ASSERT_TRUE(redoSlice.ok()) << redoSlice.errorMessage();
    EXPECT_TRUE(redoSlice.changed());
    const std::optional<quader::QuaderMeshSnapshot> afterSliceRedo = editor.query().mesh(box.mesh);
    ASSERT_TRUE(afterSliceRedo.has_value());
    EXPECT_EQ(afterSliceRedo->vertexCount, sliced->vertexCount);
    EXPECT_EQ(afterSliceRedo->primitiveCount, sliced->primitiveCount);
    EXPECT_GT(editor.selection().summary().selectedFaceCount, 1u);
    expectEntityTextured(editor, box.entity, refs);

    ASSERT_TRUE(editor.selection().setMode(quader::QuaderSelectionMode::Object).ok());
    ASSERT_TRUE(editor.selection().selectObject(box).ok());
    const quader::QuaderApiVoidResult deleteSelection =
            editor.commands().execute(quader::QuaderCommand::DeleteSelection);
    ASSERT_TRUE(deleteSelection.ok()) << deleteSelection.errorMessage();
    EXPECT_TRUE(deleteSelection.changed());
    EXPECT_FALSE(entityById(editor, box.entity).has_value());

    const quader::QuaderApiVoidResult undoDelete = editor.commands().execute(quader::QuaderCommand::HistoryUndo);
    ASSERT_TRUE(undoDelete.ok()) << undoDelete.errorMessage();
    EXPECT_TRUE(undoDelete.changed());
    ASSERT_TRUE(entityById(editor, box.entity).has_value());
    const std::optional<quader::QuaderMeshSnapshot> restoredAfterDelete = editor.query().mesh(box.mesh);
    ASSERT_TRUE(restoredAfterDelete.has_value());
    EXPECT_EQ(restoredAfterDelete->vertexCount, sliced->vertexCount);
    EXPECT_EQ(restoredAfterDelete->primitiveCount, sliced->primitiveCount);
    expectEntityTextured(editor, box.entity, refs);

    const quader::QuaderApiVoidResult redoDelete = editor.commands().execute(quader::QuaderCommand::HistoryRedo);
    ASSERT_TRUE(redoDelete.ok()) << redoDelete.errorMessage();
    EXPECT_TRUE(redoDelete.changed());
    EXPECT_FALSE(entityById(editor, box.entity).has_value());

    const quader::QuaderApiVoidResult undoDeleteAgain = editor.commands().execute(quader::QuaderCommand::HistoryUndo);
    ASSERT_TRUE(undoDeleteAgain.ok()) << undoDeleteAgain.errorMessage();
    EXPECT_TRUE(undoDeleteAgain.changed());
    ASSERT_TRUE(entityById(editor, box.entity).has_value());
    expectEntityTextured(editor, box.entity, refs);
}

TEST(EditorApiTests, SliceQuadPreviewRebuildsAfterDirectFaceAddWithoutUi) {
    CommandTestHarness harness;
    const quader::QuaderModelingObjectRef box = createSelectedBox(harness.editor, "Slice Direct Add Command Box");
    ASSERT_TRUE(harness.editor.selection().setMode(quader::QuaderSelectionMode::Face).ok());
    ASSERT_TRUE(harness.editor.selection().selectFace(box, 1).ok());

    const quader::QuaderApiVoidResult preview =
            harness.editor.commands().execute(harness.editor.commands().commandIdForModelingAction("slice_quad"));
    ASSERT_TRUE(preview.ok()) << preview.errorMessage();
    const std::optional<quader::QuaderMeshSnapshot> oneFacePreview = harness.editor.query().mesh(box.mesh);
    ASSERT_TRUE(oneFacePreview.has_value());

    const quader::QuaderApiVoidResult addFace =
            harness.editor.selection().selectFace(box, 2, quader::QuaderSelectionEdit::Add);
    ASSERT_TRUE(addFace.ok()) << addFace.errorMessage();
    EXPECT_TRUE(addFace.changed());
    const quader::QuaderSelectionSummary summary = harness.editor.selection().summary();
    EXPECT_EQ(summary.mode, quader::QuaderSelectionMode::Face);
    EXPECT_GT(summary.selectedFaceCount, 1u);

    const std::optional<quader::QuaderMeshSnapshot> twoFacePreview = harness.editor.query().mesh(box.mesh);
    ASSERT_TRUE(twoFacePreview.has_value());
    EXPECT_GT(twoFacePreview->vertexCount, oneFacePreview->vertexCount);
    EXPECT_GT(twoFacePreview->primitiveCount, oneFacePreview->primitiveCount);
}

TEST(EditorApiTests, DeleteCommandRouteRemovesSameObjectAndExposesUndoLabel) {
    CommandTestHarness harness;
    EXPECT_EQ(
            harness.editor.commands().commandIdForModelingAction("delete_selection"),
            std::string(quader::QuaderCommand::DeleteSelection.string()));

    const quader::QuaderModelingObjectRef first = createSelectedBox(harness.editor, "Delete Route First");
    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> second =
            harness.editor.modeling().createBox(
                    quader::QuaderBounds{.min = {4.0f, -1.0f, -1.0f}, .max = {6.0f, 1.0f, 1.0f}},
                    "Delete Route Second");
    ASSERT_TRUE(second.ok()) << second.errorMessage();
    ASSERT_TRUE(harness.editor.selection().selectObject(first).ok());

    const quader::QuaderApiVoidResult deleted =
            harness.editor.commands().execute(quader::QuaderCommand::DeleteSelection);
    ASSERT_TRUE(deleted.ok()) << deleted.errorMessage();
    EXPECT_TRUE(deleted.changed());
    EXPECT_FALSE(harness.editor.query().entity(first.entity).has_value());
    EXPECT_TRUE(harness.editor.query().entity(second.value().entity).has_value());
    EXPECT_EQ(harness.editor.query().history().undoLabel, "Delete Selection");

    const quader::QuaderApiVoidResult undo = harness.editor.history().undo();
    ASSERT_TRUE(undo.ok()) << undo.errorMessage();
    EXPECT_TRUE(harness.editor.query().entity(first.entity).has_value());
    EXPECT_TRUE(harness.editor.query().entity(second.value().entity).has_value());
}

TEST(EditorApiTests, RightClickPickOnUnselectedObjectChangesSelectionBeforeContextDelete) {
    CommandTestHarness harness;
    const quader::QuaderModelingObjectRef left = createSelectedBox(harness.editor, "Context Left");
    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> right =
            harness.editor.modeling().createBox(
                    quader::QuaderBounds{.min = {4.0f, -1.0f, -1.0f}, .max = {6.0f, 1.0f, 1.0f}},
                    "Context Right");
    ASSERT_TRUE(right.ok()) << right.errorMessage();
    ASSERT_TRUE(harness.editor.selection().selectObject(left).ok());

    quader::QuaderPickSelectRequest rightClickPick;
    rightClickPick.ray = {
            .origin = {5.0f, 0.0f, 5.0f},
            .direction = {0.0f, 0.0f, -1.0f},
    };
    rightClickPick.preserveSelectionOnMiss = true;
    rightClickPick.preserveSelectionOnSelectedHit = true;
    const quader::QuaderApiResult<quader::QuaderSelectionSummary> picked =
            harness.editor.selection().pickSelect(rightClickPick);
    ASSERT_TRUE(picked.ok()) << picked.errorMessage();
    EXPECT_EQ(picked.value().activeObjectId, right.value().modelingObjectId);

    const quader::QuaderApiVoidResult deleted = harness.editor.commands().execute(quader::QuaderCommand::DeleteSelection);
    ASSERT_TRUE(deleted.ok()) << deleted.errorMessage();
    EXPECT_TRUE(harness.editor.query().entity(left.entity).has_value());
    EXPECT_FALSE(harness.editor.query().entity(right.value().entity).has_value());
}

TEST(EditorApiTests, BoxSelectApiSelectsContainedObjectByViewportRectangle) {
    CommandTestHarness harness;
    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> left =
            harness.editor.modeling().createBox(
                    quader::QuaderBounds{.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
                    "Box Select Left");
    ASSERT_TRUE(left.ok()) << left.errorMessage();
    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> right =
            harness.editor.modeling().createBox(
                    quader::QuaderBounds{.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
                    "Box Select Right");
    ASSERT_TRUE(right.ok()) << right.errorMessage();
    ASSERT_TRUE(harness.editor.scene().setTransform(
            left.value().entity,
            quader::QuaderTransform{.position = {-20.0f, 0.0f, 0.0f}}).ok());
    ASSERT_TRUE(harness.editor.scene().setTransform(
            right.value().entity,
            quader::QuaderTransform{.position = {20.0f, 0.0f, 0.0f}}).ok());
    ASSERT_TRUE(harness.editor.selection().setMode(quader::QuaderSelectionMode::Object).ok());
    ASSERT_TRUE(harness.editor.selection().clear().ok());

    quader::QuaderBoxSelectRequest request;
    request.camera = apiOrthographicCamera();
    request.start = {25.0f, 45.0f};
    request.end = {35.0f, 55.0f};
    request.edit = quader::QuaderSelectionEdit::Replace;
    request.rule = quader::QuaderBoxSelectionRule::Contain;
    const quader::QuaderApiResult<quader::QuaderSelectionSummary> selected =
            harness.editor.selection().boxSelect(request);

    ASSERT_TRUE(selected.ok()) << selected.errorMessage();
    EXPECT_EQ(selected.value().mode, quader::QuaderSelectionMode::Object);
    EXPECT_EQ(selected.value().selectedObjectCount, 1u);
    EXPECT_EQ(selectedEntities(harness.editor), std::vector<quader::EntityId>{left.value().entity});
}

TEST(EditorApiTests, BoxSelectApiTogglesObjectSelection) {
    CommandTestHarness harness;
    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> left =
            harness.editor.modeling().createBox(
                    quader::QuaderBounds{.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
                    "Box Toggle Left");
    ASSERT_TRUE(left.ok()) << left.errorMessage();
    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> right =
            harness.editor.modeling().createBox(
                    quader::QuaderBounds{.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
                    "Box Toggle Right");
    ASSERT_TRUE(right.ok()) << right.errorMessage();
    ASSERT_TRUE(harness.editor.scene().setTransform(
            left.value().entity,
            quader::QuaderTransform{.position = {-20.0f, 0.0f, 0.0f}}).ok());
    ASSERT_TRUE(harness.editor.scene().setTransform(
            right.value().entity,
            quader::QuaderTransform{.position = {20.0f, 0.0f, 0.0f}}).ok());
    ASSERT_TRUE(harness.editor.selection().setMode(quader::QuaderSelectionMode::Object).ok());
    ASSERT_TRUE(harness.editor.selection().selectObject(left.value()).ok());

    quader::QuaderBoxSelectRequest request;
    request.camera = apiOrthographicCamera();
    request.start = {25.0f, 45.0f};
    request.end = {35.0f, 55.0f};
    request.edit = quader::QuaderSelectionEdit::Toggle;
    request.rule = quader::QuaderBoxSelectionRule::Contain;
    const quader::QuaderApiResult<quader::QuaderSelectionSummary> removed =
            harness.editor.selection().boxSelect(request);

    ASSERT_TRUE(removed.ok()) << removed.errorMessage();
    EXPECT_EQ(removed.value().mode, quader::QuaderSelectionMode::Object);
    EXPECT_EQ(removed.value().selectedObjectCount, 0u);
    EXPECT_TRUE(selectedEntities(harness.editor).empty());
}

TEST(EditorApiTests, BoxSelectApiPreservesActiveFaceFromBaseline) {
    CommandTestHarness harness;
    const quader::QuaderModelingObjectRef object = createSelectedBox(harness.editor, "Face Baseline Box");
    ASSERT_TRUE(harness.editor.selection().setMode(quader::QuaderSelectionMode::Face).ok());
    ASSERT_TRUE(harness.editor.selection().selectFace(object, 1).ok());
    quader::QuaderSelectionSummary summary = harness.editor.selection().summary();
    ASSERT_EQ(summary.mode, quader::QuaderSelectionMode::Face);
    ASSERT_EQ(summary.activeFaceId, 1);
    ASSERT_EQ(summary.selectedFaceCount, 1u);

    quader::QuaderBoxSelectRequest request;
    request.camera = apiOrthographicCamera();
    request.start = {0.0f, 0.0f};
    request.end = {1.0f, 1.0f};
    request.edit = quader::QuaderSelectionEdit::Add;
    request.rule = quader::QuaderBoxSelectionRule::Contain;
    const quader::QuaderApiResult<quader::QuaderSelectionSummary> selected =
            harness.editor.selection().boxSelect(request);

    ASSERT_TRUE(selected.ok()) << selected.errorMessage();
    EXPECT_EQ(selected.value().mode, quader::QuaderSelectionMode::Face);
    EXPECT_EQ(selected.value().activeKind, quader::QuaderActiveSelectionKind::Face);
    EXPECT_EQ(selected.value().activeFaceId, 1);
    EXPECT_EQ(selected.value().selectedFaceCount, 1u);
}

TEST(EditorApiTests, GroupModeBoxSelectsContainingGroupByViewportRectangle) {
    CommandTestHarness harness;
    const quader::QuaderModelingObjectRef left = createSelectedBox(harness.editor, "Box Group Left");
    ASSERT_TRUE(harness.editor.scene().setTransform(
            left.entity,
            quader::QuaderTransform{.position = {-20.0f, 0.0f, 0.0f}}).ok());
    const quader::QuaderApiResult<quader::QuaderEntityRef> leftGroup =
            harness.editor.scene().groupSelection("Box Group A");
    ASSERT_TRUE(leftGroup.ok()) << leftGroup.errorMessage();

    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> right =
            harness.editor.modeling().createBox(
                    quader::QuaderBounds{.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
                    "Box Group Right");
    ASSERT_TRUE(right.ok()) << right.errorMessage();
    ASSERT_TRUE(harness.editor.scene().setTransform(
            right.value().entity,
            quader::QuaderTransform{.position = {20.0f, 0.0f, 0.0f}}).ok());
    ASSERT_TRUE(harness.editor.selection().selectObject(right.value()).ok());
    const quader::QuaderApiResult<quader::QuaderEntityRef> rightGroup =
            harness.editor.scene().groupSelection("Box Group B");
    ASSERT_TRUE(rightGroup.ok()) << rightGroup.errorMessage();

    ASSERT_TRUE(harness.editor.selection().setMode(quader::QuaderSelectionMode::Group).ok());
    ASSERT_TRUE(harness.editor.selection().clear().ok());
    quader::QuaderBoxSelectRequest request;
    request.camera = apiOrthographicCamera();
    request.start = {25.0f, 45.0f};
    request.end = {35.0f, 55.0f};
    request.edit = quader::QuaderSelectionEdit::Replace;
    request.rule = quader::QuaderBoxSelectionRule::Contain;
    const quader::QuaderApiResult<quader::QuaderSelectionSummary> selected =
            harness.editor.selection().boxSelect(request);

    ASSERT_TRUE(selected.ok()) << selected.errorMessage();
    EXPECT_EQ(selected.value().mode, quader::QuaderSelectionMode::Group);
    EXPECT_EQ(selected.value().selectedObjectCount, 1u);
    const std::optional<quader::QuaderEntitySnapshot> leftGroupSnapshot =
            entityById(harness.editor, leftGroup.value().id);
    const std::optional<quader::QuaderEntitySnapshot> rightGroupSnapshot =
            entityById(harness.editor, rightGroup.value().id);
    ASSERT_TRUE(leftGroupSnapshot.has_value());
    ASSERT_TRUE(rightGroupSnapshot.has_value());
    EXPECT_TRUE(leftGroupSnapshot->selected);
    EXPECT_TRUE(leftGroupSnapshot->active);
    EXPECT_FALSE(rightGroupSnapshot->selected);
}

TEST(EditorApiTests, RightClickPickWithExistingFaceSelectionPreservesComponentSelectionBeforeContextDelete) {
    CommandTestHarness harness;
    const quader::QuaderModelingObjectRef left = createSelectedBox(harness.editor, "Context Face Left");
    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> right =
            harness.editor.modeling().createBox(
                    quader::QuaderBounds{.min = {4.0f, -1.0f, -1.0f}, .max = {6.0f, 1.0f, 1.0f}},
                    "Context Face Right");
    ASSERT_TRUE(right.ok()) << right.errorMessage();
    ASSERT_TRUE(harness.editor.selection().setMode(quader::QuaderSelectionMode::Face).ok());
    quader::QuaderPickSelectRequest pickLeftFront;
    pickLeftFront.ray = {
            .origin = {0.0f, 0.0f, 5.0f},
            .direction = {0.0f, 0.0f, -1.0f},
    };
    const quader::QuaderApiResult<quader::QuaderSelectionSummary> pickedLeftFront =
            harness.editor.selection().pickSelect(pickLeftFront);
    ASSERT_TRUE(pickedLeftFront.ok()) << pickedLeftFront.errorMessage();
    ASSERT_EQ(pickedLeftFront.value().activeObjectId, left.modelingObjectId);
    ASSERT_EQ(pickedLeftFront.value().selectedFaceCount, 1u);

    quader::QuaderPickSelectRequest pickLeftBack;
    pickLeftBack.ray = {
            .origin = {0.0f, 0.0f, -5.0f},
            .direction = {0.0f, 0.0f, 1.0f},
    };
    pickLeftBack.shiftPressed = true;
    const quader::QuaderApiResult<quader::QuaderSelectionSummary> pickedLeftBack =
            harness.editor.selection().pickSelect(pickLeftBack);
    ASSERT_TRUE(pickedLeftBack.ok()) << pickedLeftBack.errorMessage();
    ASSERT_EQ(pickedLeftBack.value().activeObjectId, left.modelingObjectId);
    ASSERT_EQ(pickedLeftBack.value().selectedFaceCount, 2u);
    const int activeFaceBeforeContext = pickedLeftBack.value().activeFaceId;
    const std::optional<quader::QuaderMeshAsset> leftBefore = harness.editor.query().compiledObjectMesh(left);
    ASSERT_TRUE(leftBefore.has_value());
    const std::optional<quader::QuaderMeshAsset> rightBefore = harness.editor.query().compiledObjectMesh(right.value());
    ASSERT_TRUE(rightBefore.has_value());

    quader::QuaderPickSelectRequest rightClickPick;
    rightClickPick.ray = {
            .origin = {5.0f, 0.0f, 5.0f},
            .direction = {0.0f, 0.0f, -1.0f},
    };
    rightClickPick.preserveSelectionOnMiss = true;
    rightClickPick.preserveSelectionOnSelectedHit = true;
    rightClickPick.preserveExistingComponentSelectionOnHit = true;
    const quader::QuaderApiResult<quader::QuaderSelectionSummary> picked =
            harness.editor.selection().pickSelect(rightClickPick);
    ASSERT_TRUE(picked.ok()) << picked.errorMessage();
    EXPECT_EQ(picked.value().mode, quader::QuaderSelectionMode::Face);
    EXPECT_EQ(picked.value().activeObjectId, left.modelingObjectId);
    EXPECT_EQ(picked.value().activeFaceId, activeFaceBeforeContext);
    EXPECT_EQ(picked.value().selectedFaceCount, 2u);

    const quader::QuaderApiVoidResult deleted = harness.editor.commands().execute(quader::QuaderCommand::DeleteSelection);
    ASSERT_TRUE(deleted.ok()) << deleted.errorMessage();
    EXPECT_TRUE(harness.editor.query().entity(left.entity).has_value());
    EXPECT_TRUE(harness.editor.query().entity(right.value().entity).has_value());
    const std::optional<quader::QuaderMeshAsset> leftAfter = harness.editor.query().compiledObjectMesh(left);
    ASSERT_TRUE(leftAfter.has_value());
    EXPECT_LT(leftAfter->indices.size(), leftBefore->indices.size());
    const std::optional<quader::QuaderMeshAsset> rightAfter = harness.editor.query().compiledObjectMesh(right.value());
    ASSERT_TRUE(rightAfter.has_value());
    EXPECT_EQ(rightAfter->indices.size(), rightBefore->indices.size());
}

TEST(EditorApiTests, GroupModeRightClickPickSelectsGroupBeforeContextDelete) {
    CommandTestHarness harness;
    const quader::QuaderModelingObjectRef left = createSelectedBox(harness.editor, "Context Group Left");
    const quader::QuaderApiResult<quader::QuaderEntityRef> leftGroup =
            harness.editor.scene().groupSelection("Context Group A");
    ASSERT_TRUE(leftGroup.ok()) << leftGroup.errorMessage();

    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> right =
            harness.editor.modeling().createBox(
                    quader::QuaderBounds{.min = {4.0f, -1.0f, -1.0f}, .max = {6.0f, 1.0f, 1.0f}},
                    "Context Group Right");
    ASSERT_TRUE(right.ok()) << right.errorMessage();
    ASSERT_TRUE(harness.editor.selection().selectObject(right.value()).ok());
    const quader::QuaderApiResult<quader::QuaderEntityRef> rightGroup =
            harness.editor.scene().groupSelection("Context Group B");
    ASSERT_TRUE(rightGroup.ok()) << rightGroup.errorMessage();

    ASSERT_TRUE(harness.editor.selection().setMode(quader::QuaderSelectionMode::Group).ok());
    ASSERT_TRUE(harness.editor.selection().selectObject(leftGroup.value().id).ok());

    quader::QuaderPickSelectRequest rightClickPick;
    rightClickPick.ray = {
            .origin = {5.0f, 0.0f, 5.0f},
            .direction = {0.0f, 0.0f, -1.0f},
    };
    rightClickPick.preserveSelectionOnMiss = true;
    rightClickPick.preserveSelectionOnSelectedHit = true;
    const quader::QuaderApiResult<quader::QuaderSelectionSummary> picked =
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
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    const quader::QuaderApiVoidResult quadLayout =
            editor.viewports().setLayout(quader::ViewportLayoutId::FourQuad);
    EXPECT_TRUE(quadLayout.ok()) << quadLayout.errorMessage();
    EXPECT_EQ(editor.viewports().layout(), quader::ViewportLayoutId::FourQuad);
    EXPECT_EQ(editor.viewports().activePaneCount(), 4u);

    EXPECT_TRUE(editor.viewports().setGridPreset(4).ok());
    EXPECT_EQ(editor.viewports().gridPreset(), 4);
    EXPECT_TRUE(editor.viewports().setGridPreset(999).ok());
    EXPECT_EQ(editor.viewports().gridPreset(), 10);
    EXPECT_TRUE(editor.viewports().setGridPreset(-5).ok());
    EXPECT_EQ(editor.viewports().gridPreset(), 1);

    auto viewport = editor.viewports().main();
    EXPECT_TRUE(viewport.setShadingMode(quader::QuaderShadingMode::Wireframe).ok());
    EXPECT_EQ(editor.viewports().shadingMode(quader::ViewportPaneId::Perspective), quader::QuaderShadingMode::Wireframe);
    EXPECT_TRUE(viewport.setShadingMode(quader::QuaderShadingMode::Material).ok());
    EXPECT_EQ(editor.viewports().shadingMode(quader::ViewportPaneId::Perspective), quader::QuaderShadingMode::Material);
    EXPECT_TRUE(viewport.setShadingMode(quader::QuaderShadingMode::Solid).ok());
    EXPECT_EQ(editor.viewports().shadingMode(quader::ViewportPaneId::Perspective), quader::QuaderShadingMode::Solid);
    EXPECT_FALSE(viewport.setShadingMode(quader::QuaderShadingMode::XRay).ok());
    EXPECT_EQ(editor.viewports().shadingMode(quader::ViewportPaneId::Perspective), quader::QuaderShadingMode::Solid);

    EXPECT_TRUE(viewport.setView(quader::ViewportView::Top).ok());
    EXPECT_EQ(editor.viewports().view(quader::ViewportPaneId::Perspective), quader::ViewportView::Top);
    EXPECT_TRUE(editor.commands().execute(quader::QuaderCommand::ViewportViewPerspective).ok());
    EXPECT_EQ(editor.viewports().view(quader::ViewportPaneId::Perspective), quader::ViewportView::Perspective);

    EXPECT_TRUE(viewport.camera().focusOrigin().ok());
    EXPECT_FALSE(viewport.camera().frameSelection().ok());
    EXPECT_FALSE(editor.viewports().spawnWindow().ok());
}

TEST(EditorApiTests, ObjectHandlesSelectionConveniencesAndSceneBatchesUsePublicApi) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> created =
            editor.modeling().createBox(
                    quader::QuaderBounds{.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
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
    EXPECT_FALSE(duplicate.ok());
    EXPECT_NE(duplicate.errorMessage().find("modeling object"), std::string::npos);

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

    quader::QuaderTransform moved;
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
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    quader::QuaderCreateEntityArgs args;
    args.name = "Repeated Batch Entity";
    const quader::QuaderApiResult<quader::QuaderEntityRef> entity = editor.scene().createEntity(args);
    ASSERT_TRUE(entity.ok()) << entity.errorMessage();

    quader::QuaderTransform firstMove;
    firstMove.position = {1.0f, 0.0f, 0.0f};
    quader::QuaderTransform secondMove;
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

TEST(EditorApiTests, SceneBatchReparentPreservesWorldTransformAgainstPendingParentTransform) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    quader::QuaderCreateEntityArgs parentArgs;
    parentArgs.name = "Batch Reparent Parent";
    const quader::QuaderApiResult<quader::QuaderEntityRef> parent = editor.scene().createEntity(parentArgs);
    ASSERT_TRUE(parent.ok()) << parent.errorMessage();

    quader::QuaderCreateEntityArgs childArgs;
    childArgs.name = "Batch Reparent Child";
    childArgs.transform.position = {2.0f, 0.0f, 0.0f};
    const quader::QuaderApiResult<quader::QuaderEntityRef> child = editor.scene().createEntity(childArgs);
    ASSERT_TRUE(child.ok()) << child.errorMessage();

    quader::QuaderTransform parentMove;
    parentMove.position = {10.0f, 0.0f, 0.0f};
    const quader::QuaderApiVoidResult batch = editor.scene().batch("Batch Reparent Preserve")
            .setTransform(parent.value().id, parentMove)
            .reparent(child.value().id, parent.value().id, true)
            .commit();
    ASSERT_TRUE(batch.ok()) << batch.errorMessage();
    EXPECT_TRUE(batch.changed());

    std::optional<quader::QuaderEntitySnapshot> parentSnapshot = entityById(editor, parent.value().id);
    std::optional<quader::QuaderEntitySnapshot> childSnapshot = entityById(editor, child.value().id);
    ASSERT_TRUE(parentSnapshot.has_value());
    ASSERT_TRUE(childSnapshot.has_value());
    EXPECT_EQ(childSnapshot->parent, parent.value().id);
    EXPECT_NEAR(parentSnapshot->transform.position.x, 10.0f, 0.001f);
    EXPECT_NEAR(childSnapshot->transform.position.x, -8.0f, 0.001f);

    ASSERT_TRUE(editor.history().undo().ok());
    parentSnapshot = entityById(editor, parent.value().id);
    childSnapshot = entityById(editor, child.value().id);
    ASSERT_TRUE(parentSnapshot.has_value());
    ASSERT_TRUE(childSnapshot.has_value());
    EXPECT_EQ(childSnapshot->parent, quader::InvalidEntityId);
    EXPECT_NEAR(parentSnapshot->transform.position.x, 0.0f, 0.001f);
    EXPECT_NEAR(childSnapshot->transform.position.x, 2.0f, 0.001f);

    ASSERT_TRUE(editor.history().redo().ok());
    parentSnapshot = entityById(editor, parent.value().id);
    childSnapshot = entityById(editor, child.value().id);
    ASSERT_TRUE(parentSnapshot.has_value());
    ASSERT_TRUE(childSnapshot.has_value());
    EXPECT_EQ(childSnapshot->parent, parent.value().id);
    EXPECT_NEAR(parentSnapshot->transform.position.x, 10.0f, 0.001f);
    EXPECT_NEAR(childSnapshot->transform.position.x, -8.0f, 0.001f);
}

TEST(EditorApiTests, SceneBatchReparentCanKeepLocalTransformAgainstPendingParentTransform) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    quader::QuaderCreateEntityArgs parentArgs;
    parentArgs.name = "Batch Reparent Local Parent";
    const quader::QuaderApiResult<quader::QuaderEntityRef> parent = editor.scene().createEntity(parentArgs);
    ASSERT_TRUE(parent.ok()) << parent.errorMessage();

    quader::QuaderCreateEntityArgs childArgs;
    childArgs.name = "Batch Reparent Local Child";
    childArgs.transform.position = {2.0f, 0.0f, 0.0f};
    const quader::QuaderApiResult<quader::QuaderEntityRef> child = editor.scene().createEntity(childArgs);
    ASSERT_TRUE(child.ok()) << child.errorMessage();

    quader::QuaderTransform parentMove;
    parentMove.position = {10.0f, 0.0f, 0.0f};
    const quader::QuaderApiVoidResult batch = editor.scene().batch("Batch Reparent Local")
            .setTransform(parent.value().id, parentMove)
            .reparent(child.value().id, parent.value().id, false)
            .commit();
    ASSERT_TRUE(batch.ok()) << batch.errorMessage();
    EXPECT_TRUE(batch.changed());

    std::optional<quader::QuaderEntitySnapshot> parentSnapshot = entityById(editor, parent.value().id);
    std::optional<quader::QuaderEntitySnapshot> childSnapshot = entityById(editor, child.value().id);
    ASSERT_TRUE(parentSnapshot.has_value());
    ASSERT_TRUE(childSnapshot.has_value());
    EXPECT_EQ(childSnapshot->parent, parent.value().id);
    EXPECT_NEAR(parentSnapshot->transform.position.x, 10.0f, 0.001f);
    EXPECT_NEAR(childSnapshot->transform.position.x, 2.0f, 0.001f);

    ASSERT_TRUE(editor.history().undo().ok());
    parentSnapshot = entityById(editor, parent.value().id);
    childSnapshot = entityById(editor, child.value().id);
    ASSERT_TRUE(parentSnapshot.has_value());
    ASSERT_TRUE(childSnapshot.has_value());
    EXPECT_EQ(childSnapshot->parent, quader::InvalidEntityId);
    EXPECT_NEAR(parentSnapshot->transform.position.x, 0.0f, 0.001f);
    EXPECT_NEAR(childSnapshot->transform.position.x, 2.0f, 0.001f);

    ASSERT_TRUE(editor.history().redo().ok());
    parentSnapshot = entityById(editor, parent.value().id);
    childSnapshot = entityById(editor, child.value().id);
    ASSERT_TRUE(parentSnapshot.has_value());
    ASSERT_TRUE(childSnapshot.has_value());
    EXPECT_EQ(childSnapshot->parent, parent.value().id);
    EXPECT_NEAR(parentSnapshot->transform.position.x, 10.0f, 0.001f);
    EXPECT_NEAR(childSnapshot->transform.position.x, 2.0f, 0.001f);
}

TEST(EditorApiTests, SelectionApiExposesTypedTargetsThroughPublicQueries) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> first =
            editor.modeling().createBox(
                    quader::QuaderBounds{.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
                    "Selection First");
    ASSERT_TRUE(first.ok()) << first.errorMessage();
    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> second =
            editor.modeling().createBox(
                    quader::QuaderBounds{.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
                    "Selection Second");
    ASSERT_TRUE(second.ok()) << second.errorMessage();

    ASSERT_TRUE(editor.selection().selectObject(first.value()).ok());
    quader::QuaderSelectionSummary summary = editor.modeling().selectionSummary();
    EXPECT_EQ(summary.mode, quader::QuaderSelectionMode::Object);
    EXPECT_EQ(summary.activeObjectId, first.value().modelingObjectId);
    const std::optional<quader::QuaderEntitySnapshot> firstSnapshot = entityById(editor, first.value().entity);
    ASSERT_TRUE(firstSnapshot.has_value());
    EXPECT_TRUE(firstSnapshot->active);

    ASSERT_TRUE(editor.selection().selectFace(first.value(), 1).ok());
    summary = editor.modeling().selectionSummary();
    EXPECT_EQ(summary.mode, quader::QuaderSelectionMode::Face);
    EXPECT_EQ(summary.activeObjectId, first.value().modelingObjectId);
    EXPECT_EQ(summary.activeFaceId, 1);
    EXPECT_EQ(summary.selectedFaceCount, 1u);

    ASSERT_TRUE(editor.selection().setMode(quader::QuaderSelectionMode::Object).ok());
    ASSERT_TRUE(editor.selection().selectObjects({first.value().entity, second.value().entity}, second.value().entity).ok());
    const quader::QuaderApiResult<quader::QuaderEntityRef> group = editor.scene().groupSelection("Selection Group");
    ASSERT_TRUE(group.ok()) << group.errorMessage();
    ASSERT_TRUE(editor.selection().setMode(quader::QuaderSelectionMode::Group).ok());
    summary = editor.modeling().selectionSummary();
    EXPECT_EQ(summary.mode, quader::QuaderSelectionMode::Group);
    const std::optional<quader::QuaderEntitySnapshot> groupSnapshot = entityById(editor, group.value().id);
    ASSERT_TRUE(groupSnapshot.has_value());
    EXPECT_TRUE(groupSnapshot->active);
    EXPECT_TRUE(groupSnapshot->selected);
}

TEST(EditorApiTests, MixedModelingAndSceneHistoryReplaysChronologicallyThroughPublicHistory) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

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

TEST(EditorApiTests, AppCommandsAndDirectOperationsShareChronologicalUndoStack) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    const quader::QuaderModelingObjectRef original = createSelectedBox(editor, "Chronology Command Box");

    const quader::QuaderApiVoidResult duplicated = editor.commands().execute(
            editor.commands().commandIdForModelingAction("duplicate_meshes"));
    ASSERT_TRUE(duplicated.ok()) << duplicated.errorMessage();
    ASSERT_EQ(editor.query().entities().size(), 2u);
    EXPECT_EQ(editor.query().history().undoLabel, "Duplicate Meshes");

    quader::QuaderTransform moved;
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
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    quader::QuaderCreateEntityArgs parentArgs;
    parentArgs.name = "Duplicate State Parent";
    parentArgs.kind = quader::QuaderEntityKind::Empty;
    const quader::QuaderApiResult<quader::QuaderEntityRef> parent = editor.scene().createEntity(parentArgs);
    ASSERT_TRUE(parent.ok()) << parent.errorMessage();

    const quader::QuaderModelingObjectRef source = createSelectedBox(editor, "Duplicate State Source");
    ASSERT_TRUE(editor.scene().reparent(source.entity, parent.value().id, false).ok());

    quader::QuaderTransform transform;
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

TEST(EditorApiTests, DuplicateMeshesActionCommandPreservesSceneOnlyDuplicateState) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    quader::QuaderCreateEntityArgs parentArgs;
    parentArgs.name = "Duplicate Action Parent";
    parentArgs.kind = quader::QuaderEntityKind::Empty;
    const quader::QuaderApiResult<quader::QuaderEntityRef> parent = editor.scene().createEntity(parentArgs);
    ASSERT_TRUE(parent.ok()) << parent.errorMessage();

    const quader::QuaderModelingObjectRef source = createSelectedBox(editor, "Duplicate Action Source");
    ASSERT_TRUE(editor.scene().reparent(source.entity, parent.value().id, false).ok());

    quader::QuaderTransform transform;
    transform.position = {4.0f, 5.0f, 6.0f};
    transform.rotationEuler = {0.5f, 0.25f, 1.0f};
    ASSERT_TRUE(editor.scene().setTransform(source.entity, transform).ok());
    ASSERT_TRUE(editor.scene().setVisible(source.entity, false).ok());
    ASSERT_TRUE(editor.scene().setLocked(source.entity, true).ok());

    const quader::QuaderApiResult<quader::QuaderAssetRef> material =
            editor.assets().addMaterial(quader::QuaderMaterialArgs{.name = "Duplicate Action Material"});
    ASSERT_TRUE(material.ok()) << material.errorMessage();
    ASSERT_TRUE(editor.assets().assignMaterialToEntity(source.entity, material.value().id).ok());

    ASSERT_TRUE(editor.selection().selectObject(source).ok());
    const quader::QuaderApiVoidResult duplicated =
            editor.commands().execute(editor.commands().commandIdForModelingAction("duplicate_meshes"));
    ASSERT_TRUE(duplicated.ok()) << duplicated.errorMessage();

    const std::vector<quader::EntityId> selected = selectedEntities(editor);
    ASSERT_EQ(selected.size(), 1u);
    const quader::EntityId duplicateEntity = selected.front();
    EXPECT_NE(duplicateEntity, source.entity);
    const std::optional<quader::QuaderEntitySnapshot> duplicateSnapshot = entityById(editor, duplicateEntity);
    ASSERT_TRUE(duplicateSnapshot.has_value());
    EXPECT_EQ(duplicateSnapshot->parent, parent.value().id);
    EXPECT_NEAR(duplicateSnapshot->transform.position.x, 4.0f, 0.001f);
    EXPECT_NEAR(duplicateSnapshot->transform.rotationEuler.z, 1.0f, 0.001f);
    EXPECT_FALSE(duplicateSnapshot->visible);
    EXPECT_TRUE(duplicateSnapshot->locked);
    EXPECT_TRUE(duplicateSnapshot->hasMaterial);
    EXPECT_EQ(duplicateSnapshot->material, material.value().id);
    EXPECT_TRUE(duplicateSnapshot->hasMesh);
    EXPECT_NE(duplicateSnapshot->mesh, source.mesh);
}

TEST(EditorApiTests, PublicNoopOperationsPreserveRedoUntilChangedAppCommand) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

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
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    quader::QuaderMaterialAsset invalidMaterial;
    invalidMaterial.name = "Dangling Texture Material";
    invalidMaterial.textureSlots.emplace("baseColor", 9000u);
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

    quader::QuaderMaterialAsset unsupportedMaterial;
    unsupportedMaterial.name = "Unsupported Texture Slot Material";
    unsupportedMaterial.textureSlots.emplace("normalTexture", 9000u);
    const quader::QuaderApiResult<quader::QuaderAssetRef> unsupportedAdded =
            editor.assets().addMaterial(unsupportedMaterial);
    EXPECT_FALSE(unsupportedAdded.ok());
    EXPECT_EQ(unsupportedAdded.errorMessage(), "Unsupported material texture slot");

    quader::QuaderTextureAsset texture;
    texture.name = "Aliased Base Color";
    texture.usage = quader::QuaderTextureUsage::BaseColor;
    const quader::QuaderApiResult<quader::QuaderAssetRef> textureRef =
            editor.assets().addTexture(std::move(texture));
    ASSERT_TRUE(textureRef.ok()) << textureRef.errorMessage();

    const quader::QuaderApiVoidResult aliasedSlot =
            editor.assets().setMaterialTexture(validMaterial.value().id, "baseColor", textureRef.value().id);
    ASSERT_TRUE(aliasedSlot.ok()) << aliasedSlot.errorMessage();

    const quader::QuaderApiVoidResult unsupportedSlot =
            editor.assets().setMaterialTexture(validMaterial.value().id, "normalTexture", textureRef.value().id);
    EXPECT_FALSE(unsupportedSlot.ok());
    EXPECT_EQ(unsupportedSlot.errorMessage(), "Unsupported material texture slot");

    const quader::QuaderModelingObjectRef box = createSelectedBox(editor, "Dangling Mesh Component Box");
    quader::QuaderMeshComponent invalidMesh;
    invalidMesh.mesh = quader::InvalidAssetId;
    const quader::QuaderApiVoidResult invalidMeshResult =
            editor.scene().setMeshComponent(box.entity, invalidMesh);
    EXPECT_FALSE(invalidMeshResult.ok());
    EXPECT_EQ(invalidMeshResult.errorMessage(), "Mesh asset is invalid");

    quader::QuaderMeshComponent missingMesh;
    missingMesh.mesh = 9100u;
    const quader::QuaderApiVoidResult missingMeshResult =
            editor.scene().setMeshComponent(box.entity, missingMesh);
    EXPECT_FALSE(missingMeshResult.ok());
    EXPECT_EQ(missingMeshResult.errorMessage(), "Mesh asset does not exist");
}

TEST(EditorApiTests, DirectApiStatusesUseFullRevisionStamp) {
    CommandTestHarness harness;
    quader::QuaderEditorApi& editor = harness.editor;

    const quader::QuaderRevisionStamp initial = editor.revisions();
    const quader::QuaderApiVoidResult projectUnsupported = editor.project().save();
    EXPECT_FALSE(projectUnsupported.ok());
    EXPECT_EQ(projectUnsupported.revisions.projectRevision, initial.projectRevision);
    EXPECT_EQ(projectUnsupported.revisions.sceneRevision, initial.sceneRevision);
    EXPECT_EQ(projectUnsupported.revisions.assetRevision, initial.assetRevision);
    EXPECT_EQ(projectUnsupported.revisions.modelingContentRevision, initial.modelingContentRevision);
    EXPECT_EQ(projectUnsupported.revisions.modelingSelectionRevision, initial.modelingSelectionRevision);

    const quader::QuaderApiVoidResult unknownSetting = editor.tools().setSetting("edge_bevel", "__missing__", 1.0);
    EXPECT_FALSE(unknownSetting.ok());
    EXPECT_EQ(unknownSetting.mutationKind, quader::QuaderMutationKind::None);
    EXPECT_EQ(unknownSetting.revisions.modelingContentRevision, editor.revisions().modelingContentRevision);

    const quader::QuaderApiVoidResult updatedSetting = editor.tools().setSetting("edge_bevel", "width", 0.25);
    EXPECT_TRUE(updatedSetting.ok()) << updatedSetting.errorMessage();
    EXPECT_EQ(updatedSetting.mutationKind, quader::QuaderMutationKind::Modeling);
    EXPECT_EQ(updatedSetting.revisions.modelingContentRevision, editor.revisions().modelingContentRevision);
    EXPECT_EQ(updatedSetting.revisions.modelingSelectionRevision, editor.revisions().modelingSelectionRevision);
}

TEST(EditorApiTests, PublicHistoryUndoRedoMatchesCommandApiStatus) {
    CommandTestHarness directHistory;
    createSelectedBox(directHistory.editor, "Direct History Box");
    const quader::QuaderApiVoidResult directUndo = directHistory.editor.history().undo();
    ASSERT_TRUE(directUndo.ok()) << directUndo.errorMessage();
    EXPECT_TRUE(directUndo.changed());
    EXPECT_FALSE(entityNamed(directHistory.editor, "Direct History Box").has_value());

    CommandTestHarness commandHistory;
    createSelectedBox(commandHistory.editor, "Command History Box");
    const quader::QuaderApiVoidResult commandUndo =
            commandHistory.editor.commands().execute(quader::QuaderCommand::HistoryUndo);
    expectSameApiStatus(commandUndo, directUndo);
    EXPECT_FALSE(entityNamed(commandHistory.editor, "Command History Box").has_value());

    const quader::QuaderApiVoidResult directRedo = directHistory.editor.history().redo();
    ASSERT_TRUE(directRedo.ok()) << directRedo.errorMessage();
    EXPECT_TRUE(directRedo.changed());
    EXPECT_TRUE(entityNamed(directHistory.editor, "Direct History Box").has_value());

    const quader::QuaderApiVoidResult commandRedo =
            commandHistory.editor.commands().execute(quader::QuaderCommand::HistoryRedo);
    expectSameApiStatus(commandRedo, directRedo);
    EXPECT_TRUE(entityNamed(commandHistory.editor, "Command History Box").has_value());
}

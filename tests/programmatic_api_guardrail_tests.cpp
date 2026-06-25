////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#ifndef QUADER_SOURCE_DIR
#define QUADER_SOURCE_DIR "."
#endif

namespace {

std::filesystem::path sourceRoot() {
    return std::filesystem::path(QUADER_SOURCE_DIR);
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    EXPECT_TRUE(file.is_open()) << path.string();
    if (!file.is_open()) {
        return {};
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::vector<std::filesystem::path> filesWithExtension(const std::filesystem::path& root, std::string_view extension) {
    std::vector<std::filesystem::path> files;
    if (!std::filesystem::exists(root)) {
        return files;
    }

    for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file() && entry.path().extension() == extension) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::vector<std::filesystem::path> sourceFilesUnder(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> files;
    for (const std::string_view extension : {std::string_view{".h"}, std::string_view{".cpp"}}) {
        std::vector<std::filesystem::path> withExtension = filesWithExtension(root, extension);
        files.insert(files.end(), withExtension.begin(), withExtension.end());
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::vector<std::filesystem::path> publicDocAndExampleFilesUnder(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> files = sourceFilesUnder(root);
    for (const std::string_view extension : {std::string_view{".md"}, std::string_view{".lua"}}) {
        std::vector<std::filesystem::path> withExtension = filesWithExtension(root, extension);
        files.insert(files.end(), withExtension.begin(), withExtension.end());
    }
    std::sort(files.begin(), files.end());
    return files;
}

bool isInternalApiHeader(const std::filesystem::path& path) {
    const std::string relative = std::filesystem::relative(path, sourceRoot()).generic_string();
    return relative == "src/api/QuaderApiMutationRunner.h" ||
            relative == "src/api/QuaderApiTypeConversions.h" ||
            relative == "src/api/QuaderPolygonDocumentInternalAccess.h" ||
            relative == "src/editor/viewport/QuaderViewportApiRuntimeAccess.h";
}

std::vector<std::filesystem::path> publicApiHeaderFiles() {
    std::vector<std::filesystem::path> headers;
    for (const std::filesystem::path& file : filesWithExtension(sourceRoot() / "src/api", ".h")) {
        if (!isInternalApiHeader(file)) {
            headers.push_back(file);
        }
    }
    return headers;
}

std::string normalizedRelative(const std::filesystem::path& path) {
    return std::filesystem::relative(path, sourceRoot()).generic_string();
}

bool containsAny(const std::string& source, const std::vector<std::string_view>& needles) {
    return std::any_of(needles.begin(), needles.end(), [&](std::string_view needle) {
        return source.find(needle) != std::string::npos;
    });
}

std::vector<std::string> linesContaining(const std::string& source, const std::vector<std::string_view>& needles) {
    std::vector<std::string> matches;
    std::string line;
    for (const char ch : source) {
        if (ch == '\n') {
            if (containsAny(line, needles)) {
                matches.push_back(line);
            }
            line.clear();
        } else if (ch != '\r') {
            line.push_back(ch);
        }
    }
    if (!line.empty() && containsAny(line, needles)) {
        matches.push_back(line);
    }
    return matches;
}

bool allowedRawModelingMutation(std::string_view relative, std::string_view line) {
    (void)relative;
    (void)line;
    return false;
}

bool endsWith(std::string_view value, std::string_view suffix) {
    return value.size() >= suffix.size() &&
            value.substr(value.size() - suffix.size()) == suffix;
}

bool isPublicBehaviorTestName(std::string_view filename) {
    return endsWith(filename, "_api_tests.cpp") ||
            endsWith(filename, "_public_api_tests.cpp") ||
            (filename.find("_behavior") != std::string_view::npos && endsWith(filename, "_tests.cpp"));
}

bool isWhitelistedDomainApiTest(std::string_view relative) {
    return relative == "tests/modeling_domain/qdr_document_model_api_tests.cpp";
}

std::vector<std::filesystem::path> publicBehaviorTestFiles() {
    std::vector<std::filesystem::path> files;
    for (const std::filesystem::path& file : filesWithExtension(sourceRoot() / "tests", ".cpp")) {
        const std::string relative = normalizedRelative(file);
        if (isPublicBehaviorTestName(file.filename().generic_string()) && !isWhitelistedDomainApiTest(relative)) {
            files.push_back(file);
        }
    }
    return files;
}

bool allowedPublicBehaviorHarnessUse(std::string_view relative, std::string_view line) {
    if (relative == "tests/editor_api_tests.cpp") {
        return line.find("\"scene/document/ProjectSession.h\"") != std::string_view::npos ||
                line.find("\"editor/modeling/services/ModelingSessionService.h\"") != std::string_view::npos ||
                line.find("session.document().rename(") != std::string_view::npos ||
                line.find("session.markClean(") != std::string_view::npos ||
                line.find("session.modeling().createBoxFromBounds(") != std::string_view::npos;
    }
    return false;
}

} // namespace

TEST(ProgrammaticApiGuardrailTests, PublicApiHeadersAvoidRuntimeAndSdkInternals) {
    for (const std::filesystem::path& file : publicApiHeaderFiles()) {
        const std::string source = readFile(file);
        const std::string relative = normalizedRelative(file);

        EXPECT_EQ(source.find("editor/modeling/runtime/session.h"), std::string::npos) << relative;
        EXPECT_FALSE(containsAny(source, {
                             "<filament/",
                             "\"filament/",
                             "<RmlUi/",
                             "\"RmlUi/",
                             "Rml::",
                             "<SDL",
                             "\"SDL",
                             "<entt/",
                             "\"entt/",
                     }))
                << relative;
    }
}

TEST(ProgrammaticApiGuardrailTests, PublicApiValueContractHeadersAvoidInternalOwnerHeaders) {
    for (const std::string_view relative : {
                 std::string_view{"src/api/QuaderApiTypes.h"},
                 std::string_view{"src/api/QuaderApiSnapshot.h"},
         }) {
        const std::string source = readFile(sourceRoot() / std::string(relative));

        EXPECT_FALSE(containsAny(source, {
                             "\"editor/",
                             "\"scene/",
                             "\"assets/",
                             "\"render/",
                             "\"viewport/",
                             "\"mesh/",
                             "<mesh/",
                             "quader_poly::",
                             "quader_editor_core::",
                             "quader_io::",
                             "quader_host_contract::",
                             "TransformComponent",
                             "ModelingBounds",
                             "ModelingSelectionSummary",
                             "ModelingToolStatus",
                             "ViewportOverlayFrame",
                             "MeshAssetRevision",
                     }))
                << relative;
    }
}

TEST(ProgrammaticApiGuardrailTests, PublicApiHeadersDoNotExposeMutableInternalOwners) {
    for (const std::filesystem::path& file : publicApiHeaderFiles()) {
        const std::string source = readFile(file);
        const std::string relative = normalizedRelative(file);

        for (std::string_view forbidden : {
                     "SceneDocument&",
                     "AssetRegistry&",
                     "ViewportWorkspaceService",
                     "MeshAsset*",
                     "MaterialDocument*",
                     "TextureAsset*",
                     "entt::registry&",
                     "EditorSession&",
                     "EditorCommandRegistry",
                     "QuaderKeybindingService",
                     "\"editor/commands/EditorCommandRegistry.h\"",
                     "\"editor/input/QuaderKeybindingService.h\"",
             }) {
            EXPECT_EQ(source.find(forbidden), std::string::npos) << relative << " exposes " << forbidden;
        }
    }
}

TEST(ProgrammaticApiGuardrailTests, ModelingSessionServiceDoesNotExposeSelectionSyncAuthority) {
    const std::string header = readFile(sourceRoot() / "src/editor/modeling/services/ModelingSessionService.h");
    EXPECT_EQ(header.find("syncObjectSelectionFromScene("), std::string::npos);
    EXPECT_EQ(header.find("syncSceneSelectionFromModeling("), std::string::npos);
    EXPECT_EQ(header.find("projectSceneSelectionFromModeling("), std::string::npos);
    EXPECT_EQ(header.find("friend class EditorSelectionService;"), std::string::npos);

    const std::string source = readFile(sourceRoot() / "src/editor/modeling/services/ModelingSessionService.cpp");
    EXPECT_EQ(source.find("scene_.setSelected("), std::string::npos);
    EXPECT_NE(source.find("SelectionProjectionService"), std::string::npos);

    const std::string editorSelectionHeader =
            readFile(sourceRoot() / "src" / "editor" / "selection" / "EditorSelectionService.h");
    EXPECT_EQ(editorSelectionHeader.find("syncObjectsFromScene("), std::string::npos);
    EXPECT_EQ(editorSelectionHeader.find("syncSceneFromModeling("), std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests, ApiImplementationDoesNotBypassCommandExecutionBoundary) {
    for (const std::filesystem::path& file : sourceFilesUnder(sourceRoot() / "src/api")) {
        const std::string source = readFile(file);
        const std::string relative = normalizedRelative(file);

        for (std::string_view forbidden : {
                     "executeInstantOperation",
                     "executeContextAction",
                     "commitActiveOperationPreview",
                     "cancelActiveOperationPreview",
             }) {
            EXPECT_EQ(source.find(forbidden), std::string::npos) << relative << " calls " << forbidden;
        }
    }
}

TEST(ProgrammaticApiGuardrailTests, ApiImplementationDoesNotIncludeConcreteDocumentCommands) {
    const std::vector<std::string_view> concreteDocumentCommandHeaders = {
            "\"editor/commands/document/ApplyEntityTransformCommand.h\"",
            "\"editor/commands/document/BatchDocumentCommand.h\"",
            "\"editor/commands/document/CreateEntityCommand.h\"",
            "\"editor/commands/document/DestroyEntityCommand.h\"",
            "\"editor/commands/document/DuplicateEntityCommand.h\"",
            "\"editor/commands/document/DuplicateSelectedHierarchyCommand.h\"",
            "\"editor/commands/document/GroupSelectionCommand.h\"",
            "\"editor/commands/document/RenameEntityCommand.h\"",
            "\"editor/commands/document/ReparentEntityCommand.h\"",
            "\"editor/commands/document/SelectEntitiesCommand.h\"",
            "\"editor/commands/document/SetLockCommand.h\"",
            "\"editor/commands/document/SetMaterialCommand.h\"",
            "\"editor/commands/document/SetMeshComponentCommand.h\"",
            "\"editor/commands/document/SetVisibilityCommand.h\"",
            "\"editor/commands/document/UngroupSelectionCommand.h\"",
    };

    for (const std::filesystem::path& file : sourceFilesUnder(sourceRoot() / "src/api")) {
        const std::string source = readFile(file);
        const std::string relative = normalizedRelative(file);
        for (std::string_view header : concreteDocumentCommandHeaders) {
            EXPECT_EQ(source.find(header), std::string::npos) << relative << " includes " << header;
        }
    }
}

TEST(ProgrammaticApiGuardrailTests, PublicApiAndBehaviorDslDoNotExposeModelingActionCompatibilityRoute) {
    for (const std::filesystem::path& root : {
                 sourceRoot() / "src/api",
                 sourceRoot() / "tests/support",
         }) {
        for (const std::filesystem::path& file : sourceFilesUnder(root)) {
            const std::string source = readFile(file);
            const std::string relative = normalizedRelative(file);

            EXPECT_EQ(source.find(".modeling().executeAction("), std::string::npos) << relative;
            EXPECT_EQ(source.find("QuaderModelingApi::executeAction"), std::string::npos) << relative;
            EXPECT_EQ(source.find("QuaderScript& executeAction"), std::string::npos) << relative;
            EXPECT_EQ(source.find("BehaviorWhen& executeAction"), std::string::npos) << relative;
        }
    }
}

TEST(ProgrammaticApiGuardrailTests, CommandLayerDoesNotExposeGenericModelingActionExecutionRoute) {
    for (const std::filesystem::path& file : {
                 sourceRoot() / "src/api/QuaderApiMutationRunner.h",
                 sourceRoot() / "src/api/QuaderApiMutationRunner.cpp",
                 sourceRoot() / "src/editor/commands/ModelingCommandExecutionService.h",
                 sourceRoot() / "src/editor/commands/ModelingCommandExecutionService.cpp",
         }) {
        const std::string source = readFile(file);
        const std::string relative = normalizedRelative(file);

        EXPECT_EQ(source.find("runTrackedModelingAction"), std::string::npos) << relative;
        EXPECT_EQ(source.find("executeAction("), std::string::npos) << relative;
        EXPECT_EQ(source.find("executeActionUntracked("), std::string::npos) << relative;
    }
}

TEST(ProgrammaticApiGuardrailTests, SelectionCommandToolsPreferEditorSelectionService) {
    const std::unordered_map<std::string, std::string_view> expectedRoutes{
            {"src/editor/commands/modeling/ClearSelectionTool.cpp", "context.selection->clear()"},
            {"src/editor/commands/modeling/SelectAllTool.cpp", "context.selection->selectAll()"},
            {"src/editor/commands/modeling/InvertSelectionTool.cpp", "context.selection->invert()"},
    };

    for (const auto& [relative, expectedRoute] : expectedRoutes) {
        const std::string source = readFile(sourceRoot() / relative);
        EXPECT_NE(source.find("context.selection != nullptr"), std::string::npos) << relative;
        EXPECT_NE(source.find(expectedRoute), std::string::npos) << relative;
    }
}

TEST(ProgrammaticApiGuardrailTests, ApiDoesNotTreatHostContractDescriptorsAsCommands) {
    for (const std::filesystem::path& file : sourceFilesUnder(sourceRoot() / "src/api")) {
        const std::string source = readFile(file);
        const std::string relative = normalizedRelative(file);

        EXPECT_FALSE(containsAny(source, {
                             "host-contract",
                             "host_contract",
                             "quader_host_contract",
                     }))
                << relative;
    }
}

TEST(ProgrammaticApiGuardrailTests, ProductionSourcesDoNotIncludeTestSupportDsl) {
    for (const std::filesystem::path& file : sourceFilesUnder(sourceRoot() / "src")) {
        const std::string source = readFile(file);
        const std::string relative = normalizedRelative(file);

        EXPECT_FALSE(containsAny(source, {
                             "\"tests/support/",
                             "<tests/support/",
                             "\"support/BehaviorScenario.h\"",
                             "\"support/BehaviorExpect.h\"",
                             "\"support/BehaviorSnapshot.h\"",
                             "\"support/ModelingTestEvents.h\"",
                     }))
                << relative;
    }
}

TEST(ProgrammaticApiGuardrailTests, ProgrammaticTestSourcesAreListedInCMake) {
    const std::string cmake = readFile(sourceRoot() / "CMakeLists.txt");

    std::vector<std::filesystem::path> files = filesWithExtension(sourceRoot() / "tests/support", ".cpp");
    files.push_back(sourceRoot() / "tests/programmatic_api_guardrail_tests.cpp");
    files.push_back(sourceRoot() / "tests/programmatic_api_tests.cpp");
    files.push_back(sourceRoot() / "tests/programmatic_behavior_scenario_tests.cpp");

    for (const std::filesystem::path& file : files) {
        const std::string relative = normalizedRelative(file);
        EXPECT_NE(cmake.find(relative), std::string::npos) << relative;
    }
}

TEST(ProgrammaticApiGuardrailTests, UiAppAndViewportDoNotAddSceneDocumentMutations) {
    const std::vector<std::string_view> directSceneMutations = {
            "document.createEntity(",
            "document.createEmpty(",
            "document.destroyEntity(",
            "document.renameEntity(",
            "document.setTransform(",
            "document.setSelected(",
            "document.clearSelection(",
            "document.setParent(",
            "document.setVisible(",
            "document.setLocked(",
            "document.setMesh(",
            "document.setMaterial(",
            "document.setModelingObject(",
            "context.document.createEntity(",
            "context.document.createEmpty(",
            "context.document.destroyEntity(",
            "context.document.renameEntity(",
            "context.document.setTransform(",
            "context.document.setSelected(",
            "context.document.clearSelection(",
            "context.document.setParent(",
            "context.document.setVisible(",
            "context.document.setLocked(",
            "context.document.setMesh(",
            "context.document.setMaterial(",
            "context.document.setModelingObject(",
    };

    for (const std::filesystem::path& root : {
                 sourceRoot() / "src/ui",
                 sourceRoot() / "src/app",
                 sourceRoot() / "src/viewport",
                 sourceRoot() / "src/editor/tools",
         }) {
        for (const std::filesystem::path& file : filesWithExtension(root, ".cpp")) {
            const std::string source = readFile(file);
            const std::string relative = normalizedRelative(file);
            for (const std::string& line : linesContaining(source, directSceneMutations)) {
                ADD_FAILURE() << relative << ": " << line;
            }
        }
    }
}

TEST(ProgrammaticApiGuardrailTests, UiAppAndViewportDoNotAddRawModelingMutations) {
    const std::vector<std::string_view> rawModelingMutations = {
            "modeling.executeInstantOperation(",
            "modeling.executeContextAction(",
            "modeling.commitActiveOperationPreview(",
            "modeling.cancelActiveOperationPreview(",
            "modeling.beginTransformPreview(",
            "modeling.updateTransformTranslatePreview(",
            "modeling.updateTransformMatrixPreview(",
            "modeling.updateTransformExtrudePreview(",
            "modeling.updateTransformInsetPreview(",
            "modeling.commitTransformPreview(",
            "modeling.cancelTransformPreview(",
            "modeling.beginTransformDrag(",
            "modeling.updateTransformDrag(",
            "modeling.commitTransformDrag(",
            "modeling.cancelTransformDrag(",
            "modeling.createBoxFromBounds(",
            "modeling.pickSelect(",
            "modeling.setSelectionMode(",
            "modeling.syncObjectSelectionFromScene(",
            "modeling.syncSceneSelectionFromModeling(",
            "modeling.selectAll(",
            "modeling.clearSelection(",
            "modeling.deleteSelection(",
            "modeling.applyActiveMaterial(",
            "context.modeling.executeInstantOperation(",
            "context.modeling.executeContextAction(",
            "context.modeling.commitActiveOperationPreview(",
            "context.modeling.cancelActiveOperationPreview(",
            "context.modeling.beginTransformPreview(",
            "context.modeling.updateTransformTranslatePreview(",
            "context.modeling.updateTransformMatrixPreview(",
            "context.modeling.updateTransformExtrudePreview(",
            "context.modeling.updateTransformInsetPreview(",
            "context.modeling.commitTransformPreview(",
            "context.modeling.cancelTransformPreview(",
            "context.modeling.beginTransformDrag(",
            "context.modeling.updateTransformDrag(",
            "context.modeling.commitTransformDrag(",
            "context.modeling.cancelTransformDrag(",
            "context.modeling.createBoxFromBounds(",
            "context.modeling.pickSelect(",
            "context.modeling.setSelectionMode(",
            "context.modeling.syncObjectSelectionFromScene(",
            "context.modeling.syncSceneSelectionFromModeling(",
            "context.modeling.selectAll(",
            "context.modeling.clearSelection(",
            "context.modeling.deleteSelection(",
            "context.modeling.applyActiveMaterial(",
    };

    for (const std::filesystem::path& root : {sourceRoot() / "src/ui", sourceRoot() / "src/app", sourceRoot() / "src/viewport"}) {
        for (const std::filesystem::path& file : filesWithExtension(root, ".cpp")) {
            const std::string source = readFile(file);
            const std::string relative = normalizedRelative(file);
            for (const std::string& line : linesContaining(source, rawModelingMutations)) {
                EXPECT_TRUE(allowedRawModelingMutation(relative, line)) << relative << ": " << line;
            }
        }
    }
}

TEST(ProgrammaticApiGuardrailTests, ViewportRoutesDoNotExecuteFixedRawModelingCommandStrings) {
    const std::vector<std::string_view> forbiddenRawCommandRoutes = {
            "commands().execute(\"modeling.",
            "std::string command = \"modeling.\"",
            "command.append(toolId)",
            "contextMenuCommandId(std::string_view",
            "const std::string commandId = contextMenuCommandId(action.actionId)",
    };

    for (std::string_view relative : {
                 std::string_view{"src/ui/ViewportShortcutController.cpp"},
                 std::string_view{"src/ui/UISystem.cpp"},
         }) {
        const std::string source = readFile(sourceRoot() / std::string(relative));
        for (const std::string& line : linesContaining(source, forbiddenRawCommandRoutes)) {
            EXPECT_TRUE(false) << relative << ": " << line;
        }
    }
}

TEST(ProgrammaticApiGuardrailTests, UiAppAndViewportDoNotExposeRawHistoryOrDocumentCommandServices) {
    const std::vector<std::string_view> forbiddenOwnerServices = {
            "UndoRedoManager",
            "DocumentCommandService",
            "core/UndoRedoManager.h",
            "editor/commands/DocumentCommandService.h",
            "undoRedo",
            "documentCommands",
    };

    for (const std::filesystem::path& root : {sourceRoot() / "src/ui", sourceRoot() / "src/app", sourceRoot() / "src/viewport"}) {
        for (const std::filesystem::path& file : sourceFilesUnder(root)) {
            const std::string source = readFile(file);
            const std::string relative = normalizedRelative(file);
            for (const std::string& line : linesContaining(source, forbiddenOwnerServices)) {
                EXPECT_TRUE(false) << relative << ": " << line;
            }
        }
    }
}

TEST(ProgrammaticApiGuardrailTests, UiAppAndViewportDoNotUseModelingActionCompatibilityRoute) {
    const std::vector<std::string_view> forbiddenActionRoutes = {
            "editor.modeling().executeAction(",
            "context.editor.modeling().executeAction(",
            "uiContext.editor.modeling().executeAction(",
            ".modeling().executeAction(",
    };

    for (const std::filesystem::path& root : {sourceRoot() / "src/ui", sourceRoot() / "src/app", sourceRoot() / "src/viewport"}) {
        for (const std::filesystem::path& file : sourceFilesUnder(root)) {
            const std::string source = readFile(file);
            const std::string relative = normalizedRelative(file);
            for (const std::string& line : linesContaining(source, forbiddenActionRoutes)) {
                EXPECT_TRUE(false) << relative << ": " << line;
            }
        }
    }
}

TEST(ProgrammaticApiGuardrailTests, UiAppAndViewportDoNotUseSceneSelectionAsAuthority) {
    const std::vector<std::string_view> forbiddenSceneSelectionRoutes = {
            "editor.scene().select(",
            "context.editor.scene().select(",
            "uiContext.editor.scene().select(",
            ".scene().select(",
    };

    for (const std::filesystem::path& root : {sourceRoot() / "src/ui", sourceRoot() / "src/app", sourceRoot() / "src/viewport"}) {
        for (const std::filesystem::path& file : sourceFilesUnder(root)) {
            const std::string source = readFile(file);
            const std::string relative = normalizedRelative(file);
            for (const std::string& line : linesContaining(source, forbiddenSceneSelectionRoutes)) {
                EXPECT_TRUE(false) << relative << ": " << line;
            }
        }
    }
}

TEST(ProgrammaticApiGuardrailTests, PublicHistoryDoesNotFallbackToModelingHistory) {
    const std::vector<std::string_view> forbiddenSplitHistoryRoutes = {
            "ModelingHistoryCommand",
            "modeling().canUndo(",
            "modeling().canRedo(",
            "modeling().undo(",
            "modeling().redo(",
            "modeling.canUndo(",
            "modeling.canRedo(",
            "modeling.undo(",
            "modeling.redo(",
            "canUndo() ||",
            "canRedo() ||",
            "|| modeling.canUndo",
            "|| modeling.canRedo",
    };

    for (const std::filesystem::path& root : {sourceRoot() / "src/api", sourceRoot() / "src/editor"}) {
        for (const std::filesystem::path& file : sourceFilesUnder(root)) {
            const std::string source = readFile(file);
            const std::string relative = normalizedRelative(file);
            for (const std::string& line : linesContaining(source, forbiddenSplitHistoryRoutes)) {
                EXPECT_TRUE(false) << relative << ": " << line;
            }
        }
    }
}

TEST(ProgrammaticApiGuardrailTests, EditorHistoryBoundaryHidesCoreUndoRedoManager) {
    const std::vector<std::filesystem::path> editorHistoryClients = {
            sourceRoot() / "src/api/QuaderApi.cpp",
            sourceRoot() / "src/api/QuaderSceneApi.cpp",
            sourceRoot() / "src/scene/document/ProjectSession.h",
            sourceRoot() / "src/scene/document/ProjectSession.cpp",
            sourceRoot() / "src/editor/commands/DocumentCommandService.h",
            sourceRoot() / "src/editor/commands/DocumentCommandService.cpp",
            sourceRoot() / "src/editor/history/SceneTransformPreviewTransaction.h",
    };
    for (const std::filesystem::path& file : editorHistoryClients) {
        const std::string source = readFile(file);
        const std::string relative = normalizedRelative(file);
        EXPECT_EQ(source.find("UndoRedoManager"), std::string::npos) << relative;
        EXPECT_EQ(source.find("core/UndoRedoManager.h"), std::string::npos) << relative;
        EXPECT_EQ(source.find("undoRedo()"), std::string::npos) << relative;
    }

    const std::string historyHeader = readFile(sourceRoot() / "src/editor/history/EditorHistoryService.h");
    EXPECT_EQ(historyHeader.find("core/UndoRedoManager.h"), std::string::npos);
    EXPECT_EQ(historyHeader.find("undoRedo()"), std::string::npos);

    const std::string historySource = readFile(sourceRoot() / "src/editor/history/EditorHistoryService.cpp");
    EXPECT_NE(historySource.find("core/UndoRedoManager.h"), std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests, PublicBehaviorTestsUseApiOrScenarioDsl) {
    const std::vector<std::string_view> forbidden = {
            "\"editor/commands/",
            "\"editor/modeling/services/ModelingSessionService.h\"",
            "\"editor/selection/",
            "\"editor/tools/",
            "\"scene/SceneDocument.h\"",
            "\"scene/document/ProjectSession.h\"",
            "ModelingSessionService",
            "DocumentCommandService",
            "SceneDocument",
            "AssetRegistry",
            "session.scene().",
            "session.selection().",
            "session.modeling().",
            "createBoxFromBounds(",
            "executeInstantOperation",
            "executeContextAction",
    };

    for (const std::filesystem::path& file : publicBehaviorTestFiles()) {
        const std::string source = readFile(file);
        const std::string relative = normalizedRelative(file);

        for (const std::string& line : linesContaining(source, forbidden)) {
            EXPECT_TRUE(allowedPublicBehaviorHarnessUse(relative, line)) << relative << ": " << line;
        }
    }
}

TEST(ProgrammaticApiGuardrailTests, ModelingOperationTestsUseSharedRuntimeHelpers) {
    const std::vector<std::string_view> duplicatedHelpers = {
            "find_object(const quader_editor_core::EditorSession&",
            "require_object(const quader_editor_core::EditorSession&",
            "selected_face_count(const",
            "selected_vertex_count(const",
            "selected_edge_count(const",
    };

    for (const std::filesystem::path& file : filesWithExtension(sourceRoot() / "tests/modeling_domain/operation", ".cpp")) {
        const std::string source = readFile(file);
        const std::string relative = normalizedRelative(file);
        for (const std::string& line : linesContaining(source, duplicatedHelpers)) {
            ADD_FAILURE() << relative << ": move duplicated runtime test helper to editor_operation_test_helpers.h: " << line;
        }
    }
}

TEST(ProgrammaticApiGuardrailTests, PublicApiHeadersExposeAppNamedTypes) {
    const std::vector<std::string_view> absorbedNamespaces = {
            "quader_poly::",
            "quader_editor_core::",
            "quader_io::",
            "quader_host_contract::",
    };

    for (const std::filesystem::path& file : publicApiHeaderFiles()) {
        const std::string source = readFile(file);
        const std::string relative = normalizedRelative(file);
        for (const std::string& line : linesContaining(source, absorbedNamespaces)) {
            EXPECT_TRUE(false) << relative << ": " << line;
        }
    }
}

TEST(ProgrammaticApiGuardrailTests, PublicExamplesAndDocsDoNotStartFromInternalOwnerHeaders) {
    for (const std::filesystem::path& root : {sourceRoot() / "docs", sourceRoot() / "examples"}) {
        for (const std::filesystem::path& file : publicDocAndExampleFilesUnder(root)) {
            const std::string source = readFile(file);
            const std::string relative = normalizedRelative(file);

            EXPECT_FALSE(containsAny(source, {
                                 "editor/modeling/services/ModelingSessionService.h",
                                 "scene/SceneDocument.h",
                                 "assets/AssetRegistry.h",
                                 "editor/commands/DocumentCommandService.h",
                         }))
                    << relative;
        }
    }
}

TEST(ProgrammaticApiGuardrailTests, PublicExamplesStartFromEditorApiOrScriptSurface) {
    const std::string createBox = readFile(sourceRoot() / "examples/create_box.cpp");
    const std::string workflow = readFile(sourceRoot() / "examples/script_modeling_workflow.cpp");
    const std::string luaReadme = readFile(sourceRoot() / "examples/lua/README.md");

    EXPECT_NE(createBox.find("QuaderEditorApi"), std::string::npos);
    EXPECT_NE(workflow.find("QuaderEditorApi"), std::string::npos);
    EXPECT_NE(luaReadme.find("public `quader` scripting object"), std::string::npos);
    EXPECT_NE(luaReadme.find("does not ship a Lua runtime"), std::string::npos);
}

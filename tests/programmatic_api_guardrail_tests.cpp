////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "support/GuardrailFileScan.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

using quader::test::guardrail::containsAny;
using quader::test::guardrail::filesWithExtension;
using quader::test::guardrail::linesContaining;
using quader::test::guardrail::normalizedRelative;
using quader::test::guardrail::readFile;
using quader::test::guardrail::readSourceFile;
using quader::test::guardrail::sourceFilesUnder;
using quader::test::guardrail::sourceRoot;
using quader::test::guardrail::splitLines;
using quader::test::guardrail::startsWith;

/**
 * Separates public API-facing docs/examples from internal architecture and provenance docs.
 */
struct PublicDocsExamplesManifest {
    std::vector<std::filesystem::path> publicFiles;
    std::vector<std::filesystem::path> internalFiles;
};

std::vector<std::filesystem::path> docAndExampleFilesUnder(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> files;
    for (const std::string_view extension : {
                 std::string_view{".cpp"},
                 std::string_view{".lua"},
                 std::string_view{".md"},
                 std::string_view{".txt"},
         }) {
        std::vector<std::filesystem::path> withExtension = filesWithExtension(root, extension);
        files.insert(files.end(), withExtension.begin(), withExtension.end());
    }
    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end());
    return files;
}

PublicDocsExamplesManifest publicDocsExamplesManifest() {
    PublicDocsExamplesManifest manifest;
    const std::string source = readSourceFile("docs/public-docs-examples.txt");
    std::set<std::string> seen;

    for (const std::string& line : splitLines(source)) {
        if (line.empty() || startsWith(line, "#") || startsWith(line, "//")) {
            continue;
        }

        std::istringstream stream(line);
        std::string visibility;
        std::string relative;
        stream >> visibility >> relative;
        if (visibility.empty() || relative.empty()) {
            continue;
        }

        EXPECT_TRUE(visibility == "public" || visibility == "internal") << line;
        EXPECT_TRUE(seen.insert(relative).second) << "duplicate docs/examples classification: " << relative;

        if (visibility == "public") {
            manifest.publicFiles.push_back(sourceRoot() / relative);
        } else if (visibility == "internal") {
            manifest.internalFiles.push_back(sourceRoot() / relative);
        }
    }

    return manifest;
}

std::vector<std::filesystem::path> publicDocAndExampleFiles() {
    return publicDocsExamplesManifest().publicFiles;
}

bool isInternalApiHeader(const std::filesystem::path& path) {
    const std::string relative = normalizedRelative(path);
    return relative == "src/api/QuaderApiMutationRunner.h" ||
            relative == "src/api/QuaderApiSessionInternalAccess.h" ||
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

bool allowedRawModelingMutation(std::string_view relative, std::string_view line) {
    (void)relative;
    (void)line;
    return false;
}

bool isIdentifierCharacter(char value) {
    return std::isalnum(static_cast<unsigned char>(value)) != 0 || value == '_';
}

bool containsIdentifierToken(std::string_view source, std::string_view token) {
    std::size_t position = source.find(token);
    while (position != std::string_view::npos) {
        const bool leftBoundary = position == 0 || !isIdentifierCharacter(source[position - 1]);
        const std::size_t end = position + token.size();
        const bool rightBoundary = end >= source.size() || !isIdentifierCharacter(source[end]);
        if (leftBoundary && rightBoundary) {
            return true;
        }
        position = source.find(token, position + 1);
    }
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

std::vector<std::filesystem::path> publicCppExampleFiles() {
    std::vector<std::filesystem::path> examples;
    for (const std::filesystem::path& file : publicDocAndExampleFiles()) {
        if (file.extension() == ".cpp" && normalizedRelative(file).find("examples/") == 0) {
            examples.push_back(file);
        }
    }
    return examples;
}

bool isDocumentationOnlyExample(const std::string& source) {
    return source.find("QUADER_DOCUMENTATION_ONLY_EXAMPLE") != std::string::npos ||
            source.find("documentation-only") != std::string::npos;
}

bool allowedPublicBehaviorHarnessUse(std::string_view relative, std::string_view line) {
    if (relative == "tests/editor_api_tests.cpp") {
        return line.find("\"scene/document/ProjectSession.h\"") != std::string_view::npos ||
                line.find("\"editor/modeling/services/ModelingSessionService.h\"") != std::string_view::npos ||
                line.find("session.document().rename(") != std::string_view::npos ||
                line.find("session.markClean(") != std::string_view::npos ||
                line.find("session.modeling().createBoxFromBounds(") != std::string_view::npos ||
                line.find("session.assets().addMesh(") != std::string_view::npos ||
                line.find("session.scene().createEntity(") != std::string_view::npos ||
                line.find("session.scene().setMesh(") != std::string_view::npos ||
                line.find("session.replaceStandaloneMesh(") != std::string_view::npos;
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

TEST(ProgrammaticApiGuardrailTests, PublicApiHeadersAvoidInternalOwnerContracts) {
    for (const std::filesystem::path& file : publicApiHeaderFiles()) {
        const std::string source = readFile(file);
        const std::string relative = normalizedRelative(file);

        EXPECT_FALSE(containsAny(source, {
                             "\"assets/",
                             "<assets/",
                             "\"editor/",
                             "<editor/",
                             "\"mesh/",
                             "<mesh/",
                             "\"render/",
                             "<render/",
                             "\"scene/",
                             "<scene/",
                             "\"viewport/",
                             "<viewport/",
                             "quader_editor_core::",
                             "quader_host_contract::",
                             "quader_io::",
                             "quader_poly::",
                     }))
                << relative;

        for (std::string_view token : {
                     "ProjectSession",
                     "EditorHistoryService",
                     "SceneDocument",
                     "AssetRegistry",
                     "ModelingSessionService",
                     "MeshAsset",
                     "TextureAsset",
                     "MaterialDocument",
                     "MeshAssetRevision",
                     "TransformComponent",
                     "ViewportOverlayFrame",
                     "ModelingBounds",
                     "ModelingSelectionSummary",
                     "ModelingToolStatus",
             }) {
            EXPECT_FALSE(containsIdentifierToken(source, token)) << relative << " exposes " << token;
        }
    }
}

TEST(ProgrammaticApiGuardrailTests, PublicRenderApiDoesNotExposeRenderBridge) {
    const std::string header = readFile(sourceRoot() / "src/api/QuaderRenderApi.h");

    EXPECT_EQ(header.find("SceneRenderBridge"), std::string::npos);
    EXPECT_EQ(header.find("syncScene("), std::string::npos);
    EXPECT_EQ(header.find("syncToolPreviews("), std::string::npos);
    EXPECT_EQ(header.find("syncSelectionOverlay("), std::string::npos);
    EXPECT_NE(header.find("friend class QuaderRenderApiRuntimeAccess"), std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests, ApiLayerUsesSharedResultStatusConversion) {
    for (const std::filesystem::path& file : sourceFilesUnder(sourceRoot() / "src/api")) {
        const std::string relative = normalizedRelative(file);
        if (relative == "src/api/QuaderApiMutationRunner.cpp") {
            continue;
        }

        const std::string source = readFile(file);
        for (const std::string& line : linesContaining(source, {".ok = result.ok", ".changed = result.changed"})) {
            ADD_FAILURE() << relative << ": use quaderApiStatusFromResult for QuaderApiResult status copies: " << line;
        }
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
                     "\"editor/commands/document/",
             }) {
            EXPECT_EQ(source.find(forbidden), std::string::npos) << relative << " exposes " << forbidden;
        }
    }
}

TEST(ProgrammaticApiGuardrailTests, PublicCommandApiUsesViewportInputValueContracts) {
    for (const std::filesystem::path& file : publicApiHeaderFiles()) {
        const std::string source = readFile(file);
        const std::string relative = normalizedRelative(file);

        EXPECT_EQ(source.find("\"viewport/ViewportInputRouter.h\""), std::string::npos)
                << relative;
        EXPECT_EQ(source.find("\"viewport/ViewportInputTypes.h\""), std::string::npos)
                << relative;
        EXPECT_EQ(source.find("\"editor/input/QuaderInputBinding.h\""), std::string::npos)
                << relative;
    }

    const std::string commandHeader = readFile(sourceRoot() / "src/api/QuaderCommandsApi.h");
    const std::string keybindingHeader = readFile(sourceRoot() / "src/api/QuaderKeybindingTypes.h");
    const std::string toolsHeader = readFile(sourceRoot() / "src/api/QuaderToolsApi.h");
    EXPECT_NE(commandHeader.find("\"api/QuaderViewportInputTypes.h\""), std::string::npos);
    EXPECT_NE(toolsHeader.find("\"api/QuaderViewportInputTypes.h\""), std::string::npos);
    EXPECT_NE(keybindingHeader.find("\"api/QuaderInputTypes.h\""), std::string::npos);
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

TEST(ProgrammaticApiGuardrailTests, PublicSelectionApiDelegatesEntityEditPolicyToEditorSelectionService) {
    const std::string apiHeader = readFile(sourceRoot() / "src/api/QuaderSelectionApi.h");
    const std::string apiSource = readFile(sourceRoot() / "src/api/QuaderSelectionApi.cpp");
    const std::string serviceSource = readFile(sourceRoot() / "src/editor/selection/EditorSelectionService.cpp");

    EXPECT_EQ(apiHeader.find("EditorSelectionEdit"), std::string::npos);
    EXPECT_EQ(apiSource.find("editedEntitySelection"), std::string::npos);
    EXPECT_EQ(apiSource.find("editedObjectSelection"), std::string::npos);
    EXPECT_EQ(apiSource.find("session_.scene().selectedEntities()"), std::string::npos);
    EXPECT_NE(apiSource.find("projectSession(session_).selection().editSceneSelection("), std::string::npos);
    EXPECT_NE(serviceSource.find("EditorSelectionService::editSceneSelection("), std::string::npos);
    EXPECT_NE(serviceSource.find("resolveSceneSelectionEdit("), std::string::npos);
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
            "modeling.deleteComponentSelection(",
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
            "context.modeling.deleteComponentSelection(",
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

TEST(ProgrammaticApiGuardrailTests, ViewportShortcutControllerDoesNotRemapCommands) {
    const std::string source = readFile(sourceRoot() / "src/ui/ViewportShortcutController.cpp");

    EXPECT_EQ(source.find("QuaderCommand::"), std::string::npos);
    EXPECT_EQ(source.find("executeViewportModelingShortcut"), std::string::npos);
    EXPECT_EQ(source.find("commandForMirrorAxis"), std::string::npos);
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

TEST(ProgrammaticApiGuardrailTests, PublicDocsExamplesManifestClassifiesEveryDocsAndExamplesFile) {
    const PublicDocsExamplesManifest manifest = publicDocsExamplesManifest();
    EXPECT_FALSE(manifest.publicFiles.empty());

    std::set<std::string> classified;
    for (const std::filesystem::path& file : manifest.publicFiles) {
        const std::string relative = normalizedRelative(file);
        EXPECT_TRUE(std::filesystem::exists(file)) << relative;
        classified.insert(relative);
    }
    for (const std::filesystem::path& file : manifest.internalFiles) {
        const std::string relative = normalizedRelative(file);
        EXPECT_TRUE(std::filesystem::exists(file)) << relative;
        classified.insert(relative);
    }

    for (const std::filesystem::path& root : {sourceRoot() / "docs", sourceRoot() / "examples"}) {
        for (const std::filesystem::path& file : docAndExampleFilesUnder(root)) {
            const std::string relative = normalizedRelative(file);
            EXPECT_NE(classified.find(relative), classified.end()) << "unclassified docs/examples file: " << relative;
        }
    }
}

TEST(ProgrammaticApiGuardrailTests, PublicExamplesAndDocsDoNotStartFromInternalOwnerHeaders) {
    for (const std::filesystem::path& file : publicDocAndExampleFiles()) {
        const std::string source = readFile(file);
        const std::string relative = normalizedRelative(file);

        EXPECT_FALSE(containsAny(source, {
                             "editor/modeling/services/ModelingSessionService.h",
                             "scene/SceneDocument.h",
                             "assets/AssetRegistry.h",
                             "editor/commands/DocumentCommandService.h",
                             "render/SceneRenderBridge.h",
                             "ui/UISystem.h",
                             "platform/PlatformWindow.h",
                             "scene/document/ProjectSession.h",
                             "ProjectSession",
                     }))
                << relative;
    }
}

TEST(ProgrammaticApiGuardrailTests, PublicCppExamplesAreCompiledOrMarkedDocumentationOnly) {
    const std::string cmake = readFile(sourceRoot() / "CMakeLists.txt");

    for (const std::filesystem::path& file : publicCppExampleFiles()) {
        const std::string source = readFile(file);
        const std::string relative = normalizedRelative(file);
        EXPECT_TRUE(cmake.find(relative) != std::string::npos || isDocumentationOnlyExample(source))
                << relative;
    }
}

TEST(ProgrammaticApiGuardrailTests, PublicCppExamplesUseCallableResultHelpers) {
    const std::vector<std::string_view> rawResultFieldPatterns = {
            ".ok)",
            ".ok;",
            ".ok &&",
            ".ok ||",
            ".changed)",
            ".changed;",
            ".changed &&",
            ".changed ||",
            ".value.",
            ".message",
    };

    for (const std::filesystem::path& file : publicCppExampleFiles()) {
        const std::string source = readFile(file);
        const std::string relative = normalizedRelative(file);
        for (const std::string& line : linesContaining(source, rawResultFieldPatterns)) {
            ADD_FAILURE() << relative << ": use callable result helpers in public examples: " << line;
        }
    }
}

TEST(ProgrammaticApiGuardrailTests, PublicExamplesStartFromEditorApiOrScriptSurface) {
    const std::vector<std::string_view> publicEntrySurfaces = {
            "QuaderEditorApi",
            "QuaderApi",
            "QuaderObjectHandle",
            "QuaderScript",
            "public `quader` scripting object",
            "BehaviorScenario",
    };

    for (const std::filesystem::path& file : publicDocAndExampleFiles()) {
        const std::string source = readFile(file);
        const std::string relative = normalizedRelative(file);
        EXPECT_TRUE(containsAny(source, publicEntrySurfaces)) << relative;
    }
}

TEST(ProgrammaticApiGuardrailTests, PublicModelingCommandBridgeIsDescriptorDriven) {
    const std::string commandsApi = readFile(sourceRoot() / "src/api/QuaderCommandsApi.cpp");
    const std::string commandDescriptor = readFile(sourceRoot() / "src/editor/commands/CommandToolDescriptor.h");
    const std::string commandCatalog = readFile(sourceRoot() / "src/editor/commands/ModelingCommandToolCatalog.cpp");

    EXPECT_EQ(commandsApi.find("ModelingActionCommandBridgeDescriptor"), std::string::npos);
    EXPECT_EQ(commandsApi.find("modelingActionCommandBridgeCatalog"), std::string::npos);
    EXPECT_EQ(commandsApi.find("fixedCommandIdForModelingAction"), std::string::npos);
    EXPECT_EQ(commandsApi.find("modeling.action."), std::string::npos);
    EXPECT_EQ(commandsApi.find("publicCompatibilityAlias"), std::string::npos);
    EXPECT_EQ(commandsApi.find("registerAlias"), std::string::npos);
    EXPECT_EQ(commandsApi.find("canonicalCommandId"), std::string::npos);
    EXPECT_EQ(commandsApi.find("executeModelingAction"), std::string::npos);
    EXPECT_EQ(commandsApi.find("executeToolActivation"), std::string::npos);
    EXPECT_NE(commandsApi.find("tool->publicCommandTargetId()"), std::string::npos);
    EXPECT_NE(commandDescriptor.find("publicCommandId"), std::string::npos);
    EXPECT_NE(commandDescriptor.find("registersPublicCommand"), std::string::npos);
    EXPECT_EQ(commandDescriptor.find("publicCompatibilityAlias"), std::string::npos);
    EXPECT_NE(commandCatalog.find(".publicCommandId = entry.publicCommandId"), std::string::npos);
}

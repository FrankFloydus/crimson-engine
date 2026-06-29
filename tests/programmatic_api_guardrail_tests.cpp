////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "support/guardrail_file_scan.hpp"

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
    return relative == "src/api/quader_api_mutation_runner.hpp" ||
            relative == "src/api/quader_api_status_internal.hpp" ||
            relative == "src/api/quader_api_session_internal_access.hpp" ||
            relative == "src/api/quader_api_type_conversions.hpp" ||
            relative == "src/api/quader_polygon_document_internal_access.hpp" ||
            relative == "src/editor/viewport/quader_viewport_api_runtime_access.hpp";
}

std::vector<std::filesystem::path> publicApiHeaderFiles() {
    std::vector<std::filesystem::path> headers;
    for (const std::filesystem::path& file : filesWithExtension(sourceRoot() / "src/api", ".hpp")) {
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
    (void)relative;
    (void)line;
    return false;
}

} // namespace

TEST(ProgrammaticApiGuardrailTests, PublicApiHeadersAvoidRuntimeAndSdkInternals) {
    for (const std::filesystem::path& file : publicApiHeaderFiles()) {
        const std::string source = readFile(file);
        const std::string relative = normalizedRelative(file);

        EXPECT_EQ(source.find("editor/modeling/runtime/session.hpp"), std::string::npos) << relative;
        EXPECT_FALSE(containsAny(source, {
                             "<filament/",
                             "\"filament/",
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
    const std::string header = readFile(sourceRoot() / "src/api/quader_render_api.hpp");

    EXPECT_EQ(header.find("SceneRenderBridge"), std::string::npos);
    EXPECT_EQ(header.find("syncScene("), std::string::npos);
    EXPECT_EQ(header.find("syncToolPreviews("), std::string::npos);
    EXPECT_EQ(header.find("syncSelectionOverlay("), std::string::npos);
    EXPECT_NE(header.find("friend class QuaderRenderApiRuntimeAccess"), std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests, ApiLayerUsesSharedResultStatusConversion) {
    for (const std::filesystem::path& file : sourceFilesUnder(sourceRoot() / "src/api")) {
        const std::string relative = normalizedRelative(file);
        if (relative == "src/api/quader_api_mutation_runner.cpp") {
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
                 std::string_view{"src/api/quader_api_types.hpp"},
                 std::string_view{"src/api/quader_api_snapshot.hpp"},
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
                     "\"editor/commands/editor_command_registry.hpp\"",
                     "\"editor/input/quader_keybinding_service.hpp\"",
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

        EXPECT_EQ(source.find("\"viewport/viewport_input_router.hpp\""), std::string::npos)
                << relative;
        EXPECT_EQ(source.find("\"viewport/viewport_input_types.hpp\""), std::string::npos)
                << relative;
        EXPECT_EQ(source.find("\"editor/input/quader_input_binding.hpp\""), std::string::npos)
                << relative;
    }

    const std::string commandHeader = readFile(sourceRoot() / "src/api/quader_commands_api.hpp");
    const std::string keybindingHeader = readFile(sourceRoot() / "src/api/quader_keybinding_types.hpp");
    const std::string toolsHeader = readFile(sourceRoot() / "src/api/quader_tools_api.hpp");
    EXPECT_NE(commandHeader.find("\"api/quader_viewport_input_types.hpp\""), std::string::npos);
    EXPECT_NE(toolsHeader.find("\"api/quader_viewport_input_types.hpp\""), std::string::npos);
    EXPECT_NE(keybindingHeader.find("\"api/quader_input_types.hpp\""), std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests, ModelingSessionServiceDoesNotExposeSelectionSyncAuthority) {
    const std::string header = readFile(sourceRoot() / "src/editor/modeling/services/modeling_session_service.hpp");
    EXPECT_EQ(header.find("syncObjectSelectionFromScene("), std::string::npos);
    EXPECT_EQ(header.find("syncSceneSelectionFromModeling("), std::string::npos);
    EXPECT_EQ(header.find("projectSceneSelectionFromModeling("), std::string::npos);
    EXPECT_EQ(header.find("friend class EditorSelectionService;"), std::string::npos);

    const std::string source = readFile(sourceRoot() / "src/editor/modeling/services/modeling_session_service.cpp");
    EXPECT_EQ(source.find("scene_.setSelected("), std::string::npos);
    EXPECT_NE(source.find("SelectionProjectionService"), std::string::npos);

    const std::string editorSelectionHeader =
            readFile(sourceRoot() / "src" / "editor" / "selection" / "editor_selection_service.hpp");
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
            "\"editor/commands/document/apply_entity_transform_command.hpp\"",
            "\"editor/commands/document/batch_document_command.hpp\"",
            "\"editor/commands/document/create_entity_command.hpp\"",
            "\"editor/commands/document/destroy_entity_command.hpp\"",
            "\"editor/commands/document/duplicate_entity_command.hpp\"",
            "\"editor/commands/document/duplicate_selected_hierarchy_command.hpp\"",
            "\"editor/commands/document/group_selection_command.hpp\"",
            "\"editor/commands/document/rename_entity_command.hpp\"",
            "\"editor/commands/document/reparent_entity_command.hpp\"",
            "\"editor/commands/document/select_entities_command.hpp\"",
            "\"editor/commands/document/set_lock_command.hpp\"",
            "\"editor/commands/document/set_material_command.hpp\"",
            "\"editor/commands/document/set_mesh_component_command.hpp\"",
            "\"editor/commands/document/set_visibility_command.hpp\"",
            "\"editor/commands/document/ungroup_selection_command.hpp\"",
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
                 sourceRoot() / "src/api/quader_api_mutation_runner.hpp",
                 sourceRoot() / "src/api/quader_api_mutation_runner.cpp",
                 sourceRoot() / "src/editor/commands/modeling_command_execution_service.hpp",
                 sourceRoot() / "src/editor/commands/modeling_command_execution_service.cpp",
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
            {"src/editor/commands/modeling/clear_selection_tool.cpp", "context.selection->clear()"},
            {"src/editor/commands/modeling/select_all_tool.cpp", "context.selection->selectAll()"},
            {"src/editor/commands/modeling/invert_selection_tool.cpp", "context.selection->invert()"},
    };

    for (const auto& [relative, expectedRoute] : expectedRoutes) {
        const std::string source = readFile(sourceRoot() / relative);
        EXPECT_NE(source.find("context.selection != nullptr"), std::string::npos) << relative;
        EXPECT_NE(source.find(expectedRoute), std::string::npos) << relative;
    }
}

TEST(ProgrammaticApiGuardrailTests, PublicSelectionApiDelegatesEntityEditPolicyToEditorSelectionService) {
    const std::string apiHeader = readFile(sourceRoot() / "src/api/quader_selection_api.hpp");
    const std::string apiSource = readFile(sourceRoot() / "src/api/quader_selection_api.cpp");
    const std::string serviceSource = readFile(sourceRoot() / "src/editor/selection/editor_selection_service.cpp");

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
                             "\"support/behavior_scenario.hpp\"",
                             "\"support/behavior_expect.hpp\"",
                             "\"support/behavior_snapshot.hpp\"",
                             "\"support/modeling_test_events.hpp\"",
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
                 std::string_view{"src/ui/viewport_shortcut_controller.cpp"},
                 std::string_view{"src/ui/ui_system.cpp"},
         }) {
        const std::string source = readFile(sourceRoot() / std::string(relative));
        for (const std::string& line : linesContaining(source, forbiddenRawCommandRoutes)) {
            EXPECT_TRUE(false) << relative << ": " << line;
        }
    }
}

TEST(ProgrammaticApiGuardrailTests, ViewportShortcutControllerDoesNotRemapCommands) {
    const std::string source = readFile(sourceRoot() / "src/ui/viewport_shortcut_controller.cpp");
    const std::string header = readFile(sourceRoot() / "src/ui/viewport_shortcut_controller.hpp");

    EXPECT_EQ(source.find("QuaderCommand::"), std::string::npos);
    EXPECT_EQ(source.find("executeViewportModelingShortcut"), std::string::npos);
    EXPECT_EQ(source.find("commandForMirrorAxis"), std::string::npos);
    EXPECT_EQ(source.find("ModelingSessionService"), std::string::npos);
    EXPECT_EQ(header.find("ModelingSessionService"), std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests, ViewportShadingConversionUsesSharedApiHelper) {
    const std::string viewportApi = readFile(sourceRoot() / "src/api/quader_viewport_api.cpp");
    const std::string viewportPanel = readFile(sourceRoot() / "src/ui/viewport_panel.cpp");
    const std::string conversionHelper = readFile(sourceRoot() / "src/viewport/viewport_shading_mode_conversions.hpp");

    EXPECT_NE(conversionHelper.find("quaderApiToViewportShadingMode"), std::string::npos);
    EXPECT_NE(conversionHelper.find("quaderApiFromViewportShadingMode"), std::string::npos);
    EXPECT_EQ(viewportApi.find("ViewportShadingMode toViewportShadingMode"), std::string::npos);
    EXPECT_EQ(viewportApi.find("QuaderShadingMode toQuaderShadingMode"), std::string::npos);
    EXPECT_EQ(viewportPanel.find("ViewportShadingMode toViewportShadingMode"), std::string::npos);
    EXPECT_EQ(viewportPanel.find("QuaderShadingMode toQuaderShadingMode"), std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests, UiPanelRoutingUsesStableIdsAndNoDeadShowMenu) {
    const std::string mainMenuHeader = readFile(sourceRoot() / "src/ui/main_menu_bar.hpp");
    const std::string mainMenuSource = readFile(sourceRoot() / "src/ui/main_menu_bar.cpp");
    const std::string settingsSource = readFile(sourceRoot() / "src/ui/ui_settings.cpp");
    const std::string uiPanelHeader = readFile(sourceRoot() / "src/ui/ui_panel.hpp");

    EXPECT_NE(uiPanelHeader.find("uiPanelStableId"), std::string::npos);
    EXPECT_NE(uiPanelHeader.find("uiPanelDefaultVisible"), std::string::npos);
    EXPECT_EQ(mainMenuHeader.find("ShowMenu"), std::string::npos);
    EXPECT_EQ(mainMenuHeader.find("showMenu_"), std::string::npos);
    EXPECT_EQ(mainMenuSource.find("void ShowMenu::draw"), std::string::npos);
    EXPECT_EQ(mainMenuHeader.find("toolSettingsPanelOpen"), std::string::npos);
    EXPECT_EQ(mainMenuHeader.find("UISettings& settings"), std::string::npos);
    EXPECT_EQ(mainMenuSource.find("showPanel(uiPanelKey"), std::string::npos);
    EXPECT_EQ(mainMenuSource.find("hidePanel(uiPanelKey"), std::string::npos);
    EXPECT_EQ(settingsSource.find("file << uiPanelKey"), std::string::npos);
    EXPECT_NE(settingsSource.find("uiPanelStableId(UIPanelId::Outliner)"), std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests, OutlinerUsesDocumentCommandRootSelectionOwner) {
    const std::string outliner = readFile(sourceRoot() / "src/ui/outliner_panel.cpp");

    EXPECT_NE(outliner.find("documentCommandSelectedRootEntities"), std::string::npos);
    EXPECT_EQ(outliner.find("std::vector<EntityId> selectedRootEntities("), std::string::npos);
    EXPECT_EQ(outliner.find("std::unordered_set<EntityId> selectedSet"), std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests, UiAppAndViewportDoNotExposeRawHistoryOrDocumentCommandServices) {
    const std::vector<std::string_view> forbiddenOwnerServices = {
            "UndoRedoManager",
            "DocumentCommandService",
            "core/undo_redo_manager.hpp",
            "editor/commands/document_command_service.hpp",
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
            sourceRoot() / "src/api/quader_api.cpp",
            sourceRoot() / "src/api/quader_scene_api.cpp",
            sourceRoot() / "src/scene/document/project_session.hpp",
            sourceRoot() / "src/scene/document/project_session.cpp",
            sourceRoot() / "src/editor/commands/document_command_service.hpp",
            sourceRoot() / "src/editor/commands/document_command_service.cpp",
            sourceRoot() / "src/editor/history/scene_transform_preview_transaction.hpp",
    };
    for (const std::filesystem::path& file : editorHistoryClients) {
        const std::string source = readFile(file);
        const std::string relative = normalizedRelative(file);
        EXPECT_EQ(source.find("UndoRedoManager"), std::string::npos) << relative;
        EXPECT_EQ(source.find("core/undo_redo_manager.hpp"), std::string::npos) << relative;
        EXPECT_EQ(source.find("undoRedo()"), std::string::npos) << relative;
    }

    const std::string historyHeader = readFile(sourceRoot() / "src/editor/history/editor_history_service.hpp");
    EXPECT_EQ(historyHeader.find("core/undo_redo_manager.hpp"), std::string::npos);
    EXPECT_EQ(historyHeader.find("undoRedo()"), std::string::npos);

    const std::string historySource = readFile(sourceRoot() / "src/editor/history/editor_history_service.cpp");
    EXPECT_NE(historySource.find("core/undo_redo_manager.hpp"), std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests, PublicBehaviorTestsUseApiOrScenarioDsl) {
    const std::vector<std::string_view> forbidden = {
            "\"editor/commands/",
            "\"editor/modeling/services/modeling_session_service.hpp\"",
            "\"editor/selection/",
            "\"editor/tools/",
            "\"api/quader_api_session_internal_access.hpp\"",
            "\"scene/scene_document.hpp\"",
            "\"scene/document/project_session.hpp\"",
            "ModelingSessionService",
            "DocumentCommandService",
            "SceneDocument",
            "AssetRegistry",
            "ProjectSession",
            "QuaderApiSessionInternalAccess",
            "session.scene().",
            "session.selection().",
            "session.modeling().",
            "session().scene().",
            "session().selection().",
            "session().modeling().",
            "scenario.session()",
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

    const std::string behaviorScenario = readFile(sourceRoot() / "tests/support/behavior_scenario.hpp");
    EXPECT_EQ(behaviorScenario.find("ProjectSession& session()"), std::string::npos);
    EXPECT_EQ(behaviorScenario.find("const ProjectSession& session()"), std::string::npos);
    EXPECT_NE(behaviorScenario.find("BehaviorWhen& dispatchViewportShortcut"), std::string::npos);
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
            ADD_FAILURE() << relative << ": move duplicated runtime test helper to editor_operation_test_helpers.hpp: " << line;
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
                             "editor/modeling/services/modeling_session_service.hpp",
                             "scene/scene_document.hpp",
                             "assets/asset_registry.hpp",
                             "editor/commands/document_command_service.hpp",
                             "render/scene_render_bridge.hpp",
                             "ui/ui_system.hpp",
                             "platform/platform_window.hpp",
                             "scene/document/project_session.hpp",
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
    const std::string commandsApi = readFile(sourceRoot() / "src/api/quader_commands_api.cpp");
    const std::string commandDescriptor = readFile(sourceRoot() / "src/editor/commands/command_tool_descriptor.hpp");
    const std::string commandCatalog = readFile(sourceRoot() / "src/editor/commands/modeling_command_tool_catalog.cpp");

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

TEST(ProgrammaticApiGuardrailTests, PublicCommandTargetsUseSharedPrefixOwner) {
    const std::string commandsApi = readFile(sourceRoot() / "src/api/quader_commands_api.cpp");
    const std::string commandTool = readFile(sourceRoot() / "src/editor/commands/command_tool.hpp");
    const std::string commandRegistryHeader = readFile(sourceRoot() / "src/editor/commands/editor_command_registry.hpp");
    const std::string commandRegistrySource = readFile(sourceRoot() / "src/editor/commands/editor_command_registry.cpp");
    const std::string viewportAdapter = readFile(sourceRoot() / "src/viewport/host_viewport_contract_adapter.cpp");
    const std::string commandTargets = readFile(sourceRoot() / "src/core/public_command_targets.hpp");

    EXPECT_NE(commandTargets.find("kQuaderModelingCommandTargetPrefix"), std::string::npos);
    EXPECT_NE(commandTargets.find("kQuaderToolActivationCommandTargetPrefix"), std::string::npos);
    EXPECT_NE(commandsApi.find("quaderModelingCommandTargetId"), std::string::npos);
    EXPECT_NE(commandsApi.find("quaderToolActivationCommandTargetId"), std::string::npos);
    EXPECT_NE(commandTool.find("quaderModelingCommandTargetId"), std::string::npos);
    EXPECT_NE(viewportAdapter.find("quaderToolIdFromActivationCommandTarget"), std::string::npos);

    EXPECT_EQ(commandsApi.find("canonicalTargetId"), std::string::npos);
    EXPECT_EQ(commandRegistryHeader.find("registerPrefix"), std::string::npos);
    EXPECT_EQ(commandRegistryHeader.find("PrefixRoute"), std::string::npos);
    EXPECT_EQ(commandRegistrySource.find("registerPrefix"), std::string::npos);
    EXPECT_EQ(commandRegistrySource.find("PrefixRoute"), std::string::npos);
    EXPECT_EQ(commandsApi.find("registerPrefix(\"tools.activate."), std::string::npos);
    EXPECT_EQ(commandTool.find("std::string commandId(\"modeling."), std::string::npos);
    EXPECT_EQ(viewportAdapter.find("std::string commandId(\"tools.activate."), std::string::npos);
    EXPECT_EQ(viewportAdapter.find("constexpr std::string_view toolPrefix = \"tools.activate."), std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests, HostViewportContractAdapterKeepsImplementationOutOfHeader) {
    const std::string rootCMake = readFile(sourceRoot() / "CMakeLists.txt");
    const std::string adapterHeader = readFile(sourceRoot() / "src/viewport/host_viewport_contract_adapter.hpp");
    const std::string adapterSource = readFile(sourceRoot() / "src/viewport/host_viewport_contract_adapter.cpp");

    EXPECT_NE(rootCMake.find("src/viewport/host_viewport_contract_adapter.cpp"), std::string::npos);
    EXPECT_NE(adapterSource.find("std::span<const ViewportShortcutTargetDescriptor> viewportShortcutTargetCatalog()"),
            std::string::npos);
    EXPECT_NE(adapterSource.find("#include \"api/quader_command_ids.hpp\""), std::string::npos);
    EXPECT_NE(adapterSource.find("#include <editor/metadata/host_contract.hpp>"), std::string::npos);
    EXPECT_NE(adapterHeader.find("std::span<const ViewportShortcutTargetDescriptor> viewportShortcutTargetCatalog();"),
            std::string::npos);

    EXPECT_EQ(adapterHeader.find("inline "), std::string::npos);
    EXPECT_EQ(adapterHeader.find("#include \"api/quader_command_ids.hpp\""), std::string::npos);
    EXPECT_EQ(adapterHeader.find("#include \"editor/commands/modeling_command_tool_catalog.hpp\""), std::string::npos);
    EXPECT_EQ(adapterHeader.find("#include <editor/metadata/host_contract.hpp>"), std::string::npos);
    EXPECT_EQ(adapterHeader.find("quader_host_contract::"), std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests, ApiStatusStampsUseSharedInternalHelper) {
    const std::string statusHelper = readFile(sourceRoot() / "src/api/quader_api_status_internal.hpp");
    const std::string mutationRunner = readFile(sourceRoot() / "src/api/quader_api_mutation_runner.cpp");
    const std::string toolsApi = readFile(sourceRoot() / "src/api/quader_tools_api.cpp");
    const std::string queryApi = readFile(sourceRoot() / "src/api/quader_query_api.cpp");
    const std::string modelingApi = readFile(sourceRoot() / "src/api/quader_modeling_api.cpp");
    const std::string projectApi = readFile(sourceRoot() / "src/api/quader_project_api.cpp");

    EXPECT_NE(statusHelper.find("quaderApiCurrentRevisions"), std::string::npos);
    EXPECT_NE(mutationRunner.find("quaderApiVoidStatus"), std::string::npos);
    EXPECT_NE(toolsApi.find("quaderApiVoidStatus"), std::string::npos);
    EXPECT_NE(queryApi.find("quaderApiCurrentRevisions"), std::string::npos);
    EXPECT_NE(modelingApi.find("quaderApiPreviewMutationKind"), std::string::npos);
    EXPECT_NE(projectApi.find("quaderApiFailureStatus"), std::string::npos);

    EXPECT_EQ(toolsApi.find("QuaderToolsApi::currentRevisions"), std::string::npos);
    EXPECT_EQ(toolsApi.find(".modelingContentRevision = summary.contentRevision"), std::string::npos);
    EXPECT_EQ(queryApi.find(".modelingContentRevision = summary.contentRevision"), std::string::npos);
    EXPECT_EQ(modelingApi.find(".modelingContentRevision = summary.contentRevision"), std::string::npos);
}

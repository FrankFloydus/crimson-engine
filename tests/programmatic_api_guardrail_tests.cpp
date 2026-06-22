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

bool allowedSceneDocumentMutation(std::string_view relative, std::string_view line) {
    if (relative != "src/viewport/tools/base/TwoStageBoxTool.cpp") {
        return false;
    }

    return line.find("document.setMaterial(") != std::string_view::npos ||
            line.find("document.clearSelection(") != std::string_view::npos ||
            line.find("document.setSelected(") != std::string_view::npos;
}

bool allowedRawModelingMutation(std::string_view relative, std::string_view line) {
    if (relative == "src/ui/OutlinerPanel.cpp") {
        return line.find("context.modeling.setSelectionMode(") != std::string_view::npos ||
                line.find("context.modeling.deleteSelection(") != std::string_view::npos;
    }
    if (relative == "src/ui/TransformGizmoController.cpp") {
        return line.find("modeling.beginTransformDrag(") != std::string_view::npos ||
                line.find("modeling.updateTransformDrag(") != std::string_view::npos ||
                line.find("modeling.commitTransformDrag(") != std::string_view::npos ||
                line.find("modeling.cancelTransformDrag(") != std::string_view::npos;
    }
    if (relative == "src/viewport/tools/base/TwoStageBoxTool.cpp") {
        return line.find("context.modeling.setSelectionMode(") != std::string_view::npos;
    }
    if (relative == "src/ui/ViewportSelectionController.cpp") {
        return line.find("modeling.pickSelect(") != std::string_view::npos;
    }
    if (relative == "src/ui/ViewportPanel.cpp") {
        return line.find("modeling.setSelectionMode(") != std::string_view::npos ||
                line.find("modeling.pickSelect(") != std::string_view::npos;
    }
    return false;
}

} // namespace

TEST(ProgrammaticApiGuardrailTests, PublicApiHeadersAvoidRuntimeAndSdkInternals) {
    for (const std::filesystem::path& file : filesWithExtension(sourceRoot() / "src/api", ".h")) {
        const std::string source = readFile(file);
        const std::string relative = normalizedRelative(file);

        EXPECT_EQ(source.find("quader_editor_core/session.h"), std::string::npos) << relative;
        EXPECT_FALSE(containsAny(source, {
                             "<filament/",
                             "\"filament/",
                             "<imgui",
                             "\"imgui",
                             "<SDL",
                             "\"SDL",
                             "<entt/",
                             "\"entt/",
                     }))
                << relative;
    }
}

TEST(ProgrammaticApiGuardrailTests, PublicApiHeadersDoNotExposeMutableInternalOwners) {
    for (const std::filesystem::path& file : filesWithExtension(sourceRoot() / "src/api", ".h")) {
        const std::string source = readFile(file);
        const std::string relative = normalizedRelative(file);

        for (std::string_view forbidden : {
                     "SceneDocument&",
                     "AssetRegistry&",
                     "MeshAsset*",
                     "MaterialDocument*",
                     "TextureAsset*",
                     "entt::registry&",
                     "EditorSession&",
             }) {
            EXPECT_EQ(source.find(forbidden), std::string::npos) << relative << " exposes " << forbidden;
        }
    }
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

    for (const std::filesystem::path& root : {sourceRoot() / "src/ui", sourceRoot() / "src/app", sourceRoot() / "src/viewport"}) {
        for (const std::filesystem::path& file : filesWithExtension(root, ".cpp")) {
            const std::string source = readFile(file);
            const std::string relative = normalizedRelative(file);
            for (const std::string& line : linesContaining(source, directSceneMutations)) {
                EXPECT_TRUE(allowedSceneDocumentMutation(relative, line)) << relative << ": " << line;
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

TEST(ProgrammaticApiGuardrailTests, ProgrammaticBehaviorTestsUseApiOrScenarioDsl) {
    for (const std::filesystem::path& file : {
                 sourceRoot() / "tests/programmatic_api_tests.cpp",
                 sourceRoot() / "tests/programmatic_behavior_scenario_tests.cpp",
         }) {
        const std::string source = readFile(file);
        const std::string relative = normalizedRelative(file);

        EXPECT_FALSE(containsAny(source, {
                             "ModelingSessionService",
                             "DocumentCommandService",
                             "SceneDocument",
                             "AssetRegistry",
                             "createBoxFromBounds",
                             "executeInstantOperation",
                             "pickSelect(",
                     }))
                << relative;
    }
}

TEST(ProgrammaticApiGuardrailTests, PublicExamplesAndDocsDoNotStartFromInternalOwnerHeaders) {
    for (const std::filesystem::path& root : {sourceRoot() / "docs", sourceRoot() / "examples"}) {
        for (const std::filesystem::path& file : publicDocAndExampleFilesUnder(root)) {
            const std::string source = readFile(file);
            const std::string relative = normalizedRelative(file);

            EXPECT_FALSE(containsAny(source, {
                                 "modeling/ModelingSessionService.h",
                                 "scene/SceneDocument.h",
                                 "assets/AssetRegistry.h",
                                 "document/DocumentCommandService.h",
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

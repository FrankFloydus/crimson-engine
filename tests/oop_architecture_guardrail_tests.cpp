////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>
#include <string_view>
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

std::string normalizedRelative(const std::filesystem::path& path) {
    return std::filesystem::relative(path, sourceRoot()).generic_string();
}

bool containsAny(const std::string& source, const std::vector<std::string_view>& needles) {
    return std::any_of(needles.begin(), needles.end(), [&](std::string_view needle) {
        return source.find(needle) != std::string::npos;
    });
}

bool startsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

std::string cmakeSourceTruth() {
    std::string source = readFile(sourceRoot() / "CMakeLists.txt");
    for (const std::filesystem::path& file : filesWithExtension(sourceRoot() / "cmake", ".cmake")) {
        source.append("\n");
        source.append(readFile(file));
    }
    return source;
}

bool coveredByAllowedCMakeGlob(std::string_view relative, const std::string& cmake) {
    if (startsWith(relative, "src/mesh/polygon/operations/")) {
        return cmake.find("src/mesh/polygon/operations/*.cpp") != std::string::npos;
    }
    if (startsWith(relative, "src/mesh/polygon/")) {
        return cmake.find("src/mesh/polygon/*.cpp") != std::string::npos;
    }
    return false;
}

bool looksConstructorOnlyCpp(const std::string& source) {
    const std::regex constructorOnly(
            R"(^\s*#include\s+\"[^\"]+\"\s*(?:namespace\s+\w+\s*\{\s*){0,2}\w+::\w+\([^)]*\)\s*:)",
            std::regex::ECMAScript);
    const std::regex methodDefinition(R"(\w+::\w+\s*\([^)]*\)\s*(?:const\s*)?\{)");
    const auto begin = std::sregex_iterator(source.begin(), source.end(), methodDefinition);
    const auto end = std::sregex_iterator();
    return std::regex_search(source, constructorOnly) && std::distance(begin, end) <= 1;
}

std::vector<std::string> lines(const std::string& source) {
    std::vector<std::string> result;
    std::string line;
    for (const char ch : source) {
        if (ch == '\n') {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            result.push_back(line);
            line.clear();
        } else {
            line.push_back(ch);
        }
    }
    if (!line.empty()) {
        result.push_back(line);
    }
    return result;
}

std::string trim(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }).base();
    if (first >= last) {
        return {};
    }
    return {first, last};
}

std::string firstIncludeLine(const std::string& source) {
    for (const std::string& line : lines(source)) {
        const std::string trimmed = trim(line);
        if (startsWith(trimmed, "#include ")) {
            return trimmed;
        }
    }
    return {};
}

bool hasNearbyDeclarationDoc(const std::vector<std::string>& fileLines, std::size_t declarationLine) {
    if (declarationLine == 0) {
        return false;
    }

    std::size_t cursor = declarationLine;
    while (cursor > 0) {
        --cursor;
        const std::string previous = trim(fileLines[cursor]);
        if (previous.empty()) {
            continue;
        }
        return previous == "*/" ||
                previous.rfind("///", 0) == 0 ||
                previous.rfind("//!", 0) == 0 ||
                previous.find("@brief") != std::string::npos;
    }

    return false;
}

} // namespace

TEST(OopArchitectureGuardrailTests, TypeDeclarationsHaveMeaningfulDocumentation) {
    const std::regex declaration(R"(^\s*(class|struct|enum(?:\s+class)?)\s+([A-Za-z_][A-Za-z0-9_]*)\b)");

    for (const std::filesystem::path& root : {sourceRoot() / "src", sourceRoot() / "tests", sourceRoot() / "benchmarks"}) {
        for (const std::string_view extension : {std::string_view{".h"}, std::string_view{".hpp"}, std::string_view{".cpp"}}) {
            for (const std::filesystem::path& file : filesWithExtension(root, extension)) {
                const std::string source = readFile(file);
                const std::vector<std::string> fileLines = lines(source);
                const std::string relative = normalizedRelative(file);

                for (std::size_t index = 0; index < fileLines.size(); ++index) {
                    const std::string& line = fileLines[index];
                    if (!std::regex_search(line, declaration) || line.find(';') != std::string::npos) {
                        continue;
                    }

                    EXPECT_TRUE(hasNearbyDeclarationDoc(fileLines, index)) << relative << ":" << (index + 1) << ": " << line;
                }
            }
        }
    }
}

TEST(OopArchitectureGuardrailTests, SourceCppFilesAreNotEmptyOrConstructorOnly) {
    for (const std::filesystem::path& file : filesWithExtension(sourceRoot() / "src", ".cpp")) {
        const std::string source = readFile(file);
        const std::string relative = normalizedRelative(file);

        EXPECT_FALSE(source.empty()) << relative;
        EXPECT_FALSE(looksConstructorOnlyCpp(source)) << relative;
    }
}

TEST(OopArchitectureGuardrailTests, InteractionSourceFilesIncludeTheirPublicHeaderFirst) {
    const std::filesystem::path root = sourceRoot() / "src/editor/interactions";
    for (const std::filesystem::path& file : filesWithExtension(root, ".cpp")) {
        const std::filesystem::path header = file.parent_path() / (file.stem().string() + ".h");
        if (!std::filesystem::exists(header)) {
            continue;
        }

        const std::string firstInclude = firstIncludeLine(readFile(file));
        const std::string relative = normalizedRelative(file);
        const std::string stem = file.stem().string();
        const std::vector<std::string> allowed{
                "#include <editor/interactions/" + stem + ".h>",
                "#include \"editor/interactions/" + stem + ".h\"",
                "#include \"" + stem + ".h\"",
        };

        EXPECT_NE(std::find(allowed.begin(), allowed.end(), firstInclude), allowed.end())
                << relative << " first include was: " << firstInclude;
    }
}

TEST(OopArchitectureGuardrailTests, EverySourceCppIsPartOfCMakeSourceTruth) {
    const std::string cmake = cmakeSourceTruth();

    for (const std::filesystem::path& file : filesWithExtension(sourceRoot() / "src", ".cpp")) {
        const std::string relative = normalizedRelative(file);
        EXPECT_TRUE(cmake.find(relative) != std::string::npos || coveredByAllowedCMakeGlob(relative, cmake)) << relative;
    }
}

TEST(OopArchitectureGuardrailTests, UserFacingToolsAreNotPassThroughAliases) {
    for (const std::filesystem::path& file : filesWithExtension(sourceRoot() / "src/editor/commands/modeling", ".h")) {
        const std::string source = readFile(file);
        const std::string relative = normalizedRelative(file);
        if (relative.find("Tool.h") == std::string::npos ||
                (relative.find("/commands/") == std::string::npos &&
                        relative.find("/context/") == std::string::npos &&
                        relative.find("/preview/") == std::string::npos)) {
            continue;
        }

        EXPECT_EQ(source.find("using "), std::string::npos) << relative;
        EXPECT_EQ(source.find("= default;"), std::string::npos) << relative;
        EXPECT_EQ(source.find("SdkOperationTool"), std::string::npos) << relative;
    }
}

TEST(OopArchitectureGuardrailTests, ToolCatalogHeadersAreDeclarationsOnly) {
    for (const std::filesystem::path& file : {
                 sourceRoot() / "src/editor/commands/ModelingCommandToolCatalog.h",
                 sourceRoot() / "src/editor/tools/ViewportToolCatalog.h",
         }) {
        const std::string source = readFile(file);
        const std::string relative = normalizedRelative(file);
        EXPECT_EQ(source.find("static const"), std::string::npos) << relative;
        EXPECT_EQ(source.find("inline std::span"), std::string::npos) << relative;
        EXPECT_EQ(source.find("std::array"), std::string::npos) << relative;
    }
}

TEST(OopArchitectureGuardrailTests, ModelingInternalDomainHeadersAvoidFacadeDependency) {
    for (const std::filesystem::path& root : {
                 sourceRoot() / "src/editor/modeling/runtime",
                 sourceRoot() / "src/editor/modeling/picking",
                 sourceRoot() / "src/editor/modeling/preview",
                 sourceRoot() / "src/editor/modeling/runtime_state",
         }) {
        for (const std::filesystem::path& file : filesWithExtension(root, ".h")) {
            const std::string source = readFile(file);
            const std::string relative = normalizedRelative(file);
            EXPECT_EQ(source.find("editor/modeling/services/ModelingSessionService.h"), std::string::npos) << relative;
        }
    }
}

TEST(OopArchitectureGuardrailTests, SceneDocumentDoesNotExposeRawRegistryAccess) {
    const std::string header = readFile(sourceRoot() / "src/scene/SceneDocument.h");

    EXPECT_EQ(header.find("registry& registry("), std::string::npos);
    EXPECT_EQ(header.find("entt::registry& registry("), std::string::npos);
}

TEST(OopArchitectureGuardrailTests, UiDoesNotBypassCommandAndPreviewBoundaries) {
    for (const std::filesystem::path& root : {sourceRoot() / "src/ui", sourceRoot() / "src/app", sourceRoot() / "src/viewport"}) {
        for (const std::filesystem::path& file : filesWithExtension(root, ".cpp")) {
            const std::string source = readFile(file);
            const std::string relative = normalizedRelative(file);
            EXPECT_FALSE(containsAny(source, {
                                      "executeInstantOperation",
                                      "executeContextAction",
                                      "commitActiveOperationPreview",
                                      "cancelActiveOperationPreview",
                                      "executeExternal",
                              }))
                    << relative;
        }
    }
}

TEST(OopArchitectureGuardrailTests, UiShellDoesNotOwnToolImplementations) {
    const std::string source = readFile(sourceRoot() / "src/ui/UISystem.cpp");

    for (std::string_view forbidden : {
                 "selectFromViewport",
                 "drawTransformGizmo",
                 "GizmoAxis",
                 "GizmoStartTransform",
                 "boxPreview_",
                 "insertEdgeLoopDragActive_",
                 "cutDragActive_",
                 "mirrorDragActive_",
                 "beginInsertEdgeLoopDrag",
                 "updateKnifeHover",
                 "clickCut",
                 "clickMirror",
                 "acceptPolyPoint",
                 "updateBoxHover",
                 "beginTransformDrag",
                 "commitTransformDrag",
         }) {
        EXPECT_EQ(source.find(forbidden), std::string::npos) << forbidden;
    }
}

TEST(OopArchitectureGuardrailTests, UiShellLeavesSceneOverlayExtractionToRenderBuilder) {
    const std::string uiSystem = readFile(sourceRoot() / "src/ui/UISystem.cpp");
    const std::string viewportHost = readFile(sourceRoot() / "src/ui/controllers/ViewportHost.cpp");
    const std::string uiShell = uiSystem + viewportHost;
    const std::string builder = readFile(sourceRoot() / "src/render/overlay/ViewportSceneOverlayBuilder.cpp");

    EXPECT_NE(builder.find("appendHierarchySelectionWires"), std::string::npos);
    EXPECT_NE(builder.find("appendGroupSelectionCages"), std::string::npos);
    EXPECT_NE(builder.find("appendSceneWireframeOverlay"), std::string::npos);
    EXPECT_NE(builder.find("stripWireframeObjectFaceFills"), std::string::npos);
    EXPECT_NE(viewportHost.find("ViewportOverlayFrame{}"), std::string::npos);

    for (std::string_view forbidden : {
                 "ViewportSceneOverlayBuilder::buildSelectionFrame(",
                 "ViewportSceneOverlayBuilder::buildWireframeFrame(",
                 "appendMeshSelectionWire",
                 "appendHierarchySelectionWires",
                 "appendGroupSelectionCages",
                 "appendSceneWireframeOverlay",
                 "stripWireframeObjectFaceFills",
                 "WireEdgeKey",
         }) {
        EXPECT_EQ(uiShell.find(forbidden), std::string::npos) << forbidden;
    }
}

TEST(OopArchitectureGuardrailTests, ViewportMaximizedStateBelongsToWorkspaceService) {
    const std::string inputHeader = readFile(sourceRoot() / "src/viewport/ViewportInputRouter.h");
    const std::string inputSource = readFile(sourceRoot() / "src/viewport/ViewportInputRouter.cpp");
    const std::string runtimeSource = readFile(sourceRoot() / "src/editor/tools/ViewportToolRuntime.cpp");
    const std::string workspaceHeader = readFile(sourceRoot() / "src/viewport/ViewportWorkspaceService.h");
    const std::string workspaceSource = readFile(sourceRoot() / "src/viewport/ViewportWorkspaceService.cpp");

    const std::size_t stateStart = inputHeader.find("struct ViewportToolState");
    ASSERT_NE(stateStart, std::string::npos);
    const std::size_t stateEnd = inputHeader.find("};", stateStart);
    ASSERT_NE(stateEnd, std::string::npos);
    const std::string stateBody = inputHeader.substr(stateStart, stateEnd - stateStart);

    EXPECT_EQ(stateBody.find("maximized"), std::string::npos);
    EXPECT_EQ(inputHeader.find("setMaximized"), std::string::npos);
    EXPECT_EQ(inputSource.find("setMaximized"), std::string::npos);
    EXPECT_EQ(runtimeSource.find("state.maximized"), std::string::npos);
    EXPECT_EQ(workspaceHeader.find("activePanePlacements(bool"), std::string::npos);
    EXPECT_EQ(workspaceHeader.find("activeSplitHandles(bool"), std::string::npos);
    EXPECT_EQ(workspaceSource.find("activePanePlacements(bool"), std::string::npos);
    EXPECT_EQ(workspaceSource.find("activeSplitHandles(bool"), std::string::npos);
    EXPECT_NE(workspaceHeader.find("focusedPaneMaximized"), std::string::npos);
    EXPECT_NE(workspaceSource.find("focusedPaneMaximized_"), std::string::npos);
    EXPECT_NE(runtimeSource.find("result.maximizedToggleRequested = true"), std::string::npos);
}

TEST(OopArchitectureGuardrailTests, SceneTransformDragServiceOwnsTransformPreviewTransactions) {
    EXPECT_FALSE(std::filesystem::exists(sourceRoot() / "src/ui/TransformGizmoController.h"));
    EXPECT_FALSE(std::filesystem::exists(sourceRoot() / "src/ui/TransformGizmoController.cpp"));

    const std::string serviceSource =
            readFile(sourceRoot() / "src/editor/transform/SceneTransformDragService.cpp");

    EXPECT_NE(serviceSource.find("SceneTransformPreviewTransaction"), std::string::npos);
    EXPECT_NE(serviceSource.find("hasSelectedAncestor(document, entity)"), std::string::npos);
    EXPECT_NE(serviceSource.find("documentLocalPivotForWorldPoint(document, entity, worldPivot)"), std::string::npos);
    EXPECT_NE(serviceSource.find("editor.scene().batch(label)"), std::string::npos);
}

TEST(OopArchitectureGuardrailTests, RenderFacadeDoesNotOwnOverlayProxyInternals) {
    const std::string header = readFile(sourceRoot() / "src/render/SceneRenderBridge.h");
    const std::string source = readFile(sourceRoot() / "src/render/SceneRenderBridge.cpp");
    const std::string combined = header + "\n" + source;

    for (std::string_view forbidden : {
                 "OverlaySlot",
                 "OverlayProxyGroup",
                 "rebuildOverlayProxies",
                 "destroyOverlayProxy",
                 "toolPreviewMeshProxies_",
         }) {
        EXPECT_EQ(combined.find(forbidden), std::string::npos) << forbidden;
    }
}

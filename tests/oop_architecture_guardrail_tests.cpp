#include <gtest/gtest.h>

#include <algorithm>
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

bool looksConstructorOnlyCpp(const std::string& source) {
    const std::regex constructorOnly(
            R"(^\s*#include\s+\"[^\"]+\"\s*(?:namespace\s+\w+\s*\{\s*){0,2}\w+::\w+\([^)]*\)\s*:)",
            std::regex::ECMAScript);
    const std::regex methodDefinition(R"(\w+::\w+\s*\([^)]*\)\s*(?:const\s*)?\{)");
    const auto begin = std::sregex_iterator(source.begin(), source.end(), methodDefinition);
    const auto end = std::sregex_iterator();
    return std::regex_search(source, constructorOnly) && std::distance(begin, end) <= 1;
}

} // namespace

TEST(OopArchitectureGuardrailTests, SourceCppFilesAreNotEmptyOrConstructorOnly) {
    for (const std::filesystem::path& file : filesWithExtension(sourceRoot() / "src", ".cpp")) {
        const std::string source = readFile(file);
        const std::string relative = normalizedRelative(file);

        EXPECT_FALSE(source.empty()) << relative;
        EXPECT_FALSE(looksConstructorOnlyCpp(source)) << relative;
    }
}

TEST(OopArchitectureGuardrailTests, EverySourceCppIsPartOfCMakeSourceTruth) {
    const std::string cmake = readFile(sourceRoot() / "CMakeLists.txt");

    for (const std::filesystem::path& file : filesWithExtension(sourceRoot() / "src", ".cpp")) {
        const std::string relative = normalizedRelative(file);
        EXPECT_NE(cmake.find(relative), std::string::npos) << relative;
    }
}

TEST(OopArchitectureGuardrailTests, UserFacingToolsAreNotPassThroughAliases) {
    for (const std::filesystem::path& file : filesWithExtension(sourceRoot() / "src/modeling/tools", ".h")) {
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
                 sourceRoot() / "src/modeling/tools/ModelingCommandToolCatalog.h",
                 sourceRoot() / "src/viewport/tools/ViewportToolCatalog.h",
         }) {
        const std::string source = readFile(file);
        const std::string relative = normalizedRelative(file);
        EXPECT_EQ(source.find("static const"), std::string::npos) << relative;
        EXPECT_EQ(source.find("inline std::span"), std::string::npos) << relative;
        EXPECT_EQ(source.find("std::array"), std::string::npos) << relative;
    }
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

TEST(OopArchitectureGuardrailTests, ViewportPanelDoesNotOwnToolImplementations) {
    const std::string source = readFile(sourceRoot() / "src/ui/ViewportPanel.cpp");

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

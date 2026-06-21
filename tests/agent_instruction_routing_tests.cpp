#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
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

std::string readFile(const std::filesystem::path& relativePath) {
    const std::filesystem::path path = sourceRoot() / relativePath;
    std::ifstream file(path);
    EXPECT_TRUE(file.is_open()) << path.string();
    if (!file.is_open()) {
        return {};
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::vector<std::string_view> requiredInstructionFiles() {
    return {
            "agents/README.md",
            "agents/project-identity.md",
            "agents/public-api-and-scripting.md",
            "agents/domain-ownership.md",
            "agents/oop-architecture.md",
            "agents/sdk-unity-parity.md",
            "agents/modeling-tools.md",
            "agents/viewport-tools.md",
            "agents/rendering-overlays.md",
            "agents/ui-localization.md",
            "agents/testing-build.md",
            "agents/dependencies.md",
            "agents/docs-public-examples.md",
    };
}

bool contains(const std::string& source, std::string_view needle) {
    return source.find(needle) != std::string::npos;
}

} // namespace

TEST(AgentInstructionRoutingTests, RootAgentsFileIsThinRouterToDomainInstructions) {
    const std::string root = readFile("AGENTS.md");

    EXPECT_LT(root.size(), 8000u);
    EXPECT_TRUE(contains(root, "agents/project-identity.md"));
    EXPECT_TRUE(contains(root, "agents/public-api-and-scripting.md"));
    EXPECT_TRUE(contains(root, "agents/domain-ownership.md"));
    EXPECT_TRUE(contains(root, "agents/testing-build.md"));
    EXPECT_TRUE(contains(root, "Domain Routing"));
}

TEST(AgentInstructionRoutingTests, RequiredDomainInstructionFilesExistAndAreRouted) {
    const std::string root = readFile("AGENTS.md");

    for (const std::string_view relative : requiredInstructionFiles()) {
        EXPECT_TRUE(std::filesystem::exists(sourceRoot() / relative)) << relative;
        EXPECT_TRUE(contains(root, relative) || relative == "agents/README.md") << relative;
    }
}

TEST(AgentInstructionRoutingTests, PublicApiPolicyIsFirstClassInstruction) {
    const std::string publicApi = readFile("agents/public-api-and-scripting.md");

    EXPECT_TRUE(contains(publicApi, "QuaderApi"));
    EXPECT_TRUE(contains(publicApi, "QuaderEditorApi"));
    EXPECT_TRUE(contains(publicApi, "Lua"));
    EXPECT_TRUE(contains(publicApi, "Add To Public API When"));
    EXPECT_TRUE(contains(publicApi, "Keep Low-Level When"));
    EXPECT_TRUE(contains(publicApi, "Lua bindings must not expose"));
}

TEST(AgentInstructionRoutingTests, StaleOopSourceOfTruthIsNotReferenced) {
    const std::vector<std::filesystem::path> files = {
            "AGENTS.md",
            "agents/README.md",
            "agents/project-identity.md",
            "agents/public-api-and-scripting.md",
            "agents/domain-ownership.md",
            "agents/oop-architecture.md",
            "agents/sdk-unity-parity.md",
            "agents/modeling-tools.md",
            "agents/viewport-tools.md",
            "agents/rendering-overlays.md",
            "agents/ui-localization.md",
            "agents/testing-build.md",
            "agents/dependencies.md",
            "agents/docs-public-examples.md",
            "programmatic-api-plan.md",
    };

    for (const std::filesystem::path& file : files) {
        const std::string source = readFile(file);
        EXPECT_FALSE(contains(source, "oop-refactor.md")) << file.generic_string();
        EXPECT_FALSE(contains(source, "Progress Ledger")) << file.generic_string();
    }
}

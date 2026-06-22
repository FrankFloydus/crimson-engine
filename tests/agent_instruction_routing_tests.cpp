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

TEST(AgentInstructionRoutingTests, AppRefactorSupersedesCopiedSdkBoundary) {
    const std::string projectIdentity = readFile("agents/project-identity.md");
    const std::string domainOwnership = readFile("agents/domain-ownership.md");
    const std::string testingBuild = readFile("agents/testing-build.md");

    EXPECT_TRUE(contains(projectIdentity, "app-refactor.md"));
    EXPECT_TRUE(contains(projectIdentity, "Do not preserve the old top-level copied SDK boundary"));
    EXPECT_TRUE(contains(projectIdentity, "compile-time SDK presence macro"));
    EXPECT_TRUE(contains(domainOwnership, "Former SDK geometry, polygon, editor-core"));

    for (const std::string& source : {projectIdentity, domainOwnership, testingBuild}) {
        EXPECT_FALSE(contains(source, "preserve the SDK boundary"));
        EXPECT_FALSE(contains(source, "copied SDK tests"));
        EXPECT_FALSE(contains(source, "vs2026-sdk-tests"));
    }
}

TEST(AgentInstructionRoutingTests, AgentInstructionsStayNative) {
    const std::vector<std::filesystem::path> files = {
            "AGENTS.md",
            "agents/README.md",
            "agents/project-identity.md",
            "agents/public-api-and-scripting.md",
            "agents/domain-ownership.md",
            "agents/oop-architecture.md",
            "agents/modeling-tools.md",
            "agents/viewport-tools.md",
            "agents/rendering-overlays.md",
            "agents/ui-localization.md",
            "agents/testing-build.md",
            "agents/dependencies.md",
            "agents/docs-public-examples.md",
            "programmatic-api-plan.md",
    };

    const std::string retiredInstructionName = std::string("sdk-") + "un" + "ity-" + "par" + "ity.md";
    const std::string retiredInstruction = std::string("agents/") + retiredInstructionName;
    const std::string retiredCheckout = std::string("quader-") + "un" + "ity";
    const std::string retiredBehaviorLabel = std::string("Un") + "ity " + "par" + "ity";
    const std::string retiredPluginLabel = std::string("Un") + "ity " + "plugin";

    EXPECT_FALSE(std::filesystem::exists(sourceRoot() / retiredInstruction));

    for (const std::filesystem::path& file : files) {
        const std::string source = readFile(file);
        EXPECT_FALSE(contains(source, retiredInstructionName)) << file.generic_string();
        EXPECT_FALSE(contains(source, retiredInstruction)) << file.generic_string();
        EXPECT_FALSE(contains(source, retiredCheckout)) << file.generic_string();
        EXPECT_FALSE(contains(source, retiredBehaviorLabel)) << file.generic_string();
        EXPECT_FALSE(contains(source, retiredPluginLabel)) << file.generic_string();
    }
}

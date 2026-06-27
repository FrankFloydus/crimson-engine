////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "support/guardrail_file_scan.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace {

using quader::test::guardrail::contains;
using quader::test::guardrail::filesNamedUnder;
using quader::test::guardrail::normalizedRelative;
using quader::test::guardrail::readSourceFile;
using quader::test::guardrail::sourceRoot;

std::vector<std::string> requiredInstructionFiles() {
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
            "INPUT_KEYBINDINGS.md",
    };
}

std::vector<std::string> discoveredNestedInstructionFiles() {
    std::vector<std::string> files;
    for (const std::filesystem::path& file : filesNamedUnder(sourceRoot(), "AGENTS.md")) {
        const std::string relative = normalizedRelative(file);
        if (relative != "AGENTS.md") {
            files.push_back(relative);
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::vector<std::string> routedInstructionFiles() {
    std::vector<std::string> files = {"AGENTS.md"};
    const std::vector<std::string> required = requiredInstructionFiles();
    files.insert(files.end(), required.begin(), required.end());
    const std::vector<std::string> nested = discoveredNestedInstructionFiles();
    files.insert(files.end(), nested.begin(), nested.end());
    return files;
}

std::vector<std::string> retiredRefactorPlanFiles() {
    return {
            std::string("app-") + "refactor.md",
            std::string("oop-") + "refactor.md",
            std::string("programmatic-api-") + "plan.md",
    };
}

} // namespace

TEST(AgentInstructionRoutingTests, RootAgentsFileIsThinRouterToDomainInstructions) {
    const std::string root = readSourceFile("AGENTS.md");

    EXPECT_LT(root.size(), 8000u);
    EXPECT_TRUE(contains(root, "agents/project-identity.md"));
    EXPECT_TRUE(contains(root, "agents/public-api-and-scripting.md"));
    EXPECT_TRUE(contains(root, "agents/domain-ownership.md"));
    EXPECT_TRUE(contains(root, "agents/testing-build.md"));
    EXPECT_TRUE(contains(root, "Domain Routing"));
}

TEST(AgentInstructionRoutingTests, RequiredDomainInstructionFilesExistAndAreRouted) {
    const std::string root = readSourceFile("AGENTS.md");

    for (const std::string& relative : requiredInstructionFiles()) {
        EXPECT_TRUE(std::filesystem::exists(sourceRoot() / relative)) << relative;
        EXPECT_TRUE(contains(root, relative) || relative == "agents/README.md") << relative;
    }
}

TEST(AgentInstructionRoutingTests, NestedPackageInstructionFilesExistAndStayFocused) {
    const std::string root = readSourceFile("AGENTS.md");

    EXPECT_TRUE(contains(root, "Nested Package Routing"));
    EXPECT_TRUE(contains(root, "src/<package>/AGENTS.md"));
    EXPECT_TRUE(contains(root, "nearest nested `AGENTS.md`"));

    const std::vector<std::string> nested = discoveredNestedInstructionFiles();
    EXPECT_FALSE(nested.empty());

    for (const std::string& relative : nested) {
        const std::string source = readSourceFile(relative);
        EXPECT_FALSE(source.empty()) << relative;
        EXPECT_LT(source.size(), 2500u) << relative;
        EXPECT_TRUE(contains(source, "# ")) << relative;
    }
}

TEST(AgentInstructionRoutingTests, ArchitectureGuidanceLivesInDomainInstructions) {
    const std::string projectIdentity = readSourceFile("agents/project-identity.md");
    const std::string publicApi = readSourceFile("agents/public-api-and-scripting.md");

    EXPECT_TRUE(contains(projectIdentity, "native standalone Windows Quader app"));
    EXPECT_TRUE(contains(projectIdentity, "Do not preserve the old top-level copied SDK boundary"));
    EXPECT_TRUE(contains(publicApi, "QuaderEditorApi"));
    EXPECT_TRUE(contains(publicApi, "Add To Public API When"));
}

TEST(AgentInstructionRoutingTests, PublicApiPolicyIsFirstClassInstruction) {
    const std::string publicApi = readSourceFile("agents/public-api-and-scripting.md");

    EXPECT_TRUE(contains(publicApi, "QuaderApi"));
    EXPECT_TRUE(contains(publicApi, "QuaderEditorApi"));
    EXPECT_TRUE(contains(publicApi, "Lua"));
    EXPECT_TRUE(contains(publicApi, "Add To Public API When"));
    EXPECT_TRUE(contains(publicApi, "Keep Low-Level When"));
    EXPECT_TRUE(contains(publicApi, "Lua bindings must not expose"));
}

TEST(AgentInstructionRoutingTests, InputKeybindingGuidanceIsFirstClassInstruction) {
    const std::string input = readSourceFile("INPUT_KEYBINDINGS.md");

    EXPECT_TRUE(contains(input, "source-of-truth map for Quader input"));
    EXPECT_TRUE(contains(input, "QuaderKeybindingService"));
    EXPECT_TRUE(contains(input, "context keys"));
    EXPECT_TRUE(contains(input, "Do not bypass these owners"));
}

TEST(AgentInstructionRoutingTests, StaleOopSourceOfTruthIsNotReferenced) {
    for (const std::filesystem::path& file : routedInstructionFiles()) {
        const std::string source = readSourceFile(file);
        for (const std::string& retiredPlan : retiredRefactorPlanFiles()) {
            EXPECT_FALSE(contains(source, retiredPlan)) << file.generic_string();
        }
        EXPECT_FALSE(contains(source, "Progress Ledger")) << file.generic_string();
    }
}

TEST(AgentInstructionRoutingTests, ProjectIdentitySupersedesCopiedSdkBoundary) {
    const std::string projectIdentity = readSourceFile("agents/project-identity.md");
    const std::string domainOwnership = readSourceFile("agents/domain-ownership.md");
    const std::string testingBuild = readSourceFile("agents/testing-build.md");

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
    const std::string retiredInstructionName = std::string("sdk-") + "un" + "ity-" + "par" + "ity.md";
    const std::string retiredInstruction = std::string("agents/") + retiredInstructionName;
    const std::string retiredCheckout = std::string("quader-") + "un" + "ity";
    const std::string retiredBehaviorLabel = std::string("Un") + "ity " + "par" + "ity";
    const std::string retiredPluginLabel = std::string("Un") + "ity " + "plugin";

    EXPECT_FALSE(std::filesystem::exists(sourceRoot() / retiredInstruction));

    for (const std::filesystem::path& file : routedInstructionFiles()) {
        const std::string source = readSourceFile(file);
        EXPECT_FALSE(contains(source, retiredInstructionName)) << file.generic_string();
        EXPECT_FALSE(contains(source, retiredInstruction)) << file.generic_string();
        EXPECT_FALSE(contains(source, retiredCheckout)) << file.generic_string();
        EXPECT_FALSE(contains(source, retiredBehaviorLabel)) << file.generic_string();
        EXPECT_FALSE(contains(source, retiredPluginLabel)) << file.generic_string();
    }
}

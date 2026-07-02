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
using quader::test::guardrail::files_named_under;
using quader::test::guardrail::normalized_relative;
using quader::test::guardrail::read_source_file;
using quader::test::guardrail::source_root;

std::vector<std::string> required_instruction_files() {
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

std::vector<std::string> discovered_nested_instruction_files() {
  std::vector<std::string> files;
  for (const std::filesystem::path &file :
       files_named_under(source_root(), "AGENTS.md")) {
    const std::string relative = normalized_relative(file);
    if (relative != "AGENTS.md") {
      files.push_back(relative);
    }
  }
  std::sort(files.begin(), files.end());
  return files;
}

std::vector<std::string> routed_instruction_files() {
  std::vector<std::string> files = {"AGENTS.md"};
  const std::vector<std::string> required = required_instruction_files();
  files.insert(files.end(), required.begin(), required.end());
  const std::vector<std::string> nested = discovered_nested_instruction_files();
  files.insert(files.end(), nested.begin(), nested.end());
  return files;
}

std::vector<std::string> retired_refactor_plan_files() {
  return {
      std::string("app-") + "refactor.md",
      std::string("oop-") + "refactor.md",
      std::string("programmatic-api-") + "plan.md",
  };
}

} // namespace

TEST(AgentInstructionRoutingTests, RootAgentsFileIsThinRouterToDomainInstructions) {
  const std::string root = read_source_file("AGENTS.md");

  EXPECT_LT(root.size(), 8000u);
  EXPECT_TRUE(contains(root, "agents/project-identity.md"));
  EXPECT_TRUE(contains(root, "agents/public-api-and-scripting.md"));
  EXPECT_TRUE(contains(root, "agents/domain-ownership.md"));
  EXPECT_TRUE(contains(root, "agents/testing-build.md"));
  EXPECT_TRUE(contains(root, "Domain Routing"));
}

TEST(AgentInstructionRoutingTests, RequiredDomainInstructionFilesExistAndAreRouted) {
  const std::string root = read_source_file("AGENTS.md");

  for (const std::string &relative : required_instruction_files()) {
    EXPECT_TRUE(std::filesystem::exists(source_root() / relative)) << relative;
    EXPECT_TRUE(contains(root, relative) || relative == "agents/README.md")
        << relative;
  }
}

TEST(AgentInstructionRoutingTests, NestedPackageInstructionFilesExistAndStayFocused) {
  const std::string root = read_source_file("AGENTS.md");

  EXPECT_TRUE(contains(root, "Nested Package Routing"));
  EXPECT_TRUE(contains(root, "src/<package>/AGENTS.md"));
  EXPECT_TRUE(contains(root, "nearest nested `AGENTS.md`"));

  const std::vector<std::string> nested = discovered_nested_instruction_files();
  EXPECT_FALSE(nested.empty());

  for (const std::string &relative : nested) {
    const std::string source = read_source_file(relative);
    EXPECT_FALSE(source.empty()) << relative;
    EXPECT_LT(source.size(), 2500u) << relative;
    EXPECT_TRUE(contains(source, "# ")) << relative;
  }
}

TEST(AgentInstructionRoutingTests, ArchitectureGuidanceLivesInDomainInstructions) {
  const std::string project_identity =
      read_source_file("agents/project-identity.md");
  const std::string public_api =
      read_source_file("agents/public-api-and-scripting.md");

  EXPECT_TRUE(
      contains(project_identity, "native standalone Windows Quader app"));
  EXPECT_TRUE(
      contains(project_identity,
               "Do not preserve the old top-level copied SDK boundary"));
  EXPECT_TRUE(contains(public_api, "QuaderEditorApi"));
  EXPECT_TRUE(contains(public_api, "Add To Public API When"));
}

TEST(AgentInstructionRoutingTests, PublicApiPolicyIsFirstClassInstruction) {
  const std::string public_api =
      read_source_file("agents/public-api-and-scripting.md");

  EXPECT_TRUE(contains(public_api, "QuaderApi"));
  EXPECT_TRUE(contains(public_api, "QuaderEditorApi"));
  EXPECT_TRUE(contains(public_api, "Lua"));
  EXPECT_TRUE(contains(public_api, "Add To Public API When"));
  EXPECT_TRUE(contains(public_api, "Keep Low-Level When"));
  EXPECT_TRUE(contains(public_api, "Lua bindings must not expose"));
}

TEST(AgentInstructionRoutingTests, InputKeybindingGuidanceIsFirstClassInstruction) {
  const std::string input = read_source_file("INPUT_KEYBINDINGS.md");

  EXPECT_TRUE(contains(input, "source-of-truth map for Quader input"));
  EXPECT_TRUE(contains(input, "QuaderKeybindingService"));
  EXPECT_TRUE(contains(input, "context keys"));
  EXPECT_TRUE(contains(input, "Do not bypass these owners"));
}

TEST(AgentInstructionRoutingTests, StaleOopSourceOfTruthIsNotReferenced) {
  for (const std::filesystem::path &file : routed_instruction_files()) {
    const std::string source = read_source_file(file);
    for (const std::string &retired_plan : retired_refactor_plan_files()) {
      EXPECT_FALSE(contains(source, retired_plan)) << file.generic_string();
    }
    EXPECT_FALSE(contains(source, "Progress Ledger")) << file.generic_string();
  }
}

TEST(AgentInstructionRoutingTests, ProjectIdentitySupersedesCopiedSdkBoundary) {
  const std::string project_identity =
      read_source_file("agents/project-identity.md");
  const std::string domain_ownership =
      read_source_file("agents/domain-ownership.md");
  const std::string testing_build = read_source_file("agents/testing-build.md");

  EXPECT_TRUE(
      contains(project_identity,
               "Do not preserve the old top-level copied SDK boundary"));
  EXPECT_TRUE(contains(project_identity, "compile-time SDK presence macro"));
  EXPECT_TRUE(
      contains(domain_ownership, "Former SDK geometry, polygon, editor-core"));

  for (const std::string &source :
       {project_identity, domain_ownership, testing_build}) {
    EXPECT_FALSE(contains(source, "preserve the SDK boundary"));
    EXPECT_FALSE(contains(source, "copied SDK tests"));
    EXPECT_FALSE(contains(source, "vs2026-sdk-tests"));
  }
}

TEST(AgentInstructionRoutingTests, AgentInstructionsStayNative) {
  const std::string retired_instruction_name =
      std::string("sdk-") + "un" + "ity-" + "par" + "ity.md";
  const std::string retired_instruction =
      std::string("agents/") + retired_instruction_name;
  const std::string retired_checkout = std::string("quader-") + "un" + "ity";
  const std::string retired_behavior_label =
      std::string("Un") + "ity " + "par" + "ity";
  const std::string retired_plugin_label =
      std::string("Un") + "ity " + "plugin";

  EXPECT_FALSE(std::filesystem::exists(source_root() / retired_instruction));

  for (const std::filesystem::path &file : routed_instruction_files()) {
    const std::string source = read_source_file(file);
    EXPECT_FALSE(contains(source, retired_instruction_name))
        << file.generic_string();
    EXPECT_FALSE(contains(source, retired_instruction))
        << file.generic_string();
    EXPECT_FALSE(contains(source, retired_checkout)) << file.generic_string();
    EXPECT_FALSE(contains(source, retired_behavior_label))
        << file.generic_string();
    EXPECT_FALSE(contains(source, retired_plugin_label))
        << file.generic_string();
  }
}

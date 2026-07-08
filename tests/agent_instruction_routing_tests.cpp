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
#include <vector>

namespace {

using quader::test::guardrail::contains;
using quader::test::guardrail::files_named_under;
using quader::test::guardrail::normalized_relative;
using quader::test::guardrail::read_source_file;
using quader::test::guardrail::source_root;

std::vector<std::string> required_root_instruction_files() {
  return {
      "AGENTS.md",
      "architecture-review.md",
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
  std::vector<std::string> files = required_root_instruction_files();
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

TEST(AgentInstructionRoutingTests, RootAgentsFileIsSelfContainedAuthority) {
  const std::string root = read_source_file("AGENTS.md");

  EXPECT_GT(root.size(), 8000u);
  EXPECT_TRUE(contains(root, "self-contained authority"));
  EXPECT_TRUE(contains(root, "## Project Overview"));
  EXPECT_TRUE(contains(root, "## Architecture Direction"));
  EXPECT_TRUE(contains(root, "## SDK Boundary Rules"));
  EXPECT_TRUE(contains(root, "## Domain Ownership"));
  EXPECT_TRUE(contains(root, "## Test Placement"));
  EXPECT_FALSE(contains(root, "agents/"));
  EXPECT_FALSE(contains(root, "docs/"));
}

TEST(AgentInstructionRoutingTests, RequiredRootInstructionFilesExist) {
  const std::string root = read_source_file("AGENTS.md");

  for (const std::string &relative : required_root_instruction_files()) {
    EXPECT_TRUE(std::filesystem::exists(source_root() / relative)) << relative;
  }

  EXPECT_TRUE(contains(root, "architecture-review.md"));
  EXPECT_TRUE(contains(root, "INPUT_KEYBINDINGS.md"));
}

TEST(AgentInstructionRoutingTests, NestedPackageInstructionFilesExistAndStayFocused) {
  const std::string root = read_source_file("AGENTS.md");

  EXPECT_TRUE(contains(root, "## Nested Package Routing"));
  EXPECT_TRUE(contains(root, "nearest `src/<package>/AGENTS.md`"));
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

TEST(AgentInstructionRoutingTests, ArchitectureGuidanceLivesInRootInstructions) {
  const std::string root = read_source_file("AGENTS.md");
  const std::string review = read_source_file("architecture-review.md");

  EXPECT_TRUE(contains(root, "native standalone Windows"));
  EXPECT_TRUE(contains(root,
                       "Do not preserve the old top-level copied SDK boundary"));
  EXPECT_TRUE(contains(root, "src/sdk/core/modeling"));
  EXPECT_TRUE(contains(root, "quader::modeling"));
  EXPECT_TRUE(contains(review, "Resolved Architecture Decisions"));
  EXPECT_TRUE(contains(review, "src/sdk/core/modeling"));
}

TEST(AgentInstructionRoutingTests, PublicApiPolicyIsFirstClassInstruction) {
  const std::string root = read_source_file("AGENTS.md");

  EXPECT_TRUE(contains(root, "QuaderApi"));
  EXPECT_TRUE(contains(root, "QuaderEditorApi"));
  EXPECT_TRUE(contains(root, "Lua"));
  EXPECT_TRUE(contains(root, "Add To Public API When"));
  EXPECT_TRUE(contains(root, "Keep Low-Level When"));
  EXPECT_TRUE(contains(root, "Lua bindings must not expose"));
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

TEST(AgentInstructionRoutingTests, ProjectIdentityAllowsAppOwnedSdkAndBansOldCopiedSdk) {
  const std::string root = read_source_file("AGENTS.md");
  const std::string review = read_source_file("architecture-review.md");

  EXPECT_TRUE(
      contains(root, "Do not preserve the old top-level copied SDK boundary"));
  EXPECT_TRUE(contains(root, "compile-time SDK presence macro"));
  EXPECT_TRUE(
      contains(root, "Former SDK geometry, polygon, editor-core"));
  EXPECT_TRUE(contains(root, "app-owned portable modeling core"));
  EXPECT_TRUE(contains(review, "Old copied SDK"));

  for (const std::string &source : {root, review}) {
    EXPECT_FALSE(contains(source, "preserve the SDK boundary"));
    EXPECT_FALSE(contains(source, "copied SDK tests"));
    EXPECT_FALSE(contains(source, "vs2026-sdk-tests"));
  }
}

TEST(AgentInstructionRoutingTests, AgentInstructionsStayNative) {
  const std::string retired_instruction_name =
      std::string("sdk-") + "un" + "ity-" + "par" + "ity.md";
  const std::string retired_checkout = std::string("quader-") + "un" + "ity";
  const std::string retired_behavior_label =
      std::string("Un") + "ity " + "par" + "ity";
  const std::string retired_plugin_label =
      std::string("Un") + "ity " + "plugin";

  EXPECT_FALSE(std::filesystem::exists(source_root() / retired_instruction_name));

  for (const std::filesystem::path &file : routed_instruction_files()) {
    const std::string source = read_source_file(file);
    EXPECT_FALSE(contains(source, retired_instruction_name))
        << file.generic_string();
    EXPECT_FALSE(contains(source, retired_checkout)) << file.generic_string();
    EXPECT_FALSE(contains(source, retired_behavior_label))
        << file.generic_string();
    EXPECT_FALSE(contains(source, retired_plugin_label))
        << file.generic_string();
  }
}

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
using quader::test::guardrail::split_lines;
using quader::test::guardrail::starts_with;

std::vector<std::string> required_root_instruction_files() {
  return {
      "AGENTS.md",
      "architecture-review.md",
      "REFACTORING_PLAN.md",
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
  EXPECT_TRUE(contains(root, "REFACTORING_PLAN.md"));
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
    EXPECT_LE(source.size(), 2500u) << relative;
    EXPECT_TRUE(contains(source, "# ")) << relative;
  }
}

TEST(AgentInstructionRoutingTests, ArchitectureGuidanceLivesInRootInstructions) {
  const std::string root = read_source_file("AGENTS.md");
  const std::string review = read_source_file("architecture-review.md");
  const std::string tracker = read_source_file("REFACTORING_PLAN.md");

  EXPECT_TRUE(contains(root, "native standalone Windows"));
  EXPECT_TRUE(contains(root,
                       "Do not preserve the old top-level copied SDK boundary"));
  EXPECT_TRUE(contains(root, "src/sdk/core/modeling"));
  EXPECT_TRUE(contains(root, "quader::modeling"));
  EXPECT_TRUE(contains(review, "Resolved Architecture Decisions"));
  EXPECT_TRUE(contains(review, "src/sdk/core/modeling"));
  EXPECT_TRUE(contains(tracker, "## 7. Target architecture"));
}

TEST(AgentInstructionRoutingTests,
     ArchitectureDirectionAndImplementationTrackerHaveOneAuthorityContract) {
  const std::string root = read_source_file("AGENTS.md");
  const std::string review = read_source_file("architecture-review.md");
  const std::string tracker = read_source_file("REFACTORING_PLAN.md");
  const std::string historical = read_source_file("refactor.md");

  EXPECT_TRUE(contains(root, "fixed migration direction"));
  EXPECT_TRUE(contains(root, "only executable refactoring tracker"));
  EXPECT_TRUE(contains(review, "fixed architecture-direction authority"));
  EXPECT_TRUE(contains(review, "Execution Authority And Legacy Crosswalk"));
  EXPECT_TRUE(contains(review, "Explicit legacy-task dispositions"));
  EXPECT_TRUE(contains(tracker, "Executable task/progress authority"));
  EXPECT_TRUE(contains(tracker, "sole executable task/progress tracker"));
  EXPECT_TRUE(contains(historical, "closed historical ledger"));
  EXPECT_TRUE(contains(historical, "not current implementation"));
  EXPECT_TRUE(contains(historical, "IDs and progress live only"));

  std::vector<std::string> legacy_rows;
  for (const std::string &line : split_lines(review)) {
    if (starts_with(line, "| AR-")) {
      legacy_rows.push_back(line);
    }
  }
  ASSERT_EQ(legacy_rows.size(), 11u);

  const std::vector<std::string> expected_statuses = {
      "Complete", "Complete", "Complete", "Complete", "Complete", "Complete",
      "Complete", "Complete", "Complete", "Complete", "Complete",
  };
  for (int phase = 0; phase <= 10; ++phase) {
    const std::string phase_label = "| AR-" + std::to_string(phase) + " ";
    EXPECT_TRUE(starts_with(legacy_rows[static_cast<std::size_t>(phase)],
                            phase_label))
        << phase;
    EXPECT_TRUE(contains(legacy_rows[static_cast<std::size_t>(phase)],
                         "| " + expected_statuses[static_cast<std::size_t>(phase)] +
                             " |"))
        << phase;
    EXPECT_TRUE(contains(legacy_rows[static_cast<std::size_t>(phase)], "P"))
        << "legacy row has no canonical task owner: " << phase;
  }

  std::vector<std::string> action_rows;
  for (const std::string &line : split_lines(review)) {
    if (starts_with(line, "- **AR-") && contains(line, " actions:**")) {
      action_rows.push_back(line);
    }
  }
  ASSERT_EQ(action_rows.size(), 11u);
  const std::vector<int> expected_action_counts = {
      6, 7, 6, 7, 8, 5, 4, 9, 4, 8, 4,
  };
  for (int phase = 0; phase <= 10; ++phase) {
    const std::string action_label =
        "- **AR-" + std::to_string(phase) + " actions:**";
    const std::string &action_row =
        action_rows[static_cast<std::size_t>(phase)];
    EXPECT_TRUE(starts_with(action_row, action_label)) << phase;
    EXPECT_TRUE(contains(action_row, "P") || contains(action_row, "Complete"))
        << "legacy actions have no task or completed disposition: " << phase;

    const std::size_t action_start = review.find(action_label);
    ASSERT_NE(action_start, std::string::npos) << phase;
    std::size_t action_end =
        review.find("\n- **AR-", action_start + action_label.size());
    if (action_end == std::string::npos) {
      action_end = review.find("\n### Explicit legacy-task dispositions",
                               action_start + action_label.size());
    }
    ASSERT_NE(action_end, std::string::npos) << phase;
    const std::string action_block =
        review.substr(action_start, action_end - action_start);
    for (int action = 1;
         action <= expected_action_counts[static_cast<std::size_t>(phase)];
         ++action) {
      EXPECT_TRUE(contains(action_block, "A" + std::to_string(action)))
          << "legacy action lacks a disposition: AR-" << phase << "."
          << action;
    }
  }

  for (const std::string &disposition : {
           "AR-4 OBJ writer",
           "AR-6 picking and repeat ownership",
           "AR-7 general `UIContext` decomposition",
           "AR-8 speculative picking/preview caches",
           "AR-9 blanket golden/fuzz expansion",
       }) {
    EXPECT_TRUE(contains(review, disposition)) << disposition;
  }

  EXPECT_FALSE(contains(review, "active deterministic plan"));
  EXPECT_FALSE(contains(review, "active architecture migration plan"));
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
  const std::string root = read_source_file("AGENTS.md");
  const std::string input = read_source_file("INPUT_KEYBINDINGS.md");

  EXPECT_TRUE(contains(input, "source-of-truth map for Quader input"));
  EXPECT_TRUE(contains(input, "QuaderKeybindingService"));
  EXPECT_TRUE(contains(input, "context keys"));
  EXPECT_TRUE(contains(input, "Do not bypass these owners"));
  EXPECT_TRUE(contains(
      root,
      "Treat command lifecycle and default-keybinding lifecycle as one change"));
  EXPECT_TRUE(contains(
      root, "Every new command must deliberately receive a default"));
  EXPECT_TRUE(contains(
      root, "removed or renamed command must leave no stale default"));
  EXPECT_TRUE(contains(root, "a changed default must"));
  EXPECT_TRUE(contains(root, "preserve existing user overrides"));
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

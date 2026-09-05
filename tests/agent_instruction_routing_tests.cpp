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
#include <regex>
#include <string>
#include <vector>

namespace {

using quader::test::guardrail::contains;
using quader::test::guardrail::normalized_relative;
using quader::test::guardrail::read_source_file;
using quader::test::guardrail::source_root;

std::vector<std::string> required_root_instruction_files() {
  return {
      "AGENTS.md",
      "architecture-review.md",
      "INPUT_KEYBINDINGS.md",
      "memory.md",
      "tasklist.md",
  };
}

std::vector<std::string> project_documentation_files() {
  std::vector<std::string> files;
  for (const std::filesystem::directory_entry &entry :
       std::filesystem::directory_iterator(source_root())) {
    if (entry.is_regular_file() && entry.path().extension() == ".md") {
      files.push_back(normalized_relative(entry.path()));
    }
  }
  // Avoid generated build trees, dependency checkouts, and user-local files.
  for (const char *directory :
       {"src", "tests", "tools", "cmake", "examples", "resources",
        "benchmarks", "thirdparty"}) {
    for (std::filesystem::recursive_directory_iterator it(source_root() / directory), end;
         it != end; ++it) {
      const std::string relative = normalized_relative(it->path());
      if (it->is_directory() &&
          (relative == "src/sdk/core/modeling/internal/thirdparty" ||
           (relative.starts_with("thirdparty/") &&
            !relative.starts_with("thirdparty/filament")))) {
        it.disable_recursion_pending();
        continue;
      }
      if (it->is_regular_file() && it->path().extension() == ".md") {
        files.push_back(relative);
      }
    }
  }
  std::sort(files.begin(), files.end());
  return files;
}

std::vector<std::string> discovered_nested_instruction_files() {
  std::vector<std::string> files;
  for (const std::string &relative : project_documentation_files()) {
    if (relative != "AGENTS.md" &&
        std::filesystem::path(relative).filename() == "AGENTS.md") {
      files.push_back(relative);
    }
  }
  return files;
}

std::vector<std::string> retired_refactor_plan_files() {
  return {
      std::string("app-") + "refactor.md",
      std::string("oop-") + "refactor.md",
      std::string("programmatic-api-") + "plan.md",
      "REFACTORING_PLAN.md",
      "refactor.md",
      "ARCHITECTURE_CONSOLIDATION_PLAN.md",
      "ARCHITECTURE_CONSOLIDATION_STABILITY_HANDOFF.md",
  };
}

} // namespace

TEST(AgentInstructionRoutingTests, RootGuideRoutesToCurrentDocumentsAndOwners) {
  const std::string root = read_source_file("AGENTS.md");

  for (const std::string &relative : required_root_instruction_files()) {
    EXPECT_TRUE(std::filesystem::is_regular_file(source_root() / relative))
        << relative;
    if (relative != "AGENTS.md") {
      EXPECT_TRUE(contains(root, relative)) << relative;
    }
  }
  for (const char *owner :
       {"src/AGENTS.md", "tests/AGENTS.md", "tools/AGENTS.md"}) {
    EXPECT_TRUE(contains(root, owner)) << owner;
    EXPECT_TRUE(std::filesystem::is_regular_file(source_root() / owner)) << owner;
  }
}

TEST(AgentInstructionRoutingTests, NestedPackageInstructionsAreDiscoverable) {
  const std::vector<std::string> nested = discovered_nested_instruction_files();
  ASSERT_FALSE(nested.empty());
  for (const std::string &relative : nested) {
    const std::string source = read_source_file(relative);
    EXPECT_TRUE(contains(source, "# ")) << relative;
  }
}

TEST(AgentInstructionRoutingTests, LocalDocumentationLinksResolve) {
  const std::regex link(R"(\[[^\]]*\]\(<?([^\s)>]+)>?\))");
  for (const std::string &relative : project_documentation_files()) {
    const std::string source = read_source_file(relative);
    for (std::sregex_iterator it(source.begin(), source.end(), link), end;
         it != end; ++it) {
      std::string target = (*it)[1].str();
      if (target.find(':') != std::string::npos || target.starts_with('#')) {
        continue;
      }
      target = target.substr(0, target.find('#'));
      const std::filesystem::path resolved =
          source_root() / std::filesystem::path(relative).parent_path() / target;
      EXPECT_TRUE(std::filesystem::exists(resolved))
          << relative << " links to missing path " << target;
    }
  }
}

TEST(AgentInstructionRoutingTests, ArchitectureGuideNamesCurrentOwners) {
  const std::string review = read_source_file("architecture-review.md");
  for (const char *owner :
       {"src/sdk/core/modeling", "src/api", "src/editor", "src/scene",
        "src/assets", "src/io", "src/render", "src/viewport", "src/ui",
        "src/platform"}) {
    EXPECT_TRUE(contains(review, owner)) << owner;
    EXPECT_TRUE(std::filesystem::is_directory(source_root() / owner)) << owner;
  }
  for (const char *identity :
       {"quader::modeling", "QuaderEditorApi", "QuaderApi"}) {
    EXPECT_TRUE(contains(review, identity)) << identity;
  }
}

TEST(AgentInstructionRoutingTests, InputGuideKeepsCommandAndDefaultLifecycleTogether) {
  const std::string input = read_source_file("INPUT_KEYBINDINGS.md");
  for (const char *owner :
       {"QuaderKeybindingService", "ViewportInputRouter", "qt_input_adapter"}) {
    EXPECT_TRUE(contains(input, owner)) << owner;
  }
  // The executable command/default tests own exact mappings. Documentation
  // must retain the lifecycle decisions without freezing paragraph wording.
  for (const char *contract : {"unassigned", "removed", "renamed", "overrides"}) {
    EXPECT_TRUE(contains(input, contract)) << contract;
  }
}

TEST(AgentInstructionRoutingTests, CompletedPlansAreNotCurrentInstructions) {
  for (const std::string &retired_plan : retired_refactor_plan_files()) {
    EXPECT_FALSE(std::filesystem::exists(source_root() / retired_plan))
        << retired_plan;
  }
  for (const std::string &relative : project_documentation_files()) {
    const std::string source = read_source_file(relative);
    for (const std::string &retired_plan : retired_refactor_plan_files()) {
      EXPECT_FALSE(contains(source, retired_plan))
          << relative << " references retired plan " << retired_plan;
    }
  }
}

TEST(AgentInstructionRoutingTests, AgentInstructionsStayNative) {
  const std::vector<std::string> retired_names = {
      std::string("sdk-") + "un" + "ity-" + "par" + "ity.md",
      std::string("quader-") + "un" + "ity",
      std::string("Un") + "ity " + "par" + "ity",
      std::string("Un") + "ity " + "plugin",
      "vs2026-sdk-tests",
  };
  for (const std::string &relative : project_documentation_files()) {
    const std::string source = read_source_file(relative);
    for (const std::string &retired_name : retired_names) {
      EXPECT_FALSE(contains(source, retired_name)) << relative;
    }
  }
}

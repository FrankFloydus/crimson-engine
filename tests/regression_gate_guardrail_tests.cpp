////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "support/guardrail_file_scan.hpp"

#include <string>

namespace {

using quader::test::guardrail::contains;
using quader::test::guardrail::read_source_file;

} // namespace

TEST(RegressionGateGuardrailTests,
     GateBuildsEveryTestOwnerRunsFullCtestAndBuildsTheNativeAppLast) {
  const std::string gate = read_source_file("tools/regression_gate.ps1");

  EXPECT_TRUE(contains(gate, "cmake --preset vs2026"));
  EXPECT_TRUE(contains(gate, "quader_guardrail_tests"));
  EXPECT_TRUE(contains(gate, "quader_tests"));
  EXPECT_TRUE(contains(gate, "quader_modeling_domain_tests"));
  EXPECT_TRUE(contains(gate, "quader_modeling_sdk_consumer_tests"));
  EXPECT_TRUE(contains(gate, "quader_modeling_sdk_parity_tests"));
  EXPECT_TRUE(
      contains(gate, "ctest --preset vs2026-debug --output-on-failure"));
  EXPECT_TRUE(
      contains(gate, "cmake --build --preset vs2026-debug --target quader"));

  const std::size_t ctest = gate.find("ctest --preset vs2026-debug");
  const std::size_t native_build =
      gate.find("cmake --build --preset vs2026-debug --target quader");
  ASSERT_NE(ctest, std::string::npos);
  ASSERT_NE(native_build, std::string::npos);
  EXPECT_LT(ctest, native_build);
}

TEST(RegressionGateGuardrailTests,
     GateCannotSkipTestsOrRewriteApprovedBehaviorBaselines) {
  const std::string gate = read_source_file("tools/regression_gate.ps1");

  EXPECT_FALSE(contains(gate, "SkipTests"));
  EXPECT_FALSE(contains(gate, "UpdateBaseline"));
  EXPECT_TRUE(contains(gate, "QUADER_UPDATE_COMMAND_BEHAVIOR_BASELINE"));
  EXPECT_TRUE(contains(gate, "QUADER_COMMAND_BEHAVIOR_CHANGE_REASON"));
  EXPECT_TRUE(contains(gate, "The regression gate is read-only"));
  EXPECT_TRUE(contains(gate, "$ErrorActionPreference = 'Stop'"));
  EXPECT_TRUE(contains(gate, "throw \"Regression gate step failed"));
}

TEST(RegressionGateGuardrailTests,
     BehaviorBaselineUpdatesRequireASeparateReasonedReviewPath) {
  const std::string updater =
      read_source_file("tools/update_command_behavior_baseline.ps1");

  EXPECT_TRUE(contains(updater, "[Parameter(Mandatory = $true)]"));
  EXPECT_TRUE(contains(updater, "[ValidateNotNullOrEmpty()]"));
  EXPECT_TRUE(contains(updater, "[ValidateScript("));
  EXPECT_TRUE(contains(updater, "IsNullOrWhiteSpace"));
  EXPECT_TRUE(contains(updater, "[string]$Reason"));
  EXPECT_TRUE(
      contains(updater, "QUADER_UPDATE_COMMAND_BEHAVIOR_BASELINE"));
  EXPECT_TRUE(contains(updater, "QUADER_COMMAND_BEHAVIOR_CHANGE_REASON"));
  EXPECT_TRUE(contains(updater, "Review the complete baseline diff"));
  EXPECT_FALSE(contains(updater, "--force"));
}

TEST(RegressionGateGuardrailTests,
     CommandBehaviorApprovalIsExplicitLawGatedAndAtomic) {
  const std::string contracts =
      read_source_file("tests/support/command_behavior_contract.cpp");
  const std::string behavior_test =
      read_source_file("tests/command_behavior_contract_tests.cpp");
  const std::string build = read_source_file("CMakeLists.txt");

  EXPECT_TRUE(contains(contracts, "Every id is intentionally written here"));
  EXPECT_TRUE(contains(contracts, "Do not replace this catalog with"));
  EXPECT_TRUE(contains(contracts, "static_assert(std::size(kContracts)"));
  EXPECT_TRUE(contains(behavior_test,
                       "EveryRegisteredCommandHasExactlyOneExplicitContract"));
  EXPECT_TRUE(contains(behavior_test,
                       "Refusing to approve fingerprints while behavior laws fail"));
  EXPECT_TRUE(contains(behavior_test, "ReplaceFileW"));
  EXPECT_TRUE(contains(behavior_test, "REPLACEFILE_WRITE_THROUGH"));
  EXPECT_TRUE(contains(behavior_test, "before_domains"));
  EXPECT_TRUE(contains(behavior_test, "after_domains"));
  EXPECT_TRUE(contains(build, "tests/command_behavior_contract_tests.cpp"));
  EXPECT_TRUE(contains(build, "tests/support/command_behavior_contract.cpp"));
  EXPECT_TRUE(contains(build, "tests/support/command_observable_state.cpp"));
}

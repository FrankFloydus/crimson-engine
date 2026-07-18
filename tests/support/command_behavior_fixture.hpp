////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "command_behavior_contract.hpp"

#include <cstdint>
#include <memory>

namespace quader {

class QuaderEditorApi;
class SceneSession;

namespace test {

/**
 * Owns one isolated, command-specific canonical behavior-test environment.
 */
class CommandBehaviorFixture final {
public:
  [[nodiscard]] static std::unique_ptr<CommandBehaviorFixture>
  create(const CommandBehaviorContract &contract);

  ~CommandBehaviorFixture();

  CommandBehaviorFixture(const CommandBehaviorFixture &) = delete;
  CommandBehaviorFixture &operator=(const CommandBehaviorFixture &) = delete;

  [[nodiscard]] QuaderEditorApi &editor();
  [[nodiscard]] const QuaderEditorApi &editor() const;
  [[nodiscard]] SceneSession &native_scene();
  [[nodiscard]] const SceneSession &native_scene() const;
  [[nodiscard]] std::uint64_t external_call_count() const;

private:
  class Impl;

  explicit CommandBehaviorFixture(std::unique_ptr<Impl> impl);

  std::unique_ptr<Impl> impl_;
};

} // namespace test
} // namespace quader

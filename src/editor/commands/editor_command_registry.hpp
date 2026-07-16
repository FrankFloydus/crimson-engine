////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_editor_api_types.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

/**
 * Represents an Editor Command Registration value used by the editor coordination layer.
 */
struct EditorCommandRegistration {
    std::string id;
    std::string label;
    std::string shortcut;
    bool visible = true;
    std::function<QuaderApiVoidResult()> execute;
    std::function<bool()> enabled;
};

/**
 * Owns Editor Command Registry lookup and registration for the editor coordination layer.
 */
class EditorCommandRegistry final {
public:
  void register_command(EditorCommandRegistration command);

  [[nodiscard]] bool contains(std::string_view command_id) const;
  [[nodiscard]] std::optional<QuaderApiVoidResult>
  execute(std::string_view command_id) const;
  [[nodiscard]] std::optional<QuaderCommandDescriptor>
  descriptor(std::string_view command_id) const;
  [[nodiscard]] std::vector<QuaderCommandDescriptor> available() const;
  [[nodiscard]] std::string shortcut_for(std::string_view command_id) const;

private:
  [[nodiscard]] const EditorCommandRegistration *
  find(std::string_view command_id) const;

  std::vector<EditorCommandRegistration> commands_;
};

} // namespace quader

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/commands/editor_command_registry.hpp"

#include "diagnostics/profile.hpp"

#include <algorithm>
#include <utility>

namespace quader {
namespace {

QuaderCommandDescriptor
make_descriptor(const EditorCommandRegistration &command) {
  return {
      .id = command.id,
      .label = command.label,
      .shortcut = command.shortcut,
      .enabled = [&]() {
        QDR_PROFILE_SCOPE("Editor command availability predicate");
        QDR_PROFILE_NAME(command.id.data(), command.id.size());
        return command.enabled ? command.enabled() : true;
      }(),
  };
}

} // namespace

void EditorCommandRegistry::register_command(
    EditorCommandRegistration command) {
  if (command.id.empty() || contains(command.id)) {
    return;
  }
  commands_.push_back(std::move(command));
}

bool EditorCommandRegistry::contains(std::string_view command_id) const {
  return find(command_id) != nullptr;
}

std::optional<QuaderApiVoidResult>
EditorCommandRegistry::execute(std::string_view command_id) const {
  QDR_PROFILE_FUNCTION();
  QDR_PROFILE_NAME(command_id.data(), command_id.size());
  if (const EditorCommandRegistration *command = find(command_id)) {
    if (command->enabled && !command->enabled()) {
      return QuaderApiVoidResult{
          .ok = false,
          .changed = false,
          .message = "Command is disabled.",
          .mutation_kind = QuaderMutationKind::None,
      };
    }
    if (!command->execute) {
      return QuaderApiVoidResult{
          .ok = false,
          .changed = false,
          .message = "Command has no handler.",
          .mutation_kind = QuaderMutationKind::None,
      };
    }
    QDR_PROFILE_SCOPE("Editor command handler");
    return command->execute();
  }

  return std::nullopt;
}

std::optional<QuaderCommandDescriptor>
EditorCommandRegistry::descriptor(std::string_view command_id) const {
  QDR_PROFILE_FUNCTION();
  QDR_PROFILE_NAME(command_id.data(), command_id.size());
  const EditorCommandRegistration *command = find(command_id);
  return command != nullptr && command->visible
             ? std::optional<QuaderCommandDescriptor>{make_descriptor(*command)}
             : std::nullopt;
}

std::vector<QuaderCommandDescriptor> EditorCommandRegistry::available() const {
    QDR_PROFILE_FUNCTION();
    std::vector<QuaderCommandDescriptor> result;
    result.reserve(commands_.size());
    for (const EditorCommandRegistration& command : commands_) {
        if (!command.visible) {
            continue;
        }
        result.push_back(make_descriptor(command));
    }
    return result;
}

std::string
EditorCommandRegistry::shortcut_for(std::string_view command_id) const {
  if (const EditorCommandRegistration *command = find(command_id)) {
    return command->shortcut;
  }
  return {};
}

const EditorCommandRegistration *
EditorCommandRegistry::find(std::string_view command_id) const {
  const auto found =
      std::find_if(commands_.begin(), commands_.end(),
                   [&](const EditorCommandRegistration &command) {
                     return command.id == command_id;
                   });
  return found == commands_.end() ? nullptr : &(*found);
}

} // namespace quader

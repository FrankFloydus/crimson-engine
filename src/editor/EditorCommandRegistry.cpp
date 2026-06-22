////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/EditorCommandRegistry.h"

#include <algorithm>
#include <utility>

namespace quader {

void EditorCommandRegistry::registerCommand(EditorCommandRegistration command) {
    if (command.id.empty() || contains(command.id)) {
        return;
    }
    commands_.push_back(std::move(command));
}

void EditorCommandRegistry::registerAlias(std::string aliasId, std::string targetId) {
    if (aliasId.empty() || targetId.empty() || contains(aliasId)) {
        return;
    }
    aliases_.emplace_back(std::move(aliasId), std::move(targetId));
}

void EditorCommandRegistry::registerPrefix(std::string prefix, PrefixHandler handler) {
    if (prefix.empty() || !handler) {
        return;
    }
    prefixes_.push_back(PrefixRoute{
            .prefix = std::move(prefix),
            .handler = std::move(handler),
    });
}

bool EditorCommandRegistry::contains(std::string_view commandId) const {
    return find(commandId) != nullptr ||
            std::any_of(aliases_.begin(), aliases_.end(), [&](const auto& alias) {
                return alias.first == commandId;
            });
}

std::optional<QuaderApiVoidResult> EditorCommandRegistry::execute(std::string_view commandId) const {
    if (const EditorCommandRegistration* command = find(commandId)) {
        if (command->enabled && !command->enabled()) {
            return QuaderApiVoidResult{
                    .ok = false,
                    .changed = false,
                    .message = "Command is disabled.",
                    .mutationKind = QuaderMutationKind::None,
            };
        }
        if (!command->execute) {
            return QuaderApiVoidResult{
                    .ok = false,
                    .changed = false,
                    .message = "Command has no handler.",
                    .mutationKind = QuaderMutationKind::None,
            };
        }
        return command->execute();
    }

    for (const PrefixRoute& route : prefixes_) {
        if (commandId.starts_with(route.prefix)) {
            return route.handler(commandId.substr(route.prefix.size()));
        }
    }
    return std::nullopt;
}

std::vector<QuaderCommandDescriptor> EditorCommandRegistry::available() const {
    std::vector<QuaderCommandDescriptor> result;
    result.reserve(commands_.size());
    for (const EditorCommandRegistration& command : commands_) {
        if (!command.visible) {
            continue;
        }
        result.push_back(QuaderCommandDescriptor{
                .id = command.id,
                .label = command.label,
                .shortcut = command.shortcut,
                .enabled = command.enabled ? command.enabled() : true,
        });
    }
    return result;
}

std::string EditorCommandRegistry::shortcutFor(std::string_view commandId) const {
    if (const EditorCommandRegistration* command = find(commandId)) {
        return command->shortcut;
    }
    return {};
}

const EditorCommandRegistration* EditorCommandRegistry::find(std::string_view commandId) const {
    commandId = resolveAlias(commandId);
    const auto found = std::find_if(commands_.begin(), commands_.end(), [&](const EditorCommandRegistration& command) {
        return command.id == commandId;
    });
    return found == commands_.end() ? nullptr : &(*found);
}

std::string_view EditorCommandRegistry::resolveAlias(std::string_view commandId) const {
    for (const auto& alias : aliases_) {
        if (alias.first == commandId) {
            return alias.second;
        }
    }
    return commandId;
}

} // namespace quader

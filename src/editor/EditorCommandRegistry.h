////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/QuaderEditorApiTypes.h"

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
    using PrefixHandler = std::function<QuaderApiVoidResult(std::string_view suffix)>;

    void registerCommand(EditorCommandRegistration command);
    void registerAlias(std::string aliasId, std::string targetId);
    void registerPrefix(std::string prefix, PrefixHandler handler);

    [[nodiscard]] bool contains(std::string_view commandId) const;
    [[nodiscard]] std::optional<QuaderApiVoidResult> execute(std::string_view commandId) const;
    [[nodiscard]] std::vector<QuaderCommandDescriptor> available() const;
    [[nodiscard]] std::string shortcutFor(std::string_view commandId) const;

private:
    /**
     * Represents a Prefix Route value used by the editor coordination layer.
     */
    struct PrefixRoute {
        std::string prefix;
        PrefixHandler handler;
    };

    [[nodiscard]] const EditorCommandRegistration* find(std::string_view commandId) const;
    [[nodiscard]] std::string_view resolveAlias(std::string_view commandId) const;

    std::vector<EditorCommandRegistration> commands_;
    std::vector<std::pair<std::string, std::string>> aliases_;
    std::vector<PrefixRoute> prefixes_;
};

} // namespace quader

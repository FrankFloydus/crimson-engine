////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/QuaderCommandIds.h"
#include "api/QuaderEditorApiTypes.h"
#include "editor/commands/EditorCommandRegistry.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

class QuaderApi;
class QuaderToolsApi;
class QuaderViewportApi;

/**
 * Exposes Quader Commands API operations through the public Quader API surface.
 */
class QuaderCommandsApi final {
public:
    QuaderCommandsApi(QuaderApi& api, QuaderViewportApi& viewports, QuaderToolsApi& tools);

    QuaderApiVoidResult execute(std::string_view commandId);
    QuaderApiVoidResult execute(QuaderCommandId commandId);
    [[nodiscard]] std::vector<QuaderCommandDescriptor> available() const;
    [[nodiscard]] std::string shortcutFor(std::string_view commandId) const;
    [[nodiscard]] std::string shortcutFor(QuaderCommandId commandId) const;

private:
    void registerCommands();
    void registerCommand(
            std::string id,
            std::string label,
            std::function<QuaderApiVoidResult()> execute,
            std::string shortcut = {},
            std::function<bool()> enabled = {},
            bool visible = true);
    void registerAlias(std::string aliasId, std::string targetId);
    [[nodiscard]] QuaderApiVoidResult failure(std::string message) const;

    QuaderApi& api_;
    QuaderViewportApi& viewports_;
    QuaderToolsApi& tools_;
    EditorCommandRegistry registry_;
};

} // namespace quader

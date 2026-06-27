////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_command_ids.hpp"
#include "api/quader_editor_api_types.hpp"

namespace quader {

class QuaderCommandsApi;

/**
 * Exposes Quader Preferences API operations through the public Quader API surface.
 */
class QuaderPreferencesApi final {
public:
    QuaderPreferencesApi();
    explicit QuaderPreferencesApi(QuaderCommandsApi& commands);

    void attachCommands(QuaderCommandsApi& commands);
    void setRuntimeHooks(QuaderPreferencesRuntimeHooks hooks);

    [[nodiscard]] std::string locale() const;
    QuaderApiVoidResult setLocale(std::string locale);
    QuaderApiVoidResult setShortcut(std::string_view commandId, std::string shortcut);
    QuaderApiVoidResult setShortcut(QuaderCommandId commandId, std::string shortcut);
    QuaderApiVoidResult removeShortcut(std::string_view commandId);
    QuaderApiVoidResult removeShortcut(QuaderCommandId commandId);
    QuaderApiVoidResult resetShortcut(std::string_view commandId);
    QuaderApiVoidResult resetShortcut(QuaderCommandId commandId);
    QuaderApiVoidResult setNavigationGizmoVisible(bool visible);
    [[nodiscard]] bool navigationGizmoVisible() const;
    QuaderApiVoidResult save();

private:
    [[nodiscard]] QuaderApiVoidResult unavailable(std::string message) const;

    QuaderPreferencesRuntimeHooks hooks_;
    QuaderCommandsApi* commands_ = nullptr;
};

} // namespace quader

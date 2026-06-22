////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/QuaderEditorApiTypes.h"

namespace quader {

/**
 * Exposes Quader Preferences API operations through the public Quader API surface.
 */
class QuaderPreferencesApi final {
public:
    QuaderPreferencesApi();

    void setRuntimeHooks(QuaderPreferencesRuntimeHooks hooks);

    [[nodiscard]] std::string locale() const;
    QuaderApiVoidResult setLocale(std::string locale);
    QuaderApiVoidResult setShortcut(std::string_view commandId, std::string shortcut);
    QuaderApiVoidResult setNavigationGizmoVisible(bool visible);
    [[nodiscard]] bool navigationGizmoVisible() const;
    QuaderApiVoidResult save();

private:
    [[nodiscard]] QuaderApiVoidResult unavailable(std::string message) const;

    QuaderPreferencesRuntimeHooks hooks_;
};

} // namespace quader

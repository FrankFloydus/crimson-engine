////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_editor_api_types.hpp"

namespace quader {

/**
 * Exposes Quader Console API operations through the public Quader API surface.
 */
class QuaderConsoleApi final {
public:
    QuaderApiVoidResult append(std::string message);
};

/**
 * Exposes Quader UI API operations through the public Quader API surface.
 */
class QuaderUIApi final {
public:
    QuaderUIApi();

    void set_runtime_hooks(QuaderUIRuntimeHooks hooks);

    QuaderApiVoidResult show_panel(std::string_view panel);
    QuaderApiVoidResult hide_panel(std::string_view panel);
    QuaderApiVoidResult focus_panel(std::string_view panel);
    [[nodiscard]] bool panel_visible(std::string_view panel) const;
    QuaderApiVoidResult notify(QuaderNotification notification);
    [[nodiscard]] QuaderConsoleApi& console();
    [[nodiscard]] const QuaderConsoleApi& console() const;

private:
  [[nodiscard]] QuaderApiVoidResult set_panel_visible(std::string_view panel,
                                                      bool visible);
  [[nodiscard]] QuaderApiVoidResult unavailable(std::string message) const;

  QuaderUIRuntimeHooks hooks_;
  QuaderConsoleApi console_;
};

} // namespace quader

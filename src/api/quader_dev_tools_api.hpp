////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#ifndef NDEBUG

#include "api/quader_api_result.hpp"
#include "api/quader_api_session.hpp"

#include <functional>
#include <string>
#include <string_view>

namespace quader {

/**
 * Native runtime hooks used by debug-only developer tools.
 */
struct QuaderDevToolsRuntimeHooks {
  std::function<bool(std::string_view, std::string &)> set_clipboard_text;
};

/**
 * Exposes debug-only tools for inspecting and sharing editor state.
 */
class QuaderDevToolsApi final {
public:
  explicit QuaderDevToolsApi(QuaderApiSession session);

  void set_runtime_hooks(QuaderDevToolsRuntimeHooks hooks);

  [[nodiscard]] std::string current_selection_json() const;
  QuaderApiVoidResult copy_selection_json_to_clipboard();

private:
  QuaderApiSession session_;
  QuaderDevToolsRuntimeHooks hooks_;
};

} // namespace quader

#endif

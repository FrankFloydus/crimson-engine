////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <string_view>

namespace quader {

/// Applies the immutable executable release to canonical native editor intent.
[[nodiscard]] bool release_allows_command(std::string_view id) noexcept;
/// UI discovery can omit advanced controls while their bound shortcuts remain usable.
[[nodiscard]] bool release_presents_command(std::string_view id) noexcept;
[[nodiscard]] bool release_allows_action(std::string_view id) noexcept;
[[nodiscard]] bool release_allows_operation(std::string_view id) noexcept;
[[nodiscard]] bool release_allows_tool(std::string_view id) noexcept;
[[nodiscard]] bool release_allows_input_target(std::string_view id) noexcept;

} // namespace quader

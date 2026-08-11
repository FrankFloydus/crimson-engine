////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <filesystem>

namespace quader {

/**
 * Opens the platform file manager and selects path when possible. If the path
 * cannot be selected, its parent directory is opened without invoking a shell
 * command parser.
 */
[[nodiscard]] bool
platform_reveal_file(const std::filesystem::path &path);

} // namespace quader

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <filesystem>

namespace quader {

// A native host may select its application directory before creating services.
// Quader's original default remains unchanged when no override is supplied.
void initialize_user_config_directory(std::filesystem::path directory);

[[nodiscard]] std::filesystem::path
quader_user_config_directory(const std::filesystem::path &executable_dir);

} // namespace quader

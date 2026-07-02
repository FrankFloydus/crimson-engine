////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "core/user_config_paths.hpp"

#include <cstdlib>

namespace quader {

std::filesystem::path
quader_user_config_directory(const std::filesystem::path &executable_dir) {
#ifdef _WIN32
  if (const char *local_app_data = std::getenv("LOCALAPPDATA")) {
    if (local_app_data[0] != '\0') {
      return std::filesystem::path(local_app_data) / "Quader";
    }
  }
#else
    if (const char* configHome = std::getenv("XDG_CONFIG_HOME")) {
        if (configHome[0] != '\0') {
            return std::filesystem::path(configHome) / "Quader";
        }
    }
#endif
  return executable_dir;
}

} // namespace quader

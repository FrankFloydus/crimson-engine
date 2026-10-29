////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "core/user_config_paths.hpp"
#include "core/release_features.hpp"

#include <cstdlib>
#include <stdexcept>
#include <utility>

namespace quader {
namespace {
std::filesystem::path application_directory;
}

void initialize_user_config_directory(std::filesystem::path directory){
  if(directory.empty()||!directory.is_absolute())throw std::invalid_argument("Application configuration needs an absolute directory.");
  if(!application_directory.empty()&&application_directory!=directory)throw std::logic_error("Application configuration was already initialized.");
  application_directory=std::move(directory);
}

std::filesystem::path
quader_user_config_directory(const std::filesystem::path &executable_dir) {
  if(!application_directory.empty())return application_directory;
  const bool public_release = current_product_release() != kInternalFullRelease;
#ifdef _WIN32
  if (const char *local_app_data = std::getenv("LOCALAPPDATA")) {
    if (local_app_data[0] != '\0') {
      const auto root = std::filesystem::path(local_app_data) / "Quader";
      return public_release ? root / "Public" : root;
    }
  }
#else
    if (const char* configHome = std::getenv("XDG_CONFIG_HOME")) {
        if (configHome[0] != '\0') {
            const auto root = std::filesystem::path(configHome) / "Quader";
            return public_release ? root / "Public" : root;
        }
    }
#endif
  return public_release ? executable_dir / "Public" : executable_dir;
}

} // namespace quader

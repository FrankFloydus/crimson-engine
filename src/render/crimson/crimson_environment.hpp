////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once
#include <crimson/renderer/forward/Environment.h>
#include <filesystem>
#include <string>

namespace quader {
// CPU-only preparation of the shared Quader environment catalog. Resource
// workers can coalesce this result for rich modeling, previews and thumbnails.
crimson::PreparedEnvironment prepare_crimson_packaged_environment(
    const std::filesystem::path& resources,std::string id);
}

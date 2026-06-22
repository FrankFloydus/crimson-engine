////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cstddef>
#include <filesystem>
#include <vector>

namespace quader {

/**
 * Coordinates Material Compiler Service responsibilities for the render extraction and Filament resource layer.
 */
class MaterialCompilerService {
public:
    static std::vector<std::byte> loadPackage(const std::filesystem::path& path);
};

} // namespace quader

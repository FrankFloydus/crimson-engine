#pragma once

#include <cstddef>
#include <filesystem>
#include <vector>

namespace quader {

class MaterialCompilerService {
public:
    static std::vector<std::byte> loadPackage(const std::filesystem::path& path);
};

} // namespace quader

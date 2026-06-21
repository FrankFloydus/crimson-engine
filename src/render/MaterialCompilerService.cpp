#include "render/MaterialCompilerService.h"

#include <fstream>
#include <stdexcept>

namespace quader {

std::vector<std::byte> MaterialCompilerService::loadPackage(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Unable to open material package: " + path.string());
    }

    const std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<std::byte> data(static_cast<std::size_t>(size));
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
        throw std::runtime_error("Unable to read material package: " + path.string());
    }

    return data;
}

} // namespace quader

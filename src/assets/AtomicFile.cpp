#include "AtomicFile.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace crimson {

void writeFileAtomically(const std::filesystem::path& path, std::string_view contents) {
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    static std::atomic<uint64_t> sequence{0};
    std::filesystem::path temporary;
    do {
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        temporary = path;
        temporary += "." + std::to_string(ticks) + "." + std::to_string(sequence.fetch_add(1)) + ".tmp";
    } while (std::filesystem::exists(temporary));
    try {
        {
            std::ofstream output(temporary,std::ios::binary | std::ios::trunc);
            if (!output) throw std::runtime_error("Cannot create the temporary file.");
            output.write(contents.data(),static_cast<std::streamsize>(contents.size()));
            output.flush();
            if (!output) throw std::runtime_error("Could not write the complete file.");
            output.close();
            if (!output) throw std::runtime_error("Could not close the temporary file.");
        }
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Could not publish the file (Windows error " + std::to_string(GetLastError()) + ").");
#else
        std::filesystem::rename(temporary,path);
#endif
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary,ignored);
        throw;
    }
}

} // namespace crimson

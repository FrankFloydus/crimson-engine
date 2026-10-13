#include "Diagnostics.h"

#include <lodepng.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace crimson {

Diagnostics::Diagnostics() : logFile_("renderer.log", std::ios::trunc) {}

void Diagnostics::log(const std::string& message) {
    std::lock_guard lock(mutex_);
    std::fprintf(stderr, "%s\n", message.c_str());
    if (logFile_) {
        logFile_ << message << '\n';
        logFile_.flush();
    }
}

void Diagnostics::fatal(const char* file, uint16_t line, bgfx::Fatal::Enum code, const char* message) {
    renderError_.store(true);
    log(std::string("bgfx ERROR: ") + message + " (" + file + ":" + std::to_string(line) + ")");
    if (code != bgfx::Fatal::DebugCheck) std::abort();
}

void Diagnostics::traceVargs(const char* file, uint16_t line, const char* format, va_list arguments) {
    char buffer[8192]{};
    std::vsnprintf(buffer, sizeof(buffer), format, arguments);
    log(std::string(file) + ":" + std::to_string(line) + " " + buffer);
}

void Diagnostics::screenShot(const char* path, uint32_t width, uint32_t height, uint32_t pitch,
                             const void* data, uint32_t size, bool flipY) {
    try {
        if (!data || !width || !height || uint64_t(width) * 4 > pitch ||
            uint64_t(pitch) * (height - 1) + uint64_t(width) * 4 > size) {
            throw std::runtime_error("Invalid screenshot buffer returned by bgfx.");
        }
        const auto* source = static_cast<const uint8_t*>(data);
        std::vector<uint8_t> rgba(size_t(width) * height * 4);
        for (uint32_t y = 0; y < height; ++y) {
            const auto* row = source + size_t(flipY ? height - 1 - y : y) * pitch;
            for (uint32_t x = 0; x < width; ++x) {
                const size_t target = (size_t(y) * width + x) * 4;
                rgba[target + 0] = row[x * 4 + 2];
                rgba[target + 1] = row[x * 4 + 1];
                rgba[target + 2] = row[x * 4 + 0];
                rgba[target + 3] = 255;
            }
        }
        const std::filesystem::path output(path);
        if (!output.parent_path().empty()) std::filesystem::create_directories(output.parent_path());
        // Encode in memory so the filesystem path retains Windows Unicode support.
        std::vector<uint8_t> png;
        const unsigned error = lodepng::encode(png, rgba, width, height);
        if (error) throw std::runtime_error(lodepng_error_text(error));
        std::ofstream image(output, std::ios::binary);
        image.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
        if (!image) throw std::runtime_error("Could not write screenshot: " + output.string());
        captureDone_.store(true);
        log("Screenshot saved: " + output.string());
    } catch (const std::exception& error) {
        captureFailed_.store(true);
        log(std::string("Screenshot ERROR: ") + error.what());
    }
}

} // namespace crimson

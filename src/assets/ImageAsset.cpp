module;

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

module quader.assets.image;

namespace quader {
namespace {

std::filesystem::path path_from_utf8(std::string_view utf8_path)
{
    std::u8string native_utf8;
    native_utf8.reserve(utf8_path.size());
    for (const char value : utf8_path) {
        native_utf8.push_back(static_cast<char8_t>(static_cast<unsigned char>(value)));
    }

    return std::filesystem::path(native_utf8);
}

std::vector<uint8_t> read_binary_file(std::string_view source_path)
{
    const std::filesystem::path path = path_from_utf8(source_path);
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Failed to open image file: " + std::string(source_path));
    }

    const std::ifstream::pos_type size = file.tellg();
    if (size <= 0) {
        throw std::runtime_error("Image file is empty: " + std::string(source_path));
    }

    if (size > static_cast<std::ifstream::pos_type>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("Image file is too large for the current decoder: " + std::string(source_path));
    }

    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!file) {
        throw std::runtime_error("Failed to read image file: " + std::string(source_path));
    }

    return bytes;
}

} // namespace

Rgba8Image load_rgba8_image_from_file(
    std::string_view source_path,
    ImageColorEncoding color_encoding)
{
    const std::vector<uint8_t> encoded_bytes = read_binary_file(source_path);

    int width = 0;
    int height = 0;
    int source_channels = 0;
    stbi_uc* decoded = stbi_load_from_memory(
        encoded_bytes.data(),
        static_cast<int>(encoded_bytes.size()),
        &width,
        &height,
        &source_channels,
        4);

    if (decoded == nullptr || width <= 0 || height <= 0) {
        const char* reason = stbi_failure_reason();
        throw std::runtime_error(
            "Failed to decode image file " + std::string(source_path) +
            ": " + (reason != nullptr ? reason : "unknown decoder error"));
    }

    const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> decoded_owner(decoded, &stbi_image_free);
    const size_t pixel_byte_count = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;

    Rgba8Image image;
    image.width = static_cast<uint32_t>(width);
    image.height = static_cast<uint32_t>(height);
    image.color_encoding = color_encoding;
    image.source_path = std::string(source_path);
    image.rgba.assign(decoded, decoded + pixel_byte_count);
    return image;
}

} // namespace quader

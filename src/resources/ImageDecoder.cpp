#include "ImageDecoder.h"
#include <bimg/decode.h>
#include <bx/allocator.h>
#include <bx/error.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
namespace crimson {
namespace {
std::string utf8(const std::filesystem::path& path) {
    const auto bytes=path.generic_u8string(); return {reinterpret_cast<const char*>(bytes.data()),bytes.size()};
}
std::string extension(const std::filesystem::path& path) {
    auto value=path.extension().string(); std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));}); return value;
}
}
DecodedImage decodeManagedImage(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    const auto length = input.tellg();
    if (!input || length <= 0 || static_cast<uint64_t>(length) > std::numeric_limits<uint32_t>::max())
        throw std::runtime_error("Could not read texture: " + utf8(path.filename()));
    std::vector<uint8_t> encoded(static_cast<size_t>(length));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
    if (!input) throw std::runtime_error("Texture file could not be read completely.");
    bx::DefaultAllocator allocator;
    bx::Error error;
    std::unique_ptr<bimg::ImageContainer, decltype(&bimg::imageFree)> image(
        bimg::imageParse(&allocator, encoded.data(), static_cast<uint32_t>(encoded.size()), bimg::TextureFormat::Count, &error), bimg::imageFree);
    const bool parsed = error.isOk();
    if (!parsed || !image || image->m_cubeMap || image->m_numLayers != 1 || image->m_depth > 1)
        throw std::runtime_error("Could not decode a 2D texture: " + utf8(path.filename()));
    const uint32_t limit = 16384;
    if (!image->m_width || !image->m_height || image->m_width > limit || image->m_height > limit)
        throw std::runtime_error("Texture dimensions exceed the GPU limit.");
    const auto sourceFormat = image->m_format;
    if (extension(path) == ".hdr") {
        // Keep HDR values until thumbnail tone mapping. The renderer decodes the
        // original file independently for environment preprocessing.
        if (sourceFormat != bimg::TextureFormat::RGBA32F) {
            auto* converted = bimg::imageConvert(&allocator, bimg::TextureFormat::RGBA32F, *image, false);
            if (!converted) throw std::runtime_error("HDR pixel conversion failed.");
            image.reset(converted);
        }
        bimg::ImageMip mip{};
        if (!bimg::imageGetRawData(*image, 0, 0, image->m_data, image->m_size, mip) ||
            mip.m_format != bimg::TextureFormat::RGBA32F)
            throw std::runtime_error("HDR decoder did not return floating point pixels.");
        const size_t channels = static_cast<size_t>(mip.m_width) * mip.m_height * 4;
        if (channels * sizeof(float) > mip.m_size) throw std::runtime_error("HDR pixel buffer is incomplete.");
        DecodedImage result{mip.m_width, mip.m_height, std::vector<uint8_t>(channels)};
        for (size_t index = 0; index < channels; ++index) {
            if (index % 4 == 3) { result.rgba[index] = 255; continue; }
            float value{};
            std::memcpy(&value, mip.m_data + index * sizeof(float), sizeof(value));
            value = std::isfinite(value) ? std::max(value, 0.0f) : 0.0f;
            const float mapped = value / (1.0f + value);
            const float srgb = mapped <= 0.0031308f ? 12.92f * mapped : 1.055f * std::pow(mapped, 1.0f / 2.4f) - 0.055f;
            result.rgba[index] = static_cast<uint8_t>(std::clamp(std::round(srgb * 255.0f), 0.0f, 255.0f));
        }
        return result;
    }
    const bool grayscale = sourceFormat == bimg::TextureFormat::R8 || sourceFormat == bimg::TextureFormat::R16 ||
                           sourceFormat == bimg::TextureFormat::RG8 || sourceFormat == bimg::TextureFormat::RG16;
    const bool grayAlpha = sourceFormat == bimg::TextureFormat::RG8 || sourceFormat == bimg::TextureFormat::RG16;
    if (sourceFormat != bimg::TextureFormat::RGBA8) {
        auto* converted = bimg::imageConvert(&allocator, bimg::TextureFormat::RGBA8, *image, false);
        if (!converted) throw std::runtime_error("Texture pixel conversion failed.");
        image.reset(converted);
    }
    bimg::ImageMip mip{};
    if (!bimg::imageGetRawData(*image, 0, 0, image->m_data, image->m_size, mip) || mip.m_format != bimg::TextureFormat::RGBA8)
        throw std::runtime_error("Texture decoder did not return RGBA pixels.");
    const size_t size = static_cast<size_t>(mip.m_width) * mip.m_height * 4;
    if (size > mip.m_size) throw std::runtime_error("Texture pixel buffer is incomplete.");
    DecodedImage result{mip.m_width, mip.m_height, {mip.m_data, mip.m_data + size}};
    // bimg expands scalar data into R, not RGB luminance. Restore image grayscale
    // for both the browser and albedo while retaining R for material data maps.
    if (grayscale) for (size_t pixel = 0; pixel < size; pixel += 4) {
        result.rgba[pixel + 3] = grayAlpha ? result.rgba[pixel + 1] : 255;
        result.rgba[pixel + 1] = result.rgba[pixel + 2] = result.rgba[pixel];
    }
    return result;
}


}


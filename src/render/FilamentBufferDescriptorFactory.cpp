#include "render/FilamentBufferDescriptorFactory.h"

#include <backend/DriverEnums.h>

#include <cstring>

namespace quader {
namespace {

void releaseBytes(void* buffer, std::size_t, void*) {
    delete[] static_cast<std::byte*>(buffer);
}

std::byte* copyBytes(const void* data, std::size_t byteCount) {
    if (byteCount == 0u) {
        return nullptr;
    }

    auto* bytes = new std::byte[byteCount];
    if (data) {
        std::memcpy(bytes, data, byteCount);
    }
    return bytes;
}

} // namespace

filament::backend::BufferDescriptor FilamentBufferDescriptorFactory::bytes(const void* data, std::size_t byteCount) {
    return {copyBytes(data, byteCount), byteCount, releaseBytes};
}

filament::backend::PixelBufferDescriptor FilamentBufferDescriptorFactory::rgbaPixels(
        const unsigned char* pixels,
        std::size_t byteCount) {
    return {
            copyBytes(pixels, byteCount),
            byteCount,
            filament::backend::PixelDataFormat::RGBA,
            filament::backend::PixelDataType::UBYTE,
            releaseBytes,
    };
}

} // namespace quader

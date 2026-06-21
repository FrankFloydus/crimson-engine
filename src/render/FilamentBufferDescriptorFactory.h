#pragma once

#include <backend/BufferDescriptor.h>
#include <backend/PixelBufferDescriptor.h>

#include <cstddef>
#include <vector>

namespace quader {

class FilamentBufferDescriptorFactory {
public:
    static filament::backend::BufferDescriptor bytes(const void* data, std::size_t byteCount);
    static filament::backend::PixelBufferDescriptor rgbaPixels(const unsigned char* pixels, std::size_t byteCount);

    template <typename T>
    static filament::backend::BufferDescriptor vector(const std::vector<T>& values) {
        return bytes(values.data(), sizeof(T) * values.size());
    }
};

} // namespace quader

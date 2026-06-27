////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <backend/BufferDescriptor.h>
#include <backend/PixelBufferDescriptor.h>

#include <cstddef>
#include <vector>

namespace quader {

/**
 * Creates Filament Buffer Descriptor Factory resources for the render extraction and Filament resource layer.
 */
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

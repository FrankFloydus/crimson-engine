////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/filament_buffer_descriptor_factory.hpp"

#include <backend/DriverEnums.h>

#include <cstring>

namespace quader {
namespace {

void release_bytes(void *buffer, std::size_t, void *) {
  delete[] static_cast<std::byte *>(buffer);
}

std::byte *copy_bytes(const void *data, std::size_t byte_count) {
  if (byte_count == 0u) {
    return nullptr;
  }

  auto *bytes = new std::byte[byte_count];
  if (data) {
    std::memcpy(bytes, data, byte_count);
  }
  return bytes;
}

} // namespace

filament::backend::BufferDescriptor
FilamentBufferDescriptorFactory::bytes(const void *data,
                                       std::size_t byte_count) {
  return {copy_bytes(data, byte_count), byte_count, release_bytes};
}

filament::backend::PixelBufferDescriptor
FilamentBufferDescriptorFactory::rgba_pixels(const unsigned char *pixels,
                                             std::size_t byte_count) {
  return {
      copy_bytes(pixels, byte_count),
      byte_count,
      filament::backend::PixelDataFormat::RGBA,
      filament::backend::PixelDataType::UBYTE,
      release_bytes,
  };
}

} // namespace quader

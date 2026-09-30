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
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace quader {

/**
 * Creates Filament Buffer Descriptor Factory resources for the render extraction and Filament resource layer.
 */
class FilamentBufferDescriptorFactory {
public:
  static filament::backend::BufferDescriptor bytes(const void *data,
                                                   std::size_t byte_count);
  /// Pins immutable storage shared by several asynchronous uploads without
  /// copying its bytes. The supplied owner must contain the complete span.
  static filament::backend::BufferDescriptor retained_bytes(
      const void *data, std::size_t byte_count,
      std::shared_ptr<const void> owner);
  static filament::backend::PixelBufferDescriptor
  rgba_pixels(const unsigned char *pixels, std::size_t byte_count);

  template <typename T>
  static filament::backend::BufferDescriptor
  vector(const std::vector<T> &values) {
    return bytes(values.data(), sizeof(T) * values.size());
  }

  /**
   * Transfers a vector allocation directly to Filament's asynchronous upload
   * descriptor. The release callback owns the vector until the render thread
   * reports that the upload no longer references its storage.
   */
  template <typename T>
  static filament::backend::BufferDescriptor vector(std::vector<T> &&values) {
    auto *owner = new std::vector<T>(std::move(values));
    return filament::backend::BufferDescriptor(
        owner->data(), sizeof(T) * owner->size(), &release_vector<T>, owner);
  }

  template <typename T>
  static filament::backend::BufferDescriptor
  span(std::span<const T> values) {
    return bytes(values.data(), sizeof(T) * values.size());
  }

private:
  template <typename T>
  static void release_vector(void *, std::size_t, void *user) noexcept {
    delete static_cast<std::vector<T> *>(user);
  }
};

} // namespace quader

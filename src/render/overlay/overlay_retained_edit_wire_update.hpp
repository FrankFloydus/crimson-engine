////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/overlay/overlay_proxy_factory.hpp"

#include <cstring>
#include <utility>

namespace quader {

/// Prepared bytes transfer to the backend without allocating during consumption.
class RetainedEditWireUpload final {
public:
  struct Released final {
    void *buffer = nullptr;
    std::size_t size = 0u;
    void (*callback)(void *, std::size_t, void *) = nullptr;
    void *user = nullptr;
  };

  RetainedEditWireUpload() = default;
  ~RetainedEditWireUpload() { reset(); }
  RetainedEditWireUpload(const RetainedEditWireUpload &) = delete;
  RetainedEditWireUpload &operator=(const RetainedEditWireUpload &) = delete;
  RetainedEditWireUpload(RetainedEditWireUpload &&other) noexcept
      : storage_(other.release()) {}
  RetainedEditWireUpload &operator=(RetainedEditWireUpload &&other) noexcept {
    if (this != &other) {
      reset();
      storage_ = other.release();
    }
    return *this;
  }

  [[nodiscard]] Released release() noexcept {
    return std::exchange(storage_, Released{});
  }

  template <typename T>
  static RetainedEditWireUpload span(std::span<const T> values) {
    RetainedEditWireUpload upload;
    const auto size = values.size_bytes();
    auto *data = size == 0u ? nullptr : new std::byte[size];
    if (size != 0u) std::memcpy(data, values.data(), size);
    upload.storage_ = {data, size, &release_bytes, nullptr};
    return upload;
  }

  template <typename T>
  static RetainedEditWireUpload vector(const std::vector<T> &values) {
    return span(std::span<const T>(values));
  }

  template <typename T>
  static RetainedEditWireUpload vector(std::vector<T> &&values) {
    RetainedEditWireUpload upload;
    auto *owner = new std::vector<T>(std::move(values));
    upload.storage_ = {owner->data(), sizeof(T) * owner->size(),
                       &release_vector<T>, owner};
    return upload;
  }

private:
  static void release_bytes(void *buffer, std::size_t, void *) noexcept {
    delete[] static_cast<std::byte *>(buffer);
  }
  template <typename T>
  static void release_vector(void *, std::size_t, void *owner) noexcept {
    delete static_cast<std::vector<T> *>(owner);
  }
  void reset() noexcept {
    if (storage_.callback != nullptr)
      storage_.callback(storage_.buffer, storage_.size, storage_.user);
  }
  Released storage_;
};

/// Static ribbon controls stay separate from preview-updated endpoints.
struct RetainedEditWireVertex {
  OverlayFloat4 position;
  OverlayFloat4 control;
  std::uint32_t color = 0u;
};

/// Keeps candidate host storage independent of every live overlay stream.
struct OverlayProxyFactory::RetainedEditWireUpdate final {
  /// Coalesces adjacent changed slots into one upload per attribute stream.
  struct Range final {
    std::uint32_t first_vertex = 0u;
    std::vector<OverlayFloat4> starts;
    std::vector<OverlayFloat4> ends;
    std::vector<OverlayFloat4> support0;
    std::vector<OverlayFloat4> support1;
    RetainedEditWireUpload static_upload;
    RetainedEditWireUpload start_upload;
    RetainedEditWireUpload end_upload;
    RetainedEditWireUpload support0_upload;
    RetainedEditWireUpload support1_upload;
  };
  filament::VertexBuffer *expected_vertices = nullptr;
  filament::IndexBuffer *expected_indices = nullptr;
  std::uint32_t expected_entity = 0u;
  std::uint64_t expected_revision = 0u;
  std::uint32_t slot_capacity = 0u;
  std::uint32_t index_count = 0u;
  std::vector<Range> ranges;
  std::vector<OverlayPositionStreamBinding> bindings;
  RetainedEditWireUpload index_upload;
  bool indices_changed = false;
  bool consolidated = false;
  bool consumed = false;
};

} // namespace quader

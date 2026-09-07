////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace quader_io {

/// Distinguishes a failed open, size limit, or incomplete shared source read.
enum class SharedFileReadError { None, Open, Size, Limit, Read };

/// Owns complete source bytes; its file handle is already closed on return.
struct SharedFileReadResult final {
  bool ok = false;
  std::vector<std::byte> bytes;
  SharedFileReadError error = SharedFileReadError::None;
  std::string message;
};

namespace detail {

#if defined(_WIN32)
/// Closes a shared source handle before its byte result reaches a decoder.
class SharedReadHandle final {
public:
  explicit SharedReadHandle(HANDLE handle) : handle_(handle) {}
  ~SharedReadHandle() {
    if (handle_ != INVALID_HANDLE_VALUE) {
      ::CloseHandle(handle_);
    }
  }
  SharedReadHandle(const SharedReadHandle &) = delete;
  SharedReadHandle &operator=(const SharedReadHandle &) = delete;
  [[nodiscard]] HANDLE get() const noexcept { return handle_; }

private:
  HANDLE handle_ = INVALID_HANDLE_VALUE;
};
#endif

} // namespace detail

/**
 * Reads a single opened file version without blocking atomic replacement.
 * Size and bytes come from that same handle; parsing and hashing happen after
 * it closes. Concurrent in-place truncation/growth is reported as a failed read.
 */
[[nodiscard]] inline SharedFileReadResult read_file_bytes_shared(
    const std::filesystem::path &path,
    std::uintmax_t maximum_bytes = std::numeric_limits<std::size_t>::max()) {
  std::uintmax_t byte_count = 0u;
#if defined(_WIN32)
  const detail::SharedReadHandle file(::CreateFileW(
      path.c_str(), GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
  if (file.get() == INVALID_HANDLE_VALUE) {
    return {.error = SharedFileReadError::Open,
            .message = "Could not open source file (Windows error " +
                       std::to_string(::GetLastError()) + ")."};
  }
  LARGE_INTEGER size{};
  if (!::GetFileSizeEx(file.get(), &size) || size.QuadPart < 0) {
    return {.error = SharedFileReadError::Size,
            .message = "Could not inspect opened source file size."};
  }
  byte_count = static_cast<std::uintmax_t>(size.QuadPart);
#else
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    return {.error = SharedFileReadError::Open,
            .message = "Could not open source file."};
  }
  const std::streamoff size = file.tellg();
  if (size < 0) {
    return {.error = SharedFileReadError::Size,
            .message = "Could not inspect opened source file size."};
  }
  byte_count = static_cast<std::uintmax_t>(size);
  file.seekg(0, std::ios::beg);
#endif
  if (byte_count > maximum_bytes ||
      byte_count > std::numeric_limits<std::size_t>::max()) {
    return {.error = SharedFileReadError::Limit,
            .message = "Source file exceeds the supported size limit."};
  }
  SharedFileReadResult result;
  result.bytes.resize(static_cast<std::size_t>(byte_count));
  std::size_t offset = 0u;
  while (offset < result.bytes.size()) {
    constexpr std::size_t kReadChunkBytes = 1024u * 1024u;
    const std::size_t requested =
        std::min(kReadChunkBytes, result.bytes.size() - offset);
#if defined(_WIN32)
    DWORD read_count = 0u;
    if (!::ReadFile(file.get(), result.bytes.data() + offset,
                    static_cast<DWORD>(requested), &read_count, nullptr) ||
        read_count != requested) {
#else
    file.read(reinterpret_cast<char *>(result.bytes.data() + offset),
              static_cast<std::streamsize>(requested));
    if (file.gcount() != static_cast<std::streamsize>(requested)) {
#endif
      return {.error = SharedFileReadError::Read,
              .message = "Source file changed or could not be completely read."};
    }
    offset += requested;
  }
#if defined(_WIN32)
  LARGE_INTEGER completed_size{};
  if (!::GetFileSizeEx(file.get(), &completed_size) ||
      completed_size.QuadPart != size.QuadPart) {
#else
  if (file.peek() != std::char_traits<char>::eof() || file.bad()) {
#endif
    return {.error = SharedFileReadError::Read,
            .message = "Source file changed while being read."};
  }
  result.ok = true;
  return result;
}

} // namespace quader_io

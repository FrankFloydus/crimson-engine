////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "io/qdr/qdr_diagnostic.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace quader_io {

inline constexpr std::array<std::byte, 4> kQdrBinaryMagic{
    static_cast<std::byte>('Q'), static_cast<std::byte>('D'),
    static_cast<std::byte>('R'), static_cast<std::byte>('B')};
inline constexpr std::uint16_t kQdrBinaryContainerMajor = 1;
inline constexpr std::uint16_t kQdrBinaryContainerMinor = 1;
inline constexpr std::uint32_t kQdrBinaryEndianMarker = 0x01020304u;
inline constexpr std::size_t kQdrBinaryHeaderSize = 64u;
inline constexpr std::size_t kQdrBinaryDirectoryEntrySize = 64u;

/// Identifies a typed payload stored in the binary scene container.
enum class QdrBinaryChunkType : std::uint32_t {
  SceneMetadata = 1,
  Geometry = 2,
};

/// Identifies the compression codec used by one stored chunk.
enum class QdrBinaryChunkCodec : std::uint8_t {
  None = 0,
  Zstd = 1,
};

/// Declares compatibility and cache semantics for one chunk.
enum class QdrBinaryChunkFlags : std::uint8_t {
  None = 0,
  Critical = 1u << 0u,
  DerivedCache = 1u << 1u,
};

[[nodiscard]] constexpr QdrBinaryChunkFlags
operator|(QdrBinaryChunkFlags left, QdrBinaryChunkFlags right) {
  return static_cast<QdrBinaryChunkFlags>(
      static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
}

[[nodiscard]] constexpr bool qdr_binary_has_flag(QdrBinaryChunkFlags value,
                                                 QdrBinaryChunkFlags flag) {
  return (static_cast<std::uint8_t>(value) & static_cast<std::uint8_t>(flag)) !=
         0u;
}

/// Supplies one raw chunk to the binary container builder.
struct QdrBinaryChunkInput final {
  QdrBinaryChunkType type = QdrBinaryChunkType::SceneMetadata;
  std::uint16_t schema_version = 1;
  QdrBinaryChunkFlags flags = QdrBinaryChunkFlags::Critical;
  std::uint64_t id = 0;
  std::uint64_t content_hash = 0;
  std::vector<std::byte> bytes;
  bool allow_compression = false;
};

struct QdrBinaryContainerReadResult;

/// Exposes one verified container chunk with lazy decompression.
struct QdrBinaryChunk final {
  std::uint32_t type = 0;
  std::uint16_t schema_version = 0;
  QdrBinaryChunkCodec codec = QdrBinaryChunkCodec::None;
  QdrBinaryChunkFlags flags = QdrBinaryChunkFlags::None;
  std::uint64_t id = 0;
  std::uint64_t stored_size = 0;
  std::uint64_t raw_size = 0;
  std::uint32_t checksum = 0;
  std::uint64_t content_hash = 0;

  [[nodiscard]] std::span<const std::byte> bytes() const;
  [[nodiscard]] bool was_decompressed() const;
  [[nodiscard]] double decompression_time_ms() const;

private:
  friend struct QdrBinaryContainerReadResult;
  friend QdrBinaryContainerReadResult
  read_qdr_binary_container(std::vector<std::byte> file_bytes);

  std::shared_ptr<const std::vector<std::byte>> backing_store_;
  /// Coordinates thread-safe lazy decoding for a compressed chunk.
  struct DecodeState final {
    mutable std::mutex mutex;
    std::shared_ptr<const std::vector<std::byte>> decoded_store;
    std::string error_message;
    double decompression_ms = 0.0;
  };

  std::shared_ptr<DecodeState> decode_state_;
  std::size_t stored_offset_ = 0;
};

/// Reports verified container metadata, chunks, and diagnostics.
struct QdrBinaryContainerReadResult final {
  bool ok = false;
  std::array<std::byte, 16> scene_uuid{};
  std::vector<QdrBinaryChunk> chunks;
  std::string message;
  std::vector<QdrDiagnostic> diagnostics;
  std::uint64_t stored_chunk_bytes = 0;
  std::uint64_t raw_chunk_bytes = 0;
  std::uint64_t decompressed_bytes = 0;
  std::size_t compressed_chunk_count = 0;
  double decompression_ms = 0.0;
};

/// Measures geometry compression performed while building a container.
struct QdrBinaryContainerBuildStats final {
  std::uint64_t uncompressed_geometry_bytes = 0;
  std::uint64_t stored_geometry_bytes = 0;
  std::size_t compressed_geometry_chunk_count = 0;
};

/// Returns encoded container bytes together with build measurements.
struct QdrBinaryContainerBuildResult final {
  std::vector<std::byte> bytes;
  QdrBinaryContainerBuildStats stats;
};

[[nodiscard]] std::uint32_t
qdr_binary_crc32(std::span<const std::byte> bytes);
[[nodiscard]] std::uint64_t
qdr_binary_content_hash(std::span<const std::byte> bytes);

[[nodiscard]] std::vector<std::byte> build_qdr_binary_container(
    std::array<std::byte, 16> scene_uuid,
    std::vector<QdrBinaryChunkInput> chunks);
[[nodiscard]] QdrBinaryContainerBuildResult
build_qdr_binary_container_with_stats(
    std::array<std::byte, 16> scene_uuid,
    std::vector<QdrBinaryChunkInput> chunks);

[[nodiscard]] QdrBinaryContainerReadResult
read_qdr_binary_container(std::vector<std::byte> file_bytes);

} // namespace quader_io

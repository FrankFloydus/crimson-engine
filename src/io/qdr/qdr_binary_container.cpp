////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/qdr/qdr_binary_container.hpp"

#include "diagnostics/profile.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstring>
#include <limits>
#include <map>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

#if defined(QUADER_QDR_ENABLE_ZSTD)
#include <zstd.h>
#endif

namespace quader_io {
namespace {

constexpr std::size_t kChunkAlignment = 8u;
constexpr std::size_t kCompressionThreshold = 64u * 1024u;
constexpr std::size_t kMinimumCompressionSavingDivisor = 10u;
constexpr std::uint64_t kMaxChunkRawSize =
    static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max());
constexpr std::uint64_t kMaxDirectoryEntries = 1'000'000u;

/// Holds one chunk after optional compression and before directory emission.
struct EncodedChunk final {
  QdrBinaryChunkInput input;
  QdrBinaryChunkCodec codec = QdrBinaryChunkCodec::None;
  std::vector<std::byte> stored_bytes;
  std::uint32_t checksum = 0;
  std::uint64_t offset = 0;
};

[[nodiscard]] bool checked_add(std::uint64_t left, std::uint64_t right,
                               std::uint64_t &result) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    return false;
  }
  result = left + right;
  return true;
}

[[nodiscard]] bool checked_multiply(std::uint64_t left, std::uint64_t right,
                                    std::uint64_t &result) {
  if (left != 0u && right > std::numeric_limits<std::uint64_t>::max() / left) {
    return false;
  }
  result = left * right;
  return true;
}

void append_zeros(std::vector<std::byte> &bytes, std::size_t count) {
  bytes.insert(bytes.end(), count, std::byte{0});
}

void align_bytes(std::vector<std::byte> &bytes) {
  const std::size_t remainder = bytes.size() % kChunkAlignment;
  if (remainder != 0u) {
    append_zeros(bytes, kChunkAlignment - remainder);
  }
}

template <typename Integer>
void write_little_endian_at(std::vector<std::byte> &bytes, std::size_t offset,
                            Integer value) {
  static_assert(std::is_unsigned_v<Integer>);
  if (offset > bytes.size() || sizeof(Integer) > bytes.size() - offset) {
    throw std::runtime_error("QDR binary writer exceeded its output buffer.");
  }
  for (std::size_t index = 0u; index < sizeof(Integer); ++index) {
    bytes[offset + index] =
        static_cast<std::byte>((value >> (index * 8u)) & Integer{0xffu});
  }
}

template <typename Integer>
[[nodiscard]] bool read_little_endian(std::span<const std::byte> bytes,
                                      std::size_t offset, Integer &value) {
  static_assert(std::is_unsigned_v<Integer>);
  if (offset > bytes.size() || sizeof(Integer) > bytes.size() - offset) {
    return false;
  }
  value = 0;
  for (std::size_t index = 0u; index < sizeof(Integer); ++index) {
    value |= static_cast<Integer>(std::to_integer<unsigned int>(
                 bytes[offset + index]))
             << (index * 8u);
  }
  return true;
}

template <typename Integer>
void append_little_endian(std::vector<std::byte> &bytes, Integer value) {
  const std::size_t offset = bytes.size();
  append_zeros(bytes, sizeof(Integer));
  write_little_endian_at(bytes, offset, value);
}

[[nodiscard]] QdrBinaryContainerReadResult
container_error(std::string_view code, std::string message) {
  QdrDiagnostic diagnostic =
      qdr_make_error(code, "", std::move(message));
  const std::string result_message = diagnostic.message;
  return {
      .ok = false,
      .message = result_message,
      .diagnostics = {std::move(diagnostic)},
  };
}

[[nodiscard]] EncodedChunk encode_chunk(QdrBinaryChunkInput input) {
  if (input.bytes.size() > kMaxChunkRawSize) {
    throw std::runtime_error(
        "QDR binary chunk exceeds the supported 4 GiB raw-size limit.");
  }

  EncodedChunk encoded{
      .input = std::move(input),
  };
  encoded.checksum = qdr_binary_crc32(encoded.input.bytes);
  encoded.stored_bytes = encoded.input.bytes;

#if defined(QUADER_QDR_ENABLE_ZSTD)
  if (encoded.input.allow_compression &&
      encoded.input.bytes.size() >= kCompressionThreshold) {
    const std::size_t bound = ZSTD_compressBound(encoded.input.bytes.size());
    std::vector<std::byte> compressed(bound);
    const std::size_t compressed_size = ZSTD_compress(
        compressed.data(), compressed.size(), encoded.input.bytes.data(),
        encoded.input.bytes.size(), 1);
    if (ZSTD_isError(compressed_size) != 0u) {
      throw std::runtime_error(std::string("Could not compress QDR geometry: ") +
                               ZSTD_getErrorName(compressed_size));
    }
    const std::size_t minimum_saving =
        encoded.input.bytes.size() / kMinimumCompressionSavingDivisor;
    if (compressed_size + minimum_saving <= encoded.input.bytes.size()) {
      compressed.resize(compressed_size);
      encoded.stored_bytes = std::move(compressed);
      encoded.codec = QdrBinaryChunkCodec::Zstd;
    }
  }
#endif
  return encoded;
}

[[nodiscard]] std::shared_ptr<const std::vector<std::byte>>
decode_zstd_chunk(std::span<const std::byte> stored, std::uint64_t raw_size,
                  std::string &error_message) {
#if defined(QUADER_QDR_ENABLE_ZSTD)
  if (raw_size > kMaxChunkRawSize ||
      raw_size > static_cast<std::uint64_t>(
                     std::numeric_limits<std::size_t>::max())) {
    error_message = "QDR compressed chunk raw size exceeds the decode limit.";
    return {};
  }
  auto decoded = std::make_shared<std::vector<std::byte>>(
      static_cast<std::size_t>(raw_size));
  const std::size_t decoded_size =
      ZSTD_decompress(decoded->data(), decoded->size(), stored.data(),
                      stored.size());
  if (ZSTD_isError(decoded_size) != 0u) {
    error_message = std::string("Could not decompress QDR chunk: ") +
                    ZSTD_getErrorName(decoded_size);
    return {};
  }
  if (decoded_size != decoded->size()) {
    error_message = "QDR compressed chunk decoded to an unexpected size.";
    return {};
  }
  return decoded;
#else
  (void)stored;
  (void)raw_size;
  error_message =
      "This Quader build cannot read Zstandard-compressed QDR chunks.";
  return {};
#endif
}

} // namespace

std::span<const std::byte> QdrBinaryChunk::bytes() const {
  if (!backing_store_ || stored_offset_ > backing_store_->size() ||
      stored_size > backing_store_->size() - stored_offset_) {
    return {};
  }
  const std::span<const std::byte> stored =
      std::span<const std::byte>(*backing_store_)
      .subspan(stored_offset_, static_cast<std::size_t>(stored_size));
  if (codec == QdrBinaryChunkCodec::None) {
    return stored;
  }
  if (!decode_state_) {
    throw QdrDiagnosticException(qdr_make_error(
        "qdr.binary.decompression_failed", "",
        "QDR compressed chunk has no decode state."));
  }

  std::scoped_lock lock(decode_state_->mutex);
  if (decode_state_->decoded_store) {
    return *decode_state_->decoded_store;
  }
  if (!decode_state_->error_message.empty()) {
    throw QdrDiagnosticException(qdr_make_error(
        "qdr.binary.decompression_failed", "",
        decode_state_->error_message));
  }
  const auto started = std::chrono::steady_clock::now();
  decode_state_->decoded_store =
      decode_zstd_chunk(stored, raw_size, decode_state_->error_message);
  decode_state_->decompression_ms =
      std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - started)
          .count();
  if (!decode_state_->decoded_store) {
    throw QdrDiagnosticException(qdr_make_error(
        "qdr.binary.decompression_failed", "",
        decode_state_->error_message));
  }
  if (qdr_binary_crc32(*decode_state_->decoded_store) != checksum) {
    decode_state_->decoded_store.reset();
    decode_state_->error_message =
        "QDR binary chunk checksum does not match.";
    throw QdrDiagnosticException(qdr_make_error(
        "qdr.binary.checksum_mismatch", "",
        decode_state_->error_message));
  }
  if (content_hash != 0u &&
      qdr_binary_content_hash(*decode_state_->decoded_store) != content_hash) {
    decode_state_->decoded_store.reset();
    decode_state_->error_message =
        "QDR binary chunk content hash does not match.";
    throw QdrDiagnosticException(qdr_make_error(
        "qdr.binary.content_hash_mismatch", "",
        decode_state_->error_message));
  }
  return *decode_state_->decoded_store;
}

bool QdrBinaryChunk::was_decompressed() const {
  if (!decode_state_) {
    return false;
  }
  std::scoped_lock lock(decode_state_->mutex);
  return decode_state_->decoded_store != nullptr;
}

double QdrBinaryChunk::decompression_time_ms() const {
  if (!decode_state_) {
    return 0.0;
  }
  std::scoped_lock lock(decode_state_->mutex);
  return decode_state_->decompression_ms;
}

std::uint32_t qdr_binary_crc32(std::span<const std::byte> bytes) {
  std::uint32_t crc = 0xffffffffu;
  for (const std::byte value : bytes) {
    crc ^= std::to_integer<std::uint8_t>(value);
    for (unsigned int bit = 0u; bit < 8u; ++bit) {
      const std::uint32_t mask = 0u - (crc & 1u);
      crc = (crc >> 1u) ^ (0xedb88320u & mask);
    }
  }
  return ~crc;
}

std::uint64_t qdr_binary_content_hash(std::span<const std::byte> bytes) {
  std::uint64_t hash = 14695981039346656037ull;
  for (const std::byte value : bytes) {
    hash ^= std::to_integer<std::uint8_t>(value);
    hash *= 1099511628211ull;
  }
  return hash;
}

QdrBinaryContainerBuildResult build_qdr_binary_container_with_stats(
    std::array<std::byte, 16> scene_uuid,
    std::vector<QdrBinaryChunkInput> chunks,
    quader::ProductRelease minimum_product_release) {
  QDR_PROFILE_FUNCTION();
  if (minimum_product_release == 0u) {
    throw std::runtime_error("QDR binary minimum product release is invalid.");
  }
  if (chunks.empty()) {
    throw std::runtime_error("QDR binary container requires at least one chunk.");
  }
  if (chunks.size() > kMaxDirectoryEntries) {
    throw std::runtime_error("QDR binary container has too many chunks.");
  }

  std::map<std::pair<std::uint32_t, std::uint64_t>, bool> unique_chunks;
  std::vector<EncodedChunk> encoded_chunks;
  encoded_chunks.reserve(chunks.size());
  for (QdrBinaryChunkInput &chunk : chunks) {
    const auto key =
        std::pair{static_cast<std::uint32_t>(chunk.type), chunk.id};
    if (!unique_chunks.emplace(key, true).second) {
      throw std::runtime_error("QDR binary container has duplicate chunk ids.");
    }
    encoded_chunks.push_back(encode_chunk(std::move(chunk)));
  }

  QdrBinaryContainerBuildStats stats;
  for (const EncodedChunk &chunk : encoded_chunks) {
    if (chunk.input.type != QdrBinaryChunkType::Geometry) {
      continue;
    }
    stats.uncompressed_geometry_bytes += chunk.input.bytes.size();
    stats.stored_geometry_bytes += chunk.stored_bytes.size();
    if (chunk.codec != QdrBinaryChunkCodec::None) {
      ++stats.compressed_geometry_chunk_count;
    }
  }

  std::vector<std::byte> output(kQdrBinaryHeaderSize, std::byte{0});
  for (EncodedChunk &chunk : encoded_chunks) {
    align_bytes(output);
    chunk.offset = output.size();
    output.insert(output.end(), chunk.stored_bytes.begin(),
                  chunk.stored_bytes.end());
  }
  align_bytes(output);
  const std::uint64_t directory_offset = output.size();

  for (const EncodedChunk &chunk : encoded_chunks) {
    append_little_endian(output,
                         static_cast<std::uint32_t>(chunk.input.type));
    append_little_endian(output, chunk.input.schema_version);
    output.push_back(static_cast<std::byte>(
        static_cast<std::uint8_t>(chunk.codec)));
    output.push_back(static_cast<std::byte>(
        static_cast<std::uint8_t>(chunk.input.flags)));
    append_little_endian(output, chunk.input.id);
    append_little_endian(output, chunk.offset);
    append_little_endian(output,
                         static_cast<std::uint64_t>(chunk.stored_bytes.size()));
    append_little_endian(output,
                         static_cast<std::uint64_t>(chunk.input.bytes.size()));
    append_little_endian(output, chunk.checksum);
    append_little_endian(output, std::uint32_t{0});
    append_little_endian(output, chunk.input.content_hash);
    append_little_endian(output, std::uint64_t{0});
  }

  std::copy(kQdrBinaryMagic.begin(), kQdrBinaryMagic.end(), output.begin());
  write_little_endian_at(output, 4u, kQdrBinaryContainerMajor);
  write_little_endian_at(output, 6u, kQdrBinaryContainerMinor);
  write_little_endian_at(output, 8u, kQdrBinaryEndianMarker);
  write_little_endian_at(output, 12u, std::uint32_t{0});
  std::copy(scene_uuid.begin(), scene_uuid.end(), output.begin() + 16u);
  write_little_endian_at(output, 32u, directory_offset);
  write_little_endian_at(output, 40u,
                         static_cast<std::uint64_t>(encoded_chunks.size()));
  write_little_endian_at(output, 48u,
                         static_cast<std::uint64_t>(output.size()));
  write_little_endian_at(output, 56u, minimum_product_release);
  return {.bytes = std::move(output), .stats = stats};
}

std::vector<std::byte> build_qdr_binary_container(
    std::array<std::byte, 16> scene_uuid,
    std::vector<QdrBinaryChunkInput> chunks,
    quader::ProductRelease minimum_product_release) {
  return build_qdr_binary_container_with_stats(
             scene_uuid, std::move(chunks), minimum_product_release)
      .bytes;
}

QdrBinaryContainerReadResult
read_qdr_binary_container(std::vector<std::byte> file_bytes) {
  QDR_PROFILE_FUNCTION();
  const auto backing =
      std::make_shared<const std::vector<std::byte>>(std::move(file_bytes));
  const std::span<const std::byte> bytes(*backing);
  const auto first_non_whitespace = std::find_if(
      bytes.begin(), bytes.end(), [](std::byte value) {
        const char character =
            static_cast<char>(std::to_integer<unsigned char>(value));
        return character != ' ' && character != '\t' && character != '\r' &&
               character != '\n';
      });
  if (first_non_whitespace != bytes.end() &&
      (*first_non_whitespace == static_cast<std::byte>('{') ||
       *first_non_whitespace == static_cast<std::byte>('['))) {
    return container_error(
        "qdr.binary.unsupported_format",
        "Unsupported QDR format. Legacy JSON QDR files are not supported.");
  }
  if (bytes.size() < kQdrBinaryHeaderSize) {
    return container_error("qdr.binary.truncated_header",
                           "QDR binary file is smaller than its header.");
  }
  if (!std::equal(kQdrBinaryMagic.begin(), kQdrBinaryMagic.end(),
                  bytes.begin())) {
    return container_error(
        "qdr.binary.unsupported_format",
        "Unsupported QDR format. Legacy JSON QDR files are not supported.");
  }

  std::uint16_t major = 0;
  std::uint16_t minor = 0;
  std::uint32_t endian_marker = 0;
  std::uint32_t header_flags = 0;
  std::uint64_t directory_offset = 0;
  std::uint64_t directory_count = 0;
  std::uint64_t declared_file_size = 0;
  std::uint64_t header_reserved = 0;
  (void)read_little_endian(bytes, 4u, major);
  (void)read_little_endian(bytes, 6u, minor);
  (void)read_little_endian(bytes, 8u, endian_marker);
  (void)read_little_endian(bytes, 12u, header_flags);
  (void)read_little_endian(bytes, 32u, directory_offset);
  (void)read_little_endian(bytes, 40u, directory_count);
  (void)read_little_endian(bytes, 48u, declared_file_size);
  (void)read_little_endian(bytes, 56u, header_reserved);
  if (major != kQdrBinaryContainerMajor) {
    return container_error(
        "qdr.binary.unsupported_version",
        "Unsupported QDR binary container major version: " +
            std::to_string(major));
  }
  if (minor > kQdrBinaryContainerMinor) {
    return container_error(
        "qdr.binary.unsupported_minor_version",
        "QDR binary container requires a newer Quader version.");
  }
  if (endian_marker != kQdrBinaryEndianMarker) {
    return container_error("qdr.binary.invalid_endian_marker",
                           "QDR binary byte-order marker is invalid.");
  }
  if (header_flags != 0u || (minor < 2u && header_reserved != 0u)) {
    return container_error("qdr.binary.unsupported_header_flags",
                           "QDR binary header contains unsupported flags.");
  }
  if (minor >= 2u && header_reserved == 0u) {
    return container_error("qdr.binary.invalid_product_release",
                           "QDR binary minimum product release is invalid.");
  }
  if (declared_file_size != bytes.size()) {
    return container_error("qdr.binary.file_size_mismatch",
                           "QDR binary declared file size does not match the "
                           "available bytes.");
  }
  if (directory_count == 0u || directory_count > kMaxDirectoryEntries) {
    return container_error("qdr.binary.invalid_directory_count",
                           "QDR binary chunk directory count is invalid.");
  }
  if (directory_offset < kQdrBinaryHeaderSize ||
      directory_offset % kChunkAlignment != 0u) {
    return container_error("qdr.binary.invalid_directory_offset",
                           "QDR binary chunk directory offset is invalid.");
  }
  std::uint64_t directory_size = 0;
  std::uint64_t directory_end = 0;
  if (!checked_multiply(directory_count, kQdrBinaryDirectoryEntrySize,
                        directory_size) ||
      !checked_add(directory_offset, directory_size, directory_end) ||
      directory_end != bytes.size()) {
    return container_error("qdr.binary.invalid_directory_bounds",
                           "QDR binary chunk directory is outside the file.");
  }

  /// Describes one verified non-overlapping byte range in the container.
  struct Range final {
    std::uint64_t begin = 0;
    std::uint64_t end = 0;
  };
  std::vector<Range> ranges;
  ranges.reserve(static_cast<std::size_t>(directory_count));
  std::map<std::pair<std::uint32_t, std::uint64_t>, bool> unique_chunks;
  std::vector<QdrBinaryChunk> chunks;
  chunks.reserve(static_cast<std::size_t>(directory_count));
  std::uint64_t total_stored_bytes = 0u;
  std::uint64_t total_raw_bytes = 0u;
  std::uint64_t total_decompressed_bytes = 0u;
  std::size_t compressed_chunk_count = 0u;
  double decompression_ms = 0.0;

  for (std::uint64_t index = 0u; index < directory_count; ++index) {
    const std::size_t entry_offset = static_cast<std::size_t>(
        directory_offset + index * kQdrBinaryDirectoryEntrySize);
    QdrBinaryChunk chunk;
    std::uint8_t codec = 0;
    std::uint8_t flags = 0;
    std::uint64_t stored_offset = 0;
    std::uint32_t entry_reserved = 0;
    std::uint64_t entry_reserved64 = 0;
    (void)read_little_endian(bytes, entry_offset, chunk.type);
    (void)read_little_endian(bytes, entry_offset + 4u,
                             chunk.schema_version);
    codec = std::to_integer<std::uint8_t>(bytes[entry_offset + 6u]);
    flags = std::to_integer<std::uint8_t>(bytes[entry_offset + 7u]);
    chunk.codec = static_cast<QdrBinaryChunkCodec>(codec);
    chunk.flags = static_cast<QdrBinaryChunkFlags>(flags);
    (void)read_little_endian(bytes, entry_offset + 8u, chunk.id);
    (void)read_little_endian(bytes, entry_offset + 16u, stored_offset);
    (void)read_little_endian(bytes, entry_offset + 24u, chunk.stored_size);
    (void)read_little_endian(bytes, entry_offset + 32u, chunk.raw_size);
    (void)read_little_endian(bytes, entry_offset + 40u, chunk.checksum);
    (void)read_little_endian(bytes, entry_offset + 44u, entry_reserved);
    (void)read_little_endian(bytes, entry_offset + 48u,
                             chunk.content_hash);
    (void)read_little_endian(bytes, entry_offset + 56u, entry_reserved64);

    if (chunk.schema_version == 0u) {
      return container_error("qdr.binary.invalid_schema_version",
                             "QDR binary chunk schema version is invalid.");
    }
    if (entry_reserved != 0u || entry_reserved64 != 0u) {
      return container_error("qdr.binary.unsupported_directory_fields",
                             "QDR binary directory contains unsupported data.");
    }
    if ((flags & ~static_cast<std::uint8_t>(
                     QdrBinaryChunkFlags::Critical |
                     QdrBinaryChunkFlags::DerivedCache)) != 0u) {
      return container_error("qdr.binary.invalid_chunk_flags",
                             "QDR binary chunk contains unsupported flags.");
    }
    if (!unique_chunks.emplace(std::pair{chunk.type, chunk.id}, true).second) {
      return container_error("qdr.binary.duplicate_chunk",
                             "QDR binary contains duplicate chunk ids.");
    }
    if (chunk.raw_size > kMaxChunkRawSize) {
      return container_error("qdr.binary.chunk_too_large",
                             "QDR binary chunk exceeds the decode limit.");
    }
    std::uint64_t stored_end = 0;
    if (stored_offset < kQdrBinaryHeaderSize ||
        stored_offset % kChunkAlignment != 0u ||
        !checked_add(stored_offset, chunk.stored_size, stored_end) ||
        stored_end > directory_offset) {
      return container_error("qdr.binary.invalid_chunk_bounds",
                             "QDR binary chunk is outside the payload area.");
    }
    if (chunk.codec == QdrBinaryChunkCodec::None &&
        chunk.stored_size != chunk.raw_size) {
      return container_error("qdr.binary.invalid_raw_chunk_size",
                             "Uncompressed QDR chunk sizes do not match.");
    }
    if (chunk.codec != QdrBinaryChunkCodec::None &&
        chunk.codec != QdrBinaryChunkCodec::Zstd) {
      return container_error("qdr.binary.unsupported_codec",
                             "QDR binary chunk uses an unsupported codec.");
    }

    chunk.backing_store_ = backing;
    chunk.stored_offset_ = static_cast<std::size_t>(stored_offset);
    if (chunk.codec == QdrBinaryChunkCodec::Zstd) {
      chunk.decode_state_ = std::make_shared<QdrBinaryChunk::DecodeState>();
      ++compressed_chunk_count;
    } else {
      if (qdr_binary_crc32(chunk.bytes()) != chunk.checksum) {
        return container_error("qdr.binary.checksum_mismatch",
                               "QDR binary chunk checksum does not match.");
      }
      if (chunk.content_hash != 0u &&
          qdr_binary_content_hash(chunk.bytes()) != chunk.content_hash) {
        return container_error("qdr.binary.content_hash_mismatch",
                               "QDR binary chunk content hash does not match.");
      }
    }
    ranges.push_back({stored_offset, stored_end});
    total_stored_bytes += chunk.stored_size;
    total_raw_bytes += chunk.raw_size;
    chunks.push_back(std::move(chunk));
  }

  std::sort(ranges.begin(), ranges.end(),
            [](const Range &left, const Range &right) {
              return std::tie(left.begin, left.end) <
                     std::tie(right.begin, right.end);
            });
  for (std::size_t index = 1u; index < ranges.size(); ++index) {
    if (ranges[index].begin < ranges[index - 1u].end) {
      return container_error("qdr.binary.overlapping_chunks",
                             "QDR binary chunks overlap.");
    }
  }

  QdrBinaryContainerReadResult result{
      .ok = true,
      .minimum_product_release =
          minor >= 2u ? header_reserved : quader::kInternalFullRelease,
      .chunks = std::move(chunks),
      .stored_chunk_bytes = total_stored_bytes,
      .raw_chunk_bytes = total_raw_bytes,
      .decompressed_bytes = total_decompressed_bytes,
      .compressed_chunk_count = compressed_chunk_count,
      .decompression_ms = decompression_ms,
  };
  std::copy_n(bytes.begin() + 16u, result.scene_uuid.size(),
              result.scene_uuid.begin());
  return result;
}

} // namespace quader_io

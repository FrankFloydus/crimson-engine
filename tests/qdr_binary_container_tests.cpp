////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/qdr/qdr_binary_container.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace quader_io {
namespace {

[[nodiscard]] std::vector<std::byte> bytes(std::string_view text) {
  const auto *begin = reinterpret_cast<const std::byte *>(text.data());
  return {begin, begin + text.size()};
}

template <typename Integer>
void write_little_endian(std::vector<std::byte> &output, std::size_t offset,
                         Integer value) {
  for (std::size_t index = 0u; index < sizeof(Integer); ++index) {
    output.at(offset + index) =
        static_cast<std::byte>((value >> (index * 8u)) & Integer{0xffu});
  }
}

template <typename Integer>
[[nodiscard]] Integer read_little_endian(const std::vector<std::byte> &input,
                                         std::size_t offset) {
  Integer value = 0;
  for (std::size_t index = 0u; index < sizeof(Integer); ++index) {
    value |= static_cast<Integer>(
                 std::to_integer<unsigned int>(input.at(offset + index)))
             << (index * 8u);
  }
  return value;
}

[[nodiscard]] std::vector<std::byte> sample_container(
    std::array<std::byte, 16> uuid = {},
    quader::ProductRelease minimum_product_release = quader::kInternalFullRelease) {
  std::vector<QdrBinaryChunkInput> chunks;
  chunks.push_back({
      .type = QdrBinaryChunkType::SceneMetadata,
      .schema_version = 1u,
      .flags = QdrBinaryChunkFlags::Critical,
      .id = 0u,
      .content_hash = qdr_binary_content_hash(bytes("metadata")),
      .bytes = bytes("metadata"),
  });
  chunks.push_back({
      .type = QdrBinaryChunkType::Geometry,
      .schema_version = 1u,
      .flags = QdrBinaryChunkFlags::Critical,
      .id = 7u,
      .content_hash = qdr_binary_content_hash(bytes("geometry")),
      .bytes = bytes("geometry"),
      .allow_compression = true,
  });
  return build_qdr_binary_container(uuid, std::move(chunks),
                                     minimum_product_release);
}

TEST(QdrBinaryContainerTests, PreservesExplicitMinimumProductRelease) {
  for (const quader::ProductRelease release :
       {quader::ProductRelease{1u}, quader::ProductRelease{2u},
        quader::kInternalFullRelease}) {
    const auto encoded = sample_container({}, release);
    EXPECT_EQ(read_little_endian<std::uint16_t>(encoded, 6u), 2u);
    EXPECT_EQ(read_little_endian<std::uint64_t>(encoded, 56u), release);
    const auto read = read_qdr_binary_container(encoded);
    ASSERT_TRUE(read.ok) << read.message;
    EXPECT_EQ(read.minimum_product_release, release);
  }
}

TEST(QdrBinaryContainerTests, LegacyContainersRequireFullAndRetainReservedValidation) {
  for (const std::uint16_t minor : {std::uint16_t{0u}, std::uint16_t{1u}}) {
    auto encoded = sample_container();
    write_little_endian(encoded, 6u, minor);
    write_little_endian(encoded, 56u, std::uint64_t{0u});
    const auto read = read_qdr_binary_container(encoded);
    ASSERT_TRUE(read.ok) << read.message;
    EXPECT_EQ(read.minimum_product_release, quader::kInternalFullRelease);

    write_little_endian(encoded, 56u, std::uint64_t{1u});
    const auto invalid = read_qdr_binary_container(std::move(encoded));
    ASSERT_FALSE(invalid.ok);
    ASSERT_FALSE(invalid.diagnostics.empty());
    EXPECT_EQ(invalid.diagnostics.front().code,
              "qdr.binary.unsupported_header_flags");
  }
}

TEST(QdrBinaryContainerTests, RejectsMissingMinimumProductRelease) {
  auto encoded = sample_container();
  write_little_endian(encoded, 56u, std::uint64_t{0u});
  const auto read = read_qdr_binary_container(std::move(encoded));
  ASSERT_FALSE(read.ok);
  ASSERT_FALSE(read.diagnostics.empty());
  EXPECT_EQ(read.diagnostics.front().code, "qdr.binary.invalid_product_release");
  EXPECT_THROW((void)sample_container({}, 0u), std::runtime_error);
}

TEST(QdrBinaryContainerTests, RoundTripPreservesEnvelopeAndChunks) {
  std::array<std::byte, 16> uuid{};
  uuid[0] = std::byte{0x41};
  uuid[15] = std::byte{0x9f};
  const QdrBinaryContainerReadResult result =
      read_qdr_binary_container(sample_container(uuid));

  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.scene_uuid, uuid);
  ASSERT_EQ(result.chunks.size(), 2u);
  EXPECT_EQ(result.chunks[0].type,
            static_cast<std::uint32_t>(QdrBinaryChunkType::SceneMetadata));
  EXPECT_EQ(result.chunks[0].id, 0u);
  EXPECT_TRUE(std::ranges::equal(result.chunks[0].bytes(), bytes("metadata")));
  EXPECT_EQ(result.chunks[1].type,
            static_cast<std::uint32_t>(QdrBinaryChunkType::Geometry));
  EXPECT_EQ(result.chunks[1].id, 7u);
  EXPECT_TRUE(std::ranges::equal(result.chunks[1].bytes(), bytes("geometry")));
}

TEST(QdrBinaryContainerTests, RejectsLegacyJsonWithoutParsingIt) {
  const QdrBinaryContainerReadResult result = read_qdr_binary_container(
      bytes(R"({"format":"quader.qdr","version":4})"));

  ASSERT_FALSE(result.ok);
  ASSERT_FALSE(result.diagnostics.empty());
  EXPECT_EQ(result.diagnostics.front().code, "qdr.binary.unsupported_format");
  EXPECT_NE(result.message.find("Legacy JSON"), std::string::npos);
}

TEST(QdrBinaryContainerTests, RejectsTruncatedHeaders) {
  const QdrBinaryContainerReadResult result =
      read_qdr_binary_container({static_cast<std::byte>('Q'),
                                 static_cast<std::byte>('D'),
                                 static_cast<std::byte>('R'),
                                 static_cast<std::byte>('B')});
  ASSERT_FALSE(result.ok);
  ASSERT_FALSE(result.diagnostics.empty());
  EXPECT_EQ(result.diagnostics.front().code,
            "qdr.binary.truncated_header");
}

TEST(QdrBinaryContainerTests, RejectsUnsupportedContainerVersions) {
  std::vector<std::byte> major = sample_container();
  write_little_endian(major, 4u,
                      static_cast<std::uint16_t>(
                          kQdrBinaryContainerMajor + 1u));
  QdrBinaryContainerReadResult result =
      read_qdr_binary_container(std::move(major));
  ASSERT_FALSE(result.ok);
  ASSERT_FALSE(result.diagnostics.empty());
  EXPECT_EQ(result.diagnostics.front().code,
            "qdr.binary.unsupported_version");

  std::vector<std::byte> minor = sample_container();
  write_little_endian(minor, 6u,
                      static_cast<std::uint16_t>(
                          kQdrBinaryContainerMinor + 1u));
  result = read_qdr_binary_container(std::move(minor));
  ASSERT_FALSE(result.ok);
  ASSERT_FALSE(result.diagnostics.empty());
  EXPECT_EQ(result.diagnostics.front().code,
            "qdr.binary.unsupported_minor_version");
}

TEST(QdrBinaryContainerTests, RejectsInvalidHeaderMetadata) {
  std::vector<std::byte> endian = sample_container();
  write_little_endian(endian, 8u, std::uint32_t{0x04030201u});
  QdrBinaryContainerReadResult result =
      read_qdr_binary_container(std::move(endian));
  ASSERT_FALSE(result.ok);
  ASSERT_FALSE(result.diagnostics.empty());
  EXPECT_EQ(result.diagnostics.front().code,
            "qdr.binary.invalid_endian_marker");

  std::vector<std::byte> size = sample_container();
  write_little_endian(
      size, 48u, static_cast<std::uint64_t>(size.size() + 1u));
  result = read_qdr_binary_container(std::move(size));
  ASSERT_FALSE(result.ok);
  ASSERT_FALSE(result.diagnostics.empty());
  EXPECT_EQ(result.diagnostics.front().code,
            "qdr.binary.file_size_mismatch");

  std::vector<std::byte> flags = sample_container();
  write_little_endian(flags, 12u, std::uint32_t{1u});
  result = read_qdr_binary_container(std::move(flags));
  ASSERT_FALSE(result.ok);
  ASSERT_FALSE(result.diagnostics.empty());
  EXPECT_EQ(result.diagnostics.front().code,
            "qdr.binary.unsupported_header_flags");
}

TEST(QdrBinaryContainerTests, RejectsPayloadCorruptionByChecksum) {
  std::vector<std::byte> encoded = sample_container();
  ASSERT_GT(encoded.size(), kQdrBinaryHeaderSize);
  encoded[kQdrBinaryHeaderSize] ^= std::byte{0x80};

  const QdrBinaryContainerReadResult result =
      read_qdr_binary_container(std::move(encoded));
  ASSERT_FALSE(result.ok);
  ASSERT_FALSE(result.diagnostics.empty());
  EXPECT_EQ(result.diagnostics.front().code, "qdr.binary.checksum_mismatch");
}

TEST(QdrBinaryContainerTests, RejectsContentHashMismatch) {
  std::vector<std::byte> encoded = sample_container();
  const std::uint64_t directory_offset =
      read_little_endian<std::uint64_t>(encoded, 32u);
  const std::uint64_t content_hash = read_little_endian<std::uint64_t>(
      encoded, static_cast<std::size_t>(directory_offset) + 48u);
  write_little_endian(encoded,
                      static_cast<std::size_t>(directory_offset) + 48u,
                      content_hash + 1u);

  const QdrBinaryContainerReadResult result =
      read_qdr_binary_container(std::move(encoded));
  ASSERT_FALSE(result.ok);
  ASSERT_FALSE(result.diagnostics.empty());
  EXPECT_EQ(result.diagnostics.front().code,
            "qdr.binary.content_hash_mismatch");
}

TEST(QdrBinaryContainerTests, RejectsInvalidChunkSchemaCodecAndFlags) {
  const auto require_code = [](std::vector<std::byte> encoded,
                               std::size_t entry_field_offset,
                               std::byte value, std::string_view code) {
    const std::uint64_t directory_offset =
        read_little_endian<std::uint64_t>(encoded, 32u);
    encoded.at(static_cast<std::size_t>(directory_offset) +
               entry_field_offset) = value;
    const QdrBinaryContainerReadResult result =
        read_qdr_binary_container(std::move(encoded));
    ASSERT_FALSE(result.ok);
    ASSERT_FALSE(result.diagnostics.empty());
    EXPECT_EQ(result.diagnostics.front().code, code);
  };

  require_code(sample_container(), 4u, std::byte{0},
               "qdr.binary.invalid_schema_version");
  require_code(sample_container(), 6u, std::byte{0xff},
               "qdr.binary.unsupported_codec");
  require_code(sample_container(), 7u, std::byte{0x80},
               "qdr.binary.invalid_chunk_flags");
}

TEST(QdrBinaryContainerTests, WriterRejectsDuplicateChunkIds) {
  std::vector<QdrBinaryChunkInput> chunks;
  chunks.push_back({
      .type = QdrBinaryChunkType::Geometry,
      .schema_version = 1u,
      .id = 4u,
      .bytes = bytes("first"),
  });
  chunks.push_back({
      .type = QdrBinaryChunkType::Geometry,
      .schema_version = 1u,
      .id = 4u,
      .bytes = bytes("second"),
  });
  EXPECT_THROW((void)build_qdr_binary_container({}, std::move(chunks)),
               std::runtime_error);
}

TEST(QdrBinaryContainerTests, RejectsOverflowingDirectoryBounds) {
  std::vector<std::byte> encoded = sample_container();
  write_little_endian(encoded, 40u,
                      std::numeric_limits<std::uint64_t>::max());

  const QdrBinaryContainerReadResult result =
      read_qdr_binary_container(std::move(encoded));
  ASSERT_FALSE(result.ok);
  ASSERT_FALSE(result.diagnostics.empty());
  EXPECT_EQ(result.diagnostics.front().code,
            "qdr.binary.invalid_directory_count");
}

TEST(QdrBinaryContainerTests, RejectsCompressedChunksBeyondDecodeLimit) {
  std::vector<std::byte> repetitive(70u * 1024u, std::byte{0});
  std::vector<QdrBinaryChunkInput> chunks;
  chunks.push_back({
      .type = QdrBinaryChunkType::Geometry,
      .schema_version = 1u,
      .id = 9u,
      .content_hash = qdr_binary_content_hash(repetitive),
      .bytes = std::move(repetitive),
      .allow_compression = true,
  });
  std::vector<std::byte> encoded =
      build_qdr_binary_container({}, std::move(chunks));
  const std::uint64_t directory_offset =
      read_little_endian<std::uint64_t>(encoded, 32u);
  ASSERT_EQ(encoded.at(static_cast<std::size_t>(directory_offset) + 6u),
            static_cast<std::byte>(QdrBinaryChunkCodec::Zstd));
  write_little_endian(
      encoded, static_cast<std::size_t>(directory_offset) + 32u,
      static_cast<std::uint64_t>(
          std::numeric_limits<std::uint32_t>::max()) +
          1u);

  const QdrBinaryContainerReadResult result =
      read_qdr_binary_container(std::move(encoded));
  ASSERT_FALSE(result.ok);
  ASSERT_FALSE(result.diagnostics.empty());
  EXPECT_EQ(result.diagnostics.front().code, "qdr.binary.chunk_too_large");
}

TEST(QdrBinaryContainerTests, RejectsOverlappingChunkRanges) {
  std::vector<std::byte> encoded = sample_container();
  std::uint64_t directory_offset = 0u;
  for (std::size_t index = 0u; index < sizeof(directory_offset); ++index) {
    directory_offset |= static_cast<std::uint64_t>(
                            std::to_integer<std::uint8_t>(encoded[32u + index]))
                        << (index * 8u);
  }
  ASSERT_LT(directory_offset + kQdrBinaryDirectoryEntrySize * 2u,
            encoded.size() + 1u);
  // Make the second chunk start at the first chunk's offset.
  std::uint64_t first_offset = 0u;
  for (std::size_t index = 0u; index < sizeof(first_offset); ++index) {
    first_offset |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(
                        encoded[static_cast<std::size_t>(directory_offset) +
                                16u + index]))
                    << (index * 8u);
  }
  write_little_endian(
      encoded,
      static_cast<std::size_t>(directory_offset) +
          kQdrBinaryDirectoryEntrySize + 16u,
      first_offset);
  for (std::size_t index = 0u; index < 4u; ++index) {
    encoded[static_cast<std::size_t>(directory_offset) +
            kQdrBinaryDirectoryEntrySize + 40u + index] =
        encoded[static_cast<std::size_t>(directory_offset) + 40u + index];
  }
  for (std::size_t index = 0u; index < 8u; ++index) {
    encoded[static_cast<std::size_t>(directory_offset) +
            kQdrBinaryDirectoryEntrySize + 48u + index] =
        encoded[static_cast<std::size_t>(directory_offset) + 48u + index];
  }

  const QdrBinaryContainerReadResult result =
      read_qdr_binary_container(std::move(encoded));
  ASSERT_FALSE(result.ok);
  ASSERT_FALSE(result.diagnostics.empty());
  EXPECT_EQ(result.diagnostics.front().code, "qdr.binary.overlapping_chunks");
}

} // namespace
} // namespace quader_io

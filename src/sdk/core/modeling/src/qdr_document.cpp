////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/io/qdr_document.hpp>

#include "polygon_document_native.hpp"

#include <quader/modeling/neutral/polygon/document_selection.hpp>
#include <quader/modeling/neutral/polygon/document_topology.hpp>
#include <quader/modeling/neutral/polygon/document_validation.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader::modeling {
namespace {

using ByteSpan = std::span<const std::byte>;

constexpr std::uint16_t kFormatMajor = 1;
constexpr std::uint16_t kFormatMinor = 0;
constexpr std::uint16_t kQgeoFormatMinor = 3;
constexpr std::uint32_t kNoFlags = 0;
constexpr std::size_t kQgeoHeaderSize = 56;
constexpr std::size_t kQpolHeaderSize = 88;
constexpr std::size_t kQdrmHeaderSize = 112;
constexpr std::size_t kObjectRecordSize = 56;
constexpr std::size_t kMaterialRecordSize = 24;
constexpr std::uint64_t kMaxPayloadSize =
    std::numeric_limits<std::uint32_t>::max();
constexpr std::uint32_t kMaxDocumentRecords = 1'000'000;
constexpr std::uint64_t kMaxNameSize = 1ULL << 20U;
constexpr std::uint64_t kMaxStringBlobSize = 64ULL << 20U;

constexpr std::array<std::byte, 4> kQgeoMagic{
    std::byte{static_cast<unsigned char>('Q')},
    std::byte{static_cast<unsigned char>('G')},
    std::byte{static_cast<unsigned char>('E')},
    std::byte{static_cast<unsigned char>('O')},
};
constexpr std::array<std::byte, 4> kQpolMagic{
    std::byte{static_cast<unsigned char>('Q')},
    std::byte{static_cast<unsigned char>('P')},
    std::byte{static_cast<unsigned char>('O')},
    std::byte{static_cast<unsigned char>('L')},
};
constexpr std::array<std::byte, 4> kQdrmMagic{
    std::byte{static_cast<unsigned char>('Q')},
    std::byte{static_cast<unsigned char>('D')},
    std::byte{static_cast<unsigned char>('R')},
    std::byte{static_cast<unsigned char>('M')},
};

/// Carries structured portable-codec failures through internal decode helpers.
class CodecFailure final : public std::runtime_error {
public:
  CodecFailure(ErrorCode code, std::string diagnostic, std::string message)
      : std::runtime_error(std::move(message)), code_(code),
        diagnostic_(std::move(diagnostic)) {}

  [[nodiscard]] ErrorCode code() const noexcept { return code_; }
  [[nodiscard]] const std::string &diagnostic() const noexcept {
    return diagnostic_;
  }

private:
  ErrorCode code_;
  std::string diagnostic_;
};

[[noreturn]] void fail(ErrorCode code, std::string_view diagnostic,
                       std::string message) {
  throw CodecFailure(code, std::string(diagnostic), std::move(message));
}

[[noreturn]] void malformed(std::string message) {
  fail(ErrorCode::IoError, "qdr.binary.malformed", std::move(message));
}

[[noreturn]] void unsupported_format(std::string message) {
  fail(ErrorCode::IoError, "qdr.binary.unsupported_format",
       std::move(message));
}

[[noreturn]] void unsupported_version(std::string message) {
  fail(ErrorCode::UnsupportedOperation, "qdr.binary.unsupported_version",
       std::move(message));
}

[[noreturn]] void invalid_document(std::string message) {
  fail(ErrorCode::ValidationFailed, "qdr.binary.validation",
       std::move(message));
}

[[nodiscard]] Error codec_error(const CodecFailure &failure) {
  Error error = make_error(failure.code(), failure.what());
  error.diagnostics.push_back({
      .code = failure.diagnostic(),
      .severity = DiagnosticSeverity::Error,
      .message = failure.what(),
  });
  return error;
}

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
  if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left) {
    return false;
  }
  result = left * right;
  return true;
}

[[nodiscard]] std::uint64_t require_add(std::uint64_t left,
                                        std::uint64_t right,
                                        std::string_view label) {
  std::uint64_t result = 0;
  if (!checked_add(left, right, result)) {
    malformed(std::string(label) + " size overflows.");
  }
  return result;
}

[[nodiscard]] std::uint64_t require_multiply(std::uint64_t left,
                                             std::uint64_t right,
                                             std::string_view label) {
  std::uint64_t result = 0;
  if (!checked_multiply(left, right, result)) {
    malformed(std::string(label) + " size overflows.");
  }
  return result;
}

[[nodiscard]] std::size_t require_size(std::uint64_t value,
                                       std::string_view label) {
  if (value > static_cast<std::uint64_t>(
                  std::numeric_limits<std::size_t>::max())) {
    malformed(std::string(label) + " does not fit this platform.");
  }
  return static_cast<std::size_t>(value);
}

[[nodiscard]] std::uint32_t require_u32(std::size_t value,
                                        std::string_view label) {
  if (value > std::numeric_limits<std::uint32_t>::max()) {
    invalid_document(std::string(label) + " exceeds the binary format limit.");
  }
  return static_cast<std::uint32_t>(value);
}

[[nodiscard]] std::uint64_t align_up(std::uint64_t value,
                                     std::uint64_t alignment) {
  const std::uint64_t mask = alignment - 1U;
  if (value > std::numeric_limits<std::uint64_t>::max() - mask) {
    malformed("Aligned payload size overflows.");
  }
  return (value + mask) & ~mask;
}

/// Writes portable modeling payloads in deterministic little-endian order.
class ByteWriter final {
public:
  explicit ByteWriter(std::size_t capacity = 0) { bytes_.reserve(capacity); }

  void write_u8(std::uint8_t value) {
    bytes_.push_back(std::byte{value});
  }

  void write_u16(std::uint16_t value) {
    for (unsigned int shift = 0; shift < 16; shift += 8) {
      write_u8(static_cast<std::uint8_t>(value >> shift));
    }
  }

  void write_u32(std::uint32_t value) {
    for (unsigned int shift = 0; shift < 32; shift += 8) {
      write_u8(static_cast<std::uint8_t>(value >> shift));
    }
  }

  void write_u64(std::uint64_t value) {
    for (unsigned int shift = 0; shift < 64; shift += 8) {
      write_u8(static_cast<std::uint8_t>(value >> shift));
    }
  }

  void write_f32(float value) {
    write_u32(std::bit_cast<std::uint32_t>(value));
  }

  template <std::size_t Size>
  void write_magic(const std::array<std::byte, Size> &magic) {
    bytes_.insert(bytes_.end(), magic.begin(), magic.end());
  }

  void write_bytes(ByteSpan bytes) {
    bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
  }

  void write_string(const std::string &value) {
    const auto *begin = reinterpret_cast<const std::byte *>(value.data());
    write_bytes(ByteSpan{begin, value.size()});
  }

  void pad_to(std::size_t alignment) {
    while ((bytes_.size() % alignment) != 0) {
      write_u8(0);
    }
  }

  void patch_u32(std::size_t offset, std::uint32_t value) {
    if (offset > bytes_.size() || bytes_.size() - offset < sizeof(value)) {
      throw std::logic_error("Binary writer patch is out of bounds.");
    }
    for (unsigned int index = 0; index < sizeof(value); ++index) {
      bytes_[offset + index] =
          std::byte{static_cast<std::uint8_t>(value >> (index * 8U))};
    }
  }

  [[nodiscard]] std::size_t size() const noexcept { return bytes_.size(); }
  [[nodiscard]] ByteSpan bytes() const noexcept { return bytes_; }
  [[nodiscard]] std::vector<std::byte> take() && { return std::move(bytes_); }

private:
  std::vector<std::byte> bytes_;
};

/// Reads bounds-checked little-endian values from immutable payload bytes.
class ByteReader final {
public:
  explicit ByteReader(ByteSpan bytes) : bytes_(bytes) {}

  [[nodiscard]] std::uint8_t read_u8(std::string_view label) {
    return std::to_integer<std::uint8_t>(read_bytes(1, label).front());
  }

  [[nodiscard]] std::uint16_t read_u16(std::string_view label) {
    std::uint16_t value = 0;
    const ByteSpan bytes = read_bytes(sizeof(value), label);
    for (unsigned int index = 0; index < sizeof(value); ++index) {
      value |= static_cast<std::uint16_t>(
                   std::to_integer<std::uint8_t>(bytes[index]))
               << (index * 8U);
    }
    return value;
  }

  [[nodiscard]] std::uint32_t read_u32(std::string_view label) {
    std::uint32_t value = 0;
    const ByteSpan bytes = read_bytes(sizeof(value), label);
    for (unsigned int index = 0; index < sizeof(value); ++index) {
      value |= static_cast<std::uint32_t>(
                   std::to_integer<std::uint8_t>(bytes[index]))
               << (index * 8U);
    }
    return value;
  }

  [[nodiscard]] std::uint64_t read_u64(std::string_view label) {
    std::uint64_t value = 0;
    const ByteSpan bytes = read_bytes(sizeof(value), label);
    for (unsigned int index = 0; index < sizeof(value); ++index) {
      value |= static_cast<std::uint64_t>(
                   std::to_integer<std::uint8_t>(bytes[index]))
               << (index * 8U);
    }
    return value;
  }

  [[nodiscard]] float read_f32(std::string_view label) {
    return std::bit_cast<float>(read_u32(label));
  }

  [[nodiscard]] ByteSpan read_bytes(std::uint64_t count,
                                    std::string_view label) {
    const std::size_t size = require_size(count, label);
    if (size > bytes_.size() - offset_) {
      malformed(std::string(label) + " is truncated.");
    }
    const ByteSpan result = bytes_.subspan(offset_, size);
    offset_ += size;
    return result;
  }

  template <std::size_t Size>
  void require_magic(const std::array<std::byte, Size> &expected,
                     std::string_view label) {
    const ByteSpan actual = read_bytes(Size, label);
    if (!std::equal(actual.begin(), actual.end(), expected.begin())) {
      unsupported_format(std::string(label) + " has unsupported magic.");
    }
  }

  void require_zero(std::uint64_t count, std::string_view label) {
    const ByteSpan bytes = read_bytes(count, label);
    if (std::any_of(bytes.begin(), bytes.end(), [](std::byte value) {
          return value != std::byte{0};
        })) {
      malformed(std::string(label) + " must be zero.");
    }
  }

  void require_end(std::string_view label) const {
    if (offset_ != bytes_.size()) {
      malformed(std::string(label) + " contains trailing bytes.");
    }
  }

  [[nodiscard]] std::size_t offset() const noexcept { return offset_; }
  [[nodiscard]] std::size_t remaining() const noexcept {
    return bytes_.size() - offset_;
  }

private:
  ByteSpan bytes_;
  std::size_t offset_ = 0;
};

[[nodiscard]] ByteSpan slice(ByteSpan bytes, std::uint64_t offset,
                             std::uint64_t size, std::string_view label) {
  const std::uint64_t end = require_add(offset, size, label);
  if (end > bytes.size()) {
    malformed(std::string(label) + " is out of bounds.");
  }
  return bytes.subspan(require_size(offset, label), require_size(size, label));
}

[[nodiscard]] std::uint32_t read_u32_at(ByteSpan bytes, std::size_t offset,
                                        std::string_view label) {
  if (offset > bytes.size() || bytes.size() - offset < sizeof(std::uint32_t)) {
    malformed(std::string(label) + " is out of bounds.");
  }
  std::uint32_t value = 0;
  for (unsigned int index = 0; index < sizeof(value); ++index) {
    value |= static_cast<std::uint32_t>(
                 std::to_integer<std::uint8_t>(bytes[offset + index]))
             << (index * 8U);
  }
  return value;
}

[[nodiscard]] std::uint64_t id_key(std::uint32_t index,
                                   std::uint32_t generation) {
  return static_cast<std::uint64_t>(generation) << 32U | index;
}

template <typename IdType>
void validate_optional_id(IdType id, std::string_view label) {
  if ((id.index == 0) != (id.generation == 0)) {
    invalid_document(std::string(label) +
                     " must be either fully invalid or fully live.");
  }
}

template <typename IdType>
void write_id(ByteWriter &writer, IdType id) {
  writer.write_u32(id.index);
  writer.write_u32(id.generation);
}

template <typename Tag>
[[nodiscard]] Id<Tag> read_id(ByteReader &reader, std::string_view label) {
  const std::uint32_t index = reader.read_u32(label);
  const std::uint32_t generation = reader.read_u32(label);
  Id<Tag> id{index, generation};
  if ((id.index == 0) != (id.generation == 0)) {
    malformed(std::string(label) +
              " must be either fully invalid or fully live.");
  }
  return id;
}

[[nodiscard]] constexpr auto make_crc32_table() {
  std::array<std::uint32_t, 256> table{};
  for (std::uint32_t value = 0; value < table.size(); ++value) {
    std::uint32_t crc = value;
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 1U) != 0
                ? 0xEDB88320U ^ (crc >> 1U)
                : crc >> 1U;
    }
    table[value] = crc;
  }
  return table;
}

constexpr auto kCrc32Table = make_crc32_table();

[[nodiscard]] std::uint32_t document_crc32(ByteSpan bytes) {
  std::uint32_t crc = 0xFFFFFFFFU;
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    const std::uint8_t value = index >= 24 && index < 28
                                   ? 0
                                   : std::to_integer<std::uint8_t>(bytes[index]);
    crc = kCrc32Table[(crc ^ value) & 0xFFU] ^ (crc >> 8U);
  }
  return crc ^ 0xFFFFFFFFU;
}

void write_common_header(ByteWriter &writer,
                         const std::array<std::byte, 4> &magic,
                         std::uint32_t header_size, std::uint64_t total_size,
                         std::uint16_t minor = kFormatMinor) {
  writer.write_magic(magic);
  writer.write_u16(kFormatMajor);
  writer.write_u16(minor);
  writer.write_u32(header_size);
  writer.write_u32(kNoFlags);
  writer.write_u64(total_size);
}

std::uint16_t read_common_header(ByteReader &reader,
                        const std::array<std::byte, 4> &magic,
                        std::uint32_t header_size, std::uint64_t total_size,
                        std::string_view label,
                        std::uint16_t supported_minor = kFormatMinor) {
  reader.require_magic(magic, label);
  const std::uint16_t major = reader.read_u16("binary major version");
  const std::uint16_t minor = reader.read_u16("binary minor version");
  if (major != kFormatMajor || minor > supported_minor) {
    unsupported_version(std::string(label) + " version " +
                        std::to_string(major) + "." +
                        std::to_string(minor) + " is unsupported.");
  }
  if (reader.read_u32("binary header size") != header_size) {
    malformed(std::string(label) + " header size is invalid.");
  }
  if (reader.read_u32("binary flags") != kNoFlags) {
    malformed(std::string(label) + " contains unsupported flags.");
  }
  if (reader.read_u64("binary total size") != total_size) {
    malformed(std::string(label) + " total size does not match its buffer.");
  }
  if (total_size > kMaxPayloadSize) {
    malformed(std::string(label) + " exceeds the binary size limit.");
  }
  return minor;
}

[[nodiscard]] std::uint8_t shading_byte(quader_poly::SurfaceShading shading) {
  switch (shading) {
  case quader_poly::SurfaceShading::Flat:
    return 0;
  case quader_poly::SurfaceShading::Smooth:
    return 1;
  case quader_poly::SurfaceShading::Authored:
    return 2;
  }
  invalid_document("Polygon face has an unsupported shading mode.");
}

[[nodiscard]] quader_poly::SurfaceShading
shading_from_byte(std::uint8_t value) {
  switch (value) {
  case 0:
    return quader_poly::SurfaceShading::Flat;
  case 1:
    return quader_poly::SurfaceShading::Smooth;
  case 2:
    return quader_poly::SurfaceShading::Authored;
  default:
    malformed("Authored topology contains an unsupported shading value.");
  }
}

[[nodiscard]] std::uint8_t
selection_mode_byte(quader_poly::SelectionMode mode) {
  switch (mode) {
  case quader_poly::SelectionMode::Vertex:
    return 0;
  case quader_poly::SelectionMode::Edge:
    return 1;
  case quader_poly::SelectionMode::Face:
    return 2;
  }
  invalid_document("Polygon selection has an unsupported mode.");
}

[[nodiscard]] quader_poly::SelectionMode
selection_mode_from_byte(std::uint8_t value) {
  switch (value) {
  case 0:
    return quader_poly::SelectionMode::Vertex;
  case 1:
    return quader_poly::SelectionMode::Edge;
  case 2:
    return quader_poly::SelectionMode::Face;
  default:
    malformed("Polygon selection contains an unsupported mode.");
  }
}

[[nodiscard]] std::uint8_t element_kind_byte(quader_poly::ElementKind kind) {
  switch (kind) {
  case quader_poly::ElementKind::Vertex:
    return 0;
  case quader_poly::ElementKind::Edge:
    return 1;
  case quader_poly::ElementKind::Face:
    return 2;
  }
  invalid_document("Polygon selection has an unsupported active kind.");
}

[[nodiscard]] quader_poly::ElementKind
element_kind_from_byte(std::uint8_t value) {
  switch (value) {
  case 0:
    return quader_poly::ElementKind::Vertex;
  case 1:
    return quader_poly::ElementKind::Edge;
  case 2:
    return quader_poly::ElementKind::Face;
  default:
    malformed("Polygon selection contains an unsupported active kind.");
  }
}

[[nodiscard]] std::uint8_t document_selection_mode_byte(QdrSelectionMode mode) {
  switch (mode) {
  case QdrSelectionMode::Object:
    return 0;
  case QdrSelectionMode::Vertex:
    return 1;
  case QdrSelectionMode::Edge:
    return 2;
  case QdrSelectionMode::Face:
    return 3;
  }
  invalid_document("QDR document has an unsupported selection mode.");
}

[[nodiscard]] QdrSelectionMode
document_selection_mode_from_byte(std::uint8_t value) {
  switch (value) {
  case 0:
    return QdrSelectionMode::Object;
  case 1:
    return QdrSelectionMode::Vertex;
  case 2:
    return QdrSelectionMode::Edge;
  case 3:
    return QdrSelectionMode::Face;
  default:
    malformed("QDR document contains an unsupported selection mode.");
  }
}

void validate_finite(float value, std::string_view label,
                     bool decoding = false) {
  if (!std::isfinite(value)) {
    if (decoding) {
      malformed(std::string(label) + " must be finite.");
    }
    invalid_document(std::string(label) + " must be finite.");
  }
}

void validate_polygon_document(const quader_poly::Document &document,
                               bool decoding) {
  const auto reject = [decoding](std::string message) {
    if (decoding) {
      malformed(std::move(message));
    }
    invalid_document(std::move(message));
  };

  const quader_poly::PolygonDocumentValidationReport report =
      quader_poly::validate_polygon_document(
          document, quader_poly::PolygonDocumentValidationPolicy::AuthoredTopology);
  if (!report.ok()) {
    const auto issue = std::find_if(
        report.diagnostics.begin(), report.diagnostics.end(),
        [](const quader_poly::PolygonDocumentDiagnostic &diagnostic) {
          return diagnostic.severity ==
                 quader_poly::PolygonDocumentDiagnosticSeverity::Error;
        });
    reject(issue == report.diagnostics.end()
               ? "Polygon document validation failed."
               : issue->message);
  }

  std::uint32_t max_vertex_id = 0;
  std::unordered_set<std::uint32_t> vertex_ids;
  vertex_ids.reserve(document.vertices.size());
  for (const quader_poly::Vertex &vertex : document.vertices) {
    if (vertex.id == 0 || !vertex_ids.insert(vertex.id).second) {
      reject("Polygon document contains an invalid or duplicate vertex ID.");
    }
    max_vertex_id = std::max(max_vertex_id, vertex.id);
    validate_finite(vertex.position.x, "Polygon vertex position", decoding);
    validate_finite(vertex.position.y, "Polygon vertex position", decoding);
    validate_finite(vertex.position.z, "Polygon vertex position", decoding);
  }

  std::uint32_t max_face_id = 0;
  std::unordered_set<std::uint32_t> face_ids;
  face_ids.reserve(document.faces.size());
  for (const quader_poly::Face &face : document.faces) {
    if (face.id == 0 || !face_ids.insert(face.id).second) {
      reject("Polygon document contains an invalid or duplicate face ID.");
    }
    max_face_id = std::max(max_face_id, face.id);
    static_cast<void>(shading_byte(face.normal_shading));
    for (const quader::QVec2 uv : face.uvs) {
      validate_finite(uv.x, "Polygon face UV", decoding);
      validate_finite(uv.y, "Polygon face UV", decoding);
    }
  }

  if (document.next_vertex_id == 0 ||
      (max_vertex_id != 0 &&
       (max_vertex_id == std::numeric_limits<std::uint32_t>::max() ||
        document.next_vertex_id <= max_vertex_id))) {
    reject("Polygon next vertex ID is not greater than every live vertex ID.");
  }
  if (document.next_face_id == 0 ||
      (max_face_id != 0 &&
       (max_face_id == std::numeric_limits<std::uint32_t>::max() ||
        document.next_face_id <= max_face_id))) {
    reject("Polygon next face ID is not greater than every live face ID.");
  }

  std::unordered_set<std::uint64_t> edge_modes;
  edge_modes.reserve(document.hard_normal_edges.size() +
                     document.soft_normal_edges.size());
  const auto validate_edge_mode = [&](std::span<const quader_poly::Edge> edges,
                                      std::string_view mode) {
    for (quader_poly::Edge edge : edges) {
      const quader_poly::Edge canonical = quader_poly::make_edge(edge.a, edge.b);
      const std::uint64_t key =
          static_cast<std::uint64_t>(canonical.a) << 32U | canonical.b;
      if (edge != canonical || !edge_modes.insert(key).second) {
        reject("Polygon document contains a noncanonical, duplicate, or "
               "conflicting " +
               std::string(mode) + " edge.");
      }
    }
  };
  validate_edge_mode(document.hard_normal_edges, "hard");
  validate_edge_mode(document.soft_normal_edges, "soft");
}

[[nodiscard]] bool edge_less(quader_poly::Edge left,
                             quader_poly::Edge right) noexcept {
  return left.a < right.a || (left.a == right.a && left.b < right.b);
}

[[nodiscard]] std::vector<quader_poly::Edge>
sorted_edges(std::span<const quader_poly::Edge> edges) {
  std::vector<quader_poly::Edge> sorted(edges.begin(), edges.end());
  std::ranges::sort(sorted, edge_less);
  return sorted;
}

void migrate_legacy_soft_normal_edges(quader_poly::Document &document) {
  struct Incidence {
    quader_poly::Edge edge{};
    std::uint32_t smooth_face_count = 0;
  };
  std::map<std::uint64_t, Incidence> incidences;
  for (const quader_poly::Face &face : document.faces) {
    if (face.vertices.size() < 2U) {
      continue;
    }
    for (std::size_t index = 0; index < face.vertices.size(); ++index) {
      const quader_poly::Edge edge = quader_poly::make_edge(
          face.vertices[index], face.vertices[(index + 1U) % face.vertices.size()]);
      const std::uint64_t key = static_cast<std::uint64_t>(edge.a) << 32U |
                                static_cast<std::uint64_t>(edge.b);
      Incidence &incidence = incidences[key];
      incidence.edge = edge;
      if (face.normal_shading == quader_poly::SurfaceShading::Smooth) {
        ++incidence.smooth_face_count;
      }
    }
  }

  const std::vector<quader_poly::Edge> hard_edges =
      sorted_edges(document.hard_normal_edges);
  document.soft_normal_edges.clear();
  document.soft_normal_edges.reserve(incidences.size());
  for (const auto &[key, incidence] : incidences) {
    static_cast<void>(key);
    if (incidence.smooth_face_count >= 2U &&
        !std::ranges::binary_search(hard_edges, incidence.edge, edge_less)) {
      document.soft_normal_edges.push_back(incidence.edge);
    }
  }
}

[[nodiscard]] std::vector<std::byte>
encode_authored_topology(const PolygonDocument &polygon) {
  const quader_poly::Document &document =
      PolygonDocumentNativeAccess::document(polygon);
  validate_polygon_document(document, false);

  const std::vector<quader_poly::Edge> hard_edges =
      sorted_edges(document.hard_normal_edges);
  const std::vector<quader_poly::Edge> soft_edges =
      sorted_edges(document.soft_normal_edges);

  const std::uint32_t vertex_count =
      require_u32(document.vertices.size(), "Polygon vertex count");
  const std::uint32_t face_count =
      require_u32(document.faces.size(), "Polygon face count");
  const std::uint32_t hard_edge_count =
      require_u32(hard_edges.size(), "Polygon hard-edge count");
  const std::uint32_t soft_edge_count =
      require_u32(soft_edges.size(), "Polygon soft-edge count");

  std::uint64_t corner_count = 0;
  std::uint64_t uv_count = 0;
  for (const quader_poly::Face &face : document.faces) {
    corner_count = require_add(corner_count, face.vertices.size(),
                               "Polygon corner count");
    uv_count = require_add(uv_count, face.uvs.size(), "Polygon UV count");
  }
  if (corner_count > std::numeric_limits<std::uint32_t>::max() ||
      uv_count > std::numeric_limits<std::uint32_t>::max()) {
    invalid_document("Polygon corner or UV count exceeds the binary limit.");
  }

  std::uint64_t total_size = kQgeoHeaderSize;
  total_size = require_add(
      total_size, require_multiply(vertex_count, 4, "Polygon vertex IDs"),
      "Authored topology");
  total_size = require_add(
      total_size, require_multiply(vertex_count, 12, "Polygon positions"),
      "Authored topology");
  total_size = require_add(
      total_size, require_multiply(face_count, 4, "Polygon face IDs"),
      "Authored topology");
  total_size = require_add(
      total_size,
      require_multiply(static_cast<std::uint64_t>(face_count) + 1U, 4,
                       "Polygon face offsets"),
      "Authored topology");
  total_size = require_add(
      total_size, require_multiply(corner_count, 4, "Polygon corners"),
      "Authored topology");
  total_size = require_add(
      total_size,
      require_multiply(static_cast<std::uint64_t>(face_count) + 1U, 4,
                       "Polygon UV offsets"),
      "Authored topology");
  total_size = require_add(
      total_size, require_multiply(uv_count, 8, "Polygon UVs"),
      "Authored topology");
  total_size = require_add(
      total_size, require_multiply(face_count, 4, "Polygon materials"),
      "Authored topology");
  total_size = require_add(total_size, face_count, "Authored topology");
  total_size = align_up(total_size, 4);
  total_size = require_add(
      total_size,
      require_multiply(hard_edge_count, 8, "Polygon hard edges"),
      "Authored topology");
  total_size = require_add(
      total_size,
      require_multiply(soft_edge_count, 8, "Polygon soft edges"),
      "Authored topology");
  total_size = require_add(
      total_size, require_multiply(face_count, 20, "Face texture transforms"),
      "Authored topology");
  if (total_size > kMaxPayloadSize) {
    invalid_document("Authored topology exceeds the binary size limit.");
  }

  ByteWriter writer(require_size(total_size, "Authored topology"));
  write_common_header(writer, kQgeoMagic, kQgeoHeaderSize, total_size,
                      kQgeoFormatMinor);
  writer.write_u32(document.next_vertex_id);
  writer.write_u32(document.next_face_id);
  writer.write_u32(vertex_count);
  writer.write_u32(face_count);
  writer.write_u32(static_cast<std::uint32_t>(corner_count));
  writer.write_u32(static_cast<std::uint32_t>(uv_count));
  writer.write_u32(hard_edge_count);
  writer.write_u32(soft_edge_count);

  for (const quader_poly::Vertex &vertex : document.vertices) {
    writer.write_u32(vertex.id);
  }
  for (const quader_poly::Vertex &vertex : document.vertices) {
    writer.write_f32(vertex.position.x);
    writer.write_f32(vertex.position.y);
    writer.write_f32(vertex.position.z);
  }
  for (const quader_poly::Face &face : document.faces) {
    writer.write_u32(face.id);
  }
  std::uint32_t offset = 0;
  writer.write_u32(offset);
  for (const quader_poly::Face &face : document.faces) {
    offset += static_cast<std::uint32_t>(face.vertices.size());
    writer.write_u32(offset);
  }
  for (const quader_poly::Face &face : document.faces) {
    for (const quader_poly::ElementId id : face.vertices) {
      writer.write_u32(id);
    }
  }
  offset = 0;
  writer.write_u32(offset);
  for (const quader_poly::Face &face : document.faces) {
    offset += static_cast<std::uint32_t>(face.uvs.size());
    writer.write_u32(offset);
  }
  for (const quader_poly::Face &face : document.faces) {
    for (const quader::QVec2 uv : face.uvs) {
      writer.write_f32(uv.x);
      writer.write_f32(uv.y);
    }
  }
  for (const quader_poly::Face &face : document.faces) {
    writer.write_u32(face.material_slot);
  }
  for (const quader_poly::Face &face : document.faces) {
    writer.write_u8(shading_byte(face.normal_shading));
  }
  writer.pad_to(4);
  for (const quader_poly::Edge edge : hard_edges) {
    writer.write_u32(edge.a);
    writer.write_u32(edge.b);
  }
  for (const quader_poly::Edge edge : soft_edges) {
    writer.write_u32(edge.a);
    writer.write_u32(edge.b);
  }
  for (const quader_poly::Face &face : document.faces) {
    writer.write_f32(face.texture_transform.scale.x);
    writer.write_f32(face.texture_transform.scale.y);
    writer.write_f32(face.texture_transform.shift_uv.x);
    writer.write_f32(face.texture_transform.shift_uv.y);
    writer.write_f32(face.texture_transform.rotation_degrees);
  }
  if (writer.size() != total_size) {
    throw std::logic_error("Authored topology size calculation drifted.");
  }
  return std::move(writer).take();
}

[[nodiscard]] PolygonDocument decode_authored_topology(ByteSpan bytes) {
  if (bytes.size() > kMaxPayloadSize) {
    malformed("Authored topology exceeds the binary size limit.");
  }
  ByteReader reader(bytes);
  const auto minor = read_common_header(
      reader, kQgeoMagic, kQgeoHeaderSize, bytes.size(), "Authored topology",
      kQgeoFormatMinor);
  quader_poly::Document document;
  document.next_vertex_id = reader.read_u32("next vertex ID");
  document.next_face_id = reader.read_u32("next face ID");
  const std::uint32_t vertex_count = reader.read_u32("vertex count");
  const std::uint32_t face_count = reader.read_u32("face count");
  const std::uint32_t corner_count = reader.read_u32("corner count");
  const std::uint32_t uv_count = reader.read_u32("UV count");
  const std::uint32_t hard_edge_count = reader.read_u32("hard-edge count");
  const std::uint32_t soft_edge_count =
      reader.read_u32(minor >= 2U ? "soft-edge count"
                                  : "authored topology reserved field");
  if (minor < 2U && soft_edge_count != 0U) {
    malformed("Authored topology reserved field must be zero.");
  }

  const ByteSpan vertex_ids =
      reader.read_bytes(require_multiply(vertex_count, 4, "vertex IDs"),
                        "vertex IDs");
  const ByteSpan positions =
      reader.read_bytes(require_multiply(vertex_count, 12, "positions"),
                        "positions");
  const ByteSpan face_ids =
      reader.read_bytes(require_multiply(face_count, 4, "face IDs"),
                        "face IDs");
  const ByteSpan face_offsets = reader.read_bytes(
      require_multiply(static_cast<std::uint64_t>(face_count) + 1U, 4,
                       "face offsets"),
      "face offsets");
  const ByteSpan corners =
      reader.read_bytes(require_multiply(corner_count, 4, "corner IDs"),
                        "corner IDs");
  const ByteSpan uv_offsets = reader.read_bytes(
      require_multiply(static_cast<std::uint64_t>(face_count) + 1U, 4,
                       "UV offsets"),
      "UV offsets");
  const ByteSpan uvs =
      reader.read_bytes(require_multiply(uv_count, 8, "UV values"),
                        "UV values");
  const ByteSpan materials =
      reader.read_bytes(require_multiply(face_count, 4, "material slots"),
                        "material slots");
  const ByteSpan shading = reader.read_bytes(face_count, "shading values");
  const std::size_t shading_end = reader.offset();
  const std::size_t aligned_shading_end =
      static_cast<std::size_t>(align_up(shading_end, 4));
  reader.require_zero(aligned_shading_end - shading_end,
                      "authored topology padding");
  const ByteSpan hard_edges = reader.read_bytes(
      require_multiply(hard_edge_count, 8, "hard edges"), "hard edges");
  const ByteSpan soft_edges = minor >= 2U
      ? reader.read_bytes(require_multiply(soft_edge_count, 8, "soft edges"),
                          "soft edges")
      : ByteSpan{};
  const ByteSpan texture_transforms = minor >= 1U
      ? reader.read_bytes(require_multiply(face_count, 20,
                                           "face texture transforms"),
                          "face texture transforms")
      : ByteSpan{};
  reader.require_end("Authored topology");

  if (read_u32_at(face_offsets, 0, "first face offset") != 0 ||
      read_u32_at(face_offsets, static_cast<std::size_t>(face_count) * 4U,
                  "last face offset") != corner_count) {
    malformed("Polygon face CSR offsets do not span the corner array.");
  }
  if (read_u32_at(uv_offsets, 0, "first UV offset") != 0 ||
      read_u32_at(uv_offsets, static_cast<std::size_t>(face_count) * 4U,
                  "last UV offset") != uv_count) {
    malformed("Polygon UV CSR offsets do not span the UV array.");
  }

  document.vertices.resize(vertex_count);
  for (std::uint32_t index = 0; index < vertex_count; ++index) {
    quader_poly::Vertex &vertex = document.vertices[index];
    vertex.id = read_u32_at(vertex_ids, static_cast<std::size_t>(index) * 4U,
                            "vertex ID");
    vertex.position.x = std::bit_cast<float>(read_u32_at(
        positions, static_cast<std::size_t>(index) * 12U, "vertex position"));
    vertex.position.y = std::bit_cast<float>(read_u32_at(
        positions, static_cast<std::size_t>(index) * 12U + 4U,
        "vertex position"));
    vertex.position.z = std::bit_cast<float>(read_u32_at(
        positions, static_cast<std::size_t>(index) * 12U + 8U,
        "vertex position"));
  }

  document.faces.resize(face_count);
  std::uint32_t previous_corner_offset = 0;
  std::uint32_t previous_uv_offset = 0;
  for (std::uint32_t index = 0; index < face_count; ++index) {
    const std::uint32_t corner_end = read_u32_at(
        face_offsets, static_cast<std::size_t>(index + 1U) * 4U,
        "face offset");
    const std::uint32_t uv_end = read_u32_at(
        uv_offsets, static_cast<std::size_t>(index + 1U) * 4U, "UV offset");
    if (corner_end < previous_corner_offset || corner_end > corner_count ||
        uv_end < previous_uv_offset || uv_end > uv_count) {
      malformed("Polygon CSR offsets are not monotonic and in bounds.");
    }
    const std::uint32_t face_corners = corner_end - previous_corner_offset;
    const std::uint32_t face_uvs = uv_end - previous_uv_offset;
    if (face_corners < 3 || (face_uvs != 0 && face_uvs != face_corners)) {
      malformed("Polygon face has invalid corner or UV CSR spans.");
    }

    quader_poly::Face &face = document.faces[index];
    face.id = read_u32_at(face_ids, static_cast<std::size_t>(index) * 4U,
                          "face ID");
    face.material_slot = read_u32_at(
        materials, static_cast<std::size_t>(index) * 4U, "material slot");
    face.normal_shading =
        shading_from_byte(std::to_integer<std::uint8_t>(shading[index]));
    if (minor >= 1U) {
      const std::size_t offset = static_cast<std::size_t>(index) * 20U;
      const auto value = [&](std::size_t component) {
        return std::bit_cast<float>(read_u32_at(
            texture_transforms, offset + component * 4U,
            "face texture transform"));
      };
      face.texture_transform = {
          .scale = {value(0U), value(1U)},
          .shift_uv = {value(2U), value(3U)},
          .rotation_degrees = value(4U),
      };
    }
    face.vertices.reserve(face_corners);
    for (std::uint32_t corner = previous_corner_offset; corner < corner_end;
         ++corner) {
      face.vertices.push_back(read_u32_at(
          corners, static_cast<std::size_t>(corner) * 4U, "corner vertex ID"));
    }
    face.uvs.reserve(face_uvs);
    for (std::uint32_t uv = previous_uv_offset; uv < uv_end; ++uv) {
      face.uvs.push_back({
          std::bit_cast<float>(read_u32_at(
              uvs, static_cast<std::size_t>(uv) * 8U, "UV value")),
          std::bit_cast<float>(read_u32_at(
              uvs, static_cast<std::size_t>(uv) * 8U + 4U, "UV value")),
      });
    }
    previous_corner_offset = corner_end;
    previous_uv_offset = uv_end;
  }

  document.hard_normal_edges.reserve(hard_edge_count);
  for (std::uint32_t index = 0; index < hard_edge_count; ++index) {
    document.hard_normal_edges.push_back({
        read_u32_at(hard_edges, static_cast<std::size_t>(index) * 8U,
                    "hard-edge endpoint"),
        read_u32_at(hard_edges, static_cast<std::size_t>(index) * 8U + 4U,
                    "hard-edge endpoint"),
    });
  }
  if (minor >= 2U) {
    document.soft_normal_edges.reserve(soft_edge_count);
    for (std::uint32_t index = 0; index < soft_edge_count; ++index) {
      document.soft_normal_edges.push_back({
          read_u32_at(soft_edges, static_cast<std::size_t>(index) * 8U,
                      "soft-edge endpoint"),
          read_u32_at(soft_edges, static_cast<std::size_t>(index) * 8U + 4U,
                      "soft-edge endpoint"),
      });
    }
  } else {
    migrate_legacy_soft_normal_edges(document);
  }
  if (minor < 3U) {
    // Before 1.3, implicit constructor defaults and an explicit Shade Flat
    // command both stored Flat. The encoding cannot distinguish them; recover
    // automatic edge normals without guessing from geometry or edge overrides.
    // Derive legacy explicit Soft edges above before changing face defaults.
    for (quader_poly::Face &face : document.faces) {
      if (face.normal_shading == quader_poly::SurfaceShading::Flat) {
        face.normal_shading = quader_poly::SurfaceShading::Authored;
      }
    }
  }
  std::ranges::sort(document.hard_normal_edges, edge_less);
  std::ranges::sort(document.soft_normal_edges, edge_less);
  validate_polygon_document(document, true);
  return PolygonDocumentNativeAccess::from_native(std::move(document));
}

void validate_selection(const quader_poly::Document &document,
                        const quader_poly::Selection &selection,
                        bool decoding) {
  const auto reject = [decoding](std::string message) {
    if (decoding) {
      malformed(std::move(message));
    }
    invalid_document(std::move(message));
  };

  std::unordered_set<std::uint32_t> vertices;
  vertices.reserve(selection.vertices.size());
  for (const std::uint32_t id : selection.vertices) {
    if (id == 0 || !vertices.insert(id).second) {
      reject("Polygon selection contains an invalid or duplicate vertex ID.");
    }
  }
  std::unordered_set<std::uint32_t> faces;
  faces.reserve(selection.faces.size());
  for (const std::uint32_t id : selection.faces) {
    if (id == 0 || !faces.insert(id).second) {
      reject("Polygon selection contains an invalid or duplicate face ID.");
    }
  }
  std::unordered_set<std::uint64_t> edges;
  edges.reserve(selection.edges.size());
  for (const quader_poly::Edge edge : selection.edges) {
    const quader_poly::Edge canonical = quader_poly::make_edge(edge.a, edge.b);
    const std::uint64_t key =
        static_cast<std::uint64_t>(canonical.a) << 32U | canonical.b;
    if (edge.a == 0 || edge.b == 0 || edge != canonical ||
        !edges.insert(key).second) {
      reject("Polygon selection contains an invalid or duplicate edge.");
    }
  }

  const auto liveness =
      quader_poly::validate_polygon_selection_liveness(document, selection);
  if (!liveness.ok()) {
    reject("Polygon selection references topology that is not live.");
  }

  if (!selection.has_active) {
    if (selection.active_vertex != 0 || selection.active_edge.a != 0 ||
        selection.active_edge.b != 0 || selection.active_face != 0) {
      reject("Inactive polygon selection contains active references.");
    }
    return;
  }
  switch (selection.active_kind) {
  case quader_poly::ElementKind::Vertex:
    if (selection.active_vertex == 0 || selection.active_edge.a != 0 ||
        selection.active_edge.b != 0 || selection.active_face != 0) {
      reject("Active vertex selection has inconsistent active references.");
    }
    break;
  case quader_poly::ElementKind::Edge:
    if (selection.active_vertex != 0 || selection.active_edge.a == 0 ||
        selection.active_edge.b == 0 ||
        selection.active_edge != quader_poly::make_edge(selection.active_edge.a,
                                                        selection.active_edge.b) ||
        selection.active_face != 0) {
      reject("Active edge selection has inconsistent active references.");
    }
    break;
  case quader_poly::ElementKind::Face:
    if (selection.active_vertex != 0 || selection.active_edge.a != 0 ||
        selection.active_edge.b != 0 || selection.active_face == 0) {
      reject("Active face selection has inconsistent active references.");
    }
    break;
  }
}

[[nodiscard]] std::vector<std::byte>
encode_polygon_state(const PolygonDocument &polygon) {
  const quader_poly::Document &document =
      PolygonDocumentNativeAccess::document(polygon);
  const quader_poly::Selection &selection =
      PolygonDocumentNativeAccess::selection(polygon);
  validate_selection(document, selection, false);
  const std::vector<std::byte> geometry = encode_authored_topology(polygon);

  const std::uint32_t vertex_count =
      require_u32(selection.vertices.size(), "Selected vertex count");
  const std::uint32_t edge_count =
      require_u32(selection.edges.size(), "Selected edge count");
  const std::uint32_t face_count =
      require_u32(selection.faces.size(), "Selected face count");
  std::uint64_t total_size = kQpolHeaderSize;
  total_size = require_add(total_size, geometry.size(), "Polygon state");
  total_size = require_add(
      total_size, require_multiply(vertex_count, 4, "Selected vertices"),
      "Polygon state");
  total_size = require_add(
      total_size, require_multiply(edge_count, 8, "Selected edges"),
      "Polygon state");
  total_size = require_add(
      total_size, require_multiply(face_count, 4, "Selected faces"),
      "Polygon state");
  if (total_size > kMaxPayloadSize) {
    invalid_document("Polygon state exceeds the binary size limit.");
  }

  const quader_poly::ElementId active_vertex =
      selection.has_active &&
              selection.active_kind == quader_poly::ElementKind::Vertex
          ? selection.active_vertex
          : 0;
  const quader_poly::Edge active_edge =
      selection.has_active &&
              selection.active_kind == quader_poly::ElementKind::Edge
          ? selection.active_edge
          : quader_poly::Edge{};
  const quader_poly::ElementId active_face =
      selection.has_active &&
              selection.active_kind == quader_poly::ElementKind::Face
          ? selection.active_face
          : 0;

  ByteWriter writer(require_size(total_size, "Polygon state"));
  write_common_header(writer, kQpolMagic, kQpolHeaderSize, total_size);
  writer.write_u64(geometry.size());
  writer.write_u64(PolygonDocumentNativeAccess::impl(polygon).content_revision);
  writer.write_u64(
      PolygonDocumentNativeAccess::impl(polygon).selection_revision);
  writer.write_u32(vertex_count);
  writer.write_u32(edge_count);
  writer.write_u32(face_count);
  writer.write_u8(selection_mode_byte(selection.mode));
  writer.write_u8(element_kind_byte(selection.active_kind));
  writer.write_u8(selection.has_active ? 1 : 0);
  writer.write_u8(0);
  writer.write_u32(active_vertex);
  writer.write_u32(active_edge.a);
  writer.write_u32(active_edge.b);
  writer.write_u32(active_face);
  writer.write_u64(0);
  writer.write_bytes(geometry);
  for (const std::uint32_t id : selection.vertices) {
    writer.write_u32(id);
  }
  for (const quader_poly::Edge edge : selection.edges) {
    writer.write_u32(edge.a);
    writer.write_u32(edge.b);
  }
  for (const std::uint32_t id : selection.faces) {
    writer.write_u32(id);
  }
  if (writer.size() != total_size) {
    throw std::logic_error("Polygon state size calculation drifted.");
  }
  return std::move(writer).take();
}

[[nodiscard]] PolygonDocument decode_polygon_state(ByteSpan bytes) {
  if (bytes.size() > kMaxPayloadSize) {
    malformed("Polygon state exceeds the binary size limit.");
  }
  ByteReader reader(bytes);
  read_common_header(reader, kQpolMagic, kQpolHeaderSize, bytes.size(),
                     "Polygon state");
  const std::uint64_t geometry_size = reader.read_u64("geometry size");
  const std::uint64_t content_revision = reader.read_u64("content revision");
  const std::uint64_t selection_revision =
      reader.read_u64("selection revision");
  const std::uint32_t vertex_count = reader.read_u32("selected vertex count");
  const std::uint32_t edge_count = reader.read_u32("selected edge count");
  const std::uint32_t face_count = reader.read_u32("selected face count");
  const quader_poly::SelectionMode mode =
      selection_mode_from_byte(reader.read_u8("polygon selection mode"));
  const quader_poly::ElementKind active_kind =
      element_kind_from_byte(reader.read_u8("active element kind"));
  const std::uint8_t has_active = reader.read_u8("has-active flag");
  if (has_active > 1) {
    malformed("Polygon has-active flag must be zero or one.");
  }
  if (reader.read_u8("polygon selection reserved byte") != 0) {
    malformed("Polygon selection reserved byte must be zero.");
  }
  const std::uint32_t active_vertex = reader.read_u32("active vertex");
  const std::uint32_t active_edge_a = reader.read_u32("active edge endpoint");
  const std::uint32_t active_edge_b = reader.read_u32("active edge endpoint");
  const std::uint32_t active_face = reader.read_u32("active face");
  if (reader.read_u64("polygon state reserved field") != 0) {
    malformed("Polygon state reserved field must be zero.");
  }

  const ByteSpan geometry = reader.read_bytes(geometry_size, "geometry payload");
  const ByteSpan vertices = reader.read_bytes(
      require_multiply(vertex_count, 4, "selected vertices"),
      "selected vertices");
  const ByteSpan edges = reader.read_bytes(
      require_multiply(edge_count, 8, "selected edges"), "selected edges");
  const ByteSpan faces = reader.read_bytes(
      require_multiply(face_count, 4, "selected faces"), "selected faces");
  reader.require_end("Polygon state");

  PolygonDocument polygon = decode_authored_topology(geometry);
  PolygonDocumentImpl &impl = PolygonDocumentNativeAccess::impl(polygon);
  const quader_poly::Document &document = *impl.document;
  if (vertex_count > document.vertices.size() ||
      face_count > document.faces.size() ||
      edge_count > quader_poly::document_edges(document).size()) {
    malformed("Polygon selection count exceeds its live topology.");
  }
  impl.content_revision = content_revision;
  impl.selection_revision = selection_revision;
  quader_poly::Selection selection;
  selection.mode = mode;
  selection.has_active = has_active != 0;
  selection.active_kind = active_kind;
  selection.active_vertex = active_vertex;
  selection.active_edge = {active_edge_a, active_edge_b};
  selection.active_face = active_face;
  selection.vertices.reserve(vertex_count);
  for (std::uint32_t index = 0; index < vertex_count; ++index) {
    selection.vertices.push_back(read_u32_at(
        vertices, static_cast<std::size_t>(index) * 4U, "selected vertex"));
  }
  selection.edges.reserve(edge_count);
  for (std::uint32_t index = 0; index < edge_count; ++index) {
    selection.edges.push_back({
        read_u32_at(edges, static_cast<std::size_t>(index) * 8U,
                    "selected edge"),
        read_u32_at(edges, static_cast<std::size_t>(index) * 8U + 4U,
                    "selected edge"),
    });
  }
  selection.faces.reserve(face_count);
  for (std::uint32_t index = 0; index < face_count; ++index) {
    selection.faces.push_back(read_u32_at(
        faces, static_cast<std::size_t>(index) * 4U, "selected face"));
  }
  validate_selection(document, selection, true);
  impl.selection = std::make_shared<const quader_poly::Selection>(
      std::move(selection));
  return polygon;
}

/// Holds a validated object-directory record before payload hydration.
struct ObjectRecord {
  ObjectId id{};
  MaterialId material{};
  bool selected = false;
  std::uint64_t name_offset = 0;
  std::uint64_t name_size = 0;
  std::uint64_t polygon_offset = 0;
  std::uint64_t polygon_size = 0;
};

/// Holds a validated material-directory record before payload hydration.
struct MaterialRecord {
  MaterialId id{};
  std::uint64_t name_offset = 0;
  std::uint64_t name_size = 0;
};

void validate_name(const std::string &name, std::string_view label) {
  if (name.size() > kMaxNameSize) {
    invalid_document(std::string(label) + " exceeds the name-size limit.");
  }
}

[[nodiscard]] std::string string_from_bytes(ByteSpan bytes) {
  if (bytes.empty()) {
    return {};
  }
  return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}

[[nodiscard]] std::vector<std::byte>
encode_document(const QdrDocumentDto &document) {
  if (document.objects.size() > kMaxDocumentRecords ||
      document.materials.size() > kMaxDocumentRecords) {
    invalid_document("QDR document contains too many objects or materials.");
  }
  const std::uint32_t object_count =
      static_cast<std::uint32_t>(document.objects.size());
  const std::uint32_t material_count =
      static_cast<std::uint32_t>(document.materials.size());
  static_cast<void>(document_selection_mode_byte(document.selection_mode));
  validate_optional_id(document.active_object, "Active object ID");

  std::unordered_set<std::uint32_t> material_indices;
  std::unordered_set<std::uint64_t> material_ids;
  material_indices.reserve(document.materials.size());
  material_ids.reserve(document.materials.size());
  std::uint64_t string_blob_size = 0;
  std::vector<std::uint64_t> material_name_offsets;
  material_name_offsets.reserve(document.materials.size());
  for (const QdrMaterialDto &material : document.materials) {
    if (!material.id.valid() ||
        !material_indices.insert(material.id.index).second) {
      invalid_document("QDR materials contain an invalid or duplicate ID.");
    }
    material_ids.insert(id_key(material.id.index, material.id.generation));
    validate_name(material.name, "Material name");
    material_name_offsets.push_back(string_blob_size);
    string_blob_size = require_add(string_blob_size, material.name.size(),
                                   "QDR string blob");
  }

  std::unordered_set<std::uint32_t> object_indices;
  std::unordered_set<std::uint64_t> object_ids;
  object_indices.reserve(document.objects.size());
  object_ids.reserve(document.objects.size());
  std::vector<std::uint64_t> object_name_offsets;
  std::vector<std::vector<std::byte>> polygon_payloads;
  object_name_offsets.reserve(document.objects.size());
  polygon_payloads.reserve(document.objects.size());
  for (const QdrObjectDto &object : document.objects) {
    if (!object.id.valid() || !object_indices.insert(object.id.index).second) {
      invalid_document("QDR objects contain an invalid or duplicate ID.");
    }
    object_ids.insert(id_key(object.id.index, object.id.generation));
    validate_optional_id(object.material, "Object material ID");
    if (object.material.valid() &&
        !material_ids.contains(
            id_key(object.material.index, object.material.generation))) {
      invalid_document("QDR object references a material dependency that is missing.");
    }
    validate_name(object.name, "Object name");
    object_name_offsets.push_back(string_blob_size);
    string_blob_size = require_add(string_blob_size, object.name.size(),
                                   "QDR string blob");
    polygon_payloads.push_back(encode_polygon_state(object.document));
  }
  if (string_blob_size > kMaxStringBlobSize) {
    invalid_document("QDR string blob exceeds the aggregate name-size limit.");
  }
  if (document.active_object.valid() &&
      !object_ids.contains(id_key(document.active_object.index,
                                  document.active_object.generation))) {
    invalid_document("QDR active object ID is not live.");
  }

  const std::uint64_t object_table_offset = kQdrmHeaderSize;
  const std::uint64_t object_table_size =
      require_multiply(object_count, kObjectRecordSize, "object table");
  const std::uint64_t material_table_offset =
      require_add(object_table_offset, object_table_size, "QDR tables");
  const std::uint64_t material_table_size =
      require_multiply(material_count, kMaterialRecordSize, "material table");
  const std::uint64_t string_blob_offset =
      require_add(material_table_offset, material_table_size, "QDR tables");
  const std::uint64_t polygon_blob_offset =
      align_up(require_add(string_blob_offset, string_blob_size,
                           "QDR string blob"),
               8);

  std::vector<std::uint64_t> polygon_offsets;
  polygon_offsets.reserve(polygon_payloads.size());
  std::uint64_t polygon_blob_size = 0;
  for (const std::vector<std::byte> &payload : polygon_payloads) {
    polygon_blob_size = align_up(polygon_blob_size, 8);
    polygon_offsets.push_back(polygon_blob_size);
    polygon_blob_size = require_add(polygon_blob_size, payload.size(),
                                    "QDR polygon blob");
  }
  const std::uint64_t total_size =
      require_add(polygon_blob_offset, polygon_blob_size, "QDR document");
  if (total_size > kMaxPayloadSize) {
    invalid_document("QDR document exceeds the binary size limit.");
  }

  ByteWriter writer(require_size(total_size, "QDR document"));
  write_common_header(writer, kQdrmMagic, kQdrmHeaderSize, total_size);
  writer.write_u32(0); // CRC is patched after the payload is complete.
  writer.write_u32(object_count);
  writer.write_u32(material_count);
  writer.write_u8(document_selection_mode_byte(document.selection_mode));
  writer.write_u8(0);
  writer.write_u8(0);
  writer.write_u8(0);
  write_id(writer, document.active_object);
  writer.write_u64(document.content_revision);
  writer.write_u64(document.selection_revision);
  writer.write_u64(object_table_offset);
  writer.write_u64(material_table_offset);
  writer.write_u64(string_blob_offset);
  writer.write_u64(string_blob_size);
  writer.write_u64(polygon_blob_offset);
  writer.write_u64(polygon_blob_size);

  for (std::size_t index = 0; index < document.objects.size(); ++index) {
    const QdrObjectDto &object = document.objects[index];
    write_id(writer, object.id);
    write_id(writer, object.material);
    writer.write_u32(object.selected ? 1U : 0U);
    writer.write_u32(0);
    writer.write_u64(object_name_offsets[index]);
    writer.write_u64(object.name.size());
    writer.write_u64(polygon_offsets[index]);
    writer.write_u64(polygon_payloads[index].size());
  }
  for (std::size_t index = 0; index < document.materials.size(); ++index) {
    const QdrMaterialDto &material = document.materials[index];
    write_id(writer, material.id);
    writer.write_u64(material_name_offsets[index]);
    writer.write_u64(material.name.size());
  }
  for (const QdrMaterialDto &material : document.materials) {
    writer.write_string(material.name);
  }
  for (const QdrObjectDto &object : document.objects) {
    writer.write_string(object.name);
  }
  while (writer.size() < polygon_blob_offset) {
    writer.write_u8(0);
  }
  for (std::size_t index = 0; index < polygon_payloads.size(); ++index) {
    while (writer.size() < polygon_blob_offset + polygon_offsets[index]) {
      writer.write_u8(0);
    }
    writer.write_bytes(polygon_payloads[index]);
  }
  if (writer.size() != total_size) {
    throw std::logic_error("QDR document size calculation drifted.");
  }
  writer.patch_u32(24, document_crc32(writer.bytes()));
  return std::move(writer).take();
}

[[nodiscard]] QdrDocumentDto decode_document(ByteSpan bytes) {
  if (bytes.size() > kMaxPayloadSize) {
    malformed("QDR document exceeds the binary size limit.");
  }
  ByteReader reader(bytes);
  read_common_header(reader, kQdrmMagic, kQdrmHeaderSize, bytes.size(),
                     "QDR document");
  const std::uint32_t expected_crc = reader.read_u32("document CRC32");
  if (expected_crc != document_crc32(bytes)) {
    malformed("QDR document checksum does not match its content.");
  }
  const std::uint32_t object_count = reader.read_u32("object count");
  const std::uint32_t material_count = reader.read_u32("material count");
  if (object_count > kMaxDocumentRecords ||
      material_count > kMaxDocumentRecords) {
    malformed("QDR document exceeds the object or material count limit.");
  }
  const QdrSelectionMode selection_mode = document_selection_mode_from_byte(
      reader.read_u8("document selection mode"));
  reader.require_zero(3, "QDR document reserved bytes");
  const ObjectId active_object = read_id<ObjectTag>(reader, "active object ID");
  const std::uint64_t content_revision = reader.read_u64("content revision");
  const std::uint64_t selection_revision =
      reader.read_u64("selection revision");
  const std::uint64_t object_table_offset =
      reader.read_u64("object table offset");
  const std::uint64_t material_table_offset =
      reader.read_u64("material table offset");
  const std::uint64_t string_blob_offset =
      reader.read_u64("string blob offset");
  const std::uint64_t string_blob_size = reader.read_u64("string blob size");
  const std::uint64_t polygon_blob_offset =
      reader.read_u64("polygon blob offset");
  const std::uint64_t polygon_blob_size = reader.read_u64("polygon blob size");

  const std::uint64_t expected_material_offset = require_add(
      kQdrmHeaderSize,
      require_multiply(object_count, kObjectRecordSize, "object table"),
      "QDR table layout");
  const std::uint64_t expected_string_offset = require_add(
      expected_material_offset,
      require_multiply(material_count, kMaterialRecordSize, "material table"),
      "QDR table layout");
  const std::uint64_t expected_polygon_offset = align_up(
      require_add(expected_string_offset, string_blob_size, "string blob"), 8);
  const std::uint64_t expected_total =
      require_add(expected_polygon_offset, polygon_blob_size, "polygon blob");
  if (object_table_offset != kQdrmHeaderSize ||
      material_table_offset != expected_material_offset ||
      string_blob_offset != expected_string_offset ||
      polygon_blob_offset != expected_polygon_offset ||
      expected_total != bytes.size() || string_blob_size > kMaxStringBlobSize) {
    malformed("QDR document section layout is invalid.");
  }

  const ByteSpan object_table = slice(
      bytes, object_table_offset,
      require_multiply(object_count, kObjectRecordSize, "object table"),
      "object table");
  const ByteSpan material_table = slice(
      bytes, material_table_offset,
      require_multiply(material_count, kMaterialRecordSize, "material table"),
      "material table");
  const ByteSpan string_blob =
      slice(bytes, string_blob_offset, string_blob_size, "string blob");
  const ByteSpan padding = slice(
      bytes, string_blob_offset + string_blob_size,
      polygon_blob_offset - (string_blob_offset + string_blob_size),
      "string padding");
  if (std::any_of(padding.begin(), padding.end(), [](std::byte value) {
        return value != std::byte{0};
      })) {
    malformed("QDR string padding must be zero.");
  }
  const ByteSpan polygon_blob =
      slice(bytes, polygon_blob_offset, polygon_blob_size, "polygon blob");

  std::vector<ObjectRecord> object_records;
  object_records.reserve(object_count);
  ByteReader object_reader(object_table);
  std::unordered_set<std::uint32_t> object_indices;
  std::unordered_set<std::uint64_t> object_ids;
  object_indices.reserve(object_count);
  object_ids.reserve(object_count);
  for (std::uint32_t index = 0; index < object_count; ++index) {
    ObjectRecord record;
    record.id = read_id<ObjectTag>(object_reader, "object ID");
    record.material = read_id<MaterialTag>(object_reader, "object material ID");
    const std::uint32_t flags = object_reader.read_u32("object flags");
    if ((flags & ~1U) != 0) {
      malformed("QDR object contains unsupported flags.");
    }
    record.selected = (flags & 1U) != 0;
    if (object_reader.read_u32("object reserved field") != 0) {
      malformed("QDR object reserved field must be zero.");
    }
    record.name_offset = object_reader.read_u64("object name offset");
    record.name_size = object_reader.read_u64("object name size");
    record.polygon_offset = object_reader.read_u64("object polygon offset");
    record.polygon_size = object_reader.read_u64("object polygon size");
    if (!record.id.valid() ||
        !object_indices.insert(record.id.index).second) {
      malformed("QDR objects contain an invalid or duplicate ID.");
    }
    object_ids.insert(id_key(record.id.index, record.id.generation));
    if (record.name_size > kMaxNameSize) {
      malformed("QDR object name exceeds the name-size limit.");
    }
    object_records.push_back(record);
  }
  object_reader.require_end("object table");

  std::vector<MaterialRecord> material_records;
  material_records.reserve(material_count);
  ByteReader material_reader(material_table);
  std::unordered_set<std::uint32_t> material_indices;
  std::unordered_set<std::uint64_t> material_ids;
  material_indices.reserve(material_count);
  material_ids.reserve(material_count);
  for (std::uint32_t index = 0; index < material_count; ++index) {
    MaterialRecord record;
    record.id = read_id<MaterialTag>(material_reader, "material ID");
    record.name_offset = material_reader.read_u64("material name offset");
    record.name_size = material_reader.read_u64("material name size");
    if (!record.id.valid() ||
        !material_indices.insert(record.id.index).second) {
      malformed("QDR materials contain an invalid or duplicate ID.");
    }
    material_ids.insert(id_key(record.id.index, record.id.generation));
    if (record.name_size > kMaxNameSize) {
      malformed("QDR material name exceeds the name-size limit.");
    }
    material_records.push_back(record);
  }
  material_reader.require_end("material table");

  std::uint64_t expected_name_offset = 0;
  for (const MaterialRecord &record : material_records) {
    if (record.name_offset != expected_name_offset) {
      malformed("QDR material names are not canonically packed.");
    }
    expected_name_offset =
        require_add(expected_name_offset, record.name_size, "material names");
  }
  for (const ObjectRecord &record : object_records) {
    if (record.name_offset != expected_name_offset) {
      malformed("QDR object names are not canonically packed.");
    }
    expected_name_offset =
        require_add(expected_name_offset, record.name_size, "object names");
  }
  if (expected_name_offset != string_blob_size) {
    malformed("QDR name records do not cover the string blob.");
  }

  std::uint64_t previous_polygon_end = 0;
  for (const ObjectRecord &record : object_records) {
    const std::uint64_t expected_polygon_record_offset =
        align_up(previous_polygon_end, 8);
    if (record.polygon_offset != expected_polygon_record_offset ||
        record.polygon_size < kQpolHeaderSize) {
      malformed("QDR polygon records are not canonically packed.");
    }
    const ByteSpan record_padding =
        slice(polygon_blob, previous_polygon_end,
              expected_polygon_record_offset - previous_polygon_end,
              "polygon padding");
    if (std::any_of(record_padding.begin(), record_padding.end(),
                    [](std::byte value) { return value != std::byte{0}; })) {
      malformed("QDR polygon padding must be zero.");
    }
    previous_polygon_end = require_add(
        expected_polygon_record_offset, record.polygon_size, "polygon records");
  }
  if (previous_polygon_end != polygon_blob_size) {
    malformed("QDR polygon records do not cover the polygon blob.");
  }

  QdrDocumentDto document;
  document.active_object = active_object;
  document.selection_mode = selection_mode;
  document.content_revision = content_revision;
  document.selection_revision = selection_revision;
  document.materials.reserve(material_count);
  for (const MaterialRecord &record : material_records) {
    document.materials.push_back({
        .id = record.id,
        .name = string_from_bytes(slice(string_blob, record.name_offset,
                                        record.name_size, "material name")),
    });
  }
  document.objects.reserve(object_count);
  for (const ObjectRecord &record : object_records) {
    if (record.material.valid() &&
        !material_ids.contains(
            id_key(record.material.index, record.material.generation))) {
      malformed("QDR object references a missing material dependency.");
    }
    document.objects.push_back({
        .id = record.id,
        .material = record.material,
        .selected = record.selected,
        .name = string_from_bytes(
            slice(string_blob, record.name_offset, record.name_size,
                  "object name")),
        .document = decode_polygon_state(
            slice(polygon_blob, record.polygon_offset, record.polygon_size,
                  "polygon state")),
    });
  }
  if (active_object.valid() &&
      !object_ids.contains(
          id_key(active_object.index, active_object.generation))) {
    malformed("QDR active object ID is not live.");
  }
  return document;
}

template <typename T, typename Operation>
[[nodiscard]] Result<T> run_codec(Operation operation, bool decoding) {
  try {
    return Result<T>::success(operation());
  } catch (const CodecFailure &failure) {
    return Result<T>::failure(codec_error(failure));
  } catch (const std::bad_alloc &) {
    return Result<T>::failure(make_error(
        decoding ? ErrorCode::IoError : ErrorCode::InternalError,
        "QDR binary codec could not allocate the requested memory."));
  } catch (const std::exception &exception) {
    return Result<T>::failure(make_error(
        decoding ? ErrorCode::IoError : ErrorCode::InternalError,
        std::string("QDR binary codec failed: ") + exception.what()));
  } catch (...) {
    return Result<T>::failure(make_error(
        decoding ? ErrorCode::IoError : ErrorCode::InternalError,
        "QDR binary codec failed with an unknown error."));
  }
}

} // namespace

Result<std::vector<std::byte>>
serialize_qdr_authored_topology(const PolygonDocument &document) {
  return run_codec<std::vector<std::byte>>(
      [&document] { return encode_authored_topology(document); }, false);
}

Result<PolygonDocument>
deserialize_qdr_authored_topology(std::span<const std::byte> bytes) {
  return run_codec<PolygonDocument>([bytes] { return decode_authored_topology(bytes); },
                                    true);
}

Result<std::vector<std::byte>>
serialize_qdr_document(const QdrDocumentDto &document) {
  return run_codec<std::vector<std::byte>>(
      [&document] { return encode_document(document); }, false);
}

Result<QdrDocumentDto>
deserialize_qdr_document(std::span<const std::byte> bytes) {
  return run_codec<QdrDocumentDto>([bytes] { return decode_document(bytes); },
                                   true);
}

Result<std::vector<std::byte>>
serialize_single_document_as_qdr(const PolygonDocument &document) {
  QdrDocumentDto dto;
  dto.objects.push_back({
      .id = make_id<ObjectTag>(1),
      .selected = true,
      .name = "Mesh",
      .document = document,
  });
  dto.active_object = make_id<ObjectTag>(1);
  dto.selection_mode = QdrSelectionMode::Object;
  dto.content_revision = document.content_revision();
  dto.selection_revision = document.selection_revision();
  return serialize_qdr_document(dto);
}

} // namespace quader::modeling

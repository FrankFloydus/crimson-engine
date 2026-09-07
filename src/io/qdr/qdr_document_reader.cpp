////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/qdr/qdr_document_reader.hpp"

#include "diagnostics/profile.hpp"
#include "io/qdr/qdr_binary_container.hpp"
#include "io/qdr/qdr_scene_binary_codec.hpp"
#include "io/shared_file_read.hpp"

#include <cstddef>
#include <chrono>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader {
namespace {

[[nodiscard]] QdrProjectReadResult
project_error_result(quader_io::QdrDiagnostic diagnostic) {
  const std::string message = diagnostic.message;
  return {
      .ok = false,
      .document = SceneContentDocument{},
      .message = message,
      .diagnostics = {std::move(diagnostic)},
  };
}

[[nodiscard]] QdrProjectReadResult
project_error_result(std::string_view code, std::string message) {
  return project_error_result(
      quader_io::qdr_make_error(code, "", std::move(message)));
}

[[nodiscard]] QdrProjectReadResult project_read_result_with_source_path(
    QdrProjectReadResult result, const std::filesystem::path &path) {
  for (quader_io::QdrDiagnostic &diagnostic : result.diagnostics) {
    if (!diagnostic.source_path.has_value()) {
      diagnostic.source_path = path;
    }
  }
  return result;
}

[[nodiscard]] std::vector<std::byte>
copy_bytes(std::span<const std::byte> bytes) {
  return {bytes.begin(), bytes.end()};
}

[[nodiscard]] QdrProjectReadResult
deserialize_owned_project_document(std::vector<std::byte> bytes) {
  QdrProjectReadStats stats;
  stats.file_bytes = bytes.size();
  const std::uint64_t source_content_hash =
      quader_io::qdr_binary_content_hash(bytes);
  const auto container_started = std::chrono::steady_clock::now();
  quader_io::QdrBinaryContainerReadResult container = [&]() {
    QDR_PROFILE_SCOPE("QDR validate binary container");
    return quader_io::read_qdr_binary_container(std::move(bytes));
  }();
  stats.container_access_and_verification_ms =
      std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - container_started)
          .count();
  if (!container.ok) {
    if (!container.diagnostics.empty()) {
      QdrProjectReadResult result =
          project_error_result(std::move(container.diagnostics.front()));
      result.stats = stats;
      return result;
    }
    QdrProjectReadResult result = project_error_result(
        "qdr.binary.read_failed",
        container.message.empty() ? "Could not read QDR binary scene."
                                  : std::move(container.message));
    result.stats = stats;
    return result;
  }

  stats.stored_chunk_bytes = container.stored_chunk_bytes;
  stats.raw_chunk_bytes = container.raw_chunk_bytes;
  stats.decompressed_bytes = container.decompressed_bytes;
  stats.compressed_chunk_count = container.compressed_chunk_count;
  stats.decompression_ms = container.decompression_ms;
  const std::array<std::byte, 16> scene_uuid = container.scene_uuid;
  QdrSceneBinaryDecodeResult decoded = [&]() {
    QDR_PROFILE_SCOPE("QDR hydrate binary scene");
    return decode_qdr_scene_binary_with_stats(container);
  }();
  stats.logical_geometry_count = decoded.stats.logical_geometry_count;
  stats.unique_geometry_decoded_count =
      decoded.stats.unique_geometry_decoded_count;
  stats.unique_geometry_compiled_count =
      decoded.stats.unique_geometry_compiled_count;
  stats.metadata_access_ms = decoded.stats.metadata_access_ms;
  stats.geometry_decode_ms = decoded.stats.geometry_decode_ms;
  stats.geometry_compile_ms = decoded.stats.geometry_compile_ms;
  stats.hydration_ms = decoded.stats.hydration_ms;
  stats.decompressed_bytes = 0u;
  stats.decompression_ms = 0.0;
  for (const quader_io::QdrBinaryChunk &chunk : container.chunks) {
    if (!chunk.was_decompressed()) {
      continue;
    }
    stats.decompressed_bytes += chunk.raw_size;
    stats.decompression_ms += chunk.decompression_time_ms();
  }
  return {
      .ok = true,
      .document = std::move(decoded.document),
      .message = {},
      .diagnostics = {},
      .scene_uuid = scene_uuid,
      .project_asset_dependencies =
          std::move(decoded.project_asset_dependencies),
      .viewport_camera_pose = std::move(decoded.viewport_camera_pose),
      .stats = stats,
      .source_byte_count = stats.file_bytes,
      .source_content_hash = source_content_hash,
  };
}

} // namespace

QdrProjectReadResult QdrDocumentReader::deserialize_project_document(
    std::span<const std::byte> bytes) {
  QDR_PROFILE_FUNCTION();
  try {
    return deserialize_owned_project_document(copy_bytes(bytes));
  } catch (const quader_io::QdrDiagnosticException &exception) {
    return project_error_result(exception.diagnostic());
  } catch (const std::exception &exception) {
    return project_error_result("qdr.binary.read_failed", exception.what());
  } catch (...) {
    return project_error_result("qdr.binary.unknown_error",
                                "Unknown error reading QDR binary scene.");
  }
}

QdrProjectReadResult QdrDocumentReader::read_project_document(
    const std::filesystem::path &path) {
  QDR_PROFILE_FUNCTION();
  const auto byte_read_started = std::chrono::steady_clock::now();
  try {
    // The opened source version is fully copied and closed before hydration;
    // a Godot import must not hold up a subsequent native atomic save.
    quader_io::SharedFileReadResult source =
        quader_io::read_file_bytes_shared(path);
    if (!source.ok) {
      std::string_view code = "qdr.file_read_failed";
      if (source.error == quader_io::SharedFileReadError::Open) {
        code = "qdr.file_open_failed";
      } else if (source.error == quader_io::SharedFileReadError::Size ||
                 source.error == quader_io::SharedFileReadError::Limit) {
        code = "qdr.binary.file_too_large";
      }
      return project_read_result_with_source_path(
          project_error_result(code, "Could not read QDR scene '" +
                                         path.string() + "': " + source.message),
          path);
    }
    const double byte_read_ms = std::chrono::duration<double, std::milli>(
                                    std::chrono::steady_clock::now() -
                                    byte_read_started)
                                    .count();
    QdrProjectReadResult result =
        deserialize_owned_project_document(std::move(source.bytes));
    result.stats.byte_read_ms = byte_read_ms;
    return project_read_result_with_source_path(std::move(result), path);
  } catch (const quader_io::QdrDiagnosticException &exception) {
    return project_read_result_with_source_path(
        project_error_result(exception.diagnostic()), path);
  } catch (const std::exception &exception) {
    return project_read_result_with_source_path(
        project_error_result("qdr.binary.read_failed", exception.what()),
        path);
  } catch (...) {
    return project_read_result_with_source_path(
        project_error_result("qdr.binary.unknown_error",
                             "Unknown error reading QDR binary scene."),
        path);
  }
}

} // namespace quader

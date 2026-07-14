////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/qdr/qdr_document_writer.hpp"

#include "diagnostics/profile.hpp"
#include "io/qdr/qdr_atomic_write.hpp"
#include "io/qdr/qdr_scene_binary_codec.hpp"

#include <cstddef>
#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace quader {
namespace {

[[nodiscard]] std::array<std::byte, 16>
standalone_scene_uuid(std::uint64_t scene_id) {
  std::array<std::byte, 16> uuid{};
  for (std::size_t index = 0u; index < sizeof(scene_id); ++index) {
    uuid[index] =
        static_cast<std::byte>((scene_id >> (index * 8u)) & 0xffu);
  }
  return uuid;
}

[[nodiscard]] std::string binary_string(std::span<const std::byte> bytes) {
  return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}

[[nodiscard]] QdrProjectWriteResult
project_write_result_from_atomic(quader_io::QdrAtomicWriteResult result) {
  return {result.ok, std::move(result.message)};
}

} // namespace

std::vector<std::byte> QdrDocumentWriter::serialize_project_document_bytes(
    const QdrProjectWriteSnapshot &snapshot) {
  QDR_PROFILE_FUNCTION();
  return serialize_project_document_with_stats(snapshot).bytes;
}

std::vector<std::byte> QdrDocumentWriter::serialize_project_document_bytes(
    const QdrProjectWriteSnapshot &snapshot,
    std::array<std::byte, 16> scene_uuid) {
  QDR_PROFILE_FUNCTION();
  return serialize_project_document_with_stats(snapshot, scene_uuid).bytes;
}

QdrSceneBinaryEncodeResult
QdrDocumentWriter::serialize_project_document_with_stats(
    const QdrProjectWriteSnapshot &snapshot) {
  QDR_PROFILE_FUNCTION();
  return encode_qdr_scene_binary(snapshot);
}

QdrSceneBinaryEncodeResult
QdrDocumentWriter::serialize_project_document_with_stats(
    const QdrProjectWriteSnapshot &snapshot,
    std::array<std::byte, 16> scene_uuid) {
  QDR_PROFILE_FUNCTION();
  return encode_qdr_scene_binary(snapshot, scene_uuid);
}

std::vector<std::byte> QdrDocumentWriter::serialize_project_document_bytes(
    const SceneContentDocument &document) {
  QDR_PROFILE_FUNCTION();
  return serialize_project_document_bytes(
      qdr_project_write_snapshot_from_document(document));
}

QdrProjectWriteResult QdrDocumentWriter::write_project_document_atomic(
    const std::filesystem::path &path,
    const QdrProjectWriteSnapshot &snapshot) {
  return write_project_document_atomic(
      path, snapshot, standalone_scene_uuid(snapshot.project_id));
}

QdrProjectWriteResult QdrDocumentWriter::write_project_document_atomic(
    const std::filesystem::path &path,
    const QdrProjectWriteSnapshot &snapshot,
    std::array<std::byte, 16> scene_uuid) {
  QDR_PROFILE_FUNCTION();
  try {
    const std::vector<std::byte> bytes = [&]() {
      QDR_PROFILE_SCOPE("QDR encode binary scene");
      return serialize_project_document_bytes(snapshot, scene_uuid);
    }();
    return project_write_result_from_atomic(quader_io::write_qdr_text_atomic(
        path, binary_string(bytes)));
  } catch (const std::exception &exception) {
    return {.ok = false, .message = exception.what()};
  } catch (...) {
    return {.ok = false,
            .message = "Unknown error writing QDR binary scene."};
  }
}

QdrProjectWriteResult QdrDocumentWriter::write_project_document_atomic(
    const std::filesystem::path &path,
    const SceneContentDocument &document) {
  QDR_PROFILE_FUNCTION();
  const SceneContentDocumentIdentityReport identity =
      document.validate_object_identity_invariants();
  if (!identity.ok()) {
    return {
        .ok = false,
        .message = identity.issues.empty()
                       ? "Scene object identity validation failed."
                       : identity.issues.front().message,
    };
  }
  const SceneContentDocumentAssetReferenceReport asset_references =
      document.validate_asset_references();
  if (!asset_references.ok()) {
    return {
        .ok = false,
        .message = asset_references.issues.empty()
                       ? "Scene asset reference validation failed."
                       : asset_references.issues.front().message,
    };
  }
  return write_project_document_atomic(
      path, qdr_project_write_snapshot_from_document(document));
}

QdrProjectWriteResult QdrDocumentWriter::write_project_document_atomic(
    const std::filesystem::path &path, const SceneContentDocument &document,
    std::array<std::byte, 16> scene_uuid) {
  const SceneContentDocumentIdentityReport identity =
      document.validate_object_identity_invariants();
  if (!identity.ok()) {
    return {.ok = false,
            .message = identity.issues.empty()
                           ? "Scene object identity validation failed."
                           : identity.issues.front().message};
  }
  const SceneContentDocumentAssetReferenceReport references =
      document.validate_asset_references();
  if (!references.ok()) {
    return {.ok = false,
            .message = references.issues.empty()
                           ? "Scene asset reference validation failed."
                           : references.issues.front().message};
  }
  return write_project_document_atomic(
      path, qdr_project_write_snapshot_from_document(document), scene_uuid);
}

} // namespace quader

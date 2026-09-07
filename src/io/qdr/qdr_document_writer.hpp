////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "io/qdr/qdr_scene_binary_codec.hpp"
#include "scene/document/scene_content_document.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace quader {

/**
 * Stores the QDR Project Write Result data contract used by the QDR document persistence layer.
 */
struct QdrProjectWriteResult {
    bool ok = false;
    std::string message;
    /// Identifies the exact serialized bytes after successful atomic publication.
    std::uint64_t source_byte_count = 0u;
    std::uint64_t source_content_hash = 0u;
};

/**
 * Owns QDR Document Writer behavior within the QDR document persistence layer.
 */
class QdrDocumentWriter final {
public:
  [[nodiscard]] static QdrSceneBinaryEncodeResult
  serialize_project_document_with_stats(
      const QdrProjectWriteSnapshot &snapshot);
  [[nodiscard]] static QdrSceneBinaryEncodeResult
  serialize_project_document_with_stats(
      const QdrProjectWriteSnapshot &snapshot,
      std::array<std::byte, 16> scene_uuid);
  [[nodiscard]] static std::vector<std::byte>
  serialize_project_document_bytes(const QdrProjectWriteSnapshot &snapshot);
  [[nodiscard]] static std::vector<std::byte> serialize_project_document_bytes(
      const QdrProjectWriteSnapshot &snapshot,
      std::array<std::byte, 16> scene_uuid);
  [[nodiscard]] static std::vector<std::byte>
  serialize_project_document_bytes(const SceneContentDocument &document);
  [[nodiscard]] static QdrProjectWriteResult
  write_project_document_atomic(const std::filesystem::path &path,
                                const QdrProjectWriteSnapshot &snapshot);
  [[nodiscard]] static QdrProjectWriteResult write_project_document_atomic(
      const std::filesystem::path &path,
      const QdrProjectWriteSnapshot &snapshot,
      std::array<std::byte, 16> scene_uuid);
  [[nodiscard]] static QdrProjectWriteResult
  write_project_document_atomic(const std::filesystem::path &path,
                                const SceneContentDocument &document);
  [[nodiscard]] static QdrProjectWriteResult write_project_document_atomic(
      const std::filesystem::path &path,
      const SceneContentDocument &document,
      std::optional<QdrSceneViewportCameraPose> viewport_camera_pose);
  [[nodiscard]] static QdrProjectWriteResult write_project_document_atomic(
      const std::filesystem::path &path, const SceneContentDocument &document,
      std::array<std::byte, 16> scene_uuid);
};

} // namespace quader

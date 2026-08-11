////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "io/qdr/qdr_diagnostic.hpp"
#include "io/qdr/qdr_scene_viewport_camera.hpp"
#include "io/project/project_asset_resolver.hpp"
#include "scene/document/scene_content_document.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace quader {

/// Measures each major phase of binary scene loading.
struct QdrProjectReadStats final {
  std::uint64_t file_bytes = 0;
  std::uint64_t stored_chunk_bytes = 0;
  std::uint64_t raw_chunk_bytes = 0;
  std::uint64_t decompressed_bytes = 0;
  std::size_t compressed_chunk_count = 0;
  std::size_t logical_geometry_count = 0;
  std::size_t unique_geometry_decoded_count = 0;
  std::size_t unique_geometry_compiled_count = 0;
  double byte_read_ms = 0.0;
  double container_access_and_verification_ms = 0.0;
  double decompression_ms = 0.0;
  double metadata_access_ms = 0.0;
  double geometry_decode_ms = 0.0;
  double geometry_compile_ms = 0.0;
  double hydration_ms = 0.0;
};

/**
 * Stores the QDR Project Read Result data contract used by the QDR document persistence layer.
 */
struct QdrProjectReadResult {
    bool ok = false;
    SceneContentDocument document;
    std::string message;
    std::vector<quader_io::QdrDiagnostic> diagnostics;
    std::array<std::byte, 16> scene_uuid{};
    std::vector<ProjectAssetReference> project_asset_dependencies;
    std::optional<QdrSceneViewportCameraPose> viewport_camera_pose;
    QdrProjectReadStats stats;
};

/**
 * Owns QDR Document Reader behavior within the QDR document persistence layer.
 */
class QdrDocumentReader final {
public:
  [[nodiscard]] static QdrProjectReadResult
  deserialize_project_document(std::span<const std::byte> bytes);
  [[nodiscard]] static QdrProjectReadResult
  read_project_document(const std::filesystem::path &path);
};

} // namespace quader

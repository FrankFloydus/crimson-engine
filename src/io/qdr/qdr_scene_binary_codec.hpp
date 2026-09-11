////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "io/qdr/qdr_binary_container.hpp"
#include "io/qdr/qdr_project_write_snapshot.hpp"
#include "io/qdr/qdr_scene_viewport_camera.hpp"
#include "scene/document/scene_content_document.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <vector>

namespace quader {

/// Returns encoded scene bytes and per-phase serialization measurements.
struct QdrSceneBinaryEncodeResult final {
  std::vector<std::byte> bytes;
  std::size_t logical_geometry_count = 0;
  std::size_t geometry_encode_count = 0;
  std::size_t unique_geometry_count = 0;
  std::size_t compressed_geometry_count = 0;
  std::uint64_t uncompressed_geometry_bytes = 0;
  std::uint64_t stored_geometry_bytes = 0;
  double geometry_encode_ms = 0.0;
  double geometry_deduplication_ms = 0.0;
  double metadata_encode_ms = 0.0;
  double container_build_and_compression_ms = 0.0;
};

/// Measures metadata, geometry, compilation, and hydration decode work.
struct QdrSceneBinaryDecodeStats final {
  std::size_t logical_geometry_count = 0;
  std::size_t unique_geometry_decoded_count = 0;
  std::size_t unique_geometry_compiled_count = 0;
  double metadata_access_ms = 0.0;
  double geometry_decode_ms = 0.0;
  double geometry_compile_ms = 0.0;
  double hydration_ms = 0.0;
};

/// Returns a hydrated scene and its external asset dependency seam.
struct QdrSceneBinaryDecodeResult final {
  SceneContentDocument document;
  std::vector<ProjectAssetReference> project_asset_dependencies;
  std::optional<QdrSceneViewportCameraPose> viewport_camera_pose;
  QdrSceneBinaryDecodeStats stats;
};

/// Project references read from validated metadata, including dormant bindings.
struct QdrSceneBinaryDependencyDecodeResult final {
  std::string scene_name;
  std::vector<ProjectAssetId> material_ids;
  std::vector<ProjectAssetReference> project_asset_dependencies;
  QdrSceneBinaryDecodeStats stats;
};

[[nodiscard]] QdrSceneBinaryEncodeResult
encode_qdr_scene_binary(const QdrProjectWriteSnapshot &snapshot);
[[nodiscard]] QdrSceneBinaryEncodeResult encode_qdr_scene_binary(
    const QdrProjectWriteSnapshot &snapshot,
    std::array<std::byte, 16> scene_uuid);

[[nodiscard]] SceneContentDocument decode_qdr_scene_binary(
    const quader_io::QdrBinaryContainerReadResult &container);
[[nodiscard]] QdrSceneBinaryDecodeResult decode_qdr_scene_binary_with_stats(
    const quader_io::QdrBinaryContainerReadResult &container);
[[nodiscard]] QdrSceneBinaryDependencyDecodeResult
decode_qdr_scene_binary_dependencies(
    const quader_io::QdrBinaryContainerReadResult &container);

} // namespace quader

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/release_features.hpp"
#include "io/qdr/qdr_diagnostic.hpp"
#include "io/qdr/qdr_scene_viewport_camera.hpp"
#include "io/project/project_asset_resolver.hpp"
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
    ProductRelease minimum_product_release = kInternalFullRelease;
    std::vector<ProjectAssetReference> project_asset_dependencies;
    std::optional<QdrSceneViewportCameraPose> viewport_camera_pose;
    QdrProjectReadStats stats;
    /// Identifies the exact bytes decoded, independently of later disk changes.
    std::uint64_t source_byte_count = 0u;
    std::uint64_t source_content_hash = 0u;
};

/// Verified release requirement without decoding or hydrating scene contents.
struct QdrProjectReleaseReadResult final {
  bool ok = false;
  ProductRelease minimum_product_release = kInternalFullRelease;
  std::string message;
};

/// Validated project references without decoding or compiling scene geometry.
struct QdrProjectDependencyReadResult final {
  bool ok = false;
  /// True when a validated dependency projection survived a broader read error.
  bool dependency_data_available = false;
  std::string message;
  std::vector<quader_io::QdrDiagnostic> diagnostics;
  std::array<std::byte, 16> scene_uuid{};
  std::string scene_name;
  std::vector<ProjectAssetId> material_ids;
  std::vector<ProjectAssetReference> project_asset_dependencies;
  std::uint64_t source_byte_count = 0u;
  std::uint64_t source_content_hash = 0u;
  QdrProjectReadStats stats;
};

/// Actual surface assignments, decoded without constructing render geometry.
/// Completeness describes this projection only, never general scene validity.
struct QdrProjectMaterialUsageReadResult final {
  bool ok = false;
  bool complete = false;
  std::string message;
  std::vector<quader_io::QdrDiagnostic> diagnostics;
  std::array<std::byte, 16> scene_uuid{};
  std::string scene_name;
  std::vector<ProjectAssetId> material_ids;
  std::vector<AssetId> embedded_material_ids;
  std::uint64_t source_byte_count = 0u;
  std::uint64_t source_content_hash = 0u;
  QdrProjectReadStats stats;
};

/// Slots present on authored faces or nonempty compiled primitives.
struct QdrMeshMaterialSlotsReadResult final {
  bool ok = false;
  std::string message;
  std::vector<std::uint32_t> material_slots;
};

/**
 * Owns QDR Document Reader behavior within the QDR document persistence layer.
 */
class QdrDocumentReader final {
public:
  [[nodiscard]] static QdrProjectReadResult
  deserialize_project_document(
      std::span<const std::byte> bytes,
      std::optional<ProductRelease> authoring_release = std::nullopt);
  [[nodiscard]] static QdrProjectReadResult
  read_project_document(
      const std::filesystem::path &path,
      std::optional<ProductRelease> authoring_release = std::nullopt);
  [[nodiscard]] static QdrProjectReleaseReadResult
  read_project_release_requirement(const std::filesystem::path &path);
  [[nodiscard]] static QdrProjectDependencyReadResult
  deserialize_project_dependencies(std::span<const std::byte> bytes);
  [[nodiscard]] static QdrProjectDependencyReadResult
  read_project_dependencies(const std::filesystem::path &path);
  [[nodiscard]] static QdrProjectMaterialUsageReadResult
  deserialize_project_material_usage(std::span<const std::byte> bytes);
  [[nodiscard]] static QdrProjectMaterialUsageReadResult
  read_project_material_usage(const std::filesystem::path &path);
  [[nodiscard]] static QdrMeshMaterialSlotsReadResult
  deserialize_mesh_material_slots(std::span<const std::byte> bytes,
                                  std::uint16_t scene_schema_version = 16u);
};

} // namespace quader

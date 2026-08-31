////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/material_document.hpp"
#include "assets/curve_asset.hpp"
#include "assets/mesh_asset.hpp"
#include "assets/texture_asset.hpp"
#include "io/project/project_asset_resolver.hpp"
#include "io/project/project_types.hpp"
#include "io/qdr/qdr_scene_viewport_camera.hpp"
#include "scene/document/scene_content_document.hpp"
#include "scene/document/scene_entity_state.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace quader {

/**
 * Captures one mesh asset for QDR project export by pinning its immutable
 * compiled and authored topology payloads.
 */
struct QdrProjectMeshAssetWriteSnapshot final {
  AssetId id = kInvalidAssetId;
  std::string name;
  bool has_stable_cpu_topology = true;
  bool has_authored_polygon_document = false;
  std::shared_ptr<const MeshGeometryPayload> geometry;
  std::shared_ptr<const quader_poly::Document> authored_polygon_document;
  /** Verified QDR geometry payload retained without decoding/compilation. */
  std::shared_ptr<const std::vector<std::byte>> encoded_geometry;
};

/// Pins the SDK-authored controls of one curve for background serialization.
struct QdrProjectCurveAssetWriteSnapshot final {
  AssetId id = kInvalidAssetId;
  std::string name;
  std::shared_ptr<const modeling::CurveDocument> document;
};

/**
 * Captures one material asset for QDR project export.
 */
struct QdrProjectMaterialAssetWriteSnapshot final {
  AssetId id = kInvalidAssetId;
  MaterialDocument material;
};

/**
 * Captures one texture asset for QDR project export.
 */
struct QdrProjectTextureAssetWriteSnapshot final {
  AssetId id = kInvalidAssetId;
  TextureAsset texture;
};

/**
 * Captures one native scene layer for deterministic QDR scene export.
 */
struct QdrProjectSceneLayerWriteSnapshot final {
  std::uint64_t id = 0;
  std::string name;
  bool visible = true;
  QVec3 wireframe_color = {238.0f / 255.0f, 242.0f / 255.0f,
                           247.0f / 255.0f};
  bool locked = false;
};

using QdrProjectEntityWriteSnapshot = SceneEntityState;

/**
 * Owns the immutable-by-contract data consumed by QDR project serialization and
 * background export workers.
 */
struct QdrProjectWriteSnapshot final {
  std::uint64_t project_id = 0;
  std::string project_name;
  std::vector<QdrProjectMeshAssetWriteSnapshot> meshes;
  std::vector<QdrProjectCurveAssetWriteSnapshot> curves;
  std::vector<QdrProjectMaterialAssetWriteSnapshot> materials;
  std::vector<QdrProjectTextureAssetWriteSnapshot> textures;
  std::vector<QdrProjectEntityWriteSnapshot> entities;
  std::vector<ProjectAssetReference> project_asset_dependencies;
  std::vector<QdrProjectSceneLayerWriteSnapshot> layers;
  std::optional<QdrSceneViewportCameraPose> viewport_camera_pose;
};

using QdrProjectWriteSnapshotResult = ProjectIoResult<QdrProjectWriteSnapshot>;

[[nodiscard]] QdrProjectWriteSnapshotResult
qdr_project_write_snapshot_from_document(const SceneContentDocument &document);

} // namespace quader

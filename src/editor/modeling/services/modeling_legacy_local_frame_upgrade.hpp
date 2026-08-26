////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cstdint>

namespace quader {

class AssetRegistry;
class ModelingMeshSyncService;
class SceneDocument;

/**
 * Counts the deterministic decisions made by one legacy local-frame upgrade.
 */
struct ModelingLegacyLocalFrameUpgradeStats final {
  std::uint64_t scanned_entity_count = 0u;
  std::uint64_t legacy_candidate_count = 0u;
  std::uint64_t ineligible_object_count = 0u;
  std::uint64_t unrecognized_object_count = 0u;
  std::uint64_t unrepresentable_object_count = 0u;
  std::uint64_t failed_object_count = 0u;
  std::uint64_t upgraded_object_count = 0u;
  std::uint64_t marker_only_object_count = 0u;
  std::uint64_t cloned_mesh_asset_count = 0u;
};

/**
 * Canonicalizes only legacy meshes that exactly replay an old Quader cube.
 *
 * The upgrade moves the recovered rigid construction frame from authored
 * vertices into the scene transform. It is deliberately conservative: an
 * unknown topology, custom pivot, or non-TRS compensation is left untouched.
 * Older schemas carry no creation provenance, so the exact cube source replay
 * is the accepted compatibility signature, including rigid whole-mesh edits.
 */
class ModelingLegacyLocalFrameUpgradeService final {
public:
  [[nodiscard]] static ModelingLegacyLocalFrameUpgradeStats
  upgrade_scene(SceneDocument &scene, AssetRegistry &assets,
                ModelingMeshSyncService &mesh_sync);
};

} // namespace quader

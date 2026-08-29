////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace quader {

/**
 * Stores the Quader Entity Snapshot data contract used by the public Quader API surface.
 */
struct QuaderEntitySnapshot {
  EntityId id = kInvalidEntityId;
  std::string uuid;
  QuaderEditorObjectId editor_object_id = kInvalidQuaderEditorObjectId;
  std::string name;
  QuaderEntityKind kind = QuaderEntityKind::Generic;
  EntityId parent = kInvalidEntityId;
  QuaderTransform transform;
  bool visible = true;
  bool selectable = true;
  bool locked = false;
  bool selected = false;
  bool active = false;
  bool has_mesh = false;
  bool has_material = false;
  bool has_light = false;
  bool is_scene_root = false;
  bool has_scene_settings = false;
  bool has_modeling_object = false;
  bool is_instance_definition = false;
  bool has_instance_occurrence = false;
  EntityId instance_definition = kInvalidEntityId;
  QuaderLayerId layer_id = kInvalidQuaderLayerId;
  AssetId mesh = kInvalidAssetId;
  AssetId material = kInvalidAssetId;
  QuaderLightComponent light;
  QuaderSceneSettings scene_settings;
  QuaderModelingObjectId modeling_object_id = kInvalidQuaderModelingObjectId;
  /// Per-occurrence texture placement; absence denotes the identity mapping.
  std::optional<QMat4> texture_sampling_from_local;
  bool has_curve = false;
  AssetId curve = kInvalidAssetId;
};

/**
 * Stores one scene display-layer projection for public callers.
 */
struct QuaderLayerSnapshot {
  QuaderLayerId id = kInvalidQuaderLayerId;
  std::string name;
  bool visible = true;
  QVec3 wireframe_color = kDefaultQuaderLayerWireframeColor;
  bool locked = false;
  std::vector<EntityId> members;
};

/**
 * Stores the Quader Mesh Snapshot data contract used by the public Quader API surface.
 */
struct QuaderMeshSnapshot {
  AssetId mesh = kInvalidAssetId;
  std::uint64_t revision = 0;
  std::size_t vertex_count = 0;
  std::size_t index_count = 0;
  std::size_t primitive_count = 0;
  bool stable_cpu_topology = false;
  bool has_bounds = false;
  QuaderBounds bounds;
};

/**
 * Stores the Quader Asset Snapshot data contract used by the public Quader API surface.
 */
struct QuaderAssetSnapshot {
  std::size_t mesh_count = 0;
  std::size_t material_count = 0;
  std::size_t texture_count = 0;
  std::uint64_t revision = 0;
  std::size_t curve_count = 0;
};

/**
 * Stores the Quader History Snapshot data contract used by the public Quader API surface.
 */
struct QuaderHistorySnapshot {
  bool can_undo = false;
  bool can_redo = false;
  std::string undo_label;
  std::string redo_label;
};

/**
 * Stores the Quader API Snapshot data contract used by the public Quader API surface.
 */
struct QuaderApiSnapshot {
    QuaderRevisionStamp revisions;
    std::vector<QuaderEntitySnapshot> entities;
    std::vector<QuaderLayerSnapshot> layers;
    std::vector<QuaderModelingObjectRecord> modeling_objects;
    QuaderSelectionSummary selection;
    QuaderToolStatus tool_status;
    QuaderAssetSnapshot assets;
    QuaderHistorySnapshot history;
    QuaderOverlayFrame selection_overlay;
};

} // namespace quader

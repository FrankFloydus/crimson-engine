////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "editor/modeling/runtime/modeling_object_runtime_map.hpp"
#include "editor/selection/modeling_selection_projection_snapshot.hpp"
#include "math/types.hpp"
#include "scene/scene_bounds.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>

namespace quader {

class AssetRegistry;
class SceneDocument;

} // namespace quader

namespace quader {

/** One immutable native selection edit derived by projection policy. */
struct SelectionProjectionEditResult {
  bool changed = false;
  ModelingSelectionProjectionSnapshot selection;
};

/**
 * Stores the outcome of projecting scene object selection into the modeling runtime.
 */
struct RuntimeObjectSelectionProjectionResult {
    bool ok = true;
    bool changed = false;
    bool has_modeling_objects = false;
    ModelingSelectionProjectionSnapshot selection;
};

/**
 * Counts the input-sized work used to project component selection bounds.
 *
 * These counters keep complexity regressions deterministic without relying on
 * wall-clock timing.
 */
struct SelectionBoundsWorkStats {
  std::uint64_t selected_object_visit_count = 0;
  std::uint64_t vertex_index_visit_count = 0;
  std::uint64_t selected_vertex_lookup_count = 0;
  std::uint64_t world_transform_evaluation_count = 0;
  std::uint64_t included_point_count = 0;
};

/**
 * Owns projection policy between scene selection state and runtime modeling selection state.
 */
class SelectionProjectionService final {
public:
  [[nodiscard]] bool should_project_object_selection_from_scene(
      const ModelingSelectionProjectionSnapshot &selection,
      bool force_object_mode) const;

  [[nodiscard]] SelectionProjectionEditResult
  project_object_selection_from_scene(
      const ModelingSelectionProjectionSnapshot &selection,
      const SceneDocument &scene,
      const ModelingObjectRuntimeMap &object_registry,
      bool force_object_mode = false) const;

  [[nodiscard]] RuntimeObjectSelectionProjectionResult
  project_object_selection_for_scene_entities(
      const ModelingSelectionProjectionSnapshot &selection,
      std::span<const EntityId> entities, EntityId active,
      const ModelingObjectRuntimeMap &object_registry) const;

  [[nodiscard]] SelectionProjectionEditResult promote_object_selection_for_tool(
      const ModelingSelectionProjectionSnapshot &selection,
      const SceneDocument &scene,
      const ModelingObjectRuntimeMap &object_registry) const;

  bool project_scene_selection_from_modeling(
      const ModelingSelectionProjectionSnapshot &selection,
      SceneDocument &scene,
      const ModelingObjectRuntimeMap &object_registry) const;

  bool restore_scene_selection(SceneDocument &scene,
                               std::span<const EntityId> selected,
                               EntityId active = kInvalidEntityId) const;

  bool add_scene_selection(SceneDocument &scene,
                           std::span<const EntityId> selected,
                           EntityId active = kInvalidEntityId) const;

  [[nodiscard]] std::optional<QVec3>
  world_selection_center(
                         const ModelingSelectionProjectionSnapshot &selection,
                         const SceneDocument &scene,
                         const AssetRegistry &assets,
                         const ModelingObjectRuntimeMap &object_registry) const;

  [[nodiscard]] std::optional<SceneBounds>
  world_selection_bounds(
                         const ModelingSelectionProjectionSnapshot &selection,
                         const SceneDocument &scene,
                         const AssetRegistry &assets,
                         const ModelingObjectRuntimeMap &object_registry,
                         SelectionBoundsWorkStats *work_stats = nullptr) const;

  /// Measures selected authored points after occurrence and reference-frame projection.
  [[nodiscard]] std::optional<SceneBounds> component_selection_bounds(
      const ModelingSelectionProjectionSnapshot &selection,
      const std::function<std::optional<QMat4>(int)> &world_transform,
      const std::function<QVec3(QVec3)> &reference_position = {},
      SelectionBoundsWorkStats *work_stats = nullptr) const;
};

} // namespace quader

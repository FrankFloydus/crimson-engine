////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "math/types.hpp"

#include <optional>
#include <span>

namespace quader {

class AssetRegistry;
class ModelingObjectRuntimeMap;
class SceneDocument;

} // namespace quader

namespace quader_editor_core {

class EditorSession;

} // namespace quader_editor_core

namespace quader {

/**
 * Stores the outcome of projecting scene object selection into the modeling runtime.
 */
struct RuntimeObjectSelectionProjectionResult {
    bool ok = true;
    bool changed = false;
    bool has_modeling_objects = false;
};

/**
 * Owns projection policy between scene selection state and runtime modeling selection state.
 */
class SelectionProjectionService final {
public:
  [[nodiscard]] bool should_project_object_selection_from_scene(
      const quader_editor_core::EditorSession &session,
      bool force_object_mode) const;

  bool project_object_selection_from_scene(
      quader_editor_core::EditorSession &session, const SceneDocument &scene,
      const ModelingObjectRuntimeMap &object_registry,
      bool force_object_mode = false) const;

  RuntimeObjectSelectionProjectionResult
  project_object_selection_for_scene_entities(
      quader_editor_core::EditorSession &session,
      std::span<const EntityId> entities, EntityId active,
      const ModelingObjectRuntimeMap &object_registry) const;

  bool promote_object_selection_for_tool(
      quader_editor_core::EditorSession &session, const SceneDocument &scene,
      const ModelingObjectRuntimeMap &object_registry) const;

  bool project_scene_selection_from_modeling(
      const quader_editor_core::EditorSession &session, SceneDocument &scene,
      const ModelingObjectRuntimeMap &object_registry) const;

  bool restore_scene_selection(SceneDocument &scene,
                               std::span<const EntityId> selected,
                               EntityId active = kInvalidEntityId) const;

  bool add_scene_selection(SceneDocument &scene,
                           std::span<const EntityId> selected,
                           EntityId active = kInvalidEntityId) const;

  [[nodiscard]] std::optional<QVec3>
  world_selection_center(const quader_editor_core::EditorSession &session,
                         const SceneDocument &scene,
                         const AssetRegistry &assets,
                         const ModelingObjectRuntimeMap &object_registry) const;
};

} // namespace quader

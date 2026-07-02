////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_types.hpp"
#include "core/types.hpp"

#include <functional>

namespace quader_editor_core {
class EditorSession;
}

namespace quader {

class DocumentCommandService;
class ModelingObjectRuntimeMap;
class SceneDocument;

/**
 * Stores runtime callbacks required to duplicate modeling-backed group hierarchies.
 */
struct ModelingHierarchyDuplicateRuntime {
  std::function<void()> ensure_scene_objects_synced;
  std::function<ModelingCommandResult(ModelingSelectionMode)>
      set_selection_mode;
  std::function<void()> project_object_selection_from_scene;
  std::function<ModelingCommandResult()> duplicate_selected_meshes;
};

/**
 * Owns modeling-backed hierarchy duplicate policy for group drag workflows.
 */
class ModelingHierarchyDuplicateService final {
public:
  ModelingCommandResult duplicate_selected_hierarchy_for_group_drag(
      quader_editor_core::EditorSession &session, SceneDocument &scene,
      ModelingObjectRuntimeMap &object_registry,
      DocumentCommandService &document_commands,
      ModelingHierarchyDuplicateRuntime runtime) const;
  ModelingCommandResult duplicate_selected_entity_hierarchy(
      quader_editor_core::EditorSession &session, SceneDocument &scene,
      ModelingObjectRuntimeMap &object_registry,
      DocumentCommandService &document_commands,
      ModelingHierarchyDuplicateRuntime runtime,
      ModelingSelectionMode result_selection_mode) const;
};

} // namespace quader

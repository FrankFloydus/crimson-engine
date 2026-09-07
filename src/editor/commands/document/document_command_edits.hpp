////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "scene/components.hpp"

#include <string>
#include <optional>

namespace quader {

/**
 * Represents a Document Transform Edit value used by the editor document command layer.
 */
struct DocumentTransformEdit {
  EntityId entity = kInvalidEntityId;
  TransformComponent before;
  TransformComponent after;
};

/**
 * Records exact optional placement mappings before and after a scene edit.
 * History replays these values without consulting current texture lock settings.
 */
struct DocumentTextureMappingEdit {
  EntityId entity = kInvalidEntityId;
  std::optional<TextureMappingComponent> before;
  std::optional<TextureMappingComponent> after;
};

/**
 * Represents a Document Visibility Edit value used by the editor document command layer.
 */
struct DocumentVisibilityEdit {
  EntityId entity = kInvalidEntityId;
  bool before = true;
  bool after = true;
};

/**
 * Represents a Document Lock Edit value used by the editor document command layer.
 */
struct DocumentLockEdit {
  EntityId entity = kInvalidEntityId;
  bool before = false;
  bool after = false;
};

/**
 * Enumerates Document Scene Batch Operation Kind values used by the document command layer.
 */
enum class DocumentSceneBatchOperationKind {
    Rename,
    Reparent,
    SetTransform,
    SetVisible,
    SetLocked,
    SetMeshComponent,
};

/**
 * Represents a Document Scene Batch Operation value used by the document command layer.
 */
struct DocumentSceneBatchOperation {
    DocumentSceneBatchOperationKind kind = DocumentSceneBatchOperationKind::Rename;
    EntityId entity = kInvalidEntityId;
    EntityId parent = kInvalidEntityId;
    bool preserve_world_transform = true;
    bool flag = false;
    TransformComponent transform;
    MeshComponent mesh;
    std::string name;
};

inline bool document_command_same_transform(const TransformComponent &left,
                                            const TransformComponent &right) {
  return left.position.x == right.position.x &&
         left.position.y == right.position.y &&
         left.position.z == right.position.z &&
         left.rotation_euler.x == right.rotation_euler.x &&
         left.rotation_euler.y == right.rotation_euler.y &&
         left.rotation_euler.z == right.rotation_euler.z &&
         left.scale.x == right.scale.x && left.scale.y == right.scale.y &&
         left.scale.z == right.scale.z &&
         left.custom_pivot == right.custom_pivot;
}

inline bool document_command_same_mesh_component(const MeshComponent &left,
                                                 const MeshComponent &right) {
  return left == right;
}

inline bool document_command_same_curve_component(const CurveComponent &left,
                                                  const CurveComponent &right) {
  return left == right;
}

inline bool document_command_same_light_component(const LightComponent &left,
                                                  const LightComponent &right) {
  return left == right;
}

inline bool
document_command_same_scene_settings(const SceneSettingsComponent &left,
                                     const SceneSettingsComponent &right) {
  return left == right;
}

} // namespace quader

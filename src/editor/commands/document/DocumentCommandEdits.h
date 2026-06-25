////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/Types.h"
#include "scene/Components.h"

#include <string>

namespace quader {

/**
 * Represents a Document Transform Edit value used by the editor document command layer.
 */
struct DocumentTransformEdit {
    EntityId entity = InvalidEntityId;
    TransformComponent before;
    TransformComponent after;
};

/**
 * Represents a Document Visibility Edit value used by the editor document command layer.
 */
struct DocumentVisibilityEdit {
    EntityId entity = InvalidEntityId;
    bool before = true;
    bool after = true;
};

/**
 * Represents a Document Lock Edit value used by the editor document command layer.
 */
struct DocumentLockEdit {
    EntityId entity = InvalidEntityId;
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
};

/**
 * Represents a Document Scene Batch Operation value used by the document command layer.
 */
struct DocumentSceneBatchOperation {
    DocumentSceneBatchOperationKind kind = DocumentSceneBatchOperationKind::Rename;
    EntityId entity = InvalidEntityId;
    EntityId parent = InvalidEntityId;
    bool preserveWorldTransform = true;
    bool flag = false;
    TransformComponent transform;
    std::string name;
};

inline bool documentCommandSameTransform(const TransformComponent& left, const TransformComponent& right) {
    return left.position.x == right.position.x &&
            left.position.y == right.position.y &&
            left.position.z == right.position.z &&
            left.rotationEuler.x == right.rotationEuler.x &&
            left.rotationEuler.y == right.rotationEuler.y &&
            left.rotationEuler.z == right.rotationEuler.z &&
            left.scale.x == right.scale.x &&
            left.scale.y == right.scale.y &&
            left.scale.z == right.scale.z;
}

inline bool documentCommandSameMeshComponent(const MeshComponent& left, const MeshComponent& right) {
    return left.mesh == right.mesh &&
            left.editorOnly == right.editorOnly &&
            left.castShadows == right.castShadows &&
            left.doubleSided == right.doubleSided &&
            left.generateCollisions == right.generateCollisions &&
            left.collisionType == right.collisionType &&
            left.displayCollision == right.displayCollision &&
            left.bodyType == right.bodyType;
}

} // namespace quader

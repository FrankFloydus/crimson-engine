////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/Types.h"
#include "scene/Components.h"

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

} // namespace quader

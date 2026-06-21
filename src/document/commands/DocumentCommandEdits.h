#pragma once

#include "core/Types.h"
#include "scene/Components.h"

namespace quader {

struct DocumentTransformEdit {
    EntityId entity = InvalidEntityId;
    TransformComponent before;
    TransformComponent after;
};

struct DocumentVisibilityEdit {
    EntityId entity = InvalidEntityId;
    bool before = true;
    bool after = true;
};

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

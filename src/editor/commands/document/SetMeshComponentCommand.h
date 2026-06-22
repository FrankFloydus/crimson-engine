////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/document/SceneCommand.h"

namespace quader {

/**
 * Applies Set Mesh Component Command behavior as a command in the editor document command layer.
 */
class SetMeshComponentCommand final : public SceneCommand {
public:
    SetMeshComponentCommand(SceneDocument& scene, EntityId entity, MeshComponent before, MeshComponent after)
        : SceneCommand(scene, "Set Mesh Component")
        , entity_(entity)
        , before_(before)
        , after_(after) {}

    DocumentCommandResult undo() override {
        return apply(before_);
    }

    DocumentCommandResult redo() override {
        if (sameMeshComponent(before_, after_)) {
            return documentCommandUnchangedResult();
        }
        return apply(after_);
    }

private:
    static bool sameMeshComponent(const MeshComponent& left, const MeshComponent& right) {
        return left.mesh == right.mesh &&
                left.editorOnly == right.editorOnly &&
                left.castShadows == right.castShadows &&
                left.doubleSided == right.doubleSided &&
                left.generateCollisions == right.generateCollisions &&
                left.collisionType == right.collisionType &&
                left.displayCollision == right.displayCollision &&
                left.bodyType == right.bodyType;
    }

    DocumentCommandResult apply(MeshComponent mesh) {
        if (!scene().exists(entity_)) {
            return documentCommandMissingEntityResult();
        }
        scene().setMesh(entity_, mesh);
        return documentCommandChangedResult();
    }

    EntityId entity_ = InvalidEntityId;
    MeshComponent before_;
    MeshComponent after_;
};

} // namespace quader

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/document/document_command_edits.hpp"
#include "editor/commands/document/scene_command.hpp"

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
      if (document_command_same_mesh_component(before_, after_)) {
        return document_command_unchanged_result();
      }
        return apply(after_);
    }

private:
    DocumentCommandResult apply(MeshComponent mesh) {
        if (!scene().exists(entity_)) {
          return document_command_missing_entity_result();
        }
        scene().set_mesh(entity_, mesh);
        return document_command_changed_result();
    }

    EntityId entity_ = kInvalidEntityId;
    MeshComponent before_;
    MeshComponent after_;
};

} // namespace quader

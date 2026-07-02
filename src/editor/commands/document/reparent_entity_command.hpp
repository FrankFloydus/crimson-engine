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
 * Applies Reparent Entity Command behavior as a command in the editor document command layer.
 */
class ReparentEntityCommand final : public SceneCommand {
public:
  ReparentEntityCommand(SceneDocument &scene, EntityId entity, EntityId parent,
                        bool preserve_world_transform = true)
      : SceneCommand(scene, "Reparent Entity"), entity_(entity),
        before_parent_(scene.parent(entity)), after_parent_(parent),
        preserve_world_transform_(preserve_world_transform) {
    if (const TransformComponent *transform = scene.transform(entity)) {
      before_transform_ = *transform;
      after_transform_ = *transform;
    }
  }

  ReparentEntityCommand(SceneDocument &scene, EntityId entity,
                        EntityId before_parent, EntityId after_parent,
                        TransformComponent before_transform,
                        TransformComponent after_transform)
      : SceneCommand(scene, "Reparent Entity"), entity_(entity),
        before_parent_(before_parent), after_parent_(after_parent),
        before_transform_(before_transform), after_transform_(after_transform),
        captured_after_transform_(true) {}

  DocumentCommandResult undo() override {
    return apply(before_parent_, before_transform_);
  }

    DocumentCommandResult redo() override {
        if (!scene().exists(entity_)) {
          return document_command_missing_entity_result();
        }
        if (after_parent_ != kInvalidEntityId &&
            !scene().exists(after_parent_)) {
          return document_command_invalid_request_result(
              "Parent entity does not exist");
        }
        if (before_parent_ == after_parent_ && !captured_after_transform_) {
          return document_command_unchanged_result();
        }
        if (captured_after_transform_) {
          return apply(after_parent_, after_transform_);
        }
        if (!scene().set_parent(entity_, after_parent_,
                                preserve_world_transform_)) {
          return document_command_invalid_request_result(
              "Entity cannot be reparented");
        }
        if (const TransformComponent* transform = scene().transform(entity_)) {
          after_transform_ = *transform;
          captured_after_transform_ = true;
        }
        return document_command_changed_result();
    }

private:
    DocumentCommandResult apply(EntityId parent, const TransformComponent& transform) {
        if (!scene().exists(entity_)) {
          return document_command_missing_entity_result();
        }
        if (parent != kInvalidEntityId && !scene().exists(parent)) {
          return document_command_invalid_request_result(
              "Parent entity does not exist");
        }
        if (!scene().set_parent(entity_, parent, false)) {
          return document_command_invalid_request_result(
              "Entity cannot be reparented");
        }
        scene().set_transform(entity_, transform);
        return document_command_changed_result();
    }

    EntityId entity_ = kInvalidEntityId;
    EntityId before_parent_ = kInvalidEntityId;
    EntityId after_parent_ = kInvalidEntityId;
    TransformComponent before_transform_;
    TransformComponent after_transform_;
    bool preserve_world_transform_ = true;
    bool captured_after_transform_ = false;
};

} // namespace quader

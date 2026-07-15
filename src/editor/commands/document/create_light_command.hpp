////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/document/document_command_scene_helpers.hpp"
#include "editor/commands/document/scene_command.hpp"
#include "scene/scene_transform.hpp"

#include <string>
#include <utility>

namespace quader {

/**
 * Applies Create Light Command behavior as a command in the editor document command layer.
 */
class CreateLightCommand final : public SceneCommand {
public:
  CreateLightCommand(SceneDocument &scene, std::string label, std::string name,
                     LightComponent light, EntityId parent = kInvalidEntityId,
                     TransformComponent transform = TransformComponent{},
                     bool select_created = false)
      : SceneCommand(scene, std::move(label)), name_(std::move(name)),
        light_(light), parent_(parent), transform_(transform),
        select_created_(select_created) {}

  [[nodiscard]] EntityId entity() const { return entity_; }

  DocumentCommandResult undo() override {
    if (entity_ == kInvalidEntityId || !scene().exists(entity_)) {
      return document_command_missing_entity_result();
    }
    if (!scene().destroy_entity(entity_)) {
      return document_command_missing_entity_result();
    }
    if (select_created_) {
      document_command_apply_selection(scene(), before_selection_);
    }
    return document_command_changed_result();
  }

    DocumentCommandResult redo() override {
      if (entity_ != kInvalidEntityId && scene().exists(entity_)) {
        return document_command_unchanged_result();
      }
      if (parent_ != kInvalidEntityId && !scene().exists(parent_)) {
        return document_command_invalid_request_result(
            "Parent entity does not exist");
      }
      if (!scene_transform_is_finite(transform_)) {
        return document_command_invalid_request_result(
            "Light transform must be finite");
      }

      before_selection_ = document_command_capture_selection(scene());
      entity_ = scene().create_entity_with_id(entity_, name_, EntityKind::Light,
                                              editor_object_id_);
      if (entity_ == kInvalidEntityId) {
        return document_command_invalid_request_result(
            "Light entity cannot be created");
      }
      if (!uuid_.empty() && !scene().set_entity_uuid(entity_, uuid_)) {
        scene().destroy_entity(entity_);
        entity_ = kInvalidEntityId;
        return document_command_invalid_request_result(
            "Light identity cannot be restored");
      }
        if (uuid_.empty()) {
            uuid_ = scene().uuid(entity_);
        }
        editor_object_id_ = scene().editor_object_id(entity_);
        if (parent_ != kInvalidEntityId &&
            !scene().set_parent(entity_, parent_, false)) {
          scene().destroy_entity(entity_);
          entity_ = kInvalidEntityId;
          return document_command_invalid_request_result(
              "Light cannot be reparented");
        }
        if (!scene().set_transform(entity_, transform_) ||
            !scene().set_light(entity_, light_)) {
          scene().destroy_entity(entity_);
          entity_ = kInvalidEntityId;
          return document_command_invalid_request_result(
              "Light components cannot be applied");
        }
        if (select_created_) {
          document_command_select_only(scene(), {entity_});
        }
        return document_command_changed_result();
    }

private:
    std::string name_;
    LightComponent light_;
    EntityId parent_ = kInvalidEntityId;
    TransformComponent transform_;
    bool select_created_ = false;
    EntityId entity_ = kInvalidEntityId;
    std::string uuid_;
    EditorObjectId editor_object_id_ = kInvalidEditorObjectId;
    DocumentSelectionState before_selection_;
};

} // namespace quader

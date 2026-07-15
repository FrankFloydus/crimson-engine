////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/document/scene_command.hpp"

#include <optional>

namespace quader {

/**
 * Applies Set Material Command behavior as a command in the editor document command layer.
 */
class SetMaterialCommand final : public SceneCommand {
public:
    SetMaterialCommand(
            SceneDocument& scene,
            EntityId entity,
            std::optional<MaterialComponent> before,
            MaterialComponent after)
        : SceneCommand(scene, "Set Material")
        , entity_(entity)
        , before_(before)
        , after_(after) {}

    DocumentCommandResult undo() override {
        return apply(before_);
    }

    DocumentCommandResult redo() override {
        if (before_.has_value() && *before_ == after_) {
          return document_command_unchanged_result();
        }
        return apply(after_);
    }

private:
    DocumentCommandResult apply(const std::optional<MaterialComponent>& material) {
        if (!scene().exists(entity_)) {
          return document_command_missing_entity_result();
        }
        if (!material.has_value()) {
          if (!scene().clear_material(entity_)) {
            return document_command_invalid_request_result(
                "Material component could not be cleared");
          }
          return document_command_changed_result();
        }
        if (!scene().set_material(entity_, *material)) {
          return document_command_invalid_request_result(
              "Material component could not be applied");
        }
        return document_command_changed_result();
    }

    DocumentCommandResult apply(MaterialComponent material) {
        if (!scene().exists(entity_)) {
          return document_command_missing_entity_result();
        }
        if (!scene().set_material(entity_, material)) {
          return document_command_invalid_request_result(
              "Material component could not be applied");
        }
        return document_command_changed_result();
    }

    EntityId entity_ = kInvalidEntityId;
    std::optional<MaterialComponent> before_;
    MaterialComponent after_;
};

} // namespace quader
